#include "localfilemanager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

QString LocalFileManager::lastTransferDirectory() {
    QSettings settings("QtNetworkChat", "QtNetworkChat");
    QString directory = settings.value("transfer/lastDirectory").toString();
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    if (directory.isEmpty() || !QDir(directory).exists()) {
        directory = QDir::homePath();
    }
    return directory;
}

void LocalFileManager::rememberTransferDirectory(const QString& filePath) {
    const QString directory = QFileInfo(filePath).absolutePath();
    if (directory.isEmpty() || !QDir(directory).exists()) {
        return;
    }

    QSettings settings("QtNetworkChat", "QtNetworkChat");
    settings.setValue("transfer/lastDirectory", directory);
}

QString LocalFileManager::lastAvatarDirectory() {
    QSettings settings("QtNetworkChat", "QtNetworkChat");
    QString directory = settings.value("avatar/lastDirectory").toString();
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    if (directory.isEmpty() || !QDir(directory).exists()) {
        directory = QDir::homePath();
    }
    return directory;
}

void LocalFileManager::rememberAvatarDirectory(const QString& filePath) {
    const QString directory = QFileInfo(filePath).absolutePath();
    if (directory.isEmpty() || !QDir(directory).exists()) {
        return;
    }

    QSettings settings("QtNetworkChat", "QtNetworkChat");
    settings.setValue("avatar/lastDirectory", directory);
}

LocalFileSelectionResult LocalFileManager::selectTransferFile(const QString& selectedPath,
                                                              const QString& confirmKind) {
    if (selectedPath.trimmed().isEmpty()) {
        return cancelTransferSelection(confirmKind);
    }

    LocalFileSelectionResult result;
    result.filePath = selectedPath;
    rememberTransferDirectory(selectedPath);
    result.fileInfo = QFileInfo(selectedPath);
    const LocalFileValidationResult validation = validateTransferFile(result.fileInfo, confirmKind);
    result.accepted = validation.accepted;
    result.warningRequired = validation.warningRequired;
    result.statusMessage = validation.statusMessage;
    result.failureTitle = validation.failureTitle;
    result.failureMessage = validation.failureMessage;
    result.warningTitle = validation.warningTitle;
    result.warningMessage = validation.warningMessage;
    result.rejectedStatusMessage = validation.statusMessage;
    if (validation.accepted) {
        result.fileSize = humanFileSize(result.fileInfo.size());
    } else {
        result.rejectedStatusTimeoutMs = 2600;
    }
    return result;
}

LocalFileSelectionResult LocalFileManager::selectAvatarFile(const QString& selectedPath) {
    if (selectedPath.trimmed().isEmpty()) {
        return cancelAvatarSelection();
    }

    LocalFileSelectionResult result;
    result.filePath = selectedPath;
    rememberAvatarDirectory(selectedPath);
    result.fileInfo = QFileInfo(selectedPath);
    const LocalFileValidationResult validation = validateAvatarFile(result.fileInfo);
    result.accepted = validation.accepted;
    result.statusMessage = validation.statusMessage;
    result.failureTitle = validation.failureTitle;
    result.failureMessage = validation.failureMessage;
    result.rejectedStatusMessage = validation.statusMessage;
    if (validation.accepted) {
        result.fileSize = humanFileSize(result.fileInfo.size());
    }
    return result;
}

LocalFileSelectionResult LocalFileManager::cancelTransferSelection(const QString& kind) {
    LocalFileSelectionResult result;
    result.canceled = true;
    result.canceledHint = QStringLiteral("%1发送已取消").arg(kind);
    result.canceledStatusMessage = QStringLiteral("已取消选择%1").arg(kind);
    result.canceledStatusTimeoutMs = 1600;
    return result;
}

LocalFileSelectionResult LocalFileManager::cancelTransferWarningSelection(const QString& kind) {
    LocalFileSelectionResult result;
    result.warningCanceled = true;
    result.warningCanceledHint = QStringLiteral("已取消发送%1").arg(kind);
    result.warningCanceledStatusMessage = result.warningCanceledHint;
    result.warningCanceledStatusTimeoutMs = 2600;
    return result;
}

LocalFileSelectionResult LocalFileManager::cancelAvatarSelection() {
    LocalFileSelectionResult result;
    result.canceled = true;
    result.canceledStatusMessage = QStringLiteral("已取消选择头像");
    result.canceledStatusTimeoutMs = 1600;
    return result;
}

LocalFileSelectionResult LocalFileManager::invalidAvatarDataResult() {
    LocalFileSelectionResult result;
    result.invalidDataTitle = QStringLiteral("头像上传失败");
    result.invalidDataMessage = QStringLiteral("无法读取该图片，请确认文件格式是否正确。");
    result.invalidDataStatusMessage = QStringLiteral("头像上传失败：无法读取图片");
    result.invalidDataStatusTimeoutMs = 2200;
    return result;
}

LocalFileSelectionResult LocalFileManager::avatarSaveFailedResult() {
    LocalFileSelectionResult result;
    result.saveFailedTitle = QStringLiteral("头像保存失败");
    result.saveFailedMessage = QStringLiteral("头像已读取，但保存到本地失败，请检查应用数据目录权限。");
    result.saveFailedStatusMessage = QStringLiteral("头像保存失败，请检查应用数据目录权限");
    result.saveFailedStatusTimeoutMs = 2600;
    return result;
}

QString LocalFileManager::safeReceivedFileName(const QString& rawName, const QString& fallbackName) {
    QString fileName = QFileInfo(rawName).fileName().trimmed();
    if (fileName.isEmpty()) {
        fileName = fallbackName;
    }
    return fileName;
}

QString LocalFileManager::ensureReceivedDownloadDirectory(const QString& downloadSubdir) {
    const QString saveDirPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
        + QStringLiteral("/QtNetworkChat/")
        + downloadSubdir;
    QDir().mkpath(saveDirPath);
    return saveDirPath;
}

QString LocalFileManager::uniqueReceivedSavePath(const QString& directoryPath, const QString& fileName) {
    const QDir directory(directoryPath);
    const QString stampedName = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss_")) + fileName;
    QString candidate = directory.filePath(stampedName);
    if (!QFileInfo::exists(candidate)) {
        return candidate;
    }

    const QFileInfo stampedInfo(stampedName);
    const QString suffix = stampedInfo.suffix();
    const QString baseName = stampedInfo.completeBaseName();
    for (int index = 2; index < 1000; ++index) {
        const QString numberedName = suffix.isEmpty()
            ? QStringLiteral("%1_%2").arg(baseName).arg(index)
            : QStringLiteral("%1_%2.%3").arg(baseName).arg(index).arg(suffix);
        candidate = directory.filePath(numberedName);
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    return directory.filePath(QStringLiteral("%1_%2").arg(stampedName).arg(QDateTime::currentMSecsSinceEpoch()));
}

bool LocalFileManager::writeReceivedTransferPayload(const QString& savePath, const QByteArray& fileData) {
    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const qint64 bytesWritten = file.write(fileData);
    file.close();
    return bytesWritten == fileData.size();
}

LocalFileValidationResult LocalFileManager::validateTransferFile(const QFileInfo& info, const QString& kind) {
    LocalFileValidationResult result;
    constexpr qint64 warningBytes = 20LL * 1024 * 1024;
    constexpr qint64 maxBytes = 80LL * 1024 * 1024;

    if (!info.exists() || !info.isFile()) {
        result.failureTitle = QStringLiteral("无法发送");
        result.failureMessage = QStringLiteral("请选择一个可读取的本地文件。");
        result.statusMessage = QStringLiteral("%1发送失败：文件不可读取").arg(kind);
        return result;
    }
    if (info.size() <= 0) {
        result.failureTitle = QStringLiteral("无法发送");
        result.failureMessage = QStringLiteral("文件为空，已取消发送。");
        result.statusMessage = QStringLiteral("%1发送失败：文件为空").arg(kind);
        return result;
    }
    if (info.size() > maxBytes) {
        result.failureTitle = QStringLiteral("文件过大");
        result.failureMessage = QStringLiteral("%1大小为 %2，超过当前 80 MB 的安全发送上限。")
                                    .arg(kind, humanFileSize(info.size()));
        result.statusMessage = QStringLiteral("%1发送失败：超过 80 MB").arg(kind);
        return result;
    }

    result.accepted = true;
    if (info.size() > warningBytes) {
        result.warningRequired = true;
        result.warningTitle = QStringLiteral("确认发送大文件");
        result.warningMessage = QStringLiteral("%1大小为 %2，发送时可能需要等待一会儿，是否继续？")
                                    .arg(kind, humanFileSize(info.size()));
        result.statusMessage = QStringLiteral("已取消发送%1").arg(kind);
    }
    return result;
}

LocalFileValidationResult LocalFileManager::validateAvatarFile(const QFileInfo& info) {
    LocalFileValidationResult result;
    constexpr qint64 maxAvatarBytes = 10LL * 1024 * 1024;

    if (!info.exists() || !info.isFile()) {
        result.failureTitle = QStringLiteral("头像上传失败");
        result.failureMessage = QStringLiteral("请选择一个可读取的本地图片文件。");
        result.statusMessage = QStringLiteral("头像上传失败：文件不可读取");
        return result;
    }
    if (info.size() <= 0) {
        result.failureTitle = QStringLiteral("头像上传失败");
        result.failureMessage = QStringLiteral("图片文件为空，请重新选择。");
        result.statusMessage = QStringLiteral("头像上传失败：图片文件为空");
        return result;
    }
    if (info.size() > maxAvatarBytes) {
        result.failureTitle = QStringLiteral("头像过大");
        result.failureMessage = QStringLiteral("头像图片大小为 %1，超过 10 MB 上限，请选择更小的图片。")
                                    .arg(humanFileSize(info.size()));
        result.statusMessage = QStringLiteral("头像上传失败：图片超过 10 MB");
        return result;
    }

    result.accepted = true;
    return result;
}

QString LocalFileManager::humanFileSize(qint64 bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(qMax<qint64>(1, bytes / 1024));
    return QString::number(bytes / 1024.0 / 1024.0, 'f', 1) + " MB";
}
