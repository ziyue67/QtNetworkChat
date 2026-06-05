#ifndef CLIENTSTORAGE_H
#define CLIENTSTORAGE_H

#include <QMap>
#include <QString>
#include <QStringList>

class ClientStorage {
public:
    explicit ClientStorage(QString userName = QString());

    QString userName() const;
    void setUserName(const QString& userName);

    QString friendFilePath() const;
    QString groupFilePath() const;
    QString avatarFilePath() const;

    bool readLegacyFriends(QStringList* friendIds, QMap<QString, QString>* friendNames) const;
    bool writeLegacyFriends(const QStringList& friendIds,
                            const QMap<QString, QString>& friendNames) const;
    bool readLegacyLocalGroups(const QString& currentUserId,
                               QStringList* groupIds,
                               QMap<QString, QString>* groupNames,
                               QMap<QString, QStringList>* groupMembers,
                               QMap<QString, QString>* groupAnnouncements) const;
    bool writeLegacyLocalGroups(const QString& currentUserId,
                                const QStringList& groupIds,
                                const QMap<QString, QString>& groupNames,
                                const QMap<QString, QStringList>& groupMembers,
                                const QMap<QString, QString>& groupAnnouncements) const;

    bool saveProfileToSqlite(const QString& databasePath,
                             const QString& userId,
                             const QString& userName,
                             const QString& avatarPath) const;
    bool saveFriendsToSqlite(const QString& databasePath,
                             const QStringList& friendIds,
                             const QMap<QString, QString>& friendNames,
                             const QStringList& pendingIncoming,
                             const QStringList& pendingOutgoing) const;
    bool saveLocalGroupsToSqlite(const QString& databasePath,
                                 const QString& currentUserId,
                                 const QStringList& groupIds,
                                 const QMap<QString, QString>& groupNames,
                                 const QMap<QString, QStringList>& groupMembers,
                                 const QMap<QString, QString>& groupAnnouncements) const;

private:
    QString appDataDirectory() const;
    QString safeUserName() const;

    QString m_userName;
};

#endif // CLIENTSTORAGE_H
