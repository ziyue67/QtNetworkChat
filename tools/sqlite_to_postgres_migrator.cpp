#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>

struct Options {
    QString mode = "plan";
    QString sqlitePath;
    QString jsonPath;
    QString pgHost = "127.0.0.1";
    int pgPort = 5432;
    QString pgDatabase = "qtnetworkchat";
    QString pgUser = "postgres";
    QString pgPassword;
    bool createSample = false;
    QString sampleOwnerId = "910001";
    QString samplePeerId = "910002";
};

struct TableSpec {
    QString name;
    QStringList columns;
    QStringList conflictColumns;
    QString createSql;
};

QVector<TableSpec> tableSpecs() {
    const QString id = "id BIGSERIAL PRIMARY KEY, ";
    return {
        {"accounts",
         {"account", "password_hash", "user_name", "created_at", "updated_at", "last_login_at", "last_login_address", "login_count"},
         {"account"},
         "CREATE TABLE IF NOT EXISTS accounts (account TEXT PRIMARY KEY, password_hash TEXT NOT NULL, user_name TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, last_login_at TEXT, last_login_address TEXT, login_count INTEGER DEFAULT 0)"},
        {"user_sessions",
         {"id", "user_id", "user_name", "event_name", "peer_address", "peer_port", "created_at"},
         {"id"},
         "CREATE TABLE IF NOT EXISTS user_sessions (" + id + "user_id TEXT NOT NULL, user_name TEXT NOT NULL, event_name TEXT NOT NULL, peer_address TEXT, peer_port INTEGER, created_at TEXT DEFAULT CURRENT_TIMESTAMP)"},
        {"messages",
         {"id", "message_type", "sender_id", "sender_name", "receiver_id", "content", "file_name", "file_size", "file_hash", "file_chunk_size", "file_chunk_count", "delivery_state", "created_at"},
         {"id"},
         "CREATE TABLE IF NOT EXISTS messages (" + id + "message_type INTEGER NOT NULL, sender_id TEXT, sender_name TEXT, receiver_id TEXT, content TEXT, file_name TEXT, file_size INTEGER DEFAULT 0, file_hash TEXT, file_chunk_size INTEGER DEFAULT 0, file_chunk_count INTEGER DEFAULT 0, delivery_state TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP)"},
        {"offline_messages",
         {"id", "receiver_id", "payload", "created_at"},
         {"id"},
         "CREATE TABLE IF NOT EXISTS offline_messages (" + id + "receiver_id TEXT NOT NULL, payload TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP)"},
        {"friend_events",
         {"id", "event_type", "sender_id", "sender_name", "receiver_id", "query_account", "accepted", "event_state", "created_at"},
         {"id"},
         "CREATE TABLE IF NOT EXISTS friend_events (" + id + "event_type TEXT NOT NULL, sender_id TEXT, sender_name TEXT, receiver_id TEXT, query_account TEXT, accepted INTEGER DEFAULT 0, event_state TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP)"},
        {"server_groups",
         {"group_id", "group_name", "owner_id", "announcement", "created_at", "updated_at"},
         {"group_id"},
         "CREATE TABLE IF NOT EXISTS server_groups (group_id TEXT PRIMARY KEY, group_name TEXT NOT NULL, owner_id TEXT, announcement TEXT, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP)"},
        {"server_group_members",
         {"group_id", "user_id", "user_name", "role", "joined_at", "updated_at"},
         {"group_id", "user_id"},
         "CREATE TABLE IF NOT EXISTS server_group_members (group_id TEXT NOT NULL, user_id TEXT NOT NULL, user_name TEXT, role TEXT NOT NULL DEFAULT 'member', joined_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY(group_id, user_id))"},
        {"server_group_removed_members",
         {"group_id", "user_id", "removed_by", "removed_by_name", "removed_at"},
         {"group_id", "user_id"},
         "CREATE TABLE IF NOT EXISTS server_group_removed_members (group_id TEXT NOT NULL, user_id TEXT NOT NULL, removed_by TEXT, removed_by_name TEXT, removed_at TEXT DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY(group_id, user_id))"},
        {"server_group_announcements",
         {"id", "group_id", "author_id", "author_name", "content", "created_at"},
         {"id"},
         "CREATE TABLE IF NOT EXISTS server_group_announcements (" + id + "group_id TEXT NOT NULL, author_id TEXT, author_name TEXT, content TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP)"},
        {"server_group_audit_events",
         {"id", "group_id", "action", "actor_id", "actor_name", "target_user_id", "target_user_name", "details", "created_at"},
         {"id"},
         "CREATE TABLE IF NOT EXISTS server_group_audit_events (" + id + "group_id TEXT NOT NULL, action TEXT NOT NULL, actor_id TEXT NOT NULL, actor_name TEXT, target_user_id TEXT, target_user_name TEXT, details TEXT, created_at TEXT DEFAULT CURRENT_TIMESTAMP)"}
    };
}

QStringList placeholders(int count) {
    QStringList values;
    for (int i = 0; i < count; ++i) values << "?";
    return values;
}

QString upsertSql(const TableSpec& spec, const QStringList& columns) {
    QStringList updates;
    for (const QString& column : columns) {
        if (!spec.conflictColumns.contains(column)) {
            updates << QString("%1 = EXCLUDED.%1").arg(column);
        }
    }
    const QString action = updates.isEmpty() ? "DO NOTHING" : "DO UPDATE SET " + updates.join(", ");
    return QString("INSERT INTO %1(%2) VALUES(%3) ON CONFLICT(%4) %5")
        .arg(spec.name, columns.join(", "), placeholders(columns.size()).join(", "), spec.conflictColumns.join(", "), action);
}

bool parseArgs(const QStringList& args, Options* options, QString* error) {
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);
        auto takeValue = [&](QString* out) {
            if (i + 1 >= args.size()) {
                if (error) *error = "Missing value for " + arg;
                return false;
            }
            *out = args.at(++i);
            return true;
        };
        if (arg == "--mode") {
            if (!takeValue(&options->mode)) return false;
        } else if (arg == "--sqlite") {
            if (!takeValue(&options->sqlitePath)) return false;
        } else if (arg == "--json") {
            if (!takeValue(&options->jsonPath)) return false;
        } else if (arg == "--pg-host") {
            if (!takeValue(&options->pgHost)) return false;
        } else if (arg == "--pg-port") {
            QString port;
            if (!takeValue(&port)) return false;
            options->pgPort = port.toInt();
        } else if (arg == "--pg-database") {
            if (!takeValue(&options->pgDatabase)) return false;
        } else if (arg == "--pg-user") {
            if (!takeValue(&options->pgUser)) return false;
        } else if (arg == "--pg-password") {
            if (!takeValue(&options->pgPassword)) return false;
        } else if (arg == "--create-sample") {
            options->createSample = true;
        } else if (arg == "--sample-owner-id") {
            if (!takeValue(&options->sampleOwnerId)) return false;
        } else if (arg == "--sample-peer-id") {
            if (!takeValue(&options->samplePeerId)) return false;
        } else {
            if (error) *error = "Unknown argument: " + arg;
            return false;
        }
    }
    options->mode = options->mode.trimmed().toLower();
    if (options->sqlitePath.isEmpty()) {
        if (error) *error = "--sqlite is required";
        return false;
    }
    const QStringList modes{
        QStringLiteral("plan"),
        QStringLiteral("execute"),
        QStringLiteral("validate"),
        QStringLiteral("diff"),
        QStringLiteral("rollback")
    };
    if (!modes.contains(options->mode)) {
        if (error) *error = "--mode must be plan, execute, validate, diff, or rollback";
        return false;
    }
    return true;
}

QString sqlStringLiteral(QString value) {
    value.replace("'", "''");
    return "'" + value + "'";
}

bool createSampleSqlite(const QString& path, const QString& ownerId, const QString& peerId, QString* error) {
    QFile::remove(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString name = "sample_sqlite_migration";
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        ok = db.open();
        if (!ok && error) *error = db.lastError().text();
        QSqlQuery q(db);
        for (const TableSpec& spec : tableSpecs()) {
            QString sqliteSql = spec.createSql;
            sqliteSql.replace("BIGSERIAL PRIMARY KEY", "INTEGER PRIMARY KEY AUTOINCREMENT");
            ok = ok && q.exec(sqliteSql);
        }
        ok = ok && q.exec(QString("INSERT INTO accounts(account,password_hash,user_name,created_at,updated_at,login_count) VALUES(%1,'kdf$pbkdf2-sha256$120000$salt$hash','MigratedUser',CURRENT_TIMESTAMP,CURRENT_TIMESTAMP,3)")
            .arg(sqlStringLiteral(ownerId)));
        ok = ok && q.exec(QString("INSERT INTO messages(message_type,sender_id,sender_name,receiver_id,content,delivery_state,created_at) VALUES(3,%1,'MigratedUser',%2,'hello pg','offline',CURRENT_TIMESTAMP)")
            .arg(sqlStringLiteral(ownerId), sqlStringLiteral(peerId)));
        ok = ok && q.exec(QString("INSERT INTO server_groups(group_id,group_name,owner_id,announcement,created_at,updated_at) VALUES('public','公共聊天室',%1,'欢迎来到公共聊天室。',CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)")
            .arg(sqlStringLiteral(ownerId)));
        ok = ok && q.exec(QString("INSERT INTO server_group_members(group_id,user_id,user_name,role,joined_at,updated_at) VALUES('public',%1,'MigratedUser','owner',CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)")
            .arg(sqlStringLiteral(ownerId)));
        ok = ok && q.exec(QString("INSERT INTO server_group_audit_events(group_id,action,actor_id,actor_name,details,created_at) VALUES('public','migration_sample',%1,'MigratedUser','{}',CURRENT_TIMESTAMP)")
            .arg(sqlStringLiteral(ownerId)));
        if (!ok && error) *error = q.lastError().text();
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

QSet<QString> tableColumns(QSqlDatabase& db, const QString& table) {
    QSet<QString> columns;
    QSqlRecord record = db.record(table);
    for (int i = 0; i < record.count(); ++i) columns.insert(record.fieldName(i));
    return columns;
}

QString countSql(const QString& table) {
    return QStringLiteral("SELECT COUNT(*) FROM %1").arg(table);
}

bool queryCount(QSqlDatabase& db, const QString& sql, qint64* out, QString* error) {
    QSqlQuery query(db);
    if (!query.exec(sql) || !query.next()) {
        if (error) *error = query.lastError().text();
        return false;
    }
    if (out) *out = query.value(0).toLongLong();
    return true;
}

QString whereSql(const QStringList& columns) {
    QStringList parts;
    for (const QString& column : columns) {
        parts << QStringLiteral("%1 = ?").arg(column);
    }
    return parts.join(QStringLiteral(" AND "));
}

bool tableExists(QSqlDatabase& db, const QString& table) {
    return db.tables().contains(table, Qt::CaseInsensitive);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Options options;
    QString error;
    if (!parseArgs(app.arguments(), &options, &error)) {
        qWarning().noquote() << error;
        return 2;
    }

    if (options.createSample && !createSampleSqlite(options.sqlitePath, options.sampleOwnerId, options.samplePeerId, &error)) {
        qWarning().noquote() << "Failed to create sample SQLite:" << error;
        return 2;
    }

    QJsonObject result;
    result["format"] = "qtnetworkchat-sqlite-pg-migration-v1";
    result["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    result["mode"] = options.mode;
    result["sqlitePath"] = QDir::toNativeSeparators(QFileInfo(options.sqlitePath).absoluteFilePath());
    result["postgresHost"] = options.pgHost;
    result["postgresPort"] = options.pgPort;
    result["postgresDatabase"] = options.pgDatabase;
    result["postgresUser"] = options.pgUser;
    result["postgresPassword"] = "<redacted>";

    bool ok = true;
    QJsonArray tables;
    const QString sqliteName = "sqlite_migration_source";
    QSqlDatabase sqlite = QSqlDatabase::addDatabase("QSQLITE", sqliteName);
    sqlite.setDatabaseName(options.sqlitePath);
    if (!sqlite.open()) {
        result["ok"] = false;
        result["error"] = sqlite.lastError().text();
    } else {
        QSqlDatabase pg;
        const bool needsPostgres = options.mode != "plan";
        if (needsPostgres) {
            pg = QSqlDatabase::addDatabase("QPSQL", "sqlite_migration_pg_target");
            pg.setHostName(options.pgHost);
            pg.setPort(options.pgPort);
            pg.setDatabaseName(options.pgDatabase);
            pg.setUserName(options.pgUser);
            pg.setPassword(options.pgPassword);
            ok = pg.open();
            if (!ok) result["error"] = pg.lastError().text();
        }

        for (const TableSpec& spec : tableSpecs()) {
            QJsonObject item;
            item["name"] = spec.name;
            const QSet<QString> available = tableColumns(sqlite, spec.name);
            item["exists"] = !available.isEmpty();
            QStringList columns;
            for (const QString& column : spec.columns) {
                if (available.contains(column)) columns << column;
            }
            item["columns"] = QJsonArray::fromStringList(columns);
            qint64 count = 0;
            if (!available.isEmpty()) {
                QSqlQuery countQuery(sqlite);
                if (countQuery.exec("SELECT COUNT(*) FROM " + spec.name) && countQuery.next()) {
                    count = countQuery.value(0).toLongLong();
                }
            }
            item["rows"] = count;

            qint64 copied = 0;
            qint64 pgRows = 0;
            qint64 validated = 0;
            qint64 missing = 0;
            qint64 rolledBack = 0;
            if (ok && options.mode == "execute" && !columns.isEmpty()) {
                QSqlQuery ddl(pg);
                ok = ddl.exec(spec.createSql);
                QSqlQuery source(sqlite);
                QSqlQuery target(pg);
                ok = ok && source.exec("SELECT " + columns.join(", ") + " FROM " + spec.name);
                ok = ok && target.prepare(upsertSql(spec, columns));
                while (ok && source.next()) {
                    for (int i = 0; i < columns.size(); ++i) target.addBindValue(source.value(i));
                    ok = target.exec();
                    if (!ok) item["error"] = target.lastError().text();
                    else ++copied;
                }
            } else if (ok
                       && (options.mode == "validate" || options.mode == "diff" || options.mode == "rollback")
                       && !columns.isEmpty()) {
                const bool targetExists = tableExists(pg, spec.name);
                item["postgresExists"] = targetExists;
                if (targetExists) {
                    QString countError;
                    if (!queryCount(pg, countSql(spec.name), &pgRows, &countError)) {
                        item["error"] = countError;
                        ok = false;
                    }
                }

                QStringList keyColumns;
                for (const QString& column : spec.conflictColumns) {
                    if (columns.contains(column)) keyColumns << column;
                }
                item["keyColumns"] = QJsonArray::fromStringList(keyColumns);
                if (ok && targetExists && !keyColumns.isEmpty()) {
                    QSqlQuery source(sqlite);
                    ok = source.exec("SELECT " + keyColumns.join(", ") + " FROM " + spec.name);
                    if (!ok) item["error"] = source.lastError().text();

                    const QString existsSql = QStringLiteral("SELECT COUNT(*) FROM %1 WHERE %2")
                        .arg(spec.name, whereSql(keyColumns));
                    const QString deleteSql = QStringLiteral("DELETE FROM %1 WHERE %2")
                        .arg(spec.name, whereSql(keyColumns));

                    while (ok && source.next()) {
                        QList<QVariant> keys;
                        for (int i = 0; i < keyColumns.size(); ++i) keys << source.value(i);
                        if (options.mode == "rollback") {
                            QSqlQuery deleteQuery(pg);
                            ok = deleteQuery.prepare(deleteSql);
                            for (const QVariant& value : keys) deleteQuery.addBindValue(value);
                            if (!ok || !deleteQuery.exec()) {
                                item["error"] = deleteQuery.lastError().text();
                                ok = false;
                            } else {
                                rolledBack += deleteQuery.numRowsAffected();
                            }
                        } else {
                            QSqlQuery existsQuery(pg);
                            ok = existsQuery.prepare(existsSql);
                            for (const QVariant& value : keys) existsQuery.addBindValue(value);
                            if (!ok || !existsQuery.exec() || !existsQuery.next()) {
                                item["error"] = existsQuery.lastError().text();
                                ok = false;
                            } else if (existsQuery.value(0).toLongLong() > 0) {
                                ++validated;
                            } else {
                                ++missing;
                            }
                        }
                    }
                } else if (ok && count > 0) {
                    missing = count;
                }

                if (ok && options.mode == "validate" && missing > 0) {
                    ok = false;
                    item["error"] = QStringLiteral("missing rows in PostgreSQL");
                }
            }
            item["copiedRows"] = copied;
            item["postgresRows"] = pgRows;
            item["validatedRows"] = validated;
            item["missingRows"] = missing;
            item["rolledBackRows"] = rolledBack;
            item["diffStatus"] = missing == 0 ? QStringLiteral("clean") : QStringLiteral("drift");
            tables.append(item);
        }
        if (pg.isValid()) pg.close();
        sqlite.close();
        result["ok"] = ok;
    }
    QSqlDatabase::removeDatabase(sqliteName);
    QSqlDatabase::removeDatabase("sqlite_migration_pg_target");

    result["tables"] = tables;
    const QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Indented);
    if (!options.jsonPath.isEmpty()) {
        QDir().mkpath(QFileInfo(options.jsonPath).absolutePath());
        QFile file(options.jsonPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(json);
    }
    fputs(json.constData(), stdout);
    return result["ok"].toBool(false) ? 0 : 1;
}
