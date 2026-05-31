#ifndef LOGINCREDENTIALSTORE_H
#define LOGINCREDENTIALSTORE_H

#include <QString>

struct SavedLoginCredential {
    QString account;
    QString userName;
    bool rememberPassword = false;
};

class LoginCredentialStore {
public:
    QString databasePath() const;

    bool ensureDatabase() const;
    bool load(SavedLoginCredential* credential) const;
    bool save(const QString& account, const QString& userName, bool rememberPassword) const;
    bool migrateLegacySettings(SavedLoginCredential* credential = nullptr) const;
};

#endif // LOGINCREDENTIALSTORE_H
