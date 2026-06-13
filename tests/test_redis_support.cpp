#include "test_redis_support.h"
#include "redisclient.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

namespace {
QByteArray bulkReply(const QByteArray& value) {
    return "$" + QByteArray::number(value.size()) + "\r\n" + value + "\r\n";
}

QByteArray integerReply(int value) {
    return ":" + QByteArray::number(value) + "\r\n";
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

class TestRedisServerEnvironment::Impl final : public QObject {
    Q_OBJECT

public:
    explicit Impl(QString prefix)
        : m_prefix(std::move(prefix)) {
    }

    bool start(QString* error) {
        m_thread = new QThread;
        moveToThread(m_thread);
        connect(m_thread, &QThread::finished, this, &QObject::deleteLater);

        QString startError;
        quint16 startedPort = 0;
        QEventLoop loop;
        connect(this, &Impl::started, &loop, [&](quint16 port) {
            startedPort = port;
            loop.quit();
        });
        connect(this, &Impl::failed, &loop, [&](const QString& reason) {
            startError = reason;
            loop.quit();
        });

        m_thread->start();
        QMetaObject::invokeMethod(this, "startServer", Qt::QueuedConnection);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();

        if (!startError.isEmpty() || startedPort == 0) {
            if (error) *error = startError.isEmpty() ? QStringLiteral("fake-redis-start-timeout") : startError;
            stop();
            return false;
        }

        m_port = startedPort;
        if (error) error->clear();
        return true;
    }

    void stop() {
        if (m_thread) {
            QMetaObject::invokeMethod(this, "stopServer", Qt::BlockingQueuedConnection);
            m_thread->quit();
            m_thread->wait();
            m_thread = nullptr;
        }
        m_port = 0;
        m_strings.clear();
        m_sets.clear();
        m_subscribers.clear();
    }

    quint16 port() const { return m_port; }
    QString prefix() const { return m_prefix; }

public slots:
    void startServer() {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &Impl::onNewConnection);
        if (!m_server->listen(QHostAddress::LocalHost, 0)) {
            emit failed(m_server->errorString());
            return;
        }
        emit started(m_server->serverPort());
    }

    void stopServer() {
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
                if (error != QStringLiteral("incomplete")) {
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
                    m_sets[args.at(1)].insert(args.at(i));
                    ++added;
                }
            }
            return integerReply(added);
        }
        if (command == "SMEMBERS" && args.size() >= 2) {
            QByteArray response = "*" + QByteArray::number(m_sets.value(args.at(1)).size()) + "\r\n";
            for (const QByteArray& value : m_sets.value(args.at(1))) {
                response.append(bulkReply(value));
            }
            return response;
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

    QString m_prefix;
    QThread* m_thread = nullptr;
    QTcpServer* m_server = nullptr;
    quint16 m_port = 0;
    QMap<QByteArray, QByteArray> m_strings;
    QMap<QByteArray, QSet<QByteArray>> m_sets;
    QMap<QByteArray, QSet<QTcpSocket*>> m_subscribers;
};

TestRedisServerEnvironment::TestRedisServerEnvironment(QString prefix)
    : m_prefix(std::move(prefix))
    , m_impl(new Impl(m_prefix)) {
}

TestRedisServerEnvironment::~TestRedisServerEnvironment() {
    stop();
}

bool TestRedisServerEnvironment::start(QString* error) {
    return m_impl->start(error);
}

void TestRedisServerEnvironment::stop() {
    if (m_impl) {
        m_impl->stop();
    }
    clearEnvironment();
}

quint16 TestRedisServerEnvironment::port() const {
    return m_impl ? m_impl->port() : 0;
}

QString TestRedisServerEnvironment::prefix() const {
    return m_prefix;
}

void TestRedisServerEnvironment::applyEnvironment() const {
    qputenv("QTNETWORKCHAT_REDIS", "1");
    qputenv("QTNETWORKCHAT_REDIS_HOST", "127.0.0.1");
    qputenv("QTNETWORKCHAT_REDIS_PORT", QByteArray::number(port()));
    qputenv("QTNETWORKCHAT_REDIS_PREFIX", m_prefix.toUtf8());
    qunsetenv("QTNETWORKCHAT_REDIS_PASSWORD");
}

void TestRedisServerEnvironment::clearEnvironment() const {
    qunsetenv("QTNETWORKCHAT_REDIS");
    qunsetenv("QTNETWORKCHAT_REDIS_HOST");
    qunsetenv("QTNETWORKCHAT_REDIS_PORT");
    qunsetenv("QTNETWORKCHAT_REDIS_PREFIX");
    qunsetenv("QTNETWORKCHAT_REDIS_PASSWORD");
}

#include "test_redis_support.moc"
