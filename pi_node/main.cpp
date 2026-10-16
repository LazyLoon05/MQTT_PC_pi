#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>
#include <QDebug>
#include <csignal>
#include "MqttService.h"
#include "Protocol.h"
#include "CommandHandler.h"
#include "Temperature.h"

static volatile std::sig_atomic_t g_stop = 0;
static void onSignal(int) { g_stop = 1; }

static QString readPassword(const QCommandLineParser &p)
{
    if (p.isSet("pass-file")) {
        QFile f(p.value("pass-file"));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        QByteArray line = f.readLine();
        if (line.endsWith('\n')) line.chop(1);
        if (line.endsWith('\r')) line.chop(1);
        return QString::fromUtf8(line);
    }
    return qEnvironmentVariable("MQTT_PASSWORD");
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qSetMessagePattern("%{time yyyy-MM-dd hh:mm:ss.zzz} %{type} %{message}");

    QCommandLineParser p;
    p.addHelpOption();
    p.addOption({"host", "Địa chỉ broker", "host", "localhost"});
    p.addOption({"port", "Cổng broker", "port", "1883"});
    p.addOption({"user", "MQTT user (hoặc biến MQTT_USER)", "user"});
    p.addOption({"pass-file", "File chứa mật khẩu (hoặc biến MQTT_PASSWORD)", "file"});
    p.addOption({"id", "Node id (a-z 0-9 _ -)", "id", "pi"});
    p.process(app);

    Config cfg;
    cfg.nodeId = p.value("id");
    cfg.host = p.value("host");
    bool portOk = false;
    const uint port = p.value("port").toUInt(&portOk);
    if (!portOk || port < 1 || port > 65535 || cfg.host.trimmed().isEmpty()) {
        qCritical() << "Host hoặc cổng không hợp lệ (1..65535)";
        return 2;
    }
    cfg.port = static_cast<quint16>(port);
    cfg.user = p.isSet("user") ? p.value("user") : qEnvironmentVariable("MQTT_USER");
    cfg.password = readPassword(p);

    if (!Config::isValidId(cfg.nodeId)) {
        qCritical() << "Node id không hợp lệ";
        return 2;
    }
    if (cfg.user.isEmpty() || cfg.password.isEmpty()) {
        qCritical() << "Thiếu user hoặc mật khẩu (--user, --pass-file hoặc biến môi trường)";
        return 2;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    MqttService mqtt(cfg);
    QObject::connect(&mqtt, &MqttService::logMessage,
                     [&](const QString &s) { qInfo().noquote() << s; });
    QObject::connect(&mqtt, &MqttService::publishFailed,
                     [&](const QString &t, const QString &why) {
        qWarning().noquote() << "Gửi thất bại" << t << ":" << why;
    });

    const QString cmdPrefix   = cfg.nodeTopic(cfg.nodeId, "cmd/");
    const QString replyPrefix = cfg.nodeTopic(cfg.nodeId, "reply/");
    mqtt.subscribe(cmdPrefix + "#", 1);

    QElapsedTimer uptime;
    uptime.start();
    qint64 sensorSeq = 0, hbSeq = 0;

    // --- Telemetry ---
    QTimer sensorTimer;
    sensorTimer.setInterval(2000);
    QObject::connect(&sensorTimer, &QTimer::timeout, [&] {
        if (!mqtt.isConnected()) return;
        const TempReading t = readCpuTemp();
        QJsonObject o{{"seq", Protocol::jsonNumber(++sensorSeq)},
                      {"ts_ms", Protocol::jsonNumber(Protocol::nowMs())},
                      {"value", t.valid ? QJsonValue(qRound(t.celsius * 10) / 10.0)
                                        : QJsonValue(QJsonValue::Null)},
                      {"valid", t.valid}};
        if (!t.valid) o["error"] = t.error;
        mqtt.publish(cfg.nodeTopic(cfg.nodeId, "sensor/cpu_temp"), Protocol::toJson(o), 0);
    });
    sensorTimer.start();

    // --- Heartbeat ---
    auto sendHeartbeat = [&] {
        if (!mqtt.isConnected()) return;
        QJsonObject o{{"seq", Protocol::jsonNumber(++hbSeq)},
                      {"ts_ms", Protocol::jsonNumber(Protocol::nowMs())},
                      {"uptime_s", Protocol::jsonNumber(uptime.elapsed() / 1000)},
                      {"interval_ms", sensorTimer.interval()}};
        mqtt.publish(cfg.nodeTopic(cfg.nodeId, "heartbeat"), Protocol::toJson(o), 0);
    };
    QTimer hbTimer;
    hbTimer.setInterval(5000);
    QObject::connect(&hbTimer, &QTimer::timeout, sendHeartbeat);
    hbTimer.start();
    QObject::connect(&mqtt, &MqttService::stateChanged, [&](MqttService::State s) {
        if (s == MqttService::State::Connected) sendHeartbeat();   // báo ngay khi kết nối
    });

    // --- Xử lý lệnh, chống xử lý lặp theo request_id ---
    CommandHandler handler;
    QObject::connect(&mqtt, &MqttService::messageReceived,
                     [&](const QString &topic, const QByteArray &payload) {
        if (!topic.startsWith(cmdPrefix)) return;
        const QString command = topic.mid(cmdPrefix.size());
        // Không tạo topic reply chứa một phân cấp ngoài giao thức.
        if (!Config::isValidId(command)) return;
        const auto result = handler.handle(command, payload);
        if (result.appliedInterval > 0) sensorTimer.setInterval(result.appliedInterval);
        qInfo().noquote() << (result.duplicate ? "Lệnh trùng" : "Lệnh") << command;
        mqtt.publish(replyPrefix + command, result.ack, 1);
    });

    // --- Thoát êm khi nhận SIGINT/SIGTERM ---
    QTimer sigTimer;
    sigTimer.setInterval(200);
    QObject::connect(&sigTimer, &QTimer::timeout, [&] {
        if (!g_stop) return;
        sigTimer.stop();
        sensorTimer.stop();
        hbTimer.stop();
        mqtt.stop();                                         // báo offline rồi ngắt
        QTimer::singleShot(750, &app, &QCoreApplication::quit);   // chờ gói tin kịp gửi
    });
    sigTimer.start();

    mqtt.start();
    return app.exec();
}
