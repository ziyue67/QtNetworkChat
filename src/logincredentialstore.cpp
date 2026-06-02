#include "logincredentialstore.h"

#include <QDir>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>

namespace {
QString connectionName(const char* purpose, const void* owner) {
    return QString::fromLatin1(purpose) + "_" + QString::number(reinterpret_cast<quintptr>(owner));
}

QString appDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
}

QString LoginCredentialStore::databasePath() const {
    QString dir = appDataDir();
    if (dir.isEmpty()) dir = ".";
    QDir().mkpath(dir);
    return dir + "/login_accounts.sqlite3";
}

bool LoginCredentialStore::ensureDatabase() const {
    const QString name = connectionName("login_accounts_init", this);
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(databasePath());
        if (db.open()) {
            QSqlQuery query(db);
            ok = query.exec("CREATE TABLE IF NOT EXISTS login_accounts ("
                            "account TEXT PRIMARY KEY, "
                            "user_name TEXT, "
                            "password TEXT, "
                            "remember_password INTEGER DEFAULT 0, "
                            "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

bool LoginCredentialStore::load(SavedLoginCredential* credential) const {
    if (!credential || !ensureDatabase()) return false;

    const QString name = connectionName("login_accounts_read", this);
    bool loaded = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(databasePath());
        if (db.open()) {
            QSqlQuery query(db);
            if (query.exec("SELECT account, user_name, remember_password FROM login_accounts ORDER BY updated_at DESC LIMIT 1")
                && query.next()) {
                credential->account = query.value(0).toString();
                credential->userName = query.value(1).toString();
                credential->rememberPassword = query.value(2).toInt() != 0;
                loaded = true;
            }
            QSqlQuery cleanup(db);
            cleanup.exec("UPDATE login_accounts SET password = '' WHERE password IS NOT NULL AND password <> ''");
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return loaded;
}

bool LoginCredentialStore::save(const QString& account, const QString& userName, bool rememberPassword) const {
    if (!ensureDatabase()) return false;

    const QString normalizedAccount = account.trimmed();
    const QString normalizedUserName = userName.trimmed();
    if (normalizedAccount.isEmpty()) return true;

    const QString name = connectionName("login_accounts_write", this);
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(databasePath());
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare("INSERT OR REPLACE INTO login_accounts(account, user_name, password, remember_password, updated_at) "
                          "VALUES(?, ?, '', ?, datetime('now'))");
            query.addBindValue(normalizedAccount);
            query.addBindValue(normalizedUserName);
            query.addBindValue(rememberPassword ? 1 : 0);
            ok = query.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

bool LoginCredentialStore::migrateLegacySettings(SavedLoginCredential* credential) const {
    QSettings settings("QtNetworkChat", "QtNetworkChat");
    const QString account = settings.value("login/account").toString().trimmed();
    const QString userName = settings.value("login/name").toString().trimmed();
    const bool rememberPassword = settings.value("login/remember", false).toBool();

    if (credential && !account.isEmpty()) {
        credential->account = account;
        credential->userName = userName;
        credential->rememberPassword = rememberPassword;
    }

    const bool saved = account.isEmpty() ? ensureDatabase() : save(account, userName, rememberPassword);
    settings.remove("login/password");
    settings.setValue("login/account", account);
    settings.setValue("login/name", userName);
    settings.setValue("login/remember", rememberPassword);
    return saved;
}
