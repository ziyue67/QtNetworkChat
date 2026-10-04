#include "mainwindow.h"
#include "mainwindow_support.h"
#include "ui_mainwindow.h"
#include "sessionitemdelegate.h"
#include "sessionlistbuilder.h"
#include "chatbubbledelegate.h"
#include "qqnt_log.h"
#include "qqnt_backend_service.h"
#include "dialogs/friendmanagerdialog.h"
#include "dialogs/creategroupdialog.h"
#include "dialogs/addfrienddialog.h"
#include "dialogs/globalsearchdialog.h"
#include "dialogs/memberprofilecard.h"
#include "dialogs/groupnicknamedialog.h"
#include "dialogs/essencepanel.h"
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QApplication>
#include <QDateTime>
#include <QDebug>

#include "chatsessionmanager.h"
#include "chatcontextmanager.h"
#include "composermanager.h"
#include "filetransferstatus.h"
#include "localfilemanager.h"
#include "notificationpanelmanager.h"
#include "qtnetworkchat_version.h"
#include "transferchatitemrenderer.h"
#include "windowstatemanager.h"
#include "views/messagesview.h"
#include "views/contactsview.h"
#include "widgets/contactlistwidget.h"
#include "views/favoritesview.h"
#include "views/settingsview.h"
#include "views/profileview.h"
#include "widgets/appnav.h"
#include "widgets/titlebar.h"
#include "widgets/composerwidget.h"
#include "widgets/groupmembersidebar.h"
#include "widgets/avatarlabel.h"
#include "widgets/dialogtitlebar.h"
#include "theme/thememanager.h"
#include "theme/dialogstyle.h"
#include "windows/screenshotcapturewindow.h"
#include "screenshotgeometry.h"
#include "windows/imagepreviewwindow.h"
#include "windows/forwardwindow.h"
#include "dialogs/mutedurationdialog.h"
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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QListView>
#include <QStatusBar>
#include <QPixmap>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QLinearGradient>
#include <QPolygonF>
#include <QRegularExpression>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QClipboard>
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QTabWidget>
#include <QShortcut>
#include <QUrl>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QProgressDialog>
#include <QCheckBox>
#include <QButtonGroup>
#include <QRadioButton>
#include <QComboBox>
#include <QFrame>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QCryptographicHash>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QBuffer>
#include <QScrollArea>
#include <QStackedWidget>
#include <QScreen>
#include <QStyleHints>
#include <QPalette>
#include <QWindow>
#include <functional>
#include <algorithm>


using namespace MainWindowSupport;

MainWindow::MainWindow(Client* client, const QString& userId, const QString& userName, QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_client(client)
    , m_clientStorage(userName)
    , m_userListModel(new QStandardItemModel(this))
    , m_sessionModel(new QStandardItemModel(this))
    , m_chatModel(new QStandardItemModel(this))
    , m_groupMemberModel(new QStandardItemModel(this))
    , m_historyService(userId)
    , m_currentUserId(userId)
    , m_currentUserName(userName)
    , m_hasServerGroupSnapshot(false)
    , m_wasInPublicServerGroup(false)
    , m_privateChatTarget(QString())
    , m_trayIcon(new QSystemTrayIcon(this))
    , m_resumeSavedTransferAction(nullptr)
    , m_clearSavedTransferAction(nullptr)
    , m_unreadCount(0)
    , m_isQuitting(false)
{
    qtnetworkchat::logDebug("MainWindow", "constructor start");
    ui->setupUi(this);
    qtnetworkchat::logDebug("MainWindow", "ui setup done");

    // Apply drop shadows to structural cards to create Z-depth
    auto addShadow = [](QWidget* widget) {
        if (!widget) return;
        QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(widget);
        shadow->setBlurRadius(20);
        shadow->setColor(QColor(94, 92, 230, 60));
        shadow->setOffset(0, 4);
        widget->setGraphicsEffect(shadow);
    };

    addShadow(ui->profileCard);
    addShadow(ui->announcementCard);
    addShadow(ui->groupOverviewCard);
    addShadow(ui->transferOverviewCard);
    qtnetworkchat::logDebug("MainWindow", "shadow effects applied");

    // Load theme-aware stylesheet
    loadStyleSheet();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MainWindow::loadStyleSheet);
    qtnetworkchat::logDebug("MainWindow", "stylesheet loaded");

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    resize(1100, 740);
    setMinimumSize(860, 540);
    setWindowIcon(createChatIcon(userName));
    setupUi();
    qtnetworkchat::logDebug("MainWindow", "setupUi done");
    setupTray();
    qtnetworkchat::logDebug("MainWindow", "setupTray done");
    setupQQNT();
    qtnetworkchat::logDebug("MainWindow", "setupQQNT done");

    if (m_client && !m_client->parent()) {
        m_client->setParent(this);
    }
    if (!m_client) {
        QMessageBox::critical(this, "错误", "客户端未初始化");
        close();
        return;
    }

    setWindowTitle(appWindowTitle(userName));
    ui->avatarLabel->setText(userName.left(1).toUpper());
    ui->profileNameLabel->setText("QQ: " + userId);
    ui->profileIdLabel->setText("昵称: " + userName);
    loadAvatar();
    ui->appTitleLabel->setText("Qt 聊天室");
    ui->chatTitleLabel->setText("公共聊天室");
    ui->chatHintLabel->setText(QString("账号 %1 · 双击左侧成员可私聊").arg(m_currentUserId));

    connect(m_client, &Client::connected, this, [this]() {
        appendSystemMessage("已连接服务器 · " + m_client->transportSecurityDescription());
        ui->statusbar->showMessage(m_client->transportSecurityDescription(), 2200);
        refreshComposerState();
        updateSavedOutgoingTransferRecoveryUi(true);
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
    connect(m_client, &Client::serverGroupSnapshotReceived, this, &MainWindow::onServerGroupSnapshotReceived);
    connect(m_client, &Client::serverGroupJoinApplicationReceived, this, [this](const QJsonObject& application) {
        const QString requestId = application.value("requestId").toString();
        if (requestId.isEmpty()) return;
        const QString groupName = application.value("groupName").toString(QStringLiteral("群聊"));
        const QString applicant = application.value("applicantName").toString(application.value("applicantId").toString());
        m_pendingGroupJoinApplications.insert(requestId, application);
        appendSystemMessage(QStringLiteral("收到入群申请：%1 申请加入“%2”，请在群通知中处理").arg(applicant, groupName));
        ui->statusbar->showMessage(QStringLiteral("新的入群申请 · 请在群通知中同意或拒绝"), 3500);
        ui->groupNoticeBtn->setText(QStringLiteral("群通知 %1").arg(m_pendingGroupJoinApplications.size()));
        ui->groupNoticeBtn->setToolTip(QStringLiteral("有 %1 条入群申请待处理").arg(m_pendingGroupJoinApplications.size()));
    });
    connect(m_client, &Client::serverGroupJoinRequestStatusReceived, this, [this](const QJsonObject& status) {
        const QString requestId = status.value("requestId").toString();
        const QString groupId = status.value("groupId").toString();
        const QString groupName = status.value("groupName").toString(QStringLiteral("群聊"));
        const QString state = status.value("state").toString();
        const QString reason = status.value("reason").toString();
        if (m_activeContactGroupSearchDialog && !groupId.isEmpty()) {
            m_activeContactGroupSearchDialog->setGroupJoinPending(groupId, state == QLatin1String("pending"));
            if (state == QLatin1String("approved")) m_activeContactGroupSearchDialog->setGroupJoined(groupId, true);
        }
        if (state == QLatin1String("pending")) {
            if (!requestId.isEmpty()) m_pendingOutgoingGroupJoinApplications.insert(requestId, status);
            ui->statusbar->showMessage(QStringLiteral("已提交入群申请，等待群主或管理员同意"), 3200);
        } else if (state == QLatin1String("approved")) {
            appendSystemMessage(QStringLiteral("入群申请已通过：%1").arg(groupName));
            ui->statusbar->showMessage(QStringLiteral("已通过“%1”的入群申请").arg(groupName), 3200);
        } else {
            ui->statusbar->showMessage(reason.isEmpty() ? QStringLiteral("入群申请被拒绝") : reason, 3200);
        }
        if (!requestId.isEmpty() && m_pendingGroupJoinApplications.contains(requestId)
            && (state == QLatin1String("approved") || state == QLatin1String("rejected"))) {
            m_pendingGroupJoinApplications.remove(requestId);
            ui->groupNoticeBtn->setText(m_pendingGroupJoinApplications.isEmpty()
                ? QStringLiteral("群通知")
                : QStringLiteral("群通知 %1").arg(m_pendingGroupJoinApplications.size()));
        }
        if (!requestId.isEmpty() && state == QLatin1String("approved")) {
            m_pendingOutgoingGroupJoinApplications.remove(requestId);
        }
        if (!requestId.isEmpty() && state == QLatin1String("rejected")) {
            QJsonObject rejected = status;
            rejected["reviewerName"] = status.value("reviewerName").toString(QStringLiteral("群主或管理员"));
            m_pendingOutgoingGroupJoinApplications.remove(requestId);
            m_rejectedOutgoingGroupJoinApplications.insert(requestId, rejected);
        }
    });
    connect(m_client, &Client::serverGroupLeaveStatusReceived, this, [this](const QJsonObject& status) {
        const QString groupId = status.value("groupId").toString();
        const QString groupName = status.value("groupName").toString(QStringLiteral("群聊"));
        if (!status.value("success").toBool(false)) {
            ui->statusbar->showMessage(status.value("reason").toString(QStringLiteral("退出群聊失败")), 3000);
            return;
        }
        const QString localId = m_joinedServerSearchGroups.take(groupId);
        if (!localId.isEmpty()) {
            m_localGroupIds.removeAll(localId);
            m_localGroupNames.remove(localId);
            m_localGroupMembers.remove(localId);
            m_localGroupAnnouncements.remove(localId);
            m_localGroupAvatarPaths.remove(localId);
            saveLocalGroups();
        }
        refreshFriendList();
        ui->statusbar->showMessage(QStringLiteral("已退出群聊：%1").arg(groupName), 2600);
    });
    connect(m_client, &Client::serverGroupDissolveStatusReceived, this, [this](const QJsonObject& status) {
        const QString groupId = status.value("groupId").toString();
        const QString groupName = status.value("groupName").toString(QStringLiteral("群聊"));
        if (!status.value("success").toBool(false)) {
            ui->statusbar->showMessage(status.value("reason").toString(QStringLiteral("解散群聊失败")), 3000);
            return;
        }
        const QString localId = m_joinedServerSearchGroups.take(groupId);
        if (!localId.isEmpty()) {
            m_localGroupIds.removeAll(localId);
            m_localGroupNames.remove(localId);
            m_localGroupMembers.remove(localId);
            m_localGroupAnnouncements.remove(localId);
            m_localGroupAvatarPaths.remove(localId);
            saveLocalGroups();
        }
        if (m_privateChatTarget == localId) {
            m_privateChatTarget.clear();
            showMessagesView();
        }
        refreshFriendList();
        ui->statusbar->showMessage(QStringLiteral("群聊已解散：%1").arg(groupName), 2800);
    });
    connect(m_client, &Client::serverGroupSearchResults, this, [this](const QString&, const QJsonArray& groups) {
        if (!m_activeContactGroupSearchDialog) return;
        m_activeContactGroupSearchDialog->setSearching(false);
        for (const QJsonValue& value : groups) {
            const QJsonObject group = value.toObject();
            const QString id = group.value("groupId").toString();
            if (id.isEmpty() || m_serverGroupNames.contains(id) || m_localGroupIds.contains(id)) continue;
            const QString name = group.value("groupName").toString(id);
            const int count = group.value("memberCount").toInt();
            m_activeContactGroupSearchDialog->addResult(QStringLiteral("group"), id, name,
                QStringLiteral("群号:%1 · %2 人").arg(id).arg(count));
        }
    });
    connect(m_client, &Client::e2eSessionStateChanged, this, &MainWindow::onE2ESessionStateChanged);
    connect(m_client, &Client::e2eIdentityStateChanged, this, &MainWindow::onE2EIdentityStateChanged);
    connect(m_client, &Client::e2eSessionRotationRequested, this, &MainWindow::onE2ESessionRotationRequested);
    connect(m_client, &Client::e2eSessionRotationResponded, this, &MainWindow::onE2ESessionRotationResponded);
    connect(m_client, &Client::fileTransferStatusChanged, this, &MainWindow::onFileTransferStatusChanged);
    connect(m_client, &Client::fileReceiveProgress, this, [this](const QString& fileName, qint64 bytesReceived, qint64 totalBytes) {
        const int percent = totalBytes > 0
            ? qBound(0, static_cast<int>((bytesReceived * 100) / totalBytes), 100)
            : 0;
        const QString detail = QString("正在接收分片文件 · %1 · %2 / %3 · %4%")
            .arg(fileName, LocalFileManager::humanFileSize(bytesReceived), LocalFileManager::humanFileSize(totalBytes))
            .arg(percent);
        ui->chatHintLabel->setText(detail);
        ui->statusbar->showMessage(detail, 1600);
    });

    m_currentUserId = m_client->currentUserId();
    m_currentUserName = m_client->currentUserName();
    m_historyService.setUserId(m_currentUserId);
    m_clientStorage.setUserName(m_currentUserName);
    setWindowIcon(createChatIcon(m_currentUserName));
    ui->profileNameLabel->setText("QQ: " + m_currentUserId);
    ui->profileIdLabel->setText("昵称: " + m_currentUserName);
    ui->profileNameLabel->setToolTip(QString("当前 QQ 号：%1").arg(m_currentUserId));
    ui->profileIdLabel->setToolTip(QString("当前昵称：%1").arg(m_currentUserName));
    ui->profileCard->setToolTip("右键可复制名片、在线状态，或打开好友管理");
    ui->copyAccountBtn->setToolTip(QString("复制 QQ 号 %1 到剪贴板").arg(m_currentUserId));
    qtnetworkchat::logDebug("MainWindow", "before saveProfileToSqlite");
    saveProfileToSqlite();
    qtnetworkchat::logDebug("MainWindow", "after saveProfileToSqlite");
    ui->addFriendBtn->hide();
    ui->uploadAvatarBtn->setText("换头像");
    if (m_client->hasServerGroupSnapshot()) {
        onServerGroupSnapshotReceived(m_client->serverGroups());
    }

    if (m_currentUserId.isEmpty()) {
        ui->statusbar->showMessage("已连接");
    } else {
        ui->statusbar->showMessage("已连接 - 用户ID: " + m_currentUserId);
    }
    qtnetworkchat::logDebug("MainWindow", "before refreshWorkspaceChrome");
    refreshWorkspaceChrome();
    qtnetworkchat::logDebug("MainWindow", "after refreshWorkspaceChrome");
    qtnetworkchat::logDebug("MainWindow", "before loadHistory");
    loadHistory("group");
    qtnetworkchat::logDebug("MainWindow", "after loadHistory");
    qtnetworkchat::logDebug("MainWindow", "before updateSavedOutgoingTransferRecoveryUi");
    updateSavedOutgoingTransferRecoveryUi(true);
    qtnetworkchat::logDebug("MainWindow", "constructor end");
}

MainWindow::~MainWindow() {
    // A window-owned Client can emit disconnected while QObject deletes its
    // children, after the chat models have gone. Stop UI callbacks before that
    // destruction phase; externally owned clients follow the same lifetime rule.
    if (m_client) QObject::disconnect(m_client, nullptr, this, nullptr);
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
}

LocalSavedFileState MainWindow::savedFileActionState(const QModelIndex& index) const {
    LocalSavedFileState state;
    if (!index.isValid()) return state;
    QString openPath = index.data(ChatBubbleMediaOpenPathRole).toString().trimmed();
    if (!openPath.isEmpty()) {
        state.savePath = openPath;
        state.fileInfo = QFileInfo(openPath);
        state.folderInfo = QFileInfo(state.fileInfo.absolutePath());
        state.hasSavePath = true;
        state.canOpenFile = state.fileInfo.exists() && state.fileInfo.isFile();
        state.canOpenFolder = state.folderInfo.exists() && state.folderInfo.isDir();
        return state;
    }
    return LocalFileManager::savedFileStateFromChatText(index.data().toString(),
                                                        index.data(Qt::ToolTipRole).toString());
}

ChatContextSavedFileState MainWindow::chatContextSavedFileState(const LocalSavedFileState& savedFileState) const {
    ChatContextSavedFileState state;
    state.hasSavePath = savedFileState.hasSavePath;
    state.canOpenFile = savedFileState.canOpenFile;
    state.canOpenFolder = savedFileState.canOpenFolder;
    state.fileExists = savedFileState.fileInfo.exists();
    return state;
}

bool MainWindow::copySavedFilePathToClipboard(const ChatContextSavedFileCommand& command) {
    if (!command.canExecute) {
        ui->statusbar->showMessage(command.missingStatusMessage, command.timeoutMs);
        return false;
    }

    QApplication::clipboard()->setText(command.clipboardText);
    ui->statusbar->showMessage(command.successStatusMessage, command.timeoutMs);
    return true;
}

bool MainWindow::openSavedFileFromState(const LocalSavedFileState& savedFileState, const ChatContextSavedFileCommand& command) {
    if (!command.canExecute) {
        ui->statusbar->showMessage(command.missingStatusMessage, command.timeoutMs);
        return false;
    }

    if (QDesktopServices::openUrl(QUrl::fromLocalFile(savedFileState.fileInfo.absoluteFilePath()))) {
        ui->statusbar->showMessage(command.successStatusMessage, command.timeoutMs);
        return true;
    }

    showFileTransferStatusEvent(savedFileState.fileInfo.fileName(),
                                QString(),
                                command.failureTransferReason,
                                0,
                                0);
    ui->statusbar->showMessage(command.failureStatusMessage, command.timeoutMs);
    return false;
}

bool MainWindow::openMediaPreviewFromState(const LocalSavedFileState& savedFileState) {
    if (!savedFileState.canOpenFile) {
        return false;
    }

    ImagePreviewWindow preview(this);
    preview.setWindowTitle(QStringLiteral("图片预览 - %1").arg(savedFileState.fileInfo.fileName()));
    preview.setImagePath(savedFileState.fileInfo.absoluteFilePath());
    connect(&preview, &ImagePreviewWindow::saveRequested, this,
            [this, &savedFileState](const QString& destinationPath) {
        if (destinationPath.isEmpty()) return;
        const QFileInfo destinationInfo(destinationPath);
        QJsonObject payload;
        payload[QStringLiteral("sourcePath")] = savedFileState.fileInfo.absoluteFilePath();
        payload[QStringLiteral("directoryPath")] = destinationInfo.absolutePath();
        payload[QStringLiteral("fileName")] = destinationInfo.fileName();
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        if (QQNTBackendService::handle(QStringLiteral("save_file_to_directory"), payload, &response, &errorCode, &errorMessage)) {
            ui->statusbar->showMessage(QStringLiteral("图片已保存到: %1").arg(response.value(QStringLiteral("filePath")).toString()), 2200);
        } else {
            ui->statusbar->showMessage(QStringLiteral("保存图片失败: %1").arg(errorMessage), 3000);
        }
    });
    preview.exec();
    ui->statusbar->showMessage(QStringLiteral("已打开图片预览"), 1800);
    return true;
}

bool MainWindow::openSavedFolderFromState(const LocalSavedFileState& savedFileState, const ChatContextSavedFileCommand& command) {
    if (!command.canExecute) {
        ui->statusbar->showMessage(command.missingStatusMessage, command.timeoutMs);
        return false;
    }

    if (QDesktopServices::openUrl(QUrl::fromLocalFile(savedFileState.folderInfo.absoluteFilePath()))) {
        ui->statusbar->showMessage(command.successStatusMessage, command.timeoutMs);
        return true;
    }

    ui->statusbar->showMessage(command.failureStatusMessage, command.timeoutMs);
    return false;
}

void MainWindow::copyTextWithStatus(const QString& text, const QString& statusMessage, int timeoutMs) {
    QApplication::clipboard()->setText(text);
    ui->statusbar->showMessage(statusMessage, timeoutMs);
}


bool MainWindow::handleSavedFileContextCommand(const QString& commandId, const LocalSavedFileState& savedFileState) {
    const ChatContextSavedFileCommand command = ChatContextManager::savedFileCommand(commandId,
                                                                                     chatContextSavedFileState(savedFileState),
                                                                                     savedFileState.savePath);
    if (!command.handled) {
        return false;
    }

    if (command.action == ChatContextSavedFileCommand::Action::CopySavePath) {
        return copySavedFilePathToClipboard(command);
    }
    if (command.action == ChatContextSavedFileCommand::Action::OpenSavedFile) {
        if (openMediaPreviewFromState(savedFileState)) {
            return true;
        }
        return openSavedFileFromState(savedFileState, command);
    }
    if (command.action == ChatContextSavedFileCommand::Action::OpenSaveFolder) {
        return openSavedFolderFromState(savedFileState, command);
    }
    return false;
}

bool MainWindow::confirmAction(const QString& title,
                               const QString& message,
                               const QString& canceledStatusMessage,
                               int canceledStatusTimeoutMs,
                               QWidget* parent) {
    QWidget* dialogParent = parent ? parent : this;
    QDialog dialog(dialogParent);
    dialog.setObjectName(QStringLiteral("projectConfirmDialog"));
    dialog.setWindowTitle(title);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setFixedSize(344, 190);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 16);
    layout->setSpacing(0);
    auto* bar = new DialogTitleBar(&dialog, title);
    connect(bar, &DialogTitleBar::closeRequested, &dialog, &QDialog::reject);
    layout->addWidget(bar);
    auto* content = new QWidget(&dialog);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 16, 18, 0);
    auto* detail = new QLabel(message, content);
    detail->setObjectName(QStringLiteral("projectConfirmDetail"));
    detail->setWordWrap(true);
    contentLayout->addWidget(detail, 1);
    auto* actions = new QHBoxLayout();
    actions->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("取消"), content);
    cancel->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    auto* accept = new QPushButton(QStringLiteral("确认"), content);
    accept->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    actions->addWidget(cancel);
    actions->addWidget(accept);
    contentLayout->addLayout(actions);
    layout->addWidget(content, 1);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#projectConfirmDialog { background:%1; border:1px solid %2; }"
        "QLabel#projectConfirmDetail { color:%3; font-size:13px; line-height:1.45; }")
        .arg(ThemeManager::instance()->backgroundColor().name(), ThemeManager::instance()->borderColor().name(),
             ThemeManager::instance()->textSecondaryColor().name()));
    if (dialog.exec() == QDialog::Accepted) {
        return true;
    }
    if (!canceledStatusMessage.isEmpty()) {
        ui->statusbar->showMessage(canceledStatusMessage, canceledStatusTimeoutMs);
    }
    return false;
}

QString MainWindow::promptTextValue(const QString& title,
                                    const QString& label,
                                    const QString& initialValue,
                                    bool* accepted,
                                    QWidget* parent) const {
    QWidget* dialogParent = parent ? parent : const_cast<MainWindow*>(this);
    QDialog dialog(dialogParent);
    dialog.setObjectName(QStringLiteral("projectTextPrompt"));
    dialog.setWindowTitle(title);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setFixedSize(344, 190);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 16);
    layout->setSpacing(0);
    auto* bar = new DialogTitleBar(&dialog, title);
    connect(bar, &DialogTitleBar::closeRequested, &dialog, &QDialog::reject);
    layout->addWidget(bar);
    auto* content = new QWidget(&dialog);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 14, 18, 0);
    auto* hint = new QLabel(label, content);
    hint->setObjectName(QStringLiteral("projectPromptHint"));
    auto* input = new QLineEdit(initialValue, content);
    input->setObjectName(QStringLiteral("projectPromptInput"));
    input->setClearButtonEnabled(true);
    contentLayout->addWidget(hint);
    contentLayout->addWidget(input);
    auto* actions = new QHBoxLayout(); actions->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("取消"), content); cancel->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    auto* save = new QPushButton(QStringLiteral("保存"), content); save->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    actions->addWidget(cancel); actions->addWidget(save); contentLayout->addLayout(actions);
    layout->addWidget(content, 1);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#projectTextPrompt { background:%1; border:1px solid %2; }"
        "QLabel#projectPromptHint { color:%3; font-size:12px; }"
        "QLineEdit#projectPromptInput { background:%4; color:%5; border:1px solid %2; border-radius:6px; padding:8px 10px; }"
        "QLineEdit#projectPromptInput:focus { border-color:%6; }")
        .arg(ThemeManager::instance()->backgroundColor().name(), ThemeManager::instance()->borderColor().name(),
             ThemeManager::instance()->textSecondaryColor().name(), ThemeManager::instance()->backgroundSecondaryColor().name(),
             ThemeManager::instance()->textColor().name(), ThemeManager::instance()->primaryColor().name()));
    input->setFocus();
    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted) *accepted = ok;
    return ok ? input->text().trimmed() : QString();
}

QString MainWindow::promptMultilineValue(const QString& title,
                                         const QString& label,
                                         const QString& initialValue,
                                         bool* accepted,
                                         QWidget* parent) const {
    QWidget* dialogParent = parent ? parent : const_cast<MainWindow*>(this);
    QDialog dialog(dialogParent);
    dialog.setObjectName(QStringLiteral("projectMultilinePrompt"));
    dialog.setWindowTitle(title);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setFixedSize(360, 272);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 16);
    layout->setSpacing(0);
    auto* bar = new DialogTitleBar(&dialog, title);
    connect(bar, &DialogTitleBar::closeRequested, &dialog, &QDialog::reject);
    layout->addWidget(bar);
    auto* content = new QWidget(&dialog);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 14, 18, 0);
    auto* hint = new QLabel(label, content); hint->setObjectName(QStringLiteral("projectPromptHint"));
    auto* input = new QPlainTextEdit(initialValue, content); input->setObjectName(QStringLiteral("projectPromptText")); input->setFixedHeight(112);
    contentLayout->addWidget(hint); contentLayout->addWidget(input);
    auto* actions = new QHBoxLayout(); actions->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("取消"), content); cancel->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    auto* save = new QPushButton(QStringLiteral("保存"), content); save->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    actions->addWidget(cancel); actions->addWidget(save); contentLayout->addLayout(actions);
    layout->addWidget(content, 1);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#projectMultilinePrompt { background:%1; border:1px solid %2; }"
        "QLabel#projectPromptHint { color:%3; font-size:12px; }"
        "QPlainTextEdit#projectPromptText { background:%4; color:%5; border:1px solid %2; border-radius:6px; padding:8px 10px; }"
        "QPlainTextEdit#projectPromptText:focus { border-color:%6; }")
        .arg(ThemeManager::instance()->backgroundColor().name(), ThemeManager::instance()->borderColor().name(),
             ThemeManager::instance()->textSecondaryColor().name(), ThemeManager::instance()->backgroundSecondaryColor().name(),
             ThemeManager::instance()->textColor().name(), ThemeManager::instance()->primaryColor().name()));
    input->setFocus();
    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted) *accepted = ok;
    return ok ? input->toPlainText().trimmed() : QString();
}

QString MainWindow::promptItemValue(const QString& title,
                                    const QString& label,
                                    const QStringList& items,
                                    bool* accepted,
                                    QWidget* parent) const {
    QWidget* dialogParent = parent ? parent : const_cast<MainWindow*>(this);
    return QInputDialog::getItem(dialogParent,
                                 title,
                                 label,
                                 items,
                                 0,
                                 false,
                                 accepted).trimmed();
}

QStringList MainWindow::currentSessionMemberIds() const {
    if (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
        return m_localGroupMembers.value(m_privateChatTarget);
    }

    QStringList ids;
    for (auto it = m_knownUsers.constBegin(); it != m_knownUsers.constEnd(); ++it) {
        ids << it.key();
    }
    return ids;
}

bool MainWindow::applyAvatarSelection(const LocalFileSelectionResult& selection) {
    if (selection.canceled) {
        ui->statusbar->showMessage(selection.canceledStatusMessage, selection.canceledStatusTimeoutMs);
        return false;
    }
    if (!selection.accepted) {
        QMessageBox::warning(this, selection.failureTitle, selection.failureMessage);
        ui->statusbar->showMessage(selection.rejectedStatusMessage.isEmpty() ? selection.statusMessage
                                                                             : selection.rejectedStatusMessage,
                                   selection.rejectedStatusTimeoutMs);
        return false;
    }

    const QPixmap pixmap(selection.filePath);
    if (pixmap.isNull()) {
        const LocalFileSelectionResult invalidAvatar = LocalFileManager::invalidAvatarDataResult();
        QMessageBox::warning(this, invalidAvatar.invalidDataTitle, invalidAvatar.invalidDataMessage);
        ui->statusbar->showMessage(invalidAvatar.invalidDataStatusMessage, invalidAvatar.invalidDataStatusTimeoutMs);
        return false;
    }

    return persistAvatarPixmap(pixmap, selection.fileInfo);
}

bool MainWindow::persistAvatarPixmap(const QPixmap& pixmap, const QFileInfo& info) {
    QPixmap savedAvatar = squareAvatarPixmap(pixmap, 256);
    if (savedAvatar.isNull() || !savedAvatar.save(getAvatarFilePath(), "PNG")) {
        const LocalFileSelectionResult saveFailed = LocalFileManager::avatarSaveFailedResult();
        QMessageBox::warning(this, saveFailed.saveFailedTitle, saveFailed.saveFailedMessage);
        ui->statusbar->showMessage(saveFailed.saveFailedStatusMessage, saveFailed.saveFailedStatusTimeoutMs);
        return false;
    }

    ui->avatarLabel->setPixmap(squareAvatarPixmap(savedAvatar, ui->avatarLabel->width()));
    saveProfileToSqlite();
    QByteArray avatarBytes;
    QBuffer avatarBuffer(&avatarBytes);
    if (avatarBuffer.open(QIODevice::WriteOnly) && savedAvatar.save(&avatarBuffer, "PNG")) {
        if (m_client && m_client->isConnected()) {
            m_client->sendAvatarUpdate(avatarBytes);
        } else if (m_client) {
            m_client->setAvatarData(avatarBytes);
        }
    }
    const LocalAvatarAppliedState appliedState = LocalFileManager::avatarAppliedState(info);
    ui->avatarLabel->setToolTip(appliedState.toolTip);
    ui->uploadAvatarBtn->setToolTip(appliedState.toolTip);
    appendSystemMessage(appliedState.detail);
    ui->chatHintLabel->setText(appliedState.detail);
    ui->statusbar->showMessage(appliedState.detail, appliedState.statusTimeoutMs);
    return true;
}

void MainWindow::openPrivateSession(const QString& userId) {
    if (userId.isEmpty()) {
        return;
    }
    const QString userName = contactDisplayName(userId);
    m_privateChatTarget = userId;
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({QStringLiteral("聊天记录")});
    loadHistory(userId);
    readLocalChatActions(userId);
    const PrivateChatUiState privateState = ChatSessionManager::privateChatState(
        userId,
        userName,
        isContactOnline(userId),
        m_client && m_client->hasE2ESession(userId),
        m_client && m_client->e2eSessionNeedsRotation(userId));
    setWindowTitle(appWindowTitle(privateState.windowSuffix));
    ui->chatTitleLabel->setText(privateState.titleText);
    ui->chatHintLabel->setText(privateState.hintText);
    if (m_messagesView) {
        // Essence messages and the member rail only exist for groups; hide both
        // in private chats.
        m_messagesView->setEssenceButtonVisible(false);
        m_messagesView->setGroupMoreButtonVisible(false);
        m_messagesView->setGroupMemberSidebarVisible(false);
    }
    refreshComposerState();
}

bool MainWindow::ensureFriendRequestQueued(const QString& userId,
                                           const QString& successTemplate,
                                           bool refreshGroups) {
    if (userId.isEmpty() || userId == m_currentUserId) {
        return false;
    }
    const QString displayName = contactDisplayName(userId);
    if (m_friendIds.contains(userId)) {
        return true;
    }
    if (m_pendingOutgoingFriendRequests.contains(userId)) {
        ui->statusbar->showMessage(QStringLiteral("已向 %1 发送过好友申请，等待对方处理").arg(displayName), 2500);
        return false;
    }
    if (!m_client || !m_client->sendFriendRequest(userId)) {
        ui->statusbar->showMessage(QStringLiteral("好友申请发送失败：%1").arg(displayName), 3000);
        return false;
    }
    m_friendNames[userId] = displayName;
    m_pendingOutgoingFriendRequests << userId;
    refreshFriendList();
    if (refreshGroups) {
        refreshGroupMemberPanel();
    }
    if (!successTemplate.isEmpty()) {
        appendSystemMessage(successTemplate.arg(userId));
    }
    return true;
}

QString MainWindow::createLocalGroupSession(const QString& groupName,
                                            const QStringList& members,
                                            const QString& announcement) {
    const QString normalizedName = groupName.trimmed().isEmpty() ? QStringLiteral("我的群聊") : groupName.trimmed();
    const QString groupId = QStringLiteral("local_group_")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz"));
    QStringList normalizedMembers = members;
    if (!normalizedMembers.contains(m_currentUserId)) {
        normalizedMembers.prepend(m_currentUserId);
    }
    normalizedMembers.removeAll(QString());
    normalizedMembers.removeDuplicates();
    m_localGroupIds << groupId;
    m_localGroupNames[groupId] = normalizedName;
    m_localGroupAnnouncements[groupId] = announcement.isEmpty()
        ? QStringLiteral("%1 已创建，可继续邀请好友并发送消息。").arg(normalizedName)
        : announcement;
    m_localGroupMembers[groupId] = normalizedMembers;
    saveLocalGroups();
    refreshFriendList();
    return groupId;
}

int MainWindow::appendMembersToLocalGroup(const QString& groupId, const QStringList& memberIds) {
    if (groupId.isEmpty() || !m_localGroupIds.contains(groupId)) {
        return 0;
    }
    int appended = 0;
    QStringList& members = m_localGroupMembers[groupId];
    if (!members.contains(m_currentUserId)) {
        members.prepend(m_currentUserId);
    }
    for (const QString& memberId : memberIds) {
        if (memberId.isEmpty() || memberId == m_currentUserId || members.contains(memberId)) {
            continue;
        }
        members << memberId;
        ++appended;
    }
    if (appended > 0) {
        saveLocalGroups();
        refreshFriendList();
        refreshGroupMemberPanel();
    }
    return appended;
}


void MainWindow::setChatDraftText(const QString& text, const QString& statusMessage, int timeoutMs) {
    QTextEdit* input = m_messagesView ? m_messagesView->composer()->inputEdit() : ui->messageEdit;
    input->setPlainText(text);
    input->setFocus();
    ui->statusbar->showMessage(statusMessage, timeoutMs);
}

void MainWindow::insertChatDraftText(const QString& text, const QString& statusMessage, int timeoutMs) {
    QTextEdit* input = m_messagesView ? m_messagesView->composer()->inputEdit() : ui->messageEdit;
    input->insertPlainText(text);
    input->setFocus();
    ui->statusbar->showMessage(statusMessage, timeoutMs);
}

ChatContextComposerState MainWindow::currentChatContextComposerState() const {
    ChatContextComposerState composerState;
    composerState.privateChatTarget = m_privateChatTarget;
    composerState.targetDisplayName = m_privateChatTarget.isEmpty()
        ? QStringLiteral("公共聊天室")
        : contactDisplayName(m_privateChatTarget);
    composerState.currentUserId = m_currentUserId;
    composerState.currentUserName = m_currentUserName;
    composerState.currentGroupName = m_privateChatTarget.startsWith("local_group_")
        ? m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊"))
        : QStringLiteral("群聊");
    composerState.currentGroupMemberCount = m_localGroupMembers.value(m_privateChatTarget).size();
    composerState.currentTargetOnline = isContactOnline(m_privateChatTarget);
    composerState.friendCount = m_friendIds.size();
    composerState.localGroupCount = m_localGroupIds.size();
    composerState.knownUserCount = m_knownUsers.size();
    return composerState;
}

bool MainWindow::applyChatContextComposerCommand(const QString& commandId) {
    const ChatContextComposerCommand command =
        ChatContextManager::composerCommand(commandId, currentChatContextComposerState());
    if (!command.handled) {
        return false;
    }

    if (command.action == ChatContextComposerCommand::Action::SetDraft) {
        setChatDraftText(command.text, command.statusMessage, command.timeoutMs);
        return true;
    }
    if (command.action == ChatContextComposerCommand::Action::InsertText) {
        insertChatDraftText(command.text, command.statusMessage, command.timeoutMs);
        return true;
    }
    return false;
}

QAction* MainWindow::addChatContextAction(QMenu& menu,
                                          const QString& title,
                                          const QString& tip,
                                          const QString& commandId,
                                          bool enabled) {
    QAction* action = menu.addAction(title);
    action->setToolTip(tip);
    action->setStatusTip(tip);
    action->setData(commandId);
    action->setEnabled(enabled);
    return action;
}

bool MainWindow::handleChatContextCommand(const QString& commandId,
                                          const QString& chatText,
                                          const LocalSavedFileState& savedFileState,
                                          const QModelIndex& index) {
    const ChatContextCommandRoute route = ChatContextManager::commandRoute(commandId);
    if (!route.handled) {
        return false;
    }

    const QString targetDisplayName = m_privateChatTarget.isEmpty()
        ? QStringLiteral("公共聊天室")
        : contactDisplayName(m_privateChatTarget);

    if (route.kind == ChatContextCommandRoute::Kind::Copy) {
        const ChatContextCopyResult copyResult = ChatContextManager::copyCommandResult(
            commandId,
            chatText,
            m_privateChatTarget,
            targetDisplayName,
            m_currentUserId,
            m_currentUserName);
        if (!copyResult.handled) {
            return false;
        }
        copyTextWithStatus(copyResult.clipboardText, copyResult.statusMessage, copyResult.timeoutMs);
        return true;
    }

    if (route.kind == ChatContextCommandRoute::Kind::SavedFile) {
        return handleSavedFileContextCommand(commandId, savedFileState);
    }

    if (route.kind == ChatContextCommandRoute::Kind::Draft) {
        const ChatContextDraftResult draftResult = ChatContextManager::draftCommandResult(
            commandId,
            chatText,
            m_privateChatTarget,
            targetDisplayName);
        if (!draftResult.handled) {
            return false;
        }
        if (draftResult.action == ChatContextDraftResult::Action::SetDraft) {
            setChatDraftText(draftResult.draftText, draftResult.statusMessage, draftResult.timeoutMs);
        } else if (draftResult.action == ChatContextDraftResult::Action::Resend) {
            ui->messageEdit->setPlainText(draftResult.resendText);
            ui->messageEdit->setFocus();
            onSendMessage();
        }
        return true;
    }

    if (route.kind == ChatContextCommandRoute::Kind::Backend) {
        return handleBackendContextCommand(commandId, chatText, index);
    }

    return false;
}

bool MainWindow::handleBackendContextCommand(const QString& commandId,
                                             const QString& chatText,
                                             const QModelIndex& index) {
    if (!index.isValid()) return false;

    const QString sessionId = m_privateChatTarget.isEmpty()
        ? QStringLiteral("public")
        : m_privateChatTarget;
    const QString senderId = index.data(ChatBubbleSenderIdRole).toString();
    const QString senderName = index.data(ChatBubbleSenderNameRole).toString();
    const QString messageId = QStringLiteral("msg-%1-%2").arg(index.row()).arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz")));

    QJsonObject messageObject;
    messageObject[QStringLiteral("id")] = messageId;
    messageObject[QStringLiteral("messageId")] = messageId;
    messageObject[QStringLiteral("senderId")] = senderId;
    messageObject[QStringLiteral("senderName")] = senderName;
    messageObject[QStringLiteral("content")] = chatText;
    messageObject[QStringLiteral("sessionId")] = sessionId;
    messageObject[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);

    ChatContextBackendCommand command = ChatContextManager::backendCommand(
        commandId,
        messageObject,
        m_currentUserId,
        m_currentUserName,
        sessionId,
        senderId,
        senderName);

    if (commandId == QLatin1String("set-group-nickname")) {
        bool accepted = false;
        const QString nickname = promptTextValue(
            QStringLiteral("修改群昵称"),
            QStringLiteral("请输入 %1 在本群的昵称:").arg(senderName),
            QString(),
            &accepted,
            this);
        if (!accepted) return false;
        command.payload[QStringLiteral("nickname")] = nickname;
    }

    if (command.needsConfirmation) {
        const QMessageBox::StandardButton choice = QMessageBox::question(
            this,
            command.confirmTitle,
            command.confirmMessage,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (choice != QMessageBox::Yes) {
            ui->statusbar->showMessage(QStringLiteral("操作已取消"), 1600);
            return false;
        }
    }

    QJsonObject response;
    QString errorCode;
    QString errorMessage;
    if (!QQNTBackendService::handle(command.op, command.payload, &response, &errorCode, &errorMessage)) {
        ui->statusbar->showMessage(command.failureStatusMessage + QStringLiteral(" (%1)").arg(errorMessage), 3000);
        return false;
    }

    ui->statusbar->showMessage(command.successStatusMessage, 2200);

    if (commandId == QLatin1String("delete")) {
        m_chatModel->removeRow(index.row());
    } else if (commandId == QLatin1String("recall")) {
        QStandardItem* item = m_chatModel->item(index.row());
        if (item) {
            item->setText(item->text() + QStringLiteral(" [已撤回]"));
            item->setData(true, ChatBubbleSystemRole);
        }
    } else if (commandId == QLatin1String("essence") || commandId == QLatin1String("unessence")) {
        QStandardItem* item = m_chatModel->item(index.row());
        if (item) {
            const bool isEssence = (commandId == QLatin1String("essence"));
            item->setData(isEssence, ChatBubbleForwardedRole);
            item->setToolTip(isEssence ? QStringLiteral("精华消息") : QStringLiteral("已取消精华"));
        }
    }

    return true;
}


void MainWindow::onNewMessage(const Message& msg) {
    QString displayName = msg.senderName;
    const QString avatarUserId = msg.senderId == m_currentUserId ? m_currentUserId : msg.senderId;
    const bool isServerGroupMessage = !msg.receiverId.isEmpty()
        && (msg.receiverId == QLatin1String("public") || m_joinedServerSearchGroups.contains(msg.receiverId));
    const QString groupHistoryPeerId = isServerGroupMessage
        ? (msg.receiverId == QLatin1String("public")
            ? QStringLiteral("group")
            : m_joinedServerSearchGroups.value(msg.receiverId, msg.receiverId))
        : QString();
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
    } else if (msg.isPrivate() && !isServerGroupMessage) {
        line = QString("[%1] <%2> [私聊] %3").arg(timeStr, displayName, msg.content);
    } else {
        line = QString("[%1] <%2> %3").arg(timeStr, displayName, msg.content);
    }

    if (msg.isPrivate() && !isServerGroupMessage) {
        QString peerId = msg.senderId == m_currentUserId ? msg.receiverId : msg.senderId;
        if (!m_privateChatTarget.isEmpty() && peerId != m_privateChatTarget) {
            const QString encryptionState = msg.e2eEnvelope.isValid()
                ? (msg.content == QStringLiteral("加密消息无法解密")
                    ? QStringLiteral("decrypt-failed")
                    : QStringLiteral("encrypted"))
                : QStringLiteral("plaintext");
            const QJsonObject e2eStatus = msg.e2eEnvelope.isValid() && m_client
                ? m_client->e2eSessionStatus(peerId)
                : QJsonObject();
            const bool envelopeMatchesLocalSession = e2eStatus.value("keyId").toString() == msg.e2eEnvelope.keyId;
            saveHistory(peerId,
                        line,
                        encryptionState,
                        msg.e2eEnvelope.keyId,
                        envelopeMatchesLocalSession ? e2eStatus.value("keyFingerprintSha256").toString() : QString());
            ++m_unreadCount;
            updateUnreadState();
            ui->statusbar->showMessage(QString("新的私聊消息 · %1：%2").arg(displayName, msg.content.left(24)), 5000);
            if (m_trayIcon->isVisible()) {
                m_trayIcon->showMessage("新的私聊消息", QString("%1: %2").arg(displayName, msg.content), QSystemTrayIcon::Information, 3000);
            }
            return;
        }
    }

    if (msg.senderId == m_currentUserId) {
        return;
    }

    if ((msg.type == MessageType::Image || msg.type == MessageType::File) && handleReceivedTransferMessage(msg, displayName)) {
        const QString historyPeerId = isServerGroupMessage ? groupHistoryPeerId
            : (msg.isPrivate() ? (msg.senderId == m_currentUserId ? msg.receiverId : msg.senderId) : QStringLiteral("group"));
        const QString encryptionState = msg.e2eEnvelope.isValid()
            ? (msg.content == QStringLiteral("加密消息无法解密")
                ? QStringLiteral("decrypt-failed")
                : QStringLiteral("encrypted"))
            : QStringLiteral("plaintext");
        const QJsonObject e2eStatus = msg.e2eEnvelope.isValid() && m_client
            ? m_client->e2eSessionStatus(historyPeerId)
            : QJsonObject();
        const bool envelopeMatchesLocalSession = e2eStatus.value("keyId").toString() == msg.e2eEnvelope.keyId;
        saveHistory(historyPeerId,
                    line,
                    encryptionState,
                    msg.e2eEnvelope.keyId,
                    envelopeMatchesLocalSession ? e2eStatus.value("keyFingerprintSha256").toString() : QString());
        scrollActiveChatToBottom();
        return;
    }

    QString bubbleBody = msg.content;
    if (msg.type == MessageType::Image) {
        bubbleBody = QStringLiteral("[图片] %1").arg(msg.fileName);
    }
    QStandardItem* item = new QStandardItem(bubbleBody);
    item->setEditable(false);
    item->setData(bubbleTimestamp(timeStr), ChatBubbleTimestampRole);
    decorateChatItem(item, avatarUserId, displayName, msg.senderId == m_currentUserId);
    m_chatModel->appendRow(item);
    const QString historyPeerId = isServerGroupMessage ? groupHistoryPeerId
        : (msg.isPrivate() ? (msg.senderId == m_currentUserId ? msg.receiverId : msg.senderId) : QStringLiteral("group"));
    const QString encryptionState = msg.e2eEnvelope.isValid()
        ? (msg.content == QStringLiteral("加密消息无法解密")
            ? QStringLiteral("decrypt-failed")
            : QStringLiteral("encrypted"))
        : QStringLiteral("plaintext");
    const QJsonObject e2eStatus = msg.e2eEnvelope.isValid() && m_client
        ? m_client->e2eSessionStatus(historyPeerId)
        : QJsonObject();
    const bool envelopeMatchesLocalSession = e2eStatus.value("keyId").toString() == msg.e2eEnvelope.keyId;
    saveHistory(historyPeerId,
                line,
                encryptionState,
                msg.e2eEnvelope.keyId,
                envelopeMatchesLocalSession ? e2eStatus.value("keyFingerprintSha256").toString() : QString());

    int rowCount = m_chatModel->rowCount();
    if (rowCount > MAX_HISTORY_LINES) {
        m_chatModel->removeRows(0, rowCount - MAX_HISTORY_LINES);
    }

    const QString messageGroupId = isServerGroupMessage ? msg.receiverId : QString();
    const QJsonObject messageGroupUserSettings = m_serverGroupUserSettings.value(messageGroupId);
    const bool suppressGroupNotification = !messageGroupId.isEmpty()
        && (messageGroupUserSettings.value(QStringLiteral("muteNotifications")).toBool(false)
            || messageGroupUserSettings.value(QStringLiteral("receiveWithoutNotify")).toBool(false)
            || messageGroupUserSettings.value(QStringLiteral("receiveMode")).toString() == QLatin1String("receive_quiet")
            || messageGroupUserSettings.value(QStringLiteral("receiveMode")).toString() == QLatin1String("assistant_quiet"));
    if (!isActiveWindow() && !suppressGroupNotification) {
        ++m_unreadCount;
        updateUnreadState();
        if (m_trayIcon->isVisible()) {
            QString preview = msg.type == MessageType::File ? msg.content : msg.content.left(60);
            if (msg.type == MessageType::Image) preview = "[图片] " + msg.fileName;
            m_trayIcon->showMessage("QtNetworkChat", QString("%1: %2").arg(displayName, preview), QSystemTrayIcon::Information, 3000);
        }
    }

    scrollActiveChatToBottom();
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
        cachePeerAvatar(user);
    }
    refreshFriendList();
    if (!m_privateChatTarget.isEmpty()) {
        ui->chatHintLabel->setText(QString("QQ: %1 · %2 · 点击菜单“返回群聊”回到公共聊天室")
            .arg(m_privateChatTarget, isContactOnline(m_privateChatTarget) ? "在线" : "离线"));
    }
    ui->statusbar->showMessage(QString("在线: %1 人 | 好友: %2 人 | 当前账号: %3")
        .arg(knownOnlineUserCount())
        .arg(m_friendIds.size())
        .arg(m_currentUserId));
    refreshGroupMemberPanel();
    refreshSessionSummary();
}

void MainWindow::onServerGroupSnapshotReceived(const QJsonArray& groups) {
    const bool hadServerGroupSnapshot = m_hasServerGroupSnapshot;
    const bool wasInPublicGroup = m_wasInPublicServerGroup;
    m_hasServerGroupSnapshot = true;

    m_serverGroupNames.clear();
    m_serverGroupAnnouncements.clear();
    m_serverGroupOwners.clear();
    m_serverGroupMembers.clear();
    m_serverGroupMemberNames.clear();
    m_serverGroupMemberRoles.clear();
    m_serverGroupSettings.clear();
    m_serverGroupUserSettings.clear();
    m_serverGroupAuditEvents.clear();
    m_removedServerGroups.clear();

    for (const QJsonValue& value : groups) {
        const QJsonObject groupObj = value.toObject();
        const QString groupId = groupObj["groupId"].toString();
        if (groupId.isEmpty()) continue;

        m_serverGroupNames[groupId] = groupObj["groupName"].toString(groupId);
        m_serverGroupAnnouncements[groupId] = groupObj["announcement"].toString();
        m_serverGroupOwners[groupId] = groupObj["ownerId"].toString();
        m_serverGroupSettings[groupId] = QJsonObject{
            {QStringLiteral("avatar"), groupObj["avatar"].toString()},
            {QStringLiteral("allMuted"), groupObj["allMuted"].toBool(false)},
             {QStringLiteral("speakingRule"), groupObj["speakingRule"].toString(QStringLiteral("unrestricted"))},
             {QStringLiteral("joinPolicy"), groupObj["joinPolicy"].toString(QStringLiteral("approval"))},
             {QStringLiteral("searchable"), groupObj["searchable"].toBool(true)},
             {QStringLiteral("searchMode"), groupObj["searchMode"].toString(QStringLiteral("id_and_keyword"))}
        };
        m_serverGroupUserSettings[groupId] = groupObj["userSettings"].toObject();

        QStringList memberIds;
        const QJsonArray members = groupObj["members"].toArray();
        for (const QJsonValue& memberValue : members) {
            const QJsonObject memberObj = memberValue.toObject();
            const QString memberId = memberObj["userId"].toString();
            if (memberId.isEmpty() || memberIds.contains(memberId)) continue;

            memberIds << memberId;
            m_serverGroupMemberNames[groupId + "|" + memberId] = memberObj["userName"].toString(memberId);
            m_serverGroupMemberRoles[groupId + "|" + memberId] = memberObj["role"].toString("member");
        }
        m_serverGroupMembers[groupId] = memberIds;
        m_serverGroupAuditEvents[groupId] = groupObj["auditEvents"].toArray();
        if (groupId != QLatin1String("public") && memberIds.contains(m_currentUserId)
            && !m_joinedServerSearchGroups.contains(groupId)) {
            const QString localId = createLocalGroupSession(
                m_serverGroupNames.value(groupId, QStringLiteral("群聊")), memberIds,
                m_serverGroupAnnouncements.value(groupId));
            m_joinedServerSearchGroups.insert(groupId, localId);
        }
    }

    const QJsonArray removedGroups = m_client ? m_client->removedServerGroups() : QJsonArray();
    for (const QJsonValue& value : removedGroups) {
        const QJsonObject groupObj = value.toObject();
        const QString groupId = groupObj["groupId"].toString();
        if (groupId.isEmpty()) continue;

        m_removedServerGroups[groupId] = groupObj;
        m_serverGroupNames[groupId] = groupObj["groupName"].toString(groupId);
        m_serverGroupAnnouncements[groupId] = groupObj["announcement"].toString();
        m_serverGroupOwners[groupId] = groupObj["ownerId"].toString();
    }

    const bool isInPublicGroup = m_serverGroupMembers.value("public").contains(m_currentUserId);
    for (auto it = m_pendingGroupJoinApplications.begin(); it != m_pendingGroupJoinApplications.end(); ) {
        const QString groupId = it.value().value("groupId").toString();
        const QString role = m_serverGroupMemberRoles.value(groupId + "|" + m_currentUserId).toLower();
        const bool canReview = m_serverGroupOwners.value(groupId) == m_currentUserId
            || role == QLatin1String("owner") || role == QLatin1String("admin");
        if (!canReview) it = m_pendingGroupJoinApplications.erase(it);
        else ++it;
    }
    m_wasInPublicServerGroup = isInPublicGroup;

    if (m_privateChatTarget.isEmpty()) {
        if (isCurrentUserRemovedFromPublicGroup()) {
            const QJsonObject removedInfo = m_removedServerGroups.value("public");
            const QString removedBy = removedInfo["removedByName"].toString(removedInfo["removedBy"].toString());
            const QString removedAt = removedInfo["removedAt"].toString();
            const QString removedDetail = removedBy.isEmpty()
                ? QStringLiteral("可查看本机历史，等待群主或管理员重新邀请")
                : QStringLiteral("由 %1 移出%2 · 可查看本机历史，等待重新邀请")
                    .arg(removedBy, removedAt.isEmpty() ? QString() : QStringLiteral("于 %1").arg(removedAt));
            ui->chatTitleLabel->setText("公共聊天室");
            ui->chatHintLabel->setText(QString("当前账号 %1 已不在公共群 · %2").arg(m_currentUserId, removedDetail));
            ui->announcementTitleLabel->setText("群公告");
            ui->announcementBodyLabel->setText(QString("当前账号已不在公共群。%1；重新邀请后会自动恢复群公告和成员列表。").arg(removedDetail));
            if (!hadServerGroupSnapshot || wasInPublicGroup) {
                appendSystemMessage(QString("你已不在公共群，暂不能发送公共群消息、文件或图片；%1。").arg(removedDetail));
            }
            ui->statusbar->showMessage("当前账号已不在公共群，等待重新邀请", 3200);
        } else {
            const QString publicAnnouncement = m_serverGroupAnnouncements.value("public");
            const QString publicName = m_serverGroupNames.value("public", "公共聊天室");
            if (!publicName.isEmpty()) {
                ui->chatTitleLabel->setText(publicName);
            }
            ui->announcementTitleLabel->setText(canCurrentUserManageServerGroup("public")
                ? "群公告 <a href=\"edit\">编辑</a>"
                : "群公告");
            if (!publicAnnouncement.isEmpty()) {
                ui->announcementBodyLabel->setText(publicAnnouncement);
            }
            if (hadServerGroupSnapshot && !wasInPublicGroup) {
                appendSystemMessage("你已重新加入公共群，群公告、成员列表和发送入口已恢复。");
            }
            ui->statusbar->showMessage(QString("已同步服务端群组 · %1 个").arg(groups.size()), 1800);
        }
        refreshGroupMemberPanel();
        refreshComposerState();
    }
    refreshSessionSummary();
}

void MainWindow::onE2ESessionStateChanged(const QString& peerId, const QJsonObject& status) {
    if (peerId != m_privateChatTarget) {
        return;
    }
    const QString hint = ChatSessionManager::e2eSessionHint(contactDisplayName(peerId), status.value("state").toString());
    if (!hint.isEmpty()) {
        ui->chatHintLabel->setText(hint);
    }
}

void MainWindow::onE2EIdentityStateChanged(const QString& peerId, const QJsonObject& status) {
    const QString trustState = status.value("trustState").toString();
    const QString fingerprint = status.value("publicKeyFingerprintSha256").toString().left(16);
    if (trustState == QLatin1String("mismatch")) {
        appendSystemMessage(QString("%1 的端到端加密身份指纹发生变化 · 指纹:%2")
            .arg(contactDisplayName(peerId), fingerprint));
        ui->statusbar->showMessage("端到端加密身份指纹变化，请核对", 4200);
    } else if (trustState == QLatin1String("trusted")) {
        appendSystemMessage(QString("已信任 %1 的端到端加密身份 · 指纹:%2")
            .arg(contactDisplayName(peerId), fingerprint));
    } else if (peerId == m_privateChatTarget) {
        ui->chatHintLabel->setText(ChatSessionManager::e2eIdentityPendingHint(contactDisplayName(peerId), fingerprint));
    }
}

void MainWindow::onE2ESessionRotationRequested(const QString& peerId, const QJsonObject& agreement) {
    const QString keyId = agreement.value("keyId").toString();
    const QString fingerprint = agreement.value("publicKeyFingerprintSha256").toString().left(16);
    appendSystemMessage(QString("%1 请求轮换端到端加密会话 · keyId:%2 · 指纹:%3")
        .arg(contactDisplayName(peerId), keyId, fingerprint));
    if (peerId == m_privateChatTarget) {
        ui->chatHintLabel->setText(ChatSessionManager::e2eRotationRequestHint(contactDisplayName(peerId)));
    }
}

void MainWindow::onE2ESessionRotationResponded(const QString& peerId, const QJsonObject& agreement, bool accepted, const QString& reason) {
    const QString keyId = agreement.value("keyId").toString();
    appendSystemMessage(QString("%1 %2端到端加密轮换 · keyId:%3%4")
        .arg(contactDisplayName(peerId),
             accepted ? QStringLiteral("已接受") : QStringLiteral("已拒绝"),
             keyId,
             reason.trimmed().isEmpty() ? QString() : QStringLiteral(" · %1").arg(reason)));
    if (peerId == m_privateChatTarget) {
        ui->chatHintLabel->setText(ChatSessionManager::e2eRotationResponseHint(contactDisplayName(peerId), accepted));
    }
}

void MainWindow::onPrivateChat(const QModelIndex& index) {
    if (!index.isValid()) return;
    // Prefer the stable SessionIdRole (new session list); fall back to the legacy
    // Qt::UserRole+1 routing id used by the hidden user list and search/create items.
    QString targetId = index.data(SessionItemDelegate::SessionIdRole).toString();
    if (targetId.isEmpty() || targetId == QStringLiteral("__section__")) {
        targetId = index.data(Qt::UserRole + 1).toString();
    }
    if (targetId.isEmpty() || targetId == QStringLiteral("__section__")) return;
    if (targetId == QStringLiteral("__public__")) {
        onBackToGroupChat();
        return;
    }
    if (targetId.startsWith("search_add:")) {
        searchAndAddAccount(targetId.mid(QString("search_add:").size()), this);
        return;
    }
    if (targetId.startsWith("create_group:")) {
        QString groupName = targetId.mid(QString("create_group:").size()).trimmed();
        if (groupName.isEmpty()) groupName = "我的群聊";
        QString groupId = "local_group_" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
        m_localGroupIds << groupId;
        m_localGroupNames[groupId] = groupName;
        m_localGroupAnnouncements[groupId] = QString("%1 已创建，可继续邀请好友并发送消息。").arg(groupName);
        m_localGroupMembers[groupId] = QStringList{m_currentUserId};
        saveLocalGroups();
        m_contactFilter.clear();
        ui->contactSearchEdit->clear();
        refreshFriendList();
        switchToLocalGroup(groupId, groupName);
        appendSystemMessage("已从联系人搜索创建群聊: " + groupName);
        return;
    }
    if (m_localGroupIds.contains(targetId)) {
        switchToLocalGroup(targetId, m_localGroupNames.value(targetId, "群聊"));
        return;
    }
    QString userName = contactDisplayName(targetId);
    m_privateChatTarget = targetId;
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    loadHistory(targetId);
    const PrivateChatUiState privateState = ChatSessionManager::privateChatState(
        targetId,
        userName,
        isContactOnline(targetId),
        m_client && m_client->hasE2ESession(targetId),
        m_client && m_client->e2eSessionNeedsRotation(targetId));
    setWindowTitle(appWindowTitle(privateState.windowSuffix));
    ui->chatTitleLabel->setText(privateState.titleText);
    ui->chatHintLabel->setText(privateState.hintText);
    refreshComposerState();
    refreshSessionSummary();
}

void MainWindow::onClientDisconnected() {
    appendSystemMessage("已断开服务器连接");
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    const ConnectionUiState state = ChatSessionManager::disconnectedState(targetName, isLocalGroup);
    ui->chatHintLabel->setText(state.hintText);
    ui->statusbar->showMessage(state.statusMessage, 3500);
    refreshComposerState();
    refreshSessionSummary();
}

void MainWindow::onClientError(const QString& error) {
    appendSystemMessage("连接错误: " + error);
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    const ConnectionUiState state = ChatSessionManager::errorState(targetName, isLocalGroup, error);
    ui->chatHintLabel->setText(state.hintText);
    ui->statusbar->showMessage(state.statusMessage, 3500);
    refreshComposerState();
    refreshSessionSummary();
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
    ui->statusbar->showMessage(QString("QQ 号已复制: %1").arg(m_currentUserId), 2600);
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

void MainWindow::onAddFriend() {
    bool ok = false;
    QString account = QInputDialog::getText(this, "发送好友申请", "请输入对方 QQ 账号:", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok) return;
    searchAndAddAccount(account, this);
}

void MainWindow::searchAndAddAccount(const QString& account, QWidget* warningParent) {
    Q_UNUSED(warningParent)
    const QString normalizedAccount = account.trimmed();
    if (normalizedAccount.isEmpty()) {
        ui->statusbar->showMessage("请输入 QQ 号后再搜索", 1800);
        return;
    }
    if (normalizedAccount == m_currentUserId) {
        ui->statusbar->showMessage("不能添加自己为好友", 2500);
        return;
    }
    if (m_friendIds.contains(normalizedAccount)) {
        ui->statusbar->showMessage("该账号已经是你的好友: " + normalizedAccount, 2500);
        return;
    }
    if (!m_client->searchFriendByAccount(normalizedAccount)) {
        ui->statusbar->showMessage("当前未连接，无法搜索账号", 2500);
    } else {
        ui->statusbar->showMessage("正在搜索 QQ / 昵称: " + normalizedAccount, 2500);
    }
}

void MainWindow::onShowStorageManager() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("存储管理"));
    dialog.resize(660, 300);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* titleLabel = new QLabel(QStringLiteral("存储管理"), &dialog);
    titleLabel->setObjectName(QStringLiteral("storageManagerTitle"));
    QLabel* hintLabel = new QLabel(QStringLiteral("管理接收文件和聊天本地数据的保存位置。未设置时使用系统默认目录。"), &dialog);
    hintLabel->setObjectName(QStringLiteral("storageManagerHint"));
    hintLabel->setWordWrap(true);
    layout->addWidget(titleLabel);
    layout->addWidget(hintLabel);

    QWidget* panel = new QWidget(&dialog);
    panel->setObjectName(QStringLiteral("storageManagerPanel"));
    QVBoxLayout* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(16, 14, 16, 14);
    panelLayout->setSpacing(12);

    auto addStorageRow = [&](const QString& title,
                             const QString& description,
                             const std::function<QString()>& pathProvider,
                             const std::function<void(const QString&)>& pathSetter,
                             const std::function<void()>& resetter) {
        QWidget* row = new QWidget(panel);
        QHBoxLayout* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(12);

        QVBoxLayout* textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        QLabel* rowTitle = new QLabel(title, row);
        rowTitle->setObjectName(QStringLiteral("storageRowTitle"));
        QLabel* rowDescription = new QLabel(description, row);
        rowDescription->setObjectName(QStringLiteral("storageRowDescription"));
        rowDescription->setWordWrap(true);
        QLabel* pathLabel = new QLabel(pathProvider(), row);
        pathLabel->setObjectName(QStringLiteral("storagePathLabel"));
        pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        pathLabel->setWordWrap(true);
        textLayout->addWidget(rowTitle);
        textLayout->addWidget(rowDescription);
        textLayout->addWidget(pathLabel);

        QPushButton* changeButton = new QPushButton(QStringLiteral("更改存储路径"), row);
        QPushButton* resetButton = new QPushButton(QStringLiteral("恢复默认"), row);
        QVBoxLayout* buttonLayout = new QVBoxLayout();
        buttonLayout->setContentsMargins(0, 0, 0, 0);
        buttonLayout->addWidget(changeButton);
        buttonLayout->addWidget(resetButton);
        buttonLayout->addStretch();

        rowLayout->addLayout(textLayout, 1);
        rowLayout->addLayout(buttonLayout);
        panelLayout->addWidget(row);

        connect(changeButton, &QPushButton::clicked, this, [this, pathProvider, pathSetter, pathLabel, title]() {
            const QString selected = QFileDialog::getExistingDirectory(this,
                                                                       QStringLiteral("选择%1").arg(title),
                                                                       pathProvider());
            if (selected.isEmpty()) {
                ui->statusbar->showMessage(QStringLiteral("已取消更改存储路径"), 1600);
                return;
            }
            pathSetter(selected);
            pathLabel->setText(pathProvider());
            ui->statusbar->showMessage(QStringLiteral("%1已更新").arg(title), 2200);
        });
        connect(resetButton, &QPushButton::clicked, this, [this, resetter, pathProvider, pathLabel, title]() {
            resetter();
            pathLabel->setText(pathProvider());
            ui->statusbar->showMessage(QStringLiteral("%1已恢复默认").arg(title), 2200);
        });
    };

    addStorageRow(QStringLiteral("接收/下载文件保存到"),
                  QStringLiteral("图片、文件、视频接收后会保存到这里的分类子目录。"),
                  []() { return LocalFileManager::receivedDownloadRootDirectory(); },
                  [](const QString& path) { LocalFileManager::setReceivedDownloadRootDirectory(path); },
                  []() { LocalFileManager::resetReceivedDownloadRootDirectory(); });
    addStorageRow(QStringLiteral("聊天本地数据保存到"),
                  QStringLiteral("好友、群聊、头像缓存和本地资料文件会保存到这里。"),
                  []() { return ClientStorage::appDataRootDirectory(); },
                  [](const QString& path) { ClientStorage::setAppDataRootDirectory(path); },
                  []() { ClientStorage::resetAppDataRootDirectory(); });

    layout->addWidget(panel);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);


    dialog.exec();
}


void MainWindow::switchToLocalGroup(const QString& groupId, const QString& groupName) {
    m_privateChatTarget = groupId;
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    loadHistory(groupId);
    setWindowTitle(appWindowTitle(QString("群聊: %1").arg(groupName)));
    ui->chatTitleLabel->setText(groupName);
    const QString ownerId = groupOwnerId(groupId);
    const bool isOwner = isCurrentUserGroupOwner(groupId);
    ui->chatHintLabel->setText(ChatSessionManager::localGroupHint(groupId, ownerId, isOwner));
    ui->announcementTitleLabel->setText(isOwner ? "群公告 <a href=\"edit\">编辑</a>" : "群公告");
    ui->announcementBodyLabel->setText(m_localGroupAnnouncements.value(groupId, QString("%1 已创建，可继续邀请好友并发送消息。").arg(groupName)));
    refreshGroupMemberPanel();
    refreshComposerState();
    refreshSessionSummary();
    readLocalChatActions(groupId);
    if (m_messagesView) {
        m_messagesView->setEssenceButtonVisible(true);
        m_messagesView->setGroupMoreButtonVisible(true);
    }
    refreshGroupMemberSidebar();
}

void MainWindow::readLocalChatActions(const QString& sessionId) {
    if (!QQNTBackendService::isCommand(QStringLiteral("get_local_chat_actions"))) {
        return;
    }
    QJsonObject payload;
    payload[QStringLiteral("sessionId")] = sessionId;
    QJsonObject response;
    QString errorCode;
    QString errorMessage;
    if (!QQNTBackendService::handle(QStringLiteral("get_local_chat_actions"), payload, &response, &errorCode, &errorMessage)) {
        qDebug() << "get_local_chat_actions failed:" << errorCode << errorMessage;
        return;
    }
    // Backend currently returns historical records (deletedMessages, favoriteMessages, etc.)
    // rather than a simple action visibility map. If a future backend provides an 'actions'
    // array, use it to drive the context menu state directly.
    const QJsonArray actions = response.value(QStringLiteral("actions")).toArray();
    for (const QJsonValue& value : actions) {
        const QJsonObject action = value.toObject();
        const QString actionId = action.value(QStringLiteral("id")).toString();
        const bool enabled = action.value(QStringLiteral("enabled")).toBool(true);
        const bool visible = action.value(QStringLiteral("visible")).toBool(true);
        if (m_messagesView) {
            m_messagesView->setLocalActionState(actionId, visible, enabled);
        }
    }
}

void MainWindow::onEditGroupAnnouncement() {
    const QString serverGroupId = m_privateChatTarget.isEmpty()
        ? QStringLiteral("public")
        : m_joinedServerSearchGroups.key(m_privateChatTarget);
    const bool isServerGroup = !serverGroupId.isEmpty();
    const bool isLocalGroup = !isServerGroup && !m_privateChatTarget.isEmpty()
        && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    if (isLocalGroup && !isCurrentUserGroupOwner(m_privateChatTarget)) {
        ui->statusbar->showMessage("只有群主可以编辑群公告", 2400);
        appendSystemMessage("群公告编辑被权限保护拦截：当前账号不是群主");
        return;
    }
    if (isServerGroup && !canCurrentUserManageServerGroup(serverGroupId)) {
        ui->statusbar->showMessage("只有群主或管理员可以编辑群公告", 2400);
        appendSystemMessage("群公告编辑被服务端角色保护拦截");
        return;
    }

    const QString oldText = ui->announcementBodyLabel->text().trimmed();
    bool accepted = false;
    const QString inputText = promptMultilineValue(QStringLiteral("编辑群公告"),
                                                   QStringLiteral("群公告内容"), oldText,
                                                   &accepted, this);
    if (!accepted) {
        ui->statusbar->showMessage("已取消编辑群公告", 1600);
        return;
    }
    const GroupAnnouncementEditDecision announcementDecision =
        GroupManager::announcementEditDecision(oldText,
                                               inputText,
                                               isLocalGroup,
                                               ui->chatTitleLabel->text());
    if (!announcementDecision.changed) {
        ui->statusbar->showMessage(announcementDecision.unchangedStatusMessage, 1600);
        return;
    }
    if (isServerGroup) {
        if (!m_client || !m_client->sendServerGroupAnnouncementUpdate(serverGroupId, announcementDecision.text)) {
            ui->statusbar->showMessage("群公告提交失败，请检查连接状态", 2400);
            appendSystemMessage("群公告提交失败：客户端未连接或发送失败");
            return;
        }
        // Keep every visible group surface coherent while the server snapshot
        // is in flight. The authoritative value will still arrive by snapshot.
        m_serverGroupAnnouncements[serverGroupId] = announcementDecision.text;
        ui->announcementBodyLabel->setText(announcementDecision.text);
        refreshGroupMemberSidebar();
        refreshFriendList();
        appendSystemMessage("群公告更新已提交，等待服务端同步");
        ui->statusbar->showMessage(announcementDecision.submittedStatusMessage, 2200);
        return;
    }
    ui->announcementBodyLabel->setText(announcementDecision.text);
    if (isLocalGroup) {
        m_localGroupAnnouncements[m_privateChatTarget] = announcementDecision.text;
        saveLocalGroups();
        saveHistory(m_privateChatTarget,
                    QString("[%1] [系统] 群公告已更新: %2")
                        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"),
                             announcementDecision.text));
    }
    refreshGroupMemberSidebar();
    refreshFriendList();
    appendSystemMessage("群公告已更新");
    ui->statusbar->showMessage(announcementDecision.appliedStatusMessage, 2200);
}

void MainWindow::onUploadAvatar() {
    const LocalAvatarSelectionPlan selectionPlan = LocalFileManager::avatarSelectionPlan();
    const QString selectedPath = QFileDialog::getOpenFileName(this,
                                                              selectionPlan.dialogTitle,
                                                              LocalFileManager::lastAvatarDirectory(),
                                                              selectionPlan.filters);
    const LocalFileSelectionResult selection = LocalFileManager::selectAvatarFile(selectedPath);
    applyAvatarSelection(selection);
}

void MainWindow::onBackToGroupChat() {
    m_privateChatTarget.clear();
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    loadHistory("group");
    setWindowTitle(appWindowTitle(m_currentUserName));
    ui->chatTitleLabel->setText("公共聊天室");
    if (isCurrentUserRemovedFromPublicGroup()) {
        const QJsonObject removedInfo = m_removedServerGroups.value("public");
        const QString removedBy = removedInfo["removedByName"].toString(removedInfo["removedBy"].toString());
        const QString removedAt = removedInfo["removedAt"].toString();
        const QString removedDetail = removedBy.isEmpty()
            ? QStringLiteral("等待群主或管理员重新邀请")
            : QStringLiteral("由 %1 移出%2，等待重新邀请")
                .arg(removedBy, removedAt.isEmpty() ? QString() : QStringLiteral("于 %1").arg(removedAt));
        ui->chatHintLabel->setText(QString("当前账号 %1 已不在公共群 · %2").arg(m_currentUserId, removedDetail));
        ui->announcementTitleLabel->setText("群公告");
        ui->announcementBodyLabel->setText(QString("当前账号已不在公共群，%1。你仍可查看本机历史记录；重新邀请后会自动恢复群公告和成员列表。").arg(removedDetail));
    } else {
        ui->chatHintLabel->setText(QString("账号 %1 · 双击左侧成员可私聊").arg(m_currentUserId));
        ui->announcementTitleLabel->setText(canCurrentUserManageServerGroup("public")
            ? "群公告 <a href=\"edit\">编辑</a>"
            : "群公告");
        ui->announcementBodyLabel->setText(m_serverGroupAnnouncements.value(
            "public",
            "欢迎来到公共聊天室，支持 QQ 号搜索、好友、私聊和文件发送。"));
    }
    refreshGroupMemberPanel();
    refreshComposerState();
    refreshSessionSummary();
    if (m_messagesView) m_messagesView->setGroupMoreButtonVisible(true);
}

void MainWindow::onFriendRequestReceived(const QString& senderId, const QString& senderName) {
    if (senderId.isEmpty() || senderId == m_currentUserId) return;
    const QString displayName = senderName.isEmpty() ? senderId : senderName;
    m_friendNames[senderId] = displayName;
    m_pendingFriendRequests.removeAll(senderId);
    if (m_friendIds.contains(senderId)) {
        m_client->sendFriendResponse(senderId, true);
        appendSystemMessage(QString("%1 已是好友，已自动确认好友申请").arg(displayName));
        return;
    }
    m_pendingFriendRequests << senderId;
    saveFriends();
    ui->friendNoticeBtn->setText(QString("好友通知 %1").arg(m_pendingFriendRequests.size()));
    ui->friendNoticeBtn->setToolTip(QString("有 %1 个好友申请待处理").arg(m_pendingFriendRequests.size()));
    appendSystemMessage(QString("收到好友申请：%1（QQ:%2），请在好友通知中处理").arg(displayName, senderId));
    ui->statusbar->showMessage(QString("新的好友申请 · %1").arg(displayName), 3500);
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage("新的好友申请",
                                QString("%1（QQ:%2）请求加为好友").arg(displayName, senderId),
                                QSystemTrayIcon::Information,
                                3000);
    }
}

void MainWindow::onFriendSearchResult(const QString& account, const QString& userId, const QString& userName, bool found, bool online, bool exactMatch, int matchCount, const QString& matchReason) {
    // When the QQNT AddFriendDialog is open, it owns the search flow: show the
    // result in-dialog and let the user confirm, rather than the legacy path that
    // auto-sends a request on an exact online match. The user then clicks "加好友"
    // which emits addFriendRequested → sendFriendRequest.
    if (m_activeAddFriendDialog) {
        if (found && !userName.isEmpty()) {
            m_friendNames[userId] = userName;
        }
        m_activeAddFriendDialog->onSearchResult(account, userId, userName, found);
        return;
    }
    if (m_activeContactGroupSearchDialog) {
        m_activeContactGroupSearchDialog->setSearching(false);
        if (found && !userId.isEmpty()) {
            const QString displayName = userName.isEmpty() ? account : userName;
            m_friendNames[userId] = displayName;
            m_activeContactGroupSearchDialog->setContactKnown(userId, m_friendIds.contains(userId));
            m_activeContactGroupSearchDialog->addResult(QStringLiteral("contact"), userId, displayName,
                                                        QStringLiteral("QQ:%1 · %2").arg(account, online ? QStringLiteral("在线") : QStringLiteral("离线")),
                                                        peerAvatarPath(userId));
        } else {
            m_activeContactGroupSearchDialog->showSearchState(
                QStringLiteral("未找到 QQ:%1，请确认账号或昵称后重试").arg(account));
        }
        return;
    }
    if (!found) {
        ui->statusbar->showMessage(QString("没有找到 QQ 或昵称：%1").arg(account), 3000);
        appendSystemMessage(QString("没有找到 QQ 或昵称: %1，可尝试输入更完整的 QQ 号或昵称关键词").arg(account));
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
    if (m_pendingOutgoingFriendRequests.contains(userId)) {
        ui->statusbar->showMessage(QString("已向 QQ 账号 %1 发送过好友申请，等待对方处理").arg(userId), 3000);
        return;
    }

    const QString displayName = userName.isEmpty() ? userId : userName;
    const QString relation = m_friendIds.contains(userId)
        ? "好友"
        : (m_pendingOutgoingFriendRequests.contains(userId) ? "申请中" : "陌生人");
    const QString profileCard = QString("搜索资料卡\nQQ:%1\n昵称:%2\n状态:%3\n关系:%4\n匹配:%5 · 共%6个结果")
        .arg(userId,
             displayName,
             online ? "在线" : "离线",
             relation,
             matchReason.isEmpty() ? (exactMatch ? "QQ号精确匹配" : "模糊匹配") : matchReason,
             QString::number(matchCount));

    m_friendNames[userId] = displayName;
    QString compactProfileCard = profileCard;
    appendSystemMessage(compactProfileCard.replace('\n', " · "));

    if (!exactMatch) {
        if (!online) {
            ui->statusbar->showMessage(QString("模糊匹配到 %1（QQ:%2），但当前离线").arg(displayName, userId), 3500);
            appendSystemMessage(QString("模糊匹配到 %1（QQ:%2），对方离线，暂不能发送好友申请").arg(displayName, userId));
            return;
        }

        const QMessageBox::StandardButton choice = QMessageBox::question(
            this,
            "确认模糊匹配",
            QString("%1\n\n是否向该用户发送好友申请？").arg(profileCard),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (choice != QMessageBox::Yes) {
            ui->statusbar->showMessage(QString("已查看资料卡，未发送好友申请：%1").arg(displayName), 2600);
            return;
        }
    }

    if (online) {
        if (!m_client->sendFriendRequest(userId)) {
            appendSystemMessage(QString("好友申请发送失败 QQ:%1，请检查连接后重试").arg(userId));
            ui->statusbar->showMessage(QString("好友申请发送失败：%1").arg(displayName), 3000);
            return;
        }
        if (!m_pendingOutgoingFriendRequests.contains(userId)) {
            m_pendingOutgoingFriendRequests << userId;
        }
        saveFriends();
        appendSystemMessage(QString("已发送好友申请 QQ:%1，等待对方同意 · %2").arg(userId, exactMatch ? "精确匹配" : "模糊匹配确认"));
        ui->statusbar->showMessage(QString("好友申请已发送给 %1").arg(displayName), 2500);
        refreshFriendList();
    } else {
        ui->statusbar->showMessage(QString("QQ 账号 %1 当前离线，暂不能发送好友申请").arg(userId), 3000);
        appendSystemMessage(QString("QQ:%1 当前离线，未加入好友列表，可稍后重试").arg(userId));
    }
}

void MainWindow::onFriendRequestSent(const QString& receiverId, bool delivered) {
    QString userName = contactDisplayName(receiverId);
    if (delivered) {
        if (!m_pendingOutgoingFriendRequests.contains(receiverId)) {
            m_pendingOutgoingFriendRequests << receiverId;
        }
        appendSystemMessage(QString("好友申请已送达 QQ:%1，等待对方处理").arg(receiverId));
        ui->statusbar->showMessage(QString("好友申请已送达 %1").arg(userName), 2400);
    } else {
        m_pendingOutgoingFriendRequests.removeAll(receiverId);
        appendSystemMessage(QString("好友申请未送达 QQ:%1，对方当前离线").arg(receiverId));
        ui->statusbar->showMessage(QString("%1 当前离线，好友申请未送达").arg(userName), 3000);
    }
    saveFriends();
    refreshFriendList();
    refreshGroupMemberPanel();
}

void MainWindow::onFriendResponseReceived(const QString& senderId, const QString& senderName, bool accepted) {
    const QString displayName = senderName.isEmpty() ? contactDisplayName(senderId) : senderName;
    m_pendingOutgoingFriendRequests.removeAll(senderId);
    if (accepted) {
        if (!m_friendIds.contains(senderId)) {
            m_friendIds << senderId;
            m_friendNames[senderId] = displayName;
            QFile file(getFriendFilePath());
            if (file.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream out(&file);
                out << senderId << "|" << displayName << "\n";
            }
        }
        refreshFriendList();
        refreshGroupMemberPanel();
        m_pendingFriendRequests.removeAll(senderId);
        saveFriends();
        const FriendNoticeUiState noticeState = m_friendManager.noticeUiState(m_pendingFriendRequests.size());
        ui->friendNoticeBtn->setText(noticeState.text);
        ui->friendNoticeBtn->setToolTip(noticeState.toolTip);
        appendSystemMessage(displayName + " 已同意你的好友申请");
        ui->statusbar->showMessage(QString("%1 已同意好友申请").arg(displayName), 2800);
    } else {
        m_friendIds.removeAll(senderId);
        m_friendNames.remove(senderId);
        m_pendingFriendRequests.removeAll(senderId);
        saveFriends();
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->friendNoticeBtn->setText(m_pendingFriendRequests.isEmpty() ? "好友通知" : QString("好友通知 %1").arg(m_pendingFriendRequests.size()));
        ui->friendNoticeBtn->setToolTip(m_pendingFriendRequests.isEmpty() ? "查看并处理好友申请" : QString("有 %1 个好友申请待处理").arg(m_pendingFriendRequests.size()));
        appendSystemMessage(displayName + " 已拒绝你的好友申请");
        ui->statusbar->showMessage(QString("%1 已拒绝好友申请").arg(displayName), 2800);
    }
}



void MainWindow::appendMessage(const Message& msg) {
    onNewMessage(msg);
}

void MainWindow::appendSystemMessage(const QString& text) {
    QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss");
    QStandardItem* item = new QStandardItem(text);
    item->setEditable(false);
    item->setData(bubbleTimestamp(timeStr), ChatBubbleTimestampRole);
    decorateChatItem(item, QString(), QStringLiteral("系统"), false, true);
    m_chatModel->appendRow(item);
    scrollActiveChatToBottom();
}

void MainWindow::cachePeerAvatar(const ChatUser& user) {
    if (user.id.trimmed().isEmpty() || user.avatar.trimmed().isEmpty()) {
        return;
    }

    const QByteArray avatarBytes = QByteArray::fromBase64(user.avatar.toLatin1());
    if (avatarBytes.isEmpty()) {
        return;
    }

    if (!m_clientStorage.savePeerAvatar(user.id, avatarBytes)) {
        return;
    }

    if (ensureClientDatabase()) {
        m_clientStorage.savePeerAvatarToSqlite(clientDbPath(), user.id, m_clientStorage.peerAvatarFilePath(user.id));
    }
}

QString MainWindow::peerAvatarPath(const QString& userId) const {
    if (userId.trimmed().isEmpty()) {
        return QString();
    }

    const QString filePath = m_clientStorage.peerAvatarFilePath(userId);
    if (QFileInfo::exists(filePath)) {
        return filePath;
    }

    if (ensureClientDatabase()) {
        const QMap<QString, QString> peerIndex = m_clientStorage.loadPeerAvatarIndexFromSqlite(clientDbPath());
        const QString indexedPath = peerIndex.value(userId).trimmed();
        if (!indexedPath.isEmpty() && QFileInfo::exists(indexedPath)) {
            return indexedPath;
        }
    }
    return QString();
}

QString MainWindow::chatAvatarPath(const QString& userId) const {
    const QString trimmedUserId = userId.trimmed();
    if (trimmedUserId.isEmpty()) {
        return QString();
    }
    if (trimmedUserId == m_currentUserId) {
        const QString selfAvatarPath = getAvatarFilePath();
        if (QFileInfo::exists(selfAvatarPath)) {
            return selfAvatarPath;
        }
    }
    return peerAvatarPath(trimmedUserId);
}

QIcon MainWindow::peerAvatarIcon(const QString& userId, const QString& displayName) const {
    const QString avatarPath = chatAvatarPath(userId);
    if (!avatarPath.isEmpty()) {
        const QPixmap avatarPixmap(avatarPath);
        const QPixmap rounded = roundAvatarPixmap(avatarPixmap, 34);
        if (!rounded.isNull()) {
            return QIcon(rounded);
        }
    }
    return generatedPeerAvatarIcon(displayName, userId, 34);
}

void MainWindow::decorateChatItem(QStandardItem* item,
                                  const QString& senderId,
                                  const QString& senderName,
                                  bool outgoing,
                                  bool system) const {
    if (!item) {
        return;
    }

    item->setData(senderId, ChatBubbleSenderIdRole);
    item->setData(senderName, ChatBubbleSenderNameRole);
    item->setData(chatAvatarPath(senderId), ChatBubbleAvatarPathRole);
    item->setData(outgoing, ChatBubbleOutgoingRole);
    item->setData(system, ChatBubbleSystemRole);
    if (!system) {
        item->setIcon(peerAvatarIcon(senderId, senderName));
        // Populate the enhanced bubble roles consumed by ChatBubbleDelegate so the
        // visible message page renders timestamp and read status. Grouped state is
        // auto-computed by the delegate; forwarded/quoted are set by their own flows.
        if (!item->data(ChatBubbleTimestampRole).isValid()) {
            item->setData(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")),
                          ChatBubbleTimestampRole);
        }
    }
}

bool MainWindow::ensureClientDatabase() const {
    const QString connectionName = "client_history_init_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(clientDbPath());
        if (db.open()) {
            QSqlQuery query(db);
            ok = query.exec("CREATE TABLE IF NOT EXISTS friends ("
                            "user_id TEXT PRIMARY KEY, "
                            "display_name TEXT NOT NULL, "
                            "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS local_groups ("
                                "group_id TEXT PRIMARY KEY, "
                                "group_name TEXT NOT NULL, "
                                "members TEXT NOT NULL, "
                                "announcement TEXT, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS profile ("
                                "user_id TEXT PRIMARY KEY, "
                                "user_name TEXT NOT NULL, "
                                "avatar_path TEXT, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS friend_requests ("
                                "request_id TEXT NOT NULL, "
                                "display_name TEXT NOT NULL, "
                                "direction TEXT NOT NULL, "
                                "status TEXT NOT NULL, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "PRIMARY KEY(request_id, direction))");
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

QString MainWindow::clientDbPath() const {
    return m_historyService.databasePath();
}

bool MainWindow::saveProfileToSqlite() const {
    if (m_currentUserId.isEmpty() || !ensureClientDatabase()) return false;
    const QString avatarPath = QFileInfo::exists(getAvatarFilePath()) ? getAvatarFilePath() : QString();
    return m_clientStorage.saveProfileToSqlite(clientDbPath(), m_currentUserId, m_currentUserName, avatarPath);
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


void MainWindow::refreshSessionList() {
    if (!m_sessionModel) return;
    m_sessionModel->clear();

    // Ordering rules live in the pure SessionListBuilder helper so the sort
    // order and public-room placement can be unit tested without a MainWindow.
    QList<SessionListBuilder::LocalGroupInput> localGroups;
    localGroups.reserve(m_localGroupIds.size());
    for (const QString& groupId : m_localGroupIds) {
        SessionListBuilder::LocalGroupInput group;
        group.id = groupId;
        group.name = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
        group.announcement = m_localGroupAnnouncements.value(groupId, QStringLiteral("[本地群聊]"));
        group.pinned = QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"))
            .value(QStringLiteral("groupInfo/local/%1/pinned").arg(groupId), false).toBool();
        localGroups.append(group);
    }

    QList<SessionListBuilder::FriendInput> friends;
    friends.reserve(m_friendIds.size());
    for (const QString& friendId : m_friendIds) {
        SessionListBuilder::FriendInput friendInput;
        friendInput.id = friendId;
        friendInput.name = m_friendNames.value(friendId, friendId);
        friendInput.online = m_knownUsers.contains(friendId);
        friends.append(friendInput);
    }

    const QString publicRoomName = m_serverGroupNames.value(QStringLiteral("public"), QStringLiteral("公共聊天室"));
    const QString publicRoomLastMessage = isCurrentUserRemovedFromPublicGroup()
        ? QStringLiteral("当前账号已不在公共群")
        : QStringLiteral("公共聊天室 · 点击进入");

    const QList<SessionListBuilder::SessionEntry> entries =
        SessionListBuilder::build(publicRoomName, publicRoomLastMessage, localGroups, friends);

    for (const SessionListBuilder::SessionEntry& entry : entries) {
        QStandardItem* item = new QStandardItem(entry.name);
        item->setEditable(false);
        item->setData(entry.id, SessionItemDelegate::SessionIdRole);
        item->setData(entry.name, SessionItemDelegate::SessionNameRole);
        item->setData(entry.lastMessage, SessionItemDelegate::SessionLastMessageRole);
        item->setData(QDateTime::currentDateTime(), SessionItemDelegate::SessionLastMessageTimestampRole);
        item->setData(entry.unread, SessionItemDelegate::SessionUnreadRole);
        const QString pinKey = QStringLiteral("groupInfo/%1/%2/pinned")
            .arg(entry.id.startsWith(QStringLiteral("local_group_")) ? QStringLiteral("local") : QStringLiteral("server"), entry.id);
        const bool pinned = entry.pinned || (entry.isGroup && QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).value(pinKey, false).toBool());
        item->setData(pinned, SessionItemDelegate::SessionPinnedRole);
        item->setData(entry.atMention, SessionItemDelegate::SessionAtMentionRole);
        item->setData(entry.online, SessionItemDelegate::SessionOnlineRole);
        item->setData(entry.isGroup, SessionItemDelegate::SessionGroupRole);
        if (entry.isGroup) {
            QString avatarPath = m_localGroupAvatarPaths.value(entry.id);
            if (avatarPath.isEmpty()) {
                const QString candidate = QDir(ClientStorage::appDataRootDirectory())
                    .filePath(QStringLiteral("group_avatars/%1.png").arg(entry.id));
                if (QFileInfo::exists(candidate)) {
                    avatarPath = candidate;
                    m_localGroupAvatarPaths[entry.id] = candidate;
                }
            }
            if (!avatarPath.isEmpty()) item->setData(avatarPath, SessionItemDelegate::SessionAvatarPathRole);
        }
        // Avatar path: public room and local groups have no avatar file, friends may.
        if (!entry.isGroup) {
            const QString avatarPath = chatAvatarPath(entry.id);
            if (!avatarPath.isEmpty()) {
                item->setData(avatarPath, SessionItemDelegate::SessionAvatarPathRole);
            }
        }
        // Stable routing id for onPrivateChat (kept in sync with the legacy UserRole+1 path).
        item->setData(entry.id, Qt::UserRole + 1);
        m_sessionModel->appendRow(item);
    }
}

void MainWindow::scrollActiveChatToBottom() {
    // The new message page is the single visible chat surface; keep the legacy
    // hidden list in sync but drive scrolling through MessagesView.
    if (ui && ui->chatListView) {
        ui->chatListView->QListView::scrollToBottom();
    }
    if (m_messagesView) {
        m_messagesView->scrollChatToBottom();
    }
}

void MainWindow::refreshContactsAndProfile() {
    // Populate the QQNT ContactsView friend/group models with real local data,
    // independent of the legacy left session list (m_userListModel).
    if (m_contactsView) {
        QStandardItemModel* friendModel = m_contactsView->friendModel();
        QStandardItemModel* groupModel = m_contactsView->groupModel();
        QList<ContactDisplayData> friendContacts;
        QList<ContactDisplayData> groupContacts;
        const QString filter = m_contactsView->searchEdit()
            ? m_contactsView->searchEdit()->text().trimmed()
            : QString();

        auto matches = [&filter](const QString& id, const QString& name) {
            if (filter.isEmpty()) {
                return true;
            }
            return id.contains(filter, Qt::CaseInsensitive)
                || name.contains(filter, Qt::CaseInsensitive);
        };

        if (friendModel) {
            friendModel->clear();
            int friendRows = 0;
            for (const QString& friendId : m_friendIds) {
                const QString name = m_friendNames.value(friendId, friendId);
                if (!matches(friendId, name)) {
                    continue;
                }
                const bool online = m_knownUsers.contains(friendId);
                QStandardItem* item = new QStandardItem(
                    online ? QStringLiteral("%1（在线）").arg(name) : name);
                item->setEditable(false);
                item->setData(friendId, Qt::UserRole);
                item->setToolTip(QStringLiteral("QQ: %1").arg(friendId));
                friendModel->appendRow(item);
                ++friendRows;
                ContactDisplayData fd;
                fd.id = friendId;
                fd.nickname = name;
                fd.isOnline = online;
                fd.status = online ? QStringLiteral("在线") : QStringLiteral("离线");
                friendContacts.append(fd);
            }
            if (friendRows == 0) {
                QStandardItem* empty = new QStandardItem(
                    filter.isEmpty() ? QStringLiteral("暂无好友")
                                     : QStringLiteral("未找到匹配的好友"));
                empty->setEditable(false);
                empty->setEnabled(false);
                empty->setData(QString(), Qt::UserRole);
                friendModel->appendRow(empty);
            }
        }

        if (groupModel) {
            groupModel->clear();
            int groupRows = 0;
            for (const QString& groupId : m_localGroupIds) {
                const QString name = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
                if (!matches(groupId, name)) {
                    continue;
                }
                const int memberCount = m_localGroupMembers.value(groupId).size();
                QStandardItem* item = new QStandardItem(
                    memberCount > 0 ? QStringLiteral("%1（%2人）").arg(name).arg(memberCount) : name);
                item->setEditable(false);
                item->setData(groupId, Qt::UserRole);
                item->setToolTip(QStringLiteral("群号: %1").arg(groupId));
                groupModel->appendRow(item);
                ++groupRows;
                ContactDisplayData gd;
                gd.id = groupId;
                gd.nickname = name;
                gd.memberCount = memberCount;
                gd.isGroup = true;
                groupContacts.append(gd);
            }
            if (groupRows == 0) {
                QStandardItem* empty = new QStandardItem(
                    filter.isEmpty() ? QStringLiteral("暂无群聊")
                                     : QStringLiteral("未找到匹配的群聊"));
                empty->setEditable(false);
                empty->setEnabled(false);
                empty->setData(QString(), Qt::UserRole);
                groupModel->appendRow(empty);
            }
        }
        // Feed new list widget with the same data.
        m_contactsView->setFriends(friendContacts);
        m_contactsView->setGroups(groupContacts);
    }

    // Feed real statistics into the ProfileView.
    if (m_profileView) {
        m_profileView->setUserInfo(m_currentUserId, m_currentUserName);
        m_profileView->setStats(m_friendIds.size(),
                                m_localGroupIds.size(),
                                m_chatModel ? m_chatModel->rowCount() : 0);
    }

    // Keep the settings account info in sync with the current login.
    if (m_settingsView) {
        m_settingsView->setAccountInfo(m_currentUserName, m_currentUserId);
    }
}


void MainWindow::onContactSearchChanged(const QString& text) {
    m_contactFilter = text.trimmed();
    refreshFriendList();
    if (!m_contactFilter.isEmpty()) {
        ui->statusbar->showMessage(QString("QQ搜索:%1 · 无结果可双击搜索申请或建群").arg(m_contactFilter), 1800);
    } else {
        ui->statusbar->showMessage(QString("联系人已显示 · 好友%1 · 本地群%2 · 在线%3").arg(m_friendIds.size()).arg(m_localGroupIds.size()).arg(knownOnlineUserCount()), 1200);
    }
}

void MainWindow::loadAvatar() {
    const QString avatarPath = getAvatarFilePath();
    QPixmap pixmap(avatarPath);
    if (!pixmap.isNull()) {
        ui->avatarLabel->setPixmap(squareAvatarPixmap(pixmap, ui->avatarLabel->width()));
        QFile avatarFile(avatarPath);
        if (m_client && avatarFile.open(QIODevice::ReadOnly)) {
            const QByteArray avatarBytes = avatarFile.readAll();
            if (m_client->isConnected()) {
                m_client->sendAvatarUpdate(avatarBytes);
            } else {
                m_client->setAvatarData(avatarBytes);
            }
        }
        QFileInfo info(avatarPath);
        const QString avatarTip = QString("当前头像：本地头像 · %1；点击“换头像”重新选择")
                                      .arg(LocalFileManager::humanFileSize(info.size()));
        ui->avatarLabel->setToolTip(avatarTip);
        ui->uploadAvatarBtn->setToolTip(avatarTip);
    }
}

QString MainWindow::contactDisplayName(const QString& userId) const {
    return m_friendManager.contactDisplayName(userId, m_knownUsers, m_friendNames);
}

bool MainWindow::isContactOnline(const QString& userId) const {
    return m_friendManager.isContactOnline(userId, m_knownUsers);
}

int MainWindow::knownOnlineUserCount() const {
    int count = 0;
    for (auto it = m_knownUsers.constBegin(); it != m_knownUsers.constEnd(); ++it) {
        if (it.value().isOnline) {
            ++count;
        }
    }
    if (!m_currentUserId.isEmpty() && !isContactOnline(m_currentUserId)) {
        ++count;
    }
    return count;
}

bool MainWindow::isCurrentUserRemovedFromPublicGroup() const {
    return m_hasServerGroupSnapshot
        && !m_currentUserId.isEmpty()
        && m_removedServerGroups.contains("public")
        && !m_serverGroupMembers.value("public").contains(m_currentUserId);
}

QString MainWindow::groupOwnerId(const QString& groupId) const {
    return m_groupManager.localGroupOwnerId(m_localGroupMembers.value(groupId), m_currentUserId);
}

bool MainWindow::isCurrentUserGroupOwner(const QString& groupId) const {
    return m_groupManager.isLocalGroupOwner(groupId, m_localGroupMembers.value(groupId), m_currentUserId);
}

bool MainWindow::canCurrentUserManageServerGroup(const QString& groupId) const {
    return m_groupManager.canManageServerGroup(groupId, m_currentUserId, m_serverGroupOwners, m_serverGroupMemberRoles);
}

bool MainWindow::requestServerGroupMemberUpdate(const QString& memberId, const QString& action,
                                                const QString& requestedGroupId) {
    const QString groupId = requestedGroupId.isEmpty() ? QStringLiteral("public") : requestedGroupId;
    const QStringList members = m_serverGroupMembers.value(groupId);
    const ServerGroupMemberUpdateDecision decision = m_groupManager.serverGroupMemberUpdateDecision(
        memberId,
        action,
        m_client && m_client->isConnected(),
        groupId,
        m_currentUserId,
        members,
        m_serverGroupOwners,
        m_serverGroupMemberRoles);
    if (!decision.allowed) {
        ui->statusbar->showMessage(decision.statusMessage, decision.statusTimeoutMs);
        if (!decision.auditMessage.isEmpty()) {
            appendSystemMessage(decision.auditMessage);
        }
        return false;
    }
    if (!m_client->sendServerGroupMemberUpdate(groupId, decision.targetId, decision.normalizedAction)) {
        ui->statusbar->showMessage(QStringLiteral("群成员变更提交失败"), 2600);
        return false;
    }

    const QString displayName = m_serverGroupMemberNames.value(groupId + QStringLiteral("|") + decision.targetId,
                                                                contactDisplayName(decision.targetId));
    const QString actionText = decision.normalizedAction == "add"
        ? QStringLiteral("邀请")
        : (decision.normalizedAction == "remove"
            ? QStringLiteral("移出")
            : (decision.normalizedAction == "promote_admin" ? QStringLiteral("设置管理员") : QStringLiteral("取消管理员")));
    appendSystemMessage(QString("已提交群%1成员请求：%2（QQ:%3），等待服务端同步").arg(actionText, displayName, decision.targetId));
    ui->statusbar->showMessage(QString("群%1请求已提交，等待服务端同步").arg(actionText), 2400);
    return true;
}

QString MainWindow::e2eSessionStatusText(const QString& peerId) const {
    if (!m_client) {
        return QStringLiteral("端到端加密状态：客户端未就绪");
    }
    const QJsonObject status = m_client->e2eSessionStatus(peerId);
    const QJsonObject identity = m_client->e2ePeerIdentityStatus(peerId);
    const QString identityLine = identity.value("configured").toBool(false)
        ? QString("\n身份信任：%1\n跨设备验证：%2\n验证短码：%3\n身份指纹：%4\n信任持久化：%5")
            .arg(identity.value("trustState").toString(),
                 identity.value("verified").toBool(false) ? QStringLiteral("verified") : QStringLiteral("unverified"),
                 identity.value("verificationCodeDisplay").toString(),
                 identity.value("publicKeyFingerprintSha256").toString().left(16),
                 identity.value("pinPersisted").toBool(false) ? QStringLiteral("yes") : QStringLiteral("no"))
        : QStringLiteral("\n身份信任：unknown\n身份指纹：未收到");
    const QString state = status.value("state").toString();
    if (state == QLatin1String("ready")) {
        return QString("端到端加密状态：已就绪\n对端QQ：%1%7\nkeyId：%2\n指纹：%3\n已加密发送：%4\n已解密接收：%5\n轮换阈值：%6")
            .arg(peerId,
                 status.value("keyId").toString(),
                 status.value("keyFingerprintSha256").toString().left(16),
                 status.value("encryptedMessages").toString(),
                 status.value("decryptedMessages").toString(),
                 QString::number(status.value("messageLimit").toInt()),
                 identityLine);
    }
    if (state == QLatin1String("rotation-required")) {
        return QString("端到端加密状态：需要轮换\n对端QQ：%1%6\nkeyId：%2\n指纹：%3\n已加密发送：%4\n轮换阈值：%5")
            .arg(peerId,
                 status.value("keyId").toString(),
                 status.value("keyFingerprintSha256").toString().left(16),
                 status.value("encryptedMessages").toString(),
                 QString::number(status.value("messageLimit").toInt()),
                 identityLine);
    }
    return QString("端到端加密状态：未就绪\n对端QQ：%1%2\n说明：当前本机没有可用于该联系人的会话密钥").arg(peerId, identityLine);
}

void MainWindow::copyE2ESessionStatus(const QString& peerId) {
    const QString text = e2eSessionStatusText(peerId);
    QApplication::clipboard()->setText(text);
    ui->statusbar->showMessage("端到端加密状态已复制", 2200);
}

QString MainWindow::getFriendFilePath() const {
    return m_clientStorage.friendFilePath();
}

QString MainWindow::getGroupFilePath() const {
    return m_clientStorage.groupFilePath();
}

QString MainWindow::getAvatarFilePath() const {
    return m_clientStorage.avatarFilePath();
}

void MainWindow::saveFriends() const {
    if (ensureClientDatabase()) {
        m_clientStorage.saveFriendsToSqlite(clientDbPath(),
                                            m_friendIds,
                                            m_friendNames,
                                            m_pendingFriendRequests,
                                            m_pendingOutgoingFriendRequests);
    }
    m_clientStorage.writeLegacyFriends(m_friendIds, m_friendNames);
}

void MainWindow::saveFriendGroups() const {
    if (ensureClientDatabase()) {
        m_clientStorage.saveFriendGroupsToSqlite(clientDbPath(), m_friendGroups, m_customGroups);
    }
    m_clientStorage.writeFriendGroups(m_friendGroups, m_customGroups);
}

void MainWindow::saveLocalGroups() const {
    if (ensureClientDatabase()) {
        m_clientStorage.saveLocalGroupsToSqlite(clientDbPath(),
                                                m_currentUserId,
                                                m_localGroupIds,
                                                m_localGroupNames,
                                                m_localGroupMembers,
                                                m_localGroupAnnouncements);
    }
    m_clientStorage.writeLegacyLocalGroups(m_currentUserId,
                                           m_localGroupIds,
                                           m_localGroupNames,
                                           m_localGroupMembers,
                                           m_localGroupAnnouncements);
}

void MainWindow::updateUnreadState() {
    const WindowChromeState state = WindowStateManager::unreadState(
        QString::fromLatin1(QTNETWORKCHAT_VERSION_STRING),
        m_unreadCount);
    setWindowTitle(state.windowTitle);
    m_trayIcon->setToolTip(state.trayToolTip);
}

void MainWindow::clearUnreadState() {
    m_unreadCount = 0;
    const WindowChromeState state = WindowStateManager::clearedState(
        QString::fromLatin1(QTNETWORKCHAT_VERSION_STRING),
        !m_privateChatTarget.isEmpty(),
        m_currentUserName,
        ui->chatTitleLabel->text());
    setWindowTitle(state.windowTitle);
    m_trayIcon->setToolTip(state.trayToolTip);
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
