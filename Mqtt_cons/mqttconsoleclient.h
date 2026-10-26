#ifndef MQTTCONSOLECLIENT_H
#define MQTTCONSOLECLIENT_H

#include <QObject>
#include <QMqttClient>
#include <QMqttSubscription>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QMap>
#include <atomic>
#include <csignal>

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
        QString subTopic = QStringLiteral("orangepi/cmd");
        QString pubTopic = QStringLiteral("orangepi/data");
        QString statusTopic = QStringLiteral("orangepi/status");
        QString username;
        QString password;
        QString clientId = QStringLiteral("OrangePi_Client");
        int heartbeatInterval = 0; // seconds, 0 = disabled
    };

    struct SystemMetrics {
        double cpuTempC = 0.0;
        qint64 ramUsedMB = 0;
        qint64 ramTotalMB = 0;
        double ramUsagePercent = 0.0;
        QString loadAvg = QStringLiteral("N/A");
        qint64 sysUptimeSec = 0;
    };

    explicit MqttConsoleClient(const Config &config, QObject *parent = nullptr);
    ~MqttConsoleClient() override;

    void start();
    void gracefulShutdown();

    static void handlePosixSignal(int sig);

public slots:
    void onMqttConnected();
    void onMqttDisconnected();
    void onMqttStateChanged(QMqttClient::ClientState state);
    void onMqttErrorChanged(QMqttClient::ClientError error);
    void onMqttMessageReceived(const QByteArray &message, const QMqttTopicName &topic);
    void onMqttPingResponseReceived();

    void onTcpPingConnected();
    void onTcpPingError(QAbstractSocket::SocketError socketError);

    void onConsoleInput(const QString &line);
    void onHeartbeatTimeout();
    void onSendNextFileChunk();
    void checkSignalFlag();
    void tryConnect();

private:
    struct OutgoingFileState {
        bool active = false;
        QString transferId;
        QString fileName;
        QString targetTopic;
        QByteArray data;
        QString sha256Hex;
        int totalChunks = 0;
        int currentChunk = 0;
        int lastLoggedMilestone = 0;
        int chunkSize = 32768; // 32 KB per chunk
    };

    struct IncomingFileState {
        bool active = false;
        QString transferId;
        QString fileName;
        qint64 totalSize = 0;
        int totalChunks = 0;
        int receivedChunks = 0;
        int lastLoggedMilestone = 0;
        QString expectedSha256;
        QByteArray buffer;
    };

    void printHelp();
    void publishMessage(const QString &topic, const QString &message, quint8 qos = 0, bool retain = false);
    void subscribeTopic(const QString &topic, quint8 qos = 0);
    void unsubscribeTopic(const QString &topic);
    void publishOnlineStatus(bool online);
    void sendPingProbe();

    bool startFileSend(const QString &filePath, const QString &targetTopic);
    bool tryHandleFileTransferMessage(const QByteArray &message, const QString &topic);
    QString receivedFilesDir() const;

    SystemMetrics readSystemMetrics() const;
    QString buildTelemetryJson(const QString &eventType = QStringLiteral("telemetry")) const;
    void processIncomingCommand(const QString &payload);
    void setHeartbeatInterval(int seconds);

    void log(const QString &tag, const QString &message);

    Config m_config;
    QMqttClient *m_client;
    QMap<QString, QMqttSubscription*> m_subscriptions;
    QTcpSocket *m_pingSocket;
    QElapsedTimer m_pingTimer;
    ConsoleInputWorker *m_inputWorker;
    QTimer *m_heartbeatTimer;
    QTimer *m_reconnectTimer;
    QTimer *m_signalWatchTimer;
    QTimer *m_fileChunkTimer;
    QElapsedTimer m_appUptimeTimer;
    OutgoingFileState m_outFile;
    IncomingFileState m_inFile;
    bool m_shuttingDown;

    static volatile std::sig_atomic_t s_stopSignalReceived;
};

#endif // MQTTCONSOLECLIENT_H
