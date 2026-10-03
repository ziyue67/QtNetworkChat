#include "historymetadata.h"

#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>

namespace {
const char kPlaintextState[] = "plaintext";
const char kEncryptedState[] = "encrypted";
const char kDecryptFailedState[] = "decrypt-failed";

bool execSql(QSqlQuery& query, const QString& sql, QString* reason) {
    if (query.exec(sql)) {
        return true;
    }
    if (reason) {
        *reason = query.lastError().text();
    }
    return false;
}
}

QString normalizedHistoryEncryptionState(const QString& state) {
    const QString normalized = state.trimmed().toLower();
    if (normalized == QLatin1String(kEncryptedState)
        || normalized == QLatin1String(kDecryptFailedState)) {
        return normalized;
    }
    return QString::fromLatin1(kPlaintextState);
}

QString historyEncryptionStateLabel(const QString& state) {
    const QString normalized = normalizedHistoryEncryptionState(state);
    if (normalized == QLatin1String(kEncryptedState)) {
        return QStringLiteral("端到端加密");
    }
    if (normalized == QLatin1String(kDecryptFailedState)) {
        return QStringLiteral("端到端加密解密失败");
    }
    return QStringLiteral("明文");
}

QString formattedHistoryContent(const QString& content,
                                const QString& encryptionState,
                                const QString& keyId,
                                const QString& keyFingerprint) {
    const QString normalized = normalizedHistoryEncryptionState(encryptionState);
    if (normalized == QLatin1String(kPlaintextState)) {
        return content;
    }

    QStringList details;
    details << historyEncryptionStateLabel(normalized);
    if (!keyId.trimmed().isEmpty()) {
        details << QStringLiteral("key:%1").arg(keyId.trimmed());
    }
    if (!keyFingerprint.trimmed().isEmpty()) {
        details << QStringLiteral("fp:%1").arg(keyFingerprint.trimmed().left(16));
    }
    return QStringLiteral("[历史:%1] %2").arg(details.join(QStringLiteral(" · ")), content);
}

QString formattedHistoryExportLine(const QString& createdAt,
                                   const QString& content,
                                   const QString& encryptionState,
                                   const QString& keyId,
                                   const QString& keyFingerprint) {
    return QStringLiteral("%1 | %2").arg(createdAt,
                                         formattedHistoryContent(content, encryptionState, keyId, keyFingerprint));
}

bool ensureChatHistorySchema(QSqlDatabase& db, QString* reason) {
    if (reason) {
        reason->clear();
    }
    if (!db.isOpen()) {
        if (reason) {
            *reason = QStringLiteral("database-not-open");
        }
        return false;
    }

    QSqlQuery query(db);
    if (!execSql(query,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS chat_history ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                "peer_id TEXT NOT NULL, "
                                "content TEXT NOT NULL, "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "encryption_state TEXT DEFAULT 'plaintext', "
                                "e2e_key_id TEXT, "
                                "e2e_key_fingerprint TEXT)"),
                 reason)) {
        return false;
    }

    QSet<QString> columns;
    if (!query.exec(QStringLiteral("PRAGMA table_info(chat_history)"))) {
        if (reason) {
            *reason = query.lastError().text();
        }
        return false;
    }
    while (query.next()) {
        columns.insert(query.value(QStringLiteral("name")).toString());
    }

    const QList<QPair<QString, QString>> requiredColumns = {
        {QStringLiteral("encryption_state"), QStringLiteral("TEXT DEFAULT 'plaintext'")},
        {QStringLiteral("e2e_key_id"), QStringLiteral("TEXT")},
        {QStringLiteral("e2e_key_fingerprint"), QStringLiteral("TEXT")}
    };
    for (const auto& column : requiredColumns) {
        if (!columns.contains(column.first)) {
            QSqlQuery alter(db);
            if (!execSql(alter,
                         QStringLiteral("ALTER TABLE chat_history ADD COLUMN %1 %2")
                            .arg(column.first, column.second),
                         reason)) {
                return false;
            }
        }
    }

    QSqlQuery normalize(db);
    if (!execSql(normalize,
                 QStringLiteral("UPDATE chat_history "
                                "SET encryption_state = 'plaintext' "
                                "WHERE encryption_state IS NULL OR trim(encryption_state) = ''"),
                 reason)) {
        return false;
    }
    if (!execSql(query,
                 QStringLiteral("CREATE INDEX IF NOT EXISTS idx_chat_history_peer_id "
                                "ON chat_history(peer_id, id)"),
                 reason)) {
        return false;
    }
    if (!execSql(query,
                 QStringLiteral("CREATE INDEX IF NOT EXISTS idx_chat_history_encryption_state "
                                "ON chat_history(peer_id, encryption_state, id)"),
                 reason)) {
        return false;
    }
    return true;
}
