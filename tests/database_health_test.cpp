#include "server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning() << message;
        return false;
    }
    return true;
}

bool hasPassingCheck(const QJsonArray& checks, const QString& name) {
    for (const QJsonValue& value : checks) {
        const QJsonObject obj = value.toObject();
        if (obj.value("name").toString() == name
            && obj.value("ok").toBool(false)
            && obj.value("reason").toString() == "ok") {
            return true;
        }
    }
    return false;
}

bool hasReasonBucket(const QJsonArray& buckets, const QString& reason) {
    for (const QJsonValue& value : buckets) {
        if (value.toString() == reason) return true;
    }
    return false;
}

QString appDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) return QDir::cleanPath(overrideDir);
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("database_health_test");
    QStandardPaths::setTestModeEnabled(true);

    qunsetenv("QTNETWORKCHAT_DB_DRIVER");
    qunsetenv("QTNETWORKCHAT_PGPASSWORD");
    qunsetenv("QTNETWORKCHAT_DB_POOL_MAX");
    qunsetenv("QTNETWORKCHAT_DB_POOL_IDLE_MS");
    qunsetenv("QTNETWORKCHAT_DB_SLOW_QUERY_MS");
    const QString appDataDir = ::appDataDir();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    Server server;
    bool ok = true;
    ok = expect(server.start(0), "server should start and initialize SQLite account database") && ok;

    const QJsonObject health = server.databaseHealthSnapshot();
    const QJsonObject config = health.value("config").toObject();
    const QJsonObject pool = health.value("pool").toObject();
    const QJsonArray checks = health.value("checks").toArray();
    ok = expect(health.value("format").toString() == "qtnetworkchat-database-health-v1",
                "database health format should be stable") && ok;
    ok = expect(health.value("ok").toBool(false),
                "SQLite database health should be healthy after initialization") && ok;
    ok = expect(health.value("status").toString() == "healthy",
                "SQLite database status should be healthy") && ok;
    ok = expect(health.value("reason").toString() == "ok",
                "SQLite database health should expose ok reason") && ok;
    ok = expect(config.value("driver").toString() == "QSQLITE",
                "default database health should report SQLite") && ok;
    ok = expect(!config.contains("password"),
                "SQLite database health should not include a password field") && ok;
    ok = expect(pool.value("driver").toString() == "QSQLITE",
                "database health should expose pool driver") && ok;
    ok = expect(!pool.value("enabled").toBool(true),
                "SQLite database pool should be disabled by default") && ok;
    ok = expect(pool.contains("createdConnections") && pool.contains("openAttempts") && pool.contains("backoffMs"),
                "database health pool metrics should include connection and backoff counters") && ok;
    ok = expect(pool.value("maxConnections").toString() == "16"
                    && pool.value("idleMs").toString() == "300000"
                    && pool.value("slowQueryMs").toString() == "1000",
                "database health pool metrics should expose default pool and slow-query policy") && ok;
    ok = expect(pool.contains("idleConnectionsClosed")
                    && pool.contains("overflowConnectionsClosed")
                    && pool.contains("pooledConnections"),
                "database health pool metrics should expose idle and overflow cleanup counters") && ok;
    ok = expect(pool.contains("crossThreadCheckoutPrevented")
                    && pool.contains("crossThreadReleaseDetected")
                    && pool.contains("peakPooledConnections")
                    && pool.contains("pooledConnectionThreadCount"),
                "database health pool metrics should expose cross-thread and peak pool counters") && ok;
    const QJsonObject poolThreadPolicy = pool.value("threadPolicy").toObject();
    ok = expect(poolThreadPolicy.value("crossThreadReuse").toBool(true) == false
                    && poolThreadPolicy.value("checkoutScope").toString() == "not-applicable"
                    && poolThreadPolicy.value("releaseScope").toString() == "not-applicable",
                "SQLite database health should expose direct-open thread policy") && ok;
    ok = expect(pool.value("queryAttempts").toString().toLongLong() >= 1
                    && pool.value("queryFailures").toString() == "0"
                    && pool.value("lastQueryFailureReason").toString() == "ok",
                "database health pool metrics should count ping query attempts without failures") && ok;
    ok = expect(pool.contains("slowQueries")
                    && pool.contains("lastQueryDurationMs")
                    && pool.value("lastQueryScope").toString() == "database_health_ping",
                "database health pool metrics should expose slow-query counters and last query scope") && ok;
    ok = expect(hasReasonBucket(pool.value("reasonBuckets").toArray(), "network")
                    && hasReasonBucket(pool.value("reasonBuckets").toArray(), "auth")
                    && hasReasonBucket(pool.value("reasonBuckets").toArray(), "runtime"),
                "database health pool metrics should expose fixed reason buckets") && ok;
    ok = expect(hasPassingCheck(checks, "open"),
                "database health should include passing open check") && ok;
    ok = expect(hasPassingCheck(checks, "ping"),
                "database health should include passing ping check") && ok;
    ok = expect(hasPassingCheck(checks, "required-tables"),
                "database health should include passing required table check") && ok;
    ok = expect(hasPassingCheck(checks, "connection-pool"),
                "database health should include passing connection pool check") && ok;

    const QString exportPath = QDir(appDataDir).filePath("database-health-export.json");
    QFile exportFile(exportPath);
    ok = expect(exportFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
                "database health export file should be writable") && ok;
    if (exportFile.isOpen()) {
        exportFile.write(QJsonDocument(health).toJson(QJsonDocument::Indented));
        exportFile.close();
    }
    ok = expect(QFileInfo::exists(exportPath),
                "database health export JSON should be written") && ok;
    QFile readBack(exportPath);
    ok = expect(readBack.open(QIODevice::ReadOnly),
                "database health export JSON should be readable") && ok;
    if (readBack.isOpen()) {
        const QJsonObject exported = QJsonDocument::fromJson(readBack.readAll()).object();
        ok = expect(exported.value("status").toString() == "healthy",
                    "database health export JSON should preserve healthy status") && ok;
        ok = expect(exported.value("reason").toString() == "ok",
                    "database health export JSON should preserve ok reason") && ok;
        ok = expect(exported.value("config").toObject().value("driver").toString() == "QSQLITE",
                    "database health export JSON should preserve redacted config") && ok;
    }

    server.stop();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
