#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "chatsessionmanager.h"
#include "composermanager.h"
#include "filetransferstatus.h"
#include "qtnetworkchat_version.h"
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
#include <QListView>
#include <QStatusBar>
#include <QPixmap>
#include <QImage>
#include <QPainter>
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
#include <QVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QProgressDialog>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QCryptographicHash>

namespace {
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

QString humanFileSize(qint64 bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(qMax<qint64>(1, bytes / 1024));
    return QString::number(bytes / 1024.0 / 1024.0, 'f', 1) + " MB";
}

QString safeReceivedFileName(const QString& rawName, const QString& fallbackName) {
    QString fileName = QFileInfo(rawName).fileName().trimmed();
    if (fileName.isEmpty()) fileName = fallbackName;
    return fileName;
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
    QString id = selectedFriendNoticeEntryId(noticeList);
    if (id.startsWith("search_add:")) {
        id = id.mid(QString("search_add:").size());
    }
    if (id.isEmpty() && searchEdit) {
        id = searchEdit->text().trimmed();
    }
    return id;
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
    if (currentId.startsWith("search_add:")) {
        if (statusBar) statusBar->showMessage(searchAddMessage, 2200);
        return false;
    }
    if (requestId) {
        *requestId = currentId;
    }
    return true;
}

QStringList visibleFriendNoticeIds(QListWidget* noticeList) {
    QStringList ids;
    if (!noticeList) return ids;
    for (int i = 0; i < noticeList->count(); ++i) {
        QListWidgetItem* item = noticeList->item(i);
        QString id = item ? item->data(Qt::UserRole).toString() : QString();
        if (id.isEmpty()) continue;
        if (id.startsWith("search_add:")) {
            id = id.mid(QString("search_add:").size());
        }
        if (id.isEmpty() || ids.contains(id)) continue;
        ids << id;
    }
    return ids;
}

QString selectedGroupNoticeEntryId(QListWidget* noticeList) {
    if (!noticeList || !noticeList->currentItem()) return QString();
    return noticeList->currentItem()->data(Qt::UserRole).toString();
}

bool isGroupCreateEntryId(const QString& groupId) {
    return groupId.startsWith("group_create:");
}

QString groupCreateEntryName(const QString& groupId) {
    return isGroupCreateEntryId(groupId)
        ? groupId.mid(QString("group_create:").size()).trimmed()
        : QString();
}

bool trySelectedInspectableGroupNoticeId(QListWidget* noticeList,
                                         QStatusBar* statusBar,
                                         const QString& emptyMessage,
                                         const QString& createMessage,
                                         QString* groupId) {
    const QString currentId = selectedGroupNoticeEntryId(noticeList);
    if (currentId.isEmpty()) {
        if (statusBar) statusBar->showMessage(emptyMessage, 1800);
        return false;
    }
    if (isGroupCreateEntryId(currentId)) {
        if (statusBar) statusBar->showMessage(createMessage, 2200);
        return false;
    }
    if (groupId) {
        *groupId = currentId;
    }
    return true;
}

QStringList visibleGroupNoticeIds(QListWidget* noticeList) {
    QStringList ids;
    if (!noticeList) return ids;
    for (int i = 0; i < noticeList->count(); ++i) {
        QListWidgetItem* item = noticeList->item(i);
        const QString id = item ? item->data(Qt::UserRole).toString() : QString();
        if (ids.contains(id)) continue;
        ids << id;
    }
    return ids;
}

QString uniqueReceivedSavePath(const QString& directoryPath, const QString& fileName) {
    const QDir directory(directoryPath);
    const QString stampedName = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_") + fileName;
    QString candidate = directory.filePath(stampedName);
    if (!QFileInfo::exists(candidate)) return candidate;

    const QFileInfo stampedInfo(stampedName);
    const QString suffix = stampedInfo.suffix();
    const QString baseName = stampedInfo.completeBaseName();
    for (int index = 2; index < 1000; ++index) {
        const QString numberedName = suffix.isEmpty()
            ? QString("%1_%2").arg(baseName).arg(index)
            : QString("%1_%2.%3").arg(baseName).arg(index).arg(suffix);
        candidate = directory.filePath(numberedName);
        if (!QFileInfo::exists(candidate)) return candidate;
    }

    return directory.filePath(QString("%1_%2").arg(stampedName).arg(QDateTime::currentMSecsSinceEpoch()));
}

QString extractSavePathFromChatText(const QString& text) {
    QString savePath = text.section("保存路径：", 1, 1).section(" · ", 0, 0).trimmed();
    if (savePath.isEmpty()) savePath = text.section("自动保存:", 1).section(" · ", 0, 0).trimmed();
    if (savePath.isEmpty()) savePath = text.section("自动保存：", 1).section(" · ", 0, 0).trimmed();
    if (savePath.isEmpty()) savePath = text.section("已保存到：", 1, 1).section('\n', 0, 0).section(" · ", 0, 0).trimmed();
    return savePath;
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

QString lastTransferDirectory() {
    QSettings settings("QtNetworkChat", "QtNetworkChat");
    QString directory = settings.value("transfer/lastDirectory").toString();
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    if (directory.isEmpty() || !QDir(directory).exists()) {
        directory = QDir::homePath();
    }
    return directory;
}

void rememberTransferDirectory(const QString& filePath) {
    const QString directory = QFileInfo(filePath).absolutePath();
    if (directory.isEmpty() || !QDir(directory).exists()) return;

    QSettings settings("QtNetworkChat", "QtNetworkChat");
    settings.setValue("transfer/lastDirectory", directory);
}

QString lastAvatarDirectory() {
    QSettings settings("QtNetworkChat", "QtNetworkChat");
    QString directory = settings.value("avatar/lastDirectory").toString();
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    if (directory.isEmpty() || !QDir(directory).exists()) {
        directory = QDir::homePath();
    }
    return directory;
}

void rememberAvatarDirectory(const QString& filePath) {
    const QString directory = QFileInfo(filePath).absolutePath();
    if (directory.isEmpty() || !QDir(directory).exists()) return;

    QSettings settings("QtNetworkChat", "QtNetworkChat");
    settings.setValue("avatar/lastDirectory", directory);
}

bool confirmTransferFile(QWidget* parent, const QFileInfo& info, const QString& kind, QString* failureMessage = nullptr) {
    constexpr qint64 warningBytes = 20LL * 1024 * 1024;
    constexpr qint64 maxBytes = 80LL * 1024 * 1024;
    auto setFailure = [failureMessage](const QString& text) {
        if (failureMessage) *failureMessage = text;
    };

    if (!info.exists() || !info.isFile()) {
        QMessageBox::warning(parent, "无法发送", "请选择一个可读取的本地文件。");
        setFailure(QString("%1发送失败：文件不可读取").arg(kind));
        return false;
    }
    if (info.size() <= 0) {
        QMessageBox::warning(parent, "无法发送", "文件为空，已取消发送。");
        setFailure(QString("%1发送失败：文件为空").arg(kind));
        return false;
    }
    if (info.size() > maxBytes) {
        QMessageBox::warning(parent,
                             "文件过大",
                             QString("%1大小为 %2，超过当前 80 MB 的安全发送上限。")
                                 .arg(kind, humanFileSize(info.size())));
        setFailure(QString("%1发送失败：超过 80 MB").arg(kind));
        return false;
    }
    if (info.size() > warningBytes) {
        const bool confirmed = QMessageBox::question(parent,
                                                     "确认发送大文件",
                                                     QString("%1大小为 %2，发送时可能需要等待一会儿，是否继续？")
                                                         .arg(kind, humanFileSize(info.size())),
                                                     QMessageBox::Yes | QMessageBox::No,
                                                     QMessageBox::No) == QMessageBox::Yes;
        if (!confirmed) setFailure(QString("已取消发送%1").arg(kind));
        return confirmed;
    }
    return true;
}

QPixmap squareAvatarPixmap(const QPixmap& source, int side) {
    if (source.isNull() || side <= 0) return QPixmap();
    QPixmap scaled = source.scaled(side, side, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = qMax(0, (scaled.width() - side) / 2);
    const int y = qMax(0, (scaled.height() - side) / 2);
    return scaled.copy(x, y, side, side);
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
{
    ui->setupUi(this);
    setMinimumSize(980, 680);
    setWindowIcon(createChatIcon(userName));
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
            .arg(fileName, humanFileSize(bytesReceived), humanFileSize(totalBytes))
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
    saveProfileToSqlite();
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

        QMetaObject::Connection cancelConnection = connect(
            &progress,
            &QProgressDialog::canceled,
            this,
            [this, &progress, &info, &kind, &cancelRequested, canceled]() {
                cancelRequested = true;
                if (canceled) *canceled = true;
                progress.setLabelText(m_transferManager.sendingCancelState(kind, info.fileName()).labelText);
                if (m_client) m_client->cancelCurrentOutgoingTransfer();
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
                QApplication::processEvents();
            });

        const bool ok = asImage
            ? m_client->sendImage(filePath, receiverId)
            : m_client->sendFile(filePath, receiverId);

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
    appendSystemMessage(event.message);
    ui->chatHintLabel->setText(event.chatHintText);
    ui->statusbar->showMessage(event.statusBarMessage, event.statusBarTimeoutMs);
}

MainWindow::SavedFileActionState MainWindow::savedFileActionState(const QModelIndex& index) const {
    SavedFileActionState state;
    if (!index.isValid()) return state;

    state.savePath = extractSavePathFromChatText(index.data().toString());
    if (state.savePath.isEmpty()) {
        state.savePath = extractSavePathFromChatText(index.data(Qt::ToolTipRole).toString());
    }
    state.fileInfo = QFileInfo(state.savePath);
    state.folderInfo = QFileInfo(state.fileInfo.absolutePath());
    state.hasSavePath = !state.savePath.isEmpty();
    state.canOpenFile = state.hasSavePath && state.fileInfo.exists() && state.fileInfo.isFile();
    state.canOpenFolder = state.hasSavePath && state.folderInfo.exists() && state.folderInfo.isDir();
    return state;
}

bool MainWindow::isChatMediaMessage(const QString& chatText, const SavedFileActionState& savedFileState) const {
    return savedFileState.hasSavePath
        || chatText.contains("文件")
        || chatText.contains("图片")
        || chatText.contains("视频")
        || chatText.contains("媒体")
        || chatText.contains("查收话术")
        || chatText.contains("回执话术");
}

void MainWindow::configureSavedFileActions(QAction* copySavePathAction,
                                           QAction* openSavedFileAction,
                                           QAction* openSaveFolderAction,
                                           const SavedFileActionState& savedFileState) const {
    copySavePathAction->setEnabled(savedFileState.hasSavePath);
    openSavedFileAction->setEnabled(savedFileState.canOpenFile);
    openSaveFolderAction->setEnabled(savedFileState.canOpenFolder);

    if (!savedFileState.hasSavePath) {
        copySavePathAction->setToolTip("这条记录还没有保存路径");
        openSavedFileAction->setToolTip("收到并保存文件后可直接打开");
        openSaveFolderAction->setToolTip("收到并保存文件后可打开目录");
        return;
    }

    if (!savedFileState.canOpenFile) {
        openSavedFileAction->setToolTip(savedFileState.fileInfo.exists()
            ? "当前保存路径不是文件"
            : "保存文件不存在或文件已移动");
    }
    if (!savedFileState.canOpenFolder) {
        openSaveFolderAction->setToolTip("保存目录不存在或无权访问");
    }
}

bool MainWindow::copySavedFilePathToClipboard(const SavedFileActionState& savedFileState) {
    if (savedFileState.savePath.isEmpty()) {
        ui->statusbar->showMessage("当前消息没有保存路径", 2200);
        return false;
    }

    QApplication::clipboard()->setText(savedFileState.savePath);
    ui->statusbar->showMessage("保存路径已复制", 2200);
    return true;
}

bool MainWindow::openSavedFileFromState(const SavedFileActionState& savedFileState, const QString& missingMessage) {
    if (savedFileState.canOpenFile
        && QDesktopServices::openUrl(QUrl::fromLocalFile(savedFileState.fileInfo.absoluteFilePath()))) {
        ui->statusbar->showMessage("已打开保存文件", 2200);
        return true;
    }

    showFileTransferStatusEvent(savedFileState.fileInfo.fileName(),
                                QString(),
                                QStringLiteral("receive-open-failed"),
                                0,
                                0);
    ui->statusbar->showMessage(missingMessage, 2200);
    return false;
}

bool MainWindow::openSavedFolderFromState(const SavedFileActionState& savedFileState) {
    if (!savedFileState.canOpenFolder) {
        ui->statusbar->showMessage("当前消息没有可打开的保存路径", 2200);
        return false;
    }

    if (QDesktopServices::openUrl(QUrl::fromLocalFile(savedFileState.folderInfo.absoluteFilePath()))) {
        ui->statusbar->showMessage("已打开保存目录", 2200);
        return true;
    }

    ui->statusbar->showMessage("保存目录无法打开", 2200);
    return false;
}

void MainWindow::copyTextWithStatus(const QString& text, const QString& statusMessage, int timeoutMs) {
    QApplication::clipboard()->setText(text);
    ui->statusbar->showMessage(statusMessage, timeoutMs);
}

bool MainWindow::handleSavedFileContextCommand(const QString& commandId, const SavedFileActionState& savedFileState) {
    if (commandId == QLatin1String("copy-save-path")) {
        return copySavedFilePathToClipboard(savedFileState);
    }
    if (commandId == QLatin1String("open-saved-file")) {
        return openSavedFileFromState(savedFileState, "当前消息没有可打开的文件");
    }
    if (commandId == QLatin1String("open-save-folder")) {
        return openSavedFolderFromState(savedFileState);
    }
    return false;
}

bool MainWindow::handleChatCopyContextCommand(const QString& commandId, const QString& chatText) {
    if (commandId == QLatin1String("copy-message")) {
        copyTextWithStatus(chatText, "消息已复制");
        return true;
    }
    if (commandId == QLatin1String("copy-plain")) {
        copyTextWithStatus(chatPlainContentText(chatText), "消息内容已复制");
        return true;
    }
    if (commandId == QLatin1String("copy-sender")) {
        copyTextWithStatus(chatSenderText(chatText), "发送者已复制");
        return true;
    }
    if (commandId == QLatin1String("copy-time")) {
        const QString timeText = chatTimeText(chatText);
        copyTextWithStatus(timeText, "消息时间已复制: " + timeText);
        return true;
    }
    if (commandId == QLatin1String("copy-media-card")) {
        copyTextWithStatus(chatMediaCardText(chatText), "媒体卡片已复制", 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-file-notice")) {
        copyTextWithStatus(chatMediaNoticeText(chatText), "查收话术已复制", 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-receipt")) {
        copyTextWithStatus(chatMediaReceiptText(chatText), "回执话术已复制", 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-media-flow")) {
        copyTextWithStatus(chatMediaFlowText(chatText), "媒体流程已复制", 2200);
        return true;
    }
    return false;
}

bool MainWindow::handleChatDraftContextCommand(const QString& commandId, const QString& chatText) {
    if (commandId == QLatin1String("quote")) {
        quoteChatMessage(chatText);
        return true;
    }
    if (commandId == QLatin1String("forward")) {
        forwardChatMessage(chatText);
        return true;
    }
    if (commandId == QLatin1String("resend")) {
        resendChatMessage(chatText);
        return true;
    }
    if (commandId == QLatin1String("mention-reply")) {
        mentionChatSender(chatText);
        return true;
    }
    return false;
}

void MainWindow::setChatDraftText(const QString& text, const QString& statusMessage, int timeoutMs) {
    ui->messageEdit->setPlainText(text);
    ui->messageEdit->setFocus();
    ui->statusbar->showMessage(statusMessage, timeoutMs);
}

void MainWindow::quoteChatMessage(const QString& chatText) {
    setChatDraftText(QString("> %1\n").arg(chatText), "已插入引用回复");
}

void MainWindow::forwardChatMessage(const QString& chatText) {
    setChatDraftText(QString("转发：%1").arg(chatPlainContentText(chatText)), "已转发到输入框");
}

void MainWindow::resendChatMessage(const QString& chatText) {
    ui->messageEdit->setPlainText(chatResendContentText(chatText));
    ui->messageEdit->setFocus();
    onSendMessage();
}

void MainWindow::mentionChatSender(const QString& chatText) {
    const QString name = chatMentionTargetText(chatText);
    setChatDraftText(QString("@%1 ").arg(name), QString("已插入 @%1 回复").arg(name));
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
                                          const SavedFileActionState& savedFileState) {
    if (handleChatCopyContextCommand(commandId, chatText)
        || handleSavedFileContextCommand(commandId, savedFileState)
        || handleChatDraftContextCommand(commandId, chatText)) {
        return true;
    }
    return false;
}

QString MainWindow::chatPlainContentText(const QString& chatText) const {
    QString content = chatText.section(']', 2).trimmed();
    if (content.isEmpty()) content = chatText;
    return content;
}

QString MainWindow::chatResendContentText(const QString& chatText) const {
    QString content = chatText.section(']', 2).trimmed();
    if (content.isEmpty()) content = chatText.section('>', 1).trimmed();
    if (content.isEmpty()) content = chatText;
    return content;
}

QString MainWindow::chatSenderText(const QString& chatText) const {
    QString sender = chatText.section('<', 1, 1).section('>', 0, 0).trimmed();
    if (sender.isEmpty()) sender = chatText.section(']', 1, 1).trimmed();
    return sender;
}

QString MainWindow::chatTimeText(const QString& chatText) const {
    QString timeText = chatText.section(']', 0, 0).section('[', 1).trimmed();
    if (timeText.isEmpty()) timeText = QDateTime::currentDateTime().toString("hh:mm:ss");
    return timeText;
}

QString MainWindow::chatMentionTargetText(const QString& chatText) const {
    QString name = chatText.section('<', 1, 1).section('>', 0, 0).trimmed();
    if (name.isEmpty()) name = contactDisplayName(m_privateChatTarget);
    return name;
}

QString MainWindow::mediaTypeFromChatText(const QString& text) const {
    if (text.contains("视频")) return QStringLiteral("视频");
    if (text.contains("图片")) return QStringLiteral("图片");
    return QStringLiteral("文件");
}

QString MainWindow::chatMediaCardText(const QString& chatText) const {
    QString fileName = chatText.section(" · ", 0, 0).section(']', -1).trimmed();
    if (fileName.isEmpty()) fileName = chatText;

    const QString sender = chatText.section('<', 1, 1).section('>', 0, 0).trimmed();
    return QString("%1卡片\n文件:%2\n会话:%3\n发送者:%4\n我的QQ:%5")
        .arg(mediaTypeFromChatText(chatText),
             fileName,
             m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget),
             sender.isEmpty() ? m_currentUserName : sender,
             m_currentUserId);
}

QString MainWindow::chatMediaNoticeText(const QString& chatText) const {
    QString fileName = chatText.section(" · ", 0, 0).section(']', -1).trimmed();
    if (fileName.isEmpty()) fileName = "刚发送的文件";

    const QString target = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    return QString("我已发送 %1 到 %2，请注意查收。").arg(fileName, target);
}

QString MainWindow::chatMediaReceiptText(const QString& chatText) const {
    QString fileName = chatText.section(" · ", 1, 1).trimmed();
    if (fileName.isEmpty()) fileName = chatText.section("已收到", 1, 1).section("，", 0, 0).trimmed();
    if (fileName.isEmpty()) fileName = "刚收到的文件";
    return QString("已收到 %1，文件已保存，我会尽快查看。").arg(fileName);
}

QString MainWindow::chatMediaFlowText(const QString& chatText) const {
    QString fileName = chatText.section(" · ", 1, 1).trimmed();
    if (fileName.isEmpty()) fileName = chatText.section(" · ", 0, 0).section(']', -1).trimmed();
    if (fileName.isEmpty()) fileName = "当前媒体文件";

    const QString target = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    QStringList rows;
    rows << QString("媒体流程 · 类型:%1 · 文件:%2").arg(mediaTypeFromChatText(chatText), fileName);
    rows << QString("会话:%1 · 我的QQ:%2 · 昵称:%3").arg(target, m_currentUserId, m_currentUserName);
    rows << "1. 发送方点击图片/视频或闪传文件选择媒体";
    rows << "2. 聊天记录生成媒体卡片和查收话术";
    rows << "3. 接收方自动保存后可复制回执话术和保存路径";
    rows << QString("查收话术：我已发送 %1 到 %2，请注意查收。").arg(fileName, target);
    rows << QString("回执话术：已收到 %1，文件已保存，我会尽快查看。").arg(fileName);
    return rows.join('\n');
}

void MainWindow::applyReceivedTransferSaveStatus(const QString& kind,
                                                 const QString& fileName,
                                                 const QString& displayName,
                                                 const QString& receivedSize,
                                                 const QString& manifestSuffix,
                                                 const QString& integritySuffix,
                                                 const QString& transferId,
                                                 qint64 receivedBytes,
                                                 qint64 totalBytes,
                                                 bool saved,
                                                 bool integrityFailed) {
    if (saved) {
        showFileTransferStatusEvent(fileName,
                                    transferId,
                                    integrityFailed ? QStringLiteral("hash") : QStringLiteral("receive-saved"),
                                    receivedBytes,
                                    totalBytes);
        ui->chatHintLabel->setText(QString("已接收%1 · %2 · %3 · 来自 %4%5%6")
            .arg(kind, fileName, receivedSize, displayName, manifestSuffix, integritySuffix));
        ui->statusbar->showMessage(QString("%1已保存到下载目录 · %2%3%4").arg(kind, receivedSize, manifestSuffix, integritySuffix), 3000);
        return;
    }

    showFileTransferStatusEvent(fileName,
                                transferId,
                                QStringLiteral("receive-save-failed"),
                                receivedBytes,
                                totalBytes);
    ui->chatHintLabel->setText(QString("%1保存失败 · %2 · 来自 %3").arg(kind, fileName, displayName));
    ui->statusbar->showMessage(QString("%1保存失败，请检查下载目录权限").arg(kind), 3200);
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
            QApplication::processEvents();
        });

    ui->statusbar->showMessage("正在恢复未完成发送：" + fileName, 1800);
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
        ui->statusbar->showMessage(resultState.statusMessage, 2600);
    } else if (resultState.canceled) {
        appendSystemMessage(resultState.systemMessage);
        ui->chatHintLabel->setText(resultState.hintText);
        ui->statusbar->showMessage(resultState.statusMessage, 2200);
    } else {
        appendSystemMessage(resultState.systemMessage);
        ui->chatHintLabel->setText(resultState.hintText);
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
            ui->statusbar->showMessage(resultState.clearedStatusMessage, 2200);
        }
    }

    updateSavedOutgoingTransferRecoveryUi(false);
}

void MainWindow::setupUi() {
    m_userListModel->setHorizontalHeaderLabels({"在线用户"});
    ui->userListView->setModel(m_userListModel);
    ui->userListView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    ui->chatListView->setModel(m_chatModel);
    ui->chatListView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->chatListView->setToolTip("右键消息可复制、引用和转发；双击带保存路径的文件记录可直接打开文件");

    m_groupMemberModel->setHorizontalHeaderLabels({"群成员"});
    ui->groupMemberListView->setModel(m_groupMemberModel);
    ui->groupMemberListView->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->messageEdit->setPlaceholderText("输入消息... (Enter 发送，Shift/Ctrl+Enter 换行，Esc 清空草稿)");
    ui->messageEdit->setFocus();
    ui->messageEdit->installEventFilter(this);
    ui->messageEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->contactSearchEdit->installEventFilter(this);
    ui->memberSearchEdit->installEventFilter(this);
    ui->contactSearchEdit->setToolTip("搜索联系人、QQ 号或群聊；按 Enter 搜索账号，Esc 清空");
    ui->memberSearchEdit->setToolTip("搜索当前群成员；群聊中可输入 QQ 号后按 Enter 邀请");

    ui->clearBtn->setObjectName("clearBtn");
    ui->clearBtn->setToolTip("清空当前会话的本地聊天记录");
    ui->fileBtn->setObjectName("toolBtn");
    ui->fileBtn->setToolTip("闪传文件，支持文档、压缩包和媒体文件");
    ui->imageBtn->setObjectName("toolBtn");
    ui->imageBtn->setText("图片/视频");
    ui->imageBtn->setToolTip("发送图片或视频文件，图片会显示预览");
    ui->emojiBtn->setObjectName("iconToolBtn");
    ui->emojiBtn->setToolTip("插入常用表情");
    ui->mentionBtn->setObjectName("iconToolBtn");
    ui->mentionBtn->setToolTip("快速 @ 群成员或插入会话提醒");
    ui->sendBtn->setToolTip("请输入消息后发送");
    ui->sendBtn->setEnabled(false);
    ui->globalSearchBtn->setToolTip("打开综合搜索；搜索框有内容时直接搜索该 QQ 号");
    ui->createMenuBtn->setToolTip("打开创建和快捷操作菜单");
    ui->friendNoticeBtn->setToolTip("查看并处理好友申请");
    ui->groupNoticeBtn->setToolTip("查看群聊、群公告和入群邀请");
    ui->copyAccountBtn->setToolTip("复制当前 QQ 账号");
    ui->friendManagerBtn->setToolTip("打开好友管理器");
    ui->groupChatBtn->setToolTip("返回公共聊天室");
    ui->uploadAvatarBtn->setToolTip("更换当前头像");
    setStyleSheet(R"(
        QMainWindow, QWidget#centralwidget {
            background: #EEF4F7;
            font-family: "Microsoft YaHei", "Segoe UI";
            font-size: 13px;
            color: #253342;
        }
        QFrame#sidePanel {
            background: qlineargradient(x1:0, y1:0, x2:0.9, y2:1, stop:0 #20D6C5, stop:0.44 #149EE9, stop:1 #5B5CE2);
        }
        QLabel#appTitleLabel {
            color: white;
            font-size: 21px;
            font-weight: 800;
            padding-bottom: 2px;
        }
        QLabel#avatarLabel {
            background: white;
            color: #1289DF;
            border-radius: 36px;
            font-size: 30px;
            font-weight: 700;
            margin-left: 63px;
            margin-right: 63px;
        }
        QFrame#profileCard {
            background: rgba(255, 255, 255, 48);
            border: 1px solid rgba(255, 255, 255, 86);
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
            background: rgba(255, 255, 255, 232);
            color: #1278D4;
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
            background: rgba(255, 255, 255, 232);
            color: #1278D4;
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
            color: #253342;
            border: 1px solid rgba(255, 255, 255, 105);
            border-radius: 15px;
            min-height: 30px;
            padding: 3px 12px;
        }
        QPushButton#globalSearchBtn, QPushButton#createMenuBtn {
            background: rgba(255, 255, 255, 232);
            color: #1278D4;
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
            background: rgba(255, 255, 255, 42);
            color: white;
            border: 1px solid rgba(255, 255, 255, 78);
            border-radius: 16px;
            padding: 6px;
            outline: none;
            alternate-background-color: rgba(255, 255, 255, 18);
        }
        QListView#userListView::item {
            height: 48px;
            border-radius: 11px;
            padding-left: 8px;
        }
        QListView#userListView::item:selected, QListView#userListView::item:hover {
            background: rgba(255, 255, 255, 86);
        }
        QFrame#chatHeader, QFrame#inputPanel, QListView#chatListView {
            background: #FFFFFF;
            border: 1px solid #DDE8F0;
            border-radius: 18px;
        }
        QFrame#chatPanel {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #F3F8FB, stop:0.56 #F8FBFD, stop:1 #EEF8F5);
        }
        QFrame#groupInfoPanel {
            background: #F8FBFC;
            border-left: 1px solid #E1EAF1;
        }
        QFrame#announcementCard {
            background: white;
            border: 1px solid #E2EAF1;
            border-radius: 14px;
        }
        QLabel#announcementTitleLabel, QLabel#memberTitleLabel {
            color: #203144;
            font-size: 14px;
            font-weight: 800;
        }
        QLabel#announcementTitleLabel a {
            color: #168BE8;
            text-decoration: none;
        }
        QLabel#announcementBodyLabel {
            color: #6E7F90;
            font-size: 12px;
            line-height: 18px;
        }
        QLineEdit#memberSearchEdit {
            background: white;
            color: #253342;
            border: 1px solid #DAE6EF;
            border-radius: 15px;
            min-height: 30px;
            padding: 3px 12px;
        }
        QLineEdit#memberSearchEdit:focus {
            border: 1px solid #17A8F3;
        }
        QListView#groupMemberListView {
            background: white;
            border: 1px solid #E1EAF1;
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
            color: #166BAF;
        }
        QLabel#chatTitleLabel {
            color: #203144;
            font-size: 18px;
            font-weight: 700;
        }
        QLabel#chatHintLabel {
            color: #7F8D9B;
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
            background: #F2F8FC;
        }
        QTextEdit#messageEdit {
            background: #F9FBFD;
            border: 1px solid #DAE6EF;
            border-radius: 14px;
            padding: 8px 10px;
            selection-background-color: #17B8F2;
            selection-color: white;
        }
        QTextEdit#messageEdit:focus {
            background: white;
            border: 1px solid #17A8F3;
        }
        QPushButton {
            background: #F1F6FA;
            color: #34495B;
            border: 1px solid #D2E0EA;
            border-radius: 12px;
            padding: 7px 14px;
        }
        QPushButton:hover {
            background: #E8F2F9;
            border-color: #BBD3E5;
        }
        QPushButton:pressed {
            background: #DCEBF5;
        }
        QPushButton:disabled {
            background: #EEF2F5;
            color: #A5B1BC;
            border-color: #E0E7ED;
        }
        QPushButton#sendBtn {
            background: #18A8F2;
            color: white;
            border: none;
            font-weight: 700;
        }
        QPushButton#sendBtn:hover {
            background: #0E95DF;
        }
        QPushButton#sendBtn:pressed {
            background: #0B7EC6;
        }
        QPushButton#sendBtn:disabled {
            background: #BFD0DE;
            color: #F8FBFD;
            border: none;
        }
        QPushButton#toolBtn, QPushButton#iconToolBtn {
            background: transparent;
            color: #516274;
            border: none;
            border-radius: 14px;
            padding: 5px 10px;
            font-weight: 700;
            min-width: 44px;
        }
        QPushButton#iconToolBtn {
            min-width: 30px;
            font-size: 16px;
            padding: 4px 6px;
        }
        QPushButton#toolBtn:hover, QPushButton#iconToolBtn:hover {
            background: #EAF7FF;
            color: #168BE8;
        }
        QPushButton#toolBtn:disabled, QPushButton#iconToolBtn:disabled {
            background: transparent;
            color: #AEBAC4;
        }
        QPushButton#clearBtn {
            color: #D35454;
        }
        QMenuBar {
            background: #F8FBFD;
            color: #435367;
            border-bottom: 1px solid #DDE8F0;
            spacing: 4px;
        }
        QMenuBar::item {
            background: transparent;
            padding: 5px 10px;
            border-radius: 6px;
        }
        QMenuBar::item:selected {
            background: #EAF4FB;
            color: #1679CA;
        }
        QMenu {
            background: #FFFFFF;
            color: #253342;
            border: 1px solid #D7E3EC;
            border-radius: 8px;
            padding: 6px;
        }
        QMenu::item {
            padding: 7px 24px 7px 12px;
            border-radius: 6px;
        }
        QMenu::item:selected {
            background: #EAF7FF;
            color: #1679CA;
        }
        QMenu::separator {
            height: 1px;
            background: #E7EEF4;
            margin: 6px 4px;
        }
        QToolTip {
            background: #203144;
            color: white;
            border: none;
            border-radius: 6px;
            padding: 6px 8px;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 10px;
            margin: 4px 2px 4px 2px;
        }
        QScrollBar::handle:vertical {
            background: #C5D7E5;
            border-radius: 5px;
            min-height: 36px;
        }
        QScrollBar::handle:vertical:hover {
            background: #9FBCD2;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
            height: 0px;
        }
        QStatusBar {
            background: #EEF4F7;
            color: #5F7183;
            border-top: 1px solid #DCE7EF;
            padding-left: 6px;
        }
        QStatusBar::item {
            border: none;
        }
    )");

    QAction* friendManagerAction = new QAction("好友管理器", this);
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
    QAction* logoutAction = new QAction("退出登录", this);
    ui->menubar->addAction(friendManagerAction);
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
    ui->menubar->addAction(logoutAction);

    connect(friendManagerAction, &QAction::triggered, this, &MainWindow::onShowFriendManager);
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
    connect(logoutAction, &QAction::triggered, this, &MainWindow::onLogout);
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
        QAction* pasteAction = menu.addAction("粘贴");
        QAction* pasteSendAction = menu.addAction("粘贴并发送");
        QAction* sendAction = menu.addAction("立即发送");
        QAction* clearAction = menu.addAction("清空输入");
        pasteAction->setEnabled(hasClipboardText);
        pasteSendAction->setEnabled(hasClipboardText && canReachTarget);
        sendAction->setEnabled(hasDraft && canReachTarget);
        clearAction->setEnabled(hasDraft);
        pasteAction->setToolTip(hasClipboardText ? "把剪贴板文字插入输入框" : "剪贴板里没有可粘贴的文字");
        pasteSendAction->setToolTip(!canReachTarget
            ? QString("当前已断开，暂不能粘贴并发送到 %1").arg(targetName)
            : (hasClipboardText ? "粘贴剪贴板文字后立即发送" : "剪贴板里没有可发送的文字"));
        sendAction->setToolTip(!canReachTarget
            ? QString("当前已断开，暂不能发送到 %1").arg(targetName)
            : (hasDraft ? QString("发送当前输入 · %1 字").arg(draftText.size()) : "请输入消息后再发送"));
        clearAction->setToolTip(hasDraft ? "清空当前输入框内容" : "输入框已经是空的");
        auto describeInputAction = [](QAction* action, const QString& tip) {
            action->setToolTip(tip);
            action->setStatusTip(tip);
        };
        pasteAction->setStatusTip(pasteAction->toolTip());
        pasteSendAction->setStatusTip(pasteSendAction->toolTip());
        sendAction->setStatusTip(sendAction->toolTip());
        clearAction->setStatusTip(clearAction->toolTip());
        menu.addSeparator();
        QAction* quickAction = menu.addAction("插入快捷语");
        QAction* commandCardAction = menu.addAction("插入/card指令");
        QAction* commandInviteAction = menu.addAction("插入/invite指令");
        QAction* commandQqAction = menu.addAction("插入/qq指令");
        QAction* searchFriendAction = menu.addAction("插入QQ搜索话术");
        QAction* addFriendAction = menu.addAction("插入申请话术");
        QAction* inviteGroupAction = menu.addAction("插入入群邀请话术");
        QAction* quoteTemplateAction = menu.addAction("插入引用模板");
        menu.addSeparator();
        QMenu* phraseMenu = menu.addMenu("常用话术");
        QMenu* qqPhraseMenu = menu.addMenu("QQ快捷话术");
        const QStringList quickPhrases = {"在吗？", "收到，我马上看。", "稍等一下", "我发你文件", "我们群里说", "方便的话加个好友", "拉我进群聊一下", "这个 QQ 号是我"};
        const QStringList qqPhrases = {
            "你好，我是通过 QQ 搜索找到你的，方便加个好友吗？",
            "我已经发送好友申请了，通过后我们私聊。",
            "我建了一个群聊，等下把大家拉进去一起沟通。",
            "这个是我的 QQ 号，请复制保存。",
            "收到文件后麻烦回复一下。"
        };
        for (const QString& phrase : quickPhrases) {
            QAction* phraseAction = phraseMenu->addAction(phrase);
            connect(phraseAction, &QAction::triggered, ui->messageEdit, [this, phrase]() {
                ui->messageEdit->insertPlainText(phrase);
                ui->messageEdit->setFocus();
                ui->statusbar->showMessage("已插入常用话术", 1400);
            });
        }
        for (const QString& phrase : qqPhrases) {
            QAction* phraseAction = qqPhraseMenu->addAction(phrase);
            connect(phraseAction, &QAction::triggered, ui->messageEdit, [this, phrase]() {
                ui->messageEdit->insertPlainText(phrase);
                ui->messageEdit->setFocus();
                ui->statusbar->showMessage("已插入 QQ 快捷话术", 1400);
            });
        }
        QAction* mentionAction = menu.addAction("@成员");
        QAction* friendCardAction = menu.addAction("插入我的QQ名片");
        QAction* groupCardAction = menu.addAction("插入当前会话名片");
        QAction* fileTemplateAction = menu.addAction("插入发文件模板");
        QAction* imageTemplateAction = menu.addAction("插入发图片模板");
        QAction* videoTemplateAction = menu.addAction("插入发视频模板");
        QAction* groupInviteTemplateAction = menu.addAction("插入拉群模板");
        QAction* currentSummaryAction = menu.addAction("插入当前会话摘要");
        describeInputAction(quickAction, "插入一句常用确认回复");
        describeInputAction(commandCardAction, "插入 /card 指令，发送时展开为我的 QQ 名片");
        describeInputAction(commandInviteAction, "插入 /invite 指令，发送时展开为入群邀请");
        describeInputAction(commandQqAction, "插入 /qq 指令，发送时展开为当前 QQ 号");
        describeInputAction(searchFriendAction, "插入一段引导对方通过 QQ 搜索加好友的话术");
        describeInputAction(addFriendAction, "插入面向当前会话对象的好友申请话术");
        describeInputAction(inviteGroupAction, "插入邀请对方加入当前群聊的话术");
        describeInputAction(quoteTemplateAction, "插入引用回复模板，方便补充上下文");
        describeInputAction(mentionAction, "打开 @ 成员菜单，插入群成员或在线成员提醒");
        describeInputAction(friendCardAction, "插入我的 QQ 名片到输入框");
        describeInputAction(groupCardAction, "插入当前私聊或群聊名片");
        describeInputAction(fileTemplateAction, "插入发送文件前的提醒话术");
        describeInputAction(imageTemplateAction, "插入发送图片前的提醒话术");
        describeInputAction(videoTemplateAction, "插入发送视频前的提醒话术");
        describeInputAction(groupInviteTemplateAction, "插入拉群邀请模板");
        describeInputAction(currentSummaryAction, "插入当前会话、账号和在线状态摘要");
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
        } else if (selected == quickAction) {
            ui->messageEdit->setPlainText("收到，我马上看。");
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入快捷语", 1400);
        } else if (selected == commandCardAction) {
            ui->messageEdit->setPlainText("/card");
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入快捷指令：/card", 1600);
        } else if (selected == commandInviteAction) {
            ui->messageEdit->setPlainText("/invite");
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入快捷指令：/invite", 1600);
        } else if (selected == commandQqAction) {
            ui->messageEdit->setPlainText("/qq");
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入快捷指令：/qq", 1600);
        } else if (selected == searchFriendAction) {
            ui->messageEdit->insertPlainText(QString("请在综合搜索里搜索 QQ:%1，确认资料后可以发送好友申请。").arg(m_currentUserId));
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入 QQ 搜索话术", 1400);
        } else if (selected == addFriendAction) {
            QString target = m_privateChatTarget.isEmpty() ? "你" : contactDisplayName(m_privateChatTarget);
            ui->messageEdit->insertPlainText(QString("%1，你好，我是 %2（QQ:%3），方便加个好友继续聊吗？").arg(target, m_currentUserName, m_currentUserId));
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入好友申请话术", 1400);
        } else if (selected == inviteGroupAction) {
            QString groupName = m_privateChatTarget.startsWith("local_group_") ? m_localGroupNames.value(m_privateChatTarget, "群聊") : "群聊";
            ui->messageEdit->insertPlainText(QString("我邀请你加入群聊“%1”，进群后可以一起聊天、发图片和传文件。").arg(groupName));
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入入群邀请话术", 1400);
        } else if (selected == quoteTemplateAction) {
            ui->messageEdit->insertPlainText("> 引用消息\n我的回复：");
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入引用模板", 1400);
        } else if (selected == mentionAction) {
            onInsertMention();
        } else if (selected == friendCardAction) {
            ui->messageEdit->insertPlainText(QString("我的QQ名片：%1（%2）").arg(m_currentUserId, m_currentUserName));
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入我的 QQ 名片", 1400);
        } else if (selected == groupCardAction) {
            QString card;
            if (m_privateChatTarget.startsWith("local_group_")) {
                card = QString("群聊名片：%1 QQ:%2").arg(m_localGroupNames.value(m_privateChatTarget, "群聊"), m_privateChatTarget.mid(QString("local_group_").size()));
            } else if (!m_privateChatTarget.isEmpty()) {
                card = QString("好友名片：%1 QQ:%2").arg(contactDisplayName(m_privateChatTarget), m_privateChatTarget);
            } else {
                card = QString("公共聊天室 当前QQ:%1").arg(m_currentUserId);
            }
            ui->messageEdit->insertPlainText(card);
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入当前会话名片", 1400);
        } else if (selected == fileTemplateAction) {
            QString target = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
            ui->messageEdit->insertPlainText(QString("我准备发一个文件到 %1，请注意查收。").arg(target));
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入发文件模板", 1400);
        } else if (selected == imageTemplateAction) {
            QString target = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
            ui->messageEdit->insertPlainText(QString("我准备发图片到 %1，发送后会显示预览卡片。").arg(target));
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入发图片模板", 1400);
        } else if (selected == videoTemplateAction) {
            QString target = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
            ui->messageEdit->insertPlainText(QString("我准备发视频到 %1，视频会以文件卡片形式发送。").arg(target));
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入发视频模板", 1400);
        } else if (selected == groupInviteTemplateAction) {
            QString target = m_privateChatTarget.startsWith("local_group_") ? m_localGroupNames.value(m_privateChatTarget, "群聊") : "群聊";
            ui->messageEdit->insertPlainText(QString("我想邀请你加入 %1，一起在群里沟通。").arg(target));
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入拉群模板", 1400);
        } else if (selected == currentSummaryAction) {
            QString summary;
            if (m_privateChatTarget.startsWith("local_group_")) {
                summary = QString("当前群聊：%1（群号:%2）· 成员%3人 · 我的QQ:%4")
                    .arg(m_localGroupNames.value(m_privateChatTarget, "群聊"),
                         m_privateChatTarget.mid(QString("local_group_").size()),
                         QString::number(m_localGroupMembers.value(m_privateChatTarget).size()),
                         m_currentUserId);
            } else if (!m_privateChatTarget.isEmpty()) {
                summary = QString("当前私聊：%1 · QQ:%2 · %3 · 我的QQ:%4")
                    .arg(contactDisplayName(m_privateChatTarget), m_privateChatTarget, isContactOnline(m_privateChatTarget) ? "在线" : "离线", m_currentUserId);
            } else {
                summary = QString("公共聊天室 · 我的QQ:%1 · 好友%2人 · 群聊%3个 · 在线成员%4人")
                    .arg(m_currentUserId).arg(m_friendIds.size()).arg(m_localGroupIds.size()).arg(m_knownUsers.size());
            }
            ui->messageEdit->insertPlainText(summary);
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入当前会话摘要", 1400);
        }
    });
    connect(ui->userListView, &QListView::doubleClicked, this, &MainWindow::onPrivateChat);
    connect(ui->userListView, &QListView::customContextMenuRequested, this, &MainWindow::onUserContextMenu);
    connect(ui->chatListView, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;

        const SavedFileActionState savedFileState = savedFileActionState(index);
        if (!savedFileState.hasSavePath) return;

        openSavedFileFromState(savedFileState, "保存文件不存在或无法打开");
    });
    connect(ui->chatListView, &QListView::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex index = ui->chatListView->indexAt(pos);
        if (!index.isValid()) return;
        QString text = index.data().toString();
        if (text.isEmpty()) return;
        QMenu menu(this);
        addChatContextAction(menu, "复制消息", "复制整条聊天记录，包括时间和发送者", "copy-message");
        addChatContextAction(menu, "只复制内容", "只复制消息正文内容", "copy-plain");
        addChatContextAction(menu, "复制发送者", "复制这条消息的发送者名称或账号", "copy-sender");
        addChatContextAction(menu, "引用回复", "把这条消息作为引用插入输入框", "quote");
        addChatContextAction(menu, "转发到输入框", "把消息正文整理成转发内容放入输入框", "forward");
        addChatContextAction(menu, "再次发送", "把消息正文重新填入输入框并立即发送", "resend");
        addChatContextAction(menu, "复制时间", "复制这条消息的发送时间", "copy-time");
        menu.addSeparator();
        const SavedFileActionState savedFileState = savedFileActionState(index);
        const bool isMediaMessage = isChatMediaMessage(text, savedFileState);
        QAction* copyMediaCardAction = addChatContextAction(menu, "复制媒体卡片", "复制当前媒体或文件消息的卡片摘要", "copy-media-card", isMediaMessage);
        QAction* copyFileNoticeAction = addChatContextAction(menu, "复制查收话术", "复制提醒对方查收文件的简短话术", "copy-file-notice", isMediaMessage);
        QAction* copyReceiptAction = addChatContextAction(menu, "复制回执话术", "复制已收到文件后的回执话术", "copy-receipt", isMediaMessage);
        QAction* copySavePathAction = addChatContextAction(menu, "复制保存路径", "复制收到文件在本机的保存路径", "copy-save-path");
        QAction* openSavedFileAction = addChatContextAction(menu, "打开文件", "打开这条记录关联的本地文件", "open-saved-file");
        QAction* openSaveFolderAction = addChatContextAction(menu, "打开保存目录", "打开这条记录关联文件所在目录", "open-save-folder");
        QAction* copyMediaFlowAction = addChatContextAction(menu, "复制媒体流程", "复制媒体发送、保存和回执的操作流程", "copy-media-flow", isMediaMessage);
        configureSavedFileActions(copySavePathAction, openSavedFileAction, openSaveFolderAction, savedFileState);
        menu.addSeparator();
        addChatContextAction(menu, "@对方回复", "把发送者作为 @ 回复对象插入输入框", "mention-reply");
        QAction* selected = menu.exec(ui->chatListView->viewport()->mapToGlobal(pos));
        if (!selected) return;
        handleChatContextCommand(selected->data().toString(), text, savedFileState);
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
    connect(ui->friendNoticeBtn, &QPushButton::clicked, this, &MainWindow::onShowFriendNotifications);
    connect(ui->groupNoticeBtn, &QPushButton::clicked, this, &MainWindow::onShowGroupNotifications);
    connect(ui->announcementTitleLabel, &QLabel::linkActivated, this, &MainWindow::onEditGroupAnnouncement);
    connect(ui->copyAccountBtn, &QPushButton::clicked, this, &MainWindow::onCopyAccount);
    ui->profileCard->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->profileCard, &QFrame::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        QAction* copyAccountAction = menu.addAction("复制QQ号");
        QAction* copyCardAction = menu.addAction("复制我的名片");
        QAction* copyStatusAction = menu.addAction("复制在线状态");
        QAction* copyProfileSummaryAction = menu.addAction("复制账号摘要");
        QAction* globalSearchAction = menu.addAction("打开综合搜索");
        QAction* friendManagerAction = menu.addAction("打开好友管理");
        auto describeProfileAction = [](QAction* action, const QString& tip) {
            action->setToolTip(tip);
            action->setStatusTip(tip);
        };
        describeProfileAction(copyAccountAction, "复制当前登录账号的 QQ 号");
        describeProfileAction(copyCardAction, "复制我的 QQ、昵称、好友数和群聊数");
        describeProfileAction(copyStatusAction, "复制当前在线状态和好友/群聊数量");
        describeProfileAction(copyProfileSummaryAction, "复制账号、当前会话和可用操作摘要");
        describeProfileAction(globalSearchAction, "打开综合搜索，查找 QQ、好友和群聊");
        describeProfileAction(friendManagerAction, "打开好友管理器，搜索、备注和整理好友");
        QAction* selected = menu.exec(ui->profileCard->mapToGlobal(pos));
        if (selected == copyAccountAction) {
            onCopyAccount();
        } else if (selected == copyCardAction) {
            QString card = QString("QQ:%1\n昵称:%2\n好友:%3\n群聊:%4")
                .arg(m_currentUserId, m_currentUserName, QString::number(m_friendIds.size()), QString::number(m_localGroupIds.size()));
            QApplication::clipboard()->setText(card);
            ui->statusbar->showMessage("我的 QQ 名片已复制", 2200);
        } else if (selected == copyStatusAction) {
            QString status = QString("QQ:%1 · %2 · 在线 · 好友%3 · 群聊%4")
                .arg(m_currentUserId, m_currentUserName)
                .arg(m_friendIds.size())
                .arg(m_localGroupIds.size());
            QApplication::clipboard()->setText(status);
            ui->statusbar->showMessage("在线状态已复制", 2200);
        } else if (selected == copyProfileSummaryAction) {
            QString summary = QString("账号摘要\nQQ:%1\n昵称:%2\n在线状态:在线\n好友:%3\n群聊:%4\n当前会话:%5\n可通过综合搜索发送好友申请或创建群聊")
                .arg(m_currentUserId,
                     m_currentUserName,
                     QString::number(m_friendIds.size()),
                     QString::number(m_localGroupIds.size()),
                     m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget));
            QApplication::clipboard()->setText(summary);
            ui->statusbar->showMessage("账号摘要已复制", 2200);
        } else if (selected == globalSearchAction) {
            onShowGlobalSearch();
        } else if (selected == friendManagerAction) {
            onShowFriendManager();
        }
    });
    connect(ui->addFriendBtn, &QPushButton::clicked, this, &MainWindow::onShowQuickAddFriend);
    connect(ui->friendManagerBtn, &QPushButton::clicked, this, &MainWindow::onShowFriendManager);
    connect(ui->groupChatBtn, &QPushButton::clicked, this, &MainWindow::onBackToGroupChat);
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
        if (!m_friendIds.contains(targetId)) {
            if (m_pendingOutgoingFriendRequests.contains(targetId)) {
                ui->statusbar->showMessage(QString("已向 %1 发送过好友申请，等待对方处理").arg(contactDisplayName(targetId)), 2500);
            } else if (m_client->sendFriendRequest(targetId)) {
                m_friendNames[targetId] = contactDisplayName(targetId);
                m_pendingOutgoingFriendRequests << targetId;
                refreshFriendList();
                refreshGroupMemberPanel();
                appendSystemMessage(QString("已向群成员发送好友申请 QQ:%1，等待对方同意").arg(targetId));
            } else {
                ui->statusbar->showMessage(QString("好友申请发送失败：%1").arg(contactDisplayName(targetId)), 3000);
            }
        }
        m_privateChatTarget = targetId;
        m_chatModel->clear();
        m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
        loadHistory(targetId);
        ui->chatTitleLabel->setText(QString("与 %1 私聊中").arg(contactDisplayName(targetId)));
        ui->chatHintLabel->setText(QString("QQ: %1 · %2 · 点击菜单“返回群聊”回到公共聊天室").arg(targetId, isContactOnline(targetId) ? "在线" : "离线"));
        refreshComposerState();
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
        const bool canManageGroup = isLocalGroup
            ? isCurrentUserGroupOwner(m_privateChatTarget)
            : canCurrentUserManageServerGroup("public");
        const QString ownerId = isLocalGroup ? groupOwnerId(m_privateChatTarget) : m_serverGroupOwners.value("public");
        QAction* chatAction = menu.addAction("私聊");
        QAction* copyAction = menu.addAction("复制QQ号");
        QAction* profileAction = menu.addAction("复制名片");
        QAction* copyAllAction = menu.addAction("复制群成员列表");
        QAction* copyOnlineAction = menu.addAction("复制在线群成员");
        QAction* renameAction = menu.addAction("设置备注");
        QAction* promoteAdminAction = nullptr;
        QAction* demoteAdminAction = nullptr;
        const QString serverTargetRole = isServerPublicGroup
            ? m_serverGroupMemberRoles.value("public|" + memberId).toLower()
            : QString();
        const bool canSetPublicAdmin = isServerPublicGroup
            && (m_serverGroupOwners.value("public") == m_currentUserId
                || m_serverGroupMemberRoles.value("public|" + m_currentUserId).toLower() == "owner")
            && memberId != ownerId;
        if (isServerPublicGroup) {
            promoteAdminAction = menu.addAction("设为管理员");
            demoteAdminAction = menu.addAction("取消管理员");
        }
        QAction* removeAction = menu.addAction("移出群聊");
        auto describeMemberAction = [](QAction* action, const QString& tip) {
            action->setToolTip(tip);
            action->setStatusTip(tip);
        };
        describeMemberAction(chatAction, "打开当前群成员的私聊；非好友会先尝试发送好友申请");
        describeMemberAction(copyAction, "复制当前群成员的 QQ 号");
        describeMemberAction(profileAction, "复制当前群成员的 QQ、昵称和所属群聊");
        describeMemberAction(copyAllAction, "复制当前群聊的全部成员列表");
        describeMemberAction(copyOnlineAction, "复制当前群聊在线成员的 QQ 和昵称");
        describeMemberAction(renameAction, "修改当前群成员在本地显示的备注名");
        if (promoteAdminAction) {
            describeMemberAction(promoteAdminAction, canSetPublicAdmin
                ? "由服务端校验群主权限，并把该公共群成员设为管理员"
                : "只有公共群群主可以设置管理员");
            promoteAdminAction->setEnabled(canSetPublicAdmin && serverTargetRole == "member");
        }
        if (demoteAdminAction) {
            describeMemberAction(demoteAdminAction, canSetPublicAdmin
                ? "由服务端校验群主权限，并取消该公共群成员的管理员角色"
                : "只有公共群群主可以取消管理员");
            demoteAdminAction->setEnabled(canSetPublicAdmin && serverTargetRole == "admin");
        }
        describeMemberAction(removeAction, canManageGroup
            ? (isLocalGroup ? "将当前成员从本地群聊成员列表中移除" : "通过服务端权限校验移出公共群成员")
            : (isLocalGroup ? "只有群主可以移出群成员" : "只有公共群群主或管理员可以移出成员"));
        removeAction->setEnabled(canManageGroup && memberId != ownerId);
        QAction* selected = menu.exec(ui->groupMemberListView->viewport()->mapToGlobal(pos));
        if (selected == chatAction) {
            if (!m_friendIds.contains(memberId)) {
                if (m_pendingOutgoingFriendRequests.contains(memberId)) {
                    ui->statusbar->showMessage(QString("已向 %1 发送过好友申请，等待对方处理").arg(contactDisplayName(memberId)), 2500);
                } else if (m_client->sendFriendRequest(memberId)) {
                    m_friendNames[memberId] = contactDisplayName(memberId);
                    m_pendingOutgoingFriendRequests << memberId;
                    refreshFriendList();
                    refreshGroupMemberPanel();
                    appendSystemMessage(QString("已向群成员发送好友申请 QQ:%1，等待对方同意").arg(memberId));
                } else {
                    ui->statusbar->showMessage(QString("好友申请发送失败：%1").arg(contactDisplayName(memberId)), 3000);
                }
            }
            m_privateChatTarget = memberId;
            m_chatModel->clear();
            m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
            loadHistory(memberId);
            ui->chatTitleLabel->setText(QString("与 %1 私聊中").arg(contactDisplayName(memberId)));
            ui->chatHintLabel->setText(QString("QQ: %1 · %2 · 点击菜单“返回群聊”回到公共聊天室").arg(memberId, isContactOnline(memberId) ? "在线" : "离线"));
            refreshComposerState();
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
        } else if (promoteAdminAction && selected == promoteAdminAction) {
            if (!canSetPublicAdmin) {
                ui->statusbar->showMessage("只有群主可以设置公共群管理员", 2400);
                return;
            }
            requestServerGroupMemberUpdate(memberId, "promote_admin");
        } else if (demoteAdminAction && selected == demoteAdminAction) {
            if (!canSetPublicAdmin) {
                ui->statusbar->showMessage("只有群主可以取消公共群管理员", 2400);
                return;
            }
            requestServerGroupMemberUpdate(memberId, "demote_admin");
        } else if (selected == removeAction) {
            if (!canManageGroup) {
                ui->statusbar->showMessage(isLocalGroup ? "只有群主可以移出群成员" : "只有群主或管理员可以移出公共群成员", 2400);
                return;
            }
            if (memberId == ownerId) {
                ui->statusbar->showMessage("群主不能被移出群聊", 2200);
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
        saveHistory(m_privateChatTarget, line);

        QStandardItem* item = new QStandardItem(line);
        item->setEditable(false);
        item->setForeground(QColor(20, 92, 160));
        item->setBackground(QColor(218, 241, 255));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
            ok = m_client->sendPrivateMessage(m_privateChatTarget, text);
        }
    } else {
        ok = m_client->sendMessage(text);
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
        item->setForeground(QColor(20, 92, 160));
        item->setBackground(QColor(218, 241, 255));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
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

    QString filePath;
    QFileInfo info;
    QString fileSize;
    if (!selectTransferFile("选择文件",
                            "常用文件 (*.txt *.pdf *.doc *.docx *.xls *.xlsx *.zip *.rar *.7z);;媒体文件 (*.png *.jpg *.jpeg *.gif *.bmp *.mp4 *.mov *.avi *.mkv *.wmv *.flv *.webm);;所有文件 (*.*)",
                            "文件",
                            "文件发送已取消",
                            "已取消选择文件",
                            &filePath,
                            &info,
                            &fileSize)) {
        return;
    }

    const TransferSendUiState preparingState = m_transferManager.preparingSendState(QStringLiteral("文件"), info.fileName(), fileSize, targetName);
    applyTransferSendState(preparingState);
    const QString completedAt = QDateTime::currentDateTime().toString("hh:mm:ss");
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        const TransferSendUiState completedState = m_transferManager.localSendCompletedState(QStringLiteral("文件"), info.fileName(), fileSize, targetName, completedAt);
        QString line = QString("[%1] <%2> 发送了文件: %3 · %4").arg(completedAt, m_currentUserName, info.fileName(), fileSize);
        saveHistory(m_privateChatTarget, line);
        QStandardItem* item = new QStandardItem(line);
        item->setEditable(false);
        item->setForeground(QColor(20, 92, 160));
        item->setBackground(QColor(218, 241, 255));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_chatModel->appendRow(item);
        appendTransferCompletionState(completedState, true, true, QColor(0, 121, 107), QColor(232, 248, 245));
        return;
    }

    QString transferSummary;
    bool transferCanceled = false;
    bool ok = sendTransferWithProgress(filePath, m_privateChatTarget, targetName, "文件", false, &transferSummary, &transferCanceled);
    updateSavedOutgoingTransferRecoveryUi(!ok && !transferCanceled);
    if (ok) {
        const TransferSendUiState completedState = m_transferManager.remoteSendCompletedState(QStringLiteral("文件"), info.fileName(), fileSize, targetName, QDateTime::currentDateTime().toString("hh:mm:ss"), transferSummary);
        appendTransferCompletionState(completedState, true, true, QColor(0, 121, 107), QColor(232, 248, 245));
    } else if (transferCanceled) {
        appendSystemMessage(QString("已取消发送文件: %1 · 到 %2").arg(info.fileName(), targetName));
        const TransferSendUiState state = m_transferManager.canceledSendState(QStringLiteral("文件"), info.fileName());
        ui->chatHintLabel->setText(QString("%1 · %2").arg(state.hintText, targetName));
        ui->statusbar->showMessage(state.statusMessage, state.statusTimeoutMs);
        refreshComposerState();
    } else {
        const TransferSendUiState state = m_transferManager.failedSendState(QStringLiteral("文件"), info.fileName(), fileSize, targetName);
        applyTransferSendState(state);
        QMessageBox::warning(this, state.warningTitle, state.warningMessage);
        refreshComposerState();
    }
}

void MainWindow::onSendImage() {
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    if (!ensureTransferTargetReady(QStringLiteral("图片/视频"), targetName, isLocalGroup)) {
        return;
    }

    QString filePath;
    QFileInfo info;
    QString fileSize;
    if (!selectTransferFile("选择图片或视频",
                            "图片和视频 (*.png *.jpg *.jpeg *.bmp *.gif *.mp4 *.mov *.avi *.mkv *.wmv *.flv *.webm);;图片文件 (*.png *.jpg *.jpeg *.bmp *.gif);;视频文件 (*.mp4 *.mov *.avi *.mkv *.wmv *.flv *.webm);;所有文件 (*.*)",
                            "媒体文件",
                            "图片/视频发送已取消",
                            "已取消选择图片/视频",
                            &filePath,
                            &info,
                            &fileSize)) {
        return;
    }

    const QString suffix = info.suffix().toLower();
    const bool isVideo = QStringList{"mp4", "mov", "avi", "mkv", "wmv", "flv", "webm"}.contains(suffix);
    const QString mediaType = isVideo ? "视频" : "图片";
    const TransferSendUiState preparingState = m_transferManager.preparingSendState(mediaType, info.fileName(), fileSize, targetName);
    applyTransferSendState(preparingState);
    const QString completedAt = QDateTime::currentDateTime().toString("hh:mm:ss");
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        const TransferSendUiState completedState = m_transferManager.localSendCompletedState(mediaType, info.fileName(), fileSize, targetName, completedAt);
        QPixmap pixmap(filePath);
        QString line = QString("[%1] <%2> [%3] %4 · %5").arg(completedAt, m_currentUserName, mediaType, info.fileName(), fileSize);
        saveHistory(m_privateChatTarget, line);
        QStandardItem* item = new QStandardItem(line);
        item->setEditable(false);
        item->setForeground(QColor(20, 92, 160));
        item->setBackground(QColor(218, 241, 255));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_chatModel->appendRow(item);
        if (!isVideo && !pixmap.isNull()) {
            appendMediaPreviewItem(QString("%1 · %2").arg(info.fileName(), fileSize), pixmap, false, false);
        } else if (isVideo) {
            appendMediaPreviewItem(QString("视频文件 · %1 · %2 · 可在文件目录中打开").arg(info.fileName(), fileSize), QPixmap(), true, false);
        }
        appendTransferCompletionState(completedState, true, false, QColor(), QColor());
        return;
    }

    QString transferSummary;
    bool transferCanceled = false;
    bool ok = sendTransferWithProgress(filePath, m_privateChatTarget, targetName, mediaType, !isVideo, &transferSummary, &transferCanceled);
    updateSavedOutgoingTransferRecoveryUi(!ok && !transferCanceled);
    if (ok) {
        const TransferSendUiState completedState = m_transferManager.remoteSendCompletedState(mediaType, info.fileName(), fileSize, targetName, QDateTime::currentDateTime().toString("hh:mm:ss"), transferSummary);
        appendSystemMessage(completedState.systemMessage);
        if (!isVideo) {
            QPixmap pixmap(filePath);
            if (!pixmap.isNull()) {
                appendMediaPreviewItem(completedState.cardText, pixmap, false, true);
            }
        } else {
            appendMediaPreviewItem(completedState.cardText, QPixmap(), true, true);
        }
        appendTransferCompletionState(completedState, false, false, QColor(), QColor());
    } else if (transferCanceled) {
        appendSystemMessage(QString("已取消发送%1: %2 · 到 %3").arg(mediaType, info.fileName(), targetName));
        const TransferSendUiState state = m_transferManager.canceledSendState(mediaType, info.fileName());
        ui->chatHintLabel->setText(QString("%1 · %2").arg(state.hintText, targetName));
        ui->statusbar->showMessage(state.statusMessage, state.statusTimeoutMs);
        refreshComposerState();
    } else {
        const TransferSendUiState state = m_transferManager.failedSendState(mediaType, info.fileName(), fileSize, targetName);
        applyTransferSendState(state);
        QMessageBox::warning(this, state.warningTitle, state.warningMessage);
        refreshComposerState();
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

    if (msg.type == MessageType::Image && !msg.fileData.isEmpty()) {
        const ReceivedTransferContext context = receivedTransferContext(msg,
                                                                        QStringLiteral("图片"),
                                                                        QStringLiteral("received_image"),
                                                                        QStringLiteral("Images"),
                                                                        displayName);
        QPixmap pixmap;
        if (pixmap.loadFromData(msg.fileData)) {
            appendMediaPreviewItem(QString("%1 · %2%3").arg(context.receivedName, context.receivedSize, context.manifestSuffix), pixmap, false, false);
        }
        persistReceivedTransferPayload(context,
                                       displayName,
                                       msg.transferId,
                                       msg.fileData,
                                       msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
    } else if (msg.type == MessageType::File && !msg.fileData.isEmpty()) {
        const ReceivedTransferContext context = receivedTransferContext(msg,
                                                                        QStringLiteral("文件"),
                                                                        QStringLiteral("received_file"),
                                                                        QStringLiteral("Files"),
                                                                        displayName);
        persistReceivedTransferPayload(context,
                                       displayName,
                                       msg.transferId,
                                       msg.fileData,
                                       msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
    }

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
    m_serverGroupAuditEvents.clear();
    m_removedServerGroups.clear();

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
        }
        m_serverGroupMembers[groupId] = memberIds;
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
}

void MainWindow::onClientDisconnected() {
    appendSystemMessage("已断开服务器连接");
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    const ConnectionUiState state = ChatSessionManager::disconnectedState(targetName, isLocalGroup);
    ui->chatHintLabel->setText(state.hintText);
    ui->statusbar->showMessage(state.statusMessage, 3500);
    refreshComposerState();
}

void MainWindow::onClientError(const QString& error) {
    appendSystemMessage("连接错误: " + error);
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    const ConnectionUiState state = ChatSessionManager::errorState(targetName, isLocalGroup, error);
    ui->chatHintLabel->setText(state.hintText);
    ui->statusbar->showMessage(state.statusMessage, 3500);
    refreshComposerState();
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

    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();
    const QString safeSessionName = sessionName.simplified().replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    const QString defaultPath = QDir(defaultDir).filePath(QString("QtNetworkChat_%1_%2.txt")
        .arg(safeSessionName, QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")));
    const QString savePath = QFileDialog::getSaveFileName(this, "导出聊天记录", defaultPath, "文本文件 (*.txt);;所有文件 (*.*)");
    if (savePath.isEmpty()) {
        ui->statusbar->showMessage("已取消导出聊天记录", 1600);
        return;
    }

    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "导出失败", "无法写入导出文件，请检查保存位置权限。");
        ui->statusbar->showMessage("聊天记录导出失败", 2600);
        return;
    }

    QTextStream out(&file);
    out << "QtNetworkChat 聊天记录导出\n";
    out << "会话: " << sessionName << "\n";
    out << "账号: " << m_currentUserId << " / " << m_currentUserName << "\n";
    out << "导出时间: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
    out << "记录数: " << rows.size() << "\n\n";
    for (const QString& row : rows) {
        out << row << "\n";
    }
    file.close();

    ui->statusbar->showMessage(QString("已导出 %1 条聊天记录").arg(rows.size()), 2600);
    appendSystemMessage(QString("已导出 %1 的聊天记录：%2").arg(sessionName, savePath));
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
        QLabel#globalActionHint {
            color: #6B7A88;
            font-size: 13px;
            font-weight: 700;
        }
        QLabel#globalStatsLabel {
            color: #1296F7;
            font-size: 12px;
            font-weight: 800;
            padding-left: 10px;
        }
        QLabel#globalPreviewLabel {
            color: #3A4A5A;
            background: #EAF7FF;
            border-radius: 12px;
            font-size: 12px;
            font-weight: 800;
            padding: 5px 10px;
        }
        QPushButton#globalSearchGhostBtn {
            min-width: 76px;
            min-height: 36px;
            background: white;
            color: #3A4A5A;
            border: 1px solid #D4E1EC;
            border-radius: 10px;
            font-weight: 700;
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
        if (!m_friendIds.contains(id) && !m_pendingOutgoingFriendRequests.contains(id)) {
            const QString displayName = contactDisplayName(id);
            if (m_client->sendFriendRequest(id)) {
                m_friendNames[id] = displayName;
                m_pendingOutgoingFriendRequests << id;
                refreshFriendList();
                appendSystemMessage(QString("已从综合搜索向 %1（QQ:%2）发送好友申请").arg(displayName, id));
            } else {
                ui->statusbar->showMessage(QString("好友申请发送失败：%1").arg(displayName), 3000);
            }
        } else if (m_pendingOutgoingFriendRequests.contains(id)) {
            ui->statusbar->showMessage(QString("%1 的好友申请正在等待确认").arg(contactDisplayName(id)), 2200);
        }
        dialog.accept();
        m_privateChatTarget = id;
        m_chatModel->clear();
        m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
        loadHistory(id);
        ui->chatTitleLabel->setText(QString("与 %1 私聊中").arg(contactDisplayName(id)));
        ui->chatHintLabel->setText(QString("QQ: %1 · %2 · 点击菜单“返回群聊”回到公共聊天室").arg(id, isContactOnline(id) ? "在线" : "离线"));
        refreshComposerState();
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
        QString groupId = "local_group_" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
        m_localGroupIds << groupId;
        m_localGroupNames[groupId] = groupName;
        m_localGroupAnnouncements[groupId] = QString("%1 已创建，可继续邀请好友并发送消息。").arg(groupName);
        m_localGroupMembers[groupId] = QStringList{m_currentUserId};
        saveLocalGroups();
        refreshFriendList();
        dialog.accept();
        switchToLocalGroup(groupId, groupName);
        appendSystemMessage("已从搜索创建群聊: " + groupName);
    });
    connect(inviteVisibleBtn, &QPushButton::clicked, &dialog, [this, resultList, searchEdit, &dialog]() {
        QString groupName = searchEdit->text().trimmed();
        if (groupName.isEmpty()) groupName = "搜索群聊";
        QString groupId = "local_group_" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
        QStringList members = QStringList{m_currentUserId};
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
        int invitedCount = qMax(0, members.size() - 1);
        if (invitedCount == 0) {
            ui->statusbar->showMessage("当前没有可邀请的可见用户", 2200);
            return;
        }
        if (QMessageBox::question(&dialog,
                                  "可见用户建群",
                                  QString("确定创建群聊“%1”并邀请 %2 位可见用户吗？其中 %3 位会同时发送好友申请。")
                                      .arg(groupName)
                                      .arg(invitedCount)
                                      .arg(requestIds.size()),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage("已取消可见用户建群", 1600);
            return;
        }
        QStringList sentNames;
        QStringList failedNames;
        for (const QString& id : requestIds) {
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
        m_localGroupIds << groupId;
        m_localGroupNames[groupId] = groupName;
        m_localGroupAnnouncements[groupId] = QString("%1 已从综合搜索创建，已邀请可见用户。").arg(groupName);
        m_localGroupMembers[groupId] = members;
        saveLocalGroups();
        refreshFriendList();
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
        if (QMessageBox::question(&dialog,
                                  "发送可见用户申请",
                                  QString("确定向 %1 位可见用户发送好友申请吗？").arg(addIds.size()),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage("已取消发送可见用户申请", 1600);
            searchEdit->setFocus();
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
    connect(copyListBtn, &QPushButton::clicked, &dialog, [this, resultList]() {
        QStringList rows;
        for (int i = 0; i < resultList->count(); ++i) {
            QListWidgetItem* item = resultList->item(i);
            QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty()) continue;
            if (id.startsWith("search_add:")) {
                rows << QString("搜索申请 QQ:%1").arg(id.mid(QString("search_add:").size()));
            } else if (id.startsWith("local_group_")) {
                rows << QString("群聊 QQ:%1 名称:%2").arg(id.mid(QString("local_group_").size()), m_localGroupNames.value(id, "群聊"));
            } else {
                rows << QString("QQ:%1 昵称:%2 状态:%3").arg(id, contactDisplayName(id), isContactOnline(id) ? "在线" : "离线");
            }
        }
        if (rows.isEmpty()) {
            ui->statusbar->showMessage("当前搜索结果没有可复制条目", 2200);
            return;
        }
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage(QString("已复制 %1 条搜索结果").arg(rows.size()), 2200);
    });
    connect(copyAddTextBtn, &QPushButton::clicked, &dialog, [this, resultList, searchEdit]() {
        QListWidgetItem* item = resultList->currentItem();
        QString id = item ? item->data(Qt::UserRole).toString() : searchEdit->text().trimmed();
        if (id.startsWith("search_add:")) id = id.mid(QString("search_add:").size());
        QString name = id.startsWith("local_group_") ? m_localGroupNames.value(id, "群聊") : contactDisplayName(id);
        if (id.isEmpty()) id = searchEdit->text().trimmed();
        if (name.isEmpty()) name = id;
        QString text = id.startsWith("local_group_")
            ? QString("我想邀请你加入群聊 %1，一起在群里沟通。").arg(name)
            : QString("你好，我是 %1（QQ:%2），通过 QQ 搜索找到你，方便加个好友吗？").arg(m_currentUserName, m_currentUserId);
        QApplication::clipboard()->setText(text);
        ui->statusbar->showMessage("申请/邀请话术已复制", 2200);
    });
    connect(copyInviteCardBtn, &QPushButton::clicked, &dialog, [this, resultList, searchEdit]() {
        QListWidgetItem* item = resultList->currentItem();
        QString id = item ? item->data(Qt::UserRole).toString() : searchEdit->text().trimmed();
        if (id.startsWith("search_add:")) id = id.mid(QString("search_add:").size());
        if (id.isEmpty()) id = searchEdit->text().trimmed();
        QString title;
        QString detail;
        if (id.startsWith("local_group_")) {
            title = QString("邀请加入群聊：%1").arg(m_localGroupNames.value(id, "群聊"));
            detail = QString("群号:%1 · 成员:%2 · 邀请人:%3(QQ:%4)")
                .arg(id.mid(QString("local_group_").size()), QString::number(m_localGroupMembers.value(id).size()), m_currentUserName, m_currentUserId);
        } else {
            title = QString("好友邀请：%1").arg(id.isEmpty() ? "QQ搜索" : contactDisplayName(id));
            detail = QString("目标QQ:%1 · 我的QQ:%2 · 昵称:%3 · 可搜索后直接添加").arg(id, m_currentUserId, m_currentUserName);
        }
        QApplication::clipboard()->setText(title + '\n' + detail);
        ui->statusbar->showMessage("搜索邀请卡已复制", 2200);
    });
    connect(copySearchCardBtn, &QPushButton::clicked, &dialog, [this, resultList, searchEdit]() {
        QString keyword = searchEdit->text().trimmed();
        QStringList rows;
        rows << "综合搜索卡片";
        rows << QString("关键词:%1").arg(keyword.isEmpty() ? "全部" : keyword);
        rows << QString("我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        int userCount = 0;
        int friendCount = 0;
        int groupCount = 0;
        for (int i = 0; i < resultList->count(); ++i) {
            QListWidgetItem* item = resultList->item(i);
            QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty()) continue;
            if (id.startsWith("search_add:")) {
                rows << QString("继续搜索申请 QQ:%1").arg(id.mid(QString("search_add:").size()));
            } else if (id.startsWith("local_group_")) {
                ++groupCount;
                rows << QString("群聊 QQ:%1 名称:%2 成员:%3")
                    .arg(id.mid(QString("local_group_").size()), m_localGroupNames.value(id, "群聊"), QString::number(m_localGroupMembers.value(id).size()));
            } else {
                bool isFriend = m_friendIds.contains(id);
                if (isFriend) ++friendCount; else ++userCount;
                rows << QString("%1 QQ:%2 昵称:%3 状态:%4")
                    .arg(isFriend ? "好友" : "用户", id, contactDisplayName(id), isContactOnline(id) ? "在线" : "离线");
            }
        }
        rows << QString("匹配好友:%1 · 可申请用户:%2 · 群聊:%3")
            .arg(friendCount)
            .arg(userCount)
            .arg(groupCount);
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("综合搜索卡片已复制", 2200);
    });
    connect(copySearchMediaPackBtn, &QPushButton::clicked, &dialog, [this, resultList, searchEdit]() {
        QListWidgetItem* item = resultList->currentItem();
        QString id = item ? item->data(Qt::UserRole).toString() : searchEdit->text().trimmed();
        if (id.startsWith("search_add:")) id = id.mid(QString("search_add:").size());
        QString keyword = searchEdit->text().trimmed();
        QString targetName = keyword.isEmpty() ? "全部搜索结果" : keyword;
        QString targetId = id;
        QString relation = "搜索结果";
        if (id.startsWith("local_group_")) {
            targetName = m_localGroupNames.value(id, "群聊");
            targetId = id.mid(QString("local_group_").size());
            relation = "群聊";
        } else if (!id.isEmpty()) {
            targetName = contactDisplayName(id);
            relation = m_friendIds.contains(id) ? "好友" : "可申请用户";
        }
        QStringList rows;
        rows << QString("综合搜索媒体包 · 目标:%1 · QQ:%2 · 类型:%3").arg(targetName, targetId.isEmpty() ? "批量搜索" : targetId, relation);
        rows << QString("关键词:%1 · 我的QQ:%2 · 昵称:%3").arg(keyword.isEmpty() ? "全部" : keyword, m_currentUserId, m_currentUserName);
        rows << "可先打开/申请搜索结果，再发送图片/视频或闪传文件";
        rows << "支持 png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm 和常用文档压缩包";
        rows << QString("邀请话术：你好，我是 %1（QQ:%2），通过综合搜索找到你，可以通过好友申请或进群后收发媒体文件。").arg(m_currentUserName, m_currentUserId);
        rows << QString("查收话术：我已准备发送媒体文件到 %1，请注意查收。").arg(targetName);
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("综合搜索媒体包已复制", 2200);
    });
    connect(copyBatchMediaPlanBtn, &QPushButton::clicked, &dialog, [this, resultList, searchEdit]() {
        QStringList users;
        QStringList groups;
        int friendCount = 0;
        int addableCount = 0;
        for (int i = 0; i < resultList->count(); ++i) {
            QListWidgetItem* item = resultList->item(i);
            QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty()) continue;
            if (id.startsWith("search_add:")) {
                users << QString("待搜索QQ:%1").arg(id.mid(QString("search_add:").size()));
            } else if (id.startsWith("local_group_")) {
                groups << QString("%1(群号:%2,成员:%3)").arg(m_localGroupNames.value(id, "群聊"), id.mid(QString("local_group_").size()), QString::number(m_localGroupMembers.value(id).size()));
            } else {
                bool isFriend = m_friendIds.contains(id);
                if (isFriend) ++friendCount; else ++addableCount;
                users << QString("%1(QQ:%2,%3,%4)").arg(contactDisplayName(id), id, isFriend ? "好友" : "可申请", isContactOnline(id) ? "在线" : "离线");
            }
        }
        QString keyword = searchEdit->text().trimmed();
        QStringList rows;
        rows << QString("综合搜索批量媒体计划 · 关键词:%1").arg(keyword.isEmpty() ? "全部" : keyword);
        rows << QString("我的QQ:%1 · 昵称:%2 · 好友结果:%3 · 可申请:%4 · 群聊:%5")
            .arg(m_currentUserId, m_currentUserName, QString::number(friendCount), QString::number(addableCount), QString::number(groups.size()));
        rows << QString("用户目标:%1").arg(users.isEmpty() ? "无" : users.join("、"));
        rows << QString("群聊目标:%1").arg(groups.isEmpty() ? "无" : groups.join("、"));
        rows << "1. 先打开好友或群聊结果，未成为好友先发送申请或复制申请话术";
        rows << "2. 图片/GIF/视频走图片视频入口，文档和压缩包走闪传文件";
        rows << "3. 发送后右键聊天记录复制媒体流程、查收话术、回执和保存路径";
        rows << "4. 可见用户建群后可统一发送群媒体文件";
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("综合搜索批量媒体计划已复制", 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this]() {
        QStringList rows;
        rows << QString("上传指南 · 我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << "图片/视频：支持 png、jpg、gif、mp4、mov、avi、mkv、wmv、flv、webm";
        rows << "闪传文件：支持文档、压缩包和媒体文件";
        rows << "聊天记录右键：可复制媒体卡片和查收话术";
        rows << QString("当前会话:%1").arg(m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget));
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("综合搜索上传指南已复制", 2200);
    });
    connect(copyOnlineBtn, &QPushButton::clicked, &dialog, [this, resultList]() {
        QStringList rows;
        for (int i = 0; i < resultList->count(); ++i) {
            QListWidgetItem* item = resultList->item(i);
            QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty() || id.startsWith("search_add:") || id.startsWith("local_group_") || !isContactOnline(id)) continue;
            rows << QString("在线搜索结果 QQ:%1 昵称:%2 关系:%3")
                .arg(id, contactDisplayName(id), m_friendIds.contains(id) ? "好友" : "可申请");
        }
        if (rows.isEmpty()) {
            ui->statusbar->showMessage("当前搜索结果没有在线用户", 2200);
            return;
        }
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage(QString("已复制 %1 个在线搜索结果").arg(rows.size()), 2200);
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

void MainWindow::onShowCreateMenu() {
    QMenu menu(this);
    QAction* createGroupAction = menu.addAction("创建群聊");
    QAction* createGroupWithFriendsAction = menu.addAction("创建群并拉全部好友");
    QAction* addFriendAction = menu.addAction("申请好友/群");
    QAction* focusSearchAction = menu.addAction("定位QQ搜索框");
    QAction* refreshContactsAction = menu.addAction("刷新联系人");
    QAction* clearSearchAction = menu.addAction("清空搜索");
    QAction* copyChatIdAction = menu.addAction("复制当前会话号");
    QAction* copyChatCardAction = menu.addAction("复制当前会话名片");
    QAction* copyCurrentInviteAction = menu.addAction("复制当前邀请语");
    QAction* copyCurrentMembersAction = menu.addAction("复制当前成员列表");
    QAction* copyCurrentOnlineAction = menu.addAction("复制当前在线成员");
    QAction* copyAllContactsAction = menu.addAction("复制全部联系人");
    QAction* copySearchSummaryAction = menu.addAction("复制搜索摘要");
    QAction* copyQuickGuideAction = menu.addAction("复制QQ功能指南");
    QAction* copyMediaGuideAction = menu.addAction("复制上传指南");
    QAction* copyCurrentMediaPackAction = menu.addAction("复制当前媒体包");
    QAction* copyFullMediaPlanAction = menu.addAction("复制完整媒体计划");
    QAction* editAnnouncementAction = menu.addAction("编辑群公告");
    QAction* copyAnnouncementAction = menu.addAction("复制群公告");
    QAction* friendNoticeAction = menu.addAction("好友通知");
    QAction* groupNoticeAction = menu.addAction("群通知");
    QAction* sendImageAction = menu.addAction("发送图片/视频");
    QAction* sendFileAction = menu.addAction("闪传文件");
    auto describeAction = [](QAction* action, const QString& tip) {
        action->setToolTip(tip);
        action->setStatusTip(tip);
    };
    describeAction(createGroupAction, "创建一个新的本地群聊并立即进入");
    describeAction(createGroupWithFriendsAction, "创建群聊并自动邀请当前全部好友");
    describeAction(addFriendAction, "打开好友申请窗口，搜索 QQ 号并发送申请");
    describeAction(focusSearchAction, "把焦点定位到左侧 QQ 搜索框");
    describeAction(refreshContactsAction, "重新加载好友、群聊和在线联系人列表");
    describeAction(clearSearchAction, "清空联系人搜索条件");
    describeAction(copyChatIdAction, "复制当前私聊 QQ 号或群聊号");
    describeAction(copyChatCardAction, "复制当前会话的名称、账号和成员摘要");
    describeAction(copyCurrentInviteAction, "复制当前会话可用的邀请话术");
    describeAction(copyCurrentMembersAction, "复制当前群聊的成员列表");
    describeAction(copyCurrentOnlineAction, "复制当前群聊在线成员列表");
    describeAction(copyAllContactsAction, "复制全部好友、群聊和在线成员摘要");
    describeAction(copySearchSummaryAction, "复制当前搜索条件和联系人统计");
    describeAction(copyQuickGuideAction, "复制 QQ 搜索、好友、群聊和媒体操作指南");
    describeAction(copyMediaGuideAction, "复制图片、视频和文件上传指南");
    describeAction(copyCurrentMediaPackAction, "复制当前会话的媒体发送准备包");
    describeAction(copyFullMediaPlanAction, "复制好友、群聊和媒体发送的完整计划");
    describeAction(editAnnouncementAction, "编辑当前本地群聊公告");
    describeAction(copyAnnouncementAction, "复制当前本地群聊公告");
    describeAction(friendNoticeAction, "打开好友通知并处理好友申请");
    describeAction(groupNoticeAction, "打开群通知并查看群聊、公告和邀请");
    describeAction(sendImageAction, "选择图片或视频发送到当前会话");
    describeAction(sendFileAction, "选择文件闪传到当前会话");
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
    } else if (selected == createGroupWithFriendsAction) {
        QString groupName = ui->contactSearchEdit->text().trimmed();
        if (groupName.isEmpty()) groupName = "好友群聊";
        QString groupId = "local_group_" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
        QStringList members = QStringList{m_currentUserId};
        for (const QString& friendId : m_friendIds) {
            if (!members.contains(friendId)) members << friendId;
        }
        m_localGroupIds << groupId;
        m_localGroupNames[groupId] = groupName;
        m_localGroupAnnouncements[groupId] = QString("%1 已创建，已自动邀请全部好友。").arg(groupName);
        m_localGroupMembers[groupId] = members;
        saveLocalGroups();
        refreshFriendList();
        switchToLocalGroup(groupId, groupName);
        appendSystemMessage(QString("已创建群聊并邀请 %1 位好友").arg(qMax(0, members.size() - 1)));
        saveHistory(groupId, QString("[%1] [系统] 已创建群聊并邀请 %2 位好友").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(qMax(0, members.size() - 1)));
    } else if (selected == addFriendAction) {
        onShowGlobalSearch();
    } else if (selected == focusSearchAction) {
        ui->contactSearchEdit->setFocus();
        ui->contactSearchEdit->selectAll();
        ui->statusbar->showMessage("已定位到 QQ 搜索框，输入账号后回车自动查找", 2500);
    } else if (selected == refreshContactsAction) {
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->statusbar->showMessage("联系人和群成员已刷新", 2000);
    } else if (selected == clearSearchAction) {
        ui->contactSearchEdit->clear();
        ui->memberSearchEdit->clear();
        m_contactFilter.clear();
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->statusbar->showMessage("搜索条件已清空", 1800);
    } else if (selected == copyChatIdAction) {
        QString chatId = m_privateChatTarget;
        if (chatId.startsWith("local_group_")) chatId = chatId.mid(QString("local_group_").size());
        if (chatId.isEmpty()) chatId = m_currentUserId;
        QApplication::clipboard()->setText(chatId);
        ui->statusbar->showMessage("当前会话号已复制: " + chatId, 2500);
    } else if (selected == copyChatCardAction) {
        QString card;
        if (m_privateChatTarget.startsWith("local_group_")) {
            card = QString("群聊 QQ:%1\n%2\n公告:%3")
                .arg(m_privateChatTarget.mid(QString("local_group_").size()),
                     m_localGroupNames.value(m_privateChatTarget, "群聊"),
                     m_localGroupAnnouncements.value(m_privateChatTarget, ui->announcementBodyLabel->text()));
        } else if (!m_privateChatTarget.isEmpty()) {
            card = QString("QQ:%1\n昵称:%2\n状态:%3").arg(m_privateChatTarget, contactDisplayName(m_privateChatTarget), isContactOnline(m_privateChatTarget) ? "在线" : "离线");
        } else {
            card = QString("公共聊天室\n当前账号:%1\n在线成员:%2").arg(m_currentUserId).arg(m_knownUsers.size());
        }
        QApplication::clipboard()->setText(card);
        ui->statusbar->showMessage("当前会话名片已复制", 1800);
    } else if (selected == copyCurrentInviteAction) {
        QString text;
        if (m_privateChatTarget.startsWith("local_group_")) {
            text = QString("我邀请你加入群聊“%1”（群号:%2）。我是 %3（QQ:%4），进群后我们一起沟通。")
                .arg(m_localGroupNames.value(m_privateChatTarget, "群聊"),
                     m_privateChatTarget.mid(QString("local_group_").size()),
                     m_currentUserName,
                     m_currentUserId);
        } else if (!m_privateChatTarget.isEmpty()) {
            text = QString("你好 %1，我是 %2（QQ:%3）。方便的话加个好友，我们可以继续私聊。")
                .arg(contactDisplayName(m_privateChatTarget), m_currentUserName, m_currentUserId);
        } else {
            text = QString("你好，我是 %1（QQ:%2），欢迎加入公共聊天室，也可以通过 QQ 搜索加我好友。").arg(m_currentUserName, m_currentUserId);
        }
        QApplication::clipboard()->setText(text);
        ui->statusbar->showMessage("当前会话邀请语已复制", 2200);
    } else if (selected == copyCurrentMembersAction) {
        QStringList cards;
        QStringList ids = m_privateChatTarget.startsWith("local_group_") ? m_localGroupMembers.value(m_privateChatTarget) : QStringList();
        if (ids.isEmpty()) {
            for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) ids << it.key();
        }
        for (const QString& id : ids) {
            cards << QString("QQ:%1 昵称:%2 状态:%3").arg(id, contactDisplayName(id), isContactOnline(id) || id == m_currentUserId ? "在线" : "离线");
        }
        if (cards.isEmpty()) {
            ui->statusbar->showMessage("当前会话没有成员可复制", 2200);
            return;
        }
        QApplication::clipboard()->setText(cards.join('\n'));
        ui->statusbar->showMessage(QString("已复制 %1 个当前成员").arg(cards.size()), 2200);
    } else if (selected == copyCurrentOnlineAction) {
        QStringList cards;
        QStringList ids = m_privateChatTarget.startsWith("local_group_") ? m_localGroupMembers.value(m_privateChatTarget) : QStringList();
        if (ids.isEmpty()) {
            for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) ids << it.key();
        }
        for (const QString& id : ids) {
            if (id != m_currentUserId && !isContactOnline(id)) continue;
            cards << QString("在线 QQ:%1 昵称:%2").arg(id, contactDisplayName(id));
        }
        if (cards.isEmpty()) {
            ui->statusbar->showMessage("当前会话没有在线成员可复制", 2200);
            return;
        }
        QApplication::clipboard()->setText(cards.join('\n'));
        ui->statusbar->showMessage(QString("已复制 %1 个在线成员").arg(cards.size()), 2200);
    } else if (selected == copyAllContactsAction) {
        QStringList rows;
        rows << QString("我的QQ:%1 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QString("好友:%1 群聊:%2 在线:%3").arg(m_friendIds.size()).arg(m_localGroupIds.size()).arg(m_knownUsers.size());
        for (const QString& id : m_friendIds) {
            rows << QString("好友 QQ:%1 昵称:%2 状态:%3").arg(id, contactDisplayName(id), isContactOnline(id) ? "在线" : "离线");
        }
        for (const QString& groupId : m_localGroupIds) {
            rows << QString("群聊 QQ:%1 名称:%2 成员:%3").arg(groupId.mid(QString("local_group_").size()), m_localGroupNames.value(groupId, "群聊"), QString::number(m_localGroupMembers.value(groupId).size()));
        }
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage(QString("已复制联系人摘要 %1 行").arg(rows.size()), 2200);
    } else if (selected == copySearchSummaryAction) {
        QString filter = ui->contactSearchEdit->text().trimmed();
        if (filter.isEmpty()) filter = "全部";
        QString summary = QString("QQ搜索:%1\n好友:%2\n本地群:%3\n在线成员:%4\n当前会话:%5")
            .arg(filter)
            .arg(m_friendIds.size())
            .arg(m_localGroupIds.size())
            .arg(m_knownUsers.size())
            .arg(m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget));
        QApplication::clipboard()->setText(summary);
        ui->statusbar->showMessage("QQ 搜索摘要已复制", 2200);
    } else if (selected == copyQuickGuideAction) {
        QStringList rows;
        rows << QString("我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << "1. 点击综合搜索可按 QQ 号/昵称查找用户、好友和群聊";
        rows << "2. 搜索结果可直接打开、发送好友申请、复制名片或复制邀请卡";
        rows << "3. 好友申请支持推荐在线用户、复制申请话术和自动发送申请";
        rows << "4. 好友管理器可搜索、备注、邀入群、复制在线好友和统计";
        rows << QString("当前好友:%1 · 群聊:%2 · 在线:%3").arg(m_friendIds.size()).arg(m_localGroupIds.size()).arg(m_knownUsers.size());
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("QQ 功能指南已复制", 2200);
    } else if (selected == copyMediaGuideAction) {
        QStringList rows;
        rows << QString("上传指南 · 我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << "1. 点击 图片/视频 可发送 png、jpg、gif、mp4、mov、avi、mkv、wmv、flv、webm";
        rows << "2. 图片会显示预览卡片，视频会以文件卡片发送";
        rows << "3. 点击 闪传文件 可发送文档、压缩包和媒体文件";
        rows << "4. 聊天记录右键可复制媒体卡片或查收话术";
        rows << QString("当前会话:%1").arg(m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget));
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("上传指南已复制", 2200);
    } else if (selected == copyCurrentMediaPackAction) {
        QString sessionName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
        QString sessionId = m_privateChatTarget;
        if (sessionId.startsWith("local_group_")) sessionId = sessionId.mid(QString("local_group_").size());
        if (sessionId.isEmpty()) sessionId = "public";
        QStringList rows;
        rows << QString("媒体发送包 · 会话:%1 · 会话号:%2").arg(sessionName, sessionId);
        rows << QString("发送者:%1 · QQ:%2").arg(m_currentUserName, m_currentUserId);
        rows << "图片/视频入口：点击工具栏 图片/视频，或菜单栏 发送图片/视频";
        rows << "文件入口：点击工具栏 闪传文件，或菜单栏 闪传文件";
        rows << "支持格式：png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm + 文档/压缩包";
        rows << QString("查收话术：我已准备发送图片/视频/文件到 %1，请注意查收。").arg(sessionName);
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("当前媒体发送包已复制", 2200);
    } else if (selected == copyFullMediaPlanAction) {
        QString sessionName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
        QString sessionId = m_privateChatTarget;
        if (sessionId.startsWith("local_group_")) sessionId = sessionId.mid(QString("local_group_").size());
        if (sessionId.isEmpty()) sessionId = "public";
        QStringList rows;
        rows << QString("完整媒体计划 · 当前会话:%1 · 会话号:%2").arg(sessionName, sessionId);
        rows << QString("我的QQ:%1 · 昵称:%2 · 好友:%3 · 群聊:%4 · 在线:%5")
            .arg(m_currentUserId, m_currentUserName, QString::number(m_friendIds.size()), QString::number(m_localGroupIds.size()), QString::number(m_knownUsers.size()));
        rows << "1. 先用综合搜索/好友申请确认目标 QQ 或群聊";
        rows << "2. 通过好友管理/群通知复制媒体包、邀请语和成员列表";
        rows << "3. 点击 图片/视频 发送图片、GIF 或视频；点击 闪传文件 发送文档和压缩包";
        rows << "4. 发送后聊天记录会生成媒体卡片、查收话术；接收后生成回执话术和保存路径";
        rows << QString("当前查收话术：我已准备发送媒体文件到 %1，请注意查收。").arg(sessionName);
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("完整媒体计划已复制", 2200);
    } else if (selected == editAnnouncementAction) {
        onEditGroupAnnouncement();
    } else if (selected == copyAnnouncementAction) {
        QString announcement = ui->announcementBodyLabel->text();
        QApplication::clipboard()->setText(announcement);
        ui->statusbar->showMessage("群公告已复制", 1800);
    } else if (selected == friendNoticeAction) {
        onShowFriendNotifications();
    } else if (selected == groupNoticeAction) {
        onShowGroupNotifications();
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
    setWindowTitle(appWindowTitle(QString("群聊: %1").arg(groupName)));
    ui->chatTitleLabel->setText(groupName);
    const QString ownerId = groupOwnerId(groupId);
    const bool isOwner = isCurrentUserGroupOwner(groupId);
    ui->chatHintLabel->setText(ChatSessionManager::localGroupHint(groupId, ownerId, isOwner));
    ui->announcementTitleLabel->setText(isOwner ? "群公告 <a href=\"edit\">编辑</a>" : "群公告");
    ui->announcementBodyLabel->setText(m_localGroupAnnouncements.value(groupId, QString("%1 已创建，可继续邀请好友并发送消息。").arg(groupName)));
    refreshGroupMemberPanel();
    refreshComposerState();
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
    QString text = QInputDialog::getMultiLineText(
        this,
        "编辑群公告",
        "群公告内容:",
        oldText,
        &ok).trimmed();
    if (!ok) {
        ui->statusbar->showMessage("已取消编辑群公告", 1600);
        return;
    }
    bool usedDefaultAnnouncement = false;
    if (text.isEmpty()) {
        if (isLocalGroup) {
            text = QString("%1 已创建，可继续邀请好友并发送消息。").arg(ui->chatTitleLabel->text().trimmed().isEmpty() ? "群聊" : ui->chatTitleLabel->text().trimmed());
        } else {
            text = "欢迎来到公共聊天室，支持 QQ 号搜索、好友、私聊和文件发送。";
        }
        usedDefaultAnnouncement = true;
    }
    if (text == oldText) {
        ui->statusbar->showMessage(usedDefaultAnnouncement ? "群公告已是默认内容" : "群公告未改变", 1600);
        return;
    }
    if (isServerPublicGroup) {
        if (!m_client || !m_client->sendServerGroupAnnouncementUpdate("public", text)) {
            ui->statusbar->showMessage("群公告提交失败，请检查连接状态", 2400);
            appendSystemMessage("群公告提交失败：客户端未连接或发送失败");
            return;
        }
        appendSystemMessage("群公告更新已提交，等待服务端同步");
        ui->statusbar->showMessage(usedDefaultAnnouncement ? "群公告为空，已提交默认公告" : "群公告更新已提交", 2200);
        return;
    }
    ui->announcementBodyLabel->setText(text);
    if (isLocalGroup) {
        m_localGroupAnnouncements[m_privateChatTarget] = text;
        saveLocalGroups();
        saveHistory(m_privateChatTarget, QString("[%1] [系统] 群公告已更新: %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), text));
    }
    appendSystemMessage("群公告已更新");
    ui->statusbar->showMessage(usedDefaultAnnouncement ? "群公告为空，已使用默认公告" : "群公告已更新", 2200);
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
    const QMap<QString, QString> commands = {
        {"/card", "发送我的QQ名片"},
        {"/invite", "发送好友申请/入群邀请"},
        {"/qq", "发送我的QQ号"},
        {"/summary", "发送当前会话摘要"},
        {"/file", "发送文件查收话术"},
        {"/image", "发送图片预览话术"},
        {"/video", "发送视频查收话术"},
        {"名片", "中文名片快捷语"},
        {"邀请", "中文邀请快捷语"},
        {"QQ", "中文QQ号快捷语"},
        {"摘要", "中文会话摘要"},
        {"文件", "中文文件查收话术"},
        {"图片", "中文图片预览话术"},
        {"视频", "中文视频查收话术"}
    };
    for (auto it = commands.begin(); it != commands.end(); ++it) {
        QAction* action = commandMenu->addAction(QString("%1 · %2").arg(it.key(), it.value()));
        QString command = it.key();
        connect(action, &QAction::triggered, this, [this, command]() {
            ui->messageEdit->setPlainText(command);
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage(QString("已插入快捷指令：%1").arg(command), 1600);
        });
    }
    const QStringList quickMessages = {
        "在吗？",
        "收到，我马上看。",
        "稍等一下",
        "我发你文件",
        "我们群里说",
        "你好，我是通过 QQ 搜索找到你的。",
        "方便的话加个好友。",
        "我建了群聊，拉大家一起沟通。"
    };
    for (const QString& message : quickMessages) {
        QAction* action = menu.addAction("快捷语 · " + message);
        connect(action, &QAction::triggered, this, [this, message]() {
            ui->messageEdit->setPlainText(message);
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("已插入快捷语", 1400);
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
        ui->statusbar->showMessage("已插入 @全体成员", 1400);
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
            ui->statusbar->showMessage(QString("已插入 @%1").arg(name), 1400);
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
        QString filter = accountEdit->text().trimmed();
        int onlineCandidates = 0;
        int pendingCandidates = 0;
        int visibleCount = 0;
        QString firstPreviewId;
        QString firstPreviewName;
        for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
            const ChatUser& user = it.value();
            if (user.id == m_currentUserId || m_friendIds.contains(user.id)) continue;
            const bool matches = filter.isEmpty()
                || user.id.contains(filter, Qt::CaseInsensitive)
                || user.name.contains(filter, Qt::CaseInsensitive);
            if (m_pendingOutgoingFriendRequests.contains(user.id)) {
                if (matches) ++pendingCandidates;
                continue;
            }
            ++onlineCandidates;
            if (!matches) continue;
            if (visibleCount < 5) {
                QListWidgetItem* item = new QListWidgetItem(QString("QQ:%1 · %2 · 在线 · 双击添加").arg(user.id, user.name));
                item->setData(Qt::UserRole, user.id);
                item->setSizeHint(QSize(0, 34));
                suggestionList->addItem(item);
            }
            if (firstPreviewId.isEmpty()) {
                firstPreviewId = user.id;
                firstPreviewName = user.name;
            }
            ++visibleCount;
        }
        QString statsText = filter.isEmpty()
            ? QString("在线推荐 %1 人 · 已有好友 %2 人").arg(onlineCandidates).arg(m_friendIds.size())
            : QString("匹配推荐 %1 人 · 输入回车可搜索 QQ:%2").arg(visibleCount).arg(filter);
        if (pendingCandidates > 0) {
            statsText += QString(" · 申请中 %1 人").arg(pendingCandidates);
        }
        statsLabel->setText(statsText);
        if (visibleCount > 5) {
            QListWidgetItem* moreItem = new QListWidgetItem(QString("还有 %1 位匹配用户，可缩小关键词继续筛选").arg(visibleCount - 5));
            moreItem->setFlags(Qt::NoItemFlags);
            moreItem->setForeground(QColor(135, 150, 165));
            moreItem->setSizeHint(QSize(0, 34));
            suggestionList->addItem(moreItem);
        }
        if (suggestionList->count() == 0) {
            QListWidgetItem* item = new QListWidgetItem(filter.isEmpty() ? "输入 QQ 号后回车搜索申请" : QString("回车搜索并发送申请 QQ:%1").arg(filter));
            item->setData(Qt::UserRole, filter.isEmpty() ? QString() : filter);
            item->setForeground(QColor(135, 150, 165));
            item->setSizeHint(QSize(0, 34));
            suggestionList->addItem(item);
        }
        for (int i = 0; i < suggestionList->count(); ++i) {
            QListWidgetItem* item = suggestionList->item(i);
            if (item->flags().testFlag(Qt::ItemIsEnabled) && !item->data(Qt::UserRole).toString().isEmpty()) {
                suggestionList->setCurrentRow(i);
                break;
            }
        }
        QString previewId = firstPreviewId.isEmpty() ? filter : firstPreviewId;
        QString previewName = firstPreviewName.isEmpty() ? (previewId.isEmpty() ? "待搜索好友" : contactDisplayName(previewId)) : firstPreviewName;
        cardLabel->setText(QString("邀请预览：%1（QQ:%2）\n你好，我是 %3（QQ:%4），方便加个好友吗？")
            .arg(previewName, previewId.isEmpty() ? "-" : previewId, m_currentUserName, m_currentUserId));
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
        QLabel#quickAddStats {
            min-height: 24px;
            border-radius: 12px;
            background: #EAF7FF;
            color: #1296F7;
            font-size: 12px;
            font-weight: 800;
            padding: 2px 10px;
        }
        QLabel#quickAddPreviewCard {
            min-height: 54px;
            border-radius: 14px;
            background: #F6FBFF;
            border: 1px solid #DCEFFF;
            color: #3A4A5A;
            font-size: 12px;
            font-weight: 700;
            padding: 8px 12px;
        }
        QLabel#quickAddActionTip {
            min-height: 28px;
            border-radius: 14px;
            background: #FFF8E8;
            border: 1px solid #FFE1A8;
            color: #A36800;
            font-size: 12px;
            font-weight: 800;
            padding: 5px 10px;
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
        QListWidget#quickAddSuggestionList {
            background: #F8FBFE;
            border: 1px solid #E4EEF6;
            border-radius: 12px;
            padding: 4px;
            outline: none;
        }
        QListWidget#quickAddSuggestionList::item {
            border-radius: 8px;
            padding: 4px 8px;
            color: #3A4A5A;
        }
        QListWidget#quickAddSuggestionList::item:selected, QListWidget#quickAddSuggestionList::item:hover {
            background: #EAF7FF;
            color: #1296F7;
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
    connect(copySearchCardBtn, &QPushButton::clicked, &dialog, [this, accountEdit, suggestionList]() {
        QString keyword = accountEdit->text().trimmed();
        QStringList rows;
        rows << QString("好友申请搜索卡片");
        rows << QString("关键词:%1").arg(keyword.isEmpty() ? "推荐好友" : keyword);
        rows << QString("我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        for (int i = 0; i < suggestionList->count(); ++i) {
            QListWidgetItem* item = suggestionList->item(i);
            QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty()) continue;
            rows << QString("候选 QQ:%1 昵称:%2 状态:%3").arg(id, contactDisplayName(id), isContactOnline(id) ? "在线" : "待搜索");
        }
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("好友申请搜索卡片已复制", 2200);
    });
    connect(copyFriendMediaPackBtn, &QPushButton::clicked, &dialog, [this, accountEdit, suggestionList]() {
        QString target = accountEdit->text().trimmed();
        if (target.isEmpty()) {
            QListWidgetItem* item = suggestionList->currentItem();
            if (item) target = item->data(Qt::UserRole).toString();
        }
        QString targetName = target.isEmpty() ? "待搜索好友" : contactDisplayName(target);
        QStringList rows;
        rows << QString("好友媒体包 · 目标:%1 · QQ:%2").arg(targetName, target.isEmpty() ? "待搜索" : target);
        rows << QString("我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << "通过好友申请后可直接发送图片/视频，也可使用闪传文件";
        rows << "支持 png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm 和文档压缩包";
        rows << QString("申请话术：%1，你好，我是 %2（QQ:%3），通过好友申请后我可以把图片/视频/文件发给你。").arg(targetName, m_currentUserName, m_currentUserId);
        rows << QString("查收话术：我已发送媒体文件给 %1，请注意查收。").arg(targetName);
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("好友媒体包已复制", 2200);
    });
    connect(copyAddChecklistBtn, &QPushButton::clicked, &dialog, [this, accountEdit, suggestionList]() {
        QString target = accountEdit->text().trimmed();
        if (target.isEmpty()) {
            QListWidgetItem* item = suggestionList->currentItem();
            if (item) target = item->data(Qt::UserRole).toString();
        }
        QString targetName = target.isEmpty() ? "待搜索好友" : contactDisplayName(target);
        QStringList rows;
        rows << QString("好友申请清单 · 目标:%1 · QQ:%2").arg(targetName, target.isEmpty() ? "待搜索" : target);
        rows << QString("我的QQ:%1 · 昵称:%2 · 已有好友:%3").arg(m_currentUserId, m_currentUserName, QString::number(m_friendIds.size()));
        rows << "1. 输入或选择 QQ 账号，先确认昵称和在线状态";
        rows << "2. 点击搜索申请，或复制申请话术发给对方";
        rows << "3. 通过后可发送图片/GIF/视频，也可闪传文件和压缩包";
        rows << "4. 发送后在聊天记录右键复制媒体卡片、查收话术和回执";
        rows << QString("申请话术：%1，你好，我是 %2（QQ:%3），方便通过好友申请后收发图片视频和文件吗？").arg(targetName, m_currentUserName, m_currentUserId);
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("好友申请清单已复制", 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this, accountEdit]() {
        QString target = accountEdit->text().trimmed();
        QStringList rows;
        rows << QString("好友申请上传指南 · 我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QString("目标QQ:%1").arg(target.isEmpty() ? "待搜索好友" : target);
        rows << "通过好友申请后可直接发送图片/视频，也可用闪传文件发送文档和压缩包";
        rows << "支持 mp4、mov、avi、mkv、wmv、flv、webm，聊天记录可复制媒体卡片";
        QApplication::clipboard()->setText(rows.join('\n'));
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
        int onlineCount = 0;
        int offlineCount = 0;
        int visibleCount = 0;
        for (const QString& id : m_friendIds) {
            QString name = m_friendNames.value(id, id);
            bool online = isContactOnline(id);
            if (online) {
                ++onlineCount;
            } else {
                ++offlineCount;
            }
            if (!filter.isEmpty()
                && !id.contains(filter, Qt::CaseInsensitive)
                && !name.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QString state = online ? "在线" : "离线";
            QListWidgetItem* item = new QListWidgetItem(QString("QQ:%1\n%2 · %3").arg(id, name, state));
            item->setData(Qt::UserRole, id);
            item->setSizeHint(QSize(0, 58));
            friendList->addItem(item);
            ++visibleCount;
        }
        subTitleLabel->setText(QString("当前 QQ：%1 · 好友 %2 人 · 可见 %3 人").arg(m_currentUserId).arg(m_friendIds.size()).arg(visibleCount));
        statsLabel->setText(QString("在线 %1 · 离线 %2 · 本地群 %3").arg(onlineCount).arg(offlineCount).arg(m_localGroupIds.size()));
        if (friendList->count() == 0) {
            QListWidgetItem* emptyItem = new QListWidgetItem(filter.isEmpty() ? "暂无好友，点击下方发送好友申请" : QString("未找到好友，双击搜索并发送申请 QQ:%1").arg(filter));
            emptyItem->setData(Qt::UserRole, filter.isEmpty() ? QString() : "search_add:" + filter);
            emptyItem->setForeground(QColor(135, 150, 165));
            friendList->addItem(emptyItem);
        }
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
        QLabel#managerStats {
            color: white;
            background: rgba(255, 255, 255, 35);
            border-radius: 12px;
            padding: 3px 10px;
            font-size: 12px;
            font-weight: 800;
        }
        QLabel#managerSelectionPreview {
            color: #3A4A5A;
            background: #EAF7FF;
            border: 1px solid #DCEFFF;
            border-radius: 14px;
            padding: 7px 12px;
            font-size: 12px;
            font-weight: 800;
        }
        QLabel#managerOperationGuide {
            color: #7A5310;
            background: #FFF8E8;
            border: 1px solid #FFE1A8;
            border-radius: 14px;
            padding: 7px 12px;
            font-size: 12px;
            font-weight: 800;
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
        m_privateChatTarget = id;
        m_chatModel->clear();
        m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
        loadHistory(id);
        ui->chatTitleLabel->setText(QString("与 %1 私聊中").arg(contactDisplayName(id)));
        ui->chatHintLabel->setText(QString("QQ: %1 · %2 · 点击菜单“返回群聊”回到公共聊天室").arg(id, isContactOnline(id) ? "在线" : "离线"));
        refreshComposerState();
        ui->statusbar->showMessage(QString("已打开与 %1 的私聊").arg(contactDisplayName(id)), 1800);
    };

    auto updateSelectionPreview = [this, friendList, selectionPreviewLabel]() {
        QListWidgetItem* selected = friendList->currentItem();
        if (!selected) {
            selectionPreviewLabel->setText("选择好友后可复制名片、邀请语或邀入群");
            return;
        }
        QString id = selected->data(Qt::UserRole).toString();
        if (id.startsWith("search_add:")) {
            selectionPreviewLabel->setText(QString("未找到好友，可搜索并发送申请 QQ:%1").arg(id.mid(QString("search_add:").size())));
        } else if (!id.isEmpty()) {
            selectionPreviewLabel->setText(QString("%1 · QQ:%2 · %3 · %4")
                .arg(contactDisplayName(id), id, isContactOnline(id) ? "在线" : "离线", m_privateChatTarget.startsWith("local_group_") ? "可邀入当前群" : "可发起私聊"));
        } else {
            selectionPreviewLabel->setText("输入 QQ 号或昵称可搜索好友");
        }
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
    connect(copyAllBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        QStringList cards;
        const QStringList visibleIds = visibleFriendManagerIds(friendList);
        for (const QString& id : visibleIds) {
            cards << QString("QQ:%1 昵称:%2 状态:%3").arg(id, contactDisplayName(id), isContactOnline(id) ? "在线" : "离线");
        }
        if (cards.isEmpty()) {
            ui->statusbar->showMessage("当前筛选没有可复制好友", 2200);
            return;
        }
        copyTextWithStatus(cards.join('\n'), QString("已复制 %1 个可见好友").arg(cards.size()), 2200);
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
    connect(copyOnlineBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        QStringList rows;
        const QStringList visibleIds = visibleFriendManagerIds(friendList);
        for (const QString& id : visibleIds) {
            if (!isContactOnline(id)) continue;
            rows << QString("在线好友 QQ:%1 昵称:%2").arg(id, contactDisplayName(id));
        }
        if (rows.isEmpty()) {
            ui->statusbar->showMessage("当前筛选没有在线好友", 2200);
            return;
        }
        copyTextWithStatus(rows.join('\n'), QString("已复制 %1 个在线好友").arg(rows.size()), 2200);
    });
    connect(copySearchCardBtn, &QPushButton::clicked, &dialog, [this, friendList, searchEdit]() {
        QString keyword = searchEdit->text().trimmed();
        QStringList rows;
        rows << "好友管理搜索卡片";
        rows << QString("关键词:%1").arg(keyword.isEmpty() ? "全部好友" : keyword);
        rows << QString("我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        int visibleCount = 0;
        for (int i = 0; i < friendList->count(); ++i) {
            QListWidgetItem* item = friendList->item(i);
            QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty()) continue;
            if (id.startsWith("search_add:")) {
                rows << QString("可搜索申请 QQ:%1").arg(id.mid(QString("search_add:").size()));
                continue;
            }
            rows << QString("好友 QQ:%1 昵称:%2 状态:%3")
                .arg(id, contactDisplayName(id), isContactOnline(id) ? "在线" : "离线");
            ++visibleCount;
        }
        rows << QString("可见好友:%1 · 全部好友:%2 · 群聊:%3")
            .arg(visibleCount)
            .arg(m_friendIds.size())
            .arg(m_localGroupIds.size());
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("好友管理搜索卡片已复制", 2200);
    });
    connect(copyFriendMediaPackBtn, &QPushButton::clicked, &dialog, [this, friendList, searchEdit]() {
        QString id = selectedFriendManagerTargetId(friendList);
        if (id.isEmpty()) id = searchEdit->text().trimmed();
        QString targetName = id.isEmpty() ? "可见好友" : contactDisplayName(id);
        QStringList rows;
        rows << QString("好友媒体包 · 目标:%1 · QQ:%2").arg(targetName, id.isEmpty() ? "批量可见" : id);
        rows << QString("我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QString("当前筛选:%1 · 全部好友:%2").arg(searchEdit->text().trimmed().isEmpty() ? "全部好友" : searchEdit->text().trimmed()).arg(m_friendIds.size());
        rows << "可先发起私聊，再点击 图片/视频 或 闪传文件 发送媒体";
        rows << "支持 png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm 和常用文档压缩包";
        rows << QString("邀请话术：%1，你好，我是 %2（QQ:%3），我可以发图片/视频/文件给你，请注意查收。").arg(targetName, m_currentUserName, m_currentUserId);
        rows << QString("回执话术：已收到来自 %1 的媒体文件，保存后我会尽快查看。").arg(m_currentUserName);
        copyTextWithStatus(rows.join('\n'), "好友管理媒体包已复制", 2200);
    });
    connect(copyBatchMediaPlanBtn, &QPushButton::clicked, &dialog, [this, friendList, searchEdit]() {
        QStringList targets;
        int onlineCount = 0;
        int offlineCount = 0;
        const QStringList visibleIds = visibleFriendManagerIds(friendList);
        for (const QString& id : visibleIds) {
            if (isContactOnline(id)) ++onlineCount; else ++offlineCount;
            targets << QString("%1(QQ:%2,%3)").arg(contactDisplayName(id), id, isContactOnline(id) ? "在线" : "离线");
        }
        QString keyword = searchEdit->text().trimmed();
        QStringList rows;
        rows << QString("好友批量媒体计划 · 筛选:%1").arg(keyword.isEmpty() ? "全部好友" : keyword);
        rows << QString("我的QQ:%1 · 昵称:%2 · 可见:%3 · 在线:%4 · 离线:%5")
            .arg(m_currentUserId, m_currentUserName, QString::number(targets.size()), QString::number(onlineCount), QString::number(offlineCount));
        rows << QString("目标列表:%1").arg(targets.isEmpty() ? "无可见好友" : targets.join("、"));
        rows << "1. 先给在线好友发图片/视频，离线好友复制查收话术";
        rows << "2. 大文件用闪传文件，图片/GIF/视频用图片视频入口";
        rows << "3. 发送后在聊天记录右键复制媒体流程、查收话术和回执";
        rows << "4. 可按筛选关键词分批发送，避免漏掉目标好友";
        copyTextWithStatus(rows.join('\n'), "好友批量媒体计划已复制", 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this, friendList, searchEdit]() {
        QString keyword = searchEdit->text().trimmed();
        QStringList rows;
        rows << QString("好友管理上传指南 · 我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QString("当前筛选:%1").arg(keyword.isEmpty() ? "全部好友" : keyword);
        const int visibleCount = visibleFriendManagerIds(friendList).size();
        rows << QString("可见好友:%1 · 全部好友:%2").arg(visibleCount).arg(m_friendIds.size());
        rows << "可向好友发送图片/视频，或用闪传文件发送文档、压缩包和媒体文件";
        rows << "聊天记录右键可复制媒体卡片和查收话术";
        copyTextWithStatus(rows.join('\n'), "好友管理上传指南已复制", 2200);
    });
    connect(remarkBtn, &QPushButton::clicked, &dialog, [this, friendList, fillList, searchEdit]() {
        QString id;
        if (!trySelectedValidFriendId(friendList, ui->statusbar, "请先选择要备注的好友", &id)) {
            return;
        }
        bool ok = false;
        const QString oldRemark = contactDisplayName(id);
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
            QString groupName = "我的群聊";
            QString groupId = "local_group_" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
            m_localGroupIds << groupId;
            m_localGroupNames[groupId] = groupName;
            m_localGroupAnnouncements[groupId] = QString("%1 已创建，可继续邀请好友并发送消息。").arg(groupName);
            m_localGroupMembers[groupId] = QStringList{m_currentUserId};
        }
        QString targetGroup = m_privateChatTarget.startsWith("local_group_") ? m_privateChatTarget : m_localGroupIds.last();
        if (m_localGroupMembers[targetGroup].contains(friendId)) {
            ui->statusbar->showMessage(QString("%1 已在目标群聊中").arg(contactDisplayName(friendId)), 1800);
            return;
        }
        m_localGroupMembers[targetGroup] << friendId;
        saveLocalGroups();
        refreshFriendList();
        switchToLocalGroup(targetGroup, m_localGroupNames.value(targetGroup, "群聊"));
        refreshGroupMemberPanel();
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
        if (QMessageBox::question(this,
                                  "邀请可见好友",
                                  QString("确定邀请 %1 位可见好友加入群聊“%2”吗？").arg(inviteIds.size()).arg(groupName),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage("已取消邀请可见好友", 1600);
            return;
        }
        if (willCreateGroup) {
            QString groupName = "好友群聊";
            m_localGroupIds << targetGroup;
            m_localGroupNames[targetGroup] = groupName;
            m_localGroupAnnouncements[targetGroup] = QString("%1 已创建，可继续邀请好友并发送消息。").arg(groupName);
            m_localGroupMembers[targetGroup] = QStringList{m_currentUserId};
        }
        for (const QString& friendId : inviteIds) {
            if (!m_localGroupMembers[targetGroup].contains(friendId)) {
                m_localGroupMembers[targetGroup] << friendId;
            }
        }
        saveLocalGroups();
        refreshFriendList();
        switchToLocalGroup(targetGroup, m_localGroupNames.value(targetGroup, "群聊"));
        refreshGroupMemberPanel();
        appendSystemMessage(QString("已邀请 %1 位可见好友加入群聊").arg(inviteIds.size()));
        saveHistory(targetGroup, QString("[%1] [系统] 已邀请 %2 位可见好友加入群聊").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(inviteIds.size()));
    });
    connect(deleteBtn, &QPushButton::clicked, &dialog, [this, friendList, fillList, searchEdit, &dialog]() {
        QString id;
        if (!trySelectedValidFriendId(friendList, ui->statusbar, "请先选择要删除的好友", &id)) {
            return;
        }
        QString displayName = contactDisplayName(id);
        if (QMessageBox::question(&dialog,
                                  "删除好友",
                                  QString("确定删除好友“%1”（QQ:%2）吗？删除后可重新搜索并发送申请。").arg(displayName, id),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage("已取消删除好友", 1600);
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
    QString filePath = QFileDialog::getOpenFileName(this, "选择头像", lastAvatarDirectory(), "图片 (*.png *.jpg *.jpeg *.bmp *.gif)");
    if (filePath.isEmpty()) {
        ui->statusbar->showMessage("已取消选择头像", 1600);
        return;
    }
    rememberAvatarDirectory(filePath);

    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        QMessageBox::warning(this, "头像上传失败", "请选择一个可读取的本地图片文件。");
        ui->statusbar->showMessage("头像上传失败：文件不可读取", 2200);
        return;
    }
    if (info.size() <= 0) {
        QMessageBox::warning(this, "头像上传失败", "图片文件为空，请重新选择。");
        ui->statusbar->showMessage("头像上传失败：图片文件为空", 2200);
        return;
    }
    constexpr qint64 maxAvatarBytes = 10LL * 1024 * 1024;
    if (info.size() > maxAvatarBytes) {
        QMessageBox::warning(this,
                             "头像过大",
                             QString("头像图片大小为 %1，超过 10 MB 上限，请选择更小的图片。")
                                 .arg(humanFileSize(info.size())));
        ui->statusbar->showMessage("头像上传失败：图片超过 10 MB", 2200);
        return;
    }

    QPixmap pixmap(filePath);
    if (pixmap.isNull()) {
        QMessageBox::warning(this, "头像上传失败", "无法读取该图片，请确认文件格式是否正确。");
        ui->statusbar->showMessage("头像上传失败：无法读取图片", 2200);
        return;
    }

    QPixmap savedAvatar = squareAvatarPixmap(pixmap, 256);
    if (savedAvatar.isNull() || !savedAvatar.save(getAvatarFilePath(), "PNG")) {
        QMessageBox::warning(this, "头像保存失败", "头像已读取，但保存到本地失败，请检查应用数据目录权限。");
        ui->statusbar->showMessage("头像保存失败，请检查应用数据目录权限", 2600);
        return;
    }

    ui->avatarLabel->setPixmap(squareAvatarPixmap(savedAvatar, ui->avatarLabel->width()));
    saveProfileToSqlite();
    QString detail = QString("头像已更新 · %1 · %2 · 已保存到本地").arg(info.fileName(), humanFileSize(info.size()));
    QString avatarTip = QString("当前头像：%1 · %2；点击“换头像”重新选择")
                            .arg(info.fileName(), humanFileSize(info.size()));
    ui->avatarLabel->setToolTip(avatarTip);
    ui->uploadAvatarBtn->setToolTip(avatarTip);
    appendSystemMessage(detail);
    ui->chatHintLabel->setText(detail);
    ui->statusbar->showMessage(detail, 2600);
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
    auto describeUserAction = [](QAction* action, const QString& tip) {
        if (!action) return;
        action->setToolTip(tip);
        action->setStatusTip(tip);
    };
    if (m_localGroupIds.contains(userId)) {
        QAction* openGroupAction = menu.addAction("进入群聊");
        QAction* copyGroupAction = menu.addAction("复制群号");
        QAction* copyGroupCardAction = menu.addAction("复制群名片");
        QAction* copyGroupInviteAction = menu.addAction("复制群邀请语");
        QAction* copyMembersAction = menu.addAction("复制成员列表");
        QAction* inviteFriendAction = menu.addAction("邀请好友");
        QAction* inviteByAccountAction = menu.addAction("按QQ号邀请");
        QAction* inviteAllAction = menu.addAction("邀请全部好友");
        QAction* copyOnlineMembersAction = menu.addAction("复制在线成员");
        QAction* renameGroupAction = menu.addAction("重命名群聊");
        QAction* deleteGroupAction = menu.addAction("删除群聊");
        describeUserAction(openGroupAction, "进入当前本地群聊并加载聊天记录");
        describeUserAction(copyGroupAction, "复制当前群聊的群号");
        describeUserAction(copyGroupCardAction, "复制群名、群号、成员数和公告摘要");
        describeUserAction(copyGroupInviteAction, "复制一段可发送给好友的入群邀请语");
        describeUserAction(copyMembersAction, "复制当前群聊的全部成员列表");
        describeUserAction(inviteFriendAction, "从好友列表中选择一个好友邀请入群");
        describeUserAction(inviteByAccountAction, "输入 QQ 号邀请用户入群，并按需发送好友申请");
        describeUserAction(inviteAllAction, "把当前全部好友批量邀请进该群聊");
        describeUserAction(copyOnlineMembersAction, "复制当前群聊在线成员的 QQ 和昵称");
        describeUserAction(renameGroupAction, "修改当前本地群聊名称");
        describeUserAction(deleteGroupAction, "删除当前本地群聊配置，聊天记录不会在此步骤删除");
        QAction* selected = menu.exec(ui->userListView->viewport()->mapToGlobal(pos));
        if (!selected) return;
        if (selected == openGroupAction) {
            switchToLocalGroup(userId, m_localGroupNames.value(userId, "群聊"));
        } else if (selected == copyGroupAction) {
            QString groupNumber = userId.mid(QString("local_group_").size());
            QApplication::clipboard()->setText(groupNumber);
            ui->statusbar->showMessage("群号已复制: " + groupNumber, 2500);
        } else if (selected == copyGroupCardAction) {
            QString groupNumber = userId.mid(QString("local_group_").size());
            QString card = QString("群聊 QQ:%1\n%2\n成员:%3\n公告:%4")
                .arg(groupNumber,
                     m_localGroupNames.value(userId, "群聊"),
                     QString::number(m_localGroupMembers.value(userId).size()),
                     m_localGroupAnnouncements.value(userId, QString("%1 已创建，可继续邀请好友并发送消息。").arg(m_localGroupNames.value(userId, "群聊"))));
            QApplication::clipboard()->setText(card);
            ui->statusbar->showMessage("群名片已复制", 1800);
        } else if (selected == copyGroupInviteAction) {
            QString groupName = m_localGroupNames.value(userId, "群聊");
            QString groupNumber = userId.mid(QString("local_group_").size());
            QString inviteText = QString("我邀请你加入群聊“%1”（群号:%2）。我是 %3（QQ:%4），进群后我们一起沟通。").arg(groupName, groupNumber, m_currentUserName, m_currentUserId);
            QApplication::clipboard()->setText(inviteText);
            ui->statusbar->showMessage("群邀请语已复制", 2200);
        } else if (selected == copyMembersAction) {
            QStringList cards;
            for (const QString& id : m_localGroupMembers.value(userId)) {
                cards << QString("QQ:%1 昵称:%2 状态:%3").arg(id, contactDisplayName(id), isContactOnline(id) || id == m_currentUserId ? "在线" : "离线");
            }
            if (cards.isEmpty()) {
                ui->statusbar->showMessage("当前群聊没有成员可复制", 2200);
                return;
            }
            QApplication::clipboard()->setText(cards.join('\n'));
            ui->statusbar->showMessage(QString("已复制 %1 个群成员").arg(cards.size()), 2200);
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
        } else if (selected == inviteByAccountAction) {
            bool ok = false;
            QString account = QInputDialog::getText(this, "按QQ号邀请", "输入 QQ 账号:", QLineEdit::Normal, QString(), &ok).trimmed();
            if (!ok) return;
            if (account.isEmpty()) {
                ui->statusbar->showMessage("请输入 QQ 号后再邀请入群", 1800);
                return;
            }
            if (account == m_currentUserId) {
                ui->statusbar->showMessage("你已在当前群聊中，无需重复邀请", 1800);
                return;
            }
            if (m_localGroupMembers[userId].contains(account)) {
                ui->statusbar->showMessage("该 QQ 已在当前群聊中", 1800);
                return;
            }
            QString requestNote;
            m_localGroupMembers[userId] << account;
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
            switchToLocalGroup(userId, m_localGroupNames.value(userId, "群聊"));
            refreshGroupMemberPanel();
            appendSystemMessage(QString("已按 QQ 号邀请 %1 加入群聊%2").arg(account, requestNote));
            saveHistory(userId, QString("[%1] [系统] 已按 QQ 号邀请 %2 加入群聊").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), account));
        } else if (selected == inviteAllAction) {
            QStringList inviteIds;
            for (const QString& friendId : m_friendIds) {
                if (!m_localGroupMembers[userId].contains(friendId)) inviteIds << friendId;
            }
            if (inviteIds.isEmpty()) {
                ui->statusbar->showMessage("全部好友已在该群聊中", 1800);
                return;
            }
            QString groupName = m_localGroupNames.value(userId, "群聊");
            if (QMessageBox::question(this,
                                      "邀请全部好友",
                                      QString("确定邀请 %1 位好友加入群聊“%2”吗？").arg(inviteIds.size()).arg(groupName),
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No) != QMessageBox::Yes) {
                ui->statusbar->showMessage("已取消邀请全部好友", 1600);
                return;
            }
            for (const QString& friendId : inviteIds) {
                m_localGroupMembers[userId] << friendId;
            }
            saveLocalGroups();
            switchToLocalGroup(userId, m_localGroupNames.value(userId, "群聊"));
            appendSystemMessage(QString("已自动邀请 %1 位好友加入群聊").arg(inviteIds.size()));
            saveHistory(userId, QString("[%1] [系统] 已自动邀请 %2 位好友加入群聊").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(inviteIds.size()));
        } else if (selected == copyOnlineMembersAction) {
            QStringList rows;
            for (const QString& id : m_localGroupMembers.value(userId)) {
                if (id != m_currentUserId && !isContactOnline(id)) continue;
                rows << QString("在线成员 QQ:%1 昵称:%2").arg(id, contactDisplayName(id));
            }
            if (rows.isEmpty()) {
                ui->statusbar->showMessage("当前群聊没有在线成员可复制", 2200);
                return;
            }
            QApplication::clipboard()->setText(rows.join('\n'));
            ui->statusbar->showMessage(QString("已复制 %1 个在线群成员").arg(rows.size()), 2200);
        } else if (selected == renameGroupAction) {
            bool ok = false;
            const QString oldName = m_localGroupNames.value(userId, "群聊");
            QString newName = QInputDialog::getText(this, "重命名群聊", "群聊名称:", QLineEdit::Normal, oldName, &ok).trimmed();
            if (!ok) return;
            if (newName.isEmpty()) {
                ui->statusbar->showMessage("群聊名称不能为空", 1800);
                return;
            }
            if (newName == oldName) {
                ui->statusbar->showMessage("群聊名称未改变", 1600);
                return;
            }
            m_localGroupNames[userId] = newName;
            saveLocalGroups();
            refreshFriendList();
            if (m_privateChatTarget == userId) switchToLocalGroup(userId, newName);
            ui->statusbar->showMessage(QString("群聊已重命名为：%1").arg(newName), 2200);
        } else if (selected == deleteGroupAction) {
            QString groupName = m_localGroupNames.value(userId, userName);
            int memberCount = qMax(1, m_localGroupMembers.value(userId).size());
            if (QMessageBox::question(this,
                                      "删除群聊",
                                      QString("确定删除群聊“%1”吗？本地群成员 %2 人，聊天记录不会在此步骤删除。")
                                          .arg(groupName)
                                          .arg(memberCount),
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No) != QMessageBox::Yes) {
                ui->statusbar->showMessage("已取消删除群聊", 1600);
                return;
            }
            m_localGroupIds.removeAll(userId);
            m_localGroupNames.remove(userId);
            m_localGroupAnnouncements.remove(userId);
            m_localGroupMembers.remove(userId);
            saveLocalGroups();
            refreshFriendList();
            if (m_privateChatTarget == userId) onBackToGroupChat();
            appendSystemMessage("已删除群聊: " + groupName);
        }
        return;
    }

    QAction* chatAction = menu.addAction("发送消息");
    QAction* copyAction = menu.addAction("复制QQ号");
    QAction* profileAction = menu.addAction("复制名片");
    QAction* copyAddTextAction = menu.addAction("复制申请话术");
    QAction* copyOnlineCardAction = menu.addAction("复制在线名片");
    QAction* copyChatStarterAction = menu.addAction("复制开聊话术");
    QAction* copyE2EStatusAction = menu.addAction("复制加密状态");
    QAction* copyE2EIdentityAction = menu.addAction("复制加密身份指纹");
    QAction* copyE2EVerificationAction = menu.addAction("复制加密验证短码");
    QAction* trustE2EIdentityAction = menu.addAction("信任加密身份");
    QAction* verifyE2EIdentityAction = menu.addAction("验证并信任加密身份");
    const QJsonObject currentE2EIdentity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
    QAction* clearE2EIdentityTrustAction = currentE2EIdentity.value("pinned").toBool(false)
        ? menu.addAction("清除加密身份信任")
        : nullptr;
    QAction* requestE2ERotationAction = menu.addAction("请求加密轮换");
    QAction* clearE2ESessionAction = m_client && m_client->hasE2ESession(userId) ? menu.addAction("关闭本机会话密钥") : nullptr;
    const QJsonObject currentE2ESession = m_client ? m_client->e2eSessionStatus(userId) : QJsonObject();
    const QJsonObject currentE2ELocalIdentity = m_client ? m_client->e2eLocalIdentityStatus() : QJsonObject();
    const bool e2eBackendMigrationRequired =
        currentE2ELocalIdentity.value("backendMigrationRequired").toBool(false)
        || currentE2EIdentity.value("backendMigrationRequired").toBool(false)
        || currentE2ESession.value("backendMigrationRequired").toBool(false);
    QAction* clearE2EBackendMigrationAction = e2eBackendMigrationRequired
        ? menu.addAction("清理加密后端迁移状态")
        : nullptr;
    QAction* inviteCurrentGroupAction = m_privateChatTarget.startsWith("local_group_") ? menu.addAction("邀入当前群") : nullptr;
    QAction* renameAction = nullptr;
    QAction* addAction = nullptr;
    QAction* removeAction = nullptr;
    const bool hasPendingOutgoing = m_pendingOutgoingFriendRequests.contains(userId);
    if (m_friendIds.contains(userId)) {
        renameAction = menu.addAction("设置备注");
        removeAction = menu.addAction("删除好友");
    } else if (hasPendingOutgoing) {
        addAction = menu.addAction("好友申请待确认");
        addAction->setEnabled(false);
    } else {
        addAction = menu.addAction("加为好友");
    }
    describeUserAction(chatAction, "打开当前联系人私聊会话");
    describeUserAction(copyAction, "复制当前联系人 QQ 号");
    describeUserAction(profileAction, "复制当前联系人 QQ、昵称和关系状态");
    describeUserAction(copyAddTextAction, "复制适合当前联系人的好友申请话术");
    describeUserAction(copyOnlineCardAction, "复制当前联系人的在线名片和状态");
    describeUserAction(copyChatStarterAction, "复制一段可直接发送的开聊话术");
    describeUserAction(copyE2EStatusAction, "复制当前联系人端到端加密会话状态");
    describeUserAction(copyE2EIdentityAction, "复制本机记录的联系人端到端加密身份指纹");
    describeUserAction(copyE2EVerificationAction, "复制需要与对方跨设备核对的端到端加密验证短码");
    describeUserAction(trustE2EIdentityAction, "仅固定当前记录的联系人端到端加密身份指纹，仍需验证短码后才能用于默认加密");
    describeUserAction(verifyE2EIdentityAction, "输入与对方核对一致的验证短码并将身份标记为已验证信任");
    describeUserAction(clearE2EIdentityTrustAction, "清除当前联系人端到端加密身份固定信任并恢复为未验证");
    describeUserAction(requestE2ERotationAction, "向当前联系人发送端到端加密会话轮换请求；不包含本机会话密钥");
    describeUserAction(clearE2ESessionAction, "清除本机为该联系人保存的端到端会话密钥");
    describeUserAction(clearE2EBackendMigrationAction, "清除本机旧加密后端身份、信任和会话状态，等待新后端重新建立信任");
    describeUserAction(inviteCurrentGroupAction, "邀请当前联系人加入正在查看的本地群聊");
    describeUserAction(renameAction, "修改当前好友在本地显示的备注名");
    describeUserAction(removeAction, "从本地好友列表删除当前好友");
    describeUserAction(addAction, hasPendingOutgoing ? "好友申请已发送，等待对方处理" : "向当前联系人发送好友申请");

    QAction* selected = menu.exec(ui->userListView->viewport()->mapToGlobal(pos));
    if (!selected) return;
    if (selected == chatAction) {
        onPrivateChat(index);
    } else if (selected == copyAction) {
        QApplication::clipboard()->setText(userId);
        ui->statusbar->showMessage("QQ 号已复制: " + userId, 2500);
    } else if (selected == profileAction) {
        QString card = QString("QQ:%1\n昵称:%2\n状态:%3").arg(userId, contactDisplayName(userId), isContactOnline(userId) ? "在线" : "离线");
        QApplication::clipboard()->setText(card);
        ui->statusbar->showMessage("联系人名片已复制", 1800);
    } else if (selected == copyAddTextAction) {
        QString text = QString("你好，我是 %1（QQ:%2），通过 QQ 搜索看到你。方便的话加个好友，我们可以私聊或一起进群沟通。")
            .arg(m_currentUserName, m_currentUserId);
        QApplication::clipboard()->setText(text);
        ui->statusbar->showMessage("好友申请话术已复制", 2200);
    } else if (selected == copyOnlineCardAction) {
        QString card = QString("QQ:%1\n昵称:%2\n状态:%3\n关系:%4\n当前会话:%5")
            .arg(userId,
                 contactDisplayName(userId),
                 isContactOnline(userId) ? "在线" : "离线",
                 m_friendIds.contains(userId) ? "好友" : (m_pendingOutgoingFriendRequests.contains(userId) ? "申请中" : "陌生人"),
                 m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget));
        QApplication::clipboard()->setText(card);
        ui->statusbar->showMessage("在线名片已复制", 2200);
    } else if (selected == copyChatStarterAction) {
        QString text = m_friendIds.contains(userId)
            ? QString("%1，在吗？我是 %2（QQ:%3），想和你私聊确认一下刚才的消息。")
                .arg(contactDisplayName(userId), m_currentUserName, m_currentUserId)
            : m_pendingOutgoingFriendRequests.contains(userId)
            ? QString("%1，你好，我是 %2（QQ:%3），我已经发送好友申请了，通过后我们可以继续私聊。")
                .arg(contactDisplayName(userId), m_currentUserName, m_currentUserId)
            : QString("你好 %1，我是 %2（QQ:%3）。通过 QQ 搜索看到你，方便通过好友申请后再聊吗？")
                .arg(contactDisplayName(userId), m_currentUserName, m_currentUserId);
        QApplication::clipboard()->setText(text);
        ui->statusbar->showMessage("开聊话术已复制", 2200);
    } else if (selected == copyE2EStatusAction) {
        copyE2ESessionStatus(userId);
    } else if (selected == copyE2EIdentityAction) {
        const QJsonObject identity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
        const QString text = identity.value("configured").toBool(false)
            ? QString("端到端加密身份\n对端QQ：%1\n信任状态：%2\n验证状态：%3\n公钥指纹：%4\n验证短码：%5\n首次看到：%6\n最近看到：%7")
                .arg(userId,
                     identity.value("trustState").toString(),
                     identity.value("verified").toBool(false) ? QStringLiteral("verified") : QStringLiteral("unverified"),
                     identity.value("publicKeyFingerprintSha256").toString(),
                     identity.value("verificationCodeDisplay").toString(),
                     identity.value("firstSeenAt").toString(),
                     identity.value("lastSeenAt").toString())
            : QString("端到端加密身份\n对端QQ：%1\n信任状态：unknown\n说明：尚未收到该联系人的身份公告").arg(userId);
        QApplication::clipboard()->setText(text);
        ui->statusbar->showMessage("端到端加密身份指纹已复制", 2400);
    } else if (selected == copyE2EVerificationAction) {
        const QJsonObject identity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
        const QString code = identity.value("verificationCodeDisplay").toString();
        const QString text = identity.value("configured").toBool(false) && !code.isEmpty()
            ? QString("端到端加密验证短码\n对端QQ：%1\n短码：%2\n核对方式：请通过另一个可信渠道与对方屏幕上的短码一致后再验证信任。")
                .arg(userId, code)
            : QString("端到端加密验证短码\n对端QQ：%1\n说明：尚未收到该联系人的身份公告").arg(userId);
        QApplication::clipboard()->setText(text);
        ui->statusbar->showMessage("端到端加密验证短码已复制", 2400);
    } else if (selected == trustE2EIdentityAction) {
        QString rejectReason;
        if (m_client && m_client->pinE2EPeerIdentity(userId, QString(), &rejectReason)) {
            appendSystemMessage(QString("已固定 %1 的端到端加密身份指纹；完成验证短码核对前不会启用默认加密").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage("端到端加密身份已固定，等待验证", 2600);
        } else {
            appendSystemMessage(QString("信任端到端加密身份失败：%1").arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage("信任端到端加密身份失败", 3000);
        }
    } else if (selected == verifyE2EIdentityAction) {
        const QJsonObject identity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
        const QString suggestedCode = identity.value("verificationCodeDisplay").toString();
        bool ok = false;
        const QString code = QInputDialog::getText(this,
                                                   "验证加密身份",
                                                   QString("请输入与 %1 通过可信渠道核对一致的验证短码:").arg(contactDisplayName(userId)),
                                                   QLineEdit::Normal,
                                                   suggestedCode,
                                                   &ok).trimmed();
        if (!ok) {
            ui->statusbar->showMessage("已取消端到端加密身份验证", 1800);
        } else {
            QString rejectReason;
            if (m_client && m_client->verifyAndPinE2EPeerIdentity(userId, code, &rejectReason)) {
                appendSystemMessage(QString("已验证并信任 %1 的端到端加密身份").arg(contactDisplayName(userId)));
                ui->statusbar->showMessage("端到端加密身份已验证", 2400);
            } else {
                appendSystemMessage(QString("验证端到端加密身份失败：%1").arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
                ui->statusbar->showMessage("验证端到端加密身份失败", 3000);
            }
        }
    } else if (selected == clearE2EIdentityTrustAction) {
        QString rejectReason;
        if (m_client && m_client->clearE2EPeerIdentityPin(userId, &rejectReason)) {
            appendSystemMessage(QString("已清除 %1 的端到端加密身份信任固定").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage("端到端加密身份信任已清除", 2400);
        } else {
            appendSystemMessage(QString("清除端到端加密身份信任失败：%1").arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage("清除端到端加密身份信任失败", 3000);
        }
    } else if (selected == requestE2ERotationAction) {
        QString rejectReason;
        if (m_client && m_client->requestE2ESessionRotation(userId, &rejectReason)) {
            appendSystemMessage(QString("已向 %1 发送端到端加密轮换请求").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage("端到端加密轮换请求已发送", 2400);
        } else {
            appendSystemMessage(QString("端到端加密轮换请求发送失败：%1").arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage("端到端加密轮换请求发送失败", 3000);
        }
    } else if (selected == clearE2ESessionAction) {
        if (m_client) {
            m_client->clearE2ESessionKey(userId);
            appendSystemMessage(QString("已关闭 %1 的本机端到端加密会话").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage("本机端到端加密会话已关闭", 2400);
        }
    } else if (selected == clearE2EBackendMigrationAction) {
        QString rejectReason;
        if (m_client && m_client->clearE2EBackendMigrationState(&rejectReason)) {
            appendSystemMessage("已清理本机端到端加密后端迁移状态；需要重新接收身份公告、核对短码并建立会话后才能继续默认加密");
            ui->statusbar->showMessage("端到端加密迁移状态已清理", 3200);
        } else {
            appendSystemMessage(QString("清理端到端加密迁移状态失败：%1").arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage("清理端到端加密迁移状态失败", 3000);
        }
    } else if (selected == inviteCurrentGroupAction) {
        QString requestNote;
        if (!m_friendIds.contains(userId)) {
            if (m_pendingOutgoingFriendRequests.contains(userId)) {
                requestNote = "，好友申请已在等待确认";
            } else if (m_client->sendFriendRequest(userId)) {
                m_friendNames[userId] = contactDisplayName(userId);
                m_pendingOutgoingFriendRequests << userId;
                requestNote = "，好友申请等待确认";
            } else {
                requestNote = "，好友申请发送失败";
                ui->statusbar->showMessage(QString("已邀请入群，但好友申请发送失败：%1").arg(contactDisplayName(userId)), 3000);
            }
        }
        if (!m_localGroupMembers[m_privateChatTarget].contains(userId)) {
            m_localGroupMembers[m_privateChatTarget] << userId;
            saveLocalGroups();
        }
        refreshFriendList();
        refreshGroupMemberPanel();
        appendSystemMessage(QString("已邀请 %1 加入当前群聊%2").arg(contactDisplayName(userId), requestNote));
        saveHistory(m_privateChatTarget, QString("[%1] [系统] 已邀请 %2 加入群聊").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), contactDisplayName(userId)));
    } else if (selected == renameAction) {
        bool ok = false;
        const QString oldRemark = contactDisplayName(userId);
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
        m_friendNames[userId] = remark;
        saveFriends();
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->statusbar->showMessage(QString("已设置备注：%1").arg(remark), 2200);
        appendSystemMessage(QString("已设置 %1 的备注为 %2").arg(userId, remark));
    } else if (selected == addAction) {
        if (m_pendingOutgoingFriendRequests.contains(userId)) {
            ui->statusbar->showMessage(QString("已向 %1 发送过好友申请，等待对方处理").arg(contactDisplayName(userId)), 2500);
            return;
        }
        if (!m_friendIds.contains(userId)) {
            const QString displayName = contactDisplayName(userId);
            if (!m_client->sendFriendRequest(userId)) {
                ui->statusbar->showMessage(QString("好友申请发送失败：%1").arg(displayName), 3000);
                appendSystemMessage(QString("好友申请发送失败 QQ:%1，请检查连接后重试").arg(userId));
                return;
            }
            m_friendNames[userId] = displayName;
            m_pendingOutgoingFriendRequests << userId;
            refreshFriendList();
            appendSystemMessage(QString("已发送好友申请: %1（QQ:%2），等待对方同意").arg(displayName, userId));
        }
    } else if (selected == removeAction) {
        const QString displayName = contactDisplayName(userId);
        if (QMessageBox::question(this,
                                  "删除好友",
                                  QString("确定删除好友“%1”（QQ:%2）吗？删除后可重新搜索并发送申请。").arg(displayName, userId),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage("已取消删除好友", 1600);
            return;
        }
        m_friendIds.removeAll(userId);
        m_friendNames.remove(userId);
        saveFriends();
        refreshFriendList();
        appendSystemMessage(QString("已删除好友: %1（QQ:%2）").arg(displayName, userId));
    }
}

void MainWindow::onShowFriendNotifications() {
    QDialog dialog(this);
    dialog.setObjectName("noticeDialog");
    dialog.setWindowTitle("好友通知");
    dialog.setFixedSize(860, 660);

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
    clearBtn->setToolTip("清空全部待处理好友申请，不会自动回复对方");
    titleLayout->addWidget(clearBtn);
    layout->addLayout(titleLayout);

    QLabel* statsLabel = new QLabel(&dialog);
    statsLabel->setObjectName("noticeSubTitle");
    layout->addWidget(statsLabel);

    QLineEdit* searchEdit = new QLineEdit(&dialog);
    searchEdit->setObjectName("noticeSearch");
    searchEdit->setPlaceholderText("搜索申请人 QQ 号 / 昵称");
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setToolTip("按 QQ 号或昵称筛选好友申请；回车可搜索账号");
    layout->addWidget(searchEdit);

    QListWidget* noticeList = new QListWidget(&dialog);
    noticeList->setObjectName("noticeList");
    noticeList->setWordWrap(true);
    layout->addWidget(noticeList, 1);

    auto fillList = [this, noticeList, statsLabel, searchEdit]() {
        noticeList->clear();
        QString filter = searchEdit->text().trimmed();
        int visibleCount = 0;
        statsLabel->setText(QString("待处理 %1 个申请 · 已有好友 %2 人").arg(m_pendingFriendRequests.size()).arg(m_friendIds.size()));
        if (m_pendingFriendRequests.isEmpty()) {
            if (filter.isEmpty()) {
                QListWidgetItem* emptyItem = new QListWidgetItem("暂无新的好友申请");
                emptyItem->setFlags(Qt::NoItemFlags);
                emptyItem->setForeground(QColor(135, 150, 165));
                emptyItem->setSizeHint(QSize(0, 68));
                emptyItem->setToolTip("当前没有待处理好友申请，可在搜索框输入 QQ 号后回车查找");
                noticeList->addItem(emptyItem);
            } else {
                statsLabel->setText(QString("暂无待处理申请 · 可搜索 QQ:%1").arg(filter));
                QListWidgetItem* searchItem = new QListWidgetItem(QString("暂无待处理申请，可直接搜索并添加 QQ:%1").arg(filter));
                searchItem->setData(Qt::UserRole, "search_add:" + filter);
                searchItem->setForeground(QColor(18, 150, 247));
                searchItem->setSizeHint(QSize(0, 68));
                searchItem->setToolTip(QString("选择后点击“搜索并添加”，或按回车搜索 QQ:%1").arg(filter));
                noticeList->addItem(searchItem);
                noticeList->setCurrentRow(0);
            }
            return;
        }
        for (const QString& id : m_pendingFriendRequests) {
            QString name = m_friendNames.value(id, id);
            if (!filter.isEmpty()
                && !id.contains(filter, Qt::CaseInsensitive)
                && !name.contains(filter, Qt::CaseInsensitive)) continue;
            QListWidgetItem* item = new QListWidgetItem(QString("%1  请求加为好友\n留言：请求添加对方为好友\n来源：QQ号-%2").arg(name, id));
            item->setData(Qt::UserRole, id);
            item->setSizeHint(QSize(0, 92));
            item->setToolTip(QString("申请人 %1（QQ:%2），可同意、拒绝、复制名片或回复话术").arg(name, id));
            noticeList->addItem(item);
            ++visibleCount;
        }
        statsLabel->setText(filter.isEmpty()
            ? QString("待处理 %1 个申请 · 已有好友 %2 人").arg(m_pendingFriendRequests.size()).arg(m_friendIds.size())
            : QString("待处理 %1 个申请 · 匹配 %2 个 · 已有好友 %3 人").arg(m_pendingFriendRequests.size()).arg(visibleCount).arg(m_friendIds.size()));
        if (visibleCount == 0 && !filter.isEmpty()) {
            QListWidgetItem* emptyItem = new QListWidgetItem(QString("未找到申请人，可清空搜索或直接添加 QQ:%1").arg(filter));
            emptyItem->setData(Qt::UserRole, "search_add:" + filter);
            emptyItem->setForeground(QColor(18, 150, 247));
            emptyItem->setSizeHint(QSize(0, 68));
            emptyItem->setToolTip(QString("没有匹配的好友申请，可直接搜索并添加 QQ:%1").arg(filter));
            noticeList->addItem(emptyItem);
        }
        for (int i = 0; i < noticeList->count(); ++i) {
            if (noticeList->item(i)->flags().testFlag(Qt::ItemIsEnabled)) {
                noticeList->setCurrentRow(i);
                break;
            }
        }
    };
    fillList();

    QLabel* requestPreviewLabel = new QLabel("选择申请后可同意、拒绝、复制名片或回复话术", &dialog);
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
    QPushButton* acceptBtn = new QPushButton("同意", &dialog);
    acceptBtn->setObjectName("noticePrimaryBtn");
    acceptBtn->setToolTip("同意当前选中的好友申请并加入好友列表");
    QPushButton* acceptAllBtn = new QPushButton("一键同意全部", &dialog);
    acceptAllBtn->setObjectName("noticePrimaryBtn");
    acceptAllBtn->setToolTip("确认后批量同意所有待处理好友申请");
    QPushButton* rejectBtn = new QPushButton("拒绝", &dialog);
    rejectBtn->setObjectName("noticeDangerBtn");
    rejectBtn->setToolTip("拒绝当前选中的好友申请");
    QPushButton* rejectAllBtn = new QPushButton("一键拒绝全部", &dialog);
    rejectAllBtn->setObjectName("noticeDangerBtn");
    rejectAllBtn->setToolTip("确认后批量拒绝所有待处理好友申请");
    QPushButton* copyBtn = new QPushButton("复制名片", &dialog);
    copyBtn->setObjectName("noticeGhostBtn");
    copyBtn->setToolTip("复制当前申请人的 QQ、昵称和来源");
    QPushButton* copyInviteBtn = new QPushButton("复制申请话术", &dialog);
    copyInviteBtn->setObjectName("noticeGhostBtn");
    copyInviteBtn->setToolTip("复制一段回复好友申请的礼貌话术");
    QPushButton* copyAllBtn = new QPushButton("复制全部申请", &dialog);
    copyAllBtn->setObjectName("noticeGhostBtn");
    copyAllBtn->setToolTip("复制所有待处理申请的 QQ、昵称和回复话术");
    QPushButton* copyRequestMediaPackBtn = new QPushButton("复制申请媒体包", &dialog);
    copyRequestMediaPackBtn->setObjectName("noticeGhostBtn");
    copyRequestMediaPackBtn->setToolTip("复制同意好友后发送图片、视频或文件的准备摘要");
    QPushButton* copyRequestBatchPlanBtn = new QPushButton("复制申请处理计划", &dialog);
    copyRequestBatchPlanBtn->setObjectName("noticeGhostBtn");
    copyRequestBatchPlanBtn->setToolTip("复制当前筛选申请的批量处理和媒体发送清单");
    QPushButton* copyMediaGuideBtn = new QPushButton("复制上传指南", &dialog);
    copyMediaGuideBtn->setObjectName("noticeGhostBtn");
    copyMediaGuideBtn->setToolTip("复制同意好友后发送图片、视频和文件的简短指南");
    QPushButton* closeBtn = new QPushButton("关闭", &dialog);
    closeBtn->setObjectName("noticeGhostBtn");
    closeBtn->setToolTip("关闭好友通知窗口");
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
        QLabel#noticeSubTitle {
            color: #6B7A88;
            font-size: 13px;
            font-weight: 800;
            padding-left: 4px;
        }
        QLabel#noticePreviewLabel {
            color: #3A4A5A;
            background: #EAF7FF;
            border: 1px solid #DCEFFF;
            border-radius: 14px;
            padding: 7px 12px;
            font-size: 12px;
            font-weight: 800;
        }
        QLineEdit#noticeSearch {
            min-height: 38px;
            background: white;
            border: 1px solid #DDE7F0;
            border-radius: 18px;
            padding: 4px 14px;
            color: #263238;
        }
        QLineEdit#noticeSearch:focus {
            border: 1px solid #12B7F5;
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
        QPushButton#noticePrimaryBtn:disabled,
        QPushButton#noticeDangerBtn:disabled,
        QPushButton#noticeGhostBtn:disabled {
            background: #F3F6F9;
            color: #9AA8B6;
            border: 1px solid #E3EAF1;
        }
    )");

    auto updateBadge = [this]() {
        const FriendNoticeUiState noticeState = m_friendManager.noticeUiState(m_pendingFriendRequests.size());
        ui->friendNoticeBtn->setText(noticeState.text);
        ui->friendNoticeBtn->setToolTip(noticeState.toolTip);
    };
    auto currentRequestId = [noticeList]() -> QString {
        return selectedFriendNoticeEntryId(noticeList);
    };
    auto updateRequestActionState = [=]() {
        const QString currentId = currentRequestId();
        const bool hasPending = !m_pendingFriendRequests.isEmpty();
        const bool canAccept = currentId.startsWith("search_add:") || !currentId.isEmpty();
        const bool isRealRequest = !currentId.isEmpty() && !currentId.startsWith("search_add:");
        const bool hasSearchKeyword = !searchEdit->text().trimmed().isEmpty();
        const bool isSearchAdd = currentId.startsWith("search_add:");
        acceptBtn->setEnabled(canAccept);
        acceptBtn->setText(isSearchAdd ? "搜索并添加" : "同意");
        acceptBtn->setToolTip(isRealRequest
            ? "同意当前选中的好友申请并加入好友列表"
            : (isSearchAdd ? "对搜索结果里的 QQ 号发送好友申请" : "选择申请后可同意，或先搜索 QQ 号"));
        rejectBtn->setEnabled(isRealRequest);
        rejectBtn->setToolTip(isRealRequest ? "拒绝当前选中的好友申请" : "当前没有可拒绝的好友申请");
        copyBtn->setEnabled(isRealRequest);
        copyBtn->setToolTip(isRealRequest ? "复制当前申请人的 QQ、昵称和来源" : "当前没有可复制的申请人名片");
        copyInviteBtn->setEnabled(hasPending || hasSearchKeyword);
        copyInviteBtn->setToolTip(hasPending || hasSearchKeyword ? "复制一段回复好友申请的礼貌话术" : "有申请或输入 QQ 后可复制回复话术");
        copyAllBtn->setEnabled(hasPending);
        copyAllBtn->setToolTip(hasPending ? "复制所有待处理申请的 QQ、昵称和回复话术" : "当前没有待处理好友申请");
        copyRequestMediaPackBtn->setEnabled(hasPending || hasSearchKeyword);
        copyRequestMediaPackBtn->setToolTip(hasPending || hasSearchKeyword ? "复制同意好友后发送图片、视频或文件的准备摘要" : "有申请或输入 QQ 后可复制媒体准备摘要");
        copyRequestBatchPlanBtn->setEnabled(hasPending || hasSearchKeyword);
        copyRequestBatchPlanBtn->setToolTip(hasPending || hasSearchKeyword ? "复制当前筛选申请的批量处理和媒体发送清单" : "当前没有可整理的申请");
        copyMediaGuideBtn->setEnabled(hasPending || hasSearchKeyword);
        copyMediaGuideBtn->setToolTip(hasPending || hasSearchKeyword ? "复制同意好友后发送图片、视频和文件的简短指南" : "有申请或输入 QQ 后可复制上传指南");
        acceptAllBtn->setEnabled(hasPending);
        acceptAllBtn->setToolTip(hasPending ? "确认后批量同意所有待处理好友申请" : "当前没有待处理好友申请");
        rejectAllBtn->setEnabled(hasPending);
        rejectAllBtn->setToolTip(hasPending ? "确认后批量拒绝所有待处理好友申请" : "当前没有待处理好友申请");
        clearBtn->setEnabled(hasPending);
        clearBtn->setToolTip(hasPending ? "清空全部待处理好友申请，不会自动回复对方" : "当前没有待清空的好友申请");
    };
    auto updateRequestPreview = [this, noticeList, requestPreviewLabel]() {
        QListWidgetItem* item = noticeList->currentItem();
        if (!item) {
            requestPreviewLabel->setText("选择申请后可同意、拒绝、复制名片或回复话术");
            return;
        }
        QString id = item->data(Qt::UserRole).toString();
        if (id.startsWith("search_add:")) {
            requestPreviewLabel->setText(QString("未找到申请人，可搜索并发送申请 QQ:%1").arg(id.mid(QString("search_add:").size())));
        } else if (!id.isEmpty()) {
            requestPreviewLabel->setText(QString("申请人 · %1 · QQ:%2 · 可自动同意并加为好友").arg(m_friendNames.value(id, contactDisplayName(id)), id));
        } else {
            requestPreviewLabel->setText("暂无可处理申请");
        }
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
        ui->statusbar->showMessage(QString("已同意 %1 的好友申请").arg(name), 2200);
        appendSystemMessage("已同意好友申请 QQ: " + id);
    });
    connect(acceptAllBtn, &QPushButton::clicked, &dialog, [this, fillList, updateBadge, updateRequestActionState, &dialog]() {
        QStringList pending = m_pendingFriendRequests;
        if (pending.isEmpty()) {
            ui->statusbar->showMessage("暂无好友申请可同意", 1800);
            return;
        }
        if (QMessageBox::question(&dialog,
                                  "一键同意好友申请",
                                  QString("确定同意全部 %1 个好友申请吗？同意后会加入好友列表。").arg(pending.size()),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage("已取消一键同意", 1600);
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
        ui->statusbar->showMessage(QString("已一键同意 %1 个好友申请").arg(pending.size()), 2200);
        appendSystemMessage(QString("已一键同意 %1 个好友申请").arg(pending.size()));
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
        ui->statusbar->showMessage(QString("已拒绝 QQ:%1 的好友申请").arg(id), 2200);
        appendSystemMessage("已拒绝好友申请 QQ: " + id);
    });
    connect(rejectAllBtn, &QPushButton::clicked, &dialog, [this, fillList, updateBadge, updateRequestActionState, &dialog]() {
        QStringList pending = m_pendingFriendRequests;
        if (pending.isEmpty()) {
            ui->statusbar->showMessage("暂无好友申请可拒绝", 1800);
            return;
        }
        if (QMessageBox::question(&dialog,
                                  "一键拒绝好友申请",
                                  QString("确定拒绝全部 %1 个好友申请吗？").arg(pending.size()),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage("已取消一键拒绝", 1600);
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
        ui->statusbar->showMessage(QString("已一键拒绝 %1 个好友申请").arg(pending.size()), 2200);
        appendSystemMessage(QString("已一键拒绝 %1 个好友申请").arg(pending.size()));
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
        QString card = QString("QQ:%1\n昵称:%2\n来源:好友申请").arg(id, m_friendNames.value(id, id));
        copyTextWithStatus(card, "申请人名片已复制");
    });
    connect(copyInviteBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        QString id = selectedFriendNoticeTargetId(noticeList, searchEdit);
        QString name = id.isEmpty() ? "朋友" : m_friendNames.value(id, contactDisplayName(id));
        QString text = QString("%1，你好，我是 %2（QQ:%3）。我已看到你的好友申请，稍后可以通过后继续私聊，也可以邀请你加入群聊沟通。")
            .arg(name, m_currentUserName, m_currentUserId);
        copyTextWithStatus(text, "申请回复话术已复制", 2200);
    });
    connect(copyAllBtn, &QPushButton::clicked, &dialog, [this]() {
        QStringList rows;
        for (const QString& id : m_pendingFriendRequests) {
            if (id.isEmpty()) continue;
            rows << QString("好友申请 QQ:%1 昵称:%2 回复:%3，你好，我是 %4（QQ:%5），已看到你的好友申请。")
                .arg(id, m_friendNames.value(id, contactDisplayName(id)), m_friendNames.value(id, contactDisplayName(id)), m_currentUserName, m_currentUserId);
        }
        if (rows.isEmpty()) {
            ui->statusbar->showMessage("暂无好友申请可复制", 2200);
            return;
        }
        copyTextWithStatus(rows.join('\n'), QString("已复制 %1 条好友申请").arg(rows.size()), 2200);
    });
    connect(copyRequestMediaPackBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        QString id = selectedFriendNoticeTargetId(noticeList, searchEdit);
        QString name = id.isEmpty() ? "新好友" : m_friendNames.value(id, contactDisplayName(id));
        QStringList rows;
        rows << QString("好友申请媒体包 · 申请人:%1 · QQ:%2").arg(name, id.isEmpty() ? "待选择" : id);
        rows << QString("我的QQ:%1 · 昵称:%2 · 待处理申请:%3").arg(m_currentUserId, m_currentUserName, QString::number(m_pendingFriendRequests.size()));
        rows << "同意好友后可直接私聊，点击 图片/视频 或 闪传文件 发送媒体";
        rows << "支持 png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm 和常用文档压缩包";
        rows << QString("通过话术：%1，你好，我是 %2（QQ:%3），我会通过你的好友申请，之后可以发图片/视频/文件给你。").arg(name, m_currentUserName, m_currentUserId);
        rows << QString("查收话术：我已发送媒体文件给 %1，请注意查收。").arg(name);
        copyTextWithStatus(rows.join('\n'), "好友申请媒体包已复制", 2200);
    });
    connect(copyRequestBatchPlanBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        QStringList applicants;
        const QStringList visibleIds = visibleFriendNoticeIds(noticeList);
        for (const QString& id : visibleIds) {
            applicants << QString("%1(QQ:%2)").arg(m_friendNames.value(id, contactDisplayName(id)), id);
        }
        QString keyword = searchEdit->text().trimmed();
        QStringList rows;
        rows << QString("好友申请处理计划 · 筛选:%1").arg(keyword.isEmpty() ? "全部申请" : keyword);
        rows << QString("我的QQ:%1 · 昵称:%2 · 待处理:%3 · 可见:%4")
            .arg(m_currentUserId, m_currentUserName, QString::number(m_pendingFriendRequests.size()), QString::number(applicants.size()));
        rows << QString("申请人:%1").arg(applicants.isEmpty() ? "无可见申请" : applicants.join("、"));
        rows << "1. 先复制申请媒体包确认目标和后续发送内容";
        rows << "2. 同意后从好友管理或私聊入口发送图片/视频/闪传文件";
        rows << "3. 对方离线时复制查收话术，在线时直接发送媒体";
        rows << "4. 聊天记录右键复制媒体流程、回执话术和保存路径";
        copyTextWithStatus(rows.join('\n'), "好友申请处理计划已复制", 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        QString id = selectedFriendNoticeTargetId(noticeList, searchEdit);
        QString name = id.isEmpty() ? "新好友" : m_friendNames.value(id, contactDisplayName(id));
        QStringList rows;
        rows << QString("好友申请上传指南 · 我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QString("申请人:%1 · QQ:%2").arg(name, id.isEmpty() ? "待选择" : id);
        rows << "同意好友后可发送图片/视频，也可用闪传文件发送文档、压缩包和媒体文件";
        rows << "聊天记录右键可复制媒体卡片和查收话术";
        copyTextWithStatus(rows.join('\n'), "好友申请上传指南已复制", 2200);
    });
    connect(clearBtn, &QPushButton::clicked, &dialog, [this, fillList, updateBadge, updateRequestActionState, &dialog]() {
        if (m_pendingFriendRequests.isEmpty()) {
            ui->statusbar->showMessage("暂无好友申请可清空", 1600);
            return;
        }
        if (QMessageBox::question(&dialog,
                                  "清空好友申请",
                                  QString("确定清空 %1 个待处理好友申请吗？清空不会自动回复对方。").arg(m_pendingFriendRequests.size()),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage("已取消清空好友申请", 1600);
            return;
        }
        m_pendingFriendRequests.clear();
        saveFriends();
        updateBadge();
        fillList();
        updateRequestActionState();
        ui->statusbar->showMessage("好友申请已清空", 1800);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::onShowGroupNotifications() {
    QDialog dialog(this);
    dialog.setObjectName("noticeDialog");
    dialog.setWindowTitle("群通知");
    dialog.setFixedSize(880, 620);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(18);

    QHBoxLayout* titleLayout = new QHBoxLayout;
    QLabel* titleLabel = new QLabel("群通知", &dialog);
    titleLabel->setObjectName("noticeTitle");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    QLabel* countLabel = new QLabel(QString("已加入 %1 个群聊").arg(m_localGroupIds.size() + 1), &dialog);
    countLabel->setObjectName("noticeSubTitle");
    titleLayout->addWidget(countLabel);
    layout->addLayout(titleLayout);

    QLineEdit* searchEdit = new QLineEdit(&dialog);
    searchEdit->setObjectName("noticeSearch");
    searchEdit->setPlaceholderText("搜索群名 / 群号 / 公告");
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setToolTip("按群名、群号或公告筛选；无结果时可按回车创建新群");
    layout->addWidget(searchEdit);

    QListWidget* noticeList = new QListWidget(&dialog);
    noticeList->setObjectName("noticeList");
    noticeList->setWordWrap(true);

    auto fillGroups = [this, noticeList, countLabel, searchEdit]() {
        noticeList->clear();
        QString filter = searchEdit->text().trimmed();
        int visibleCount = 0;
        bool publicMatched = filter.isEmpty()
            || QString("公共聊天室").contains(filter, Qt::CaseInsensitive)
            || QString("默认公共聊天室").contains(filter, Qt::CaseInsensitive);
        if (publicMatched) {
            QListWidgetItem* publicItem = new QListWidgetItem(QString("默认公共聊天室\n你已加入默认群聊，可直接发送消息、图片和文件。\n在线成员：%1 人").arg(m_knownUsers.size()));
            publicItem->setData(Qt::UserRole, QString());
            publicItem->setSizeHint(QSize(0, 96));
            publicItem->setToolTip(QString("进入公共聊天室，当前在线成员 %1 人").arg(m_knownUsers.size()));
            noticeList->addItem(publicItem);
            ++visibleCount;
        }

        for (const QString& groupId : m_localGroupIds) {
            QString groupName = m_localGroupNames.value(groupId, "群聊");
            QString groupNumber = groupId.mid(QString("local_group_").size());
            QStringList members = m_localGroupMembers.value(groupId);
            if (members.isEmpty()) members << m_currentUserId;
            QString announcement = m_localGroupAnnouncements.value(groupId, QString("%1 已创建，可继续邀请好友并发送消息。").arg(groupName));
            if (!filter.isEmpty()
                && !groupName.contains(filter, Qt::CaseInsensitive)
                && !groupNumber.contains(filter, Qt::CaseInsensitive)
                && !announcement.contains(filter, Qt::CaseInsensitive)) continue;
            QListWidgetItem* item = new QListWidgetItem(QString("%1\n群号：%2 · 成员：%3 人\n%4").arg(groupName, groupNumber).arg(members.size()).arg(announcement));
            item->setData(Qt::UserRole, groupId);
            item->setSizeHint(QSize(0, 108));
            item->setToolTip(QString("群聊 %1（群号:%2），可进入、复制公告、成员或入群话术").arg(groupName, groupNumber));
            noticeList->addItem(item);
            ++visibleCount;
        }
        countLabel->setText(filter.isEmpty()
            ? QString("已加入 %1 个群聊").arg(m_localGroupIds.size() + 1)
            : QString("匹配 %1 / %2 个群聊").arg(visibleCount).arg(m_localGroupIds.size() + 1));
        if (visibleCount == 0) {
            QListWidgetItem* emptyItem = new QListWidgetItem(QString("未找到群聊，可用关键词“%1”创建新群").arg(filter));
            emptyItem->setData(Qt::UserRole, "group_create:" + filter);
            emptyItem->setForeground(QColor(18, 150, 247));
            emptyItem->setSizeHint(QSize(0, 76));
            emptyItem->setToolTip(QString("选择后点击“创建并进入群聊”，使用关键词“%1”创建新群").arg(filter));
            noticeList->addItem(emptyItem);
        }
        if (noticeList->count() > 0) noticeList->setCurrentRow(0);
    };
    fillGroups();

    QLabel* groupPreviewLabel = new QLabel("选择群聊后可复制群号、公告、成员或入群话术", &dialog);
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
    QLabel* hintLabel = new QLabel("双击群通知可直接进入群聊", &dialog);
    hintLabel->setObjectName("noticeHint");
    hintLayout->addWidget(hintLabel);
    hintLayout->addStretch();
    QPushButton* openBtn = new QPushButton("进入选中群聊", &dialog);
    openBtn->setObjectName("noticePrimaryBtn");
    openBtn->setToolTip("进入当前选中的公共聊天室或本地群聊");
    QPushButton* copyBtn = new QPushButton("复制群号", &dialog);
    copyBtn->setObjectName("noticeGhostBtn");
    copyBtn->setToolTip("复制当前选中群聊的群号");
    QPushButton* cardBtn = new QPushButton("复制群名片", &dialog);
    cardBtn->setObjectName("noticeGhostBtn");
    cardBtn->setToolTip("复制群名、群号、成员数和公告摘要");
    QPushButton* announceBtn = new QPushButton("复制公告", &dialog);
    announceBtn->setObjectName("noticeGhostBtn");
    announceBtn->setToolTip("复制当前选中群聊的公告内容");
    QPushButton* inviteTextBtn = new QPushButton("复制入群话术", &dialog);
    inviteTextBtn->setObjectName("noticeGhostBtn");
    inviteTextBtn->setToolTip("复制一段可直接发给好友的入群邀请");
    QPushButton* memberBtn = new QPushButton("复制成员", &dialog);
    memberBtn->setObjectName("noticeGhostBtn");
    memberBtn->setToolTip("复制当前选中群聊的全部成员列表");
    QPushButton* onlineMemberBtn = new QPushButton("复制在线成员", &dialog);
    onlineMemberBtn->setObjectName("noticeGhostBtn");
    onlineMemberBtn->setToolTip("复制当前群里在线成员的 QQ 和昵称");
    QPushButton* copyGroupMediaPackBtn = new QPushButton("复制群媒体包", &dialog);
    copyGroupMediaPackBtn->setObjectName("noticeGhostBtn");
    copyGroupMediaPackBtn->setToolTip("复制群聊媒体发送前的目标、成员和话术摘要");
    QPushButton* copyGroupBatchPlanBtn = new QPushButton("复制群批量媒体计划", &dialog);
    copyGroupBatchPlanBtn->setObjectName("noticeGhostBtn");
    copyGroupBatchPlanBtn->setToolTip("复制群聊批量发送图片、视频或文件的操作清单");
    QPushButton* copyMediaGuideBtn = new QPushButton("复制上传指南", &dialog);
    copyMediaGuideBtn->setObjectName("noticeGhostBtn");
    copyMediaGuideBtn->setToolTip("复制群聊中发送图片、视频和文件的简短指南");
    QPushButton* closeBtn = new QPushButton("关闭", &dialog);
    closeBtn->setObjectName("noticeGhostBtn");
    closeBtn->setToolTip("关闭群通知窗口");
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
        const QString groupId = selectedGroupNoticeEntryId(noticeList);
        if (groupId.isEmpty()) {
            ui->statusbar->showMessage("请先选择要进入的群聊", 1800);
            return;
        }
        if (isGroupCreateEntryId(groupId)) {
            QString groupName = groupCreateEntryName(groupId);
            if (groupName.isEmpty()) groupName = "搜索群聊";
            QString newGroupId = "local_group_" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
            m_localGroupIds << newGroupId;
            m_localGroupNames[newGroupId] = groupName;
            m_localGroupAnnouncements[newGroupId] = QString("%1 已从群通知搜索创建，可继续邀请好友并发送消息。").arg(groupName);
            m_localGroupMembers[newGroupId] = QStringList{m_currentUserId};
            saveLocalGroups();
            refreshFriendList();
            dialog.accept();
            switchToLocalGroup(newGroupId, groupName);
            appendSystemMessage("已从群通知搜索创建群聊: " + groupName);
            ui->statusbar->showMessage("已创建并进入群聊: " + groupName, 2200);
            return;
        }
        dialog.accept();
        if (groupId.isEmpty()) {
            onBackToGroupChat();
            ui->statusbar->showMessage("已进入公共聊天室", 1800);
            return;
        }
        switchToLocalGroup(groupId, m_localGroupNames.value(groupId, "群聊"));
        ui->statusbar->showMessage("已进入群聊: " + m_localGroupNames.value(groupId, "群聊"), 1800);
    };
    auto publicGroupMemberIds = [this]() {
        QStringList members;
        members << m_currentUserId;
        for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
            if (!members.contains(it.key())) members << it.key();
        }
        return members;
    };

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
        QLabel#noticeSubTitle, QLabel#noticeHint {
            color: #6B7A88;
            font-size: 13px;
            font-weight: 700;
        }
        QLabel#noticePreviewLabel {
            color: #3A4A5A;
            background: #EAF7FF;
            border: 1px solid #DCEFFF;
            border-radius: 14px;
            padding: 7px 12px;
            font-size: 12px;
            font-weight: 800;
        }
        QLineEdit#noticeSearch {
            min-height: 38px;
            background: white;
            border: 1px solid #DDE7F0;
            border-radius: 18px;
            padding: 4px 14px;
            color: #263238;
        }
        QLineEdit#noticeSearch:focus {
            border: 1px solid #12B7F5;
        }
        QListWidget#noticeList {
            background: #F4F4F4;
            border: none;
            outline: none;
        }
        QListWidget#noticeList::item {
            background: white;
            border-radius: 10px;
            margin: 7px 44px;
            padding: 14px 18px;
            color: #263238;
        }
        QListWidget#noticeList::item:selected {
            background: #DFF2FF;
            color: #102A43;
        }
        QPushButton#noticePrimaryBtn {
            min-height: 34px;
            border-radius: 17px;
            padding: 6px 18px;
            font-weight: 800;
            background: #12B7F5;
            color: white;
            border: none;
        }
        QPushButton#noticePrimaryBtn:disabled,
        QPushButton#noticeDangerBtn:disabled,
        QPushButton#noticeGhostBtn:disabled {
            background: #F3F6F9;
            color: #9AA8B6;
            border: 1px solid #E3EAF1;
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
    connect(openBtn, &QPushButton::clicked, &dialog, openSelectedGroup);
    auto updateGroupPreview = [this, noticeList, groupPreviewLabel]() {
        QListWidgetItem* current = noticeList->currentItem();
        if (!current) {
            groupPreviewLabel->setText("选择群聊后可复制群号、公告、成员或入群话术");
            return;
        }
        const QString groupId = selectedGroupNoticeEntryId(noticeList);
        if (isGroupCreateEntryId(groupId)) {
            QString name = groupCreateEntryName(groupId);
            groupPreviewLabel->setText(QString("待创建群聊 · %1 · 创建后可邀请好友").arg(name.isEmpty() ? "搜索群聊" : name));
        } else if (groupId.startsWith("local_group_")) {
            groupPreviewLabel->setText(QString("群聊 · %1 · 群号:%2 · 成员%3人")
                .arg(m_localGroupNames.value(groupId, "群聊"), groupId.mid(QString("local_group_").size()), QString::number(m_localGroupMembers.value(groupId).size())));
        } else {
            groupPreviewLabel->setText(QString("公共聊天室 · 在线成员%1人 · 可直接进入").arg(m_knownUsers.size()));
        }
    };
    auto updateGroupActionState = [=]() {
        const QString groupId = selectedGroupNoticeEntryId(noticeList);
        const bool hasSelection = noticeList->currentItem() != nullptr;
        const bool isCreateEntry = isGroupCreateEntryId(groupId);
        const bool canInspectGroup = hasSelection && !isCreateEntry;
        const bool hasSearchKeyword = !searchEdit->text().trimmed().isEmpty();
        openBtn->setEnabled(hasSelection);
        openBtn->setText(isCreateEntry ? "创建并进入群聊" : (groupId.isEmpty() ? "进入公共聊天室" : "进入选中群聊"));
        openBtn->setToolTip(!hasSelection
            ? "选择群聊后可进入"
            : (isCreateEntry ? "按当前关键词创建新群并立即进入" : "进入当前选中的公共聊天室或本地群聊"));
        copyBtn->setEnabled(canInspectGroup);
        copyBtn->setToolTip(canInspectGroup ? "复制当前选中群聊的群号" : "待创建群聊没有群号，请先进入创建");
        cardBtn->setEnabled(canInspectGroup);
        cardBtn->setToolTip(canInspectGroup ? "复制群名、群号、成员数和公告摘要" : "待创建群聊没有名片，请先进入创建");
        announceBtn->setEnabled(canInspectGroup);
        announceBtn->setToolTip(canInspectGroup ? "复制当前选中群聊的公告内容" : "待创建群聊没有公告，请先进入创建");
        memberBtn->setEnabled(canInspectGroup);
        memberBtn->setToolTip(canInspectGroup ? "复制当前选中群聊的全部成员列表" : "待创建群聊没有成员列表，请先进入创建");
        onlineMemberBtn->setEnabled(canInspectGroup);
        onlineMemberBtn->setToolTip(canInspectGroup ? "复制当前群里在线成员的 QQ 和昵称" : "待创建群聊没有在线成员，请先进入创建");
        inviteTextBtn->setEnabled(hasSelection || hasSearchKeyword);
        inviteTextBtn->setToolTip(hasSelection || hasSearchKeyword ? "复制一段可直接发给好友的入群邀请" : "当前没有可邀请的群聊");
        copyGroupMediaPackBtn->setEnabled(hasSelection || hasSearchKeyword);
        copyGroupMediaPackBtn->setToolTip(hasSelection || hasSearchKeyword ? "复制群聊媒体发送前的目标、成员和话术摘要" : "当前没有可复制的群聊媒体包");
        copyGroupBatchPlanBtn->setEnabled(noticeList->count() > 0);
        copyGroupBatchPlanBtn->setToolTip(noticeList->count() > 0 ? "复制群聊批量发送图片、视频或文件的操作清单" : "当前没有可整理的群聊");
        copyMediaGuideBtn->setEnabled(hasSelection || hasSearchKeyword);
        copyMediaGuideBtn->setToolTip(hasSelection || hasSearchKeyword ? "复制群聊中发送图片、视频和文件的简短指南" : "当前没有可复制的上传指南");
        hintLabel->setText(!hasSelection
            ? "先选择群聊后再进入或复制信息"
            : (isCreateEntry ? "双击可按当前关键词创建新群并进入" : "双击群通知可直接进入群聊"));
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
    connect(copyBtn, &QPushButton::clicked, &dialog, [this, noticeList]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制群号的群聊",
                                                 "待创建群聊还没有群号，请先进入创建",
                                                 &groupId)) {
            return;
        }
        QString copyId = groupId.isEmpty() ? "公共聊天室" : groupId.mid(QString("local_group_").size());
        QApplication::clipboard()->setText(copyId);
        ui->statusbar->showMessage("群号已复制: " + copyId, 2500);
    });
    connect(cardBtn, &QPushButton::clicked, &dialog, [this, noticeList]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制名片的群聊",
                                                 "待创建群聊还没有名片，请先进入创建",
                                                 &groupId)) {
            return;
        }
        QListWidgetItem* current = noticeList->currentItem();
        QString card;
        if (groupId.isEmpty()) {
            card = QString("公共聊天室\n当前账号:%1\n在线成员:%2").arg(m_currentUserId).arg(m_knownUsers.size());
        } else {
            QString groupNumber = groupId.mid(QString("local_group_").size());
            QString groupName = m_localGroupNames.value(groupId, "群聊");
            QStringList members = m_localGroupMembers.value(groupId);
            QString announcement = m_localGroupAnnouncements.value(groupId, current->text().section('\n', 2));
            card = QString("群聊 QQ:%1\n%2\n成员:%3\n公告:%4")
                .arg(groupNumber, groupName, QString::number(members.size()), announcement);
        }
        QApplication::clipboard()->setText(card);
        ui->statusbar->showMessage("群名片已复制", 1800);
    });
    connect(announceBtn, &QPushButton::clicked, &dialog, [this, noticeList]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制公告的群聊",
                                                 "待创建群聊还没有公告，请先进入创建",
                                                 &groupId)) {
            return;
        }
        QListWidgetItem* current = noticeList->currentItem();
        QString announcement = groupId.isEmpty()
            ? "你已加入默认群聊，可直接发送消息、图片和文件。"
            : m_localGroupAnnouncements.value(groupId, current->text().section('\n', 2));
        QApplication::clipboard()->setText(announcement);
        ui->statusbar->showMessage("群公告已复制", 1800);
    });
    connect(inviteTextBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        const QString groupId = selectedGroupNoticeEntryId(noticeList);
        QString groupName = searchEdit->text().trimmed();
        QString groupNumber = "公共聊天室";
        if (isGroupCreateEntryId(groupId)) {
            groupName = groupCreateEntryName(groupId);
            groupNumber = "待创建";
        } else if (groupId.startsWith("local_group_")) {
            groupName = m_localGroupNames.value(groupId, "群聊");
            groupNumber = groupId.mid(QString("local_group_").size());
        } else if (groupName.isEmpty()) {
            groupName = "公共聊天室";
        }
        QString text = QString("我邀请你加入群聊“%1”（群号:%2）。我是 %3（QQ:%4），进群后可以一起聊天、发图片和传文件。")
            .arg(groupName, groupNumber, m_currentUserName, m_currentUserId);
        QApplication::clipboard()->setText(text);
        ui->statusbar->showMessage("入群邀请话术已复制", 2200);
    });
    connect(memberBtn, &QPushButton::clicked, &dialog, [this, noticeList, publicGroupMemberIds]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制成员的群聊",
                                                 "待创建群聊还没有成员列表，请先进入创建",
                                                 &groupId)) {
            return;
        }
        QStringList members = groupId.isEmpty() ? publicGroupMemberIds() : m_localGroupMembers.value(groupId);
        if (members.isEmpty()) members << m_currentUserId;
        QStringList cards;
        for (const QString& id : members) {
            cards << QString("QQ:%1 昵称:%2 状态:%3").arg(id, contactDisplayName(id), isContactOnline(id) || id == m_currentUserId ? "在线" : "离线");
        }
        QApplication::clipboard()->setText(cards.join('\n'));
        ui->statusbar->showMessage(QString("已复制 %1 个群成员").arg(cards.size()), 2200);
    });
    connect(onlineMemberBtn, &QPushButton::clicked, &dialog, [this, noticeList, publicGroupMemberIds]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制在线成员的群聊",
                                                 "待创建群聊还没有在线成员，请先进入创建",
                                                 &groupId)) {
            return;
        }
        QStringList members = groupId.isEmpty() ? publicGroupMemberIds() : m_localGroupMembers.value(groupId);
        if (members.isEmpty()) members << m_currentUserId;
        QStringList cards;
        for (const QString& id : members) {
            if (id != m_currentUserId && !isContactOnline(id)) continue;
            cards << QString("在线群成员 QQ:%1 昵称:%2").arg(id, contactDisplayName(id));
        }
        if (cards.isEmpty()) {
            ui->statusbar->showMessage("当前群聊没有在线成员可复制", 2200);
            return;
        }
        QApplication::clipboard()->setText(cards.join('\n'));
        ui->statusbar->showMessage(QString("已复制 %1 个在线群成员").arg(cards.size()), 2200);
    });
    connect(copyGroupMediaPackBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        const QString groupId = selectedGroupNoticeEntryId(noticeList);
        QString groupName = searchEdit->text().trimmed();
        QString groupNumber = "公共聊天室";
        int memberCount = m_knownUsers.size();
        if (isGroupCreateEntryId(groupId)) {
            groupName = groupCreateEntryName(groupId);
            groupNumber = "待创建";
            memberCount = 1;
        } else if (groupId.startsWith("local_group_")) {
            groupName = m_localGroupNames.value(groupId, "群聊");
            groupNumber = groupId.mid(QString("local_group_").size());
            memberCount = qMax(1, m_localGroupMembers.value(groupId).size());
        } else if (groupName.isEmpty()) {
            groupName = "公共聊天室";
        }
        QStringList rows;
        rows << QString("群媒体包 · %1 · 群号:%2").arg(groupName, groupNumber);
        rows << QString("我的QQ:%1 · 昵称:%2 · 群成员:%3").arg(m_currentUserId, m_currentUserName, QString::number(memberCount));
        rows << "群内可直接发送图片/视频，也可用闪传文件发送文档和压缩包";
        rows << "支持 png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm 和常用文档压缩包";
        rows << QString("入群话术：我邀请你加入群聊“%1”（群号:%2），进群后可以一起聊天、发图片和传文件。").arg(groupName, groupNumber);
        rows << QString("查收话术：我已发送媒体文件到群聊“%1”，请注意查收。").arg(groupName);
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("群媒体包已复制", 2200);
    });
    connect(copyGroupBatchPlanBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit, publicGroupMemberIds]() {
        QStringList groups;
        int totalMembers = 0;
        int onlineMembers = 0;
        const QStringList visibleIds = visibleGroupNoticeIds(noticeList);
        for (const QString& id : visibleIds) {
            if (isGroupCreateEntryId(id)) {
                groups << QString("待创建群:%1").arg(groupCreateEntryName(id));
                ++totalMembers;
                ++onlineMembers;
            } else if (id.isEmpty()) {
                const QStringList publicMembers = publicGroupMemberIds();
                totalMembers += publicMembers.size();
                onlineMembers += publicMembers.size();
                groups << QString("公共聊天室(成员:%1,在线:%2)")
                    .arg(QString::number(publicMembers.size()), QString::number(publicMembers.size()));
            } else if (id.startsWith("local_group_")) {
                QStringList members = m_localGroupMembers.value(id);
                int groupOnline = 0;
                for (const QString& memberId : members) {
                    if (memberId == m_currentUserId || isContactOnline(memberId)) ++groupOnline;
                }
                totalMembers += qMax(1, members.size());
                onlineMembers += groupOnline;
                groups << QString("%1(群号:%2,成员:%3,在线:%4)")
                    .arg(m_localGroupNames.value(id, "群聊"), id.mid(QString("local_group_").size()), QString::number(qMax(1, members.size())), QString::number(groupOnline));
            }
        }
        QString keyword = searchEdit->text().trimmed();
        QStringList rows;
        rows << QString("群批量媒体计划 · 筛选:%1").arg(keyword.isEmpty() ? "全部群通知" : keyword);
        rows << QString("我的QQ:%1 · 昵称:%2 · 可见群:%3 · 成员:%4 · 在线:%5")
            .arg(m_currentUserId, m_currentUserName, QString::number(groups.size()), QString::number(totalMembers), QString::number(onlineMembers));
        rows << QString("群聊目标:%1").arg(groups.isEmpty() ? "无可见群聊" : groups.join("、"));
        rows << "1. 先处理待创建群或打开已有群聊";
        rows << "2. 图片/GIF/视频走图片视频入口，文档和压缩包走闪传文件";
        rows << "3. 发送后复制群媒体包、上传指南和查收话术给群成员";
        rows << "4. 可按筛选关键词分批发送，优先覆盖在线成员较多的群聊";
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("群批量媒体计划已复制", 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        const QString groupId = selectedGroupNoticeEntryId(noticeList);
        QString groupName = searchEdit->text().trimmed();
        QString groupNumber = "公共聊天室";
        int memberCount = m_knownUsers.size();
        if (isGroupCreateEntryId(groupId)) {
            groupName = groupCreateEntryName(groupId);
            groupNumber = "待创建";
            memberCount = 1;
        } else if (groupId.startsWith("local_group_")) {
            groupName = m_localGroupNames.value(groupId, "群聊");
            groupNumber = groupId.mid(QString("local_group_").size());
            memberCount = qMax(1, m_localGroupMembers.value(groupId).size());
        } else if (groupName.isEmpty()) {
            groupName = "公共聊天室";
        }
        QStringList rows;
        rows << QString("群上传指南 · %1 · 群号:%2").arg(groupName, groupNumber);
        rows << QString("我的QQ:%1 · 昵称:%2 · 群成员:%3").arg(m_currentUserId, m_currentUserName, QString::number(memberCount));
        rows << "图片/视频：支持 png、jpg、gif、mp4、mov、avi、mkv、wmv、flv、webm";
        rows << "闪传文件：支持文档、压缩包和媒体文件";
        rows << "聊天记录右键可复制媒体卡片和查收话术";
        rows << "可先复制入群话术邀请好友，进群后直接发送图片/视频/文件";
        QApplication::clipboard()->setText(rows.join('\n'));
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
    item->setBackground(QColor(245, 247, 250));
    item->setForeground(Qt::darkGray);
    m_chatModel->appendRow(item);
    ui->chatListView->scrollToBottom();
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

bool MainWindow::selectTransferFile(const QString& dialogTitle,
                                    const QString& filters,
                                    const QString& confirmKind,
                                    const QString& canceledHint,
                                    const QString& canceledStatus,
                                    QString* filePath,
                                    QFileInfo* fileInfo,
                                    QString* fileSize) {
    if (!filePath || !fileInfo) {
        return false;
    }

    *filePath = QFileDialog::getOpenFileName(this, dialogTitle, lastTransferDirectory(), filters);
    if (filePath->isEmpty()) {
        ui->chatHintLabel->setText(canceledHint);
        ui->statusbar->showMessage(canceledStatus, 1600);
        return false;
    }
    rememberTransferDirectory(*filePath);

    *fileInfo = QFileInfo(*filePath);
    QString failureMessage;
    if (!confirmTransferFile(this, *fileInfo, confirmKind, &failureMessage)) {
        if (!failureMessage.isEmpty()) {
            ui->chatHintLabel->setText(failureMessage);
            ui->statusbar->showMessage(failureMessage, 2600);
        }
        return false;
    }

    if (fileSize) {
        *fileSize = humanFileSize(fileInfo->size());
    }
    return true;
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

void MainWindow::appendMediaPreviewItem(const QString& text,
                                        const QPixmap& pixmap,
                                        bool isVideo,
                                        bool alignRight) {
    QStandardItem* previewItem = new QStandardItem(text);
    if (!isVideo && !pixmap.isNull()) {
        previewItem->setData(pixmap.scaled(180, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::DecorationRole);
    }
    previewItem->setEditable(false);
    previewItem->setBackground(isVideo ? QColor(245, 240, 255) : QColor(246, 250, 253));
    if (isVideo) {
        previewItem->setForeground(QColor(126, 87, 194));
    }
    if (alignRight) {
        previewItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    m_chatModel->appendRow(previewItem);
}

void MainWindow::appendReceivedTransferReceiptItems(const QString& cardText,
                                                    const QString& cardToolTip,
                                                    const QString& replyText,
                                                    const QString& replyToolTip) {
    QStandardItem* cardItem = new QStandardItem(cardText);
    cardItem->setEditable(false);
    cardItem->setData(cardToolTip, Qt::ToolTipRole);
    cardItem->setForeground(QColor(0, 121, 107));
    cardItem->setBackground(QColor(232, 248, 245));
    m_chatModel->appendRow(cardItem);

    QStandardItem* replyItem = new QStandardItem(replyText);
    replyItem->setEditable(false);
    replyItem->setData(replyToolTip, Qt::ToolTipRole);
    replyItem->setForeground(QColor(86, 116, 130));
    replyItem->setBackground(QColor(246, 251, 253));
    m_chatModel->appendRow(replyItem);
}

void MainWindow::appendReceivedTransferSavedEvidence(const ReceivedTransferContext& context,
                                                     const QString& displayName) {
    const QString savedFileTip = QString("双击打开文件；右键可复制保存路径或打开目录\n%1").arg(context.savePath);
    appendReceivedTransferSavedItem(
        QString("%1已自动保存: %2 · %3%4%5").arg(context.kind,
                                               context.savePath,
                                               context.receivedSize,
                                               context.manifestSuffix,
                                               context.integritySuffix),
        savedFileTip,
        context.integrityText.startsWith("完整性校验失败"));
    appendReceivedTransferReceiptItems(
        QString("%1接收卡片 · %2 · %3 · 来自 %4 · 已保存到下载目录%5%6").arg(context.kind,
                                                                       context.receivedName,
                                                                       context.receivedSize,
                                                                       displayName,
                                                                       context.manifestSuffix,
                                                                       context.integritySuffix),
        QString("%1已保存到：%2").arg(context.kind, context.savePath),
        QString("回执话术 · 已收到%1 %2（%3%4），%5，保存路径：%6 · 右键聊天记录可复制或打开保存目录").arg(context.kind,
                                                                                                           context.receivedName,
                                                                                                           context.receivedSize,
                                                                                                           context.manifestSuffix,
                                                                                                           context.integrityText,
                                                                                                           context.savePath),
        savedFileTip);
}

void MainWindow::appendReceivedTransferSaveFailedEvidence(const ReceivedTransferContext& context,
                                                          const QString& displayName,
                                                          const QString& transferId,
                                                          qint64 receivedBytes,
                                                          qint64 totalBytes) {
    appendReceivedTransferSaveFailedItem(QString("%1保存失败 · %2 · %3 · 请检查下载目录权限")
        .arg(context.kind, context.receivedName, context.receivedSize));
    applyReceivedTransferSaveStatus(context.kind,
                                    context.receivedName,
                                    displayName,
                                    context.receivedSize,
                                    context.manifestSuffix,
                                    context.integritySuffix,
                                    transferId,
                                    receivedBytes,
                                    totalBytes,
                                    false,
                                    false);
}

bool MainWindow::persistReceivedTransferPayload(const ReceivedTransferContext& context,
                                                const QString& displayName,
                                                const QString& transferId,
                                                const QByteArray& fileData,
                                                qint64 totalBytes) {
    QFile file(context.savePath);
    if (!file.open(QIODevice::WriteOnly)) {
        appendReceivedTransferSaveFailedEvidence(context,
                                                 displayName,
                                                 transferId,
                                                 fileData.size(),
                                                 totalBytes);
        return false;
    }

    file.write(fileData);
    file.close();
    appendReceivedTransferSavedEvidence(context, displayName);
    applyReceivedTransferSaveStatus(context.kind,
                                    context.receivedName,
                                    displayName,
                                    context.receivedSize,
                                    context.manifestSuffix,
                                    context.integritySuffix,
                                    transferId,
                                    fileData.size(),
                                    totalBytes,
                                    true,
                                    context.integrityText.startsWith("完整性校验失败"));
    return true;
}

void MainWindow::appendReceivedTransferSavedItem(const QString& text,
                                                 const QString& toolTip,
                                                 bool integrityFailed) {
    QStandardItem* savedItem = new QStandardItem(text);
    savedItem->setEditable(false);
    savedItem->setData(toolTip, Qt::ToolTipRole);
    savedItem->setForeground(integrityFailed ? QColor(180, 70, 70) : Qt::darkGreen);
    savedItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_chatModel->appendRow(savedItem);
}

void MainWindow::appendReceivedTransferSaveFailedItem(const QString& text) {
    QStandardItem* failedItem = new QStandardItem(text);
    failedItem->setEditable(false);
    failedItem->setForeground(QColor(180, 70, 70));
    failedItem->setBackground(QColor(255, 245, 245));
    m_chatModel->appendRow(failedItem);
}

MainWindow::ReceivedTransferContext MainWindow::receivedTransferContext(const Message& msg,
                                                                        const QString& kind,
                                                                        const QString& fallbackName,
                                                                        const QString& downloadSubdir,
                                                                        const QString& displayName) const {
    ReceivedTransferContext context;
    context.kind = kind;
    context.receivedName = safeReceivedFileName(msg.fileName, fallbackName);
    context.receivedSize = humanFileSize(msg.fileData.size());
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

    const QString saveDirPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
        + QStringLiteral("/QtNetworkChat/")
        + downloadSubdir;
    QDir().mkpath(saveDirPath);
    context.savePath = uniqueReceivedSavePath(saveDirPath, context.receivedName);
    return context;
}

void MainWindow::loadHistory(const QString& peerId) {
    if (peerId.isEmpty()) return;

    const QStringList rows = m_historyService.recentRows(peerId, MAX_HISTORY_LINES);
    for (const QString& line : rows) {
        QStandardItem* item = new QStandardItem(line);
        item->setEditable(false);
        item->setBackground(QColor(250, 252, 254));
        item->setForeground(Qt::gray);
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
        ui->friendNoticeBtn->setText(m_pendingFriendRequests.isEmpty() ? "好友通知" : QString("好友通知 %1").arg(m_pendingFriendRequests.size()));
        ui->friendNoticeBtn->setToolTip(m_pendingFriendRequests.isEmpty() ? "查看并处理好友申请" : QString("有 %1 个好友申请待处理").arg(m_pendingFriendRequests.size()));
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

    const FriendNoticeUiState noticeState = m_friendManager.noticeUiState(m_pendingFriendRequests.size());
    const GroupNoticeUiState groupNoticeState = m_groupManager.noticeUiState(m_localGroupIds.size());
    ui->friendNoticeBtn->setText(noticeState.text);
    ui->friendNoticeBtn->setToolTip(noticeState.toolTip);
    ui->groupNoticeBtn->setText(groupNoticeState.text);
    ui->groupNoticeBtn->setToolTip(groupNoticeState.toolTip);

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
    int onlineFriendCount = 0;
    int visiblePendingOutgoing = 0;
    int visibleStrangers = 0;
    auto matchesFilter = [this](const QString& id, const QString& name) {
        return m_friendManager.matchesFilter(id, name, m_contactFilter);
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
            ++onlineFriendCount;
        }
        const bool isPending = m_pendingOutgoingFriendRequests.contains(user.id);
        if (isPending && !isFriend) ++visiblePendingOutgoing;
        const QString marker = isFriend ? "★" : "○";
        const QString stateSuffix = isFriend ? " [在线]" : (isPending ? " [申请中]" : "");
        QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n   %3%4").arg(marker, user.id, user.name, stateSuffix));
        item->setData(user.id, Qt::UserRole + 1);
        item->setForeground(isFriend ? Qt::white : (isPending ? QColor(255, 225, 160) : QColor(220, 240, 255)));
        m_userListModel->appendRow(item);
        ++visibleCount;
        ++visibleOnlineUsers;
        if (!isFriend && !isPending) ++visibleStrangers;
    }

    const QString pendingPart = visiblePendingOutgoing > 0 ? QString(" · 申请中%1").arg(visiblePendingOutgoing) : QString();
    ui->onlineTitleLabel->setText(m_contactFilter.isEmpty()
        ? QString("联系人 · 好友%1/%2在线 · 群聊%3%4 · 陌生人%5").arg(onlineFriendCount).arg(m_friendIds.size()).arg(visibleGroups).arg(pendingPart).arg(visibleStrangers)
        : QString("联系人 · 匹配%1 · 好友%2 · 群聊%3 · 在线%4%5 · 陌生人%6").arg(visibleCount).arg(visibleFriends + onlineFriendCount).arg(visibleGroups).arg(visibleOnlineUsers).arg(pendingPart).arg(visibleStrangers));

    if (visibleCount == 0 && !m_contactFilter.isEmpty()) {
        QStandardItem* addItem = new QStandardItem(QString("搜索并发送申请 QQ:%1\n   回车或双击查找好友").arg(m_contactFilter));
        addItem->setData("search_add:" + m_contactFilter, Qt::UserRole + 1);
        addItem->setForeground(QColor(255, 255, 255));
        addItem->setBackground(QColor(18, 183, 245));
        m_userListModel->appendRow(addItem);
        QStandardItem* groupItem = new QStandardItem(QString("创建群聊:%1\n   双击立即建群并进入").arg(m_contactFilter));
        groupItem->setData("create_group:" + m_contactFilter, Qt::UserRole + 1);
        groupItem->setForeground(QColor(255, 255, 255));
        groupItem->setBackground(QColor(36, 203, 162));
        m_userListModel->appendRow(groupItem);
        ui->onlineTitleLabel->setText(QString("联系人 · 未匹配 · 可搜索QQ或建群:%1").arg(m_contactFilter));
    }
}

void MainWindow::onContactSearchChanged(const QString& text) {
    m_contactFilter = text.trimmed();
    refreshFriendList();
    if (!m_contactFilter.isEmpty()) {
        ui->statusbar->showMessage(QString("QQ搜索:%1 · 无结果可双击搜索申请或建群").arg(m_contactFilter), 1800);
    } else {
        ui->statusbar->showMessage(QString("联系人已显示 · 好友%1 · 本地群%2 · 在线%3").arg(m_friendIds.size()).arg(m_localGroupIds.size()).arg(m_knownUsers.size()), 1200);
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
            QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n%3 · %4 · %5")
                .arg(display.role, memberId, name, display.state, display.actionText));
            item->setData(memberId, Qt::UserRole + 1);
            item->setEditable(false);
            item->setForeground(memberId == m_currentUserId ? QColor(18, 150, 247) : (isFriend ? QColor(20, 92, 160) : (isPending ? QColor(170, 110, 20) : QColor(38, 50, 56))));
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
            QStandardItem* addItem = new QStandardItem(QString("邀请 QQ:%1\n双击自动加入当前群聊").arg(filter));
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
            const GroupMemberDisplayState display = m_groupManager.memberDisplayState(
                memberId, m_currentUserId, ownerId, serverRole, isFriend, isPending, online, true);
            QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n%3 · %4 · %5")
                .arg(display.role, memberId, name, display.state, display.actionText));
            item->setData(memberId, Qt::UserRole + 1);
            item->setEditable(false);
            item->setForeground(display.isOwner ? QColor(156, 98, 0) : (memberId == m_currentUserId ? QColor(18, 150, 247) : (isFriend ? QColor(18, 150, 247) : (isPending ? QColor(170, 110, 20) : QColor(38, 50, 56)))));
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
    int visibleMembers = 0;
    int friendMembers = 0;
    int pendingMembers = 0;
    int onlineMembers = 1;
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
        bool isPending = !isFriend && m_pendingOutgoingFriendRequests.contains(user.id);
        if (isFriend) ++friendMembers;
        if (isPending) ++pendingMembers;
        ++onlineMembers;
        QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n%3 · 在线 · %4").arg(isFriend ? "好友" : (isPending ? "申请中" : "成员"), user.id, user.name, isFriend ? "已是好友" : (isPending ? "等待确认" : "双击发送申请")));
        item->setData(user.id, Qt::UserRole + 1);
        item->setEditable(false);
        item->setForeground(isFriend ? QColor(18, 150, 247) : (isPending ? QColor(170, 110, 20) : QColor(38, 50, 56)));
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
}

void MainWindow::loadAvatar() {
    const QString avatarPath = getAvatarFilePath();
    QPixmap pixmap(avatarPath);
    if (!pixmap.isNull()) {
        ui->avatarLabel->setPixmap(squareAvatarPixmap(pixmap, ui->avatarLabel->width()));
        QFileInfo info(avatarPath);
        const QString avatarTip = QString("当前头像：本地头像 · %1；点击“换头像”重新选择")
                                      .arg(humanFileSize(info.size()));
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
