#include "server.h"
#include "server_delivery_support.h"
#include "server_group_support.h"
#include "qqnt_redis_service.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QEventLoop>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>
#include <QUuid>
#include <algorithm>

namespace {
using namespace ServerDeliverySupport;

constexpr qint64 kMaxIncomingPayloadBytes = 80LL * 1024 * 1024;
constexpr qint64 kMaxIncomingChunks = 4096;
constexpr int kChunkAckTimeoutMs = 4000;
constexpr qint64 kTransferStaleTimeoutMs = 2LL * 60 * 1000;

void sendFileChunkAck(QTcpSocket* socket,
                      const QString& transferId,
                      qint64 chunkIndex,
                      bool accepted,
                      const QString& reason = QString(),
                      qint64 receivedBytes = 0) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState || transferId.isEmpty()) return;

    QJsonObject response;
    response["type"] = "file_chunk_ack";
    response["transferId"] = transferId;
    response["chunkIndex"] = QString::number(chunkIndex);
    response["accepted"] = accepted;
    response["reason"] = reason;
    response["receivedBytes"] = QString::number(receivedBytes);
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

QString pendingFileTransferKey(const QString& senderId, const QString& transferId) {
    if (senderId.trimmed().isEmpty() || transferId.trimmed().isEmpty()) {
        return QString();
    }
    return senderId.trimmed() + ":" + transferId.trimmed();
}
} // namespace

void Server::handleFile(const QJsonObject& obj, QTcpSocket* socket) {
    Message msg;
    msg.senderId = obj["senderId"].toString();
    msg.senderName = obj["senderName"].toString();
    msg.receiverId = obj["receiverId"].toString();
    const QString serverGroupId = obj["groupId"].toString().trimmed();
    msg.content = obj["content"].toString();
    msg.fileName = obj["fileName"].toString();
    msg.transferId = obj["transferId"].toString().trimmed();
    msg.fileSize = obj["fileSize"].toVariant().toLongLong();
    msg.fileHash = obj["fileHash"].toString();
    msg.e2eFileEncrypted = obj["e2eFileEncrypted"].toBool(false);
    msg.e2eFileKeyId = obj["e2eFileKeyId"].toString();
    msg.e2eFileKeyFingerprint = obj["e2eFileKeyFingerprintSha256"].toString();
    msg.e2eFilePlainSize = obj["e2eFilePlainSize"].toVariant().toLongLong();
    msg.e2eFilePlainHash = obj["e2eFilePlainHash"].toString();
    if (obj.value("e2eEnvelope").isObject()) {
        const QJsonObject envelopeObject = obj.value("e2eEnvelope").toObject();
        const E2EEnvelope envelope = E2EEnvelope::fromJson(envelopeObject);
        if (envelope.isValid()) {
            msg.e2eEnvelope = envelope;
        } else if (e2eEnvelopeHeaderLooksSafe(envelopeObject)) {
            msg.e2eEnvelopeHeader = envelopeObject;
        }
    }
    if (msg.e2eFileEncrypted
        && !msg.e2eEnvelope.isValid()
        && msg.e2eEnvelopeHeader.isEmpty()) {
        sendSystemNotice(socket, QStringLiteral("端到端加密文件转发失败：信封头无效"));
        return;
    }
    const qint64 declaredChunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 declaredChunkCount = obj["chunkCount"].toVariant().toLongLong();
    msg.chunkSize = declaredChunkSize;
    msg.chunkCount = declaredChunkCount;
    msg.type = static_cast<MessageType>(obj["messageType"].toInt(static_cast<int>(MessageType::File)));
    msg.timestamp = QDateTime::currentDateTime();

    if (ChatUser* sender = findUserBySocket(socket)) {
        msg.senderId = sender->id;
        msg.senderName = sender->name;
        msg.senderAvatar = sender->avatar;
    }
    const QString routedGroupId = !serverGroupId.isEmpty()
        ? ServerGroupSupport::normalizeGroupId(serverGroupId)
        : (msg.receiverId.isEmpty() ? QStringLiteral("public") : QString());
    if (routedGroupId == QLatin1String("public")) {
        ensurePublicGroupMembership(msg.senderId, socket);
    }
    if (!routedGroupId.isEmpty() && !isServerGroupMember(routedGroupId, msg.senderId)) {
        QJsonObject details;
        details["fileName"] = msg.fileName;
        details["reason"] = QStringLiteral("not-member");
        details["filePolicy"] = ServerGroupSupport::filePolicy(ServerGroupSupport::groupType(routedGroupId));
        recordServerGroupAuditEvent(routedGroupId,
                                    QStringLiteral("file_rejected"),
                                    msg.senderId,
                                    msg.senderName,
                                    msg.senderId,
                                    msg.senderName,
                                    details);
        const QStringList memberIds = serverGroupMemberIds(routedGroupId);
        for (const QString& memberId : memberIds) {
            QTcpSocket* memberSocket = m_userSockets.value(memberId);
            if (memberSocket && memberSocket->state() == QAbstractSocket::ConnectedState) {
                sendServerGroupSnapshot(memberId, memberSocket);
            }
        }
        sendSystemNotice(socket, routedGroupId == QLatin1String("public")
            ? QStringLiteral("公共群文件发送失败：你已不在该群组，请联系群主或管理员重新邀请。")
            : QStringLiteral("私有群文件发送失败：你不在该群组或已被移出。"));
        return;
    }

    QString base64Data = obj["fileData"].toString();
    if (!base64Data.isEmpty()) {
        msg.fileData = QByteArray::fromBase64(base64Data.toLatin1());
    }
    const qint64 declaredSize = msg.fileSize;
    const QString declaredHash = msg.fileHash.trimmed();
    const qint64 actualSize = msg.fileData.size();
    const QString actualHash = msg.fileData.isEmpty()
        ? QString()
        : QString::fromLatin1(QCryptographicHash::hash(msg.fileData, QCryptographicHash::Sha256).toHex());

    QStringList integrityErrors;
    if (actualSize <= 0) {
        integrityErrors << "文件内容为空";
    }
    if (declaredSize < 0) {
        integrityErrors << "声明大小非法";
    }
    if (declaredSize > kMaxIncomingPayloadBytes || actualSize > kMaxIncomingPayloadBytes) {
        integrityErrors << QString("超过服务器限制 %1 MB").arg(kMaxIncomingPayloadBytes / 1024 / 1024);
    }
    if (declaredSize > 0 && declaredSize != actualSize) {
        integrityErrors << QString("大小不一致：声明 %1 字节，实际 %2 字节").arg(declaredSize).arg(actualSize);
    }
    if (!msg.e2eFileEncrypted && !declaredHash.isEmpty() && actualHash.compare(declaredHash, Qt::CaseInsensitive) != 0) {
        integrityErrors << "SHA-256 不一致";
    }
    if (declaredChunkSize < 0 || declaredChunkCount < 0) {
        integrityErrors << "分片元数据非法";
    } else if (declaredChunkSize > 0 || declaredChunkCount > 0) {
        if (declaredChunkSize <= 0 || declaredChunkCount <= 0) {
            integrityErrors << "分片元数据不完整";
        } else {
            const qint64 basisSize = declaredSize > 0 ? declaredSize : actualSize;
            const qint64 expectedChunkCount = (basisSize + declaredChunkSize - 1) / declaredChunkSize;
            if (expectedChunkCount != declaredChunkCount) {
                integrityErrors << QString("分片数量不一致：声明 %1 片，预期 %2 片")
                                       .arg(declaredChunkCount)
                                       .arg(expectedChunkCount);
            }
        }
    }
    if (!integrityErrors.isEmpty()) {
        const QString visibleName = msg.fileName.isEmpty() ? "未命名文件" : msg.fileName;
        sendSystemNotice(socket, QString("文件传输已被服务端拒绝：%1，%2。请重新发送。")
                                .arg(visibleName, integrityErrors.join("；")));
        qWarning() << "Rejected file transfer from" << msg.senderId << msg.fileName << integrityErrors;
        return;
    }

    if (msg.fileSize <= 0) {
        msg.fileSize = msg.fileData.size();
    }
    if (msg.fileHash.isEmpty() && !actualHash.isEmpty()) {
        msg.fileHash = actualHash;
    }

    QString deliveryState = "broadcast";
    bool serverGroupLargeFileOfferEligible = false;
    bool serverGroupLargeFileOfferPublished = false;
    if (!routedGroupId.isEmpty() && routedGroupId != QLatin1String("public")) {
        msg.receiverId = routedGroupId;
        deliveryState = "server-group-file";
        const QStringList memberIds = serverGroupMemberIds(routedGroupId);
        for (const QString& memberId : memberIds) {
            if (memberId == msg.senderId) continue;
            QTcpSocket* memberSocket = m_userSockets.value(memberId);
            if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
            Message groupMsg = msg;
            groupMsg.receiverId = routedGroupId;
            if (groupMsg.type == MessageType::File || groupMsg.type == MessageType::Image) {
                sendChunkedFileToSocket(groupMsg, memberSocket);
            } else {
                QJsonObject forwarded = QJsonDocument::fromJson(groupMsg.toJson()).object();
                forwarded["type"] = "server_group_message";
                forwarded["groupId"] = routedGroupId;
                memberSocket->write(QJsonDocument(forwarded).toJson(QJsonDocument::Compact));
                memberSocket->write("\n");
                memberSocket->flush();
            }
        }
        QJsonObject details;
        details["fileName"] = msg.fileName;
        details["fileSize"] = QString::number(msg.fileSize);
        details["transferId"] = msg.transferId;
        details["filePolicy"] = ServerGroupSupport::filePolicy(QStringLiteral("private"));
        recordServerGroupAuditEvent(routedGroupId,
                                    QStringLiteral("file_sent"),
                                    msg.senderId,
                                    msg.senderName,
                                    msg.senderId,
                                    msg.senderName,
                                    details);
        for (const QString& memberId : memberIds) {
            QTcpSocket* memberSocket = m_userSockets.value(memberId);
            if (memberSocket && memberSocket->state() == QAbstractSocket::ConnectedState) {
                sendServerGroupSnapshot(memberId, memberSocket);
            }
        }
        serverGroupLargeFileOfferEligible = shouldPublishServerGroupLargeFileOffer(msg, memberIds);
        if (serverGroupLargeFileOfferEligible) {
            serverGroupLargeFileOfferPublished = publishRedisServerGroupLargeFileOffer(msg, memberIds);
        }
    } else if (!msg.receiverId.isEmpty()) {
        QTcpSocket* targetSocket = m_userSockets.value(msg.receiverId);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            deliveryState = "direct";
            sendToUser(msg);
        } else {
            bool redisOnline = false;
            const bool redisPresenceKnown = isRedisUserOnline(msg.receiverId, &redisOnline);
            if (redisPresenceKnown && redisOnline) {
                if (canPublishRedisMessageEvent(msg, QStringLiteral("remote"))) {
                    deliveryState = "remote";
                } else if (shouldPublishLargeFileOffer(msg)) {
                    deliveryState = "offline";
                    sendToUser(msg);
                } else {
                    sendSystemNotice(socket, QStringLiteral("文件发送失败：Redis 文件路由负载不可用，请调整文件后重试。"));
                    return;
                }
            } else {
                deliveryState = "offline";
                sendToUser(msg);
            }
        }
    } else {
        broadcastMessage(msg, socket);
        QJsonObject details;
        details["fileName"] = msg.fileName;
        details["fileSize"] = QString::number(msg.fileSize);
        details["transferId"] = msg.transferId;
        details["filePolicy"] = ServerGroupSupport::filePolicy(QStringLiteral("public"));
        recordServerGroupAuditEvent(QStringLiteral("public"),
                                    QStringLiteral("file_sent"),
                                    msg.senderId,
                                    msg.senderName,
                                    msg.senderId,
                                    msg.senderName,
                                    details);
    }
    saveMessageToSqlite(msg, deliveryState);
    const bool redisPublished = publishRedisMessageEvent(msg, deliveryState);
    if (deliveryState == "remote" && !redisPublished) {
        sendSystemNotice(socket, QStringLiteral("文件发送失败：Redis 路由不可用，请等待服务恢复。"));
        return;
    }
    if (deliveryState == "server-group-file" && m_redisService->isEnabled()) {
        const bool isLargeServerGroupPayload =
            (msg.type == MessageType::File || msg.type == MessageType::Image)
            && msg.fileData.size() > kRedisPubSubFileMaxBytes;
        const bool redisRouteExpected = !isLargeServerGroupPayload || serverGroupLargeFileOfferEligible;
        const bool redisRouteDelivered = redisPublished || serverGroupLargeFileOfferPublished;
        if (redisRouteExpected && !redisRouteDelivered) {
            sendSystemNotice(socket, QStringLiteral("群文件跨实例路由失败：其他实例成员可能未收到。"));
        }
    }
}

void Server::handleFileChunk(const QJsonObject& obj, QTcpSocket* socket) {
    if (!socket) return;

    const QString transferId = obj["transferId"].toString().trimmed();
    const QString fileName = obj["fileName"].toString();
    const qint64 fileSize = obj["fileSize"].toVariant().toLongLong();
    const qint64 chunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 chunkCount = obj["chunkCount"].toVariant().toLongLong();
    const qint64 chunkIndex = obj["chunkIndex"].toVariant().toLongLong();
    const QByteArray chunkData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());
    const QString serverGroupId = obj["groupId"].toString().trimmed();
    const ChatUser* sender = findUserBySocket(socket);
    const QString declaredSenderId = obj["senderId"].toString().trimmed();
    const QString senderId = sender ? sender->id : QString();
    const QString key = pendingFileTransferKey(senderId, transferId);

    auto rejectTransfer = [this, socket, key, fileName, transferId, chunkIndex, serverGroupId, senderId](const QString& reason) {
        if (!key.isEmpty()) {
            m_pendingFileTransfers.remove(key);
        }
        if (!serverGroupId.isEmpty() && !senderId.isEmpty()) {
            QJsonObject details;
            details["fileName"] = fileName;
            details["reason"] = reason;
            details["filePolicy"] = ServerGroupSupport::filePolicy(ServerGroupSupport::groupType(serverGroupId));
            recordServerGroupAuditEvent(serverGroupId,
                                        QStringLiteral("file_rejected"),
                                        senderId,
                                        QString(),
                                        senderId,
                                        QString(),
                                        details);
            const QStringList memberIds = serverGroupMemberIds(serverGroupId);
            for (const QString& memberId : memberIds) {
                QTcpSocket* memberSocket = m_userSockets.value(memberId);
                if (memberSocket && memberSocket->state() == QAbstractSocket::ConnectedState) {
                    sendServerGroupSnapshot(memberId, memberSocket);
                }
            }
        }
        const QString visibleName = fileName.isEmpty() ? "未命名文件" : fileName;
        sendFileChunkAck(socket, transferId, chunkIndex, false, reason);
        sendSystemNotice(socket, QString("文件分片上传已被服务端拒绝：%1，%2。请重新发送。").arg(visibleName, reason));
        qWarning() << "Rejected file chunk transfer" << visibleName << reason;
    };

    if (transferId.isEmpty()) {
        rejectTransfer("缺少传输编号");
        return;
    }
    if (key.isEmpty()) {
        rejectTransfer("发送者身份非法");
        return;
    }
    if (!declaredSenderId.isEmpty() && declaredSenderId != senderId) {
        rejectTransfer("发送者身份不一致");
        return;
    }
    const QString routedGroupId = !serverGroupId.isEmpty()
        ? ServerGroupSupport::normalizeGroupId(serverGroupId)
        : (obj["receiverId"].toString().trimmed().isEmpty() ? QStringLiteral("public") : QString());
    if (routedGroupId == QLatin1String("public")) {
        ensurePublicGroupMembership(senderId, socket);
    }
    if (!routedGroupId.isEmpty() && !isServerGroupMember(routedGroupId, senderId)) {
        rejectTransfer("已不在该群组");
        return;
    }
    if (fileSize <= 0 || fileSize > kMaxIncomingPayloadBytes) {
        rejectTransfer(QString("文件大小非法或超过 %1 MB").arg(kMaxIncomingPayloadBytes / 1024 / 1024));
        return;
    }
    if (chunkSize <= 0 || chunkCount <= 0 || chunkIndex < 0 || chunkIndex >= chunkCount) {
        rejectTransfer("分片序号或数量非法");
        return;
    }
    if (chunkCount > kMaxIncomingChunks) {
        rejectTransfer(QString("分片数量超过服务器限制 %1 片").arg(kMaxIncomingChunks));
        return;
    }
    const qint64 expectedChunkCount = (fileSize + chunkSize - 1) / chunkSize;
    if (expectedChunkCount != chunkCount) {
        rejectTransfer(QString("分片数量不一致：声明 %1 片，预期 %2 片").arg(chunkCount).arg(expectedChunkCount));
        return;
    }
    if (chunkData.isEmpty() || chunkData.size() > chunkSize) {
        rejectTransfer("分片内容为空或超过声明大小");
        return;
    }
    if (chunkIndex < chunkCount - 1 && chunkData.size() != chunkSize) {
        rejectTransfer("非末尾分片大小不一致");
        return;
    }

    PendingFileTransfer& pending = m_pendingFileTransfers[key];
    if (pending.chunks.isEmpty()) {
        pending.envelope = obj;
        pending.envelope["type"] = "file";
        pending.envelope["senderId"] = senderId;
        pending.envelope.remove("transferId");
        pending.envelope.remove("chunkIndex");
        pending.envelope.remove("fileData");
        pending.socket = socket;
        pending.fileName = fileName;
        pending.fileSize = fileSize;
        pending.chunkSize = chunkSize;
        pending.chunkCount = chunkCount;
        pending.chunks.resize(static_cast<int>(chunkCount));
    } else if (pending.fileSize != fileSize
               || pending.chunkSize != chunkSize
               || pending.chunkCount != chunkCount
               || pending.envelope["fileHash"].toString().trimmed() != obj["fileHash"].toString().trimmed()) {
        rejectTransfer("同一传输编号的元数据不一致");
        return;
    }
    pending.socket = socket;
    pending.lastActivityMs = QDateTime::currentMSecsSinceEpoch();

    const int index = static_cast<int>(chunkIndex);
    if (!pending.receivedIndexes.contains(index)) {
        pending.chunks[index] = chunkData;
        pending.receivedIndexes.insert(index);
        pending.receivedBytes += chunkData.size();
    }
    if (pending.receivedBytes > fileSize) {
        rejectTransfer("累计分片大小超过声明文件大小");
        return;
    }
    sendFileChunkAck(socket, transferId, chunkIndex, true, QString(), pending.receivedBytes);
    if (pending.receivedIndexes.size() < pending.chunkCount) {
        return;
    }

    QByteArray fileData;
    fileData.reserve(static_cast<int>(fileSize));
    for (const QByteArray& chunk : pending.chunks) {
        if (chunk.isEmpty()) {
            rejectTransfer("存在缺失分片");
            return;
        }
        fileData.append(chunk);
    }

    QJsonObject fullFile = pending.envelope;
    m_pendingFileTransfers.remove(key);
    fullFile["transferId"] = transferId;
    if (routedGroupId == QLatin1String("public")) {
        fullFile["receiverId"] = QString();
        fullFile["groupId"] = QStringLiteral("public");
    }
    fullFile["fileData"] = QString::fromLatin1(fileData.toBase64());
    handleFile(fullFile, socket);
}

void Server::handleFileTransferResumeQuery(const QJsonObject& obj, QTcpSocket* socket) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

    const QString transferId = obj["transferId"].toString().trimmed();
    QJsonObject response;
    response["type"] = "file_transfer_resume_state";
    response["transferId"] = transferId;
    response["canResume"] = false;
    response["confirmedBytes"] = QString::number(0);
    response["nextChunkIndex"] = QString::number(0);
    response["receivedChunks"] = QJsonArray();

    if (transferId.isEmpty()) {
        response["reason"] = "缺少传输编号";
    } else {
        const ChatUser* sender = findUserBySocket(socket);
        const QString key = pendingFileTransferKey(sender ? sender->id : QString(), transferId);
        const auto it = m_pendingFileTransfers.constFind(key);
        if (key.isEmpty()) {
            response["reason"] = "发送者身份非法";
        } else if (it == m_pendingFileTransfers.constEnd()) {
            response["reason"] = "未找到未完成传输";
        } else {
            const PendingFileTransfer& pending = it.value();
            QJsonArray receivedChunks;
            QVector<int> sortedIndexes = pending.receivedIndexes.values().toVector();
            std::sort(sortedIndexes.begin(), sortedIndexes.end());
            for (int index : sortedIndexes) {
                receivedChunks.append(QString::number(index));
            }

            qint64 nextChunkIndex = 0;
            while (nextChunkIndex < pending.chunkCount
                   && pending.receivedIndexes.contains(static_cast<int>(nextChunkIndex))) {
                ++nextChunkIndex;
            }

            response["canResume"] = true;
            response["reason"] = "";
            response["fileName"] = pending.fileName;
            response["fileSize"] = QString::number(pending.fileSize);
            response["chunkSize"] = QString::number(pending.chunkSize);
            response["chunkCount"] = QString::number(pending.chunkCount);
            response["fileHash"] = pending.envelope["fileHash"].toString();
            response["confirmedBytes"] = QString::number(pending.receivedBytes);
            response["nextChunkIndex"] = QString::number(nextChunkIndex);
            response["receivedChunks"] = receivedChunks;
        }
    }

    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

void Server::handleFileTransferCancel(const QJsonObject& obj, QTcpSocket* socket) {
    if (!socket) return;

    const QString transferId = obj["transferId"].toString().trimmed();
    if (transferId.isEmpty()) return;

    const ChatUser* sender = findUserBySocket(socket);
    const QString key = pendingFileTransferKey(sender ? sender->id : QString(), transferId);
    const bool removed = m_pendingFileTransfers.remove(key) > 0;
    const QString fileName = obj["fileName"].toString();
    const QString visibleName = fileName.isEmpty() ? "未命名文件" : fileName;

    if (removed) {
        sendSystemNotice(socket, QString("文件发送已取消，服务端已清理未完成分片：%1。").arg(visibleName));
        qDebug() << "Canceled pending file transfer" << visibleName << transferId;
    } else {
        qDebug() << "File transfer cancel received after cleanup or completion" << visibleName << transferId;
    }
}

bool Server::sendChunkedFileToSocket(const Message& msg, QTcpSocket* socket) {
    QPointer<QTcpSocket> socketGuard(socket);
    if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState || msg.fileData.isEmpty()) {
        return false;
    }

    const qint64 totalBytes = msg.fileData.size();
    const qint64 chunkSize = (msg.chunkSize > 0 && msg.chunkSize <= kForwardChunkBytes)
        ? msg.chunkSize
        : kForwardChunkBytes;
    const qint64 chunkCount = (totalBytes + chunkSize - 1) / chunkSize;
    const QString transferId = QString("%1_%2_%3")
        .arg(msg.senderId,
             QString::number(QDateTime::currentMSecsSinceEpoch()),
             QString::number(QRandomGenerator::global()->generate()));

    for (qint64 index = 0; index < chunkCount; ++index) {
        const qint64 offset = index * chunkSize;
        const QByteArray chunk = msg.fileData.mid(static_cast<int>(offset), static_cast<int>(qMin(chunkSize, totalBytes - offset)));

        QJsonObject obj;
        obj["type"] = "file_chunk";
        obj["transferId"] = transferId;
        obj["messageType"] = static_cast<int>(msg.type);
        obj["senderId"] = msg.senderId;
        obj["senderName"] = msg.senderName;
        obj["senderAvatar"] = msg.senderAvatar;
        obj["receiverId"] = msg.receiverId;
        obj["content"] = msg.content;
        obj["fileName"] = msg.fileName;
        obj["fileSize"] = QString::number(totalBytes);
        obj["fileHash"] = msg.fileHash;
        obj["chunkSize"] = QString::number(chunkSize);
        obj["chunkCount"] = QString::number(chunkCount);
        obj["chunkIndex"] = QString::number(index);
        obj["fileData"] = QString::fromLatin1(chunk.toBase64());
        appendE2EFields(&obj, msg);

        QString ackRejectReason;
        qint64 ackReceivedBytes = 0;
        bool acknowledged = false;
        const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        for (int attempt = 1; attempt <= kChunkSendMaxAttempts; ++attempt) {
            if (sendFileChunkAndWaitForAck(socketGuard, data, transferId, index, &ackRejectReason, &ackReceivedBytes)) {
                const qint64 expectedAckBytes = qMin(totalBytes, index * chunkSize + chunk.size());
                if (ackReceivedBytes > 0 && (ackReceivedBytes < expectedAckBytes || ackReceivedBytes > totalBytes)) {
                    if (attempt == kChunkSendMaxAttempts) {
                        ackRejectReason = QString::fromUtf8("文件分片确认进度非法");
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
                qWarning() << "File chunk rejected by receiver:" << ackRejectReason;
                return false;
            }
        }
        if (!acknowledged) {
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "File chunk rejected by receiver:" << ackRejectReason;
                return false;
            }
            qWarning() << "File chunk ack timeout:" << msg.fileName << index + 1 << "/" << chunkCount;
            return false;
        }
    }
    return true;
}

bool Server::sendFileChunkAndWaitForAck(QTcpSocket* socket,
                                         const QByteArray& data,
                                         const QString& transferId,
                                         qint64 chunkIndex,
                                         QString* rejectReason,
                                         qint64* receivedBytes) {
    if (rejectReason) rejectReason->clear();
    if (receivedBytes) *receivedBytes = 0;
    QPointer<QTcpSocket> socketGuard(socket);
    if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState || transferId.isEmpty() || data.isEmpty()) {
        return false;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    bool matched = false;
    bool accepted = false;
    QString reason;
    qint64 ackReceivedBytes = 0;

    QMetaObject::Connection ackConnection = connect(
        this,
        &Server::fileChunkAckReceived,
        &loop,
        [&](QTcpSocket* ackSocket, const QString& ackTransferId, qint64 ackChunkIndex, bool ackAccepted, const QString& ackReason, qint64 ackBytes) {
            // Ignore late or unrelated acknowledgements from other sockets/transfers.
            if (!socketGuard || ackSocket != socketGuard.data() || ackTransferId != transferId || ackChunkIndex != chunkIndex) return;
            matched = true;
            accepted = ackAccepted;
            reason = ackReason;
            ackReceivedBytes = ackBytes;
            loop.quit();
        });
    QMetaObject::Connection disconnectedConnection = connect(socketGuard, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QMetaObject::Connection destroyedConnection = connect(socketGuard, &QObject::destroyed, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    const bool sent = socketGuard->write(data) > 0 && socketGuard->write("\n") > 0;
    if (sent) {
        socketGuard->flush();
        timer.start(kChunkAckTimeoutMs);
        loop.exec();
    }

    QObject::disconnect(ackConnection);
    QObject::disconnect(disconnectedConnection);
    QObject::disconnect(destroyedConnection);

    if (!sent) {
        return false;
    }
    if (matched && !accepted && rejectReason) {
        *rejectReason = reason.isEmpty() ? "客户端拒绝分片" : reason;
    }
    if (matched && receivedBytes) {
        *receivedBytes = ackReceivedBytes;
    }
    return matched && accepted;
}

void Server::cleanupExpiredFileTransfers() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QString& key : m_pendingFileTransfers.keys()) {
        const auto it = m_pendingFileTransfers.constFind(key);
        if (it == m_pendingFileTransfers.constEnd()) {
            continue;
        }
        const PendingFileTransfer& pending = it.value();
        if (pending.lastActivityMs <= 0 || now - pending.lastActivityMs <= kTransferStaleTimeoutMs) {
            continue;
        }

        const QString visibleName = pending.fileName.isEmpty() ? "未命名文件" : pending.fileName;
        QTcpSocket* socket = pending.socket;
        const int receivedCount = pending.receivedIndexes.size();
        const qint64 chunkCount = pending.chunkCount;
        m_pendingFileTransfers.remove(key);
        if (socket && socket->state() == QAbstractSocket::ConnectedState) {
            sendSystemNotice(socket, QString("文件分片上传已超时清理：%1。请重新发送。").arg(visibleName));
        }
        qWarning() << "Cleaned expired incoming file transfer" << visibleName << receivedCount << "/" << chunkCount;
    }
}
