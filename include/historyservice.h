#ifndef HISTORYSERVICE_H
#define HISTORYSERVICE_H

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>

struct HistoryExportSelectionPlan {
    QString dialogTitle;
    QString filters;
    QString defaultPath;
    QString canceledStatusMessage;
    int canceledStatusTimeoutMs = 1600;
};

struct HistoryExportWriteResult {
    bool written = false;
    QString failureTitle;
    QString failureMessage;
    QString failureStatusMessage;
    int failureStatusTimeoutMs = 2600;
    QString successStatusMessage;
    int successStatusTimeoutMs = 2600;
    QString systemMessage;
};

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
    HistoryExportSelectionPlan exportSelectionPlan(const QString& sessionName,
                                                   const QDateTime& exportedAt = QDateTime::currentDateTime()) const;
    HistoryExportWriteResult writeExportFile(const QString& savePath,
                                             const QString& sessionName,
                                             const QString& currentUserId,
                                             const QString& currentUserName,
                                             const QStringList& rows,
                                             const QDateTime& exportedAt = QDateTime::currentDateTime()) const;
    void clear(const QString& peerId) const;

private:
    QString appDataDirectory() const;
    QString sanitizedId(const QString& value, const QString& fallback) const;
    QString sanitizedFileSegment(const QString& value, const QString& fallback) const;
    QString connectionName(const QString& purpose) const;
    QStringList legacyRecentRows(const QString& peerId, int maxRows) const;
    QStringList legacyExportRows(const QString& peerId) const;

    QString m_userId;
};

#endif // HISTORYSERVICE_H
