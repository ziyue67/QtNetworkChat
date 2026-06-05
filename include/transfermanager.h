#ifndef TRANSFERMANAGER_H
#define TRANSFERMANAGER_H

#include <QJsonObject>
#include <QString>

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
};

struct TransferStatusEvent {
    QString message;
    QString diagnostic;
    bool copyActionVisible = false;
    bool copyActionEnabled = false;
    QString copyActionToolTip;
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
};

#endif // TRANSFERMANAGER_H
