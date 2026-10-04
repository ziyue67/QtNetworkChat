#include "server.h"
#include "server_database.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QVariant>
#include <limits>

namespace ServerDatabase {
qint64 positiveIntegerEnvOrDefault(const char* name, qint64 defaultValue, qint64 maxValue) {
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

QString appDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString accountDatabaseDriver() {
    const QString configured = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_DB_DRIVER")).trimmed().toUpper();
    if (configured.isEmpty()) {
        return QStringLiteral("QSQLITE");
    }
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

bool execAccountDatabaseQuery(QSqlQuery& query, const QString& scope, const QString& sql) {
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

QString currentTimestampPlusDaysSql(int days) {
    if (accountDatabaseIsPostgres()) {
        return QStringLiteral("CURRENT_TIMESTAMP + INTERVAL '%1 days'").arg(days);
    }
    return QStringLiteral("datetime('now', '+%1 days')").arg(days);
}

QString accountUidDefaultSql() {
    return accountDatabaseIsPostgres()
        ? QStringLiteral("CAST(gen_random_uuid() AS TEXT)")
        : QStringLiteral("lower(hex(randomblob(4)) || '-' || hex(randomblob(2)) || '-' || hex(randomblob(2)) || '-' || hex(randomblob(2)) || '-' || hex(randomblob(6)))");
}

} // namespace ServerDatabase

using namespace ServerDatabase;

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
                QStringLiteral("server_friends"),
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

bool Server::ensureAccountDatabase() const {
    QString connectionName = "accounts_init_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            const QString idColumn = autoIdColumnSql();
            if (accountDatabaseIsPostgres()) {
                query.exec("CREATE EXTENSION IF NOT EXISTS pgcrypto");
            }
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
                query.exec("ALTER TABLE accounts ADD COLUMN account_uid TEXT");
                query.exec("ALTER TABLE accounts ADD COLUMN login_account TEXT");
                query.exec("ALTER TABLE accounts ADD COLUMN account_generation INTEGER DEFAULT 1");
                query.exec("ALTER TABLE accounts ADD COLUMN account_status TEXT DEFAULT 'active'");
                query.exec("ALTER TABLE accounts ADD COLUMN deactivation_requested_at TEXT");
                query.exec("ALTER TABLE accounts ADD COLUMN deactivation_effective_at TEXT");
                query.exec("ALTER TABLE accounts ADD COLUMN deactivated_at TEXT");
                query.exec("ALTER TABLE accounts ADD COLUMN deactivation_reason TEXT");
                query.exec("UPDATE accounts SET account_uid = " + accountUidDefaultSql() + " WHERE account_uid IS NULL OR account_uid = ''");
                query.exec("UPDATE accounts SET login_account = account WHERE login_account IS NULL OR login_account = ''");
                query.exec("UPDATE accounts SET account_generation = 1 WHERE account_generation IS NULL OR account_generation < 1");
                query.exec("UPDATE accounts SET account_status = 'active' WHERE account_status IS NULL OR account_status = ''");
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
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_friends ("
                                "user_id TEXT NOT NULL, "
                                "friend_id TEXT NOT NULL, "
                                "friend_name TEXT, "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "PRIMARY KEY(user_id, friend_id))");
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
                query.exec("ALTER TABLE server_groups ADD COLUMN avatar TEXT DEFAULT ''");
                query.exec("ALTER TABLE server_groups ADD COLUMN all_muted INTEGER DEFAULT 0");
                query.exec("ALTER TABLE server_groups ADD COLUMN speaking_rule TEXT DEFAULT 'unrestricted'");
                query.exec("ALTER TABLE server_groups ADD COLUMN join_policy TEXT DEFAULT 'approval'");
                query.exec("ALTER TABLE server_groups ADD COLUMN searchable INTEGER DEFAULT 1");
                query.exec("ALTER TABLE server_groups ADD COLUMN search_mode TEXT DEFAULT 'id_and_keyword'");
                query.exec("UPDATE server_groups SET search_mode = CASE WHEN COALESCE(searchable, 1) = 1 THEN 'id_and_keyword' ELSE 'private' END WHERE search_mode IS NULL OR search_mode = ''");
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
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_group_user_settings ("
                                "group_id TEXT NOT NULL, "
                                "user_id TEXT NOT NULL, "
                                "nickname TEXT, "
                                "remark TEXT, "
                                 "mute_notifications INTEGER NOT NULL DEFAULT 0, "
                                 "receive_without_notify INTEGER NOT NULL DEFAULT 0, "
                                 "receive_mode TEXT NOT NULL DEFAULT 'receive_quiet', "
                                 "updated_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                 "PRIMARY KEY(group_id, user_id))");
                query.exec("ALTER TABLE server_group_user_settings ADD COLUMN receive_mode TEXT DEFAULT 'receive_quiet'");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_group_join_requests ("
                                "request_id TEXT PRIMARY KEY, "
                                "group_id TEXT NOT NULL, "
                                "applicant_id TEXT NOT NULL, "
                                "applicant_name TEXT, "
                                "message TEXT, "
                                "state TEXT NOT NULL DEFAULT 'pending', "
                                "reviewed_by TEXT, "
                                "reviewed_by_name TEXT, "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "reviewed_at TEXT, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
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
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_group_essence_messages ("
                                "group_id TEXT NOT NULL, "
                                "message_id TEXT NOT NULL, "
                                "set_by TEXT, "
                                "set_by_name TEXT, "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "PRIMARY KEY(group_id, message_id))");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_message_favorites ("
                                "user_id TEXT NOT NULL, "
                                "session_id TEXT NOT NULL, "
                                "message_id TEXT NOT NULL, "
                                "message_json TEXT, "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "PRIMARY KEY(user_id, session_id, message_id))");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_group_recalled_messages ("
                                "group_id TEXT NOT NULL, "
                                "message_id TEXT NOT NULL, "
                                "recalled_by TEXT, "
                                "recalled_by_name TEXT, "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "PRIMARY KEY(group_id, message_id))");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_group_member_mutes ("
                                "group_id TEXT NOT NULL, "
                                "user_id TEXT NOT NULL, "
                                "muted_until INTEGER NOT NULL, "
                                "muted_by TEXT, "
                                "muted_by_name TEXT, "
                                "reason TEXT, "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "PRIMARY KEY(group_id, user_id))");
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
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_friends_user ON server_friends(user_id, friend_id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_members_user ON server_group_members(user_id, group_id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_removed_members_user ON server_group_removed_members(user_id, group_id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_join_requests_pending ON server_group_join_requests(group_id, state, applicant_id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_announcements_group ON server_group_announcements(group_id, id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_audit_group ON server_group_audit_events(group_id, id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_essence_group ON server_group_essence_messages(group_id, message_id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_message_favorites_user ON server_message_favorites(user_id, updated_at)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_recalled_group ON server_group_recalled_messages(group_id, message_id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_mutes_group ON server_group_member_mutes(group_id, user_id, muted_until)");
                query.exec("CREATE UNIQUE INDEX IF NOT EXISTS ux_accounts_uid ON accounts(account_uid)");
                query.exec("CREATE UNIQUE INDEX IF NOT EXISTS ux_accounts_login_generation ON accounts(login_account, account_generation)");
                if (accountDatabaseIsPostgres()) {
                    query.exec("CREATE UNIQUE INDEX IF NOT EXISTS ux_accounts_login_active_pending "
                               "ON accounts(login_account) "
                               "WHERE account_status IN ('active', 'deactivation_pending')");
                } else {
                    query.exec("CREATE UNIQUE INDEX IF NOT EXISTS ux_accounts_login_active_pending "
                               "ON accounts(login_account) "
                               "WHERE account_status IN ('active', 'deactivation_pending')");
                }
                query.exec("CREATE INDEX IF NOT EXISTS idx_accounts_deactivation_due ON accounts(account_status, deactivation_effective_at)");
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
