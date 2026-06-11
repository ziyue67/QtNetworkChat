#ifndef NOTIFICATIONPANELMANAGER_H
#define NOTIFICATIONPANELMANAGER_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>

struct FriendNoticeActionState {
    bool acceptEnabled = false;
    QString acceptText;
    QString acceptToolTip;
    bool rejectEnabled = false;
    QString rejectToolTip;
    bool copyCardEnabled = false;
    QString copyCardToolTip;
    bool copyInviteEnabled = false;
    QString copyInviteToolTip;
    bool copyAllEnabled = false;
    QString copyAllToolTip;
    bool copyMediaPackEnabled = false;
    QString copyMediaPackToolTip;
    bool copyBatchPlanEnabled = false;
    QString copyBatchPlanToolTip;
    bool copyMediaGuideEnabled = false;
    QString copyMediaGuideToolTip;
    bool acceptAllEnabled = false;
    QString acceptAllToolTip;
    bool rejectAllEnabled = false;
    QString rejectAllToolTip;
    bool clearEnabled = false;
    QString clearToolTip;
};

struct FriendNoticeListEntryUiState {
    QString entryId;
    QString text;
    QString toolTip;
    bool enabled = true;
    bool muted = false;
    bool accent = false;
    int rowHeight = 68;
};

struct FriendNoticeListRenderUiState {
    QString statsText;
    QList<FriendNoticeListEntryUiState> entries;
};

struct GroupNoticeListGroupInput {
    QString groupId;
    QString groupName;
    QString groupNumber;
    QString announcement;
    int memberCount = 0;
};

struct GroupNoticeListEntryUiState {
    QString entryId;
    QString text;
    QString toolTip;
    bool accent = false;
    int rowHeight = 96;
};

struct GroupNoticeListRenderUiState {
    QString countText;
    QList<GroupNoticeListEntryUiState> entries;
};

struct GroupNoticeActionState {
    bool openEnabled = false;
    QString openText;
    QString openToolTip;
    bool copyIdEnabled = false;
    QString copyIdToolTip;
    bool copyCardEnabled = false;
    QString copyCardToolTip;
    bool copyAnnouncementEnabled = false;
    QString copyAnnouncementToolTip;
    bool copyMembersEnabled = false;
    QString copyMembersToolTip;
    bool copyOnlineMembersEnabled = false;
    QString copyOnlineMembersToolTip;
    bool copyInviteEnabled = false;
    QString copyInviteToolTip;
    bool copyMediaPackEnabled = false;
    QString copyMediaPackToolTip;
    bool copyBatchPlanEnabled = false;
    QString copyBatchPlanToolTip;
    bool copyMediaGuideEnabled = false;
    QString copyMediaGuideToolTip;
    QString hintText;
};

class NotificationPanelManager {
public:
    static FriendNoticeListRenderUiState friendNoticeListRenderUiState(
        const QStringList& pendingFriendRequests,
        const QMap<QString, QString>& friendNames,
        int friendCount,
        const QString& filter);
    static GroupNoticeListRenderUiState groupNoticeListRenderUiState(
        int publicOnlineCount,
        const QList<GroupNoticeListGroupInput>& localGroups,
        const QString& filter);
    static FriendNoticeActionState friendNoticeActionState(const QString& currentId,
                                                           bool hasPending,
                                                           bool hasSearchKeyword);
    static QString friendNoticePreviewText(const QString& currentId,
                                           const QString& displayName);
    static GroupNoticeActionState groupNoticeActionState(const QString& currentId,
                                                         bool hasSelection,
                                                         bool hasSearchKeyword,
                                                         int visibleCount);
    static QString groupNoticePreviewText(const QString& currentId,
                                          const QString& groupName,
                                          const QString& groupNumber,
                                          int memberCount,
                                          int publicOnlineCount);
    static QString groupNoticeInviteText(const QString& groupName,
                                         const QString& groupNumber,
                                         const QString& currentUserName,
                                         const QString& currentUserId);
    static QString groupNoticeMediaPackText(const QString& groupName,
                                            const QString& groupNumber,
                                            int memberCount,
                                            const QString& currentUserName,
                                            const QString& currentUserId);
    static QString groupNoticeBatchPlanText(const QString& keyword,
                                            const QStringList& groups,
                                            int totalMembers,
                                            int onlineMembers,
                                            const QString& currentUserName,
                                            const QString& currentUserId);
    static QString groupNoticeMediaGuideText(const QString& groupName,
                                             const QString& groupNumber,
                                             int memberCount,
                                             const QString& currentUserName,
                                             const QString& currentUserId);
    static bool isSearchAddEntryId(const QString& entryId);
    static QString searchAddEntryTarget(const QString& entryId);
    static bool isGroupCreateEntryId(const QString& entryId);
    static QString groupCreateEntryName(const QString& entryId);
};

#endif // NOTIFICATIONPANELMANAGER_H
