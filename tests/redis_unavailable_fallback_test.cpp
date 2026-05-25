#include "client.h"
#include "server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>

#include <functional>

namespace {
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
                    quint16 port) {
    client.setUserInfo(account, userName);
    client.setAccountInfo(account, "secret", true);
    if (!client.connectToServer("127.0.0.1", port)) return false;
    return client.waitForLoginResult(5000);
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("redis_unavailable_fallback_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    const quint16 chatPort = freeLocalPort();
    const quint16 redisPort = freeLocalPort();
    bool ok = true;
    ok = expect(chatPort != 0, "a local chat test port should be available") && ok;
    ok = expect(redisPort != 0, "an unused Redis test port should be available") && ok;
    if (!ok) return 1;

    qputenv("QTNETWORKCHAT_REDIS", "1");
    qputenv("QTNETWORKCHAT_REDIS_HOST", "127.0.0.1");
    qputenv("QTNETWORKCHAT_REDIS_PORT", QByteArray::number(redisPort));
    qputenv("QTNETWORKCHAT_REDIS_PREFIX", "qtchat-fallback-test");

    Server server;
    ok = expect(server.start(chatPort), "server should start even when Redis is unreachable") && ok;
    if (!ok) return 1;

    Client client;
    ok = expect(registerClient(client, "930001", "RedisFallback", chatPort),
                "client should log in through the in-memory fallback when Redis is unreachable") && ok;
    ok = expect(client.isConnected(), "client should remain connected after fallback login") && ok;
    ok = expect(client.currentUserId() == "930001", "client should receive the expected user id") && ok;

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
