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
                    && resumable.clearVisible
                    && resumable.clearEnabled
                    && resumable.fileName == QStringLiteral("report.zip")
                    && resumable.targetName == QStringLiteral("QQ:920001")
                    && resumable.resumeToolTip == resumable.detail
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

    TransferStatusEvent timeoutEvent = TransferManager::statusEvent(QStringLiteral("report.zip"),
                                                                    QStringLiteral("transfer-abcdef1234567890"),
                                                                    QStringLiteral("chunk-ack-timeout"),
                                                                    1024,
                                                                    4096);
    ok = expect(timeoutEvent.copyActionVisible
                    && timeoutEvent.copyActionEnabled
                    && timeoutEvent.message.contains(QStringLiteral("report.zip"))
                    && timeoutEvent.message.contains(QStringLiteral("ID:transfer-abc"))
                    && timeoutEvent.diagnostic.contains(QStringLiteral("category=timeout"))
                    && timeoutEvent.copyActionToolTip.contains(QString::fromUtf8("文件传输")),
                "status event should carry user message, diagnostic and copy action state") && ok;

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
                                                                                   QStringLiteral("abcdef1234567890"));
    ok = expect(sendingPrepared.manifestSummary == QString::fromUtf8("4片 · 分片1.0 KB · SHA-256 abcdef123456")
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
