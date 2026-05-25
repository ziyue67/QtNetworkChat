#include "client.h"
#include "redisclient.h"
#include "server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

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

QByteArray bulkReply(const QByteArray& value) {
    return "$" + QByteArray::number(value.size()) + "\r\n" + value + "\r\n";
}

QByteArray integerReply(int value) {
    return ":" + QByteArray::number(value) + "\r\n";
}

QByteArray arrayReply(const QList<QByteArray>& values) {
    QByteArray response = "*" + QByteArray::number(values.size()) + "\r\n";
    for (const QByteArray& value : values) {
        response.append(bulkReply(value));
    }
    return response;
}

QByteArray pubSubMessageReply(const QByteArray& channel, const QByteArray& payload) {
    QByteArray response = "*3\r\n";
    response.append(bulkReply(QByteArrayLiteral("message")));
    response.append(bulkReply(channel));
    response.append(bulkReply(payload));
    return response;
}

QList<QByteArray> commandArguments(const RedisClient::Reply& request) {
    QList<QByteArray> args;
    if (request.type != RedisClient::ReplyType::Array) return args;
    for (const RedisClient::Reply& element : request.elements) {
        if (element.type != RedisClient::ReplyType::BulkString || element.isNull) {
            args.clear();
            return args;
        }
        args.append(element.value);
    }
    return args;
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

class FakeRedisHub : public QObject {
    Q_OBJECT

public slots:
    void start() {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &FakeRedisHub::onNewConnection);
        if (!m_server->listen(QHostAddress::LocalHost, 0)) {
            emit failed(m_server->errorString());
            return;
        }
        emit started(m_server->serverPort());
    }

    void stop() {
        if (!m_server) return;
        for (QTcpSocket* socket : m_server->findChildren<QTcpSocket*>()) {
            socket->disconnectFromHost();
        }
        m_server->close();
        m_subscribers.clear();
    }

signals:
    void started(quint16 port);
    void failed(const QString& reason);

private:
    void onNewConnection() {
        while (m_server->hasPendingConnections()) {
            QTcpSocket* socket = m_server->nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                onReadyRead(socket);
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
                removeSubscriber(socket);
                socket->deleteLater();
            });
        }
    }

    void onReadyRead(QTcpSocket* socket) {
        QByteArray buffer = socket->property("buffer").toByteArray();
        buffer.append(socket->readAll());

        while (!buffer.isEmpty()) {
            RedisClient::Reply request;
            int consumed = 0;
            QString error;
            if (!RedisClient::parseReply(buffer, &request, &consumed, &error)) {
                if (error != "incomplete") {
                    socket->write("-ERR invalid request\r\n");
                    buffer.clear();
                }
                break;
            }

            buffer = buffer.mid(consumed);
            socket->write(handleCommand(request, socket));
            socket->flush();
        }

        socket->setProperty("buffer", buffer);
    }

    QByteArray handleCommand(const RedisClient::Reply& request, QTcpSocket* socket) {
        const QList<QByteArray> args = commandArguments(request);
        if (args.isEmpty()) return "-ERR empty command\r\n";

        const QByteArray command = args.first().toUpper();
        if (command == "PING") {
            return "+PONG\r\n";
        }
        if (command == "AUTH") {
            return "+OK\r\n";
        }
        if (command == "SUBSCRIBE" && args.size() >= 2) {
            const QByteArray channel = args.at(1);
            m_subscribers[channel].insert(socket);

            QByteArray response = "*3\r\n";
            response.append(bulkReply(QByteArrayLiteral("subscribe")));
            response.append(bulkReply(channel));
            response.append(integerReply(m_subscribers.value(channel).size()));
            return response;
        }
        if (command == "PUBLISH" && args.size() >= 3) {
            const QByteArray channel = args.at(1);
            const QByteArray payload = args.at(2);
            int delivered = 0;
            for (QTcpSocket* subscriber : m_subscribers.value(channel)) {
                if (subscriber && subscriber->state() == QAbstractSocket::ConnectedState) {
                    subscriber->write(pubSubMessageReply(channel, payload));
                    subscriber->flush();
                    ++delivered;
                }
            }
            return integerReply(delivered);
        }
        if (command == "SET" && args.size() >= 3) {
            m_strings[args.at(1)] = args.at(2);
            return "+OK\r\n";
        }
        if (command == "SADD" && args.size() >= 3) {
            int added = 0;
            for (int i = 2; i < args.size(); ++i) {
                if (!m_sets[args.at(1)].contains(args.at(i))) {
                    ++added;
                }
                m_sets[args.at(1)].insert(args.at(i));
            }
            return integerReply(added);
        }
        if (command == "SMEMBERS" && args.size() >= 2) {
            return arrayReply(m_sets.value(args.at(1)).values());
        }
        if (command == "MGET" && args.size() >= 2) {
            QByteArray response = "*" + QByteArray::number(args.size() - 1) + "\r\n";
            for (int i = 1; i < args.size(); ++i) {
                const QByteArray key = args.at(i);
                response.append(m_strings.contains(key) ? bulkReply(m_strings.value(key)) : QByteArray("$-1\r\n"));
            }
            return response;
        }
        if (command == "DEL" && args.size() >= 2) {
            int removed = 0;
            for (int i = 1; i < args.size(); ++i) {
                removed += m_strings.remove(args.at(i));
            }
            return integerReply(removed);
        }
        if (command == "SREM" && args.size() >= 3) {
            int removed = 0;
            for (int i = 2; i < args.size(); ++i) {
                removed += m_sets[args.at(1)].remove(args.at(i)) ? 1 : 0;
            }
            return integerReply(removed);
        }
        return "-ERR unsupported command\r\n";
    }

    void removeSubscriber(QTcpSocket* socket) {
        for (auto it = m_subscribers.begin(); it != m_subscribers.end(); ++it) {
            it.value().remove(socket);
        }
    }

    QTcpServer* m_server = nullptr;
    QMap<QByteArray, QByteArray> m_strings;
    QMap<QByteArray, QSet<QByteArray>> m_sets;
    QMap<QByteArray, QSet<QTcpSocket*>> m_subscribers;
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("redis_cross_instance_chat_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    QThread redisThread;
    auto* fakeRedis = new FakeRedisHub;
    fakeRedis->moveToThread(&redisThread);
    QObject::connect(&redisThread, &QThread::finished, fakeRedis, &QObject::deleteLater);

    quint16 redisPort = 0;
    QString startError;
    QEventLoop startLoop;
    QObject::connect(fakeRedis, &FakeRedisHub::started, &startLoop, [&](quint16 port) {
        redisPort = port;
        startLoop.quit();
    });
    QObject::connect(fakeRedis, &FakeRedisHub::failed, &startLoop, [&](const QString& reason) {
        startError = reason;
        startLoop.quit();
    });
    QTimer::singleShot(5000, &startLoop, &QEventLoop::quit);

    redisThread.start();
    QMetaObject::invokeMethod(fakeRedis, "start", Qt::QueuedConnection);
    startLoop.exec();

    bool ok = true;
    ok = expect(redisPort != 0, "fake Redis hub should start") && ok;
    ok = expect(startError.isEmpty(), "fake Redis hub should not report a startup error") && ok;
    if (!ok) {
        redisThread.quit();
        redisThread.wait();
        return 1;
    }

    qputenv("QTNETWORKCHAT_REDIS", "1");
    qputenv("QTNETWORKCHAT_REDIS_HOST", "127.0.0.1");
    qputenv("QTNETWORKCHAT_REDIS_PORT", QByteArray::number(redisPort));
    qputenv("QTNETWORKCHAT_REDIS_PREFIX", "qtchat-cross-instance-test");
    qunsetenv("QTNETWORKCHAT_REDIS_PASSWORD");

    const quint16 serverAPort = freeLocalPort();
    const quint16 serverBPort = freeLocalPort();
    ok = expect(serverAPort != 0, "first chat server test port should be available") && ok;
    ok = expect(serverBPort != 0, "second chat server test port should be available") && ok;
    if (serverAPort == serverBPort) {
        ok = expect(false, "chat server test ports should be distinct") && ok;
    }

    Server serverA;
    Server serverB;
    ok = expect(serverA.start(serverAPort), "first server instance should start") && ok;
    ok = expect(serverB.start(serverBPort), "second server instance should start") && ok;

    Client alice;
    Client bob;
    QStringList aliceGroupMessages;
    QStringList bobGroupMessages;
    QObject::connect(&alice, &Client::newMessage, &app, [&](const Message& msg) {
        if (msg.type == MessageType::Text) {
            aliceGroupMessages << msg.content;
        }
    });
    QObject::connect(&bob, &Client::newMessage, &app, [&](const Message& msg) {
        if (msg.type == MessageType::Text) {
            bobGroupMessages << msg.content;
        }
    });

    ok = expect(registerClient(alice, "960001", "RedisAlice", serverAPort),
                "alice should log in to the first server") && ok;
    ok = expect(registerClient(bob, "960002", "RedisBob", serverBPort),
                "bob should log in to the second server") && ok;
    ok = expect(waitFor([&] { return alice.hasServerGroupSnapshot() && bob.hasServerGroupSnapshot(); }),
                "both clients should receive server group snapshots") && ok;

    const QString groupMessage = "Redis cross instance group chat should arrive once";
    ok = expect(alice.sendMessage(groupMessage),
                "alice should send a public group message") && ok;
    ok = expect(waitFor([&] {
        return aliceGroupMessages.count(groupMessage) == 1
            && bobGroupMessages.count(groupMessage) == 1;
    }), "remote server client should receive the Redis-routed group message") && ok;
    ok = expect(!waitFor([&] {
        return aliceGroupMessages.count(groupMessage) > 1;
    }, 500), "origin server should skip its own Redis event by instance id") && ok;

    alice.disconnectFromServer();
    bob.disconnectFromServer();
    serverA.stop();
    serverB.stop();

    qunsetenv("QTNETWORKCHAT_REDIS");
    qunsetenv("QTNETWORKCHAT_REDIS_HOST");
    qunsetenv("QTNETWORKCHAT_REDIS_PORT");
    qunsetenv("QTNETWORKCHAT_REDIS_PREFIX");
    qunsetenv("QTNETWORKCHAT_REDIS_PASSWORD");

    QMetaObject::invokeMethod(fakeRedis, "stop", Qt::BlockingQueuedConnection);
    redisThread.quit();
    redisThread.wait();

    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}

#include "redis_cross_instance_chat_test.moc"
