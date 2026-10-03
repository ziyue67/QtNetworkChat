#include "client.h"
#include "filetransferstatus.h"
#include "objectstore.h"
#include "qtnetworkchat_version.h"
#include "tlssecurity.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDataStream>
#include <QFileInfo>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QSslSocket>
#include <QSslError>
#include <QSslConfiguration>
#include <QCryptographicHash>
#include <QDateTime>
#include <QRandomGenerator>
#include <QUrl>
#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketProtocol>

namespace {
constexpr qint64 kMaxOutgoingPayloadBytes = 80LL * 1024 * 1024;
constexpr qint64 kTransferChunkBytes = 256LL * 1024;
constexpr qint64 kMaxIncomingChunks = 4096;
constexpr int kChunkAckTimeoutMs = 4000;
constexpr int kChunkSendMaxAttempts = 3;
constexpr int kDefaultE2ESessionMessageLimit = 100;
constexpr qint64 kTransferStaleTimeoutMs = 2LL * 60 * 1000;
constexpr int kTransferCleanupIntervalMs = 30 * 1000;
constexpr qint64 kOutgoingTransferStateMaxAgeMs = 24LL * 60 * 60 * 1000;
const char kOutgoingTransferStateFileName[] = "outgoing_transfer_state.json";
const char kE2EFileResumeCachePrefix[] = "e2e_file_resume_cache_";
const char kE2EFileResumeCacheSuffix[] = ".bin";
const char kE2EFileResumeCacheFormat[] = "qtnetworkchat-e2e-file-wire-cache-v1";
const char kE2ETrustPinsFilePrefix[] = "e2e_trust_pins_";
const char kE2EIdentityFilePrefix[] = "e2e_identity_";
constexpr qsizetype kMaxE2EIdentityPublicKeyBytes = 4096;
constexpr qsizetype kE2ETrustFingerprintHexLength = 64;
constexpr qsizetype kMaxAvatarBytes = 128 * 1024;
const char kE2EDraftBackendId[] = "draft-qt-hmac-stream-v1";

bool envEnabled(const char* name) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

bool envEnabledOrDefault(const char* name, bool defaultValue) {
    return qEnvironmentVariableIsSet(name) ? envEnabled(name) : defaultValue;
}

bool clientTlsEnabled() {
    return envEnabledOrDefault("QTNETWORKCHAT_TLS", QTNETWORKCHAT_DEFAULT_TLS != 0);
}

bool clientTlsVerifyEnabled() {
    return envEnabledOrDefault("QTNETWORKCHAT_TLS_VERIFY",
                               QTNETWORKCHAT_DEFAULT_TLS_VERIFY != 0);
}

QString clientTransportName() {
    QString value = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_TRANSPORT"))
        .trimmed().toLower();
    if (value.isEmpty()) {
        value = QString::fromLatin1(QTNETWORKCHAT_DEFAULT_TRANSPORT).trimmed().toLower();
    }
    return value.isEmpty() ? QStringLiteral("tcp") : value;
}

bool isWebSocketTransport(const QString& transport) {
    return transport == QStringLiteral("ws") || transport == QStringLiteral("wss");
}

bool isSupportedClientTransport(const QString& transport) {
    return transport == QStringLiteral("tcp")
        || transport == QStringLiteral("tls")
        || isWebSocketTransport(transport);
}

bool tcpTransportUsesTls(const QString& transport) {
    return transport == QStringLiteral("tls")
        || (transport == QStringLiteral("tcp") && clientTlsEnabled());
}

QUrl configuredWebSocketUrl(const QString& transport,
                            const QString& host,
                            quint16 port) {
    const QString configuredUrl = QString::fromLocal8Bit(
        qgetenv("QTNETWORKCHAT_WEBSOCKET_URL")).trimmed();
    if (!configuredUrl.isEmpty()) {
        return QUrl(configuredUrl, QUrl::StrictMode);
    }

    QString path = QString::fromLocal8Bit(
        qgetenv("QTNETWORKCHAT_WEBSOCKET_PATH")).trimmed();
    if (path.isEmpty()) {
        path = QString::fromLatin1(QTNETWORKCHAT_DEFAULT_WEBSOCKET_PATH).trimmed();
    }
    if (path.isEmpty()) {
        path = QStringLiteral("/ws");
    } else if (!path.startsWith(QLatin1Char('/'))) {
        path.prepend(QLatin1Char('/'));
    }

    QUrl url;
    url.setScheme(transport);
    url.setHost(host.trimmed());
    url.setPort(port);
    url.setPath(path);
    return url;
}

QString avatarBase64FromPngData(const QByteArray& pngData) {
    if (pngData.isEmpty()
        || pngData.size() > kMaxAvatarBytes
        || !pngData.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) {
        return QString();
    }
    return QString::fromLatin1(pngData.toBase64());
}

QString appDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool isSafeObjectStoreKeyToken(const QString& value) {
    const QString trimmed = value.trimmed();
    if (trimmed != value || trimmed.isEmpty() || trimmed.size() > 97) {
        return false;
    }
    const int dotIndex = trimmed.indexOf(QLatin1Char('.'));
    const QString keyBody = dotIndex < 0 ? trimmed : trimmed.left(dotIndex);
    const QString extension = dotIndex < 0 ? QString() : trimmed.mid(dotIndex + 1);
    if (keyBody.size() < 8 || keyBody.size() > 64 || extension.size() > 32) {
        return false;
    }
    for (const QChar ch : keyBody) {
        const ushort code = ch.unicode();
        const bool alpha = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
        const bool digit = code >= '0' && code <= '9';
        if (!alpha && !digit && ch != QLatin1Char('_') && ch != QLatin1Char('-')) {
            return false;
        }
    }
    for (const QChar ch : extension) {
        const ushort code = ch.unicode();
        const bool alpha = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
        const bool digit = code >= '0' && code <= '9';
        if (!alpha && !digit && ch != QLatin1Char('_') && ch != QLatin1Char('-')) {
            return false;
        }
    }
    return true;
}

QString normalizedE2EObjectRecoveryStoreType(const QString& value) {
    const QString storeType = value.trimmed().toLower();
    if (storeType == QLatin1String("filesystem")
        || storeType == QLatin1String("s3")
        || storeType == QLatin1String("offline")) {
        return storeType;
    }
    return QStringLiteral("unsupported");
}

bool isS3OfflineE2EObjectRecoveryStore(const QString& storeType) {
    return storeType == QLatin1String("s3") || storeType == QLatin1String("offline");
}

bool e2eS3ObjectRecoveryReviewedEnabled() {
    return envEnabled("QTNETWORKCHAT_E2E_S3_OBJECT_RECOVERY_REVIEWED");
}

bool e2eOfflineObjectRecoveryReviewedEnabled() {
    return envEnabled("QTNETWORKCHAT_E2E_OFFLINE_OBJECT_RECOVERY_REVIEWED");
}

QString e2eOfflineObjectRecoveryRootDir() {
    const QString root = QString::fromLocal8Bit(
        qgetenv("QTNETWORKCHAT_E2E_OFFLINE_OBJECT_RECOVERY_ROOT")).trimmed();
    return root.isEmpty() ? QString() : QDir::cleanPath(root);
}

QString e2eObjectRecoveryScopeForStoreType(const QString& storeType) {
    if (storeType == QLatin1String("filesystem")) {
        return QStringLiteral("filesystem-object-ciphertext-readback");
    }
    if (storeType == QLatin1String("s3") && e2eS3ObjectRecoveryReviewedEnabled()) {
        return QStringLiteral("s3-object-ciphertext-readback");
    }
    if (storeType == QLatin1String("s3")) {
        return QStringLiteral("s3-offline-auto-readback");
    }
    if (storeType == QLatin1String("offline") && e2eOfflineObjectRecoveryReviewedEnabled()) {
        return QStringLiteral("offline-ciphertext-readback");
    }
    if (storeType == QLatin1String("offline")) {
        return QStringLiteral("offline-auto-readback");
    }
    return QStringLiteral("unsupported-object-store-readback");
}

QString e2eObjectRecoveryReviewGateForStoreType(const QString& storeType, bool objectKeySafe) {
    if (!objectKeySafe) {
        return QStringLiteral("object-store-key-token-invalid");
    }
    if (storeType == QLatin1String("filesystem")) {
        return QStringLiteral("filesystem-object-ciphertext-readback-reviewed");
    }
    if (storeType == QLatin1String("s3") && e2eS3ObjectRecoveryReviewedEnabled()) {
        return QStringLiteral("s3-object-ciphertext-readback-reviewed");
    }
    if (storeType == QLatin1String("s3")) {
        return QStringLiteral("s3-offline-auto-readback-not-reviewed");
    }
    if (storeType == QLatin1String("offline") && e2eOfflineObjectRecoveryReviewedEnabled()) {
        return QStringLiteral("offline-ciphertext-readback-reviewed");
    }
    if (storeType == QLatin1String("offline")) {
        return QStringLiteral("offline-auto-readback-not-reviewed");
    }
    return QStringLiteral("object-store-type-unsupported");
}

QString e2eObjectRecoveryDefaultReasonForStoreType(const QString& storeType, bool objectKeySafe) {
    if (!objectKeySafe) {
        return QStringLiteral("e2e-file-object-recovery-evidence-invalid");
    }
    if (storeType == QLatin1String("s3") && e2eS3ObjectRecoveryReviewedEnabled()) {
        return QStringLiteral("e2e-file-object-recovery-read-path-unavailable");
    }
    if (storeType == QLatin1String("s3")) {
        return QStringLiteral("e2e-file-s3-offline-auto-readback-not-reviewed");
    }
    if (storeType == QLatin1String("offline") && e2eOfflineObjectRecoveryReviewedEnabled()) {
        return QStringLiteral("e2e-file-object-recovery-read-path-unavailable");
    }
    if (storeType == QLatin1String("offline")) {
        return QStringLiteral("e2e-file-offline-auto-readback-not-reviewed");
    }
    return QStringLiteral("e2e-file-object-recovery-read-path-unavailable");
}

QString e2eObjectRecoveryDefaultActionForStoreType(const QString& storeType, bool objectKeySafe) {
    if (!objectKeySafe) {
        return QStringLiteral("suppress-object-key-and-resend");
    }
    if (storeType == QLatin1String("s3") && e2eS3ObjectRecoveryReviewedEnabled()) {
        return QStringLiteral("resend-or-wait-for-object-recovery");
    }
    if (storeType == QLatin1String("s3")) {
        return QStringLiteral("keep-s3-offline-auto-readback-fail-closed-until-reviewed");
    }
    if (storeType == QLatin1String("offline") && e2eOfflineObjectRecoveryReviewedEnabled()) {
        return QStringLiteral("resend-or-wait-for-object-recovery");
    }
    if (storeType == QLatin1String("offline")) {
        return QStringLiteral("keep-offline-auto-readback-fail-closed-until-reviewed");
    }
    return QStringLiteral("resend-or-wait-for-object-recovery");
}

bool isRetriableFileChunkRejectReason(const QString& reason) {
    const QString trimmed = reason.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    const QStringList fatalTokens = {
        QString::fromUtf8("元数据"),
        QString::fromUtf8("哈希"),
        QString::fromUtf8("校验"),
        QString::fromUtf8("发送者"),
        QString::fromUtf8("分片序号"),
        QString::fromUtf8("分片数量"),
        QString::fromUtf8("分片大小"),
        QString::fromUtf8("非末尾"),
        QString::fromUtf8("文件大小"),
        QString::fromUtf8("超过"),
        QString::fromUtf8("非法"),
        QString::fromUtf8("不一致"),
        QString::fromUtf8("不存在"),
        QString::fromUtf8("取消")
    };
    for (const QString& token : fatalTokens) {
        if (trimmed.contains(token, Qt::CaseInsensitive)) {
            return false;
        }
    }

    const QStringList retriableTokens = {
        QString::fromUtf8("临时"),
        QString::fromUtf8("繁忙"),
        QString::fromUtf8("重试"),
        QString::fromUtf8("稍后"),
        QString::fromLatin1("busy"),
        QString::fromLatin1("temporary"),
        QString::fromLatin1("timeout"),
        QString::fromLatin1("retry")
    };
    for (const QString& token : retriableTokens) {
        if (trimmed.contains(token, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QTcpSocket* createClientSocket(QObject* parent, bool tlsEnabled) {
    if (tlsEnabled) {
        QSslSocket* socket = new QSslSocket(parent);
        socket->setPeerVerifyMode(clientTlsVerifyEnabled()
            ? QSslSocket::VerifyPeer
            : QSslSocket::VerifyNone);
        return socket;
    }
    return new QTcpSocket(parent);
}

bool collectFileTransferMetadata(const QString& filePath,
                                 qint64* fileSize,
                                 qint64* chunkCount,
                                 QString* fileHash) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile() || fileInfo.size() <= 0 || fileInfo.size() > kMaxOutgoingPayloadBytes) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kTransferChunkBytes);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            return false;
        }
        hasher.addData(chunk);
    }

    if (fileSize) *fileSize = fileInfo.size();
    if (chunkCount) *chunkCount = (fileInfo.size() + kTransferChunkBytes - 1) / kTransferChunkBytes;
    if (fileHash) *fileHash = QString::fromLatin1(hasher.result().toHex());
    return true;
}

bool resolveResumeProgress(qint64 confirmedBytes,
                           qint64 nextChunkIndex,
                           const QVector<qint64>& receivedChunks,
                           qint64 fileSize,
                           qint64 chunkCount,
                           QSet<qint64>* receivedSet,
                           qint64* firstMissingChunkIndex) {
    if (confirmedBytes < 0
        || confirmedBytes > fileSize
        || nextChunkIndex < 0
        || nextChunkIndex > chunkCount) {
        return false;
    }

    QSet<qint64> received;
    for (qint64 index : receivedChunks) {
        if (index < 0 || index >= chunkCount) {
            return false;
        }
        received.insert(index);
    }
    qint64 firstMissing = 0;
    while (firstMissing < chunkCount && received.contains(firstMissing)) {
        ++firstMissing;
    }

    const qint64 minimumConfirmedBytes = qMin(fileSize, firstMissing * kTransferChunkBytes);
    if (confirmedBytes < minimumConfirmedBytes) {
        return false;
    }
    if (receivedSet) *receivedSet = received;
    if (firstMissingChunkIndex) *firstMissingChunkIndex = firstMissing;
    return true;
}

qint64 receivedBytesFromChunks(const QSet<qint64>& receivedChunks,
                               qint64 fileSize,
                               qint64 chunkCount) {
    qint64 receivedBytes = 0;
    for (qint64 index : receivedChunks) {
        if (index < 0 || index >= chunkCount) {
            continue;
        }
        const qint64 chunkStart = index * kTransferChunkBytes;
        const qint64 chunkEnd = qMin(fileSize, chunkStart + kTransferChunkBytes);
        if (chunkEnd > chunkStart) {
            receivedBytes += chunkEnd - chunkStart;
        }
    }
    return qMin(receivedBytes, fileSize);
}

QString outgoingTransferStateFilePath() {
    QString dir = appDataDir();
    if (dir.isEmpty()) {
        dir = QDir::currentPath();
    }
    QDir().mkpath(dir);
    return QDir(dir).filePath(QString::fromLatin1(kOutgoingTransferStateFileName));
}

QString safeLocalFileToken(const QString& value);

QString e2eFileResumeCachePath(const QString& transferId) {
    QString dir = appDataDir();
    if (dir.isEmpty()) {
        dir = QDir::currentPath();
    }
    QDir().mkpath(dir);
    return QDir(dir).filePath(QString::fromLatin1(kE2EFileResumeCachePrefix)
                              + safeLocalFileToken(transferId)
                              + QString::fromLatin1(kE2EFileResumeCacheSuffix));
}

QString sha256Hex(const QByteArray& value) {
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}

bool removeE2EFileResumeCache(const QString& transferId) {
    const QString trimmedTransferId = transferId.trimmed();
    if (trimmedTransferId.isEmpty()) {
        return true;
    }
    const QString path = e2eFileResumeCachePath(trimmedTransferId);
    return !QFile::exists(path) || QFile::remove(path);
}

QString safeLocalFileToken(const QString& value) {
    QString token;
    for (const QChar ch : value.trimmed()) {
        const ushort code = ch.unicode();
        const bool alpha = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
        const bool digit = code >= '0' && code <= '9';
        token.append(alpha || digit ? ch : QLatin1Char('_'));
    }
    return token.isEmpty() ? QStringLiteral("default") : token.left(96);
}

QString e2eDataFilePath(const QString& prefix, const QString& userId) {
    QString dir = appDataDir();
    if (dir.isEmpty()) {
        dir = QDir::currentPath();
    }
    QDir().mkpath(dir);
    return QDir(dir).filePath(prefix + safeLocalFileToken(userId) + QStringLiteral(".json"));
}

QString e2eTrustPinsFilePath(const QString& userId) {
    return e2eDataFilePath(QString::fromLatin1(kE2ETrustPinsFilePrefix), userId);
}

QString e2eIdentityFilePath(const QString& userId) {
    return e2eDataFilePath(QString::fromLatin1(kE2EIdentityFilePrefix), userId);
}

bool isValidE2EFingerprint(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized.size() != kE2ETrustFingerprintHexLength) {
        return false;
    }
    for (const QChar ch : normalized) {
        const ushort code = ch.unicode();
        const bool digit = code >= '0' && code <= '9';
        const bool hex = code >= 'a' && code <= 'f';
        if (!digit && !hex) {
            return false;
        }
    }
    return true;
}

QString normalizedVerificationCode(QString value) {
    value = value.trimmed().toUpper();
    value.remove(QLatin1Char(' '));
    value.remove(QLatin1Char('-'));
    return value;
}

QString formatE2EVerificationCode(const QString& localFingerprint, const QString& peerFingerprint) {
    const QString left = localFingerprint.trimmed().toLower();
    const QString right = peerFingerprint.trimmed().toLower();
    if (!isValidE2EFingerprint(left) || !isValidE2EFingerprint(right) || left == right) {
        return QString();
    }
    const QString first = left < right ? left : right;
    const QString second = left < right ? right : left;
    const QByteArray digest = QCryptographicHash::hash(
        QByteArray("qtnetworkchat-e2e-cross-device-v1|") + first.toUtf8() + "|" + second.toUtf8(),
        QCryptographicHash::Sha256).toHex().toUpper();
    return QStringLiteral("%1-%2-%3")
        .arg(QString::fromLatin1(digest.left(4)),
             QString::fromLatin1(digest.mid(4, 4)),
             QString::fromLatin1(digest.mid(8, 4)));
}

QString shortE2EFingerprint(QString value) {
    value = value.trimmed().toLower();
    return value.size() <= 12 ? value : value.left(12);
}

QJsonArray e2eProductionRotationStageManifest(const QJsonObject& backendStatus,
                                              const QJsonObject& providerCompatibility,
                                              const QJsonObject& migrationPlan) {
    const QJsonObject operations = backendStatus.value(QStringLiteral("operations")).toObject();
    const auto appendStage = [&](QJsonArray* stages,
                                 const QString& name,
                                 const QString& requiredOperation,
                                 const QString& evidenceSource,
                                 bool localStateRequired) {
        const QJsonObject operation = operations.value(requiredOperation).toObject();
        const bool operationAvailable = operation.value(QStringLiteral("available")).toBool(false);
        const bool stateReady = !localStateRequired
            || migrationPlan.value(QStringLiteral("migrationRequired")).toBool(false);
        QJsonObject stage;
        stage[QStringLiteral("name")] = name;
        stage[QStringLiteral("requiredOperation")] = requiredOperation;
        stage[QStringLiteral("evidenceSource")] = evidenceSource;
        stage[QStringLiteral("operationAvailable")] = operationAvailable;
        stage[QStringLiteral("localStateRequired")] = localStateRequired;
        stage[QStringLiteral("localStateReady")] = stateReady;
        stage[QStringLiteral("providerCompatibilityGate")] =
            providerCompatibility.value(QStringLiteral("gate")).toString();
        stage[QStringLiteral("providerCompatibilityStatus")] =
            providerCompatibility.value(QStringLiteral("status")).toString();
        stage[QStringLiteral("status")] = operationAvailable && stateReady
            ? QStringLiteral("ready")
            : QStringLiteral("blocked");
        stage[QStringLiteral("blockedReason")] = operationAvailable && stateReady
            ? QString()
            : (!operationAvailable
                ? operation.value(QStringLiteral("blockedReason")).toString(
                    backendStatus.value(QStringLiteral("unavailableReason")).toString(
                        QStringLiteral("production-crypto-backend-unavailable")))
                : QStringLiteral("migration-not-required"));
        stage[QStringLiteral("operatorAction")] = operationAvailable && stateReady
            ? QStringLiteral("none")
            : (!operationAvailable
                ? operation.value(QStringLiteral("operatorAction")).toString(
                    QStringLiteral("complete-reviewed-production-provider-before-rotation"))
                : QStringLiteral("no-local-e2e-migration-needed"));
        stages->append(stage);
    };

    QJsonArray stages;
    appendStage(&stages,
                QStringLiteral("generate-production-identity"),
                QStringLiteral("identity-key-generation"),
                QStringLiteral("local-identity-store"),
                true);
    appendStage(&stages,
                QStringLiteral("derive-production-public-identity"),
                QStringLiteral("public-key-derivation"),
                QStringLiteral("local-identity-store"),
                true);
    appendStage(&stages,
                QStringLiteral("sign-production-agreement"),
                QStringLiteral("agreement-sign"),
                QStringLiteral("peer-trust-pins"),
                true);
    appendStage(&stages,
                QStringLiteral("verify-production-agreement"),
                QStringLiteral("agreement-verify"),
                QStringLiteral("peer-trust-pins"),
                true);
    appendStage(&stages,
                QStringLiteral("derive-production-session"),
                QStringLiteral("session-derive"),
                QStringLiteral("active-sessions"),
                true);
    appendStage(&stages,
                QStringLiteral("encrypt-production-payload"),
                QStringLiteral("payload-encrypt"),
                QStringLiteral("private-message-and-file-payloads"),
                true);
    appendStage(&stages,
                QStringLiteral("decrypt-production-payload"),
                QStringLiteral("payload-decrypt"),
                QStringLiteral("private-message-and-file-payloads"),
                true);
    return stages;
}

QString e2eCurrentBackendId() {
    return e2eCryptoBackendStatus().value(QStringLiteral("requestedBackendId")).toString(e2eCryptoBackendId()).trimmed();
}

bool e2eBackendUsableForLocalMaterial(QString materialBackendId,
                                      QString* rejectReason = nullptr) {
    QString availabilityReason;
    if (!e2eCryptoBackendAvailable(&availabilityReason)) {
        if (rejectReason) *rejectReason = availabilityReason.isEmpty()
            ? QStringLiteral("crypto-backend-unavailable")
            : availabilityReason;
        return false;
    }
    materialBackendId = materialBackendId.trimmed();
    if (materialBackendId.isEmpty()) {
        materialBackendId = QString::fromLatin1(kE2EDraftBackendId);
    }
    const QString requestedBackend = e2eCurrentBackendId();
    if (materialBackendId != requestedBackend) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-backend-migration-required");
        return false;
    }
    if (rejectReason) rejectReason->clear();
    return true;
}

QString base64Url(const QByteArray& value) {
    return QString::fromLatin1(value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QByteArray fromBase64Url(const QString& value) {
    return QByteArray::fromBase64(value.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

bool e2eEnvelopeHeaderLooksSafe(const QJsonObject& header) {
    const QByteArray nonce = fromBase64Url(header.value("nonce").toString());
    const QByteArray tag = fromBase64Url(header.value("tag").toString());
    return isSupportedE2EProtocol(header.value("protocol").toString())
        && isSupportedE2ESuite(header.value("suite").toString())
        && !header.value("senderId").toString().trimmed().isEmpty()
        && !header.value("receiverId").toString().trimmed().isEmpty()
        && header.value("senderId").toString().trimmed() != header.value("receiverId").toString().trimmed()
        && !header.value("keyId").toString().trimmed().isEmpty()
        && nonce.size() >= 8
        && nonce.size() <= 64
        && tag.size() >= 8
        && tag.size() <= 128
        && header.value("aad").toString().size() <= 512
        && (!header.contains("ciphertextSha256") || isValidE2EFingerprint(header.value("ciphertextSha256").toString()));
}

QJsonObject e2eEnvelopeHeaderJson(const E2EEnvelope& envelope) {
    QJsonObject header = envelope.toJson();
    header.remove(QStringLiteral("ciphertext"));
    return header;
}

bool writeE2EFileResumeCache(const QString& transferId,
                             const QByteArray& wirePayload,
                             const QString& expectedWireHash,
                             QString* cachePath = nullptr) {
    if (cachePath) cachePath->clear();
    const QString trimmedTransferId = transferId.trimmed();
    const QString trimmedWireHash = expectedWireHash.trimmed().toLower();
    if (trimmedTransferId.isEmpty()
        || wirePayload.isEmpty()
        || !isValidE2EFingerprint(trimmedWireHash)
        || sha256Hex(wirePayload).compare(trimmedWireHash, Qt::CaseInsensitive) != 0) {
        return false;
    }

    const QString path = e2eFileResumeCachePath(trimmedTransferId);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(wirePayload) != wirePayload.size() || !file.commit()) {
        return false;
    }
    if (cachePath) *cachePath = path;
    return true;
}

bool loadE2EFileResumeCache(const QString& transferId,
                            const QString& expectedWireHash,
                            qint64 expectedWireSize,
                            QByteArray* wirePayload,
                            QString* rejectReason = nullptr) {
    if (wirePayload) wirePayload->clear();
    if (rejectReason) rejectReason->clear();
    const QString trimmedTransferId = transferId.trimmed();
    const QString trimmedWireHash = expectedWireHash.trimmed().toLower();
    if (trimmedTransferId.isEmpty()
        || expectedWireSize <= 0
        || !isValidE2EFingerprint(trimmedWireHash)) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-resume-cache-invalid");
        return false;
    }

    QFile file(e2eFileResumeCachePath(trimmedTransferId));
    if (!file.open(QIODevice::ReadOnly)) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-resume-cache-missing");
        return false;
    }
    const QByteArray payload = file.readAll();
    if (payload.size() != expectedWireSize
        || sha256Hex(payload).compare(trimmedWireHash, Qt::CaseInsensitive) != 0) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-resume-cache-mismatch");
        return false;
    }
    if (wirePayload) *wirePayload = payload;
    return true;
}

bool loadE2EOfflineObjectRecoveryPayload(const QString& objectKey,
                                         const QString& objectHash,
                                         qint64 objectSize,
                                         QByteArray* wirePayload,
                                         QString* rejectReason) {
    if (!e2eOfflineObjectRecoveryReviewedEnabled()) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-offline-auto-readback-not-reviewed");
        return false;
    }

    const QString rootDir = e2eOfflineObjectRecoveryRootDir();
    if (rootDir.isEmpty()) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-object-recovery-store-unavailable");
        return false;
    }

    const QFileInfo rootInfo(rootDir);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-object-recovery-store-unavailable");
        return false;
    }

    const QString canonicalRoot =
        QDir::cleanPath(QDir::fromNativeSeparators(rootInfo.canonicalFilePath()));
    const QString candidatePath = QDir(rootInfo.absoluteFilePath()).absoluteFilePath(objectKey);
    const QFileInfo candidateInfo(candidatePath);
    const QString canonicalCandidate =
        QDir::cleanPath(QDir::fromNativeSeparators(candidateInfo.canonicalFilePath()));
    const QString rootWithSeparator = canonicalRoot.endsWith(QLatin1Char('/'))
        ? canonicalRoot
        : canonicalRoot + QLatin1Char('/');
    if (canonicalRoot.isEmpty()
        || canonicalCandidate.isEmpty()
        || !canonicalCandidate.startsWith(rootWithSeparator)
        || !candidateInfo.exists()
        || !candidateInfo.isFile()
        || candidateInfo.size() != objectSize) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-object-recovery-validation-failed");
        return false;
    }

    QFile file(canonicalCandidate);
    if (!file.open(QIODevice::ReadOnly)) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-object-recovery-read-failed");
        return false;
    }

    const QByteArray payload = file.readAll();
    if (payload.size() != objectSize
        || sha256Hex(payload).compare(objectHash, Qt::CaseInsensitive) != 0) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-object-recovery-validation-failed");
        return false;
    }

    if (wirePayload) *wirePayload = payload;
    return true;
}

bool loadE2EFileObjectRecoveryPayload(const QJsonObject& state,
                                      QByteArray* wirePayload,
                                      QString* rejectReason = nullptr) {
    if (wirePayload) wirePayload->clear();
    if (rejectReason) rejectReason->clear();

    const QString storeType = normalizedE2EObjectRecoveryStoreType(
        state.value(QStringLiteral("e2eFileObjectStoreType")).toString(QStringLiteral("filesystem")));
    const QString objectKey = state.value(QStringLiteral("e2eFileObjectStoreKey")).toString().trimmed();
    const QString objectHash = state.value(QStringLiteral("e2eFileObjectStoreHash")).toString().trimmed().toLower();
    const qint64 objectSize = state.value(QStringLiteral("e2eFileObjectStoreSize")).toVariant().toLongLong();
    const QString wireHash = state.value(QStringLiteral("e2eFileWireHash")).toString().trimmed().toLower();
    const qint64 wireSize = state.value(QStringLiteral("e2eFileWireSize")).toVariant().toLongLong();
    const QJsonObject envelopeHeader = state.value(QStringLiteral("e2eFileEnvelopeHeader")).toObject();
    if (!state.value(QStringLiteral("e2eFileObjectRecoveryCandidate")).toBool(false)
        || !isSafeObjectStoreKeyToken(objectKey)
        || !isValidE2EFingerprint(objectHash)
        || objectHash.compare(wireHash, Qt::CaseInsensitive) != 0
        || objectSize <= 0
        || objectSize != wireSize
        || !e2eEnvelopeHeaderLooksSafe(envelopeHeader)
        || envelopeHeader.value(QStringLiteral("ciphertextSha256")).toString().trimmed()
               .compare(objectHash, Qt::CaseInsensitive) != 0) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-object-recovery-evidence-invalid");
        return false;
    }

    if (storeType == QLatin1String("s3") && !e2eS3ObjectRecoveryReviewedEnabled()) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-s3-offline-auto-readback-not-reviewed");
        return false;
    }
    if (storeType == QLatin1String("offline")) {
        return loadE2EOfflineObjectRecoveryPayload(objectKey,
                                                   objectHash,
                                                   objectSize,
                                                   wirePayload,
                                                   rejectReason);
    }
    if (storeType != QLatin1String("filesystem") && storeType != QLatin1String("s3")) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-object-recovery-store-unavailable");
        return false;
    }

    const QString rootDir = storeType == QLatin1String("filesystem")
        ? QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_ROOT")).trimmed()
        : QString();
    QString storeError;
    std::unique_ptr<ObjectStore> objectStore =
        createObjectStore(storeType, rootDir, &storeError);
    if (!objectStore) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-object-recovery-store-unavailable");
        return false;
    }

    const ObjectStore::ValidationResult validation =
        objectStore->validateObject(objectKey, objectSize, objectHash);
    if (!validation.ok) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-object-recovery-validation-failed");
        return false;
    }

    std::unique_ptr<QIODevice> objectDevice = objectStore->openObject(objectKey);
    if (!objectDevice) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-object-recovery-read-failed");
        return false;
    }
    const QByteArray payload = objectDevice->readAll();
    if (payload.size() != objectSize
        || sha256Hex(payload).compare(objectHash, Qt::CaseInsensitive) != 0) {
        if (rejectReason) *rejectReason = QStringLiteral("e2e-file-object-recovery-validation-failed");
        return false;
    }

    if (wirePayload) *wirePayload = payload;
    return true;
}

bool loadE2EFileRecoveryWirePayload(const QJsonObject& state,
                                    QByteArray* wirePayload,
                                    QString* rejectReason = nullptr) {
    if (wirePayload) wirePayload->clear();
    if (rejectReason) rejectReason->clear();

    QString cacheRejectReason;
    if (state.value(QStringLiteral("e2eFileResumeCache")).toBool(false)
        && loadE2EFileResumeCache(state.value(QStringLiteral("transferId")).toString(),
                                  state.value(QStringLiteral("e2eFileWireHash")).toString(),
                                  state.value(QStringLiteral("e2eFileWireSize")).toVariant().toLongLong(),
                                  wirePayload,
                                  &cacheRejectReason)) {
        return true;
    }

    QString objectRejectReason;
    if (state.value(QStringLiteral("e2eFileObjectRecoveryCandidate")).toBool(false)) {
        if (loadE2EFileObjectRecoveryPayload(state, wirePayload, &objectRejectReason)) {
            return true;
        }
    }

    if (rejectReason) {
        *rejectReason = !objectRejectReason.isEmpty()
            ? objectRejectReason
            : (cacheRejectReason.isEmpty()
                ? QStringLiteral("e2e-file-resume-cache-unavailable")
                : cacheRejectReason);
    }
    return false;
}

QJsonObject e2eIdentityJson(const QString& userId, const QByteArray& publicKey) {
    QJsonObject obj;
    obj["protocol"] = QStringLiteral("qtnetworkchat-e2e-v1");
    obj["suite"] = e2eDefaultSuite();
    obj["userId"] = userId.trimmed();
    obj["publicKey"] = base64Url(publicKey);
    obj["publicKeyFingerprintSha256"] = e2eFingerprint(publicKey);
    obj["agreementSigning"] = !publicKey.isEmpty() && e2eCryptoBackendStatus().value("available").toBool(false);
    obj["signatureSuite"] = e2eAgreementSignatureSuite();
    obj["cryptoBackend"] = e2eCryptoBackendStatus();
    obj["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    return obj;
}

bool validateE2EIdentityJson(const QJsonObject& identity, QString* reason = nullptr) {
    if (!isSupportedE2EProtocol(identity.value("protocol").toString())) {
        if (reason) *reason = QStringLiteral("unsupported-protocol");
        return false;
    }
    if (!isSupportedE2ESuite(identity.value("suite").toString())) {
        if (reason) *reason = QStringLiteral("unsupported-suite");
        return false;
    }
    if (identity.value("userId").toString().trimmed().isEmpty()) {
        if (reason) *reason = QStringLiteral("invalid-peer");
        return false;
    }
    const QByteArray publicKey = fromBase64Url(identity.value("publicKey").toString());
    if (publicKey.isEmpty() || publicKey.size() > kMaxE2EIdentityPublicKeyBytes) {
        if (reason) *reason = QStringLiteral("invalid-public-key");
        return false;
    }
    const QString expectedFingerprint = e2eFingerprint(publicKey);
    if (identity.value("publicKeyFingerprintSha256").toString().trimmed().toLower() != expectedFingerprint) {
        if (reason) *reason = QStringLiteral("fingerprint-mismatch");
        return false;
    }
    const QString signatureSuite = identity.value("signatureSuite").toString().trimmed();
    if (!signatureSuite.isEmpty()
        && signatureSuite != e2eAgreementSignatureSuite()) {
        if (reason) *reason = QStringLiteral("unsupported-signature-suite");
        return false;
    }
    if (reason) reason->clear();
    return true;
}
}

Client::Client(QObject* parent)
    : QObject(parent)
    , m_socket(nullptr)
    , m_webSocket(nullptr)
    , m_heartbeatTimer(new QTimer(this))
    , m_transferCleanupTimer(new QTimer(this))
    , m_transportName(clientTransportName())
    , m_webSocketPinAccepted(false)
    , m_tcpTlsPinAccepted(false)
    , m_registerMode(false)
    , m_loginFinished(false)
    , m_loginOk(false)
    , m_loginWasRegister(false)
    , m_reconnectAttempts(0)
    , m_hasServerGroupSnapshot(false)
    , m_cancelOutgoingTransfer(false)
    , m_e2eSessionMessageLimit(kDefaultE2ESessionMessageLimit)
{
    if (isWebSocketTransport(m_transportName)) {
        m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        connect(m_webSocket, &QWebSocket::connected,
                this, &Client::onWebSocketConnected);
        connect(m_webSocket, &QWebSocket::disconnected,
                this, &Client::onDisconnected);
        connect(m_webSocket, &QWebSocket::binaryMessageReceived,
                this, [this](const QByteArray& message) {
                    processIncomingData(message);
                });
        connect(m_webSocket, &QWebSocket::textMessageReceived,
                this, [this](const QString& message) {
                    processIncomingData(message.toUtf8(), true);
                });
        connect(m_webSocket, &QWebSocket::sslErrors,
                this, [this](const QList<QSslError>&) {
                    // A configured certificate pin is an explicit trust root for self-signed
                    // deployments. The connected handler checks the peer certificate before login.
                    if (!configuredPinnedTlsFingerprint().isEmpty()
                        || !clientTlsVerifyEnabled()) {
                        m_webSocket->ignoreSslErrors();
                    }
                });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        connect(m_webSocket, &QWebSocket::errorOccurred,
                this, &Client::onWebSocketError);
#else
        connect(m_webSocket,
                QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
                this, &Client::onWebSocketError);
#endif
    } else {
        m_socket = createClientSocket(this, tcpTransportUsesTls(m_transportName));
        connect(m_socket, &QTcpSocket::readyRead, this, &Client::onReadyRead);
        if (QSslSocket* sslSocket = qobject_cast<QSslSocket*>(m_socket)) {
            connect(sslSocket, &QSslSocket::encrypted, this, &Client::onTlsEncrypted);
            connect(sslSocket, QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
                    this, [sslSocket](const QList<QSslError>&) {
                        // The TLS handshake must finish before onTlsEncrypted can check the pin.
                        if (!configuredPinnedTlsFingerprint().isEmpty()
                            || !clientTlsVerifyEnabled()) {
                            sslSocket->ignoreSslErrors();
                        }
                    });
        } else {
            connect(m_socket, &QTcpSocket::connected, this, &Client::onConnected);
        }
        connect(m_socket, &QTcpSocket::disconnected, this, &Client::onDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        connect(m_socket, &QTcpSocket::errorOccurred, this, &Client::onError);
#else
        connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
                this, &Client::onError);
#endif
    }

    connect(m_heartbeatTimer, &QTimer::timeout, this, &Client::onHeartbeat);
    connect(m_transferCleanupTimer, &QTimer::timeout, this, &Client::cleanupExpiredIncomingFileTransfers);
    loadOrCreateE2ELocalIdentity();
}

Client::~Client() {
    m_heartbeatTimer->stop();
    m_transferCleanupTimer->stop();
    if (m_webSocket && m_webSocket->state() != QAbstractSocket::UnconnectedState) {
        m_webSocket->close(QWebSocketProtocol::CloseCodeNormal,
                           QStringLiteral("client shutdown"));
    }
    if (m_socket && m_socket->isOpen()) {
        m_socket->disconnectFromHost();
    }
}

bool Client::isConnected() const {
    if (m_webSocket) {
        return m_webSocketPinAccepted
            && m_webSocket->state() == QAbstractSocket::ConnectedState;
    }
    if (qobject_cast<const QSslSocket*>(m_socket)) {
        return m_tcpTlsPinAccepted
            && m_socket->state() == QAbstractSocket::ConnectedState;
    }
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

bool Client::connectToServer(const QString& host, quint16 port) {
    m_serverHost = host;
    m_serverPort = port;
    m_loginError.clear();
    if (!isSupportedClientTransport(m_transportName)) {
        m_loginError = QStringLiteral("不支持的传输类型: %1").arg(m_transportName);
        emit connectionError(m_loginError);
        return false;
    }
    if (host.trimmed().isEmpty() || port == 0) {
        m_loginError = QStringLiteral("服务器地址或端口无效");
        emit connectionError(m_loginError);
        return false;
    }

    if (m_webSocket) {
        const QUrl url = configuredWebSocketUrl(m_transportName, host, port);
        if (!url.isValid()
            || url.host().isEmpty()
            || (url.scheme() != QStringLiteral("ws")
                && url.scheme() != QStringLiteral("wss"))) {
            m_loginError = QStringLiteral("WebSocket URL 无效: %1").arg(url.toString());
            emit connectionError(m_loginError);
            return false;
        }
        if (url.scheme() != m_transportName) {
            m_loginError = QStringLiteral("WebSocket URL 协议与传输类型不一致: %1 != %2")
                .arg(url.scheme(), m_transportName);
            emit connectionError(m_loginError);
            return false;
        }
        if (url.scheme() == QStringLiteral("ws")
            && !configuredPinnedTlsFingerprint().isEmpty()) {
            m_loginError = QStringLiteral("明文 WebSocket 不能使用 TLS 证书指纹校验");
            emit connectionError(m_loginError);
            return false;
        }

        if (m_webSocket->state() != QAbstractSocket::UnconnectedState) {
            m_webSocket->abort();
        }
        m_webSocketPinAccepted = false;
        if (url.scheme() == QStringLiteral("wss")) {
            QSslConfiguration configuration = m_webSocket->sslConfiguration();
            configuration.setPeerVerifyMode(clientTlsVerifyEnabled()
                ? QSslSocket::VerifyPeer
                : QSslSocket::VerifyNone);
            m_webSocket->setSslConfiguration(configuration);
        }

        QEventLoop waitLoop;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        const QMetaObject::Connection connectedConnection = connect(
            m_webSocket, &QWebSocket::connected, &waitLoop, &QEventLoop::quit);
        const QMetaObject::Connection disconnectedConnection = connect(
            m_webSocket, &QWebSocket::disconnected, &waitLoop, &QEventLoop::quit);
        connect(&timeoutTimer, &QTimer::timeout, &waitLoop, &QEventLoop::quit);

        timeoutTimer.start(5000);
        m_webSocket->open(url);
        waitLoop.exec();
        const bool timedOut = !timeoutTimer.isActive();
        timeoutTimer.stop();
        disconnect(connectedConnection);
        disconnect(disconnectedConnection);

        if (timedOut) {
            m_webSocket->abort();
            m_loginError = QStringLiteral("WebSocket 连接超时: %1").arg(url.toString());
            emit connectionError(m_loginError);
            return false;
        }
        if (!isConnected()) {
            if (m_loginError.isEmpty()) {
                m_loginError = QStringLiteral("WebSocket 连接失败: %1")
                    .arg(m_webSocket->errorString());
            }
            return false;
        }
        return true;
    }

    if (QSslSocket* sslSocket = qobject_cast<QSslSocket*>(m_socket)) {
        m_tcpTlsPinAccepted = false;
        sslSocket->connectToHostEncrypted(host, port);
        const bool encrypted = sslSocket->waitForEncrypted(5000);
        if (!encrypted) {
            if (m_loginError.isEmpty()) {
                m_loginError = "TLS 握手失败: " + sslSocket->errorString();
            }
            return false;
        }
        return m_tcpTlsPinAccepted;
    }
    m_socket->connectToHost(host, port);
    return m_socket->waitForConnected(5000);
}

void Client::disconnectFromServer() {
    m_heartbeatTimer->stop();
    m_tcpTlsPinAccepted = false;
    if (m_webSocket && m_webSocket->state() != QAbstractSocket::UnconnectedState) {
        m_webSocket->close(QWebSocketProtocol::CloseCodeNormal,
                           QStringLiteral("client disconnect"));
    }
    if (m_socket && m_socket->isOpen()) {
        m_socket->disconnectFromHost();
    }
}

void Client::setUserInfo(const QString& userId, const QString& userName) {
    m_userId = userId;
    m_userName = userName;
    loadOrCreateE2ELocalIdentity();
    loadE2ETrustPins();
}

void Client::setAvatarData(const QByteArray& pngData) {
    m_avatarBase64 = avatarBase64FromPngData(pngData);
}

ChatUser Client::friendCandidateForId(const QString& userId, const QString& fallbackName) const {
    const QString normalizedUserId = userId.trimmed();
    ChatUser candidate;
    if (normalizedUserId.isEmpty() || normalizedUserId == m_userId) {
        return candidate;
    }

    for (const ChatUser& user : m_onlineUsers) {
        if (user.id == normalizedUserId) {
            candidate = user;
            if (candidate.name.trimmed().isEmpty()) {
                const QString normalizedName = fallbackName.trimmed();
                candidate.name = normalizedName.isEmpty() ? normalizedUserId : normalizedName;
            }
            candidate.isOnline = user.isOnline;
            return candidate;
        }
    }

    candidate.id = normalizedUserId;
    const QString normalizedName = fallbackName.trimmed();
    candidate.name = normalizedName.isEmpty() ? normalizedUserId : normalizedName;
    candidate.isOnline = false;
    return candidate;
}

void Client::addOrUpdateFriend(const QString& userId, const QString& fallbackName) {
    const ChatUser candidate = friendCandidateForId(userId, fallbackName);
    if (candidate.id.isEmpty()) {
        return;
    }

    for (ChatUser& friendUser : m_friends) {
        if (friendUser.id != candidate.id) {
            continue;
        }
        if (friendUser.name == candidate.name
            && friendUser.avatar == candidate.avatar
            && friendUser.isOnline == candidate.isOnline
            && friendUser.lastActive == candidate.lastActive) {
            return;
        }
        friendUser = candidate;
        emit friendListUpdated(m_friends);
        return;
    }

    m_friends.append(candidate);
    emit friendListUpdated(m_friends);
}

void Client::refreshFriendPresenceFromOnlineUsers() {
    bool changed = false;
    for (ChatUser& friendUser : m_friends) {
        bool foundOnline = false;
        for (const ChatUser& user : m_onlineUsers) {
            if (user.id != friendUser.id) {
                continue;
            }
            foundOnline = true;
            ChatUser updated = user;
            updated.isOnline = user.isOnline;
            if (updated.name.trimmed().isEmpty()) {
                updated.name = friendUser.name;
            }
            if (friendUser.name != updated.name
                || friendUser.avatar != updated.avatar
                || friendUser.isOnline != updated.isOnline
                || friendUser.lastActive != updated.lastActive) {
                friendUser = updated;
                changed = true;
            }
            break;
        }
        if (!foundOnline && friendUser.isOnline) {
            friendUser.isOnline = false;
            changed = true;
        }
    }

    if (changed) {
        emit friendListUpdated(m_friends);
    }
}

bool Client::sendAvatarUpdate(const QByteArray& pngData) {
    setAvatarData(pngData);
    if (!isConnected()) {
        return false;
    }

    QJsonObject obj;
    obj["type"] = QStringLiteral("profile_update");
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["avatar"] = m_avatarBase64;
    return sendJson(obj);
}

void Client::setAccountInfo(const QString& account, const QString& password, bool registerMode) {
    const bool accountChanged = m_account != account;
    m_account = account;
    m_password = password;
    m_registerMode = registerMode;
    m_loginMode = registerMode ? QStringLiteral("register") : QStringLiteral("login");
    m_loginFinished = false;
    m_loginOk = false;
    m_loginWasRegister = false;
    m_hasServerGroupSnapshot = false;
    m_cancelOutgoingTransfer = false;
    m_serverGroups = QJsonArray();
    m_removedServerGroups = QJsonArray();
    if (accountChanged) {
        m_friends.clear();
        m_pendingIncomingFriendNames.clear();
    }
    m_loginError.clear();
}

void Client::setLoginMode(const QString& mode) {
    const QString normalized = mode.trimmed().toLower();
    m_loginMode = normalized == QStringLiteral("reset_password")
        ? normalized
        : (m_registerMode ? QStringLiteral("register") : QStringLiteral("login"));
}

void Client::loadOrCreateE2ELocalIdentity() {
    const QString normalizedUserId = m_userId.trimmed();
    const auto refreshPeerVerificationCodes = [this]() {
        if (m_e2eIdentityPublicKey.isEmpty()) {
            return;
        }
        const QString localFingerprint = e2eFingerprint(m_e2eIdentityPublicKey);
        for (auto it = m_e2ePeerIdentities.begin(); it != m_e2ePeerIdentities.end(); ++it) {
            it->verificationCode = formatE2EVerificationCode(localFingerprint, it->fingerprint);
            applyE2EStoredTrustPin(it.key(), &(*it));
            emit e2eIdentityStateChanged(it.key(), e2ePeerIdentityStatus(it.key()));
        }
    };
    m_e2eIdentityBackendId.clear();
    m_e2eIdentityFingerprint.clear();
    if (!normalizedUserId.isEmpty()) {
        QFile file(e2eIdentityFilePath(normalizedUserId));
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
            const QByteArray privateKey = fromBase64Url(root.value(QStringLiteral("privateKey")).toString());
            const QString storedBackendId = root.value(QStringLiteral("backendId")).toString(QString::fromLatin1(kE2EDraftBackendId)).trimmed();
            const QString storedFingerprint = root.value(QStringLiteral("publicKeyFingerprintSha256")).toString().trimmed().toLower();
            const QByteArray publicKey = e2ePublicKeyFromPrivateKey(privateKey);
            if (!privateKey.isEmpty() && !publicKey.isEmpty()
                && (storedFingerprint.isEmpty() || storedFingerprint == e2eFingerprint(publicKey))) {
                m_e2eIdentityPrivateKey = privateKey;
                m_e2eIdentityPublicKey = publicKey;
                m_e2eIdentityBackendId = storedBackendId.isEmpty() ? QString::fromLatin1(kE2EDraftBackendId) : storedBackendId;
                m_e2eIdentityFingerprint = e2eFingerprint(publicKey);
                refreshPeerVerificationCodes();
                return;
            }
            if (!privateKey.isEmpty() && isValidE2EFingerprint(storedFingerprint)) {
                m_e2eIdentityPrivateKey = privateKey;
                m_e2eIdentityPublicKey.clear();
                m_e2eIdentityBackendId = storedBackendId.isEmpty() ? QString::fromLatin1(kE2EDraftBackendId) : storedBackendId;
                m_e2eIdentityFingerprint = storedFingerprint;
                refreshPeerVerificationCodes();
                return;
            }
        }
    }

    m_e2eIdentityPrivateKey = generateE2EPrivateKey();
    m_e2eIdentityPublicKey = e2ePublicKeyFromPrivateKey(m_e2eIdentityPrivateKey);
    m_e2eIdentityBackendId = m_e2eIdentityPublicKey.isEmpty() ? QString() : e2eCurrentBackendId();
    m_e2eIdentityFingerprint = m_e2eIdentityPublicKey.isEmpty() ? QString() : e2eFingerprint(m_e2eIdentityPublicKey);
    refreshPeerVerificationCodes();
    if (!normalizedUserId.isEmpty()) {
        saveE2ELocalIdentity();
    }
}

bool Client::saveE2ELocalIdentity(QString* rejectReason) const {
    if (rejectReason) rejectReason->clear();
    const QString normalizedUserId = m_userId.trimmed();
    if (normalizedUserId.isEmpty() || m_e2eIdentityPrivateKey.isEmpty() || m_e2eIdentityPublicKey.isEmpty()) {
        if (rejectReason) *rejectReason = QStringLiteral("identity-not-ready");
        return false;
    }
    QJsonObject root;
    root[QStringLiteral("schema")] = QStringLiteral("qtnetworkchat-e2e-identity-v1");
    root[QStringLiteral("userId")] = normalizedUserId;
    const QString identityBackendId = m_e2eIdentityBackendId.trimmed().isEmpty()
        ? e2eCurrentBackendId()
        : m_e2eIdentityBackendId.trimmed();
    root[QStringLiteral("backendId")] = identityBackendId;
    root[QStringLiteral("backendMigrationRequired")] = e2eCurrentBackendId() != identityBackendId;
    root[QStringLiteral("privateKey")] = base64Url(m_e2eIdentityPrivateKey);
    root[QStringLiteral("publicKeyFingerprintSha256")] = e2eFingerprint(m_e2eIdentityPublicKey);
    root[QStringLiteral("updatedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QSaveFile file(e2eIdentityFilePath(normalizedUserId));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (rejectReason) *rejectReason = QStringLiteral("identity-store-open-failed");
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (rejectReason) *rejectReason = QStringLiteral("identity-store-write-failed");
        return false;
    }
    return true;
}

bool Client::waitForLoginResult(int timeoutMs) {
    if (m_loginFinished) return m_loginOk;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(this, &Client::loginSucceeded, &loop, &QEventLoop::quit);
    connect(this, &Client::loginFailed, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    if (!m_loginFinished) {
        m_loginError = "登录超时";
        return false;
    }
    return m_loginOk;
}

void Client::setE2ESessionKey(const QString& peerId, const QString& keyId, const QByteArray& sessionKey) {
    const QString normalizedPeerId = peerId.trimmed();
    const QString normalizedKeyId = keyId.trimmed();
    if (normalizedPeerId.isEmpty() || normalizedKeyId.isEmpty() || sessionKey.size() < 16) {
        return;
    }

    E2ESession session;
    session.keyId = normalizedKeyId;
    session.backendId = e2eCurrentBackendId();
    session.sessionKey = sessionKey;
    session.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    m_e2eSessions[normalizedPeerId] = session;
    emit e2eSessionStateChanged(normalizedPeerId, e2eSessionStatus(normalizedPeerId));
}

void Client::clearE2ESessionKey(const QString& peerId) {
    const QString normalizedPeerId = peerId.trimmed();
    m_e2eSessions.remove(normalizedPeerId);
    emit e2eSessionStateChanged(normalizedPeerId, e2eSessionStatus(normalizedPeerId));
}

bool Client::clearE2EBackendMigrationState(QString* rejectReason) {
    const QJsonObject evidence = executeE2EBackendMigration(rejectReason);
    return evidence.value(QStringLiteral("executed")).toBool(false);
}

QJsonObject Client::planE2EBackendMigration() const {
    QJsonObject plan;
    const QString normalizedUserId = m_userId.trimmed();
    plan[QStringLiteral("schema")] = QStringLiteral("qtnetworkchat-e2e-backend-migration-v1");
    plan[QStringLiteral("mode")] = QStringLiteral("plan");
    plan[QStringLiteral("userConfigured")] = !normalizedUserId.isEmpty();
    plan[QStringLiteral("currentBackendId")] = e2eCurrentBackendId();
    plan[QStringLiteral("localIdentityBackendId")] = m_e2eIdentityBackendId.trimmed();
    plan[QStringLiteral("localIdentityFingerprintSummary")] = shortE2EFingerprint(m_e2eIdentityFingerprint);
    plan[QStringLiteral("identityStorePresent")] = !normalizedUserId.isEmpty()
        && QFile::exists(e2eIdentityFilePath(normalizedUserId));
    plan[QStringLiteral("trustPinStorePresent")] = !normalizedUserId.isEmpty()
        && QFile::exists(e2eTrustPinsFilePath(normalizedUserId));

    const QJsonObject localStatus = e2eLocalIdentityStatus();
    const bool localMigrationRequired = localStatus.value(QStringLiteral("backendMigrationRequired")).toBool(false);
    plan[QStringLiteral("localIdentityMigrationRequired")] = localMigrationRequired;
    plan[QStringLiteral("localIdentityBlockedReason")] = localStatus.value(QStringLiteral("blockedReason")).toString();

    QJsonArray peerPins;
    int pinnedPeerCount = 0;
    int pinnedPeerMigrationCount = 0;
    for (auto it = m_e2ePeerIdentities.constBegin(); it != m_e2ePeerIdentities.constEnd(); ++it) {
        const QJsonObject peerStatus = e2ePeerIdentityStatus(it.key());
        if (!peerStatus.value(QStringLiteral("pinned")).toBool(false)) {
            continue;
        }
        ++pinnedPeerCount;
        if (peerStatus.value(QStringLiteral("backendMigrationRequired")).toBool(false)) {
            ++pinnedPeerMigrationCount;
        }
        QJsonObject peer;
        peer[QStringLiteral("peerId")] = it.key();
        peer[QStringLiteral("trustState")] = peerStatus.value(QStringLiteral("trustState")).toString();
        peer[QStringLiteral("pinBackendId")] = peerStatus.value(QStringLiteral("pinBackendId")).toString();
        peer[QStringLiteral("backendMigrationRequired")] = peerStatus.value(QStringLiteral("backendMigrationRequired")).toBool(false);
        peer[QStringLiteral("blockedReason")] = peerStatus.value(QStringLiteral("blockedReason")).toString();
        peer[QStringLiteral("fingerprintSummary")] =
            shortE2EFingerprint(peerStatus.value(QStringLiteral("publicKeyFingerprintSha256")).toString());
        peerPins.append(peer);
    }

    QJsonArray sessions;
    int sessionMigrationCount = 0;
    for (auto it = m_e2eSessions.constBegin(); it != m_e2eSessions.constEnd(); ++it) {
        const QJsonObject sessionStatus = e2eSessionStatus(it.key());
        if (sessionStatus.value(QStringLiteral("backendMigrationRequired")).toBool(false)) {
            ++sessionMigrationCount;
        }
        QJsonObject session;
        session[QStringLiteral("peerId")] = it.key();
        session[QStringLiteral("backendId")] = sessionStatus.value(QStringLiteral("backendId")).toString();
        session[QStringLiteral("state")] = sessionStatus.value(QStringLiteral("state")).toString();
        session[QStringLiteral("backendMigrationRequired")] = sessionStatus.value(QStringLiteral("backendMigrationRequired")).toBool(false);
        session[QStringLiteral("blockedReason")] = sessionStatus.value(QStringLiteral("blockedReason")).toString();
        session[QStringLiteral("keyFingerprintSummary")] =
            shortE2EFingerprint(sessionStatus.value(QStringLiteral("keyFingerprintSha256")).toString());
        sessions.append(session);
    }

    plan[QStringLiteral("pinnedPeerCount")] = pinnedPeerCount;
    plan[QStringLiteral("pinnedPeerMigrationCount")] = pinnedPeerMigrationCount;
    plan[QStringLiteral("sessionCount")] = m_e2eSessions.size();
    plan[QStringLiteral("sessionMigrationCount")] = sessionMigrationCount;
    plan[QStringLiteral("pendingOutgoingAgreementCount")] = m_e2ePendingOutgoingAgreements.size();
    plan[QStringLiteral("pendingIncomingAgreementCount")] = m_e2ePendingIncomingAgreements.size();
    plan[QStringLiteral("peerPins")] = peerPins;
    plan[QStringLiteral("sessions")] = sessions;

    const bool migrationRequired = localMigrationRequired
        || pinnedPeerMigrationCount > 0
        || sessionMigrationCount > 0;
    plan[QStringLiteral("migrationRequired")] = migrationRequired;
    plan[QStringLiteral("releaseGate")] = migrationRequired
        ? QStringLiteral("manual-e2e-backend-migration-required")
        : QStringLiteral("no-e2e-backend-migration-required");
    plan[QStringLiteral("operatorAction")] = migrationRequired
        ? QStringLiteral("execute-local-e2e-backend-migration-before-production-crypto")
        : QStringLiteral("no-local-e2e-migration-needed");
    return plan;
}

QJsonObject Client::planE2EProductionRotationDryRun() const {
    QJsonObject dryRun;
    const QJsonObject backendStatus = e2eCryptoBackendStatus();
    const QJsonObject migrationPlan = planE2EBackendMigration();
    const QJsonObject providerReadiness =
        backendStatus.value(QStringLiteral("selectedProviderReadiness")).toObject();
    const QJsonObject providerCompatibility =
        backendStatus.value(QStringLiteral("selectedProviderCompatibility")).toObject();
    const QJsonObject productionAcceptance =
        backendStatus.value(QStringLiteral("productionAcceptance")).toObject();
    const QJsonObject productionExecutionPlan =
        backendStatus.value(QStringLiteral("productionOperationExecutionPlan")).toObject();
    const QJsonObject productionInvocation =
        backendStatus.value(QStringLiteral("productionOperationInvocation")).toObject();
    const QJsonObject productionSlots =
        backendStatus.value(QStringLiteral("productionOperationSlots")).toObject();
    const QJsonObject productionDispatchBindings =
        backendStatus.value(QStringLiteral("productionOperationDispatchBindings")).toObject();
    const QJsonObject productionExecutionResult =
        backendStatus.value(QStringLiteral("productionOperationExecutionResult")).toObject();
    const QJsonObject productionProviderTable =
        backendStatus.value(QStringLiteral("productionProviderTable")).toObject();
    const QJsonObject productionProviderTableBindingProbe =
        backendStatus.value(QStringLiteral("productionProviderTableBindingProbe")).toObject();
    const QJsonObject productionProviderTableRegistration =
        backendStatus.value(QStringLiteral("productionProviderTableRegistration")).toObject();
    const QJsonObject productionProviderOperationPreflight =
        backendStatus.value(QStringLiteral("productionProviderOperationPreflight")).toObject();
    const QJsonObject productionProviderCallFrame =
        backendStatus.value(QStringLiteral("productionProviderCallFrame")).toObject();
    const QJsonObject productionProviderInvocationDryRun =
        backendStatus.value(QStringLiteral("productionProviderInvocationDryRun")).toObject();
    const QJsonObject productionProviderInvocationResult =
        backendStatus.value(QStringLiteral("productionProviderInvocationResult")).toObject();
    const QJsonObject productionProviderExecutionDecision =
        backendStatus.value(QStringLiteral("productionProviderExecutionDecision")).toObject();
    const QJsonObject productionProviderCallbackHarness =
        backendStatus.value(QStringLiteral("productionProviderCallbackHarness")).toObject();
    const QJsonObject productionProviderVectorSelfTest =
        backendStatus.value(QStringLiteral("productionProviderVectorSelfTest")).toObject();
    const QJsonObject productionProviderExecutionSlotBinding =
        backendStatus.value(QStringLiteral("productionProviderExecutionSlotBinding")).toObject();
    const QJsonObject productionProviderExecutionPath =
        backendStatus.value(QStringLiteral("productionProviderExecutionPath")).toObject();
    const QJsonObject productionProviderInvocationSandbox =
        backendStatus.value(QStringLiteral("productionProviderInvocationSandbox")).toObject();
    const QJsonObject productionProviderInvocationVectorResult =
        backendStatus.value(QStringLiteral("productionProviderInvocationVectorResult")).toObject();
    const QJsonObject productionProviderInvocationExecution =
        backendStatus.value(QStringLiteral("productionProviderInvocationExecution")).toObject();
    const QJsonObject productionProviderReviewedExecutionCandidate =
        backendStatus.value(QStringLiteral("productionProviderReviewedExecutionCandidate")).toObject();
    const QJsonObject productionProviderReviewedCallHandoff =
        backendStatus.value(QStringLiteral("productionProviderReviewedCallHandoff")).toObject();
    const QJsonObject productionProviderReviewedOperationStubBoundary =
        backendStatus.value(QStringLiteral("productionProviderReviewedOperationStubBoundary")).toObject();
    const QJsonObject productionProviderReviewedCallableTableBridge =
        backendStatus.value(QStringLiteral("productionProviderReviewedCallableTableBridge")).toObject();
    const QJsonObject productionProviderReviewedOperationCallableInterface =
        backendStatus.value(QStringLiteral("productionProviderReviewedOperationCallableInterface")).toObject();
    const QJsonObject productionProviderReviewedCallableRuntimePreflight =
        backendStatus.value(QStringLiteral("productionProviderReviewedCallableRuntimePreflight")).toObject();
    const QJsonObject productionProviderReviewedInvocationArming =
        backendStatus.value(QStringLiteral("productionProviderReviewedInvocationArming")).toObject();
    const QJsonObject productionProviderReviewedInvocationExecutionAcceptance =
        backendStatus.value(QStringLiteral("productionProviderReviewedInvocationExecutionAcceptance")).toObject();
    const bool backendAvailable = backendStatus.value(QStringLiteral("available")).toBool(false);
    const bool productionReady = backendStatus.value(QStringLiteral("productionReady")).toBool(false);
    const bool migrationRequired = migrationPlan.value(QStringLiteral("migrationRequired")).toBool(false);
    const QJsonArray stages =
        e2eProductionRotationStageManifest(backendStatus, providerCompatibility, migrationPlan);
    int blockedStageCount = 0;
    for (const QJsonValue& value : stages) {
        if (value.toObject().value(QStringLiteral("status")).toString() != QStringLiteral("ready")) {
            ++blockedStageCount;
        }
    }
    const bool canRotate = backendAvailable && productionReady && migrationRequired && blockedStageCount == 0;

    dryRun[QStringLiteral("schema")] = QStringLiteral("qtnetworkchat-e2e-production-rotation-dry-run-v1");
    dryRun[QStringLiteral("mode")] = QStringLiteral("dry-run");
    dryRun[QStringLiteral("destructive")] = false;
    dryRun[QStringLiteral("wouldClearLocalIdentityStore")] = false;
    dryRun[QStringLiteral("wouldClearTrustPinStore")] = false;
    dryRun[QStringLiteral("wouldDropActiveSessions")] = false;
    dryRun[QStringLiteral("executed")] = false;
    dryRun[QStringLiteral("userConfigured")] = migrationPlan.value(QStringLiteral("userConfigured")).toBool(false);
    dryRun[QStringLiteral("currentBackendId")] = migrationPlan.value(QStringLiteral("currentBackendId")).toString();
    dryRun[QStringLiteral("requestedBackendId")] =
        backendStatus.value(QStringLiteral("requestedBackendId")).toString();
    dryRun[QStringLiteral("selectedBackendId")] =
        backendStatus.value(QStringLiteral("selectedBackendId")).toString();
    dryRun[QStringLiteral("backendAvailable")] = backendAvailable;
    dryRun[QStringLiteral("productionReady")] = productionReady;
    dryRun[QStringLiteral("providerId")] = providerReadiness.value(QStringLiteral("providerId")).toString();
    dryRun[QStringLiteral("readinessGate")] = providerReadiness.value(QStringLiteral("readinessGate")).toString();
    dryRun[QStringLiteral("compatibilityGate")] = providerCompatibility.value(QStringLiteral("gate")).toString();
    dryRun[QStringLiteral("compatibilityStatus")] = providerCompatibility.value(QStringLiteral("status")).toString();
    dryRun[QStringLiteral("knownAnswerPassed")] =
        providerCompatibility.value(QStringLiteral("knownAnswerPassed")).toBool(false);
    dryRun[QStringLiteral("roundTripPassed")] =
        providerCompatibility.value(QStringLiteral("roundTripPassed")).toBool(false);
    dryRun[QStringLiteral("productionAcceptance")] = productionAcceptance;
    dryRun[QStringLiteral("productionAcceptanceReleaseGate")] =
        productionAcceptance.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionAcceptanceAccepted")] =
        productionAcceptance.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionAcceptanceBlockedOperationCount")] =
        productionAcceptance.value(QStringLiteral("blockedOperationCount")).toInt();
    dryRun[QStringLiteral("productionOperationExecutionPlan")] = productionExecutionPlan;
    dryRun[QStringLiteral("productionExecutionPlanReleaseGate")] =
        productionExecutionPlan.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionExecutionPlanAccepted")] =
        productionExecutionPlan.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionExecutionPlanBlockedStepCount")] =
        productionExecutionPlan.value(QStringLiteral("blockedStepCount")).toInt();
    dryRun[QStringLiteral("productionOperationInvocation")] = productionInvocation;
    dryRun[QStringLiteral("productionInvocationReleaseGate")] =
        productionInvocation.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionInvocationAccepted")] =
        productionInvocation.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionInvocationBlockedOperationCount")] =
        productionInvocation.value(QStringLiteral("blockedOperationCount")).toInt();
    dryRun[QStringLiteral("productionOperationSlots")] = productionSlots;
    dryRun[QStringLiteral("productionSlotsReleaseGate")] =
        productionSlots.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionSlotsAccepted")] =
        productionSlots.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionSlotsBlockedSlotCount")] =
        productionSlots.value(QStringLiteral("blockedSlotCount")).toInt();
    dryRun[QStringLiteral("productionOperationDispatchBindings")] = productionDispatchBindings;
    dryRun[QStringLiteral("productionDispatchBindingsReleaseGate")] =
        productionDispatchBindings.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionDispatchBindingsAccepted")] =
        productionDispatchBindings.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionDispatchBindingsBlockedBindingCount")] =
        productionDispatchBindings.value(QStringLiteral("blockedBindingCount")).toInt();
    dryRun[QStringLiteral("productionOperationExecutionResult")] = productionExecutionResult;
    dryRun[QStringLiteral("productionExecutionResultReleaseGate")] =
        productionExecutionResult.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionExecutionResultAccepted")] =
        productionExecutionResult.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionExecutionResultBlockedResultCount")] =
        productionExecutionResult.value(QStringLiteral("blockedResultCount")).toInt();
    dryRun[QStringLiteral("productionExecutionResultPassedResultCount")] =
        productionExecutionResult.value(QStringLiteral("passedResultCount")).toInt();
    dryRun[QStringLiteral("productionProviderTable")] = productionProviderTable;
    dryRun[QStringLiteral("productionProviderTableReleaseGate")] =
        productionProviderTable.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderTableAccepted")] =
        productionProviderTable.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderTableBoundSymbolCount")] =
        productionProviderTable.value(QStringLiteral("boundSymbolCount")).toInt();
    dryRun[QStringLiteral("productionProviderTableMissingSymbolCount")] =
        productionProviderTable.value(QStringLiteral("missingSymbolCount")).toInt();
    dryRun[QStringLiteral("productionProviderTableBindingProbe")] =
        productionProviderTableBindingProbe;
    dryRun[QStringLiteral("productionProviderTableBindingProbeReleaseGate")] =
        productionProviderTableBindingProbe.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderTableBindingProbeAccepted")] =
        productionProviderTableBindingProbe.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderTableRegistration")] =
        productionProviderTableRegistration;
    dryRun[QStringLiteral("productionProviderTableRegistrationReleaseGate")] =
        productionProviderTableRegistration.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderTableRegistrationAccepted")] =
        productionProviderTableRegistration.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderTableRegistered")] =
        productionProviderTableRegistration.value(QStringLiteral("registered")).toBool(false);
    dryRun[QStringLiteral("productionProviderOperationPreflight")] =
        productionProviderOperationPreflight;
    dryRun[QStringLiteral("productionProviderOperationPreflightReleaseGate")] =
        productionProviderOperationPreflight.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderOperationPreflightAccepted")] =
        productionProviderOperationPreflight.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderOperationPreflightBlockedOperationCount")] =
        productionProviderOperationPreflight.value(QStringLiteral("blockedOperationCount")).toInt();
    dryRun[QStringLiteral("productionProviderCallFrame")] =
        productionProviderCallFrame;
    dryRun[QStringLiteral("productionProviderCallFrameReleaseGate")] =
        productionProviderCallFrame.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderCallFrameAccepted")] =
        productionProviderCallFrame.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderCallFrameBlockedFrameCount")] =
        productionProviderCallFrame.value(QStringLiteral("blockedFrameCount")).toInt();
    dryRun[QStringLiteral("productionProviderInvocationDryRun")] =
        productionProviderInvocationDryRun;
    dryRun[QStringLiteral("productionProviderInvocationDryRunReleaseGate")] =
        productionProviderInvocationDryRun.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderInvocationDryRunAccepted")] =
        productionProviderInvocationDryRun.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderInvocationDryRunBlockedInvocationCount")] =
        productionProviderInvocationDryRun.value(QStringLiteral("blockedInvocationCount")).toInt();
    dryRun[QStringLiteral("productionProviderInvocationResult")] =
        productionProviderInvocationResult;
    dryRun[QStringLiteral("productionProviderInvocationResultReleaseGate")] =
        productionProviderInvocationResult.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderInvocationResultAccepted")] =
        productionProviderInvocationResult.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderInvocationResultBlockedResultCount")] =
        productionProviderInvocationResult.value(QStringLiteral("blockedResultCount")).toInt();
    dryRun[QStringLiteral("productionProviderExecutionDecision")] =
        productionProviderExecutionDecision;
    dryRun[QStringLiteral("productionProviderExecutionDecisionReleaseGate")] =
        productionProviderExecutionDecision.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderExecutionDecisionAccepted")] =
        productionProviderExecutionDecision.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderExecutionDecisionBlockedDecisionCount")] =
        productionProviderExecutionDecision.value(QStringLiteral("blockedDecisionCount")).toInt();
    dryRun[QStringLiteral("productionProviderCallbackHarness")] =
        productionProviderCallbackHarness;
    dryRun[QStringLiteral("productionProviderCallbackHarnessReleaseGate")] =
        productionProviderCallbackHarness.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderCallbackHarnessAccepted")] =
        productionProviderCallbackHarness.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderCallbackHarnessBlockedCallbackCount")] =
        productionProviderCallbackHarness.value(QStringLiteral("blockedCallbackCount")).toInt();
    dryRun[QStringLiteral("productionProviderVectorSelfTest")] =
        productionProviderVectorSelfTest;
    dryRun[QStringLiteral("productionProviderVectorSelfTestReleaseGate")] =
        productionProviderVectorSelfTest.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderVectorSelfTestAccepted")] =
        productionProviderVectorSelfTest.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderVectorSelfTestBlockedVectorCount")] =
        productionProviderVectorSelfTest.value(QStringLiteral("blockedVectorCount")).toInt();
    dryRun[QStringLiteral("productionProviderExecutionSlotBinding")] =
        productionProviderExecutionSlotBinding;
    dryRun[QStringLiteral("productionProviderExecutionSlotBindingReleaseGate")] =
        productionProviderExecutionSlotBinding.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderExecutionSlotBindingAccepted")] =
        productionProviderExecutionSlotBinding.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderExecutionSlotBindingBlockedSlotCount")] =
        productionProviderExecutionSlotBinding.value(QStringLiteral("blockedSlotCount")).toInt();
    dryRun[QStringLiteral("productionProviderExecutionPath")] =
        productionProviderExecutionPath;
    dryRun[QStringLiteral("productionProviderExecutionPathReleaseGate")] =
        productionProviderExecutionPath.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderExecutionPathAccepted")] =
        productionProviderExecutionPath.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderExecutionPathBlockedPathCount")] =
        productionProviderExecutionPath.value(QStringLiteral("blockedPathCount")).toInt();
    dryRun[QStringLiteral("productionProviderInvocationSandbox")] =
        productionProviderInvocationSandbox;
    dryRun[QStringLiteral("productionProviderInvocationSandboxReleaseGate")] =
        productionProviderInvocationSandbox.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderInvocationSandboxAccepted")] =
        productionProviderInvocationSandbox.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderInvocationSandboxBlockedSandboxCount")] =
        productionProviderInvocationSandbox.value(QStringLiteral("blockedSandboxCount")).toInt();
    dryRun[QStringLiteral("productionProviderInvocationVectorResult")] =
        productionProviderInvocationVectorResult;
    dryRun[QStringLiteral("productionProviderInvocationVectorResultReleaseGate")] =
        productionProviderInvocationVectorResult.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderInvocationVectorResultAccepted")] =
        productionProviderInvocationVectorResult.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderInvocationVectorResultBlockedVectorResultCount")] =
        productionProviderInvocationVectorResult.value(QStringLiteral("blockedVectorResultCount")).toInt();
    dryRun[QStringLiteral("productionProviderInvocationExecution")] =
        productionProviderInvocationExecution;
    dryRun[QStringLiteral("productionProviderInvocationExecutionReleaseGate")] =
        productionProviderInvocationExecution.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderInvocationExecutionAccepted")] =
        productionProviderInvocationExecution.value(QStringLiteral("accepted")).toBool(false);
    dryRun[QStringLiteral("productionProviderInvocationExecutionBlockedExecutionCount")] =
        productionProviderInvocationExecution.value(QStringLiteral("blockedExecutionCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedExecutionCandidate")] =
        productionProviderReviewedExecutionCandidate;
    dryRun[QStringLiteral("productionProviderReviewedExecutionCandidateReleaseGate")] =
        productionProviderReviewedExecutionCandidate.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderReviewedExecutionCandidateReadyCount")] =
        productionProviderReviewedExecutionCandidate.value(QStringLiteral("candidateReadyCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedExecutionCandidateBlockedCount")] =
        productionProviderReviewedExecutionCandidate.value(QStringLiteral("blockedCandidateCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedExecutionCandidateNonReleaseGate")] =
        productionProviderReviewedExecutionCandidate.value(QStringLiteral("candidateNonReleaseGate")).toBool(false);
    dryRun[QStringLiteral("productionProviderReviewedCallHandoff")] =
        productionProviderReviewedCallHandoff;
    dryRun[QStringLiteral("productionProviderReviewedCallHandoffReleaseGate")] =
        productionProviderReviewedCallHandoff.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderReviewedCallHandoffReadyCount")] =
        productionProviderReviewedCallHandoff.value(QStringLiteral("readyHandoffCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedCallHandoffBlockedCount")] =
        productionProviderReviewedCallHandoff.value(QStringLiteral("blockedHandoffCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedCallHandoffNonReleaseGate")] =
        productionProviderReviewedCallHandoff.value(QStringLiteral("handoffNonReleaseGate")).toBool(false);
    dryRun[QStringLiteral("productionProviderReviewedOperationStubBoundary")] =
        productionProviderReviewedOperationStubBoundary;
    dryRun[QStringLiteral("productionProviderReviewedOperationStubBoundaryReleaseGate")] =
        productionProviderReviewedOperationStubBoundary.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderReviewedOperationStubBoundaryReadyCount")] =
        productionProviderReviewedOperationStubBoundary.value(QStringLiteral("readyStubCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedOperationStubBoundaryBlockedCount")] =
        productionProviderReviewedOperationStubBoundary.value(QStringLiteral("blockedStubCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedOperationStubBoundaryNonReleaseGate")] =
        productionProviderReviewedOperationStubBoundary.value(QStringLiteral("stubNonReleaseGate")).toBool(false);
    dryRun[QStringLiteral("productionProviderReviewedCallableTableBridge")] =
        productionProviderReviewedCallableTableBridge;
    dryRun[QStringLiteral("productionProviderReviewedCallableTableBridgeReleaseGate")] =
        productionProviderReviewedCallableTableBridge.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderReviewedCallableTableBridgeReadyCount")] =
        productionProviderReviewedCallableTableBridge.value(QStringLiteral("readyBridgeCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedCallableTableBridgeBlockedCount")] =
        productionProviderReviewedCallableTableBridge.value(QStringLiteral("blockedBridgeCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedCallableTableBridgeNonReleaseGate")] =
        productionProviderReviewedCallableTableBridge.value(QStringLiteral("bridgeNonReleaseGate")).toBool(false);
    dryRun[QStringLiteral("productionProviderReviewedOperationCallableInterface")] =
        productionProviderReviewedOperationCallableInterface;
    dryRun[QStringLiteral("productionProviderReviewedOperationCallableInterfaceReleaseGate")] =
        productionProviderReviewedOperationCallableInterface.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderReviewedOperationCallableInterfaceReadyCount")] =
        productionProviderReviewedOperationCallableInterface.value(QStringLiteral("readyInterfaceCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedOperationCallableInterfaceBlockedCount")] =
        productionProviderReviewedOperationCallableInterface.value(QStringLiteral("blockedInterfaceCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedOperationCallableInterfaceNonReleaseGate")] =
        productionProviderReviewedOperationCallableInterface.value(QStringLiteral("interfaceNonReleaseGate")).toBool(false);
    dryRun[QStringLiteral("productionProviderReviewedCallableRuntimePreflight")] =
        productionProviderReviewedCallableRuntimePreflight;
    dryRun[QStringLiteral("productionProviderReviewedCallableRuntimePreflightReleaseGate")] =
        productionProviderReviewedCallableRuntimePreflight.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderReviewedCallableRuntimePreflightReadyCount")] =
        productionProviderReviewedCallableRuntimePreflight.value(QStringLiteral("readyPreflightCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedCallableRuntimePreflightBlockedCount")] =
        productionProviderReviewedCallableRuntimePreflight.value(QStringLiteral("blockedPreflightCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedCallableRuntimePreflightNonReleaseGate")] =
        productionProviderReviewedCallableRuntimePreflight.value(QStringLiteral("runtimePreflightNonReleaseGate")).toBool(false);
    dryRun[QStringLiteral("productionProviderReviewedInvocationArming")] =
        productionProviderReviewedInvocationArming;
    dryRun[QStringLiteral("productionProviderReviewedInvocationArmingReleaseGate")] =
        productionProviderReviewedInvocationArming.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderReviewedInvocationArmingReadyCount")] =
        productionProviderReviewedInvocationArming.value(QStringLiteral("readyArmingCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedInvocationArmingBlockedCount")] =
        productionProviderReviewedInvocationArming.value(QStringLiteral("blockedArmingCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedInvocationArmingNonReleaseGate")] =
        productionProviderReviewedInvocationArming.value(QStringLiteral("invocationArmingNonReleaseGate")).toBool(false);
    dryRun[QStringLiteral("productionProviderReviewedInvocationExecutionAcceptance")] =
        productionProviderReviewedInvocationExecutionAcceptance;
    dryRun[QStringLiteral("productionProviderReviewedInvocationExecutionAcceptanceReleaseGate")] =
        productionProviderReviewedInvocationExecutionAcceptance.value(QStringLiteral("releaseGate")).toString();
    dryRun[QStringLiteral("productionProviderReviewedInvocationExecutionAcceptanceReadyCount")] =
        productionProviderReviewedInvocationExecutionAcceptance.value(QStringLiteral("readyAcceptanceCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedInvocationExecutionAcceptanceBlockedCount")] =
        productionProviderReviewedInvocationExecutionAcceptance.value(QStringLiteral("blockedAcceptanceCount")).toInt();
    dryRun[QStringLiteral("productionProviderReviewedInvocationExecutionAcceptanceNonReleaseGate")] =
        productionProviderReviewedInvocationExecutionAcceptance.value(QStringLiteral("executionAcceptanceNonReleaseGate")).toBool(false);
    dryRun[QStringLiteral("migrationRequired")] = migrationRequired;
    dryRun[QStringLiteral("localIdentityMigrationRequired")] =
        migrationPlan.value(QStringLiteral("localIdentityMigrationRequired")).toBool(false);
    dryRun[QStringLiteral("pinnedPeerMigrationCount")] =
        migrationPlan.value(QStringLiteral("pinnedPeerMigrationCount")).toInt();
    dryRun[QStringLiteral("sessionMigrationCount")] =
        migrationPlan.value(QStringLiteral("sessionMigrationCount")).toInt();
    dryRun[QStringLiteral("pendingOutgoingAgreementCount")] =
        migrationPlan.value(QStringLiteral("pendingOutgoingAgreementCount")).toInt();
    dryRun[QStringLiteral("pendingIncomingAgreementCount")] =
        migrationPlan.value(QStringLiteral("pendingIncomingAgreementCount")).toInt();
    dryRun[QStringLiteral("affectedPeerPins")] = migrationPlan.value(QStringLiteral("peerPins")).toArray();
    dryRun[QStringLiteral("affectedSessions")] = migrationPlan.value(QStringLiteral("sessions")).toArray();
    dryRun[QStringLiteral("rotationStages")] = stages;
    dryRun[QStringLiteral("blockedStageCount")] = blockedStageCount;
    dryRun[QStringLiteral("canRotateInPlace")] = canRotate;
    dryRun[QStringLiteral("releaseGate")] = canRotate
        ? QStringLiteral("can-rotate-e2e-state-to-production-backend")
        : (productionReady
            ? QStringLiteral("no-local-e2e-backend-migration-required")
            : QStringLiteral("production-crypto-provider-not-ready"));
    dryRun[QStringLiteral("blockedReason")] = canRotate
        ? QString()
        : (!productionReady
            ? backendStatus.value(QStringLiteral("unavailableReason")).toString(
                QStringLiteral("production-adapter-not-ready"))
            : (migrationRequired ? QString() : QStringLiteral("migration-not-required")));
    dryRun[QStringLiteral("operatorAction")] = canRotate
        ? QStringLiteral("execute-reviewed-production-key-signature-session-rotation")
        : (!productionReady
            ? QStringLiteral("complete-reviewed-production-provider-before-rotation")
            : QStringLiteral("no-local-e2e-migration-needed"));
    return dryRun;
}

QJsonObject Client::executeE2EProductionRotation(QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    QJsonObject evidence;
    evidence[QStringLiteral("schema")] = QStringLiteral("qtnetworkchat-e2e-production-rotation-v1");
    evidence[QStringLiteral("mode")] = QStringLiteral("execute");
    evidence[QStringLiteral("executed")] = false;
    evidence[QStringLiteral("destructive")] = false;
    evidence[QStringLiteral("wouldClearLocalIdentityStore")] = false;
    evidence[QStringLiteral("wouldClearTrustPinStore")] = false;
    evidence[QStringLiteral("wouldDropActiveSessions")] = false;
    evidence[QStringLiteral("startedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    const QJsonObject dryRun = planE2EProductionRotationDryRun();
    evidence[QStringLiteral("dryRun")] = dryRun;
    evidence[QStringLiteral("releaseGate")] = dryRun.value(QStringLiteral("releaseGate")).toString();
    evidence[QStringLiteral("operatorAction")] = dryRun.value(QStringLiteral("operatorAction")).toString();
    evidence[QStringLiteral("affectedPeerPinCount")] =
        dryRun.value(QStringLiteral("affectedPeerPins")).toArray().size();
    evidence[QStringLiteral("affectedSessionCount")] =
        dryRun.value(QStringLiteral("affectedSessions")).toArray().size();
    evidence[QStringLiteral("pendingOutgoingAgreementCount")] =
        dryRun.value(QStringLiteral("pendingOutgoingAgreementCount")).toInt();
    evidence[QStringLiteral("pendingIncomingAgreementCount")] =
        dryRun.value(QStringLiteral("pendingIncomingAgreementCount")).toInt();
    evidence[QStringLiteral("rotationStages")] = dryRun.value(QStringLiteral("rotationStages")).toArray();
    evidence[QStringLiteral("blockedStageCount")] = dryRun.value(QStringLiteral("blockedStageCount")).toInt();
    evidence[QStringLiteral("productionAcceptance")] =
        dryRun.value(QStringLiteral("productionAcceptance")).toObject();
    evidence[QStringLiteral("productionAcceptanceReleaseGate")] =
        dryRun.value(QStringLiteral("productionAcceptanceReleaseGate")).toString();
    evidence[QStringLiteral("productionAcceptanceAccepted")] =
        dryRun.value(QStringLiteral("productionAcceptanceAccepted")).toBool(false);
    evidence[QStringLiteral("productionOperationExecutionPlan")] =
        dryRun.value(QStringLiteral("productionOperationExecutionPlan")).toObject();
    evidence[QStringLiteral("productionExecutionPlanReleaseGate")] =
        dryRun.value(QStringLiteral("productionExecutionPlanReleaseGate")).toString();
    evidence[QStringLiteral("productionExecutionPlanAccepted")] =
        dryRun.value(QStringLiteral("productionExecutionPlanAccepted")).toBool(false);
    evidence[QStringLiteral("productionExecutionPlanBlockedStepCount")] =
        dryRun.value(QStringLiteral("productionExecutionPlanBlockedStepCount")).toInt();
    evidence[QStringLiteral("productionOperationInvocation")] =
        dryRun.value(QStringLiteral("productionOperationInvocation")).toObject();
    evidence[QStringLiteral("productionInvocationReleaseGate")] =
        dryRun.value(QStringLiteral("productionInvocationReleaseGate")).toString();
    evidence[QStringLiteral("productionInvocationAccepted")] =
        dryRun.value(QStringLiteral("productionInvocationAccepted")).toBool(false);
    evidence[QStringLiteral("productionInvocationBlockedOperationCount")] =
        dryRun.value(QStringLiteral("productionInvocationBlockedOperationCount")).toInt();
    evidence[QStringLiteral("productionOperationSlots")] =
        dryRun.value(QStringLiteral("productionOperationSlots")).toObject();
    evidence[QStringLiteral("productionSlotsReleaseGate")] =
        dryRun.value(QStringLiteral("productionSlotsReleaseGate")).toString();
    evidence[QStringLiteral("productionSlotsAccepted")] =
        dryRun.value(QStringLiteral("productionSlotsAccepted")).toBool(false);
    evidence[QStringLiteral("productionSlotsBlockedSlotCount")] =
        dryRun.value(QStringLiteral("productionSlotsBlockedSlotCount")).toInt();
    evidence[QStringLiteral("productionOperationDispatchBindings")] =
        dryRun.value(QStringLiteral("productionOperationDispatchBindings")).toObject();
    evidence[QStringLiteral("productionDispatchBindingsReleaseGate")] =
        dryRun.value(QStringLiteral("productionDispatchBindingsReleaseGate")).toString();
    evidence[QStringLiteral("productionDispatchBindingsAccepted")] =
        dryRun.value(QStringLiteral("productionDispatchBindingsAccepted")).toBool(false);
    evidence[QStringLiteral("productionDispatchBindingsBlockedBindingCount")] =
        dryRun.value(QStringLiteral("productionDispatchBindingsBlockedBindingCount")).toInt();
    evidence[QStringLiteral("productionOperationExecutionResult")] =
        dryRun.value(QStringLiteral("productionOperationExecutionResult")).toObject();
    evidence[QStringLiteral("productionExecutionResultReleaseGate")] =
        dryRun.value(QStringLiteral("productionExecutionResultReleaseGate")).toString();
    evidence[QStringLiteral("productionExecutionResultAccepted")] =
        dryRun.value(QStringLiteral("productionExecutionResultAccepted")).toBool(false);
    evidence[QStringLiteral("productionExecutionResultBlockedResultCount")] =
        dryRun.value(QStringLiteral("productionExecutionResultBlockedResultCount")).toInt();
    evidence[QStringLiteral("productionExecutionResultPassedResultCount")] =
        dryRun.value(QStringLiteral("productionExecutionResultPassedResultCount")).toInt();
    evidence[QStringLiteral("productionProviderTable")] =
        dryRun.value(QStringLiteral("productionProviderTable")).toObject();
    evidence[QStringLiteral("productionProviderTableReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderTableReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderTableAccepted")] =
        dryRun.value(QStringLiteral("productionProviderTableAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderTableBoundSymbolCount")] =
        dryRun.value(QStringLiteral("productionProviderTableBoundSymbolCount")).toInt();
    evidence[QStringLiteral("productionProviderTableMissingSymbolCount")] =
        dryRun.value(QStringLiteral("productionProviderTableMissingSymbolCount")).toInt();
    evidence[QStringLiteral("productionProviderTableBindingProbe")] =
        dryRun.value(QStringLiteral("productionProviderTableBindingProbe")).toObject();
    evidence[QStringLiteral("productionProviderTableBindingProbeReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderTableBindingProbeReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderTableBindingProbeAccepted")] =
        dryRun.value(QStringLiteral("productionProviderTableBindingProbeAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderTableRegistration")] =
        dryRun.value(QStringLiteral("productionProviderTableRegistration")).toObject();
    evidence[QStringLiteral("productionProviderTableRegistrationReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderTableRegistrationReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderTableRegistrationAccepted")] =
        dryRun.value(QStringLiteral("productionProviderTableRegistrationAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderTableRegistered")] =
        dryRun.value(QStringLiteral("productionProviderTableRegistered")).toBool(false);
    evidence[QStringLiteral("productionProviderOperationPreflight")] =
        dryRun.value(QStringLiteral("productionProviderOperationPreflight")).toObject();
    evidence[QStringLiteral("productionProviderOperationPreflightReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderOperationPreflightReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderOperationPreflightAccepted")] =
        dryRun.value(QStringLiteral("productionProviderOperationPreflightAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderOperationPreflightBlockedOperationCount")] =
        dryRun.value(QStringLiteral("productionProviderOperationPreflightBlockedOperationCount")).toInt();
    evidence[QStringLiteral("productionProviderCallFrame")] =
        dryRun.value(QStringLiteral("productionProviderCallFrame")).toObject();
    evidence[QStringLiteral("productionProviderCallFrameReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderCallFrameReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderCallFrameAccepted")] =
        dryRun.value(QStringLiteral("productionProviderCallFrameAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderCallFrameBlockedFrameCount")] =
        dryRun.value(QStringLiteral("productionProviderCallFrameBlockedFrameCount")).toInt();
    evidence[QStringLiteral("productionProviderInvocationDryRun")] =
        dryRun.value(QStringLiteral("productionProviderInvocationDryRun")).toObject();
    evidence[QStringLiteral("productionProviderInvocationDryRunReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderInvocationDryRunReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderInvocationDryRunAccepted")] =
        dryRun.value(QStringLiteral("productionProviderInvocationDryRunAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderInvocationDryRunBlockedInvocationCount")] =
        dryRun.value(QStringLiteral("productionProviderInvocationDryRunBlockedInvocationCount")).toInt();
    evidence[QStringLiteral("productionProviderInvocationResult")] =
        dryRun.value(QStringLiteral("productionProviderInvocationResult")).toObject();
    evidence[QStringLiteral("productionProviderInvocationResultReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderInvocationResultReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderInvocationResultAccepted")] =
        dryRun.value(QStringLiteral("productionProviderInvocationResultAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderInvocationResultBlockedResultCount")] =
        dryRun.value(QStringLiteral("productionProviderInvocationResultBlockedResultCount")).toInt();
    evidence[QStringLiteral("productionProviderExecutionDecision")] =
        dryRun.value(QStringLiteral("productionProviderExecutionDecision")).toObject();
    evidence[QStringLiteral("productionProviderExecutionDecisionReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderExecutionDecisionReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderExecutionDecisionAccepted")] =
        dryRun.value(QStringLiteral("productionProviderExecutionDecisionAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderExecutionDecisionBlockedDecisionCount")] =
        dryRun.value(QStringLiteral("productionProviderExecutionDecisionBlockedDecisionCount")).toInt();
    evidence[QStringLiteral("productionProviderCallbackHarness")] =
        dryRun.value(QStringLiteral("productionProviderCallbackHarness")).toObject();
    evidence[QStringLiteral("productionProviderCallbackHarnessReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderCallbackHarnessReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderCallbackHarnessAccepted")] =
        dryRun.value(QStringLiteral("productionProviderCallbackHarnessAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderCallbackHarnessBlockedCallbackCount")] =
        dryRun.value(QStringLiteral("productionProviderCallbackHarnessBlockedCallbackCount")).toInt();
    evidence[QStringLiteral("productionProviderVectorSelfTest")] =
        dryRun.value(QStringLiteral("productionProviderVectorSelfTest")).toObject();
    evidence[QStringLiteral("productionProviderVectorSelfTestReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderVectorSelfTestReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderVectorSelfTestAccepted")] =
        dryRun.value(QStringLiteral("productionProviderVectorSelfTestAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderVectorSelfTestBlockedVectorCount")] =
        dryRun.value(QStringLiteral("productionProviderVectorSelfTestBlockedVectorCount")).toInt();
    evidence[QStringLiteral("productionProviderExecutionSlotBinding")] =
        dryRun.value(QStringLiteral("productionProviderExecutionSlotBinding")).toObject();
    evidence[QStringLiteral("productionProviderExecutionSlotBindingReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderExecutionSlotBindingReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderExecutionSlotBindingAccepted")] =
        dryRun.value(QStringLiteral("productionProviderExecutionSlotBindingAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderExecutionSlotBindingBlockedSlotCount")] =
        dryRun.value(QStringLiteral("productionProviderExecutionSlotBindingBlockedSlotCount")).toInt();
    evidence[QStringLiteral("productionProviderExecutionPath")] =
        dryRun.value(QStringLiteral("productionProviderExecutionPath")).toObject();
    evidence[QStringLiteral("productionProviderExecutionPathReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderExecutionPathReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderExecutionPathAccepted")] =
        dryRun.value(QStringLiteral("productionProviderExecutionPathAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderExecutionPathBlockedPathCount")] =
        dryRun.value(QStringLiteral("productionProviderExecutionPathBlockedPathCount")).toInt();
    evidence[QStringLiteral("productionProviderInvocationSandbox")] =
        dryRun.value(QStringLiteral("productionProviderInvocationSandbox")).toObject();
    evidence[QStringLiteral("productionProviderInvocationSandboxReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderInvocationSandboxReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderInvocationSandboxAccepted")] =
        dryRun.value(QStringLiteral("productionProviderInvocationSandboxAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderInvocationSandboxBlockedSandboxCount")] =
        dryRun.value(QStringLiteral("productionProviderInvocationSandboxBlockedSandboxCount")).toInt();
    evidence[QStringLiteral("productionProviderInvocationVectorResult")] =
        dryRun.value(QStringLiteral("productionProviderInvocationVectorResult")).toObject();
    evidence[QStringLiteral("productionProviderInvocationVectorResultReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderInvocationVectorResultReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderInvocationVectorResultAccepted")] =
        dryRun.value(QStringLiteral("productionProviderInvocationVectorResultAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderInvocationVectorResultBlockedVectorResultCount")] =
        dryRun.value(QStringLiteral("productionProviderInvocationVectorResultBlockedVectorResultCount")).toInt();
    evidence[QStringLiteral("productionProviderInvocationExecution")] =
        dryRun.value(QStringLiteral("productionProviderInvocationExecution")).toObject();
    evidence[QStringLiteral("productionProviderInvocationExecutionReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderInvocationExecutionReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderInvocationExecutionAccepted")] =
        dryRun.value(QStringLiteral("productionProviderInvocationExecutionAccepted")).toBool(false);
    evidence[QStringLiteral("productionProviderInvocationExecutionBlockedExecutionCount")] =
        dryRun.value(QStringLiteral("productionProviderInvocationExecutionBlockedExecutionCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedExecutionCandidate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedExecutionCandidate")).toObject();
    evidence[QStringLiteral("productionProviderReviewedExecutionCandidateReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedExecutionCandidateReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderReviewedExecutionCandidateReadyCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedExecutionCandidateReadyCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedExecutionCandidateBlockedCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedExecutionCandidateBlockedCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedExecutionCandidateNonReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedExecutionCandidateNonReleaseGate")).toBool(false);
    evidence[QStringLiteral("productionProviderReviewedCallHandoff")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallHandoff")).toObject();
    evidence[QStringLiteral("productionProviderReviewedCallHandoffReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallHandoffReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderReviewedCallHandoffReadyCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallHandoffReadyCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedCallHandoffBlockedCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallHandoffBlockedCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedCallHandoffNonReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallHandoffNonReleaseGate")).toBool(false);
    evidence[QStringLiteral("productionProviderReviewedOperationStubBoundary")] =
        dryRun.value(QStringLiteral("productionProviderReviewedOperationStubBoundary")).toObject();
    evidence[QStringLiteral("productionProviderReviewedOperationStubBoundaryReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedOperationStubBoundaryReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderReviewedOperationStubBoundaryReadyCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedOperationStubBoundaryReadyCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedOperationStubBoundaryBlockedCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedOperationStubBoundaryBlockedCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedOperationStubBoundaryNonReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedOperationStubBoundaryNonReleaseGate")).toBool(false);
    evidence[QStringLiteral("productionProviderReviewedCallableTableBridge")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallableTableBridge")).toObject();
    evidence[QStringLiteral("productionProviderReviewedCallableTableBridgeReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallableTableBridgeReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderReviewedCallableTableBridgeReadyCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallableTableBridgeReadyCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedCallableTableBridgeBlockedCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallableTableBridgeBlockedCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedCallableTableBridgeNonReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallableTableBridgeNonReleaseGate")).toBool(false);
    evidence[QStringLiteral("productionProviderReviewedOperationCallableInterface")] =
        dryRun.value(QStringLiteral("productionProviderReviewedOperationCallableInterface")).toObject();
    evidence[QStringLiteral("productionProviderReviewedOperationCallableInterfaceReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedOperationCallableInterfaceReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderReviewedOperationCallableInterfaceReadyCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedOperationCallableInterfaceReadyCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedOperationCallableInterfaceBlockedCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedOperationCallableInterfaceBlockedCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedOperationCallableInterfaceNonReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedOperationCallableInterfaceNonReleaseGate")).toBool(false);
    evidence[QStringLiteral("productionProviderReviewedCallableRuntimePreflight")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallableRuntimePreflight")).toObject();
    evidence[QStringLiteral("productionProviderReviewedCallableRuntimePreflightReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallableRuntimePreflightReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderReviewedCallableRuntimePreflightReadyCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallableRuntimePreflightReadyCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedCallableRuntimePreflightBlockedCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallableRuntimePreflightBlockedCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedCallableRuntimePreflightNonReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedCallableRuntimePreflightNonReleaseGate")).toBool(false);
    evidence[QStringLiteral("productionProviderReviewedInvocationArming")] =
        dryRun.value(QStringLiteral("productionProviderReviewedInvocationArming")).toObject();
    evidence[QStringLiteral("productionProviderReviewedInvocationArmingReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedInvocationArmingReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderReviewedInvocationArmingReadyCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedInvocationArmingReadyCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedInvocationArmingBlockedCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedInvocationArmingBlockedCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedInvocationArmingNonReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedInvocationArmingNonReleaseGate")).toBool(false);
    evidence[QStringLiteral("productionProviderReviewedInvocationExecutionAcceptance")] =
        dryRun.value(QStringLiteral("productionProviderReviewedInvocationExecutionAcceptance")).toObject();
    evidence[QStringLiteral("productionProviderReviewedInvocationExecutionAcceptanceReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedInvocationExecutionAcceptanceReleaseGate")).toString();
    evidence[QStringLiteral("productionProviderReviewedInvocationExecutionAcceptanceReadyCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedInvocationExecutionAcceptanceReadyCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedInvocationExecutionAcceptanceBlockedCount")] =
        dryRun.value(QStringLiteral("productionProviderReviewedInvocationExecutionAcceptanceBlockedCount")).toInt();
    evidence[QStringLiteral("productionProviderReviewedInvocationExecutionAcceptanceNonReleaseGate")] =
        dryRun.value(QStringLiteral("productionProviderReviewedInvocationExecutionAcceptanceNonReleaseGate")).toBool(false);

    if (!dryRun.value(QStringLiteral("canRotateInPlace")).toBool(false)) {
        const QString blockedReason = dryRun.value(QStringLiteral("blockedReason")).toString(
            QStringLiteral("production-rotation-not-ready"));
        if (rejectReason) *rejectReason = blockedReason;
        evidence[QStringLiteral("blockedReason")] = blockedReason;
        evidence[QStringLiteral("completedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        return evidence;
    }

    const QString currentBackend = e2eCurrentBackendId();
    const QByteArray previousPrivateKey = m_e2eIdentityPrivateKey;
    const QByteArray previousPublicKey = m_e2eIdentityPublicKey;
    const QString previousBackendId = m_e2eIdentityBackendId;
    const QString previousIdentityFingerprint =
        !m_e2eIdentityFingerprint.trimmed().isEmpty()
            ? m_e2eIdentityFingerprint.trimmed().toLower()
            : (previousPublicKey.isEmpty() ? QString() : e2eFingerprint(previousPublicKey));
    const QStringList affectedSessionPeers = m_e2eSessions.keys();
    const QStringList affectedPeerIds = m_e2ePeerIdentities.keys();
    const int previousStoredPinCount = m_e2eStoredTrustPins.size();
    const int previousPendingOutgoingCount = m_e2ePendingOutgoingAgreements.size();
    const int previousPendingIncomingCount = m_e2ePendingIncomingAgreements.size();

    const QByteArray rotatedPrivateKey = generateE2EPrivateKey();
    const QByteArray rotatedPublicKey = e2ePublicKeyFromPrivateKey(rotatedPrivateKey);
    if (rotatedPrivateKey.isEmpty() || rotatedPublicKey.isEmpty()) {
        if (rejectReason) *rejectReason = QStringLiteral("production-identity-generation-failed");
        evidence[QStringLiteral("blockedReason")] =
            QStringLiteral("production-identity-generation-failed");
        evidence[QStringLiteral("releaseGate")] =
            QStringLiteral("production-rotation-identity-generation-blocked");
        evidence[QStringLiteral("operatorAction")] =
            QStringLiteral("retry-production-rotation-after-provider-identity-generation-recovers");
        evidence[QStringLiteral("completedAt")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        return evidence;
    }

    m_e2eIdentityPrivateKey = rotatedPrivateKey;
    m_e2eIdentityPublicKey = rotatedPublicKey;
    m_e2eIdentityBackendId = currentBackend;
    m_e2eIdentityFingerprint = e2eFingerprint(rotatedPublicKey);
    QString saveReason;
    if (!saveE2ELocalIdentity(&saveReason)) {
        m_e2eIdentityPrivateKey = previousPrivateKey;
        m_e2eIdentityPublicKey = previousPublicKey;
        m_e2eIdentityBackendId = previousBackendId;
        m_e2eIdentityFingerprint = previousIdentityFingerprint;
        if (rejectReason) *rejectReason = saveReason.isEmpty()
            ? QStringLiteral("identity-store-write-failed")
            : saveReason;
        evidence[QStringLiteral("blockedReason")] = rejectReason ? *rejectReason : saveReason;
        evidence[QStringLiteral("releaseGate")] =
            QStringLiteral("production-rotation-identity-store-blocked");
        evidence[QStringLiteral("operatorAction")] =
            QStringLiteral("fix-local-identity-store-before-production-rotation");
        evidence[QStringLiteral("completedAt")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        return evidence;
    }

    m_e2eStoredTrustPins.clear();
    m_e2ePendingOutgoingAgreements.clear();
    m_e2ePendingIncomingAgreements.clear();
    m_e2eSessions.clear();

    for (auto it = m_e2ePeerIdentities.begin(); it != m_e2ePeerIdentities.end(); ++it) {
        it->pinned = false;
        it->pinnedFingerprint.clear();
        it->verified = false;
        it->verifiedAtMs = 0;
        it->fingerprintMismatch = false;
        it->verificationCode = formatE2EVerificationCode(m_e2eIdentityFingerprint, it->fingerprint);
        emit e2eIdentityStateChanged(it.key(), e2ePeerIdentityStatus(it.key()));
    }
    for (const QString& peerId : affectedSessionPeers) {
        emit e2eSessionStateChanged(peerId, e2eSessionStatus(peerId));
    }
    QString trustPinSaveReason;
    const bool trustPinRebindPersisted = saveE2ETrustPins(&trustPinSaveReason);

    evidence[QStringLiteral("executed")] = true;
    evidence[QStringLiteral("executedStage")] =
        QStringLiteral("local-production-identity-rebind");
    evidence[QStringLiteral("releaseGate")] =
        QStringLiteral("production-rotation-local-state-rebound");
    evidence[QStringLiteral("operatorAction")] =
        QStringLiteral("announce-production-identity-and-repin-peers-before-new-sessions");
    evidence[QStringLiteral("blockedReason")] = QString();
    evidence[QStringLiteral("rotatedBackendId")] = currentBackend;
    evidence[QStringLiteral("previousIdentityFingerprintSummary")] =
        shortE2EFingerprint(previousIdentityFingerprint);
    evidence[QStringLiteral("rotatedIdentityFingerprintSummary")] =
        shortE2EFingerprint(m_e2eIdentityFingerprint);
    evidence[QStringLiteral("identityFingerprintChanged")] =
        isValidE2EFingerprint(previousIdentityFingerprint)
        && previousIdentityFingerprint != m_e2eIdentityFingerprint;
    evidence[QStringLiteral("rotatedIdentityPersisted")] =
        !m_userId.trimmed().isEmpty() && QFile::exists(e2eIdentityFilePath(m_userId));
    evidence[QStringLiteral("rebindingRequired")] = true;
    evidence[QStringLiteral("trustPinsClearedForRebind")] = previousStoredPinCount;
    evidence[QStringLiteral("trustPinRebindPersisted")] = trustPinRebindPersisted;
    if (!trustPinRebindPersisted) {
        evidence[QStringLiteral("trustPinRebindPersistReason")] =
            trustPinSaveReason.isEmpty()
                ? QStringLiteral("pin-store-write-failed")
                : trustPinSaveReason;
    }
    evidence[QStringLiteral("clearedSessionCount")] = affectedSessionPeers.size();
    evidence[QStringLiteral("sessionRotationCompleted")] = false;
    evidence[QStringLiteral("requiresPeerReverification")] = true;
    evidence[QStringLiteral("requiresNewSessionAgreement")] = true;
    evidence[QStringLiteral("clearedPendingOutgoingAgreementCount")] =
        previousPendingOutgoingCount;
    evidence[QStringLiteral("clearedPendingIncomingAgreementCount")] =
        previousPendingIncomingCount;
    evidence[QStringLiteral("peerIdentityReverificationCount")] = affectedPeerIds.size();
    evidence[QStringLiteral("newSessionCount")] = m_e2eSessions.size();
    evidence[QStringLiteral("privateMaterialExported")] = false;
    evidence[QStringLiteral("rawKeyExported")] = false;
    evidence[QStringLiteral("sessionSecretExported")] = false;
    evidence[QStringLiteral("publicKeyExported")] = false;
    evidence[QStringLiteral("completedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    return evidence;
}

QJsonObject Client::executeE2EBackendMigration(QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    QJsonObject evidence;
    evidence[QStringLiteral("schema")] = QStringLiteral("qtnetworkchat-e2e-backend-migration-v1");
    evidence[QStringLiteral("mode")] = QStringLiteral("execute");
    evidence[QStringLiteral("executed")] = false;
    evidence[QStringLiteral("startedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    const QString normalizedUserId = m_userId.trimmed();
    if (normalizedUserId.isEmpty()) {
        if (rejectReason) *rejectReason = QStringLiteral("identity-not-ready");
        evidence[QStringLiteral("reason")] = QStringLiteral("identity-not-ready");
        return evidence;
    }

    const QJsonObject beforePlan = planE2EBackendMigration();
    evidence[QStringLiteral("before")] = beforePlan;
    if (!beforePlan.value(QStringLiteral("migrationRequired")).toBool(false)) {
        if (rejectReason) *rejectReason = QStringLiteral("migration-not-required");
        evidence[QStringLiteral("reason")] = QStringLiteral("migration-not-required");
        return evidence;
    }

    const QString identityPath = e2eIdentityFilePath(normalizedUserId);
    if (QFile::exists(identityPath) && !QFile::remove(identityPath)) {
        if (rejectReason) *rejectReason = QStringLiteral("identity-store-remove-failed");
        evidence[QStringLiteral("reason")] = QStringLiteral("identity-store-remove-failed");
        return evidence;
    }
    const QString pinsPath = e2eTrustPinsFilePath(normalizedUserId);
    if (QFile::exists(pinsPath) && !QFile::remove(pinsPath)) {
        if (rejectReason) *rejectReason = QStringLiteral("pin-store-remove-failed");
        evidence[QStringLiteral("reason")] = QStringLiteral("pin-store-remove-failed");
        return evidence;
    }

    const QStringList sessionPeers = m_e2eSessions.keys();
    const QStringList identityPeers = m_e2ePeerIdentities.keys();
    m_e2eIdentityPrivateKey.clear();
    m_e2eIdentityPublicKey.clear();
    m_e2eIdentityBackendId.clear();
    m_e2eIdentityFingerprint.clear();
    m_e2eStoredTrustPins.clear();
    m_e2ePendingOutgoingAgreements.clear();
    m_e2ePendingIncomingAgreements.clear();
    m_e2eSessions.clear();
    for (auto it = m_e2ePeerIdentities.begin(); it != m_e2ePeerIdentities.end(); ++it) {
        it->pinned = false;
        it->pinnedFingerprint.clear();
        it->verified = false;
        it->verifiedAtMs = 0;
        it->fingerprintMismatch = false;
        emit e2eIdentityStateChanged(it.key(), e2ePeerIdentityStatus(it.key()));
    }
    for (const QString& peerId : sessionPeers) {
        emit e2eSessionStateChanged(peerId, e2eSessionStatus(peerId));
    }
    evidence[QStringLiteral("executed")] = true;
    evidence[QStringLiteral("completedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    evidence[QStringLiteral("clearedIdentityStore")] = true;
    evidence[QStringLiteral("clearedTrustPinStore")] = true;
    evidence[QStringLiteral("clearedSessionCount")] = sessionPeers.size();
    evidence[QStringLiteral("clearedPeerTrustCount")] = identityPeers.size();
    evidence[QStringLiteral("clearedPendingOutgoingAgreementCount")] =
        beforePlan.value(QStringLiteral("pendingOutgoingAgreementCount")).toInt();
    evidence[QStringLiteral("clearedPendingIncomingAgreementCount")] =
        beforePlan.value(QStringLiteral("pendingIncomingAgreementCount")).toInt();
    evidence[QStringLiteral("after")] = planE2EBackendMigration();
    return evidence;
}

bool Client::hasE2ESession(const QString& peerId) const {
    return m_e2eSessions.contains(peerId.trimmed());
}

bool Client::e2eSessionNeedsRotation(const QString& peerId) const {
    const auto it = m_e2eSessions.constFind(peerId.trimmed());
    return it != m_e2eSessions.constEnd() && it->rotationRequired;
}

bool Client::e2ePeerIdentityTrusted(const QString& peerId) const {
    QString reason;
    return requireTrustedE2EPeer(peerId, &reason);
}

QJsonObject Client::e2eSessionStatus(const QString& peerId) const {
    const QString normalizedPeerId = peerId.trimmed();
    QJsonObject status;
    status["peerId"] = normalizedPeerId;
    status["configured"] = false;
    status["ready"] = false;
    status["rotationRequired"] = false;
    status["messageLimit"] = m_e2eSessionMessageLimit;
    status["cryptoBackend"] = e2eCryptoBackendStatus();
    status["backendMigrationRequired"] = false;
    const auto it = m_e2eSessions.constFind(normalizedPeerId);
    if (it == m_e2eSessions.constEnd()) {
        status["state"] = QStringLiteral("missing-session");
        return status;
    }

    status["configured"] = true;
    const QString sessionBackendId = it->backendId.trimmed().isEmpty()
        ? QString::fromLatin1(kE2EDraftBackendId)
        : it->backendId.trimmed();
    QString backendRejectReason;
    const bool backendUsable = e2eBackendUsableForLocalMaterial(sessionBackendId, &backendRejectReason);
    status["backendId"] = sessionBackendId;
    status["backendMigrationRequired"] = sessionBackendId != e2eCurrentBackendId();
    status["backendUsable"] = backendUsable;
    if (!backendRejectReason.isEmpty()) {
        status["blockedReason"] = backendRejectReason;
    }
    status["ready"] = backendUsable && !it->rotationRequired;
    status["state"] = !backendUsable
        ? (sessionBackendId != e2eCurrentBackendId()
            ? QStringLiteral("backend-migration-required")
            : QStringLiteral("backend-unavailable"))
        : (it->rotationRequired ? QStringLiteral("rotation-required") : QStringLiteral("ready"));
    status["keyId"] = it->keyId;
    status["keyFingerprintSha256"] = e2eFingerprint(it->sessionKey);
    status["createdAt"] = QDateTime::fromMSecsSinceEpoch(it->createdAtMs).toUTC().toString(Qt::ISODateWithMs);
    status["encryptedMessages"] = QString::number(it->encryptedMessages);
    status["decryptedMessages"] = QString::number(it->decryptedMessages);
    status["rotationRequired"] = it->rotationRequired;
    return status;
}

QJsonObject Client::e2eLocalIdentityStatus() const {
    QJsonObject status = e2eIdentityJson(m_userId, m_e2eIdentityPublicKey);
    QString identityRejectReason;
    const bool identityUsable = e2eLocalIdentityUsable(&identityRejectReason);
    const QString identityBackendId = m_e2eIdentityBackendId.trimmed().isEmpty()
        ? (m_e2eIdentityPublicKey.isEmpty() ? QString() : e2eCryptoBackendId())
        : m_e2eIdentityBackendId.trimmed();
    const QString identityFingerprint = !m_e2eIdentityFingerprint.trimmed().isEmpty()
        ? m_e2eIdentityFingerprint.trimmed().toLower()
        : (m_e2eIdentityPublicKey.isEmpty() ? QString() : e2eFingerprint(m_e2eIdentityPublicKey));
    status["configured"] = !m_userId.trimmed().isEmpty()
        && (!m_e2eIdentityPublicKey.isEmpty() || isValidE2EFingerprint(identityFingerprint));
    status["trusted"] = true;
    status["trustState"] = QStringLiteral("local");
    if (isValidE2EFingerprint(identityFingerprint)) {
        status["publicKeyFingerprintSha256"] = identityFingerprint;
    }
    status["backendId"] = identityBackendId;
    status["backendUsable"] = identityUsable;
    status["backendMigrationRequired"] = !identityBackendId.isEmpty()
        && identityBackendId != e2eCurrentBackendId();
    if (!identityRejectReason.isEmpty()) {
        status["blockedReason"] = identityRejectReason;
    }
    status["agreementSigning"] = !m_e2eIdentityPrivateKey.isEmpty()
        && !m_e2eIdentityPublicKey.isEmpty()
        && identityUsable;
    status["signatureSuite"] = e2eAgreementSignatureSuite();
    status["cryptoBackend"] = e2eCryptoBackendStatus();
    status["identityPersisted"] = !m_userId.trimmed().isEmpty()
        && QFile::exists(e2eIdentityFilePath(m_userId));
    return status;
}

QJsonObject Client::e2ePeerIdentityStatus(const QString& peerId) const {
    const QString normalizedPeerId = peerId.trimmed();
    QJsonObject status;
    status["peerId"] = normalizedPeerId;
    status["configured"] = false;
    status["trusted"] = false;
    status["pinned"] = false;
    status["fingerprintMismatch"] = false;
    status["trustState"] = QStringLiteral("unknown");
    const auto it = m_e2ePeerIdentities.constFind(normalizedPeerId);
    if (it == m_e2ePeerIdentities.constEnd()) {
        return status;
    }

    status["configured"] = true;
    status["publicKeyFingerprintSha256"] = it->fingerprint;
    status["pinned"] = it->pinned;
    status["pinnedFingerprintSha256"] = it->pinnedFingerprint;
    const auto storedPin = m_e2eStoredTrustPins.constFind(normalizedPeerId);
    status["pinPersisted"] = it->pinned
        && storedPin != m_e2eStoredTrustPins.constEnd()
        && storedPin->fingerprint == it->pinnedFingerprint;
    status["fingerprintMismatch"] = it->fingerprintMismatch;
    status["verified"] = it->verified && !it->fingerprintMismatch;
    status["verificationCode"] = it->verificationCode;
    status["verificationCodeDisplay"] = it->verificationCode;
    if (it->verifiedAtMs > 0) {
        status["verifiedAt"] = QDateTime::fromMSecsSinceEpoch(it->verifiedAtMs).toUTC().toString(Qt::ISODateWithMs);
    }
    status["trusted"] = it->pinned && it->verified && !it->fingerprintMismatch;
    status["agreementSignatureVerified"] = it->pinned && it->verified && !it->fingerprintMismatch && !it->publicKey.isEmpty();
    status["signatureSuite"] = e2eAgreementSignatureSuite();
    status["cryptoBackend"] = e2eCryptoBackendStatus();
    const QString pinBackendId = storedPin == m_e2eStoredTrustPins.constEnd() || storedPin->backendId.trimmed().isEmpty()
        ? QString::fromLatin1(kE2EDraftBackendId)
        : storedPin->backendId.trimmed();
    status["pinBackendId"] = it->pinned ? pinBackendId : QString();
    QString pinBackendRejectReason;
    const bool pinBackendUsable = !it->pinned || e2eBackendUsableForLocalMaterial(pinBackendId, &pinBackendRejectReason);
    status["backendUsable"] = pinBackendUsable;
    status["backendMigrationRequired"] = it->pinned && pinBackendId != e2eCurrentBackendId();
    if (!pinBackendRejectReason.isEmpty()) {
        status["blockedReason"] = pinBackendRejectReason;
    }
    status["trustState"] = it->fingerprintMismatch
        ? QStringLiteral("mismatch")
        : (it->pinned && it->verified ? QStringLiteral("trusted")
            : (it->pinned ? QStringLiteral("pending-verification") : QStringLiteral("unverified")));
    status["firstSeenAt"] = QDateTime::fromMSecsSinceEpoch(it->firstSeenAtMs).toUTC().toString(Qt::ISODateWithMs);
    status["lastSeenAt"] = QDateTime::fromMSecsSinceEpoch(it->lastSeenAtMs).toUTC().toString(Qt::ISODateWithMs);
    return status;
}

void Client::setE2ESessionMessageLimitForTesting(int limit) {
    m_e2eSessionMessageLimit = qBound(1, limit, 1000000);
}

bool Client::announceE2EIdentity(const QString& peerId, QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    if (!isConnected()) {
        if (rejectReason) *rejectReason = QStringLiteral("not-connected");
        return false;
    }
    if (!m_userId.trimmed().isEmpty()
        && (m_e2eIdentityPrivateKey.isEmpty() || m_e2eIdentityPublicKey.isEmpty())
        && e2eCryptoBackendStatus().value("available").toBool(false)) {
        loadOrCreateE2ELocalIdentity();
    }
    if (m_userId.trimmed().isEmpty() || m_e2eIdentityPublicKey.isEmpty()) {
        if (rejectReason) *rejectReason = QStringLiteral("identity-not-ready");
        return false;
    }
    if (!e2eCryptoBackendStatus().value("available").toBool(false)) {
        if (rejectReason) *rejectReason = QStringLiteral("production-crypto-backend-unavailable");
        return false;
    }

    const QString normalizedPeerId = peerId.trimmed();
    if (normalizedPeerId == m_userId) {
        if (rejectReason) *rejectReason = QStringLiteral("invalid-peer");
        return false;
    }

    QJsonObject obj;
    obj["type"] = QStringLiteral("e2e_identity_announce");
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    if (!normalizedPeerId.isEmpty()) {
        obj["receiverId"] = normalizedPeerId;
    }
    QJsonObject identity = e2eIdentityJson(m_userId, m_e2eIdentityPublicKey);
    identity.remove(QStringLiteral("cryptoBackend"));
    obj["e2eIdentity"] = identity;
    return sendJson(obj);
}

bool Client::pinE2EPeerIdentity(const QString& peerId, const QString& expectedFingerprint, QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    const QString normalizedPeerId = peerId.trimmed();
    auto it = m_e2ePeerIdentities.find(normalizedPeerId);
    if (normalizedPeerId.isEmpty() || it == m_e2ePeerIdentities.end()) {
        if (rejectReason) *rejectReason = QStringLiteral("missing-identity");
        return false;
    }

    const QString expected = expectedFingerprint.trimmed().toLower();
    if (!expected.isEmpty() && expected != it->fingerprint) {
        if (rejectReason) *rejectReason = QStringLiteral("fingerprint-mismatch");
        it->fingerprintMismatch = true;
        emit e2eIdentityStateChanged(normalizedPeerId, e2ePeerIdentityStatus(normalizedPeerId));
        return false;
    }

    it->pinned = true;
    it->pinnedFingerprint = it->fingerprint;
    it->verified = false;
    it->verifiedAtMs = 0;
    it->fingerprintMismatch = false;
    if (!saveE2ETrustPins(rejectReason)) {
        it->pinned = false;
        it->pinnedFingerprint.clear();
        emit e2eIdentityStateChanged(normalizedPeerId, e2ePeerIdentityStatus(normalizedPeerId));
        return false;
    }
    E2EStoredTrustPin storedPin;
    storedPin.fingerprint = it->pinnedFingerprint;
    storedPin.backendId = e2eCurrentBackendId();
    m_e2eStoredTrustPins[normalizedPeerId] = storedPin;
    emit e2eIdentityStateChanged(normalizedPeerId, e2ePeerIdentityStatus(normalizedPeerId));
    return true;
}

bool Client::verifyAndPinE2EPeerIdentity(const QString& peerId,
                                         const QString& verificationCode,
                                         QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    const QString normalizedPeerId = peerId.trimmed();
    auto it = m_e2ePeerIdentities.find(normalizedPeerId);
    if (normalizedPeerId.isEmpty() || it == m_e2ePeerIdentities.end()) {
        if (rejectReason) *rejectReason = QStringLiteral("missing-identity");
        return false;
    }
    if (it->fingerprintMismatch) {
        if (rejectReason) *rejectReason = QStringLiteral("fingerprint-mismatch");
        return false;
    }
    const QString expectedCode = normalizedVerificationCode(it->verificationCode);
    if (expectedCode.isEmpty()
        || normalizedVerificationCode(verificationCode) != expectedCode) {
        if (rejectReason) *rejectReason = QStringLiteral("verification-code-mismatch");
        return false;
    }

    it->pinned = true;
    it->pinnedFingerprint = it->fingerprint;
    it->verified = true;
    it->verifiedAtMs = QDateTime::currentMSecsSinceEpoch();
    it->fingerprintMismatch = false;
    if (!saveE2ETrustPins(rejectReason)) {
        it->pinned = false;
        it->pinnedFingerprint.clear();
        it->verified = false;
        it->verifiedAtMs = 0;
        emit e2eIdentityStateChanged(normalizedPeerId, e2ePeerIdentityStatus(normalizedPeerId));
        return false;
    }
    E2EStoredTrustPin storedPin;
    storedPin.fingerprint = it->pinnedFingerprint;
    storedPin.backendId = e2eCurrentBackendId();
    storedPin.verified = true;
    storedPin.verificationCode = it->verificationCode;
    storedPin.verifiedAtMs = it->verifiedAtMs;
    m_e2eStoredTrustPins[normalizedPeerId] = storedPin;
    emit e2eIdentityStateChanged(normalizedPeerId, e2ePeerIdentityStatus(normalizedPeerId));
    return true;
}

bool Client::clearE2EPeerIdentityPin(const QString& peerId, QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    const QString normalizedPeerId = peerId.trimmed();
    if (normalizedPeerId.isEmpty()) {
        if (rejectReason) *rejectReason = QStringLiteral("invalid-peer");
        return false;
    }
    bool changed = m_e2eStoredTrustPins.remove(normalizedPeerId) > 0;
    auto it = m_e2ePeerIdentities.find(normalizedPeerId);
    if (it != m_e2ePeerIdentities.end()) {
        changed = changed || it->pinned || it->verified || it->fingerprintMismatch || !it->pinnedFingerprint.isEmpty();
        it->pinned = false;
        it->pinnedFingerprint.clear();
        it->verified = false;
        it->verifiedAtMs = 0;
        it->fingerprintMismatch = false;
    }
    if (!changed) {
        if (rejectReason) *rejectReason = QStringLiteral("missing-pin");
        return false;
    }
    if (!saveE2ETrustPins(rejectReason)) {
        return false;
    }
    emit e2eIdentityStateChanged(normalizedPeerId, e2ePeerIdentityStatus(normalizedPeerId));
    return true;
}

void Client::loadE2ETrustPins() {
    m_e2eStoredTrustPins.clear();
    const QString normalizedUserId = m_userId.trimmed();
    if (normalizedUserId.isEmpty()) {
        return;
    }
    QFile file(e2eTrustPinsFilePath(normalizedUserId));
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = doc.object();
    const QJsonArray pins = root.value(QStringLiteral("pins")).toArray();
    for (const QJsonValue& value : pins) {
        const QJsonObject pin = value.toObject();
        const QString peerId = pin.value(QStringLiteral("peerId")).toString().trimmed();
        const QString fingerprint = pin.value(QStringLiteral("fingerprintSha256")).toString().trimmed().toLower();
        if (!peerId.isEmpty() && isValidE2EFingerprint(fingerprint)) {
            E2EStoredTrustPin storedPin;
            storedPin.fingerprint = fingerprint;
            storedPin.backendId = pin.value(QStringLiteral("backendId")).toString(QString::fromLatin1(kE2EDraftBackendId)).trimmed();
            storedPin.verified = pin.value(QStringLiteral("verified")).toBool(false);
            storedPin.verificationCode = normalizedVerificationCode(pin.value(QStringLiteral("verificationCode")).toString());
            const QDateTime verifiedAt = QDateTime::fromString(pin.value(QStringLiteral("verifiedAt")).toString(), Qt::ISODateWithMs);
            if (verifiedAt.isValid()) {
                storedPin.verifiedAtMs = verifiedAt.toUTC().toMSecsSinceEpoch();
            }
            m_e2eStoredTrustPins[peerId] = storedPin;
        }
    }
}

bool Client::saveE2ETrustPins(QString* rejectReason) const {
    if (rejectReason) rejectReason->clear();
    QJsonArray pins;
    for (auto it = m_e2ePeerIdentities.constBegin(); it != m_e2ePeerIdentities.constEnd(); ++it) {
        if (!it->pinned || it->pinnedFingerprint.isEmpty() || !isValidE2EFingerprint(it->pinnedFingerprint)) {
            continue;
        }
        QJsonObject pin;
        pin[QStringLiteral("peerId")] = it.key();
        pin[QStringLiteral("fingerprintSha256")] = it->pinnedFingerprint;
        const QString currentBackendId = e2eCurrentBackendId();
        pin[QStringLiteral("backendId")] = currentBackendId;
        pin[QStringLiteral("backendMigrationRequired")] = false;
        pin[QStringLiteral("verified")] = it->verified && !it->fingerprintMismatch;
        if (it->verified && !it->verificationCode.isEmpty()) {
            pin[QStringLiteral("verificationCode")] = it->verificationCode;
        }
        if (it->verifiedAtMs > 0) {
            pin[QStringLiteral("verifiedAt")] = QDateTime::fromMSecsSinceEpoch(it->verifiedAtMs).toUTC().toString(Qt::ISODateWithMs);
        }
        pin[QStringLiteral("updatedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        pins.append(pin);
    }

    QJsonObject root;
    root[QStringLiteral("schema")] = QStringLiteral("qtnetworkchat-e2e-trust-pins-v1");
    root[QStringLiteral("userId")] = m_userId.trimmed();
    root[QStringLiteral("pins")] = pins;
    QSaveFile file(e2eTrustPinsFilePath(m_userId));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (rejectReason) *rejectReason = QStringLiteral("pin-store-open-failed");
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (rejectReason) *rejectReason = QStringLiteral("pin-store-write-failed");
        return false;
    }
    return true;
}

void Client::applyE2EStoredTrustPin(const QString& peerId, E2EPeerIdentity* peerIdentity) const {
    if (!peerIdentity) {
        return;
    }
    const auto storedPin = m_e2eStoredTrustPins.constFind(peerId.trimmed());
    const QString pinnedFingerprint = storedPin == m_e2eStoredTrustPins.constEnd()
        ? QString()
        : storedPin->fingerprint.trimmed().toLower();
    if (pinnedFingerprint.isEmpty()) {
        if (!peerIdentity->pinned) {
            peerIdentity->pinnedFingerprint.clear();
            peerIdentity->verified = false;
            peerIdentity->verifiedAtMs = 0;
            peerIdentity->fingerprintMismatch = false;
        }
        return;
    }
    peerIdentity->pinned = true;
    peerIdentity->pinnedFingerprint = pinnedFingerprint;
    peerIdentity->fingerprintMismatch = peerIdentity->fingerprint != pinnedFingerprint;
    peerIdentity->verified = storedPin != m_e2eStoredTrustPins.constEnd()
        && storedPin->verified
        && !peerIdentity->fingerprintMismatch
        && normalizedVerificationCode(storedPin->verificationCode)
            == normalizedVerificationCode(peerIdentity->verificationCode);
    peerIdentity->verifiedAtMs = peerIdentity->verified ? storedPin->verifiedAtMs : 0;
}

bool Client::e2eLocalIdentityUsable(QString* rejectReason) const {
    if (rejectReason) rejectReason->clear();
    if (m_userId.trimmed().isEmpty() || m_e2eIdentityPrivateKey.isEmpty() || m_e2eIdentityPublicKey.isEmpty()) {
        if (rejectReason) {
            const QString identityBackendId = m_e2eIdentityBackendId.trimmed().isEmpty()
                ? QString()
                : m_e2eIdentityBackendId.trimmed();
            *rejectReason = !identityBackendId.isEmpty() && identityBackendId != e2eCurrentBackendId()
                ? QStringLiteral("e2e-backend-migration-required")
                : QStringLiteral("identity-not-ready");
        }
        return false;
    }
    return e2eBackendUsableForLocalMaterial(m_e2eIdentityBackendId.trimmed().isEmpty()
                                                ? e2eCryptoBackendId()
                                                : m_e2eIdentityBackendId.trimmed(),
                                            rejectReason);
}

bool Client::requireTrustedE2EPeer(const QString& peerId, QString* rejectReason) const {
    if (rejectReason) rejectReason->clear();
    const QString normalizedPeerId = peerId.trimmed();
    const auto peerIdentity = m_e2ePeerIdentities.constFind(normalizedPeerId);
    if (normalizedPeerId.isEmpty() || peerIdentity == m_e2ePeerIdentities.constEnd()) {
        if (rejectReason) *rejectReason = QStringLiteral("missing-identity");
        return false;
    }
    if (peerIdentity->fingerprintMismatch) {
        if (rejectReason) *rejectReason = QStringLiteral("fingerprint-mismatch");
        return false;
    }
    if (!peerIdentity->pinned || peerIdentity->pinnedFingerprint != peerIdentity->fingerprint) {
        if (rejectReason) *rejectReason = QStringLiteral("untrusted-identity");
        return false;
    }
    if (!peerIdentity->verified) {
        if (rejectReason) *rejectReason = QStringLiteral("unverified-identity");
        return false;
    }
    const auto storedPin = m_e2eStoredTrustPins.constFind(normalizedPeerId);
    const QString pinBackendId = storedPin == m_e2eStoredTrustPins.constEnd() || storedPin->backendId.trimmed().isEmpty()
        ? QString::fromLatin1(kE2EDraftBackendId)
        : storedPin->backendId.trimmed();
    if (!e2eBackendUsableForLocalMaterial(pinBackendId, rejectReason)) {
        return false;
    }
    return true;
}

bool Client::populateE2EAgreementIdentityFingerprints(const QString& peerId,
                                                      E2EKeyAgreement* agreement,
                                                      QString* rejectReason) const {
    if (rejectReason) rejectReason->clear();
    if (!agreement) {
        if (rejectReason) *rejectReason = QStringLiteral("invalid-agreement");
        return false;
    }
    if (m_userId.trimmed().isEmpty() || m_e2eIdentityPublicKey.isEmpty()) {
        if (rejectReason) *rejectReason = QStringLiteral("identity-not-ready");
        return false;
    }
    if (!e2eLocalIdentityUsable(rejectReason)) {
        return false;
    }

    const QString normalizedPeerId = peerId.trimmed();
    if (!requireTrustedE2EPeer(normalizedPeerId, rejectReason)) {
        return false;
    }
    const auto peerIdentity = m_e2ePeerIdentities.constFind(normalizedPeerId);

    agreement->senderIdentityFingerprint = e2eFingerprint(m_e2eIdentityPublicKey);
    agreement->receiverIdentityFingerprint = peerIdentity->fingerprint;
    return true;
}

bool Client::validateIncomingE2EAgreementIdentity(const E2EKeyAgreement& agreement,
                                                  QString* rejectReason) const {
    if (rejectReason) rejectReason->clear();
    const QString senderId = agreement.senderId.trimmed();
    if (senderId.isEmpty() || senderId == m_userId) {
        if (rejectReason) *rejectReason = QStringLiteral("invalid-peer");
        return false;
    }
    const QString localFingerprint = e2eFingerprint(m_e2eIdentityPublicKey);
    if (agreement.receiverIdentityFingerprint.trimmed().toLower() != localFingerprint) {
        if (rejectReason) *rejectReason = QStringLiteral("receiver-fingerprint-mismatch");
        return false;
    }

    if (!requireTrustedE2EPeer(senderId, rejectReason)) {
        return false;
    }
    const auto peerIdentity = m_e2ePeerIdentities.constFind(senderId);
    if (agreement.senderIdentityFingerprint.trimmed().toLower() != peerIdentity->fingerprint) {
        if (rejectReason) *rejectReason = QStringLiteral("sender-fingerprint-mismatch");
        return false;
    }
    if (!verifyE2EKeyAgreementSignature(agreement, peerIdentity->publicKey, rejectReason)) {
        return false;
    }
    return true;
}

void Client::installE2EDerivedSession(const QString& peerId,
                                      const QString& keyId,
                                      const QByteArray& sessionKey) {
    const QString normalizedPeerId = peerId.trimmed();
    const QString normalizedKeyId = keyId.trimmed();
    if (normalizedPeerId.isEmpty() || normalizedKeyId.isEmpty() || sessionKey.size() < 16) {
        return;
    }

    E2ESession session;
    session.keyId = normalizedKeyId;
    session.backendId = e2eCurrentBackendId();
    session.sessionKey = sessionKey;
    session.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    m_e2eSessions[normalizedPeerId] = session;
    emit e2eSessionStateChanged(normalizedPeerId, e2eSessionStatus(normalizedPeerId));
}

bool Client::requestE2ESessionRotation(const QString& peerId, QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    if (!isConnected()) {
        if (rejectReason) *rejectReason = QStringLiteral("not-connected");
        return false;
    }

    const QString normalizedPeerId = peerId.trimmed();
    if (normalizedPeerId.isEmpty() || normalizedPeerId == m_userId) {
        if (rejectReason) *rejectReason = QStringLiteral("invalid-peer");
        return false;
    }

    E2EKeyAgreement agreement;
    agreement.protocol = QStringLiteral("qtnetworkchat-e2e-v1");
    agreement.suite = e2eDefaultSuite();
    agreement.senderId = m_userId;
    agreement.receiverId = normalizedPeerId;
    agreement.keyId = QStringLiteral("rotate-%1-%2")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddhhmmsszzz")),
             e2eFingerprint(generateE2ESessionKey()).left(12));
    E2EPendingAgreement pending;
    pending.privateKey = generateE2EPrivateKey();
    agreement.publicKey = e2ePublicKeyFromPrivateKey(pending.privateKey);
    if (!populateE2EAgreementIdentityFingerprints(normalizedPeerId, &agreement, rejectReason)) {
        return false;
    }
    if (!signE2EKeyAgreement(&agreement, m_e2eIdentityPrivateKey, rejectReason)) {
        return false;
    }

    QString validationReason;
    if (!agreement.isValid(&validationReason)) {
        if (rejectReason) *rejectReason = validationReason;
        return false;
    }

    QJsonObject obj;
    obj["type"] = QStringLiteral("e2e_key_rotation_request");
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["receiverId"] = normalizedPeerId;
    obj["reason"] = e2eSessionNeedsRotation(normalizedPeerId)
        ? QStringLiteral("rotation-required")
        : QStringLiteral("manual-request");
    obj["e2eKeyAgreement"] = agreement.toJson();
    pending.agreement = agreement;
    pending.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    m_e2ePendingOutgoingAgreements[normalizedPeerId] = pending;
    return sendJson(obj);
}

bool Client::respondE2ESessionRotation(const QString& peerId,
                                       const QString& keyId,
                                       const QByteArray& publicKey,
                                       bool accepted,
                                       const QString& reason,
                                       QString* rejectReason) {
    Q_UNUSED(publicKey);
    if (rejectReason) rejectReason->clear();
    if (!isConnected()) {
        if (rejectReason) *rejectReason = QStringLiteral("not-connected");
        return false;
    }

    const QString normalizedPeerId = peerId.trimmed();
    E2EKeyAgreement agreement;
    agreement.protocol = QStringLiteral("qtnetworkchat-e2e-v1");
    agreement.suite = e2eDefaultSuite();
    agreement.senderId = m_userId;
    agreement.receiverId = normalizedPeerId;
    agreement.keyId = keyId.trimmed();
    E2EPendingAgreement pending;
    pending.privateKey = generateE2EPrivateKey();
    agreement.publicKey = e2ePublicKeyFromPrivateKey(pending.privateKey);
    if (!populateE2EAgreementIdentityFingerprints(normalizedPeerId, &agreement, rejectReason)) {
        return false;
    }
    if (!signE2EKeyAgreement(&agreement, m_e2eIdentityPrivateKey, rejectReason)) {
        return false;
    }

    QString validationReason;
    if (!agreement.isValid(&validationReason)) {
        if (rejectReason) *rejectReason = validationReason;
        return false;
    }

    QJsonObject obj;
    obj["type"] = QStringLiteral("e2e_key_rotation_response");
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["receiverId"] = normalizedPeerId;
    obj["accepted"] = accepted;
    obj["reason"] = accepted ? QStringLiteral("accepted") : (reason.trimmed().isEmpty() ? QStringLiteral("rejected") : reason.trimmed());
    obj["e2eKeyAgreement"] = agreement.toJson();
    if (accepted) {
        const auto requestIt = m_e2ePendingIncomingAgreements.constFind(normalizedPeerId);
        if (requestIt == m_e2ePendingIncomingAgreements.constEnd()) {
            if (rejectReason) *rejectReason = QStringLiteral("missing-pending-agreement");
            return false;
        }
        QString deriveReason;
        const QByteArray sessionKey = deriveE2EAuthenticatedSessionKey(pending.privateKey,
                                                                       agreement,
                                                                       requestIt->agreement,
                                                                       &deriveReason);
        if (sessionKey.isEmpty()) {
            if (rejectReason) *rejectReason = deriveReason;
            return false;
        }
        installE2EDerivedSession(normalizedPeerId, agreement.keyId, sessionKey);
        m_e2ePendingIncomingAgreements.remove(normalizedPeerId);
    }
    pending.agreement = agreement;
    pending.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    return sendJson(obj);
}

QString Client::transportSecurityDescription() const {
    if (m_webSocket) {
        return m_transportName == QStringLiteral("wss")
            ? (isConnected()
                ? QStringLiteral("WSS 加密通道")
                : QStringLiteral("WSS 已启用，等待握手"))
            : QStringLiteral("WebSocket 明文通道");
    }
    if (const QSslSocket* sslSocket = qobject_cast<const QSslSocket*>(m_socket)) {
        return sslSocket->isEncrypted()
            ? "TLS 加密通道"
            : "TLS 已启用，等待握手";
    }
    if (clientTlsEnabled() && !QSslSocket::supportsSsl()) {
        return "TLS 已请求，但当前 Qt/OpenSSL 不可用";
    }
    return "普通 TCP 通道";
}

bool Client::sendMessage(const QString& content) {
    if (!isConnected()) return false;

    QJsonObject obj;
    obj["type"] = "message";
    obj["messageType"] = static_cast<int>(MessageType::Text);
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["content"] = content;

    return sendJson(obj);
}

bool Client::sendPrivateMessage(const QString& receiverId, const QString& content, const QString& clientMessageId) {
    if (!isConnected()) return false;

    QJsonObject obj;
    obj["type"] = "private";
    obj["messageType"] = static_cast<int>(MessageType::Private);
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["receiverId"] = receiverId;
    obj["content"] = content;
    const QString trimmedClientMessageId = clientMessageId.trimmed();
    if (!trimmedClientMessageId.isEmpty()) {
        obj["clientMessageId"] = trimmedClientMessageId;
    }

    return sendJson(obj);
}

bool Client::sendEncryptedPrivateMessage(const QString& receiverId, const QString& content, QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    if (!isConnected()) {
        if (rejectReason) *rejectReason = QStringLiteral("not-connected");
        return false;
    }

    const QString normalizedReceiverId = receiverId.trimmed();
    auto sessionIt = m_e2eSessions.find(normalizedReceiverId);
    if (normalizedReceiverId.isEmpty() || sessionIt == m_e2eSessions.constEnd()) {
        if (rejectReason) *rejectReason = QStringLiteral("missing-session");
        return false;
    }
    if (!requireTrustedE2EPeer(normalizedReceiverId, rejectReason)) {
        return false;
    }
    if (sessionIt->rotationRequired || sessionIt->encryptedMessages >= m_e2eSessionMessageLimit) {
        sessionIt->rotationRequired = true;
        if (rejectReason) *rejectReason = QStringLiteral("rotation-required");
        emit e2eSessionStateChanged(normalizedReceiverId, e2eSessionStatus(normalizedReceiverId));
        return false;
    }

    QString reason;
    const E2EEnvelope envelope = encryptE2EText(m_userId,
                                               normalizedReceiverId,
                                               sessionIt->keyId,
                                               sessionIt->sessionKey,
                                               content,
                                               &reason);
    if (!envelope.isValid(&reason)) {
        if (rejectReason) *rejectReason = reason.isEmpty() ? QStringLiteral("invalid-envelope") : reason;
        return false;
    }

    QJsonObject obj;
    obj["type"] = "private";
    obj["messageType"] = static_cast<int>(MessageType::Private);
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["receiverId"] = normalizedReceiverId;
    obj["content"] = QStringLiteral("[encrypted]");
    obj["e2eEnvelope"] = envelope.toJson();
    obj["isEncrypted"] = true;

    const bool sent = sendJson(obj);
    if (sent) {
        ++sessionIt->encryptedMessages;
        if (sessionIt->encryptedMessages >= m_e2eSessionMessageLimit) {
            sessionIt->rotationRequired = true;
        }
        emit e2eSessionStateChanged(normalizedReceiverId, e2eSessionStatus(normalizedReceiverId));
    }
    return sent;
}

bool Client::e2eFileSessionForPeer(const QString& peerId,
                                   const E2ESession** session,
                                   QString* rejectReason) const {
    if (session) *session = nullptr;
    if (rejectReason) rejectReason->clear();
    const QString normalizedPeerId = peerId.trimmed();
    const auto sessionIt = m_e2eSessions.constFind(normalizedPeerId);
    if (normalizedPeerId.isEmpty() || sessionIt == m_e2eSessions.constEnd()) {
        if (rejectReason) *rejectReason = QStringLiteral("missing-session");
        return false;
    }
    if (!requireTrustedE2EPeer(normalizedPeerId, rejectReason)) {
        return false;
    }
    if (sessionIt->rotationRequired || sessionIt->encryptedMessages >= m_e2eSessionMessageLimit) {
        if (rejectReason) *rejectReason = QStringLiteral("rotation-required");
        return false;
    }
    if (session) *session = &(*sessionIt);
    return true;
}

bool Client::markE2EFileChunkSent(const QString& peerId) {
    auto sessionIt = m_e2eSessions.find(peerId.trimmed());
    if (sessionIt == m_e2eSessions.end()) {
        return false;
    }
    ++sessionIt->encryptedMessages;
    if (sessionIt->encryptedMessages >= m_e2eSessionMessageLimit) {
        sessionIt->rotationRequired = true;
    }
    emit e2eSessionStateChanged(peerId.trimmed(), e2eSessionStatus(peerId));
    return !sessionIt->rotationRequired;
}

bool Client::sendFriendRequest(const QString& receiverId) {
    if (!isConnected() || receiverId.isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "friend_request";
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["receiverId"] = receiverId;
    return sendJson(obj);
}

bool Client::searchFriendByAccount(const QString& account) {
    if (!isConnected() || account.trimmed().isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "friend_search";
    obj["account"] = account.trimmed();
    return sendJson(obj);
}

bool Client::sendFriendResponse(const QString& receiverId, bool accepted) {
    if (!isConnected() || receiverId.isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "friend_response";
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["receiverId"] = receiverId;
    obj["accepted"] = accepted;
    const bool sent = sendJson(obj);
    if (sent) {
        const QString normalizedReceiverId = receiverId.trimmed();
        if (accepted) {
            addOrUpdateFriend(normalizedReceiverId, m_pendingIncomingFriendNames.value(normalizedReceiverId));
        }
        m_pendingIncomingFriendNames.remove(normalizedReceiverId);
    }
    return sent;
}

bool Client::requestAccountDeactivation(const QString& reason) {
    if (!isConnected()) return false;

    QJsonObject obj;
    obj["type"] = "account_deactivation_request";
    obj["reason"] = reason.trimmed();
    return sendJson(obj);
}

bool Client::cancelAccountDeactivation() {
    if (!isConnected()) return false;

    QJsonObject obj;
    obj["type"] = "account_deactivation_cancel";
    return sendJson(obj);
}

bool Client::sendServerGroupAnnouncementUpdate(const QString& groupId, const QString& announcement) {
    if (!isConnected() || groupId.trimmed().isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "server_group_announcement_update";
    obj["groupId"] = groupId.trimmed();
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["announcement"] = announcement.trimmed();
    return sendJson(obj);
}

bool Client::sendServerGroupMemberUpdate(const QString& groupId, const QString& memberId, const QString& action) {
    if (!isConnected() || groupId.trimmed().isEmpty() || memberId.trimmed().isEmpty()) return false;

    const QString normalizedAction = action.trimmed().toLower();
    if (normalizedAction != "add"
        && normalizedAction != "remove"
        && normalizedAction != "promote_admin"
        && normalizedAction != "demote_admin"
        && normalizedAction != "set_admin"
        && normalizedAction != "unset_admin") {
        return false;
    }

    QJsonObject obj;
    obj["type"] = "server_group_member_update";
    obj["groupId"] = groupId.trimmed();
    obj["memberId"] = memberId.trimmed();
    obj["action"] = normalizedAction == QLatin1String("set_admin")
        ? QStringLiteral("promote_admin")
        : (normalizedAction == QLatin1String("unset_admin") ? QStringLiteral("demote_admin") : normalizedAction);
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    return sendJson(obj);
}

bool Client::sendServerGroupEssenceUpdate(const QString& groupId, const QString& messageId, bool enabled) {
    if (!isConnected() || groupId.trimmed().isEmpty() || messageId.trimmed().isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "server_group_essence_update";
    obj["groupId"] = groupId.trimmed();
    obj["messageId"] = messageId.trimmed();
    obj["enabled"] = enabled;
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    return sendJson(obj);
}

bool Client::sendMessageFavoriteUpdate(const QString& sessionId, const QString& messageId, bool favorite, const QJsonObject& message) {
    if (!isConnected() || sessionId.trimmed().isEmpty() || messageId.trimmed().isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "message_favorite_update";
    obj["sessionId"] = sessionId.trimmed();
    obj["messageId"] = messageId.trimmed();
    obj["favorite"] = favorite;
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    if (!message.isEmpty()) {
        obj["message"] = message;
    }
    return sendJson(obj);
}

bool Client::sendServerGroupMessageRecall(const QString& groupId, const QString& messageId) {
    if (!isConnected() || groupId.trimmed().isEmpty() || messageId.trimmed().isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "server_group_message_recall";
    obj["groupId"] = groupId.trimmed();
    obj["messageId"] = messageId.trimmed();
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    return sendJson(obj);
}

bool Client::sendServerGroupMemberMute(const QString& groupId, const QString& memberId, qint64 mutedUntil, const QString& reason) {
    if (!isConnected() || groupId.trimmed().isEmpty() || memberId.trimmed().isEmpty() || mutedUntil <= 0) return false;

    QJsonObject obj;
    obj["type"] = "server_group_member_mute";
    obj["groupId"] = groupId.trimmed();
    obj["memberId"] = memberId.trimmed();
    obj["mutedUntil"] = QString::number(mutedUntil);
    obj["reason"] = reason.trimmed();
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    return sendJson(obj);
}

bool Client::sendServerGroupMemberUnmute(const QString& groupId, const QString& memberId) {
    if (!isConnected() || groupId.trimmed().isEmpty() || memberId.trimmed().isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "server_group_member_unmute";
    obj["groupId"] = groupId.trimmed();
    obj["memberId"] = memberId.trimmed();
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    return sendJson(obj);
}

bool Client::requestServerGroupMemberProfile(const QString& groupId, const QString& memberId) {
    if (!isConnected() || groupId.trimmed().isEmpty() || memberId.trimmed().isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "server_group_member_profile_request";
    obj["groupId"] = groupId.trimmed();
    obj["memberId"] = memberId.trimmed();
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    return sendJson(obj);
}

bool Client::sendServerGroupSettingsUpdate(const QString& groupId, const QJsonObject& settings) {
    if (!isConnected() || groupId.trimmed().isEmpty() || settings.isEmpty()) return false;
    QJsonObject obj;
    obj["type"] = "server_group_settings_update";
    obj["groupId"] = groupId.trimmed();
    obj["settings"] = settings;
    return sendJson(obj);
}

bool Client::sendServerGroupUserSettingsUpdate(const QString& groupId, const QJsonObject& settings) {
    if (!isConnected() || groupId.trimmed().isEmpty() || settings.isEmpty()) return false;
    QJsonObject obj;
    obj["type"] = "server_group_user_settings_update";
    obj["groupId"] = groupId.trimmed();
    obj["settings"] = settings;
    return sendJson(obj);
}

bool Client::searchServerGroups(const QString& keyword) {
    if (!isConnected() || keyword.trimmed().isEmpty()) return false;
    QJsonObject obj;
    obj["type"] = "server_group_search";
    obj["keyword"] = keyword.trimmed();
    return sendJson(obj);
}

bool Client::requestServerGroupJoin(const QString& groupId, const QString& message) {
    if (!isConnected() || groupId.trimmed().isEmpty()) return false;
    QJsonObject obj;
    obj["type"] = "server_group_join_request";
    obj["groupId"] = groupId.trimmed();
    obj["message"] = message.trimmed().left(120);
    return sendJson(obj);
}

bool Client::respondServerGroupJoinRequest(const QString& requestId, bool accepted) {
    if (!isConnected() || requestId.trimmed().isEmpty()) return false;
    QJsonObject obj;
    obj["type"] = "server_group_join_response";
    obj["requestId"] = requestId.trimmed();
    obj["accepted"] = accepted;
    return sendJson(obj);
}

bool Client::leaveServerGroup(const QString& groupId) {
    if (!isConnected() || groupId.trimmed().isEmpty()) return false;
    QJsonObject obj;
    obj["type"] = "server_group_leave";
    obj["groupId"] = groupId.trimmed();
    return sendJson(obj);
}

bool Client::dissolveServerGroup(const QString& groupId) {
    if (!isConnected() || groupId.trimmed().isEmpty() || groupId.trimmed() == QLatin1String("public")) return false;
    QJsonObject obj;
    obj["type"] = "server_group_dissolve";
    obj["groupId"] = groupId.trimmed();
    return sendJson(obj);
}

bool Client::createPrivateServerGroup(const QString& groupName, const QString& announcement, const QStringList& initialMemberIds) {
    if (!isConnected() || groupName.trimmed().isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "server_group_create";
    obj["groupType"] = "private";
    obj["groupName"] = groupName.trimmed();
    obj["announcement"] = announcement.trimmed();
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    QJsonArray members;
    QStringList seenMemberIds;
    for (const QString& memberId : initialMemberIds) {
        const QString normalizedMemberId = memberId.trimmed();
        if (normalizedMemberId.isEmpty() || normalizedMemberId == m_userId || seenMemberIds.contains(normalizedMemberId)) {
            continue;
        }
        seenMemberIds << normalizedMemberId;
        members.append(normalizedMemberId);
    }
    if (!members.isEmpty()) {
        obj["members"] = members;
    }
    return sendJson(obj);
}

bool Client::sendServerGroupMessage(const QString& groupId, const QString& content, const QString& clientMessageId) {
    if (!isConnected() || groupId.trimmed().isEmpty() || content.trimmed().isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "server_group_message";
    obj["groupId"] = groupId.trimmed();
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["content"] = content;
    const QString trimmedClientMessageId = clientMessageId.trimmed();
    if (!trimmedClientMessageId.isEmpty()) {
        obj["clientMessageId"] = trimmedClientMessageId;
    }
    return sendJson(obj);
}

bool Client::sendServerGroupFile(const QString& groupId, const QString& filePath) {
    const QString trimmedGroupId = groupId.trimmed();
    if (trimmedGroupId.isEmpty()) return false;
    return sendFilePayload(filePath,
                           QString(),
                           MessageType::File,
                           "发送了文件: ",
                           QString(),
                           0,
                           0,
                           QVector<qint64>(),
                           trimmedGroupId);
}

bool Client::sendServerGroupImage(const QString& groupId, const QString& filePath) {
    const QString trimmedGroupId = groupId.trimmed();
    if (trimmedGroupId.isEmpty()) return false;
    return sendFilePayload(filePath,
                           QString(),
                           MessageType::Image,
                           "发送了图片: ",
                           QString(),
                           0,
                           0,
                           QVector<qint64>(),
                           trimmedGroupId);
}

bool Client::sendFile(const QString& filePath, const QString& receiverId) {
    return sendFilePayload(filePath, receiverId, MessageType::File, "发送了文件: ");
}

bool Client::sendImage(const QString& filePath, const QString& receiverId) {
    return sendFilePayload(filePath, receiverId, MessageType::Image, "发送了图片: ");
}

bool Client::resumeFileTransfer(const QString& filePath,
                                const QString& transferId,
                                qint64 confirmedBytes,
                                qint64 nextChunkIndex,
                                const QString& receiverId,
                                MessageType messageType,
                                const QString& serverGroupId) {
    const QString trimmedTransferId = transferId.trimmed();
    if (trimmedTransferId.isEmpty() || confirmedBytes < 0 || nextChunkIndex < 0) {
        return false;
    }
    if (messageType != MessageType::File && messageType != MessageType::Image) {
        return false;
    }

    const QString contentPrefix = messageType == MessageType::Image
        ? "发送了图片: "
        : "发送了文件: ";
    return sendFilePayload(filePath,
                           receiverId,
                           messageType,
                           contentPrefix,
                           trimmedTransferId,
                           confirmedBytes,
                           nextChunkIndex,
                           QVector<qint64>(),
                           serverGroupId);
}

bool Client::queryAndResumeFileTransfer(const QString& filePath,
                                        const QString& transferId,
                                        const QString& receiverId,
                                        MessageType messageType,
                                        QString* rejectReason,
                                        int timeoutMs,
                                        const QString& serverGroupId) {
    if (rejectReason) rejectReason->clear();
    if (messageType != MessageType::File && messageType != MessageType::Image) {
        if (rejectReason) *rejectReason = "续传类型非法";
        return false;
    }

    const QString trimmedTransferId = transferId.trimmed();
    QJsonObject savedState;
    const bool hasSavedState = loadOutgoingTransferState(&savedState)
        && savedState.value("transferId").toString().trimmed() == trimmedTransferId;
    const bool savedE2EWireResume =
        hasSavedState
        && savedState.value("e2eFileEncrypted").toBool(false)
        && savedState.value("recoveryMode").toString().trimmed().toLower() == QLatin1String("resume");

    qint64 localFileSize = 0;
    qint64 localChunkCount = 0;
    QString localFileHash;
    if (savedE2EWireResume) {
        localFileSize = savedState.value("e2eFileWireSize").toVariant().toLongLong();
        localChunkCount = savedState.value("chunkCount").toVariant().toLongLong();
        localFileHash = savedState.value("e2eFileWireHash").toString();
        QByteArray recoveryWirePayload;
        QString recoveryRejectReason;
        if (!loadE2EFileRecoveryWirePayload(savedState, &recoveryWirePayload, &recoveryRejectReason)
            || !e2eEnvelopeHeaderLooksSafe(savedState.value("e2eFileEnvelopeHeader").toObject())) {
            if (rejectReason) *rejectReason = recoveryRejectReason.isEmpty()
                ? QStringLiteral("e2e-file-resume-cache-unavailable")
                : recoveryRejectReason;
            return false;
        }
        const auto sessionIt = m_e2eSessions.constFind(savedState.value("receiverId").toString().trimmed());
        const QString keyFingerprint = savedState.value("e2eFileKeyFingerprintSha256").toString().trimmed();
        if (sessionIt == m_e2eSessions.constEnd()
            || sessionIt->keyId != savedState.value("e2eFileKeyId").toString().trimmed()
            || e2eFingerprint(sessionIt->sessionKey).compare(keyFingerprint, Qt::CaseInsensitive) != 0) {
            if (rejectReason) *rejectReason = QStringLiteral("e2e-file-session-mismatch");
            return false;
        }
    } else if (!collectFileTransferMetadata(filePath, &localFileSize, &localChunkCount, &localFileHash)) {
        if (rejectReason) *rejectReason = "本地续传文件不可用";
        return false;
    }

    qint64 confirmedBytes = 0;
    qint64 nextChunkIndex = 0;
    QVector<qint64> receivedChunks;
    qint64 remoteFileSize = 0;
    qint64 remoteChunkSize = 0;
    qint64 remoteChunkCount = 0;
    QString remoteFileHash;
    QString queryRejectReason;
    if (!queryFileTransferResumeState(trimmedTransferId,
                                      &confirmedBytes,
                                      &nextChunkIndex,
                                      &receivedChunks,
                                      &queryRejectReason,
                                      timeoutMs,
                                      &remoteFileSize,
                                      &remoteChunkSize,
                                      &remoteChunkCount,
                                      &remoteFileHash)) {
        if (rejectReason) *rejectReason = queryRejectReason;
        return false;
    }

    const QString trimmedRemoteHash = remoteFileHash.trimmed();
    QSet<qint64> receivedChunkSet;
    qint64 firstMissingChunkIndex = 0;
    if (remoteFileSize != localFileSize) {
        if (rejectReason) *rejectReason = "续传文件大小不一致";
        return false;
    }
    if (remoteChunkSize != kTransferChunkBytes) {
        if (rejectReason) *rejectReason = "续传分片大小不一致";
        return false;
    }
    if (remoteChunkCount != localChunkCount) {
        if (rejectReason) *rejectReason = "续传分片数量不一致";
        return false;
    }
    if (trimmedRemoteHash.isEmpty()) {
        if (rejectReason) *rejectReason = "续传文件校验信息缺失";
        return false;
    }
    if (trimmedRemoteHash.compare(localFileHash, Qt::CaseInsensitive) != 0) {
        if (rejectReason) *rejectReason = "续传文件哈希不一致";
        return false;
    }
    if (!resolveResumeProgress(confirmedBytes,
                               nextChunkIndex,
                               receivedChunks,
                               localFileSize,
                               localChunkCount,
                               &receivedChunkSet,
                               &firstMissingChunkIndex)) {
        if (rejectReason) *rejectReason = "续传进度非法";
        return false;
    }

    return sendFilePayload(filePath,
                           receiverId,
                           messageType,
                           messageType == MessageType::Image ? "发送了图片: " : "发送了文件: ",
                           trimmedTransferId,
                           confirmedBytes,
                           firstMissingChunkIndex,
                           receivedChunks,
                           serverGroupId);
}

bool Client::queryFileTransferResumeState(const QString& transferId,
                                          qint64* confirmedBytes,
                                          qint64* nextChunkIndex,
                                          QVector<qint64>* receivedChunks,
                                          QString* rejectReason,
                                          int timeoutMs,
                                          qint64* fileSize,
                                          qint64* chunkSize,
                                          qint64* chunkCount,
                                          QString* fileHash) {
    if (confirmedBytes) *confirmedBytes = 0;
    if (nextChunkIndex) *nextChunkIndex = 0;
    if (receivedChunks) receivedChunks->clear();
    if (rejectReason) rejectReason->clear();
    if (fileSize) *fileSize = 0;
    if (chunkSize) *chunkSize = 0;
    if (chunkCount) *chunkCount = 0;
    if (fileHash) fileHash->clear();

    const QString trimmedTransferId = transferId.trimmed();
    if (trimmedTransferId.isEmpty()) {
        if (rejectReason) *rejectReason = "传输编号为空";
        return false;
    }

    QJsonObject obj;
    obj["type"] = "file_transfer_resume_query";
    obj["transferId"] = trimmedTransferId;
    if (!sendJson(obj)) {
        if (rejectReason) *rejectReason = "续传状态查询发送失败";
        return false;
    }
    return waitForFileTransferResumeState(trimmedTransferId,
                                          confirmedBytes,
                                          nextChunkIndex,
                                          receivedChunks,
                                          rejectReason,
                                          timeoutMs,
                                          fileSize,
                                          chunkSize,
                                          chunkCount,
                                          fileHash);
}

bool Client::saveOutgoingTransferState(const QString& transferId,
                                       const QString& filePath,
                                       const QString& receiverId,
                                       MessageType messageType,
                                       const QString& fileHash,
                                       qint64 fileSize,
                                       qint64 chunkCount,
                                       const QJsonObject& recoveryPolicy,
                                       const QString& serverGroupId) {
    const QString trimmedTransferId = transferId.trimmed();
    const QString trimmedFileHash = fileHash.trimmed();
    if (trimmedTransferId.isEmpty()
        || filePath.trimmed().isEmpty()
        || trimmedFileHash.isEmpty()
        || fileSize <= 0
        || chunkCount <= 0
        || (messageType != MessageType::File && messageType != MessageType::Image)) {
        return false;
    }

    QJsonObject state;
    state["transferId"] = trimmedTransferId;
    state["filePath"] = QFileInfo(filePath).absoluteFilePath();
    state["receiverId"] = receiverId;
    state["groupId"] = serverGroupId.trimmed();
    state["messageType"] = static_cast<int>(messageType);
    state["fileHash"] = trimmedFileHash;
    state["fileSize"] = QString::number(fileSize);
    state["chunkSize"] = QString::number(kTransferChunkBytes);
    state["chunkCount"] = QString::number(chunkCount);
    state["updatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString recoveryMode = recoveryPolicy.value(QStringLiteral("recoveryMode"))
        .toString(QStringLiteral("resume"))
        .trimmed()
        .toLower();
    state["recoveryMode"] = recoveryMode == QLatin1String("resend") ? QStringLiteral("resend") : QStringLiteral("resume");
    if (state["recoveryMode"].toString() == QLatin1String("resend")) {
        state["canAutoResume"] = false;
        state["recoveryReason"] = recoveryPolicy.value(QStringLiteral("recoveryReason"))
            .toString(QStringLiteral("e2e-file-resend-required"));
        state["recoveryAction"] = recoveryPolicy.value(QStringLiteral("recoveryAction"))
            .toString(QStringLiteral("resend-file"));
    } else {
        state["canAutoResume"] = true;
        state["recoveryReason"] = recoveryPolicy.value(QStringLiteral("recoveryReason"))
            .toString(QStringLiteral("can-query-resume-state"));
        state["recoveryAction"] = recoveryPolicy.value(QStringLiteral("recoveryAction"))
            .toString(QStringLiteral("resume-transfer"));
    }
    if (recoveryPolicy.value(QStringLiteral("e2eFileEncrypted")).toBool(false)) {
        state["e2eFileEncrypted"] = true;
        state["e2eFileKeyId"] = recoveryPolicy.value(QStringLiteral("e2eFileKeyId")).toString();
        state["e2eFileKeyFingerprintSha256"] = recoveryPolicy.value(QStringLiteral("e2eFileKeyFingerprintSha256")).toString();
        state["e2eFilePlainSize"] = QString::number(recoveryPolicy.value(QStringLiteral("e2eFilePlainSize")).toVariant().toLongLong());
        state["e2eFilePlainHash"] = recoveryPolicy.value(QStringLiteral("e2eFilePlainHash")).toString();
        state["e2eFileWireHash"] = recoveryPolicy.value(QStringLiteral("e2eFileWireHash")).toString();
        state["e2eFileWireSize"] = QString::number(recoveryPolicy.value(QStringLiteral("e2eFileWireSize")).toVariant().toLongLong());
        if (recoveryPolicy.value(QStringLiteral("e2eFileResumeCache")).toBool(false)) {
            state["e2eFileResumeCache"] = true;
            state["e2eFileResumeCacheFormat"] =
                recoveryPolicy.value(QStringLiteral("e2eFileResumeCacheFormat"))
                    .toString(QString::fromLatin1(kE2EFileResumeCacheFormat));
            state["e2eFileResumeCacheSha256"] =
                recoveryPolicy.value(QStringLiteral("e2eFileResumeCacheSha256")).toString();
            state["e2eFileResumeCacheSize"] =
                QString::number(recoveryPolicy.value(QStringLiteral("e2eFileResumeCacheSize"))
                                    .toVariant().toLongLong());
            if (recoveryPolicy.value(QStringLiteral("e2eFileEnvelopeHeader")).isObject()) {
                state["e2eFileEnvelopeHeader"] =
                    recoveryPolicy.value(QStringLiteral("e2eFileEnvelopeHeader")).toObject();
            }
        }
        const QString objectKey = recoveryPolicy.value(QStringLiteral("e2eFileObjectStoreKey"))
            .toString()
            .trimmed();
        const QString objectHash = recoveryPolicy.value(QStringLiteral("e2eFileObjectStoreHash"))
            .toString()
            .trimmed()
            .toLower();
        const qint64 objectSize = recoveryPolicy.value(QStringLiteral("e2eFileObjectStoreSize"))
            .toVariant()
            .toLongLong();
        const QString storeType = recoveryPolicy.value(QStringLiteral("e2eFileObjectStoreType"))
            .toString(QStringLiteral("filesystem"))
            .trimmed();
        const QString normalizedStoreType = normalizedE2EObjectRecoveryStoreType(storeType);
        const bool objectEvidenceReady =
            recoveryPolicy.value(QStringLiteral("e2eFileObjectRecoveryCandidate")).toBool(false)
            && isSafeObjectStoreKeyToken(objectKey)
            && isValidE2EFingerprint(objectHash)
            && objectHash.compare(state.value(QStringLiteral("e2eFileWireHash")).toString(),
                                  Qt::CaseInsensitive) == 0
            && objectSize == state.value(QStringLiteral("e2eFileWireSize")).toVariant().toLongLong()
            && (normalizedStoreType == QLatin1String("filesystem")
                || isS3OfflineE2EObjectRecoveryStore(normalizedStoreType))
            && recoveryPolicy.value(QStringLiteral("e2eFileEnvelopeHeader")).isObject()
            && e2eEnvelopeHeaderLooksSafe(
                recoveryPolicy.value(QStringLiteral("e2eFileEnvelopeHeader")).toObject())
            && recoveryPolicy.value(QStringLiteral("e2eFileEnvelopeHeader"))
                   .toObject()
                   .value(QStringLiteral("ciphertextSha256"))
                   .toString()
                   .trimmed()
                   .compare(objectHash, Qt::CaseInsensitive) == 0;
        if (objectEvidenceReady) {
            state["e2eFileObjectRecoveryCandidate"] = true;
            state["e2eFileObjectStoreKey"] = objectKey;
            state["e2eFileObjectStoreType"] = normalizedStoreType;
            state["e2eFileObjectStoreHash"] = objectHash;
            state["e2eFileObjectStoreSize"] = QString::number(objectSize);
            state["e2eFileOfflineObjectRecoveryReady"] = false;
            state["e2eFileObjectRecoveryScope"] =
                e2eObjectRecoveryScopeForStoreType(normalizedStoreType);
            state["e2eFileObjectRecoveryReviewGate"] =
                e2eObjectRecoveryReviewGateForStoreType(normalizedStoreType, true);
            state["e2eFileObjectRecoveryReason"] =
                e2eObjectRecoveryDefaultReasonForStoreType(normalizedStoreType, true);
            state["e2eFileObjectRecoveryAction"] =
                e2eObjectRecoveryDefaultActionForStoreType(normalizedStoreType, true);
            state["e2eFileObjectRecoveryMaterialPolicy"] =
                QStringLiteral("object-ciphertext-only-no-secret-export");
            if (!state.contains(QStringLiteral("e2eFileEnvelopeHeader"))) {
                state["e2eFileEnvelopeHeader"] =
                    recoveryPolicy.value(QStringLiteral("e2eFileEnvelopeHeader")).toObject();
            }
        }
    }

    QSaveFile file(outgoingTransferStateFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
    file.write("\n");
    return file.commit();
}

bool Client::loadOutgoingTransferState(QJsonObject* state) const {
    if (state) *state = QJsonObject();

    const QString path = outgoingTransferStateFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return false;
    }

    const QJsonObject obj = doc.object();
    const MessageType messageType = static_cast<MessageType>(obj["messageType"].toInt(-1));
    if (obj["transferId"].toString().trimmed().isEmpty()
        || obj["filePath"].toString().trimmed().isEmpty()
        || obj["fileHash"].toString().trimmed().isEmpty()
        || obj["fileSize"].toVariant().toLongLong() <= 0
        || obj["chunkSize"].toVariant().toLongLong() != kTransferChunkBytes
        || obj["chunkCount"].toVariant().toLongLong() <= 0
        || (messageType != MessageType::File && messageType != MessageType::Image)) {
        return false;
    }

    const QString updatedAtText = obj["updatedAt"].toString().trimmed();
    if (!updatedAtText.isEmpty()) {
        const QDateTime updatedAt = QDateTime::fromString(updatedAtText, Qt::ISODate);
        if (!updatedAt.isValid()
            || updatedAt.msecsTo(QDateTime::currentDateTimeUtc()) > kOutgoingTransferStateMaxAgeMs) {
            file.close();
            QFile::remove(path);
            removeE2EFileResumeCache(obj.value("transferId").toString());
            return false;
        }
    }

    if (state) *state = obj;
    return true;
}

QJsonObject Client::savedOutgoingTransferRecoveryStatus() const {
    QJsonObject status;
    status["configured"] = false;
    status["canAutoResume"] = false;
    status["recoveryMode"] = QStringLiteral("none");
    status["reason"] = QStringLiteral("missing-state");

    QJsonObject state;
    if (!loadOutgoingTransferState(&state)) {
        return status;
    }

    const bool e2eFileEncrypted = state.value("e2eFileEncrypted").toBool(false);
    const QFileInfo savedFileInfo(state.value("filePath").toString());
    status["configured"] = true;
    status["transferId"] = state.value("transferId").toString();
    status["fileName"] = savedFileInfo.fileName().isEmpty()
        ? QStringLiteral("unnamed")
        : savedFileInfo.fileName();
    if (!e2eFileEncrypted) {
        status["filePath"] = state.value("filePath").toString();
    }
    status["receiverId"] = state.value("receiverId").toString();
    status["groupId"] = state.value("groupId").toString();
    status["messageType"] = state.value("messageType").toInt();
    status["fileHash"] = state.value("fileHash").toString();
    status["fileSize"] = state.value("fileSize").toString();
    status["chunkSize"] = state.value("chunkSize").toString();
    status["chunkCount"] = state.value("chunkCount").toString();
    status["updatedAt"] = state.value("updatedAt").toString();

    const QString recoveryMode = state.value("recoveryMode").toString(QStringLiteral("resume")).trimmed().toLower();
    status["e2eFileEncrypted"] = e2eFileEncrypted;
    if (e2eFileEncrypted) {
        status["e2eFileKeyId"] = state.value("e2eFileKeyId").toString();
        status["e2eFileKeyFingerprintSha256"] = state.value("e2eFileKeyFingerprintSha256").toString();
        status["e2eFilePlainSize"] = state.value("e2eFilePlainSize").toString();
        status["e2eFilePlainHash"] = state.value("e2eFilePlainHash").toString();
        status["e2eFileWireHash"] = state.value("e2eFileWireHash").toString();
        status["e2eFileWireSize"] = state.value("e2eFileWireSize").toString();
        status["e2eFileResumeCache"] = state.value("e2eFileResumeCache").toBool(false);
        status["e2eFileResumeCacheFormat"] = state.value("e2eFileResumeCacheFormat").toString();
        status["e2eFileResumeCacheSha256"] = state.value("e2eFileResumeCacheSha256").toString();
        status["e2eFileResumeCacheSize"] = state.value("e2eFileResumeCacheSize").toString();
        if (state.value("e2eFileEnvelopeHeader").isObject()) {
            QJsonObject header = state.value("e2eFileEnvelopeHeader").toObject();
            header.remove(QStringLiteral("ciphertext"));
            status["e2eFileEnvelopeHeader"] = header;
        }
        const bool objectRecoveryCandidate =
            state.value("e2eFileObjectRecoveryCandidate").toBool(false);
        status["e2eFileObjectRecoveryCandidate"] = objectRecoveryCandidate;
        status["e2eFileOfflineObjectRecoveryReady"] =
            state.value("e2eFileOfflineObjectRecoveryReady").toBool(false);
        if (objectRecoveryCandidate) {
            const QString objectKey = state.value("e2eFileObjectStoreKey").toString().trimmed();
            const bool objectKeySafe = isSafeObjectStoreKeyToken(objectKey);
            const QString storeType = normalizedE2EObjectRecoveryStoreType(
                state.value("e2eFileObjectStoreType").toString(QStringLiteral("filesystem")));
            if (objectKeySafe) {
                status["e2eFileObjectStoreKey"] = objectKey;
            } else {
                status["e2eFileObjectStoreKeySuppressed"] = true;
            }
            status["e2eFileObjectStoreKeySafe"] = objectKeySafe;
            status["e2eFileObjectStoreType"] = storeType;
            status["e2eFileObjectStoreHash"] = state.value("e2eFileObjectStoreHash").toString();
            status["e2eFileObjectStoreSize"] = state.value("e2eFileObjectStoreSize").toString();
            const bool s3NotReviewed =
                storeType == QLatin1String("s3") && !e2eS3ObjectRecoveryReviewedEnabled();
            const bool offlineNotReviewed =
                storeType == QLatin1String("offline") && !e2eOfflineObjectRecoveryReviewedEnabled();
            const bool forceDefaultRecoveryDecision = !objectKeySafe || s3NotReviewed || offlineNotReviewed;
            status["e2eFileObjectRecoveryScope"] = forceDefaultRecoveryDecision
                ? e2eObjectRecoveryScopeForStoreType(storeType)
                : state.value("e2eFileObjectRecoveryScope")
                      .toString(e2eObjectRecoveryScopeForStoreType(storeType));
            status["e2eFileObjectRecoveryReviewGate"] = forceDefaultRecoveryDecision
                ? e2eObjectRecoveryReviewGateForStoreType(storeType, objectKeySafe)
                : state.value("e2eFileObjectRecoveryReviewGate")
                      .toString(e2eObjectRecoveryReviewGateForStoreType(storeType, objectKeySafe));
            status["e2eFileObjectRecoveryNoSensitiveLocatorExport"] = true;
            status["e2eFileObjectRecoveryReason"] = forceDefaultRecoveryDecision
                ? e2eObjectRecoveryDefaultReasonForStoreType(storeType, objectKeySafe)
                : state.value("e2eFileObjectRecoveryReason")
                      .toString(e2eObjectRecoveryDefaultReasonForStoreType(storeType, objectKeySafe));
            status["e2eFileObjectRecoveryAction"] = forceDefaultRecoveryDecision
                ? e2eObjectRecoveryDefaultActionForStoreType(storeType, objectKeySafe)
                : state.value("e2eFileObjectRecoveryAction")
                      .toString(e2eObjectRecoveryDefaultActionForStoreType(storeType, objectKeySafe));
            status["e2eFileObjectRecoveryMaterialPolicy"] =
                state.value("e2eFileObjectRecoveryMaterialPolicy")
                    .toString(QStringLiteral("object-ciphertext-only-no-secret-export"));
        }
    }

    bool e2eRecoverySessionReady = true;
    if (e2eFileEncrypted) {
        const auto sessionIt = m_e2eSessions.constFind(state.value("receiverId").toString().trimmed());
        const QString keyFingerprint = state.value("e2eFileKeyFingerprintSha256").toString().trimmed();
        e2eRecoverySessionReady =
            sessionIt != m_e2eSessions.constEnd()
            && sessionIt->keyId == state.value("e2eFileKeyId").toString().trimmed()
            && e2eFingerprint(sessionIt->sessionKey).compare(keyFingerprint, Qt::CaseInsensitive) == 0;
        status["e2eFileRecoverySessionReady"] = e2eRecoverySessionReady;
        if (!e2eRecoverySessionReady) {
            status["e2eFileRecoverySessionReason"] = QStringLiteral("e2e-file-session-mismatch");
        }
    }

    const bool e2eResumeCacheEvidenceReady =
        e2eFileEncrypted
        && recoveryMode == QLatin1String("resume")
        && state.value("e2eFileResumeCache").toBool(false)
        && state.value("e2eFileResumeCacheFormat").toString()
            == QString::fromLatin1(kE2EFileResumeCacheFormat)
        && state.value("e2eFileResumeCacheSha256").toString().trimmed().compare(
               state.value("e2eFileWireHash").toString().trimmed(), Qt::CaseInsensitive) == 0
        && state.value("e2eFileResumeCacheSize").toVariant().toLongLong()
            == state.value("e2eFileWireSize").toVariant().toLongLong()
        && e2eEnvelopeHeaderLooksSafe(state.value("e2eFileEnvelopeHeader").toObject());
    bool e2eResumeCacheReady = e2eResumeCacheEvidenceReady;
    if (e2eResumeCacheEvidenceReady) {
        QByteArray cachedWirePayload;
        e2eResumeCacheReady = loadE2EFileResumeCache(state.value("transferId").toString(),
                                                     state.value("e2eFileWireHash").toString(),
                                                     state.value("e2eFileWireSize").toVariant().toLongLong(),
                                                     &cachedWirePayload);
    }
    bool e2eObjectRecoveryReady = false;
    QString e2eObjectRecoveryRejectReason;
    if (!e2eResumeCacheReady
        && e2eFileEncrypted
        && recoveryMode == QLatin1String("resume")
        && state.value("e2eFileObjectRecoveryCandidate").toBool(false)) {
        QByteArray objectWirePayload;
        e2eObjectRecoveryReady = loadE2EFileObjectRecoveryPayload(state,
                                                                  &objectWirePayload,
                                                                  &e2eObjectRecoveryRejectReason);
        if (e2eObjectRecoveryReady) {
            status["e2eFileOfflineObjectRecoveryReady"] = true;
            status["e2eFileObjectRecoveryReason"] = QStringLiteral("e2e-file-object-recovery-ready");
            status["e2eFileObjectRecoveryAction"] = QStringLiteral("resume-object-wire-envelope");
        } else if (!e2eObjectRecoveryRejectReason.isEmpty()) {
            status["e2eFileObjectRecoveryReason"] = e2eObjectRecoveryRejectReason;
        }
    }

    if ((e2eResumeCacheReady || e2eObjectRecoveryReady) && e2eRecoverySessionReady) {
        status["recoveryMode"] = QStringLiteral("resume");
        status["canAutoResume"] = true;
        status["reason"] = e2eObjectRecoveryReady
            ? QStringLiteral("e2e-file-object-recovery-ready")
            : state.value("recoveryReason")
                  .toString(QStringLiteral("e2e-file-same-wire-cache-ready"));
        status["action"] = e2eObjectRecoveryReady
            ? QStringLiteral("resume-object-wire-envelope")
            : state.value("recoveryAction")
                  .toString(QStringLiteral("resume-same-wire-envelope"));
    } else if ((e2eResumeCacheReady || e2eObjectRecoveryReady) && !e2eRecoverySessionReady) {
        status["recoveryMode"] = QStringLiteral("resend");
        status["canAutoResume"] = false;
        status["reason"] = QStringLiteral("e2e-file-session-mismatch");
        status["action"] = QStringLiteral("reestablish-e2e-session-before-resume");
    } else if (e2eFileEncrypted
               && state.value("e2eFileObjectRecoveryCandidate").toBool(false)) {
        status["recoveryMode"] = QStringLiteral("resend");
        status["canAutoResume"] = false;
        status["reason"] = status.value("e2eFileObjectRecoveryReason")
            .toString(QStringLiteral("e2e-file-object-recovery-read-path-unavailable"));
        status["action"] = status.value("e2eFileObjectRecoveryAction")
            .toString(QStringLiteral("resend-or-wait-for-object-recovery"));
    } else if (recoveryMode == QLatin1String("resend") || e2eFileEncrypted) {
        status["recoveryMode"] = QStringLiteral("resend");
        status["canAutoResume"] = false;
        status["reason"] = e2eFileEncrypted && recoveryMode == QLatin1String("resume")
            ? QStringLiteral("e2e-file-resume-cache-unavailable")
            : state.value("recoveryReason").toString(QStringLiteral("e2e-file-resend-required"));
        status["action"] = e2eFileEncrypted && recoveryMode == QLatin1String("resume")
            ? QStringLiteral("resend-file")
            : state.value("recoveryAction").toString(QStringLiteral("resend-file"));
    } else {
        status["recoveryMode"] = QStringLiteral("resume");
        status["canAutoResume"] = true;
        status["reason"] = state.value("recoveryReason").toString(QStringLiteral("can-query-resume-state"));
        status["action"] = state.value("recoveryAction").toString(QStringLiteral("resume-transfer"));
    }
    return status;
}

bool Client::clearOutgoingTransferState() {
    const QString path = outgoingTransferStateFilePath();
    QJsonObject state;
    const bool hadState = loadOutgoingTransferState(&state);
    if (hadState) {
        removeE2EFileResumeCache(state.value("transferId").toString());
    }
    if (!QFile::exists(path)) {
        return true;
    }
    return QFile::remove(path);
}

bool Client::resumeSavedOutgoingTransfer(QString* rejectReason, int timeoutMs) {
    if (rejectReason) rejectReason->clear();

    QJsonObject state;
    if (!loadOutgoingTransferState(&state)) {
        if (rejectReason) *rejectReason = "未找到可恢复的发送任务";
        return false;
    }

    const QJsonObject recoveryStatus = savedOutgoingTransferRecoveryStatus();
    if (!recoveryStatus.value("canAutoResume").toBool(false)) {
        const QString reason = recoveryStatus.value("reason").toString(QStringLiteral("saved-transfer-requires-resend"));
        if (rejectReason) *rejectReason = reason;
        emit fileTransferStatusChanged(QFileInfo(state["filePath"].toString()).fileName(),
                                       state["transferId"].toString(),
                                       reason,
                                       0,
                                       state["fileSize"].toVariant().toLongLong(),
                                       QStringLiteral("outgoing"),
                                       state["filePath"].toString(),
                                       true);
        emit connectionError(QStringLiteral("未完成发送需要重新发送：%1").arg(reason));
        return false;
    }

    const MessageType messageType = static_cast<MessageType>(state["messageType"].toInt(-1));
    const bool e2eFileEncrypted = state.value("e2eFileEncrypted").toBool(false);
    if (e2eFileEncrypted) {
        const QString wireHash = state.value("e2eFileWireHash").toString().trimmed();
        const qint64 wireSize = state.value("e2eFileWireSize").toVariant().toLongLong();
        const qint64 wireChunkCount = state.value("chunkCount").toVariant().toLongLong();
        QByteArray recoveryWirePayload;
        QString recoveryRejectReason;
        if (!loadE2EFileRecoveryWirePayload(state, &recoveryWirePayload, &recoveryRejectReason)
            || state.value("fileHash").toString().trimmed().compare(wireHash, Qt::CaseInsensitive) != 0
            || state.value("fileSize").toVariant().toLongLong() != wireSize
            || state.value("chunkSize").toVariant().toLongLong() != kTransferChunkBytes
            || wireChunkCount != (wireSize + kTransferChunkBytes - 1) / kTransferChunkBytes
            || !e2eEnvelopeHeaderLooksSafe(state.value("e2eFileEnvelopeHeader").toObject())) {
            if (rejectReason) *rejectReason = recoveryRejectReason.isEmpty()
                ? QStringLiteral("e2e-file-resume-cache-unavailable")
                : recoveryRejectReason;
            return false;
        }
        const QString keyFingerprint = state.value("e2eFileKeyFingerprintSha256").toString().trimmed();
        const auto sessionIt = m_e2eSessions.constFind(state.value("receiverId").toString().trimmed());
        if (sessionIt == m_e2eSessions.constEnd()
            || sessionIt->keyId != state.value("e2eFileKeyId").toString().trimmed()
            || e2eFingerprint(sessionIt->sessionKey).compare(keyFingerprint, Qt::CaseInsensitive) != 0) {
            if (rejectReason) *rejectReason = QStringLiteral("e2e-file-session-mismatch");
            return false;
        }
    } else {
        qint64 localFileSize = 0;
        qint64 localChunkCount = 0;
        QString localFileHash;
        if (!collectFileTransferMetadata(state["filePath"].toString(), &localFileSize, &localChunkCount, &localFileHash)
            || state["fileSize"].toVariant().toLongLong() != localFileSize
            || state["chunkSize"].toVariant().toLongLong() != kTransferChunkBytes
            || state["chunkCount"].toVariant().toLongLong() != localChunkCount
            || state["fileHash"].toString().trimmed().compare(localFileHash, Qt::CaseInsensitive) != 0) {
            if (rejectReason) *rejectReason = "保存的发送任务与本地文件不一致";
            return false;
        }
    }

    QString resumeReason;
    const bool resumed = queryAndResumeFileTransfer(
        state["filePath"].toString(),
        state["transferId"].toString(),
        state["receiverId"].toString(),
        messageType,
        &resumeReason,
        timeoutMs,
        state.value("groupId").toString());
    if (!resumed) {
        if (rejectReason) *rejectReason = resumeReason.isEmpty() ? "发送任务恢复失败" : resumeReason;
        return false;
    }

    clearOutgoingTransferState();
    return true;
}

void Client::cancelCurrentOutgoingTransfer() {
    if (m_cancelOutgoingTransfer) return;
    m_cancelOutgoingTransfer = true;
    if (isConnected() && !m_currentOutgoingTransferId.isEmpty()) {
        QJsonObject obj;
        obj["type"] = "file_transfer_cancel";
        obj["transferId"] = m_currentOutgoingTransferId;
        obj["senderId"] = m_userId;
        obj["senderName"] = m_userName;
        obj["receiverId"] = m_currentOutgoingReceiverId;
        if (!m_currentOutgoingGroupId.isEmpty()) {
            obj["groupId"] = m_currentOutgoingGroupId;
        }
        obj["fileName"] = m_currentOutgoingFileName;
        sendJson(obj);
    }
    clearOutgoingTransferState();
    emit outgoingTransferCancelRequested();
}

bool Client::sendFilePayload(const QString& filePath,
                             const QString& receiverId,
                             MessageType messageType,
                             const QString& contentPrefix,
                             const QString& resumeTransferId,
                             qint64 resumeConfirmedBytes,
                             qint64 resumeNextChunkIndex,
                             const QVector<qint64>& resumeReceivedChunks,
                             const QString& serverGroupId) {
    if (!isConnected()) return false;
    const QString trimmedServerGroupId = serverGroupId.trimmed();
    const bool privateFileTarget = !receiverId.trimmed().isEmpty()
        && (messageType == MessageType::File || messageType == MessageType::Image);
    const bool allowPlaintextPrivateFile = envEnabled("QTNETWORKCHAT_E2E_ALLOW_PLAINTEXT_PRIVATE_FILE");
    const bool allowLegacyPlaintextPrivateImage = privateFileTarget
        && messageType == MessageType::Image
        && !hasE2ESession(receiverId);
    const bool e2eFileRequired = privateFileTarget
        && !allowLegacyPlaintextPrivateImage
        && (hasE2ESession(receiverId) || !allowPlaintextPrivateFile);
    const E2ESession* e2eFileSession = nullptr;
    QString e2eFileRejectReason;
    if (e2eFileRequired && !e2eFileSessionForPeer(receiverId, &e2eFileSession, &e2eFileRejectReason)) {
        emit connectionError(QStringLiteral("端到端加密文件发送失败：%1").arg(e2eFileRejectReason));
        return false;
    }
    m_cancelOutgoingTransfer = false;
    m_currentOutgoingTransferId.clear();
    m_lastOutgoingTransferId.clear();
    m_currentOutgoingReceiverId.clear();
    m_currentOutgoingGroupId.clear();
    m_currentOutgoingFileName.clear();
    struct OutgoingTransferCleanup {
        Client* client;
        ~OutgoingTransferCleanup() {
            client->m_currentOutgoingTransferId.clear();
            client->m_currentOutgoingReceiverId.clear();
            client->m_currentOutgoingGroupId.clear();
            client->m_currentOutgoingFileName.clear();
        }
    } cleanup{this};

    const bool resumeMode = !resumeTransferId.trimmed().isEmpty();
    const QString transferId = resumeMode
        ? resumeTransferId.trimmed()
        : QString("%1_%2_%3")
            .arg(m_userId,
                 QString::number(QDateTime::currentMSecsSinceEpoch()),
                 QString::number(QRandomGenerator::global()->generate()));
    QFileInfo fileInfo(filePath);
    QFile file(filePath);
    QString displayFileName = fileInfo.fileName();
    QString stateFilePath = fileInfo.absoluteFilePath();
    qint64 plainFileSize = 0;
    qint64 plainChunkCount = 0;
    QString plainFileHash;
    QJsonObject savedE2EResumeState;
    if (e2eFileRequired && resumeMode) {
        if (!loadOutgoingTransferState(&savedE2EResumeState)
            || savedE2EResumeState.value("transferId").toString().trimmed() != transferId
            || !savedE2EResumeState.value("e2eFileEncrypted").toBool(false)
            || savedE2EResumeState.value("receiverId").toString().trimmed() != receiverId.trimmed()) {
            emit connectionError(QStringLiteral("端到端加密文件续传失败：恢复状态与当前会话不匹配"));
            return false;
        }
        stateFilePath = savedE2EResumeState.value("filePath").toString().trimmed();
        displayFileName = QFileInfo(stateFilePath).fileName();
        plainFileSize = savedE2EResumeState.value("e2eFilePlainSize").toVariant().toLongLong();
        plainFileHash = savedE2EResumeState.value("e2eFilePlainHash").toString().trimmed();
        plainChunkCount = (plainFileSize + kTransferChunkBytes - 1) / kTransferChunkBytes;
        if (displayFileName.isEmpty()
            || plainFileSize <= 0
            || plainFileSize > kMaxOutgoingPayloadBytes
            || plainChunkCount <= 0
            || !isValidE2EFingerprint(plainFileHash)) {
            emit connectionError(QStringLiteral("端到端加密文件续传失败：恢复状态与当前会话不匹配"));
            return false;
        }
    } else {
        if (!fileInfo.exists() || !fileInfo.isFile() || fileInfo.size() <= 0 || fileInfo.size() > kMaxOutgoingPayloadBytes) {
            return false;
        }
        if (!file.open(QIODevice::ReadOnly)) return false;

        QCryptographicHash hasher(QCryptographicHash::Sha256);
        qint64 preparedBytes = 0;
        plainFileSize = fileInfo.size();
        plainChunkCount = (plainFileSize + kTransferChunkBytes - 1) / kTransferChunkBytes;
        emit fileTransferProgress(displayFileName, 0, plainFileSize, transferId);

        while (!file.atEnd()) {
            if (m_cancelOutgoingTransfer) {
                file.close();
                return false;
            }
            const QByteArray chunk = file.read(kTransferChunkBytes);
            if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
                file.close();
                return false;
            }
            hasher.addData(chunk);
            preparedBytes += chunk.size();
            emit fileTransferProgress(displayFileName, preparedBytes, plainFileSize, transferId);
            if (m_cancelOutgoingTransfer) {
                file.close();
                return false;
            }
        }
        file.close();
        plainFileHash = QString::fromLatin1(hasher.result().toHex());
    }
    const QString e2eFileKeyId = e2eFileRequired && e2eFileSession ? e2eFileSession->keyId : QString();
    const QString e2eFileKeyFingerprint = e2eFileRequired && e2eFileSession ? e2eFingerprint(e2eFileSession->sessionKey) : QString();
    QByteArray e2eWirePayload;
    E2EEnvelope e2eFileEnvelope;
    QJsonObject e2eFileEnvelopeHeader;
    QString wireFileHash = plainFileHash;
    qint64 wireFileSize = plainFileSize;
    qint64 wireChunkCount = plainChunkCount;
    if (e2eFileRequired) {
        if (resumeMode) {
            QString cacheRejectReason;
            if (savedE2EResumeState.value("e2eFileKeyId").toString().trimmed() != e2eFileKeyId
                || savedE2EResumeState.value("e2eFileKeyFingerprintSha256").toString().trimmed()
                    .compare(e2eFileKeyFingerprint, Qt::CaseInsensitive) != 0
                || savedE2EResumeState.value("e2eFilePlainSize").toVariant().toLongLong() != plainFileSize
                || savedE2EResumeState.value("e2eFilePlainHash").toString().trimmed()
                    .compare(plainFileHash, Qt::CaseInsensitive) != 0) {
                emit connectionError(QStringLiteral("端到端加密文件续传失败：恢复状态与当前会话不匹配"));
                file.close();
                return false;
            }
            wireFileSize = savedE2EResumeState.value("e2eFileWireSize").toVariant().toLongLong();
            wireChunkCount = savedE2EResumeState.value("chunkCount").toVariant().toLongLong();
            wireFileHash = savedE2EResumeState.value("e2eFileWireHash").toString().trimmed();
            if (!loadE2EFileRecoveryWirePayload(savedE2EResumeState,
                                                &e2eWirePayload,
                                                &cacheRejectReason)) {
                emit connectionError(QStringLiteral("端到端加密文件续传失败：%1").arg(cacheRejectReason));
                file.close();
                return false;
            }
            e2eFileEnvelopeHeader = savedE2EResumeState.value("e2eFileEnvelopeHeader").toObject();
            if (!e2eEnvelopeHeaderLooksSafe(e2eFileEnvelopeHeader)) {
                emit connectionError(QStringLiteral("端到端加密文件续传失败：信封恢复证据无效"));
                file.close();
                return false;
            }
            e2eFileEnvelope = E2EEnvelope::fromJson(e2eFileEnvelopeHeader);
            e2eFileEnvelope.ciphertext = e2eWirePayload;
            QString envelopeReason;
            if (!e2eFileEnvelope.isValid(&envelopeReason)
                || e2eFileEnvelope.senderId != m_userId
                || e2eFileEnvelope.receiverId != receiverId.trimmed()
                || e2eFileEnvelope.keyId != e2eFileKeyId
                || e2eFileEnvelopeHeader.value("ciphertextSha256").toString().trimmed()
                    .compare(wireFileHash, Qt::CaseInsensitive) != 0) {
                emit connectionError(QStringLiteral("端到端加密文件续传失败：信封恢复证据不匹配"));
                file.close();
                return false;
            }
        } else {
            QFile plainFile(stateFilePath);
            if (!plainFile.open(QIODevice::ReadOnly)) {
                file.close();
                return false;
            }
            const QByteArray plainPayload = plainFile.readAll();
            plainFile.close();
            if (plainPayload.size() != plainFileSize) {
                file.close();
                return false;
            }
            const QString aad = QStringLiteral("file/private/v1;%1;%2;%3;%4")
                .arg(transferId,
                     QString::number(plainFileSize),
                     plainFileHash,
                     displayFileName);
            QString encryptReason;
            e2eFileEnvelope = encryptE2EPayload(m_userId,
                                                receiverId.trimmed(),
                                                e2eFileKeyId,
                                                e2eFileSession->sessionKey,
                                                plainPayload,
                                                aad,
                                                &encryptReason);
            if (!e2eFileEnvelope.isValid(&encryptReason)) {
                emit connectionError(QStringLiteral("端到端加密文件发送失败：%1").arg(encryptReason));
                file.close();
                return false;
            }
            e2eWirePayload = e2eFileEnvelope.ciphertext;
            wireFileSize = e2eWirePayload.size();
            wireChunkCount = (wireFileSize + kTransferChunkBytes - 1) / kTransferChunkBytes;
            wireFileHash = sha256Hex(e2eWirePayload);
            e2eFileEnvelopeHeader = e2eEnvelopeHeaderJson(e2eFileEnvelope);
        }
    }
    QSet<qint64> receivedChunkIndexes;
    qint64 resolvedResumeNextChunkIndex = resumeNextChunkIndex;
    if (resumeMode) {
        QVector<qint64> resumeProgressChunks = resumeReceivedChunks;
        if (resumeProgressChunks.isEmpty()) {
            for (qint64 index = 0; index < resumeNextChunkIndex; ++index) {
                resumeProgressChunks.append(index);
            }
        }
        if (!resolveResumeProgress(resumeConfirmedBytes,
                                   resumeNextChunkIndex,
                                   resumeProgressChunks,
                                   wireFileSize,
                                   wireChunkCount,
                                   &receivedChunkIndexes,
                                   &resolvedResumeNextChunkIndex)) {
            return false;
        }
        if (resolvedResumeNextChunkIndex == wireChunkCount && resumeConfirmedBytes != wireFileSize) {
            return false;
        }
    }
    emit fileTransferPrepared(displayFileName, plainFileSize, kTransferChunkBytes, plainChunkCount, plainFileHash);
    if (m_cancelOutgoingTransfer) return false;

    if (!e2eFileRequired && !file.open(QIODevice::ReadOnly)) return false;
    m_currentOutgoingTransferId = transferId;
    m_lastOutgoingTransferId = transferId;
    m_currentOutgoingReceiverId = receiverId;
    m_currentOutgoingGroupId = trimmedServerGroupId;
    m_currentOutgoingFileName = displayFileName;
    emit fileTransferStatusChanged(displayFileName,
                                   transferId,
                                   resumeMode ? QStringLiteral("transfer-resumed") : QStringLiteral("transfer-prepared"),
                                   resumeMode ? resumeConfirmedBytes : 0,
                                   plainFileSize,
                                   QStringLiteral("outgoing"),
                                   stateFilePath,
                                   false);
    if (!e2eFileRequired
        && !saveOutgoingTransferState(transferId,
                                      stateFilePath,
                                      receiverId,
                                      messageType,
                                      wireFileHash,
                                      wireFileSize,
                                      wireChunkCount,
                                      QJsonObject(),
                                      trimmedServerGroupId)) {
        file.close();
        return false;
    }
    if (e2eFileRequired && !resumeMode) {
        QJsonObject recoveryPolicy;
        QString cachePath;
        const bool cacheReady =
            writeE2EFileResumeCache(transferId, e2eWirePayload, wireFileHash, &cachePath);
        recoveryPolicy["recoveryMode"] = cacheReady ? QStringLiteral("resume") : QStringLiteral("resend");
        recoveryPolicy["recoveryReason"] = cacheReady
            ? QStringLiteral("e2e-file-same-wire-cache-ready")
            : QStringLiteral("e2e-file-resend-required");
        recoveryPolicy["recoveryAction"] = cacheReady
            ? QStringLiteral("resume-same-wire-envelope")
            : QStringLiteral("resend-file");
        recoveryPolicy["e2eFileEncrypted"] = true;
        recoveryPolicy["e2eFileKeyId"] = e2eFileKeyId;
        recoveryPolicy["e2eFileKeyFingerprintSha256"] = e2eFileKeyFingerprint;
        recoveryPolicy["e2eFilePlainSize"] = QString::number(plainFileSize);
        recoveryPolicy["e2eFilePlainHash"] = plainFileHash;
        recoveryPolicy["e2eFileWireSize"] = QString::number(wireFileSize);
        recoveryPolicy["e2eFileWireHash"] = wireFileHash;
        if (cacheReady) {
            recoveryPolicy["e2eFileResumeCache"] = true;
            recoveryPolicy["e2eFileResumeCacheFormat"] =
                QString::fromLatin1(kE2EFileResumeCacheFormat);
            recoveryPolicy["e2eFileResumeCacheSha256"] = wireFileHash;
            recoveryPolicy["e2eFileResumeCacheSize"] = QString::number(wireFileSize);
            recoveryPolicy["e2eFileEnvelopeHeader"] = e2eFileEnvelopeHeader;
        }
        if (!saveOutgoingTransferState(transferId,
                                       stateFilePath,
                                       receiverId,
                                       messageType,
                                       wireFileHash,
                                       wireFileSize,
                                       wireChunkCount,
                                       recoveryPolicy,
                                       trimmedServerGroupId)) {
            removeE2EFileResumeCache(transferId);
            file.close();
            return false;
        }
    }
    qint64 sentBytes = resumeMode ? resumeConfirmedBytes : 0;
    qint64 chunkIndex = resumeMode ? resolvedResumeNextChunkIndex : 0;
    emit fileTransferProgress(displayFileName, sentBytes, plainFileSize, transferId);
    if (chunkIndex == wireChunkCount) {
        file.close();
        const bool completed = sentBytes == wireFileSize;
        if (completed) {
            clearOutgoingTransferState();
            emit fileTransferStatusChanged(displayFileName,
                                           transferId,
                                           QStringLiteral("transfer-completed"),
                                           sentBytes,
                                           plainFileSize,
                                           QStringLiteral("outgoing"),
                                           stateFilePath,
                                           true);
        }
        return completed;
    }
    const qint64 startOffset = chunkIndex * kTransferChunkBytes;
    if (startOffset > wireFileSize || (!e2eFileRequired && !file.seek(startOffset))) {
        file.close();
        return false;
    }

    bool resumeAfterAckTimeoutUsed = false;
    while (chunkIndex < wireChunkCount) {
        if (m_cancelOutgoingTransfer) {
            file.close();
            return false;
        }
        const QByteArray chunk = e2eFileRequired
            ? e2eWirePayload.mid(static_cast<int>(chunkIndex * kTransferChunkBytes),
                                 static_cast<int>(qMin(kTransferChunkBytes, wireFileSize - chunkIndex * kTransferChunkBytes)))
            : file.read(kTransferChunkBytes);
        if (chunk.isEmpty()) {
            file.close();
            return false;
        }
        // Resume state is scoped to this transfer and records acknowledged chunk indexes.
        // Skipping them avoids replaying accepted bytes after an interrupted connection.
        if (receivedChunkIndexes.contains(chunkIndex)) {
            sentBytes = qMax(sentBytes, qMin(wireFileSize, (chunkIndex + 1) * kTransferChunkBytes));
            ++chunkIndex;
            emit fileTransferProgress(displayFileName, sentBytes, plainFileSize, transferId);
            continue;
        }

        QJsonObject obj;
        obj["type"] = "file_chunk";
        obj["transferId"] = transferId;
        obj["senderId"] = m_userId;
        obj["senderName"] = m_userName;
        obj["receiverId"] = receiverId;
        if (!trimmedServerGroupId.isEmpty()) {
            obj["groupId"] = trimmedServerGroupId;
        }
        obj["messageType"] = static_cast<int>(messageType);
        obj["fileName"] = displayFileName;
        obj["fileSize"] = QString::number(wireFileSize);
        obj["fileHash"] = wireFileHash;
        obj["chunkSize"] = QString::number(kTransferChunkBytes);
        obj["chunkCount"] = QString::number(wireChunkCount);
        obj["chunkIndex"] = QString::number(chunkIndex);
        obj["content"] = contentPrefix + displayFileName;
        if (e2eFileRequired) {
            obj["e2eEnvelope"] = e2eFileEnvelopeHeader;
            obj["isEncrypted"] = true;
            obj["e2eFileEncrypted"] = true;
            obj["e2eFileKeyId"] = e2eFileKeyId;
            obj["e2eFileKeyFingerprintSha256"] = e2eFileKeyFingerprint;
            obj["e2eFilePlainSize"] = QString::number(plainFileSize);
            obj["e2eFilePlainHash"] = plainFileHash;
        }
        obj["fileData"] = QString::fromLatin1(chunk.toBase64());

        QString ackRejectReason;
        qint64 ackReceivedBytes = 0;
        bool acknowledged = false;
        bool advancedByResumeState = false;
        for (int attempt = 1; attempt <= kChunkSendMaxAttempts; ++attempt) {
            if (m_cancelOutgoingTransfer) {
                file.close();
                return false;
            }
            if (!sendJson(obj)) {
                file.close();
                return false;
            }
            if (waitForFileChunkAck(transferId, chunkIndex, &ackRejectReason, &ackReceivedBytes)) {
                const qint64 expectedAckBytes = qMin(wireFileSize, chunkIndex * kTransferChunkBytes + chunk.size());
                if (ackReceivedBytes > 0 && (ackReceivedBytes < expectedAckBytes || ackReceivedBytes > wireFileSize)) {
                    if (attempt == kChunkSendMaxAttempts) {
                        ackRejectReason = QString::fromUtf8("文件分片确认进度非法");
                    }
                    continue;
                }
                acknowledged = true;
                break;
            }
            if (m_cancelOutgoingTransfer) {
                file.close();
                return false;
            }
            if (!ackRejectReason.isEmpty()) {
                if (isRetriableFileChunkRejectReason(ackRejectReason) && attempt < kChunkSendMaxAttempts) {
                    emit fileTransferStatusChanged(displayFileName,
                                                   transferId,
                                                   ackRejectReason,
                                                   sentBytes,
                                                   plainFileSize,
                                                   QStringLiteral("outgoing"),
                                                   stateFilePath,
                                                   false);
                    emit connectionError(fileTransferUserMessage(ackRejectReason,
                        QString("文件分片暂时被拒绝，正在重试：%1").arg(ackRejectReason)) + QStringLiteral("，正在重试"));
                    ackRejectReason.clear();
                    continue;
                }
                emit fileTransferStatusChanged(displayFileName,
                                               transferId,
                                               ackRejectReason,
                                               sentBytes,
                                               plainFileSize,
                                               QStringLiteral("outgoing"),
                                               stateFilePath,
                                               true);
                emit connectionError(fileTransferUserMessage(ackRejectReason,
                    QString("文件分片发送被拒绝：%1").arg(ackRejectReason)));
                file.close();
                return false;
            }
            if (!resumeAfterAckTimeoutUsed) {
                resumeAfterAckTimeoutUsed = true;
                qint64 resumeConfirmedBytes = 0;
                qint64 resumeNextChunkIndex = 0;
                qint64 resumeFileSize = 0;
                qint64 resumeChunkSize = 0;
                qint64 resumeChunkCount = 0;
                QVector<qint64> resumeReceivedChunks;
                QSet<qint64> resumeReceivedChunkSet;
                qint64 firstMissingChunkIndex = 0;
                QString resumeFileHash;
                QString resumeRejectReason;
                if (queryFileTransferResumeState(transferId,
                                                 &resumeConfirmedBytes,
                                                 &resumeNextChunkIndex,
                                                 &resumeReceivedChunks,
                                                 &resumeRejectReason,
                                                 kChunkAckTimeoutMs,
                                                 &resumeFileSize,
                                                 &resumeChunkSize,
                                                 &resumeChunkCount,
                                                 &resumeFileHash)) {
                    if (resumeFileSize == wireFileSize
                        && resumeChunkSize == kTransferChunkBytes
                        && resumeChunkCount == wireChunkCount
                        && resumeFileHash.compare(wireFileHash, Qt::CaseInsensitive) == 0
                        && resolveResumeProgress(resumeConfirmedBytes,
                                                  resumeNextChunkIndex,
                                                  resumeReceivedChunks,
                                                  wireFileSize,
                                                  wireChunkCount,
                                                  &resumeReceivedChunkSet,
                                                  &firstMissingChunkIndex)) {
                        receivedChunkIndexes = resumeReceivedChunkSet;
                        if (firstMissingChunkIndex > chunkIndex) {
                            if (firstMissingChunkIndex == wireChunkCount) {
                                sentBytes = wireFileSize;
                                chunkIndex = wireChunkCount;
                                emit fileTransferProgress(displayFileName, sentBytes, plainFileSize, transferId);
                                advancedByResumeState = true;
                                acknowledged = true;
                                break;
                            }
                            const qint64 resumeOffset = firstMissingChunkIndex * kTransferChunkBytes;
                            if (resumeOffset > wireFileSize || (!e2eFileRequired && !file.seek(resumeOffset))) {
                                continue;
                            }
                            sentBytes = qMax(sentBytes,
                                             receivedBytesFromChunks(resumeReceivedChunkSet,
                                                                     wireFileSize,
                                                                     wireChunkCount));
                            chunkIndex = firstMissingChunkIndex;
                            emit fileTransferProgress(displayFileName, sentBytes, plainFileSize, transferId);
                            advancedByResumeState = true;
                            acknowledged = true;
                            break;
                        }
                    }
                }
            }
        }
        if (advancedByResumeState) {
            if (chunkIndex == wireChunkCount) {
                file.close();
                const bool completed = sentBytes == wireFileSize;
                if (completed) {
                    clearOutgoingTransferState();
                    emit fileTransferStatusChanged(displayFileName,
                                                   transferId,
                                                   QStringLiteral("transfer-completed"),
                                                   sentBytes,
                                                   plainFileSize,
                                                   QStringLiteral("outgoing"),
                                                   stateFilePath,
                                                   true);
                }
                return completed;
            }
            continue;
        }
        if (!acknowledged) {
            emit fileTransferStatusChanged(displayFileName,
                                           transferId,
                                           QStringLiteral("chunk-ack-timeout"),
                                           sentBytes,
                                           plainFileSize,
                                           QStringLiteral("outgoing"),
                                           stateFilePath,
                                           true);
            emit connectionError(fileTransferUserMessage(QStringLiteral("chunk-ack-timeout"),
                QString("文件分片发送超时：%1 第 %2/%3 片").arg(displayFileName).arg(chunkIndex + 1).arg(wireChunkCount)));
            file.close();
            return false;
        }
        const qint64 nextSentBytes = sentBytes + chunk.size();
        sentBytes = ackReceivedBytes > 0
            ? qBound<qint64>(sentBytes, ackReceivedBytes, wireFileSize)
            : nextSentBytes;
        receivedChunkIndexes.insert(chunkIndex);
        if (e2eFileRequired) {
            markE2EFileChunkSent(receiverId);
        }
        ++chunkIndex;
        emit fileTransferProgress(displayFileName, sentBytes, plainFileSize, transferId);
    }
    file.close();
    const bool completed = sentBytes == wireFileSize && chunkIndex == wireChunkCount;
    if (completed) {
        clearOutgoingTransferState();
        emit fileTransferStatusChanged(displayFileName,
                                       transferId,
                                       QStringLiteral("transfer-completed"),
                                       sentBytes,
                                       plainFileSize,
                                       QStringLiteral("outgoing"),
                                       stateFilePath,
                                       true);
    }
    return completed;
}

void Client::onReadyRead() {
    if (m_socket) {
        processIncomingData(m_socket->readAll());
    }
}

void Client::processIncomingData(const QByteArray& data, bool completeTextMessage) {
    QByteArray incoming = data;
    if (completeTextMessage && !incoming.endsWith('\n')) {
        incoming.append('\n');
    }
    m_buffer.append(incoming);

    while (m_buffer.contains('\n')) {
        int newlineIndex = m_buffer.indexOf('\n');
        QByteArray line = m_buffer.left(newlineIndex);
        m_buffer = m_buffer.mid(newlineIndex + 1);

        if (line.isEmpty()) continue;

        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isNull() || !doc.isObject()) continue;

        handleServerMessage(doc.object());
    }
}

void Client::onWebSocketConnected() {
    const QString pinnedFingerprint = configuredPinnedTlsFingerprint();
    if (m_transportName == QStringLiteral("wss") && !pinnedFingerprint.isEmpty()) {
        QString actualFingerprint;
        if (!pinnedCertificateFingerprintMatches(
                m_webSocket->sslConfiguration().peerCertificate(),
                pinnedFingerprint,
                &actualFingerprint)) {
            m_loginError = QStringLiteral("TLS 证书指纹不匹配: expected=%1 actual=%2")
                .arg(normalizedSha256Fingerprint(pinnedFingerprint),
                     actualFingerprint.isEmpty()
                        ? QStringLiteral("unavailable")
                        : actualFingerprint);
            m_webSocketPinAccepted = false;
            m_webSocket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                               QStringLiteral("certificate pin mismatch"));
            emit connectionError(QStringLiteral("TLS 证书指纹不匹配，已断开连接"));
            return;
        }
    }
    m_webSocketPinAccepted = true;
    onConnected();
}

void Client::onConnected() {
    qDebug() << "Connected to server";
    m_reconnectAttempts = 0;
    sendLogin();
    m_heartbeatTimer->start(30000);
    m_transferCleanupTimer->start(kTransferCleanupIntervalMs);
    emit connected();
}

void Client::onTlsEncrypted() {
    QSslSocket* sslSocket = qobject_cast<QSslSocket*>(m_socket);
    if (!sslSocket) return;
    const QString pinnedFingerprint = configuredPinnedTlsFingerprint();
    if (!pinnedFingerprint.isEmpty()) {
        QString actualFingerprint;
        if (!pinnedCertificateFingerprintMatches(sslSocket->peerCertificate(),
                                                 pinnedFingerprint,
                                                 &actualFingerprint)) {
            m_loginError = QStringLiteral("TLS 证书指纹不匹配: expected=%1 actual=%2")
                .arg(normalizedSha256Fingerprint(pinnedFingerprint),
                     actualFingerprint.isEmpty() ? QStringLiteral("unavailable") : actualFingerprint);
            m_tcpTlsPinAccepted = false;
            sslSocket->abort();
            emit connectionError(QStringLiteral("TLS 证书指纹不匹配，已断开连接"));
            return;
        }
    }
    m_tcpTlsPinAccepted = true;
    onConnected();
}

void Client::onDisconnected() {
    m_tcpTlsPinAccepted = false;
    m_heartbeatTimer->stop();
    m_transferCleanupTimer->stop();
    m_incomingFileTransfers.clear();
    qDebug() << "Disconnected from server";
    emit disconnected();
}

void Client::onError(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError)
    const QString errorMsg = m_socket
        ? m_socket->errorString()
        : QStringLiteral("未知 TCP 错误");
    qWarning() << "Socket error:" << errorMsg;
    emit connectionError(errorMsg);
}

void Client::onWebSocketError(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError)
    const QString errorMsg = m_webSocket
        ? m_webSocket->errorString()
        : QStringLiteral("未知 WebSocket 错误");
    if (m_loginError.isEmpty()) {
        m_loginError = QStringLiteral("WebSocket 连接失败: %1").arg(errorMsg);
    }
    qWarning() << "WebSocket error:" << errorMsg;
    emit connectionError(errorMsg);
}

void Client::onHeartbeat() {
    QJsonObject obj;
    obj["type"] = "heartbeat";
    sendJson(obj);
}

void Client::sendLogin() {
    qInfo().noquote() << QStringLiteral("Login request: account=%1 mode=%2 passwordLength=%3")
                            .arg(m_account, m_loginMode)
                            .arg(m_password.size());
    QJsonObject obj;
    obj["type"] = "login";
    obj["mode"] = m_loginMode;
    obj["account"] = m_account;
    obj["password"] = m_password;
    obj["userName"] = m_userName;
    if (!m_avatarBase64.isEmpty()) {
        obj["avatar"] = m_avatarBase64;
    }
    sendJson(obj);
}

bool Client::sendJson(const QJsonObject& obj) {
    if (!isConnected()) return false;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    data.append('\n');
    if (m_webSocket) {
        return m_webSocket->sendBinaryMessage(data) == data.size();
    }
    qint64 totalWritten = 0;
    while (totalWritten < data.size()) {
        const qint64 written = m_socket->write(data.constData() + totalWritten,
                                               data.size() - totalWritten);
        if (written < 0) {
            return false;
        }
        if (written == 0) {
            if (!m_socket->waitForBytesWritten(100)) {
                return false;
            }
            continue;
        }
        totalWritten += written;
    }
    m_socket->flush();
    return true;
}

bool Client::sendFileChunkAck(const QString& transferId,
                              qint64 chunkIndex,
                              bool accepted,
                              const QString& reason,
                              qint64 receivedBytes) {
    if (transferId.isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "file_chunk_ack";
    obj["transferId"] = transferId;
    obj["chunkIndex"] = QString::number(chunkIndex);
    obj["accepted"] = accepted;
    obj["reason"] = reason;
    obj["receivedBytes"] = QString::number(receivedBytes);
    return sendJson(obj);
}

void Client::handleServerMessage(const QJsonObject& obj) {
    QString type = obj["type"].toString();
    qDebug() << "Server message type:" << type;

    if (type == "login_success") {
        m_userId = obj["userId"].toString();
        m_userName = obj["userName"].toString();
        m_loginFinished = true;
        m_loginOk = true;
        m_loginWasRegister = obj["registered"].toBool(false);
        qDebug() << "Login success, userId:" << m_userId;
        emit loginSucceeded();
        announceE2EIdentity();
        return;
    }

    if (type == "login_failed") {
        m_loginFinished = true;
        m_loginOk = false;
        m_loginError = obj["reason"].toString("登录失败");
        emit loginFailed(m_loginError);
        emit connectionError(m_loginError);
        disconnectFromServer();
        return;
    }

    if (type == "userlist") {
        QJsonArray usersArray = obj["users"].toArray();
        m_onlineUsers.clear();
        for (const QJsonValue& val : usersArray) {
            QJsonObject u = val.toObject();
            ChatUser user;
            user.id = u["id"].toString();
            user.name = u["name"].toString();
            user.avatar = u["avatar"].toString();
            user.isOnline = u["online"].toBool();
            m_onlineUsers.append(user);
        }
        emit userListUpdated(m_onlineUsers);
        refreshFriendPresenceFromOnlineUsers();
        return;
    }

    if (type == "friend_list") {
        QVector<ChatUser> friends;
        const QJsonArray friendsArray = obj["friends"].toArray();
        friends.reserve(friendsArray.size());
        for (const QJsonValue& value : friendsArray) {
            const QJsonObject friendObject = value.toObject();
            const QString friendId = friendObject["id"].toString().trimmed();
            if (friendId.isEmpty() || friendId == m_userId) {
                continue;
            }

            ChatUser friendUser;
            friendUser.id = friendId;
            friendUser.name = friendObject["name"].toString().trimmed();
            if (friendUser.name.isEmpty()) {
                friendUser.name = friendId;
            }
            friendUser.avatar = friendObject["avatar"].toString();
            friendUser.isOnline = friendObject["online"].toBool(false);
            friendUser.lastActive = QDateTime::fromString(friendObject["lastActive"].toString(), Qt::ISODateWithMs);
            if (!friendUser.lastActive.isValid()) {
                friendUser.lastActive = QDateTime::fromString(friendObject["lastActive"].toString(), Qt::ISODate);
            }
            friends.append(friendUser);
        }
        m_friends = friends;
        refreshFriendPresenceFromOnlineUsers();
        emit friendListUpdated(m_friends);
        return;
    }

    if (type == "e2e_identity_announce") {
        const QString senderId = obj.value("senderId").toString().trimmed();
        const QString receiverId = obj.value("receiverId").toString().trimmed();
        const QJsonObject identity = obj.value("e2eIdentity").toObject();
        QString reason;
        if (senderId.isEmpty()
            || senderId == m_userId
            || (!receiverId.isEmpty() && receiverId != m_userId)
            || identity.value("userId").toString().trimmed() != senderId
            || !validateE2EIdentityJson(identity, &reason)) {
            emit connectionError(QStringLiteral("端到端加密身份公告无效：%1")
                .arg(reason.isEmpty() ? QStringLiteral("identity-mismatch") : reason));
            return;
        }

        const QByteArray publicKey = fromBase64Url(identity.value("publicKey").toString());
        const QString fingerprint = e2eFingerprint(publicKey);
        E2EPeerIdentity& peerIdentity = m_e2ePeerIdentities[senderId];
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (peerIdentity.firstSeenAtMs <= 0) {
            peerIdentity.firstSeenAtMs = nowMs;
        }
        peerIdentity.lastSeenAtMs = nowMs;
        peerIdentity.publicKey = publicKey;
        peerIdentity.fingerprint = fingerprint;
        peerIdentity.verificationCode = formatE2EVerificationCode(e2eFingerprint(m_e2eIdentityPublicKey), fingerprint);
        applyE2EStoredTrustPin(senderId, &peerIdentity);
        peerIdentity.fingerprintMismatch = peerIdentity.pinned
            && !peerIdentity.pinnedFingerprint.isEmpty()
            && peerIdentity.pinnedFingerprint != fingerprint;
        if (peerIdentity.fingerprintMismatch) {
            peerIdentity.verified = false;
            peerIdentity.verifiedAtMs = 0;
        }
        emit e2eIdentityStateChanged(senderId, e2ePeerIdentityStatus(senderId));
        if (peerIdentity.fingerprintMismatch) {
            emit connectionError(QStringLiteral("端到端加密身份指纹变化：%1").arg(senderId));
        }
        return;
    }

    if (type == "e2e_key_rotation_request" || type == "e2e_key_rotation_response") {
        const E2EKeyAgreement agreement = E2EKeyAgreement::fromJson(obj.value("e2eKeyAgreement").toObject());
        QString reason;
        const QString senderId = obj.value("senderId").toString();
        const QString receiverId = obj.value("receiverId").toString();
        if (!agreement.isValid(&reason)
            || senderId != agreement.senderId
            || receiverId != agreement.receiverId
            || receiverId != m_userId
            || !validateIncomingE2EAgreementIdentity(agreement, &reason)) {
            emit connectionError(QStringLiteral("端到端加密轮换消息无效：%1")
                .arg(reason.isEmpty() ? QStringLiteral("identity-mismatch") : reason));
            return;
        }

        if (type == "e2e_key_rotation_request") {
            E2EPendingAgreement pending;
            pending.agreement = agreement;
            pending.createdAtMs = QDateTime::currentMSecsSinceEpoch();
            m_e2ePendingIncomingAgreements[senderId] = pending;
            emit e2eSessionRotationRequested(senderId, agreement.toJson());
        } else {
            const bool accepted = obj.value("accepted").toBool(false);
            if (accepted) {
                const auto pendingIt = m_e2ePendingOutgoingAgreements.constFind(senderId);
                if (pendingIt == m_e2ePendingOutgoingAgreements.constEnd()) {
                    emit connectionError(QStringLiteral("端到端加密轮换消息无效：missing-pending-agreement"));
                    return;
                }
                QString deriveReason;
                const QByteArray sessionKey = deriveE2EAuthenticatedSessionKey(pendingIt->privateKey,
                                                                               pendingIt->agreement,
                                                                               agreement,
                                                                               &deriveReason);
                if (sessionKey.isEmpty()) {
                    emit connectionError(QStringLiteral("端到端加密轮换消息无效：%1").arg(deriveReason));
                    return;
                }
                installE2EDerivedSession(senderId, agreement.keyId, sessionKey);
                m_e2ePendingOutgoingAgreements.remove(senderId);
            }
            emit e2eSessionRotationResponded(senderId,
                                             agreement.toJson(),
                                             accepted,
                                             obj.value("reason").toString());
        }
        return;
    }

    if (type == "message" || type == "private") {
        Message msg;
        msg.type = static_cast<MessageType>(obj["messageType"].toInt());
        msg.senderId = obj["senderId"].toString();
        msg.senderName = obj["senderName"].toString();
        msg.senderAvatar = obj["senderAvatar"].toString();
        msg.receiverId = obj["receiverId"].toString();
        msg.content = obj["content"].toString();
        msg.clientMessageId = obj["clientMessageId"].toString();
        msg.timestamp = QDateTime::currentDateTime();
        if (obj.value("e2eEnvelope").isObject()) {
            const E2EEnvelope envelope = E2EEnvelope::fromJson(obj.value("e2eEnvelope").toObject());
            QString reason;
            if (envelope.isValid(&reason)) {
                msg.e2eEnvelope = envelope;
                auto sessionIt = m_e2eSessions.find(msg.senderId);
                QString plaintext;
                if (sessionIt != m_e2eSessions.constEnd()
                    && sessionIt->keyId == envelope.keyId
                    && decryptE2EText(envelope, sessionIt->sessionKey, &plaintext, &reason)) {
                    msg.content = plaintext;
                    ++sessionIt->decryptedMessages;
                    emit e2eSessionStateChanged(msg.senderId, e2eSessionStatus(msg.senderId));
                } else {
                    msg.content = QStringLiteral("加密消息无法解密");
                    emit connectionError(QStringLiteral("端到端加密消息无法解密：%1")
                        .arg(reason.isEmpty() ? QStringLiteral("missing-session") : reason));
                }
            } else {
                msg.content = QStringLiteral("加密消息格式无效");
                emit connectionError(QStringLiteral("端到端加密消息格式无效：%1").arg(reason));
            }
        }
        emit newMessage(msg);
        return;
    }

    if (type == "file") {
        Message msg;
        msg.type = static_cast<MessageType>(obj["messageType"].toInt(static_cast<int>(MessageType::File)));
        msg.senderId = obj["senderId"].toString();
        msg.senderName = obj["senderName"].toString();
        msg.senderAvatar = obj["senderAvatar"].toString();
        msg.receiverId = obj["receiverId"].toString();
        msg.content = obj["content"].toString();
        msg.fileName = obj["fileName"].toString();
        msg.transferId = obj["transferId"].toString();
        msg.fileSize = obj["fileSize"].toVariant().toLongLong();
        msg.fileHash = obj["fileHash"].toString();
        msg.chunkSize = obj["chunkSize"].toVariant().toLongLong();
        msg.chunkCount = obj["chunkCount"].toVariant().toLongLong();
        msg.e2eFileEncrypted = obj["e2eFileEncrypted"].toBool(false);
        msg.e2eFileKeyId = obj["e2eFileKeyId"].toString();
        msg.e2eFileKeyFingerprint = obj["e2eFileKeyFingerprintSha256"].toString();
        msg.e2eFilePlainSize = obj["e2eFilePlainSize"].toVariant().toLongLong();
        msg.e2eFilePlainHash = obj["e2eFilePlainHash"].toString();
        msg.timestamp = QDateTime::currentDateTime();
        QString base64Data = obj["fileData"].toString();
        if (!base64Data.isEmpty()) {
            msg.fileData = QByteArray::fromBase64(base64Data.toLatin1());
        }
        emit newMessage(msg);
        return;
    }

    if (type == "server_group_message") {
        Message msg;
        msg.type = MessageType::Text;
        msg.senderId = obj["senderId"].toString();
        msg.senderName = obj["senderName"].toString();
        msg.senderAvatar = obj["senderAvatar"].toString();
        msg.receiverId = obj["groupId"].toString(obj["receiverId"].toString());
        msg.content = obj["content"].toString();
        msg.clientMessageId = obj["clientMessageId"].toString();
        if (msg.clientMessageId.isEmpty()) {
            msg.clientMessageId = obj["messageId"].toString(obj["id"].toString());
        }
        msg.timestamp = QDateTime::currentDateTime();
        emit newMessage(msg);
        return;
    }

    if (type == "file_chunk") {
        handleIncomingFileChunk(obj);
        return;
    }

    if (type == "file_chunk_ack") {
        emit fileChunkAckReceived(
            obj["transferId"].toString(),
            obj["chunkIndex"].toVariant().toLongLong(),
            obj["accepted"].toBool(false),
            obj["reason"].toString(),
            obj["receivedBytes"].toVariant().toLongLong());
        return;
    }

    if (type == "file_transfer_resume_state") {
        QVector<qint64> receivedChunks;
        const QJsonArray chunks = obj["receivedChunks"].toArray();
        receivedChunks.reserve(chunks.size());
        for (const QJsonValue& value : chunks) {
            receivedChunks.append(value.toVariant().toLongLong());
        }
        emit fileTransferResumeStateReceived(
            obj["transferId"].toString(),
            obj["canResume"].toBool(false),
            obj["confirmedBytes"].toVariant().toLongLong(),
            obj["nextChunkIndex"].toVariant().toLongLong(),
            obj["fileSize"].toVariant().toLongLong(),
            obj["chunkSize"].toVariant().toLongLong(),
            obj["chunkCount"].toVariant().toLongLong(),
            obj["fileHash"].toString(),
            receivedChunks,
            obj["reason"].toString());
        return;
    }

    if (type == "server_group_snapshot") {
        m_serverGroups = obj["groups"].toArray();
        m_removedServerGroups = obj["removedGroups"].toArray();
        m_hasServerGroupSnapshot = true;
        emit serverGroupSnapshotReceived(m_serverGroups);
        return;
    }

    if (type == "server_group_member_updated") {
        emit serverGroupMemberUpdated(obj["groupId"].toString(),
                                      obj["memberId"].toString(),
                                      obj["action"].toString());
        return;
    }

    if (type == "server_group_search_results") {
        emit serverGroupSearchResults(obj["keyword"].toString(), obj["groups"].toArray());
        return;
    }

    if (type == "server_group_join_application") {
        emit serverGroupJoinApplicationReceived(obj);
        return;
    }

    if (type == "server_group_join_request_status") {
        emit serverGroupJoinRequestStatusReceived(obj);
        return;
    }

    if (type == "server_group_leave_status") {
        emit serverGroupLeaveStatusReceived(obj);
        return;
    }

    if (type == "server_group_dissolve_status") {
        emit serverGroupDissolveStatusReceived(obj);
        return;
    }

    if (type == "group_essence_updated") {
        emit serverGroupEssenceUpdated(obj);
        return;
    }

    if (type == "message_favorite_updated") {
        emit messageFavoriteUpdated(obj);
        return;
    }

    if (type == "favorite_messages_snapshot") {
        emit favoriteMessagesSnapshotReceived(obj.value("favorites").toArray());
        return;
    }

    if (type == "group_message_recalled") {
        emit serverGroupMessageRecalled(obj);
        return;
    }

    if (type == "group_member_muted") {
        emit serverGroupMemberMuted(obj);
        return;
    }

    if (type == "group_member_unmuted") {
        emit serverGroupMemberUnmuted(obj);
        return;
    }

    if (type == "server_group_member_profile") {
        emit serverGroupMemberProfileReceived(obj);
        return;
    }

    if (type == "error") {
        const QString code = obj.value("code").toString();
        const QString message = obj.value("message").toString(code.isEmpty() ? QStringLiteral("服务器错误") : code);
        if (code == QLatin1String("group_member_muted")) {
            emit serverGroupMemberMuted(obj);
            Message systemMessage;
            systemMessage.type = MessageType::System;
            systemMessage.content = message;
            systemMessage.timestamp = QDateTime::currentDateTime();
            emit newMessage(systemMessage);
        }
        emit connectionError(message);
        return;
    }

    if (type == "friend_search_result") {
        emit friendSearchResult(
            obj["account"].toString(),
            obj["userId"].toString(),
            obj["userName"].toString(),
            obj["found"].toBool(),
            obj["online"].toBool(),
            obj["exactMatch"].toBool(true),
            obj["matchCount"].toInt(obj["found"].toBool() ? 1 : 0),
            obj["matchReason"].toString());
        return;
    }

    if (type == "friend_request_sent") {
        emit friendRequestSent(obj["receiverId"].toString(), obj["delivered"].toBool());
        return;
    }

    if (type == "friend_request") {
        const QString senderId = obj["senderId"].toString().trimmed();
        const QString senderName = obj["senderName"].toString().trimmed();
        if (!senderId.isEmpty()) {
            m_pendingIncomingFriendNames.insert(senderId, senderName);
        }
        emit friendRequestReceived(senderId, senderName);
        return;
    }

    if (type == "friend_response") {
        const QString senderId = obj["senderId"].toString().trimmed();
        const QString senderName = obj["senderName"].toString().trimmed();
        const bool accepted = obj["accepted"].toBool();
        if (accepted) {
            addOrUpdateFriend(senderId, senderName);
        }
        emit friendResponseReceived(senderId, senderName, accepted);
        return;
    }

    if (type == "system") {
        Message msg;
        msg.type = MessageType::System;
        msg.content = obj["content"].toString();
        msg.timestamp = QDateTime::currentDateTime();
        emit newMessage(msg);
        return;
    }
}

void Client::handleIncomingFileChunk(const QJsonObject& obj) {
    const QString transferId = obj["transferId"].toString().trimmed();
    const QString fileName = obj["fileName"].toString();
    const qint64 fileSize = obj["fileSize"].toVariant().toLongLong();
    const qint64 chunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 chunkCount = obj["chunkCount"].toVariant().toLongLong();
    const qint64 chunkIndex = obj["chunkIndex"].toVariant().toLongLong();
    QByteArray chunkData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());
    const bool e2eFileEncrypted = obj["e2eFileEncrypted"].toBool(false);
    const qint64 e2ePlainSize = obj["e2eFilePlainSize"].toVariant().toLongLong();
    const QString e2ePlainHash = obj["e2eFilePlainHash"].toString().trimmed();
    QJsonObject e2eEnvelopeObject;
    if (e2eFileEncrypted && obj.value("e2eEnvelope").isObject()) {
        e2eEnvelopeObject = obj.value("e2eEnvelope").toObject();
    }

    auto failTransfer = [this, transferId, fileName, fileSize, chunkIndex](const QString& reason) {
        qint64 receivedBytes = 0;
        const auto it = m_incomingFileTransfers.constFind(transferId);
        if (it != m_incomingFileTransfers.constEnd()) {
            receivedBytes = it->receivedBytes;
        }
        if (!transferId.isEmpty()) {
            sendFileChunkAck(transferId, chunkIndex, false, reason);
            m_incomingFileTransfers.remove(transferId);
        }
        emit fileTransferStatusChanged(fileName,
                                       transferId,
                                       reason,
                                       receivedBytes,
                                       fileSize,
                                       QStringLiteral("incoming"),
                                       QString(),
                                       true);
        emit connectionError("文件分片接收失败：" + reason);
    };

    if (transferId.isEmpty()) {
        failTransfer("缺少传输编号");
        return;
    }
    if (fileSize <= 0 || fileSize > kMaxOutgoingPayloadBytes || chunkSize <= 0 || chunkCount <= 0 || chunkIndex < 0 || chunkIndex >= chunkCount) {
        failTransfer("分片元数据非法");
        return;
    }
    if (chunkCount > kMaxIncomingChunks) {
        failTransfer("分片数量超过接收限制");
        return;
    }
    const qint64 expectedChunkCount = (fileSize + chunkSize - 1) / chunkSize;
    if (expectedChunkCount != chunkCount) {
        failTransfer("分片数量与文件大小不一致");
        return;
    }
    if (chunkData.isEmpty() || chunkData.size() > chunkSize) {
        failTransfer("分片内容为空或超过声明大小");
        return;
    }
    if (chunkIndex < chunkCount - 1 && chunkData.size() != chunkSize) {
        failTransfer("非末尾分片大小不一致");
        return;
    }
    QString e2eValidationReason;
    const E2EEnvelope e2eEnvelope = E2EEnvelope::fromJson(e2eEnvelopeObject);
    const bool e2eEnvelopeValid = e2eEnvelopeObject.contains(QStringLiteral("ciphertext"))
        ? e2eEnvelope.isValid(&e2eValidationReason)
        : e2eEnvelopeHeaderLooksSafe(e2eEnvelopeObject);
    if (e2eFileEncrypted
        && (!e2eEnvelopeValid
            || e2eEnvelopeObject.value("senderId").toString() != obj["senderId"].toString()
            || e2eEnvelopeObject.value("receiverId").toString() != m_userId
            || e2eEnvelopeObject.value("keyId").toString() != obj["e2eFileKeyId"].toString()
            || e2ePlainSize <= 0
            || e2ePlainHash.isEmpty())) {
        failTransfer(QStringLiteral("端到端加密文件信封无效"));
        return;
    }

    PendingIncomingFileTransfer& pending = m_incomingFileTransfers[transferId];
    if (pending.chunks.isEmpty()) {
        pending.envelope = obj;
        pending.envelope["type"] = "file";
        pending.envelope.remove("chunkIndex");
        pending.envelope.remove("fileData");
        pending.fileName = obj["fileName"].toString();
        pending.fileSize = fileSize;
        pending.chunkSize = chunkSize;
        pending.chunkCount = chunkCount;
        pending.chunks.resize(static_cast<int>(chunkCount));
        emit fileTransferStatusChanged(pending.fileName,
                                       transferId,
                                       QStringLiteral("receive-started"),
                                       0,
                                       pending.fileSize,
                                       QStringLiteral("incoming"),
                                       QString(),
                                       false);
    } else if (pending.fileSize != fileSize
               || pending.chunkSize != chunkSize
               || pending.chunkCount != chunkCount
               || pending.envelope["e2eFileEncrypted"].toBool(false) != e2eFileEncrypted
               || pending.envelope["e2eFilePlainHash"].toString().trimmed() != e2ePlainHash) {
        failTransfer("同一传输编号的元数据不一致");
        return;
    }
    pending.lastActivityMs = QDateTime::currentMSecsSinceEpoch();

    const int index = static_cast<int>(chunkIndex);
    if (!pending.receivedIndexes.contains(index)) {
        pending.chunks[index] = chunkData;
        pending.receivedIndexes.insert(index);
        pending.receivedBytes += chunkData.size();
    }
    if (pending.receivedBytes > pending.fileSize) {
        failTransfer("累计分片大小超过声明文件大小");
        return;
    }
    emit fileReceiveProgress(obj["fileName"].toString(), pending.receivedBytes, pending.fileSize, transferId);
    if (pending.receivedIndexes.size() < pending.chunkCount) {
        sendFileChunkAck(transferId, chunkIndex, true, QString(), pending.receivedBytes);
        return;
    }

    QByteArray fileData;
    fileData.reserve(static_cast<int>(fileSize));
    for (const QByteArray& chunk : pending.chunks) {
        if (chunk.isEmpty()) {
            failTransfer("存在缺失分片");
            return;
        }
        fileData.append(chunk);
    }

    QJsonObject fullFile = pending.envelope;
    if (e2eFileEncrypted) {
        QJsonObject envelopeObject = fullFile.value("e2eEnvelope").toObject();
        if (!envelopeObject.contains(QStringLiteral("ciphertext"))) {
            envelopeObject[QStringLiteral("ciphertext")] = QString::fromLatin1(fileData.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
        }
        const E2EEnvelope fullEnvelope = E2EEnvelope::fromJson(envelopeObject);
        auto sessionIt = m_e2eSessions.find(fullEnvelope.senderId);
        QByteArray plaintextPayload;
        QString decryptReason;
        if (sessionIt == m_e2eSessions.end()
            || sessionIt->keyId != fullEnvelope.keyId
            || !decryptE2EPayload(fullEnvelope, sessionIt->sessionKey, &plaintextPayload, &decryptReason)) {
            failTransfer(QStringLiteral("端到端加密文件无法解密"));
            emit connectionError(QStringLiteral("端到端加密文件无法解密：%1")
                .arg(decryptReason.isEmpty() ? QStringLiteral("missing-session") : decryptReason));
            return;
        }
        fileData = plaintextPayload;
        const QString actualPlainHash = QString::fromLatin1(QCryptographicHash::hash(fileData, QCryptographicHash::Sha256).toHex());
        if (e2ePlainSize > 0 && fileData.size() != e2ePlainSize) {
            failTransfer(QStringLiteral("端到端加密文件明文大小校验失败"));
            return;
        }
        if (!e2ePlainHash.isEmpty() && actualPlainHash.compare(e2ePlainHash, Qt::CaseInsensitive) != 0) {
            failTransfer(QStringLiteral("端到端加密文件明文哈希校验失败"));
            return;
        }
        fullFile.remove("e2eEnvelope");
        fullFile["fileSize"] = QString::number(e2ePlainSize);
        fullFile["fileHash"] = e2ePlainHash;
        fullFile["chunkSize"] = QString::number(kTransferChunkBytes);
        fullFile["chunkCount"] = QString::number((e2ePlainSize + kTransferChunkBytes - 1) / kTransferChunkBytes);
        fullFile["content"] = QStringLiteral("[端到端加密] ") + fullFile["content"].toString();
        ++sessionIt->decryptedMessages;
        emit e2eSessionStateChanged(fullEnvelope.senderId, e2eSessionStatus(fullEnvelope.senderId));
    }
    m_incomingFileTransfers.remove(transferId);
    sendFileChunkAck(transferId, chunkIndex, true, QString(), fileSize);
    emit fileTransferStatusChanged(fileName,
                                   transferId,
                                   QStringLiteral("receive-completed"),
                                   fileData.size(),
                                   fileSize,
                                   QStringLiteral("incoming"),
                                   QString(),
                                   true);
    fullFile["fileData"] = QString::fromLatin1(fileData.toBase64());
    handleServerMessage(fullFile);
}

bool Client::waitForFileChunkAck(const QString& transferId, qint64 chunkIndex, QString* rejectReason, qint64* receivedBytes) {
    if (rejectReason) rejectReason->clear();
    if (receivedBytes) *receivedBytes = 0;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    bool matched = false;
    bool accepted = false;
    QString reason;
    qint64 ackReceivedBytes = 0;

    QMetaObject::Connection ackConnection = connect(
        this,
        &Client::fileChunkAckReceived,
        &loop,
        [&](const QString& ackTransferId, qint64 ackChunkIndex, bool ackAccepted, const QString& ackReason, qint64 ackBytes) {
            if (ackTransferId != transferId || ackChunkIndex != chunkIndex) return;
            matched = true;
            accepted = ackAccepted;
            reason = ackReason;
            ackReceivedBytes = ackBytes;
            loop.quit();
        });
    QMetaObject::Connection disconnectedConnection = connect(this, &Client::disconnected, &loop, &QEventLoop::quit);
    QMetaObject::Connection cancelConnection = connect(this, &Client::outgoingTransferCancelRequested, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(kChunkAckTimeoutMs);
    loop.exec();

    QObject::disconnect(ackConnection);
    QObject::disconnect(disconnectedConnection);
    QObject::disconnect(cancelConnection);

    if (matched && !accepted && rejectReason) {
        *rejectReason = reason.isEmpty() ? "服务端拒绝分片" : reason;
    }
    if (matched && receivedBytes) {
        *receivedBytes = ackReceivedBytes;
    }
    return matched && accepted;
}

bool Client::waitForFileTransferResumeState(const QString& transferId,
                                            qint64* confirmedBytes,
                                            qint64* nextChunkIndex,
                                            QVector<qint64>* receivedChunks,
                                            QString* rejectReason,
                                            int timeoutMs,
                                            qint64* fileSize,
                                            qint64* chunkSize,
                                            qint64* chunkCount,
                                            QString* fileHash) {
    if (confirmedBytes) *confirmedBytes = 0;
    if (nextChunkIndex) *nextChunkIndex = 0;
    if (receivedChunks) receivedChunks->clear();
    if (rejectReason) rejectReason->clear();
    if (fileSize) *fileSize = 0;
    if (chunkSize) *chunkSize = 0;
    if (chunkCount) *chunkCount = 0;
    if (fileHash) fileHash->clear();

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    bool matched = false;
    bool canResume = false;
    qint64 matchedConfirmedBytes = 0;
    qint64 matchedNextChunkIndex = 0;
    qint64 matchedFileSize = 0;
    qint64 matchedChunkSize = 0;
    qint64 matchedChunkCount = 0;
    QString matchedFileHash;
    QVector<qint64> matchedReceivedChunks;
    QString reason;

    QMetaObject::Connection resumeConnection = connect(
        this,
        &Client::fileTransferResumeStateReceived,
        &loop,
        [&](const QString& stateTransferId,
            bool stateCanResume,
            qint64 stateConfirmedBytes,
            qint64 stateNextChunkIndex,
            qint64 stateFileSize,
            qint64 stateChunkSize,
            qint64 stateChunkCount,
            const QString& stateFileHash,
            const QVector<qint64>& stateReceivedChunks,
            const QString& stateReason) {
            if (stateTransferId != transferId) return;
            matched = true;
            canResume = stateCanResume;
            matchedConfirmedBytes = stateConfirmedBytes;
            matchedNextChunkIndex = stateNextChunkIndex;
            matchedFileSize = stateFileSize;
            matchedChunkSize = stateChunkSize;
            matchedChunkCount = stateChunkCount;
            matchedFileHash = stateFileHash;
            matchedReceivedChunks = stateReceivedChunks;
            reason = stateReason;
            loop.quit();
        });
    QMetaObject::Connection disconnectedConnection = connect(this, &Client::disconnected, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(qMax(1, timeoutMs));
    loop.exec();

    QObject::disconnect(resumeConnection);
    QObject::disconnect(disconnectedConnection);

    if (matched) {
        if (confirmedBytes) *confirmedBytes = matchedConfirmedBytes;
        if (nextChunkIndex) *nextChunkIndex = matchedNextChunkIndex;
        if (receivedChunks) *receivedChunks = matchedReceivedChunks;
        if (fileSize) *fileSize = matchedFileSize;
        if (chunkSize) *chunkSize = matchedChunkSize;
        if (chunkCount) *chunkCount = matchedChunkCount;
        if (fileHash) *fileHash = matchedFileHash;
        if (!canResume && rejectReason) {
            *rejectReason = reason.isEmpty() ? "服务端未找到可续传状态" : reason;
        }
        return canResume;
    }

    if (rejectReason) *rejectReason = "续传状态查询超时";
    return false;
}

void Client::cleanupExpiredIncomingFileTransfers() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QString& transferId : m_incomingFileTransfers.keys()) {
        const auto it = m_incomingFileTransfers.constFind(transferId);
        if (it == m_incomingFileTransfers.constEnd()) {
            continue;
        }
        const PendingIncomingFileTransfer& pending = it.value();
        if (pending.lastActivityMs <= 0 || now - pending.lastActivityMs <= kTransferStaleTimeoutMs) {
            continue;
        }

        const QString visibleName = pending.fileName.isEmpty() ? "未命名文件" : pending.fileName;
        m_incomingFileTransfers.remove(transferId);
        emit connectionError(QString("文件分片接收超时，已清理：%1。请对方重新发送。").arg(visibleName));
    }
}
