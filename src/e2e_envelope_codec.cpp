#include "e2eenvelope.h"
#include "e2e_codec_support.h"

#include <QJsonObject>

using namespace E2ECodecSupport;

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
    if (!validOptionalFingerprint(senderIdentityFingerprint)
        || !validOptionalFingerprint(receiverIdentityFingerprint)) {
        return fail(reason, QStringLiteral("invalid-identity-fingerprint"));
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
    if (!senderIdentityFingerprint.trimmed().isEmpty()) {
        obj["senderIdentityFingerprintSha256"] = senderIdentityFingerprint.trimmed().toLower();
    }
    if (!receiverIdentityFingerprint.trimmed().isEmpty()) {
        obj["receiverIdentityFingerprintSha256"] = receiverIdentityFingerprint.trimmed().toLower();
    }
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
    agreement.senderIdentityFingerprint = trimmed(obj.value("senderIdentityFingerprintSha256").toString()).toLower();
    agreement.receiverIdentityFingerprint = trimmed(obj.value("receiverIdentityFingerprintSha256").toString()).toLower();
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
