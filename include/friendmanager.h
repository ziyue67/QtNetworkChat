#ifndef FRIENDMANAGER_H
#define FRIENDMANAGER_H

#include "chatuser.h"

#include <QMap>
#include <QString>
#include <QStringList>

struct FriendNoticeUiState {
    bool hasPending = false;
    int pendingCount = 0;
    QString text;
    QString toolTip;
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
};

#endif // FRIENDMANAGER_H
