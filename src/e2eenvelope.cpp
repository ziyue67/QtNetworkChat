#include "e2eenvelope.h"

#include <QCryptographicHash>
#include <QJsonValue>

namespace {
constexpr qsizetype MaxKeyIdLength = 128;
constexpr qsizetype MaxAadLength = 512;
constexpr qsizetype MinNonceBytes = 12;
constexpr qsizetype MaxNonceBytes = 24;
constexpr qsizetype MinTagBytes = 16;
constexpr qsizetype MaxTagBytes = 32;
constexpr qsizetype MaxPublicKeyBytes = 4096;
constexpr qsizetype MaxSignatureBytes = 4096;

QString trimmed(QString value) {
    return value.trimmed();
}

QByteArray base64Field(const QJsonObject& obj, const char* name) {
    return QByteArray::fromBase64(obj.value(QString::fromLatin1(name)).toString().toLatin1(),
                                  QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QString toBase64Url(const QByteArray& value) {
    return QString::fromLatin1(value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool fail(QString* reason, const QString& value) {
    if (reason) {
        *reason = value;
    }
    return false;
}

bool validIdentity(const QString& value) {
    return !value.trimmed().isEmpty() && value.size() <= 128;
}
}

QString normalizedE2EProtocol(QString protocol) {
    return protocol.trimmed().toLower();
}

QString normalizedE2ESuite(QString suite) {
    return suite.trimmed().toLower();
}

bool isSupportedE2EProtocol(const QString& protocol) {
    return normalizedE2EProtocol(protocol) == QStringLiteral("qtnetworkchat-e2e-v1");
}

bool isSupportedE2ESuite(const QString& suite) {
    const QString normalized = normalizedE2ESuite(suite);
    return normalized == QStringLiteral("x25519-hkdf-sha256-aes-256-gcm")
        || normalized == QStringLiteral("draft-placeholder");
}

QString e2eFingerprint(const QByteArray& value) {
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}

bool E2EKeyAgreement::isValid(QString* reason) const {
    if (!isSupportedE2EProtocol(protocol)) {
        return fail(reason, QStringLiteral("unsupported-protocol"));
    }
    if (!isSupportedE2ESuite(suite)) {
        return fail(reason, QStringLiteral("unsupported-suite"));
    }
    if (!validIdentity(senderId) || !validIdentity(receiverId) || senderId == receiverId) {
        return fail(reason, QStringLiteral("invalid-peer"));
    }
    if (keyId.trimmed().isEmpty() || keyId.size() > MaxKeyIdLength) {
        return fail(reason, QStringLiteral("invalid-key-id"));
    }
    if (publicKey.isEmpty() || publicKey.size() > MaxPublicKeyBytes) {
        return fail(reason, QStringLiteral("invalid-public-key"));
    }
    if (signature.size() > MaxSignatureBytes) {
        return fail(reason, QStringLiteral("invalid-signature"));
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

QJsonObject E2EKeyAgreement::toJson() const {
    QJsonObject obj;
    obj["protocol"] = normalizedE2EProtocol(protocol);
    obj["suite"] = normalizedE2ESuite(suite);
    obj["senderId"] = trimmed(senderId);
    obj["receiverId"] = trimmed(receiverId);
    obj["keyId"] = trimmed(keyId);
    obj["publicKey"] = toBase64Url(publicKey);
    if (!signature.isEmpty()) {
        obj["signature"] = toBase64Url(signature);
    }
    obj["publicKeyFingerprintSha256"] = e2eFingerprint(publicKey);
    return obj;
}

E2EKeyAgreement E2EKeyAgreement::fromJson(const QJsonObject& obj) {
    E2EKeyAgreement agreement;
    agreement.protocol = normalizedE2EProtocol(obj.value("protocol").toString());
    agreement.suite = normalizedE2ESuite(obj.value("suite").toString());
    agreement.senderId = trimmed(obj.value("senderId").toString());
    agreement.receiverId = trimmed(obj.value("receiverId").toString());
    agreement.keyId = trimmed(obj.value("keyId").toString());
    agreement.publicKey = base64Field(obj, "publicKey");
    agreement.signature = base64Field(obj, "signature");
    return agreement;
}

bool E2EEnvelope::isValid(QString* reason) const {
    if (!isSupportedE2EProtocol(protocol)) {
        return fail(reason, QStringLiteral("unsupported-protocol"));
    }
    if (!isSupportedE2ESuite(suite)) {
        return fail(reason, QStringLiteral("unsupported-suite"));
    }
    if (!validIdentity(senderId) || !validIdentity(receiverId) || senderId == receiverId) {
        return fail(reason, QStringLiteral("invalid-peer"));
    }
    if (keyId.trimmed().isEmpty() || keyId.size() > MaxKeyIdLength) {
        return fail(reason, QStringLiteral("invalid-key-id"));
    }
    if (nonce.size() < MinNonceBytes || nonce.size() > MaxNonceBytes) {
        return fail(reason, QStringLiteral("invalid-nonce"));
    }
    if (ciphertext.isEmpty()) {
        return fail(reason, QStringLiteral("empty-ciphertext"));
    }
    if (tag.size() < MinTagBytes || tag.size() > MaxTagBytes) {
        return fail(reason, QStringLiteral("invalid-tag"));
    }
    if (aad.size() > MaxAadLength) {
        return fail(reason, QStringLiteral("invalid-aad"));
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

QJsonObject E2EEnvelope::toJson() const {
    QJsonObject obj;
    obj["protocol"] = normalizedE2EProtocol(protocol);
    obj["suite"] = normalizedE2ESuite(suite);
    obj["senderId"] = trimmed(senderId);
    obj["receiverId"] = trimmed(receiverId);
    obj["keyId"] = trimmed(keyId);
    obj["nonce"] = toBase64Url(nonce);
    obj["ciphertext"] = toBase64Url(ciphertext);
    obj["tag"] = toBase64Url(tag);
    if (!aad.trimmed().isEmpty()) {
        obj["aad"] = aad;
    }
    obj["ciphertextSha256"] = e2eFingerprint(ciphertext);
    return obj;
}

E2EEnvelope E2EEnvelope::fromJson(const QJsonObject& obj) {
    E2EEnvelope envelope;
    envelope.protocol = normalizedE2EProtocol(obj.value("protocol").toString());
    envelope.suite = normalizedE2ESuite(obj.value("suite").toString());
    envelope.senderId = trimmed(obj.value("senderId").toString());
    envelope.receiverId = trimmed(obj.value("receiverId").toString());
    envelope.keyId = trimmed(obj.value("keyId").toString());
    envelope.nonce = base64Field(obj, "nonce");
    envelope.ciphertext = base64Field(obj, "ciphertext");
    envelope.tag = base64Field(obj, "tag");
    envelope.aad = obj.value("aad").toString();
    return envelope;
}
