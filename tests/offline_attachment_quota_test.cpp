#include "client.h"
#include "message.h"
#include "server.h"

#include <QCoreApplication>
#include <QDateTime>
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
#include <QTcpSocket>
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

QJsonObject offlineQueuePayload(const QString& appDataDir, const QString& receiverId) {
    const QString dbPath = appDataDir + "/accounts.sqlite3";
    if (!QFile::exists(dbPath)) return {};

    QJsonObject payload;
    const QString connectionName = "offline_quota_payload_" + QString::number(QCoreApplication::applicationPid());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare("SELECT payload FROM offline_messages WHERE receiver_id = ? ORDER BY id ASC LIMIT 1");
            query.addBindValue(receiverId);
            if (query.exec() && query.next()) {
                const QJsonDocument doc = QJsonDocument::fromJson(query.value(0).toString().toUtf8());
                if (doc.isObject()) {
                    payload = doc.object();
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return payload;
}

bool updateOfflineQueuePayload(const QString& appDataDir,
                               const QString& receiverId,
                               const std::function<void(QJsonObject*)>& mutate) {
    const QString dbPath = appDataDir + "/accounts.sqlite3";
    if (!QFile::exists(dbPath)) return false;

    QJsonObject payload = offlineQueuePayload(appDataDir, receiverId);
    if (payload.isEmpty()) return false;
    mutate(&payload);

    bool updated = false;
    const QString connectionName = "offline_quota_payload_update_" + QString::number(QCoreApplication::applicationPid());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare("UPDATE offline_messages SET payload = ? WHERE receiver_id = ?");
            query.addBindValue(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
            query.addBindValue(receiverId);
            updated = query.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return updated;
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
    const qint64 chunkSize = 256 * 1024;
    payload["chunkSize"] = QString::number(chunkSize);
    payload["chunkCount"] = QString::number((declaredFileSize + chunkSize - 1) / chunkSize);
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

bool insertLegacyJsonlOfflineAttachmentQueue(const QString& appDataDir,
                                             const QString& receiverId,
                                             const QString& fileName,
                                             const QString& attachmentPath,
                                             qint64 declaredFileSize) {
    const QString offlineDirPath = appDataDir + "/offline";
    if (!QDir().mkpath(offlineDirPath)) return false;

    QJsonObject payload;
    payload["type"] = "file";
    payload["messageType"] = static_cast<int>(MessageType::File);
    payload["senderId"] = "970001";
    payload["senderName"] = "QuotaSender";
    payload["receiverId"] = receiverId;
    payload["content"] = QString("发送了文件: %1").arg(fileName);
    payload["fileName"] = fileName;
    payload["fileSize"] = QString::number(declaredFileSize);
    payload["fileHash"] = "offline-attachment-startup-preserve-test";
    payload["chunkSize"] = QString::number(256 * 1024);
    payload["chunkCount"] = QString::number(1);
    payload["offlineFilePath"] = attachmentPath;
    payload["offlineFileStoredOnDisk"] = true;

    QFile queueFile(offlineDirPath + "/" + receiverId + ".jsonl");
    if (!queueFile.open(QIODevice::Append | QIODevice::Text)) return false;
    const QByteArray line = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return queueFile.write(line) == line.size() && queueFile.write("\n") == 1;
}

bool loginRawAndDisconnectOnFileChunk(const QString& account,
                                      const QString& userName,
                                      quint16 port,
                                      int timeoutMs = 10000) {
    QTcpSocket socket;
    QByteArray buffer;
    bool sawLoginSuccess = false;
    bool sawFileChunk = false;

    auto drainSocket = [&]() {
        buffer.append(socket.readAll());
        while (buffer.contains('\n')) {
            const int newlineIndex = buffer.indexOf('\n');
            const QByteArray line = buffer.left(newlineIndex);
            buffer = buffer.mid(newlineIndex + 1);
            if (line.trimmed().isEmpty()) continue;

            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) continue;
            const QString type = doc.object()["type"].toString();
            if (type == "login_success") {
                sawLoginSuccess = true;
            } else if (type == "file_chunk") {
                sawFileChunk = true;
                socket.disconnectFromHost();
                return;
            }
        }
    };
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, drainSocket);

    socket.connectToHost("127.0.0.1", port);
    if (!socket.waitForConnected(timeoutMs)) return false;

    QJsonObject login;
    login["type"] = "login";
    login["mode"] = "login";
    login["account"] = account;
    login["password"] = "secret";
    login["userName"] = userName;
    socket.write(QJsonDocument(login).toJson(QJsonDocument::Compact));
    socket.write("\n");
    socket.flush();

    const bool gotChunk = waitFor([&] {
        drainSocket();
        return sawFileChunk;
    }, timeoutMs);
    if (socket.state() != QAbstractSocket::UnconnectedState) {
        socket.disconnectFromHost();
    }
    socket.waitForDisconnected(1000);
    return gotChunk && sawLoginSuccess;
}

bool loginRawAndRejectFirstFileChunk(const QString& account,
                                     const QString& userName,
                                     quint16 port,
                                     int timeoutMs = 10000) {
    QTcpSocket socket;
    QByteArray buffer;
    bool sawLoginSuccess = false;
    bool sentReject = false;

    auto drainSocket = [&]() {
        buffer.append(socket.readAll());
        while (buffer.contains('\n')) {
            const int newlineIndex = buffer.indexOf('\n');
            const QByteArray line = buffer.left(newlineIndex);
            buffer = buffer.mid(newlineIndex + 1);
            if (line.trimmed().isEmpty()) continue;

            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) continue;
            const QJsonObject obj = doc.object();
            const QString type = obj["type"].toString();
            if (type == "login_success") {
                sawLoginSuccess = true;
            } else if (type == "file_chunk") {
                QJsonObject ack;
                ack["type"] = "file_chunk_ack";
                ack["transferId"] = obj["transferId"].toString();
                ack["chunkIndex"] = obj["chunkIndex"].toString();
                ack["accepted"] = false;
                ack["reason"] = "offline replay reject ack test";
                ack["receivedBytes"] = "0";
                socket.write(QJsonDocument(ack).toJson(QJsonDocument::Compact));
                socket.write("\n");
                socket.flush();
                socket.waitForBytesWritten(1000);
                sentReject = true;
                return;
            }
        }
    };
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, drainSocket);

    socket.connectToHost("127.0.0.1", port);
    if (!socket.waitForConnected(timeoutMs)) return false;

    QJsonObject login;
    login["type"] = "login";
    login["mode"] = "login";
    login["account"] = account;
    login["password"] = "secret";
    login["userName"] = userName;
    socket.write(QJsonDocument(login).toJson(QJsonDocument::Compact));
    socket.write("\n");
    socket.flush();

    const bool rejectedChunk = waitFor([&] {
        drainSocket();
        return sentReject;
    }, timeoutMs);
    waitFor([] { return false; }, 200);
    if (socket.state() != QAbstractSocket::UnconnectedState) {
        socket.disconnectFromHost();
    }
    socket.waitForDisconnected(1000);
    return rejectedChunk && sawLoginSuccess;
}

bool loginRawAckChunksThenDisconnect(const QString& account,
                                     const QString& userName,
                                     quint16 port,
                                     int chunksToAck,
                                     qint64* lastReceivedBytes = nullptr,
                                     int timeoutMs = 10000) {
    QTcpSocket socket;
    QByteArray buffer;
    bool sawLoginSuccess = false;
    int ackedChunks = 0;
    qint64 receivedBytes = 0;

    auto drainSocket = [&]() {
        buffer.append(socket.readAll());
        while (buffer.contains('\n')) {
            const int newlineIndex = buffer.indexOf('\n');
            const QByteArray line = buffer.left(newlineIndex);
            buffer = buffer.mid(newlineIndex + 1);
            if (line.trimmed().isEmpty()) continue;

            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) continue;
            const QJsonObject obj = doc.object();
            const QString type = obj["type"].toString();
            if (type == "login_success") {
                sawLoginSuccess = true;
            } else if (type == "file_chunk" && ackedChunks < chunksToAck) {
                const qint64 chunkIndex = obj["chunkIndex"].toVariant().toLongLong();
                const qint64 chunkSize = obj["chunkSize"].toVariant().toLongLong();
                const qint64 fileSize = obj["fileSize"].toVariant().toLongLong();
                const QByteArray chunkData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());
                receivedBytes = qMin(fileSize, chunkIndex * chunkSize + chunkData.size());

                QJsonObject ack;
                ack["type"] = "file_chunk_ack";
                ack["transferId"] = obj["transferId"].toString();
                ack["chunkIndex"] = obj["chunkIndex"].toString();
                ack["accepted"] = true;
                ack["reason"] = "";
                ack["receivedBytes"] = QString::number(receivedBytes);
                socket.write(QJsonDocument(ack).toJson(QJsonDocument::Compact));
                socket.write("\n");
                socket.flush();
                socket.waitForBytesWritten(1000);
                ++ackedChunks;
                if (ackedChunks >= chunksToAck) {
                    socket.disconnectFromHost();
                    return;
                }
            }
        }
    };
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, drainSocket);

    socket.connectToHost("127.0.0.1", port);
    if (!socket.waitForConnected(timeoutMs)) return false;

    QJsonObject login;
    login["type"] = "login";
    login["mode"] = "login";
    login["account"] = account;
    login["password"] = "secret";
    login["userName"] = userName;
    socket.write(QJsonDocument(login).toJson(QJsonDocument::Compact));
    socket.write("\n");
    socket.flush();

    const bool ackedRequestedChunks = waitFor([&] {
        drainSocket();
        return ackedChunks >= chunksToAck;
    }, timeoutMs);
    if (socket.state() != QAbstractSocket::UnconnectedState) {
        socket.disconnectFromHost();
    }
    socket.waitForDisconnected(1000);
    if (lastReceivedBytes) *lastReceivedBytes = receivedBytes;
    return sawLoginSuccess && ackedRequestedChunks;
}

bool loginRawAckFileReplay(const QString& account,
                           const QString& userName,
                           quint16 port,
                           QVector<qint64>* receivedChunkIndexes = nullptr,
                           qint64* lastReceivedBytes = nullptr,
                           int timeoutMs = 10000) {
    QTcpSocket socket;
    QByteArray buffer;
    bool sawLoginSuccess = false;
    bool completedReplay = false;
    qint64 receivedBytes = 0;

    auto drainSocket = [&]() {
        buffer.append(socket.readAll());
        while (buffer.contains('\n')) {
            const int newlineIndex = buffer.indexOf('\n');
            const QByteArray line = buffer.left(newlineIndex);
            buffer = buffer.mid(newlineIndex + 1);
            if (line.trimmed().isEmpty()) continue;

            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) continue;
            const QJsonObject obj = doc.object();
            const QString type = obj["type"].toString();
            if (type == "login_success") {
                sawLoginSuccess = true;
            } else if (type == "file_chunk") {
                const qint64 chunkIndex = obj["chunkIndex"].toVariant().toLongLong();
                const qint64 chunkSize = obj["chunkSize"].toVariant().toLongLong();
                const qint64 fileSize = obj["fileSize"].toVariant().toLongLong();
                const QByteArray chunkData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());
                receivedBytes = qMin(fileSize, chunkIndex * chunkSize + chunkData.size());
                if (receivedChunkIndexes) {
                    receivedChunkIndexes->append(chunkIndex);
                }

                QJsonObject ack;
                ack["type"] = "file_chunk_ack";
                ack["transferId"] = obj["transferId"].toString();
                ack["chunkIndex"] = obj["chunkIndex"].toString();
                ack["accepted"] = true;
                ack["reason"] = "";
                ack["receivedBytes"] = QString::number(receivedBytes);
                socket.write(QJsonDocument(ack).toJson(QJsonDocument::Compact));
                socket.write("\n");
                socket.flush();
                socket.waitForBytesWritten(1000);

                if (receivedBytes >= fileSize) {
                    completedReplay = true;
                    socket.disconnectFromHost();
                    return;
                }
            }
        }
    };
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, drainSocket);

    socket.connectToHost("127.0.0.1", port);
    if (!socket.waitForConnected(timeoutMs)) return false;

    QJsonObject login;
    login["type"] = "login";
    login["mode"] = "login";
    login["account"] = account;
    login["password"] = "secret";
    login["userName"] = userName;
    socket.write(QJsonDocument(login).toJson(QJsonDocument::Compact));
    socket.write("\n");
    socket.flush();

    const bool ackedReplay = waitFor([&] {
        drainSocket();
        return completedReplay;
    }, timeoutMs);
    if (socket.state() != QAbstractSocket::UnconnectedState) {
        socket.disconnectFromHost();
    }
    socket.waitForDisconnected(1000);
    if (lastReceivedBytes) *lastReceivedBytes = receivedBytes;
    return sawLoginSuccess && ackedReplay;
}

bool setOfflineQueueRejectTrigger(const QString& appDataDir, bool enabled) {
    const QString dbPath = appDataDir + "/accounts.sqlite3";
    if (!QFile::exists(dbPath)) return false;

    bool ok = false;
    const QString connectionName = "offline_rollback_trigger_" + QString::number(QCoreApplication::applicationPid());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            ok = query.exec("DROP TRIGGER IF EXISTS offline_queue_reject_for_test");
            if (ok && enabled) {
                ok = query.exec("CREATE TRIGGER offline_queue_reject_for_test "
                                "BEFORE INSERT ON offline_messages "
                                "BEGIN "
                                "SELECT RAISE(ABORT, 'offline queue rollback test'); "
                                "END");
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
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

    const QString referencedDirPath = appDataDir + "/offline_files/referenced";
    ok = expect(QDir().mkpath(referencedDirPath),
                "referenced attachment directory should be created before server startup") && ok;
    const QString referencedPath = referencedDirPath + "/payload.bin";
    QFile referencedFile(referencedPath);
    ok = expect(referencedFile.open(QIODevice::WriteOnly),
                "referenced attachment file should be writable before server startup") && ok;
    if (ok) {
        ok = expect(referencedFile.write(QByteArray("referenced-payload")) == 18,
                    "referenced attachment file should contain the test payload") && ok;
        referencedFile.close();
    }
    ok = expect(insertLegacyJsonlOfflineAttachmentQueue(appDataDir,
                                                        "970099",
                                                        "referenced-startup.bin",
                                                        referencedPath,
                                                        18),
                "legacy offline queue row should reference the startup attachment") && ok;
    ok = expect(QFile::exists(referencedPath),
                "referenced attachment should exist before server startup cleanup") && ok;

    const QString expiredReceiverId = "970098";
    const QString expiredDirPath = appDataDir + "/offline_files/expired";
    ok = expect(QDir().mkpath(expiredDirPath),
                "expired referenced attachment directory should be created before server startup") && ok;
    const QString expiredPath = expiredDirPath + "/payload.bin";
    QFile expiredFile(expiredPath);
    ok = expect(expiredFile.open(QIODevice::WriteOnly),
                "expired referenced attachment file should be writable before server startup") && ok;
    if (ok) {
        ok = expect(expiredFile.write(QByteArray("expired-payload")) == 15,
                    "expired referenced attachment file should contain the test payload") && ok;
        expiredFile.close();
    }
    QFile expiredTimeFile(expiredPath);
    ok = expect(expiredTimeFile.open(QIODevice::ReadWrite),
                "expired referenced attachment file should reopen for timestamp update") && ok;
    if (ok) {
        ok = expect(expiredTimeFile.setFileTime(QDateTime::currentDateTime().addDays(-15),
                                                QFileDevice::FileModificationTime),
                    "expired referenced attachment modification time should be set before startup") && ok;
        expiredTimeFile.close();
    }
    ok = expect(insertLegacyJsonlOfflineAttachmentQueue(appDataDir,
                                                        expiredReceiverId,
                                                        "expired-startup.bin",
                                                        expiredPath,
                                                        15),
                "legacy offline queue row should reference the expired startup attachment") && ok;
    ok = expect(QFile::exists(expiredPath),
                "expired referenced attachment should exist before server startup cleanup") && ok;
    if (!ok) return 1;

    Server server;
    ok = expect(server.start(port), "server should start on the test port") && ok;
    if (!ok) return 1;
    ok = expect(!QFile::exists(orphanPath),
                "server startup should remove unreferenced offline attachment files") && ok;
    ok = expect(QFile::exists(referencedPath),
                "server startup should preserve offline attachments still referenced by a queue") && ok;
    ok = expect(!QFile::exists(expiredPath),
                "server startup should remove expired offline attachments even if a queue references them") && ok;
    QFile::remove(appDataDir + "/offline/970099.jsonl");
    QFile::remove(referencedPath);

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

    Client expiredReceiver;
    QVector<Message> expiredReplayMessages;
    bool expiredReceiverDisconnected = false;
    QObject::connect(&server, &Server::clientDisconnected, &app, [&](const QString& userId) {
        if (userId == expiredReceiverId) expiredReceiverDisconnected = true;
    });
    QObject::connect(&expiredReceiver, &Client::newMessage, &app, [&](const Message& msg) {
        expiredReplayMessages.append(msg);
    });
    ok = expect(loginClient(expiredReceiver, expiredReceiverId, "ExpiredReceiver", port, true),
                "expired attachment receiver should log in for missing attachment replay") && ok;
    ok = expect(waitFor([&] {
        for (const Message& msg : expiredReplayMessages) {
            if (msg.type == MessageType::System
                && msg.content.contains(QString::fromUtf8("离线文件已丢失"))
                && msg.content.contains(QStringLiteral("expired-startup.bin"))) {
                return true;
            }
        }
        return false;
    }, 3000), "expired referenced attachment should produce a missing-file system notice") && ok;
    ok = expect(waitFor([&] { return !QFile::exists(appDataDir + "/offline/" + expiredReceiverId + ".jsonl"); }, 3000),
                "expired referenced attachment queue row should be cleared after notice") && ok;

    expiredReceiver.disconnectFromServer();
    ok = expect(waitFor([&] { return expiredReceiverDisconnected; }),
                "server should observe expired attachment receiver disconnect after cleanup") && ok;

    const QString interruptedReceiverId = "970007";
    Client interruptedReceiverSeed;
    bool interruptedReceiverDisconnected = false;
    QObject::connect(&server, &Server::clientDisconnected, &app, [&](const QString& userId) {
        if (userId == interruptedReceiverId) interruptedReceiverDisconnected = true;
    });
    ok = expect(loginClient(interruptedReceiverSeed, interruptedReceiverId, "InterruptedReceiver", port, true),
                "interrupted receiver should register before interrupted offline replay") && ok;
    interruptedReceiverSeed.disconnectFromServer();
    ok = expect(waitFor([&] { return interruptedReceiverDisconnected; }),
                "server should observe interrupted receiver disconnect before queue seeding") && ok;

    const QString interruptedFileName = "interrupted-offline-attachment.bin";
    const QString interruptedDirPath = appDataDir + "/offline_files/interrupted";
    ok = expect(QDir().mkpath(interruptedDirPath),
                "interrupted attachment directory should be created") && ok;
    const QString interruptedPath = interruptedDirPath + "/payload.bin";
    const QByteArray interruptedPayload("interrupted-payload");
    QFile interruptedFile(interruptedPath);
    ok = expect(interruptedFile.open(QIODevice::WriteOnly),
                "interrupted attachment file should be writable") && ok;
    if (ok) {
        ok = expect(interruptedFile.write(interruptedPayload) == interruptedPayload.size(),
                    "interrupted attachment file should contain the test payload") && ok;
        interruptedFile.close();
    }
    ok = expect(insertOfflineAttachmentQueue(appDataDir,
                                             interruptedReceiverId,
                                             interruptedFileName,
                                             interruptedPath,
                                             interruptedPayload.size()),
                "interrupted attachment offline queue row should be inserted") && ok;
    ok = expect(offlineQueueCount(appDataDir, interruptedReceiverId) == 1,
                "interrupted attachment offline row should be queued before replay") && ok;
    ok = expect(QFile::exists(interruptedPath),
                "interrupted attachment should exist before replay interruption") && ok;

    interruptedReceiverDisconnected = false;
    ok = expect(loginRawAndDisconnectOnFileChunk(interruptedReceiverId,
                                                 "InterruptedReceiver",
                                                 port),
                "raw receiver should disconnect after receiving the first offline file chunk") && ok;
    ok = expect(waitFor([&] { return interruptedReceiverDisconnected; }, 3000),
                "server should observe raw receiver disconnect after interrupted replay") && ok;
    ok = expect(offlineQueueCount(appDataDir, interruptedReceiverId) == 1,
                "interrupted offline attachment queue row should be retained for retry") && ok;
    ok = expect(QFile::exists(interruptedPath),
                "interrupted offline attachment file should be retained for retry") && ok;

    Client interruptedRetryReceiver;
    QVector<Message> interruptedRetryMessages;
    QObject::connect(&interruptedRetryReceiver, &Client::newMessage, &app, [&](const Message& msg) {
        interruptedRetryMessages.append(msg);
    });
    ok = expect(loginClient(interruptedRetryReceiver, interruptedReceiverId, "InterruptedReceiver", port, false),
                "interrupted receiver should log in again to retry offline attachment replay") && ok;
    ok = expect(waitFor([&] {
        for (const Message& msg : interruptedRetryMessages) {
            if (msg.type == MessageType::File
                && msg.fileName == interruptedFileName
                && msg.fileData == interruptedPayload) {
                return true;
            }
        }
        return false;
    }, 7000), "interrupted offline attachment retry should deliver the original payload") && ok;
    ok = expect(waitFor([&] {
        return offlineQueueCount(appDataDir, interruptedReceiverId) == 0
            && !QFile::exists(interruptedPath);
    }, 3000), "interrupted offline attachment retry should clear queue and attachment") && ok;

    interruptedReceiverDisconnected = false;
    interruptedRetryReceiver.disconnectFromServer();
    ok = expect(waitFor([&] { return interruptedReceiverDisconnected; }),
                "server should observe interrupted retry receiver disconnect after cleanup") && ok;

    const QString rollbackReceiverId = "970006";
    Client rollbackReceiverSeed;
    bool rollbackReceiverDisconnected = false;
    QObject::connect(&server, &Server::clientDisconnected, &app, [&](const QString& userId) {
        if (userId == rollbackReceiverId) rollbackReceiverDisconnected = true;
    });
    ok = expect(loginClient(rollbackReceiverSeed, rollbackReceiverId, "RollbackReceiver", port, true),
                "rollback receiver should register before queue persistence failure") && ok;
    rollbackReceiverSeed.disconnectFromServer();
    ok = expect(waitFor([&] { return rollbackReceiverDisconnected; }),
                "server should observe rollback receiver disconnect before queue persistence failure") && ok;

    ok = expect(setOfflineQueueRejectTrigger(appDataDir, true),
                "offline queue reject trigger should be installed before rollback test") && ok;
    const QString offlinePath = appDataDir + "/offline";
    QDir offlineDir(offlinePath);
    if (offlineDir.exists()) {
        ok = expect(offlineDir.removeRecursively(),
                    "offline queue directory should be removable before fallback blocking") && ok;
    } else {
        QFile::remove(offlinePath);
    }
    QFile blockedOfflinePath(offlinePath);
    ok = expect(blockedOfflinePath.open(QIODevice::WriteOnly),
                "offline queue path should be blockable as a plain file") && ok;
    if (ok) {
        ok = expect(blockedOfflinePath.write(QByteArray("blocked")) == 7,
                    "offline queue path blocker should contain test payload") && ok;
        blockedOfflinePath.close();
    }
    ok = expect(offlineAttachmentFileCount(appDataDir) == 0,
                "rollback test should start without existing offline attachments") && ok;
    const QString rollbackFilePath = tempDir.filePath("offline-rollback.bin");
    QByteArray rollbackPayload;
    ok = expect(writeSmallFile(rollbackFilePath, &rollbackPayload),
                "small rollback test file should be created") && ok;
    if (!ok) return 1;

    ok = expect(sender.sendFile(rollbackFilePath, rollbackReceiverId),
                "sender should finish uploading the rollback test file to the server") && ok;
    waitFor([] { return false; }, 500);
    ok = expect(offlineAttachmentFileCount(appDataDir) == 0,
                "queue persistence failure should roll back the just-saved offline attachment") && ok;
    ok = expect(offlineQueueCount(appDataDir, rollbackReceiverId) == 0,
                "queue persistence failure should not leave an offline queue row") && ok;
    ok = expect(QFileInfo(offlinePath).isFile(),
                "rollback test should keep JSONL fallback blocked until assertions complete") && ok;
    ok = expect(setOfflineQueueRejectTrigger(appDataDir, false),
                "offline queue reject trigger should be removed after rollback test") && ok;
    QFile::remove(offlinePath);

    const QString rejectedAckReceiverId = "970008";
    Client rejectedAckReceiverSeed;
    bool rejectedAckReceiverDisconnected = false;
    QObject::connect(&server, &Server::clientDisconnected, &app, [&](const QString& userId) {
        if (userId == rejectedAckReceiverId) rejectedAckReceiverDisconnected = true;
    });
    ok = expect(loginClient(rejectedAckReceiverSeed, rejectedAckReceiverId, "RejectedAckReceiver", port, true),
                "rejected-ack receiver should register before offline replay") && ok;
    rejectedAckReceiverSeed.disconnectFromServer();
    ok = expect(waitFor([&] { return rejectedAckReceiverDisconnected; }),
                "server should observe rejected-ack receiver disconnect before queue seeding") && ok;

    const QString rejectedAckFileName = "rejected-ack-offline-attachment.bin";
    const QString rejectedAckDirPath = appDataDir + "/offline_files/rejected_ack";
    ok = expect(QDir().mkpath(rejectedAckDirPath),
                "rejected-ack attachment directory should be created") && ok;
    const QString rejectedAckPath = rejectedAckDirPath + "/payload.bin";
    const QByteArray rejectedAckPayload("rejected-ack-payload");
    QFile rejectedAckFile(rejectedAckPath);
    ok = expect(rejectedAckFile.open(QIODevice::WriteOnly),
                "rejected-ack attachment file should be writable") && ok;
    if (ok) {
        ok = expect(rejectedAckFile.write(rejectedAckPayload) == rejectedAckPayload.size(),
                    "rejected-ack attachment file should contain the test payload") && ok;
        rejectedAckFile.close();
    }
    ok = expect(insertOfflineAttachmentQueue(appDataDir,
                                             rejectedAckReceiverId,
                                             rejectedAckFileName,
                                             rejectedAckPath,
                                             rejectedAckPayload.size()),
                "rejected-ack attachment offline queue row should be inserted") && ok;
    ok = expect(offlineQueueCount(appDataDir, rejectedAckReceiverId) == 1,
                "rejected-ack attachment offline row should be queued before replay") && ok;
    ok = expect(QFile::exists(rejectedAckPath),
                "rejected-ack attachment should exist before replay") && ok;

    rejectedAckReceiverDisconnected = false;
    ok = expect(loginRawAndRejectFirstFileChunk(rejectedAckReceiverId,
                                                "RejectedAckReceiver",
                                                port),
                "raw receiver should reject the first offline file chunk") && ok;
    ok = expect(waitFor([&] { return rejectedAckReceiverDisconnected; }, 3000),
                "server should observe raw rejected-ack receiver disconnect") && ok;
    ok = expect(offlineQueueCount(appDataDir, rejectedAckReceiverId) == 1,
                "rejected offline attachment queue row should be retained for retry") && ok;
    ok = expect(QFile::exists(rejectedAckPath),
                "rejected offline attachment file should be retained for retry") && ok;

    Client rejectedAckRetryReceiver;
    QVector<Message> rejectedAckRetryMessages;
    QObject::connect(&rejectedAckRetryReceiver, &Client::newMessage, &app, [&](const Message& msg) {
        rejectedAckRetryMessages.append(msg);
    });
    ok = expect(loginClient(rejectedAckRetryReceiver, rejectedAckReceiverId, "RejectedAckReceiver", port, false),
                "rejected-ack receiver should log in again to retry offline attachment replay") && ok;
    ok = expect(waitFor([&] {
        for (const Message& msg : rejectedAckRetryMessages) {
            if (msg.type == MessageType::File
                && msg.fileName == rejectedAckFileName
                && msg.fileData == rejectedAckPayload) {
                return true;
            }
        }
        return false;
    }, 7000), "rejected-ack offline attachment retry should deliver the original payload") && ok;
    ok = expect(waitFor([&] {
        return offlineQueueCount(appDataDir, rejectedAckReceiverId) == 0
            && !QFile::exists(rejectedAckPath);
    }, 3000), "rejected-ack offline attachment retry should clear queue and attachment") && ok;

    rejectedAckReceiverDisconnected = false;
    rejectedAckRetryReceiver.disconnectFromServer();
    ok = expect(waitFor([&] { return rejectedAckReceiverDisconnected; }),
                "server should observe rejected-ack retry receiver disconnect after cleanup") && ok;

    const QString partialAckReceiverId = "970009";
    Client partialAckReceiverSeed;
    bool partialAckReceiverDisconnected = false;
    QObject::connect(&server, &Server::clientDisconnected, &app, [&](const QString& userId) {
        if (userId == partialAckReceiverId) partialAckReceiverDisconnected = true;
    });
    ok = expect(loginClient(partialAckReceiverSeed, partialAckReceiverId, "PartialAckReceiver", port, true),
                "partial-ack receiver should register before offline replay") && ok;
    partialAckReceiverSeed.disconnectFromServer();
    ok = expect(waitFor([&] { return partialAckReceiverDisconnected; }),
                "server should observe partial-ack receiver disconnect before queue seeding") && ok;

    const QString partialAckFileName = "partial-ack-offline-attachment.bin";
    const QString partialAckDirPath = appDataDir + "/offline_files/partial_ack";
    ok = expect(QDir().mkpath(partialAckDirPath),
                "partial-ack attachment directory should be created") && ok;
    const QString partialAckPath = partialAckDirPath + "/payload.bin";
    QByteArray partialAckPayload(300 * 1024, Qt::Uninitialized);
    for (int i = 0; i < partialAckPayload.size(); ++i) {
        partialAckPayload[i] = static_cast<char>('A' + (i % 26));
    }
    QFile partialAckFile(partialAckPath);
    ok = expect(partialAckFile.open(QIODevice::WriteOnly),
                "partial-ack attachment file should be writable") && ok;
    if (ok) {
        ok = expect(partialAckFile.write(partialAckPayload) == partialAckPayload.size(),
                    "partial-ack attachment file should contain the test payload") && ok;
        partialAckFile.close();
    }
    ok = expect(insertOfflineAttachmentQueue(appDataDir,
                                             partialAckReceiverId,
                                             partialAckFileName,
                                             partialAckPath,
                                             partialAckPayload.size()),
                "partial-ack attachment offline queue row should be inserted") && ok;
    ok = expect(offlineQueueCount(appDataDir, partialAckReceiverId) == 1,
                "partial-ack attachment offline row should be queued before replay") && ok;

    qint64 partialAckReceivedBytes = 0;
    partialAckReceiverDisconnected = false;
    ok = expect(loginRawAckChunksThenDisconnect(partialAckReceiverId,
                                                "PartialAckReceiver",
                                                port,
                                                1,
                                                &partialAckReceivedBytes),
                "raw receiver should ack the first offline file chunk before disconnecting") && ok;
    ok = expect(waitFor([&] { return partialAckReceiverDisconnected; }, 3000),
                "server should observe raw partial-ack receiver disconnect") && ok;
    ok = expect(partialAckReceivedBytes == 256 * 1024,
                "raw partial-ack helper should report the first confirmed chunk bytes") && ok;
    ok = expect(offlineQueueCount(appDataDir, partialAckReceiverId) == 1,
                "partial-ack offline attachment queue row should be retained for retry") && ok;
    ok = expect(QFile::exists(partialAckPath),
                "partial-ack offline attachment file should be retained for retry") && ok;
    ok = expect(waitFor([&] {
        const QJsonObject payload = offlineQueuePayload(appDataDir, partialAckReceiverId);
        return payload["confirmedBytes"].toVariant().toLongLong() == partialAckReceivedBytes
            && !payload["resumeUpdatedAt"].toString().isEmpty();
    }, 3000), "partial-ack offline attachment queue payload should record confirmed progress") && ok;
    const QJsonObject partialAckQueuePayload = offlineQueuePayload(appDataDir, partialAckReceiverId);
    ok = expect(!QDateTime::fromString(partialAckQueuePayload["resumeUpdatedAt"].toString(), Qt::ISODate).isNull(),
                "partial-ack offline attachment progress timestamp should be ISO formatted") && ok;

    QVector<qint64> partialAckRetryChunkIndexes;
    qint64 partialAckRetryReceivedBytes = 0;
    partialAckReceiverDisconnected = false;
    ok = expect(loginRawAckFileReplay(partialAckReceiverId,
                                      "PartialAckReceiver",
                                      port,
                                      &partialAckRetryChunkIndexes,
                                      &partialAckRetryReceivedBytes),
                "partial-ack receiver should log in again to ack resumed offline attachment replay") && ok;
    ok = expect(partialAckRetryChunkIndexes.size() == 1 && partialAckRetryChunkIndexes.first() == 1,
                "partial-ack offline attachment retry should resume from the first unconfirmed chunk") && ok;
    ok = expect(partialAckRetryReceivedBytes == partialAckPayload.size(),
                "partial-ack offline attachment retry should confirm the full attachment size") && ok;
    ok = expect(waitFor([&] {
        return offlineQueueCount(appDataDir, partialAckReceiverId) == 0
            && !QFile::exists(partialAckPath);
    }, 3000), "partial-ack offline attachment retry should clear queue and attachment") && ok;
    ok = expect(waitFor([&] { return partialAckReceiverDisconnected; }),
                "server should observe partial-ack resumed receiver disconnect after cleanup") && ok;

    auto runInvalidResumeFallbackCase = [&](const QString& receiverId,
                                            const QString& userName,
                                            const QString& caseName,
                                            const std::function<void(QJsonObject*)>& mutatePayload,
                                            const QVector<qint64>& expectedChunkIndexes) {
        Client seedReceiver;
        bool disconnected = false;
        QObject::connect(&server, &Server::clientDisconnected, &app, [&](const QString& userId) {
            if (userId == receiverId) disconnected = true;
        });
        bool caseOk = expect(loginClient(seedReceiver, receiverId, userName, port, true),
                             qPrintable(caseName + " receiver should register before queue seeding"));
        seedReceiver.disconnectFromServer();
        caseOk = expect(waitFor([&] { return disconnected; }),
                        qPrintable(caseName + " receiver should disconnect before queue seeding")) && caseOk;

        const QString fileName = caseName + "-offline-attachment.bin";
        const QString dirPath = appDataDir + "/offline_files/" + caseName;
        caseOk = expect(QDir().mkpath(dirPath),
                        qPrintable(caseName + " attachment directory should be created")) && caseOk;
        const QString attachmentPath = dirPath + "/payload.bin";
        QByteArray payload(300 * 1024, Qt::Uninitialized);
        for (int i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<char>('A' + (i % 26));
        }
        QFile attachment(attachmentPath);
        caseOk = expect(attachment.open(QIODevice::WriteOnly),
                        qPrintable(caseName + " attachment file should be writable")) && caseOk;
        if (caseOk) {
            caseOk = expect(attachment.write(payload) == payload.size(),
                            qPrintable(caseName + " attachment file should contain the test payload")) && caseOk;
            attachment.close();
        }
        caseOk = expect(insertOfflineAttachmentQueue(appDataDir, receiverId, fileName, attachmentPath, payload.size()),
                        qPrintable(caseName + " offline queue row should be inserted")) && caseOk;
        caseOk = expect(updateOfflineQueuePayload(appDataDir, receiverId, mutatePayload),
                        qPrintable(caseName + " offline queue row should be mutated with invalid resume metadata")) && caseOk;

        QVector<qint64> chunkIndexes;
        qint64 replayReceivedBytes = 0;
        disconnected = false;
        caseOk = expect(loginRawAckFileReplay(receiverId, userName, port, &chunkIndexes, &replayReceivedBytes),
                        qPrintable(caseName + " receiver should ack fallback offline replay")) && caseOk;
        caseOk = expect(chunkIndexes == expectedChunkIndexes,
                        qPrintable(caseName + " invalid resume metadata should fall back to chunk 0 replay")) && caseOk;
        caseOk = expect(replayReceivedBytes == payload.size(),
                        qPrintable(caseName + " fallback replay should confirm the full attachment size")) && caseOk;
        caseOk = expect(waitFor([&] {
            return offlineQueueCount(appDataDir, receiverId) == 0
                && !QFile::exists(attachmentPath);
        }, 3000), qPrintable(caseName + " fallback replay should clear queue and attachment")) && caseOk;
        caseOk = expect(waitFor([&] { return disconnected; }),
                        qPrintable(caseName + " receiver disconnect should be observed after fallback replay")) && caseOk;
        return caseOk;
    };

    ok = runInvalidResumeFallbackCase("970010",
                                      "BadBoundaryReceiver",
                                      "bad-boundary-resume",
                                      [](QJsonObject* payload) {
                                          (*payload)["confirmedBytes"] = QString::number(128 * 1024);
                                          (*payload)["resumeUpdatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
                                      },
                                      QVector<qint64>{0, 1}) && ok;
    ok = runInvalidResumeFallbackCase("970011",
                                      "TooLargeProgressReceiver",
                                      "too-large-resume",
                                      [](QJsonObject* payload) {
                                          (*payload)["confirmedBytes"] = QString::number(512 * 1024);
                                          (*payload)["resumeUpdatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
                                      },
                                      QVector<qint64>{0, 1}) && ok;
    ok = runInvalidResumeFallbackCase("970012",
                                      "ChunkCountMismatchReceiver",
                                      "chunk-count-mismatch-resume",
                                      [](QJsonObject* payload) {
                                          (*payload)["confirmedBytes"] = QString::number(256 * 1024);
                                          (*payload)["chunkCount"] = QString::number(99);
                                          (*payload)["resumeUpdatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
                                      },
                                      QVector<qint64>{0, 1}) && ok;

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
