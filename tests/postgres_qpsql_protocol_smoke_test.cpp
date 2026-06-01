#include "client.h"
#include "server.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>

#include <cstdio>
#include <functional>

namespace {
QString gSmokeStep;

void setSmokeStep(const QString& step) {
    gSmokeStep = step;
    std::fprintf(stderr, "[pgsql-smoke] %s\n", step.toLocal8Bit().constData());
}

bool envEnabled(const char* name) {
    const QString value = QString::fromLocal8Bit(qgetenv(name)).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

QString testAppDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        if (!gSmokeStep.isEmpty()) {
            std::fprintf(stderr, "[pgsql-smoke] failed at %s: %s\n",
                         gSmokeStep.toLocal8Bit().constData(),
                         message);
        } else {
            std::fprintf(stderr, "[pgsql-smoke] failed: %s\n", message);
        }
        qWarning() << message;
        return false;
    }
    return true;
}

bool waitFor(const std::function<bool()>& predicate, int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

void drainEvents(int rounds = 5) {
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
}

void disconnectClient(Client& client) {
    client.disconnectFromServer();
    waitFor([&] {
        return !client.isConnected();
    }, 2000);
    drainEvents();
}

quint16 freeLocalPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) return 0;
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

QSqlDatabase openPostgres(const QString& connectionName) {
    QSqlDatabase db = QSqlDatabase::addDatabase("QPSQL", connectionName);
    db.setHostName(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGHOST")).trimmed().isEmpty()
        ? QStringLiteral("127.0.0.1")
        : QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGHOST")).trimmed());
    db.setPort(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGPORT")).trimmed().isEmpty()
        ? 5432
        : QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGPORT")).trimmed().toInt());
    db.setDatabaseName(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGDATABASE")).trimmed().isEmpty()
        ? QStringLiteral("qtnetworkchat")
        : QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGDATABASE")).trimmed());
    db.setUserName(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGUSER")).trimmed().isEmpty()
        ? QStringLiteral("postgres")
        : QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGUSER")).trimmed());
    db.setPassword(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGPASSWORD")));
    return db;
}

bool execSql(QSqlDatabase& db, const QString& sql, const QList<QVariant>& values = {}) {
    QSqlQuery query(db);
    query.prepare(sql);
    for (const QVariant& value : values) {
        query.addBindValue(value);
    }
    const bool ok = query.exec();
    if (!ok) {
        std::fprintf(stderr, "[pgsql-smoke] SQL failed at %s: %s | %s\n",
                     gSmokeStep.toLocal8Bit().constData(),
                     query.lastError().text().toLocal8Bit().constData(),
                     sql.toLocal8Bit().constData());
        qWarning() << "PostgreSQL smoke SQL failed:" << query.lastError().text() << sql;
    }
    return ok;
}

bool cleanupSmokeRows(const QString& ownerId, const QString& peerId) {
    const QString connectionName = "postgres_qpsql_cleanup";
    bool ok = true;
    {
        QSqlDatabase db = openPostgres(connectionName);
        if (!db.open()) {
            std::fprintf(stderr, "[pgsql-smoke] cleanup connection failed: %s\n",
                         db.lastError().text().toLocal8Bit().constData());
            qWarning() << "PostgreSQL cleanup connection failed:" << db.lastError().text();
            ok = false;
        } else {
            const QList<QVariant> ids{ownerId, peerId};
            ok = execSql(db, "DELETE FROM offline_messages WHERE receiver_id IN (?, ?)", ids) && ok;
            ok = execSql(db, "DELETE FROM friend_events WHERE sender_id IN (?, ?) OR receiver_id IN (?, ?) OR query_account IN (?, ?)",
                         {ownerId, peerId, ownerId, peerId, ownerId, peerId}) && ok;
            ok = execSql(db, "DELETE FROM messages WHERE sender_id IN (?, ?) OR receiver_id IN (?, ?)",
                         {ownerId, peerId, ownerId, peerId}) && ok;
            ok = execSql(db, "DELETE FROM user_sessions WHERE user_id IN (?, ?)", ids) && ok;
            ok = execSql(db, "DELETE FROM server_group_removed_members WHERE user_id IN (?, ?)", ids) && ok;
            ok = execSql(db, "DELETE FROM server_group_members WHERE user_id IN (?, ?)", ids) && ok;
            ok = execSql(db, "DELETE FROM accounts WHERE account IN (?, ?)", ids) && ok;
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

struct PublicGroupState {
    QString ownerId;
    QString ownerRole;
    QString announcement;
    bool hasGroup = false;
    bool hasOwnerMember = false;
};

bool scalarString(const QString& sql, const QList<QVariant>& values, QString* out) {
    const QString connectionName = "postgres_qpsql_scalar";
    bool ok = false;
    {
        QSqlDatabase db = openPostgres(connectionName);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(sql);
            for (const QVariant& value : values) {
                query.addBindValue(value);
            }
            if (query.exec() && query.next()) {
                if (out) *out = query.value(0).toString();
                ok = true;
            } else {
                std::fprintf(stderr, "[pgsql-smoke] scalar query failed at %s: %s | %s\n",
                             gSmokeStep.toLocal8Bit().constData(),
                             query.lastError().text().toLocal8Bit().constData(),
                             sql.toLocal8Bit().constData());
                qWarning() << "PostgreSQL scalar query failed:" << query.lastError().text() << sql;
            }
            db.close();
        } else {
            std::fprintf(stderr, "[pgsql-smoke] scalar connection failed at %s: %s\n",
                         gSmokeStep.toLocal8Bit().constData(),
                         db.lastError().text().toLocal8Bit().constData());
            qWarning() << "PostgreSQL scalar connection failed:" << db.lastError().text();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool scalarLongLong(const QString& sql, const QList<QVariant>& values, qint64* out) {
    QString value;
    if (!scalarString(sql, values, &value)) return false;
    bool converted = false;
    const qint64 number = value.toLongLong(&converted);
    if (out) *out = number;
    return converted;
}

bool capturePublicGroupState(PublicGroupState* state) {
    if (!state) return false;

    const QString connectionName = "postgres_qpsql_capture_public_group";
    bool ok = false;
    {
        QSqlDatabase db = openPostgres(connectionName);
        if (db.open()) {
            QSqlQuery groupQuery(db);
            groupQuery.prepare("SELECT COALESCE(owner_id, ''), COALESCE(announcement, '') FROM server_groups WHERE group_id = 'public'");
            if (groupQuery.exec() && groupQuery.next()) {
                state->hasGroup = true;
                state->ownerId = groupQuery.value(0).toString();
                state->announcement = groupQuery.value(1).toString();
                ok = true;
            } else if (groupQuery.lastError().type() == QSqlError::NoError) {
                ok = true;
            } else {
                std::fprintf(stderr, "[pgsql-smoke] capture public group failed: %s\n",
                             groupQuery.lastError().text().toLocal8Bit().constData());
            }

            if (ok && !state->ownerId.isEmpty()) {
                QSqlQuery roleQuery(db);
                roleQuery.prepare("SELECT COALESCE(role, '') FROM server_group_members WHERE group_id = 'public' AND user_id = ?");
                roleQuery.addBindValue(state->ownerId);
                if (roleQuery.exec() && roleQuery.next()) {
                    state->hasOwnerMember = true;
                    state->ownerRole = roleQuery.value(0).toString();
                } else if (roleQuery.lastError().type() != QSqlError::NoError) {
                    ok = false;
                    std::fprintf(stderr, "[pgsql-smoke] capture public owner role failed: %s\n",
                                 roleQuery.lastError().text().toLocal8Bit().constData());
                }
            }
            db.close();
        } else {
            std::fprintf(stderr, "[pgsql-smoke] capture public group connection failed: %s\n",
                         db.lastError().text().toLocal8Bit().constData());
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool grantSmokePublicGroupOwner(const QString& ownerId) {
    const QString connectionName = "postgres_qpsql_grant_public_owner";
    bool ok = true;
    {
        QSqlDatabase db = openPostgres(connectionName);
        if (!db.open()) {
            std::fprintf(stderr, "[pgsql-smoke] grant public owner connection failed: %s\n",
                         db.lastError().text().toLocal8Bit().constData());
            ok = false;
        } else {
            ok = execSql(db,
                         "UPDATE server_groups SET owner_id = ?, updated_at = CURRENT_TIMESTAMP WHERE group_id = 'public'",
                         {ownerId}) && ok;
            ok = execSql(db,
                         "UPDATE server_group_members SET role = CASE WHEN user_id = ? THEN 'owner' WHEN role = 'owner' THEN 'member' ELSE role END, updated_at = CURRENT_TIMESTAMP WHERE group_id = 'public'",
                         {ownerId}) && ok;
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool restorePublicGroupState(const PublicGroupState& state) {
    if (!state.hasGroup) return true;

    const QString connectionName = "postgres_qpsql_restore_public_group";
    bool ok = true;
    {
        QSqlDatabase db = openPostgres(connectionName);
        if (!db.open()) {
            std::fprintf(stderr, "[pgsql-smoke] restore public group connection failed: %s\n",
                         db.lastError().text().toLocal8Bit().constData());
            ok = false;
        } else {
            ok = execSql(db,
                         "UPDATE server_groups SET owner_id = ?, announcement = ?, updated_at = CURRENT_TIMESTAMP WHERE group_id = 'public'",
                         {state.ownerId, state.announcement}) && ok;
            if (!state.ownerId.isEmpty() && state.hasOwnerMember) {
                ok = execSql(db,
                             "UPDATE server_group_members SET role = ?, updated_at = CURRENT_TIMESTAMP WHERE group_id = 'public' AND user_id = ?",
                             {state.ownerRole.isEmpty() ? QStringLiteral("owner") : state.ownerRole, state.ownerId}) && ok;
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool loginClient(Client& client,
                 const QString& account,
                 const QString& userName,
                 quint16 port,
                 bool registerMode) {
    client.setUserInfo(account, userName);
    client.setAccountInfo(account, "pg-smoke-secret", registerMode);
    if (!client.connectToServer("127.0.0.1", port)) return false;
    return client.waitForLoginResult(5000);
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("postgres_qpsql_protocol_smoke_test");
    QStandardPaths::setTestModeEnabled(true);

    if (!envEnabled("QTNETWORKCHAT_RUN_REAL_QPSQL_TEST")) {
        qInfo() << "Skipping real PostgreSQL QPSQL smoke; set QTNETWORKCHAT_RUN_REAL_QPSQL_TEST=1 to enable it.";
        return 0;
    }

    bool ok = true;
    ok = expect(QSqlDatabase::drivers().contains("QPSQL"),
                "Qt QPSQL driver should be available for real PostgreSQL smoke") && ok;
    ok = expect(!QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGPASSWORD")).isEmpty(),
                "QTNETWORKCHAT_PGPASSWORD should be set for real PostgreSQL smoke") && ok;
    if (!ok) return 1;

    qputenv("QTNETWORKCHAT_DB_DRIVER", "QPSQL");
    qunsetenv("QTNETWORKCHAT_REDIS");

    const QString appDataDir = testAppDataDir();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    const QString suffix = QString::number(QDateTime::currentMSecsSinceEpoch() % 100000000LL).rightJustified(8, '0');
    const QString ownerId = "94" + suffix.left(6);
    const QString peerId = "95" + suffix.right(6);
    PublicGroupState publicGroupState;
    setSmokeStep(QStringLiteral("capture public group state"));
    ok = expect(capturePublicGroupState(&publicGroupState),
                "public group state should be captured before smoke overrides") && ok;
    if (!ok) return 1;

    setSmokeStep(QStringLiteral("cleanup previous smoke rows"));
    cleanupSmokeRows(ownerId, peerId);

    setSmokeStep(QStringLiteral("allocate local port"));
    const quint16 port = freeLocalPort();
    ok = expect(port != 0, "a local PostgreSQL smoke test port should be available") && ok;
    if (!ok) return 1;

    {
        setSmokeStep(QStringLiteral("start QPSQL-backed server"));
        Server server;
        ok = expect(server.start(port), "server should start with QPSQL account database") && ok;
        if (!ok) {
            cleanupSmokeRows(ownerId, peerId);
            return 1;
        }

        Client owner;
        Client peer;
        QStringList peerPrivateMessages;
        QStringList peerOfflineMessages;
        QStringList peerFiles;
        QStringList ownerSystemMessages;
        QStringList ownerFriendSearchReasons;
        QStringList peerFriendRequests;
        QStringList ownerFriendResponses;
        QStringList ownerFriendRequestSentResults;
        QStringList peerGroupNotices;
        QJsonArray peerRemovedGroups;
        QObject::connect(&peer, &Client::newMessage, &app, [&](const Message& msg) {
            if (msg.type == MessageType::Private) {
                peerPrivateMessages << msg.content;
            } else if (msg.type == MessageType::File) {
                peerFiles << msg.fileName;
            }
        });
        QObject::connect(&peer, &Client::friendRequestReceived, &app, [&](const QString& senderId, const QString&) {
            peerFriendRequests << senderId;
        });
        QObject::connect(&peer, &Client::newMessage, &app, [&](const Message& msg) {
            if (msg.type == MessageType::System) {
                peerGroupNotices << msg.content;
            }
        });
        QObject::connect(&owner, &Client::friendSearchResult, &app, [&](const QString&, const QString&, const QString&, bool, bool, bool, int, const QString& reason) {
            ownerFriendSearchReasons << reason;
        });
        QObject::connect(&owner, &Client::friendResponseReceived, &app, [&](const QString& senderId, const QString&, bool accepted) {
            ownerFriendResponses << QString("%1:%2").arg(senderId, accepted ? "accepted" : "rejected");
        });
        QObject::connect(&owner, &Client::friendRequestSent, &app, [&](const QString& receiverId, bool delivered) {
            ownerFriendRequestSentResults << QString("%1:%2").arg(receiverId, delivered ? "delivered" : "queued");
        });
        QObject::connect(&owner, &Client::newMessage, &app, [&](const Message& msg) {
            if (msg.type == MessageType::System) {
                ownerSystemMessages << msg.content;
            }
        });
        QObject::connect(&peer, &Client::serverGroupSnapshotReceived, &app, [&](const QJsonArray&) {
            peerRemovedGroups = peer.removedServerGroups();
        });

        setSmokeStep(QStringLiteral("register owner"));
        ok = expect(loginClient(owner, ownerId, "PgOwner", port, true),
                    "owner should register through PostgreSQL") && ok;
        setSmokeStep(QStringLiteral("register peer"));
        ok = expect(loginClient(peer, peerId, "PgPeer", port, true),
                    "peer should register through PostgreSQL") && ok;
        setSmokeStep(QStringLiteral("grant smoke public group owner"));
        ok = expect(grantSmokePublicGroupOwner(ownerId),
                    "smoke owner should temporarily own public group") && ok;

        setSmokeStep(QStringLiteral("private message persistence"));
        const QString privateMessage = "PostgreSQL QPSQL direct message smoke";
        ok = expect(owner.sendPrivateMessage(peerId, privateMessage),
                    "owner should send a private message through the QPSQL-backed server") && ok;
        ok = expect(waitFor([&] { return peerPrivateMessages.contains(privateMessage); }),
                    "peer should receive the private message") && ok;

        QString persistedContent;
        ok = expect(waitFor([&] {
            return scalarString("SELECT content FROM messages WHERE sender_id = ? AND receiver_id = ? ORDER BY id DESC LIMIT 1",
                                {ownerId, peerId},
                                &persistedContent)
                && persistedContent == privateMessage;
        }), "private message should be persisted in PostgreSQL") && ok;

        setSmokeStep(QStringLiteral("friend search/request/response persistence"));
        ok = expect(owner.searchFriendByAccount(peerId),
                    "owner should search peer through PostgreSQL account data") && ok;
        ok = expect(waitFor([&] { return ownerFriendSearchReasons.contains(QStringLiteral("QQ号精确匹配")); }),
                    "friend search should find peer by exact account") && ok;
        ok = expect(owner.sendFriendRequest(peerId),
                    "owner should send friend request through PostgreSQL-backed server") && ok;
        ok = expect(waitFor([&] { return peerFriendRequests.contains(ownerId); }),
                    "peer should receive friend request") && ok;
        ok = expect(peer.sendFriendResponse(ownerId, true),
                    "peer should accept friend request") && ok;
        ok = expect(waitFor([&] { return ownerFriendResponses.contains(peerId + ":accepted"); }),
                    "owner should receive friend response") && ok;
        qint64 friendEvents = 0;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM friend_events WHERE sender_id IN (?, ?) OR receiver_id IN (?, ?) OR query_account IN (?, ?)",
                                  {ownerId, peerId, ownerId, peerId, ownerId, peerId},
                                  &friendEvents)
                && friendEvents >= 3;
        }), "friend search/request/response events should be persisted in PostgreSQL") && ok;

        setSmokeStep(QStringLiteral("friend boundary event persistence"));
        ok = expect(owner.searchFriendByAccount("00" + suffix),
                    "owner should be able to search a missing PostgreSQL account") && ok;
        ok = expect(waitFor([&] { return ownerFriendSearchReasons.contains(QStringLiteral("未找到匹配资料")); }),
                    "missing friend search should return not-found boundary reason") && ok;
        const int friendRequestResultsBeforeDuplicate = ownerFriendRequestSentResults.size();
        ok = expect(owner.sendFriendRequest(peerId),
                    "duplicate friend request should still be recorded as a boundary event") && ok;
        ok = expect(waitFor([&] { return ownerFriendRequestSentResults.size() > friendRequestResultsBeforeDuplicate; }),
                    "duplicate friend request should produce a sent/queued result") && ok;
        ok = expect(peer.sendFriendResponse(ownerId, false),
                    "peer should be able to reject an already-known requester as a boundary event") && ok;
        qint64 boundaryEvents = 0;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM friend_events WHERE (event_type = 'friend_search' AND query_account = ?) OR (event_type IN ('friend_request','friend_response') AND sender_id IN (?, ?) AND receiver_id IN (?, ?))",
                                  {"00" + suffix, ownerId, peerId, ownerId, peerId},
                                  &boundaryEvents)
                && boundaryEvents >= 3;
        }), "friend boundary search/request/reject events should be persisted in PostgreSQL") && ok;

        setSmokeStep(QStringLiteral("public group announcement and audit persistence"));
        const QString announcement = "PostgreSQL group announcement smoke";
        ok = expect(owner.sendServerGroupAnnouncementUpdate("public", announcement),
                    "owner should update public group announcement through PostgreSQL") && ok;
        ok = expect(waitFor([&] {
            for (const QString& notice : peerGroupNotices) {
                if (notice.contains(QString::fromUtf8("更新了群公告"))) return true;
            }
            return false;
        }), "peer should receive public group announcement notice") && ok;
        QString persistedAnnouncement;
        ok = expect(waitFor([&] {
            return scalarString("SELECT announcement FROM server_groups WHERE group_id = 'public'", {}, &persistedAnnouncement)
                && persistedAnnouncement == announcement;
        }), "public group announcement should be persisted in PostgreSQL") && ok;
        qint64 auditEvents = 0;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM server_group_audit_events WHERE group_id = 'public' AND actor_id = ? AND action = 'announcement_update'",
                                  {ownerId},
                                  &auditEvents)
                && auditEvents >= 1;
        }), "public group audit event should be persisted in PostgreSQL") && ok;

        setSmokeStep(QStringLiteral("public group member role persistence"));
        ok = expect(owner.sendServerGroupMemberUpdate("public", peerId, "promote_admin"),
                    "owner should promote peer to public group admin through PostgreSQL") && ok;
        QString persistedRole;
        ok = expect(waitFor([&] {
            return scalarString("SELECT role FROM server_group_members WHERE group_id = 'public' AND user_id = ?",
                                {peerId},
                                &persistedRole)
                && persistedRole == QStringLiteral("admin");
        }), "public group admin promotion should be persisted in PostgreSQL") && ok;
        qint64 roleAuditEvents = 0;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM server_group_audit_events WHERE group_id = 'public' AND actor_id = ? AND target_user_id = ? AND action = 'promote_admin'",
                                  {ownerId, peerId},
                                  &roleAuditEvents)
                && roleAuditEvents >= 1;
        }), "public group admin promotion audit should be persisted in PostgreSQL") && ok;
        ok = expect(owner.sendServerGroupMemberUpdate("public", peerId, "demote_admin"),
                    "owner should demote peer back to member through PostgreSQL") && ok;
        ok = expect(waitFor([&] {
            return scalarString("SELECT role FROM server_group_members WHERE group_id = 'public' AND user_id = ?",
                                {peerId},
                                &persistedRole)
                && persistedRole == QStringLiteral("member");
        }), "public group admin demotion should be persisted in PostgreSQL") && ok;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM server_group_audit_events WHERE group_id = 'public' AND actor_id = ? AND target_user_id = ? AND action = 'demote_admin'",
                                  {ownerId, peerId},
                                  &roleAuditEvents)
                && roleAuditEvents >= 1;
        }), "public group admin demotion audit should be persisted in PostgreSQL") && ok;

        setSmokeStep(QStringLiteral("public group remove re-add marker persistence"));
        ok = expect(owner.sendServerGroupMemberUpdate("public", peerId, "remove"),
                    "owner should remove peer from public group through PostgreSQL") && ok;
        qint64 removedMarkers = 0;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM server_group_removed_members WHERE group_id = 'public' AND user_id = ? AND removed_by = ?",
                                  {peerId, ownerId},
                                  &removedMarkers)
                && removedMarkers == 1;
        }), "public group removed marker should be persisted in PostgreSQL") && ok;
        ok = expect(waitFor([&] {
            for (const QJsonValue& value : peerRemovedGroups) {
                const QJsonObject obj = value.toObject();
                if (obj.value("groupId").toString() == QStringLiteral("public")) return true;
            }
            return false;
        }, 8000), "removed peer should receive a public group history marker") && ok;
        ok = expect(owner.sendServerGroupMemberUpdate("public", peerId, "add"),
                    "owner should re-add peer to public group through PostgreSQL") && ok;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM server_group_removed_members WHERE group_id = 'public' AND user_id = ?",
                                  {peerId},
                                  &removedMarkers)
                && removedMarkers == 0;
        }), "public group re-add should clear removed marker in PostgreSQL") && ok;
        ok = expect(waitFor([&] {
            return scalarString("SELECT role FROM server_group_members WHERE group_id = 'public' AND user_id = ?",
                                {peerId},
                                &persistedRole)
                && persistedRole == QStringLiteral("member");
        }), "public group re-add should restore member row in PostgreSQL") && ok;

        setSmokeStep(QStringLiteral("file metadata persistence"));
        const QString testFilePath = QDir(appDataDir).filePath("pgsql-smoke-file.txt");
        QFile testFile(testFilePath);
        ok = expect(testFile.open(QIODevice::WriteOnly), "PostgreSQL smoke file should be writable") && ok;
        if (testFile.isOpen()) {
            testFile.write("PostgreSQL QPSQL file metadata smoke");
            testFile.close();
        }
        ok = expect(owner.sendFile(testFilePath, peerId),
                    "owner should send file through PostgreSQL-backed server") && ok;
        ok = expect(waitFor([&] { return peerFiles.contains(QStringLiteral("pgsql-smoke-file.txt")); }, 8000),
                    "peer should receive file metadata") && ok;
        qint64 fileRows = 0;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM messages WHERE sender_id = ? AND receiver_id = ? AND file_name = 'pgsql-smoke-file.txt' AND file_size > 0 AND file_hash IS NOT NULL AND file_chunk_size > 0 AND file_chunk_count > 0",
                                  {ownerId, peerId},
                                  &fileRows)
                && fileRows >= 1;
        }), "file transfer chunk metadata should be persisted in PostgreSQL") && ok;

        setSmokeStep(QStringLiteral("offline private message and attachment queue replay"));
        disconnectClient(peer);
        const QString offlineMessage = "PostgreSQL QPSQL offline private smoke";
        ok = expect(waitFor([&] { return !peer.isConnected(); }, 2000),
                    "peer should be offline before offline message smoke") && ok;
        ok = expect(owner.sendPrivateMessage(peerId, offlineMessage),
                    "owner should send offline private message") && ok;
        const QString offlineFilePath = QDir(appDataDir).filePath("pgsql-smoke-offline-file.txt");
        QFile offlineFile(offlineFilePath);
        ok = expect(offlineFile.open(QIODevice::WriteOnly),
                    "PostgreSQL offline attachment smoke file should be writable") && ok;
        if (offlineFile.isOpen()) {
            offlineFile.write("PostgreSQL QPSQL offline attachment smoke");
            offlineFile.close();
        }
        ok = expect(owner.sendFile(offlineFilePath, peerId),
                    "owner should queue an offline attachment through PostgreSQL") && ok;
        qint64 offlineRows = 0;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM offline_messages WHERE receiver_id = ?",
                                  {peerId},
                                  &offlineRows)
                && offlineRows >= 2;
        }), "offline private message and attachment should be queued in PostgreSQL") && ok;
        qint64 offlineAttachmentRows = 0;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM offline_messages WHERE receiver_id = ? AND payload LIKE '%pgsql-smoke-offline-file.txt%' AND payload LIKE '%chunkSize%' AND payload LIKE '%chunkCount%'",
                                  {peerId},
                                  &offlineAttachmentRows)
                && offlineAttachmentRows >= 1;
        }), "offline attachment payload should keep chunk metadata in PostgreSQL") && ok;

        QObject::disconnect(&peer, nullptr, &app, nullptr);
        QObject::connect(&peer, &Client::newMessage, &app, [&](const Message& msg) {
            if (msg.type == MessageType::Private) {
                peerOfflineMessages << msg.content;
            } else if (msg.type == MessageType::File) {
                peerFiles << msg.fileName;
            }
        });
        ok = expect(loginClient(peer, peerId, "PgPeer", port, false),
                    "peer should re-login to replay PostgreSQL offline message") && ok;
        ok = expect(waitFor([&] { return peerOfflineMessages.contains(offlineMessage); }, 8000),
                    "peer should receive PostgreSQL offline private message") && ok;
        ok = expect(waitFor([&] { return peerFiles.contains(QStringLiteral("pgsql-smoke-offline-file.txt")); }, 8000),
                    "peer should receive PostgreSQL offline attachment replay") && ok;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM offline_messages WHERE receiver_id = ?",
                                  {peerId},
                                  &offlineRows)
                && offlineRows == 0;
        }), "PostgreSQL offline queue should be cleared after replay") && ok;

        disconnectClient(owner);
        disconnectClient(peer);
        server.stop();
        drainEvents();
    }

    {
        setSmokeStep(QStringLiteral("restart QPSQL-backed server"));
        Server server;
        ok = expect(server.start(port), "server should restart with the same QPSQL database") && ok;
        Client relogin;
        ok = expect(loginClient(relogin, ownerId, "PgOwner", port, false),
                    "registered PostgreSQL account should log in after server restart") && ok;

        QString passwordHash;
        ok = expect(scalarString("SELECT password_hash FROM accounts WHERE account = ?", {ownerId}, &passwordHash),
                    "PostgreSQL account hash should be queryable") && ok;
        ok = expect(passwordHash.startsWith("kdf$pbkdf2-sha256$"),
                    "PostgreSQL account should store the PBKDF2 KDF hash format") && ok;

        qint64 loginSessions = 0;
        ok = expect(scalarLongLong("SELECT COUNT(*) FROM user_sessions WHERE user_id = ? AND event_name = 'login'",
                                   {ownerId},
                                   &loginSessions)
                        && loginSessions >= 2,
                    "PostgreSQL user_sessions should record login and restart login") && ok;

        disconnectClient(relogin);
        server.stop();
        drainEvents();
    }

    setSmokeStep(QStringLiteral("cleanup current smoke rows"));
    ok = restorePublicGroupState(publicGroupState) && ok;
    ok = cleanupSmokeRows(ownerId, peerId) && ok;
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
