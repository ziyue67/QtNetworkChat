#include "client.h"
#include "message.h"
#include "server.h"
#include "test_redis_support.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <functional>

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

bool registerClient(Client& client,
                    const QString& account,
                    const QString& userName,
                    quint16 port) {
    client.setUserInfo(account, userName);
    client.setAccountInfo(account, "secret", true);
    if (!client.connectToServer("127.0.0.1", port)) return false;
    return client.waitForLoginResult(5000);
}

int offlineQueueCount(const QString& appDataDir, const QString& receiverId) {
    const QString dbPath = appDataDir + "/accounts.sqlite3";
    if (!QFile::exists(dbPath)) return 0;

    int count = -1;
    const QString connectionName = "file_cancel_offline_count_" + QString::number(QCoreApplication::applicationPid());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare("SELECT COUNT(*) FROM offline_messages WHERE receiver_id = ?");
            query.addBindValue(receiverId);
            if (query.exec() && query.next()) {
                count = query.value(0).toInt();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return count;
}

bool writeTestFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QByteArray block;
    block.resize(300 * 1024);
    for (int i = 0; i < block.size(); ++i) {
        block[i] = static_cast<char>('A' + (i % 23));
    }
    for (int i = 0; i < 3; ++i) {
        if (file.write(block) != block.size()) return false;
    }
    return true;
}
}

int main(int argc, char** argv) {
    qputenv("QTNETWORKCHAT_E2E_ALLOW_PLAINTEXT_PRIVATE_FILE", "1");
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("file_transfer_cancel_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = testAppDataDir();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    const quint16 port = freeLocalPort();
    bool ok = true;
    ok = expect(port != 0, "a local test port should be available") && ok;
    if (!ok) return 1;

    TestRedisServerEnvironment redis(QStringLiteral("qtchat-file-transfer-cancel-test"));
    QString redisError;
    ok = expect(redis.start(&redisError), "fake Redis should start for file transfer cancel test") && ok;
    if (!ok) return 1;
    redis.applyEnvironment();

    Server server;
    ok = expect(server.start(port), "server should start on the test port") && ok;
    if (!ok) return 1;

    Client sender;
    QStringList senderSystemMessages;
    QObject::connect(&sender, &Client::newMessage, &app, [&](const Message& msg) {
        if (msg.type == MessageType::System) {
            senderSystemMessages << msg.content;
        }
    });

    ok = expect(registerClient(sender, "920001", "Sender", port),
                "sender should register and log in") && ok;

    QTemporaryDir tempDir;
    ok = expect(tempDir.isValid(), "temporary directory should be available") && ok;
    const QString filePath = tempDir.filePath("cancel-transfer.bin");
    ok = expect(writeTestFile(filePath), "test file should be created") && ok;
    if (!ok) return 1;

    bool prepared = false;
    bool cancelRequested = false;
    bool sawAcceptedAck = false;
    bool ackReceivedBytesPositive = false;
    bool ackReceivedBytesMonotonic = true;
    qint64 lastAckReceivedBytes = 0;
    QObject::connect(&sender, &Client::fileTransferPrepared, &app, [&](const QString&, qint64, qint64, qint64, const QString&) {
        prepared = true;
    });
    QObject::connect(&sender, &Client::fileChunkAckReceived, &app, [&](const QString&, qint64, bool accepted, const QString&, qint64 receivedBytes) {
        if (!accepted) return;
        sawAcceptedAck = true;
        if (receivedBytes < lastAckReceivedBytes) {
            ackReceivedBytesMonotonic = false;
        }
        lastAckReceivedBytes = receivedBytes;
        if (receivedBytes > 0) {
            ackReceivedBytesPositive = true;
        }
    });
    QObject::connect(&sender, &Client::fileTransferProgress, &app, [&](const QString&, qint64 bytesPrepared, qint64 totalBytes) {
        if (prepared && !cancelRequested && bytesPrepared > 0 && bytesPrepared < totalBytes) {
            cancelRequested = true;
            sender.cancelCurrentOutgoingTransfer();
        }
    });

    const bool sent = sender.sendFile(filePath);
    ok = expect(sawAcceptedAck, "sender should receive at least one accepted chunk ack") && ok;
    ok = expect(ackReceivedBytesPositive, "chunk ack should expose positive received bytes") && ok;
    ok = expect(ackReceivedBytesMonotonic, "chunk ack received bytes should be monotonic") && ok;
    ok = expect(cancelRequested, "sender should request cancellation after the first uploaded chunk") && ok;
    ok = expect(!sent, "canceled file transfer should not report success") && ok;
    ok = expect(waitFor([&] {
        for (const QString& message : senderSystemMessages) {
            if (message.contains(QString::fromUtf8("服务端已清理未完成分片"))) return true;
        }
        return false;
    }), "sender should receive server cleanup confirmation") && ok;

    const QString rejectingReceiverId = "920002";
    QTcpSocket rejectingReceiver;
    QByteArray rejectingReceiverBuffer;
    bool rejectingReceiverLoggedIn = false;
    bool rejectingReceiverRejectedChunk = false;
    auto drainRejectingReceiver = [&]() {
        rejectingReceiverBuffer.append(rejectingReceiver.readAll());
        while (rejectingReceiverBuffer.contains('\n')) {
            const int newlineIndex = rejectingReceiverBuffer.indexOf('\n');
            const QByteArray line = rejectingReceiverBuffer.left(newlineIndex);
            rejectingReceiverBuffer = rejectingReceiverBuffer.mid(newlineIndex + 1);
            if (line.trimmed().isEmpty()) continue;

            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) continue;
            const QJsonObject obj = doc.object();
            const QString type = obj["type"].toString();
            if (type == "login_success") {
                rejectingReceiverLoggedIn = true;
            } else if (type == "file_chunk" && !rejectingReceiverRejectedChunk) {
                QJsonObject ack;
                ack["type"] = "file_chunk_ack";
                ack["transferId"] = obj["transferId"].toString();
                ack["chunkIndex"] = obj["chunkIndex"].toString();
                ack["accepted"] = false;
                ack["reason"] = "direct receiver rejected chunk for fallback test";
                ack["receivedBytes"] = "0";
                rejectingReceiver.write(QJsonDocument(ack).toJson(QJsonDocument::Compact));
                rejectingReceiver.write("\n");
                rejectingReceiver.flush();
                rejectingReceiverRejectedChunk = true;
            }
        }
    };
    QObject::connect(&rejectingReceiver, &QTcpSocket::readyRead, &app, drainRejectingReceiver);
    rejectingReceiver.connectToHost("127.0.0.1", port);
    ok = expect(rejectingReceiver.waitForConnected(5000),
                "rejecting receiver should connect to the server") && ok;
    QJsonObject login;
    login["type"] = "login";
    login["mode"] = "register";
    login["account"] = rejectingReceiverId;
    login["password"] = "secret";
    login["userName"] = "RejectingReceiver";
    rejectingReceiver.write(QJsonDocument(login).toJson(QJsonDocument::Compact));
    rejectingReceiver.write("\n");
    rejectingReceiver.flush();
    ok = expect(waitFor([&] {
        drainRejectingReceiver();
        return rejectingReceiverLoggedIn;
    }), "rejecting receiver should log in before direct file delivery") && ok;

    const QString fallbackFilePath = tempDir.filePath("direct-reject-fallback.bin");
    ok = expect(writeTestFile(fallbackFilePath), "direct reject fallback test file should be created") && ok;
    ok = expect(sender.sendFile(fallbackFilePath, rejectingReceiverId),
                "sender upload should still finish when direct receiver rejects forwarding") && ok;
    ok = expect(waitFor([&] {
        drainRejectingReceiver();
        return rejectingReceiverRejectedChunk;
    }), "online receiver should reject the forwarded file chunk") && ok;
    ok = expect(waitFor([&] { return offlineQueueCount(appDataDir, rejectingReceiverId) == 1; }),
                "rejected direct file delivery should be queued for offline retry") && ok;
    rejectingReceiver.disconnectFromHost();

    sender.disconnectFromServer();
    server.stop();
    redis.stop();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
