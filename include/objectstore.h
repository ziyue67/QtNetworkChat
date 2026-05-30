#ifndef OBJECTSTORE_H
#define OBJECTSTORE_H

#include <QByteArray>
#include <QIODevice>
#include <QMap>
#include <QNetworkRequest>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <memory>

class ObjectStore {
public:
    struct ValidationResult {
        bool ok = false;
        qint64 size = 0;
        QString fileHash;
        QString error;
    };

    virtual ~ObjectStore() = default;

    virtual bool writeObject(const QByteArray& data,
                             QString* objectKey,
                             QString* fileHash = nullptr,
                             QString* error = nullptr,
                             const QString& extension = QString()) const = 0;
    virtual ValidationResult validateObject(const QString& objectKey,
                                            qint64 expectedSize,
                                            const QString& expectedHash) const = 0;
    virtual std::unique_ptr<QIODevice> openObject(const QString& objectKey) const = 0;
    virtual bool removeObject(const QString& objectKey) const = 0;
    virtual int cleanupExpired(qint64 ttlMs, QStringList* removedKeys = nullptr) const = 0;
};

class FilesystemObjectStore : public ObjectStore {
public:
    explicit FilesystemObjectStore(const QString& rootDir);

    QString rootDir() const;
    static QString generateObjectKey(const QString& extension = QString());
    static bool isValidObjectKey(const QString& objectKey);

    QString objectPath(const QString& objectKey) const;
    bool writeObject(const QByteArray& data,
                     QString* objectKey,
                     QString* fileHash = nullptr,
                     QString* error = nullptr,
                     const QString& extension = QString()) const override;
    ValidationResult validateObject(const QString& objectKey,
                                    qint64 expectedSize,
                                    const QString& expectedHash) const override;
    std::unique_ptr<QIODevice> openObject(const QString& objectKey) const override;
    bool removeObject(const QString& objectKey) const override;
    int cleanupExpired(qint64 ttlMs, QStringList* removedKeys = nullptr) const override;

private:
    QString m_rootDir;
};

struct S3ObjectStoreConfig {
    QString endpoint;
    QString bucket;
    QString region;
    QString accessKey;
    QString secretKey;
    QString sessionToken;
    QString prefix;
    bool tlsVerify = true;
    int requestTimeoutMs = 30000;
};

struct S3SignedObjectRequest {
    QNetworkRequest request;
    QByteArray method;
    QString payloadSha256Hex;
    QString signedHeaders;
    QString authorizationHeader;
};

enum class S3HttpResultKind {
    Success,
    NotFound,
    Retryable,
    AuthError,
    ClientError,
    ServerError,
    Unknown
};

struct S3HttpResult {
    S3HttpResultKind kind = S3HttpResultKind::Unknown;
    bool ok = false;
    bool retryable = false;
    QString reason;
};

struct S3RequestResult {
    S3HttpResult http;
    int statusCode = 0;
    bool networkError = false;
    bool timeout = false;
    bool tlsError = false;
    QString error;
};

struct S3RequestExecutionResult {
    S3RequestResult result;
    QMap<QString, QString> headers;
    QByteArray body;
};

struct LargeFileDeliveredReceipt {
    QString sourceInstanceId;
    QString transferId;
    QString receiverId;
    QString objectKey;
    QString fileHash;
    qint64 confirmedBytes = 0;
};

struct LargeFileDeliveredFallback {
    QString sourceInstanceId;
    QString transferId;
    QString receiverId;
    QString objectKey;
    QString fileHash;
    qint64 fileSize = 0;
};

struct LargeFileDeliveredReceiptDecision {
    bool shouldCleanup = false;
    QString reason;
};

using S3RequestExecutor = std::function<S3RequestExecutionResult(const S3SignedObjectRequest&, const QByteArray&)>;

class S3ObjectStore : public ObjectStore {
public:
    explicit S3ObjectStore(const S3ObjectStoreConfig& config);
    S3ObjectStore(const S3ObjectStoreConfig& config, S3RequestExecutor requestExecutor);

    S3ObjectStoreConfig config() const;

    bool writeObject(const QByteArray& data,
                     QString* objectKey,
                     QString* fileHash = nullptr,
                     QString* error = nullptr,
                     const QString& extension = QString()) const override;
    ValidationResult validateObject(const QString& objectKey,
                                    qint64 expectedSize,
                                    const QString& expectedHash) const override;
    std::unique_ptr<QIODevice> openObject(const QString& objectKey) const override;
    QString lastOpenFailureReason() const;
    bool removeObject(const QString& objectKey) const override;
    int cleanupExpired(qint64 ttlMs, QStringList* removedKeys = nullptr) const override;

private:
    S3ObjectStoreConfig m_config;
    S3RequestExecutor m_requestExecutor;
    mutable QString m_lastOpenFailureReason;
};

QString normalizeObjectStoreType(const QString& storeType);
bool isSupportedObjectStoreType(const QString& storeType);
QString normalizeS3ObjectPrefix(const QString& prefix);
bool validateS3ObjectStoreConfig(const S3ObjectStoreConfig& config, QString* error = nullptr);
bool s3ObjectStoreEnabledFromEnvironment();
S3ObjectStoreConfig s3ObjectStoreConfigFromEnvironment();
QUrl s3ObjectUrl(const S3ObjectStoreConfig& config, const QString& objectKey);
bool isSupportedS3ObjectMethod(const QString& method);
S3HttpResult classifyS3HttpStatus(int statusCode);
QString redactS3ErrorText(const S3ObjectStoreConfig& config, const QString& text);
S3RequestResult s3RequestResultFromReply(const S3ObjectStoreConfig& config,
                                         int statusCode,
                                         const QString& errorText = QString(),
                                         bool timedOut = false,
                                         bool tlsFailed = false);
QString s3FailureReasonForLog(const S3RequestResult& result);
QString s3ValidationFailureReasonForLog(const ObjectStore::ValidationResult& result);
QString objectStoreWriteFailureReasonForLog(const QString& storeType,
                                            bool storeAvailable,
                                            const QString& writeError,
                                            const QString& expectedHash,
                                            const QString& actualHash);
LargeFileDeliveredReceiptDecision evaluateLargeFileDeliveredReceiptCleanup(
    const LargeFileDeliveredReceipt& receipt,
    const LargeFileDeliveredFallback& fallback);
S3RequestExecutionResult executeS3ObjectRequest(const S3ObjectStoreConfig& config,
                                                const S3SignedObjectRequest& request,
                                                const QByteArray& body = QByteArray());
ObjectStore::ValidationResult validateS3ObjectBody(const QByteArray& body,
                                                   qint64 expectedSize,
                                                   const QString& expectedHash);
QString s3PayloadSha256Hex(const QByteArray& payload);
QString s3CredentialScope(const QString& date, const QString& region);
QString s3CanonicalRequest(const QString& method,
                           const QUrl& url,
                           const QMap<QString, QString>& headers,
                           const QString& payloadSha256Hex,
                           QString* signedHeaders = nullptr);
QString s3StringToSign(const QString& amzDate,
                       const QString& credentialScope,
                       const QString& canonicalRequest);
QString s3SignatureHex(const QString& secretKey,
                       const QString& date,
                       const QString& region,
                       const QString& stringToSign);
QString s3AuthorizationHeader(const QString& accessKey,
                              const QString& credentialScope,
                              const QString& signedHeaders,
                              const QString& signatureHex);
S3SignedObjectRequest s3SignedObjectRequest(const S3ObjectStoreConfig& config,
                                            const QString& objectKey,
                                            const QString& method,
                                            const QByteArray& payload,
                                            const QString& amzDate = QString());
std::unique_ptr<ObjectStore> createObjectStore(const QString& storeType,
                                               const QString& rootDir,
                                               QString* error = nullptr);

#endif // OBJECTSTORE_H
