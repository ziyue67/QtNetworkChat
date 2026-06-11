#ifndef FRIENDMANAGER_H
#define FRIENDMANAGER_H

#include "chatuser.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct FriendNoticeUiState {
    bool hasPending = false;
    int pendingCount = 0;
    QString text;
    QString toolTip;
};

struct FriendManagerListUiState {
    int visibleCount = 0;
    int onlineCount = 0;
    int offlineCount = 0;
    QString subTitle;
    QString statsText;
    QString emptyText;
    QString emptyEntryId;
};

struct FriendManagerListEntryUiState {
    QString entryId;
    QString text;
    bool placeholder = false;
    bool muted = false;
    int rowHeight = 58;
};

struct FriendManagerListRenderUiState {
    FriendManagerListUiState summary;
    QList<FriendManagerListEntryUiState> entries;
};

class FriendManager {
public:
    static QString contactDisplayName(const QString& userId,
                                      const QMap<QString, ChatUser>& knownUsers,
                                      const QMap<QString, QString>& friendNames);
    static bool isContactOnline(const QString& userId,
                                const QMap<QString, ChatUser>& knownUsers);
    static FriendNoticeUiState noticeUiState(int pendingIncomingCount);
    static bool matchesFilter(const QString& id, const QString& name, const QString& filter);
    static QString relationLabel(const QString& userId,
                                 const QString& currentUserId,
                                 const QStringList& friendIds,
                                 const QStringList& pendingOutgoingFriendRequests,
                                 const QMap<QString, ChatUser>& knownUsers);
    static FriendManagerListUiState managerListUiState(
        const QString& currentUserId,
        const QStringList& friendIds,
        const QStringList& localGroupIds,
        const QMap<QString, QString>& friendNames,
        const QMap<QString, ChatUser>& knownUsers,
        const QString& filter);
    static FriendManagerListRenderUiState managerListRenderUiState(
        const QString& currentUserId,
        const QStringList& friendIds,
        const QStringList& localGroupIds,
        const QMap<QString, QString>& friendNames,
        const QMap<QString, ChatUser>& knownUsers,
        const QString& filter);
    static QString managerSelectionPreviewText(const QString& entryId,
                                               const QString& displayName,
                                               bool online,
                                               bool canInviteCurrentGroup);
};

#endif // FRIENDMANAGER_H
