#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include "CommandTracker.h"
#include "MqttService.h"

class NodeController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionStateChanged)
    Q_PROPERTY(QString nodeState READ nodeState NOTIFY nodeStateChanged)
    Q_PROPERTY(QString cpuTemp READ cpuTemp NOTIFY telemetryChanged)
    Q_PROPERTY(bool tempValid READ tempValid NOTIFY telemetryChanged)
    Q_PROPERTY(int dataAgeSec READ dataAgeSec NOTIFY telemetryChanged)
    Q_PROPERTY(QString lastResult READ lastResult NOTIFY lastResultChanged)
    Q_PROPERTY(QStringList log READ log NOTIFY logChanged)
public:
    explicit NodeController(QObject *parent = nullptr);
    ~NodeController() override;

    QString connectionState() const { return m_connState; }
    bool connected() const { return m_connState == "Connected"; }
    QString nodeState() const { return m_nodeState; }
    QString cpuTemp() const { return m_cpuTemp; }
    bool tempValid() const { return m_tempValid; }
    int dataAgeSec() const;
    QString lastResult() const { return m_lastResult; }
    QStringList log() const { return m_log; }

    Q_INVOKABLE void connectBroker(const QString &host, int port, const QString &user,
                                   const QString &pass, const QString &node);
    Q_INVOKABLE void disconnectBroker();
    Q_INVOKABLE void ping();
    Q_INVOKABLE void setInterval(int ms);

signals:
    void connectionStateChanged();
    void nodeStateChanged();
    void telemetryChanged();
    void lastResultChanged();
    void logChanged();

private slots:
    void onMqttState(MqttService::State s);
    void onMessage(const QString &topic, const QByteArray &payload);
    void onCommandFinished(const QString &id, const QString &command, const QString &status,
                           const QString &detail, qint64 rttMs);
    void onTick();

private:
    void addLog(const QString &s);
    void resetNodeInfo();
    void evaluateNodeState();

    Config m_cfg;
    QString m_node = "pi";
    MqttService *m_mqtt = nullptr;
    CommandTracker *m_tracker = nullptr;
    QTimer m_tick;

    QString m_connState = "Disconnected";
    QString m_nodeState = "unknown";
    QString m_statusPayload;                 // "", "online", "offline"
    bool m_hasHeartbeat = false;
    bool m_hasTelemetry = false;
    QElapsedTimer m_sinceHeartbeat;
    QElapsedTimer m_sinceTelemetry;
    QString m_cpuTemp = "--";
    bool m_tempValid = false;
    QString m_lastResult;
    QStringList m_log;
};
