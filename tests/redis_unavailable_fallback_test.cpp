#include "client.h"
#include "redisclient.h"
#include "server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTextStream>
#include <QThread>

#include <functional>

namespace {
QString testAppDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        QTextStream(stderr) << message << Qt::endl;
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

QString scalarString(const QString& databasePath, const QString& sql, const QVariantList& binds = QVariantList()) {
    const QString connectionName = QStringLiteral("redis_fallback_avatar_check_") + QString::number(qHash(sql));
    QString result;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(sql);
            for (const QVariant& bind : binds) {
                query.addBindValue(bind);
            }
            if (query.exec() && query.next()) {
                result = query.value(0).toString();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

quint16 freeLocalPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) return 0;
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

bool registerClient(Client& client,
                    const QString& account,
                    const QString& userName,
                    quint16 port,
                    bool registerMode = true) {
    client.setUserInfo(account, userName);
    client.setAccountInfo(account, "secret", registerMode);
    if (!client.connectToServer("127.0.0.1", port)) return false;
    return client.waitForLoginResult(5000);
}

QByteArray tinyPngAvatar(uchar marker) {
    static const QByteArray avatarA = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFElEQVQImWNkYPj/n4GBgYGJAQoAAB0nA/2bQVWkAAAAAElFTkSuQmCC");
    static const QByteArray avatarB = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFElEQVQImWP8z/D/PwMDAwMTAxQAABoSA/0ZKq8cAAAAAElFTkSuQmCC");
    return marker == 1 ? avatarA : avatarB;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("redis_unavailable_fallback_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = testAppDataDir();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    const quint16 chatPort = freeLocalPort();
    bool ok = true;
    ok = expect(chatPort != 0, "a local chat test port should be available") && ok;
    if (!ok) return 1;

    QTcpServer droppingRedis;
    int redisConnectionAttempts = 0;
    QObject::connect(&droppingRedis, &QTcpServer::newConnection, &app, [&] {
        while (QTcpSocket* socket = droppingRedis.nextPendingConnection()) {
            ++redisConnectionAttempts;
            socket->disconnectFromHost();
            socket->deleteLater();
        }
    });
    ok = expect(droppingRedis.listen(QHostAddress::LocalHost, 0),
                "dropping Redis test server should listen on the fallback test port") && ok;
    const quint16 redisPort = droppingRedis.serverPort();
    ok = expect(redisPort != 0, "dropping Redis test port should be available") && ok;
    if (!ok) return 1;

    qputenv("QTNETWORKCHAT_REDIS", "1");
    qputenv("QTNETWORKCHAT_REDIS_HOST", "127.0.0.1");
    qputenv("QTNETWORKCHAT_REDIS_PORT", QByteArray::number(redisPort));
    qputenv("QTNETWORKCHAT_REDIS_PREFIX", "qtchat-fallback-test");

    RedisClient redisClient;
    redisClient.configureFromEnvironment();
    ok = expect(!redisClient.publish("messages", QByteArrayLiteral("{\"kind\":\"first\"}"), 200),
                "first Redis publish should fail against the dropping server") && ok;
    ok = expect(waitFor([&] { return redisConnectionAttempts >= 1; }, 1000),
                "dropping Redis should observe the first publish connection attempt") && ok;
    const int attemptsAfterFirstPublish = redisConnectionAttempts;
    ok = expect(attemptsAfterFirstPublish == 1,
                "first Redis publish should make one connection attempt") && ok;
    ok = expect(!redisClient.publish("messages", QByteArrayLiteral("{\"kind\":\"second\"}"), 200),
                "second Redis publish should fail during reconnect backoff") && ok;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    ok = expect(redisConnectionAttempts == attemptsAfterFirstPublish,
                "Redis reconnect backoff should avoid an immediate second connection attempt") && ok;
    droppingRedis.close();

    Server server;
    ok = expect(server.start(chatPort), "server should start even when Redis is unreachable") && ok;
    if (!ok) return 1;

    Client client;
    ok = expect(registerClient(client, "930001", "RedisFallback", chatPort),
                "client should log in through the in-memory fallback when Redis is unreachable") && ok;
    ok = expect(client.isConnected(), "client should remain connected after fallback login") && ok;
    ok = expect(client.currentUserId() == "930001", "client should receive the expected user id") && ok;

    const QByteArray initialAvatar = tinyPngAvatar(1);
    const QByteArray updatedAvatar = tinyPngAvatar(2);
    Client avatarPeer;
    avatarPeer.setAvatarData(initialAvatar);
    ok = expect(registerClient(avatarPeer, "930002", "AvatarPeer", chatPort),
                "avatar peer should log in with avatar metadata") && ok;
    const QString initialAvatarBase64 = QString::fromLatin1(initialAvatar.toBase64());
    ok = expect(waitFor([&] {
                    for (const ChatUser& user : client.onlineUsers()) {
                        if (user.id == QStringLiteral("930002") && user.avatar == initialAvatarBase64) {
                            return true;
                        }
                    }
                    return false;
                }),
                "user list should expose peer avatar metadata from login") && ok;

    const QString updatedAvatarBase64 = QString::fromLatin1(updatedAvatar.toBase64());
    ok = expect(avatarPeer.sendAvatarUpdate(updatedAvatar),
                "avatar peer should send a live avatar profile update") && ok;
    ok = expect(waitFor([&] {
                    for (const ChatUser& user : client.onlineUsers()) {
                        if (user.id == QStringLiteral("930002") && user.avatar == updatedAvatarBase64) {
                            return true;
                        }
                    }
                    return false;
                }),
                "user list should refresh peer avatar metadata after profile update") && ok;
    const QString persistedAvatarBase64 = scalarString(QDir(appDataDir).filePath(QStringLiteral("accounts.sqlite3")),
                                                       QStringLiteral("SELECT COALESCE(avatar_base64, '') FROM accounts WHERE account = ?"),
                                                       QVariantList{QStringLiteral("930002")});
    ok = expect(persistedAvatarBase64 == updatedAvatarBase64,
                "server account storage should persist the latest avatar_base64 after profile update") && ok;

    avatarPeer.disconnectFromServer();
    ok = expect(waitFor([&] {
                    for (const ChatUser& user : client.onlineUsers()) {
                        if (user.id == QStringLiteral("930002")) {
                            return false;
                        }
                    }
                    return true;
                }),
                "avatar peer should disappear from the online user list after disconnect") && ok;

    Client avatarPeerRelogin;
    ok = expect(registerClient(avatarPeerRelogin, "930002", "AvatarPeer", chatPort, false),
                "avatar peer should be able to relogin from persisted account state") && ok;
    ok = expect(waitFor([&] {
                    for (const ChatUser& user : client.onlineUsers()) {
                        if (user.id == QStringLiteral("930002") && user.avatar == updatedAvatarBase64) {
                            return true;
                        }
                    }
                    return false;
                }),
                "user list should restore peer avatar metadata from persisted account state after relogin") && ok;
    QStringList receivedMessages;
    QObject::connect(&client, &Client::newMessage, &app, [&](const Message& msg) {
        if (msg.type == MessageType::Text) {
            receivedMessages << msg.content;
        }
    });
    const QString fallbackMessage = "Redis fallback broadcast should still work";
    ok = expect(client.sendMessage(fallbackMessage),
                "client should send a broadcast message while Redis is unreachable") && ok;
    ok = expect(waitFor([&] { return receivedMessages.contains(fallbackMessage); }),
                "client should receive the local broadcast even when Redis publish falls back") && ok;

    avatarPeerRelogin.disconnectFromServer();
    client.disconnectFromServer();
    server.stop();

    qunsetenv("QTNETWORKCHAT_REDIS");
    qunsetenv("QTNETWORKCHAT_REDIS_HOST");
    qunsetenv("QTNETWORKCHAT_REDIS_PORT");
    qunsetenv("QTNETWORKCHAT_REDIS_PREFIX");

    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
