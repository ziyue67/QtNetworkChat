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

    return ok ? 0 : 1;
}
