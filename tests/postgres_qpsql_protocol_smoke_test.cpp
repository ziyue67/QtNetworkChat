#include "client.h"
#include "server.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
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
    bool ok = false;
    if (values.isEmpty()) {
        ok = query.exec(sql);
    } else {
        query.prepare(sql);
        for (const QVariant& value : values) {
            query.addBindValue(value);
        }
        ok = query.exec();
    }
    if (!ok) {
        std::fprintf(stderr, "[pgsql-smoke] SQL failed at %s: %s | %s\n",
                     gSmokeStep.toLocal8Bit().constData(),
                     query.lastError().text().toLocal8Bit().constData(),
                     sql.toLocal8Bit().constData());
        qWarning() << "PostgreSQL smoke SQL failed:" << query.lastError().text() << sql;
    }
    return ok;
}

QString sqlStringLiteral(QString value) {
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}

QString sqlInList(const QStringList& values) {
    QStringList quoted;
    quoted.reserve(values.size());
    for (const QString& value : values) {
        quoted.append(sqlStringLiteral(value));
    }
    return quoted.join(QStringLiteral(", "));
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
            const QString ids = sqlInList({ownerId, peerId});
            ok = execSql(db, QStringLiteral("DELETE FROM offline_messages WHERE receiver_id IN (%1)").arg(ids)) && ok;
            ok = execSql(db, QStringLiteral("DELETE FROM friend_events WHERE sender_id IN (%1) OR receiver_id IN (%1) OR query_account IN (%1)").arg(ids)) && ok;
            ok = execSql(db, QStringLiteral("DELETE FROM messages WHERE sender_id IN (%1) OR receiver_id IN (%1)").arg(ids)) && ok;
            ok = execSql(db, QStringLiteral("DELETE FROM user_sessions WHERE user_id IN (%1)").arg(ids)) && ok;
            ok = execSql(db, QStringLiteral("DELETE FROM server_group_removed_members WHERE user_id IN (%1)").arg(ids)) && ok;
            ok = execSql(db, QStringLiteral("DELETE FROM server_group_members WHERE user_id IN (%1)").arg(ids)) && ok;
            ok = execSql(db, QStringLiteral("DELETE FROM accounts WHERE account IN (%1)").arg(ids)) && ok;
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
            if (!db.tables().contains(QStringLiteral("server_groups"), Qt::CaseInsensitive)) {
                ok = true;
            } else {
                QSqlQuery groupQuery(db);
                if (groupQuery.exec("SELECT COALESCE(owner_id, ''), COALESCE(announcement, '') FROM server_groups WHERE group_id = 'public'")
                    && groupQuery.next()) {
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

bool loginRawAckChunksThenDisconnect(const QString& account,
                                     const QString& userName,
                                     quint16 port,
                                     int chunksToAck,
                                     qint64* lastReceivedBytes = nullptr,
                                     QVector<qint64>* receivedChunkIndexes = nullptr,
                                     int timeoutMs = 10000) {
    QTcpSocket socket;
    QByteArray buffer;
    bool sawLoginSuccess = false;
    int ackedChunks = 0;
    qint64 receivedBytes = 0;

    auto drainSocket = [&]() {
        buffer.append(socket.readAll());
        while (buffer.contains('\n')) {
            const int newlineIndex = buffer.indexOf('\n');
            const QByteArray line = buffer.left(newlineIndex);
            buffer = buffer.mid(newlineIndex + 1);
            if (line.trimmed().isEmpty()) continue;

            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) continue;
            const QJsonObject obj = doc.object();
            const QString type = obj["type"].toString();
            if (type == "login_success") {
                sawLoginSuccess = true;
            } else if (type == "file_chunk" && ackedChunks < chunksToAck) {
                const qint64 chunkIndex = obj["chunkIndex"].toVariant().toLongLong();
                const qint64 chunkSize = obj["chunkSize"].toVariant().toLongLong();
                const qint64 fileSize = obj["fileSize"].toVariant().toLongLong();
                const QByteArray chunkData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());
                receivedBytes = qMin(fileSize, chunkIndex * chunkSize + chunkData.size());
                if (receivedChunkIndexes) {
                    receivedChunkIndexes->append(chunkIndex);
                }

                QJsonObject ack;
                ack["type"] = "file_chunk_ack";
                ack["transferId"] = obj["transferId"].toString();
                ack["chunkIndex"] = obj["chunkIndex"].toString();
                ack["accepted"] = true;
                ack["reason"] = "";
                ack["receivedBytes"] = QString::number(receivedBytes);
                socket.write(QJsonDocument(ack).toJson(QJsonDocument::Compact));
                socket.write("\n");
                socket.flush();
                socket.waitForBytesWritten(1000);
                ++ackedChunks;
                if (ackedChunks >= chunksToAck) {
                    socket.disconnectFromHost();
                    return;
                }
            }
        }
    };
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, drainSocket);

    socket.connectToHost("127.0.0.1", port);
    if (!socket.waitForConnected(timeoutMs)) return false;

    QJsonObject login;
    login["type"] = "login";
    login["mode"] = "login";
    login["account"] = account;
    login["password"] = "pg-smoke-secret";
    login["userName"] = userName;
    socket.write(QJsonDocument(login).toJson(QJsonDocument::Compact));
    socket.write("\n");
    socket.flush();

    const bool ackedRequestedChunks = waitFor([&] {
        drainSocket();
        return ackedChunks >= chunksToAck;
    }, timeoutMs);
    if (socket.state() != QAbstractSocket::UnconnectedState) {
        socket.disconnectFromHost();
    }
    socket.waitForDisconnected(1000);
    if (lastReceivedBytes) *lastReceivedBytes = receivedBytes;
    return sawLoginSuccess && ackedRequestedChunks;
}

bool loginRawAckFileReplay(const QString& account,
                           const QString& userName,
                           quint16 port,
                           QVector<qint64>* receivedChunkIndexes = nullptr,
                           qint64* lastReceivedBytes = nullptr,
                           int timeoutMs = 10000) {
    QTcpSocket socket;
    QByteArray buffer;
    bool sawLoginSuccess = false;
    bool completedReplay = false;
    qint64 receivedBytes = 0;

    auto drainSocket = [&]() {
        buffer.append(socket.readAll());
        while (buffer.contains('\n')) {
            const int newlineIndex = buffer.indexOf('\n');
            const QByteArray line = buffer.left(newlineIndex);
            buffer = buffer.mid(newlineIndex + 1);
            if (line.trimmed().isEmpty()) continue;

            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) continue;
            const QJsonObject obj = doc.object();
            const QString type = obj["type"].toString();
            if (type == "login_success") {
                sawLoginSuccess = true;
            } else if (type == "file_chunk") {
                const qint64 chunkIndex = obj["chunkIndex"].toVariant().toLongLong();
                const qint64 chunkSize = obj["chunkSize"].toVariant().toLongLong();
                const qint64 fileSize = obj["fileSize"].toVariant().toLongLong();
                const QByteArray chunkData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());
                receivedBytes = qMin(fileSize, chunkIndex * chunkSize + chunkData.size());
                if (receivedChunkIndexes) {
                    receivedChunkIndexes->append(chunkIndex);
                }

                QJsonObject ack;
                ack["type"] = "file_chunk_ack";
                ack["transferId"] = obj["transferId"].toString();
                ack["chunkIndex"] = obj["chunkIndex"].toString();
                ack["accepted"] = true;
                ack["reason"] = "";
                ack["receivedBytes"] = QString::number(receivedBytes);
                socket.write(QJsonDocument(ack).toJson(QJsonDocument::Compact));
                socket.write("\n");
                socket.flush();
                socket.waitForBytesWritten(1000);

                if (receivedBytes >= fileSize) {
                    completedReplay = true;
                    socket.disconnectFromHost();
                    return;
                }
            }
        }
    };
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, drainSocket);

    socket.connectToHost("127.0.0.1", port);
    if (!socket.waitForConnected(timeoutMs)) return false;

    QJsonObject login;
    login["type"] = "login";
    login["mode"] = "login";
    login["account"] = account;
    login["password"] = "pg-smoke-secret";
    login["userName"] = userName;
    socket.write(QJsonDocument(login).toJson(QJsonDocument::Compact));
    socket.write("\n");
    socket.flush();

    const bool ackedReplay = waitFor([&] {
        drainSocket();
        return completedReplay;
    }, timeoutMs);
    if (socket.state() != QAbstractSocket::UnconnectedState) {
        socket.disconnectFromHost();
    }
    socket.waitForDisconnected(1000);
    if (lastReceivedBytes) *lastReceivedBytes = receivedBytes;
    return sawLoginSuccess && ackedReplay;
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
        QStringList peerSystemMessages;
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

        setSmokeStep(QStringLiteral("online file chunk retry boundary"));
        disconnectClient(peer);
        ok = expect(waitFor([&] { return !peer.isConnected(); }, 2000),
                    "peer should be disconnected before raw online file retry smoke") && ok;
        QTcpSocket rawPeerSocket;
        QByteArray rawPeerBuffer;
        bool rawPeerLoginOk = false;
        bool rawPeerCompletedFile = false;
        int rawPeerChunkAttempts = 0;
        QVector<qint64> rawPeerChunkIndexes;
        auto drainRawPeerSocket = [&]() {
            rawPeerBuffer.append(rawPeerSocket.readAll());
            while (rawPeerBuffer.contains('\n')) {
                const int newlineIndex = rawPeerBuffer.indexOf('\n');
                const QByteArray line = rawPeerBuffer.left(newlineIndex);
                rawPeerBuffer = rawPeerBuffer.mid(newlineIndex + 1);
                if (line.trimmed().isEmpty()) continue;

                const QJsonDocument doc = QJsonDocument::fromJson(line);
                if (!doc.isObject()) continue;
                const QJsonObject obj = doc.object();
                const QString type = obj["type"].toString();
                if (type == "login_success") {
                    rawPeerLoginOk = true;
                } else if (type == "file_chunk") {
                    ++rawPeerChunkAttempts;
                    const qint64 chunkIndex = obj["chunkIndex"].toVariant().toLongLong();
                    const qint64 chunkSize = obj["chunkSize"].toVariant().toLongLong();
                    const qint64 fileSize = obj["fileSize"].toVariant().toLongLong();
                    const QByteArray chunkData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());
                    const qint64 receivedBytes = qMin(fileSize, chunkIndex * chunkSize + chunkData.size());
                    rawPeerChunkIndexes.append(chunkIndex);

                    QJsonObject ack;
                    ack["type"] = "file_chunk_ack";
                    ack["transferId"] = obj["transferId"].toString();
                    ack["chunkIndex"] = obj["chunkIndex"].toString();
                    ack["accepted"] = true;
                    ack["reason"] = "";
                    ack["receivedBytes"] = QString::number(rawPeerChunkAttempts == 1 ? 1 : receivedBytes);
                    rawPeerSocket.write(QJsonDocument(ack).toJson(QJsonDocument::Compact));
                    rawPeerSocket.write("\n");
                    rawPeerSocket.flush();
                    rawPeerSocket.waitForBytesWritten(1000);
                    if (rawPeerChunkAttempts > 1 && receivedBytes >= fileSize) {
                        rawPeerCompletedFile = true;
                    }
                }
            }
        };
        QObject::connect(&rawPeerSocket, &QTcpSocket::readyRead, &app, drainRawPeerSocket);
        rawPeerSocket.connectToHost("127.0.0.1", port);
        ok = expect(rawPeerSocket.waitForConnected(5000),
                    "raw peer should connect before online file retry smoke") && ok;
        QJsonObject rawPeerLogin;
        rawPeerLogin["type"] = "login";
        rawPeerLogin["mode"] = "login";
        rawPeerLogin["account"] = peerId;
        rawPeerLogin["password"] = "pg-smoke-secret";
        rawPeerLogin["userName"] = "PgPeer";
        rawPeerSocket.write(QJsonDocument(rawPeerLogin).toJson(QJsonDocument::Compact));
        rawPeerSocket.write("\n");
        rawPeerSocket.flush();
        ok = expect(waitFor([&] {
            drainRawPeerSocket();
            return rawPeerLoginOk;
        }, 5000), "raw peer should log in for online file retry smoke") && ok;

        const QString retryFilePath = QDir(appDataDir).filePath("pgsql-smoke-online-retry-file.bin");
        QFile retryFile(retryFilePath);
        ok = expect(retryFile.open(QIODevice::WriteOnly),
                    "PostgreSQL online retry file should be writable") && ok;
        if (retryFile.isOpen()) {
            QByteArray retryPayload(300 * 1024, Qt::Uninitialized);
            for (int i = 0; i < retryPayload.size(); ++i) {
                retryPayload[i] = static_cast<char>('a' + (i % 26));
            }
            ok = expect(retryFile.write(retryPayload) == retryPayload.size(),
                        "PostgreSQL online retry file should contain the test payload") && ok;
            retryFile.close();
        }
        ok = expect(owner.sendFile(retryFilePath, peerId),
                    "owner should complete online file retry through PostgreSQL-backed server") && ok;
        ok = expect(waitFor([&] {
            drainRawPeerSocket();
            return rawPeerCompletedFile;
        }, 5000), "raw peer should complete online file retry after invalid first ACK") && ok;
        ok = expect(rawPeerChunkAttempts >= 3
                        && rawPeerChunkIndexes.size() >= 3
                        && rawPeerChunkIndexes[0] == 0
                        && rawPeerChunkIndexes[1] == 0,
                    "online retry should resend the first chunk after invalid ACK progress") && ok;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM messages WHERE sender_id = ? AND receiver_id = ? AND file_name = 'pgsql-smoke-online-retry-file.bin' AND file_size > 0 AND file_hash IS NOT NULL AND file_chunk_size > 0 AND file_chunk_count > 0",
                                  {ownerId, peerId},
                                  &fileRows)
                && fileRows >= 1;
        }), "online retry file metadata should be persisted in PostgreSQL") && ok;
        rawPeerSocket.disconnectFromHost();
        rawPeerSocket.waitForDisconnected(1000);

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
        const QString missingOfflineFileName = "pgsql-smoke-missing-offline-file.txt";
        const QString missingOfflineFilePath = QDir(appDataDir).filePath(missingOfflineFileName);
        QFile missingOfflineFile(missingOfflineFilePath);
        ok = expect(missingOfflineFile.open(QIODevice::WriteOnly),
                    "PostgreSQL missing offline attachment smoke file should be writable") && ok;
        if (missingOfflineFile.isOpen()) {
            missingOfflineFile.write("PostgreSQL QPSQL missing offline attachment smoke");
            missingOfflineFile.close();
        }
        ok = expect(owner.sendFile(missingOfflineFilePath, peerId),
                    "owner should queue a second offline attachment for missing-file cleanup") && ok;
        const QString sizeMismatchFileName = "pgsql-smoke-size-mismatch-offline-file.txt";
        const QString sizeMismatchFilePath = QDir(appDataDir).filePath(sizeMismatchFileName);
        QFile sizeMismatchFile(sizeMismatchFilePath);
        ok = expect(sizeMismatchFile.open(QIODevice::WriteOnly),
                    "PostgreSQL size-mismatch offline attachment smoke file should be writable") && ok;
        if (sizeMismatchFile.isOpen()) {
            sizeMismatchFile.write("PostgreSQL QPSQL size mismatch offline attachment smoke");
            sizeMismatchFile.close();
        }
        ok = expect(owner.sendFile(sizeMismatchFilePath, peerId),
                    "owner should queue an offline attachment for size mismatch cleanup") && ok;
        const QString hashMismatchFileName = "pgsql-smoke-hash-mismatch-offline-file.txt";
        const QString hashMismatchFilePath = QDir(appDataDir).filePath(hashMismatchFileName);
        QFile hashMismatchFile(hashMismatchFilePath);
        ok = expect(hashMismatchFile.open(QIODevice::WriteOnly),
                    "PostgreSQL hash-mismatch offline attachment smoke file should be writable") && ok;
        if (hashMismatchFile.isOpen()) {
            hashMismatchFile.write("PostgreSQL QPSQL hash mismatch offline attachment smoke");
            hashMismatchFile.close();
        }
        ok = expect(owner.sendFile(hashMismatchFilePath, peerId),
                    "owner should queue an offline attachment for hash mismatch cleanup") && ok;
        const QString chunkMismatchFileName = "pgsql-smoke-chunk-mismatch-offline-file.txt";
        const QString chunkMismatchFilePath = QDir(appDataDir).filePath(chunkMismatchFileName);
        QFile chunkMismatchFile(chunkMismatchFilePath);
        ok = expect(chunkMismatchFile.open(QIODevice::WriteOnly),
                    "PostgreSQL chunk-mismatch offline attachment smoke file should be writable") && ok;
        if (chunkMismatchFile.isOpen()) {
            chunkMismatchFile.write("PostgreSQL QPSQL chunk mismatch offline attachment smoke");
            chunkMismatchFile.close();
        }
        ok = expect(owner.sendFile(chunkMismatchFilePath, peerId),
                    "owner should queue an offline attachment for chunk metadata cleanup") && ok;
        qint64 offlineRows = 0;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM offline_messages WHERE receiver_id = ?",
                                  {peerId},
                                  &offlineRows)
                && offlineRows >= 6;
        }), "offline private message and attachments should be queued in PostgreSQL") && ok;
        qint64 offlineAttachmentRows = 0;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM offline_messages WHERE receiver_id = ? AND payload LIKE '%pgsql-smoke-offline-file.txt%' AND payload LIKE '%chunkSize%' AND payload LIKE '%chunkCount%'",
                                  {peerId},
                                  &offlineAttachmentRows)
                && offlineAttachmentRows >= 1;
        }), "offline attachment payload should keep chunk metadata in PostgreSQL") && ok;
        QString missingAttachmentPayload;
        ok = expect(waitFor([&] {
            return scalarString("SELECT payload FROM offline_messages WHERE receiver_id = ? AND payload LIKE ? ORDER BY id DESC LIMIT 1",
                                {peerId, "%" + missingOfflineFileName + "%"},
                                &missingAttachmentPayload)
                && missingAttachmentPayload.contains(missingOfflineFileName);
        }), "missing-file offline attachment payload should be persisted in PostgreSQL") && ok;
        const QJsonDocument missingAttachmentDoc = QJsonDocument::fromJson(missingAttachmentPayload.toUtf8());
        ok = expect(missingAttachmentDoc.isObject(),
                    "missing-file offline attachment payload should be valid JSON") && ok;
        const QString storedMissingAttachmentPath = missingAttachmentDoc.object().value("offlineFilePath").toString();
        ok = expect(!storedMissingAttachmentPath.isEmpty(),
                    "missing-file offline attachment payload should include an offline file path") && ok;
        ok = expect(QFile::remove(storedMissingAttachmentPath),
                    "smoke should be able to delete the queued offline attachment file before replay") && ok;
        auto loadOfflineAttachmentPayload = [&](const QString& fileName, QString* payload, QString* offlinePath) {
            QString loadedPayload;
            if (!waitFor([&] {
                    return scalarString("SELECT payload FROM offline_messages WHERE receiver_id = ? AND payload LIKE ? ORDER BY id DESC LIMIT 1",
                                        {peerId, "%" + fileName + "%"},
                                        &loadedPayload)
                        && loadedPayload.contains(fileName);
                })) {
                return false;
            }
            const QJsonDocument doc = QJsonDocument::fromJson(loadedPayload.toUtf8());
            if (!doc.isObject()) return false;
            const QString path = doc.object().value("offlineFilePath").toString();
            if (path.isEmpty()) return false;
            if (payload) *payload = loadedPayload;
            if (offlinePath) *offlinePath = path;
            return true;
        };
        QString sizeMismatchPayload;
        QString storedSizeMismatchPath;
        ok = expect(loadOfflineAttachmentPayload(sizeMismatchFileName, &sizeMismatchPayload, &storedSizeMismatchPath),
                    "size-mismatch offline attachment payload should be persisted in PostgreSQL") && ok;
        QFile storedSizeMismatchFile(storedSizeMismatchPath);
        ok = expect(storedSizeMismatchFile.open(QIODevice::Append),
                    "smoke should open the queued offline attachment file for size mutation") && ok;
        if (storedSizeMismatchFile.isOpen()) {
            storedSizeMismatchFile.write("x");
            storedSizeMismatchFile.close();
        }
        QString hashMismatchPayload;
        QString storedHashMismatchPath;
        ok = expect(loadOfflineAttachmentPayload(hashMismatchFileName, &hashMismatchPayload, &storedHashMismatchPath),
                    "hash-mismatch offline attachment payload should be persisted in PostgreSQL") && ok;
        QFile storedHashMismatchFile(storedHashMismatchPath);
        ok = expect(storedHashMismatchFile.open(QIODevice::ReadWrite),
                    "smoke should open the queued offline attachment file for hash mutation") && ok;
        if (storedHashMismatchFile.isOpen()) {
            ok = expect(storedHashMismatchFile.seek(0) && storedHashMismatchFile.write("X") == 1,
                        "smoke should mutate queued offline attachment content without changing metadata") && ok;
            storedHashMismatchFile.close();
        }
        QString chunkMismatchPayload;
        QString storedChunkMismatchPath;
        ok = expect(loadOfflineAttachmentPayload(chunkMismatchFileName, &chunkMismatchPayload, &storedChunkMismatchPath),
                    "chunk-mismatch offline attachment payload should be persisted in PostgreSQL") && ok;
        QJsonObject chunkMismatchPayloadObject = QJsonDocument::fromJson(chunkMismatchPayload.toUtf8()).object();
        chunkMismatchPayloadObject["chunkCount"] = QString::number(999999);
        const QString mutatedChunkMismatchPayload = QString::fromUtf8(QJsonDocument(chunkMismatchPayloadObject).toJson(QJsonDocument::Compact));
        const QString mutateChunkConnectionName = "postgres_qpsql_mutate_chunk_payload";
        bool mutatedChunkPayload = false;
        {
            QSqlDatabase db = openPostgres(mutateChunkConnectionName);
            if (db.open()) {
                mutatedChunkPayload = execSql(db,
                                              "UPDATE offline_messages SET payload = ? WHERE receiver_id = ? AND payload LIKE ?",
                                              {mutatedChunkMismatchPayload, peerId, "%" + chunkMismatchFileName + "%"});
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(mutateChunkConnectionName);
        ok = expect(mutatedChunkPayload,
                    "smoke should mutate queued offline attachment chunk metadata in PostgreSQL") && ok;

        QObject::disconnect(&peer, nullptr, &app, nullptr);
        QObject::connect(&peer, &Client::newMessage, &app, [&](const Message& msg) {
            if (msg.type == MessageType::Private) {
                peerOfflineMessages << msg.content;
            } else if (msg.type == MessageType::File) {
                peerFiles << msg.fileName;
            } else if (msg.type == MessageType::System) {
                peerSystemMessages << msg.content;
            }
        });
        ok = expect(loginClient(peer, peerId, "PgPeer", port, false),
                    "peer should re-login to replay PostgreSQL offline message") && ok;
        ok = expect(waitFor([&] { return peerOfflineMessages.contains(offlineMessage); }, 8000),
                    "peer should receive PostgreSQL offline private message") && ok;
        ok = expect(waitFor([&] { return peerFiles.contains(QStringLiteral("pgsql-smoke-offline-file.txt")); }, 8000),
                    "peer should receive PostgreSQL offline attachment replay") && ok;
        ok = expect(waitFor([&] {
            for (const QString& message : peerSystemMessages) {
                if (message.contains(QString::fromUtf8("离线文件已丢失"))
                    && message.contains(missingOfflineFileName)) {
                    return true;
                }
            }
            return false;
        }, 8000), "peer should receive a missing offline attachment notice from PostgreSQL replay") && ok;
        ok = expect(waitFor([&] {
            bool sawSizeMismatch = false;
            bool sawHashMismatch = false;
            bool sawChunkMismatch = false;
            for (const QString& message : peerSystemMessages) {
                sawSizeMismatch = sawSizeMismatch
                    || (message.contains(QString::fromUtf8("离线文件大小异常")) && message.contains(sizeMismatchFileName));
                sawHashMismatch = sawHashMismatch
                    || (message.contains(QString::fromUtf8("离线文件校验失败")) && message.contains(hashMismatchFileName));
                sawChunkMismatch = sawChunkMismatch
                    || (message.contains(QString::fromUtf8("离线文件分片元数据异常")) && message.contains(chunkMismatchFileName));
            }
            return sawSizeMismatch && sawHashMismatch && sawChunkMismatch;
        }, 8000), "peer should receive size/hash/chunk metadata offline attachment notices from PostgreSQL replay") && ok;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM offline_messages WHERE receiver_id = ?",
                                  {peerId},
                                  &offlineRows)
                && offlineRows == 0;
        }), "PostgreSQL offline queue should be cleared after replay") && ok;

        setSmokeStep(QStringLiteral("offline attachment partial ack resume"));
        disconnectClient(peer);
        ok = expect(waitFor([&] { return !peer.isConnected(); }, 2000),
                    "peer should be offline before partial-ack resume smoke") && ok;
        const QString partialAckFileName = "pgsql-smoke-partial-ack-offline-file.bin";
        const QString partialAckFilePath = QDir(appDataDir).filePath(partialAckFileName);
        QByteArray partialAckPayload(300 * 1024, Qt::Uninitialized);
        for (int i = 0; i < partialAckPayload.size(); ++i) {
            partialAckPayload[i] = static_cast<char>('A' + (i % 26));
        }
        QFile partialAckFile(partialAckFilePath);
        ok = expect(partialAckFile.open(QIODevice::WriteOnly),
                    "PostgreSQL partial-ack offline attachment smoke file should be writable") && ok;
        if (partialAckFile.isOpen()) {
            ok = expect(partialAckFile.write(partialAckPayload) == partialAckPayload.size(),
                        "PostgreSQL partial-ack offline attachment smoke file should contain the test payload") && ok;
            partialAckFile.close();
        }
        ok = expect(owner.sendFile(partialAckFilePath, peerId),
                    "owner should queue an offline attachment for partial-ack resume") && ok;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM offline_messages WHERE receiver_id = ? AND payload LIKE ?",
                                  {peerId, "%" + partialAckFileName + "%"},
                                  &offlineRows)
                && offlineRows == 1;
        }), "partial-ack offline attachment should be queued in PostgreSQL") && ok;

        qint64 partialAckReceivedBytes = 0;
        QVector<qint64> partialAckFirstChunks;
        ok = expect(loginRawAckChunksThenDisconnect(peerId,
                                                    "PgPeer",
                                                    port,
                                                    1,
                                                    &partialAckReceivedBytes,
                                                    &partialAckFirstChunks),
                    "raw peer should ack the first PostgreSQL offline attachment chunk before disconnecting") && ok;
        ok = expect(partialAckFirstChunks.size() == 1 && partialAckFirstChunks.first() == 0,
                    "raw partial-ack peer should confirm chunk 0 first") && ok;
        ok = expect(partialAckReceivedBytes == 256 * 1024,
                    "raw partial-ack peer should report the first confirmed chunk bytes") && ok;
        QString partialAckPayloadJson;
        ok = expect(waitFor([&] {
            return scalarString("SELECT payload FROM offline_messages WHERE receiver_id = ? AND payload LIKE ? ORDER BY id DESC LIMIT 1",
                                {peerId, "%" + partialAckFileName + "%"},
                                &partialAckPayloadJson)
                && partialAckPayloadJson.contains(partialAckFileName);
        }), "partial-ack offline attachment queue row should be retained in PostgreSQL") && ok;
        const QJsonObject partialAckQueuedPayload = QJsonDocument::fromJson(partialAckPayloadJson.toUtf8()).object();
        const QJsonArray partialAckConfirmedChunks = partialAckQueuedPayload["confirmedChunks"].toArray();
        ok = expect(partialAckQueuedPayload["confirmedBytes"].toVariant().toLongLong() == partialAckReceivedBytes,
                    "partial-ack offline attachment should persist confirmedBytes in PostgreSQL") && ok;
        ok = expect(partialAckConfirmedChunks.size() == 1
                        && partialAckConfirmedChunks.first().toVariant().toLongLong() == 0,
                    "partial-ack offline attachment should persist confirmed chunk 0 in PostgreSQL") && ok;
        ok = expect(!partialAckQueuedPayload["resumeUpdatedAt"].toString().isEmpty(),
                    "partial-ack offline attachment should persist resumeUpdatedAt in PostgreSQL") && ok;

        QVector<qint64> partialAckRetryChunks;
        qint64 partialAckRetryReceivedBytes = 0;
        ok = expect(loginRawAckFileReplay(peerId,
                                          "PgPeer",
                                          port,
                                          &partialAckRetryChunks,
                                          &partialAckRetryReceivedBytes),
                    "raw peer should resume and finish the PostgreSQL offline attachment replay") && ok;
        ok = expect(partialAckRetryChunks.size() == 1 && partialAckRetryChunks.first() == 1,
                    "PostgreSQL offline attachment retry should resume from the first unconfirmed chunk") && ok;
        ok = expect(partialAckRetryReceivedBytes == partialAckPayload.size(),
                    "PostgreSQL offline attachment retry should confirm the full attachment size") && ok;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM offline_messages WHERE receiver_id = ? AND payload LIKE ?",
                                  {peerId, "%" + partialAckFileName + "%"},
                                  &offlineRows)
                && offlineRows == 0;
        }), "partial-ack offline attachment retry should clear the PostgreSQL queue row") && ok;

        setSmokeStep(QStringLiteral("offline attachment expired resume fallback"));
        const QString expiredResumeFileName = "pgsql-smoke-expired-resume-offline-file.bin";
        const QString expiredResumeFilePath = QDir(appDataDir).filePath(expiredResumeFileName);
        QByteArray expiredResumePayload(300 * 1024, Qt::Uninitialized);
        for (int i = 0; i < expiredResumePayload.size(); ++i) {
            expiredResumePayload[i] = static_cast<char>('0' + (i % 10));
        }
        QFile expiredResumeFile(expiredResumeFilePath);
        ok = expect(expiredResumeFile.open(QIODevice::WriteOnly),
                    "PostgreSQL expired-resume offline attachment smoke file should be writable") && ok;
        if (expiredResumeFile.isOpen()) {
            ok = expect(expiredResumeFile.write(expiredResumePayload) == expiredResumePayload.size(),
                        "PostgreSQL expired-resume offline attachment smoke file should contain the test payload") && ok;
            expiredResumeFile.close();
        }
        ok = expect(owner.sendFile(expiredResumeFilePath, peerId),
                    "owner should queue an offline attachment for expired resume fallback") && ok;
        QString expiredResumePayloadJson;
        ok = expect(loadOfflineAttachmentPayload(expiredResumeFileName, &expiredResumePayloadJson, nullptr),
                    "expired-resume offline attachment payload should be persisted in PostgreSQL") && ok;
        QJsonObject expiredResumePayloadObject = QJsonDocument::fromJson(expiredResumePayloadJson.toUtf8()).object();
        QJsonArray expiredConfirmedChunks;
        expiredConfirmedChunks.append(QString::number(0));
        expiredResumePayloadObject["confirmedBytes"] = QString::number(256 * 1024);
        expiredResumePayloadObject["confirmedChunks"] = expiredConfirmedChunks;
        expiredResumePayloadObject["resumeUpdatedAt"] = QDateTime::currentDateTimeUtc().addDays(-2).toString(Qt::ISODate);
        const QString mutatedExpiredResumePayload = QString::fromUtf8(QJsonDocument(expiredResumePayloadObject).toJson(QJsonDocument::Compact));
        const QString mutateExpiredResumeConnectionName = "postgres_qpsql_mutate_expired_resume_payload";
        bool mutatedExpiredResumePayloadOk = false;
        {
            QSqlDatabase db = openPostgres(mutateExpiredResumeConnectionName);
            if (db.open()) {
                mutatedExpiredResumePayloadOk = execSql(db,
                                                        "UPDATE offline_messages SET payload = ? WHERE receiver_id = ? AND payload LIKE ?",
                                                        {mutatedExpiredResumePayload, peerId, "%" + expiredResumeFileName + "%"});
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(mutateExpiredResumeConnectionName);
        ok = expect(mutatedExpiredResumePayloadOk,
                    "smoke should mutate queued offline attachment resume progress to an expired timestamp") && ok;

        QVector<qint64> expiredResumeReplayChunks;
        qint64 expiredResumeReplayReceivedBytes = 0;
        ok = expect(loginRawAckFileReplay(peerId,
                                          "PgPeer",
                                          port,
                                          &expiredResumeReplayChunks,
                                          &expiredResumeReplayReceivedBytes),
                    "raw peer should finish expired-resume PostgreSQL offline attachment replay") && ok;
        ok = expect(expiredResumeReplayChunks.size() == 2
                        && expiredResumeReplayChunks[0] == 0
                        && expiredResumeReplayChunks[1] == 1,
                    "expired PostgreSQL offline attachment resume progress should fall back to a full replay") && ok;
        ok = expect(expiredResumeReplayReceivedBytes == expiredResumePayload.size(),
                    "expired-resume replay should confirm the full attachment size") && ok;
        ok = expect(waitFor([&] {
            return scalarLongLong("SELECT COUNT(*) FROM offline_messages WHERE receiver_id = ? AND payload LIKE ?",
                                  {peerId, "%" + expiredResumeFileName + "%"},
                                  &offlineRows)
                && offlineRows == 0;
        }), "expired-resume offline attachment replay should clear the PostgreSQL queue row") && ok;

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
