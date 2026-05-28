#include "objectstore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QBuffer>
#include <QTemporaryDir>

#include <cstdio>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning() << message;
        std::fprintf(stderr, "%s\n", message);
        return false;
    }
    return true;
}

QString sha256Hex(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

class InMemoryObjectStore final : public ObjectStore {
public:
    bool writeObject(const QByteArray& data,
                     QString* objectKey,
                     QString* fileHash = nullptr,
                     QString* error = nullptr,
                     const QString& extension = QString()) const override {
        if (objectKey) {
            objectKey->clear();
        }
        if (fileHash) {
            fileHash->clear();
        }
        if (error) {
            error->clear();
        }

        QString key = FilesystemObjectStore::generateObjectKey(extension);
        while (m_objects.contains(key)) {
            key = FilesystemObjectStore::generateObjectKey(extension);
        }
        m_objects.insert(key, data);
        if (objectKey) *objectKey = key;
        if (fileHash) *fileHash = sha256Hex(data);
        return true;
    }

    ValidationResult validateObject(const QString& objectKey,
                                    qint64 expectedSize,
                                    const QString& expectedHash) const override {
        ValidationResult result;
        if (!m_objects.contains(objectKey)) {
            result.error = QStringLiteral("对象不存在或不可读");
            return result;
        }

        const QByteArray data = m_objects.value(objectKey);
        result.size = data.size();
        if (expectedSize >= 0 && result.size != expectedSize) {
            result.error = QStringLiteral("对象大小不一致");
            return result;
        }

        result.fileHash = sha256Hex(data);
        if (!expectedHash.trimmed().isEmpty()
            && result.fileHash.compare(expectedHash.trimmed(), Qt::CaseInsensitive) != 0) {
            result.error = QStringLiteral("对象哈希不一致");
            return result;
        }

        result.ok = true;
        return result;
    }

    std::unique_ptr<QIODevice> openObject(const QString& objectKey) const override {
        if (!m_objects.contains(objectKey)) {
            return {};
        }

        auto buffer = std::make_unique<QBuffer>();
        buffer->setData(m_objects.value(objectKey));
        if (!buffer->open(QIODevice::ReadOnly)) {
            return {};
        }
        return buffer;
    }

    bool removeObject(const QString& objectKey) const override {
        return m_objects.remove(objectKey) > 0;
    }

    int cleanupExpired(qint64 ttlMs, QStringList* removedKeys = nullptr) const override {
        Q_UNUSED(ttlMs);
        if (removedKeys) {
            removedKeys->clear();
        }
        return 0;
    }

private:
    mutable QHash<QString, QByteArray> m_objects;
};

bool verifyObjectStoreContract(const ObjectStore& store,
                               const QByteArray& payload,
                               const QString& extension,
                               const char* label,
                               bool expectCleanupNoOp) {
    bool ok = true;
    QString objectKey;
    QString fileHash;
    QString error;
    ok = expect(store.writeObject(payload, &objectKey, &fileHash, &error, extension),
                label) && ok;
    ok = expect(error.isEmpty(), "successful object write should not expose an error") && ok;
    ok = expect(FilesystemObjectStore::isValidObjectKey(objectKey),
                "generated object key should be valid") && ok;
    if (!extension.isEmpty()) {
        ok = expect(objectKey.endsWith("." + extension),
                    "generated object key should keep the safe extension") && ok;
    }
    ok = expect(fileHash == sha256Hex(payload), "object write should return the SHA-256 hash") && ok;

    const ObjectStore::ValidationResult valid =
        store.validateObject(objectKey, payload.size(), fileHash);
    ok = expect(valid.ok, "object validation should accept matching size and hash") && ok;
    ok = expect(valid.size == payload.size(), "validation should report object size") && ok;
    ok = expect(valid.fileHash == fileHash, "validation should report object hash") && ok;

    const ObjectStore::ValidationResult sizeMismatch =
        store.validateObject(objectKey, payload.size() + 1, fileHash);
    ok = expect(!sizeMismatch.ok && sizeMismatch.error.contains(QString::fromUtf8("大小")),
                "object validation should reject size mismatch") && ok;

    const ObjectStore::ValidationResult hashMismatch =
        store.validateObject(objectKey, payload.size(), QString::fromLatin1("not-the-right-hash"));
    ok = expect(!hashMismatch.ok && hashMismatch.error.contains(QString::fromUtf8("哈希")),
                "object validation should reject hash mismatch") && ok;

    std::unique_ptr<QIODevice> openedObject = store.openObject(objectKey);
    ok = expect(openedObject && openedObject->isOpen(),
                "object store interface should open a valid object for reading") && ok;
    if (openedObject) {
        ok = expect(openedObject->readAll() == payload,
                    "object store interface should read the written payload") && ok;
        openedObject.reset();
    }

    if (expectCleanupNoOp) {
        QStringList removedKeys;
        const int removed = store.cleanupExpired(0, &removedKeys);
        ok = expect(removed == 0 && removedKeys.isEmpty(),
                    "test object store cleanup should be a no-op") && ok;
        ok = expect(store.validateObject(objectKey, payload.size(), fileHash).ok,
                    "no-op cleanup should retain the object") && ok;
    }

    ok = expect(store.removeObject(objectKey),
                "object store interface should remove an object by key") && ok;
    ok = expect(!store.openObject(objectKey),
                "removed object should no longer open") && ok;
    ok = expect(!store.removeObject(objectKey),
                "removing an already removed object should report false") && ok;

    return ok;
}
}

int main() {
    bool ok = true;

    QTemporaryDir tempDir;
    ok = expect(tempDir.isValid(), "temporary object store directory should be available") && ok;
    if (!ok) return 1;

    FilesystemObjectStore store(tempDir.filePath("objects"));
    const ObjectStore& genericStore = store;
    InMemoryObjectStore memoryStore;
    ok = verifyObjectStoreContract(memoryStore,
                                   QByteArrayLiteral("in-memory object store payload"),
                                   QStringLiteral("bin"),
                                   "in-memory object store should write a payload",
                                   true) && ok;

    QString factoryError;
    std::unique_ptr<ObjectStore> factoryStore = createObjectStore(QString(), tempDir.filePath("factory-objects"), &factoryError);
    ok = expect(factoryStore != nullptr,
                "empty object store type should default to filesystem") && ok;
    ok = expect(factoryError.isEmpty(),
                "successful object store factory creation should not expose an error") && ok;
    ok = expect(isSupportedObjectStoreType(QStringLiteral("filesystem")),
                "filesystem object store type should be supported") && ok;
    ok = expect(!isSupportedObjectStoreType(QStringLiteral("s3")),
                "s3 object store type should be reserved but unsupported until implemented") && ok;
    std::unique_ptr<ObjectStore> unsupportedStore = createObjectStore(QStringLiteral("s3"), tempDir.filePath("s3"), &factoryError);
    ok = expect(!unsupportedStore && factoryError.contains(QString::fromUtf8("暂不支持")),
                "unsupported object store types should fail with a clear error") && ok;
    std::unique_ptr<ObjectStore> missingRootStore = createObjectStore(QStringLiteral("filesystem"), QString(), &factoryError);
    ok = expect(!missingRootStore && factoryError.contains(QString::fromUtf8("根目录")),
                "filesystem object store should require a root directory") && ok;

    const QByteArray payload("filesystem object store payload");
    QString objectKey;
    QString fileHash;
    QString error;
    ok = verifyObjectStoreContract(genericStore,
                                   payload,
                                   QStringLiteral("bin"),
                                   "filesystem object store should satisfy the generic object store contract",
                                   false) && ok;
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
