#include "server.h"
#include "server_group_support.h"
#include "objectstore.h"
#include "qqnt_redis_service.h"
#include "redisclient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QCoreApplication>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QDataStream>
#include <QJsonArray>
#include <QStandardPaths>
#include <QStringList>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QCryptographicHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QRandomGenerator>
#include <QMessageAuthenticationCode>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QPointer>
#include <QPair>
#include <QThread>
#include <QUuid>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <algorithm>
#include <limits>
#include <memory>

void Server::handleServerGroupCreate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) {
        ServerGroupSupport::sendNotice(socket, "创建群组失败：请先登录");
        return;
    }

    QString groupName = obj.value("groupName").toString().trimmed();
    QString announcement = obj.value("announcement").toString().trimmed();
    if (groupName.isEmpty()) {
        ServerGroupSupport::sendNotice(socket, "创建群组失败：群名称不能为空");
        return;
    }
    if (groupName.size() > 80) {
        groupName = groupName.left(80);
    }
    if (announcement.isEmpty()) {
        announcement = QStringLiteral("私有群已创建。");
    }
    if (announcement.size() > 1000) {
        announcement = announcement.left(1000);
    }
    if (!ensureAccountDatabase()) {
        ServerGroupSupport::sendNotice(socket, "创建群组失败：服务端群组存储不可用");
        return;
    }

    const QStringList requestedMemberIds = ServerGroupSupport::initialMemberIds(obj, requester->id);
    QStringList memberIdsForSnapshot{requester->id};
    QStringList addedInitialMemberIds;
    QJsonArray addedInitialMembers;
    QJsonArray skippedInitialMemberIds;
    const QString groupId = QStringLiteral("private-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool created = false;
    QString errorText;
    const QString connectionName = "server_group_create_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (!ServerGroupSupport::openConnection(db, connectionName)) {
            errorText = "创建群组失败：无法打开群组数据库";
        } else if (!db.transaction()) {
            errorText = "创建群组失败：无法开启事务";
        } else {
            QSqlQuery groupQuery(db);
            groupQuery.prepare("INSERT INTO server_groups(group_id, group_name, owner_id, announcement, group_type, history_policy, file_policy, created_at, updated_at) "
                               "VALUES(?, ?, ?, ?, 'private', 'member-and-removed-readonly', 'members-only', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
            groupQuery.addBindValue(groupId);
            groupQuery.addBindValue(groupName);
            groupQuery.addBindValue(requester->id);
            groupQuery.addBindValue(announcement);
            if (!groupQuery.exec()) {
                errorText = "创建群组失败：保存群组失败";
            }

            if (errorText.isEmpty()) {
                QSqlQuery memberQuery(db);
                memberQuery.prepare("INSERT INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) "
                                    "VALUES(?, ?, ?, 'owner', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
                memberQuery.addBindValue(groupId);
                memberQuery.addBindValue(requester->id);
                memberQuery.addBindValue(requester->name);
                if (!memberQuery.exec()) {
                    errorText = "创建群组失败：保存群主成员失败";
                }
            }

            if (errorText.isEmpty()) {
                for (const QString& memberId : requestedMemberIds) {
                    QSqlQuery accountQuery(db);
                    accountQuery.prepare("SELECT COALESCE(user_name, '') FROM accounts WHERE account = ?");
                    accountQuery.addBindValue(memberId);
                    if (!accountQuery.exec()) {
                        errorText = "创建群组失败：查询初始成员失败";
                        break;
                    }
                    if (!accountQuery.next()) {
                        skippedInitialMemberIds.append(memberId);
                        continue;
                    }

                    const QString memberName = accountQuery.value(0).toString().trimmed().isEmpty()
                        ? memberId
                        : accountQuery.value(0).toString().trimmed();
                    QSqlQuery initialMemberQuery(db);
                    initialMemberQuery.prepare("INSERT INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) "
                                               "VALUES(?, ?, ?, 'member', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
                    initialMemberQuery.addBindValue(groupId);
                    initialMemberQuery.addBindValue(memberId);
                    initialMemberQuery.addBindValue(memberName);
                    if (!initialMemberQuery.exec()) {
                        errorText = "创建群组失败：保存初始成员失败";
                        break;
                    }

                    memberIdsForSnapshot << memberId;
                    addedInitialMemberIds << memberId;
                    QJsonObject addedMember;
                    addedMember[QStringLiteral("userId")] = memberId;
                    addedMember[QStringLiteral("userName")] = memberName;
                    addedInitialMembers.append(addedMember);
                }
            }

            if (errorText.isEmpty() && db.commit()) {
                created = true;
            } else {
                if (errorText.isEmpty()) {
                    errorText = "创建群组失败：提交事务失败";
                }
                db.rollback();
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);

    if (!created) {
        ServerGroupSupport::sendNotice(socket, errorText.isEmpty() ? "创建群组失败" : errorText);
        return;
    }

    QJsonObject auditDetails{
        {QStringLiteral("groupType"), QStringLiteral("private")},
        {QStringLiteral("historyPolicy"), QStringLiteral("member-and-removed-readonly")},
        {QStringLiteral("filePolicy"), QStringLiteral("members-only")},
        {QStringLiteral("initialMemberCount"), addedInitialMembers.size()}
    };
    if (!addedInitialMembers.isEmpty()) {
        auditDetails[QStringLiteral("initialMembers")] = addedInitialMembers;
    }
    if (!skippedInitialMemberIds.isEmpty()) {
        auditDetails[QStringLiteral("skippedInitialMemberIds")] = skippedInitialMemberIds;
    }
    recordServerGroupAuditEvent(groupId,
                                QStringLiteral("create_private_group"),
                                requester->id,
                                requester->name,
                                requester->id,
                                requester->name,
                                auditDetails);
    ServerGroupSupport::sendNotice(socket, QString("私有群 %1 已创建").arg(groupName));
    const QString initialMemberNotice = QString("你已被加入私有群 %1").arg(groupName);
    for (const QString& memberId : memberIdsForSnapshot) {
        QTcpSocket* memberSocket = memberId == requester->id ? socket : m_userSockets.value(memberId);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        if (memberId != requester->id) {
            ServerGroupSupport::sendNotice(memberSocket, initialMemberNotice);
        }
        sendServerGroupSnapshot(memberId, memberSocket);
    }
    publishRedisServerGroupSnapshotRefresh(addedInitialMemberIds, groupId, initialMemberNotice);
}

void Server::handleServerGroupMessage(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* sender = findUserBySocket(socket);
    if (!sender) {
        ServerGroupSupport::sendNotice(socket, "群消息发送失败：请先登录");
        return;
    }
    const QString groupId = ServerGroupSupport::normalizeGroupId(obj.value("groupId").toString());
    const QString content = obj.value("content").toString();
    if (groupId.isEmpty() || content.trimmed().isEmpty()) {
        ServerGroupSupport::sendNotice(socket, "群消息发送失败：请求参数无效");
        return;
    }
    if (groupId == QLatin1String("public")) {
        ensurePublicGroupMembership(sender->id, socket);
    }
    if (!isServerGroupMember(groupId, sender->id)) {
        ServerGroupSupport::sendNotice(socket, groupId == QLatin1String("public")
            ? QStringLiteral("公共群消息发送失败：你已不在该群组，请联系群主或管理员重新邀请。")
            : QStringLiteral("私有群消息发送失败：你不在该群组或已被移出。"));
        return;
    }
    bool allMuted = false;
    QString speakingRule;
    qint64 joinedAtMs = 0;
    const QString settingsConnection = "server_group_message_settings_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(settingsConnection);
        if (ServerGroupSupport::openConnection(db, settingsConnection)) {
            QSqlQuery groupSettings(db);
            groupSettings.prepare("SELECT COALESCE(g.all_muted, 0), COALESCE(g.speaking_rule, 'unrestricted'), "
                                  "COALESCE(m.joined_at, '') FROM server_groups g JOIN server_group_members m ON m.group_id = g.group_id "
                                  "WHERE g.group_id = ? AND m.user_id = ?");
            groupSettings.addBindValue(groupId);
            groupSettings.addBindValue(sender->id);
            if (groupSettings.exec() && groupSettings.next()) {
                allMuted = groupSettings.value(0).toInt() != 0;
                speakingRule = groupSettings.value(1).toString();
                const QString joinedAtText = groupSettings.value(2).toString();
                QDateTime joinedAt = QDateTime::fromString(joinedAtText, Qt::ISODate);
                if (!joinedAt.isValid()) joinedAt = QDateTime::fromString(joinedAtText, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                joinedAtMs = joinedAt.isValid() ? joinedAt.toMSecsSinceEpoch() : 0;
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(settingsConnection);
    const bool manager = [&]() {
        const QString ownerId = [&]() {
            QString value;
            const QString ownerConnection = "server_group_message_owner_" + QString::number(reinterpret_cast<quintptr>(socket));
            QSqlDatabase db = ServerGroupSupport::openDatabase(ownerConnection);
            if (ServerGroupSupport::openConnection(db, ownerConnection)) {
                QSqlQuery ownerQuery(db);
                ownerQuery.prepare("SELECT COALESCE(g.owner_id, ''), COALESCE(m.role, '') FROM server_groups g "
                                   "JOIN server_group_members m ON m.group_id = g.group_id WHERE g.group_id = ? AND m.user_id = ?");
                ownerQuery.addBindValue(groupId); ownerQuery.addBindValue(sender->id);
                if (ownerQuery.exec() && ownerQuery.next()) {
                    if (ownerQuery.value(0).toString() == sender->id || ownerQuery.value(1).toString().toLower() == QLatin1String("admin")
                        || ownerQuery.value(1).toString().toLower() == QLatin1String("owner")) value = sender->id;
                }
                db.close();
            }
            ServerGroupSupport::releaseDatabase(ownerConnection);
            return value;
        }();
        return !ownerId.isEmpty();
    }();
    if ((allMuted && !manager)
        || (speakingRule == QLatin1String("new_members_24h") && !manager && joinedAtMs > 0
            && QDateTime::currentMSecsSinceEpoch() - joinedAtMs < 24LL * 60 * 60 * 1000)) {
        ServerGroupSupport::sendNotice(socket, allMuted ? QStringLiteral("群消息发送失败：当前群已开启全员禁言")
                                          : QStringLiteral("群消息发送失败：新成员需在入群 24 小时后发言"));
        return;
    }
    const int perMinuteLimit = speakingRule == QLatin1String("per_minute_10") ? 10
        : (speakingRule == QLatin1String("per_minute_5") ? 5 : 0);
    if (!manager && perMinuteLimit > 0 && !ServerGroupSupport::messageRateAllowed(groupId, sender->id, perMinuteLimit)) {
        ServerGroupSupport::sendNotice(socket, QStringLiteral("群消息发送失败：当前群每分钟最多发送 %1 条消息").arg(perMinuteLimit));
        return;
    }
    qint64 mutedUntil = 0;
    if (isServerGroupMemberMuted(groupId, sender->id, &mutedUntil)) {
        QJsonObject notice;
        notice["type"] = "error";
        notice["code"] = "group_member_muted";
        notice["message"] = "群消息发送失败：你已被禁言";
        notice["groupId"] = groupId;
        notice["mutedUntil"] = QString::number(mutedUntil);
        socket->write(QJsonDocument(notice).toJson(QJsonDocument::Compact));
        socket->write("\n");
        socket->flush();
        return;
    }

    struct GroupMessageRecipient {
        QString userId;
        QString receiveMode;
    };
    QList<GroupMessageRecipient> recipients;
    const QString connectionName = "server_group_message_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (ServerGroupSupport::openConnection(db, connectionName)) {
            QSqlQuery memberQuery(db);
            memberQuery.prepare("SELECT m.user_id, COALESCE(s.receive_mode, 'receive_quiet') "
                                "FROM server_group_members m LEFT JOIN server_group_user_settings s "
                                "ON s.group_id = m.group_id AND s.user_id = m.user_id WHERE m.group_id = ?");
            memberQuery.addBindValue(groupId);
            if (memberQuery.exec()) {
                while (memberQuery.next()) {
                    const QString memberId = memberQuery.value(0).toString();
                    if (!memberId.isEmpty()) {
                        recipients.append({memberId, memberQuery.value(1).toString()});
                    }
                }
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);

    Message msg;
    msg.senderId = sender->id;
    msg.senderName = sender->name;
    msg.senderAvatar = sender->avatar;
    msg.receiverId = groupId;
    msg.content = content;
    msg.clientMessageId = obj.value("clientMessageId").toString().trimmed();
    msg.type = MessageType::Text;
    msg.timestamp = QDateTime::currentDateTime();

    for (const GroupMessageRecipient& recipient : recipients) {
        if (recipient.receiveMode == QLatin1String("block") && recipient.userId != sender->id) continue;
        QTcpSocket* memberSocket = m_userSockets.value(recipient.userId);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        QJsonObject forwarded = QJsonDocument::fromJson(msg.toJson()).object();
        forwarded["type"] = "server_group_message";
        forwarded["groupId"] = groupId;
        forwarded["receiveMode"] = recipient.receiveMode;
        forwarded["assistantInbox"] = recipient.receiveMode == QLatin1String("assistant_quiet");
        memberSocket->write(QJsonDocument(forwarded).toJson(QJsonDocument::Compact));
        memberSocket->write("\n");
        memberSocket->flush();
    }
    saveMessageToSqlite(msg, QStringLiteral("server-group"));
    const bool redisPublished = publishRedisMessageEvent(msg, QStringLiteral("server-group"));
    if (m_redisService->isEnabled() && !redisPublished) {
        ServerGroupSupport::sendNotice(socket, QStringLiteral("群消息跨实例路由失败：其他实例成员可能未收到。"));
    }
    emit newMessage(msg);
}

void Server::handleServerGroupAnnouncementUpdate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) {
        ServerGroupSupport::sendNotice(socket, "群公告更新失败：请先登录");
        return;
    }

    const QString groupId = obj["groupId"].toString("public").trimmed().isEmpty()
        ? QString("public")
        : obj["groupId"].toString("public").trimmed();
    QString announcement = obj["announcement"].toString().trimmed();
    if (announcement.isEmpty()) {
        announcement = "欢迎来到公共聊天室。";
    }
    if (announcement.size() > 1000) {
        announcement = announcement.left(1000);
    }
    if (!ensureAccountDatabase()) {
        ServerGroupSupport::sendNotice(socket, "群公告更新失败：服务端群组存储不可用");
        return;
    }

    QStringList memberIds;
    bool allowed = false;
    bool saved = false;
    QString errorText;
    const QString connectionName = "server_group_announcement_update_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (!ServerGroupSupport::openConnection(db, connectionName)) {
            errorText = "群公告更新失败：无法打开群组数据库";
        } else {
            QSqlQuery permissionQuery(db);
            permissionQuery.prepare("SELECT COALESCE(g.owner_id, ''), COALESCE(m.role, '') "
                                    "FROM server_groups g "
                                    "JOIN server_group_members m ON m.group_id = g.group_id "
                                    "WHERE g.group_id = ? AND m.user_id = ?");
            permissionQuery.addBindValue(groupId);
            permissionQuery.addBindValue(requester->id);
            if (!permissionQuery.exec()) {
                errorText = "群公告更新失败：权限校验失败";
            } else if (!permissionQuery.next()) {
                errorText = "群公告更新失败：你不在该群组";
            } else {
                const QString ownerId = permissionQuery.value(0).toString();
                const QString role = permissionQuery.value(1).toString().toLower();
                allowed = ownerId == requester->id || role == "owner" || role == "admin";
                if (!allowed) {
                    errorText = "群公告更新失败：只有群主或管理员可以编辑";
                }
            }

            if (allowed) {
                QSqlQuery updateQuery(db);
                updateQuery.prepare("UPDATE server_groups SET announcement = ?, updated_at = CURRENT_TIMESTAMP "
                                    "WHERE group_id = ?");
                updateQuery.addBindValue(announcement);
                updateQuery.addBindValue(groupId);
                saved = updateQuery.exec();
                if (!saved) {
                    errorText = "群公告更新失败：保存公告失败";
                }
            }

            if (saved) {
                QSqlQuery insertQuery(db);
                insertQuery.prepare("INSERT INTO server_group_announcements(group_id, author_id, author_name, content, created_at) "
                                    "VALUES(?, ?, ?, ?, CURRENT_TIMESTAMP)");
                insertQuery.addBindValue(groupId);
                insertQuery.addBindValue(requester->id);
                insertQuery.addBindValue(requester->name);
                insertQuery.addBindValue(announcement);
                if (!insertQuery.exec()) {
                    qWarning() << "Failed to record server group announcement history:" << insertQuery.lastError().text();
                }
            }

            if (saved) {
                QSqlQuery memberQuery(db);
                memberQuery.prepare("SELECT user_id FROM server_group_members WHERE group_id = ?");
                memberQuery.addBindValue(groupId);
                if (memberQuery.exec()) {
                    while (memberQuery.next()) {
                        const QString memberId = memberQuery.value(0).toString();
                        if (!memberId.isEmpty() && !memberIds.contains(memberId)) {
                            memberIds << memberId;
                        }
                    }
                }
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);

    if (!saved) {
        QJsonObject details;
        details["requestedAction"] = QStringLiteral("announcement_update");
        details["rejected"] = true;
        details["reason"] = errorText.isEmpty() ? QStringLiteral("unknown") : errorText;
        details["contentLength"] = announcement.size();
        recordServerGroupAuditEvent(groupId,
                                    QStringLiteral("announcement_rejected"),
                                    requester->id,
                                    requester->name,
                                    QString(),
                                    QString(),
                                    details);
        const QStringList snapshotUserIds = serverGroupMemberIds(groupId);
        for (const QString& userId : snapshotUserIds) {
            QTcpSocket* memberSocket = m_userSockets.value(userId);
            if (memberSocket && memberSocket->state() == QAbstractSocket::ConnectedState) {
                sendServerGroupSnapshot(userId, memberSocket);
            }
        }
        ServerGroupSupport::sendNotice(socket, errorText.isEmpty() ? "群公告更新失败" : errorText);
        return;
    }

    recordServerGroupAuditEvent(groupId,
                                QStringLiteral("announcement_update"),
                                requester->id,
                                requester->name,
                                QString(),
                                QString(),
                                QJsonObject{{QStringLiteral("contentLength"), announcement.size()}});

    const QString notice = QString("%1 更新了群公告").arg(requester->name.isEmpty() ? requester->id : requester->name);
    for (const QString& memberId : memberIds) {
        QTcpSocket* memberSocket = m_userSockets.value(memberId);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        ServerGroupSupport::sendNotice(memberSocket, notice);
        sendServerGroupSnapshot(memberId, memberSocket);
    }
}

void Server::handleServerGroupSearch(const QJsonObject& obj, QTcpSocket* socket) {
    if (!findUserBySocket(socket)) return;
    const QString keyword = obj.value("keyword").toString().trimmed();
    if (keyword.isEmpty() || !ensureAccountDatabase()) return;

    QJsonArray groups;
    const QString connectionName = "server_group_search_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (ServerGroupSupport::openConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("SELECT g.group_id, g.group_name, COALESCE(g.announcement, ''), COALESCE(g.owner_id, ''), COUNT(m.user_id) "
                          "FROM server_groups g LEFT JOIN server_group_members m ON m.group_id = g.group_id "
                          "WHERE g.group_id <> 'public' AND COALESCE(g.search_mode, 'id_and_keyword') <> 'private' "
                          "AND (g.group_id LIKE ? OR (COALESCE(g.search_mode, 'id_and_keyword') = 'id_and_keyword' AND g.group_name LIKE ?)) "
                          "GROUP BY g.group_id, g.group_name, g.announcement, g.owner_id "
                          "ORDER BY g.updated_at DESC LIMIT 30");
            const QString pattern = QStringLiteral("%%1%").arg(keyword);
            query.addBindValue(pattern);
            query.addBindValue(pattern);
            if (query.exec()) {
                while (query.next()) {
                    QJsonObject group;
                    group["groupId"] = query.value(0).toString();
                    group["groupName"] = query.value(1).toString();
                    group["announcement"] = query.value(2).toString();
                    group["ownerId"] = query.value(3).toString();
                    group["memberCount"] = query.value(4).toInt();
                    groups.append(group);
                }
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);
    QJsonObject reply;
    reply["type"] = "server_group_search_results";
    reply["keyword"] = keyword;
    reply["groups"] = groups;
    socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

void Server::handleServerGroupSettingsUpdate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    const QString groupId = ServerGroupSupport::normalizeGroupId(obj.value("groupId").toString());
    const QJsonObject requested = obj.value("settings").toObject();
    if (!requester || groupId.isEmpty() || requested.isEmpty() || !ensureAccountDatabase()) return;

    QString error;
    QStringList recipients;
    const QString connectionName = "server_group_settings_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (!ServerGroupSupport::openConnection(db, connectionName)) {
            error = QStringLiteral("群设置保存失败：群组数据库不可用");
        } else {
            QSqlQuery permission(db);
            permission.prepare("SELECT COALESCE(g.owner_id, ''), COALESCE(m.role, '') FROM server_groups g "
                               "JOIN server_group_members m ON m.group_id = g.group_id WHERE g.group_id = ? AND m.user_id = ?");
            permission.addBindValue(groupId);
            permission.addBindValue(requester->id);
            if (!permission.exec() || !permission.next()) {
                error = QStringLiteral("群设置保存失败：你不在该群聊");
            } else if (permission.value(0).toString() != requester->id
                       && permission.value(1).toString().toLower() != QLatin1String("owner")
                       && permission.value(1).toString().toLower() != QLatin1String("admin")) {
                error = QStringLiteral("群设置保存失败：只有群主或管理员可以修改");
            }

            const QStringList allowedKeys = {"groupName", "avatar", "allMuted", "speakingRule", "joinPolicy", "searchMode", "searchable"};
            for (auto it = requested.begin(); error.isEmpty() && it != requested.end(); ++it) {
                if (!allowedKeys.contains(it.key())) error = QStringLiteral("群设置保存失败：存在不支持的设置项");
            }
            const QString speakingRule = requested.value("speakingRule").toString();
            const QString joinPolicy = requested.value("joinPolicy").toString();
            const QString searchMode = requested.value("searchMode").toString();
            if (error.isEmpty() && requested.contains("groupName")
                && requested.value("groupName").toString().trimmed().isEmpty()) {
                error = QStringLiteral("群设置保存失败：群名称不能为空");
            }
            if (error.isEmpty() && requested.contains("avatar")) {
                const QByteArray avatarData = QByteArray::fromBase64(requested.value("avatar").toString().toUtf8());
                const QImage avatar = QImage::fromData(avatarData);
                if (avatarData.isEmpty() || avatar.isNull()) {
                    error = QStringLiteral("群设置保存失败：群头像不是有效图片");
                } else if (avatarData.size() > 768 * 1024 || avatar.width() > 1024 || avatar.height() > 1024) {
                    error = QStringLiteral("群设置保存失败：群头像尺寸或文件过大");
                }
            }
            if (error.isEmpty() && !speakingRule.isEmpty()
                && speakingRule != QLatin1String("unrestricted") && speakingRule != QLatin1String("new_members_24h")
                && speakingRule != QLatin1String("per_minute_10") && speakingRule != QLatin1String("per_minute_5")) {
                error = QStringLiteral("群设置保存失败：发言限制无效");
            }
            if (error.isEmpty() && !joinPolicy.isEmpty()
                && joinPolicy != QLatin1String("approval") && joinPolicy != QLatin1String("open")
                && joinPolicy != QLatin1String("disabled")) {
                error = QStringLiteral("群设置保存失败：加群方式无效");
            }
            if (error.isEmpty() && !searchMode.isEmpty()
                && searchMode != QLatin1String("id_and_keyword") && searchMode != QLatin1String("id_only")
                && searchMode != QLatin1String("private")) {
                error = QStringLiteral("群设置保存失败：群搜索方式无效");
            }
            if (error.isEmpty()) {
                QStringList assignments;
                QList<QVariant> values;
                if (requested.contains("groupName")) { assignments << "group_name = ?"; values << requested.value("groupName").toString().trimmed().left(80); }
                if (requested.contains("avatar")) { assignments << "avatar = ?"; values << requested.value("avatar").toString().left(1024 * 1024); }
                if (requested.contains("allMuted")) { assignments << "all_muted = ?"; values << (requested.value("allMuted").toBool() ? 1 : 0); }
                if (requested.contains("speakingRule")) { assignments << "speaking_rule = ?"; values << (speakingRule.isEmpty() ? QStringLiteral("unrestricted") : speakingRule); }
                if (requested.contains("joinPolicy")) { assignments << "join_policy = ?"; values << (joinPolicy.isEmpty() ? QStringLiteral("approval") : joinPolicy); }
                if (requested.contains("searchMode")) { assignments << "search_mode = ?"; values << (searchMode.isEmpty() ? QStringLiteral("id_and_keyword") : searchMode); }
                if (requested.contains("searchable") && !requested.contains("searchMode")) { assignments << "search_mode = ?"; values << (requested.value("searchable").toBool() ? QStringLiteral("id_and_keyword") : QStringLiteral("private")); }
                if (assignments.isEmpty()) error = QStringLiteral("群设置保存失败：没有可更新的内容");
                else {
                    QSqlQuery update(db);
                    update.prepare("UPDATE server_groups SET " + assignments.join(", ") + ", updated_at = CURRENT_TIMESTAMP WHERE group_id = ?");
                    for (const QVariant& value : values) update.addBindValue(value);
                    update.addBindValue(groupId);
                    if (!update.exec()) error = QStringLiteral("群设置保存失败：数据库写入失败");
                }
            }
            if (error.isEmpty()) {
                QSqlQuery members(db);
                members.prepare("SELECT user_id FROM server_group_members WHERE group_id = ?");
                members.addBindValue(groupId);
                if (members.exec()) while (members.next()) recipients << members.value(0).toString();
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);
    if (!error.isEmpty()) { ServerGroupSupport::sendNotice(socket, error); return; }
    recordServerGroupAuditEvent(groupId, QStringLiteral("settings_update"), requester->id, requester->name,
                                QString(), QString(), requested);
    for (const QString& userId : recipients) {
        if (QTcpSocket* target = m_userSockets.value(userId)) sendServerGroupSnapshot(userId, target);
    }
}

void Server::handleServerGroupUserSettingsUpdate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    const QString groupId = ServerGroupSupport::normalizeGroupId(obj.value("groupId").toString());
    const QJsonObject settings = obj.value("settings").toObject();
    if (!requester || groupId.isEmpty() || settings.isEmpty() || !ensureAccountDatabase()) return;
    const QStringList allowedKeys = {QStringLiteral("nickname"), QStringLiteral("remark"),
                                     QStringLiteral("muteNotifications"), QStringLiteral("receiveMode"),
                                     QStringLiteral("receiveWithoutNotify")};
    QString error;
    const QString connectionName = "server_group_user_settings_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (!ServerGroupSupport::openConnection(db, connectionName)) error = QStringLiteral("个人群设置保存失败：群组数据库不可用");
        else {
            for (auto it = settings.begin(); error.isEmpty() && it != settings.end(); ++it) {
                if (!allowedKeys.contains(it.key())) error = QStringLiteral("个人群设置保存失败：存在不支持的设置项");
            }
            QSqlQuery membership(db);
            membership.prepare("SELECT COUNT(*) FROM server_group_members WHERE group_id = ? AND user_id = ?");
            membership.addBindValue(groupId); membership.addBindValue(requester->id);
            if (!membership.exec() || !membership.next() || membership.value(0).toInt() == 0) {
                error = QStringLiteral("个人群设置保存失败：你不在该群聊");
            } else if (error.isEmpty()) {
                const QString receiveMode = settings.value(QStringLiteral("receiveMode")).toString();
                if (!receiveMode.isEmpty() && receiveMode != QLatin1String("receive_quiet")
                    && receiveMode != QLatin1String("assistant_quiet") && receiveMode != QLatin1String("block")) {
                    error = QStringLiteral("个人群设置保存失败：群消息接收方式无效");
                }
                if (error.isEmpty()) {
                    QStringList assignments;
                    QList<QVariant> values;
                    if (settings.contains("nickname")) { assignments << "nickname = ?"; values << settings.value("nickname").toString().trimmed().left(40); }
                    if (settings.contains("remark")) { assignments << "remark = ?"; values << settings.value("remark").toString().trimmed().left(80); }
                    if (settings.contains("muteNotifications")) { assignments << "mute_notifications = ?"; values << (settings.value("muteNotifications").toBool() ? 1 : 0); }
                    if (settings.contains("receiveMode")) { assignments << "receive_mode = ?"; values << receiveMode; }
                    if (settings.contains("receiveWithoutNotify")) { assignments << "receive_without_notify = ?"; values << (settings.value("receiveWithoutNotify").toBool() ? 1 : 0); }
                    QSqlQuery update(db);
                    update.prepare(ServerGroupSupport::insertIgnore(QStringLiteral("server_group_user_settings"),
                        {QStringLiteral("group_id"), QStringLiteral("user_id"), QStringLiteral("updated_at")},
                        {QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("CURRENT_TIMESTAMP")},
                        {QStringLiteral("group_id"), QStringLiteral("user_id")}));
                    update.addBindValue(groupId); update.addBindValue(requester->id);
                    if (!update.exec()) error = QStringLiteral("个人群设置保存失败：初始化失败");
                    if (error.isEmpty()) {
                        update.prepare("UPDATE server_group_user_settings SET " + assignments.join(", ") + ", updated_at = CURRENT_TIMESTAMP WHERE group_id = ? AND user_id = ?");
                        for (const QVariant& value : values) update.addBindValue(value);
                        update.addBindValue(groupId); update.addBindValue(requester->id);
                        if (!update.exec()) error = QStringLiteral("个人群设置保存失败：数据库写入失败");
                    }
                }
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);
    if (!error.isEmpty()) { ServerGroupSupport::sendNotice(socket, error); return; }
    sendServerGroupSnapshot(requester->id, socket);
}

void Server::handleServerGroupJoinRequest(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester || !ensureAccountDatabase()) return;
    const QString groupId = ServerGroupSupport::normalizeGroupId(obj.value("groupId").toString());
    if (groupId == QLatin1String("public")) {
        ServerGroupSupport::sendNotice(socket, QStringLiteral("公共聊天室无需申请，登录后自动加入"));
        return;
    }

    QString requestId;
    QString groupName;
    QString error;
    QStringList reviewers;
    QStringList affectedMembers;
    bool joinedImmediately = false;
    const QString connectionName = "server_group_join_request_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (ServerGroupSupport::openConnection(db, connectionName)) {
            QSqlQuery groupQuery(db);
            groupQuery.prepare("SELECT COALESCE(group_name, ''), COALESCE(join_policy, 'approval') FROM server_groups WHERE group_id = ?");
            groupQuery.addBindValue(groupId);
            if (!groupQuery.exec() || !groupQuery.next()) {
                error = QStringLiteral("群不存在或已解散");
            } else {
                groupName = groupQuery.value(0).toString();
                const QString joinPolicy = groupQuery.value(1).toString().toLower();
                if (joinPolicy == QLatin1String("disabled")) {
                    error = QStringLiteral("该群暂不允许加入");
                }
                QSqlQuery memberQuery(db);
                memberQuery.prepare("SELECT COUNT(*) FROM server_group_members WHERE group_id = ? AND user_id = ?");
                memberQuery.addBindValue(groupId);
                memberQuery.addBindValue(requester->id);
                if (!memberQuery.exec() || !memberQuery.next()) {
                    error = QStringLiteral("无法校验群成员状态");
                } else if (!error.isEmpty()) {
                    // Join policy already rejected the request.
                } else if (memberQuery.value(0).toInt() > 0) {
                    error = QStringLiteral("你已经是该群成员");
                } else {
                    if (joinPolicy == QLatin1String("open")) {
                        QSqlQuery nameQuery(db);
                        nameQuery.prepare("SELECT COALESCE(user_name, '') FROM accounts WHERE account = ?");
                        nameQuery.addBindValue(requester->id);
                        QString memberName = requester->name;
                        if (nameQuery.exec() && nameQuery.next() && !nameQuery.value(0).toString().trimmed().isEmpty()) memberName = nameQuery.value(0).toString().trimmed();
                        QSqlQuery addQuery(db);
                        addQuery.prepare("INSERT INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) VALUES(?, ?, ?, 'member', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
                        addQuery.addBindValue(groupId); addQuery.addBindValue(requester->id); addQuery.addBindValue(memberName);
                        if (!addQuery.exec()) error = QStringLiteral("加入群聊失败");
                        else {
                            requestId = QStringLiteral("open-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
                            joinedImmediately = true;
                            QSqlQuery recipientsQuery(db);
                            recipientsQuery.prepare("SELECT user_id FROM server_group_members WHERE group_id = ?");
                            recipientsQuery.addBindValue(groupId);
                            if (recipientsQuery.exec()) while (recipientsQuery.next()) affectedMembers << recipientsQuery.value(0).toString();
                        }
                    } else {
                    QSqlQuery pendingQuery(db);
                    pendingQuery.prepare("SELECT request_id FROM server_group_join_requests WHERE group_id = ? AND applicant_id = ? AND state = 'pending'");
                    pendingQuery.addBindValue(groupId);
                    pendingQuery.addBindValue(requester->id);
                    if (pendingQuery.exec() && pendingQuery.next()) {
                        requestId = pendingQuery.value(0).toString();
                    } else {
                        requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        QSqlQuery insertQuery(db);
                        insertQuery.prepare("INSERT INTO server_group_join_requests(request_id, group_id, applicant_id, applicant_name, message, state, created_at, updated_at) "
                                            "VALUES(?, ?, ?, ?, ?, 'pending', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
                        insertQuery.addBindValue(requestId);
                        insertQuery.addBindValue(groupId);
                        insertQuery.addBindValue(requester->id);
                        insertQuery.addBindValue(requester->name);
                        insertQuery.addBindValue(obj.value("message").toString().trimmed().left(120));
                        if (!insertQuery.exec()) error = QStringLiteral("入群申请保存失败");
                    }
                    if (error.isEmpty()) {
                        QSqlQuery reviewerQuery(db);
                        reviewerQuery.prepare("SELECT user_id FROM server_group_members WHERE group_id = ? AND role IN ('owner', 'admin')");
                        reviewerQuery.addBindValue(groupId);
                        if (reviewerQuery.exec()) while (reviewerQuery.next()) reviewers << reviewerQuery.value(0).toString();
                    }
                    }
                }
            }
            db.close();
        } else {
            error = QStringLiteral("群组数据库不可用");
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);

    QJsonObject status;
    status["type"] = "server_group_join_request_status";
    status["groupId"] = groupId;
    status["groupName"] = groupName;
    status["requestId"] = requestId;
    status["state"] = error.isEmpty() ? (joinedImmediately ? QStringLiteral("approved") : QStringLiteral("pending")) : QStringLiteral("rejected");
    status["reason"] = error;
    socket->write(QJsonDocument(status).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
    if (!error.isEmpty()) return;

    if (joinedImmediately) {
        recordServerGroupAuditEvent(groupId, QStringLiteral("join_open"), requester->id, requester->name,
                                    requester->id, requester->name);
        for (const QString& userId : affectedMembers) {
            if (QTcpSocket* target = m_userSockets.value(userId)) sendServerGroupSnapshot(userId, target);
        }
        return;
    }

    QJsonObject application;
    application["type"] = "server_group_join_application";
    application["requestId"] = requestId;
    application["groupId"] = groupId;
    application["groupName"] = groupName;
    application["applicantId"] = requester->id;
    application["applicantName"] = requester->name;
    application["message"] = obj.value("message").toString().trimmed().left(120);
    for (const QString& reviewerId : reviewers) {
        QTcpSocket* reviewerSocket = m_userSockets.value(reviewerId);
        if (!reviewerSocket || reviewerSocket->state() != QAbstractSocket::ConnectedState) continue;
        reviewerSocket->write(QJsonDocument(application).toJson(QJsonDocument::Compact));
        reviewerSocket->write("\n");
        reviewerSocket->flush();
    }
    recordServerGroupAuditEvent(groupId, QStringLiteral("join_requested"), requester->id, requester->name,
                                requester->id, requester->name, QJsonObject{{QStringLiteral("requestId"), requestId}});
}

void Server::handleServerGroupJoinResponse(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* reviewer = findUserBySocket(socket);
    const QString requestId = obj.value("requestId").toString().trimmed();
    if (!reviewer || requestId.isEmpty() || !ensureAccountDatabase()) return;

    QString groupId, groupName, applicantId, applicantName, error;
    bool approved = false;
    QStringList affected;
    const QString connectionName = "server_group_join_response_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (ServerGroupSupport::openConnection(db, connectionName)) {
            QSqlQuery requestQuery(db);
            requestQuery.prepare("SELECT r.group_id, COALESCE(g.group_name, ''), r.applicant_id, COALESCE(r.applicant_name, '') "
                                 "FROM server_group_join_requests r JOIN server_groups g ON g.group_id = r.group_id "
                                 "WHERE r.request_id = ? AND r.state = 'pending'");
            requestQuery.addBindValue(requestId);
            if (!requestQuery.exec() || !requestQuery.next()) {
                error = QStringLiteral("入群申请不存在或已处理");
            } else {
                groupId = requestQuery.value(0).toString(); groupName = requestQuery.value(1).toString();
                applicantId = requestQuery.value(2).toString(); applicantName = requestQuery.value(3).toString();
                QSqlQuery permissionQuery(db);
                permissionQuery.prepare("SELECT COUNT(*) FROM server_group_members WHERE group_id = ? AND user_id = ? AND role IN ('owner', 'admin')");
                permissionQuery.addBindValue(groupId); permissionQuery.addBindValue(reviewer->id);
                if (!permissionQuery.exec() || !permissionQuery.next() || permissionQuery.value(0).toInt() == 0) {
                    error = QStringLiteral("只有群主或管理员可以处理入群申请");
                } else {
                    approved = obj.value("accepted").toBool(false);
                    if (approved) {
                        QSqlQuery addQuery(db);
                        addQuery.prepare("INSERT OR IGNORE INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) VALUES(?, ?, ?, 'member', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
                        addQuery.addBindValue(groupId); addQuery.addBindValue(applicantId); addQuery.addBindValue(applicantName);
                        if (!addQuery.exec()) error = QStringLiteral("批准入群失败");
                    }
                    if (error.isEmpty()) {
                        QSqlQuery updateQuery(db);
                        updateQuery.prepare("UPDATE server_group_join_requests SET state = ?, reviewed_by = ?, reviewed_by_name = ?, reviewed_at = CURRENT_TIMESTAMP, updated_at = CURRENT_TIMESTAMP WHERE request_id = ?");
                        updateQuery.addBindValue(approved ? QStringLiteral("approved") : QStringLiteral("rejected"));
                        updateQuery.addBindValue(reviewer->id); updateQuery.addBindValue(reviewer->name); updateQuery.addBindValue(requestId);
                        if (!updateQuery.exec()) error = QStringLiteral("入群申请状态更新失败");
                    }
                    if (error.isEmpty() && approved) affected = serverGroupMemberIds(groupId);
                }
            }
            db.close();
        } else error = QStringLiteral("群组数据库不可用");
    }
    ServerGroupSupport::releaseDatabase(connectionName);
    if (!error.isEmpty()) { ServerGroupSupport::sendNotice(socket, error); return; }
    QJsonObject status;
    status["type"] = "server_group_join_request_status"; status["requestId"] = requestId;
    status["groupId"] = groupId; status["groupName"] = groupName;
    status["state"] = approved ? QStringLiteral("approved") : QStringLiteral("rejected");
    status["reviewerName"] = reviewer->name;
    if (QTcpSocket* applicantSocket = m_userSockets.value(applicantId)) {
        applicantSocket->write(QJsonDocument(status).toJson(QJsonDocument::Compact)); applicantSocket->write("\n"); applicantSocket->flush();
    }
    socket->write(QJsonDocument(status).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
    ServerGroupSupport::sendNotice(socket, approved ? QStringLiteral("已同意入群申请") : QStringLiteral("已拒绝入群申请"));
    recordServerGroupAuditEvent(groupId, approved ? QStringLiteral("join_approved") : QStringLiteral("join_rejected"), reviewer->id, reviewer->name, applicantId, applicantName);
    if (approved) {
        for (const QString& userId : affected) {
            if (QTcpSocket* memberSocket = m_userSockets.value(userId)) sendServerGroupSnapshot(userId, memberSocket);
        }
        publishRedisServerGroupSnapshotRefresh(affected, groupId, QStringLiteral("新的入群申请已批准"), applicantId, QStringLiteral("add"));
    }
}

void Server::handleServerGroupLeave(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    const QString groupId = ServerGroupSupport::normalizeGroupId(obj.value("groupId").toString());
    if (!requester || groupId == QLatin1String("public") || !ensureAccountDatabase()) return;

    QString groupName, error;
    QStringList affected;
    const QString connectionName = "server_group_leave_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (ServerGroupSupport::openConnection(db, connectionName)) {
            QSqlQuery memberQuery(db);
            memberQuery.prepare("SELECT COALESCE(g.group_name, ''), COALESCE(g.owner_id, ''), COALESCE(m.role, '') "
                                "FROM server_groups g JOIN server_group_members m ON m.group_id = g.group_id "
                                "WHERE g.group_id = ? AND m.user_id = ?");
            memberQuery.addBindValue(groupId); memberQuery.addBindValue(requester->id);
            if (!memberQuery.exec() || !memberQuery.next()) {
                error = QStringLiteral("你不在该群聊中");
            } else {
                groupName = memberQuery.value(0).toString();
                const QString ownerId = memberQuery.value(1).toString();
                const QString role = memberQuery.value(2).toString().toLower();
                if (ownerId == requester->id || role == QLatin1String("owner")) {
                    error = QStringLiteral("群主不能直接退群，请先转让群主或解散群聊");
                } else {
                    QSqlQuery deleteQuery(db);
                    deleteQuery.prepare("DELETE FROM server_group_members WHERE group_id = ? AND user_id = ?");
                    deleteQuery.addBindValue(groupId); deleteQuery.addBindValue(requester->id);
                    if (!deleteQuery.exec()) error = QStringLiteral("退出群聊失败");
                    else {
                        QSqlQuery removedQuery(db);
                        removedQuery.prepare(ServerGroupSupport::insertReplace(
                            QStringLiteral("server_group_removed_members"),
                            {QStringLiteral("group_id"), QStringLiteral("user_id"), QStringLiteral("removed_by"), QStringLiteral("removed_by_name"), QStringLiteral("removed_at")},
                            {QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("CURRENT_TIMESTAMP")},
                            {QStringLiteral("group_id"), QStringLiteral("user_id")},
                            {QStringLiteral("removed_by = EXCLUDED.removed_by"), QStringLiteral("removed_by_name = EXCLUDED.removed_by_name"), QStringLiteral("removed_at = EXCLUDED.removed_at")}));
                        removedQuery.addBindValue(groupId); removedQuery.addBindValue(requester->id);
                        removedQuery.addBindValue(requester->id); removedQuery.addBindValue(requester->name);
                        if (!removedQuery.exec()) error = QStringLiteral("退出群聊记录失败");
                    }
                    if (error.isEmpty()) affected = serverGroupMemberIds(groupId);
                }
            }
            db.close();
        } else error = QStringLiteral("群组数据库不可用");
    }
    ServerGroupSupport::releaseDatabase(connectionName);
    QJsonObject status;
    status["type"] = "server_group_leave_status";
    status["groupId"] = groupId; status["groupName"] = groupName;
    status["success"] = error.isEmpty(); status["reason"] = error;
    socket->write(QJsonDocument(status).toJson(QJsonDocument::Compact)); socket->write("\n"); socket->flush();
    if (!error.isEmpty()) return;
    affected << requester->id;
    recordServerGroupAuditEvent(groupId, QStringLiteral("member_left"), requester->id, requester->name, requester->id, requester->name);
    for (const QString& userId : affected) {
        if (QTcpSocket* memberSocket = m_userSockets.value(userId)) sendServerGroupSnapshot(userId, memberSocket);
    }
    publishRedisServerGroupSnapshotRefresh(affected, groupId, QStringLiteral("成员已退出群聊"), requester->id, QStringLiteral("remove"));
}

void Server::handleServerGroupDissolve(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    const QString groupId = ServerGroupSupport::normalizeGroupId(obj.value("groupId").toString());
    if (!requester || groupId.isEmpty() || groupId == QLatin1String("public") || !ensureAccountDatabase()) return;

    QString groupName;
    QString error;
    QStringList members;
    const QString connectionName = "server_group_dissolve_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (!ServerGroupSupport::openConnection(db, connectionName)) {
            error = QStringLiteral("解散群聊失败：群组数据库不可用");
        } else {
            QSqlQuery groupQuery(db);
            groupQuery.prepare("SELECT COALESCE(group_name, ''), COALESCE(owner_id, '') FROM server_groups WHERE group_id = ?");
            groupQuery.addBindValue(groupId);
            if (!groupQuery.exec() || !groupQuery.next()) {
                error = QStringLiteral("解散群聊失败：群不存在");
            } else if (groupQuery.value(1).toString() != requester->id) {
                error = QStringLiteral("解散群聊失败：只有群主可以解散群聊");
            } else {
                groupName = groupQuery.value(0).toString();
                QSqlQuery memberQuery(db);
                memberQuery.prepare("SELECT user_id FROM server_group_members WHERE group_id = ?");
                memberQuery.addBindValue(groupId);
                if (memberQuery.exec()) while (memberQuery.next()) members << memberQuery.value(0).toString();
                if (!db.transaction()) error = QStringLiteral("解散群聊失败：无法开始事务");
                if (error.isEmpty()) {
                    const QStringList tables = {QStringLiteral("server_group_members"), QStringLiteral("server_group_removed_members"),
                                                QStringLiteral("server_group_join_requests"), QStringLiteral("server_group_announcements"),
                                                QStringLiteral("server_group_audit_events"), QStringLiteral("server_group_essence_messages"),
                                                QStringLiteral("server_group_member_mutes"), QStringLiteral("server_group_user_settings"),
                                                QStringLiteral("server_group_recalled_messages")};
                    for (const QString& table : tables) {
                        QSqlQuery deleteRelated(db);
                        deleteRelated.prepare(QStringLiteral("DELETE FROM %1 WHERE group_id = ?").arg(table));
                        deleteRelated.addBindValue(groupId);
                        if (!deleteRelated.exec()) { error = QStringLiteral("解散群聊失败：无法清理群资料"); break; }
                    }
                }
                if (error.isEmpty()) {
                    QSqlQuery deleteGroup(db);
                    deleteGroup.prepare("DELETE FROM server_groups WHERE group_id = ?");
                    deleteGroup.addBindValue(groupId);
                    if (!deleteGroup.exec()) error = QStringLiteral("解散群聊失败：无法删除群聊");
                }
                if (error.isEmpty()) {
                    if (!db.commit()) error = QStringLiteral("解散群聊失败：无法提交操作");
                } else {
                    db.rollback();
                }
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);
    QJsonObject status;
    status["type"] = "server_group_dissolve_status";
    status["groupId"] = groupId;
    status["groupName"] = groupName;
    status["success"] = error.isEmpty();
    status["reason"] = error;
    for (const QString& userId : members) {
        if (QTcpSocket* target = m_userSockets.value(userId)) {
            target->write(QJsonDocument(status).toJson(QJsonDocument::Compact));
            target->write("\n");
            target->flush();
            sendServerGroupSnapshot(userId, target);
        }
    }
    if (!error.isEmpty()) ServerGroupSupport::sendNotice(socket, error);
}

void Server::handleServerGroupMemberUpdate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) {
        ServerGroupSupport::sendNotice(socket, "群成员变更失败：请先登录");
        return;
    }

    const QString groupId = obj["groupId"].toString("public").trimmed().isEmpty()
        ? QString("public")
        : obj["groupId"].toString("public").trimmed();
    const QString action = obj["action"].toString().trimmed().toLower();
    const QString memberId = obj["memberId"].toString().trimmed();
    const bool roleAction = action == QLatin1String("promote_admin") || action == QLatin1String("demote_admin");
    if ((action != "add" && action != "remove" && !roleAction) || groupId.isEmpty() || memberId.isEmpty()) {
        ServerGroupSupport::sendNotice(socket, "群成员变更失败：请求参数无效");
        return;
    }
    if (!ensureAccountDatabase()) {
        ServerGroupSupport::sendNotice(socket, "群成员变更失败：服务端群组存储不可用");
        return;
    }

    QStringList affectedUserIds;
    bool changed = false;
    QString memberName;
    QString errorText;
    const QString connectionName = "server_group_member_update_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (!ServerGroupSupport::openConnection(db, connectionName)) {
            errorText = "群成员变更失败：无法打开群组数据库";
        } else {
            QString ownerId;
            QString requesterRole;
            QSqlQuery permissionQuery(db);
            permissionQuery.prepare("SELECT COALESCE(g.owner_id, ''), COALESCE(m.role, '') "
                                    "FROM server_groups g "
                                    "JOIN server_group_members m ON m.group_id = g.group_id "
                                    "WHERE g.group_id = ? AND m.user_id = ?");
            permissionQuery.addBindValue(groupId);
            permissionQuery.addBindValue(requester->id);
            if (!permissionQuery.exec()) {
                errorText = "群成员变更失败：权限校验失败";
            } else if (!permissionQuery.next()) {
                errorText = "群成员变更失败：你不在该群组";
            } else {
                ownerId = permissionQuery.value(0).toString();
                requesterRole = permissionQuery.value(1).toString().toLower();
                const bool allowed = ownerId == requester->id || requesterRole == "owner" || requesterRole == "admin";
                if (!allowed) {
                    errorText = "群成员变更失败：只有群主或管理员可以管理成员";
                } else if (roleAction && ownerId != requester->id && requesterRole != QLatin1String("owner")) {
                    errorText = "群成员变更失败：只有群主可以设置管理员";
                }
            }

            if (errorText.isEmpty() && action == "add") {
                QSqlQuery accountQuery(db);
                accountQuery.prepare("SELECT COALESCE(user_name, '') FROM accounts WHERE account = ?");
                accountQuery.addBindValue(memberId);
                if (!accountQuery.exec()) {
                    errorText = "群成员变更失败：账号查询失败";
                } else if (!accountQuery.next()) {
                    errorText = "群成员变更失败：目标账号不存在";
                } else {
                    memberName = accountQuery.value(0).toString();
                    if (memberName.isEmpty()) memberName = memberId;
                }
            }

            bool alreadyMember = false;
            QString targetRole;
            if (errorText.isEmpty()) {
                QSqlQuery targetQuery(db);
                targetQuery.prepare("SELECT COALESCE(user_name, ''), COALESCE(role, '') "
                                    "FROM server_group_members WHERE group_id = ? AND user_id = ?");
                targetQuery.addBindValue(groupId);
                targetQuery.addBindValue(memberId);
                if (!targetQuery.exec()) {
                    errorText = "群成员变更失败：成员查询失败";
                } else if (targetQuery.next()) {
                    alreadyMember = true;
                    if (memberName.isEmpty()) memberName = targetQuery.value(0).toString();
                    targetRole = targetQuery.value(1).toString().toLower();
                }
            }

            if (errorText.isEmpty() && action == "add") {
                if (alreadyMember) {
                    errorText = "该用户已经是群成员";
                } else {
                    QSqlQuery insertQuery(db);
                    insertQuery.prepare("INSERT INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) "
                                        "VALUES(?, ?, ?, 'member', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
                    insertQuery.addBindValue(groupId);
                    insertQuery.addBindValue(memberId);
                    insertQuery.addBindValue(memberName);
                    if (!insertQuery.exec()) {
                        errorText = "群成员变更失败：添加成员失败";
                    } else {
                        QSqlQuery clearRemovedQuery(db);
                        clearRemovedQuery.prepare("DELETE FROM server_group_removed_members WHERE group_id = ? AND user_id = ?");
                        clearRemovedQuery.addBindValue(groupId);
                        clearRemovedQuery.addBindValue(memberId);
                        if (!clearRemovedQuery.exec()) {
                            qWarning() << "Failed to clear removed group member marker:" << clearRemovedQuery.lastError().text();
                        }
                        changed = true;
                    }
                }
            } else if (errorText.isEmpty() && action == "remove") {
                if (!alreadyMember) {
                    errorText = "群成员变更失败：目标用户不在该群组";
                } else if (memberId == ownerId || targetRole == "owner") {
                    errorText = "群成员变更失败：不能移出群主";
                } else if (requesterRole == QLatin1String("admin") && targetRole == QLatin1String("admin")) {
                    errorText = "群成员变更失败：管理员不能移出其他管理员";
                } else if (memberId == requester->id) {
                    errorText = "群成员变更失败：不能通过管理操作移出自己";
                } else {
                    QSqlQuery deleteQuery(db);
                    deleteQuery.prepare("DELETE FROM server_group_members WHERE group_id = ? AND user_id = ?");
                    deleteQuery.addBindValue(groupId);
                    deleteQuery.addBindValue(memberId);
                    if (!deleteQuery.exec()) {
                        errorText = "群成员变更失败：移出成员失败";
                    } else {
                        QSqlQuery removedQuery(db);
                        removedQuery.prepare(ServerGroupSupport::insertReplace(
                            QStringLiteral("server_group_removed_members"),
                            {QStringLiteral("group_id"), QStringLiteral("user_id"), QStringLiteral("removed_by"), QStringLiteral("removed_by_name"), QStringLiteral("removed_at")},
                            {QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("CURRENT_TIMESTAMP")},
                            {QStringLiteral("group_id"), QStringLiteral("user_id")},
                            {QStringLiteral("removed_by = EXCLUDED.removed_by"),
                             QStringLiteral("removed_by_name = EXCLUDED.removed_by_name"),
                             QStringLiteral("removed_at = EXCLUDED.removed_at")}));
                        removedQuery.addBindValue(groupId);
                        removedQuery.addBindValue(memberId);
                        removedQuery.addBindValue(requester->id);
                        removedQuery.addBindValue(requester->name);
                        const bool removedMarkerOk = removedQuery.exec();
                        if (!removedMarkerOk) {
                            errorText = QStringLiteral("群成员变更失败：记录移出成员失败");
                            qWarning() << "Failed to record removed group member marker:" << removedQuery.lastError().text();
                        }
                        changed = removedMarkerOk;
                    }
                }
            } else if (errorText.isEmpty() && roleAction) {
                if (!alreadyMember) {
                    errorText = "群成员变更失败：目标用户不在该群组";
                } else if (memberId == ownerId || targetRole == "owner") {
                    errorText = "群成员变更失败：群主角色不能被修改";
                } else if (memberId == requester->id) {
                    errorText = "群成员变更失败：不能修改自己的管理员角色";
                } else {
                    const QString desiredRole = action == QLatin1String("promote_admin")
                        ? QStringLiteral("admin")
                        : QStringLiteral("member");
                    if (targetRole == desiredRole) {
                        errorText = action == QLatin1String("promote_admin")
                            ? QStringLiteral("群成员变更失败：目标用户已经是管理员")
                            : QStringLiteral("群成员变更失败：目标用户已经是普通成员");
                    } else {
                        QSqlQuery updateRoleQuery(db);
                        updateRoleQuery.prepare("UPDATE server_group_members "
                                                "SET role = ?, updated_at = CURRENT_TIMESTAMP "
                                                "WHERE group_id = ? AND user_id = ?");
                        updateRoleQuery.addBindValue(desiredRole);
                        updateRoleQuery.addBindValue(groupId);
                        updateRoleQuery.addBindValue(memberId);
                        if (!updateRoleQuery.exec()) {
                            errorText = "群成员变更失败：更新成员角色失败";
                        } else {
                            changed = true;
                        }
                    }
                }
            }

            if (changed) {
                QSqlQuery memberQuery(db);
                memberQuery.prepare("SELECT user_id FROM server_group_members WHERE group_id = ?");
                memberQuery.addBindValue(groupId);
                if (memberQuery.exec()) {
                    while (memberQuery.next()) {
                        const QString userId = memberQuery.value(0).toString();
                        if (!userId.isEmpty() && !affectedUserIds.contains(userId)) {
                            affectedUserIds << userId;
                        }
                    }
                }
                if (!affectedUserIds.contains(requester->id)) {
                    affectedUserIds << requester->id;
                }
                if (!affectedUserIds.contains(memberId)) {
                    affectedUserIds << memberId;
                }
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);

    if (!changed) {
        QJsonObject details;
        details["requestedAction"] = action;
        details["roleAction"] = roleAction;
        details["rejected"] = true;
        details["reason"] = errorText.isEmpty() ? QStringLiteral("not-applied") : errorText;
        recordServerGroupAuditEvent(groupId,
                                    action.isEmpty() ? QStringLiteral("member_update_rejected") : action + QStringLiteral("_rejected"),
                                    requester->id,
                                    requester->name,
                                    memberId,
                                    memberName,
                                    details);
        const QStringList snapshotUserIds = serverGroupMemberIds(groupId);
        for (const QString& userId : snapshotUserIds) {
            QTcpSocket* memberSocket = m_userSockets.value(userId);
            if (memberSocket && memberSocket->state() == QAbstractSocket::ConnectedState) {
                sendServerGroupSnapshot(userId, memberSocket);
            }
        }
        ServerGroupSupport::sendNotice(socket, errorText.isEmpty() ? "群成员变更未生效" : errorText);
        return;
    }

    recordServerGroupAuditEvent(groupId,
                                action,
                                requester->id,
                                requester->name,
                                memberId,
                                memberName,
                                QJsonObject{{QStringLiteral("roleAction"), roleAction}});

    const QString displayName = memberName.isEmpty() ? memberId : memberName;
    QString notice;
    if (action == QLatin1String("add")) {
        notice = QString("%1 已被加入群组").arg(displayName);
    } else if (action == QLatin1String("remove")) {
        notice = QString("%1 已被移出群组").arg(displayName);
    } else if (action == QLatin1String("promote_admin")) {
        notice = QString("%1 已被设为群管理员").arg(displayName);
    } else {
        notice = QString("%1 已被取消群管理员").arg(displayName);
    }
    for (const QString& userId : affectedUserIds) {
        QTcpSocket* memberSocket = m_userSockets.value(userId);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        ServerGroupSupport::sendNotice(memberSocket, notice);
        sendServerGroupMemberUpdated(memberSocket, groupId, memberId, action);
        sendServerGroupSnapshot(userId, memberSocket);
    }
    publishRedisServerGroupSnapshotRefresh(affectedUserIds, groupId, notice, memberId, action);
}

void Server::handleServerGroupEssenceUpdate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) {
        ServerGroupSupport::sendNotice(socket, "设置精华失败：请先登录");
        return;
    }
    const QString groupId = ServerGroupSupport::normalizeGroupId(obj.value("groupId").toString(QStringLiteral("public")));
    QString messageId = obj.value("messageId").toString().trimmed();
    if (messageId.isEmpty()) {
        messageId = obj.value("clientMessageId").toString(
            obj.value("id").toString(
                obj.value("transferId").toString())).trimmed();
    }
    const bool enabled = obj.value("enabled").toBool(true);
    if (groupId.isEmpty() || messageId.isEmpty() || !ensureAccountDatabase()) {
        ServerGroupSupport::sendNotice(socket, "设置精华失败：请求参数无效");
        return;
    }

    QString errorText;
    bool changed = false;
    const QString connectionName = "server_group_essence_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (!ServerGroupSupport::openConnection(db, connectionName)) {
            errorText = "设置精华失败：无法打开群组数据库";
        } else {
            const ServerGroupSupport::ActorPermission permission = ServerGroupSupport::actorPermission(db, groupId, requester->id);
            if (!permission.member) {
                errorText = "设置精华失败：你不在该群组";
            } else if (!permission.manager && groupId != QLatin1String("public")) {
                errorText = "设置精华失败：只有群主或管理员可以设置精华";
            } else if (enabled) {
                QSqlQuery query(db);
                query.prepare(ServerGroupSupport::insertReplace(
                    QStringLiteral("server_group_essence_messages"),
                    {QStringLiteral("group_id"), QStringLiteral("message_id"), QStringLiteral("set_by"), QStringLiteral("set_by_name"), QStringLiteral("created_at")},
                    {QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("CURRENT_TIMESTAMP")},
                    {QStringLiteral("group_id"), QStringLiteral("message_id")},
                    {QStringLiteral("set_by = EXCLUDED.set_by"),
                     QStringLiteral("set_by_name = EXCLUDED.set_by_name"),
                     QStringLiteral("created_at = EXCLUDED.created_at")}));
                query.addBindValue(groupId);
                query.addBindValue(messageId);
                query.addBindValue(requester->id);
                query.addBindValue(requester->name);
                changed = query.exec();
            } else {
                QSqlQuery query(db);
                query.prepare("DELETE FROM server_group_essence_messages WHERE group_id = ? AND message_id = ?");
                query.addBindValue(groupId);
                query.addBindValue(messageId);
                changed = query.exec();
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);

    if (!changed) {
        ServerGroupSupport::sendNotice(socket, errorText.isEmpty() ? "设置精华失败" : errorText);
        return;
    }
    recordServerGroupAuditEvent(groupId,
                                enabled ? QStringLiteral("essence_set") : QStringLiteral("essence_unset"),
                                requester->id,
                                requester->name,
                                QString(),
                                QString(),
                                QJsonObject{{QStringLiteral("messageId"), messageId}});
    const QStringList memberIds = serverGroupMemberIds(groupId);
    for (const QString& memberId : memberIds) {
        QTcpSocket* memberSocket = m_userSockets.value(memberId);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        QJsonObject event;
        event["type"] = "group_essence_updated";
        event["groupId"] = groupId;
        event["messageId"] = messageId;
        event["enabled"] = enabled;
        event["operatorId"] = requester->id;
        memberSocket->write(QJsonDocument(event).toJson(QJsonDocument::Compact));
        memberSocket->write("\n");
        sendServerGroupSnapshot(memberId, memberSocket);
    }
}

void Server::handleServerGroupMessageRecall(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) {
        ServerGroupSupport::sendNotice(socket, "撤回消息失败：请先登录");
        return;
    }
    const QString groupId = ServerGroupSupport::normalizeGroupId(obj.value("groupId").toString(QStringLiteral("public")));
    QString messageId = obj.value("messageId").toString().trimmed();
    if (messageId.isEmpty()) {
        messageId = obj.value("clientMessageId").toString(
            obj.value("id").toString(
                obj.value("transferId").toString())).trimmed();
    }
    if (groupId.isEmpty() || messageId.isEmpty() || !ensureAccountDatabase()) {
        ServerGroupSupport::sendNotice(socket, "撤回消息失败：请求参数无效");
        return;
    }

    QString errorText;
    bool changed = false;
    const QString connectionName = "server_group_recall_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (!ServerGroupSupport::openConnection(db, connectionName)) {
            errorText = "撤回消息失败：无法打开群组数据库";
        } else {
            const ServerGroupSupport::ActorPermission permission = ServerGroupSupport::actorPermission(db, groupId, requester->id);
            if (!permission.member) {
                errorText = "撤回消息失败：你不在该群组";
            } else if (!permission.manager) {
                errorText = "撤回消息失败：只有群主或管理员可以撤回群消息";
            } else {
                QSqlQuery query(db);
                query.prepare(ServerGroupSupport::insertReplace(
                    QStringLiteral("server_group_recalled_messages"),
                    {QStringLiteral("group_id"), QStringLiteral("message_id"), QStringLiteral("recalled_by"), QStringLiteral("recalled_by_name"), QStringLiteral("created_at")},
                    {QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("CURRENT_TIMESTAMP")},
                    {QStringLiteral("group_id"), QStringLiteral("message_id")},
                    {QStringLiteral("recalled_by = EXCLUDED.recalled_by"),
                     QStringLiteral("recalled_by_name = EXCLUDED.recalled_by_name"),
                     QStringLiteral("created_at = EXCLUDED.created_at")}));
                query.addBindValue(groupId);
                query.addBindValue(messageId);
                query.addBindValue(requester->id);
                query.addBindValue(requester->name);
                changed = query.exec();
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);

    if (!changed) {
        ServerGroupSupport::sendNotice(socket, errorText.isEmpty() ? "撤回消息失败" : errorText);
        return;
    }
    recordServerGroupAuditEvent(groupId,
                                QStringLiteral("message_recall"),
                                requester->id,
                                requester->name,
                                QString(),
                                QString(),
                                QJsonObject{{QStringLiteral("messageId"), messageId}});
    const qint64 recalledAt = QDateTime::currentMSecsSinceEpoch();
    const QStringList memberIds = serverGroupMemberIds(groupId);
    for (const QString& memberId : memberIds) {
        QTcpSocket* memberSocket = m_userSockets.value(memberId);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        QJsonObject event;
        event["type"] = "group_message_recalled";
        event["groupId"] = groupId;
        event["messageId"] = messageId;
        event["operatorId"] = requester->id;
        event["recalledAt"] = QString::number(recalledAt);
        memberSocket->write(QJsonDocument(event).toJson(QJsonDocument::Compact));
        memberSocket->write("\n");
        sendServerGroupSnapshot(memberId, memberSocket);
    }
}

void Server::handleServerGroupMemberMute(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) {
        ServerGroupSupport::sendNotice(socket, "禁言失败：请先登录");
        return;
    }
    const QString groupId = obj.value("groupId").toString("public").trimmed();
    const QString memberId = obj.value("memberId").toString().trimmed();
    const qint64 mutedUntil = obj.value("mutedUntil").isString()
        ? obj.value("mutedUntil").toString().toLongLong()
        : static_cast<qint64>(obj.value("mutedUntil").toDouble(-1));
    const QString reason = obj.value("reason").toString().trimmed();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (groupId.isEmpty() || memberId.isEmpty() || mutedUntil <= now || mutedUntil > now + 30LL * 24 * 60 * 60 * 1000 || !ensureAccountDatabase()) {
        ServerGroupSupport::sendNotice(socket, "禁言失败：请求参数无效或超过 30 天");
        return;
    }

    QString errorText;
    bool changed = false;
    QString memberName = memberId;
    const QString connectionName = "server_group_mute_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (!ServerGroupSupport::openConnection(db, connectionName)) {
            errorText = "禁言失败：无法打开群组数据库";
        } else {
            const ServerGroupSupport::ActorPermission permission = ServerGroupSupport::actorPermission(db, groupId, requester->id);
            if (!permission.member) {
                errorText = "禁言失败：你不在该群组";
            } else if (!permission.manager) {
                errorText = "禁言失败：只有群主或管理员可以禁言";
            } else {
                QSqlQuery targetQuery(db);
                targetQuery.prepare("SELECT COALESCE(user_name, ''), COALESCE(role, '') FROM server_group_members WHERE group_id = ? AND user_id = ?");
                targetQuery.addBindValue(groupId);
                targetQuery.addBindValue(memberId);
                if (!targetQuery.exec() || !targetQuery.next()) {
                    errorText = "禁言失败：目标用户不在该群组";
                } else {
                    memberName = targetQuery.value(0).toString().isEmpty() ? memberId : targetQuery.value(0).toString();
                    const QString targetRole = targetQuery.value(1).toString().toLower();
                    if (memberId == requester->id) {
                        errorText = "禁言失败：不能禁言自己";
                    } else if (!permission.owner && (targetRole == QLatin1String("owner") || targetRole == QLatin1String("admin"))) {
                        errorText = "禁言失败：管理员不能禁言群主或其他管理员";
                    } else {
                        QSqlQuery query(db);
                        query.prepare(ServerGroupSupport::insertReplace(
                            QStringLiteral("server_group_member_mutes"),
                            {QStringLiteral("group_id"), QStringLiteral("user_id"), QStringLiteral("muted_until"), QStringLiteral("muted_by"), QStringLiteral("muted_by_name"), QStringLiteral("reason"), QStringLiteral("created_at"), QStringLiteral("updated_at")},
                            {QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("CURRENT_TIMESTAMP"), QStringLiteral("CURRENT_TIMESTAMP")},
                            {QStringLiteral("group_id"), QStringLiteral("user_id")},
                            {QStringLiteral("muted_until = EXCLUDED.muted_until"),
                             QStringLiteral("muted_by = EXCLUDED.muted_by"),
                             QStringLiteral("muted_by_name = EXCLUDED.muted_by_name"),
                             QStringLiteral("reason = EXCLUDED.reason"),
                             QStringLiteral("updated_at = EXCLUDED.updated_at")}));
                        query.addBindValue(groupId);
                        query.addBindValue(memberId);
                        query.addBindValue(mutedUntil);
                        query.addBindValue(requester->id);
                        query.addBindValue(requester->name);
                        query.addBindValue(reason);
                        changed = query.exec();
                    }
                }
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);
    if (!changed) {
        ServerGroupSupport::sendNotice(socket, errorText.isEmpty() ? "禁言失败" : errorText);
        return;
    }
    recordServerGroupAuditEvent(groupId, QStringLiteral("member_mute"), requester->id, requester->name, memberId, memberName, QJsonObject{{QStringLiteral("mutedUntil"), QString::number(mutedUntil)}, {QStringLiteral("reason"), reason}});
    const QStringList memberIds = serverGroupMemberIds(groupId);
    for (const QString& id : memberIds) {
        QTcpSocket* memberSocket = m_userSockets.value(id);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        QJsonObject event;
        event["type"] = "group_member_muted";
        event["groupId"] = groupId;
        event["memberId"] = memberId;
        event["mutedUntil"] = QString::number(mutedUntil);
        event["reason"] = reason;
        event["operatorId"] = requester->id;
        memberSocket->write(QJsonDocument(event).toJson(QJsonDocument::Compact));
        memberSocket->write("\n");
        sendServerGroupSnapshot(id, memberSocket);
    }
}

void Server::handleServerGroupMemberUnmute(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) {
        ServerGroupSupport::sendNotice(socket, "解除禁言失败：请先登录");
        return;
    }
    const QString groupId = obj.value("groupId").toString("public").trimmed();
    const QString memberId = obj.value("memberId").toString().trimmed();
    if (groupId.isEmpty() || memberId.isEmpty() || !ensureAccountDatabase()) {
        ServerGroupSupport::sendNotice(socket, "解除禁言失败：请求参数无效");
        return;
    }
    QString errorText;
    bool changed = false;
    const QString connectionName = "server_group_unmute_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (!ServerGroupSupport::openConnection(db, connectionName)) {
            errorText = "解除禁言失败：无法打开群组数据库";
        } else {
            const ServerGroupSupport::ActorPermission permission = ServerGroupSupport::actorPermission(db, groupId, requester->id);
            if (!permission.manager) {
                errorText = "解除禁言失败：只有群主或管理员可以解除禁言";
            } else {
                QSqlQuery query(db);
                query.prepare("DELETE FROM server_group_member_mutes WHERE group_id = ? AND user_id = ?");
                query.addBindValue(groupId);
                query.addBindValue(memberId);
                changed = query.exec();
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);
    if (!changed) {
        ServerGroupSupport::sendNotice(socket, errorText.isEmpty() ? "解除禁言失败" : errorText);
        return;
    }
    recordServerGroupAuditEvent(groupId, QStringLiteral("member_unmute"), requester->id, requester->name, memberId, memberId);
    const QStringList memberIds = serverGroupMemberIds(groupId);
    for (const QString& id : memberIds) {
        QTcpSocket* memberSocket = m_userSockets.value(id);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        QJsonObject event;
        event["type"] = "group_member_unmuted";
        event["groupId"] = groupId;
        event["memberId"] = memberId;
        event["operatorId"] = requester->id;
        memberSocket->write(QJsonDocument(event).toJson(QJsonDocument::Compact));
        memberSocket->write("\n");
        sendServerGroupSnapshot(id, memberSocket);
    }
}

void Server::handleServerGroupMemberProfileRequest(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) return;
    const QString groupId = obj.value("groupId").toString("public").trimmed();
    const QString memberId = obj.value("memberId").toString().trimmed();
    if (groupId.isEmpty() || memberId.isEmpty() || !ensureAccountDatabase()) {
        ServerGroupSupport::sendNotice(socket, "查看资料失败：请求参数无效");
        return;
    }
    QJsonObject profile;
    profile["type"] = "server_group_member_profile";
    profile["groupId"] = groupId;
    profile["userId"] = memberId;
    profile["userName"] = memberId;
    profile["online"] = m_userSockets.contains(memberId);
    profile["role"] = "member";
    profile["groupNickname"] = "";
    profile["remark"] = "";
    profile["signature"] = "";
    profile["location"] = "";
    profile["qqSpaceUrl"] = QString("https://user.qzone.qq.com/%1").arg(memberId);
    const QString connectionName = "server_group_profile_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = ServerGroupSupport::openDatabase(connectionName);
        if (ServerGroupSupport::openConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("SELECT COALESCE(m.user_name, ''), COALESCE(m.role, 'member'), COALESCE(a.avatar, '') "
                          "FROM server_group_members m "
                          "LEFT JOIN accounts a ON a.account = m.user_id "
                          "WHERE m.group_id = ? AND m.user_id = ?");
            query.addBindValue(groupId);
            query.addBindValue(memberId);
            if (query.exec() && query.next()) {
                profile["userName"] = query.value(0).toString().isEmpty() ? memberId : query.value(0).toString();
                profile["role"] = query.value(1).toString().isEmpty() ? QStringLiteral("member") : query.value(1).toString();
                profile["avatar"] = query.value(2).toString();
                profile["groupNickname"] = profile["userName"];
            }
            qint64 mutedUntil = 0;
            if (isServerGroupMemberMuted(groupId, memberId, &mutedUntil)) {
                profile["mutedUntil"] = QString::number(mutedUntil);
            }
            db.close();
        }
    }
    ServerGroupSupport::releaseDatabase(connectionName);
    socket->write(QJsonDocument(profile).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}
