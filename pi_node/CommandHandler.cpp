#include "CommandHandler.h"
#include "Protocol.h"

CommandHandler::Result CommandHandler::handle(const QString &command, const QByteArray &payload)
{
    QJsonObject request;
    const bool parsed = payload.size() <= 4096 && Protocol::parseObject(payload, request);
    const QString id = request.value("request_id").toString();
    QJsonObject reply{{"request_id", id}, {"command", command},
                      {"status", "error"}, {"ts_ms", Protocol::jsonNumber(Protocol::nowMs())}};
    auto error = [&](const QString &message) {
        reply["error"] = message;
        return Result{Protocol::toJson(reply), 0, false};
    };
    if (!parsed || id.isEmpty() || id.size() > 128)
        return error("payload không hợp lệ hoặc thiếu request_id (tối đa 128 ký tự)");
    if (request.value("command").toString() != command)
        return error("command phải khớp topic");
    if (m_cache.contains(id)) {
        const auto &cached = m_cache[id];
        if (cached.command != command) return error("request_id đã dùng cho lệnh khác");
        return {cached.ack, 0, true};
    }
    int interval = 0;
    if (command == "ping") {
        reply["status"] = "success";
    } else if (command == "set_interval") {
        const QJsonValue value = request.value("value");
        if (!Protocol::isInteger(value, 200, 60000)) {
            reply["error"] = "value phải là số nguyên 200..60000";
        } else {
            interval = value.toInt();
            reply["status"] = "success";
            reply["applied_value"] = interval;
        }
    } else {
        reply["error"] = "lệnh không được hỗ trợ";
    }
    const QByteArray ack = Protocol::toJson(reply);
    m_cache.insert(id, {command, ack});
    m_order.enqueue(id);
    if (m_order.size() > 64) m_cache.remove(m_order.dequeue());
    return {ack, interval, false};
}
