#include "e2eenvelope.h"

#include <QDebug>
#include <QJsonObject>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning() << message;
        return false;
    }
    return true;
}
}

int main() {
    bool ok = true;

    E2EKeyAgreement agreement;
    agreement.protocol = " QtNetworkChat-E2E-V1 ";
    agreement.suite = "X25519-HKDF-SHA256-AES-256-GCM";
    agreement.senderId = "10001";
    agreement.receiverId = "10002";
    agreement.keyId = "alice-bob-1";
    agreement.publicKey = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    agreement.senderIdentityFingerprint = QString(64, QLatin1Char('a'));
    agreement.receiverIdentityFingerprint = QString(64, QLatin1Char('b'));
    agreement.signature = QByteArray::fromHex("aabbccdd");

    QString reason;
    ok = expect(agreement.isValid(&reason) && reason.isEmpty(),
                "valid key agreement should pass") && ok;
    const QJsonObject backendStatus = e2eCryptoBackendStatus();
    ok = expect(backendStatus.value("backendId").toString() == QStringLiteral("draft-qt-hmac-stream-v1")
                    && backendStatus.value("suite").toString() == e2eDefaultSuite()
                    && backendStatus.value("signatureSuite").toString() == e2eAgreementSignatureSuite()
                    && !backendStatus.value("productionReady").toBool(true)
                    && !backendStatus.value("productionBackendRequestedAtBuild").toBool(true)
                    && !backendStatus.value("productionBackendAvailableAtBuild").toBool(true)
                    && backendStatus.value("productionBackendReason").toString()
                        == QStringLiteral("production-backend-not-requested")
                    && backendStatus.value("available").toBool(false),
                "default e2e backend status should explicitly identify the draft backend") && ok;

    const QJsonObject agreementJson = agreement.toJson();
    ok = expect(agreementJson.value("protocol").toString() == "qtnetworkchat-e2e-v1",
                "key agreement protocol should normalize") && ok;
    ok = expect(!agreementJson.value("publicKeyFingerprintSha256").toString().isEmpty(),
                "key agreement should expose public key fingerprint") && ok;
    ok = expect(agreementJson.value("senderIdentityFingerprintSha256").toString() == agreement.senderIdentityFingerprint
                    && agreementJson.value("receiverIdentityFingerprintSha256").toString() == agreement.receiverIdentityFingerprint,
                "key agreement should bind both identity fingerprints") && ok;

    const E2EKeyAgreement restoredAgreement = E2EKeyAgreement::fromJson(agreementJson);
    ok = expect(restoredAgreement.publicKey == agreement.publicKey,
                "key agreement public key should round-trip") && ok;
    ok = expect(restoredAgreement.senderIdentityFingerprint == agreement.senderIdentityFingerprint
                    && restoredAgreement.receiverIdentityFingerprint == agreement.receiverIdentityFingerprint,
                "key agreement identity fingerprints should round-trip") && ok;
    ok = expect(restoredAgreement.signature == agreement.signature,
                "key agreement signature should round-trip") && ok;

    E2EEnvelope envelope;
    envelope.protocol = "qtnetworkchat-e2e-v1";
    envelope.suite = "x25519-hkdf-sha256-aes-256-gcm";
    envelope.senderId = "10001";
    envelope.receiverId = "10002";
    envelope.keyId = "alice-bob-1";
    envelope.nonce = QByteArray("123456789012", 12);
    envelope.ciphertext = QByteArray("opaque ciphertext", 17);
    envelope.tag = QByteArray("1234567890abcdef", 16);
    envelope.aad = "sender=10001;receiver=10002";

    ok = expect(envelope.isValid(&reason) && reason.isEmpty(),
                "valid envelope should pass") && ok;
    const QJsonObject envelopeJson = envelope.toJson();
    ok = expect(envelopeJson.value("ciphertext").toString() != QString::fromLatin1(envelope.ciphertext),
                "ciphertext should be base64url encoded in JSON") && ok;
    ok = expect(!envelopeJson.value("ciphertextSha256").toString().isEmpty(),
                "envelope should expose ciphertext fingerprint") && ok;

    const E2EEnvelope restoredEnvelope = E2EEnvelope::fromJson(envelopeJson);
    ok = expect(restoredEnvelope.nonce == envelope.nonce,
                "envelope nonce should round-trip") && ok;
    ok = expect(restoredEnvelope.ciphertext == envelope.ciphertext,
                "envelope ciphertext should round-trip") && ok;
    ok = expect(restoredEnvelope.tag == envelope.tag,
                "envelope tag should round-trip") && ok;

    E2EEnvelope invalid = envelope;
    invalid.protocol = "unknown";
    ok = expect(!invalid.isValid(&reason) && reason == "unsupported-protocol",
                "unsupported protocol should fail closed") && ok;
    invalid = envelope;
    invalid.nonce = QByteArray("short");
    ok = expect(!invalid.isValid(&reason) && reason == "invalid-nonce",
                "short nonce should fail closed") && ok;
    invalid = envelope;
    invalid.ciphertext.clear();
    ok = expect(!invalid.isValid(&reason) && reason == "empty-ciphertext",
                "empty ciphertext should fail closed") && ok;
    invalid = envelope;
    invalid.senderId = invalid.receiverId;
    ok = expect(!invalid.isValid(&reason) && reason == "invalid-peer",
                "same sender and receiver should fail closed") && ok;

    E2EKeyAgreement invalidAgreement = agreement;
    invalidAgreement.publicKey.clear();
    ok = expect(!invalidAgreement.isValid(&reason) && reason == "invalid-public-key",
                "missing public key should fail closed") && ok;
    invalidAgreement = agreement;
    invalidAgreement.senderIdentityFingerprint = "not-a-fingerprint";
    ok = expect(!invalidAgreement.isValid(&reason) && reason == "invalid-identity-fingerprint",
                "malformed identity fingerprints should fail closed") && ok;

    const QByteArray alicePrivateKey = generateE2EPrivateKey();
    const QByteArray bobPrivateKey = generateE2EPrivateKey();
    const QByteArray alicePublicKey = e2ePublicKeyFromPrivateKey(alicePrivateKey);
    const QByteArray bobPublicKey = e2ePublicKeyFromPrivateKey(bobPrivateKey);
    ok = expect(!alicePrivateKey.isEmpty() && !bobPrivateKey.isEmpty()
                    && !alicePublicKey.isEmpty() && !bobPublicKey.isEmpty()
                    && alicePrivateKey != alicePublicKey,
                "draft key agreement should expose only derived public material") && ok;

    E2EKeyAgreement aliceAgreement;
    aliceAgreement.protocol = "qtnetworkchat-e2e-v1";
    aliceAgreement.suite = "draft-placeholder";
    aliceAgreement.senderId = "10001";
    aliceAgreement.receiverId = "10002";
    aliceAgreement.keyId = "alice-bob-auth-1";
    aliceAgreement.publicKey = alicePublicKey;
    aliceAgreement.senderIdentityFingerprint = QString(64, QLatin1Char('1'));
    aliceAgreement.receiverIdentityFingerprint = QString(64, QLatin1Char('2'));
    E2EKeyAgreement bobAgreement;
    bobAgreement.protocol = "qtnetworkchat-e2e-v1";
    bobAgreement.suite = "draft-placeholder";
    bobAgreement.senderId = "10002";
    bobAgreement.receiverId = "10001";
    bobAgreement.keyId = "alice-bob-auth-1-response";
    bobAgreement.publicKey = bobPublicKey;
    bobAgreement.senderIdentityFingerprint = aliceAgreement.receiverIdentityFingerprint;
    bobAgreement.receiverIdentityFingerprint = aliceAgreement.senderIdentityFingerprint;
    const QByteArray aliceIdentityPrivateKey = generateE2EPrivateKey();
    const QByteArray bobIdentityPrivateKey = generateE2EPrivateKey();
    const QByteArray aliceIdentityPublicKey = e2ePublicKeyFromPrivateKey(aliceIdentityPrivateKey);
    const QByteArray bobIdentityPublicKey = e2ePublicKeyFromPrivateKey(bobIdentityPrivateKey);
    aliceAgreement.senderIdentityFingerprint = e2eFingerprint(aliceIdentityPublicKey);
    aliceAgreement.receiverIdentityFingerprint = e2eFingerprint(bobIdentityPublicKey);
    bobAgreement.senderIdentityFingerprint = e2eFingerprint(bobIdentityPublicKey);
    bobAgreement.receiverIdentityFingerprint = e2eFingerprint(aliceIdentityPublicKey);
    ok = expect(signE2EKeyAgreement(&aliceAgreement, aliceIdentityPrivateKey, &reason)
                    && !aliceAgreement.signature.isEmpty()
                    && reason.isEmpty(),
                "sender should sign key agreement with local identity material") && ok;
    ok = expect(signE2EKeyAgreement(&bobAgreement, bobIdentityPrivateKey, &reason)
                    && !bobAgreement.signature.isEmpty(),
                "receiver should sign key agreement with local identity material") && ok;
    ok = expect(verifyE2EKeyAgreementSignature(aliceAgreement, aliceIdentityPublicKey, &reason)
                    && verifyE2EKeyAgreementSignature(bobAgreement, bobIdentityPublicKey, &reason),
                "signed key agreements should verify against pinned identity public material") && ok;
    E2EKeyAgreement unsignedAgreement = aliceAgreement;
    unsignedAgreement.signature.clear();
    ok = expect(!verifyE2EKeyAgreementSignature(unsignedAgreement, aliceIdentityPublicKey, &reason)
                    && reason == QStringLiteral("missing-signature"),
                "unsigned key agreement should fail closed") && ok;
    E2EKeyAgreement tamperedSignatureAgreement = aliceAgreement;
    tamperedSignatureAgreement.publicKey = e2ePublicKeyFromPrivateKey(generateE2EPrivateKey());
    ok = expect(!verifyE2EKeyAgreementSignature(tamperedSignatureAgreement, aliceIdentityPublicKey, &reason)
                    && reason == QStringLiteral("signature-mismatch"),
                "tampered agreement public material should fail signature verification") && ok;
    tamperedSignatureAgreement = aliceAgreement;
    tamperedSignatureAgreement.senderIdentityFingerprint = e2eFingerprint(bobIdentityPublicKey);
    ok = expect(!verifyE2EKeyAgreementSignature(tamperedSignatureAgreement, aliceIdentityPublicKey, &reason)
                    && reason == QStringLiteral("identity-key-mismatch"),
                "agreement signed by a different identity should fail closed") && ok;

    const QByteArray aliceDerived = deriveE2EAuthenticatedSessionKey(alicePrivateKey, aliceAgreement, bobAgreement, &reason);
    const QByteArray bobDerived = deriveE2EAuthenticatedSessionKey(bobPrivateKey, bobAgreement, aliceAgreement, &reason);
    ok = expect(aliceDerived.size() == 32 && aliceDerived == bobDerived && reason.isEmpty(),
                "authenticated key agreement should derive the same session without sending it") && ok;
    E2EKeyAgreement tamperedAgreement = bobAgreement;
    tamperedAgreement.receiverIdentityFingerprint = QString(64, QLatin1Char('3'));
    ok = expect(deriveE2EAuthenticatedSessionKey(alicePrivateKey, aliceAgreement, tamperedAgreement, &reason).isEmpty()
                    && reason == "transcript-identity-mismatch",
                "identity-bound transcript mismatch should fail closed") && ok;
    tamperedAgreement = bobAgreement;
    tamperedAgreement.publicKey = e2ePublicKeyFromPrivateKey(generateE2EPrivateKey());
    ok = expect(deriveE2EAuthenticatedSessionKey(alicePrivateKey, aliceAgreement, tamperedAgreement, &reason) != aliceDerived,
                "changing public agreement material should change the derived session") && ok;

    const QByteArray sessionKey = generateE2ESessionKey();
    ok = expect(sessionKey.size() == 32,
                "generated session key should use 32 bytes") && ok;

    const QString plaintext = "encrypted hello";
    const E2EEnvelope encrypted = encryptE2EText("10001", "10002", "alice-bob-2", sessionKey, plaintext, &reason);
    ok = expect(encrypted.isValid(&reason) && reason.isEmpty(),
                "encrypted text envelope should be valid") && ok;
    ok = expect(encrypted.ciphertext != plaintext.toUtf8(),
                "encrypted text should not expose plaintext bytes") && ok;

    QString decrypted;
    ok = expect(decryptE2EText(encrypted, sessionKey, &decrypted, &reason) && decrypted == plaintext,
                "encrypted text should decrypt with the matching session key") && ok;

    E2EEnvelope tampered = encrypted;
    tampered.ciphertext[0] = static_cast<char>(tampered.ciphertext[0] ^ 0x01);
    ok = expect(!decryptE2EText(tampered, sessionKey, &decrypted, &reason)
                    && reason == "authentication-failed",
                "tampered ciphertext should fail authentication") && ok;

    ok = expect(!decryptE2EText(encrypted, QByteArray("too-short"), &decrypted, &reason)
                    && reason == "invalid-session-key",
                "short session keys should fail closed") && ok;

    const QByteArray binaryPayload("\x00\x01binary file payload\x7f", 22);
    const E2EEnvelope encryptedPayload = encryptE2EPayload("10001",
                                                           "10002",
                                                           "alice-bob-file-1",
                                                           sessionKey,
                                                           binaryPayload,
                                                           QStringLiteral("file/private/v1;transfer-1"),
                                                           &reason);
    ok = expect(encryptedPayload.isValid(&reason)
                    && encryptedPayload.aad.startsWith(QStringLiteral("file/private/v1"))
                    && encryptedPayload.ciphertext != binaryPayload,
                "encrypted binary payload should be valid and opaque") && ok;
    QByteArray decryptedPayload;
    ok = expect(decryptE2EPayload(encryptedPayload, sessionKey, &decryptedPayload, &reason)
                    && decryptedPayload == binaryPayload,
                "encrypted binary payload should decrypt with the matching session key") && ok;
    E2EEnvelope tamperedPayload = encryptedPayload;
    tamperedPayload.aad.append(QStringLiteral(";tampered"));
    ok = expect(!decryptE2EPayload(tamperedPayload, sessionKey, &decryptedPayload, &reason)
                    && reason == QStringLiteral("authentication-failed"),
                "binary payload aad tampering should fail authentication") && ok;

    qputenv("QTNETWORKCHAT_E2E_REQUIRE_PRODUCTION_CRYPTO", "1");
    const QJsonObject requiredBackendStatus = e2eCryptoBackendStatus();
    ok = expect(requiredBackendStatus.value("productionRequired").toBool(false)
                    && !requiredBackendStatus.value("available").toBool(true)
                    && requiredBackendStatus.value("status").toString()
                        == QStringLiteral("blocked-production-backend-unavailable"),
                "production-required mode should report the draft backend as unavailable") && ok;
    ok = expect(generateE2ESessionKey().isEmpty(),
                "production-required mode should not generate draft session keys") && ok;
    ok = expect(generateE2EPrivateKey().isEmpty(),
                "production-required mode should not generate draft private keys") && ok;
    E2EKeyAgreement blockedAgreement = aliceAgreement;
    blockedAgreement.signature.clear();
    ok = expect(!signE2EKeyAgreement(&blockedAgreement, aliceIdentityPrivateKey, &reason)
                    && reason == QStringLiteral("production-crypto-backend-unavailable")
                    && blockedAgreement.signature.isEmpty(),
                "production-required mode should block draft agreement signatures") && ok;
    ok = expect(!verifyE2EKeyAgreementSignature(aliceAgreement, aliceIdentityPublicKey, &reason)
                    && reason == QStringLiteral("production-crypto-backend-unavailable"),
                "production-required mode should block draft signature verification") && ok;
    ok = expect(deriveE2EAuthenticatedSessionKey(alicePrivateKey, aliceAgreement, bobAgreement, &reason).isEmpty()
                    && reason == QStringLiteral("production-crypto-backend-unavailable"),
                "production-required mode should block draft session derivation") && ok;
    ok = expect(!encryptE2EPayload("10001",
                                   "10002",
                                   "blocked-production-required",
                                   sessionKey,
                                   binaryPayload,
                                   QStringLiteral("file/private/v1"),
                                   &reason).isValid()
                    && reason == QStringLiteral("production-crypto-backend-unavailable"),
                "production-required mode should block draft payload encryption") && ok;
    ok = expect(!decryptE2EPayload(encryptedPayload, sessionKey, &decryptedPayload, &reason)
                    && reason == QStringLiteral("production-crypto-backend-unavailable"),
                "production-required mode should block draft payload decryption") && ok;
    qunsetenv("QTNETWORKCHAT_E2E_REQUIRE_PRODUCTION_CRYPTO");

    return ok ? 0 : 1;
}
