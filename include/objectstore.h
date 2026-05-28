#ifndef OBJECTSTORE_H
#define OBJECTSTORE_H

#include <QByteArray>
#include <QIODevice>
#include <QString>
#include <QStringList>

#include <memory>

class ObjectStore {
public:
    struct ValidationResult {
        bool ok = false;
        qint64 size = 0;
        QString fileHash;
        QString error;
    };

    virtual ~ObjectStore() = default;

    virtual bool writeObject(const QByteArray& data,
                             QString* objectKey,
                             QString* fileHash = nullptr,
                             QString* error = nullptr,
                             const QString& extension = QString()) const = 0;
    virtual ValidationResult validateObject(const QString& objectKey,
                                            qint64 expectedSize,
                                            const QString& expectedHash) const = 0;
    virtual std::unique_ptr<QIODevice> openObject(const QString& objectKey) const = 0;
    virtual bool removeObject(const QString& objectKey) const = 0;
    virtual int cleanupExpired(qint64 ttlMs, QStringList* removedKeys = nullptr) const = 0;
};

class FilesystemObjectStore : public ObjectStore {
public:
    explicit FilesystemObjectStore(const QString& rootDir);

    QString rootDir() const;
    static QString generateObjectKey(const QString& extension = QString());
    static bool isValidObjectKey(const QString& objectKey);

    QString objectPath(const QString& objectKey) const;
    bool writeObject(const QByteArray& data,
                     QString* objectKey,
                     QString* fileHash = nullptr,
                     QString* error = nullptr,
                     const QString& extension = QString()) const override;
    ValidationResult validateObject(const QString& objectKey,
                                    qint64 expectedSize,
                                    const QString& expectedHash) const override;
    std::unique_ptr<QIODevice> openObject(const QString& objectKey) const override;
    bool removeObject(const QString& objectKey) const override;
    int cleanupExpired(qint64 ttlMs, QStringList* removedKeys = nullptr) const override;

private:
    QString m_rootDir;
};

struct S3ObjectStoreConfig {
    QString endpoint;
    QString bucket;
    QString region;
    QString accessKey;
    QString secretKey;
    QString prefix;
    bool tlsVerify = true;
};

class S3ObjectStore : public ObjectStore {
public:
    explicit S3ObjectStore(const S3ObjectStoreConfig& config);

    S3ObjectStoreConfig config() const;

    bool writeObject(const QByteArray& data,
                     QString* objectKey,
                     QString* fileHash = nullptr,
                     QString* error = nullptr,
                     const QString& extension = QString()) const override;
    ValidationResult validateObject(const QString& objectKey,
                                    qint64 expectedSize,
                                    const QString& expectedHash) const override;
    std::unique_ptr<QIODevice> openObject(const QString& objectKey) const override;
    bool removeObject(const QString& objectKey) const override;
    int cleanupExpired(qint64 ttlMs, QStringList* removedKeys = nullptr) const override;

private:
    S3ObjectStoreConfig m_config;
};

QString normalizeObjectStoreType(const QString& storeType);
bool isSupportedObjectStoreType(const QString& storeType);
QString normalizeS3ObjectPrefix(const QString& prefix);
bool validateS3ObjectStoreConfig(const S3ObjectStoreConfig& config, QString* error = nullptr);
S3ObjectStoreConfig s3ObjectStoreConfigFromEnvironment();
std::unique_ptr<ObjectStore> createObjectStore(const QString& storeType,
                                               const QString& rootDir,
                                               QString* error = nullptr);

#endif // OBJECTSTORE_H
