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

    LocalFileValidationResult avatarOk = LocalFileManager::validateAvatarFile(QFileInfo(avatarFilePath));
    ok = expect(avatarOk.accepted, "normal avatar file should be accepted") && ok;

    LocalFileValidationResult avatarMissing = LocalFileManager::validateAvatarFile(QFileInfo(QDir(nestedDir).filePath("missing-avatar.png")));
    ok = expect(!avatarMissing.accepted && avatarMissing.statusMessage.contains(QString::fromUtf8("文件不可读取")),
                "missing avatar file should be rejected") && ok;

    LocalFileValidationResult avatarLarge = LocalFileManager::validateAvatarFile(QFileInfo(avatarLargePath));
    ok = expect(!avatarLarge.accepted && avatarLarge.statusMessage.contains(QString::fromUtf8("超过 10 MB")),
                "oversized avatar file should be rejected") && ok;

    ok = expect(LocalFileManager::humanFileSize(512) == QStringLiteral("512 B")
                    && LocalFileManager::humanFileSize(2048).contains(QStringLiteral("KB"))
                    && LocalFileManager::humanFileSize(3 * 1024 * 1024).contains(QStringLiteral("MB")),
                "human file size should cover B KB MB") && ok;

    QDir(tempRoot).removeRecursively();
    settings.clear();
    return ok ? 0 : 1;
}
