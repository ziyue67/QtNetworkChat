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

enum class FriendNoticeBulkActionKind {
    AcceptAll,
    RejectAll,
    ClearAll
};

struct FriendNoticeRequestDecisionState {
    QString statusMessage;
    QString systemMessage;
};

struct FriendNoticeBulkActionState {
    QString title;
    QString questionText;
    QString emptyStatusMessage;
    QString cancelledStatusMessage;
    QString successStatusMessage;
    QString systemMessage;
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

struct FriendManagerSelectionPreviewUiState {
    QString text;
};

struct FriendManagerContactCopyInput {
    QString userId;
    QString displayName;
    bool online = false;
};

struct FriendManagerContactCopyState {
    QStringList rows;
    QString emptyStatusMessage;
    QString copiedStatusMessage;
};

struct FriendManagerVisibleTargetSummary {
    QString userId;
    QString displayName;
    bool online = false;
};

struct GlobalSearchResultCopyInput {
    QString entryId;
    QString displayName;
    bool online = false;
    bool friendContact = false;
    bool localGroup = false;
    int memberCount = 0;
};

struct GlobalSearchResultCopyState {
    QStringList rows;
    QString emptyStatusMessage;
    QString copiedStatusMessage;
};

struct GlobalSearchSelectionCopyState {
    QString text;
    bool valid = false;
};

struct FriendQuickAddSuggestionEntryUiState {
    QString entryId;
    QString text;
    bool placeholder = false;
    bool muted = false;
    bool enabled = true;
    int rowHeight = 34;
};

struct FriendQuickAddSuggestionUiState {
    int onlineCandidates = 0;
    int pendingCandidates = 0;
    int visibleCount = 0;
    QString statsText;
    QString previewText;
    QList<FriendQuickAddSuggestionEntryUiState> entries;
};

class FriendManager {
public:
    static QString contactDisplayName(const QString& userId,
                                      const QMap<QString, ChatUser>& knownUsers,
                                      const QMap<QString, QString>& friendNames);
    static bool isContactOnline(const QString& userId,
                                const QMap<QString, ChatUser>& knownUsers);
    static FriendNoticeUiState noticeUiState(int pendingIncomingCount);
    static FriendNoticeRequestDecisionState friendNoticeRequestDecisionState(
        const QString& userId,
        const QString& displayName,
        bool accepted);
    static FriendNoticeBulkActionState friendNoticeBulkActionState(
        FriendNoticeBulkActionKind action,
        int pendingCount);
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
    static FriendManagerSelectionPreviewUiState managerSelectionPreviewUiState(const QString& entryId,
                                                                               const QString& displayName,
                                                                               bool online,
                                                                               bool canInviteCurrentGroup);
    static FriendManagerContactCopyState managerContactCopyState(
        const QList<FriendManagerContactCopyInput>& contacts,
        bool onlineOnly);
    static GlobalSearchSelectionCopyState friendManagerSearchSummaryCardState(
        const QString& currentUserId,
        const QString& currentUserName,
        const QString& keyword,
        int totalFriendCount,
        int localGroupCount,
        const QList<FriendManagerVisibleTargetSummary>& visibleTargets);
    static GlobalSearchSelectionCopyState friendManagerMediaPackState(
        const QString& currentUserId,
        const QString& currentUserName,
        const QString& keyword,
        int totalFriendCount,
        const FriendManagerVisibleTargetSummary& selectedTarget);
    static GlobalSearchSelectionCopyState friendManagerBatchMediaPlanState(
        const QString& currentUserId,
        const QString& currentUserName,
        const QString& keyword,
        const QList<FriendManagerVisibleTargetSummary>& visibleTargets);
    static QString friendManagerMediaGuideText(const QString& currentUserId,
                                               const QString& currentUserName,
                                               const QString& keyword,
                                               int visibleCount,
                                               int totalFriendCount);
    static GlobalSearchSelectionCopyState quickAddSearchSummaryCardState(
        const QString& currentUserId,
        const QString& currentUserName,
        const QString& keyword,
        const QList<FriendManagerVisibleTargetSummary>& candidates);
    static GlobalSearchSelectionCopyState quickAddMediaPackState(
        const QString& currentUserId,
        const QString& currentUserName,
        const FriendManagerVisibleTargetSummary& target);
    static GlobalSearchSelectionCopyState quickAddChecklistState(
        const QString& currentUserId,
        const QString& currentUserName,
        int currentFriendCount,
        const FriendManagerVisibleTargetSummary& target);
    static QString quickAddMediaGuideText(const QString& currentUserId,
                                          const QString& currentUserName,
                                          const QString& targetId);
    static GlobalSearchSelectionCopyState friendNoticeMediaPackState(
        const QString& currentUserId,
        const QString& currentUserName,
        int pendingCount,
        const FriendManagerVisibleTargetSummary& target);
    static QString friendNoticeApplicantCardText(const QString& userId,
                                                 const QString& displayName);
    static QString friendNoticeReplyText(const QString& targetName,
                                         const QString& currentUserName,
                                         const QString& currentUserId);
    static QString friendNoticeBulkCopyText(const QStringList& pendingFriendRequests,
                                            const QMap<QString, QString>& friendNames,
                                            const QString& currentUserName,
                                            const QString& currentUserId);
    static GlobalSearchSelectionCopyState friendNoticeBatchPlanState(
        const QString& currentUserId,
        const QString& currentUserName,
        int pendingCount,
        const QString& keyword,
        const QList<FriendManagerVisibleTargetSummary>& applicants);
    static QString friendNoticeMediaGuideText(const QString& currentUserId,
                                              const QString& currentUserName,
                                              const FriendManagerVisibleTargetSummary& target);
    static GlobalSearchResultCopyState globalSearchResultCopyState(
        const QList<GlobalSearchResultCopyInput>& results,
        bool onlineOnly);
    static QString globalSearchInviteText(const QString& currentUserId,
                                          const QString& currentUserName,
                                          const GlobalSearchResultCopyInput& selectedResult,
                                          const QString& fallbackKeyword);
    static GlobalSearchSelectionCopyState globalSearchInviteCardState(
        const QString& currentUserId,
        const QString& currentUserName,
        const GlobalSearchResultCopyInput& selectedResult,
        const QString& fallbackKeyword);
    static GlobalSearchSelectionCopyState globalSearchSummaryCardState(
        const QString& currentUserId,
        const QString& currentUserName,
        const QList<GlobalSearchResultCopyInput>& results,
        const QString& keyword);
    static GlobalSearchSelectionCopyState globalSearchMediaPackState(
        const QString& currentUserId,
        const QString& currentUserName,
        const GlobalSearchResultCopyInput& selectedResult,
        const QString& keyword);
    static GlobalSearchSelectionCopyState globalSearchBatchMediaPlanState(
        const QString& currentUserId,
        const QString& currentUserName,
        const QList<GlobalSearchResultCopyInput>& results,
        const QString& keyword);
    static QString globalSearchMediaGuideText(const QString& currentUserId,
                                              const QString& currentUserName,
                                              const QString& currentChatDisplayName);
    static QString managerSelectionPreviewText(const QString& entryId,
                                               const QString& displayName,
                                               bool online,
                                               bool canInviteCurrentGroup);
    static FriendQuickAddSuggestionUiState quickAddSuggestionUiState(
        const QString& currentUserId,
        const QString& currentUserName,
        const QStringList& friendIds,
        const QStringList& pendingOutgoingFriendRequests,
        const QMap<QString, ChatUser>& knownUsers,
        const QMap<QString, QString>& friendNames,
        const QString& filter,
        int maxVisible = 5);
};

#endif // FRIENDMANAGER_H
