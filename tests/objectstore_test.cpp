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
    ok = expect(isSupportedObjectStoreType(QStringLiteral("s3")),
                "s3 object store type should be recognized as supported") && ok;
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
    s3Config.sessionToken = QStringLiteral("temporary-session-token");
    s3Config.prefix = QStringLiteral("/qtchat/large-files/");
    s3Config.requestTimeoutMs = 45000;
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
                    && signedPutRequest.request.rawHeader("x-amz-security-token") == QByteArrayLiteral("temporary-session-token")
                    && signedPutRequest.signedHeaders == QStringLiteral("host;x-amz-content-sha256;x-amz-date;x-amz-security-token"),
                "s3 signed request should include host, date, session token and signed header names") && ok;
    ok = expect(signedPutRequest.request.transferTimeout() == 45000,
                "s3 signed request should apply the configured transfer timeout") && ok;
    const QByteArray authHeader = signedPutRequest.request.rawHeader("Authorization");
    ok = expect(authHeader == signedPutRequest.authorizationHeader.toLatin1()
                    && authHeader.contains("Credential=access-key/20130524/local/s3/aws4_request")
                    && authHeader.contains("SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-security-token")
                    && authHeader.contains("Signature=")
                    && !authHeader.contains("super-secret-value"),
                "s3 signed request should include Authorization without leaking the secret key") && ok;
    ok = expect(isSupportedS3ObjectMethod(QStringLiteral("PUT"))
                    && isSupportedS3ObjectMethod(QStringLiteral("get"))
                    && isSupportedS3ObjectMethod(QStringLiteral(" HEAD "))
                    && isSupportedS3ObjectMethod(QStringLiteral("delete"))
                    && !isSupportedS3ObjectMethod(QStringLiteral("POST")),
                "s3 object request helper should only allow first-stage object methods") && ok;
    const auto expectS3Status = [&ok](int statusCode,
                                      S3HttpResultKind expectedKind,
                                      bool expectedOk,
                                      bool expectedRetryable,
                                      const char* message) {
        const S3HttpResult result = classifyS3HttpStatus(statusCode);
        ok = expect(result.kind == expectedKind
                        && result.ok == expectedOk
                        && result.retryable == expectedRetryable
                        && !result.reason.isEmpty(),
                    message) && ok;
    };
    expectS3Status(200, S3HttpResultKind::Success, true, false,
                   "s3 status classifier should accept 200 as success");
    expectS3Status(201, S3HttpResultKind::Success, true, false,
                   "s3 status classifier should accept 201 as success");
    expectS3Status(204, S3HttpResultKind::Success, true, false,
                   "s3 status classifier should accept 204 as success");
    expectS3Status(206, S3HttpResultKind::Success, true, false,
                   "s3 status classifier should accept 206 as partial content success");
    expectS3Status(404, S3HttpResultKind::NotFound, false, false,
                   "s3 status classifier should classify 404 as not found");
    expectS3Status(401, S3HttpResultKind::AuthError, false, false,
                   "s3 status classifier should classify 401 as auth error");
    expectS3Status(403, S3HttpResultKind::AuthError, false, false,
                   "s3 status classifier should classify 403 as permission error");
    expectS3Status(408, S3HttpResultKind::Retryable, false, true,
                   "s3 status classifier should classify 408 as retryable");
    expectS3Status(409, S3HttpResultKind::Retryable, false, true,
                   "s3 status classifier should classify 409 as retryable");
    expectS3Status(429, S3HttpResultKind::Retryable, false, true,
                   "s3 status classifier should classify 429 as retryable");
    expectS3Status(400, S3HttpResultKind::ClientError, false, false,
                   "s3 status classifier should classify 400 as client error");
    expectS3Status(405, S3HttpResultKind::ClientError, false, false,
                   "s3 status classifier should classify 405 as client error");
    expectS3Status(500, S3HttpResultKind::ServerError, false, true,
                   "s3 status classifier should classify 500 as retryable server error");
    expectS3Status(503, S3HttpResultKind::ServerError, false, true,
                   "s3 status classifier should classify 503 as retryable server error");
    expectS3Status(0, S3HttpResultKind::Unknown, false, false,
                   "s3 status classifier should classify missing status as unknown");
    expectS3Status(302, S3HttpResultKind::Unknown, false, false,
                   "s3 status classifier should classify unexpected redirects as unknown");
    const auto expectS3RequestResult = [&ok](const S3RequestResult& result,
                                             S3HttpResultKind expectedKind,
                                             bool expectedOk,
                                             bool expectedRetryable,
                                             const QString& expectedReason,
                                             const char* message) {
        ok = expect(result.http.kind == expectedKind
                        && result.http.ok == expectedOk
                        && result.http.retryable == expectedRetryable
                        && result.http.reason == expectedReason,
                    message) && ok;
    };
    expectS3RequestResult(s3RequestResultFromReply(s3Config, 200),
                          S3HttpResultKind::Success,
                          true,
                          false,
                          QStringLiteral("success"),
                          "s3 request result should preserve successful HTTP classification");
    expectS3RequestResult(s3RequestResultFromReply(s3Config, 404),
                          S3HttpResultKind::NotFound,
                          false,
                          false,
                          QStringLiteral("not_found"),
                          "s3 request result should preserve not-found classification");
    expectS3RequestResult(s3RequestResultFromReply(s3Config, 403),
                          S3HttpResultKind::AuthError,
                          false,
                          false,
                          QStringLiteral("auth_or_permission_error"),
                          "s3 request result should preserve auth classification");
    expectS3RequestResult(s3RequestResultFromReply(s3Config, 503),
                          S3HttpResultKind::ServerError,
                          false,
                          true,
                          QStringLiteral("server_error"),
                          "s3 request result should preserve retryable server classification");
    ok = expect(s3FailureReasonForLog(s3RequestResultFromReply(s3Config, 200)) == QStringLiteral("success")
                    && s3FailureReasonForLog(s3RequestResultFromReply(s3Config, 404)) == QStringLiteral("not_found")
                    && s3FailureReasonForLog(s3RequestResultFromReply(s3Config, 403)) == QStringLiteral("auth")
                    && s3FailureReasonForLog(s3RequestResultFromReply(s3Config, 408)) == QStringLiteral("retryable")
                    && s3FailureReasonForLog(s3RequestResultFromReply(s3Config, 400)) == QStringLiteral("client")
                    && s3FailureReasonForLog(s3RequestResultFromReply(s3Config, 503)) == QStringLiteral("server")
                    && s3FailureReasonForLog(s3RequestResultFromReply(s3Config, 302)) == QStringLiteral("unknown"),
                "s3 failure reason helper should expose stable aggregate-safe HTTP reasons") && ok;
    const S3RequestResult networkFailure =
        s3RequestResultFromReply(s3Config,
                                 0,
                                 QStringLiteral("socket failed with access-key and super-secret-value"));
    expectS3RequestResult(networkFailure,
                          S3HttpResultKind::Retryable,
                          false,
                          true,
                          QStringLiteral("network_error"),
                          "s3 request result should classify network errors as retryable");
    ok = expect(networkFailure.networkError
                    && !networkFailure.timeout
                    && !networkFailure.tlsError
                    && !networkFailure.error.contains(s3Config.accessKey)
                    && !networkFailure.error.contains(s3Config.secretKey),
                "s3 network error text should be redacted") && ok;
    ok = expect(s3FailureReasonForLog(networkFailure) == QStringLiteral("network"),
                "s3 failure reason helper should classify network failures without leaking error text") && ok;
    const S3RequestResult timeoutFailure =
        s3RequestResultFromReply(s3Config, 0, QString(), true);
    expectS3RequestResult(timeoutFailure,
                          S3HttpResultKind::Retryable,
                          false,
                          true,
                          QStringLiteral("timeout"),
                          "s3 request result should classify timeouts as retryable");
    ok = expect(timeoutFailure.timeout
                    && timeoutFailure.error.contains(QStringLiteral("timed out")),
                "s3 timeout result should carry a safe default error") && ok;
    ok = expect(s3FailureReasonForLog(timeoutFailure) == QStringLiteral("timeout"),
                "s3 failure reason helper should classify timeouts") && ok;
    const S3RequestResult tlsFailure =
        s3RequestResultFromReply(s3Config,
                                 0,
                                 QStringLiteral("TLS failed for temporary-session-token"),
                                 false,
                                 true);
    expectS3RequestResult(tlsFailure,
                          S3HttpResultKind::AuthError,
                          false,
                          false,
                          QStringLiteral("tls_error"),
                          "s3 request result should classify TLS failures as non-retryable auth boundary errors");
    ok = expect(tlsFailure.networkError
                    && tlsFailure.tlsError
                    && !tlsFailure.error.contains(s3Config.sessionToken),
                "s3 TLS error text should be redacted") && ok;
    ok = expect(s3FailureReasonForLog(tlsFailure) == QStringLiteral("tls"),
                "s3 failure reason helper should classify TLS failures") && ok;
    const QString redactedS3Error =
        redactS3ErrorText(s3Config,
                          QStringLiteral("Authorization=AWS4-HMAC-SHA256 Credential=access-key/20130524/local/s3/aws4_request,SignedHeaders=host,Signature=abcdef "
                                         "X-Amz-Credential=access-key%2F20130524%2Flocal%2Fs3%2Faws4_request&X-Amz-Signature=012345 "
                                         "secret=super-secret-value token=temporary-session-token"));
    ok = expect(redactedS3Error.contains(QStringLiteral("<redacted>"))
                    && !redactedS3Error.contains(QStringLiteral("access-key"))
                    && !redactedS3Error.contains(QStringLiteral("super-secret-value"))
                    && !redactedS3Error.contains(QStringLiteral("temporary-session-token"))
                    && !redactedS3Error.contains(QStringLiteral("Signature=abcdef"))
                    && !redactedS3Error.contains(QStringLiteral("X-Amz-Signature=012345")),
                "s3 error redaction should hide credentials and signature material") && ok;
    const S3RequestExecutionResult invalidNetworkExecution =
        executeS3ObjectRequest(s3Config, S3SignedObjectRequest());
    ok = expect(invalidNetworkExecution.result.http.kind == S3HttpResultKind::Unknown
                    && invalidNetworkExecution.result.http.reason == QStringLiteral("invalid_request")
                    && !invalidNetworkExecution.result.http.ok
                    && invalidNetworkExecution.headers.isEmpty()
                    && invalidNetworkExecution.body.isEmpty(),
                "s3 Qt Network executor should fail closed before network I/O for invalid requests") && ok;
    const QStringList s3SupportedMethods = {
        QStringLiteral("PUT"),
        QStringLiteral("GET"),
        QStringLiteral("HEAD"),
        QStringLiteral("DELETE")
    };
    for (const QString& method : s3SupportedMethods) {
        const S3SignedObjectRequest signedRequest =
            s3SignedObjectRequest(s3Config,
                                  QStringLiteral("abcdef1234567890.bin"),
                                  method,
                                  method == QStringLiteral("PUT") ? s3PutPayload : QByteArray(),
                                  QStringLiteral("20130524T010203Z"));
        ok = expect(signedRequest.method == method.toLatin1()
                        && signedRequest.request.url() == s3Url
                        && signedRequest.request.rawHeader("Authorization").contains("Signature=")
                        && signedRequest.request.rawHeader("Authorization")
                            == signedRequest.authorizationHeader.toLatin1(),
                    "s3 signed request should support all first-stage object methods") && ok;
    }
    const S3SignedObjectRequest unsupportedPostRequest =
        s3SignedObjectRequest(s3Config,
                              QStringLiteral("abcdef1234567890.bin"),
                              QStringLiteral("POST"),
                              QByteArray(),
                              QStringLiteral("20130524T010203Z"));
    ok = expect(unsupportedPostRequest.method.isEmpty()
                    && unsupportedPostRequest.request.url().isEmpty()
                    && unsupportedPostRequest.authorizationHeader.isEmpty(),
                "s3 signed request should reject methods outside the first-stage object scope") && ok;
    S3ObjectStoreConfig noRegionS3Config = s3Config;
    noRegionS3Config.region.clear();
    const S3SignedObjectRequest defaultRegionRequest =
        s3SignedObjectRequest(noRegionS3Config,
                              QStringLiteral("abcdef1234567890.bin"),
                              QStringLiteral("head"),
                              QByteArray(),
                              QStringLiteral("20130524T010203Z"));
    ok = expect(defaultRegionRequest.method == QByteArrayLiteral("HEAD")
                    && defaultRegionRequest.authorizationHeader.contains(QStringLiteral("/20130524/us-east-1/s3/aws4_request"))
                    && defaultRegionRequest.request.transferTimeout() == 45000,
                "s3 signed request should default an empty region to us-east-1 and keep the configured timeout") && ok;
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
    qputenv("QTNETWORKCHAT_OBJECT_S3_SESSION_TOKEN", "temporary-session-token");
    qputenv("QTNETWORKCHAT_OBJECT_S3_PREFIX", "/qtchat/large-files/");
    qputenv("QTNETWORKCHAT_OBJECT_S3_TLS_VERIFY", "0");
    qputenv("QTNETWORKCHAT_OBJECT_S3_TIMEOUT_MS", "45000");
    const S3ObjectStoreConfig envS3Config = s3ObjectStoreConfigFromEnvironment();
    ok = expect(envS3Config.prefix == QStringLiteral("qtchat/large-files/")
                    && envS3Config.sessionToken == QStringLiteral("temporary-session-token")
                    && !envS3Config.tlsVerify
                    && envS3Config.requestTimeoutMs == 45000,
                "s3 config should parse environment prefix, session token, TLS flag and request timeout") && ok;
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_ENABLE");
    ok = expect(!s3ObjectStoreEnabledFromEnvironment(),
                "s3 backend factory gate should be disabled by default") && ok;
    qputenv("QTNETWORKCHAT_OBJECT_S3_ENABLE", "yes");
    ok = expect(s3ObjectStoreEnabledFromEnvironment(),
                "s3 backend factory gate should accept explicit opt-in values") && ok;
    qputenv("QTNETWORKCHAT_OBJECT_S3_ENABLE", "0");
    ok = expect(!s3ObjectStoreEnabledFromEnvironment(),
                "s3 backend factory gate should reject explicit disabled values") && ok;
    qputenv("QTNETWORKCHAT_OBJECT_S3_TIMEOUT_MS", "999999");
    const S3ObjectStoreConfig invalidTimeoutS3Config = s3ObjectStoreConfigFromEnvironment();
    ok = expect(invalidTimeoutS3Config.requestTimeoutMs == 30000,
                "s3 config should fall back to the default timeout when the env value is out of range") && ok;
    const S3SignedObjectRequest defaultTimeoutRequest =
        s3SignedObjectRequest(invalidTimeoutS3Config,
                              QStringLiteral("abcdef1234567890.bin"),
                              QStringLiteral("GET"),
                              QByteArray(),
                              QStringLiteral("20130524T010203Z"));
    ok = expect(defaultTimeoutRequest.request.transferTimeout() == 30000,
                "s3 signed request should apply the default timeout after invalid env input") && ok;
    S3ObjectStore s3Placeholder(envS3Config);
    ok = expect(s3Placeholder.config().prefix == QStringLiteral("qtchat/large-files/"),
                "s3 placeholder should keep normalized prefix") && ok;
    ok = expect(s3Placeholder.config().requestTimeoutMs == 45000,
                "s3 placeholder should keep the parsed request timeout") && ok;
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
    const QString expectedS3Hash = sha256Hex(QByteArrayLiteral("payload"));
    QStringList s3ExecutorMethods;
    QList<QByteArray> s3ExecutorBodies;
    S3ObjectStore s3HeadDeleteStore(envS3Config,
                                    [&s3ExecutorMethods, &s3ExecutorBodies, &envS3Config, expectedS3Hash](
                                        const S3SignedObjectRequest& request,
                                        const QByteArray& body) {
        s3ExecutorMethods.append(QString::fromLatin1(request.method));
        s3ExecutorBodies.append(body);
        S3RequestExecutionResult result;
        if (request.method == QByteArrayLiteral("PUT")) {
            result.result = s3RequestResultFromReply(envS3Config, 200);
        } else if (request.method == QByteArrayLiteral("GET")) {
            result.result = s3RequestResultFromReply(envS3Config, 200);
            result.body = QByteArrayLiteral("payload");
        } else if (request.method == QByteArrayLiteral("HEAD")) {
            result.result = s3RequestResultFromReply(envS3Config, 200);
            result.headers.insert(QStringLiteral("Content-Length"), QStringLiteral("7"));
            result.headers.insert(QStringLiteral("X-Amz-Meta-Sha256"), expectedS3Hash.toUpper());
        } else if (request.method == QByteArrayLiteral("DELETE")) {
            result.result = s3RequestResultFromReply(envS3Config, 204);
        } else {
            result.result = s3RequestResultFromReply(envS3Config,
                                                     400,
                                                     QStringLiteral("unexpected method with super-secret-value"));
        }
        return result;
    });
    QString s3UploadedKey;
    QString s3UploadedHash;
    QString s3UploadError;
    ok = expect(s3HeadDeleteStore.writeObject(QByteArrayLiteral("payload"),
                                              &s3UploadedKey,
                                              &s3UploadedHash,
                                              &s3UploadError,
                                              QStringLiteral("bin"))
                    && FilesystemObjectStore::isValidObjectKey(s3UploadedKey)
                    && s3UploadedKey.endsWith(QStringLiteral(".bin"))
                    && s3UploadedHash == expectedS3Hash
                    && s3UploadError.isEmpty(),
                "s3 injected PUT path should upload payload and return generated key plus SHA-256") && ok;
    const std::unique_ptr<QIODevice> s3OpenedObject = s3HeadDeleteStore.openObject(s3UploadedKey);
    ok = expect(s3OpenedObject && s3OpenedObject->readAll() == QByteArrayLiteral("payload"),
                "s3 injected GET path should return a readable payload device") && ok;
    ok = expect(s3HeadDeleteStore.lastOpenFailureReason().isEmpty(),
                "s3 successful GET path should clear the last open failure reason") && ok;
    const ObjectStore::ValidationResult s3HeadValidation =
        s3HeadDeleteStore.validateObject(QStringLiteral("abcdef1234567890.bin"),
                                         7,
                                         expectedS3Hash);
    ok = expect(s3HeadValidation.ok
                    && s3HeadValidation.size == 7
                    && s3HeadValidation.fileHash == expectedS3Hash,
                "s3 injected HEAD+GET path should validate size and SHA-256 body") && ok;
    ok = expect(s3HeadDeleteStore.removeObject(QStringLiteral("abcdef1234567890.bin")),
                "s3 injected DELETE path should report successful deletion") && ok;
    ok = expect(s3HeadDeleteStore.lastRemoveFailureReason().isEmpty(),
                "s3 successful DELETE path should clear the last remove failure reason") && ok;
    ok = expect(s3ExecutorMethods == QStringList({QStringLiteral("PUT"),
                                                  QStringLiteral("GET"),
                                                  QStringLiteral("HEAD"),
                                                  QStringLiteral("GET"),
                                                  QStringLiteral("DELETE")})
                    && s3ExecutorBodies.size() == 5
                    && s3ExecutorBodies.at(0) == QByteArrayLiteral("payload")
                    && s3ExecutorBodies.at(1).isEmpty()
                    && s3ExecutorBodies.at(2).isEmpty()
                    && s3ExecutorBodies.at(3).isEmpty()
                    && s3ExecutorBodies.at(4).isEmpty(),
                "s3 injected executor should receive PUT payload and empty GET/HEAD/GET/DELETE bodies") && ok;
    S3ObjectStore s3MissingHashStore(envS3Config,
                                     [&envS3Config](const S3SignedObjectRequest& request, const QByteArray& body) {
        Q_UNUSED(body);
        S3RequestExecutionResult result;
        if (request.method == QByteArrayLiteral("HEAD")) {
            result.result = s3RequestResultFromReply(envS3Config, 200);
            result.headers.insert(QStringLiteral("content-length"), QStringLiteral("7"));
        } else if (request.method == QByteArrayLiteral("GET")) {
            result.result = s3RequestResultFromReply(envS3Config, 200);
            result.body = QByteArrayLiteral("payload");
        } else {
            result.result = s3RequestResultFromReply(envS3Config, 204);
        }
        return result;
    });
    const ObjectStore::ValidationResult s3MissingHashValidation =
        s3MissingHashStore.validateObject(QStringLiteral("abcdef1234567890.bin"), 7, expectedS3Hash);
    ok = expect(s3MissingHashValidation.ok
                    && s3MissingHashValidation.size == 7
                    && s3MissingHashValidation.fileHash == expectedS3Hash,
                "s3 validation should use GET body hash when HEAD hash metadata is missing") && ok;
    const ObjectStore::ValidationResult validS3Body =
        validateS3ObjectBody(QByteArrayLiteral("payload"), 7, expectedS3Hash.toUpper());
    ok = expect(validS3Body.ok && validS3Body.fileHash == expectedS3Hash,
                "s3 body validation should accept matching size and case-insensitive SHA-256") && ok;
    const ObjectStore::ValidationResult emptyHashS3Body =
        validateS3ObjectBody(QByteArrayLiteral("payload"), 7, QString());
    ok = expect(emptyHashS3Body.ok && emptyHashS3Body.fileHash == expectedS3Hash,
                "s3 body validation should compute SHA-256 when expected hash is empty") && ok;
    ok = expect(!validateS3ObjectBody(QByteArrayLiteral("payload"), 8, expectedS3Hash).ok,
                "s3 body validation should reject size mismatches") && ok;
    ok = expect(!validateS3ObjectBody(QByteArrayLiteral("payload"), 7, sha256Hex(QByteArrayLiteral("other"))).ok,
                "s3 body validation should reject SHA-256 mismatches") && ok;
    ok = expect(s3ValidationFailureReasonForLog(validateS3ObjectBody(QByteArrayLiteral("payload"), 7, expectedS3Hash))
                    == QStringLiteral("success")
                    && s3ValidationFailureReasonForLog(validateS3ObjectBody(QByteArrayLiteral("payload"), 8, expectedS3Hash))
                        == QStringLiteral("size")
                    && s3ValidationFailureReasonForLog(validateS3ObjectBody(QByteArrayLiteral("payload"), 7, sha256Hex(QByteArrayLiteral("other"))))
                        == QStringLiteral("hash"),
                "s3 validation failure reason helper should expose stable size/hash reasons") && ok;
    ObjectStore::ValidationResult s3HeadNetworkValidation;
    s3HeadNetworkValidation.error = QStringLiteral("S3 HEAD 请求失败: network_error socket closed");
    ObjectStore::ValidationResult s3HeadTimeoutValidation;
    s3HeadTimeoutValidation.error = QStringLiteral("S3 HEAD 请求失败: timeout S3 request timed out");
    ObjectStore::ValidationResult s3GetTlsValidation;
    s3GetTlsValidation.error = QStringLiteral("S3 GET 请求失败: tls_error temporary-session-token");
    ObjectStore::ValidationResult s3GetAuthValidation;
    s3GetAuthValidation.error = QStringLiteral("S3 GET 请求失败: auth_or_permission_error");
    ObjectStore::ValidationResult s3GetNotFoundValidation;
    s3GetNotFoundValidation.error = QStringLiteral("S3 GET 请求失败: not_found");
    ok = expect(s3ValidationFailureReasonForLog(s3HeadNetworkValidation) == QStringLiteral("network")
                    && s3ValidationFailureReasonForLog(s3HeadTimeoutValidation) == QStringLiteral("timeout")
                    && s3ValidationFailureReasonForLog(s3GetTlsValidation) == QStringLiteral("tls")
                    && s3ValidationFailureReasonForLog(s3GetAuthValidation) == QStringLiteral("auth")
                    && s3ValidationFailureReasonForLog(s3GetNotFoundValidation) == QStringLiteral("not_found"),
                "s3 validation failure reason helper should classify HEAD/GET request failures") && ok;
    ok = expect(objectStoreOpenFailureReasonForLog(QStringLiteral("s3"),
                                                   QStringLiteral("S3 GET 请求失败: timeout access-key super-secret-value"))
                    == QStringLiteral("timeout")
                    && objectStoreOpenFailureReasonForLog(QStringLiteral("s3"),
                                                          QStringLiteral("S3 GET 请求失败: network_error socket closed"))
                        == QStringLiteral("network")
                    && objectStoreOpenFailureReasonForLog(QStringLiteral("s3"),
                                                          QStringLiteral("S3 GET 请求失败: tls_error temporary-session-token"))
                        == QStringLiteral("tls")
                    && objectStoreOpenFailureReasonForLog(QStringLiteral("s3"),
                                                          QStringLiteral("S3 GET 请求失败: auth_or_permission_error"))
                        == QStringLiteral("auth")
                    && objectStoreOpenFailureReasonForLog(QStringLiteral("s3"),
                                                          QStringLiteral("S3 GET 请求失败: retryable_client_status"))
                        == QStringLiteral("retryable")
                    && objectStoreOpenFailureReasonForLog(QStringLiteral("s3"),
                                                          QStringLiteral("S3 GET 请求失败: server_error"))
                        == QStringLiteral("server")
                    && objectStoreOpenFailureReasonForLog(QStringLiteral("s3"),
                                                          QStringLiteral("S3 GET 请求失败: unknown_status"))
                        == QStringLiteral("unknown")
                    && objectStoreOpenFailureReasonForLog(QStringLiteral("filesystem"),
                                                          QStringLiteral("disk path detail"))
                        == QStringLiteral("object-open-failed"),
                "object store open reason helper should classify S3 failures without exposing error text") && ok;
    ok = expect(objectStoreWriteFailureReasonForLog(QStringLiteral("s3"),
                                                    false,
                                                    QStringLiteral("S3 endpoint secret should not leak"),
                                                    expectedS3Hash,
                                                    QString()) == QStringLiteral("object-store-unavailable"),
                "object store write reason helper should classify unavailable stores") && ok;
    ok = expect(objectStoreWriteFailureReasonForLog(QStringLiteral("s3"),
                                                    true,
                                                    QStringLiteral("S3 PUT 请求失败: timeout access-key super-secret-value"),
                                                    expectedS3Hash,
                                                    QString()) == QStringLiteral("timeout"),
                "object store write reason helper should classify S3 timeouts without exposing error text") && ok;
    ok = expect(objectStoreWriteFailureReasonForLog(QStringLiteral("s3"),
                                                    true,
                                                    QStringLiteral("S3 PUT 请求失败: network_error socket closed"),
                                                    expectedS3Hash,
                                                    QString()) == QStringLiteral("network"),
                "object store write reason helper should classify S3 network failures") && ok;
    ok = expect(objectStoreWriteFailureReasonForLog(QStringLiteral("s3"),
                                                    true,
                                                    QStringLiteral("S3 PUT 请求失败: tls_error temporary-session-token"),
                                                    expectedS3Hash,
                                                    QString()) == QStringLiteral("tls"),
                "object store write reason helper should classify S3 TLS failures") && ok;
    ok = expect(objectStoreWriteFailureReasonForLog(QStringLiteral("s3"),
                                                    true,
                                                    QStringLiteral("S3 PUT 请求失败: auth_or_permission_error"),
                                                    expectedS3Hash,
                                                    QString()) == QStringLiteral("auth"),
                "object store write reason helper should classify S3 auth failures") && ok;
    ok = expect(objectStoreWriteFailureReasonForLog(QStringLiteral("s3"),
                                                    true,
                                                    QStringLiteral("S3 PUT 请求失败: server_error"),
                                                    expectedS3Hash,
                                                    QString()) == QStringLiteral("server"),
                "object store write reason helper should classify S3 server failures") && ok;
    ok = expect(objectStoreWriteFailureReasonForLog(QStringLiteral("s3"),
                                                    true,
                                                    QString(),
                                                    expectedS3Hash,
                                                    sha256Hex(QByteArrayLiteral("other"))) == QStringLiteral("hash"),
                "object store write reason helper should classify write hash mismatches") && ok;
    ok = expect(objectStoreWriteFailureReasonForLog(QStringLiteral("filesystem"),
                                                    true,
                                                    QStringLiteral("disk full"),
                                                    expectedS3Hash,
                                                    QString()) == QStringLiteral("write_failed"),
                "object store write reason helper should keep non-S3 write failures aggregate-safe") && ok;
    LargeFileDeliveredReceipt deliveredReceipt;
    deliveredReceipt.sourceInstanceId = QStringLiteral("source-a");
    deliveredReceipt.transferId = QStringLiteral("transfer-a");
    deliveredReceipt.receiverId = QStringLiteral("receiver-a");
    deliveredReceipt.objectKey = QStringLiteral("abcdef1234567890.bin");
    deliveredReceipt.fileHash = expectedS3Hash.toUpper();
    deliveredReceipt.confirmedBytes = 7;
    LargeFileDeliveredFallback deliveredFallback;
    deliveredFallback.sourceInstanceId = QStringLiteral("source-a");
    deliveredFallback.transferId = QStringLiteral("transfer-a");
    deliveredFallback.receiverId = QStringLiteral("receiver-a");
    deliveredFallback.objectKey = QStringLiteral("abcdef1234567890.bin");
    deliveredFallback.fileHash = expectedS3Hash;
    deliveredFallback.fileSize = 7;
    const LargeFileDeliveredReceiptDecision deliveredDecision =
        evaluateLargeFileDeliveredReceiptCleanup(deliveredReceipt, deliveredFallback);
    ok = expect(deliveredDecision.shouldCleanup
                    && deliveredDecision.reason == QStringLiteral("cleaned"),
                "large file delivered receipt helper should allow cleanup only for a complete matching receipt") && ok;
    LargeFileDeliveredReceipt invalidReceipt = deliveredReceipt;
    invalidReceipt.objectKey = QStringLiteral("../escape.bin");
    ok = expect(!evaluateLargeFileDeliveredReceiptCleanup(invalidReceipt, deliveredFallback).shouldCleanup
                    && evaluateLargeFileDeliveredReceiptCleanup(invalidReceipt, deliveredFallback).reason
                        == QStringLiteral("invalid-receipt"),
                "large file delivered receipt helper should reject unsafe receipt object keys") && ok;
    LargeFileDeliveredFallback invalidFallback = deliveredFallback;
    invalidFallback.fileSize = 0;
    ok = expect(!evaluateLargeFileDeliveredReceiptCleanup(deliveredReceipt, invalidFallback).shouldCleanup
                    && evaluateLargeFileDeliveredReceiptCleanup(deliveredReceipt, invalidFallback).reason
                        == QStringLiteral("invalid-payload"),
                "large file delivered receipt helper should reject invalid fallback payload metadata") && ok;
    LargeFileDeliveredFallback mismatchedFallback = deliveredFallback;
    mismatchedFallback.receiverId = QStringLiteral("other-receiver");
    ok = expect(!evaluateLargeFileDeliveredReceiptCleanup(deliveredReceipt, mismatchedFallback).shouldCleanup
                    && evaluateLargeFileDeliveredReceiptCleanup(deliveredReceipt, mismatchedFallback).reason
                        == QStringLiteral("receipt-not-matched"),
                "large file delivered receipt helper should retain fallback for metadata mismatches") && ok;
    LargeFileDeliveredReceipt partialReceipt = deliveredReceipt;
    partialReceipt.confirmedBytes = 6;
    ok = expect(!evaluateLargeFileDeliveredReceiptCleanup(partialReceipt, deliveredFallback).shouldCleanup
                    && evaluateLargeFileDeliveredReceiptCleanup(partialReceipt, deliveredFallback).reason
                        == QStringLiteral("confirmed-bytes-insufficient"),
                "large file delivered receipt helper should retain fallback for incomplete receipts") && ok;
    S3ObjectStore s3GetMismatchStore(envS3Config,
                                     [&envS3Config, expectedS3Hash](const S3SignedObjectRequest& request, const QByteArray& body) {
        Q_UNUSED(body);
        S3RequestExecutionResult result;
        if (request.method == QByteArrayLiteral("HEAD")) {
            result.result = s3RequestResultFromReply(envS3Config, 200);
            result.headers.insert(QStringLiteral("Content-Length"), QStringLiteral("7"));
            result.headers.insert(QStringLiteral("ETag"), QStringLiteral("\"not-a-sha256\""));
            result.headers.insert(QStringLiteral("X-Amz-Meta-Sha256"), expectedS3Hash);
        } else if (request.method == QByteArrayLiteral("GET")) {
            result.result = s3RequestResultFromReply(envS3Config, 200);
            result.body = QByteArrayLiteral("tamper");
        } else {
            result.result = s3RequestResultFromReply(envS3Config, 204);
        }
        return result;
    });
    const ObjectStore::ValidationResult s3GetMismatchValidation =
        s3GetMismatchStore.validateObject(QStringLiteral("abcdef1234567890.bin"), 7, expectedS3Hash);
    ok = expect(!s3GetMismatchValidation.ok
                    && s3GetMismatchValidation.error.contains(QString::fromUtf8("大小不一致")),
                "s3 validation should reject mismatched GET body and not trust ETag or HEAD hash alone") && ok;
    S3ObjectStore s3FailedHeadStore(envS3Config,
                                    [&envS3Config](const S3SignedObjectRequest& request, const QByteArray& body) {
        Q_UNUSED(request);
        Q_UNUSED(body);
        S3RequestExecutionResult result;
        result.result = s3RequestResultFromReply(envS3Config,
                                                 0,
                                                 QStringLiteral("network failed with access-key super-secret-value temporary-session-token"));
        return result;
    });
    const ObjectStore::ValidationResult s3FailedHeadValidation =
        s3FailedHeadStore.validateObject(QStringLiteral("abcdef1234567890.bin"), 7, expectedS3Hash);
    ok = expect(!s3FailedHeadValidation.ok
                    && s3FailedHeadValidation.error.contains(QStringLiteral("network_error"))
                    && !s3FailedHeadValidation.error.contains(envS3Config.accessKey)
                    && !s3FailedHeadValidation.error.contains(envS3Config.secretKey)
                    && !s3FailedHeadValidation.error.contains(envS3Config.sessionToken),
                "s3 HEAD validation failure should keep errors redacted") && ok;
    QString s3FailedPutKey;
    QString s3FailedPutHash;
    QString s3FailedPutError;
    ok = expect(!s3FailedHeadStore.writeObject(QByteArrayLiteral("payload"),
                                               &s3FailedPutKey,
                                               &s3FailedPutHash,
                                               &s3FailedPutError,
                                               QStringLiteral("bin"))
                    && s3FailedPutKey.isEmpty()
                    && s3FailedPutHash.isEmpty()
                    && s3FailedPutError.contains(QStringLiteral("network_error"))
                    && !s3FailedPutError.contains(envS3Config.accessKey)
                    && !s3FailedPutError.contains(envS3Config.secretKey)
                    && !s3FailedPutError.contains(envS3Config.sessionToken),
                "s3 PUT failure should fail closed and keep errors redacted") && ok;
    ok = expect(!s3FailedHeadStore.openObject(QStringLiteral("abcdef1234567890.bin")),
                "s3 GET failure should fail closed without returning a device") && ok;
    ok = expect(s3FailedHeadStore.lastOpenFailureReason() == QStringLiteral("network"),
                "s3 GET network failure should expose a stable open failure reason") && ok;
    S3ObjectStore s3FailedGetStore(envS3Config,
                                   [&envS3Config](const S3SignedObjectRequest& request, const QByteArray& body) {
        Q_UNUSED(body);
        S3RequestExecutionResult result;
        if (request.method == QByteArrayLiteral("GET")) {
            result.result = s3RequestResultFromReply(envS3Config, 403);
        } else {
            result.result = s3RequestResultFromReply(envS3Config, 200);
        }
        return result;
    });
    ok = expect(!s3FailedGetStore.openObject(QStringLiteral("abcdef1234567890.bin"))
                    && s3FailedGetStore.lastOpenFailureReason() == QStringLiteral("auth"),
                "s3 GET auth failure should expose a stable open failure reason without leaking credentials") && ok;
    ok = expect(!s3FailedGetStore.lastOpenFailureReason().contains(envS3Config.accessKey)
                    && !s3FailedGetStore.lastOpenFailureReason().contains(envS3Config.secretKey)
                    && !s3FailedGetStore.lastOpenFailureReason().contains(envS3Config.sessionToken),
                "s3 GET open failure reason should not include credentials") && ok;
    S3ObjectStore s3TlsGetStore(envS3Config,
                                [&envS3Config](const S3SignedObjectRequest& request, const QByteArray& body) {
        Q_UNUSED(request);
        Q_UNUSED(body);
        S3RequestExecutionResult result;
        result.result = s3RequestResultFromReply(envS3Config,
                                                 0,
                                                 QStringLiteral("certificate rejected"),
                                                 false,
                                                 true);
        return result;
    });
    ok = expect(!s3TlsGetStore.openObject(QStringLiteral("abcdef1234567890.bin"))
                    && s3TlsGetStore.lastOpenFailureReason() == QStringLiteral("tls"),
                "s3 GET TLS failure should expose a stable open failure reason") && ok;
    S3ObjectStore s3MissingGetStore(envS3Config,
                                    [&envS3Config](const S3SignedObjectRequest& request, const QByteArray& body) {
        Q_UNUSED(request);
        Q_UNUSED(body);
        S3RequestExecutionResult result;
        result.result = s3RequestResultFromReply(envS3Config, 404);
        return result;
    });
    ok = expect(!s3MissingGetStore.openObject(QStringLiteral("abcdef1234567890.bin"))
                    && s3MissingGetStore.lastOpenFailureReason() == QStringLiteral("not_found"),
                "s3 GET missing object should expose not_found as the open failure reason") && ok;
    ok = expect(!s3MissingGetStore.openObject(QStringLiteral("../escape.bin"))
                    && s3MissingGetStore.lastOpenFailureReason() == QStringLiteral("unknown"),
                "s3 invalid object key open should fail closed with a fixed reason bucket") && ok;
    S3ObjectStore s3FailedDeleteStore(envS3Config,
                                      [&envS3Config](const S3SignedObjectRequest& request, const QByteArray& body) {
        Q_UNUSED(request);
        Q_UNUSED(body);
        S3RequestExecutionResult result;
        result.result = s3RequestResultFromReply(envS3Config, 404);
        return result;
    });
    ok = expect(!s3FailedDeleteStore.removeObject(QStringLiteral("abcdef1234567890.bin"))
                    && s3FailedDeleteStore.lastRemoveFailureReason() == QStringLiteral("not_found")
                    && !s3FailedDeleteStore.removeObject(QStringLiteral("../escape.bin")),
                "s3 DELETE should fail closed for not-found results and invalid object keys") && ok;
    ok = expect(s3FailedDeleteStore.lastRemoveFailureReason() == QStringLiteral("unknown"),
                "s3 invalid object key DELETE should expose a fixed remove failure reason") && ok;
    S3ObjectStore s3AuthDeleteStore(envS3Config,
                                    [&envS3Config](const S3SignedObjectRequest& request, const QByteArray& body) {
        Q_UNUSED(request);
        Q_UNUSED(body);
        S3RequestExecutionResult result;
        result.result = s3RequestResultFromReply(envS3Config, 403);
        return result;
    });
    ok = expect(!s3AuthDeleteStore.removeObject(QStringLiteral("abcdef1234567890.bin"))
                    && s3AuthDeleteStore.lastRemoveFailureReason() == QStringLiteral("auth"),
                "s3 DELETE auth failure should expose a stable remove failure reason") && ok;
    S3ObjectStore s3NetworkDeleteStore(envS3Config,
                                       [&envS3Config](const S3SignedObjectRequest& request, const QByteArray& body) {
        Q_UNUSED(request);
        Q_UNUSED(body);
        S3RequestExecutionResult result;
        result.result = s3RequestResultFromReply(envS3Config,
                                                 0,
                                                 QStringLiteral("network failed with access-key super-secret-value temporary-session-token"));
        return result;
    });
    ok = expect(!s3NetworkDeleteStore.removeObject(QStringLiteral("abcdef1234567890.bin"))
                    && s3NetworkDeleteStore.lastRemoveFailureReason() == QStringLiteral("network")
                    && !s3NetworkDeleteStore.lastRemoveFailureReason().contains(envS3Config.accessKey)
                    && !s3NetworkDeleteStore.lastRemoveFailureReason().contains(envS3Config.secretKey)
                    && !s3NetworkDeleteStore.lastRemoveFailureReason().contains(envS3Config.sessionToken),
                "s3 DELETE network failure should expose a redacted stable remove failure reason") && ok;
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
                    && factoryError.contains(QString::fromUtf8("未启用"))
                    && !factoryError.contains(QStringLiteral("super-secret-value"))
                    && !factoryError.contains(QStringLiteral("access-key"))
                    && !factoryError.contains(QStringLiteral("temporary-session-token")),
                "configured s3 backend should stay gated by default without leaking credentials") && ok;
    qputenv("QTNETWORKCHAT_OBJECT_S3_ENABLE", "1");
    unsupportedStore = createObjectStore(QStringLiteral("s3"), QString(), &factoryError);
    ok = expect(unsupportedStore
                    && dynamic_cast<S3ObjectStore*>(unsupportedStore.get()) != nullptr
                    && factoryError.isEmpty(),
                "configured s3 backend should require an explicit opt-in before the factory creates it") && ok;
    unsupportedStore.reset();
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_ENDPOINT");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_BUCKET");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_REGION");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_SECRET_KEY");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_SESSION_TOKEN");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_PREFIX");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_TLS_VERIFY");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_TIMEOUT_MS");
    qunsetenv("QTNETWORKCHAT_OBJECT_S3_ENABLE");

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
