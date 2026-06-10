#ifndef TRANSFERMANAGER_H
#define TRANSFERMANAGER_H

#include <QJsonObject>
#include <QString>

struct TransferActionUiState {
    bool visible = false;
    bool enabled = false;
    QString toolTip;
};

struct TransferDiagnosticCopyUiState {
    TransferActionUiState action;
    QString clipboardText;
    QString emptyStatusMessage;
    QString copiedStatusMessage;
};

struct TransferClearRecoveryPrompt {
    QString fileName;
    QString title;
    QString message;
    QString noSavedStatusMessage;
    QString keptStatusMessage;
    QString clearedSystemMessage;
    QString clearedStatusMessage;
    QString clearFailedSystemMessage;
    QString clearFailedStatusMessage;
};

struct TransferResumeBlockedPrompt {
    QString fileName;
    QString targetName;
    QString reason;
    QString title;
    QString message;
    QString systemMessage;
    QString hintText;
    QString statusMessage;
    QString clearedSystemMessage;
    QString clearedHintText;
    QString clearedStatusMessage;
};

struct TransferResumeResultState {
    bool succeeded = false;
    bool canceled = false;
    bool failed = false;
    QString fileName;
    QString targetName;
    QString reason;
    QString systemMessage;
    QString hintText;
    QString statusMessage;
    QString failureTitle;
    QString failureMessage;
    QString clearedSystemMessage;
    QString clearedHintText;
    QString clearedStatusMessage;
};

struct TransferRecoveryUiState {
    bool hasSavedTransfer = false;
    bool canAutoResume = false;
    bool resumeVisible = false;
    bool resumeEnabled = false;
    bool clearVisible = false;
    bool clearEnabled = false;
    bool e2eFileEncrypted = false;
    QString recoveryMode;
    QString recoveryReason;
    QString fileName;
    QString targetName;
    QString detail;
    QString resumeToolTip;
    QString clearToolTip;
    QString announceMessage;
    QString statusMessage;
    TransferActionUiState resumeAction;
    TransferActionUiState clearAction;
};

struct TransferStatusEvent {
    QString message;
    QString chatHintText;
    QString statusBarMessage;
    QString actionHint;
    QString diagnostic;
    int statusBarTimeoutMs = 4200;
    bool copyActionVisible = false;
    bool copyActionEnabled = false;
    QString copyActionToolTip;
    TransferDiagnosticCopyUiState copyDiagnostic;
};

struct TransferProgressUiState {
    QString labelText;
    QString manifestSummary;
    int percent = 0;
};

struct TransferSendUiState {
    QString hintText;
    QString statusMessage;
    QString systemMessage;
    QString cardText;
    QString receiptText;
    QString warningTitle;
    QString warningMessage;
    int statusTimeoutMs = 3000;
};

class TransferManager {
public:
    static TransferRecoveryUiState recoveryUiState(bool hasSavedTransfer,
                                                   bool clientConnected,
                                                   const QJsonObject& state,
                                                   const QJsonObject& recoveryStatus,
                                                   bool announce = false);
    static TransferStatusEvent statusEvent(const QString& fileName,
                                           const QString& transferId,
                                           const QString& reason,
                                           qint64 receivedBytes = 0,
                                           qint64 totalBytes = 0);
    static TransferDiagnosticCopyUiState diagnosticCopyUiState(const QString& diagnostic);
    static TransferClearRecoveryPrompt clearRecoveryPrompt(const QJsonObject& state);
    static TransferResumeBlockedPrompt resumeBlockedPrompt(const QJsonObject& state,
                                                           const QJsonObject& recoveryStatus);
    static TransferResumeResultState resumeResultState(const QString& fileName,
                                                       const QString& targetName,
                                                       bool resumed,
                                                       bool canceled,
                                                       const QString& rejectReason);
    static TransferProgressUiState sendingInitialState(const QString& kind,
                                                       const QString& fileName,
                                                       const QString& targetName);
    static TransferProgressUiState sendingCancelState(const QString& kind,
                                                      const QString& fileName);
    static TransferProgressUiState sendingProgressState(const QString& kind,
                                                        const QString& fileName,
                                                        const QString& targetName,
                                                        qint64 bytesPrepared,
                                                        qint64 totalBytes);
    static TransferProgressUiState sendingPreparedState(const QString& kind,
                                                        const QString& fileName,
                                                        const QString& targetName,
                                                        qint64 totalBytes,
                                                        qint64 chunkSize,
                                                        qint64 chunkCount,
                                                        const QString& fileHash);
    static TransferSendUiState publicGroupRemovedState(const QString& kind);
    static TransferSendUiState disconnectedSendState(const QString& kind, const QString& targetName);
    static TransferSendUiState canceledSendState(const QString& kind, const QString& fileName);
    static TransferSendUiState failedSendState(const QString& kind,
                                               const QString& fileName,
                                               const QString& fileSize,
                                               const QString& targetName);
    static TransferSendUiState preparingSendState(const QString& kind,
                                                  const QString& fileName,
                                                  const QString& fileSize,
                                                  const QString& targetName);
    static TransferSendUiState localSendCompletedState(const QString& kind,
                                                       const QString& fileName,
                                                       const QString& fileSize,
                                                       const QString& targetName,
                                                       const QString& completedAt);
    static TransferSendUiState remoteSendCompletedState(const QString& kind,
                                                        const QString& fileName,
                                                        const QString& fileSize,
                                                        const QString& targetName,
                                                        const QString& completedAt,
                                                        const QString& transferSummary = QString());
    static TransferProgressUiState resumeInitialState(const QString& fileName,
                                                      const QString& targetName);
    static TransferProgressUiState resumeCancelState(const QString& fileName);
    static TransferProgressUiState resumeProgressState(const QString& fileName,
                                                       const QString& targetName,
                                                       qint64 bytesPrepared,
                                                       qint64 totalBytes);
    static TransferProgressUiState resumePreparedState(const QString& fileName,
                                                       const QString& targetName,
                                                       qint64 totalBytes,
                                                       qint64 chunkSize,
                                                       qint64 chunkCount,
                                                       const QString& fileHash);
};

#endif // TRANSFERMANAGER_H
