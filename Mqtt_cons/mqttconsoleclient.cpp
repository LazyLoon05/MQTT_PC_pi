#include "mqttconsoleclient.h"

#include <iostream>
#include <string>
#include <QCoreApplication>
#include <QDateTime>
#include <QTextStream>

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
    while (m_running && std::getline(std::cin, line)) {
        if (!m_running) {
            break;
        }
        emit lineReceived(QString::fromStdString(line));
    }
}

MqttConsoleClient::MqttConsoleClient(const Config &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_client(new QMqttClient(this))
    , m_sub(nullptr)
    , m_inputWorker(new ConsoleInputWorker(this))
    , m_heartbeatTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
    , m_uptimeSeconds(0)
{
    // Connect MQTT signals
    connect(m_client, &QMqttClient::connected, this, &MqttConsoleClient::onMqttConnected);
    connect(m_client, &QMqttClient::disconnected, this, &MqttConsoleClient::onMqttDisconnected);
    connect(m_client, &QMqttClient::stateChanged, this, &MqttConsoleClient::onMqttStateChanged);
    connect(m_client, &QMqttClient::errorChanged, this, &MqttConsoleClient::onMqttErrorChanged);
    connect(m_client, &QMqttClient::messageReceived, this, &MqttConsoleClient::onMqttMessageReceived);
    connect(m_client, &QMqttClient::pingResponseReceived, this, &MqttConsoleClient::onMqttPingResponseReceived);

    // Connect Console Input worker
    connect(m_inputWorker, &ConsoleInputWorker::lineReceived, this, &MqttConsoleClient::onConsoleInput);

    // Reconnect timer configuration (every 5 seconds when disconnected)
    m_reconnectTimer->setInterval(5000);
    connect(m_reconnectTimer, &QTimer::timeout, this, &MqttConsoleClient::tryConnect);

    // Heartbeat timer configuration
    if (m_config.heartbeatInterval > 0) {
        m_heartbeatTimer->setInterval(m_config.heartbeatInterval * 1000);
        connect(m_heartbeatTimer, &QTimer::timeout, this, &MqttConsoleClient::onHeartbeatTimeout);
    }
}

MqttConsoleClient::~MqttConsoleClient()
{
    m_inputWorker->stop();
    m_inputWorker->terminate();
    m_inputWorker->wait(1000);

    if (m_client->state() == QMqttClient::Connected) {
        m_client->disconnectFromHost();
    }
}

void MqttConsoleClient::start()
{
    std::cout << "======================================================" << std::endl;
    std::cout << "  MQTT Console Client (Orange Pi / Linux / Embedded)  " << std::endl;
    std::cout << "======================================================" << std::endl;
    std::cout << "Broker Host : " << m_config.host.toStdString() << ":" << m_config.port << std::endl;
    std::cout << "Client ID   : " << m_config.clientId.toStdString() << std::endl;
    std::cout << "Topic       : " << m_config.topic.toStdString() << std::endl;
    if (!m_config.username.isEmpty()) {
        std::cout << "Username    : " << m_config.username.toStdString() << std::endl;
    }
    if (m_config.heartbeatInterval > 0) {
        std::cout << "Heartbeat   : Every " << m_config.heartbeatInterval << "s" << std::endl;
    }
    std::cout << "------------------------------------------------------" << std::endl;
    std::cout << "Type any message and press Enter to publish." << std::endl;
    std::cout << "Type :help for a list of console commands." << std::endl;
    std::cout << "======================================================" << std::endl << std::endl;

    // Start background input listener
    m_inputWorker->start();

    // Initiate first connection
    tryConnect();
}

void MqttConsoleClient::tryConnect()
{
    if (m_client->state() == QMqttClient::Connected || m_client->state() == QMqttClient::Connecting) {
        return;
    }

    log(QStringLiteral("CONNECTING"),
        QString("Connecting to broker %1:%2...").arg(m_config.host).arg(m_config.port));

    m_client->setHostname(m_config.host);
    m_client->setPort(m_config.port);
    m_client->setClientId(m_config.clientId);
    m_client->setKeepAlive(60);

    if (!m_config.username.isEmpty()) {
        m_client->setUsername(m_config.username);
    }
    if (!m_config.password.isEmpty()) {
        m_client->setPassword(m_config.password);
    }

    m_client->connectToHost();
}

void MqttConsoleClient::onMqttConnected()
{
    m_reconnectTimer->stop();
    log(QStringLiteral("CONNECTED"), QString("Successfully connected to broker %1:%2!").arg(m_config.host).arg(m_config.port));

    // Subscribe to default topic
    subscribeTopic(m_config.topic);

    // Start heartbeat timer if configured
    if (m_config.heartbeatInterval > 0 && !m_heartbeatTimer->isActive()) {
        m_heartbeatTimer->start();
    }
}

void MqttConsoleClient::onMqttDisconnected()
{
    log(QStringLiteral("DISCONNECTED"), QStringLiteral("Lost connection to broker. Will retry in 5s..."));
    m_sub = nullptr;

    if (m_heartbeatTimer->isActive()) {
        m_heartbeatTimer->stop();
    }

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
    log(QStringLiteral("RECV"), QString("[%1]: %2").arg(topic.name(), QString::fromUtf8(message)));
}

void MqttConsoleClient::onMqttPingResponseReceived()
{
    log(QStringLiteral("PING"), QStringLiteral("MQTT Ping ACK received from Broker (Keep-alive OK)."));
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
            if (m_client->state() == QMqttClient::Connected) {
                log(QStringLiteral("PING"), QStringLiteral("Sending MQTT PINGREQ to Broker..."));
                m_client->requestPing();
            } else {
                log(QStringLiteral("WARN"), QStringLiteral("Client is not connected. Cannot send ping."));
            }
        } else if (cmd == QStringLiteral(":topic")) {
            if (parts.size() >= 2) {
                QString newTopic = parts.value(1).trimmed();
                m_config.topic = newTopic;
                log(QStringLiteral("INFO"), QString("Default topic changed to: %1").arg(newTopic));
                if (m_client->state() == QMqttClient::Connected) {
                    subscribeTopic(newTopic);
                }
            } else {
                log(QStringLiteral("INFO"), QString("Current default topic is: %1").arg(m_config.topic));
            }
        } else if (cmd == QStringLiteral(":sub")) {
            if (parts.size() >= 2) {
                subscribeTopic(parts.value(1).trimmed());
            } else {
                log(QStringLiteral("WARN"), QStringLiteral("Usage: :sub <topic_name>"));
            }
        } else if (cmd == QStringLiteral(":pub")) {
            if (parts.size() >= 3) {
                QString targetTopic = parts.value(1);
                QString message = line.section(QLatin1Char(' '), 2);
                publishMessage(targetTopic, message);
            } else {
                log(QStringLiteral("WARN"), QStringLiteral("Usage: :pub <topic> <message>"));
            }
        } else if (cmd == QStringLiteral(":status")) {
            std::cout << "--- Status ---" << std::endl;
            std::cout << "Broker   : " << m_config.host.toStdString() << ":" << m_config.port << std::endl;
            std::cout << "State    : " << (m_client->state() == QMqttClient::Connected ? "Connected" : "Disconnected") << std::endl;
            std::cout << "Topic    : " << m_config.topic.toStdString() << std::endl;
            std::cout << "Client ID: " << m_config.clientId.toStdString() << std::endl;
            std::cout << "--------------" << std::endl;
        } else if (cmd == QStringLiteral(":reconnect")) {
            log(QStringLiteral("INFO"), QStringLiteral("Reconnecting on request..."));
            m_client->disconnectFromHost();
            m_reconnectTimer->stop();
            tryConnect();
        } else if (cmd == QStringLiteral(":quit") || cmd == QStringLiteral(":q")) {
            log(QStringLiteral("INFO"), QStringLiteral("Exiting application..."));
            QCoreApplication::quit();
        } else {
            log(QStringLiteral("WARN"), QString("Unknown command: %1. Type :help for commands.").arg(cmd));
        }
    } else {
        // Plain text is published directly to default topic
        publishMessage(m_config.topic, line);
    }
}

void MqttConsoleClient::onHeartbeatTimeout()
{
    if (m_client->state() != QMqttClient::Connected) {
        return;
    }
    m_uptimeSeconds += m_config.heartbeatInterval;
    QString payload = QString("{\"device\":\"%1\",\"status\":\"online\",\"uptime_s\":%2}")
                          .arg(m_config.clientId)
                          .arg(m_uptimeSeconds);
    publishMessage(m_config.topic, payload);
}

void MqttConsoleClient::publishMessage(const QString &topic, const QString &message)
{
    if (m_client->state() != QMqttClient::Connected) {
        log(QStringLiteral("WARN"), QStringLiteral("Cannot publish: Not connected to Broker!"));
        return;
    }

    qint32 id = m_client->publish(QMqttTopicName(topic), message.toUtf8(), 0);
    if (id == -1) {
        log(QStringLiteral("ERROR"), QString("Failed to publish to topic: %1").arg(topic));
    } else {
        log(QStringLiteral("SENT"), QString("[%1]: %2").arg(topic, message));
    }
}

void MqttConsoleClient::subscribeTopic(const QString &topic)
{
    if (topic.isEmpty()) {
        return;
    }
    QMqttSubscription *sub = m_client->subscribe(QMqttTopicFilter(topic), 0);
    if (!sub) {
        log(QStringLiteral("ERROR"), QString("Failed to subscribe to topic: %1").arg(topic));
    } else {
        log(QStringLiteral("SUB"), QString("Subscribed to topic: %1").arg(topic));
    }
}

void MqttConsoleClient::printHelp()
{
    std::cout << "\n--- Console Commands ---" << std::endl;
    std::cout << "  <message>              : Publish message to default topic" << std::endl;
    std::cout << "  :pub <topic> <message> : Publish message to a specific topic" << std::endl;
    std::cout << "  :sub <topic>           : Subscribe to an additional topic" << std::endl;
    std::cout << "  :topic [new_topic]     : View or change default topic" << std::endl;
    std::cout << "  :ping                  : Send MQTT PINGREQ to Broker" << std::endl;
    std::cout << "  :status                : Show current connection and settings" << std::endl;
    std::cout << "  :reconnect             : Force reconnect to broker" << std::endl;
    std::cout << "  :quit (or :q)          : Exit application" << std::endl;
    std::cout << "------------------------\n" << std::endl;
}

void MqttConsoleClient::log(const QString &tag, const QString &message)
{
    QString timeStr = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    std::cout << QString("[%1] [%2] %3")
                     .arg(timeStr, tag, message)
                     .toStdString()
              << std::endl;
}
