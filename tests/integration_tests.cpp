#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <memory>
#include <csignal>
#include <unistd.h>
#include "MqttService.h"
#include "CommandTracker.h"
#include "NodeController.h"
#include "Protocol.h"

class IntegrationTests : public QObject {
    Q_OBJECT
    struct Message { QString topic; QByteArray payload; qint64 elapsed; };
    QTemporaryDir m_dir;
    QProcess m_broker, m_node;
    std::unique_ptr<MqttService> m_pc;
    std::unique_ptr<CommandTracker> m_tracker;
    std::unique_ptr<NodeController> m_controller;
    QList<Message> m_messages;
    Config m_pcCfg, m_piCfg;
    QElapsedTimer m_clock;
    QString m_caseDir;
    bool write(const QString &name, const QByteArray &data)
    {
        QFile f(m_caseDir + "/" + name);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    }
    void startBroker()
    {
        m_broker.start("mosquitto", {"-c", m_caseDir + "/mosquitto.conf"});
    }
    bool brokerReady()
    {
        QTcpSocket socket;
        socket.connectToHost("127.0.0.1", m_pcCfg.port);
        return socket.waitForConnected(30);
    }
    void startNode()
    {
        m_node.start(PI_NODE_PATH, {"--host", "127.0.0.1", "--port", QString::number(m_pcCfg.port),
                                 "--user", "pi_user", "--pass-file", m_caseDir + "/pi.pass"});
    }
    QList<Message> messages(const QString &topic) const
    {
        QList<Message> result;
        for (const auto &m : m_messages) if (m.topic == topic) result << m;
        return result;
    }
    QByteArray latest(const QString &topic) const
    {
        const auto list = messages(topic);
        return list.isEmpty() ? QByteArray() : list.last().payload;
    }
    bool replyReceived(const QString &command, const QString &id) const
    {
        for (const auto &m : messages("lab/pi/reply/" + command)) {
            QJsonObject o;
            if (Protocol::parseObject(m.payload, o) && o.value("request_id").toString() == id) return true;
        }
        return false;
    }
private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_messages.clear(); m_clock.start();
        m_caseDir = m_dir.filePath(QTest::currentTestFunction());
        QVERIFY(QDir().mkpath(m_caseDir));
        QTcpServer port;
        QVERIFY(port.listen(QHostAddress::LocalHost, 0));
        m_pcCfg = Config(); m_pcCfg.host = "127.0.0.1"; m_pcCfg.port = port.serverPort();
        port.close();
        m_pcCfg.nodeId = "pc"; m_pcCfg.user = "pc_user";
        m_pcCfg.password = QUuid::createUuid().toString(QUuid::Id128);
        m_piCfg = m_pcCfg; m_piCfg.nodeId = "pi"; m_piCfg.user = "pi_user";
        m_piCfg.password = QUuid::createUuid().toString(QUuid::Id128);
        QVERIFY(write("pi.pass", m_piCfg.password.toUtf8() + "\n"));
        QVERIFY(write("passwd", "pi_user:" + m_piCfg.password.toUtf8() + "\npc_user:" + m_pcCfg.password.toUtf8() + "\n"));
        QCOMPARE(QProcess::execute("mosquitto_passwd", {"-U", m_caseDir + "/passwd"}), 0);
        QFile acl(ACL_PATH);
        QVERIFY(acl.open(QIODevice::ReadOnly));
        QVERIFY(write("acl", acl.readAll()));
        auto config = QString("listener %1 127.0.0.1\nallow_anonymous false\npassword_file %2/passwd\nacl_file %2/acl\npersistence false\n")
                                .arg(m_pcCfg.port).arg(m_caseDir).toUtf8();
        // CI chạy root trong container: broker thử nghiệm phải đọc được QTemporaryDir.
        if (::geteuid() == 0) config.prepend("user root\n");
        QVERIFY(write("mosquitto.conf", config));
        startBroker();
        QVERIFY(m_broker.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(brokerReady(), 3000);
        m_pc = std::make_unique<MqttService>(m_pcCfg);
        m_tracker = std::make_unique<CommandTracker>(m_pc.get(), m_pcCfg);
        connect(m_pc.get(), &MqttService::messageReceived, this, [this](const QString &topic, const QByteArray &data) {
            m_messages << Message{topic, data, m_clock.elapsed()};
            m_tracker->handleMessage(topic, data);
        });
        QVERIFY(m_pc->subscribe("lab/pi/#", 1));
        m_pc->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_pc->isConnected(), 3000);
        m_controller = std::make_unique<NodeController>();
        m_controller->connectBroker("127.0.0.1", m_pcCfg.port, m_pcCfg.user, m_pcCfg.password, "pi");
        QTRY_VERIFY_WITH_TIMEOUT(m_controller->connected(), 3000);
        // Đợi SUBACK trên kết nối PC trước khi node phát heartbeat đầu tiên.
        QTest::qWait(150);
        startNode();
        QVERIFY(m_node.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->nodeState(), "online", 7000);
    }
    void cleanup()
    {
        if (m_node.state() != QProcess::NotRunning) {
            ::kill(static_cast<pid_t>(m_node.processId()), SIGCONT);
            m_node.terminate();
            if (!m_node.waitForFinished(2000)) { m_node.kill(); m_node.waitForFinished(); }
        }
        m_controller.reset(); m_tracker.reset(); m_pc.reset();
        m_broker.terminate();
        if (!m_broker.waitForFinished(2000)) { m_broker.kill(); m_broker.waitForFinished(); }
    }
    void telemetryHeartbeatAndController()
    {
        QTRY_VERIFY_WITH_TIMEOUT(!latest("lab/pi/sensor/cpu_temp").isEmpty(), 4000);
        QJsonObject data, hb;
        QVERIFY(Protocol::parseObject(latest("lab/pi/sensor/cpu_temp"), data));
        QVERIFY(data.value("valid").isBool());
        QVERIFY(data.value("valid").toBool() ? data.value("value").isDouble() : data.value("value").isNull());
        QVERIFY(Protocol::parseObject(latest("lab/pi/heartbeat"), hb));
        QVERIFY(Protocol::isHeartbeat(hb));
        QTRY_VERIFY(m_controller->dataAgeSec() >= 0 && m_controller->dataAgeSec() <= 3);
    }
    void pingAndSetInterval()
    {
        QSignalSpy finished(m_tracker.get(), &CommandTracker::finished);
        const QString pingId = m_tracker->send("pi", "ping");
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished[0][0].toString(), pingId);
        QCOMPARE(finished[0][2].toString(), "success");
        QVERIFY(finished[0][4].toLongLong() >= 0);
        qInfo() << "Ping RTT (ms):" << finished[0][4].toLongLong();
        m_tracker->send("pi", "set_interval", 500);
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished[1][2].toString(), "success");
        QCOMPARE(finished[1][3].toString(), "500");
        m_messages.clear();
        QTRY_VERIFY_WITH_TIMEOUT(messages("lab/pi/sensor/cpu_temp").size() >= 3, 3000);
        const auto list = messages("lab/pi/sensor/cpu_temp");
        const qint64 interval = list[2].elapsed - list[1].elapsed;
        QVERIFY(interval >= 300 && interval <= 900);
        m_tracker->send("pi", "set_interval", 50);
        QTRY_COMPARE(finished.size(), 3);
        QCOMPARE(finished[2][2].toString(), "error");
    }
    void duplicateCommand()
    {
        const QByteArray first = R"({"request_id":"manual-9","command":"set_interval","value":500})";
        QVERIFY(m_pc->publish("lab/pi/cmd/set_interval", first, 1));
        QTRY_VERIFY(replyReceived("set_interval", "manual-9"));
        const auto ack = latest("lab/pi/reply/set_interval");
        const int count = messages("lab/pi/reply/set_interval").size();
        // Cùng id nhưng value khác vẫn phải trả ACK cũ, không áp dụng lại.
        QVERIFY(m_pc->publish("lab/pi/cmd/set_interval",
            R"({"request_id":"manual-9","command":"set_interval","value":1000})", 1));
        QTRY_VERIFY(messages("lab/pi/reply/set_interval").size() > count);
        QCOMPARE(latest("lab/pi/reply/set_interval"), ack);
    }
    void aclRejectsPcTelemetry()
    {
        m_node.terminate(); QVERIFY(m_node.waitForFinished(2000));
        QTRY_COMPARE(latest("lab/pi/status"), QByteArray("offline"));
        QVERIFY(m_pc->publish("lab/pi/sensor/forbidden", "should-not-arrive", 1));
        QTest::qWait(400);
        QVERIFY(messages("lab/pi/sensor/forbidden").isEmpty());
        // Ping được ACL cho phép: chứng minh kết nối vẫn sống sau tin bị chặn.
        QVERIFY(m_pc->publish("lab/pi/cmd/ping", "allowed", 1));
        QTRY_COMPARE(latest("lab/pi/cmd/ping"), QByteArray("allowed"));
    }
    void gracefulShutdown()
    {
        m_node.terminate(); QVERIFY(m_node.waitForFinished(2000));
        QTRY_COMPARE_WITH_TIMEOUT(latest("lab/pi/status"), QByteArray("offline"), 2000);
        QTRY_COMPARE(m_controller->nodeState(), "offline");
        QCOMPARE(m_node.exitCode(), 0);
    }
    void suddenDeathLastWill()
    {
        m_node.kill(); QVERIFY(m_node.waitForFinished(2000));
        QTRY_COMPARE_WITH_TIMEOUT(latest("lab/pi/status"), QByteArray("offline"), 25000);
        QTRY_COMPARE(m_controller->nodeState(), "offline");
    }
    void offlineCommandRetriesThenTimesOut()
    {
        m_node.terminate(); QVERIFY(m_node.waitForFinished(2000));
        QSignalSpy finished(m_tracker.get(), &CommandTracker::finished);
        m_messages.clear();
        m_tracker->send("pi", "ping");
        QElapsedTimer elapsed; elapsed.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 8000);
        QCOMPARE(finished[0][2].toString(), "timeout");
        QVERIFY(elapsed.elapsed() >= 5900);
        const auto commands = messages("lab/pi/cmd/ping");
        QCOMPARE(commands.size(), 2);
        QCOMPARE(commands[0].payload, commands[1].payload);
    }
    void brokerRestartReconnectsAndResubscribes()
    {
        QSignalSpy states(m_pc.get(), &MqttService::stateChanged);
        m_broker.terminate(); QVERIFY(m_broker.waitForFinished(2000));
        QTRY_VERIFY(!m_pc->isConnected());
        QTRY_COMPARE(m_controller->nodeState(), "unknown");
        QSignalSpy finished(m_tracker.get(), &CommandTracker::finished);
        m_tracker->send("pi", "ping");
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished[0][2].toString(), "not_sent");
        m_messages.clear();
        startBroker(); QVERIFY(m_broker.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(m_pc->isConnected(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->nodeState(), "online", 12000);
        m_tracker->send("pi", "ping");
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished[1][2].toString(), "success");
        QVERIFY(!states.isEmpty());
    }
    void pausedNodeBecomesStale()
    {
        QCOMPARE(::kill(static_cast<pid_t>(m_node.processId()), SIGSTOP), 0);
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->nodeState(), "stale", 18000);
        QCOMPARE(::kill(static_cast<pid_t>(m_node.processId()), SIGCONT), 0);
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->nodeState(), "online", 8000);
    }
    void wrongAckIsIgnored()
    {
        m_node.terminate(); QVERIFY(m_node.waitForFinished(2000));
        MqttService fakePi(m_piCfg);
        fakePi.start(); QTRY_VERIFY(fakePi.isConnected());
        QSignalSpy finished(m_tracker.get(), &CommandTracker::finished);
        const QString id = m_tracker->send("pi", "ping");
        const auto wrong = Protocol::toJson({{"request_id", id}, {"command", "set_interval"}, {"status", "success"}});
        QVERIFY(fakePi.publish("lab/pi/reply/set_interval", wrong, 1));
        QTest::qWait(150);
        QCOMPARE(finished.size(), 0);
        const auto correct = Protocol::toJson({{"request_id", id}, {"command", "ping"}, {"status", "success"}});
        QVERIFY(fakePi.publish("lab/pi/reply/ping", correct, 1));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished[0][2].toString(), "success");
        // ACK muộn/lặp không sinh thêm kết quả.
        QVERIFY(fakePi.publish("lab/pi/reply/ping", correct, 1));
        QTest::qWait(150); QCOMPARE(finished.size(), 1);
    }
    void invalidDataDoesNotRefreshHeartbeat()
    {
        m_node.terminate(); QVERIFY(m_node.waitForFinished(2000));
        QTRY_COMPARE(m_controller->nodeState(), "offline");
        MqttService fakePi(m_piCfg); fakePi.start(); QTRY_VERIFY(fakePi.isConnected());
        // Reset PC để không còn heartbeat cũ.
        m_controller->disconnectBroker();
        m_controller->connectBroker("127.0.0.1", m_pcCfg.port, m_pcCfg.user, m_pcCfg.password, "pi");
        QTRY_COMPARE(m_controller->nodeState(), "stale");
        QVERIFY(fakePi.publish("lab/pi/heartbeat", "not-json"));
        QVERIFY(fakePi.publish("lab/pi/sensor/cpu_temp", R"({"valid":true,"value":"bad"})"));
        QTest::qWait(150);
        QCOMPARE(m_controller->nodeState(), "stale");
        QVERIFY(!m_controller->tempValid());
        QCOMPARE(m_controller->cpuTemp(), "--");
    }
    void invalidCliConfiguration()
    {
        for (const QString &port : {QString("0"), QString("65536"), QString("-1"), QString("abc")}) {
            QProcess process;
            process.start(PI_NODE_PATH, {"--port", port});
            QVERIFY(process.waitForFinished(2000));
            QCOMPARE(process.exitCode(), 2);
        }
    }
};
QTEST_GUILESS_MAIN(IntegrationTests)
#include "integration_tests.moc"
