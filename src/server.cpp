#include "server.h"
#include "objectstore.h"
#include "qqnt_redis_service.h"
#include "redisclient.h"
#include "heartbeatmonitor.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QCoreApplication>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QDataStream>
#include <QJsonArray>
#include <QStandardPaths>
#include <QStringList>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QRandomGenerator>
#include <QMessageAuthenticationCode>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QPointer>
#include <QPair>
#include <QThread>
#include <QUuid>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <limits>
#include <memory>

namespace {
constexpr qint64 kMaxIncomingPayloadBytes = 80LL * 1024 * 1024;
constexpr qint64 kMaxIncomingChunks = 4096;
constexpr qint64 kForwardChunkBytes = 256LL * 1024;
constexpr int kChunkAckTimeoutMs = 4000;
constexpr int kChunkSendMaxAttempts = 3;
constexpr qint64 kTransferStaleTimeoutMs = 2LL * 60 * 1000;
constexpr int kTransferCleanupIntervalMs = 30 * 1000;
constexpr qint64 kDefaultOfflineAttachmentTtlDays = 14;
constexpr qint64 kMaxOfflineAttachmentTtlDays = 3650;
constexpr int kOfflineAttachmentCleanupIntervalMs = 60 * 60 * 1000;
constexpr qint64 kDefaultOfflineAttachmentResumeProgressTtlHours = 24;
constexpr qint64 kMaxOfflineAttachmentResumeProgressTtlHours = 24LL * 365;
constexpr qint64 kDefaultOfflineAttachmentQuotaBytes = 512LL * 1024 * 1024;
constexpr qint64 kRedisPubSubFileMaxBytes = 1LL * 1024 * 1024;
constexpr qint64 kRedisPubSubEventMaxBytes = 1LL * 1024 * 1024;
constexpr qint64 kDefaultObjectStoreTtlHours = 24;
constexpr qint64 kMaxObjectStoreTtlHours = 24LL * 365;
constexpr int kPasswordKdfIterations = 120000;
constexpr int kPasswordKdfSaltBytes = 16;
constexpr int kPasswordKdfOutputBytes = 32;
constexpr qsizetype kMaxE2EIdentityPublicKeyBytes = 4096;

bool envEnabled(const char* name) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

bool looksLikeSha256Hex(const QString& value);

QString serverGroupTypeFromId(const QString& groupId) {
    return groupId == QLatin1String("public") ? QStringLiteral("public") : QStringLiteral("private");
}

QString serverGroupHistoryPolicy(const QString& groupType) {
    return groupType == QLatin1String("private")
        ? QStringLiteral("member-and-removed-readonly")
        : QStringLiteral("public-removed-readonly");
}

QString serverGroupFilePolicy(const QString& groupType) {
    return groupType == QLatin1String("private")
        ? QStringLiteral("members-only")
        : QStringLiteral("public-members-only");
}

QStringList initialServerGroupMemberIds(const QJsonObject& obj, const QString& requesterId) {
    QStringList memberIds;
    const QJsonArray members = obj.value(QStringLiteral("members")).toArray();
    for (const QJsonValue& memberValue : members) {
        QString memberId = memberValue.toString().trimmed();
        if (memberId.isEmpty() && memberValue.isObject()) {
            const QJsonObject memberObject = memberValue.toObject();
            memberId = memberObject.value(QStringLiteral("userId")).toString(
                memberObject.value(QStringLiteral("account")).toString(
                    memberObject.value(QStringLiteral("id")).toString())).trimmed();
        }
        if (memberId.isEmpty() || memberId == requesterId || memberIds.contains(memberId)) {
            continue;
        }
        memberIds << memberId;
    }
    return memberIds;
}

bool e2eEnvelopeHeaderLooksSafe(const QJsonObject& header);

void appendE2EFields(QJsonObject* obj, const Message& msg) {
    if (!obj) return;
    QString reason;
    if (msg.e2eEnvelope.isValid(&reason)) {
        (*obj)["e2eEnvelope"] = msg.e2eEnvelope.toJson();
        (*obj)["isEncrypted"] = true;
    } else if (!msg.e2eEnvelopeHeader.isEmpty()
               && e2eEnvelopeHeaderLooksSafe(msg.e2eEnvelopeHeader)) {
        (*obj)["e2eEnvelope"] = msg.e2eEnvelopeHeader;
        (*obj)["isEncrypted"] = true;
    }
    if (msg.e2eFileEncrypted) {
        (*obj)["e2eFileEncrypted"] = true;
        (*obj)["e2eFileKeyId"] = msg.e2eFileKeyId;
        (*obj)["e2eFileKeyFingerprintSha256"] = msg.e2eFileKeyFingerprint;
        (*obj)["e2eFilePlainSize"] = QString::number(msg.e2eFilePlainSize);
        (*obj)["e2eFilePlainHash"] = msg.e2eFilePlainHash;
    }
    if (msg.e2eKeyAgreement.isValid(&reason)) {
        (*obj)["e2eKeyAgreement"] = msg.e2eKeyAgreement.toJson();
    }
}

QJsonObject e2eEnvelopeHeaderJson(const E2EEnvelope& envelope) {
    QJsonObject header = envelope.toJson();
    header.remove(QStringLiteral("ciphertext"));
    return header;
}

bool e2eEnvelopeHeaderLooksSafe(const QJsonObject& header) {
    const QByteArray nonce = QByteArray::fromBase64(header.value("nonce").toString().toLatin1(),
                                                    QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QByteArray tag = QByteArray::fromBase64(header.value("tag").toString().toLatin1(),
                                                  QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
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
        && (!header.contains("ciphertextSha256")
            || looksLikeSha256Hex(header.value("ciphertextSha256").toString()));
}

void appendE2EFileFields(QJsonObject* target, const QJsonObject& source, bool includeCiphertext = false) {
    if (!target || !source.value("e2eFileEncrypted").toBool(false)) {
        return;
    }

    const QString keyId = source.value("e2eFileKeyId").toString().trimmed();
    const QString keyFingerprint = source.value("e2eFileKeyFingerprintSha256").toString().trimmed().toLower();
    const qint64 plainSize = source.value("e2eFilePlainSize").toVariant().toLongLong();
    const QString plainHash = source.value("e2eFilePlainHash").toString().trimmed().toLower();
    if (keyId.isEmpty() || !looksLikeSha256Hex(keyFingerprint) || plainSize <= 0 || !looksLikeSha256Hex(plainHash)) {
        return;
    }

    (*target)["e2eFileEncrypted"] = true;
    (*target)["e2eFileKeyId"] = keyId;
    (*target)["e2eFileKeyFingerprintSha256"] = keyFingerprint;
    (*target)["e2eFilePlainSize"] = QString::number(plainSize);
    (*target)["e2eFilePlainHash"] = plainHash;
    if (source.value("e2eEnvelope").isObject()) {
        QJsonObject envelopeObject = source.value("e2eEnvelope").toObject();
        if (includeCiphertext || envelopeObject.contains(QStringLiteral("ciphertext"))) {
            const E2EEnvelope envelope = E2EEnvelope::fromJson(envelopeObject);
            if (!envelope.isValid()) {
                return;
            }
            envelopeObject = includeCiphertext ? envelope.toJson() : e2eEnvelopeHeaderJson(envelope);
        }
        if (e2eEnvelopeHeaderLooksSafe(envelopeObject)) {
            (*target)["e2eEnvelope"] = envelopeObject;
            (*target)["isEncrypted"] = true;
        }
    }
}

QByteArray fromBase64Url(const QString& value) {
    return QByteArray::fromBase64(value.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
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
    if (identity.value("publicKeyFingerprintSha256").toString().trimmed().toLower() != e2eFingerprint(publicKey)) {
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

qint64 positiveIntegerEnvOrDefault(const char* name, qint64 defaultValue, qint64 maxValue = 0) {
    const QByteArray value = qgetenv(name).trimmed();
    if (value.isEmpty()) {
        return defaultValue;
    }

    bool ok = false;
    const qint64 parsed = value.toLongLong(&ok);
    if (!ok || parsed <= 0 || (maxValue > 0 && parsed > maxValue)) {
        return defaultValue;
    }
    return parsed;
}

qint64 offlineAttachmentResumeProgressTtlMs() {
    const qint64 hours = positiveIntegerEnvOrDefault("QTNETWORKCHAT_OFFLINE_RESUME_TTL_HOURS",
                                                     kDefaultOfflineAttachmentResumeProgressTtlHours,
                                                     kMaxOfflineAttachmentResumeProgressTtlHours);
    return hours * 60LL * 60 * 1000;
}

qint64 offlineAttachmentTtlMs() {
    const qint64 days = positiveIntegerEnvOrDefault("QTNETWORKCHAT_OFFLINE_ATTACHMENT_TTL_DAYS",
                                                    kDefaultOfflineAttachmentTtlDays,
                                                    kMaxOfflineAttachmentTtlDays);
    return days * 24LL * 60 * 60 * 1000;
}

QString appDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString accountDatabaseDriver() {
    const QString configured = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_DB_DRIVER")).trimmed().toUpper();
    if (configured == QLatin1String("QPSQL") || configured == QLatin1String("POSTGRES") || configured == QLatin1String("POSTGRESQL")) {
        return QStringLiteral("QPSQL");
    }
    return QStringLiteral("QSQLITE");
}

QString accountDatabaseDriverAlias() {
    const QString configured = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_DB_DRIVER")).trimmed();
    return configured.isEmpty() ? QStringLiteral("QSQLITE") : configured;
}

bool accountDatabaseIsPostgres() {
    return accountDatabaseDriver() == QLatin1String("QPSQL");
}

bool accountDatabasePoolEnabled() {
    const QByteArray value = qgetenv("QTNETWORKCHAT_DB_POOL").trimmed().toLower();
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }
    return accountDatabaseIsPostgres();
}

qint64 accountDatabasePoolMaxConnections() {
    return positiveIntegerEnvOrDefault("QTNETWORKCHAT_DB_POOL_MAX", 16, 256);
}

qint64 accountDatabasePoolIdleMs() {
    return positiveIntegerEnvOrDefault("QTNETWORKCHAT_DB_POOL_IDLE_MS", 5LL * 60 * 1000, 24LL * 60 * 60 * 1000);
}

qint64 accountDatabaseBackoffMs() {
    return positiveIntegerEnvOrDefault("QTNETWORKCHAT_DB_RECONNECT_BACKOFF_MS", 2000, 60000);
}

qint64 accountDatabaseSlowQueryMs() {
    return positiveIntegerEnvOrDefault("QTNETWORKCHAT_DB_SLOW_QUERY_MS", 1000, 10LL * 60 * 1000);
}

QString accountDatabasePath() {
    QString dir = appDataDir();
    if (dir.isEmpty()) dir = ".";
    QDir().mkpath(dir);
    return dir + "/accounts.sqlite3";
}

struct AccountDatabasePoolMetrics {
    qint64 createdConnections = 0;
    qint64 reusedConnections = 0;
    qint64 openAttempts = 0;
    qint64 openFailures = 0;
    qint64 backoffSkips = 0;
    qint64 idleConnectionsClosed = 0;
    qint64 overflowConnectionsClosed = 0;
    qint64 crossThreadCheckoutPrevented = 0;
    qint64 crossThreadReleaseDetected = 0;
    qint64 peakPooledConnections = 0;
    qint64 queryAttempts = 0;
    qint64 queryFailures = 0;
    qint64 slowQueries = 0;
    qint64 lastQueryDurationMs = 0;
    qint64 lastFailureAtMs = 0;
    QString lastFailureReason = QStringLiteral("ok");
    QString lastFailureScope;
    QString lastQueryScope;
    QString lastQueryFailureReason = QStringLiteral("ok");
    QString lastSlowQueryScope;
    QMap<QString, qint64> pooledConnectionLastReleasedAtMs;
    QMap<QString, quintptr> pooledConnectionThreadIds;
};

QMutex& accountDatabasePoolMutex() {
    static QMutex mutex;
    return mutex;
}

AccountDatabasePoolMetrics& accountDatabasePoolMetrics() {
    static AccountDatabasePoolMetrics metrics;
    return metrics;
}

QString accountDatabaseErrorReason(const QSqlError& error) {
    const QString errorText = (error.driverText() + QLatin1Char(' ') + error.databaseText() + QLatin1Char(' ') + error.nativeErrorCode()).toLower();
    if (errorText.contains(QStringLiteral("driver not loaded"))
        || errorText.contains(QStringLiteral("not loaded"))
        || errorText.contains(QStringLiteral("not available"))) {
        return QStringLiteral("runtime");
    }
    if (errorText.contains(QStringLiteral("password"))
        || errorText.contains(QStringLiteral("authentication"))
        || errorText.contains(QStringLiteral("permission denied"))
        || errorText.contains(QStringLiteral("role"))) {
        return QStringLiteral("auth");
    }
    if (errorText.contains(QStringLiteral("ssl"))
        || errorText.contains(QStringLiteral("tls"))
        || errorText.contains(QStringLiteral("certificate"))) {
        return QStringLiteral("tls");
    }
    if (errorText.contains(QStringLiteral("connection refused"))
        || errorText.contains(QStringLiteral("timeout"))
        || errorText.contains(QStringLiteral("timed out"))
        || errorText.contains(QStringLiteral("host"))
        || errorText.contains(QStringLiteral("network"))
        || errorText.contains(QStringLiteral("socket"))) {
        return QStringLiteral("network");
    }
    return QStringLiteral("query");
}

bool accountDatabaseInBackoffLocked(qint64 nowMs, qint64 backoffMs) {
    const AccountDatabasePoolMetrics& metrics = accountDatabasePoolMetrics();
    return metrics.lastFailureAtMs > 0
        && nowMs >= metrics.lastFailureAtMs
        && nowMs - metrics.lastFailureAtMs < backoffMs;
}

void recordAccountDatabaseOpenFailure(const QString& scope, const QSqlError& error) {
    QMutexLocker locker(&accountDatabasePoolMutex());
    AccountDatabasePoolMetrics& metrics = accountDatabasePoolMetrics();
    ++metrics.openFailures;
    metrics.lastFailureAtMs = QDateTime::currentMSecsSinceEpoch();
    metrics.lastFailureReason = accountDatabaseErrorReason(error);
    metrics.lastFailureScope = scope;
}

void recordAccountDatabaseQueryResult(const QString& scope, qint64 elapsedMs, bool ok, const QSqlError& error = QSqlError()) {
    QMutexLocker locker(&accountDatabasePoolMutex());
    AccountDatabasePoolMetrics& metrics = accountDatabasePoolMetrics();
    ++metrics.queryAttempts;
    metrics.lastQueryDurationMs = elapsedMs;
    metrics.lastQueryScope = scope;
    if (!ok) {
        ++metrics.queryFailures;
        metrics.lastQueryFailureReason = accountDatabaseErrorReason(error);
        metrics.lastFailureReason = metrics.lastQueryFailureReason;
        metrics.lastFailureScope = scope;
    } else {
        metrics.lastQueryFailureReason = QStringLiteral("ok");
    }

    if (elapsedMs >= accountDatabaseSlowQueryMs()) {
        ++metrics.slowQueries;
        metrics.lastSlowQueryScope = scope;
        qWarning() << "Slow account database query"
                   << scope
                   << elapsedMs
                   << "ms"
                   << accountDatabaseDriver();
    }
}

bool execAccountDatabaseQuery(QSqlQuery& query, const QString& scope, const QString& sql = QString()) {
    QElapsedTimer timer;
    timer.start();
    const bool ok = sql.isNull() ? query.exec() : query.exec(sql);
    recordAccountDatabaseQueryResult(scope, timer.elapsed(), ok, ok ? QSqlError() : query.lastError());
    return ok;
}

QStringList pruneReleasedAccountDatabaseConnectionsLocked(qint64 nowMs, const QString& preserveConnectionName) {
    AccountDatabasePoolMetrics& metrics = accountDatabasePoolMetrics();
    QStringList removeNames;
    const qint64 idleMs = accountDatabasePoolIdleMs();
    for (auto it = metrics.pooledConnectionLastReleasedAtMs.begin(); it != metrics.pooledConnectionLastReleasedAtMs.end();) {
        if (it.key() != preserveConnectionName && nowMs >= it.value() && nowMs - it.value() >= idleMs) {
            removeNames.append(it.key());
            metrics.pooledConnectionThreadIds.remove(it.key());
            it = metrics.pooledConnectionLastReleasedAtMs.erase(it);
            ++metrics.idleConnectionsClosed;
        } else {
            ++it;
        }
    }

    while (metrics.pooledConnectionLastReleasedAtMs.size() > accountDatabasePoolMaxConnections()) {
        QString oldestName;
        qint64 oldestReleasedAt = std::numeric_limits<qint64>::max();
        for (auto it = metrics.pooledConnectionLastReleasedAtMs.constBegin(); it != metrics.pooledConnectionLastReleasedAtMs.constEnd(); ++it) {
            if (it.key() != preserveConnectionName && it.value() < oldestReleasedAt) {
                oldestName = it.key();
                oldestReleasedAt = it.value();
            }
        }
        if (oldestName.isEmpty()) {
            break;
        }
        metrics.pooledConnectionLastReleasedAtMs.remove(oldestName);
        metrics.pooledConnectionThreadIds.remove(oldestName);
        removeNames.append(oldestName);
        ++metrics.overflowConnectionsClosed;
    }
    return removeNames;
}

QSqlDatabase openAccountDatabase(const QString& connectionName) {
    const QString driver = accountDatabaseDriver();
    const quintptr currentThreadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    if (QSqlDatabase::contains(connectionName)) {
        {
            QMutexLocker locker(&accountDatabasePoolMutex());
            AccountDatabasePoolMetrics& metrics = accountDatabasePoolMetrics();
            const quintptr ownerThreadId = metrics.pooledConnectionThreadIds.value(connectionName, currentThreadId);
            if (ownerThreadId != currentThreadId) {
                ++metrics.crossThreadCheckoutPrevented;
                metrics.pooledConnectionLastReleasedAtMs.remove(connectionName);
                metrics.pooledConnectionThreadIds.remove(connectionName);
            } else {
                ++metrics.reusedConnections;
                metrics.pooledConnectionLastReleasedAtMs.remove(connectionName);
                metrics.pooledConnectionThreadIds[connectionName] = currentThreadId;
                return QSqlDatabase::database(connectionName, false);
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(driver, connectionName);
    {
        QMutexLocker locker(&accountDatabasePoolMutex());
        AccountDatabasePoolMetrics& metrics = accountDatabasePoolMetrics();
        ++metrics.createdConnections;
        metrics.pooledConnectionThreadIds[connectionName] = currentThreadId;
    }
    if (driver == QLatin1String("QPSQL")) {
        db.setHostName(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGHOST")).trimmed().isEmpty()
            ? QStringLiteral("127.0.0.1")
            : QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGHOST")).trimmed());
        db.setPort(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGPORT")).trimmed().isEmpty()
            ? 5432
            : QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGPORT")).trimmed().toInt());
        db.setDatabaseName(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGDATABASE")).trimmed().isEmpty()
            ? QStringLiteral("qtnetworkchat")
            : QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGDATABASE")).trimmed());
        db.setUserName(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGUSER")).trimmed().isEmpty()
            ? QStringLiteral("postgres")
            : QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGUSER")).trimmed());
        db.setPassword(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGPASSWORD")));
    } else {
        db.setDatabaseName(accountDatabasePath());
    }
    return db;
}

bool openAccountDatabaseConnection(QSqlDatabase& db, const QString& scope) {
    {
        QMutexLocker locker(&accountDatabasePoolMutex());
        ++accountDatabasePoolMetrics().openAttempts;
        if (accountDatabasePoolEnabled()
            && accountDatabaseInBackoffLocked(QDateTime::currentMSecsSinceEpoch(), accountDatabaseBackoffMs())) {
            ++accountDatabasePoolMetrics().backoffSkips;
            accountDatabasePoolMetrics().lastFailureScope = scope;
            return false;
        }
    }

    if (db.isOpen()) {
        return true;
    }
    const bool opened = db.open();
    if (!opened) {
        recordAccountDatabaseOpenFailure(scope, db.lastError());
    }
    return opened;
}

void releaseAccountDatabase(const QString& connectionName) {
    if (!QSqlDatabase::contains(connectionName)) {
        return;
    }
    const quintptr currentThreadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    if (accountDatabasePoolEnabled()) {
        QStringList removeNames;
        {
            QMutexLocker locker(&accountDatabasePoolMutex());
            AccountDatabasePoolMetrics& metrics = accountDatabasePoolMetrics();
            const quintptr ownerThreadId = metrics.pooledConnectionThreadIds.value(connectionName, currentThreadId);
            if (ownerThreadId != currentThreadId) {
                ++metrics.crossThreadReleaseDetected;
                metrics.pooledConnectionLastReleasedAtMs.remove(connectionName);
                metrics.pooledConnectionThreadIds.remove(connectionName);
                removeNames.append(connectionName);
            } else {
                metrics.pooledConnectionLastReleasedAtMs[connectionName] = QDateTime::currentMSecsSinceEpoch();
                metrics.pooledConnectionThreadIds[connectionName] = currentThreadId;
                metrics.peakPooledConnections = qMax<qint64>(metrics.peakPooledConnections, metrics.pooledConnectionLastReleasedAtMs.size());
            }
            removeNames.append(pruneReleasedAccountDatabaseConnectionsLocked(QDateTime::currentMSecsSinceEpoch(), connectionName));
        }
        for (const QString& name : removeNames) {
            if (QSqlDatabase::contains(name)) {
                QSqlDatabase::removeDatabase(name);
            }
        }
        return;
    }
    QSqlDatabase::removeDatabase(connectionName);
}

QJsonObject accountDatabasePoolSnapshot() {
    QMutexLocker locker(&accountDatabasePoolMutex());
    const AccountDatabasePoolMetrics& metrics = accountDatabasePoolMetrics();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 backoffMs = accountDatabaseBackoffMs();
    QJsonObject obj;
    obj["enabled"] = accountDatabasePoolEnabled();
    obj["driver"] = accountDatabaseDriver();
    obj["createdConnections"] = QString::number(metrics.createdConnections);
    obj["reusedConnections"] = QString::number(metrics.reusedConnections);
    obj["openAttempts"] = QString::number(metrics.openAttempts);
    obj["openFailures"] = QString::number(metrics.openFailures);
    obj["backoffSkips"] = QString::number(metrics.backoffSkips);
    obj["idleConnectionsClosed"] = QString::number(metrics.idleConnectionsClosed);
    obj["overflowConnectionsClosed"] = QString::number(metrics.overflowConnectionsClosed);
    obj["crossThreadCheckoutPrevented"] = QString::number(metrics.crossThreadCheckoutPrevented);
    obj["crossThreadReleaseDetected"] = QString::number(metrics.crossThreadReleaseDetected);
    obj["peakPooledConnections"] = QString::number(metrics.peakPooledConnections);
    obj["queryAttempts"] = QString::number(metrics.queryAttempts);
    obj["queryFailures"] = QString::number(metrics.queryFailures);
    obj["slowQueries"] = QString::number(metrics.slowQueries);
    obj["lastQueryDurationMs"] = QString::number(metrics.lastQueryDurationMs);
    obj["lastQueryScope"] = metrics.lastQueryScope;
    obj["lastQueryFailureReason"] = metrics.lastQueryFailureReason.isEmpty() ? QStringLiteral("ok") : metrics.lastQueryFailureReason;
    obj["lastSlowQueryScope"] = metrics.lastSlowQueryScope;
    obj["pooledConnections"] = QString::number(metrics.pooledConnectionLastReleasedAtMs.size());
    obj["pooledConnectionThreadCount"] = QString::number(metrics.pooledConnectionThreadIds.size());
    obj["maxConnections"] = QString::number(accountDatabasePoolMaxConnections());
    obj["idleMs"] = QString::number(accountDatabasePoolIdleMs());
    obj["backoffMs"] = QString::number(backoffMs);
    obj["slowQueryMs"] = QString::number(accountDatabaseSlowQueryMs());
    obj["inBackoff"] = accountDatabasePoolEnabled() && accountDatabaseInBackoffLocked(nowMs, backoffMs);
    obj["lastFailureReason"] = metrics.lastFailureReason.isEmpty() ? QStringLiteral("ok") : metrics.lastFailureReason;
    obj["lastFailureScope"] = metrics.lastFailureScope;
    obj["reasonBuckets"] = QJsonArray{
        QStringLiteral("ok"),
        QStringLiteral("runtime"),
        QStringLiteral("auth"),
        QStringLiteral("network"),
        QStringLiteral("tls"),
        QStringLiteral("schema"),
        QStringLiteral("path"),
        QStringLiteral("query")
    };
    obj["threadPolicy"] = QJsonObject{
        {QStringLiteral("connectionOwnership"), accountDatabasePoolEnabled() ? QStringLiteral("thread-affine pooled connections") : QStringLiteral("direct-open per caller")},
        {QStringLiteral("crossThreadReuse"), false},
        {QStringLiteral("checkoutScope"), accountDatabasePoolEnabled() ? QStringLiteral("connection-name plus owning thread") : QStringLiteral("not-applicable")},
        {QStringLiteral("releaseScope"), accountDatabasePoolEnabled() ? QStringLiteral("same thread that checked out or created the connection") : QStringLiteral("not-applicable")},
        {QStringLiteral("governance"), QStringLiteral("cross-thread checkout is discarded and recreated; cross-thread release is closed instead of pooled")}
    };
    return obj;
}

QJsonObject redactedAccountDatabaseConfig() {
    QJsonObject config;
    const QString driver = accountDatabaseDriver();
    config["driver"] = driver;
    config["configuredDriver"] = accountDatabaseDriverAlias();
    if (driver == QLatin1String("QPSQL")) {
        config["host"] = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGHOST")).trimmed().isEmpty()
            ? QStringLiteral("127.0.0.1")
            : QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGHOST")).trimmed();
        config["port"] = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGPORT")).trimmed().isEmpty()
            ? 5432
            : QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGPORT")).trimmed().toInt();
        config["database"] = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGDATABASE")).trimmed().isEmpty()
            ? QStringLiteral("qtnetworkchat")
            : QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGDATABASE")).trimmed();
        config["user"] = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGUSER")).trimmed().isEmpty()
            ? QStringLiteral("postgres")
            : QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_PGUSER")).trimmed();
        config["password"] = QStringLiteral("<redacted>");
    } else {
        config["path"] = accountDatabasePath();
    }
    return config;
}

QJsonObject databaseErrorJson(const QString& scope, const QSqlError& error) {
    QJsonObject obj;
    obj["scope"] = scope;
    const QString errorText = (error.driverText() + QLatin1Char(' ') + error.databaseText() + QLatin1Char(' ') + error.nativeErrorCode()).toLower();
    QString reason = QStringLiteral("query");
    if (errorText.contains(QStringLiteral("driver not loaded"))
        || errorText.contains(QStringLiteral("not loaded"))
        || errorText.contains(QStringLiteral("not available"))) {
        reason = QStringLiteral("runtime");
    } else if (errorText.contains(QStringLiteral("password"))
               || errorText.contains(QStringLiteral("authentication"))
               || errorText.contains(QStringLiteral("permission denied"))
               || errorText.contains(QStringLiteral("role"))) {
        reason = QStringLiteral("auth");
    } else if (errorText.contains(QStringLiteral("ssl"))
               || errorText.contains(QStringLiteral("tls"))
               || errorText.contains(QStringLiteral("certificate"))) {
        reason = QStringLiteral("tls");
    } else if (errorText.contains(QStringLiteral("connection refused"))
               || errorText.contains(QStringLiteral("timeout"))
               || errorText.contains(QStringLiteral("timed out"))
               || errorText.contains(QStringLiteral("host"))
               || errorText.contains(QStringLiteral("network"))
               || errorText.contains(QStringLiteral("socket"))) {
        reason = QStringLiteral("network");
    }
    obj["reason"] = reason;
    obj["driverText"] = error.driverText();
    obj["databaseText"] = error.databaseText();
    obj["nativeErrorCode"] = error.nativeErrorCode();
    return obj;
}

QString insertIgnoreSql(const QString& table,
                        const QStringList& columns,
                        const QStringList& values,
                        const QStringList& conflictColumns) {
    const QString columnList = columns.join(QStringLiteral(", "));
    const QString valueList = values.join(QStringLiteral(", "));
    if (accountDatabaseIsPostgres()) {
        return QStringLiteral("INSERT INTO %1(%2) VALUES(%3) ON CONFLICT(%4) DO NOTHING")
            .arg(table, columnList, valueList, conflictColumns.join(QStringLiteral(", ")));
    }
    return QStringLiteral("INSERT OR IGNORE INTO %1(%2) VALUES(%3)").arg(table, columnList, valueList);
}

QString insertReplaceSql(const QString& table,
                         const QStringList& columns,
                         const QStringList& values,
                         const QStringList& conflictColumns,
                         const QStringList& updateAssignments) {
    const QString columnList = columns.join(QStringLiteral(", "));
    const QString valueList = values.join(QStringLiteral(", "));
    if (accountDatabaseIsPostgres()) {
        return QStringLiteral("INSERT INTO %1(%2) VALUES(%3) ON CONFLICT(%4) DO UPDATE SET %5")
            .arg(table,
                 columnList,
                 valueList,
                 conflictColumns.join(QStringLiteral(", ")),
                 updateAssignments.join(QStringLiteral(", ")));
    }
    return QStringLiteral("INSERT OR REPLACE INTO %1(%2) VALUES(%3)").arg(table, columnList, valueList);
}

QString autoIdColumnSql() {
    return accountDatabaseIsPostgres()
        ? QStringLiteral("id BIGSERIAL PRIMARY KEY, ")
        : QStringLiteral("id INTEGER PRIMARY KEY AUTOINCREMENT, ");
}

QString safePathPart(const QString& value) {
    QString safe;
    safe.reserve(value.size());
    for (const QChar& ch : value) {
        if (ch.isLetterOrNumber()
            || ch == QLatin1Char('_')
            || ch == QLatin1Char('-')
            || ch == QLatin1Char('.')) {
            safe.append(ch);
        } else {
            safe.append(QLatin1Char('_'));
        }
    }
    return safe.isEmpty() ? "unknown" : safe;
}

QString metricFieldValue(QString value) {
    value = value.trimmed().left(160);
    for (QChar& ch : value) {
        if (ch.isSpace()) {
            ch = QLatin1Char('_');
        }
    }
    return value;
}

void logRedisLargeFileRouteEvent(const QString& eventName,
                                 const QString& result,
                                 const QJsonObject& metadata,
                                 const QString& reason = QString(),
                                 qint64 bytes = -1) {
    QStringList fields;
    fields << QStringLiteral("event=%1").arg(eventName)
           << QStringLiteral("result=%1").arg(result);

    const auto appendStringField = [&fields, &metadata](const char* key) {
        const QString value = metadata[QString::fromLatin1(key)].toString().trimmed();
        if (!value.isEmpty()) {
            fields << QStringLiteral("%1=%2").arg(QString::fromLatin1(key), metricFieldValue(value));
        }
    };

    appendStringField("sourceInstanceId");
    appendStringField("transferId");
    appendStringField("objectKey");
    appendStringField("receiverId");
    appendStringField("fileHash");
    appendStringField("fileName");
    appendStringField("messageType");
    appendStringField("storeType");
    appendStringField("operation");
    if (bytes >= 0) {
        fields << QStringLiteral("bytes=%1").arg(bytes);
    }
    if (!reason.trimmed().isEmpty()) {
        fields << QStringLiteral("reason=%1").arg(metricFieldValue(reason));
    }

    const QString line = QStringLiteral("redis_large_file_route %1").arg(fields.join(QLatin1Char(' ')));
    const bool isExpectedSuccess = result == QLatin1String("published")
        || result == QLatin1String("cleaned")
        || result == QLatin1String("removed");
    if (eventName == QLatin1String("failed") || !isExpectedSuccess) {
        qWarning().noquote() << line;
    } else {
        qInfo().noquote() << line;
    }
}

QJsonObject largeFileRouteLogMetadata(QJsonObject metadata,
                                      const QString& storeType,
                                      const QString& operation) {
    const QString trimmedStoreType = storeType.trimmed();
    if (!trimmedStoreType.isEmpty()) {
        metadata["storeType"] = trimmedStoreType;
    }
    const QString trimmedOperation = operation.trimmed();
    if (!trimmedOperation.isEmpty()) {
        metadata["operation"] = trimmedOperation;
    }
    return metadata;
}

bool looksLikeSha256Hex(const QString& value) {
    const QString trimmed = value.trimmed();
    if (trimmed.size() != 64) return false;
    for (const QChar& ch : trimmed) {
        const ushort c = ch.toLatin1();
        const bool isHex = (c >= '0' && c <= '9')
            || (c >= 'a' && c <= 'f')
            || (c >= 'A' && c <= 'F');
        if (!isHex) return false;
    }
    return true;
}

QByteArray pbkdf2Sha256(const QByteArray& password, const QByteArray& salt, int iterations, int outputBytes) {
    if (iterations <= 0 || outputBytes <= 0) {
        return QByteArray();
    }

    QByteArray derived;
    quint32 blockIndex = 1;
    while (derived.size() < outputBytes) {
        QByteArray counter;
        counter.append(char((blockIndex >> 24) & 0xff));
        counter.append(char((blockIndex >> 16) & 0xff));
        counter.append(char((blockIndex >> 8) & 0xff));
        counter.append(char(blockIndex & 0xff));

        QByteArray u = QMessageAuthenticationCode::hash(salt + counter, password, QCryptographicHash::Sha256);
        QByteArray block = u;
        for (int i = 1; i < iterations; ++i) {
            u = QMessageAuthenticationCode::hash(u, password, QCryptographicHash::Sha256);
            for (int j = 0; j < block.size(); ++j) {
                block[j] = char(uchar(block.at(j)) ^ uchar(u.at(j)));
            }
        }
        derived += block;
        ++blockIndex;
    }
    return derived.left(outputBytes);
}

QByteArray randomSalt(int bytes) {
    QByteArray salt;
    salt.reserve(bytes);
    while (salt.size() < bytes) {
        const quint64 value = QRandomGenerator::global()->generate64();
        for (int shift = 0; shift < 64 && salt.size() < bytes; shift += 8) {
            salt.append(char((value >> shift) & 0xff));
        }
    }
    return salt;
}

QString legacyPasswordHash(const QString& account, const QString& password) {
    return QString::fromLatin1(QCryptographicHash::hash((account + ":" + password).toUtf8(),
                                                        QCryptographicHash::Sha256).toHex());
}

QString makePasswordKdfHash(const QString& account, const QString& password, const QByteArray& salt = QByteArray()) {
    const QByteArray actualSalt = salt.isEmpty() ? randomSalt(kPasswordKdfSaltBytes) : salt;
    const QByteArray material = (account + ":" + password).toUtf8();
    const QByteArray hash = pbkdf2Sha256(material, actualSalt, kPasswordKdfIterations, kPasswordKdfOutputBytes);
    return QStringLiteral("kdf$pbkdf2-sha256$%1$%2$%3")
        .arg(kPasswordKdfIterations)
        .arg(QString::fromLatin1(actualSalt.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)))
        .arg(QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
}

bool verifyPasswordKdfHash(const QString& account, const QString& password, const QString& storedHash) {
    const QStringList parts = storedHash.split(QLatin1Char('$'));
    if (parts.size() != 5
        || parts.at(0) != QLatin1String("kdf")
        || parts.at(1) != QLatin1String("pbkdf2-sha256")) {
        return false;
    }

    bool ok = false;
    const int iterations = parts.at(2).toInt(&ok);
    if (!ok || iterations <= 0 || iterations > 1000000) {
        return false;
    }
    const QByteArray salt = QByteArray::fromBase64(parts.at(3).toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QByteArray expected = QByteArray::fromBase64(parts.at(4).toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    if (salt.isEmpty() || expected.size() != kPasswordKdfOutputBytes) {
        return false;
    }

    const QByteArray material = (account + ":" + password).toUtf8();
    return pbkdf2Sha256(material, salt, iterations, expected.size()) == expected;
}

bool isKdfPasswordHash(const QString& storedHash) {
    return storedHash.startsWith(QStringLiteral("kdf$pbkdf2-sha256$"));
}

bool verifyStoredPasswordHash(const QString& account,
                              const QString& password,
                              const QString& storedHash,
                              bool* needsUpgrade = nullptr) {
    if (needsUpgrade) {
        *needsUpgrade = false;
    }
    if (isKdfPasswordHash(storedHash)) {
        return verifyPasswordKdfHash(account, password, storedHash);
    }
    if (looksLikeSha256Hex(storedHash)) {
        const bool ok = storedHash.compare(legacyPasswordHash(account, password), Qt::CaseInsensitive) == 0;
        if (ok && needsUpgrade) {
            *needsUpgrade = true;
        }
        return ok;
    }
    return false;
}

class TlsTcpServer : public QTcpServer {
public:
    TlsTcpServer(const QSslCertificate& certificate, const QSslKey& privateKey, QObject* parent = nullptr)
        : QTcpServer(parent)
        , m_certificate(certificate)
        , m_privateKey(privateKey) {
    }

protected:
    void incomingConnection(qintptr socketDescriptor) override {
        QSslSocket* socket = new QSslSocket(this);
        socket->setLocalCertificate(m_certificate);
        socket->setPrivateKey(m_privateKey);
        socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        if (!socket->setSocketDescriptor(socketDescriptor)) {
            socket->deleteLater();
            return;
        }
        addPendingConnection(socket);
        socket->startServerEncryption();
    }

private:
    QSslCertificate m_certificate;
    QSslKey m_privateKey;
};

QTcpServer* createServerSocket(QObject* parent) {
    if (!envEnabled("QTNETWORKCHAT_TLS")) {
        return new QTcpServer(parent);
    }
    if (!QSslSocket::supportsSsl()) {
        qWarning() << "TLS requested but Qt/OpenSSL is unavailable; falling back to TCP";
        return new QTcpServer(parent);
    }

    const QString certPath = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_TLS_CERT")).trimmed();
    const QString keyPath = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_TLS_KEY")).trimmed();
    QFile certFile(certPath);
    QFile keyFile(keyPath);
    if (certPath.isEmpty() || keyPath.isEmpty()
        || !certFile.open(QIODevice::ReadOnly)
        || !keyFile.open(QIODevice::ReadOnly)) {
        qWarning() << "TLS requested but QTNETWORKCHAT_TLS_CERT/QTNETWORKCHAT_TLS_KEY are not readable; falling back to TCP";
        return new QTcpServer(parent);
    }

    const QSslCertificate certificate(&certFile, QSsl::Pem);
    const QSslKey privateKey(&keyFile, QSsl::Rsa, QSsl::Pem);
    if (certificate.isNull() || privateKey.isNull()) {
        qWarning() << "TLS certificate or private key is invalid; falling back to TCP";
        return new QTcpServer(parent);
    }

    QTcpServer* server = new TlsTcpServer(certificate, privateKey, parent);
    server->setProperty("tlsEnabled", true);
    return server;
}

void sendSystemNotice(QTcpSocket* socket, const QString& content) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

    QJsonObject response;
    response["type"] = "system";
    response["content"] = content;
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

enum class OfflineAttachmentValidationResult {
    Ready,
    CleanedBadState,
    Blocked
};

OfflineAttachmentValidationResult sendOfflineAttachmentBadStateNotice(QTcpSocket* socket,
                                                                      const QString& filePath,
                                                                      const QString& fileName,
                                                                      const QString& reason) {
    sendSystemNotice(socket, QString("%1：%2，请让对方重新发送。").arg(reason, fileName));
    QFile::remove(filePath);
    return OfflineAttachmentValidationResult::CleanedBadState;
}

OfflineAttachmentValidationResult validateOfflineAttachmentForReplay(const QJsonObject& obj,
                                                                     const QString& filePath,
                                                                     QTcpSocket* socket) {
    const QString fileName = obj["fileName"].toString("未命名文件");
    const QFileInfo attachmentInfo(filePath);
    if (!attachmentInfo.exists() || !attachmentInfo.isFile()) {
        return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件已丢失");
    }
    if (attachmentInfo.size() <= 0) {
        return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件为空");
    }

    const qint64 declaredSize = obj["fileSize"].toVariant().toLongLong();
    if (declaredSize > 0 && declaredSize != attachmentInfo.size()) {
        return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件大小异常");
    }

    const qint64 declaredChunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 declaredChunkCount = obj["chunkCount"].toVariant().toLongLong();
    if (declaredChunkSize <= 0
        || declaredChunkSize > kForwardChunkBytes
        || declaredChunkCount <= 0
        || declaredChunkCount != (attachmentInfo.size() + declaredChunkSize - 1) / declaredChunkSize) {
        return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件分片元数据异常");
    }

    const QString declaredHash = obj["fileHash"].toString().trimmed();
    if (looksLikeSha256Hex(declaredHash)) {
        QFile hashFile(filePath);
        if (!hashFile.open(QIODevice::ReadOnly)) {
            return OfflineAttachmentValidationResult::Blocked;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&hashFile)) {
            return OfflineAttachmentValidationResult::Blocked;
        }
        const QString actualHash = QString::fromLatin1(hash.result().toHex());
        hashFile.close();
        if (actualHash.compare(declaredHash, Qt::CaseInsensitive) != 0) {
            return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件校验失败");
        }
    }

    if (obj["e2eFileEncrypted"].toBool(false)) {
        QJsonObject e2eEvidence;
        appendE2EFileFields(&e2eEvidence, obj);
        if (!e2eEvidence.value("e2eFileEncrypted").toBool(false)
            || !e2eEvidence.value("e2eEnvelope").isObject()) {
            return sendOfflineAttachmentBadStateNotice(socket,
                                                       filePath,
                                                       fileName,
                                                       "端到端加密离线文件恢复证据无效");
        }
    }

    return OfflineAttachmentValidationResult::Ready;
}

struct OfflineAttachmentReplayPlan {
    qint64 startChunkIndex = 0;
    bool confirmedChunksValid = false;
    QSet<qint64> confirmedChunkIndexes;
};

OfflineAttachmentReplayPlan buildOfflineAttachmentReplayPlan(const QJsonObject& obj,
                                                             qint64 totalBytes,
                                                             qint64 chunkSize,
                                                             qint64 chunkCount,
                                                             qint64 sqliteMessageId) {
    OfflineAttachmentReplayPlan plan;
    const qint64 declaredChunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 declaredChunkCount = obj["chunkCount"].toVariant().toLongLong();
    const qint64 recordedConfirmedBytes = obj["confirmedBytes"].toVariant().toLongLong();
    const QJsonArray confirmedChunks = obj["confirmedChunks"].toArray();
    const bool hasResumeProgress = recordedConfirmedBytes > 0 || !confirmedChunks.isEmpty();
    bool resumeProgressFresh = !hasResumeProgress;
    if (hasResumeProgress) {
        const QDateTime resumeUpdatedAt = QDateTime::fromString(obj["resumeUpdatedAt"].toString(), Qt::ISODate);
        if (resumeUpdatedAt.isValid()) {
            const qint64 ageMs = resumeUpdatedAt.toUTC().msecsTo(QDateTime::currentDateTimeUtc());
            resumeProgressFresh = ageMs >= 0 && ageMs <= offlineAttachmentResumeProgressTtlMs();
        }
    }

    plan.confirmedChunksValid = resumeProgressFresh
        && sqliteMessageId > 0
        && declaredChunkSize == chunkSize
        && declaredChunkCount == chunkCount;
    for (const QJsonValue& value : confirmedChunks) {
        const qint64 chunkIndex = value.toVariant().toLongLong();
        if (chunkIndex < 0 || chunkIndex >= chunkCount) {
            plan.confirmedChunksValid = false;
            plan.confirmedChunkIndexes.clear();
            break;
        }
        plan.confirmedChunkIndexes.insert(chunkIndex);
    }

    const bool canResumeFromConfirmedBytes = resumeProgressFresh
        && sqliteMessageId > 0
        && declaredChunkSize == chunkSize
        && (declaredChunkCount <= 0 || declaredChunkCount == chunkCount)
        && recordedConfirmedBytes > 0
        && recordedConfirmedBytes <= totalBytes
        && recordedConfirmedBytes % chunkSize == 0;
    plan.startChunkIndex = canResumeFromConfirmedBytes
        ? qMin(recordedConfirmedBytes / chunkSize, chunkCount)
        : 0;
    if (plan.confirmedChunksValid && !plan.confirmedChunkIndexes.isEmpty()) {
        plan.startChunkIndex = chunkCount;
        for (qint64 index = 0; index < chunkCount; ++index) {
            if (!plan.confirmedChunkIndexes.contains(index)) {
                plan.startChunkIndex = index;
                break;
            }
        }
    }
    return plan;
}

void sendFileChunkAck(QTcpSocket* socket,
                      const QString& transferId,
                      qint64 chunkIndex,
                      bool accepted,
                      const QString& reason = QString(),
                      qint64 receivedBytes = 0) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState || transferId.isEmpty()) return;

    QJsonObject response;
    response["type"] = "file_chunk_ack";
    response["transferId"] = transferId;
    response["chunkIndex"] = QString::number(chunkIndex);
    response["accepted"] = accepted;
    response["reason"] = reason;
    response["receivedBytes"] = QString::number(receivedBytes);
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

QString pendingFileTransferKey(const QString& senderId, const QString& transferId) {
    if (senderId.trimmed().isEmpty() || transferId.trimmed().isEmpty()) {
        return QString();
    }
    return senderId.trimmed() + ":" + transferId.trimmed();
}
}

Server::Server(QObject* parent)
    : QObject(parent)
    , m_tcpServer(createServerSocket(this))
    , m_redisService(new QQNTRedisService(this))
    , m_transferCleanupTimer(new QTimer(this))
    , m_offlineAttachmentCleanupTimer(new QTimer(this))
    , m_heartbeatMonitor(new HeartbeatMonitor(this))
    , m_serverPort(0)
    , m_tlsEnabled(m_tcpServer->property("tlsEnabled").toBool())
    , m_instanceId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_redisService->configureFromEnvironment();
    connect(m_redisService, &QQNTRedisService::messageReceived, this, [this](const RedisClient::PubSubMessage& message) {
        handleRedisMessageEvent(message.payload);
    });
    connect(m_redisService, &QQNTRedisService::readinessChanged, this, [this](bool ready, const QString& reason) {
        m_serviceReady = ready;
        m_serviceReadinessReason = reason;
    });

    connect(m_tcpServer, &QTcpServer::newConnection, this, &Server::onNewConnection);
    connect(m_transferCleanupTimer, &QTimer::timeout, this, &Server::cleanupExpiredFileTransfers);
    connect(m_offlineAttachmentCleanupTimer, &QTimer::timeout, this, &Server::cleanupExpiredOfflineAttachments);
    m_transferCleanupTimer->start(kTransferCleanupIntervalMs);
    m_offlineAttachmentCleanupTimer->start(kOfflineAttachmentCleanupIntervalMs);

    // Configure heartbeat monitor
    m_heartbeatMonitor->setTimeoutMs(90000); // 90 seconds timeout
    m_heartbeatMonitor->setEventLoopStallGraceMs(60000);
    m_heartbeatMonitor->setTimeoutCallback([this](const QString& clientId) {
        QTcpSocket* socket = m_userSockets.value(clientId);
        if (socket) {
            qWarning() << "Closing connection for timed out client:" << clientId;
            socket->disconnectFromHost();
        }
    });
    connect(m_heartbeatMonitor, &HeartbeatMonitor::clientTimedOut, this, [this](const QString& clientId) {
        qWarning() << "Heartbeat timeout detected for client:" << clientId;
    });
    connect(m_heartbeatMonitor, &HeartbeatMonitor::statsUpdated, this, [this](const HeartbeatStats& stats) {
        if (stats.timedOutClients > 0) {
            qDebug() << "Heartbeat stats - Total:" << stats.totalClients
                     << "Active:" << stats.activeClients
                     << "Timed out:" << stats.timedOutClients
                     << "Avg response:" << stats.avgResponseTimeMs << "ms";
        }
    });
    m_heartbeatMonitor->start(10000); // Check every 10 seconds
}

Server::~Server() {
    stop();
}

void Server::setObjectStoreFactoryForTesting(ObjectStoreFactory factory) {
    m_objectStoreFactoryForTesting = std::move(factory);
}

bool Server::start(quint16 port) {
    if (!ensureRedisReadyForStartup()) {
        return false;
    }

    ensureAccountDatabase();
    if (m_tcpServer->listen(QHostAddress::Any, port)) {
        if (!m_transferCleanupTimer->isActive()) {
            m_transferCleanupTimer->start(kTransferCleanupIntervalMs);
        }
        if (!m_offlineAttachmentCleanupTimer->isActive()) {
            m_offlineAttachmentCleanupTimer->start(kOfflineAttachmentCleanupIntervalMs);
        }
        cleanupExpiredOfflineAttachments();
        m_serverPort = port;
        refreshServiceReadiness();
        qDebug() << "Server started on port" << port << transportSecurityDescription();

        QList<QHostAddress> interfaces = QNetworkInterface::allAddresses();
        for (const QHostAddress& addr : interfaces) {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol && addr != QHostAddress::LocalHost) {
                qDebug() << "Server IP:" << addr.toString();
            }
        }
        return true;
    }
    return false;
}

QString Server::transportSecurityDescription() const {
    if (m_tlsEnabled) return "TLS 加密服务";
    if (envEnabled("QTNETWORKCHAT_TLS") && !QSslSocket::supportsSsl()) {
        return "TLS 已请求，但 Qt/OpenSSL 不可用，已回退 TCP";
    }
    if (envEnabled("QTNETWORKCHAT_TLS")) {
        return "TLS 已请求，但证书未配置，已回退 TCP";
    }
    return "普通 TCP 服务";
}

QJsonObject Server::databaseHealthSnapshot() const {
    QJsonObject snapshot;
    snapshot["format"] = QStringLiteral("qtnetworkchat-database-health-v1");
    snapshot["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    snapshot["ok"] = false;
    snapshot["config"] = redactedAccountDatabaseConfig();
    snapshot["pool"] = accountDatabasePoolSnapshot();

    const QString connectionName = "database_health_" + QString::number(reinterpret_cast<quintptr>(this));
    QJsonArray checks;
    bool healthy = true;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        const bool opened = openAccountDatabaseConnection(db, connectionName);
        QJsonObject openCheck;
        openCheck["name"] = QStringLiteral("open");
        openCheck["ok"] = opened;
        openCheck["reason"] = QStringLiteral("ok");
        if (!opened) {
            const QJsonObject error = databaseErrorJson(QStringLiteral("open"), db.lastError());
            openCheck["error"] = error;
            openCheck["reason"] = error.value("reason").toString(QStringLiteral("query"));
            healthy = false;
            qWarning() << "Database health open failed:"
                       << db.lastError().text()
                       << accountDatabaseDriver();
        }
        checks.append(openCheck);

        if (opened) {
            QSqlQuery pingQuery(db);
            const bool pingOk = execAccountDatabaseQuery(pingQuery,
                                                         QStringLiteral("database_health_ping"),
                                                         QStringLiteral("SELECT 1"));
            QJsonObject pingCheck;
            pingCheck["name"] = QStringLiteral("ping");
            pingCheck["ok"] = pingOk;
            pingCheck["reason"] = QStringLiteral("ok");
            if (!pingOk) {
                const QJsonObject error = databaseErrorJson(QStringLiteral("ping"), pingQuery.lastError());
                pingCheck["error"] = error;
                pingCheck["reason"] = error.value("reason").toString(QStringLiteral("query"));
                healthy = false;
                qWarning() << "Database health ping failed:"
                           << pingQuery.lastError().text()
                           << accountDatabaseDriver();
            }
            checks.append(pingCheck);

            const QStringList requiredTables{
                QStringLiteral("accounts"),
                QStringLiteral("user_sessions"),
                QStringLiteral("messages"),
                QStringLiteral("offline_messages"),
                QStringLiteral("friend_events"),
                QStringLiteral("server_groups"),
                QStringLiteral("server_group_members"),
                QStringLiteral("server_group_removed_members"),
                QStringLiteral("server_group_announcements"),
                QStringLiteral("server_group_audit_events")
            };
            const QStringList tables = db.tables();
            QJsonArray missingTables;
            for (const QString& table : requiredTables) {
                if (!tables.contains(table, Qt::CaseInsensitive)) {
                    missingTables.append(table);
                }
            }
            QJsonObject schemaCheck;
            schemaCheck["name"] = QStringLiteral("required-tables");
            schemaCheck["ok"] = missingTables.isEmpty();
            schemaCheck["reason"] = missingTables.isEmpty() ? QStringLiteral("ok") : QStringLiteral("schema");
            schemaCheck["requiredCount"] = requiredTables.size();
            schemaCheck["missingCount"] = missingTables.size();
            schemaCheck["missingTables"] = missingTables;
            if (!missingTables.isEmpty()) {
                healthy = false;
                qWarning() << "Database health missing required tables:"
                           << missingTables
                           << accountDatabaseDriver();
            }
            checks.append(schemaCheck);

            db.close();
        }
    }
    releaseAccountDatabase(connectionName);

    QJsonObject poolCheck;
    const QJsonObject pool = accountDatabasePoolSnapshot();
    poolCheck["name"] = QStringLiteral("connection-pool");
    poolCheck["ok"] = !pool.value("inBackoff").toBool(false);
    poolCheck["reason"] = poolCheck.value("ok").toBool(false)
        ? QStringLiteral("ok")
        : pool.value("lastFailureReason").toString(QStringLiteral("runtime"));
    poolCheck["detail"] = QStringLiteral("created=%1 reused=%2 failures=%3 skips=%4")
        .arg(pool.value("createdConnections").toString(),
             pool.value("reusedConnections").toString(),
             pool.value("openFailures").toString(),
             pool.value("backoffSkips").toString());
    checks.append(poolCheck);

    snapshot["checks"] = checks;
    snapshot["pool"] = pool;
    snapshot["ok"] = healthy;
    snapshot["status"] = healthy ? QStringLiteral("healthy") : QStringLiteral("unhealthy");
    QString reason = QStringLiteral("ok");
    for (const QJsonValue& checkValue : checks) {
        const QJsonObject check = checkValue.toObject();
        if (!check.value("ok").toBool(false)) {
            reason = check.value("reason").toString(QStringLiteral("query"));
            break;
        }
    }
    snapshot["reason"] = reason;
    return snapshot;
}

void Server::stop() {
    if (m_stopping) {
        return;
    }
    m_stopping = true;
    m_serviceReady = false;
    m_serviceReadinessReason = QStringLiteral("server-stopped");
    m_transferCleanupTimer->stop();
    m_offlineAttachmentCleanupTimer->stop();
    for (const ChatUser& user : m_clients.values()) {
        clearRedisPresence(user.id);
    }
    m_redisService->shutdown();
    const QList<QTcpSocket*> sockets = m_clients.keys();
    for (QTcpSocket* socket : sockets) {
        if (!socket) {
            continue;
        }
        disconnect(socket, nullptr, this, nullptr);
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    m_clients.clear();
    m_userSockets.clear();
    m_usedNames.clear();
    m_pendingFileTransfers.clear();
    m_tcpServer->close();
    qDebug() << "Server stopped";
    m_stopping = false;
}

void Server::onNewConnection() {
    QTcpSocket* clientSocket = m_tcpServer->nextPendingConnection();
    if (!clientSocket) return;

    qDebug() << "New connection from:" << clientSocket->peerAddress().toString()
             << "port:" << clientSocket->peerPort();

    connect(clientSocket, &QTcpSocket::readyRead, this, &Server::onClientReadyRead);
    connect(clientSocket, &QTcpSocket::disconnected, this, &Server::onClientDisconnected);
}

void Server::onClientReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray buffer = socket->property("buffer").toByteArray();
    buffer.append(socket->readAll());
    qDebug() << "Received from" << socket->peerAddress().toString() << ":" << buffer.size() << "bytes";

    while (buffer.contains('\n')) {
        int newlineIndex = buffer.indexOf('\n');
        QByteArray line = buffer.left(newlineIndex);
        buffer = buffer.mid(newlineIndex + 1);
        if (line.isEmpty()) continue;

        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isNull() || !doc.isObject()) {
            qWarning() << "Invalid JSON received";
            continue;
        }

        QJsonObject obj = doc.object();
        QString type = obj["type"].toString();
        if (type != "heartbeat" && !ensureServiceReady(socket, type)) {
            continue;
        }
        if (ChatUser* user = findUserBySocket(socket)) {
            user->lastActive = QDateTime::currentDateTime();
            refreshRedisPresence(*user);
            m_heartbeatMonitor->updateClientActivity(user->id);
        }

        if (type == "login") {
            handleLogin(obj, socket);
        } else if (type == "message") {
            handleMessage(obj, socket);
        } else if (type == "file") {
            handleFile(obj, socket);
        } else if (type == "file_chunk") {
            handleFileChunk(obj, socket);
        } else if (type == "file_transfer_resume_query") {
            handleFileTransferResumeQuery(obj, socket);
        } else if (type == "file_transfer_cancel") {
            handleFileTransferCancel(obj, socket);
        } else if (type == "file_chunk_ack") {
            emit fileChunkAckReceived(
                socket,
                obj["transferId"].toString(),
                obj["chunkIndex"].toVariant().toLongLong(),
                obj["accepted"].toBool(false),
                obj["reason"].toString(),
                obj["receivedBytes"].toVariant().toLongLong());
        } else if (type == "private") {
            handleMessage(obj, socket);
        } else if (type == "profile_update") {
            handleProfileUpdate(obj, socket);
        } else if (type == "e2e_identity_announce") {
            handleE2EIdentityAnnouncement(obj, socket);
        } else if (type == "e2e_key_rotation_request" || type == "e2e_key_rotation_response") {
            handleE2EKeyRotation(obj, socket);
        } else if (type == "server_group_create") {
            handleServerGroupCreate(obj, socket);
        } else if (type == "server_group_message") {
            handleServerGroupMessage(obj, socket);
        } else if (type == "server_group_announcement_update") {
            handleServerGroupAnnouncementUpdate(obj, socket);
        } else if (type == "server_group_member_update") {
            handleServerGroupMemberUpdate(obj, socket);
        } else if (type == "friend_request" || type == "friend_response" || type == "friend_search") {
            handleFriendEvent(obj, socket);
        } else if (type == "heartbeat") {
            // Activity was refreshed when the frame was accepted.
        }
    }

    socket->setProperty("buffer", buffer);
}

void Server::onClientDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    if (m_stopping) {
        socket->deleteLater();
        return;
    }

    ChatUser* user = findUserBySocket(socket);
    if (user) {
        recordUserSessionToSqlite(*user, "logout");
        QString userId = user->id;
        QString userName = user->name;
        clearRedisPresence(userId);
        m_heartbeatMonitor->unregisterClient(userId);
        for (const QString& key : m_pendingFileTransfers.keys()) {
            auto it = m_pendingFileTransfers.find(key);
            if (it != m_pendingFileTransfers.end() && it->socket == socket) {
                it->socket = nullptr;
            }
        }
        m_userSockets.remove(userId);
        m_clients.remove(socket);
        m_usedNames.remove(userName);

        emit userLeft(userId, userName);
        emit clientDisconnected(userId);

        Message sysMsg;
        sysMsg.type = MessageType::System;
        sysMsg.content = userName + " 离开了聊天室";
        sysMsg.timestamp = QDateTime::currentDateTime();
        broadcastMessage(sysMsg, socket);
        refreshConnectedClientViews();

        qDebug() << "User disconnected:" << userName;
    }
    socket->deleteLater();
}

bool Server::ensureRedisReadyForStartup() {
    QString error;
    if (!m_redisService->initialize(&error)) {
        m_serviceReady = false;
        m_serviceReadinessReason = m_redisService->readinessReason();
        if (m_serviceReadinessReason == QStringLiteral("redis-required")) {
            qWarning() << "Redis is required for server startup; set QTNETWORKCHAT_REDIS=1";
        } else {
            qWarning() << "Redis startup check failed:" << error;
        }
        return false;
    }

    qDebug() << "Redis presence service enabled";
    qDebug() << "Redis Pub/Sub subscriber enabled";
    refreshServiceReadiness();
    return true;
}

void Server::updateRedisCommandAvailability(bool available, const QString& reason) {
    const bool wasReady = m_serviceReady;
    m_redisService->setCommandAvailability(available, reason);
    if (!available && wasReady) {
        qWarning() << "Redis command channel became unavailable:" << m_redisService->readinessReason();
    }
    refreshServiceReadiness();
}

void Server::updateRedisSubscriberAvailability(bool available, const QString& reason) {
    const bool wasReady = m_serviceReady;
    m_redisService->setSubscriberAvailability(available, reason);
    if (!available && wasReady) {
        qWarning() << "Redis subscriber channel became unavailable:" << m_redisService->readinessReason();
    }
    refreshServiceReadiness();
}

void Server::refreshServiceReadiness() {
    m_serviceReady = m_redisService->isReady();
    m_serviceReadinessReason = m_redisService->readinessReason();
}

void Server::tryRecoverRedisCommandAvailability() {
    m_redisService->recoverCommandAvailability();
    refreshServiceReadiness();
}

void Server::tryRecoverRedisSubscriberAvailability() {
    m_redisService->recoverSubscriberAvailability();
    refreshServiceReadiness();
}

bool Server::ensureServiceReady(QTcpSocket* socket, const QString& action) {
    tryRecoverRedisCommandAvailability();
    tryRecoverRedisSubscriberAvailability();
    if (m_serviceReady) {
        return true;
    }

    if (socket && socket->state() == QAbstractSocket::ConnectedState) {
        const QString actionName = action.trimmed().isEmpty() ? QStringLiteral("request") : action.trimmed();
        sendSystemNotice(socket,
                         QStringLiteral("服务暂不可用：Redis 未就绪，已拒绝 %1。")
                             .arg(actionName));
    }
    return false;
}

void Server::handleLogin(const QJsonObject& obj, QTcpSocket* socket) {
    QString mode = obj["mode"].toString("login");
    QString account = obj["account"].toString().trimmed();
    QString password = obj["password"].toString();
    QString userName = obj["userName"].toString().trimmed();

    if (mode != "register" && account.isEmpty()) account = userName;
    if (userName.isEmpty()) userName = account.isEmpty() ? "User" : account;

    QJsonObject accounts = loadAccountsFromSqlite();
    if (mode == "register") {
        if (password.isEmpty()) {
            QJsonObject response;
            response["type"] = "login_failed";
            response["reason"] = "密码不能为空";
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
            return;
        }
        if (account.isEmpty()) {
            account = generateAccountId(accounts);
        }
        QString passwordHash = makePasswordKdfHash(account, password);
        if (accounts.contains(account)) {
            QJsonObject response;
            response["type"] = "login_failed";
            response["reason"] = "账号已存在";
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
            return;
        }
        QJsonObject accountObj;
        accountObj["passwordHash"] = passwordHash;
        accountObj["userName"] = userName;
        accountObj["userId"] = account;
        accountObj["avatar"] = obj["avatar"].toString();
        accounts[account] = accountObj;
        insertAccountToSqlite(account,
                              passwordHash,
                              userName,
                              accountObj["avatar"].toString());
    } else if (accounts.contains(account)) {
        QJsonObject accountObj = accounts[account].toObject();
        QString storedHash = accountObj["passwordHash"].toString();
        if (storedHash.isEmpty()) {
            storedHash = legacyPasswordHash(account, accountObj["password"].toString());
            accountObj.remove("password");
            accountObj["passwordHash"] = storedHash;
            accounts[account] = accountObj;
            insertAccountToSqlite(account,
                                  storedHash,
                                  accountObj["userName"].toString(userName),
                                  accountObj["avatar"].toString());
        }
        bool needsPasswordHashUpgrade = false;
        if (!verifyStoredPasswordHash(account, password, storedHash, &needsPasswordHashUpgrade)) {
            QJsonObject response;
            response["type"] = "login_failed";
            response["reason"] = "密码错误";
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
            return;
        }
        if (needsPasswordHashUpgrade) {
            const QString upgradedHash = makePasswordKdfHash(account, password);
            accountObj["passwordHash"] = upgradedHash;
            accounts[account] = accountObj;
            updateAccountPasswordHashInSqlite(account, upgradedHash);
        }
        userName = accountObj["userName"].toString(userName);
    } else if (!account.isEmpty()) {
        QJsonObject response;
        response["type"] = "login_failed";
        response["reason"] = "账号不存在，请先注册";
        socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
        socket->write("\n");
        socket->flush();
        return;
    }

    if (m_usedNames.contains(userName)) {
        userName += "_" + QString::number(QDateTime::currentMSecsSinceEpoch() % 10000);
    }

    ChatUser user;
    user.id = account.isEmpty() ? QString::number(QDateTime::currentMSecsSinceEpoch()) : account;
    user.name = userName;
    user.avatar = accounts.value(user.id).toObject().value("avatar").toString();
    user.address = socket->peerAddress();
    user.port = socket->peerPort();
    user.isOnline = true;
    user.lastActive = QDateTime::currentDateTime();

    m_clients[socket] = user;
    m_userSockets[user.id] = socket;
    m_usedNames.insert(userName);
    recordUserSessionToSqlite(user, "login");
    recordDefaultGroupMembership(user);
    refreshRedisPresence(user);
    m_heartbeatMonitor->registerClient(user.id);

    QJsonObject response;
    response["type"] = "login_success";
    response["userId"] = user.id;
    response["userName"] = user.name;
    response["account"] = account;
    response["registered"] = mode == "register";
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();

    refreshConnectedClientViews();
    emit userJoined(user.id, user.name);
    emit clientConnected(user.id);

    Message sysMsg;
    sysMsg.type = MessageType::System;
    sysMsg.content = userName + " 加入了聊天室";
    sysMsg.timestamp = QDateTime::currentDateTime();
    broadcastMessage(sysMsg, socket);

    QPointer<QTcpSocket> socketGuard(socket);
    const QString loggedInUserId = user.id;
    QTimer::singleShot(0, this, [this, socketGuard, loggedInUserId]() {
        if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState) return;
        const ChatUser* currentUser = findUserBySocket(socketGuard);
        if (!currentUser || currentUser->id != loggedInUserId) return;
        sendOfflineMessages(loggedInUserId, socketGuard);
    });

    qDebug() << "User logged in:" << user.name << "id:" << user.id;
}

void Server::handleMessage(const QJsonObject& obj, QTcpSocket* socket) {
    Message msg;
    msg.type = static_cast<MessageType>(obj["messageType"].toInt(static_cast<int>(MessageType::Text)));
    msg.senderId = obj["senderId"].toString();
    msg.senderName = obj["senderName"].toString();
    msg.content = obj["content"].toString();
    msg.receiverId = obj["receiverId"].toString();
    msg.timestamp = QDateTime::currentDateTime();

    if (ChatUser* sender = findUserBySocket(socket)) {
        msg.senderId = sender->id;
        msg.senderName = sender->name;
        msg.senderAvatar = sender->avatar;
    }

    if (obj.value("e2eEnvelope").isObject()) {
        QString reason;
        const E2EEnvelope envelope = E2EEnvelope::fromJson(obj.value("e2eEnvelope").toObject());
        if (!envelope.isValid(&reason)
            || msg.receiverId.isEmpty()
            || envelope.senderId != msg.senderId
            || envelope.receiverId != msg.receiverId) {
            sendSystemNotice(socket, QStringLiteral("加密消息转发失败：端到端加密信封无效"));
            return;
        }
        msg.e2eEnvelope = envelope;
        if (msg.content.trimmed().isEmpty()) {
            msg.content = QStringLiteral("[encrypted]");
        }
    }

    QString deliveryState = "broadcast";
    if (!msg.receiverId.isEmpty()) {
        QTcpSocket* targetSocket = m_userSockets.value(msg.receiverId);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            deliveryState = "direct";
            sendToUser(msg);
        } else {
            bool redisOnline = false;
            if (isRedisUserOnline(msg.receiverId, &redisOnline) && redisOnline) {
                deliveryState = "remote";
            } else {
                deliveryState = "offline";
                sendToUser(msg);
            }
        }
    } else {
        if (!isServerGroupMember("public", msg.senderId)) {
            ChatUser* sender = findUserBySocket(socket);
            if (sender) {
                recordDefaultGroupMembership(*sender);
            }
        }
        if (!isServerGroupMember("public", msg.senderId)) {
            sendSystemNotice(socket, "公共群消息发送失败：你已不在该群组，请联系群主或管理员重新邀请。");
            return;
        }
        broadcastMessage(msg);
    }
    saveMessageToSqlite(msg, deliveryState);
    const bool redisPublished = publishRedisMessageEvent(msg, deliveryState);
    if (deliveryState == "remote" && !redisPublished) {
        sendSystemNotice(socket, QStringLiteral("私聊消息发送失败：Redis 路由不可用，请等待服务恢复。"));
        return;
    }

    emit newMessage(msg);
}

void Server::handleE2EIdentityAnnouncement(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* sender = findUserBySocket(socket);
    if (!sender) {
        sendSystemNotice(socket, QStringLiteral("端到端加密身份公告失败：请先登录"));
        return;
    }

    const QString receiverId = obj.value("receiverId").toString().trimmed();
    const QJsonObject identity = obj.value("e2eIdentity").toObject();
    QString reason;
    if (!receiverId.isEmpty() && receiverId == sender->id) {
        sendSystemNotice(socket, QStringLiteral("端到端加密身份公告失败：请求无效"));
        return;
    }
    if (identity.value("userId").toString().trimmed() != sender->id
        || !validateE2EIdentityJson(identity, &reason)) {
        sendSystemNotice(socket, QStringLiteral("端到端加密身份公告失败：身份材料无效"));
        return;
    }

    QJsonObject forwarded;
    forwarded["type"] = QStringLiteral("e2e_identity_announce");
    forwarded["senderId"] = sender->id;
    forwarded["senderName"] = sender->name;
    forwarded["e2eIdentity"] = identity;
    if (!receiverId.isEmpty()) {
        forwarded["receiverId"] = receiverId;
        QTcpSocket* targetSocket = m_userSockets.value(receiverId);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            targetSocket->write(QJsonDocument(forwarded).toJson(QJsonDocument::Compact));
            targetSocket->write("\n");
            targetSocket->flush();
            return;
        }
        if (publishRedisE2EControlEvent(forwarded)) {
            return;
        }
        if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) {
            sendSystemNotice(socket, QStringLiteral("端到端加密身份公告失败：对方不在线，未缓存身份材料"));
            return;
        }
    }

    for (QTcpSocket* targetSocket : m_clients.keys()) {
        if (targetSocket == socket || targetSocket->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        targetSocket->write(QJsonDocument(forwarded).toJson(QJsonDocument::Compact));
        targetSocket->write("\n");
        targetSocket->flush();
    }
}

void Server::handleE2EKeyRotation(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* sender = findUserBySocket(socket);
    if (!sender) {
        sendSystemNotice(socket, QStringLiteral("端到端加密轮换失败：请先登录"));
        return;
    }

    const QString type = obj.value("type").toString();
    const QString receiverId = obj.value("receiverId").toString().trimmed();
    const E2EKeyAgreement agreement = E2EKeyAgreement::fromJson(obj.value("e2eKeyAgreement").toObject());
    QString reason;
    if ((type != QLatin1String("e2e_key_rotation_request") && type != QLatin1String("e2e_key_rotation_response"))
        || receiverId.isEmpty()
        || !agreement.isValid(&reason)
        || agreement.senderId != sender->id
        || agreement.receiverId != receiverId) {
        sendSystemNotice(socket, QStringLiteral("端到端加密轮换失败：请求无效"));
        return;
    }

    QTcpSocket* targetSocket = m_userSockets.value(receiverId);
    if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) {
        QJsonObject forwarded;
        forwarded["type"] = type;
        forwarded["senderId"] = sender->id;
        forwarded["senderName"] = sender->name;
        forwarded["receiverId"] = receiverId;
        forwarded["e2eKeyAgreement"] = agreement.toJson();
        forwarded["reason"] = obj.value("reason").toString(type == QLatin1String("e2e_key_rotation_request")
            ? QStringLiteral("manual-request")
            : QStringLiteral("accepted"));
        if (type == QLatin1String("e2e_key_rotation_response")) {
            forwarded["accepted"] = obj.value("accepted").toBool(false);
        }
        if (publishRedisE2EControlEvent(forwarded)) {
            return;
        }
        sendSystemNotice(socket, QStringLiteral("端到端加密轮换失败：对方不在线，未缓存轮换材料"));
        return;
    }

    QJsonObject forwarded;
    forwarded["type"] = type;
    forwarded["senderId"] = sender->id;
    forwarded["senderName"] = sender->name;
    forwarded["receiverId"] = receiverId;
    forwarded["e2eKeyAgreement"] = agreement.toJson();
    forwarded["reason"] = obj.value("reason").toString(type == QLatin1String("e2e_key_rotation_request")
        ? QStringLiteral("manual-request")
        : QStringLiteral("accepted"));
    if (type == QLatin1String("e2e_key_rotation_response")) {
        forwarded["accepted"] = obj.value("accepted").toBool(false);
    }

    targetSocket->write(QJsonDocument(forwarded).toJson(QJsonDocument::Compact));
    targetSocket->write("\n");
    targetSocket->flush();
}

void Server::handleServerGroupCreate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) {
        sendSystemNotice(socket, "创建群组失败：请先登录");
        return;
    }

    QString groupName = obj.value("groupName").toString().trimmed();
    QString announcement = obj.value("announcement").toString().trimmed();
    if (groupName.isEmpty()) {
        sendSystemNotice(socket, "创建群组失败：群名称不能为空");
        return;
    }
    if (groupName.size() > 80) {
        groupName = groupName.left(80);
    }
    if (announcement.isEmpty()) {
        announcement = QStringLiteral("私有群已创建。");
    }
    if (announcement.size() > 1000) {
        announcement = announcement.left(1000);
    }
    if (!ensureAccountDatabase()) {
        sendSystemNotice(socket, "创建群组失败：服务端群组存储不可用");
        return;
    }

    const QStringList requestedMemberIds = initialServerGroupMemberIds(obj, requester->id);
    QStringList memberIdsForSnapshot{requester->id};
    QStringList addedInitialMemberIds;
    QJsonArray addedInitialMembers;
    QJsonArray skippedInitialMemberIds;
    const QString groupId = QStringLiteral("private-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool created = false;
    QString errorText;
    const QString connectionName = "server_group_create_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (!openAccountDatabaseConnection(db, connectionName)) {
            errorText = "创建群组失败：无法打开群组数据库";
        } else if (!db.transaction()) {
            errorText = "创建群组失败：无法开启事务";
        } else {
            QSqlQuery groupQuery(db);
            groupQuery.prepare("INSERT INTO server_groups(group_id, group_name, owner_id, announcement, group_type, history_policy, file_policy, created_at, updated_at) "
                               "VALUES(?, ?, ?, ?, 'private', 'member-and-removed-readonly', 'members-only', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
            groupQuery.addBindValue(groupId);
            groupQuery.addBindValue(groupName);
            groupQuery.addBindValue(requester->id);
            groupQuery.addBindValue(announcement);
            if (!groupQuery.exec()) {
                errorText = "创建群组失败：保存群组失败";
            }

            if (errorText.isEmpty()) {
                QSqlQuery memberQuery(db);
                memberQuery.prepare("INSERT INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) "
                                    "VALUES(?, ?, ?, 'owner', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
                memberQuery.addBindValue(groupId);
                memberQuery.addBindValue(requester->id);
                memberQuery.addBindValue(requester->name);
                if (!memberQuery.exec()) {
                    errorText = "创建群组失败：保存群主成员失败";
                }
            }

            if (errorText.isEmpty()) {
                for (const QString& memberId : requestedMemberIds) {
                    QSqlQuery accountQuery(db);
                    accountQuery.prepare("SELECT COALESCE(user_name, '') FROM accounts WHERE account = ?");
                    accountQuery.addBindValue(memberId);
                    if (!accountQuery.exec()) {
                        errorText = "创建群组失败：查询初始成员失败";
                        break;
                    }
                    if (!accountQuery.next()) {
                        skippedInitialMemberIds.append(memberId);
                        continue;
                    }

                    const QString memberName = accountQuery.value(0).toString().trimmed().isEmpty()
                        ? memberId
                        : accountQuery.value(0).toString().trimmed();
                    QSqlQuery initialMemberQuery(db);
                    initialMemberQuery.prepare("INSERT INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) "
                                               "VALUES(?, ?, ?, 'member', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
                    initialMemberQuery.addBindValue(groupId);
                    initialMemberQuery.addBindValue(memberId);
                    initialMemberQuery.addBindValue(memberName);
                    if (!initialMemberQuery.exec()) {
                        errorText = "创建群组失败：保存初始成员失败";
                        break;
                    }

                    memberIdsForSnapshot << memberId;
                    addedInitialMemberIds << memberId;
                    QJsonObject addedMember;
                    addedMember[QStringLiteral("userId")] = memberId;
                    addedMember[QStringLiteral("userName")] = memberName;
                    addedInitialMembers.append(addedMember);
                }
            }

            if (errorText.isEmpty() && db.commit()) {
                created = true;
            } else {
                if (errorText.isEmpty()) {
                    errorText = "创建群组失败：提交事务失败";
                }
                db.rollback();
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);

    if (!created) {
        sendSystemNotice(socket, errorText.isEmpty() ? "创建群组失败" : errorText);
        return;
    }

    QJsonObject auditDetails{
        {QStringLiteral("groupType"), QStringLiteral("private")},
        {QStringLiteral("historyPolicy"), QStringLiteral("member-and-removed-readonly")},
        {QStringLiteral("filePolicy"), QStringLiteral("members-only")},
        {QStringLiteral("initialMemberCount"), addedInitialMembers.size()}
    };
    if (!addedInitialMembers.isEmpty()) {
        auditDetails[QStringLiteral("initialMembers")] = addedInitialMembers;
    }
    if (!skippedInitialMemberIds.isEmpty()) {
        auditDetails[QStringLiteral("skippedInitialMemberIds")] = skippedInitialMemberIds;
    }
    recordServerGroupAuditEvent(groupId,
                                QStringLiteral("create_private_group"),
                                requester->id,
                                requester->name,
                                requester->id,
                                requester->name,
                                auditDetails);
    sendSystemNotice(socket, QString("私有群 %1 已创建").arg(groupName));
    const QString initialMemberNotice = QString("你已被加入私有群 %1").arg(groupName);
    for (const QString& memberId : memberIdsForSnapshot) {
        QTcpSocket* memberSocket = memberId == requester->id ? socket : m_userSockets.value(memberId);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        if (memberId != requester->id) {
            sendSystemNotice(memberSocket, initialMemberNotice);
        }
        sendServerGroupSnapshot(memberId, memberSocket);
    }
    publishRedisServerGroupSnapshotRefresh(addedInitialMemberIds, groupId, initialMemberNotice);
}

void Server::handleProfileUpdate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* user = findUserBySocket(socket);
    if (!user) {
        sendSystemNotice(socket, QStringLiteral("头像更新失败：请先登录"));
        return;
    }

    const QString avatarBase64 = obj.value("avatar").toString().trimmed();
    if (!ensureAccountDatabase()) {
        sendSystemNotice(socket, QStringLiteral("头像更新失败：账号存储不可用"));
        return;
    }

    const QJsonObject accounts = loadAccountsFromSqlite();
    const QJsonObject existing = accounts.value(user->id).toObject();
    const QString passwordHash = existing.value("passwordHash").toString();
    const QString userName = existing.value("userName").toString(user->name);
    if (passwordHash.isEmpty()) {
        sendSystemNotice(socket, QStringLiteral("头像更新失败：账号资料缺失"));
        return;
    }

    if (!insertAccountToSqlite(user->id, passwordHash, userName, avatarBase64)) {
        sendSystemNotice(socket, QStringLiteral("头像更新失败：保存到账号资料失败"));
        return;
    }

    user->avatar = avatarBase64;
    refreshConnectedClientViews();
}

void Server::handleServerGroupMessage(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* sender = findUserBySocket(socket);
    if (!sender) {
        sendSystemNotice(socket, "群消息发送失败：请先登录");
        return;
    }
    const QString groupId = obj.value("groupId").toString().trimmed();
    const QString content = obj.value("content").toString();
    if (groupId.isEmpty() || content.trimmed().isEmpty()) {
        sendSystemNotice(socket, "群消息发送失败：请求参数无效");
        return;
    }
    if (!isServerGroupMember(groupId, sender->id)) {
        sendSystemNotice(socket, groupId == QLatin1String("public")
            ? QStringLiteral("公共群消息发送失败：你已不在该群组，请联系群主或管理员重新邀请。")
            : QStringLiteral("私有群消息发送失败：你不在该群组或已被移出。"));
        return;
    }

    QStringList memberIds;
    const QString connectionName = "server_group_message_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery memberQuery(db);
            memberQuery.prepare("SELECT user_id FROM server_group_members WHERE group_id = ?");
            memberQuery.addBindValue(groupId);
            if (memberQuery.exec()) {
                while (memberQuery.next()) {
                    const QString memberId = memberQuery.value(0).toString();
                    if (!memberId.isEmpty() && !memberIds.contains(memberId)) {
                        memberIds << memberId;
                    }
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);

    Message msg;
    msg.senderId = sender->id;
    msg.senderName = sender->name;
    msg.senderAvatar = sender->avatar;
    msg.receiverId = groupId;
    msg.content = content;
    msg.type = MessageType::Text;
    msg.timestamp = QDateTime::currentDateTime();

    for (const QString& memberId : memberIds) {
        QTcpSocket* memberSocket = m_userSockets.value(memberId);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        QJsonObject forwarded = QJsonDocument::fromJson(msg.toJson()).object();
        forwarded["type"] = "server_group_message";
        forwarded["groupId"] = groupId;
        memberSocket->write(QJsonDocument(forwarded).toJson(QJsonDocument::Compact));
        memberSocket->write("\n");
        memberSocket->flush();
    }
    saveMessageToSqlite(msg, QStringLiteral("server-group"));
    emit newMessage(msg);
}

void Server::handleServerGroupAnnouncementUpdate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) {
        sendSystemNotice(socket, "群公告更新失败：请先登录");
        return;
    }

    const QString groupId = obj["groupId"].toString("public").trimmed().isEmpty()
        ? QString("public")
        : obj["groupId"].toString("public").trimmed();
    QString announcement = obj["announcement"].toString().trimmed();
    if (announcement.isEmpty()) {
        announcement = "欢迎来到公共聊天室。";
    }
    if (announcement.size() > 1000) {
        announcement = announcement.left(1000);
    }
    if (!ensureAccountDatabase()) {
        sendSystemNotice(socket, "群公告更新失败：服务端群组存储不可用");
        return;
    }

    QStringList memberIds;
    bool allowed = false;
    bool saved = false;
    QString errorText;
    const QString connectionName = "server_group_announcement_update_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (!openAccountDatabaseConnection(db, connectionName)) {
            errorText = "群公告更新失败：无法打开群组数据库";
        } else {
            QSqlQuery permissionQuery(db);
            permissionQuery.prepare("SELECT COALESCE(g.owner_id, ''), COALESCE(m.role, '') "
                                    "FROM server_groups g "
                                    "JOIN server_group_members m ON m.group_id = g.group_id "
                                    "WHERE g.group_id = ? AND m.user_id = ?");
            permissionQuery.addBindValue(groupId);
            permissionQuery.addBindValue(requester->id);
            if (!permissionQuery.exec()) {
                errorText = "群公告更新失败：权限校验失败";
            } else if (!permissionQuery.next()) {
                errorText = "群公告更新失败：你不在该群组";
            } else {
                const QString ownerId = permissionQuery.value(0).toString();
                const QString role = permissionQuery.value(1).toString().toLower();
                allowed = ownerId == requester->id || role == "owner" || role == "admin";
                if (!allowed) {
                    errorText = "群公告更新失败：只有群主或管理员可以编辑";
                }
            }

            if (allowed) {
                QSqlQuery updateQuery(db);
                updateQuery.prepare("UPDATE server_groups SET announcement = ?, updated_at = CURRENT_TIMESTAMP "
                                    "WHERE group_id = ?");
                updateQuery.addBindValue(announcement);
                updateQuery.addBindValue(groupId);
                saved = updateQuery.exec();
                if (!saved) {
                    errorText = "群公告更新失败：保存公告失败";
                }
            }

            if (saved) {
                QSqlQuery insertQuery(db);
                insertQuery.prepare("INSERT INTO server_group_announcements(group_id, author_id, author_name, content, created_at) "
                                    "VALUES(?, ?, ?, ?, CURRENT_TIMESTAMP)");
                insertQuery.addBindValue(groupId);
                insertQuery.addBindValue(requester->id);
                insertQuery.addBindValue(requester->name);
                insertQuery.addBindValue(announcement);
                if (!insertQuery.exec()) {
                    qWarning() << "Failed to record server group announcement history:" << insertQuery.lastError().text();
                }
            }

            if (saved) {
                QSqlQuery memberQuery(db);
                memberQuery.prepare("SELECT user_id FROM server_group_members WHERE group_id = ?");
                memberQuery.addBindValue(groupId);
                if (memberQuery.exec()) {
                    while (memberQuery.next()) {
                        const QString memberId = memberQuery.value(0).toString();
                        if (!memberId.isEmpty() && !memberIds.contains(memberId)) {
                            memberIds << memberId;
                        }
                    }
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);

    if (!saved) {
        QJsonObject details;
        details["requestedAction"] = QStringLiteral("announcement_update");
        details["rejected"] = true;
        details["reason"] = errorText.isEmpty() ? QStringLiteral("unknown") : errorText;
        details["contentLength"] = announcement.size();
        recordServerGroupAuditEvent(groupId,
                                    QStringLiteral("announcement_rejected"),
                                    requester->id,
                                    requester->name,
                                    QString(),
                                    QString(),
                                    details);
        const QStringList snapshotUserIds = serverGroupMemberIds(groupId);
        for (const QString& userId : snapshotUserIds) {
            QTcpSocket* memberSocket = m_userSockets.value(userId);
            if (memberSocket && memberSocket->state() == QAbstractSocket::ConnectedState) {
                sendServerGroupSnapshot(userId, memberSocket);
            }
        }
        sendSystemNotice(socket, errorText.isEmpty() ? "群公告更新失败" : errorText);
        return;
    }

    recordServerGroupAuditEvent(groupId,
                                QStringLiteral("announcement_update"),
                                requester->id,
                                requester->name,
                                QString(),
                                QString(),
                                QJsonObject{{QStringLiteral("contentLength"), announcement.size()}});

    const QString notice = QString("%1 更新了群公告").arg(requester->name.isEmpty() ? requester->id : requester->name);
    for (const QString& memberId : memberIds) {
        QTcpSocket* memberSocket = m_userSockets.value(memberId);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        sendSystemNotice(memberSocket, notice);
        sendServerGroupSnapshot(memberId, memberSocket);
    }
}

void Server::handleServerGroupMemberUpdate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) {
        sendSystemNotice(socket, "群成员变更失败：请先登录");
        return;
    }

    const QString groupId = obj["groupId"].toString("public").trimmed().isEmpty()
        ? QString("public")
        : obj["groupId"].toString("public").trimmed();
    const QString action = obj["action"].toString().trimmed().toLower();
    const QString memberId = obj["memberId"].toString().trimmed();
    const bool roleAction = action == QLatin1String("promote_admin") || action == QLatin1String("demote_admin");
    if ((action != "add" && action != "remove" && !roleAction) || groupId.isEmpty() || memberId.isEmpty()) {
        sendSystemNotice(socket, "群成员变更失败：请求参数无效");
        return;
    }
    if (!ensureAccountDatabase()) {
        sendSystemNotice(socket, "群成员变更失败：服务端群组存储不可用");
        return;
    }

    QStringList affectedUserIds;
    bool changed = false;
    QString memberName;
    QString errorText;
    const QString connectionName = "server_group_member_update_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (!openAccountDatabaseConnection(db, connectionName)) {
            errorText = "群成员变更失败：无法打开群组数据库";
        } else {
            QString ownerId;
            QString requesterRole;
            QSqlQuery permissionQuery(db);
            permissionQuery.prepare("SELECT COALESCE(g.owner_id, ''), COALESCE(m.role, '') "
                                    "FROM server_groups g "
                                    "JOIN server_group_members m ON m.group_id = g.group_id "
                                    "WHERE g.group_id = ? AND m.user_id = ?");
            permissionQuery.addBindValue(groupId);
            permissionQuery.addBindValue(requester->id);
            if (!permissionQuery.exec()) {
                errorText = "群成员变更失败：权限校验失败";
            } else if (!permissionQuery.next()) {
                errorText = "群成员变更失败：你不在该群组";
            } else {
                ownerId = permissionQuery.value(0).toString();
                requesterRole = permissionQuery.value(1).toString().toLower();
                const bool allowed = ownerId == requester->id || requesterRole == "owner" || requesterRole == "admin";
                if (!allowed) {
                    errorText = "群成员变更失败：只有群主或管理员可以管理成员";
                } else if (roleAction && ownerId != requester->id && requesterRole != QLatin1String("owner")) {
                    errorText = "群成员变更失败：只有群主可以设置管理员";
                }
            }

            if (errorText.isEmpty() && action == "add") {
                QSqlQuery accountQuery(db);
                accountQuery.prepare("SELECT COALESCE(user_name, '') FROM accounts WHERE account = ?");
                accountQuery.addBindValue(memberId);
                if (!accountQuery.exec()) {
                    errorText = "群成员变更失败：账号查询失败";
                } else if (!accountQuery.next()) {
                    errorText = "群成员变更失败：目标账号不存在";
                } else {
                    memberName = accountQuery.value(0).toString();
                    if (memberName.isEmpty()) memberName = memberId;
                }
            }

            bool alreadyMember = false;
            QString targetRole;
            if (errorText.isEmpty()) {
                QSqlQuery targetQuery(db);
                targetQuery.prepare("SELECT COALESCE(user_name, ''), COALESCE(role, '') "
                                    "FROM server_group_members WHERE group_id = ? AND user_id = ?");
                targetQuery.addBindValue(groupId);
                targetQuery.addBindValue(memberId);
                if (!targetQuery.exec()) {
                    errorText = "群成员变更失败：成员查询失败";
                } else if (targetQuery.next()) {
                    alreadyMember = true;
                    if (memberName.isEmpty()) memberName = targetQuery.value(0).toString();
                    targetRole = targetQuery.value(1).toString().toLower();
                }
            }

            if (errorText.isEmpty() && action == "add") {
                if (alreadyMember) {
                    errorText = "该用户已经是群成员";
                } else {
                    QSqlQuery insertQuery(db);
                    insertQuery.prepare("INSERT INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) "
                                        "VALUES(?, ?, ?, 'member', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
                    insertQuery.addBindValue(groupId);
                    insertQuery.addBindValue(memberId);
                    insertQuery.addBindValue(memberName);
                    if (!insertQuery.exec()) {
                        errorText = "群成员变更失败：添加成员失败";
                    } else {
                        QSqlQuery clearRemovedQuery(db);
                        clearRemovedQuery.prepare("DELETE FROM server_group_removed_members WHERE group_id = ? AND user_id = ?");
                        clearRemovedQuery.addBindValue(groupId);
                        clearRemovedQuery.addBindValue(memberId);
                        if (!clearRemovedQuery.exec()) {
                            qWarning() << "Failed to clear removed group member marker:" << clearRemovedQuery.lastError().text();
                        }
                        changed = true;
                    }
                }
            } else if (errorText.isEmpty() && action == "remove") {
                if (!alreadyMember) {
                    errorText = "群成员变更失败：目标用户不在该群组";
                } else if (memberId == ownerId || targetRole == "owner") {
                    errorText = "群成员变更失败：不能移出群主";
                } else if (requesterRole == QLatin1String("admin") && targetRole == QLatin1String("admin")) {
                    errorText = "群成员变更失败：管理员不能移出其他管理员";
                } else if (memberId == requester->id) {
                    errorText = "群成员变更失败：不能通过管理操作移出自己";
                } else {
                    QSqlQuery deleteQuery(db);
                    deleteQuery.prepare("DELETE FROM server_group_members WHERE group_id = ? AND user_id = ?");
                    deleteQuery.addBindValue(groupId);
                    deleteQuery.addBindValue(memberId);
                    if (!deleteQuery.exec()) {
                        errorText = "群成员变更失败：移出成员失败";
                    } else {
                        QSqlQuery removedQuery(db);
                        removedQuery.prepare(insertReplaceSql(
                            QStringLiteral("server_group_removed_members"),
                            {QStringLiteral("group_id"), QStringLiteral("user_id"), QStringLiteral("removed_by"), QStringLiteral("removed_by_name"), QStringLiteral("removed_at")},
                            {QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("CURRENT_TIMESTAMP")},
                            {QStringLiteral("group_id"), QStringLiteral("user_id")},
                            {QStringLiteral("removed_by = EXCLUDED.removed_by"),
                             QStringLiteral("removed_by_name = EXCLUDED.removed_by_name"),
                             QStringLiteral("removed_at = EXCLUDED.removed_at")}));
                        removedQuery.addBindValue(groupId);
                        removedQuery.addBindValue(memberId);
                        removedQuery.addBindValue(requester->id);
                        removedQuery.addBindValue(requester->name);
                        if (!removedQuery.exec()) {
                            qWarning() << "Failed to record removed group member marker:" << removedQuery.lastError().text();
                        }
                        changed = true;
                    }
                }
            } else if (errorText.isEmpty() && roleAction) {
                if (!alreadyMember) {
                    errorText = "群成员变更失败：目标用户不在该群组";
                } else if (memberId == ownerId || targetRole == "owner") {
                    errorText = "群成员变更失败：群主角色不能被修改";
                } else if (memberId == requester->id) {
                    errorText = "群成员变更失败：不能修改自己的管理员角色";
                } else {
                    const QString desiredRole = action == QLatin1String("promote_admin")
                        ? QStringLiteral("admin")
                        : QStringLiteral("member");
                    if (targetRole == desiredRole) {
                        errorText = action == QLatin1String("promote_admin")
                            ? QStringLiteral("群成员变更失败：目标用户已经是管理员")
                            : QStringLiteral("群成员变更失败：目标用户已经是普通成员");
                    } else {
                        QSqlQuery updateRoleQuery(db);
                        updateRoleQuery.prepare("UPDATE server_group_members "
                                                "SET role = ?, updated_at = CURRENT_TIMESTAMP "
                                                "WHERE group_id = ? AND user_id = ?");
                        updateRoleQuery.addBindValue(desiredRole);
                        updateRoleQuery.addBindValue(groupId);
                        updateRoleQuery.addBindValue(memberId);
                        if (!updateRoleQuery.exec()) {
                            errorText = "群成员变更失败：更新成员角色失败";
                        } else {
                            changed = true;
                        }
                    }
                }
            }

            if (changed) {
                QSqlQuery memberQuery(db);
                memberQuery.prepare("SELECT user_id FROM server_group_members WHERE group_id = ?");
                memberQuery.addBindValue(groupId);
                if (memberQuery.exec()) {
                    while (memberQuery.next()) {
                        const QString userId = memberQuery.value(0).toString();
                        if (!userId.isEmpty() && !affectedUserIds.contains(userId)) {
                            affectedUserIds << userId;
                        }
                    }
                }
                if (!affectedUserIds.contains(requester->id)) {
                    affectedUserIds << requester->id;
                }
                if (!affectedUserIds.contains(memberId)) {
                    affectedUserIds << memberId;
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);

    if (!changed) {
        QJsonObject details;
        details["requestedAction"] = action;
        details["roleAction"] = roleAction;
        details["rejected"] = true;
        details["reason"] = errorText.isEmpty() ? QStringLiteral("not-applied") : errorText;
        recordServerGroupAuditEvent(groupId,
                                    action.isEmpty() ? QStringLiteral("member_update_rejected") : action + QStringLiteral("_rejected"),
                                    requester->id,
                                    requester->name,
                                    memberId,
                                    memberName,
                                    details);
        const QStringList snapshotUserIds = serverGroupMemberIds(groupId);
        for (const QString& userId : snapshotUserIds) {
            QTcpSocket* memberSocket = m_userSockets.value(userId);
            if (memberSocket && memberSocket->state() == QAbstractSocket::ConnectedState) {
                sendServerGroupSnapshot(userId, memberSocket);
            }
        }
        sendSystemNotice(socket, errorText.isEmpty() ? "群成员变更未生效" : errorText);
        return;
    }

    recordServerGroupAuditEvent(groupId,
                                action,
                                requester->id,
                                requester->name,
                                memberId,
                                memberName,
                                QJsonObject{{QStringLiteral("roleAction"), roleAction}});

    const QString displayName = memberName.isEmpty() ? memberId : memberName;
    QString notice;
    if (action == QLatin1String("add")) {
        notice = QString("%1 已被加入群组").arg(displayName);
    } else if (action == QLatin1String("remove")) {
        notice = QString("%1 已被移出群组").arg(displayName);
    } else if (action == QLatin1String("promote_admin")) {
        notice = QString("%1 已被设为群管理员").arg(displayName);
    } else {
        notice = QString("%1 已被取消群管理员").arg(displayName);
    }
    for (const QString& userId : affectedUserIds) {
        QTcpSocket* memberSocket = m_userSockets.value(userId);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        sendSystemNotice(memberSocket, notice);
        sendServerGroupMemberUpdated(memberSocket, groupId, memberId, action);
        sendServerGroupSnapshot(userId, memberSocket);
    }
}

void Server::handleFriendEvent(const QJsonObject& obj, QTcpSocket* socket) {
    QString type = obj["type"].toString();
    if (type == "friend_search") {
        QString account = obj["account"].toString().trimmed();
        ChatUser* requester = findUserBySocket(socket);
        QJsonObject response;
        response["type"] = "friend_search_result";
        response["account"] = account;
        response["exactMatch"] = false;
        response["matchCount"] = 0;
        response["matchReason"] = "未找到匹配资料";

        QJsonObject accounts = loadAccountsFromSqlite();
        if (!account.isEmpty() && accounts.contains(account)) {
            QJsonObject accountObj = accounts[account].toObject();
            response["found"] = true;
            response["userId"] = account;
            response["userName"] = accountObj["userName"].toString(account);
            bool online = false;
            response["online"] = isRedisUserOnline(account, &online) && online;
            response["exactMatch"] = true;
            response["matchCount"] = 1;
            response["matchReason"] = "QQ号精确匹配";
        } else {
            QString matchedId;
            QString matchedName;
            int matchCount = 0;
            for (auto it = accounts.begin(); it != accounts.end(); ++it) {
                const QString candidateId = it.key();
                const QJsonObject accountObj = it.value().toObject();
                const QString candidateName = accountObj["userName"].toString(candidateId);
                const bool idMatched = candidateId.contains(account, Qt::CaseInsensitive);
                const bool nameMatched = candidateName.contains(account, Qt::CaseInsensitive);
                if (!account.isEmpty() && (idMatched || nameMatched)) {
                    ++matchCount;
                    if (matchedId.isEmpty()) {
                        matchedId = candidateId;
                        matchedName = candidateName;
                        response["matchReason"] = idMatched ? "QQ号模糊匹配" : "昵称模糊匹配";
                    }
                }
            }

            response["matchCount"] = matchCount;
            if (!matchedId.isEmpty()) {
                response["found"] = true;
                response["userId"] = matchedId;
                response["userName"] = matchedName.isEmpty() ? matchedId : matchedName;
                bool online = false;
                response["online"] = isRedisUserOnline(matchedId, &online) && online;
            } else {
                response["found"] = false;
                response["online"] = false;
            }
        }
        const QString searchState = response["found"].toBool()
            ? QString("%1_%2").arg(response["exactMatch"].toBool() ? "found_exact" : "found_fuzzy",
                                   response["online"].toBool() ? "online" : "offline")
            : "not_found";
        saveFriendEventToSqlite(type,
                                requester ? requester->id : QString(),
                                requester ? requester->name : QString(),
                                response["userId"].toString(),
                                account,
                                searchState);

        if (socket && socket->state() == QAbstractSocket::ConnectedState) {
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
        }
        return;
    }

    QString receiverId = obj["receiverId"].toString();
    const QString senderId = obj["senderId"].toString();
    const QString senderName = obj["senderName"].toString();
    QTcpSocket* targetSocket = m_userSockets.value(receiverId);
    if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) {
        saveFriendEventToSqlite(type, senderId, senderName, receiverId, QString(), "target_offline", obj["accepted"].toBool(false));
        if (type == "friend_request" && socket && socket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject response;
            response["type"] = "friend_request_sent";
            response["receiverId"] = receiverId;
            response["delivered"] = false;
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
        }
        return;
    }
    saveFriendEventToSqlite(type, senderId, senderName, receiverId, QString(), "delivered", obj["accepted"].toBool(false));

    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    targetSocket->write(data);
    targetSocket->write("\n");
    targetSocket->flush();

    if (type == "friend_request" && socket && socket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject response;
        response["type"] = "friend_request_sent";
        response["receiverId"] = receiverId;
        response["delivered"] = true;
        socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
        socket->write("\n");
        socket->flush();
    }
}

void Server::handleFile(const QJsonObject& obj, QTcpSocket* socket) {
    Message msg;
    msg.senderId = obj["senderId"].toString();
    msg.senderName = obj["senderName"].toString();
    msg.receiverId = obj["receiverId"].toString();
    const QString serverGroupId = obj["groupId"].toString().trimmed();
    msg.content = obj["content"].toString();
    msg.fileName = obj["fileName"].toString();
    msg.transferId = obj["transferId"].toString().trimmed();
    msg.fileSize = obj["fileSize"].toVariant().toLongLong();
    msg.fileHash = obj["fileHash"].toString();
    msg.e2eFileEncrypted = obj["e2eFileEncrypted"].toBool(false);
    msg.e2eFileKeyId = obj["e2eFileKeyId"].toString();
    msg.e2eFileKeyFingerprint = obj["e2eFileKeyFingerprintSha256"].toString();
    msg.e2eFilePlainSize = obj["e2eFilePlainSize"].toVariant().toLongLong();
    msg.e2eFilePlainHash = obj["e2eFilePlainHash"].toString();
    if (obj.value("e2eEnvelope").isObject()) {
        const QJsonObject envelopeObject = obj.value("e2eEnvelope").toObject();
        const E2EEnvelope envelope = E2EEnvelope::fromJson(envelopeObject);
        if (envelope.isValid()) {
            msg.e2eEnvelope = envelope;
        } else if (e2eEnvelopeHeaderLooksSafe(envelopeObject)) {
            msg.e2eEnvelopeHeader = envelopeObject;
        }
    }
    if (msg.e2eFileEncrypted
        && !msg.e2eEnvelope.isValid()
        && msg.e2eEnvelopeHeader.isEmpty()) {
        sendSystemNotice(socket, QStringLiteral("端到端加密文件转发失败：信封头无效"));
        return;
    }
    const qint64 declaredChunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 declaredChunkCount = obj["chunkCount"].toVariant().toLongLong();
    msg.chunkSize = declaredChunkSize;
    msg.chunkCount = declaredChunkCount;
    msg.type = static_cast<MessageType>(obj["messageType"].toInt(static_cast<int>(MessageType::File)));
    msg.timestamp = QDateTime::currentDateTime();

    if (ChatUser* sender = findUserBySocket(socket)) {
        msg.senderId = sender->id;
        msg.senderName = sender->name;
        msg.senderAvatar = sender->avatar;
    }
    const QString routedGroupId = !serverGroupId.isEmpty()
        ? serverGroupId
        : (msg.receiverId.isEmpty() ? QStringLiteral("public") : QString());
    if (!routedGroupId.isEmpty() && !isServerGroupMember(routedGroupId, msg.senderId)) {
        QJsonObject details;
        details["fileName"] = msg.fileName;
        details["reason"] = QStringLiteral("not-member");
        details["filePolicy"] = serverGroupFilePolicy(serverGroupTypeFromId(routedGroupId));
        recordServerGroupAuditEvent(routedGroupId,
                                    QStringLiteral("file_rejected"),
                                    msg.senderId,
                                    msg.senderName,
                                    msg.senderId,
                                    msg.senderName,
                                    details);
        const QStringList memberIds = serverGroupMemberIds(routedGroupId);
        for (const QString& memberId : memberIds) {
            QTcpSocket* memberSocket = m_userSockets.value(memberId);
            if (memberSocket && memberSocket->state() == QAbstractSocket::ConnectedState) {
                sendServerGroupSnapshot(memberId, memberSocket);
            }
        }
        sendSystemNotice(socket, routedGroupId == QLatin1String("public")
            ? QStringLiteral("公共群文件发送失败：你已不在该群组，请联系群主或管理员重新邀请。")
            : QStringLiteral("私有群文件发送失败：你不在该群组或已被移出。"));
        return;
    }

    QString base64Data = obj["fileData"].toString();
    if (!base64Data.isEmpty()) {
        msg.fileData = QByteArray::fromBase64(base64Data.toLatin1());
    }
    const qint64 declaredSize = msg.fileSize;
    const QString declaredHash = msg.fileHash.trimmed();
    const qint64 actualSize = msg.fileData.size();
    const QString actualHash = msg.fileData.isEmpty()
        ? QString()
        : QString::fromLatin1(QCryptographicHash::hash(msg.fileData, QCryptographicHash::Sha256).toHex());

    QStringList integrityErrors;
    if (actualSize <= 0) {
        integrityErrors << "文件内容为空";
    }
    if (declaredSize < 0) {
        integrityErrors << "声明大小非法";
    }
    if (declaredSize > kMaxIncomingPayloadBytes || actualSize > kMaxIncomingPayloadBytes) {
        integrityErrors << QString("超过服务器限制 %1 MB").arg(kMaxIncomingPayloadBytes / 1024 / 1024);
    }
    if (declaredSize > 0 && declaredSize != actualSize) {
        integrityErrors << QString("大小不一致：声明 %1 字节，实际 %2 字节").arg(declaredSize).arg(actualSize);
    }
    if (!msg.e2eFileEncrypted && !declaredHash.isEmpty() && actualHash.compare(declaredHash, Qt::CaseInsensitive) != 0) {
        integrityErrors << "SHA-256 不一致";
    }
    if (declaredChunkSize < 0 || declaredChunkCount < 0) {
        integrityErrors << "分片元数据非法";
    } else if (declaredChunkSize > 0 || declaredChunkCount > 0) {
        if (declaredChunkSize <= 0 || declaredChunkCount <= 0) {
            integrityErrors << "分片元数据不完整";
        } else {
            const qint64 basisSize = declaredSize > 0 ? declaredSize : actualSize;
            const qint64 expectedChunkCount = (basisSize + declaredChunkSize - 1) / declaredChunkSize;
            if (expectedChunkCount != declaredChunkCount) {
                integrityErrors << QString("分片数量不一致：声明 %1 片，预期 %2 片")
                                       .arg(declaredChunkCount)
                                       .arg(expectedChunkCount);
            }
        }
    }
    if (!integrityErrors.isEmpty()) {
        const QString visibleName = msg.fileName.isEmpty() ? "未命名文件" : msg.fileName;
        sendSystemNotice(socket, QString("文件传输已被服务端拒绝：%1，%2。请重新发送。")
                                .arg(visibleName, integrityErrors.join("；")));
        qWarning() << "Rejected file transfer from" << msg.senderId << msg.fileName << integrityErrors;
        return;
    }

    if (msg.fileSize <= 0) {
        msg.fileSize = msg.fileData.size();
    }
    if (msg.fileHash.isEmpty() && !actualHash.isEmpty()) {
        msg.fileHash = actualHash;
    }

    QString deliveryState = "broadcast";
    if (!routedGroupId.isEmpty() && routedGroupId != QLatin1String("public")) {
        msg.receiverId = routedGroupId;
        deliveryState = "server-group-file";
        const QStringList memberIds = serverGroupMemberIds(routedGroupId);
        for (const QString& memberId : memberIds) {
            if (memberId == msg.senderId) continue;
            QTcpSocket* memberSocket = m_userSockets.value(memberId);
            if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
            Message groupMsg = msg;
            groupMsg.receiverId = routedGroupId;
            if (groupMsg.type == MessageType::File || groupMsg.type == MessageType::Image) {
                sendChunkedFileToSocket(groupMsg, memberSocket);
            } else {
                QJsonObject forwarded = QJsonDocument::fromJson(groupMsg.toJson()).object();
                forwarded["type"] = "server_group_message";
                forwarded["groupId"] = routedGroupId;
                memberSocket->write(QJsonDocument(forwarded).toJson(QJsonDocument::Compact));
                memberSocket->write("\n");
                memberSocket->flush();
            }
        }
        QJsonObject details;
        details["fileName"] = msg.fileName;
        details["fileSize"] = QString::number(msg.fileSize);
        details["transferId"] = msg.transferId;
        details["filePolicy"] = serverGroupFilePolicy(QStringLiteral("private"));
        recordServerGroupAuditEvent(routedGroupId,
                                    QStringLiteral("file_sent"),
                                    msg.senderId,
                                    msg.senderName,
                                    msg.senderId,
                                    msg.senderName,
                                    details);
        for (const QString& memberId : memberIds) {
            QTcpSocket* memberSocket = m_userSockets.value(memberId);
            if (memberSocket && memberSocket->state() == QAbstractSocket::ConnectedState) {
                sendServerGroupSnapshot(memberId, memberSocket);
            }
        }
    } else if (!msg.receiverId.isEmpty()) {
        QTcpSocket* targetSocket = m_userSockets.value(msg.receiverId);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            deliveryState = "direct";
            sendToUser(msg);
        } else {
            bool redisOnline = false;
            const bool redisPresenceKnown = isRedisUserOnline(msg.receiverId, &redisOnline);
            if (redisPresenceKnown && redisOnline) {
                if (canPublishRedisMessageEvent(msg, QStringLiteral("remote"))) {
                    deliveryState = "remote";
                } else if (shouldPublishLargeFileOffer(msg)) {
                    deliveryState = "offline";
                    sendToUser(msg);
                } else {
                    sendSystemNotice(socket, QStringLiteral("文件发送失败：Redis 文件路由负载不可用，请调整文件后重试。"));
                    return;
                }
            } else {
                deliveryState = "offline";
                sendToUser(msg);
            }
        }
    } else {
        broadcastMessage(msg, socket);
        QJsonObject details;
        details["fileName"] = msg.fileName;
        details["fileSize"] = QString::number(msg.fileSize);
        details["transferId"] = msg.transferId;
        details["filePolicy"] = serverGroupFilePolicy(QStringLiteral("public"));
        recordServerGroupAuditEvent(QStringLiteral("public"),
                                    QStringLiteral("file_sent"),
                                    msg.senderId,
                                    msg.senderName,
                                    msg.senderId,
                                    msg.senderName,
                                    details);
    }
    saveMessageToSqlite(msg, deliveryState);
    const bool redisPublished = publishRedisMessageEvent(msg, deliveryState);
    if (deliveryState == "remote" && !redisPublished) {
        sendSystemNotice(socket, QStringLiteral("文件发送失败：Redis 路由不可用，请等待服务恢复。"));
        return;
    }
}

void Server::handleFileChunk(const QJsonObject& obj, QTcpSocket* socket) {
    if (!socket) return;

    const QString transferId = obj["transferId"].toString().trimmed();
    const QString fileName = obj["fileName"].toString();
    const qint64 fileSize = obj["fileSize"].toVariant().toLongLong();
    const qint64 chunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 chunkCount = obj["chunkCount"].toVariant().toLongLong();
    const qint64 chunkIndex = obj["chunkIndex"].toVariant().toLongLong();
    const QByteArray chunkData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());
    const QString serverGroupId = obj["groupId"].toString().trimmed();
    const ChatUser* sender = findUserBySocket(socket);
    const QString declaredSenderId = obj["senderId"].toString().trimmed();
    const QString senderId = sender ? sender->id : QString();
    const QString key = pendingFileTransferKey(senderId, transferId);

    auto rejectTransfer = [this, socket, key, fileName, transferId, chunkIndex, serverGroupId, senderId](const QString& reason) {
        if (!key.isEmpty()) {
            m_pendingFileTransfers.remove(key);
        }
        if (!serverGroupId.isEmpty() && !senderId.isEmpty()) {
            QJsonObject details;
            details["fileName"] = fileName;
            details["reason"] = reason;
            details["filePolicy"] = serverGroupFilePolicy(serverGroupTypeFromId(serverGroupId));
            recordServerGroupAuditEvent(serverGroupId,
                                        QStringLiteral("file_rejected"),
                                        senderId,
                                        QString(),
                                        senderId,
                                        QString(),
                                        details);
            const QStringList memberIds = serverGroupMemberIds(serverGroupId);
            for (const QString& memberId : memberIds) {
                QTcpSocket* memberSocket = m_userSockets.value(memberId);
                if (memberSocket && memberSocket->state() == QAbstractSocket::ConnectedState) {
                    sendServerGroupSnapshot(memberId, memberSocket);
                }
            }
        }
        const QString visibleName = fileName.isEmpty() ? "未命名文件" : fileName;
        sendFileChunkAck(socket, transferId, chunkIndex, false, reason);
        sendSystemNotice(socket, QString("文件分片上传已被服务端拒绝：%1，%2。请重新发送。").arg(visibleName, reason));
        qWarning() << "Rejected file chunk transfer" << visibleName << reason;
    };

    if (transferId.isEmpty()) {
        rejectTransfer("缺少传输编号");
        return;
    }
    if (key.isEmpty()) {
        rejectTransfer("发送者身份非法");
        return;
    }
    if (!declaredSenderId.isEmpty() && declaredSenderId != senderId) {
        rejectTransfer("发送者身份不一致");
        return;
    }
    const QString routedGroupId = !serverGroupId.isEmpty()
        ? serverGroupId
        : (obj["receiverId"].toString().trimmed().isEmpty() ? QStringLiteral("public") : QString());
    if (!routedGroupId.isEmpty() && !isServerGroupMember(routedGroupId, senderId)) {
        rejectTransfer("已不在该群组");
        return;
    }
    if (fileSize <= 0 || fileSize > kMaxIncomingPayloadBytes) {
        rejectTransfer(QString("文件大小非法或超过 %1 MB").arg(kMaxIncomingPayloadBytes / 1024 / 1024));
        return;
    }
    if (chunkSize <= 0 || chunkCount <= 0 || chunkIndex < 0 || chunkIndex >= chunkCount) {
        rejectTransfer("分片序号或数量非法");
        return;
    }
    if (chunkCount > kMaxIncomingChunks) {
        rejectTransfer(QString("分片数量超过服务器限制 %1 片").arg(kMaxIncomingChunks));
        return;
    }
    const qint64 expectedChunkCount = (fileSize + chunkSize - 1) / chunkSize;
    if (expectedChunkCount != chunkCount) {
        rejectTransfer(QString("分片数量不一致：声明 %1 片，预期 %2 片").arg(chunkCount).arg(expectedChunkCount));
        return;
    }
    if (chunkData.isEmpty() || chunkData.size() > chunkSize) {
        rejectTransfer("分片内容为空或超过声明大小");
        return;
    }
    if (chunkIndex < chunkCount - 1 && chunkData.size() != chunkSize) {
        rejectTransfer("非末尾分片大小不一致");
        return;
    }

    PendingFileTransfer& pending = m_pendingFileTransfers[key];
    if (pending.chunks.isEmpty()) {
        pending.envelope = obj;
        pending.envelope["type"] = "file";
        pending.envelope["senderId"] = senderId;
        pending.envelope.remove("transferId");
        pending.envelope.remove("chunkIndex");
        pending.envelope.remove("fileData");
        pending.socket = socket;
        pending.fileName = fileName;
        pending.fileSize = fileSize;
        pending.chunkSize = chunkSize;
        pending.chunkCount = chunkCount;
        pending.chunks.resize(static_cast<int>(chunkCount));
    } else if (pending.fileSize != fileSize
               || pending.chunkSize != chunkSize
               || pending.chunkCount != chunkCount
               || pending.envelope["fileHash"].toString().trimmed() != obj["fileHash"].toString().trimmed()) {
        rejectTransfer("同一传输编号的元数据不一致");
        return;
    }
    pending.socket = socket;
    pending.lastActivityMs = QDateTime::currentMSecsSinceEpoch();

    const int index = static_cast<int>(chunkIndex);
    if (!pending.receivedIndexes.contains(index)) {
        pending.chunks[index] = chunkData;
        pending.receivedIndexes.insert(index);
        pending.receivedBytes += chunkData.size();
    }
    if (pending.receivedBytes > fileSize) {
        rejectTransfer("累计分片大小超过声明文件大小");
        return;
    }
    sendFileChunkAck(socket, transferId, chunkIndex, true, QString(), pending.receivedBytes);
    if (pending.receivedIndexes.size() < pending.chunkCount) {
        return;
    }

    QByteArray fileData;
    fileData.reserve(static_cast<int>(fileSize));
    for (const QByteArray& chunk : pending.chunks) {
        if (chunk.isEmpty()) {
            rejectTransfer("存在缺失分片");
            return;
        }
        fileData.append(chunk);
    }

    QJsonObject fullFile = pending.envelope;
    m_pendingFileTransfers.remove(key);
    fullFile["transferId"] = transferId;
    fullFile["fileData"] = QString::fromLatin1(fileData.toBase64());
    handleFile(fullFile, socket);
}

void Server::handleFileTransferResumeQuery(const QJsonObject& obj, QTcpSocket* socket) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

    const QString transferId = obj["transferId"].toString().trimmed();
    QJsonObject response;
    response["type"] = "file_transfer_resume_state";
    response["transferId"] = transferId;
    response["canResume"] = false;
    response["confirmedBytes"] = QString::number(0);
    response["nextChunkIndex"] = QString::number(0);
    response["receivedChunks"] = QJsonArray();

    if (transferId.isEmpty()) {
        response["reason"] = "缺少传输编号";
    } else {
        const ChatUser* sender = findUserBySocket(socket);
        const QString key = pendingFileTransferKey(sender ? sender->id : QString(), transferId);
        const auto it = m_pendingFileTransfers.constFind(key);
        if (key.isEmpty()) {
            response["reason"] = "发送者身份非法";
        } else if (it == m_pendingFileTransfers.constEnd()) {
            response["reason"] = "未找到未完成传输";
        } else {
            const PendingFileTransfer& pending = it.value();
            QJsonArray receivedChunks;
            QVector<int> sortedIndexes = pending.receivedIndexes.values().toVector();
            std::sort(sortedIndexes.begin(), sortedIndexes.end());
            for (int index : sortedIndexes) {
                receivedChunks.append(QString::number(index));
            }

            qint64 nextChunkIndex = 0;
            while (nextChunkIndex < pending.chunkCount
                   && pending.receivedIndexes.contains(static_cast<int>(nextChunkIndex))) {
                ++nextChunkIndex;
            }

            response["canResume"] = true;
            response["reason"] = "";
            response["fileName"] = pending.fileName;
            response["fileSize"] = QString::number(pending.fileSize);
            response["chunkSize"] = QString::number(pending.chunkSize);
            response["chunkCount"] = QString::number(pending.chunkCount);
            response["fileHash"] = pending.envelope["fileHash"].toString();
            response["confirmedBytes"] = QString::number(pending.receivedBytes);
            response["nextChunkIndex"] = QString::number(nextChunkIndex);
            response["receivedChunks"] = receivedChunks;
        }
    }

    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

void Server::handleFileTransferCancel(const QJsonObject& obj, QTcpSocket* socket) {
    if (!socket) return;

    const QString transferId = obj["transferId"].toString().trimmed();
    if (transferId.isEmpty()) return;

    const ChatUser* sender = findUserBySocket(socket);
    const QString key = pendingFileTransferKey(sender ? sender->id : QString(), transferId);
    const bool removed = m_pendingFileTransfers.remove(key) > 0;
    const QString fileName = obj["fileName"].toString();
    const QString visibleName = fileName.isEmpty() ? "未命名文件" : fileName;

    if (removed) {
        sendSystemNotice(socket, QString("文件发送已取消，服务端已清理未完成分片：%1。").arg(visibleName));
        qDebug() << "Canceled pending file transfer" << visibleName << transferId;
    } else {
        qDebug() << "File transfer cancel received after cleanup or completion" << visibleName << transferId;
    }
}

void Server::refreshRedisPresence(const ChatUser& user) {
    if (!m_redisService->isEnabled()) return;
    if (!m_redisService->setPresence(user.id, user.name)) return;
    publishRedisPresenceEvent(user.id, QStringLiteral("online"));
}

void Server::clearRedisPresence(const QString& userId) {
    if (!m_redisService->isEnabled()) return;
    if (!m_redisService->clearPresence(userId)) return;
    publishRedisPresenceEvent(userId, QStringLiteral("offline"));
}

bool Server::publishRedisPresenceEvent(const QString& userId, const QString& action) const {
    if (!m_redisService->isEnabled() || userId.trimmed().isEmpty()) {
        return false;
    }

    QJsonObject event;
    event["eventType"] = QStringLiteral("presence_update");
    event["instanceId"] = m_instanceId;
    event["userId"] = userId.trimmed();
    event["action"] = action.trimmed().isEmpty() ? QStringLiteral("online") : action.trimmed();
    event["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return m_redisService->publish(QStringLiteral("messages"),
                                   QJsonDocument(event).toJson(QJsonDocument::Compact));
}

bool Server::publishRedisServerGroupSnapshotRefresh(const QStringList& userIds, const QString& groupId, const QString& notice) const {
    if (!m_redisService->isEnabled() || groupId.trimmed().isEmpty()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    QJsonArray userIdArray;
    QStringList normalizedUserIds;
    for (const QString& userId : userIds) {
        const QString normalizedUserId = userId.trimmed();
        if (normalizedUserId.isEmpty() || normalizedUserIds.contains(normalizedUserId)) {
            continue;
        }
        normalizedUserIds << normalizedUserId;
        userIdArray.append(normalizedUserId);
    }
    if (userIdArray.isEmpty()) return false;

    QJsonObject event;
    event["eventType"] = QStringLiteral("server_group_snapshot_refresh");
    event["instanceId"] = m_instanceId;
    event["groupId"] = groupId.trimmed();
    event["userIds"] = userIdArray;
    event["notice"] = notice.left(200);
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Skip Redis server group snapshot refresh because encoded payload is too large:"
                   << eventPayload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        return false;
    }
    return m_redisService->publish(QStringLiteral("messages"), eventPayload);
}

void Server::refreshConnectedClientViews() {
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        QTcpSocket* clientSocket = it.key();
        if (!clientSocket || clientSocket->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        sendUserList(clientSocket);
        sendServerGroupSnapshot(it.value().id, clientSocket);
    }
}

bool Server::isRedisUserOnline(const QString& userId, bool* online) const {
    if (online) *online = false;
    if (!m_redisService->isEnabled() || userId.isEmpty()) return false;
    return m_redisService->queryPresence(userId, online);
}

bool Server::canPublishRedisMessageEvent(const Message& msg, const QString& deliveryState) const {
    if (!m_redisService->isEnabled()) return false;

    const bool isRedisFilePayload =
        (msg.type == MessageType::File || msg.type == MessageType::Image)
        && !msg.fileData.isEmpty()
        && msg.fileData.size() <= kRedisPubSubFileMaxBytes;
    if (msg.type != MessageType::Text && msg.type != MessageType::Private && !isRedisFilePayload) {
        return false;
    }

    const QJsonDocument messageDoc = QJsonDocument::fromJson(msg.toJson());
    if (!messageDoc.isObject()) return false;

    QJsonObject event;
    event["eventType"] = "chat_message";
    event["instanceId"] = m_instanceId;
    event["deliveryState"] = deliveryState;
    event["isPrivate"] = msg.isPrivate();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    event["message"] = messageDoc.object();

    return QJsonDocument(event).toJson(QJsonDocument::Compact).size() <= kRedisPubSubEventMaxBytes;
}

bool Server::publishRedisMessageEvent(const Message& msg, const QString& deliveryState) {
    if (!m_redisService->isEnabled()) return false;
    tryRecoverRedisCommandAvailability();
    if (!canPublishRedisMessageEvent(msg, deliveryState)) return false;

    const QJsonDocument messageDoc = QJsonDocument::fromJson(msg.toJson());
    if (!messageDoc.isObject()) return false;

    QJsonObject event;
    event["eventType"] = "chat_message";
    event["instanceId"] = m_instanceId;
    event["deliveryState"] = deliveryState;
    event["isPrivate"] = msg.isPrivate();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    event["message"] = messageDoc.object();

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Skip Redis Pub/Sub message event because encoded payload is too large:"
                   << eventPayload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        return false;
    }
    return m_redisService->publish("messages", eventPayload);
}

bool Server::publishRedisE2EControlEvent(const QJsonObject& forwarded) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();
    const QString receiverId = forwarded.value("receiverId").toString().trimmed();
    if (receiverId.isEmpty()) return false;

    QJsonObject event;
    event["eventType"] = "e2e_control";
    event["instanceId"] = m_instanceId;
    event["receiverId"] = receiverId;
    event["senderId"] = forwarded.value("senderId").toString();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    event["message"] = forwarded;

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Skip Redis E2E control event because encoded payload is too large:"
                   << eventPayload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        return false;
    }
    return m_redisService->publish("messages", eventPayload);
}

bool Server::publishRedisLargeFileOffer(const QJsonObject& offlinePayload) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    const QString objectKey = offlinePayload["objectStoreKey"].toString().trimmed();
    if (!FilesystemObjectStore::isValidObjectKey(objectKey)) return false;

    const qint64 fileSize = offlinePayload["fileSize"].toVariant().toLongLong();
    const qint64 chunkSize = offlinePayload["chunkSize"].toVariant().toLongLong();
    const qint64 chunkCount = offlinePayload["chunkCount"].toVariant().toLongLong();
    const QString fileHash = offlinePayload["fileHash"].toString().trimmed();
    if (fileSize <= 0 || chunkSize <= 0 || chunkCount <= 0 || !looksLikeSha256Hex(fileHash)) {
        return false;
    }

    const int messageType = offlinePayload["messageType"].toInt(static_cast<int>(MessageType::File));
    if (messageType != static_cast<int>(MessageType::File)
        && messageType != static_cast<int>(MessageType::Image)) {
        return false;
    }

    const QString storedTransferId = offlinePayload["transferId"].toString().trimmed();
    QJsonObject event;
    event["eventType"] = "large_file_offer";
    event["instanceId"] = m_instanceId;
    event["transferId"] = storedTransferId.isEmpty() ? objectKey : storedTransferId;
    event["objectKey"] = objectKey;
    event["senderId"] = offlinePayload["senderId"].toString();
    event["senderName"] = offlinePayload["senderName"].toString();
    event["receiverId"] = offlinePayload["receiverId"].toString();
    event["messageType"] = messageType == static_cast<int>(MessageType::Image) ? "Image" : "File";
    event["storeType"] = objectStoreType();
    event["fileName"] = offlinePayload["fileName"].toString();
    event["fileSize"] = QString::number(fileSize);
    event["fileHash"] = fileHash;
    event["chunkSize"] = QString::number(chunkSize);
    event["chunkCount"] = QString::number(chunkCount);
    event["expiresAt"] = offlinePayload["objectStoreExpiresAt"].toString();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    appendE2EFileFields(&event, offlinePayload);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Skip large file offer because encoded payload is too large:"
                   << eventPayload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        logRedisLargeFileRouteEvent(QStringLiteral("offer"),
                                    QStringLiteral("skipped"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                    QStringLiteral("payload-too-large"),
                                    fileSize);
        return false;
    }
    const bool published = m_redisService->publish("messages", eventPayload);
    logRedisLargeFileRouteEvent(QStringLiteral("offer"),
                                published ? QStringLiteral("published") : QStringLiteral("publish-failed"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                published ? QString() : m_redisService->lastError(),
                                fileSize);
    return published;
}

bool Server::publishRedisLargeFileClaim(const QJsonObject& offer) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    QJsonObject event;
    event["eventType"] = "large_file_claim";
    event["instanceId"] = m_instanceId;
    event["sourceInstanceId"] = offer["instanceId"].toString();
    event["transferId"] = offer["transferId"].toString();
    event["objectKey"] = offer["objectKey"].toString();
    event["receiverId"] = offer["receiverId"].toString();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        logRedisLargeFileRouteEvent(QStringLiteral("claim"),
                                    QStringLiteral("skipped"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                    QStringLiteral("payload-too-large"));
        return false;
    }
    const bool published = m_redisService->publish("messages", eventPayload);
    logRedisLargeFileRouteEvent(QStringLiteral("claim"),
                                published ? QStringLiteral("published") : QStringLiteral("publish-failed"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                published ? QString() : m_redisService->lastError());
    return published;
}

bool Server::publishRedisLargeFileDelivered(const QJsonObject& offer, qint64 confirmedBytes) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    QJsonObject event;
    event["eventType"] = "large_file_delivered";
    event["instanceId"] = m_instanceId;
    event["sourceInstanceId"] = offer["instanceId"].toString();
    event["transferId"] = offer["transferId"].toString();
    event["objectKey"] = offer["objectKey"].toString();
    event["receiverId"] = offer["receiverId"].toString();
    event["confirmedBytes"] = QString::number(confirmedBytes);
    event["fileHash"] = offer["fileHash"].toString();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    appendE2EFileFields(&event, offer);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        logRedisLargeFileRouteEvent(QStringLiteral("delivered"),
                                    QStringLiteral("skipped"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                    QStringLiteral("payload-too-large"),
                                    confirmedBytes);
        return false;
    }
    const bool published = m_redisService->publish("messages", eventPayload);
    logRedisLargeFileRouteEvent(QStringLiteral("delivered"),
                                published ? QStringLiteral("published") : QStringLiteral("publish-failed"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                published ? QString() : m_redisService->lastError(),
                                confirmedBytes);
    return published;
}

bool Server::publishRedisLargeFileFailed(const QJsonObject& offer, const QString& reason) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    QJsonObject event;
    event["eventType"] = "large_file_failed";
    event["instanceId"] = m_instanceId;
    event["sourceInstanceId"] = offer["instanceId"].toString();
    event["transferId"] = offer["transferId"].toString();
    event["objectKey"] = offer["objectKey"].toString();
    event["receiverId"] = offer["receiverId"].toString();
    event["fileHash"] = offer["fileHash"].toString();
    event["reason"] = reason.left(160);
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    appendE2EFileFields(&event, offer);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        logRedisLargeFileRouteEvent(QStringLiteral("failed"),
                                    QStringLiteral("skipped"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                    QStringLiteral("payload-too-large"));
        return false;
    }
    const bool published = m_redisService->publish("messages", eventPayload);
    logRedisLargeFileRouteEvent(QStringLiteral("failed"),
                                published ? QStringLiteral("published") : QStringLiteral("publish-failed"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                published ? reason : m_redisService->lastError());
    return published;
}

void Server::handleRedisMessageEvent(const QByteArray& payload) {
    if (payload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Ignore Redis Pub/Sub message event because encoded payload is too large:"
                   << payload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) return;

    const QJsonObject event = doc.object();
    const QString eventType = event["eventType"].toString();
    if (eventType == "server_group_snapshot_refresh") {
        handleRedisServerGroupSnapshotRefresh(event);
        return;
    }
    if (eventType == "e2e_control") {
        handleRedisE2EControlEvent(event);
        return;
    }
    if (eventType == "large_file_offer") {
        handleRedisLargeFileOffer(event);
        return;
    }
    if (eventType == "large_file_delivered") {
        handleRedisLargeFileDelivered(event);
        return;
    }
    if (eventType == "large_file_failed") {
        handleRedisLargeFileFailed(event);
        return;
    }
    if (eventType == "presence_update") {
        if (event["instanceId"].toString() == m_instanceId) return;
        refreshConnectedClientViews();
        return;
    }
    if (eventType != "chat_message") return;
    if (event["instanceId"].toString() == m_instanceId) return;

    const QJsonObject messageObj = event["message"].toObject();
    if (messageObj.isEmpty()) return;

    const Message msg = Message::fromJson(QJsonDocument(messageObj).toJson(QJsonDocument::Compact));
    if (msg.senderId.isEmpty()) return;
    const bool isRedisFilePayload =
        (msg.type == MessageType::File || msg.type == MessageType::Image)
        && !msg.fileData.isEmpty()
        && msg.fileData.size() <= kRedisPubSubFileMaxBytes;
    if (msg.type != MessageType::Text && msg.type != MessageType::Private && !isRedisFilePayload) return;

    if (msg.receiverId.isEmpty()) {
        broadcastMessage(msg);
    } else {
        QTcpSocket* targetSocket = m_userSockets.value(msg.receiverId);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            sendToUser(msg);
        } else {
            return;
        }
    }

    emit newMessage(msg);
}

void Server::handleRedisServerGroupSnapshotRefresh(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;

    const QString groupId = event.value("groupId").toString().trimmed();
    if (groupId.isEmpty()) return;

    const QString notice = event.value("notice").toString().trimmed();
    QStringList userIds;
    const QJsonArray userIdArray = event.value("userIds").toArray();
    for (const QJsonValue& userIdValue : userIdArray) {
        const QString userId = userIdValue.toString().trimmed();
        if (!userId.isEmpty() && !userIds.contains(userId)) {
            userIds << userId;
        }
    }

    for (const QString& userId : userIds) {
        QTcpSocket* targetSocket = m_userSockets.value(userId);
        if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) continue;
        if (!isServerGroupMember(groupId, userId)) continue;
        if (!notice.isEmpty()) {
            sendSystemNotice(targetSocket, notice);
        }
        sendServerGroupSnapshot(userId, targetSocket);
    }
}

void Server::handleRedisE2EControlEvent(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;
    const QString receiverId = event.value("receiverId").toString().trimmed();
    if (receiverId.isEmpty()) return;

    QTcpSocket* targetSocket = m_userSockets.value(receiverId);
    if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const QJsonObject message = event.value("message").toObject();
    const QString type = message.value("type").toString();
    if ((type != QLatin1String("e2e_identity_announce")
         && type != QLatin1String("e2e_key_rotation_request")
         && type != QLatin1String("e2e_key_rotation_response"))
        || message.value("receiverId").toString().trimmed() != receiverId
        || message.value("senderId").toString().trimmed().isEmpty()) {
        return;
    }

    targetSocket->write(QJsonDocument(message).toJson(QJsonDocument::Compact));
    targetSocket->write("\n");
    targetSocket->flush();
}

void Server::handleRedisLargeFileOffer(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return;

    const QString receiverId = event["receiverId"].toString().trimmed();
    if (receiverId.isEmpty()) return;

    QTcpSocket* targetSocket = m_userSockets.value(receiverId);
    if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    if (deliverRedisLargeFileOffer(event, targetSocket)) {
        Message msg;
        msg.senderId = event["senderId"].toString();
        msg.senderName = event["senderName"].toString();
        msg.receiverId = receiverId;
        msg.fileName = event["fileName"].toString();
        msg.transferId = event["transferId"].toString();
        msg.fileSize = event["fileSize"].toVariant().toLongLong();
        msg.fileHash = event["fileHash"].toString();
        msg.chunkSize = event["chunkSize"].toVariant().toLongLong();
        msg.chunkCount = event["chunkCount"].toVariant().toLongLong();
        msg.type = event["messageType"].toString() == "Image" ? MessageType::Image : MessageType::File;
        msg.content = QString(msg.type == MessageType::Image ? "发送了图片: %1" : "发送了文件: %1").arg(msg.fileName);
        msg.e2eFileEncrypted = event["e2eFileEncrypted"].toBool(false);
        msg.e2eFileKeyId = event["e2eFileKeyId"].toString();
        msg.e2eFileKeyFingerprint = event["e2eFileKeyFingerprintSha256"].toString();
        msg.e2eFilePlainSize = event["e2eFilePlainSize"].toVariant().toLongLong();
        msg.e2eFilePlainHash = event["e2eFilePlainHash"].toString();
        if (event.value("e2eEnvelope").isObject()) {
            const QJsonObject envelopeObject = event.value("e2eEnvelope").toObject();
            const E2EEnvelope envelope = E2EEnvelope::fromJson(envelopeObject);
            if (envelope.isValid()) {
                msg.e2eEnvelope = envelope;
            } else if (e2eEnvelopeHeaderLooksSafe(envelopeObject)) {
                msg.e2eEnvelopeHeader = envelopeObject;
            }
        }
        msg.timestamp = QDateTime::currentDateTime();
        emit newMessage(msg);
    }
}

void Server::handleRedisLargeFileDelivered(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;
    if (event["sourceInstanceId"].toString() != m_instanceId) return;
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return;
    const LargeFileDeliveredReceiptDecision reconcileDecision =
        evaluateRedisLargeFileDeliveredReceipt(event);
    logRedisLargeFileRouteEvent(QStringLiteral("delivered_reconcile"),
                                reconcileDecision.shouldCleanup ? QStringLiteral("cleaned") : QStringLiteral("retained"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("reconcile")),
                                reconcileDecision.reason,
                                event["confirmedBytes"].toVariant().toLongLong());
    const LargeFileCleanupResult cleanupResult = cleanupDeliveredRedisLargeFile(event);
    persistRedisLargeFileDeliveredReceiptSummary(event, reconcileDecision, cleanupResult.queueCleaned);
    logRedisLargeFileRouteEvent(QStringLiteral("delivered_cleanup"),
                                cleanupResult.queueCleaned ? QStringLiteral("cleaned") : QStringLiteral("retained"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("delete")),
                                cleanupResult.queueCleaned ? QString() : QStringLiteral("offline-fallback-not-matched"),
                                event["confirmedBytes"].toVariant().toLongLong());
    if (cleanupResult.objectDeleteAttempted) {
        logRedisLargeFileRouteEvent(QStringLiteral("object_delete"),
                                    cleanupResult.objectDeleted ? QStringLiteral("deleted") : QStringLiteral("retained"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("delete")),
                                    cleanupResult.objectDeleteReason,
                                    event["confirmedBytes"].toVariant().toLongLong());
    }
}

void Server::handleRedisLargeFileFailed(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;
    if (event["sourceInstanceId"].toString() != m_instanceId) return;
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return;

    const QString objectKey = event["objectKey"].toString().trimmed();
    const QString transferId = event["transferId"].toString().trimmed();
    const QString receiverId = event["receiverId"].toString().trimmed();
    const QString fileHash = event["fileHash"].toString().trimmed();
    if (!FilesystemObjectStore::isValidObjectKey(objectKey)
        || transferId.isEmpty()
        || receiverId.isEmpty()
        || !looksLikeSha256Hex(fileHash)) {
        return;
    }

    qWarning() << "Large file object routing failed on remote instance; origin offline fallback remains"
               << receiverId << transferId << objectKey << event["reason"].toString();
    logRedisLargeFileRouteEvent(QStringLiteral("failed_received"),
                                QStringLiteral("fallback-retained"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("fallback")),
                                event["reason"].toString());
}

bool Server::deliverRedisLargeFileOffer(const QJsonObject& event, QTcpSocket* socket) {
    QPointer<QTcpSocket> socketGuard(socket);
    auto logDeliveryFailure = [this, &event](const QString& reason, qint64 bytes = 0) {
        logRedisLargeFileRouteEvent(QStringLiteral("offer_delivery"),
                                    QStringLiteral("failed"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("deliver")),
                                    reason,
                                    bytes);
    };
    auto failOffer = [this, &event](const QString& reason) {
        publishRedisLargeFileFailed(event, reason);
        return false;
    };

    if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState) {
        logDeliveryFailure(QStringLiteral("receiver-disconnected"));
        return failOffer(QStringLiteral("receiver-disconnected"));
    }

    if (!isSupportedObjectStoreType(objectStoreType())) {
        return false;
    }
    const QString localStoreType = objectStoreType();
    const QString offerStoreType = normalizeObjectStoreType(event["storeType"].toString());
    if (!isSupportedObjectStoreType(offerStoreType)) {
        logRedisLargeFileRouteEvent(QStringLiteral("offer_validation"),
                                    QStringLiteral("rejected"),
                                    largeFileRouteLogMetadata(event, localStoreType, QStringLiteral("validate")),
                                    QStringLiteral("unsupported-offer-store-type"));
        return failOffer(QStringLiteral("unsupported-offer-store-type"));
    }
    if (offerStoreType != localStoreType) {
        logRedisLargeFileRouteEvent(QStringLiteral("offer_validation"),
                                    QStringLiteral("rejected"),
                                    largeFileRouteLogMetadata(event, localStoreType, QStringLiteral("validate")),
                                    QStringLiteral("object-store-type-mismatch"));
        return failOffer(QStringLiteral("object-store-type-mismatch"));
    }

    QString objectStoreError;
    std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore(&objectStoreError);
    if (!objectStore) {
        if (!objectStoreError.isEmpty()) {
            qWarning() << "Skip large file offer because object store is unavailable" << objectStoreError;
        }
        return failOffer(QStringLiteral("object-store-unavailable: ") + objectStoreError);
    }

    const QString objectKey = event["objectKey"].toString().trimmed();
    const qint64 fileSize = event["fileSize"].toVariant().toLongLong();
    const qint64 chunkSize = event["chunkSize"].toVariant().toLongLong();
    const qint64 chunkCount = event["chunkCount"].toVariant().toLongLong();
    const QString fileHash = event["fileHash"].toString().trimmed();
    const QString messageType = event["messageType"].toString();
    if (!FilesystemObjectStore::isValidObjectKey(objectKey)
        || fileSize <= 0
        || chunkSize <= 0
        || chunkSize > kForwardChunkBytes
        || chunkCount <= 0
        || chunkCount != (fileSize + chunkSize - 1) / chunkSize
        || !looksLikeSha256Hex(fileHash)
        || event["senderId"].toString().trimmed().isEmpty()
        || event["receiverId"].toString().trimmed().isEmpty()
        || (messageType != "File" && messageType != "Image")) {
        return failOffer(QStringLiteral("invalid-offer-metadata"));
    }

    const ObjectStore::ValidationResult validation =
        objectStore->validateObject(objectKey, fileSize, fileHash);
    if (!validation.ok) {
        const QString reason = s3ValidationFailureReasonForLog(validation);
        qWarning() << "Rejected large file offer because object validation failed"
                   << objectKey << reason;
        logRedisLargeFileRouteEvent(QStringLiteral("offer_validation"),
                                    QStringLiteral("rejected"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("validate")),
                                    reason,
                                    fileSize);
        return failOffer(reason);
    }

    std::unique_ptr<QIODevice> file = objectStore->openObject(objectKey);
    if (!file || !file->isOpen()) {
        QString reason = QStringLiteral("object-open-failed");
        const QString storeReason = objectStore->lastOpenFailureReason();
        if (!storeReason.isEmpty()) {
            reason = objectStoreOpenFailureReasonForLog(objectStoreType(), storeReason);
        }
        qWarning() << "Rejected large file offer because object open failed"
                   << objectKey << reason;
        logRedisLargeFileRouteEvent(QStringLiteral("offer_read"),
                                    QStringLiteral("rejected"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("read")),
                                    reason,
                                    fileSize);
        return failOffer(reason);
    }

    publishRedisLargeFileClaim(event);

    const QString transferId = event["transferId"].toString().trimmed().isEmpty()
        ? objectKey
        : event["transferId"].toString().trimmed();
    const QString fileName = event["fileName"].toString();
    for (qint64 index = 0; index < chunkCount; ++index) {
        if (!file->seek(index * chunkSize)) {
            logDeliveryFailure(QStringLiteral("object-seek-failed"), index * chunkSize);
            return failOffer(QStringLiteral("object-seek-failed"));
        }
        const QByteArray chunk = file->read(chunkSize);
        if (chunk.isEmpty() || (index < chunkCount - 1 && chunk.size() != chunkSize)) {
            logDeliveryFailure(QStringLiteral("object-read-failed"), index * chunkSize);
            return failOffer(QStringLiteral("object-read-failed"));
        }

        QJsonObject chunkObj;
        chunkObj["type"] = "file_chunk";
        chunkObj["transferId"] = transferId;
        chunkObj["messageType"] = messageType == "Image" ? static_cast<int>(MessageType::Image) : static_cast<int>(MessageType::File);
        chunkObj["senderId"] = event["senderId"].toString();
        chunkObj["senderName"] = event["senderName"].toString();
        chunkObj["receiverId"] = event["receiverId"].toString();
        chunkObj["content"] = QString(messageType == "Image" ? "发送了图片: %1" : "发送了文件: %1").arg(fileName);
        chunkObj["fileName"] = fileName;
        chunkObj["fileSize"] = QString::number(fileSize);
        chunkObj["fileHash"] = fileHash;
        chunkObj["chunkSize"] = QString::number(chunkSize);
        chunkObj["chunkCount"] = QString::number(chunkCount);
        chunkObj["chunkIndex"] = QString::number(index);
        chunkObj["fileData"] = QString::fromLatin1(chunk.toBase64());
        appendE2EFileFields(&chunkObj, event);

        QString ackRejectReason;
        qint64 ackReceivedBytes = 0;
        bool acknowledged = false;
        const QByteArray data = QJsonDocument(chunkObj).toJson(QJsonDocument::Compact);
        for (int attempt = 1; attempt <= kChunkSendMaxAttempts; ++attempt) {
            if (sendFileChunkAndWaitForAck(socketGuard, data, transferId, index, &ackRejectReason, &ackReceivedBytes)) {
                const qint64 expectedAckBytes = qMin(fileSize, index * chunkSize + chunk.size());
                if (ackReceivedBytes > 0 && (ackReceivedBytes < expectedAckBytes || ackReceivedBytes > fileSize)) {
                    if (attempt == kChunkSendMaxAttempts) {
                        ackRejectReason = QString::fromUtf8("跨实例大文件确认进度非法");
                    }
                    continue;
                }
                acknowledged = true;
                break;
            }
            if (!socketGuard) {
                logDeliveryFailure(QStringLiteral("receiver-disconnected"), index * chunkSize);
                return failOffer(QStringLiteral("receiver-disconnected"));
            }
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "Large file offer chunk rejected by receiver:" << ackRejectReason;
                logDeliveryFailure(QStringLiteral("chunk-rejected"), qMin(fileSize, index * chunkSize + chunk.size()));
                return failOffer(QStringLiteral("chunk-rejected: ") + ackRejectReason);
            }
        }
        if (!acknowledged) {
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "Large file offer chunk rejected by receiver:" << ackRejectReason;
                logDeliveryFailure(QStringLiteral("chunk-rejected"), qMin(fileSize, index * chunkSize + chunk.size()));
                return failOffer(QStringLiteral("chunk-rejected: ") + ackRejectReason);
            }
            qWarning() << "Large file offer chunk ack timeout:" << fileName << index + 1 << "/" << chunkCount;
            logDeliveryFailure(QStringLiteral("chunk-ack-timeout"), qMin(fileSize, index * chunkSize + chunk.size()));
            return failOffer(QStringLiteral("chunk-ack-timeout"));
        }
    }

    publishRedisLargeFileDelivered(event, fileSize);
    return true;
}

ChatUser* Server::findUserBySocket(QTcpSocket* socket) {
    auto it = m_clients.find(socket);
    if (it != m_clients.end()) {
        return &it.value();
    }
    return nullptr;
}

QString Server::generateAccountId(const QJsonObject& accounts) const {
    for (int i = 0; i < 200; ++i) {
        int length = QRandomGenerator::global()->bounded(1, 10);
        int minValue = 1;
        for (int j = 1; j < length; ++j) {
            minValue *= 10;
        }
        int maxValue = minValue * 10;
        QString account = QString::number(QRandomGenerator::global()->bounded(minValue, maxValue));
        if (!accounts.contains(account)) return account;
    }
    return QString::number(QDateTime::currentMSecsSinceEpoch() % 1000000000).rightJustified(1, '1');
}

QJsonObject Server::loadAccountsFromSqlite() const {
    ensureAccountDatabase();

    QJsonObject accounts;
    QString connectionName = "accounts_read_" + QString::number(reinterpret_cast<quintptr>(this));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            if (query.exec("SELECT account, password_hash, user_name, COALESCE(avatar, '') FROM accounts")) {
                while (query.next()) {
                    QJsonObject accountObj;
                    accountObj["passwordHash"] = query.value(1).toString();
                    accountObj["userName"] = query.value(2).toString();
                    accountObj["avatar"] = query.value(3).toString();
                    accountObj["userId"] = query.value(0).toString();
                    accounts[query.value(0).toString()] = accountObj;
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);

    if (accounts.isEmpty()) {
        QJsonObject legacyAccounts = loadAccounts();
        for (auto it = legacyAccounts.begin(); it != legacyAccounts.end(); ++it) {
            QJsonObject accountObj = it.value().toObject();
            QString passwordHash = accountObj["passwordHash"].toString();
            if (passwordHash.isEmpty() && accountObj.contains("password")) {
                passwordHash = legacyPasswordHash(it.key(), accountObj["password"].toString());
            }
            QString userName = accountObj["userName"].toString(it.key());
            if (!passwordHash.isEmpty()
                && insertAccountToSqlite(it.key(),
                                         passwordHash,
                                         userName,
                                         accountObj["avatar"].toString())) {
                QJsonObject migratedObj;
                migratedObj["passwordHash"] = passwordHash;
                migratedObj["userName"] = userName;
                migratedObj["avatar"] = accountObj["avatar"].toString();
                migratedObj["userId"] = it.key();
                accounts[it.key()] = migratedObj;
            }
        }
    }

    return accounts;
}

bool Server::insertAccountToSqlite(const QString& account,
                                   const QString& passwordHash,
                                   const QString& userName,
                                   const QString& avatarBase64) const {
    if (!ensureAccountDatabase()) return false;

    QString connectionName = "accounts_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare(insertReplaceSql(
                QStringLiteral("accounts"),
                {QStringLiteral("account"),
                 QStringLiteral("password_hash"),
                 QStringLiteral("user_name"),
                 QStringLiteral("avatar"),
                 QStringLiteral("updated_at")},
                {QStringLiteral("?"),
                 QStringLiteral("?"),
                 QStringLiteral("?"),
                 QStringLiteral("?"),
                 QStringLiteral("CURRENT_TIMESTAMP")},
                {QStringLiteral("account")},
                {QStringLiteral("password_hash = EXCLUDED.password_hash"),
                 QStringLiteral("user_name = EXCLUDED.user_name"),
                 QStringLiteral("avatar = EXCLUDED.avatar"),
                 QStringLiteral("updated_at = EXCLUDED.updated_at")}));
            query.addBindValue(account);
            query.addBindValue(passwordHash);
            query.addBindValue(userName);
            query.addBindValue(avatarBase64);
            ok = query.exec();
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::updateAccountPasswordHashInSqlite(const QString& account, const QString& passwordHash) const {
    if (account.isEmpty() || passwordHash.isEmpty() || !ensureAccountDatabase()) return false;

    QString connectionName = "accounts_password_update_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("UPDATE accounts SET password_hash = ?, updated_at = CURRENT_TIMESTAMP WHERE account = ?");
            query.addBindValue(passwordHash);
            query.addBindValue(account);
            ok = query.exec();
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::recordUserSessionToSqlite(const ChatUser& user, const QString& eventName) const {
    if (!ensureAccountDatabase()) return false;

    QString connectionName = "sessions_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO user_sessions(user_id, user_name, event_name, peer_address, peer_port, created_at) "
                          "VALUES(?, ?, ?, ?, ?, CURRENT_TIMESTAMP)");
            query.addBindValue(user.id);
            query.addBindValue(user.name);
            query.addBindValue(eventName);
            query.addBindValue(user.address.toString());
            query.addBindValue(user.port);
            ok = query.exec();
            if (ok && eventName == "login" && !user.id.isEmpty()) {
                QSqlQuery accountQuery(db);
                accountQuery.prepare("UPDATE accounts SET "
                                     "user_name = ?, "
                                     "last_login_at = CURRENT_TIMESTAMP, "
                                     "last_login_address = ?, "
                                     "login_count = COALESCE(login_count, 0) + 1, "
                                     "updated_at = CURRENT_TIMESTAMP "
                                     "WHERE account = ?");
                accountQuery.addBindValue(user.name);
                accountQuery.addBindValue(user.address.toString());
                accountQuery.addBindValue(user.id);
                ok = accountQuery.exec();
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::recordDefaultGroupMembership(const ChatUser& user) const {
    if (user.id.isEmpty() || !ensureAccountDatabase()) return false;

    QString connectionName = "default_group_member_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery groupQuery(db);
            ok = groupQuery.exec(insertIgnoreSql(
                QStringLiteral("server_groups"),
                {QStringLiteral("group_id"), QStringLiteral("group_name"), QStringLiteral("announcement"), QStringLiteral("created_at"), QStringLiteral("updated_at")},
                {QStringLiteral("'public'"), QStringLiteral("'公共聊天室'"), QStringLiteral("'欢迎来到公共聊天室。'"), QStringLiteral("CURRENT_TIMESTAMP"), QStringLiteral("CURRENT_TIMESTAMP")},
                {QStringLiteral("group_id")}));
            if (ok) {
                QSqlQuery removedQuery(db);
                removedQuery.prepare("SELECT COUNT(*) FROM server_group_removed_members WHERE group_id = 'public' AND user_id = ?");
                removedQuery.addBindValue(user.id);
                ok = removedQuery.exec();
                bool wasRemoved = false;
                if (ok && removedQuery.next()) {
                    wasRemoved = removedQuery.value(0).toInt() > 0;
                }
                if (ok && !wasRemoved) {
                    QSqlQuery insertMemberQuery(db);
                    insertMemberQuery.prepare(insertIgnoreSql(
                        QStringLiteral("server_group_members"),
                        {QStringLiteral("group_id"), QStringLiteral("user_id"), QStringLiteral("user_name"), QStringLiteral("role"), QStringLiteral("joined_at"), QStringLiteral("updated_at")},
                        {QStringLiteral("'public'"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("'member'"), QStringLiteral("CURRENT_TIMESTAMP"), QStringLiteral("CURRENT_TIMESTAMP")},
                        {QStringLiteral("group_id"), QStringLiteral("user_id")}));
                    insertMemberQuery.addBindValue(user.id);
                    insertMemberQuery.addBindValue(user.name);
                    ok = insertMemberQuery.exec();
                }
            }
            if (ok) {
                QSqlQuery updateMemberQuery(db);
                updateMemberQuery.prepare("UPDATE server_group_members SET user_name = ?, updated_at = CURRENT_TIMESTAMP "
                                          "WHERE group_id = 'public' AND user_id = ?");
                updateMemberQuery.addBindValue(user.name);
                updateMemberQuery.addBindValue(user.id);
                ok = updateMemberQuery.exec();
            }
            QString ownerId;
            if (ok) {
                QSqlQuery ownerQuery(db);
                ownerQuery.prepare("SELECT COALESCE(owner_id, '') FROM server_groups WHERE group_id = 'public'");
                ok = ownerQuery.exec();
                if (ok && ownerQuery.next()) {
                    ownerId = ownerQuery.value(0).toString().trimmed();
                }
            }
            if (ok && !ownerId.isEmpty()) {
                QSqlQuery ownerMemberQuery(db);
                ownerMemberQuery.prepare("SELECT COUNT(*) FROM server_group_members WHERE group_id = 'public' AND user_id = ?");
                ownerMemberQuery.addBindValue(ownerId);
                ok = ownerMemberQuery.exec();
                if (ok && ownerMemberQuery.next() && ownerMemberQuery.value(0).toInt() == 0) {
                    ownerId.clear();
                }
            }
            if (ok && ownerId.isEmpty()) {
                ownerId = user.id;
                QSqlQuery updateOwnerQuery(db);
                updateOwnerQuery.prepare("UPDATE server_groups SET owner_id = ?, updated_at = CURRENT_TIMESTAMP "
                                         "WHERE group_id = 'public'");
                updateOwnerQuery.addBindValue(ownerId);
                ok = updateOwnerQuery.exec();
            }
            if (ok) {
                QSqlQuery updateRoleQuery(db);
                updateRoleQuery.prepare("UPDATE server_group_members "
                                        "SET role = CASE WHEN user_id = ? THEN 'owner' "
                                        "WHEN role = 'owner' THEN 'member' ELSE role END, "
                                        "updated_at = CURRENT_TIMESTAMP "
                                        "WHERE group_id = 'public'");
                updateRoleQuery.addBindValue(ownerId);
                ok = updateRoleQuery.exec();
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::isServerGroupMember(const QString& groupId, const QString& userId) const {
    if (groupId.isEmpty() || userId.isEmpty() || !ensureAccountDatabase()) return false;

    const QString connectionName = "server_group_membership_check_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(groupId + "|" + userId));
    bool exists = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("SELECT COUNT(*) FROM server_group_members WHERE group_id = ? AND user_id = ?");
            query.addBindValue(groupId);
            query.addBindValue(userId);
            if (query.exec() && query.next()) {
                exists = query.value(0).toInt() > 0;
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return exists;
}

QStringList Server::serverGroupMemberIds(const QString& groupId) const {
    QStringList memberIds;
    if (groupId.trimmed().isEmpty() || !ensureAccountDatabase()) return memberIds;

    const QString connectionName = "server_group_member_ids_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(groupId));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("SELECT user_id FROM server_group_members WHERE group_id = ?");
            query.addBindValue(groupId.trimmed());
            if (query.exec()) {
                while (query.next()) {
                    const QString memberId = query.value(0).toString().trimmed();
                    if (!memberId.isEmpty() && !memberIds.contains(memberId)) {
                        memberIds << memberId;
                    }
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return memberIds;
}

bool Server::recordServerGroupAuditEvent(const QString& groupId,
                                         const QString& action,
                                         const QString& actorId,
                                         const QString& actorName,
                                         const QString& targetUserId,
                                         const QString& targetUserName,
                                         const QJsonObject& details) const {
    if (groupId.trimmed().isEmpty()
        || action.trimmed().isEmpty()
        || actorId.trimmed().isEmpty()
        || !ensureAccountDatabase()) {
        return false;
    }

    QString connectionName = "server_group_audit_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO server_group_audit_events("
                          "group_id, action, actor_id, actor_name, target_user_id, target_user_name, details, created_at) "
                          "VALUES(?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)");
            query.addBindValue(groupId.trimmed());
            query.addBindValue(action.trimmed().toLower());
            query.addBindValue(actorId.trimmed());
            query.addBindValue(actorName.trimmed());
            query.addBindValue(targetUserId.trimmed());
            query.addBindValue(targetUserName.trimmed());
            query.addBindValue(QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact)));
            ok = query.exec();
            if (!ok) {
                qWarning() << "Failed to record server group audit event:"
                           << query.lastError().text()
                           << groupId.trimmed()
                           << action.trimmed().toLower()
                           << actorId.trimmed();
            }
            db.close();
        } else {
            qWarning() << "Failed to open server group audit database:"
                       << db.lastError().text()
                       << groupId.trimmed()
                       << action.trimmed().toLower()
                       << actorId.trimmed();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::saveMessageToSqlite(const Message& msg, const QString& deliveryState) const {
    if (!ensureAccountDatabase()) return false;

    QString connectionName = "messages_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO messages(message_type, sender_id, sender_name, receiver_id, content, file_name, file_size, file_hash, file_chunk_size, file_chunk_count, delivery_state, created_at) "
                          "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
            query.addBindValue(static_cast<int>(msg.type));
            query.addBindValue(msg.senderId);
            query.addBindValue(msg.senderName);
            query.addBindValue(msg.receiverId);
            query.addBindValue(msg.content);
            query.addBindValue(msg.fileName);
            query.addBindValue(msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
            query.addBindValue(msg.fileHash.trimmed());
            query.addBindValue(msg.chunkSize);
            query.addBindValue(msg.chunkCount);
            query.addBindValue(deliveryState);
            query.addBindValue(msg.timestamp.toUTC().toString(Qt::ISODate));
            ok = query.exec();
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::saveFriendEventToSqlite(const QString& eventType,
                                     const QString& senderId,
                                     const QString& senderName,
                                     const QString& receiverId,
                                     const QString& queryAccount,
                                     const QString& eventState,
                                     bool accepted) const {
    if (!ensureAccountDatabase()) return false;

    QString connectionName = "friend_events_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO friend_events(event_type, sender_id, sender_name, receiver_id, query_account, accepted, event_state, created_at) "
                          "VALUES(?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)");
            query.addBindValue(eventType);
            query.addBindValue(senderId);
            query.addBindValue(senderName);
            query.addBindValue(receiverId);
            query.addBindValue(queryAccount);
            query.addBindValue(accepted ? 1 : 0);
            query.addBindValue(eventState);
            ok = query.exec();
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::ensureAccountDatabase() const {
    QString connectionName = "accounts_init_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            const QString idColumn = autoIdColumnSql();
            ok = query.exec("CREATE TABLE IF NOT EXISTS accounts ("
                            "account TEXT PRIMARY KEY, "
                            "password_hash TEXT NOT NULL, "
                            "user_name TEXT NOT NULL, "
                            "avatar TEXT DEFAULT '', "
                            "created_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                            "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            if (ok) {
                query.exec("ALTER TABLE accounts ADD COLUMN avatar TEXT DEFAULT ''");
                query.exec("ALTER TABLE accounts ADD COLUMN last_login_at TEXT");
                query.exec("ALTER TABLE accounts ADD COLUMN last_login_address TEXT");
                query.exec("ALTER TABLE accounts ADD COLUMN login_count INTEGER DEFAULT 0");
            }
            if (ok) {
                ok = query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS user_sessions (")
                                + idColumn
                                + QStringLiteral("user_id TEXT NOT NULL, "
                                                 "user_name TEXT NOT NULL, "
                                                 "event_name TEXT NOT NULL, "
                                                 "peer_address TEXT, "
                                                 "peer_port INTEGER, "
                                                 "created_at TEXT DEFAULT CURRENT_TIMESTAMP)"));
            }
            if (ok) {
                ok = query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS messages (")
                                + idColumn
                                + QStringLiteral("message_type INTEGER NOT NULL, "
                                                 "sender_id TEXT, "
                                                 "sender_name TEXT, "
                                                 "receiver_id TEXT, "
                                                 "content TEXT, "
                                                 "file_name TEXT, "
                                                 "file_size INTEGER DEFAULT 0, "
                                                 "file_hash TEXT, "
                                                 "file_chunk_size INTEGER DEFAULT 0, "
                                                 "file_chunk_count INTEGER DEFAULT 0, "
                                                 "delivery_state TEXT NOT NULL, "
                                                 "created_at TEXT DEFAULT CURRENT_TIMESTAMP)"));
            }
            if (ok) {
                query.exec("ALTER TABLE messages ADD COLUMN file_hash TEXT");
                query.exec("ALTER TABLE messages ADD COLUMN file_chunk_size INTEGER DEFAULT 0");
                query.exec("ALTER TABLE messages ADD COLUMN file_chunk_count INTEGER DEFAULT 0");
            }
            if (ok) {
                ok = query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS offline_messages (")
                                + idColumn
                                + QStringLiteral("receiver_id TEXT NOT NULL, "
                                                 "payload TEXT NOT NULL, "
                                                 "created_at TEXT DEFAULT CURRENT_TIMESTAMP)"));
            }
            if (ok) {
                ok = query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS friend_events (")
                                + idColumn
                                + QStringLiteral("event_type TEXT NOT NULL, "
                                                 "sender_id TEXT, "
                                                 "sender_name TEXT, "
                                                 "receiver_id TEXT, "
                                                 "query_account TEXT, "
                                                 "accepted INTEGER DEFAULT 0, "
                                                 "event_state TEXT NOT NULL, "
                                                 "created_at TEXT DEFAULT CURRENT_TIMESTAMP)"));
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_groups ("
                                "group_id TEXT PRIMARY KEY, "
                                "group_name TEXT NOT NULL, "
                                "owner_id TEXT, "
                                "announcement TEXT, "
                                "group_type TEXT DEFAULT 'public', "
                                "history_policy TEXT DEFAULT 'public-removed-readonly', "
                                "file_policy TEXT DEFAULT 'public-members-only', "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
                query.exec("ALTER TABLE server_groups ADD COLUMN group_type TEXT DEFAULT 'public'");
                query.exec("ALTER TABLE server_groups ADD COLUMN history_policy TEXT DEFAULT 'public-removed-readonly'");
                query.exec("ALTER TABLE server_groups ADD COLUMN file_policy TEXT DEFAULT 'public-members-only'");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_group_members ("
                                "group_id TEXT NOT NULL, "
                                "user_id TEXT NOT NULL, "
                                "user_name TEXT, "
                                "role TEXT NOT NULL DEFAULT 'member', "
                                "joined_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "PRIMARY KEY(group_id, user_id))");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_group_removed_members ("
                                "group_id TEXT NOT NULL, "
                                "user_id TEXT NOT NULL, "
                                "removed_by TEXT, "
                                "removed_by_name TEXT, "
                                "removed_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "PRIMARY KEY(group_id, user_id))");
            }
            if (ok) {
                ok = query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS server_group_announcements (")
                                + idColumn
                                + QStringLiteral("group_id TEXT NOT NULL, "
                                                 "author_id TEXT, "
                                                 "author_name TEXT, "
                                                 "content TEXT NOT NULL, "
                                                 "created_at TEXT DEFAULT CURRENT_TIMESTAMP)"));
            }
            if (ok) {
                ok = query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS server_group_audit_events (")
                                + idColumn
                                + QStringLiteral("group_id TEXT NOT NULL, "
                                                 "action TEXT NOT NULL, "
                                                 "actor_id TEXT NOT NULL, "
                                                 "actor_name TEXT, "
                                                 "target_user_id TEXT, "
                                                 "target_user_name TEXT, "
                                                 "details TEXT, "
                                                 "created_at TEXT DEFAULT CURRENT_TIMESTAMP)"));
            }
            if (ok) {
                query.exec(insertIgnoreSql(
                    QStringLiteral("server_groups"),
                    {QStringLiteral("group_id"), QStringLiteral("group_name"), QStringLiteral("announcement"), QStringLiteral("group_type"), QStringLiteral("history_policy"), QStringLiteral("file_policy"), QStringLiteral("created_at"), QStringLiteral("updated_at")},
                    {QStringLiteral("'public'"), QStringLiteral("'公共聊天室'"), QStringLiteral("'欢迎来到公共聊天室。'"), QStringLiteral("'public'"), QStringLiteral("'public-removed-readonly'"), QStringLiteral("'public-members-only'"), QStringLiteral("CURRENT_TIMESTAMP"), QStringLiteral("CURRENT_TIMESTAMP")},
                    {QStringLiteral("group_id")}));
                query.exec("UPDATE server_groups SET group_type = 'public' WHERE group_id = 'public' AND (group_type IS NULL OR group_type = '')");
                query.exec("UPDATE server_groups SET history_policy = 'public-removed-readonly' WHERE group_id = 'public' AND (history_policy IS NULL OR history_policy = '')");
                query.exec("UPDATE server_groups SET file_policy = 'public-members-only' WHERE group_id = 'public' AND (file_policy IS NULL OR file_policy = '')");
            }
            if (ok) {
                query.exec("CREATE INDEX IF NOT EXISTS idx_messages_created_at ON messages(created_at)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_messages_receiver ON messages(receiver_id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_offline_receiver ON offline_messages(receiver_id, id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_friend_events_sender ON friend_events(sender_id, id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_friend_events_receiver ON friend_events(receiver_id, id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_members_user ON server_group_members(user_id, group_id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_removed_members_user ON server_group_removed_members(user_id, group_id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_announcements_group ON server_group_announcements(group_id, id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_audit_group ON server_group_audit_events(group_id, id)");
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

QString Server::accountDbPath() const {
    return accountDatabasePath();
}

QJsonObject Server::loadAccounts() const {
    QFile file(accountsFilePath());
    if (!file.open(QIODevice::ReadOnly)) return {};

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return {};
    return doc.object();
}

void Server::saveAccounts(const QJsonObject& accounts) const {
    QFile file(accountsFilePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(accounts).toJson(QJsonDocument::Indented));
}

QString Server::accountsFilePath() const {
    QString dir = appDataDir();
    if (dir.isEmpty()) dir = ".";
    QDir().mkpath(dir);
    return dir + "/accounts.json";
}

QString Server::offlineFilePath(const QString& userId) const {
    QString baseDir = appDataDir();
    if (baseDir.isEmpty()) baseDir = ".";
    QString dir = baseDir + "/offline";
    QDir().mkpath(dir);
    return dir + "/" + userId + ".jsonl";
}

QString Server::offlineAttachmentRootDir() const {
    QString baseDir = appDataDir();
    if (baseDir.isEmpty()) baseDir = ".";
    const QString dir = baseDir + "/offline_files";
    QDir().mkpath(dir);
    return dir;
}

QString Server::offlineAttachmentDir(const QString& userId) const {
    const QString dir = offlineAttachmentRootDir() + "/" + safePathPart(userId);
    QDir().mkpath(dir);
    return dir;
}

qint64 Server::offlineAttachmentQuotaBytes() const {
    const qint64 quotaMb = positiveIntegerEnvOrDefault("QTNETWORKCHAT_OFFLINE_ATTACHMENT_QUOTA_MB",
                                                       kDefaultOfflineAttachmentQuotaBytes / (1024 * 1024));
    return quotaMb * 1024 * 1024;
}

qint64 Server::offlineAttachmentUsedBytes() const {
    const QString rootDirPath = offlineAttachmentRootDir();
    qint64 usedBytes = 0;
    QDirIterator it(rootDirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        usedBytes += QFileInfo(it.next()).size();
    }
    return usedBytes;
}

bool Server::hasOfflineAttachmentCapacity(qint64 incomingBytes) const {
    if (incomingBytes <= 0) return false;

    const qint64 quotaBytes = offlineAttachmentQuotaBytes();
    return incomingBytes <= quotaBytes && offlineAttachmentUsedBytes() <= quotaBytes - incomingBytes;
}

bool Server::shouldPublishLargeFileOffer(const Message& msg) const {
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return false;
    if (!m_redisService->isEnabled()) return false;
    if (!isRedisUserOnline(msg.receiverId)) return false;
    if (msg.receiverId.isEmpty() || msg.fileData.isEmpty()) return false;
    if (msg.type != MessageType::File && msg.type != MessageType::Image) return false;
    QTcpSocket* localSocket = m_userSockets.value(msg.receiverId, nullptr);
    if (localSocket && localSocket->state() == QAbstractSocket::ConnectedState) return false;

    if (!createConfiguredObjectStore()) return false;
    if (msg.fileSize <= 0 || msg.chunkSize <= 0 || msg.chunkCount <= 0) return false;
    if (msg.chunkCount != (msg.fileSize + msg.chunkSize - 1) / msg.chunkSize) return false;
    return looksLikeSha256Hex(msg.fileHash);
}

QString Server::objectStoreType() const {
    return normalizeObjectStoreType(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_STORE")));
}

QString Server::objectStoreRootDir() const {
    const QString root = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_ROOT")).trimmed();
    return root.isEmpty() ? QString() : QDir::cleanPath(root);
}

std::unique_ptr<ObjectStore> Server::createConfiguredObjectStore(QString* error) const {
    if (m_objectStoreFactoryForTesting) {
        return m_objectStoreFactoryForTesting(error);
    }
    return createObjectStore(objectStoreType(), objectStoreRootDir(), error);
}

qint64 Server::objectStoreTtlMs() const {
    const qint64 hours = positiveIntegerEnvOrDefault("QTNETWORKCHAT_OBJECT_TTL_HOURS",
                                                     kDefaultObjectStoreTtlHours,
                                                     kMaxObjectStoreTtlHours);
    return hours * 60LL * 60 * 1000;
}

LargeFileDeliveredReceiptDecision Server::evaluateRedisLargeFileDeliveredReceipt(const QJsonObject& event) const {
    LargeFileDeliveredReceipt receipt;
    receipt.sourceInstanceId = event["sourceInstanceId"].toString();
    receipt.transferId = event["transferId"].toString().trimmed();
    receipt.receiverId = event["receiverId"].toString().trimmed();
    receipt.objectKey = event["objectKey"].toString().trimmed();
    receipt.fileHash = event["fileHash"].toString().trimmed();
    receipt.confirmedBytes = event["confirmedBytes"].toVariant().toLongLong();

    LargeFileDeliveredReceiptDecision invalidDecision =
        evaluateLargeFileDeliveredReceiptCleanup(receipt, LargeFileDeliveredFallback());
    if (invalidDecision.reason == QStringLiteral("invalid-receipt")) {
        return invalidDecision;
    }

    LargeFileDeliveredReceiptDecision decision;
    decision.shouldCleanup = false;
    decision.reason = QStringLiteral("receipt-not-matched");

    const auto makeFallback = [this](const QJsonObject& obj) {
        LargeFileDeliveredFallback fallback;
        fallback.sourceInstanceId = m_instanceId;
        fallback.transferId = obj["transferId"].toString();
        fallback.receiverId = obj["receiverId"].toString();
        fallback.objectKey = obj["objectStoreKey"].toString();
        fallback.fileHash = obj["fileHash"].toString();
        fallback.fileSize = obj["fileSize"].toVariant().toLongLong();
        return fallback;
    };
    const auto isSameRoute = [&receipt](const LargeFileDeliveredFallback& fallback) {
        return receipt.transferId == fallback.transferId.trimmed()
            && receipt.receiverId == fallback.receiverId.trimmed()
            && receipt.objectKey == fallback.objectKey.trimmed();
    };
    const auto considerPayload = [&](const QJsonObject& obj) {
        const LargeFileDeliveredFallback fallback = makeFallback(obj);
        if (!isSameRoute(fallback)) {
            return false;
        }
        decision = evaluateLargeFileDeliveredReceiptCleanup(receipt, fallback);
        return decision.shouldCleanup;
    };

    if (ensureAccountDatabase()) {
        const QString connectionName = "offline_reconcile_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = openAccountDatabase(connectionName);
            if (openAccountDatabaseConnection(db, connectionName)) {
                QSqlQuery query(db);
                query.prepare("SELECT payload FROM offline_messages WHERE receiver_id = ? ORDER BY id ASC");
                query.addBindValue(receipt.receiverId);
                if (query.exec()) {
                    while (query.next()) {
                        const QJsonDocument doc = QJsonDocument::fromJson(query.value(0).toString().toUtf8());
                        if (doc.isObject() && considerPayload(doc.object())) {
                            break;
                        }
                    }
                }
                db.close();
            }
        }
        releaseAccountDatabase(connectionName);
    }

    if (decision.shouldCleanup) {
        return decision;
    }

    QFile jsonlFile(offlineFilePath(receipt.receiverId));
    if (jsonlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!jsonlFile.atEnd()) {
            const QByteArray line = jsonlFile.readLine().trimmed();
            if (line.isEmpty()) continue;
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (doc.isObject() && considerPayload(doc.object())) {
                break;
            }
        }
        jsonlFile.close();
    }

    return decision;
}

void Server::persistRedisLargeFileDeliveredReceiptSummary(const QJsonObject& event,
                                                          const LargeFileDeliveredReceiptDecision& decision,
                                                          bool cleanupSucceeded) const {
    const QString configuredDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_DELIVERED_RECEIPT_DIR")).trimmed();
    if (configuredDir.isEmpty()) {
        return;
    }

    const QString sourceInstanceId = event["sourceInstanceId"].toString().trimmed();
    const QString transferId = event["transferId"].toString().trimmed();
    const QString receiverId = event["receiverId"].toString().trimmed();
    const QString objectKey = event["objectKey"].toString().trimmed();
    const QString fileHash = event["fileHash"].toString().trimmed().toLower();
    const qint64 confirmedBytes = event["confirmedBytes"].toVariant().toLongLong();
    LargeFileDeliveredReceipt receipt;
    receipt.sourceInstanceId = sourceInstanceId;
    receipt.transferId = transferId;
    receipt.receiverId = receiverId;
    receipt.objectKey = objectKey;
    receipt.fileHash = fileHash;
    receipt.confirmedBytes = confirmedBytes;
    if (evaluateLargeFileDeliveredReceiptCleanup(receipt, LargeFileDeliveredFallback()).reason
            == QStringLiteral("invalid-receipt")) {
        return;
    }

    QDir dir(QDir::cleanPath(configuredDir));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        qWarning() << "Failed to create delivered receipt summary directory";
        return;
    }

    QJsonObject row;
    row["sourceInstanceId"] = sourceInstanceId;
    row["transferId"] = transferId;
    row["receiverId"] = receiverId;
    row["objectKey"] = objectKey;
    row["fileHash"] = fileHash;
    row["confirmedBytes"] = QString::number(confirmedBytes);
    row["result"] = decision.shouldCleanup ? QStringLiteral("cleaned") : QStringLiteral("retained");
    row["reason"] = decision.reason;
    row["cleanupResult"] = cleanupSucceeded ? QStringLiteral("cleaned") : QStringLiteral("retained");
    row["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QFile file(dir.filePath(QStringLiteral("delivered-receipts.jsonl")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Failed to open delivered receipt summary file";
        return;
    }
    file.write(QJsonDocument(row).toJson(QJsonDocument::Compact));
    file.write("\n");
}

Server::LargeFileCleanupResult Server::cleanupDeliveredRedisLargeFile(const QJsonObject& event) const {
    LargeFileCleanupResult result;
    const QString objectKey = event["objectKey"].toString().trimmed();
    const QString transferId = event["transferId"].toString().trimmed();
    const QString receiverId = event["receiverId"].toString().trimmed();
    const QString fileHash = event["fileHash"].toString().trimmed();
    const qint64 confirmedBytes = event["confirmedBytes"].toVariant().toLongLong();
    LargeFileDeliveredReceipt receipt;
    receipt.sourceInstanceId = event["sourceInstanceId"].toString();
    receipt.transferId = transferId;
    receipt.receiverId = receiverId;
    receipt.objectKey = objectKey;
    receipt.fileHash = fileHash;
    receipt.confirmedBytes = confirmedBytes;
    if (evaluateLargeFileDeliveredReceiptCleanup(receipt, LargeFileDeliveredFallback()).reason
            == QStringLiteral("invalid-receipt")) {
        return result;
    }

    auto matchesPayload = [&](const QJsonObject& obj) {
        LargeFileDeliveredFallback fallback;
        fallback.sourceInstanceId = m_instanceId;
        fallback.transferId = obj["transferId"].toString();
        fallback.receiverId = obj["receiverId"].toString();
        fallback.objectKey = obj["objectStoreKey"].toString();
        fallback.fileHash = obj["fileHash"].toString();
        fallback.fileSize = obj["fileSize"].toVariant().toLongLong();
        return evaluateLargeFileDeliveredReceiptCleanup(receipt, fallback).shouldCleanup;
    };

    QStringList offlineAttachmentPaths;
    bool removedQueue = false;
    if (ensureAccountDatabase()) {
        const QString connectionName = "offline_delivered_" + QString::number(reinterpret_cast<quintptr>(this));
        QVector<qint64> deliveredIds;
        {
            QSqlDatabase db = openAccountDatabase(connectionName);
            if (openAccountDatabaseConnection(db, connectionName)) {
                QSqlQuery query(db);
                query.prepare("SELECT id, payload FROM offline_messages WHERE receiver_id = ? ORDER BY id ASC");
                query.addBindValue(receiverId);
                if (query.exec()) {
                    while (query.next()) {
                        const QJsonDocument doc = QJsonDocument::fromJson(query.value(1).toString().toUtf8());
                        if (!doc.isObject()) continue;
                        const QJsonObject obj = doc.object();
                        if (!matchesPayload(obj)) continue;
                        deliveredIds.append(query.value(0).toLongLong());
                        const QString offlinePath = obj["offlineFilePath"].toString();
                        if (!offlinePath.isEmpty()) {
                            offlineAttachmentPaths.append(QDir::cleanPath(offlinePath));
                        }
                    }
                }
                for (qint64 messageId : deliveredIds) {
                    QSqlQuery deleteQuery(db);
                    deleteQuery.prepare("DELETE FROM offline_messages WHERE id = ?");
                    deleteQuery.addBindValue(messageId);
                    if (deleteQuery.exec()) {
                        removedQueue = true;
                    }
                }
                db.close();
            }
        }
        releaseAccountDatabase(connectionName);
    }

    QFile jsonlFile(offlineFilePath(receiverId));
    if (jsonlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QVector<QByteArray> remainingLines;
        while (!jsonlFile.atEnd()) {
            const QByteArray line = jsonlFile.readLine().trimmed();
            if (line.isEmpty()) continue;
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (doc.isObject() && matchesPayload(doc.object())) {
                const QString offlinePath = doc.object()["offlineFilePath"].toString();
                if (!offlinePath.isEmpty()) {
                    offlineAttachmentPaths.append(QDir::cleanPath(offlinePath));
                }
                removedQueue = true;
                continue;
            }
            remainingLines.append(line);
        }
        jsonlFile.close();

        if (removedQueue) {
            if (remainingLines.isEmpty()) {
                jsonlFile.remove();
            } else if (jsonlFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                for (const QByteArray& line : remainingLines) {
                    jsonlFile.write(line);
                    jsonlFile.write("\n");
                }
                jsonlFile.close();
            }
        }
    }

    if (!removedQueue) {
        return result;
    }
    result.queueCleaned = true;

    offlineAttachmentPaths.removeDuplicates();
    for (const QString& path : offlineAttachmentPaths) {
        if (!path.isEmpty()) {
            QFile::remove(path);
        }
    }

    std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore();
    if (objectStore) {
        result.objectDeleteAttempted = true;
        result.objectDeleted = objectStore->removeObject(objectKey);
        if (result.objectDeleted) {
            result.objectDeleteReason = QStringLiteral("success");
        } else {
            result.objectDeleteReason =
                objectStoreRemoveFailureReasonForLog(objectStoreType(), objectStore->lastRemoveFailureReason());
        }
    }

    return result;
}

QString Server::saveOfflineAttachment(const Message& msg) const {
    if (msg.receiverId.isEmpty() || msg.fileData.isEmpty()) return {};
    if (!hasOfflineAttachmentCapacity(msg.fileData.size())) {
        qWarning() << "Offline attachment quota exceeded for" << msg.receiverId
                   << "file:" << msg.fileName
                   << "size:" << msg.fileData.size()
                   << "quota:" << offlineAttachmentQuotaBytes();
        return {};
    }

    const QString dir = offlineAttachmentDir(msg.receiverId);
    for (int attempt = 0; attempt < 5; ++attempt) {
        const QString filePath = QString("%1/%2_%3.bin")
            .arg(dir,
                 QString::number(QDateTime::currentMSecsSinceEpoch()),
                 QString::number(QRandomGenerator::global()->generate()));
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) continue;

        const qint64 written = file.write(msg.fileData);
        file.close();
        if (written == msg.fileData.size()) {
            return filePath;
        }
        file.remove();
    }
    return {};
}

QSet<QString> Server::collectReferencedOfflineAttachments() const {
    QSet<QString> referencedPaths;
    auto collectPayload = [&referencedPaths](const QByteArray& payload) {
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject()) return;

        const QString filePath = doc.object()["offlineFilePath"].toString();
        if (!filePath.isEmpty()) {
            referencedPaths.insert(QDir::cleanPath(filePath));
        }
    };

    if (ensureAccountDatabase()) {
        QString connectionName = "offline_refs_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = openAccountDatabase(connectionName);
            if (openAccountDatabaseConnection(db, connectionName)) {
                QSqlQuery query(db);
                if (query.exec("SELECT payload FROM offline_messages ORDER BY id ASC")) {
                    while (query.next()) {
                        collectPayload(query.value(0).toString().toUtf8());
                    }
                }
                db.close();
            }
        }
        releaseAccountDatabase(connectionName);
    }

    QString baseDir = appDataDir();
    if (baseDir.isEmpty()) baseDir = ".";
    QDir offlineDir(baseDir + "/offline");
    const QFileInfoList offlineFiles = offlineDir.entryInfoList(QStringList() << "*.jsonl", QDir::Files);
    for (const QFileInfo& info : offlineFiles) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        while (!file.atEnd()) {
            collectPayload(file.readLine().trimmed());
        }
    }

    return referencedPaths;
}

void Server::cleanupExpiredOfflineAttachments() {
    const QString rootDirPath = offlineAttachmentRootDir();
    QDir rootDir(rootDirPath);
    if (!rootDir.exists()) return;

    const QSet<QString> referencedPaths = collectReferencedOfflineAttachments();
    const QDateTime now = QDateTime::currentDateTime();
    QStringList visitedDirs;
    QDirIterator it(rootDirPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (info.isDir()) {
            visitedDirs.append(path);
            continue;
        }

        const QString cleanPath = QDir::cleanPath(path);
        const bool isExpired = info.lastModified().msecsTo(now) > offlineAttachmentTtlMs();
        const bool isOrphaned = !referencedPaths.contains(cleanPath);
        if ((isExpired || isOrphaned) && QFile::remove(path)) {
            qDebug() << "Cleaned offline attachment" << cleanPath
                     << (isExpired ? "expired" : "orphaned");
        }
    }

    std::sort(visitedDirs.begin(), visitedDirs.end(), [](const QString& left, const QString& right) {
        return left.count(QLatin1Char('/')) > right.count(QLatin1Char('/'));
    });
    for (const QString& dirPath : visitedDirs) {
        const QFileInfo info(dirPath);
        QDir parentDir(info.absolutePath());
        parentDir.rmdir(info.fileName());
    }

    std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore();
    if (objectStore) {
        QStringList removedKeys;
        const int removedObjects = objectStore->cleanupExpired(objectStoreTtlMs(), &removedKeys);
        if (removedObjects > 0) {
            qDebug() << "Cleaned expired object-store attachments" << removedObjects << removedKeys;
            QJsonObject cleanupMeta;
            cleanupMeta["objectKey"] = removedKeys.join(QLatin1Char(','));
            logRedisLargeFileRouteEvent(QStringLiteral("object_ttl_cleanup"),
                                        QStringLiteral("removed"),
                                        largeFileRouteLogMetadata(cleanupMeta, objectStoreType(), QStringLiteral("delete")),
                                        QStringLiteral("expired"),
                                        removedObjects);
        }
    }
}

void Server::saveOfflineMessage(const Message& msg) const {
    QJsonObject obj;
    obj["type"] = msg.type == MessageType::File || msg.type == MessageType::Image ? "file" : "private";
    obj["messageType"] = static_cast<int>(msg.type);
    obj["senderId"] = msg.senderId;
    obj["senderName"] = msg.senderName;
    obj["senderAvatar"] = msg.senderAvatar;
    obj["receiverId"] = msg.receiverId;
    obj["content"] = msg.content;
    obj["fileName"] = msg.fileName;
    obj["transferId"] = msg.transferId;
    obj["fileSize"] = QString::number(msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
    obj["fileHash"] = msg.fileHash;
    obj["chunkSize"] = QString::number(msg.chunkSize);
    obj["chunkCount"] = QString::number(msg.chunkCount);
    appendE2EFields(&obj, msg);
    QString savedAttachmentPath;
    QString savedObjectKey;
    bool shouldPublishObjectOffer = false;
    if (!msg.fileData.isEmpty()) {
        const bool shouldStoreAsAttachment = msg.type == MessageType::File || msg.type == MessageType::Image;
        const QString attachmentPath = shouldStoreAsAttachment ? saveOfflineAttachment(msg) : QString();
        if (shouldStoreAsAttachment && attachmentPath.isEmpty()) {
            qWarning() << "Offline file was not queued because attachment storage failed" << msg.receiverId << msg.fileName;
            return;
        } else if (!attachmentPath.isEmpty()) {
            savedAttachmentPath = attachmentPath;
            obj["offlineFilePath"] = attachmentPath;
            obj["offlineFileStoredOnDisk"] = true;

            if (shouldPublishLargeFileOffer(msg)) {
                QString objectStoreError;
                std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore(&objectStoreError);
                QString objectKey;
                QString objectHash;
                QString objectError;
                const QString extension = QFileInfo(msg.fileName).suffix();
                if (objectStore
                    && objectStore->writeObject(msg.fileData, &objectKey, &objectHash, &objectError, extension)
                    && objectHash.compare(msg.fileHash.trimmed(), Qt::CaseInsensitive) == 0) {
                    savedObjectKey = objectKey;
                    obj["objectStoreKey"] = objectKey;
                    obj["objectStoreHash"] = objectHash;
                    obj["objectStoreCreatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
                    obj["objectStoreExpiresAt"] = QDateTime::currentDateTimeUtc().addMSecs(objectStoreTtlMs()).toString(Qt::ISODate);
                    shouldPublishObjectOffer = true;
                } else {
                    if (objectStore && !objectKey.isEmpty()) {
                        objectStore->removeObject(objectKey);
                    }
                    QJsonObject logMeta;
                    logMeta["transferId"] = msg.transferId;
                    logMeta["receiverId"] = msg.receiverId;
                    logMeta["fileName"] = msg.fileName;
                    logMeta["messageType"] = msg.type == MessageType::Image ? QStringLiteral("Image") : QStringLiteral("File");
                    if (!objectKey.isEmpty()) {
                        logMeta["objectKey"] = objectKey;
                    }
                    const QString writeFailureReason =
                        objectStoreWriteFailureReasonForLog(objectStoreType(),
                                                            static_cast<bool>(objectStore),
                                                            objectError.isEmpty() ? objectStoreError : objectError,
                                                            msg.fileHash,
                                                            objectHash);
                    logRedisLargeFileRouteEvent(QStringLiteral("object_write"),
                                                QStringLiteral("skipped"),
                                                largeFileRouteLogMetadata(logMeta, objectStoreType(), QStringLiteral("write")),
                                                writeFailureReason,
                                                msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
                    qWarning() << "Large file object routing skipped because object write/validation failed"
                               << msg.receiverId << msg.fileName << writeFailureReason;
                }
            }
        } else {
            obj["fileData"] = QString::fromLatin1(msg.fileData.toBase64());
        }
    }
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    auto rollbackSavedAttachment = [this, &savedAttachmentPath, &savedObjectKey, &obj]() {
        if (!savedAttachmentPath.isEmpty() && QFile::remove(savedAttachmentPath)) {
            qWarning() << "Rolled back offline attachment after queue persistence failure" << savedAttachmentPath;
        }
        std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore();
        if (objectStore && !savedObjectKey.isEmpty()) {
            const bool removedObject = objectStore->removeObject(savedObjectKey);
            QString reason = removedObject ? QStringLiteral("success") : QStringLiteral("unknown");
            if (!removedObject) {
                reason = objectStoreRemoveFailureReasonForLog(objectStoreType(), objectStore->lastRemoveFailureReason());
            }
            QJsonObject logMeta;
            logMeta["transferId"] = obj["transferId"].toString();
            logMeta["receiverId"] = obj["receiverId"].toString();
            logMeta["objectKey"] = savedObjectKey;
            logRedisLargeFileRouteEvent(QStringLiteral("object_delete"),
                                        removedObject ? QStringLiteral("deleted") : QStringLiteral("retained"),
                                        largeFileRouteLogMetadata(logMeta, objectStoreType(), QStringLiteral("delete")),
                                        reason,
                                        obj["fileSize"].toVariant().toLongLong());
            if (removedObject) {
                qWarning() << "Rolled back object-store attachment after queue persistence failure" << savedObjectKey;
            } else {
                qWarning() << "Object-store rollback delete failed after queue persistence failure"
                           << savedObjectKey << reason;
            }
        }
    };

    bool savedToSqlite = false;
    if (ensureAccountDatabase()) {
        QString connectionName = "offline_write_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = openAccountDatabase(connectionName);
            if (openAccountDatabaseConnection(db, connectionName)) {
                QSqlQuery query(db);
                query.prepare("INSERT INTO offline_messages(receiver_id, payload, created_at) VALUES(?, ?, CURRENT_TIMESTAMP)");
                query.addBindValue(msg.receiverId);
                query.addBindValue(QString::fromUtf8(payload));
                savedToSqlite = query.exec();
                db.close();
            }
        }
        releaseAccountDatabase(connectionName);
    }
    if (savedToSqlite) {
        if (shouldPublishObjectOffer && !publishRedisLargeFileOffer(obj)) {
            qWarning() << "Large file offer publish failed; origin offline queue remains as fallback"
                       << msg.receiverId << msg.fileName;
        }
        return;
    }

    QFile file(offlineFilePath(msg.receiverId));
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        rollbackSavedAttachment();
        return;
    }

    const bool savedToJsonl = file.write(payload) == payload.size()
        && file.write("\n") == 1;
    file.close();
    if (!savedToJsonl) {
        rollbackSavedAttachment();
    } else if (shouldPublishObjectOffer && !publishRedisLargeFileOffer(obj)) {
        qWarning() << "Large file offer publish failed; origin offline queue remains as fallback"
                   << msg.receiverId << msg.fileName;
    }
}

bool Server::updateOfflineMessageProgress(qint64 sqliteMessageId, const QJsonObject& obj, qint64 confirmedBytes, qint64 confirmedChunkIndex) const {
    if (sqliteMessageId <= 0 || confirmedBytes <= 0 || !ensureAccountDatabase()) {
        return false;
    }

    bool saved = false;
    const QString connectionName = "offline_progress_" + QString::number(QCoreApplication::applicationPid())
        + "_" + QString::number(QRandomGenerator::global()->generate());
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QJsonObject updated = obj;
            QSqlQuery selectQuery(db);
            selectQuery.prepare("SELECT payload FROM offline_messages WHERE id = ?");
            selectQuery.addBindValue(sqliteMessageId);
            if (selectQuery.exec() && selectQuery.next()) {
                const QJsonDocument currentDoc = QJsonDocument::fromJson(selectQuery.value(0).toString().toUtf8());
                if (currentDoc.isObject()) {
                    updated = currentDoc.object();
                }
            }

            const qint64 existingConfirmedBytes = updated["confirmedBytes"].toVariant().toLongLong();
            updated["confirmedBytes"] = QString::number(qMax(existingConfirmedBytes, confirmedBytes));
            QJsonArray confirmedChunks = updated["confirmedChunks"].toArray();
            bool alreadyRecorded = false;
            for (const QJsonValue& value : confirmedChunks) {
                if (value.toVariant().toLongLong() == confirmedChunkIndex) {
                    alreadyRecorded = true;
                    break;
                }
            }
            if (confirmedChunkIndex >= 0 && !alreadyRecorded) {
                confirmedChunks.append(QString::number(confirmedChunkIndex));
            }
            updated["confirmedChunks"] = confirmedChunks;
            updated["resumeUpdatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

            QSqlQuery query(db);
            query.prepare("UPDATE offline_messages SET payload = ? WHERE id = ?");
            query.addBindValue(QString::fromUtf8(QJsonDocument(updated).toJson(QJsonDocument::Compact)));
            query.addBindValue(sqliteMessageId);
            saved = query.exec();
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return saved;
}

bool Server::deliverOfflinePayload(const QByteArray& payload, QTcpSocket* socket, qint64 sqliteMessageId) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState || payload.isEmpty()) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        const bool written = socket->write(payload) > 0 && socket->write("\n") > 0;
        socket->flush();
        return written;
    }

    const QJsonObject obj = doc.object();
    const QString offlineFilePath = obj["offlineFilePath"].toString();
    const int messageType = obj["messageType"].toInt(static_cast<int>(MessageType::File));
    const bool isFileMessage = messageType == static_cast<int>(MessageType::File)
        || messageType == static_cast<int>(MessageType::Image);
    if (!isFileMessage || offlineFilePath.isEmpty()) {
        const qint64 written = socket->write(payload);
        socket->write("\n");
        socket->flush();
        return written > 0;
    }

    const OfflineAttachmentValidationResult validation = validateOfflineAttachmentForReplay(obj, offlineFilePath, socket);
    if (validation == OfflineAttachmentValidationResult::CleanedBadState) {
        return true;
    }
    if (validation == OfflineAttachmentValidationResult::Blocked) {
        return false;
    }

    const bool delivered = sendOfflineAttachmentToSocket(obj, offlineFilePath, socket, sqliteMessageId);
    if (delivered) {
        QFile::remove(offlineFilePath);
    }
    return delivered;
}

bool Server::sendOfflineAttachmentToSocket(const QJsonObject& obj, const QString& filePath, QTcpSocket* socket, qint64 sqliteMessageId) {
    QPointer<QTcpSocket> socketGuard(socket);
    if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const qint64 totalBytes = QFileInfo(file).size();
    const qint64 declaredChunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 chunkSize = (declaredChunkSize > 0 && declaredChunkSize <= kForwardChunkBytes)
        ? declaredChunkSize
        : kForwardChunkBytes;
    const qint64 chunkCount = (totalBytes + chunkSize - 1) / chunkSize;
    const QString fileName = obj["fileName"].toString();
    const OfflineAttachmentReplayPlan replayPlan = buildOfflineAttachmentReplayPlan(obj,
                                                                                   totalBytes,
                                                                                   chunkSize,
                                                                                   chunkCount,
                                                                                   sqliteMessageId);
    if (replayPlan.startChunkIndex > 0
        && replayPlan.startChunkIndex < chunkCount
        && !file.seek(replayPlan.startChunkIndex * chunkSize)) {
        qWarning() << "Offline attachment resume seek failed:" << fileName << replayPlan.startChunkIndex << "/" << chunkCount;
        return false;
    }
    const QString transferId = QString("%1_%2_%3")
        .arg(obj["senderId"].toString(),
             QString::number(QDateTime::currentMSecsSinceEpoch()),
             QString::number(QRandomGenerator::global()->generate()));

    for (qint64 index = replayPlan.startChunkIndex; index < chunkCount; ++index) {
        if (replayPlan.confirmedChunksValid && replayPlan.confirmedChunkIndexes.contains(index)) {
            continue;
        }
        if (!file.seek(index * chunkSize)) {
            qWarning() << "Offline attachment chunk seek failed:" << fileName << index + 1 << "/" << chunkCount;
            return false;
        }
        const QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty() || (index < chunkCount - 1 && chunk.size() != chunkSize)) {
            qWarning() << "Offline attachment chunk read failed:" << fileName << index + 1 << "/" << chunkCount;
            return false;
        }

        QJsonObject chunkObj;
        chunkObj["type"] = "file_chunk";
        chunkObj["transferId"] = transferId;
        chunkObj["messageType"] = obj["messageType"].toInt(static_cast<int>(MessageType::File));
        chunkObj["senderId"] = obj["senderId"].toString();
        chunkObj["senderName"] = obj["senderName"].toString();
        chunkObj["receiverId"] = obj["receiverId"].toString();
        chunkObj["content"] = obj["content"].toString();
        chunkObj["fileName"] = fileName;
        chunkObj["fileSize"] = QString::number(totalBytes);
        chunkObj["fileHash"] = obj["fileHash"].toString();
        chunkObj["chunkSize"] = QString::number(chunkSize);
        chunkObj["chunkCount"] = QString::number(chunkCount);
        chunkObj["chunkIndex"] = QString::number(index);
        chunkObj["fileData"] = QString::fromLatin1(chunk.toBase64());
        appendE2EFileFields(&chunkObj, obj);

        QString ackRejectReason;
        qint64 ackReceivedBytes = 0;
        bool acknowledged = false;
        const QByteArray data = QJsonDocument(chunkObj).toJson(QJsonDocument::Compact);
        for (int attempt = 1; attempt <= kChunkSendMaxAttempts; ++attempt) {
            if (sendFileChunkAndWaitForAck(socketGuard, data, transferId, index, &ackRejectReason, &ackReceivedBytes)) {
                const qint64 expectedAckBytes = qMin(totalBytes, index * chunkSize + chunk.size());
                if (ackReceivedBytes > 0 && (ackReceivedBytes < expectedAckBytes || ackReceivedBytes > totalBytes)) {
                    if (attempt == kChunkSendMaxAttempts) {
                        ackRejectReason = QString::fromUtf8("离线附件确认进度非法");
                    }
                    continue;
                }
                acknowledged = true;
                break;
            }
            if (!socketGuard) {
                return false;
            }
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "Offline attachment chunk rejected by receiver:" << ackRejectReason;
                return false;
            }
        }
        if (!acknowledged) {
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "Offline attachment chunk rejected by receiver:" << ackRejectReason;
                return false;
            }
            qWarning() << "Offline attachment chunk ack timeout:" << fileName << index + 1 << "/" << chunkCount;
            return false;
        }
        const qint64 fallbackConfirmedBytes = qMin(totalBytes, (index + 1) * chunkSize);
        const qint64 confirmedBytes = qBound<qint64>(0, ackReceivedBytes > 0 ? ackReceivedBytes : fallbackConfirmedBytes, totalBytes);
        if (confirmedBytes > 0) {
            updateOfflineMessageProgress(sqliteMessageId, obj, confirmedBytes, index);
        }
    }

    return true;
}

void Server::sendOfflineMessages(const QString& userId, QTcpSocket* socket) {
    if (ensureAccountDatabase()) {
        QString connectionName = "offline_read_" + QString::number(reinterpret_cast<quintptr>(this));
        QVector<qint64> deliveredIds;
        QVector<QPair<qint64, QByteArray>> pendingRows;
        {
            QSqlDatabase db = openAccountDatabase(connectionName);
            if (openAccountDatabaseConnection(db, connectionName)) {
                QSqlQuery query(db);
                query.prepare("SELECT id, payload FROM offline_messages WHERE receiver_id = ? ORDER BY id ASC");
                query.addBindValue(userId);
                if (query.exec()) {
                    while (query.next()) {
                        const qint64 messageId = query.value(0).toLongLong();
                        QByteArray line = query.value(1).toString().toUtf8();
                        if (line.isEmpty()) continue;
                        pendingRows.append(qMakePair(messageId, line));
                    }
                }
                for (const auto& row : pendingRows) {
                    if (!deliverOfflinePayload(row.second, socket, row.first)) {
                        break;
                    }
                    deliveredIds.append(row.first);
                }
                if (!deliveredIds.isEmpty()) {
                    for (qint64 messageId : deliveredIds) {
                        QSqlQuery deleteQuery(db);
                        deleteQuery.prepare("DELETE FROM offline_messages WHERE id = ?");
                        deleteQuery.addBindValue(messageId);
                        deleteQuery.exec();
                    }
                }
                db.close();
            }
        }
        releaseAccountDatabase(connectionName);
    }

    QFile file(offlineFilePath(userId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QVector<QByteArray> remainingLines;
    bool deliveryBlocked = false;
    while (!file.atEnd()) {
        QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) continue;
        if (deliveryBlocked || !deliverOfflinePayload(line, socket)) {
            deliveryBlocked = true;
            remainingLines.append(line);
        }
    }
    file.close();
    if (remainingLines.isEmpty()) {
        file.remove();
        return;
    }
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        for (const QByteArray& line : remainingLines) {
            file.write(line);
            file.write("\n");
        }
    }
}

bool Server::sendChunkedFileToSocket(const Message& msg, QTcpSocket* socket) {
    QPointer<QTcpSocket> socketGuard(socket);
    if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState || msg.fileData.isEmpty()) {
        return false;
    }

    const qint64 totalBytes = msg.fileData.size();
    const qint64 chunkSize = (msg.chunkSize > 0 && msg.chunkSize <= kForwardChunkBytes)
        ? msg.chunkSize
        : kForwardChunkBytes;
    const qint64 chunkCount = (totalBytes + chunkSize - 1) / chunkSize;
    const QString transferId = QString("%1_%2_%3")
        .arg(msg.senderId,
             QString::number(QDateTime::currentMSecsSinceEpoch()),
             QString::number(QRandomGenerator::global()->generate()));

    for (qint64 index = 0; index < chunkCount; ++index) {
        const qint64 offset = index * chunkSize;
        const QByteArray chunk = msg.fileData.mid(static_cast<int>(offset), static_cast<int>(qMin(chunkSize, totalBytes - offset)));

        QJsonObject obj;
        obj["type"] = "file_chunk";
        obj["transferId"] = transferId;
        obj["messageType"] = static_cast<int>(msg.type);
        obj["senderId"] = msg.senderId;
        obj["senderName"] = msg.senderName;
        obj["senderAvatar"] = msg.senderAvatar;
        obj["receiverId"] = msg.receiverId;
        obj["content"] = msg.content;
        obj["fileName"] = msg.fileName;
        obj["fileSize"] = QString::number(totalBytes);
        obj["fileHash"] = msg.fileHash;
        obj["chunkSize"] = QString::number(chunkSize);
        obj["chunkCount"] = QString::number(chunkCount);
        obj["chunkIndex"] = QString::number(index);
        obj["fileData"] = QString::fromLatin1(chunk.toBase64());
        appendE2EFields(&obj, msg);

        QString ackRejectReason;
        qint64 ackReceivedBytes = 0;
        bool acknowledged = false;
        const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        for (int attempt = 1; attempt <= kChunkSendMaxAttempts; ++attempt) {
            if (sendFileChunkAndWaitForAck(socketGuard, data, transferId, index, &ackRejectReason, &ackReceivedBytes)) {
                const qint64 expectedAckBytes = qMin(totalBytes, index * chunkSize + chunk.size());
                if (ackReceivedBytes > 0 && (ackReceivedBytes < expectedAckBytes || ackReceivedBytes > totalBytes)) {
                    if (attempt == kChunkSendMaxAttempts) {
                        ackRejectReason = QString::fromUtf8("文件分片确认进度非法");
                    }
                    continue;
                }
                acknowledged = true;
                break;
            }
            if (!socketGuard) {
                return false;
            }
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "File chunk rejected by receiver:" << ackRejectReason;
                return false;
            }
        }
        if (!acknowledged) {
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "File chunk rejected by receiver:" << ackRejectReason;
                return false;
            }
            qWarning() << "File chunk ack timeout:" << msg.fileName << index + 1 << "/" << chunkCount;
            return false;
        }
    }
    return true;
}

bool Server::sendFileChunkAndWaitForAck(QTcpSocket* socket,
                                         const QByteArray& data,
                                         const QString& transferId,
                                         qint64 chunkIndex,
                                         QString* rejectReason,
                                         qint64* receivedBytes) {
    if (rejectReason) rejectReason->clear();
    if (receivedBytes) *receivedBytes = 0;
    QPointer<QTcpSocket> socketGuard(socket);
    if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState || transferId.isEmpty() || data.isEmpty()) {
        return false;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    bool matched = false;
    bool accepted = false;
    QString reason;
    qint64 ackReceivedBytes = 0;

    QMetaObject::Connection ackConnection = connect(
        this,
        &Server::fileChunkAckReceived,
        &loop,
        [&](QTcpSocket* ackSocket, const QString& ackTransferId, qint64 ackChunkIndex, bool ackAccepted, const QString& ackReason, qint64 ackBytes) {
            if (!socketGuard || ackSocket != socketGuard.data() || ackTransferId != transferId || ackChunkIndex != chunkIndex) return;
            matched = true;
            accepted = ackAccepted;
            reason = ackReason;
            ackReceivedBytes = ackBytes;
            loop.quit();
        });
    QMetaObject::Connection disconnectedConnection = connect(socketGuard, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QMetaObject::Connection destroyedConnection = connect(socketGuard, &QObject::destroyed, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    const bool sent = socketGuard->write(data) > 0 && socketGuard->write("\n") > 0;
    if (sent) {
        socketGuard->flush();
        timer.start(kChunkAckTimeoutMs);
        loop.exec();
    }

    QObject::disconnect(ackConnection);
    QObject::disconnect(disconnectedConnection);
    QObject::disconnect(destroyedConnection);

    if (!sent) {
        return false;
    }
    if (matched && !accepted && rejectReason) {
        *rejectReason = reason.isEmpty() ? "客户端拒绝分片" : reason;
    }
    if (matched && receivedBytes) {
        *receivedBytes = ackReceivedBytes;
    }
    return matched && accepted;
}

void Server::cleanupExpiredFileTransfers() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QString& key : m_pendingFileTransfers.keys()) {
        const auto it = m_pendingFileTransfers.constFind(key);
        if (it == m_pendingFileTransfers.constEnd()) {
            continue;
        }
        const PendingFileTransfer& pending = it.value();
        if (pending.lastActivityMs <= 0 || now - pending.lastActivityMs <= kTransferStaleTimeoutMs) {
            continue;
        }

        const QString visibleName = pending.fileName.isEmpty() ? "未命名文件" : pending.fileName;
        QTcpSocket* socket = pending.socket;
        const int receivedCount = pending.receivedIndexes.size();
        const qint64 chunkCount = pending.chunkCount;
        m_pendingFileTransfers.remove(key);
        if (socket && socket->state() == QAbstractSocket::ConnectedState) {
            sendSystemNotice(socket, QString("文件分片上传已超时清理：%1。请重新发送。").arg(visibleName));
        }
        qWarning() << "Cleaned expired incoming file transfer" << visibleName << receivedCount << "/" << chunkCount;
    }
}

void Server::broadcastMessage(const Message& msg, QTcpSocket* excludeSocket) {
    QJsonObject obj;
    obj["type"] = msg.type == MessageType::System ? "system" : ((msg.type == MessageType::File || msg.type == MessageType::Image) ? "file" : (msg.isPrivate() ? "private" : "message"));
    obj["messageType"] = static_cast<int>(msg.type);
    obj["senderId"] = msg.senderId;
    obj["senderName"] = msg.senderName;
    obj["senderAvatar"] = msg.senderAvatar;
    obj["receiverId"] = msg.receiverId;
    obj["content"] = msg.content;
    obj["fileName"] = msg.fileName;
    obj["fileSize"] = QString::number(msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
    obj["fileHash"] = msg.fileHash;
    obj["chunkSize"] = QString::number(msg.chunkSize);
    obj["chunkCount"] = QString::number(msg.chunkCount);
    if (!msg.fileData.isEmpty() && msg.type != MessageType::File && msg.type != MessageType::Image) {
        obj["fileData"] = QString::fromLatin1(msg.fileData.toBase64());
    }
    appendE2EFields(&obj, msg);
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        QTcpSocket* socket = it.key();
        if (socket != excludeSocket && socket->state() == QAbstractSocket::ConnectedState) {
            if ((msg.type == MessageType::File || msg.type == MessageType::Image) && !msg.fileData.isEmpty()) {
                sendChunkedFileToSocket(msg, socket);
            } else {
                socket->write(data);
                socket->write("\n");
                socket->flush();
            }
        }
    }
}

void Server::sendToUser(const Message& msg) {
    QString receiverId = msg.receiverId;
    if (receiverId.isEmpty()) return;

    QTcpSocket* targetSocket = m_userSockets.value(receiverId);
    if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
        if ((msg.type == MessageType::File || msg.type == MessageType::Image) && !msg.fileData.isEmpty()) {
            if (!sendChunkedFileToSocket(msg, targetSocket)) {
                saveOfflineMessage(msg);
            }
            return;
        }

        QJsonObject obj;
        obj["type"] = msg.type == MessageType::File || msg.type == MessageType::Image ? "file" : "private";
        obj["messageType"] = static_cast<int>(msg.type);
        obj["senderId"] = msg.senderId;
        obj["senderName"] = msg.senderName;
        obj["senderAvatar"] = msg.senderAvatar;
        obj["receiverId"] = msg.receiverId;
        obj["content"] = msg.content;
        obj["fileName"] = msg.fileName;
        obj["fileSize"] = QString::number(msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
        obj["fileHash"] = msg.fileHash;
        obj["chunkSize"] = QString::number(msg.chunkSize);
        obj["chunkCount"] = QString::number(msg.chunkCount);
        if (!msg.fileData.isEmpty()) {
            obj["fileData"] = QString::fromLatin1(msg.fileData.toBase64());
        }
        appendE2EFields(&obj, msg);
        QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        targetSocket->write(data);
        targetSocket->write("\n");
        targetSocket->flush();
    } else {
        saveOfflineMessage(msg);
    }
}

void Server::sendUserList(QTcpSocket* socket) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QJsonObject obj;
    obj["type"] = "userlist";

    QSet<QString> onlineIds;
    QMap<QString, QString> onlineNames;
    QJsonArray users;
    if (m_redisService->isEnabled()) {
        QList<RedisClient::Presence> redisUsers;
        if (m_redisService->fetchOnlinePresence(&redisUsers)) {
            for (const RedisClient::Presence& user : redisUsers) {
                const QString userId = user.userId.trimmed();
                if (userId.isEmpty()) {
                    continue;
                }
                onlineIds.insert(userId);
                if (!user.userName.trimmed().isEmpty()) {
                    onlineNames.insert(userId, user.userName.trimmed());
                }
            }
        }
    }

    const QJsonObject accounts = loadAccountsFromSqlite();
    for (auto it = accounts.constBegin(); it != accounts.constEnd(); ++it) {
        const QString userId = it.key().trimmed();
        if (userId.isEmpty()) {
            continue;
        }
        const QJsonObject account = it.value().toObject();
        const QString persistedName = account.value("userName").toString().trimmed();
        QJsonObject u;
        u["id"] = userId;
        u["name"] = onlineNames.value(userId, persistedName.isEmpty() ? userId : persistedName);
        u["avatar"] = account.value("avatar").toString();
        u["online"] = onlineIds.contains(userId);
        users.append(u);
    }

    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        const ChatUser& user = it.value();
        if (user.id.trimmed().isEmpty()) {
            continue;
        }

        bool merged = false;
        for (int i = 0; i < users.size(); ++i) {
            QJsonObject existing = users.at(i).toObject();
            if (existing.value("id").toString() != user.id) {
                continue;
            }
            existing["name"] = user.name.isEmpty() ? user.id : user.name;
            existing["avatar"] = user.avatar;
            existing["online"] = true;
            users.replace(i, existing);
            merged = true;
            break;
        }
        if (merged) {
            continue;
        }

        QJsonObject u;
        u["id"] = user.id;
        u["name"] = user.name.isEmpty() ? user.id : user.name;
        u["avatar"] = user.avatar;
        u["online"] = true;
        users.append(u);
    }
    obj["users"] = users;

    socket->write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

void Server::sendServerGroupMemberUpdated(QTcpSocket* socket, const QString& groupId, const QString& memberId, const QString& action) const {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

    QJsonObject obj;
    obj["type"] = "server_group_member_updated";
    obj["groupId"] = groupId;
    obj["memberId"] = memberId;
    obj["action"] = action;
    socket->write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

void Server::sendServerGroupSnapshot(const QString& userId, QTcpSocket* socket) const {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState || userId.isEmpty() || !ensureAccountDatabase()) {
        return;
    }

    QJsonArray groups;
    QJsonArray removedGroups;
    QString connectionName = "server_group_snapshot_" + QString::number(reinterpret_cast<quintptr>(this));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery groupQuery(db);
            groupQuery.prepare("SELECT g.group_id, g.group_name, COALESCE(g.announcement, ''), COALESCE(g.owner_id, ''), "
                               "COALESCE(g.group_type, ''), COALESCE(g.history_policy, ''), COALESCE(g.file_policy, '') "
                               "FROM server_groups g "
                               "JOIN server_group_members m ON m.group_id = g.group_id "
                               "WHERE m.user_id = ? "
                               "ORDER BY g.group_id ASC");
            groupQuery.addBindValue(userId);
            if (groupQuery.exec()) {
                while (groupQuery.next()) {
                    const QString groupId = groupQuery.value(0).toString();
                    QJsonObject groupObj;
                    groupObj["groupId"] = groupId;
                    groupObj["groupName"] = groupQuery.value(1).toString();
                    groupObj["announcement"] = groupQuery.value(2).toString();
                    groupObj["ownerId"] = groupQuery.value(3).toString();
                    const QString groupType = groupQuery.value(4).toString().trimmed().isEmpty()
                        ? serverGroupTypeFromId(groupId)
                        : groupQuery.value(4).toString();
                    const QString historyPolicy = groupQuery.value(5).toString().trimmed().isEmpty()
                        ? serverGroupHistoryPolicy(groupType)
                        : groupQuery.value(5).toString();
                    const QString filePolicy = groupQuery.value(6).toString().trimmed().isEmpty()
                        ? serverGroupFilePolicy(groupType)
                        : groupQuery.value(6).toString();
                    groupObj["groupType"] = groupType;
                    groupObj["membershipState"] = "active";
                    groupObj["historyPolicy"] = historyPolicy;
                    groupObj["filePolicy"] = filePolicy;
                    groupObj["canSend"] = true;
                    groupObj["canSendFiles"] = true;
                    groupObj["canReadHistory"] = true;
                    groupObj["historyVisibility"] = groupType == QLatin1String("private")
                        ? QStringLiteral("active-members-and-removed-readonly")
                        : QStringLiteral("public-members-and-removed-readonly");
                    groupObj["historyReadOnly"] = false;
                    groupObj["historyRetainedAfterRemoval"] = historyPolicy.contains(QStringLiteral("removed-readonly"));

                    QJsonArray members;
                    QSqlQuery memberQuery(db);
                    memberQuery.prepare("SELECT user_id, COALESCE(user_name, ''), role "
                                        "FROM server_group_members "
                                        "WHERE group_id = ? "
                                        "ORDER BY role = 'owner' DESC, role = 'admin' DESC, joined_at ASC, user_id ASC");
                    memberQuery.addBindValue(groupId);
                    if (memberQuery.exec()) {
                        while (memberQuery.next()) {
                            QJsonObject memberObj;
                            memberObj["userId"] = memberQuery.value(0).toString();
                            memberObj["userName"] = memberQuery.value(1).toString();
                            memberObj["role"] = memberQuery.value(2).toString();
                            members.append(memberObj);
                        }
                    }
                    groupObj["members"] = members;

                    QJsonArray auditEvents;
                    QSqlQuery auditQuery(db);
                    auditQuery.prepare("SELECT action, actor_id, COALESCE(actor_name, ''), "
                                       "COALESCE(target_user_id, ''), COALESCE(target_user_name, ''), "
                                       "COALESCE(details, ''), created_at "
                                       "FROM server_group_audit_events "
                                       "WHERE group_id = ? "
                                       "ORDER BY id DESC LIMIT 20");
                    auditQuery.addBindValue(groupId);
                    if (auditQuery.exec()) {
                        while (auditQuery.next()) {
                            QJsonObject auditObj;
                            auditObj["action"] = auditQuery.value(0).toString();
                            auditObj["actorId"] = auditQuery.value(1).toString();
                            auditObj["actorName"] = auditQuery.value(2).toString();
                            auditObj["targetUserId"] = auditQuery.value(3).toString();
                            auditObj["targetUserName"] = auditQuery.value(4).toString();
                            const QJsonDocument detailsDoc = QJsonDocument::fromJson(auditQuery.value(5).toString().toUtf8());
                            auditObj["details"] = detailsDoc.isObject() ? detailsDoc.object() : QJsonObject();
                            auditObj["createdAt"] = auditQuery.value(6).toString();
                            auditEvents.prepend(auditObj);
                        }
                    }
                    groupObj["auditEvents"] = auditEvents;
                    groups.append(groupObj);
                }
            }

            QSqlQuery removedQuery(db);
            removedQuery.prepare("SELECT g.group_id, g.group_name, COALESCE(g.announcement, ''), "
                                 "COALESCE(g.owner_id, ''), COALESCE(g.group_type, ''), "
                                 "COALESCE(g.history_policy, ''), COALESCE(g.file_policy, ''), "
                                 "COALESCE(r.removed_by, ''), "
                                 "COALESCE(r.removed_by_name, ''), r.removed_at "
                                 "FROM server_group_removed_members r "
                                 "JOIN server_groups g ON g.group_id = r.group_id "
                                 "LEFT JOIN server_group_members m ON m.group_id = r.group_id AND m.user_id = r.user_id "
                                 "WHERE r.user_id = ? AND m.user_id IS NULL "
                                 "ORDER BY r.removed_at DESC, g.group_id ASC");
            removedQuery.addBindValue(userId);
            if (removedQuery.exec()) {
                while (removedQuery.next()) {
                    QJsonObject groupObj;
                    groupObj["groupId"] = removedQuery.value(0).toString();
                    groupObj["groupName"] = removedQuery.value(1).toString();
                    groupObj["announcement"] = removedQuery.value(2).toString();
                    groupObj["ownerId"] = removedQuery.value(3).toString();
                    const QString groupType = removedQuery.value(4).toString().trimmed().isEmpty()
                        ? serverGroupTypeFromId(groupObj["groupId"].toString())
                        : removedQuery.value(4).toString();
                    const QString historyPolicy = removedQuery.value(5).toString().trimmed().isEmpty()
                        ? serverGroupHistoryPolicy(groupType)
                        : removedQuery.value(5).toString();
                    const QString filePolicy = removedQuery.value(6).toString().trimmed().isEmpty()
                        ? serverGroupFilePolicy(groupType)
                        : removedQuery.value(6).toString();
                    groupObj["groupType"] = groupType;
                    groupObj["membershipState"] = "removed";
                    groupObj["canSend"] = false;
                    groupObj["canSendFiles"] = false;
                    groupObj["canReadHistory"] = true;
                    groupObj["historyPolicy"] = historyPolicy;
                    groupObj["filePolicy"] = filePolicy;
                    groupObj["historyVisibility"] = groupType == QLatin1String("private")
                        ? QStringLiteral("removed-member-readonly")
                        : QStringLiteral("public-removed-member-readonly");
                    groupObj["historyReadOnly"] = true;
                    groupObj["historyRetainedAfterRemoval"] = historyPolicy.contains(QStringLiteral("removed-readonly"));
                    groupObj["removedBy"] = removedQuery.value(7).toString();
                    groupObj["removedByName"] = removedQuery.value(8).toString();
                    groupObj["removedAt"] = removedQuery.value(9).toString();
                    removedGroups.append(groupObj);
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);

    QJsonObject obj;
    obj["type"] = "server_group_snapshot";
    obj["groups"] = groups;
    obj["removedGroups"] = removedGroups;
    socket->write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}
