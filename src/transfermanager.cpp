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

QString transferActionHint(const FileTransferStatusInfo& info) {
    if (info.category == QLatin1String("completed")
            || info.category == QLatin1String("receive-completed")
            || info.category == QLatin1String("receive-saved")) {
        return QStringLiteral("文件状态已完成，无需处理。");
    }
    if (info.category == QLatin1String("prepared")
            || info.category == QLatin1String("receive-started")
            || info.category == QLatin1String("resumed")) {
        return QStringLiteral("保持窗口在线，等待后续分片确认或完成回执。");
    }
    if (info.category == QLatin1String("fallback-retained")
            || info.category == QLatin1String("receiver-disconnected")) {
        return QStringLiteral("可等待对方重新上线自动回放；若长时间未送达，再重新发送。");
    }
    if (info.category == QLatin1String("object-readback-unavailable")
            || info.category == QLatin1String("object-write-failed")) {
        return QStringLiteral("保留诊断并稍后重试；若对象存储持续异常，请重新发送或切回普通文件路径。");
    }
    if (info.category == QLatin1String("receive-save-failed")) {
        return QStringLiteral("请检查下载目录权限、磁盘空间或安全软件拦截后重试。");
    }
    if (info.category == QLatin1String("receive-open-failed")) {
        return QStringLiteral("请从聊天记录复制保存路径后手动检查文件是否仍存在。");
    }
    if (info.category == QLatin1String("chunk-delivery-failed")
            || info.category == QLatin1String("timeout")
            || info.category == QLatin1String("retryable")) {
        return QStringLiteral("可稍后重试；客户端会优先保留续传或离线兜底证据。");
    }
    if (info.category == QLatin1String("integrity")
            || info.category == QLatin1String("missing")) {
        return QStringLiteral("为避免错误文件送达，请重新选择原文件发送。");
    }
    if (info.category == QLatin1String("size")) {
        return QStringLiteral("请确认文件未被修改，且大小在服务端允许范围内。");
    }
    if (info.category == QLatin1String("auth")) {
        return QStringLiteral("请确认账号、群成员身份或会话权限后再发送。");
    }
    return info.retryable
        ? QStringLiteral("可稍后重试；必要时复制诊断给值班人员。")
        : QStringLiteral("请复制诊断并根据原因重新发送或联系值班人员。");
}

int transferStatusTimeout(const FileTransferStatusInfo& info) {
    if (info.category == QLatin1String("completed")
            || info.category == QLatin1String("receive-completed")
            || info.category == QLatin1String("receive-saved")) {
        return 2600;
    }
    if (info.retryable) {
        return 5200;
    }
    return 4600;
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
    const FileTransferStatusInfo info = describeFileTransferReason(reason);
    result.message = fileTransferStatusEventMessage(fileName, transferId, reason, receivedBytes, totalBytes);
    result.actionHint = transferActionHint(info);
    result.chatHintText = QStringLiteral("%1 · %2").arg(info.title, result.actionHint);
    result.statusBarMessage = result.message + QStringLiteral(" · ") + result.actionHint;
    result.statusBarTimeoutMs = transferStatusTimeout(info);
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

TransferClearRecoveryPrompt TransferManager::clearRecoveryPrompt(const QJsonObject& state) {
    TransferClearRecoveryPrompt result;
    result.fileName = savedTransferFileName(state);
    result.title = QStringLiteral("清除恢复记录");
    result.message = QStringLiteral("清除“%1”的未完成发送恢复记录？\n清除后不会删除本地文件，但需要重新手动发送。")
        .arg(result.fileName);
    result.noSavedStatusMessage = QStringLiteral("暂无可清除的恢复记录");
    result.keptStatusMessage = QStringLiteral("已保留恢复记录：") + result.fileName;
    result.clearedSystemMessage = QStringLiteral("已清除未完成发送恢复记录：") + result.fileName;
    result.clearedStatusMessage = QStringLiteral("已清除恢复记录：") + result.fileName;
    result.clearFailedSystemMessage = QStringLiteral("清除未完成发送恢复记录失败：") + result.fileName;
    result.clearFailedStatusMessage = QStringLiteral("清除恢复记录失败：") + result.fileName;
    return result;
}

TransferResumeBlockedPrompt TransferManager::resumeBlockedPrompt(const QJsonObject& state,
                                                                 const QJsonObject& recoveryStatus) {
    TransferResumeBlockedPrompt result;
    result.fileName = savedTransferFileName(state);
    result.targetName = savedTransferTargetName(state);
    result.reason = recoveryStatus.value(QStringLiteral("reason")).toString(QStringLiteral("saved-transfer-requires-resend"));
    result.title = QStringLiteral("需要重新发送");
    result.systemMessage = QStringLiteral("未完成发送不能自动续传：%1 -> %2（%3）。请重新发送该文件，或清除恢复记录。")
        .arg(result.fileName, result.targetName, result.reason);
    result.hintText = QStringLiteral("未完成发送需重新发送 · %1").arg(result.fileName);
    result.statusMessage = QStringLiteral("端到端加密文件需重新发送：") + result.fileName;
    result.message = QStringLiteral("文件：%1\n目标：%2\n原因：%3\n\n该未完成发送不会自动续传。请重新选择文件发送；也可以现在清除恢复记录。")
        .arg(result.fileName, result.targetName, result.reason);
    result.clearedSystemMessage = QStringLiteral("已清除未完成发送恢复记录：") + result.fileName;
    result.clearedHintText = QStringLiteral("已清除未完成发送恢复记录 · ") + result.fileName;
    result.clearedStatusMessage = QStringLiteral("已清除恢复记录：") + result.fileName;
    return result;
}

TransferResumeResultState TransferManager::resumeResultState(const QString& fileName,
                                                             const QString& targetName,
                                                             bool resumed,
                                                             bool canceled,
                                                             const QString& rejectReason) {
    TransferResumeResultState result;
    result.succeeded = resumed;
    result.canceled = !resumed && canceled;
    result.failed = !resumed && !canceled;
    result.fileName = fileName;
    result.targetName = targetName;
    result.reason = rejectReason.isEmpty() ? QStringLiteral("恢复失败") : rejectReason;
    if (result.succeeded) {
        result.systemMessage = QStringLiteral("已恢复并完成未完成发送：%1 -> %2").arg(fileName, targetName);
        result.hintText = QStringLiteral("未完成发送已恢复 · %1 · %2").arg(fileName, targetName);
        result.statusMessage = QStringLiteral("未完成发送已恢复完成：") + fileName;
        return result;
    }
    if (result.canceled) {
        result.systemMessage = QStringLiteral("已取消恢复未完成发送：") + fileName;
        result.hintText = QStringLiteral("已取消恢复未完成发送 · ") + fileName;
        result.statusMessage = QStringLiteral("已取消恢复发送：") + fileName;
        return result;
    }
    result.systemMessage = QStringLiteral("恢复未完成发送失败：%1 -> %2（%3）。恢复记录已保留，可稍后重试。")
        .arg(fileName, targetName, result.reason);
    result.hintText = QStringLiteral("恢复未完成发送失败 · %1 · %2").arg(fileName, result.reason);
    result.statusMessage = QStringLiteral("恢复未完成发送失败：") + result.reason;
    result.failureTitle = QStringLiteral("恢复未完成发送失败");
    result.failureMessage = QStringLiteral("文件：%1\n目标：%2\n原因：%3\n\n恢复记录已保留，可稍后通过菜单“恢复未完成发送”重试；也可以现在清除这条恢复记录。")
        .arg(fileName, targetName, result.reason);
    result.clearedSystemMessage = QStringLiteral("已清除未完成发送恢复记录：") + fileName;
    result.clearedHintText = QStringLiteral("已清除未完成发送恢复记录 · ") + fileName;
    result.clearedStatusMessage = QStringLiteral("已清除恢复记录：") + fileName;
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

TransferSendUiState TransferManager::publicGroupRemovedState(const QString& kind) {
    TransferSendUiState result;
    result.hintText = QStringLiteral("%1发送暂停 · 当前账号已不在公共群，等待重新邀请").arg(kind);
    result.statusMessage = QStringLiteral("当前账号已不在公共群，暂不能发送%1").arg(kind);
    result.statusTimeoutMs = 3000;
    return result;
}

TransferSendUiState TransferManager::disconnectedSendState(const QString& kind, const QString& targetName) {
    TransferSendUiState result;
    const QString safeTarget = targetName.trimmed().isEmpty() ? QStringLiteral("公共聊天室") : targetName.trimmed();
    result.hintText = QStringLiteral("%1发送暂停 · %2 已断开").arg(kind, safeTarget);
    result.statusMessage = QStringLiteral("已断开连接，暂不能发送%1到 %2").arg(kind, safeTarget);
    result.statusTimeoutMs = 3000;
    return result;
}

TransferSendUiState TransferManager::canceledSendState(const QString& kind, const QString& fileName) {
    TransferSendUiState result;
    result.hintText = QStringLiteral("已取消发送%1 · %2").arg(kind, fileName);
    result.statusMessage = QStringLiteral("已取消发送%1：%2").arg(kind, fileName);
    result.statusTimeoutMs = 2200;
    return result;
}

TransferSendUiState TransferManager::failedSendState(const QString& kind,
                                                     const QString& fileName,
                                                     const QString& fileSize,
                                                     const QString& targetName) {
    TransferSendUiState result;
    result.hintText = QStringLiteral("%1发送失败 · %2 · %3").arg(kind, fileName, targetName);
    result.statusMessage = QStringLiteral("%1发送失败：%2").arg(kind, fileName);
    result.warningTitle = QStringLiteral("发送失败");
    result.warningMessage = QStringLiteral("%1“%2”（%3）未发送到 %4，请检查连接状态或稍后重试。")
        .arg(kind, fileName, fileSize, targetName);
    result.statusTimeoutMs = 3000;
    return result;
}

TransferSendUiState TransferManager::preparingSendState(const QString& kind,
                                                        const QString& fileName,
                                                        const QString& fileSize,
                                                        const QString& targetName) {
    TransferSendUiState result;
    result.hintText = QStringLiteral("准备发送%1到 %2 · %3 · %4").arg(kind, targetName, fileName, fileSize);
    result.statusMessage = result.hintText;
    result.statusTimeoutMs = 1800;
    return result;
}

TransferSendUiState TransferManager::localSendCompletedState(const QString& kind,
                                                             const QString& fileName,
                                                             const QString& fileSize,
                                                             const QString& targetName,
                                                             const QString& completedAt) {
    TransferSendUiState result;
    result.systemMessage = QStringLiteral("%1发送详情：%2 · %3 · 到 %4").arg(kind, fileName, fileSize, targetName);
    result.cardText = QStringLiteral("%1卡片 · %2 · %3 · 已发送到 %4").arg(kind, fileName, fileSize, targetName);
    result.receiptText = QStringLiteral("%1查收话术 · 我已发送%1 %2 到 %3，请注意查收。 · 右键聊天记录可复制")
        .arg(kind, fileName, targetName);
    result.hintText = QStringLiteral("已发送%1到 %2 · %3 · %4").arg(kind, targetName, fileSize, completedAt);
    result.statusMessage = QStringLiteral("已发送%1到 %2 · %3").arg(kind, targetName, fileSize);
    result.statusTimeoutMs = 2200;
    return result;
}

TransferSendUiState TransferManager::remoteSendCompletedState(const QString& kind,
                                                              const QString& fileName,
                                                              const QString& fileSize,
                                                              const QString& targetName,
                                                              const QString& completedAt,
                                                              const QString& transferSummary) {
    TransferSendUiState result;
    const QString transferSuffix = transferSummary.trimmed().isEmpty()
        ? QString()
        : QStringLiteral(" · %1").arg(transferSummary.trimmed());
    result.systemMessage = QStringLiteral("已发送%1: %2 · %3 · 到 %4%5").arg(kind, fileName, fileSize, targetName, transferSuffix);
    result.cardText = QStringLiteral("%1卡片 · %2 · %3 · 已发送到 %4%5").arg(kind, fileName, fileSize, targetName, transferSuffix);
    result.receiptText = QStringLiteral("%1查收话术 · 我已发送%1 %2 到 %3，请注意查收。 · 右键聊天记录可复制")
        .arg(kind, fileName, targetName);
    result.hintText = QStringLiteral("已发送%1到 %2 · %3 · %4%5").arg(kind, targetName, fileSize, completedAt, transferSuffix);
    result.statusMessage = QStringLiteral("已发送%1到 %2 · %3%4").arg(kind, targetName, fileSize, transferSuffix);
    result.statusTimeoutMs = 2600;
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
