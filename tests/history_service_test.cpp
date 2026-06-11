#include "historyservice.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextStream>
#include <QVariant>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning() << message;
        return false;
    }
    return true;
}

bool updateCreatedAt(const HistoryService& service,
                     const QString& peerId,
                     const QString& content,
                     const QString& createdAt) {
    const QString connectionName = QStringLiteral("history_service_test_update");
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(service.databasePath());
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("UPDATE chat_history SET created_at = ? WHERE peer_id = ? AND content = ?"));
            query.addBindValue(createdAt);
            query.addBindValue(peerId);
            query.addBindValue(content);
            ok = query.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QtNetworkChatTests"));
    QCoreApplication::setApplicationName(QStringLiteral("history_service_test"));
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    bool ok = true;
    const QString peerId = QStringLiteral("peer:unsafe/42");
    HistoryService service(QStringLiteral("user:unsafe/1"));

    ok = expect(service.databasePath().endsWith(QStringLiteral("client_user_unsafe_1.sqlite3")),
                "database path should sanitize user id") && ok;
    ok = expect(service.legacyFilePath(peerId).endsWith(QStringLiteral("chat_history_peer_unsafe_42.txt")),
                "legacy file path should sanitize peer id") && ok;

    ok = expect(service.ensureDatabase(), "history database should initialize") && ok;
    ok = expect(!service.hasRecords(peerId), "new peer should not have rows") && ok;

    ok = expect(service.save(peerId, QStringLiteral("[10:00] <Alice> plaintext")),
                "plaintext row should save") && ok;
    ok = expect(service.save(peerId,
                             QStringLiteral("[10:01] <Alice> encrypted"),
                             QStringLiteral("encrypted"),
                             QStringLiteral("rotate-7-response"),
                             QString(64, QLatin1Char('c'))),
                "encrypted row should save") && ok;
    ok = expect(updateCreatedAt(service, peerId, QStringLiteral("[10:00] <Alice> plaintext"), QStringLiteral("2026-06-05 10:00:00"))
                    && updateCreatedAt(service, peerId, QStringLiteral("[10:01] <Alice> encrypted"), QStringLiteral("2026-06-05 10:01:00")),
                "test timestamps should update") && ok;
    ok = expect(service.hasRecords(peerId), "saved peer should have rows") && ok;

    const QStringList recentOne = service.recentRows(peerId, 1);
    ok = expect(recentOne.size() == 1
                    && recentOne.first().contains(QStringLiteral("端到端加密"))
                    && recentOne.first().contains(QStringLiteral("fp:cccccccccccccccc")),
                "recent rows should honor limit and format encryption metadata") && ok;

    const QStringList dateRows = service.rowsForDate(peerId, QDate(2026, 6, 5));
    ok = expect(dateRows.size() == 2
                    && dateRows.first().contains(QStringLiteral("plaintext"))
                    && dateRows.last().contains(QStringLiteral("rotate-7-response")),
                "date rows should preserve order and formatted metadata") && ok;

    const QStringList exportRows = service.rowsForExport(peerId);
    ok = expect(exportRows.size() == 2
                    && exportRows.first().startsWith(QStringLiteral("2026-06-05 10:00:00 | "))
                    && !exportRows.last().contains(QString(64, QLatin1Char('c'))),
                "export rows should include created_at and redact full fingerprints") && ok;

    const QDateTime exportedAt = QDateTime::fromString(QStringLiteral("2026-06-11T23:15:00"), Qt::ISODate);
    const HistoryExportSelectionPlan exportPlan =
        service.exportSelectionPlan(QString::fromUtf8("公共聊天室/项目群"), exportedAt);
    ok = expect(exportPlan.dialogTitle == QString::fromUtf8("导出聊天记录")
                    && exportPlan.filters.contains(QStringLiteral("*.txt"))
                    && exportPlan.defaultPath.contains(QStringLiteral("QtNetworkChat_公共聊天室_项目群_20260611_231500.txt"))
                    && exportPlan.canceledStatusMessage == QString::fromUtf8("已取消导出聊天记录")
                    && exportPlan.canceledStatusTimeoutMs == 1600,
                "export selection plan should centralize dialog copy, default path, and cancel feedback") && ok;

    const QString exportOutputPath = QDir(appDataDir).filePath(QStringLiteral("history-export.txt"));
    const HistoryExportWriteResult exportWriteResult =
        service.writeExportFile(exportOutputPath,
                                QString::fromUtf8("公共聊天室"),
                                QStringLiteral("10001"),
                                QString::fromUtf8("测试用户"),
                                exportRows,
                                exportedAt);
    ok = expect(exportWriteResult.written
                    && exportWriteResult.successStatusMessage == QString::fromUtf8("已导出 2 条聊天记录")
                    && exportWriteResult.successStatusTimeoutMs == 2600
                    && exportWriteResult.systemMessage == QString::fromUtf8("已导出 公共聊天室 的聊天记录：") + exportOutputPath,
                "export writer should centralize success status and system message") && ok;
    QFile exportedFile(exportOutputPath);
    ok = expect(exportedFile.open(QIODevice::ReadOnly | QIODevice::Text),
                "history export output should be readable after writing") && ok;
    const QString exportedContent = QString::fromUtf8(exportedFile.readAll());
    exportedFile.close();
    ok = expect(exportedContent.contains(QString::fromUtf8("QtNetworkChat 聊天记录导出"))
                    && exportedContent.contains(QString::fromUtf8("会话: 公共聊天室"))
                    && exportedContent.contains(QString::fromUtf8("账号: 10001 / 测试用户"))
                    && exportedContent.contains(QString::fromUtf8("记录数: 2"))
                    && exportedContent.contains(exportRows.first())
                    && exportedContent.contains(exportRows.last()),
                "export writer should render header metadata and all exported rows") && ok;

    const QString exportMissingDirPath = QDir(appDataDir).filePath(QStringLiteral("missing-dir/history-export.txt"));
    const HistoryExportWriteResult exportFailedResult =
        service.writeExportFile(exportMissingDirPath,
                                QString::fromUtf8("公共聊天室"),
                                QStringLiteral("10001"),
                                QString::fromUtf8("测试用户"),
                                exportRows,
                                exportedAt);
    ok = expect(!exportFailedResult.written
                    && exportFailedResult.failureTitle == QString::fromUtf8("导出失败")
                    && exportFailedResult.failureMessage == QString::fromUtf8("无法写入导出文件，请检查保存位置权限。")
                    && exportFailedResult.failureStatusMessage == QString::fromUtf8("聊天记录导出失败")
                    && exportFailedResult.failureStatusTimeoutMs == 2600,
                "export writer should centralize failure dialog and status feedback when the destination cannot be opened") && ok;

    service.clear(peerId);
    ok = expect(!service.hasRecords(peerId)
                    && service.recentRows(peerId, 5).isEmpty()
                    && service.rowsForExport(peerId).isEmpty(),
                "clear should remove sqlite rows") && ok;

    const QString legacyPeerId = QStringLiteral("legacy-peer");
    {
        QFile legacy(service.legacyFilePath(legacyPeerId));
        ok = expect(legacy.open(QIODevice::WriteOnly | QIODevice::Text),
                    "legacy file should open for fixture write") && ok;
        QTextStream out(&legacy);
        out << "legacy-one\nlegacy-two\nlegacy-three\n";
    }
    const QStringList legacyRecent = service.recentRows(legacyPeerId, 2);
    ok = expect(legacyRecent == QStringList({QStringLiteral("legacy-two"), QStringLiteral("legacy-three")}),
                "legacy fallback should return the newest bounded rows") && ok;
    ok = expect(service.hasRecords(legacyPeerId),
                "legacy fallback should import rows into sqlite when database is available") && ok;
    const QStringList importedExport = service.rowsForExport(legacyPeerId);
    ok = expect(importedExport.size() == 2
                    && importedExport.first().contains(QStringLiteral("legacy-two"))
                    && importedExport.last().contains(QStringLiteral("legacy-three")),
                "imported legacy rows should become exportable") && ok;

    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
