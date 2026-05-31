#include "filetransferstatus.h"

#include <QCoreApplication>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning("%s", message);
        return false;
    }
    return true;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    bool ok = true;
    FileTransferStatusInfo info = describeFileTransferReason(QStringLiteral("chunk-ack-timeout"));
    ok = expect(info.category == QStringLiteral("timeout") && info.retryable,
                "timeout reason should be retryable and categorized") && ok;
    info = describeFileTransferReason(QString::fromUtf8("文件哈希不一致"));
    ok = expect(info.category == QStringLiteral("integrity") && !info.retryable,
                "hash mismatch should be non-retryable integrity failure") && ok;
    info = describeFileTransferReason(QStringLiteral("fallback-retained"));
    ok = expect(info.category == QStringLiteral("fallback-retained") && info.retryable,
                "fallback retained should be visible as recoverable") && ok;
    info = describeFileTransferReason(QStringLiteral("transfer-prepared"));
    ok = expect(info.category == QStringLiteral("prepared") && !info.retryable,
                "prepared transfer should be categorized as non-retryable status") && ok;
    info = describeFileTransferReason(QStringLiteral("transfer-resumed"));
    ok = expect(info.category == QStringLiteral("resumed") && info.retryable,
                "resumed transfer should be categorized as recoverable status") && ok;
    info = describeFileTransferReason(QStringLiteral("transfer-completed"));
    ok = expect(info.category == QStringLiteral("completed") && !info.retryable,
                "completed transfer should be categorized as final status") && ok;
    info = describeFileTransferReason(QStringLiteral("receive-started"));
    ok = expect(info.category == QStringLiteral("receive-started") && info.retryable,
                "receive-started should be categorized as receiver-side recoverable status") && ok;
    info = describeFileTransferReason(QStringLiteral("receive-completed"));
    ok = expect(info.category == QStringLiteral("receive-completed") && !info.retryable,
                "receive-completed should be categorized as receiver-side assembled status") && ok;
    info = describeFileTransferReason(QStringLiteral("receive-saved"));
    ok = expect(info.category == QStringLiteral("receive-saved") && !info.retryable,
                "receive-saved should be categorized as local save success") && ok;
    info = describeFileTransferReason(QString::fromUtf8("文件保存失败"));
    ok = expect(info.category == QStringLiteral("receive-save-failed") && info.retryable,
                "save failure should be categorized as receiver-side retryable local failure") && ok;
    info = describeFileTransferReason(QStringLiteral("receive-open-failed"));
    ok = expect(info.category == QStringLiteral("receive-open-failed") && !info.retryable,
                "open failure should be categorized as receiver-side local open failure") && ok;
    info = describeFileTransferReason(QStringLiteral("large_file_failed offer_delivery failed_received"));
    ok = expect(info.category == QStringLiteral("fallback-retained") && info.retryable,
                "cross-instance large file failure should be visible as fallback retained") && ok;
    ok = expect(fileTransferUserMessage(QStringLiteral("auth")).contains(QString::fromUtf8("权限")),
                "auth reason should produce user-facing permission text") && ok;
    ok = expect(fileTransferUserMessage(QString(), QString::fromUtf8("备用提示")) == QString::fromUtf8("备用提示"),
                "empty reason should use fallback text when provided") && ok;
    const QString eventText = fileTransferStatusEventMessage(QStringLiteral("report.zip"),
                                                             QStringLiteral("transfer-abcdef1234567890"),
                                                             QStringLiteral("chunk-ack-timeout"),
                                                             1024,
                                                             4096);
    ok = expect(eventText.contains(QStringLiteral("report.zip"))
                    && eventText.contains(QStringLiteral("ID:transfer-abc"))
                    && eventText.contains(QString::fromUtf8("可重试")),
                "status event should include file, short transfer id and retry hint") && ok;
    const QString diagnostic = fileTransferStatusDiagnostic(QStringLiteral("report.zip"),
                                                           QStringLiteral("transfer-abcdef1234567890"),
                                                           QStringLiteral("fallback-retained"),
                                                           4096,
                                                           4096);
    ok = expect(diagnostic.contains(QStringLiteral("category=fallback-retained"))
                    && diagnostic.contains(QStringLiteral("retryable=true"))
                    && diagnostic.contains(QStringLiteral("transferId=transfer-abcdef1234567890")),
                "diagnostic should include category, retryability and full transfer id") && ok;
    const QString completedText = fileTransferStatusEventMessage(QStringLiteral("report.zip"),
                                                                 QStringLiteral("transfer-abcdef1234567890"),
                                                                 QStringLiteral("transfer-completed"),
                                                                 4096,
                                                                 4096);
    ok = expect(completedText.contains(QString::fromUtf8("文件传输已完成"))
                    && !completedText.contains(QString::fromUtf8("可重试")),
                "completed event should not ask the user to retry") && ok;
    const QString saveFailedDiagnostic = fileTransferStatusDiagnostic(QStringLiteral("report.zip"),
                                                                      QStringLiteral("transfer-receive-001"),
                                                                      QStringLiteral("receive-save-failed"),
                                                                      4096,
                                                                      4096);
    ok = expect(saveFailedDiagnostic.contains(QStringLiteral("category=receive-save-failed"))
                    && saveFailedDiagnostic.contains(QStringLiteral("retryable=true")),
                "receiver save failure diagnostic should expose category and retryability") && ok;
    return ok ? 0 : 1;
}
