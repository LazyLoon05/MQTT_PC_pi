#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "Config.h"
#include "Protocol.h"
#include "CommandHandler.h"
#include "Temperature.h"

class UnitTests : public QObject {
    Q_OBJECT
private slots:
    void ids_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<bool>("valid");
        QTest::newRow("pi") << "pi" << true;
        QTest::newRow("orange-pi") << "orange_pi-3b" << true;
        QTest::newRow("32") << QString(32, 'a') << true;
        QTest::newRow("33") << QString(33, 'a') << false;
        QTest::newRow("wildcard") << "pi/#" << false;
        QTest::newRow("plus") << "+" << false;
        QTest::newRow("newline") << "pi\n" << false;
        QTest::newRow("uppercase") << "Pi" << false;
        QTest::newRow("empty") << "" << false;
    }
    void ids() { QFETCH(QString, id); QFETCH(bool, valid); QCOMPARE(Config::isValidId(id), valid); }
    void clientIdsAreUnique()
    {
        Config cfg;
        QVERIFY(cfg.clientId().startsWith("pi-"));
        QVERIFY(cfg.clientId() != cfg.clientId());
    }
    void parsing()
    {
        QJsonObject object;
        QVERIFY(!Protocol::parseObject("[]", object));
        QVERIFY(!Protocol::parseObject("{", object));
        QVERIFY(Protocol::parseObject("{\"value\":null}", object));
        QVERIFY(object.value("value").isNull());
    }
    void intervals_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<bool>("valid");
        QTest::newRow("min") << QJsonValue(200) << true;
        QTest::newRow("max") << QJsonValue(60000) << true;
        QTest::newRow("small") << QJsonValue(50) << false;
        QTest::newRow("large") << QJsonValue(60001) << false;
        QTest::newRow("huge") << QJsonValue(1e100) << false;
        QTest::newRow("fraction") << QJsonValue(500.5) << false;
        QTest::newRow("string") << QJsonValue("500") << false;
        QTest::newRow("null") << QJsonValue(QJsonValue::Null) << false;
        QTest::newRow("bool") << QJsonValue(true) << false;
    }
    void intervals()
    {
        QFETCH(QJsonValue, value); QFETCH(bool, valid);
        CommandHandler handler;
        const auto result = handler.handle("set_interval", Protocol::toJson(
            {{"request_id", "test"}, {"command", "set_interval"}, {"value", value}}));
        QJsonObject ack;
        QVERIFY(Protocol::parseObject(result.ack, ack));
        QCOMPARE(ack.value("status").toString(), valid ? "success" : "error");
        QCOMPARE(result.appliedInterval, valid ? value.toInt() : 0);
    }
    void deduplicationAndEviction()
    {
        CommandHandler handler;
        const auto payload = Protocol::toJson({{"request_id", "same"}, {"command", "set_interval"}, {"value", 500}});
        const auto first = handler.handle("set_interval", payload);
        const auto duplicate = handler.handle("set_interval", payload);
        QCOMPARE(first.appliedInterval, 500);
        QVERIFY(duplicate.duplicate);
        QCOMPARE(duplicate.appliedInterval, 0);
        QCOMPARE(duplicate.ack, first.ack);
        for (int i = 0; i < 64; ++i)
            handler.handle("ping", Protocol::toJson({{"request_id", QString::number(i)}, {"command", "ping"}}));
        QVERIFY(!handler.handle("set_interval", payload).duplicate);
    }
    void invalidCommands()
    {
        CommandHandler handler;
        for (const QByteArray &payload : {QByteArray("[]"), QByteArray("{}"), QByteArray("{"),
                                         QByteArray("{\"request_id\":\"x\",\"command\":\"wrong\"}")}) {
            QJsonObject ack;
            QVERIFY(Protocol::parseObject(handler.handle("ping", payload).ack, ack));
            QCOMPARE(ack.value("status").toString(), "error");
        }
        const auto first = handler.handle("ping", "{\"request_id\":\"used\",\"command\":\"ping\"}");
        QJsonObject ack;
        QVERIFY(Protocol::parseObject(handler.handle("set_interval",
            "{\"request_id\":\"used\",\"command\":\"set_interval\",\"value\":500}").ack, ack));
        QCOMPARE(ack.value("status").toString(), "error");
    }
    void thermalZones()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(!readCpuTemp(dir.path()).valid);
        auto zone = [&](const QString &name, const QByteArray &type, const QByteArray &temp) {
            QDir().mkpath(dir.filePath(name));
            QFile f(dir.filePath(name + "/type"));
            if (!f.open(QIODevice::WriteOnly) || f.write(type) < 0) return false;
            f.close(); f.setFileName(dir.filePath(name + "/temp"));
            return f.open(QIODevice::WriteOnly) && f.write(temp) >= 0;
        };
        QVERIFY(zone("thermal_zone0", "gpu-thermal", "30000"));
        QVERIFY(zone("thermal_zone12", "soc-thermal", "47800"));
        QCOMPARE(readCpuTemp(dir.path()).celsius, 47.8);
        QVERIFY(zone("thermal_zone12", "soc-thermal", "200000"));
        QCOMPARE(readCpuTemp(dir.path()).celsius, 30.0); // Bỏ zone lỗi, tiếp tục quét.
        QVERIFY(zone("thermal_zone0", "gpu-thermal", "nan"));
        QVERIFY(!readCpuTemp(dir.path()).valid);
    }
    void heartbeatValidation()
    {
        QJsonObject hb{{"seq", 1}, {"ts_ms", Protocol::jsonNumber(Protocol::nowMs())}, {"uptime_s", 0}, {"interval_ms", 2000}};
        QVERIFY(Protocol::isHeartbeat(hb));
        hb["seq"] = -1;
        QVERIFY(!Protocol::isHeartbeat(hb));
    }
};
QTEST_GUILESS_MAIN(UnitTests)
#include "unit_tests.moc"
