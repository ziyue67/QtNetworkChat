#ifndef LOCALFILEMANAGER_H
#define LOCALFILEMANAGER_H

#include <QFileInfo>
#include <QString>

struct LocalFileValidationResult {
    bool accepted = false;
    bool warningRequired = false;
    QString failureTitle;
    QString failureMessage;
    QString statusMessage;
    QString warningTitle;
    QString warningMessage;
};

class LocalFileManager {
public:
    static QString lastTransferDirectory();
    static void rememberTransferDirectory(const QString& filePath);
    static QString lastAvatarDirectory();
    static void rememberAvatarDirectory(const QString& filePath);
    static LocalFileValidationResult validateTransferFile(const QFileInfo& info, const QString& kind);
    static LocalFileValidationResult validateAvatarFile(const QFileInfo& info);
    static QString humanFileSize(qint64 bytes);
};

#endif // LOCALFILEMANAGER_H
