#include "server.h"

#include <QCoreApplication>
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

bool writeJson(QTcpSocket& socket, const QJsonObject& obj) {
    if (socket.state() != QAbstractSocket::ConnectedState) return false;
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    return socket.write(payload) == payload.size()
        && socket.write("\n") == 1
        && socket.flush();
}

QVector<QJsonObject> takeJsonLines(QByteArray& buffer) {
    QVector<QJsonObject> messages;
    while (buffer.contains('\n')) {
        const int newlineIndex = buffer.indexOf('\n');
        const QByteArray line = buffer.left(newlineIndex);
        buffer = buffer.mid(newlineIndex + 1);
        if (line.isEmpty()) continue;

        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isObject()) {
            messages.append(doc.object());
        }
    }
    return messages;
}

bool waitForMessage(QTcpSocket& socket,
                    QByteArray& buffer,
                    const std::function<bool(const QJsonObject&)>& predicate,
                    QJsonObject* matched,
                    int timeoutMs = 5000) {
    return waitFor([&] {
        buffer.append(socket.readAll());
        const QVector<QJsonObject> messages = takeJsonLines(buffer);
        for (const QJsonObject& message : messages) {
            if (predicate(message)) {
                if (matched) *matched = message;
                return true;
            }
        }
        return false;
    }, timeoutMs);
}

QJsonObject makeChunk(qint64 chunkIndex,
                      const QString& transferId = QStringLiteral("duplicate-transfer"),
                      const QString& fileHash = QString(),
                      const QString& fileName = QStringLiteral("duplicate.bin"),
                      const QString& receiverId = QString()) {
    const QByteArray chunkData = chunkIndex == 0
        ? QByteArray("abcd")
        : QByteArray("ef");

    QJsonObject obj;
    obj["type"] = "file_chunk";
    obj["transferId"] = transferId;
    obj["senderId"] = "940001";
    obj["senderName"] = "DuplicateSender";
    obj["receiverId"] = receiverId;
    obj["messageType"] = static_cast<int>(MessageType::File);
    obj["content"] = "发送了文件: " + fileName;
    obj["fileName"] = fileName;
    obj["fileSize"] = QString::number(6);
    obj["fileHash"] = fileHash;
    obj["chunkSize"] = QString::number(4);
    obj["chunkCount"] = QString::number(2);
    obj["chunkIndex"] = QString::number(chunkIndex);
    obj["fileData"] = QString::fromLatin1(chunkData.toBase64());
    return obj;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("file_chunk_duplicate_ack_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    const quint16 port = freeLocalPort();
    bool ok = true;
    ok = expect(port != 0, "a local test port should be available") && ok;
    if (!ok) return 1;

    Server server;
    ok = expect(server.start(port), "server should start on the test port") && ok;
    if (!ok) return 1;

    QTcpSocket socket;
    QByteArray buffer;
    QObject::connect(&socket, &QTcpSocket::readyRead, &app, [&] {
        buffer.append(socket.readAll());
    });

    socket.connectToHost(QHostAddress::LocalHost, port);
    ok = expect(socket.waitForConnected(5000), "raw socket should connect to server") && ok;

    QJsonObject login;
    login["type"] = "login";
    login["mode"] = "register";
    login["account"] = "940001";
    login["password"] = "secret";
    login["userName"] = "DuplicateSender";
    ok = expect(writeJson(socket, login), "raw socket should send login") && ok;

    QJsonObject loginSuccess;
    ok = expect(waitForMessage(socket, buffer, [](const QJsonObject& message) {
        return message["type"].toString() == "login_success";
    }, &loginSuccess), "raw socket should receive login success") && ok;
    if (!ok) return 1;

    const QJsonObject firstChunk = makeChunk(0);
    ok = expect(writeJson(socket, firstChunk), "raw socket should send the first chunk") && ok;

    QJsonObject firstAck;
    ok = expect(waitForMessage(socket, buffer, [](const QJsonObject& message) {
        return message["type"].toString() == "file_chunk_ack"
            && message["transferId"].toString() == "duplicate-transfer"
            && message["chunkIndex"].toVariant().toLongLong() == 0;
    }, &firstAck), "server should ack the first chunk") && ok;
    ok = expect(firstAck["accepted"].toBool(false), "first chunk ack should be accepted") && ok;
    const qint64 firstReceivedBytes = firstAck["receivedBytes"].toVariant().toLongLong();
    ok = expect(firstReceivedBytes == 4, "first chunk ack should report four received bytes") && ok;

    QJsonObject resumeQuery;
    resumeQuery["type"] = "file_transfer_resume_query";
    resumeQuery["transferId"] = "duplicate-transfer";
    ok = expect(writeJson(socket, resumeQuery), "raw socket should query resume state") && ok;

    QJsonObject resumeState;
    ok = expect(waitForMessage(socket, buffer, [](const QJsonObject& message) {
        return message["type"].toString() == "file_transfer_resume_state"
            && message["transferId"].toString() == "duplicate-transfer";
    }, &resumeState), "server should report resume state for the pending transfer") && ok;
    ok = expect(resumeState["canResume"].toBool(false), "resume state should be resumable") && ok;
    ok = expect(resumeState["confirmedBytes"].toVariant().toLongLong() == firstReceivedBytes,
                "resume state should report confirmed bytes") && ok;
    ok = expect(resumeState["nextChunkIndex"].toVariant().toLongLong() == 1,
                "resume state should report the first missing chunk index") && ok;
    const QJsonArray receivedChunks = resumeState["receivedChunks"].toArray();
    ok = expect(receivedChunks.size() == 1 && receivedChunks.first().toVariant().toLongLong() == 0,
                "resume state should list the received chunk indexes") && ok;

    ok = expect(writeJson(socket, firstChunk), "raw socket should resend the first chunk") && ok;

    QJsonObject duplicateAck;
    ok = expect(waitForMessage(socket, buffer, [](const QJsonObject& message) {
        return message["type"].toString() == "file_chunk_ack"
            && message["transferId"].toString() == "duplicate-transfer"
            && message["chunkIndex"].toVariant().toLongLong() == 0;
    }, &duplicateAck), "server should ack the duplicate chunk") && ok;
    ok = expect(duplicateAck["accepted"].toBool(false), "duplicate chunk ack should be accepted") && ok;
    ok = expect(duplicateAck["receivedBytes"].toVariant().toLongLong() == firstReceivedBytes,
                "duplicate chunk ack should not increase received bytes") && ok;

    const QString outOfOrderTransferId = "out-of-order-transfer";
    const QString outOfOrderFileName = "out-of-order.bin";
    const QString outOfOrderReceiverId = "949999";
    ok = expect(writeJson(socket, makeChunk(1,
                                            outOfOrderTransferId,
                                            QString(),
                                            outOfOrderFileName,
                                            outOfOrderReceiverId)),
                "raw socket should send the final chunk before the missing first chunk") && ok;

    QJsonObject outOfOrderFirstAck;
    ok = expect(waitForMessage(socket, buffer, [&](const QJsonObject& message) {
        return message["type"].toString() == "file_chunk_ack"
            && message["transferId"].toString() == outOfOrderTransferId
            && message["chunkIndex"].toVariant().toLongLong() == 1;
    }, &outOfOrderFirstAck), "server should ack the out-of-order final chunk") && ok;
    ok = expect(outOfOrderFirstAck["accepted"].toBool(false),
                "out-of-order final chunk ack should be accepted") && ok;
    ok = expect(outOfOrderFirstAck["receivedBytes"].toVariant().toLongLong() == 2,
                "out-of-order final chunk should only count received bytes for that chunk") && ok;

    QJsonObject outOfOrderResumeQuery;
    outOfOrderResumeQuery["type"] = "file_transfer_resume_query";
    outOfOrderResumeQuery["transferId"] = outOfOrderTransferId;
    ok = expect(writeJson(socket, outOfOrderResumeQuery),
                "raw socket should query resume state for the out-of-order transfer") && ok;

    QJsonObject outOfOrderResumeState;
    ok = expect(waitForMessage(socket, buffer, [&](const QJsonObject& message) {
        return message["type"].toString() == "file_transfer_resume_state"
            && message["transferId"].toString() == outOfOrderTransferId;
    }, &outOfOrderResumeState), "server should report missing first chunk in resume state") && ok;
    ok = expect(outOfOrderResumeState["canResume"].toBool(false),
                "out-of-order transfer should remain resumable while the first chunk is missing") && ok;
    ok = expect(outOfOrderResumeState["confirmedBytes"].toVariant().toLongLong() == 2,
                "out-of-order resume state should report received bytes for the final chunk") && ok;
    ok = expect(outOfOrderResumeState["nextChunkIndex"].toVariant().toLongLong() == 0,
                "out-of-order resume state should point to the missing first chunk") && ok;
    const QJsonArray outOfOrderReceivedChunks = outOfOrderResumeState["receivedChunks"].toArray();
    ok = expect(outOfOrderReceivedChunks.size() == 1
                    && outOfOrderReceivedChunks.first().toVariant().toLongLong() == 1,
                "out-of-order resume state should list only the final chunk as received") && ok;

    ok = expect(writeJson(socket, makeChunk(0,
                                            outOfOrderTransferId,
                                            QString(),
                                            outOfOrderFileName,
                                            outOfOrderReceiverId)),
                "raw socket should send the missing first chunk") && ok;

    QJsonObject outOfOrderFinalAck;
    ok = expect(waitForMessage(socket, buffer, [&](const QJsonObject& message) {
        return message["type"].toString() == "file_chunk_ack"
            && message["transferId"].toString() == outOfOrderTransferId
            && message["chunkIndex"].toVariant().toLongLong() == 0;
    }, &outOfOrderFinalAck), "server should ack the missing first chunk") && ok;
    ok = expect(outOfOrderFinalAck["accepted"].toBool(false),
                "missing first chunk ack should be accepted") && ok;
    ok = expect(outOfOrderFinalAck["receivedBytes"].toVariant().toLongLong() == 6,
                "missing first chunk ack should report completed file size") && ok;

    ok = expect(writeJson(socket, outOfOrderResumeQuery),
                "raw socket should query resume state after out-of-order transfer completes") && ok;
    QJsonObject completedOutOfOrderState;
    ok = expect(waitForMessage(socket, buffer, [&](const QJsonObject& message) {
        return message["type"].toString() == "file_transfer_resume_state"
            && message["transferId"].toString() == outOfOrderTransferId;
    }, &completedOutOfOrderState), "server should clear pending state after all out-of-order chunks arrive") && ok;
    ok = expect(!completedOutOfOrderState["canResume"].toBool(true),
                "completed out-of-order transfer should no longer be resumable") && ok;

    ok = expect(writeJson(socket, makeChunk(1)), "raw socket should send the final chunk") && ok;

    QJsonObject finalAck;
    ok = expect(waitForMessage(socket, buffer, [](const QJsonObject& message) {
        return message["type"].toString() == "file_chunk_ack"
            && message["transferId"].toString() == "duplicate-transfer"
            && message["chunkIndex"].toVariant().toLongLong() == 1;
    }, &finalAck), "server should ack the final chunk") && ok;
    ok = expect(finalAck["accepted"].toBool(false), "final chunk ack should be accepted") && ok;
    ok = expect(finalAck["receivedBytes"].toVariant().toLongLong() == 6,
                "final chunk ack should report the completed file size") && ok;

    const QString badHashTransferId = "bad-hash-transfer";
    const QString badHash(64, QLatin1Char('0'));
    ok = expect(writeJson(socket, makeChunk(0, badHashTransferId, badHash)),
                "raw socket should send the first bad-hash chunk") && ok;

    QJsonObject badHashFirstAck;
    ok = expect(waitForMessage(socket, buffer, [&](const QJsonObject& message) {
        return message["type"].toString() == "file_chunk_ack"
            && message["transferId"].toString() == badHashTransferId
            && message["chunkIndex"].toVariant().toLongLong() == 0;
    }, &badHashFirstAck), "server should ack the first bad-hash chunk") && ok;
    ok = expect(badHashFirstAck["accepted"].toBool(false), "first bad-hash chunk ack should be accepted") && ok;

    ok = expect(writeJson(socket, makeChunk(1, badHashTransferId, badHash)),
                "raw socket should send the final bad-hash chunk") && ok;

    bool badHashFinalAckAccepted = false;
    bool badHashRejected = false;
    ok = expect(waitFor([&] {
        buffer.append(socket.readAll());
        const QVector<QJsonObject> messages = takeJsonLines(buffer);
        for (const QJsonObject& message : messages) {
            if (message["type"].toString() == "file_chunk_ack"
                && message["transferId"].toString() == badHashTransferId
                && message["chunkIndex"].toVariant().toLongLong() == 1
                && message["accepted"].toBool(false)) {
                badHashFinalAckAccepted = true;
            }
            if (message["type"].toString() == "system"
                && message["content"].toString().contains("SHA-256")) {
                badHashRejected = true;
            }
        }
        return badHashFinalAckAccepted && badHashRejected;
    }), "server should ack final chunk and reject assembled file when SHA-256 does not match") && ok;

    socket.disconnectFromHost();
    server.stop();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
