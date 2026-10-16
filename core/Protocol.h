#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <cmath>

namespace Protocol {

inline qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// Qt 5 không có QJsonValue(qint64). Giá trị timestamp/sequence dùng ở đây
// nhỏ hơn 2^53 nên chuyển sang số JSON double vẫn chính xác.
inline QJsonValue jsonNumber(qint64 value) { return QJsonValue(static_cast<double>(value)); }

inline QByteArray toJson(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

inline bool parseObject(const QByteArray &data, QJsonObject &out)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    out = doc.object();
    return true;
}

inline QString newRequestId()
{
    return "req-" + QUuid::createUuid().toString(QUuid::Id128);
}

inline bool isInteger(const QJsonValue &value, double minimum, double maximum)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    return std::isfinite(number) && number >= minimum && number <= maximum
        && std::floor(number) == number;
}

inline bool isHeartbeat(const QJsonObject &o)
{
    return isInteger(o.value("seq"), 1, 9007199254740991.0)
        && isInteger(o.value("ts_ms"), 0, 9007199254740991.0)
        && isInteger(o.value("uptime_s"), 0, 9007199254740991.0)
        && isInteger(o.value("interval_ms"), 200, 60000);
}

} // namespace Protocol
