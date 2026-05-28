#include "objectstore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning() << message;
        return false;
    }
    return true;
}

QString sha256Hex(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
}

int main() {
    bool ok = true;

    QTemporaryDir tempDir;
    ok = expect(tempDir.isValid(), "temporary object store directory should be available") && ok;
    if (!ok) return 1;

    FilesystemObjectStore store(tempDir.filePath("objects"));
    const ObjectStore& genericStore = store;
    const QByteArray payload("filesystem object store payload");
    QString objectKey;
    QString fileHash;
    QString error;
    ok = expect(genericStore.writeObject(payload, &objectKey, &fileHash, &error, "bin"),
                "object store should write a payload") && ok;
    ok = expect(error.isEmpty(), "successful object write should not expose an error") && ok;
    ok = expect(FilesystemObjectStore::isValidObjectKey(objectKey),
                "generated object key should be valid") && ok;
    ok = expect(objectKey.endsWith(".bin"), "generated object key should keep the safe extension") && ok;
    ok = expect(fileHash == sha256Hex(payload), "object write should return the SHA-256 hash") && ok;

    const QString objectPath = store.objectPath(objectKey);
    ok = expect(!objectPath.isEmpty(), "valid object key should resolve to a path") && ok;
    ok = expect(QFileInfo::exists(objectPath), "object file should exist after write") && ok;
    ok = expect(QFileInfo(objectPath).absoluteFilePath().startsWith(QFileInfo(store.rootDir()).absoluteFilePath()),
                "object file should stay inside the configured root") && ok;

    const ObjectStore::ValidationResult valid =
        genericStore.validateObject(objectKey, payload.size(), fileHash);
    ok = expect(valid.ok, "object validation should accept matching size and hash") && ok;
    ok = expect(valid.size == payload.size(), "validation should report object size") && ok;
    ok = expect(valid.fileHash == fileHash, "validation should report object hash") && ok;

    const ObjectStore::ValidationResult sizeMismatch =
        genericStore.validateObject(objectKey, payload.size() + 1, fileHash);
    ok = expect(!sizeMismatch.ok && sizeMismatch.error.contains(QString::fromUtf8("大小")),
                "object validation should reject size mismatch") && ok;

    const ObjectStore::ValidationResult hashMismatch =
        genericStore.validateObject(objectKey, payload.size(), QString::fromLatin1("not-the-right-hash"));
    ok = expect(!hashMismatch.ok && hashMismatch.error.contains(QString::fromUtf8("哈希")),
                "object validation should reject hash mismatch") && ok;

    ok = expect(!FilesystemObjectStore::isValidObjectKey("../escape.bin"),
                "path traversal object key should be rejected") && ok;
    ok = expect(!FilesystemObjectStore::isValidObjectKey("nested/escape.bin"),
                "nested path object key should be rejected") && ok;
    ok = expect(!FilesystemObjectStore::isValidObjectKey("C:\\temp\\escape.bin"),
                "absolute Windows path object key should be rejected") && ok;
    ok = expect(store.objectPath("../escape.bin").isEmpty(),
                "path traversal key should not resolve to an object path") && ok;

    const std::unique_ptr<QIODevice> openedObject = genericStore.openObject(objectKey);
    ok = expect(openedObject && openedObject->isOpen(),
                "object store interface should open a valid object for reading") && ok;
    if (openedObject) {
        ok = expect(openedObject->readAll() == payload,
                    "object store interface should read the written payload") && ok;
    }

    QString expiredKey;
    ok = expect(genericStore.writeObject(QByteArrayLiteral("expired object"), &expiredKey, nullptr, &error, "dat"),
                "object store should write an expired-test payload") && ok;
    const QString expiredPath = store.objectPath(expiredKey);
    QFile expiredFile(expiredPath);
    ok = expect(expiredFile.open(QIODevice::ReadWrite),
                "expired-test object should reopen for timestamp update") && ok;
    if (expiredFile.isOpen()) {
        const QDateTime oldTime = QDateTime::currentDateTimeUtc().addSecs(-7200);
        ok = expect(expiredFile.setFileTime(oldTime, QFileDevice::FileModificationTime),
                    "expired-test object modification time should be adjustable") && ok;
        expiredFile.close();
    }

    QString freshKey;
    ok = expect(genericStore.writeObject(QByteArrayLiteral("fresh object"), &freshKey, nullptr, &error, "dat"),
                "object store should write a fresh-test payload") && ok;
    QStringList removedKeys;
    ok = expect(genericStore.cleanupExpired(60 * 60 * 1000, &removedKeys) == 1,
                "object cleanup should remove exactly one expired object") && ok;
    ok = expect(removedKeys.size() == 1 && removedKeys.first() == expiredKey,
                "object cleanup should report the removed object key") && ok;
    ok = expect(!QFileInfo::exists(expiredPath),
                "expired object should be removed from disk") && ok;
    ok = expect(QFileInfo::exists(store.objectPath(freshKey)),
                "fresh object should be kept during cleanup") && ok;
    ok = expect(genericStore.removeObject(freshKey),
                "object store interface should remove an object by key") && ok;
    ok = expect(!QFileInfo::exists(store.objectPath(freshKey)),
                "removed object should no longer exist on disk") && ok;

    return ok ? 0 : 1;
}
