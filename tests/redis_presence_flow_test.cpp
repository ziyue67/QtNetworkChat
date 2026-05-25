#include "redisclient.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QSet>
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
}

class FakeRedisServer : public QObject {
    Q_OBJECT

public slots:
    void start() {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &FakeRedisServer::onNewConnection);
        if (!m_server->listen(QHostAddress::LocalHost, 0)) {
            emit failed(m_server->errorString());
            return;
        }
        emit started(m_server->serverPort());
    }

    void stop() {
        if (m_server) {
            m_server->close();
        }
    }

    void disconnectSubscribers() {
        const QSet<QTcpSocket*> subscribers = allSubscribers();
        for (QTcpSocket* socket : subscribers) {
            if (socket && socket->state() == QAbstractSocket::ConnectedState) {
                socket->disconnectFromHost();
            }
        }
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
        if (command == "SUBSCRIBE" && args.size() >= 2) {
            const QByteArray channel = args.at(1);
            m_subscribers[channel].insert(socket);
            QTimer::singleShot(20, this, [socket, channel]() {
                if (socket && socket->state() == QAbstractSocket::ConnectedState) {
                    socket->write(pubSubMessageReply(channel, QByteArrayLiteral("{\"kind\":\"subscribed\"}")));
                    socket->flush();
                }
            });
            QByteArray response = "*3\r\n";
            response.append(bulkReply(QByteArrayLiteral("subscribe")));
            response.append(bulkReply(channel));
            response.append(integerReply(1));
            return response;
        }
        if (command == "PUBLISH" && args.size() >= 3) {
            const QByteArray channel = args.at(1);
            const QByteArray payload = args.at(2);
            m_published[channel].append(payload);
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
        if (command == "SET" && args.size() >= 5 && args.at(3).toUpper() == "EX") {
            m_strings[args.at(1)] = args.at(2);
            return "+OK\r\n";
        }
        if (command == "SADD" && args.size() >= 3) {
            m_sets[args.at(1)].insert(args.at(2));
            return integerReply(1);
        }
        if (command == "SMEMBERS" && args.size() >= 2) {
            return arrayReply(m_sets.value(args.at(1)).values());
        }
        if (command == "GET" && args.size() >= 2) {
            const QByteArray key = args.at(1);
            return m_strings.contains(key) ? bulkReply(m_strings.value(key)) : QByteArray("$-1\r\n");
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
            return integerReply(m_strings.remove(args.at(1)));
        }
        if (command == "SREM" && args.size() >= 3) {
            return integerReply(m_sets[args.at(1)].remove(args.at(2)) ? 1 : 0);
        }
        return "-ERR unsupported command\r\n";
    }

    QSet<QTcpSocket*> allSubscribers() const {
        QSet<QTcpSocket*> sockets;
        for (const QSet<QTcpSocket*>& channelSubscribers : m_subscribers) {
            sockets.unite(channelSubscribers);
        }
        return sockets;
    }

    void removeSubscriber(QTcpSocket* socket) {
        for (auto it = m_subscribers.begin(); it != m_subscribers.end(); ++it) {
            it.value().remove(socket);
        }
    }

    QTcpServer* m_server = nullptr;
    QMap<QByteArray, QByteArray> m_strings;
    QMap<QByteArray, QSet<QByteArray>> m_sets;
    QMap<QByteArray, QList<QByteArray>> m_published;
    QMap<QByteArray, QSet<QTcpSocket*>> m_subscribers;
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    QThread redisThread;
    auto* fakeRedis = new FakeRedisServer;
    fakeRedis->moveToThread(&redisThread);
    QObject::connect(&redisThread, &QThread::finished, fakeRedis, &QObject::deleteLater);

    quint16 redisPort = 0;
    QString startError;
    QEventLoop startLoop;
    QObject::connect(fakeRedis, &FakeRedisServer::started, &startLoop, [&](quint16 port) {
        redisPort = port;
        startLoop.quit();
    });
    QObject::connect(fakeRedis, &FakeRedisServer::failed, &startLoop, [&](const QString& reason) {
        startError = reason;
        startLoop.quit();
    });
    QTimer::singleShot(5000, &startLoop, &QEventLoop::quit);

    redisThread.start();
    QMetaObject::invokeMethod(fakeRedis, "start", Qt::QueuedConnection);
    startLoop.exec();

    bool ok = true;
    ok = expect(redisPort != 0, "fake Redis server should start") && ok;
    ok = expect(startError.isEmpty(), "fake Redis server should not report a startup error") && ok;
    if (!ok) {
        redisThread.quit();
        redisThread.wait();
        return 1;
    }

    qputenv("QTNETWORKCHAT_REDIS", "1");
    qputenv("QTNETWORKCHAT_REDIS_HOST", "127.0.0.1");
    qputenv("QTNETWORKCHAT_REDIS_PORT", QByteArray::number(redisPort));
    qputenv("QTNETWORKCHAT_REDIS_PREFIX", "qtchat-flow-test");

    RedisClient client;
    client.configureFromEnvironment();
    ok = expect(client.connectToServer(2000), "Redis client should connect to the fake Redis service") && ok;
    ok = expect(client.publish("messages", QByteArrayLiteral("{\"kind\":\"ping\"}"), 2000),
                "Redis client should publish a namespaced Pub/Sub payload") && ok;

    RedisSubscriber subscriber;
    subscriber.configureFromEnvironment();
    QList<RedisClient::PubSubMessage> pubSubMessages;
    QObject::connect(&subscriber, &RedisSubscriber::messageReceived, &app, [&](const RedisClient::PubSubMessage& message) {
        pubSubMessages.append(message);
    });
    ok = expect(subscriber.subscribe("messages", 2000),
                "Redis subscriber should subscribe to the fake Redis service") && ok;
    ok = expect(waitFor([&] { return !pubSubMessages.isEmpty(); }),
                "Redis subscriber should consume a fake Pub/Sub message") && ok;
    if (!pubSubMessages.isEmpty()) {
        ok = expect(pubSubMessages.first().channel == "qtchat-flow-test:pubsub:messages",
                    "Redis subscriber should expose the subscribed channel") && ok;
        ok = expect(pubSubMessages.first().payload == QByteArrayLiteral("{\"kind\":\"subscribed\"}"),
                    "Redis subscriber should expose the message payload") && ok;
    }

    const int messagesBeforeDisconnect = pubSubMessages.size();
    QMetaObject::invokeMethod(fakeRedis, "disconnectSubscribers", Qt::BlockingQueuedConnection);
    ok = expect(waitFor([&] {
        return subscriber.isSubscribed() && pubSubMessages.size() > messagesBeforeDisconnect;
    }), "Redis subscriber should reconnect and resubscribe after a dropped subscription connection") && ok;

    const QByteArray postReconnectPayload = QByteArrayLiteral("{\"kind\":\"after-reconnect\"}");
    ok = expect(client.publish("messages", postReconnectPayload, 2000),
                "Redis client should publish after subscriber reconnect") && ok;
    ok = expect(waitFor([&] {
        for (const RedisClient::PubSubMessage& message : pubSubMessages) {
            if (message.payload == postReconnectPayload) return true;
        }
        return false;
    }), "Redis subscriber should receive published messages after resubscribing") && ok;

    ok = expect(client.setPresence("940001", "RedisFlow", 90, 2000),
                "Redis client should write presence and index entries") && ok;
    ok = expect(client.hasPresence("940001", 2000),
                "Redis client should find an active single-user presence entry") && ok;

    QList<RedisClient::Presence> users;
    ok = expect(client.fetchOnlinePresence(&users, 2000),
                "Redis client should fetch online presence from the fake Redis service") && ok;
    ok = expect(users.size() == 1, "exactly one Redis presence entry should be returned") && ok;
    if (!users.isEmpty()) {
        ok = expect(users.first().userId == "940001", "presence user id should match") && ok;
        ok = expect(users.first().userName == "RedisFlow", "presence user name should match") && ok;
    }

    ok = expect(client.clearPresence("940001", 2000),
                "Redis client should clear presence and index entries") && ok;
    ok = expect(!client.hasPresence("940001", 2000),
                "Redis client should not find presence after clearing it") && ok;
    users.clear();
    ok = expect(client.fetchOnlinePresence(&users, 2000),
                "Redis client should fetch after clearing presence") && ok;
    ok = expect(users.isEmpty(), "cleared Redis presence should not be returned") && ok;

    qunsetenv("QTNETWORKCHAT_REDIS");
    qunsetenv("QTNETWORKCHAT_REDIS_HOST");
    qunsetenv("QTNETWORKCHAT_REDIS_PORT");
    qunsetenv("QTNETWORKCHAT_REDIS_PREFIX");

    subscriber.disconnectFromServer();
    QMetaObject::invokeMethod(fakeRedis, "stop", Qt::BlockingQueuedConnection);
    redisThread.quit();
    redisThread.wait();

    return ok ? 0 : 1;
}

#include "redis_presence_flow_test.moc"
