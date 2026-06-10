#include "transfermanager.h"

#include <QCoreApplication>
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
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    bool ok = true;

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
