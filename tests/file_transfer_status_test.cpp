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
    return ok ? 0 : 1;
}
