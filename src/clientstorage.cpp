#include "clientstorage.h"

#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

ClientStorage::ClientStorage(QString userName)
    : m_userName(std::move(userName)) {
}

QString ClientStorage::userName() const {
    return m_userName;
}

void ClientStorage::setUserName(const QString& userName) {
    m_userName = userName;
}

QString ClientStorage::appDataRootDirectory() {
    QSettings settings("QtNetworkChat", "QtNetworkChat");
    QString dir = settings.value("storage/appDataRoot").toString().trimmed();
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    if (dir.isEmpty()) {
        dir = QStringLiteral(".");
    }
    QDir().mkpath(dir);
    return QDir::cleanPath(dir);
}

void ClientStorage::setAppDataRootDirectory(const QString& directoryPath) {
    const QString cleaned = QDir::cleanPath(directoryPath.trimmed());
    if (cleaned.isEmpty()) {
        return;
    }
    QDir().mkpath(cleaned);
    QSettings settings("QtNetworkChat", "QtNetworkChat");
    settings.setValue("storage/appDataRoot", cleaned);
}

void ClientStorage::resetAppDataRootDirectory() {
    QSettings settings("QtNetworkChat", "QtNetworkChat");
    settings.remove("storage/appDataRoot");
}

QString ClientStorage::friendFilePath() const {
    return QDir(appDataDirectory()).filePath(QStringLiteral("friends_%1.txt").arg(safeUserName()));
}

QString ClientStorage::groupFilePath() const {
    return QDir(appDataDirectory()).filePath(QStringLiteral("groups_%1.txt").arg(safeUserName()));
}

QString ClientStorage::friendGroupFilePath() const {
    return QDir(appDataDirectory()).filePath(QStringLiteral("friend_groups_%1.txt").arg(safeUserName()));
}

QString ClientStorage::avatarFilePath() const {
    return QDir(appDataDirectory()).filePath(QStringLiteral("avatar_%1.png").arg(safeUserName()));
}

QString ClientStorage::peerAvatarFilePath(const QString& userId) const {
    const QString peerDir = QDir(appDataDirectory()).filePath(QStringLiteral("peer_avatars"));
    QDir().mkpath(peerDir);
    return QDir(peerDir).filePath(QStringLiteral("avatar_%1.png").arg(safeToken(userId, QStringLiteral("peer"))));
}

bool ClientStorage::savePeerAvatar(const QString& userId, const QByteArray& pngData) const {
    if (userId.trimmed().isEmpty() || pngData.isEmpty()) {
        return false;
    }

    QPixmap pixmap;
    if (!pixmap.loadFromData(pngData, "PNG")) {
        return false;
    }
    const QString filePath = peerAvatarFilePath(userId);
    return pixmap.save(filePath, "PNG");
}

QPixmap ClientStorage::loadPeerAvatar(const QString& userId) const {
    const QString filePath = peerAvatarFilePath(userId);
    if (!QFile::exists(filePath)) {
        return QPixmap();
    }
    return QPixmap(filePath);
}

bool ClientStorage::savePeerAvatarToSqlite(const QString& databasePath,
                                           const QString& userId,
                                           const QString& avatarPath) const {
    if (databasePath.isEmpty() || userId.trimmed().isEmpty() || avatarPath.trimmed().isEmpty()) {
        return false;
    }

    const QString connectionName = QStringLiteral("client_storage_peer_avatar_%1_%2")
        .arg(QString::number(reinterpret_cast<quintptr>(this)))
        .arg(safeToken(userId, QStringLiteral("peer")));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("INSERT OR REPLACE INTO peer_avatars(user_id, avatar_path, updated_at) "
                                         "VALUES(?, ?, datetime('now'))"));
            query.addBindValue(userId.trimmed());
            query.addBindValue(avatarPath.trimmed());
            ok = query.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

QMap<QString, QString> ClientStorage::loadPeerAvatarIndexFromSqlite(const QString& databasePath) const {
    QMap<QString, QString> result;
    if (databasePath.isEmpty()) {
        return result;
    }

    const QString connectionName = QStringLiteral("client_storage_peer_avatar_read_%1")
        .arg(QString::number(reinterpret_cast<quintptr>(this)));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (db.open()) {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral("SELECT user_id, avatar_path FROM peer_avatars ORDER BY updated_at DESC"))) {
                while (query.next()) {
                    const QString userId = query.value(0).toString().trimmed();
                    const QString avatarPath = query.value(1).toString().trimmed();
                    if (!userId.isEmpty() && !avatarPath.isEmpty()) {
                        result.insert(userId, avatarPath);
                    }
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

bool ClientStorage::readLegacyFriends(QStringList* friendIds, QMap<QString, QString>* friendNames) const {
    if (!friendIds || !friendNames) {
        return false;
    }
    QFile file(friendFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        const QString id = line.section(QLatin1Char('|'), 0, 0);
        const QString name = line.section(QLatin1Char('|'), 1, 1);
        if (!id.isEmpty() && !friendIds->contains(id)) {
            *friendIds << id;
        }
        if (!id.isEmpty() && !name.isEmpty()) {
            friendNames->insert(id, name);
        }
    }
    return true;
}

bool ClientStorage::writeLegacyFriends(const QStringList& friendIds,
                                       const QMap<QString, QString>& friendNames) const {
    QFile file(friendFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    for (const QString& id : friendIds) {
        if (!id.isEmpty()) {
            out << id << "|" << friendNames.value(id, id) << "\n";
        }
    }
    return true;
}

bool ClientStorage::readFriendGroups(QMap<QString, QString>* friendGroups,
                                     QStringList* customGroups) const {
    QFile file(friendGroupFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.isEmpty()) {
            continue;
        }
        // "G|<groupName>"        -> a custom (possibly empty) group
        // "<friendId>|<groupName>" -> a friend's group assignment
        const int sep = line.indexOf(QLatin1Char('|'));
        if (sep < 0) {
            continue;
        }
        const QString key = line.left(sep);
        const QString value = line.mid(sep + 1);
        if (key == QLatin1String("G")) {
            if (customGroups && !value.isEmpty() && !customGroups->contains(value)) {
                customGroups->append(value);
            }
        } else if (friendGroups && !key.isEmpty() && !value.isEmpty()) {
            friendGroups->insert(key, value);
        }
    }
    return true;
}

bool ClientStorage::writeFriendGroups(const QMap<QString, QString>& friendGroups,
                                      const QStringList& customGroups) const {
    QFile file(friendGroupFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    for (const QString& name : customGroups) {
        if (!name.isEmpty()) {
            out << "G|" << name << "\n";
        }
    }
    for (auto it = friendGroups.constBegin(); it != friendGroups.constEnd(); ++it) {
        if (!it.key().isEmpty() && !it.value().isEmpty()) {
            out << it.key() << "|" << it.value() << "\n";
        }
    }
    return true;
}

bool ClientStorage::readLegacyLocalGroups(const QString& currentUserId,
                                          QStringList* groupIds,
                                          QMap<QString, QString>* groupNames,
                                          QMap<QString, QStringList>* groupMembers,
                                          QMap<QString, QString>* groupAnnouncements) const {
    if (!groupIds || !groupNames || !groupMembers || !groupAnnouncements) {
        return false;
    }
    QFile file(groupFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        const QString id = line.section(QLatin1Char('|'), 0, 0);
        const QString name = line.section(QLatin1Char('|'), 1, 1);
        if (!id.isEmpty() && !groupIds->contains(id)) {
            *groupIds << id;
        }
        if (!id.isEmpty() && !name.isEmpty()) {
            groupNames->insert(id, name);
        }
        QStringList members = line.section(QLatin1Char('|'), 2, 2).split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (members.isEmpty() && !id.isEmpty()) {
            members << currentUserId;
        }
        if (!id.isEmpty()) {
            groupMembers->insert(id, members);
        }
        const QString announcement = line.section(QLatin1Char('|'), 3, 3);
        if (!id.isEmpty()) {
            groupAnnouncements->insert(id, announcement.isEmpty()
                ? QStringLiteral("%1 已创建，可继续邀请好友并发送消息。").arg(groupNames->value(id, QStringLiteral("群聊")))
                : announcement);
        }
    }
    return true;
}

bool ClientStorage::writeLegacyLocalGroups(const QString& currentUserId,
                                           const QStringList& groupIds,
                                           const QMap<QString, QString>& groupNames,
                                           const QMap<QString, QStringList>& groupMembers,
                                           const QMap<QString, QString>& groupAnnouncements) const {
    QFile file(groupFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    for (const QString& id : groupIds) {
        if (id.isEmpty()) {
            continue;
        }
        QStringList members = groupMembers.value(id);
        if (members.isEmpty()) {
            members << currentUserId;
        }
        out << id << "|" << groupNames.value(id, QStringLiteral("群聊")) << "|"
            << members.join(QLatin1Char(',')) << "|" << groupAnnouncements.value(id) << "\n";
    }
    return true;
}

bool ClientStorage::saveProfileToSqlite(const QString& databasePath,
                                        const QString& userId,
                                        const QString& userName,
                                        const QString& avatarPath) const {
    if (databasePath.isEmpty() || userId.isEmpty()) {
        return false;
    }

    const QString connectionName = QStringLiteral("client_storage_profile_%1")
        .arg(QString::number(reinterpret_cast<quintptr>(this)));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("INSERT OR REPLACE INTO profile(user_id, user_name, avatar_path, updated_at) "
                                         "VALUES(?, ?, ?, datetime('now'))"));
            query.addBindValue(userId);
            query.addBindValue(userName);
            query.addBindValue(avatarPath);
            ok = query.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool ClientStorage::saveFriendsToSqlite(const QString& databasePath,
                                        const QStringList& friendIds,
                                        const QMap<QString, QString>& friendNames,
                                        const QStringList& pendingIncoming,
                                        const QStringList& pendingOutgoing) const {
    if (databasePath.isEmpty()) {
        return false;
    }

    const QString connectionName = QStringLiteral("client_storage_friends_%1")
        .arg(QString::number(reinterpret_cast<quintptr>(this)));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (db.open()) {
            db.transaction();
            QSqlQuery clearQuery(db);
            ok = clearQuery.exec(QStringLiteral("DELETE FROM friends"));
            QSqlQuery insertQuery(db);
            insertQuery.prepare(QStringLiteral("INSERT OR REPLACE INTO friends(user_id, display_name, updated_at) "
                                               "VALUES(?, ?, datetime('now'))"));
            for (const QString& id : friendIds) {
                if (id.isEmpty()) {
                    continue;
                }
                insertQuery.addBindValue(id);
                insertQuery.addBindValue(friendNames.value(id, id));
                ok = insertQuery.exec() && ok;
            }
            if (ok) {
                QSqlQuery clearRequestsQuery(db);
                ok = clearRequestsQuery.exec(QStringLiteral("DELETE FROM friend_requests"));
            }
            QSqlQuery requestQuery(db);
            requestQuery.prepare(QStringLiteral("INSERT OR REPLACE INTO friend_requests(request_id, display_name, direction, status, updated_at) "
                                                "VALUES(?, ?, ?, 'pending', datetime('now'))"));
            for (const QString& id : pendingIncoming) {
                if (id.isEmpty() || friendIds.contains(id)) {
                    continue;
                }
                requestQuery.bindValue(0, id);
                requestQuery.bindValue(1, friendNames.value(id, id));
                requestQuery.bindValue(2, QStringLiteral("incoming"));
                ok = requestQuery.exec() && ok;
            }
            for (const QString& id : pendingOutgoing) {
                if (id.isEmpty() || friendIds.contains(id)) {
                    continue;
                }
                requestQuery.bindValue(0, id);
                requestQuery.bindValue(1, friendNames.value(id, id));
                requestQuery.bindValue(2, QStringLiteral("outgoing"));
                ok = requestQuery.exec() && ok;
            }
            ok ? db.commit() : db.rollback();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool ClientStorage::saveLocalGroupsToSqlite(const QString& databasePath,
                                            const QString& currentUserId,
                                            const QStringList& groupIds,
                                            const QMap<QString, QString>& groupNames,
                                            const QMap<QString, QStringList>& groupMembers,
                                            const QMap<QString, QString>& groupAnnouncements) const {
    if (databasePath.isEmpty()) {
        return false;
    }

    const QString connectionName = QStringLiteral("client_storage_groups_%1")
        .arg(QString::number(reinterpret_cast<quintptr>(this)));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (db.open()) {
            db.transaction();
            QSqlQuery clearQuery(db);
            ok = clearQuery.exec(QStringLiteral("DELETE FROM local_groups"));
            QSqlQuery insertQuery(db);
            insertQuery.prepare(QStringLiteral("INSERT OR REPLACE INTO local_groups(group_id, group_name, members, announcement, updated_at) "
                                               "VALUES(?, ?, ?, ?, datetime('now'))"));
            for (const QString& id : groupIds) {
                if (id.isEmpty()) {
                    continue;
                }
                QStringList members = groupMembers.value(id);
                if (members.isEmpty()) {
                    members << currentUserId;
                }
                insertQuery.addBindValue(id);
                insertQuery.addBindValue(groupNames.value(id, QStringLiteral("群聊")));
                insertQuery.addBindValue(members.join(QLatin1Char(',')));
                insertQuery.addBindValue(groupAnnouncements.value(id));
                ok = insertQuery.exec() && ok;
            }
            ok ? db.commit() : db.rollback();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

QString ClientStorage::appDataDirectory() const {
    const QString dir = appDataRootDirectory();
    QDir().mkpath(dir);
    return dir;
}

QString ClientStorage::safeUserName() const {
    return m_userName.isEmpty() ? QStringLiteral("guest") : m_userName;
}

QString ClientStorage::safeToken(const QString& value, const QString& fallback) const {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return fallback;
    }

    QString token;
    token.reserve(trimmed.size());
    for (const QChar ch : trimmed) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-')) {
            token.append(ch);
        } else {
            token.append(QLatin1Char('_'));
        }
    }
    return token.isEmpty() ? fallback : token;
}
