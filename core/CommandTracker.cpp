#include "CommandTracker.h"
#include "Protocol.h"
#include <QStringList>
#include <utility>

CommandTracker::CommandTracker(MqttService *mqtt, const Config &cfg, QObject *parent)
    : QObject(parent), m_mqtt(mqtt), m_cfg(cfg)
{
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, &CommandTracker::onTick);
    m_tick.start();
}

bool CommandTracker::transmit(Pending &p)
{
    ++p.attempts;
    p.sinceLast.restart();
    return m_mqtt->publish(p.topic, p.payload, 1);
}

QString CommandTracker::send(const QString &node, const QString &command, const QJsonValue &value)
{
    const QString id = Protocol::newRequestId();
    if (!Config::isValidId(node) || (command != "ping" && command != "set_interval")) {
        emit finished(id, command, "not_sent", "node hoặc lệnh không hợp lệ", -1);
        return id;
    }

    QJsonObject obj{{"request_id", id}, {"command", command}, {"ts_ms", Protocol::jsonNumber(Protocol::nowMs())}};
    if (!value.isNull()) obj["value"] = value;

    Pending p;
    p.topic = m_cfg.nodeTopic(node, "cmd/" + command);
    p.replyTopic = m_cfg.nodeTopic(node, "reply/" + command);
    p.command = command;
    p.payload = Protocol::toJson(obj);

    if (!transmit(p)) {
        emit finished(id, command, "not_sent", "chưa kết nối broker", -1);
        return id;
    }
    m_pending.insert(id, p);
    return id;
}

void CommandTracker::onTick()
{
    QStringList expired;
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
        if (it->sinceLast.elapsed() >= kTimeoutMs)
            expired << it.key();

    for (const QString &id : std::as_const(expired)) {
        auto it = m_pending.find(id);
        if (it == m_pending.end()) continue;

        if (it->attempts < kMaxAttempts) {
            if (transmit(*it)) continue;                 // gửi lại cùng request_id
            const QString cmd = it->command;
            m_pending.erase(it);
            emit finished(id, cmd, "not_sent", "mất kết nối khi gửi lại", -1);
            continue;
        }
        const QString cmd = it->command;
        m_pending.erase(it);
        emit finished(id, cmd, "timeout",
                      QString("không có phản hồi sau %1 lần gửi").arg(kMaxAttempts), -1);
    }
}

bool CommandTracker::handleMessage(const QString &topic, const QByteArray &payload)
{
    // Dạng: <base>/<node>/reply/<lệnh>
    const QStringList parts = topic.split('/');
    if (parts.size() != 4 || parts[0] != m_cfg.base || parts[2] != "reply")
        return false;

    QJsonObject obj;
    if (!Protocol::parseObject(payload, obj)) return false;

    const QString id = obj.value("request_id").toString();
    auto it = m_pending.find(id);
    if (it == m_pending.end()) return true;              // ACK muộn hoặc lặp: bỏ qua

    const QString status = obj.value("status").toString();
    if (topic != it->replyTopic || obj.value("command").toString() != it->command
        || (status != "success" && status != "error")) return true;

    const qint64 rtt = it->sinceLast.elapsed();
    const QString cmd = it->command;
    m_pending.erase(it);

    QString detail;
    if (obj.contains("error"))
        detail = obj.value("error").toString();
    else if (obj.contains("applied_value"))
        detail = QString::number(obj.value("applied_value").toInt());

    emit finished(id, cmd, status, detail, rtt);
    return true;
}

void CommandTracker::cancelAll()
{
    const auto pending = m_pending;
    m_pending.clear();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        emit finished(it.key(), it->command, "timeout", "đã ngắt; chưa xác nhận thực thi", -1);
}
