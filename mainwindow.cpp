#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDateTime>
#include <QMessageBox>
#include <QStatusBar>

#ifndef QT_NO_SSL
#include <QSslConfiguration>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_client(new QMqttClient(this))
    , m_sub(nullptr)
    , m_pingSocket(new QTcpSocket(this))
{
    ui->setupUi(this);

    // Initial UI configurations
    ui->PassEdit->setEchoMode(QLineEdit::Password);
    ui->StatusEdit->setReadOnly(true);
    ui->RecvEdit->setReadOnly(true);
    ui->StatusEdit->setPlainText(tr("Disconnected"));

    if (ui->TopicEdit->text().isEmpty()) {
        ui->TopicEdit->setText(QStringLiteral("test/topic"));
    }

    // Connect QMqttClient signals
    connect(m_client, &QMqttClient::connected, this, &MainWindow::onMqttConnected);
    connect(m_client, &QMqttClient::disconnected, this, &MainWindow::onMqttDisconnected);
    connect(m_client, &QMqttClient::stateChanged, this, &MainWindow::onMqttStateChanged);
    connect(m_client, &QMqttClient::errorChanged, this, &MainWindow::onMqttErrorChanged);
    connect(m_client, &QMqttClient::messageReceived, this, &MainWindow::onMqttMessageReceived);
    connect(m_client, &QMqttClient::pingResponseReceived, this, &MainWindow::onMqttPingResponseReceived);

    // Connect TCP Ping probe socket signals
    connect(m_pingSocket, &QTcpSocket::connected, this, &MainWindow::onTcpPingConnected);
    connect(m_pingSocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, &MainWindow::onTcpPingError);

    updateUiState();
}

MainWindow::~MainWindow()
{
    if (m_client->state() == QMqttClient::Connected) {
        m_client->disconnectFromHost();
    }
    delete ui;
}

void MainWindow::on_ConnectButton_clicked()
{
    QString host = ui->IpEdit->text().trimmed();
    quint16 port = ui->PortEdit->text().toUShort();

    if (host.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please enter broker IP or hostname."));
        return;
    }
    if (port == 0) {
        QMessageBox::warning(this, tr("Warning"), tr("Please enter a valid port number."));
        return;
    }

    m_client->setHostname(host);
    m_client->setPort(port);
    m_client->setKeepAlive(60);

    QString username = ui->UserNameEdit->text().trimmed();
    QString password = ui->PassEdit->text();
    if (!username.isEmpty()) {
        m_client->setUsername(username);
    }
    if (!password.isEmpty()) {
        m_client->setPassword(password);
    }

    logMessage(QString("Connecting to broker %1:%2...").arg(host).arg(port));

    bool useEncryption = (ui->Encryption->currentIndex() == 1);
    if (useEncryption) {
#ifndef QT_NO_SSL
        QSslConfiguration sslConf = QSslConfiguration::defaultConfiguration();
        sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
        m_client->connectToHostEncrypted(sslConf);
#else
        QMessageBox::critical(this, tr("SSL Error"), tr("SSL is not supported in this Qt build."));
        return;
#endif
    } else {
        m_client->connectToHost();
    }
}

void MainWindow::on_CancelButton_clicked()
{
    if (m_client->state() == QMqttClient::Connecting || m_client->state() == QMqttClient::Connected) {
        logMessage(tr("Disconnecting from broker..."));
        m_client->disconnectFromHost();
    } else {
        logMessage(tr("Already disconnected."));
    }
}

void MainWindow::on_SendButton_clicked()
{
    if (m_client->state() != QMqttClient::Connected) {
        QMessageBox::warning(this, tr("Warning"), tr("MQTT client is not connected to broker!"));
        return;
    }

    QString topic = ui->TopicEdit->text().trimmed();
    if (topic.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please enter a topic before publishing!"));
        return;
    }

    QString message = ui->SendEdit->toPlainText();
    if (message.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please enter message content to send."));
        return;
    }

    qint32 publishId = m_client->publish(QMqttTopicName(topic), message.toUtf8(), 0);
    if (publishId == -1) {
        logMessage(QString("Failed to publish message to topic: %1").arg(topic));
    } else {
        QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
        logMessage(QString("[%1] [Sent -> %2]: %3").arg(timeStr, topic, message));
    }
}

void MainWindow::on_Encryption_currentIndexChanged(int index)
{
    if (index == 1) { // Encryption selected
        if (ui->PortEdit->text().trimmed() == QStringLiteral("1883")) {
            ui->PortEdit->setText(QStringLiteral("8883"));
        }
#ifndef QT_NO_SSL
        if (!QSslSocket::supportsSsl()) {
            QMessageBox::warning(this, tr("SSL Warning"),
                tr("OpenSSL libraries were not found on this system.\nSSL encrypted connection may fail if DLLs are missing."));
        }
#endif
    } else { // No Encryption
        if (ui->PortEdit->text().trimmed() == QStringLiteral("8883")) {
            ui->PortEdit->setText(QStringLiteral("1883"));
        }
    }
}

void MainWindow::on_PingButton_clicked()
{
    QString host = ui->IpEdit->text().trimmed();
    quint16 port = ui->PortEdit->text().toUShort();

    if (host.isEmpty() || port == 0) {
        QMessageBox::warning(this, tr("Warning"), tr("Please enter valid IP and Port to test Ping."));
        return;
    }

    logMessage(QString("Pinging broker at %1:%2...").arg(host).arg(port));
    m_pingTimer.start();
    m_pingSocket->abort();
    m_pingSocket->connectToHost(host, port);
}

void MainWindow::onMqttConnected()
{
    logMessage(tr("Connected to MQTT Broker successfully!"));
    subscribeTopic();
}

void MainWindow::onMqttDisconnected()
{
    logMessage(tr("Disconnected from MQTT Broker."));
    m_sub = nullptr;
}

void MainWindow::onMqttStateChanged(QMqttClient::ClientState state)
{
    switch (state) {
    case QMqttClient::Disconnected:
        ui->StatusEdit->setPlainText(tr("Disconnected"));
        break;
    case QMqttClient::Connecting:
        ui->StatusEdit->setPlainText(tr("Connecting..."));
        break;
    case QMqttClient::Connected:
        ui->StatusEdit->setPlainText(tr("Connected"));
        break;
    }
    updateUiState();
}

void MainWindow::onMqttErrorChanged(QMqttClient::ClientError error)
{
    QString errStr;
    switch (error) {
    case QMqttClient::NoError:
        return;
    case QMqttClient::InvalidProtocolVersion:
        errStr = tr("Invalid protocol version");
        break;
    case QMqttClient::IdRejected:
        errStr = tr("Client ID rejected");
        break;
    case QMqttClient::ServerUnavailable:
        errStr = tr("Broker server unavailable");
        break;
    case QMqttClient::BadUsernameOrPassword:
        errStr = tr("Bad username or password");
        break;
    case QMqttClient::NotAuthorized:
        errStr = tr("Not authorized");
        break;
    case QMqttClient::TransportInvalid:
        errStr = tr("Transport invalid / Network unreachable");
        break;
    case QMqttClient::ProtocolViolation:
        errStr = tr("Protocol violation");
        break;
    default:
        errStr = QString(tr("Unknown error (Code %1)")).arg(error);
        break;
    }
    logMessage(QString("MQTT Error: %1").arg(errStr));
    statusBar()->showMessage(QString("Error: %1").arg(errStr), 5000);
}

void MainWindow::onMqttMessageReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString content = QString("[%1] [Recv <- %2]: %3")
                          .arg(timeStr, topic.name(), QString::fromUtf8(message));
    ui->RecvEdit->appendPlainText(content);
}

void MainWindow::onMqttPingResponseReceived()
{
    logMessage(tr("MQTT Ping response received: Broker is alive (Keep-alive OK)!"));
}

void MainWindow::onTcpPingConnected()
{
    qint64 latency = m_pingTimer.elapsed();
    QString sessionState = (m_client->state() == QMqttClient::Connected)
                               ? tr("MQTT Connected")
                               : tr("MQTT Disconnected");

    QString pingMsg = QString("[Ping OK] Broker %1:%2 | Latency: %3 ms (%4)")
                          .arg(ui->IpEdit->text().trimmed())
                          .arg(ui->PortEdit->text().trimmed())
                          .arg(latency)
                          .arg(sessionState);

    logMessage(pingMsg);
    statusBar()->showMessage(QString("Broker Latency: %1 ms").arg(latency), 5000);
    m_pingSocket->disconnectFromHost();
}

void MainWindow::onTcpPingError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    logMessage(QString("[Ping Failed] Unable to connect to %1:%2 (%3)")
                   .arg(ui->IpEdit->text().trimmed())
                   .arg(ui->PortEdit->text().trimmed())
                   .arg(m_pingSocket->errorString()));
    statusBar()->showMessage(tr("Ping Failed"), 5000);
}

void MainWindow::subscribeTopic()
{
    QString topic = ui->TopicEdit->text().trimmed();
    if (topic.isEmpty()) {
        logMessage(tr("No topic specified to subscribe."));
        return;
    }

    m_sub = m_client->subscribe(QMqttTopicFilter(topic), 0);
    if (!m_sub) {
        logMessage(QString("Failed to subscribe to topic: %1").arg(topic));
    } else {
        logMessage(QString("Subscribed to topic: %1").arg(topic));
    }
}

void MainWindow::updateUiState()
{
    bool isConnected = (m_client->state() == QMqttClient::Connected);
    bool isConnecting = (m_client->state() == QMqttClient::Connecting);

    ui->ConnectButton->setEnabled(!isConnected && !isConnecting);
    ui->CancelButton->setEnabled(isConnected || isConnecting);
    ui->SendButton->setEnabled(isConnected);

    ui->IpEdit->setEnabled(!isConnected && !isConnecting);
    ui->PortEdit->setEnabled(!isConnected && !isConnecting);
    ui->UserNameEdit->setEnabled(!isConnected && !isConnecting);
    ui->PassEdit->setEnabled(!isConnected && !isConnecting);
    ui->Encryption->setEnabled(!isConnected && !isConnecting);
}

void MainWindow::logMessage(const QString &text)
{
    ui->RecvEdit->appendPlainText(text);
    statusBar()->showMessage(text, 3000);
}
