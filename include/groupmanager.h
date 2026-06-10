#ifndef GROUPMANAGER_H
#define GROUPMANAGER_H

#include <QMap>
#include <QString>
#include <QStringList>

struct GroupNoticeUiState {
    int joinedGroupCount = 1;
    QString text;
    QString toolTip;
};

struct GroupMemberDisplayState {
    QString role;
    QString state;
    QString actionText;
    bool isOwner = false;
    bool isAdmin = false;
};

struct ServerGroupMemberUpdateDecision {
    bool allowed = false;
    bool roleAction = false;
    QString targetId;
    QString normalizedAction;
    QString statusMessage;
    QString auditMessage;
    int statusTimeoutMs = 2200;
};

class GroupManager {
public:
    static GroupNoticeUiState noticeUiState(int localGroupCount);
    static QString localGroupOwnerId(const QStringList& members, const QString& currentUserId);
    static bool isLocalGroupOwner(const QString& groupId,
                                  const QStringList& members,
                                  const QString& currentUserId);
    static bool canManageServerGroup(const QString& groupId,
                                     const QString& currentUserId,
                                     const QMap<QString, QString>& serverGroupOwners,
                                     const QMap<QString, QString>& serverGroupMemberRoles);
    static GroupMemberDisplayState memberDisplayState(const QString& memberId,
                                                      const QString& currentUserId,
                                                      const QString& ownerId,
                                                      const QString& serverRole,
                                                      bool isFriend,
                                                      bool isPending,
                                                      bool online,
                                                      bool serverGroup);
    static ServerGroupMemberUpdateDecision serverGroupMemberUpdateDecision(
        const QString& memberId,
        const QString& action,
        bool clientConnected,
        const QString& groupId,
        const QString& currentUserId,
        const QStringList& members,
        const QMap<QString, QString>& serverGroupOwners,
        const QMap<QString, QString>& serverGroupMemberRoles);
};

#endif // GROUPMANAGER_H
