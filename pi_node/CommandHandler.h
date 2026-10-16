#pragma once
#include <QByteArray>
#include <QHash>
#include <QQueue>
#include <QString>

// Cache ACK theo request_id; chỉ gọi applyInterval khi appliedInterval > 0.
class CommandHandler {
public:
    struct Result {
        QByteArray ack;
        int appliedInterval = 0;
        bool duplicate = false;
    };
    Result handle(const QString &command, const QByteArray &payload);
private:
    struct Cached { QString command; QByteArray ack; };
    QHash<QString, Cached> m_cache;
    QQueue<QString> m_order;
};
