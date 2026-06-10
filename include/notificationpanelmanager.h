#ifndef NOTIFICATIONPANELMANAGER_H
#define NOTIFICATIONPANELMANAGER_H

#include <QString>

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
    static bool isSearchAddEntryId(const QString& entryId);
    static QString searchAddEntryTarget(const QString& entryId);
    static bool isGroupCreateEntryId(const QString& entryId);
    static QString groupCreateEntryName(const QString& entryId);
};

#endif // NOTIFICATIONPANELMANAGER_H
