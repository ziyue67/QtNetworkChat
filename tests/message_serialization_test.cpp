#include "message.h"

#include <QCryptographicHash>
#include <QDebug>

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
    Message original;
    original.senderId = "10001";
    original.senderName = "Alice";
    original.senderAvatar = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAAB";
    original.receiverId = "10002";
    original.content = "file payload";
    original.fileName = "report.bin";
    original.fileData = QByteArray("chunk-one\0chunk-two", 19);
    original.fileSize = original.fileData.size();
    original.fileHash = QString::fromLatin1(
        QCryptographicHash::hash(original.fileData, QCryptographicHash::Sha256).toHex());
    original.chunkSize = 8;
    original.chunkCount = 3;
    original.type = MessageType::File;
    original.timestamp = QDateTime::fromString("2026-05-24T15:54:00", Qt::ISODate);
    original.e2eKeyAgreement.protocol = "qtnetworkchat-e2e-v1";
    original.e2eKeyAgreement.suite = "x25519-hkdf-sha256-aes-256-gcm";
    original.e2eKeyAgreement.senderId = original.senderId;
    original.e2eKeyAgreement.receiverId = original.receiverId;
    original.e2eKeyAgreement.keyId = "alice-bob-1";
    original.e2eKeyAgreement.publicKey = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    original.e2eKeyAgreement.senderIdentityFingerprint = QString(64, QLatin1Char('a'));
    original.e2eKeyAgreement.receiverIdentityFingerprint = QString(64, QLatin1Char('b'));
    original.e2eKeyAgreement.signature = QByteArray::fromHex("aabbccddeeff00112233445566778899");
    original.e2eEnvelope.protocol = "qtnetworkchat-e2e-v1";
    original.e2eEnvelope.suite = "x25519-hkdf-sha256-aes-256-gcm";
    original.e2eEnvelope.senderId = original.senderId;
    original.e2eEnvelope.receiverId = original.receiverId;
    original.e2eEnvelope.keyId = "alice-bob-1";
    original.e2eEnvelope.nonce = QByteArray("123456789012", 12);
    original.e2eEnvelope.ciphertext = QByteArray("encrypted-payload", 17);
    original.e2eEnvelope.tag = QByteArray("1234567890abcdef", 16);
    original.e2eFileEncrypted = true;
    original.e2eFileKeyId = original.e2eEnvelope.keyId;
    original.e2eFileKeyFingerprint = QString(64, QLatin1Char('a'));
    original.e2eFilePlainSize = 19;
    original.e2eFilePlainHash = QString(64, QLatin1Char('b'));

    const Message restored = Message::fromJson(original.toJson());

    bool ok = true;
    ok = expect(restored.senderId == original.senderId, "senderId should round-trip") && ok;
    ok = expect(restored.senderName == original.senderName, "senderName should round-trip") && ok;
    ok = expect(restored.senderAvatar == original.senderAvatar, "senderAvatar should round-trip") && ok;
    ok = expect(restored.receiverId == original.receiverId, "receiverId should round-trip") && ok;
    ok = expect(restored.content == original.content, "content should round-trip") && ok;
    ok = expect(restored.fileName == original.fileName, "fileName should round-trip") && ok;
    ok = expect(restored.fileData == original.fileData, "fileData should round-trip") && ok;
    ok = expect(restored.fileSize == original.fileSize, "fileSize should round-trip") && ok;
    ok = expect(restored.fileHash == original.fileHash, "fileHash should round-trip") && ok;
    ok = expect(restored.chunkSize == original.chunkSize, "chunkSize should round-trip") && ok;
    ok = expect(restored.chunkCount == original.chunkCount, "chunkCount should round-trip") && ok;
    ok = expect(restored.type == original.type, "message type should round-trip") && ok;
    ok = expect(restored.timestamp == original.timestamp, "timestamp should round-trip") && ok;
    ok = expect(restored.e2eKeyAgreement.publicKey == original.e2eKeyAgreement.publicKey,
                "e2e key agreement should round-trip") && ok;
    ok = expect(restored.e2eKeyAgreement.signature == original.e2eKeyAgreement.signature,
                "signed e2e key agreement should round-trip") && ok;
    ok = expect(restored.e2eEnvelope.ciphertext == original.e2eEnvelope.ciphertext,
                "e2e envelope should round-trip") && ok;
    ok = expect(restored.e2eEnvelope.tag == original.e2eEnvelope.tag,
                "e2e envelope auth tag should round-trip") && ok;
    ok = expect(restored.e2eFileEncrypted
                    && restored.e2eFileKeyId == original.e2eFileKeyId
                    && restored.e2eFileKeyFingerprint == original.e2eFileKeyFingerprint
                    && restored.e2eFilePlainSize == original.e2eFilePlainSize
                    && restored.e2eFilePlainHash == original.e2eFilePlainHash,
                "e2e file metadata should round-trip") && ok;

    return ok ? 0 : 1;
}
