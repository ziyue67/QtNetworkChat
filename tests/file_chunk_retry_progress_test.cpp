#include "client.h"

#include <QCoreApplication>
#include <QCryptographicHash>
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
const char kQueryAndResumeTransferId[] = "query-and-resume-transfer";
const char kMismatchResumeTransferId[] = "mismatch-resume-transfer";
const char kGapResumeTransferId[] = "gap-resume-transfer";
const char kAckTimeoutAutoResumeFileName[] = "ack-timeout-auto-resume.bin";
const char kAckTimeoutGapResumeFileName[] = "ack-timeout-gap-resume.bin";

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

QString fileSha256(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hasher.addData(file.read(kClientChunkBytes));
    }
    return QString::fromLatin1(hasher.result().toHex());
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
    QVector<qint64> autoResumeChunkIndexes() const { return m_autoResumeChunkIndexes; }
    int autoResumeQueries() const { return m_autoResumeQueries; }
    QVector<qint64> gapAutoResumeChunkIndexes() const { return m_gapAutoResumeChunkIndexes; }
    int gapAutoResumeQueries() const { return m_gapAutoResumeQueries; }
    void setResumeMetadata(qint64 fileSize, const QString& fileHash) {
        m_resumeFileSize = fileSize;
        m_resumeFileHash = fileHash;
    }

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

            const QString transferId = message["transferId"].toString();
            if (!m_gapAutoResumeTransferId.isEmpty() && transferId == m_gapAutoResumeTransferId) {
                ++m_gapAutoResumeQueries;

                QJsonObject response;
                response["type"] = "file_transfer_resume_state";
                response["transferId"] = transferId;
                response["canResume"] = true;
                response["confirmedBytes"] = QString::number(kClientChunkBytes);
                response["nextChunkIndex"] = QString::number(1);
                response["fileSize"] = QString::number(m_gapAutoResumeFileSize);
                response["chunkSize"] = QString::number(m_gapAutoResumeChunkSize);
                response["chunkCount"] = QString::number(m_gapAutoResumeChunkCount);
                response["fileHash"] = m_gapAutoResumeFileHash;
                response["receivedChunks"] = QJsonArray();
                response["reason"] = "";
                writeJson(socket, response);
                return;
            }

            if (!m_autoResumeTransferId.isEmpty() && transferId == m_autoResumeTransferId) {
                ++m_autoResumeQueries;
                QJsonArray receivedChunks;
                receivedChunks.append(QString::number(0));

                QJsonObject response;
                response["type"] = "file_transfer_resume_state";
                response["transferId"] = transferId;
                response["canResume"] = true;
                response["confirmedBytes"] = QString::number(kClientChunkBytes);
                response["nextChunkIndex"] = QString::number(1);
                response["fileSize"] = QString::number(m_autoResumeFileSize);
                response["chunkSize"] = QString::number(m_autoResumeChunkSize);
                response["chunkCount"] = QString::number(m_autoResumeChunkCount);
                response["fileHash"] = m_autoResumeFileHash;
                response["receivedChunks"] = receivedChunks;
                response["reason"] = "";
                writeJson(socket, response);
                return;
            }

            if (transferId == QString::fromLatin1(kQueryAndResumeTransferId)
                || transferId == QString::fromLatin1(kMismatchResumeTransferId)
                || transferId == QString::fromLatin1(kGapResumeTransferId)) {
                QJsonArray resumeChunks;
                resumeChunks.append(QString::number(0));
                if (transferId != QString::fromLatin1(kGapResumeTransferId)) {
                    resumeChunks.append(QString::number(1));
                }
                QJsonObject response;
                response["type"] = "file_transfer_resume_state";
                response["transferId"] = transferId;
                response["canResume"] = true;
                response["confirmedBytes"] = QString::number(2 * kClientChunkBytes);
                response["nextChunkIndex"] = QString::number(2);
                response["fileSize"] = QString::number(m_resumeFileSize);
                response["chunkSize"] = QString::number(kClientChunkBytes);
                response["chunkCount"] = QString::number((m_resumeFileSize + kClientChunkBytes - 1) / kClientChunkBytes);
                response["fileHash"] = transferId == QString::fromLatin1(kMismatchResumeTransferId)
                    ? QString::fromLatin1("not-the-same-hash")
                    : m_resumeFileHash;
                response["receivedChunks"] = resumeChunks;
                response["reason"] = "";
                writeJson(socket, response);
                return;
            }

            QJsonObject response;
            response["type"] = "file_transfer_resume_state";
            response["transferId"] = transferId;
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

        if (message["fileName"].toString() == QString::fromLatin1(kAckTimeoutGapResumeFileName)) {
            if (m_gapAutoResumeTransferId.isEmpty() && chunkIndex == 0) {
                m_gapAutoResumeTransferId = transferId;
                m_gapAutoResumeFileSize = fileSize;
                m_gapAutoResumeChunkSize = chunkSize;
                m_gapAutoResumeChunkCount = message["chunkCount"].toVariant().toLongLong();
                m_gapAutoResumeFileHash = message["fileHash"].toString();
                return;
            }

            if (transferId == m_gapAutoResumeTransferId) {
                m_gapAutoResumeChunkIndexes.append(chunkIndex);
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
        }

        if (message["fileName"].toString() == QString::fromLatin1(kAckTimeoutAutoResumeFileName)) {
            if (m_autoResumeTransferId.isEmpty() && chunkIndex == 0) {
                m_autoResumeTransferId = transferId;
                m_autoResumeFileSize = fileSize;
                m_autoResumeChunkSize = chunkSize;
                m_autoResumeChunkCount = message["chunkCount"].toVariant().toLongLong();
                m_autoResumeFileHash = message["fileHash"].toString();
                return;
            }

            if (transferId == m_autoResumeTransferId) {
                m_autoResumeChunkIndexes.append(chunkIndex);
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
        }

        if (transferId == QString::fromLatin1(kResumeTransferId)
            || transferId == QString::fromLatin1(kQueryAndResumeTransferId)) {
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
    qint64 m_resumeFileSize = 0;
    QString m_resumeFileHash;
    QString m_autoResumeTransferId;
    QVector<qint64> m_autoResumeChunkIndexes;
    int m_autoResumeQueries = 0;
    qint64 m_autoResumeFileSize = 0;
    qint64 m_autoResumeChunkSize = 0;
    qint64 m_autoResumeChunkCount = 0;
    QString m_autoResumeFileHash;
    QString m_gapAutoResumeTransferId;
    QVector<qint64> m_gapAutoResumeChunkIndexes;
    int m_gapAutoResumeQueries = 0;
    qint64 m_gapAutoResumeFileSize = 0;
    qint64 m_gapAutoResumeChunkSize = 0;
    qint64 m_gapAutoResumeChunkCount = 0;
    QString m_gapAutoResumeFileHash;
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
    const QString resumeFileHash = fileSha256(resumeFilePath);
    ok = expect(!resumeFileHash.isEmpty(), "resume test file hash should be available") && ok;
    server.setResumeMetadata(resumeFileSize, resumeFileHash);
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
    const int chunksBeforeQueryResume = server.resumedChunkIndexes().size();
    QString queryResumeReason;
    ok = expect(sender.queryAndResumeFileTransfer(resumeFilePath,
                                                  QString::fromLatin1(kQueryAndResumeTransferId),
                                                  QString(),
                                                  MessageType::File,
                                                  &queryResumeReason,
                                                  5000),
                "sender should query resume metadata and continue from the confirmed chunk") && ok;
    const QVector<qint64> queryResumedChunks = server.resumedChunkIndexes();
    ok = expect(queryResumedChunks.size() == chunksBeforeQueryResume + 1 && queryResumedChunks.last() == 2,
                "query-and-resume should send only the next missing chunk") && ok;
    ok = expect(queryResumeReason.isEmpty(), "accepted query-and-resume should not expose a reject reason") && ok;
    QString mismatchReason;
    ok = expect(!sender.queryAndResumeFileTransfer(resumeFilePath,
                                                   QString::fromLatin1(kMismatchResumeTransferId),
                                                   QString(),
                                                   MessageType::File,
                                                   &mismatchReason,
                                                   5000),
                "sender should reject query-and-resume when metadata does not match the local file") && ok;
    ok = expect(!mismatchReason.isEmpty(), "metadata mismatch should expose a reject reason") && ok;
    ok = expect(server.resumedChunkIndexes().size() == queryResumedChunks.size(),
                "metadata mismatch should not send any resumed chunks") && ok;
    QString gapReason;
    ok = expect(!sender.queryAndResumeFileTransfer(resumeFilePath,
                                                   QString::fromLatin1(kGapResumeTransferId),
                                                   QString(),
                                                   MessageType::File,
                                                   &gapReason,
                                                   5000),
                "sender should reject query-and-resume when received chunks have a gap") && ok;
    ok = expect(!gapReason.isEmpty(), "received chunk gap should expose a reject reason") && ok;
    ok = expect(server.resumedChunkIndexes().size() == queryResumedChunks.size(),
                "received chunk gap should not send any resumed chunks") && ok;

    const QString autoResumePath = tempDir.filePath(QString::fromLatin1(kAckTimeoutAutoResumeFileName));
    qint64 autoResumeFileSize = 0;
    ok = expect(writeResumeFile(autoResumePath, &autoResumeFileSize),
                "auto resume test file should be created") && ok;
    ok = expect(sender.sendFile(autoResumePath),
                "sender should query resume state and continue after an ack timeout") && ok;
    const QVector<qint64> autoResumeChunks = server.autoResumeChunkIndexes();
    ok = expect(server.autoResumeQueries() == 1,
                "sender should query resume state once after the first ack timeout") && ok;
    ok = expect(autoResumeChunks.size() == 2 && autoResumeChunks[0] == 1 && autoResumeChunks[1] == 2,
                "sender should continue with the remaining chunks after resume state advances") && ok;

    const QString gapAutoResumePath = tempDir.filePath(QString::fromLatin1(kAckTimeoutGapResumeFileName));
    qint64 gapAutoResumeFileSize = 0;
    ok = expect(writeResumeFile(gapAutoResumePath, &gapAutoResumeFileSize),
                "gap auto resume test file should be created") && ok;
    ok = expect(sender.sendFile(gapAutoResumePath),
                "sender should ignore inconsistent auto resume state and fall back to retrying chunks") && ok;
    const QVector<qint64> gapAutoResumeChunks = server.gapAutoResumeChunkIndexes();
    ok = expect(server.gapAutoResumeQueries() == 1,
                "sender should query the inconsistent auto resume state once") && ok;
    ok = expect(gapAutoResumeChunks.size() == 3
                    && gapAutoResumeChunks[0] == 0
                    && gapAutoResumeChunks[1] == 1
                    && gapAutoResumeChunks[2] == 2,
                "sender should retry from the timed-out chunk when auto resume state is inconsistent") && ok;

    sender.disconnectFromServer();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}

#include "file_chunk_retry_progress_test.moc"
