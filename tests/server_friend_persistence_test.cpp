#include "client.h"
#include "server.h"
#include "test_redis_support.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QStandardPaths>
#include <QTcpServer>
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
        qWarning("%s", message);
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

bool loginClient(Client& client,
                 const QString& account,
                 const QString& userName,
                 quint16 port,
                 bool registerMode) {
    client.setUserInfo(account, userName);
    client.setAccountInfo(account, "secret", registerMode);
    if (!client.connectToServer("127.0.0.1", port)) return false;
    return client.waitForLoginResult(5000);
}

bool hasFriend(const Client& client, const QString& friendId, const QString& expectedName = QString()) {
    for (const ChatUser& friendUser : client.friends()) {
        if (friendUser.id != friendId) {
            continue;
        }
        return expectedName.isEmpty() || friendUser.name == expectedName;
    }
    return false;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("server_friend_persistence_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = testAppDataDir();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    const quint16 port = freeLocalPort();
    bool ok = true;
    ok = expect(port != 0, "a local chat test port should be available") && ok;
    if (!ok) return 1;

    TestRedisServerEnvironment redis(QStringLiteral("qtchat-friend-persistence-test"));
    QString redisError;
    ok = expect(redis.start(&redisError), "fake Redis should start for friend persistence test") && ok;
    if (!ok) return 1;
    redis.applyEnvironment();

    Server server;
    ok = expect(server.start(port), "server should start for friend persistence test") && ok;
    if (!ok) return 1;

    const QString aliceId = "920001";
    const QString bobId = "920002";

    Client alice;
    Client bob;
    bool bobReceivedAliceRequest = false;
    QObject::connect(&bob, &Client::friendRequestReceived, &app, [&](const QString& senderId, const QString&) {
        if (senderId == aliceId) {
            bobReceivedAliceRequest = true;
        }
    });
    ok = expect(loginClient(alice, aliceId, "PersistAlice", port, true),
                "Alice should register") && ok;
    ok = expect(loginClient(bob, bobId, "PersistBob", port, true),
                "Bob should register") && ok;

    ok = expect(alice.sendFriendRequest(bobId),
                "Alice should send a friend request") && ok;
    ok = expect(waitFor([&] { return bobReceivedAliceRequest; }),
                "Bob should receive Alice's friend request") && ok;
    ok = expect(bob.sendFriendResponse(aliceId, true),
                "Bob should accept Alice as a friend") && ok;
    ok = expect(waitFor([&] { return hasFriend(alice, bobId, "PersistBob"); }),
                "Alice should see Bob after the accepted response") && ok;
    ok = expect(waitFor([&] { return hasFriend(bob, aliceId, "PersistAlice"); }),
                "Bob should see Alice after accepting the request") && ok;

    disconnectClient(alice);

    Client aliceRelogin;
    ok = expect(loginClient(aliceRelogin, aliceId, "PersistAlice", port, false),
                "Alice should log in again with the existing account") && ok;
    ok = expect(waitFor([&] { return hasFriend(aliceRelogin, bobId, "PersistBob"); }),
                "Alice should receive Bob from the persisted server friend snapshot after relogin") && ok;

    disconnectClient(aliceRelogin);
    disconnectClient(bob);
    server.stop();
    redis.stop();
    drainEvents();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
