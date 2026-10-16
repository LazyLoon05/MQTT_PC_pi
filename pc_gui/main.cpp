#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QTimer>
#include "NodeController.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("MQTT PC ↔ Orange Pi");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"smoke-test", "Kiểm tra tải giao diện rồi tự thoát"});
    parser.process(app);
    NodeController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("node", &controller);
    engine.loadFromModule("MqttGui", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    if (parser.isSet("smoke-test")) QTimer::singleShot(1000, &app, &QCoreApplication::quit);
    return app.exec();
}
