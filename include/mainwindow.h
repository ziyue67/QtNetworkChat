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
#include "client.h"
#include "chatuser.h"
#include "message.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
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

signals:
    void logoutRequested();

private:
    void setupUi();
    void setupTray();
    void appendMessage(const Message& msg);
    void appendSystemMessage(const QString& text);
    void loadHistory(const QString& peerId = QString());
    void saveHistory(const QString& peerId, const QString& content);
    bool ensureClientDatabase() const;
    bool saveHistoryToSqlite(const QString& peerId, const QString& content) const;
    bool hasHistoryRecords(const QString& peerId) const;
    QStringList historyRecordsForDate(const QString& peerId, const QDate& date) const;
    QStringList historyRecordsForExport(const QString& peerId) const;
    void clearHistoryRecords(const QString& peerId) const;
    QString clientDbPath() const;
    bool saveProfileToSqlite() const;
    QString getHistoryFilePath(const QString& peerId) const;
    bool sendTransferWithProgress(const QString& filePath, const QString& receiverId, const QString& targetName, const QString& kind, bool asImage);
    QStandardItem* findUserItem(const QString& userId);
    void refreshFriendList();
    void refreshGroupMemberPanel();
    void refreshComposerState();
    void switchToLocalGroup(const QString& groupId, const QString& groupName);
    void searchAndAddAccount(const QString& account, QWidget* warningParent = nullptr);
    void loadAvatar();
    QString contactDisplayName(const QString& userId) const;
    bool isContactOnline(const QString& userId) const;
    QString groupOwnerId(const QString& groupId) const;
    bool isCurrentUserGroupOwner(const QString& groupId) const;
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
    QStandardItemModel* m_userListModel;
    QStandardItemModel* m_chatModel;
    QStandardItemModel* m_groupMemberModel;
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
    QMap<QString, ChatUser> m_knownUsers;
    QString m_contactFilter;
    QString m_privateChatTarget;
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayMenu;
    int m_unreadCount;
    bool m_isQuitting;

    static constexpr int MAX_HISTORY_LINES = 500;
    static constexpr quint16 DEFAULT_PORT = 8888;
};

#endif // MAINWINDOW_H
