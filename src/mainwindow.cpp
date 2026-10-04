#include "mainwindow.h"
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

namespace {
struct StoredChatMessage {
    QString timestamp;
    QString senderName;
    QString body;
    bool system = false;
};

// History predates the QQNT message model and stores a human-readable line.
// Keep that storage format for compatibility, but never present its envelope as
// the body of a chat bubble.
StoredChatMessage parseStoredChatMessage(const QString& line) {
    const QRegularExpression systemPattern(
        QStringLiteral(R"(^\s*\[([^\]]+)\]\s*\[系统\]\s*(.*)$)"));
    const QRegularExpression normalPattern(
        QStringLiteral(R"(^\s*\[([^\]]+)\]\s*<([^>]+)>\s*(.*)$)"));

    QString normalizedLine = line.trimmed();
    // Export rows include the database timestamp before the original chat
    // envelope. Stored rows and legacy files only contain the envelope.
    normalizedLine.remove(QRegularExpression(
        QStringLiteral(R"(^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}\s*\|\s*)")));

    QRegularExpressionMatch match = systemPattern.match(normalizedLine);
    if (match.hasMatch()) {
        return {match.captured(1).trimmed(), QStringLiteral("系统"),
                match.captured(2).trimmed(), true};
    }

    match = normalPattern.match(normalizedLine);
    if (match.hasMatch()) {
        QString body = match.captured(3).trimmed();
        body.remove(QRegularExpression(QStringLiteral(R"(^\[(?:私聊|端到端加密)\]\s*)")));
        return {match.captured(1).trimmed(), match.captured(2).trimmed(), body, false};
    }

    return {QString(), QString(), normalizedLine, false};
}

QString bubbleTimestamp(const QString& timestamp) {
    const QTime time = QTime::fromString(timestamp, QStringLiteral("hh:mm:ss"));
    if (time.isValid()) {
        return time.toString(QStringLiteral("HH:mm"));
    }
    return timestamp.left(5);
}

QPixmap loadChatImagePreview(const QString& filePath) {
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            image = QImage::fromData(file.readAll());
        }
    }
    if (!image.isNull()) {
        return QPixmap::fromImage(image);
    }
    QPixmap pixmap;
    pixmap.load(filePath);
    return pixmap;
}

QPixmap roundAvatarPixmap(const QPixmap& source, int side);
QIcon generatedPeerAvatarIcon(const QString& displayName, const QString& seedId, int side);

class ChatMessageDelegate : public QStyledItemDelegate {
public:
    explicit ChatMessageDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const bool system = index.data(ChatBubbleSystemRole).toBool();
        const int width = qMax(360, option.rect.width() > 0 ? option.rect.width() : 640);
        QFontMetrics fm(option.font);
        const int maxTextWidth = system ? width - 80 : qMin(520, qMax(240, width - 156));
        const QString mediaKind = index.data(ChatBubbleMediaKindRole).toString();
        const QVariant mediaPreviewData = index.data(ChatBubbleMediaPreviewRole);
        const bool hasImagePreview = mediaKind == QLatin1String("image")
            && mediaPreviewData.canConvert<QPixmap>()
            && !mediaPreviewData.value<QPixmap>().isNull();
        const QRect textBounds = fm.boundingRect(QRect(0, 0, maxTextWidth, 1000),
                                                 Qt::TextWordWrap,
                                                 index.data(Qt::DisplayRole).toString());
        if (hasImagePreview) {
            QSize mediaSize = mediaPreviewData.value<QPixmap>().size();
            mediaSize.scale(qMin(300, maxTextWidth), 220, Qt::KeepAspectRatio);
            return QSize(width, qMax(120, mediaSize.height() + textBounds.height() + 44));
        }
        return QSize(width, qMax(system ? 42 : 58, textBounds.height() + (system ? 22 : 30)));
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const QString text = index.data(Qt::DisplayRole).toString();
        const bool outgoing = index.data(ChatBubbleOutgoingRole).toBool();
        const bool system = index.data(ChatBubbleSystemRole).toBool();
        const QString mediaKind = index.data(ChatBubbleMediaKindRole).toString();
        const bool hasImagePreview = mediaKind == QLatin1String("image")
            && index.data(ChatBubbleMediaPreviewRole).canConvert<QPixmap>()
            && !index.data(ChatBubbleMediaPreviewRole).value<QPixmap>().isNull();
        const QRect rect = option.rect.adjusted(10, 4, -10, -4);
        QFontMetrics fm(option.font);

        if (system) {
            const int maxWidth = qMin(rect.width() - 40, 620);
            const QRect textRect = fm.boundingRect(QRect(0, 0, maxWidth, 1000), Qt::TextWordWrap, text);
            const QRect bubble(QPoint(rect.center().x() - textRect.width() / 2 - 14, rect.top() + 5),
                               QSize(textRect.width() + 28, textRect.height() + 14));
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(245, 247, 250));
            painter->drawRoundedRect(bubble, 13, 13);
            painter->setPen(QColor(102, 116, 130));
            painter->drawText(bubble.adjusted(14, 7, -14, -7), Qt::TextWordWrap | Qt::AlignCenter, text);
            painter->restore();
            return;
        }

        const int avatarSize = 38;
        const int sideInset = 14;
        const int avatarX = outgoing ? rect.right() - avatarSize - sideInset : rect.left() + sideInset;
        const QRect avatarRect(avatarX, rect.top() + 8, avatarSize, avatarSize);
        QPixmap avatar;
        const QString avatarPath = index.data(ChatBubbleAvatarPathRole).toString();
        if (!avatarPath.isEmpty()) {
            avatar.load(avatarPath);
        }
        if (avatar.isNull()) {
            avatar = generatedPeerAvatarIcon(index.data(ChatBubbleSenderNameRole).toString(),
                                             index.data(ChatBubbleSenderIdRole).toString(),
                                             avatarSize).pixmap(avatarSize, avatarSize);
        } else {
            avatar = roundAvatarPixmap(avatar, avatarSize);
        }
        painter->drawPixmap(avatarRect, avatar);

        const int maxBubbleWidth = qMin(560, qMax(250, rect.width() - avatarSize - 88));
        QSize bubbleSize;
        QRect textBounds;
        QPixmap mediaPreview;
        QSize mediaSize;
        if (hasImagePreview) {
            mediaPreview = index.data(ChatBubbleMediaPreviewRole).value<QPixmap>();
            mediaSize = mediaPreview.size();
            const int maxPreviewWidth = qMin(300, maxBubbleWidth - 24);
            const int maxPreviewHeight = 220;
            mediaSize.scale(maxPreviewWidth, maxPreviewHeight, Qt::KeepAspectRatio);
            textBounds = fm.boundingRect(QRect(0, 0, maxPreviewWidth, 1000), Qt::TextWordWrap, text);
            bubbleSize = QSize(qMax(mediaSize.width(), textBounds.width()) + 24,
                               mediaSize.height() + textBounds.height() + 30);
        } else {
            textBounds = fm.boundingRect(QRect(0, 0, maxBubbleWidth, 1000), Qt::TextWordWrap, text);
            bubbleSize = QSize(textBounds.width() + 28, textBounds.height() + 20);
        }
        const int bubbleX = outgoing
            ? avatarRect.left() - 10 - bubbleSize.width()
            : avatarRect.right() + 10;
        const QRect bubbleRect(QPoint(bubbleX, rect.top() + 6), bubbleSize);

        painter->setPen(Qt::NoPen);
        painter->setBrush(outgoing ? QColor(218, 241, 255) : QColor(246, 250, 253));
        painter->drawRoundedRect(bubbleRect, 14, 14);
        painter->setPen(outgoing ? QColor(20, 92, 160) : QColor(38, 50, 56));
        if (hasImagePreview) {
            const QRect imageRect(bubbleRect.left() + 12,
                                  bubbleRect.top() + 12,
                                  mediaSize.width(),
                                  mediaSize.height());
            painter->drawPixmap(imageRect, mediaPreview);
            painter->setPen(QColor(86, 116, 130));
            painter->drawText(QRect(bubbleRect.left() + 12,
                                    imageRect.bottom() + 8,
                                    bubbleRect.width() - 24,
                                    textBounds.height() + 4),
                              Qt::TextWordWrap,
                              text);
        } else {
            painter->drawText(bubbleRect.adjusted(14, 10, -14, -10), Qt::TextWordWrap, text);
        }

        painter->restore();
    }
};

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
    // Transfer progress is useful in the status bar/workspace card, but placing
    // every image chunk event in the chat created the dark diagnostic bars seen
    // above image and screenshot bubbles.
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    const bool image = QStringList{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                   QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("webp")}.contains(suffix);
    if (!image) {
        appendSystemMessage(event.message);
    }
    ui->chatHintLabel->setText(event.chatHintText);
    ui->statusbar->showMessage(event.statusBarMessage, event.statusBarTimeoutMs);
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
    ui->userListView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    ui->chatListView->setModel(m_chatModel);
    ui->chatListView->setItemDelegate(new ChatMessageDelegate(ui->chatListView));
    ui->chatListView->setIconSize(QSize(34, 34));
    ui->chatListView->setSpacing(8);
    ui->chatListView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->chatListView->setToolTip("右键消息可复制、引用和转发；双击带保存路径的文件记录可直接打开文件");

    m_groupMemberModel->setHorizontalHeaderLabels({"群成员"});
    ui->groupMemberListView->setModel(m_groupMemberModel);
    ui->groupMemberListView->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->messageEdit->setPlaceholderText("输入消息... (Enter 发送，Shift/Ctrl+Enter 换行，Esc 清空草稿)");
    ui->messageEdit->setFocus();
    ui->messageEdit->installEventFilter(this);
    ui->messageEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->chatSubtitleLabel->setText("Redis 在线工作台 · 多实例状态同步中");
    ui->chatStatusBadgeLabel->setText("公共群会话");
    ui->composerStateLabel->setText("Enter 发送，Shift/Ctrl+Enter 换行，Esc 清空草稿");
    ui->sideSummaryTitleLabel->setText("工作台概览");
    ui->sideSummaryStatsLabel->setText("好友 0 · 群聊 0 · 在线 0");
    ui->sideSummaryStateLabel->setText("等待服务端在线状态同步");
    ui->groupOverviewTitleLabel->setText("会话状态");
    ui->groupOverviewStateLabel->setText("公共群在线视图已准备");
    ui->groupOverviewMetaLabel->setText("成员面板会随着当前会话自动刷新");
    ui->transferOverviewTitleLabel->setText("文件传输");
    setTransferWorkspaceState(m_transferManager.idleWorkspaceState(false, false, false));
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
        QAction* selected = menu.exec(ui->chatListView->viewport()->mapToGlobal(pos));
        if (!selected) return;
        handleChatContextCommand(selected->data().toString(), text, savedFileState, index);
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
        QAction* viewProfileAction = menu.addAction("查看资料");
        QAction* groupNicknameAction = menu.addAction("修改群昵称");
        QAction* copyAction = menu.addAction("复制QQ号");
        QAction* profileAction = menu.addAction("复制名片");
        QAction* copyAllAction = menu.addAction("复制群成员列表");
        QAction* copyOnlineAction = menu.addAction("复制在线群成员");
        QAction* renameAction = menu.addAction("设置备注");
        QAction* promoteAdminAction = nullptr;
        QAction* demoteAdminAction = nullptr;
        if (isServerPublicGroup) {
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
        } else if (selected == viewProfileAction) {
            const QString groupName = isLocalGroup
                ? m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊"))
                : m_serverGroupNames.value(QStringLiteral("public"), QStringLiteral("公共聊天室"));
            QString role = QStringLiteral("成员");
            if (isLocalGroup && groupOwnerId(m_privateChatTarget) == memberId) {
                role = QStringLiteral("群主");
            } else if (isServerPublicGroup) {
                const QString serverRole = m_serverGroupMemberRoles.value(memberId);
                if (m_serverGroupOwners.value(QStringLiteral("public")) == memberId) {
                    role = QStringLiteral("群主");
                } else if (serverRole == QStringLiteral("admin")) {
                    role = QStringLiteral("管理员");
                }
            }
            MemberProfileCard card(this);
            card.setMemberInfo(memberId, contactDisplayName(memberId), role, groupName,
                               memberId == m_currentUserId || isContactOnline(memberId));
            connect(&card, &MemberProfileCard::sendMessageRequested, this, [this](const QString& id) {
                ensureFriendRequestQueued(id,
                                          QStringLiteral("已向群成员发送好友申请 QQ:%1，等待对方同意"),
                                          true);
                openPrivateSession(id);
                showMessagesView();
            });
            connect(&card, &MemberProfileCard::addFriendRequested, this, [this](const QString& id) {
                ensureFriendRequestQueued(id,
                                          QStringLiteral("已向群成员发送好友申请 QQ:%1，等待对方同意"),
                                          true);
            });
            card.exec();
        } else if (selected == groupNicknameAction) {
            GroupNicknameDialog dlg(this);
            dlg.setCurrentNickname(contactDisplayName(memberId));
            connect(&dlg, &GroupNicknameDialog::nicknameConfirmed, this, [this, memberId](const QString& nick) {
                const QString remark = nick.trimmed();
                if (remark.isEmpty()) {
                    return;
                }
                m_friendNames[memberId] = remark;
                if (m_friendIds.contains(memberId)) {
                    saveFriends();
                }
                refreshFriendList();
                refreshGroupMemberPanel();
                appendSystemMessage(QStringLiteral("已设置 %1 的群昵称为 %2").arg(memberId, remark));
                ui->statusbar->showMessage(QStringLiteral("已设置群昵称：%1").arg(remark), 2200);
            });
            dlg.exec();
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
    QTextEdit* input = m_messagesView ? m_messagesView->composer()->inputEdit() : ui->messageEdit;
    const QString draftText = input->toPlainText().trimmed();
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

    if (m_messagesView) {
        QStringList mentions{QStringLiteral("@全体成员")};
        const QStringList memberIds = currentSessionMemberIds();
        for (const QString& memberId : memberIds) {
            if (memberId != m_currentUserId) {
                mentions.append(QStringLiteral("@%1").arg(contactDisplayName(memberId)));
            }
        }
        // Drive the single visible chat surface through the unified state interface.
        m_messagesView->setComposerState(state.canSend, state.messagePlaceholder,
                                         state.draftSummary, mentions);
        ComposerWidget* composer = m_messagesView->composer();
        composer->setFileEnabled(state.sendFileEnabled);
        composer->setImageEnabled(state.sendImageEnabled);
        composer->inputEdit()->setToolTip(state.messageToolTip);
    }

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

    const QString groupId = inPublicSession ? QStringLiteral("public") : m_privateChatTarget;
    const int groupMemberCount = (inPublicSession || isLocalGroup)
        ? (isLocalGroup ? m_localGroupMembers.value(groupId).size() : m_serverGroupMembers.value(groupId).size())
        : 0;
    const bool isGroup = inPublicSession || isLocalGroup || m_joinedServerSearchGroups.values().contains(m_privateChatTarget);
    if (isGroup) {
        const QString displayName = isLocalGroup ? m_localGroupNames.value(groupId, sessionName)
            : m_serverGroupNames.value(inPublicSession ? QStringLiteral("public") : m_joinedServerSearchGroups.key(m_privateChatTarget), sessionName);
        ui->chatTitleLabel->setText(QStringLiteral("%1（%2）").arg(displayName).arg(groupMemberCount));
        ui->chatSubtitleLabel->clear();
        ui->chatHintLabel->clear();
    } else {
        ui->chatSubtitleLabel->setText(connected ? QStringLiteral("在线") : QStringLiteral("离线"));
    }
    ui->chatStatusBadgeLabel->setText(sessionKind);
    ui->sideSummaryStatsLabel->setText(QString("好友 %1 · 群聊 %2 · 在线 %3")
        .arg(m_friendIds.size())
        .arg(m_localGroupIds.size())
        .arg(knownOnlineUserCount()));
    ui->sideSummaryStateLabel->setText(connected
        ? QString("当前会话：%1 · 服务端在线视图已同步").arg(sessionName)
        : QString("当前会话：%1 · 连接中断时只保留本地视图与草稿").arg(sessionName));
    if (m_messagesView) {
        m_messagesView->setChatTitle(ui->chatTitleLabel->text(),
                                     ui->chatSubtitleLabel->text(),
                                     ui->chatHintLabel->text());
    }
}

void MainWindow::refreshSessionSummary() {
    const bool inPublicSession = m_privateChatTarget.isEmpty();
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    const QString sessionName = inPublicSession ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget);

    QString overviewState;
    QString overviewMeta;
    if (inPublicSession) {
        overviewState = isCurrentUserRemovedFromPublicGroup()
            ? QStringLiteral("公共群当前为只读历史态")
            : QStringLiteral("公共群在线成员与公告联动刷新中");
        overviewMeta = QString("会话：%1 · 在线 %2 · 好友 %3")
            .arg(sessionName)
            .arg(knownOnlineUserCount())
            .arg(m_friendIds.size());
    } else if (isLocalGroup) {
        const QStringList members = m_localGroupMembers.value(m_privateChatTarget);
        int onlineMembers = 0;
        for (const QString& memberId : members) {
            if (memberId == m_currentUserId || isContactOnline(memberId)) {
                ++onlineMembers;
            }
        }
        overviewState = QString("本地群聊 %1 · 成员 %2").arg(sessionName).arg(members.size());
        overviewMeta = QString("群主：%1 · 在线 %2 · 可继续邀请好友扩展会话")
            .arg(contactDisplayName(groupOwnerId(m_privateChatTarget)))
            .arg(onlineMembers);
    } else {
        overviewState = QString("私聊对象：%1 · %2")
            .arg(sessionName, isContactOnline(m_privateChatTarget) ? QStringLiteral("在线") : QStringLiteral("离线"));
        overviewMeta = QString("端到端状态：%1")
            .arg(e2eSessionStatusText(m_privateChatTarget));
    }

    ui->groupOverviewStateLabel->setText(overviewState);
    ui->groupOverviewMetaLabel->setText(overviewMeta);
    refreshWorkspaceChrome();
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
    QTextEdit* input = m_messagesView ? m_messagesView->composer()->inputEdit() : ui->messageEdit;
    QString text = input->toPlainText().trimmed();
    if (text.isEmpty()) {
        input->setFocus();
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
        const QString sentAt = QDateTime::currentDateTime().toString("hh:mm:ss");
        QString line = QString("[%1] <%2> %3").arg(sentAt, m_currentUserName, text);
        saveHistory(m_privateChatTarget, line);

        QStandardItem* item = new QStandardItem(text);
        item->setEditable(false);
        item->setData(bubbleTimestamp(sentAt), ChatBubbleTimestampRole);
        decorateChatItem(item, m_currentUserId, m_currentUserName, true);
        m_chatModel->appendRow(item);
        input->clear();
        ui->chatHintLabel->setText(QString("本地群聊 · %1 · 已发送 %2 字%3").arg(groupName).arg(text.size()).arg(originalText == text ? QString() : " · 快捷指令已展开"));
        ui->statusbar->showMessage(QString("已发送到 %1 · %2 字").arg(groupName).arg(text.size()), 1800);
        scrollActiveChatToBottom();
        return;
    }

    if (m_privateChatTarget.isEmpty() && isCurrentUserRemovedFromPublicGroup()) {
        input->setFocus();
        ui->chatHintLabel->setText("发送暂停 · 当前账号已不在公共群，等待重新邀请");
        ui->statusbar->showMessage("当前账号已不在公共群，暂不能发送公共群消息", 3000);
        refreshComposerState();
        return;
    }

    if (!m_client || !m_client->isConnected()) {
        input->setFocus();
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
        const QString sentAt = QDateTime::currentDateTime().toString("hh:mm:ss");
        QString line = QString("[%1] <%2> %3%4").arg(sentAt,
                                                     m_currentUserName,
                                                     sentEncrypted ? QStringLiteral("[端到端加密] ") : QString(),
                                                     text);
        const QJsonObject e2eStatus = sentEncrypted ? m_client->e2eSessionStatus(peerId) : QJsonObject();
        saveHistory(peerId,
                    line,
                    sentEncrypted ? QStringLiteral("encrypted") : QStringLiteral("plaintext"),
                    e2eStatus.value("keyId").toString(),
                    e2eStatus.value("keyFingerprintSha256").toString());

        QStandardItem* item = new QStandardItem(text);
        item->setEditable(false);
        item->setData(bubbleTimestamp(sentAt), ChatBubbleTimestampRole);
        decorateChatItem(item, m_currentUserId, m_currentUserName, true);
        m_chatModel->appendRow(item);
        int rowCount = m_chatModel->rowCount();
        if (rowCount > MAX_HISTORY_LINES) {
            m_chatModel->removeRows(0, rowCount - MAX_HISTORY_LINES);
        }
        scrollActiveChatToBottom();
        ui->chatHintLabel->setText(QString("已发送到 %1 · %2 字 · %3%4%5")
            .arg(targetName)
            .arg(text.size())
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss"),
                 originalText == text ? QString() : " · 快捷指令已展开",
                 sentEncrypted ? QStringLiteral(" · 端到端加密") : QString()));
        ui->statusbar->showMessage(QString("已发送到 %1 · %2 字%3").arg(targetName).arg(text.size()).arg(sentEncrypted ? QStringLiteral(" · 端到端加密") : QString()), 1800);

        input->clear();
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

    sendSelectedTransfer(selectedFile, false);
}

void MainWindow::sendSelectedTransfer(const SelectedTransferFile& selectedFile, bool media) {
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    const TransferSelectionPlan selectionPlan = media
        ? m_transferManager.mediaSelectionPlan()
        : m_transferManager.fileSelectionPlan();
    const QString kind = media
        ? m_transferManager.mediaSelection(selectedFile.info).mediaType
        : selectionPlan.preparingKind;
    const bool isVideo = media && m_transferManager.mediaSelection(selectedFile.info).isVideo;

    const TransferSendUiState preparingState =
        m_transferManager.preparingSendState(kind,
                                             selectedFile.info.fileName(),
                                             selectedFile.fileSize,
                                             targetName);
    applyTransferSendState(preparingState);
    const QString completedAt = QDateTime::currentDateTime().toString("hh:mm:ss");
    if (isLocalGroup) {
        if (media) {
            appendLocalGroupMediaTransferCompletion(selectedFile.filePath,
                                                    selectedFile.info,
                                                    selectedFile.fileSize,
                                                    kind,
                                                    isVideo,
                                                    targetName,
                                                    completedAt);
        } else {
            appendLocalGroupFileTransferCompletion(selectionPlan,
                                                   selectedFile.info,
                                                   selectedFile.fileSize,
                                                   targetName,
                                                   completedAt);
        }
        return;
    }

    // Images should behave like chat messages: send immediately and render the
    // local bubble without opening the file-transfer progress dialog. Videos
    // and regular files keep the resumable progress workflow below.
    if (media && !isVideo) {
        const int optimisticRow = m_chatModel ? m_chatModel->rowCount() : -1;
        const QString optimisticTime = QDateTime::currentDateTime().toString("hh:mm:ss");
        const TransferSendUiState optimisticState = m_transferManager.remoteSendCompletedState(
            kind,
            selectedFile.info.fileName(),
            selectedFile.fileSize,
            targetName,
            optimisticTime,
            QString());
        const TransferWorkspaceCardState optimisticWorkspace = m_transferManager.remoteSendCompletedWorkspaceState(
            kind,
            selectedFile.info.fileName(),
            selectedFile.fileSize,
            targetName,
            optimisticTime,
            QString());
        appendRemoteMediaTransferCompletion(selectedFile.filePath,
                                            optimisticState,
                                            false,
                                            optimisticWorkspace);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        qtnetworkchat::logDebug("MainWindow", QStringLiteral("direct image send start row=%1 file=%2")
                .arg(QString::number(optimisticRow), selectedFile.info.fileName()));

        const bool ok = m_client && m_client->sendImage(selectedFile.filePath, m_privateChatTarget);
        qtnetworkchat::logDebug("MainWindow", QStringLiteral("direct image send finished ok=%1 file=%2")
                .arg(ok ? QStringLiteral("true") : QStringLiteral("false"), selectedFile.info.fileName()));
        updateSavedOutgoingTransferRecoveryUi(!ok);
        if (!ok) {
            if (m_chatModel && optimisticRow >= 0 && optimisticRow < m_chatModel->rowCount()) {
                m_chatModel->removeRow(optimisticRow);
            }
            handleRemoteTransferResult(false,
                                       false,
                                       selectedFile.filePath,
                                       selectedFile.info,
                                       selectedFile.fileSize,
                                       targetName,
                                       kind,
                                       true,
                                       false,
                                       QString());
        }
        return;
    }

    QString transferSummary;
    bool transferCanceled = false;
    bool ok = sendTransferWithProgress(selectedFile.filePath,
                                       m_privateChatTarget,
                                       targetName,
                                       kind,
                                       media && !isVideo,
                                       &transferSummary,
                                       &transferCanceled);
    updateSavedOutgoingTransferRecoveryUi(!ok && !transferCanceled);
    handleRemoteTransferResult(ok,
                               transferCanceled,
                               selectedFile.filePath,
                               selectedFile.info,
                               selectedFile.fileSize,
                               targetName,
                                kind,
                                media,
                                isVideo,
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
    sendSelectedTransfer(selectedFile, true);
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

void MainWindow::onViewHistory() {
    const QString peerId = m_privateChatTarget.isEmpty() ? QStringLiteral("group") : m_privateChatTarget;
    const QString sessionName = m_privateChatTarget.isEmpty()
        ? QStringLiteral("公共聊天室")
        : (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))
            ? m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊"))
            : contactDisplayName(m_privateChatTarget));
    const QStringList rows = m_historyService.rowsForExport(peerId);
    QDialog panel(this);
    panel.setObjectName(QStringLiteral("historyWindow"));
    panel.setWindowTitle(sessionName + QStringLiteral(" - 聊天记录"));
    panel.setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
    panel.resize(760, 680);
    panel.setMinimumSize(620, 520);
    auto* root = new QVBoxLayout(&panel);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);
    auto* header = new QFrame(&panel);
    header->setObjectName(QStringLiteral("historyWindowHeader"));
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(4, 0, 4, 6);
    headerLayout->setSpacing(3);
    auto* title = new QLabel(sessionName, header);
    title->setObjectName(QStringLiteral("historyWindowTitle"));
    auto* meta = new QLabel(QStringLiteral("聊天记录 · 共 %1 条").arg(QString::number(rows.size())), header);
    meta->setObjectName(QStringLiteral("historyWindowMeta"));
    headerLayout->addWidget(title);
    headerLayout->addWidget(meta);
    root->addWidget(header);

    auto* search = new QLineEdit(&panel);
    search->setObjectName(QStringLiteral("historyWindowSearch"));
    search->setPlaceholderText(QStringLiteral("搜索"));
    search->setClearButtonEnabled(true);
    search->setFixedHeight(34);
    root->addWidget(search);

    auto* filters = new QFrame(&panel);
    auto* filtersLayout = new QHBoxLayout(filters);
    filtersLayout->setContentsMargins(0, 0, 0, 0);
    filtersLayout->setSpacing(22);
    auto* filterGroup = new QButtonGroup(filters);
    filterGroup->setExclusive(true);
    const QList<QPair<QString, QString>> filterChoices = {
        {QStringLiteral("全部"), QStringLiteral("all")},
        {QStringLiteral("图片/视频"), QStringLiteral("media")},
        {QStringLiteral("表情"), QStringLiteral("emoji")},
        {QStringLiteral("文件"), QStringLiteral("file")},
        {QStringLiteral("链接"), QStringLiteral("link")}
    };
    for (int index = 0; index < filterChoices.size(); ++index) {
        auto* button = new QPushButton(filterChoices.at(index).first, filters);
        button->setObjectName(QStringLiteral("historyWindowFilter"));
        button->setProperty("mode", filterChoices.at(index).second);
        button->setCheckable(true);
        button->setChecked(index == 0);
        button->setFlat(true);
        filterGroup->addButton(button, index);
        filtersLayout->addWidget(button);
    }
    filtersLayout->addStretch();
    auto* filterHint = new QPushButton(QStringLiteral("筛选"), filters);
    filterHint->setObjectName(QStringLiteral("historyWindowFilterHint"));
    filterHint->setFlat(true);
    filtersLayout->addWidget(filterHint);
    root->addWidget(filters);

    auto* scroll = new QScrollArea(&panel);
    scroll->setObjectName(QStringLiteral("historyWindowScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll, 1);

    auto rebuildList = [this, &scroll, &rows, search, filterGroup]() {
        QWidget* oldList = scroll->takeWidget();
        if (oldList) oldList->deleteLater();
        auto* list = new QWidget(scroll);
        list->setObjectName(QStringLiteral("historyWindowList"));
        auto* listLayout = new QVBoxLayout(list);
        listLayout->setContentsMargins(4, 0, 4, 0);
        listLayout->setSpacing(0);
        const QString keyword = search->text().trimmed();
        const QString mode = filterGroup->checkedButton()
            ? filterGroup->checkedButton()->property("mode").toString() : QStringLiteral("all");
        QString currentDate;
        int visibleCount = 0;
        for (const QString& row : rows) {
            const StoredChatMessage stored = parseStoredChatMessage(row);
            const QString bodyLower = stored.body.toLower();
            const bool media = stored.body.contains(QStringLiteral("图片")) || stored.body.contains(QStringLiteral("视频"));
            const bool file = stored.body.contains(QStringLiteral("文件"));
            const bool link = bodyLower.contains(QStringLiteral("http://")) || bodyLower.contains(QStringLiteral("https://"));
            const bool emoji = stored.body.contains(QRegularExpression(QStringLiteral("[\\x{1F300}-\\x{1FAFF}]")));
            const bool modeMatches = mode == QLatin1String("all")
                || (mode == QLatin1String("media") && media)
                || (mode == QLatin1String("file") && file)
                || (mode == QLatin1String("link") && link)
                || (mode == QLatin1String("emoji") && emoji);
            const bool keywordMatches = keyword.isEmpty()
                || stored.senderName.contains(keyword, Qt::CaseInsensitive)
                || stored.body.contains(keyword, Qt::CaseInsensitive);
            if (!modeMatches || !keywordMatches) continue;

            const QRegularExpressionMatch dateMatch = QRegularExpression(QStringLiteral(R"(^(\d{4}-\d{2}-\d{2}))")).match(row);
            const QString date = dateMatch.hasMatch() ? dateMatch.captured(1).replace(QLatin1Char('-'), QLatin1Char('/'))
                                                       : QStringLiteral("更早记录");
            if (date != currentDate) {
                currentDate = date;
                auto* dateLabel = new QLabel(date, list);
                dateLabel->setObjectName(QStringLiteral("historyWindowDate"));
                listLayout->addWidget(dateLabel);
            }

            const QString senderName = stored.system ? QStringLiteral("系统消息")
                : (stored.senderName.isEmpty() ? QStringLiteral("未知成员") : stored.senderName);
            QString senderId;
            if (!stored.system) {
                if (senderName == m_currentUserName) senderId = m_currentUserId;
                else {
                    for (auto user = m_knownUsers.cbegin(); user != m_knownUsers.cend(); ++user) {
                        if (user.value().name == senderName) { senderId = user.key(); break; }
                    }
                    if (senderId.isEmpty()) {
                        for (auto member = m_serverGroupMemberNames.cbegin(); member != m_serverGroupMemberNames.cend(); ++member) {
                            if (member.value() == senderName) { senderId = member.key().section(QLatin1Char('|'), 1); break; }
                        }
                    }
                }
            }
            auto* card = new QFrame(list);
            card->setObjectName(QStringLiteral("historyWindowRow"));
            auto* rowLayout = new QHBoxLayout(card);
            rowLayout->setContentsMargins(0, 12, 0, 12);
            rowLayout->setSpacing(10);
            auto* avatar = new AvatarLabel(card, 36);
            const QString avatarPath = senderId.isEmpty() ? QString() : chatAvatarPath(senderId);
            QPixmap avatarPixmap;
            if (!avatarPath.isEmpty()) avatarPixmap.load(avatarPath);
            if (avatarPixmap.isNull() && senderId == m_currentUserId) avatarPixmap.load(getAvatarFilePath());
            if (!avatarPixmap.isNull()) avatar->setPixmap(avatarPixmap);
            else avatar->setTextAvatar(senderName, ThemeManager::instance()->primaryColor());
            rowLayout->addWidget(avatar, 0, Qt::AlignTop);
            auto* content = new QWidget(card);
            auto* contentLayout = new QVBoxLayout(content);
            contentLayout->setContentsMargins(0, 0, 0, 0);
            contentLayout->setSpacing(4);
            auto* cardHeader = new QHBoxLayout();
            cardHeader->setContentsMargins(0, 0, 0, 0);
            auto* sender = new QLabel(senderName, content);
            sender->setObjectName(QStringLiteral("historyWindowSender"));
            auto* timestamp = new QLabel(bubbleTimestamp(stored.timestamp), content);
            timestamp->setObjectName(QStringLiteral("historyWindowTime"));
            cardHeader->addWidget(sender, 1);
            cardHeader->addWidget(timestamp);
            auto* body = new QLabel(stored.body, content);
            body->setObjectName(QStringLiteral("historyWindowBody"));
            body->setWordWrap(true);
            body->setMaximumHeight(96);
            contentLayout->addLayout(cardHeader);
            contentLayout->addWidget(body);
            rowLayout->addWidget(content, 1);
            listLayout->addWidget(card);
            ++visibleCount;
        }
        if (visibleCount == 0) {
            auto* empty = new QLabel(QStringLiteral("暂无聊天记录"), list);
            empty->setObjectName(QStringLiteral("historyWindowEmpty"));
            empty->setAlignment(Qt::AlignCenter);
            listLayout->addWidget(empty, 1);
        }
        listLayout->addStretch();
        scroll->setWidget(list);
    };
    rebuildList();
    connect(search, &QLineEdit::textChanged, &panel, [rebuildList]() { rebuildList(); });
    connect(filterGroup, qOverload<QAbstractButton*>(&QButtonGroup::buttonClicked), &panel,
            [rebuildList](QAbstractButton*) { rebuildList(); });
    auto* footer = new QFrame(&panel);
    footer->setObjectName(QStringLiteral("historyWindowFooter"));
    footer->setFixedHeight(56);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(12, 10, 12, 10);
    auto* clear = new QPushButton(QStringLiteral("清空聊天记录"), footer);
    clear->setObjectName(QStringLiteral("historyWindowClear"));
    clear->setFixedHeight(36);
    clear->setEnabled(!rows.isEmpty());
    footerLayout->addWidget(clear);
    root->addWidget(footer);
    connect(clear, &QPushButton::clicked, &panel, [&panel, this]() {
        panel.accept();
        onClearHistory();
    });
    panel.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#historyWindow { background:%1; }"
        "QFrame#historyWindowFooter { border-top:1px solid %2; }"
        "QLineEdit#historyWindowSearch { background:%3; color:%4; border:1px solid %2; border-radius:7px; padding:0 12px; font-size:13px; }"
        "QLineEdit#historyWindowSearch:focus { border-color:%6; }"
        "QPushButton#historyWindowFilter { color:%4; border:none; background:transparent; padding:6px 2px; border-radius:0; font-size:13px; }"
        "QPushButton#historyWindowFilter:checked { color:%6; border-bottom:2px solid %6; font-weight:600; }"
        "QPushButton#historyWindowFilterHint { color:%4; border:none; background:transparent; padding:6px 4px; font-size:12px; }"
        "QScrollArea#historyWindowScroll,QWidget#historyWindowList { background:%1; }"
        "QLabel#historyWindowTitle { color:%4; font-size:15px; font-weight:600; }"
        "QLabel#historyWindowMeta,QLabel#historyWindowTime { color:%5; font-size:12px; }"
        "QLabel#historyWindowDate { color:%4; font-size:13px; padding:12px 0 7px 0; }"
        "QLabel#historyWindowSender { color:%5; font-size:13px; }"
        "QLabel#historyWindowBody { color:%4; font-size:14px; }"
        "QFrame#historyWindowRow { background:transparent; border:none; border-bottom:1px solid %2; }"
        "QLabel#historyWindowEmpty { color:%5; font-size:12px; }"
        "QPushButton#historyWindowClear { min-height:36px; max-height:36px; padding:0 14px; color:%7; background:transparent; border:1px solid %2; border-radius:5px; font-size:13px; }"
        "QPushButton#historyWindowClear:hover { background:%8; border-color:%7; }"
        "QPushButton#historyWindowClear:disabled { color:%5; border-color:%2; }")
        .arg(ThemeManager::instance()->backgroundColor().name(), ThemeManager::instance()->borderColor().name(),
             ThemeManager::instance()->backgroundSecondaryColor().name(), ThemeManager::instance()->textColor().name(),
             ThemeManager::instance()->textSecondaryColor().name(), ThemeManager::instance()->primaryColor().name(),
             ThemeManager::instance()->dangerColor().name(), ThemeManager::instance()->backgroundTertiaryColor().name()));
    panel.exec();
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
        const StoredChatMessage stored = parseStoredChatMessage(row);
        const bool outgoing = !stored.system && !m_currentUserName.isEmpty()
            && stored.senderName == m_currentUserName;
        QStandardItem* item = new QStandardItem(stored.body);
        item->setEditable(false);
        if (!stored.timestamp.isEmpty()) {
            item->setData(bubbleTimestamp(stored.timestamp), ChatBubbleTimestampRole);
        }
        decorateChatItem(item,
                         outgoing ? m_currentUserId : QString(),
                         stored.senderName,
                         outgoing,
                         stored.system);
        m_chatModel->appendRow(item);
    }
    ui->chatHintLabel->setText(QString("%1 · %2 · 已筛选 %3 条记录")
        .arg(sessionName, selectedDate.toString("yyyy-MM-dd"), QString::number(rows.size())));
    ui->statusbar->showMessage(QString("已筛选 %1 条聊天记录").arg(rows.size()), 2400);
    scrollActiveChatToBottom();
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

void MainWindow::onInsertEmoji() {
    QMenu menu(this);
    // Categorized emoji panel: 10 categories rendered as submenus, each laid out as
    // a compact emoji grid. Mirrors the tauri-qqnt EmojiPicker categories.
    struct EmojiCategory {
        QString title;
        QStringList emojis;
    };
    static const QList<EmojiCategory> categories = {
        {QStringLiteral("最近"), {"😀", "😂", "👍", "❤️", "🎉", "🔥", "🙏", "👏"}},
        {QStringLiteral("超级"), {"🤩", "🥳", "😻", "💯", "✨", "⭐", "🌟", "💫"}},
        {QStringLiteral("小黄脸"), {"😀", "😁", "😂", "🤣", "😊", "😇", "🙂", "😉", "😍", "😘", "😜", "🤔", "😐", "😴", "😭", "😡"}},
        {QStringLiteral("手势"), {"👍", "👎", "👌", "✌️", "🤞", "👏", "🙌", "🙏", "💪", "🤝", "👋", "✋"}},
        {QStringLiteral("爱心"), {"❤️", "🧡", "💛", "💚", "💙", "💜", "🖤", "🤍", "💔", "💕", "💞", "💗"}},
        {QStringLiteral("动物"), {"🐶", "🐱", "🐭", "🐹", "🐰", "🦊", "🐻", "🐼", "🐨", "🐯", "🦁", "🐮"}},
        {QStringLiteral("自然"), {"🌸", "🌼", "🌻", "🌹", "🌈", "☀️", "🌙", "⭐", "❄️", "🍀", "🌿", "🌊"}},
        {QStringLiteral("食物"), {"🍎", "🍌", "🍉", "🍇", "🍓", "🍔", "🍟", "🍕", "🍰", "🍦", "☕", "🍺"}},
        {QStringLiteral("物品"), {"📌", "📎", "📷", "🎁", "💡", "🔔", "📱", "💻", "⏰", "🔑", "📚", "✏️"}},
        {QStringLiteral("符号"), {"✅", "❌", "❓", "❗", "💤", "💢", "💦", "💨", "🎵", "🔞", "♻️", "✔️"}},
    };
    for (const EmojiCategory& category : categories) {
        QMenu* categoryMenu = menu.addMenu(category.title);
        for (const QString& emoji : category.emojis) {
            QAction* action = categoryMenu->addAction(emoji);
            connect(action, &QAction::triggered, this, [this, emoji]() {
                insertChatDraftText(emoji, QString("已插入表情 %1").arg(emoji), 1400);
            });
        }
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
    const QPoint position = m_messagesView
        ? m_messagesView->composer()->mapToGlobal(QPoint(0, -menu.sizeHint().height()))
        : ui->emojiBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height()));
    menu.exec(position);
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
    const QPoint position = m_messagesView
        ? m_messagesView->composer()->mapToGlobal(QPoint(36, -menu.sizeHint().height()))
        : ui->mentionBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height()));
    menu.exec(position);
}

void MainWindow::onComposerFilesDropped(const QStringList& paths) {
    if (paths.isEmpty()) return;

    const QString path = paths.first();
    const QFileInfo info(path);
    if (!info.isFile()) return;

    const TransferMediaSelection mediaSelection = m_transferManager.mediaSelection(info);
    const bool media = mediaSelection.mediaType == QStringLiteral("图片")
        || mediaSelection.mediaType == QStringLiteral("视频");
    const QString targetName = m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    if (!ensureTransferTargetReady(media ? mediaSelection.mediaType : QStringLiteral("文件"), targetName, isLocalGroup)) {
        return;
    }

    SelectedTransferFile selected;
    selected.filePath = path;
    selected.info = info;
    selected.fileSize = LocalFileManager::humanFileSize(info.size());
    sendSelectedTransfer(selected, media);
}

void MainWindow::onMessageActionRequested(const QModelIndex& index, const QString& action) {
    if (!index.isValid()) return;

    const QString text = index.data(Qt::DisplayRole).toString();
    const QString senderName = index.data(ChatBubbleSenderNameRole).toString();

    if (action == QStringLiteral("copy")) {
        QClipboard* clipboard = QGuiApplication::clipboard();
        if (clipboard) clipboard->setText(text);
    } else if (action == QStringLiteral("quote")) {
        if (m_messagesView) {
            m_messagesView->composer()->insertText(
                QStringLiteral("[%1]: %2\n").arg(senderName, text));
            m_messagesView->composer()->inputEdit()->setFocus();
        }
    } else if (action == QStringLiteral("multiSelect")) {
        if (m_messagesView) {
            m_messagesView->setMultiSelectMode(true);
        }
        const QString sessionId = m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget;
        const QString senderId = index.data(ChatBubbleSenderIdRole).toString();
        const QString senderName = index.data(ChatBubbleSenderNameRole).toString();
        const QString msgId = QStringLiteral("msg-%1-%2").arg(index.row()).arg(
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz")));
        QJsonObject messageObject;
        messageObject[QStringLiteral("id")] = msgId;
        messageObject[QStringLiteral("messageId")] = msgId;
        messageObject[QStringLiteral("senderId")] = senderId;
        messageObject[QStringLiteral("senderName")] = senderName;
        messageObject[QStringLiteral("content")] = text;
        messageObject[QStringLiteral("sessionId")] = sessionId;
        messageObject[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
        messageObject[QStringLiteral("selected")] = true;
        QJsonObject payload;
        payload[QStringLiteral("message")] = messageObject;
        payload[QStringLiteral("selected")] = true;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        QQNTBackendService::handle(QStringLiteral("toggle_multi_select_local_message"), payload, &response, &errorCode, &errorMessage);
    } else if (action == QStringLiteral("delete") ||
               action == QStringLiteral("favorite") || action == QStringLiteral("essence") || action == QStringLiteral("unessence") ||
               action == QStringLiteral("recall")) {
        handleBackendContextCommand(action, text, index);
    } else if (action == QStringLiteral("forward")) {
        runForwardForMessage(index, text);
    } else {
        qDebug() << "Unhandled message action:" << action;
    }
}

void MainWindow::onAvatarActionRequested(const QModelIndex& index, const QString& action) {
    if (!index.isValid()) return;

    const QString senderName = index.data(ChatBubbleSenderNameRole).toString();
    const QString senderId = index.data(ChatBubbleSenderIdRole).toString();

    if (action == QStringLiteral("at")) {
        if (m_messagesView) {
            m_messagesView->composer()->insertText(QStringLiteral("@%1 ").arg(senderName));
            m_messagesView->composer()->inputEdit()->setFocus();
        }
    } else if (action == QStringLiteral("sendMessage")) {
        if (!senderId.isEmpty() && senderId != m_currentUserId) {
            openPrivateSession(senderId);
            ui->statusbar->showMessage(QStringLiteral("已切换到与 %1 的私聊").arg(senderName), 2000);
        }
    } else if (action == QStringLiteral("viewProfile") || action == QStringLiteral("addFriend") ||
               action == QStringLiteral("setGroupNickname") || action == QStringLiteral("block") ||
               action == QStringLiteral("report")) {
        const QString commandId = camelToKebabCase(action);
        handleBackendContextCommand(commandId, QString(), index);
    } else if (action == QStringLiteral("mute")) {
        // Muting is a group-only, owner/admin action routed through the server group flow.
        if (senderId.isEmpty() || senderId == m_currentUserId) {
            ui->statusbar->showMessage(QStringLiteral("无法对该成员执行禁言"), 2000);
            return;
        }
        const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
        const bool isPublicGroup = m_privateChatTarget.isEmpty();
        if (!isLocalGroup && !isPublicGroup) {
            ui->statusbar->showMessage(QStringLiteral("禁言仅在群聊会话中可用"), 2000);
            return;
        }
        if (isPublicGroup && !canCurrentUserManageServerGroup("public")) {
            ui->statusbar->showMessage(QStringLiteral("只有群主或管理员可以禁言成员"), 2400);
            return;
        }
        if (isLocalGroup && !isCurrentUserGroupOwner(m_privateChatTarget)) {
            ui->statusbar->showMessage(QStringLiteral("只有群主可以禁言成员"), 2400);
            return;
        }
        if (isLocalGroup) {
            // Local groups have no server-side mute enforcement; inform the user.
            ui->statusbar->showMessage(QStringLiteral("本地群聊暂不支持服务端禁言"), 2400);
            return;
        }
        MuteDurationDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted) {
            ui->statusbar->showMessage(QStringLiteral("已取消禁言操作"), 1600);
            return;
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        // Permanent mute maps to the backend's maximum window (30 days).
        const qint64 mutedUntil = dialog.isPermanent()
            ? nowMs + 30LL * 24 * 60 * 60 * 1000
            : nowMs + static_cast<qint64>(dialog.durationMinutes()) * 60 * 1000;
        if (m_client && m_client->sendServerGroupMemberMute(QStringLiteral("public"), senderId, mutedUntil, QString())) {
            ui->statusbar->showMessage(QStringLiteral("已请求禁言 %1").arg(senderName), 2200);
        } else {
            ui->statusbar->showMessage(QStringLiteral("禁言失败：需要有效的服务器连接"), 3000);
        }
    } else if (action == QStringLiteral("setAdmin")) {
        // Admin promotion applies only to the server public group.
        if (senderId.isEmpty() || senderId == m_currentUserId) {
            ui->statusbar->showMessage(QStringLiteral("无法对该成员设置管理员"), 2000);
            return;
        }
        if (!m_privateChatTarget.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("管理员设置仅在公共群会话中可用"), 2000);
            return;
        }
        if (!canCurrentUserManageServerGroup("public")) {
            ui->statusbar->showMessage(QStringLiteral("只有群主可以设置管理员"), 2400);
            return;
        }
        requestServerGroupMemberUpdate(senderId, "promote_admin");
    } else {
        qDebug() << "Unhandled avatar action:" << action;
    }
}

void MainWindow::onMediaActivated(const QModelIndex& index) {
    if (!index.isValid()) return;

    QString mediaKind = index.data(ChatBubbleMediaKindRole).toString();
    QString openPath = index.data(ChatBubbleMediaOpenPathRole).toString().trimmed();

    if (mediaKind == QLatin1String("image")) {
        ImagePreviewWindow preview(this);
        preview.setWindowTitle(QStringLiteral("图片预览"));
        QVariant previewData = index.data(ChatBubbleMediaPreviewRole);
        if (!openPath.isEmpty() && QFile::exists(openPath)) {
            preview.setImagePath(openPath);
        } else if (previewData.canConvert<QPixmap>() && !previewData.value<QPixmap>().isNull()) {
            preview.setImage(previewData.value<QPixmap>());
        } else {
            preview.setErrorMessage(QStringLiteral("图片预览不可用"));
        }
        connect(&preview, &ImagePreviewWindow::saveRequested, this, [this, openPath](const QString& destinationPath) {
            if (destinationPath.isEmpty()) return;
            const QFileInfo destinationInfo(destinationPath);
            QJsonObject payload;
            payload[QStringLiteral("sourcePath")] = openPath;
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
        connect(&preview, &ImagePreviewWindow::openFolderRequested, this, [this, openPath]() {
            if (openPath.isEmpty() || !QFile::exists(openPath)) {
                ui->statusbar->showMessage(QStringLiteral("图片路径无效"), 2000);
                return;
            }
            const QFileInfo info(openPath);
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
            ui->statusbar->showMessage(QStringLiteral("已打开图片所在文件夹"), 1800);
        });
        connect(&preview, &ImagePreviewWindow::copyBase64Requested, this, [this, openPath]() {
            if (openPath.isEmpty() || !QFile::exists(openPath)) {
                ui->statusbar->showMessage(QStringLiteral("图片路径无效"), 2000);
                return;
            }
            QJsonObject payload;
            payload[QStringLiteral("filePath")] = openPath;
            QJsonObject response;
            QString errorCode;
            QString errorMessage;
            if (QQNTBackendService::handle(QStringLiteral("read_image_base64"), payload, &response, &errorCode, &errorMessage)) {
                const QString b64 = response.value(QStringLiteral("base64")).toString();
                if (!b64.isEmpty()) {
                    QClipboard* clipboard = QGuiApplication::clipboard();
                    if (clipboard) clipboard->setText(b64);
                    ui->statusbar->showMessage(QStringLiteral("图片 Base64 已复制到剪贴板"), 1800);
                } else {
                    ui->statusbar->showMessage(QStringLiteral("无法读取图片 Base64"), 2000);
                }
            } else {
                ui->statusbar->showMessage(QStringLiteral("读取 Base64 失败: %1").arg(errorMessage), 3000);
            }
        });
        connect(&preview, &ImagePreviewWindow::forwardRequested, this, [this, index]() {
            runForwardForMessage(index, index.data(Qt::DisplayRole).toString());
        });
        preview.exec();
    } else if (mediaKind == QLatin1String("video")) {
        if (!openPath.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(openPath));
            ui->statusbar->showMessage(QStringLiteral("正在打开视频..."), 1800);
        } else {
            ui->statusbar->showMessage(QStringLiteral("视频路径无效"), 2000);
        }
    } else if (!openPath.isEmpty()) {
        const QFileInfo info(openPath);
        if (info.isFile()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(openPath));
        } else if (info.isDir()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
        } else {
            ui->statusbar->showMessage(QStringLiteral("无法打开文件"), 2000);
        }
    } else {
        ui->statusbar->showMessage(QStringLiteral("无法识别的媒体内容"), 1600);
    }
}

QString MainWindow::camelToKebabCase(const QString& camel) const {
    QString result;
    for (int i = 0; i < camel.size(); ++i) {
        const QChar ch = camel.at(i);
        if (i > 0 && ch.isUpper()) {
            result.append(QLatin1Char('-'));
        }
        result.append(ch.toLower());
    }
    return result;
}

void MainWindow::runForwardForMessage(const QModelIndex& index, const QString& text) {
    if (!m_messagesView) return;

    ForwardWindow dialog(this);
    QMap<QString, QString> contactNames;
    for (const QString& id : m_friendIds) {
        contactNames[id] = contactDisplayName(id);
    }
    QMap<QString, QString> groupNames;
    for (const QString& id : m_localGroupIds) {
        groupNames[id] = m_localGroupNames.value(id, id);
    }
    dialog.setContacts(m_friendIds, contactNames);
    dialog.setGroups(m_localGroupIds, groupNames);

    connect(&dialog, &ForwardWindow::forwardToContactRequested, this, [this, index, text](const QString& userId) {
        forwardMessageToTarget(index, text, userId, QString());
    });
    connect(&dialog, &ForwardWindow::forwardToGroupRequested, this, [this, index, text](const QString& groupId) {
        forwardMessageToTarget(index, text, QString(), groupId);
    });
    dialog.exec();
}

void MainWindow::forwardMessageToTarget(const QModelIndex& index,
                                        const QString& text,
                                        const QString& userId,
                                        const QString& groupId) {
    const QString senderId = index.data(ChatBubbleSenderIdRole).toString();
    const QString senderName = index.data(ChatBubbleSenderNameRole).toString();
    const QString sessionId = m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget;
    const QString messageId = QStringLiteral("msg-%1-%2").arg(index.row()).arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz")));
    const QString targetId = userId.isEmpty() ? groupId : userId;
    if (targetId.isEmpty()) return;

    QJsonObject messageObject;
    messageObject[QStringLiteral("id")] = messageId;
    messageObject[QStringLiteral("messageId")] = messageId;
    messageObject[QStringLiteral("senderId")] = senderId;
    messageObject[QStringLiteral("senderName")] = senderName;
    messageObject[QStringLiteral("content")] = text;
    messageObject[QStringLiteral("sessionId")] = sessionId;
    messageObject[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonObject target;
    target[QStringLiteral("id")] = targetId;
    if (!userId.isEmpty()) {
        target[QStringLiteral("type")] = QStringLiteral("contact");
    } else {
        target[QStringLiteral("type")] = QStringLiteral("group");
    }

    QJsonObject payload;
    payload[QStringLiteral("message")] = messageObject;
    payload[QStringLiteral("target")] = target;

    QJsonObject response;
    QString errorCode;
    QString errorMessage;
    if (!QQNTBackendService::handle(QStringLiteral("forward_local_message"), payload, &response, &errorCode, &errorMessage)) {
        ui->statusbar->showMessage(QStringLiteral("转发失败 (%1)").arg(errorMessage), 3000);
        return;
    }
    ui->statusbar->showMessage(QStringLiteral("已转发到 %1").arg(contactDisplayName(targetId)), 2200);
}

void MainWindow::persistMultiSelectMessages(const QModelIndexList& selected) {
    const QString sessionId = m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget;
    for (const QModelIndex& index : selected) {
        const QString text = index.data(Qt::DisplayRole).toString();
        const QString senderId = index.data(ChatBubbleSenderIdRole).toString();
        const QString senderName = index.data(ChatBubbleSenderNameRole).toString();
        const QString msgId = QStringLiteral("msg-%1-%2").arg(index.row()).arg(
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz")));
        QJsonObject messageObject;
        messageObject[QStringLiteral("id")] = msgId;
        messageObject[QStringLiteral("messageId")] = msgId;
        messageObject[QStringLiteral("senderId")] = senderId;
        messageObject[QStringLiteral("senderName")] = senderName;
        messageObject[QStringLiteral("content")] = text;
        messageObject[QStringLiteral("sessionId")] = sessionId;
        messageObject[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
        messageObject[QStringLiteral("selected")] = true;
        QJsonObject payload;
        payload[QStringLiteral("message")] = messageObject;
        payload[QStringLiteral("selected")] = true;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        QQNTBackendService::handle(QStringLiteral("toggle_multi_select_local_message"), payload, &response, &errorCode, &errorMessage);
    }
}

void MainWindow::onMultiSelectForwardRequested() {
    if (!m_messagesView) return;

    const QModelIndexList selected = m_messagesView->chatListView()->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        ui->statusbar->showMessage(QStringLiteral("请先选择要转发的消息"), 2000);
        return;
    }

    ForwardWindow dialog(this);
    QMap<QString, QString> contactNames;
    for (const QString& id : m_friendIds) {
        contactNames[id] = contactDisplayName(id);
    }
    QMap<QString, QString> groupNames;
    for (const QString& id : m_localGroupIds) {
        groupNames[id] = m_localGroupNames.value(id, id);
    }
    dialog.setContacts(m_friendIds, contactNames);
    dialog.setGroups(m_localGroupIds, groupNames);

    connect(&dialog, &ForwardWindow::forwardToContactRequested, this, [this, selected](const QString& userId) {
        persistMultiSelectMessages(selected);
        for (const QModelIndex& index : selected) {
            const QString text = index.data(Qt::DisplayRole).toString();
            forwardMessageToTarget(index, text, userId, QString());
        }
    });
    connect(&dialog, &ForwardWindow::forwardToGroupRequested, this, [this, selected](const QString& groupId) {
        persistMultiSelectMessages(selected);
        for (const QModelIndex& index : selected) {
            const QString text = index.data(Qt::DisplayRole).toString();
            forwardMessageToTarget(index, text, QString(), groupId);
        }
    });
    dialog.exec();
}

void MainWindow::onMultiSelectDeleteRequested() {
    if (!m_messagesView) return;

    QModelIndexList selected = m_messagesView->chatListView()->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    std::sort(selected.begin(), selected.end(), [](const QModelIndex& a, const QModelIndex& b) {
        return a.row() > b.row();
    });

    const QString sessionId = m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget;
    QStandardItemModel* model = m_messagesView->chatModel();
    int deletedCount = 0;
    for (const QModelIndex& index : selected) {
        const QString msgId = QStringLiteral("msg-%1-%2").arg(index.row()).arg(
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz")));
        QJsonObject payload;
        payload[QStringLiteral("sessionId")] = sessionId;
        payload[QStringLiteral("messageId")] = msgId;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        if (QQNTBackendService::handle(QStringLiteral("delete_local_message"), payload, &response, &errorCode, &errorMessage)) {
            ++deletedCount;
        }
        if (model) {
            model->removeRow(index.row());
        }
    }
    ui->statusbar->showMessage(QStringLiteral("已删除 %1 条消息").arg(deletedCount), 2200);
    m_messagesView->setMultiSelectMode(false);
}

void MainWindow::onMultiSelectFavoriteRequested() {
    if (!m_messagesView) return;

    const QModelIndexList selected = m_messagesView->chatListView()->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    const QString sessionId = m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget;
    int favoriteCount = 0;
    for (const QModelIndex& index : selected) {
        const QString text = index.data(Qt::DisplayRole).toString();
        const QString senderId = index.data(ChatBubbleSenderIdRole).toString();
        const QString senderName = index.data(ChatBubbleSenderNameRole).toString();
        const QString msgId = QStringLiteral("msg-%1-%2").arg(index.row()).arg(
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz")));
        QJsonObject messageObject;
        messageObject[QStringLiteral("id")] = msgId;
        messageObject[QStringLiteral("messageId")] = msgId;
        messageObject[QStringLiteral("senderId")] = senderId;
        messageObject[QStringLiteral("senderName")] = senderName;
        messageObject[QStringLiteral("content")] = text;
        messageObject[QStringLiteral("sessionId")] = sessionId;
        messageObject[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);

        QJsonObject payload;
        payload[QStringLiteral("message")] = messageObject;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        if (QQNTBackendService::handle(QStringLiteral("favorite_local_message"), payload, &response, &errorCode, &errorMessage)) {
            ++favoriteCount;
        }
    }
    ui->statusbar->showMessage(QStringLiteral("已收藏 %1 条消息").arg(favoriteCount), 2200);
    m_messagesView->setMultiSelectMode(false);
}

void MainWindow::onCaptureScreenshot(bool hideCurrentWindow) {
    // 1. Mirror the Composer screenshot setting. The default keeps the window
    // hidden, while the menu can intentionally capture the current window.
    if (hideCurrentWindow) {
        QJsonObject payload;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        QQNTBackendService::handle(QStringLiteral("hide_main_window"), payload, &response, &errorCode, &errorMessage);
        hide();
        QApplication::processEvents();
    }

    // 2. Compute the virtual desktop geometry across all monitors.
    const QList<QScreen*> screens = QGuiApplication::screens();
    QRect virtualRect;
    for (QScreen* screen : screens) {
        virtualRect = virtualRect.united(screen->geometry());
    }
    if (virtualRect.isEmpty()) {
        if (hideCurrentWindow) {
            show();
            raise();
            activateWindow();
            QJsonObject payload;
            QJsonObject response;
            QString errorCode;
            QString errorMessage;
            QQNTBackendService::handle(QStringLiteral("restore_main_window"), payload, &response, &errorCode, &errorMessage);
        }
        QMessageBox::warning(this, QStringLiteral("截图失败"), QStringLiteral("未找到可用显示器。"));
        return;
    }

    // 3. Composite every screen into one virtual-desktop QPixmap (no shared buffer).
    //    Each screen is grabbed at its own device pixel ratio and blitted into a
    //    uniformly-scaled canvas whose origin is the virtual desktop's top-left, so
    //    negative-coordinate and high-DPI secondary monitors map correctly.
    QList<qreal> dprs;
    for (QScreen* screen : screens) {
        dprs.append(screen->devicePixelRatio());
    }
    const qreal compositeScale = ScreenshotGeometry::compositeScale(dprs);
    QPixmap screenshot(QSize(qRound(virtualRect.width() * compositeScale),
                             qRound(virtualRect.height() * compositeScale)));
    if (!screenshot.isNull()) {
        screenshot.setDevicePixelRatio(1.0);
        screenshot.fill(Qt::black);
        QPainter compositor(&screenshot);
        for (QScreen* screen : screens) {
            QPixmap grab = screen->grabWindow(0);
            if (grab.isNull()) {
                continue;
            }
            // grabWindow returns a pixmap carrying the screen's DPR; draw it into the
            // canvas using logical target coordinates scaled by the composite scale.
            grab.setDevicePixelRatio(1.0);
            const QRectF target = ScreenshotGeometry::screenTargetRect(
                screen->geometry(), virtualRect, compositeScale);
            compositor.drawPixmap(target, grab, QRectF(grab.rect()));
        }
    }
    if (screenshot.isNull()) {
        if (hideCurrentWindow) {
            show();
            raise();
            activateWindow();
            QJsonObject payload;
            QJsonObject response;
            QString errorCode;
            QString errorMessage;
            QQNTBackendService::handle(QStringLiteral("restore_main_window"), payload, &response, &errorCode, &errorMessage);
        }
        QMessageBox::warning(this, QStringLiteral("截图失败"), QStringLiteral("无法获取当前屏幕内容。"));
        return;
    }

    // 4. Show a full-desktop selection overlay. The overlay is a top-level tool window
    //    and the main window is already hidden, so the screenshot excludes itself.
    ScreenshotCaptureWindow capture;
    capture.setCaptureGeometry(virtualRect);
    capture.setScreenshot(screenshot);
    connect(&capture, &ScreenshotCaptureWindow::saveRequested, this, [this](const QPixmap& cropped) {
        if (cropped.isNull()) return;
        const QString directory = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("QtNetworkChat/screenshots"));
        QDir().mkpath(directory);
        const QString filePath = QDir(directory).filePath(
            QStringLiteral("screenshot-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"))));
        if (!cropped.save(filePath, "PNG")) {
            QMessageBox::warning(this, QStringLiteral("截图失败"), QStringLiteral("无法保存截图文件。"));
            return;
        }
        qtnetworkchat::logDebug("Screenshot", QStringLiteral("saved crop to %1").arg(filePath));
        // Paste the screenshot into the composer as a file drop (sends as image/file).
        onComposerFilesDropped(QStringList{filePath});
    });
    capture.exec();
    if (hideCurrentWindow) {
        show();
        raise();
        activateWindow();
        QJsonObject payload;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        QQNTBackendService::handle(QStringLiteral("restore_main_window"), payload, &response, &errorCode, &errorMessage);
    }
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

void MainWindow::setTransferWorkspaceState(const TransferWorkspaceCardState& state) {
    ui->transferOverviewStageLabel->setText(state.stageText);
    ui->transferOverviewSummaryLabel->setText(state.summaryText);
    ui->transferOverviewDetailLabel->setText(state.detailText);
    ui->transferOverviewActionLabel->setText(state.actionText);
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
    scrollActiveChatToBottom();
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
    QStandardItem* item = new QStandardItem(QStringLiteral("发送了文件: %1 · %2").arg(info.fileName(), fileSize));
    item->setEditable(false);
    item->setData(bubbleTimestamp(completedAt), ChatBubbleTimestampRole);
    decorateChatItem(item, m_currentUserId, m_currentUserName, true);
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
    // A media bubble is the chat record; do not append a second generic
    // completion card underneath it.
    setTransferWorkspaceState(m_transferManager.localSendCompletedWorkspaceState(mediaType,
                                                                                 info.fileName(),
                                                                                 fileSize,
                                                                                 targetName,
                                                                                 completedAt));
    ui->chatHintLabel->setText(completedState.hintText);
    ui->statusbar->showMessage(completedState.statusMessage, completedState.statusTimeoutMs);
    scrollActiveChatToBottom();
}

void MainWindow::appendRemoteMediaTransferCompletion(const QString& filePath,
                                                     const TransferSendUiState& completedState,
                                                     bool isVideo,
                                                     const TransferWorkspaceCardState& workspaceState) {
    setTransferWorkspaceState(workspaceState);
    const TransferMediaPreviewPlan previewPlan = m_transferManager.remoteMediaPreviewPlan(completedState.cardText, isVideo);
    if (!isVideo) {
        const QPixmap pixmap = loadChatImagePreview(filePath);
        qtnetworkchat::logDebug("MainWindow", QStringLiteral("image preview load ok=%1 path=%2")
                .arg(pixmap.isNull() ? QStringLiteral("false") : QStringLiteral("true"), filePath));
        appendMediaPreviewItem(QString(),
                               pixmap,
                               previewPlan.isVideo,
                               previewPlan.alignRight,
                               filePath,
                               m_currentUserId,
                               m_currentUserName);
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
    scrollActiveChatToBottom();
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
    appendTransferCompletionState(completedState, false, false, QColor(0, 121, 107), QColor(232, 248, 245));
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
    if (!isVideo) {
        previewItem->setData(QStringLiteral("image"), ChatBubbleMediaKindRole);
        if (!pixmap.isNull()) {
            const QPixmap preview = pixmap.scaled(360, 260, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            previewItem->setData(preview, ChatBubbleMediaPreviewRole);
        } else {
            previewItem->setData(true, ChatBubbleMediaPreviewUnavailableRole);
        }
    } else if (isVideo) {
        previewItem->setData(QStringLiteral("video"), ChatBubbleMediaKindRole);
    }
    if (!openPath.trimmed().isEmpty()) {
        previewItem->setData(openPath.trimmed(), ChatBubbleMediaOpenPathRole);
        previewItem->setData(QStringLiteral("双击打开文件；右键可复制保存路径或打开目录\n%1").arg(openPath.trimmed()), Qt::ToolTipRole);
    }
    previewItem->setEditable(false);
    previewItem->setBackground(isVideo ? QColor(245, 240, 255) : QColor(246, 250, 253));
    if (isVideo) {
        previewItem->setForeground(QColor(126, 87, 194));
    }
    if (alignRight) {
        previewItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    decorateChatItem(previewItem,
                     senderId.isEmpty() ? m_currentUserId : senderId,
                     senderName.isEmpty() ? m_currentUserName : senderName,
                     alignRight);
    m_chatModel->appendRow(previewItem);
    if (m_messagesView) {
        m_messagesView->setEmptyStateVisible(false);
    }
    qtnetworkchat::logDebug("MainWindow", QStringLiteral("media bubble appended kind=%1 row=%2 path=%3")
            .arg(isVideo ? QStringLiteral("video") : QStringLiteral("image"),
                 QString::number(m_chatModel->rowCount() - 1),
                 openPath));
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
        const StoredChatMessage stored = parseStoredChatMessage(line);
        const bool outgoing = !stored.system && !m_currentUserName.isEmpty()
            && stored.senderName == m_currentUserName;
        QStandardItem* item = new QStandardItem(stored.body);
        item->setEditable(false);
        if (!stored.timestamp.isEmpty()) {
            item->setData(bubbleTimestamp(stored.timestamp), ChatBubbleTimestampRole);
        }
        decorateChatItem(item,
                         outgoing ? m_currentUserId : QString(),
                         stored.senderName,
                         outgoing,
                         stored.system);
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

    // Load local-only friend groups (friendId -> group name + custom groups).
    m_friendGroups.clear();
    m_customGroups.clear();
    const bool loadedFriendGroupsFromSqlite = ensureClientDatabase()
        && m_clientStorage.loadFriendGroupsFromSqlite(clientDbPath(), &m_friendGroups, &m_customGroups);
    if (!loadedFriendGroupsFromSqlite || (m_friendGroups.isEmpty() && m_customGroups.isEmpty())) {
        if (m_clientStorage.readFriendGroups(&m_friendGroups, &m_customGroups) && ensureClientDatabase()) {
            m_clientStorage.saveFriendGroupsToSqlite(clientDbPath(), m_friendGroups, m_customGroups);
        }
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
    refreshWorkspaceChrome();

    auto appendSessionItem = [this](const QString& id, const QString& name, const QString& lastMessage,
                                    bool isGroup, bool online, bool pinned, int unreadCount, bool atMention) {
        QStandardItem* item = new QStandardItem(name);
        item->setEditable(false);
        item->setData(id, SessionItemDelegate::SessionIdRole);
        item->setData(name, SessionItemDelegate::SessionNameRole);
        item->setData(lastMessage, SessionItemDelegate::SessionLastMessageRole);
        item->setData(QDateTime::currentDateTime(), SessionItemDelegate::SessionLastMessageTimestampRole);
        item->setData(unreadCount, SessionItemDelegate::SessionUnreadRole);
        item->setData(pinned, SessionItemDelegate::SessionPinnedRole);
        item->setData(atMention, SessionItemDelegate::SessionAtMentionRole);
        item->setData(online, SessionItemDelegate::SessionOnlineRole);
        item->setData(isGroup, SessionItemDelegate::SessionGroupRole);
        item->setData(id, Qt::UserRole + 1);
        m_userListModel->appendRow(item);
    };

    auto appendSection = [this](const QString& title) {
        QStandardItem* section = new QStandardItem(title);
        section->setEditable(false);
        section->setEnabled(false);
        section->setData(QStringLiteral("__section__"), SessionItemDelegate::SessionIdRole);
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
        appendSessionItem(friendId, name, QStringLiteral("[离线] 暂无消息"), false, false, false, 0, false);
        ++visibleCount;
        ++visibleFriends;
    }

    appendSection("群聊");
    for (const QString& groupId : m_localGroupIds) {
        QString groupName = m_localGroupNames.value(groupId, "群聊");
        if (!matchesFilter(groupId, groupName)) continue;
        appendSessionItem(groupId, groupName, m_localGroupAnnouncements.value(groupId, QStringLiteral("[本地群聊]")), true, false, false, 0, false);
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
        const QString lastMessage = isPending ? QStringLiteral("[申请中]") : QStringLiteral("[在线] 暂无消息");
        appendSessionItem(user.id, user.name, lastMessage, false, true, false, 0, false);
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

    refreshSessionList();
    refreshContactsAndProfile();
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

void MainWindow::refreshFavoritesView() {
    if (!m_favoritesView) return;
    QStandardItemModel* model = m_favoritesView->model();
    if (!model) return;
    model->clear();

    if (!QQNTBackendService::isCommand(QStringLiteral("get_local_favorite_messages"))) {
        return;
    }
    QJsonObject payload;
    QJsonObject response;
    QString errorCode;
    QString errorMessage;
    if (!QQNTBackendService::handle(QStringLiteral("get_local_favorite_messages"),
                                    payload, &response, &errorCode, &errorMessage)) {
        qDebug() << "get_local_favorite_messages failed:" << errorCode << errorMessage;
        m_favoritesView->setEmptyStateVisible(true);
        return;
    }

    const QJsonArray messages = response.value(QStringLiteral("messages")).toArray();
    QSet<QString> addedSessionHeaders;
    for (const QJsonValue& value : messages) {
        const QJsonObject msg = value.toObject();
        const QString sessionId = msg.value(QStringLiteral("sessionId")).toString();
        const QString messageId = msg.value(QStringLiteral("messageId")).toString(msg.value(QStringLiteral("id")).toString());
        QString senderName = msg.value(QStringLiteral("senderName")).toString().trimmed();
        QString content = msg.value(QStringLiteral("content")).toString().trimmed();
        const QString timestampRaw = msg.value(QStringLiteral("timestamp")).toString();

        // Older favorite snapshots preserve the pre-QQNT archive line in
        // content. Reuse the chat-history parser so FavoritesList receives the
        // same sender/body separation as tauri-qqnt.
        const StoredChatMessage stored = parseStoredChatMessage(content);
        if (!stored.senderName.isEmpty() || stored.system) {
            if (senderName.isEmpty()) {
                senderName = stored.senderName;
            }
            content = stored.body;
        }

        const QString sessionName = sessionId == QStringLiteral("public")
            ? QStringLiteral("公共聊天室")
            : (m_localGroupIds.contains(sessionId)
                   ? m_localGroupNames.value(sessionId, QStringLiteral("群聊"))
                   : contactDisplayName(sessionId));

        if (!addedSessionHeaders.contains(sessionId)) {
            QStandardItem* header = new QStandardItem(sessionName.isEmpty() ? sessionId : sessionName);
            header->setEditable(false);
            header->setData(true, Qt::UserRole + 6);
            header->setData(sessionName.isEmpty() ? sessionId : sessionName, Qt::UserRole + 5);
            header->setData(QStringLiteral("%1 %2").arg(sessionName, content), Qt::UserRole + 8);
            model->appendRow(header);
            addedSessionHeaders.insert(sessionId);
        }

        const QString type = msg.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("image")) {
            content = QStringLiteral("[图片]");
        } else if (type == QStringLiteral("file")) {
            const QString fileName = msg.value(QStringLiteral("fileInfo")).toObject().value(QStringLiteral("name")).toString();
            content = QStringLiteral("[文件] %1").arg(fileName.isEmpty() ? content : fileName);
        }
        QDateTime timestamp = QDateTime::fromString(timestampRaw, Qt::ISODate);
        if (!timestamp.isValid()) timestamp = QDateTime::fromString(timestampRaw, Qt::ISODateWithMs);
        QString displayTime = timestamp.isValid()
            ? timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm"))
            : timestampRaw;
        if (displayTime.isEmpty() && !stored.timestamp.isEmpty()) {
            displayTime = stored.timestamp;
        }

        QStandardItem* item = new QStandardItem();
        item->setEditable(false);
        item->setData(sessionId, Qt::UserRole);
        item->setData(messageId, Qt::UserRole + 1);
        item->setData(senderName.isEmpty() ? QStringLiteral("未知用户") : senderName, Qt::UserRole + 2);
        item->setData(content, Qt::UserRole + 3);
        item->setData(displayTime, Qt::UserRole + 4);
        item->setData(sessionName, Qt::UserRole + 5);
        item->setData(false, Qt::UserRole + 6);
        item->setData(msg, Qt::UserRole + 7);
        item->setData(QStringLiteral("%1 %2 %3 %4").arg(sessionName, senderName, content, displayTime), Qt::UserRole + 8);
        item->setToolTip(QStringLiteral("%1\n%2").arg(content, displayTime));
        model->appendRow(item);
    }
    m_favoritesView->setEmptyStateVisible(messages.isEmpty());
}

void MainWindow::onFavoriteSelected(const QString& sessionId, const QString& messageId) {
    // Switch to the originating session, then jump to the message page so the user
    // lands on the conversation that holds the favorited message.
    if (sessionId == QStringLiteral("public") || sessionId.isEmpty()) {
        onBackToGroupChat();
    } else if (m_localGroupIds.contains(sessionId)) {
        switchToLocalGroup(sessionId, m_localGroupNames.value(sessionId, QStringLiteral("群聊")));
    } else {
        openPrivateSession(sessionId);
    }

    if (m_viewStack) {
        m_viewStack->setCurrentIndex(0);
    }
    if (m_appNav) {
        m_appNav->setCurrentIndex(0);
    }

    // Best-effort locate: highlight the matching row if it is present in the loaded history.
    if (!messageId.isEmpty() && m_chatModel && m_messagesView) {
        for (int row = 0; row < m_chatModel->rowCount(); ++row) {
            QStandardItem* item = m_chatModel->item(row);
            if (item && item->data(ChatBubbleMessageIdRole).toString() == messageId) {
                const QModelIndex idx = m_chatModel->index(row, 0);
                if (QListView* view = m_messagesView->chatListView()) {
                    view->scrollTo(idx, QAbstractItemView::PositionAtCenter);
                    view->setCurrentIndex(idx);
                }
                break;
            }
        }
    }
    ui->statusbar->showMessage(QStringLiteral("已定位收藏消息所在会话"), 1800);
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
