#include "client.h"
#include "server.h"
#include "redisclient.h"
#include "objectstore.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <cstdio>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "[real-redis] %s\n", message);
    return condition;
}
bool waitFor(const std::function<bool()>& predicate, int ms = 8000) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return predicate();
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (qgetenv("QTNETWORKCHAT_RUN_REAL_REDIS_TEST") != "1") {
        qInfo() << "SKIP: set QTNETWORKCHAT_RUN_REAL_REDIS_TEST=1 for actual Redis integration";
        return 77;
    }
    QTemporaryDir data, objects;
    if (!data.isValid() || !objects.isValid()) return 1;
    qputenv("QTNETWORKCHAT_APPDATA_DIR", data.path().toUtf8());
    qputenv("QTNETWORKCHAT_DB_DRIVER", "QSQLITE");
    qputenv("QTNETWORKCHAT_TRANSPORT", "tcp");
    qputenv("QTNETWORKCHAT_TLS", "0");
    qputenv("QTNETWORKCHAT_REDIS", "1");
    const auto prefix = "qnc-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    qputenv("QTNETWORKCHAT_REDIS_PREFIX", prefix.toUtf8());
    qputenv("QTNETWORKCHAT_OBJECT_STORE", "filesystem");
    qputenv("QTNETWORKCHAT_OBJECT_ROOT", objects.path().toUtf8());
    qputenv("QTNETWORKCHAT_LARGE_FILE_ROUTING", "1");
    qputenv("QTNETWORKCHAT_E2E_ALLOW_PLAINTEXT_PRIVATE_FILE", "1");
    qunsetenv("QTNETWORKCHAT_E2E_REQUIRE_PRODUCTION_CRYPTO");

    RedisClient redis;
    redis.configureFromEnvironment();
    if (!expect(redis.connectToServer(2000) && redis.ping(), "real Redis connection/PING required")) return 1;
    RedisSubscriber observer;
    observer.configureFromEnvironment();
    QList<QJsonObject> events;
    QObject::connect(&observer, &RedisSubscriber::messageReceived, &app, [&](const RedisClient::PubSubMessage& event) {
        events.append(QJsonDocument::fromJson(event.payload).object());
    });
    if (!expect(observer.subscribe("messages", 2000), "real Pub/Sub subscription required")) return 1;
    Server first, second;
    if (!expect(first.start(0) && second.start(0), "both actual application server instances should start")) return 1;
    Client alice, bob;
    QList<Message> received;
    QObject::connect(&bob, &Client::newMessage, &app, [&](const Message& message) { received.append(message); });
    auto login = [](Client& client, const QString& id, const QString& name, quint16 port, bool registration) {
        client.setUserInfo(id, name); client.setAccountInfo(id, "real-redis-test-only", registration);
        return client.connectToServer("127.0.0.1", port) && client.waitForLoginResult();
    };
    if (!expect(login(alice, "930101", "RealAlice", first.serverPort(), true)
        && login(bob, "930102", "RealBob", second.serverPort(), true), "register on two instances")) return 1;
    bool ok = expect(redis.hasPresence("930101") && redis.hasPresence("930102"), "real Redis presence keys");
    auto countMessage = [&](const QString& content) {
        int count = 0;
        for (const auto& message : received) if (message.content == content) ++count;
        return count;
    };
    const QString groupText = "real-redis-group-exactly-once";
    ok = expect(alice.sendMessage(groupText) && waitFor([&] { return countMessage(groupText) == 1; }), "cross-instance public message") && ok;
    const QString privateText = "real-redis-private-exactly-once";
    ok = expect(alice.sendPrivateMessage("930102", privateText)
        && waitFor([&] { return countMessage(privateText) == 1; }), "cross-instance private message") && ok;
    qInfo() << "[real-redis] group/private routing passed";
    for (const int size : {8192, 1024 * 1024 + 4096}) {
        const QString name = QString("real-redis-%1.bin").arg(size);
        QByteArray payload(size, Qt::Uninitialized);
        for (int i = 0; i < size; ++i) payload[i] = char(i % 251);
        QFile file(data.filePath(name));
        if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size()) return 1;
        file.close();
        ok = expect(alice.sendFile(file.fileName(), "930102"), "upload through actual server") && ok;
        ok = expect(waitFor([&] {
            for (const auto& message : received) if (message.fileName == name && message.fileData == payload) return true;
            return false;
        }), "cross-instance file bytes should match") && ok;
        if (size > 1024 * 1024) {
            QJsonObject offer;
            ok = expect(waitFor([&] {
                for (const auto& event : events) if (event.value("eventType").toString() == "large_file_offer"
                    && event.value("fileName").toString() == name) { offer = event; return true; }
                return false;
            }), "actual Redis should carry an object-store offer") && ok;
            const auto objectKey = offer.value("objectKey").toString();
            auto hasEvent = [&](const QString& type) {
                for (const auto& event : events) if (event.value("eventType").toString() == type
                    && event.value("objectKey").toString() == objectKey) return true;
                return false;
            };
            ok = expect(QJsonDocument(offer).toJson(QJsonDocument::Compact).size() < 65536
                && !offer.contains("fileData") && FilesystemObjectStore::isValidObjectKey(objectKey), "Pub/Sub offer carries metadata without file bytes") && ok;
            ok = expect(waitFor([&] { return hasEvent("large_file_claim") && hasEvent("large_file_delivered"); }), "claim/delivery receipts on real Pub/Sub") && ok;
            FilesystemObjectStore store(objects.path());
            ok = expect(waitFor([&] { return !QFile::exists(store.objectPath(objectKey)); }), "acknowledged object cleanup") && ok;
        }
        qInfo() << "[real-redis] file route passed, bytes=" << size;
    }
    bob.disconnectFromServer();
    ok = expect(waitFor([&] { return !redis.hasPresence("930102"); }), "disconnect should remove real presence") && ok;
    const QString offline = "real-redis-offline-replay";
    ok = expect(alice.sendPrivateMessage("930102", offline), "offline message queue") && ok;
    ok = expect(login(bob, "930102", "RealBob", first.serverPort(), false)
        && waitFor([&] { return countMessage(offline) == 1; }), "offline replay after moving to another instance") && ok;
    ok = expect(countMessage(groupText) == 1 && countMessage(privateText) == 1, "routing should not duplicate earlier messages") && ok;
    alice.disconnectFromServer(); bob.disconnectFromServer();
    ok = expect(waitFor([&] { return !redis.hasPresence("930101") && !redis.hasPresence("930102"); }), "test presence cleanup") && ok;
    first.stop(); second.stop(); observer.disconnectFromServer();
    qInfo() << "[real-redis] final result" << (ok ? "PASS" : "FAIL") << "prefix=" << prefix;
    return ok ? 0 : 1;
}
