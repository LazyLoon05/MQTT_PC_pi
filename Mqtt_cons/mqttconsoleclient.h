#ifndef MQTTCONSOLECLIENT_H
#define MQTTCONSOLECLIENT_H

#include <QObject>
#include <QMqttClient>
#include <QMqttSubscription>
#include <QThread>
#include <QTimer>
#include <atomic>

class ConsoleInputWorker : public QThread
{
    Q_OBJECT
public:
    explicit ConsoleInputWorker(QObject *parent = nullptr);
    void stop();

protected:
    void run() override;

signals:
    void lineReceived(const QString &line);

private:
    std::atomic<bool> m_running;
};

class MqttConsoleClient : public QObject
{
    Q_OBJECT

public:
    struct Config {
        QString host = QStringLiteral("192.168.0.104");
        quint16 port = 1883;
        QString topic = QStringLiteral("test/topic");
        QString username;
        QString password;
        QString clientId = QStringLiteral("OrangePi_Client");
        int heartbeatInterval = 0; // seconds, 0 = disabled
    };

    explicit MqttConsoleClient(const Config &config, QObject *parent = nullptr);
    ~MqttConsoleClient() override;

    void start();

public slots:
    void onMqttConnected();
    void onMqttDisconnected();
    void onMqttStateChanged(QMqttClient::ClientState state);
    void onMqttErrorChanged(QMqttClient::ClientError error);
    void onMqttMessageReceived(const QByteArray &message, const QMqttTopicName &topic);
    void onMqttPingResponseReceived();

    void onConsoleInput(const QString &line);
    void onHeartbeatTimeout();
    void tryConnect();

private:
    void printHelp();
    void publishMessage(const QString &topic, const QString &message);
    void subscribeTopic(const QString &topic);
    void log(const QString &tag, const QString &message);

    Config m_config;
    QMqttClient *m_client;
    QMqttSubscription *m_sub;
    ConsoleInputWorker *m_inputWorker;
    QTimer *m_heartbeatTimer;
    QTimer *m_reconnectTimer;
    qint64 m_uptimeSeconds;
};

#endif // MQTTCONSOLECLIENT_H
