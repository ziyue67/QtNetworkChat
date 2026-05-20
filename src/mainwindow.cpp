#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QMenu>
#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTextEdit>
#include <QPushButton>
#include <QListView>
#include <QStatusBar>
#include <QPixmap>
#include <QImage>
#include <QRegularExpression>
#include <QKeyEvent>
#include <QLineEdit>
#include <QClipboard>
#include <QApplication>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QTabWidget>

MainWindow::MainWindow(Client* client, const QString& userId, const QString& userName, QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_client(client)
    , m_userListModel(new QStandardItemModel(this))
    , m_chatModel(new QStandardItemModel(this))
    , m_groupMemberModel(new QStandardItemModel(this))
    , m_currentUserId(userId)
    , m_currentUserName(userName)
    , m_privateChatTarget(QString())
    , m_trayIcon(new QSystemTrayIcon(this))
    , m_unreadCount(0)
    , m_isQuitting(false)
{
    ui->setupUi(this);
    setupUi();
    setupTray();

    if (m_client && !m_client->parent()) {
        m_client->setParent(this);
    }
    if (!m_client) {
        QMessageBox::critical(this, "错误", "客户端未初始化");
        close();
        return;
    }

    setWindowTitle("QtNetworkChat - " + userName);
    ui->avatarLabel->setText(userName.left(1).toUpper());
    ui->profileNameLabel->setText("QQ: " + userId);
    ui->profileIdLabel->setText("昵称: " + userName);
    loadAvatar();
    ui->appTitleLabel->setText("Qt 聊天室");
    ui->chatTitleLabel->setText("公共聊天室");
    ui->chatHintLabel->setText(QString("账号 %1 · 双击左侧成员可私聊").arg(m_currentUserId));

    connect(m_client, &Client::connected, this, [this]() {
        appendSystemMessage("已连接服务器");
    });
    connect(m_client, &Client::disconnected, this, &MainWindow::onClientDisconnected);
    connect(m_client, &Client::newMessage, this, &MainWindow::onNewMessage);
    connect(m_client, &Client::userJoined, this, &MainWindow::onUserJoined);
    connect(m_client, &Client::userLeft, this, &MainWindow::onUserLeft);
    connect(m_client, &Client::userListUpdated, this, &MainWindow::onUserListUpdated);
    connect(m_client, &Client::connectionError, this, &MainWindow::onClientError);
    connect(m_client, &Client::friendRequestReceived, this, &MainWindow::onFriendRequestReceived);
    connect(m_client, &Client::friendSearchResult, this, &MainWindow::onFriendSearchResult);
    connect(m_client, &Client::friendRequestSent, this, &MainWindow::onFriendRequestSent);
    connect(m_client, &Client::friendResponseReceived, this, &MainWindow::onFriendResponseReceived);

    m_currentUserId = m_client->currentUserId();
    m_currentUserName = m_client->currentUserName();
    ui->profileNameLabel->setText("QQ: " + m_currentUserId);
    ui->profileIdLabel->setText("昵称: " + m_currentUserName);

    if (m_currentUserId.isEmpty()) {
        ui->statusbar->showMessage("已连接");
    } else {
        ui->statusbar->showMessage("已连接 - 用户ID: " + m_currentUserId);
    }
    loadHistory("group");
}

MainWindow::~MainWindow() {
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
}

void MainWindow::setupUi() {
    m_userListModel->setHorizontalHeaderLabels({"在线用户"});
    ui->userListView->setModel(m_userListModel);
    ui->userListView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    ui->chatListView->setModel(m_chatModel);

    m_groupMemberModel->setHorizontalHeaderLabels({"群成员"});
    ui->groupMemberListView->setModel(m_groupMemberModel);
    ui->groupMemberListView->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->messageEdit->setPlaceholderText("输入消息... (Enter 发送，Ctrl+Enter 换行)");
    ui->messageEdit->setFocus();
    ui->messageEdit->installEventFilter(this);

    ui->clearBtn->setObjectName("clearBtn");
    ui->fileBtn->setObjectName("toolBtn");
    ui->imageBtn->setObjectName("toolBtn");
    ui->emojiBtn->setObjectName("iconToolBtn");
    ui->mentionBtn->setObjectName("iconToolBtn");
    setStyleSheet(R"(
        QMainWindow, QWidget#centralwidget {
            background: #EEF3F8;
            font-family: "Microsoft YaHei", "Segoe UI";
            font-size: 13px;
            color: #263238;
        }
        QFrame#sidePanel {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #18C1F7, stop:0.55 #0B9DE8, stop:1 #0877C9);
        }
        QLabel#appTitleLabel {
            color: white;
            font-size: 21px;
            font-weight: 800;
            padding-bottom: 2px;
            letter-spacing: 1px;
        }
        QLabel#avatarLabel {
            background: white;
            color: #0B8DDF;
            border-radius: 36px;
            font-size: 30px;
            font-weight: 700;
            margin-left: 63px;
            margin-right: 63px;
        }
        QFrame#profileCard {
            background: rgba(255, 255, 255, 42);
            border: 1px solid rgba(255, 255, 255, 70);
            border-radius: 14px;
        }
        QLabel#profileNameLabel {
            color: white;
            font-size: 15px;
            font-weight: 800;
        }
        QLabel#profileIdLabel {
            color: rgba(255, 255, 255, 215);
            font-size: 12px;
        }
        QPushButton#copyAccountBtn, QPushButton#addFriendBtn, QPushButton#friendManagerBtn, QPushButton#groupChatBtn, QPushButton#uploadAvatarBtn {
            background: rgba(255, 255, 255, 225);
            color: #0B8DDF;
            border: none;
            border-radius: 11px;
            min-height: 28px;
            padding: 4px 8px;
            font-weight: 700;
        }
        QPushButton#copyAccountBtn:hover, QPushButton#addFriendBtn:hover, QPushButton#friendManagerBtn:hover, QPushButton#groupChatBtn:hover, QPushButton#uploadAvatarBtn:hover {
            background: white;
        }
        QLabel#onlineTitleLabel {
            color: rgba(255, 255, 255, 220);
            font-size: 14px;
            font-weight: 600;
            padding-top: 8px;
        }
        QPushButton#friendNoticeBtn, QPushButton#groupNoticeBtn {
            background: rgba(255, 255, 255, 225);
            color: #0B8DDF;
            border: none;
            border-radius: 13px;
            min-height: 28px;
            padding: 3px 8px;
            font-weight: 700;
        }
        QPushButton#friendNoticeBtn:hover, QPushButton#groupNoticeBtn:hover {
            background: white;
        }
        QLineEdit#contactSearchEdit {
            background: rgba(255, 255, 255, 235);
            color: #263238;
            border: 1px solid rgba(255, 255, 255, 105);
            border-radius: 15px;
            min-height: 30px;
            padding: 3px 12px;
        }
        QPushButton#globalSearchBtn, QPushButton#createMenuBtn {
            background: rgba(255, 255, 255, 225);
            color: #0B8DDF;
            border: none;
            border-radius: 15px;
            min-height: 30px;
            padding: 3px 10px;
            font-weight: 700;
        }
        QPushButton#createMenuBtn {
            font-size: 18px;
            padding: 0 10px;
        }
        QPushButton#globalSearchBtn:hover, QPushButton#createMenuBtn:hover {
            background: white;
        }
        QLineEdit#contactSearchEdit:focus {
            background: white;
            border: 1px solid white;
        }
        QListView#userListView {
            background: rgba(255, 255, 255, 38);
            color: white;
            border: 1px solid rgba(255, 255, 255, 70);
            border-radius: 16px;
            padding: 6px;
            outline: none;
        }
        QListView#userListView::item {
            height: 48px;
            border-radius: 11px;
            padding-left: 8px;
        }
        QListView#userListView::item:selected, QListView#userListView::item:hover {
            background: rgba(255, 255, 255, 75);
        }
        QFrame#chatHeader, QFrame#inputPanel, QListView#chatListView {
            background: white;
            border: 1px solid #DCE8F2;
            border-radius: 18px;
        }
        QFrame#chatPanel {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #EEF5FB, stop:1 #F7FAFD);
        }
        QFrame#groupInfoPanel {
            background: #F7FAFD;
            border-left: 1px solid #E4EBF2;
        }
        QFrame#announcementCard {
            background: white;
            border: 1px solid #E4EBF2;
            border-radius: 14px;
        }
        QLabel#announcementTitleLabel, QLabel#memberTitleLabel {
            color: #1F2D3D;
            font-size: 14px;
            font-weight: 800;
        }
        QLabel#announcementTitleLabel a {
            color: #1296F7;
            text-decoration: none;
        }
        QLabel#announcementBodyLabel {
            color: #7B8A99;
            font-size: 12px;
            line-height: 18px;
        }
        QLineEdit#memberSearchEdit {
            background: white;
            color: #263238;
            border: 1px solid #DDE7F0;
            border-radius: 15px;
            min-height: 30px;
            padding: 3px 12px;
        }
        QListView#groupMemberListView {
            background: white;
            border: 1px solid #E4EBF2;
            border-radius: 14px;
            padding: 6px;
            outline: none;
        }
        QListView#groupMemberListView::item {
            min-height: 34px;
            border-radius: 9px;
            padding-left: 6px;
        }
        QListView#groupMemberListView::item:selected, QListView#groupMemberListView::item:hover {
            background: #EAF7FF;
        }
        QLabel#chatTitleLabel {
            color: #1F2D3D;
            font-size: 18px;
            font-weight: 700;
        }
        QLabel#chatHintLabel {
            color: #8A99A8;
            font-size: 12px;
        }
        QListView#chatListView {
            padding: 14px;
            outline: none;
        }
        QListView#chatListView::item {
            min-height: 32px;
            padding: 8px 12px;
            margin: 4px 0;
            border-radius: 14px;
        }
        QListView#chatListView::item:hover {
            background: #F3F8FC;
        }
        QTextEdit#messageEdit {
            background: #F9FBFD;
            border: 1px solid #DDE7F0;
            border-radius: 14px;
            padding: 8px 10px;
            selection-background-color: #17B8F2;
        }
        QPushButton {
            background: #EFF5FA;
            color: #3A4A5A;
            border: 1px solid #D4E1EC;
            border-radius: 12px;
            padding: 7px 14px;
        }
        QPushButton:hover {
            background: #E5F0F8;
        }
        QPushButton#sendBtn {
            background: #12B7F5;
            color: white;
            border: none;
            font-weight: 700;
        }
        QPushButton#sendBtn:hover {
            background: #0AA4E5;
        }
        QPushButton#toolBtn, QPushButton#iconToolBtn {
            background: transparent;
            color: #52616F;
            border: none;
            border-radius: 14px;
            padding: 5px 10px;
            font-weight: 700;
        }
        QPushButton#iconToolBtn {
            min-width: 30px;
            font-size: 16px;
            padding: 4px 6px;
        }
        QPushButton#toolBtn:hover, QPushButton#iconToolBtn:hover {
            background: #EAF7FF;
            color: #1296F7;
        }
        QPushButton#clearBtn {
            color: #D35454;
        }
        QStatusBar {
            background: #EEF3F8;
            color: #607080;
        }
    )");

    QAction* addFriendAction = new QAction("加好友", this);
    QAction* friendManagerAction = new QAction("好友管理器", this);
    QAction* backGroupAction = new QAction("返回群聊", this);
    QAction* avatarAction = new QAction("上传头像", this);
    QAction* copyAccountAction = new QAction("复制账号", this);
    QAction* logoutAction = new QAction("退出登录", this);
    ui->menubar->addAction(addFriendAction);
    ui->menubar->addAction(friendManagerAction);
    ui->menubar->addAction(backGroupAction);
    ui->menubar->addAction(avatarAction);
    ui->menubar->addAction(copyAccountAction);
    ui->menubar->addAction(logoutAction);

    connect(addFriendAction, &QAction::triggered, this, &MainWindow::onAddFriend);
    connect(friendManagerAction, &QAction::triggered, this, &MainWindow::onShowFriendManager);
    connect(backGroupAction, &QAction::triggered, this, &MainWindow::onBackToGroupChat);
    connect(avatarAction, &QAction::triggered, this, &MainWindow::onUploadAvatar);
    connect(copyAccountAction, &QAction::triggered, this, &MainWindow::onCopyAccount);
    connect(logoutAction, &QAction::triggered, this, &MainWindow::onLogout);
    refreshFriendList();

    connect(ui->sendBtn, &QPushButton::clicked, this, &MainWindow::onSendMessage);
    connect(ui->fileBtn, &QPushButton::clicked, this, &MainWindow::onSendFile);
    connect(ui->imageBtn, &QPushButton::clicked, this, &MainWindow::onSendImage);
    connect(ui->emojiBtn, &QPushButton::clicked, this, &MainWindow::onInsertEmoji);
    connect(ui->mentionBtn, &QPushButton::clicked, this, &MainWindow::onInsertMention);
    connect(ui->userListView, &QListView::doubleClicked, this, &MainWindow::onPrivateChat);
    connect(ui->userListView, &QListView::customContextMenuRequested, this, &MainWindow::onUserContextMenu);
    connect(ui->contactSearchEdit, &QLineEdit::textChanged, this, &MainWindow::onContactSearchChanged);
    connect(ui->contactSearchEdit, &QLineEdit::returnPressed, this, [this]() {
        searchAndAddAccount(ui->contactSearchEdit->text().trimmed(), this);
    });
    connect(ui->globalSearchBtn, &QPushButton::clicked, this, [this]() {
        QString text = ui->contactSearchEdit->text().trimmed();
        if (text.isEmpty()) {
            onShowGlobalSearch();
        } else {
            searchAndAddAccount(text, this);
        }
    });
    connect(ui->createMenuBtn, &QPushButton::clicked, this, &MainWindow::onShowCreateMenu);
    connect(ui->friendNoticeBtn, &QPushButton::clicked, this, &MainWindow::onShowFriendNotifications);
    connect(ui->groupNoticeBtn, &QPushButton::clicked, this, &MainWindow::onShowGroupNotifications);
    connect(ui->announcementTitleLabel, &QLabel::linkActivated, this, &MainWindow::onEditGroupAnnouncement);
    connect(ui->copyAccountBtn, &QPushButton::clicked, this, &MainWindow::onCopyAccount);
    connect(ui->addFriendBtn, &QPushButton::clicked, this, &MainWindow::onShowQuickAddFriend);
    connect(ui->friendManagerBtn, &QPushButton::clicked, this, &MainWindow::onShowFriendManager);
    connect(ui->groupChatBtn, &QPushButton::clicked, this, &MainWindow::onBackToGroupChat);
    connect(ui->memberSearchEdit, &QLineEdit::textChanged, this, [this]() { refreshGroupMemberPanel(); });
    connect(ui->groupMemberListView, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        QString targetId = index.data(Qt::UserRole + 1).toString();
        if (targetId.isEmpty() || targetId == m_currentUserId) return;
        if (!m_friendIds.contains(targetId)) {
            m_friendIds << targetId;
            m_friendNames[targetId] = contactDisplayName(targetId);
            saveFriends();
            refreshFriendList();
            m_client->sendFriendRequest(targetId);
            appendSystemMessage("已自动添加群成员 QQ: " + targetId);
        }
        m_privateChatTarget = targetId;
        m_chatModel->clear();
        m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
        loadHistory(targetId);
        ui->chatTitleLabel->setText(QString("与 %1 私聊中").arg(contactDisplayName(targetId)));
        ui->chatHintLabel->setText(QString("QQ: %1 · %2 · 点击菜单“返回群聊”回到公共聊天室").arg(targetId, isContactOnline(targetId) ? "在线" : "离线"));
    });
    connect(ui->groupMemberListView, &QListView::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex index = ui->groupMemberListView->indexAt(pos);
        if (!index.isValid() || !m_privateChatTarget.startsWith("local_group_")) return;
        QString memberId = index.data(Qt::UserRole + 1).toString();
        if (memberId.isEmpty() || memberId == m_currentUserId) return;
        QMenu menu(this);
        QAction* chatAction = menu.addAction("私聊");
        QAction* removeAction = menu.addAction("移出群聊");
        QAction* selected = menu.exec(ui->groupMemberListView->viewport()->mapToGlobal(pos));
        if (selected == chatAction) {
            if (!m_friendIds.contains(memberId)) {
                m_friendIds << memberId;
                m_friendNames[memberId] = contactDisplayName(memberId);
                saveFriends();
                refreshFriendList();
                m_client->sendFriendRequest(memberId);
            }
            m_privateChatTarget = memberId;
            m_chatModel->clear();
            m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
            loadHistory(memberId);
            ui->chatTitleLabel->setText(QString("与 %1 私聊中").arg(contactDisplayName(memberId)));
            ui->chatHintLabel->setText(QString("QQ: %1 · %2 · 点击菜单“返回群聊”回到公共聊天室").arg(memberId, isContactOnline(memberId) ? "在线" : "离线"));
        } else if (selected == removeAction) {
            m_localGroupMembers[m_privateChatTarget].removeAll(memberId);
            saveLocalGroups();
            refreshGroupMemberPanel();
            appendSystemMessage(QString("已将 %1 移出群聊").arg(contactDisplayName(memberId)));
        }
    });
    connect(ui->clearBtn, &QPushButton::clicked, this, &MainWindow::onClearHistory);
    ui->announcementTitleLabel->setText("群公告 <a href=\"edit\">+</a>");
    ui->announcementTitleLabel->setTextFormat(Qt::RichText);
    ui->announcementTitleLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    refreshGroupMemberPanel();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == ui->messageEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ControlModifier) {
                ui->messageEdit->insertPlainText("\n");
            } else {
                onSendMessage();
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setupTray() {
    m_trayMenu = new QMenu(this);
    QAction* showAction = new QAction("显示窗口", this);
    QAction* copyAccountAction = new QAction("复制账号", this);
    QAction* logoutAction = new QAction("退出登录", this);
    QAction* quitAction = new QAction("退出", this);
    m_trayMenu->addAction(showAction);
    m_trayMenu->addAction(copyAccountAction);
    m_trayMenu->addAction(logoutAction);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(quitAction);

    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setToolTip("QtNetworkChat");
    m_trayIcon->setIcon(QIcon(":/icons/chat.png"));

    connect(showAction, &QAction::triggered, this, [this]() {
        this->show();
        this->raise();
        this->activateWindow();
    });
    connect(copyAccountAction, &QAction::triggered, this, &MainWindow::onCopyAccount);
    connect(logoutAction, &QAction::triggered, this, &MainWindow::onLogout);
    connect(quitAction, &QAction::triggered, this, [this]() {
        m_isQuitting = true;
        close();
    });
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);
}

void MainWindow::onSendMessage() {
    QString text = ui->messageEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;

    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        QString groupName = m_localGroupNames.value(m_privateChatTarget, "群聊");
        QString line = QString("[%1] <%2> %3").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), m_currentUserName, text);
        saveHistory(m_privateChatTarget, line);

        QStandardItem* item = new QStandardItem(line);
        item->setEditable(false);
        item->setForeground(QColor(20, 92, 160));
        item->setBackground(QColor(218, 241, 255));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_chatModel->appendRow(item);
        ui->messageEdit->clear();
        ui->chatHintLabel->setText(QString("本地群聊 · %1 · 消息已保存在本地记录").arg(groupName));
        ui->chatListView->scrollToBottom();
        return;
    }

    bool ok = false;
    if (!m_privateChatTarget.isEmpty()) {
        ok = m_client->sendPrivateMessage(m_privateChatTarget, text);
    } else {
        ok = m_client->sendMessage(text);
    }

    if (ok) {
        QString peerId = m_privateChatTarget.isEmpty() ? "group" : m_privateChatTarget;
        QString line = QString("[%1] <%2> %3").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), m_currentUserName, text);
        saveHistory(peerId, line);

        QStandardItem* item = new QStandardItem(line);
        item->setEditable(false);
        item->setForeground(QColor(20, 92, 160));
        item->setBackground(QColor(218, 241, 255));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_chatModel->appendRow(item);
        int rowCount = m_chatModel->rowCount();
        if (rowCount > MAX_HISTORY_LINES) {
            m_chatModel->removeRows(0, rowCount - MAX_HISTORY_LINES);
        }
        ui->chatListView->scrollToBottom();

        ui->messageEdit->clear();
    }
}

void MainWindow::onSendFile() {
    QString filePath = QFileDialog::getOpenFileName(this, "选择文件", QString(),
        "所有文件 (*.*);;文本文件 (*.txt);;图片 (*.png *.jpg *.jpeg *.gif)");
    if (filePath.isEmpty()) return;

    QFileInfo info(filePath);
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        QString line = QString("[%1] <%2> 发送了文件: %3").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), m_currentUserName, info.fileName());
        saveHistory(m_privateChatTarget, line);
        QStandardItem* item = new QStandardItem(line);
        item->setEditable(false);
        item->setForeground(QColor(20, 92, 160));
        item->setBackground(QColor(218, 241, 255));
        m_chatModel->appendRow(item);
        ui->chatListView->scrollToBottom();
        return;
    }

    bool ok = m_client->sendFile(filePath, m_privateChatTarget);
    if (ok) {
        appendSystemMessage("已发送文件: " + info.fileName());
    } else {
        QMessageBox::warning(this, "发送失败", "文件发送失败");
    }
}

void MainWindow::onSendImage() {
    QString filePath = QFileDialog::getOpenFileName(this, "选择图片", QString(),
        "图片文件 (*.png *.jpg *.jpeg *.bmp *.gif);;所有文件 (*.*)");
    if (filePath.isEmpty()) return;

    QFileInfo info(filePath);
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        QPixmap pixmap(filePath);
        QString line = QString("[%1] <%2> [图片] %3").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), m_currentUserName, info.fileName());
        saveHistory(m_privateChatTarget, line);
        QStandardItem* item = new QStandardItem(line);
        item->setEditable(false);
        item->setForeground(QColor(20, 92, 160));
        item->setBackground(QColor(218, 241, 255));
        m_chatModel->appendRow(item);
        if (!pixmap.isNull()) {
            QStandardItem* previewItem = new QStandardItem(info.fileName());
            previewItem->setData(pixmap.scaled(180, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::DecorationRole);
            previewItem->setEditable(false);
            previewItem->setBackground(QColor(246, 250, 253));
            m_chatModel->appendRow(previewItem);
        }
        ui->chatListView->scrollToBottom();
        return;
    }

    bool ok = m_client->sendImage(filePath, m_privateChatTarget);
    if (ok) {
        appendSystemMessage("已发送图片: " + info.fileName());
    } else {
        QMessageBox::warning(this, "发送失败", "图片发送失败");
    }
}

void MainWindow::onNewMessage(const Message& msg) {
    QString displayName = msg.senderName;
    if (msg.type == MessageType::System) {
        appendSystemMessage(msg.content);
        return;
    }

    QString timeStr = msg.timestamp.toString("hh:mm:ss");
    QString line;

    if (msg.type == MessageType::Image) {
        line = QString("[%1] <%2> [图片] %3").arg(timeStr, displayName, msg.fileName);
    } else if (msg.type == MessageType::File) {
        line = QString("[%1] <%2> %3").arg(timeStr, displayName, msg.content);
    } else if (msg.isPrivate()) {
        line = QString("[%1] <%2> [私聊] %3").arg(timeStr, displayName, msg.content);
    } else {
        line = QString("[%1] <%2> %3").arg(timeStr, displayName, msg.content);
    }

    if (msg.isPrivate()) {
        QString peerId = msg.senderId == m_currentUserId ? msg.receiverId : msg.senderId;
        if (!m_privateChatTarget.isEmpty() && peerId != m_privateChatTarget) {
            saveHistory(peerId, line);
            if (!isActiveWindow()) {
                ++m_unreadCount;
                updateUnreadState();
            }
            return;
        }
    }

    QStandardItem* item = new QStandardItem(line);
    item->setEditable(false);
    if (msg.isPrivate()) {
        item->setForeground(Qt::darkMagenta);
        item->setBackground(QColor(252, 240, 255));
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    } else if (msg.senderName == m_currentUserName) {
        item->setForeground(QColor(20, 92, 160));
        item->setBackground(QColor(218, 241, 255));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    } else {
        item->setForeground(QColor(38, 50, 56));
        item->setBackground(QColor(246, 250, 253));
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    m_chatModel->appendRow(item);
    saveHistory(msg.isPrivate() ? (msg.senderId == m_currentUserId ? msg.receiverId : msg.senderId) : "group", line);

    if (msg.type == MessageType::Image && !msg.fileData.isEmpty()) {
        QPixmap pixmap;
        if (pixmap.loadFromData(msg.fileData)) {
            QStandardItem* previewItem = new QStandardItem;
            previewItem->setData(pixmap.scaled(180, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::DecorationRole);
            previewItem->setText(msg.fileName);
            previewItem->setEditable(false);
            previewItem->setBackground(QColor(246, 250, 253));
            m_chatModel->appendRow(previewItem);
        }

        QString imageDirPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/QtNetworkChat/Images";
        QDir().mkpath(imageDirPath);
        QString savePath = imageDirPath + "/" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_") + msg.fileName;
        QFile f(savePath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(msg.fileData);
            f.close();
            QStandardItem* savedItem = new QStandardItem(QString("图片已自动保存: %1").arg(savePath));
            savedItem->setForeground(Qt::darkGreen);
            m_chatModel->appendRow(savedItem);
        }
    } else if (msg.type == MessageType::File && !msg.fileData.isEmpty()) {
        QString fileDirPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/QtNetworkChat/Files";
        QDir().mkpath(fileDirPath);
        QString savePath = fileDirPath + "/" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_") + msg.fileName;
        QFile f(savePath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(msg.fileData);
            f.close();
            QStandardItem* item2 = new QStandardItem(QString("文件已自动保存: %1").arg(savePath));
            item2->setForeground(Qt::darkGreen);
            m_chatModel->appendRow(item2);
        }
    }

    int rowCount = m_chatModel->rowCount();
    if (rowCount > MAX_HISTORY_LINES) {
        m_chatModel->removeRows(0, rowCount - MAX_HISTORY_LINES);
    }

    if (!isActiveWindow()) {
        ++m_unreadCount;
        updateUnreadState();
        if (m_trayIcon->isVisible()) {
            m_trayIcon->showMessage("QtNetworkChat", QString("%1: %2").arg(displayName, msg.content), QSystemTrayIcon::Information, 3000);
        }
    }

    ui->chatListView->scrollToBottom();
}

void MainWindow::onUserJoined(const QString& userId, const QString& userName) {
    Q_UNUSED(userId)
    appendSystemMessage(userName + " 加入了聊天室");
}

void MainWindow::onUserLeft(const QString& userId, const QString& userName) {
    Q_UNUSED(userId)
    appendSystemMessage(userName + " 离开了聊天室");
}

void MainWindow::onUserListUpdated(const QVector<ChatUser>& users) {
    m_knownUsers.clear();
    for (const ChatUser& user : users) {
        m_knownUsers[user.id] = user;
    }
    refreshFriendList();
    if (!m_privateChatTarget.isEmpty()) {
        ui->chatHintLabel->setText(QString("QQ: %1 · %2 · 点击菜单“返回群聊”回到公共聊天室")
            .arg(m_privateChatTarget, isContactOnline(m_privateChatTarget) ? "在线" : "离线"));
    }
    ui->statusbar->showMessage(QString("在线: %1 人 | 好友: %2 人 | 当前账号: %3")
        .arg(users.size())
        .arg(m_friendIds.size())
        .arg(m_currentUserId));
    refreshGroupMemberPanel();
}

void MainWindow::onPrivateChat(const QModelIndex& index) {
    if (!index.isValid()) return;
    QString targetId = index.data(Qt::UserRole + 1).toString();
    if (targetId.isEmpty()) return;
    if (m_localGroupIds.contains(targetId)) {
        switchToLocalGroup(targetId, m_localGroupNames.value(targetId, "群聊"));
        return;
    }
    QString userName = contactDisplayName(targetId);
    m_privateChatTarget = targetId;
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    loadHistory(targetId);
    QString onlineText = isContactOnline(targetId) ? "在线" : "离线";
    setWindowTitle(QString("QtNetworkChat - 私聊: %1").arg(userName));
    ui->chatTitleLabel->setText(QString("与 %1 私聊中").arg(userName));
    ui->chatHintLabel->setText(QString("QQ: %1 · %2 · 点击菜单“返回群聊”回到公共聊天室").arg(targetId, onlineText));
}

void MainWindow::onClientDisconnected() {
    appendSystemMessage("已断开服务器连接");
}

void MainWindow::onClientError(const QString& error) {
    appendSystemMessage("连接错误: " + error);
}

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        show();
        raise();
        activateWindow();
        clearUnreadState();
    }
}

void MainWindow::onCopyAccount() {
    QApplication::clipboard()->setText(m_currentUserId);
    ui->statusbar->showMessage("QQ 账号已复制: " + m_currentUserId, 3000);
}

void MainWindow::onLogout() {
    if (QMessageBox::question(this, "退出登录", "确定退出当前账号并返回登录界面吗？") != QMessageBox::Yes) {
        return;
    }
    m_isQuitting = true;
    if (m_client) {
        m_client->disconnectFromServer();
    }
    emit logoutRequested();
}

void MainWindow::onClearHistory() {
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    QString peerId = m_privateChatTarget.isEmpty() ? "group" : m_privateChatTarget;
    QFile::remove(getHistoryFilePath(peerId));
    appendSystemMessage("聊天记录已清空");
}

void MainWindow::onAddFriend() {
    bool ok = false;
    QString account = QInputDialog::getText(this, "加好友", "请输入对方 QQ 账号:", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || account.isEmpty()) return;
    searchAndAddAccount(account, this);
}

void MainWindow::searchAndAddAccount(const QString& account, QWidget* warningParent) {
    Q_UNUSED(warningParent)
    if (account.isEmpty()) return;
    if (account == m_currentUserId) {
        ui->statusbar->showMessage("不能添加自己为好友", 2500);
        return;
    }
    if (m_friendIds.contains(account)) {
        ui->statusbar->showMessage("该账号已经是你的好友: " + account, 2500);
        return;
    }
    if (!m_client->searchFriendByAccount(account)) {
        ui->statusbar->showMessage("当前未连接，无法搜索账号", 2500);
    } else {
        ui->statusbar->showMessage("正在搜索 QQ 账号: " + account, 2500);
    }
}

void MainWindow::onShowGlobalSearch() {
    QDialog dialog(this);
    dialog.setObjectName("globalSearchDialog");
    dialog.setWindowTitle("综合搜索");
    dialog.setFixedSize(680, 620);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QFrame* header = new QFrame(&dialog);
    header->setObjectName("searchHeader");
    QVBoxLayout* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(18, 14, 18, 8);
    headerLayout->setSpacing(10);

    QHBoxLayout* searchLayout = new QHBoxLayout;
    QLineEdit* searchEdit = new QLineEdit(header);
    searchEdit->setObjectName("globalSearchInput");
    searchEdit->setPlaceholderText("输入 QQ 号 / 昵称搜索");
    searchEdit->setClearButtonEnabled(true);
    QPushButton* searchBtn = new QPushButton("搜索", header);
    searchBtn->setObjectName("globalSearchPrimaryBtn");
    searchLayout->addWidget(searchEdit, 1);
    searchLayout->addWidget(searchBtn);
    headerLayout->addLayout(searchLayout);

    QHBoxLayout* tabLayout = new QHBoxLayout;
    const QStringList tabs = {"全部", "用户", "群聊", "小程序", "机器人"};
    for (const QString& tab : tabs) {
        QLabel* label = new QLabel(tab, header);
        label->setObjectName(tab == "全部" ? "activeSearchTab" : "searchTab");
        label->setAlignment(Qt::AlignCenter);
        tabLayout->addWidget(label);
    }
    tabLayout->addStretch();
    headerLayout->addLayout(tabLayout);
    layout->addWidget(header);

    QListWidget* resultList = new QListWidget(&dialog);
    resultList->setObjectName("globalResultList");
    layout->addWidget(resultList, 1);

    auto fillResults = [this, resultList](const QString& filter = QString()) {
        resultList->clear();
        for (const QString& id : m_friendIds) {
            QString name = m_friendNames.value(id, id);
            if (!filter.isEmpty()
                && !id.contains(filter, Qt::CaseInsensitive)
                && !name.contains(filter, Qt::CaseInsensitive)) continue;
            QListWidgetItem* item = new QListWidgetItem(QString("好友  QQ:%1\n%2 · %3").arg(id, name, isContactOnline(id) ? "在线" : "离线"));
            item->setData(Qt::UserRole, id);
            item->setSizeHint(QSize(0, 66));
            resultList->addItem(item);
        }
        for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
            const ChatUser& user = it.value();
            if (user.id == m_currentUserId || m_friendIds.contains(user.id)) continue;
            if (!filter.isEmpty()
                && !user.id.contains(filter, Qt::CaseInsensitive)
                && !user.name.contains(filter, Qt::CaseInsensitive)) continue;
            QListWidgetItem* item = new QListWidgetItem(QString("用户  QQ:%1\n%2 · 在线 · 双击添加").arg(user.id, user.name));
            item->setData(Qt::UserRole, user.id);
            item->setSizeHint(QSize(0, 66));
            resultList->addItem(item);
        }
        for (const QString& groupId : m_localGroupIds) {
            QString groupName = m_localGroupNames.value(groupId, "群聊");
            if (!filter.isEmpty()
                && !groupId.contains(filter, Qt::CaseInsensitive)
                && !groupName.contains(filter, Qt::CaseInsensitive)) continue;
            QListWidgetItem* item = new QListWidgetItem(QString("群聊  QQ:%1\n%2 · 本地群聊 · 双击进入").arg(groupId.mid(QString("local_group_").size()), groupName));
            item->setData(Qt::UserRole, groupId);
            item->setSizeHint(QSize(0, 66));
            resultList->addItem(item);
        }
        if (!filter.isEmpty()) {
            QListWidgetItem* searchItem = new QListWidgetItem(QString("搜索 QQ 账号：%1\n点击右侧搜索按钮可从服务器查找并自动添加").arg(filter));
            searchItem->setFlags(Qt::NoItemFlags);
            searchItem->setForeground(QColor(92, 110, 128));
            searchItem->setSizeHint(QSize(0, 58));
            resultList->addItem(searchItem);
        }
        if (resultList->count() == 0) {
            QListWidgetItem* emptyItem = new QListWidgetItem("输入 QQ 号搜索用户并添加好友");
            emptyItem->setFlags(Qt::NoItemFlags);
            emptyItem->setForeground(QColor(135, 150, 165));
            resultList->addItem(emptyItem);
        }
    };
    fillResults();

    dialog.setStyleSheet(R"(
        QDialog#globalSearchDialog {
            background: #F4F4F4;
            font-family: "Microsoft YaHei", "Segoe UI";
        }
        QFrame#searchHeader {
            background: white;
            border-bottom: 1px solid #E8E8E8;
        }
        QLineEdit#globalSearchInput {
            min-height: 36px;
            background: #F1F2F4;
            border: none;
            border-radius: 8px;
            padding: 4px 12px;
            color: #263238;
        }
        QPushButton#globalSearchPrimaryBtn {
            min-width: 76px;
            min-height: 36px;
            background: #1296F7;
            color: white;
            border: none;
            border-radius: 10px;
            font-weight: 700;
        }
        QLabel#activeSearchTab {
            color: #1296F7;
            border-bottom: 2px solid #1296F7;
            font-weight: 700;
            padding: 8px 12px;
        }
        QLabel#searchTab {
            color: #1F2D3D;
            padding: 8px 12px;
        }
        QListWidget#globalResultList {
            background: #F4F4F4;
            border: none;
            outline: none;
            padding: 12px 18px;
        }
        QListWidget#globalResultList::item {
            background: white;
            border-radius: 12px;
            margin: 6px 0;
            padding: 10px 14px;
            color: #263238;
        }
        QListWidget#globalResultList::item:selected, QListWidget#globalResultList::item:hover {
            background: #EAF7FF;
        }
    )");

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [fillResults](const QString& text) {
        fillResults(text.trimmed());
    });
    auto runServerSearch = [this, searchEdit, &dialog]() {
        QString account = searchEdit->text().trimmed();
        if (account.isEmpty()) {
            ui->statusbar->showMessage("请输入 QQ 号", 2500);
            return;
        }
        searchAndAddAccount(account, &dialog);
        dialog.accept();
    };
    connect(searchBtn, &QPushButton::clicked, &dialog, runServerSearch);
    connect(searchEdit, &QLineEdit::returnPressed, &dialog, runServerSearch);
    connect(resultList, &QListWidget::itemDoubleClicked, &dialog, [this, &dialog](QListWidgetItem* item) {
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        if (m_localGroupIds.contains(id)) {
            dialog.accept();
            switchToLocalGroup(id, m_localGroupNames.value(id, "群聊"));
            return;
        }
        if (!m_friendIds.contains(id)) {
            m_friendNames[id] = contactDisplayName(id);
            m_client->sendFriendRequest(id);
        }
        dialog.accept();
        m_privateChatTarget = id;
        m_chatModel->clear();
        m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
        loadHistory(id);
        ui->chatTitleLabel->setText(QString("与 %1 私聊中").arg(contactDisplayName(id)));
        ui->chatHintLabel->setText(QString("QQ: %1 · %2 · 点击菜单“返回群聊”回到公共聊天室").arg(id, isContactOnline(id) ? "在线" : "离线"));
    });

    searchEdit->setFocus();
    dialog.exec();
}

void MainWindow::onShowCreateMenu() {
    QMenu menu(this);
    QAction* createGroupAction = menu.addAction("创建群聊");
    QAction* addFriendAction = menu.addAction("加好友/群");
    QAction* editAnnouncementAction = menu.addAction("编辑群公告");
    QAction* sendImageAction = menu.addAction("发送图片");
    QAction* sendFileAction = menu.addAction("闪传文件");
    QAction* selected = menu.exec(ui->createMenuBtn->mapToGlobal(QPoint(0, ui->createMenuBtn->height())));
    if (selected == createGroupAction) {
        bool ok = false;
        QString groupName = QInputDialog::getText(this, "创建群聊", "群聊名称:", QLineEdit::Normal, "我的群聊", &ok).trimmed();
        if (!ok) return;
        if (groupName.isEmpty()) groupName = "我的群聊";
        QString groupId = "local_group_" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
        m_localGroupIds << groupId;
        m_localGroupNames[groupId] = groupName;
        m_localGroupAnnouncements[groupId] = QString("%1 已创建，可继续邀请好友并发送消息。").arg(groupName);
        m_localGroupMembers[groupId] = QStringList{m_currentUserId};
        saveLocalGroups();
        refreshFriendList();
        switchToLocalGroup(groupId, groupName);
        appendSystemMessage("已创建群聊: " + groupName);
    } else if (selected == addFriendAction) {
        onShowGlobalSearch();
    } else if (selected == editAnnouncementAction) {
        onEditGroupAnnouncement();
    } else if (selected == sendImageAction) {
        onSendImage();
    } else if (selected == sendFileAction) {
        onSendFile();
    }
}

void MainWindow::switchToLocalGroup(const QString& groupId, const QString& groupName) {
    m_privateChatTarget = groupId;
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    loadHistory(groupId);
    setWindowTitle(QString("QtNetworkChat - 群聊: %1").arg(groupName));
    ui->chatTitleLabel->setText(groupName);
    ui->chatHintLabel->setText(QString("本地群聊 · 群号 %1 · 当前成员会自动显示在右侧").arg(groupId.mid(QString("local_group_").size())));
    ui->announcementBodyLabel->setText(m_localGroupAnnouncements.value(groupId, QString("%1 已创建，可继续邀请好友并发送消息。").arg(groupName)));
    refreshGroupMemberPanel();
}

void MainWindow::onEditGroupAnnouncement() {
    bool ok = false;
    QString text = QInputDialog::getMultiLineText(
        this,
        "编辑群公告",
        "群公告内容:",
        ui->announcementBodyLabel->text(),
        &ok).trimmed();
    if (!ok) return;
    if (text.isEmpty()) {
        text = "欢迎来到公共聊天室，支持 QQ 号搜索、好友、私聊和文件发送。";
    }
    ui->announcementBodyLabel->setText(text);
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        m_localGroupAnnouncements[m_privateChatTarget] = text;
        saveLocalGroups();
        saveHistory(m_privateChatTarget, QString("[%1] [系统] 群公告已更新: %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), text));
    }
    appendSystemMessage("群公告已更新");
}

void MainWindow::onInsertEmoji() {
    QMenu menu(this);
    const QStringList emojis = {"😀", "😂", "😊", "😍", "😎", "😭", "👍", "🎉", "❤️", "🔥"};
    for (const QString& emoji : emojis) {
        QAction* action = menu.addAction(emoji);
        connect(action, &QAction::triggered, this, [this, emoji]() {
            ui->messageEdit->insertPlainText(emoji);
            ui->messageEdit->setFocus();
        });
    }
    menu.exec(ui->emojiBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
}

void MainWindow::onInsertMention() {
    QMenu menu(this);
    QAction* allAction = menu.addAction("@全体成员");
    connect(allAction, &QAction::triggered, this, [this]() {
        ui->messageEdit->insertPlainText("@全体成员 ");
        ui->messageEdit->setFocus();
    });
    QStringList mentionIds;
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        mentionIds = m_localGroupMembers.value(m_privateChatTarget);
    } else {
        for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
            mentionIds << it.key();
        }
    }
    mentionIds.removeAll(m_currentUserId);
    if (!mentionIds.isEmpty()) menu.addSeparator();
    for (const QString& memberId : mentionIds) {
        QString name = memberId == m_currentUserId ? m_currentUserName : contactDisplayName(memberId);
        QAction* action = menu.addAction(QString("@%1 (QQ:%2)").arg(name, memberId));
        connect(action, &QAction::triggered, this, [this, name]() {
            ui->messageEdit->insertPlainText(QString("@%1 ").arg(name));
            ui->messageEdit->setFocus();
        });
    }
    menu.exec(ui->mentionBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
}

void MainWindow::onShowQuickAddFriend() {
    QDialog dialog(this);
    dialog.setObjectName("quickAddDialog");
    dialog.setWindowTitle("加好友");
    dialog.setFixedSize(360, 230);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(26, 22, 26, 22);
    layout->setSpacing(14);

    QLabel* titleLabel = new QLabel("搜索 QQ 账号添加好友", &dialog);
    titleLabel->setObjectName("quickAddTitle");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    QLineEdit* accountEdit = new QLineEdit(&dialog);
    accountEdit->setObjectName("quickAddInput");
    accountEdit->setPlaceholderText("输入对方 QQ 号");
    accountEdit->setClearButtonEnabled(true);
    layout->addWidget(accountEdit);

    QLabel* hintLabel = new QLabel("可添加在线或离线账号，好友列表会保存在本地。", &dialog);
    hintLabel->setObjectName("quickAddHint");
    hintLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(hintLabel);

    QHBoxLayout* buttonLayout = new QHBoxLayout;
    QPushButton* cancelBtn = new QPushButton("取消", &dialog);
    cancelBtn->setObjectName("quickCancelBtn");
    QPushButton* searchBtn = new QPushButton("搜索并添加", &dialog);
    searchBtn->setObjectName("quickSearchBtn");
    searchBtn->setDefault(true);
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(searchBtn);
    layout->addLayout(buttonLayout);

    dialog.setStyleSheet(R"(
        QDialog#quickAddDialog {
            background: white;
            font-family: "Microsoft YaHei", "Segoe UI";
        }
        QLabel#quickAddTitle {
            color: #1F2D3D;
            font-size: 18px;
            font-weight: 800;
        }
        QLabel#quickAddHint {
            color: #8A99A8;
            font-size: 12px;
        }
        QLineEdit#quickAddInput {
            min-height: 42px;
            border: 1px solid #DDE7F0;
            border-radius: 18px;
            padding: 4px 14px;
            background: #F8FBFE;
            color: #263238;
            font-size: 15px;
        }
        QLineEdit#quickAddInput:focus {
            border: 1px solid #12B7F5;
            background: white;
        }
        QPushButton {
            min-height: 36px;
            border-radius: 18px;
            padding: 6px 16px;
            font-weight: 700;
        }
        QPushButton#quickSearchBtn {
            background: #12B7F5;
            color: white;
            border: none;
        }
        QPushButton#quickSearchBtn:hover {
            background: #0AA4E5;
        }
        QPushButton#quickCancelBtn {
            background: #EFF5FA;
            color: #3A4A5A;
            border: 1px solid #D4E1EC;
        }
    )");

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    auto runQuickAdd = [this, accountEdit, hintLabel, &dialog]() {
        QString account = accountEdit->text().trimmed();
        if (account.isEmpty()) {
            hintLabel->setText("请输入对方 QQ 账号");
            ui->statusbar->showMessage("请输入对方 QQ 账号", 2500);
            return;
        }
        searchAndAddAccount(account, &dialog);
        dialog.accept();
    };
    connect(searchBtn, &QPushButton::clicked, &dialog, runQuickAdd);
    connect(accountEdit, &QLineEdit::returnPressed, &dialog, runQuickAdd);

    accountEdit->setFocus();
    dialog.exec();
}

void MainWindow::onShowFriendManager() {
    QDialog dialog(this);
    dialog.setObjectName("friendManagerDialog");
    dialog.setWindowTitle("好友管理器");
    dialog.setFixedSize(520, 560);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QFrame* header = new QFrame(&dialog);
    header->setObjectName("managerHeader");
    header->setFixedHeight(118);
    QVBoxLayout* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(26, 18, 26, 16);
    QLabel* titleLabel = new QLabel("好友管理器", header);
    titleLabel->setObjectName("managerTitle");
    QLabel* subTitleLabel = new QLabel(QString("当前 QQ：%1 · 好友 %2 人").arg(m_currentUserId).arg(m_friendIds.size()), header);
    subTitleLabel->setObjectName("managerSubTitle");
    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(subTitleLabel);
    layout->addWidget(header);

    QFrame* body = new QFrame(&dialog);
    body->setObjectName("managerBody");
    QVBoxLayout* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(24, 22, 24, 22);
    bodyLayout->setSpacing(12);

    QLineEdit* searchEdit = new QLineEdit(body);
    searchEdit->setObjectName("managerSearch");
    searchEdit->setPlaceholderText("搜索好友 QQ 号 / 昵称");
    searchEdit->setClearButtonEnabled(true);
    bodyLayout->addWidget(searchEdit);

    QListWidget* friendList = new QListWidget(body);
    friendList->setObjectName("managerList");
    bodyLayout->addWidget(friendList, 1);

    auto fillList = [this, friendList](const QString& filter = QString()) {
        friendList->clear();
        for (const QString& id : m_friendIds) {
            QString name = m_friendNames.value(id, id);
            if (!filter.isEmpty()
                && !id.contains(filter, Qt::CaseInsensitive)
                && !name.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QString state = isContactOnline(id) ? "在线" : "离线";
            QListWidgetItem* item = new QListWidgetItem(QString("QQ:%1\n%2 · %3").arg(id, name, state));
            item->setData(Qt::UserRole, id);
            item->setSizeHint(QSize(0, 58));
            friendList->addItem(item);
        }
        if (friendList->count() == 0) {
            QListWidgetItem* emptyItem = new QListWidgetItem(filter.isEmpty() ? "暂无好友，点击下方加好友" : "没有匹配的好友");
            emptyItem->setFlags(Qt::NoItemFlags);
            emptyItem->setForeground(QColor(135, 150, 165));
            friendList->addItem(emptyItem);
        }
    };
    fillList();

    QHBoxLayout* buttonLayout = new QHBoxLayout;
    QPushButton* addBtn = new QPushButton("加好友", body);
    addBtn->setObjectName("managerPrimaryBtn");
    QPushButton* chatBtn = new QPushButton("发消息", body);
    chatBtn->setObjectName("managerSecondaryBtn");
    QPushButton* deleteBtn = new QPushButton("删除好友", body);
    deleteBtn->setObjectName("managerDangerBtn");
    QPushButton* closeBtn = new QPushButton("关闭", body);
    closeBtn->setObjectName("managerSecondaryBtn");
    buttonLayout->addWidget(addBtn);
    buttonLayout->addWidget(chatBtn);
    buttonLayout->addWidget(deleteBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeBtn);
    bodyLayout->addLayout(buttonLayout);
    layout->addWidget(body);

    dialog.setStyleSheet(R"(
        QDialog#friendManagerDialog {
            background: #EEF3F8;
            font-family: "Microsoft YaHei", "Segoe UI";
        }
        QFrame#managerHeader {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #18C1F7, stop:1 #0877C9);
        }
        QLabel#managerTitle {
            color: white;
            font-size: 24px;
            font-weight: 900;
        }
        QLabel#managerSubTitle {
            color: rgba(255, 255, 255, 220);
            font-size: 13px;
        }
        QFrame#managerBody {
            background: #F7FAFD;
        }
        QLineEdit#managerSearch {
            min-height: 38px;
            border: 1px solid #DDE7F0;
            border-radius: 18px;
            padding: 4px 14px;
            background: white;
        }
        QListWidget#managerList {
            background: white;
            border: 1px solid #DCE8F2;
            border-radius: 18px;
            padding: 8px;
            outline: none;
        }
        QListWidget#managerList::item {
            border-radius: 12px;
            padding: 8px 12px;
            color: #263238;
        }
        QListWidget#managerList::item:selected, QListWidget#managerList::item:hover {
            background: #EAF7FF;
        }
        QPushButton {
            min-height: 34px;
            border-radius: 17px;
            padding: 6px 14px;
            font-weight: 700;
        }
        QPushButton#managerPrimaryBtn {
            background: #12B7F5;
            color: white;
            border: none;
        }
        QPushButton#managerSecondaryBtn {
            background: white;
            color: #3A4A5A;
            border: 1px solid #D4E1EC;
        }
        QPushButton#managerDangerBtn {
            background: white;
            color: #D35454;
            border: 1px solid #F1CCCC;
        }
    )");

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [fillList](const QString& text) {
        fillList(text.trimmed());
    });
    connect(addBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onShowQuickAddFriend();
    });
    connect(chatBtn, &QPushButton::clicked, &dialog, [this, &dialog, friendList]() {
        QListWidgetItem* selected = friendList->currentItem();
        if (!selected) return;
        QString id = selected->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        dialog.accept();
        m_privateChatTarget = id;
        m_chatModel->clear();
        m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
        loadHistory(id);
        ui->chatTitleLabel->setText(QString("与 %1 私聊中").arg(contactDisplayName(id)));
        ui->chatHintLabel->setText(QString("QQ: %1 · %2 · 点击菜单“返回群聊”回到公共聊天室").arg(id, isContactOnline(id) ? "在线" : "离线"));
    });
    connect(deleteBtn, &QPushButton::clicked, &dialog, [this, friendList, fillList, searchEdit]() {
        QListWidgetItem* selected = friendList->currentItem();
        if (!selected) return;
        QString id = selected->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        if (QMessageBox::question(this, "删除好友", QString("确定删除 QQ:%1 吗？").arg(id)) != QMessageBox::Yes) return;
        m_friendIds.removeAll(id);
        m_friendNames.remove(id);
        saveFriends();
        refreshFriendList();
        fillList(searchEdit->text().trimmed());
        appendSystemMessage("已删除好友 QQ: " + id);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::onUploadAvatar() {
    QString filePath = QFileDialog::getOpenFileName(this, "选择头像", QString(), "图片 (*.png *.jpg *.jpeg *.bmp)");
    if (filePath.isEmpty()) return;

    QPixmap pixmap(filePath);
    if (pixmap.isNull()) {
        QMessageBox::warning(this, "头像上传失败", "无法读取该图片");
        return;
    }

    QPixmap scaled = pixmap.scaled(ui->avatarLabel->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    ui->avatarLabel->setPixmap(scaled);
    scaled.save(getAvatarFilePath(), "PNG");
    appendSystemMessage("头像已更新");
}

void MainWindow::onBackToGroupChat() {
    m_privateChatTarget.clear();
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    loadHistory("group");
    setWindowTitle("QtNetworkChat - " + m_currentUserName);
    ui->chatTitleLabel->setText("公共聊天室");
    ui->chatHintLabel->setText(QString("账号 %1 · 双击左侧成员可私聊").arg(m_currentUserId));
}

void MainWindow::onFriendRequestReceived(const QString& senderId, const QString& senderName) {
    m_friendNames[senderId] = senderName;
    m_pendingFriendRequests.removeAll(senderId);
    if (!m_friendIds.contains(senderId)) {
        m_friendIds << senderId;
        saveFriends();
        refreshFriendList();
    }
    m_client->sendFriendResponse(senderId, true);
    ui->friendNoticeBtn->setText("好友通知");
    appendSystemMessage(QString("已自动同意好友申请 QQ:%1").arg(senderId));
}

void MainWindow::onFriendSearchResult(const QString& account, const QString& userId, const QString& userName, bool found, bool online) {
    if (!found) {
        ui->statusbar->showMessage(QString("没有找到 QQ 账号：%1").arg(account), 3000);
        appendSystemMessage(QString("没有找到 QQ 账号: %1").arg(account));
        return;
    }
    if (userId == m_currentUserId) {
        ui->statusbar->showMessage("不能添加自己为好友", 2500);
        return;
    }
    if (m_friendIds.contains(userId)) {
        ui->statusbar->showMessage(QString("QQ 账号 %1 已经是你的好友").arg(userId), 2500);
        return;
    }

    m_friendNames[userId] = userName;
    if (!m_friendIds.contains(userId)) {
        m_friendIds << userId;
        saveFriends();
        refreshFriendList();
    }
    if (online) {
        m_client->sendFriendRequest(userId);
        appendSystemMessage("已自动发送好友申请 QQ: " + userId);
    } else {
        appendSystemMessage("已自动添加离线好友 QQ: " + userId);
    }
}

void MainWindow::onFriendRequestSent(const QString& receiverId, bool delivered) {
    QString userName = contactDisplayName(receiverId);
    if (!m_friendIds.contains(receiverId)) {
        m_friendIds << receiverId;
        saveFriends();
        refreshFriendList();
    }
    appendSystemMessage(delivered ? "好友申请已送达 QQ: " + receiverId : "对方当前离线，已添加到好友列表 QQ: " + receiverId);
}

void MainWindow::onFriendResponseReceived(const QString& senderId, const QString& senderName, bool accepted) {
    if (accepted) {
        if (!m_friendIds.contains(senderId)) {
            m_friendIds << senderId;
            m_friendNames[senderId] = senderName;
            QFile file(getFriendFilePath());
            if (file.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream out(&file);
                out << senderId << "|" << senderName << "\n";
            }
        }
        refreshFriendList();
        m_pendingFriendRequests.removeAll(senderId);
        ui->friendNoticeBtn->setText(m_pendingFriendRequests.isEmpty() ? "好友通知" : QString("好友通知 %1").arg(m_pendingFriendRequests.size()));
        appendSystemMessage(senderName + " 已同意你的好友申请");
    } else {
        m_friendIds.removeAll(senderId);
        m_friendNames.remove(senderId);
        saveFriends();
        refreshFriendList();
        m_pendingFriendRequests.removeAll(senderId);
        ui->friendNoticeBtn->setText(m_pendingFriendRequests.isEmpty() ? "好友通知" : QString("好友通知 %1").arg(m_pendingFriendRequests.size()));
        appendSystemMessage(senderName + " 已拒绝你的好友申请");
    }
}

void MainWindow::onUserContextMenu(const QPoint& pos) {
    QModelIndex index = ui->userListView->indexAt(pos);
    if (!index.isValid()) return;

    QString userId = index.data(Qt::UserRole + 1).toString();
    if (userId.isEmpty()) return;
    QString userName = index.data().toString();
    userName.remove(QRegularExpression("^[★☆○]\\s*"));
    userName.remove(QRegularExpression("^群聊\\s+QQ:[^\\n]+\\n\\s*"));
    userName.remove(QRegularExpression("\\s*\\[(在线|离线|本地)\\]$"));

    QMenu menu(this);
    if (m_localGroupIds.contains(userId)) {
        QAction* openGroupAction = menu.addAction("进入群聊");
        QAction* inviteFriendAction = menu.addAction("邀请好友");
        QAction* inviteAllAction = menu.addAction("邀请全部好友");
        QAction* renameGroupAction = menu.addAction("重命名群聊");
        QAction* deleteGroupAction = menu.addAction("删除群聊");
        QAction* selected = menu.exec(ui->userListView->viewport()->mapToGlobal(pos));
        if (selected == openGroupAction) {
            switchToLocalGroup(userId, m_localGroupNames.value(userId, "群聊"));
        } else if (selected == inviteFriendAction) {
            if (m_friendIds.isEmpty()) {
                appendSystemMessage("当前没有好友可邀请");
            } else {
                QStringList friendLabels;
                QMap<QString, QString> labelToId;
                for (const QString& friendId : m_friendIds) {
                    QString label = QString("%1 (QQ:%2)").arg(m_friendNames.value(friendId, friendId), friendId);
                    friendLabels << label;
                    labelToId[label] = friendId;
                }
                bool ok = false;
                QString selectedFriend = QInputDialog::getItem(this, "邀请好友", "选择好友:", friendLabels, 0, false, &ok);
                if (ok && !selectedFriend.isEmpty()) {
                    QString friendId = labelToId.value(selectedFriend);
                    QString friendName = m_friendNames.value(friendId, friendId);
                    if (!m_localGroupMembers[userId].contains(friendId)) {
                        m_localGroupMembers[userId] << friendId;
                        saveLocalGroups();
                    }
                    switchToLocalGroup(userId, m_localGroupNames.value(userId, "群聊"));
                    appendSystemMessage(QString("已邀请 %1 加入群聊").arg(friendName));
                    saveHistory(userId, QString("[%1] [系统] 已邀请 %2 加入群聊").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), friendName));
                }
            }
        } else if (selected == inviteAllAction) {
            int addedCount = 0;
            for (const QString& friendId : m_friendIds) {
                if (!m_localGroupMembers[userId].contains(friendId)) {
                    m_localGroupMembers[userId] << friendId;
                    ++addedCount;
                }
            }
            if (addedCount > 0) {
                saveLocalGroups();
            }
            switchToLocalGroup(userId, m_localGroupNames.value(userId, "群聊"));
            appendSystemMessage(QString("已自动邀请 %1 位好友加入群聊").arg(addedCount));
            saveHistory(userId, QString("[%1] [系统] 已自动邀请 %2 位好友加入群聊").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(addedCount));
        } else if (selected == renameGroupAction) {
            bool ok = false;
            QString newName = QInputDialog::getText(this, "重命名群聊", "群聊名称:", QLineEdit::Normal, m_localGroupNames.value(userId, "群聊"), &ok).trimmed();
            if (ok && !newName.isEmpty()) {
                m_localGroupNames[userId] = newName;
                saveLocalGroups();
                refreshFriendList();
                if (m_privateChatTarget == userId) switchToLocalGroup(userId, newName);
            }
        } else if (selected == deleteGroupAction) {
            m_localGroupIds.removeAll(userId);
            m_localGroupNames.remove(userId);
            m_localGroupAnnouncements.remove(userId);
            m_localGroupMembers.remove(userId);
            saveLocalGroups();
            refreshFriendList();
            if (m_privateChatTarget == userId) onBackToGroupChat();
            appendSystemMessage("已删除群聊: " + userName);
        }
        return;
    }

    QAction* chatAction = menu.addAction("发送消息");
    QAction* addAction = nullptr;
    QAction* removeAction = nullptr;
    if (m_friendIds.contains(userId)) {
        removeAction = menu.addAction("删除好友");
    } else {
        addAction = menu.addAction("加为好友");
    }

    QAction* selected = menu.exec(ui->userListView->viewport()->mapToGlobal(pos));
    if (selected == chatAction) {
        onPrivateChat(index);
    } else if (selected == addAction) {
        if (!m_friendIds.contains(userId)) {
            m_friendIds << userId;
            m_friendNames[userId] = userName;
            saveFriends();
            m_client->sendFriendRequest(userId);
            refreshFriendList();
            appendSystemMessage("已发送好友申请: " + userName);
        }
    } else if (selected == removeAction) {
        m_friendIds.removeAll(userId);
        m_friendNames.remove(userId);
        saveFriends();
        refreshFriendList();
        appendSystemMessage("已删除好友: " + userName);
    }
}

void MainWindow::onShowFriendNotifications() {
    QDialog dialog(this);
    dialog.setObjectName("noticeDialog");
    dialog.setWindowTitle("好友通知");
    dialog.setFixedSize(760, 620);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(18);

    QHBoxLayout* titleLayout = new QHBoxLayout;
    QLabel* titleLabel = new QLabel("好友通知", &dialog);
    titleLabel->setObjectName("noticeTitle");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    QPushButton* clearBtn = new QPushButton("清空", &dialog);
    clearBtn->setObjectName("noticeGhostBtn");
    titleLayout->addWidget(clearBtn);
    layout->addLayout(titleLayout);

    QListWidget* noticeList = new QListWidget(&dialog);
    noticeList->setObjectName("noticeList");
    layout->addWidget(noticeList, 1);

    auto fillList = [this, noticeList]() {
        noticeList->clear();
        if (m_pendingFriendRequests.isEmpty()) {
            QListWidgetItem* emptyItem = new QListWidgetItem("暂无新的好友申请");
            emptyItem->setFlags(Qt::NoItemFlags);
            emptyItem->setForeground(QColor(135, 150, 165));
            emptyItem->setSizeHint(QSize(0, 68));
            noticeList->addItem(emptyItem);
            return;
        }
        for (const QString& id : m_pendingFriendRequests) {
            QString name = m_friendNames.value(id, id);
            QListWidgetItem* item = new QListWidgetItem(QString("%1  请求加为好友\n留言：请求添加对方为好友\n来源：QQ号-%2").arg(name, id));
            item->setData(Qt::UserRole, id);
            item->setSizeHint(QSize(0, 92));
            noticeList->addItem(item);
        }
    };
    fillList();

    QHBoxLayout* buttonLayout = new QHBoxLayout;
    QPushButton* acceptBtn = new QPushButton("同意", &dialog);
    acceptBtn->setObjectName("noticePrimaryBtn");
    QPushButton* rejectBtn = new QPushButton("拒绝", &dialog);
    rejectBtn->setObjectName("noticeDangerBtn");
    QPushButton* closeBtn = new QPushButton("关闭", &dialog);
    closeBtn->setObjectName("noticeGhostBtn");
    buttonLayout->addWidget(acceptBtn);
    buttonLayout->addWidget(rejectBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeBtn);
    layout->addLayout(buttonLayout);

    dialog.setStyleSheet(R"(
        QDialog#noticeDialog {
            background: #F4F4F4;
            font-family: "Microsoft YaHei", "Segoe UI";
        }
        QLabel#noticeTitle {
            color: #111111;
            font-size: 20px;
            font-weight: 900;
        }
        QListWidget#noticeList {
            background: #F4F4F4;
            border: none;
            outline: none;
        }
        QListWidget#noticeList::item {
            background: white;
            border-radius: 10px;
            margin: 8px 80px;
            padding: 14px 18px;
            color: #263238;
        }
        QListWidget#noticeList::item:selected, QListWidget#noticeList::item:hover {
            background: #EAF7FF;
        }
        QPushButton {
            min-height: 34px;
            border-radius: 17px;
            padding: 6px 18px;
            font-weight: 700;
        }
        QPushButton#noticePrimaryBtn {
            background: #1296F7;
            color: white;
            border: none;
        }
        QPushButton#noticeDangerBtn {
            background: white;
            color: #D35454;
            border: 1px solid #F1CCCC;
        }
        QPushButton#noticeGhostBtn {
            background: white;
            color: #3A4A5A;
            border: 1px solid #D4E1EC;
        }
    )");

    auto updateBadge = [this]() {
        ui->friendNoticeBtn->setText(m_pendingFriendRequests.isEmpty() ? "好友通知" : QString("好友通知 %1").arg(m_pendingFriendRequests.size()));
    };
    connect(acceptBtn, &QPushButton::clicked, &dialog, [this, noticeList, fillList, updateBadge]() {
        QListWidgetItem* item = noticeList->currentItem();
        if (!item) return;
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        QString name = m_friendNames.value(id, id);
        m_client->sendFriendResponse(id, true);
        if (!m_friendIds.contains(id)) {
            m_friendIds << id;
        }
        m_friendNames[id] = name;
        m_pendingFriendRequests.removeAll(id);
        saveFriends();
        refreshFriendList();
        updateBadge();
        fillList();
        appendSystemMessage("已同意好友申请 QQ: " + id);
    });
    connect(rejectBtn, &QPushButton::clicked, &dialog, [this, noticeList, fillList, updateBadge]() {
        QListWidgetItem* item = noticeList->currentItem();
        if (!item) return;
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        m_client->sendFriendResponse(id, false);
        m_pendingFriendRequests.removeAll(id);
        updateBadge();
        fillList();
        appendSystemMessage("已拒绝好友申请 QQ: " + id);
    });
    connect(clearBtn, &QPushButton::clicked, &dialog, [this, fillList, updateBadge]() {
        m_pendingFriendRequests.clear();
        updateBadge();
        fillList();
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::onShowGroupNotifications() {
    QDialog dialog(this);
    dialog.setObjectName("noticeDialog");
    dialog.setWindowTitle("群通知");
    dialog.setFixedSize(760, 520);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(18);

    QLabel* titleLabel = new QLabel("群通知", &dialog);
    titleLabel->setObjectName("noticeTitle");
    layout->addWidget(titleLabel);

    QListWidget* noticeList = new QListWidget(&dialog);
    noticeList->setObjectName("noticeList");
    QListWidgetItem* item = new QListWidgetItem(QString("%1  当前公共聊天室\n你已加入默认群聊，可直接发送消息和文件。\n群成员列表会根据在线用户自动刷新。").arg(m_currentUserName));
    item->setSizeHint(QSize(0, 92));
    noticeList->addItem(item);
    layout->addWidget(noticeList, 1);

    QPushButton* closeBtn = new QPushButton("关闭", &dialog);
    closeBtn->setObjectName("noticeGhostBtn");
    layout->addWidget(closeBtn, 0, Qt::AlignRight);
    dialog.setStyleSheet(R"(
        QDialog#noticeDialog {
            background: #F4F4F4;
            font-family: "Microsoft YaHei", "Segoe UI";
        }
        QLabel#noticeTitle {
            color: #111111;
            font-size: 20px;
            font-weight: 900;
        }
        QListWidget#noticeList {
            background: #F4F4F4;
            border: none;
            outline: none;
        }
        QListWidget#noticeList::item {
            background: white;
            border-radius: 10px;
            margin: 8px 80px;
            padding: 14px 18px;
            color: #263238;
        }
        QPushButton#noticeGhostBtn {
            min-height: 34px;
            border-radius: 17px;
            padding: 6px 18px;
            font-weight: 700;
            background: white;
            color: #3A4A5A;
            border: 1px solid #D4E1EC;
        }
    )");
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::appendMessage(const Message& msg) {
    onNewMessage(msg);
}

void MainWindow::appendSystemMessage(const QString& text) {
    QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString line = QString("[%1] [系统] %2").arg(timeStr, text);
    QStandardItem* item = new QStandardItem(line);
    item->setEditable(false);
    item->setBackground(QColor(245, 247, 250));
    item->setForeground(Qt::darkGray);
    m_chatModel->appendRow(item);
    ui->chatListView->scrollToBottom();
}

void MainWindow::loadHistory(const QString& peerId) {
    QString filePath = getHistoryFilePath(peerId);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        QStandardItem* item = new QStandardItem(line);
        item->setEditable(false);
        item->setBackground(QColor(250, 252, 254));
        item->setForeground(Qt::gray);
        m_chatModel->appendRow(item);
    }
    file.close();
}

void MainWindow::saveHistory(const QString& peerId, const QString& content) {
    if (peerId.isEmpty()) return;
    QString filePath = getHistoryFilePath(peerId);
    QFile file(filePath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << content << "\n";
        file.close();
    }
}

QString MainWindow::getHistoryFilePath(const QString& peerId) {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = ".";
    QDir().mkpath(dir);
    QString safePeerId = peerId;
    safePeerId.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
    return dir + "/chat_history_" + safePeerId + ".txt";
}

QStandardItem* MainWindow::findUserItem(const QString& userId) {
    for (int i = 0; i < m_userListModel->rowCount(); ++i) {
        QStandardItem* item = m_userListModel->item(i);
        if (item->data(Qt::UserRole + 1).toString() == userId) {
            return item;
        }
    }
    return nullptr;
}

void MainWindow::refreshFriendList() {
    m_userListModel->clear();
    m_userListModel->setHorizontalHeaderLabels({"好友 / 在线"});

    QFile file(getFriendFilePath());
    if (m_friendIds.isEmpty() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            QString id = line.section('|', 0, 0);
            QString name = line.section('|', 1);
            if (!id.isEmpty() && !m_friendIds.contains(id)) {
                m_friendIds << id;
            }
            if (!id.isEmpty() && !name.isEmpty()) {
                m_friendNames[id] = name;
            }
        }
    }

    QFile groupFile(getGroupFilePath());
    if (m_localGroupIds.isEmpty() && groupFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&groupFile);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            QString id = line.section('|', 0, 0);
            QString name = line.section('|', 1);
            if (!id.isEmpty() && !m_localGroupIds.contains(id)) {
                m_localGroupIds << id;
            }
            if (!id.isEmpty() && !name.isEmpty()) {
                m_localGroupNames[id] = name;
            }
            QStringList members = line.section('|', 2).split(',', Qt::SkipEmptyParts);
            if (members.isEmpty() && !id.isEmpty()) members << m_currentUserId;
            if (!id.isEmpty()) m_localGroupMembers[id] = members;
            QString announcement = line.section('|', 3);
            if (!id.isEmpty()) m_localGroupAnnouncements[id] = announcement.isEmpty() ? QString("%1 已创建，可继续邀请好友并发送消息。").arg(m_localGroupNames.value(id, "群聊")) : announcement;
        }
    }

    auto appendSection = [this](const QString& title) {
        QStandardItem* section = new QStandardItem(title);
        section->setEditable(false);
        section->setEnabled(false);
        section->setForeground(QColor(176, 212, 232));
        section->setBackground(QColor(22, 46, 64));
        m_userListModel->appendRow(section);
    };

    int visibleCount = 0;
    int visibleFriends = 0;
    int visibleGroups = 0;
    int visibleOnlineUsers = 0;
    auto matchesFilter = [this](const QString& id, const QString& name) {
        return m_contactFilter.isEmpty()
            || id.contains(m_contactFilter, Qt::CaseInsensitive)
            || name.contains(m_contactFilter, Qt::CaseInsensitive);
    };

    appendSection("我的好友");
    for (const QString& friendId : m_friendIds) {
        if (m_knownUsers.contains(friendId)) continue;
        QString name = m_friendNames.value(friendId, friendId);
        if (!matchesFilter(friendId, name)) continue;
        QStandardItem* item = new QStandardItem(QString("☆ QQ:%1\n   %2 [离线]").arg(friendId, name));
        item->setData(friendId, Qt::UserRole + 1);
        item->setForeground(QColor(180, 215, 235));
        m_userListModel->appendRow(item);
        ++visibleCount;
        ++visibleFriends;
    }

    appendSection("群聊");
    for (const QString& groupId : m_localGroupIds) {
        QString groupName = m_localGroupNames.value(groupId, "群聊");
        if (!matchesFilter(groupId, groupName)) continue;
        QStandardItem* item = new QStandardItem(QString("群聊 QQ:%1\n   %2 [本地]").arg(groupId.mid(QString("local_group_").size()), groupName));
        item->setData(groupId, Qt::UserRole + 1);
        item->setForeground(QColor(164, 220, 255));
        m_userListModel->appendRow(item);
        ++visibleCount;
        ++visibleGroups;
    }

    appendSection("在线成员");
    for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
        const ChatUser& user = it.value();
        if (user.name == m_currentUserName) continue;
        if (!matchesFilter(user.id, user.name)) continue;
        bool isFriend = m_friendIds.contains(user.id);
        if (isFriend) {
            m_friendNames[user.id] = user.name;
        }
        QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n   %3%4").arg(isFriend ? "★" : "○", user.id, user.name, isFriend ? " [在线]" : ""));
        item->setData(user.id, Qt::UserRole + 1);
        item->setForeground(isFriend ? Qt::white : QColor(220, 240, 255));
        m_userListModel->appendRow(item);
        ++visibleCount;
        ++visibleOnlineUsers;
    }

    ui->onlineTitleLabel->setText(QString("联系人 · 好友%1 · 群聊%2 · 在线%3").arg(visibleFriends).arg(visibleGroups).arg(visibleOnlineUsers));

    if (visibleCount == 0 && !m_contactFilter.isEmpty()) {
        QStandardItem* emptyItem = new QStandardItem("没有匹配的联系人");
        emptyItem->setEditable(false);
        emptyItem->setEnabled(false);
        emptyItem->setForeground(QColor(220, 240, 255));
        m_userListModel->appendRow(emptyItem);
    }
}

void MainWindow::onContactSearchChanged(const QString& text) {
    m_contactFilter = text.trimmed();
    refreshFriendList();
}

void MainWindow::refreshGroupMemberPanel() {
    if (!ui->groupMemberListView || !m_groupMemberModel) return;
    QString filter = ui->memberSearchEdit ? ui->memberSearchEdit->text().trimmed() : QString();
    m_groupMemberModel->clear();
    m_groupMemberModel->setHorizontalHeaderLabels({"群成员"});

    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        QStringList members = m_localGroupMembers.value(m_privateChatTarget);
        if (members.isEmpty()) members << m_currentUserId;
        int visibleMembers = 0;
        for (const QString& memberId : members) {
            QString name = memberId == m_currentUserId ? m_currentUserName : m_friendNames.value(memberId, memberId);
            if (!filter.isEmpty()
                && !memberId.contains(filter, Qt::CaseInsensitive)
                && !name.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n%3 · %4").arg(memberId == m_currentUserId ? "我" : "群成员", memberId, name, isContactOnline(memberId) || memberId == m_currentUserId ? "在线" : "离线"));
            item->setData(memberId, Qt::UserRole + 1);
            item->setEditable(false);
            item->setForeground(memberId == m_currentUserId ? QColor(18, 150, 247) : QColor(38, 50, 56));
            m_groupMemberModel->appendRow(item);
            ++visibleMembers;
        }
        ui->memberTitleLabel->setText(QString("群聊成员 %1").arg(members.size()));
        if (visibleMembers == 0 && !filter.isEmpty()) {
            QStandardItem* emptyItem = new QStandardItem("没有匹配的群成员");
            emptyItem->setEditable(false);
            emptyItem->setEnabled(false);
            m_groupMemberModel->appendRow(emptyItem);
        }
        return;
    }

    QStandardItem* selfItem = new QStandardItem(QString("我  QQ:%1\n%2 · 在线").arg(m_currentUserId, m_currentUserName));
    selfItem->setData(m_currentUserId, Qt::UserRole + 1);
    selfItem->setEditable(false);
    selfItem->setForeground(QColor(18, 150, 247));
    if (filter.isEmpty() || m_currentUserId.contains(filter, Qt::CaseInsensitive) || m_currentUserName.contains(filter, Qt::CaseInsensitive)) {
        m_groupMemberModel->appendRow(selfItem);
    } else {
        delete selfItem;
    }

    int memberCount = 1;
    for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
        const ChatUser& user = it.value();
        if (user.id == m_currentUserId) continue;
        ++memberCount;
        if (!filter.isEmpty()
            && !user.id.contains(filter, Qt::CaseInsensitive)
            && !user.name.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        bool isFriend = m_friendIds.contains(user.id);
        QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n%3 · 在线").arg(isFriend ? "好友" : "成员", user.id, user.name));
        item->setData(user.id, Qt::UserRole + 1);
        item->setEditable(false);
        item->setForeground(isFriend ? QColor(18, 150, 247) : QColor(38, 50, 56));
        m_groupMemberModel->appendRow(item);
    }
    ui->memberTitleLabel->setText(QString("群聊成员 %1").arg(memberCount));
}

void MainWindow::loadAvatar() {
    QPixmap pixmap(getAvatarFilePath());
    if (!pixmap.isNull()) {
        ui->avatarLabel->setPixmap(pixmap.scaled(ui->avatarLabel->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
}

QString MainWindow::contactDisplayName(const QString& userId) const {
    if (m_knownUsers.contains(userId)) return m_knownUsers.value(userId).name;
    return m_friendNames.value(userId, userId);
}

bool MainWindow::isContactOnline(const QString& userId) const {
    return m_knownUsers.contains(userId) && m_knownUsers.value(userId).isOnline;
}

QString MainWindow::getFriendFilePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = ".";
    QDir().mkpath(dir);
    return dir + "/friends_" + m_currentUserName + ".txt";
}

QString MainWindow::getGroupFilePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = ".";
    QDir().mkpath(dir);
    return dir + "/groups_" + m_currentUserName + ".txt";
}

QString MainWindow::getAvatarFilePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = ".";
    QDir().mkpath(dir);
    return dir + "/avatar_" + m_currentUserName + ".png";
}

void MainWindow::saveFriends() const {
    QFile file(getFriendFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    for (const QString& id : m_friendIds) {
        out << id << "|" << m_friendNames.value(id, id) << "\n";
    }
}

void MainWindow::saveLocalGroups() const {
    QFile file(getGroupFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    for (const QString& id : m_localGroupIds) {
        QStringList members = m_localGroupMembers.value(id);
        if (members.isEmpty()) members << m_currentUserId;
        out << id << "|" << m_localGroupNames.value(id, "群聊") << "|" << members.join(',') << "|" << m_localGroupAnnouncements.value(id) << "\n";
    }
}

void MainWindow::updateUnreadState() {
    setWindowTitle(QString("QtNetworkChat - %1 条新消息").arg(m_unreadCount));
    m_trayIcon->setToolTip(QString("QtNetworkChat - %1 条新消息").arg(m_unreadCount));
}

void MainWindow::clearUnreadState() {
    m_unreadCount = 0;
    setWindowTitle(m_privateChatTarget.isEmpty()
        ? "QtNetworkChat - " + m_currentUserName
        : ui->chatTitleLabel->text());
    m_trayIcon->setToolTip("QtNetworkChat");
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
        clearUnreadState();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!m_isQuitting && m_trayIcon->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    m_client->disconnectFromServer();
    event->accept();
}
