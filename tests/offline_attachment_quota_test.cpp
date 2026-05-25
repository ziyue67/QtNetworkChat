#include "client.h"
#include "message.h"
#include "server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
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
#include <QTemporaryDir>
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

bool loginClient(Client& client,
                 const QString& account,
                 const QString& userName,
                 quint16 port,
                 bool registerMode) {
    client.setUserInfo(account, userName);
    client.setAccountInfo(account, "secret", registerMode);
    if (!client.connectToServer("127.0.0.1", port)) return false;
    return client.waitForLoginResult(5000);
}

bool writeLargeFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QByteArray payload(1024 * 1024 + 4096, Qt::Uninitialized);
    for (int i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>('A' + (i % 26));
    }
    return file.write(payload) == payload.size();
}

bool writeSmallFile(const QString& filePath, QByteArray* payloadOut) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QByteArray payload("offline replay cleanup payload\n", 31);
    for (int i = 0; i < 128; ++i) {
        payload.append(static_cast<char>('a' + (i % 26)));
    }
    if (file.write(payload) != payload.size()) return false;
    if (payloadOut) *payloadOut = payload;
    return true;
}

int offlineAttachmentFileCount(const QString& appDataDir) {
    const QString root = appDataDir + "/offline_files";
    if (!QDir(root).exists()) return 0;

    int count = 0;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

int offlineQueueCount(const QString& appDataDir, const QString& receiverId) {
    const QString dbPath = appDataDir + "/accounts.sqlite3";
    if (!QFile::exists(dbPath)) return 0;

    int count = -1;
    const QString connectionName = "offline_quota_count_" + QString::number(QCoreApplication::applicationPid());
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

bool insertOfflineAttachmentQueue(const QString& appDataDir,
                                  const QString& receiverId,
                                  const QString& fileName,
                                  const QString& attachmentPath,
                                  qint64 declaredFileSize) {
    const QString dbPath = appDataDir + "/accounts.sqlite3";
    if (!QFile::exists(dbPath)) return false;

    QJsonObject payload;
    payload["type"] = "file";
    payload["messageType"] = static_cast<int>(MessageType::File);
    payload["senderId"] = "970001";
    payload["senderName"] = "QuotaSender";
    payload["receiverId"] = receiverId;
    payload["content"] = QString("发送了文件: %1").arg(fileName);
    payload["fileName"] = fileName;
    payload["fileSize"] = QString::number(declaredFileSize);
    payload["fileHash"] = "offline-attachment-test";
    payload["chunkSize"] = QString::number(256 * 1024);
    payload["chunkCount"] = QString::number(1);
    payload["offlineFilePath"] = attachmentPath;
    payload["offlineFileStoredOnDisk"] = true;

    bool inserted = false;
    const QString connectionName = "offline_missing_insert_" + QString::number(QCoreApplication::applicationPid());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO offline_messages(receiver_id, payload, created_at) VALUES(?, ?, datetime('now'))");
            query.addBindValue(receiverId);
            query.addBindValue(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
            inserted = query.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return inserted;
}

bool insertMissingOfflineAttachmentQueue(const QString& appDataDir,
                                         const QString& receiverId,
                                         const QString& fileName,
                                         const QString& missingPath) {
    return insertOfflineAttachmentQueue(appDataDir, receiverId, fileName, missingPath, 128);
}
}

int main(int argc, char** argv) {
    qputenv("QTNETWORKCHAT_OFFLINE_ATTACHMENT_QUOTA_MB", "1");

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("offline_attachment_quota_test");
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

    const QString orphanDirPath = appDataDir + "/offline_files/orphan";
    ok = expect(QDir().mkpath(orphanDirPath),
                "orphan attachment directory should be created before server startup") && ok;
    const QString orphanPath = orphanDirPath + "/payload.bin";
    QFile orphanFile(orphanPath);
    ok = expect(orphanFile.open(QIODevice::WriteOnly),
                "orphan attachment file should be writable before server startup") && ok;
    if (ok) {
        ok = expect(orphanFile.write(QByteArray("orphan-payload")) == 14,
                    "orphan attachment file should contain the test payload") && ok;
        orphanFile.close();
    }
    ok = expect(QFile::exists(orphanPath),
                "orphan attachment should exist before server startup cleanup") && ok;
    if (!ok) return 1;

    Server server;
    ok = expect(server.start(port), "server should start on the test port") && ok;
    if (!ok) return 1;
    ok = expect(!QFile::exists(orphanPath),
                "server startup should remove unreferenced offline attachment files") && ok;

    const QString receiverId = "970002";
    Client receiver;
    bool receiverDisconnected = false;
    QObject::connect(&server, &Server::clientDisconnected, &app, [&](const QString& userId) {
        if (userId == receiverId) receiverDisconnected = true;
    });
    ok = expect(loginClient(receiver, receiverId, "QuotaReceiver", port, true),
                "receiver should register before going offline") && ok;
    receiver.disconnectFromServer();
    ok = expect(waitFor([&] { return receiverDisconnected; }),
                "server should observe receiver disconnect before offline send") && ok;

    Client sender;
    bool senderDisconnected = false;
    QObject::connect(&server, &Server::clientDisconnected, &app, [&](const QString& userId) {
        if (userId == QStringLiteral("970001")) senderDisconnected = true;
    });
    ok = expect(loginClient(sender, "970001", "QuotaSender", port, true),
                "sender should register and log in") && ok;

    QTemporaryDir tempDir;
    ok = expect(tempDir.isValid(), "temporary directory should be available") && ok;
    const QString filePath = tempDir.filePath("quota-too-large.bin");
    ok = expect(writeLargeFile(filePath), "large offline quota test file should be created") && ok;
    if (!ok) return 1;

    ok = expect(sender.sendFile(filePath, receiverId),
                "sender should finish uploading the offline file to the server") && ok;
    waitFor([] { return false; }, 250);

    ok = expect(offlineAttachmentFileCount(appDataDir) == 0,
                "quota-rejected offline file should not leave an attachment on disk") && ok;
    ok = expect(offlineQueueCount(appDataDir, receiverId) == 0,
                "quota-rejected offline file should not be queued for replay") && ok;

    Client replayReceiver;
    QVector<Message> replayedMessages;
    QObject::connect(&replayReceiver, &Client::newMessage, &app, [&](const Message& msg) {
        replayedMessages.append(msg);
    });
    ok = expect(loginClient(replayReceiver, receiverId, "QuotaReceiver", port, false),
                "receiver should log in again for offline replay check") && ok;
    waitFor([] { return false; }, 700);
    ok = expect(replayedMessages.isEmpty(),
                "quota-rejected offline file should not be replayed after receiver login") && ok;

    receiverDisconnected = false;
    replayReceiver.disconnectFromServer();
    ok = expect(waitFor([&] { return receiverDisconnected; }),
                "server should observe quota replay receiver disconnect") && ok;

    const QString cleanupReceiverId = "970003";
    Client cleanupReceiverSeed;
    bool cleanupReceiverDisconnected = false;
    QObject::connect(&server, &Server::clientDisconnected, &app, [&](const QString& userId) {
        if (userId == cleanupReceiverId) cleanupReceiverDisconnected = true;
    });
    ok = expect(loginClient(cleanupReceiverSeed, cleanupReceiverId, "CleanupReceiver", port, true),
                "cleanup receiver should register before going offline") && ok;
    cleanupReceiverSeed.disconnectFromServer();
    ok = expect(waitFor([&] { return cleanupReceiverDisconnected; }),
                "server should observe cleanup receiver disconnect before offline replay send") && ok;

    QByteArray expectedPayload;
    const QString replayFilePath = tempDir.filePath("offline-replay-cleanup.bin");
    ok = expect(writeSmallFile(replayFilePath, &expectedPayload),
                "small offline replay test file should be created") && ok;
    if (!ok) return 1;

    ok = expect(sender.sendFile(replayFilePath, cleanupReceiverId),
                "sender should finish uploading the replay cleanup file to the server") && ok;
    ok = expect(waitFor([&] { return offlineAttachmentFileCount(appDataDir) == 1; }),
                "accepted offline file should be saved as one attachment") && ok;
    ok = expect(offlineQueueCount(appDataDir, cleanupReceiverId) == 1,
                "accepted offline file should be queued for replay") && ok;

    Client cleanupReceiver;
    QVector<Message> cleanupReplayMessages;
    QObject::connect(&cleanupReceiver, &Client::newMessage, &app, [&](const Message& msg) {
        cleanupReplayMessages.append(msg);
    });
    ok = expect(loginClient(cleanupReceiver, cleanupReceiverId, "CleanupReceiver", port, false),
                "cleanup receiver should log in again for offline attachment replay") && ok;
    ok = expect(waitFor([&] {
        for (const Message& msg : cleanupReplayMessages) {
            if (msg.type == MessageType::File
                && msg.fileName == "offline-replay-cleanup.bin"
                && msg.fileData == expectedPayload) {
                return true;
            }
        }
        return false;
    }, 7000), "offline attachment should be replayed with its original payload") && ok;
    ok = expect(waitFor([&] {
        return offlineAttachmentFileCount(appDataDir) == 0
            && offlineQueueCount(appDataDir, cleanupReceiverId) == 0;
    }, 3000), "delivered offline attachment should be removed from disk and queue") && ok;

    cleanupReceiverDisconnected = false;
    cleanupReceiver.disconnectFromServer();
    ok = expect(waitFor([&] { return cleanupReceiverDisconnected; }),
                "server should observe cleanup receiver disconnect after replay") && ok;

    const QString missingReceiverId = "970004";
    Client missingReceiverSeed;
    bool missingReceiverDisconnected = false;
    QObject::connect(&server, &Server::clientDisconnected, &app, [&](const QString& userId) {
        if (userId == missingReceiverId) missingReceiverDisconnected = true;
    });
    ok = expect(loginClient(missingReceiverSeed, missingReceiverId, "MissingReceiver", port, true),
                "missing attachment receiver should register before queue seeding") && ok;
    missingReceiverSeed.disconnectFromServer();
    ok = expect(waitFor([&] { return missingReceiverDisconnected; }),
                "server should observe missing attachment receiver disconnect before queue seeding") && ok;

    const QString missingFileName = "missing-offline-attachment.bin";
    const QString missingPath = appDataDir + "/offline_files/missing/not-created.bin";
    ok = expect(insertMissingOfflineAttachmentQueue(appDataDir, missingReceiverId, missingFileName, missingPath),
                "missing attachment offline queue row should be inserted") && ok;
    ok = expect(offlineQueueCount(appDataDir, missingReceiverId) == 1,
                "missing attachment offline row should be queued before replay") && ok;

    Client missingReceiver;
    QVector<Message> missingReplayMessages;
    QObject::connect(&missingReceiver, &Client::newMessage, &app, [&](const Message& msg) {
        missingReplayMessages.append(msg);
    });
    ok = expect(loginClient(missingReceiver, missingReceiverId, "MissingReceiver", port, false),
                "missing attachment receiver should log in for cleanup replay") && ok;
    ok = expect(waitFor([&] {
        for (const Message& msg : missingReplayMessages) {
            if (msg.type == MessageType::System
                && msg.content.contains(QString::fromUtf8("离线文件已丢失"))
                && msg.content.contains(missingFileName)) {
                return true;
            }
        }
        return false;
    }, 3000), "missing offline attachment should produce a clear system notice") && ok;
    ok = expect(waitFor([&] { return offlineQueueCount(appDataDir, missingReceiverId) == 0; }, 3000),
                "missing offline attachment queue row should be cleared after notice") && ok;

    missingReceiverDisconnected = false;
    missingReceiver.disconnectFromServer();
    ok = expect(waitFor([&] { return missingReceiverDisconnected; }),
                "server should observe missing attachment receiver disconnect after cleanup") && ok;

    const QString mismatchReceiverId = "970005";
    Client mismatchReceiverSeed;
    bool mismatchReceiverDisconnected = false;
    QObject::connect(&server, &Server::clientDisconnected, &app, [&](const QString& userId) {
        if (userId == mismatchReceiverId) mismatchReceiverDisconnected = true;
    });
    ok = expect(loginClient(mismatchReceiverSeed, mismatchReceiverId, "MismatchReceiver", port, true),
                "mismatched attachment receiver should register before queue seeding") && ok;
    mismatchReceiverSeed.disconnectFromServer();
    ok = expect(waitFor([&] { return mismatchReceiverDisconnected; }),
                "server should observe mismatched attachment receiver disconnect before queue seeding") && ok;

    const QString mismatchFileName = "mismatched-offline-attachment.bin";
    const QString mismatchDirPath = appDataDir + "/offline_files/mismatch";
    ok = expect(QDir().mkpath(mismatchDirPath),
                "mismatched attachment directory should be created") && ok;
    const QString mismatchPath = mismatchDirPath + "/payload.bin";
    QFile mismatchFile(mismatchPath);
    ok = expect(mismatchFile.open(QIODevice::WriteOnly),
                "mismatched attachment file should be writable") && ok;
    if (ok) {
        ok = expect(mismatchFile.write(QByteArray("short-payload")) == 13,
                    "mismatched attachment file should contain the test payload") && ok;
        mismatchFile.close();
    }
    ok = expect(QFile::exists(mismatchPath),
                "mismatched attachment file should exist before replay") && ok;
    ok = expect(insertOfflineAttachmentQueue(appDataDir,
                                             mismatchReceiverId,
                                             mismatchFileName,
                                             mismatchPath,
                                             128),
                "mismatched attachment offline queue row should be inserted") && ok;
    ok = expect(offlineQueueCount(appDataDir, mismatchReceiverId) == 1,
                "mismatched attachment offline row should be queued before replay") && ok;

    Client mismatchReceiver;
    QVector<Message> mismatchReplayMessages;
    QObject::connect(&mismatchReceiver, &Client::newMessage, &app, [&](const Message& msg) {
        mismatchReplayMessages.append(msg);
    });
    ok = expect(loginClient(mismatchReceiver, mismatchReceiverId, "MismatchReceiver", port, false),
                "mismatched attachment receiver should log in for cleanup replay") && ok;
    ok = expect(waitFor([&] {
        for (const Message& msg : mismatchReplayMessages) {
            if (msg.type == MessageType::System
                && msg.content.contains(QString::fromUtf8("离线文件大小异常"))
                && msg.content.contains(mismatchFileName)) {
                return true;
            }
        }
        return false;
    }, 3000), "mismatched offline attachment should produce a clear system notice") && ok;
    ok = expect(waitFor([&] {
        return offlineQueueCount(appDataDir, mismatchReceiverId) == 0
            && !QFile::exists(mismatchPath);
    }, 3000), "mismatched offline attachment queue row and file should be cleared after notice") && ok;

    mismatchReceiverDisconnected = false;
    mismatchReceiver.disconnectFromServer();
    ok = expect(waitFor([&] { return mismatchReceiverDisconnected; }),
                "server should observe mismatched attachment receiver disconnect after cleanup") && ok;

    senderDisconnected = false;
    sender.disconnectFromServer();
    ok = expect(waitFor([&] { return senderDisconnected; }),
                "server should observe sender disconnect before shutdown") && ok;
    server.stop();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    qunsetenv("QTNETWORKCHAT_OFFLINE_ATTACHMENT_QUOTA_MB");
    return ok ? 0 : 1;
}
