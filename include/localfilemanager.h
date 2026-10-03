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
    bool warningCanceled = false;
    QString filePath;
    QFileInfo fileInfo;
    QString fileSize;
    QString statusMessage;
    QString failureTitle;
    QString failureMessage;
    QString warningTitle;
    QString warningMessage;
    QString canceledHint;
    QString canceledStatusMessage;
    int canceledStatusTimeoutMs = 1600;
    QString rejectedStatusMessage;
    int rejectedStatusTimeoutMs = 2600;
    QString warningCanceledHint;
    QString warningCanceledStatusMessage;
    int warningCanceledStatusTimeoutMs = 2600;
    QString invalidDataTitle;
    QString invalidDataMessage;
    QString invalidDataStatusMessage;
    int invalidDataStatusTimeoutMs = 2200;
    QString saveFailedTitle;
    QString saveFailedMessage;
    QString saveFailedStatusMessage;
    int saveFailedStatusTimeoutMs = 2600;
};

struct LocalTransferSelectionDecision {
    enum class Action {
        None,
        ShowFailureDialog,
        ConfirmLargeFile
    };

    bool accepted = false;
    Action action = Action::None;
    QString filePath;
    QFileInfo fileInfo;
    QString fileSize;
    QString hintText;
    QString statusMessage;
    int statusTimeoutMs = 0;
    QString dialogTitle;
    QString dialogMessage;
};

struct LocalSavedFileState {
    QString savePath;
    QFileInfo fileInfo;
    QFileInfo folderInfo;
    bool hasSavePath = false;
    bool canOpenFile = false;
    bool canOpenFolder = false;
};

struct LocalReceivedTransferPlan {
    QString receivedName;
    QString receivedSize;
    QString savePath;
};

struct LocalAvatarSelectionPlan {
    QString dialogTitle;
    QString filters;
};

struct LocalAvatarAppliedState {
    QString detail;
    QString toolTip;
    int statusTimeoutMs = 2600;
};

class LocalFileManager {
public:
    static QString receivedDownloadRootDirectory();
    static void setReceivedDownloadRootDirectory(const QString& directoryPath);
    static void resetReceivedDownloadRootDirectory();
    static QString lastTransferDirectory();
    static void rememberTransferDirectory(const QString& filePath);
    static QString lastAvatarDirectory();
    static void rememberAvatarDirectory(const QString& filePath);
    static LocalFileSelectionResult selectTransferFile(const QString& selectedPath,
                                                       const QString& confirmKind);
    static LocalTransferSelectionDecision transferSelectionDecision(const QString& selectedPath,
                                                                    const QString& confirmKind,
                                                                    const QString& canceledHint,
                                                                    const QString& canceledStatus);
    static LocalTransferSelectionDecision resolveTransferSelectionWarning(const LocalTransferSelectionDecision& pendingDecision,
                                                                          bool confirmed,
                                                                          const QString& confirmKind);
    static LocalAvatarSelectionPlan avatarSelectionPlan();
    static LocalFileSelectionResult selectAvatarFile(const QString& selectedPath);
    static LocalAvatarAppliedState avatarAppliedState(const QFileInfo& info);
    static LocalFileSelectionResult cancelTransferSelection(const QString& kind);
    static LocalFileSelectionResult cancelTransferWarningSelection(const QString& kind);
    static LocalFileSelectionResult cancelAvatarSelection();
    static LocalFileSelectionResult invalidAvatarDataResult();
    static LocalFileSelectionResult avatarSaveFailedResult();
    static QString safeReceivedFileName(const QString& rawName, const QString& fallbackName);
    static LocalReceivedTransferPlan receivedTransferPlan(const QString& rawName,
                                                          const QString& fallbackName,
                                                          qint64 receivedBytes,
                                                          const QString& downloadSubdir);
    static QString ensureReceivedDownloadDirectory(const QString& downloadSubdir);
    static QString uniqueReceivedSavePath(const QString& directoryPath, const QString& fileName);
    static bool writeReceivedTransferPayload(const QString& savePath, const QByteArray& fileData);
    static QString extractSavePathFromChatText(const QString& text);
    static LocalSavedFileState savedFileStateFromChatText(const QString& text,
                                                          const QString& toolTipText = QString());
    static LocalFileValidationResult validateTransferFile(const QFileInfo& info, const QString& kind);
    static LocalFileValidationResult validateAvatarFile(const QFileInfo& info);
    static QString humanFileSize(qint64 bytes);
};

#endif // LOCALFILEMANAGER_H
