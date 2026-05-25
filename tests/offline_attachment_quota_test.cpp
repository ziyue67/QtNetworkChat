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

    Server server;
    ok = expect(server.start(port), "server should start on the test port") && ok;
    if (!ok) return 1;

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

    replayReceiver.disconnectFromServer();

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

    cleanupReceiver.disconnectFromServer();
    sender.disconnectFromServer();
    server.stop();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    qunsetenv("QTNETWORKCHAT_OFFLINE_ATTACHMENT_QUOTA_MB");
    return ok ? 0 : 1;
}
