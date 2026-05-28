#include "objectstore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSslConfiguration>
#include <QSslSocket>
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
                "s3 object store type should remain disabled until implemented") && ok;
    std::unique_ptr<ObjectStore> unsupportedStore = createObjectStore(QStringLiteral("s3"), tempDir.filePath("s3"), &factoryError);
    ok = expect(!unsupportedStore && factoryError.contains(QStringLiteral("endpoint")),
                "s3 object store should fail fast when endpoint is missing") && ok;
    std::unique_ptr<ObjectStore> missingRootStore = createObjectStore(QStringLiteral("filesystem"), QString(), &factoryError);
    ok = expect(!missingRootStore && factoryError.contains(QString::fromUtf8("根目录")),
                "filesystem object store should require a root directory") && ok;
    S3ObjectStoreConfig s3Config;
    s3Config.endpoint = QStringLiteral("https://minio.internal:9000");
    s3Config.bucket = QStringLiteral("qtchat-large-files");
    s3Config.region = QStringLiteral("local");
    s3Config.accessKey = QStringLiteral("access-key");
    s3Config.secretKey = QStringLiteral("super-secret-value");
    s3Config.prefix = QStringLiteral("/qtchat/large-files/");
    QString s3Error;
    ok = expect(validateS3ObjectStoreConfig(s3Config, &s3Error),
                "valid s3 object store config should pass validation") && ok;
    ok = expect(normalizeS3ObjectPrefix(s3Config.prefix) == QStringLiteral("qtchat/large-files/"),
                "s3 prefix should be normalized without leading slash") && ok;
    const QUrl s3Url = s3ObjectUrl(s3Config, QStringLiteral("abcdef1234567890.bin"));
    ok = expect(s3Url.isValid()
                    && s3Url.toString() == QStringLiteral("https://minio.internal:9000/qtchat-large-files/qtchat/large-files/abcdef1234567890.bin"),
                "s3 object URL should use path-style bucket and normalized prefix") && ok;
    ok = expect(s3ObjectUrl(s3Config, QStringLiteral("../escape.bin")).isEmpty(),
                "s3 object URL should reject invalid object keys") && ok;
    S3ObjectStoreConfig invalidS3Config = s3Config;
    invalidS3Config.prefix = QStringLiteral("../secret");
    ok = expect(!validateS3ObjectStoreConfig(invalidS3Config, &s3Error)
                    && s3Error.contains(QString::fromUtf8("前缀"))
                    && !s3Error.contains(invalidS3Config.secretKey),
                "invalid s3 prefix should fail without leaking credentials") && ok;
    ok = expect(s3PayloadSha256Hex(QByteArray()) == QStringLiteral("e3b0c44298fc1c149afbf4c8996fb924"
                                                                    "27ae41e4649b934ca495991b7852b855"),
                "s3 payload helper should return SHA-256 hex for an empty payload") && ok;
    QMap<QString, QString> s3Headers;
    s3Headers.insert(QStringLiteral("Host"), QStringLiteral("examplebucket.s3.amazonaws.com"));
    s3Headers.insert(QStringLiteral("Range"), QStringLiteral("bytes=0-9"));
    s3Headers.insert(QStringLiteral("x-amz-content-sha256"), QStringLiteral("e3b0c44298fc1c149afbf4c8996fb924"
                                                                            "27ae41e4649b934ca495991b7852b855"));
    s3Headers.insert(QStringLiteral("x-amz-date"), QStringLiteral("20130524T000000Z"));
    QString signedHeaders;
    const QString canonicalRequest = s3CanonicalRequest(QStringLiteral("GET"),
                                                        QUrl(QStringLiteral("https://examplebucket.s3.amazonaws.com/test.txt")),
                                                        s3Headers,
                                                        QStringLiteral("e3b0c44298fc1c149afbf4c8996fb924"
                                                                       "27ae41e4649b934ca495991b7852b855"),
                                                        &signedHeaders);
    const QString expectedCanonicalRequest =
        QStringLiteral("GET\n"
                       "/test.txt\n"
                       "\n"
                       "host:examplebucket.s3.amazonaws.com\n"
                       "range:bytes=0-9\n"
                       "x-amz-content-sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n"
                       "x-amz-date:20130524T000000Z\n"
                       "\n"
                       "host;range;x-amz-content-sha256;x-amz-date\n"
                       "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    ok = expect(canonicalRequest == expectedCanonicalRequest
                    && signedHeaders == QStringLiteral("host;range;x-amz-content-sha256;x-amz-date"),
                "s3 canonical request should match the AWS Signature V4 GET object example") && ok;
    const QString credentialScope = s3CredentialScope(QStringLiteral("20130524"), QStringLiteral("us-east-1"));
    const QString stringToSign = s3StringToSign(QStringLiteral("20130524T000000Z"),
                                                credentialScope,
                                                canonicalRequest);
    ok = expect(stringToSign == QStringLiteral("AWS4-HMAC-SHA256\n"
                                               "20130524T000000Z\n"
                                               "20130524/us-east-1/s3/aws4_request\n"
                                               "7344ae5b7ee6c3e7e6b0fe0640412a37625d1fbfff95c48bbb2dc43964946972"),
                "s3 string-to-sign should match the AWS Signature V4 GET object example") && ok;
    const QString signature = s3SignatureHex(QStringLiteral("wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY"),
                                             QStringLiteral("20130524"),
                                             QStringLiteral("us-east-1"),
                                             stringToSign);
    ok = expect(signature == QStringLiteral("67fe34c8530db585abddc51067328adfedb6e42487d2566dc7d927d6e2722900"),
                "s3 signature helper should match the AWS Signature V4 GET object example") && ok;
    const QString authorizationHeader = s3AuthorizationHeader(QStringLiteral("AKIAIOSFODNN7EXAMPLE"),
                                                              credentialScope,
                                                              signedHeaders,
                                                              signature);
    ok = expect(authorizationHeader == QStringLiteral("AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/"
                                                      "20130524/us-east-1/s3/aws4_request,SignedHeaders="
                                                      "host;range;x-amz-content-sha256;x-amz-date,Signature="
                                                      "67fe34c8530db585abddc51067328adfedb6e42487d2566dc7d927d6e2722900"),
                "s3 authorization header should match the AWS Signature V4 GET object example") && ok;
    QMap<QString, QString> messyHeaders;
    messyHeaders.insert(QStringLiteral(" X-Amz-Date "), QStringLiteral("  20130524T000000Z  "));
    messyHeaders.insert(QStringLiteral("HOST"), QStringLiteral("examplebucket.s3.amazonaws.com"));
    messyHeaders.insert(QStringLiteral("x-amz-content-sha256"), s3PayloadSha256Hex(QByteArrayLiteral("payload")));
    QString messySignedHeaders;
    const QString messyCanonical = s3CanonicalRequest(QStringLiteral("put"),
                                                      QUrl(QStringLiteral("https://examplebucket.s3.amazonaws.com/a%20b.txt?partNumber=1&uploadId=xyz")),
                                                      messyHeaders,
                                                      s3PayloadSha256Hex(QByteArrayLiteral("payload")),
                                                      &messySignedHeaders);
    ok = expect(messySignedHeaders == QStringLiteral("host;x-amz-content-sha256;x-amz-date")
                    && messyCanonical.startsWith(QStringLiteral("PUT\n/a%20b.txt\npartNumber=1&uploadId=xyz\n")),
                "s3 canonical request should normalize method, path, query and signed header names") && ok;
    const QByteArray s3PutPayload = QByteArrayLiteral("signed request payload");
    const S3SignedObjectRequest signedPutRequest =
        s3SignedObjectRequest(s3Config,
                              QStringLiteral("abcdef1234567890.bin"),
                              QStringLiteral("put"),
                              s3PutPayload,
                              QStringLiteral("20130524T010203Z"));
    ok = expect(signedPutRequest.method == QByteArrayLiteral("PUT")
                    && signedPutRequest.request.url() == s3Url,
                "s3 signed request should keep the HTTP method and path-style URL") && ok;
    ok = expect(signedPutRequest.payloadSha256Hex == s3PayloadSha256Hex(s3PutPayload)
                    && signedPutRequest.request.rawHeader("x-amz-content-sha256")
                        == signedPutRequest.payloadSha256Hex.toLatin1(),
                "s3 signed request should include the payload SHA-256 header") && ok;
    ok = expect(signedPutRequest.request.rawHeader("host") == QByteArrayLiteral("minio.internal:9000")
                    && signedPutRequest.request.rawHeader("x-amz-date") == QByteArrayLiteral("20130524T010203Z")
                    && signedPutRequest.signedHeaders == QStringLiteral("host;x-amz-content-sha256;x-amz-date"),
                "s3 signed request should include host, date and signed header names") && ok;
    const QByteArray authHeader = signedPutRequest.request.rawHeader("Authorization");
    ok = expect(authHeader == signedPutRequest.authorizationHeader.toLatin1()
                    && authHeader.contains("Credential=access-key/20130524/local/s3/aws4_request")
                    && authHeader.contains("SignedHeaders=host;x-amz-content-sha256;x-amz-date")
                    && authHeader.contains("Signature=")
                    && !authHeader.contains("super-secret-value"),
                "s3 signed request should include Authorization without leaking the secret key") && ok;
    S3ObjectStoreConfig noRegionS3Config = s3Config;
    noRegionS3Config.region.clear();
    const S3SignedObjectRequest defaultRegionRequest =
        s3SignedObjectRequest(noRegionS3Config,
                              QStringLiteral("abcdef1234567890.bin"),
                              QStringLiteral("head"),
                              QByteArray(),
                              QStringLiteral("20130524T010203Z"));
    ok = expect(defaultRegionRequest.method == QByteArrayLiteral("HEAD")
                    && defaultRegionRequest.authorizationHeader.contains(QStringLiteral("/20130524/us-east-1/s3/aws4_request")),
                "s3 signed request should default an empty region to us-east-1") && ok;
    ok = expect(s3SignedObjectRequest(s3Config,
                                      QStringLiteral("../escape.bin"),
                                      QStringLiteral("GET"),
                                      QByteArray(),
                                      QStringLiteral("20130524T010203Z")).request.url().isEmpty(),
                "s3 signed request should reject invalid object keys") && ok;
    qputenv("QTNETWORKCHAT_OBJECT_S3_ENDPOINT", "https://minio.internal:9000");
    qputenv("QTNETWORKCHAT_OBJECT_S3_BUCKET", "qtchat-large-files");
    qputenv("QTNETWORKCHAT_OBJECT_S3_REGION", "local");
    qputenv("QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY", "access-key");
    qputenv("QTNETWORKCHAT_OBJECT_S3_SECRET_KEY", "super-secret-value");
    qputenv("QTNETWORKCHAT_OBJECT_S3_PREFIX", "/qtchat/large-files/");
    qputenv("QTNETWORKCHAT_OBJECT_S3_TLS_VERIFY", "0");
    const S3ObjectStoreConfig envS3Config = s3ObjectStoreConfigFromEnvironment();
    ok = expect(envS3Config.prefix == QStringLiteral("qtchat/large-files/")
                    && !envS3Config.tlsVerify,
                "s3 config should parse environment prefix and TLS flag") && ok;
    S3ObjectStore s3Placeholder(envS3Config);
    ok = expect(s3Placeholder.config().prefix == QStringLiteral("qtchat/large-files/"),
                "s3 placeholder should keep normalized prefix") && ok;
    const S3SignedObjectRequest tlsDisabledRequest =
        s3SignedObjectRequest(envS3Config,
                              QStringLiteral("abcdef1234567890.bin"),
                              QStringLiteral("GET"),
                              QByteArray(),
                              QStringLiteral("20130524T010203Z"));
#if QT_CONFIG(ssl)
    ok = expect(tlsDisabledRequest.request.sslConfiguration().peerVerifyMode() == QSslSocket::VerifyNone,
                "s3 signed request should disable TLS peer verification only when configured") && ok;
#else
    ok = expect(tlsDisabledRequest.request.url().isValid(),
                "s3 signed request should still be constructible without Qt SSL support") && ok;
#endif
    QString s3WriteKey;
    QString s3WriteHash;
    QString s3WriteError;
    ok = expect(!s3Placeholder.writeObject(QByteArrayLiteral("payload"),
                                           &s3WriteKey,
                                           &s3WriteHash,
                                           &s3WriteError,
                                           QStringLiteral("bin"))
                    && s3WriteKey.isEmpty()
                    && s3WriteHash.isEmpty()
                    && s3WriteError.contains(QString::fromUtf8("暂未实现"))
                    && !s3WriteError.contains(envS3Config.secretKey)
                    && !s3WriteError.contains(envS3Config.accessKey),
                "s3 placeholder writes should fail without leaking credentials") && ok;
    const ObjectStore::ValidationResult s3Validation =
        s3Placeholder.validateObject(QStringLiteral("object-key"), 7, sha256Hex(QByteArrayLiteral("payload")));
    ok = expect(!s3Validation.ok
                    && s3Validation.error.contains(QString::fromUtf8("暂未实现"))
                    && !s3Placeholder.openObject(QStringLiteral("object-key"))
                    && !s3Placeholder.removeObject(QStringLiteral("object-key")),
                "s3 placeholder read/validate/remove should fail closed") && ok;
    QStringList s3RemovedKeys;
    ok = expect(s3Placeholder.cleanupExpired(0, &s3RemovedKeys) == 0 && s3RemovedKeys.isEmpty(),
                "s3 placeholder cleanup should be a no-op") && ok;
    unsupportedStore = createObjectStore(QStringLiteral("s3"), QString(), &factoryError);
    ok = expect(!unsupportedStore
                    && factoryError.contains(QString::fromUtf8("暂未实现"))
                    && !factoryError.contains(QStringLiteral("super-secret-value"))
                    && !factoryError.contains(QStringLiteral("access-key")),
                "configured s3 backend should remain unimplemented without leaking credentials") && ok;
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_ENDPOINT");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_BUCKET");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_REGION");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_SECRET_KEY");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_PREFIX");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_TLS_VERIFY");

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
