#ifndef HEARTBEATMONITOR_H
#define HEARTBEATMONITOR_H

#include <QObject>
#include <QTimer>
#include <QMap>
#include <QDateTime>
#include <QString>
#include <functional>

struct HeartbeatStats {
    int totalClients = 0;
    int activeClients = 0;
    int timedOutClients = 0;
    qint64 avgResponseTimeMs = 0;
    QDateTime lastCheckTime;
};

class HeartbeatMonitor : public QObject {
    Q_OBJECT

public:
    explicit HeartbeatMonitor(QObject* parent = nullptr);

    void start(int checkIntervalMs = 10000);
    void stop();
    bool isRunning() const { return m_checkTimer && m_checkTimer->isActive(); }

    void registerClient(const QString& clientId);
    void unregisterClient(const QString& clientId);
    void updateClientActivity(const QString& clientId);
    bool isClientActive(const QString& clientId) const;
    QDateTime lastActivityTime(const QString& clientId) const;

    void setTimeoutMs(int timeoutMs) { m_timeoutMs = timeoutMs; }
    int timeoutMs() const { return m_timeoutMs; }
    void setEventLoopStallGraceMs(int graceMs) { m_eventLoopStallGraceMs = graceMs; }
    int eventLoopStallGraceMs() const { return m_eventLoopStallGraceMs; }

    HeartbeatStats stats() const;
    void resetStats();

    using TimeoutCallback = std::function<void(const QString& clientId)>;
    void setTimeoutCallback(TimeoutCallback callback) { m_timeoutCallback = std::move(callback); }

signals:
    void clientTimedOut(const QString& clientId);
    void statsUpdated(const HeartbeatStats& stats);

private slots:
    void checkClients();

private:
    QTimer* m_checkTimer;
    QMap<QString, QDateTime> m_clientActivity;
    int m_timeoutMs;
    int m_checkIntervalMs;
    int m_eventLoopStallGraceMs;
    int m_totalChecks;
    int m_totalTimedOut;
    QDateTime m_lastCheckTime;
    TimeoutCallback m_timeoutCallback;
};

#endif // HEARTBEATMONITOR_H
