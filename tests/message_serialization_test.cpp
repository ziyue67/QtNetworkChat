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

    const Message restored = Message::fromJson(original.toJson());

    bool ok = true;
    ok = expect(restored.senderId == original.senderId, "senderId should round-trip") && ok;
    ok = expect(restored.senderName == original.senderName, "senderName should round-trip") && ok;
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

    return ok ? 0 : 1;
}
