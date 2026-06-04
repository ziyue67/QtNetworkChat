#ifndef HISTORYMETADATA_H
#define HISTORYMETADATA_H

#include <QString>

class QSqlDatabase;

QString normalizedHistoryEncryptionState(const QString& state);
QString historyEncryptionStateLabel(const QString& state);
QString formattedHistoryContent(const QString& content,
                                const QString& encryptionState,
                                const QString& keyId = QString(),
                                const QString& keyFingerprint = QString());
QString formattedHistoryExportLine(const QString& createdAt,
                                   const QString& content,
                                   const QString& encryptionState,
                                   const QString& keyId = QString(),
                                   const QString& keyFingerprint = QString());
bool ensureChatHistorySchema(QSqlDatabase& db, QString* reason = nullptr);

#endif // HISTORYMETADATA_H
