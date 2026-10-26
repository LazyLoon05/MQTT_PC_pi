#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMqttClient>
#include <QMqttSubscription>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QTimer>
#include <QMap>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    // UI Button Slots
    void on_ConnectButton_clicked();
    void on_CancelButton_clicked();
    void on_SendButton_clicked();
    void on_SendFileButton_clicked();
    void on_OpenFolderButton_clicked();
    void on_PingButton_clicked();
    void on_SubButton_clicked();
    void on_UnsubButton_clicked();
    void on_ClearRecvButton_clicked();
    void on_ClearLogButton_clicked();
    void on_Encryption_currentIndexChanged(int index);

    // MQTT Client Slots
    void onMqttConnected();
    void onMqttDisconnected();
    void onMqttStateChanged(QMqttClient::ClientState state);
    void onMqttErrorChanged(QMqttClient::ClientError error);
    void onMqttMessageReceived(const QByteArray &message, const QMqttTopicName &topic);
    void onMqttPingResponseReceived();

    // TCP Ping Probe Slot
    void onTcpPingConnected();
    void onTcpPingError(QAbstractSocket::SocketError socketError);

    // File Transfer Chunk Sender Slot
    void onSendNextFileChunk();

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
        int chunkSize = 32768; // 32 KB per chunk
    };

    struct IncomingFileState {
        bool active = false;
        QString transferId;
        QString fileName;
        qint64 totalSize = 0;
        int totalChunks = 0;
        int receivedChunks = 0;
        QString expectedSha256;
        QByteArray buffer;
    };

    void updateUiState();
    void subscribeTopic(const QString &topic, quint8 qos = 0);
    void unsubscribeTopic(const QString &topic);
    bool tryHandleFileTransferMessage(const QByteArray &message, const QString &topic);
    QString receivedFilesDir() const;
    void logSystem(const QString &text);

    Ui::MainWindow *ui;
    QMqttClient *m_client;
    QMap<QString, QMqttSubscription*> m_subscriptions;
    QTcpSocket *m_pingSocket;
    QElapsedTimer m_pingTimer;
    QTimer *m_fileChunkTimer;
    OutgoingFileState m_outFile;
    IncomingFileState m_inFile;
};

#endif // MAINWINDOW_H
