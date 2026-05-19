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
    void onNewMessage(const Message& msg);
    void onUserJoined(const QString& userId, const QString& userName);
    void onUserLeft(const QString& userId, const QString& userName);
    void onUserListUpdated(const QVector<ChatUser>& users);
    void onPrivateChat(const QModelIndex& index);
    void onClientDisconnected();
    void onClientError(const QString& error);
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void onClearHistory();
    void onAddFriend();
    void onUploadAvatar();
    void onBackToGroupChat();
    void onFriendRequestReceived(const QString& senderId, const QString& senderName);
    void onFriendSearchResult(const QString& account, const QString& userId, const QString& userName, bool found, bool online);
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

signals:
    void logoutRequested();

private:
    void setupUi();
    void setupTray();
    void appendMessage(const Message& msg);
    void appendSystemMessage(const QString& text);
    void loadHistory(const QString& peerId = QString());
    void saveHistory(const QString& peerId, const QString& content);
    QString getHistoryFilePath(const QString& peerId);
    QStandardItem* findUserItem(const QString& userId);
    void refreshFriendList();
    void refreshGroupMemberPanel();
    void loadAvatar();
    QString contactDisplayName(const QString& userId) const;
    bool isContactOnline(const QString& userId) const;
    QString getFriendFilePath() const;
    QString getAvatarFilePath() const;
    void saveFriends() const;
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
    QMap<QString, QString> m_friendNames;
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
