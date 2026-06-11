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

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QAction;

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
    void onShowCreateMenu();
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
    bool applyTransferSelectionUiStateFeedback(const TransferSelectionUiState& selectionState);
    bool confirmTransferSelectionWarning(TransferSelectionUiState* selectionState);
    void appendTransferCompletionState(const TransferSendUiState& state,
                                       bool includeSystemMessage,
                                       bool includeCard,
                                       const QColor& cardForeground,
                                       const QColor& cardBackground);
    void appendMediaPreviewItem(const QString& text,
                                const QPixmap& pixmap,
                                bool isVideo,
                                bool alignRight);
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
    bool saveProfileToSqlite() const;
    bool sendTransferWithProgress(const QString& filePath, const QString& receiverId, const QString& targetName, const QString& kind, bool asImage, QString* transferSummary = nullptr, bool* canceled = nullptr);
    QStandardItem* findUserItem(const QString& userId);
    void refreshFriendList();
    void refreshGroupMemberPanel();
    void refreshComposerState();
    void updateSavedOutgoingTransferRecoveryUi(bool announce = false);
    void showFileTransferStatusEvent(const QString& fileName, const QString& transferId, const QString& reason, qint64 receivedBytes, qint64 totalBytes);
    LocalSavedFileState savedFileActionState(const QModelIndex& index) const;
    ChatContextSavedFileState chatContextSavedFileState(const LocalSavedFileState& savedFileState) const;
    bool copySavedFilePathToClipboard(const ChatContextSavedFileCommand& command);
    bool openSavedFileFromState(const LocalSavedFileState& savedFileState, const ChatContextSavedFileCommand& command);
    bool openSavedFolderFromState(const LocalSavedFileState& savedFileState, const ChatContextSavedFileCommand& command);
    void copyTextWithStatus(const QString& text, const QString& statusMessage, int timeoutMs = 1800);
    bool handleSavedFileContextCommand(const QString& commandId, const LocalSavedFileState& savedFileState);
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

    static constexpr int MAX_HISTORY_LINES = 500;
    static constexpr quint16 DEFAULT_PORT = 8888;
};

#endif // MAINWINDOW_H
