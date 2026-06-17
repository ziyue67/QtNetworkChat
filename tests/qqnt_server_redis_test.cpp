#include "server.h"
#include "message.h"
#include "test_redis_support.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
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

bool connectSocket(QTcpSocket* socket, quint16 port) {
    if (!socket) return false;
    socket->connectToHost(QHostAddress::LocalHost, port);
    return socket->waitForConnected(3000);
}

bool writeJsonLine(QTcpSocket* socket, const QJsonObject& object) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return false;
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    return socket->write(line) == line.size() && socket->waitForBytesWritten(3000);
}

bool loginSocket(QTcpSocket* socket, const QString& account, const QString& name) {
    QJsonObject login;
    login["type"] = "login";
    login["mode"] = "register";
    login["account"] = account;
    login["password"] = "secret";
    login["userName"] = name;
    return writeJsonLine(socket, login);
}

bool socketBufferContains(QTcpSocket* socket, QByteArray* buffer, const QByteArray& needle, int timeoutMs = 3000) {
    if (!socket || !buffer) return false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        buffer->append(socket->readAll());
        if (buffer->contains(needle)) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        socket->waitForReadyRead(50);
    }
    buffer->append(socket->readAll());
    return buffer->contains(needle);
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("qqnt_server_redis_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    TestRedisServerEnvironment redisEnv(QStringLiteral("qtchat-server-redis-test"));
    QString redisError;
    bool ok = redisEnv.start(&redisError);
    ok = expect(ok, "fake Redis server should start") && ok;
    ok = expect(redisError.isEmpty(), "fake Redis server should not fail to start") && ok;
    if (!ok) return 1;
    redisEnv.applyEnvironment();

    const quint16 serverAPort = freeLocalPort();
    const quint16 serverBPort = freeLocalPort();
    ok = expect(serverAPort != 0 && serverBPort != 0, "chat ports should be available") && ok;
    if (!ok) return 1;

    Server serverA;
    Server serverB;
    ok = expect(serverA.start(serverAPort), "server A should start with Redis enabled") && ok;
    ok = expect(serverB.start(serverBPort), "server B should start with Redis enabled") && ok;
    ok = expect(serverA.isServiceReady() && serverB.isServiceReady(),
                "both servers should report Redis-ready") && ok;
    if (!ok) return 1;

    QTcpSocket alice;
    QTcpSocket bob;
    QByteArray aliceBuffer;
    QByteArray bobBuffer;

    ok = expect(connectSocket(&alice, serverAPort), "alice should connect to server A") && ok;
    ok = expect(connectSocket(&bob, serverBPort), "bob should connect to server B") && ok;
    ok = expect(loginSocket(&alice, QStringLiteral("970001"), QStringLiteral("AliceRedis")),
                "alice should log in on server A") && ok;
    ok = expect(loginSocket(&bob, QStringLiteral("970002"), QStringLiteral("BobRedis")),
                "bob should log in on server B") && ok;
    ok = expect(socketBufferContains(&alice, &aliceBuffer, QByteArrayLiteral("\"type\":\"login_success\""), 5000),
                "alice should receive login success") && ok;
    ok = expect(socketBufferContains(&bob, &bobBuffer, QByteArrayLiteral("\"type\":\"login_success\""), 5000),
                "bob should receive login success") && ok;

    QJsonObject privateMessage;
    privateMessage["type"] = "private";
    privateMessage["messageType"] = static_cast<int>(MessageType::Private);
    privateMessage["receiverId"] = "970002";
    privateMessage["content"] = "qqnt server redis routing works";
    ok = expect(writeJsonLine(&alice, privateMessage), "alice should send a private message") && ok;
    ok = expect(waitFor([&] {
        bobBuffer.append(bob.readAll());
        return bobBuffer.contains("qqnt server redis routing works") && bobBuffer.contains("\"type\":\"private\"");
    }, 5000), "bob should receive the Redis-routed private message") && ok;

    alice.disconnectFromHost();
    bob.disconnectFromHost();
    serverA.stop();
    serverB.stop();
    redisEnv.stop();
    return ok ? 0 : 1;
}
