#include "client.h"

#include <QCoreApplication>
#include <QDebug>
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

bool writeSmallFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray data("retry-progress-payload");
    return file.write(data) == data.size();
}

void writeJson(QTcpSocket* socket, const QJsonObject& obj) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
    socket->write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
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

class RetryAckServer : public QObject {
    Q_OBJECT

public:
    bool start() {
        connect(&m_server, &QTcpServer::newConnection, this, &RetryAckServer::onNewConnection);
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const { return m_server.serverPort(); }
    int chunkAttempts() const { return m_chunkAttempts; }
    qint64 acknowledgedBytes() const { return m_acknowledgedBytes; }

private slots:
    void onNewConnection() {
        QTcpSocket* socket = m_server.nextPendingConnection();
        if (!socket) return;
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            m_buffer.append(socket->readAll());
            const QVector<QJsonObject> messages = takeJsonLines(m_buffer);
            for (const QJsonObject& message : messages) {
                handleMessage(socket, message);
            }
        });
    }

private:
    void handleMessage(QTcpSocket* socket, const QJsonObject& message) {
        const QString type = message["type"].toString();
        if (type == "login") {
            QJsonObject response;
            response["type"] = "login_success";
            response["userId"] = message["account"].toString("950001");
            response["userName"] = message["userName"].toString("RetrySender");
            response["registered"] = message["mode"].toString() == "register";
            writeJson(socket, response);
            return;
        }

        if (type != "file_chunk") return;

        ++m_chunkAttempts;
        m_acknowledgedBytes = QByteArray::fromBase64(message["fileData"].toString().toLatin1()).size();
        if (m_chunkAttempts == 1) {
            return;
        }

        QJsonObject ack;
        ack["type"] = "file_chunk_ack";
        ack["transferId"] = message["transferId"].toString();
        ack["chunkIndex"] = message["chunkIndex"].toString();
        ack["accepted"] = true;
        ack["reason"] = "";
        ack["receivedBytes"] = QString::number(m_acknowledgedBytes);
        writeJson(socket, ack);
    }

    QTcpServer m_server;
    QByteArray m_buffer;
    int m_chunkAttempts = 0;
    qint64 m_acknowledgedBytes = 0;
};
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("file_chunk_retry_progress_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    bool ok = true;
    RetryAckServer server;
    ok = expect(server.start(), "retry ack server should start") && ok;
    if (!ok) return 1;

    Client sender;
    sender.setUserInfo("950001", "RetrySender");
    sender.setAccountInfo("950001", "secret", true);
    ok = expect(sender.connectToServer("127.0.0.1", server.port()),
                "sender should connect to retry ack server") && ok;
    ok = expect(sender.waitForLoginResult(5000),
                "sender should log in to retry ack server") && ok;

    QTemporaryDir tempDir;
    ok = expect(tempDir.isValid(), "temporary directory should be available") && ok;
    const QString filePath = tempDir.filePath("retry-progress.bin");
    ok = expect(writeSmallFile(filePath), "small retry test file should be created") && ok;
    if (!ok) return 1;

    QVector<qint64> progressValues;
    QObject::connect(&sender, &Client::fileTransferProgress, &app, [&](const QString&, qint64 bytesPrepared, qint64) {
        progressValues.append(bytesPrepared);
    });

    ok = expect(sender.sendFile(filePath), "sender should succeed after retrying the unacked chunk") && ok;
    ok = expect(server.chunkAttempts() == 2, "sender should retry the same chunk after the first ack is lost") && ok;
    ok = expect(!progressValues.isEmpty(), "sender should emit transfer progress") && ok;
    ok = expect(progressValues.last() == server.acknowledgedBytes(),
                "sender progress should use the acked received byte count after retry") && ok;

    sender.disconnectFromServer();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}

#include "file_chunk_retry_progress_test.moc"
