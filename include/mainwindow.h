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
#include <QPixmap>
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

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QAction;
class QLabel;
class QListWidget;
class QLineEdit;
class QListWidgetItem;
class QPushButton;
class QStyledItemDelegate;

struct GroupInfoWorkspaceRow {
    QString commandId;
    QString title;
    QString detail;
    QString preview;
    QString keywords;
    bool accent = false;
    bool dangerous = false;
};

struct ContactWorkspaceRow {
    QString rowId;
    QString actionKey;
    QString title;
    QString detail;
    QString preview;
    QString keywords;
    bool accent = false;
    bool muted = false;
};

struct NotificationWorkspaceRow {
    QString id;
    QString title;
    QString detail;
    QString preview;
    QString keywords;
    bool accent = false;
    bool dangerous = false;
};

struct ProfileWorkspaceRow {
    QString id;
    QString title;
    QString detail;
    QString preview;
    QString keywords;
    bool accent = false;
};

struct UserEntryActionRow {
    QString commandId;
    QString title;
    QString detail;
    QString preview;
    QString keywords;
    bool accent = false;
    bool dangerous = false;
};

struct GroupMemberWorkspaceRow {
    QString memberId;
    QString displayName;
    QString role;
    QString status;
    QString relation;
    QString preview;
    bool enabled = true;
    bool self = false;
    bool online = false;
    bool friendRelation = false;
    bool pendingRelation = false;
    bool inviteCandidate = false;
    bool searchAddCandidate = false;
    bool removable = false;
    bool localGroup = false;
};

struct ChatHistoryWorkspaceRow {
    QModelIndex index;
    QString chatText;
    LocalSavedFileState savedFileState;
    bool isMediaMessage = false;
    QString title;
    QString detail;
    QString preview;
    QString keywords;
};

struct AvatarWorkspaceRow {
    QString id;
    QString title;
    QString detail;
    QString preview;
    QString keywords;
    bool accent = false;
    bool muted = false;
};

struct ComposerWorkspaceRow {
    QString id;
    QString title;
    QString detail;
    QString preview;
    QString keywords;
    bool enabled = true;
    bool phrase = false;
    QString phraseText;
    bool insertMode = false;
};

struct TransferWorkspaceRow {
    QString id;
    QString title;
    QString detail;
    QString preview;
    QString keywords;
    bool enabled = true;
    QString statusTone;
};

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
    void onShowContactWorkspace(const QString& initialFilter = QString());
    void onShowFriendManager();
    void onShowGlobalSearch(const QString& initialFilter = QString());
    void onShowCreateMenu();
    void onShowGroupMemberWorkspace(const QString& initialFilter = QString());
    void onShowTransferWorkspace();
    void onShowComposerWorkspace();
    void onShowChatHistoryWorkspace();
    void onShowNotificationWorkspace();
    void onShowFriendNotifications();
    void onShowGroupNotifications();
    void onEditGroupAnnouncement();
    void onInsertEmoji();
    void onInsertMention();
    void onResumeSavedOutgoingTransfer();
    void onClearSavedOutgoingTransfer();
    void onFileTransferStatusChanged(const QString& fileName, const QString& transferId, const QString& reason, qint64 receivedBytes, qint64 totalBytes);

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
    void appendMessage(const Message& msg);
    void appendSystemMessage(const QString& text);
    void applyTransferSendState(const TransferSendUiState& state);
    bool ensureTransferTargetReady(const QString& kind, const QString& targetName, bool isLocalGroup);
    bool selectTransferFile(const TransferSelectionPlan& selectionPlan,
                            QString* filePath,
                            QFileInfo* fileInfo,
                            QString* fileSize = nullptr);
    bool selectTransferFileContext(const TransferSelectionPlan& selectionPlan,
                                   SelectedTransferFile* selectedFile);
    bool handleTransferSelectionUiState(TransferSelectionUiState* selectionState);
    void appendTransferCompletionState(const TransferSendUiState& state,
                                       bool includeSystemMessage,
                                       bool includeCard,
                                       const QColor& cardForeground,
                                       const QColor& cardBackground,
                                       const QString& mediaKind = QString(),
                                       const QString& openPath = QString());
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
                                             bool isVideo);
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
    void showReceivedTransferWorkspace(const ReceivedTransferContext& context,
                                       const QString& displayName,
                                       bool saved);
    bool handleReceivedTransferMessage(const Message& msg,
                                       const QString& displayName);
    TransferReceiveRenderPlan receivedTransferPersistencePlan(const ReceivedTransferContext& context,
                                                              const QString& displayName,
                                                              bool saved) const;
    void appendTransferChatListItem(const TransferChatListItemUiState& itemState);
    void appendTransferHistoryCard(const QString& senderId,
                                   const QString& senderName,
                                   const QString& mediaKind,
                                   const QString& fileName,
                                   const QString& openPath,
                                   bool alignRight);
    QStandardItem* createChatMessageItem(const QString& text,
                                         const QString& senderId,
                                         const QString& senderName,
                                         bool alignRight,
                                         const QColor& foreground,
                                         const QColor& background,
                                         const QString& mediaKind = QString(),
                                         const QString& openPath = QString(),
                                         const QPixmap& mediaPreview = QPixmap()) const;
    void applyChatItemVisualMetadata(QStandardItem* item,
                                     const QString& senderId,
                                     const QString& senderName,
                                     const QString& mediaKind = QString(),
                                     const QString& openPath = QString(),
                                     const QPixmap& mediaPreview = QPixmap()) const;
    QString avatarPathForUser(const QString& userId) const;
    void cacheKnownUserAvatars();
    void loadPeerAvatarIndexFromStorage();
    QPixmap chatAvatarPixmap(const QString& userId, const QString& displayName, int side = 36) const;
    QPixmap groupAvatarPixmap(const QString& groupId, const QString& groupName, int side = 32) const;
    QPixmap chatAttachmentDecoration(const QString& senderId,
                                     const QString& senderName,
                                     const QPixmap& mediaPreview,
                                     bool isVideo,
                                     int previewWidth = 180,
                                     int previewHeight = 140) const;
    bool openChatAttachmentFromIndex(const QModelIndex& index);
    bool showChatImagePreview(const QString& filePath,
                              const QString& titleText = QString()) const;
    bool showChatVideoPreview(const QString& filePath,
                              const QString& titleText = QString()) const;
    QDialog* createMediaPreviewDialog(const QString& geometryKey,
                                      const QString& windowTitle) const;
    QStringList visibleChatImagePaths() const;
    int visibleChatImageIndex(const QString& filePath) const;
    QPixmap videoPreviewFrame(const QString& filePath,
                              const QSize& targetSize = QSize(320, 220)) const;
    qint64 videoDurationMs(const QString& filePath) const;
    QString formatDurationLabel(qint64 durationMs) const;
    QString extractHistorySenderName(const QString& line) const;
    QString extractHistorySenderId(const QString& line,
                                   const QString& peerId,
                                   const QString& senderName) const;
    QString extractHistoryAttachmentName(const QString& line) const;
    QString extractHistoryAttachmentPath(const QString& line,
                                        const QString& toolTipText = QString()) const;
    QString detectHistoryMediaKind(const QString& line,
                                   QString* fileName = nullptr) const;
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
    bool saveProfileToSqlite() const;
    void persistPeerAvatarIndexEntry(const QString& userId, const QString& avatarBase64);
    bool sendTransferWithProgress(const QString& filePath, const QString& receiverId, const QString& targetName, const QString& kind, bool asImage, QString* transferSummary = nullptr, bool* canceled = nullptr);
    QStandardItem* findUserItem(const QString& userId);
    void refreshFriendList();
    void refreshGroupMemberPanel();
    void refreshComposerState();
    void refreshTransferWorkspaceCard(const TransferRecoveryUiState* recoveryState = nullptr,
                                      const TransferStatusEvent* latestEvent = nullptr,
                                      const TransferSendUiState* sendState = nullptr);
    TransferWorkspaceSummaryState currentTransferWorkspaceSummary(const TransferRecoveryUiState* recoveryState = nullptr,
                                                                 const TransferStatusEvent* latestEvent = nullptr,
                                                                 const TransferSendUiState* sendState = nullptr) const;
    void refreshMainWorkbenchChrome();
    void setTransferWorkspaceSendState(const TransferSendUiState& state);
    void clearTransferWorkspaceSendState();
    void updateSavedOutgoingTransferRecoveryUi(bool announce = false);
    QJsonObject readLocalGovernanceArtifact(const QString& fileName) const;
    void showFileTransferStatusEvent(const QString& fileName, const QString& transferId, const QString& reason, qint64 receivedBytes, qint64 totalBytes);
    LocalSavedFileState savedFileActionState(const QModelIndex& index) const;
    ChatContextSavedFileState chatContextSavedFileState(const LocalSavedFileState& savedFileState) const;
    bool copySavedFilePathToClipboard(const ChatContextSavedFileCommand& command);
    bool openSavedFileFromState(const LocalSavedFileState& savedFileState, const ChatContextSavedFileCommand& command);
    bool openSavedFolderFromState(const LocalSavedFileState& savedFileState, const ChatContextSavedFileCommand& command);
    void showSavedFileWorkspace(const LocalSavedFileState& savedFileState,
                                const QString& chatText,
                                const QString& fallbackStatusMessage = QString());
    void showChatHistoryWorkspaceForRow(int preferredRow = -1);
    void clearSavedFileWorkspace();
    void setTransferWorkspaceSavedFileState(const LocalSavedFileState& savedFileState,
                                            const QString& chatText = QString());
    QString transferWorkspaceStatusSnapshotText() const;
    void showAvatarWorkspace();
    void showAvatarWorkspaceMenu(const QPoint& globalPos);
    void refreshAvatarWorkspaceCard();
    void copyTextWithStatus(const QString& text, const QString& statusMessage, int timeoutMs = 1800);
    bool showChoiceDialog(const QString& title,
                          const QString& message,
                          const QString& confirmText,
                          const QString& cancelText,
                          bool destructiveConfirm,
                          const QString& canceledStatusMessage = QString(),
                          int canceledStatusTimeoutMs = 1600,
                          QWidget* parent = nullptr) const;
    QString showSingleFieldDialog(const QString& dialogObjectName,
                                  const QString& title,
                                  const QString& subTitle,
                                  const QString& fieldLabel,
                                  const QString& placeholder,
                                  const QString& initialValue,
                                  bool multiline,
                                  bool* accepted,
                                  QWidget* parent = nullptr) const;
    QString showItemPickerDialog(const QString& dialogObjectName,
                                 const QString& title,
                                 const QString& subTitle,
                                 const QString& label,
                                 const QStringList& items,
                                 bool* accepted,
                                 QWidget* parent = nullptr) const;
    bool confirmAction(const QString& title,
                       const QString& message,
                       const QString& canceledStatusMessage = QString(),
                       int canceledStatusTimeoutMs = 1600,
                       QWidget* parent = nullptr);
    void showWarningDialog(const QString& title,
                           const QString& message,
                           QWidget* parent = nullptr) const;
    bool confirmDestructiveAction(const QString& title,
                                  const QString& message,
                                  const QString& confirmText = QStringLiteral("继续"),
                                  const QString& cancelText = QStringLiteral("取消"),
                                  QWidget* parent = nullptr) const;
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
    QString selectOpenFilePath(const QString& title,
                               const QString& initialPath,
                               const QString& filters,
                               QWidget* parent = nullptr) const;
    QString selectSaveFilePath(const QString& title,
                               const QString& initialPath,
                               const QString& filters,
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
    bool addAccountToCurrentLocalGroup(const QString& account, QWidget* parent = nullptr);
    bool handleGroupMemberSearchSubmit(const QString& text, QWidget* parent = nullptr);
    bool handleGroupMemberEntryActivated(const QString& targetId, QWidget* parent = nullptr);
    void copyVisibleGroupMembers(bool onlineOnly);
    bool promptAndSetGroupMemberRemark(const QString& memberId, QWidget* parent = nullptr);
    bool removeGroupMemberWithConfirmation(const QString& memberId, QWidget* parent = nullptr);
    bool showCreateGroupWorkspace(QWidget* parent = nullptr);
    bool showInviteFriendToGroupWorkspace(const QString& groupId, QWidget* parent = nullptr);
    bool showInviteAccountToGroupWorkspace(const QString& groupId, QWidget* parent = nullptr);
    bool showRenameGroupWorkspace(const QString& groupId, QWidget* parent = nullptr);
    bool showEditGroupAnnouncementWorkspace(QWidget* parent = nullptr);
    void showProfileWorkspace();
    void showUserEntryWorkspace(const QString& targetId, const QString& fallbackLabel = QString());
    void showGroupInfoWorkspace();
    QList<GroupInfoWorkspaceRow> buildGroupInfoWorkspaceRows(bool localGroupContext,
                                                             bool removedFromPublicGroup,
                                                             const QString& currentGroupId,
                                                             const QString& currentGroupName,
                                                             const QString& ownerName,
                                                             const QString& announcementText) const;
    void fillGroupInfoWorkspaceList(QListWidget* listWidget,
                                    QLabel* statsLabel,
                                    const QList<GroupInfoWorkspaceRow>& rows,
                                    const QString& filter,
                                    const QString& emptyPreviewText) const;
    GroupInfoWorkspaceRow* selectedGroupInfoWorkspaceRow(QList<GroupInfoWorkspaceRow>& rows,
                                                         QListWidget* listWidget) const;
    QString groupInfoWorkspaceCardText(bool localGroupContext,
                                       const QString& currentGroupName,
                                       const QString& currentGroupId,
                                       const QString& ownerName,
                                       const QString& announcementText) const;
    QString groupInfoWorkspaceStatusText(bool localGroupContext,
                                         bool removedFromPublicGroup,
                                         const QString& currentGroupName,
                                         const QString& currentGroupId,
                                         const QString& ownerName,
                                         const QString& announcementText) const;
    void updateGroupInfoWorkspaceActionState(QPushButton* openBtn,
                                            QPushButton* copyCardBtn,
                                            QPushButton* copyStatusBtn,
                                            QListWidget* listWidget,
                                            QList<GroupInfoWorkspaceRow>& rows) const;
    void runGroupInfoWorkspaceCommand(const QString& commandId,
                                      bool localGroupContext,
                                      const QString& currentGroupId,
                                      const QString& currentGroupName,
                                      QDialog* dialog);
    QList<ContactWorkspaceRow> buildContactWorkspaceRows() const;
    void fillContactWorkspaceList(QListWidget* listWidget,
                                  QLabel* statsLabel,
                                  const QList<ContactWorkspaceRow>& rows,
                                  const QString& filter,
                                  const QString& emptyPreviewText) const;
    ContactWorkspaceRow* selectedContactWorkspaceRow(QList<ContactWorkspaceRow>& rows,
                                                     QListWidget* listWidget) const;
    QString contactWorkspaceStatusText(QList<ContactWorkspaceRow>& rows,
                                       QListWidget* listWidget) const;
    QString contactWorkspaceClipboardText(QList<ContactWorkspaceRow>& rows,
                                          QListWidget* listWidget) const;
    QString selectedContactWorkspaceSearchAccount(ContactWorkspaceRow* row) const;
    void updateContactWorkspaceActionState(QPushButton* openBtn,
                                           QPushButton* searchBtn,
                                           QPushButton* copyCardBtn,
                                           QPushButton* copyStatusBtn,
                                           QPushButton* friendManagerBtn,
                                           QListWidget* listWidget,
                                           QList<ContactWorkspaceRow>& rows) const;
    void runContactWorkspaceOpenAction(ContactWorkspaceRow* row,
                                       QDialog* dialog);
    QList<NotificationWorkspaceRow> buildNotificationWorkspaceRows(const QString& summaryText,
                                                                   const QString& friendPlanText,
                                                                   const QString& groupPlanText) const;
    void fillNotificationWorkspaceList(QListWidget* listWidget,
                                       QLabel* statsLabel,
                                       const QList<NotificationWorkspaceRow>& rows,
                                       const QString& filter,
                                       const QString& emptyPreviewText) const;
    NotificationWorkspaceRow* selectedNotificationWorkspaceRow(QList<NotificationWorkspaceRow>& rows,
                                                               QListWidget* listWidget) const;
    QString notificationWorkspaceRowClipboardText(NotificationWorkspaceRow* row,
                                                  const QString& summaryText,
                                                  const QString& friendPlanText,
                                                  const QString& groupPlanText) const;
    void updateNotificationWorkspaceActionState(QPushButton* openBtn,
                                                QPushButton* copyCardBtn,
                                                QPushButton* copyStatusBtn,
                                                QListWidget* listWidget,
                                                QList<NotificationWorkspaceRow>& rows) const;
    void runNotificationWorkspaceCommand(NotificationWorkspaceRow* row,
                                         QDialog* dialog,
                                         const QString& summaryText,
                                         const QString& friendPlanText,
                                         const QString& groupPlanText);
    QList<ProfileWorkspaceRow> buildProfileWorkspaceRows() const;
    void fillProfileWorkspaceList(QListWidget* listWidget,
                                  QLabel* statsLabel,
                                  const QList<ProfileWorkspaceRow>& rows,
                                  const QString& filter,
                                  const QString& emptyPreviewText) const;
    ProfileWorkspaceRow* selectedProfileWorkspaceRow(QList<ProfileWorkspaceRow>& rows,
                                                     QListWidget* listWidget) const;
    QString profileWorkspaceCardText() const;
    QString profileWorkspaceStatusText() const;
    QString profileWorkspaceClipboardText(ProfileWorkspaceRow* row) const;
    void updateProfileWorkspaceActionState(QPushButton* openBtn,
                                           QPushButton* copyCardBtn,
                                           QPushButton* copyStatusBtn,
                                           QListWidget* listWidget,
                                           QList<ProfileWorkspaceRow>& rows) const;
    void runProfileWorkspaceOpenAction(ProfileWorkspaceRow* row,
                                       QDialog* dialog);
    QList<UserEntryActionRow> buildUserEntryWorkspaceRows(const QString& targetId,
                                                          const QString& displayName,
                                                          bool isLocalGroup) const;
    void fillUserEntryWorkspaceList(QListWidget* listWidget,
                                    QLabel* statsLabel,
                                    const QList<UserEntryActionRow>& rows,
                                    const QString& filter,
                                    const QString& emptyPreviewText) const;
    UserEntryActionRow* selectedUserEntryWorkspaceRow(QList<UserEntryActionRow>& rows,
                                                      QListWidget* listWidget) const;
    QString userEntryWorkspaceCardText(const QString& targetId,
                                       const QString& displayName,
                                       bool isLocalGroup) const;
    QString userEntryWorkspaceStatusText(const QString& targetId,
                                         const QString& displayName,
                                         bool isLocalGroup) const;
    QString userEntryWorkspaceClipboardText(UserEntryActionRow* row,
                                            const QString& targetId,
                                            const QString& displayName,
                                            bool isLocalGroup) const;
    void updateUserEntryWorkspaceActionState(QPushButton* openBtn,
                                             QPushButton* copyCardBtn,
                                             QPushButton* copyStatusBtn,
                                             QListWidget* listWidget,
                                             QList<UserEntryActionRow>& rows) const;
    void runUserEntryWorkspaceCommand(UserEntryActionRow* row,
                                      const QString& targetId,
                                      const QString& displayName,
                                      bool isLocalGroup,
                                      QDialog* dialog);
    QString groupMemberWorkspaceRoleText(bool localGroupContext,
                                         const QString& ownerId,
                                         const QString& memberId) const;
    QList<GroupMemberWorkspaceRow> buildGroupMemberWorkspaceRows(bool localGroupContext,
                                                                 bool removedFromPublicGroup,
                                                                 const QString& ownerId,
                                                                 const QString& filter) const;
    void fillGroupMemberWorkspaceList(QListWidget* memberList,
                                      QLabel* statsLabel,
                                      QLabel* subTitleLabel,
                                      const QList<GroupMemberWorkspaceRow>& rows,
                                      bool localGroupContext,
                                      bool removedFromPublicGroup,
                                      const QString& currentGroupName,
                                      const QString& ownerName) const;
    GroupMemberWorkspaceRow* selectedGroupMemberWorkspaceRow(QList<GroupMemberWorkspaceRow>& rows,
                                                             QListWidget* memberList) const;
    QString groupMemberWorkspaceCardText(GroupMemberWorkspaceRow* row) const;
    QString groupMemberWorkspaceInviteText(GroupMemberWorkspaceRow* row,
                                           bool localGroupContext,
                                           const QString& currentGroupName) const;
    QString groupMemberWorkspaceVisibleMembersText(QListWidget* memberList,
                                                   bool onlineOnly) const;
    QString groupMemberWorkspacePrimaryCommand(GroupMemberWorkspaceRow* row) const;
    void updateGroupMemberWorkspaceActionState(QPushButton* chatBtn,
                                               QPushButton* inviteBtn,
                                               QPushButton* addFriendBtn,
                                               QPushButton* remarkBtn,
                                               QPushButton* removeBtn,
                                               QPushButton* copyMemberBtn,
                                               QPushButton* copyVisibleBtn,
                                               QPushButton* copyOnlineBtn,
                                               QPushButton* copyInviteTextBtn,
                                               QListWidget* memberList,
                                               QList<GroupMemberWorkspaceRow>& rows,
                                               bool localGroupContext,
                                               bool removedFromPublicGroup) const;
    QList<ChatHistoryWorkspaceRow> buildChatHistoryWorkspaceRows() const;
    void fillChatHistoryWorkspaceList(QListWidget* messageList,
                                      QLabel* statsLabel,
                                      const QList<ChatHistoryWorkspaceRow>& rows,
                                      const QString& filter,
                                      int preferredRow) const;
    ChatHistoryWorkspaceRow* selectedChatHistoryWorkspaceRow(QList<ChatHistoryWorkspaceRow>& rows,
                                                             QListWidget* messageList) const;
    QString chatHistoryWorkspaceSummaryText(ChatHistoryWorkspaceRow* row) const;
    void updateChatHistoryWorkspaceActionState(QPushButton* copySummaryBtn,
                                               QPushButton* quoteBtn,
                                               QPushButton* forwardBtn,
                                               QPushButton* resendBtn,
                                               QPushButton* copyMediaBtn,
                                               QPushButton* openFileWorkspaceBtn,
                                               QPushButton* openSavedFileBtn,
                                               QPushButton* openFolderBtn,
                                               QPushButton* copyPathBtn,
                                               QListWidget* messageList,
                                               QList<ChatHistoryWorkspaceRow>& rows) const;
    QString avatarWorkspaceCardText() const;
    QString avatarWorkspaceStatusText() const;
    QList<AvatarWorkspaceRow> buildAvatarWorkspaceRows() const;
    void fillAvatarWorkspaceList(QListWidget* listWidget,
                                 QLabel* statsLabel,
                                 const QList<AvatarWorkspaceRow>& rows,
                                 const QString& filter,
                                 const QString& emptyPreviewText) const;
    AvatarWorkspaceRow* selectedAvatarWorkspaceRow(QList<AvatarWorkspaceRow>& rows,
                                                   QListWidget* listWidget) const;
    QString avatarWorkspaceClipboardText(AvatarWorkspaceRow* row) const;
    void updateAvatarWorkspaceActionState(QPushButton* openBtn,
                                          QPushButton* copyCardBtn,
                                          QPushButton* copyStatusBtn,
                                          QListWidget* listWidget,
                                          QList<AvatarWorkspaceRow>& rows) const;
    void runAvatarWorkspaceCommand(AvatarWorkspaceRow* row,
                                   QDialog* dialog);
    QList<ComposerWorkspaceRow> buildComposerWorkspaceRows(const ChatContextComposerState& composerState,
                                                           const QString& draftText,
                                                           const QString& clipboardText) const;
    void fillComposerWorkspaceList(QListWidget* actionList,
                                   QLabel* statsLabel,
                                   const QList<ComposerWorkspaceRow>& rows,
                                   const QString& filter,
                                   int draftLength) const;
    ComposerWorkspaceRow* selectedComposerWorkspaceRow(QList<ComposerWorkspaceRow>& rows,
                                                       QListWidget* actionList) const;
    QString composerWorkspaceSummaryText(const QString& targetName) const;
    void updateComposerWorkspaceActionState(QPushButton* replaceDraftBtn,
                                            QPushButton* appendDraftBtn,
                                            QPushButton* clearDraftBtn,
                                            QPushButton* sendBtn,
                                            QPushButton* openImageBtn,
                                            QPushButton* openFileBtn,
                                            QPushButton* copySummaryBtn,
                                            QListWidget* actionList,
                                            QList<ComposerWorkspaceRow>& rows) const;
    QString transferWorkspaceArtifactPath(const QString& fileName) const;
    QList<TransferWorkspaceRow> buildTransferWorkspaceRows() const;
    void fillTransferWorkspaceList(QListWidget* stateList,
                                   QLabel* statsLabel,
                                   const QList<TransferWorkspaceRow>& rows,
                                   const QString& filter) const;
    TransferWorkspaceRow* selectedTransferWorkspaceRow(QList<TransferWorkspaceRow>& rows,
                                                       QListWidget* stateList) const;
    QString transferWorkspaceSelectedRowId(QList<TransferWorkspaceRow>& rows,
                                           QListWidget* stateList) const;
    void updateTransferWorkspaceActionState(QPushButton* sendFileBtn,
                                            QPushButton* sendMediaBtn,
                                            QPushButton* resumeBtn,
                                            QPushButton* clearBtn,
                                            QPushButton* copyDiagBtn,
                                            QPushButton* openFileBtn,
                                            QPushButton* openFolderBtn,
                                            QPushButton* copyPathBtn,
                                            QPushButton* copySnapshotBtn,
                                            QListWidget* stateList,
                                            QList<TransferWorkspaceRow>& rows) const;
    bool openUserTargetById(const QString& targetId);
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
    bool handleSavedFileContextCommand(const QString& commandId,
                                       const QString& chatText,
                                       const LocalSavedFileState& savedFileState);
    void setChatDraftText(const QString& text, const QString& statusMessage, int timeoutMs = 1400);
    void insertChatDraftText(const QString& text, const QString& statusMessage, int timeoutMs = 1400);
    ChatContextComposerState currentChatContextComposerState() const;
    bool applyChatContextComposerCommand(const QString& commandId);
    QAction* addChatContextAction(QMenu& menu,
                                  const QString& title,
                                  const QString& tip,
                                  const QString& commandId,
                                  bool enabled = true);
    bool handleChatContextCommand(const QString& commandId,
                                  const QString& chatText,
                                  const LocalSavedFileState& savedFileState);
    bool isCurrentUserRemovedFromPublicGroup() const;
    void switchToLocalGroup(const QString& groupId, const QString& groupName);
    void searchAndAddAccount(const QString& account, QWidget* warningParent = nullptr);
    void loadAvatar();
    QString contactDisplayName(const QString& userId) const;
    bool isContactOnline(const QString& userId) const;
    QString groupOwnerId(const QString& groupId) const;
    bool isCurrentUserGroupOwner(const QString& groupId) const;
    bool canCurrentUserManageServerGroup(const QString& groupId) const;
    bool requestServerGroupMemberUpdate(const QString& memberId, const QString& action);
    QString e2eSessionStatusText(const QString& peerId) const;
    void copyE2ESessionStatus(const QString& peerId);
    QString getFriendFilePath() const;
    QString getGroupFilePath() const;
    QString getAvatarFilePath() const;
    void saveFriends() const;
    void saveLocalGroups() const;
    void updateUnreadState();
    void clearUnreadState();
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

    Ui::MainWindow* ui;
    Client* m_client;
    ClientStorage m_clientStorage;
    QStandardItemModel* m_userListModel;
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
    QMap<QString, QString> m_localGroupNames;
    QMap<QString, QString> m_localGroupAnnouncements;
    QMap<QString, QStringList> m_localGroupMembers;
    QMap<QString, QString> m_serverGroupNames;
    QMap<QString, QString> m_serverGroupAnnouncements;
    QMap<QString, QString> m_serverGroupOwners;
    QMap<QString, QStringList> m_serverGroupMembers;
    QMap<QString, QString> m_serverGroupMemberNames;
    QMap<QString, QString> m_serverGroupMemberRoles;
    QMap<QString, QJsonArray> m_serverGroupAuditEvents;
    QMap<QString, QJsonObject> m_removedServerGroups;
    bool m_hasServerGroupSnapshot;
    bool m_wasInPublicServerGroup;
    QMap<QString, ChatUser> m_knownUsers;
    QMap<QString, QString> m_peerAvatarIndex;
    QString m_contactFilter;
    QString m_privateChatTarget;
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayMenu;
    QAction* m_resumeSavedTransferAction;
    QAction* m_clearSavedTransferAction;
    QAction* m_copyLastTransferStatusAction;
    QString m_lastTransferStatusDiagnostic;
    bool m_hasLastTransferRecoveryUiState;
    TransferRecoveryUiState m_lastTransferRecoveryUiState;
    bool m_hasLastTransferStatusEvent;
    TransferStatusEvent m_lastTransferStatusEvent;
    bool m_hasTransferWorkspaceSendState;
    TransferSendUiState m_transferWorkspaceSendState;
    bool m_hasTransferWorkspaceSavedFileState;
    LocalSavedFileState m_transferWorkspaceSavedFileState;
    QString m_transferWorkspaceSavedChatText;
    int m_unreadCount;
    bool m_isQuitting;

    static constexpr int MAX_HISTORY_LINES = 500;
    static constexpr quint16 DEFAULT_PORT = 8888;
};

#endif // MAINWINDOW_H
