#include "redisclient.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

namespace {
constexpr qint64 kReconnectBackoffMs = 5000;

bool envEnabled(const char* name) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

qint64 nowMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

int findLineEnd(const QByteArray& data, int offset) {
    return data.indexOf("\r\n", offset);
}

bool parseIntegerLine(const QByteArray& data, int offset, qint64* value, int* nextOffset, QString* errorMessage) {
    const int lineEnd = findLineEnd(data, offset);
    if (lineEnd < 0) {
        if (errorMessage) *errorMessage = "incomplete";
        return false;
    }

    bool ok = false;
    const qint64 parsed = data.mid(offset, lineEnd - offset).toLongLong(&ok);
    if (!ok) {
        if (errorMessage) *errorMessage = "invalid integer reply";
        return false;
    }

    if (value) *value = parsed;
    if (nextOffset) *nextOffset = lineEnd + 2;
    return true;
}

bool parseReplyAt(const QByteArray& data, int* offset, RedisClient::Reply* reply, QString* errorMessage) {
    if (!offset || !reply || *offset >= data.size()) {
        if (errorMessage) *errorMessage = "incomplete";
        return false;
    }

    const char prefix = data.at(*offset);
    ++(*offset);

    if (prefix == '+' || prefix == '-') {
        const int lineEnd = findLineEnd(data, *offset);
        if (lineEnd < 0) {
            if (errorMessage) *errorMessage = "incomplete";
            return false;
        }

        reply->value = data.mid(*offset, lineEnd - *offset);
        reply->type = prefix == '+' ? RedisClient::ReplyType::SimpleString : RedisClient::ReplyType::Error;
        if (reply->type == RedisClient::ReplyType::Error) {
            reply->error = QString::fromUtf8(reply->value);
        }
        *offset = lineEnd + 2;
        return true;
    }

    if (prefix == ':') {
        qint64 integer = 0;
        int nextOffset = 0;
        if (!parseIntegerLine(data, *offset, &integer, &nextOffset, errorMessage)) return false;
        reply->type = RedisClient::ReplyType::Integer;
        reply->integer = integer;
        *offset = nextOffset;
        return true;
    }

    if (prefix == '$') {
        qint64 length = 0;
        int nextOffset = 0;
        if (!parseIntegerLine(data, *offset, &length, &nextOffset, errorMessage)) return false;
        if (length < 0) {
            reply->type = RedisClient::ReplyType::BulkString;
            reply->isNull = true;
            reply->value.clear();
            *offset = nextOffset;
            return true;
        }
        if (length > data.size() || nextOffset + length + 2 > data.size()) {
            if (errorMessage) *errorMessage = "incomplete";
            return false;
        }
        if (data.mid(nextOffset + static_cast<int>(length), 2) != "\r\n") {
            if (errorMessage) *errorMessage = "invalid bulk string terminator";
            return false;
        }

        reply->type = RedisClient::ReplyType::BulkString;
        reply->value = data.mid(nextOffset, static_cast<int>(length));
        *offset = nextOffset + static_cast<int>(length) + 2;
        return true;
    }

    if (prefix == '*') {
        qint64 count = 0;
        int nextOffset = 0;
        if (!parseIntegerLine(data, *offset, &count, &nextOffset, errorMessage)) return false;
        if (count < 0) {
            reply->type = RedisClient::ReplyType::Array;
            reply->elements.clear();
            *offset = nextOffset;
            return true;
        }

        QList<RedisClient::Reply> elements;
        int cursor = nextOffset;
        for (qint64 i = 0; i < count; ++i) {
            RedisClient::Reply element;
            if (!parseReplyAt(data, &cursor, &element, errorMessage)) return false;
            elements.append(element);
        }

        reply->type = RedisClient::ReplyType::Array;
        reply->elements = elements;
        *offset = cursor;
        return true;
    }

    if (errorMessage) *errorMessage = "unsupported reply type";
    return false;
}
}

RedisClient::RedisClient(QObject* parent)
    : QObject(parent) {
}

bool RedisClient::isEnabledFromEnvironment() {
    return envEnabled("QTNETWORKCHAT_REDIS");
}

void RedisClient::configureFromEnvironment() {
    m_enabled = isEnabledFromEnvironment();
    m_host = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_REDIS_HOST")).trimmed();
    if (m_host.isEmpty()) m_host = "127.0.0.1";

    bool portOk = false;
    const int configuredPort = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_REDIS_PORT")).trimmed().toInt(&portOk);
    m_port = portOk && configuredPort > 0 && configuredPort <= 65535 ? static_cast<quint16>(configuredPort) : 6379;

    m_password = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_REDIS_PASSWORD"));
    m_prefix = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_REDIS_PREFIX")).trimmed();
    if (m_prefix.isEmpty()) m_prefix = "qtchat";
    while (m_prefix.endsWith(':')) {
        m_prefix.chop(1);
    }
    if (m_prefix.isEmpty()) m_prefix = "qtchat";

    m_lastError.clear();
}

bool RedisClient::connectToServer(int timeoutMs) {
    if (!m_enabled) return false;

    m_lastConnectAttemptMs = nowMs();
    m_socket.abort();
    m_socket.connectToHost(m_host, m_port);
    if (!m_socket.waitForConnected(timeoutMs)) {
        m_lastError = m_socket.errorString();
        return false;
    }

    if (!m_password.isEmpty()) {
        Reply authReply;
        if (!sendCommand({QByteArrayLiteral("AUTH"), m_password.toUtf8()}, &authReply, timeoutMs)
            || !authReply.isSimpleString(QByteArrayLiteral("OK"))) {
            if (m_lastError.isEmpty()) m_lastError = "Redis AUTH failed";
            m_socket.abort();
            return false;
        }
    }

    return ping(timeoutMs);
}

bool RedisClient::ping(int timeoutMs) {
    if (!m_enabled) return true;

    Reply reply;
    return sendCommand({QByteArrayLiteral("PING")}, &reply, timeoutMs)
        && reply.isSimpleString(QByteArrayLiteral("PONG"));
}

bool RedisClient::setPresence(const QString& userId, const QString& userName, int ttlSeconds, int timeoutMs) {
    if (!m_enabled || userId.isEmpty()) return true;

    Reply reply;
    const int boundedTtl = qMax(ttlSeconds, 10);
    const bool saved = sendCommand({
            QByteArrayLiteral("SET"),
            presenceKey(userId),
            presenceValue(userId, userName),
            QByteArrayLiteral("EX"),
            QByteArray::number(boundedTtl)
        },
        &reply,
        timeoutMs)
        && reply.isSimpleString(QByteArrayLiteral("OK"));
    if (!saved) return false;

    Reply indexReply;
    return sendCommand({
            QByteArrayLiteral("SADD"),
            presenceUsersKey(),
            userId.toUtf8()
        },
        &indexReply,
        timeoutMs)
        && indexReply.type == ReplyType::Integer;
}

bool RedisClient::clearPresence(const QString& userId, int timeoutMs) {
    if (!m_enabled || userId.isEmpty()) return true;

    Reply reply;
    const bool deleted = sendCommand({QByteArrayLiteral("DEL"), presenceKey(userId)}, &reply, timeoutMs)
        && reply.type == ReplyType::Integer;
    if (!deleted) return false;

    Reply indexReply;
    return sendCommand({
            QByteArrayLiteral("SREM"),
            presenceUsersKey(),
            userId.toUtf8()
        },
        &indexReply,
        timeoutMs)
        && indexReply.type == ReplyType::Integer;
}

bool RedisClient::fetchOnlinePresence(QList<Presence>* users, int timeoutMs) {
    if (users) users->clear();
    if (!m_enabled) return true;
    if (!users) return false;

    Reply membersReply;
    if (!sendCommand({QByteArrayLiteral("SMEMBERS"), presenceUsersKey()}, &membersReply, timeoutMs)
        || membersReply.type != ReplyType::Array) {
        return false;
    }

    QList<QByteArray> userIds;
    for (const Reply& member : membersReply.elements) {
        if (member.type == ReplyType::BulkString && !member.isNull && !member.value.isEmpty()) {
            userIds.append(member.value);
        }
    }
    if (userIds.isEmpty()) return true;

    QList<QByteArray> mgetArgs;
    mgetArgs.append(QByteArrayLiteral("MGET"));
    for (const QByteArray& userId : userIds) {
        mgetArgs.append(presenceKey(QString::fromUtf8(userId)));
    }

    Reply valuesReply;
    if (!sendCommand(mgetArgs, &valuesReply, timeoutMs) || valuesReply.type != ReplyType::Array) {
        return false;
    }

    QList<QByteArray> staleUserIds;
    const int count = qMin(userIds.size(), valuesReply.elements.size());
    for (int i = 0; i < count; ++i) {
        const Reply& value = valuesReply.elements.at(i);
        if (value.type != ReplyType::BulkString || value.isNull || value.value.isEmpty()) {
            staleUserIds.append(userIds.at(i));
            continue;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(value.value);
        if (!doc.isObject()) {
            staleUserIds.append(userIds.at(i));
            continue;
        }

        const QJsonObject obj = doc.object();
        Presence presence;
        presence.userId = obj["userId"].toString(QString::fromUtf8(userIds.at(i))).trimmed();
        presence.userName = obj["userName"].toString(presence.userId).trimmed();
        presence.lastSeen = QDateTime::fromString(obj["lastSeen"].toString(), Qt::ISODate);
        if (!presence.userId.isEmpty()) {
            if (presence.userName.isEmpty()) presence.userName = presence.userId;
            users->append(presence);
        }
    }

    if (!staleUserIds.isEmpty()) {
        QList<QByteArray> cleanupArgs;
        cleanupArgs.append(QByteArrayLiteral("SREM"));
        cleanupArgs.append(presenceUsersKey());
        cleanupArgs.append(staleUserIds);
        Reply cleanupReply;
        sendCommand(cleanupArgs, &cleanupReply, timeoutMs);
    }

    return true;
}

bool RedisClient::publish(const QString& channel, const QByteArray& payload, int timeoutMs) {
    if (!m_enabled) return true;
    if (channel.trimmed().isEmpty()) return false;

    Reply reply;
    return sendCommand({
            QByteArrayLiteral("PUBLISH"),
            pubSubChannel(channel),
            payload
        },
        &reply,
        timeoutMs)
        && reply.type == ReplyType::Integer;
}

QByteArray RedisClient::encodeCommand(const QList<QByteArray>& arguments) {
    QByteArray command;
    command.append('*');
    command.append(QByteArray::number(arguments.size()));
    command.append("\r\n");
    for (const QByteArray& argument : arguments) {
        command.append('$');
        command.append(QByteArray::number(argument.size()));
        command.append("\r\n");
        command.append(argument);
        command.append("\r\n");
    }
    return command;
}

bool RedisClient::parseReply(const QByteArray& data, Reply* reply, int* bytesConsumed, QString* errorMessage) {
    int offset = 0;
    Reply parsed;
    QString localError;
    if (!parseReplyAt(data, &offset, &parsed, &localError)) {
        if (errorMessage) *errorMessage = localError;
        return false;
    }

    if (reply) *reply = parsed;
    if (bytesConsumed) *bytesConsumed = offset;
    if (errorMessage) errorMessage->clear();
    return true;
}

bool RedisClient::parsePubSubMessage(const Reply& reply, PubSubMessage* message) {
    if (!message
        || reply.type != ReplyType::Array
        || reply.elements.size() != 3) {
        return false;
    }

    const Reply& kind = reply.elements.at(0);
    const Reply& channel = reply.elements.at(1);
    const Reply& payload = reply.elements.at(2);
    if (kind.type != ReplyType::BulkString
        || channel.type != ReplyType::BulkString
        || payload.type != ReplyType::BulkString
        || kind.isNull
        || channel.isNull
        || payload.isNull
        || kind.value.toLower() != QByteArrayLiteral("message")) {
        return false;
    }

    message->channel = QString::fromUtf8(channel.value);
    message->payload = payload.value;
    return !message->channel.isEmpty();
}

bool RedisClient::ensureConnected(int timeoutMs) {
    if (!m_enabled) return false;
    if (m_socket.state() == QAbstractSocket::ConnectedState) return true;

    const qint64 elapsedSinceLastAttempt = nowMs() - m_lastConnectAttemptMs;
    if (m_lastConnectAttemptMs > 0 && elapsedSinceLastAttempt < kReconnectBackoffMs) {
        if (m_lastError.isEmpty()) m_lastError = "Redis reconnect backoff";
        return false;
    }

    return connectToServer(timeoutMs);
}

bool RedisClient::sendCommand(const QList<QByteArray>& arguments, Reply* reply, int timeoutMs) {
    if (!ensureConnected(timeoutMs)) return false;

    const QByteArray command = encodeCommand(arguments);
    if (m_socket.write(command) != command.size() || !m_socket.waitForBytesWritten(timeoutMs)) {
        m_lastError = m_socket.errorString();
        m_socket.abort();
        return false;
    }

    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        buffer.append(m_socket.readAll());

        Reply parsed;
        int consumed = 0;
        QString parseError;
        if (parseReply(buffer, &parsed, &consumed, &parseError)) {
            if (parsed.isError()) {
                m_lastError = parsed.error;
                return false;
            }
            if (reply) *reply = parsed;
            m_lastError.clear();
            return true;
        }
        if (!parseError.isEmpty() && parseError != "incomplete") {
            m_lastError = parseError;
            m_socket.abort();
            return false;
        }

        const int remainingMs = qMax(1, timeoutMs - static_cast<int>(timer.elapsed()));
        if (!m_socket.waitForReadyRead(remainingMs)) break;
    }

    m_lastError = m_socket.errorString().isEmpty() ? "Redis reply timed out" : m_socket.errorString();
    m_socket.abort();
    return false;
}

QByteArray RedisClient::pubSubChannel(const QString& channel) const {
    QString normalized = channel.trimmed();
    while (normalized.startsWith(':')) {
        normalized.remove(0, 1);
    }
    return (m_prefix + ":pubsub:" + normalized).toUtf8();
}

QByteArray RedisClient::presenceUsersKey() const {
    return (m_prefix + ":presence:users").toUtf8();
}

QByteArray RedisClient::presenceKey(const QString& userId) const {
    return (m_prefix + ":presence:" + userId).toUtf8();
}

QByteArray RedisClient::presenceValue(const QString& userId, const QString& userName) const {
    QJsonObject obj;
    obj["userId"] = userId;
    obj["userName"] = userName;
    obj["lastSeen"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}
