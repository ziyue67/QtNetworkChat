#ifndef OBJECTSTORE_H
#define OBJECTSTORE_H

#include <QString>
#include <QStringList>

class FilesystemObjectStore {
public:
    struct ValidationResult {
        bool ok = false;
        qint64 size = 0;
        QString fileHash;
        QString error;
    };

    explicit FilesystemObjectStore(const QString& rootDir);

    QString rootDir() const;
    static QString generateObjectKey(const QString& extension = QString());
    static bool isValidObjectKey(const QString& objectKey);

    QString objectPath(const QString& objectKey) const;
    bool writeObject(const QByteArray& data,
                     QString* objectKey,
                     QString* fileHash = nullptr,
                     QString* error = nullptr,
                     const QString& extension = QString()) const;
    ValidationResult validateObject(const QString& objectKey,
                                    qint64 expectedSize,
                                    const QString& expectedHash) const;
    int cleanupExpired(qint64 ttlMs, QStringList* removedKeys = nullptr) const;

private:
    QString m_rootDir;
};

#endif // OBJECTSTORE_H
