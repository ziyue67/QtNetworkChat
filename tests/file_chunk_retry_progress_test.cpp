#include "client.h"
#include "e2eenvelope.h"
#include "objectstore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QStandardPaths>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <cstdio>
#include <functional>

namespace {
constexpr qint64 kClientChunkBytes = 256LL * 1024;
const char kResumeTransferId[] = "resume-send-transfer";
const char kQueryAndResumeTransferId[] = "query-and-resume-transfer";
const char kCrossConnectionSavedResumeTransferId[] = "cross-connection-saved-resume-transfer";
const char kMismatchResumeTransferId[] = "mismatch-resume-transfer";
const char kChunkSizeMismatchResumeTransferId[] = "chunk-size-mismatch-resume-transfer";
const char kChunkCountMismatchResumeTransferId[] = "chunk-count-mismatch-resume-transfer";
const char kInvalidProgressResumeTransferId[] = "invalid-progress-resume-transfer";
const char kGapResumeTransferId[] = "gap-resume-transfer";
const char kAckTimeoutAutoResumeFileName[] = "ack-timeout-auto-resume.bin";
const char kAckTimeoutGapResumeFileName[] = "ack-timeout-gap-resume.bin";
const char kAckTimeoutMiddleGapResumeFileName[] = "ack-timeout-middle-gap-resume.bin";
const char kAckTimeoutCompleteResumeFileName[] = "ack-timeout-complete-resume.bin";
const char kInvalidAckProgressFileName[] = "invalid-ack-progress.bin";
const char kTransientRejectRetryFileName[] = "transient-reject-retry.bin";
const char kHardRejectNoRetryFileName[] = "hard-reject-no-retry.bin";
const char kE2ECacheResumeTransferId[] = "e2e-cache-resume-transfer";
const char kE2EObjectResumeTransferId[] = "e2e-object-resume-transfer";
const char kE2EOfflineResumeTransferId[] = "e2e-offline-resume-transfer";

QString testAppDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
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

bool writeSmallFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "failed to open small file" << filePath << file.errorString();
        return false;
    }
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

QString outgoingTransferStatePath() {
    return QDir(testAppDataDir()).filePath("outgoing_transfer_state.json");
}

bool writeOutgoingTransferState(const QJsonObject& state) {
    QFile file(outgoingTransferStatePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
    file.write("\n");
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

QString bytesSha256(const QByteArray& payload) {
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

QString base64Url(const QByteArray& value) {
    return QString::fromLatin1(value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString safeLocalFileToken(const QString& value) {
    QString token;
    for (const QChar ch : value.trimmed()) {
        const ushort code = ch.unicode();
        const bool alpha = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
        const bool digit = code >= '0' && code <= '9';
        token.append(alpha || digit ? ch : QLatin1Char('_'));
    }
    return token.isEmpty() ? QStringLiteral("default") : token.left(96);
}

QString e2eResumeCachePath(const QString& transferId) {
    return QDir(testAppDataDir()).filePath(QStringLiteral("e2e_file_resume_cache_")
                                           + safeLocalFileToken(transferId)
                                           + QStringLiteral(".bin"));
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
    QVector<qint64> middleGapAutoResumeChunkIndexes() const { return m_middleGapAutoResumeChunkIndexes; }
    int middleGapAutoResumeQueries() const { return m_middleGapAutoResumeQueries; }
    QVector<qint64> completeAutoResumeChunkIndexes() const { return m_completeAutoResumeChunkIndexes; }
    int completeAutoResumeQueries() const { return m_completeAutoResumeQueries; }
    int invalidAckProgressAttempts() const { return m_invalidAckProgressAttempts; }
    int transientRejectAttempts() const { return m_transientRejectAttempts; }
    int hardRejectAttempts() const { return m_hardRejectAttempts; }
    QVector<qint64> e2eCacheResumeChunkIndexes() const { return m_e2eCacheResumeChunkIndexes; }
    int e2eCacheResumeQueries() const { return m_e2eCacheResumeQueries; }
    QVector<qint64> e2eObjectResumeChunkIndexes() const { return m_e2eObjectResumeChunkIndexes; }
    int e2eObjectResumeQueries() const { return m_e2eObjectResumeQueries; }
    QVector<qint64> e2eOfflineResumeChunkIndexes() const { return m_e2eOfflineResumeChunkIndexes; }
    int e2eOfflineResumeQueries() const { return m_e2eOfflineResumeQueries; }
    void setResumeMetadata(qint64 fileSize, const QString& fileHash) {
        m_resumeFileSize = fileSize;
        m_resumeFileHash = fileHash;
    }
    void setPeerIdentity(const QJsonObject& identity) {
        m_peerIdentity = identity;
    }
    void setE2ECacheResumeMetadata(qint64 fileSize, qint64 chunkCount, const QString& fileHash) {
        m_e2eCacheResumeFileSize = fileSize;
        m_e2eCacheResumeChunkCount = chunkCount;
        m_e2eCacheResumeFileHash = fileHash;
    }
    void setE2EObjectResumeMetadata(qint64 fileSize, qint64 chunkCount, const QString& fileHash) {
        m_e2eObjectResumeFileSize = fileSize;
        m_e2eObjectResumeChunkCount = chunkCount;
        m_e2eObjectResumeFileHash = fileHash;
    }
    void setE2EOfflineResumeMetadata(qint64 fileSize, qint64 chunkCount, const QString& fileHash) {
        m_e2eOfflineResumeFileSize = fileSize;
        m_e2eOfflineResumeChunkCount = chunkCount;
        m_e2eOfflineResumeFileHash = fileHash;
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
            if (!m_peerIdentity.isEmpty()) {
                QJsonObject announce;
                announce["type"] = "e2e_identity_announce";
                announce["senderId"] = "960002";
                announce["senderName"] = "RetryPeer";
                announce["receiverId"] = response["userId"].toString();
                announce["e2eIdentity"] = m_peerIdentity;
                QTimer::singleShot(0, socket, [socket, announce] {
                    writeJson(socket, announce);
                });
            }
            return;
        }

        if (type == "file_transfer_resume_query") {
            ++m_resumeQueries;
            QJsonArray receivedChunks;
            receivedChunks.append(QString::number(0));
            receivedChunks.append(QString::number(1));

            const QString transferId = message["transferId"].toString();
            if (transferId == QString::fromLatin1(kE2ECacheResumeTransferId)) {
                ++m_e2eCacheResumeQueries;
                QJsonArray e2eReceivedChunks;
                e2eReceivedChunks.append(QString::number(0));
                e2eReceivedChunks.append(QString::number(1));

                QJsonObject response;
                response["type"] = "file_transfer_resume_state";
                response["transferId"] = transferId;
                response["canResume"] = true;
                response["confirmedBytes"] = QString::number(2 * kClientChunkBytes);
                response["nextChunkIndex"] = QString::number(2);
                response["fileSize"] = QString::number(m_e2eCacheResumeFileSize);
                response["chunkSize"] = QString::number(kClientChunkBytes);
                response["chunkCount"] = QString::number(m_e2eCacheResumeChunkCount);
                response["fileHash"] = m_e2eCacheResumeFileHash;
                response["receivedChunks"] = e2eReceivedChunks;
                response["reason"] = "";
                writeJson(socket, response);
                return;
            }
            if (transferId == QString::fromLatin1(kE2EObjectResumeTransferId)) {
                ++m_e2eObjectResumeQueries;
                QJsonArray e2eReceivedChunks;
                e2eReceivedChunks.append(QString::number(0));
                e2eReceivedChunks.append(QString::number(1));

                QJsonObject response;
                response["type"] = "file_transfer_resume_state";
                response["transferId"] = transferId;
                response["canResume"] = true;
                response["confirmedBytes"] = QString::number(2 * kClientChunkBytes);
                response["nextChunkIndex"] = QString::number(2);
                response["fileSize"] = QString::number(m_e2eObjectResumeFileSize);
                response["chunkSize"] = QString::number(kClientChunkBytes);
                response["chunkCount"] = QString::number(m_e2eObjectResumeChunkCount);
                response["fileHash"] = m_e2eObjectResumeFileHash;
                response["receivedChunks"] = e2eReceivedChunks;
                response["reason"] = "";
                writeJson(socket, response);
                return;
            }
            if (transferId == QString::fromLatin1(kE2EOfflineResumeTransferId)) {
                ++m_e2eOfflineResumeQueries;
                QJsonArray e2eReceivedChunks;
                e2eReceivedChunks.append(QString::number(0));
                e2eReceivedChunks.append(QString::number(1));

                QJsonObject response;
                response["type"] = "file_transfer_resume_state";
                response["transferId"] = transferId;
                response["canResume"] = true;
                response["confirmedBytes"] = QString::number(2 * kClientChunkBytes);
                response["nextChunkIndex"] = QString::number(2);
                response["fileSize"] = QString::number(m_e2eOfflineResumeFileSize);
                response["chunkSize"] = QString::number(kClientChunkBytes);
                response["chunkCount"] = QString::number(m_e2eOfflineResumeChunkCount);
                response["fileHash"] = m_e2eOfflineResumeFileHash;
                response["receivedChunks"] = e2eReceivedChunks;
                response["reason"] = "";
                writeJson(socket, response);
                return;
            }

            if (!m_gapAutoResumeTransferId.isEmpty() && transferId == m_gapAutoResumeTransferId) {
                ++m_gapAutoResumeQueries;
                QJsonArray receivedChunks;
                receivedChunks.append(QString::number(0));
                receivedChunks.append(QString::number(2));

                QJsonObject response;
                response["type"] = "file_transfer_resume_state";
                response["transferId"] = transferId;
                response["canResume"] = true;
                response["confirmedBytes"] = QString::number(2 * kClientChunkBytes);
                response["nextChunkIndex"] = QString::number(2);
                response["fileSize"] = QString::number(m_gapAutoResumeFileSize);
                response["chunkSize"] = QString::number(m_gapAutoResumeChunkSize);
                response["chunkCount"] = QString::number(m_gapAutoResumeChunkCount);
                response["fileHash"] = m_gapAutoResumeFileHash;
                response["receivedChunks"] = receivedChunks;
                response["reason"] = "";
                writeJson(socket, response);
                return;
            }

            if (!m_middleGapAutoResumeTransferId.isEmpty() && transferId == m_middleGapAutoResumeTransferId) {
                ++m_middleGapAutoResumeQueries;
                QJsonArray receivedChunks;
                receivedChunks.append(QString::number(0));
                receivedChunks.append(QString::number(2));

                QJsonObject response;
                response["type"] = "file_transfer_resume_state";
                response["transferId"] = transferId;
                response["canResume"] = true;
                response["confirmedBytes"] = QString::number(m_middleGapAutoResumeFileSize);
                response["nextChunkIndex"] = QString::number(1);
                response["fileSize"] = QString::number(m_middleGapAutoResumeFileSize);
                response["chunkSize"] = QString::number(m_middleGapAutoResumeChunkSize);
                response["chunkCount"] = QString::number(m_middleGapAutoResumeChunkCount);
                response["fileHash"] = m_middleGapAutoResumeFileHash;
                response["receivedChunks"] = receivedChunks;
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

            if (!m_completeAutoResumeTransferId.isEmpty() && transferId == m_completeAutoResumeTransferId) {
                ++m_completeAutoResumeQueries;
                QJsonArray receivedChunks;
                for (qint64 index = 0; index < m_completeAutoResumeChunkCount; ++index) {
                    receivedChunks.append(QString::number(index));
                }

                QJsonObject response;
                response["type"] = "file_transfer_resume_state";
                response["transferId"] = transferId;
                response["canResume"] = true;
                response["confirmedBytes"] = QString::number(m_completeAutoResumeFileSize);
                response["nextChunkIndex"] = QString::number(m_completeAutoResumeChunkCount);
                response["fileSize"] = QString::number(m_completeAutoResumeFileSize);
                response["chunkSize"] = QString::number(m_completeAutoResumeChunkSize);
                response["chunkCount"] = QString::number(m_completeAutoResumeChunkCount);
                response["fileHash"] = m_completeAutoResumeFileHash;
                response["receivedChunks"] = receivedChunks;
                response["reason"] = "";
                writeJson(socket, response);
                return;
            }

            if (transferId == QString::fromLatin1(kQueryAndResumeTransferId)
                || transferId == QString::fromLatin1(kCrossConnectionSavedResumeTransferId)
                || transferId == QString::fromLatin1(kMismatchResumeTransferId)
                || transferId == QString::fromLatin1(kChunkSizeMismatchResumeTransferId)
                || transferId == QString::fromLatin1(kChunkCountMismatchResumeTransferId)
                || transferId == QString::fromLatin1(kInvalidProgressResumeTransferId)
                || transferId == QString::fromLatin1(kGapResumeTransferId)) {
                QJsonArray resumeChunks;
                resumeChunks.append(QString::number(0));
                resumeChunks.append(transferId == QString::fromLatin1(kGapResumeTransferId)
                    ? QString::number(2)
                    : QString::number(1));
                QJsonObject response;
                response["type"] = "file_transfer_resume_state";
                response["transferId"] = transferId;
                response["canResume"] = true;
                response["confirmedBytes"] = transferId == QString::fromLatin1(kInvalidProgressResumeTransferId)
                    ? QString::number(0)
                    : QString::number(2 * kClientChunkBytes);
                response["nextChunkIndex"] = QString::number(2);
                response["fileSize"] = QString::number(m_resumeFileSize);
                response["chunkSize"] = transferId == QString::fromLatin1(kChunkSizeMismatchResumeTransferId)
                    ? QString::number(kClientChunkBytes / 2)
                    : QString::number(kClientChunkBytes);
                const qint64 resumeChunkCount = (m_resumeFileSize + kClientChunkBytes - 1) / kClientChunkBytes;
                response["chunkCount"] = transferId == QString::fromLatin1(kChunkCountMismatchResumeTransferId)
                    ? QString::number(resumeChunkCount + 1)
                    : QString::number(resumeChunkCount);
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

        if (message["fileName"].toString() == QString::fromLatin1(kTransientRejectRetryFileName)) {
            ++m_transientRejectAttempts;

            QJsonObject ack;
            ack["type"] = "file_chunk_ack";
            ack["transferId"] = transferId;
            ack["chunkIndex"] = message["chunkIndex"].toString();
            ack["accepted"] = m_transientRejectAttempts > 1;
            ack["reason"] = m_transientRejectAttempts == 1
                ? QString::fromUtf8("临时繁忙，请重试")
                : QString();
            ack["receivedBytes"] = QString::number(m_transientRejectAttempts > 1 ? receivedBytes : 0);
            writeJson(socket, ack);
            return;
        }

        if (message["fileName"].toString() == QString::fromLatin1(kHardRejectNoRetryFileName)) {
            ++m_hardRejectAttempts;

            QJsonObject ack;
            ack["type"] = "file_chunk_ack";
            ack["transferId"] = transferId;
            ack["chunkIndex"] = message["chunkIndex"].toString();
            ack["accepted"] = false;
            ack["reason"] = QString::fromUtf8("同一传输编号的元数据不一致");
            ack["receivedBytes"] = QString::number(0);
            writeJson(socket, ack);
            return;
        }

        if (message["fileName"].toString() == QString::fromLatin1(kInvalidAckProgressFileName)) {
            ++m_invalidAckProgressAttempts;

            QJsonObject ack;
            ack["type"] = "file_chunk_ack";
            ack["transferId"] = transferId;
            ack["chunkIndex"] = message["chunkIndex"].toString();
            ack["accepted"] = true;
            ack["reason"] = "";
            ack["receivedBytes"] = QString::number(m_invalidAckProgressAttempts == 1
                ? 1
                : receivedBytes);
            writeJson(socket, ack);
            return;
        }

        if (message["fileName"].toString() == QString::fromLatin1(kAckTimeoutMiddleGapResumeFileName)) {
            if (m_middleGapAutoResumeTransferId.isEmpty() && chunkIndex == 1) {
                m_middleGapAutoResumeTransferId = transferId;
                m_middleGapAutoResumeFileSize = fileSize;
                m_middleGapAutoResumeChunkSize = chunkSize;
                m_middleGapAutoResumeChunkCount = message["chunkCount"].toVariant().toLongLong();
                m_middleGapAutoResumeFileHash = message["fileHash"].toString();
                return;
            }

            if (transferId == m_middleGapAutoResumeTransferId) {
                m_middleGapAutoResumeChunkIndexes.append(chunkIndex);
            }

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

        if (message["fileName"].toString() == QString::fromLatin1(kAckTimeoutCompleteResumeFileName)) {
            if (m_completeAutoResumeTransferId.isEmpty() && chunkIndex == 0) {
                m_completeAutoResumeTransferId = transferId;
                m_completeAutoResumeFileSize = fileSize;
                m_completeAutoResumeChunkSize = chunkSize;
                m_completeAutoResumeChunkCount = message["chunkCount"].toVariant().toLongLong();
                m_completeAutoResumeFileHash = message["fileHash"].toString();
                return;
            }

            if (transferId == m_completeAutoResumeTransferId) {
                m_completeAutoResumeChunkIndexes.append(chunkIndex);
            }
        }

        if (transferId == QString::fromLatin1(kResumeTransferId)
            || transferId == QString::fromLatin1(kQueryAndResumeTransferId)
            || transferId == QString::fromLatin1(kCrossConnectionSavedResumeTransferId)
            || transferId == QString::fromLatin1(kGapResumeTransferId)) {
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

        if (transferId == QString::fromLatin1(kE2ECacheResumeTransferId)) {
            if (message.value("e2eFileEncrypted").toBool(false)
                && message.value("e2eEnvelope").isObject()
                && !message.value("e2eEnvelope").toObject().contains(QStringLiteral("ciphertext"))) {
                m_e2eCacheResumeChunkIndexes.append(chunkIndex);
            }
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
        if (transferId == QString::fromLatin1(kE2EObjectResumeTransferId)) {
            if (message.value("e2eFileEncrypted").toBool(false)
                && message.value("e2eEnvelope").isObject()
                && !message.value("e2eEnvelope").toObject().contains(QStringLiteral("ciphertext"))) {
                m_e2eObjectResumeChunkIndexes.append(chunkIndex);
            }
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
        if (transferId == QString::fromLatin1(kE2EOfflineResumeTransferId)) {
            if (message.value("e2eFileEncrypted").toBool(false)
                && message.value("e2eEnvelope").isObject()
                && !message.value("e2eEnvelope").toObject().contains(QStringLiteral("ciphertext"))) {
                m_e2eOfflineResumeChunkIndexes.append(chunkIndex);
            }
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
    QString m_middleGapAutoResumeTransferId;
    QVector<qint64> m_middleGapAutoResumeChunkIndexes;
    int m_middleGapAutoResumeQueries = 0;
    qint64 m_middleGapAutoResumeFileSize = 0;
    qint64 m_middleGapAutoResumeChunkSize = 0;
    qint64 m_middleGapAutoResumeChunkCount = 0;
    QString m_middleGapAutoResumeFileHash;
    QString m_completeAutoResumeTransferId;
    QVector<qint64> m_completeAutoResumeChunkIndexes;
    int m_completeAutoResumeQueries = 0;
    qint64 m_completeAutoResumeFileSize = 0;
    qint64 m_completeAutoResumeChunkSize = 0;
    qint64 m_completeAutoResumeChunkCount = 0;
    QString m_completeAutoResumeFileHash;
    int m_invalidAckProgressAttempts = 0;
    int m_transientRejectAttempts = 0;
    int m_hardRejectAttempts = 0;
    QJsonObject m_peerIdentity;
    QVector<qint64> m_e2eCacheResumeChunkIndexes;
    int m_e2eCacheResumeQueries = 0;
    qint64 m_e2eCacheResumeFileSize = 0;
    qint64 m_e2eCacheResumeChunkCount = 0;
    QString m_e2eCacheResumeFileHash;
    QVector<qint64> m_e2eObjectResumeChunkIndexes;
    int m_e2eObjectResumeQueries = 0;
    qint64 m_e2eObjectResumeFileSize = 0;
    qint64 m_e2eObjectResumeChunkCount = 0;
    QString m_e2eObjectResumeFileHash;
    QVector<qint64> m_e2eOfflineResumeChunkIndexes;
    int m_e2eOfflineResumeQueries = 0;
    qint64 m_e2eOfflineResumeFileSize = 0;
    qint64 m_e2eOfflineResumeChunkCount = 0;
    QString m_e2eOfflineResumeFileHash;
};

class LoopbackS3ReadbackServer : public QObject {
    Q_OBJECT

public:
    explicit LoopbackS3ReadbackServer(const QByteArray& payload, const QString& objectKey, QObject* parent = nullptr)
        : QObject(parent),
          m_payload(payload),
          m_objectKey(objectKey),
          m_hash(bytesSha256(payload)) {
    }

    bool start() {
        connect(&m_server, &QTcpServer::newConnection, this, &LoopbackS3ReadbackServer::onNewConnection);
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const { return m_server.serverPort(); }
    int headCount() const { return m_headCount; }
    int getCount() const { return m_getCount; }

private slots:
    void onNewConnection() {
        QTcpSocket* socket = m_server.nextPendingConnection();
        if (!socket) return;
        m_buffers.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            QByteArray& buffer = m_buffers[socket];
            buffer.append(socket->readAll());
            if (!buffer.contains("\r\n\r\n")) {
                return;
            }
            const QByteArray request = buffer;
            buffer.clear();
            handleRequest(socket, request);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }

private:
    void writeHttpResponse(QTcpSocket* socket,
                           int status,
                           const QByteArray& reason,
                           const QByteArray& body,
                           const QList<QByteArray>& extraHeaders = {},
                           qint64 contentLengthOverride = -1) {
        QByteArray response;
        response += "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
        response += "Connection: close\r\n";
        const qint64 contentLength = contentLengthOverride >= 0 ? contentLengthOverride : body.size();
        response += "Content-Length: " + QByteArray::number(contentLength) + "\r\n";
        for (const QByteArray& header : extraHeaders) {
            response += header + "\r\n";
        }
        response += "\r\n";
        response += body;
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }

    void handleRequest(QTcpSocket* socket, const QByteArray& request) {
        const QList<QByteArray> lines = request.split('\n');
        const QByteArray requestLine = lines.isEmpty() ? QByteArray() : lines.first().trimmed();
        const QList<QByteArray> parts = requestLine.split(' ');
        const QByteArray method = parts.size() >= 1 ? parts.at(0).trimmed() : QByteArray();
        const QByteArray path = parts.size() >= 2 ? parts.at(1).trimmed() : QByteArray();
        const QByteArray expectedPath = QByteArray("/qtchat-e2e-test-bucket/") + m_objectKey.toUtf8();
        if (path != expectedPath) {
            writeHttpResponse(socket, 404, QByteArrayLiteral("Not Found"), QByteArray());
            return;
        }
        if (method == QByteArrayLiteral("HEAD")) {
            ++m_headCount;
            writeHttpResponse(socket,
                              200,
                              QByteArrayLiteral("OK"),
                              QByteArray(),
                              {
                                  QByteArrayLiteral("x-amz-meta-sha256: ") + m_hash.toLatin1(),
                                  QByteArrayLiteral("ETag: \"not-trusted\""),
                              },
                              m_payload.size());
            return;
        }
        if (method == QByteArrayLiteral("GET")) {
            ++m_getCount;
            writeHttpResponse(socket,
                              200,
                              QByteArrayLiteral("OK"),
                              m_payload,
                              {
                                  QByteArrayLiteral("x-amz-meta-sha256: ") + m_hash.toLatin1(),
                              });
            return;
        }
        writeHttpResponse(socket, 405, QByteArrayLiteral("Method Not Allowed"), QByteArray());
    }

    QTcpServer m_server;
    QMap<QTcpSocket*, QByteArray> m_buffers;
    QByteArray m_payload;
    QString m_objectKey;
    QString m_hash;
    int m_headCount = 0;
    int m_getCount = 0;
};
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("file_chunk_retry_progress_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = testAppDataDir();
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
    QMap<QString, QVector<qint64>> progressByFileName;
    QStringList connectionErrors;
    QObject::connect(&sender, &Client::fileTransferProgress, &app, [&](const QString&, qint64 bytesPrepared, qint64) {
        progressValues.append(bytesPrepared);
    });
    QObject::connect(&sender, &Client::fileTransferProgress, &app, [&](const QString& fileName, qint64 bytesPrepared, qint64) {
        progressByFileName[fileName].append(bytesPrepared);
    });
    QObject::connect(&sender, &Client::connectionError, &app, [&](const QString& error) {
        connectionErrors.append(error);
    });

    ok = expect(sender.sendFile(filePath), "sender should succeed after retrying the unacked chunk") && ok;
    ok = expect(server.chunkAttempts() == 2, "sender should retry the same chunk after the first ack is lost") && ok;
    ok = expect(!progressValues.isEmpty(), "sender should emit transfer progress") && ok;
    ok = expect(progressValues.first() == 0,
                "sender progress should start from zero prepared bytes for the current transfer") && ok;
    ok = expect(progressValues.last() == server.acknowledgedBytes(),
                "sender progress should use the acked received byte count after retry") && ok;
    const QVector<qint64> fileSpecificProgress = progressByFileName.value(QFileInfo(filePath).fileName());
    ok = expect(!fileSpecificProgress.isEmpty()
                    && fileSpecificProgress.first() == 0
                    && fileSpecificProgress.last() == server.acknowledgedBytes(),
                "file-specific progress tracking should converge to the acknowledged byte count") && ok;

    const QString invalidAckProgressPath = tempDir.filePath(QString::fromLatin1(kInvalidAckProgressFileName));
    ok = expect(writeSmallFile(invalidAckProgressPath),
                "invalid ack progress test file should be created") && ok;
    ok = expect(sender.sendFile(invalidAckProgressPath),
                "sender should retry the chunk when accepted ack progress is behind the current chunk") && ok;
    ok = expect(server.invalidAckProgressAttempts() == 2,
                "sender should resend after an accepted ack reports invalid progress") && ok;

    const QString transientRejectPath = tempDir.filePath(QString::fromLatin1(kTransientRejectRetryFileName));
    const int errorsBeforeTransientReject = connectionErrors.size();
    ok = expect(writeSmallFile(transientRejectPath),
                "transient reject retry test file should be created") && ok;
    ok = expect(sender.sendFile(transientRejectPath),
                "sender should retry a safely transient file chunk rejection") && ok;
    ok = expect(server.transientRejectAttempts() == 2,
                "sender should resend the same chunk after a transient reject") && ok;
    ok = expect(connectionErrors.size() > errorsBeforeTransientReject
                    && connectionErrors.last().contains(QString::fromUtf8("正在重试")),
                "transient reject should emit a retrying status instead of failing") && ok;

    const QString hardRejectPath = tempDir.filePath(QString::fromLatin1(kHardRejectNoRetryFileName));
    const int errorsBeforeHardReject = connectionErrors.size();
    ok = expect(writeSmallFile(hardRejectPath),
                "hard reject no-retry test file should be created") && ok;
    ok = expect(!sender.sendFile(hardRejectPath),
                "sender should not retry hard metadata rejection") && ok;
    ok = expect(server.hardRejectAttempts() == 1,
                "sender should fail fast on hard metadata rejection") && ok;
    ok = expect(connectionErrors.size() > errorsBeforeHardReject
                    && connectionErrors.last().contains(QString::fromUtf8("元数据不一致")),
                "hard reject should expose the metadata rejection reason") && ok;
    ok = expect(sender.clearOutgoingTransferState(),
                "sender should clear hard reject state before continuing the test") && ok;

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
    ok = expect(sender.clearOutgoingTransferState(), "sender should clear old outgoing transfer state") && ok;
    ok = expect(sender.saveOutgoingTransferState("persist-transfer",
                                                 resumeFilePath,
                                                 "960002",
                                                 MessageType::Image,
                                                 resumeFileHash,
                                                 resumeFileSize,
                                                 resumeChunkCount),
                "sender should persist outgoing transfer resume metadata") && ok;
    QJsonObject persistedState;
    ok = expect(sender.loadOutgoingTransferState(&persistedState),
                "sender should load outgoing transfer resume metadata") && ok;
    ok = expect(persistedState["transferId"].toString() == "persist-transfer",
                "persisted state should include the transfer id") && ok;
    ok = expect(persistedState["filePath"].toString() == QFileInfo(resumeFilePath).absoluteFilePath(),
                "persisted state should include the absolute file path") && ok;
    ok = expect(persistedState["receiverId"].toString() == "960002",
                "persisted state should include the receiver id") && ok;
    ok = expect(persistedState["messageType"].toInt() == static_cast<int>(MessageType::Image),
                "persisted state should include the message type") && ok;
    ok = expect(persistedState["fileHash"].toString() == resumeFileHash,
                "persisted state should include the file hash") && ok;
    ok = expect(persistedState["fileSize"].toVariant().toLongLong() == resumeFileSize,
                "persisted state should include the file size") && ok;
    ok = expect(persistedState["chunkSize"].toVariant().toLongLong() == kClientChunkBytes,
                "persisted state should include the chunk size") && ok;
    ok = expect(persistedState["chunkCount"].toVariant().toLongLong() == resumeChunkCount,
                "persisted state should include the chunk count") && ok;
    const QJsonObject defaultRecoveryStatus = sender.savedOutgoingTransferRecoveryStatus();
    ok = expect(defaultRecoveryStatus["configured"].toBool(),
                "default persisted state should expose recovery status") && ok;
    ok = expect(defaultRecoveryStatus["recoveryMode"].toString() == "resume"
                    && defaultRecoveryStatus["canAutoResume"].toBool(),
                "default persisted state should remain auto-resumable") && ok;
    ok = expect(defaultRecoveryStatus["reason"].toString() == "can-query-resume-state",
                "default persisted state should describe resume query recovery") && ok;
    ok = expect(defaultRecoveryStatus["filePath"].toString() == QFileInfo(resumeFilePath).absoluteFilePath()
                    && defaultRecoveryStatus["fileName"].toString() == QFileInfo(resumeFilePath).fileName(),
                "plaintext saved transfer recovery status may expose the local path for UI recovery") && ok;
    QJsonObject expiredState = persistedState;
    expiredState["updatedAt"] = QDateTime::currentDateTimeUtc().addDays(-2).toString(Qt::ISODate);
    ok = expect(writeOutgoingTransferState(expiredState),
                "test should write an expired outgoing transfer state") && ok;
    ok = expect(!sender.loadOutgoingTransferState(nullptr),
                "expired outgoing transfer state should not be loadable") && ok;
    ok = expect(!QFile::exists(outgoingTransferStatePath()),
                "expired outgoing transfer state should be removed") && ok;
    ok = expect(sender.saveOutgoingTransferState("persist-transfer",
                                                 resumeFilePath,
                                                 "960002",
                                                 MessageType::Image,
                                                 resumeFileHash,
                                                 resumeFileSize,
                                                 resumeChunkCount),
                "sender should recreate outgoing transfer state after expired cleanup") && ok;
    ok = expect(sender.clearOutgoingTransferState(), "sender should remove outgoing transfer state") && ok;
    ok = expect(!sender.loadOutgoingTransferState(nullptr),
                "cleared outgoing transfer state should not be loadable") && ok;
    ok = expect(sender.saveOutgoingTransferState(QString::fromLatin1(kQueryAndResumeTransferId),
                                                 resumeFilePath,
                                                 QString(),
                                                 MessageType::File,
                                                 resumeFileHash,
                                                 resumeFileSize,
                                                 resumeChunkCount),
                "sender should persist state for saved transfer recovery") && ok;
    const int chunksBeforeSavedResume = server.resumedChunkIndexes().size();
    QString savedResumeReason;
    ok = expect(sender.resumeSavedOutgoingTransfer(&savedResumeReason, 5000),
                "sender should resume from persisted outgoing transfer state") && ok;
    const QVector<qint64> savedResumedChunks = server.resumedChunkIndexes();
    ok = expect(savedResumedChunks.size() == chunksBeforeSavedResume + 1 && savedResumedChunks.last() == 2,
                "saved transfer recovery should send the next missing chunk") && ok;
    ok = expect(savedResumeReason.isEmpty(),
                "successful saved transfer recovery should not expose a reject reason") && ok;
    ok = expect(!sender.loadOutgoingTransferState(nullptr),
                "successful saved transfer recovery should clear persisted state") && ok;

    ok = expect(sender.saveOutgoingTransferState(QString::fromLatin1(kCrossConnectionSavedResumeTransferId),
                                                 resumeFilePath,
                                                 QString(),
                                                 MessageType::File,
                                                 resumeFileHash,
                                                 resumeFileSize,
                                                 resumeChunkCount),
                "sender should persist state for cross-connection saved transfer recovery") && ok;
    Client recoveredSender;
    recoveredSender.setUserInfo("950001", "RetrySender");
    recoveredSender.setAccountInfo("950001", "secret", false);
    ok = expect(recoveredSender.connectToServer("127.0.0.1", server.port()),
                "recovered sender should connect on a new connection") && ok;
    ok = expect(recoveredSender.waitForLoginResult(5000),
                "recovered sender should log in before resuming saved transfer") && ok;
    const int chunksBeforeCrossConnectionSavedResume = server.resumedChunkIndexes().size();
    const int queriesBeforeCrossConnectionSavedResume = server.resumeQueries();
    QString crossConnectionSavedResumeReason;
    ok = expect(recoveredSender.resumeSavedOutgoingTransfer(&crossConnectionSavedResumeReason, 5000),
                "new client connection should resume persisted outgoing transfer state") && ok;
    const QVector<qint64> crossConnectionSavedChunks = server.resumedChunkIndexes();
    ok = expect(crossConnectionSavedChunks.size() == chunksBeforeCrossConnectionSavedResume + 1
                    && crossConnectionSavedChunks.last() == 2,
                "cross-connection saved recovery should send only the next missing chunk") && ok;
    ok = expect(server.resumeQueries() == queriesBeforeCrossConnectionSavedResume + 1,
                "cross-connection saved recovery should query server resume state once") && ok;
    ok = expect(crossConnectionSavedResumeReason.isEmpty(),
                "successful cross-connection saved recovery should not expose a reject reason") && ok;
    ok = expect(!recoveredSender.loadOutgoingTransferState(nullptr),
                "successful cross-connection saved recovery should clear persisted state") && ok;
    recoveredSender.disconnectFromServer();

    QJsonObject e2eRecoveryPolicy;
    e2eRecoveryPolicy["recoveryMode"] = "resend";
    e2eRecoveryPolicy["recoveryReason"] = "e2e-file-resend-required";
    e2eRecoveryPolicy["recoveryAction"] = "resend-file";
    e2eRecoveryPolicy["e2eFileEncrypted"] = true;
    e2eRecoveryPolicy["e2eFileKeyId"] = "e2e-file-key-001";
    e2eRecoveryPolicy["e2eFileKeyFingerprintSha256"] = "0123456789abcdef";
    e2eRecoveryPolicy["e2eFilePlainSize"] = QString::number(resumeFileSize);
    e2eRecoveryPolicy["e2eFilePlainHash"] = resumeFileHash;
    e2eRecoveryPolicy["e2eFileWireSize"] = QString::number(resumeFileSize + 96);
    e2eRecoveryPolicy["e2eFileWireHash"] = "wire-ciphertext-sha256";
    ok = expect(sender.saveOutgoingTransferState("e2e-resend-transfer",
                                                 resumeFilePath,
                                                 "960002",
                                                 MessageType::File,
                                                 "wire-ciphertext-sha256",
                                                 resumeFileSize + 96,
                                                 resumeChunkCount + 1,
                                                 e2eRecoveryPolicy),
                "sender should persist E2E file resend recovery metadata") && ok;
    const QJsonObject e2eRecoveryStatus = sender.savedOutgoingTransferRecoveryStatus();
    ok = expect(e2eRecoveryStatus["configured"].toBool(),
                "E2E persisted state should expose recovery status") && ok;
    ok = expect(e2eRecoveryStatus["recoveryMode"].toString() == "resend"
                    && !e2eRecoveryStatus["canAutoResume"].toBool(),
                "E2E persisted state should require a resend instead of auto-resume") && ok;
    ok = expect(e2eRecoveryStatus["reason"].toString() == "e2e-file-resend-required"
                    && e2eRecoveryStatus["action"].toString() == "resend-file",
                "E2E persisted state should expose resend reason and operator action") && ok;
    ok = expect(e2eRecoveryStatus["fileHash"].toString() == "wire-ciphertext-sha256"
                    && e2eRecoveryStatus["fileSize"].toVariant().toLongLong() == resumeFileSize + 96,
                "E2E persisted state should keep wire ciphertext size/hash as the transfer metadata") && ok;
    ok = expect(!e2eRecoveryStatus.contains("filePath")
                    && e2eRecoveryStatus["fileName"].toString() == QFileInfo(resumeFilePath).fileName(),
                "E2E recovery status should expose only the display filename, not the local path") && ok;
    ok = expect(e2eRecoveryStatus["e2eFileEncrypted"].toBool()
                    && e2eRecoveryStatus["e2eFileKeyId"].toString() == "e2e-file-key-001"
                    && e2eRecoveryStatus["e2eFileKeyFingerprintSha256"].toString() == "0123456789abcdef"
                    && e2eRecoveryStatus["e2eFilePlainSize"].toVariant().toLongLong() == resumeFileSize
                    && e2eRecoveryStatus["e2eFilePlainHash"].toString() == resumeFileHash
                    && e2eRecoveryStatus["e2eFileWireSize"].toVariant().toLongLong() == resumeFileSize + 96
                    && e2eRecoveryStatus["e2eFileWireHash"].toString() == "wire-ciphertext-sha256",
                "E2E persisted state should expose sanitized plaintext/wire evidence") && ok;
    const int queriesBeforeE2eResend = server.resumeQueries();
    const int chunksBeforeE2eResend = server.resumedChunkIndexes().size();
    const int errorsBeforeE2eResend = connectionErrors.size();
    QString e2eResendReason;
    ok = expect(!sender.resumeSavedOutgoingTransfer(&e2eResendReason, 5000),
                "E2E persisted file recovery should refuse automatic resume") && ok;
    ok = expect(e2eResendReason == "e2e-file-resend-required",
                "E2E resend refusal should expose the fixed reason") && ok;
    ok = expect(server.resumeQueries() == queriesBeforeE2eResend,
                "E2E resend recovery should not query server resume state") && ok;
    ok = expect(server.resumedChunkIndexes().size() == chunksBeforeE2eResend,
                "E2E resend recovery should not send any chunks") && ok;
    ok = expect(connectionErrors.size() > errorsBeforeE2eResend
                    && connectionErrors.last().contains(QString::fromUtf8("未完成发送需要重新发送")),
                "E2E resend recovery should surface a user-visible resend message") && ok;
    ok = expect(sender.loadOutgoingTransferState(nullptr),
                "E2E resend recovery should keep persisted state until the user clears it") && ok;
    ok = expect(sender.clearOutgoingTransferState(),
                "sender should clear E2E resend recovery state before continuing the test") && ok;

    QJsonObject e2eObjectRecoveryHeader;
    e2eObjectRecoveryHeader["protocol"] = "qtnetworkchat-e2e-v1";
    e2eObjectRecoveryHeader["suite"] = e2eDefaultSuite();
    e2eObjectRecoveryHeader["senderId"] = "950001";
    e2eObjectRecoveryHeader["receiverId"] = "960002";
    e2eObjectRecoveryHeader["keyId"] = "e2e-file-object-session";
    e2eObjectRecoveryHeader["nonce"] = base64Url(QByteArray("object-nonce-123"));
    e2eObjectRecoveryHeader["tag"] = base64Url(QByteArray("object-tag-123456"));
    e2eObjectRecoveryHeader["aad"] = "file/private/v1;object-recovery";
    const QString e2eObjectWireHash =
        QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    e2eObjectRecoveryHeader["ciphertextSha256"] = e2eObjectWireHash;
    QJsonObject e2eObjectRecoveryPolicy;
    e2eObjectRecoveryPolicy["recoveryMode"] = "resume";
    e2eObjectRecoveryPolicy["recoveryReason"] = "e2e-file-same-wire-cache-ready";
    e2eObjectRecoveryPolicy["recoveryAction"] = "resume-same-wire-envelope";
    e2eObjectRecoveryPolicy["e2eFileEncrypted"] = true;
    e2eObjectRecoveryPolicy["e2eFileKeyId"] = "e2e-file-object-session";
    e2eObjectRecoveryPolicy["e2eFileKeyFingerprintSha256"] =
        QStringLiteral("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    e2eObjectRecoveryPolicy["e2eFilePlainSize"] = QString::number(resumeFileSize);
    e2eObjectRecoveryPolicy["e2eFilePlainHash"] = resumeFileHash;
    e2eObjectRecoveryPolicy["e2eFileWireSize"] = QString::number(resumeFileSize + 128);
    e2eObjectRecoveryPolicy["e2eFileWireHash"] = e2eObjectWireHash;
    e2eObjectRecoveryPolicy["e2eFileEnvelopeHeader"] = e2eObjectRecoveryHeader;
    e2eObjectRecoveryPolicy["e2eFileObjectRecoveryCandidate"] = true;
    e2eObjectRecoveryPolicy["e2eFileObjectStoreKey"] = "safeObjectKey_123.bin";
    e2eObjectRecoveryPolicy["e2eFileObjectStoreType"] = "filesystem";
    e2eObjectRecoveryPolicy["e2eFileObjectStoreHash"] = e2eObjectWireHash;
    e2eObjectRecoveryPolicy["e2eFileObjectStoreSize"] = QString::number(resumeFileSize + 128);
    qunsetenv("QTNETWORKCHAT_OBJECT_ROOT");
    ok = expect(sender.saveOutgoingTransferState("e2e-object-recovery-transfer",
                                                 resumeFilePath,
                                                 "960002",
                                                 MessageType::File,
                                                 e2eObjectWireHash,
                                                 resumeFileSize + 128,
                                                 resumeChunkCount + 1,
                                                 e2eObjectRecoveryPolicy),
                "sender should persist E2E object recovery candidate metadata") && ok;
    const QJsonObject e2eObjectRecoveryStatus = sender.savedOutgoingTransferRecoveryStatus();
    const QByteArray e2eObjectRecoveryStatusJson =
        QJsonDocument(e2eObjectRecoveryStatus).toJson(QJsonDocument::Compact);
    ok = expect(e2eObjectRecoveryStatus["configured"].toBool()
                    && e2eObjectRecoveryStatus["e2eFileEncrypted"].toBool()
                    && e2eObjectRecoveryStatus["e2eFileObjectRecoveryCandidate"].toBool()
                    && !e2eObjectRecoveryStatus["e2eFileOfflineObjectRecoveryReady"].toBool(true)
                    && e2eObjectRecoveryStatus["recoveryMode"].toString() == "resend"
                    && !e2eObjectRecoveryStatus["canAutoResume"].toBool()
                    && e2eObjectRecoveryStatus["reason"].toString()
                        == "e2e-file-object-recovery-store-unavailable"
                    && e2eObjectRecoveryStatus["action"].toString()
                        == "resend-or-wait-for-object-recovery",
                "E2E object recovery candidate should remain fail-closed when object storage is unavailable") && ok;
    ok = expect(e2eObjectRecoveryStatus["e2eFileObjectStoreKey"].toString() == "safeObjectKey_123.bin"
                    && e2eObjectRecoveryStatus["e2eFileObjectStoreHash"].toString() == e2eObjectWireHash
                    && e2eObjectRecoveryStatus["e2eFileObjectStoreSize"].toVariant().toLongLong()
                        == resumeFileSize + 128
                    && e2eObjectRecoveryStatus["e2eFileObjectRecoveryMaterialPolicy"].toString()
                        == "object-ciphertext-only-no-secret-export",
                "E2E object recovery status should expose only sanitized object evidence") && ok;
    ok = expect(!e2eObjectRecoveryStatusJson.contains("\"ciphertext\"")
                    && !e2eObjectRecoveryStatusJson.contains("ciphertextBytes")
                    && !e2eObjectRecoveryStatusJson.contains("rawCiphertext")
                    && !e2eObjectRecoveryStatusJson.contains("object://")
                    && !e2eObjectRecoveryStatusJson.contains("file://")
                    && !e2eObjectRecoveryStatusJson.contains("privateKey")
                    && !e2eObjectRecoveryStatusJson.contains("sessionKey"),
                "E2E object recovery status should not export sensitive payloads or local paths") && ok;
    const int queriesBeforeE2eObjectRecovery = server.resumeQueries();
    const int chunksBeforeE2eObjectRecovery = server.resumedChunkIndexes().size();
    QString e2eObjectRecoveryReason;
    ok = expect(!sender.resumeSavedOutgoingTransfer(&e2eObjectRecoveryReason, 5000),
                "E2E object recovery candidate should refuse automatic resume") && ok;
    ok = expect(e2eObjectRecoveryReason == "e2e-file-object-recovery-store-unavailable",
                "E2E object recovery refusal should expose the fixed store-unavailable reason") && ok;
    ok = expect(server.resumeQueries() == queriesBeforeE2eObjectRecovery
                    && server.resumedChunkIndexes().size() == chunksBeforeE2eObjectRecovery,
                "E2E object recovery candidate should not query or send without readback support") && ok;
    ok = expect(sender.clearOutgoingTransferState(),
                "sender should clear E2E object recovery state before continuing the test") && ok;

    QJsonObject e2eS3RecoveryPolicy = e2eObjectRecoveryPolicy;
    e2eS3RecoveryPolicy["e2eFileObjectStoreKey"] = "safeS3Object_123.bin";
    e2eS3RecoveryPolicy["e2eFileObjectStoreType"] = "s3";
    ok = expect(sender.saveOutgoingTransferState("e2e-s3-recovery-transfer",
                                                 resumeFilePath,
                                                 "960002",
                                                 MessageType::File,
                                                 e2eObjectWireHash,
                                                 resumeFileSize + 128,
                                                 resumeChunkCount + 1,
                                                 e2eS3RecoveryPolicy),
                "sender should persist E2E S3 object recovery candidate metadata") && ok;
    const QJsonObject e2eS3RecoveryStatus = sender.savedOutgoingTransferRecoveryStatus();
    const QByteArray e2eS3RecoveryStatusJson =
        QJsonDocument(e2eS3RecoveryStatus).toJson(QJsonDocument::Compact);
    ok = expect(e2eS3RecoveryStatus["configured"].toBool()
                    && e2eS3RecoveryStatus["e2eFileObjectRecoveryCandidate"].toBool()
                    && e2eS3RecoveryStatus["e2eFileObjectStoreKeySafe"].toBool(false)
                    && e2eS3RecoveryStatus["e2eFileObjectStoreKey"].toString()
                        == "safeS3Object_123.bin"
                    && e2eS3RecoveryStatus["e2eFileObjectStoreType"].toString() == "s3"
                    && e2eS3RecoveryStatus["e2eFileObjectRecoveryScope"].toString()
                        == "s3-offline-auto-readback"
                    && e2eS3RecoveryStatus["e2eFileObjectRecoveryReviewGate"].toString()
                        == "s3-offline-auto-readback-not-reviewed"
                    && e2eS3RecoveryStatus["reason"].toString()
                        == "e2e-file-s3-offline-auto-readback-not-reviewed"
                    && e2eS3RecoveryStatus["action"].toString()
                        == "keep-s3-offline-auto-readback-fail-closed-until-reviewed"
                    && !e2eS3RecoveryStatus["canAutoResume"].toBool(),
                "E2E S3 object recovery candidate should stay fail-closed behind reviewed auto-readback gate") && ok;
    ok = expect(e2eS3RecoveryStatus["e2eFileObjectRecoveryNoSensitiveLocatorExport"].toBool(false)
                    && !e2eS3RecoveryStatus["e2eFileOfflineObjectRecoveryReady"].toBool(true)
                    && !e2eS3RecoveryStatusJson.contains("https://")
                    && !e2eS3RecoveryStatusJson.contains("object://")
                    && !e2eS3RecoveryStatusJson.contains("bucket")
                    && !e2eS3RecoveryStatusJson.contains("endpoint")
                    && !e2eS3RecoveryStatusJson.contains("signature")
                    && !e2eS3RecoveryStatusJson.contains("privateKey")
                    && !e2eS3RecoveryStatusJson.contains("sessionKey"),
                "E2E S3 recovery status should expose only safe token evidence") && ok;
    const int queriesBeforeE2eS3Recovery = server.resumeQueries();
    const int chunksBeforeE2eS3Recovery = server.resumedChunkIndexes().size();
    QString e2eS3RecoveryReason;
    ok = expect(!sender.resumeSavedOutgoingTransfer(&e2eS3RecoveryReason, 5000),
                "E2E S3 object recovery candidate should refuse automatic resume before review") && ok;
    ok = expect(e2eS3RecoveryReason == "e2e-file-s3-offline-auto-readback-not-reviewed",
                "E2E S3 recovery refusal should expose the fixed not-reviewed reason") && ok;
    ok = expect(server.resumeQueries() == queriesBeforeE2eS3Recovery
                    && server.resumedChunkIndexes().size() == chunksBeforeE2eS3Recovery,
                "E2E S3 recovery candidate should not query or send while fail-closed") && ok;

    QJsonObject e2eOfflineRecoveryPolicy = e2eObjectRecoveryPolicy;
    e2eOfflineRecoveryPolicy["e2eFileObjectStoreKey"] = "safeOfflineObject_123.bin";
    e2eOfflineRecoveryPolicy["e2eFileObjectStoreType"] = "offline";
    ok = expect(sender.saveOutgoingTransferState("e2e-offline-recovery-transfer",
                                                 resumeFilePath,
                                                 "960002",
                                                 MessageType::File,
                                                 e2eObjectWireHash,
                                                 resumeFileSize + 128,
                                                 resumeChunkCount + 1,
                                                 e2eOfflineRecoveryPolicy),
                "sender should persist E2E offline object recovery candidate metadata") && ok;
    const QJsonObject e2eOfflineRecoveryStatus = sender.savedOutgoingTransferRecoveryStatus();
    const QByteArray e2eOfflineRecoveryStatusJson =
        QJsonDocument(e2eOfflineRecoveryStatus).toJson(QJsonDocument::Compact);
    ok = expect(e2eOfflineRecoveryStatus["configured"].toBool()
                    && e2eOfflineRecoveryStatus["e2eFileObjectRecoveryCandidate"].toBool()
                    && e2eOfflineRecoveryStatus["e2eFileObjectStoreKeySafe"].toBool(false)
                    && e2eOfflineRecoveryStatus["e2eFileObjectStoreKey"].toString()
                        == "safeOfflineObject_123.bin"
                    && e2eOfflineRecoveryStatus["e2eFileObjectStoreType"].toString() == "offline"
                    && e2eOfflineRecoveryStatus["e2eFileObjectRecoveryScope"].toString()
                        == "offline-auto-readback"
                    && e2eOfflineRecoveryStatus["e2eFileObjectRecoveryReviewGate"].toString()
                        == "offline-auto-readback-not-reviewed"
                    && e2eOfflineRecoveryStatus["reason"].toString()
                        == "e2e-file-offline-auto-readback-not-reviewed"
                    && e2eOfflineRecoveryStatus["action"].toString()
                        == "keep-offline-auto-readback-fail-closed-until-reviewed"
                    && !e2eOfflineRecoveryStatus["canAutoResume"].toBool(),
                "E2E offline object recovery candidate should stay fail-closed behind reviewed auto-readback gate") && ok;
    ok = expect(e2eOfflineRecoveryStatus["e2eFileObjectRecoveryNoSensitiveLocatorExport"].toBool(false)
                    && !e2eOfflineRecoveryStatus["e2eFileOfflineObjectRecoveryReady"].toBool(true)
                    && !e2eOfflineRecoveryStatusJson.contains("offline_files")
                    && !e2eOfflineRecoveryStatusJson.contains("file://")
                    && !e2eOfflineRecoveryStatusJson.contains("privateKey")
                    && !e2eOfflineRecoveryStatusJson.contains("sessionKey"),
                "E2E offline recovery status should expose only safe token evidence") && ok;
    const int queriesBeforeE2eOfflineRecovery = server.resumeQueries();
    const int chunksBeforeE2eOfflineRecovery = server.resumedChunkIndexes().size();
    QString e2eOfflineRecoveryReason;
    ok = expect(!sender.resumeSavedOutgoingTransfer(&e2eOfflineRecoveryReason, 5000),
                "E2E offline object recovery candidate should refuse automatic resume before review") && ok;
    ok = expect(e2eOfflineRecoveryReason == "e2e-file-offline-auto-readback-not-reviewed",
                "E2E offline recovery refusal should expose the fixed not-reviewed reason") && ok;
    ok = expect(server.resumeQueries() == queriesBeforeE2eOfflineRecovery
                    && server.resumedChunkIndexes().size() == chunksBeforeE2eOfflineRecovery,
                "E2E offline recovery candidate should not query or send while fail-closed") && ok;

    QJsonObject legacyUnsafeS3State;
    ok = expect(sender.loadOutgoingTransferState(&legacyUnsafeS3State),
                "test should load E2E S3 recovery state before legacy mutation") && ok;
    legacyUnsafeS3State["e2eFileObjectStoreKey"] =
        QStringLiteral("https://") + QStringLiteral("s3.example.invalid")
        + QStringLiteral("/private/path.bin?") + QStringLiteral("signature=")
        + QStringLiteral("secret");
    legacyUnsafeS3State["e2eFileObjectRecoveryReason"] = "resume-object-wire-envelope";
    legacyUnsafeS3State["e2eFileObjectRecoveryAction"] = "resume-object-wire-envelope";
    ok = expect(writeOutgoingTransferState(legacyUnsafeS3State),
                "test should write legacy unsafe E2E S3 recovery metadata") && ok;
    const QJsonObject legacyUnsafeS3Status = sender.savedOutgoingTransferRecoveryStatus();
    const QByteArray legacyUnsafeS3StatusJson =
        QJsonDocument(legacyUnsafeS3Status).toJson(QJsonDocument::Compact);
    ok = expect(legacyUnsafeS3Status["configured"].toBool()
                    && legacyUnsafeS3Status["e2eFileObjectRecoveryCandidate"].toBool()
                    && !legacyUnsafeS3Status["e2eFileObjectStoreKeySafe"].toBool(true)
                    && legacyUnsafeS3Status["e2eFileObjectStoreKeySuppressed"].toBool(false)
                    && !legacyUnsafeS3Status.contains("e2eFileObjectStoreKey")
                    && legacyUnsafeS3Status["e2eFileObjectRecoveryReviewGate"].toString()
                        == "object-store-key-token-invalid"
                    && legacyUnsafeS3Status["reason"].toString()
                        == "e2e-file-object-recovery-evidence-invalid"
                    && legacyUnsafeS3Status["action"].toString()
                        == "suppress-object-key-and-resend"
                    && !legacyUnsafeS3Status["canAutoResume"].toBool(),
                "legacy unsafe E2E S3 object locator should be suppressed and fail closed") && ok;
    const QByteArray unsafeScheme = QByteArrayLiteral("https://");
    const QByteArray unsafeHost = QByteArrayLiteral("s3.example.invalid");
    const QByteArray unsafeSigToken = QByteArrayLiteral("signature=") + QByteArrayLiteral("secret");
    ok = expect(!legacyUnsafeS3StatusJson.contains(unsafeScheme)
                    && !legacyUnsafeS3StatusJson.contains(unsafeHost)
                    && !legacyUnsafeS3StatusJson.contains(unsafeSigToken)
                    && !legacyUnsafeS3StatusJson.contains("private/path")
                    && !legacyUnsafeS3StatusJson.contains("resume-object-wire-envelope"),
                "legacy unsafe E2E S3 status should not export URL-like object locators or stale resume actions") && ok;
    const int queriesBeforeLegacyUnsafeS3 = server.resumeQueries();
    const int chunksBeforeLegacyUnsafeS3 = server.resumedChunkIndexes().size();
    QString legacyUnsafeS3Reason;
    ok = expect(!sender.resumeSavedOutgoingTransfer(&legacyUnsafeS3Reason, 5000),
                "legacy unsafe E2E S3 recovery state should refuse automatic resume") && ok;
    ok = expect(legacyUnsafeS3Reason == "e2e-file-object-recovery-evidence-invalid",
                "legacy unsafe E2E S3 recovery refusal should expose the fixed evidence-invalid reason") && ok;
    ok = expect(server.resumeQueries() == queriesBeforeLegacyUnsafeS3
                    && server.resumedChunkIndexes().size() == chunksBeforeLegacyUnsafeS3,
                "legacy unsafe E2E S3 recovery state should not query or send") && ok;
    ok = expect(sender.clearOutgoingTransferState(),
                "sender should clear E2E S3 recovery state before continuing the test") && ok;

    const QByteArray e2eSessionKey = generateE2ESessionKey();
    const QByteArray peerPrivateKey = generateE2EPrivateKey();
    const QByteArray peerPublicKey = e2ePublicKeyFromPrivateKey(peerPrivateKey);
    QJsonObject peerIdentity;
    peerIdentity["protocol"] = "qtnetworkchat-e2e-v1";
    peerIdentity["suite"] = e2eDefaultSuite();
    peerIdentity["userId"] = "960002";
    peerIdentity["publicKey"] = base64Url(peerPublicKey);
    peerIdentity["publicKeyFingerprintSha256"] = e2eFingerprint(peerPublicKey);
    peerIdentity["agreementSigning"] = true;
    server.setPeerIdentity(peerIdentity);

    Client e2eRecoveredSender;
    e2eRecoveredSender.setUserInfo("950001", "RetrySender");
    e2eRecoveredSender.setAccountInfo("950001", "secret", false);
    ok = expect(e2eRecoveredSender.connectToServer("127.0.0.1", server.port()),
                "E2E recovered sender should connect before same-wire resume") && ok;
    ok = expect(e2eRecoveredSender.waitForLoginResult(5000),
                "E2E recovered sender should log in before same-wire resume") && ok;
    ok = expect(waitFor([&] {
        return e2eRecoveredSender.e2ePeerIdentityStatus("960002")
            .value("publicKeyFingerprintSha256").toString() == e2eFingerprint(peerPublicKey);
    }, 5000), "E2E recovered sender should observe the peer identity before same-wire resume") && ok;
    const QString e2eVerificationCode =
        e2eRecoveredSender.e2ePeerIdentityStatus("960002").value("verificationCode").toString();
    ok = expect(e2eRecoveredSender.pinE2EPeerIdentity("960002", e2eFingerprint(peerPublicKey))
                    && e2eRecoveredSender.verifyAndPinE2EPeerIdentity("960002", e2eVerificationCode),
                "E2E recovered sender should trust the cached file peer identity") && ok;
    e2eRecoveredSender.setE2ESessionKey("960002", "e2e-file-cache-session", e2eSessionKey);

    const QString e2ePlainFilePath = tempDir.filePath("e2e-cache-resume-plain.bin");
    qint64 e2ePlainSize = 0;
    ok = expect(writeResumeFile(e2ePlainFilePath, &e2ePlainSize),
                "E2E same-wire resume plaintext file should be created") && ok;
    const QString e2ePlainHash = fileSha256(e2ePlainFilePath);
    QFile e2ePlainFile(e2ePlainFilePath);
    ok = expect(e2ePlainFile.open(QIODevice::ReadOnly),
                "E2E same-wire resume plaintext file should be readable") && ok;
    const QByteArray e2ePlainPayload = e2ePlainFile.readAll();
    e2ePlainFile.close();
    QString e2eEncryptReason;
    const E2EEnvelope e2eEnvelope = encryptE2EPayload("950001",
                                                     "960002",
                                                     "e2e-file-cache-session",
                                                     e2eSessionKey,
                                                     e2ePlainPayload,
                                                     QStringLiteral("file/private/v1;%1;%2;%3;%4")
                                                         .arg(QString::fromLatin1(kE2ECacheResumeTransferId),
                                                              QString::number(e2ePlainSize),
                                                              e2ePlainHash,
                                                              QFileInfo(e2ePlainFilePath).fileName()),
                                                     &e2eEncryptReason);
    ok = expect(e2eEnvelope.isValid(&e2eEncryptReason),
                "E2E same-wire resume envelope should be valid") && ok;
    const QByteArray e2eWirePayload = e2eEnvelope.ciphertext;
    const QString e2eWireHash = bytesSha256(e2eWirePayload);
    const qint64 e2eWireChunkCount =
        (e2eWirePayload.size() + kClientChunkBytes - 1) / kClientChunkBytes;
    QJsonObject e2eEnvelopeHeader = e2eEnvelope.toJson();
    e2eEnvelopeHeader.remove(QStringLiteral("ciphertext"));

    QFile e2eCacheFile(e2eResumeCachePath(QString::fromLatin1(kE2ECacheResumeTransferId)));
    ok = expect(e2eCacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
                "E2E same-wire resume cache should be writable") && ok;
    ok = expect(e2eCacheFile.write(e2eWirePayload) == e2eWirePayload.size(),
                "E2E same-wire resume cache should persist the wire payload") && ok;
    e2eCacheFile.close();

    QJsonObject e2eCacheRecoveryPolicy;
    e2eCacheRecoveryPolicy["recoveryMode"] = "resume";
    e2eCacheRecoveryPolicy["recoveryReason"] = "e2e-file-same-wire-cache-ready";
    e2eCacheRecoveryPolicy["recoveryAction"] = "resume-same-wire-envelope";
    e2eCacheRecoveryPolicy["e2eFileEncrypted"] = true;
    e2eCacheRecoveryPolicy["e2eFileKeyId"] = "e2e-file-cache-session";
    e2eCacheRecoveryPolicy["e2eFileKeyFingerprintSha256"] = e2eFingerprint(e2eSessionKey);
    e2eCacheRecoveryPolicy["e2eFilePlainSize"] = QString::number(e2ePlainSize);
    e2eCacheRecoveryPolicy["e2eFilePlainHash"] = e2ePlainHash;
    e2eCacheRecoveryPolicy["e2eFileWireSize"] = QString::number(e2eWirePayload.size());
    e2eCacheRecoveryPolicy["e2eFileWireHash"] = e2eWireHash;
    e2eCacheRecoveryPolicy["e2eFileResumeCache"] = true;
    e2eCacheRecoveryPolicy["e2eFileResumeCacheFormat"] =
        "qtnetworkchat-e2e-file-wire-cache-v1";
    e2eCacheRecoveryPolicy["e2eFileResumeCacheSha256"] = e2eWireHash;
    e2eCacheRecoveryPolicy["e2eFileResumeCacheSize"] = QString::number(e2eWirePayload.size());
    e2eCacheRecoveryPolicy["e2eFileEnvelopeHeader"] = e2eEnvelopeHeader;
    ok = expect(e2eRecoveredSender.saveOutgoingTransferState(QString::fromLatin1(kE2ECacheResumeTransferId),
                                                             e2ePlainFilePath,
                                                             "960002",
                                                             MessageType::File,
                                                             e2eWireHash,
                                                             e2eWirePayload.size(),
                                                             e2eWireChunkCount,
                                                             e2eCacheRecoveryPolicy),
                "sender should persist E2E same-wire resume metadata") && ok;
    server.setE2ECacheResumeMetadata(e2eWirePayload.size(), e2eWireChunkCount, e2eWireHash);
    const QJsonObject e2eCacheRecoveryStatus =
        e2eRecoveredSender.savedOutgoingTransferRecoveryStatus();
    ok = expect(e2eCacheRecoveryStatus["configured"].toBool()
                    && e2eCacheRecoveryStatus["e2eFileEncrypted"].toBool()
                    && e2eCacheRecoveryStatus["recoveryMode"].toString() == "resume"
                    && e2eCacheRecoveryStatus["canAutoResume"].toBool()
                    && e2eCacheRecoveryStatus["reason"].toString()
                        == "e2e-file-same-wire-cache-ready"
                    && e2eCacheRecoveryStatus["action"].toString()
                        == "resume-same-wire-envelope",
                "E2E same-wire cache recovery should be auto-resumable only with cache evidence") && ok;
    const QString e2eCachePath = e2eResumeCachePath(QString::fromLatin1(kE2ECacheResumeTransferId));
    ok = expect(QFile::remove(e2eCachePath),
                "E2E same-wire recovery status should be tested after cache loss") && ok;
    const QJsonObject missingCacheRecoveryStatus =
        e2eRecoveredSender.savedOutgoingTransferRecoveryStatus();
    ok = expect(missingCacheRecoveryStatus["configured"].toBool()
                    && missingCacheRecoveryStatus["e2eFileEncrypted"].toBool()
                    && missingCacheRecoveryStatus["recoveryMode"].toString() == "resend"
                    && !missingCacheRecoveryStatus["canAutoResume"].toBool()
                    && missingCacheRecoveryStatus["reason"].toString()
                        == "e2e-file-resume-cache-unavailable"
                    && missingCacheRecoveryStatus["action"].toString() == "resend-file",
                "E2E same-wire recovery should fail closed when the local cache is missing") && ok;
    QFile e2eRestoredCacheFile(e2eCachePath);
    ok = expect(e2eRestoredCacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
                "E2E same-wire resume cache should be restorable for resume execution") && ok;
    ok = expect(e2eRestoredCacheFile.write(e2eWirePayload) == e2eWirePayload.size(),
                "E2E same-wire resume cache should restore the original wire payload") && ok;
    e2eRestoredCacheFile.close();
    const int e2eCacheQueriesBefore = server.e2eCacheResumeQueries();
    QString e2eCacheResumeReason;
    ok = expect(e2eRecoveredSender.resumeSavedOutgoingTransfer(&e2eCacheResumeReason, 5000),
                "E2E same-wire cache recovery should resume encrypted file chunks") && ok;
    ok = expect(server.e2eCacheResumeQueries() == e2eCacheQueriesBefore + 1,
                "E2E same-wire cache recovery should query server resume state once") && ok;
    const QVector<qint64> e2eCacheChunks = server.e2eCacheResumeChunkIndexes();
    ok = expect(e2eCacheChunks.size() == 1 && e2eCacheChunks.last() == 2,
                "E2E same-wire cache recovery should send only the missing ciphertext chunk") && ok;
    ok = expect(e2eCacheResumeReason.isEmpty(),
                "successful E2E same-wire recovery should not expose a reject reason") && ok;
    ok = expect(!e2eRecoveredSender.loadOutgoingTransferState(nullptr),
                "successful E2E same-wire recovery should clear persisted state") && ok;
    ok = expect(!QFile::exists(e2eResumeCachePath(QString::fromLatin1(kE2ECacheResumeTransferId))),
                "successful E2E same-wire recovery should clear cached wire payload") && ok;

    const QString objectRoot = tempDir.filePath("e2e-object-recovery-store");
    qputenv("QTNETWORKCHAT_OBJECT_ROOT", objectRoot.toUtf8());
    FilesystemObjectStore objectStore(objectRoot);
    QString objectKey;
    QString objectHash;
    QString objectError;
    ok = expect(objectStore.writeObject(e2eWirePayload,
                                        &objectKey,
                                        &objectHash,
                                        &objectError,
                                        QStringLiteral("bin")),
                "E2E object recovery test should persist ciphertext in filesystem ObjectStore") && ok;
    ok = expect(objectHash == e2eWireHash,
                "E2E object recovery test object hash should match wire ciphertext hash") && ok;

    QJsonObject e2eObjectResumePolicy = e2eCacheRecoveryPolicy;
    e2eObjectResumePolicy.remove("e2eFileResumeCache");
    e2eObjectResumePolicy.remove("e2eFileResumeCacheFormat");
    e2eObjectResumePolicy.remove("e2eFileResumeCacheSha256");
    e2eObjectResumePolicy.remove("e2eFileResumeCacheSize");
    e2eObjectResumePolicy["e2eFileObjectRecoveryCandidate"] = true;
    e2eObjectResumePolicy["e2eFileObjectStoreKey"] = objectKey;
    e2eObjectResumePolicy["e2eFileObjectStoreType"] = "filesystem";
    e2eObjectResumePolicy["e2eFileObjectStoreHash"] = objectHash;
    e2eObjectResumePolicy["e2eFileObjectStoreSize"] = QString::number(e2eWirePayload.size());
    ok = expect(e2eRecoveredSender.saveOutgoingTransferState(QString::fromLatin1(kE2EObjectResumeTransferId),
                                                             e2ePlainFilePath,
                                                             "960002",
                                                             MessageType::File,
                                                             e2eWireHash,
                                                             e2eWirePayload.size(),
                                                             e2eWireChunkCount,
                                                             e2eObjectResumePolicy),
                "sender should persist E2E object recovery metadata with verified ciphertext object") && ok;
    ok = expect(QFile::remove(e2ePlainFilePath),
                "E2E object recovery should not depend on the original plaintext file") && ok;
    server.setE2EObjectResumeMetadata(e2eWirePayload.size(), e2eWireChunkCount, e2eWireHash);
    const QJsonObject e2eObjectReadyStatus =
        e2eRecoveredSender.savedOutgoingTransferRecoveryStatus();
    const QByteArray e2eObjectReadyStatusJson =
        QJsonDocument(e2eObjectReadyStatus).toJson(QJsonDocument::Compact);
    ok = expect(e2eObjectReadyStatus["configured"].toBool()
                    && e2eObjectReadyStatus["e2eFileEncrypted"].toBool()
                    && e2eObjectReadyStatus["e2eFileObjectRecoveryCandidate"].toBool()
                    && e2eObjectReadyStatus["e2eFileOfflineObjectRecoveryReady"].toBool()
                    && e2eObjectReadyStatus["e2eFileRecoverySessionReady"].toBool()
                    && e2eObjectReadyStatus["recoveryMode"].toString() == "resume"
                    && e2eObjectReadyStatus["canAutoResume"].toBool()
                    && e2eObjectReadyStatus["reason"].toString()
                        == "e2e-file-object-recovery-ready"
                    && e2eObjectReadyStatus["action"].toString()
                        == "resume-object-wire-envelope",
                "E2E object recovery should become auto-resumable after verified ciphertext readback") && ok;
    ok = expect(!e2eObjectReadyStatusJson.contains("\"ciphertext\"")
                    && !e2eObjectReadyStatusJson.contains(objectRoot.toUtf8())
                    && !e2eObjectReadyStatusJson.contains("file://")
                    && !e2eObjectReadyStatusJson.contains("privateKey")
                    && !e2eObjectReadyStatusJson.contains("sessionKey"),
                "E2E object recovery ready status should not export ciphertext, local paths, or secret material") && ok;
    const int e2eObjectQueriesBefore = server.e2eObjectResumeQueries();
    QString e2eObjectResumeReason;
    ok = expect(e2eRecoveredSender.resumeSavedOutgoingTransfer(&e2eObjectResumeReason, 5000),
                "E2E object recovery should resume encrypted file chunks from verified object ciphertext") && ok;
    ok = expect(server.e2eObjectResumeQueries() == e2eObjectQueriesBefore + 1,
                "E2E object recovery should query server resume state once") && ok;
    const QVector<qint64> e2eObjectChunks = server.e2eObjectResumeChunkIndexes();
    ok = expect(e2eObjectChunks.size() == 1 && e2eObjectChunks.last() == 2,
                "E2E object recovery should send only the missing ciphertext chunk") && ok;
    ok = expect(e2eObjectResumeReason.isEmpty(),
                "successful E2E object recovery should not expose a reject reason") && ok;
    ok = expect(!e2eRecoveredSender.loadOutgoingTransferState(nullptr),
                "successful E2E object recovery should clear persisted state") && ok;
    e2eRecoveredSender.disconnectFromServer();
    qunsetenv("QTNETWORKCHAT_OBJECT_ROOT");

    Client e2eS3RecoveredSender;
    e2eS3RecoveredSender.setUserInfo("950001", "RetrySender");
    e2eS3RecoveredSender.setAccountInfo("950001", "secret", false);
    ok = expect(e2eS3RecoveredSender.connectToServer("127.0.0.1", server.port()),
                "E2E S3 recovered sender should connect before reviewed S3 readback") && ok;
    ok = expect(e2eS3RecoveredSender.waitForLoginResult(5000),
                "E2E S3 recovered sender should log in before reviewed S3 readback") && ok;
    ok = expect(waitFor([&] {
        return e2eS3RecoveredSender.e2ePeerIdentityStatus("960002")
            .value("publicKeyFingerprintSha256").toString() == e2eFingerprint(peerPublicKey);
    }, 5000), "E2E S3 recovered sender should observe the peer identity before reviewed S3 readback") && ok;
    const QString e2eS3VerificationCode =
        e2eS3RecoveredSender.e2ePeerIdentityStatus("960002").value("verificationCode").toString();
    ok = expect(e2eS3RecoveredSender.pinE2EPeerIdentity("960002", e2eFingerprint(peerPublicKey))
                    && e2eS3RecoveredSender.verifyAndPinE2EPeerIdentity("960002", e2eS3VerificationCode),
                "E2E S3 recovered sender should trust the peer identity before reviewed S3 readback") && ok;
    e2eS3RecoveredSender.setE2ESessionKey("960002", "e2e-file-cache-session", e2eSessionKey);

    const QString s3ObjectKey = QStringLiteral("safeS3Object_456.bin");
    LoopbackS3ReadbackServer s3ReadbackServer(e2eWirePayload, s3ObjectKey);
    ok = expect(s3ReadbackServer.start(),
                "loopback S3 readback server should start for reviewed E2E S3 recovery") && ok;
    qputenv("QTNETWORKCHAT_E2E_S3_OBJECT_RECOVERY_REVIEWED", "1");
    qputenv("QTNETWORKCHAT_OBJECT_S3_ENABLE", "1");
    qputenv("QTNETWORKCHAT_OBJECT_S3_ENDPOINT",
            QStringLiteral("http://127.0.0.1:%1").arg(s3ReadbackServer.port()).toUtf8());
    qputenv("QTNETWORKCHAT_OBJECT_S3_BUCKET", "qtchat-e2e-test-bucket");
    qputenv("QTNETWORKCHAT_OBJECT_S3_REGION", "local");
    qputenv("QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY", "e2e-user-id");
    qputenv("QTNETWORKCHAT_OBJECT_S3_SECRET_KEY", "e2e-user-material");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_SESSION_TOKEN");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_PREFIX");
    qputenv("QTNETWORKCHAT_OBJECT_S3_TLS_VERIFY", "0");
    qputenv("QTNETWORKCHAT_OBJECT_S3_TIMEOUT_MS", "5000");

    QJsonObject e2eS3ResumePolicy = e2eCacheRecoveryPolicy;
    e2eS3ResumePolicy.remove("e2eFileResumeCache");
    e2eS3ResumePolicy.remove("e2eFileResumeCacheFormat");
    e2eS3ResumePolicy.remove("e2eFileResumeCacheSha256");
    e2eS3ResumePolicy.remove("e2eFileResumeCacheSize");
    e2eS3ResumePolicy["e2eFileObjectRecoveryCandidate"] = true;
    e2eS3ResumePolicy["e2eFileObjectStoreKey"] = s3ObjectKey;
    e2eS3ResumePolicy["e2eFileObjectStoreType"] = "s3";
    e2eS3ResumePolicy["e2eFileObjectStoreHash"] = e2eWireHash;
    e2eS3ResumePolicy["e2eFileObjectStoreSize"] = QString::number(e2eWirePayload.size());
    ok = expect(e2eS3RecoveredSender.saveOutgoingTransferState(QString::fromLatin1(kE2EObjectResumeTransferId),
                                                               e2ePlainFilePath,
                                                               "960002",
                                                               MessageType::File,
                                                               e2eWireHash,
                                                               e2eWirePayload.size(),
                                                               e2eWireChunkCount,
                                                               e2eS3ResumePolicy),
                "sender should persist E2E S3 recovery metadata with reviewed ciphertext object") && ok;
    server.setE2EObjectResumeMetadata(e2eWirePayload.size(), e2eWireChunkCount, e2eWireHash);
    const QJsonObject e2eS3ReadyStatus =
        e2eS3RecoveredSender.savedOutgoingTransferRecoveryStatus();
    const QByteArray e2eS3ReadyStatusJson =
        QJsonDocument(e2eS3ReadyStatus).toJson(QJsonDocument::Compact);
    ok = expect(e2eS3ReadyStatus["configured"].toBool()
                    && e2eS3ReadyStatus["e2eFileEncrypted"].toBool()
                    && e2eS3ReadyStatus["e2eFileObjectRecoveryCandidate"].toBool()
                    && e2eS3ReadyStatus["e2eFileObjectStoreType"].toString() == "s3"
                    && e2eS3ReadyStatus["e2eFileObjectRecoveryScope"].toString()
                        == "s3-object-ciphertext-readback"
                    && e2eS3ReadyStatus["e2eFileObjectRecoveryReviewGate"].toString()
                        == "s3-object-ciphertext-readback-reviewed"
                    && e2eS3ReadyStatus["e2eFileOfflineObjectRecoveryReady"].toBool()
                    && e2eS3ReadyStatus["recoveryMode"].toString() == "resume"
                    && e2eS3ReadyStatus["canAutoResume"].toBool()
                    && e2eS3ReadyStatus["reason"].toString()
                        == "e2e-file-object-recovery-ready"
                    && e2eS3ReadyStatus["action"].toString()
                        == "resume-object-wire-envelope",
                "E2E reviewed S3 object recovery should become auto-resumable after verified ciphertext readback") && ok;
    ok = expect(!e2eS3ReadyStatusJson.contains("qtchat-e2e-test-bucket")
                    && !e2eS3ReadyStatusJson.contains("127.0.0.1")
                    && !e2eS3ReadyStatusJson.contains("Authorization")
                    && !e2eS3ReadyStatusJson.contains("e2e-user-id")
                    && !e2eS3ReadyStatusJson.contains("e2e-user-material")
                    && !e2eS3ReadyStatusJson.contains("\"ciphertext\"")
                    && !e2eS3ReadyStatusJson.contains("privateKey")
                    && !e2eS3ReadyStatusJson.contains("sessionKey"),
                "E2E reviewed S3 ready status should not export endpoints, buckets, credentials, ciphertext, or secret material") && ok;
    ok = expect(!e2eS3ReadyStatus.contains("filePath")
                    && e2eS3ReadyStatus["fileName"].toString() == QFileInfo(e2ePlainFilePath).fileName(),
                "E2E reviewed S3 ready status should not export the local plaintext path") && ok;
    ok = expect(s3ReadbackServer.headCount() >= 1 && s3ReadbackServer.getCount() >= 1,
                "E2E reviewed S3 recovery status should validate and read ciphertext through HEAD/GET") && ok;
    const int e2eS3ObjectQueriesBefore = server.e2eObjectResumeQueries();
    const int e2eS3HeadBeforeResume = s3ReadbackServer.headCount();
    const int e2eS3GetBeforeResume = s3ReadbackServer.getCount();
    QString e2eS3ObjectResumeReason;
    ok = expect(e2eS3RecoveredSender.resumeSavedOutgoingTransfer(&e2eS3ObjectResumeReason, 5000),
                "E2E reviewed S3 object recovery should resume encrypted file chunks from verified ciphertext") && ok;
    ok = expect(server.e2eObjectResumeQueries() == e2eS3ObjectQueriesBefore + 1,
                "E2E reviewed S3 object recovery should query server resume state once") && ok;
    const QVector<qint64> e2eS3ObjectChunks = server.e2eObjectResumeChunkIndexes();
    ok = expect(!e2eS3ObjectChunks.isEmpty() && e2eS3ObjectChunks.last() == 2,
                "E2E reviewed S3 object recovery should send only the missing ciphertext chunk") && ok;
    ok = expect(e2eS3ObjectResumeReason.isEmpty(),
                "successful E2E reviewed S3 recovery should not expose a reject reason") && ok;
    ok = expect(s3ReadbackServer.headCount() >= e2eS3HeadBeforeResume + 1
                    && s3ReadbackServer.getCount() >= e2eS3GetBeforeResume + 1,
                "E2E reviewed S3 recovery execution should revalidate and read ciphertext before sending") && ok;
    ok = expect(!e2eS3RecoveredSender.loadOutgoingTransferState(nullptr),
                "successful E2E reviewed S3 recovery should clear persisted state") && ok;
    e2eS3RecoveredSender.disconnectFromServer();
    qunsetenv("QTNETWORKCHAT_E2E_S3_OBJECT_RECOVERY_REVIEWED");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_ENABLE");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_ENDPOINT");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_BUCKET");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_REGION");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_SECRET_KEY");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_SESSION_TOKEN");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_PREFIX");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_TLS_VERIFY");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_TIMEOUT_MS");

    Client e2eOfflineRecoveredSender;
    e2eOfflineRecoveredSender.setUserInfo("950001", "RetrySender");
    e2eOfflineRecoveredSender.setAccountInfo("950001", "secret", false);
    ok = expect(e2eOfflineRecoveredSender.connectToServer("127.0.0.1", server.port()),
                "E2E offline recovered sender should connect before reviewed offline readback") && ok;
    ok = expect(e2eOfflineRecoveredSender.waitForLoginResult(5000),
                "E2E offline recovered sender should log in before reviewed offline readback") && ok;
    ok = expect(waitFor([&] {
        return e2eOfflineRecoveredSender.e2ePeerIdentityStatus("960002")
            .value("publicKeyFingerprintSha256").toString() == e2eFingerprint(peerPublicKey);
    }, 5000), "E2E offline recovered sender should observe the peer identity before reviewed offline readback") && ok;
    const QString e2eOfflineVerificationCode =
        e2eOfflineRecoveredSender.e2ePeerIdentityStatus("960002").value("verificationCode").toString();
    ok = expect(e2eOfflineRecoveredSender.pinE2EPeerIdentity("960002", e2eFingerprint(peerPublicKey))
                    && e2eOfflineRecoveredSender.verifyAndPinE2EPeerIdentity("960002", e2eOfflineVerificationCode),
                "E2E offline recovered sender should trust the peer identity before reviewed offline readback") && ok;
    e2eOfflineRecoveredSender.setE2ESessionKey("960002", "e2e-file-cache-session", e2eSessionKey);

    const QString offlineMirrorRoot = tempDir.filePath("e2e-offline-ciphertext-mirror");
    ok = expect(QDir().mkpath(offlineMirrorRoot),
                "reviewed E2E offline readback mirror root should be created") && ok;
    const QString offlineObjectKey = QStringLiteral("safeOfflineObject_456.bin");
    QFile offlineMirrorFile(QDir(offlineMirrorRoot).filePath(offlineObjectKey));
    ok = expect(offlineMirrorFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
                "reviewed E2E offline readback mirror ciphertext should be writable") && ok;
    ok = expect(offlineMirrorFile.write(e2eWirePayload) == e2eWirePayload.size(),
                "reviewed E2E offline readback mirror should contain the ciphertext payload") && ok;
    offlineMirrorFile.close();
    qputenv("QTNETWORKCHAT_E2E_OFFLINE_OBJECT_RECOVERY_REVIEWED", "1");
    qputenv("QTNETWORKCHAT_E2E_OFFLINE_OBJECT_RECOVERY_ROOT", offlineMirrorRoot.toUtf8());

    QJsonObject e2eOfflineResumePolicy = e2eCacheRecoveryPolicy;
    e2eOfflineResumePolicy.remove("e2eFileResumeCache");
    e2eOfflineResumePolicy.remove("e2eFileResumeCacheFormat");
    e2eOfflineResumePolicy.remove("e2eFileResumeCacheSha256");
    e2eOfflineResumePolicy.remove("e2eFileResumeCacheSize");
    e2eOfflineResumePolicy["e2eFileObjectRecoveryCandidate"] = true;
    e2eOfflineResumePolicy["e2eFileObjectStoreKey"] = offlineObjectKey;
    e2eOfflineResumePolicy["e2eFileObjectStoreType"] = "offline";
    e2eOfflineResumePolicy["e2eFileObjectStoreHash"] = e2eWireHash;
    e2eOfflineResumePolicy["e2eFileObjectStoreSize"] = QString::number(e2eWirePayload.size());
    ok = expect(e2eOfflineRecoveredSender.saveOutgoingTransferState(QString::fromLatin1(kE2EOfflineResumeTransferId),
                                                                    e2ePlainFilePath,
                                                                    "960002",
                                                                    MessageType::File,
                                                                    e2eWireHash,
                                                                    e2eWirePayload.size(),
                                                                    e2eWireChunkCount,
                                                                    e2eOfflineResumePolicy),
                "sender should persist E2E offline recovery metadata with reviewed ciphertext mirror") && ok;
    server.setE2EOfflineResumeMetadata(e2eWirePayload.size(), e2eWireChunkCount, e2eWireHash);
    const QJsonObject e2eOfflineReadyStatus =
        e2eOfflineRecoveredSender.savedOutgoingTransferRecoveryStatus();
    const QByteArray e2eOfflineReadyStatusJson =
        QJsonDocument(e2eOfflineReadyStatus).toJson(QJsonDocument::Compact);
    ok = expect(e2eOfflineReadyStatus["configured"].toBool()
                    && e2eOfflineReadyStatus["e2eFileEncrypted"].toBool()
                    && e2eOfflineReadyStatus["e2eFileObjectRecoveryCandidate"].toBool()
                    && e2eOfflineReadyStatus["e2eFileObjectStoreType"].toString() == "offline"
                    && e2eOfflineReadyStatus["e2eFileObjectRecoveryScope"].toString()
                        == "offline-ciphertext-readback"
                    && e2eOfflineReadyStatus["e2eFileObjectRecoveryReviewGate"].toString()
                        == "offline-ciphertext-readback-reviewed"
                    && e2eOfflineReadyStatus["e2eFileOfflineObjectRecoveryReady"].toBool()
                    && e2eOfflineReadyStatus["recoveryMode"].toString() == "resume"
                    && e2eOfflineReadyStatus["canAutoResume"].toBool()
                    && e2eOfflineReadyStatus["reason"].toString()
                        == "e2e-file-object-recovery-ready"
                    && e2eOfflineReadyStatus["action"].toString()
                        == "resume-object-wire-envelope",
                "E2E reviewed offline object recovery should become auto-resumable after verified ciphertext mirror readback") && ok;
    ok = expect(!e2eOfflineReadyStatusJson.contains(offlineMirrorRoot.toUtf8())
                    && !e2eOfflineReadyStatusJson.contains("offline_files")
                    && !e2eOfflineReadyStatusJson.contains("file://")
                    && !e2eOfflineReadyStatusJson.contains("\"ciphertext\"")
                    && !e2eOfflineReadyStatusJson.contains("privateKey")
                    && !e2eOfflineReadyStatusJson.contains("sessionKey"),
                "E2E reviewed offline ready status should not export mirror paths, ciphertext, or secret material") && ok;
    ok = expect(!e2eOfflineReadyStatus.contains("filePath")
                    && e2eOfflineReadyStatus["fileName"].toString() == QFileInfo(e2ePlainFilePath).fileName(),
                "E2E reviewed offline ready status should not export the local plaintext path") && ok;
    const int e2eOfflineObjectQueriesBefore = server.e2eOfflineResumeQueries();
    QString e2eOfflineObjectResumeReason;
    ok = expect(e2eOfflineRecoveredSender.resumeSavedOutgoingTransfer(&e2eOfflineObjectResumeReason, 5000),
                "E2E reviewed offline object recovery should resume encrypted file chunks from verified ciphertext mirror") && ok;
    ok = expect(server.e2eOfflineResumeQueries() == e2eOfflineObjectQueriesBefore + 1,
                "E2E reviewed offline object recovery should query server resume state once") && ok;
    const QVector<qint64> e2eOfflineObjectChunks = server.e2eOfflineResumeChunkIndexes();
    ok = expect(!e2eOfflineObjectChunks.isEmpty() && e2eOfflineObjectChunks.last() == 2,
                "E2E reviewed offline object recovery should send only the missing ciphertext chunk") && ok;
    ok = expect(e2eOfflineObjectResumeReason.isEmpty(),
                "successful E2E reviewed offline recovery should not expose a reject reason") && ok;
    ok = expect(!e2eOfflineRecoveredSender.loadOutgoingTransferState(nullptr),
                "successful E2E reviewed offline recovery should clear persisted state") && ok;

    ok = expect(e2eOfflineRecoveredSender.saveOutgoingTransferState(QString::fromLatin1(kE2EOfflineResumeTransferId),
                                                                    e2ePlainFilePath,
                                                                    "960002",
                                                                    MessageType::File,
                                                                    e2eWireHash,
                                                                    e2eWirePayload.size(),
                                                                    e2eWireChunkCount,
                                                                    e2eOfflineResumePolicy),
                "sender should persist reviewed offline recovery metadata before unsafe locator mutation") && ok;
    QJsonObject legacyUnsafeOfflineState;
    ok = expect(e2eOfflineRecoveredSender.loadOutgoingTransferState(&legacyUnsafeOfflineState),
                "test should load reviewed offline recovery state before unsafe locator mutation") && ok;
    legacyUnsafeOfflineState["e2eFileObjectStoreKey"] =
        QStringLiteral("../outside/") + offlineObjectKey;
    legacyUnsafeOfflineState["e2eFileObjectRecoveryReason"] = "resume-object-wire-envelope";
    legacyUnsafeOfflineState["e2eFileObjectRecoveryAction"] = "resume-object-wire-envelope";
    ok = expect(writeOutgoingTransferState(legacyUnsafeOfflineState),
                "test should write legacy unsafe E2E offline recovery metadata") && ok;
    const QJsonObject legacyUnsafeOfflineStatus =
        e2eOfflineRecoveredSender.savedOutgoingTransferRecoveryStatus();
    const QByteArray legacyUnsafeOfflineStatusJson =
        QJsonDocument(legacyUnsafeOfflineStatus).toJson(QJsonDocument::Compact);
    ok = expect(legacyUnsafeOfflineStatus["configured"].toBool()
                    && legacyUnsafeOfflineStatus["e2eFileObjectRecoveryCandidate"].toBool()
                    && !legacyUnsafeOfflineStatus["e2eFileObjectStoreKeySafe"].toBool(true)
                    && legacyUnsafeOfflineStatus["e2eFileObjectStoreKeySuppressed"].toBool(false)
                    && !legacyUnsafeOfflineStatus.contains("e2eFileObjectStoreKey")
                    && legacyUnsafeOfflineStatus["e2eFileObjectStoreType"].toString() == "offline"
                    && legacyUnsafeOfflineStatus["e2eFileObjectRecoveryScope"].toString()
                        == "offline-ciphertext-readback"
                    && legacyUnsafeOfflineStatus["e2eFileObjectRecoveryReviewGate"].toString()
                        == "object-store-key-token-invalid"
                    && legacyUnsafeOfflineStatus["reason"].toString()
                        == "e2e-file-object-recovery-evidence-invalid"
                    && legacyUnsafeOfflineStatus["action"].toString()
                        == "suppress-object-key-and-resend"
                    && !legacyUnsafeOfflineStatus["canAutoResume"].toBool(),
                "reviewed E2E offline recovery should suppress unsafe mirror locators and fail closed") && ok;
    ok = expect(!legacyUnsafeOfflineStatusJson.contains("../")
                    && !legacyUnsafeOfflineStatusJson.contains("outside")
                    && !legacyUnsafeOfflineStatusJson.contains(offlineMirrorRoot.toUtf8())
                    && !legacyUnsafeOfflineStatusJson.contains("file://")
                    && !legacyUnsafeOfflineStatusJson.contains("resume-object-wire-envelope")
                    && !legacyUnsafeOfflineStatusJson.contains("\"ciphertext\"")
                    && !legacyUnsafeOfflineStatusJson.contains("privateKey")
                    && !legacyUnsafeOfflineStatusJson.contains("sessionKey"),
                "reviewed E2E offline unsafe status should not export mirror paths, stale resume actions, ciphertext, or secret material") && ok;
    const int queriesBeforeUnsafeOfflineRecovery = server.e2eOfflineResumeQueries();
    const int chunksBeforeUnsafeOfflineRecovery = server.e2eOfflineResumeChunkIndexes().size();
    QString legacyUnsafeOfflineReason;
    ok = expect(!e2eOfflineRecoveredSender.resumeSavedOutgoingTransfer(&legacyUnsafeOfflineReason, 5000),
                "legacy unsafe reviewed E2E offline recovery state should refuse automatic resume") && ok;
    ok = expect(legacyUnsafeOfflineReason == "e2e-file-object-recovery-evidence-invalid",
                "legacy unsafe reviewed E2E offline recovery refusal should expose the fixed evidence-invalid reason") && ok;
    ok = expect(server.e2eOfflineResumeQueries() == queriesBeforeUnsafeOfflineRecovery
                    && server.e2eOfflineResumeChunkIndexes().size() == chunksBeforeUnsafeOfflineRecovery,
                "legacy unsafe reviewed E2E offline recovery state should not query or send") && ok;
    ok = expect(e2eOfflineRecoveredSender.clearOutgoingTransferState(),
                "sender should clear unsafe reviewed E2E offline recovery state before continuing") && ok;
    e2eOfflineRecoveredSender.disconnectFromServer();
    qunsetenv("QTNETWORKCHAT_E2E_OFFLINE_OBJECT_RECOVERY_REVIEWED");
    qunsetenv("QTNETWORKCHAT_E2E_OFFLINE_OBJECT_RECOVERY_ROOT");

    ok = expect(sender.saveOutgoingTransferState(QString::fromLatin1(kMismatchResumeTransferId),
                                                 resumeFilePath,
                                                 QString(),
                                                 MessageType::File,
                                                 resumeFileHash,
                                                 resumeFileSize,
                                                 resumeChunkCount),
                "sender should persist state for failed recovery") && ok;
    QString failedSavedResumeReason;
    ok = expect(!sender.resumeSavedOutgoingTransfer(&failedSavedResumeReason, 5000),
                "sender should keep saved transfer state when recovery fails") && ok;
    ok = expect(!failedSavedResumeReason.isEmpty(),
                "failed saved transfer recovery should expose a reject reason") && ok;
    ok = expect(sender.loadOutgoingTransferState(nullptr),
                "failed saved transfer recovery should keep persisted state") && ok;
    ok = expect(sender.clearOutgoingTransferState(),
                "sender should clear failed recovery state before continuing the test") && ok;
    ok = expect(sender.saveOutgoingTransferState(QString::fromLatin1(kQueryAndResumeTransferId),
                                                 resumeFilePath,
                                                 QString(),
                                                 MessageType::File,
                                                 "stale-local-hash",
                                                 resumeFileSize,
                                                 resumeChunkCount),
                "sender should persist state with stale local metadata for validation") && ok;
    const int chunksBeforeStaleStateResume = server.resumedChunkIndexes().size();
    QString staleStateReason;
    ok = expect(!sender.resumeSavedOutgoingTransfer(&staleStateReason, 5000),
                "sender should reject saved transfer state when local metadata no longer matches") && ok;
    ok = expect(!staleStateReason.isEmpty(),
                "stale saved transfer state should expose a reject reason") && ok;
    ok = expect(server.resumedChunkIndexes().size() == chunksBeforeStaleStateResume,
                "stale saved transfer state should not send resumed chunks") && ok;
    ok = expect(sender.loadOutgoingTransferState(nullptr),
                "stale saved transfer state should remain for user-visible recovery handling") && ok;
    ok = expect(sender.clearOutgoingTransferState(),
                "sender should clear stale recovery state before continuing the test") && ok;
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
    ok = expect(mismatchReason == QString::fromUtf8("续传文件哈希不一致"),
                "metadata mismatch should expose the hash mismatch reason") && ok;
    ok = expect(server.resumedChunkIndexes().size() == queryResumedChunks.size(),
                "metadata mismatch should not send any resumed chunks") && ok;
    QString chunkSizeMismatchReason;
    ok = expect(!sender.queryAndResumeFileTransfer(resumeFilePath,
                                                   QString::fromLatin1(kChunkSizeMismatchResumeTransferId),
                                                   QString(),
                                                   MessageType::File,
                                                   &chunkSizeMismatchReason,
                                                   5000),
                "sender should reject query-and-resume when chunk size does not match") && ok;
    ok = expect(chunkSizeMismatchReason == QString::fromUtf8("续传分片大小不一致"),
                "chunk size mismatch should expose a specific reject reason") && ok;
    ok = expect(server.resumedChunkIndexes().size() == queryResumedChunks.size(),
                "chunk size mismatch should not send any resumed chunks") && ok;
    QString chunkCountMismatchReason;
    ok = expect(!sender.queryAndResumeFileTransfer(resumeFilePath,
                                                   QString::fromLatin1(kChunkCountMismatchResumeTransferId),
                                                   QString(),
                                                   MessageType::File,
                                                   &chunkCountMismatchReason,
                                                   5000),
                "sender should reject query-and-resume when chunk count does not match") && ok;
    ok = expect(chunkCountMismatchReason == QString::fromUtf8("续传分片数量不一致"),
                "chunk count mismatch should expose a specific reject reason") && ok;
    ok = expect(server.resumedChunkIndexes().size() == queryResumedChunks.size(),
                "chunk count mismatch should not send any resumed chunks") && ok;
    QString invalidProgressReason;
    ok = expect(!sender.queryAndResumeFileTransfer(resumeFilePath,
                                                   QString::fromLatin1(kInvalidProgressResumeTransferId),
                                                   QString(),
                                                   MessageType::File,
                                                   &invalidProgressReason,
                                                   5000),
                "sender should reject query-and-resume when progress is invalid") && ok;
    ok = expect(invalidProgressReason == QString::fromUtf8("续传进度非法"),
                "invalid progress should expose a specific reject reason") && ok;
    ok = expect(server.resumedChunkIndexes().size() == queryResumedChunks.size(),
                "invalid progress should not send any resumed chunks") && ok;
    const int chunksBeforeGapResume = server.resumedChunkIndexes().size();
    QString gapReason;
    ok = expect(sender.queryAndResumeFileTransfer(resumeFilePath,
                                                  QString::fromLatin1(kGapResumeTransferId),
                                                  QString(),
                                                  MessageType::File,
                                                  &gapReason,
                                                  5000),
                "sender should infer the earliest missing chunk from received chunks") && ok;
    const QVector<qint64> gapResumedChunks = server.resumedChunkIndexes();
    ok = expect(gapResumedChunks.size() == chunksBeforeGapResume + 1 && gapResumedChunks.last() == 1,
                "query-and-resume should resend the earliest missing chunk and skip later confirmed chunks") && ok;
    ok = expect(gapReason.isEmpty(), "accepted gap resume should not expose a reject reason") && ok;

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
                "sender should infer missing chunk from auto resume state and skip later confirmed chunks") && ok;
    const QVector<qint64> gapAutoResumeChunks = server.gapAutoResumeChunkIndexes();
    ok = expect(server.gapAutoResumeQueries() == 1,
                "sender should query the gap auto resume state once") && ok;
    ok = expect(gapAutoResumeChunks.size() == 1 && gapAutoResumeChunks[0] == 1,
                "auto resume should resend the earliest missing chunk and skip later confirmed chunks") && ok;
    const QVector<qint64> gapAutoResumeProgress =
        progressByFileName.value(QString::fromLatin1(kAckTimeoutGapResumeFileName));
    ok = expect(gapAutoResumeProgress.contains(gapAutoResumeFileSize - kClientChunkBytes),
                "auto resume progress should use received chunk bytes instead of a non-contiguous high watermark") && ok;

    const QString middleGapAutoResumePath = tempDir.filePath(QString::fromLatin1(kAckTimeoutMiddleGapResumeFileName));
    qint64 middleGapAutoResumeFileSize = 0;
    ok = expect(writeResumeFile(middleGapAutoResumePath, &middleGapAutoResumeFileSize),
                "middle-gap auto resume test file should be created") && ok;
    ok = expect(sender.sendFile(middleGapAutoResumePath),
                "sender should reuse auto resume state when the current chunk is still missing") && ok;
    const QVector<qint64> middleGapAutoResumeChunks = server.middleGapAutoResumeChunkIndexes();
    ok = expect(server.middleGapAutoResumeQueries() == 1,
                "sender should query the middle-gap auto resume state once") && ok;
    ok = expect(middleGapAutoResumeChunks.size() == 1 && middleGapAutoResumeChunks[0] == 1,
                "auto resume should resend only the missing middle chunk and skip later confirmed chunks") && ok;

    const QString completeAutoResumePath = tempDir.filePath(QString::fromLatin1(kAckTimeoutCompleteResumeFileName));
    qint64 completeAutoResumeFileSize = 0;
    ok = expect(writeResumeFile(completeAutoResumePath, &completeAutoResumeFileSize),
                "complete auto resume test file should be created") && ok;
    ok = expect(sender.sendFile(completeAutoResumePath),
                "sender should accept a complete resume state after an ack timeout") && ok;
    ok = expect(server.completeAutoResumeQueries() == 1,
                "sender should query the complete auto resume state once") && ok;
    ok = expect(server.completeAutoResumeChunkIndexes().isEmpty(),
                "complete auto resume should not resend already confirmed chunks") && ok;
    ok = expect(!sender.loadOutgoingTransferState(nullptr),
                "complete auto resume should clear persisted outgoing transfer state") && ok;

    sender.disconnectFromServer();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}

#include "file_chunk_retry_progress_test.moc"
