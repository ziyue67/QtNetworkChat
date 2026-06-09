#include "transfermanager.h"

#include "filetransferstatus.h"

#include <QFileInfo>
#include <QStringList>
#include <QtGlobal>

namespace {
QString savedTransferFileName(const QJsonObject& state) {
    const QFileInfo info(state.value(QStringLiteral("filePath")).toString());
    return info.fileName().isEmpty() ? QStringLiteral("未命名文件") : info.fileName();
}

QString savedTransferTargetName(const QJsonObject& state) {
    const QString receiverId = state.value(QStringLiteral("receiverId")).toString().trimmed();
    return receiverId.isEmpty() ? QStringLiteral("公共聊天室") : QStringLiteral("QQ:%1").arg(receiverId);
}

QString humanTransferSize(qint64 bytes) {
    const double size = static_cast<double>(bytes);
    if (size < 1024.0) return QStringLiteral("%1 B").arg(bytes);
    if (size < 1024.0 * 1024.0) return QStringLiteral("%1 KB").arg(size / 1024.0, 0, 'f', 1);
    if (size < 1024.0 * 1024.0 * 1024.0) return QStringLiteral("%1 MB").arg(size / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(size / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

QString transferManifestSummary(qint64 totalBytes, qint64 chunkSize, qint64 chunkCount, const QString& fileHash) {
    QStringList parts;
    if (chunkCount > 0) {
        parts << QStringLiteral("%1片").arg(chunkCount);
    }
    if (chunkSize > 0) {
        parts << QStringLiteral("分片%1").arg(humanTransferSize(chunkSize));
    }
    const QString trimmedHash = fileHash.trimmed();
    if (!trimmedHash.isEmpty()) {
        parts << QStringLiteral("SHA-256 %1").arg(trimmedHash.left(12));
    }
    if (parts.isEmpty() && totalBytes > 0) {
        parts << humanTransferSize(totalBytes);
    }
    return parts.join(QStringLiteral(" · "));
}

int transferPercent(qint64 bytesPrepared, qint64 totalBytes) {
    return totalBytes > 0
        ? qBound(0, static_cast<int>((bytesPrepared * 100) / totalBytes), 100)
        : 0;
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
    result.resumeAction.visible = result.resumeVisible;
    result.resumeAction.enabled = result.resumeEnabled;
    result.clearAction.visible = result.clearVisible;
    result.clearAction.enabled = result.clearEnabled;

    if (!hasSavedTransfer) {
        result.resumeToolTip = QStringLiteral("暂无可恢复的未完成发送");
        result.clearToolTip = QStringLiteral("暂无可清除的恢复记录");
        result.resumeAction.toolTip = result.resumeToolTip;
        result.clearAction.toolTip = result.clearToolTip;
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
    result.resumeAction.toolTip = result.resumeToolTip;
    result.clearAction.toolTip = result.clearToolTip;

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
    result.copyDiagnostic = diagnosticCopyUiState(result.diagnostic);
    return result;
}

TransferDiagnosticCopyUiState TransferManager::diagnosticCopyUiState(const QString& diagnostic) {
    TransferDiagnosticCopyUiState result;
    result.clipboardText = diagnostic.trimmed();
    result.action.visible = true;
    result.action.enabled = !result.clipboardText.isEmpty();
    result.action.toolTip = QStringLiteral("复制最近一次文件传输准备、续传、完成、失败或离线兜底状态诊断");
    result.emptyStatusMessage = QStringLiteral("暂无可复制的文件状态诊断");
    result.copiedStatusMessage = QStringLiteral("最近文件状态诊断已复制");
    return result;
}

TransferProgressUiState TransferManager::sendingInitialState(const QString& kind,
                                                             const QString& fileName,
                                                             const QString& targetName) {
    TransferProgressUiState result;
    result.labelText = QStringLiteral("正在分片读取%1...\n%2 -> %3").arg(kind, fileName, targetName);
    return result;
}

TransferProgressUiState TransferManager::sendingCancelState(const QString& kind,
                                                            const QString& fileName) {
    TransferProgressUiState result;
    result.labelText = QStringLiteral("正在取消%1发送...\n%2").arg(kind, fileName);
    return result;
}

TransferProgressUiState TransferManager::sendingProgressState(const QString& kind,
                                                              const QString& fileName,
                                                              const QString& targetName,
                                                              qint64 bytesPrepared,
                                                              qint64 totalBytes) {
    TransferProgressUiState result;
    result.percent = transferPercent(bytesPrepared, totalBytes);
    result.labelText = QStringLiteral("正在分片发送%1到 %2\n%3 · %4 / %5")
        .arg(kind,
             targetName,
             fileName,
             humanTransferSize(bytesPrepared),
             humanTransferSize(totalBytes));
    return result;
}

TransferProgressUiState TransferManager::sendingPreparedState(const QString& kind,
                                                              const QString& fileName,
                                                              const QString& targetName,
                                                              qint64 totalBytes,
                                                              qint64 chunkSize,
                                                              qint64 chunkCount,
                                                              const QString& fileHash) {
    TransferProgressUiState result;
    result.manifestSummary = transferManifestSummary(totalBytes, chunkSize, chunkCount, fileHash);
    result.labelText = QStringLiteral("%1校验清单已生成\n%2 -> %3\n%4")
        .arg(kind, fileName, targetName, result.manifestSummary);
    return result;
}

TransferProgressUiState TransferManager::resumeInitialState(const QString& fileName,
                                                            const QString& targetName) {
    TransferProgressUiState result;
    result.labelText = QStringLiteral("正在恢复发送\n%1 -> %2").arg(fileName, targetName);
    return result;
}

TransferProgressUiState TransferManager::resumeCancelState(const QString& fileName) {
    TransferProgressUiState result;
    result.labelText = QStringLiteral("正在取消恢复发送...\n") + fileName;
    return result;
}

TransferProgressUiState TransferManager::resumeProgressState(const QString& fileName,
                                                             const QString& targetName,
                                                             qint64 bytesPrepared,
                                                             qint64 totalBytes) {
    TransferProgressUiState result;
    result.percent = transferPercent(bytesPrepared, totalBytes);
    result.labelText = QStringLiteral("正在恢复发送\n%1 -> %2\n%3 / %4")
        .arg(fileName,
             targetName,
             humanTransferSize(bytesPrepared),
             humanTransferSize(totalBytes));
    return result;
}

TransferProgressUiState TransferManager::resumePreparedState(const QString& fileName,
                                                             const QString& targetName,
                                                             qint64 totalBytes,
                                                             qint64 chunkSize,
                                                             qint64 chunkCount,
                                                             const QString& fileHash) {
    TransferProgressUiState result;
    result.manifestSummary = transferManifestSummary(totalBytes, chunkSize, chunkCount, fileHash);
    result.labelText = QStringLiteral("恢复发送校验清单已生成\n%1 -> %2\n%3")
        .arg(fileName, targetName, result.manifestSummary);
    return result;
}
