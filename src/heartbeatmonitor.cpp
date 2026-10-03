#include "heartbeatmonitor.h"
#include <QDebug>
#include <QDateTime>

HeartbeatMonitor::HeartbeatMonitor(QObject* parent)
    : QObject(parent)
    , m_checkTimer(nullptr)
    , m_timeoutMs(60000) // Default 60 seconds timeout
    , m_checkIntervalMs(10000)
    , m_eventLoopStallGraceMs(0)
    , m_totalChecks(0)
    , m_totalTimedOut(0)
{
}

void HeartbeatMonitor::start(int checkIntervalMs) {
    if (m_checkTimer) {
        m_checkTimer->stop();
        delete m_checkTimer;
    }

    m_checkTimer = new QTimer(this);
    connect(m_checkTimer, &QTimer::timeout, this, &HeartbeatMonitor::checkClients);
    m_checkIntervalMs = checkIntervalMs;
    m_lastCheckTime = QDateTime::currentDateTime();
    m_checkTimer->start(checkIntervalMs);

    qDebug() << "HeartbeatMonitor started with interval:" << checkIntervalMs << "ms, timeout:" << m_timeoutMs << "ms";
}

void HeartbeatMonitor::stop() {
    if (m_checkTimer) {
        m_checkTimer->stop();
        delete m_checkTimer;
        m_checkTimer = nullptr;
    }
    qDebug() << "HeartbeatMonitor stopped";
}

void HeartbeatMonitor::registerClient(const QString& clientId) {
    m_clientActivity[clientId] = QDateTime::currentDateTime();
    qDebug() << "Client registered for heartbeat monitoring:" << clientId;
}

void HeartbeatMonitor::unregisterClient(const QString& clientId) {
    m_clientActivity.remove(clientId);
    qDebug() << "Client unregistered from heartbeat monitoring:" << clientId;
}

void HeartbeatMonitor::updateClientActivity(const QString& clientId) {
    if (m_clientActivity.contains(clientId)) {
        m_clientActivity[clientId] = QDateTime::currentDateTime();
    }
}

bool HeartbeatMonitor::isClientActive(const QString& clientId) const {
    if (!m_clientActivity.contains(clientId)) {
        return false;
    }

    qint64 elapsed = m_clientActivity[clientId].msecsTo(QDateTime::currentDateTime());
    return elapsed < m_timeoutMs;
}

QDateTime HeartbeatMonitor::lastActivityTime(const QString& clientId) const {
    return m_clientActivity.value(clientId, QDateTime());
}

HeartbeatStats HeartbeatMonitor::stats() const {
    HeartbeatStats stats;
    stats.totalClients = m_clientActivity.size();
    stats.lastCheckTime = QDateTime::currentDateTime();

    qint64 totalResponseTime = 0;
    int activeCount = 0;

    for (auto it = m_clientActivity.begin(); it != m_clientActivity.end(); ++it) {
        qint64 elapsed = it.value().msecsTo(QDateTime::currentDateTime());
        if (elapsed < m_timeoutMs) {
            activeCount++;
            totalResponseTime += elapsed;
        }
    }

    stats.activeClients = activeCount;
    stats.timedOutClients = stats.totalClients - activeCount;
    stats.avgResponseTimeMs = activeCount > 0 ? totalResponseTime / activeCount : 0;

    return stats;
}

void HeartbeatMonitor::resetStats() {
    m_totalChecks = 0;
    m_totalTimedOut = 0;
}

void HeartbeatMonitor::checkClients() {
    m_totalChecks++;
    QDateTime now = QDateTime::currentDateTime();
    const qint64 elapsedSinceLastCheck =
        m_lastCheckTime.isValid() ? m_lastCheckTime.msecsTo(now) : 0;
    m_lastCheckTime = now;
    if (m_eventLoopStallGraceMs > 0
        && elapsedSinceLastCheck > m_checkIntervalMs + m_eventLoopStallGraceMs) {
        for (auto it = m_clientActivity.begin(); it != m_clientActivity.end(); ++it) {
            it.value() = now;
        }
        HeartbeatStats currentStats = stats();
        currentStats.lastCheckTime = now;
        emit statsUpdated(currentStats);
        qDebug() << "Heartbeat check skipped after event-loop stall:"
                 << elapsedSinceLastCheck << "ms";
        return;
    }
    QStringList timedOutClients;

    for (auto it = m_clientActivity.begin(); it != m_clientActivity.end(); ++it) {
        qint64 elapsed = it.value().msecsTo(now);
        if (elapsed >= m_timeoutMs) {
            timedOutClients.append(it.key());
        }
    }

    for (const QString& clientId : timedOutClients) {
        m_totalTimedOut++;
        qWarning() << "Client heartbeat timed out:" << clientId
                    << "last active:" << m_clientActivity[clientId].toString()
                    << "elapsed:" << m_clientActivity[clientId].msecsTo(now) << "ms";

        emit clientTimedOut(clientId);

        if (m_timeoutCallback) {
            m_timeoutCallback(clientId);
        }

        m_clientActivity.remove(clientId);
    }

    HeartbeatStats currentStats = stats();
    emit statsUpdated(currentStats);

    if (!timedOutClients.isEmpty()) {
        qDebug() << "Heartbeat check #" << m_totalChecks
                 << "- Total:" << currentStats.totalClients
                 << "Active:" << currentStats.activeClients
                 << "Timed out:" << timedOutClients.size()
                 << "Cumulative timeouts:" << m_totalTimedOut;
    }
}
