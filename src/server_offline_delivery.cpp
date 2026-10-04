#include "server.h"
#include "server_database.h"
#include "server_delivery_support.h"
#include "objectstore.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRandomGenerator>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>

namespace {
using namespace ServerDatabase;
using namespace ServerDeliverySupport;

constexpr qint64 kDefaultOfflineAttachmentTtlDays = 14;
constexpr qint64 kMaxOfflineAttachmentTtlDays = 3650;
constexpr qint64 kDefaultOfflineAttachmentResumeProgressTtlHours = 24;
constexpr qint64 kMaxOfflineAttachmentResumeProgressTtlHours = 24LL * 365;
constexpr qint64 kDefaultOfflineAttachmentQuotaBytes = 512LL * 1024 * 1024;

qint64 offlineAttachmentResumeProgressTtlMs() {
    const qint64 hours = positiveIntegerEnvOrDefault("QTNETWORKCHAT_OFFLINE_RESUME_TTL_HOURS",
                                                     kDefaultOfflineAttachmentResumeProgressTtlHours,
                                                     kMaxOfflineAttachmentResumeProgressTtlHours);
    return hours * 60LL * 60 * 1000;
}

qint64 offlineAttachmentTtlMs() {
    const qint64 days = positiveIntegerEnvOrDefault("QTNETWORKCHAT_OFFLINE_ATTACHMENT_TTL_DAYS",
                                                    kDefaultOfflineAttachmentTtlDays,
                                                    kMaxOfflineAttachmentTtlDays);
    return days * 24LL * 60 * 60 * 1000;
}

QString safePathPart(const QString& value) {
    QString safe;
    safe.reserve(value.size());
    for (const QChar& ch : value) {
        if (ch.isLetterOrNumber()
            || ch == QLatin1Char('_')
            || ch == QLatin1Char('-')
            || ch == QLatin1Char('.')) {
            safe.append(ch);
        } else {
            safe.append(QLatin1Char('_'));
        }
    }
    return safe.isEmpty() ? "unknown" : safe;
}

enum class OfflineAttachmentValidationResult {
    Ready,
    CleanedBadState,
    Blocked
};

OfflineAttachmentValidationResult sendOfflineAttachmentBadStateNotice(QTcpSocket* socket,
                                                                      const QString& filePath,
                                                                      const QString& fileName,
                                                                      const QString& reason) {
    sendSystemNotice(socket, QString("%1：%2，请让对方重新发送。").arg(reason, fileName));
    QFile::remove(filePath);
    return OfflineAttachmentValidationResult::CleanedBadState;
}

OfflineAttachmentValidationResult validateOfflineAttachmentForReplay(const QJsonObject& obj,
                                                                     const QString& filePath,
                                                                     QTcpSocket* socket) {
    const QString fileName = obj["fileName"].toString("未命名文件");
    const QFileInfo attachmentInfo(filePath);
    if (!attachmentInfo.exists() || !attachmentInfo.isFile()) {
        return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件已丢失");
    }
    if (attachmentInfo.size() <= 0) {
        return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件为空");
    }

    const qint64 declaredSize = obj["fileSize"].toVariant().toLongLong();
    if (declaredSize > 0 && declaredSize != attachmentInfo.size()) {
        return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件大小异常");
    }

    const qint64 declaredChunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 declaredChunkCount = obj["chunkCount"].toVariant().toLongLong();
    if (declaredChunkSize <= 0
        || declaredChunkSize > kForwardChunkBytes
        || declaredChunkCount <= 0
        || declaredChunkCount != (attachmentInfo.size() + declaredChunkSize - 1) / declaredChunkSize) {
        return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件分片元数据异常");
    }

    const QString declaredHash = obj["fileHash"].toString().trimmed();
    if (looksLikeSha256Hex(declaredHash)) {
        QFile hashFile(filePath);
        if (!hashFile.open(QIODevice::ReadOnly)) {
            return OfflineAttachmentValidationResult::Blocked;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&hashFile)) {
            return OfflineAttachmentValidationResult::Blocked;
        }
        const QString actualHash = QString::fromLatin1(hash.result().toHex());
        hashFile.close();
        if (actualHash.compare(declaredHash, Qt::CaseInsensitive) != 0) {
            return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件校验失败");
        }
    }

    if (obj["e2eFileEncrypted"].toBool(false)) {
        QJsonObject e2eEvidence;
        appendE2EFileFields(&e2eEvidence, obj);
        if (!e2eEvidence.value("e2eFileEncrypted").toBool(false)
            || !e2eEvidence.value("e2eEnvelope").isObject()) {
            return sendOfflineAttachmentBadStateNotice(socket,
                                                       filePath,
                                                       fileName,
                                                       "端到端加密离线文件恢复证据无效");
        }
    }

    return OfflineAttachmentValidationResult::Ready;
}

struct OfflineAttachmentReplayPlan {
    qint64 startChunkIndex = 0;
    bool confirmedChunksValid = false;
    QSet<qint64> confirmedChunkIndexes;
};

OfflineAttachmentReplayPlan buildOfflineAttachmentReplayPlan(const QJsonObject& obj,
                                                             qint64 totalBytes,
                                                             qint64 chunkSize,
                                                             qint64 chunkCount,
                                                             qint64 sqliteMessageId) {
    OfflineAttachmentReplayPlan plan;
    const qint64 declaredChunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 declaredChunkCount = obj["chunkCount"].toVariant().toLongLong();
    const qint64 recordedConfirmedBytes = obj["confirmedBytes"].toVariant().toLongLong();
    const QJsonArray confirmedChunks = obj["confirmedChunks"].toArray();
    const bool hasResumeProgress = recordedConfirmedBytes > 0 || !confirmedChunks.isEmpty();
    bool resumeProgressFresh = !hasResumeProgress;
    if (hasResumeProgress) {
        const QDateTime resumeUpdatedAt = QDateTime::fromString(obj["resumeUpdatedAt"].toString(), Qt::ISODate);
        if (resumeUpdatedAt.isValid()) {
            const qint64 ageMs = resumeUpdatedAt.toUTC().msecsTo(QDateTime::currentDateTimeUtc());
            resumeProgressFresh = ageMs >= 0 && ageMs <= offlineAttachmentResumeProgressTtlMs();
        }
    }

    plan.confirmedChunksValid = resumeProgressFresh
        && sqliteMessageId > 0
        && declaredChunkSize == chunkSize
        && declaredChunkCount == chunkCount;
    for (const QJsonValue& value : confirmedChunks) {
        const qint64 chunkIndex = value.toVariant().toLongLong();
        if (chunkIndex < 0 || chunkIndex >= chunkCount) {
            plan.confirmedChunksValid = false;
            plan.confirmedChunkIndexes.clear();
            break;
        }
        plan.confirmedChunkIndexes.insert(chunkIndex);
    }

    const bool canResumeFromConfirmedBytes = resumeProgressFresh
        && sqliteMessageId > 0
        && declaredChunkSize == chunkSize
        && (declaredChunkCount <= 0 || declaredChunkCount == chunkCount)
        && recordedConfirmedBytes > 0
        && recordedConfirmedBytes <= totalBytes
        && recordedConfirmedBytes % chunkSize == 0;
    plan.startChunkIndex = canResumeFromConfirmedBytes
        ? qMin(recordedConfirmedBytes / chunkSize, chunkCount)
        : 0;
    if (plan.confirmedChunksValid && !plan.confirmedChunkIndexes.isEmpty()) {
        plan.startChunkIndex = chunkCount;
        for (qint64 index = 0; index < chunkCount; ++index) {
            if (!plan.confirmedChunkIndexes.contains(index)) {
                plan.startChunkIndex = index;
                break;
            }
        }
    }
    return plan;
}

} // namespace

QString Server::offlineFilePath(const QString& userId) const {
    QString baseDir = appDataDir();
    if (baseDir.isEmpty()) baseDir = ".";
    QString dir = baseDir + "/offline";
    QDir().mkpath(dir);
    return dir + "/" + userId + ".jsonl";
}

QString Server::offlineAttachmentRootDir() const {
    QString baseDir = appDataDir();
    if (baseDir.isEmpty()) baseDir = ".";
    const QString dir = baseDir + "/offline_files";
    QDir().mkpath(dir);
    return dir;
}

QString Server::offlineAttachmentDir(const QString& userId) const {
    const QString dir = offlineAttachmentRootDir() + "/" + safePathPart(userId);
    QDir().mkpath(dir);
    return dir;
}

qint64 Server::offlineAttachmentQuotaBytes() const {
    const qint64 quotaMb = positiveIntegerEnvOrDefault("QTNETWORKCHAT_OFFLINE_ATTACHMENT_QUOTA_MB",
                                                       kDefaultOfflineAttachmentQuotaBytes / (1024 * 1024));
    return quotaMb * 1024 * 1024;
}

qint64 Server::offlineAttachmentUsedBytes() const {
    const QString rootDirPath = offlineAttachmentRootDir();
    qint64 usedBytes = 0;
    QDirIterator it(rootDirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        usedBytes += QFileInfo(it.next()).size();
    }
    return usedBytes;
}

bool Server::hasOfflineAttachmentCapacity(qint64 incomingBytes) const {
    if (incomingBytes <= 0) return false;

    const qint64 quotaBytes = offlineAttachmentQuotaBytes();
    return incomingBytes <= quotaBytes && offlineAttachmentUsedBytes() <= quotaBytes - incomingBytes;
}

QString Server::saveOfflineAttachment(const Message& msg) const {
    if (msg.receiverId.isEmpty() || msg.fileData.isEmpty()) return {};
    if (!hasOfflineAttachmentCapacity(msg.fileData.size())) {
        qWarning() << "Offline attachment quota exceeded for" << msg.receiverId
                   << "file:" << msg.fileName
                   << "size:" << msg.fileData.size()
                   << "quota:" << offlineAttachmentQuotaBytes();
        return {};
    }

    const QString dir = offlineAttachmentDir(msg.receiverId);
    for (int attempt = 0; attempt < 5; ++attempt) {
        const QString filePath = QString("%1/%2_%3.bin")
            .arg(dir,
                 QString::number(QDateTime::currentMSecsSinceEpoch()),
                 QString::number(QRandomGenerator::global()->generate()));
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) continue;

        const qint64 written = file.write(msg.fileData);
        file.close();
        if (written == msg.fileData.size()) {
            return filePath;
        }
        file.remove();
    }
    return {};
}

QSet<QString> Server::collectReferencedOfflineAttachments() const {
    QSet<QString> referencedPaths;
    auto collectPayload = [&referencedPaths](const QByteArray& payload) {
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject()) return;

        const QString filePath = doc.object()["offlineFilePath"].toString();
        if (!filePath.isEmpty()) {
            referencedPaths.insert(QDir::cleanPath(filePath));
        }
    };

    if (ensureAccountDatabase()) {
        QString connectionName = "offline_refs_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = openAccountDatabase(connectionName);
            if (openAccountDatabaseConnection(db, connectionName)) {
                QSqlQuery query(db);
                if (query.exec("SELECT payload FROM offline_messages ORDER BY id ASC")) {
                    while (query.next()) {
                        collectPayload(query.value(0).toString().toUtf8());
                    }
                }
                db.close();
            }
        }
        releaseAccountDatabase(connectionName);
    }

    QString baseDir = appDataDir();
    if (baseDir.isEmpty()) baseDir = ".";
    QDir offlineDir(baseDir + "/offline");
    const QFileInfoList offlineFiles = offlineDir.entryInfoList(QStringList() << "*.jsonl", QDir::Files);
    for (const QFileInfo& info : offlineFiles) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        while (!file.atEnd()) {
            collectPayload(file.readLine().trimmed());
        }
    }

    return referencedPaths;
}

void Server::cleanupExpiredOfflineAttachments() {
    const QString rootDirPath = offlineAttachmentRootDir();
    QDir rootDir(rootDirPath);
    if (!rootDir.exists()) return;

    const QSet<QString> referencedPaths = collectReferencedOfflineAttachments();
    const QDateTime now = QDateTime::currentDateTime();
    QStringList visitedDirs;
    QDirIterator it(rootDirPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (info.isDir()) {
            visitedDirs.append(path);
            continue;
        }

        const QString cleanPath = QDir::cleanPath(path);
        const bool isExpired = info.lastModified().msecsTo(now) > offlineAttachmentTtlMs();
        const bool isOrphaned = !referencedPaths.contains(cleanPath);
        if ((isExpired || isOrphaned) && QFile::remove(path)) {
            qDebug() << "Cleaned offline attachment" << cleanPath
                     << (isExpired ? "expired" : "orphaned");
        }
    }

    std::sort(visitedDirs.begin(), visitedDirs.end(), [](const QString& left, const QString& right) {
        return left.count(QLatin1Char('/')) > right.count(QLatin1Char('/'));
    });
    for (const QString& dirPath : visitedDirs) {
        const QFileInfo info(dirPath);
        QDir parentDir(info.absolutePath());
        parentDir.rmdir(info.fileName());
    }

    std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore();
    if (objectStore) {
        QStringList removedKeys;
        const int removedObjects = objectStore->cleanupExpired(objectStoreTtlMs(), &removedKeys);
        if (removedObjects > 0) {
            qDebug() << "Cleaned expired object-store attachments" << removedObjects << removedKeys;
            QJsonObject cleanupMeta;
            cleanupMeta["objectKey"] = removedKeys.join(QLatin1Char(','));
            logRedisLargeFileRouteEvent(QStringLiteral("object_ttl_cleanup"),
                                        QStringLiteral("removed"),
                                        largeFileRouteLogMetadata(cleanupMeta, objectStoreType(), QStringLiteral("delete")),
                                        QStringLiteral("expired"),
                                        removedObjects);
        }
    }
}

void Server::saveOfflineMessage(const Message& msg) const {
    QJsonObject obj;
    obj["type"] = msg.type == MessageType::File || msg.type == MessageType::Image ? "file" : "private";
    obj["messageType"] = static_cast<int>(msg.type);
    obj["senderId"] = msg.senderId;
    obj["senderName"] = msg.senderName;
    obj["senderAvatar"] = msg.senderAvatar;
    obj["receiverId"] = msg.receiverId;
    obj["content"] = msg.content;
    obj["fileName"] = msg.fileName;
    obj["transferId"] = msg.transferId;
    obj["fileSize"] = QString::number(msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
    obj["fileHash"] = msg.fileHash;
    obj["chunkSize"] = QString::number(msg.chunkSize);
    obj["chunkCount"] = QString::number(msg.chunkCount);
    appendE2EFields(&obj, msg);
    QString savedAttachmentPath;
    QString savedObjectKey;
    bool shouldPublishObjectOffer = false;
    if (!msg.fileData.isEmpty()) {
        const bool shouldStoreAsAttachment = msg.type == MessageType::File || msg.type == MessageType::Image;
        const QString attachmentPath = shouldStoreAsAttachment ? saveOfflineAttachment(msg) : QString();
        if (shouldStoreAsAttachment && attachmentPath.isEmpty()) {
            qWarning() << "Offline file was not queued because attachment storage failed" << msg.receiverId << msg.fileName;
            return;
        } else if (!attachmentPath.isEmpty()) {
            savedAttachmentPath = attachmentPath;
            obj["offlineFilePath"] = attachmentPath;
            obj["offlineFileStoredOnDisk"] = true;

            if (shouldPublishLargeFileOffer(msg)) {
                QString objectStoreError;
                std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore(&objectStoreError);
                QString objectKey;
                QString objectHash;
                QString objectError;
                const QString extension = QFileInfo(msg.fileName).suffix();
                if (objectStore
                    && objectStore->writeObject(msg.fileData, &objectKey, &objectHash, &objectError, extension)
                    && objectHash.compare(msg.fileHash.trimmed(), Qt::CaseInsensitive) == 0) {
                    savedObjectKey = objectKey;
                    obj["objectStoreKey"] = objectKey;
                    obj["objectStoreHash"] = objectHash;
                    obj["objectStoreCreatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
                    obj["objectStoreExpiresAt"] = QDateTime::currentDateTimeUtc().addMSecs(objectStoreTtlMs()).toString(Qt::ISODate);
                    shouldPublishObjectOffer = true;
                } else {
                    if (objectStore && !objectKey.isEmpty()) {
                        objectStore->removeObject(objectKey);
                    }
                    QJsonObject logMeta;
                    logMeta["transferId"] = msg.transferId;
                    logMeta["receiverId"] = msg.receiverId;
                    logMeta["fileName"] = msg.fileName;
                    logMeta["messageType"] = msg.type == MessageType::Image ? QStringLiteral("Image") : QStringLiteral("File");
                    if (!objectKey.isEmpty()) {
                        logMeta["objectKey"] = objectKey;
                    }
                    const QString writeFailureReason =
                        objectStoreWriteFailureReasonForLog(objectStoreType(),
                                                            static_cast<bool>(objectStore),
                                                            objectError.isEmpty() ? objectStoreError : objectError,
                                                            msg.fileHash,
                                                            objectHash);
                    logRedisLargeFileRouteEvent(QStringLiteral("object_write"),
                                                QStringLiteral("skipped"),
                                                largeFileRouteLogMetadata(logMeta, objectStoreType(), QStringLiteral("write")),
                                                writeFailureReason,
                                                msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
                    qWarning() << "Large file object routing skipped because object write/validation failed"
                               << msg.receiverId << msg.fileName << writeFailureReason;
                }
            }
        } else {
            obj["fileData"] = QString::fromLatin1(msg.fileData.toBase64());
        }
    }
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    auto rollbackSavedAttachment = [this, &savedAttachmentPath, &savedObjectKey, &obj]() {
        if (!savedAttachmentPath.isEmpty() && QFile::remove(savedAttachmentPath)) {
            qWarning() << "Rolled back offline attachment after queue persistence failure" << savedAttachmentPath;
        }
        std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore();
        if (objectStore && !savedObjectKey.isEmpty()) {
            const bool removedObject = objectStore->removeObject(savedObjectKey);
            QString reason = removedObject ? QStringLiteral("success") : QStringLiteral("unknown");
            if (!removedObject) {
                reason = objectStoreRemoveFailureReasonForLog(objectStoreType(), objectStore->lastRemoveFailureReason());
            }
            QJsonObject logMeta;
            logMeta["transferId"] = obj["transferId"].toString();
            logMeta["receiverId"] = obj["receiverId"].toString();
            logMeta["objectKey"] = savedObjectKey;
            logRedisLargeFileRouteEvent(QStringLiteral("object_delete"),
                                        removedObject ? QStringLiteral("deleted") : QStringLiteral("retained"),
                                        largeFileRouteLogMetadata(logMeta, objectStoreType(), QStringLiteral("delete")),
                                        reason,
                                        obj["fileSize"].toVariant().toLongLong());
            if (removedObject) {
                qWarning() << "Rolled back object-store attachment after queue persistence failure" << savedObjectKey;
            } else {
                qWarning() << "Object-store rollback delete failed after queue persistence failure"
                           << savedObjectKey << reason;
            }
        }
    };

    bool savedToSqlite = false;
    if (ensureAccountDatabase()) {
        QString connectionName = "offline_write_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = openAccountDatabase(connectionName);
            if (openAccountDatabaseConnection(db, connectionName)) {
                QSqlQuery query(db);
                query.prepare("INSERT INTO offline_messages(receiver_id, payload, created_at) VALUES(?, ?, CURRENT_TIMESTAMP)");
                query.addBindValue(msg.receiverId);
                query.addBindValue(QString::fromUtf8(payload));
                savedToSqlite = query.exec();
                db.close();
            }
        }
        releaseAccountDatabase(connectionName);
    }
    if (savedToSqlite) {
        if (shouldPublishObjectOffer && !publishRedisLargeFileOffer(obj)) {
            qWarning() << "Large file offer publish failed; origin offline queue remains as fallback"
                       << msg.receiverId << msg.fileName;
        }
        return;
    }

    QFile file(offlineFilePath(msg.receiverId));
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        rollbackSavedAttachment();
        return;
    }

    const bool savedToJsonl = file.write(payload) == payload.size()
        && file.write("\n") == 1;
    file.close();
    if (!savedToJsonl) {
        rollbackSavedAttachment();
    } else if (shouldPublishObjectOffer && !publishRedisLargeFileOffer(obj)) {
        qWarning() << "Large file offer publish failed; origin offline queue remains as fallback"
                   << msg.receiverId << msg.fileName;
    }
}

bool Server::updateOfflineMessageProgress(qint64 sqliteMessageId, const QJsonObject& obj, qint64 confirmedBytes, qint64 confirmedChunkIndex) const {
    if (sqliteMessageId <= 0 || confirmedBytes <= 0 || !ensureAccountDatabase()) {
        return false;
    }

    bool saved = false;
    const QString connectionName = "offline_progress_" + QString::number(QCoreApplication::applicationPid())
        + "_" + QString::number(QRandomGenerator::global()->generate());
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QJsonObject updated = obj;
            QSqlQuery selectQuery(db);
            selectQuery.prepare("SELECT payload FROM offline_messages WHERE id = ?");
            selectQuery.addBindValue(sqliteMessageId);
            if (selectQuery.exec() && selectQuery.next()) {
                const QJsonDocument currentDoc = QJsonDocument::fromJson(selectQuery.value(0).toString().toUtf8());
                if (currentDoc.isObject()) {
                    updated = currentDoc.object();
                }
            }

            const qint64 existingConfirmedBytes = updated["confirmedBytes"].toVariant().toLongLong();
            updated["confirmedBytes"] = QString::number(qMax(existingConfirmedBytes, confirmedBytes));
            QJsonArray confirmedChunks = updated["confirmedChunks"].toArray();
            bool alreadyRecorded = false;
            for (const QJsonValue& value : confirmedChunks) {
                if (value.toVariant().toLongLong() == confirmedChunkIndex) {
                    alreadyRecorded = true;
                    break;
                }
            }
            if (confirmedChunkIndex >= 0 && !alreadyRecorded) {
                confirmedChunks.append(QString::number(confirmedChunkIndex));
            }
            updated["confirmedChunks"] = confirmedChunks;
            updated["resumeUpdatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

            QSqlQuery query(db);
            query.prepare("UPDATE offline_messages SET payload = ? WHERE id = ?");
            query.addBindValue(QString::fromUtf8(QJsonDocument(updated).toJson(QJsonDocument::Compact)));
            query.addBindValue(sqliteMessageId);
            saved = query.exec();
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return saved;
}

bool Server::deliverOfflinePayload(const QByteArray& payload, QTcpSocket* socket, qint64 sqliteMessageId) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState || payload.isEmpty()) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        const bool written = socket->write(payload) > 0 && socket->write("\n") > 0;
        socket->flush();
        return written;
    }

    const QJsonObject obj = doc.object();
    const QString offlineFilePath = obj["offlineFilePath"].toString();
    const int messageType = obj["messageType"].toInt(static_cast<int>(MessageType::File));
    const bool isFileMessage = messageType == static_cast<int>(MessageType::File)
        || messageType == static_cast<int>(MessageType::Image);
    if (!isFileMessage || offlineFilePath.isEmpty()) {
        const qint64 written = socket->write(payload);
        socket->write("\n");
        socket->flush();
        return written > 0;
    }

    const OfflineAttachmentValidationResult validation = validateOfflineAttachmentForReplay(obj, offlineFilePath, socket);
    if (validation == OfflineAttachmentValidationResult::CleanedBadState) {
        return true;
    }
    if (validation == OfflineAttachmentValidationResult::Blocked) {
        return false;
    }

    const bool delivered = sendOfflineAttachmentToSocket(obj, offlineFilePath, socket, sqliteMessageId);
    if (delivered) {
        QFile::remove(offlineFilePath);
    }
    return delivered;
}

bool Server::sendOfflineAttachmentToSocket(const QJsonObject& obj, const QString& filePath, QTcpSocket* socket, qint64 sqliteMessageId) {
    QPointer<QTcpSocket> socketGuard(socket);
    if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const qint64 totalBytes = QFileInfo(file).size();
    const qint64 declaredChunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 chunkSize = (declaredChunkSize > 0 && declaredChunkSize <= kForwardChunkBytes)
        ? declaredChunkSize
        : kForwardChunkBytes;
    const qint64 chunkCount = (totalBytes + chunkSize - 1) / chunkSize;
    const QString fileName = obj["fileName"].toString();
    const OfflineAttachmentReplayPlan replayPlan = buildOfflineAttachmentReplayPlan(obj,
                                                                                   totalBytes,
                                                                                   chunkSize,
                                                                                   chunkCount,
                                                                                   sqliteMessageId);
    if (replayPlan.startChunkIndex > 0
        && replayPlan.startChunkIndex < chunkCount
        && !file.seek(replayPlan.startChunkIndex * chunkSize)) {
        qWarning() << "Offline attachment resume seek failed:" << fileName << replayPlan.startChunkIndex << "/" << chunkCount;
        return false;
    }
    const QString transferId = QString("%1_%2_%3")
        .arg(obj["senderId"].toString(),
             QString::number(QDateTime::currentMSecsSinceEpoch()),
             QString::number(QRandomGenerator::global()->generate()));

    for (qint64 index = replayPlan.startChunkIndex; index < chunkCount; ++index) {
        if (replayPlan.confirmedChunksValid && replayPlan.confirmedChunkIndexes.contains(index)) {
            continue;
        }
        if (!file.seek(index * chunkSize)) {
            qWarning() << "Offline attachment chunk seek failed:" << fileName << index + 1 << "/" << chunkCount;
            return false;
        }
        const QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty() || (index < chunkCount - 1 && chunk.size() != chunkSize)) {
            qWarning() << "Offline attachment chunk read failed:" << fileName << index + 1 << "/" << chunkCount;
            return false;
        }

        QJsonObject chunkObj;
        chunkObj["type"] = "file_chunk";
        chunkObj["transferId"] = transferId;
        chunkObj["messageType"] = obj["messageType"].toInt(static_cast<int>(MessageType::File));
        chunkObj["senderId"] = obj["senderId"].toString();
        chunkObj["senderName"] = obj["senderName"].toString();
        chunkObj["receiverId"] = obj["receiverId"].toString();
        chunkObj["content"] = obj["content"].toString();
        chunkObj["fileName"] = fileName;
        chunkObj["fileSize"] = QString::number(totalBytes);
        chunkObj["fileHash"] = obj["fileHash"].toString();
        chunkObj["chunkSize"] = QString::number(chunkSize);
        chunkObj["chunkCount"] = QString::number(chunkCount);
        chunkObj["chunkIndex"] = QString::number(index);
        chunkObj["fileData"] = QString::fromLatin1(chunk.toBase64());
        appendE2EFileFields(&chunkObj, obj);

        QString ackRejectReason;
        qint64 ackReceivedBytes = 0;
        bool acknowledged = false;
        const QByteArray data = QJsonDocument(chunkObj).toJson(QJsonDocument::Compact);
        for (int attempt = 1; attempt <= kChunkSendMaxAttempts; ++attempt) {
            if (sendFileChunkAndWaitForAck(socketGuard, data, transferId, index, &ackRejectReason, &ackReceivedBytes)) {
                const qint64 expectedAckBytes = qMin(totalBytes, index * chunkSize + chunk.size());
                if (ackReceivedBytes > 0 && (ackReceivedBytes < expectedAckBytes || ackReceivedBytes > totalBytes)) {
                    if (attempt == kChunkSendMaxAttempts) {
                        ackRejectReason = QString::fromUtf8("离线附件确认进度非法");
                    }
                    continue;
                }
                acknowledged = true;
                break;
            }
            if (!socketGuard) {
                return false;
            }
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "Offline attachment chunk rejected by receiver:" << ackRejectReason;
                return false;
            }
        }
        if (!acknowledged) {
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "Offline attachment chunk rejected by receiver:" << ackRejectReason;
                return false;
            }
            qWarning() << "Offline attachment chunk ack timeout:" << fileName << index + 1 << "/" << chunkCount;
            return false;
        }
        const qint64 fallbackConfirmedBytes = qMin(totalBytes, (index + 1) * chunkSize);
        const qint64 confirmedBytes = qBound<qint64>(0, ackReceivedBytes > 0 ? ackReceivedBytes : fallbackConfirmedBytes, totalBytes);
        if (confirmedBytes > 0) {
            updateOfflineMessageProgress(sqliteMessageId, obj, confirmedBytes, index);
        }
    }

    return true;
}

void Server::sendOfflineMessages(const QString& userId, QTcpSocket* socket) {
    if (ensureAccountDatabase()) {
        QString connectionName = "offline_read_" + QString::number(reinterpret_cast<quintptr>(this));
        QVector<qint64> deliveredIds;
        QVector<QPair<qint64, QByteArray>> pendingRows;
        {
            QSqlDatabase db = openAccountDatabase(connectionName);
            if (openAccountDatabaseConnection(db, connectionName)) {
                QSqlQuery query(db);
                query.prepare("SELECT id, payload FROM offline_messages WHERE receiver_id = ? ORDER BY id ASC");
                query.addBindValue(userId);
                if (query.exec()) {
                    while (query.next()) {
                        const qint64 messageId = query.value(0).toLongLong();
                        QByteArray line = query.value(1).toString().toUtf8();
                        if (line.isEmpty()) continue;
                        pendingRows.append(qMakePair(messageId, line));
                    }
                }
                for (const auto& row : pendingRows) {
                    if (!deliverOfflinePayload(row.second, socket, row.first)) {
                        break;
                    }
                    deliveredIds.append(row.first);
                }
                if (!deliveredIds.isEmpty()) {
                    for (qint64 messageId : deliveredIds) {
                        QSqlQuery deleteQuery(db);
                        deleteQuery.prepare("DELETE FROM offline_messages WHERE id = ?");
                        deleteQuery.addBindValue(messageId);
                        deleteQuery.exec();
                    }
                }
                db.close();
            }
        }
        releaseAccountDatabase(connectionName);
    }

    QFile file(offlineFilePath(userId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QVector<QByteArray> remainingLines;
    bool deliveryBlocked = false;
    while (!file.atEnd()) {
        QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) continue;
        if (deliveryBlocked || !deliverOfflinePayload(line, socket)) {
            deliveryBlocked = true;
            remainingLines.append(line);
        }
    }
    file.close();
    if (remainingLines.isEmpty()) {
        file.remove();
        return;
    }
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        for (const QByteArray& line : remainingLines) {
            file.write(line);
            file.write("\n");
        }
    }
}
