#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QStatusBar>
#include <QUrl>

#ifndef QT_NO_SSL
#include <QSslConfiguration>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_client(new QMqttClient(this))
    , m_pingSocket(new QTcpSocket(this))
    , m_fileChunkTimer(new QTimer(this))
{
    ui->setupUi(this);

    // Initial UI configurations
    ui->PassEdit->setEchoMode(QLineEdit::Password);
    ui->StatusEdit->setReadOnly(true);
    ui->RecvEdit->setReadOnly(true);
    ui->LogEdit->setReadOnly(true);
    ui->StatusEdit->setText(tr("Disconnected"));
    ui->FileProgressBar->setValue(0);

    if (ui->SubTopicEdit->text().isEmpty()) {
        ui->SubTopicEdit->setText(QStringLiteral("orangepi/data"));
    }
    if (ui->PubTopicEdit->text().isEmpty()) {
        ui->PubTopicEdit->setText(QStringLiteral("orangepi/cmd"));
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

    // Connect File Transfer chunk timer (10ms per 32KB chunk -> ~3.2 MB/s smooth non-blocking transfer)
    m_fileChunkTimer->setInterval(10);
    connect(m_fileChunkTimer, &QTimer::timeout, this, &MainWindow::onSendNextFileChunk);

    updateUiState();
}

MainWindow::~MainWindow()
{
    m_fileChunkTimer->stop();
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

    logSystem(QString("Connecting to broker %1:%2...").arg(host).arg(port));

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
        logSystem(tr("Disconnecting from broker..."));
        m_client->disconnectFromHost();
    } else {
        logSystem(tr("Already disconnected."));
    }
}

void MainWindow::on_SendButton_clicked()
{
    if (m_client->state() != QMqttClient::Connected) {
        QMessageBox::warning(this, tr("Warning"), tr("MQTT client is not connected to broker!"));
        return;
    }

    QString topic = ui->PubTopicEdit->text().trimmed();
    if (topic.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please enter a Publish Topic before sending!"));
        return;
    }

    QString message = ui->SendEdit->toPlainText();
    if (message.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please enter message content to send."));
        return;
    }

    quint8 qos = static_cast<quint8>(ui->PubQosCombo->currentIndex());
    bool retain = ui->RetainCheck->isChecked();

    qint32 publishId = m_client->publish(QMqttTopicName(topic), message.toUtf8(), qos, retain);
    if (publishId == -1) {
        logSystem(QString("Failed to publish message to topic: %1").arg(topic));
    } else {
        QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
        logSystem(QString("[%1] [Sent -> %2] (QoS %3%4): %5")
                      .arg(timeStr, topic)
                      .arg(qos)
                      .arg(retain ? QStringLiteral(", Retain") : QString())
                      .arg(message));
    }
}

void MainWindow::on_SendFileButton_clicked()
{
    if (m_client->state() != QMqttClient::Connected) {
        QMessageBox::warning(this, tr("Warning"), tr("MQTT client is not connected to broker!"));
        return;
    }

    if (m_outFile.active) {
        QMessageBox::information(this, tr("File Transfer"), tr("A file transfer is already in progress."));
        return;
    }

    QString topic = ui->PubTopicEdit->text().trimmed();
    if (topic.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please enter a Publish Topic before sending a file!"));
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(this, tr("Select File to Send via MQTT"));
    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("File Error"),
                              tr("Cannot open file for reading:\n%1").arg(file.errorString()));
        return;
    }

    QFileInfo fileInfo(file);
    if (fileInfo.size() > 50 * 1024 * 1024) { // 50 MB limit
        QMessageBox::warning(this, tr("File Too Large"),
                             tr("Selected file exceeds the 50 MB limit for MQTT transfer."));
        file.close();
        return;
    }

    m_outFile.data = file.readAll();
    file.close();

    m_outFile.active = true;
    m_outFile.transferId = QString::number(QDateTime::currentMSecsSinceEpoch());
    m_outFile.fileName = fileInfo.fileName();
    m_outFile.targetTopic = topic;
    m_outFile.sha256Hex = QString::fromLatin1(
        QCryptographicHash::hash(m_outFile.data, QCryptographicHash::Sha256).toHex());
    m_outFile.totalChunks = m_outFile.data.isEmpty()
                                ? 0
                                : ((m_outFile.data.size() + m_outFile.chunkSize - 1) / m_outFile.chunkSize);
    m_outFile.currentChunk = 0;

    // 1. Publish "start" metadata packet (QoS 1)
    QJsonObject startObj;
    startObj[QStringLiteral("mqtt_type")] = QStringLiteral("file_transfer");
    startObj[QStringLiteral("stage")] = QStringLiteral("start");
    startObj[QStringLiteral("transfer_id")] = m_outFile.transferId;
    startObj[QStringLiteral("filename")] = m_outFile.fileName;
    startObj[QStringLiteral("total_size")] = m_outFile.data.size();
    startObj[QStringLiteral("total_chunks")] = m_outFile.totalChunks;
    startObj[QStringLiteral("sha256")] = m_outFile.sha256Hex;

    QByteArray payload = QJsonDocument(startObj).toJson(QJsonDocument::Compact);
    m_client->publish(QMqttTopicName(m_outFile.targetTopic), payload, 1, false);

    ui->FileProgressBar->setValue(0);
    ui->FileProgressBar->setFormat(QString("Sending %1: %p%").arg(m_outFile.fileName));
    updateUiState();

    logSystem(QString("[File Send] Starting upload '%1' (%2 bytes, %3 chunks) -> [%4]")
                  .arg(m_outFile.fileName)
                  .arg(m_outFile.data.size())
                  .arg(m_outFile.totalChunks)
                  .arg(m_outFile.targetTopic));

    m_fileChunkTimer->start();
}

void MainWindow::onSendNextFileChunk()
{
    if (!m_outFile.active || m_client->state() != QMqttClient::Connected) {
        m_fileChunkTimer->stop();
        m_outFile.active = false;
        m_outFile.data.clear();
        updateUiState();
        return;
    }

    if (m_outFile.currentChunk < m_outFile.totalChunks) {
        int offset = m_outFile.currentChunk * m_outFile.chunkSize;
        QByteArray chunkBytes = m_outFile.data.mid(offset, m_outFile.chunkSize);

        QJsonObject chunkObj;
        chunkObj[QStringLiteral("mqtt_type")] = QStringLiteral("file_transfer");
        chunkObj[QStringLiteral("stage")] = QStringLiteral("chunk");
        chunkObj[QStringLiteral("transfer_id")] = m_outFile.transferId;
        chunkObj[QStringLiteral("index")] = m_outFile.currentChunk;
        chunkObj[QStringLiteral("data")] = QString::fromLatin1(chunkBytes.toBase64());

        QByteArray payload = QJsonDocument(chunkObj).toJson(QJsonDocument::Compact);
        m_client->publish(QMqttTopicName(m_outFile.targetTopic), payload, 1, false);

        m_outFile.currentChunk++;
        int progress = (m_outFile.currentChunk * 100) / qMax(1, m_outFile.totalChunks);
        ui->FileProgressBar->setValue(progress);
        return;
    }

    // All chunks sent -> publish "end" packet
    m_fileChunkTimer->stop();

    QJsonObject endObj;
    endObj[QStringLiteral("mqtt_type")] = QStringLiteral("file_transfer");
    endObj[QStringLiteral("stage")] = QStringLiteral("end");
    endObj[QStringLiteral("transfer_id")] = m_outFile.transferId;
    endObj[QStringLiteral("filename")] = m_outFile.fileName;
    endObj[QStringLiteral("sha256")] = m_outFile.sha256Hex;

    QByteArray payload = QJsonDocument(endObj).toJson(QJsonDocument::Compact);
    m_client->publish(QMqttTopicName(m_outFile.targetTopic), payload, 1, false);

    ui->FileProgressBar->setValue(100);
    ui->FileProgressBar->setFormat(QString("Sent %1 (100%)").arg(m_outFile.fileName));

    logSystem(QString("[File Send] Completed '%1' (%2 bytes, SHA-256: %3...)")
                  .arg(m_outFile.fileName)
                  .arg(m_outFile.data.size())
                  .arg(m_outFile.sha256Hex.left(12)));

    m_outFile.active = false;
    m_outFile.data.clear();
    updateUiState();
}

void MainWindow::on_OpenFolderButton_clicked()
{
    QString dirPath = receivedFilesDir();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
}

QString MainWindow::receivedFilesDir() const
{
    QString dirPath = QCoreApplication::applicationDirPath() + QStringLiteral("/received_files");
    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.absolutePath();
}

void MainWindow::on_SubButton_clicked()
{
    if (m_client->state() != QMqttClient::Connected) {
        QMessageBox::warning(this, tr("Warning"), tr("MQTT client is not connected to broker!"));
        return;
    }

    QString topic = ui->SubTopicEdit->text().trimmed();
    if (topic.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please enter a Subscribe Topic."));
        return;
    }

    quint8 qos = static_cast<quint8>(ui->SubQosCombo->currentIndex());
    subscribeTopic(topic, qos);
}

void MainWindow::on_UnsubButton_clicked()
{
    if (m_client->state() != QMqttClient::Connected) {
        QMessageBox::warning(this, tr("Warning"), tr("MQTT client is not connected to broker!"));
        return;
    }

    QString topic = ui->SubTopicEdit->text().trimmed();
    if (topic.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please enter a Subscribe Topic to unsubscribe."));
        return;
    }

    unsubscribeTopic(topic);
}

void MainWindow::on_ClearRecvButton_clicked()
{
    ui->RecvEdit->clear();
    statusBar()->showMessage(tr("Received messages cleared"), 2000);
}

void MainWindow::on_ClearLogButton_clicked()
{
    ui->LogEdit->clear();
    statusBar()->showMessage(tr("System log cleared"), 2000);
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

    logSystem(QString("Pinging broker at %1:%2...").arg(host).arg(port));
    m_pingTimer.start();
    m_pingSocket->abort();
    m_pingSocket->connectToHost(host, port);
}

void MainWindow::onMqttConnected()
{
    logSystem(tr("Connected to MQTT Broker successfully!"));
    m_subscriptions.clear();

    QString defaultSub = ui->SubTopicEdit->text().trimmed();
    if (!defaultSub.isEmpty()) {
        quint8 qos = static_cast<quint8>(ui->SubQosCombo->currentIndex());
        subscribeTopic(defaultSub, qos);
    }
}

void MainWindow::onMqttDisconnected()
{
    logSystem(tr("Disconnected from MQTT Broker."));
    m_subscriptions.clear();
    m_fileChunkTimer->stop();
    m_outFile.active = false;
    m_inFile.active = false;
    updateUiState();
}

void MainWindow::onMqttStateChanged(QMqttClient::ClientState state)
{
    switch (state) {
    case QMqttClient::Disconnected:
        ui->StatusEdit->setText(tr("Disconnected"));
        break;
    case QMqttClient::Connecting:
        ui->StatusEdit->setText(tr("Connecting..."));
        break;
    case QMqttClient::Connected:
        ui->StatusEdit->setText(tr("Connected"));
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
    logSystem(QString("MQTT Error: %1").arg(errStr));
}

void MainWindow::onMqttMessageReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    // Intercept chunked file transfer packets so raw Base64 chunks don't flood RecvEdit
    if (tryHandleFileTransferMessage(message, topic.name())) {
        return;
    }

    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString content = QString("[%1] [Recv <- %2]: %3")
                          .arg(timeStr, topic.name(), QString::fromUtf8(message));
    ui->RecvEdit->appendPlainText(content);
}

bool MainWindow::tryHandleFileTransferMessage(const QByteArray &message, const QString &topic)
{
    if (!message.startsWith('{')) {
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(message, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("mqtt_type")).toString() != QStringLiteral("file_transfer")) {
        return false;
    }

    QString stage = obj.value(QStringLiteral("stage")).toString();
    QString transferId = obj.value(QStringLiteral("transfer_id")).toString();

    if (stage == QStringLiteral("start")) {
        m_inFile.active = true;
        m_inFile.transferId = transferId;
        m_inFile.fileName = QFileInfo(obj.value(QStringLiteral("filename")).toString()).fileName();
        if (m_inFile.fileName.isEmpty()) {
            m_inFile.fileName = QStringLiteral("received_file.bin");
        }
        m_inFile.totalSize = static_cast<qint64>(obj.value(QStringLiteral("total_size")).toDouble());
        m_inFile.totalChunks = obj.value(QStringLiteral("total_chunks")).toInt();
        m_inFile.receivedChunks = 0;
        m_inFile.expectedSha256 = obj.value(QStringLiteral("sha256")).toString();
        m_inFile.buffer.clear();
        if (m_inFile.totalSize > 0 && m_inFile.totalSize <= 50 * 1024 * 1024) {
            m_inFile.buffer.reserve(static_cast<int>(m_inFile.totalSize));
        }

        ui->FileProgressBar->setValue(0);
        ui->FileProgressBar->setFormat(QString("Receiving %1: %p%").arg(m_inFile.fileName));

        logSystem(QString("[File Recv] Receiving '%1' (%2 bytes, %3 chunks) from [%4]...")
                      .arg(m_inFile.fileName)
                      .arg(m_inFile.totalSize)
                      .arg(m_inFile.totalChunks)
                      .arg(topic));
        return true;
    }

    if (stage == QStringLiteral("chunk")) {
        if (!m_inFile.active || m_inFile.transferId != transferId) {
            return true;
        }
        QByteArray chunkData = QByteArray::fromBase64(obj.value(QStringLiteral("data")).toString().toLatin1());
        m_inFile.buffer.append(chunkData);
        m_inFile.receivedChunks++;

        int progress = (m_inFile.receivedChunks * 100) / qMax(1, m_inFile.totalChunks);
        ui->FileProgressBar->setValue(progress);
        return true;
    }

    if (stage == QStringLiteral("end")) {
        if (!m_inFile.active || m_inFile.transferId != transferId) {
            return true;
        }

        QString actualSha256 = QString::fromLatin1(
            QCryptographicHash::hash(m_inFile.buffer, QCryptographicHash::Sha256).toHex());
        bool hashOk = m_inFile.expectedSha256.isEmpty() ||
                      (actualSha256.compare(m_inFile.expectedSha256, Qt::CaseInsensitive) == 0);

        QString savePath = receivedFilesDir() + QLatin1Char('/') + m_inFile.fileName;
        QFile outFile(savePath);
        if (outFile.open(QIODevice::WriteOnly)) {
            outFile.write(m_inFile.buffer);
            outFile.close();

            ui->FileProgressBar->setValue(100);
            ui->FileProgressBar->setFormat(QString("Saved %1 (100%)").arg(m_inFile.fileName));

            QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
            QString verifyStr = hashOk ? QStringLiteral("SHA-256 Verified OK")
                                       : QStringLiteral("SHA-256 MISMATCH!");
            ui->RecvEdit->appendPlainText(
                QString("[%1] [FILE RECEIVED <- %2]: '%3' (%4 bytes, %5)\n    Saved to: %6")
                    .arg(timeStr, topic, m_inFile.fileName)
                    .arg(m_inFile.buffer.size())
                    .arg(verifyStr, savePath));
            logSystem(QString("[File Saved] '%1' (%2 bytes, %3) -> %4")
                          .arg(m_inFile.fileName)
                          .arg(m_inFile.buffer.size())
                          .arg(verifyStr, savePath));
        } else {
            logSystem(QString("[File Error] Failed to save '%1' to %2: %3")
                          .arg(m_inFile.fileName, savePath, outFile.errorString()));
        }

        m_inFile.active = false;
        m_inFile.buffer.clear();
        return true;
    }

    return true;
}

void MainWindow::onMqttPingResponseReceived()
{
    logSystem(tr("MQTT Ping response received: Broker is alive (Keep-alive OK)!"));
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

    logSystem(pingMsg);
    m_pingSocket->disconnectFromHost();
}

void MainWindow::onTcpPingError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    logSystem(QString("[Ping Failed] Unable to connect to %1:%2 (%3)")
                   .arg(ui->IpEdit->text().trimmed())
                   .arg(ui->PortEdit->text().trimmed())
                   .arg(m_pingSocket->errorString()));
}

void MainWindow::subscribeTopic(const QString &topic, quint8 qos)
{
    if (topic.isEmpty()) {
        logSystem(tr("No topic specified to subscribe."));
        return;
    }

    if (m_subscriptions.contains(topic)) {
        logSystem(QString("Already subscribed to topic: %1").arg(topic));
        return;
    }

    QMqttSubscription *sub = m_client->subscribe(QMqttTopicFilter(topic), qos);
    if (!sub) {
        logSystem(QString("Failed to subscribe to topic: %1").arg(topic));
    } else {
        m_subscriptions[topic] = sub;
        logSystem(QString("Subscribed to topic: %1 (QoS %2)").arg(topic).arg(qos));
    }
}

void MainWindow::unsubscribeTopic(const QString &topic)
{
    if (topic.isEmpty()) {
        return;
    }

    m_client->unsubscribe(QMqttTopicFilter(topic));
    m_subscriptions.remove(topic);
    logSystem(QString("Unsubscribed from topic: %1").arg(topic));
}

void MainWindow::updateUiState()
{
    bool isConnected = (m_client->state() == QMqttClient::Connected);
    bool isConnecting = (m_client->state() == QMqttClient::Connecting);

    ui->ConnectButton->setEnabled(!isConnected && !isConnecting);
    ui->CancelButton->setEnabled(isConnected || isConnecting);
    ui->SendButton->setEnabled(isConnected);
    ui->SendFileButton->setEnabled(isConnected && !m_outFile.active);
    ui->SubButton->setEnabled(isConnected);
    ui->UnsubButton->setEnabled(isConnected);

    ui->IpEdit->setEnabled(!isConnected && !isConnecting);
    ui->PortEdit->setEnabled(!isConnected && !isConnecting);
    ui->UserNameEdit->setEnabled(!isConnected && !isConnecting);
    ui->PassEdit->setEnabled(!isConnected && !isConnecting);
    ui->Encryption->setEnabled(!isConnected && !isConnecting);
}

void MainWindow::logSystem(const QString &text)
{
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
    ui->LogEdit->appendPlainText(QString("[%1] %2").arg(timeStr, text));
    statusBar()->showMessage(text, 3000);
}
