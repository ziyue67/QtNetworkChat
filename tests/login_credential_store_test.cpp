#include "logincredentialstore.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning("%s", message);
        return false;
    }
    return true;
}

QString appDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) return QDir::cleanPath(overrideDir);
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString storedPassword(const QString& dbPath, const QString& account) {
    const QString name = QStringLiteral("login_store_read_%1").arg(account);
    QString password;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare("SELECT password FROM login_accounts WHERE account = ?");
            query.addBindValue(account);
            if (query.exec() && query.next()) {
                password = query.value(0).toString();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return password;
}

bool insertLegacyPlaintextPassword(const QString& dbPath,
                                   const QString& account,
                                   const QString& userName,
                                   const QString& password) {
    const QString name = QStringLiteral("login_store_insert_%1").arg(account);
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare("INSERT OR REPLACE INTO login_accounts(account, user_name, password, remember_password, updated_at) "
                          "VALUES(?, ?, ?, 1, datetime('now'))");
            query.addBindValue(account);
            query.addBindValue(userName);
            query.addBindValue(password);
            ok = query.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("login_credential_store_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString dir = appDataDir();
    if (!dir.isEmpty()) {
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
    }
    QSettings settings("QtNetworkChat", "QtNetworkChat");
    settings.clear();

    LoginCredentialStore store;
    bool ok = true;
    ok = expect(QDir::cleanPath(store.databasePath()).startsWith(QDir::cleanPath(dir)),
                "login credential database should use isolated app data override") && ok;

    ok = expect(store.save("910100", "SafeUser", true), "saving remembered login should succeed") && ok;
    SavedLoginCredential saved;
    ok = expect(store.load(&saved), "saved login metadata should load") && ok;
    ok = expect(saved.account == "910100", "account should be restored") && ok;
    ok = expect(saved.userName == "SafeUser", "user name should be restored") && ok;
    ok = expect(saved.rememberPassword, "remember flag should be restored") && ok;
    ok = expect(storedPassword(store.databasePath(), "910100").isEmpty(),
                "new remembered login must not store plaintext password") && ok;

    ok = expect(insertLegacyPlaintextPassword(store.databasePath(), "910101", "LegacyLocal", "plain-secret"),
                "legacy plaintext login row should be inserted") && ok;
    SavedLoginCredential migratedSqlite;
    ok = expect(store.load(&migratedSqlite), "legacy login metadata should load") && ok;
    ok = expect(storedPassword(store.databasePath(), "910101").isEmpty(),
                "loading should clear legacy plaintext password from sqlite") && ok;

    settings.setValue("login/account", "910102");
    settings.setValue("login/name", "LegacySettings");
    settings.setValue("login/remember", true);
    settings.setValue("login/password", "settings-secret");
    SavedLoginCredential migratedSettings;
    ok = expect(store.migrateLegacySettings(&migratedSettings), "legacy QSettings should migrate") && ok;
    ok = expect(migratedSettings.account == "910102", "settings account should migrate") && ok;
    ok = expect(settings.value("login/password").toString().isEmpty(),
                "legacy QSettings plaintext password should be removed") && ok;
    ok = expect(storedPassword(store.databasePath(), "910102").isEmpty(),
                "migrated settings login must not store plaintext password in sqlite") && ok;

    if (!dir.isEmpty()) {
        QDir(dir).removeRecursively();
    }
    settings.clear();
    return ok ? 0 : 1;
}
