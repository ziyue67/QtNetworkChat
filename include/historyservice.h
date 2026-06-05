#ifndef HISTORYSERVICE_H
#define HISTORYSERVICE_H

#include <QDate>
#include <QString>
#include <QStringList>

class HistoryService {
public:
    static constexpr int DefaultMaxHistoryLines = 500;

    explicit HistoryService(QString userId = QString());

    QString userId() const;
    void setUserId(const QString& userId);

    QString databasePath() const;
    QString legacyFilePath(const QString& peerId) const;

    bool ensureDatabase() const;
    bool save(const QString& peerId, const QString& content) const;
    bool save(const QString& peerId,
              const QString& content,
              const QString& encryptionState,
              const QString& e2eKeyId = QString(),
              const QString& e2eKeyFingerprint = QString()) const;
    bool saveToSqlite(const QString& peerId,
                      const QString& content,
                      const QString& encryptionState = QStringLiteral("plaintext"),
                      const QString& e2eKeyId = QString(),
                      const QString& e2eKeyFingerprint = QString()) const;
    bool hasRecords(const QString& peerId) const;
    QStringList recentRows(const QString& peerId, int maxRows = DefaultMaxHistoryLines) const;
    QStringList rowsForDate(const QString& peerId, const QDate& date) const;
    QStringList rowsForExport(const QString& peerId) const;
    void clear(const QString& peerId) const;

private:
    QString appDataDirectory() const;
    QString sanitizedId(const QString& value, const QString& fallback) const;
    QString connectionName(const QString& purpose) const;
    QStringList legacyRecentRows(const QString& peerId, int maxRows) const;
    QStringList legacyExportRows(const QString& peerId) const;

    QString m_userId;
};

#endif // HISTORYSERVICE_H
