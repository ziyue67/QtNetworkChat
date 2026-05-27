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

#include <cstdio>
#include <functional>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FileChunkDuplicateAck assertion failed: %s\n", message);
        std::fflush(stderr);
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

    const QString crossConnectionTransferId = "cross-connection-resume-transfer";
    ok = expect(writeJson(socket, makeChunk(0, crossConnectionTransferId)),
                "raw socket should send a chunk before reconnecting") && ok;

    QJsonObject crossConnectionAck;
    ok = expect(waitForMessage(socket, buffer, [&](const QJsonObject& message) {
        return message["type"].toString() == "file_chunk_ack"
            && message["transferId"].toString() == crossConnectionTransferId
            && message["chunkIndex"].toVariant().toLongLong() == 0;
    }, &crossConnectionAck), "server should ack the cross-connection first chunk") && ok;
    ok = expect(crossConnectionAck["accepted"].toBool(false),
                "cross-connection first chunk ack should be accepted") && ok;

    socket.disconnectFromHost();
    ok = expect(waitFor([&] { return socket.state() == QAbstractSocket::UnconnectedState; }, 5000),
                "raw socket should disconnect before cross-connection resume query") && ok;

    QTcpSocket resumedSocket;
    QByteArray resumedBuffer;
    QObject::connect(&resumedSocket, &QTcpSocket::readyRead, &app, [&] {
        resumedBuffer.append(resumedSocket.readAll());
    });
    resumedSocket.connectToHost(QHostAddress::LocalHost, port);
    ok = expect(resumedSocket.waitForConnected(5000),
                "raw socket should reconnect for cross-connection resume query") && ok;
    QJsonObject relogin = login;
    relogin["mode"] = "login";
    ok = expect(writeJson(resumedSocket, relogin),
                "raw socket should log in again before cross-connection resume query") && ok;
    ok = expect(waitForMessage(resumedSocket, resumedBuffer, [](const QJsonObject& message) {
        return message["type"].toString() == "login_success";
    }, nullptr), "raw socket should receive login success after reconnect") && ok;

    QJsonObject crossConnectionResumeQuery;
    crossConnectionResumeQuery["type"] = "file_transfer_resume_query";
    crossConnectionResumeQuery["transferId"] = crossConnectionTransferId;
    ok = expect(writeJson(resumedSocket, crossConnectionResumeQuery),
                "raw socket should query resume state after reconnect") && ok;

    QJsonObject crossConnectionResumeState;
    ok = expect(waitForMessage(resumedSocket, resumedBuffer, [&](const QJsonObject& message) {
        return message["type"].toString() == "file_transfer_resume_state"
            && message["transferId"].toString() == crossConnectionTransferId;
    }, &crossConnectionResumeState), "server should report resume state across connections") && ok;
    ok = expect(crossConnectionResumeState["canResume"].toBool(false),
                "cross-connection resume state should remain resumable") && ok;
    ok = expect(crossConnectionResumeState["confirmedBytes"].toVariant().toLongLong()
                    == crossConnectionAck["receivedBytes"].toVariant().toLongLong(),
                "cross-connection resume state should keep confirmed bytes") && ok;
    const QJsonArray crossConnectionReceivedChunks = crossConnectionResumeState["receivedChunks"].toArray();
    ok = expect(crossConnectionReceivedChunks.size() == 1
                    && crossConnectionReceivedChunks.first().toVariant().toLongLong() == 0,
                "cross-connection resume state should keep received chunk indexes") && ok;
    resumedSocket.disconnectFromHost();

    socket.connectToHost(QHostAddress::LocalHost, port);
    ok = expect(socket.waitForConnected(5000),
                "raw socket should reconnect to continue duplicate chunk checks") && ok;
    ok = expect(writeJson(socket, relogin),
                "raw socket should log in again to continue duplicate chunk checks") && ok;
    ok = expect(waitForMessage(socket, buffer, [](const QJsonObject& message) {
        return message["type"].toString() == "login_success";
    }, nullptr), "raw socket should receive login success before continuing") && ok;

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

    const QString emptyChunkTransferId = "empty-chunk-transfer";
    QJsonObject emptyChunk = makeChunk(0, emptyChunkTransferId);
    emptyChunk["fileData"] = "";
    ok = expect(writeJson(socket, emptyChunk), "raw socket should send an empty chunk") && ok;

    QJsonObject emptyChunkAck;
    ok = expect(waitForMessage(socket, buffer, [&](const QJsonObject& message) {
        return message["type"].toString() == "file_chunk_ack"
            && message["transferId"].toString() == emptyChunkTransferId
            && message["chunkIndex"].toVariant().toLongLong() == 0;
    }, &emptyChunkAck), "server should reject an empty chunk") && ok;
    ok = expect(!emptyChunkAck["accepted"].toBool(true),
                "empty chunk ack should be rejected") && ok;
    ok = expect(emptyChunkAck["reason"].toString().contains("分片内容为空"),
                "empty chunk rejection should explain the empty payload") && ok;

    QJsonObject emptyChunkResumeQuery;
    emptyChunkResumeQuery["type"] = "file_transfer_resume_query";
    emptyChunkResumeQuery["transferId"] = emptyChunkTransferId;
    ok = expect(writeJson(socket, emptyChunkResumeQuery),
                "raw socket should query resume state after empty chunk rejection") && ok;

    QJsonObject emptyChunkResumeState;
    ok = expect(waitForMessage(socket, buffer, [&](const QJsonObject& message) {
        return message["type"].toString() == "file_transfer_resume_state"
            && message["transferId"].toString() == emptyChunkTransferId;
    }, &emptyChunkResumeState), "server should not keep rejected empty chunk state") && ok;
    ok = expect(!emptyChunkResumeState["canResume"].toBool(true),
                "empty chunk transfer should not remain resumable after rejection") && ok;

    const QString invalidIndexTransferId = "invalid-index-transfer";
    ok = expect(writeJson(socket, makeChunk(2, invalidIndexTransferId)),
                "raw socket should send an out-of-range chunk index") && ok;

    bool invalidIndexAckRejected = false;
    bool invalidIndexSystemNotice = false;
    ok = expect(waitFor([&] {
        buffer.append(socket.readAll());
        const QVector<QJsonObject> messages = takeJsonLines(buffer);
        for (const QJsonObject& message : messages) {
            if (message["type"].toString() == "file_chunk_ack"
                && message["transferId"].toString() == invalidIndexTransferId
                && message["chunkIndex"].toVariant().toLongLong() == 2
                && !message["accepted"].toBool(true)
                && message["reason"].toString().contains("分片序号")) {
                invalidIndexAckRejected = true;
            }
            if (message["type"].toString() == "system"
                && message["content"].toString().contains("分片序号")) {
                invalidIndexSystemNotice = true;
            }
        }
        return invalidIndexAckRejected && invalidIndexSystemNotice;
    }), "server should reject an out-of-range chunk index with ack and system notice") && ok;

    const QString mismatchedChunkCountTransferId = "mismatched-chunk-count-transfer";
    QJsonObject mismatchedChunkCount = makeChunk(0, mismatchedChunkCountTransferId);
    mismatchedChunkCount["chunkCount"] = QString::number(3);
    ok = expect(writeJson(socket, mismatchedChunkCount),
                "raw socket should send a chunk with mismatched chunk count metadata") && ok;

    bool mismatchedChunkCountAckRejected = false;
    bool mismatchedChunkCountSystemNotice = false;
    ok = expect(waitFor([&] {
        buffer.append(socket.readAll());
        const QVector<QJsonObject> messages = takeJsonLines(buffer);
        for (const QJsonObject& message : messages) {
            if (message["type"].toString() == "file_chunk_ack"
                && message["transferId"].toString() == mismatchedChunkCountTransferId
                && message["chunkIndex"].toVariant().toLongLong() == 0
                && !message["accepted"].toBool(true)
                && message["reason"].toString().contains("分片数量不一致")) {
                mismatchedChunkCountAckRejected = true;
            }
            if (message["type"].toString() == "system"
                && message["content"].toString().contains("分片数量不一致")) {
                mismatchedChunkCountSystemNotice = true;
            }
        }
        return mismatchedChunkCountAckRejected && mismatchedChunkCountSystemNotice;
    }), "server should reject mismatched chunk count metadata with ack and system notice") && ok;

    const QString shortNonFinalTransferId = "short-non-final-transfer";
    QJsonObject shortNonFinalChunk = makeChunk(0, shortNonFinalTransferId);
    shortNonFinalChunk["fileData"] = QString::fromLatin1(QByteArray("abc").toBase64());
    ok = expect(writeJson(socket, shortNonFinalChunk),
                "raw socket should send a short non-final chunk") && ok;

    bool shortNonFinalAckRejected = false;
    bool shortNonFinalSystemNotice = false;
    ok = expect(waitFor([&] {
        buffer.append(socket.readAll());
        const QVector<QJsonObject> messages = takeJsonLines(buffer);
        for (const QJsonObject& message : messages) {
            if (message["type"].toString() == "file_chunk_ack"
                && message["transferId"].toString() == shortNonFinalTransferId
                && message["chunkIndex"].toVariant().toLongLong() == 0
                && !message["accepted"].toBool(true)
                && message["reason"].toString().contains("非末尾分片大小")) {
                shortNonFinalAckRejected = true;
            }
            if (message["type"].toString() == "system"
                && message["content"].toString().contains("非末尾分片大小")) {
                shortNonFinalSystemNotice = true;
            }
        }
        return shortNonFinalAckRejected && shortNonFinalSystemNotice;
    }), "server should reject a short non-final chunk with ack and system notice") && ok;

    const QString changedMetadataTransferId = "changed-metadata-transfer";
    ok = expect(writeJson(socket, makeChunk(0, changedMetadataTransferId)),
                "raw socket should send the first chunk before changing metadata") && ok;

    QJsonObject changedMetadataFirstAck;
    ok = expect(waitForMessage(socket, buffer, [&](const QJsonObject& message) {
        return message["type"].toString() == "file_chunk_ack"
            && message["transferId"].toString() == changedMetadataTransferId
            && message["chunkIndex"].toVariant().toLongLong() == 0;
    }, &changedMetadataFirstAck), "server should ack the first chunk before metadata changes") && ok;
    ok = expect(changedMetadataFirstAck["accepted"].toBool(false),
                "first changed-metadata chunk ack should be accepted") && ok;

    QJsonObject changedMetadataChunk = makeChunk(1, changedMetadataTransferId);
    changedMetadataChunk["chunkSize"] = QString::number(3);
    changedMetadataChunk["chunkCount"] = QString::number(2);
    changedMetadataChunk["fileSize"] = QString::number(6);
    ok = expect(writeJson(socket, changedMetadataChunk),
                "raw socket should send a later chunk with changed metadata") && ok;

    bool changedMetadataAckRejected = false;
    bool changedMetadataSystemNotice = false;
    ok = expect(waitFor([&] {
        buffer.append(socket.readAll());
        const QVector<QJsonObject> messages = takeJsonLines(buffer);
        for (const QJsonObject& message : messages) {
            if (message["type"].toString() == "file_chunk_ack"
                && message["transferId"].toString() == changedMetadataTransferId
                && message["chunkIndex"].toVariant().toLongLong() == 1
                && !message["accepted"].toBool(true)
                && message["reason"].toString().contains("元数据不一致")) {
                changedMetadataAckRejected = true;
            }
            if (message["type"].toString() == "system"
                && message["content"].toString().contains("元数据不一致")) {
                changedMetadataSystemNotice = true;
            }
        }
        return changedMetadataAckRejected && changedMetadataSystemNotice;
    }), "server should reject later chunks whose metadata changed") && ok;

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
