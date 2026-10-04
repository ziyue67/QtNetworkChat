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

bool Server::saveFriendEventToSqlite(const QString& eventType,
                                     const QString& senderId,
                                     const QString& senderName,
                                     const QString& receiverId,
                                     const QString& queryAccount,
                                     const QString& eventState,
                                     bool accepted) const {
    if (!ensureAccountDatabase()) return false;

    QString connectionName = "friend_events_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO friend_events(event_type, sender_id, sender_name, receiver_id, query_account, accepted, event_state, created_at) "
                          "VALUES(?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)");
            query.addBindValue(eventType);
            query.addBindValue(senderId);
            query.addBindValue(senderName);
            query.addBindValue(receiverId);
            query.addBindValue(queryAccount);
            query.addBindValue(accepted ? 1 : 0);
            query.addBindValue(eventState);
            ok = query.exec();
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::saveAcceptedFriendshipToSqlite(const QString& userId,
                                        const QString& userName,
                                        const QString& friendId,
                                        const QString& friendName) const {
    const QString normalizedUserId = userId.trimmed();
    const QString normalizedFriendId = friendId.trimmed();
    if (normalizedUserId.isEmpty() || normalizedFriendId.isEmpty() || normalizedUserId == normalizedFriendId || !ensureAccountDatabase()) {
        return false;
    }

    const QString normalizedUserName = userName.trimmed().isEmpty() ? normalizedUserId : userName.trimmed();
    const QString normalizedFriendName = friendName.trimmed().isEmpty() ? normalizedFriendId : friendName.trimmed();
    const QString connectionName = "server_friends_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName) && db.transaction()) {
            auto upsertFriend = [&db](const QString& ownerId, const QString& peerId, const QString& peerName) {
                QSqlQuery query(db);
                query.prepare(insertReplaceSql(
                    QStringLiteral("server_friends"),
                    {QStringLiteral("user_id"), QStringLiteral("friend_id"), QStringLiteral("friend_name"), QStringLiteral("updated_at")},
                    {QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("CURRENT_TIMESTAMP")},
                    {QStringLiteral("user_id"), QStringLiteral("friend_id")},
                    {QStringLiteral("friend_name = EXCLUDED.friend_name"), QStringLiteral("updated_at = EXCLUDED.updated_at")}));
                query.addBindValue(ownerId);
                query.addBindValue(peerId);
                query.addBindValue(peerName);
                return query.exec();
            };

            ok = upsertFriend(normalizedUserId, normalizedFriendId, normalizedFriendName)
                && upsertFriend(normalizedFriendId, normalizedUserId, normalizedUserName);
            ok = ok ? db.commit() : false;
            if (!ok) {
                db.rollback();
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

QVector<ChatUser> Server::loadFriendListFromSqlite(const QString& userId) const {
    QVector<ChatUser> friends;
    const QString normalizedUserId = userId.trimmed();
    if (normalizedUserId.isEmpty() || !ensureAccountDatabase()) return friends;

    const QString connectionName = "server_friends_read_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(normalizedUserId));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("SELECT f.friend_id, "
                          "COALESCE(NULLIF(a.user_name, ''), NULLIF(f.friend_name, ''), f.friend_id), "
                          "COALESCE(a.avatar, ''), "
                          "COALESCE(f.updated_at, f.created_at, '') "
                          "FROM server_friends f "
                          "LEFT JOIN accounts a ON a.account = f.friend_id "
                          "WHERE f.user_id = ? "
                          "ORDER BY LOWER(COALESCE(NULLIF(a.user_name, ''), NULLIF(f.friend_name, ''), f.friend_id)), f.friend_id");
            query.addBindValue(normalizedUserId);
            if (query.exec()) {
                while (query.next()) {
                    ChatUser user;
                    user.id = query.value(0).toString().trimmed();
                    if (user.id.isEmpty()) {
                        continue;
                    }
                    user.name = query.value(1).toString().trimmed();
                    if (user.name.isEmpty()) {
                        user.name = user.id;
                    }
                    user.avatar = query.value(2).toString();
                    const QString updatedAt = query.value(3).toString().trimmed();
                    user.lastActive = QDateTime::fromString(updatedAt, Qt::ISODate);
                    if (!user.lastActive.isValid()) {
                        user.lastActive = QDateTime::fromString(updatedAt, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                    }
                    friends.append(user);
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return friends;
}
