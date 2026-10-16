#include "NodeController.h"
#include "Protocol.h"
#include <QTime>
#include <cmath>

namespace { constexpr qint64 kStaleMs = 15000; }

NodeController::NodeController(QObject *parent) : QObject(parent)
{
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &NodeController::onTick);
    m_tick.start();
}

NodeController::~NodeController()
{
    if (m_mqtt) m_mqtt->stop();
}

int NodeController::dataAgeSec() const
{
    return m_hasTelemetry ? static_cast<int>(m_sinceTelemetry.elapsed() / 1000) : -1;
}

void NodeController::connectBroker(const QString &host, int port, const QString &user,
                                   const QString &pass, const QString &node)
{
    if (!Config::isValidId(node)) {
        addLog("Node ID không hợp lệ (chỉ a-z, 0-9, _, -; tối đa 32 ký tự)");
        return;
    }
    if (node == "pc") {
        addLog("Node ID pc dành cho ứng dụng PC; hãy dùng pi hoặc pi2");
        return;
    }
    if (host.trimmed().isEmpty() || port < 1 || port > 65535) {
        addLog("Host không được rỗng; cổng phải trong 1..65535");
        return;
    }
    if (user.isEmpty() || pass.isEmpty()) {
        addLog("Cần nhập user và mật khẩu");
        return;
    }
    disconnectBroker();

    m_cfg = Config();
    m_cfg.nodeId = "pc";
    m_cfg.host = host;
    m_cfg.port = static_cast<quint16>(port);
    m_cfg.user = user;
    m_cfg.password = pass;
    m_node = node;
    resetNodeInfo();

    m_mqtt = new MqttService(m_cfg, this);
    m_tracker = new CommandTracker(m_mqtt, m_cfg, this);

    connect(m_mqtt, &MqttService::stateChanged, this, &NodeController::onMqttState);
    connect(m_mqtt, &MqttService::messageReceived, this, &NodeController::onMessage);
    connect(m_mqtt, &MqttService::logMessage, this, &NodeController::addLog);
    connect(m_mqtt, &MqttService::publishFailed, this,
            [this](const QString &t, const QString &why) { addLog("Gửi thất bại " + t + ": " + why); });
    connect(m_tracker, &CommandTracker::finished, this, &NodeController::onCommandFinished);

    m_mqtt->subscribe(m_cfg.nodeTopic(m_node, "#"), 1);
    m_mqtt->start();
}

void NodeController::disconnectBroker()
{
    if (!m_mqtt) return;
    m_tracker->cancelAll();
    m_mqtt->stop();
    m_tracker->deleteLater();
    // Giữ service đủ lâu để offline QoS 1 được ACK trước khi hủy socket.
    MqttService *retiring = m_mqtt;
    disconnect(retiring, nullptr, this, nullptr);
    QTimer::singleShot(750, retiring, &QObject::deleteLater);
    m_tracker = nullptr;
    m_mqtt = nullptr;
    m_connState = "Disconnected";
    resetNodeInfo();
    emit connectionStateChanged();
}

void NodeController::ping()
{
    if (m_tracker) m_tracker->send(m_node, "ping");
}

void NodeController::setInterval(int ms)
{
    if (m_tracker) m_tracker->send(m_node, "set_interval", ms);
}

void NodeController::onMqttState(MqttService::State s)
{
    switch (s) {
    case MqttService::State::Disconnected:  m_connState = "Disconnected";  break;
    case MqttService::State::Connecting:    m_connState = "Connecting";    break;
    case MqttService::State::Connected:     m_connState = "Connected";     break;
    case MqttService::State::Reconnecting:  m_connState = "Reconnecting";  break;
    }
    if (s != MqttService::State::Connected)
        resetNodeInfo();                     // mất kết nối: không còn biết trạng thái node
    emit connectionStateChanged();
}

void NodeController::onMessage(const QString &topic, const QByteArray &payload)
{
    const QString prefix = m_cfg.nodeTopic(m_node, QString());       // "lab/<node>/"
    if (!topic.startsWith(prefix)) return;
    const QString suffix = topic.mid(prefix.size());

    if (suffix == "status") {
        m_statusPayload = QString::fromUtf8(payload);
        addLog("Trạng thái " + m_node + ": " + m_statusPayload);
        evaluateNodeState();
    } else if (suffix == "heartbeat") {
        QJsonObject heartbeat;
        if (!Protocol::parseObject(payload, heartbeat) || !Protocol::isHeartbeat(heartbeat)) return;
        m_hasHeartbeat = true;
        m_sinceHeartbeat.restart();
        evaluateNodeState();
    } else if (suffix == "sensor/cpu_temp") {
        QJsonObject o;
        if (!Protocol::parseObject(payload, o)) return;
        if (!o.value("valid").isBool()) return;
        m_tempValid = o.value("valid").toBool(false);
        const QJsonValue value = o.value("value");
        if (m_tempValid && (!value.isDouble() || !std::isfinite(value.toDouble())
                           || value.toDouble() < -20 || value.toDouble() > 125))
            m_tempValid = false;
        m_cpuTemp = m_tempValid ? QString::number(o.value("value").toDouble(), 'f', 1) : "--";
        m_hasTelemetry = true;
        m_sinceTelemetry.restart();
        emit telemetryChanged();
    } else if (m_tracker && m_tracker->handleMessage(topic, payload)) {
        // ACK đã được CommandTracker xử lý
    } else {
        addLog(topic + " → " + QString::fromUtf8(payload));
    }
}

void NodeController::onCommandFinished(const QString &id, const QString &command,
                                       const QString &status, const QString &detail, qint64 rttMs)
{
    QString text = command + " → " + status;
    if (!detail.isEmpty()) text += " (" + detail + ")";
    if (status == "success" && rttMs >= 0) text += QString(", %1 ms").arg(rttMs);
    m_lastResult = text;
    emit lastResultChanged();
    addLog(text + "  [" + id + "]");
}

void NodeController::onTick()
{
    evaluateNodeState();
    if (m_hasTelemetry) emit telemetryChanged();      // cập nhật tuổi dữ liệu
}

void NodeController::resetNodeInfo()
{
    m_statusPayload.clear();
    m_hasHeartbeat = false;
    m_hasTelemetry = false;
    m_cpuTemp = "--";
    m_tempValid = false;
    evaluateNodeState();
    emit telemetryChanged();
}

void NodeController::evaluateNodeState()
{
    QString s = "unknown";
    if (m_connState == "Connected") {
        const bool fresh = m_hasHeartbeat && m_sinceHeartbeat.elapsed() <= kStaleMs;
        if (m_statusPayload == "offline")      s = "offline";
        else if (m_statusPayload == "online")  s = fresh ? "online" : "stale";
    }
    if (s != m_nodeState) {
        m_nodeState = s;
        emit nodeStateChanged();
    }
}

void NodeController::addLog(const QString &s)
{
    m_log.prepend(QTime::currentTime().toString("hh:mm:ss") + "  " + s);
    if (m_log.size() > 200) m_log.removeLast();
    emit logChanged();
}
