#include "e2e_backend_status_p.h"
#include <QCryptographicHash>

using namespace E2EBackendStatus;
namespace {
constexpr qsizetype MinSessionKeyBytes = 16;
constexpr quint32 DraftDhGenerator = 5u;
const char DraftDhPrivatePrefix[] = "qnc-dh1-private:";
const char DraftDhPublicPrefix[] = "qnc-dh1-public:";
}

QString normalizedE2EProtocol(QString protocol) {
    return protocol.trimmed().toLower();
}

QString normalizedE2ESuite(QString suite) {
    return suite.trimmed().toLower();
}

bool isSupportedE2EProtocol(const QString& protocol) {
    return normalizedE2EProtocol(protocol) == QString::fromLatin1(E2EProtocolV1);
}

bool isSupportedE2ESuite(const QString& suite) {
    const QString normalized = normalizedE2ESuite(suite);
    return normalized == QString::fromLatin1(E2EProductionSuite)
        || normalized == QString::fromLatin1(E2EDraftSuite);
}

QString e2eFingerprint(const QByteArray& value) {
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}


QByteArray generateE2ESessionKey() {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::SessionKeyGeneration, nullptr)) {
        return QByteArray();
    }
    if (productionBackendSelected()) {
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::SessionKeyGeneration,
                                                QByteArray(),
                                                QByteArray(),
                                                QByteArray());
        if (providerResultOk(result)
            && result.materialPolicy == QNC_E2E_MATERIAL_HANDLE_ONLY
            && result.sealedOutput.size() == SessionKeyBytes) {
            return result.sealedOutput;
        }
        return QByteArray();
    }
    return randomBytes(SessionKeyBytes);
}

QByteArray generateE2EPrivateKey() {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::IdentityKeyGeneration, nullptr)) {
        return QByteArray();
    }
    if (productionBackendSelected()) {
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::IdentityKeyGeneration,
                                                QByteArray(),
                                                QByteArray(),
                                                QByteArray());
        if (providerResultOk(result)
            && result.publicOutput.size() == 32
            && result.sealedOutput.size() == 32) {
            return result.sealedOutput;
        }
        return QByteArray();
    }
    quint32 scalar = 0;
    while (scalar < 2 || scalar >= DraftDhPrime - 1) {
        const QByteArray bytes = randomBytes(4);
        scalar = 0;
        for (const char byte : bytes) {
            scalar = (scalar << 8) | static_cast<unsigned char>(byte);
        }
        scalar = 2 + (scalar % (DraftDhPrime - 3));
    }
    return writeDhValue(DraftDhPrivatePrefix, scalar);
}

QByteArray e2ePublicKeyFromPrivateKey(const QByteArray& privateKey) {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::PublicKeyDerivation, nullptr)) {
        return QByteArray();
    }
    if (productionBackendSelected()) {
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::PublicKeyDerivation,
                                                privateKey,
                                                QByteArray(),
                                                QByteArray());
        if (providerResultOk(result)
            && result.materialPolicy == QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED
            && result.publicOutput.size() == 32) {
            return result.publicOutput;
        }
        return QByteArray();
    }
    const quint32 scalar = readDhValue(privateKey, DraftDhPrivatePrefix);
    if (scalar < 2 || scalar >= DraftDhPrime - 1) {
        return QByteArray();
    }
    return writeDhValue(DraftDhPublicPrefix, modPow(DraftDhGenerator, scalar));
}

bool signE2EKeyAgreement(E2EKeyAgreement* agreement,
                         const QByteArray& identityPrivateKey,
                         QString* reason) {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::AgreementSign, reason)) {
        if (agreement) agreement->signature.clear();
        return false;
    }
    if (!agreement) {
        return fail(reason, QStringLiteral("invalid-agreement"));
    }
    agreement->signature.clear();
    QString validationReason;
    if (!agreement->isValid(&validationReason)) {
        return fail(reason, validationReason);
    }
    const QByteArray identityPublicKey = e2ePublicKeyFromPrivateKey(identityPrivateKey);
    if (identityPublicKey.isEmpty()
        || agreement->senderIdentityFingerprint.trimmed().toLower() != e2eFingerprint(identityPublicKey)) {
        return fail(reason, QStringLiteral("identity-key-mismatch"));
    }
    if (productionBackendSelected()) {
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::AgreementSign,
                                                identityPrivateKey,
                                                agreementSignatureData(*agreement),
                                                QByteArray());
        if (!providerResultOk(result)
            || result.materialPolicy != QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED
            || result.publicOutput.isEmpty()
            || result.publicOutput.size() > MaxSignatureBytes) {
            return fail(reason, result.reason.isEmpty()
                ? QStringLiteral("signature-generation-failed")
                : result.reason);
        }
        agreement->signature = result.publicOutput;
        if (reason) {
            reason->clear();
        }
        return true;
    }
    agreement->signature = hmacSha256(identityPublicKey, agreementSignatureData(*agreement));
    if (reason) {
        reason->clear();
    }
    return true;
}

bool verifyE2EKeyAgreementSignature(const E2EKeyAgreement& agreement,
                                    const QByteArray& identityPublicKey,
                                    QString* reason) {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::AgreementVerify, reason)) {
        return false;
    }
    QString validationReason;
    if (!agreement.isValid(&validationReason)) {
        return fail(reason, validationReason);
    }
    if (identityPublicKey.isEmpty()
        || agreement.senderIdentityFingerprint.trimmed().toLower() != e2eFingerprint(identityPublicKey)) {
        return fail(reason, QStringLiteral("identity-key-mismatch"));
    }
    if (agreement.signature.isEmpty()) {
        return fail(reason, QStringLiteral("missing-signature"));
    }
    if (productionBackendSelected()) {
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::AgreementVerify,
                                                identityPublicKey,
                                                agreementSignatureData(agreement),
                                                agreement.signature);
        if (!providerResultOk(result)
            || result.materialPolicy != QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED) {
            return fail(reason, result.reason.isEmpty()
                ? QStringLiteral("signature-mismatch")
                : result.reason);
        }
        if (reason) {
            reason->clear();
        }
        return true;
    }
    const QByteArray expected = hmacSha256(identityPublicKey, agreementSignatureData(agreement));
    if (!constantTimeEqual(expected, agreement.signature)) {
        return fail(reason, QStringLiteral("signature-mismatch"));
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

QByteArray deriveE2EAuthenticatedSessionKey(const QByteArray& localPrivateKey,
                                            const E2EKeyAgreement& localAgreement,
                                            const E2EKeyAgreement& remoteAgreement,
                                            QString* reason) {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::SessionDerive, reason)) {
        return QByteArray();
    }
    QString validationReason;
    if (!localAgreement.isValid(&validationReason) || !remoteAgreement.isValid(&validationReason)) {
        fail(reason, validationReason);
        return QByteArray();
    }
    if (localAgreement.senderId.trimmed() != remoteAgreement.receiverId.trimmed()
        || localAgreement.receiverId.trimmed() != remoteAgreement.senderId.trimmed()) {
        fail(reason, QStringLiteral("transcript-peer-mismatch"));
        return QByteArray();
    }
    if (localAgreement.senderIdentityFingerprint.trimmed().toLower()
            != remoteAgreement.receiverIdentityFingerprint.trimmed().toLower()
        || localAgreement.receiverIdentityFingerprint.trimmed().toLower()
            != remoteAgreement.senderIdentityFingerprint.trimmed().toLower()) {
        fail(reason, QStringLiteral("transcript-identity-mismatch"));
        return QByteArray();
    }
    if (productionBackendSelected()) {
        const QByteArray localPublic = e2ePublicKeyFromPrivateKey(localPrivateKey);
        if (localPublic.isEmpty() || localPublic != localAgreement.publicKey) {
            fail(reason, QStringLiteral("invalid-local-key"));
            return QByteArray();
        }
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::SessionDerive,
                                                productionSessionDerivePrimary(localAgreement, remoteAgreement),
                                                productionSessionDeriveSecondary(localAgreement, remoteAgreement),
                                                productionSessionDeriveAad(localAgreement, remoteAgreement));
        if (!providerResultOk(result)
            || result.materialPolicy != QNC_E2E_MATERIAL_HANDLE_ONLY
            || result.sealedOutput.size() != SessionKeyBytes) {
            fail(reason, result.reason.isEmpty()
                ? QStringLiteral("session-derivation-failed")
                : result.reason);
            return QByteArray();
        }
        if (reason) {
            reason->clear();
        }
        return result.sealedOutput;
    }
    const quint32 privateScalar = readDhValue(localPrivateKey, DraftDhPrivatePrefix);
    const quint32 localPublic = readDhValue(localAgreement.publicKey, DraftDhPublicPrefix);
    const quint32 expectedLocalPublic = privateScalar < 2 ? 0 : modPow(DraftDhGenerator, privateScalar);
    if (privateScalar < 2 || privateScalar >= DraftDhPrime - 1 || localPublic != expectedLocalPublic) {
        fail(reason, QStringLiteral("invalid-local-key"));
        return QByteArray();
    }
    const quint32 remotePublic = readDhValue(remoteAgreement.publicKey, DraftDhPublicPrefix);
    if (remotePublic < 2 || remotePublic >= DraftDhPrime) {
        fail(reason, QStringLiteral("invalid-remote-public-key"));
        return QByteArray();
    }

    const QByteArray sharedSecret = writeDhValue("qnc-dh1-shared:", modPow(remotePublic, privateScalar));
    const QByteArray transcript = agreementTranscriptData(localAgreement, remoteAgreement, sharedSecret);
    const QByteArray sessionKey = hmacSha256(QByteArray("qtnetworkchat-e2e-session-v1"), transcript);
    if (sessionKey.size() < SessionKeyBytes) {
        fail(reason, QStringLiteral("session-derivation-failed"));
        return QByteArray();
    }
    if (reason) {
        reason->clear();
    }
    return sessionKey.left(SessionKeyBytes);
}

E2EEnvelope encryptE2EText(const QString& senderId,
                           const QString& receiverId,
                           const QString& keyId,
                           const QByteArray& sessionKey,
                           const QString& plaintext,
                           QString* reason) {
    return encryptE2EPayload(senderId,
                             receiverId,
                             keyId,
                             sessionKey,
                             plaintext.toUtf8(),
                             QStringLiteral("text/private/v1"),
                             reason);
}

bool decryptE2EText(const E2EEnvelope& envelope,
                    const QByteArray& sessionKey,
                    QString* plaintext,
                    QString* reason) {
    if (plaintext) {
        plaintext->clear();
    }
    QByteArray plainBytes;
    if (!decryptE2EPayload(envelope, sessionKey, &plainBytes, reason)) {
        return false;
    }
    const QString decoded = QString::fromUtf8(plainBytes);
    if (decoded.toUtf8() != plainBytes) {
        return fail(reason, QStringLiteral("invalid-plaintext"));
    }
    if (plaintext) {
        *plaintext = decoded;
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

E2EEnvelope encryptE2EPayload(const QString& senderId,
                              const QString& receiverId,
                              const QString& keyId,
                              const QByteArray& sessionKey,
                              const QByteArray& plaintext,
                              const QString& aad,
                              QString* reason) {
    E2EEnvelope envelope;
    envelope.protocol = QString::fromLatin1(E2EProtocolV1);
    envelope.suite = e2eDefaultSuite();
    envelope.senderId = trimmed(senderId);
    envelope.receiverId = trimmed(receiverId);
    envelope.keyId = trimmed(keyId);
    envelope.nonce = randomBytes(MinNonceBytes);
    envelope.aad = aad.trimmed().isEmpty() ? QStringLiteral("payload/private/v1") : aad.trimmed();

    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::PayloadEncrypt, reason)) {
        return envelope;
    }
    if (sessionKey.size() < MinSessionKeyBytes) {
        fail(reason, QStringLiteral("invalid-session-key"));
        return envelope;
    }
    if (plaintext.isEmpty()) {
        fail(reason, QStringLiteral("empty-plaintext"));
        return envelope;
    }

    if (productionBackendSelected()) {
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::PayloadEncrypt,
                                                sessionKey,
                                                plaintext,
                                                envelope.aad.toUtf8());
        if (!providerResultOk(result)
            || result.materialPolicy != QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED
            || result.sealedOutput.size() <= MinNonceBytes + MinTagBytes) {
            fail(reason, result.reason.isEmpty()
                ? QStringLiteral("payload-encrypt-failed")
                : result.reason);
            return E2EEnvelope();
        }
        envelope.nonce = result.sealedOutput.left(MinNonceBytes);
        envelope.tag = result.sealedOutput.mid(MinNonceBytes, MinTagBytes);
        envelope.ciphertext = result.sealedOutput.mid(MinNonceBytes + MinTagBytes);
        if (!envelope.isValid(reason)) {
            return E2EEnvelope();
        }
        if (reason) {
            reason->clear();
        }
        return envelope;
    }
    envelope.ciphertext = streamXor(sessionKey, envelope.nonce, envelope.aad, plaintext);
    envelope.tag = hmacSha256(sessionKey, envelopeTagData(envelope));
    if (!envelope.isValid(reason)) {
        return E2EEnvelope();
    }
    if (reason) {
        reason->clear();
    }
    return envelope;
}

bool decryptE2EPayload(const E2EEnvelope& envelope,
                       const QByteArray& sessionKey,
                       QByteArray* plaintext,
                       QString* reason) {
    if (plaintext) {
        plaintext->clear();
    }
    if (sessionKey.size() < MinSessionKeyBytes) {
        return fail(reason, QStringLiteral("invalid-session-key"));
    }
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::PayloadDecrypt, reason)) {
        return false;
    }
    QString validationReason;
    if (!envelope.isValid(&validationReason)) {
        return fail(reason, validationReason);
    }
    if (productionBackendSelected()) {
        const QByteArray sealed = envelope.nonce + envelope.tag + envelope.ciphertext;
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::PayloadDecrypt,
                                                sessionKey,
                                                sealed,
                                                envelope.aad.toUtf8());
        if (!providerResultOk(result)
            || result.materialPolicy != QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED) {
            return fail(reason, result.reason.isEmpty()
                ? QStringLiteral("authentication-failed")
                : result.reason);
        }
        if (plaintext) {
            *plaintext = result.publicOutput;
        }
        if (reason) {
            reason->clear();
        }
        return true;
    }
    const QByteArray expectedTag = hmacSha256(sessionKey, envelopeTagData(envelope));
    if (!constantTimeEqual(expectedTag, envelope.tag)) {
        return fail(reason, QStringLiteral("authentication-failed"));
    }
    const QByteArray plainBytes = streamXor(sessionKey, envelope.nonce, envelope.aad, envelope.ciphertext);
    if (plaintext) {
        *plaintext = plainBytes;
    }
    if (reason) {
        reason->clear();
    }
    return true;
}
