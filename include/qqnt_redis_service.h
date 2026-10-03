#ifndef QQNT_REDIS_SERVICE_H
#define QQNT_REDIS_SERVICE_H

#include "redisclient.h"

#include <QObject>
#include <QString>

class QQNTRedisService : public QObject {
    Q_OBJECT

public:
    explicit QQNTRedisService(QObject* parent = nullptr);

    void configureFromEnvironment();
    bool initialize(QString* error = nullptr);
    void shutdown();

    bool isEnabled() const;
    bool isCommandReady() const { return m_commandReady; }
    bool isSubscriberReady() const { return m_subscriberReady; }
    bool isReady() const { return m_commandReady && m_subscriberReady; }
    QString readinessReason() const { return m_readinessReason; }
    QString lastError() const { return m_lastError; }

    void setCommandAvailability(bool available, const QString& reason = QString());
    void setSubscriberAvailability(bool available, const QString& reason = QString());
    bool recoverCommandAvailability();
    bool recoverSubscriberAvailability();

    bool setPresence(const QString& userId, const QString& userName, int ttlSeconds = 90, int timeoutMs = 200);
    bool clearPresence(const QString& userId, int timeoutMs = 200);
    bool queryPresence(const QString& userId, bool* online, int timeoutMs = 200);
    bool fetchOnlinePresence(QList<RedisClient::Presence>* users, int timeoutMs = 300);
    bool publish(const QString& channel, const QByteArray& payload, int timeoutMs = 200);

signals:
    void messageReceived(const RedisClient::PubSubMessage& message);
    void readinessChanged(bool ready, const QString& reason);
    void subscriptionStateChanged(bool subscribed);

private:
    void updateReadinessReason();
    void emitReadinessIfChanged(bool previousReady, const QString& previousReason);

    RedisClient* m_client;
    RedisSubscriber* m_subscriber;
    bool m_commandReady = false;
    bool m_subscriberReady = false;
    QString m_readinessReason;
    QString m_lastError;
};

#endif // QQNT_REDIS_SERVICE_H
