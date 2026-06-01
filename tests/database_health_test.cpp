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
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("database_health_test");
    QStandardPaths::setTestModeEnabled(true);

    qunsetenv("QTNETWORKCHAT_DB_DRIVER");
    qunsetenv("QTNETWORKCHAT_PGPASSWORD");
    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
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
