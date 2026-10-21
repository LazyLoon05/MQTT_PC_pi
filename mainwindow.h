#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMqttClient>
#include <QMqttSubscription>
#include <QTcpSocket>

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

    // MQTT Client Slots
    void onMqttConnected();
    void onMqttDisconnected();
    void onMqttStateChanged(QMqttClient::ClientState state);
    void onMqttErrorChanged(QMqttClient::ClientError error);
    void onMqttMessageReceived(const QByteArray &message, const QMqttTopicName &topic);
    void onMqttPingResponseReceived();

    // TCP Ping Probe Slot (when not connected)
    void onTcpPingConnected();
    void onTcpPingError(QAbstractSocket::SocketError socketError);

private:
    void updateUiState();
    void subscribeTopic();
    void logMessage(const QString &text);

    Ui::MainWindow *ui;
    QMqttClient *m_client;
    QMqttSubscription *m_sub;
    QTcpSocket *m_pingSocket;
};

#endif // MAINWINDOW_H
