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
    QString diagnostic;
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
