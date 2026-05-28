#include "objectstore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <memory>

namespace {
QString normalizeExtension(const QString& extension) {
    QString normalized = extension.trimmed();
    if (normalized.isEmpty()) {
        return QString();
    }
    if (normalized.startsWith('.')) {
        normalized.remove(0, 1);
    }
    static const QRegularExpression validExtension(QStringLiteral("^[A-Za-z0-9_-]{1,32}$"));
    if (!validExtension.match(normalized).hasMatch()) {
        return QString();
    }
    return "." + normalized;
}

QString sha256Hex(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString fileSha256Hex(QFile& file) {
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(256 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            return QString();
        }
        hasher.addData(chunk);
    }
    return QString::fromLatin1(hasher.result().toHex());
}
}

FilesystemObjectStore::FilesystemObjectStore(const QString& rootDir)
    : m_rootDir(QDir::cleanPath(rootDir)) {
}

QString FilesystemObjectStore::rootDir() const {
    return m_rootDir;
}

QString FilesystemObjectStore::generateObjectKey(const QString& extension) {
    return QUuid::createUuid().toString(QUuid::WithoutBraces) + normalizeExtension(extension);
}

bool FilesystemObjectStore::isValidObjectKey(const QString& objectKey) {
    static const QRegularExpression validKey(QStringLiteral("^[A-Za-z0-9_-]{8,64}(\\.[A-Za-z0-9_-]{1,32})?$"));
    const QString trimmed = objectKey.trimmed();
    return trimmed == objectKey
        && !trimmed.isEmpty()
        && !trimmed.contains("..")
        && !trimmed.contains('/')
        && !trimmed.contains('\\')
        && !trimmed.contains(':')
        && validKey.match(trimmed).hasMatch();
}

QString FilesystemObjectStore::objectPath(const QString& objectKey) const {
    if (m_rootDir.isEmpty() || !isValidObjectKey(objectKey)) {
        return QString();
    }

    const QString rootPath = QDir(m_rootDir).absolutePath();
    const QString candidatePath = QDir(rootPath).absoluteFilePath(objectKey);
    const QString cleanRoot = QDir::cleanPath(rootPath);
    const QString cleanCandidate = QDir::cleanPath(candidatePath);
#ifdef Q_OS_WIN
    const bool insideRoot = cleanCandidate.compare(cleanRoot, Qt::CaseInsensitive) != 0
        && cleanCandidate.startsWith(cleanRoot + "/", Qt::CaseInsensitive);
#else
    const bool insideRoot = cleanCandidate != cleanRoot
        && cleanCandidate.startsWith(cleanRoot + "/");
#endif
    return insideRoot ? cleanCandidate : QString();
}

bool FilesystemObjectStore::writeObject(const QByteArray& data,
                                        QString* objectKey,
                                        QString* fileHash,
                                        QString* error,
                                        const QString& extension) const {
    if (objectKey) {
        objectKey->clear();
    }
    if (fileHash) {
        fileHash->clear();
    }
    if (error) {
        error->clear();
    }
    if (m_rootDir.isEmpty()) {
        if (error) *error = QStringLiteral("对象存储根目录未配置");
        return false;
    }
    if (!QDir().mkpath(m_rootDir)) {
        if (error) *error = QStringLiteral("对象存储根目录不可写");
        return false;
    }

    for (int attempt = 0; attempt < 8; ++attempt) {
        const QString key = generateObjectKey(extension);
        const QString path = objectPath(key);
        if (path.isEmpty() || QFile::exists(path)) {
            continue;
        }

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) *error = file.errorString();
            return false;
        }
        if (file.write(data) != data.size()) {
            if (error) *error = file.errorString();
            return false;
        }
        if (!file.commit()) {
            if (error) *error = file.errorString();
            return false;
        }
        if (objectKey) *objectKey = key;
        if (fileHash) *fileHash = sha256Hex(data);
        return true;
    }

    if (error) *error = QStringLiteral("无法生成唯一对象 key");
    return false;
}

FilesystemObjectStore::ValidationResult FilesystemObjectStore::validateObject(const QString& objectKey,
                                                                              qint64 expectedSize,
                                                                              const QString& expectedHash) const {
    ValidationResult result;
    const QString path = objectPath(objectKey);
    if (path.isEmpty()) {
        result.error = QStringLiteral("对象 key 非法");
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("对象不存在或不可读");
        return result;
    }

    result.size = file.size();
    if (expectedSize >= 0 && result.size != expectedSize) {
        result.error = QStringLiteral("对象大小不一致");
        return result;
    }

    result.fileHash = fileSha256Hex(file);
    if (result.fileHash.isEmpty()) {
        result.error = QStringLiteral("对象哈希计算失败");
        return result;
    }
    if (!expectedHash.trimmed().isEmpty()
        && result.fileHash.compare(expectedHash.trimmed(), Qt::CaseInsensitive) != 0) {
        result.error = QStringLiteral("对象哈希不一致");
        return result;
    }

    result.ok = true;
    return result;
}

std::unique_ptr<QIODevice> FilesystemObjectStore::openObject(const QString& objectKey) const {
    const QString path = objectPath(objectKey);
    if (path.isEmpty()) {
        return {};
    }

    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) {
        return {};
    }
    return file;
}

bool FilesystemObjectStore::removeObject(const QString& objectKey) const {
    const QString path = objectPath(objectKey);
    return !path.isEmpty() && QFile::remove(path);
}

int FilesystemObjectStore::cleanupExpired(qint64 ttlMs, QStringList* removedKeys) const {
    if (removedKeys) {
        removedKeys->clear();
    }
    if (ttlMs < 0 || m_rootDir.isEmpty() || !QDir(m_rootDir).exists()) {
        return 0;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    int removed = 0;
    QDirIterator it(m_rootDir, QDir::Files | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        const QString key = info.fileName();
        if (!isValidObjectKey(key)) {
            continue;
        }
        if (info.lastModified().toUTC().msecsTo(now) <= ttlMs) {
            continue;
        }
        if (QFile::remove(path)) {
            ++removed;
            if (removedKeys) {
                removedKeys->append(key);
            }
        }
    }
    return removed;
}

QString normalizeObjectStoreType(const QString& storeType) {
    const QString normalized = storeType.trimmed().toLower();
    return normalized.isEmpty() ? QStringLiteral("filesystem") : normalized;
}

bool isSupportedObjectStoreType(const QString& storeType) {
    return normalizeObjectStoreType(storeType) == QStringLiteral("filesystem");
}

std::unique_ptr<ObjectStore> createObjectStore(const QString& storeType,
                                               const QString& rootDir,
                                               QString* error) {
    if (error) {
        error->clear();
    }

    const QString normalizedType = normalizeObjectStoreType(storeType);
    if (normalizedType != QStringLiteral("filesystem")) {
        if (error) {
            *error = QStringLiteral("对象存储后端暂不支持: %1").arg(normalizedType);
        }
        return {};
    }

    const QString cleanRoot = QDir::cleanPath(rootDir.trimmed());
    if (cleanRoot.isEmpty()) {
        if (error) {
            *error = QStringLiteral("对象存储根目录未配置");
        }
        return {};
    }

    return std::make_unique<FilesystemObjectStore>(cleanRoot);
}
