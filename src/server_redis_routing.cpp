#include "server.h"
#include "server_group_support.h"
#include "server_database.h"
#include "server_delivery_support.h"
#include "objectstore.h"
#include "qqnt_redis_service.h"
#include "redisclient.h"
#include "heartbeatmonitor.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QJsonArray>
#include <QStringList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QTimer>
#include <QPointer>
#include <QUuid>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <memory>

namespace {
using namespace ServerDatabase;
using namespace ServerDeliverySupport;

}

bool Server::ensureRedisReadyForStartup() {
    QString error;
    if (!m_redisService->initialize(&error)) {
        m_serviceReady = false;
        m_serviceReadinessReason = m_redisService->readinessReason();
        if (m_serviceReadinessReason == QStringLiteral("redis-required")) {
            qWarning() << "Redis is required for server startup; set QTNETWORKCHAT_REDIS=1";
        } else {
            qWarning() << "Redis startup check failed:" << error;
        }
        return false;
    }

    qDebug() << "Redis presence service enabled";
    qDebug() << "Redis Pub/Sub subscriber enabled";
    refreshServiceReadiness();
    return true;
}

void Server::updateRedisCommandAvailability(bool available, const QString& reason) {
    const bool wasReady = m_serviceReady;
    m_redisService->setCommandAvailability(available, reason);
    if (!available && wasReady) {
        qWarning() << "Redis command channel became unavailable:" << m_redisService->readinessReason();
    }
    refreshServiceReadiness();
}

void Server::updateRedisSubscriberAvailability(bool available, const QString& reason) {
    const bool wasReady = m_serviceReady;
    m_redisService->setSubscriberAvailability(available, reason);
    if (!available && wasReady) {
        qWarning() << "Redis subscriber channel became unavailable:" << m_redisService->readinessReason();
    }
    refreshServiceReadiness();
}

void Server::refreshServiceReadiness() {
    m_serviceReady = m_redisService->isReady();
    m_serviceReadinessReason = m_redisService->readinessReason();
}

void Server::tryRecoverRedisCommandAvailability() {
    m_redisService->recoverCommandAvailability();
    refreshServiceReadiness();
}

void Server::tryRecoverRedisSubscriberAvailability() {
    m_redisService->recoverSubscriberAvailability();
    refreshServiceReadiness();
}

bool Server::ensureServiceReady(QTcpSocket* socket, const QString& action) {
    tryRecoverRedisCommandAvailability();
    tryRecoverRedisSubscriberAvailability();
    if (m_serviceReady) {
        return true;
    }

    if (socket && socket->state() == QAbstractSocket::ConnectedState) {
        const QString actionName = action.trimmed().isEmpty() ? QStringLiteral("request") : action.trimmed();
        sendSystemNotice(socket,
                         QStringLiteral("服务暂不可用：Redis 未就绪，已拒绝 %1。")
                             .arg(actionName));
    }
    return false;
}

void Server::refreshRedisPresence(const ChatUser& user) {
    if (!m_redisService->isEnabled()) return;
    if (!m_redisService->setPresence(user.id, user.name)) return;
    publishRedisPresenceEvent(user.id, QStringLiteral("online"));
}

void Server::clearRedisPresence(const QString& userId) {
    if (!m_redisService->isEnabled()) return;
    if (!m_redisService->clearPresence(userId)) return;
    publishRedisPresenceEvent(userId, QStringLiteral("offline"));
}

bool Server::publishRedisPresenceEvent(const QString& userId, const QString& action) const {
    if (!m_redisService->isEnabled() || userId.trimmed().isEmpty()) {
        return false;
    }

    QJsonObject event;
    event["eventType"] = QStringLiteral("presence_update");
    event["instanceId"] = m_instanceId;
    event["userId"] = userId.trimmed();
    event["action"] = action.trimmed().isEmpty() ? QStringLiteral("online") : action.trimmed();
    event["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return m_redisService->publish(QStringLiteral("messages"),
                                   QJsonDocument(event).toJson(QJsonDocument::Compact));
}

bool Server::publishRedisServerGroupSnapshotRefresh(const QStringList& userIds,
                                                    const QString& groupId,
                                                    const QString& notice,
                                                    const QString& memberId,
                                                    const QString& memberAction) const {
    if (!m_redisService->isEnabled() || groupId.trimmed().isEmpty()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    QJsonArray userIdArray;
    QStringList normalizedUserIds;
    for (const QString& userId : userIds) {
        const QString normalizedUserId = userId.trimmed();
        if (normalizedUserId.isEmpty() || normalizedUserIds.contains(normalizedUserId)) {
            continue;
        }
        normalizedUserIds << normalizedUserId;
        userIdArray.append(normalizedUserId);
    }
    if (userIdArray.isEmpty()) return false;

    QJsonObject event;
    event["eventType"] = QStringLiteral("server_group_snapshot_refresh");
    event["instanceId"] = m_instanceId;
    event["groupId"] = groupId.trimmed();
    event["userIds"] = userIdArray;
    event["notice"] = notice.left(200);
    const QString normalizedMemberId = memberId.trimmed();
    const QString normalizedMemberAction = memberAction.trimmed().toLower();
    if (!normalizedMemberId.isEmpty() && !normalizedMemberAction.isEmpty()) {
        event["memberId"] = normalizedMemberId;
        event["memberAction"] = normalizedMemberAction;
    }
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Skip Redis server group snapshot refresh because encoded payload is too large:"
                   << eventPayload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        return false;
    }
    return m_redisService->publish(QStringLiteral("messages"), eventPayload);
}

void Server::refreshConnectedClientViews() {
    struct ClientViewTarget {
        QTcpSocket* socket = nullptr;
        QString userId;
    };

    QVector<ClientViewTarget> targets;
    targets.reserve(m_clients.size());
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        QTcpSocket* clientSocket = it.key();
        if (!clientSocket || clientSocket->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        targets.append({clientSocket, it.value().id});
    }

    for (const ClientViewTarget& target : targets) {
        if (!target.socket || target.socket->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        sendUserList(target.socket);
        if (!target.socket || target.socket->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        sendFriendListSnapshot(target.userId, target.socket);
        if (!target.socket || target.socket->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        sendServerGroupSnapshot(target.userId, target.socket);
    }
}

bool Server::isRedisUserOnline(const QString& userId, bool* online) const {
    if (online) *online = false;
    if (!m_redisService->isEnabled() || userId.isEmpty()) return false;
    return m_redisService->queryPresence(userId, online);
}

bool Server::canPublishRedisMessageEvent(const Message& msg, const QString& deliveryState) const {
    if (!m_redisService->isEnabled()) return false;

    const bool isRedisFilePayload =
        (msg.type == MessageType::File || msg.type == MessageType::Image)
        && !msg.fileData.isEmpty()
        && msg.fileData.size() <= kRedisPubSubFileMaxBytes;
    if (msg.type != MessageType::Text && msg.type != MessageType::Private && !isRedisFilePayload) {
        return false;
    }

    const QJsonDocument messageDoc = QJsonDocument::fromJson(msg.toJson());
    if (!messageDoc.isObject()) return false;

    QJsonObject event;
    event["eventType"] = "chat_message";
    event["instanceId"] = m_instanceId;
    event["deliveryState"] = deliveryState;
    event["isPrivate"] = msg.isPrivate();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    event["message"] = messageDoc.object();

    return QJsonDocument(event).toJson(QJsonDocument::Compact).size() <= kRedisPubSubEventMaxBytes;
}

bool Server::publishRedisMessageEvent(const Message& msg, const QString& deliveryState) {
    if (!m_redisService->isEnabled()) return false;
    tryRecoverRedisCommandAvailability();
    if (!canPublishRedisMessageEvent(msg, deliveryState)) return false;

    const QJsonDocument messageDoc = QJsonDocument::fromJson(msg.toJson());
    if (!messageDoc.isObject()) return false;

    QJsonObject event;
    event["eventType"] = "chat_message";
    event["instanceId"] = m_instanceId;
    event["deliveryState"] = deliveryState;
    event["isPrivate"] = msg.isPrivate();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    event["message"] = messageDoc.object();

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Skip Redis Pub/Sub message event because encoded payload is too large:"
                   << eventPayload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        return false;
    }
    return m_redisService->publish("messages", eventPayload);
}

bool Server::publishRedisE2EControlEvent(const QJsonObject& forwarded) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();
    const QString receiverId = forwarded.value("receiverId").toString().trimmed();
    if (receiverId.isEmpty()) return false;

    QJsonObject event;
    event["eventType"] = "e2e_control";
    event["instanceId"] = m_instanceId;
    event["receiverId"] = receiverId;
    event["senderId"] = forwarded.value("senderId").toString();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    event["message"] = forwarded;

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Skip Redis E2E control event because encoded payload is too large:"
                   << eventPayload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        return false;
    }
    return m_redisService->publish("messages", eventPayload);
}

void Server::handleRedisMessageEvent(const QByteArray& payload) {
    if (payload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Ignore Redis Pub/Sub message event because encoded payload is too large:"
                   << payload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) return;

    const QJsonObject event = doc.object();
    const QString eventType = event["eventType"].toString();
    if (eventType == "server_group_snapshot_refresh") {
        handleRedisServerGroupSnapshotRefresh(event);
        return;
    }
    if (eventType == "e2e_control") {
        handleRedisE2EControlEvent(event);
        return;
    }
    if (eventType == "large_file_offer") {
        handleRedisLargeFileOffer(event);
        return;
    }
    if (eventType == "large_file_delivered") {
        handleRedisLargeFileDelivered(event);
        return;
    }
    if (eventType == "large_file_failed") {
        handleRedisLargeFileFailed(event);
        return;
    }
    if (eventType == "presence_update") {
        if (event["instanceId"].toString() == m_instanceId) return;
        refreshConnectedClientViews();
        return;
    }
    if (eventType != "chat_message") return;
    if (event["instanceId"].toString() == m_instanceId) return;

    const QJsonObject messageObj = event["message"].toObject();
    if (messageObj.isEmpty()) return;
    const QString deliveryState = event.value("deliveryState").toString();

    const Message msg = Message::fromJson(QJsonDocument(messageObj).toJson(QJsonDocument::Compact));
    if (msg.senderId.isEmpty()) return;
    const bool isRedisFilePayload =
        (msg.type == MessageType::File || msg.type == MessageType::Image)
        && !msg.fileData.isEmpty()
        && msg.fileData.size() <= kRedisPubSubFileMaxBytes;
    if (msg.type != MessageType::Text && msg.type != MessageType::Private && !isRedisFilePayload) return;

    if (msg.receiverId.isEmpty()) {
        broadcastMessage(msg);
    } else {
        if (deliveryState == QLatin1String("server-group") && msg.type == MessageType::Text) {
            const QStringList groupMemberIds = serverGroupMemberIds(msg.receiverId);
            if (!groupMemberIds.isEmpty()) {
                for (const QString& memberId : groupMemberIds) {
                    QTcpSocket* memberSocket = m_userSockets.value(memberId);
                    if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
                    QJsonObject forwarded = QJsonDocument::fromJson(msg.toJson()).object();
                    forwarded["type"] = "server_group_message";
                    forwarded["groupId"] = msg.receiverId;
                    memberSocket->write(QJsonDocument(forwarded).toJson(QJsonDocument::Compact));
                    memberSocket->write("\n");
                    memberSocket->flush();
                }
                emit newMessage(msg);
                return;
            }
        }
        if (deliveryState == QLatin1String("server-group-file") && isRedisFilePayload) {
            const QStringList groupMemberIds = serverGroupMemberIds(msg.receiverId);
            if (groupMemberIds.isEmpty()) return;
            for (const QString& memberId : groupMemberIds) {
                if (memberId == msg.senderId) continue;
                QTcpSocket* memberSocket = m_userSockets.value(memberId);
                if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
                Message groupMsg = msg;
                groupMsg.receiverId = msg.receiverId;
                if (!sendChunkedFileToSocket(groupMsg, memberSocket)) {
                    qWarning() << "Redis private group file delivery failed"
                               << msg.receiverId << memberId << msg.fileName;
                }
            }
            emit newMessage(msg);
            return;
        }
        QTcpSocket* targetSocket = m_userSockets.value(msg.receiverId);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            sendToUser(msg);
        } else {
            return;
        }
    }

    emit newMessage(msg);
}

void Server::handleRedisServerGroupSnapshotRefresh(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;

    const QString groupId = event.value("groupId").toString().trimmed();
    if (groupId.isEmpty()) return;

    const QString notice = event.value("notice").toString().trimmed();
    QStringList userIds;
    const QJsonArray userIdArray = event.value("userIds").toArray();
    for (const QJsonValue& userIdValue : userIdArray) {
        const QString userId = userIdValue.toString().trimmed();
        if (!userId.isEmpty() && !userIds.contains(userId)) {
            userIds << userId;
        }
    }

    for (const QString& userId : userIds) {
        QTcpSocket* targetSocket = m_userSockets.value(userId);
        if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) continue;
        const bool isActiveMember = isServerGroupMember(groupId, userId);
        if (!isActiveMember && !isServerGroupRemovedMember(groupId, userId)) continue;
        if (!notice.isEmpty()) {
            sendSystemNotice(targetSocket, notice);
        }
        const QString memberId = event.value("memberId").toString().trimmed();
        const QString memberAction = event.value("memberAction").toString().trimmed().toLower();
        if (isActiveMember && !memberId.isEmpty() && !memberAction.isEmpty()) {
            sendServerGroupMemberUpdated(targetSocket, groupId, memberId, memberAction);
        }
        sendServerGroupSnapshot(userId, targetSocket);
    }
}

void Server::handleRedisE2EControlEvent(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;
    const QString receiverId = event.value("receiverId").toString().trimmed();
    if (receiverId.isEmpty()) return;

    QTcpSocket* targetSocket = m_userSockets.value(receiverId);
    if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const QJsonObject message = event.value("message").toObject();
    const QString type = message.value("type").toString();
    if ((type != QLatin1String("e2e_identity_announce")
         && type != QLatin1String("e2e_key_rotation_request")
         && type != QLatin1String("e2e_key_rotation_response"))
        || message.value("receiverId").toString().trimmed() != receiverId
        || message.value("senderId").toString().trimmed().isEmpty()) {
        return;
    }

    targetSocket->write(QJsonDocument(message).toJson(QJsonDocument::Compact));
    targetSocket->write("\n");
    targetSocket->flush();
}
