#pragma once

#include <QJsonObject>
#include <QSqlDatabase>
#include <QStringList>

class QSqlError;
class QSqlQuery;

// Shared server persistence boundary: driver differences, connection ownership, and health.
namespace ServerDatabase {
qint64 positiveIntegerEnvOrDefault(const char* name, qint64 defaultValue, qint64 maxValue = 0);
QString appDataDir();
QString accountDatabaseDriver();
bool accountDatabaseIsPostgres();
QString accountDatabasePath();
bool execAccountDatabaseQuery(QSqlQuery& query, const QString& scope, const QString& sql = QString());
QSqlDatabase openAccountDatabase(const QString& connectionName);
bool openAccountDatabaseConnection(QSqlDatabase& db, const QString& scope);
void releaseAccountDatabase(const QString& connectionName);
QJsonObject accountDatabasePoolSnapshot();
QJsonObject redactedAccountDatabaseConfig();
QJsonObject databaseErrorJson(const QString& scope, const QSqlError& error);
QString insertIgnoreSql(const QString& table,
                        const QStringList& columns,
                        const QStringList& values,
                        const QStringList& conflictColumns);
QString insertReplaceSql(const QString& table,
                         const QStringList& columns,
                         const QStringList& values,
                         const QStringList& conflictColumns,
                         const QStringList& updateAssignments);
QString autoIdColumnSql();
QString currentTimestampPlusDaysSql(int days);
QString accountUidDefaultSql();
} // namespace ServerDatabase
