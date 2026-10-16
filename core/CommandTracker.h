#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QJsonValue>
#include <QObject>
#include <QTimer>
#include "Config.h"
#include "MqttService.h"

// Gửi lệnh có request_id, chờ ACK, thử lại và báo kết quả cuối cùng.
class CommandTracker : public QObject {
    Q_OBJECT
public:
    CommandTracker(MqttService *mqtt, const Config &cfg, QObject *parent = nullptr);

    // Trả về request_id. Kết quả đến qua tín hiệu finished().
    QString send(const QString &node, const QString &command,
                 const QJsonValue &value = QJsonValue());

    // Gọi với mọi tin nhận được; trả true nếu là topic reply.
    bool handleMessage(const QString &topic, const QByteArray &payload);
    void cancelAll();

signals:
    // status: "success" | "error" | "timeout" | "not_sent"
    // rttMs: thời gian từ lần gửi gần nhất đến khi có ACK (đồng hồ PC), -1 nếu không có.
    void finished(const QString &requestId, const QString &command,
                  const QString &status, const QString &detail, qint64 rttMs);

private:
    struct Pending {
        QString topic;
        QString replyTopic;
        QString command;
        QByteArray payload;
        int attempts = 0;
        QElapsedTimer sinceLast;
    };

    bool transmit(Pending &p);
    void onTick();

    MqttService *m_mqtt;
    Config m_cfg;
    QHash<QString, Pending> m_pending;
    QTimer m_tick;

    static constexpr int kTimeoutMs = 3000;
    static constexpr int kMaxAttempts = 2;      // gửi lần đầu + 1 lần thử lại
};
