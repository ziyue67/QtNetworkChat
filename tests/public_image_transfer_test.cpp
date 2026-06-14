#include "client.h"
#include "message.h"
#include "server.h"
#include "test_redis_support.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

#include <functional>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning("%s", message);
        return false;
    }
    return true;
}

bool waitFor(const std::function<bool()>& predicate, int timeoutMs = 8000) {
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

bool registerClient(Client& client, const QString& account, const QString& name, quint16 port) {
    client.setUserInfo(account, name);
    client.setAccountInfo(account, QStringLiteral("secret"), true);
    if (!client.connectToServer(QStringLiteral("127.0.0.1"), port)) return false;
    return client.waitForLoginResult(5000);
}

bool writeImageLikeFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QByteArray payload;
    payload.resize(346 * 1024);
    for (int i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>((i * 37 + 11) & 0xff);
    }
    return file.write(payload) == payload.size();
}

bool loginRawReceiverWithoutChunkAck(quint16 port, const QString& account, const QString& name, QTcpSocket* socket) {
    if (!socket) return false;
    socket->connectToHost(QHostAddress::LocalHost, port);
    if (!socket->waitForConnected(3000)) return false;

    QJsonObject login;
    login["type"] = "login";
    login["mode"] = "register";
    login["account"] = account;
    login["password"] = "secret";
    login["userName"] = name;
    const QByteArray line = QJsonDocument(login).toJson(QJsonDocument::Compact) + '\n';
    if (socket->write(line) != line.size() || !socket->waitForBytesWritten(3000)) {
        return false;
    }

    QByteArray buffer;
    return waitFor([&] {
        buffer.append(socket->readAll());
        return buffer.contains(QByteArrayLiteral("\"type\":\"login_success\""));
    }, 5000);
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QtNetworkChatTests"));
    QCoreApplication::setApplicationName(QStringLiteral("public_image_transfer_test"));
    QStandardPaths::setTestModeEnabled(true);

    const quint16 port = freeLocalPort();
    bool ok = true;
    ok = expect(port != 0, "a local test port should be available") && ok;
    if (!ok) return 1;

    TestRedisServerEnvironment redis(QStringLiteral("qtchat-public-image-transfer-test"));
    QString redisError;
    ok = expect(redis.start(&redisError), "fake Redis should start for public image transfer test") && ok;
    if (!ok) return 1;
    redis.applyEnvironment();

    Server server;
    ok = expect(server.start(port), "server should start on the test port") && ok;
    if (!ok) return 1;

    Client sender;
    Client receiver;
    QVector<Message> receiverMessages;
    QVector<Message> senderSystemMessages;
    QObject::connect(&receiver, &Client::newMessage, &app, [&](const Message& msg) {
        receiverMessages.append(msg);
    });
    QObject::connect(&sender, &Client::newMessage, &app, [&](const Message& msg) {
        if (msg.type == MessageType::System) {
            senderSystemMessages.append(msg);
        }
    });

    ok = expect(registerClient(sender, QStringLiteral("public-image-sender"), QStringLiteral("PublicImageSender"), port),
                "sender should register and log in") && ok;
    ok = expect(registerClient(receiver, QStringLiteral("public-image-receiver"), QStringLiteral("PublicImageReceiver"), port),
                "receiver should register and log in") && ok;
    QTcpSocket stalledReceiver;
    ok = expect(loginRawReceiverWithoutChunkAck(port,
                                                QStringLiteral("public-image-stalled"),
                                                QStringLiteral("PublicImageStalled"),
                                                &stalledReceiver),
                "stalled raw receiver should log in but not acknowledge file chunks") && ok;

    QTemporaryDir tempDir;
    ok = expect(tempDir.isValid(), "temporary directory should be available") && ok;
    const QString imagePath = tempDir.filePath(QStringLiteral("public-image-transfer.png"));
    ok = expect(writeImageLikeFile(imagePath), "public image test file should be written") && ok;
    if (!ok) return 1;

    ok = expect(sender.sendImage(imagePath), "sender should complete public image upload") && ok;
    ok = expect(waitFor([&] {
        for (const Message& msg : receiverMessages) {
            if (msg.type == MessageType::Image
                && msg.fileName == QFileInfo(imagePath).fileName()
                && msg.fileData.size() == QFileInfo(imagePath).size()) {
                return true;
            }
        }
        return false;
    }), "receiver should receive the public image payload") && ok;
    ok = expect(std::none_of(senderSystemMessages.cbegin(),
                             senderSystemMessages.cend(),
                             [](const Message& msg) { return msg.content.contains(QStringLiteral("文件分片")); }),
                "sender should not receive a file chunk failure notice") && ok;

    sender.disconnectFromServer();
    receiver.disconnectFromServer();
    stalledReceiver.disconnectFromHost();
    server.stop();
    redis.stop();
    return ok ? 0 : 1;
}
