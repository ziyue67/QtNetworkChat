#include "server.h"
#include "server_group_support.h"
#include "server_database.h"
#include "server_delivery_support.h"
#include "objectstore.h"
#include "qqnt_redis_service.h"
#include "redisclient.h"
#include "heartbeatmonitor.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QJsonArray>
#include <QStringList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QTimer>
#include <QPointer>
#include <QUuid>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <memory>

namespace {
using namespace ServerDatabase;
using namespace ServerDeliverySupport;
constexpr qint64 kDefaultObjectStoreTtlHours = 24;
constexpr qint64 kMaxObjectStoreTtlHours = 24LL * 365;
bool envEnabled(const char* name) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

void copyLargeFileRouteContext(QJsonObject* target, const QJsonObject& source) {
    if (!target) return;
    const QStringList keys = {
        QStringLiteral("deliveryState"),
        QStringLiteral("groupId"),
        QStringLiteral("targetUserId")
    };
    for (const QString& key : keys) {
        const QString value = source.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            (*target)[key] = value;
        }
    }
}

bool isServerGroupLargeFileOffer(const QJsonObject& event) {
    return event.value("deliveryState").toString() == QLatin1String("server-group-file")
        || !event.value("groupId").toString().trimmed().isEmpty();
}

}

bool Server::publishRedisLargeFileOffer(const QJsonObject& offlinePayload) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    const QString objectKey = offlinePayload["objectStoreKey"].toString().trimmed();
    if (!FilesystemObjectStore::isValidObjectKey(objectKey)) return false;

    const qint64 fileSize = offlinePayload["fileSize"].toVariant().toLongLong();
    const qint64 chunkSize = offlinePayload["chunkSize"].toVariant().toLongLong();
    const qint64 chunkCount = offlinePayload["chunkCount"].toVariant().toLongLong();
    const QString fileHash = offlinePayload["fileHash"].toString().trimmed();
    if (fileSize <= 0 || chunkSize <= 0 || chunkCount <= 0 || !looksLikeSha256Hex(fileHash)) {
        return false;
    }

    const int messageType = offlinePayload["messageType"].toInt(static_cast<int>(MessageType::File));
    if (messageType != static_cast<int>(MessageType::File)
        && messageType != static_cast<int>(MessageType::Image)) {
        return false;
    }

    const QString storedTransferId = offlinePayload["transferId"].toString().trimmed();
    QJsonObject event;
    event["eventType"] = "large_file_offer";
    event["instanceId"] = m_instanceId;
    event["transferId"] = storedTransferId.isEmpty() ? objectKey : storedTransferId;
    event["objectKey"] = objectKey;
    event["senderId"] = offlinePayload["senderId"].toString();
    event["senderName"] = offlinePayload["senderName"].toString();
    event["receiverId"] = offlinePayload["receiverId"].toString();
    event["messageType"] = messageType == static_cast<int>(MessageType::Image) ? "Image" : "File";
    event["storeType"] = objectStoreType();
    event["fileName"] = offlinePayload["fileName"].toString();
    event["fileSize"] = QString::number(fileSize);
    event["fileHash"] = fileHash;
    event["chunkSize"] = QString::number(chunkSize);
    event["chunkCount"] = QString::number(chunkCount);
    event["expiresAt"] = offlinePayload["objectStoreExpiresAt"].toString();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    appendE2EFileFields(&event, offlinePayload);
    copyLargeFileRouteContext(&event, offlinePayload);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Skip large file offer because encoded payload is too large:"
                   << eventPayload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        logRedisLargeFileRouteEvent(QStringLiteral("offer"),
                                    QStringLiteral("skipped"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                    QStringLiteral("payload-too-large"),
                                    fileSize);
        return false;
    }
    const bool published = m_redisService->publish("messages", eventPayload);
    logRedisLargeFileRouteEvent(QStringLiteral("offer"),
                                published ? QStringLiteral("published") : QStringLiteral("publish-failed"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                published ? QString() : m_redisService->lastError(),
                                fileSize);
    return published;
}

bool Server::publishRedisLargeFileClaim(const QJsonObject& offer) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    QJsonObject event;
    event["eventType"] = "large_file_claim";
    event["instanceId"] = m_instanceId;
    event["sourceInstanceId"] = offer["instanceId"].toString();
    event["transferId"] = offer["transferId"].toString();
    event["objectKey"] = offer["objectKey"].toString();
    event["receiverId"] = offer["receiverId"].toString();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    copyLargeFileRouteContext(&event, offer);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        logRedisLargeFileRouteEvent(QStringLiteral("claim"),
                                    QStringLiteral("skipped"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                    QStringLiteral("payload-too-large"));
        return false;
    }
    const bool published = m_redisService->publish("messages", eventPayload);
    logRedisLargeFileRouteEvent(QStringLiteral("claim"),
                                published ? QStringLiteral("published") : QStringLiteral("publish-failed"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                published ? QString() : m_redisService->lastError());
    return published;
}

bool Server::publishRedisLargeFileDelivered(const QJsonObject& offer, qint64 confirmedBytes) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    QJsonObject event;
    event["eventType"] = "large_file_delivered";
    event["instanceId"] = m_instanceId;
    event["sourceInstanceId"] = offer["instanceId"].toString();
    event["transferId"] = offer["transferId"].toString();
    event["objectKey"] = offer["objectKey"].toString();
    event["receiverId"] = offer["receiverId"].toString();
    event["confirmedBytes"] = QString::number(confirmedBytes);
    event["fileHash"] = offer["fileHash"].toString();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    appendE2EFileFields(&event, offer);
    copyLargeFileRouteContext(&event, offer);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        logRedisLargeFileRouteEvent(QStringLiteral("delivered"),
                                    QStringLiteral("skipped"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                    QStringLiteral("payload-too-large"),
                                    confirmedBytes);
        return false;
    }
    const bool published = m_redisService->publish("messages", eventPayload);
    logRedisLargeFileRouteEvent(QStringLiteral("delivered"),
                                published ? QStringLiteral("published") : QStringLiteral("publish-failed"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                published ? QString() : m_redisService->lastError(),
                                confirmedBytes);
    return published;
}

bool Server::publishRedisLargeFileFailed(const QJsonObject& offer, const QString& reason) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    QJsonObject event;
    event["eventType"] = "large_file_failed";
    event["instanceId"] = m_instanceId;
    event["sourceInstanceId"] = offer["instanceId"].toString();
    event["transferId"] = offer["transferId"].toString();
    event["objectKey"] = offer["objectKey"].toString();
    event["receiverId"] = offer["receiverId"].toString();
    event["fileHash"] = offer["fileHash"].toString();
    event["reason"] = reason.left(160);
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    appendE2EFileFields(&event, offer);
    copyLargeFileRouteContext(&event, offer);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        logRedisLargeFileRouteEvent(QStringLiteral("failed"),
                                    QStringLiteral("skipped"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                    QStringLiteral("payload-too-large"));
        return false;
    }
    const bool published = m_redisService->publish("messages", eventPayload);
    logRedisLargeFileRouteEvent(QStringLiteral("failed"),
                                published ? QStringLiteral("published") : QStringLiteral("publish-failed"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                published ? reason : m_redisService->lastError());
    return published;
}

void Server::handleRedisLargeFileOffer(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return;

    auto messageFromOffer = [&event](const QString& receiverId) {
        Message msg;
        msg.senderId = event["senderId"].toString();
        msg.senderName = event["senderName"].toString();
        msg.receiverId = receiverId;
        msg.fileName = event["fileName"].toString();
        msg.transferId = event["transferId"].toString();
        msg.fileSize = event["fileSize"].toVariant().toLongLong();
        msg.fileHash = event["fileHash"].toString();
        msg.chunkSize = event["chunkSize"].toVariant().toLongLong();
        msg.chunkCount = event["chunkCount"].toVariant().toLongLong();
        msg.type = event["messageType"].toString() == "Image" ? MessageType::Image : MessageType::File;
        msg.content = QString(msg.type == MessageType::Image ? "发送了图片: %1" : "发送了文件: %1").arg(msg.fileName);
        msg.e2eFileEncrypted = event["e2eFileEncrypted"].toBool(false);
        msg.e2eFileKeyId = event["e2eFileKeyId"].toString();
        msg.e2eFileKeyFingerprint = event["e2eFileKeyFingerprintSha256"].toString();
        msg.e2eFilePlainSize = event["e2eFilePlainSize"].toVariant().toLongLong();
        msg.e2eFilePlainHash = event["e2eFilePlainHash"].toString();
        if (event.value("e2eEnvelope").isObject()) {
            const QJsonObject envelopeObject = event.value("e2eEnvelope").toObject();
            const E2EEnvelope envelope = E2EEnvelope::fromJson(envelopeObject);
            if (envelope.isValid()) {
                msg.e2eEnvelope = envelope;
            } else if (e2eEnvelopeHeaderLooksSafe(envelopeObject)) {
                msg.e2eEnvelopeHeader = envelopeObject;
            }
        }
        msg.timestamp = QDateTime::currentDateTime();
        return msg;
    };

    if (isServerGroupLargeFileOffer(event)) {
        QString groupId = event.value("groupId").toString().trimmed();
        if (groupId.isEmpty()) {
            groupId = event.value("receiverId").toString().trimmed();
        }
        const QString senderId = event.value("senderId").toString().trimmed();
        if (groupId.isEmpty() || groupId == QLatin1String("public") || senderId.isEmpty()) return;

        const QStringList memberIds = serverGroupMemberIds(groupId);
        if (memberIds.isEmpty()) return;
        bool deliveredAny = false;
        for (const QString& memberId : memberIds) {
            if (memberId.isEmpty() || memberId == senderId) continue;
            QTcpSocket* memberSocket = m_userSockets.value(memberId);
            if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
            if (!isServerGroupMember(groupId, memberId)) continue;

            QJsonObject targetedEvent = event;
            targetedEvent["receiverId"] = groupId;
            targetedEvent["groupId"] = groupId;
            targetedEvent["targetUserId"] = memberId;
            if (deliverRedisLargeFileOffer(targetedEvent, memberSocket, false)) {
                deliveredAny = true;
            } else {
                qWarning() << "Redis private group large file delivery failed"
                           << groupId << memberId << event.value("fileName").toString();
            }
        }
        if (deliveredAny) {
            emit newMessage(messageFromOffer(groupId));
        }
        return;
    }

    const QString receiverId = event["receiverId"].toString().trimmed();
    if (receiverId.isEmpty()) return;

    QTcpSocket* targetSocket = m_userSockets.value(receiverId);
    if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    if (deliverRedisLargeFileOffer(event, targetSocket)) {
        emit newMessage(messageFromOffer(receiverId));
    }
}

void Server::handleRedisLargeFileDelivered(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;
    if (event["sourceInstanceId"].toString() != m_instanceId) return;
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return;
    const LargeFileDeliveredReceiptDecision reconcileDecision =
        evaluateRedisLargeFileDeliveredReceipt(event);
    logRedisLargeFileRouteEvent(QStringLiteral("delivered_reconcile"),
                                reconcileDecision.shouldCleanup ? QStringLiteral("cleaned") : QStringLiteral("retained"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("reconcile")),
                                reconcileDecision.reason,
                                event["confirmedBytes"].toVariant().toLongLong());
    const LargeFileCleanupResult cleanupResult = cleanupDeliveredRedisLargeFile(event);
    persistRedisLargeFileDeliveredReceiptSummary(event, reconcileDecision, cleanupResult.queueCleaned);
    logRedisLargeFileRouteEvent(QStringLiteral("delivered_cleanup"),
                                cleanupResult.queueCleaned ? QStringLiteral("cleaned") : QStringLiteral("retained"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("delete")),
                                cleanupResult.queueCleaned ? QString() : QStringLiteral("offline-fallback-not-matched"),
                                event["confirmedBytes"].toVariant().toLongLong());
    if (cleanupResult.objectDeleteAttempted) {
        logRedisLargeFileRouteEvent(QStringLiteral("object_delete"),
                                    cleanupResult.objectDeleted ? QStringLiteral("deleted") : QStringLiteral("retained"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("delete")),
                                    cleanupResult.objectDeleteReason,
                                    event["confirmedBytes"].toVariant().toLongLong());
    }
}

void Server::handleRedisLargeFileFailed(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;
    if (event["sourceInstanceId"].toString() != m_instanceId) return;
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return;

    const QString objectKey = event["objectKey"].toString().trimmed();
    const QString transferId = event["transferId"].toString().trimmed();
    const QString receiverId = event["receiverId"].toString().trimmed();
    const QString fileHash = event["fileHash"].toString().trimmed();
    if (!FilesystemObjectStore::isValidObjectKey(objectKey)
        || transferId.isEmpty()
        || receiverId.isEmpty()
        || !looksLikeSha256Hex(fileHash)) {
        return;
    }

    qWarning() << "Large file object routing failed on remote instance; origin offline fallback remains"
               << receiverId << transferId << objectKey << event["reason"].toString();
    logRedisLargeFileRouteEvent(QStringLiteral("failed_received"),
                                QStringLiteral("fallback-retained"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("fallback")),
                                event["reason"].toString());
}

bool Server::deliverRedisLargeFileOffer(const QJsonObject& event, QTcpSocket* socket, bool publishDeliveredReceipt) {
    QPointer<QTcpSocket> socketGuard(socket);
    auto logDeliveryFailure = [this, &event](const QString& reason, qint64 bytes = 0) {
        logRedisLargeFileRouteEvent(QStringLiteral("offer_delivery"),
                                    QStringLiteral("failed"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("deliver")),
                                    reason,
                                    bytes);
    };
    auto failOffer = [this, &event, publishDeliveredReceipt](const QString& reason) {
        if (publishDeliveredReceipt) {
            publishRedisLargeFileFailed(event, reason);
        }
        return false;
    };

    if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState) {
        logDeliveryFailure(QStringLiteral("receiver-disconnected"));
        return failOffer(QStringLiteral("receiver-disconnected"));
    }

    if (!isSupportedObjectStoreType(objectStoreType())) {
        return false;
    }
    const QString localStoreType = objectStoreType();
    const QString offerStoreType = normalizeObjectStoreType(event["storeType"].toString());
    if (!isSupportedObjectStoreType(offerStoreType)) {
        logRedisLargeFileRouteEvent(QStringLiteral("offer_validation"),
                                    QStringLiteral("rejected"),
                                    largeFileRouteLogMetadata(event, localStoreType, QStringLiteral("validate")),
                                    QStringLiteral("unsupported-offer-store-type"));
        return failOffer(QStringLiteral("unsupported-offer-store-type"));
    }
    if (offerStoreType != localStoreType) {
        logRedisLargeFileRouteEvent(QStringLiteral("offer_validation"),
                                    QStringLiteral("rejected"),
                                    largeFileRouteLogMetadata(event, localStoreType, QStringLiteral("validate")),
                                    QStringLiteral("object-store-type-mismatch"));
        return failOffer(QStringLiteral("object-store-type-mismatch"));
    }

    QString objectStoreError;
    std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore(&objectStoreError);
    if (!objectStore) {
        if (!objectStoreError.isEmpty()) {
            qWarning() << "Skip large file offer because object store is unavailable" << objectStoreError;
        }
        return failOffer(QStringLiteral("object-store-unavailable: ") + objectStoreError);
    }

    const QString objectKey = event["objectKey"].toString().trimmed();
    const qint64 fileSize = event["fileSize"].toVariant().toLongLong();
    const qint64 chunkSize = event["chunkSize"].toVariant().toLongLong();
    const qint64 chunkCount = event["chunkCount"].toVariant().toLongLong();
    const QString fileHash = event["fileHash"].toString().trimmed();
    const QString messageType = event["messageType"].toString();
    if (!FilesystemObjectStore::isValidObjectKey(objectKey)
        || fileSize <= 0
        || chunkSize <= 0
        || chunkSize > kForwardChunkBytes
        || chunkCount <= 0
        || chunkCount != (fileSize + chunkSize - 1) / chunkSize
        || !looksLikeSha256Hex(fileHash)
        || event["senderId"].toString().trimmed().isEmpty()
        || event["receiverId"].toString().trimmed().isEmpty()
        || (messageType != "File" && messageType != "Image")) {
        return failOffer(QStringLiteral("invalid-offer-metadata"));
    }

    const ObjectStore::ValidationResult validation =
        objectStore->validateObject(objectKey, fileSize, fileHash);
    if (!validation.ok) {
        const QString reason = s3ValidationFailureReasonForLog(validation);
        qWarning() << "Rejected large file offer because object validation failed"
                   << objectKey << reason;
        logRedisLargeFileRouteEvent(QStringLiteral("offer_validation"),
                                    QStringLiteral("rejected"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("validate")),
                                    reason,
                                    fileSize);
        return failOffer(reason);
    }

    std::unique_ptr<QIODevice> file = objectStore->openObject(objectKey);
    if (!file || !file->isOpen()) {
        QString reason = QStringLiteral("object-open-failed");
        const QString storeReason = objectStore->lastOpenFailureReason();
        if (!storeReason.isEmpty()) {
            reason = objectStoreOpenFailureReasonForLog(objectStoreType(), storeReason);
        }
        qWarning() << "Rejected large file offer because object open failed"
                   << objectKey << reason;
        logRedisLargeFileRouteEvent(QStringLiteral("offer_read"),
                                    QStringLiteral("rejected"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("read")),
                                    reason,
                                    fileSize);
        return failOffer(reason);
    }

    publishRedisLargeFileClaim(event);

    const QString transferId = event["transferId"].toString().trimmed().isEmpty()
        ? objectKey
        : event["transferId"].toString().trimmed();
    const QString fileName = event["fileName"].toString();
    for (qint64 index = 0; index < chunkCount; ++index) {
        if (!file->seek(index * chunkSize)) {
            logDeliveryFailure(QStringLiteral("object-seek-failed"), index * chunkSize);
            return failOffer(QStringLiteral("object-seek-failed"));
        }
        const QByteArray chunk = file->read(chunkSize);
        if (chunk.isEmpty() || (index < chunkCount - 1 && chunk.size() != chunkSize)) {
            logDeliveryFailure(QStringLiteral("object-read-failed"), index * chunkSize);
            return failOffer(QStringLiteral("object-read-failed"));
        }

        QJsonObject chunkObj;
        chunkObj["type"] = "file_chunk";
        chunkObj["transferId"] = transferId;
        chunkObj["messageType"] = messageType == "Image" ? static_cast<int>(MessageType::Image) : static_cast<int>(MessageType::File);
        chunkObj["senderId"] = event["senderId"].toString();
        chunkObj["senderName"] = event["senderName"].toString();
        chunkObj["receiverId"] = event["receiverId"].toString();
        chunkObj["content"] = QString(messageType == "Image" ? "发送了图片: %1" : "发送了文件: %1").arg(fileName);
        chunkObj["fileName"] = fileName;
        chunkObj["fileSize"] = QString::number(fileSize);
        chunkObj["fileHash"] = fileHash;
        chunkObj["chunkSize"] = QString::number(chunkSize);
        chunkObj["chunkCount"] = QString::number(chunkCount);
        chunkObj["chunkIndex"] = QString::number(index);
        chunkObj["fileData"] = QString::fromLatin1(chunk.toBase64());
        appendE2EFileFields(&chunkObj, event);

        QString ackRejectReason;
        qint64 ackReceivedBytes = 0;
        bool acknowledged = false;
        const QByteArray data = QJsonDocument(chunkObj).toJson(QJsonDocument::Compact);
        for (int attempt = 1; attempt <= kChunkSendMaxAttempts; ++attempt) {
            if (sendFileChunkAndWaitForAck(socketGuard, data, transferId, index, &ackRejectReason, &ackReceivedBytes)) {
                const qint64 expectedAckBytes = qMin(fileSize, index * chunkSize + chunk.size());
                if (ackReceivedBytes > 0 && (ackReceivedBytes < expectedAckBytes || ackReceivedBytes > fileSize)) {
                    if (attempt == kChunkSendMaxAttempts) {
                        ackRejectReason = QString::fromUtf8("跨实例大文件确认进度非法");
                    }
                    continue;
                }
                acknowledged = true;
                break;
            }
            if (!socketGuard) {
                logDeliveryFailure(QStringLiteral("receiver-disconnected"), index * chunkSize);
                return failOffer(QStringLiteral("receiver-disconnected"));
            }
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "Large file offer chunk rejected by receiver:" << ackRejectReason;
                logDeliveryFailure(QStringLiteral("chunk-rejected"), qMin(fileSize, index * chunkSize + chunk.size()));
                return failOffer(QStringLiteral("chunk-rejected: ") + ackRejectReason);
            }
        }
        if (!acknowledged) {
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "Large file offer chunk rejected by receiver:" << ackRejectReason;
                logDeliveryFailure(QStringLiteral("chunk-rejected"), qMin(fileSize, index * chunkSize + chunk.size()));
                return failOffer(QStringLiteral("chunk-rejected: ") + ackRejectReason);
            }
            qWarning() << "Large file offer chunk ack timeout:" << fileName << index + 1 << "/" << chunkCount;
            logDeliveryFailure(QStringLiteral("chunk-ack-timeout"), qMin(fileSize, index * chunkSize + chunk.size()));
            return failOffer(QStringLiteral("chunk-ack-timeout"));
        }
    }

    if (publishDeliveredReceipt) {
        publishRedisLargeFileDelivered(event, fileSize);
    }
    return true;
}

bool Server::shouldPublishLargeFileOffer(const Message& msg) const {
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return false;
    if (!m_redisService->isEnabled()) return false;
    if (!isRedisUserOnline(msg.receiverId)) return false;
    if (msg.receiverId.isEmpty() || msg.fileData.isEmpty()) return false;
    if (msg.type != MessageType::File && msg.type != MessageType::Image) return false;
    QTcpSocket* localSocket = m_userSockets.value(msg.receiverId, nullptr);
    if (localSocket && localSocket->state() == QAbstractSocket::ConnectedState) return false;

    if (!createConfiguredObjectStore()) return false;
    if (msg.fileSize <= 0 || msg.chunkSize <= 0 || msg.chunkCount <= 0) return false;
    if (msg.chunkCount != (msg.fileSize + msg.chunkSize - 1) / msg.chunkSize) return false;
    return looksLikeSha256Hex(msg.fileHash);
}

bool Server::shouldPublishServerGroupLargeFileOffer(const Message& msg, const QStringList& memberIds) const {
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return false;
    if (!m_redisService->isEnabled()) return false;
    if (msg.receiverId.isEmpty() || msg.receiverId == QLatin1String("public")) return false;
    if (msg.fileData.isEmpty() || msg.fileData.size() <= kRedisPubSubFileMaxBytes) return false;
    if (msg.type != MessageType::File && msg.type != MessageType::Image) return false;
    if (msg.fileSize <= 0 || msg.chunkSize <= 0 || msg.chunkCount <= 0) return false;
    if (msg.chunkCount != (msg.fileSize + msg.chunkSize - 1) / msg.chunkSize) return false;
    if (!looksLikeSha256Hex(msg.fileHash)) return false;
    if (!createConfiguredObjectStore()) return false;

    for (const QString& memberId : memberIds) {
        if (memberId.isEmpty() || memberId == msg.senderId) continue;
        QTcpSocket* localSocket = m_userSockets.value(memberId, nullptr);
        if (localSocket && localSocket->state() == QAbstractSocket::ConnectedState) continue;
        bool redisOnline = false;
        if (isRedisUserOnline(memberId, &redisOnline) && redisOnline) {
            return true;
        }
    }
    return false;
}

bool Server::publishRedisServerGroupLargeFileOffer(const Message& msg, const QStringList& memberIds) const {
    if (!shouldPublishServerGroupLargeFileOffer(msg, memberIds)) return false;

    QString objectStoreError;
    std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore(&objectStoreError);
    QString objectKey;
    QString objectHash;
    QString objectError;
    const QString extension = QFileInfo(msg.fileName).suffix();
    if (!objectStore
        || !objectStore->writeObject(msg.fileData, &objectKey, &objectHash, &objectError, extension)
        || objectHash.compare(msg.fileHash.trimmed(), Qt::CaseInsensitive) != 0) {
        if (objectStore && !objectKey.isEmpty()) {
            objectStore->removeObject(objectKey);
        }
        QJsonObject logMeta;
        logMeta["deliveryState"] = QStringLiteral("server-group-file");
        logMeta["groupId"] = msg.receiverId;
        logMeta["receiverId"] = msg.receiverId;
        logMeta["transferId"] = msg.transferId;
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
        qWarning() << "Private group large file object routing skipped because object write/validation failed"
                   << msg.receiverId << msg.fileName << writeFailureReason;
        return false;
    }

    QJsonObject obj;
    obj["type"] = QStringLiteral("file");
    obj["messageType"] = static_cast<int>(msg.type);
    obj["senderId"] = msg.senderId;
    obj["senderName"] = msg.senderName;
    obj["senderAvatar"] = msg.senderAvatar;
    obj["receiverId"] = msg.receiverId;
    obj["groupId"] = msg.receiverId;
    obj["deliveryState"] = QStringLiteral("server-group-file");
    obj["content"] = msg.content;
    obj["fileName"] = msg.fileName;
    obj["transferId"] = msg.transferId;
    obj["fileSize"] = QString::number(msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
    obj["fileHash"] = msg.fileHash;
    obj["chunkSize"] = QString::number(msg.chunkSize);
    obj["chunkCount"] = QString::number(msg.chunkCount);
    obj["objectStoreKey"] = objectKey;
    obj["objectStoreHash"] = objectHash;
    obj["objectStoreCreatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    obj["objectStoreExpiresAt"] = QDateTime::currentDateTimeUtc().addMSecs(objectStoreTtlMs()).toString(Qt::ISODate);
    appendE2EFields(&obj, msg);

    const bool published = publishRedisLargeFileOffer(obj);
    if (!published && objectStore) {
        const bool removedObject = objectStore->removeObject(objectKey);
        QJsonObject logMeta = obj;
        logRedisLargeFileRouteEvent(QStringLiteral("object_delete"),
                                    removedObject ? QStringLiteral("deleted") : QStringLiteral("retained"),
                                    largeFileRouteLogMetadata(logMeta, objectStoreType(), QStringLiteral("delete")),
                                    removedObject ? QStringLiteral("publish-failed")
                                                  : objectStoreRemoveFailureReasonForLog(objectStoreType(), objectStore->lastRemoveFailureReason()),
                                    msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
    }
    return published;
}

QString Server::objectStoreType() const {
    return normalizeObjectStoreType(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_STORE")));
}

QString Server::objectStoreRootDir() const {
    const QString root = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_ROOT")).trimmed();
    return root.isEmpty() ? QString() : QDir::cleanPath(root);
}

std::unique_ptr<ObjectStore> Server::createConfiguredObjectStore(QString* error) const {
    if (m_objectStoreFactoryForTesting) {
        return m_objectStoreFactoryForTesting(error);
    }
    return createObjectStore(objectStoreType(), objectStoreRootDir(), error);
}

qint64 Server::objectStoreTtlMs() const {
    const qint64 hours = positiveIntegerEnvOrDefault("QTNETWORKCHAT_OBJECT_TTL_HOURS",
                                                     kDefaultObjectStoreTtlHours,
                                                     kMaxObjectStoreTtlHours);
    return hours * 60LL * 60 * 1000;
}

LargeFileDeliveredReceiptDecision Server::evaluateRedisLargeFileDeliveredReceipt(const QJsonObject& event) const {
    LargeFileDeliveredReceipt receipt;
    receipt.sourceInstanceId = event["sourceInstanceId"].toString();
    receipt.transferId = event["transferId"].toString().trimmed();
    receipt.receiverId = event["receiverId"].toString().trimmed();
    receipt.objectKey = event["objectKey"].toString().trimmed();
    receipt.fileHash = event["fileHash"].toString().trimmed();
    receipt.confirmedBytes = event["confirmedBytes"].toVariant().toLongLong();

    LargeFileDeliveredReceiptDecision invalidDecision =
        evaluateLargeFileDeliveredReceiptCleanup(receipt, LargeFileDeliveredFallback());
    if (invalidDecision.reason == QStringLiteral("invalid-receipt")) {
        return invalidDecision;
    }

    LargeFileDeliveredReceiptDecision decision;
    decision.shouldCleanup = false;
    decision.reason = QStringLiteral("receipt-not-matched");

    const auto makeFallback = [this](const QJsonObject& obj) {
        LargeFileDeliveredFallback fallback;
        fallback.sourceInstanceId = m_instanceId;
        fallback.transferId = obj["transferId"].toString();
        fallback.receiverId = obj["receiverId"].toString();
        fallback.objectKey = obj["objectStoreKey"].toString();
        fallback.fileHash = obj["fileHash"].toString();
        fallback.fileSize = obj["fileSize"].toVariant().toLongLong();
        return fallback;
    };
    const auto isSameRoute = [&receipt](const LargeFileDeliveredFallback& fallback) {
        return receipt.transferId == fallback.transferId.trimmed()
            && receipt.receiverId == fallback.receiverId.trimmed()
            && receipt.objectKey == fallback.objectKey.trimmed();
    };
    const auto considerPayload = [&](const QJsonObject& obj) {
        const LargeFileDeliveredFallback fallback = makeFallback(obj);
        if (!isSameRoute(fallback)) {
            return false;
        }
        decision = evaluateLargeFileDeliveredReceiptCleanup(receipt, fallback);
        return decision.shouldCleanup;
    };

    if (ensureAccountDatabase()) {
        const QString connectionName = "offline_reconcile_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = openAccountDatabase(connectionName);
            if (openAccountDatabaseConnection(db, connectionName)) {
                QSqlQuery query(db);
                query.prepare("SELECT payload FROM offline_messages WHERE receiver_id = ? ORDER BY id ASC");
                query.addBindValue(receipt.receiverId);
                if (query.exec()) {
                    while (query.next()) {
                        const QJsonDocument doc = QJsonDocument::fromJson(query.value(0).toString().toUtf8());
                        if (doc.isObject() && considerPayload(doc.object())) {
                            break;
                        }
                    }
                }
                db.close();
            }
        }
        releaseAccountDatabase(connectionName);
    }

    if (decision.shouldCleanup) {
        return decision;
    }

    QFile jsonlFile(offlineFilePath(receipt.receiverId));
    if (jsonlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!jsonlFile.atEnd()) {
            const QByteArray line = jsonlFile.readLine().trimmed();
            if (line.isEmpty()) continue;
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (doc.isObject() && considerPayload(doc.object())) {
                break;
            }
        }
        jsonlFile.close();
    }

    return decision;
}

void Server::persistRedisLargeFileDeliveredReceiptSummary(const QJsonObject& event,
                                                          const LargeFileDeliveredReceiptDecision& decision,
                                                          bool cleanupSucceeded) const {
    const QString configuredDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_DELIVERED_RECEIPT_DIR")).trimmed();
    if (configuredDir.isEmpty()) {
        return;
    }

    const QString sourceInstanceId = event["sourceInstanceId"].toString().trimmed();
    const QString transferId = event["transferId"].toString().trimmed();
    const QString receiverId = event["receiverId"].toString().trimmed();
    const QString objectKey = event["objectKey"].toString().trimmed();
    const QString fileHash = event["fileHash"].toString().trimmed().toLower();
    const qint64 confirmedBytes = event["confirmedBytes"].toVariant().toLongLong();
    LargeFileDeliveredReceipt receipt;
    receipt.sourceInstanceId = sourceInstanceId;
    receipt.transferId = transferId;
    receipt.receiverId = receiverId;
    receipt.objectKey = objectKey;
    receipt.fileHash = fileHash;
    receipt.confirmedBytes = confirmedBytes;
    if (evaluateLargeFileDeliveredReceiptCleanup(receipt, LargeFileDeliveredFallback()).reason
            == QStringLiteral("invalid-receipt")) {
        return;
    }

    QDir dir(QDir::cleanPath(configuredDir));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        qWarning() << "Failed to create delivered receipt summary directory";
        return;
    }

    QJsonObject row;
    row["sourceInstanceId"] = sourceInstanceId;
    row["transferId"] = transferId;
    row["receiverId"] = receiverId;
    row["objectKey"] = objectKey;
    row["fileHash"] = fileHash;
    row["confirmedBytes"] = QString::number(confirmedBytes);
    row["result"] = decision.shouldCleanup ? QStringLiteral("cleaned") : QStringLiteral("retained");
    row["reason"] = decision.reason;
    row["cleanupResult"] = cleanupSucceeded ? QStringLiteral("cleaned") : QStringLiteral("retained");
    row["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QFile file(dir.filePath(QStringLiteral("delivered-receipts.jsonl")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Failed to open delivered receipt summary file";
        return;
    }
    file.write(QJsonDocument(row).toJson(QJsonDocument::Compact));
    file.write("\n");
}

Server::LargeFileCleanupResult Server::cleanupDeliveredRedisLargeFile(const QJsonObject& event) const {
    LargeFileCleanupResult result;
    const QString objectKey = event["objectKey"].toString().trimmed();
    const QString transferId = event["transferId"].toString().trimmed();
    const QString receiverId = event["receiverId"].toString().trimmed();
    const QString fileHash = event["fileHash"].toString().trimmed();
    const qint64 confirmedBytes = event["confirmedBytes"].toVariant().toLongLong();
    LargeFileDeliveredReceipt receipt;
    receipt.sourceInstanceId = event["sourceInstanceId"].toString();
    receipt.transferId = transferId;
    receipt.receiverId = receiverId;
    receipt.objectKey = objectKey;
    receipt.fileHash = fileHash;
    receipt.confirmedBytes = confirmedBytes;
    if (evaluateLargeFileDeliveredReceiptCleanup(receipt, LargeFileDeliveredFallback()).reason
            == QStringLiteral("invalid-receipt")) {
        return result;
    }

    auto matchesPayload = [&](const QJsonObject& obj) {
        LargeFileDeliveredFallback fallback;
        fallback.sourceInstanceId = m_instanceId;
        fallback.transferId = obj["transferId"].toString();
        fallback.receiverId = obj["receiverId"].toString();
        fallback.objectKey = obj["objectStoreKey"].toString();
        fallback.fileHash = obj["fileHash"].toString();
        fallback.fileSize = obj["fileSize"].toVariant().toLongLong();
        return evaluateLargeFileDeliveredReceiptCleanup(receipt, fallback).shouldCleanup;
    };

    QStringList offlineAttachmentPaths;
    bool removedQueue = false;
    if (ensureAccountDatabase()) {
        const QString connectionName = "offline_delivered_" + QString::number(reinterpret_cast<quintptr>(this));
        QVector<qint64> deliveredIds;
        {
            QSqlDatabase db = openAccountDatabase(connectionName);
            if (openAccountDatabaseConnection(db, connectionName)) {
                QSqlQuery query(db);
                query.prepare("SELECT id, payload FROM offline_messages WHERE receiver_id = ? ORDER BY id ASC");
                query.addBindValue(receiverId);
                if (query.exec()) {
                    while (query.next()) {
                        const QJsonDocument doc = QJsonDocument::fromJson(query.value(1).toString().toUtf8());
                        if (!doc.isObject()) continue;
                        const QJsonObject obj = doc.object();
                        if (!matchesPayload(obj)) continue;
                        deliveredIds.append(query.value(0).toLongLong());
                        const QString offlinePath = obj["offlineFilePath"].toString();
                        if (!offlinePath.isEmpty()) {
                            offlineAttachmentPaths.append(QDir::cleanPath(offlinePath));
                        }
                    }
                }
                for (qint64 messageId : deliveredIds) {
                    QSqlQuery deleteQuery(db);
                    deleteQuery.prepare("DELETE FROM offline_messages WHERE id = ?");
                    deleteQuery.addBindValue(messageId);
                    if (deleteQuery.exec()) {
                        removedQueue = true;
                    }
                }
                db.close();
            }
        }
        releaseAccountDatabase(connectionName);
    }

    QFile jsonlFile(offlineFilePath(receiverId));
    if (jsonlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QVector<QByteArray> remainingLines;
        while (!jsonlFile.atEnd()) {
            const QByteArray line = jsonlFile.readLine().trimmed();
            if (line.isEmpty()) continue;
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (doc.isObject() && matchesPayload(doc.object())) {
                const QString offlinePath = doc.object()["offlineFilePath"].toString();
                if (!offlinePath.isEmpty()) {
                    offlineAttachmentPaths.append(QDir::cleanPath(offlinePath));
                }
                removedQueue = true;
                continue;
            }
            remainingLines.append(line);
        }
        jsonlFile.close();

        if (removedQueue) {
            if (remainingLines.isEmpty()) {
                jsonlFile.remove();
            } else if (jsonlFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                for (const QByteArray& line : remainingLines) {
                    jsonlFile.write(line);
                    jsonlFile.write("\n");
                }
                jsonlFile.close();
            }
        }
    }

    if (!removedQueue) {
        return result;
    }
    result.queueCleaned = true;

    offlineAttachmentPaths.removeDuplicates();
    for (const QString& path : offlineAttachmentPaths) {
        if (!path.isEmpty()) {
            QFile::remove(path);
        }
    }

    std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore();
    if (objectStore) {
        result.objectDeleteAttempted = true;
        result.objectDeleted = objectStore->removeObject(objectKey);
        if (result.objectDeleted) {
            result.objectDeleteReason = QStringLiteral("success");
        } else {
            result.objectDeleteReason =
                objectStoreRemoveFailureReasonForLog(objectStoreType(), objectStore->lastRemoveFailureReason());
        }
    }

    return result;
}
