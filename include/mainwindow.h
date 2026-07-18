#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStandardItemModel>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QCloseEvent>
#include <QStringList>
#include <QMap>
#include <QEvent>
#include <QDate>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include "client.h"
#include "chatuser.h"
#include "clientstorage.h"
#include "friendmanager.h"
#include "groupmanager.h"
#include "historyservice.h"
#include "chatcontextmanager.h"
#include "notificationpanelmanager.h"
#include "localfilemanager.h"
#include "message.h"
#include "transfermanager.h"

namespace Ui {
class MainWindow;
}

class MessagesView;
class ContactsView;
class FavoritesView;
class SettingsView;
class ProfileView;
class AppNav;
class TitleBar;
class AddFriendDialog;
class GlobalSearchDialog;
class QStackedWidget;
class QAction;
class QLabel;
class QListWidget;
class QLineEdit;
class QIcon;
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(Client* client, const QString& userId, const QString& userName, QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onSendMessage();
    void onSendFile();
    void onSendImage();
    void onNewMessage(const Message& msg);
    void onUserJoined(const QString& userId, const QString& userName);
    void onUserLeft(const QString& userId, const QString& userName);
    void onUserListUpdated(const QVector<ChatUser>& users);
    void onPrivateChat(const QModelIndex& index);
    void onClientDisconnected();
    void onClientError(const QString& error);
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void onClearHistory();
    void onFilterHistoryByDate();
    void onExportHistory();
    void onAddFriend();
    void onUploadAvatar();
    void onBackToGroupChat();
    void onFriendRequestReceived(const QString& senderId, const QString& senderName);
    void onFriendSearchResult(const QString& account, const QString& userId, const QString& userName, bool found, bool online, bool exactMatch, int matchCount, const QString& matchReason);
    void onFriendRequestSent(const QString& receiverId, bool delivered);
    void onFriendResponseReceived(const QString& senderId, const QString& senderName, bool accepted);
    void onServerGroupSnapshotReceived(const QJsonArray& groups);
    void onE2ESessionStateChanged(const QString& peerId, const QJsonObject& status);
    void onE2EIdentityStateChanged(const QString& peerId, const QJsonObject& status);
    void onE2ESessionRotationRequested(const QString& peerId, const QJsonObject& agreement);
    void onE2ESessionRotationResponded(const QString& peerId, const QJsonObject& agreement, bool accepted, const QString& reason);
    void onUserContextMenu(const QPoint& pos);
    void onContactSearchChanged(const QString& text);
    void onCopyAccount();
    void onLogout();
    void onShowQuickAddFriend();
    void onShowFriendManager();
    void onShowGlobalSearch();
    void onShowStorageManager();
    void onShowCreateMenu();
    void onShowFriendNotifications();
    void onShowGroupNotifications();
    void onEditGroupAnnouncement();
    void onInsertEmoji();
    void onInsertMention();
    void onCaptureScreenshot(bool hideCurrentWindow = true);
    void onComposerFilesDropped(const QStringList& paths);
    void onMessageActionRequested(const QModelIndex& index, const QString& action);
    void onAvatarActionRequested(const QModelIndex& index, const QString& action);
    void onMediaActivated(const QModelIndex& index);
    void onMultiSelectForwardRequested();
    void onMultiSelectDeleteRequested();
    void onMultiSelectFavoriteRequested();
    void onResumeSavedOutgoingTransfer();
    void onClearSavedOutgoingTransfer();
    void onFileTransferStatusChanged(const QString& fileName, const QString& transferId, const QString& reason, qint64 receivedBytes, qint64 totalBytes);
    void onAppNavRouteActivated(const QString& route);
    void onThemeToggled();
    void onSettingsThemeModeChanged(int mode);
    void onSettingsNotificationsToggled(bool enabled);
    void onSettingsSoundToggled(bool enabled);
    void onSettingsDesktopNotificationsToggled(bool enabled);
    void onSettingsMuteInSessionToggled(bool enabled);
    void onSettingsE2EEnabledToggled(bool enabled);
    void onSettingsAutoAcceptFilesToggled(bool enabled);
    void onSettingsOpenFolderAfterDownloadToggled(bool enabled);
    void onSettingsHideWindowBeforeScreenshotToggled(bool enabled);
    void onSettingsDownloadPathChangeRequested();
    void onSettingsScreenshotShortcutChangeRequested();

signals:
    void logoutRequested();

private:
    struct ReceivedTransferContext {
        QString kind;
        QString receivedName;
        QString receivedSize;
        QString integrityText;
        QString integritySuffix;
        QString manifestSuffix;
        QString savePath;
    };

    struct SelectedTransferFile {
        QString filePath;
        QFileInfo info;
        QString fileSize;
    };

    void setupUi();
    void setupTray();
    void setupQQNT();
    void loadStyleSheet();
    void updateStyleSheet();
    void appendMessage(const Message& msg);
    void appendSystemMessage(const QString& text);
    void setTransferWorkspaceState(const TransferWorkspaceCardState& state);
    void applyTransferSendState(const TransferSendUiState& state);
    bool ensureTransferTargetReady(const QString& kind, const QString& targetName, bool isLocalGroup);
    bool selectTransferFile(const TransferSelectionPlan& selectionPlan,
                            QString* filePath,
                            QFileInfo* fileInfo,
                            QString* fileSize = nullptr);
    bool selectTransferFileContext(const TransferSelectionPlan& selectionPlan,
                                   SelectedTransferFile* selectedFile);
    void sendSelectedTransfer(const SelectedTransferFile& selectedFile, bool media);
    bool handleTransferSelectionUiState(TransferSelectionUiState* selectionState);
    void appendTransferCompletionState(const TransferSendUiState& state,
                                       bool includeSystemMessage,
                                       bool includeCard,
                                       const QColor& cardForeground,
                                       const QColor& cardBackground);
    void appendLocalGroupFileTransferCompletion(const TransferSelectionPlan& selectionPlan,
                                                const QFileInfo& info,
                                                const QString& fileSize,
                                                const QString& targetName,
                                                const QString& completedAt);
    void appendLocalGroupMediaTransferCompletion(const QString& filePath,
                                                 const QFileInfo& info,
                                                 const QString& fileSize,
                                                 const QString& mediaType,
                                                 bool isVideo,
                                                 const QString& targetName,
                                                 const QString& completedAt);
    void appendRemoteMediaTransferCompletion(const QString& filePath,
                                             const TransferSendUiState& completedState,
                                             bool isVideo,
                                             const TransferWorkspaceCardState& workspaceState);
    void handleRemoteTransferResult(bool ok,
                                    bool transferCanceled,
                                    const QString& filePath,
                                    const QFileInfo& info,
                                    const QString& fileSize,
                                    const QString& targetName,
                                    const QString& kind,
                                    bool media,
                                    bool isVideo,
                                    const QString& transferSummary);
    void appendMediaPreviewItem(const QString& text,
                                const QPixmap& pixmap,
                                bool isVideo,
                                bool alignRight,
                                const QString& openPath = QString(),
                                const QString& senderId = QString(),
                                const QString& senderName = QString());
    void applyReceivedTransferRenderPlan(const TransferReceiveRenderPlan& plan,
                                         const QString& fileName,
                                         const QString& transferId,
                                         qint64 receivedBytes,
                                         qint64 totalBytes);
    bool persistReceivedTransferPayload(const ReceivedTransferContext& context,
                                        const QString& displayName,
                                        const QString& transferId,
                                        const QByteArray& fileData,
                                        qint64 totalBytes);
    bool handleReceivedTransferMessage(const Message& msg,
                                       const QString& displayName);
    TransferReceiveRenderPlan receivedTransferPersistencePlan(const ReceivedTransferContext& context,
                                                              const QString& displayName,
                                                              bool saved) const;
    void appendTransferChatListItem(const TransferChatListItemUiState& itemState);
    ReceivedTransferContext receivedTransferContext(const Message& msg,
                                                    const QString& kind,
                                                    const QString& fallbackName,
                                                    const QString& downloadSubdir,
                                                    const QString& displayName) const;
    void loadHistory(const QString& peerId = QString());
    void saveHistory(const QString& peerId, const QString& content);
    void saveHistory(const QString& peerId,
                     const QString& content,
                     const QString& encryptionState,
                     const QString& e2eKeyId = QString(),
                     const QString& e2eKeyFingerprint = QString());
    bool ensureClientDatabase() const;
    QString clientDbPath() const;
    QString camelToKebabCase(const QString& camel) const;
    void runForwardForMessage(const QModelIndex& index, const QString& text);
    void forwardMessageToTarget(const QModelIndex& index,
                                  const QString& text,
                                  const QString& userId,
                                  const QString& groupId);
    void persistMultiSelectMessages(const QModelIndexList& selected);
    void readLocalChatActions(const QString& sessionId);

    bool saveProfileToSqlite() const;
    bool sendTransferWithProgress(const QString& filePath, const QString& receiverId, const QString& targetName, const QString& kind, bool asImage, QString* transferSummary = nullptr, bool* canceled = nullptr);
    QStandardItem* findUserItem(const QString& userId);
    void refreshFriendList();
    void refreshSessionList();
    void refreshFavoritesView();
    void onFavoriteSelected(const QString& sessionId, const QString& messageId);
    void scrollActiveChatToBottom();
    void refreshContactsAndProfile();
    void refreshGroupMemberPanel();
    // QQNT group member sidebar (right rail in MessagesView). connectGroupMemberSidebar
    // wires its action signals to the same backend used by the legacy member menu;
    // refreshGroupMemberSidebar feeds it the current group's members and toggles it
    // on for group sessions only.
    void connectGroupMemberSidebar();
    void refreshGroupMemberSidebar();
    void refreshComposerState();
    void refreshWorkspaceChrome();
    void refreshSessionSummary();
    void updateSavedOutgoingTransferRecoveryUi(bool announce = false);
    void showFileTransferStatusEvent(const QString& fileName, const QString& transferId, const QString& reason, qint64 receivedBytes, qint64 totalBytes);
    LocalSavedFileState savedFileActionState(const QModelIndex& index) const;
    ChatContextSavedFileState chatContextSavedFileState(const LocalSavedFileState& savedFileState) const;
    bool copySavedFilePathToClipboard(const ChatContextSavedFileCommand& command);
    bool openSavedFileFromState(const LocalSavedFileState& savedFileState, const ChatContextSavedFileCommand& command);
    bool openMediaPreviewFromState(const LocalSavedFileState& savedFileState);
    bool openSavedFolderFromState(const LocalSavedFileState& savedFileState, const ChatContextSavedFileCommand& command);
    void cachePeerAvatar(const ChatUser& user);
    QString peerAvatarPath(const QString& userId) const;
    QIcon peerAvatarIcon(const QString& userId, const QString& displayName) const;
    QString chatAvatarPath(const QString& userId) const;
    void decorateChatItem(QStandardItem* item,
                          const QString& senderId,
                          const QString& senderName,
                          bool outgoing,
                          bool system = false) const;
    void copyTextWithStatus(const QString& text, const QString& statusMessage, int timeoutMs = 1800);
    bool confirmAction(const QString& title,
                       const QString& message,
                       const QString& canceledStatusMessage = QString(),
                       int canceledStatusTimeoutMs = 1600,
                       QWidget* parent = nullptr);
    QString promptTextValue(const QString& title,
                            const QString& label,
                            const QString& initialValue,
                            bool* accepted,
                            QWidget* parent = nullptr) const;
    QString promptMultilineValue(const QString& title,
                                 const QString& label,
                                 const QString& initialValue,
                                 bool* accepted,
                                 QWidget* parent = nullptr) const;
    QString promptItemValue(const QString& title,
                            const QString& label,
                            const QStringList& items,
                            bool* accepted,
                            QWidget* parent = nullptr) const;
    QStringList currentSessionMemberIds() const;
    bool applyAvatarSelection(const LocalFileSelectionResult& selection);
    bool persistAvatarPixmap(const QPixmap& pixmap, const QFileInfo& info);
    void openPrivateSession(const QString& userId);
    bool ensureFriendRequestQueued(const QString& userId,
                                   const QString& successTemplate = QString(),
                                   bool refreshGroups = false);
    QString createLocalGroupSession(const QString& groupName,
                                    const QStringList& members = QStringList(),
                                    const QString& announcement = QString());
    int appendMembersToLocalGroup(const QString& groupId, const QStringList& memberIds);
    bool handleCreateMenuCommand(const QString& commandId);
    bool handleLocalGroupContextCommand(const QString& groupId,
                                        const QString& groupLabel,
                                        const QString& commandId);
    bool handleContactContextCommand(const QString& userId,
                                     const QString& commandId);
    FriendManagerVisibleTargetSummary friendNoticeVisibleTarget(const QString& userId) const;
    QList<FriendManagerVisibleTargetSummary> visibleFriendNoticeTargets(QListWidget* noticeList) const;
    FriendNoticeSelectionSnapshot currentFriendNoticeSelectionSnapshot(QListWidget* noticeList,
                                                                       QLineEdit* searchEdit) const;
    QList<GroupNoticeMemberInput> groupNoticeMemberCopyInputs(const QStringList& memberIds) const;
    QStringList publicGroupNoticeMemberIds() const;
    QStringList groupNoticeMemberIds(const QString& groupId) const;
    QList<GroupNoticeListGroupInput> groupNoticeLocalGroups() const;
    void fillGroupNoticeList(QListWidget* noticeList,
                             QLabel* countLabel,
                             QLineEdit* searchEdit) const;
    GroupNoticeSelectionSnapshot currentGroupNoticeSelectionSnapshot(QListWidget* noticeList,
                                                                     QLineEdit* searchEdit) const;
    QList<GroupNoticeBatchTargetInput> visibleGroupNoticeBatchTargets(QListWidget* noticeList) const;
    bool openSelectedGroupNoticeEntry(QListWidget* noticeList, QDialog* dialog = nullptr);
    bool handleSavedFileContextCommand(const QString& commandId, const LocalSavedFileState& savedFileState);
    void setChatDraftText(const QString& text, const QString& statusMessage, int timeoutMs = 1400);
    void insertChatDraftText(const QString& text, const QString& statusMessage, int timeoutMs = 1400);
    void updateEmptyStateVisibility();
    void setSendButtonSending(bool sending);
    ChatContextComposerState currentChatContextComposerState() const;
    bool applyChatContextComposerCommand(const QString& commandId);
    QAction* addChatContextAction(QMenu& menu,
                                  const QString& title,
                                  const QString& tip,
                                  const QString& commandId,
                                  bool enabled = true);
    bool handleChatContextCommand(const QString& commandId,
                                  const QString& chatText,
                                  const LocalSavedFileState& savedFileState,
                                  const QModelIndex& index = QModelIndex());
    bool handleBackendContextCommand(const QString& commandId,
                                      const QString& chatText,
                                      const QModelIndex& index);
    bool isCurrentUserRemovedFromPublicGroup() const;
    void switchToLocalGroup(const QString& groupId, const QString& groupName);
    void showMessagesView();
    void showAddFriendDialog();
    void showGlobalSearchDialog(bool contactGroupMode = false);
    void showEssencePanel();
    void showGroupInfoPanel();
    void searchAndAddAccount(const QString& account, QWidget* warningParent = nullptr);
    void loadAvatar();
    QString contactDisplayName(const QString& userId) const;
    bool isContactOnline(const QString& userId) const;
    int knownOnlineUserCount() const;
    QString groupOwnerId(const QString& groupId) const;
    bool isCurrentUserGroupOwner(const QString& groupId) const;
    bool canCurrentUserManageServerGroup(const QString& groupId) const;
    bool requestServerGroupMemberUpdate(const QString& memberId, const QString& action,
                                        const QString& groupId = QString());
    QString e2eSessionStatusText(const QString& peerId) const;
    void copyE2ESessionStatus(const QString& peerId);
    QString getFriendFilePath() const;
    QString getGroupFilePath() const;
    QString getAvatarFilePath() const;
    void saveFriends() const;
    void saveFriendGroups() const;
    void saveLocalGroups() const;
    void updateUnreadState();
    void clearUnreadState();
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

    Client* m_client;
    ClientStorage m_clientStorage;
    QStandardItemModel* m_userListModel;
    QStandardItemModel* m_sessionModel;
    QStandardItemModel* m_chatModel;
    QStandardItemModel* m_groupMemberModel;
    FriendManager m_friendManager;
    GroupManager m_groupManager;
    HistoryService m_historyService;
    TransferManager m_transferManager;
    QString m_currentUserId;
    QString m_currentUserName;
    QStringList m_friendIds;
    QStringList m_localGroupIds;
    QStringList m_pendingFriendRequests;
    QStringList m_pendingOutgoingFriendRequests;
    QMap<QString, QString> m_friendNames;
    // Local-only friend groups: friendId -> group name, plus custom (possibly
    // empty) group names. No server protocol; persisted via ClientStorage.
    QMap<QString, QString> m_friendGroups;
    QStringList m_customGroups;
    QMap<QString, QString> m_localGroupNames;
    QMap<QString, QString> m_localGroupAnnouncements;
    QMap<QString, QString> m_localGroupAvatarPaths;
    QMap<QString, QStringList> m_localGroupMembers;
    QMap<QString, QString> m_serverGroupNames;
    // Server group id -> locally persisted group session after the user joins
    // it from QQNT global search. Keeps the result-card action stable.
    QMap<QString, QString> m_joinedServerSearchGroups;
    QMap<QString, QString> m_serverGroupAnnouncements;
    QMap<QString, QString> m_serverGroupOwners;
    QMap<QString, QStringList> m_serverGroupMembers;
    QMap<QString, QString> m_serverGroupMemberNames;
    QMap<QString, QString> m_serverGroupMemberRoles;
    QMap<QString, QJsonObject> m_serverGroupSettings;
    QMap<QString, QJsonObject> m_serverGroupUserSettings;
    QMap<QString, QJsonArray> m_serverGroupAuditEvents;
    QMap<QString, QJsonObject> m_removedServerGroups;
    // Pending server-side join applications received by a group owner/admin.
    // Keyed by requestId and rendered in the Group Notifications dialog.
    QMap<QString, QJsonObject> m_pendingGroupJoinApplications;
    // Outgoing applications are visible to their applicant in Group Notifications
    // until a group owner or administrator decides them.
    QMap<QString, QJsonObject> m_pendingOutgoingGroupJoinApplications;
    QMap<QString, QJsonObject> m_rejectedOutgoingGroupJoinApplications;
    bool m_hasServerGroupSnapshot;
    bool m_wasInPublicServerGroup;
    QMap<QString, ChatUser> m_knownUsers;
    QString m_contactFilter;
    QString m_privateChatTarget;
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayMenu;
    QAction* m_resumeSavedTransferAction;
    QAction* m_clearSavedTransferAction;
    QAction* m_copyLastTransferStatusAction;
    QString m_lastTransferStatusDiagnostic;
    int m_unreadCount;
    bool m_isQuitting;

    Ui::MainWindow* ui = nullptr;

    // QQNT UI components
    QWidget* m_qqntRoot = nullptr;
    TitleBar* m_titleBar = nullptr;
    AppNav* m_appNav = nullptr;
    QStackedWidget* m_viewStack = nullptr;
    MessagesView* m_messagesView = nullptr;
    ContactsView* m_contactsView = nullptr;
    // When a QQNT AddFriendDialog is open, friend-search results are routed to it
    // (two-step search → confirm) instead of the legacy auto-send path.
    AddFriendDialog* m_activeAddFriendDialog = nullptr;
    GlobalSearchDialog* m_activeContactGroupSearchDialog = nullptr;
    FavoritesView* m_favoritesView = nullptr;
    SettingsView* m_settingsView = nullptr;
    ProfileView* m_profileView = nullptr;

    static constexpr int MAX_HISTORY_LINES = 500;
    static constexpr quint16 DEFAULT_PORT = 8888;
};

#endif // MAINWINDOW_H

