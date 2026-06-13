#include "clientstorage.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QImage>
#include <QBuffer>
#include <QGuiApplication>
#include <QPixmap>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextStream>
#include <QVariant>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        QTextStream(stderr) << message << Qt::endl;
        qWarning() << message;
        return false;
    }
    return true;
}

bool execSql(QSqlDatabase& db, const QString& sql) {
    QSqlQuery query(db);
    return query.exec(sql);
}

bool createClientTables(const QString& databasePath) {
    const QString connectionName = QStringLiteral("client_storage_test_schema");
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (db.open()) {
            ok = execSql(db, QStringLiteral("CREATE TABLE friends (user_id TEXT PRIMARY KEY, display_name TEXT NOT NULL, updated_at TEXT DEFAULT CURRENT_TIMESTAMP)"))
                && execSql(db, QStringLiteral("CREATE TABLE friend_requests (request_id TEXT NOT NULL, display_name TEXT NOT NULL, direction TEXT NOT NULL, status TEXT NOT NULL, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY(request_id, direction))"))
                && execSql(db, QStringLiteral("CREATE TABLE local_groups (group_id TEXT PRIMARY KEY, group_name TEXT NOT NULL, members TEXT NOT NULL, announcement TEXT, updated_at TEXT DEFAULT CURRENT_TIMESTAMP)"))
                && execSql(db, QStringLiteral("CREATE TABLE profile (user_id TEXT PRIMARY KEY, user_name TEXT NOT NULL, avatar_path TEXT, updated_at TEXT DEFAULT CURRENT_TIMESTAMP)"))
                && execSql(db, QStringLiteral("CREATE TABLE peer_avatars (user_id TEXT PRIMARY KEY, avatar_path TEXT NOT NULL, updated_at TEXT DEFAULT CURRENT_TIMESTAMP)"));
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

int countRows(const QString& databasePath, const QString& tableName) {
    const QString connectionName = QStringLiteral("client_storage_test_count_") + tableName;
    int count = -1;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (db.open()) {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(tableName)) && query.next()) {
                count = query.value(0).toInt();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return count;
}
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QtNetworkChatTests"));
    QCoreApplication::setApplicationName(QStringLiteral("client_storage_test"));
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    bool ok = true;
    ClientStorage storage(QStringLiteral("Alice"));
    ok = expect(storage.friendFilePath().endsWith(QStringLiteral("friends_Alice.txt"))
                    && storage.groupFilePath().endsWith(QStringLiteral("groups_Alice.txt"))
                    && storage.avatarFilePath().endsWith(QStringLiteral("avatar_Alice.png")),
                "storage paths should use current user name") && ok;
    storage.setUserName(QString());
    ok = expect(storage.friendFilePath().endsWith(QStringLiteral("friends_guest.txt")),
                "empty user name should use guest path fallback") && ok;
    storage.setUserName(QStringLiteral("Alice"));

    QImage peerAvatarImage(3, 3, QImage::Format_ARGB32);
    peerAvatarImage.fill(QColor(12, 140, 220));
    QByteArray peerAvatarBytes;
    {
        QBuffer buffer(&peerAvatarBytes);
        buffer.open(QIODevice::WriteOnly);
        peerAvatarImage.save(&buffer, "PNG");
    }
    ok = expect(storage.savePeerAvatar(QStringLiteral("peer_1001"), peerAvatarBytes),
                "peer avatar should save to local cache") && ok;
    const QPixmap peerAvatarPixmap = storage.loadPeerAvatar(QStringLiteral("peer_1001"));
    ok = expect(!peerAvatarPixmap.isNull()
                    && storage.peerAvatarFilePath(QStringLiteral("peer_1001")).contains(QStringLiteral("peer_avatars")),
                "peer avatar cache should round-trip through local storage") && ok;
    ok = expect(storage.loadPeerAvatar(QStringLiteral("missing_peer")).isNull(),
                "missing peer avatar should return a null pixmap") && ok;

    QStringList friendIds{QStringLiteral("1001"), QStringLiteral("1002")};
    QMap<QString, QString> friendNames;
    friendNames.insert(QStringLiteral("1001"), QStringLiteral("Alice Friend"));
    friendNames.insert(QStringLiteral("1002"), QStringLiteral("Bob Friend"));
    ok = expect(storage.writeLegacyFriends(friendIds, friendNames),
                "legacy friends should write") && ok;
    QStringList loadedFriendIds;
    QMap<QString, QString> loadedFriendNames;
    ok = expect(storage.readLegacyFriends(&loadedFriendIds, &loadedFriendNames)
                    && loadedFriendIds == friendIds
                    && loadedFriendNames.value(QStringLiteral("1002")) == QStringLiteral("Bob Friend"),
                "legacy friends should round-trip") && ok;

    QStringList groupIds{QStringLiteral("local_group_1"), QStringLiteral("local_group_2")};
    QMap<QString, QString> groupNames;
    groupNames.insert(QStringLiteral("local_group_1"), QStringLiteral("Dev Group"));
    groupNames.insert(QStringLiteral("local_group_2"), QStringLiteral("Ops Group"));
    QMap<QString, QStringList> groupMembers;
    groupMembers.insert(QStringLiteral("local_group_1"), QStringList{QStringLiteral("self"), QStringLiteral("1001")});
    QMap<QString, QString> groupAnnouncements;
    groupAnnouncements.insert(QStringLiteral("local_group_1"), QStringLiteral("Daily sync"));
    ok = expect(storage.writeLegacyLocalGroups(QStringLiteral("self"), groupIds, groupNames, groupMembers, groupAnnouncements),
                "legacy groups should write") && ok;
    QStringList loadedGroupIds;
    QMap<QString, QString> loadedGroupNames;
    QMap<QString, QStringList> loadedGroupMembers;
    QMap<QString, QString> loadedGroupAnnouncements;
    ok = expect(storage.readLegacyLocalGroups(QStringLiteral("self"),
                                              &loadedGroupIds,
                                              &loadedGroupNames,
                                              &loadedGroupMembers,
                                              &loadedGroupAnnouncements)
                    && loadedGroupIds == groupIds
                    && loadedGroupMembers.value(QStringLiteral("local_group_2")) == QStringList{QStringLiteral("self")}
                    && loadedGroupAnnouncements.value(QStringLiteral("local_group_2")).contains(QStringLiteral("Ops Group")),
                "legacy groups should round-trip with default members and announcements") && ok;

    const QString databasePath = QDir(appDataDir).filePath(QStringLiteral("client.sqlite3"));
    ok = expect(createClientTables(databasePath), "sqlite tables should be created") && ok;
    ok = expect(storage.saveProfileToSqlite(databasePath,
                                            QStringLiteral("self"),
                                            QStringLiteral("Alice"),
                                            storage.avatarFilePath()),
                "profile should save to sqlite") && ok;
    ok = expect(storage.savePeerAvatarToSqlite(databasePath,
                                               QStringLiteral("peer_1001"),
                                               storage.peerAvatarFilePath(QStringLiteral("peer_1001"))),
                "peer avatar index should save to sqlite") && ok;
    ok = expect(storage.saveFriendsToSqlite(databasePath,
                                            friendIds,
                                            friendNames,
                                            QStringList{QStringLiteral("2001"), QStringLiteral("1001")},
                                            QStringList{QStringLiteral("2002")}),
                "friends and pending requests should save to sqlite") && ok;
    ok = expect(storage.saveLocalGroupsToSqlite(databasePath,
                                                QStringLiteral("self"),
                                                groupIds,
                                                groupNames,
                                                groupMembers,
                                                groupAnnouncements),
                "local groups should save to sqlite") && ok;
    ok = expect(countRows(databasePath, QStringLiteral("profile")) == 1
                    && countRows(databasePath, QStringLiteral("peer_avatars")) == 1
                    && countRows(databasePath, QStringLiteral("friends")) == 2
                    && countRows(databasePath, QStringLiteral("friend_requests")) == 2
                    && countRows(databasePath, QStringLiteral("local_groups")) == 2,
                "sqlite persistence should write expected row counts and skip requests for existing friends") && ok;

    const QMap<QString, QString> peerAvatarIndex = storage.loadPeerAvatarIndexFromSqlite(databasePath);
    ok = expect(peerAvatarIndex.value(QStringLiteral("peer_1001")) == storage.peerAvatarFilePath(QStringLiteral("peer_1001")),
                "peer avatar index should load back from sqlite") && ok;

    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
