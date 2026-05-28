#include "objectstore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QMessageAuthenticationCode>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <memory>

namespace {
QString normalizeExtension(const QString& extension) {
    QString normalized = extension.trimmed();
    if (normalized.isEmpty()) {
        return QString();
    }
    if (normalized.startsWith('.')) {
        normalized.remove(0, 1);
    }
    static const QRegularExpression validExtension(QStringLiteral("^[A-Za-z0-9_-]{1,32}$"));
    if (!validExtension.match(normalized).hasMatch()) {
        return QString();
    }
    return "." + normalized;
}

QString sha256Hex(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QByteArray hmacSha256(const QByteArray& key, const QByteArray& message) {
    return QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha256);
}

QString fileSha256Hex(QFile& file) {
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(256 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            return QString();
        }
        hasher.addData(chunk);
    }
    return QString::fromLatin1(hasher.result().toHex());
}

bool envFlagDefaultTrue(const char* name) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    if (value.isEmpty()) {
        return true;
    }
    return !(value == "0" || value == "false" || value == "no" || value == "off");
}

QString collapseHeaderWhitespace(const QString& value) {
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    return QString(value).replace(whitespace, QStringLiteral(" ")).trimmed();
}

QString canonicalUri(const QUrl& url) {
    const QString path = url.path(QUrl::FullyEncoded);
    return path.isEmpty() ? QStringLiteral("/") : path;
}

QString canonicalQueryString(const QUrl& url) {
    const QString query = url.query(QUrl::FullyEncoded);
    if (query.isEmpty()) {
        return QString();
    }

    QStringList parts = query.split('&', Qt::KeepEmptyParts);
    std::sort(parts.begin(), parts.end());
    return parts.join('&');
}

QString s3HostHeader(const QUrl& url) {
    QString host = url.host(QUrl::FullyEncoded);
    const int port = url.port();
    const bool includePort = port > 0
        && !((url.scheme() == QStringLiteral("https") && port == 443)
             || (url.scheme() == QStringLiteral("http") && port == 80));
    if (includePort) {
        host += QStringLiteral(":%1").arg(port);
    }
    return host;
}

QString normalizedS3Region(const S3ObjectStoreConfig& config) {
    const QString region = config.region.trimmed();
    return region.isEmpty() ? QStringLiteral("us-east-1") : region;
}

QString normalizedAmzDate(const QString& amzDate) {
    const QString trimmed = amzDate.trimmed();
    if (!trimmed.isEmpty()) {
        return trimmed;
    }
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
}
}

FilesystemObjectStore::FilesystemObjectStore(const QString& rootDir)
    : m_rootDir(QDir::cleanPath(rootDir)) {
}

QString FilesystemObjectStore::rootDir() const {
    return m_rootDir;
}

QString FilesystemObjectStore::generateObjectKey(const QString& extension) {
    return QUuid::createUuid().toString(QUuid::WithoutBraces) + normalizeExtension(extension);
}

bool FilesystemObjectStore::isValidObjectKey(const QString& objectKey) {
    static const QRegularExpression validKey(QStringLiteral("^[A-Za-z0-9_-]{8,64}(\\.[A-Za-z0-9_-]{1,32})?$"));
    const QString trimmed = objectKey.trimmed();
    return trimmed == objectKey
        && !trimmed.isEmpty()
        && !trimmed.contains("..")
        && !trimmed.contains('/')
        && !trimmed.contains('\\')
        && !trimmed.contains(':')
        && validKey.match(trimmed).hasMatch();
}

QString FilesystemObjectStore::objectPath(const QString& objectKey) const {
    if (m_rootDir.isEmpty() || !isValidObjectKey(objectKey)) {
        return QString();
    }

    const QString rootPath = QDir(m_rootDir).absolutePath();
    const QString candidatePath = QDir(rootPath).absoluteFilePath(objectKey);
    const QString cleanRoot = QDir::cleanPath(rootPath);
    const QString cleanCandidate = QDir::cleanPath(candidatePath);
#ifdef Q_OS_WIN
    const bool insideRoot = cleanCandidate.compare(cleanRoot, Qt::CaseInsensitive) != 0
        && cleanCandidate.startsWith(cleanRoot + "/", Qt::CaseInsensitive);
#else
    const bool insideRoot = cleanCandidate != cleanRoot
        && cleanCandidate.startsWith(cleanRoot + "/");
#endif
    return insideRoot ? cleanCandidate : QString();
}

bool FilesystemObjectStore::writeObject(const QByteArray& data,
                                        QString* objectKey,
                                        QString* fileHash,
                                        QString* error,
                                        const QString& extension) const {
    if (objectKey) {
        objectKey->clear();
    }
    if (fileHash) {
        fileHash->clear();
    }
    if (error) {
        error->clear();
    }
    if (m_rootDir.isEmpty()) {
        if (error) *error = QStringLiteral("对象存储根目录未配置");
        return false;
    }
    if (!QDir().mkpath(m_rootDir)) {
        if (error) *error = QStringLiteral("对象存储根目录不可写");
        return false;
    }

    for (int attempt = 0; attempt < 8; ++attempt) {
        const QString key = generateObjectKey(extension);
        const QString path = objectPath(key);
        if (path.isEmpty() || QFile::exists(path)) {
            continue;
        }

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) *error = file.errorString();
            return false;
        }
        if (file.write(data) != data.size()) {
            if (error) *error = file.errorString();
            return false;
        }
        if (!file.commit()) {
            if (error) *error = file.errorString();
            return false;
        }
        if (objectKey) *objectKey = key;
        if (fileHash) *fileHash = sha256Hex(data);
        return true;
    }

    if (error) *error = QStringLiteral("无法生成唯一对象 key");
    return false;
}

FilesystemObjectStore::ValidationResult FilesystemObjectStore::validateObject(const QString& objectKey,
                                                                              qint64 expectedSize,
                                                                              const QString& expectedHash) const {
    ValidationResult result;
    const QString path = objectPath(objectKey);
    if (path.isEmpty()) {
        result.error = QStringLiteral("对象 key 非法");
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("对象不存在或不可读");
        return result;
    }

    result.size = file.size();
    if (expectedSize >= 0 && result.size != expectedSize) {
        result.error = QStringLiteral("对象大小不一致");
        return result;
    }

    result.fileHash = fileSha256Hex(file);
    if (result.fileHash.isEmpty()) {
        result.error = QStringLiteral("对象哈希计算失败");
        return result;
    }
    if (!expectedHash.trimmed().isEmpty()
        && result.fileHash.compare(expectedHash.trimmed(), Qt::CaseInsensitive) != 0) {
        result.error = QStringLiteral("对象哈希不一致");
        return result;
    }

    result.ok = true;
    return result;
}

std::unique_ptr<QIODevice> FilesystemObjectStore::openObject(const QString& objectKey) const {
    const QString path = objectPath(objectKey);
    if (path.isEmpty()) {
        return {};
    }

    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) {
        return {};
    }
    return file;
}

bool FilesystemObjectStore::removeObject(const QString& objectKey) const {
    const QString path = objectPath(objectKey);
    return !path.isEmpty() && QFile::remove(path);
}

int FilesystemObjectStore::cleanupExpired(qint64 ttlMs, QStringList* removedKeys) const {
    if (removedKeys) {
        removedKeys->clear();
    }
    if (ttlMs < 0 || m_rootDir.isEmpty() || !QDir(m_rootDir).exists()) {
        return 0;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    int removed = 0;
    QDirIterator it(m_rootDir, QDir::Files | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        const QString key = info.fileName();
        if (!isValidObjectKey(key)) {
            continue;
        }
        if (info.lastModified().toUTC().msecsTo(now) <= ttlMs) {
            continue;
        }
        if (QFile::remove(path)) {
            ++removed;
            if (removedKeys) {
                removedKeys->append(key);
            }
        }
    }
    return removed;
}

S3ObjectStore::S3ObjectStore(const S3ObjectStoreConfig& config)
    : m_config(config) {
    m_config.prefix = normalizeS3ObjectPrefix(m_config.prefix);
}

S3ObjectStoreConfig S3ObjectStore::config() const {
    return m_config;
}

bool S3ObjectStore::writeObject(const QByteArray& data,
                                QString* objectKey,
                                QString* fileHash,
                                QString* error,
                                const QString& extension) const {
    Q_UNUSED(data);
    Q_UNUSED(extension);
    if (objectKey) {
        objectKey->clear();
    }
    if (fileHash) {
        fileHash->clear();
    }
    if (error) {
        *error = QStringLiteral("S3对象存储后端暂未实现");
    }
    return false;
}

ObjectStore::ValidationResult S3ObjectStore::validateObject(const QString& objectKey,
                                                            qint64 expectedSize,
                                                            const QString& expectedHash) const {
    Q_UNUSED(objectKey);
    Q_UNUSED(expectedSize);
    Q_UNUSED(expectedHash);
    ValidationResult result;
    result.error = QStringLiteral("S3对象存储后端暂未实现");
    return result;
}

std::unique_ptr<QIODevice> S3ObjectStore::openObject(const QString& objectKey) const {
    Q_UNUSED(objectKey);
    return {};
}

bool S3ObjectStore::removeObject(const QString& objectKey) const {
    Q_UNUSED(objectKey);
    return false;
}

int S3ObjectStore::cleanupExpired(qint64 ttlMs, QStringList* removedKeys) const {
    Q_UNUSED(ttlMs);
    if (removedKeys) {
        removedKeys->clear();
    }
    return 0;
}

QString normalizeObjectStoreType(const QString& storeType) {
    const QString normalized = storeType.trimmed().toLower();
    return normalized.isEmpty() ? QStringLiteral("filesystem") : normalized;
}

bool isSupportedObjectStoreType(const QString& storeType) {
    return normalizeObjectStoreType(storeType) == QStringLiteral("filesystem");
}

QString normalizeS3ObjectPrefix(const QString& prefix) {
    QString normalized = prefix.trimmed();
    while (normalized.startsWith('/')) {
        normalized.remove(0, 1);
    }
    while (normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized.isEmpty() ? QString() : normalized + "/";
}

bool validateS3ObjectStoreConfig(const S3ObjectStoreConfig& config, QString* error) {
    if (error) {
        error->clear();
    }

    const QUrl endpoint(config.endpoint.trimmed());
    if (!endpoint.isValid()
        || endpoint.host().isEmpty()
        || (endpoint.scheme() != QStringLiteral("https") && endpoint.scheme() != QStringLiteral("http"))) {
        if (error) *error = QStringLiteral("S3 endpoint 必须是有效的 http/https URL");
        return false;
    }

    static const QRegularExpression validBucket(QStringLiteral("^[a-z0-9][a-z0-9.-]{1,61}[a-z0-9]$"));
    const QString bucket = config.bucket.trimmed();
    if (!validBucket.match(bucket).hasMatch()
        || bucket.contains(QStringLiteral(".."))
        || bucket.contains(QStringLiteral(".-"))
        || bucket.contains(QStringLiteral("-."))) {
        if (error) *error = QStringLiteral("S3 bucket 名称非法");
        return false;
    }

    if (config.accessKey.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("S3 access key 未配置");
        return false;
    }
    if (config.secretKey.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("S3 secret key 未配置");
        return false;
    }

    const QString prefix = normalizeS3ObjectPrefix(config.prefix);
    static const QRegularExpression validPrefix(QStringLiteral("^[A-Za-z0-9._/-]*$"));
    if (prefix.contains(QStringLiteral(".."))
        || prefix.contains('\\')
        || prefix.contains(':')
        || prefix.contains(QStringLiteral("//"))
        || !validPrefix.match(prefix).hasMatch()) {
        if (error) *error = QStringLiteral("S3 object key 前缀非法");
        return false;
    }

    return true;
}

S3ObjectStoreConfig s3ObjectStoreConfigFromEnvironment() {
    S3ObjectStoreConfig config;
    config.endpoint = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_S3_ENDPOINT")).trimmed();
    config.bucket = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_S3_BUCKET")).trimmed();
    config.region = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_S3_REGION")).trimmed();
    config.accessKey = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY")).trimmed();
    config.secretKey = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_S3_SECRET_KEY")).trimmed();
    config.prefix = normalizeS3ObjectPrefix(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_S3_PREFIX")));
    config.tlsVerify = envFlagDefaultTrue("QTNETWORKCHAT_OBJECT_S3_TLS_VERIFY");
    return config;
}

QUrl s3ObjectUrl(const S3ObjectStoreConfig& config, const QString& objectKey) {
    QString configError;
    if (!validateS3ObjectStoreConfig(config, &configError)
        || !FilesystemObjectStore::isValidObjectKey(objectKey)) {
        return {};
    }

    QUrl url(config.endpoint.trimmed());
    QString path = url.path();
    if (!path.endsWith('/')) {
        path += "/";
    }
    path += config.bucket.trimmed() + "/" + normalizeS3ObjectPrefix(config.prefix) + objectKey.trimmed();
    url.setPath(path);
    return url;
}

bool isSupportedS3ObjectMethod(const QString& method) {
    const QString normalized = method.trimmed().toUpper();
    return normalized == QStringLiteral("PUT")
        || normalized == QStringLiteral("GET")
        || normalized == QStringLiteral("HEAD")
        || normalized == QStringLiteral("DELETE");
}

S3HttpResult classifyS3HttpStatus(int statusCode) {
    S3HttpResult result;

    if (statusCode == 200 || statusCode == 201 || statusCode == 204 || statusCode == 206) {
        result.kind = S3HttpResultKind::Success;
        result.ok = true;
        result.reason = QStringLiteral("success");
        return result;
    }

    if (statusCode == 404) {
        result.kind = S3HttpResultKind::NotFound;
        result.reason = QStringLiteral("not_found");
        return result;
    }

    if (statusCode == 401 || statusCode == 403) {
        result.kind = S3HttpResultKind::AuthError;
        result.reason = QStringLiteral("auth_or_permission_error");
        return result;
    }

    if (statusCode == 408 || statusCode == 409 || statusCode == 425 || statusCode == 429) {
        result.kind = S3HttpResultKind::Retryable;
        result.retryable = true;
        result.reason = QStringLiteral("retryable_client_status");
        return result;
    }

    if (statusCode >= 500 && statusCode <= 599) {
        result.kind = S3HttpResultKind::ServerError;
        result.retryable = true;
        result.reason = QStringLiteral("server_error");
        return result;
    }

    if (statusCode >= 400 && statusCode <= 499) {
        result.kind = S3HttpResultKind::ClientError;
        result.reason = QStringLiteral("client_error");
        return result;
    }

    result.kind = S3HttpResultKind::Unknown;
    result.reason = QStringLiteral("unknown_status");
    return result;
}

QString s3PayloadSha256Hex(const QByteArray& payload) {
    return sha256Hex(payload);
}

QString s3CredentialScope(const QString& date, const QString& region) {
    return QStringLiteral("%1/%2/s3/aws4_request").arg(date.trimmed(), region.trimmed());
}

QString s3CanonicalRequest(const QString& method,
                           const QUrl& url,
                           const QMap<QString, QString>& headers,
                           const QString& payloadSha256Hex,
                           QString* signedHeaders) {
    if (signedHeaders) {
        signedHeaders->clear();
    }

    QMap<QString, QString> canonicalHeaders;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        const QString name = it.key().trimmed().toLower();
        if (name.isEmpty()) {
            continue;
        }
        canonicalHeaders.insert(name, collapseHeaderWhitespace(it.value()));
    }

    QStringList headerLines;
    QStringList signedHeaderNames;
    for (auto it = canonicalHeaders.constBegin(); it != canonicalHeaders.constEnd(); ++it) {
        headerLines.append(QStringLiteral("%1:%2\n").arg(it.key(), it.value()));
        signedHeaderNames.append(it.key());
    }

    const QString signedHeaderText = signedHeaderNames.join(';');
    if (signedHeaders) {
        *signedHeaders = signedHeaderText;
    }

    return QStringLiteral("%1\n%2\n%3\n%4\n%5\n%6")
        .arg(method.trimmed().toUpper(),
             canonicalUri(url),
             canonicalQueryString(url),
             headerLines.join(QString()),
             signedHeaderText,
             payloadSha256Hex.trimmed().toLower());
}

QString s3StringToSign(const QString& amzDate,
                       const QString& credentialScope,
                       const QString& canonicalRequest) {
    const QString canonicalHash = sha256Hex(canonicalRequest.toUtf8());
    return QStringLiteral("AWS4-HMAC-SHA256\n%1\n%2\n%3")
        .arg(amzDate.trimmed(), credentialScope.trimmed(), canonicalHash);
}

QString s3SignatureHex(const QString& secretKey,
                       const QString& date,
                       const QString& region,
                       const QString& stringToSign) {
    const QByteArray kDate = hmacSha256(QByteArrayLiteral("AWS4") + secretKey.toUtf8(), date.trimmed().toUtf8());
    const QByteArray kRegion = hmacSha256(kDate, region.trimmed().toUtf8());
    const QByteArray kService = hmacSha256(kRegion, QByteArrayLiteral("s3"));
    const QByteArray kSigning = hmacSha256(kService, QByteArrayLiteral("aws4_request"));
    return QString::fromLatin1(hmacSha256(kSigning, stringToSign.toUtf8()).toHex());
}

QString s3AuthorizationHeader(const QString& accessKey,
                              const QString& credentialScope,
                              const QString& signedHeaders,
                              const QString& signatureHex) {
    return QStringLiteral("AWS4-HMAC-SHA256 Credential=%1/%2,SignedHeaders=%3,Signature=%4")
        .arg(accessKey.trimmed(),
             credentialScope.trimmed(),
             signedHeaders.trimmed(),
             signatureHex.trimmed().toLower());
}

S3SignedObjectRequest s3SignedObjectRequest(const S3ObjectStoreConfig& config,
                                            const QString& objectKey,
                                            const QString& method,
                                            const QByteArray& payload,
                                            const QString& amzDate) {
    S3SignedObjectRequest result;
    result.method = method.trimmed().toUpper().toLatin1();

    const QUrl url = s3ObjectUrl(config, objectKey);
    if (!url.isValid() || result.method.isEmpty() || !isSupportedS3ObjectMethod(QString::fromLatin1(result.method))) {
        result.method.clear();
        return result;
    }

    const QString requestDate = normalizedAmzDate(amzDate);
    const QString credentialDate = requestDate.left(8);
    const QString region = normalizedS3Region(config);
    result.payloadSha256Hex = s3PayloadSha256Hex(payload);

    QMap<QString, QString> headers;
    headers.insert(QStringLiteral("host"), s3HostHeader(url));
    headers.insert(QStringLiteral("x-amz-content-sha256"), result.payloadSha256Hex);
    headers.insert(QStringLiteral("x-amz-date"), requestDate);

    const QString canonicalRequest = s3CanonicalRequest(QString::fromLatin1(result.method),
                                                        url,
                                                        headers,
                                                        result.payloadSha256Hex,
                                                        &result.signedHeaders);
    const QString credentialScope = s3CredentialScope(credentialDate, region);
    const QString stringToSign = s3StringToSign(requestDate, credentialScope, canonicalRequest);
    const QString signature = s3SignatureHex(config.secretKey, credentialDate, region, stringToSign);
    result.authorizationHeader = s3AuthorizationHeader(config.accessKey,
                                                       credentialScope,
                                                       result.signedHeaders,
                                                       signature);

    result.request = QNetworkRequest(url);
    result.request.setRawHeader("host", headers.value(QStringLiteral("host")).toLatin1());
    result.request.setRawHeader("x-amz-content-sha256", result.payloadSha256Hex.toLatin1());
    result.request.setRawHeader("x-amz-date", requestDate.toLatin1());
    result.request.setRawHeader("Authorization", result.authorizationHeader.toLatin1());
#if QT_CONFIG(ssl)
    if (url.scheme() == QStringLiteral("https") && !config.tlsVerify) {
        QSslConfiguration sslConfig = result.request.sslConfiguration();
        sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
        result.request.setSslConfiguration(sslConfig);
    }
#endif
    return result;
}

std::unique_ptr<ObjectStore> createObjectStore(const QString& storeType,
                                               const QString& rootDir,
                                               QString* error) {
    if (error) {
        error->clear();
    }

    const QString normalizedType = normalizeObjectStoreType(storeType);
    if (normalizedType == QStringLiteral("s3")) {
        QString configError;
        const S3ObjectStoreConfig config = s3ObjectStoreConfigFromEnvironment();
        if (!validateS3ObjectStoreConfig(config, &configError)) {
            if (error) {
                *error = QStringLiteral("S3对象存储配置无效: %1").arg(configError);
            }
            return {};
        }
        if (error) {
            *error = QStringLiteral("S3对象存储后端暂未实现");
        }
        return {};
    }

    if (normalizedType != QStringLiteral("filesystem")) {
        if (error) {
            *error = QStringLiteral("对象存储后端暂不支持: %1").arg(normalizedType);
        }
        return {};
    }

    const QString cleanRoot = QDir::cleanPath(rootDir.trimmed());
    if (cleanRoot.isEmpty()) {
        if (error) {
            *error = QStringLiteral("对象存储根目录未配置");
        }
        return {};
    }

    return std::make_unique<FilesystemObjectStore>(cleanRoot);
}
