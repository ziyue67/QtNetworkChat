#include "transfermanager.h"

#include "filetransferstatus.h"
#include "localfilemanager.h"

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
    result.category = info.category;
    result.title = info.title;
    result.detail = info.detail;
    result.retryable = info.retryable;
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
    result.workspaceTitle = QStringLiteral("文件工作区 · 发送暂停");
    result.workspaceDetail = QStringLiteral("当前账号已不在公共群，%1发送先暂停。需要重新加入公共群后再继续。").arg(kind);
    result.statusTone = QStringLiteral("warning");
    result.statusTimeoutMs = 3000;
    return result;
}

TransferSendUiState TransferManager::disconnectedSendState(const QString& kind, const QString& targetName) {
    TransferSendUiState result;
    const QString safeTarget = targetName.trimmed().isEmpty() ? QStringLiteral("公共聊天室") : targetName.trimmed();
    result.hintText = QStringLiteral("%1发送暂停 · %2 已断开").arg(kind, safeTarget);
    result.statusMessage = QStringLiteral("已断开连接，暂不能发送%1到 %2").arg(kind, safeTarget);
    result.workspaceTitle = QStringLiteral("文件工作区 · 等待连接");
    result.workspaceDetail = QStringLiteral("%1发送还没开始，当前无法连接到 %2。恢复连接后可重新发送。").arg(kind, safeTarget);
    result.statusTone = QStringLiteral("warning");
    result.statusTimeoutMs = 3000;
    return result;
}

TransferSendUiState TransferManager::canceledSendState(const QString& kind, const QString& fileName) {
    TransferSendUiState result;
    result.hintText = QStringLiteral("已取消发送%1 · %2").arg(kind, fileName);
    result.statusMessage = QStringLiteral("已取消发送%1：%2").arg(kind, fileName);
    result.workspaceTitle = QStringLiteral("文件工作区 · 已取消");
    result.workspaceDetail = QStringLiteral("%1“%2”已取消发送。可以重新选择文件发送，或查看最近诊断确认是否留下恢复记录。").arg(kind, fileName);
    result.statusTone = QStringLiteral("warning");
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
    result.workspaceTitle = QStringLiteral("文件工作区 · 发送失败");
    result.workspaceDetail = QStringLiteral("%1“%2”发送到 %3 失败。建议先复制最近诊断，再决定立即重试、稍后恢复，或重新发送。")
        .arg(kind, fileName, targetName);
    result.statusTone = QStringLiteral("danger");
    result.statusTimeoutMs = 3000;
    return result;
}

TransferSelectionPlan TransferManager::fileSelectionPlan() {
    TransferSelectionPlan result;
    result.dialogTitle = QStringLiteral("选择文件");
    result.filters = QStringLiteral("常用文件 (*.txt *.pdf *.doc *.docx *.xls *.xlsx *.zip *.rar *.7z);;媒体文件 (*.png *.jpg *.jpeg *.gif *.bmp *.mp4 *.mov *.avi *.mkv *.wmv *.flv *.webm);;所有文件 (*.*)");
    result.confirmKind = QStringLiteral("文件");
    result.canceledHint = QStringLiteral("文件发送已取消");
    result.canceledStatus = QStringLiteral("已取消选择文件");
    result.preparingKind = QStringLiteral("文件");
    return result;
}

TransferSelectionPlan TransferManager::mediaSelectionPlan() {
    TransferSelectionPlan result;
    result.dialogTitle = QStringLiteral("选择图片或视频");
    result.filters = QStringLiteral("图片和视频 (*.png *.jpg *.jpeg *.bmp *.gif *.mp4 *.mov *.avi *.mkv *.wmv *.flv *.webm);;图片文件 (*.png *.jpg *.jpeg *.bmp *.gif);;视频文件 (*.mp4 *.mov *.avi *.mkv *.wmv *.flv *.webm);;所有文件 (*.*)");
    result.confirmKind = QStringLiteral("媒体文件");
    result.canceledHint = QStringLiteral("图片/视频发送已取消");
    result.canceledStatus = QStringLiteral("已取消选择图片/视频");
    result.preparingKind = QStringLiteral("媒体文件");
    return result;
}

TransferSelectionUiState TransferManager::transferSelectionUiState(const TransferSelectionPlan& selectionPlan,
                                                                   const QString& selectedPath) {
    TransferSelectionUiState state;
    state.confirmKind = selectionPlan.confirmKind;

    const LocalTransferSelectionDecision decision =
        LocalFileManager::transferSelectionDecision(selectedPath,
                                                    selectionPlan.confirmKind,
                                                    selectionPlan.canceledHint,
                                                    selectionPlan.canceledStatus);
    if (decision.action == LocalTransferSelectionDecision::Action::ShowFailureDialog) {
        state.showFailureDialog = true;
        state.hintText = decision.hintText;
        state.statusMessage = decision.statusMessage;
        state.statusTimeoutMs = decision.statusTimeoutMs;
        state.dialogTitle = decision.dialogTitle;
        state.dialogMessage = decision.dialogMessage;
        return state;
    }

    if (!decision.accepted
        && decision.action != LocalTransferSelectionDecision::Action::ConfirmLargeFile) {
        state.hintText = decision.hintText;
        state.statusMessage = decision.statusMessage;
        state.statusTimeoutMs = decision.statusTimeoutMs;
        return state;
    }

    state.filePath = decision.filePath;
    state.fileInfo = decision.fileInfo;
    state.fileSize = decision.fileSize;
    if (decision.action == LocalTransferSelectionDecision::Action::ConfirmLargeFile) {
        state.showConfirmDialog = true;
        state.dialogTitle = decision.dialogTitle;
        state.dialogMessage = decision.dialogMessage;
        return state;
    }

    state.accepted = true;
    return state;
}

TransferSelectionFeedbackPlan TransferManager::transferSelectionFeedbackPlan(const TransferSelectionUiState& selectionState) {
    TransferSelectionFeedbackPlan plan;
    plan.hintText = selectionState.hintText;
    plan.statusMessage = selectionState.statusMessage;
    plan.statusTimeoutMs = selectionState.statusTimeoutMs;

    if (selectionState.showFailureDialog) {
        plan.dialogKind = TransferSelectionFeedbackPlan::DialogKind::Warning;
        plan.dialogTitle = selectionState.dialogTitle;
        plan.dialogMessage = selectionState.dialogMessage;
        plan.stopSelection = true;
        return plan;
    }

    if (selectionState.showConfirmDialog) {
        plan.dialogKind = TransferSelectionFeedbackPlan::DialogKind::Question;
        plan.dialogTitle = selectionState.dialogTitle;
        plan.dialogMessage = selectionState.dialogMessage;
        plan.requiresConfirmation = true;
        return plan;
    }

    if (!selectionState.accepted) {
        plan.stopSelection = true;
    }
    return plan;
}

TransferSelectionUiState TransferManager::resolveTransferSelectionUiState(const TransferSelectionUiState& pendingState,
                                                                          bool confirmed) {
    if (!pendingState.showConfirmDialog) {
        return pendingState;
    }

    TransferSelectionUiState state;
    state.filePath = pendingState.filePath;
    state.fileInfo = pendingState.fileInfo;
    state.fileSize = pendingState.fileSize;
    state.confirmKind = pendingState.confirmKind;
    if (confirmed) {
        state.accepted = true;
        return state;
    }

    state.hintText = QStringLiteral("已取消发送%1").arg(pendingState.confirmKind);
    state.statusMessage = state.hintText;
    state.statusTimeoutMs = 2600;
    return state;
}

TransferSendUiState TransferManager::selectionFeedbackWorkspaceState(const TransferSelectionUiState& selectionState,
                                                                    const TransferSelectionFeedbackPlan& feedbackPlan) {
    TransferSendUiState result;
    if (selectionState.showFailureDialog) {
        result.workspaceTitle = QStringLiteral("文件工作区 · 选择被拦截");
        result.workspaceDetail = selectionState.dialogMessage.trimmed().isEmpty()
            ? QStringLiteral("当前文件不满足发送前置条件，建议先修正后再重新选择。")
            : selectionState.dialogMessage.trimmed();
        result.statusTone = QStringLiteral("danger");
    } else if (selectionState.showConfirmDialog) {
        result.workspaceTitle = QStringLiteral("文件工作区 · 等待确认");
        result.workspaceDetail = selectionState.dialogMessage.trimmed().isEmpty()
            ? QStringLiteral("当前文件需要额外确认后才能继续发送。")
            : selectionState.dialogMessage.trimmed();
        result.statusTone = QStringLiteral("warning");
    } else if (!selectionState.accepted) {
        result.workspaceTitle = QStringLiteral("文件工作区 · 已取消选择");
        result.workspaceDetail = feedbackPlan.statusMessage.trimmed().isEmpty()
            ? QStringLiteral("当前没有选中文件，发送流程已停在选择阶段。")
            : feedbackPlan.statusMessage.trimmed();
        result.statusTone = QStringLiteral("muted");
    } else {
        result.workspaceTitle = QStringLiteral("文件工作区 · 已选中文件");
        result.workspaceDetail = QStringLiteral("%1 已选中，准备进入发送流程。")
            .arg(selectionState.fileInfo.fileName().trimmed().isEmpty()
                     ? QStringLiteral("文件")
                     : selectionState.fileInfo.fileName().trimmed());
        result.statusTone = QStringLiteral("accent");
    }

    if (!feedbackPlan.hintText.trimmed().isEmpty()) {
        result.hintText = feedbackPlan.hintText.trimmed();
    } else if (!feedbackPlan.statusMessage.trimmed().isEmpty()) {
        result.hintText = feedbackPlan.statusMessage.trimmed();
    } else {
        result.hintText = result.workspaceTitle;
    }
    result.statusMessage = feedbackPlan.statusMessage.trimmed().isEmpty()
        ? result.workspaceTitle
        : feedbackPlan.statusMessage.trimmed();
    result.statusTimeoutMs = feedbackPlan.statusTimeoutMs > 0 ? feedbackPlan.statusTimeoutMs : 2400;
    return result;
}

TransferMediaSelection TransferManager::mediaSelection(const QFileInfo& info) {
    const QString suffix = info.suffix().toLower();
    const bool isVideo = QStringList{QStringLiteral("mp4"),
                                     QStringLiteral("mov"),
                                     QStringLiteral("avi"),
                                     QStringLiteral("mkv"),
                                     QStringLiteral("wmv"),
                                     QStringLiteral("flv"),
                                     QStringLiteral("webm")}.contains(suffix);
    TransferMediaSelection result;
    result.isVideo = isVideo;
    result.mediaType = isVideo ? QStringLiteral("视频") : QStringLiteral("图片");
    return result;
}

TransferMediaPreviewPlan TransferManager::localMediaPreviewPlan(const QString& fileName,
                                                                const QString& fileSize,
                                                                bool isVideo) {
    TransferMediaPreviewPlan result;
    result.isVideo = isVideo;
    result.alignRight = false;
    result.text = isVideo
        ? QStringLiteral("视频文件 · %1 · %2 · 可在文件目录中打开").arg(fileName, fileSize)
        : QStringLiteral("%1 · %2").arg(fileName, fileSize);
    return result;
}

TransferMediaPreviewPlan TransferManager::remoteMediaPreviewPlan(const QString& cardText,
                                                                 bool isVideo) {
    TransferMediaPreviewPlan result;
    result.text = cardText;
    result.isVideo = isVideo;
    result.alignRight = true;
    return result;
}

TransferMediaPreviewPlan TransferManager::receivedMediaPreviewPlan(const QString& receivedName,
                                                                   const QString& receivedSize,
                                                                   const QString& manifestSuffix) {
    TransferMediaPreviewPlan result;
    result.text = QStringLiteral("%1 · %2%3").arg(receivedName, receivedSize, manifestSuffix);
    result.isVideo = false;
    result.alignRight = false;
    return result;
}

TransferSendUiState TransferManager::preparingSendState(const QString& kind,
                                                        const QString& fileName,
                                                        const QString& fileSize,
                                                        const QString& targetName) {
    TransferSendUiState result;
    result.hintText = QStringLiteral("准备发送%1到 %2 · %3 · %4").arg(kind, targetName, fileName, fileSize);
    result.statusMessage = result.hintText;
    result.workspaceTitle = QStringLiteral("文件工作区 · 准备发送");
    result.workspaceDetail = QStringLiteral("正在整理%1“%2”并准备发送到 %3。大小 %4，接下来会生成分片和校验清单。")
        .arg(kind, fileName, targetName, fileSize);
    result.statusTone = QStringLiteral("accent");
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
    result.workspaceTitle = QStringLiteral("文件工作区 · 已发送");
    result.workspaceDetail = QStringLiteral("%1“%2”已发送到 %3。接收方后续保存、校验或失败状态会继续显示在这里。")
        .arg(kind, fileName, targetName);
    result.statusTone = QStringLiteral("success");
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
    result.workspaceTitle = QStringLiteral("文件工作区 · 已发送");
    result.workspaceDetail = transferSuffix.isEmpty()
        ? QStringLiteral("%1“%2”已发送到 %3。接收方后续保存、校验或失败状态会继续显示在这里。")
              .arg(kind, fileName, targetName)
        : QStringLiteral("%1“%2”已发送到 %3。%4。接收方后续保存、校验或失败状态会继续显示在这里。")
              .arg(kind, fileName, targetName, transferSummary.trimmed());
    result.statusTone = QStringLiteral("success");
    result.statusTimeoutMs = 2600;
    return result;
}

TransferReceiveSaveUiState TransferManager::receivedTransferSaveUiState(const QString& kind,
                                                                        const QString& receivedName,
                                                                        const QString& receivedSize,
                                                                        const QString& displayName,
                                                                        const QString& manifestSuffix,
                                                                        const QString& integrityText,
                                                                        const QString& integritySuffix,
                                                                        const QString& savePath,
                                                                        bool saved,
                                                                        bool integrityFailed) {
    TransferReceiveSaveUiState result;
    if (saved) {
        result.saved = true;
        result.savedItemText = QStringLiteral("%1已自动保存: %2 · %3%4%5")
            .arg(kind, savePath, receivedSize, manifestSuffix, integritySuffix);
        result.savedItemToolTip = QStringLiteral("双击打开文件；右键可复制保存路径或打开目录\n%1").arg(savePath);
        result.savedIntegrityFailed = integrityFailed;
        result.receiptCardText = QStringLiteral("%1接收卡片 · %2 · %3 · 来自 %4 · 已保存到下载目录%5%6")
            .arg(kind, receivedName, receivedSize, displayName, manifestSuffix, integritySuffix);
        result.receiptCardToolTip = QStringLiteral("%1已保存到：%2").arg(kind, savePath);
        result.receiptReplyText = QStringLiteral("回执话术 · 已收到%1 %2（%3%4），%5，保存路径：%6 · 右键聊天记录可复制或打开保存目录")
            .arg(kind, receivedName, receivedSize, manifestSuffix, integrityText, savePath);
        result.receiptReplyToolTip = result.savedItemToolTip;
        result.eventReason = integrityFailed ? QStringLiteral("hash") : QStringLiteral("receive-saved");
        result.hintText = QStringLiteral("已接收%1 · %2 · %3 · 来自 %4%5%6")
            .arg(kind, receivedName, receivedSize, displayName, manifestSuffix, integritySuffix);
        result.statusMessage = QStringLiteral("%1已保存到下载目录 · %2%3%4")
            .arg(kind, receivedSize, manifestSuffix, integritySuffix);
        result.statusTimeoutMs = 3000;
        result.savedItem.text = result.savedItemText;
        result.savedItem.toolTip = result.savedItemToolTip;
        result.savedItem.foregroundRole = integrityFailed ? QStringLiteral("danger") : QStringLiteral("success");
        result.savedItem.alignRight = false;
        result.receiptCardItem.text = result.receiptCardText;
        result.receiptCardItem.toolTip = result.receiptCardToolTip;
        result.receiptCardItem.foregroundRole = QStringLiteral("success");
        result.receiptCardItem.backgroundRole = QStringLiteral("success-soft");
        result.receiptCardItem.alignRight = false;
        result.receiptReplyItem.text = result.receiptReplyText;
        result.receiptReplyItem.toolTip = result.receiptReplyToolTip;
        result.receiptReplyItem.foregroundRole = QStringLiteral("muted");
        result.receiptReplyItem.backgroundRole = QStringLiteral("muted-soft");
        result.receiptReplyItem.alignRight = false;
        return result;
    }

    result.failedItemText = QStringLiteral("%1保存失败 · %2 · %3 · 请检查下载目录权限")
        .arg(kind, receivedName, receivedSize);
    result.eventReason = QStringLiteral("receive-save-failed");
    result.hintText = QStringLiteral("%1保存失败 · %2 · 来自 %3").arg(kind, receivedName, displayName);
    result.statusMessage = QStringLiteral("%1保存失败，请检查下载目录权限").arg(kind);
    result.statusTimeoutMs = 3200;
    result.failedItem.text = result.failedItemText;
    result.failedItem.foregroundRole = QStringLiteral("danger");
    result.failedItem.backgroundRole = QStringLiteral("danger-soft");
    result.failedItem.alignRight = false;
    return result;
}

TransferReceiveRenderPlan TransferManager::receivedTransferRenderPlan(const TransferReceiveSaveUiState& uiState) {
    TransferReceiveRenderPlan result;
    result.eventReason = uiState.eventReason;
    result.hintText = uiState.hintText;
    result.statusMessage = uiState.statusMessage;
    result.statusTimeoutMs = uiState.statusTimeoutMs;
    if (uiState.saved) {
        result.chatItems.append(uiState.savedItem);
        result.chatItems.append(uiState.receiptCardItem);
        result.chatItems.append(uiState.receiptReplyItem);
    } else {
        result.chatItems.append(uiState.failedItem);
    }
    return result;
}

TransferReceiveRenderPlan TransferManager::receivedTransferPersistenceRenderPlan(const QString& kind,
                                                                                 const QString& receivedName,
                                                                                 const QString& receivedSize,
                                                                                 const QString& displayName,
                                                                                 const QString& manifestSuffix,
                                                                                 const QString& integrityText,
                                                                                 const QString& integritySuffix,
                                                                                 const QString& savePath,
                                                                                 bool saved) {
    const bool integrityFailed = integrityText.startsWith(QStringLiteral("完整性校验失败"));
    const TransferReceiveSaveUiState uiState = receivedTransferSaveUiState(kind,
                                                                           receivedName,
                                                                           receivedSize,
                                                                           displayName,
                                                                           manifestSuffix,
                                                                           integrityText,
                                                                           integritySuffix,
                                                                           savePath,
                                                                           saved,
                                                                           integrityFailed);
    return receivedTransferRenderPlan(uiState);
}

TransferReceivedWorkspaceState TransferManager::receivedTransferWorkspaceState(const QString& kind,
                                                                               const QString& receivedName,
                                                                               const QString& receivedSize,
                                                                               const QString& displayName,
                                                                               const QString& manifestSuffix,
                                                                               const QString& integrityText,
                                                                               const QString& integritySuffix,
                                                                               const QString& savePath,
                                                                               bool saved) {
    TransferReceivedWorkspaceState result;
    const bool integrityFailed = integrityText.startsWith(QStringLiteral("完整性校验失败"));
    const QString fileSummary = QStringLiteral("%1 · %2").arg(receivedName, receivedSize);
    const QString sourceSummary = displayName.trimmed().isEmpty()
        ? QStringLiteral("未知发送方")
        : displayName.trimmed();
    const QString integritySummary = integrityText.trimmed().isEmpty()
        ? QStringLiteral("完整性状态未提供")
        : integrityText.trimmed();

    if (saved) {
        result.workspaceTitle = integrityFailed
            ? QStringLiteral("文件工作区 · 已保存待复核")
            : QStringLiteral("文件工作区 · 已接收并保存");
        result.workspaceDetail = integrityFailed
            ? QStringLiteral("%1已保存到本机，但完整性校验提示异常。建议先复制保存路径、打开目录，再决定是否继续使用该文件。")
                  .arg(fileSummary)
            : QStringLiteral("%1已从 %2 接收并保存到本机。下一步可直接打开文件、打开目录，或复制路径继续流转。")
                  .arg(fileSummary, sourceSummary);
        result.nextStep = integrityFailed
            ? QStringLiteral("先打开目录或复制路径，完成人工复核后再决定是否继续使用该文件。")
            : QStringLiteral("可直接打开文件、打开目录，或复制路径交给协作者继续流转。");
        result.preservedState = integrityFailed
            ? QStringLiteral("系统已保留本地保存路径、清单摘要和完整性异常提示。")
            : QStringLiteral("系统已保留本地保存路径、清单摘要和接收回执。");
        result.diagnosticHint = QStringLiteral("如需排查，可复制文件工作区摘要或最近传输诊断。");
        result.previewText = QStringLiteral("来源：%1\n保存路径：%2\n清单：%3\n完整性：%4\n下一步：%5")
                                 .arg(sourceSummary,
                                      savePath,
                                      manifestSuffix.trimmed().isEmpty() ? QStringLiteral("无额外清单") : manifestSuffix.mid(3),
                                      integritySummary,
                                      result.nextStep);
        result.statusTone = integrityFailed ? QStringLiteral("warning") : QStringLiteral("success");
        return result;
    }

    result.workspaceTitle = QStringLiteral("文件工作区 · 接收保存失败");
    result.workspaceDetail = QStringLiteral("%1已收到，但写入下载目录失败。建议先检查下载目录权限、磁盘空间或安全软件拦截，再决定是否重新接收。")
                                 .arg(fileSummary);
    result.nextStep = QStringLiteral("先检查下载目录权限、磁盘空间或安全软件拦截，再决定是否重新接收。");
    result.preservedState = QStringLiteral("系统已保留接收来源、目标路径和清单摘要，便于继续排查。");
    result.diagnosticHint = QStringLiteral("建议先复制文件工作区摘要或最近传输诊断，再联系协作者或值班同学。");
    result.previewText = QStringLiteral("来源：%1\n目标路径：%2\n清单：%3\n完整性：%4\n下一步：检查下载目录权限、磁盘空间或路径可达性。")
                             .arg(sourceSummary,
                                  savePath,
                                  manifestSuffix.trimmed().isEmpty() ? QStringLiteral("无额外清单") : manifestSuffix.mid(3),
                                  integritySummary);
    result.statusTone = QStringLiteral("danger");
    return result;
}

TransferWorkspaceSummaryState TransferManager::recoveryWorkspaceSummary(const TransferRecoveryUiState& state,
                                                                       const TransferStatusEvent* latestEvent,
                                                                       bool hasDiagnostic) {
    TransferWorkspaceSummaryState result;
    result.title = state.canAutoResume
        ? QStringLiteral("文件工作区 · 可恢复")
        : QStringLiteral("文件工作区 · 需手动重发");
    result.detail = state.canAutoResume
        ? QStringLiteral("%1。系统已识别到可继续的未完成发送。").arg(state.detail)
        : QStringLiteral("%1。当前不会自动续传，必须改走手动重发。").arg(state.detail);
    result.nextStep = state.canAutoResume
        ? QStringLiteral("下一步：恢复发送，或先复制最近诊断确认风险后再恢复。")
        : QStringLiteral("下一步：重新选择原文件发送，或清理恢复记录后回到手动流程。");
    result.preservedState = state.canAutoResume
        ? QStringLiteral("系统已保留未完成发送记录、目标会话和恢复模式。")
        : QStringLiteral("系统已保留未完成发送记录和失败闭环原因，但不会自动调用续传。");

    QStringList hints;
    hints << (hasDiagnostic
                  ? QStringLiteral("可复制最近传输诊断，用于确认失败原因或交接排查。")
                  : QStringLiteral("当前还没有额外诊断文本，可先查看恢复模式和原因。"));
    if (latestEvent && !latestEvent->message.trimmed().isEmpty()) {
        hints << QStringLiteral("最近事件：%1").arg(latestEvent->title.trimmed().isEmpty()
                                                        ? latestEvent->message
                                                        : latestEvent->title);
    }
    result.diagnosticHint = hints.join(QStringLiteral(" "));
    result.previewText = QStringLiteral("恢复记录：%1\n目标：%2\n恢复模式：%3\n恢复原因：%4\n下一步：%5\n保留状态：%6")
                             .arg(state.fileName.isEmpty() ? QStringLiteral("未命名文件") : state.fileName,
                                  state.targetName.isEmpty() ? QStringLiteral("公共聊天室") : state.targetName,
                                  state.recoveryMode.isEmpty() ? QStringLiteral("未标记") : state.recoveryMode,
                                  state.recoveryReason.isEmpty() ? QStringLiteral("无额外原因") : state.recoveryReason,
                                  result.nextStep,
                                  result.preservedState);
    result.statusTone = state.canAutoResume ? QStringLiteral("warning") : QStringLiteral("danger");
    return result;
}

TransferWorkspaceSummaryState TransferManager::statusWorkspaceSummary(const TransferStatusEvent& event,
                                                                     bool hasRecovery,
                                                                     bool hasDiagnostic) {
    TransferWorkspaceSummaryState result;
    result.title = QStringLiteral("文件工作区 · 最近状态");
    result.detail = QStringLiteral("%1。%2").arg(event.message,
                                               event.actionHint.isEmpty()
                                                   ? QStringLiteral("可继续查看工作区摘要。")
                                                   : event.actionHint);
    result.nextStep = event.actionHint.trimmed().isEmpty()
        ? QStringLiteral("下一步：查看最近诊断后决定继续等待、重试还是重新发送。")
        : QStringLiteral("下一步：%1").arg(event.actionHint);
    result.preservedState = hasRecovery
        ? QStringLiteral("系统同时保留了未完成发送恢复记录，必要时可切回恢复路径。")
        : QStringLiteral("系统已保留最近一次状态分类和用户可见诊断。");
    result.diagnosticHint = hasDiagnostic
        ? QStringLiteral("可复制最近传输诊断，用于定位当前事件。")
        : QStringLiteral("当前还没有额外诊断文本，可先依据事件分类判断。");
    result.previewText = QStringLiteral("事件分类：%1\n状态标题：%2\n事件说明：%3\n下一步：%4\n保留状态：%5")
                             .arg(event.category.isEmpty() ? QStringLiteral("未标记") : event.category,
                                  event.title.isEmpty() ? QStringLiteral("文件事件") : event.title,
                                  event.detail.isEmpty() ? event.message : event.detail,
                                  result.nextStep,
                                  result.preservedState);

    if (event.category == QLatin1String("receive-saved")
        || event.category == QLatin1String("receive-completed")
        || event.category == QLatin1String("completed")) {
        result.statusTone = QStringLiteral("success");
    } else if (event.category == QLatin1String("receive-save-failed")
               || event.category == QLatin1String("receive-open-failed")
               || !event.retryable) {
        result.statusTone = QStringLiteral("danger");
    } else {
        result.statusTone = QStringLiteral("warning");
    }
    return result;
}

TransferWorkspaceSummaryState TransferManager::sendWorkspaceSummary(const TransferSendUiState& state,
                                                                   bool hasDiagnostic) {
    TransferWorkspaceSummaryState result;
    result.title = state.workspaceTitle.trimmed().isEmpty()
        ? QStringLiteral("文件工作区")
        : state.workspaceTitle.trimmed();
    result.detail = state.workspaceDetail.trimmed().isEmpty()
        ? state.statusMessage.trimmed()
        : state.workspaceDetail.trimmed();
    result.nextStep = state.hintText.trimmed().isEmpty()
        ? QStringLiteral("下一步：继续关注后续发送、接收或恢复状态。")
        : QStringLiteral("下一步：%1").arg(state.hintText.trimmed());
    result.preservedState = hasDiagnostic
        ? QStringLiteral("系统已保留最近一次传输诊断，可随时复制交接。")
        : QStringLiteral("系统会继续把后续发送、接收和失败结果聚合到这里。");
    result.diagnosticHint = hasDiagnostic
        ? QStringLiteral("如需排查，可直接复制最近传输诊断。")
        : QStringLiteral("当前没有额外诊断文本，后续状态更新后会自动补齐。");
    result.previewText = QStringLiteral("工作区：%1\n当前说明：%2\n下一步：%3\n保留状态：%4")
                             .arg(result.title,
                                  result.detail,
                                  result.nextStep,
                                  result.preservedState);
    result.statusTone = state.statusTone.trimmed().isEmpty()
        ? QStringLiteral("accent")
        : state.statusTone.trimmed();
    return result;
}

TransferWorkspaceSummaryState TransferManager::savedFileWorkspaceSummary(const QString& fileName,
                                                                        const QString& fileSize,
                                                                        const QString& savePath,
                                                                        bool canOpenFile,
                                                                        bool canOpenFolder,
                                                                        const QString& contextText) {
    TransferWorkspaceSummaryState result;
    const QString safeFileName = fileName.trimmed().isEmpty() ? QStringLiteral("已保存文件") : fileName.trimmed();
    const QString summary = fileSize.trimmed().isEmpty()
        ? safeFileName
        : QStringLiteral("%1 · %2").arg(safeFileName, fileSize.trimmed());
    result.title = canOpenFile
        ? QStringLiteral("文件工作区 · 已保存文件")
        : QStringLiteral("文件工作区 · 已保存文件待排查");
    result.detail = canOpenFile
        ? QStringLiteral("%1 已保存到本机。可直接打开文件、打开目录，或复制路径继续流转。").arg(summary)
        : QStringLiteral("%1 的保存记录仍在，但当前不能直接打开文件。建议先复制路径，再检查本地目录或安全软件拦截。").arg(summary);
    result.nextStep = canOpenFile
        ? QStringLiteral("下一步：打开文件、打开目录，或复制路径给协作者继续处理。")
        : canOpenFolder
            ? QStringLiteral("下一步：先打开目录核对文件是否仍在，再决定是否需要重新接收。")
            : QStringLiteral("下一步：先复制路径并检查目录可达性，再决定是否重新接收。");
    result.preservedState = QStringLiteral("系统已保留保存路径和聊天上下文，可继续排查来源与落盘位置。");
    result.diagnosticHint = QStringLiteral("如需交接，可复制文件工作区摘要；路径：%1").arg(savePath.trimmed().isEmpty()
                                                                 ? QStringLiteral("未记录")
                                                                 : savePath.trimmed());
    result.previewText = QStringLiteral("保存路径：%1\n文件可打开：%2\n目录可打开：%3\n聊天上下文：%4\n下一步：%5")
                             .arg(savePath.trimmed().isEmpty() ? QStringLiteral("未记录") : savePath.trimmed(),
                                  canOpenFile ? QStringLiteral("是") : QStringLiteral("否"),
                                  canOpenFolder ? QStringLiteral("是") : QStringLiteral("否"),
                                  contextText.trimmed().isEmpty() ? QStringLiteral("无额外聊天上下文") : contextText.trimmed(),
                                  result.nextStep);
    result.statusTone = canOpenFile ? QStringLiteral("success") : QStringLiteral("warning");
    return result;
}

TransferWorkspaceSummaryState TransferManager::emptyWorkspaceSummary(bool hasDiagnostic) {
    TransferWorkspaceSummaryState result;
    result.title = QStringLiteral("文件工作区");
    result.detail = QStringLiteral("当前没有未完成发送，也没有新的接收保存结果。下一次发送、恢复、失败或已保存文件动作会集中显示在这里。");
    result.nextStep = QStringLiteral("下一步：发送文件或图片/视频，或从聊天记录打开已保存文件，让工作区建立新的上下文。");
    result.preservedState = hasDiagnostic
        ? QStringLiteral("系统仍保留上一条传输诊断，可打开文件工作区继续查看。")
        : QStringLiteral("当前没有额外诊断文本，工作区会在下一次传输动作后自动补齐。");
    result.diagnosticHint = hasDiagnostic
        ? QStringLiteral("可复制最近一次传输诊断，作为后续排查起点。")
        : QStringLiteral("这里会在后续动作发生后补充诊断、恢复和保存路径信息。");
    result.previewText = QStringLiteral("工作区暂时为空\n下一步：%1\n保留状态：%2").arg(result.nextStep, result.preservedState);
    result.statusTone = QStringLiteral("muted");
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
