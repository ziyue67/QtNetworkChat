#include "historymetadata.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QVariant>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning() << message;
        return false;
    }
    return true;
}

bool hasColumn(QSqlDatabase& db, const QString& columnName) {
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(chat_history)"))) {
        return false;
    }
    while (query.next()) {
        if (query.value(QStringLiteral("name")).toString() == columnName) {
            return true;
        }
    }
    return false;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("history_metadata_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    bool ok = true;
    const QString connectionName = QStringLiteral("history_metadata_test");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(QDir(appDataDir).filePath(QStringLiteral("history.sqlite3")));
        ok = expect(db.open(), "sqlite database should open") && ok;
        QSqlQuery query(db);
        ok = expect(query.exec(QStringLiteral("CREATE TABLE chat_history ("
                                             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                             "peer_id TEXT NOT NULL, "
                                             "content TEXT NOT NULL, "
                                             "created_at TEXT DEFAULT CURRENT_TIMESTAMP)")),
                    "legacy chat_history table should be created") && ok;
        query.prepare(QStringLiteral("INSERT INTO chat_history(peer_id, content, created_at) VALUES(?, ?, ?)"));
        query.addBindValue(QStringLiteral("920002"));
        query.addBindValue(QStringLiteral("[10:00] <Alice> legacy plaintext"));
        query.addBindValue(QStringLiteral("2026-06-04 10:00:00"));
        ok = expect(query.exec(), "legacy plaintext history row should insert") && ok;

        QString reason;
        ok = expect(ensureChatHistorySchema(db, &reason) && reason.isEmpty(),
                    "history schema migration should add encryption metadata") && ok;
        ok = expect(hasColumn(db, QStringLiteral("encryption_state"))
                        && hasColumn(db, QStringLiteral("e2e_key_id"))
                        && hasColumn(db, QStringLiteral("e2e_key_fingerprint")),
                    "history schema should contain e2e metadata columns") && ok;

        query.prepare(QStringLiteral("SELECT encryption_state FROM chat_history WHERE peer_id = ?"));
        query.addBindValue(QStringLiteral("920002"));
        ok = expect(query.exec() && query.next()
                        && query.value(0).toString() == QStringLiteral("plaintext"),
                    "legacy rows should migrate to plaintext state") && ok;

        query.prepare(QStringLiteral("INSERT INTO chat_history(peer_id, content, encryption_state, e2e_key_id, e2e_key_fingerprint, created_at) "
                                     "VALUES(?, ?, ?, ?, ?, ?)"));
        query.addBindValue(QStringLiteral("920002"));
        query.addBindValue(QStringLiteral("[10:01] <Alice> [端到端加密] secret"));
        query.addBindValue(QStringLiteral("encrypted"));
        query.addBindValue(QStringLiteral("rotate-1-response"));
        query.addBindValue(QString(64, QLatin1Char('a')));
        query.addBindValue(QStringLiteral("2026-06-04 10:01:00"));
        ok = expect(query.exec(), "encrypted history row should insert") && ok;

        query.prepare(QStringLiteral("INSERT INTO chat_history(peer_id, content, encryption_state, e2e_key_id, e2e_key_fingerprint, created_at) "
                                     "VALUES(?, ?, ?, ?, ?, ?)"));
        query.addBindValue(QStringLiteral("920002"));
        query.addBindValue(QStringLiteral("[10:02] <Bob> 加密消息无法解密"));
        query.addBindValue(QStringLiteral("decrypt-failed"));
        query.addBindValue(QStringLiteral("rotate-2-response"));
        query.addBindValue(QString(64, QLatin1Char('b')));
        query.addBindValue(QStringLiteral("2026-06-04 10:02:00"));
        ok = expect(query.exec(), "decrypt-failed history row should insert") && ok;

        ok = expect(normalizedHistoryEncryptionState(QStringLiteral("unknown")) == QStringLiteral("plaintext")
                        && historyEncryptionStateLabel(QStringLiteral("encrypted")) == QStringLiteral("端到端加密"),
                    "history state helpers should normalize and label states") && ok;
        const QString encryptedLine = formattedHistoryContent(QStringLiteral("[10:01] <Alice> secret"),
                                                              QStringLiteral("encrypted"),
                                                              QStringLiteral("rotate-1-response"),
                                                              QString(64, QLatin1Char('a')));
        ok = expect(encryptedLine.contains(QStringLiteral("端到端加密"))
                        && encryptedLine.contains(QStringLiteral("rotate-1-response"))
                        && encryptedLine.contains(QStringLiteral("fp:aaaaaaaaaaaaaaaa")),
                    "encrypted history formatting should expose state and safe key summary") && ok;
        const QString failedLine = formattedHistoryExportLine(QStringLiteral("2026-06-04 10:02:00"),
                                                              QStringLiteral("[10:02] <Bob> 加密消息无法解密"),
                                                              QStringLiteral("decrypt-failed"),
                                                              QStringLiteral("rotate-2-response"),
                                                              QString(64, QLatin1Char('b')));
        ok = expect(failedLine.contains(QStringLiteral("端到端加密解密失败"))
                        && failedLine.contains(QStringLiteral("rotate-2-response"))
                        && !failedLine.contains(QString(64, QLatin1Char('b'))),
                    "decrypt-failed export formatting should avoid full fingerprints") && ok;
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
