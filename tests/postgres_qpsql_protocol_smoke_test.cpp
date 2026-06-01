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

#include <functional>

namespace {
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
                qWarning() << "PostgreSQL scalar query failed:" << query.lastError().text() << sql;
            }
            db.close();
        } else {
            qWarning() << "PostgreSQL scalar connection failed:" << db.lastError().text();
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
    cleanupSmokeRows(ownerId, peerId);

    const quint16 port = freeLocalPort();
    ok = expect(port != 0, "a local PostgreSQL smoke test port should be available") && ok;
    if (!ok) return 1;

    {
        Server server;
        ok = expect(server.start(port), "server should start with QPSQL account database") && ok;
        if (!ok) {
            cleanupSmokeRows(ownerId, peerId);
            return 1;
        }

        Client owner;
        Client peer;
        QStringList peerPrivateMessages;
        QObject::connect(&peer, &Client::newMessage, &app, [&](const Message& msg) {
            if (msg.type == MessageType::Private) {
                peerPrivateMessages << msg.content;
            }
        });

        ok = expect(loginClient(owner, ownerId, "PgOwner", port, true),
                    "owner should register through PostgreSQL") && ok;
        ok = expect(loginClient(peer, peerId, "PgPeer", port, true),
                    "peer should register through PostgreSQL") && ok;

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

        disconnectClient(owner);
        disconnectClient(peer);
        server.stop();
        drainEvents();
    }

    {
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

        disconnectClient(relogin);
        server.stop();
        drainEvents();
    }

    ok = cleanupSmokeRows(ownerId, peerId) && ok;
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
