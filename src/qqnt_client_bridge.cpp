#include "qqnt_client_bridge.h"

#include "chatuser.h"
#include "message.h"
#include "qqnt_engine_command_router.h"
#include "qtnetworkchat_version.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QTextStream>
#include <QVersionNumber>
#include <QtGlobal>

#include <cstdio>

namespace {
constexpr int kQQNTProtocolVersion = 1;

QString messageTypeName(MessageType type) {
    switch (type) {
    case MessageType::File:
        return QStringLiteral("file");
    case MessageType::Image:
        return QStringLiteral("image");
    case MessageType::System:
        return QStringLiteral("system");
    default:
        return QStringLiteral("text");
    }
}

void writeProtocolObject(const QJsonObject& object) {
    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    std::fwrite(data.constData(), 1, static_cast<size_t>(data.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}
}

QQNTClientBridge::QQNTClientBridge(QObject* parent)
    : QObject(parent)
    , m_router(new QQNTEngineCommandRouter(this))
    , m_port(0)
{
    bindClientSignals();
}

QQNTClientBridge::~QQNTClientBridge() = default;

void QQNTClientBridge::start() {
    sendEvent(QStringLiteral("ready"), readyPayload());
}

void QQNTClientBridge::handleCommandLine(const QByteArray& line) {
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        sendErrorAck(QString(), QString(), QStringLiteral("invalid_json"), parseError.errorString());
        return;
    }

    m_router->route(document.object());
}

QJsonObject QQNTClientBridge::readyPayload() const {
    QJsonObject payload;
    payload[QStringLiteral("protocolVersion")] = kQQNTProtocolVersion;
    payload[QStringLiteral("version")] = QStringLiteral(QTNETWORKCHAT_VERSION_STRING);
    payload[QStringLiteral("qtVersion")] = QString::fromLatin1(qVersion());
    payload[QStringLiteral("e2eStatus")] = m_client.e2eLocalIdentityStatus().isEmpty()
        ? QStringLiteral("uninitialized")
        : QStringLiteral("initialized");
    return payload;
}

QJsonObject QQNTClientBridge::userListPayload() const {
    QJsonArray userArray;
    for (const ChatUser& user : m_client.onlineUsers()) {
        userArray.append(userToJson(user));
    }

    QJsonObject payload;
    payload[QStringLiteral("users")] = userArray;
    return payload;
}

QJsonObject QQNTClientBridge::groupListPayload() const {
    QJsonObject payload;
    payload[QStringLiteral("groups")] = m_client.serverGroups();
    payload[QStringLiteral("removedGroups")] = m_client.removedServerGroups();
    payload[QStringLiteral("hasSnapshot")] = m_client.hasServerGroupSnapshot();
    return payload;
}

void QQNTClientBridge::setConnectionTarget(const QString& host, quint16 port) {
    m_host = host;
    m_port = port;
}

void QQNTClientBridge::sendAck(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QJsonObject envelope;
    envelope[QStringLiteral("type")] = QStringLiteral("ack");
    envelope[QStringLiteral("op")] = op;
    envelope[QStringLiteral("reqId")] = reqId;
    envelope[QStringLiteral("status")] = QStringLiteral("ok");
    envelope[QStringLiteral("payload")] = payload;
    writeProtocolObject(envelope);
}

void QQNTClientBridge::sendErrorAck(const QString& op,
                                    const QString& reqId,
                                    const QString& code,
                                    const QString& message,
                                    const QString& source) {
    QJsonObject error;
    error[QStringLiteral("code")] = code;
    error[QStringLiteral("message")] = message;
    error[QStringLiteral("source")] = source;

    QJsonObject envelope;
    envelope[QStringLiteral("type")] = QStringLiteral("ack");
    envelope[QStringLiteral("op")] = op;
    envelope[QStringLiteral("reqId")] = reqId;
    envelope[QStringLiteral("status")] = QStringLiteral("error");
    envelope[QStringLiteral("payload")] = QJsonObject();
    envelope[QStringLiteral("error")] = error;
    writeProtocolObject(envelope);
}

void QQNTClientBridge::sendEvent(const QString& event, const QJsonObject& payload) {
    QJsonObject envelope;
    envelope[QStringLiteral("type")] = QStringLiteral("event");
    envelope[QStringLiteral("event")] = event;
    envelope[QStringLiteral("payload")] = payload;
    writeProtocolObject(envelope);
}

void QQNTClientBridge::bindClientSignals() {
    connect(&m_client, &Client::connected, this, [this]() {
        QJsonObject payload;
        payload[QStringLiteral("connected")] = true;
        payload[QStringLiteral("host")] = m_host;
        payload[QStringLiteral("port")] = static_cast<int>(m_port);
        sendEvent(QStringLiteral("connection_state"), payload);
    });

    connect(&m_client, &Client::disconnected, this, [this]() {
        QJsonObject payload;
        payload[QStringLiteral("connected")] = false;
        payload[QStringLiteral("host")] = m_host;
        payload[QStringLiteral("port")] = static_cast<int>(m_port);
        sendEvent(QStringLiteral("connection_state"), payload);
    });

    connect(&m_client, &Client::loginSucceeded, this, [this]() {
        QJsonObject payload;
        payload[QStringLiteral("success")] = true;
        payload[QStringLiteral("userId")] = m_client.currentUserId();
        payload[QStringLiteral("userName")] = m_client.currentUserName();
        payload[QStringLiteral("registered")] = m_client.currentLoginWasRegister();
        sendEvent(QStringLiteral("login_result"), payload);
    });

    connect(&m_client, &Client::loginFailed, this, [this](const QString& reason) {
        QJsonObject payload;
        payload[QStringLiteral("success")] = false;
        payload[QStringLiteral("error")] = reason;
        sendEvent(QStringLiteral("login_result"), payload);
    });

    connect(&m_client, &Client::connectionError, this, [this](const QString& error) {
        QJsonObject payload;
        payload[QStringLiteral("message")] = error;
        payload[QStringLiteral("source")] = QStringLiteral("client");
        sendEvent(QStringLiteral("error"), payload);
    });

    connect(&m_client, &Client::newMessage, this, [this](const Message& message) {
        QJsonObject payload;
        const QString sessionId = sessionIdForMessage(message);
        payload[QStringLiteral("sessionId")] = sessionId;
        payload[QStringLiteral("message")] = messageToJson(message);
        sendEvent(QStringLiteral("message"), payload);
    });

    connect(&m_client, &Client::userListUpdated, this, [this](const QVector<ChatUser>& users) {
        QJsonArray userArray;
        for (const ChatUser& user : users) {
            userArray.append(userToJson(user));
        }
        QJsonObject payload;
        payload[QStringLiteral("users")] = userArray;
        sendEvent(QStringLiteral("user_list"), payload);
    });

    connect(&m_client, &Client::userJoined, this, [this](const QString& userId, const QString& userName) {
        QJsonObject payload;
        payload[QStringLiteral("userId")] = userId;
        payload[QStringLiteral("userName")] = userName;
        sendEvent(QStringLiteral("user_joined"), payload);
    });

    connect(&m_client, &Client::userLeft, this, [this](const QString& userId, const QString& userName) {
        QJsonObject payload;
        payload[QStringLiteral("userId")] = userId;
        payload[QStringLiteral("userName")] = userName;
        sendEvent(QStringLiteral("user_left"), payload);
    });

    connect(&m_client, &Client::friendRequestReceived, this, [this](const QString& senderId, const QString& senderName) {
        QJsonObject payload;
        payload[QStringLiteral("type")] = QStringLiteral("request_received");
        payload[QStringLiteral("senderId")] = senderId;
        payload[QStringLiteral("senderName")] = senderName;
        sendEvent(QStringLiteral("friend_event"), payload);
    });

    connect(&m_client, &Client::friendRequestSent, this, [this](const QString& receiverId, bool delivered) {
        QJsonObject payload;
        payload[QStringLiteral("type")] = QStringLiteral("request_sent");
        payload[QStringLiteral("receiverId")] = receiverId;
        payload[QStringLiteral("delivered")] = delivered;
        sendEvent(QStringLiteral("friend_event"), payload);
    });

    connect(&m_client, &Client::friendSearchResult, this, [this](const QString&, const QString& userId, const QString& userName, bool found, bool online, bool, int, const QString& reason) {
        QJsonObject payload;
        payload[QStringLiteral("found")] = found;
        payload[QStringLiteral("userId")] = userId;
        payload[QStringLiteral("userName")] = userName;
        payload[QStringLiteral("online")] = online;
        payload[QStringLiteral("reason")] = reason;
        sendEvent(QStringLiteral("friend_search_result"), payload);
    });

    connect(&m_client, &Client::friendResponseReceived, this, [this](const QString& senderId, const QString& senderName, bool accepted) {
        QJsonObject payload;
        payload[QStringLiteral("type")] = QStringLiteral("response_received");
        payload[QStringLiteral("senderId")] = senderId;
        payload[QStringLiteral("senderName")] = senderName;
        payload[QStringLiteral("accepted")] = accepted;
        sendEvent(QStringLiteral("friend_event"), payload);
    });

    connect(&m_client, &Client::fileTransferProgress, this, [this](const QString& fileName, qint64 bytes, qint64 total, const QString& transferId) {
        QJsonObject payload;
        payload[QStringLiteral("transferId")] = transferId;
        payload[QStringLiteral("fileName")] = fileName;
        payload[QStringLiteral("bytes")] = QString::number(bytes);
        payload[QStringLiteral("total")] = QString::number(total);
        payload[QStringLiteral("direction")] = QStringLiteral("outgoing");
        sendEvent(QStringLiteral("file_progress"), payload);
    });

    connect(&m_client, &Client::fileReceiveProgress, this, [this](const QString& fileName, qint64 bytes, qint64 total, const QString& transferId) {
        QJsonObject payload;
        payload[QStringLiteral("transferId")] = transferId;
        payload[QStringLiteral("fileName")] = fileName;
        payload[QStringLiteral("bytes")] = QString::number(bytes);
        payload[QStringLiteral("total")] = QString::number(total);
        payload[QStringLiteral("direction")] = QStringLiteral("incoming");
        sendEvent(QStringLiteral("file_progress"), payload);
    });

    connect(&m_client, &Client::fileTransferStatusChanged, this, [this](const QString& fileName, const QString& transferId, const QString& reason, qint64 receivedBytes, qint64 totalBytes) {
        QJsonObject payload;
        payload[QStringLiteral("fileName")] = fileName;
        payload[QStringLiteral("transferId")] = transferId;
        payload[QStringLiteral("reason")] = reason;
        payload[QStringLiteral("bytes")] = QString::number(receivedBytes);
        payload[QStringLiteral("total")] = QString::number(totalBytes);
        sendEvent(reason.isEmpty() ? QStringLiteral("file_done") : QStringLiteral("file_error"), payload);
    });

    connect(&m_client, &Client::serverGroupSnapshotReceived, this, [this](const QJsonArray& groups) {
        QJsonObject payload;
        payload[QStringLiteral("groups")] = groups;
        sendEvent(QStringLiteral("group_snapshot"), payload);
    });

    connect(&m_client, &Client::serverGroupMemberUpdated, this, [this](const QString& groupId, const QString& memberId, const QString& action) {
        QJsonObject payload;
        payload[QStringLiteral("groupId")] = groupId;
        payload[QStringLiteral("memberId")] = memberId;
        payload[QStringLiteral("action")] = action;
        sendEvent(QStringLiteral("group_member_updated"), payload);
    });

    connect(&m_client, &Client::e2eSessionStateChanged, this, [this](const QString& peerId, const QJsonObject& status) {
        QJsonObject payload = status;
        payload[QStringLiteral("peerId")] = peerId;
        sendEvent(QStringLiteral("e2e_session_state"), payload);
    });

    connect(&m_client, &Client::e2eIdentityStateChanged, this, [this](const QString& peerId, const QJsonObject& status) {
        QJsonObject payload = status;
        payload[QStringLiteral("peerId")] = peerId;
        sendEvent(QStringLiteral("e2e_identity_state"), payload);
    });

    connect(&m_client, &Client::e2eSessionRotationRequested, this, [this](const QString& peerId, const QJsonObject& agreement) {
        QJsonObject payload;
        payload[QStringLiteral("peerId")] = peerId;
        payload[QStringLiteral("agreement")] = agreement;
        sendEvent(QStringLiteral("e2e_rotation_request"), payload);
    });

    connect(&m_client, &Client::e2eSessionRotationResponded, this, [this](const QString& peerId, const QJsonObject& agreement, bool accepted, const QString& reason) {
        QJsonObject payload;
        payload[QStringLiteral("peerId")] = peerId;
        payload[QStringLiteral("agreement")] = agreement;
        payload[QStringLiteral("accepted")] = accepted;
        payload[QStringLiteral("reason")] = reason;
        sendEvent(QStringLiteral("e2e_rotation_response"), payload);
    });
}

QJsonObject QQNTClientBridge::messageToJson(const Message& message) const {
    QJsonObject object = QJsonDocument::fromJson(message.toJson()).object();
    const QString sessionId = sessionIdForMessage(message);
    object[QStringLiteral("messageId")] = message.transferId.isEmpty()
        ? QString::number(message.timestamp.toMSecsSinceEpoch())
        : message.transferId;
    object[QStringLiteral("sessionId")] = sessionId;
    object[QStringLiteral("senderId")] = message.senderId;
    object[QStringLiteral("senderName")] = message.senderName;
    object[QStringLiteral("timestamp")] = QString::number(message.timestamp.toMSecsSinceEpoch());
    object[QStringLiteral("contentType")] = messageTypeName(message.type);
    object[QStringLiteral("status")] = QStringLiteral("received");
    return object;
}

QString QQNTClientBridge::sessionIdForMessage(const Message& message) const {
    const QString receiverId = message.receiverId.trimmed();
    if (receiverId.isEmpty()) {
        return QStringLiteral("group:public");
    }
    if (isKnownServerGroupId(receiverId)) {
        return QStringLiteral("group:%1").arg(receiverId);
    }
    return receiverId == m_client.currentUserId() ? message.senderId : receiverId;
}

bool QQNTClientBridge::isKnownServerGroupId(const QString& groupId) const {
    const QString trimmedGroupId = groupId.trimmed();
    if (trimmedGroupId.isEmpty()) {
        return false;
    }
    if (trimmedGroupId == QLatin1String("public")) {
        return true;
    }

    const auto groupArrayContains = [&trimmedGroupId](const QJsonArray& groups) {
        for (const QJsonValue& value : groups) {
            if (value.toObject().value(QStringLiteral("groupId")).toString() == trimmedGroupId) {
                return true;
            }
        }
        return false;
    };

    return groupArrayContains(m_client.serverGroups()) || groupArrayContains(m_client.removedServerGroups());
}

QJsonObject QQNTClientBridge::userToJson(const ChatUser& user) const {
    QJsonObject object;
    object[QStringLiteral("id")] = user.id;
    object[QStringLiteral("name")] = user.name;
    object[QStringLiteral("avatar")] = user.avatar;
    object[QStringLiteral("online")] = user.isOnline;
    object[QStringLiteral("lastActive")] = user.lastActive.toString(Qt::ISODateWithMs);
    return object;
}
