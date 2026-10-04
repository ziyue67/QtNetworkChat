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
bool serverGroupMessageRateAllowed(const QString& groupId, const QString& userId, int perMinute) {
    static QMutex mutex;
    static QHash<QString, QList<qint64>> timestampsByMember;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString key = groupId + QLatin1Char('|') + userId;
    QMutexLocker lock(&mutex);
    QList<qint64>& timestamps = timestampsByMember[key];
    while (!timestamps.isEmpty() && timestamps.first() <= now - 60 * 1000) timestamps.removeFirst();
    if (timestamps.size() >= perMinute) return false;
    timestamps.append(now);
    return true;
}

QString serverGroupTypeFromId(const QString& groupId) {
    return groupId == QLatin1String("public") ? QStringLiteral("public") : QStringLiteral("private");
}

QString serverGroupHistoryPolicy(const QString& groupType) {
    return groupType == QLatin1String("private")
        ? QStringLiteral("member-and-removed-readonly")
        : QStringLiteral("public-removed-readonly");
}

QString serverGroupFilePolicy(const QString& groupType) {
    return groupType == QLatin1String("private")
        ? QStringLiteral("members-only")
        : QStringLiteral("public-members-only");
}

QString normalizeServerGroupId(QString groupId) {
    groupId = groupId.trimmed();
    if (groupId.startsWith(QStringLiteral("group:"))) {
        groupId = groupId.mid(QStringLiteral("group:").size()).trimmed();
    }
    return groupId.isEmpty() ? QStringLiteral("public") : groupId;
}

QStringList initialServerGroupMemberIds(const QJsonObject& obj, const QString& requesterId) {
    QStringList memberIds;
    const QJsonArray members = obj.value(QStringLiteral("members")).toArray();
    for (const QJsonValue& memberValue : members) {
        QString memberId = memberValue.toString().trimmed();
        if (memberId.isEmpty() && memberValue.isObject()) {
            const QJsonObject memberObject = memberValue.toObject();
            memberId = memberObject.value(QStringLiteral("userId")).toString(
                memberObject.value(QStringLiteral("account")).toString(
                    memberObject.value(QStringLiteral("id")).toString())).trimmed();
        }
        if (memberId.isEmpty() || memberId == requesterId || memberIds.contains(memberId)) {
            continue;
        }
        memberIds << memberId;
    }
    return memberIds;
}

using ServerGroupActorPermission = ServerGroupSupport::ActorPermission;

ServerGroupActorPermission loadServerGroupActorPermission(QSqlDatabase& db,
                                                          const QString& groupId,
                                                          const QString& userId) {
    ServerGroupActorPermission permission;
    QSqlQuery query(db);
    query.prepare("SELECT COALESCE(g.owner_id, ''), COALESCE(m.role, '') "
                  "FROM server_groups g "
                  "JOIN server_group_members m ON m.group_id = g.group_id "
                  "WHERE g.group_id = ? AND m.user_id = ?");
    query.addBindValue(groupId);
    query.addBindValue(userId);
    if (query.exec() && query.next()) {
        permission.member = true;
        permission.ownerId = query.value(0).toString();
        permission.role = query.value(1).toString().toLower();
        permission.owner = permission.ownerId == userId || permission.role == QLatin1String("owner");
        permission.manager = permission.owner || permission.role == QLatin1String("admin");
    }
    return permission;
}

}

namespace ServerGroupSupport {
QString groupType(const QString& groupId) {
    return serverGroupTypeFromId(groupId);
}

QString filePolicy(const QString& groupType) {
    return serverGroupFilePolicy(groupType);
}

bool messageRateAllowed(const QString& groupId, const QString& userId, int perMinute) {
    return serverGroupMessageRateAllowed(groupId, userId, perMinute);
}

QString normalizeGroupId(QString groupId) {
    return normalizeServerGroupId(groupId);
}

QStringList initialMemberIds(const QJsonObject& obj, const QString& requesterId) {
    return initialServerGroupMemberIds(obj, requesterId);
}

QSqlDatabase openDatabase(const QString& connectionName) {
    return openAccountDatabase(connectionName);
}

bool openConnection(QSqlDatabase& db, const QString& scope) {
    return openAccountDatabaseConnection(db, scope);
}

void releaseDatabase(const QString& connectionName) {
    releaseAccountDatabase(connectionName);
}

ActorPermission actorPermission(QSqlDatabase& db, const QString& groupId, const QString& userId) {
    return loadServerGroupActorPermission(db, groupId, userId);
}

QString insertIgnore(const QString& table, const QStringList& columns,
                     const QStringList& values, const QStringList& conflictColumns) {
    return insertIgnoreSql(table, columns, values, conflictColumns);
}

QString insertReplace(const QString& table, const QStringList& columns,
                      const QStringList& values, const QStringList& conflictColumns,
                      const QStringList& updateAssignments) {
    return insertReplaceSql(table, columns, values, conflictColumns, updateAssignments);
}

void sendNotice(QTcpSocket* socket, const QString& content) {
    sendSystemNotice(socket, content);
}
}

bool Server::recordDefaultGroupMembership(const ChatUser& user) const {
    if (user.id.trimmed().isEmpty() || !ensureAccountDatabase()) return false;

    const QString normalizedUserId = user.id.trimmed();
    const QString normalizedUserName = user.name.trimmed().isEmpty() ? normalizedUserId : user.name.trimmed();
    QString connectionName = "default_group_member_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            if (db.transaction()) {
                QSqlQuery groupQuery(db);
                ok = groupQuery.exec(insertIgnoreSql(
                    QStringLiteral("server_groups"),
                    {QStringLiteral("group_id"), QStringLiteral("group_name"), QStringLiteral("announcement"), QStringLiteral("group_type"), QStringLiteral("history_policy"), QStringLiteral("file_policy"), QStringLiteral("created_at"), QStringLiteral("updated_at")},
                    {QStringLiteral("'public'"), QStringLiteral("'公共聊天室'"), QStringLiteral("'欢迎来到公共聊天室。'"), QStringLiteral("'public'"), QStringLiteral("'public-removed-readonly'"), QStringLiteral("'public-members-only'"), QStringLiteral("CURRENT_TIMESTAMP"), QStringLiteral("CURRENT_TIMESTAMP")},
                    {QStringLiteral("group_id")}));

                if (ok) {
                    QSqlQuery repairGroupQuery(db);
                    repairGroupQuery.prepare("UPDATE server_groups "
                                             "SET group_name = COALESCE(NULLIF(group_name, ''), '公共聊天室'), "
                                             "announcement = COALESCE(NULLIF(announcement, ''), '欢迎来到公共聊天室。'), "
                                             "group_type = 'public', "
                                             "history_policy = COALESCE(NULLIF(history_policy, ''), 'public-removed-readonly'), "
                                             "file_policy = COALESCE(NULLIF(file_policy, ''), 'public-members-only'), "
                                             "updated_at = CURRENT_TIMESTAMP "
                                             "WHERE group_id = 'public'");
                    ok = repairGroupQuery.exec();
                }

                const bool wasRemovedFromPublic = [&]() {
                    QSqlQuery removedQuery(db);
                    removedQuery.prepare("SELECT COUNT(*) FROM server_group_removed_members WHERE group_id = 'public' AND user_id = ?");
                    removedQuery.addBindValue(normalizedUserId);
                    return removedQuery.exec() && removedQuery.next() && removedQuery.value(0).toInt() > 0;
                }();

                if (ok && wasRemovedFromPublic) {
                    ok = db.commit();
                    db.close();
                    releaseAccountDatabase(connectionName);
                    return ok;
                }

                if (ok) {
                    QSqlQuery insertMemberQuery(db);
                    insertMemberQuery.prepare(insertReplaceSql(
                        QStringLiteral("server_group_members"),
                        {QStringLiteral("group_id"), QStringLiteral("user_id"), QStringLiteral("user_name"), QStringLiteral("role"), QStringLiteral("joined_at"), QStringLiteral("updated_at")},
                        {QStringLiteral("'public'"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("COALESCE((SELECT role FROM server_group_members WHERE group_id = 'public' AND user_id = ?), 'member')"), QStringLiteral("CURRENT_TIMESTAMP"), QStringLiteral("CURRENT_TIMESTAMP")},
                        {QStringLiteral("group_id"), QStringLiteral("user_id")},
                        {QStringLiteral("user_name = EXCLUDED.user_name"),
                         QStringLiteral("role = CASE WHEN server_group_members.role = 'owner' THEN 'owner' ELSE EXCLUDED.role END"),
                         QStringLiteral("updated_at = EXCLUDED.updated_at")}));
                    insertMemberQuery.addBindValue(normalizedUserId);
                    insertMemberQuery.addBindValue(normalizedUserName);
                    insertMemberQuery.addBindValue(normalizedUserId);
                    ok = insertMemberQuery.exec();
                    if (!ok) {
                        qWarning() << "Failed to upsert public group member:" << insertMemberQuery.lastError().text();
                    }
                }

                QString ownerId;
                if (ok) {
                    QSqlQuery ownerQuery(db);
                    ownerQuery.prepare("SELECT COALESCE(owner_id, '') FROM server_groups WHERE group_id = 'public'");
                    ok = ownerQuery.exec();
                    if (ok && ownerQuery.next()) {
                        ownerId = ownerQuery.value(0).toString().trimmed();
                    }
                }
                if (ok && !ownerId.isEmpty()) {
                    QSqlQuery ownerMemberQuery(db);
                    ownerMemberQuery.prepare("SELECT COUNT(*) FROM server_group_members WHERE group_id = 'public' AND user_id = ?");
                    ownerMemberQuery.addBindValue(ownerId);
                    ok = ownerMemberQuery.exec();
                    if (ok && ownerMemberQuery.next() && ownerMemberQuery.value(0).toInt() == 0) {
                        ownerId.clear();
                    }
                }
                if (ok && ownerId.isEmpty()) {
                    ownerId = normalizedUserId;
                    QSqlQuery updateOwnerQuery(db);
                    updateOwnerQuery.prepare("UPDATE server_groups SET owner_id = ?, updated_at = CURRENT_TIMESTAMP "
                                             "WHERE group_id = 'public'");
                    updateOwnerQuery.addBindValue(ownerId);
                    ok = updateOwnerQuery.exec();
                }
                if (ok) {
                    QSqlQuery backfillMembersQuery(db);
                    const QString currentOwnerSql = QStringLiteral("COALESCE((SELECT owner_id FROM server_groups WHERE group_id = 'public'), '')");
                    if (accountDatabaseIsPostgres()) {
                        ok = backfillMembersQuery.exec(
                            "INSERT INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) "
                            "SELECT 'public', a.account, COALESCE(NULLIF(a.user_name, ''), a.account), "
                            "CASE WHEN a.account = " + currentOwnerSql + " THEN 'owner' ELSE COALESCE(NULLIF(m.role, ''), 'member') END, "
                            "COALESCE(m.joined_at, CURRENT_TIMESTAMP::text), CURRENT_TIMESTAMP::text "
                            "FROM accounts a "
                            "LEFT JOIN server_group_members m ON m.group_id = 'public' AND m.user_id = a.account "
                            "WHERE COALESCE(a.account_status, 'active') = 'active' "
                            "AND NOT EXISTS (SELECT 1 FROM server_group_removed_members r WHERE r.group_id = 'public' AND r.user_id = a.account) "
                            "ON CONFLICT(group_id, user_id) DO UPDATE SET "
                            "user_name = EXCLUDED.user_name, "
                            "role = CASE WHEN server_group_members.user_id = " + currentOwnerSql + " THEN 'owner' "
                            "WHEN server_group_members.role = 'owner' THEN 'member' ELSE server_group_members.role END, "
                            "updated_at = CURRENT_TIMESTAMP::text");
                    } else {
                        ok = backfillMembersQuery.exec(
                            "INSERT OR REPLACE INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) "
                            "SELECT 'public', a.account, COALESCE(NULLIF(a.user_name, ''), a.account), "
                            "CASE WHEN a.account = " + currentOwnerSql + " THEN 'owner' ELSE COALESCE(NULLIF(m.role, ''), 'member') END, "
                            "COALESCE(m.joined_at, CURRENT_TIMESTAMP), CURRENT_TIMESTAMP "
                            "FROM accounts a "
                            "LEFT JOIN server_group_members m ON m.group_id = 'public' AND m.user_id = a.account "
                            "WHERE COALESCE(a.account_status, 'active') = 'active' "
                            "AND NOT EXISTS (SELECT 1 FROM server_group_removed_members r WHERE r.group_id = 'public' AND r.user_id = a.account)");
                    }
                    if (!ok) {
                        qWarning() << "Failed to backfill public group members:" << backfillMembersQuery.lastError().text();
                    }
                }
                if (ok) {
                    QSqlQuery updateRoleQuery(db);
                    updateRoleQuery.prepare("UPDATE server_group_members "
                                            "SET role = CASE WHEN user_id = ? THEN 'owner' "
                                            "WHEN role = 'owner' THEN 'member' ELSE role END, "
                                            "updated_at = CURRENT_TIMESTAMP "
                                            "WHERE group_id = 'public'");
                    updateRoleQuery.addBindValue(ownerId);
                    ok = updateRoleQuery.exec();
                }

                if (ok) {
                    ok = db.commit();
                } else {
                    db.rollback();
                }
            } else {
                qWarning() << "Failed to start public group membership transaction:" << db.lastError().text();
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::ensurePublicGroupMembership(const QString& userId, QTcpSocket* socket) const {
    const QString normalizedUserId = userId.trimmed();
    if (normalizedUserId.isEmpty()) return false;

    ChatUser user;
    if (socket && m_clients.contains(socket)) {
        const ChatUser socketUser = m_clients.value(socket);
        if (socketUser.id == normalizedUserId) {
            user = socketUser;
        }
    }
    if (user.id.isEmpty()) {
        for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it) {
            if (it.value().id == normalizedUserId) {
                user = it.value();
                break;
            }
        }
    }
    if (user.id.isEmpty()) {
        user.id = normalizedUserId;
        user.name = normalizedUserId;
    }

    if (isServerGroupRemovedMember(QStringLiteral("public"), normalizedUserId)) {
        return false;
    }

    const bool recorded = recordDefaultGroupMembership(user);
    const bool member = isServerGroupMember(QStringLiteral("public"), normalizedUserId);
    if (!member) {
        qWarning() << "Public group membership repair failed" << normalizedUserId << "recorded" << recorded;
    }
    return member;
}

bool Server::isServerGroupMember(const QString& groupId, const QString& userId) const {
    if (groupId.isEmpty() || userId.isEmpty() || !ensureAccountDatabase()) return false;

    const QString connectionName = "server_group_membership_check_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(groupId + "|" + userId));
    bool exists = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("SELECT COUNT(*) FROM server_group_members WHERE group_id = ? AND user_id = ?");
            query.addBindValue(groupId);
            query.addBindValue(userId);
            if (query.exec() && query.next()) {
                exists = query.value(0).toInt() > 0;
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return exists;
}

bool Server::isServerGroupMemberMuted(const QString& groupId, const QString& userId, qint64* mutedUntil) const {
    if (mutedUntil) *mutedUntil = 0;
    if (groupId.isEmpty() || userId.isEmpty() || !ensureAccountDatabase()) return false;

    const QString connectionName = "server_group_mute_check_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(groupId + "|" + userId));
    bool muted = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery cleanup(db);
            cleanup.prepare("DELETE FROM server_group_member_mutes WHERE muted_until <= ?");
            cleanup.addBindValue(now);
            cleanup.exec();

            QSqlQuery query(db);
            query.prepare("SELECT muted_until FROM server_group_member_mutes WHERE group_id = ? AND user_id = ? AND muted_until > ?");
            query.addBindValue(groupId);
            query.addBindValue(userId);
            query.addBindValue(now);
            if (query.exec() && query.next()) {
                const qint64 until = query.value(0).toLongLong();
                muted = true;
                if (mutedUntil) *mutedUntil = until;
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return muted;
}

bool Server::isServerGroupRemovedMember(const QString& groupId, const QString& userId) const {
    if (groupId.isEmpty() || userId.isEmpty() || !ensureAccountDatabase()) return false;

    const QString connectionName = "server_group_removed_membership_check_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(groupId + "|" + userId));
    bool exists = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("SELECT COUNT(*) FROM server_group_removed_members r "
                          "LEFT JOIN server_group_members m ON m.group_id = r.group_id AND m.user_id = r.user_id "
                          "WHERE r.group_id = ? AND r.user_id = ? AND m.user_id IS NULL");
            query.addBindValue(groupId);
            query.addBindValue(userId);
            if (query.exec() && query.next()) {
                exists = query.value(0).toInt() > 0;
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return exists;
}

QStringList Server::serverGroupMemberIds(const QString& groupId) const {
    QStringList memberIds;
    if (groupId.trimmed().isEmpty() || !ensureAccountDatabase()) return memberIds;

    const QString connectionName = "server_group_member_ids_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(groupId));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("SELECT user_id FROM server_group_members WHERE group_id = ?");
            query.addBindValue(groupId.trimmed());
            if (query.exec()) {
                while (query.next()) {
                    const QString memberId = query.value(0).toString().trimmed();
                    if (!memberId.isEmpty() && !memberIds.contains(memberId)) {
                        memberIds << memberId;
                    }
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return memberIds;
}

bool Server::recordServerGroupAuditEvent(const QString& groupId,
                                         const QString& action,
                                         const QString& actorId,
                                         const QString& actorName,
                                         const QString& targetUserId,
                                         const QString& targetUserName,
                                         const QJsonObject& details) const {
    if (groupId.trimmed().isEmpty()
        || action.trimmed().isEmpty()
        || actorId.trimmed().isEmpty()
        || !ensureAccountDatabase()) {
        return false;
    }

    QString connectionName = "server_group_audit_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO server_group_audit_events("
                          "group_id, action, actor_id, actor_name, target_user_id, target_user_name, details, created_at) "
                          "VALUES(?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)");
            query.addBindValue(groupId.trimmed());
            query.addBindValue(action.trimmed().toLower());
            query.addBindValue(actorId.trimmed());
            query.addBindValue(actorName.trimmed());
            query.addBindValue(targetUserId.trimmed());
            query.addBindValue(targetUserName.trimmed());
            query.addBindValue(QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact)));
            ok = query.exec();
            if (!ok) {
                qWarning() << "Failed to record server group audit event:"
                           << query.lastError().text()
                           << groupId.trimmed()
                           << action.trimmed().toLower()
                           << actorId.trimmed();
            }
            db.close();
        } else {
            qWarning() << "Failed to open server group audit database:"
                       << db.lastError().text()
                       << groupId.trimmed()
                       << action.trimmed().toLower()
                       << actorId.trimmed();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

void Server::sendServerGroupSnapshot(const QString& userId, QTcpSocket* socket) const {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState || userId.isEmpty() || !ensureAccountDatabase()) {
        return;
    }
    ensurePublicGroupMembership(userId, socket);

    QJsonArray groups;
    QJsonArray removedGroups;
    QString connectionName = "server_group_snapshot_" + QString::number(reinterpret_cast<quintptr>(this));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery groupQuery(db);
            groupQuery.prepare("SELECT g.group_id, g.group_name, COALESCE(g.announcement, ''), COALESCE(g.owner_id, ''), "
                               "COALESCE(g.group_type, ''), COALESCE(g.history_policy, ''), COALESCE(g.file_policy, ''), "
                               "COALESCE(g.avatar, ''), COALESCE(g.all_muted, 0), COALESCE(g.speaking_rule, 'unrestricted'), "
                                "COALESCE(g.join_policy, 'approval'), COALESCE(g.search_mode, 'id_and_keyword') "
                               "FROM server_groups g "
                               "JOIN server_group_members m ON m.group_id = g.group_id "
                               "WHERE m.user_id = ? "
                               "ORDER BY g.group_id ASC");
            groupQuery.addBindValue(userId);
            if (groupQuery.exec()) {
                while (groupQuery.next()) {
                    const QString groupId = groupQuery.value(0).toString();
                    QJsonObject groupObj;
                    groupObj["groupId"] = groupId;
                    groupObj["groupName"] = groupQuery.value(1).toString();
                    groupObj["announcement"] = groupQuery.value(2).toString();
                    groupObj["ownerId"] = groupQuery.value(3).toString();
                    const QString groupType = groupQuery.value(4).toString().trimmed().isEmpty()
                        ? serverGroupTypeFromId(groupId)
                        : groupQuery.value(4).toString();
                    const QString historyPolicy = groupQuery.value(5).toString().trimmed().isEmpty()
                        ? serverGroupHistoryPolicy(groupType)
                        : groupQuery.value(5).toString();
                    const QString filePolicy = groupQuery.value(6).toString().trimmed().isEmpty()
                        ? serverGroupFilePolicy(groupType)
                        : groupQuery.value(6).toString();
                    groupObj["groupType"] = groupType;
                    groupObj["membershipState"] = "active";
                    groupObj["historyPolicy"] = historyPolicy;
                    groupObj["filePolicy"] = filePolicy;
                    groupObj["avatar"] = groupQuery.value(7).toString();
                    groupObj["allMuted"] = groupQuery.value(8).toInt() != 0;
                    groupObj["speakingRule"] = groupQuery.value(9).toString();
                    groupObj["joinPolicy"] = groupQuery.value(10).toString();
                    groupObj["searchMode"] = groupQuery.value(11).toString();
                    groupObj["searchable"] = groupObj["searchMode"].toString() != QLatin1String("private");
                    groupObj["canSend"] = true;
                    groupObj["canSendFiles"] = true;
                    groupObj["canReadHistory"] = true;
                    groupObj["historyVisibility"] = groupType == QLatin1String("private")
                        ? QStringLiteral("active-members-and-removed-readonly")
                        : QStringLiteral("public-members-and-removed-readonly");
                    groupObj["historyReadOnly"] = false;
                    groupObj["historyRetainedAfterRemoval"] = historyPolicy.contains(QStringLiteral("removed-readonly"));

                    QJsonArray members;
                    QSqlQuery memberQuery(db);
                    memberQuery.prepare("SELECT m.user_id, COALESCE(NULLIF(m.user_name, ''), NULLIF(a.user_name, ''), m.user_id), "
                                        "COALESCE(NULLIF(m.role, ''), 'member'), COALESCE(a.avatar, '') "
                                        "FROM server_group_members m "
                                        "LEFT JOIN accounts a ON a.account = m.user_id "
                                        "WHERE m.group_id = ? "
                                        "ORDER BY CASE WHEN m.role = 'owner' THEN 0 WHEN m.role = 'admin' THEN 1 ELSE 2 END, "
                                        "m.joined_at ASC, m.user_id ASC");
                    memberQuery.addBindValue(groupId);
                    if (memberQuery.exec()) {
                        while (memberQuery.next()) {
                            QJsonObject memberObj;
                            const QString memberUserId = memberQuery.value(0).toString();
                            const QString memberUserName = memberQuery.value(1).toString();
                            memberObj["userId"] = memberUserId;
                            memberObj["userName"] = memberUserName;
                            memberObj["role"] = memberQuery.value(2).toString();
                            memberObj["avatar"] = memberQuery.value(3).toString();
                            memberObj["online"] = m_userSockets.contains(memberUserId);
                            memberObj["groupNickname"] = memberUserName;
                            qint64 mutedUntil = 0;
                            QSqlQuery muteQuery(db);
                            muteQuery.prepare("SELECT muted_until FROM server_group_member_mutes WHERE group_id = ? AND user_id = ? AND muted_until > ?");
                            muteQuery.addBindValue(groupId);
                            muteQuery.addBindValue(memberUserId);
                            muteQuery.addBindValue(QDateTime::currentMSecsSinceEpoch());
                            if (muteQuery.exec() && muteQuery.next()) {
                                mutedUntil = muteQuery.value(0).toLongLong();
                            }
                            if (mutedUntil > 0) {
                                memberObj["mutedUntil"] = QString::number(mutedUntil);
                            }
                            members.append(memberObj);
                        }
                    } else {
                        qWarning() << "Failed to load server group members for snapshot:" << memberQuery.lastError().text() << groupId;
                    }
                    groupObj["members"] = members;
                    groupObj["memberCount"] = members.size();

                    QSqlQuery userSettingsQuery(db);
                    userSettingsQuery.prepare("SELECT COALESCE(nickname, ''), COALESCE(remark, ''), "
                                              "COALESCE(mute_notifications, 0), COALESCE(receive_without_notify, 0), "
                                              "COALESCE(receive_mode, 'receive_quiet') "
                                              "FROM server_group_user_settings WHERE group_id = ? AND user_id = ?");
                    userSettingsQuery.addBindValue(groupId);
                    userSettingsQuery.addBindValue(userId);
                    QJsonObject userSettings;
                    if (userSettingsQuery.exec() && userSettingsQuery.next()) {
                        userSettings["nickname"] = userSettingsQuery.value(0).toString();
                        userSettings["remark"] = userSettingsQuery.value(1).toString();
                        userSettings["muteNotifications"] = userSettingsQuery.value(2).toInt() != 0;
                        userSettings["receiveWithoutNotify"] = userSettingsQuery.value(3).toInt() != 0;
                        userSettings["receiveMode"] = userSettingsQuery.value(4).toString();
                    }
                    groupObj["userSettings"] = userSettings;

                    QJsonArray essenceMessages;
                    QSqlQuery essenceQuery(db);
                    essenceQuery.prepare("SELECT message_id, COALESCE(set_by, ''), COALESCE(set_by_name, ''), created_at "
                                         "FROM server_group_essence_messages "
                                         "WHERE group_id = ? "
                                         "ORDER BY created_at DESC LIMIT 50");
                    essenceQuery.addBindValue(groupId);
                    if (essenceQuery.exec()) {
                        while (essenceQuery.next()) {
                            QJsonObject essenceObj;
                            essenceObj["messageId"] = essenceQuery.value(0).toString();
                            essenceObj["sessionId"] = groupId;
                            essenceObj["setBy"] = essenceQuery.value(1).toString();
                            essenceObj["senderName"] = essenceQuery.value(2).toString();
                            essenceObj["setAt"] = essenceQuery.value(3).toString();
                            essenceObj["enabled"] = true;
                            essenceMessages.append(essenceObj);
                        }
                    }
                    groupObj["essenceMessages"] = essenceMessages;

                    QJsonArray auditEvents;
                    QSqlQuery auditQuery(db);
                    auditQuery.prepare("SELECT action, actor_id, COALESCE(actor_name, ''), "
                                       "COALESCE(target_user_id, ''), COALESCE(target_user_name, ''), "
                                       "COALESCE(details, ''), created_at "
                                       "FROM server_group_audit_events "
                                       "WHERE group_id = ? "
                                       "ORDER BY id DESC LIMIT 20");
                    auditQuery.addBindValue(groupId);
                    if (auditQuery.exec()) {
                        while (auditQuery.next()) {
                            QJsonObject auditObj;
                            auditObj["action"] = auditQuery.value(0).toString();
                            auditObj["actorId"] = auditQuery.value(1).toString();
                            auditObj["actorName"] = auditQuery.value(2).toString();
                            auditObj["targetUserId"] = auditQuery.value(3).toString();
                            auditObj["targetUserName"] = auditQuery.value(4).toString();
                            const QJsonDocument detailsDoc = QJsonDocument::fromJson(auditQuery.value(5).toString().toUtf8());
                            auditObj["details"] = detailsDoc.isObject() ? detailsDoc.object() : QJsonObject();
                            auditObj["createdAt"] = auditQuery.value(6).toString();
                            auditEvents.prepend(auditObj);
                        }
                    }
                    groupObj["auditEvents"] = auditEvents;
                    groups.append(groupObj);
                }
            }

            QSqlQuery removedQuery(db);
            removedQuery.prepare("SELECT g.group_id, g.group_name, COALESCE(g.announcement, ''), "
                                 "COALESCE(g.owner_id, ''), COALESCE(g.group_type, ''), "
                                 "COALESCE(g.history_policy, ''), COALESCE(g.file_policy, ''), "
                                 "COALESCE(r.removed_by, ''), "
                                 "COALESCE(r.removed_by_name, ''), r.removed_at "
                                 "FROM server_group_removed_members r "
                                 "JOIN server_groups g ON g.group_id = r.group_id "
                                 "LEFT JOIN server_group_members m ON m.group_id = r.group_id AND m.user_id = r.user_id "
                                 "WHERE r.user_id = ? AND m.user_id IS NULL "
                                 "ORDER BY r.removed_at DESC, g.group_id ASC");
            removedQuery.addBindValue(userId);
            if (removedQuery.exec()) {
                while (removedQuery.next()) {
                    QJsonObject groupObj;
                    groupObj["groupId"] = removedQuery.value(0).toString();
                    groupObj["groupName"] = removedQuery.value(1).toString();
                    groupObj["announcement"] = removedQuery.value(2).toString();
                    groupObj["ownerId"] = removedQuery.value(3).toString();
                    const QString groupType = removedQuery.value(4).toString().trimmed().isEmpty()
                        ? serverGroupTypeFromId(groupObj["groupId"].toString())
                        : removedQuery.value(4).toString();
                    const QString historyPolicy = removedQuery.value(5).toString().trimmed().isEmpty()
                        ? serverGroupHistoryPolicy(groupType)
                        : removedQuery.value(5).toString();
                    const QString filePolicy = removedQuery.value(6).toString().trimmed().isEmpty()
                        ? serverGroupFilePolicy(groupType)
                        : removedQuery.value(6).toString();
                    groupObj["groupType"] = groupType;
                    groupObj["membershipState"] = "removed";
                    groupObj["canSend"] = false;
                    groupObj["canSendFiles"] = false;
                    groupObj["canReadHistory"] = true;
                    groupObj["historyPolicy"] = historyPolicy;
                    groupObj["filePolicy"] = filePolicy;
                    groupObj["historyVisibility"] = groupType == QLatin1String("private")
                        ? QStringLiteral("removed-member-readonly")
                        : QStringLiteral("public-removed-member-readonly");
                    groupObj["historyReadOnly"] = true;
                    groupObj["historyRetainedAfterRemoval"] = historyPolicy.contains(QStringLiteral("removed-readonly"));
                    groupObj["removedBy"] = removedQuery.value(7).toString();
                    groupObj["removedByName"] = removedQuery.value(8).toString();
                    groupObj["removedAt"] = removedQuery.value(9).toString();
                    removedGroups.append(groupObj);
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);

    QJsonObject obj;
    obj["type"] = "server_group_snapshot";
    obj["groups"] = groups;
    obj["removedGroups"] = removedGroups;
    socket->write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();

    // Pending applications are a management-only feed. Do not expose them to
    // ordinary members, and resend them after owner/admin reconnects.
    const QString applicationConnection = "server_group_join_application_snapshot_"
        + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase applicationDb = openAccountDatabase(applicationConnection);
        if (openAccountDatabaseConnection(applicationDb, applicationConnection)) {
            QSqlQuery applications(applicationDb);
            applications.prepare("SELECT r.request_id, r.group_id, COALESCE(g.group_name, ''), r.applicant_id, "
                                 "COALESCE(r.applicant_name, ''), COALESCE(r.message, '') "
                                 "FROM server_group_join_requests r "
                                 "JOIN server_group_members m ON m.group_id = r.group_id AND m.user_id = ? "
                                 "JOIN server_groups g ON g.group_id = r.group_id "
                                 "WHERE r.state = 'pending' AND m.role IN ('owner', 'admin') "
                                 "ORDER BY r.created_at ASC LIMIT 100");
            applications.addBindValue(userId);
            if (applications.exec()) {
                while (applications.next()) {
                    QJsonObject application;
                    application["type"] = "server_group_join_application";
                    application["requestId"] = applications.value(0).toString();
                    application["groupId"] = applications.value(1).toString();
                    application["groupName"] = applications.value(2).toString();
                    application["applicantId"] = applications.value(3).toString();
                    application["applicantName"] = applications.value(4).toString();
                    application["message"] = applications.value(5).toString();
                    socket->write(QJsonDocument(application).toJson(QJsonDocument::Compact));
                    socket->write("\n");
                }
                socket->flush();
            }
            applicationDb.close();
        }
    }
    releaseAccountDatabase(applicationConnection);

    const QString outgoingApplicationConnection = "server_group_join_outgoing_snapshot_"
        + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase outgoingDb = openAccountDatabase(outgoingApplicationConnection);
        if (openAccountDatabaseConnection(outgoingDb, outgoingApplicationConnection)) {
            QSqlQuery applications(outgoingDb);
            applications.prepare("SELECT r.request_id, r.group_id, COALESCE(g.group_name, ''), COALESCE(r.message, '') "
                                 "FROM server_group_join_requests r JOIN server_groups g ON g.group_id = r.group_id "
                                 "WHERE r.applicant_id = ? AND r.state = 'pending' ORDER BY r.created_at ASC LIMIT 100");
            applications.addBindValue(userId);
            if (applications.exec()) {
                while (applications.next()) {
                    QJsonObject application;
                    application["type"] = "server_group_join_request_status";
                    application["requestId"] = applications.value(0).toString();
                    application["groupId"] = applications.value(1).toString();
                    application["groupName"] = applications.value(2).toString();
                    application["message"] = applications.value(3).toString();
                    application["state"] = "pending";
                    socket->write(QJsonDocument(application).toJson(QJsonDocument::Compact));
                    socket->write("\n");
                }
                socket->flush();
            }
            outgoingDb.close();
        }
    }
    releaseAccountDatabase(outgoingApplicationConnection);
}
