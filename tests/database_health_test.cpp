#include "server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
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
        if (obj.value("name").toString() == name && obj.value("ok").toBool(false)) {
            return true;
        }
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
    const QJsonArray checks = health.value("checks").toArray();
    ok = expect(health.value("format").toString() == "qtnetworkchat-database-health-v1",
                "database health format should be stable") && ok;
    ok = expect(health.value("ok").toBool(false),
                "SQLite database health should be healthy after initialization") && ok;
    ok = expect(health.value("status").toString() == "healthy",
                "SQLite database status should be healthy") && ok;
    ok = expect(config.value("driver").toString() == "QSQLITE",
                "default database health should report SQLite") && ok;
    ok = expect(!config.contains("password"),
                "SQLite database health should not include a password field") && ok;
    ok = expect(hasPassingCheck(checks, "open"),
                "database health should include passing open check") && ok;
    ok = expect(hasPassingCheck(checks, "ping"),
                "database health should include passing ping check") && ok;
    ok = expect(hasPassingCheck(checks, "required-tables"),
                "database health should include passing required table check") && ok;

    server.stop();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
