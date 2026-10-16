#pragma once
#include <QList>
#include <QObject>
#include <QPair>
#include <QTimer>
#include <QtMqtt/QMqttClient>
#include <QtMqtt/QMqttSubscription>
#include "Config.h"

class MqttService : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected, Reconnecting };
    Q_ENUM(State)

    explicit MqttService(const Config &cfg, QObject *parent = nullptr);
    ~MqttService() override;

    void start();
    void stop();

    State state() const { return m_state; }
    bool isConnected() const { return m_client.state() == QMqttClient::Connected; }

    // true = đã giao cho QMqttClient (chưa có nghĩa broker/đích đã nhận).
    bool publish(const QString &topic, const QByteArray &payload,
                 quint8 qos = 0, bool retain = false);
    // Lưu filter và tự đăng ký lại sau mỗi lần kết nối.
    bool subscribe(const QString &filter, quint8 qos = 0);

signals:
    void stateChanged(MqttService::State state);
    void messageReceived(const QString &topic, const QByteArray &payload);
    void publishFailed(const QString &topic, const QString &reason);
    void logMessage(const QString &text);
    void stopped();

private slots:
    void onClientState(QMqttClient::ClientState s);
    void onMessage(const QByteArray &msg, const QMqttTopicName &topic);

private:
    void setState(State s);
    void tryConnect();
    void scheduleReconnect();
    bool doSubscribe(const QString &filter, quint8 qos);
    void resubscribeAll();

    Config m_cfg;
    QMqttClient m_client;
    QTimer m_reconnect;
    QTimer m_shutdown;
    qint32 m_offlineMessageId = -1;
    QList<QPair<QString, quint8>> m_subs;
    State m_state = State::Disconnected;
    int m_attempt = 0;
    bool m_wantRun = false;
};
