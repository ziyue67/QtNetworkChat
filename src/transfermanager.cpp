#include "transfermanager.h"

#include "filetransferstatus.h"

#include <QFileInfo>

namespace {
QString savedTransferFileName(const QJsonObject& state) {
    const QFileInfo info(state.value(QStringLiteral("filePath")).toString());
    return info.fileName().isEmpty() ? QStringLiteral("未命名文件") : info.fileName();
}

QString savedTransferTargetName(const QJsonObject& state) {
    const QString receiverId = state.value(QStringLiteral("receiverId")).toString().trimmed();
    return receiverId.isEmpty() ? QStringLiteral("公共聊天室") : QStringLiteral("QQ:%1").arg(receiverId);
}
}

TransferRecoveryUiState TransferManager::recoveryUiState(bool hasSavedTransfer,
                                                         bool clientConnected,
                                                         const QJsonObject& state,
                                                         const QJsonObject& recoveryStatus,
                                                         bool announce) {
    TransferRecoveryUiState result;
    result.hasSavedTransfer = hasSavedTransfer;
    result.canAutoResume = recoveryStatus.value(QStringLiteral("canAutoResume")).toBool(false);
    result.resumeVisible = hasSavedTransfer;
    result.resumeEnabled = hasSavedTransfer && clientConnected && result.canAutoResume;
    result.clearVisible = hasSavedTransfer;
    result.clearEnabled = hasSavedTransfer;

    if (!hasSavedTransfer) {
        result.resumeToolTip = QStringLiteral("暂无可恢复的未完成发送");
        result.clearToolTip = QStringLiteral("暂无可清除的恢复记录");
        return result;
    }

    result.fileName = savedTransferFileName(state);
    result.targetName = savedTransferTargetName(state);
    result.detail = QStringLiteral("检测到未完成发送：%1 -> %2").arg(result.fileName, result.targetName);
    result.e2eFileEncrypted = recoveryStatus.value(QStringLiteral("e2eFileEncrypted")).toBool(false);
    result.recoveryMode = recoveryStatus.value(QStringLiteral("recoveryMode")).toString();
    result.recoveryReason = recoveryStatus.value(QStringLiteral("reason")).toString();
    result.resumeToolTip = result.canAutoResume
        ? result.detail
        : result.detail + QStringLiteral("（需要重新发送，原因：%1）").arg(result.recoveryReason);
    result.clearToolTip = QStringLiteral("清除恢复记录：") + result.detail;

    if (announce) {
        if (result.canAutoResume) {
            result.announceMessage = result.detail + QStringLiteral("，可通过菜单“恢复未完成发送”继续，或清除恢复记录。");
            result.statusMessage = QStringLiteral("可恢复未完成发送：") + result.fileName;
        } else {
            result.announceMessage = result.detail
                + QStringLiteral("，%1文件恢复策略为“重新发送”，不会自动续传；可重新选择文件发送或清除恢复记录。")
                    .arg(result.e2eFileEncrypted ? QStringLiteral("端到端加密") : result.recoveryMode);
            result.statusMessage = QStringLiteral("未完成发送需要重新发送：") + result.fileName;
        }
    }
    return result;
}

TransferStatusEvent TransferManager::statusEvent(const QString& fileName,
                                                 const QString& transferId,
                                                 const QString& reason,
                                                 qint64 receivedBytes,
                                                 qint64 totalBytes) {
    TransferStatusEvent result;
    result.message = fileTransferStatusEventMessage(fileName, transferId, reason, receivedBytes, totalBytes);
    result.diagnostic = fileTransferStatusDiagnostic(fileName, transferId, reason, receivedBytes, totalBytes);
    result.copyActionVisible = true;
    result.copyActionEnabled = true;
    result.copyActionToolTip = QStringLiteral("复制最近一次文件传输准备、续传、完成、失败或离线兜底状态诊断");
    return result;
}
