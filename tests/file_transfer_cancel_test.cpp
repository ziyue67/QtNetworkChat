#include "client.h"
#include "message.h"
#include "server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

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

bool registerClient(Client& client,
                    const QString& account,
                    const QString& userName,
                    quint16 port) {
    client.setUserInfo(account, userName);
    client.setAccountInfo(account, "secret", true);
    if (!client.connectToServer("127.0.0.1", port)) return false;
    return client.waitForLoginResult(5000);
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
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("file_transfer_cancel_test");
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
    QObject::connect(&sender, &Client::fileTransferPrepared, &app, [&](const QString&, qint64, qint64, qint64, const QString&) {
        prepared = true;
    });
    QObject::connect(&sender, &Client::fileTransferProgress, &app, [&](const QString&, qint64 bytesPrepared, qint64 totalBytes) {
        if (prepared && !cancelRequested && bytesPrepared > 0 && bytesPrepared < totalBytes) {
            cancelRequested = true;
            sender.cancelCurrentOutgoingTransfer();
        }
    });

    const bool sent = sender.sendFile(filePath);
    ok = expect(cancelRequested, "sender should request cancellation after the first uploaded chunk") && ok;
    ok = expect(!sent, "canceled file transfer should not report success") && ok;
    ok = expect(waitFor([&] {
        for (const QString& message : senderSystemMessages) {
            if (message.contains(QString::fromUtf8("服务端已清理未完成分片"))) return true;
        }
        return false;
    }), "sender should receive server cleanup confirmation") && ok;

    sender.disconnectFromServer();
    server.stop();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
