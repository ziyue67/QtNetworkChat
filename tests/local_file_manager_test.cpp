#include "localfilemanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning("%s", message);
        return false;
    }
    return true;
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
    QSettings settings("QtNetworkChat", "QtNetworkChat");
    settings.clear();

    const QString tempRoot = QDir::temp().filePath("qtnetworkchat_local_file_manager_test");
    QDir().mkpath(tempRoot);
    const QString nestedDir = QDir(tempRoot).filePath("nested");
    QDir().mkpath(nestedDir);
    const QString receiveDir = QDir(tempRoot).filePath("receive");
    QDir().mkpath(receiveDir);

    const QString smallFilePath = createFileWithSize(QDir(nestedDir).filePath("small.txt"), 1024);
    const QString warningFilePath = createFileWithSize(QDir(nestedDir).filePath("warning.bin"), 21 * 1024 * 1024);
    const QString tooLargeFilePath = createFileWithSize(QDir(nestedDir).filePath("large.bin"), 81 * 1024 * 1024);
    const QString emptyFilePath = createFileWithSize(QDir(nestedDir).filePath("empty.bin"), 0);
    const QString avatarFilePath = createFileWithSize(QDir(nestedDir).filePath("avatar.png"), 4096);
    const QString avatarLargePath = createFileWithSize(QDir(nestedDir).filePath("avatar-large.png"), 11 * 1024 * 1024);

    ok = expect(!smallFilePath.isEmpty() && !warningFilePath.isEmpty() && !tooLargeFilePath.isEmpty() && !emptyFilePath.isEmpty(),
                "test files should be created") && ok;

    LocalFileManager::rememberTransferDirectory(smallFilePath);
    ok = expect(LocalFileManager::lastTransferDirectory() == nestedDir,
                "transfer directory should round-trip through settings") && ok;

    LocalFileManager::rememberAvatarDirectory(avatarFilePath);
    ok = expect(LocalFileManager::lastAvatarDirectory() == nestedDir,
                "avatar directory should round-trip through settings") && ok;

    LocalFileValidationResult missingTransfer = LocalFileManager::validateTransferFile(QFileInfo(QDir(nestedDir).filePath("missing.txt")), QStringLiteral("文件"));
    ok = expect(!missingTransfer.accepted && missingTransfer.statusMessage.contains(QString::fromUtf8("文件不可读取")),
                "missing transfer file should be rejected") && ok;

    LocalFileValidationResult emptyTransfer = LocalFileManager::validateTransferFile(QFileInfo(emptyFilePath), QStringLiteral("文件"));
    ok = expect(!emptyTransfer.accepted && emptyTransfer.statusMessage.contains(QString::fromUtf8("文件为空")),
                "empty transfer file should be rejected") && ok;

    LocalFileValidationResult largeTransfer = LocalFileManager::validateTransferFile(QFileInfo(tooLargeFilePath), QStringLiteral("文件"));
    ok = expect(!largeTransfer.accepted && largeTransfer.statusMessage.contains(QString::fromUtf8("超过 80 MB")),
                "oversized transfer file should be rejected") && ok;

    LocalFileValidationResult warningTransfer = LocalFileManager::validateTransferFile(QFileInfo(warningFilePath), QStringLiteral("文件"));
    ok = expect(warningTransfer.accepted && warningTransfer.warningRequired && warningTransfer.warningTitle.contains(QString::fromUtf8("确认发送大文件")),
                "large but allowed transfer file should request confirmation") && ok;

    LocalFileValidationResult normalTransfer = LocalFileManager::validateTransferFile(QFileInfo(smallFilePath), QStringLiteral("文件"));
    ok = expect(normalTransfer.accepted && !normalTransfer.warningRequired,
                "small transfer file should be accepted without warning") && ok;

    LocalFileSelectionResult canceledTransferSelection = LocalFileManager::selectTransferFile(QString(), QStringLiteral("文件"));
    ok = expect(canceledTransferSelection.canceled && !canceledTransferSelection.accepted,
                "empty transfer selection should be treated as canceled") && ok;
    ok = expect(canceledTransferSelection.canceledHint == QString::fromUtf8("文件发送已取消")
                    && canceledTransferSelection.canceledStatusMessage == QString::fromUtf8("已取消选择文件")
                    && canceledTransferSelection.canceledStatusTimeoutMs == 1600,
                "canceled transfer selection should expose reusable UI feedback") && ok;

    LocalFileSelectionResult warningTransferSelection = LocalFileManager::selectTransferFile(warningFilePath, QStringLiteral("文件"));
    ok = expect(warningTransferSelection.accepted
                    && warningTransferSelection.warningRequired
                    && warningTransferSelection.fileInfo.fileName() == QStringLiteral("warning.bin")
                    && !warningTransferSelection.fileSize.isEmpty(),
                "transfer selection should preserve validation and file metadata") && ok;
    ok = expect(warningTransferSelection.rejectedStatusMessage == QString::fromUtf8("已取消发送文件"),
                "warning transfer selection should preserve warning-cancel status text") && ok;

    LocalFileSelectionResult rejectedTransferSelection = LocalFileManager::selectTransferFile(tooLargeFilePath, QStringLiteral("文件"));
    ok = expect(!rejectedTransferSelection.accepted
                    && !rejectedTransferSelection.canceled
                    && rejectedTransferSelection.rejectedStatusMessage.contains(QString::fromUtf8("超过 80 MB"))
                    && rejectedTransferSelection.rejectedStatusTimeoutMs == 2600,
                "rejected transfer selection should expose reusable failure UI feedback") && ok;

    LocalFileSelectionResult warningCanceledSelection = LocalFileManager::cancelTransferWarningSelection(QStringLiteral("文件"));
    ok = expect(warningCanceledSelection.warningCanceled
                    && warningCanceledSelection.warningCanceledHint == QString::fromUtf8("已取消发送文件")
                    && warningCanceledSelection.warningCanceledStatusMessage == QString::fromUtf8("已取消发送文件")
                    && warningCanceledSelection.warningCanceledStatusTimeoutMs == 2600,
                "warning-canceled transfer selection should expose reusable confirmation-cancel feedback") && ok;

    LocalTransferSelectionDecision canceledTransferDecision = LocalFileManager::transferSelectionDecision(QString(),
                                                                                                          QStringLiteral("文件"),
                                                                                                          QString::fromUtf8("文件发送已取消"),
                                                                                                          QString::fromUtf8("已取消选择文件"));
    ok = expect(!canceledTransferDecision.accepted
                    && canceledTransferDecision.action == LocalTransferSelectionDecision::Action::None
                    && canceledTransferDecision.hintText == QString::fromUtf8("文件发送已取消")
                    && canceledTransferDecision.statusMessage == QString::fromUtf8("已取消选择文件")
                    && canceledTransferDecision.statusTimeoutMs == 1600,
                "transfer selection decision should preserve canceled transfer UI feedback") && ok;

    LocalTransferSelectionDecision rejectedTransferDecision = LocalFileManager::transferSelectionDecision(tooLargeFilePath,
                                                                                                          QStringLiteral("文件"),
                                                                                                          QString(),
                                                                                                          QString());
    ok = expect(!rejectedTransferDecision.accepted
                    && rejectedTransferDecision.action == LocalTransferSelectionDecision::Action::ShowFailureDialog
                    && rejectedTransferDecision.dialogTitle == QString::fromUtf8("文件过大")
                    && rejectedTransferDecision.dialogMessage.contains(QString::fromUtf8("超过当前 80 MB"))
                    && rejectedTransferDecision.statusMessage.contains(QString::fromUtf8("超过 80 MB")),
                "transfer selection decision should centralize rejected-file dialog and status feedback") && ok;

    LocalTransferSelectionDecision warningTransferDecision = LocalFileManager::transferSelectionDecision(warningFilePath,
                                                                                                         QStringLiteral("文件"),
                                                                                                         QString(),
                                                                                                         QString());
    ok = expect(!warningTransferDecision.accepted
                    && warningTransferDecision.action == LocalTransferSelectionDecision::Action::ConfirmLargeFile
                    && warningTransferDecision.fileInfo.fileName() == QStringLiteral("warning.bin")
                    && warningTransferDecision.dialogTitle == QString::fromUtf8("确认发送大文件"),
                "transfer selection decision should centralize large-file confirmation prompts") && ok;

    LocalTransferSelectionDecision warningAcceptedDecision =
        LocalFileManager::resolveTransferSelectionWarning(warningTransferDecision, true, QStringLiteral("文件"));
    ok = expect(warningAcceptedDecision.accepted
                    && warningAcceptedDecision.action == LocalTransferSelectionDecision::Action::None
                    && warningAcceptedDecision.fileInfo.fileName() == QStringLiteral("warning.bin")
                    && warningAcceptedDecision.statusMessage.isEmpty(),
                "confirmed large-file decision should become accepted without extra UI feedback") && ok;

    LocalTransferSelectionDecision warningRejectedDecision =
        LocalFileManager::resolveTransferSelectionWarning(warningTransferDecision, false, QStringLiteral("文件"));
    ok = expect(!warningRejectedDecision.accepted
                    && warningRejectedDecision.action == LocalTransferSelectionDecision::Action::None
                    && warningRejectedDecision.hintText == QString::fromUtf8("已取消发送文件")
                    && warningRejectedDecision.statusMessage == QString::fromUtf8("已取消发送文件")
                    && warningRejectedDecision.statusTimeoutMs == 2600,
                "rejected large-file confirmation should reuse warning-canceled UI feedback") && ok;

    LocalFileValidationResult avatarOk = LocalFileManager::validateAvatarFile(QFileInfo(avatarFilePath));
    ok = expect(avatarOk.accepted, "normal avatar file should be accepted") && ok;

    LocalFileSelectionResult avatarSelection = LocalFileManager::selectAvatarFile(avatarFilePath);
    ok = expect(avatarSelection.accepted
                    && !avatarSelection.canceled
                    && avatarSelection.fileInfo.fileName() == QStringLiteral("avatar.png"),
                "avatar selection should preserve accepted file metadata") && ok;

    LocalFileSelectionResult canceledAvatarSelection = LocalFileManager::selectAvatarFile(QString());
    ok = expect(canceledAvatarSelection.canceled
                    && canceledAvatarSelection.canceledStatusMessage == QString::fromUtf8("已取消选择头像")
                    && canceledAvatarSelection.canceledStatusTimeoutMs == 1600,
                "empty avatar selection should expose reusable cancel feedback") && ok;

    LocalFileValidationResult avatarMissing = LocalFileManager::validateAvatarFile(QFileInfo(QDir(nestedDir).filePath("missing-avatar.png")));
    ok = expect(!avatarMissing.accepted && avatarMissing.statusMessage.contains(QString::fromUtf8("文件不可读取")),
                "missing avatar file should be rejected") && ok;

    LocalFileValidationResult avatarLarge = LocalFileManager::validateAvatarFile(QFileInfo(avatarLargePath));
    ok = expect(!avatarLarge.accepted && avatarLarge.statusMessage.contains(QString::fromUtf8("超过 10 MB")),
                "oversized avatar file should be rejected") && ok;

    LocalFileSelectionResult invalidAvatar = LocalFileManager::invalidAvatarDataResult();
    ok = expect(invalidAvatar.invalidDataTitle == QString::fromUtf8("头像上传失败")
                    && invalidAvatar.invalidDataMessage.contains(QString::fromUtf8("无法读取该图片"))
                    && invalidAvatar.invalidDataStatusMessage == QString::fromUtf8("头像上传失败：无法读取图片")
                    && invalidAvatar.invalidDataStatusTimeoutMs == 2200,
                "invalid avatar data result should centralize unreadable-image feedback") && ok;

    LocalFileSelectionResult avatarSaveFailed = LocalFileManager::avatarSaveFailedResult();
    ok = expect(avatarSaveFailed.saveFailedTitle == QString::fromUtf8("头像保存失败")
                    && avatarSaveFailed.saveFailedMessage.contains(QString::fromUtf8("保存到本地失败"))
                    && avatarSaveFailed.saveFailedStatusMessage == QString::fromUtf8("头像保存失败，请检查应用数据目录权限")
                    && avatarSaveFailed.saveFailedStatusTimeoutMs == 2600,
                "avatar save failed result should centralize local-save failure feedback") && ok;

    ok = expect(LocalFileManager::humanFileSize(512) == QStringLiteral("512 B")
                    && LocalFileManager::humanFileSize(2048).contains(QStringLiteral("KB"))
                    && LocalFileManager::humanFileSize(3 * 1024 * 1024).contains(QStringLiteral("MB")),
                "human file size should cover B KB MB") && ok;

    ok = expect(LocalFileManager::safeReceivedFileName(QStringLiteral("C:/downloads/report.zip"),
                                                       QStringLiteral("fallback.bin")) == QStringLiteral("report.zip")
                    && LocalFileManager::safeReceivedFileName(QStringLiteral("   "),
                                                              QStringLiteral("fallback.bin")) == QStringLiteral("fallback.bin"),
                "safe received file name should preserve leaf names and fallback when empty") && ok;

    const QString firstReceivedPath = LocalFileManager::uniqueReceivedSavePath(receiveDir, QStringLiteral("report.zip"));
    ok = expect(!firstReceivedPath.isEmpty() && firstReceivedPath.contains(QStringLiteral("report.zip")),
                "unique received save path should create a candidate name") && ok;
    ok = expect(LocalFileManager::writeReceivedTransferPayload(firstReceivedPath, QByteArray("payload")),
                "received payload writer should persist bytes") && ok;
    QFile firstReceivedFile(firstReceivedPath);
    ok = expect(firstReceivedFile.exists() && firstReceivedFile.size() == 7,
                "received payload writer should create the destination file") && ok;

    const QString secondReceivedPath = LocalFileManager::uniqueReceivedSavePath(receiveDir, QStringLiteral("report.zip"));
    ok = expect(secondReceivedPath != firstReceivedPath,
                "unique received save path should avoid collisions when a file already exists") && ok;

    ok = expect(LocalFileManager::extractSavePathFromChatText(
                    QString::fromUtf8("文件已接收 · 保存路径：C:/downloads/report.zip · 可打开")) == QStringLiteral("C:/downloads/report.zip"),
                "save path extractor should parse explicit save-path cards") && ok;
    ok = expect(LocalFileManager::extractSavePathFromChatText(
                    QString::fromUtf8("离线附件已恢复 · 自动保存:C:/downloads/cache.bin · 可重试")) == QStringLiteral("C:/downloads/cache.bin"),
                "save path extractor should parse auto-save cards with ascii colon") && ok;
    ok = expect(LocalFileManager::extractSavePathFromChatText(
                    QString::fromUtf8("离线附件已恢复 · 自动保存：C:/downloads/cache2.bin · 可重试")) == QStringLiteral("C:/downloads/cache2.bin"),
                "save path extractor should parse auto-save cards with full-width colon") && ok;
    ok = expect(LocalFileManager::extractSavePathFromChatText(
                    QString::fromUtf8("图片已保存到：C:/downloads/image.png\n完整性已验证")) == QStringLiteral("C:/downloads/image.png"),
                "save path extractor should parse saved-to multiline cards") && ok;

    const LocalSavedFileState primarySavedState =
        LocalFileManager::savedFileStateFromChatText(QString::fromUtf8("文件已接收 · 保存路径：") + firstReceivedPath,
                                                     QString());
    ok = expect(primarySavedState.hasSavePath
                    && primarySavedState.canOpenFile
                    && primarySavedState.canOpenFolder
                    && primarySavedState.fileInfo.absoluteFilePath() == firstReceivedPath,
                "saved file state should resolve an existing saved file and folder") && ok;

    const QString tooltipOnlyPath = QDir(receiveDir).filePath(QStringLiteral("tooltip-only.txt"));
    ok = expect(createFileWithSize(tooltipOnlyPath, 3) == tooltipOnlyPath,
                "tooltip-only saved file should be created") && ok;
    const LocalSavedFileState tooltipSavedState =
        LocalFileManager::savedFileStateFromChatText(QString::fromUtf8("普通消息"),
                                                     QString::fromUtf8("附件提示 · 已保存到：") + tooltipOnlyPath);
    ok = expect(tooltipSavedState.hasSavePath
                    && tooltipSavedState.canOpenFile
                    && tooltipSavedState.canOpenFolder
                    && tooltipSavedState.savePath == tooltipOnlyPath,
                "saved file state should fall back to tooltip text when chat text lacks a path") && ok;

    const QString missingSavedPath = QDir(receiveDir).filePath(QStringLiteral("missing.bin"));
    const LocalSavedFileState missingSavedState =
        LocalFileManager::savedFileStateFromChatText(QString::fromUtf8("附件已接收 · 保存路径：") + missingSavedPath,
                                                     QString());
    ok = expect(missingSavedState.hasSavePath
                    && !missingSavedState.canOpenFile
                    && missingSavedState.canOpenFolder,
                "saved file state should preserve open-folder ability when only the file is missing") && ok;

    QDir(tempRoot).removeRecursively();
    settings.clear();
    return ok ? 0 : 1;
}
