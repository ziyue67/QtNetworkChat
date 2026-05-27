#include "client.h"
#include "objectstore.h"
#include "redisclient.h"
#include "server.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QTemporaryDir>

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

QByteArray makePatternPayload(int size) {
    QByteArray payload(size, Qt::Uninitialized);
    for (int i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>('A' + (i % 26));
    }
    return payload;
}

QByteArray redisMessageEventPayload(MessageType messageType,
                                    const QString& receiverId,
                                    const QString& fileName,
                                    const QByteArray& filePayload) {
    QJsonObject message;
    message["senderId"] = "external-instance";
    message["senderName"] = "ExternalInstance";
    message["receiverId"] = receiverId;
    message["content"] = QString(messageType == MessageType::Image ? "发送了图片: %1" : "发送了文件: %1").arg(fileName);
    message["type"] = static_cast<int>(messageType);
    message["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    message["fileName"] = fileName;
    message["fileSize"] = QString::number(filePayload.size());
    message["chunkSize"] = QString::number(256 * 1024);
    message["chunkCount"] = QString::number((filePayload.size() + 256 * 1024 - 1) / (256 * 1024));
    message["fileData"] = QString::fromLatin1(filePayload.toBase64());
    message["hasFile"] = true;

    QJsonObject event;
    event["eventType"] = "chat_message";
    event["instanceId"] = "external-injected-instance";
    event["deliveryState"] = "remote";
    event["isPrivate"] = true;
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    event["message"] = message;
    return QJsonDocument(event).toJson(QJsonDocument::Compact);
}

QByteArray redisRawMessageEventPayload(const QJsonObject& message) {
    QJsonObject event;
    event["eventType"] = "chat_message";
    event["instanceId"] = "external-injected-instance";
    event["deliveryState"] = "remote";
    event["isPrivate"] = !message["receiverId"].toString().isEmpty();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    event["message"] = message;
    return QJsonDocument(event).toJson(QJsonDocument::Compact);
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

    void setPublishFailure(bool enabled) {
        m_failPublishes = enabled;
    }

    void injectMessageEvent(const QByteArray& payload) {
        const QByteArray channel = QByteArrayLiteral("qtchat-cross-instance-test:pubsub:messages");
        for (QTcpSocket* subscriber : m_subscribers.value(channel)) {
            if (subscriber && subscriber->state() == QAbstractSocket::ConnectedState) {
                subscriber->write(pubSubMessageReply(channel, payload));
                subscriber->flush();
            }
        }
    }

signals:
    void started(quint16 port);
    void failed(const QString& reason);
    void published(const QByteArray& channel, const QByteArray& payload);

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
            if (m_failPublishes) {
                return "-ERR injected publish failure\r\n";
            }
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
            emit published(channel, payload);
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
    bool m_failPublishes = false;
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
    qputenv("QTNETWORKCHAT_LARGE_FILE_ROUTING", "1");
    qputenv("QTNETWORKCHAT_OBJECT_STORE", "filesystem");
    qputenv("QTNETWORKCHAT_OBJECT_TTL_HOURS", "24");
    QTemporaryDir objectRoot;
    ok = expect(objectRoot.isValid(), "temporary object store root should be available") && ok;
    qputenv("QTNETWORKCHAT_OBJECT_ROOT", objectRoot.path().toUtf8());

    QList<QByteArray> publishedMessagePayloads;
    QObject::connect(fakeRedis, &FakeRedisHub::published, &app, [&](const QByteArray& channel, const QByteArray& payload) {
        if (channel == QByteArrayLiteral("qtchat-cross-instance-test:pubsub:messages")) {
            publishedMessagePayloads.append(payload);
        }
    });
    auto findPublishedEvent = [&publishedMessagePayloads](const QString& eventType,
                                                          const QString& fileName = QString(),
                                                          const QString& objectKey = QString()) {
        for (const QByteArray& payload : publishedMessagePayloads) {
            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            if (!doc.isObject()) continue;
            const QJsonObject event = doc.object();
            if (event["eventType"].toString() == eventType
                && (fileName.isEmpty() || event["fileName"].toString() == fileName)
                && (objectKey.isEmpty() || event["objectKey"].toString() == objectKey)) {
                return event;
            }
        }
        return QJsonObject();
    };
    auto findLargeFileOffer = [&findPublishedEvent](const QString& fileName) {
        return findPublishedEvent("large_file_offer", fileName);
    };

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
    QStringList bobPrivateMessages;
    QStringList bobFileNames;
    QList<QByteArray> bobFilePayloads;
    QStringList bobImageNames;
    QList<QByteArray> bobImagePayloads;
    QVector<Message> serverBMessages;
    QObject::connect(&serverB, &Server::newMessage, &app, [&](const Message& msg) {
        serverBMessages.append(msg);
    });
    QObject::connect(&alice, &Client::newMessage, &app, [&](const Message& msg) {
        if (msg.type == MessageType::Text) {
            aliceGroupMessages << msg.content;
        }
    });
    QObject::connect(&bob, &Client::newMessage, &app, [&](const Message& msg) {
        if (msg.type == MessageType::Text) {
            bobGroupMessages << msg.content;
        } else if (msg.type == MessageType::Private) {
            bobPrivateMessages << msg.content;
        } else if (msg.type == MessageType::File) {
            bobFileNames << msg.fileName;
            bobFilePayloads << msg.fileData;
        } else if (msg.type == MessageType::Image) {
            bobImageNames << msg.fileName;
            bobImagePayloads << msg.fileData;
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

    const QString privateMessage = "Redis cross instance private chat should not be queued offline";
    ok = expect(alice.sendPrivateMessage("960002", privateMessage),
                "alice should send a private message to a user on the second server") && ok;
    ok = expect(waitFor([&] {
        return bobPrivateMessages.count(privateMessage) == 1;
    }), "bob should receive the private message through Redis cross-instance routing") && ok;

    QTemporaryDir transferDir;
    ok = expect(transferDir.isValid(), "temporary transfer directory should be available") && ok;
    const QString fileName = "redis-cross-instance.txt";
    const QString filePath = transferDir.filePath(fileName);
    const QByteArray filePayload = QByteArrayLiteral("small redis cross instance file payload");
    QFile transferFile(filePath);
    ok = expect(transferFile.open(QIODevice::WriteOnly),
                "temporary transfer file should open for writing") && ok;
    if (transferFile.isOpen()) {
        ok = expect(transferFile.write(filePayload) == filePayload.size(),
                    "temporary transfer file should be written") && ok;
        transferFile.close();
    }
    ok = expect(alice.sendFile(filePath, "960002"),
                "alice should send a small file to a user on the second server") && ok;
    ok = expect(waitFor([&] {
        return bobFileNames.contains(fileName) && bobFilePayloads.contains(filePayload);
    }), "bob should receive the small file through Redis cross-instance routing") && ok;

    const QString imageName = "redis-cross-instance-image.png";
    const QString imagePath = transferDir.filePath(imageName);
    const QByteArray imagePayload = QByteArray::fromHex(
        "89504E470D0A1A0A"
        "0000000D49484452000000010000000108060000001F15C489"
        "0000000D49444154789C6360606060000000050001A5F64540"
        "0000000049454E44AE426082");
    QFile imageFile(imagePath);
    ok = expect(imageFile.open(QIODevice::WriteOnly),
                "temporary image file should open for writing") && ok;
    if (imageFile.isOpen()) {
        ok = expect(imageFile.write(imagePayload) == imagePayload.size(),
                    "temporary image file should be written") && ok;
        imageFile.close();
    }
    ok = expect(alice.sendImage(imagePath, "960002"),
                "alice should send a small image to a user on the second server") && ok;
    ok = expect(waitFor([&] {
        return bobImageNames.contains(imageName) && bobImagePayloads.contains(imagePayload);
    }), "bob should receive the small image through Redis cross-instance routing") && ok;

    QTcpSocket rawReceiver;
    QByteArray rawBuffer;
    bool rawReceiverLoggedIn = false;
    bool rawReceiverCompletedFile = false;
    int rawReceiverFileChunkAttempts = 0;
    auto drainRawReceiver = [&]() {
        rawBuffer.append(rawReceiver.readAll());
        while (rawBuffer.contains('\n')) {
            const int newlineIndex = rawBuffer.indexOf('\n');
            const QByteArray line = rawBuffer.left(newlineIndex);
            rawBuffer = rawBuffer.mid(newlineIndex + 1);
            if (line.trimmed().isEmpty()) continue;

            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) continue;
            const QJsonObject obj = doc.object();
            const QString type = obj["type"].toString();
            if (type == "login_success") {
                rawReceiverLoggedIn = true;
            } else if (type == "file_chunk") {
                ++rawReceiverFileChunkAttempts;
                const qint64 chunkIndex = obj["chunkIndex"].toVariant().toLongLong();
                const qint64 chunkSize = obj["chunkSize"].toVariant().toLongLong();
                const qint64 fileSize = obj["fileSize"].toVariant().toLongLong();
                const QByteArray chunkData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());
                const qint64 receivedBytes = qMin(fileSize, chunkIndex * chunkSize + chunkData.size());

                QJsonObject ack;
                ack["type"] = "file_chunk_ack";
                ack["transferId"] = obj["transferId"].toString();
                ack["chunkIndex"] = obj["chunkIndex"].toString();
                ack["accepted"] = true;
                ack["reason"] = "";
                ack["receivedBytes"] = QString::number(rawReceiverFileChunkAttempts == 1 ? 1 : receivedBytes);
                rawReceiver.write(QJsonDocument(ack).toJson(QJsonDocument::Compact));
                rawReceiver.write("\n");
                rawReceiver.flush();

                if (rawReceiverFileChunkAttempts > 1 && receivedBytes >= fileSize) {
                    rawReceiverCompletedFile = true;
                }
            }
        }
    };
    QObject::connect(&rawReceiver, &QTcpSocket::readyRead, &app, drainRawReceiver);
    rawReceiver.connectToHost("127.0.0.1", serverBPort);
    ok = expect(rawReceiver.waitForConnected(5000),
                "raw Redis receiver should connect to the second server") && ok;
    QJsonObject rawLogin;
    rawLogin["type"] = "login";
    rawLogin["mode"] = "register";
    rawLogin["account"] = "960009";
    rawLogin["password"] = "secret";
    rawLogin["userName"] = "RawRedisReceiver";
    rawReceiver.write(QJsonDocument(rawLogin).toJson(QJsonDocument::Compact));
    rawReceiver.write("\n");
    rawReceiver.flush();
    ok = expect(waitFor([&] {
        drainRawReceiver();
        return rawReceiverLoggedIn;
    }), "raw Redis receiver should log in") && ok;

    const QString invalidAckForwardFileName = "redis-invalid-ack-progress.txt";
    const QByteArray invalidAckForwardPayload = QByteArrayLiteral("redis forwarded file retry payload");
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, redisMessageEventPayload(MessageType::File,
                                                                         "960009",
                                                                         invalidAckForwardFileName,
                                                                         invalidAckForwardPayload)));
    ok = expect(waitFor([&] {
        drainRawReceiver();
        return rawReceiverCompletedFile;
    }), "raw Redis receiver should complete after corrected forwarded file ack") && ok;
    ok = expect(rawReceiverFileChunkAttempts == 2,
                "server should resend a forwarded file chunk after invalid accepted ack progress") && ok;
    rawReceiver.disconnectFromHost();

    const QString injectedLargeFileName = "redis-injected-large-pubsub.bin";
    const QByteArray injectedLargeFilePayload = makePatternPayload(1024 * 1024 + 4096);

    bobFileNames.clear();
    bobFilePayloads.clear();
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, redisMessageEventPayload(MessageType::File,
                                                                         "960002",
                                                                         injectedLargeFileName,
                                                                         injectedLargeFilePayload)));
    ok = expect(!waitFor([&] {
        return bobFileNames.contains(injectedLargeFileName);
    }, 800), "subscriber should ignore oversized Redis file payload events") && ok;

    const QString injectedLargeImageName = "redis-injected-large-pubsub.png";
    const QByteArray injectedLargeImagePayload = makePatternPayload(1024 * 1024 + 4096);
    bobImageNames.clear();
    bobImagePayloads.clear();
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, redisMessageEventPayload(MessageType::Image,
                                                                         "960002",
                                                                         injectedLargeImageName,
                                                                         injectedLargeImagePayload)));
    ok = expect(!waitFor([&] {
        return bobImageNames.contains(injectedLargeImageName);
    }, 800), "subscriber should ignore oversized Redis image payload events") && ok;

    QJsonObject missingSenderMessage;
    missingSenderMessage["senderName"] = "ExternalInstance";
    missingSenderMessage["receiverId"] = "960002";
    missingSenderMessage["content"] = "Redis missing sender id should be ignored";
    missingSenderMessage["type"] = static_cast<int>(MessageType::Private);
    missingSenderMessage["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    bobPrivateMessages.clear();
    serverBMessages.clear();
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, redisRawMessageEventPayload(missingSenderMessage)));
    ok = expect(!waitFor([&] {
        return bobPrivateMessages.contains(missingSenderMessage["content"].toString())
            || !serverBMessages.isEmpty();
    }, 800), "subscriber should ignore Redis events without sender id") && ok;

    QJsonObject unknownTypeMessage;
    unknownTypeMessage["senderId"] = "external-instance";
    unknownTypeMessage["senderName"] = "ExternalInstance";
    unknownTypeMessage["receiverId"] = "960002";
    unknownTypeMessage["content"] = "Redis unknown message type should be ignored";
    unknownTypeMessage["type"] = 999;
    unknownTypeMessage["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    serverBMessages.clear();
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, redisRawMessageEventPayload(unknownTypeMessage)));
    ok = expect(!waitFor([&] {
        return !serverBMessages.isEmpty();
    }, 800), "subscriber should ignore Redis events with unknown message types") && ok;

    QJsonObject wrongEventTypeMessage;
    wrongEventTypeMessage["senderId"] = "external-instance";
    wrongEventTypeMessage["senderName"] = "ExternalInstance";
    wrongEventTypeMessage["receiverId"] = "960002";
    wrongEventTypeMessage["content"] = "Redis non-chat event should be ignored";
    wrongEventTypeMessage["type"] = static_cast<int>(MessageType::Private);
    wrongEventTypeMessage["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QJsonObject wrongEventTypeEvent;
    wrongEventTypeEvent["eventType"] = "presence_update";
    wrongEventTypeEvent["instanceId"] = "external-injected-instance";
    wrongEventTypeEvent["deliveryState"] = "remote";
    wrongEventTypeEvent["isPrivate"] = true;
    wrongEventTypeEvent["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    wrongEventTypeEvent["message"] = wrongEventTypeMessage;
    bobPrivateMessages.clear();
    serverBMessages.clear();
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, QJsonDocument(wrongEventTypeEvent).toJson(QJsonDocument::Compact)));
    ok = expect(!waitFor([&] {
        return bobPrivateMessages.contains(wrongEventTypeMessage["content"].toString())
            || !serverBMessages.isEmpty();
    }, 800), "subscriber should ignore Redis events with non-chat event types") && ok;

    QJsonObject emptyMessageEvent;
    emptyMessageEvent["eventType"] = "chat_message";
    emptyMessageEvent["instanceId"] = "external-injected-instance";
    emptyMessageEvent["deliveryState"] = "remote";
    emptyMessageEvent["isPrivate"] = true;
    emptyMessageEvent["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    serverBMessages.clear();
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, QJsonDocument(emptyMessageEvent).toJson(QJsonDocument::Compact)));
    ok = expect(!waitFor([&] {
        return !serverBMessages.isEmpty();
    }, 800), "subscriber should ignore Redis events with empty messages") && ok;

    const QString largeFileName = "redis-large-offline-fallback.bin";
    const QString largeFilePath = transferDir.filePath(largeFileName);
    const QByteArray largeFilePayload = makePatternPayload(1024 * 1024 + 4096);
    QFile largeFile(largeFilePath);
    ok = expect(largeFile.open(QIODevice::WriteOnly),
                "large transfer file should open for writing") && ok;
    if (largeFile.isOpen()) {
        ok = expect(largeFile.write(largeFilePayload) == largeFilePayload.size(),
                    "large transfer file should be written") && ok;
        largeFile.close();
    }
    bobFileNames.clear();
    bobFilePayloads.clear();
    ok = expect(alice.sendFile(largeFilePath, "960002"),
                "alice should upload a large file while bob is online on another server") && ok;
    ok = expect(waitFor([&] {
        return !findLargeFileOffer(largeFileName).isEmpty();
    }), "large files should publish a small Redis object-store offer") && ok;
    const QJsonObject largeFileOffer = findLargeFileOffer(largeFileName);
    const QString largeFileObjectKey = largeFileOffer["objectKey"].toString();
    ok = expect(FilesystemObjectStore::isValidObjectKey(largeFileObjectKey),
                "large file offer should contain a safe object key") && ok;
    ok = expect(largeFileOffer["receiverId"].toString() == "960002",
                "large file offer should target the remote online receiver") && ok;
    ok = expect(largeFileOffer["messageType"].toString() == "File",
                "large file offer should identify file messages") && ok;
    ok = expect(largeFileOffer["fileSize"].toVariant().toLongLong() == largeFilePayload.size(),
                "large file offer should carry the original file size") && ok;
    ok = expect(!largeFileOffer["transferId"].toString().trimmed().isEmpty(),
                "large file offer should carry a transfer id") && ok;
    const FilesystemObjectStore objectStore(objectRoot.path());
    const FilesystemObjectStore::ValidationResult largeObjectValidation =
        objectStore.validateObject(largeFileObjectKey,
                                   largeFilePayload.size(),
                                   largeFileOffer["fileHash"].toString());
    ok = expect(largeObjectValidation.ok,
                "large file offer should point to an object matching the advertised size and hash") && ok;
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_claim", QString(), largeFileObjectKey).isEmpty();
    }), "remote server should claim a large file offer for its local online receiver") && ok;
    ok = expect(waitFor([&] {
        return bobFileNames.contains(largeFileName) && bobFilePayloads.contains(largeFilePayload);
    }, 9000), "bob should receive the large file through object-store offer routing") && ok;

    const QString encodedOverflowFileName = "redis-encoded-payload-overflow.bin";
    const QString encodedOverflowFilePath = transferDir.filePath(encodedOverflowFileName);
    const QByteArray encodedOverflowPayload = makePatternPayload(800 * 1024);
    QFile encodedOverflowFile(encodedOverflowFilePath);
    ok = expect(encodedOverflowFile.open(QIODevice::WriteOnly),
                "encoded-overflow transfer file should open for writing") && ok;
    if (encodedOverflowFile.isOpen()) {
        ok = expect(encodedOverflowFile.write(encodedOverflowPayload) == encodedOverflowPayload.size(),
                    "encoded-overflow transfer file should be written") && ok;
        encodedOverflowFile.close();
    }
    bobFileNames.clear();
    bobFilePayloads.clear();
    ok = expect(alice.sendFile(encodedOverflowFilePath, "960002"),
                "alice should upload a file whose encoded Redis event would exceed the Pub/Sub limit") && ok;
    ok = expect(waitFor([&] {
        return !findLargeFileOffer(encodedOverflowFileName).isEmpty();
    }), "encoded-overflow files should publish a small Redis object-store offer") && ok;
    const QJsonObject encodedOverflowOffer = findLargeFileOffer(encodedOverflowFileName);
    ok = expect(FilesystemObjectStore::isValidObjectKey(encodedOverflowOffer["objectKey"].toString()),
                "encoded-overflow offer should contain a safe object key") && ok;
    const FilesystemObjectStore::ValidationResult encodedOverflowValidation =
        objectStore.validateObject(encodedOverflowOffer["objectKey"].toString(),
                                   encodedOverflowPayload.size(),
                                   encodedOverflowOffer["fileHash"].toString());
    ok = expect(encodedOverflowValidation.ok,
                "encoded-overflow offer should point to an object matching the advertised size and hash") && ok;
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_claim", QString(), encodedOverflowOffer["objectKey"].toString()).isEmpty();
    }), "remote server should claim an encoded-overflow offer for its local online receiver") && ok;
    ok = expect(waitFor([&] {
        return bobFileNames.contains(encodedOverflowFileName) && bobFilePayloads.contains(encodedOverflowPayload);
    }, 9000), "bob should receive the encoded-overflow file through object-store offer routing") && ok;

    const QString fallbackPrivateMessage = "Redis private publish failure should fall back offline";
    bobPrivateMessages.clear();
    QMetaObject::invokeMethod(fakeRedis, "setPublishFailure", Qt::BlockingQueuedConnection, Q_ARG(bool, true));
    ok = expect(alice.sendPrivateMessage("960002", fallbackPrivateMessage),
                "alice should send a private message even when Redis publish fails") && ok;
    ok = expect(!waitFor([&] {
        return bobPrivateMessages.contains(fallbackPrivateMessage);
    }, 500), "bob should not receive the private message immediately when Redis publish fails") && ok;

    const QString fallbackFileName = "redis-publish-fallback.txt";
    const QString fallbackFilePath = transferDir.filePath(fallbackFileName);
    const QByteArray fallbackFilePayload = QByteArrayLiteral("small redis file should fall back offline");
    QFile fallbackFile(fallbackFilePath);
    ok = expect(fallbackFile.open(QIODevice::WriteOnly),
                "fallback transfer file should open for writing") && ok;
    if (fallbackFile.isOpen()) {
        ok = expect(fallbackFile.write(fallbackFilePayload) == fallbackFilePayload.size(),
                    "fallback transfer file should be written") && ok;
        fallbackFile.close();
    }
    bobFileNames.clear();
    bobFilePayloads.clear();
    ok = expect(alice.sendFile(fallbackFilePath, "960002"),
                "alice should upload a small file even when Redis publish fails") && ok;
    ok = expect(!waitFor([&] {
        return bobFileNames.contains(fallbackFileName);
    }, 500), "bob should not receive the file immediately when Redis publish fails") && ok;
    QMetaObject::invokeMethod(fakeRedis, "setPublishFailure", Qt::BlockingQueuedConnection, Q_ARG(bool, false));

    bob.disconnectFromServer();
    ok = expect(waitFor([&] { return !bob.isConnected(); }),
                "bob should disconnect from the second server before replay check") && ok;
    bobPrivateMessages.clear();
    bobFileNames.clear();
    bobFilePayloads.clear();
    bob.setAccountInfo("960002", "secret", false);
    ok = expect(bob.connectToServer("127.0.0.1", serverAPort),
                "bob should reconnect to the first server with the existing account") && ok;
    ok = expect(bob.waitForLoginResult(5000),
                "bob should log in on the first server for offline replay check") && ok;
    ok = expect(waitFor([&] {
        return bobPrivateMessages.contains(fallbackPrivateMessage)
            && bobFileNames.contains(largeFileName)
            && bobFilePayloads.contains(largeFilePayload)
            && bobFileNames.contains(encodedOverflowFileName)
            && bobFilePayloads.contains(encodedOverflowPayload)
            && bobFileNames.contains(fallbackFileName)
            && bobFilePayloads.contains(fallbackFilePayload);
    }, 9000), "large files and publish failures should fall back to the origin server offline queue") && ok;
    ok = expect(!waitFor([&] {
        return bobPrivateMessages.contains(privateMessage) || bobFileNames.contains(fileName);
    }, 800), "cross-instance private messages and files should not be replayed from the origin server offline queue") && ok;

    alice.disconnectFromServer();
    bob.disconnectFromServer();
    serverA.stop();
    serverB.stop();

    qunsetenv("QTNETWORKCHAT_REDIS");
    qunsetenv("QTNETWORKCHAT_REDIS_HOST");
    qunsetenv("QTNETWORKCHAT_REDIS_PORT");
    qunsetenv("QTNETWORKCHAT_REDIS_PREFIX");
    qunsetenv("QTNETWORKCHAT_REDIS_PASSWORD");
    qunsetenv("QTNETWORKCHAT_LARGE_FILE_ROUTING");
    qunsetenv("QTNETWORKCHAT_OBJECT_STORE");
    qunsetenv("QTNETWORKCHAT_OBJECT_ROOT");
    qunsetenv("QTNETWORKCHAT_OBJECT_TTL_HOURS");

    QMetaObject::invokeMethod(fakeRedis, "stop", Qt::BlockingQueuedConnection);
    redisThread.quit();
    redisThread.wait();

    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}

#include "redis_cross_instance_chat_test.moc"
