#include "qqnt_engine_command_router.h"

#include "qqnt_client_bridge.h"
#include "localfilemanager.h"

#include <QByteArray>
#include <QDateTime>
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

MessageType resumeMessageTypeFromContentType(const QString& contentType) {
    if (contentType == QLatin1String("image")) {
        return MessageType::Image;
    }
    return MessageType::File;
}

bool isSupportedResumeContentType(const QString& contentType) {
    return contentType.trimmed().isEmpty()
        || contentType == QLatin1String("file")
        || contentType == QLatin1String("image");
}

bool optionalSettingsStringField(const QJsonObject& object,
                                 const QString& field,
                                 const QString& label,
                                 QString* value,
                                 QString* rejectReason) {
    const QJsonValue fieldValue = object.value(field);
    if (fieldValue.isUndefined() || fieldValue.isNull()) {
        value->clear();
        return true;
    }
    if (!fieldValue.isString()) {
        if (rejectReason) {
            *rejectReason = QStringLiteral("settings.%1 must be a string when provided.").arg(label);
        }
        return false;
    }

    *value = fieldValue.toString().trimmed();
    return true;
}

bool downloadDirFromSettings(const QJsonObject& settings, QString* requestedDir, QString* rejectReason) {
    requestedDir->clear();

    QString direct;
    if (!optionalSettingsStringField(settings,
                                     QStringLiteral("fileDownloadDir"),
                                     QStringLiteral("fileDownloadDir"),
                                     &direct,
                                     rejectReason)) {
        return false;
    }
    if (!direct.isEmpty()) {
        *requestedDir = direct;
    }

    const QJsonValue filesValue = settings.value(QStringLiteral("files"));
    if (filesValue.isUndefined() || filesValue.isNull()) {
        return true;
    }
    if (!filesValue.isObject()) {
        if (rejectReason) {
            *rejectReason = QStringLiteral("settings.files must be an object when provided.");
        }
        return false;
    }

    const QJsonObject files = filesValue.toObject();
    QString nested;
    if (!optionalSettingsStringField(files,
                                     QStringLiteral("downloadDir"),
                                     QStringLiteral("files.downloadDir"),
                                     &nested,
                                     rejectReason)) {
        return false;
    }
    if (requestedDir->isEmpty() && !nested.isEmpty()) {
        *requestedDir = nested;
    }

    if (!optionalSettingsStringField(files,
                                     QStringLiteral("downloadDirectory"),
                                     QStringLiteral("files.downloadDirectory"),
                                     &nested,
                                     rejectReason)) {
        return false;
    }
    if (requestedDir->isEmpty() && !nested.isEmpty()) {
        *requestedDir = nested;
    }
    return true;
}

bool appendMemberId(QStringList* memberIds, const QString& memberId, QString* rejectReason) {
    const QString normalizedMemberId = memberId.trimmed();
    if (normalizedMemberId.isEmpty()) {
        if (rejectReason) {
            *rejectReason = QStringLiteral("create_group members entries must not be empty.");
        }
        return false;
    }
    if (!memberIds->contains(normalizedMemberId)) {
        *memberIds << normalizedMemberId;
    }
    return true;
}

bool memberIdsFromPayload(const QJsonObject& payload, QStringList* memberIds, QString* rejectReason) {
    memberIds->clear();
    const QJsonArray members = payload.value(QStringLiteral("members")).toArray();
    for (const QJsonValue& memberValue : members) {
        if (memberValue.isString()) {
            if (!appendMemberId(memberIds, memberValue.toString(), rejectReason)) {
                return false;
            }
            continue;
        }

        if (memberValue.isObject()) {
            const QJsonObject memberObject = memberValue.toObject();
            QString memberId;
            const QStringList candidateFields = {
                QStringLiteral("userId"),
                QStringLiteral("account"),
                QStringLiteral("id"),
                QStringLiteral("memberId"),
            };
            for (const QString& candidateField : candidateFields) {
                const QJsonValue candidateValue = memberObject.value(candidateField);
                if (candidateValue.isUndefined() || candidateValue.isNull()) {
                    continue;
                }
                if (!candidateValue.isString()) {
                    if (rejectReason) {
                        *rejectReason = QStringLiteral("create_group member.%1 must be a string when provided.").arg(candidateField);
                    }
                    return false;
                }
                memberId = candidateValue.toString().trimmed();
                break;
            }
            if (!appendMemberId(memberIds, memberId, rejectReason)) {
                return false;
            }
            continue;
        }

        if (rejectReason) {
            *rejectReason = QStringLiteral("create_group members entries must be strings.");
        }
        return false;
    }
    return true;
}

bool normalizeGroupMemberAction(const QString& action, QString* normalizedAction) {
    const QString text = action;
    if (text == QLatin1String("add")
        || text == QLatin1String("remove")
        || text == QLatin1String("promote_admin")
        || text == QLatin1String("demote_admin")) {
        *normalizedAction = text;
        return true;
    }

    normalizedAction->clear();
    return false;
}

bool applyDownloadDirSetting(const QString& requestedDir, QString* appliedDownloadDir, QString* rejectReason) {
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

bool isEmptyPayloadCommand(const QString& op) {
    return op == QLatin1String("ready")
        || op == QLatin1String("disconnect")
        || op == QLatin1String("logout")
        || op == QLatin1String("cancel_account_deactivation")
        || op == QLatin1String("get_user_list")
        || op == QLatin1String("get_friend_list")
        || op == QLatin1String("get_group_list");
}

QJsonObject targetFieldsErrorDetails() {
    QJsonArray targetFields;
    targetFields.append(QStringLiteral("receiverId"));
    targetFields.append(QStringLiteral("groupId"));

    QJsonObject details;
    details[QStringLiteral("targetFields")] = targetFields;
    return details;
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
    const QJsonValue payloadValue = command.value(QStringLiteral("payload"));

    if (op.isEmpty()) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("missing_op"), QStringLiteral("Command op is required."));
        return;
    }
    if (reqId.isEmpty()) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("missing_req_id"), QStringLiteral("Command reqId is required."));
        return;
    }
    if (!payloadValue.isUndefined() && !payloadValue.isObject()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_payload"),
                               QStringLiteral("Command payload must be an object."));
        return;
    }

    const QJsonObject payload = payloadValue.toObject();
    if (isEmptyPayloadCommand(op) && !payload.isEmpty()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_payload"),
                               QStringLiteral("Command payload for %1 must be empty.").arg(op));
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
    } else if (op == QLatin1String("get_user_list")) {
        handleGetUserList(op, reqId);
    } else if (op == QLatin1String("get_friend_list")) {
        handleGetFriendList(op, reqId);
    } else if (op == QLatin1String("get_group_list")) {
        handleGetGroupList(op, reqId);
    } else if (op == QLatin1String("search_friend")) {
        handleSearchFriend(op, reqId, payload);
    } else if (op == QLatin1String("send_friend_request")) {
        handleSendFriendRequest(op, reqId, payload);
    } else if (op == QLatin1String("respond_friend_request")) {
        handleRespondFriendRequest(op, reqId, payload);
    } else if (op == QLatin1String("deactivate_account")) {
        handleAccountDeactivation(op, reqId, payload);
    } else if (op == QLatin1String("cancel_account_deactivation")) {
        handleAccountDeactivationCancel(op, reqId);
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
    } else if (op == QLatin1String("set_group_essence_message")) {
        handleSetGroupEssenceMessage(op, reqId, payload);
    } else if (op == QLatin1String("set_message_favorite")) {
        handleSetMessageFavorite(op, reqId, payload);
    } else if (op == QLatin1String("recall_group_message")) {
        handleRecallGroupMessage(op, reqId, payload);
    } else if (op == QLatin1String("mute_group_member")) {
        handleMuteGroupMember(op, reqId, payload);
    } else if (op == QLatin1String("unmute_group_member")) {
        handleUnmuteGroupMember(op, reqId, payload);
    } else if (op == QLatin1String("get_group_member_profile")) {
        handleGetGroupMemberProfile(op, reqId, payload);
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
    const QStringList allowedFields = registerMode
        ? QStringList{QStringLiteral("account"), QStringLiteral("password"), QStringLiteral("userName")}
        : QStringList{QStringLiteral("account"), QStringLiteral("password")};
    if (!requireOnlyFields(payload, allowedFields, op, reqId)) {
        return;
    }

    QString account;
    QString password;
    if (!requireString(payload, QStringLiteral("account"), &account, op, reqId)
        || !requireString(payload, QStringLiteral("password"), &password, op, reqId)) {
        return;
    }

    if (registerMode) {
        QString userName;
        if (!requireString(payload, QStringLiteral("userName"), &userName, op, reqId)) {
            return;
        }
        m_bridge->client()->setUserInfo(QString(), userName);
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
    if (!requireOnlyFields(payload, {QStringLiteral("host"), QStringLiteral("port")}, op, reqId)) {
        return;
    }

    QString host;
    quint16 port = 0;
    if (!requireString(payload, QStringLiteral("host"), &host, op, reqId)
        || !requireTcpPort(payload, QStringLiteral("port"), &port, op, reqId)) {
        return;
    }

    if (!m_hasAccountInfo) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("login_required"),
                               QStringLiteral("Send login or register credentials before connect."));
        return;
    }

    m_bridge->setConnectionTarget(host, port);
    const bool connected = m_bridge->client()->connectToServer(host, port);
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
    response[QStringLiteral("port")] = static_cast<int>(port);
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleDisconnect(const QString& op, const QString& reqId) {
    m_bridge->client()->disconnectFromServer();
    m_bridge->sendAck(op, reqId);
}

void QQNTEngineCommandRouter::handleSetUserInfo(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("userId"), QStringLiteral("userName")}, op, reqId)) {
        return;
    }

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
    m_bridge->sendAck(op, reqId, m_bridge->userListPayload());
}

void QQNTEngineCommandRouter::handleGetFriendList(const QString& op, const QString& reqId) {
    m_bridge->sendAck(op, reqId, m_bridge->friendListPayload());
}

void QQNTEngineCommandRouter::handleGetGroupList(const QString& op, const QString& reqId) {
    m_bridge->sendAck(op, reqId, m_bridge->groupListPayload());
}

void QQNTEngineCommandRouter::handleSearchFriend(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("account")}, op, reqId)) {
        return;
    }

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
    if (!requireOnlyFields(payload, {QStringLiteral("receiverId")}, op, reqId)) {
        return;
    }

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
    if (!requireOnlyFields(payload, {QStringLiteral("senderId"), QStringLiteral("accepted")}, op, reqId)) {
        return;
    }

    QString senderId;
    bool accepted = false;
    if (!requireString(payload, QStringLiteral("senderId"), &senderId, op, reqId)
        || !requireBool(payload, QStringLiteral("accepted"), &accepted, op, reqId)) {
        return;
    }
    sendBoolAck(op,
                reqId,
                m_bridge->client()->sendFriendResponse(senderId, accepted),
                QStringLiteral("friend_response_failed"),
                QStringLiteral("Friend response requires an active server connection."));
}

void QQNTEngineCommandRouter::handleAccountDeactivation(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("reason")}, op, reqId)) {
        return;
    }

    QString reason;
    if (!optionalStringField(payload, QStringLiteral("reason"), &reason, QStringLiteral("invalid_reason"), op, reqId)) {
        return;
    }

    sendBoolAck(op,
                reqId,
                m_bridge->client()->requestAccountDeactivation(reason),
                QStringLiteral("account_deactivation_failed"),
                QStringLiteral("Account deactivation requires an active server connection."));
}

void QQNTEngineCommandRouter::handleAccountDeactivationCancel(const QString& op, const QString& reqId) {
    sendBoolAck(op,
                reqId,
                m_bridge->client()->cancelAccountDeactivation(),
                QStringLiteral("account_deactivation_cancel_failed"),
                QStringLiteral("Cancelling account deactivation requires an active server connection."));
}

void QQNTEngineCommandRouter::handleSendPrivateMessage(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("receiverId"), QStringLiteral("content"), QStringLiteral("clientMessageId")}, op, reqId)) {
        return;
    }

    QString receiverId;
    QString content;
    QString clientMessageId;
    if (!requireString(payload, QStringLiteral("receiverId"), &receiverId, op, reqId)
        || !requireString(payload, QStringLiteral("content"), &content, op, reqId)
        || !optionalStringField(payload, QStringLiteral("clientMessageId"), &clientMessageId, QStringLiteral("invalid_client_message_id"), op, reqId)) {
        return;
    }

    if (!m_bridge->client()->isConnected()) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("not_connected"), QStringLiteral("Engine is not connected to QQNTServer."));
        return;
    }

    if (!m_bridge->client()->sendPrivateMessage(receiverId, content, clientMessageId)) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("send_failed"), QStringLiteral("Client rejected private message send."));
        return;
    }

    QJsonObject response;
    response[QStringLiteral("receiverId")] = receiverId;
    if (!clientMessageId.trimmed().isEmpty()) {
        response[QStringLiteral("clientMessageId")] = clientMessageId.trimmed();
    }
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleSendGroupMessage(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("groupId"), QStringLiteral("content"), QStringLiteral("clientMessageId")}, op, reqId)) {
        return;
    }

    QString groupId;
    QString content;
    QString clientMessageId;
    if (!requireString(payload, QStringLiteral("groupId"), &groupId, op, reqId)
        || !requireString(payload, QStringLiteral("content"), &content, op, reqId)
        || !optionalStringField(payload, QStringLiteral("clientMessageId"), &clientMessageId, QStringLiteral("invalid_client_message_id"), op, reqId)) {
        return;
    }
    if (!m_bridge->client()->sendServerGroupMessage(groupId, content, clientMessageId)) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("send_group_failed"),
                               QStringLiteral("Group message requires an active server connection and valid group."));
        return;
    }

    QJsonObject response;
    response[QStringLiteral("accepted")] = true;
    response[QStringLiteral("groupId")] = groupId;
    if (!clientMessageId.trimmed().isEmpty()) {
        response[QStringLiteral("clientMessageId")] = clientMessageId.trimmed();
    }
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleCreateGroup(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload,
                           {QStringLiteral("groupName"), QStringLiteral("members"), QStringLiteral("announcement")},
                           op,
                           reqId)) {
        return;
    }

    QString groupName;
    if (!requireString(payload, QStringLiteral("groupName"), &groupName, op, reqId)) {
        return;
    }
    const QJsonValue membersValue = payload.value(QStringLiteral("members"));
    if (!membersValue.isUndefined() && !membersValue.isArray()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_members"),
                               QStringLiteral("create_group members must be an array when provided."));
        return;
    }
    QString announcement;
    if (!optionalStringField(payload,
                             QStringLiteral("announcement"),
                             &announcement,
                             QStringLiteral("invalid_announcement"),
                             op,
                             reqId)) {
        return;
    }
    QStringList memberIds;
    QString membersRejectReason;
    if (!memberIdsFromPayload(payload, &memberIds, &membersRejectReason)) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_members"),
                               membersRejectReason.isEmpty() ? QStringLiteral("create_group members entries are invalid.") : membersRejectReason);
        return;
    }
    sendBoolAck(op,
                reqId,
                m_bridge->client()->createPrivateServerGroup(groupName,
                                                            announcement,
                                                            memberIds),
                QStringLiteral("create_group_failed"),
                QStringLiteral("Group creation requires an active server connection."));
}

void QQNTEngineCommandRouter::handleUpdateGroupAnnouncement(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("groupId"), QStringLiteral("announcement")}, op, reqId)) {
        return;
    }

    QString groupId;
    QString announcement;
    if (!requireString(payload, QStringLiteral("groupId"), &groupId, op, reqId)
        || !requireStringField(payload, QStringLiteral("announcement"), &announcement, op, reqId)) {
        return;
    }
    sendBoolAck(op,
                reqId,
                m_bridge->client()->sendServerGroupAnnouncementUpdate(groupId, announcement),
                QStringLiteral("group_announcement_failed"),
                QStringLiteral("Group announcement update requires an active server connection."));
}

void QQNTEngineCommandRouter::handleUpdateGroupMember(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("groupId"), QStringLiteral("memberId"), QStringLiteral("action")}, op, reqId)) {
        return;
    }

    QString groupId;
    QString memberId;
    if (!requireString(payload, QStringLiteral("groupId"), &groupId, op, reqId)
        || !requireString(payload, QStringLiteral("memberId"), &memberId, op, reqId)) {
        return;
    }

    QString action;
    if (!requireStringField(payload, QStringLiteral("action"), &action, op, reqId)) {
        return;
    }
    if (action.trimmed().isEmpty()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("missing_field"),
                               QStringLiteral("payload.action is required."));
        return;
    }
    if (!normalizeGroupMemberAction(action, &action)) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_action"),
                               QStringLiteral("update_group_member action must be add, remove, promote_admin, or demote_admin."));
        return;
    }
    sendBoolAck(op,
                reqId,
                m_bridge->client()->sendServerGroupMemberUpdate(groupId, memberId, action),
                QStringLiteral("group_member_failed"),
                QStringLiteral("Group member update requires an active server connection and valid action."));
}

void QQNTEngineCommandRouter::handleSetGroupEssenceMessage(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("groupId"), QStringLiteral("messageId"), QStringLiteral("enabled")}, op, reqId)) {
        return;
    }
    QString groupId;
    QString messageId;
    bool enabled = false;
    if (!requireString(payload, QStringLiteral("groupId"), &groupId, op, reqId)
        || !requireString(payload, QStringLiteral("messageId"), &messageId, op, reqId)
        || !requireBool(payload, QStringLiteral("enabled"), &enabled, op, reqId)) {
        return;
    }
    if (!m_bridge->client()->sendServerGroupEssenceUpdate(groupId, messageId, enabled)) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("group_essence_failed"), QStringLiteral("Group essence update requires an active server connection."));
        return;
    }
    QJsonObject response;
    response[QStringLiteral("accepted")] = true;
    response[QStringLiteral("groupId")] = groupId;
    response[QStringLiteral("messageId")] = messageId;
    response[QStringLiteral("enabled")] = enabled;
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleSetMessageFavorite(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("sessionId"), QStringLiteral("messageId"), QStringLiteral("favorite"), QStringLiteral("message")}, op, reqId)) {
        return;
    }

    QString sessionId;
    QString messageId;
    bool favorite = false;
    if (!requireString(payload, QStringLiteral("sessionId"), &sessionId, op, reqId)
        || !requireString(payload, QStringLiteral("messageId"), &messageId, op, reqId)
        || !requireBool(payload, QStringLiteral("favorite"), &favorite, op, reqId)) {
        return;
    }
    const QJsonValue messageValue = payload.value(QStringLiteral("message"));
    if (!messageValue.isUndefined() && !messageValue.isNull() && !messageValue.isObject()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_message"),
                               QStringLiteral("payload.message must be an object when provided."));
        return;
    }

    const bool accepted = m_bridge->client()->sendMessageFavoriteUpdate(sessionId, messageId, favorite, messageValue.toObject());
    if (!accepted) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("favorite_failed"),
                               QStringLiteral("Message favorite update requires an active server connection."));
        return;
    }

    QJsonObject response;
    response[QStringLiteral("accepted")] = true;
    response[QStringLiteral("sessionId")] = sessionId;
    response[QStringLiteral("messageId")] = messageId;
    response[QStringLiteral("favorite")] = favorite;
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleRecallGroupMessage(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("groupId"), QStringLiteral("messageId")}, op, reqId)) {
        return;
    }
    QString groupId;
    QString messageId;
    if (!requireString(payload, QStringLiteral("groupId"), &groupId, op, reqId)
        || !requireString(payload, QStringLiteral("messageId"), &messageId, op, reqId)) {
        return;
    }
    if (!m_bridge->client()->sendServerGroupMessageRecall(groupId, messageId)) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("group_recall_failed"), QStringLiteral("Group recall requires an active server connection."));
        return;
    }
    QJsonObject response;
    response[QStringLiteral("accepted")] = true;
    response[QStringLiteral("groupId")] = groupId;
    response[QStringLiteral("messageId")] = messageId;
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleMuteGroupMember(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("groupId"), QStringLiteral("memberId"), QStringLiteral("mutedUntil"), QStringLiteral("reason")}, op, reqId)) {
        return;
    }
    QString groupId;
    QString memberId;
    QString reason;
    if (!requireString(payload, QStringLiteral("groupId"), &groupId, op, reqId)
        || !requireString(payload, QStringLiteral("memberId"), &memberId, op, reqId)
        || !optionalStringField(payload, QStringLiteral("reason"), &reason, QStringLiteral("invalid_reason"), op, reqId)) {
        return;
    }
    const QJsonValue mutedUntilValue = payload.value(QStringLiteral("mutedUntil"));
    const qint64 mutedUntil = mutedUntilValue.isString()
        ? mutedUntilValue.toString().toLongLong()
        : static_cast<qint64>(mutedUntilValue.toDouble(-1));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 maxMutedUntil = now + 30LL * 24 * 60 * 60 * 1000;
    if (mutedUntil <= now || mutedUntil > maxMutedUntil) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_muted_until"),
                               QStringLiteral("payload.mutedUntil must be a future millisecond timestamp within 30 days."));
        return;
    }
    if (!m_bridge->client()->sendServerGroupMemberMute(groupId, memberId, mutedUntil, reason)) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("group_mute_failed"), QStringLiteral("Group mute requires an active server connection."));
        return;
    }
    QJsonObject response;
    response[QStringLiteral("accepted")] = true;
    response[QStringLiteral("groupId")] = groupId;
    response[QStringLiteral("memberId")] = memberId;
    response[QStringLiteral("mutedUntil")] = QString::number(mutedUntil);
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleUnmuteGroupMember(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("groupId"), QStringLiteral("memberId")}, op, reqId)) {
        return;
    }
    QString groupId;
    QString memberId;
    if (!requireString(payload, QStringLiteral("groupId"), &groupId, op, reqId)
        || !requireString(payload, QStringLiteral("memberId"), &memberId, op, reqId)) {
        return;
    }
    if (!m_bridge->client()->sendServerGroupMemberUnmute(groupId, memberId)) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("group_unmute_failed"), QStringLiteral("Group unmute requires an active server connection."));
        return;
    }
    QJsonObject response;
    response[QStringLiteral("accepted")] = true;
    response[QStringLiteral("groupId")] = groupId;
    response[QStringLiteral("memberId")] = memberId;
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleGetGroupMemberProfile(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("groupId"), QStringLiteral("memberId")}, op, reqId)) {
        return;
    }
    QString groupId;
    QString memberId;
    if (!requireString(payload, QStringLiteral("groupId"), &groupId, op, reqId)
        || !requireString(payload, QStringLiteral("memberId"), &memberId, op, reqId)) {
        return;
    }
    if (!m_bridge->client()->requestServerGroupMemberProfile(groupId, memberId)) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("group_profile_failed"), QStringLiteral("Group member profile requires an active server connection."));
        return;
    }
    QJsonObject response;
    response[QStringLiteral("accepted")] = true;
    response[QStringLiteral("groupId")] = groupId;
    response[QStringLiteral("memberId")] = memberId;
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleSendFileLike(const QString& op, const QString& reqId, const QJsonObject& payload, bool imageMode) {
    if (!requireOnlyFields(payload, {QStringLiteral("filePath"), QStringLiteral("receiverId"), QStringLiteral("groupId")}, op, reqId)) {
        return;
    }

    QString filePath;
    if (!requireString(payload, QStringLiteral("filePath"), &filePath, op, reqId)) {
        return;
    }

    QString groupId;
    QString receiverId;
    if (!optionalTargetString(payload, QStringLiteral("groupId"), &groupId, op, reqId)
        || !optionalTargetString(payload, QStringLiteral("receiverId"), &receiverId, op, reqId)) {
        return;
    }
    if (groupId.isEmpty() && receiverId.isEmpty()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("missing_target"),
                               QStringLiteral("File send requires exactly one of receiverId or groupId."),
                               QStringLiteral("engine"),
                               targetFieldsErrorDetails());
        return;
    }
    if (!groupId.isEmpty() && !receiverId.isEmpty()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("ambiguous_target"),
                               QStringLiteral("File send target must not include both receiverId and groupId."),
                               QStringLiteral("engine"),
                               targetFieldsErrorDetails());
        return;
    }
    const bool accepted = groupId.isEmpty()
        ? (imageMode ? m_bridge->client()->sendImage(filePath, receiverId) : m_bridge->client()->sendFile(filePath, receiverId))
        : (imageMode ? m_bridge->client()->sendServerGroupImage(groupId, filePath) : m_bridge->client()->sendServerGroupFile(groupId, filePath));
    if (!accepted) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("file_send_failed"),
                               QStringLiteral("File send requires an active server connection and readable file."));
        return;
    }

    const QString transferId = m_bridge->client()->lastOutgoingTransferId().trimmed();
    if (transferId.isEmpty()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("file_send_failed"),
                               QStringLiteral("File send did not create a transfer id."));
        return;
    }

    QJsonObject response;
    response[QStringLiteral("accepted")] = true;
    response[QStringLiteral("transferId")] = transferId;
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleCancelTransfer(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("transferId")}, op, reqId)) {
        return;
    }

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
    if (!requireOnlyFields(payload,
                           {QStringLiteral("transferId"),
                            QStringLiteral("filePath"),
                            QStringLiteral("receiverId"),
                            QStringLiteral("groupId"),
                            QStringLiteral("contentType")},
                           op,
                           reqId)) {
        return;
    }

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
    QString filePath;
    QString contentType;
    if (!optionalStringField(payload,
                             QStringLiteral("filePath"),
                             &filePath,
                             QStringLiteral("invalid_file_path"),
                             op,
                             reqId)
        || !optionalStringField(payload,
                                QStringLiteral("contentType"),
                                &contentType,
                                QStringLiteral("invalid_content_type"),
                                op,
                                reqId)) {
        return;
    }
    if (!isSupportedResumeContentType(contentType)) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_content_type"),
                               QStringLiteral("query_resume contentType must be file or image when provided."));
        return;
    }
    filePath = filePath.trimmed();
    QString groupId;
    QString receiverId;
    if (!optionalTargetString(payload, QStringLiteral("groupId"), &groupId, op, reqId)
        || !optionalTargetString(payload, QStringLiteral("receiverId"), &receiverId, op, reqId)) {
        return;
    }
    if (filePath.isEmpty() && (!groupId.isEmpty() || !receiverId.isEmpty())) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_target"),
                               QStringLiteral("query_resume receiverId/groupId may only be provided with filePath."),
                               QStringLiteral("engine"),
                               targetFieldsErrorDetails());
        return;
    }
    if (!filePath.isEmpty() && groupId.isEmpty() && receiverId.isEmpty()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("missing_target"),
                               QStringLiteral("Resume transfer requires exactly one of receiverId or groupId."),
                               QStringLiteral("engine"),
                               targetFieldsErrorDetails());
        return;
    }
    if (!filePath.isEmpty() && !groupId.isEmpty() && !receiverId.isEmpty()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("ambiguous_target"),
                               QStringLiteral("Resume transfer target must not include both receiverId and groupId."),
                               QStringLiteral("engine"),
                               targetFieldsErrorDetails());
        return;
    }
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
    const MessageType messageType = resumeMessageTypeFromContentType(contentType);
    if (!m_bridge->client()->queryAndResumeFileTransfer(filePath,
                                                        transferId,
                                                        receiverId,
                                                        messageType,
                                                        &resumeRejectReason,
                                                        5000,
                                                        groupId)) {
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
    if (!requireOnlyFields(payload, {QStringLiteral("peerId")}, op, reqId)) {
        return;
    }

    QString peerId;
    if (!optionalStringField(payload,
                             QStringLiteral("peerId"),
                             &peerId,
                             QStringLiteral("invalid_peer_id"),
                             op,
                             reqId)) {
        return;
    }
    peerId = peerId.trimmed();
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
    if (!requireOnlyFields(payload, {QStringLiteral("peerId")}, op, reqId)) {
        return;
    }

    QString peerId;
    if (!requireString(payload, QStringLiteral("peerId"), &peerId, op, reqId)) {
        return;
    }
    QString rejectReason;
    sendBoolAck(op,
                reqId,
                m_bridge->client()->announceE2EIdentity(peerId, &rejectReason),
                QStringLiteral("e2e_announce_failed"),
                rejectReason.isEmpty() ? QStringLiteral("E2E identity announce failed.") : rejectReason);
}

void QQNTEngineCommandRouter::handleE2EPinIdentity(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("peerId"), QStringLiteral("fingerprint")}, op, reqId)) {
        return;
    }

    QString peerId;
    if (!requireString(payload, QStringLiteral("peerId"), &peerId, op, reqId)) {
        return;
    }
    QString fingerprint;
    if (!optionalStringField(payload,
                             QStringLiteral("fingerprint"),
                             &fingerprint,
                             QStringLiteral("invalid_fingerprint"),
                             op,
                             reqId)) {
        return;
    }
    QString rejectReason;
    sendBoolAck(op,
                reqId,
                m_bridge->client()->pinE2EPeerIdentity(peerId, fingerprint, &rejectReason),
                QStringLiteral("e2e_pin_failed"),
                rejectReason.isEmpty() ? QStringLiteral("E2E identity pin failed.") : rejectReason);
}

void QQNTEngineCommandRouter::handleE2ERequestRotation(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!requireOnlyFields(payload, {QStringLiteral("peerId")}, op, reqId)) {
        return;
    }

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
    if (!requireOnlyFields(payload, {QStringLiteral("userName"), QStringLiteral("avatarBase64")}, op, reqId)) {
        return;
    }

    QString userName;
    QString avatarBase64;
    if (!optionalStringField(payload,
                             QStringLiteral("userName"),
                             &userName,
                             QStringLiteral("invalid_profile_field"),
                             op,
                             reqId)
        || !optionalStringField(payload,
                                QStringLiteral("avatarBase64"),
                                &avatarBase64,
                                QStringLiteral("invalid_profile_field"),
                                op,
                                reqId)) {
        return;
    }
    userName = userName.trimmed();
    avatarBase64 = avatarBase64.trimmed();
    if (!userName.isEmpty()) {
        m_bridge->client()->setUserInfo(m_bridge->client()->currentUserId(), userName);
    }

    bool avatarSent = false;
    if (!avatarBase64.isEmpty()) {
        const QByteArray::FromBase64Result avatarBytes =
            QByteArray::fromBase64Encoding(avatarBase64.toLatin1(),
                                           QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
        if (!avatarBytes) {
            m_bridge->sendErrorAck(op,
                                   reqId,
                                   QStringLiteral("invalid_profile_field"),
                                   QStringLiteral("payload.avatarBase64 must be valid Base64 when provided."));
            return;
        }

        avatarSent = m_bridge->client()->sendAvatarUpdate(avatarBytes.decoded);
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
    if (!requireOnlyFields(payload, {QStringLiteral("settings")}, op, reqId)) {
        return;
    }

    const QJsonValue settingsValue = payload.value(QStringLiteral("settings"));
    if (!settingsValue.isObject()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_settings"),
                               QStringLiteral("payload.settings must be an object."));
        return;
    }

    const QJsonObject settings = settingsValue.toObject();
    QString requestedDownloadDir;
    QString appliedDownloadDir;
    QString rejectReason;
    if (!downloadDirFromSettings(settings, &requestedDownloadDir, &rejectReason)) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_settings"),
                               rejectReason.isEmpty() ? QStringLiteral("Settings fields are invalid.") : rejectReason);
        return;
    }
    if (!applyDownloadDirSetting(requestedDownloadDir, &appliedDownloadDir, &rejectReason)) {
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

bool QQNTEngineCommandRouter::requireStringField(const QJsonObject& payload,
                                                 const QString& field,
                                                 QString* value,
                                                 const QString& op,
                                                 const QString& reqId) const {
    const QJsonValue fieldValue = payload.value(field);
    if (!fieldValue.isString()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("missing_field"),
                               QStringLiteral("payload.%1 is required.").arg(field));
        return false;
    }
    *value = fieldValue.toString();
    return true;
}

bool QQNTEngineCommandRouter::requireBool(const QJsonObject& payload,
                                          const QString& field,
                                          bool* value,
                                          const QString& op,
                                          const QString& reqId) const {
    const QJsonValue fieldValue = payload.value(field);
    if (!fieldValue.isBool()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("missing_field"),
                               QStringLiteral("payload.%1 is required.").arg(field));
        return false;
    }
    *value = fieldValue.toBool();
    return true;
}

bool QQNTEngineCommandRouter::requireTcpPort(const QJsonObject& payload,
                                             const QString& field,
                                             quint16* value,
                                             const QString& op,
                                             const QString& reqId) const {
    const QJsonValue fieldValue = payload.value(field);
    if (!fieldValue.isDouble()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("missing_field"),
                               QStringLiteral("payload.%1 is required.").arg(field));
        return false;
    }

    const int port = fieldValue.toInt(-1);
    if (port <= 0 || port > 65535 || fieldValue.toDouble(-1.0) != static_cast<double>(port)) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_target"),
                               QStringLiteral("connect requires a valid host and port."));
        return false;
    }

    *value = static_cast<quint16>(port);
    return true;
}

bool QQNTEngineCommandRouter::optionalStringField(const QJsonObject& payload,
                                                  const QString& field,
                                                  QString* value,
                                                  const QString& errorCode,
                                                  const QString& op,
                                                  const QString& reqId) const {
    const QJsonValue fieldValue = payload.value(field);
    if (fieldValue.isUndefined() || fieldValue.isNull()) {
        value->clear();
        return true;
    }
    if (!fieldValue.isString()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               errorCode,
                               QStringLiteral("payload.%1 must be a string when provided.").arg(field));
        return false;
    }

    *value = fieldValue.toString();
    return true;
}

bool QQNTEngineCommandRouter::optionalTargetString(const QJsonObject& payload,
                                                   const QString& field,
                                                   QString* value,
                                                   const QString& op,
                                                   const QString& reqId) const {
    if (!optionalStringField(payload, field, value, QStringLiteral("invalid_target"), op, reqId)) {
        return false;
    }

    *value = value->trimmed();
    return true;
}

bool QQNTEngineCommandRouter::requireOnlyFields(const QJsonObject& payload,
                                                const QStringList& allowedFields,
                                                const QString& op,
                                                const QString& reqId) const {
    for (const QString& field : payload.keys()) {
        if (allowedFields.contains(field)) {
            continue;
        }

        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("invalid_payload"),
                               QStringLiteral("Command payload for %1 contains unsupported field: %2.").arg(op, field));
        return false;
    }
    return true;
}
