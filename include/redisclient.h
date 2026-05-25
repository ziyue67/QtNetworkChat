#ifndef REDISCLIENT_H
#define REDISCLIENT_H

#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QTcpSocket>

class RedisClient : public QObject {
public:
    enum class ReplyType {
        SimpleString,
        Error,
        Integer,
        BulkString,
        Array,
        Unknown
    };

    struct Reply {
        ReplyType type = ReplyType::Unknown;
        QByteArray value;
        QList<Reply> elements;
        qint64 integer = 0;
        bool isNull = false;
        QString error;

        bool isError() const { return type == ReplyType::Error; }
        bool isSimpleString(const QByteArray& expected) const {
            return type == ReplyType::SimpleString && value == expected;
        }
    };

    struct Presence {
        QString userId;
        QString userName;
        QDateTime lastSeen;
    };

    struct PubSubMessage {
        QString channel;
        QByteArray payload;
    };

    explicit RedisClient(QObject* parent = nullptr);

    void configureFromEnvironment();
    static bool isEnabledFromEnvironment();
    bool isEnabled() const { return m_enabled; }
    bool isReady() const { return m_socket.state() == QAbstractSocket::ConnectedState; }
    QString lastError() const { return m_lastError; }

    bool connectToServer(int timeoutMs = 500);
    bool ping(int timeoutMs = 500);
    bool setPresence(const QString& userId, const QString& userName, int ttlSeconds = 90, int timeoutMs = 200);
    bool clearPresence(const QString& userId, int timeoutMs = 200);
    bool fetchOnlinePresence(QList<Presence>* users, int timeoutMs = 300);
    bool publish(const QString& channel, const QByteArray& payload, int timeoutMs = 200);

    static QByteArray encodeCommand(const QList<QByteArray>& arguments);
    static bool parseReply(const QByteArray& data, Reply* reply, int* bytesConsumed = nullptr, QString* errorMessage = nullptr);
    static bool parsePubSubMessage(const Reply& reply, PubSubMessage* message);

private:
    bool ensureConnected(int timeoutMs);
    bool sendCommand(const QList<QByteArray>& arguments, Reply* reply, int timeoutMs);
    QByteArray pubSubChannel(const QString& channel) const;
    QByteArray presenceUsersKey() const;
    QByteArray presenceKey(const QString& userId) const;
    QByteArray presenceValue(const QString& userId, const QString& userName) const;

    QTcpSocket m_socket;
    bool m_enabled = false;
    QString m_host = "127.0.0.1";
    quint16 m_port = 6379;
    QString m_password;
    QString m_prefix = "qtchat";
    QString m_lastError;
    qint64 m_lastConnectAttemptMs = 0;
};

#endif // REDISCLIENT_H
