#ifndef LOCALFILEMANAGER_H
#define LOCALFILEMANAGER_H

#include <QByteArray>
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

struct LocalFileSelectionResult {
    bool accepted = false;
    bool canceled = false;
    bool warningRequired = false;
    QString filePath;
    QFileInfo fileInfo;
    QString fileSize;
    QString statusMessage;
    QString failureTitle;
    QString failureMessage;
    QString warningTitle;
    QString warningMessage;
};

class LocalFileManager {
public:
    static QString lastTransferDirectory();
    static void rememberTransferDirectory(const QString& filePath);
    static QString lastAvatarDirectory();
    static void rememberAvatarDirectory(const QString& filePath);
    static LocalFileSelectionResult selectTransferFile(const QString& selectedPath,
                                                       const QString& confirmKind);
    static LocalFileSelectionResult selectAvatarFile(const QString& selectedPath);
    static QString safeReceivedFileName(const QString& rawName, const QString& fallbackName);
    static QString ensureReceivedDownloadDirectory(const QString& downloadSubdir);
    static QString uniqueReceivedSavePath(const QString& directoryPath, const QString& fileName);
    static bool writeReceivedTransferPayload(const QString& savePath, const QByteArray& fileData);
    static LocalFileValidationResult validateTransferFile(const QFileInfo& info, const QString& kind);
    static LocalFileValidationResult validateAvatarFile(const QFileInfo& info);
    static QString humanFileSize(qint64 bytes);
};

#endif // LOCALFILEMANAGER_H
