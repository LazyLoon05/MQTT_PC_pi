#include "MqttService.h"
#include <QRandomGenerator>
#include <utility>

MqttService::MqttService(const Config &cfg, QObject *parent)
    : QObject(parent), m_cfg(cfg)
{
    qRegisterMetaType<MqttService::State>("MqttService::State");
    m_client.setHostname(cfg.host);
    m_client.setPort(cfg.port);
    if (!cfg.user.isEmpty()) m_client.setUsername(cfg.user);
    m_client.setPassword(cfg.password);
    m_client.setClientId(cfg.clientId());
    m_client.setProtocolVersion(QMqttClient::MQTT_3_1_1);
    m_client.setCleanSession(true);
    m_client.setKeepAlive(15);          // broker thường phát hiện mất kết nối sau ~1,5 × keepalive

    // Last Will: broker tự phát "offline" khi client biến mất đột ngột.
    m_client.setWillTopic(cfg.statusTopic());
    m_client.setWillMessage("offline");
    m_client.setWillQoS(1);
    m_client.setWillRetain(true);

    connect(&m_client, &QMqttClient::stateChanged, this, &MqttService::onClientState);
    connect(&m_client, &QMqttClient::messageReceived, this, &MqttService::onMessage);
    connect(&m_client, &QMqttClient::errorChanged, this, [this](QMqttClient::ClientError e) {
        if (e != QMqttClient::NoError)
            emit logMessage(QString("Lỗi MQTT (mã %1)").arg(static_cast<int>(e)));
    });

    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &MqttService::tryConnect);
    m_shutdown.setSingleShot(true);
    m_shutdown.setInterval(500);
    connect(&m_shutdown, &QTimer::timeout, &m_client, &QMqttClient::disconnectFromHost);
    connect(&m_client, &QMqttClient::messageSent, this, [this](qint32 id) {
        if (!m_wantRun && id == m_offlineMessageId) {
            m_offlineMessageId = -1;
            m_shutdown.stop();
            m_client.disconnectFromHost();
        }
    });
}

MqttService::~MqttService()
{
    // QMqttClient có thể phát stateChanged trong destructor; timer/member của
    // service sẽ bị hủy trước client, vì vậy phải tháo connection trước.
    m_reconnect.stop();
    m_shutdown.stop();
    disconnect(&m_client, nullptr, this, nullptr);
    m_client.disconnectFromHost();
}

void MqttService::start()
{
    if (m_wantRun) return;                 // chống gọi start() nhiều lần
    if (!Config::isValidId(m_cfg.nodeId) || !Config::isValidId(m_cfg.base)
        || m_cfg.host.trimmed().isEmpty() || m_cfg.port == 0) {
        emit logMessage("Cấu hình MQTT không hợp lệ");
        return;
    }
    m_wantRun = true;
    m_attempt = 0;
    setState(State::Connecting);
    tryConnect();
}

void MqttService::stop()
{
    m_wantRun = false;
    m_reconnect.stop();
    if (isConnected()) {
        // Chờ PUBACK trước DISCONNECT, có giới hạn thời gian khi mạng hỏng.
        m_offlineMessageId = m_client.publish(QMqttTopicName(m_cfg.statusTopic()), "offline", 1, true);
        if (m_offlineMessageId >= 0) m_shutdown.start();
        else m_client.disconnectFromHost();
    } else {
        m_client.disconnectFromHost();
        if (m_client.state() == QMqttClient::Disconnected) emit stopped();
    }
    setState(State::Disconnected);
}

void MqttService::tryConnect()
{
    if (!m_wantRun) return;
    if (m_client.state() != QMqttClient::Disconnected) return;   // không gọi chồng
    m_client.connectToHost();
}

void MqttService::scheduleReconnect()
{
    setState(State::Reconnecting);
    // Backoff: 1, 2, 4, 8, 16, 30, 30... giây, cộng jitter 0-500 ms.
    const int base = qMin(30000, 1000 << qMin(m_attempt, 5));
    const int delay = base + static_cast<int>(QRandomGenerator::global()->bounded(500));
    m_attempt = qMin(m_attempt + 1, 6);
    emit logMessage(QString("Không có kết nối; thử lại sau %1 ms").arg(delay));
    m_reconnect.start(delay);
}

bool MqttService::publish(const QString &topic, const QByteArray &payload,
                          quint8 qos, bool retain)
{
    if (!isConnected()) {
        emit publishFailed(topic, "chưa kết nối broker");
        return false;
    }
    if (m_client.publish(QMqttTopicName(topic), payload, qos, retain) < 0) {
        emit publishFailed(topic, "QMqttClient::publish báo lỗi");
        return false;
    }
    return true;
}

bool MqttService::subscribe(const QString &filter, quint8 qos)
{
    if (!QMqttTopicFilter(filter).isValid() || qos > 2) return false;
    for (const auto &sub : std::as_const(m_subs))
        if (sub.first == filter && sub.second == qos) return true;
    m_subs.append({filter, qos});
    return isConnected() ? doSubscribe(filter, qos) : true;   // sẽ đăng ký khi kết nối xong
}

bool MqttService::doSubscribe(const QString &filter, quint8 qos)
{
    QMqttSubscription *sub = m_client.subscribe(QMqttTopicFilter(filter), qos);
    if (!sub) {
        emit logMessage("Subscribe thất bại: " + filter);
        return false;
    }
    connect(sub, &QMqttSubscription::stateChanged, this,
            [this, filter](QMqttSubscription::SubscriptionState st) {
        if (st == QMqttSubscription::Error)
            emit logMessage("Broker từ chối subscribe (kiểm tra ACL): " + filter);
    });
    return true;
}

void MqttService::resubscribeAll()
{
    for (const auto &s : std::as_const(m_subs))
        doSubscribe(s.first, s.second);
}

void MqttService::onClientState(QMqttClient::ClientState s)
{
    switch (s) {
    case QMqttClient::Connecting:
        if (m_state != State::Reconnecting) setState(State::Connecting);
        break;
    case QMqttClient::Connected:
        if (!m_wantRun) {
            m_client.disconnectFromHost();
            return;
        }
        m_attempt = 0;
        m_reconnect.stop();
        emit logMessage("Đã kết nối broker");
        resubscribeAll();
        publish(m_cfg.statusTopic(), "online", 1, true);
        setState(State::Connected);
        break;
    case QMqttClient::Disconnected:
        m_shutdown.stop();
        m_offlineMessageId = -1;
        if (m_wantRun) scheduleReconnect();
        else {
            setState(State::Disconnected);
            emit stopped();
        }
        break;
    }
}

void MqttService::onMessage(const QByteArray &msg, const QMqttTopicName &topic)
{
    emit messageReceived(topic.name(), msg);
}

void MqttService::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}
