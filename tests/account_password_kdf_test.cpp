#include "client.h"
#include "server.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>

#include <functional>

namespace {
QString testAppDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning("%s", message);
        return false;
    }
    return true;
}

bool waitFor(const std::function<bool()>& predicate, int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

quint16 freeLocalPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) return 0;
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

QString legacyPasswordHash(const QString& account, const QString& password) {
    return QString::fromLatin1(QCryptographicHash::hash((account + ":" + password).toUtf8(),
                                                        QCryptographicHash::Sha256).toHex());
}

QString readPasswordHash(const QString& dbPath, const QString& account) {
    const QString connectionName = QStringLiteral("account_password_kdf_read_%1").arg(account);
    QString hash;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare("SELECT password_hash FROM accounts WHERE account = ?");
            query.addBindValue(account);
            if (query.exec() && query.next()) {
                hash = query.value(0).toString();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return hash;
}

bool insertLegacyAccount(const QString& dbPath,
                         const QString& account,
                         const QString& password,
                         const QString& userName) {
    const QString connectionName = QStringLiteral("account_password_kdf_insert_%1").arg(account);
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare("INSERT OR REPLACE INTO accounts(account, password_hash, user_name, updated_at) VALUES(?, ?, ?, datetime('now'))");
            query.addBindValue(account);
            query.addBindValue(legacyPasswordHash(account, password));
            query.addBindValue(userName);
            ok = query.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool loginClient(Client& client,
                 const QString& account,
                 const QString& userName,
                 const QString& password,
                 quint16 port,
                 bool registerMode) {
    client.setUserInfo(account, userName);
    client.setAccountInfo(account, password, registerMode);
    if (!client.connectToServer("127.0.0.1", port)) return false;
    return client.waitForLoginResult(5000);
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("account_password_kdf_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = testAppDataDir();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    const quint16 port = freeLocalPort();
    bool ok = true;
    ok = expect(port != 0, "a local chat test port should be available") && ok;
    if (!ok) return 1;

    const QString dbPath = appDataDir + "/accounts.sqlite3";

    {
        Server bootstrapServer;
        ok = expect(bootstrapServer.start(port), "bootstrap server should create the account database") && ok;
        bootstrapServer.stop();
    }
    ok = expect(ok && insertLegacyAccount(dbPath, "910002", "legacy-secret", "LegacyUser"),
                "legacy SHA-256 account should be inserted before server startup") && ok;
    const QString legacyHash = readPasswordHash(dbPath, "910002");
    ok = expect(legacyHash == legacyPasswordHash("910002", "legacy-secret"),
                "legacy account should start with old SHA-256 hash") && ok;
    if (!ok) return 1;

    Server server;
    ok = expect(server.start(port), "server should start for account KDF test") && ok;
    if (!ok) return 1;

    Client registered;
    ok = expect(loginClient(registered, "910001", "KdfRegistered", "secret", port, true),
                "new account should register with KDF password hash") && ok;
    ok = expect(waitFor([&] { return !readPasswordHash(dbPath, "910001").isEmpty(); }),
                "new account hash should be persisted") && ok;
    const QString newHash = readPasswordHash(dbPath, "910001");
    ok = expect(newHash.startsWith("kdf$pbkdf2-sha256$"),
                "new account should store versioned PBKDF2-SHA256 hash") && ok;
    ok = expect(!newHash.contains("secret"),
                "stored KDF hash should not contain the raw password") && ok;
    registered.disconnectFromServer();

    Client wrongPassword;
    ok = expect(!loginClient(wrongPassword, "910002", "LegacyUser", "wrong-secret", port, false),
                "legacy account should reject wrong password") && ok;
    wrongPassword.disconnectFromServer();
    ok = expect(readPasswordHash(dbPath, "910002") == legacyHash,
                "wrong password should not upgrade legacy hash") && ok;

    Client migrated;
    ok = expect(loginClient(migrated, "910002", "LegacyUser", "legacy-secret", port, false),
                "legacy account should log in with old SHA-256 hash") && ok;
    ok = expect(waitFor([&] { return readPasswordHash(dbPath, "910002").startsWith("kdf$pbkdf2-sha256$"); }),
                "legacy account should upgrade to KDF after successful login") && ok;
    const QString upgradedHash = readPasswordHash(dbPath, "910002");
    ok = expect(upgradedHash != legacyHash,
                "upgraded KDF hash should replace legacy SHA-256 hash") && ok;
    migrated.disconnectFromServer();

    Client relogin;
    ok = expect(loginClient(relogin, "910002", "LegacyUser", "legacy-secret", port, false),
                "upgraded KDF account should log in again") && ok;
    relogin.disconnectFromServer();

    server.stop();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
