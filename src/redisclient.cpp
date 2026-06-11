#include "redisclient.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QtGlobal>

namespace {
constexpr qint64 kReconnectBackoffMs = 5000;
constexpr int kSubscriberReconnectDelayMs = 1000;

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

bool RedisClient::hasPresence(const QString& userId, int timeoutMs) {
    bool online = false;
    return queryPresence(userId, &online, timeoutMs) && online;
}

bool RedisClient::queryPresence(const QString& userId, bool* online, int timeoutMs) {
    if (online) *online = false;
    if (!m_enabled || userId.isEmpty()) return false;

    Reply reply;
    if (!sendCommand({QByteArrayLiteral("GET"), presenceKey(userId)}, &reply, timeoutMs)) {
        return false;
    }

    if (online) {
        *online = reply.type == ReplyType::BulkString
            && !reply.isNull
            && !reply.value.isEmpty();
    }
    return true;
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

RedisSubscriber::RedisSubscriber(QObject* parent)
    : QObject(parent) {
    connect(&m_socket, &QTcpSocket::readyRead, this, &RedisSubscriber::onReadyRead);
    connect(&m_socket, &QTcpSocket::disconnected, this, &RedisSubscriber::onDisconnected);
}

void RedisSubscriber::configureFromEnvironment() {
    m_enabled = RedisClient::isEnabledFromEnvironment();
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

bool RedisSubscriber::subscribe(const QString& channel, int timeoutMs) {
    if (!m_enabled) return true;
    const QString normalizedChannel = channel.trimmed();
    if (normalizedChannel.isEmpty()) return false;

    m_manualDisconnect = false;
    m_subscribedChannel = normalizedChannel;
    if (!connectToServer(timeoutMs)) {
        scheduleReconnect();
        return false;
    }

    if (!m_password.isEmpty()) {
        RedisClient::Reply authReply;
        if (!writeCommand({QByteArrayLiteral("AUTH"), m_password.toUtf8()}, timeoutMs)
            || !readReply(&authReply, timeoutMs)
            || !authReply.isSimpleString(QByteArrayLiteral("OK"))) {
            if (m_lastError.isEmpty()) m_lastError = "Redis subscriber AUTH failed";
            m_socket.abort();
            scheduleReconnect();
            return false;
        }
    }

    const QByteArray redisChannel = pubSubChannel(normalizedChannel);
    if (!writeCommand({QByteArrayLiteral("SUBSCRIBE"), redisChannel}, timeoutMs)) {
        scheduleReconnect();
        return false;
    }

    RedisClient::Reply subscribeReply;
    if (!readReply(&subscribeReply, timeoutMs)
        || subscribeReply.type != RedisClient::ReplyType::Array
        || subscribeReply.elements.size() < 3
        || subscribeReply.elements.at(0).value.toLower() != QByteArrayLiteral("subscribe")
        || subscribeReply.elements.at(1).value != redisChannel) {
        if (m_lastError.isEmpty()) m_lastError = "Redis subscribe acknowledgement is invalid";
        m_socket.abort();
        scheduleReconnect();
        return false;
    }

    m_subscribed = true;
    m_reconnectScheduled = false;
    emit subscriptionStateChanged(true);
    processBuffer();
    return true;
}

void RedisSubscriber::disconnectFromServer() {
    m_manualDisconnect = true;
    m_reconnectScheduled = false;
    m_subscribedChannel.clear();
    m_subscribed = false;
    m_buffer.clear();
    emit subscriptionStateChanged(false);
    m_socket.disconnectFromHost();
}

bool RedisSubscriber::connectToServer(int timeoutMs) {
    if (m_socket.state() == QAbstractSocket::ConnectedState) return true;

    m_socket.abort();
    m_socket.connectToHost(m_host, m_port);
    if (!m_socket.waitForConnected(timeoutMs)) {
        m_lastError = m_socket.errorString();
        return false;
    }
    m_lastError.clear();
    return true;
}

bool RedisSubscriber::writeCommand(const QList<QByteArray>& arguments, int timeoutMs) {
    const QByteArray command = RedisClient::encodeCommand(arguments);
    if (m_socket.write(command) != command.size() || !m_socket.waitForBytesWritten(timeoutMs)) {
        m_lastError = m_socket.errorString();
        m_socket.abort();
        return false;
    }
    return true;
}

bool RedisSubscriber::readReply(RedisClient::Reply* reply, int timeoutMs) {
    struct ReadGuard {
        bool& flag;
        explicit ReadGuard(bool& value) : flag(value) { flag = true; }
        ~ReadGuard() { flag = false; }
    } guard(m_readingSynchronously);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        m_buffer.append(m_socket.readAll());

        RedisClient::Reply parsed;
        int consumed = 0;
        QString parseError;
        if (RedisClient::parseReply(m_buffer, &parsed, &consumed, &parseError)) {
            m_buffer = m_buffer.mid(consumed);
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

    m_lastError = m_socket.errorString().isEmpty() ? "Redis subscribe reply timed out" : m_socket.errorString();
    m_socket.abort();
    return false;
}

void RedisSubscriber::processBuffer() {
    while (!m_buffer.isEmpty()) {
        RedisClient::Reply reply;
        int consumed = 0;
        QString parseError;
        if (!RedisClient::parseReply(m_buffer, &reply, &consumed, &parseError)) {
            if (parseError != "incomplete") {
                m_lastError = parseError;
                m_socket.abort();
            }
            return;
        }

        m_buffer = m_buffer.mid(consumed);
        RedisClient::PubSubMessage message;
        if (RedisClient::parsePubSubMessage(reply, &message)) {
            emit messageReceived(message);
        }
    }
}

void RedisSubscriber::scheduleReconnect() {
    if (!m_enabled
        || m_manualDisconnect
        || m_subscribed
        || m_subscribedChannel.isEmpty()
        || m_reconnectScheduled) {
        return;
    }

    m_reconnectScheduled = true;
    QTimer::singleShot(kSubscriberReconnectDelayMs, this, [this]() {
        m_reconnectScheduled = false;
        if (!m_enabled
            || m_manualDisconnect
            || m_subscribed
            || m_subscribedChannel.isEmpty()) {
            return;
        }

        subscribe(m_subscribedChannel);
    });
}

QByteArray RedisSubscriber::pubSubChannel(const QString& channel) const {
    QString normalized = channel.trimmed();
    while (normalized.startsWith(':')) {
        normalized.remove(0, 1);
    }
    return (m_prefix + ":pubsub:" + normalized).toUtf8();
}

void RedisSubscriber::onReadyRead() {
    if (m_readingSynchronously) return;
    m_buffer.append(m_socket.readAll());
    processBuffer();
}

void RedisSubscriber::onDisconnected() {
    const bool shouldReconnect = m_enabled && !m_manualDisconnect && !m_subscribedChannel.isEmpty();
    m_subscribed = false;
    emit subscriptionStateChanged(false);
    emit disconnected();
    if (shouldReconnect) {
        scheduleReconnect();
    }
}
