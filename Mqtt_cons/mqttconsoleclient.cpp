#include "mqttconsoleclient.h"

#include <iostream>
#include <string>
#include <cmath>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#ifdef Q_OS_UNIX
#include <poll.h>
#include <unistd.h>
#endif

volatile std::sig_atomic_t MqttConsoleClient::s_stopSignalReceived = 0;

void MqttConsoleClient::handlePosixSignal(int sig)
{
    s_stopSignalReceived = sig;
}

ConsoleInputWorker::ConsoleInputWorker(QObject *parent)
    : QThread(parent)
    , m_running(true)
{
}

void ConsoleInputWorker::stop()
{
    m_running = false;
}

void ConsoleInputWorker::run()
{
    std::string line;
#ifdef Q_OS_UNIX
    while (m_running) {
        struct pollfd pfd;
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 200); // 200ms non-blocking poll
        if (ret > 0 && (pfd.revents & POLLIN)) {
            if (std::getline(std::cin, line)) {
                if (!m_running) {
                    break;
                }
                QString qline = QString::fromStdString(line);
                emit lineReceived(qline);
                QString cmd = qline.trimmed().toLower();
                if (cmd == QStringLiteral(":quit") || cmd == QStringLiteral(":q")) {
                    m_running = false;
                    break;
                }
            } else {
                // EOF on stdin (e.g., running as background service)
                break;
            }
        } else if (ret < 0) {
            break;
        }
    }
#else
    while (m_running && std::getline(std::cin, line)) {
        if (!m_running) {
            break;
        }
        QString qline = QString::fromStdString(line);
        emit lineReceived(qline);
        QString cmd = qline.trimmed().toLower();
        if (cmd == QStringLiteral(":quit") || cmd == QStringLiteral(":q")) {
            m_running = false;
            break;
        }
    }
#endif
}

MqttConsoleClient::MqttConsoleClient(const Config &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_client(new QMqttClient(this))
    , m_pingSocket(new QTcpSocket(this))
    , m_inputWorker(new ConsoleInputWorker(this))
    , m_heartbeatTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
    , m_signalWatchTimer(new QTimer(this))
    , m_fileChunkTimer(new QTimer(this))
    , m_shuttingDown(false)
{
    m_appUptimeTimer.start();

    // Connect MQTT signals
    connect(m_client, &QMqttClient::connected, this, &MqttConsoleClient::onMqttConnected);
    connect(m_client, &QMqttClient::disconnected, this, &MqttConsoleClient::onMqttDisconnected);
    connect(m_client, &QMqttClient::stateChanged, this, &MqttConsoleClient::onMqttStateChanged);
    connect(m_client, &QMqttClient::errorChanged, this, &MqttConsoleClient::onMqttErrorChanged);
    connect(m_client, &QMqttClient::messageReceived, this, &MqttConsoleClient::onMqttMessageReceived);
    connect(m_client, &QMqttClient::pingResponseReceived, this, &MqttConsoleClient::onMqttPingResponseReceived);

    // Connect TCP Ping probe socket signals
    connect(m_pingSocket, &QTcpSocket::connected, this, &MqttConsoleClient::onTcpPingConnected);
    connect(m_pingSocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, &MqttConsoleClient::onTcpPingError);

    // Connect Console Input worker
    connect(m_inputWorker, &ConsoleInputWorker::lineReceived, this, &MqttConsoleClient::onConsoleInput);

    // Reconnect timer configuration (every 5 seconds when disconnected)
    m_reconnectTimer->setInterval(5000);
    connect(m_reconnectTimer, &QTimer::timeout, this, &MqttConsoleClient::tryConnect);

    // Heartbeat timer configuration
    connect(m_heartbeatTimer, &QTimer::timeout, this, &MqttConsoleClient::onHeartbeatTimeout);
    if (m_config.heartbeatInterval > 0) {
        m_heartbeatTimer->setInterval(m_config.heartbeatInterval * 1000);
    }

    // File transfer chunk timer (10ms per 32KB chunk)
    m_fileChunkTimer->setInterval(10);
    connect(m_fileChunkTimer, &QTimer::timeout, this, &MqttConsoleClient::onSendNextFileChunk);

    // Signal watcher timer (checks SIGINT / SIGTERM every 200ms)
    m_signalWatchTimer->setInterval(200);
    connect(m_signalWatchTimer, &QTimer::timeout, this, &MqttConsoleClient::checkSignalFlag);
    m_signalWatchTimer->start();
}

MqttConsoleClient::~MqttConsoleClient()
{
    gracefulShutdown();
}

void MqttConsoleClient::checkSignalFlag()
{
    if (s_stopSignalReceived != 0 && !m_shuttingDown) {
        std::cout << std::endl;
        log(QStringLiteral("SHUTDOWN"),
            QString("Caught OS signal (%1). Initiating graceful shutdown...").arg(s_stopSignalReceived));
        gracefulShutdown();
    }
}

void MqttConsoleClient::gracefulShutdown()
{
    if (m_shuttingDown) {
        return;
    }
    m_shuttingDown = true;

    m_signalWatchTimer->stop();
    m_reconnectTimer->stop();
    m_heartbeatTimer->stop();
    m_fileChunkTimer->stop();

    // Stop input worker thread cleanly without terminate()
    m_inputWorker->stop();

    if (m_client->state() == QMqttClient::Connected) {
        publishOnlineStatus(false);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        m_client->disconnectFromHost();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    m_inputWorker->wait(500);
    QCoreApplication::quit();
}

void MqttConsoleClient::start()
{
    std::cout << "======================================================" << std::endl;
    std::cout << "  MQTT Console Client (Orange Pi / Linux / Embedded)  " << std::endl;
    std::cout << "======================================================" << std::endl;
    std::cout << "Broker Host  : " << m_config.host.toStdString() << ":" << m_config.port << std::endl;
    std::cout << "Client ID    : " << m_config.clientId.toStdString() << std::endl;
    std::cout << "Sub Topic    : " << m_config.subTopic.toStdString() << " (Listen from PC)" << std::endl;
    std::cout << "Pub Topic    : " << m_config.pubTopic.toStdString() << " (Send to PC)" << std::endl;
    std::cout << "Status Topic : " << m_config.statusTopic.toStdString() << " (LWT Online/Offline)" << std::endl;
    std::cout << "Recv Folder  : " << receivedFilesDir().toStdString() << std::endl;
    if (!m_config.username.isEmpty()) {
        std::cout << "Username     : " << m_config.username.toStdString() << std::endl;
    }
    if (m_config.heartbeatInterval > 0) {
        std::cout << "Heartbeat    : Every " << m_config.heartbeatInterval << "s" << std::endl;
    }
    std::cout << "------------------------------------------------------" << std::endl;
    std::cout << "Remote PC Commands supported on [" << m_config.subTopic.toStdString() << "]:" << std::endl;
    std::cout << "  temp | ram | sysinfo | status | ping | heartbeat <sec> | getfile <path>" << std::endl;
    std::cout << "Local File Transfer command:" << std::endl;
    std::cout << "  :sendfile <file_path>  (Send any file from Orange Pi to PC)" << std::endl;
    std::cout << "------------------------------------------------------" << std::endl;
    std::cout << "Type any message and press Enter to publish to Pub Topic." << std::endl;
    std::cout << "Type :help for local commands, or press Ctrl+C to exit." << std::endl;
    std::cout << "======================================================" << std::endl << std::endl;

    // Start background input listener
    m_inputWorker->start();

    // Initiate first connection
    tryConnect();
}

void MqttConsoleClient::tryConnect()
{
    if (m_shuttingDown ||
        m_client->state() == QMqttClient::Connected ||
        m_client->state() == QMqttClient::Connecting) {
        return;
    }

    log(QStringLiteral("CONNECTING"),
        QString("Connecting to broker %1:%2...").arg(m_config.host).arg(m_config.port));

    m_client->setHostname(m_config.host);
    m_client->setPort(m_config.port);
    m_client->setClientId(m_config.clientId);
    m_client->setKeepAlive(60);
    m_client->setCleanSession(true);

    if (!m_config.username.isEmpty()) {
        m_client->setUsername(m_config.username);
    }
    if (!m_config.password.isEmpty()) {
        m_client->setPassword(m_config.password);
    }

    // Configure Last Will and Testament (LWT)
    if (!m_config.statusTopic.isEmpty()) {
        QString willPayload = QString("{\"device\":\"%1\",\"status\":\"offline\"}")
                                  .arg(m_config.clientId);
        m_client->setWillTopic(m_config.statusTopic);
        m_client->setWillMessage(willPayload.toUtf8());
        m_client->setWillQoS(1);
        m_client->setWillRetain(true);
    }

    m_client->connectToHost();
}

void MqttConsoleClient::onMqttConnected()
{
    m_reconnectTimer->stop();
    m_subscriptions.clear();

    log(QStringLiteral("CONNECTED"),
        QString("Successfully connected to broker %1:%2!").arg(m_config.host).arg(m_config.port));

    // Publish Online status (Retained) to Status Topic
    publishOnlineStatus(true);

    // Subscribe to default command topic from PC
    subscribeTopic(m_config.subTopic, 1);

    // Start heartbeat timer if configured
    if (m_config.heartbeatInterval > 0 && !m_heartbeatTimer->isActive()) {
        m_heartbeatTimer->start(m_config.heartbeatInterval * 1000);
    }
}

void MqttConsoleClient::onMqttDisconnected()
{
    m_subscriptions.clear();
    m_fileChunkTimer->stop();
    m_outFile.active = false;
    m_inFile.active = false;

    if (m_heartbeatTimer->isActive()) {
        m_heartbeatTimer->stop();
    }

    if (m_shuttingDown) {
        log(QStringLiteral("DISCONNECTED"), QStringLiteral("Disconnected cleanly from broker."));
        return;
    }

    log(QStringLiteral("DISCONNECTED"), QStringLiteral("Lost connection to broker. Will retry in 5s..."));
    if (!m_reconnectTimer->isActive()) {
        m_reconnectTimer->start();
    }
}

void MqttConsoleClient::onMqttStateChanged(QMqttClient::ClientState state)
{
    QString stateStr;
    switch (state) {
    case QMqttClient::Disconnected:
        stateStr = QStringLiteral("Disconnected");
        break;
    case QMqttClient::Connecting:
        stateStr = QStringLiteral("Connecting...");
        break;
    case QMqttClient::Connected:
        stateStr = QStringLiteral("Connected");
        break;
    }
    log(QStringLiteral("STATE"), stateStr);
}

void MqttConsoleClient::onMqttErrorChanged(QMqttClient::ClientError error)
{
    if (error == QMqttClient::NoError) {
        return;
    }

    QString errStr;
    switch (error) {
    case QMqttClient::InvalidProtocolVersion:
        errStr = QStringLiteral("Invalid protocol version");
        break;
    case QMqttClient::IdRejected:
        errStr = QStringLiteral("Client ID rejected");
        break;
    case QMqttClient::ServerUnavailable:
        errStr = QStringLiteral("Broker server unavailable");
        break;
    case QMqttClient::BadUsernameOrPassword:
        errStr = QStringLiteral("Bad username or password");
        break;
    case QMqttClient::NotAuthorized:
        errStr = QStringLiteral("Not authorized");
        break;
    case QMqttClient::TransportInvalid:
        errStr = QStringLiteral("Network unreachable or port closed");
        break;
    case QMqttClient::ProtocolViolation:
        errStr = QStringLiteral("Protocol violation");
        break;
    default:
        errStr = QString("Unknown error (Code %1)").arg(error);
        break;
    }
    log(QStringLiteral("ERROR"), errStr);
}

void MqttConsoleClient::onMqttMessageReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    // 1. Intercept chunked file transfer packets first
    if (tryHandleFileTransferMessage(message, topic.name())) {
        return;
    }

    QString payload = QString::fromUtf8(message).trimmed();
    log(QStringLiteral("RECV"), QString("[%1]: %2").arg(topic.name(), payload));

    // 2. Process remote commands received on Sub Topic
    if (topic.name() == m_config.subTopic) {
        processIncomingCommand(payload);
    }
}

bool MqttConsoleClient::startFileSend(const QString &filePath, const QString &targetTopic)
{
    if (m_client->state() != QMqttClient::Connected) {
        log(QStringLiteral("WARN"), QStringLiteral("Cannot send file: Not connected to Broker!"));
        return false;
    }
    if (m_outFile.active) {
        log(QStringLiteral("WARN"), QStringLiteral("Another file transfer is already in progress."));
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        log(QStringLiteral("FILE ERR"),
            QString("Cannot open file '%1': %2").arg(filePath, file.errorString()));
        return false;
    }

    QFileInfo fileInfo(file);
    if (fileInfo.size() > 50 * 1024 * 1024) {
        log(QStringLiteral("FILE ERR"), QStringLiteral("File exceeds 50 MB limit."));
        file.close();
        return false;
    }

    m_outFile.data = file.readAll();
    file.close();

    m_outFile.active = true;
    m_outFile.transferId = QString::number(QDateTime::currentMSecsSinceEpoch());
    m_outFile.fileName = fileInfo.fileName();
    m_outFile.targetTopic = targetTopic;
    m_outFile.sha256Hex = QString::fromLatin1(
        QCryptographicHash::hash(m_outFile.data, QCryptographicHash::Sha256).toHex());
    m_outFile.totalChunks = m_outFile.data.isEmpty()
                                ? 0
                                : ((m_outFile.data.size() + m_outFile.chunkSize - 1) / m_outFile.chunkSize);
    m_outFile.currentChunk = 0;
    m_outFile.lastLoggedMilestone = 0;

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

    log(QStringLiteral("FILE SEND"),
        QString("Uploading '%1' (%2 bytes, %3 chunks) -> [%4]...")
            .arg(m_outFile.fileName)
            .arg(m_outFile.data.size())
            .arg(m_outFile.totalChunks)
            .arg(m_outFile.targetTopic));

    m_fileChunkTimer->start();
    return true;
}

void MqttConsoleClient::onSendNextFileChunk()
{
    if (!m_outFile.active || m_client->state() != QMqttClient::Connected) {
        m_fileChunkTimer->stop();
        m_outFile.active = false;
        m_outFile.data.clear();
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
        if (progress >= m_outFile.lastLoggedMilestone + 25 && progress < 100) {
            m_outFile.lastLoggedMilestone = (progress / 25) * 25;
            log(QStringLiteral("FILE SEND"),
                QString("Progress '%1': %2% (%3/%4 chunks)")
                    .arg(m_outFile.fileName)
                    .arg(progress)
                    .arg(m_outFile.currentChunk)
                    .arg(m_outFile.totalChunks));
        }
        return;
    }

    m_fileChunkTimer->stop();

    QJsonObject endObj;
    endObj[QStringLiteral("mqtt_type")] = QStringLiteral("file_transfer");
    endObj[QStringLiteral("stage")] = QStringLiteral("end");
    endObj[QStringLiteral("transfer_id")] = m_outFile.transferId;
    endObj[QStringLiteral("filename")] = m_outFile.fileName;
    endObj[QStringLiteral("sha256")] = m_outFile.sha256Hex;

    QByteArray payload = QJsonDocument(endObj).toJson(QJsonDocument::Compact);
    m_client->publish(QMqttTopicName(m_outFile.targetTopic), payload, 1, false);

    log(QStringLiteral("FILE SEND"),
        QString("Completed '%1' (100%, %2 bytes, SHA-256: %3...)")
            .arg(m_outFile.fileName)
            .arg(m_outFile.data.size())
            .arg(m_outFile.sha256Hex.left(12)));

    m_outFile.active = false;
    m_outFile.data.clear();
}

bool MqttConsoleClient::tryHandleFileTransferMessage(const QByteArray &message, const QString &topic)
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
        m_inFile.lastLoggedMilestone = 0;
        m_inFile.expectedSha256 = obj.value(QStringLiteral("sha256")).toString();
        m_inFile.buffer.clear();
        if (m_inFile.totalSize > 0 && m_inFile.totalSize <= 50 * 1024 * 1024) {
            m_inFile.buffer.reserve(static_cast<int>(m_inFile.totalSize));
        }

        log(QStringLiteral("FILE RECV"),
            QString("Receiving '%1' (%2 bytes, %3 chunks) from [%4]...")
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
        if (progress >= m_inFile.lastLoggedMilestone + 25 && progress < 100) {
            m_inFile.lastLoggedMilestone = (progress / 25) * 25;
            log(QStringLiteral("FILE RECV"),
                QString("Progress '%1': %2% (%3/%4 chunks)")
                    .arg(m_inFile.fileName)
                    .arg(progress)
                    .arg(m_inFile.receivedChunks)
                    .arg(m_inFile.totalChunks));
        }
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

            QString verifyStr = hashOk ? QStringLiteral("SHA-256 Verified OK")
                                       : QStringLiteral("SHA-256 MISMATCH!");
            log(QStringLiteral("FILE SAVED"),
                QString("'%1' (%2 bytes, %3) -> %4")
                    .arg(m_inFile.fileName)
                    .arg(m_inFile.buffer.size())
                    .arg(verifyStr, savePath));

            // Send acknowledgement back to PC on pubTopic
            QJsonObject ack;
            ack[QStringLiteral("device")] = m_config.clientId;
            ack[QStringLiteral("event")] = QStringLiteral("file_ack");
            ack[QStringLiteral("filename")] = m_inFile.fileName;
            ack[QStringLiteral("size_bytes")] = m_inFile.buffer.size();
            ack[QStringLiteral("sha256_verified")] = hashOk;
            ack[QStringLiteral("saved_to")] = savePath;
            ack[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
            publishMessage(m_config.pubTopic, QString::fromUtf8(QJsonDocument(ack).toJson(QJsonDocument::Compact)), 1, false);
        } else {
            log(QStringLiteral("FILE ERR"),
                QString("Failed to save '%1' to %2: %3")
                    .arg(m_inFile.fileName, savePath, outFile.errorString()));
        }

        m_inFile.active = false;
        m_inFile.buffer.clear();
        return true;
    }

    return true;
}

QString MqttConsoleClient::receivedFilesDir() const
{
    QString dirPath = QCoreApplication::applicationDirPath() + QStringLiteral("/received_files");
    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.absolutePath();
}

void MqttConsoleClient::processIncomingCommand(const QString &payload)
{
    if (payload.isEmpty()) {
        return;
    }

    QString cmd;
    int intervalArg = -1;
    QString filePathArg;

    // 1. Check if payload is JSON command
    if (payload.startsWith(QLatin1Char('{')) && payload.endsWith(QLatin1Char('}'))) {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject obj = doc.object();
            cmd = obj.value(QStringLiteral("cmd")).toString().trimmed().toLower();
            if (cmd.isEmpty()) {
                cmd = obj.value(QStringLiteral("action")).toString().trimmed().toLower();
            }
            if (obj.contains(QStringLiteral("interval"))) {
                intervalArg = obj.value(QStringLiteral("interval")).toInt(-1);
            }
            if (obj.contains(QStringLiteral("path"))) {
                filePathArg = obj.value(QStringLiteral("path")).toString().trimmed();
            } else if (obj.contains(QStringLiteral("file"))) {
                filePathArg = obj.value(QStringLiteral("file")).toString().trimmed();
            }
        }
    } else {
        // 2. Check plain text command
        QStringList parts = payload.split(QLatin1Char(' '), QString::SkipEmptyParts);
        QString firstWord = parts.value(0).toLower();
        if (firstWord == QStringLiteral("ping") ||
            firstWord == QStringLiteral("temp") ||
            firstWord == QStringLiteral("get_temp") ||
            firstWord == QStringLiteral("ram") ||
            firstWord == QStringLiteral("get_ram") ||
            firstWord == QStringLiteral("status") ||
            firstWord == QStringLiteral("get_status") ||
            firstWord == QStringLiteral("sysinfo") ||
            firstWord == QStringLiteral("get_sysinfo") ||
            firstWord == QStringLiteral("help")) {
            cmd = firstWord;
        } else if ((firstWord == QStringLiteral("heartbeat") || firstWord == QStringLiteral("set_heartbeat")) && parts.size() >= 2) {
            cmd = QStringLiteral("set_heartbeat");
            intervalArg = parts.value(1).toInt();
        } else if ((firstWord == QStringLiteral("getfile") || firstWord == QStringLiteral("get_file") ||
                    firstWord == QStringLiteral("sendfile")) && parts.size() >= 2) {
            cmd = QStringLiteral("get_file");
            filePathArg = payload.section(QLatin1Char(' '), 1).trimmed();
        }
    }

    if (cmd.isEmpty()) {
        // Normal chat/data message, no command action needed
        return;
    }

    log(QStringLiteral("CMD"), QString("Executing remote command: '%1'").arg(cmd));
    QString timeStr = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));

    if (cmd == QStringLiteral("ping")) {
        QJsonObject reply;
        reply[QStringLiteral("device")] = m_config.clientId;
        reply[QStringLiteral("event")] = QStringLiteral("cmd_reply");
        reply[QStringLiteral("cmd")] = QStringLiteral("ping");
        reply[QStringLiteral("reply")] = QStringLiteral("pong");
        reply[QStringLiteral("timestamp")] = timeStr;
        publishMessage(m_config.pubTopic, QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
    } else if (cmd == QStringLiteral("temp") || cmd == QStringLiteral("get_temp")) {
        SystemMetrics m = readSystemMetrics();
        QJsonObject reply;
        reply[QStringLiteral("device")] = m_config.clientId;
        reply[QStringLiteral("event")] = QStringLiteral("cmd_reply");
        reply[QStringLiteral("cmd")] = QStringLiteral("get_temp");
        reply[QStringLiteral("cpu_temp_c")] = std::round(m.cpuTempC * 10.0) / 10.0;
        reply[QStringLiteral("timestamp")] = timeStr;
        publishMessage(m_config.pubTopic, QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
    } else if (cmd == QStringLiteral("ram") || cmd == QStringLiteral("get_ram")) {
        SystemMetrics m = readSystemMetrics();
        QJsonObject reply;
        reply[QStringLiteral("device")] = m_config.clientId;
        reply[QStringLiteral("event")] = QStringLiteral("cmd_reply");
        reply[QStringLiteral("cmd")] = QStringLiteral("get_ram");
        reply[QStringLiteral("ram_used_mb")] = m.ramUsedMB;
        reply[QStringLiteral("ram_total_mb")] = m.ramTotalMB;
        reply[QStringLiteral("ram_usage_pct")] = std::round(m.ramUsagePercent * 10.0) / 10.0;
        reply[QStringLiteral("timestamp")] = timeStr;
        publishMessage(m_config.pubTopic, QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
    } else if (cmd == QStringLiteral("status") || cmd == QStringLiteral("get_status") ||
               cmd == QStringLiteral("sysinfo") || cmd == QStringLiteral("get_sysinfo")) {
        publishMessage(m_config.pubTopic, buildTelemetryJson(QStringLiteral("cmd_reply")));
    } else if (cmd == QStringLiteral("set_heartbeat")) {
        if (intervalArg >= 0) {
            setHeartbeatInterval(intervalArg);
            QJsonObject reply;
            reply[QStringLiteral("device")] = m_config.clientId;
            reply[QStringLiteral("event")] = QStringLiteral("cmd_reply");
            reply[QStringLiteral("cmd")] = QStringLiteral("set_heartbeat");
            reply[QStringLiteral("heartbeat_interval_s")] = m_config.heartbeatInterval;
            reply[QStringLiteral("timestamp")] = timeStr;
            publishMessage(m_config.pubTopic, QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
        }
    } else if (cmd == QStringLiteral("get_file") || cmd == QStringLiteral("getfile")) {
        if (!startFileSend(filePathArg, m_config.pubTopic)) {
            QJsonObject reply;
            reply[QStringLiteral("device")] = m_config.clientId;
            reply[QStringLiteral("event")] = QStringLiteral("cmd_reply");
            reply[QStringLiteral("cmd")] = QStringLiteral("get_file");
            reply[QStringLiteral("status")] = QStringLiteral("error");
            reply[QStringLiteral("error")] = QString("Failed to open or send file: %1").arg(filePathArg);
            reply[QStringLiteral("timestamp")] = timeStr;
            publishMessage(m_config.pubTopic, QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
        }
    } else if (cmd == QStringLiteral("help")) {
        QJsonObject reply;
        reply[QStringLiteral("device")] = m_config.clientId;
        reply[QStringLiteral("event")] = QStringLiteral("cmd_reply");
        reply[QStringLiteral("available_commands")] = QJsonArray::fromStringList(
            {QStringLiteral("ping"), QStringLiteral("temp"), QStringLiteral("ram"),
             QStringLiteral("sysinfo"), QStringLiteral("status"),
             QStringLiteral("heartbeat <sec>"), QStringLiteral("getfile <path>")});
        publishMessage(m_config.pubTopic, QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
    }
}

MqttConsoleClient::SystemMetrics MqttConsoleClient::readSystemMetrics() const
{
    SystemMetrics metrics;

    // 1. Read CPU Temperature (/sys/class/thermal/thermal_zone0/temp)
    QFile tempFile(QStringLiteral("/sys/class/thermal/thermal_zone0/temp"));
    if (tempFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = tempFile.readAll().trimmed();
        bool ok = false;
        double val = data.toDouble(&ok);
        if (ok) {
            metrics.cpuTempC = (val > 200.0) ? (val / 1000.0) : val;
        }
        tempFile.close();
    }

    // 2. Read Memory Info (/proc/meminfo)
    QFile memFile(QStringLiteral("/proc/meminfo"));
    if (memFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qint64 memTotalKB = 0;
        qint64 memAvailKB = 0;
        const QList<QByteArray> lines = memFile.readAll().split('\n');
        for (const QByteArray &line : lines) {
            if (line.startsWith("MemTotal:")) {
                QStringList parts = QString::fromLatin1(line).split(QLatin1Char(' '), QString::SkipEmptyParts);
                if (parts.size() >= 2) {
                    memTotalKB = parts.at(1).toLongLong();
                }
            } else if (line.startsWith("MemAvailable:")) {
                QStringList parts = QString::fromLatin1(line).split(QLatin1Char(' '), QString::SkipEmptyParts);
                if (parts.size() >= 2) {
                    memAvailKB = parts.at(1).toLongLong();
                }
            }
        }
        if (memTotalKB > 0) {
            qint64 usedKB = memTotalKB - memAvailKB;
            metrics.ramTotalMB = memTotalKB / 1024;
            metrics.ramUsedMB = usedKB / 1024;
            metrics.ramUsagePercent = (static_cast<double>(usedKB) * 100.0) / static_cast<double>(memTotalKB);
        }
        memFile.close();
    }

    // 3. Read Load Average (/proc/loadavg)
    QFile loadFile(QStringLiteral("/proc/loadavg"));
    if (loadFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromLatin1(loadFile.readAll().trimmed());
        QStringList parts = content.split(QLatin1Char(' '), QString::SkipEmptyParts);
        if (parts.size() >= 3) {
            metrics.loadAvg = QString("%1 %2 %3").arg(parts.at(0), parts.at(1), parts.at(2));
        }
        loadFile.close();
    }

    // 4. Read System Uptime (/proc/uptime)
    QFile upFile(QStringLiteral("/proc/uptime"));
    if (upFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromLatin1(upFile.readAll().trimmed());
        QStringList parts = content.split(QLatin1Char(' '), QString::SkipEmptyParts);
        if (!parts.isEmpty()) {
            metrics.sysUptimeSec = static_cast<qint64>(parts.at(0).toDouble());
        }
        upFile.close();
    }

    return metrics;
}

QString MqttConsoleClient::buildTelemetryJson(const QString &eventType) const
{
    SystemMetrics m = readSystemMetrics();
    QJsonObject obj;
    obj[QStringLiteral("device")] = m_config.clientId;
    obj[QStringLiteral("event")] = eventType;
    obj[QStringLiteral("status")] = QStringLiteral("online");
    obj[QStringLiteral("cpu_temp_c")] = std::round(m.cpuTempC * 10.0) / 10.0;
    obj[QStringLiteral("ram_used_mb")] = m.ramUsedMB;
    obj[QStringLiteral("ram_total_mb")] = m.ramTotalMB;
    obj[QStringLiteral("ram_usage_pct")] = std::round(m.ramUsagePercent * 10.0) / 10.0;
    obj[QStringLiteral("load_avg")] = m.loadAvg;
    obj[QStringLiteral("sys_uptime_s")] = m.sysUptimeSec;
    obj[QStringLiteral("app_uptime_s")] = m_appUptimeTimer.elapsed() / 1000;
    obj[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));

    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void MqttConsoleClient::setHeartbeatInterval(int seconds)
{
    m_config.heartbeatInterval = seconds;
    if (seconds > 0) {
        m_heartbeatTimer->setInterval(seconds * 1000);
        if (m_client->state() == QMqttClient::Connected) {
            m_heartbeatTimer->start();
        }
        log(QStringLiteral("INFO"), QString("Heartbeat telemetry enabled: every %1s").arg(seconds));
    } else {
        m_heartbeatTimer->stop();
        log(QStringLiteral("INFO"), QStringLiteral("Heartbeat telemetry disabled."));
    }
}

void MqttConsoleClient::onMqttPingResponseReceived()
{
    log(QStringLiteral("PING"), QStringLiteral("MQTT Ping ACK received from Broker (Keep-alive OK)."));
}

void MqttConsoleClient::sendPingProbe()
{
    log(QStringLiteral("PING"), QString("Pinging broker at %1:%2...").arg(m_config.host).arg(m_config.port));
    m_pingTimer.start();
    m_pingSocket->abort();
    m_pingSocket->connectToHost(m_config.host, m_config.port);
}

void MqttConsoleClient::onTcpPingConnected()
{
    qint64 latency = m_pingTimer.elapsed();
    QString sessionState = (m_client->state() == QMqttClient::Connected)
                               ? QStringLiteral("MQTT Connected")
                               : QStringLiteral("MQTT Disconnected");

    log(QStringLiteral("PING"),
        QString("Broker %1:%2 reachable | Latency: %3 ms (%4)")
            .arg(m_config.host)
            .arg(m_config.port)
            .arg(latency)
            .arg(sessionState));
    m_pingSocket->disconnectFromHost();
}

void MqttConsoleClient::onTcpPingError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    log(QStringLiteral("PING"),
        QString("Ping Failed: Unable to reach %1:%2 (%3)")
            .arg(m_config.host)
            .arg(m_config.port)
            .arg(m_pingSocket->errorString()));
}

void MqttConsoleClient::onConsoleInput(const QString &rawLine)
{
    QString line = rawLine.trimmed();
    if (line.isEmpty()) {
        return;
    }

    if (line.startsWith(QLatin1Char(':'))) {
        QStringList parts = line.split(QLatin1Char(' '), QString::SkipEmptyParts);
        QString cmd = parts.value(0).toLower();

        if (cmd == QStringLiteral(":help")) {
            printHelp();
        } else if (cmd == QStringLiteral(":ping")) {
            sendPingProbe();
        } else if (cmd == QStringLiteral(":sendfile") || cmd == QStringLiteral(":send")) {
            if (parts.size() >= 2) {
                QString filePath = line.section(QLatin1Char(' '), 1).trimmed();
                startFileSend(filePath, m_config.pubTopic);
            } else {
                log(QStringLiteral("WARN"), QStringLiteral("Usage: :sendfile <file_path>"));
            }
        } else if (cmd == QStringLiteral(":sysinfo") || cmd == QStringLiteral(":hw")) {
            SystemMetrics m = readSystemMetrics();
            std::cout << "\n--- Orange Pi Hardware Telemetry ---" << std::endl;
            std::cout << "CPU Temp     : " << (std::round(m.cpuTempC * 10.0) / 10.0) << " C" << std::endl;
            std::cout << "RAM Usage    : " << m.ramUsedMB << " MB / " << m.ramTotalMB << " MB ("
                      << (std::round(m.ramUsagePercent * 10.0) / 10.0) << "%)" << std::endl;
            std::cout << "Load Average : " << m.loadAvg.toStdString() << std::endl;
            std::cout << "Sys Uptime   : " << m.sysUptimeSec << " s" << std::endl;
            std::cout << "------------------------------------\n" << std::endl;
            if (m_client->state() == QMqttClient::Connected) {
                publishMessage(m_config.pubTopic, buildTelemetryJson(QStringLiteral("manual_telemetry")));
            }
        } else if (cmd == QStringLiteral(":heartbeat")) {
            if (parts.size() >= 2) {
                setHeartbeatInterval(parts.value(1).toInt());
            } else {
                log(QStringLiteral("INFO"), QString("Current heartbeat interval: %1s (Usage: :heartbeat <seconds>)").arg(m_config.heartbeatInterval));
            }
        } else if (cmd == QStringLiteral(":pubtopic") || cmd == QStringLiteral(":topic")) {
            if (parts.size() >= 2) {
                QString newTopic = parts.value(1).trimmed();
                m_config.pubTopic = newTopic;
                log(QStringLiteral("INFO"), QString("Default Publish Topic changed to: %1").arg(newTopic));
            } else {
                log(QStringLiteral("INFO"), QString("Current Publish Topic is: %1").arg(m_config.pubTopic));
            }
        } else if (cmd == QStringLiteral(":subtopic")) {
            if (parts.size() >= 2) {
                QString newTopic = parts.value(1).trimmed();
                if (m_client->state() == QMqttClient::Connected && !m_config.subTopic.isEmpty()) {
                    unsubscribeTopic(m_config.subTopic);
                }
                m_config.subTopic = newTopic;
                log(QStringLiteral("INFO"), QString("Default Subscribe Topic changed to: %1").arg(newTopic));
                if (m_client->state() == QMqttClient::Connected) {
                    subscribeTopic(newTopic, 1);
                }
            } else {
                log(QStringLiteral("INFO"), QString("Current Subscribe Topic is: %1").arg(m_config.subTopic));
            }
        } else if (cmd == QStringLiteral(":sub")) {
            if (parts.size() >= 2) {
                subscribeTopic(parts.value(1).trimmed(), 1);
            } else {
                log(QStringLiteral("WARN"), QStringLiteral("Usage: :sub <topic_name>"));
            }
        } else if (cmd == QStringLiteral(":unsub")) {
            if (parts.size() >= 2) {
                unsubscribeTopic(parts.value(1).trimmed());
            } else {
                log(QStringLiteral("WARN"), QStringLiteral("Usage: :unsub <topic_name>"));
            }
        } else if (cmd == QStringLiteral(":pub")) {
            if (parts.size() >= 3) {
                QString targetTopic = parts.value(1);
                QString message = line.section(QLatin1Char(' '), 2);
                publishMessage(targetTopic, message, 0, false);
            } else {
                log(QStringLiteral("WARN"), QStringLiteral("Usage: :pub <topic> <message>"));
            }
        } else if (cmd == QStringLiteral(":status")) {
            std::cout << "\n--- Orange Pi MQTT Status ---" << std::endl;
            std::cout << "Broker       : " << m_config.host.toStdString() << ":" << m_config.port << std::endl;
            std::cout << "State        : " << (m_client->state() == QMqttClient::Connected ? "Connected" : "Disconnected") << std::endl;
            std::cout << "Client ID    : " << m_config.clientId.toStdString() << std::endl;
            std::cout << "Sub Topic    : " << m_config.subTopic.toStdString() << std::endl;
            std::cout << "Pub Topic    : " << m_config.pubTopic.toStdString() << std::endl;
            std::cout << "Status Topic : " << m_config.statusTopic.toStdString() << " (LWT)" << std::endl;
            std::cout << "Recv Folder  : " << receivedFilesDir().toStdString() << std::endl;
            std::cout << "Heartbeat    : " << m_config.heartbeatInterval << " s" << std::endl;
            std::cout << "Active Subs  : " << m_subscriptions.keys().join(QStringLiteral(", ")).toStdString() << std::endl;
            std::cout << "-----------------------------\n" << std::endl;
        } else if (cmd == QStringLiteral(":reconnect")) {
            log(QStringLiteral("INFO"), QStringLiteral("Reconnecting on request..."));
            m_client->disconnectFromHost();
            m_reconnectTimer->stop();
            tryConnect();
        } else if (cmd == QStringLiteral(":quit") || cmd == QStringLiteral(":q")) {
            log(QStringLiteral("INFO"), QStringLiteral("Publishing offline status and exiting..."));
            gracefulShutdown();
        } else {
            log(QStringLiteral("WARN"), QString("Unknown command: %1. Type :help for commands.").arg(cmd));
        }
    } else {
        // Plain text is published directly to default Pub Topic
        publishMessage(m_config.pubTopic, line, 0, false);
    }
}

void MqttConsoleClient::onHeartbeatTimeout()
{
    if (m_client->state() != QMqttClient::Connected) {
        return;
    }
    publishMessage(m_config.pubTopic, buildTelemetryJson(QStringLiteral("heartbeat")), 0, false);
}

void MqttConsoleClient::publishOnlineStatus(bool online)
{
    if (m_client->state() != QMqttClient::Connected || m_config.statusTopic.isEmpty()) {
        return;
    }
    QString statusStr = online ? QStringLiteral("online") : QStringLiteral("offline");
    QString payload = QString("{\"device\":\"%1\",\"status\":\"%2\"}")
                          .arg(m_config.clientId, statusStr);
    m_client->publish(QMqttTopicName(m_config.statusTopic), payload.toUtf8(), 1, true);
    log(QStringLiteral("LWT/STATUS"),
        QString("Published [%1] (Retain) -> %2").arg(m_config.statusTopic, payload));
}

void MqttConsoleClient::publishMessage(const QString &topic, const QString &message, quint8 qos, bool retain)
{
    if (m_client->state() != QMqttClient::Connected) {
        log(QStringLiteral("WARN"), QStringLiteral("Cannot publish: Not connected to Broker!"));
        return;
    }

    qint32 id = m_client->publish(QMqttTopicName(topic), message.toUtf8(), qos, retain);
    if (id == -1) {
        log(QStringLiteral("ERROR"), QString("Failed to publish to topic: %1").arg(topic));
    } else {
        log(QStringLiteral("SENT"), QString("[%1]: %2").arg(topic, message));
    }
}

void MqttConsoleClient::subscribeTopic(const QString &topic, quint8 qos)
{
    if (topic.isEmpty()) {
        return;
    }
    if (m_subscriptions.contains(topic)) {
        log(QStringLiteral("INFO"), QString("Already subscribed to topic: %1").arg(topic));
        return;
    }
    QMqttSubscription *sub = m_client->subscribe(QMqttTopicFilter(topic), qos);
    if (!sub) {
        log(QStringLiteral("ERROR"), QString("Failed to subscribe to topic: %1").arg(topic));
    } else {
        m_subscriptions[topic] = sub;
        log(QStringLiteral("SUB"), QString("Subscribed to topic: %1 (QoS %2)").arg(topic).arg(qos));
    }
}

void MqttConsoleClient::unsubscribeTopic(const QString &topic)
{
    if (topic.isEmpty()) {
        return;
    }
    m_client->unsubscribe(QMqttTopicFilter(topic));
    m_subscriptions.remove(topic);
    log(QStringLiteral("UNSUB"), QString("Unsubscribed from topic: %1").arg(topic));
}

void MqttConsoleClient::printHelp()
{
    std::cout << "\n--- Local Console Commands ---" << std::endl;
    std::cout << "  <message>              : Publish message to default Pub Topic (" << m_config.pubTopic.toStdString() << ")" << std::endl;
    std::cout << "  :sendfile <file_path>  : Send any file (text/binary/image) to PC" << std::endl;
    std::cout << "  :sysinfo (or :hw)      : Read & publish Orange Pi CPU Temp, RAM, Load, Uptime" << std::endl;
    std::cout << "  :heartbeat <seconds>   : Set auto-telemetry interval (0 to disable)" << std::endl;
    std::cout << "  :pub <topic> <message> : Publish message to a specific topic" << std::endl;
    std::cout << "  :sub <topic>           : Subscribe to an additional topic" << std::endl;
    std::cout << "  :unsub <topic>         : Unsubscribe from a topic" << std::endl;
    std::cout << "  :pubtopic [new_topic]  : View or change default Publish Topic" << std::endl;
    std::cout << "  :subtopic [new_topic]  : View or change default Subscribe Topic" << std::endl;
    std::cout << "  :ping                  : Measure TCP latency (ms) to Broker" << std::endl;
    std::cout << "  :status                : Show connection status, topics, and active subs" << std::endl;
    std::cout << "  :reconnect             : Force reconnect to broker" << std::endl;
    std::cout << "  :quit (or :q)          : Publish offline status and exit" << std::endl;
    std::cout << "------------------------------\n" << std::endl;
}

void MqttConsoleClient::log(const QString &tag, const QString &message)
{
    QString timeStr = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    std::cout << QString("[%1] [%2] %3")
                     .arg(timeStr, tag, message)
                     .toStdString()
              << std::endl;
}
