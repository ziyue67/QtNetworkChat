#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "chatbubbledelegate.h"
#include "sessionitemdelegate.h"
#include "groupmemberitemdelegate.h"
#include "iconhelper.h"
#include <QFile>
#include <QTextStream>
#include <QApplication>
#include "chatsessionmanager.h"
#include "chatcontextmanager.h"
#include "composermanager.h"
#include "filetransferstatus.h"
#include "localfilemanager.h"
#include "notificationpanelmanager.h"
#include "qtnetworkchat_version.h"
#include "transferchatitemrenderer.h"
#include "windowstatemanager.h"
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
#include <QButtonGroup>
#include <QAbstractButton>
#include <QListView>
#include <QStatusBar>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QPropertyAnimation>
#include <QLinearGradient>
#include <QPolygonF>
#include <QRegularExpression>
#include <QKeyEvent>
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
#include <QSettings>
#include <QVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressDialog>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QCryptographicHash>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QBuffer>
#include <QScrollArea>
#include <QStyle>
#include <QUuid>
#include <functional>

namespace {
enum ChatVisualRole {
    ChatSenderIdRole = Qt::UserRole + 900,
    ChatSenderNameRole,
    ChatAvatarPathRole,
    ChatOutgoingRole,
    ChatSystemRole,
    ChatMediaKindRole,
    ChatMediaPreviewRole,
    ChatMediaOpenPathRole,
    ChatMessageIdRole,
    ChatSessionIdRole,
    ChatFavoritedRole,
    ChatEssenceRole,
    ChatRecalledRole
};

QPixmap roundAvatarPixmap(const QPixmap& source, int side);
QIcon generatedPeerAvatarIcon(const QString& displayName, const QString& seedId, int side);

void applyTransferActionState(QAction* action, const TransferActionUiState& state) {
    if (!action) return;
    action->setVisible(state.visible);
    action->setEnabled(state.enabled);
    action->setToolTip(state.toolTip);
}

QIcon createChatIcon(const QString& seedText = QString()) {
    QIcon icon;
    const int sizes[] = {16, 24, 32, 48, 64, 128};
    for (int size : sizes) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QLinearGradient gradient(0, 0, size, size);
        gradient.setColorAt(0.0, QColor("#2BD8C5"));
        gradient.setColorAt(0.55, QColor("#17A8F3"));
        gradient.setColorAt(1.0, QColor("#6C5CE7"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRoundedRect(QRectF(size * 0.08, size * 0.10, size * 0.84, size * 0.72), size * 0.24, size * 0.24);

        QPolygonF tail;
        tail << QPointF(size * 0.36, size * 0.78)
             << QPointF(size * 0.29, size * 0.94)
             << QPointF(size * 0.54, size * 0.80);
        painter.drawPolygon(tail);

        painter.setBrush(QColor(255, 255, 255, 235));
        const qreal dot = qMax(2.0, size * 0.10);
        painter.drawEllipse(QPointF(size * 0.36, size * 0.45), dot, dot);
        painter.drawEllipse(QPointF(size * 0.50, size * 0.45), dot, dot);
        painter.drawEllipse(QPointF(size * 0.64, size * 0.45), dot, dot);

        if (size >= 48 && !seedText.trimmed().isEmpty()) {
            QFont font = painter.font();
            font.setFamily("Microsoft YaHei");
            font.setBold(true);
            font.setPixelSize(static_cast<int>(size * 0.28));
            painter.setFont(font);
            painter.setPen(QColor(255, 255, 255, 245));
            painter.drawText(QRectF(size * 0.08, size * 0.12, size * 0.84, size * 0.54),
                             Qt::AlignCenter,
                             seedText.left(1).toUpper());
        }

        icon.addPixmap(pixmap);
    }
    return icon;
}

QString selectedFriendManagerEntryId(QListWidget* friendList) {
    if (!friendList || !friendList->currentItem()) return QString();
    return friendList->currentItem()->data(Qt::UserRole).toString();
}

bool trySelectedValidFriendId(QListWidget* friendList,
                              QStatusBar* statusBar,
                              const QString& emptyMessage,
                              QString* friendId) {
    const QString selectedId = selectedFriendManagerEntryId(friendList);
    if (selectedId.isEmpty()) {
        if (statusBar) statusBar->showMessage(emptyMessage, 1800);
        return false;
    }
    if (selectedId.startsWith("search_add:")) {
        if (statusBar) statusBar->showMessage("请先选择有效好友，或点击搜索申请", 2200);
        return false;
    }
    if (friendId) {
        *friendId = selectedId;
    }
    return true;
}

QString selectedFriendManagerTargetId(QListWidget* friendList) {
    QString selectedId = selectedFriendManagerEntryId(friendList);
    if (selectedId.startsWith("search_add:")) {
        selectedId = selectedId.mid(QString("search_add:").size());
    }
    return selectedId;
}

QStringList visibleFriendManagerIds(QListWidget* friendList) {
    QStringList ids;
    if (!friendList) return ids;
    for (int i = 0; i < friendList->count(); ++i) {
        QListWidgetItem* item = friendList->item(i);
        const QString id = item ? item->data(Qt::UserRole).toString() : QString();
        if (id.isEmpty() || id.startsWith("search_add:") || ids.contains(id)) continue;
        ids << id;
    }
    return ids;
}

QString selectedFriendNoticeEntryId(QListWidget* noticeList) {
    if (!noticeList || !noticeList->currentItem()) return QString();
    return noticeList->currentItem()->data(Qt::UserRole).toString();
}

QString selectedFriendNoticeTargetId(QListWidget* noticeList, QLineEdit* searchEdit = nullptr) {
    return NotificationPanelManager::friendNoticeTargetId(
        selectedFriendNoticeEntryId(noticeList),
        searchEdit ? searchEdit->text() : QString());
}

bool trySelectedRealFriendNoticeId(QListWidget* noticeList,
                                   QStatusBar* statusBar,
                                   const QString& emptyMessage,
                                   const QString& searchAddMessage,
                                   QString* requestId) {
    const QString currentId = selectedFriendNoticeEntryId(noticeList);
    if (currentId.isEmpty()) {
        if (statusBar) statusBar->showMessage(emptyMessage, 1800);
        return false;
    }
    if (!NotificationPanelManager::isRealFriendNoticeRequestId(currentId)) {
        if (statusBar) statusBar->showMessage(searchAddMessage, 2200);
        return false;
    }
    if (requestId) {
        *requestId = currentId;
    }
    return true;
}

QStringList visibleFriendNoticeIds(QListWidget* noticeList) {
    QStringList entryIds;
    if (!noticeList) return entryIds;
    for (int i = 0; i < noticeList->count(); ++i) {
        QListWidgetItem* item = noticeList->item(i);
        const QString id = item ? item->data(Qt::UserRole).toString() : QString();
        if (id.isEmpty()) continue;
        entryIds << id;
    }
    return NotificationPanelManager::visibleFriendNoticeTargetIds(entryIds);
}

QString selectedGroupNoticeEntryId(QListWidget* noticeList) {
    if (!noticeList || !noticeList->currentItem()) return QString();
    return noticeList->currentItem()->data(Qt::UserRole).toString();
}

bool isGroupCreateEntryId(const QString& groupId) {
    return NotificationPanelManager::isGroupCreateEntryId(groupId);
}

QString groupCreateEntryName(const QString& groupId) {
    return NotificationPanelManager::groupCreateEntryName(groupId);
}

bool trySelectedInspectableGroupNoticeId(QListWidget* noticeList,
                                         QStatusBar* statusBar,
                                         const QString& emptyMessage,
                                         const QString& createMessage,
                                         QString* groupId) {
    if (!noticeList || !noticeList->currentItem()) {
        if (statusBar) statusBar->showMessage(emptyMessage, 1800);
        return false;
    }
    const QString currentId = selectedGroupNoticeEntryId(noticeList);
    if (!NotificationPanelManager::isInspectableGroupNoticeId(currentId)) {
        if (statusBar) statusBar->showMessage(createMessage, 2200);
        return false;
    }
    if (groupId) {
        *groupId = currentId;
    }
    return true;
}

QStringList visibleGroupNoticeIds(QListWidget* noticeList) {
    QStringList entryIds;
    if (!noticeList) return entryIds;
    for (int i = 0; i < noticeList->count(); ++i) {
        QListWidgetItem* item = noticeList->item(i);
        const QString id = item ? item->data(Qt::UserRole).toString() : QString();
        entryIds << id;
    }
    return NotificationPanelManager::uniqueGroupNoticeEntryIds(entryIds);
}

QString serverGroupAuditActionText(const QString& action) {
    const QString normalized = action.trimmed().toLower();
    if (normalized == QLatin1String("announcement_update")) return QStringLiteral("更新公告");
    if (normalized == QLatin1String("add")) return QStringLiteral("加入成员");
    if (normalized == QLatin1String("remove")) return QStringLiteral("移出成员");
    if (normalized == QLatin1String("promote_admin")) return QStringLiteral("设为管理员");
    if (normalized == QLatin1String("demote_admin")) return QStringLiteral("取消管理员");
    return normalized.isEmpty() ? QStringLiteral("群操作") : normalized;
}

QString serverGroupAuditSummary(const QJsonObject& event) {
    const QString action = serverGroupAuditActionText(event.value("action").toString());
    const QString actorName = event.value("actorName").toString().trimmed();
    const QString actorId = event.value("actorId").toString().trimmed();
    const QString targetName = event.value("targetUserName").toString().trimmed();
    const QString targetId = event.value("targetUserId").toString().trimmed();
    const QString createdAt = event.value("createdAt").toString().trimmed();
    const QString actor = actorName.isEmpty() ? actorId : QString("%1(%2)").arg(actorName, actorId);
    const QString target = targetId.isEmpty()
        ? QString()
        : (targetName.isEmpty() ? targetId : QString("%1(%2)").arg(targetName, targetId));
    const QString timePart = createdAt.isEmpty() ? QString() : QString(" · %1").arg(createdAt);
    return target.isEmpty()
        ? QString("%1 · %2%3").arg(action, actor, timePart)
        : QString("%1 · %2 -> %3%4").arg(action, actor, target, timePart);
}

QString transferIntegritySummary(const Message& msg) {
    const bool hasExpectedSize = msg.fileSize > 0;
    const bool hasExpectedHash = !msg.fileHash.trimmed().isEmpty();
    if (!hasExpectedSize && !hasExpectedHash) {
        return "未提供完整性校验";
    }

    const bool sizeOk = !hasExpectedSize || msg.fileSize == msg.fileData.size();
    bool hashOk = true;
    if (hasExpectedHash) {
        const QString actualHash = QString::fromLatin1(QCryptographicHash::hash(msg.fileData, QCryptographicHash::Sha256).toHex());
        hashOk = actualHash.compare(msg.fileHash.trimmed(), Qt::CaseInsensitive) == 0;
    }

    if (sizeOk && hashOk) {
        return "完整性已验证";
    }

    QStringList issues;
    if (!sizeOk) issues << "大小不一致";
    if (!hashOk) issues << "哈希不一致";
    return "完整性校验失败：" + issues.join("、");
}

QPixmap squareAvatarPixmap(const QPixmap& source, int side) {
    if (source.isNull() || side <= 0) return QPixmap();
    QPixmap scaled = source.scaled(side, side, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = qMax(0, (scaled.width() - side) / 2);
    const int y = qMax(0, (scaled.height() - side) / 2);
    return scaled.copy(x, y, side, side);
}

QPixmap roundAvatarPixmap(const QPixmap& source, int side) {
    const QPixmap square = squareAvatarPixmap(source, side);
    if (square.isNull() || side <= 0) {
        return QPixmap();
    }

    QPixmap rounded(side, side);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addEllipse(0, 0, side, side);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, square);
    return rounded;
}

QIcon generatedPeerAvatarIcon(const QString& displayName, const QString& seedId, int side = 36) {
    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QByteArray hash = QCryptographicHash::hash((seedId + "|" + displayName).toUtf8(), QCryptographicHash::Sha1);
    const int hue = hash.isEmpty() ? 210 : static_cast<unsigned char>(hash.at(0)) % 360;
    QColor start = QColor::fromHsv(hue, 120, 220);
    QColor end = QColor::fromHsv((hue + 28) % 360, 150, 200);
    QLinearGradient gradient(0, 0, side, side);
    gradient.setColorAt(0.0, start);
    gradient.setColorAt(1.0, end);
    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(0, 0, side, side);

    QFont font = painter.font();
    font.setFamily(QStringLiteral("Microsoft YaHei"));
    font.setBold(true);
    font.setPixelSize(qMax(14, side / 2));
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 245));
    painter.drawText(QRect(0, 0, side, side), Qt::AlignCenter, displayName.trimmed().isEmpty() ? QStringLiteral("?") : displayName.left(1).toUpper());
    return QIcon(pixmap);
}

QString compactMessageId(const QString& seed) {
    const QByteArray hash = QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString::fromLatin1(hash.left(16));
}

QString appWindowTitle(const QString& suffix = QString()) {
    return WindowStateManager::appWindowTitle(QString::fromLatin1(QTNETWORKCHAT_VERSION_STRING), suffix);
}
}

MainWindow::MainWindow(Client* client, const QString& userId, const QString& userName, QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_client(client)
    , m_clientStorage(userName)
    , m_userListModel(new QStandardItemModel(this))
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
    , m_navGroup(nullptr)
{
        ui->setupUi(this);

    // Fade-in Window Animation Effect
    this->setWindowOpacity(0.0);
    QPropertyAnimation* fadeAnim = new QPropertyAnimation(this, "windowOpacity");
    fadeAnim->setDuration(400); // 400ms fade
    fadeAnim->setStartValue(0.0);
    fadeAnim->setEndValue(1.0);
    fadeAnim->setEasingCurve(QEasingCurve::InOutQuad);
    fadeAnim->start(QAbstractAnimation::DeleteWhenStopped);

    // Load modern stylesheet
    QFile styleFile("ui/style.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        QTextStream textStream(&styleFile);
        QString styleSheet = textStream.readAll();
        this->setStyleSheet(styleSheet);
    }
    setProperty("theme", "light");
    loadTheme();
    setMinimumSize(980, 680);
    setWindowIcon(createChatIcon(userName));
    setupUi();
    setupTray();
    setupNavPanel();

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
    ui->chatTitleLabel->setText("公共聊天室");
    ui->chatStatusLabel->setText("Redis 在线工作台 · 多实例状态同步中");
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
    connect(m_client, &Client::serverGroupEssenceUpdated, this, &MainWindow::handleServerGroupEssenceUpdated);
    connect(m_client, &Client::messageFavoriteUpdated, this, &MainWindow::handleMessageFavoriteUpdated);
    connect(m_client, &Client::favoriteMessagesSnapshotReceived, this, &MainWindow::handleFavoriteMessagesSnapshotReceived);
    connect(m_client, &Client::serverGroupMessageRecalled, this, &MainWindow::handleServerGroupMessageRecalled);
    connect(m_client, &Client::serverGroupMemberMuted, this, &MainWindow::handleServerGroupMemberMuted);
    connect(m_client, &Client::serverGroupMemberUnmuted, this, &MainWindow::handleServerGroupMemberUnmuted);
    connect(m_client, &Client::serverGroupMemberProfileReceived, this, &MainWindow::handleServerGroupMemberProfileReceived);
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
    ui->uploadAvatarBtn->setToolTip("更换当前头像");
    if (ui->groupMemberListView) {
        ui->groupMemberListView->setProperty("currentUserId", m_currentUserId);
    }
    saveProfileToSqlite();
    if (m_client->hasServerGroupSnapshot()) {
        onServerGroupSnapshotReceived(m_client->serverGroups());
    }

    if (m_currentUserId.isEmpty()) {
        ui->statusbar->showMessage("已连接");
    } else {
        ui->statusbar->showMessage("已连接 - 用户ID: " + m_currentUserId);
    }
    refreshWorkspaceChrome();
    loadHistory("group");
    updateSavedOutgoingTransferRecoveryUi(true);
}

MainWindow::~MainWindow() {
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
}

bool MainWindow::sendTransferWithProgress(const QString& filePath,
                                          const QString& receiverId,
                                          const QString& targetName,
                                          const QString& kind,
                                          bool asImage,
                                          QString* transferSummary,
                                          bool* canceled) {
    if (!m_client) return false;

    const QFileInfo info(filePath);
    constexpr int maxAttempts = 3;
    if (transferSummary) transferSummary->clear();
    if (canceled) *canceled = false;

    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        QString preparedSummary;
        bool cancelRequested = false;
        QProgressDialog progress(this);
        progress.setWindowTitle(QString("发送%1").arg(kind));
        progress.setLabelText(m_transferManager.sendingInitialState(kind, info.fileName(), targetName).labelText);
        progress.setRange(0, 100);
        progress.setValue(0);
        progress.setMinimumDuration(0);
        progress.setAutoClose(false);
        progress.setAutoReset(false);
        progress.setCancelButtonText("取消发送");
        progress.show();
        QApplication::processEvents();
        setTransferWorkspaceState(m_transferManager.preparingSendWorkspaceState(kind,
                                                                                info.fileName(),
                                                                                LocalFileManager::humanFileSize(info.size()),
                                                                                targetName));

        QMetaObject::Connection cancelConnection = connect(
            &progress,
            &QProgressDialog::canceled,
            this,
            [this, &progress, &info, &kind, &cancelRequested, canceled, &targetName]() {
                cancelRequested = true;
                if (canceled) *canceled = true;
                progress.setLabelText(m_transferManager.sendingCancelState(kind, info.fileName()).labelText);
                if (m_client) m_client->cancelCurrentOutgoingTransfer();
                setTransferWorkspaceState(m_transferManager.canceledSendWorkspaceState(kind, info.fileName(), targetName));
                ui->statusbar->showMessage(QString("正在取消发送%1：%2").arg(kind, info.fileName()), 1600);
                QApplication::processEvents();
            });
        QMetaObject::Connection progressConnection = connect(
            m_client,
            &Client::fileTransferProgress,
            this,
            [this, &progress, &info, &targetName, &kind](const QString& fileName, qint64 bytesPrepared, qint64 totalBytes) {
                if (fileName != info.fileName()) return;
                const TransferProgressUiState state = m_transferManager.sendingProgressState(kind, fileName, targetName, bytesPrepared, totalBytes);
                progress.setValue(state.percent);
                progress.setLabelText(state.labelText);
                setTransferWorkspaceState(m_transferManager.sendingProgressWorkspaceState(kind,
                                                                                          fileName,
                                                                                          targetName,
                                                                                          bytesPrepared,
                                                                                          totalBytes));
                QApplication::processEvents();
            });
        QMetaObject::Connection preparedConnection = connect(
            m_client,
            &Client::fileTransferPrepared,
            this,
            [this, &progress, &info, &targetName, &kind, &preparedSummary](const QString& fileName,
                                                                      qint64 totalBytes,
                                                                      qint64 chunkSize,
                                                                      qint64 chunkCount,
                                                                      const QString& fileHash) {
                if (fileName != info.fileName()) return;
                const TransferProgressUiState state = m_transferManager.sendingPreparedState(kind, fileName, targetName, totalBytes, chunkSize, chunkCount, fileHash);
                preparedSummary = state.manifestSummary;
                progress.setLabelText(state.labelText);
                setTransferWorkspaceState(m_transferManager.sendingPreparedWorkspaceState(kind,
                                                                                          fileName,
                                                                                          targetName,
                                                                                          totalBytes,
                                                                                          chunkSize,
                                                                                          chunkCount,
                                                                                          fileHash));
                QApplication::processEvents();
            });

        const bool ok = receiverId.trimmed().isEmpty() && m_hasServerGroupSnapshot
            ? (asImage
                ? m_client->sendServerGroupImage(QStringLiteral("public"), filePath)
                : m_client->sendServerGroupFile(QStringLiteral("public"), filePath))
            : (asImage
                ? m_client->sendImage(filePath, receiverId)
                : m_client->sendFile(filePath, receiverId));

        QObject::disconnect(progressConnection);
        QObject::disconnect(preparedConnection);
        QObject::disconnect(cancelConnection);
        progress.setValue(ok ? 100 : progress.value());
        QApplication::processEvents();
        progress.close();

        if (cancelRequested && !ok) {
            if (transferSummary) *transferSummary = "已取消";
            if (canceled) *canceled = true;
            return false;
        }

        if (ok) {
            if (transferSummary) *transferSummary = preparedSummary;
            return true;
        }

        if (attempt < maxAttempts) {
            const QMessageBox::StandardButton retry = QMessageBox::warning(
                this,
                QString("%1发送失败").arg(kind),
                QString("%1“%2”发送失败，是否立即重试？\n当前为第 %3 次，共最多 %4 次。")
                    .arg(kind, info.fileName())
                    .arg(attempt)
                    .arg(maxAttempts),
                QMessageBox::Retry | QMessageBox::Cancel,
                QMessageBox::Retry);
            if (retry == QMessageBox::Retry) {
                ui->statusbar->showMessage(QString("正在重试发送%1：%2").arg(kind, info.fileName()), 1800);
                setTransferWorkspaceState(m_transferManager.failedSendWorkspaceState(kind,
                                                                                     info.fileName(),
                                                                                     LocalFileManager::humanFileSize(info.size()),
                                                                                     targetName));
                continue;
            }
        }

        return false;
    }

    return false;
}

void MainWindow::onFileTransferStatusChanged(const QString& fileName,
                                             const QString& transferId,
                                             const QString& reason,
                                             qint64 receivedBytes,
                                             qint64 totalBytes) {
    showFileTransferStatusEvent(fileName, transferId, reason, receivedBytes, totalBytes);
}

void MainWindow::showFileTransferStatusEvent(const QString& fileName,
                                             const QString& transferId,
                                             const QString& reason,
                                             qint64 receivedBytes,
                                             qint64 totalBytes) {
    const TransferStatusEvent event = m_transferManager.statusEvent(fileName, transferId, reason, receivedBytes, totalBytes);
    m_lastTransferStatusDiagnostic = event.diagnostic;
    applyTransferActionState(m_copyLastTransferStatusAction, event.copyDiagnostic.action);
    setTransferWorkspaceState(m_transferManager.statusWorkspaceState(fileName,
                                                                     transferId,
                                                                     reason,
                                                                     receivedBytes,
                                                                     totalBytes));
    appendSystemMessage(event.message);
    ui->chatHintLabel->setText(event.chatHintText);
    ui->statusbar->showMessage(event.statusBarMessage, event.statusBarTimeoutMs);
}

LocalSavedFileState MainWindow::savedFileActionState(const QModelIndex& index) const {
    LocalSavedFileState state;
    if (!index.isValid()) return state;
    const QString openPath = index.data(ChatMediaOpenPathRole).toString().trimmed();
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

    const QPixmap pixmap(savedFileState.fileInfo.absoluteFilePath());
    if (pixmap.isNull()) {
        return false;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("图片预览 - %1").arg(savedFileState.fileInfo.fileName()));
    dialog.resize(860, 640);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* imageLabel = new QLabel(&dialog);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    QScrollArea* scrollArea = new QScrollArea(&dialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setAlignment(Qt::AlignCenter);
    scrollArea->setWidget(imageLabel);
    layout->addWidget(scrollArea, 1);

    const QSize targetSize(780, 500);
    QPixmap scaled = pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull()) {
        scaled = pixmap;
    }
    imageLabel->setPixmap(scaled);
    imageLabel->resize(scaled.size());

    QLabel* infoLabel = new QLabel(QStringLiteral("%1 · %2 x %3 · %4")
                                       .arg(savedFileState.fileInfo.fileName())
                                       .arg(pixmap.width())
                                       .arg(pixmap.height())
                                       .arg(LocalFileManager::humanFileSize(savedFileState.fileInfo.size())),
                                   &dialog);
    infoLabel->setAlignment(Qt::AlignCenter);
    // infoLabel: color and padding inherited from style.qss
    layout->addWidget(infoLabel);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QPushButton* openFileButton = buttons->addButton(QStringLiteral("系统打开"), QDialogButtonBox::ActionRole);
    QPushButton* openFolderButton = buttons->addButton(QStringLiteral("打开目录"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(openFileButton, &QPushButton::clicked, this, [this, savedFileState]() {
        const ChatContextSavedFileCommand command =
            ChatContextManager::savedFileCommand(QStringLiteral("open-saved-file"),
                                                 chatContextSavedFileState(savedFileState),
                                                 savedFileState.savePath);
        openSavedFileFromState(savedFileState, command);
    });
    connect(openFolderButton, &QPushButton::clicked, this, [this, savedFileState]() {
        const ChatContextSavedFileCommand command =
            ChatContextManager::savedFileCommand(QStringLiteral("open-save-folder"),
                                                 chatContextSavedFileState(savedFileState),
                                                 savedFileState.savePath);
        openSavedFolderFromState(savedFileState, command);
    });

    dialog.exec();
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

FriendManagerVisibleTargetSummary MainWindow::friendNoticeVisibleTarget(const QString& userId) const {
    FriendManagerVisibleTargetSummary target;
    target.userId = userId.trimmed();
    if (!target.userId.isEmpty()) {
        target.displayName = m_friendNames.value(target.userId, contactDisplayName(target.userId));
        target.online = isContactOnline(target.userId);
    }
    return target;
}

QList<FriendManagerVisibleTargetSummary> MainWindow::visibleFriendNoticeTargets(QListWidget* noticeList) const {
    QList<FriendManagerVisibleTargetSummary> targets;
    const QStringList visibleIds = ::visibleFriendNoticeIds(noticeList);
    for (const QString& id : visibleIds) {
        if (id.isEmpty()) {
            continue;
        }
        targets << friendNoticeVisibleTarget(id);
    }
    return targets;
}

FriendNoticeSelectionSnapshot MainWindow::currentFriendNoticeSelectionSnapshot(QListWidget* noticeList,
                                                                               QLineEdit* searchEdit) const {
    const QString currentId = selectedFriendNoticeEntryId(noticeList);
    const FriendNoticeSelectionSnapshot snapshot =
        NotificationPanelManager::friendNoticeSelectionSnapshot(
            currentId,
            searchEdit ? searchEdit->text() : QString(),
            friendNoticeVisibleTarget(NotificationPanelManager::friendNoticeTargetId(
                currentId,
                searchEdit ? searchEdit->text() : QString())).displayName);
    return snapshot;
}

QList<GroupNoticeMemberInput> MainWindow::groupNoticeMemberCopyInputs(const QStringList& memberIds) const {
    QList<GroupNoticeMemberInput> inputs;
    for (const QString& id : memberIds) {
        GroupNoticeMemberInput input;
        input.userId = id;
        input.displayName = contactDisplayName(id);
        input.self = id == m_currentUserId;
        input.online = isContactOnline(id);
        inputs << input;
    }
    return inputs;
}

QStringList MainWindow::publicGroupNoticeMemberIds() const {
    QStringList onlineUserIds;
    for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
        onlineUserIds << it.key();
    }
    return NotificationPanelManager::publicGroupMemberIds(m_currentUserId, onlineUserIds);
}

QStringList MainWindow::groupNoticeMemberIds(const QString& groupId) const {
    QStringList members = groupId.isEmpty()
        ? publicGroupNoticeMemberIds()
        : m_localGroupMembers.value(groupId);
    if (members.isEmpty()) {
        members << m_currentUserId;
    }
    return members;
}

QList<GroupNoticeListGroupInput> MainWindow::groupNoticeLocalGroups() const {
    QList<GroupNoticeListGroupInput> localGroups;
    for (const QString& groupId : m_localGroupIds) {
        GroupNoticeListGroupInput group;
        group.groupId = groupId;
        group.groupName = m_localGroupNames.value(groupId, "群聊");
        group.groupNumber = groupId.mid(QString("local_group_").size());
        group.memberCount = groupNoticeMemberIds(groupId).size();
        group.announcement = m_localGroupAnnouncements.value(
            groupId,
            QString("%1 已创建，可继续邀请好友并发送消息。").arg(group.groupName));
        localGroups << group;
    }
    return localGroups;
}

void MainWindow::fillGroupNoticeList(QListWidget* noticeList,
                                     QLabel* countLabel,
                                     QLineEdit* searchEdit) const {
    if (!noticeList || !countLabel || !searchEdit) {
        return;
    }
    noticeList->clear();
    const GroupNoticeListRenderUiState renderState =
        NotificationPanelManager::groupNoticeListRenderUiState(m_knownUsers.size(),
                                                               groupNoticeLocalGroups(),
                                                               searchEdit->text());
    countLabel->setText(renderState.countText);
    for (const GroupNoticeListEntryUiState& entry : renderState.entries) {
        QListWidgetItem* item = new QListWidgetItem(entry.text);
        item->setData(Qt::UserRole, entry.entryId);
        item->setSizeHint(QSize(0, entry.rowHeight));
        item->setToolTip(entry.toolTip);
        if (entry.accent) {
            item->setForeground(QColor(18, 150, 247));
        }
        noticeList->addItem(item);
    }
    if (noticeList->count() > 0) {
        noticeList->setCurrentRow(0);
    }
}

GroupNoticeSelectionSnapshot MainWindow::currentGroupNoticeSelectionSnapshot(QListWidget* noticeList,
                                                                            QLineEdit* searchEdit) const {
    const QString groupId = selectedGroupNoticeEntryId(noticeList);
    return NotificationPanelManager::groupNoticeSelectionSnapshot(
        groupId,
        searchEdit ? searchEdit->text().trimmed() : QString(),
        m_knownUsers.size(),
        m_localGroupNames.value(groupId, "群聊"),
        m_localGroupMembers.value(groupId).size(),
        m_localGroupAnnouncements.value(groupId),
        m_currentUserId);
}

QList<GroupNoticeBatchTargetInput> MainWindow::visibleGroupNoticeBatchTargets(QListWidget* noticeList) const {
    QList<GroupNoticeBatchTargetInput> targets;
    const QStringList visibleIds = ::visibleGroupNoticeIds(noticeList);
    for (const QString& id : visibleIds) {
        GroupNoticeBatchTargetInput target;
        target.entryId = id;
        if (isGroupCreateEntryId(id)) {
            target.memberCount = 1;
            target.onlineCount = 1;
        } else if (id.isEmpty()) {
            const QStringList publicMembers = groupNoticeMemberIds(id);
            target.memberCount = publicMembers.size();
            target.onlineCount = publicMembers.size();
        } else if (id.startsWith("local_group_")) {
            const QStringList members = groupNoticeMemberIds(id);
            int groupOnline = 0;
            for (const QString& memberId : members) {
                if (memberId == m_currentUserId || isContactOnline(memberId)) {
                    ++groupOnline;
                }
            }
            target.groupName = m_localGroupNames.value(id, "群聊");
            target.groupNumber = id.mid(QString("local_group_").size());
            target.memberCount = qMax(1, members.size());
            target.onlineCount = groupOnline;
        }
        targets << target;
    }
    return targets;
}

bool MainWindow::openSelectedGroupNoticeEntry(QListWidget* noticeList, QDialog* dialog) {
    if (!noticeList || !noticeList->currentItem()) {
        ui->statusbar->showMessage("请先选择要进入的群聊", 1800);
        return false;
    }
    const QString groupId = selectedGroupNoticeEntryId(noticeList);
    if (isGroupCreateEntryId(groupId)) {
        QString groupName = groupCreateEntryName(groupId);
        if (groupName.isEmpty()) {
            groupName = "搜索群聊";
        }
        const QString newGroupId = "local_group_" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
        m_localGroupIds << newGroupId;
        m_localGroupNames[newGroupId] = groupName;
        m_localGroupAnnouncements[newGroupId] = QString("%1 已从群通知搜索创建，可继续邀请好友并发送消息。").arg(groupName);
        m_localGroupMembers[newGroupId] = QStringList{m_currentUserId};
        saveLocalGroups();
        refreshFriendList();
        if (dialog) {
            dialog->accept();
        }
        switchToLocalGroup(newGroupId, groupName);
        appendSystemMessage("已从群通知搜索创建群聊: " + groupName);
        ui->statusbar->showMessage("已创建并进入群聊: " + groupName, 2200);
        return true;
    }
    if (dialog) {
        dialog->accept();
    }
    if (groupId.isEmpty()) {
        onBackToGroupChat();
        ui->statusbar->showMessage("已进入公共聊天室", 1800);
        return true;
    }
    const QString groupName = m_localGroupNames.value(groupId, "群聊");
    switchToLocalGroup(groupId, groupName);
    ui->statusbar->showMessage("已进入群聊: " + groupName, 1800);
    return true;
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
    const QMessageBox::StandardButton choice = QMessageBox::question(dialogParent,
                                                                     title,
                                                                     message,
                                                                     QMessageBox::Yes | QMessageBox::No,
                                                                     QMessageBox::No);
    if (choice == QMessageBox::Yes) {
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
    return QInputDialog::getText(dialogParent,
                                 title,
                                 label,
                                 QLineEdit::Normal,
                                 initialValue,
                                 accepted).trimmed();
}

QString MainWindow::promptMultilineValue(const QString& title,
                                         const QString& label,
                                         const QString& initialValue,
                                         bool* accepted,
                                         QWidget* parent) const {
    QWidget* dialogParent = parent ? parent : const_cast<MainWindow*>(this);
    return QInputDialog::getMultiLineText(dialogParent,
                                          title,
                                          label,
                                          initialValue,
                                          accepted).trimmed();
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
    updateEmptyStateVisibility();
    const PrivateChatUiState privateState = ChatSessionManager::privateChatState(
        userId,
        userName,
        isContactOnline(userId),
        m_client && m_client->hasE2ESession(userId),
        m_client && m_client->e2eSessionNeedsRotation(userId));
    setWindowTitle(appWindowTitle(privateState.windowSuffix));
    ui->chatTitleLabel->setText(privateState.titleText);
    ui->chatHintLabel->setText(privateState.hintText);
    refreshComposerState();
    refreshSessionSummary();
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

bool MainWindow::handleCreateMenuCommand(const QString& commandId) {
    if (commandId == QLatin1String("create-group")) {
        bool ok = false;
        QString groupName = promptTextValue(QStringLiteral("创建群聊"),
                                            QStringLiteral("群聊名称:"),
                                            QStringLiteral("我的群聊"),
                                            &ok);
        if (!ok) {
            return true;
        }
        if (groupName.isEmpty()) {
            groupName = QStringLiteral("我的群聊");
        }

        const QString groupId = createLocalGroupSession(groupName);
        switchToLocalGroup(groupId, groupName);
        appendSystemMessage(QStringLiteral("已创建群聊: ") + groupName);
        return true;
    }

    if (commandId == QLatin1String("create-group-with-friends")) {
        QString groupName = ui->contactSearchEdit->text().trimmed();
        if (groupName.isEmpty()) {
            groupName = QStringLiteral("好友群聊");
        }

        const QStringList members = m_friendIds;
        const QString groupId = createLocalGroupSession(
            groupName,
            members,
            QStringLiteral("%1 已创建，已自动邀请全部好友。").arg(groupName));
        switchToLocalGroup(groupId, groupName);
        const int invitedCount = members.size();
        appendSystemMessage(QStringLiteral("已创建群聊并邀请 %1 位好友").arg(invitedCount));
        saveHistory(groupId,
                    QStringLiteral("[%1] [系统] 已创建群聊并邀请 %2 位好友")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")))
                        .arg(invitedCount));
        return true;
    }

    if (commandId == QLatin1String("open-global-search")) {
        onShowGlobalSearch();
        return true;
    }
    if (commandId == QLatin1String("focus-contact-search")) {
        ui->contactSearchEdit->setFocus();
        ui->contactSearchEdit->selectAll();
        ui->statusbar->showMessage(QStringLiteral("已定位到 QQ 搜索框，输入账号后回车自动查找"), 2500);
        return true;
    }
    if (commandId == QLatin1String("refresh-contacts")) {
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->statusbar->showMessage(QStringLiteral("联系人和群成员已刷新"), 2000);
        return true;
    }
    if (commandId == QLatin1String("clear-search")) {
        ui->contactSearchEdit->clear();
        ui->memberSearchEdit->clear();
        m_contactFilter.clear();
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->statusbar->showMessage(QStringLiteral("搜索条件已清空"), 1800);
        return true;
    }
    if (commandId == QLatin1String("copy-chat-id")) {
        QString chatId = m_privateChatTarget;
        if (chatId.startsWith(QStringLiteral("local_group_"))) {
            chatId = chatId.mid(QStringLiteral("local_group_").size());
        }
        if (chatId.isEmpty()) {
            chatId = m_currentUserId;
        }
        copyTextWithStatus(chatId, QStringLiteral("当前会话号已复制: ") + chatId, 2500);
        return true;
    }
    if (commandId == QLatin1String("copy-chat-card")) {
        QString card;
        if (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
            card = QStringLiteral("群聊 QQ:%1\n%2\n公告:%3")
                .arg(m_privateChatTarget.mid(QStringLiteral("local_group_").size()),
                     m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊")),
                     m_localGroupAnnouncements.value(m_privateChatTarget, ui->announcementBodyLabel->text()));
        } else if (!m_privateChatTarget.isEmpty()) {
            card = QStringLiteral("QQ:%1\n昵称:%2\n状态:%3")
                .arg(m_privateChatTarget,
                     contactDisplayName(m_privateChatTarget),
                     isContactOnline(m_privateChatTarget) ? QStringLiteral("在线") : QStringLiteral("离线"));
        } else {
            card = QStringLiteral("公共聊天室\n当前账号:%1\n在线成员:%2")
                .arg(m_currentUserId)
                .arg(m_knownUsers.size());
        }
        copyTextWithStatus(card, QStringLiteral("当前会话名片已复制"), 1800);
        return true;
    }
    if (commandId == QLatin1String("copy-current-invite")) {
        QString text;
        if (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
            text = QStringLiteral("我邀请你加入群聊“%1”（群号:%2）。我是 %3（QQ:%4），进群后我们一起沟通。")
                .arg(m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊")),
                     m_privateChatTarget.mid(QStringLiteral("local_group_").size()),
                     m_currentUserName,
                     m_currentUserId);
        } else if (!m_privateChatTarget.isEmpty()) {
            text = QStringLiteral("你好 %1，我是 %2（QQ:%3）。方便的话加个好友，我们可以继续私聊。")
                .arg(contactDisplayName(m_privateChatTarget), m_currentUserName, m_currentUserId);
        } else {
            text = QStringLiteral("你好，我是 %1（QQ:%2），欢迎加入公共聊天室，也可以通过 QQ 搜索加我好友。")
                .arg(m_currentUserName, m_currentUserId);
        }
        copyTextWithStatus(text, QStringLiteral("当前会话邀请语已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-current-members")) {
        QStringList cards;
        const QStringList ids = currentSessionMemberIds();
        for (const QString& id : ids) {
            cards << QStringLiteral("QQ:%1 昵称:%2 状态:%3")
                .arg(id,
                     contactDisplayName(id),
                     (id == m_currentUserId || isContactOnline(id)) ? QStringLiteral("在线") : QStringLiteral("离线"));
        }
        if (cards.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("当前会话没有成员可复制"), 2200);
            return true;
        }
        copyTextWithStatus(cards.join(QLatin1Char('\n')),
                           QStringLiteral("已复制 %1 个当前成员").arg(cards.size()),
                           2200);
        return true;
    }
    if (commandId == QLatin1String("copy-current-online")) {
        QStringList cards;
        const QStringList ids = currentSessionMemberIds();
        for (const QString& id : ids) {
            if (id != m_currentUserId && !isContactOnline(id)) {
                continue;
            }
            cards << QStringLiteral("在线 QQ:%1 昵称:%2").arg(id, contactDisplayName(id));
        }
        if (cards.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("当前会话没有在线成员可复制"), 2200);
            return true;
        }
        copyTextWithStatus(cards.join(QLatin1Char('\n')),
                           QStringLiteral("已复制 %1 个在线成员").arg(cards.size()),
                           2200);
        return true;
    }
    if (commandId == QLatin1String("copy-all-contacts")) {
        QStringList rows;
        rows << QStringLiteral("我的QQ:%1 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QStringLiteral("好友:%1 群聊:%2 在线:%3")
            .arg(m_friendIds.size())
            .arg(m_localGroupIds.size())
            .arg(m_knownUsers.size());
        for (const QString& id : m_friendIds) {
            rows << QStringLiteral("好友 QQ:%1 昵称:%2 状态:%3")
                .arg(id,
                     contactDisplayName(id),
                     isContactOnline(id) ? QStringLiteral("在线") : QStringLiteral("离线"));
        }
        for (const QString& groupId : m_localGroupIds) {
            rows << QStringLiteral("群聊 QQ:%1 名称:%2 成员:%3")
                .arg(groupId.mid(QStringLiteral("local_group_").size()),
                     m_localGroupNames.value(groupId, QStringLiteral("群聊")),
                     QString::number(m_localGroupMembers.value(groupId).size()));
        }
        copyTextWithStatus(rows.join(QLatin1Char('\n')),
                           QStringLiteral("已复制联系人摘要 %1 行").arg(rows.size()),
                           2200);
        return true;
    }
    if (commandId == QLatin1String("copy-search-summary")) {
        QString filter = ui->contactSearchEdit->text().trimmed();
        if (filter.isEmpty()) {
            filter = QStringLiteral("全部");
        }
        const QString summary = QStringLiteral("QQ搜索:%1\n好友:%2\n本地群:%3\n在线成员:%4\n当前会话:%5")
            .arg(filter)
            .arg(m_friendIds.size())
            .arg(m_localGroupIds.size())
            .arg(m_knownUsers.size())
            .arg(m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget));
        copyTextWithStatus(summary, QStringLiteral("QQ 搜索摘要已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-quick-guide")) {
        QStringList rows;
        rows << QStringLiteral("我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QStringLiteral("1. 点击综合搜索可按 QQ 号/昵称查找用户、好友和群聊");
        rows << QStringLiteral("2. 搜索结果可直接打开、发送好友申请、复制名片或复制邀请卡");
        rows << QStringLiteral("3. 好友申请支持推荐在线用户、复制申请话术和自动发送申请");
        rows << QStringLiteral("4. 好友管理器可搜索、备注、邀入群、复制在线好友和统计");
        rows << QStringLiteral("当前好友:%1 · 群聊:%2 · 在线:%3")
            .arg(m_friendIds.size())
            .arg(m_localGroupIds.size())
            .arg(m_knownUsers.size());
        copyTextWithStatus(rows.join(QLatin1Char('\n')), QStringLiteral("QQ 功能指南已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-media-guide")) {
        QStringList rows;
        rows << QStringLiteral("上传指南 · 我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QStringLiteral("1. 点击 图片/视频 可发送 png、jpg、gif、mp4、mov、avi、mkv、wmv、flv、webm");
        rows << QStringLiteral("2. 图片会显示预览卡片，视频会以文件卡片发送");
        rows << QStringLiteral("3. 点击 闪传文件 可发送文档、压缩包和媒体文件");
        rows << QStringLiteral("4. 聊天记录右键可复制媒体卡片或查收话术");
        rows << QStringLiteral("当前会话:%1")
            .arg(m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget));
        copyTextWithStatus(rows.join(QLatin1Char('\n')), QStringLiteral("上传指南已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-current-media-pack")) {
        const QString sessionName = m_privateChatTarget.isEmpty()
            ? QStringLiteral("公共聊天室")
            : contactDisplayName(m_privateChatTarget);
        QString sessionId = m_privateChatTarget;
        if (sessionId.startsWith(QStringLiteral("local_group_"))) {
            sessionId = sessionId.mid(QStringLiteral("local_group_").size());
        }
        if (sessionId.isEmpty()) {
            sessionId = QStringLiteral("public");
        }
        QStringList rows;
        rows << QStringLiteral("媒体发送包 · 会话:%1 · 会话号:%2").arg(sessionName, sessionId);
        rows << QStringLiteral("发送者:%1 · QQ:%2").arg(m_currentUserName, m_currentUserId);
        rows << QStringLiteral("图片/视频入口：点击工具栏 图片/视频，或菜单栏 发送图片/视频");
        rows << QStringLiteral("文件入口：点击工具栏 闪传文件，或菜单栏 闪传文件");
        rows << QStringLiteral("支持格式：png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm + 文档/压缩包");
        rows << QStringLiteral("查收话术：我已准备发送图片/视频/文件到 %1，请注意查收。").arg(sessionName);
        copyTextWithStatus(rows.join(QLatin1Char('\n')), QStringLiteral("当前媒体发送包已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-full-media-plan")) {
        const QString sessionName = m_privateChatTarget.isEmpty()
            ? QStringLiteral("公共聊天室")
            : contactDisplayName(m_privateChatTarget);
        QString sessionId = m_privateChatTarget;
        if (sessionId.startsWith(QStringLiteral("local_group_"))) {
            sessionId = sessionId.mid(QStringLiteral("local_group_").size());
        }
        if (sessionId.isEmpty()) {
            sessionId = QStringLiteral("public");
        }
        QStringList rows;
        rows << QStringLiteral("完整媒体计划 · 当前会话:%1 · 会话号:%2").arg(sessionName, sessionId);
        rows << QStringLiteral("我的QQ:%1 · 昵称:%2 · 好友:%3 · 群聊:%4 · 在线:%5")
            .arg(m_currentUserId,
                 m_currentUserName,
                 QString::number(m_friendIds.size()),
                 QString::number(m_localGroupIds.size()),
                 QString::number(m_knownUsers.size()));
        rows << QStringLiteral("1. 先用综合搜索/好友申请确认目标 QQ 或群聊");
        rows << QStringLiteral("2. 通过好友管理/群通知复制媒体包、邀请语和成员列表");
        rows << QStringLiteral("3. 点击 图片/视频 发送图片、GIF 或视频；点击 闪传文件 发送文档和压缩包");
        rows << QStringLiteral("4. 发送后聊天记录会生成媒体卡片、查收话术；接收后生成回执话术和保存路径");
        rows << QStringLiteral("当前查收话术：我已准备发送媒体文件到 %1，请注意查收。").arg(sessionName);
        copyTextWithStatus(rows.join(QLatin1Char('\n')), QStringLiteral("完整媒体计划已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("edit-announcement")) {
        onEditGroupAnnouncement();
        return true;
    }
    if (commandId == QLatin1String("copy-announcement")) {
        copyTextWithStatus(ui->announcementBodyLabel->text(), QStringLiteral("群公告已复制"), 1800);
        return true;
    }
    if (commandId == QLatin1String("show-friend-notice")) {
        onShowFriendNotifications();
        return true;
    }
    if (commandId == QLatin1String("show-group-notice")) {
        onShowGroupNotifications();
        return true;
    }
    if (commandId == QLatin1String("send-image")) {
        onSendImage();
        return true;
    }
    if (commandId == QLatin1String("send-file")) {
        onSendFile();
        return true;
    }
    return false;
}

void MainWindow::setChatDraftText(const QString& text, const QString& statusMessage, int timeoutMs) {
    ui->messageEdit->setPlainText(text);
    ui->messageEdit->setFocus();
    ui->statusbar->showMessage(statusMessage, timeoutMs);
}

void MainWindow::insertChatDraftText(const QString& text, const QString& statusMessage, int timeoutMs) {
    ui->messageEdit->insertPlainText(text);
    ui->messageEdit->setFocus();
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
                                          const LocalSavedFileState& savedFileState) {
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

    return false;
}

QString MainWindow::currentServerGroupId() const {
    if (m_privateChatTarget.isEmpty() && m_hasServerGroupSnapshot && m_serverGroupMembers.contains(QStringLiteral("public"))) {
        return QStringLiteral("public");
    }
    return QString();
}

QString MainWindow::currentFavoriteSessionId() const {
    if (m_privateChatTarget.isEmpty()) {
        return QStringLiteral("public");
    }
    if (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
        return m_privateChatTarget.mid(QStringLiteral("local_group_").size());
    }
    return m_privateChatTarget;
}

QString MainWindow::chatMessageIdForIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return QString();
    }
    QString messageId = index.data(ChatMessageIdRole).toString().trimmed();
    if (messageId.isEmpty()) {
        const QString sessionId = index.data(ChatSessionIdRole).toString().trimmed().isEmpty()
            ? currentFavoriteSessionId()
            : index.data(ChatSessionIdRole).toString().trimmed();
        messageId = compactMessageId(QStringLiteral("%1|%2").arg(sessionId, index.data().toString()));
        if (QStandardItem* item = m_chatModel->itemFromIndex(index)) {
            item->setData(messageId, ChatMessageIdRole);
            item->setData(sessionId, ChatSessionIdRole);
        }
    }
    return messageId;
}

QJsonObject MainWindow::chatMessagePayloadForFavorite(const QModelIndex& index,
                                                      const QString& messageId) const {
    QJsonObject message;
    const QString text = index.data().toString();
    const QString sessionId = index.data(ChatSessionIdRole).toString().trimmed().isEmpty()
        ? currentFavoriteSessionId()
        : index.data(ChatSessionIdRole).toString().trimmed();
    const QString senderId = index.data(ChatBubbleSenderIdRole).toString();
    const QString senderName = index.data(ChatBubbleSenderNameRole).toString();
    message["id"] = messageId;
    message["messageId"] = messageId;
    message["sessionId"] = sessionId;
    message["senderId"] = senderId;
    message["senderName"] = senderName;
    message["content"] = text;
    message["type"] = QStringLiteral("text");
    message["timestamp"] = QString::number(QDateTime::currentMSecsSinceEpoch());
    return message;
}

void MainWindow::applyRecalledChatMessage(const QString& messageId,
                                          const QString& groupId,
                                          const QString& operatorId) {
    if (messageId.isEmpty()) {
        return;
    }
    for (int row = 0; row < m_chatModel->rowCount(); ++row) {
        QStandardItem* item = m_chatModel->item(row);
        if (!item || item->data(ChatMessageIdRole).toString() != messageId) {
            continue;
        }
        const QString oldText = item->text();
        if (!oldText.contains(QStringLiteral("已撤回"))) {
            item->setText(QStringLiteral("[已撤回] %1").arg(oldText));
        }
        item->setData(true, ChatRecalledRole);
        item->setForeground(QColor(135, 150, 165));
        item->setToolTip(QStringLiteral("服务端群 %1 消息已由 %2 撤回").arg(groupId, operatorId));
    }
    if (ui->chatListView) {
        ui->chatListView->viewport()->update();
    }
}

void MainWindow::handleServerGroupEssenceUpdated(const QJsonObject& payload) {
    const QString groupId = payload["groupId"].toString(QStringLiteral("public")).trimmed();
    const QString messageId = payload["messageId"].toString().trimmed();
    const bool enabled = payload["enabled"].toBool(true);
    if (groupId.isEmpty() || messageId.isEmpty()) {
        return;
    }
    const QString key = groupId + QLatin1Char('|') + messageId;
    if (enabled) {
        m_serverGroupEssenceMessageKeys.insert(key);
    } else {
        m_serverGroupEssenceMessageKeys.remove(key);
    }
    for (int row = 0; row < m_chatModel->rowCount(); ++row) {
        QStandardItem* item = m_chatModel->item(row);
        if (item && item->data(ChatMessageIdRole).toString() == messageId) {
            item->setData(enabled, ChatEssenceRole);
            item->setToolTip(enabled ? QStringLiteral("已设为群精华") : QString());
        }
    }
    appendSystemMessage(QStringLiteral("群精华已%1：%2").arg(enabled ? QStringLiteral("设置") : QStringLiteral("取消"), messageId));
}

void MainWindow::handleMessageFavoriteUpdated(const QJsonObject& payload) {
    const QString sessionId = payload["sessionId"].toString().trimmed();
    const QString messageId = payload["messageId"].toString().trimmed();
    const bool favorite = payload["favorite"].toBool(false);
    if (sessionId.isEmpty() || messageId.isEmpty()) {
        return;
    }
    const QString key = sessionId + QLatin1Char('|') + messageId;
    if (favorite) {
        m_favoriteMessageKeys.insert(key);
    } else {
        m_favoriteMessageKeys.remove(key);
    }
    for (int row = 0; row < m_chatModel->rowCount(); ++row) {
        QStandardItem* item = m_chatModel->item(row);
        if (item && item->data(ChatMessageIdRole).toString() == messageId) {
            item->setData(favorite, ChatFavoritedRole);
            item->setToolTip(favorite ? QStringLiteral("已收藏到服务端") : QString());
        }
    }
    ui->statusbar->showMessage(favorite ? QStringLiteral("服务端收藏已同步") : QStringLiteral("服务端收藏已取消"), 1800);
}

void MainWindow::handleFavoriteMessagesSnapshotReceived(const QJsonArray& favorites) {
    m_favoriteMessageKeys.clear();
    for (const QJsonValue& value : favorites) {
        const QJsonObject obj = value.toObject();
        const QString sessionId = obj["sessionId"].toString().trimmed();
        const QString messageId = obj["messageId"].toString(obj["id"].toString()).trimmed();
        if (!sessionId.isEmpty() && !messageId.isEmpty()) {
            m_favoriteMessageKeys.insert(sessionId + QLatin1Char('|') + messageId);
        }
    }
    ui->statusbar->showMessage(QStringLiteral("已同步服务端收藏 %1 条").arg(favorites.size()), 1600);
}

void MainWindow::handleServerGroupMessageRecalled(const QJsonObject& payload) {
    const QString groupId = payload["groupId"].toString(QStringLiteral("public")).trimmed();
    const QString messageId = payload["messageId"].toString().trimmed();
    if (groupId.isEmpty() || messageId.isEmpty()) {
        return;
    }
    m_serverGroupRecalledMessageKeys.insert(groupId + QLatin1Char('|') + messageId);
    applyRecalledChatMessage(messageId, groupId, payload["operatorId"].toString());
    appendSystemMessage(QStringLiteral("群消息已撤回：%1").arg(messageId));
}

void MainWindow::handleServerGroupMemberMuted(const QJsonObject& payload) {
    const QString groupId = payload["groupId"].toString(QStringLiteral("public")).trimmed();
    const QString memberId = payload["memberId"].toString().trimmed();
    const qint64 mutedUntil = payload["mutedUntil"].toVariant().toLongLong();
    if (!groupId.isEmpty() && !memberId.isEmpty() && mutedUntil > 0) {
        m_serverGroupMemberMutedUntil[groupId + QLatin1Char('|') + memberId] = mutedUntil;
        refreshGroupMemberPanel();
    }
    const QString message = payload["message"].toString();
    if (!message.isEmpty()) {
        ui->statusbar->showMessage(message, 2600);
    } else if (!memberId.isEmpty()) {
        appendSystemMessage(QStringLiteral("群成员 %1 已被禁言").arg(contactDisplayName(memberId)));
    }
}

void MainWindow::handleServerGroupMemberUnmuted(const QJsonObject& payload) {
    const QString groupId = payload["groupId"].toString(QStringLiteral("public")).trimmed();
    const QString memberId = payload["memberId"].toString().trimmed();
    if (!groupId.isEmpty() && !memberId.isEmpty()) {
        m_serverGroupMemberMutedUntil.remove(groupId + QLatin1Char('|') + memberId);
        refreshGroupMemberPanel();
        appendSystemMessage(QStringLiteral("群成员 %1 已解除禁言").arg(contactDisplayName(memberId)));
    }
}

void MainWindow::handleServerGroupMemberProfileReceived(const QJsonObject& payload) {
    const QString memberId = payload["memberId"].toString().trimmed();
    const QString userName = payload["userName"].toString(contactDisplayName(memberId));
    const QString role = payload["role"].toString(QStringLiteral("member"));
    const QString groupId = payload["groupId"].toString(QStringLiteral("public"));
    const qint64 mutedUntil = payload["mutedUntil"].toVariant().toLongLong();
    const QString mutedText = mutedUntil > QDateTime::currentMSecsSinceEpoch()
        ? QDateTime::fromMSecsSinceEpoch(mutedUntil).toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))
        : QStringLiteral("未禁言");
    QMessageBox::information(this,
                             "群成员资料",
                             QStringLiteral("QQ:%1\n昵称:%2\n群:%3\n角色:%4\n在线:%5\n禁言:%6")
                                 .arg(memberId,
                                      userName,
                                      groupId,
                                      role,
                                      payload["online"].toBool(false) ? QStringLiteral("在线") : QStringLiteral("离线"),
                                      mutedText));
}

void MainWindow::updateSavedOutgoingTransferRecoveryUi(bool announce) {
    if (!m_resumeSavedTransferAction || !m_clearSavedTransferAction) return;

    QJsonObject state;
    const bool hasSavedTransfer = m_client && m_client->loadOutgoingTransferState(&state);
    const QJsonObject recoveryStatus = hasSavedTransfer ? m_client->savedOutgoingTransferRecoveryStatus() : QJsonObject();
    const TransferRecoveryUiState uiState = m_transferManager.recoveryUiState(
        hasSavedTransfer,
        m_client && m_client->isConnected(),
        state,
        recoveryStatus,
        announce);
    setTransferWorkspaceState(hasSavedTransfer
        ? m_transferManager.recoveryWorkspaceState(uiState)
        : m_transferManager.idleWorkspaceState(m_client && m_client->isConnected(), false, false));
    applyTransferActionState(m_resumeSavedTransferAction, uiState.resumeAction);
    applyTransferActionState(m_clearSavedTransferAction, uiState.clearAction);

    if (announce && !uiState.announceMessage.isEmpty()) {
        appendSystemMessage(uiState.announceMessage);
        ui->statusbar->showMessage(uiState.statusMessage, uiState.canAutoResume ? 3200 : 3600);
    }
}

void MainWindow::onClearSavedOutgoingTransfer() {
    if (!m_client) return;

    QJsonObject state;
    if (!m_client->loadOutgoingTransferState(&state)) {
        updateSavedOutgoingTransferRecoveryUi(false);
        ui->statusbar->showMessage(m_transferManager.clearRecoveryPrompt(QJsonObject()).noSavedStatusMessage, 2200);
        return;
    }

    const TransferClearRecoveryPrompt prompt = m_transferManager.clearRecoveryPrompt(state);
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        prompt.title,
        prompt.message,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        ui->statusbar->showMessage(prompt.keptStatusMessage, 1800);
        return;
    }

    if (m_client->clearOutgoingTransferState()) {
        appendSystemMessage(prompt.clearedSystemMessage);
        setTransferWorkspaceState(m_transferManager.clearedRecoveryWorkspaceState(prompt.fileName));
        ui->statusbar->showMessage(prompt.clearedStatusMessage, 2200);
    } else {
        appendSystemMessage(prompt.clearFailedSystemMessage);
        ui->statusbar->showMessage(prompt.clearFailedStatusMessage, 2600);
    }
    updateSavedOutgoingTransferRecoveryUi(false);
}

void MainWindow::onResumeSavedOutgoingTransfer() {
    if (!m_client) return;

    QJsonObject state;
    if (!m_client->loadOutgoingTransferState(&state)) {
        updateSavedOutgoingTransferRecoveryUi(false);
        ui->statusbar->showMessage("暂无可恢复的未完成发送", 2200);
        return;
    }

    const QFileInfo info(state["filePath"].toString());
    const QString fileName = info.fileName().isEmpty() ? "未命名文件" : info.fileName();
    const QString receiverId = state["receiverId"].toString().trimmed();
    const QString targetName = receiverId.isEmpty() ? "公共聊天室" : QString("QQ:%1").arg(receiverId);
    const QJsonObject recoveryStatus = m_client->savedOutgoingTransferRecoveryStatus();
    if (!recoveryStatus.value("canAutoResume").toBool(false)) {
        const TransferResumeBlockedPrompt prompt = m_transferManager.resumeBlockedPrompt(state, recoveryStatus);
        appendSystemMessage(prompt.systemMessage);
        ui->chatHintLabel->setText(prompt.hintText);
        setTransferWorkspaceState(m_transferManager.resumeBlockedWorkspaceState(prompt));
        ui->statusbar->showMessage(prompt.statusMessage, 3600);
        const QMessageBox::StandardButton choice = QMessageBox::information(
            this,
            prompt.title,
            prompt.message,
            QMessageBox::Ok | QMessageBox::Discard,
            QMessageBox::Ok);
        if (choice == QMessageBox::Discard && m_client->clearOutgoingTransferState()) {
            appendSystemMessage(prompt.clearedSystemMessage);
            ui->chatHintLabel->setText(prompt.clearedHintText);
            setTransferWorkspaceState(m_transferManager.clearedRecoveryWorkspaceState(prompt.fileName));
            ui->statusbar->showMessage(prompt.clearedStatusMessage, 2200);
        }
        updateSavedOutgoingTransferRecoveryUi(false);
        return;
    }

    QProgressDialog progress(this);
    progress.setWindowTitle("恢复未完成发送");
    progress.setLabelText(m_transferManager.resumeInitialState(fileName, targetName).labelText);
    progress.setCancelButtonText("取消");
    progress.setRange(0, 100);
    progress.setValue(0);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);

    bool cancelRequested = false;
    QMetaObject::Connection cancelConnection = connect(
        &progress,
        &QProgressDialog::canceled,
        this,
        [this, &progress, &cancelRequested, &fileName]() {
            cancelRequested = true;
            progress.setLabelText(m_transferManager.resumeCancelState(fileName).labelText);
            if (m_client) m_client->cancelCurrentOutgoingTransfer();
            ui->statusbar->showMessage("正在取消恢复发送：" + fileName, 1600);
            QApplication::processEvents();
        });
    QMetaObject::Connection progressConnection = connect(
        m_client,
        &Client::fileTransferProgress,
        this,
        [this, &progress, &fileName, &targetName](const QString& currentFileName, qint64 bytesPrepared, qint64 totalBytes) {
            if (currentFileName != fileName) return;
            const TransferProgressUiState state = m_transferManager.resumeProgressState(fileName, targetName, bytesPrepared, totalBytes);
            progress.setValue(state.percent);
            progress.setLabelText(state.labelText);
            setTransferWorkspaceState(m_transferManager.sendingProgressWorkspaceState(QStringLiteral("文件"),
                                                                                      fileName,
                                                                                      targetName,
                                                                                      bytesPrepared,
                                                                                      totalBytes,
                                                                                      true));
            QApplication::processEvents();
        });
    QMetaObject::Connection preparedConnection = connect(
        m_client,
        &Client::fileTransferPrepared,
        this,
        [this, &progress, &fileName, &targetName](const QString& currentFileName,
                                             qint64 totalBytes,
                                             qint64 chunkSize,
                                             qint64 chunkCount,
                                             const QString& fileHash) {
            if (currentFileName != fileName) return;
            progress.setLabelText(m_transferManager.resumePreparedState(fileName, targetName, totalBytes, chunkSize, chunkCount, fileHash).labelText);
            setTransferWorkspaceState(m_transferManager.sendingPreparedWorkspaceState(QStringLiteral("文件"),
                                                                                      fileName,
                                                                                      targetName,
                                                                                      totalBytes,
                                                                                      chunkSize,
                                                                                      chunkCount,
                                                                                      fileHash,
                                                                                      true));
            QApplication::processEvents();
        });

    ui->statusbar->showMessage("正在恢复未完成发送：" + fileName, 1800);
    setTransferWorkspaceState(m_transferManager.recoveryWorkspaceState(
        m_transferManager.recoveryUiState(true, true, state, recoveryStatus, false)));
    QString rejectReason;
    const bool resumed = m_client->resumeSavedOutgoingTransfer(&rejectReason, 5000);

    QObject::disconnect(progressConnection);
    QObject::disconnect(preparedConnection);
    QObject::disconnect(cancelConnection);
    progress.setValue(resumed ? 100 : progress.value());
    QApplication::processEvents();
    progress.close();

    const TransferResumeResultState resultState = m_transferManager.resumeResultState(
        fileName,
        targetName,
        resumed,
        cancelRequested,
        rejectReason);
    if (resultState.succeeded) {
        appendSystemMessage(resultState.systemMessage);
        ui->chatHintLabel->setText(resultState.hintText);
        setTransferWorkspaceState(m_transferManager.resumeResultWorkspaceState(resultState));
        ui->statusbar->showMessage(resultState.statusMessage, 2600);
    } else if (resultState.canceled) {
        appendSystemMessage(resultState.systemMessage);
        ui->chatHintLabel->setText(resultState.hintText);
        setTransferWorkspaceState(m_transferManager.resumeResultWorkspaceState(resultState));
        ui->statusbar->showMessage(resultState.statusMessage, 2200);
    } else {
        appendSystemMessage(resultState.systemMessage);
        ui->chatHintLabel->setText(resultState.hintText);
        setTransferWorkspaceState(m_transferManager.resumeResultWorkspaceState(resultState));
        ui->statusbar->showMessage(resultState.statusMessage, 3200);
        const QMessageBox::StandardButton choice = QMessageBox::warning(
            this,
            resultState.failureTitle,
            resultState.failureMessage,
            QMessageBox::Ok | QMessageBox::Discard,
            QMessageBox::Ok);
        if (choice == QMessageBox::Discard && m_client->clearOutgoingTransferState()) {
            appendSystemMessage(resultState.clearedSystemMessage);
            ui->chatHintLabel->setText(resultState.clearedHintText);
            setTransferWorkspaceState(m_transferManager.clearedRecoveryWorkspaceState(resultState.fileName));
            ui->statusbar->showMessage(resultState.clearedStatusMessage, 2200);
        }
    }

    if (!resultState.succeeded && !resultState.canceled) {
        updateSavedOutgoingTransferRecoveryUi(false);
    }
}

void MainWindow::setupUi() {
    m_userListModel->setHorizontalHeaderLabels({"在线用户"});
    ui->userListView->setModel(m_userListModel);
    ui->userListView->setItemDelegate(new SessionItemDelegate(ui->userListView));
    ui->userListView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->userListView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->userListView->setSpacing(0);

    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    ui->chatListView->setModel(m_chatModel);
    ui->chatListView->setItemDelegate(new ChatBubbleDelegate(ui->chatListView));
    ui->chatListView->setIconSize(QSize(34, 34));
    ui->chatListView->setSpacing(0);
    ui->chatListView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->chatListView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->chatListView->setToolTip("右键消息可复制、引用和转发；双击带保存路径的文件记录可直接打开文件");

    m_groupMemberModel->setHorizontalHeaderLabels({"群成员"});
    ui->groupMemberListView->setModel(m_groupMemberModel);
    ui->groupMemberListView->setItemDelegate(new GroupMemberItemDelegate(ui->groupMemberListView));
    ui->groupMemberListView->setProperty("currentUserId", m_currentUserId);
    ui->groupMemberListView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->groupMemberListView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->groupMemberListView->setSpacing(0);
    ui->groupMemberListView->setWordWrap(true);

    ui->messageEdit->setPlaceholderText("输入消息... (Enter 发送，Shift/Ctrl+Enter 换行，Esc 清空草稿)");
    ui->messageEdit->setFocus();
    ui->messageEdit->installEventFilter(this);
    ui->messageEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->chatStatusLabel->setText("Redis 在线工作台 · 多实例状态同步中");
    ui->composerStateLabel->setText("Enter 发送，Shift/Ctrl+Enter 换行，Esc 清空草稿");
    ui->contactSearchEdit->installEventFilter(this);
    ui->memberSearchEdit->installEventFilter(this);
    ui->contactSearchEdit->setToolTip("搜索联系人、QQ 号或群聊；按 Enter 搜索账号，Esc 清空");
    ui->memberSearchEdit->setToolTip("搜索当前群成员；群聊中可输入 QQ 号后按 Enter 邀请");

    ui->clearBtn->setText(IconHelper::iconFor("clear"));
    ui->clearBtn->setObjectName("clearBtn");
    ui->clearBtn->setToolTip("清空当前会话的本地聊天记录");
    ui->clearBtn->setProperty("navBtn", true);
    ui->fileBtn->setText(IconHelper::iconFor("file"));
    ui->fileBtn->setObjectName("toolBtn");
    ui->fileBtn->setToolTip("闪传文件，支持文档、压缩包和媒体文件");
    ui->imageBtn->setText(IconHelper::iconFor("image"));
    ui->imageBtn->setObjectName("toolBtn");
    ui->imageBtn->setToolTip("发送图片或视频文件，图片会显示预览");
    ui->emojiBtn->setText(IconHelper::iconFor("emoji"));
    ui->emojiBtn->setObjectName("iconToolBtn");
    ui->emojiBtn->setToolTip("插入常用表情");
    ui->mentionBtn->setText("@");
    ui->mentionBtn->setObjectName("iconToolBtn");
    ui->mentionBtn->setToolTip("快速 @ 群成员或插入会话提醒");
    ui->sendBtn->setText(IconHelper::iconFor("send"));
    ui->sendBtn->setObjectName("sendBtn");
    ui->sendBtn->setToolTip("请输入消息后发送");
    ui->sendBtn->setEnabled(false);
    ui->globalSearchBtn->setText(IconHelper::iconFor("search"));
    ui->globalSearchBtn->setObjectName("globalSearchBtn");
    ui->globalSearchBtn->setToolTip("打开综合搜索；搜索框有内容时直接搜索该 QQ 号");
    ui->createMenuBtn->setText(IconHelper::iconFor("menu"));
    ui->createMenuBtn->setObjectName("createMenuBtn");
    ui->createMenuBtn->setToolTip("打开创建和快捷操作菜单");
    ui->uploadAvatarBtn->setText(IconHelper::iconFor("avatar"));
    ui->uploadAvatarBtn->setObjectName("toolBtn");
    ui->uploadAvatarBtn->setToolTip("更换当前头像");

    ui->navMessages->setIcon(IconHelper::icon("messages", QColor(95, 102, 114)));
    ui->navContacts->setIcon(IconHelper::icon("contacts", QColor(95, 102, 114)));
    ui->navSpace->setIcon(IconHelper::icon("space", QColor(95, 102, 114)));
    ui->navChannel->setIcon(IconHelper::icon("channel", QColor(95, 102, 114)));
    ui->navMail->setIcon(IconHelper::icon("mail", QColor(95, 102, 114)));
    ui->navDocs->setIcon(IconHelper::icon("docs", QColor(95, 102, 114)));
    ui->navCalendar->setIcon(IconHelper::icon("calendar", QColor(95, 102, 114)));
    ui->navMeeting->setIcon(IconHelper::icon("meeting", QColor(95, 102, 114)));
    ui->navFavorites->setIcon(IconHelper::icon("favorites", QColor(95, 102, 114)));
    ui->navWallet->setIcon(IconHelper::icon("wallet", QColor(95, 102, 114)));
    ui->navSettings->setIcon(IconHelper::icon("settings", QColor(95, 102, 114)));
    ui->navProfile->setIcon(IconHelper::icon("friends", QColor(95, 102, 114)));

    QAction* friendManagerAction = new QAction("好友管理器", this);
    QAction* storageManagerAction = new QAction("存储管理", this);
    QAction* backGroupAction = new QAction("返回群聊", this);
    QAction* avatarAction = new QAction("上传头像", this);
    QAction* sendImageAction = new QAction("发送图片/视频", this);
    QAction* sendFileAction = new QAction("闪传文件", this);
    m_resumeSavedTransferAction = new QAction("恢复未完成发送", this);
    m_resumeSavedTransferAction->setVisible(false);
    m_resumeSavedTransferAction->setEnabled(false);
    m_clearSavedTransferAction = new QAction("清除恢复记录", this);
    m_clearSavedTransferAction->setVisible(false);
    m_clearSavedTransferAction->setEnabled(false);
    m_copyLastTransferStatusAction = new QAction("复制最近文件状态", this);
    m_copyLastTransferStatusAction->setVisible(false);
    m_copyLastTransferStatusAction->setEnabled(false);
    QAction* filterHistoryAction = new QAction("按日期查记录", this);
    QAction* exportHistoryAction = new QAction("导出聊天记录", this);
    QAction* copyAccountAction = new QAction("复制账号", this);
    QAction* copySummaryAction = new QAction("复制账号摘要", this);
    QAction* deactivateAccountAction = new QAction("申请注销账号", this);
    QAction* cancelDeactivationAction = new QAction("取消注销申请", this);
    QAction* logoutAction = new QAction("退出登录", this);
    ui->menubar->addAction(friendManagerAction);
    ui->menubar->addAction(storageManagerAction);
    ui->menubar->addAction(backGroupAction);
    ui->menubar->addAction(avatarAction);
    ui->menubar->addAction(sendImageAction);
    ui->menubar->addAction(sendFileAction);
    ui->menubar->addAction(m_resumeSavedTransferAction);
    ui->menubar->addAction(m_clearSavedTransferAction);
    ui->menubar->addAction(m_copyLastTransferStatusAction);
    ui->menubar->addAction(filterHistoryAction);
    ui->menubar->addAction(exportHistoryAction);
    ui->menubar->addAction(copyAccountAction);
    ui->menubar->addAction(copySummaryAction);
    ui->menubar->addAction(deactivateAccountAction);
    ui->menubar->addAction(cancelDeactivationAction);
    ui->menubar->addAction(logoutAction);

    connect(friendManagerAction, &QAction::triggered, this, &MainWindow::onShowFriendManager);
    connect(storageManagerAction, &QAction::triggered, this, &MainWindow::onShowStorageManager);
    connect(backGroupAction, &QAction::triggered, this, &MainWindow::onBackToGroupChat);
    connect(avatarAction, &QAction::triggered, this, &MainWindow::onUploadAvatar);
    connect(sendImageAction, &QAction::triggered, this, &MainWindow::onSendImage);
    connect(sendFileAction, &QAction::triggered, this, &MainWindow::onSendFile);
    connect(m_resumeSavedTransferAction, &QAction::triggered, this, &MainWindow::onResumeSavedOutgoingTransfer);
    connect(m_clearSavedTransferAction, &QAction::triggered, this, &MainWindow::onClearSavedOutgoingTransfer);
    connect(m_copyLastTransferStatusAction, &QAction::triggered, this, [this]() {
        const TransferDiagnosticCopyUiState copyState = m_transferManager.diagnosticCopyUiState(m_lastTransferStatusDiagnostic);
        if (!copyState.action.enabled) {
            ui->statusbar->showMessage(copyState.emptyStatusMessage, 1800);
            return;
        }
        QApplication::clipboard()->setText(copyState.clipboardText);
        ui->statusbar->showMessage(copyState.copiedStatusMessage, 2200);
    });
    connect(filterHistoryAction, &QAction::triggered, this, &MainWindow::onFilterHistoryByDate);
    connect(exportHistoryAction, &QAction::triggered, this, &MainWindow::onExportHistory);
    connect(copyAccountAction, &QAction::triggered, this, &MainWindow::onCopyAccount);
    connect(copySummaryAction, &QAction::triggered, this, [this]() {
        QString summary = QString("账号摘要\nQQ:%1\n昵称:%2\n好友:%3\n群聊:%4\n当前会话:%5")
            .arg(m_currentUserId,
                 m_currentUserName,
                 QString::number(m_friendIds.size()),
                 QString::number(m_localGroupIds.size()),
                 m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget));
        QApplication::clipboard()->setText(summary);
        ui->statusbar->showMessage("账号摘要已复制", 2200);
    });
    connect(deactivateAccountAction, &QAction::triggered, this, [this]() {
        bool ok = false;
        const QString reason = QInputDialog::getText(this,
                                                     "申请注销账号",
                                                     "注销原因（可选）:",
                                                     QLineEdit::Normal,
                                                     QString(),
                                                     &ok).trimmed();
        if (!ok) {
            ui->statusbar->showMessage("已取消注销申请", 1600);
            return;
        }
        if (!confirmAction("申请注销账号",
                           QString("确定提交账号 %1 的注销申请吗？可在宽限期内取消。").arg(m_currentUserId),
                           "已取消注销申请")) {
            return;
        }
        if (!m_client || !m_client->requestAccountDeactivation(reason)) {
            ui->statusbar->showMessage("注销申请提交失败，请检查连接", 2600);
            return;
        }
        appendSystemMessage("账号注销申请已提交，等待服务端确认。");
        ui->statusbar->showMessage("注销申请已提交", 2200);
    });
    connect(cancelDeactivationAction, &QAction::triggered, this, [this]() {
        if (!m_client || !m_client->cancelAccountDeactivation()) {
            ui->statusbar->showMessage("取消注销提交失败，请检查连接", 2600);
            return;
        }
        appendSystemMessage("取消账号注销申请已提交。");
        ui->statusbar->showMessage("取消注销申请已提交", 2200);
    });
    connect(logoutAction, &QAction::triggered, this, &MainWindow::onLogout);

    // Theme toggle from designer action.
    if (ui->actionTheme) {
        connect(ui->actionTheme, &QAction::triggered, this, &MainWindow::on_actionTheme_triggered);
    }

    refreshFriendList();

    connect(ui->sendBtn, &QPushButton::clicked, this, &MainWindow::onSendMessage);
    connect(ui->messageEdit, &QTextEdit::textChanged, this, &MainWindow::refreshComposerState);
    refreshComposerState();
    connect(ui->fileBtn, &QPushButton::clicked, this, &MainWindow::onSendFile);
    connect(ui->imageBtn, &QPushButton::clicked, this, &MainWindow::onSendImage);
    connect(ui->emojiBtn, &QPushButton::clicked, this, &MainWindow::onInsertEmoji);
    connect(ui->mentionBtn, &QPushButton::clicked, this, &MainWindow::onInsertMention);
    QShortcut* contactSearchShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(contactSearchShortcut, &QShortcut::activated, this, [this]() {
        ui->contactSearchEdit->setFocus();
        ui->contactSearchEdit->selectAll();
        ui->statusbar->showMessage("已定位到联系人搜索", 1600);
    });
    QShortcut* memberSearchShortcut = new QShortcut(QKeySequence("Ctrl+Shift+F"), this);
    connect(memberSearchShortcut, &QShortcut::activated, this, [this]() {
        ui->memberSearchEdit->setFocus();
        ui->memberSearchEdit->selectAll();
        ui->statusbar->showMessage("已定位到成员搜索", 1600);
    });
    QShortcut* globalSearchShortcut = new QShortcut(QKeySequence("Ctrl+K"), this);
    connect(globalSearchShortcut, &QShortcut::activated, this, &MainWindow::onShowGlobalSearch);
    connect(ui->messageEdit, &QTextEdit::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        const QString draftText = ui->messageEdit->toPlainText().trimmed();
        const QString clipboardText = QApplication::clipboard()->text().trimmed();
        const bool hasDraft = !draftText.isEmpty();
        const bool hasClipboardText = !clipboardText.isEmpty();
        const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
        const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
        const bool canReachTarget = isLocalGroup || (m_client && m_client->isConnected());
        auto describeInputAction = [](QAction* action, const QString& tip) {
            action->setToolTip(tip);
            action->setStatusTip(tip);
        };
        ChatContextComposerRuntimeState runtimeState;
        runtimeState.hasDraft = hasDraft;
        runtimeState.hasClipboardText = hasClipboardText;
        runtimeState.canReachTarget = canReachTarget;
        runtimeState.draftTextLength = draftText.size();
        runtimeState.targetDisplayName = targetName;
        const QList<ChatContextComposerMenuAction> runtimeActions = ChatContextManager::composerRuntimeActions(runtimeState);
        QAction* pasteAction = nullptr;
        QAction* pasteSendAction = nullptr;
        QAction* sendAction = nullptr;
        QAction* clearAction = nullptr;
        QAction* mentionAction = nullptr;
        for (const ChatContextComposerMenuAction& spec : runtimeActions) {
            QAction* action = menu.addAction(spec.title);
            describeInputAction(action, spec.toolTip);
            action->setData(spec.commandId);
            if (spec.commandId == QLatin1String("composer-paste")) {
                action->setEnabled(hasClipboardText);
                pasteAction = action;
            } else if (spec.commandId == QLatin1String("composer-paste-send")) {
                action->setEnabled(hasClipboardText && canReachTarget);
                pasteSendAction = action;
            } else if (spec.commandId == QLatin1String("composer-send")) {
                action->setEnabled(hasDraft && canReachTarget);
                sendAction = action;
            } else if (spec.commandId == QLatin1String("composer-clear")) {
                action->setEnabled(hasDraft);
                clearAction = action;
            } else if (spec.commandId == QLatin1String("composer-mention")) {
                mentionAction = action;
            }
        }
        menu.addSeparator();
        const QList<ChatContextComposerMenuAction> composerActions = ChatContextManager::composerMenuActions();
        QList<QAction*> composerMenuQtActions;
        composerMenuQtActions.reserve(composerActions.size());
        for (int i = 0; i < 8 && i < composerActions.size(); ++i) {
            const ChatContextComposerMenuAction& spec = composerActions.at(i);
            QAction* action = menu.addAction(spec.title);
            describeInputAction(action, spec.toolTip);
            action->setData(spec.commandId);
            composerMenuQtActions.append(action);
        }
        menu.addSeparator();
        const QList<ChatContextPhraseMenuPlan> phraseMenuPlans = ChatContextManager::composerPhraseMenuPlans();
        for (const ChatContextPhraseMenuPlan& plan : phraseMenuPlans) {
            QMenu* phraseMenu = menu.addMenu(plan.title);
            for (const QString& phrase : plan.phrases) {
                QAction* phraseAction = phraseMenu->addAction(phrase);
                connect(phraseAction, &QAction::triggered, ui->messageEdit, [this, phrase, plan]() {
                    insertChatDraftText(phrase, plan.insertedStatusMessage, 1400);
                });
            }
        }
        for (int i = 8; i < composerActions.size(); ++i) {
            const ChatContextComposerMenuAction& spec = composerActions.at(i);
            QAction* action = menu.addAction(spec.title);
            describeInputAction(action, spec.toolTip);
            action->setData(spec.commandId);
            composerMenuQtActions.append(action);
        }
        QAction* selected = menu.exec(ui->messageEdit->viewport()->mapToGlobal(pos));
        if (!selected) return;
        if (selected == pasteAction) {
            ui->messageEdit->paste();
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage(QString("已粘贴到输入框 · 当前 %1 字").arg(ui->messageEdit->toPlainText().trimmed().size()), 1600);
        } else if (selected == pasteSendAction) {
            ui->messageEdit->paste();
            onSendMessage();
        } else if (selected == sendAction) {
            onSendMessage();
        } else if (selected == clearAction) {
            ui->messageEdit->clear();
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("输入草稿已清空", 1400);
        } else if (selected == mentionAction) {
            onInsertMention();
        } else {
            const QString commandId = selected->data().toString();
            if (!commandId.isEmpty()) {
                applyChatContextComposerCommand(commandId);
            }
        }
    });
    connect(ui->userListView, &QListView::doubleClicked, this, &MainWindow::onPrivateChat);
    connect(ui->userListView, &QListView::customContextMenuRequested, this, &MainWindow::onUserContextMenu);
    connect(ui->chatListView, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;

        const LocalSavedFileState savedFileState = savedFileActionState(index);
        if (!savedFileState.hasSavePath) return;
        if (openMediaPreviewFromState(savedFileState)) {
            return;
        }

        ChatContextSavedFileCommand command = ChatContextManager::savedFileCommand(QStringLiteral("open-saved-file"),
                                                                                   chatContextSavedFileState(savedFileState),
                                                                                   savedFileState.savePath);
        command.failureStatusMessage = QStringLiteral("保存文件不存在或无法打开");
        openSavedFileFromState(savedFileState, command);
    });
    connect(ui->chatListView, &QListView::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex index = ui->chatListView->indexAt(pos);
        if (!index.isValid()) return;
        QString text = index.data().toString();
        if (text.isEmpty()) return;
        QMenu menu(this);
        const LocalSavedFileState savedFileState = savedFileActionState(index);
        const ChatContextSavedFileState savedContextState = chatContextSavedFileState(savedFileState);
        const bool isMediaMessage = ChatContextManager::isMediaMessage(text, savedContextState);
        const QList<ChatContextMenuActionSpec> actionSpecs = ChatContextManager::menuActionSpecs(
            isMediaMessage,
            savedContextState);
        for (const ChatContextMenuActionSpec& spec : actionSpecs) {
            if (spec.separatorBefore) {
                menu.addSeparator();
            }
            addChatContextAction(menu,
                                 spec.title,
                                 spec.toolTip,
                                 spec.commandId,
                                 spec.enabled);
        }
        const QString messageId = chatMessageIdForIndex(index);
        const QString favoriteSessionId = currentFavoriteSessionId();
        const QString serverGroupId = currentServerGroupId();
        const bool hasBackendMessageId = !messageId.isEmpty();
        const bool favorited = m_favoriteMessageKeys.contains(favoriteSessionId + QLatin1Char('|') + messageId);
        const bool isEssence = !serverGroupId.isEmpty()
            && m_serverGroupEssenceMessageKeys.contains(serverGroupId + QLatin1Char('|') + messageId);
        menu.addSeparator();
        addChatContextAction(menu,
                             favorited ? QStringLiteral("取消收藏") : QStringLiteral("收藏消息"),
                             QStringLiteral("同步到服务端收藏列表"),
                             QStringLiteral("qt-toggle-favorite"),
                             hasBackendMessageId && m_client && m_client->isConnected());
        addChatContextAction(menu,
                             isEssence ? QStringLiteral("取消精华") : QStringLiteral("设为精华"),
                             QStringLiteral("服务端群主/管理员可设置或取消群精华"),
                             QStringLiteral("qt-toggle-essence"),
                             hasBackendMessageId && !serverGroupId.isEmpty() && canCurrentUserManageServerGroup(serverGroupId));
        addChatContextAction(menu,
                             QStringLiteral("撤回群消息"),
                             QStringLiteral("服务端群主/管理员可撤回群消息"),
                             QStringLiteral("qt-recall-group-message"),
                             hasBackendMessageId && !serverGroupId.isEmpty() && canCurrentUserManageServerGroup(serverGroupId));
        QAction* selected = menu.exec(ui->chatListView->viewport()->mapToGlobal(pos));
        if (!selected) return;
        const QString commandId = selected->data().toString();
        if (commandId == QLatin1String("qt-toggle-favorite")
            || commandId == QLatin1String("qt-toggle-essence")
            || commandId == QLatin1String("qt-recall-group-message")) {
            if (!m_client || !m_client->isConnected()) {
                ui->statusbar->showMessage("后端未连接，无法执行服务端消息操作", 2400);
                return;
            }
            const QString messageId = chatMessageIdForIndex(index);
            if (messageId.isEmpty()) {
                ui->statusbar->showMessage("该消息缺少 messageId，无法同步到服务端", 2400);
                return;
            }
            const QString groupId = currentServerGroupId();
            if (commandId == QLatin1String("qt-toggle-favorite")) {
                const QString sessionId = currentFavoriteSessionId();
                const QString key = sessionId + QLatin1Char('|') + messageId;
                const bool favorite = !m_favoriteMessageKeys.contains(key);
                if (!m_client->sendMessageFavoriteUpdate(sessionId, messageId, favorite, chatMessagePayloadForFavorite(index, messageId))) {
                    ui->statusbar->showMessage("收藏同步失败，请检查连接", 2400);
                    return;
                }
                ui->statusbar->showMessage(favorite ? "收藏请求已提交" : "取消收藏请求已提交", 1800);
                return;
            }
            if (groupId.isEmpty()) {
                ui->statusbar->showMessage("当前会话不是服务端群，无法操作精华/撤回", 2400);
                return;
            }
            if (commandId == QLatin1String("qt-toggle-essence")) {
                if (!canCurrentUserManageServerGroup(groupId)) {
                    ui->statusbar->showMessage("只有群主或管理员可以设置精华", 2400);
                    return;
                }
                const QString key = groupId + QLatin1Char('|') + messageId;
                const bool enabled = !m_serverGroupEssenceMessageKeys.contains(key);
                if (!m_client->sendServerGroupEssenceUpdate(groupId, messageId, enabled)) {
                    ui->statusbar->showMessage("精华同步失败，请检查连接", 2400);
                    return;
                }
                ui->statusbar->showMessage(enabled ? "设为精华请求已提交" : "取消精华请求已提交", 1800);
                return;
            }
            if (commandId == QLatin1String("qt-recall-group-message")) {
                if (!canCurrentUserManageServerGroup(groupId)) {
                    ui->statusbar->showMessage("只有群主或管理员可以撤回群消息", 2400);
                    return;
                }
                if (!confirmAction("撤回群消息", "确定撤回这条服务端群消息吗？", "已取消撤回群消息")) {
                    return;
                }
                if (!m_client->sendServerGroupMessageRecall(groupId, messageId)) {
                    ui->statusbar->showMessage("撤回同步失败，请检查连接", 2400);
                    return;
                }
                ui->statusbar->showMessage("撤回请求已提交", 1800);
                return;
            }
        }
        handleChatContextCommand(commandId, text, savedFileState);
    });
    connect(ui->contactSearchEdit, &QLineEdit::textChanged, this, &MainWindow::onContactSearchChanged);
    ui->contactSearchEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->contactSearchEdit, &QLineEdit::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        const QString clipboardText = QApplication::clipboard()->text().trimmed();
        const bool hasClipboardText = !clipboardText.isEmpty();
        const bool hasSearchText = !ui->contactSearchEdit->text().trimmed().isEmpty();
        QAction* pasteAction = menu.addAction("粘贴");
        QAction* pasteSearchAction = menu.addAction("粘贴并搜索");
        QAction* globalSearchAction = menu.addAction("打开综合搜索");
        QAction* createGroupAction = menu.addAction("用关键词建群");
        QAction* copySearchCardAction = menu.addAction("复制搜索名片");
        QAction* clearAction = menu.addAction("清空搜索");
        pasteAction->setEnabled(hasClipboardText);
        pasteSearchAction->setEnabled(hasClipboardText);
        clearAction->setEnabled(hasSearchText);
        pasteAction->setToolTip(hasClipboardText ? "把剪贴板文字粘贴到 QQ 搜索框" : "剪贴板里没有可粘贴的文字");
        pasteSearchAction->setToolTip(hasClipboardText ? "粘贴剪贴板文字并搜索 QQ 账号" : "剪贴板里没有可搜索的文字");
        clearAction->setToolTip(hasSearchText ? "清空当前 QQ 搜索条件" : "搜索框已经是空的");
        QAction* selected = menu.exec(ui->contactSearchEdit->mapToGlobal(pos));
        if (selected == pasteAction) {
            ui->contactSearchEdit->paste();
            ui->contactSearchEdit->setFocus();
            ui->statusbar->showMessage("已粘贴到 QQ 搜索框", 1600);
        } else if (selected == pasteSearchAction) {
            ui->contactSearchEdit->clear();
            ui->contactSearchEdit->paste();
            searchAndAddAccount(ui->contactSearchEdit->text().trimmed(), this);
        } else if (selected == globalSearchAction) {
            onShowGlobalSearch();
        } else if (selected == createGroupAction) {
            QString groupName = ui->contactSearchEdit->text().trimmed();
            if (groupName.isEmpty()) groupName = "搜索群聊";
            QString groupId = "local_group_" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
            m_localGroupIds << groupId;
            m_localGroupNames[groupId] = groupName;
            m_localGroupAnnouncements[groupId] = QString("%1 已从 QQ 搜索框创建，可继续邀请好友并发送消息。").arg(groupName);
            m_localGroupMembers[groupId] = QStringList{m_currentUserId};
            saveLocalGroups();
            refreshFriendList();
            switchToLocalGroup(groupId, groupName);
            ui->statusbar->showMessage("已从 QQ 搜索框创建群聊: " + groupName, 2500);
        } else if (selected == copySearchCardAction) {
            QString keyword = ui->contactSearchEdit->text().trimmed();
            if (keyword.isEmpty()) keyword = "全部";
            QString card = QString("QQ搜索名片\n关键词:%1\n我的QQ:%2\n昵称:%3\n好友:%4\n群聊:%5")
                .arg(keyword, m_currentUserId, m_currentUserName)
                .arg(m_friendIds.size())
                .arg(m_localGroupIds.size());
            QApplication::clipboard()->setText(card);
            ui->statusbar->showMessage("QQ 搜索名片已复制", 2200);
        } else if (selected == clearAction) {
            ui->contactSearchEdit->clear();
            ui->contactSearchEdit->setFocus();
            ui->statusbar->showMessage("QQ 搜索已清空", 1400);
        }
    });
    connect(ui->contactSearchEdit, &QLineEdit::returnPressed, this, [this]() {
        const QString text = ui->contactSearchEdit->text().trimmed();
        if (text.isEmpty()) {
            ui->contactSearchEdit->setFocus();
            ui->statusbar->showMessage("请输入 QQ 号或关键词后再搜索", 1800);
            return;
        }
        searchAndAddAccount(text, this);
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
    connect(ui->announcementTitleLabel, &QLabel::linkActivated, this, &MainWindow::onEditGroupAnnouncement);
    connect(ui->uploadAvatarBtn, &QPushButton::clicked, this, &MainWindow::onUploadAvatar);
    connect(ui->memberSearchEdit, &QLineEdit::textChanged, this, [this]() { refreshGroupMemberPanel(); });
    connect(ui->memberSearchEdit, &QLineEdit::returnPressed, this, [this]() {
        QString text = ui->memberSearchEdit->text().trimmed();
        if (text.isEmpty()) {
            ui->memberSearchEdit->setFocus();
            ui->statusbar->showMessage(m_privateChatTarget.startsWith("local_group_") ? "请输入 QQ 号后邀请入群" : "请输入 QQ 号或关键词后再搜索", 1800);
            return;
        }
        if (m_privateChatTarget.startsWith("local_group_")) {
            if (!isCurrentUserGroupOwner(m_privateChatTarget)) {
                ui->memberSearchEdit->selectAll();
                ui->statusbar->showMessage("只有群主可以邀请新成员入群", 2400);
                return;
            }
            if (!m_localGroupMembers[m_privateChatTarget].contains(text)) {
                m_localGroupMembers[m_privateChatTarget] << text;
                saveLocalGroups();
                refreshGroupMemberPanel();
                appendSystemMessage("已按 QQ 号邀请入群: " + text);
                ui->memberSearchEdit->selectAll();
                ui->statusbar->showMessage(QString("已邀请 QQ:%1 入群，可继续输入下一个 QQ").arg(text), 2200);
            } else {
                ui->memberSearchEdit->selectAll();
                ui->statusbar->showMessage("该 QQ 已在当前群聊中", 1800);
            }
        } else if (canCurrentUserManageServerGroup("public")) {
            requestServerGroupMemberUpdate(text, "add");
            ui->memberSearchEdit->selectAll();
        } else {
            searchAndAddAccount(text, this);
        }
    });
    ui->memberSearchEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->memberSearchEdit, &QLineEdit::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        const QString clipboardText = QApplication::clipboard()->text().trimmed();
        const bool hasClipboardText = !clipboardText.isEmpty();
        const bool hasSearchText = !ui->memberSearchEdit->text().trimmed().isEmpty();
        QAction* pasteAction = menu.addAction("粘贴");
        QAction* pasteSearchAction = menu.addAction("粘贴并搜索");
        QAction* addVisibleAction = menu.addAction("发送可见成员好友申请");
        QAction* copyVisibleAction = menu.addAction("复制可见成员");
        QAction* copyOnlineVisibleAction = menu.addAction("复制在线成员");
        QAction* clearAction = menu.addAction("清空搜索");
        pasteAction->setEnabled(hasClipboardText);
        pasteSearchAction->setEnabled(hasClipboardText);
        clearAction->setEnabled(hasSearchText);
        pasteAction->setToolTip(hasClipboardText ? "把剪贴板文字粘贴到成员搜索框" : "剪贴板里没有可粘贴的文字");
        pasteSearchAction->setToolTip(hasClipboardText ? "粘贴剪贴板文字并刷新成员筛选" : "剪贴板里没有可搜索的文字");
        clearAction->setToolTip(hasSearchText ? "清空当前成员搜索条件" : "成员搜索框已经是空的");
        QAction* selected = menu.exec(ui->memberSearchEdit->mapToGlobal(pos));
        if (selected == pasteAction) {
            ui->memberSearchEdit->paste();
            ui->memberSearchEdit->setFocus();
            ui->statusbar->showMessage("已粘贴到成员搜索框", 1600);
        } else if (selected == pasteSearchAction) {
            ui->memberSearchEdit->clear();
            ui->memberSearchEdit->paste();
            refreshGroupMemberPanel();
            ui->memberSearchEdit->setFocus();
            ui->statusbar->showMessage("成员筛选已更新", 1600);
        } else if (selected == addVisibleAction) {
            int requestCount = 0;
            int pendingSkipped = 0;
            int failedCount = 0;
            for (int i = 0; i < m_groupMemberModel->rowCount(); ++i) {
                QStandardItem* item = m_groupMemberModel->item(i);
                if (!item) continue;
                QString id = item->data(Qt::UserRole + 1).toString();
                if (id.isEmpty() || id == m_currentUserId || id.startsWith("group_search_add:") || id.startsWith("group_invite:") || m_friendIds.contains(id)) continue;
                if (m_pendingOutgoingFriendRequests.contains(id)) {
                    ++pendingSkipped;
                    continue;
                }
                const QString displayName = contactDisplayName(id);
                if (!m_client->sendFriendRequest(id)) {
                    ++failedCount;
                    continue;
                }
                m_friendNames[id] = displayName;
                m_pendingOutgoingFriendRequests << id;
                ++requestCount;
            }
            if (requestCount > 0) {
                refreshFriendList();
                refreshGroupMemberPanel();
                QString detail = QString("已向 %1 个可见群成员发送好友申请").arg(requestCount);
                if (pendingSkipped > 0) detail += QString(" · 已跳过申请中 %1 个").arg(pendingSkipped);
                if (failedCount > 0) detail += QString(" · 失败 %1 个").arg(failedCount);
                appendSystemMessage(detail);
                ui->statusbar->showMessage(detail, 2800);
            } else {
                QString message = pendingSkipped > 0
                    ? QString("可见群成员均已是好友或申请中")
                    : QString("暂无可发送申请的可见群成员");
                if (failedCount > 0) message += QString(" · 失败 %1 个").arg(failedCount);
                ui->statusbar->showMessage(message, 2400);
            }
        } else if (selected == copyVisibleAction) {
            QStringList cards;
            for (int i = 0; i < m_groupMemberModel->rowCount(); ++i) {
                QStandardItem* item = m_groupMemberModel->item(i);
                if (!item) continue;
                QString id = item->data(Qt::UserRole + 1).toString();
                if (id.isEmpty() || id.startsWith("group_search_add:") || id.startsWith("group_invite:")) continue;
                cards << QString("QQ:%1 昵称:%2 状态:%3").arg(id, contactDisplayName(id), isContactOnline(id) || id == m_currentUserId ? "在线" : "离线");
            }
            if (!cards.isEmpty()) {
                QApplication::clipboard()->setText(cards.join('\n'));
                ui->statusbar->showMessage(QString("已复制 %1 个可见成员").arg(cards.size()), 2200);
            } else {
                ui->statusbar->showMessage("当前筛选没有可复制成员", 2200);
            }
        } else if (selected == copyOnlineVisibleAction) {
            QStringList cards;
            for (int i = 0; i < m_groupMemberModel->rowCount(); ++i) {
                QStandardItem* item = m_groupMemberModel->item(i);
                if (!item) continue;
                QString id = item->data(Qt::UserRole + 1).toString();
                if (id.isEmpty() || id.startsWith("group_search_add:") || id.startsWith("group_invite:")) continue;
                if (id != m_currentUserId && !isContactOnline(id)) continue;
                cards << QString("在线成员 QQ:%1 昵称:%2").arg(id, contactDisplayName(id));
            }
            if (cards.isEmpty()) {
                ui->statusbar->showMessage("当前筛选没有在线成员", 2200);
                return;
            }
            QApplication::clipboard()->setText(cards.join('\n'));
            ui->statusbar->showMessage(QString("已复制 %1 个在线成员").arg(cards.size()), 2200);
        } else if (selected == clearAction) {
            ui->memberSearchEdit->clear();
            ui->memberSearchEdit->setFocus();
            ui->statusbar->showMessage("成员搜索已清空", 1400);
        }
    });
    connect(ui->groupMemberListView, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        QString targetId = index.data(Qt::UserRole + 1).toString();
        if (targetId.startsWith("group_search_add:")) {
            const QString account = targetId.mid(QString("group_search_add:").size()).trimmed();
            if (m_privateChatTarget.isEmpty() && canCurrentUserManageServerGroup("public")) {
                requestServerGroupMemberUpdate(account, "add");
            } else {
                searchAndAddAccount(account, this);
            }
            return;
        }
        if (targetId.startsWith("group_invite:")) {
            QString account = targetId.mid(QString("group_invite:").size()).trimmed();
            if (!isCurrentUserGroupOwner(m_privateChatTarget)) {
                ui->statusbar->showMessage("只有群主可以邀请新成员入群", 2400);
                return;
            }
            if (!account.isEmpty() && !m_localGroupMembers[m_privateChatTarget].contains(account)) {
                m_localGroupMembers[m_privateChatTarget] << account;
                QString requestNote;
                if (!m_friendIds.contains(account) && !m_pendingOutgoingFriendRequests.contains(account)) {
                    const QString displayName = contactDisplayName(account);
                    if (m_client->sendFriendRequest(account)) {
                        m_friendNames[account] = displayName;
                        m_pendingOutgoingFriendRequests << account;
                        requestNote = "，好友申请等待确认";
                    } else {
                        requestNote = "，好友申请发送失败";
                        ui->statusbar->showMessage(QString("已邀请入群，但好友申请发送失败：%1").arg(displayName), 3000);
                    }
                } else if (m_pendingOutgoingFriendRequests.contains(account)) {
                    requestNote = "，好友申请已在等待确认";
                }
                saveLocalGroups();
                refreshFriendList();
                refreshGroupMemberPanel();
                appendSystemMessage(QString("已按 QQ 号邀请入群: %1%2").arg(account, requestNote));
                saveHistory(m_privateChatTarget, QString("[%1] [系统] 已按 QQ 号邀请 %2 加入群聊").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), account));
            }
            return;
        }
        if (targetId.isEmpty() || targetId == m_currentUserId) return;
        ensureFriendRequestQueued(targetId,
                                  QStringLiteral("已向群成员发送好友申请 QQ:%1，等待对方同意"),
                                  true);
        openPrivateSession(targetId);
    });
    connect(ui->groupMemberListView, &QListView::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex index = ui->groupMemberListView->indexAt(pos);
        const bool isLocalGroup = m_privateChatTarget.startsWith("local_group_");
        const bool isServerPublicGroup = m_privateChatTarget.isEmpty() && !m_serverGroupMembers.value("public").isEmpty();
        if (!index.isValid() || (!isLocalGroup && !isServerPublicGroup)) return;
        QString memberId = index.data(Qt::UserRole + 1).toString();
        if (memberId.startsWith("group_search_add:") || memberId.startsWith("group_invite:")) return;
        if (memberId.isEmpty() || memberId == m_currentUserId) return;
        QMenu menu(this);
        const GroupMemberContextMenuPlan plan = GroupManager::memberContextMenuPlan(
            memberId,
            m_currentUserId,
            isLocalGroup,
            isServerPublicGroup,
            isLocalGroup ? groupOwnerId(m_privateChatTarget) : QString(),
            isLocalGroup && isCurrentUserGroupOwner(m_privateChatTarget),
            m_serverGroupOwners,
            m_serverGroupMemberRoles);
        QAction* chatAction = menu.addAction("私聊");
        QAction* copyAction = menu.addAction("复制QQ号");
        QAction* profileAction = menu.addAction("复制名片");
        QAction* copyAllAction = menu.addAction("复制群成员列表");
        QAction* copyOnlineAction = menu.addAction("复制在线群成员");
        QAction* renameAction = menu.addAction("设置备注");
        QAction* requestProfileAction = nullptr;
        QAction* muteAction = nullptr;
        QAction* unmuteAction = nullptr;
        QAction* promoteAdminAction = nullptr;
        QAction* demoteAdminAction = nullptr;
        if (isServerPublicGroup) {
            requestProfileAction = menu.addAction("查看服务端资料");
            muteAction = menu.addAction("禁言 10 分钟");
            unmuteAction = menu.addAction("解除禁言");
            promoteAdminAction = menu.addAction("设为管理员");
            demoteAdminAction = menu.addAction("取消管理员");
        }
        QAction* removeAction = menu.addAction("移出群聊");
        auto describeMemberAction = [](QAction* action, const QString& tip) {
            action->setToolTip(tip);
            action->setStatusTip(tip);
        };
        describeMemberAction(chatAction, plan.chatToolTip);
        describeMemberAction(copyAction, plan.copyToolTip);
        describeMemberAction(profileAction, plan.profileToolTip);
        describeMemberAction(copyAllAction, plan.copyAllToolTip);
        describeMemberAction(copyOnlineAction, plan.copyOnlineToolTip);
        describeMemberAction(renameAction, plan.renameToolTip);
        if (requestProfileAction) {
            describeMemberAction(requestProfileAction, "向服务端请求群成员资料、角色和禁言状态");
            requestProfileAction->setEnabled(m_client && m_client->isConnected());
        }
        if (muteAction) {
            describeMemberAction(muteAction, "群主或管理员可禁言该成员 10 分钟");
            muteAction->setEnabled(plan.canManageGroup && memberId != plan.ownerId && m_client && m_client->isConnected());
        }
        if (unmuteAction) {
            describeMemberAction(unmuteAction, "群主或管理员可解除该成员禁言");
            unmuteAction->setEnabled(plan.canManageGroup && memberId != plan.ownerId && m_client && m_client->isConnected());
        }
        if (promoteAdminAction) {
            describeMemberAction(promoteAdminAction, plan.promoteAdminToolTip);
            promoteAdminAction->setEnabled(plan.promoteAdminEnabled);
        }
        if (demoteAdminAction) {
            describeMemberAction(demoteAdminAction, plan.demoteAdminToolTip);
            demoteAdminAction->setEnabled(plan.demoteAdminEnabled);
        }
        describeMemberAction(removeAction, plan.removeToolTip);
        removeAction->setEnabled(plan.removeEnabled);
        QAction* selected = menu.exec(ui->groupMemberListView->viewport()->mapToGlobal(pos));
        if (selected == chatAction) {
            ensureFriendRequestQueued(memberId,
                                      QStringLiteral("已向群成员发送好友申请 QQ:%1，等待对方同意"),
                                      true);
            openPrivateSession(memberId);
        } else if (selected == copyAction) {
            QApplication::clipboard()->setText(memberId);
            ui->statusbar->showMessage("QQ 号已复制: " + memberId, 2500);
        } else if (selected == profileAction) {
            const QString groupName = isLocalGroup ? m_localGroupNames.value(m_privateChatTarget, "群聊") : m_serverGroupNames.value("public", "公共聊天室");
            QString card = QString("QQ:%1\n昵称:%2\n群聊:%3").arg(memberId, contactDisplayName(memberId), groupName);
            QApplication::clipboard()->setText(card);
            ui->statusbar->showMessage("群成员名片已复制", 1800);
        } else if (selected == copyAllAction) {
            QStringList cards;
            const QStringList memberIds = isLocalGroup ? m_localGroupMembers.value(m_privateChatTarget) : m_serverGroupMembers.value("public");
            for (const QString& id : memberIds) {
                cards << QString("QQ:%1 昵称:%2 状态:%3").arg(id, contactDisplayName(id), isContactOnline(id) || id == m_currentUserId ? "在线" : "离线");
            }
            QApplication::clipboard()->setText(cards.join('\n'));
            ui->statusbar->showMessage(QString("已复制 %1 个群成员").arg(cards.size()), 2200);
        } else if (selected == copyOnlineAction) {
            QStringList cards;
            const QStringList memberIds = isLocalGroup ? m_localGroupMembers.value(m_privateChatTarget) : m_serverGroupMembers.value("public");
            for (const QString& id : memberIds) {
                if (id != m_currentUserId && !isContactOnline(id)) continue;
                cards << QString("在线群成员 QQ:%1 昵称:%2").arg(id, contactDisplayName(id));
            }
            if (cards.isEmpty()) {
                ui->statusbar->showMessage("当前群聊没有在线成员", 2200);
                return;
            }
            QApplication::clipboard()->setText(cards.join('\n'));
            ui->statusbar->showMessage(QString("已复制 %1 个在线群成员").arg(cards.size()), 2200);
        } else if (selected == renameAction) {
            bool ok = false;
            const QString oldRemark = contactDisplayName(memberId);
            QString remark = QInputDialog::getText(this, "设置备注", "备注名称:", QLineEdit::Normal, oldRemark, &ok).trimmed();
            if (!ok) return;
            if (remark.isEmpty()) {
                ui->statusbar->showMessage("备注名称不能为空", 1800);
                return;
            }
            if (remark == oldRemark) {
                ui->statusbar->showMessage("备注未改变", 1600);
                return;
            }
            m_friendNames[memberId] = remark;
            if (m_friendIds.contains(memberId)) {
                saveFriends();
                ui->statusbar->showMessage(QString("已设置备注：%1").arg(remark), 2200);
            } else {
                ui->statusbar->showMessage(QString("已为群成员 %1 设置临时备注，未改变好友关系").arg(memberId), 2600);
            }
            refreshFriendList();
            refreshGroupMemberPanel();
            appendSystemMessage(QString("已设置 %1 的备注为 %2").arg(memberId, remark));
        } else if (requestProfileAction && selected == requestProfileAction) {
            if (!m_client || !m_client->requestServerGroupMemberProfile("public", memberId)) {
                ui->statusbar->showMessage("成员资料请求失败，请检查连接", 2400);
                return;
            }
            ui->statusbar->showMessage("成员资料请求已提交", 1800);
        } else if (muteAction && selected == muteAction) {
            if (!plan.canManageGroup) {
                ui->statusbar->showMessage(plan.removeDeniedMessage, 2400);
                return;
            }
            const qint64 mutedUntil = QDateTime::currentMSecsSinceEpoch() + 10LL * 60 * 1000;
            if (!m_client || !m_client->sendServerGroupMemberMute("public", memberId, mutedUntil, QStringLiteral("Qt 管理菜单禁言 10 分钟"))) {
                ui->statusbar->showMessage("禁言提交失败，请检查连接", 2400);
                return;
            }
            ui->statusbar->showMessage("禁言请求已提交", 1800);
        } else if (unmuteAction && selected == unmuteAction) {
            if (!plan.canManageGroup) {
                ui->statusbar->showMessage(plan.removeDeniedMessage, 2400);
                return;
            }
            if (!m_client || !m_client->sendServerGroupMemberUnmute("public", memberId)) {
                ui->statusbar->showMessage("解除禁言提交失败，请检查连接", 2400);
                return;
            }
            ui->statusbar->showMessage("解除禁言请求已提交", 1800);
        } else if (promoteAdminAction && selected == promoteAdminAction) {
            if (!plan.canSetPublicAdmin) {
                ui->statusbar->showMessage(plan.promoteDeniedMessage, 2400);
                return;
            }
            requestServerGroupMemberUpdate(memberId, "promote_admin");
        } else if (demoteAdminAction && selected == demoteAdminAction) {
            if (!plan.canSetPublicAdmin) {
                ui->statusbar->showMessage(plan.demoteDeniedMessage, 2400);
                return;
            }
            requestServerGroupMemberUpdate(memberId, "demote_admin");
        } else if (selected == removeAction) {
            if (!plan.canManageGroup) {
                ui->statusbar->showMessage(plan.removeDeniedMessage, 2400);
                return;
            }
            if (memberId == plan.ownerId) {
                ui->statusbar->showMessage(plan.ownerRemoveDeniedMessage, 2200);
                return;
            }
            const QString memberName = contactDisplayName(memberId);
            const QString groupName = isLocalGroup ? m_localGroupNames.value(m_privateChatTarget, "群聊") : m_serverGroupNames.value("public", "公共聊天室");
            if (QMessageBox::question(this,
                                      "移出群成员",
                                      QString("确定将“%1”移出群聊“%2”吗？").arg(memberName, groupName),
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No) != QMessageBox::Yes) {
                ui->statusbar->showMessage("已取消移出群成员", 1600);
                return;
            }
            if (isLocalGroup) {
                m_localGroupMembers[m_privateChatTarget].removeAll(memberId);
                saveLocalGroups();
                refreshGroupMemberPanel();
                appendSystemMessage(QString("已将 %1 移出群聊").arg(memberName));
            } else {
                requestServerGroupMemberUpdate(memberId, "remove");
            }
        }
    });
    connect(ui->clearBtn, &QPushButton::clicked, this, &MainWindow::onClearHistory);
    ui->announcementTitleLabel->setText("群公告");
    ui->announcementTitleLabel->setTextFormat(Qt::RichText);
    ui->announcementTitleLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    refreshGroupMemberPanel();
}

void MainWindow::refreshComposerState() {
    const QString draftText = ui->messageEdit->toPlainText().trimmed();
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    ComposerContext context;
    context.draftText = draftText;
    context.targetName = targetName;
    context.localGroup = isLocalGroup;
    context.removedFromPublicGroup = m_privateChatTarget.isEmpty() && isCurrentUserRemovedFromPublicGroup();
    context.clientConnected = m_client && m_client->isConnected();
    context.encryptedReady = !m_privateChatTarget.isEmpty()
        && !isLocalGroup
        && m_client
        && m_client->hasE2ESession(m_privateChatTarget)
        && m_client->e2ePeerIdentityTrusted(m_privateChatTarget)
        && !m_client->e2eSessionNeedsRotation(m_privateChatTarget);
    const ComposerUiState state = ComposerManager::uiState(context);

    ui->sendBtn->setEnabled(state.canSend);
    ui->sendBtn->setToolTip(state.sendToolTip);
    ui->messageEdit->setPlaceholderText(state.messagePlaceholder);
    ui->messageEdit->setToolTip(state.messageToolTip);
    ui->fileBtn->setEnabled(state.sendFileEnabled);
    ui->fileBtn->setToolTip(state.fileToolTip);
    ui->imageBtn->setEnabled(state.sendImageEnabled);
    ui->imageBtn->setToolTip(state.imageToolTip);

    QString composerStateText = state.canSend
        ? QString("发送目标：%1 · 输入区已就绪").arg(targetName)
        : QString("发送目标：%1 · %2").arg(targetName, state.sendToolTip);
    if (!draftText.isEmpty()) {
        composerStateText += QString(" · 草稿 %1 字").arg(draftText.size());
    }
    ui->composerStateLabel->setText(composerStateText);
    refreshSessionSummary();
}

void MainWindow::refreshWorkspaceChrome() {
    const bool connected = m_client && m_client->isConnected();
    const bool inPublicSession = m_privateChatTarget.isEmpty();
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    const QString sessionName = inPublicSession ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget);
    const QString sessionKind = inPublicSession
        ? QStringLiteral("公共群会话")
        : (isLocalGroup ? QStringLiteral("本地群会话") : QStringLiteral("私聊会话"));
    const QString statusText = QString("%1 · %2")
        .arg(sessionName, sessionKind);

    ui->chatStatusLabel->setText(statusText);
    ui->chatStatusLabel->setToolTip(connected
        ? QString("Redis 就绪工作流已接入 · %1").arg(statusText)
        : QString("等待重连，当前处于只读/暂缓发送态 · %1").arg(statusText));
}

void MainWindow::refreshSessionSummary() {
    const bool inPublicSession = m_privateChatTarget.isEmpty();
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    const QString sessionName = inPublicSession ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget);

    QString overviewState;
    if (inPublicSession) {
        overviewState = isCurrentUserRemovedFromPublicGroup()
            ? QStringLiteral("公共群当前为只读历史态")
            : QStringLiteral("公共群在线成员与公告联动刷新中");
    } else if (isLocalGroup) {
        overviewState = QString("本地群聊 %1 · 成员 %2").arg(sessionName).arg(m_localGroupMembers.value(m_privateChatTarget).size());
    } else {
        overviewState = QString("私聊对象：%1 · %2")
            .arg(sessionName, isContactOnline(m_privateChatTarget) ? QStringLiteral("在线") : QStringLiteral("离线"));
    }

    ui->chatHintLabel->setText(overviewState);
    refreshWorkspaceChrome();
    updateEmptyStateVisibility();
}

void MainWindow::updateEmptyStateVisibility() {
    const bool hasSession = !m_privateChatTarget.isEmpty() || m_knownUsers.size() >= 0;
    // Public chat is always considered an active session once the client is initialized.
    Q_UNUSED(hasSession)
    if (ui->chatStackedWidget) {
        if (m_privateChatTarget.isEmpty()) {
            ui->chatStackedWidget->setCurrentWidget(ui->chatListPage);
        } else {
            ui->chatStackedWidget->setCurrentWidget(ui->chatListPage);
        }
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress
        && (watched == ui->contactSearchEdit || watched == ui->memberSearchEdit)) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
            if (edit && !edit->text().isEmpty()) {
                edit->clear();
                ui->statusbar->showMessage(watched == ui->contactSearchEdit ? "联系人搜索已清空" : "成员搜索已清空", 1400);
                return true;
            }
        }
    }

    if (watched == ui->messageEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            if (!ui->messageEdit->toPlainText().trimmed().isEmpty()) {
                ui->messageEdit->clear();
                ui->statusbar->showMessage("输入草稿已清空", 1400);
                return true;
            }
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            const bool wantsNewLine = keyEvent->modifiers().testFlag(Qt::ControlModifier)
                || keyEvent->modifiers().testFlag(Qt::ShiftModifier);
            if (wantsNewLine) {
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
    QAction* copySummaryAction = new QAction("复制账号摘要", this);
    QAction* copyMediaPackAction = new QAction("复制媒体发送包", this);
    QAction* copyFullMediaPlanAction = new QAction("复制完整媒体计划", this);
    QAction* sendImageAction = new QAction("发送图片/视频", this);
    QAction* sendFileAction = new QAction("闪传文件", this);
    QAction* logoutAction = new QAction("退出登录", this);
    QAction* quitAction = new QAction("退出", this);
    m_trayMenu->addAction(showAction);
    m_trayMenu->addAction(copyAccountAction);
    m_trayMenu->addAction(copySummaryAction);
    m_trayMenu->addAction(copyMediaPackAction);
    m_trayMenu->addAction(copyFullMediaPlanAction);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(sendImageAction);
    m_trayMenu->addAction(sendFileAction);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(logoutAction);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(quitAction);

    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setToolTip("QtNetworkChat");
    m_trayIcon->setIcon(windowIcon().isNull() ? createChatIcon(m_currentUserName) : windowIcon());

    connect(showAction, &QAction::triggered, this, [this]() {
        this->show();
        this->raise();
        this->activateWindow();
    });
    connect(copyAccountAction, &QAction::triggered, this, &MainWindow::onCopyAccount);
    connect(copySummaryAction, &QAction::triggered, this, [this]() {
        QString summary = QString("账号摘要\nQQ:%1\n昵称:%2\n在线状态:在线\n好友:%3\n群聊:%4\n当前会话:%5")
            .arg(m_currentUserId,
                 m_currentUserName,
                 QString::number(m_friendIds.size()),
                 QString::number(m_localGroupIds.size()),
                 m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget));
        QApplication::clipboard()->setText(summary);
        ui->statusbar->showMessage("托盘账号摘要已复制", 2200);
    });
    connect(copyMediaPackAction, &QAction::triggered, this, [this]() {
        QString sessionName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
        QString sessionId = m_privateChatTarget;
        if (sessionId.startsWith("local_group_")) sessionId = sessionId.mid(QString("local_group_").size());
        if (sessionId.isEmpty()) sessionId = "public";
        QStringList rows;
        rows << QString("托盘媒体发送包 · 会话:%1 · 会话号:%2").arg(sessionName, sessionId);
        rows << QString("QQ:%1 · 昵称:%2 · 好友:%3 · 群聊:%4").arg(m_currentUserId, m_currentUserName, QString::number(m_friendIds.size()), QString::number(m_localGroupIds.size()));
        rows << "发送图片/视频：托盘菜单直接点击发送图片/视频";
        rows << "闪传文件：托盘菜单直接点击闪传文件";
        rows << "支持 png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm 和常用文档压缩包";
        rows << QString("查收话术：我已通过 QtNetworkChat 发送媒体到 %1，请注意查收。").arg(sessionName);
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("托盘媒体发送包已复制", 2200);
    });
    connect(copyFullMediaPlanAction, &QAction::triggered, this, [this]() {
        QString sessionName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
        QString sessionId = m_privateChatTarget;
        if (sessionId.startsWith("local_group_")) sessionId = sessionId.mid(QString("local_group_").size());
        if (sessionId.isEmpty()) sessionId = "public";
        QStringList rows;
        rows << QString("托盘完整媒体计划 · 当前会话:%1 · 会话号:%2").arg(sessionName, sessionId);
        rows << QString("我的QQ:%1 · 昵称:%2 · 好友:%3 · 群聊:%4 · 在线:%5")
            .arg(m_currentUserId, m_currentUserName, QString::number(m_friendIds.size()), QString::number(m_localGroupIds.size()), QString::number(m_knownUsers.size()));
        rows << "1. 用综合搜索或好友申请确认目标 QQ、好友或群聊";
        rows << "2. 从托盘直接复制媒体包，或打开窗口后发送图片/视频、闪传文件";
        rows << "3. 发送后聊天记录可右键复制媒体卡片、查收话术、回执话术和保存路径";
        rows << "4. 支持 png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm 和常用文档压缩包";
        rows << QString("当前查收话术：我已准备发送媒体文件到 %1，请注意查收。").arg(sessionName);
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("托盘完整媒体计划已复制", 2200);
    });
    connect(sendImageAction, &QAction::triggered, this, &MainWindow::onSendImage);
    connect(sendFileAction, &QAction::triggered, this, &MainWindow::onSendFile);
    connect(logoutAction, &QAction::triggered, this, &MainWindow::onLogout);
    connect(quitAction, &QAction::triggered, this, [this]() {
        m_isQuitting = true;
        close();
    });
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);
}

void MainWindow::setupNavPanel() {
    if (!m_navGroup) {
        m_navGroup = new QButtonGroup(this);
        m_navGroup->setExclusive(true);
    }

    const QList<QPushButton*> navButtons = {
        ui->navMessages,
        ui->navContacts,
        ui->navSpace,
        ui->navChannel,
        ui->navMail,
        ui->navDocs,
        ui->navCalendar,
        ui->navMeeting,
        ui->navFavorites,
        ui->navWallet,
        ui->navSettings,
        ui->navProfile
    };

    for (QPushButton* btn : navButtons) {
        if (!btn) continue;
        btn->setProperty("navBtn", true);
        btn->setFlat(true);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIconSize(QSize(22, 22));
        btn->setMaximumSize(QSize(48, 48));
        if (m_navGroup) m_navGroup->addButton(btn);
        connect(btn, &QPushButton::clicked, this, [this, btn]() { updateNavActiveState(btn); });
    }

    if (ui->navMessages) updateNavActiveState(ui->navMessages);
}

void MainWindow::updateNavActiveState(QPushButton* activeNav) {
    const QList<QPushButton*> navButtons = {
        ui->navMessages,
        ui->navContacts,
        ui->navSpace,
        ui->navChannel,
        ui->navMail,
        ui->navDocs,
        ui->navCalendar,
        ui->navMeeting,
        ui->navFavorites,
        ui->navWallet,
        ui->navSettings,
        ui->navProfile
    };
    for (QPushButton* btn : navButtons) {
        if (!btn) continue;
        btn->setProperty("navActive", (btn == activeNav));
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
}

void MainWindow::on_actionTheme_triggered() {
    const QString currentTheme = property("theme").toString();
    const QString nextTheme = (currentTheme == QStringLiteral("dark")) ? QStringLiteral("light") : QStringLiteral("dark");
    setProperty("theme", nextTheme);

    QPalette pal = palette();
    if (nextTheme == QStringLiteral("dark")) {
        pal.setColor(QPalette::Window, QColor("#1e1e1e"));
        pal.setColor(QPalette::Base, QColor("#262626"));
        pal.setColor(QPalette::AlternateBase, QColor("#252525"));
        pal.setColor(QPalette::Text, QColor("#e8e8e8"));
    } else {
        pal.setColor(QPalette::Window, QColor("#ffffff"));
        pal.setColor(QPalette::Base, QColor("#ffffff"));
        pal.setColor(QPalette::AlternateBase, QColor("#f5f6f7"));
        pal.setColor(QPalette::Text, QColor("#1f2329"));
    }
    setPalette(pal);

    style()->unpolish(this);
    style()->polish(this);
    for (QWidget* w : findChildren<QWidget*>()) {
        if (w) {
            w->style()->unpolish(w);
            w->style()->polish(w);
        }
    }
    if (ui->userListView) ui->userListView->viewport()->update();
    if (ui->chatListView) ui->chatListView->viewport()->update();
    if (ui->groupMemberListView) ui->groupMemberListView->viewport()->update();
    update();
    repaint();
    saveTheme();
    ui->statusbar->showMessage(nextTheme == QStringLiteral("dark") ? "已切换为深色主题" : "已切换为浅色主题", 1600);
}

void MainWindow::saveTheme() const {
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("Theme"));
    settings.setValue(QStringLiteral("theme"), property("theme").toString());
}

void MainWindow::loadTheme() {
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("Theme"));
    const QString savedTheme = settings.value(QStringLiteral("theme"), QStringLiteral("light")).toString();
    setProperty("theme", savedTheme);

    QPalette pal = palette();
    if (savedTheme == QStringLiteral("dark")) {
        pal.setColor(QPalette::Window, QColor("#1e1e1e"));
        pal.setColor(QPalette::Base, QColor("#262626"));
        pal.setColor(QPalette::AlternateBase, QColor("#252525"));
        pal.setColor(QPalette::Text, QColor("#e8e8e8"));
    } else {
        pal.setColor(QPalette::Window, QColor("#ffffff"));
        pal.setColor(QPalette::Base, QColor("#ffffff"));
        pal.setColor(QPalette::AlternateBase, QColor("#f5f6f7"));
        pal.setColor(QPalette::Text, QColor("#1f2329"));
    }
    setPalette(pal);
}

void MainWindow::onSendMessage() {
    QString text = ui->messageEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        ui->messageEdit->setFocus();
        ui->statusbar->showMessage("请输入消息内容后再发送", 1800);
        return;
    }

    const QString originalText = text;
    if (text == "/card" || text == "名片") {
        text = QString("我的名片：%1（QQ:%2） · 好友%3 · 群聊%4")
            .arg(m_currentUserName, m_currentUserId, QString::number(m_friendIds.size()), QString::number(m_localGroupIds.size()));
    } else if (text == "/invite" || text == "邀请") {
        text = m_privateChatTarget.startsWith("local_group_")
            ? QString("邀请加入群聊“%1”，我是 %2（QQ:%3），进群后一起沟通。")
                .arg(m_localGroupNames.value(m_privateChatTarget, "群聊"), m_currentUserName, m_currentUserId)
            : QString("你好，我是 %1（QQ:%2），方便的话加个好友继续聊。")
                .arg(m_currentUserName, m_currentUserId);
    } else if (text == "/qq" || text == "QQ") {
        text = QString("我的 QQ 号：%1，昵称：%2").arg(m_currentUserId, m_currentUserName);
    } else if (text == "/summary" || text == "摘要") {
        if (m_privateChatTarget.startsWith("local_group_")) {
            text = QString("当前群聊：%1（群号:%2）· 成员%3人 · 我的QQ:%4")
                .arg(m_localGroupNames.value(m_privateChatTarget, "群聊"),
                     m_privateChatTarget.mid(QString("local_group_").size()),
                     QString::number(m_localGroupMembers.value(m_privateChatTarget).size()),
                     m_currentUserId);
        } else if (!m_privateChatTarget.isEmpty()) {
            text = QString("当前私聊：%1 · QQ:%2 · %3 · 我的QQ:%4")
                .arg(contactDisplayName(m_privateChatTarget), m_privateChatTarget, isContactOnline(m_privateChatTarget) ? "在线" : "离线", m_currentUserId);
        } else {
            text = QString("公共聊天室 · 我的QQ:%1 · 好友%2人 · 群聊%3个 · 在线成员%4人")
                .arg(m_currentUserId).arg(m_friendIds.size()).arg(m_localGroupIds.size()).arg(m_knownUsers.size());
        }
    } else if (text == "/file" || text == "文件") {
        text = QString("我准备发送文件，请注意查收。我的QQ:%1，当前会话:%2")
            .arg(m_currentUserId, m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget));
    } else if (text == "/image" || text == "图片") {
        text = QString("我准备发送图片，发送后可查看预览卡片。我的QQ:%1")
            .arg(m_currentUserId);
    } else if (text == "/video" || text == "视频") {
        text = QString("我准备发送视频，视频会以文件卡片形式发送，请注意查收。我的QQ:%1")
            .arg(m_currentUserId);
    }

    QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        QString groupName = m_localGroupNames.value(m_privateChatTarget, "群聊");
        QString line = QString("[%1] <%2> %3").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), m_currentUserName, text);
        const QString clientMessageId = QStringLiteral("qt-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        saveHistory(m_privateChatTarget, line);

        QStandardItem* item = new QStandardItem(line);
        item->setEditable(false);
        decorateChatItem(item, m_currentUserId, m_currentUserName, true);
        item->setData(clientMessageId, ChatMessageIdRole);
        item->setData(m_privateChatTarget, ChatSessionIdRole);
        m_chatModel->appendRow(item);
        ui->messageEdit->clear();
        ui->chatHintLabel->setText(QString("本地群聊 · %1 · 已发送 %2 字%3").arg(groupName).arg(text.size()).arg(originalText == text ? QString() : " · 快捷指令已展开"));
        ui->statusbar->showMessage(QString("已发送到 %1 · %2 字").arg(groupName).arg(text.size()), 1800);
        ui->chatListView->scrollToBottom();
        return;
    }

    if (m_privateChatTarget.isEmpty() && isCurrentUserRemovedFromPublicGroup()) {
        ui->messageEdit->setFocus();
        ui->chatHintLabel->setText("发送暂停 · 当前账号已不在公共群，等待重新邀请");
        ui->statusbar->showMessage("当前账号已不在公共群，暂不能发送公共群消息", 3000);
        refreshComposerState();
        return;
    }

    if (!m_client || !m_client->isConnected()) {
        ui->messageEdit->setFocus();
        ui->chatHintLabel->setText(QString("发送暂停 · %1 已断开，消息已保留在输入框").arg(targetName));
        ui->statusbar->showMessage(QString("已断开连接，暂不能发送到 %1").arg(targetName), 3000);
        refreshComposerState();
        return;
    }

    bool ok = false;
    bool sentEncrypted = false;
    QString encryptedRejectReason;
    const QString clientMessageId = QStringLiteral("qt-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!m_privateChatTarget.isEmpty()) {
        if (m_client->hasE2ESession(m_privateChatTarget) && !m_client->e2eSessionNeedsRotation(m_privateChatTarget)) {
            ok = m_client->sendEncryptedPrivateMessage(m_privateChatTarget, text, &encryptedRejectReason);
            sentEncrypted = ok;
            if (!ok && encryptedRejectReason == QLatin1String("rotation-required")) {
                ui->chatHintLabel->setText(QString("发送暂停 · %1 的端到端会话需要轮换").arg(targetName));
                ui->statusbar->showMessage("端到端加密会话需要轮换，消息已保留在输入框", 3600);
                appendSystemMessage(QString("%1 的端到端加密会话需要轮换，未发送明文").arg(targetName));
                return;
            }
        } else {
            ok = m_client->sendPrivateMessage(m_privateChatTarget, text, clientMessageId);
        }
    } else {
        ok = m_hasServerGroupSnapshot
            ? m_client->sendServerGroupMessage(QStringLiteral("public"), text, clientMessageId)
            : m_client->sendMessage(text);
    }

    if (ok) {
        QString peerId = m_privateChatTarget.isEmpty() ? "group" : m_privateChatTarget;
        QString line = QString("[%1] <%2> %3%4").arg(QDateTime::currentDateTime().toString("hh:mm:ss"),
                                                     m_currentUserName,
                                                     sentEncrypted ? QStringLiteral("[端到端加密] ") : QString(),
                                                     text);
        const QJsonObject e2eStatus = sentEncrypted ? m_client->e2eSessionStatus(peerId) : QJsonObject();
        saveHistory(peerId,
                    line,
                    sentEncrypted ? QStringLiteral("encrypted") : QStringLiteral("plaintext"),
                    e2eStatus.value("keyId").toString(),
                    e2eStatus.value("keyFingerprintSha256").toString());

        QStandardItem* item = new QStandardItem(line);
        item->setEditable(false);
        decorateChatItem(item, m_currentUserId, m_currentUserName, true);
        item->setData(clientMessageId, ChatMessageIdRole);
        item->setData(m_privateChatTarget.isEmpty() ? QStringLiteral("public") : peerId, ChatSessionIdRole);
        m_chatModel->appendRow(item);
        int rowCount = m_chatModel->rowCount();
        if (rowCount > MAX_HISTORY_LINES) {
            m_chatModel->removeRows(0, rowCount - MAX_HISTORY_LINES);
        }
        ui->chatListView->scrollToBottom();
        ui->chatHintLabel->setText(QString("已发送到 %1 · %2 字 · %3%4%5")
            .arg(targetName)
            .arg(text.size())
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss"),
                 originalText == text ? QString() : " · 快捷指令已展开",
                 sentEncrypted ? QStringLiteral(" · 端到端加密") : QString()));
        ui->statusbar->showMessage(QString("已发送到 %1 · %2 字%3").arg(targetName).arg(text.size()).arg(sentEncrypted ? QStringLiteral(" · 端到端加密") : QString()), 1800);

        ui->messageEdit->clear();
    } else {
        ui->chatHintLabel->setText(QString("发送失败 · 目标 %1 · 消息已保留在输入框").arg(targetName));
        ui->statusbar->showMessage(QString("发送失败，请检查连接 · %1").arg(targetName), 3000);
        appendSystemMessage(QString("发送失败，消息未送达 %1").arg(targetName));
    }
}

void MainWindow::onSendFile() {
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    if (!ensureTransferTargetReady(QStringLiteral("文件"), targetName, isLocalGroup)) {
        return;
    }

    const TransferSelectionPlan selectionPlan = m_transferManager.fileSelectionPlan();
    SelectedTransferFile selectedFile;
    if (!selectTransferFileContext(selectionPlan, &selectedFile)) {
        return;
    }

    const TransferSendUiState preparingState =
        m_transferManager.preparingSendState(selectionPlan.preparingKind,
                                             selectedFile.info.fileName(),
                                             selectedFile.fileSize,
                                             targetName);
    applyTransferSendState(preparingState);
    const QString completedAt = QDateTime::currentDateTime().toString("hh:mm:ss");
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        appendLocalGroupFileTransferCompletion(selectionPlan,
                                               selectedFile.info,
                                               selectedFile.fileSize,
                                               targetName,
                                               completedAt);
        return;
    }

    QString transferSummary;
    bool transferCanceled = false;
    bool ok = sendTransferWithProgress(selectedFile.filePath,
                                       m_privateChatTarget,
                                       targetName,
                                       "文件",
                                       false,
                                       &transferSummary,
                                       &transferCanceled);
    updateSavedOutgoingTransferRecoveryUi(!ok && !transferCanceled);
    handleRemoteTransferResult(ok,
                               transferCanceled,
                               selectedFile.filePath,
                               selectedFile.info,
                               selectedFile.fileSize,
                               targetName,
                               selectionPlan.preparingKind,
                               false,
                               false,
                               transferSummary);
}

void MainWindow::onSendImage() {
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    if (!ensureTransferTargetReady(QStringLiteral("图片/视频"), targetName, isLocalGroup)) {
        return;
    }

    const TransferSelectionPlan selectionPlan = m_transferManager.mediaSelectionPlan();
    SelectedTransferFile selectedFile;
    if (!selectTransferFileContext(selectionPlan, &selectedFile)) {
        return;
    }

    const TransferMediaSelection mediaSelection = m_transferManager.mediaSelection(selectedFile.info);
    const bool isVideo = mediaSelection.isVideo;
    const QString mediaType = mediaSelection.mediaType;
    const TransferSendUiState preparingState =
        m_transferManager.preparingSendState(mediaType,
                                             selectedFile.info.fileName(),
                                             selectedFile.fileSize,
                                             targetName);
    applyTransferSendState(preparingState);
    const QString completedAt = QDateTime::currentDateTime().toString("hh:mm:ss");
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        appendLocalGroupMediaTransferCompletion(selectedFile.filePath,
                                                selectedFile.info,
                                                selectedFile.fileSize,
                                                mediaType,
                                                isVideo,
                                                targetName,
                                                completedAt);
        return;
    }

    QString transferSummary;
    bool transferCanceled = false;
    bool ok = sendTransferWithProgress(selectedFile.filePath,
                                       m_privateChatTarget,
                                       targetName,
                                       mediaType,
                                       !isVideo,
                                       &transferSummary,
                                       &transferCanceled);
    updateSavedOutgoingTransferRecoveryUi(!ok && !transferCanceled);
    handleRemoteTransferResult(ok,
                               transferCanceled,
                               selectedFile.filePath,
                               selectedFile.info,
                               selectedFile.fileSize,
                               targetName,
                               mediaType,
                               true,
                               isVideo,
                               transferSummary);
}

void MainWindow::onNewMessage(const Message& msg) {
    QString displayName = msg.senderName;
    const QString avatarUserId = msg.senderId == m_currentUserId ? m_currentUserId : msg.senderId;
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
        const QString historyPeerId = msg.isPrivate() ? (msg.senderId == m_currentUserId ? msg.receiverId : msg.senderId) : "group";
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
        ui->chatListView->scrollToBottom();
        return;
    }

    const QString historyPeerId = msg.isPrivate() ? (msg.senderId == m_currentUserId ? msg.receiverId : msg.senderId) : "group";
    QStandardItem* item = new QStandardItem(line);
    item->setEditable(false);
    decorateChatItem(item, avatarUserId, displayName, msg.senderId == m_currentUserId);
    const QString incomingMessageId = msg.clientMessageId.trimmed().isEmpty()
        ? compactMessageId(QStringLiteral("%1|%2|%3|%4")
              .arg(msg.receiverId, msg.senderId, timeStr, msg.content.left(128)))
        : msg.clientMessageId.trimmed();
    item->setData(incomingMessageId, ChatMessageIdRole);
    item->setData(msg.isPrivate() ? historyPeerId : msg.receiverId.trimmed().isEmpty() ? QStringLiteral("public") : msg.receiverId.trimmed(), ChatSessionIdRole);
    m_chatModel->appendRow(item);
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

    if (!isActiveWindow()) {
        ++m_unreadCount;
        updateUnreadState();
        if (m_trayIcon->isVisible()) {
            QString preview = msg.type == MessageType::File ? msg.content : msg.content.left(60);
            if (msg.type == MessageType::Image) preview = "[图片] " + msg.fileName;
            m_trayIcon->showMessage("QtNetworkChat", QString("%1: %2").arg(displayName, preview), QSystemTrayIcon::Information, 3000);
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
    m_serverGroupMemberMutedUntil.clear();
    m_serverGroupAuditEvents.clear();
    m_removedServerGroups.clear();
    m_serverGroupEssenceMessageKeys.clear();

    for (const QJsonValue& value : groups) {
        const QJsonObject groupObj = value.toObject();
        const QString groupId = groupObj["groupId"].toString();
        if (groupId.isEmpty()) continue;

        m_serverGroupNames[groupId] = groupObj["groupName"].toString(groupId);
        m_serverGroupAnnouncements[groupId] = groupObj["announcement"].toString();
        m_serverGroupOwners[groupId] = groupObj["ownerId"].toString();

        QStringList memberIds;
        const QJsonArray members = groupObj["members"].toArray();
        for (const QJsonValue& memberValue : members) {
            const QJsonObject memberObj = memberValue.toObject();
            const QString memberId = memberObj["userId"].toString();
            if (memberId.isEmpty() || memberIds.contains(memberId)) continue;

            memberIds << memberId;
            m_serverGroupMemberNames[groupId + "|" + memberId] = memberObj["userName"].toString(memberId);
            m_serverGroupMemberRoles[groupId + "|" + memberId] = memberObj["role"].toString("member");
            const qint64 mutedUntil = memberObj["mutedUntil"].toVariant().toLongLong();
            if (mutedUntil > QDateTime::currentMSecsSinceEpoch()) {
                m_serverGroupMemberMutedUntil[groupId + "|" + memberId] = mutedUntil;
            }
        }
        m_serverGroupMembers[groupId] = memberIds;
        const QJsonArray essenceMessages = groupObj["essenceMessages"].toArray();
        for (const QJsonValue& essenceValue : essenceMessages) {
            const QJsonObject essenceObj = essenceValue.toObject();
            const QString messageId = essenceObj["messageId"].toString().trimmed();
            if (!messageId.isEmpty() && essenceObj["enabled"].toBool(true)) {
                m_serverGroupEssenceMessageKeys.insert(groupId + QLatin1Char('|') + messageId);
            }
        }
        m_serverGroupAuditEvents[groupId] = groupObj["auditEvents"].toArray();
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
    QString targetId = index.data(Qt::UserRole + 1).toString();
    if (targetId.isEmpty()) return;
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

void MainWindow::onClearHistory() {
    const QString peerId = m_privateChatTarget.isEmpty() ? "group" : m_privateChatTarget;
    const QString historyPath = m_historyService.legacyFilePath(peerId);
    const QString sessionName = m_privateChatTarget.isEmpty()
        ? "公共聊天室"
        : (m_privateChatTarget.startsWith("local_group_")
            ? m_localGroupNames.value(m_privateChatTarget, "群聊")
            : contactDisplayName(m_privateChatTarget));

    if (m_chatModel->rowCount() == 0 && !QFile::exists(historyPath) && !m_historyService.hasRecords(peerId)) {
        ui->statusbar->showMessage(QString("%1 暂无可清空的聊天记录").arg(sessionName), 1800);
        return;
    }

    if (QMessageBox::question(this,
                              "清空聊天记录",
                              QString("确定清空“%1”的本地聊天记录吗？此操作不会删除对方设备上的记录。").arg(sessionName),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        ui->statusbar->showMessage("已取消清空聊天记录", 1600);
        return;
    }

    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    if (QFile::exists(historyPath)) {
        QFile::remove(historyPath);
    }
    m_historyService.clear(peerId);
    appendSystemMessage(QString("%1 的聊天记录已清空").arg(sessionName));
    ui->statusbar->showMessage(QString("已清空 %1 的本地聊天记录").arg(sessionName), 2200);
}

void MainWindow::onFilterHistoryByDate() {
    const QString peerId = m_privateChatTarget.isEmpty() ? "group" : m_privateChatTarget;
    const QString sessionName = m_privateChatTarget.isEmpty()
        ? "公共聊天室"
        : (m_privateChatTarget.startsWith("local_group_")
            ? m_localGroupNames.value(m_privateChatTarget, "群聊")
            : contactDisplayName(m_privateChatTarget));

    QDialog dialog(this);
    dialog.setWindowTitle("按日期查记录");
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* label = new QLabel(QString("选择要查看的日期：%1").arg(sessionName), &dialog);
    QDateEdit* dateEdit = new QDateEdit(QDate::currentDate(), &dialog);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat("yyyy-MM-dd");
    dateEdit->setMaximumDate(QDate::currentDate());
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(label);
    layout->addWidget(dateEdit);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        ui->statusbar->showMessage("已取消按日期查记录", 1600);
        return;
    }

    const QDate selectedDate = dateEdit->date();
    const QStringList rows = m_historyService.rowsForDate(peerId, selectedDate);
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});

    if (rows.isEmpty()) {
        appendSystemMessage(QString("%1 在 %2 没有可显示的聊天记录").arg(sessionName, selectedDate.toString("yyyy-MM-dd")));
        ui->statusbar->showMessage(QString("%1 无当天记录").arg(selectedDate.toString("yyyy-MM-dd")), 2200);
        return;
    }

    for (const QString& row : rows) {
        QStandardItem* item = new QStandardItem(row);
        item->setEditable(false);
        item->setBackground(QColor(250, 252, 254));
        item->setForeground(Qt::gray);
        m_chatModel->appendRow(item);
    }
    ui->chatHintLabel->setText(QString("%1 · %2 · 已筛选 %3 条记录")
        .arg(sessionName, selectedDate.toString("yyyy-MM-dd"), QString::number(rows.size())));
    ui->statusbar->showMessage(QString("已筛选 %1 条聊天记录").arg(rows.size()), 2400);
    ui->chatListView->scrollToBottom();
}

void MainWindow::onExportHistory() {
    const QString peerId = m_privateChatTarget.isEmpty() ? "group" : m_privateChatTarget;
    const QString sessionName = m_privateChatTarget.isEmpty()
        ? "公共聊天室"
        : (m_privateChatTarget.startsWith("local_group_")
            ? m_localGroupNames.value(m_privateChatTarget, "群聊")
            : contactDisplayName(m_privateChatTarget));
    const QStringList rows = m_historyService.rowsForExport(peerId);
    if (rows.isEmpty()) {
        ui->statusbar->showMessage(QString("%1 暂无可导出的聊天记录").arg(sessionName), 2200);
        return;
    }

    const QDateTime exportedAt = QDateTime::currentDateTime();
    const HistoryExportSelectionPlan exportPlan =
        m_historyService.exportSelectionPlan(sessionName, exportedAt);
    const QString savePath = QFileDialog::getSaveFileName(this,
                                                          exportPlan.dialogTitle,
                                                          exportPlan.defaultPath,
                                                          exportPlan.filters);
    if (savePath.isEmpty()) {
        ui->statusbar->showMessage(exportPlan.canceledStatusMessage, exportPlan.canceledStatusTimeoutMs);
        return;
    }

    const HistoryExportWriteResult exportResult =
        m_historyService.writeExportFile(savePath,
                                         sessionName,
                                         m_currentUserId,
                                         m_currentUserName,
                                         rows,
                                         exportedAt);
    if (!exportResult.written) {
        QMessageBox::warning(this, exportResult.failureTitle, exportResult.failureMessage);
        ui->statusbar->showMessage(exportResult.failureStatusMessage, exportResult.failureStatusTimeoutMs);
        return;
    }

    ui->statusbar->showMessage(exportResult.successStatusMessage, exportResult.successStatusTimeoutMs);
    appendSystemMessage(exportResult.systemMessage);
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

void MainWindow::onShowGlobalSearch() {
    QDialog dialog(this);
    dialog.setObjectName("globalSearchDialog");
    dialog.setWindowTitle("综合搜索");
    dialog.setFixedSize(940, 740);

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
    searchEdit->setToolTip("输入 QQ 号、昵称或群名；回车可搜索或打开匹配结果");
    QPushButton* searchBtn = new QPushButton("搜索", header);
    searchBtn->setObjectName("globalSearchPrimaryBtn");
    searchBtn->setToolTip("按当前关键词刷新综合搜索结果");
    QPushButton* clearBtn = new QPushButton("清空", header);
    clearBtn->setObjectName("globalSearchGhostBtn");
    clearBtn->setToolTip("清空搜索关键词并恢复全部结果");
    QPushButton* quickAddBtn = new QPushButton("好友申请", header);
    quickAddBtn->setObjectName("globalSearchGhostBtn");
    quickAddBtn->setToolTip("打开好友申请窗口，按 QQ 号搜索并发送申请");
    QPushButton* friendManagerBtn = new QPushButton("好友管理", header);
    friendManagerBtn->setObjectName("globalSearchGhostBtn");
    friendManagerBtn->setToolTip("打开好友管理器，查看、搜索和整理好友");
    searchLayout->addWidget(searchEdit, 1);
    searchLayout->addWidget(searchBtn);
    searchLayout->addWidget(clearBtn);
    searchLayout->addWidget(quickAddBtn);
    searchLayout->addWidget(friendManagerBtn);
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

    QHBoxLayout* actionLayout = new QHBoxLayout;
    actionLayout->setContentsMargins(18, 10, 18, 18);
    QLabel* actionHint = new QLabel("双击结果可聊天、进群或发送好友申请", &dialog);
    actionHint->setObjectName("globalActionHint");
    QLabel* statsLabel = new QLabel(&dialog);
    statsLabel->setObjectName("globalStatsLabel");
    QLabel* previewLabel = new QLabel("选择结果后可复制QQ、名片、邀请卡或直接打开", &dialog);
    previewLabel->setObjectName("globalPreviewLabel");
    QPushButton* openBtn = new QPushButton("打开/申请", &dialog);
    openBtn->setObjectName("globalSearchPrimaryBtn");
    openBtn->setToolTip("打开当前结果；陌生用户会尝试发送好友申请");
    QPushButton* createGroupBtn = new QPushButton("用搜索创建群", &dialog);
    createGroupBtn->setObjectName("globalSearchGhostBtn");
    createGroupBtn->setToolTip("使用当前搜索关键词创建一个本地群聊");
    QPushButton* inviteVisibleBtn = new QPushButton("可见用户建群", &dialog);
    inviteVisibleBtn->setObjectName("globalSearchGhostBtn");
    inviteVisibleBtn->setToolTip("用当前可见用户创建群聊，并邀请可申请用户");
    QPushButton* addVisibleBtn = new QPushButton("申请可见用户", &dialog);
    addVisibleBtn->setObjectName("globalSearchGhostBtn");
    addVisibleBtn->setToolTip("向当前列表里可申请的在线用户批量发送好友申请");
    QPushButton* copyBtn = new QPushButton("复制QQ", &dialog);
    copyBtn->setObjectName("globalSearchGhostBtn");
    copyBtn->setToolTip("复制当前选中结果的 QQ 号或群号");
    QPushButton* copyListBtn = new QPushButton("复制结果列表", &dialog);
    copyListBtn->setObjectName("globalSearchGhostBtn");
    copyListBtn->setToolTip("复制当前可见搜索结果列表");
    QPushButton* copyAddTextBtn = new QPushButton("复制申请话术", &dialog);
    copyAddTextBtn->setObjectName("globalSearchGhostBtn");
    copyAddTextBtn->setToolTip("复制适合当前选中用户的好友申请话术");
    QPushButton* copyInviteCardBtn = new QPushButton("复制邀请卡", &dialog);
    copyInviteCardBtn->setObjectName("globalSearchGhostBtn");
    copyInviteCardBtn->setToolTip("复制当前用户或群聊的邀请卡片");
    QPushButton* copySearchCardBtn = new QPushButton("复制搜索卡片", &dialog);
    copySearchCardBtn->setObjectName("globalSearchGhostBtn");
    copySearchCardBtn->setToolTip("复制当前搜索条件和结果摘要");
    QPushButton* copySearchMediaPackBtn = new QPushButton("复制搜索媒体包", &dialog);
    copySearchMediaPackBtn->setObjectName("globalSearchGhostBtn");
    copySearchMediaPackBtn->setToolTip("复制搜索场景下发送图片、视频或文件的准备摘要");
    QPushButton* copyBatchMediaPlanBtn = new QPushButton("复制批量媒体计划", &dialog);
    copyBatchMediaPlanBtn->setObjectName("globalSearchGhostBtn");
    copyBatchMediaPlanBtn->setToolTip("复制当前搜索结果的批量媒体发送计划");
    QPushButton* copyMediaGuideBtn = new QPushButton("复制上传指南", &dialog);
    copyMediaGuideBtn->setObjectName("globalSearchGhostBtn");
    copyMediaGuideBtn->setToolTip("复制搜索后发送图片、视频和文件的简短指南");
    QPushButton* copyOnlineBtn = new QPushButton("复制在线", &dialog);
    copyOnlineBtn->setObjectName("globalSearchGhostBtn");
    copyOnlineBtn->setToolTip("复制当前可见结果中的在线用户");
    QPushButton* profileBtn = new QPushButton("复制名片", &dialog);
    profileBtn->setObjectName("globalSearchGhostBtn");
    profileBtn->setToolTip("复制当前选中结果的资料名片");
    actionLayout->addWidget(actionHint);
    actionLayout->addWidget(statsLabel);
    actionLayout->addWidget(previewLabel);
    actionLayout->addStretch();
    actionLayout->addWidget(createGroupBtn);
    actionLayout->addWidget(inviteVisibleBtn);
    actionLayout->addWidget(addVisibleBtn);
    actionLayout->addWidget(copyBtn);
    actionLayout->addWidget(copyListBtn);
    actionLayout->addWidget(copyAddTextBtn);
    actionLayout->addWidget(copyInviteCardBtn);
    actionLayout->addWidget(copySearchCardBtn);
    actionLayout->addWidget(copySearchMediaPackBtn);
    actionLayout->addWidget(copyBatchMediaPlanBtn);
    actionLayout->addWidget(copyMediaGuideBtn);
    actionLayout->addWidget(copyOnlineBtn);
    actionLayout->addWidget(profileBtn);
    actionLayout->addWidget(openBtn);
    layout->addLayout(actionLayout);

    auto updatePreview = [this, resultList, previewLabel]() {
        QListWidgetItem* item = resultList->currentItem();
        if (!item) {
            previewLabel->setText("选择结果后可复制QQ、名片、邀请卡或直接打开");
            return;
        }
        QString id = item->data(Qt::UserRole).toString();
        if (id.startsWith("search_add:")) {
            QString account = id.mid(QString("search_add:").size());
            previewLabel->setText(QString("准备搜索 QQ:%1 并发送好友申请").arg(account));
        } else if (id.startsWith("local_group_")) {
            previewLabel->setText(QString("群聊 · %1 · 群号:%2 · 成员%3人").arg(m_localGroupNames.value(id, "群聊"), id.mid(QString("local_group_").size())).arg(m_localGroupMembers.value(id).size()));
        } else if (!id.isEmpty()) {
            QString relation = m_friendIds.contains(id) ? "好友" : (m_pendingOutgoingFriendRequests.contains(id) ? "申请中" : "可申请");
            previewLabel->setText(QString("联系人 · %1 · QQ:%2 · %3 · %4")
                .arg(contactDisplayName(id), id, isContactOnline(id) ? "在线" : "离线", relation));
        } else {
            previewLabel->setText("输入 QQ 号后可继续搜索并发送申请");
        }
    };

    auto fillResults = [this, resultList, actionHint, statsLabel, updatePreview](const QString& filter = QString()) {
        resultList->clear();
        int friendCount = 0;
        int userCount = 0;
        int pendingCount = 0;
        int groupCount = 0;
        for (const QString& id : m_friendIds) {
            QString name = m_friendNames.value(id, id);
            if (!filter.isEmpty()
                && !id.contains(filter, Qt::CaseInsensitive)
                && !name.contains(filter, Qt::CaseInsensitive)) continue;
            QListWidgetItem* item = new QListWidgetItem(QString("好友  QQ:%1\n%2 · %3").arg(id, name, isContactOnline(id) ? "在线" : "离线"));
            item->setData(Qt::UserRole, id);
            item->setSizeHint(QSize(0, 66));
            resultList->addItem(item);
            ++friendCount;
        }
        for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
            const ChatUser& user = it.value();
            if (user.id == m_currentUserId || m_friendIds.contains(user.id)) continue;
            if (!filter.isEmpty()
                && !user.id.contains(filter, Qt::CaseInsensitive)
                && !user.name.contains(filter, Qt::CaseInsensitive)) continue;
            const bool isPending = m_pendingOutgoingFriendRequests.contains(user.id);
            QListWidgetItem* item = new QListWidgetItem(QString("用户  QQ:%1\n%2 · 在线 · %3").arg(user.id, user.name, isPending ? "申请中" : "双击发送申请"));
            item->setData(Qt::UserRole, user.id);
            item->setSizeHint(QSize(0, 66));
            if (isPending) {
                item->setForeground(QColor(170, 110, 20));
                ++pendingCount;
            }
            resultList->addItem(item);
            ++userCount;
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
            ++groupCount;
        }
        if (!filter.isEmpty()) {
            QListWidgetItem* searchItem = new QListWidgetItem(QString("搜索 QQ 账号：%1\n双击或点击搜索可从服务器查找并发送好友申请").arg(filter));
            searchItem->setData(Qt::UserRole, "search_add:" + filter);
            searchItem->setForeground(QColor(92, 110, 128));
            searchItem->setSizeHint(QSize(0, 58));
            resultList->addItem(searchItem);
        }
        if (resultList->count() == 0) {
            QListWidgetItem* emptyItem = new QListWidgetItem("输入 QQ 号搜索用户并发送好友申请");
            emptyItem->setFlags(Qt::NoItemFlags);
            emptyItem->setForeground(QColor(135, 150, 165));
            resultList->addItem(emptyItem);
        }
        int directResultCount = friendCount + userCount + groupCount;
        actionHint->setText(filter.isEmpty()
            ? QString("双击结果可聊天、进群或发送好友申请 · 共%1项").arg(directResultCount)
            : QString("匹配%1项 · 可继续搜索QQ:%2").arg(directResultCount).arg(filter));
        statsLabel->setText(QString("好友%1 · 用户%2 · 申请中%3 · 群聊%4").arg(friendCount).arg(userCount).arg(pendingCount).arg(groupCount));
        if (resultList->count() > 0) resultList->setCurrentRow(0);
        updatePreview();
    };
    fillResults();


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
    auto openResult = [this, &dialog, resultList]() {
        QListWidgetItem* item = resultList->currentItem();
        if (!item) {
            ui->statusbar->showMessage("请先选择要打开的搜索结果", 1800);
            return;
        }
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            ui->statusbar->showMessage("当前没有可打开的搜索结果", 1800);
            return;
        }
        if (id.startsWith("search_add:")) {
            searchAndAddAccount(id.mid(QString("search_add:").size()), &dialog);
            dialog.accept();
            return;
        }
        if (m_localGroupIds.contains(id)) {
            dialog.accept();
            switchToLocalGroup(id, m_localGroupNames.value(id, "群聊"));
            ui->statusbar->showMessage("已进入群聊: " + m_localGroupNames.value(id, "群聊"), 1800);
            return;
        }
        if (!m_friendIds.contains(id) && m_pendingOutgoingFriendRequests.contains(id)) {
            ui->statusbar->showMessage(QString("%1 的好友申请正在等待确认").arg(contactDisplayName(id)), 2200);
        } else {
            ensureFriendRequestQueued(id, QStringLiteral("已从综合搜索向 %1（QQ:%2）发送好友申请"));
        }
        dialog.accept();
        openPrivateSession(id);
        ui->statusbar->showMessage(QString("已打开与 %1 的私聊").arg(contactDisplayName(id)), 1800);
    };

    connect(searchBtn, &QPushButton::clicked, &dialog, runServerSearch);
    connect(resultList, &QListWidget::currentItemChanged, &dialog, [updatePreview](QListWidgetItem*, QListWidgetItem*) { updatePreview(); });
    connect(clearBtn, &QPushButton::clicked, &dialog, [searchEdit, fillResults]() {
        searchEdit->clear();
        fillResults();
        searchEdit->setFocus();
    });
    connect(quickAddBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onShowQuickAddFriend();
    });
    connect(friendManagerBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onShowFriendManager();
    });
    connect(searchEdit, &QLineEdit::returnPressed, &dialog, runServerSearch);
    connect(openBtn, &QPushButton::clicked, &dialog, openResult);
    connect(createGroupBtn, &QPushButton::clicked, &dialog, [this, searchEdit, &dialog]() {
        QString groupName = searchEdit->text().trimmed();
        if (groupName.isEmpty()) groupName = "我的群聊";
        const QString groupId = createLocalGroupSession(groupName);
        dialog.accept();
        switchToLocalGroup(groupId, groupName);
        appendSystemMessage("已从搜索创建群聊: " + groupName);
    });
    connect(inviteVisibleBtn, &QPushButton::clicked, &dialog, [this, resultList, searchEdit, &dialog]() {
        QString groupName = searchEdit->text().trimmed();
        if (groupName.isEmpty()) groupName = "搜索群聊";
        QStringList members;
        QStringList requestIds;
        int pendingSkipped = 0;
        for (int i = 0; i < resultList->count(); ++i) {
            QListWidgetItem* item = resultList->item(i);
            QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty() || id.startsWith("local_group_") || id.startsWith("search_add:") || id == m_currentUserId || members.contains(id)) continue;
            members << id;
            if (!m_friendIds.contains(id)) {
                if (m_pendingOutgoingFriendRequests.contains(id)) {
                    ++pendingSkipped;
                } else {
                    requestIds << id;
                }
            }
        }
        int invitedCount = members.size();
        if (invitedCount == 0) {
            ui->statusbar->showMessage("当前没有可邀请的可见用户", 2200);
            return;
        }
        if (!confirmAction(QStringLiteral("可见用户建群"),
                           QStringLiteral("确定创建群聊“%1”并邀请 %2 位可见用户吗？其中 %3 位会同时发送好友申请。")
                               .arg(groupName)
                               .arg(invitedCount)
                               .arg(requestIds.size()),
                           QStringLiteral("已取消可见用户建群"),
                           1600,
                           &dialog)) {
            return;
        }
        QStringList sentNames;
        QStringList failedNames;
        for (const QString& id : requestIds) {
            const QString displayName = contactDisplayName(id);
            if (ensureFriendRequestQueued(id)) {
                sentNames << QString("%1(%2)").arg(displayName, id);
            } else {
                failedNames << QString("%1(%2)").arg(displayName, id);
            }
        }
        const QString groupId = createLocalGroupSession(
            groupName,
            members,
            QString("%1 已从综合搜索创建，已邀请可见用户。").arg(groupName));
        dialog.accept();
        switchToLocalGroup(groupId, groupName);
        QString detail = QString("已从综合搜索建群并邀请 %1 位可见用户").arg(invitedCount);
        if (!sentNames.isEmpty()) detail += QString("，已发送好友申请 %1 个").arg(sentNames.size());
        if (pendingSkipped > 0) detail += QString("，跳过申请中 %1 个").arg(pendingSkipped);
        if (!failedNames.isEmpty()) detail += QString("，申请失败 %1 个").arg(failedNames.size());
        appendSystemMessage(detail);
    });
    connect(addVisibleBtn, &QPushButton::clicked, &dialog, [this, resultList, searchEdit, fillResults, &dialog]() {
        QStringList addIds;
        int pendingSkipped = 0;
        for (int i = 0; i < resultList->count(); ++i) {
            QListWidgetItem* item = resultList->item(i);
            QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty() || id.startsWith("local_group_") || id.startsWith("search_add:") || id == m_currentUserId || m_friendIds.contains(id)) continue;
            if (m_pendingOutgoingFriendRequests.contains(id)) {
                ++pendingSkipped;
                continue;
            }
            if (!addIds.contains(id)) addIds << id;
        }
        if (addIds.isEmpty()) {
            ui->statusbar->showMessage(pendingSkipped > 0 ? "可见用户均已是好友或申请中" : "当前没有可发送申请的用户", 2200);
            searchEdit->setFocus();
            return;
        }
        if (!confirmAction(QStringLiteral("发送可见用户申请"),
                           QString("确定向 %1 位可见用户发送好友申请吗？").arg(addIds.size()),
                           QStringLiteral("已取消发送可见用户申请"),
                           1600,
                           &dialog)) {
            searchEdit->setFocus();
            return;
        }
        QStringList sentNames;
        QStringList failedNames;
        for (const QString& id : addIds) {
            const QString displayName = contactDisplayName(id);
            if (ensureFriendRequestQueued(id)) {
                sentNames << QString("%1(%2)").arg(displayName, id);
            } else {
                failedNames << QString("%1(%2)").arg(displayName, id);
            }
        }
        if (sentNames.isEmpty()) {
            ui->statusbar->showMessage("可见用户好友申请发送失败", 2600);
            searchEdit->setFocus();
            return;
        }
        refreshFriendList();
        fillResults(searchEdit->text().trimmed());
        QString detail = QString("已向 %1 个可见用户发送好友申请").arg(sentNames.size());
        if (pendingSkipped > 0) detail += QString(" · 已跳过申请中 %1 个").arg(pendingSkipped);
        if (!failedNames.isEmpty()) detail += QString(" · 失败 %1 个").arg(failedNames.size());
        appendSystemMessage(detail);
        ui->statusbar->showMessage(detail, 2800);
        searchEdit->setFocus();
    });
    connect(copyBtn, &QPushButton::clicked, &dialog, [this, resultList]() {
        QListWidgetItem* item = resultList->currentItem();
        if (!item) {
            ui->statusbar->showMessage("请先选择要复制 QQ 的搜索结果", 1800);
            return;
        }
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            ui->statusbar->showMessage("当前没有可复制的 QQ 号", 1800);
            return;
        }
        if (id.startsWith("search_add:")) id = id.mid(QString("search_add:").size());
        if (id.startsWith("local_group_")) id = id.mid(QString("local_group_").size());
        QApplication::clipboard()->setText(id);
        ui->statusbar->showMessage("QQ 号已复制: " + id, 2500);
    });
    auto globalSearchCopyInputs = [this, resultList]() {
        QList<GlobalSearchResultCopyInput> inputs;
        for (int i = 0; i < resultList->count(); ++i) {
            QListWidgetItem* item = resultList->item(i);
            const QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty()) {
                continue;
            }
            GlobalSearchResultCopyInput input;
            input.entryId = id;
            input.localGroup = id.startsWith("local_group_");
            input.friendContact = m_friendIds.contains(id);
            input.online = isContactOnline(id);
            input.memberCount = input.localGroup ? m_localGroupMembers.value(id).size() : 0;
            input.displayName = input.localGroup ? m_localGroupNames.value(id, "群聊") : contactDisplayName(id);
            inputs << input;
        }
        return inputs;
    };
    auto currentGlobalSearchCopyInput = [this, resultList, searchEdit]() {
        GlobalSearchResultCopyInput input;
        QListWidgetItem* item = resultList->currentItem();
        const QString rawId = item ? item->data(Qt::UserRole).toString() : searchEdit->text().trimmed();
        input.entryId = rawId;
        input.localGroup = rawId.startsWith("local_group_");
        input.friendContact = m_friendIds.contains(rawId);
        input.online = isContactOnline(rawId);
        input.memberCount = input.localGroup ? m_localGroupMembers.value(rawId).size() : 0;
        if (input.localGroup) {
            input.displayName = m_localGroupNames.value(rawId, "群聊");
        } else if (!rawId.startsWith("search_add:")) {
            input.displayName = contactDisplayName(rawId);
        }
        return input;
    };
    connect(copyListBtn, &QPushButton::clicked, &dialog, [this, globalSearchCopyInputs]() {
        const GlobalSearchResultCopyState state =
            FriendManager::globalSearchResultCopyState(globalSearchCopyInputs(), false);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return;
        }
        QApplication::clipboard()->setText(state.rows.join('\n'));
        ui->statusbar->showMessage(state.copiedStatusMessage, 2200);
    });
    connect(copyAddTextBtn, &QPushButton::clicked, &dialog, [this, currentGlobalSearchCopyInput, searchEdit]() {
        const QString text = FriendManager::globalSearchInviteText(
            m_currentUserId,
            m_currentUserName,
            currentGlobalSearchCopyInput(),
            searchEdit->text().trimmed());
        QApplication::clipboard()->setText(text);
        ui->statusbar->showMessage("申请/邀请话术已复制", 2200);
    });
    connect(copyInviteCardBtn, &QPushButton::clicked, &dialog, [this, currentGlobalSearchCopyInput, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::globalSearchInviteCardState(
            m_currentUserId,
            m_currentUserName,
            currentGlobalSearchCopyInput(),
            searchEdit->text().trimmed());
        if (!state.valid) {
            ui->statusbar->showMessage("当前没有可复制的搜索邀请卡", 1800);
            return;
        }
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("搜索邀请卡已复制", 2200);
    });
    connect(copySearchCardBtn, &QPushButton::clicked, &dialog, [this, globalSearchCopyInputs, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::globalSearchSummaryCardState(
            m_currentUserId,
            m_currentUserName,
            globalSearchCopyInputs(),
            searchEdit->text().trimmed());
        if (!state.valid) {
            ui->statusbar->showMessage("当前没有可复制的综合搜索卡片", 1800);
            return;
        }
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("综合搜索卡片已复制", 2200);
    });
    connect(copySearchMediaPackBtn, &QPushButton::clicked, &dialog, [this, currentGlobalSearchCopyInput, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::globalSearchMediaPackState(
            m_currentUserId,
            m_currentUserName,
            currentGlobalSearchCopyInput(),
            searchEdit->text().trimmed());
        if (!state.valid) {
            ui->statusbar->showMessage("当前没有可复制的综合搜索媒体包", 1800);
            return;
        }
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("综合搜索媒体包已复制", 2200);
    });
    connect(copyBatchMediaPlanBtn, &QPushButton::clicked, &dialog, [this, globalSearchCopyInputs, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::globalSearchBatchMediaPlanState(
            m_currentUserId,
            m_currentUserName,
            globalSearchCopyInputs(),
            searchEdit->text().trimmed());
        if (!state.valid) {
            ui->statusbar->showMessage("当前没有可复制的综合搜索批量媒体计划", 1800);
            return;
        }
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("综合搜索批量媒体计划已复制", 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this]() {
        QApplication::clipboard()->setText(FriendManager::globalSearchMediaGuideText(
            m_currentUserId,
            m_currentUserName,
            m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget)));
        ui->statusbar->showMessage("综合搜索上传指南已复制", 2200);
    });
    connect(copyOnlineBtn, &QPushButton::clicked, &dialog, [this, globalSearchCopyInputs]() {
        const GlobalSearchResultCopyState state =
            FriendManager::globalSearchResultCopyState(globalSearchCopyInputs(), true);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return;
        }
        QApplication::clipboard()->setText(state.rows.join('\n'));
        ui->statusbar->showMessage(state.copiedStatusMessage, 2200);
    });
    connect(profileBtn, &QPushButton::clicked, &dialog, [this, resultList]() {
        QListWidgetItem* item = resultList->currentItem();
        if (!item) {
            ui->statusbar->showMessage("请先选择要复制名片的搜索结果", 1800);
            return;
        }
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            ui->statusbar->showMessage("当前没有可复制的名片信息", 1800);
            return;
        }
        QString text = item->text();
        if (id.startsWith("search_add:")) {
            id = id.mid(QString("search_add:").size());
            text = QString("QQ:%1\n一键搜索并发送好友申请").arg(id);
        } else if (id.startsWith("local_group_")) {
            QString groupNumber = id.mid(QString("local_group_").size());
            text = QString("群聊 QQ:%1\n%2").arg(groupNumber, m_localGroupNames.value(id, "群聊"));
        } else {
            text = QString("QQ:%1\n%2 · %3").arg(id, contactDisplayName(id), isContactOnline(id) ? "在线" : "离线");
        }
        QApplication::clipboard()->setText(text);
        ui->statusbar->showMessage("名片信息已复制", 1800);
    });
    connect(resultList, &QListWidget::itemDoubleClicked, &dialog, [openResult](QListWidgetItem*) { openResult(); });

    searchEdit->setFocus();
    dialog.exec();
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

void MainWindow::onShowCreateMenu() {
    QMenu menu(this);
    auto describeAction = [](QAction* action, const QString& tip) {
        action->setToolTip(tip);
        action->setStatusTip(tip);
    };
    const struct MenuSpec {
        const char* commandId;
        const char* title;
        const char* tip;
    } specs[] = {
        {"create-group", "创建群聊", "创建一个新的本地群聊并立即进入"},
        {"create-group-with-friends", "创建群并拉全部好友", "创建群聊并自动邀请当前全部好友"},
        {"open-global-search", "申请好友/群", "打开好友申请窗口，搜索 QQ 号并发送申请"},
        {"focus-contact-search", "定位QQ搜索框", "把焦点定位到左侧 QQ 搜索框"},
        {"refresh-contacts", "刷新联系人", "重新加载好友、群聊和在线联系人列表"},
        {"clear-search", "清空搜索", "清空联系人搜索条件"},
        {"copy-chat-id", "复制当前会话号", "复制当前私聊 QQ 号或群聊号"},
        {"copy-chat-card", "复制当前会话名片", "复制当前会话的名称、账号和成员摘要"},
        {"copy-current-invite", "复制当前邀请语", "复制当前会话可用的邀请话术"},
        {"copy-current-members", "复制当前成员列表", "复制当前群聊的成员列表"},
        {"copy-current-online", "复制当前在线成员", "复制当前群聊在线成员列表"},
        {"copy-all-contacts", "复制全部联系人", "复制全部好友、群聊和在线成员摘要"},
        {"copy-search-summary", "复制搜索摘要", "复制当前搜索条件和联系人统计"},
        {"copy-quick-guide", "复制QQ功能指南", "复制 QQ 搜索、好友、群聊和媒体操作指南"},
        {"copy-media-guide", "复制上传指南", "复制图片、视频和文件上传指南"},
        {"copy-current-media-pack", "复制当前媒体包", "复制当前会话的媒体发送准备包"},
        {"copy-full-media-plan", "复制完整媒体计划", "复制好友、群聊和媒体发送的完整计划"},
        {"edit-announcement", "编辑群公告", "编辑当前本地群聊公告"},
        {"copy-announcement", "复制群公告", "复制当前本地群聊公告"},
        {"show-friend-notice", "好友通知", "打开好友通知并处理好友申请"},
        {"show-group-notice", "群通知", "打开群通知并查看群聊、公告和邀请"},
        {"send-image", "发送图片/视频", "选择图片或视频发送到当前会话"},
        {"send-file", "闪传文件", "选择文件闪传到当前会话"}
    };
    for (const MenuSpec& spec : specs) {
        QAction* action = menu.addAction(QString::fromUtf8(spec.title));
        action->setData(QString::fromLatin1(spec.commandId));
        describeAction(action, QString::fromUtf8(spec.tip));
    }
    QAction* selected = menu.exec(ui->createMenuBtn->mapToGlobal(QPoint(0, ui->createMenuBtn->height())));
    if (!selected) {
        return;
    }
    handleCreateMenuCommand(selected->data().toString());
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
    updateEmptyStateVisibility();
    refreshGroupMemberPanel();
    refreshComposerState();
    refreshSessionSummary();
}

void MainWindow::onEditGroupAnnouncement() {
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    const bool isServerPublicGroup = m_privateChatTarget.isEmpty();
    if (isLocalGroup && !isCurrentUserGroupOwner(m_privateChatTarget)) {
        ui->statusbar->showMessage("只有群主可以编辑群公告", 2400);
        appendSystemMessage("群公告编辑被权限保护拦截：当前账号不是群主");
        return;
    }
    if (isServerPublicGroup && !canCurrentUserManageServerGroup("public")) {
        ui->statusbar->showMessage("只有群主或管理员可以编辑公共群公告", 2400);
        appendSystemMessage("公共群公告编辑被服务端角色保护拦截");
        return;
    }

    bool ok = false;
    const QString oldText = ui->announcementBodyLabel->text().trimmed();
    const QString inputText = QInputDialog::getMultiLineText(
        this,
        "编辑群公告",
        "群公告内容:",
        oldText,
        &ok).trimmed();
    if (!ok) {
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
    if (isServerPublicGroup) {
        if (!m_client || !m_client->sendServerGroupAnnouncementUpdate("public", announcementDecision.text)) {
            ui->statusbar->showMessage("群公告提交失败，请检查连接状态", 2400);
            appendSystemMessage("群公告提交失败：客户端未连接或发送失败");
            return;
        }
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
    appendSystemMessage("群公告已更新");
    ui->statusbar->showMessage(announcementDecision.appliedStatusMessage, 2200);
}

void MainWindow::onInsertEmoji() {
    QMenu menu(this);
    const QStringList emojis = {"😀", "😂", "😊", "😍", "😎", "😭", "👍", "🎉", "❤️", "🔥", "👏", "🙏", "💪", "🤝", "📌", "📎"};
    for (const QString& emoji : emojis) {
        QAction* action = menu.addAction(emoji);
        connect(action, &QAction::triggered, this, [this, emoji]() {
            ui->messageEdit->insertPlainText(emoji);
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage(QString("已插入表情 %1").arg(emoji), 1400);
        });
    }
    menu.addSeparator();
    QMenu* commandMenu = menu.addMenu("QQ快捷指令");
    const QList<ChatContextComposerMenuAction> composerActions = ChatContextManager::composerMenuActions();
    for (const ChatContextComposerMenuAction& spec : composerActions) {
        QAction* action = commandMenu->addAction(spec.title);
        action->setToolTip(spec.toolTip);
        action->setStatusTip(spec.toolTip);
        connect(action, &QAction::triggered, this, [this, commandId = spec.commandId]() {
            applyChatContextComposerCommand(commandId);
        });
    }

    const QList<ChatContextPhraseMenuPlan> phrasePlans = ChatContextManager::composerPhraseMenuPlans();
    for (const ChatContextPhraseMenuPlan& plan : phrasePlans) {
        QMenu* phraseMenu = menu.addMenu(plan.title);
        for (const QString& phrase : plan.phrases) {
            QAction* action = phraseMenu->addAction(phrase);
            connect(action, &QAction::triggered, this, [this, phrase, plan]() {
                setChatDraftText(phrase, plan.insertedStatusMessage, 1400);
            });
        }
    }
    menu.exec(ui->emojiBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
}

void MainWindow::onInsertMention() {
    QMenu menu(this);
    QStringList mentionIds;
    QMap<QString, QString> mentionNames;
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        mentionIds = m_localGroupMembers.value(m_privateChatTarget);
    } else {
        for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
            mentionIds << it.key();
            mentionNames[it.key()] = it.value().name;
        }
    }

    for (const QString& memberId : mentionIds) {
        if (!mentionNames.contains(memberId)) {
            mentionNames[memberId] = memberId == m_currentUserId ? m_currentUserName : contactDisplayName(memberId);
        }
    }

    const ComposerMentionMenuPlan mentionPlan =
        ComposerManager::mentionMenuPlan(mentionIds, m_currentUserId, mentionNames);
    for (int i = 0; i < mentionPlan.actions.size(); ++i) {
        if (i == 1 && mentionPlan.separatorAfterAll) {
            menu.addSeparator();
        }
        const ComposerMentionAction actionPlan = mentionPlan.actions.at(i);
        QAction* action = menu.addAction(actionPlan.title);
        connect(action, &QAction::triggered, this, [this, actionPlan]() {
            insertChatDraftText(actionPlan.insertText, actionPlan.statusMessage, 1400);
        });
    }
    menu.exec(ui->mentionBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
}

void MainWindow::onShowQuickAddFriend() {
    QDialog dialog(this);
    dialog.setObjectName("quickAddDialog");
    dialog.setWindowTitle("好友申请");
    dialog.setFixedSize(760, 660);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(26, 22, 26, 22);
    layout->setSpacing(14);

    QLabel* titleLabel = new QLabel("搜索 QQ 账号发送好友申请", &dialog);
    titleLabel->setObjectName("quickAddTitle");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    QLineEdit* accountEdit = new QLineEdit(&dialog);
    accountEdit->setObjectName("quickAddInput");
    accountEdit->setPlaceholderText("输入对方 QQ 号");
    accountEdit->setClearButtonEnabled(true);
    accountEdit->setToolTip("输入对方 QQ 号；回车会搜索并发送好友申请");
    layout->addWidget(accountEdit);

    QLabel* hintLabel = new QLabel("可搜索在线账号并发送好友申请，通过后自动加入本地好友列表。", &dialog);
    hintLabel->setObjectName("quickAddHint");
    hintLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(hintLabel);

    QLabel* statsLabel = new QLabel(&dialog);
    statsLabel->setObjectName("quickAddStats");
    statsLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(statsLabel);

    QLabel* cardLabel = new QLabel(&dialog);
    cardLabel->setObjectName("quickAddPreviewCard");
    cardLabel->setAlignment(Qt::AlignCenter);
    cardLabel->setWordWrap(true);
    layout->addWidget(cardLabel);

    QLabel* actionTipLabel = new QLabel("输入 QQ 后可一键搜索并发送申请，也可以复制申请话术发给对方。", &dialog);
    actionTipLabel->setObjectName("quickAddActionTip");
    actionTipLabel->setAlignment(Qt::AlignCenter);
    actionTipLabel->setWordWrap(true);
    layout->addWidget(actionTipLabel);

    QListWidget* suggestionList = new QListWidget(&dialog);
    suggestionList->setObjectName("quickAddSuggestionList");
    suggestionList->setFixedHeight(112);
    layout->addWidget(suggestionList);

    auto fillSuggestions = [this, accountEdit, suggestionList, statsLabel, cardLabel]() {
        suggestionList->clear();
        const FriendQuickAddSuggestionUiState suggestionState =
            FriendManager::quickAddSuggestionUiState(m_currentUserId,
                                                     m_currentUserName,
                                                     m_friendIds,
                                                     m_pendingOutgoingFriendRequests,
                                                     m_knownUsers,
                                                     m_friendNames,
                                                     accountEdit->text(),
                                                     5);
        statsLabel->setText(suggestionState.statsText);
        cardLabel->setText(suggestionState.previewText);
        for (const FriendQuickAddSuggestionEntryUiState& entry : suggestionState.entries) {
            QListWidgetItem* item = new QListWidgetItem(entry.text);
            item->setData(Qt::UserRole, entry.entryId);
            item->setSizeHint(QSize(0, entry.rowHeight));
            if (!entry.enabled) {
                item->setFlags(Qt::NoItemFlags);
            }
            if (entry.muted) {
                item->setForeground(QColor(135, 150, 165));
            }
            suggestionList->addItem(item);
        }
        for (int i = 0; i < suggestionList->count(); ++i) {
            QListWidgetItem* item = suggestionList->item(i);
            if (item->flags().testFlag(Qt::ItemIsEnabled) && !item->data(Qt::UserRole).toString().isEmpty()) {
                suggestionList->setCurrentRow(i);
                break;
            }
        }
    };
    fillSuggestions();

    QHBoxLayout* buttonLayout = new QHBoxLayout;
    QPushButton* cancelBtn = new QPushButton("取消", &dialog);
    cancelBtn->setObjectName("quickCancelBtn");
    cancelBtn->setToolTip("关闭好友申请窗口");
    QPushButton* searchBtn = new QPushButton("搜索申请", &dialog);
    searchBtn->setObjectName("quickSearchBtn");
    searchBtn->setDefault(true);
    searchBtn->setToolTip("按输入的 QQ 号搜索在线账号并发送好友申请");
    QPushButton* recommendBtn = new QPushButton("推荐申请", &dialog);
    recommendBtn->setObjectName("quickSearchBtn");
    recommendBtn->setToolTip("向当前推荐列表里的可申请用户批量发送好友申请");
    QPushButton* copyPreviewBtn = new QPushButton("复制预览", &dialog);
    copyPreviewBtn->setObjectName("quickCancelBtn");
    copyPreviewBtn->setToolTip("复制当前申请预览卡片");
    QPushButton* copyRequestBtn = new QPushButton("复制申请话术", &dialog);
    copyRequestBtn->setObjectName("quickCancelBtn");
    copyRequestBtn->setToolTip("复制适合当前 QQ 号或推荐用户的好友申请话术");
    QPushButton* copySearchCardBtn = new QPushButton("复制搜索卡片", &dialog);
    copySearchCardBtn->setObjectName("quickCancelBtn");
    copySearchCardBtn->setToolTip("复制当前好友申请搜索条件和推荐结果");
    QPushButton* copyFriendMediaPackBtn = new QPushButton("复制好友媒体包", &dialog);
    copyFriendMediaPackBtn->setObjectName("quickCancelBtn");
    copyFriendMediaPackBtn->setToolTip("复制加好友后发送图片、视频或文件的准备摘要");
    QPushButton* copyAddChecklistBtn = new QPushButton("复制申请清单", &dialog);
    copyAddChecklistBtn->setObjectName("quickCancelBtn");
    copyAddChecklistBtn->setToolTip("复制好友申请前后的操作检查清单");
    QPushButton* copyMediaGuideBtn = new QPushButton("复制上传指南", &dialog);
    copyMediaGuideBtn->setObjectName("quickCancelBtn");
    copyMediaGuideBtn->setToolTip("复制通过好友申请后发送媒体和文件的简短指南");
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(copyPreviewBtn);
    buttonLayout->addWidget(copyRequestBtn);
    buttonLayout->addWidget(copySearchCardBtn);
    buttonLayout->addWidget(copyFriendMediaPackBtn);
    buttonLayout->addWidget(copyAddChecklistBtn);
    buttonLayout->addWidget(copyMediaGuideBtn);
    buttonLayout->addWidget(recommendBtn);
    buttonLayout->addWidget(searchBtn);
    layout->addLayout(buttonLayout);


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
    auto quickAddCandidateTargets = [this, suggestionList]() {
        QList<FriendManagerVisibleTargetSummary> targets;
        for (int i = 0; i < suggestionList->count(); ++i) {
            QListWidgetItem* item = suggestionList->item(i);
            const QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty()) {
                continue;
            }
            FriendManagerVisibleTargetSummary target;
            target.userId = id;
            target.displayName = contactDisplayName(id);
            target.online = isContactOnline(id);
            targets << target;
        }
        return targets;
    };
    auto selectedQuickAddTarget = [this, accountEdit, suggestionList]() {
        FriendManagerVisibleTargetSummary target;
        target.userId = accountEdit->text().trimmed();
        if (target.userId.isEmpty()) {
            QListWidgetItem* item = suggestionList->currentItem();
            if (item) {
                target.userId = item->data(Qt::UserRole).toString();
            }
        }
        if (!target.userId.isEmpty()) {
            target.displayName = contactDisplayName(target.userId);
            target.online = isContactOnline(target.userId);
        }
        return target;
    };
    connect(accountEdit, &QLineEdit::textChanged, &dialog, [fillSuggestions]() { fillSuggestions(); });
    connect(copyPreviewBtn, &QPushButton::clicked, &dialog, [this, cardLabel]() {
        QApplication::clipboard()->setText(cardLabel->text());
        ui->statusbar->showMessage("好友申请预览已复制", 2200);
    });
    connect(copyRequestBtn, &QPushButton::clicked, &dialog, [this, accountEdit, suggestionList, cardLabel]() {
        QString account = accountEdit->text().trimmed();
        if (account.isEmpty()) {
            QListWidgetItem* item = suggestionList->currentItem();
            if (item) account = item->data(Qt::UserRole).toString();
        }
        QString name = account.isEmpty() ? "朋友" : contactDisplayName(account);
        QString text = QString("%1，你好，我是 %2（QQ:%3）。我通过 QQ 搜索看到你，想加你为好友继续沟通。\n%4")
            .arg(name, m_currentUserName, m_currentUserId, cardLabel->text());
        QApplication::clipboard()->setText(text);
        ui->statusbar->showMessage("好友申请话术已复制", 2200);
    });
    connect(copySearchCardBtn, &QPushButton::clicked, &dialog, [this, accountEdit, quickAddCandidateTargets]() {
        const GlobalSearchSelectionCopyState state = FriendManager::quickAddSearchSummaryCardState(
            m_currentUserId,
            m_currentUserName,
            accountEdit->text().trimmed(),
            quickAddCandidateTargets());
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("好友申请搜索卡片已复制", 2200);
    });
    connect(copyFriendMediaPackBtn, &QPushButton::clicked, &dialog, [this, selectedQuickAddTarget]() {
        const GlobalSearchSelectionCopyState state = FriendManager::quickAddMediaPackState(
            m_currentUserId,
            m_currentUserName,
            selectedQuickAddTarget());
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("好友媒体包已复制", 2200);
    });
    connect(copyAddChecklistBtn, &QPushButton::clicked, &dialog, [this, selectedQuickAddTarget]() {
        const GlobalSearchSelectionCopyState state = FriendManager::quickAddChecklistState(
            m_currentUserId,
            m_currentUserName,
            m_friendIds.size(),
            selectedQuickAddTarget());
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("好友申请清单已复制", 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this, accountEdit]() {
        QApplication::clipboard()->setText(FriendManager::quickAddMediaGuideText(
            m_currentUserId,
            m_currentUserName,
            accountEdit->text().trimmed()));
        ui->statusbar->showMessage("好友申请上传指南已复制", 2200);
    });
    connect(suggestionList, &QListWidget::itemDoubleClicked, &dialog, [accountEdit, runQuickAdd](QListWidgetItem* item) {
        QString account = item->data(Qt::UserRole).toString();
        if (account.isEmpty()) return;
        accountEdit->setText(account);
        runQuickAdd();
    });
    connect(recommendBtn, &QPushButton::clicked, &dialog, [this, suggestionList, hintLabel, fillSuggestions, &dialog]() {
        QStringList addIds;
        for (int i = 0; i < suggestionList->count(); ++i) {
            QListWidgetItem* item = suggestionList->item(i);
            QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty() || id == m_currentUserId || m_friendIds.contains(id) || m_pendingOutgoingFriendRequests.contains(id)) continue;
            if (!addIds.contains(id)) {
                addIds << id;
            }
        }
        if (addIds.isEmpty()) {
            hintLabel->setText("暂无可申请的推荐好友，可输入 QQ 号搜索");
            ui->statusbar->showMessage("暂无可申请的推荐好友", 2200);
            return;
        }
        if (QMessageBox::question(&dialog,
                                  "发送推荐好友申请",
                                  QString("确定向 %1 位推荐用户发送好友申请吗？").arg(addIds.size()),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage("已取消发送推荐申请", 1600);
            return;
        }
        QStringList sentNames;
        QStringList failedNames;
        for (const QString& id : addIds) {
            const QString displayName = contactDisplayName(id);
            if (m_client->sendFriendRequest(id)) {
                m_friendNames[id] = displayName;
                if (!m_pendingOutgoingFriendRequests.contains(id)) {
                    m_pendingOutgoingFriendRequests << id;
                }
                sentNames << QString("%1(%2)").arg(displayName, id);
            } else {
                failedNames << QString("%1(%2)").arg(displayName, id);
            }
        }
        if (sentNames.isEmpty()) {
            hintLabel->setText("好友申请发送失败，请检查连接后重试。");
            ui->statusbar->showMessage("推荐好友申请发送失败", 2600);
            return;
        }
        refreshFriendList();
        fillSuggestions();
        appendSystemMessage(QString("已向推荐用户发送好友申请：%1").arg(sentNames.join("、")));
        if (!failedNames.isEmpty()) {
            appendSystemMessage(QString("以下推荐好友申请发送失败：%1").arg(failedNames.join("、")));
        }
        ui->statusbar->showMessage(QString("已发送 %1 个推荐好友申请，等待确认").arg(sentNames.size()), 2500);
        dialog.accept();
    });
    connect(searchBtn, &QPushButton::clicked, &dialog, runQuickAdd);
    connect(accountEdit, &QLineEdit::returnPressed, &dialog, runQuickAdd);

    accountEdit->setFocus();
    dialog.exec();
}

void MainWindow::onShowFriendManager() {
    QDialog dialog(this);
    dialog.setObjectName("friendManagerDialog");
    dialog.setWindowTitle("好友管理器");
    dialog.setFixedSize(900, 700);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QFrame* header = new QFrame(&dialog);
    header->setObjectName("managerHeader");
    header->setFixedHeight(132);
    QVBoxLayout* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(26, 18, 26, 16);
    QLabel* titleLabel = new QLabel("好友管理器", header);
    titleLabel->setObjectName("managerTitle");
    QLabel* subTitleLabel = new QLabel(QString("当前 QQ：%1 · 好友 %2 人").arg(m_currentUserId).arg(m_friendIds.size()), header);
    subTitleLabel->setObjectName("managerSubTitle");
    QLabel* statsLabel = new QLabel(header);
    statsLabel->setObjectName("managerStats");
    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(subTitleLabel);
    headerLayout->addWidget(statsLabel);
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
    searchEdit->setToolTip("按 QQ 号或昵称筛选好友；无结果时可用搜索申请发送好友申请");
    bodyLayout->addWidget(searchEdit);

    QListWidget* friendList = new QListWidget(body);
    friendList->setObjectName("managerList");
    bodyLayout->addWidget(friendList, 1);

    auto fillList = [this, friendList, subTitleLabel, statsLabel](const QString& filter = QString()) {
        friendList->clear();
        const FriendManagerListRenderUiState listState = m_friendManager.managerListRenderUiState(
            m_currentUserId,
            m_friendIds,
            m_localGroupIds,
            m_friendNames,
            m_knownUsers,
            filter);
        for (const FriendManagerListEntryUiState& entry : listState.entries) {
            QListWidgetItem* item = new QListWidgetItem(entry.text);
            item->setData(Qt::UserRole, entry.entryId);
            item->setSizeHint(QSize(0, entry.rowHeight));
            if (entry.muted) {
                item->setForeground(QColor(135, 150, 165));
            }
            friendList->addItem(item);
        }
        subTitleLabel->setText(listState.summary.subTitle);
        statsLabel->setText(listState.summary.statsText);
    };
    fillList();

    QLabel* selectionPreviewLabel = new QLabel("选择好友后可复制名片、邀请语或邀入群", body);
    selectionPreviewLabel->setObjectName("managerSelectionPreview");
    bodyLayout->addWidget(selectionPreviewLabel);

    QLabel* operationGuideLabel = new QLabel("可在列表内双击私聊；搜索无结果时可直接按“搜索申请”发送 QQ 好友申请。", body);
    operationGuideLabel->setObjectName("managerOperationGuide");
    operationGuideLabel->setWordWrap(true);
    bodyLayout->addWidget(operationGuideLabel);

    QHBoxLayout* buttonLayout = new QHBoxLayout;
    QPushButton* addBtn = new QPushButton("发申请", body);
    addBtn->setObjectName("managerPrimaryBtn");
    addBtn->setToolTip("打开好友申请窗口，输入 QQ 号并发送申请");
    QPushButton* searchAddBtn = new QPushButton("搜索申请", body);
    searchAddBtn->setObjectName("managerPrimaryBtn");
    searchAddBtn->setToolTip("使用当前搜索框内容搜索 QQ 并发送好友申请");
    QPushButton* clearSearchBtn = new QPushButton("清空搜索", body);
    clearSearchBtn->setObjectName("managerSecondaryBtn");
    clearSearchBtn->setToolTip("清空筛选条件并显示全部好友");
    QPushButton* chatBtn = new QPushButton("发消息", body);
    chatBtn->setObjectName("managerSecondaryBtn");
    chatBtn->setToolTip("打开当前选中好友的私聊会话");
    QPushButton* copyBtn = new QPushButton("复制QQ", body);
    copyBtn->setObjectName("managerSecondaryBtn");
    copyBtn->setToolTip("复制当前选中好友的 QQ 号");
    QPushButton* copyAllBtn = new QPushButton("复制可见列表", body);
    copyAllBtn->setObjectName("managerSecondaryBtn");
    copyAllBtn->setToolTip("复制当前筛选出的好友列表");
    QPushButton* profileBtn = new QPushButton("复制名片", body);
    profileBtn->setObjectName("managerSecondaryBtn");
    profileBtn->setToolTip("复制当前选中好友的 QQ、昵称和在线状态");
    QPushButton* inviteTextBtn = new QPushButton("复制邀请语", body);
    inviteTextBtn->setObjectName("managerSecondaryBtn");
    inviteTextBtn->setToolTip("复制一段邀请当前好友加入群聊的话术");
    QPushButton* copyStatsBtn = new QPushButton("复制好友统计", body);
    copyStatsBtn->setObjectName("managerSecondaryBtn");
    copyStatsBtn->setToolTip("复制好友数量、在线状态和群聊统计");
    QPushButton* copyOnlineBtn = new QPushButton("复制在线", body);
    copyOnlineBtn->setObjectName("managerSecondaryBtn");
    copyOnlineBtn->setToolTip("复制当前可见列表中的在线好友");
    QPushButton* copySearchCardBtn = new QPushButton("复制搜索卡片", body);
    copySearchCardBtn->setObjectName("managerSecondaryBtn");
    copySearchCardBtn->setToolTip("复制当前搜索条件、选中好友和可见结果摘要");
    QPushButton* copyFriendMediaPackBtn = new QPushButton("复制好友媒体包", body);
    copyFriendMediaPackBtn->setObjectName("managerSecondaryBtn");
    copyFriendMediaPackBtn->setToolTip("复制给好友发送图片、视频或文件前的准备摘要");
    QPushButton* copyBatchMediaPlanBtn = new QPushButton("复制批量媒体计划", body);
    copyBatchMediaPlanBtn->setObjectName("managerSecondaryBtn");
    copyBatchMediaPlanBtn->setToolTip("复制当前可见好友的批量媒体发送计划");
    QPushButton* copyMediaGuideBtn = new QPushButton("复制上传指南", body);
    copyMediaGuideBtn->setObjectName("managerSecondaryBtn");
    copyMediaGuideBtn->setToolTip("复制好友私聊中发送图片、视频和文件的简短指南");
    QPushButton* remarkBtn = new QPushButton("备注", body);
    remarkBtn->setObjectName("managerSecondaryBtn");
    remarkBtn->setToolTip("修改当前选中好友在本地显示的备注名");
    QPushButton* inviteBtn = new QPushButton("邀入群", body);
    inviteBtn->setObjectName("managerSecondaryBtn");
    inviteBtn->setToolTip("把当前选中好友邀请进最近的本地群聊");
    QPushButton* inviteVisibleBtn = new QPushButton("邀请可见", body);
    inviteVisibleBtn->setObjectName("managerSecondaryBtn");
    inviteVisibleBtn->setToolTip("把当前筛选出的可见好友批量邀请进群聊");
    QPushButton* deleteBtn = new QPushButton("删除好友", body);
    deleteBtn->setObjectName("managerDangerBtn");
    deleteBtn->setToolTip("从本地好友列表中删除当前选中好友");
    QPushButton* closeBtn = new QPushButton("关闭", body);
    closeBtn->setObjectName("managerSecondaryBtn");
    closeBtn->setToolTip("关闭好友管理器");
    buttonLayout->addWidget(addBtn);
    buttonLayout->addWidget(searchAddBtn);
    buttonLayout->addWidget(clearSearchBtn);
    buttonLayout->addWidget(chatBtn);
    buttonLayout->addWidget(copyBtn);
    buttonLayout->addWidget(copyAllBtn);
    buttonLayout->addWidget(profileBtn);
    buttonLayout->addWidget(inviteTextBtn);
    buttonLayout->addWidget(copyStatsBtn);
    buttonLayout->addWidget(copyOnlineBtn);
    buttonLayout->addWidget(copySearchCardBtn);
    buttonLayout->addWidget(copyFriendMediaPackBtn);
    buttonLayout->addWidget(copyBatchMediaPlanBtn);
    buttonLayout->addWidget(copyMediaGuideBtn);
    buttonLayout->addWidget(remarkBtn);
    buttonLayout->addWidget(inviteBtn);
    buttonLayout->addWidget(inviteVisibleBtn);
    buttonLayout->addWidget(deleteBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeBtn);
    bodyLayout->addLayout(buttonLayout);
    layout->addWidget(body);


    auto openSelectedFriend = [this, &dialog, friendList]() {
        QListWidgetItem* selected = friendList->currentItem();
        if (!selected) {
            ui->statusbar->showMessage("请先选择要发消息的好友", 1800);
            return;
        }
        QString id = selected->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            ui->statusbar->showMessage("暂无可打开的好友会话", 1800);
            return;
        }
        if (id.startsWith("search_add:")) {
            dialog.accept();
            searchAndAddAccount(id.mid(QString("search_add:").size()), this);
            return;
        }
        dialog.accept();
        openPrivateSession(id);
        ui->statusbar->showMessage(QString("已打开与 %1 的私聊").arg(contactDisplayName(id)), 1800);
    };

    auto updateSelectionPreview = [this, friendList, selectionPreviewLabel]() {
        QListWidgetItem* selected = friendList->currentItem();
        if (!selected) {
            selectionPreviewLabel->setText(
                m_friendManager.managerSelectionPreviewUiState(QString(), QString(), false, false).text);
            return;
        }
        const QString id = selected->data(Qt::UserRole).toString();
        selectionPreviewLabel->setText(
            m_friendManager.managerSelectionPreviewUiState(
                id,
                id.isEmpty() ? QString() : contactDisplayName(id),
                !id.isEmpty() && isContactOnline(id),
                m_privateChatTarget.startsWith("local_group_")).text);
    };
    updateSelectionPreview();

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [fillList, updateSelectionPreview](const QString& text) {
        fillList(text.trimmed());
        updateSelectionPreview();
    });
    connect(addBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onShowQuickAddFriend();
    });
    connect(searchAddBtn, &QPushButton::clicked, &dialog, [this, searchEdit, &dialog]() {
        QString account = searchEdit->text().trimmed();
        if (account.isEmpty()) {
            searchEdit->setFocus();
            ui->statusbar->showMessage("请输入 QQ 账号后搜索申请", 2200);
            return;
        }
        dialog.accept();
        searchAndAddAccount(account, this);
    });
    connect(clearSearchBtn, &QPushButton::clicked, &dialog, [searchEdit, fillList, updateSelectionPreview]() {
        searchEdit->clear();
        fillList();
        updateSelectionPreview();
        searchEdit->setFocus();
    });
    connect(chatBtn, &QPushButton::clicked, &dialog, openSelectedFriend);
    connect(friendList, &QListWidget::currentItemChanged, &dialog, [updateSelectionPreview](QListWidgetItem*, QListWidgetItem*) { updateSelectionPreview(); });
    connect(friendList, &QListWidget::itemDoubleClicked, &dialog, [openSelectedFriend](QListWidgetItem*) { openSelectedFriend(); });
    connect(copyBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        QString id;
        if (!trySelectedValidFriendId(friendList, ui->statusbar, "请先选择要复制 QQ 的好友", &id)) {
            return;
        }
        copyTextWithStatus(id, "QQ 号已复制: " + id, 2500);
    });
    auto friendCopyInputs = [this](const QStringList& friendIds) {
        QList<FriendManagerContactCopyInput> inputs;
        for (const QString& id : friendIds) {
            FriendManagerContactCopyInput input;
            input.userId = id;
            input.displayName = contactDisplayName(id);
            input.online = isContactOnline(id);
            inputs << input;
        }
        return inputs;
    };
    auto friendManagerVisibleTargets = [this, friendList]() {
        QList<FriendManagerVisibleTargetSummary> targets;
        for (int i = 0; i < friendList->count(); ++i) {
            QListWidgetItem* item = friendList->item(i);
            const QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty()) {
                continue;
            }
            FriendManagerVisibleTargetSummary target;
            target.userId = id;
            target.online = isContactOnline(id);
            if (!id.startsWith("search_add:")) {
                target.displayName = contactDisplayName(id);
            }
            targets << target;
        }
        return targets;
    };
    auto selectedFriendManagerVisibleTarget = [this, friendList, searchEdit]() {
        FriendManagerVisibleTargetSummary target;
        target.userId = selectedFriendManagerTargetId(friendList);
        if (target.userId.isEmpty()) {
            target.userId = searchEdit->text().trimmed();
        }
        target.online = isContactOnline(target.userId);
        if (!target.userId.isEmpty() && !target.userId.startsWith("search_add:")) {
            target.displayName = contactDisplayName(target.userId);
        }
        return target;
    };
    connect(copyAllBtn, &QPushButton::clicked, &dialog, [this, friendList, friendCopyInputs]() {
        const FriendManagerContactCopyState state =
            FriendManager::managerContactCopyState(friendCopyInputs(visibleFriendManagerIds(friendList)), false);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return;
        }
        copyTextWithStatus(state.rows.join('\n'), state.copiedStatusMessage, 2200);
    });
    connect(profileBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        QString id;
        if (!trySelectedValidFriendId(friendList, ui->statusbar, "请先选择要复制名片的好友", &id)) {
            return;
        }
        QString card = QString("QQ:%1\n昵称:%2\n状态:%3").arg(id, contactDisplayName(id), isContactOnline(id) ? "在线" : "离线");
        copyTextWithStatus(card, "好友名片已复制");
    });
    connect(inviteTextBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        QString id = selectedFriendManagerTargetId(friendList);
        QString targetName = id.isEmpty() ? "朋友" : contactDisplayName(id);
        QString groupName = m_privateChatTarget.startsWith("local_group_") ? m_localGroupNames.value(m_privateChatTarget, "群聊") : "群聊";
        QString text = QString("%1，你好，我是 %2（QQ:%3）。方便的话加个好友，我也可以邀请你加入 %4 一起沟通。")
            .arg(targetName, m_currentUserName, m_currentUserId, groupName);
        copyTextWithStatus(text, "好友邀请话术已复制", 2200);
    });
    connect(copyStatsBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        int visibleCount = 0;
        int visibleOnline = 0;
        int visibleOffline = 0;
        QStringList visibleRows;
        const QStringList visibleIds = visibleFriendManagerIds(friendList);
        for (const QString& id : visibleIds) {
            bool online = isContactOnline(id);
            ++visibleCount;
            if (online) ++visibleOnline; else ++visibleOffline;
            visibleRows << QString("%1(QQ:%2,%3)").arg(contactDisplayName(id), id, online ? "在线" : "离线");
        }
        QString text = QString("好友统计\n我的QQ:%1\n全部好友:%2\n可见好友:%3\n可见在线:%4\n可见离线:%5\n本地群:%6\n可见列表:%7")
            .arg(m_currentUserId)
            .arg(m_friendIds.size())
            .arg(visibleCount)
            .arg(visibleOnline)
            .arg(visibleOffline)
            .arg(m_localGroupIds.size())
            .arg(visibleRows.isEmpty() ? "无" : visibleRows.join("、"));
        copyTextWithStatus(text, "好友统计已复制", 2200);
    });
    connect(copyOnlineBtn, &QPushButton::clicked, &dialog, [this, friendList, friendCopyInputs]() {
        const FriendManagerContactCopyState state =
            FriendManager::managerContactCopyState(friendCopyInputs(visibleFriendManagerIds(friendList)), true);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return;
        }
        copyTextWithStatus(state.rows.join('\n'), state.copiedStatusMessage, 2200);
    });
    connect(copySearchCardBtn, &QPushButton::clicked, &dialog, [this, friendManagerVisibleTargets, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::friendManagerSearchSummaryCardState(
            m_currentUserId,
            m_currentUserName,
            searchEdit->text().trimmed(),
            m_friendIds.size(),
            m_localGroupIds.size(),
            friendManagerVisibleTargets());
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("好友管理搜索卡片已复制", 2200);
    });
    connect(copyFriendMediaPackBtn, &QPushButton::clicked, &dialog, [this, selectedFriendManagerVisibleTarget, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::friendManagerMediaPackState(
            m_currentUserId,
            m_currentUserName,
            searchEdit->text().trimmed(),
            m_friendIds.size(),
            selectedFriendManagerVisibleTarget());
        copyTextWithStatus(state.text, "好友管理媒体包已复制", 2200);
    });
    connect(copyBatchMediaPlanBtn, &QPushButton::clicked, &dialog, [this, friendManagerVisibleTargets, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::friendManagerBatchMediaPlanState(
            m_currentUserId,
            m_currentUserName,
            searchEdit->text().trimmed(),
            friendManagerVisibleTargets());
        copyTextWithStatus(state.text, "好友批量媒体计划已复制", 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this, friendList, searchEdit]() {
        const int visibleCount = visibleFriendManagerIds(friendList).size();
        copyTextWithStatus(
            FriendManager::friendManagerMediaGuideText(
                m_currentUserId,
                m_currentUserName,
                searchEdit->text().trimmed(),
                visibleCount,
                m_friendIds.size()),
            "好友管理上传指南已复制",
            2200);
    });
    connect(remarkBtn, &QPushButton::clicked, &dialog, [this, friendList, fillList, searchEdit, &dialog]() {
        QString id;
        if (!trySelectedValidFriendId(friendList, ui->statusbar, "请先选择要备注的好友", &id)) {
            return;
        }
        const QString oldRemark = contactDisplayName(id);
        bool ok = false;
        const QString remark = promptTextValue(QStringLiteral("设置备注"),
                                               QStringLiteral("备注名称:"),
                                               oldRemark,
                                               &ok,
                                               &dialog);
        if (!ok) return;
        if (remark.isEmpty()) {
            ui->statusbar->showMessage("备注名称不能为空", 1800);
            return;
        }
        if (remark == oldRemark) {
            ui->statusbar->showMessage("备注未改变", 1600);
            return;
        }
        m_friendNames[id] = remark;
        saveFriends();
        refreshFriendList();
        refreshGroupMemberPanel();
        fillList(searchEdit->text().trimmed());
        ui->statusbar->showMessage(QString("已设置备注：%1").arg(remark), 2200);
        appendSystemMessage(QString("已设置 %1 的备注为 %2").arg(id, remark));
    });
    connect(inviteBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        QString friendId;
        if (!trySelectedValidFriendId(friendList, ui->statusbar, "请先选择要邀请入群的好友", &friendId)) {
            return;
        }
        if (m_localGroupIds.isEmpty()) {
            createLocalGroupSession(QStringLiteral("我的群聊"));
        }
        QString targetGroup = m_privateChatTarget.startsWith("local_group_") ? m_privateChatTarget : m_localGroupIds.last();
        if (appendMembersToLocalGroup(targetGroup, QStringList{friendId}) == 0) {
            ui->statusbar->showMessage(QString("%1 已在目标群聊中").arg(contactDisplayName(friendId)), 1800);
            return;
        }
        switchToLocalGroup(targetGroup, m_localGroupNames.value(targetGroup, "群聊"));
        appendSystemMessage(QString("已邀请 %1 加入群聊").arg(contactDisplayName(friendId)));
        saveHistory(targetGroup, QString("[%1] [系统] 已邀请 %2 加入群聊").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), contactDisplayName(friendId)));
    });
    connect(inviteVisibleBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        const bool willCreateGroup = m_localGroupIds.isEmpty();
        QString targetGroup = m_privateChatTarget.startsWith("local_group_")
            ? m_privateChatTarget
            : (willCreateGroup ? QString("local_group_" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz")) : m_localGroupIds.last());
        QString groupName = willCreateGroup ? "好友群聊" : m_localGroupNames.value(targetGroup, "群聊");
        QStringList currentMembers = willCreateGroup ? QStringList{m_currentUserId} : m_localGroupMembers.value(targetGroup);
        QStringList inviteIds;
        const QStringList visibleIds = visibleFriendManagerIds(friendList);
        for (const QString& friendId : visibleIds) {
            if (currentMembers.contains(friendId) || inviteIds.contains(friendId)) continue;
            inviteIds << friendId;
        }
        if (inviteIds.isEmpty()) {
            ui->statusbar->showMessage("当前没有可邀请的可见好友", 2200);
            return;
        }
        if (!confirmAction(QStringLiteral("邀请可见好友"),
                           QString("确定邀请 %1 位可见好友加入群聊“%2”吗？").arg(inviteIds.size()).arg(groupName),
                           QStringLiteral("已取消邀请可见好友"))) {
            return;
        }
        if (willCreateGroup) {
            targetGroup = createLocalGroupSession(QStringLiteral("好友群聊"));
        }
        appendMembersToLocalGroup(targetGroup, inviteIds);
        switchToLocalGroup(targetGroup, m_localGroupNames.value(targetGroup, "群聊"));
        appendSystemMessage(QString("已邀请 %1 位可见好友加入群聊").arg(inviteIds.size()));
        saveHistory(targetGroup, QString("[%1] [系统] 已邀请 %2 位可见好友加入群聊").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(inviteIds.size()));
    });
    connect(deleteBtn, &QPushButton::clicked, &dialog, [this, friendList, fillList, searchEdit, &dialog]() {
        QString id;
        if (!trySelectedValidFriendId(friendList, ui->statusbar, "请先选择要删除的好友", &id)) {
            return;
        }
        QString displayName = contactDisplayName(id);
        if (!confirmAction(QStringLiteral("删除好友"),
                           QString("确定删除好友“%1”（QQ:%2）吗？删除后可重新搜索并发送申请。").arg(displayName, id),
                           QStringLiteral("已取消删除好友"),
                           1600,
                           &dialog)) {
            return;
        }
        m_friendIds.removeAll(id);
        m_friendNames.remove(id);
        saveFriends();
        refreshFriendList();
        fillList(searchEdit->text().trimmed());
        ui->statusbar->showMessage(QString("已删除好友：%1").arg(displayName), 2200);
        appendSystemMessage(QString("已删除好友: %1（QQ:%2）").arg(displayName, id));
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
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
    updateFriendNoticeBadge();
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
        updateFriendNoticeBadge();
        appendSystemMessage(displayName + " 已同意你的好友申请");
        ui->statusbar->showMessage(QString("%1 已同意好友申请").arg(displayName), 2800);
    } else {
        m_friendIds.removeAll(senderId);
        m_friendNames.remove(senderId);
        m_pendingFriendRequests.removeAll(senderId);
        saveFriends();
        refreshFriendList();
        refreshGroupMemberPanel();
        updateFriendNoticeBadge();
        appendSystemMessage(displayName + " 已拒绝你的好友申请");
        ui->statusbar->showMessage(QString("%1 已拒绝好友申请").arg(displayName), 2800);
    }
}

bool MainWindow::handleLocalGroupContextCommand(const QString& groupId,
                                                const QString& groupLabel,
                                                const QString& commandId) {
    auto groupMemberCopyInputs = [this](const QStringList& memberIds) {
        QList<GroupNoticeMemberInput> inputs;
        for (const QString& id : memberIds) {
            GroupNoticeMemberInput input;
            input.userId = id;
            input.displayName = contactDisplayName(id);
            input.self = id == m_currentUserId;
            input.online = isContactOnline(id);
            inputs << input;
        }
        return inputs;
    };

    if (commandId == QLatin1String("open-group")) {
        switchToLocalGroup(groupId, m_localGroupNames.value(groupId, QStringLiteral("群聊")));
        return true;
    }
    if (commandId == QLatin1String("copy-group-id")) {
        const QString groupNumber = groupId.mid(QStringLiteral("local_group_").size());
        copyTextWithStatus(groupNumber, QStringLiteral("群号已复制: ") + groupNumber, 2500);
        return true;
    }
    if (commandId == QLatin1String("copy-group-card")) {
        const QString groupNumber = groupId.mid(QStringLiteral("local_group_").size());
        const QString card = QStringLiteral("群聊 QQ:%1\n%2\n成员:%3\n公告:%4")
            .arg(groupNumber,
                 m_localGroupNames.value(groupId, QStringLiteral("群聊")),
                 QString::number(m_localGroupMembers.value(groupId).size()),
                 m_localGroupAnnouncements.value(groupId,
                                                 QStringLiteral("%1 已创建，可继续邀请好友并发送消息。")
                                                     .arg(m_localGroupNames.value(groupId, QStringLiteral("群聊")))));
        copyTextWithStatus(card, QStringLiteral("群名片已复制"), 1800);
        return true;
    }
    if (commandId == QLatin1String("copy-group-invite")) {
        const QString groupName = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
        const QString groupNumber = groupId.mid(QStringLiteral("local_group_").size());
        const QString inviteText = QStringLiteral("我邀请你加入群聊“%1”（群号:%2）。我是 %3（QQ:%4），进群后我们一起沟通。")
            .arg(groupName, groupNumber, m_currentUserName, m_currentUserId);
        copyTextWithStatus(inviteText, QStringLiteral("群邀请语已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-group-members")) {
        const GroupNoticeMemberCopyState state =
            NotificationPanelManager::groupMemberCopyState(groupMemberCopyInputs(m_localGroupMembers.value(groupId)), false);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return true;
        }
        copyTextWithStatus(state.rows.join(QLatin1Char('\n')), state.copiedStatusMessage, 2200);
        return true;
    }
    if (commandId == QLatin1String("invite-friend")) {
        if (m_friendIds.isEmpty()) {
            appendSystemMessage(QStringLiteral("当前没有好友可邀请"));
            return true;
        }
        QStringList friendLabels;
        QMap<QString, QString> labelToId;
        for (const QString& friendId : m_friendIds) {
            const QString label = QStringLiteral("%1 (QQ:%2)").arg(m_friendNames.value(friendId, friendId), friendId);
            friendLabels << label;
            labelToId[label] = friendId;
        }
        bool ok = false;
        const QString selectedFriend = promptItemValue(QStringLiteral("邀请好友"),
                                                       QStringLiteral("选择好友:"),
                                                       friendLabels,
                                                       &ok);
        if (!ok || selectedFriend.isEmpty()) {
            return true;
        }
        const QString friendId = labelToId.value(selectedFriend);
        const QString friendName = m_friendNames.value(friendId, friendId);
        if (!m_localGroupMembers[groupId].contains(friendId)) {
            m_localGroupMembers[groupId] << friendId;
            saveLocalGroups();
        }
        switchToLocalGroup(groupId, m_localGroupNames.value(groupId, QStringLiteral("群聊")));
        appendSystemMessage(QStringLiteral("已邀请 %1 加入群聊").arg(friendName));
        saveHistory(groupId,
                    QStringLiteral("[%1] [系统] 已邀请 %2 加入群聊")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), friendName));
        return true;
    }
    if (commandId == QLatin1String("invite-by-account")) {
        bool ok = false;
        const QString account = promptTextValue(QStringLiteral("按QQ号邀请"),
                                                QStringLiteral("输入 QQ 账号:"),
                                                QString(),
                                                &ok);
        if (!ok) {
            return true;
        }
        if (account.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("请输入 QQ 号后再邀请入群"), 1800);
            return true;
        }
        if (account == m_currentUserId) {
            ui->statusbar->showMessage(QStringLiteral("你已在当前群聊中，无需重复邀请"), 1800);
            return true;
        }
        if (m_localGroupMembers[groupId].contains(account)) {
            ui->statusbar->showMessage(QStringLiteral("该 QQ 已在当前群聊中"), 1800);
            return true;
        }
        QString requestNote;
        m_localGroupMembers[groupId] << account;
        if (!m_friendIds.contains(account) && !m_pendingOutgoingFriendRequests.contains(account)) {
            const QString displayName = contactDisplayName(account);
            if (m_client && m_client->sendFriendRequest(account)) {
                m_friendNames[account] = displayName;
                m_pendingOutgoingFriendRequests << account;
                requestNote = QStringLiteral("，好友申请等待确认");
            } else {
                requestNote = QStringLiteral("，好友申请发送失败");
                ui->statusbar->showMessage(QStringLiteral("已邀请入群，但好友申请发送失败：%1").arg(displayName), 3000);
            }
        } else if (m_pendingOutgoingFriendRequests.contains(account)) {
            requestNote = QStringLiteral("，好友申请已在等待确认");
        }
        saveLocalGroups();
        refreshFriendList();
        switchToLocalGroup(groupId, m_localGroupNames.value(groupId, QStringLiteral("群聊")));
        refreshGroupMemberPanel();
        appendSystemMessage(QStringLiteral("已按 QQ 号邀请 %1 加入群聊%2").arg(account, requestNote));
        saveHistory(groupId,
                    QStringLiteral("[%1] [系统] 已按 QQ 号邀请 %2 加入群聊")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), account));
        return true;
    }
    if (commandId == QLatin1String("invite-all-friends")) {
        QStringList inviteIds;
        for (const QString& friendId : m_friendIds) {
            if (!m_localGroupMembers[groupId].contains(friendId)) {
                inviteIds << friendId;
            }
        }
        if (inviteIds.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("全部好友已在该群聊中"), 1800);
            return true;
        }
        const QString groupName = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
        if (!confirmAction(QStringLiteral("邀请全部好友"),
                           QStringLiteral("确定邀请 %1 位好友加入群聊“%2”吗？").arg(inviteIds.size()).arg(groupName),
                           QStringLiteral("已取消邀请全部好友"))) {
            return true;
        }
        for (const QString& friendId : inviteIds) {
            m_localGroupMembers[groupId] << friendId;
        }
        saveLocalGroups();
        switchToLocalGroup(groupId, m_localGroupNames.value(groupId, QStringLiteral("群聊")));
        appendSystemMessage(QStringLiteral("已自动邀请 %1 位好友加入群聊").arg(inviteIds.size()));
        saveHistory(groupId,
                    QStringLiteral("[%1] [系统] 已自动邀请 %2 位好友加入群聊")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")))
                        .arg(inviteIds.size()));
        return true;
    }
    if (commandId == QLatin1String("copy-group-online-members")) {
        const GroupNoticeMemberCopyState state =
            NotificationPanelManager::groupMemberCopyState(groupMemberCopyInputs(m_localGroupMembers.value(groupId)), true);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return true;
        }
        copyTextWithStatus(state.rows.join(QLatin1Char('\n')), state.copiedStatusMessage, 2200);
        return true;
    }
    if (commandId == QLatin1String("rename-group")) {
        bool ok = false;
        const QString oldName = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
        const QString newName = promptTextValue(QStringLiteral("重命名群聊"),
                                                QStringLiteral("群聊名称:"),
                                                oldName,
                                                &ok);
        if (!ok) {
            return true;
        }
        if (newName.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("群聊名称不能为空"), 1800);
            return true;
        }
        if (newName == oldName) {
            ui->statusbar->showMessage(QStringLiteral("群聊名称未改变"), 1600);
            return true;
        }
        m_localGroupNames[groupId] = newName;
        saveLocalGroups();
        refreshFriendList();
        if (m_privateChatTarget == groupId) {
            switchToLocalGroup(groupId, newName);
        }
        ui->statusbar->showMessage(QStringLiteral("群聊已重命名为：%1").arg(newName), 2200);
        return true;
    }
    if (commandId == QLatin1String("delete-group")) {
        const QString groupName = m_localGroupNames.value(groupId, groupLabel);
        const int memberCount = qMax(1, m_localGroupMembers.value(groupId).size());
        if (!confirmAction(QStringLiteral("删除群聊"),
                           QStringLiteral("确定删除群聊“%1”吗？本地群成员 %2 人，聊天记录不会在此步骤删除。")
                               .arg(groupName)
                               .arg(memberCount),
                           QStringLiteral("已取消删除群聊"))) {
            return true;
        }
        m_localGroupIds.removeAll(groupId);
        m_localGroupNames.remove(groupId);
        m_localGroupAnnouncements.remove(groupId);
        m_localGroupMembers.remove(groupId);
        saveLocalGroups();
        refreshFriendList();
        if (m_privateChatTarget == groupId) {
            onBackToGroupChat();
        }
        appendSystemMessage(QStringLiteral("已删除群聊: ") + groupName);
        return true;
    }

    return false;
}

bool MainWindow::handleContactContextCommand(const QString& userId,
                                             const QString& commandId) {
    if (commandId == QLatin1String("copy-account")) {
        copyTextWithStatus(userId, QStringLiteral("QQ 号已复制: ") + userId, 2500);
        return true;
    }
    if (commandId == QLatin1String("copy-profile-card")) {
        const QString card = QStringLiteral("QQ:%1\n昵称:%2\n状态:%3")
            .arg(userId,
                 contactDisplayName(userId),
                 isContactOnline(userId) ? QStringLiteral("在线") : QStringLiteral("离线"));
        copyTextWithStatus(card, QStringLiteral("联系人名片已复制"), 1800);
        return true;
    }
    if (commandId == QLatin1String("copy-add-text")) {
        const QString text = QStringLiteral("你好，我是 %1（QQ:%2），通过 QQ 搜索看到你。方便的话加个好友，我们可以私聊或一起进群沟通。")
            .arg(m_currentUserName, m_currentUserId);
        copyTextWithStatus(text, QStringLiteral("好友申请话术已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-online-card")) {
        const QString card = QStringLiteral("QQ:%1\n昵称:%2\n状态:%3\n关系:%4\n当前会话:%5")
            .arg(userId,
                 contactDisplayName(userId),
                 isContactOnline(userId) ? QStringLiteral("在线") : QStringLiteral("离线"),
                 m_friendIds.contains(userId) ? QStringLiteral("好友")
                                              : (m_pendingOutgoingFriendRequests.contains(userId) ? QStringLiteral("申请中")
                                                                                                  : QStringLiteral("陌生人")),
                 m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget));
        copyTextWithStatus(card, QStringLiteral("在线名片已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-chat-starter")) {
        const QString text = m_friendIds.contains(userId)
            ? QStringLiteral("%1，在吗？我是 %2（QQ:%3），想和你私聊确认一下刚才的消息。")
                  .arg(contactDisplayName(userId), m_currentUserName, m_currentUserId)
            : m_pendingOutgoingFriendRequests.contains(userId)
                ? QStringLiteral("%1，你好，我是 %2（QQ:%3），我已经发送好友申请了，通过后我们可以继续私聊。")
                      .arg(contactDisplayName(userId), m_currentUserName, m_currentUserId)
                : QStringLiteral("你好 %1，我是 %2（QQ:%3）。通过 QQ 搜索看到你，方便通过好友申请后再聊吗？")
                      .arg(contactDisplayName(userId), m_currentUserName, m_currentUserId);
        copyTextWithStatus(text, QStringLiteral("开聊话术已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-e2e-status")) {
        copyE2ESessionStatus(userId);
        return true;
    }
    if (commandId == QLatin1String("copy-e2e-identity")) {
        const QJsonObject identity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
        const QString text = identity.value(QStringLiteral("configured")).toBool(false)
            ? QStringLiteral("端到端加密身份\n对端QQ：%1\n信任状态：%2\n验证状态：%3\n公钥指纹：%4\n验证短码：%5\n首次看到：%6\n最近看到：%7")
                  .arg(userId,
                       identity.value(QStringLiteral("trustState")).toString(),
                       identity.value(QStringLiteral("verified")).toBool(false) ? QStringLiteral("verified")
                                                                                 : QStringLiteral("unverified"),
                       identity.value(QStringLiteral("publicKeyFingerprintSha256")).toString(),
                       identity.value(QStringLiteral("verificationCodeDisplay")).toString(),
                       identity.value(QStringLiteral("firstSeenAt")).toString(),
                       identity.value(QStringLiteral("lastSeenAt")).toString())
            : QStringLiteral("端到端加密身份\n对端QQ：%1\n信任状态：unknown\n说明：尚未收到该联系人的身份公告").arg(userId);
        copyTextWithStatus(text, QStringLiteral("端到端加密身份指纹已复制"), 2400);
        return true;
    }
    if (commandId == QLatin1String("copy-e2e-verification")) {
        const QJsonObject identity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
        const QString code = identity.value(QStringLiteral("verificationCodeDisplay")).toString();
        const QString text = identity.value(QStringLiteral("configured")).toBool(false) && !code.isEmpty()
            ? QStringLiteral("端到端加密验证短码\n对端QQ：%1\n短码：%2\n核对方式：请通过另一个可信渠道与对方屏幕上的短码一致后再验证信任。")
                  .arg(userId, code)
            : QStringLiteral("端到端加密验证短码\n对端QQ：%1\n说明：尚未收到该联系人的身份公告").arg(userId);
        copyTextWithStatus(text, QStringLiteral("端到端加密验证短码已复制"), 2400);
        return true;
    }
    if (commandId == QLatin1String("trust-e2e-identity")) {
        QString rejectReason;
        if (m_client && m_client->pinE2EPeerIdentity(userId, QString(), &rejectReason)) {
            appendSystemMessage(QStringLiteral("已固定 %1 的端到端加密身份指纹；完成验证短码核对前不会启用默认加密")
                                    .arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("端到端加密身份已固定，等待验证"), 2600);
        } else {
            appendSystemMessage(QStringLiteral("信任端到端加密身份失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("信任端到端加密身份失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("verify-e2e-identity")) {
        const QJsonObject identity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
        const QString suggestedCode = identity.value(QStringLiteral("verificationCodeDisplay")).toString();
        bool ok = false;
        const QString code = promptTextValue(QStringLiteral("验证加密身份"),
                                             QStringLiteral("请输入与 %1 通过可信渠道核对一致的验证短码:")
                                                 .arg(contactDisplayName(userId)),
                                             suggestedCode,
                                             &ok);
        if (!ok) {
            ui->statusbar->showMessage(QStringLiteral("已取消端到端加密身份验证"), 1800);
            return true;
        }
        QString rejectReason;
        if (m_client && m_client->verifyAndPinE2EPeerIdentity(userId, code, &rejectReason)) {
            appendSystemMessage(QStringLiteral("已验证并信任 %1 的端到端加密身份").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("端到端加密身份已验证"), 2400);
        } else {
            appendSystemMessage(QStringLiteral("验证端到端加密身份失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("验证端到端加密身份失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("clear-e2e-identity-trust")) {
        QString rejectReason;
        if (m_client && m_client->clearE2EPeerIdentityPin(userId, &rejectReason)) {
            appendSystemMessage(QStringLiteral("已清除 %1 的端到端加密身份信任固定").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("端到端加密身份信任已清除"), 2400);
        } else {
            appendSystemMessage(QStringLiteral("清除端到端加密身份信任失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("清除端到端加密身份信任失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("request-e2e-rotation")) {
        QString rejectReason;
        if (m_client && m_client->requestE2ESessionRotation(userId, &rejectReason)) {
            appendSystemMessage(QStringLiteral("已向 %1 发送端到端加密轮换请求").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("端到端加密轮换请求已发送"), 2400);
        } else {
            appendSystemMessage(QStringLiteral("端到端加密轮换请求发送失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("端到端加密轮换请求发送失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("clear-e2e-session")) {
        if (m_client) {
            m_client->clearE2ESessionKey(userId);
            appendSystemMessage(QStringLiteral("已关闭 %1 的本机端到端加密会话").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("本机端到端加密会话已关闭"), 2400);
        }
        return true;
    }
    if (commandId == QLatin1String("clear-e2e-backend-migration")) {
        QString rejectReason;
        if (m_client && m_client->clearE2EBackendMigrationState(&rejectReason)) {
            appendSystemMessage(QStringLiteral("已清理本机端到端加密后端迁移状态；需要重新接收身份公告、核对短码并建立会话后才能继续默认加密"));
            ui->statusbar->showMessage(QStringLiteral("端到端加密迁移状态已清理"), 3200);
        } else {
            appendSystemMessage(QStringLiteral("清理端到端加密迁移状态失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("清理端到端加密迁移状态失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("invite-current-group")) {
        QString requestNote;
        if (!m_friendIds.contains(userId)) {
            if (m_pendingOutgoingFriendRequests.contains(userId)) {
                requestNote = QStringLiteral("，好友申请已在等待确认");
            } else if (m_client && m_client->sendFriendRequest(userId)) {
                m_friendNames[userId] = contactDisplayName(userId);
                m_pendingOutgoingFriendRequests << userId;
                requestNote = QStringLiteral("，好友申请等待确认");
            } else {
                requestNote = QStringLiteral("，好友申请发送失败");
                ui->statusbar->showMessage(QStringLiteral("已邀请入群，但好友申请发送失败：%1").arg(contactDisplayName(userId)), 3000);
            }
        }
        if (!m_localGroupMembers[m_privateChatTarget].contains(userId)) {
            m_localGroupMembers[m_privateChatTarget] << userId;
            saveLocalGroups();
        }
        refreshFriendList();
        refreshGroupMemberPanel();
        appendSystemMessage(QStringLiteral("已邀请 %1 加入当前群聊%2").arg(contactDisplayName(userId), requestNote));
        saveHistory(m_privateChatTarget,
                    QStringLiteral("[%1] [系统] 已邀请 %2 加入群聊")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), contactDisplayName(userId)));
        return true;
    }
    if (commandId == QLatin1String("rename-friend")) {
        bool ok = false;
        const QString oldRemark = contactDisplayName(userId);
        const QString remark = promptTextValue(QStringLiteral("设置备注"),
                                               QStringLiteral("备注名称:"),
                                               oldRemark,
                                               &ok);
        if (!ok) {
            return true;
        }
        if (remark.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("备注名称不能为空"), 1800);
            return true;
        }
        if (remark == oldRemark) {
            ui->statusbar->showMessage(QStringLiteral("备注未改变"), 1600);
            return true;
        }
        m_friendNames[userId] = remark;
        saveFriends();
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->statusbar->showMessage(QStringLiteral("已设置备注：%1").arg(remark), 2200);
        appendSystemMessage(QStringLiteral("已设置 %1 的备注为 %2").arg(userId, remark));
        return true;
    }
    if (commandId == QLatin1String("add-friend")) {
        if (m_pendingOutgoingFriendRequests.contains(userId)) {
            ui->statusbar->showMessage(QStringLiteral("已向 %1 发送过好友申请，等待对方处理").arg(contactDisplayName(userId)), 2500);
            return true;
        }
        if (!m_friendIds.contains(userId)) {
            const QString displayName = contactDisplayName(userId);
            if (!m_client || !m_client->sendFriendRequest(userId)) {
                ui->statusbar->showMessage(QStringLiteral("好友申请发送失败：%1").arg(displayName), 3000);
                appendSystemMessage(QStringLiteral("好友申请发送失败 QQ:%1，请检查连接后重试").arg(userId));
                return true;
            }
            m_friendNames[userId] = displayName;
            m_pendingOutgoingFriendRequests << userId;
            refreshFriendList();
            appendSystemMessage(QStringLiteral("已发送好友申请: %1（QQ:%2），等待对方同意").arg(displayName, userId));
        }
        return true;
    }
    if (commandId == QLatin1String("remove-friend")) {
        const QString displayName = contactDisplayName(userId);
        if (!confirmAction(QStringLiteral("删除好友"),
                           QStringLiteral("确定删除好友“%1”（QQ:%2）吗？删除后可重新搜索并发送申请。").arg(displayName, userId),
                           QStringLiteral("已取消删除好友"))) {
            return true;
        }
        m_friendIds.removeAll(userId);
        m_friendNames.remove(userId);
        saveFriends();
        refreshFriendList();
        appendSystemMessage(QStringLiteral("已删除好友: %1（QQ:%2）").arg(displayName, userId));
        return true;
    }
    return false;
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
        const struct GroupActionSpec {
            const char* id;
            const char* title;
            const char* tip;
        } specs[] = {
            {"open-group", "进入群聊", "进入当前本地群聊并加载聊天记录"},
            {"copy-group-id", "复制群号", "复制当前群聊的群号"},
            {"copy-group-card", "复制群名片", "复制群名、群号、成员数和公告摘要"},
            {"copy-group-invite", "复制群邀请语", "复制一段可发送给好友的入群邀请语"},
            {"copy-group-members", "复制成员列表", "复制当前群聊的全部成员列表"},
            {"invite-friend", "邀请好友", "从好友列表中选择一个好友邀请入群"},
            {"invite-by-account", "按QQ号邀请", "输入 QQ 号邀请用户入群，并按需发送好友申请"},
            {"invite-all-friends", "邀请全部好友", "把当前全部好友批量邀请进该群聊"},
            {"copy-group-online-members", "复制在线成员", "复制当前群聊在线成员的 QQ 和昵称"},
            {"rename-group", "重命名群聊", "修改当前本地群聊名称"},
            {"delete-group", "删除群聊", "删除当前本地群聊配置，聊天记录不会在此步骤删除"}
        };
        for (const GroupActionSpec& spec : specs) {
            QAction* action = menu.addAction(QString::fromUtf8(spec.title));
            action->setData(QString::fromLatin1(spec.id));
            action->setToolTip(QString::fromUtf8(spec.tip));
            action->setStatusTip(QString::fromUtf8(spec.tip));
        }
        QAction* selected = menu.exec(ui->userListView->viewport()->mapToGlobal(pos));
        if (!selected) return;
        handleLocalGroupContextCommand(userId, userName, selected->data().toString());
        return;
    }

    const QJsonObject currentE2EIdentity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
    const QJsonObject currentE2ESession = m_client ? m_client->e2eSessionStatus(userId) : QJsonObject();
    const QJsonObject currentE2ELocalIdentity = m_client ? m_client->e2eLocalIdentityStatus() : QJsonObject();
    const bool e2eBackendMigrationRequired =
        currentE2ELocalIdentity.value("backendMigrationRequired").toBool(false)
        || currentE2EIdentity.value("backendMigrationRequired").toBool(false)
        || currentE2ESession.value("backendMigrationRequired").toBool(false);
    const bool hasPendingOutgoing = m_pendingOutgoingFriendRequests.contains(userId);
    const struct ContactActionSpec {
        const char* id;
        const char* title;
        const char* tip;
        bool enabled;
    } baseSpecs[] = {
        {"chat", "发送消息", "打开当前联系人私聊会话", true},
        {"copy-account", "复制QQ号", "复制当前联系人 QQ 号", true},
        {"copy-profile-card", "复制名片", "复制当前联系人 QQ、昵称和关系状态", true},
        {"copy-add-text", "复制申请话术", "复制适合当前联系人的好友申请话术", true},
        {"copy-online-card", "复制在线名片", "复制当前联系人的在线名片和状态", true},
        {"copy-chat-starter", "复制开聊话术", "复制一段可直接发送的开聊话术", true},
        {"copy-e2e-status", "复制加密状态", "复制当前联系人端到端加密会话状态", true},
        {"copy-e2e-identity", "复制加密身份指纹", "复制本机记录的联系人端到端加密身份指纹", true},
        {"copy-e2e-verification", "复制加密验证短码", "复制需要与对方跨设备核对的端到端加密验证短码", true},
        {"trust-e2e-identity", "信任加密身份", "仅固定当前记录的联系人端到端加密身份指纹，仍需验证短码后才能用于默认加密", true},
        {"verify-e2e-identity", "验证并信任加密身份", "输入与对方核对一致的验证短码并将身份标记为已验证信任", true},
        {"request-e2e-rotation", "请求加密轮换", "向当前联系人发送端到端加密会话轮换请求；不包含本机会话密钥", true}
    };
    for (const ContactActionSpec& spec : baseSpecs) {
        QAction* action = menu.addAction(QString::fromUtf8(spec.title));
        action->setData(QString::fromLatin1(spec.id));
        action->setToolTip(QString::fromUtf8(spec.tip));
        action->setStatusTip(QString::fromUtf8(spec.tip));
        action->setEnabled(spec.enabled);
    }
    if (currentE2EIdentity.value("pinned").toBool(false)) {
        QAction* action = menu.addAction(QStringLiteral("清除加密身份信任"));
        action->setData(QStringLiteral("clear-e2e-identity-trust"));
        action->setToolTip(QStringLiteral("清除当前联系人端到端加密身份固定信任并恢复为未验证"));
        action->setStatusTip(action->toolTip());
    }
    if (m_client && m_client->hasE2ESession(userId)) {
        QAction* action = menu.addAction(QStringLiteral("关闭本机会话密钥"));
        action->setData(QStringLiteral("clear-e2e-session"));
        action->setToolTip(QStringLiteral("清除本机为该联系人保存的端到端会话密钥"));
        action->setStatusTip(action->toolTip());
    }
    if (e2eBackendMigrationRequired) {
        QAction* action = menu.addAction(QStringLiteral("清理加密后端迁移状态"));
        action->setData(QStringLiteral("clear-e2e-backend-migration"));
        action->setToolTip(QStringLiteral("清除本机旧加密后端身份、信任和会话状态，等待新后端重新建立信任"));
        action->setStatusTip(action->toolTip());
    }
    if (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
        QAction* action = menu.addAction(QStringLiteral("邀入当前群"));
        action->setData(QStringLiteral("invite-current-group"));
        action->setToolTip(QStringLiteral("邀请当前联系人加入正在查看的本地群聊"));
        action->setStatusTip(action->toolTip());
    }
    if (m_friendIds.contains(userId)) {
        QAction* renameAction = menu.addAction(QStringLiteral("设置备注"));
        renameAction->setData(QStringLiteral("rename-friend"));
        renameAction->setToolTip(QStringLiteral("修改当前好友在本地显示的备注名"));
        renameAction->setStatusTip(renameAction->toolTip());
        QAction* removeAction = menu.addAction(QStringLiteral("删除好友"));
        removeAction->setData(QStringLiteral("remove-friend"));
        removeAction->setToolTip(QStringLiteral("从本地好友列表删除当前好友"));
        removeAction->setStatusTip(removeAction->toolTip());
    } else {
        QAction* addAction = menu.addAction(hasPendingOutgoing ? QStringLiteral("好友申请待确认") : QStringLiteral("加为好友"));
        addAction->setData(QStringLiteral("add-friend"));
        addAction->setEnabled(!hasPendingOutgoing);
        addAction->setToolTip(hasPendingOutgoing ? QStringLiteral("好友申请已发送，等待对方处理")
                                                 : QStringLiteral("向当前联系人发送好友申请"));
        addAction->setStatusTip(addAction->toolTip());
    }

    QAction* selected = menu.exec(ui->userListView->viewport()->mapToGlobal(pos));
    if (!selected) return;
    const QString commandId = selected->data().toString();
    if (commandId == QLatin1String("chat")) {
        onPrivateChat(index);
    } else {
        handleContactContextCommand(userId, commandId);
    }
}

void MainWindow::updateFriendNoticeBadge() {
#ifndef QT_NO_DEBUG
    const FriendNoticeUiState noticeState = m_friendManager.noticeUiState(m_pendingFriendRequests.size());
    Q_UNUSED(noticeState)
#else
    Q_UNUSED(m_friendManager)
#endif
}

void MainWindow::onShowFriendNotifications() {
    const FriendNoticeDialogChrome chrome = NotificationPanelManager::friendNoticeDialogChrome();
    QDialog dialog(this);
    dialog.setObjectName("noticeDialog");
    dialog.setWindowTitle(chrome.windowTitle);
    dialog.setFixedSize(chrome.dialogSize);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(18);

    QHBoxLayout* titleLayout = new QHBoxLayout;
    QLabel* titleLabel = new QLabel(chrome.titleText, &dialog);
    titleLabel->setObjectName("noticeTitle");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    QPushButton* clearBtn = new QPushButton(chrome.clearButton.text, &dialog);
    clearBtn->setObjectName(chrome.clearButton.objectName);
    clearBtn->setToolTip(chrome.clearButton.toolTip);
    titleLayout->addWidget(clearBtn);
    layout->addLayout(titleLayout);

    QLabel* statsLabel = new QLabel(&dialog);
    statsLabel->setObjectName("noticeSubTitle");
    layout->addWidget(statsLabel);

    QLineEdit* searchEdit = new QLineEdit(&dialog);
    searchEdit->setObjectName("noticeSearch");
    searchEdit->setPlaceholderText(chrome.searchPlaceholder);
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setToolTip(chrome.searchToolTip);
    layout->addWidget(searchEdit);

    QListWidget* noticeList = new QListWidget(&dialog);
    noticeList->setObjectName("noticeList");
    noticeList->setWordWrap(true);
    layout->addWidget(noticeList, 1);

    auto fillList = [this, noticeList, statsLabel, searchEdit]() {
        noticeList->clear();
        const FriendNoticeListRenderUiState renderState =
            NotificationPanelManager::friendNoticeListRenderUiState(m_pendingFriendRequests,
                                                                    m_friendNames,
                                                                    m_friendIds.size(),
                                                                    searchEdit->text());
        statsLabel->setText(renderState.statsText);
        for (const FriendNoticeListEntryUiState& entry : renderState.entries) {
            QListWidgetItem* item = new QListWidgetItem(entry.text);
            item->setData(Qt::UserRole, entry.entryId);
            item->setSizeHint(QSize(0, entry.rowHeight));
            item->setToolTip(entry.toolTip);
            if (!entry.enabled) {
                item->setFlags(Qt::NoItemFlags);
            }
            if (entry.muted) {
                item->setForeground(QColor(135, 150, 165));
            } else if (entry.accent) {
                item->setForeground(QColor(18, 150, 247));
            }
            noticeList->addItem(item);
        }
        for (int i = 0; i < noticeList->count(); ++i) {
            if (noticeList->item(i)->flags().testFlag(Qt::ItemIsEnabled)) {
                noticeList->setCurrentRow(i);
                break;
            }
        }
    };
    fillList();

    QLabel* requestPreviewLabel = new QLabel(chrome.previewPlaceholder, &dialog);
    requestPreviewLabel->setObjectName("noticePreviewLabel");
    layout->addWidget(requestPreviewLabel);

    QVBoxLayout* buttonLayout = new QVBoxLayout;
    buttonLayout->setSpacing(8);
    QHBoxLayout* decisionButtonLayout = new QHBoxLayout;
    decisionButtonLayout->setSpacing(8);
    QHBoxLayout* copyButtonLayout = new QHBoxLayout;
    copyButtonLayout->setSpacing(8);
    QHBoxLayout* mediaButtonLayout = new QHBoxLayout;
    mediaButtonLayout->setSpacing(8);
    QPushButton* acceptBtn = new QPushButton(chrome.acceptButton.text, &dialog);
    acceptBtn->setObjectName(chrome.acceptButton.objectName);
    acceptBtn->setToolTip(chrome.acceptButton.toolTip);
    QPushButton* acceptAllBtn = new QPushButton(chrome.acceptAllButton.text, &dialog);
    acceptAllBtn->setObjectName(chrome.acceptAllButton.objectName);
    acceptAllBtn->setToolTip(chrome.acceptAllButton.toolTip);
    QPushButton* rejectBtn = new QPushButton(chrome.rejectButton.text, &dialog);
    rejectBtn->setObjectName(chrome.rejectButton.objectName);
    rejectBtn->setToolTip(chrome.rejectButton.toolTip);
    QPushButton* rejectAllBtn = new QPushButton(chrome.rejectAllButton.text, &dialog);
    rejectAllBtn->setObjectName(chrome.rejectAllButton.objectName);
    rejectAllBtn->setToolTip(chrome.rejectAllButton.toolTip);
    QPushButton* copyBtn = new QPushButton(chrome.copyCardButton.text, &dialog);
    copyBtn->setObjectName(chrome.copyCardButton.objectName);
    copyBtn->setToolTip(chrome.copyCardButton.toolTip);
    QPushButton* copyInviteBtn = new QPushButton(chrome.copyInviteButton.text, &dialog);
    copyInviteBtn->setObjectName(chrome.copyInviteButton.objectName);
    copyInviteBtn->setToolTip(chrome.copyInviteButton.toolTip);
    QPushButton* copyAllBtn = new QPushButton(chrome.copyAllButton.text, &dialog);
    copyAllBtn->setObjectName(chrome.copyAllButton.objectName);
    copyAllBtn->setToolTip(chrome.copyAllButton.toolTip);
    QPushButton* copyRequestMediaPackBtn = new QPushButton(chrome.copyMediaPackButton.text, &dialog);
    copyRequestMediaPackBtn->setObjectName(chrome.copyMediaPackButton.objectName);
    copyRequestMediaPackBtn->setToolTip(chrome.copyMediaPackButton.toolTip);
    QPushButton* copyRequestBatchPlanBtn = new QPushButton(chrome.copyBatchPlanButton.text, &dialog);
    copyRequestBatchPlanBtn->setObjectName(chrome.copyBatchPlanButton.objectName);
    copyRequestBatchPlanBtn->setToolTip(chrome.copyBatchPlanButton.toolTip);
    QPushButton* copyMediaGuideBtn = new QPushButton(chrome.copyMediaGuideButton.text, &dialog);
    copyMediaGuideBtn->setObjectName(chrome.copyMediaGuideButton.objectName);
    copyMediaGuideBtn->setToolTip(chrome.copyMediaGuideButton.toolTip);
    QPushButton* closeBtn = new QPushButton(chrome.closeButton.text, &dialog);
    closeBtn->setObjectName(chrome.closeButton.objectName);
    closeBtn->setToolTip(chrome.closeButton.toolTip);
    decisionButtonLayout->addWidget(acceptBtn);
    decisionButtonLayout->addWidget(acceptAllBtn);
    decisionButtonLayout->addWidget(rejectBtn);
    decisionButtonLayout->addWidget(rejectAllBtn);
    decisionButtonLayout->addStretch();
    decisionButtonLayout->addWidget(closeBtn);
    copyButtonLayout->addWidget(copyBtn);
    copyButtonLayout->addWidget(copyInviteBtn);
    copyButtonLayout->addWidget(copyAllBtn);
    copyButtonLayout->addStretch();
    mediaButtonLayout->addWidget(copyRequestMediaPackBtn);
    mediaButtonLayout->addWidget(copyRequestBatchPlanBtn);
    mediaButtonLayout->addWidget(copyMediaGuideBtn);
    mediaButtonLayout->addStretch();
    buttonLayout->addLayout(decisionButtonLayout);
    buttonLayout->addLayout(copyButtonLayout);
    buttonLayout->addLayout(mediaButtonLayout);
    layout->addLayout(buttonLayout);

    dialog.setStyleSheet(NotificationPanelManager::friendNoticeDialogStyleSheet());

    auto updateBadge = [this]() {
        updateFriendNoticeBadge();
    };
    auto currentRequestId = [noticeList]() -> QString {
        return selectedFriendNoticeEntryId(noticeList);
    };
    auto selectedFriendNoticeTarget = [this, noticeList, searchEdit]() {
        return friendNoticeVisibleTarget(selectedFriendNoticeTargetId(noticeList, searchEdit));
    };
    auto visibleFriendNoticeTargetsFn = [this, noticeList]() {
        return visibleFriendNoticeTargets(noticeList);
    };
    auto updateRequestActionState = [=]() {
        const FriendNoticeActionState state = NotificationPanelManager::friendNoticeActionState(
            currentRequestId(),
            !m_pendingFriendRequests.isEmpty(),
            !searchEdit->text().trimmed().isEmpty());
        acceptBtn->setEnabled(state.acceptEnabled);
        acceptBtn->setText(state.acceptText);
        acceptBtn->setToolTip(state.acceptToolTip);
        rejectBtn->setEnabled(state.rejectEnabled);
        rejectBtn->setToolTip(state.rejectToolTip);
        copyBtn->setEnabled(state.copyCardEnabled);
        copyBtn->setToolTip(state.copyCardToolTip);
        copyInviteBtn->setEnabled(state.copyInviteEnabled);
        copyInviteBtn->setToolTip(state.copyInviteToolTip);
        copyAllBtn->setEnabled(state.copyAllEnabled);
        copyAllBtn->setToolTip(state.copyAllToolTip);
        copyRequestMediaPackBtn->setEnabled(state.copyMediaPackEnabled);
        copyRequestMediaPackBtn->setToolTip(state.copyMediaPackToolTip);
        copyRequestBatchPlanBtn->setEnabled(state.copyBatchPlanEnabled);
        copyRequestBatchPlanBtn->setToolTip(state.copyBatchPlanToolTip);
        copyMediaGuideBtn->setEnabled(state.copyMediaGuideEnabled);
        copyMediaGuideBtn->setToolTip(state.copyMediaGuideToolTip);
        acceptAllBtn->setEnabled(state.acceptAllEnabled);
        acceptAllBtn->setToolTip(state.acceptAllToolTip);
        rejectAllBtn->setEnabled(state.rejectAllEnabled);
        rejectAllBtn->setToolTip(state.rejectAllToolTip);
        clearBtn->setEnabled(state.clearEnabled);
        clearBtn->setToolTip(state.clearToolTip);
    };
    auto updateRequestPreview = [this, noticeList, searchEdit, requestPreviewLabel]() {
        const FriendNoticeSelectionSnapshot snapshot = currentFriendNoticeSelectionSnapshot(noticeList, searchEdit);
        requestPreviewLabel->setText(snapshot.previewText);
    };
    updateRequestPreview();
    updateRequestActionState();
    connect(noticeList, &QListWidget::currentItemChanged, &dialog, [updateRequestPreview, updateRequestActionState](QListWidgetItem*, QListWidgetItem*) {
        updateRequestPreview();
        updateRequestActionState();
    });
    connect(searchEdit, &QLineEdit::textChanged, &dialog, [fillList, updateRequestPreview, updateRequestActionState]() {
        fillList();
        updateRequestPreview();
        updateRequestActionState();
    });
    connect(searchEdit, &QLineEdit::returnPressed, &dialog, [this, searchEdit]() {
        QString account = searchEdit->text().trimmed();
        if (account.isEmpty()) {
            searchEdit->setFocus();
            ui->statusbar->showMessage("请输入申请人 QQ 号后搜索", 1800);
            return;
        }
        searchAndAddAccount(account, this);
    });
    connect(acceptBtn, &QPushButton::clicked, &dialog, [this, noticeList, fillList, updateBadge, updateRequestActionState]() {
        const QString id = selectedFriendNoticeEntryId(noticeList);
        if (id.startsWith("search_add:")) {
            searchAndAddAccount(id.mid(QString("search_add:").size()), this);
            return;
        }
        if (id.isEmpty()) {
            ui->statusbar->showMessage("暂无可同意的好友申请", 1800);
            return;
        }
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
        updateRequestActionState();
        const FriendNoticeRequestDecisionState state =
            FriendManager::friendNoticeRequestDecisionState(id, name, true);
        ui->statusbar->showMessage(state.statusMessage, 2200);
        appendSystemMessage(state.systemMessage);
    });
    connect(acceptAllBtn, &QPushButton::clicked, &dialog, [this, fillList, updateBadge, updateRequestActionState, &dialog]() {
        QStringList pending = m_pendingFriendRequests;
        const FriendNoticeBulkActionState actionState =
            FriendManager::friendNoticeBulkActionState(FriendNoticeBulkActionKind::AcceptAll,
                                                       pending.size());
        if (pending.isEmpty()) {
            ui->statusbar->showMessage(actionState.emptyStatusMessage, 1800);
            return;
        }
        if (QMessageBox::question(&dialog,
                                  actionState.title,
                                  actionState.questionText,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage(actionState.cancelledStatusMessage, 1600);
            return;
        }
        for (const QString& id : pending) {
            if (id.isEmpty()) continue;
            QString name = m_friendNames.value(id, id);
            m_client->sendFriendResponse(id, true);
            if (!m_friendIds.contains(id)) {
                m_friendIds << id;
            }
            m_friendNames[id] = name;
        }
        m_pendingFriendRequests.clear();
        saveFriends();
        refreshFriendList();
        updateBadge();
        fillList();
        updateRequestActionState();
        ui->statusbar->showMessage(actionState.successStatusMessage, 2200);
        appendSystemMessage(actionState.systemMessage);
    });
    connect(rejectBtn, &QPushButton::clicked, &dialog, [this, noticeList, fillList, updateBadge, updateRequestActionState]() {
        QString id;
        if (!trySelectedRealFriendNoticeId(noticeList,
                                           ui->statusbar,
                                           "请先选择要拒绝的好友申请",
                                           "这是搜索占位项，可先搜索并发送申请",
                                           &id)) {
            return;
        }
        m_client->sendFriendResponse(id, false);
        m_pendingFriendRequests.removeAll(id);
        saveFriends();
        updateBadge();
        fillList();
        updateRequestActionState();
        const FriendNoticeRequestDecisionState state =
            FriendManager::friendNoticeRequestDecisionState(id, QString(), false);
        ui->statusbar->showMessage(state.statusMessage, 2200);
        appendSystemMessage(state.systemMessage);
    });
    connect(rejectAllBtn, &QPushButton::clicked, &dialog, [this, fillList, updateBadge, updateRequestActionState, &dialog]() {
        QStringList pending = m_pendingFriendRequests;
        const FriendNoticeBulkActionState actionState =
            FriendManager::friendNoticeBulkActionState(FriendNoticeBulkActionKind::RejectAll,
                                                       pending.size());
        if (pending.isEmpty()) {
            ui->statusbar->showMessage(actionState.emptyStatusMessage, 1800);
            return;
        }
        if (QMessageBox::question(&dialog,
                                  actionState.title,
                                  actionState.questionText,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage(actionState.cancelledStatusMessage, 1600);
            return;
        }
        for (const QString& id : pending) {
            if (!id.isEmpty()) {
                m_client->sendFriendResponse(id, false);
            }
        }
        m_pendingFriendRequests.clear();
        saveFriends();
        updateBadge();
        fillList();
        updateRequestActionState();
        ui->statusbar->showMessage(actionState.successStatusMessage, 2200);
        appendSystemMessage(actionState.systemMessage);
    });
    connect(copyBtn, &QPushButton::clicked, &dialog, [this, noticeList]() {
        QString id;
        if (!trySelectedRealFriendNoticeId(noticeList,
                                           ui->statusbar,
                                           "请先选择要复制的好友申请",
                                           "这是搜索占位项，请先搜索申请人",
                                           &id)) {
            return;
        }
        copyTextWithStatus(
            FriendManager::friendNoticeApplicantCardText(id, m_friendNames.value(id, id)),
            "申请人名片已复制");
    });
    connect(copyInviteBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        QString id = selectedFriendNoticeTargetId(noticeList, searchEdit);
        QString name = id.isEmpty() ? "朋友" : m_friendNames.value(id, contactDisplayName(id));
        copyTextWithStatus(
            FriendManager::friendNoticeReplyText(name, m_currentUserName, m_currentUserId),
            "申请回复话术已复制",
            2200);
    });
    connect(copyAllBtn, &QPushButton::clicked, &dialog, [this]() {
        const QString bulkText = FriendManager::friendNoticeBulkCopyText(
            m_pendingFriendRequests,
            m_friendNames,
            m_currentUserName,
            m_currentUserId);
        if (bulkText.isEmpty()) {
            ui->statusbar->showMessage("暂无好友申请可复制", 2200);
            return;
        }
        copyTextWithStatus(
            bulkText,
            QString("已复制 %1 条好友申请").arg(m_pendingFriendRequests.size()),
            2200);
    });
    connect(copyRequestMediaPackBtn, &QPushButton::clicked, &dialog, [this, selectedFriendNoticeTarget]() {
        const GlobalSearchSelectionCopyState state = FriendManager::friendNoticeMediaPackState(
            m_currentUserId,
            m_currentUserName,
            m_pendingFriendRequests.size(),
            selectedFriendNoticeTarget());
        copyTextWithStatus(state.text, "好友申请媒体包已复制", 2200);
    });
    connect(copyRequestBatchPlanBtn, &QPushButton::clicked, &dialog, [this, visibleFriendNoticeTargetsFn, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::friendNoticeBatchPlanState(
            m_currentUserId,
            m_currentUserName,
            m_pendingFriendRequests.size(),
            searchEdit->text().trimmed(),
            visibleFriendNoticeTargetsFn());
        copyTextWithStatus(state.text, "好友申请处理计划已复制", 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this, selectedFriendNoticeTarget]() {
        copyTextWithStatus(
            FriendManager::friendNoticeMediaGuideText(
                m_currentUserId,
                m_currentUserName,
                selectedFriendNoticeTarget()),
            "好友申请上传指南已复制",
            2200);
    });
    connect(clearBtn, &QPushButton::clicked, &dialog, [this, fillList, updateBadge, updateRequestActionState, &dialog]() {
        const FriendNoticeBulkActionState actionState =
            FriendManager::friendNoticeBulkActionState(FriendNoticeBulkActionKind::ClearAll,
                                                       m_pendingFriendRequests.size());
        if (m_pendingFriendRequests.isEmpty()) {
            ui->statusbar->showMessage(actionState.emptyStatusMessage, 1600);
            return;
        }
        if (QMessageBox::question(&dialog,
                                  actionState.title,
                                  actionState.questionText,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage(actionState.cancelledStatusMessage, 1600);
            return;
        }
        m_pendingFriendRequests.clear();
        saveFriends();
        updateBadge();
        fillList();
        updateRequestActionState();
        ui->statusbar->showMessage(actionState.successStatusMessage, 1800);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::onShowGroupNotifications() {
    const GroupNoticeDialogChrome chrome = NotificationPanelManager::groupNoticeDialogChrome();
    QDialog dialog(this);
    dialog.setObjectName("noticeDialog");
    dialog.setWindowTitle(chrome.windowTitle);
    dialog.setFixedSize(chrome.dialogSize);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(18);

    QHBoxLayout* titleLayout = new QHBoxLayout;
    QLabel* titleLabel = new QLabel(chrome.titleText, &dialog);
    titleLabel->setObjectName("noticeTitle");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    QLabel* countLabel = new QLabel(chrome.countTextTemplate.arg(m_localGroupIds.size() + 1), &dialog);
    countLabel->setObjectName("noticeSubTitle");
    titleLayout->addWidget(countLabel);
    layout->addLayout(titleLayout);

    QLineEdit* searchEdit = new QLineEdit(&dialog);
    searchEdit->setObjectName("noticeSearch");
    searchEdit->setPlaceholderText(chrome.searchPlaceholder);
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setToolTip(chrome.searchToolTip);
    layout->addWidget(searchEdit);

    QListWidget* noticeList = new QListWidget(&dialog);
    noticeList->setObjectName("noticeList");
    noticeList->setWordWrap(true);

    auto fillGroups = [this, noticeList, countLabel, searchEdit]() {
        fillGroupNoticeList(noticeList, countLabel, searchEdit);
    };
    fillGroups();

    QLabel* groupPreviewLabel = new QLabel(chrome.previewPlaceholder, &dialog);
    groupPreviewLabel->setObjectName("noticePreviewLabel");
    layout->addWidget(groupPreviewLabel);
    layout->addWidget(noticeList, 1);

    QVBoxLayout* actionLayout = new QVBoxLayout;
    actionLayout->setSpacing(8);
    QHBoxLayout* hintLayout = new QHBoxLayout;
    QHBoxLayout* groupMainActionLayout = new QHBoxLayout;
    groupMainActionLayout->setSpacing(8);
    QHBoxLayout* groupMemberActionLayout = new QHBoxLayout;
    groupMemberActionLayout->setSpacing(8);
    QHBoxLayout* groupMediaActionLayout = new QHBoxLayout;
    groupMediaActionLayout->setSpacing(8);
    QLabel* hintLabel = new QLabel(chrome.hintPlaceholder, &dialog);
    hintLabel->setObjectName("noticeHint");
    hintLayout->addWidget(hintLabel);
    hintLayout->addStretch();
    QPushButton* openBtn = new QPushButton(chrome.openButton.text, &dialog);
    openBtn->setObjectName(chrome.openButton.objectName);
    openBtn->setToolTip(chrome.openButton.toolTip);
    QPushButton* copyBtn = new QPushButton(chrome.copyIdButton.text, &dialog);
    copyBtn->setObjectName(chrome.copyIdButton.objectName);
    copyBtn->setToolTip(chrome.copyIdButton.toolTip);
    QPushButton* cardBtn = new QPushButton(chrome.copyCardButton.text, &dialog);
    cardBtn->setObjectName(chrome.copyCardButton.objectName);
    cardBtn->setToolTip(chrome.copyCardButton.toolTip);
    QPushButton* announceBtn = new QPushButton(chrome.copyAnnouncementButton.text, &dialog);
    announceBtn->setObjectName(chrome.copyAnnouncementButton.objectName);
    announceBtn->setToolTip(chrome.copyAnnouncementButton.toolTip);
    QPushButton* inviteTextBtn = new QPushButton(chrome.copyInviteButton.text, &dialog);
    inviteTextBtn->setObjectName(chrome.copyInviteButton.objectName);
    inviteTextBtn->setToolTip(chrome.copyInviteButton.toolTip);
    QPushButton* memberBtn = new QPushButton(chrome.copyMembersButton.text, &dialog);
    memberBtn->setObjectName(chrome.copyMembersButton.objectName);
    memberBtn->setToolTip(chrome.copyMembersButton.toolTip);
    QPushButton* onlineMemberBtn = new QPushButton(chrome.copyOnlineMembersButton.text, &dialog);
    onlineMemberBtn->setObjectName(chrome.copyOnlineMembersButton.objectName);
    onlineMemberBtn->setToolTip(chrome.copyOnlineMembersButton.toolTip);
    QPushButton* copyGroupMediaPackBtn = new QPushButton(chrome.copyMediaPackButton.text, &dialog);
    copyGroupMediaPackBtn->setObjectName(chrome.copyMediaPackButton.objectName);
    copyGroupMediaPackBtn->setToolTip(chrome.copyMediaPackButton.toolTip);
    QPushButton* copyGroupBatchPlanBtn = new QPushButton(chrome.copyBatchPlanButton.text, &dialog);
    copyGroupBatchPlanBtn->setObjectName(chrome.copyBatchPlanButton.objectName);
    copyGroupBatchPlanBtn->setToolTip(chrome.copyBatchPlanButton.toolTip);
    QPushButton* copyMediaGuideBtn = new QPushButton(chrome.copyMediaGuideButton.text, &dialog);
    copyMediaGuideBtn->setObjectName(chrome.copyMediaGuideButton.objectName);
    copyMediaGuideBtn->setToolTip(chrome.copyMediaGuideButton.toolTip);
    QPushButton* closeBtn = new QPushButton(chrome.closeButton.text, &dialog);
    closeBtn->setObjectName(chrome.closeButton.objectName);
    closeBtn->setToolTip(chrome.closeButton.toolTip);
    groupMainActionLayout->addWidget(openBtn);
    groupMainActionLayout->addWidget(copyBtn);
    groupMainActionLayout->addWidget(cardBtn);
    groupMainActionLayout->addWidget(announceBtn);
    groupMainActionLayout->addStretch();
    groupMainActionLayout->addWidget(closeBtn);
    groupMemberActionLayout->addWidget(inviteTextBtn);
    groupMemberActionLayout->addWidget(memberBtn);
    groupMemberActionLayout->addWidget(onlineMemberBtn);
    groupMemberActionLayout->addStretch();
    groupMediaActionLayout->addWidget(copyGroupMediaPackBtn);
    groupMediaActionLayout->addWidget(copyGroupBatchPlanBtn);
    groupMediaActionLayout->addWidget(copyMediaGuideBtn);
    groupMediaActionLayout->addStretch();
    actionLayout->addLayout(hintLayout);
    actionLayout->addLayout(groupMainActionLayout);
    actionLayout->addLayout(groupMemberActionLayout);
    actionLayout->addLayout(groupMediaActionLayout);
    layout->addLayout(actionLayout);

    auto openSelectedGroup = [this, noticeList, &dialog]() {
        openSelectedGroupNoticeEntry(noticeList, &dialog);
    };
    dialog.setStyleSheet(NotificationPanelManager::groupNoticeDialogStyleSheet());
    connect(openBtn, &QPushButton::clicked, &dialog, openSelectedGroup);
    auto updateGroupPreview = [this, noticeList, searchEdit, groupPreviewLabel]() {
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        groupPreviewLabel->setText(snapshot.previewText);
    };
    auto updateGroupActionState = [=]() {
        const GroupNoticeActionState state = NotificationPanelManager::groupNoticeActionState(
            selectedGroupNoticeEntryId(noticeList),
            noticeList->currentItem() != nullptr,
            !searchEdit->text().trimmed().isEmpty(),
            noticeList->count());
        openBtn->setEnabled(state.openEnabled);
        openBtn->setText(state.openText);
        openBtn->setToolTip(state.openToolTip);
        copyBtn->setEnabled(state.copyIdEnabled);
        copyBtn->setToolTip(state.copyIdToolTip);
        cardBtn->setEnabled(state.copyCardEnabled);
        cardBtn->setToolTip(state.copyCardToolTip);
        announceBtn->setEnabled(state.copyAnnouncementEnabled);
        announceBtn->setToolTip(state.copyAnnouncementToolTip);
        memberBtn->setEnabled(state.copyMembersEnabled);
        memberBtn->setToolTip(state.copyMembersToolTip);
        onlineMemberBtn->setEnabled(state.copyOnlineMembersEnabled);
        onlineMemberBtn->setToolTip(state.copyOnlineMembersToolTip);
        inviteTextBtn->setEnabled(state.copyInviteEnabled);
        inviteTextBtn->setToolTip(state.copyInviteToolTip);
        copyGroupMediaPackBtn->setEnabled(state.copyMediaPackEnabled);
        copyGroupMediaPackBtn->setToolTip(state.copyMediaPackToolTip);
        copyGroupBatchPlanBtn->setEnabled(state.copyBatchPlanEnabled);
        copyGroupBatchPlanBtn->setToolTip(state.copyBatchPlanToolTip);
        copyMediaGuideBtn->setEnabled(state.copyMediaGuideEnabled);
        copyMediaGuideBtn->setToolTip(state.copyMediaGuideToolTip);
        hintLabel->setText(state.hintText);
    };
    updateGroupPreview();
    updateGroupActionState();
    connect(noticeList, &QListWidget::currentItemChanged, &dialog, [updateGroupPreview, updateGroupActionState](QListWidgetItem*, QListWidgetItem*) {
        updateGroupPreview();
        updateGroupActionState();
    });
    connect(searchEdit, &QLineEdit::textChanged, &dialog, [fillGroups, updateGroupPreview, updateGroupActionState]() {
        fillGroups();
        updateGroupPreview();
        updateGroupActionState();
    });
    connect(searchEdit, &QLineEdit::returnPressed, &dialog, openSelectedGroup);
    connect(copyBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制群号的群聊",
                                                 "待创建群聊还没有群号，请先进入创建",
                                                 &groupId)) {
            return;
        }
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        QApplication::clipboard()->setText(snapshot.copyId);
        ui->statusbar->showMessage("群号已复制: " + snapshot.copyId, 2500);
    });
    connect(cardBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制名片的群聊",
                                                 "待创建群聊还没有名片，请先进入创建",
                                                 &groupId)) {
            return;
        }
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        QApplication::clipboard()->setText(snapshot.cardText);
        ui->statusbar->showMessage("群名片已复制", 1800);
    });
    connect(announceBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制公告的群聊",
                                                 "待创建群聊还没有公告，请先进入创建",
                                                 &groupId)) {
            return;
        }
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        QApplication::clipboard()->setText(snapshot.announcementText);
        ui->statusbar->showMessage("群公告已复制", 1800);
    });
    connect(inviteTextBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        QApplication::clipboard()->setText(NotificationPanelManager::groupNoticeInviteText(
            snapshot.copyContext.groupName,
            snapshot.copyContext.groupNumber,
            m_currentUserName,
            m_currentUserId));
        ui->statusbar->showMessage("入群邀请话术已复制", 2200);
    });
    connect(memberBtn, &QPushButton::clicked, &dialog, [this, noticeList]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制成员的群聊",
                                                 "待创建群聊还没有成员列表，请先进入创建",
                                                 &groupId)) {
            return;
        }
        const QStringList members = groupNoticeMemberIds(groupId);
        const GroupNoticeMemberCopyState state =
            NotificationPanelManager::groupMemberCopyState(groupNoticeMemberCopyInputs(members), false);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return;
        }
        QApplication::clipboard()->setText(state.rows.join('\n'));
        ui->statusbar->showMessage(state.copiedStatusMessage, 2200);
    });
    connect(onlineMemberBtn, &QPushButton::clicked, &dialog, [this, noticeList]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制在线成员的群聊",
                                                 "待创建群聊还没有在线成员，请先进入创建",
                                                 &groupId)) {
            return;
        }
        const QStringList members = groupNoticeMemberIds(groupId);
        const GroupNoticeMemberCopyState state =
            NotificationPanelManager::groupMemberCopyState(groupNoticeMemberCopyInputs(members), true);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return;
        }
        QApplication::clipboard()->setText(state.rows.join('\n'));
        ui->statusbar->showMessage(state.copiedStatusMessage, 2200);
    });
    connect(copyGroupMediaPackBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        QApplication::clipboard()->setText(NotificationPanelManager::groupNoticeMediaPackText(
            snapshot.copyContext.groupName,
            snapshot.copyContext.groupNumber,
            snapshot.copyContext.memberCount,
            m_currentUserName,
            m_currentUserId));
        ui->statusbar->showMessage("群媒体包已复制", 2200);
    });
    connect(copyGroupBatchPlanBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        const GroupNoticeBatchPlanState state = NotificationPanelManager::groupNoticeBatchPlanState(
            searchEdit->text().trimmed(),
            visibleGroupNoticeBatchTargets(noticeList),
            m_currentUserName,
            m_currentUserId);
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage(state.statusMessage, 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        QApplication::clipboard()->setText(NotificationPanelManager::groupNoticeMediaGuideText(
            snapshot.copyContext.groupName,
            snapshot.copyContext.groupNumber,
            snapshot.copyContext.memberCount,
            m_currentUserName,
            m_currentUserId));
        ui->statusbar->showMessage("群上传指南已复制", 2200);
    });
    connect(noticeList, &QListWidget::itemDoubleClicked, &dialog, [openSelectedGroup](QListWidgetItem*) { openSelectedGroup(); });
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
    decorateChatItem(item, QString(), QStringLiteral("系统"), false, true);
    m_chatModel->appendRow(item);
    ui->chatListView->scrollToBottom();
}

void MainWindow::setTransferWorkspaceState(const TransferWorkspaceCardState& state) {
    // The old transfer-overview card widgets were removed in the QQNT redesign.
    // Surface the same state in the chat hint and status bar instead.
    if (!state.summaryText.isEmpty()) {
        ui->chatHintLabel->setText(state.summaryText);
    }
    if (!state.detailText.isEmpty()) {
        ui->statusbar->showMessage(QString("%1 · %2").arg(state.stageText, state.detailText), 2400);
    }
    Q_UNUSED(state.actionText)
}

bool MainWindow::ensureTransferTargetReady(const QString& kind, const QString& targetName, bool isLocalGroup) {
    if (m_privateChatTarget.isEmpty() && isCurrentUserRemovedFromPublicGroup()) {
        const TransferSendUiState state = m_transferManager.publicGroupRemovedState(kind);
        applyTransferSendState(state);
        refreshComposerState();
        return false;
    }
    if (!isLocalGroup && (!m_client || !m_client->isConnected())) {
        const TransferSendUiState state = m_transferManager.disconnectedSendState(kind, targetName);
        applyTransferSendState(state);
        refreshComposerState();
        return false;
    }
    return true;
}

bool MainWindow::selectTransferFile(const TransferSelectionPlan& selectionPlan,
                                    QString* filePath,
                                    QFileInfo* fileInfo,
                                    QString* fileSize) {
    if (!filePath || !fileInfo) {
        return false;
    }

    const QString selectedPath = QFileDialog::getOpenFileName(this,
                                                              selectionPlan.dialogTitle,
                                                              LocalFileManager::lastTransferDirectory(),
                                                              selectionPlan.filters);
    TransferSelectionUiState selectionState =
        m_transferManager.transferSelectionUiState(selectionPlan, selectedPath);
    if (!handleTransferSelectionUiState(&selectionState)) {
        return false;
    }

    *filePath = selectionState.filePath;
    *fileInfo = selectionState.fileInfo;
    if (fileSize) {
        *fileSize = selectionState.fileSize;
    }
    return true;
}

bool MainWindow::selectTransferFileContext(const TransferSelectionPlan& selectionPlan,
                                           SelectedTransferFile* selectedFile) {
    if (!selectedFile) {
        return false;
    }

    return selectTransferFile(selectionPlan,
                              &selectedFile->filePath,
                              &selectedFile->info,
                              &selectedFile->fileSize);
}

bool MainWindow::handleTransferSelectionUiState(TransferSelectionUiState* selectionState) {
    if (!selectionState) {
        return false;
    }

    while (true) {
        const TransferSelectionFeedbackPlan feedbackPlan =
            m_transferManager.transferSelectionFeedbackPlan(*selectionState);

        if (feedbackPlan.dialogKind == TransferSelectionFeedbackPlan::DialogKind::Warning) {
            QMessageBox::warning(this, feedbackPlan.dialogTitle, feedbackPlan.dialogMessage);
        }

        if (!feedbackPlan.hintText.isEmpty()) {
            ui->chatHintLabel->setText(feedbackPlan.hintText);
        }
        if (!feedbackPlan.statusMessage.isEmpty()) {
            ui->statusbar->showMessage(feedbackPlan.statusMessage, feedbackPlan.statusTimeoutMs);
        }

        if (feedbackPlan.requiresConfirmation) {
            const bool confirmed = QMessageBox::question(this,
                                                         feedbackPlan.dialogTitle,
                                                         feedbackPlan.dialogMessage,
                                                         QMessageBox::Yes | QMessageBox::No,
                                                         QMessageBox::No) == QMessageBox::Yes;
            *selectionState = m_transferManager.resolveTransferSelectionUiState(*selectionState, confirmed);
            continue;
        }

        return !feedbackPlan.stopSelection && selectionState->accepted;
    }
}

void MainWindow::applyTransferSendState(const TransferSendUiState& state) {
    ui->chatHintLabel->setText(state.hintText);
    ui->statusbar->showMessage(state.statusMessage, state.statusTimeoutMs);
}

void MainWindow::appendTransferCompletionState(const TransferSendUiState& state,
                                               bool includeSystemMessage,
                                               bool includeCard,
                                               const QColor& cardForeground,
                                               const QColor& cardBackground) {
    if (includeSystemMessage && !state.systemMessage.isEmpty()) {
        appendSystemMessage(state.systemMessage);
    }

    if (includeCard && !state.cardText.isEmpty()) {
        QStandardItem* cardItem = new QStandardItem(state.cardText);
        cardItem->setEditable(false);
        cardItem->setForeground(cardForeground);
        cardItem->setBackground(cardBackground);
        cardItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_chatModel->appendRow(cardItem);
    }

    if (!state.receiptText.isEmpty()) {
        QStandardItem* receiptItem = new QStandardItem(state.receiptText);
        receiptItem->setEditable(false);
        receiptItem->setForeground(QColor(86, 116, 130));
        receiptItem->setBackground(QColor(246, 251, 253));
        receiptItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_chatModel->appendRow(receiptItem);
    }

    ui->chatHintLabel->setText(state.hintText);
    ui->statusbar->showMessage(state.statusMessage, state.statusTimeoutMs);
    ui->chatListView->scrollToBottom();
}

void MainWindow::appendLocalGroupFileTransferCompletion(const TransferSelectionPlan& selectionPlan,
                                                        const QFileInfo& info,
                                                        const QString& fileSize,
                                                        const QString& targetName,
                                                        const QString& completedAt) {
    const TransferSendUiState completedState = m_transferManager.localSendCompletedState(
        selectionPlan.preparingKind,
        info.fileName(),
        fileSize,
        targetName,
        completedAt);
    const QString line = QString("[%1] <%2> 发送了文件: %3 · %4")
        .arg(completedAt, m_currentUserName, info.fileName(), fileSize);
    saveHistory(m_privateChatTarget, line);
    QStandardItem* item = new QStandardItem(line);
    item->setEditable(false);
    m_chatModel->appendRow(item);
    setTransferWorkspaceState(m_transferManager.localSendCompletedWorkspaceState(selectionPlan.preparingKind,
                                                                                 info.fileName(),
                                                                                 fileSize,
                                                                                 targetName,
                                                                                 completedAt));
    appendTransferCompletionState(completedState, true, true, QColor(0, 121, 107), QColor(232, 248, 245));
}

void MainWindow::appendLocalGroupMediaTransferCompletion(const QString& filePath,
                                                         const QFileInfo& info,
                                                         const QString& fileSize,
                                                         const QString& mediaType,
                                                         bool isVideo,
                                                         const QString& targetName,
                                                         const QString& completedAt) {
    const TransferSendUiState completedState = m_transferManager.localSendCompletedState(
        mediaType,
        info.fileName(),
        fileSize,
        targetName,
        completedAt);
    const QString line = QString("[%1] <%2> [%3] %4 · %5")
        .arg(completedAt, m_currentUserName, mediaType, info.fileName(), fileSize);
    saveHistory(m_privateChatTarget, line);

    QPixmap pixmap;
    if (!isVideo) {
        pixmap.load(filePath);
    }
    if ((!isVideo && !pixmap.isNull()) || isVideo) {
        const TransferMediaPreviewPlan previewPlan = m_transferManager.localMediaPreviewPlan(
            info.fileName(),
            fileSize,
            isVideo);
        Q_UNUSED(previewPlan)
        appendMediaPreviewItem(completedState.cardText,
                               pixmap,
                               isVideo,
                               true,
                               filePath,
                               m_currentUserId,
                               m_currentUserName);
    }
    setTransferWorkspaceState(m_transferManager.localSendCompletedWorkspaceState(mediaType,
                                                                                 info.fileName(),
                                                                                 fileSize,
                                                                                 targetName,
                                                                                 completedAt));
    ui->chatHintLabel->setText(completedState.hintText);
    ui->statusbar->showMessage(completedState.statusMessage, completedState.statusTimeoutMs);
    ui->chatListView->scrollToBottom();
}

void MainWindow::appendRemoteMediaTransferCompletion(const QString& filePath,
                                                     const TransferSendUiState& completedState,
                                                     bool isVideo,
                                                     const TransferWorkspaceCardState& workspaceState) {
    setTransferWorkspaceState(workspaceState);
    const TransferMediaPreviewPlan previewPlan = m_transferManager.remoteMediaPreviewPlan(completedState.cardText, isVideo);
    if (!isVideo) {
        QPixmap pixmap(filePath);
        if (!pixmap.isNull()) {
            appendMediaPreviewItem(previewPlan.text,
                                   pixmap,
                                   previewPlan.isVideo,
                                   previewPlan.alignRight,
                                   filePath,
                                   m_currentUserId,
                                   m_currentUserName);
        }
    } else {
        appendMediaPreviewItem(previewPlan.text,
                               QPixmap(),
                               previewPlan.isVideo,
                               previewPlan.alignRight,
                               filePath,
                               m_currentUserId,
                               m_currentUserName);
    }
    ui->chatHintLabel->setText(completedState.hintText);
    ui->statusbar->showMessage(completedState.statusMessage, completedState.statusTimeoutMs);
    ui->chatListView->scrollToBottom();
}

void MainWindow::handleRemoteTransferResult(bool ok,
                                            bool transferCanceled,
                                            const QString& filePath,
                                            const QFileInfo& info,
                                            const QString& fileSize,
                                            const QString& targetName,
                                            const QString& kind,
                                            bool media,
                                            bool isVideo,
                                            const QString& transferSummary) {
    if (ok) {
        const TransferSendUiState completedState = m_transferManager.remoteSendCompletedState(
            kind,
            info.fileName(),
            fileSize,
            targetName,
            QDateTime::currentDateTime().toString("hh:mm:ss"),
            transferSummary);
        const TransferWorkspaceCardState workspaceState = m_transferManager.remoteSendCompletedWorkspaceState(
            kind,
            info.fileName(),
            fileSize,
            targetName,
            QDateTime::currentDateTime().toString("hh:mm:ss"),
            transferSummary);
        if (media) {
            appendRemoteMediaTransferCompletion(filePath, completedState, isVideo, workspaceState);
        } else {
            setTransferWorkspaceState(workspaceState);
            appendTransferCompletionState(completedState, true, true, QColor(0, 121, 107), QColor(232, 248, 245));
        }
        return;
    }

    if (transferCanceled) {
        appendSystemMessage(QString("已取消发送%1: %2 · 到 %3").arg(kind, info.fileName(), targetName));
        const TransferSendUiState state = m_transferManager.canceledSendState(kind, info.fileName());
        setTransferWorkspaceState(m_transferManager.canceledSendWorkspaceState(kind, info.fileName(), targetName));
        ui->chatHintLabel->setText(QString("%1 · %2").arg(state.hintText, targetName));
        ui->statusbar->showMessage(state.statusMessage, state.statusTimeoutMs);
        refreshComposerState();
        return;
    }

    const TransferSendUiState state = m_transferManager.failedSendState(kind, info.fileName(), fileSize, targetName);
    setTransferWorkspaceState(m_transferManager.failedSendWorkspaceState(kind, info.fileName(), fileSize, targetName));
    applyTransferSendState(state);
    QMessageBox::warning(this, state.warningTitle, state.warningMessage);
    refreshComposerState();
}

void MainWindow::appendMediaPreviewItem(const QString& text,
                                        const QPixmap& pixmap,
                                        bool isVideo,
                                        bool alignRight,
                                        const QString& openPath,
                                        const QString& senderId,
                                        const QString& senderName) {
    QStandardItem* previewItem = new QStandardItem(text);
    if (!isVideo && !pixmap.isNull()) {
        previewItem->setData(pixmap.scaled(360, 260, Qt::KeepAspectRatio, Qt::SmoothTransformation), ChatBubbleMediaPreviewRole);
        previewItem->setData(QStringLiteral("image"), ChatBubbleMediaKindRole);
    } else if (isVideo) {
        previewItem->setData(QStringLiteral("video"), ChatBubbleMediaKindRole);
    }
    if (!openPath.trimmed().isEmpty()) {
        previewItem->setData(openPath.trimmed(), ChatBubbleMediaOpenPathRole);
        previewItem->setData(QStringLiteral("双击打开文件；右键可复制保存路径或打开目录\n%1").arg(openPath.trimmed()), Qt::ToolTipRole);
    }
    previewItem->setEditable(false);
    decorateChatItem(previewItem,
                     senderId.isEmpty() ? m_currentUserId : senderId,
                     senderName.isEmpty() ? m_currentUserName : senderName,
                     alignRight);
    m_chatModel->appendRow(previewItem);
}

void MainWindow::applyReceivedTransferRenderPlan(const TransferReceiveRenderPlan& plan,
                                                 const QString& fileName,
                                                 const QString& transferId,
                                                 qint64 receivedBytes,
                                                 qint64 totalBytes) {
    for (const TransferChatListItemUiState& itemState : plan.chatItems) {
        appendTransferChatListItem(itemState);
    }
    showFileTransferStatusEvent(fileName,
                                transferId,
                                plan.eventReason,
                                receivedBytes,
                                totalBytes);
    ui->chatHintLabel->setText(plan.hintText);
    ui->statusbar->showMessage(plan.statusMessage, plan.statusTimeoutMs);
}

bool MainWindow::persistReceivedTransferPayload(const ReceivedTransferContext& context,
                                                const QString& displayName,
                                                const QString& transferId,
                                                const QByteArray& fileData,
                                                qint64 totalBytes) {
    const bool saved = LocalFileManager::writeReceivedTransferPayload(context.savePath, fileData);
    setTransferWorkspaceState(m_transferManager.receivedTransferWorkspaceState(context.kind,
                                                                               context.receivedName,
                                                                               context.receivedSize,
                                                                               displayName,
                                                                               context.manifestSuffix,
                                                                               context.integrityText,
                                                                               context.integritySuffix,
                                                                               context.savePath,
                                                                               saved));
    const TransferReceiveRenderPlan plan = receivedTransferPersistencePlan(context, displayName, saved);
    if (context.kind == QStringLiteral("图片") && saved) {
        ui->chatHintLabel->setText(plan.hintText);
        ui->statusbar->showMessage(plan.statusMessage, plan.statusTimeoutMs);
    } else {
        applyReceivedTransferRenderPlan(plan,
                                        context.receivedName,
                                        transferId,
                                        fileData.size(),
                                        totalBytes);
    }
    return saved;
}

bool MainWindow::handleReceivedTransferMessage(const Message& msg,
                                               const QString& displayName) {
    if (msg.fileData.isEmpty()) {
        return false;
    }

    const bool image = msg.type == MessageType::Image;
    const ReceivedTransferContext context = receivedTransferContext(
        msg,
        image ? QStringLiteral("图片") : QStringLiteral("文件"),
        image ? QStringLiteral("received_image") : QStringLiteral("received_file"),
        image ? QStringLiteral("Images") : QStringLiteral("Files"),
        displayName);
    if (image) {
        QPixmap pixmap;
        if (pixmap.loadFromData(msg.fileData)) {
            const TransferMediaPreviewPlan previewPlan = m_transferManager.receivedMediaPreviewPlan(
                context.receivedName,
                context.receivedSize,
                context.manifestSuffix);
            appendMediaPreviewItem(previewPlan.text,
                                   pixmap,
                                   previewPlan.isVideo,
                                   previewPlan.alignRight,
                                   context.savePath,
                                   msg.senderId,
                                   displayName);
        }
    }
    return persistReceivedTransferPayload(context,
                                          displayName,
                                          msg.transferId,
                                          msg.fileData,
                                          msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
}

TransferReceiveRenderPlan MainWindow::receivedTransferPersistencePlan(const ReceivedTransferContext& context,
                                                                      const QString& displayName,
                                                                      bool saved) const {
    return m_transferManager.receivedTransferPersistenceRenderPlan(context.kind,
                                                                  context.receivedName,
                                                                  context.receivedSize,
                                                                  displayName,
                                                                  context.manifestSuffix,
                                                                  context.integrityText,
                                                                  context.integritySuffix,
                                                                  context.savePath,
                                                                  saved);
}

void MainWindow::appendTransferChatListItem(const TransferChatListItemUiState& itemState) {
    if (itemState.text.isEmpty()) {
        return;
    }

    QStandardItem* item = TransferChatItemRenderer::createItem(itemState);
    m_chatModel->appendRow(item);
}

MainWindow::ReceivedTransferContext MainWindow::receivedTransferContext(const Message& msg,
                                                                        const QString& kind,
                                                                        const QString& fallbackName,
                                                                        const QString& downloadSubdir,
                                                                        const QString& displayName) const {
    ReceivedTransferContext context;
    context.kind = kind;
    const LocalReceivedTransferPlan localPlan = LocalFileManager::receivedTransferPlan(msg.fileName,
                                                                                       fallbackName,
                                                                                       msg.fileData.size(),
                                                                                       downloadSubdir);
    context.receivedName = localPlan.receivedName;
    context.receivedSize = localPlan.receivedSize;
    context.savePath = localPlan.savePath;
    context.integrityText = transferIntegritySummary(msg);
    context.integritySuffix = context.integrityText.isEmpty()
        ? QString()
        : QString(" · %1").arg(context.integrityText);
    const QString manifestText = m_transferManager.sendingPreparedState(kind,
                                                                        context.receivedName,
                                                                        displayName,
                                                                        msg.fileSize > 0 ? msg.fileSize : msg.fileData.size(),
                                                                        msg.chunkSize,
                                                                        msg.chunkCount,
                                                                        msg.fileHash).manifestSummary;
    context.manifestSuffix = manifestText.isEmpty() ? QString() : QString(" · %1").arg(manifestText);
    return context;
}

void MainWindow::loadHistory(const QString& peerId) {
    if (peerId.isEmpty()) return;

    const QStringList rows = m_historyService.recentRows(peerId, MAX_HISTORY_LINES);
    for (const QString& line : rows) {
        QStandardItem* item = new QStandardItem(line);
        item->setEditable(false);

        QString senderName;
        QString senderId;
        bool outgoing = false;
        bool system = false;

        const int nameOpen = line.indexOf('<');
        const int nameClose = line.indexOf('>');
        if (nameOpen != -1 && nameClose > nameOpen) {
            senderName = line.mid(nameOpen + 1, nameClose - nameOpen - 1).trimmed();
        }

        if (line.contains(QStringLiteral("[系统]"))) {
            system = true;
            senderName = QStringLiteral("系统");
        } else if (senderName == m_currentUserName) {
            outgoing = true;
            senderId = m_currentUserId;
        } else {
            senderId = peerId;
        }

        decorateChatItem(item, senderId, senderName, outgoing, system);
        const QString sessionId = peerId == QLatin1String("group") ? QStringLiteral("public") : peerId;
        const QString historyMessageId = compactMessageId(QStringLiteral("%1|%2").arg(sessionId, line));
        item->setData(historyMessageId, ChatMessageIdRole);
        item->setData(sessionId, ChatSessionIdRole);
        item->setData(m_favoriteMessageKeys.contains(sessionId + QLatin1Char('|') + historyMessageId), ChatFavoritedRole);
        item->setData(m_serverGroupEssenceMessageKeys.contains(sessionId + QLatin1Char('|') + historyMessageId), ChatEssenceRole);
        item->setData(m_serverGroupRecalledMessageKeys.contains(sessionId + QLatin1Char('|') + historyMessageId), ChatRecalledRole);
        m_chatModel->appendRow(item);
    }
}

void MainWindow::saveHistory(const QString& peerId, const QString& content) {
    saveHistory(peerId, content, QStringLiteral("plaintext"));
}

void MainWindow::saveHistory(const QString& peerId,
                             const QString& content,
                             const QString& encryptionState,
                             const QString& e2eKeyId,
                             const QString& e2eKeyFingerprint) {
    m_historyService.save(peerId, content, encryptionState, e2eKeyId, e2eKeyFingerprint);
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

    // Legacy roles (still used by some saved-file helpers)
    item->setData(senderId, ChatSenderIdRole);
    item->setData(senderName, ChatSenderNameRole);
    item->setData(chatAvatarPath(senderId), ChatAvatarPathRole);
    item->setData(outgoing, ChatOutgoingRole);
    item->setData(system, ChatSystemRole);

    // ChatBubbleDelegate roles
    item->setData(senderId, ChatBubbleSenderIdRole);
    item->setData(senderName, ChatBubbleSenderNameRole);
    item->setData(chatAvatarPath(senderId), ChatBubbleAvatarPathRole);
    item->setData(outgoing, ChatBubbleOutgoingRole);
    item->setData(system, ChatBubbleSystemRole);
    item->setData(QDateTime::currentDateTime().toString("hh:mm"), ChatBubbleTimestampRole);
    if (!system) {
        item->setIcon(peerAvatarIcon(senderId, senderName));
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

void MainWindow::refreshFriendList() {
    m_userListModel->clear();
    m_userListModel->setHorizontalHeaderLabels({"好友 / 在线"});

    bool loadedFriendsFromSqlite = false;
    if (m_friendIds.isEmpty() && ensureClientDatabase()) {
        const QString connectionName = "client_friends_read_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(clientDbPath());
            if (db.open()) {
                QSqlQuery query(db);
                if (query.exec("SELECT user_id, display_name FROM friends ORDER BY user_id ASC")) {
                    while (query.next()) {
                        const QString id = query.value(0).toString();
                        const QString name = query.value(1).toString();
                        if (!id.isEmpty() && !m_friendIds.contains(id)) {
                            m_friendIds << id;
                            loadedFriendsFromSqlite = true;
                        }
                        if (!id.isEmpty() && !name.isEmpty()) {
                            m_friendNames[id] = name;
                        }
                    }
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    if (!loadedFriendsFromSqlite
        && m_friendIds.isEmpty()
        && m_clientStorage.readLegacyFriends(&m_friendIds, &m_friendNames)) {
        saveFriends();
    }

    if (m_pendingFriendRequests.isEmpty() && m_pendingOutgoingFriendRequests.isEmpty() && ensureClientDatabase()) {
        const QString connectionName = "client_requests_read_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(clientDbPath());
            if (db.open()) {
                QSqlQuery query(db);
                if (query.exec("SELECT request_id, display_name, direction FROM friend_requests WHERE status = 'pending' ORDER BY updated_at ASC")) {
                    while (query.next()) {
                        const QString id = query.value(0).toString();
                        const QString name = query.value(1).toString();
                        const QString direction = query.value(2).toString();
                        if (id.isEmpty()) continue;
                        if (!name.isEmpty()) m_friendNames[id] = name;
                        if (direction == "incoming" && !m_pendingFriendRequests.contains(id) && !m_friendIds.contains(id)) {
                            m_pendingFriendRequests << id;
                        } else if (direction == "outgoing" && !m_pendingOutgoingFriendRequests.contains(id) && !m_friendIds.contains(id)) {
                            m_pendingOutgoingFriendRequests << id;
                        }
                    }
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    bool loadedGroupsFromSqlite = false;
    if (m_localGroupIds.isEmpty() && ensureClientDatabase()) {
        const QString connectionName = "client_groups_read_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(clientDbPath());
            if (db.open()) {
                QSqlQuery query(db);
                if (query.exec("SELECT group_id, group_name, members, announcement FROM local_groups ORDER BY group_id ASC")) {
                    while (query.next()) {
                        const QString id = query.value(0).toString();
                        const QString name = query.value(1).toString();
                        const QStringList members = query.value(2).toString().split(',', Qt::SkipEmptyParts);
                        const QString announcement = query.value(3).toString();
                        if (!id.isEmpty() && !m_localGroupIds.contains(id)) {
                            m_localGroupIds << id;
                            loadedGroupsFromSqlite = true;
                        }
                        if (!id.isEmpty() && !name.isEmpty()) {
                            m_localGroupNames[id] = name;
                        }
                        if (!id.isEmpty()) {
                            m_localGroupMembers[id] = members.isEmpty() ? QStringList{m_currentUserId} : members;
                            m_localGroupAnnouncements[id] = announcement.isEmpty()
                                ? QString("%1 已创建，可继续邀请好友并发送消息。").arg(m_localGroupNames.value(id, "群聊"))
                                : announcement;
                        }
                    }
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    if (!loadedGroupsFromSqlite
        && m_localGroupIds.isEmpty()
        && m_clientStorage.readLegacyLocalGroups(m_currentUserId,
                                                 &m_localGroupIds,
                                                 &m_localGroupNames,
                                                 &m_localGroupMembers,
                                                 &m_localGroupAnnouncements)) {
        saveLocalGroups();
    }

    refreshWorkspaceChrome();
    updateFriendNoticeBadge();

    int visibleCount = 0;
    int visibleFriends = 0;
    int visibleGroups = 0;
    int visibleOnlineUsers = 0;
    int onlineFriendCount = 0;
    int visiblePendingOutgoing = 0;
    int visibleStrangers = 0;
    auto matchesFilter = [this](const QString& id, const QString& name) {
        return m_friendManager.matchesFilter(id, name, m_contactFilter);
    };

    auto makeSessionItem = [this, &matchesFilter](const QString& itemId,
                                                  const QString& displayName,
                                                  const QString& subtitle,
                                                  bool isGroup,
                                                  int* visibleCounter,
                                                  int* categoryCounter) {
        if (!matchesFilter(itemId, displayName)) return;
        QStandardItem* item = new QStandardItem(displayName);
        item->setEditable(false);
        item->setData(itemId, Qt::UserRole + 1);
        item->setData(displayName, SessionNameRole);
        item->setData(subtitle, SessionLastMessageRole);
        item->setData(QDateTime::currentDateTime().toString("hh:mm"), SessionTimeRole);
        item->setData(0, SessionUnreadRole);
        item->setData(false, SessionPinnedRole);
        item->setData(QString(), SessionSenderNameRole);
        item->setData(itemId, SessionIdRole);
        if (isGroup) {
            item->setData(peerAvatarIcon(QString(), displayName), Qt::DecorationRole);
        } else {
            item->setData(chatAvatarPath(itemId), SessionAvatarRole);
        }
        m_userListModel->appendRow(item);
        if (visibleCounter) ++*visibleCounter;
        if (categoryCounter) ++*categoryCounter;
    };

    // 我的好友
    for (const QString& friendId : m_friendIds) {
        if (m_knownUsers.contains(friendId)) continue;
        QString name = m_friendNames.value(friendId, friendId);
        makeSessionItem(friendId, name, "离线", false, &visibleCount, &visibleFriends);
    }

    // 群聊
    for (const QString& groupId : m_localGroupIds) {
        QString groupName = m_localGroupNames.value(groupId, "群聊");
        makeSessionItem(groupId, groupName, "本地群聊", true, &visibleCount, &visibleGroups);
    }

    // 在线成员
    for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
        const ChatUser& user = it.value();
        if (user.name == m_currentUserName) continue;
        if (!matchesFilter(user.id, user.name)) continue;
        bool isFriend = m_friendIds.contains(user.id);
        if (isFriend) {
            m_friendNames[user.id] = user.name;
            ++onlineFriendCount;
        }
        const bool isPending = m_pendingOutgoingFriendRequests.contains(user.id);
        if (isPending && !isFriend) ++visiblePendingOutgoing;
        makeSessionItem(user.id,
                        user.name,
                        isFriend ? QStringLiteral("在线") : (isPending ? QStringLiteral("申请中") : QStringLiteral("在线陌生人")),
                        false,
                        &visibleCount,
                        &visibleOnlineUsers);
        if (!isFriend && !isPending) ++visibleStrangers;
    }

    if (visibleCount == 0 && !m_contactFilter.isEmpty()) {
        QStandardItem* addItem = new QStandardItem(QString("搜索并发送申请 QQ:%1").arg(m_contactFilter));
        addItem->setData("search_add:" + m_contactFilter, Qt::UserRole + 1);
        addItem->setData(QString("搜索 %1").arg(m_contactFilter), SessionNameRole);
        addItem->setData("回车或双击查找好友", SessionLastMessageRole);
        addItem->setData(QDateTime::currentDateTime().toString("hh:mm"), SessionTimeRole);
        addItem->setEditable(false);
        m_userListModel->appendRow(addItem);
        QStandardItem* groupItem = new QStandardItem(QString("创建群聊:%1").arg(m_contactFilter));
        groupItem->setData("create_group:" + m_contactFilter, Qt::UserRole + 1);
        groupItem->setData(QString("创建 %1").arg(m_contactFilter), SessionNameRole);
        groupItem->setData("双击立即建群并进入", SessionLastMessageRole);
        groupItem->setData(QDateTime::currentDateTime().toString("hh:mm"), SessionTimeRole);
        groupItem->setEditable(false);
        m_userListModel->appendRow(groupItem);
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

void MainWindow::refreshGroupMemberPanel() {
    if (!ui->groupMemberListView || !m_groupMemberModel) return;
    QString filter = ui->memberSearchEdit ? ui->memberSearchEdit->text().trimmed() : QString();
    m_groupMemberModel->clear();
    m_groupMemberModel->setHorizontalHeaderLabels({"群成员"});

    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        QStringList members = m_localGroupMembers.value(m_privateChatTarget);
        if (members.isEmpty()) members << m_currentUserId;
        const QString ownerId = members.first();
        const QString ownerName = ownerId == m_currentUserId ? m_currentUserName : contactDisplayName(ownerId);
        int visibleMembers = 0;
        int onlineMembers = 0;
        int friendMembers = 0;
        int pendingMembers = 0;
        for (const QString& memberId : members) {
            QString name = memberId == m_currentUserId ? m_currentUserName : m_friendNames.value(memberId, memberId);
            bool online = isContactOnline(memberId) || memberId == m_currentUserId;
            bool isFriend = m_friendIds.contains(memberId);
            bool isPending = !isFriend && m_pendingOutgoingFriendRequests.contains(memberId);
            if (online) ++onlineMembers;
            if (isFriend) ++friendMembers;
            if (isPending) ++pendingMembers;
            if (!filter.isEmpty()
                && !memberId.contains(filter, Qt::CaseInsensitive)
                && !name.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            const GroupMemberDisplayState display = m_groupManager.memberDisplayState(
                memberId, m_currentUserId, ownerId, QString(), isFriend, isPending, online, false);
            QStandardItem* item = new QStandardItem();
            item->setData(memberId, MemberIdRole);
            item->setData(name, MemberNameRole);
            item->setData(display.role, MemberRoleTextRole);
            item->setData(online, MemberOnlineRole);
            item->setData(display.isOwner, MemberIsOwnerRole);
            item->setData(display.isAdmin, MemberIsAdminRole);
            item->setData(isFriend, MemberIsFriendRole);
            item->setData(isPending, MemberIsPendingRole);
            item->setData(peerAvatarIcon(memberId, name).pixmap(34, 34), MemberAvatarRole);
            item->setData(memberId, Qt::UserRole + 1);
            item->setEditable(false);
            m_groupMemberModel->appendRow(item);
            ++visibleMembers;
        }
        QString pendingPart = pendingMembers > 0 ? QString(" · 申请中%1").arg(pendingMembers) : QString();
        ui->memberTitleLabel->setText(QString("群聊成员 %1 · 群主:%2 · 在线%3 · 好友%4%5")
            .arg(members.size())
            .arg(ownerName)
            .arg(onlineMembers)
            .arg(friendMembers)
            .arg(pendingPart));
        if (visibleMembers == 0 && !filter.isEmpty()) {
            QStandardItem* addItem = new QStandardItem(QString("邀请 QQ:%1\\n双击自动加入当前群聊").arg(filter));
            addItem->setData("group_invite:" + filter, Qt::UserRole + 1);
            addItem->setEditable(false);
            addItem->setForeground(QColor(18, 150, 247));
            addItem->setEnabled(isCurrentUserGroupOwner(m_privateChatTarget));
            addItem->setToolTip(isCurrentUserGroupOwner(m_privateChatTarget) ? "双击邀请该 QQ 入群" : "只有群主可以邀请新成员入群");
            m_groupMemberModel->appendRow(addItem);
            ui->memberTitleLabel->setText(QString("群聊成员 %1 · 群主:%2 · 在线%3 · 好友%4%5 · %6QQ:%7")
                .arg(members.size())
                .arg(ownerName)
                .arg(onlineMembers)
                .arg(friendMembers)
                .arg(pendingPart)
                .arg(isCurrentUserGroupOwner(m_privateChatTarget) ? "可邀请" : "无权限邀请")
                .arg(filter));
        } else if (!filter.isEmpty()) {
            ui->memberTitleLabel->setText(QString("群聊成员 %1 · 群主:%2 · 在线%3 · 好友%4%5 · 匹配%6")
                .arg(members.size())
                .arg(ownerName)
                .arg(onlineMembers)
                .arg(friendMembers)
                .arg(pendingPart)
                .arg(visibleMembers));
        }
        refreshSessionSummary();
        return;
    }

    const QStringList serverPublicMembers = m_serverGroupMembers.value("public");
    if (isCurrentUserRemovedFromPublicGroup()) {
        const QJsonObject removedInfo = m_removedServerGroups.value("public");
        const QString removedBy = removedInfo["removedByName"].toString(removedInfo["removedBy"].toString());
        const QString removedAt = removedInfo["removedAt"].toString();
        const QString detail = removedBy.isEmpty()
            ? QStringLiteral("只读历史 · 等待重新邀请")
            : QStringLiteral("只读历史 · %1 移出%2")
                .arg(removedBy, removedAt.isEmpty() ? QString() : QStringLiteral(" · %1").arg(removedAt));
        QStandardItem* removedItem = new QStandardItem(QString("已不在公共群 QQ:%1\n%2").arg(m_currentUserId, detail));
        removedItem->setData(m_currentUserId, Qt::UserRole + 1);
        removedItem->setEditable(false);
        removedItem->setEnabled(false);
        removedItem->setForeground(QColor(170, 110, 20));
        removedItem->setToolTip("服务端已移出当前账号；本机聊天历史仍可查看，重新邀请后会自动恢复群成员列表");
        if (filter.isEmpty()
            || m_currentUserId.contains(filter, Qt::CaseInsensitive)
            || QString("等待邀请 只读 历史").contains(filter, Qt::CaseInsensitive)) {
            m_groupMemberModel->appendRow(removedItem);
        } else {
            delete removedItem;
        }
        ui->memberTitleLabel->setText("公共群成员 · 当前账号已被移出 · 历史只读");
        refreshSessionSummary();
        return;
    }

    if (!serverPublicMembers.isEmpty()) {
        const bool canManagePublicGroup = canCurrentUserManageServerGroup("public");
        const QString ownerId = m_serverGroupOwners.value("public");
        const QString ownerName = ownerId == m_currentUserId
            ? m_currentUserName
            : m_serverGroupMemberNames.value("public|" + ownerId, contactDisplayName(ownerId));
        int memberCount = 0;
        int visibleMembers = 0;
        int friendMembers = 0;
        int pendingMembers = 0;
        int onlineMembers = 0;

        for (const QString& memberId : serverPublicMembers) {
            if (memberId.trimmed().isEmpty()) continue;
            const QString name = memberId == m_currentUserId
                ? m_currentUserName
                : m_serverGroupMemberNames.value("public|" + memberId, contactDisplayName(memberId));
            const bool online = memberId == m_currentUserId || isContactOnline(memberId);
            const bool isFriend = m_friendIds.contains(memberId);
            const bool isPending = !isFriend && memberId != m_currentUserId && m_pendingOutgoingFriendRequests.contains(memberId);
            ++memberCount;
            if (online) ++onlineMembers;
            if (isFriend) ++friendMembers;
            if (isPending) ++pendingMembers;
            if (!filter.isEmpty()
                && !memberId.contains(filter, Qt::CaseInsensitive)
                && !name.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }

            const QString serverRole = m_serverGroupMemberRoles.value("public|" + memberId, "member").toLower();
            const qint64 mutedUntil = m_serverGroupMemberMutedUntil.value("public|" + memberId, 0);
            const GroupMemberDisplayState display = m_groupManager.memberDisplayState(
                memberId, m_currentUserId, ownerId, serverRole, isFriend, isPending, online, true);
            QStandardItem* item = new QStandardItem();
            item->setData(memberId, MemberIdRole);
            item->setData(name, MemberNameRole);
            item->setData(display.role, MemberRoleTextRole);
            item->setData(online, MemberOnlineRole);
            item->setData(display.isOwner, MemberIsOwnerRole);
            item->setData(display.isAdmin, MemberIsAdminRole);
            item->setData(isFriend, MemberIsFriendRole);
            item->setData(isPending, MemberIsPendingRole);
            item->setData(peerAvatarIcon(memberId, name).pixmap(34, 34), MemberAvatarRole);
            item->setData(mutedUntil, MemberMutedUntilRole);
            item->setData(memberId, Qt::UserRole + 1);
            item->setEditable(false);
            m_groupMemberModel->appendRow(item);
            ++visibleMembers;
        }

        if (visibleMembers == 0 && !filter.isEmpty()) {
            QStandardItem* addItem = new QStandardItem(canManagePublicGroup
                ? QString("邀请 QQ:%1 加入公共群\n双击提交服务端成员变更").arg(filter)
                : QString("搜索并发送申请 QQ:%1\n双击查找好友").arg(filter));
            addItem->setData("group_search_add:" + filter, Qt::UserRole + 1);
            addItem->setEditable(false);
            addItem->setForeground(QColor(18, 150, 247));
            addItem->setToolTip(canManagePublicGroup ? "双击后由服务端校验群主/管理员权限并添加成员" : "双击查找该 QQ 并发送好友申请");
            m_groupMemberModel->appendRow(addItem);
            ui->memberTitleLabel->setText(QString("群聊成员 %1 · 群主:%2 · 在线%3 · %4QQ:%5")
                .arg(memberCount)
                .arg(ownerName.isEmpty() ? "未指定" : ownerName)
                .arg(onlineMembers)
                .arg(canManagePublicGroup ? "可邀请" : "可搜索")
                .arg(filter));
            return;
        }

        QString pendingPart = pendingMembers > 0 ? QString(" · 申请中%1").arg(pendingMembers) : QString();
        int auditVisibleCount = 0;
        const QJsonArray auditEvents = m_serverGroupAuditEvents.value("public");
        if (!auditEvents.isEmpty() && (filter.isEmpty() || QStringLiteral("审计").contains(filter, Qt::CaseInsensitive))) {
            QStandardItem* auditHeader = new QStandardItem(QString("最近群审计 · %1 条\n服务端同步公告、成员和管理员变更").arg(auditEvents.size()));
            auditHeader->setEditable(false);
            auditHeader->setEnabled(false);
            auditHeader->setForeground(QColor(92, 107, 120));
            m_groupMemberModel->appendRow(auditHeader);
            const int start = qMax(0, auditEvents.size() - 3);
            for (int i = start; i < auditEvents.size(); ++i) {
                const QJsonObject event = auditEvents.at(i).toObject();
                QStandardItem* auditItem = new QStandardItem(QString("审计 · %1").arg(serverGroupAuditSummary(event)));
                auditItem->setEditable(false);
                auditItem->setEnabled(false);
                auditItem->setForeground(QColor(92, 107, 120));
                auditItem->setToolTip(QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));
                m_groupMemberModel->appendRow(auditItem);
                ++auditVisibleCount;
            }
        }
        ui->memberTitleLabel->setText(filter.isEmpty()
            ? QString("群聊成员 %1 · 群主:%2 · 在线%3 · 好友%4%5%6")
                .arg(memberCount)
                .arg(ownerName.isEmpty() ? "未指定" : ownerName)
                .arg(onlineMembers)
                .arg(friendMembers)
                .arg(pendingPart)
                .arg(auditEvents.isEmpty() ? QString() : QString(" · 审计%1").arg(auditEvents.size()))
            : QString("群聊成员 %1 · 群主:%2 · 在线%3 · 好友%4%5 · 匹配%6%7")
                .arg(memberCount)
                .arg(ownerName.isEmpty() ? "未指定" : ownerName)
                .arg(onlineMembers)
                .arg(friendMembers)
                .arg(pendingPart)
                .arg(visibleMembers)
                .arg(auditVisibleCount > 0 ? QString(" · 审计%1").arg(auditVisibleCount) : QString()));
        refreshSessionSummary();
        return;
    }

    QStandardItem* selfItem = new QStandardItem();
    selfItem->setData(m_currentUserId, MemberIdRole);
    selfItem->setData(m_currentUserName, MemberNameRole);
    selfItem->setData(QStringLiteral("我"), MemberRoleTextRole);
    selfItem->setData(true, MemberOnlineRole);
    selfItem->setData(false, MemberIsOwnerRole);
    selfItem->setData(false, MemberIsAdminRole);
    selfItem->setData(false, MemberIsFriendRole);
    selfItem->setData(false, MemberIsPendingRole);
    selfItem->setData(peerAvatarIcon(m_currentUserId, m_currentUserName).pixmap(34, 34), MemberAvatarRole);
    selfItem->setData(m_currentUserId, Qt::UserRole + 1);
    selfItem->setEditable(false);
    if (filter.isEmpty() || m_currentUserId.contains(filter, Qt::CaseInsensitive) || m_currentUserName.contains(filter, Qt::CaseInsensitive)) {
        m_groupMemberModel->appendRow(selfItem);
    } else {
        delete selfItem;
    }

    int memberCount = 1;
    int visibleMembers = 0;
    int friendMembers = 0;
    int pendingMembers = 0;
    int onlineMembers = 1;
    for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
        const ChatUser& user = it.value();
        if (user.id == m_currentUserId) continue;
        ++memberCount;
        const bool online = user.isOnline;
        if (!filter.isEmpty()
            && !user.id.contains(filter, Qt::CaseInsensitive)
            && !user.name.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        bool isFriend = m_friendIds.contains(user.id);
        bool isPending = !isFriend && m_pendingOutgoingFriendRequests.contains(user.id);
        if (isFriend) ++friendMembers;
        if (isPending) ++pendingMembers;
        if (online) ++onlineMembers;
        QStandardItem* item = new QStandardItem();
        item->setData(user.id, MemberIdRole);
        item->setData(user.name, MemberNameRole);
        item->setData(isFriend ? QStringLiteral("好友") : (isPending ? QStringLiteral("申请中") : QStringLiteral("成员")), MemberRoleTextRole);
        item->setData(online, MemberOnlineRole);
        item->setData(false, MemberIsOwnerRole);
        item->setData(false, MemberIsAdminRole);
        item->setData(isFriend, MemberIsFriendRole);
        item->setData(isPending, MemberIsPendingRole);
        item->setData(peerAvatarIcon(user.id, user.name).pixmap(34, 34), MemberAvatarRole);
        item->setData(user.id, Qt::UserRole + 1);
        item->setEditable(false);
        m_groupMemberModel->appendRow(item);
        ++visibleMembers;
    }
    if (visibleMembers == 0 && !filter.isEmpty()) {
        QStandardItem* addItem = new QStandardItem(QString("搜索并发送申请 QQ:%1\n双击查找好友").arg(filter));
        addItem->setData("group_search_add:" + filter, Qt::UserRole + 1);
        addItem->setEditable(false);
        addItem->setForeground(QColor(18, 150, 247));
        m_groupMemberModel->appendRow(addItem);
        ui->memberTitleLabel->setText(QString("群聊成员 %1 · 在线%2 · 可搜索QQ:%3").arg(memberCount).arg(onlineMembers).arg(filter));
        return;
    }
    QString pendingPart = pendingMembers > 0 ? QString(" · 申请中%1").arg(pendingMembers) : QString();
    ui->memberTitleLabel->setText(filter.isEmpty()
        ? QString("群聊成员 %1 · 在线%2 · 好友%3%4").arg(memberCount).arg(onlineMembers).arg(friendMembers).arg(pendingPart)
        : QString("群聊成员 %1 · 在线%2 · 好友%3%4 · 匹配%5").arg(memberCount).arg(onlineMembers).arg(friendMembers).arg(pendingPart).arg(visibleMembers));
    refreshSessionSummary();
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

bool MainWindow::requestServerGroupMemberUpdate(const QString& memberId, const QString& action) {
    const QStringList members = m_serverGroupMembers.value("public");
    const ServerGroupMemberUpdateDecision decision = m_groupManager.serverGroupMemberUpdateDecision(
        memberId,
        action,
        m_client && m_client->isConnected(),
        QStringLiteral("public"),
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
    if (!m_client->sendServerGroupMemberUpdate("public", decision.targetId, decision.normalizedAction)) {
        ui->statusbar->showMessage("公共群成员变更提交失败", 2600);
        return false;
    }

    const QString displayName = m_serverGroupMemberNames.value("public|" + decision.targetId, contactDisplayName(decision.targetId));
    const QString actionText = decision.normalizedAction == "add"
        ? QStringLiteral("邀请")
        : (decision.normalizedAction == "remove"
            ? QStringLiteral("移出")
            : (decision.normalizedAction == "promote_admin" ? QStringLiteral("设置管理员") : QStringLiteral("取消管理员")));
    appendSystemMessage(QString("已提交公共群%1成员请求：%2（QQ:%3），等待服务端同步").arg(actionText, displayName, decision.targetId));
    ui->statusbar->showMessage(QString("公共群%1请求已提交，等待服务端同步").arg(actionText), 2400);
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



