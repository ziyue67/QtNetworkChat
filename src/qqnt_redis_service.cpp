#include "qqnt_redis_service.h"

#include <QDebug>

QQNTRedisService::QQNTRedisService(QObject* parent)
    : QObject(parent)
    , m_client(new RedisClient(this))
    , m_subscriber(new RedisSubscriber(this)) {
    connect(m_subscriber, &RedisSubscriber::messageReceived, this, &QQNTRedisService::messageReceived);
    connect(m_subscriber, &RedisSubscriber::subscriptionStateChanged, this, [this](bool subscribed) {
        setSubscriberAvailability(subscribed, subscribed ? QString() : m_subscriber->lastError());
        emit subscriptionStateChanged(subscribed);
    });
}

void QQNTRedisService::configureFromEnvironment() {
    m_client->configureFromEnvironment();
    m_subscriber->configureFromEnvironment();
    m_commandReady = false;
    m_subscriberReady = false;
    m_lastError.clear();
    updateReadinessReason();
}

bool QQNTRedisService::initialize(QString* error) {
    configureFromEnvironment();
    // Presence and cross-instance routing require Redis; starting without it would
    // accept clients while silently losing delivery across server instances.
    if (!m_client->isEnabled()) {
        m_lastError = QStringLiteral("redis-required");
        m_commandReady = false;
        m_subscriberReady = false;
        m_readinessReason = m_lastError;
        if (error) *error = m_lastError;
        emit readinessChanged(false, m_readinessReason);
        return false;
    }

    if (!m_client->connectToServer()) {
        m_lastError = m_client->lastError().trimmed().isEmpty()
            ? QStringLiteral("redis-connect-failed")
            : m_client->lastError().trimmed();
        m_commandReady = false;
        m_subscriberReady = false;
        m_readinessReason = QStringLiteral("redis-connect-failed");
        if (error) *error = m_lastError;
        emit readinessChanged(false, m_readinessReason);
        return false;
    }
    m_commandReady = true;

    if (!m_subscriber->subscribe(QStringLiteral("messages"))) {
        m_lastError = m_subscriber->lastError().trimmed().isEmpty()
            ? QStringLiteral("redis-subscribe-failed")
            : m_subscriber->lastError().trimmed();
        m_subscriberReady = false;
        m_readinessReason = QStringLiteral("redis-subscribe-failed");
        if (error) *error = m_lastError;
        emit readinessChanged(false, m_readinessReason);
        return false;
    }

    m_subscriberReady = true;
    m_lastError.clear();
    updateReadinessReason();
    if (error) error->clear();
    emit readinessChanged(true, QString());
    return true;
}

void QQNTRedisService::shutdown() {
    const bool previousReady = isReady();
    const QString previousReason = m_readinessReason;
    m_commandReady = false;
    m_subscriberReady = false;
    m_readinessReason = QStringLiteral("server-stopped");
    m_lastError = m_readinessReason;
    m_subscriber->disconnectFromServer();
    emitReadinessIfChanged(previousReady, previousReason);
}

bool QQNTRedisService::isEnabled() const {
    return m_client && m_client->isEnabled();
}

void QQNTRedisService::setCommandAvailability(bool available, const QString& reason) {
    const bool previousReady = isReady();
    const QString previousReason = m_readinessReason;
    m_commandReady = available;
    if (!available) {
        m_lastError = reason.trimmed().isEmpty()
            ? QStringLiteral("redis-command-unavailable")
            : reason.trimmed();
    } else if (m_subscriberReady) {
        m_lastError.clear();
    }
    updateReadinessReason();
    emitReadinessIfChanged(previousReady, previousReason);
}

void QQNTRedisService::setSubscriberAvailability(bool available, const QString& reason) {
    const bool previousReady = isReady();
    const QString previousReason = m_readinessReason;
    m_subscriberReady = available;
    if (!available) {
        m_lastError = reason.trimmed().isEmpty()
            ? QStringLiteral("redis-subscriber-unavailable")
            : reason.trimmed();
    } else if (m_commandReady) {
        m_lastError.clear();
    }
    updateReadinessReason();
    emitReadinessIfChanged(previousReady, previousReason);
}

bool QQNTRedisService::recoverCommandAvailability() {
    if (m_commandReady || !isEnabled()) {
        return m_commandReady;
    }

    if (m_client->ping() || m_client->connectToServer()) {
        setCommandAvailability(true);
        return true;
    }

    setCommandAvailability(false, m_client->lastError());
    return false;
}

bool QQNTRedisService::recoverSubscriberAvailability() {
    if (m_subscriberReady || !m_subscriber || !m_subscriber->isEnabled()) {
        return m_subscriberReady;
    }

    if (m_subscriber->subscribe(QStringLiteral("messages"))) {
        setSubscriberAvailability(true);
        return true;
    }

    setSubscriberAvailability(false, m_subscriber->lastError());
    return false;
}

bool QQNTRedisService::setPresence(const QString& userId, const QString& userName, int ttlSeconds, int timeoutMs) {
    if (!isEnabled()) return true;
    const bool ok = m_client->setPresence(userId, userName, ttlSeconds, timeoutMs);
    if (!ok) setCommandAvailability(false, m_client->lastError());
    return ok;
}

bool QQNTRedisService::clearPresence(const QString& userId, int timeoutMs) {
    if (!isEnabled()) return true;
    const bool ok = m_client->clearPresence(userId, timeoutMs);
    if (!ok) setCommandAvailability(false, m_client->lastError());
    return ok;
}

bool QQNTRedisService::queryPresence(const QString& userId, bool* online, int timeoutMs) {
    if (online) *online = false;
    if (!isEnabled()) return false;
    const bool ok = m_client->queryPresence(userId, online, timeoutMs);
    if (!ok) setCommandAvailability(false, m_client->lastError());
    return ok;
}

bool QQNTRedisService::fetchOnlinePresence(QList<RedisClient::Presence>* users, int timeoutMs) {
    if (users) users->clear();
    if (!isEnabled()) return true;
    const bool ok = m_client->fetchOnlinePresence(users, timeoutMs);
    if (!ok) setCommandAvailability(false, m_client->lastError());
    return ok;
}

bool QQNTRedisService::publish(const QString& channel, const QByteArray& payload, int timeoutMs) {
    if (!isEnabled()) return true;
    const bool ok = m_client->publish(channel, payload, timeoutMs);
    if (!ok) setCommandAvailability(false, m_client->lastError());
    return ok;
}

void QQNTRedisService::updateReadinessReason() {
    if (isReady()) {
        m_readinessReason.clear();
    } else if (!m_commandReady) {
        m_readinessReason = m_lastError.trimmed().isEmpty()
            ? QStringLiteral("redis-command-unavailable")
            : m_lastError.trimmed();
    } else if (!m_subscriberReady) {
        m_readinessReason = m_lastError.trimmed().isEmpty()
            ? QStringLiteral("redis-subscriber-unavailable")
            : m_lastError.trimmed();
    }
}

void QQNTRedisService::emitReadinessIfChanged(bool previousReady, const QString& previousReason) {
    if (previousReady != isReady() || previousReason != m_readinessReason) {
        emit readinessChanged(isReady(), m_readinessReason);
    }
}
