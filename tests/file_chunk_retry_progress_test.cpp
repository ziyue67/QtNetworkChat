#include "client.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

#include <functional>

namespace {
constexpr qint64 kClientChunkBytes = 256LL * 1024;
const char kResumeTransferId[] = "resume-send-transfer";

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

bool writeResumeFile(const QString& filePath, qint64* fileSize) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    const QByteArray firstChunk(static_cast<int>(kClientChunkBytes), 'a');
    const QByteArray secondChunk(static_cast<int>(kClientChunkBytes), 'b');
    const QByteArray finalChunk("resume-tail-payload");
    const qint64 expectedSize = firstChunk.size() + secondChunk.size() + finalChunk.size();
    if (file.write(firstChunk) != firstChunk.size()) return false;
    if (file.write(secondChunk) != secondChunk.size()) return false;
    if (file.write(finalChunk) != finalChunk.size()) return false;
    if (fileSize) *fileSize = expectedSize;
    return true;
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
    int resumeQueries() const { return m_resumeQueries; }
    qint64 acknowledgedBytes() const { return m_acknowledgedBytes; }
    QVector<qint64> resumedChunkIndexes() const { return m_resumedChunkIndexes; }
    qint64 resumedAcknowledgedBytes() const { return m_resumedAcknowledgedBytes; }

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

        if (type == "file_transfer_resume_query") {
            ++m_resumeQueries;
            QJsonArray receivedChunks;
            receivedChunks.append(QString::number(0));
            receivedChunks.append(QString::number(1));

            QJsonObject response;
            response["type"] = "file_transfer_resume_state";
            response["transferId"] = message["transferId"].toString();
            response["canResume"] = true;
            response["confirmedBytes"] = QString::number(8);
            response["nextChunkIndex"] = QString::number(2);
            response["receivedChunks"] = receivedChunks;
            response["reason"] = "";
            writeJson(socket, response);
            return;
        }

        if (type != "file_chunk") return;

        const QString transferId = message["transferId"].toString();
        const qint64 chunkIndex = message["chunkIndex"].toVariant().toLongLong();
        const qint64 chunkSize = message["chunkSize"].toVariant().toLongLong();
        const qint64 fileSize = message["fileSize"].toVariant().toLongLong();
        const QByteArray chunkData = QByteArray::fromBase64(message["fileData"].toString().toLatin1());
        const qint64 receivedBytes = qMin(fileSize, chunkIndex * chunkSize + chunkData.size());

        if (transferId == QString::fromLatin1(kResumeTransferId)) {
            m_resumedChunkIndexes.append(chunkIndex);
            m_resumedAcknowledgedBytes = receivedBytes;
            QJsonObject ack;
            ack["type"] = "file_chunk_ack";
            ack["transferId"] = transferId;
            ack["chunkIndex"] = message["chunkIndex"].toString();
            ack["accepted"] = true;
            ack["reason"] = "";
            ack["receivedBytes"] = QString::number(receivedBytes);
            writeJson(socket, ack);
            return;
        }

        ++m_chunkAttempts;
        m_acknowledgedBytes = receivedBytes;
        if (m_chunkAttempts == 1) {
            return;
        }

        QJsonObject ack;
        ack["type"] = "file_chunk_ack";
        ack["transferId"] = transferId;
        ack["chunkIndex"] = message["chunkIndex"].toString();
        ack["accepted"] = true;
        ack["reason"] = "";
        ack["receivedBytes"] = QString::number(receivedBytes);
        writeJson(socket, ack);
    }

    QTcpServer m_server;
    QByteArray m_buffer;
    int m_chunkAttempts = 0;
    int m_resumeQueries = 0;
    qint64 m_acknowledgedBytes = 0;
    QVector<qint64> m_resumedChunkIndexes;
    qint64 m_resumedAcknowledgedBytes = 0;
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

    qint64 confirmedBytes = 0;
    qint64 nextChunkIndex = 0;
    QVector<qint64> receivedChunks;
    QString resumeReason;
    ok = expect(sender.queryFileTransferResumeState("resume-transfer",
                                                    &confirmedBytes,
                                                    &nextChunkIndex,
                                                    &receivedChunks,
                                                    &resumeReason,
                                                    5000),
                "sender should query and parse resume state") && ok;
    ok = expect(server.resumeQueries() == 1, "fake server should receive exactly one resume query") && ok;
    ok = expect(confirmedBytes == 8, "client should expose confirmed resume bytes") && ok;
    ok = expect(nextChunkIndex == 2, "client should expose next resume chunk index") && ok;
    ok = expect(receivedChunks.size() == 2 && receivedChunks[0] == 0 && receivedChunks[1] == 1,
                "client should expose received resume chunk indexes") && ok;
    ok = expect(resumeReason.isEmpty(), "accepted resume state should not expose a reject reason") && ok;

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

    qint64 resumeFileSize = 0;
    const QString resumeFilePath = tempDir.filePath("resume-send.bin");
    ok = expect(writeResumeFile(resumeFilePath, &resumeFileSize),
                "multi-chunk resume test file should be created") && ok;
    ok = expect(!sender.resumeFileTransfer(resumeFilePath,
                                           "invalid-resume-transfer",
                                           8,
                                           2),
                "sender should reject a resume position ahead of confirmed bytes") && ok;
    ok = expect(sender.resumeFileTransfer(resumeFilePath,
                                          QString::fromLatin1(kResumeTransferId),
                                          2 * kClientChunkBytes,
                                          2),
                "sender should resume from the next missing chunk") && ok;
    const QVector<qint64> resumedChunks = server.resumedChunkIndexes();
    ok = expect(resumedChunks.size() == 1 && resumedChunks.first() == 2,
                "sender should not resend already confirmed chunks during resume") && ok;
    ok = expect(server.resumedAcknowledgedBytes() == resumeFileSize,
                "resumed send should finish with the server-reported file size") && ok;
    const qint64 resumeChunkCount = (resumeFileSize + kClientChunkBytes - 1) / kClientChunkBytes;
    ok = expect(sender.resumeFileTransfer(resumeFilePath,
                                          "already-complete-transfer",
                                          resumeFileSize,
                                          resumeChunkCount),
                "sender should accept an already complete resume state without sending chunks") && ok;

    sender.disconnectFromServer();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}

#include "file_chunk_retry_progress_test.moc"
