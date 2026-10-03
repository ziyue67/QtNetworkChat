#pragma once

#include <QJsonObject>
#include <QSqlDatabase>
#include <QStringList>

class QTcpSocket;

namespace ServerGroupSupport {
struct ActorPermission {
    bool member = false;
    bool manager = false;
    bool owner = false;
    QString ownerId;
    QString role;
};

bool messageRateAllowed(const QString& groupId, const QString& userId, int perMinute);
QString normalizeGroupId(QString groupId);
QStringList initialMemberIds(const QJsonObject& obj, const QString& requesterId);
QSqlDatabase openDatabase(const QString& connectionName);
bool openConnection(QSqlDatabase& db, const QString& scope);
void releaseDatabase(const QString& connectionName);
ActorPermission actorPermission(QSqlDatabase& db, const QString& groupId, const QString& userId);
QString insertIgnore(const QString& table, const QStringList& columns,
                     const QStringList& values, const QStringList& conflictColumns);
QString insertReplace(const QString& table, const QStringList& columns,
                      const QStringList& values, const QStringList& conflictColumns,
                      const QStringList& updateAssignments);
void sendNotice(QTcpSocket* socket, const QString& content);
}
