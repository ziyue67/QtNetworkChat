#include "server_delivery_support.h"
#include "message.h"
#include <QJsonDocument>
#include <QTcpSocket>
#include <QVariant>
#include <QDebug>

namespace ServerDeliverySupport {

bool looksLikeSha256Hex(const QString& value) {
    const QString trimmed = value.trimmed();
    if (trimmed.size() != 64) return false;
    for (const QChar& ch : trimmed) {
        const ushort c = ch.toLatin1();
        const bool isHex = (c >= '0' && c <= '9')
            || (c >= 'a' && c <= 'f')
            || (c >= 'A' && c <= 'F');
        if (!isHex) return false;
    }
    return true;
}

void appendE2EFields(QJsonObject* obj, const Message& msg) {
    if (!obj) return;
    QString reason;
    if (msg.e2eEnvelope.isValid(&reason)) {
        (*obj)["e2eEnvelope"] = msg.e2eEnvelope.toJson();
        (*obj)["isEncrypted"] = true;
    } else if (!msg.e2eEnvelopeHeader.isEmpty()
               && e2eEnvelopeHeaderLooksSafe(msg.e2eEnvelopeHeader)) {
        (*obj)["e2eEnvelope"] = msg.e2eEnvelopeHeader;
        (*obj)["isEncrypted"] = true;
    }
    if (msg.e2eFileEncrypted) {
        (*obj)["e2eFileEncrypted"] = true;
        (*obj)["e2eFileKeyId"] = msg.e2eFileKeyId;
        (*obj)["e2eFileKeyFingerprintSha256"] = msg.e2eFileKeyFingerprint;
        (*obj)["e2eFilePlainSize"] = QString::number(msg.e2eFilePlainSize);
        (*obj)["e2eFilePlainHash"] = msg.e2eFilePlainHash;
    }
    if (msg.e2eKeyAgreement.isValid(&reason)) {
        (*obj)["e2eKeyAgreement"] = msg.e2eKeyAgreement.toJson();
    }
}

QJsonObject e2eEnvelopeHeaderJson(const E2EEnvelope& envelope) {
    QJsonObject header = envelope.toJson();
    header.remove(QStringLiteral("ciphertext"));
    return header;
}

bool e2eEnvelopeHeaderLooksSafe(const QJsonObject& header) {
    const QByteArray nonce = QByteArray::fromBase64(header.value("nonce").toString().toLatin1(),
                                                    QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QByteArray tag = QByteArray::fromBase64(header.value("tag").toString().toLatin1(),
                                                  QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return isSupportedE2EProtocol(header.value("protocol").toString())
        && isSupportedE2ESuite(header.value("suite").toString())
        && !header.value("senderId").toString().trimmed().isEmpty()
        && !header.value("receiverId").toString().trimmed().isEmpty()
        && header.value("senderId").toString().trimmed() != header.value("receiverId").toString().trimmed()
        && !header.value("keyId").toString().trimmed().isEmpty()
        && nonce.size() >= 8
        && nonce.size() <= 64
        && tag.size() >= 8
        && tag.size() <= 128
        && header.value("aad").toString().size() <= 512
        && (!header.contains("ciphertextSha256")
            || looksLikeSha256Hex(header.value("ciphertextSha256").toString()));
}

void appendE2EFileFields(QJsonObject* target, const QJsonObject& source, bool includeCiphertext) {
    if (!target || !source.value("e2eFileEncrypted").toBool(false)) {
        return;
    }

    const QString keyId = source.value("e2eFileKeyId").toString().trimmed();
    const QString keyFingerprint = source.value("e2eFileKeyFingerprintSha256").toString().trimmed().toLower();
    const qint64 plainSize = source.value("e2eFilePlainSize").toVariant().toLongLong();
    const QString plainHash = source.value("e2eFilePlainHash").toString().trimmed().toLower();
    if (keyId.isEmpty() || !looksLikeSha256Hex(keyFingerprint) || plainSize <= 0 || !looksLikeSha256Hex(plainHash)) {
        return;
    }

    (*target)["e2eFileEncrypted"] = true;
    (*target)["e2eFileKeyId"] = keyId;
    (*target)["e2eFileKeyFingerprintSha256"] = keyFingerprint;
    (*target)["e2eFilePlainSize"] = QString::number(plainSize);
    (*target)["e2eFilePlainHash"] = plainHash;
    if (source.value("e2eEnvelope").isObject()) {
        QJsonObject envelopeObject = source.value("e2eEnvelope").toObject();
        if (includeCiphertext || envelopeObject.contains(QStringLiteral("ciphertext"))) {
            const E2EEnvelope envelope = E2EEnvelope::fromJson(envelopeObject);
            if (!envelope.isValid()) {
                return;
            }
            envelopeObject = includeCiphertext ? envelope.toJson() : e2eEnvelopeHeaderJson(envelope);
        }
        if (e2eEnvelopeHeaderLooksSafe(envelopeObject)) {
            (*target)["e2eEnvelope"] = envelopeObject;
            (*target)["isEncrypted"] = true;
        }
    }
}

namespace {
QString metricFieldValue(QString value) {
    value = value.trimmed().left(160);
    for (QChar& ch : value) {
        if (ch.isSpace()) {
            ch = QLatin1Char('_');
        }
    }
    return value;
}

}

void logRedisLargeFileRouteEvent(const QString& eventName,
                                 const QString& result,
                                 const QJsonObject& metadata,
                                 const QString& reason,
                                 qint64 bytes) {
    QStringList fields;
    fields << QStringLiteral("event=%1").arg(eventName)
           << QStringLiteral("result=%1").arg(result);

    const auto appendStringField = [&fields, &metadata](const char* key) {
        const QString value = metadata[QString::fromLatin1(key)].toString().trimmed();
        if (!value.isEmpty()) {
            fields << QStringLiteral("%1=%2").arg(QString::fromLatin1(key), metricFieldValue(value));
        }
    };

    appendStringField("sourceInstanceId");
    appendStringField("transferId");
    appendStringField("objectKey");
    appendStringField("receiverId");
    appendStringField("groupId");
    appendStringField("targetUserId");
    appendStringField("fileHash");
    appendStringField("fileName");
    appendStringField("messageType");
    appendStringField("storeType");
    appendStringField("operation");
    if (bytes >= 0) {
        fields << QStringLiteral("bytes=%1").arg(bytes);
    }
    if (!reason.trimmed().isEmpty()) {
        fields << QStringLiteral("reason=%1").arg(metricFieldValue(reason));
    }

    const QString line = QStringLiteral("redis_large_file_route %1").arg(fields.join(QLatin1Char(' ')));
    const bool isExpectedSuccess = result == QLatin1String("published")
        || result == QLatin1String("cleaned")
        || result == QLatin1String("removed");
    if (eventName == QLatin1String("failed") || !isExpectedSuccess) {
        qWarning().noquote() << line;
    } else {
        qInfo().noquote() << line;
    }
}

QJsonObject largeFileRouteLogMetadata(QJsonObject metadata,
                                      const QString& storeType,
                                      const QString& operation) {
    const QString trimmedStoreType = storeType.trimmed();
    if (!trimmedStoreType.isEmpty()) {
        metadata["storeType"] = trimmedStoreType;
    }
    const QString trimmedOperation = operation.trimmed();
    if (!trimmedOperation.isEmpty()) {
        metadata["operation"] = trimmedOperation;
    }
    return metadata;
}

void sendSystemNotice(QTcpSocket* socket, const QString& content) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

    QJsonObject response;
    response["type"] = "system";
    response["content"] = content;
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

} // namespace ServerDeliverySupport
