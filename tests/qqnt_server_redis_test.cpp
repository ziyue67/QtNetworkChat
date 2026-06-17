#include "server.h"
#include "message.h"
#include "test_redis_support.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
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

QString extractGroupIdFromSnapshot(const QByteArray& buffer, const QString& groupName) {
    const QList<QByteArray> lines = buffer.split('\n');
    for (const QByteArray& line : lines) {
        const QJsonDocument doc = QJsonDocument::fromJson(line.trimmed());
        if (!doc.isObject()) continue;
        const QJsonObject root = doc.object();
        if (root.value("type").toString() != QLatin1String("server_group_snapshot")) continue;
        const QJsonArray groups = root.value("groups").toArray();
        for (const QJsonValue& groupValue : groups) {
            const QJsonObject group = groupValue.toObject();
            if (group.value("groupName").toString() == groupName) {
                return group.value("groupId").toString();
            }
        }
    }
    return QString();
}

bool drainGroupFileChunks(QTcpSocket* socket,
                          QByteArray* buffer,
                          const QString& expectedGroupId,
                          const QString& expectedFileName,
                          QByteArray* receivedPayload) {
    if (!socket || !buffer || !receivedPayload) return false;

    bool completedExpectedFile = false;
    buffer->append(socket->readAll());
    while (buffer->contains('\n')) {
        const int newlineIndex = buffer->indexOf('\n');
        const QByteArray line = buffer->left(newlineIndex).trimmed();
        *buffer = buffer->mid(newlineIndex + 1);
        if (line.isEmpty()) continue;

        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject()) continue;
        const QJsonObject object = document.object();
        if (object.value("type").toString() != QLatin1String("file_chunk")) continue;

        const QByteArray chunkData = QByteArray::fromBase64(object.value("fileData").toString().toLatin1());
        const qint64 chunkIndex = object.value("chunkIndex").toVariant().toLongLong();
        const qint64 chunkSize = object.value("chunkSize").toVariant().toLongLong();
        const qint64 fileSize = object.value("fileSize").toVariant().toLongLong();
        const qint64 receivedBytes = qMin(fileSize, chunkIndex * chunkSize + chunkData.size());

        QJsonObject ack;
        ack["type"] = "file_chunk_ack";
        ack["transferId"] = object.value("transferId").toString();
        ack["chunkIndex"] = object.value("chunkIndex").toString();
        ack["accepted"] = true;
        ack["reason"] = "";
        ack["receivedBytes"] = QString::number(receivedBytes);
        writeJsonLine(socket, ack);

        if (object.value("receiverId").toString() != expectedGroupId
            || object.value("fileName").toString() != expectedFileName) {
            continue;
        }

        receivedPayload->append(chunkData);
        completedExpectedFile = receivedPayload->size() >= fileSize;
    }
    return completedExpectedFile;
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
    QTcpSocket carol;
    QByteArray aliceBuffer;
    QByteArray bobBuffer;
    QByteArray carolBuffer;

    ok = expect(connectSocket(&alice, serverAPort), "alice should connect to server A") && ok;
    ok = expect(connectSocket(&bob, serverBPort), "bob should connect to server B") && ok;
    ok = expect(connectSocket(&carol, serverBPort), "carol should connect to server B") && ok;
    ok = expect(loginSocket(&alice, QStringLiteral("970001"), QStringLiteral("AliceRedis")),
                "alice should log in on server A") && ok;
    ok = expect(loginSocket(&bob, QStringLiteral("970002"), QStringLiteral("BobRedis")),
                "bob should log in on server B") && ok;
    ok = expect(loginSocket(&carol, QStringLiteral("970003"), QStringLiteral("CarolRedis")),
                "carol should log in on server A") && ok;
    ok = expect(socketBufferContains(&alice, &aliceBuffer, QByteArrayLiteral("\"type\":\"login_success\""), 5000),
                "alice should receive login success") && ok;
    ok = expect(socketBufferContains(&bob, &bobBuffer, QByteArrayLiteral("\"type\":\"login_success\""), 5000),
                "bob should receive login success") && ok;
    ok = expect(socketBufferContains(&carol, &carolBuffer, QByteArrayLiteral("\"type\":\"login_success\""), 5000),
                "carol should receive login success") && ok;

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

    aliceBuffer.clear();
    carolBuffer.clear();
    QJsonObject privateGroupCreate;
    privateGroupCreate["type"] = "server_group_create";
    privateGroupCreate["groupName"] = "Redis Private Group";
    privateGroupCreate["announcement"] = "Initial members should receive snapshots";
    QJsonArray initialMembers;
    initialMembers.append("970003");
    privateGroupCreate["members"] = initialMembers;
    ok = expect(writeJsonLine(&alice, privateGroupCreate), "alice should create a private group with an initial member") && ok;
    ok = expect(waitFor([&] {
        aliceBuffer.append(alice.readAll());
        return aliceBuffer.contains(QByteArrayLiteral("\"type\":\"server_group_snapshot\""))
            && aliceBuffer.contains(QByteArrayLiteral("Redis Private Group"))
            && aliceBuffer.contains(QByteArrayLiteral("Initial members should receive snapshots"));
    }, 5000), "alice should receive the created private group snapshot") && ok;
    ok = expect(waitFor([&] {
        carolBuffer.append(carol.readAll());
        return carolBuffer.contains(QByteArrayLiteral("\"type\":\"server_group_snapshot\""))
            && carolBuffer.contains(QByteArrayLiteral("Redis Private Group"))
            && carolBuffer.contains(QByteArrayLiteral("Initial members should receive snapshots"));
    }, 5000), "carol should receive the Redis-routed private group snapshot as an initial member") && ok;
    ok = expect(aliceBuffer.contains("Redis Private Group"),
                "alice snapshot should include the created private group") && ok;
    ok = expect(carolBuffer.contains("Redis Private Group")
                    && carolBuffer.contains("970003")
                    && carolBuffer.contains("Initial members should receive snapshots"),
                "carol snapshot should include the initial member and announcement") && ok;

    const QString privateGroupId = extractGroupIdFromSnapshot(aliceBuffer, QStringLiteral("Redis Private Group"));
    ok = expect(!privateGroupId.isEmpty(), "created private group id should be available from snapshot") && ok;
    const QString privateGroupText = QStringLiteral("Redis private group text should cross instances");
    aliceBuffer.clear();
    bobBuffer.clear();
    carolBuffer.clear();
    QJsonObject privateGroupMessage;
    privateGroupMessage["type"] = "server_group_message";
    privateGroupMessage["groupId"] = privateGroupId;
    privateGroupMessage["content"] = privateGroupText;
    ok = expect(writeJsonLine(&alice, privateGroupMessage), "alice should send a private group message") && ok;
    ok = expect(waitFor([&] {
        carolBuffer.append(carol.readAll());
        return carolBuffer.contains(QByteArrayLiteral("\"type\":\"server_group_message\""))
            && carolBuffer.contains(privateGroupId.toUtf8())
            && carolBuffer.contains(privateGroupText.toUtf8());
    }, 5000), "carol should receive the Redis-routed private group message") && ok;
    ok = expect(!waitFor([&] {
        bobBuffer.append(bob.readAll());
        return bobBuffer.contains(privateGroupText.toUtf8());
    }, 700), "non-member bob should not receive the private group message") && ok;

    const QString privateGroupFileName = QStringLiteral("redis-private-group-file.txt");
    const QByteArray privateGroupFilePayload = QByteArrayLiteral("Redis private group file should cross instances");
    const QString privateGroupFileHash = QString::fromLatin1(
        QCryptographicHash::hash(privateGroupFilePayload, QCryptographicHash::Sha256).toHex());
    QByteArray carolGroupFilePayload;
    QByteArray bobGroupFilePayload;
    bool carolReceivedGroupFile = false;
    QMetaObject::Connection carolFileAckConnection = QObject::connect(&carol, &QTcpSocket::readyRead, &app, [&] {
        if (drainGroupFileChunks(&carol, &carolBuffer, privateGroupId, privateGroupFileName, &carolGroupFilePayload)
            && carolGroupFilePayload == privateGroupFilePayload) {
            carolReceivedGroupFile = true;
        }
    });
    aliceBuffer.clear();
    bobBuffer.clear();
    carolBuffer.clear();
    QJsonObject privateGroupFile;
    privateGroupFile["type"] = "file";
    privateGroupFile["messageType"] = static_cast<int>(MessageType::File);
    privateGroupFile["groupId"] = privateGroupId;
    privateGroupFile["content"] = QStringLiteral("sent file: redis-private-group-file.txt");
    privateGroupFile["fileName"] = privateGroupFileName;
    privateGroupFile["fileSize"] = QString::number(privateGroupFilePayload.size());
    privateGroupFile["fileHash"] = privateGroupFileHash;
    privateGroupFile["chunkSize"] = QString::number(privateGroupFilePayload.size());
    privateGroupFile["chunkCount"] = QStringLiteral("1");
    privateGroupFile["fileData"] = QString::fromLatin1(privateGroupFilePayload.toBase64());
    ok = expect(writeJsonLine(&alice, privateGroupFile), "alice should send a private group file") && ok;
    ok = expect(waitFor([&] {
        if (drainGroupFileChunks(&carol, &carolBuffer, privateGroupId, privateGroupFileName, &carolGroupFilePayload)
            && carolGroupFilePayload == privateGroupFilePayload) {
            carolReceivedGroupFile = true;
        }
        return carolReceivedGroupFile;
    }, 8000), "carol should receive the Redis-routed private group file") && ok;
    QObject::disconnect(carolFileAckConnection);
    ok = expect(!waitFor([&] {
        return drainGroupFileChunks(&bob, &bobBuffer, privateGroupId, privateGroupFileName, &bobGroupFilePayload);
    }, 700), "non-member bob should not receive the private group file") && ok;

    alice.disconnectFromHost();
    bob.disconnectFromHost();
    carol.disconnectFromHost();
    serverA.stop();
    serverB.stop();
    redisEnv.stop();
    return ok ? 0 : 1;
}
