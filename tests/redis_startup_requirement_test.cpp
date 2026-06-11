#include "server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QHostAddress>
#include <QStandardPaths>
#include <QTcpServer>

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
        qWarning() << message;
        return false;
    }
    return true;
}

quint16 freeLocalPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) return 0;
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("redis_startup_requirement_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = testAppDataDir();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    bool ok = true;
    const quint16 chatPort = freeLocalPort();
    ok = expect(chatPort != 0, "a local chat test port should be available") && ok;
    if (!ok) return 1;

    qunsetenv("QTNETWORKCHAT_REDIS");
    qunsetenv("QTNETWORKCHAT_REDIS_HOST");
    qunsetenv("QTNETWORKCHAT_REDIS_PORT");
    qunsetenv("QTNETWORKCHAT_REDIS_PASSWORD");
    qunsetenv("QTNETWORKCHAT_REDIS_PREFIX");

    {
        Server server;
        ok = expect(!server.start(chatPort),
                    "server should fail fast when Redis is not explicitly enabled") && ok;
        ok = expect(!server.isServiceReady(),
                    "server should stay not ready when Redis enable flag is missing") && ok;
    }

    QTcpServer droppingRedis;
    ok = expect(droppingRedis.listen(QHostAddress::LocalHost, 0),
                "dropping Redis test server should listen on a local port") && ok;
    const quint16 redisPort = droppingRedis.serverPort();
    ok = expect(redisPort != 0, "dropping Redis test port should be available") && ok;
    if (!ok) return 1;

    QObject::connect(&droppingRedis, &QTcpServer::newConnection, &app, [&] {
        while (QTcpSocket* socket = droppingRedis.nextPendingConnection()) {
            socket->disconnectFromHost();
            socket->deleteLater();
        }
    });

    qputenv("QTNETWORKCHAT_REDIS", "1");
    qputenv("QTNETWORKCHAT_REDIS_HOST", "127.0.0.1");
    qputenv("QTNETWORKCHAT_REDIS_PORT", QByteArray::number(redisPort));
    qputenv("QTNETWORKCHAT_REDIS_PREFIX", "qtchat-startup-requirement-test");

    {
        Server server;
        ok = expect(!server.start(chatPort),
                    "server should fail fast when Redis is unreachable") && ok;
        ok = expect(!server.isServiceReady(),
                    "server should remain not ready when Redis startup check fails") && ok;
    }

    droppingRedis.close();

    {
        Server server;
        ok = expect(!server.start(chatPort),
                    "server should still fail until a real Redis service is available") && ok;
    }

    qunsetenv("QTNETWORKCHAT_REDIS");
    qunsetenv("QTNETWORKCHAT_REDIS_HOST");
    qunsetenv("QTNETWORKCHAT_REDIS_PORT");
    qunsetenv("QTNETWORKCHAT_REDIS_PASSWORD");
    qunsetenv("QTNETWORKCHAT_REDIS_PREFIX");

    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
