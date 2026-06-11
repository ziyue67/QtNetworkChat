#ifndef NOTIFICATIONPANELMANAGER_H
#define NOTIFICATIONPANELMANAGER_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QSize>

struct NoticeButtonSpec {
    QString text;
    QString objectName;
    QString toolTip;
};

struct FriendNoticeDialogChrome {
    QSize dialogSize;
    QString windowTitle;
    QString titleText;
    NoticeButtonSpec clearButton;
    QString searchPlaceholder;
    QString searchToolTip;
    QString previewPlaceholder;
    NoticeButtonSpec acceptButton;
    NoticeButtonSpec acceptAllButton;
    NoticeButtonSpec rejectButton;
    NoticeButtonSpec rejectAllButton;
    NoticeButtonSpec copyCardButton;
    NoticeButtonSpec copyInviteButton;
    NoticeButtonSpec copyAllButton;
    NoticeButtonSpec copyMediaPackButton;
    NoticeButtonSpec copyBatchPlanButton;
    NoticeButtonSpec copyMediaGuideButton;
    NoticeButtonSpec closeButton;
};

struct GroupNoticeDialogChrome {
    QSize dialogSize;
    QString windowTitle;
    QString titleText;
    QString countTextTemplate;
    QString searchPlaceholder;
    QString searchToolTip;
    QString previewPlaceholder;
    QString hintPlaceholder;
    NoticeButtonSpec openButton;
    NoticeButtonSpec copyIdButton;
    NoticeButtonSpec copyCardButton;
    NoticeButtonSpec copyAnnouncementButton;
    NoticeButtonSpec copyInviteButton;
    NoticeButtonSpec copyMembersButton;
    NoticeButtonSpec copyOnlineMembersButton;
    NoticeButtonSpec copyMediaPackButton;
    NoticeButtonSpec copyBatchPlanButton;
    NoticeButtonSpec copyMediaGuideButton;
    NoticeButtonSpec closeButton;
};

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

struct GroupNoticeMemberInput {
    QString userId;
    QString displayName;
    bool online = false;
    bool self = false;
};

struct GroupNoticeMemberCopyState {
    QStringList rows;
    QString emptyStatusMessage;
    QString copiedStatusMessage;
};

struct GroupNoticeBatchTargetInput {
    QString entryId;
    QString groupName;
    QString groupNumber;
    int memberCount = 0;
    int onlineCount = 0;
};

struct GroupNoticeBatchPlanState {
    QString text;
    QString statusMessage;
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

struct GroupNoticeCopyContext {
    QString groupName;
    QString groupNumber;
    int memberCount = 0;
};

struct GroupNoticeSelectionSnapshot {
    QString currentId;
    QString previewText;
    QString copyId;
    QString announcementText;
    QString cardText;
    GroupNoticeCopyContext copyContext;
    bool inspectable = false;
    bool createEntry = false;
    bool publicGroup = false;
};

class NotificationPanelManager {
public:
    static FriendNoticeDialogChrome friendNoticeDialogChrome();
    static GroupNoticeDialogChrome groupNoticeDialogChrome();
    static QString friendNoticeDialogStyleSheet();
    static QString groupNoticeDialogStyleSheet();
    static FriendNoticeListRenderUiState friendNoticeListRenderUiState(
        const QStringList& pendingFriendRequests,
        const QMap<QString, QString>& friendNames,
        int friendCount,
        const QString& filter);
    static GroupNoticeListRenderUiState groupNoticeListRenderUiState(
        int publicOnlineCount,
        const QList<GroupNoticeListGroupInput>& localGroups,
        const QString& filter);
    static QStringList publicGroupMemberIds(const QString& currentUserId,
                                            const QStringList& onlineUserIds);
    static GroupNoticeMemberCopyState groupMemberCopyState(
        const QList<GroupNoticeMemberInput>& members,
        bool onlineOnly);
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
    static GroupNoticeSelectionSnapshot groupNoticeSelectionSnapshot(
        const QString& currentId,
        const QString& fallbackName,
        int publicOnlineCount,
        const QString& localGroupName,
        int localGroupMemberCount,
        const QString& localAnnouncement,
        const QString& currentUserId);
    static GroupNoticeCopyContext groupNoticeCopyContext(const QString& currentId,
                                                         const QString& fallbackName,
                                                         int publicOnlineCount,
                                                         const QString& localGroupName,
                                                         int localGroupMemberCount);
    static QString friendNoticeTargetId(const QString& entryId,
                                        const QString& searchText = QString());
    static bool isRealFriendNoticeRequestId(const QString& entryId);
    static QStringList visibleFriendNoticeTargetIds(const QStringList& entryIds);
    static bool isInspectableGroupNoticeId(const QString& entryId);
    static QStringList uniqueGroupNoticeEntryIds(const QStringList& entryIds);
    static GroupNoticeBatchPlanState groupNoticeBatchPlanState(
        const QString& keyword,
        const QList<GroupNoticeBatchTargetInput>& targets,
        const QString& currentUserName,
        const QString& currentUserId);
    static QString groupNoticeInviteText(const QString& groupName,
                                         const QString& groupNumber,
                                         const QString& currentUserName,
                                         const QString& currentUserId);
    static QString groupNoticeCardText(bool publicGroup,
                                       const QString& groupName,
                                       const QString& groupNumber,
                                       int memberCount,
                                       const QString& announcement,
                                       const QString& currentUserId,
                                       int publicOnlineCount);
    static QString groupNoticeAnnouncementText(bool publicGroup,
                                               const QString& announcement);
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
    static QString groupNoticeBatchTargetText(const QString& currentId,
                                              const QString& groupName,
                                              const QString& groupNumber,
                                              int memberCount,
                                              int onlineCount);
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
