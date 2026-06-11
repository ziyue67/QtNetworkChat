#include "transfermanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning("%s", message);
        return false;
    }
    return true;
}

QJsonObject savedState(const QString& filePath, const QString& receiverId) {
    QJsonObject state;
    state["filePath"] = filePath;
    state["receiverId"] = receiverId;
    return state;
}

QJsonObject recoveryStatus(bool canAutoResume,
                           const QString& mode = QStringLiteral("resume"),
                           const QString& reason = QStringLiteral("ok"),
                           bool e2eFileEncrypted = false) {
    QJsonObject status;
    status["canAutoResume"] = canAutoResume;
    status["recoveryMode"] = mode;
    status["reason"] = reason;
    status["e2eFileEncrypted"] = e2eFileEncrypted;
    return status;
}

QString createFileWithSize(const QString& path, qint64 bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    if (bytes > 0) {
        file.write(QByteArray(static_cast<int>(bytes), 'a'));
    }
    file.close();
    return path;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    bool ok = true;
    const QString tempRoot = QDir::temp().filePath("qtnetworkchat_transfer_manager_test");
    QDir().mkpath(tempRoot);
    const QString warningFilePath = createFileWithSize(QDir(tempRoot).filePath("warning.bin"), 21 * 1024 * 1024);
    const QString tooLargeFilePath = createFileWithSize(QDir(tempRoot).filePath("large.bin"), 81 * 1024 * 1024);
    ok = expect(!warningFilePath.isEmpty() && !tooLargeFilePath.isEmpty(),
                "transfer manager selection test files should be created") && ok;

    TransferRecoveryUiState empty = TransferManager::recoveryUiState(false, true, QJsonObject(), QJsonObject(), true);
    ok = expect(!empty.resumeVisible
                    && !empty.clearVisible
                    && empty.resumeToolTip.contains(QString::fromUtf8("暂无可恢复"))
                    && empty.clearToolTip.contains(QString::fromUtf8("暂无可清除"))
                    && empty.announceMessage.isEmpty(),
                "empty recovery state should hide actions and avoid announcements") && ok;

    TransferRecoveryUiState resumable = TransferManager::recoveryUiState(
        true,
        true,
        savedState(QStringLiteral("C:/tmp/report.zip"), QStringLiteral("920001")),
        recoveryStatus(true),
        true);
    ok = expect(resumable.resumeVisible
                    && resumable.resumeEnabled
                    && resumable.resumeAction.visible
                    && resumable.resumeAction.enabled
                    && resumable.clearVisible
                    && resumable.clearEnabled
                    && resumable.clearAction.visible
                    && resumable.clearAction.enabled
                    && resumable.fileName == QStringLiteral("report.zip")
                    && resumable.targetName == QStringLiteral("QQ:920001")
                    && resumable.resumeToolTip == resumable.detail
                    && resumable.resumeAction.toolTip == resumable.resumeToolTip
                    && resumable.clearAction.toolTip == resumable.clearToolTip
                    && resumable.announceMessage.contains(QString::fromUtf8("恢复未完成发送"))
                    && resumable.statusMessage.contains(QStringLiteral("report.zip")),
                "resumable saved transfer should enable resume and announce recovery") && ok;

    TransferRecoveryUiState disconnected = TransferManager::recoveryUiState(
        true,
        false,
        savedState(QStringLiteral("C:/tmp/report.zip"), QStringLiteral("920001")),
        recoveryStatus(true),
        false);
    ok = expect(disconnected.resumeVisible
                    && !disconnected.resumeEnabled
                    && disconnected.clearEnabled,
                "disconnected client should keep clear enabled but disable resume") && ok;

    TransferRecoveryUiState resend = TransferManager::recoveryUiState(
        true,
        true,
        savedState(QStringLiteral("C:/tmp/secret.bin"), QString()),
        recoveryStatus(false, QStringLiteral("resend"), QStringLiteral("e2e-file-resend-required"), true),
        true);
    ok = expect(resend.resumeVisible
                    && !resend.resumeEnabled
                    && resend.clearEnabled
                    && resend.targetName == QString::fromUtf8("公共聊天室")
                    && resend.resumeToolTip.contains(QStringLiteral("e2e-file-resend-required"))
                    && resend.announceMessage.contains(QString::fromUtf8("端到端加密"))
                    && resend.statusMessage.contains(QString::fromUtf8("需要重新发送")),
                "e2e resend recovery should fail closed for auto resume with user-facing guidance") && ok;

    TransferClearRecoveryPrompt clearPrompt = TransferManager::clearRecoveryPrompt(
        savedState(QStringLiteral("C:/tmp/report.zip"), QStringLiteral("920001")));
    ok = expect(clearPrompt.fileName == QStringLiteral("report.zip")
                    && clearPrompt.title == QString::fromUtf8("清除恢复记录")
                    && clearPrompt.message.contains(QStringLiteral("report.zip"))
                    && clearPrompt.noSavedStatusMessage.contains(QString::fromUtf8("暂无可清除"))
                    && clearPrompt.keptStatusMessage.contains(QString::fromUtf8("已保留"))
                    && clearPrompt.clearedSystemMessage.contains(QString::fromUtf8("已清除未完成发送"))
                    && clearPrompt.clearFailedStatusMessage.contains(QString::fromUtf8("清除恢复记录失败")),
                "clear recovery prompt should centralize confirmation and result messages") && ok;

    TransferResumeBlockedPrompt blockedPrompt = TransferManager::resumeBlockedPrompt(
        savedState(QStringLiteral("C:/tmp/secret.bin"), QString()),
        recoveryStatus(false, QStringLiteral("resend"), QStringLiteral("e2e-file-resend-required"), true));
    ok = expect(blockedPrompt.fileName == QStringLiteral("secret.bin")
                    && blockedPrompt.targetName == QString::fromUtf8("公共聊天室")
                    && blockedPrompt.reason == QStringLiteral("e2e-file-resend-required")
                    && blockedPrompt.title == QString::fromUtf8("需要重新发送")
                    && blockedPrompt.systemMessage.contains(QString::fromUtf8("不能自动续传"))
                    && blockedPrompt.message.contains(QStringLiteral("e2e-file-resend-required"))
                    && blockedPrompt.hintText.contains(QString::fromUtf8("需重新发送"))
                    && blockedPrompt.clearedHintText.contains(QString::fromUtf8("已清除未完成发送恢复记录")),
                "blocked resume prompt should centralize fail-closed resend guidance") && ok;

    TransferResumeResultState resumeSucceeded = TransferManager::resumeResultState(
        QStringLiteral("report.zip"),
        QStringLiteral("QQ:920001"),
        true,
        false,
        QString());
    ok = expect(resumeSucceeded.succeeded
                    && !resumeSucceeded.canceled
                    && !resumeSucceeded.failed
                    && resumeSucceeded.systemMessage.contains(QString::fromUtf8("已恢复并完成"))
                    && resumeSucceeded.hintText.contains(QString::fromUtf8("未完成发送已恢复"))
                    && resumeSucceeded.statusMessage.contains(QStringLiteral("report.zip")),
                "resume result state should describe successful recovery") && ok;

    TransferResumeResultState resumeCanceled = TransferManager::resumeResultState(
        QStringLiteral("report.zip"),
        QStringLiteral("QQ:920001"),
        false,
        true,
        QString());
    ok = expect(!resumeCanceled.succeeded
                    && resumeCanceled.canceled
                    && !resumeCanceled.failed
                    && resumeCanceled.systemMessage.contains(QString::fromUtf8("已取消恢复"))
                    && resumeCanceled.statusMessage.contains(QString::fromUtf8("已取消恢复发送")),
                "resume result state should describe canceled recovery") && ok;

    TransferResumeResultState resumeFailed = TransferManager::resumeResultState(
        QStringLiteral("report.zip"),
        QStringLiteral("QQ:920001"),
        false,
        false,
        QStringLiteral("server-resume-state-mismatch"));
    ok = expect(!resumeFailed.succeeded
                    && !resumeFailed.canceled
                    && resumeFailed.failed
                    && resumeFailed.reason == QStringLiteral("server-resume-state-mismatch")
                    && resumeFailed.systemMessage.contains(QString::fromUtf8("恢复记录已保留"))
                    && resumeFailed.failureTitle == QString::fromUtf8("恢复未完成发送失败")
                    && resumeFailed.failureMessage.contains(QStringLiteral("server-resume-state-mismatch"))
                    && resumeFailed.clearedStatusMessage.contains(QString::fromUtf8("已清除恢复记录")),
                "resume result state should describe failed recovery and retained record guidance") && ok;

    TransferStatusEvent timeoutEvent = TransferManager::statusEvent(QStringLiteral("report.zip"),
                                                                    QStringLiteral("transfer-abcdefABCDEF00"),
                                                                    QStringLiteral("chunk-ack-timeout"),
                                                                    1024,
                                                                    4096);
    ok = expect(timeoutEvent.copyActionVisible
                    && timeoutEvent.copyActionEnabled
                    && timeoutEvent.copyDiagnostic.action.visible
                    && timeoutEvent.copyDiagnostic.action.enabled
                    && timeoutEvent.message.contains(QStringLiteral("report.zip"))
                    && timeoutEvent.message.contains(QStringLiteral("ID:transfer-abc"))
                    && timeoutEvent.diagnostic.contains(QStringLiteral("category=chunk-delivery-failed"))
                    && timeoutEvent.actionHint.contains(QString::fromUtf8("稍后重试"))
                    && timeoutEvent.chatHintText.contains(QString::fromUtf8("文件分片未被接收端确认"))
                    && timeoutEvent.statusBarMessage.contains(timeoutEvent.actionHint)
                    && timeoutEvent.statusBarTimeoutMs == 5200
                    && timeoutEvent.copyDiagnostic.clipboardText == timeoutEvent.diagnostic.trimmed()
                    && timeoutEvent.copyActionToolTip.contains(QString::fromUtf8("文件传输")),
                "status event should carry user message, diagnostic and copy action state") && ok;

    TransferStatusEvent objectReadbackEvent = TransferManager::statusEvent(QStringLiteral("large.bin"),
                                                                           QStringLiteral("large-offer-001"),
                                                                           QStringLiteral("object-read-failed"),
                                                                           0,
                                                                           8192);
    ok = expect(objectReadbackEvent.message.contains(QString::fromUtf8("大文件对象暂时无法读取"))
                    && objectReadbackEvent.actionHint.contains(QString::fromUtf8("对象存储"))
                    && objectReadbackEvent.chatHintText.contains(QString::fromUtf8("保留诊断"))
                    && objectReadbackEvent.statusBarMessage.contains(QString::fromUtf8("切回普通文件路径"))
                    && objectReadbackEvent.statusBarTimeoutMs == 5200
                    && objectReadbackEvent.diagnostic.contains(QStringLiteral("category=object-readback-unavailable")),
                "object readback status event should provide actionable storage fallback guidance") && ok;

    TransferStatusEvent completedEvent = TransferManager::statusEvent(QStringLiteral("report.zip"),
                                                                      QStringLiteral("transfer-abcdefABCDEF00"),
                                                                      QStringLiteral("transfer-completed"),
                                                                      4096,
                                                                      4096);
    ok = expect(completedEvent.message.contains(QString::fromUtf8("文件传输已完成"))
                    && completedEvent.actionHint.contains(QString::fromUtf8("无需处理"))
                    && completedEvent.chatHintText.contains(QString::fromUtf8("无需处理"))
                    && completedEvent.statusBarTimeoutMs == 2600
                    && !completedEvent.statusBarMessage.contains(QString::fromUtf8("稍后重试")),
                "completed status event should be short-lived and avoid retry guidance") && ok;

    TransferDiagnosticCopyUiState emptyCopy = TransferManager::diagnosticCopyUiState(QStringLiteral("  "));
    ok = expect(emptyCopy.action.visible
                    && !emptyCopy.action.enabled
                    && emptyCopy.clipboardText.isEmpty()
                    && emptyCopy.emptyStatusMessage.contains(QString::fromUtf8("暂无可复制")),
                "empty transfer diagnostic copy state should keep action visible but disabled") && ok;

    TransferDiagnosticCopyUiState readyCopy = TransferManager::diagnosticCopyUiState(QStringLiteral("  category=timeout; id=abc  "));
    ok = expect(readyCopy.action.visible
                    && readyCopy.action.enabled
                    && readyCopy.clipboardText == QStringLiteral("category=timeout; id=abc")
                    && readyCopy.copiedStatusMessage.contains(QString::fromUtf8("已复制")),
                "ready transfer diagnostic copy state should trim clipboard text and expose success message") && ok;

    TransferProgressUiState initial = TransferManager::sendingInitialState(QString::fromUtf8("文件"),
                                                                           QStringLiteral("report.zip"),
                                                                           QStringLiteral("QQ:920001"));
    ok = expect(initial.percent == 0
                    && initial.labelText.contains(QString::fromUtf8("正在分片读取文件"))
                    && initial.labelText.contains(QStringLiteral("report.zip -> QQ:920001")),
                "sending initial progress state should describe source and target") && ok;

    TransferProgressUiState sendingProgress = TransferManager::sendingProgressState(QString::fromUtf8("文件"),
                                                                                   QStringLiteral("report.zip"),
                                                                                   QStringLiteral("QQ:920001"),
                                                                                   1536,
                                                                                   2048);
    ok = expect(sendingProgress.percent == 75
                    && sendingProgress.labelText.contains(QString::fromUtf8("正在分片发送文件到 QQ:920001"))
                    && sendingProgress.labelText.contains(QStringLiteral("1.5 KB / 2.0 KB")),
                "sending progress state should clamp percent and format byte progress") && ok;

    TransferProgressUiState overflowProgress = TransferManager::sendingProgressState(QString::fromUtf8("文件"),
                                                                                    QStringLiteral("report.zip"),
                                                                                    QStringLiteral("QQ:920001"),
                                                                                    4096,
                                                                                    2048);
    ok = expect(overflowProgress.percent == 100,
                "sending progress state should clamp percent at 100") && ok;

    TransferProgressUiState sendingPrepared = TransferManager::sendingPreparedState(QString::fromUtf8("文件"),
                                                                                   QStringLiteral("report.zip"),
                                                                                   QStringLiteral("QQ:920001"),
                                                                                   4096,
                                                                                   1024,
                                                                                   4,
                                                                                   QStringLiteral("abcdefABCDEF00"));
    ok = expect(sendingPrepared.manifestSummary == QString::fromUtf8("4片 · 分片1.0 KB · SHA-256 abcdefABCDEF")
                    && sendingPrepared.labelText.contains(QString::fromUtf8("文件校验清单已生成")),
                "sending prepared state should expose manifest summary and label") && ok;

    TransferSendUiState publicRemoved = TransferManager::publicGroupRemovedState(QString::fromUtf8("图片/视频"));
    ok = expect(publicRemoved.hintText.contains(QString::fromUtf8("图片/视频发送暂停"))
                    && publicRemoved.hintText.contains(QString::fromUtf8("重新邀请"))
                    && publicRemoved.statusMessage.contains(QString::fromUtf8("暂不能发送图片/视频"))
                    && publicRemoved.statusTimeoutMs == 3000,
                "public group removed send state should fail closed with reinvite guidance") && ok;

    TransferSendUiState disconnectedSend = TransferManager::disconnectedSendState(QString::fromUtf8("文件"), QString());
    ok = expect(disconnectedSend.hintText.contains(QString::fromUtf8("公共聊天室 已断开"))
                    && disconnectedSend.statusMessage.contains(QString::fromUtf8("暂不能发送文件到 公共聊天室"))
                    && disconnectedSend.statusTimeoutMs == 3000,
                "disconnected send state should fall back to public chat target") && ok;

    TransferSendUiState preparingSend = TransferManager::preparingSendState(QString::fromUtf8("视频"),
                                                                           QStringLiteral("clip.mp4"),
                                                                           QStringLiteral("2.0 MB"),
                                                                           QString::fromUtf8("好友A"));
    ok = expect(preparingSend.hintText == QString::fromUtf8("准备发送视频到 好友A · clip.mp4 · 2.0 MB")
                    && preparingSend.statusMessage == preparingSend.hintText
                    && preparingSend.statusTimeoutMs == 1800,
                "preparing send state should centralize pre-transfer copy") && ok;

    TransferSelectionPlan fileSelectionPlan = TransferManager::fileSelectionPlan();
    ok = expect(fileSelectionPlan.dialogTitle == QString::fromUtf8("选择文件")
                    && fileSelectionPlan.confirmKind == QString::fromUtf8("文件")
                    && fileSelectionPlan.canceledHint == QString::fromUtf8("文件发送已取消")
                    && fileSelectionPlan.canceledStatus == QString::fromUtf8("已取消选择文件")
                    && fileSelectionPlan.preparingKind == QString::fromUtf8("文件")
                    && fileSelectionPlan.filters.contains(QStringLiteral("*.zip")),
                "file selection plan should centralize file dialog and cancel copy") && ok;

    TransferSelectionPlan mediaSelectionPlan = TransferManager::mediaSelectionPlan();
    ok = expect(mediaSelectionPlan.dialogTitle == QString::fromUtf8("选择图片或视频")
                    && mediaSelectionPlan.confirmKind == QString::fromUtf8("媒体文件")
                    && mediaSelectionPlan.canceledHint == QString::fromUtf8("图片/视频发送已取消")
                    && mediaSelectionPlan.canceledStatus == QString::fromUtf8("已取消选择图片/视频")
                    && mediaSelectionPlan.preparingKind == QString::fromUtf8("媒体文件")
                    && mediaSelectionPlan.filters.contains(QStringLiteral("*.mp4")),
                "media selection plan should centralize media dialog and cancel copy") && ok;

    TransferSelectionUiState canceledSelectionUiState =
        TransferManager::transferSelectionUiState(fileSelectionPlan, QString());
    ok = expect(!canceledSelectionUiState.accepted
                    && !canceledSelectionUiState.showFailureDialog
                    && !canceledSelectionUiState.showConfirmDialog
                    && canceledSelectionUiState.hintText == QString::fromUtf8("文件发送已取消")
                    && canceledSelectionUiState.statusMessage == QString::fromUtf8("已取消选择文件")
                    && canceledSelectionUiState.statusTimeoutMs == 1600,
                "transfer selection ui state should centralize canceled-file feedback") && ok;

    TransferSelectionUiState rejectedSelectionUiState =
        TransferManager::transferSelectionUiState(fileSelectionPlan, tooLargeFilePath);
    ok = expect(!rejectedSelectionUiState.accepted
                    && rejectedSelectionUiState.showFailureDialog
                    && !rejectedSelectionUiState.showConfirmDialog
                    && rejectedSelectionUiState.dialogTitle == QString::fromUtf8("文件过大")
                    && rejectedSelectionUiState.dialogMessage.contains(QString::fromUtf8("超过当前 80 MB"))
                    && rejectedSelectionUiState.statusMessage.contains(QString::fromUtf8("超过 80 MB")),
                "transfer selection ui state should centralize rejected-file dialog guidance") && ok;

    TransferSelectionUiState warningSelectionUiState =
        TransferManager::transferSelectionUiState(fileSelectionPlan, warningFilePath);
    ok = expect(!warningSelectionUiState.accepted
                    && !warningSelectionUiState.showFailureDialog
                    && warningSelectionUiState.showConfirmDialog
                    && warningSelectionUiState.fileInfo.fileName() == QStringLiteral("warning.bin")
                    && warningSelectionUiState.dialogTitle == QString::fromUtf8("确认发送大文件")
                    && !warningSelectionUiState.fileSize.isEmpty(),
                "transfer selection ui state should centralize large-file confirmation prompts") && ok;

    TransferSelectionUiState confirmedSelectionUiState =
        TransferManager::resolveTransferSelectionUiState(warningSelectionUiState, true);
    ok = expect(confirmedSelectionUiState.accepted
                    && !confirmedSelectionUiState.showFailureDialog
                    && !confirmedSelectionUiState.showConfirmDialog
                    && confirmedSelectionUiState.fileInfo.fileName() == QStringLiteral("warning.bin")
                    && confirmedSelectionUiState.statusMessage.isEmpty(),
                "confirmed transfer selection ui state should become accepted without extra feedback") && ok;

    TransferSelectionUiState rejectedWarningSelectionUiState =
        TransferManager::resolveTransferSelectionUiState(warningSelectionUiState, false);
    ok = expect(!rejectedWarningSelectionUiState.accepted
                    && !rejectedWarningSelectionUiState.showFailureDialog
                    && !rejectedWarningSelectionUiState.showConfirmDialog
                    && rejectedWarningSelectionUiState.hintText == QString::fromUtf8("已取消发送文件")
                    && rejectedWarningSelectionUiState.statusMessage == QString::fromUtf8("已取消发送文件")
                    && rejectedWarningSelectionUiState.statusTimeoutMs == 2600,
                "rejected transfer selection confirmation should reuse warning-canceled feedback") && ok;

    TransferMediaSelection imageSelection = TransferManager::mediaSelection(QFileInfo(QStringLiteral("C:/tmp/photo.png")));
    ok = expect(!imageSelection.isVideo
                    && imageSelection.mediaType == QString::fromUtf8("图片"),
                "image selection should classify image suffixes as pictures") && ok;

    TransferMediaSelection videoSelection = TransferManager::mediaSelection(QFileInfo(QStringLiteral("C:/tmp/clip.mkv")));
    ok = expect(videoSelection.isVideo
                    && videoSelection.mediaType == QString::fromUtf8("视频"),
                "media selection should classify video suffixes as videos") && ok;

    TransferMediaPreviewPlan localImagePreview = TransferManager::localMediaPreviewPlan(QStringLiteral("photo.png"),
                                                                                        QStringLiteral("512 KB"),
                                                                                        false);
    ok = expect(localImagePreview.text == QString::fromUtf8("photo.png · 512 KB")
                    && !localImagePreview.isVideo
                    && !localImagePreview.alignRight,
                "local image preview plan should keep compact left-aligned copy") && ok;

    TransferMediaPreviewPlan localVideoPreview = TransferManager::localMediaPreviewPlan(QStringLiteral("clip.mp4"),
                                                                                        QStringLiteral("2.0 MB"),
                                                                                        true);
    ok = expect(localVideoPreview.text == QString::fromUtf8("视频文件 · clip.mp4 · 2.0 MB · 可在文件目录中打开")
                    && localVideoPreview.isVideo
                    && !localVideoPreview.alignRight,
                "local video preview plan should expose open-in-folder guidance") && ok;

    TransferMediaPreviewPlan remotePreview = TransferManager::remoteMediaPreviewPlan(QString::fromUtf8("图片卡片 · photo.png · 512 KB · 已发送到 好友A"),
                                                                                     false);
    ok = expect(remotePreview.text == QString::fromUtf8("图片卡片 · photo.png · 512 KB · 已发送到 好友A")
                    && !remotePreview.isVideo
                    && remotePreview.alignRight,
                "remote media preview plan should keep card copy and right alignment") && ok;

    TransferMediaPreviewPlan receivedPreview = TransferManager::receivedMediaPreviewPlan(QStringLiteral("photo.png"),
                                                                                         QStringLiteral("512 KB"),
                                                                                         QString::fromUtf8(" · 4片"));
    ok = expect(receivedPreview.text == QString::fromUtf8("photo.png · 512 KB · 4片")
                    && !receivedPreview.isVideo
                    && !receivedPreview.alignRight,
                "received media preview plan should centralize inbound preview text") && ok;

    TransferSendUiState canceledSend = TransferManager::canceledSendState(QString::fromUtf8("文件"), QStringLiteral("report.zip"));
    ok = expect(canceledSend.hintText == QString::fromUtf8("已取消发送文件 · report.zip")
                    && canceledSend.statusMessage == QString::fromUtf8("已取消发送文件：report.zip")
                    && canceledSend.statusTimeoutMs == 2200,
                "canceled send state should centralize cancel copy") && ok;

    TransferSendUiState failedSend = TransferManager::failedSendState(QString::fromUtf8("图片"),
                                                                      QStringLiteral("photo.png"),
                                                                      QStringLiteral("512 KB"),
                                                                      QString::fromUtf8("好友A"));
    ok = expect(failedSend.hintText == QString::fromUtf8("图片发送失败 · photo.png · 好友A")
                    && failedSend.statusMessage == QString::fromUtf8("图片发送失败：photo.png")
                    && failedSend.warningTitle == QString::fromUtf8("发送失败")
                    && failedSend.warningMessage.contains(QString::fromUtf8("图片“photo.png”（512 KB）未发送到 好友A"))
                    && failedSend.statusTimeoutMs == 3000,
                "failed send state should centralize warning and retry guidance") && ok;

    TransferSendUiState localCompleted = TransferManager::localSendCompletedState(QString::fromUtf8("文件"),
                                                                                 QStringLiteral("report.zip"),
                                                                                 QStringLiteral("4.0 KB"),
                                                                                 QString::fromUtf8("本地群"),
                                                                                 QStringLiteral("12:00:00"));
    ok = expect(localCompleted.systemMessage == QString::fromUtf8("文件发送详情：report.zip · 4.0 KB · 到 本地群")
                    && localCompleted.cardText == QString::fromUtf8("文件卡片 · report.zip · 4.0 KB · 已发送到 本地群")
                    && localCompleted.receiptText == QString::fromUtf8("文件查收话术 · 我已发送文件 report.zip 到 本地群，请注意查收。 · 右键聊天记录可复制")
                    && localCompleted.hintText == QString::fromUtf8("已发送文件到 本地群 · 4.0 KB · 12:00:00")
                    && localCompleted.statusMessage == QString::fromUtf8("已发送文件到 本地群 · 4.0 KB")
                    && localCompleted.statusTimeoutMs == 2200,
                "local completed send state should centralize local file card and receipt copy") && ok;

    TransferSendUiState remoteCompleted = TransferManager::remoteSendCompletedState(QString::fromUtf8("图片"),
                                                                                   QStringLiteral("photo.png"),
                                                                                   QStringLiteral("512 KB"),
                                                                                   QString::fromUtf8("好友A"),
                                                                                   QStringLiteral("12:00:01"),
                                                                                   QString::fromUtf8("4片 · 分片128 KB"));
    ok = expect(remoteCompleted.systemMessage == QString::fromUtf8("已发送图片: photo.png · 512 KB · 到 好友A · 4片 · 分片128 KB")
                    && remoteCompleted.cardText == QString::fromUtf8("图片卡片 · photo.png · 512 KB · 已发送到 好友A · 4片 · 分片128 KB")
                    && remoteCompleted.receiptText == QString::fromUtf8("图片查收话术 · 我已发送图片 photo.png 到 好友A，请注意查收。 · 右键聊天记录可复制")
                    && remoteCompleted.hintText == QString::fromUtf8("已发送图片到 好友A · 512 KB · 12:00:01 · 4片 · 分片128 KB")
                    && remoteCompleted.statusMessage == QString::fromUtf8("已发送图片到 好友A · 512 KB · 4片 · 分片128 KB")
                    && remoteCompleted.statusTimeoutMs == 2600,
                "remote completed send state should include transfer summary in all completion surfaces") && ok;

    TransferReceiveSaveUiState savedReceive = TransferManager::receivedTransferSaveUiState(QString::fromUtf8("文件"),
                                                                                           QStringLiteral("report.zip"),
                                                                                           QStringLiteral("4.0 KB"),
                                                                                           QString::fromUtf8("好友A"),
                                                                                           QString::fromUtf8(" · 4片"),
                                                                                           QString::fromUtf8("完整性已验证"),
                                                                                           QString::fromUtf8(" · 完整性已验证"),
                                                                                           QStringLiteral("C:/Downloads/report.zip"),
                                                                                           true,
                                                                                           false);
    ok = expect(savedReceive.savedItemText.contains(QString::fromUtf8("文件已自动保存"))
                    && savedReceive.savedItemToolTip.contains(QStringLiteral("C:/Downloads/report.zip"))
                    && savedReceive.saved
                    && !savedReceive.savedIntegrityFailed
                    && savedReceive.receiptCardText.contains(QString::fromUtf8("来自 好友A"))
                    && savedReceive.receiptReplyText.contains(QString::fromUtf8("回执话术"))
                    && savedReceive.eventReason == QStringLiteral("receive-saved")
                    && savedReceive.hintText.contains(QString::fromUtf8("已接收文件"))
                    && savedReceive.statusMessage.contains(QString::fromUtf8("已保存到下载目录"))
                    && savedReceive.savedItem.text == savedReceive.savedItemText
                    && savedReceive.savedItem.toolTip == savedReceive.savedItemToolTip
                    && savedReceive.savedItem.foregroundRole == QStringLiteral("success")
                    && savedReceive.receiptCardItem.text == savedReceive.receiptCardText
                    && savedReceive.receiptCardItem.backgroundRole == QStringLiteral("success-soft")
                    && savedReceive.receiptReplyItem.text == savedReceive.receiptReplyText
                    && savedReceive.receiptReplyItem.foregroundRole == QStringLiteral("muted")
                    && savedReceive.statusTimeoutMs == 3000,
                "saved receive ui state should centralize saved receipt and status copy") && ok;
    TransferReceiveRenderPlan savedReceivePlan = TransferManager::receivedTransferRenderPlan(savedReceive);
    ok = expect(savedReceivePlan.chatItems.size() == 3
                    && savedReceivePlan.chatItems.at(0).text == savedReceive.savedItemText
                    && savedReceivePlan.chatItems.at(1).text == savedReceive.receiptCardText
                    && savedReceivePlan.chatItems.at(2).text == savedReceive.receiptReplyText
                    && savedReceivePlan.eventReason == QStringLiteral("receive-saved")
                    && savedReceivePlan.hintText == savedReceive.hintText
                    && savedReceivePlan.statusMessage == savedReceive.statusMessage
                    && savedReceivePlan.statusTimeoutMs == 3000,
                "saved receive render plan should centralize chat items and status surfaces") && ok;

    TransferReceiveSaveUiState integrityFailedReceive = TransferManager::receivedTransferSaveUiState(QString::fromUtf8("文件"),
                                                                                                      QStringLiteral("report.zip"),
                                                                                                      QStringLiteral("4.0 KB"),
                                                                                                      QString::fromUtf8("好友A"),
                                                                                                      QString(),
                                                                                                      QString::fromUtf8("完整性校验失败"),
                                                                                                      QString::fromUtf8(" · 完整性校验失败"),
                                                                                                      QStringLiteral("C:/Downloads/report.zip"),
                                                                                                      true,
                                                                                                      true);
    ok = expect(integrityFailedReceive.savedItem.foregroundRole == QStringLiteral("danger")
                    && integrityFailedReceive.eventReason == QStringLiteral("hash"),
                "integrity failed receive state should switch saved item role to danger and preserve hash event reason") && ok;

    TransferReceiveSaveUiState failedReceive = TransferManager::receivedTransferSaveUiState(QString::fromUtf8("图片"),
                                                                                            QStringLiteral("photo.png"),
                                                                                            QStringLiteral("512 KB"),
                                                                                            QString::fromUtf8("好友A"),
                                                                                            QString(),
                                                                                            QString(),
                                                                                            QString(),
                                                                                            QStringLiteral("C:/Downloads/photo.png"),
                                                                                            false,
                                                                                            false);
    ok = expect(failedReceive.failedItemText.contains(QString::fromUtf8("图片保存失败"))
                    && !failedReceive.saved
                    && failedReceive.eventReason == QStringLiteral("receive-save-failed")
                    && failedReceive.hintText == QString::fromUtf8("图片保存失败 · photo.png · 来自 好友A")
                    && failedReceive.statusMessage == QString::fromUtf8("图片保存失败，请检查下载目录权限")
                    && failedReceive.failedItem.text == failedReceive.failedItemText
                    && failedReceive.failedItem.foregroundRole == QStringLiteral("danger")
                    && failedReceive.failedItem.backgroundRole == QStringLiteral("danger-soft")
                    && failedReceive.statusTimeoutMs == 3200,
                "failed receive ui state should centralize save failure guidance") && ok;
    TransferReceiveRenderPlan failedReceivePlan = TransferManager::receivedTransferRenderPlan(failedReceive);
    ok = expect(failedReceivePlan.chatItems.size() == 1
                    && failedReceivePlan.chatItems.at(0).text == failedReceive.failedItemText
                    && failedReceivePlan.eventReason == QStringLiteral("receive-save-failed")
                    && failedReceivePlan.hintText == failedReceive.hintText
                    && failedReceivePlan.statusMessage == failedReceive.statusMessage
                    && failedReceivePlan.statusTimeoutMs == 3200,
                "failed receive render plan should centralize failure item and status surfaces") && ok;

    TransferProgressUiState resumeCancel = TransferManager::resumeCancelState(QStringLiteral("report.zip"));
    ok = expect(resumeCancel.labelText.contains(QString::fromUtf8("正在取消恢复发送"))
                    && resumeCancel.labelText.contains(QStringLiteral("report.zip")),
                "resume cancel state should carry cancel guidance") && ok;

    TransferProgressUiState resumeProgress = TransferManager::resumeProgressState(QStringLiteral("report.zip"),
                                                                                 QStringLiteral("QQ:920001"),
                                                                                 512,
                                                                                 0);
    ok = expect(resumeProgress.percent == 0
                    && resumeProgress.labelText.contains(QStringLiteral("512 B / 0 B")),
                "resume progress state should handle unknown total bytes") && ok;

    TransferProgressUiState resumePrepared = TransferManager::resumePreparedState(QStringLiteral("report.zip"),
                                                                                 QStringLiteral("QQ:920001"),
                                                                                 4096,
                                                                                 0,
                                                                                 0,
                                                                                 QString());
    ok = expect(resumePrepared.manifestSummary == QStringLiteral("4.0 KB")
                    && resumePrepared.labelText.contains(QString::fromUtf8("恢复发送校验清单已生成")),
                "resume prepared state should fall back to total size manifest") && ok;

    return ok ? 0 : 1;
}
