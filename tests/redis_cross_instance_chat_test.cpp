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
#include <QBuffer>
#include <QCryptographicHash>
#include <QSet>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QTemporaryDir>

#include <cstdio>
#include <functional>
#include <memory>
#include <utility>

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

class SharedMemoryObjectStore final : public ObjectStore {
public:
    explicit SharedMemoryObjectStore(QMap<QString, QByteArray>* objects,
                                     QString validationFailureReason = QString(),
                                     QString openFailureReason = QString(),
                                     QString removeFailureReason = QString())
        : m_objects(objects)
        , m_validationFailureReason(std::move(validationFailureReason))
        , m_openFailureReason(std::move(openFailureReason))
        , m_removeFailureReason(std::move(removeFailureReason)) {
    }

    bool writeObject(const QByteArray& data,
                     QString* objectKey,
                     QString* fileHash = nullptr,
                     QString* error = nullptr,
                     const QString& extension = QString()) const override {
        if (objectKey) objectKey->clear();
        if (fileHash) fileHash->clear();
        if (error) error->clear();
        if (!m_objects) {
            if (error) *error = QStringLiteral("object-store-unavailable");
            return false;
        }
        const QString key = FilesystemObjectStore::generateObjectKey(extension);
        (*m_objects)[key] = data;
        if (objectKey) *objectKey = key;
        if (fileHash) {
            *fileHash = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
        }
        return true;
    }

    ValidationResult validateObject(const QString& objectKey,
                                    qint64 expectedSize,
                                    const QString& expectedHash) const override {
        ValidationResult result;
        if (!m_validationFailureReason.isEmpty()) {
            result.error = m_validationFailureReason;
            return result;
        }
        if (!m_objects || !FilesystemObjectStore::isValidObjectKey(objectKey) || !m_objects->contains(objectKey)) {
            result.error = QStringLiteral("not_found");
            return result;
        }
        const QByteArray data = m_objects->value(objectKey);
        result.size = data.size();
        result.fileHash = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
        if (result.size != expectedSize) {
            result.error = QString::fromUtf8("大小不匹配");
            return result;
        }
        if (result.fileHash.compare(expectedHash, Qt::CaseInsensitive) != 0) {
            result.error = QStringLiteral("SHA-256 mismatch");
            return result;
        }
        result.ok = true;
        return result;
    }

    std::unique_ptr<QIODevice> openObject(const QString& objectKey) const override {
        m_lastOpenFailureReason.clear();
        if (!m_openFailureReason.isEmpty()) {
            m_lastOpenFailureReason = m_openFailureReason;
            return {};
        }
        if (!m_objects || !m_objects->contains(objectKey)) {
            m_lastOpenFailureReason = QStringLiteral("not_found");
            return {};
        }
        auto buffer = std::make_unique<QBuffer>();
        buffer->setData(m_objects->value(objectKey));
        if (!buffer->open(QIODevice::ReadOnly)) {
            m_lastOpenFailureReason = QStringLiteral("unknown");
            return {};
        }
        return buffer;
    }

    QString lastOpenFailureReason() const override {
        return m_lastOpenFailureReason;
    }

    bool removeObject(const QString& objectKey) const override {
        m_lastRemoveFailureReason.clear();
        if (!m_removeFailureReason.isEmpty()) {
            m_lastRemoveFailureReason = m_removeFailureReason;
            return false;
        }
        return m_objects && m_objects->remove(objectKey) > 0;
    }

    QString lastRemoveFailureReason() const override {
        return m_lastRemoveFailureReason;
    }

    int cleanupExpired(qint64 ttlMs, QStringList* removedKeys = nullptr) const override {
        Q_UNUSED(ttlMs);
        if (removedKeys) removedKeys->clear();
        return 0;
    }

private:
    QMap<QString, QByteArray>* m_objects = nullptr;
    QString m_validationFailureReason;
    QString m_openFailureReason;
    QString m_removeFailureReason;
    mutable QString m_lastOpenFailureReason;
    mutable QString m_lastRemoveFailureReason;
};
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

QStringList* gCapturedRouteLogs = nullptr;
QtMessageHandler gPreviousMessageHandler = nullptr;

void captureRouteLogMessage(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (gCapturedRouteLogs && message.contains(QStringLiteral("redis_large_file_route"))) {
        gCapturedRouteLogs->append(message);
    }
    if (gPreviousMessageHandler) {
        gPreviousMessageHandler(type, context, message);
    } else {
        std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("redis_cross_instance_chat_test");
    QStandardPaths::setTestModeEnabled(true);
    QStringList routeLogs;
    gCapturedRouteLogs = &routeLogs;
    gPreviousMessageHandler = qInstallMessageHandler(captureRouteLogMessage);
    const auto routeLogContains = [&routeLogs](const QStringList& needles) {
        for (const QString& line : routeLogs) {
            bool matched = true;
            for (const QString& needle : needles) {
                if (!line.contains(needle)) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                return true;
            }
        }
        return false;
    };

    const QString appDataDir = testAppDataDir();
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
    QTemporaryDir receiptSummaryDir;
    ok = expect(receiptSummaryDir.isValid(), "temporary delivered receipt summary dir should be available") && ok;
    qputenv("QTNETWORKCHAT_DELIVERED_RECEIPT_DIR", receiptSummaryDir.path().toUtf8());
    QMap<QString, QByteArray> injectedS3Objects;
    QString injectedS3ValidationFailureReason;
    QString injectedS3OpenFailureReason;
    QString injectedS3RemoveFailureReason;
    auto injectedS3Factory = [&objectRoot,
                              &injectedS3Objects,
                              &injectedS3ValidationFailureReason,
                              &injectedS3OpenFailureReason,
                              &injectedS3RemoveFailureReason](QString* error) -> std::unique_ptr<ObjectStore> {
        if (error) error->clear();
        if (normalizeObjectStoreType(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_STORE"))) == QStringLiteral("filesystem")) {
            return std::make_unique<FilesystemObjectStore>(objectRoot.path());
        }
        return std::make_unique<SharedMemoryObjectStore>(&injectedS3Objects,
                                                         injectedS3ValidationFailureReason,
                                                         injectedS3OpenFailureReason,
                                                         injectedS3RemoveFailureReason);
    };

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
    serverA.setObjectStoreFactoryForTesting(injectedS3Factory);
    serverB.setObjectStoreFactoryForTesting(injectedS3Factory);
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
    ok = expect(largeFileOffer["storeType"].toString() == QStringLiteral("filesystem"),
                "large file offer should carry the object store type") && ok;
    ok = expect(largeFileOffer["fileSize"].toVariant().toLongLong() == largeFilePayload.size(),
                "large file offer should carry the original file size") && ok;
    ok = expect(!largeFileOffer["transferId"].toString().trimmed().isEmpty(),
                "large file offer should carry a transfer id") && ok;
    const FilesystemObjectStore objectStore(objectRoot.path());
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_claim", QString(), largeFileObjectKey).isEmpty();
    }), "remote server should claim a large file offer for its local online receiver") && ok;
    ok = expect(waitFor([&] {
        return bobFileNames.contains(largeFileName) && bobFilePayloads.contains(largeFilePayload);
    }, 9000), "bob should receive the large file through object-store offer routing") && ok;
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_delivered", QString(), largeFileObjectKey).isEmpty();
    }), "remote server should publish delivered after the large file is fully acknowledged") && ok;
    ok = expect(waitFor([&] {
        return routeLogContains({QStringLiteral("event=delivered_reconcile"),
                                 QStringLiteral("result=cleaned"),
                                 QStringLiteral("reason=cleaned"),
                                 QStringLiteral("operation=reconcile"),
                                 QStringLiteral("objectKey=") + largeFileObjectKey,
                                 QStringLiteral("fileHash=") + largeFileOffer["fileHash"].toString()});
    }), "source server should emit a read-only cleaned delivered_reconcile route log") && ok;

    QTcpSocket rejectReceiver;
    QByteArray rejectReceiverBuffer;
    bool rejectReceiverLoggedIn = false;
    int rejectReceiverChunkCount = 0;
    auto drainRejectReceiver = [&]() {
        rejectReceiverBuffer.append(rejectReceiver.readAll());
        while (rejectReceiverBuffer.contains('\n')) {
            const int newlineIndex = rejectReceiverBuffer.indexOf('\n');
            const QByteArray line = rejectReceiverBuffer.left(newlineIndex);
            rejectReceiverBuffer = rejectReceiverBuffer.mid(newlineIndex + 1);
            if (line.trimmed().isEmpty()) continue;

            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) continue;
            const QJsonObject obj = doc.object();
            const QString type = obj["type"].toString();
            if (type == "login_success") {
                rejectReceiverLoggedIn = true;
            } else if (type == "file_chunk") {
                ++rejectReceiverChunkCount;
                QJsonObject ack;
                ack["type"] = "file_chunk_ack";
                ack["transferId"] = obj["transferId"].toString();
                ack["chunkIndex"] = obj["chunkIndex"].toString();
                ack["accepted"] = false;
                ack["reason"] = "scripted receiver rejection with internal detail";
                ack["receivedBytes"] = QStringLiteral("0");
                rejectReceiver.write(QJsonDocument(ack).toJson(QJsonDocument::Compact));
                rejectReceiver.write("\n");
                rejectReceiver.flush();
            }
        }
    };
    QObject::connect(&rejectReceiver, &QTcpSocket::readyRead, &app, drainRejectReceiver);
    rejectReceiver.connectToHost("127.0.0.1", serverBPort);
    ok = expect(rejectReceiver.waitForConnected(5000),
                "rejecting raw receiver should connect to the second server") && ok;
    QJsonObject rejectLogin;
    rejectLogin["type"] = "login";
    rejectLogin["mode"] = "register";
    rejectLogin["account"] = "960011";
    rejectLogin["password"] = "secret";
    rejectLogin["userName"] = "RejectRedisReceiver";
    rejectReceiver.write(QJsonDocument(rejectLogin).toJson(QJsonDocument::Compact));
    rejectReceiver.write("\n");
    rejectReceiver.flush();
    ok = expect(waitFor([&] {
        drainRejectReceiver();
        return rejectReceiverLoggedIn;
    }), "rejecting raw receiver should log in") && ok;

    const QString rejectedDeliveryFileName = "redis-large-delivery-rejected.bin";
    const QString rejectedDeliveryFilePath = transferDir.filePath(rejectedDeliveryFileName);
    const QByteArray rejectedDeliveryPayload = makePatternPayload(1024 * 1024 + 2048);
    QFile rejectedDeliveryFile(rejectedDeliveryFilePath);
    ok = expect(rejectedDeliveryFile.open(QIODevice::WriteOnly),
                "rejected delivery file should open for writing") && ok;
    if (rejectedDeliveryFile.isOpen()) {
        ok = expect(rejectedDeliveryFile.write(rejectedDeliveryPayload) == rejectedDeliveryPayload.size(),
                    "rejected delivery file should be written") && ok;
        rejectedDeliveryFile.close();
    }
    ok = expect(alice.sendFile(rejectedDeliveryFilePath, "960011"),
                "alice should upload a large file for a rejecting remote receiver") && ok;
    ok = expect(waitFor([&] {
        return !findLargeFileOffer(rejectedDeliveryFileName).isEmpty();
    }), "rejected large files should publish an object-store offer") && ok;
    const QJsonObject rejectedDeliveryOffer = findLargeFileOffer(rejectedDeliveryFileName);
    const QString rejectedDeliveryObjectKey = rejectedDeliveryOffer["objectKey"].toString();
    ok = expect(waitFor([&] {
        drainRejectReceiver();
        return rejectReceiverChunkCount > 0
            && !findPublishedEvent("large_file_failed", QString(), rejectedDeliveryObjectKey).isEmpty();
    }), "remote server should publish failed when the receiver rejects object-store delivery") && ok;
    ok = expect(findPublishedEvent("large_file_failed", QString(), rejectedDeliveryObjectKey)["reason"].toString()
                    .startsWith(QStringLiteral("chunk-rejected:")),
                "large_file_failed may keep the receiver-facing rejection detail") && ok;
    ok = expect(waitFor([&] {
        return routeLogContains({QStringLiteral("event=offer_delivery"),
                                 QStringLiteral("result=failed"),
                                 QStringLiteral("reason=chunk-rejected"),
                                 QStringLiteral("operation=deliver"),
                                 QStringLiteral("objectKey=") + rejectedDeliveryObjectKey,
                                 QStringLiteral("fileHash=") + rejectedDeliveryOffer["fileHash"].toString()});
    }), "remote server should emit a fixed-reason offer_delivery route log for rejected delivery") && ok;
    ok = expect(!routeLogContains({QStringLiteral("event=offer_delivery"),
                                   QStringLiteral("scripted receiver rejection")}),
                "offer_delivery route log should not include raw receiver rejection detail") && ok;
    rejectReceiver.disconnectFromHost();

    const QString receiptSummaryPath = receiptSummaryDir.filePath(QStringLiteral("delivered-receipts.jsonl"));
    const auto receiptSummaryContains = [&receiptSummaryPath](const QString& objectKey,
                                                              const QString& result,
                                                              const QString& reason) {
        QFile file(receiptSummaryPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return false;
        }
        while (!file.atEnd()) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readLine().trimmed());
            if (!doc.isObject()) continue;
            const QJsonObject obj = doc.object();
            if (obj["objectKey"].toString() == objectKey
                && obj["result"].toString() == result
                && obj["reason"].toString() == reason) {
                return obj["sourceInstanceId"].toString().trimmed().isEmpty() == false
                    && obj["transferId"].toString().trimmed().isEmpty() == false
                    && obj["receiverId"].toString().trimmed().isEmpty() == false
                    && obj["fileHash"].toString().size() == 64
                    && obj["confirmedBytes"].toString().toLongLong() > 0
                    && obj["cleanupResult"].toString().trimmed().isEmpty() == false;
            }
        }
        return false;
    };
    ok = expect(waitFor([&] {
        return receiptSummaryContains(largeFileObjectKey,
                                      QStringLiteral("cleaned"),
                                      QStringLiteral("cleaned"));
    }), "source server should persist a safe cleaned delivered receipt summary") && ok;
    ok = expect(waitFor([&] {
        return !QFileInfo::exists(objectStore.objectPath(largeFileObjectKey));
    }), "source server should remove the delivered large file object") && ok;

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
    const QString encodedOverflowObjectKey = encodedOverflowOffer["objectKey"].toString();
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_claim", QString(), encodedOverflowObjectKey).isEmpty();
    }), "remote server should claim an encoded-overflow offer for its local online receiver") && ok;
    ok = expect(waitFor([&] {
        return bobFileNames.contains(encodedOverflowFileName) && bobFilePayloads.contains(encodedOverflowPayload);
    }, 9000), "bob should receive the encoded-overflow file through object-store offer routing") && ok;
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_delivered", QString(), encodedOverflowObjectKey).isEmpty();
    }), "remote server should publish delivered after the encoded-overflow file is fully acknowledged") && ok;
    ok = expect(waitFor([&] {
        return !QFileInfo::exists(objectStore.objectPath(encodedOverflowObjectKey));
    }), "source server should remove the delivered encoded-overflow object") && ok;

    const QString invalidOfferFileName = "redis-invalid-large-offer.bin";
    const QString invalidOfferObjectKey = "missing-redis-invalid-large-offer.bin";
    QJsonObject invalidOffer;
    invalidOffer["eventType"] = "large_file_offer";
    invalidOffer["instanceId"] = "external-invalid-source";
    invalidOffer["transferId"] = "external-invalid-transfer";
    invalidOffer["objectKey"] = invalidOfferObjectKey;
    invalidOffer["senderId"] = "external-sender";
    invalidOffer["senderName"] = "ExternalSender";
    invalidOffer["receiverId"] = "960002";
    invalidOffer["messageType"] = "File";
    invalidOffer["fileName"] = invalidOfferFileName;
    invalidOffer["fileSize"] = QString::number(1024);
    invalidOffer["fileHash"] = QString(64, QLatin1Char('a'));
    invalidOffer["chunkSize"] = QString::number(256 * 1024);
    invalidOffer["chunkCount"] = QString::number(1);
    invalidOffer["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    bobFileNames.clear();
    bobFilePayloads.clear();
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, QJsonDocument(invalidOffer).toJson(QJsonDocument::Compact)));
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_failed", QString(), invalidOfferObjectKey).isEmpty();
    }), "remote server should publish failed when a large file offer object is missing") && ok;
    ok = expect(findPublishedEvent("large_file_failed", QString(), invalidOfferObjectKey)["reason"].toString()
                    == QStringLiteral("validation_error"),
                "remote failed event should publish aggregate-safe object validation reason") && ok;
    ok = expect(!waitFor([&] {
        return !findPublishedEvent("large_file_claim", QString(), invalidOfferObjectKey).isEmpty()
            || bobFileNames.contains(invalidOfferFileName);
    }, 800), "invalid large file offers should not be claimed or delivered") && ok;

    const QString unsafeObjectKey = "../redis-unsafe-large-offer.bin";
    QJsonObject unsafeKeyOffer = invalidOffer;
    unsafeKeyOffer["transferId"] = "external-unsafe-key-transfer";
    unsafeKeyOffer["objectKey"] = unsafeObjectKey;
    unsafeKeyOffer["fileName"] = "redis-unsafe-key-large-offer.bin";
    bobFileNames.clear();
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, QJsonDocument(unsafeKeyOffer).toJson(QJsonDocument::Compact)));
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_failed", QString(), unsafeObjectKey).isEmpty();
    }), "remote server should publish failed for an unsafe large file object key") && ok;
    ok = expect(findPublishedEvent("large_file_failed", QString(), unsafeObjectKey)["reason"].toString()
                    == QStringLiteral("invalid-offer-metadata"),
                "unsafe object key failure should keep a fixed metadata reason") && ok;
    ok = expect(!waitFor([&] {
        return !findPublishedEvent("large_file_claim", QString(), unsafeObjectKey).isEmpty()
            || bobFileNames.contains(unsafeKeyOffer["fileName"].toString());
    }, 800), "unsafe object keys should not be claimed or delivered") && ok;

    const QString invalidChunkObjectKey = "invalid-chunk-metadata-offer.bin";
    QJsonObject invalidChunkOffer = invalidOffer;
    invalidChunkOffer["transferId"] = "external-invalid-chunk-transfer";
    invalidChunkOffer["objectKey"] = invalidChunkObjectKey;
    invalidChunkOffer["fileName"] = "redis-invalid-chunk-large-offer.bin";
    invalidChunkOffer["chunkSize"] = QString::number(0);
    invalidChunkOffer["chunkCount"] = QString::number(0);
    bobFileNames.clear();
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, QJsonDocument(invalidChunkOffer).toJson(QJsonDocument::Compact)));
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_failed", QString(), invalidChunkObjectKey).isEmpty();
    }), "remote server should publish failed for invalid large file chunk metadata") && ok;
    ok = expect(!waitFor([&] {
        return !findPublishedEvent("large_file_claim", QString(), invalidChunkObjectKey).isEmpty()
            || bobFileNames.contains(invalidChunkOffer["fileName"].toString());
    }, 800), "invalid chunk metadata should not be claimed or delivered") && ok;

    const QString mismatchedStoreObjectKey = "mismatched-store-offer.bin";
    QJsonObject mismatchedStoreOffer = invalidOffer;
    mismatchedStoreOffer["transferId"] = "external-mismatched-store-transfer";
    mismatchedStoreOffer["objectKey"] = mismatchedStoreObjectKey;
    mismatchedStoreOffer["fileName"] = "redis-mismatched-store-large-offer.bin";
    mismatchedStoreOffer["storeType"] = "s3";
    bobFileNames.clear();
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, QJsonDocument(mismatchedStoreOffer).toJson(QJsonDocument::Compact)));
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_failed", QString(), mismatchedStoreObjectKey).isEmpty();
    }), "remote server should publish failed when offer storeType does not match local object store") && ok;
    ok = expect(findPublishedEvent("large_file_failed", QString(), mismatchedStoreObjectKey)["reason"].toString()
                    == QStringLiteral("object-store-type-mismatch"),
                "store type mismatch should publish a fixed aggregate-safe reason") && ok;
    ok = expect(!waitFor([&] {
        return !findPublishedEvent("large_file_claim", QString(), mismatchedStoreObjectKey).isEmpty()
            || bobFileNames.contains(mismatchedStoreOffer["fileName"].toString());
    }, 800), "mismatched store type offers should not be claimed or delivered") && ok;

    const QString unsupportedOfferStoreObjectKey = "unsupported-offer-store.bin";
    QJsonObject unsupportedOfferStore = invalidOffer;
    unsupportedOfferStore["transferId"] = "external-unsupported-offer-store-transfer";
    unsupportedOfferStore["objectKey"] = unsupportedOfferStoreObjectKey;
    unsupportedOfferStore["fileName"] = "redis-unsupported-offer-store-large-offer.bin";
    unsupportedOfferStore["storeType"] = "memory";
    bobFileNames.clear();
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, QJsonDocument(unsupportedOfferStore).toJson(QJsonDocument::Compact)));
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_failed", QString(), unsupportedOfferStoreObjectKey).isEmpty();
    }), "remote server should publish failed for unsupported offer storeType") && ok;
    ok = expect(findPublishedEvent("large_file_failed", QString(), unsupportedOfferStoreObjectKey)["reason"].toString()
                    == QStringLiteral("unsupported-offer-store-type"),
                "unsupported offer store type should publish a fixed aggregate-safe reason") && ok;
    ok = expect(!waitFor([&] {
        return !findPublishedEvent("large_file_claim", QString(), unsupportedOfferStoreObjectKey).isEmpty()
            || bobFileNames.contains(unsupportedOfferStore["fileName"].toString());
    }, 800), "unsupported offer store type should not be claimed or delivered") && ok;

    const QString unsupportedStoreObjectKey = "unsupported-store-offer.bin";
    QJsonObject unsupportedStoreOffer = invalidOffer;
    unsupportedStoreOffer["transferId"] = "external-unsupported-store-transfer";
    unsupportedStoreOffer["objectKey"] = unsupportedStoreObjectKey;
    unsupportedStoreOffer["fileName"] = "redis-unsupported-store-large-offer.bin";
    bobFileNames.clear();
    qputenv("QTNETWORKCHAT_OBJECT_STORE", "memory");
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, QJsonDocument(unsupportedStoreOffer).toJson(QJsonDocument::Compact)));
    ok = expect(!waitFor([&] {
        return !findPublishedEvent("large_file_claim", QString(), unsupportedStoreObjectKey).isEmpty()
            || !findPublishedEvent("large_file_delivered", QString(), unsupportedStoreObjectKey).isEmpty()
            || !findPublishedEvent("large_file_failed", QString(), unsupportedStoreObjectKey).isEmpty()
            || bobFileNames.contains(unsupportedStoreOffer["fileName"].toString());
    }, 800), "non-filesystem object stores should not consume large file offers") && ok;
    qputenv("QTNETWORKCHAT_OBJECT_STORE", "filesystem");

    qputenv("QTNETWORKCHAT_OBJECT_STORE", "s3");
    injectedS3Objects.clear();
    injectedS3ValidationFailureReason.clear();
    injectedS3OpenFailureReason.clear();
    injectedS3RemoveFailureReason.clear();
    const QString s3ValidationFailureFileName = "redis-s3-validation-failure.bin";
    const QString s3ValidationFailurePath = transferDir.filePath(s3ValidationFailureFileName);
    const QByteArray s3ValidationFailurePayload = makePatternPayload(1024 * 1024 + 12288);
    QFile s3ValidationFailureFile(s3ValidationFailurePath);
    ok = expect(s3ValidationFailureFile.open(QIODevice::WriteOnly),
                "injected S3 validation transfer file should open for writing") && ok;
    if (s3ValidationFailureFile.isOpen()) {
        ok = expect(s3ValidationFailureFile.write(s3ValidationFailurePayload) == s3ValidationFailurePayload.size(),
                    "injected S3 validation transfer file should be written") && ok;
        s3ValidationFailureFile.close();
    }
    injectedS3ValidationFailureReason = QStringLiteral("tls_error from injected S3 HEAD");
    bobFileNames.clear();
    bobFilePayloads.clear();
    ok = expect(alice.sendFile(s3ValidationFailurePath, "960002"),
                "alice should publish an injected S3 large file offer") && ok;
    ok = expect(waitFor([&] {
        return !findLargeFileOffer(s3ValidationFailureFileName).isEmpty();
    }), "injected S3 large file should publish a storeType=s3 offer") && ok;
    const QJsonObject s3ValidationFailureOffer = findLargeFileOffer(s3ValidationFailureFileName);
    const QString s3ValidationFailureObjectKey = s3ValidationFailureOffer["objectKey"].toString();
    ok = expect(s3ValidationFailureOffer["storeType"].toString() == QStringLiteral("s3"),
                "injected S3 offer should carry storeType=s3") && ok;
    ok = expect(FilesystemObjectStore::isValidObjectKey(s3ValidationFailureObjectKey),
                "injected S3 offer should use a safe object key") && ok;
    ok = expect(injectedS3Objects.contains(s3ValidationFailureObjectKey),
                "injected S3 object store should retain the uploaded object for fallback") && ok;
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_failed", QString(), s3ValidationFailureObjectKey).isEmpty();
    }), "remote server should publish failed for injected S3 validation errors") && ok;
    ok = expect(findPublishedEvent("large_file_failed", QString(), s3ValidationFailureObjectKey)["reason"].toString()
                    == QStringLiteral("tls"),
                "injected S3 validation failure should publish a fixed TLS reason") && ok;
    ok = expect(waitFor([&] {
        return routeLogContains({QStringLiteral("event=offer_validation"),
                                 QStringLiteral("result=rejected"),
                                 QStringLiteral("reason=tls"),
                                 QStringLiteral("storeType=s3"),
                                 QStringLiteral("operation=validate"),
                                 QStringLiteral("objectKey=") + s3ValidationFailureObjectKey});
    }), "remote server should log injected S3 validation failure with safe fixed metadata") && ok;
    ok = expect(!routeLogContains({QStringLiteral("event=offer_validation"),
                                   QStringLiteral("injected S3 HEAD")}),
                "route log should not include injected S3 validation error detail") && ok;
    ok = expect(!waitFor([&] {
        return !findPublishedEvent("large_file_claim", QString(), s3ValidationFailureObjectKey).isEmpty()
            || bobFileNames.contains(s3ValidationFailureFileName);
    }, 800), "S3 validation failures should not be claimed or delivered") && ok;

    QJsonObject injectedS3FailedEvent;
    injectedS3FailedEvent["eventType"] = "large_file_failed";
    injectedS3FailedEvent["instanceId"] = "external-injected-s3-failure";
    injectedS3FailedEvent["sourceInstanceId"] = s3ValidationFailureOffer["instanceId"].toString();
    injectedS3FailedEvent["transferId"] = s3ValidationFailureOffer["transferId"].toString();
    injectedS3FailedEvent["objectKey"] = s3ValidationFailureObjectKey;
    injectedS3FailedEvent["receiverId"] = "960002";
    injectedS3FailedEvent["fileHash"] = s3ValidationFailureOffer["fileHash"].toString();
    injectedS3FailedEvent["reason"] = "tls";
    injectedS3FailedEvent["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, QJsonDocument(injectedS3FailedEvent).toJson(QJsonDocument::Compact)));
    ok = expect(waitFor([&] {
        return routeLogContains({QStringLiteral("event=failed_received"),
                                 QStringLiteral("result=fallback-retained"),
                                 QStringLiteral("reason=tls"),
                                 QStringLiteral("storeType=s3"),
                                 QStringLiteral("operation=fallback"),
                                 QStringLiteral("objectKey=") + s3ValidationFailureObjectKey});
    }), "source server should log S3 failed receipt while retaining fallback") && ok;
    ok = expect(injectedS3Objects.contains(s3ValidationFailureObjectKey),
                "source injected S3 fallback object should remain after remote failure") && ok;
    injectedS3ValidationFailureReason.clear();

    injectedS3Objects.clear();
    injectedS3OpenFailureReason = QStringLiteral("network");
    const QString s3OpenFailureFileName = "redis-s3-open-failure.bin";
    const QString s3OpenFailurePath = transferDir.filePath(s3OpenFailureFileName);
    const QByteArray s3OpenFailurePayload = makePatternPayload(1024 * 1024 + 16384);
    QFile s3OpenFailureFile(s3OpenFailurePath);
    ok = expect(s3OpenFailureFile.open(QIODevice::WriteOnly),
                "injected S3 open failure transfer file should open for writing") && ok;
    if (s3OpenFailureFile.isOpen()) {
        ok = expect(s3OpenFailureFile.write(s3OpenFailurePayload) == s3OpenFailurePayload.size(),
                    "injected S3 open failure transfer file should be written") && ok;
        s3OpenFailureFile.close();
    }
    bobFileNames.clear();
    bobFilePayloads.clear();
    ok = expect(alice.sendFile(s3OpenFailurePath, "960002"),
                "alice should publish an injected S3 open failure offer") && ok;
    ok = expect(waitFor([&] {
        return !findLargeFileOffer(s3OpenFailureFileName).isEmpty();
    }), "injected S3 open failure file should publish an offer") && ok;
    const QJsonObject s3OpenFailureOffer = findLargeFileOffer(s3OpenFailureFileName);
    const QString s3OpenFailureObjectKey = s3OpenFailureOffer["objectKey"].toString();
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_failed", QString(), s3OpenFailureObjectKey).isEmpty();
    }), "remote server should publish failed for injected S3 open errors") && ok;
    ok = expect(findPublishedEvent("large_file_failed", QString(), s3OpenFailureObjectKey)["reason"].toString()
                    == QStringLiteral("network"),
                "injected S3 open failure should publish a fixed network reason") && ok;
    ok = expect(waitFor([&] {
        return routeLogContains({QStringLiteral("event=offer_read"),
                                 QStringLiteral("result=rejected"),
                                 QStringLiteral("reason=network"),
                                 QStringLiteral("storeType=s3"),
                                 QStringLiteral("operation=read"),
                                 QStringLiteral("objectKey=") + s3OpenFailureObjectKey});
    }), "remote server should log injected S3 open failure with safe fixed metadata") && ok;
    ok = expect(!waitFor([&] {
        return !findPublishedEvent("large_file_claim", QString(), s3OpenFailureObjectKey).isEmpty()
            || bobFileNames.contains(s3OpenFailureFileName);
    }, 800), "S3 open failures should not be claimed or delivered") && ok;
    injectedS3OpenFailureReason.clear();

    injectedS3Objects.clear();
    injectedS3RemoveFailureReason = QStringLiteral("server");
    const QString s3DeleteFailureFileName = "redis-s3-delete-failure.bin";
    const QString s3DeleteFailurePath = transferDir.filePath(s3DeleteFailureFileName);
    const QByteArray s3DeleteFailurePayload = makePatternPayload(1024 * 1024 + 24576);
    QFile s3DeleteFailureFile(s3DeleteFailurePath);
    ok = expect(s3DeleteFailureFile.open(QIODevice::WriteOnly),
                "injected S3 delete failure transfer file should open for writing") && ok;
    if (s3DeleteFailureFile.isOpen()) {
        ok = expect(s3DeleteFailureFile.write(s3DeleteFailurePayload) == s3DeleteFailurePayload.size(),
                    "injected S3 delete failure transfer file should be written") && ok;
        s3DeleteFailureFile.close();
    }
    bobFileNames.clear();
    bobFilePayloads.clear();
    ok = expect(alice.sendFile(s3DeleteFailurePath, "960002"),
                "alice should publish an injected S3 delete failure offer") && ok;
    ok = expect(waitFor([&] {
        return !findLargeFileOffer(s3DeleteFailureFileName).isEmpty();
    }), "injected S3 delete failure file should publish an offer") && ok;
    const QJsonObject s3DeleteFailureOffer = findLargeFileOffer(s3DeleteFailureFileName);
    const QString s3DeleteFailureObjectKey = s3DeleteFailureOffer["objectKey"].toString();
    ok = expect(waitFor([&] {
        return bobFileNames.contains(s3DeleteFailureFileName) && bobFilePayloads.contains(s3DeleteFailurePayload);
    }, 9000), "bob should receive the injected S3 delete failure transfer before cleanup") && ok;
    ok = expect(waitFor([&] {
        return !findPublishedEvent("large_file_delivered", QString(), s3DeleteFailureObjectKey).isEmpty();
    }), "remote server should publish delivered before injected S3 delete failure") && ok;
    ok = expect(waitFor([&] {
        return routeLogContains({QStringLiteral("event=object_delete"),
                                 QStringLiteral("result=retained"),
                                 QStringLiteral("reason=server"),
                                 QStringLiteral("storeType=s3"),
                                 QStringLiteral("operation=delete"),
                                 QStringLiteral("objectKey=") + s3DeleteFailureObjectKey});
    }), "source server should log injected S3 delete failure with a fixed server reason") && ok;
    ok = expect(injectedS3Objects.contains(s3DeleteFailureObjectKey),
                "injected S3 object should remain when delete fails") && ok;
    injectedS3RemoveFailureReason.clear();
    qputenv("QTNETWORKCHAT_OBJECT_STORE", "filesystem");

    const QString failedFallbackReceiverId = "960010";
    RedisClient presenceSeeder;
    presenceSeeder.configureFromEnvironment();
    ok = expect(presenceSeeder.connectToServer(1000),
                "presence seeder should connect to fake Redis") && ok;
    ok = expect(presenceSeeder.setPresence(failedFallbackReceiverId, "RemoteFailedFallback", 90, 1000),
                "presence seeder should mark a remote-only receiver online") && ok;

    const QString failedFallbackFileName = "redis-large-failed-keeps-fallback.bin";
    const QString failedFallbackFilePath = transferDir.filePath(failedFallbackFileName);
    const QByteArray failedFallbackPayload = makePatternPayload(1024 * 1024 + 8192);
    QFile failedFallbackFile(failedFallbackFilePath);
    ok = expect(failedFallbackFile.open(QIODevice::WriteOnly),
                "failed-fallback transfer file should open for writing") && ok;
    if (failedFallbackFile.isOpen()) {
        ok = expect(failedFallbackFile.write(failedFallbackPayload) == failedFallbackPayload.size(),
                    "failed-fallback transfer file should be written") && ok;
        failedFallbackFile.close();
    }
    ok = expect(alice.sendFile(failedFallbackFilePath, failedFallbackReceiverId),
                "alice should upload a large file for a remote-only receiver") && ok;
    ok = expect(waitFor([&] {
        return !findLargeFileOffer(failedFallbackFileName).isEmpty();
    }), "remote-only large files should publish an object-store offer") && ok;
    const QJsonObject failedFallbackOffer = findLargeFileOffer(failedFallbackFileName);
    const QString failedFallbackObjectKey = failedFallbackOffer["objectKey"].toString();
    ok = expect(QFileInfo::exists(objectStore.objectPath(failedFallbackObjectKey)),
                "failed-fallback object should exist before remote failure") && ok;
    ok = expect(!waitFor([&] {
        return !findPublishedEvent("large_file_claim", QString(), failedFallbackObjectKey).isEmpty()
            || !findPublishedEvent("large_file_delivered", QString(), failedFallbackObjectKey).isEmpty();
    }, 800), "offers without a local receiver should not be claimed or delivered") && ok;

    QJsonObject partialDeliveredEvent;
    partialDeliveredEvent["eventType"] = "large_file_delivered";
    partialDeliveredEvent["instanceId"] = "external-partial-delivered-instance";
    partialDeliveredEvent["sourceInstanceId"] = failedFallbackOffer["instanceId"].toString();
    partialDeliveredEvent["transferId"] = failedFallbackOffer["transferId"].toString();
    partialDeliveredEvent["objectKey"] = failedFallbackObjectKey;
    partialDeliveredEvent["receiverId"] = failedFallbackReceiverId;
    partialDeliveredEvent["fileHash"] = failedFallbackOffer["fileHash"].toString();
    partialDeliveredEvent["confirmedBytes"] = QString::number(failedFallbackPayload.size() - 1);
    partialDeliveredEvent["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, QJsonDocument(partialDeliveredEvent).toJson(QJsonDocument::Compact)));
    ok = expect(waitFor([&] {
        return QFileInfo::exists(objectStore.objectPath(failedFallbackObjectKey));
    }, 800), "source server should retain fallback after a partial delivered event") && ok;
    ok = expect(waitFor([&] {
        return routeLogContains({QStringLiteral("event=delivered_reconcile"),
                                 QStringLiteral("result=retained"),
                                 QStringLiteral("reason=confirmed-bytes-insufficient"),
                                 QStringLiteral("operation=reconcile"),
                                 QStringLiteral("objectKey=") + failedFallbackObjectKey,
                                 QStringLiteral("fileHash=") + failedFallbackOffer["fileHash"].toString()});
    }), "source server should emit a retained delivered_reconcile route log for partial receipts") && ok;
    ok = expect(waitFor([&] {
        return receiptSummaryContains(failedFallbackObjectKey,
                                      QStringLiteral("retained"),
                                      QStringLiteral("confirmed-bytes-insufficient"));
    }), "source server should persist a safe retained delivered receipt summary") && ok;

    QJsonObject failedEvent;
    failedEvent["eventType"] = "large_file_failed";
    failedEvent["instanceId"] = "external-failing-instance";
    failedEvent["sourceInstanceId"] = failedFallbackOffer["instanceId"].toString();
    failedEvent["transferId"] = failedFallbackOffer["transferId"].toString();
    failedEvent["objectKey"] = failedFallbackObjectKey;
    failedEvent["receiverId"] = failedFallbackReceiverId;
    failedEvent["fileHash"] = failedFallbackOffer["fileHash"].toString();
    failedEvent["reason"] = "injected-remote-failure";
    failedEvent["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QMetaObject::invokeMethod(fakeRedis,
                              "injectMessageEvent",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QByteArray, QJsonDocument(failedEvent).toJson(QJsonDocument::Compact)));
    ok = expect(waitFor([&] {
        return QFileInfo::exists(objectStore.objectPath(failedFallbackObjectKey));
    }, 800), "source server should keep the object after a remote failed event") && ok;

    QFile failedFallbackObjectFile(objectStore.objectPath(failedFallbackObjectKey));
    ok = expect(failedFallbackObjectFile.open(QIODevice::ReadWrite),
                "failed-fallback object should reopen before TTL cleanup") && ok;
    if (failedFallbackObjectFile.isOpen()) {
        ok = expect(failedFallbackObjectFile.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-2 * 60 * 60),
                                                         QFileDevice::FileModificationTime),
                    "failed-fallback object modification time should be aged before TTL cleanup") && ok;
        failedFallbackObjectFile.close();
    }
    qputenv("QTNETWORKCHAT_OBJECT_TTL_HOURS", "1");
    Server cleanupServer;
    const quint16 cleanupPort = freeLocalPort();
    ok = expect(cleanupPort != 0, "object cleanup server test port should be available") && ok;
    ok = expect(cleanupServer.start(cleanupPort),
                "object cleanup server should start and run startup cleanup") && ok;
    cleanupServer.stop();
    qputenv("QTNETWORKCHAT_OBJECT_TTL_HOURS", "24");
    ok = expect(!QFileInfo::exists(objectStore.objectPath(failedFallbackObjectKey)),
                "expired undelivered object should be removed by object-store TTL cleanup") && ok;

    Client failedFallbackReceiver;
    QStringList failedFallbackFileNames;
    QList<QByteArray> failedFallbackPayloads;
    QObject::connect(&failedFallbackReceiver, &Client::newMessage, &app, [&](const Message& msg) {
        if (msg.type == MessageType::File) {
            failedFallbackFileNames << msg.fileName;
            failedFallbackPayloads << msg.fileData;
        }
    });
    ok = expect(registerClient(failedFallbackReceiver,
                               failedFallbackReceiverId,
                               "FailedFallbackReceiver",
                               serverAPort),
                "failed-fallback receiver should log in to the source server") && ok;
    ok = expect(waitFor([&] {
        return failedFallbackFileNames.contains(failedFallbackFileName)
            && failedFallbackPayloads.contains(failedFallbackPayload);
    }, 9000), "source offline fallback should replay a failed cross-instance large file") && ok;
    failedFallbackReceiver.disconnectFromServer();

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
            && bobFileNames.contains(fallbackFileName)
            && bobFilePayloads.contains(fallbackFilePayload);
    }, 9000), "publish failures should fall back to the origin server offline queue") && ok;
    ok = expect(!waitFor([&] {
        return bobPrivateMessages.contains(privateMessage)
            || bobFileNames.contains(fileName)
            || bobFileNames.contains(largeFileName)
            || bobFileNames.contains(encodedOverflowFileName);
    }, 800), "cross-instance delivered files should not be replayed from the origin server offline queue") && ok;

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
    qunsetenv("QTNETWORKCHAT_DELIVERED_RECEIPT_DIR");

    QMetaObject::invokeMethod(fakeRedis, "stop", Qt::BlockingQueuedConnection);
    redisThread.quit();
    redisThread.wait();

    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    qInstallMessageHandler(gPreviousMessageHandler);
    gPreviousMessageHandler = nullptr;
    gCapturedRouteLogs = nullptr;
    return ok ? 0 : 1;
}

#include "redis_cross_instance_chat_test.moc"
