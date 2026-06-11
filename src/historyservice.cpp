#include "historyservice.h"

#include "historymetadata.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextStream>
#include <QVariant>

HistoryService::HistoryService(QString userId)
    : m_userId(std::move(userId)) {
}

QString HistoryService::userId() const {
    return m_userId;
}

void HistoryService::setUserId(const QString& userId) {
    m_userId = userId;
}

QString HistoryService::databasePath() const {
    return QDir(appDataDirectory()).filePath(QStringLiteral("client_%1.sqlite3")
                                             .arg(sanitizedId(m_userId, QStringLiteral("guest"))));
}

QString HistoryService::legacyFilePath(const QString& peerId) const {
    return QDir(appDataDirectory()).filePath(QStringLiteral("chat_history_%1.txt")
                                             .arg(sanitizedId(peerId, QStringLiteral("unknown"))));
}

bool HistoryService::ensureDatabase() const {
    const QString name = connectionName(QStringLiteral("init"));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(databasePath());
        if (db.open()) {
            ok = ensureChatHistorySchema(db);
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

bool HistoryService::save(const QString& peerId, const QString& content) const {
    return save(peerId, content, QStringLiteral("plaintext"));
}

bool HistoryService::save(const QString& peerId,
                          const QString& content,
                          const QString& encryptionState,
                          const QString& e2eKeyId,
                          const QString& e2eKeyFingerprint) const {
    if (peerId.isEmpty()) {
        return false;
    }
    if (saveToSqlite(peerId, content, encryptionState, e2eKeyId, e2eKeyFingerprint)) {
        return true;
    }

    QFile file(legacyFilePath(peerId));
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out << formattedHistoryContent(content, encryptionState, e2eKeyId, e2eKeyFingerprint) << "\n";
    return true;
}

bool HistoryService::saveToSqlite(const QString& peerId,
                                  const QString& content,
                                  const QString& encryptionState,
                                  const QString& e2eKeyId,
                                  const QString& e2eKeyFingerprint) const {
    if (peerId.isEmpty() || !ensureDatabase()) {
        return false;
    }

    const QString name = connectionName(QStringLiteral("write"));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(databasePath());
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("INSERT INTO chat_history(peer_id, content, created_at, encryption_state, e2e_key_id, e2e_key_fingerprint) "
                                         "VALUES(?, ?, datetime('now'), ?, ?, ?)"));
            query.addBindValue(peerId);
            query.addBindValue(content);
            query.addBindValue(normalizedHistoryEncryptionState(encryptionState));
            query.addBindValue(e2eKeyId.trimmed());
            query.addBindValue(e2eKeyFingerprint.trimmed());
            ok = query.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

bool HistoryService::hasRecords(const QString& peerId) const {
    if (peerId.isEmpty() || !ensureDatabase()) {
        return false;
    }

    const QString name = connectionName(QStringLiteral("count"));
    bool hasRows = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(databasePath());
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("SELECT 1 FROM chat_history WHERE peer_id = ? LIMIT 1"));
            query.addBindValue(peerId);
            hasRows = query.exec() && query.next();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return hasRows;
}

QStringList HistoryService::recentRows(const QString& peerId, int maxRows) const {
    QStringList rows;
    if (peerId.isEmpty()) {
        return rows;
    }

    const bool dbReady = ensureDatabase();
    if (dbReady) {
        const QString name = connectionName(QStringLiteral("read"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
            db.setDatabaseName(databasePath());
            if (db.open()) {
                QSqlQuery query(db);
                query.prepare(QStringLiteral("SELECT content, encryption_state, e2e_key_id, e2e_key_fingerprint FROM ("
                                             "SELECT id, content, encryption_state, e2e_key_id, e2e_key_fingerprint "
                                             "FROM chat_history WHERE peer_id = ? ORDER BY id DESC LIMIT ?"
                                             ") ORDER BY id ASC"));
                query.addBindValue(peerId);
                query.addBindValue(maxRows > 0 ? maxRows : DefaultMaxHistoryLines);
                if (query.exec()) {
                    while (query.next()) {
                        rows << formattedHistoryContent(query.value(0).toString(),
                                                        query.value(1).toString(),
                                                        query.value(2).toString(),
                                                        query.value(3).toString());
                    }
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(name);
        if (!rows.isEmpty()) {
            return rows;
        }
    }

    rows = legacyRecentRows(peerId, maxRows);
    if (dbReady) {
        for (const QString& line : rows) {
            saveToSqlite(peerId, line, QStringLiteral("plaintext"));
        }
    }
    return rows;
}

QStringList HistoryService::rowsForDate(const QString& peerId, const QDate& date) const {
    QStringList rows;
    if (peerId.isEmpty() || !date.isValid() || !ensureDatabase()) {
        return rows;
    }

    const QString name = connectionName(QStringLiteral("date"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(databasePath());
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("SELECT content, encryption_state, e2e_key_id, e2e_key_fingerprint FROM chat_history "
                                         "WHERE peer_id = ? AND date(created_at, 'localtime') = ? "
                                         "ORDER BY id ASC"));
            query.addBindValue(peerId);
            query.addBindValue(date.toString(QStringLiteral("yyyy-MM-dd")));
            if (query.exec()) {
                while (query.next()) {
                    rows << formattedHistoryContent(query.value(0).toString(),
                                                    query.value(1).toString(),
                                                    query.value(2).toString(),
                                                    query.value(3).toString());
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return rows;
}

QStringList HistoryService::rowsForExport(const QString& peerId) const {
    QStringList rows;
    if (peerId.isEmpty()) {
        return rows;
    }

    if (ensureDatabase()) {
        const QString name = connectionName(QStringLiteral("export"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
            db.setDatabaseName(databasePath());
            if (db.open()) {
                QSqlQuery query(db);
                query.prepare(QStringLiteral("SELECT created_at, content, encryption_state, e2e_key_id, e2e_key_fingerprint "
                                             "FROM chat_history WHERE peer_id = ? ORDER BY id ASC"));
                query.addBindValue(peerId);
                if (query.exec()) {
                    while (query.next()) {
                        rows << formattedHistoryExportLine(query.value(0).toString(),
                                                           query.value(1).toString(),
                                                           query.value(2).toString(),
                                                           query.value(3).toString(),
                                                           query.value(4).toString());
                    }
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(name);
    }

    if (!rows.isEmpty()) {
        return rows;
    }
    return legacyExportRows(peerId);
}

HistoryExportSelectionPlan HistoryService::exportSelectionPlan(const QString& sessionName,
                                                               const QDateTime& exportedAt) const {
    HistoryExportSelectionPlan plan;
    plan.dialogTitle = QStringLiteral("导出聊天记录");
    plan.filters = QStringLiteral("文本文件 (*.txt);;所有文件 (*.*)");
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (defaultDir.isEmpty()) {
        defaultDir = QDir::homePath();
    }
    plan.defaultPath = QDir(defaultDir).filePath(
        QStringLiteral("QtNetworkChat_%1_%2.txt")
            .arg(sanitizedFileSegment(sessionName, QStringLiteral("会话")),
                 exportedAt.toString(QStringLiteral("yyyyMMdd_hhmmss"))));
    plan.canceledStatusMessage = QStringLiteral("已取消导出聊天记录");
    plan.canceledStatusTimeoutMs = 1600;
    return plan;
}

HistoryExportWriteResult HistoryService::writeExportFile(const QString& savePath,
                                                         const QString& sessionName,
                                                         const QString& currentUserId,
                                                         const QString& currentUserName,
                                                         const QStringList& rows,
                                                         const QDateTime& exportedAt) const {
    HistoryExportWriteResult result;
    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.failureTitle = QStringLiteral("导出失败");
        result.failureMessage = QStringLiteral("无法写入导出文件，请检查保存位置权限。");
        result.failureStatusMessage = QStringLiteral("聊天记录导出失败");
        return result;
    }

    QTextStream out(&file);
    out << QStringLiteral("QtNetworkChat 聊天记录导出\n");
    out << QStringLiteral("会话: ") << sessionName << QStringLiteral("\n");
    out << QStringLiteral("账号: ") << currentUserId << QStringLiteral(" / ") << currentUserName << QStringLiteral("\n");
    out << QStringLiteral("导出时间: ") << exportedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")) << QStringLiteral("\n");
    out << QStringLiteral("记录数: ") << rows.size() << QStringLiteral("\n\n");
    for (const QString& row : rows) {
        out << row << QStringLiteral("\n");
    }
    file.close();

    result.written = true;
    result.successStatusMessage = QStringLiteral("已导出 %1 条聊天记录").arg(rows.size());
    result.successStatusTimeoutMs = 2600;
    result.systemMessage = QStringLiteral("已导出 %1 的聊天记录：%2").arg(sessionName, savePath);
    return result;
}

void HistoryService::clear(const QString& peerId) const {
    if (peerId.isEmpty() || !ensureDatabase()) {
        return;
    }

    const QString name = connectionName(QStringLiteral("clear"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(databasePath());
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("DELETE FROM chat_history WHERE peer_id = ?"));
            query.addBindValue(peerId);
            query.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
}

QString HistoryService::appDataDirectory() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) {
        dir = QStringLiteral(".");
    }
    QDir().mkpath(dir);
    return dir;
}

QString HistoryService::sanitizedId(const QString& value, const QString& fallback) const {
    QString safe = value.isEmpty() ? fallback : value;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    return safe;
}

QString HistoryService::sanitizedFileSegment(const QString& value, const QString& fallback) const {
    QString safe = value.simplified().trimmed();
    if (safe.isEmpty()) {
        safe = fallback;
    }
    safe.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    return safe;
}

QString HistoryService::connectionName(const QString& purpose) const {
    return QStringLiteral("history_service_%1_%2")
        .arg(purpose, QString::number(reinterpret_cast<quintptr>(this)));
}

QStringList HistoryService::legacyRecentRows(const QString& peerId, int maxRows) const {
    QStringList rows;
    QFile file(legacyFilePath(peerId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return rows;
    }

    QTextStream in(&file);
    const int limit = maxRows > 0 ? maxRows : DefaultMaxHistoryLines;
    while (!in.atEnd()) {
        rows << in.readLine();
        while (rows.size() > limit) {
            rows.removeFirst();
        }
    }
    return rows;
}

QStringList HistoryService::legacyExportRows(const QString& peerId) const {
    QStringList rows;
    QFile file(legacyFilePath(peerId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return rows;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        rows << in.readLine();
    }
    return rows;
}
