#include "server.h"
#include "heartbeatmonitor.h"
#include "qqnt_redis_service.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTimer>

void Server::stop() {
    if (m_stopping) {
        return;
    }
    m_stopping = true;
    m_serviceReady = false;
    m_serviceReadinessReason = QStringLiteral("server-stopped");
    m_transferCleanupTimer->stop();
    m_offlineAttachmentCleanupTimer->stop();
    for (const ChatUser& user : m_clients.values()) {
        clearRedisPresence(user.id);
    }
    m_redisService->shutdown();
    const QList<QTcpSocket*> sockets = m_clients.keys();
    for (QTcpSocket* socket : sockets) {
        if (!socket) {
            continue;
        }
        disconnect(socket, nullptr, this, nullptr);
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    m_clients.clear();
    m_userSockets.clear();
    m_usedNames.clear();
    m_pendingFileTransfers.clear();
    m_tcpServer->close();
    qDebug() << "Server stopped";
    m_stopping = false;
}

void Server::onNewConnection() {
    QTcpSocket* clientSocket = m_tcpServer->nextPendingConnection();
    if (!clientSocket) return;

    qDebug() << "New connection from:" << clientSocket->peerAddress().toString()
             << "port:" << clientSocket->peerPort();

    connect(clientSocket, &QTcpSocket::readyRead, this, &Server::onClientReadyRead);
    connect(clientSocket, &QTcpSocket::disconnected, this, &Server::onClientDisconnected);
}

void Server::onClientReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray buffer = socket->property("buffer").toByteArray();
    buffer.append(socket->readAll());
    qDebug() << "Received from" << socket->peerAddress().toString() << ":" << buffer.size() << "bytes";

    while (buffer.contains('\n')) {
        int newlineIndex = buffer.indexOf('\n');
        QByteArray line = buffer.left(newlineIndex);
        buffer = buffer.mid(newlineIndex + 1);
        if (line.isEmpty()) continue;

        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isNull() || !doc.isObject()) {
            qWarning() << "Invalid JSON received";
            continue;
        }

        QJsonObject obj = doc.object();
        QString type = obj["type"].toString();
        if (type != "heartbeat" && !ensureServiceReady(socket, type)) {
            continue;
        }
        if (ChatUser* user = findUserBySocket(socket)) {
            user->lastActive = QDateTime::currentDateTime();
            refreshRedisPresence(*user);
            m_heartbeatMonitor->updateClientActivity(user->id);
        }

        if (type == "login") {
            handleLogin(obj, socket);
        } else if (type == "account_deactivation_request") {
            handleAccountDeactivationRequest(obj, socket);
        } else if (type == "account_deactivation_cancel") {
            handleAccountDeactivationCancel(obj, socket);
        } else if (type == "message") {
            handleMessage(obj, socket);
        } else if (type == "file") {
            handleFile(obj, socket);
        } else if (type == "file_chunk") {
            handleFileChunk(obj, socket);
        } else if (type == "file_transfer_resume_query") {
            handleFileTransferResumeQuery(obj, socket);
        } else if (type == "file_transfer_cancel") {
            handleFileTransferCancel(obj, socket);
        } else if (type == "file_chunk_ack") {
            emit fileChunkAckReceived(
                socket,
                obj["transferId"].toString(),
                obj["chunkIndex"].toVariant().toLongLong(),
                obj["accepted"].toBool(false),
                obj["reason"].toString(),
                obj["receivedBytes"].toVariant().toLongLong());
        } else if (type == "private") {
            handleMessage(obj, socket);
        } else if (type == "profile_update") {
            handleProfileUpdate(obj, socket);
        } else if (type == "e2e_identity_announce") {
            handleE2EIdentityAnnouncement(obj, socket);
        } else if (type == "e2e_key_rotation_request" || type == "e2e_key_rotation_response") {
            handleE2EKeyRotation(obj, socket);
        } else if (type == "server_group_create") {
            handleServerGroupCreate(obj, socket);
        } else if (type == "server_group_message") {
            handleServerGroupMessage(obj, socket);
        } else if (type == "server_group_announcement_update") {
            handleServerGroupAnnouncementUpdate(obj, socket);
        } else if (type == "server_group_settings_update") {
            handleServerGroupSettingsUpdate(obj, socket);
        } else if (type == "server_group_user_settings_update") {
            handleServerGroupUserSettingsUpdate(obj, socket);
        } else if (type == "server_group_member_update") {
            handleServerGroupMemberUpdate(obj, socket);
        } else if (type == "server_group_essence_update") {
            handleServerGroupEssenceUpdate(obj, socket);
        } else if (type == "message_favorite_update") {
            handleMessageFavoriteUpdate(obj, socket);
        } else if (type == "server_group_message_recall") {
            handleServerGroupMessageRecall(obj, socket);
        } else if (type == "server_group_member_mute") {
            handleServerGroupMemberMute(obj, socket);
        } else if (type == "server_group_member_unmute") {
            handleServerGroupMemberUnmute(obj, socket);
        } else if (type == "server_group_member_profile_request") {
            handleServerGroupMemberProfileRequest(obj, socket);
        } else if (type == "server_group_search") {
            handleServerGroupSearch(obj, socket);
        } else if (type == "server_group_join_request") {
            handleServerGroupJoinRequest(obj, socket);
        } else if (type == "server_group_join_response") {
            handleServerGroupJoinResponse(obj, socket);
        } else if (type == "server_group_leave") {
            handleServerGroupLeave(obj, socket);
        } else if (type == "server_group_dissolve") {
            handleServerGroupDissolve(obj, socket);
        } else if (type == "friend_request" || type == "friend_response" || type == "friend_search") {
            handleFriendEvent(obj, socket);
        } else if (type == "heartbeat") {
            // Activity was refreshed when the frame was accepted.
        }
    }

    socket->setProperty("buffer", buffer);
}

void Server::onClientDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    if (m_stopping) {
        socket->deleteLater();
        return;
    }

    ChatUser* user = findUserBySocket(socket);
    if (user) {
        const ChatUser disconnectedUser = *user;
        recordUserSessionToSqlite(disconnectedUser, "logout");
        const QString userId = disconnectedUser.id;
        const QString userName = disconnectedUser.name;
        const bool ownsCurrentRoute = m_userSockets.value(userId) == socket;
        if (ownsCurrentRoute) {
            clearRedisPresence(userId);
            m_heartbeatMonitor->unregisterClient(userId);
            m_userSockets.remove(userId);
        }
        for (const QString& key : m_pendingFileTransfers.keys()) {
            auto it = m_pendingFileTransfers.find(key);
            if (it != m_pendingFileTransfers.end() && it->socket == socket) {
                it->socket = nullptr;
            }
        }
        m_clients.remove(socket);
        m_usedNames.remove(userName);

        // A newer login already replaced this account's route. Closing the old
        // socket must not remove its presence or announce the account offline.
        if (!ownsCurrentRoute) {
            refreshConnectedClientViews();
            socket->deleteLater();
            return;
        }

        emit userLeft(userId, userName);
        emit clientDisconnected(userId);

        Message sysMsg;
        sysMsg.type = MessageType::System;
        sysMsg.content = userName + " 离开了聊天室";
        sysMsg.timestamp = QDateTime::currentDateTime();
        broadcastMessage(sysMsg, socket);
        refreshConnectedClientViews();

        qDebug() << "User disconnected:" << userName;
    }
    socket->deleteLater();
}
