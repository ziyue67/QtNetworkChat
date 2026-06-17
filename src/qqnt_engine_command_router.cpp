#include "qqnt_engine_command_router.h"

#include "qqnt_client_bridge.h"
#include "localfilemanager.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonValue>
#include <QDir>
#include <QStringList>
#include <QVector>

namespace {
QJsonObject makeResumeStatePayload(const QString& transferId,
                                   qint64 confirmedBytes,
                                   qint64 nextChunkIndex,
                                   qint64 fileSize,
                                   qint64 chunkSize,
                                   qint64 chunkCount,
                                   const QString& fileHash,
                                   const QVector<qint64>& receivedChunks,
                                   bool resumed) {
    QJsonArray chunks;
    for (qint64 chunk : receivedChunks) {
        chunks.append(QString::number(chunk));
    }

    QJsonObject response;
    response[QStringLiteral("canResume")] = true;
    response[QStringLiteral("transferId")] = transferId;
    response[QStringLiteral("confirmedBytes")] = QString::number(confirmedBytes);
    response[QStringLiteral("nextChunkIndex")] = QString::number(nextChunkIndex);
    response[QStringLiteral("fileSize")] = QString::number(fileSize);
    response[QStringLiteral("chunkSize")] = QString::number(chunkSize);
    response[QStringLiteral("chunkCount")] = QString::number(chunkCount);
    response[QStringLiteral("fileHash")] = fileHash;
    response[QStringLiteral("receivedChunks")] = chunks;
    response[QStringLiteral("resumed")] = resumed;
    response[QStringLiteral("mode")] = resumed ? QStringLiteral("resume") : QStringLiteral("query");
    return response;
}

MessageType resumeMessageTypeFromPayload(const QJsonObject& payload) {
    const QJsonValue messageTypeValue = payload.value(QStringLiteral("messageType"));
    if (messageTypeValue.isDouble()
        && messageTypeValue.toInt() == static_cast<int>(MessageType::Image)) {
        return MessageType::Image;
    }

    const QString textType = payload.value(QStringLiteral("contentType"))
        .toString(messageTypeValue.toString())
        .trimmed()
        .toLower();
    if (textType == QLatin1String("image")) {
        return MessageType::Image;
    }
    return MessageType::File;
}

QString downloadDirFromSettings(const QJsonObject& settings) {
    const QString direct = settings.value(QStringLiteral("fileDownloadDir")).toString().trimmed();
    if (!direct.isEmpty()) {
        return direct;
    }

    const QJsonObject files = settings.value(QStringLiteral("files")).toObject();
    const QString nested = files.value(QStringLiteral("downloadDir")).toString().trimmed();
    if (!nested.isEmpty()) {
        return nested;
    }

    return files.value(QStringLiteral("downloadDirectory")).toString().trimmed();
}

QStringList memberIdsFromPayload(const QJsonObject& payload) {
    QStringList memberIds;
    const QJsonArray members = payload.value(QStringLiteral("members")).toArray();
    for (const QJsonValue& memberValue : members) {
        QString memberId = memberValue.toString().trimmed();
        if (memberId.isEmpty() && memberValue.isObject()) {
            const QJsonObject memberObject = memberValue.toObject();
            memberId = memberObject.value(QStringLiteral("userId")).toString(
                memberObject.value(QStringLiteral("account")).toString(
                    memberObject.value(QStringLiteral("id")).toString())).trimmed();
        }
        if (!memberId.isEmpty() && !memberIds.contains(memberId)) {
            memberIds << memberId;
        }
    }
    return memberIds;
}

bool applyDownloadDirSetting(const QJsonObject& settings, QString* appliedDownloadDir, QString* rejectReason) {
    const QString requestedDir = downloadDirFromSettings(settings);
    if (requestedDir.isEmpty()) {
        return true;
    }

    const QString cleanedDir = QDir::cleanPath(requestedDir);
    QDir dir(cleanedDir);
    if (!dir.exists() && !QDir().mkpath(cleanedDir)) {
        if (rejectReason) {
            *rejectReason = QStringLiteral("Download directory cannot be created: %1").arg(cleanedDir);
        }
        return false;
    }

    LocalFileManager::setReceivedDownloadRootDirectory(cleanedDir);
    if (appliedDownloadDir) {
        *appliedDownloadDir = LocalFileManager::receivedDownloadRootDirectory();
    }
    return true;
}
}

QQNTEngineCommandRouter::QQNTEngineCommandRouter(QQNTClientBridge* bridge)
    : QObject(bridge)
    , m_bridge(bridge)
    , m_settingsRevision(0)
    , m_hasAccountInfo(false)
{
}

void QQNTEngineCommandRouter::route(const QJsonObject& command) {
    const QString op = command.value(QStringLiteral("op")).toString().trimmed();
    const QString reqId = command.value(QStringLiteral("reqId")).toString().trimmed();
    const QJsonObject payload = command.value(QStringLiteral("payload")).toObject();

    if (op.isEmpty()) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("missing_op"), QStringLiteral("Command op is required."));
        return;
    }
    if (reqId.isEmpty()) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("missing_req_id"), QStringLiteral("Command reqId is required."));
        return;
    }

    if (op == QLatin1String("ready")) {
        handleReady(op, reqId);
    } else if (op == QLatin1String("login")) {
        handleLogin(op, reqId, payload, false);
    } else if (op == QLatin1String("register")) {
        handleLogin(op, reqId, payload, true);
    } else if (op == QLatin1String("connect")) {
        handleConnect(op, reqId, payload);
    } else if (op == QLatin1String("disconnect") || op == QLatin1String("logout")) {
        handleDisconnect(op, reqId);
    } else if (op == QLatin1String("set_user_info")) {
        handleSetUserInfo(op, reqId, payload);
    } else if (op == QLatin1String("get_user_list") || op == QLatin1String("get_friend_list")) {
        handleGetUserList(op, reqId);
    } else if (op == QLatin1String("get_group_list")) {
        handleGetGroupList(op, reqId);
    } else if (op == QLatin1String("search_friend")) {
        handleSearchFriend(op, reqId, payload);
    } else if (op == QLatin1String("send_friend_request")) {
        handleSendFriendRequest(op, reqId, payload);
    } else if (op == QLatin1String("respond_friend_request")) {
        handleRespondFriendRequest(op, reqId, payload);
    } else if (op == QLatin1String("send_private_message")) {
        handleSendPrivateMessage(op, reqId, payload);
    } else if (op == QLatin1String("send_group_message")) {
        handleSendGroupMessage(op, reqId, payload);
    } else if (op == QLatin1String("create_group")) {
        handleCreateGroup(op, reqId, payload);
    } else if (op == QLatin1String("update_group_announcement")) {
        handleUpdateGroupAnnouncement(op, reqId, payload);
    } else if (op == QLatin1String("update_group_member")) {
        handleUpdateGroupMember(op, reqId, payload);
    } else if (op == QLatin1String("send_file")) {
        handleSendFileLike(op, reqId, payload, false);
    } else if (op == QLatin1String("send_image")) {
        handleSendFileLike(op, reqId, payload, true);
    } else if (op == QLatin1String("cancel_transfer")) {
        handleCancelTransfer(op, reqId, payload);
    } else if (op == QLatin1String("query_resume")) {
        handleQueryResume(op, reqId, payload);
    } else if (op == QLatin1String("e2e_status")) {
        handleE2EStatus(op, reqId, payload);
    } else if (op == QLatin1String("e2e_announce_identity")) {
        handleE2EAnnounceIdentity(op, reqId, payload);
    } else if (op == QLatin1String("e2e_pin_identity")) {
        handleE2EPinIdentity(op, reqId, payload);
    } else if (op == QLatin1String("e2e_request_rotation")) {
        handleE2ERequestRotation(op, reqId, payload);
    } else if (op == QLatin1String("profile_update")) {
        handleProfileUpdate(op, reqId, payload);
    } else if (op == QLatin1String("settings_sync")) {
        handleSettingsSync(op, reqId, payload);
    } else {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("unknown_op"),
                               QStringLiteral("Unsupported command: %1").arg(op));
    }
}

void QQNTEngineCommandRouter::handleReady(const QString& op, const QString& reqId) {
    m_bridge->sendAck(op, reqId, m_bridge->readyPayload());
}

void QQNTEngineCommandRouter::handleLogin(const QString& op, const QString& reqId, const QJsonObject& payload, bool registerMode) {
    QString account;
    QString password;
    if (!requireString(payload, QStringLiteral("account"), &account, op, reqId)
        || !requireString(payload, QStringLiteral("password"), &password, op, reqId)) {
        return;
    }

    if (registerMode) {
        const QString userName = payload.value(QStringLiteral("userName")).toString().trimmed();
        if (!userName.isEmpty()) {
            m_bridge->client()->setUserInfo(QString(), userName);
        }
    }

    m_account = account;
    m_password = password;
    m_hasAccountInfo = true;
    m_bridge->client()->setAccountInfo(account, password, registerMode);

    QJsonObject response;
    response[QStringLiteral("accepted")] = true;
    response[QStringLiteral("requiresConnect")] = !m_bridge->client()->isConnected();
    response[QStringLiteral("mode")] = registerMode ? QStringLiteral("register") : QStringLiteral("login");
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleConnect(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!m_hasAccountInfo) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("login_required"),
                               QStringLiteral("Send login or register credentials before connect."));
        return;
    }

    const QString host = payload.value(QStringLiteral("host")).toString(QStringLiteral("127.0.0.1")).trimmed();
    const int portValue = payload.value(QStringLiteral("port")).toInt(8888);
    if (host.isEmpty() || portValue <= 0 || portValue > 65535) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("invalid_target"), QStringLiteral("connect requires a valid host and port."));
        return;
    }

    m_bridge->setConnectionTarget(host, static_cast<quint16>(portValue));
    const bool connected = m_bridge->client()->connectToServer(host, static_cast<quint16>(portValue));
    if (!connected) {
        const QString reason = m_bridge->client()->lastLoginError().isEmpty()
            ? QStringLiteral("Unable to connect to server.")
            : m_bridge->client()->lastLoginError();
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("connect_failed"), reason);
        return;
    }

    QJsonObject response;
    response[QStringLiteral("connected")] = true;
    response[QStringLiteral("host")] = host;
    response[QStringLiteral("port")] = portValue;
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleDisconnect(const QString& op, const QString& reqId) {
    m_bridge->client()->disconnectFromServer();
    m_bridge->sendAck(op, reqId);
}

void QQNTEngineCommandRouter::handleSetUserInfo(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString userId;
    QString userName;
    if (!requireString(payload, QStringLiteral("userId"), &userId, op, reqId)
        || !requireString(payload, QStringLiteral("userName"), &userName, op, reqId)) {
        return;
    }
    m_bridge->client()->setUserInfo(userId, userName);
    m_bridge->sendAck(op, reqId);
}

void QQNTEngineCommandRouter::handleGetUserList(const QString& op, const QString& reqId) {
    QJsonObject payload = m_bridge->userListPayload();
    payload[QStringLiteral("friends")] = payload.value(QStringLiteral("users")).toArray();
    m_bridge->sendAck(op, reqId, payload);
}

void QQNTEngineCommandRouter::handleGetGroupList(const QString& op, const QString& reqId) {
    m_bridge->sendAck(op, reqId, m_bridge->groupListPayload());
}

void QQNTEngineCommandRouter::handleSearchFriend(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString account;
    if (!requireString(payload, QStringLiteral("account"), &account, op, reqId)) {
        return;
    }
    sendBoolAck(op,
                reqId,
                m_bridge->client()->searchFriendByAccount(account),
                QStringLiteral("search_failed"),
                QStringLiteral("Friend search requires an active server connection."));
}

void QQNTEngineCommandRouter::handleSendFriendRequest(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString receiverId;
    if (!requireString(payload, QStringLiteral("receiverId"), &receiverId, op, reqId)) {
        return;
    }
    sendBoolAck(op,
                reqId,
                m_bridge->client()->sendFriendRequest(receiverId),
                QStringLiteral("friend_request_failed"),
                QStringLiteral("Friend request requires an active server connection."));
}

void QQNTEngineCommandRouter::handleRespondFriendRequest(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString senderId;
    if (!requireString(payload, QStringLiteral("senderId"), &senderId, op, reqId)) {
        return;
    }
    sendBoolAck(op,
                reqId,
                m_bridge->client()->sendFriendResponse(senderId, payload.value(QStringLiteral("accepted")).toBool()),
                QStringLiteral("friend_response_failed"),
                QStringLiteral("Friend response requires an active server connection."));
}

void QQNTEngineCommandRouter::handleSendPrivateMessage(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString receiverId;
    QString content;
    if (!requireString(payload, QStringLiteral("receiverId"), &receiverId, op, reqId)
        || !requireString(payload, QStringLiteral("content"), &content, op, reqId)) {
        return;
    }

    if (!m_bridge->client()->isConnected()) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("not_connected"), QStringLiteral("Engine is not connected to QQNTServer."));
        return;
    }

    if (!m_bridge->client()->sendPrivateMessage(receiverId, content)) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("send_failed"), QStringLiteral("Client rejected private message send."));
        return;
    }

    QJsonObject response;
    response[QStringLiteral("receiverId")] = receiverId;
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleSendGroupMessage(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString groupId;
    QString content;
    if (!requireString(payload, QStringLiteral("groupId"), &groupId, op, reqId)
        || !requireString(payload, QStringLiteral("content"), &content, op, reqId)) {
        return;
    }
    sendBoolAck(op,
                reqId,
                m_bridge->client()->sendServerGroupMessage(groupId, content),
                QStringLiteral("send_group_failed"),
                QStringLiteral("Group message requires an active server connection and valid group."));
}

void QQNTEngineCommandRouter::handleCreateGroup(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString groupName;
    if (!requireString(payload, QStringLiteral("groupName"), &groupName, op, reqId)) {
        return;
    }
    sendBoolAck(op,
                reqId,
                m_bridge->client()->createPrivateServerGroup(groupName,
                                                            payload.value(QStringLiteral("announcement")).toString(),
                                                            memberIdsFromPayload(payload)),
                QStringLiteral("create_group_failed"),
                QStringLiteral("Group creation requires an active server connection."));
}

void QQNTEngineCommandRouter::handleUpdateGroupAnnouncement(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString groupId;
    if (!requireString(payload, QStringLiteral("groupId"), &groupId, op, reqId)) {
        return;
    }
    sendBoolAck(op,
                reqId,
                m_bridge->client()->sendServerGroupAnnouncementUpdate(groupId, payload.value(QStringLiteral("announcement")).toString()),
                QStringLiteral("group_announcement_failed"),
                QStringLiteral("Group announcement update requires an active server connection."));
}

void QQNTEngineCommandRouter::handleUpdateGroupMember(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString groupId;
    QString memberId;
    QString action;
    if (!requireString(payload, QStringLiteral("groupId"), &groupId, op, reqId)
        || !requireString(payload, QStringLiteral("memberId"), &memberId, op, reqId)
        || !requireString(payload, QStringLiteral("action"), &action, op, reqId)) {
        return;
    }
    sendBoolAck(op,
                reqId,
                m_bridge->client()->sendServerGroupMemberUpdate(groupId, memberId, action),
                QStringLiteral("group_member_failed"),
                QStringLiteral("Group member update requires an active server connection and valid action."));
}

void QQNTEngineCommandRouter::handleSendFileLike(const QString& op, const QString& reqId, const QJsonObject& payload, bool imageMode) {
    QString filePath;
    if (!requireString(payload, QStringLiteral("filePath"), &filePath, op, reqId)) {
        return;
    }

    const QString groupId = payload.value(QStringLiteral("groupId")).toString().trimmed();
    const QString receiverId = payload.value(QStringLiteral("receiverId")).toString().trimmed();
    if (groupId.isEmpty() && receiverId.isEmpty()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("missing_target"),
                               QStringLiteral("File send requires exactly one of receiverId or groupId."));
        return;
    }
    if (!groupId.isEmpty() && !receiverId.isEmpty()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("ambiguous_target"),
                               QStringLiteral("File send target must not include both receiverId and groupId."));
        return;
    }
    const bool accepted = groupId.isEmpty()
        ? (imageMode ? m_bridge->client()->sendImage(filePath, receiverId) : m_bridge->client()->sendFile(filePath, receiverId))
        : (imageMode ? m_bridge->client()->sendServerGroupImage(groupId, filePath) : m_bridge->client()->sendServerGroupFile(groupId, filePath));
    sendBoolAck(op,
                reqId,
                accepted,
                QStringLiteral("file_send_failed"),
                QStringLiteral("File send requires an active server connection and readable file."));
}

void QQNTEngineCommandRouter::handleCancelTransfer(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString transferId;
    if (!requireString(payload, QStringLiteral("transferId"), &transferId, op, reqId)) {
        return;
    }

    const QString activeTransferId = m_bridge->client()->currentOutgoingTransferId().trimmed();
    if (activeTransferId.isEmpty()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("transfer_not_active"),
                               QStringLiteral("No outgoing file transfer is active."));
        return;
    }

    if (transferId != activeTransferId) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("transfer_mismatch"),
                               QStringLiteral("Requested transferId does not match the active outgoing transfer."));
        return;
    }

    m_bridge->client()->cancelCurrentOutgoingTransfer();
    QJsonObject response;
    response[QStringLiteral("cancelled")] = true;
    response[QStringLiteral("transferId")] = transferId;
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleQueryResume(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString transferId;
    if (!requireString(payload, QStringLiteral("transferId"), &transferId, op, reqId)) {
        return;
    }

    qint64 confirmedBytes = 0;
    qint64 nextChunkIndex = 0;
    qint64 fileSize = 0;
    qint64 chunkSize = 0;
    qint64 chunkCount = 0;
    QString fileHash;
    QVector<qint64> receivedChunks;
    QString rejectReason;
    if (!m_bridge->client()->queryFileTransferResumeState(transferId,
                                                          &confirmedBytes,
                                                          &nextChunkIndex,
                                                          &receivedChunks,
                                                          &rejectReason,
                                                          5000,
                                                          &fileSize,
                                                          &chunkSize,
                                                          &chunkCount,
                                                          &fileHash)) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("resume_query_failed"),
                               rejectReason.isEmpty() ? QStringLiteral("Resume query failed.") : rejectReason);
        return;
    }

    const QString filePath = payload.value(QStringLiteral("filePath")).toString().trimmed();
    if (filePath.isEmpty()) {
        m_bridge->sendAck(op,
                          reqId,
                          makeResumeStatePayload(transferId,
                                                 confirmedBytes,
                                                 nextChunkIndex,
                                                 fileSize,
                                                 chunkSize,
                                                 chunkCount,
                                                 fileHash,
                                                 receivedChunks,
                                                 false));
        return;
    }

    QString resumeRejectReason;
    const QString receiverId = payload.value(QStringLiteral("receiverId")).toString().trimmed();
    const MessageType messageType = resumeMessageTypeFromPayload(payload);
    if (!m_bridge->client()->queryAndResumeFileTransfer(filePath,
                                                        transferId,
                                                        receiverId,
                                                        messageType,
                                                        &resumeRejectReason,
                                                        5000)) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("resume_transfer_failed"),
                               resumeRejectReason.isEmpty() ? QStringLiteral("Resume transfer failed.") : resumeRejectReason);
        return;
    }

    m_bridge->sendAck(op,
                      reqId,
                      makeResumeStatePayload(transferId,
                                             confirmedBytes,
                                             nextChunkIndex,
                                             fileSize,
                                             chunkSize,
                                             chunkCount,
                                             fileHash,
                                             receivedChunks,
                                             true));
}

void QQNTEngineCommandRouter::handleE2EStatus(const QString& op, const QString& reqId, const QJsonObject& payload) {
    const QString peerId = payload.value(QStringLiteral("peerId")).toString().trimmed();
    QJsonObject response;
    response[QStringLiteral("localIdentity")] = m_bridge->client()->e2eLocalIdentityStatus();
    if (!peerId.isEmpty()) {
        response[QStringLiteral("peerId")] = peerId;
        response[QStringLiteral("session")] = m_bridge->client()->e2eSessionStatus(peerId);
        response[QStringLiteral("identity")] = m_bridge->client()->e2ePeerIdentityStatus(peerId);
    }
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleE2EAnnounceIdentity(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString rejectReason;
    sendBoolAck(op,
                reqId,
                m_bridge->client()->announceE2EIdentity(payload.value(QStringLiteral("peerId")).toString(), &rejectReason),
                QStringLiteral("e2e_announce_failed"),
                rejectReason.isEmpty() ? QStringLiteral("E2E identity announce failed.") : rejectReason);
}

void QQNTEngineCommandRouter::handleE2EPinIdentity(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString peerId;
    if (!requireString(payload, QStringLiteral("peerId"), &peerId, op, reqId)) {
        return;
    }
    QString rejectReason;
    sendBoolAck(op,
                reqId,
                m_bridge->client()->pinE2EPeerIdentity(peerId, payload.value(QStringLiteral("fingerprint")).toString(), &rejectReason),
                QStringLiteral("e2e_pin_failed"),
                rejectReason.isEmpty() ? QStringLiteral("E2E identity pin failed.") : rejectReason);
}

void QQNTEngineCommandRouter::handleE2ERequestRotation(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString peerId;
    if (!requireString(payload, QStringLiteral("peerId"), &peerId, op, reqId)) {
        return;
    }
    QString rejectReason;
    sendBoolAck(op,
                reqId,
                m_bridge->client()->requestE2ESessionRotation(peerId, &rejectReason),
                QStringLiteral("e2e_rotation_failed"),
                rejectReason.isEmpty() ? QStringLiteral("E2E session rotation request failed.") : rejectReason);
}

void QQNTEngineCommandRouter::handleProfileUpdate(const QString& op, const QString& reqId, const QJsonObject& payload) {
    const QString userName = payload.value(QStringLiteral("userName")).toString().trimmed();
    const QString avatarBase64 = payload.value(QStringLiteral("avatarBase64")).toString().trimmed();
    if (!userName.isEmpty()) {
        m_bridge->client()->setUserInfo(m_bridge->client()->currentUserId(), userName);
    }

    bool avatarSent = false;
    if (!avatarBase64.isEmpty()) {
        avatarSent = m_bridge->client()->sendAvatarUpdate(QByteArray::fromBase64(avatarBase64.toLatin1()));
        if (!avatarSent && m_bridge->client()->isConnected()) {
            m_bridge->sendErrorAck(op, reqId, QStringLiteral("profile_update_failed"), QStringLiteral("Avatar profile update failed."));
            return;
        }
    }

    QJsonObject response;
    response[QStringLiteral("accepted")] = true;
    response[QStringLiteral("avatarSent")] = avatarSent;
    response[QStringLiteral("userName")] = m_bridge->client()->currentUserName();
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleSettingsSync(const QString& op, const QString& reqId, const QJsonObject& payload) {
    const QJsonValue settingsValue = payload.value(QStringLiteral("settings"));
    if (!settingsValue.isObject()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_settings"),
                               QStringLiteral("payload.settings must be an object."));
        return;
    }

    const QJsonObject settings = settingsValue.toObject();
    QString appliedDownloadDir;
    QString rejectReason;
    if (!applyDownloadDirSetting(settings, &appliedDownloadDir, &rejectReason)) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("settings_apply_failed"),
                               rejectReason.isEmpty() ? QStringLiteral("Settings could not be applied.") : rejectReason);
        return;
    }

    m_settings = settings;
    ++m_settingsRevision;

    QJsonObject response;
    response[QStringLiteral("accepted")] = true;
    response[QStringLiteral("revision")] = m_settingsRevision;
    response[QStringLiteral("settings")] = m_settings;
    if (!appliedDownloadDir.isEmpty()) {
        response[QStringLiteral("appliedDownloadDir")] = appliedDownloadDir;
    }

    m_bridge->sendAck(op, reqId, response);
    m_bridge->sendEvent(QStringLiteral("settings_synced"), response);
}

void QQNTEngineCommandRouter::sendBoolAck(const QString& op,
                                          const QString& reqId,
                                          bool accepted,
                                          const QString& failureCode,
                                          const QString& failureMessage) {
    if (!accepted) {
        m_bridge->sendErrorAck(op, reqId, failureCode, failureMessage);
        return;
    }

    QJsonObject response;
    response[QStringLiteral("accepted")] = true;
    m_bridge->sendAck(op, reqId, response);
}

bool QQNTEngineCommandRouter::requireString(const QJsonObject& payload,
                                            const QString& field,
                                            QString* value,
                                            const QString& op,
                                            const QString& reqId) const {
    const QString text = payload.value(field).toString().trimmed();
    if (text.isEmpty()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("missing_field"),
                               QStringLiteral("payload.%1 is required.").arg(field));
        return false;
    }
    *value = text;
    return true;
}
