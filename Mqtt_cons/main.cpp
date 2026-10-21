#include <QCoreApplication>
#include <QCommandLineParser>
#include "mqttconsoleclient.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Mqtt_cons"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt MQTT Console Client for Orange Pi / Linux"));
    QCommandLineOption helpOption(QStringList() << QStringLiteral("help") << QStringLiteral("?"),
                                  QStringLiteral("Displays this help."));
    parser.addOption(helpOption);
    parser.addVersionOption();

    QCommandLineOption hostOption(QStringList() << QStringLiteral("h") << QStringLiteral("host") << QStringLiteral("b") << QStringLiteral("broker"),
                                  QStringLiteral("MQTT broker hostname or IP."),
                                  QStringLiteral("host"),
                                  QStringLiteral("192.168.0.104"));
    parser.addOption(hostOption);

    QCommandLineOption portOption(QStringList() << QStringLiteral("p") << QStringLiteral("port"),
                                  QStringLiteral("MQTT broker port."),
                                  QStringLiteral("port"),
                                  QStringLiteral("1883"));
    parser.addOption(portOption);

    QCommandLineOption topicOption(QStringList() << QStringLiteral("t") << QStringLiteral("topic"),
                                   QStringLiteral("Default topic to publish/subscribe."),
                                   QStringLiteral("topic"),
                                   QStringLiteral("test/topic"));
    parser.addOption(topicOption);

    QCommandLineOption userOption(QStringList() << QStringLiteral("u") << QStringLiteral("user"),
                                  QStringLiteral("MQTT username."),
                                  QStringLiteral("username"));
    parser.addOption(userOption);

    QCommandLineOption passOption(QStringList() << QStringLiteral("P") << QStringLiteral("password"),
                                  QStringLiteral("MQTT password."),
                                  QStringLiteral("password"));
    parser.addOption(passOption);

    QCommandLineOption idOption(QStringList() << QStringLiteral("i") << QStringLiteral("id"),
                                QStringLiteral("MQTT Client ID."),
                                QStringLiteral("client_id"),
                                QStringLiteral("OrangePi_Client"));
    parser.addOption(idOption);

    QCommandLineOption heartbeatOption(QStringList() << QStringLiteral("heartbeat"),
                                       QStringLiteral("Heartbeat interval in seconds (0 to disable)."),
                                       QStringLiteral("seconds"),
                                       QStringLiteral("0"));
    parser.addOption(heartbeatOption);

    parser.process(a);

    if (parser.isSet(helpOption)) {
        parser.showHelp();
        return 0;
    }

    MqttConsoleClient::Config config;
    config.host = parser.value(hostOption);
    config.port = parser.value(portOption).toUShort();
    config.topic = parser.value(topicOption);
    config.username = parser.value(userOption);
    config.password = parser.value(passOption);
    config.clientId = parser.value(idOption);
    config.heartbeatInterval = parser.value(heartbeatOption).toInt();

    MqttConsoleClient client(config);
    client.start();

    return a.exec();
}
