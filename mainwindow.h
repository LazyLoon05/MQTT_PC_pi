#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMqttClient>
#include <QMqttSubscription>
#include <QTcpSocket>
#include <QElapsedTimer>
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
    void on_PingButton_clicked();
    void on_SubButton_clicked();
    void on_UnsubButton_clicked();
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

private:
    void updateUiState();
    void subscribeTopic(const QString &topic);
    void unsubscribeTopic(const QString &topic);
    void logMessage(const QString &text);

    Ui::MainWindow *ui;
    QMqttClient *m_client;
    QMap<QString, QMqttSubscription*> m_subscriptions;
    QTcpSocket *m_pingSocket;
    QElapsedTimer m_pingTimer;
};

#endif // MAINWINDOW_H
