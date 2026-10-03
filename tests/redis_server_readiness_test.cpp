#if __has_include(<QtCore/q20algorithm.h>)
#include <QtCore/q20algorithm.h>
#endif

#include "server.h"
#include "redisclient.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
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

class FakeRedisServer final : public QObject {
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
        if (!m_server) return;
        for (QTcpSocket* socket : m_server->findChildren<QTcpSocket*>()) {
            socket->disconnectFromHost();
        }
        m_server->close();
        m_subscribers.clear();
    }

    void disconnectSubscribers() {
        for (const auto& subscribers : m_subscribers) {
            for (QTcpSocket* socket : subscribers) {
                if (socket && socket->state() == QAbstractSocket::ConnectedState) {
                    socket->disconnectFromHost();
                }
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
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
                for (auto it = m_subscribers.begin(); it != m_subscribers.end(); ++it) {
                    it.value().remove(socket);
                }
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
            QByteArray response = "*3\r\n";
            response.append(bulkReply(QByteArrayLiteral("subscribe")));
            response.append(bulkReply(channel));
            response.append(integerReply(m_subscribers.value(channel).size()));
            return response;
        }
        if (command == "SET" && args.size() >= 3) {
            m_strings[args.at(1)] = args.at(2);
            return "+OK\r\n";
        }
        if (command == "SADD" && args.size() >= 3) {
            m_sets[args.at(1)].insert(args.at(2));
            return integerReply(1);
        }
        if (command == "DEL" && args.size() >= 2) {
            return integerReply(m_strings.remove(args.at(1)));
        }
        if (command == "SREM" && args.size() >= 3) {
            return integerReply(m_sets[args.at(1)].remove(args.at(2)) ? 1 : 0);
        }
        if (command == "GET" && args.size() >= 2) {
            const QByteArray key = args.at(1);
            return m_strings.contains(key) ? bulkReply(m_strings.value(key)) : QByteArray("$-1\r\n");
        }
        if (command == "SMEMBERS" && args.size() >= 2) {
            QByteArray response = "*" + QByteArray::number(m_sets.value(args.at(1)).size()) + "\r\n";
            for (const QByteArray& value : m_sets.value(args.at(1))) {
                response.append(bulkReply(value));
            }
            return response;
        }
        if (command == "MGET" && args.size() >= 2) {
            QByteArray response = "*" + QByteArray::number(args.size() - 1) + "\r\n";
            for (int i = 1; i < args.size(); ++i) {
                const QByteArray key = args.at(i);
                response.append(m_strings.contains(key) ? bulkReply(m_strings.value(key)) : QByteArray("$-1\r\n"));
            }
            return response;
        }
        if (command == "PUBLISH" && args.size() >= 3) {
            return integerReply(0);
        }
        return "-ERR unsupported command\r\n";
    }

    QTcpServer* m_server = nullptr;
    QMap<QByteArray, QByteArray> m_strings;
    QMap<QByteArray, QSet<QByteArray>> m_sets;
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

    redisThread.start();
    QMetaObject::invokeMethod(fakeRedis, "start", Qt::QueuedConnection);
    QTimer::singleShot(5000, &startLoop, &QEventLoop::quit);
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
    qputenv("QTNETWORKCHAT_REDIS_PREFIX", "qtchat-server-readiness-test");

    const quint16 chatPort = freeLocalPort();
    ok = expect(chatPort != 0, "a local chat test port should be available") && ok;
    if (!ok) {
        redisThread.quit();
        redisThread.wait();
        return 1;
    }

    Server server;
    ok = expect(server.start(chatPort), "server should start when Redis is reachable") && ok;
    ok = expect(server.isServiceReady(), "server should be ready right after a successful Redis-backed startup") && ok;

    QTcpSocket clientSocket;
    QByteArray clientBuffer;
    ok = expect(connectSocket(&clientSocket, chatPort), "client should connect to Redis-backed chat server") && ok;
    ok = expect(loginSocket(&clientSocket, QStringLiteral("readiness-user"), QStringLiteral("Readiness User")),
                "client should submit login before subscriber recovery probe") && ok;
    ok = expect(socketBufferContains(&clientSocket, &clientBuffer, QByteArrayLiteral("\"type\":\"login_success\""), 5000),
                "client should receive login result before subscriber recovery probe") && ok;
    clientBuffer.clear();

    QMetaObject::invokeMethod(fakeRedis, "disconnectSubscribers", Qt::BlockingQueuedConnection);
    ok = expect(waitFor([&] { return !server.isServiceReady(); }, 4000),
                "server should become not ready after the Redis subscription disconnects") && ok;

    QJsonObject publicMessage;
    publicMessage["type"] = "message";
    publicMessage["messageType"] = static_cast<int>(MessageType::Text);
    publicMessage["senderId"] = "readiness-user";
    publicMessage["senderName"] = "Readiness User";
    publicMessage["content"] = "message should trigger redis subscriber recovery";
    ok = expect(writeJsonLine(&clientSocket, publicMessage),
                "client should send a message while subscriber readiness is recovering") && ok;

    ok = expect(waitFor([&] { return server.isServiceReady(); }, 5000),
                "server should become ready again when a client request triggers subscriber recovery") && ok;
    ok = expect(!socketBufferContains(&clientSocket, &clientBuffer, QByteArrayLiteral("Redis"), 500),
                "client request should not be rejected as Redis not ready after subscriber recovery") && ok;

    server.stop();
    qunsetenv("QTNETWORKCHAT_REDIS");
    qunsetenv("QTNETWORKCHAT_REDIS_HOST");
    qunsetenv("QTNETWORKCHAT_REDIS_PORT");
    qunsetenv("QTNETWORKCHAT_REDIS_PREFIX");

    QMetaObject::invokeMethod(fakeRedis, "stop", Qt::BlockingQueuedConnection);
    redisThread.quit();
    redisThread.wait();

    return ok ? 0 : 1;
}

#include "redis_server_readiness_test.moc"
