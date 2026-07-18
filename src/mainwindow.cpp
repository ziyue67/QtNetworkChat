#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "sessionitemdelegate.h"
#include "sessionlistbuilder.h"
#include "chatbubbledelegate.h"
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

namespace {
void qqntLog(const QString& tag, const QString& msg)
{
    const QString line = QString("[%1][%2] %3")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")))
                             .arg(tag)
                             .arg(msg);
    qDebug().noquote() << line;
    QFile f(QStringLiteral("qqnt-debug.log"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream s(&f);
        s << line << "\n";
        s.flush();
        f.close();
    }
}
}

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
#include <QWindow>
#include <functional>
#include <algorithm>

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
    ChatMessageIdRole
};

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
        const bool system = index.data(ChatSystemRole).toBool();
        const int width = qMax(360, option.rect.width() > 0 ? option.rect.width() : 640);
        QFontMetrics fm(option.font);
        const int maxTextWidth = system ? width - 80 : qMin(520, qMax(240, width - 156));
        const QString mediaKind = index.data(ChatMediaKindRole).toString();
        const QVariant mediaPreviewData = index.data(ChatMediaPreviewRole);
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
        const bool outgoing = index.data(ChatOutgoingRole).toBool();
        const bool system = index.data(ChatSystemRole).toBool();
        const QString mediaKind = index.data(ChatMediaKindRole).toString();
        const bool hasImagePreview = mediaKind == QLatin1String("image")
            && index.data(ChatMediaPreviewRole).canConvert<QPixmap>()
            && !index.data(ChatMediaPreviewRole).value<QPixmap>().isNull();
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
        const QString avatarPath = index.data(ChatAvatarPathRole).toString();
        if (!avatarPath.isEmpty()) {
            avatar.load(avatarPath);
        }
        if (avatar.isNull()) {
            avatar = generatedPeerAvatarIcon(index.data(ChatSenderNameRole).toString(),
                                             index.data(ChatSenderIdRole).toString(),
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
            mediaPreview = index.data(ChatMediaPreviewRole).value<QPixmap>();
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
    qqntLog("MainWindow", "constructor start");
    ui->setupUi(this);
    qqntLog("MainWindow", "ui setup done");

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
    qqntLog("MainWindow", "shadow effects applied");

    // Load theme-aware stylesheet
    loadStyleSheet();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MainWindow::loadStyleSheet);
    qqntLog("MainWindow", "stylesheet loaded");

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    resize(1100, 740);
    setMinimumSize(860, 540);
    setWindowIcon(createChatIcon(userName));
    setupUi();
    qqntLog("MainWindow", "setupUi done");
    setupTray();
    qqntLog("MainWindow", "setupTray done");
    setupQQNT();
    qqntLog("MainWindow", "setupQQNT done");

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
    qqntLog("MainWindow", "before saveProfileToSqlite");
    saveProfileToSqlite();
    qqntLog("MainWindow", "after saveProfileToSqlite");
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
    qqntLog("MainWindow", "before refreshWorkspaceChrome");
    refreshWorkspaceChrome();
    qqntLog("MainWindow", "after refreshWorkspaceChrome");
    qqntLog("MainWindow", "before loadHistory");
    loadHistory("group");
    qqntLog("MainWindow", "after loadHistory");
    qqntLog("MainWindow", "before updateSavedOutgoingTransferRecoveryUi");
    updateSavedOutgoingTransferRecoveryUi(true);
    qqntLog("MainWindow", "constructor end");
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
    if (openPath.isEmpty()) openPath = index.data(ChatMediaOpenPathRole).toString().trimmed();
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

bool MainWindow::handleCreateMenuCommand(const QString& commandId) {
    if (commandId == QLatin1String("create-group")) {
        CreateGroupDialog dialog(this);
        QStringList recentFriendIds;
        if (m_sessionModel) {
            for (int row = 0; row < m_sessionModel->rowCount(); ++row) {
                const QModelIndex index = m_sessionModel->index(row, 0);
                const QString id = index.data(SessionItemDelegate::SessionIdRole).toString();
                const bool isGroup = index.data(SessionItemDelegate::SessionGroupRole).toBool();
                if (!isGroup && m_friendIds.contains(id) && !recentFriendIds.contains(id)) recentFriendIds << id;
            }
        }
        dialog.setCandidateMembers(m_friendIds, m_friendNames, recentFriendIds);
        if (dialog.exec() != QDialog::Accepted) {
            return true;
        }
        QString groupName = dialog.groupName().trimmed();
        if (groupName.isEmpty()) {
            groupName = QStringLiteral("我的群聊");
        }
        const QStringList members = dialog.selectedMembers();
        const QString category = dialog.selectedCategory();

        const QString groupId = members.isEmpty()
            ? createLocalGroupSession(groupName)
            : createLocalGroupSession(
                  groupName,
                  members,
                  category.isEmpty()
                      ? QStringLiteral("%1 已创建，已邀请 %2 位好友。").arg(groupName).arg(members.size())
                      : QStringLiteral("群分类：%1").arg(category));
        const QMap<QString, QColor> avatarColors = {
            {QStringLiteral("blue"), QColor(QStringLiteral("#12a4ff"))},
            {QStringLiteral("green"), QColor(QStringLiteral("#18c98b"))},
            {QStringLiteral("orange"), QColor(QStringLiteral("#ff9f1a"))},
            {QStringLiteral("pink"), QColor(QStringLiteral("#fb6f92"))},
            {QStringLiteral("cyan"), QColor(QStringLiteral("#12c9bd"))},
            {QStringLiteral("purple"), QColor(QStringLiteral("#8b7cf6"))}
        };
        const QString avatarId = dialog.selectedAvatarId();
        QPixmap avatarPixmap(80, 80);
        avatarPixmap.fill(Qt::transparent);
        {
            QPainter painter(&avatarPixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(avatarColors.value(avatarId, avatarColors.value(QStringLiteral("blue"))));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(0, 0, 80, 80);
            painter.setPen(Qt::white);
            QFont font = painter.font();
            font.setBold(true);
            font.setPixelSize(30);
            painter.setFont(font);
            const QString avatarText = avatarId == QStringLiteral("green") ? QStringLiteral("G")
                : avatarId == QStringLiteral("orange") ? QStringLiteral("C")
                : avatarId == QStringLiteral("pink") ? QStringLiteral("N")
                : avatarId == QStringLiteral("cyan") ? QStringLiteral("T")
                : avatarId == QStringLiteral("purple") ? QStringLiteral("群") : QStringLiteral("Q");
            painter.drawText(avatarPixmap.rect(), Qt::AlignCenter, avatarText);
        }
        const QString groupAvatarDir = QDir(ClientStorage::appDataRootDirectory()).filePath(QStringLiteral("group_avatars"));
        QDir().mkpath(groupAvatarDir);
        const QString groupAvatarPath = QDir(groupAvatarDir).filePath(groupId + QStringLiteral(".png"));
        if (avatarPixmap.save(groupAvatarPath, "PNG")) m_localGroupAvatarPaths[groupId] = groupAvatarPath;
        switchToLocalGroup(groupId, groupName);
        showMessagesView();
        const QString created = members.isEmpty()
            ? QStringLiteral("已创建群聊: %1").arg(groupName)
            : QStringLiteral("已创建群聊: %1，并邀请 %2 位好友").arg(groupName).arg(members.size());
        appendSystemMessage(created);
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
    const QString senderId = index.data(ChatSenderIdRole).toString();
    const QString senderName = index.data(ChatSenderNameRole).toString();
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
        qqntLog("MainWindow", QStringLiteral("direct image send start row=%1 file=%2")
                .arg(QString::number(optimisticRow), selectedFile.info.fileName()));

        const bool ok = m_client && m_client->sendImage(selectedFile.filePath, m_privateChatTarget);
        qqntLog("MainWindow", QStringLiteral("direct image send finished ok=%1 file=%2")
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
        const QString senderId = index.data(ChatSenderIdRole).toString();
        const QString senderName = index.data(ChatSenderNameRole).toString();
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
    if (mediaKind.isEmpty()) mediaKind = index.data(ChatMediaKindRole).toString();
    QString openPath = index.data(ChatBubbleMediaOpenPathRole).toString().trimmed();
    if (openPath.isEmpty()) openPath = index.data(ChatMediaOpenPathRole).toString().trimmed();

    if (mediaKind == QLatin1String("image")) {
        ImagePreviewWindow preview(this);
        preview.setWindowTitle(QStringLiteral("图片预览"));
        QVariant previewData = index.data(ChatBubbleMediaPreviewRole);
        if (!previewData.isValid()) previewData = index.data(ChatMediaPreviewRole);
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
        const QString senderId = index.data(ChatSenderIdRole).toString();
        const QString senderName = index.data(ChatSenderNameRole).toString();
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
        const QString senderId = index.data(ChatSenderIdRole).toString();
        const QString senderName = index.data(ChatSenderNameRole).toString();
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
        qqntLog("Screenshot", QStringLiteral("saved crop to %1").arg(filePath));
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

void MainWindow::onShowQuickAddFriend() {
    // Keep legacy toolbar/friend-manager entry points on the same compact
    // QQNT add-friend flow used by ContactsView.
    showAddFriendDialog();
    return;

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
        const FriendNoticeUiState noticeState = m_friendManager.noticeUiState(m_pendingFriendRequests.size());
        ui->friendNoticeBtn->setText(noticeState.text);
        ui->friendNoticeBtn->setToolTip(noticeState.toolTip);
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
        const QString keyword = searchEdit->text().trimmed();
        for (auto it = m_pendingGroupJoinApplications.constBegin(); it != m_pendingGroupJoinApplications.constEnd(); ++it) {
            const QJsonObject application = it.value();
            const QString groupName = application.value("groupName").toString(QStringLiteral("群聊"));
            const QString applicantName = application.value("applicantName").toString(application.value("applicantId").toString());
            const QString message = application.value("message").toString();
            const QString searchable = groupName + applicantName + application.value("applicantId").toString() + message;
            if (!keyword.isEmpty() && !searchable.contains(keyword, Qt::CaseInsensitive)) continue;
            auto* item = new QListWidgetItem(
                QStringLiteral("入群申请 · %1\n%2 申请加入 · %3%4")
                    .arg(groupName, applicantName, application.value("applicantId").toString(),
                         message.isEmpty() ? QString() : QStringLiteral("\n验证信息：%1").arg(message)), noticeList);
            item->setData(Qt::UserRole, QStringLiteral("join_request:") + it.key());
            item->setSizeHint(QSize(0, 70));
            item->setForeground(QColor(18, 150, 247));
            item->setToolTip(QStringLiteral("选择后可同意或拒绝该入群申请"));
            noticeList->insertItem(0, item);
        }
        for (auto it = m_pendingOutgoingGroupJoinApplications.constBegin(); it != m_pendingOutgoingGroupJoinApplications.constEnd(); ++it) {
            const QJsonObject application = it.value();
            const QString groupName = application.value("groupName").toString(QStringLiteral("群聊"));
            const QString message = application.value("message").toString();
            const QString searchable = groupName + application.value("groupId").toString() + message;
            if (!keyword.isEmpty() && !searchable.contains(keyword, Qt::CaseInsensitive)) continue;
            auto* item = new QListWidgetItem(
                QStringLiteral("入群申请中 · %1\n群号：%2 · 等待群主或管理员同意%3")
                    .arg(groupName, application.value("groupId").toString(),
                         message.isEmpty() ? QString() : QStringLiteral("\n验证信息：%1").arg(message)), noticeList);
            item->setData(Qt::UserRole, QStringLiteral("join_pending:") + it.key());
            item->setSizeHint(QSize(0, 64));
            item->setToolTip(QStringLiteral("入群申请已提交，等待群主或管理员同意"));
            noticeList->insertItem(0, item);
        }
        for (auto it = m_rejectedOutgoingGroupJoinApplications.constBegin(); it != m_rejectedOutgoingGroupJoinApplications.constEnd(); ++it) {
            const QJsonObject application = it.value();
            const QString groupName = application.value("groupName").toString(QStringLiteral("群聊"));
            const QString reviewer = application.value("reviewerName").toString(QStringLiteral("群主或管理员"));
            const QString searchable = groupName + application.value("groupId").toString() + reviewer;
            if (!keyword.isEmpty() && !searchable.contains(keyword, Qt::CaseInsensitive)) continue;
            auto* item = new QListWidgetItem(
                QStringLiteral("入群申请被拒绝 · %1\n%2 拒绝了你的加群申请")
                    .arg(groupName, reviewer), noticeList);
            item->setData(Qt::UserRole, QStringLiteral("join_rejected:") + it.key());
            item->setSizeHint(QSize(0, 56));
            item->setForeground(ThemeManager::instance()->dangerColor());
            item->setToolTip(QStringLiteral("入群申请已被群主或管理员拒绝"));
            noticeList->insertItem(0, item);
        }
        countLabel->setText(QStringLiteral("%1 个群聊 · %2 条待处理 · %3 条申请中")
                            .arg(m_localGroupIds.size() + 1)
                            .arg(m_pendingGroupJoinApplications.size())
                            .arg(m_pendingOutgoingGroupJoinApplications.size()));
        if (noticeList->count() > 0) noticeList->setCurrentRow(0);
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
    QPushButton* approveJoinBtn = new QPushButton(QStringLiteral("同意"), &dialog);
    approveJoinBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    approveJoinBtn->setToolTip(QStringLiteral("同意选中的入群申请"));
    QPushButton* rejectJoinBtn = new QPushButton(QStringLiteral("拒绝"), &dialog);
    rejectJoinBtn->setObjectName(QStringLiteral("dialogDangerBtn"));
    rejectJoinBtn->setToolTip(QStringLiteral("拒绝选中的入群申请"));
    QPushButton* leaveGroupBtn = new QPushButton(QStringLiteral("退群"), &dialog);
    leaveGroupBtn->setObjectName(QStringLiteral("dialogDangerBtn"));
    leaveGroupBtn->setToolTip(QStringLiteral("退出选中的服务器群聊"));
    groupMainActionLayout->addWidget(openBtn);
    groupMainActionLayout->addWidget(copyBtn);
    groupMainActionLayout->addWidget(cardBtn);
    groupMainActionLayout->addWidget(announceBtn);
    groupMainActionLayout->addWidget(approveJoinBtn);
    groupMainActionLayout->addWidget(rejectJoinBtn);
    groupMainActionLayout->addWidget(leaveGroupBtn);
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
        const QString entryId = selectedGroupNoticeEntryId(noticeList);
        if (entryId.startsWith(QStringLiteral("join_request:"))) {
            const QJsonObject application = m_pendingGroupJoinApplications.value(entryId.mid(QStringLiteral("join_request:").size()));
            const QString applicant = application.value("applicantName").toString(application.value("applicantId").toString());
            const QString message = application.value("message").toString();
            groupPreviewLabel->setText(QStringLiteral("%1 申请加入“%2”\nQQ:%3%4")
                .arg(applicant,
                     application.value("groupName").toString(QStringLiteral("群聊")),
                     application.value("applicantId").toString(),
                     message.isEmpty() ? QString() : QStringLiteral("\n验证信息：%1").arg(message)));
            return;
        }
        if (entryId.startsWith(QStringLiteral("join_pending:"))) {
            const QJsonObject application = m_pendingOutgoingGroupJoinApplications.value(entryId.mid(QStringLiteral("join_pending:").size()));
            groupPreviewLabel->setText(QStringLiteral("已申请加入“%1”\n群号：%2\n当前状态：等待群主或管理员同意%3")
                .arg(application.value("groupName").toString(QStringLiteral("群聊")),
                     application.value("groupId").toString(),
                     application.value("message").toString().isEmpty() ? QString() : QStringLiteral("\n验证信息：%1").arg(application.value("message").toString())));
            return;
        }
        if (entryId.startsWith(QStringLiteral("join_rejected:"))) {
            const QJsonObject application = m_rejectedOutgoingGroupJoinApplications.value(entryId.mid(QStringLiteral("join_rejected:").size()));
            groupPreviewLabel->setText(QStringLiteral("“%1”已拒绝你的加群申请\n群名：%2\n群号：%3")
                .arg(application.value("reviewerName").toString(QStringLiteral("群主或管理员")),
                     application.value("groupName").toString(QStringLiteral("群聊")),
                     application.value("groupId").toString()));
            return;
        }
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        groupPreviewLabel->setText(snapshot.previewText);
    };
    auto updateGroupActionState = [=]() {
        const bool joinRequestSelected = selectedGroupNoticeEntryId(noticeList).startsWith(QStringLiteral("join_request:"));
        const bool joinPendingSelected = selectedGroupNoticeEntryId(noticeList).startsWith(QStringLiteral("join_pending:"));
        const bool joinRejectedSelected = selectedGroupNoticeEntryId(noticeList).startsWith(QStringLiteral("join_rejected:"));
        const GroupNoticeActionState state = NotificationPanelManager::groupNoticeActionState(
            selectedGroupNoticeEntryId(noticeList),
            noticeList->currentItem() != nullptr,
            !searchEdit->text().trimmed().isEmpty(),
            noticeList->count());
        openBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.openEnabled);
        openBtn->setText(state.openText);
        openBtn->setToolTip(state.openToolTip);
        copyBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyIdEnabled);
        copyBtn->setToolTip(state.copyIdToolTip);
        cardBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyCardEnabled);
        cardBtn->setToolTip(state.copyCardToolTip);
        announceBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyAnnouncementEnabled);
        announceBtn->setToolTip(state.copyAnnouncementToolTip);
        memberBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyMembersEnabled);
        memberBtn->setToolTip(state.copyMembersToolTip);
        onlineMemberBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyOnlineMembersEnabled);
        onlineMemberBtn->setToolTip(state.copyOnlineMembersToolTip);
        inviteTextBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyInviteEnabled);
        inviteTextBtn->setToolTip(state.copyInviteToolTip);
        copyGroupMediaPackBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyMediaPackEnabled);
        copyGroupMediaPackBtn->setToolTip(state.copyMediaPackToolTip);
        copyGroupBatchPlanBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyBatchPlanEnabled);
        copyGroupBatchPlanBtn->setToolTip(state.copyBatchPlanToolTip);
        copyMediaGuideBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyMediaGuideEnabled);
        copyMediaGuideBtn->setToolTip(state.copyMediaGuideToolTip);
        approveJoinBtn->setVisible(joinRequestSelected);
        rejectJoinBtn->setVisible(joinRequestSelected);
        const QString entryId = selectedGroupNoticeEntryId(noticeList);
        const bool canLeaveGroup = !joinRequestSelected
            && m_joinedServerSearchGroups.values().contains(entryId)
            && m_serverGroupOwners.value(m_joinedServerSearchGroups.key(entryId)) != m_currentUserId;
        leaveGroupBtn->setVisible(canLeaveGroup);
        hintLabel->setText(joinRequestSelected ? QStringLiteral("请选择同意或拒绝处理入群申请")
            : (joinPendingSelected ? QStringLiteral("入群申请处理中，等待群主或管理员同意")
            : (joinRejectedSelected ? QStringLiteral("该入群申请已被拒绝") : state.hintText)));
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
    auto respondToSelectedJoinRequest = [this, noticeList, fillGroups, updateGroupPreview, updateGroupActionState](bool accepted) {
        const QString entryId = selectedGroupNoticeEntryId(noticeList);
        static const QString prefix = QStringLiteral("join_request:");
        if (!entryId.startsWith(prefix)) {
            ui->statusbar->showMessage(QStringLiteral("请先选择要处理的入群申请"), 1800);
            return;
        }
        const QString requestId = entryId.mid(prefix.size());
        if (!m_client || !m_client->respondServerGroupJoinRequest(requestId, accepted)) {
            ui->statusbar->showMessage(QStringLiteral("入群申请处理发送失败，请检查连接"), 2800);
            return;
        }
        fillGroups();
        updateGroupPreview();
        updateGroupActionState();
        ui->statusbar->showMessage(accepted ? QStringLiteral("已同意入群申请") : QStringLiteral("已拒绝入群申请"), 2200);
        ui->groupNoticeBtn->setText(m_pendingGroupJoinApplications.isEmpty()
            ? QStringLiteral("群通知")
            : QStringLiteral("群通知 %1").arg(m_pendingGroupJoinApplications.size()));
    };
    connect(approveJoinBtn, &QPushButton::clicked, &dialog, [respondToSelectedJoinRequest]() { respondToSelectedJoinRequest(true); });
    connect(rejectJoinBtn, &QPushButton::clicked, &dialog, [respondToSelectedJoinRequest]() { respondToSelectedJoinRequest(false); });
    connect(leaveGroupBtn, &QPushButton::clicked, &dialog, [this, noticeList, &dialog]() {
        const QString localId = selectedGroupNoticeEntryId(noticeList);
        const QString serverId = m_joinedServerSearchGroups.key(localId);
        if (serverId.isEmpty() || !m_client) return;
        if (QMessageBox::question(&dialog, QStringLiteral("退出群聊"),
                                  QStringLiteral("确定退出“%1”吗？").arg(m_localGroupNames.value(localId, QStringLiteral("群聊"))),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            m_client->leaveServerGroup(serverId);
        }
    });
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
        qqntLog("MainWindow", QStringLiteral("image preview load ok=%1 path=%2")
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
    qqntLog("MainWindow", QStringLiteral("media bubble appended kind=%1 row=%2 path=%3")
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

    item->setData(senderId, ChatSenderIdRole);
    item->setData(senderName, ChatSenderNameRole);
    item->setData(chatAvatarPath(senderId), ChatAvatarPathRole);
    item->setData(outgoing, ChatOutgoingRole);
    item->setData(system, ChatSystemRole);
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
            if (item && item->data(ChatMessageIdRole).toString() == messageId) {
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

void MainWindow::refreshGroupMemberPanel() {
    // Keep the visible QQNT sidebar in sync with every legacy-panel refresh
    // (member snapshots, mute/role changes, kicks). The sidebar computes its own
    // data and does not call back here, so this cannot recurse.
    refreshGroupMemberSidebar();
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
        refreshSessionSummary();
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
        QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n%3 · %4 · %5").arg(isFriend ? "好友" : (isPending ? "申请中" : "成员"), user.id, user.name, online ? "在线" : "离线", isFriend ? "已是好友" : (isPending ? "等待确认" : "双击发送申请")));
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
    refreshSessionSummary();
    // Keep the visible QQNT sidebar in sync with the same data.
    refreshGroupMemberSidebar();
}

void MainWindow::refreshGroupMemberSidebar() {
    // The QQNT sidebar is the visible group-member surface (the legacy
    // ui->groupMemberListView lives in the hidden compatibility widget). Feed it
    // the same local/server group data the legacy panel computes.
    if (!m_messagesView) return;
    GroupMemberSidebar* sidebar = m_messagesView->groupMemberSidebar();
    if (!sidebar) return;

    const QString serverGroupId = m_privateChatTarget.isEmpty()
        ? QStringLiteral("public")
        : m_joinedServerSearchGroups.key(m_privateChatTarget);
    const bool isServerGroup = !serverGroupId.isEmpty();
    const bool isLocalGroup = !isServerGroup && !m_privateChatTarget.isEmpty()
        && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    if (!isLocalGroup && !isServerGroup) {
        // Not a group session: hide the rail.
        m_messagesView->setGroupMemberSidebarVisible(false);
        return;
    }

    QList<GroupMemberDisplayData> members;
    QString ownerId;
    QString groupName;
    QString announcement;

    if (isLocalGroup) {
        groupName = m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊"));
        announcement = m_localGroupAnnouncements.value(m_privateChatTarget);
        QStringList ids = m_localGroupMembers.value(m_privateChatTarget);
        if (ids.isEmpty()) ids << m_currentUserId;
        ownerId = ids.first();
        for (const QString& id : ids) {
            if (id.trimmed().isEmpty()) continue;
            GroupMemberDisplayData d;
            d.id = id;
            d.nickname = id == m_currentUserId ? m_currentUserName : contactDisplayName(id);
            d.role = (id == ownerId) ? QStringLiteral("owner") : QStringLiteral("member");
            d.isOnline = isContactOnline(id) || id == m_currentUserId;
            members.append(d);
        }
    } else {
        groupName = m_serverGroupNames.value(serverGroupId, QStringLiteral("群聊"));
        announcement = m_serverGroupAnnouncements.value(serverGroupId);
        ownerId = m_serverGroupOwners.value(serverGroupId);
        const QStringList ids = m_serverGroupMembers.value(serverGroupId);
        for (const QString& id : ids) {
            if (id.trimmed().isEmpty()) continue;
            GroupMemberDisplayData d;
            d.id = id;
            d.nickname = id == m_currentUserId
                ? m_currentUserName
                : m_serverGroupMemberNames.value(serverGroupId + QStringLiteral("|") + id, contactDisplayName(id));
            const QString serverRole =
                m_serverGroupMemberRoles.value(serverGroupId + QStringLiteral("|") + id, QStringLiteral("member")).toLower();
            if (id == ownerId) {
                d.role = QStringLiteral("owner");
            } else if (serverRole == QStringLiteral("admin")) {
                d.role = QStringLiteral("admin");
            } else {
                d.role = QStringLiteral("member");
            }
            d.isOnline = id == m_currentUserId || isContactOnline(id);
            members.append(d);
        }
        // Server group without a member snapshot yet: fall back to just showing self.
        if (members.isEmpty() && !m_currentUserId.isEmpty()) {
            GroupMemberDisplayData self;
            self.id = m_currentUserId;
            self.nickname = m_currentUserName;
            self.role = (m_currentUserId == ownerId) ? QStringLiteral("owner") : QStringLiteral("member");
            self.isOnline = true;
            members.append(self);
        }
    }

    sidebar->setGroupId(isServerGroup ? serverGroupId : m_privateChatTarget);
    sidebar->setGroupName(groupName);
    sidebar->setAnnouncement(announcement);
    sidebar->setMembers(members);
    sidebar->setManagementEnabled(isLocalGroup ? isCurrentUserGroupOwner(m_privateChatTarget)
                                                : canCurrentUserManageServerGroup(serverGroupId));
    m_messagesView->setGroupMemberSidebarVisible(true);
}

void MainWindow::connectGroupMemberSidebar() {
    if (!m_messagesView) return;
    GroupMemberSidebar* sidebar = m_messagesView->groupMemberSidebar();
    if (!sidebar) return;

    // A member context action only applies to the current group session. These
    // helpers mirror the legacy right-click handlers' permission checks so the
    // sidebar and the (hidden) legacy list stay behaviourally identical.
    auto currentServerGroupId = [this]() {
        return m_privateChatTarget.isEmpty()
            ? QStringLiteral("public")
            : m_joinedServerSearchGroups.key(m_privateChatTarget);
    };
    auto isLocalGroup = [currentServerGroupId, this]() {
        return currentServerGroupId().isEmpty() && !m_privateChatTarget.isEmpty()
            && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    };

    connect(sidebar, &GroupMemberSidebar::chatWithMember, this, [this](const QString& userId) {
        if (userId.isEmpty() || userId == m_currentUserId) return;
        ensureFriendRequestQueued(userId,
                                  QStringLiteral("已向群成员发送好友申请 QQ:%1，等待对方同意"),
                                  true);
        openPrivateSession(userId);
        showMessagesView();
    });
    connect(sidebar, &GroupMemberSidebar::atMember, this, [this](const QString& userId, const QString& displayName) {
        Q_UNUSED(userId);
        if (!m_messagesView || !m_messagesView->composer()) return;
        QTextEdit* input = m_messagesView->composer()->inputEdit();
        if (!input) return;
        input->insertPlainText(QStringLiteral("@%1 ").arg(displayName));
        input->setFocus();
    });
    connect(sidebar, &GroupMemberSidebar::viewProfile, this, [this, currentServerGroupId](const QString& userId) {
        if (userId.isEmpty()) return;
        const QString serverGroupId = currentServerGroupId();
        const bool local = serverGroupId.isEmpty() && !m_privateChatTarget.isEmpty()
            && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
        const QString groupName = local
            ? m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊"))
            : m_serverGroupNames.value(serverGroupId, QStringLiteral("群聊"));
        QString role = QStringLiteral("成员");
        if (userId == groupOwnerId(m_privateChatTarget)
            || userId == m_serverGroupOwners.value(serverGroupId)) {
            role = QStringLiteral("群主");
        } else if (m_serverGroupMemberRoles.value(serverGroupId + QStringLiteral("|") + userId).toLower()
                   == QStringLiteral("admin")) {
            role = QStringLiteral("管理员");
        }
        MemberProfileCard card(this);
        card.setMemberInfo(userId, contactDisplayName(userId), role, groupName,
                           userId == m_currentUserId || isContactOnline(userId));
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
    });
    connect(sidebar, &GroupMemberSidebar::addFriend, this, [this](const QString& userId) {
        ensureFriendRequestQueued(userId,
                                  QStringLiteral("已向群成员发送好友申请 QQ:%1，等待对方同意"),
                                  true);
    });
    connect(sidebar, &GroupMemberSidebar::renameMember, this, [this](const QString& userId, const QString& currentName) {
        GroupNicknameDialog dlg(this);
        dlg.setCurrentNickname(currentName.isEmpty() ? contactDisplayName(userId) : currentName);
        connect(&dlg, &GroupNicknameDialog::nicknameConfirmed, this, [this, userId](const QString& nick) {
            const QString remark = nick.trimmed();
            if (remark.isEmpty()) return;
            m_friendNames[userId] = remark;
            if (m_friendIds.contains(userId)) {
                saveFriends();
            }
            refreshFriendList();
            refreshGroupMemberPanel();
            appendSystemMessage(QStringLiteral("已设置 %1 的群昵称为 %2").arg(userId, remark));
        });
        dlg.exec();
    });
    connect(sidebar, &GroupMemberSidebar::muteMember, this, [this, isLocalGroup, currentServerGroupId](const QString& userId, int minutes) {
        if (userId.isEmpty() || userId == m_currentUserId) {
            ui->statusbar->showMessage(QStringLiteral("无法对该成员执行禁言"), 2000);
            return;
        }
        if (isLocalGroup()) {
            ui->statusbar->showMessage(QStringLiteral("本地群聊暂不支持服务端禁言"), 2400);
            return;
        }
        const QString serverGroupId = currentServerGroupId();
        if (serverGroupId.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("禁言仅在群聊会话中可用"), 2000);
            return;
        }
        if (!canCurrentUserManageServerGroup(serverGroupId)) {
            ui->statusbar->showMessage(QStringLiteral("只有群主或管理员可以禁言成员"), 2400);
            return;
        }
        int durationMinutes = minutes;
        if (durationMinutes < 0) {
            // Custom duration: reuse the mute dialog.
            MuteDurationDialog dialog(this);
            if (dialog.exec() != QDialog::Accepted) {
                ui->statusbar->showMessage(QStringLiteral("已取消禁言操作"), 1600);
                return;
            }
            durationMinutes = dialog.isPermanent() ? 30 * 24 * 60 : dialog.durationMinutes();
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 mutedUntil = nowMs + static_cast<qint64>(durationMinutes) * 60 * 1000;
        if (m_client && m_client->sendServerGroupMemberMute(serverGroupId, userId, mutedUntil, QString())) {
            ui->statusbar->showMessage(QStringLiteral("已请求禁言 %1").arg(contactDisplayName(userId)), 2200);
        } else {
            ui->statusbar->showMessage(QStringLiteral("禁言失败：需要有效的服务器连接"), 3000);
        }
    });
    connect(sidebar, &GroupMemberSidebar::unmuteMember, this, [this, currentServerGroupId](const QString& userId) {
        if (userId.isEmpty()) return;
        const QString serverGroupId = currentServerGroupId();
        if (serverGroupId.isEmpty() || !canCurrentUserManageServerGroup(serverGroupId)) {
            ui->statusbar->showMessage(QStringLiteral("只有群主或管理员可以解除禁言"), 2400);
            return;
        }
        // Unmute maps to a mute window that ends now.
        if (m_client && m_client->sendServerGroupMemberMute(serverGroupId, userId,
                                                            QDateTime::currentMSecsSinceEpoch(), QString())) {
            ui->statusbar->showMessage(QStringLiteral("已请求解除禁言 %1").arg(contactDisplayName(userId)), 2200);
        } else {
            ui->statusbar->showMessage(QStringLiteral("解除禁言失败：需要有效的服务器连接"), 3000);
        }
    });
    connect(sidebar, &GroupMemberSidebar::promoteAdmin, this, [this, currentServerGroupId](const QString& userId) {
        if (userId.isEmpty() || userId == m_currentUserId) return;
        const QString serverGroupId = currentServerGroupId();
        if (serverGroupId.isEmpty() || !canCurrentUserManageServerGroup(serverGroupId)) {
            ui->statusbar->showMessage(QStringLiteral("只有群主可以设置管理员"), 2400);
            return;
        }
        requestServerGroupMemberUpdate(userId, QStringLiteral("promote_admin"), serverGroupId);
    });
    connect(sidebar, &GroupMemberSidebar::demoteAdmin, this, [this, currentServerGroupId](const QString& userId) {
        if (userId.isEmpty() || userId == m_currentUserId) return;
        const QString serverGroupId = currentServerGroupId();
        if (serverGroupId.isEmpty() || !canCurrentUserManageServerGroup(serverGroupId)) {
            ui->statusbar->showMessage(QStringLiteral("只有群主可以取消管理员"), 2400);
            return;
        }
        requestServerGroupMemberUpdate(userId, QStringLiteral("demote_admin"), serverGroupId);
    });
    connect(sidebar, &GroupMemberSidebar::kickMember, this, [this, isLocalGroup, currentServerGroupId](const QString& userId) {
        if (userId.isEmpty() || userId == m_currentUserId) return;
        const QString memberName = contactDisplayName(userId);
        if (isLocalGroup()) {
            if (!isCurrentUserGroupOwner(m_privateChatTarget)) {
                ui->statusbar->showMessage(QStringLiteral("只有群主可以移出成员"), 2400);
                return;
            }
            if (userId == groupOwnerId(m_privateChatTarget)) {
                ui->statusbar->showMessage(QStringLiteral("不能移出群主"), 2200);
                return;
            }
            if (QMessageBox::question(this, QStringLiteral("移出群成员"),
                                      QStringLiteral("确定将“%1”移出当前群聊吗？").arg(memberName),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
                return;
            }
            m_localGroupMembers[m_privateChatTarget].removeAll(userId);
            saveLocalGroups();
            refreshGroupMemberPanel();
            appendSystemMessage(QStringLiteral("已将 %1 移出群聊").arg(memberName));
        } else {
            const QString serverGroupId = currentServerGroupId();
            if (serverGroupId.isEmpty()) return;
            if (!canCurrentUserManageServerGroup(serverGroupId)) {
                ui->statusbar->showMessage(QStringLiteral("只有群主或管理员可以移出成员"), 2400);
                return;
            }
            if (QMessageBox::question(this, QStringLiteral("移出群成员"),
                                      QStringLiteral("确定将“%1”移出当前群聊吗？").arg(memberName),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
                return;
            }
            requestServerGroupMemberUpdate(userId, QStringLiteral("remove"), serverGroupId);
        }
    });
    connect(sidebar, &GroupMemberSidebar::reportMember, this, [this](const QString& userId) {
        ui->statusbar->showMessage(QStringLiteral("已记录对 QQ:%1 的举报（仅本地）").arg(userId), 2400);
    });
    connect(sidebar, &GroupMemberSidebar::blockMember, this, [this](const QString& userId) {
        ui->statusbar->showMessage(QStringLiteral("已屏蔽 QQ:%1 的发言（仅本地）").arg(userId), 2400);
    });
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




void MainWindow::setupQQNT()
{
    qqntLog("MainWindow", "setupQQNT start");
    m_qqntRoot = new QWidget(this);
    m_qqntRoot->setObjectName(QStringLiteral("qqntRoot"));
    m_qqntRoot->setAttribute(Qt::WA_StyledBackground, true);
    m_qqntRoot->setAutoFillBackground(true);

    QWidget* legacyCentralWidget = takeCentralWidget();
    if (legacyCentralWidget && legacyCentralWidget != m_qqntRoot) {
        // The generated Ui object still owns pointers into this compatibility
        // surface. Keep it attached to MainWindow instead of moving it under
        // the new central widget: reparenting it made Qt's generated Ui
        // ownership and QMainWindow central-widget destruction disagree.
        legacyCentralWidget->setParent(this);
        legacyCentralWidget->hide();
    }
    setCentralWidget(m_qqntRoot);
    m_qqntRoot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    qqntLog("MainWindow", "centralWidget replaced");

    // Hide legacy menu bar and status bar for QQNT style
    if (QMenuBar* mb = menuBar()) { mb->hide(); }
    if (QStatusBar* sb = statusBar()) { sb->hide(); }

    QVBoxLayout* rootLayout = new QVBoxLayout(m_qqntRoot);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->setSizeConstraint(QLayout::SetNoConstraint);

    m_titleBar = new TitleBar(m_qqntRoot);
    rootLayout->addWidget(m_titleBar);
    qqntLog("MainWindow", "titleBar created");

    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->setSizeConstraint(QLayout::SetNoConstraint);

    m_appNav = new AppNav(m_qqntRoot);
    contentLayout->addWidget(m_appNav);
    qqntLog("MainWindow", "appNav created");

    m_viewStack = new QStackedWidget(m_qqntRoot);
    m_viewStack->setObjectName(QStringLiteral("qqntViewStack"));
    m_viewStack->setAttribute(Qt::WA_StyledBackground, true);
    qqntLog("MainWindow", "viewStack created");

    m_messagesView = new MessagesView(m_qqntRoot);
    qqntLog("MainWindow", "messagesView created");
    m_messagesView->setSessionModel(m_sessionModel);
    m_messagesView->setChatModel(m_chatModel);
    qqntLog("MainWindow", "messagesView models set");
    m_contactsView = new ContactsView(m_qqntRoot);
    m_favoritesView = new FavoritesView(m_qqntRoot);
    m_settingsView = new SettingsView(m_qqntRoot);
    m_profileView = new ProfileView(m_qqntRoot);

    m_viewStack->addWidget(m_messagesView);
    m_viewStack->addWidget(m_contactsView);
    m_viewStack->addWidget(m_favoritesView);
    m_viewStack->addWidget(m_settingsView);
    m_viewStack->addWidget(m_profileView);

    contentLayout->addWidget(m_viewStack, 1);
    rootLayout->addLayout(contentLayout, 1);

    QTimer::singleShot(0, this, [this, legacyCentralWidget]() {
        qqntLog("MainWindow", QStringLiteral("geometry window=%1x%2 root=%3x%4 stack=%5x%6 legacy=%7x%8 visible=%9")
                .arg(width()).arg(height())
                .arg(m_qqntRoot ? m_qqntRoot->width() : -1).arg(m_qqntRoot ? m_qqntRoot->height() : -1)
                .arg(m_viewStack ? m_viewStack->width() : -1).arg(m_viewStack ? m_viewStack->height() : -1)
                .arg(legacyCentralWidget ? legacyCentralWidget->width() : -1).arg(legacyCentralWidget ? legacyCentralWidget->height() : -1)
                .arg(legacyCentralWidget && legacyCentralWidget->isVisible() ? QStringLiteral("true") : QStringLiteral("false")));
    });

    connect(m_titleBar, &TitleBar::minimizeRequested, this, &QMainWindow::showMinimized);
    connect(m_titleBar, &TitleBar::maximizeRequested, this, [this]() {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
    });
    connect(m_titleBar, &TitleBar::closeRequested, this, &QMainWindow::close);

    connect(m_appNav, &AppNav::routeActivated, this, &MainWindow::onAppNavRouteActivated);
    connect(m_messagesView, &MessagesView::sendRequested, this, &MainWindow::onSendMessage);
    connect(m_messagesView, &MessagesView::fileRequested, this, &MainWindow::onSendFile);
    connect(m_messagesView, &MessagesView::imageRequested, this, &MainWindow::onSendImage);
    connect(m_messagesView, &MessagesView::emojiRequested, this, &MainWindow::onInsertEmoji);
    connect(m_messagesView, &MessagesView::mentionRequested, this, &MainWindow::onInsertMention);
    connect(m_messagesView->composer(), &ComposerWidget::screenshotRequested, this, &MainWindow::onCaptureScreenshot);
    connect(m_messagesView->composer(), &ComposerWidget::filesDropped, this, &MainWindow::onComposerFilesDropped);
    connect(m_messagesView->composer(), &ComposerWidget::textChanged, this, &MainWindow::refreshComposerState);
    connect(m_messagesView, &MessagesView::viewHistoryRequested, this, &MainWindow::onViewHistory);
    connect(m_messagesView, &MessagesView::filterHistoryByDateRequested, this, &MainWindow::onFilterHistoryByDate);
    connect(m_messagesView, &MessagesView::exportHistoryRequested, this, &MainWindow::onExportHistory);
    connect(m_messagesView, &MessagesView::clearHistoryRequested, this, &MainWindow::onClearHistory);
    connect(m_messagesView, &MessagesView::sessionSelected, this, &MainWindow::onPrivateChat);
    connect(m_messagesView, &MessagesView::filesDropped, this, &MainWindow::onComposerFilesDropped);
    connect(m_messagesView, &MessagesView::messageActionRequested, this, &MainWindow::onMessageActionRequested);
    connect(m_messagesView, &MessagesView::avatarActionRequested, this, &MainWindow::onAvatarActionRequested);
    connect(m_messagesView, &MessagesView::mediaActivated, this, &MainWindow::onMediaActivated);
    connect(m_messagesView, &MessagesView::multiSelectForwardRequested, this, &MainWindow::onMultiSelectForwardRequested);
    connect(m_messagesView, &MessagesView::multiSelectDeleteRequested, this, &MainWindow::onMultiSelectDeleteRequested);
    connect(m_messagesView, &MessagesView::multiSelectFavoriteRequested, this, &MainWindow::onMultiSelectFavoriteRequested);
    connect(m_messagesView, &MessagesView::essenceRequested, this, &MainWindow::showEssencePanel);
    connect(m_messagesView, &MessagesView::groupMoreRequested, this, &MainWindow::showGroupInfoPanel);
    connectGroupMemberSidebar();
    connect(m_contactsView, &ContactsView::friendSelected, this, [this](const QString& userId) {
        openPrivateSession(userId);
        showMessagesView();
    });
    connect(m_contactsView, &ContactsView::groupSelected, this, [this](const QString& groupId) {
        switchToLocalGroup(groupId, m_localGroupNames.value(groupId, QStringLiteral("群聊")));
        showMessagesView();
    });
    connect(m_contactsView, &ContactsView::addFriendRequested, this, [this]() {
        showGlobalSearchDialog();
    });
    connect(m_contactsView, &ContactsView::createGroupRequested, this, [this]() {
        handleCreateMenuCommand(QStringLiteral("create-group"));
    });
    connect(m_contactsView, &ContactsView::friendManagerRequested, this, [this]() {
        FriendManagerDialog dlg(this);
        QMap<QString, QString> avatarPaths;
        for (const QString& id : m_friendIds) {
            const QString path = peerAvatarPath(id);
            if (!path.isEmpty()) avatarPaths.insert(id, path);
        }
        dlg.setFriendList(m_friendIds, m_friendNames, m_friendGroups, m_customGroups, avatarPaths);
        connect(&dlg, &FriendManagerDialog::deleteFriendRequested, this, [this](const QString& userId) {
            // TODO: no delete-friend command exists in Client/protocol yet; the dialog
            // only removes the row locally. Surface this so the state isn't misleading.
            ui->statusbar->showMessage(
                QStringLiteral("暂不支持删除好友（后端未实现该协议），仅本地移除 QQ:%1").arg(userId), 3000);
        });
        // Friend groups are local-only; persist changes so they survive restart.
        connect(&dlg, &FriendManagerDialog::createGroupRequested, this, [this](const QString& name) {
            if (!name.isEmpty() && !m_customGroups.contains(name)) {
                m_customGroups.append(name);
                saveFriendGroups();
            }
        });
        connect(&dlg, &FriendManagerDialog::renameGroupRequested, this,
                [this](const QString& oldName, const QString& newName) {
            const int index = m_customGroups.indexOf(oldName);
            if (index >= 0) m_customGroups[index] = newName;
            for (auto it = m_friendGroups.begin(); it != m_friendGroups.end(); ++it) {
                if (it.value() == oldName) it.value() = newName;
            }
            saveFriendGroups();
        });
        connect(&dlg, &FriendManagerDialog::deleteGroupRequested, this, [this](const QString& name) {
            m_customGroups.removeAll(name);
            for (auto it = m_friendGroups.begin(); it != m_friendGroups.end(); ) {
                if (it.value() == name) it = m_friendGroups.erase(it);
                else ++it;
            }
            saveFriendGroups();
        });
        connect(&dlg, &FriendManagerDialog::moveFriendToGroupRequested, this,
                [this](const QString& userId, const QString& group) {
            if (userId.isEmpty()) return;
            if (group.isEmpty() || group == QStringLiteral("我的好友")) {
                m_friendGroups.remove(userId);
            } else {
                m_friendGroups[userId] = group;
            }
            saveFriendGroups();
        });
        dlg.exec();
    });
    connect(m_contactsView, &ContactsView::globalSearchRequested, this, [this]() {
        showGlobalSearchDialog();
    });
    if (m_contactsView->searchEdit()) {
        connect(m_contactsView->searchEdit(), &QLineEdit::textChanged, this, [this]() {
            refreshContactsAndProfile();
        });
    }
    connect(m_favoritesView, &FavoritesView::favoriteSelected, this,
            [this](const QString& sessionId, const QString& messageId) {
        onFavoriteSelected(sessionId, messageId);
    });
    connect(m_favoritesView, &FavoritesView::favoriteRemovalRequested, this, [this](const QJsonObject& message) {
        if (message.isEmpty()) return;
        QJsonObject payload;
        payload[QStringLiteral("message")] = message;
        payload[QStringLiteral("favorite")] = false;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        if (!QQNTBackendService::handle(QStringLiteral("toggle_local_message_favorite"),
                                        payload, &response, &errorCode, &errorMessage)) {
            ui->statusbar->showMessage(QStringLiteral("取消收藏失败: %1").arg(errorMessage), 3000);
            return;
        }
        if (m_client && m_client->isConnected()) {
            m_client->sendMessageFavoriteUpdate(message.value(QStringLiteral("sessionId")).toString(),
                                                message.value(QStringLiteral("messageId")).toString(message.value(QStringLiteral("id")).toString()),
                                                false,
                                                message);
        }
        refreshFavoritesView();
        ui->statusbar->showMessage(QStringLiteral("已取消收藏"), 1800);
    });
    connect(m_settingsView, &SettingsView::themeModeChanged, this, &MainWindow::onSettingsThemeModeChanged);
    connect(m_settingsView, &SettingsView::notificationsToggled, this, &MainWindow::onSettingsNotificationsToggled);
    connect(m_settingsView, &SettingsView::soundToggled, this, &MainWindow::onSettingsSoundToggled);
    connect(m_settingsView, &SettingsView::desktopNotificationsToggled, this, &MainWindow::onSettingsDesktopNotificationsToggled);
    connect(m_settingsView, &SettingsView::muteInSessionToggled, this, &MainWindow::onSettingsMuteInSessionToggled);
    connect(m_settingsView, &SettingsView::e2eEnabledToggled, this, &MainWindow::onSettingsE2EEnabledToggled);
    connect(m_settingsView, &SettingsView::autoAcceptFilesToggled, this, &MainWindow::onSettingsAutoAcceptFilesToggled);
    connect(m_settingsView, &SettingsView::openFolderAfterDownloadToggled, this, &MainWindow::onSettingsOpenFolderAfterDownloadToggled);
    connect(m_settingsView, &SettingsView::hideWindowBeforeScreenshotToggled, this, &MainWindow::onSettingsHideWindowBeforeScreenshotToggled);
    connect(m_settingsView, &SettingsView::downloadPathChangeRequested, this, &MainWindow::onSettingsDownloadPathChangeRequested);
    connect(m_settingsView, &SettingsView::screenshotShortcutChangeRequested, this, &MainWindow::onSettingsScreenshotShortcutChangeRequested);
    connect(m_settingsView, &SettingsView::logoutRequested, this, &MainWindow::onLogout);
    connect(m_profileView, &ProfileView::logoutRequested, this, &MainWindow::onLogout);
    connect(m_profileView, &ProfileView::editProfileRequested, this, [this]() {
        bool ok = false;
        const QString newName = QInputDialog::getText(this,
                                                      QStringLiteral("修改昵称"),
                                                      QStringLiteral("请输入新的昵称:"),
                                                      QLineEdit::Normal,
                                                      m_currentUserName,
                                                      &ok).trimmed();
        if (!ok || newName.isEmpty() || newName == m_currentUserName) {
            return;
        }
        m_currentUserName = newName;
        if (m_client) {
            m_client->setUserInfo(m_currentUserId, newName);
        }
        saveProfileToSqlite();
        setWindowTitle(appWindowTitle(m_currentUserName));
        if (m_titleBar) {
            m_titleBar->setUserName(m_currentUserName);
        }
        m_profileView->setUserInfo(m_currentUserId, m_currentUserName);
        if (m_settingsView) {
            m_settingsView->setAccountInfo(m_currentUserName, m_currentUserId);
        }
        refreshFriendList();
        ui->statusbar->showMessage(QStringLiteral("昵称已更新为 %1").arg(newName), 2200);
    });
    connect(m_profileView, &ProfileView::changeAvatarRequested, this, [this]() {
        onUploadAvatar();
    });

    m_titleBar->setUserName(m_currentUserName);
    m_titleBar->setUserId(m_currentUserId);
    m_profileView->setUserInfo(m_currentUserId, m_currentUserName);

    // Restore persisted settings state into the SettingsView so its controls
    // reflect the values applied elsewhere (theme, notifications, files, screenshot).
    {
        QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
        const int themeMode = settings.value(QStringLiteral("appearance/themeMode"),
                                              ThemeManager::instance()->isDark() ? 1 : 0).toInt();
        m_settingsView->setThemeMode(themeMode);
        m_settingsView->setAccountInfo(m_currentUserName, m_currentUserId);
        m_settingsView->setSyncStatus(QStringLiteral("已保存到本地"));
    }

    m_appNav->setCurrentIndex(0);
    m_viewStack->setCurrentIndex(0);

    m_messagesView->setChatTitle(ui->chatTitleLabel->text(),
                                 ui->chatSubtitleLabel->text(),
                                 ui->chatHintLabel->text());

    updateStyleSheet();

    // Startup assertions: the QQNT shell must be fully constructed. These guard
    // against regressions where a core surface fails to initialize.
    Q_ASSERT(m_qqntRoot);
    Q_ASSERT(m_messagesView);
    Q_ASSERT(m_viewStack);
    Q_ASSERT(m_appNav);
    if (!m_qqntRoot || !m_messagesView || !m_viewStack || !m_appNav) {
        qqntLog("MainWindow", "FATAL: QQNT shell missing a core widget after setup");
    }
    // Legacy central widget must be hidden so only the QQNT UI is visible.
    if (QWidget* legacy = centralWidget(); legacy && legacy != m_qqntRoot) {
        qqntLog("MainWindow", QStringLiteral("WARN: unexpected central widget %1")
                .arg(legacy->objectName()));
    }
    qqntLog("MainWindow", QStringLiteral("setup complete · route=%1 session=%2 sessionRows=%3 chatRows=%4")
            .arg(QString::number(m_viewStack->currentIndex()),
                 m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget,
                 QString::number(m_sessionModel ? m_sessionModel->rowCount() : -1),
                 QString::number(m_chatModel ? m_chatModel->rowCount() : -1)));
}

void MainWindow::updateStyleSheet()
{
    if (!m_qqntRoot) {
        return;
    }

    ThemeManager* tm = ThemeManager::instance();
    QString style = QStringLiteral(
        "QWidget#qqntRoot { background-color: %1; border: none; }"
        "QStackedWidget#qqntViewStack { background-color: %1; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 6px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: %2; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->borderColor().name());

    m_qqntRoot->setStyleSheet(style);

    QPalette windowPalette = palette();
    windowPalette.setColor(QPalette::Window, tm->backgroundColor());
    setPalette(windowPalette);
    setAutoFillBackground(true);
    m_qqntRoot->setPalette(windowPalette);
    m_qqntRoot->update();
}

void MainWindow::loadStyleSheet()
{
    ThemeManager* tm = ThemeManager::instance();
    const QString styleName = tm->isDark()
        ? QStringLiteral("style-qqnt-dark.qss")
        : QStringLiteral("style-qqnt.qss");
    const QString fileName = QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("ui/") + styleName);
    QFile styleFile(fileName);
    if (styleFile.open(QFile::ReadOnly)) {
        QTextStream textStream(&styleFile);
        setStyleSheet(textStream.readAll());
        styleFile.close();
        qqntLog("MainWindow", QStringLiteral("stylesheet loaded: %1").arg(fileName));
    } else {
        qqntLog("MainWindow", QStringLiteral("stylesheet load failed: %1").arg(fileName));
    }
}

void MainWindow::onThemeToggled()
{
    ThemeManager::instance()->toggleTheme();
    loadStyleSheet();
    updateStyleSheet();
    if (m_settingsView) {
        m_settingsView->setThemeMode(ThemeManager::instance()->isDark() ? 1 : 0);
    }
}

void MainWindow::onSettingsThemeModeChanged(int mode)
{
    // 0=light, 1=dark, 2=system. Persist choice and apply light/dark now.
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("appearance/themeMode"), mode);

    ThemeManager::Theme target = ThemeManager::Theme::Light;
    if (mode == 1) {
        target = ThemeManager::Theme::Dark;
    } else if (mode == 2) {
        // Read the OS color scheme. The application palette may already have
        // been overridden by ThemeManager, so it is not a reliable system signal.
        target = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
            ? ThemeManager::Theme::Dark
            : ThemeManager::Theme::Light;
    }
    ThemeManager::instance()->setTheme(target);
    loadStyleSheet();
    updateStyleSheet();
    if (m_settingsView) {
        m_settingsView->setSyncStatus(QStringLiteral("已保存到本地"));
    }
}

void MainWindow::onSettingsNotificationsToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("notifications/enabled"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("消息通知已开启") : QStringLiteral("消息通知已关闭"));
    }
    ui->statusbar->showMessage(enabled ? QStringLiteral("已开启消息通知") : QStringLiteral("已关闭消息通知"), 1800);
}

void MainWindow::onSettingsSoundToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("notifications/sound"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("提示音已开启") : QStringLiteral("提示音已关闭"));
    }
}

void MainWindow::onSettingsDesktopNotificationsToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("notifications/desktop"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("桌面通知已开启") : QStringLiteral("桌面通知已关闭"));
    }
}

void MainWindow::onSettingsMuteInSessionToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("notifications/muteInSession"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("会话内已免打扰") : QStringLiteral("会话内通知已恢复"));
    }
}

void MainWindow::onSettingsE2EEnabledToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("security/e2eEnabled"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("端到端加密已开启") : QStringLiteral("端到端加密已关闭"));
    }
    ui->statusbar->showMessage(enabled ? QStringLiteral("端到端加密已开启（重连后生效）")
                                       : QStringLiteral("端到端加密已关闭（重连后生效）"), 2200);
}

void MainWindow::onSettingsAutoAcceptFilesToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("files/autoAccept"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("已开启自动接收文件") : QStringLiteral("已关闭自动接收文件"));
    }
}

void MainWindow::onSettingsOpenFolderAfterDownloadToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("files/openFolderAfterDownload"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("下载后将自动打开文件夹") : QStringLiteral("下载后不再打开文件夹"));
    }
}

void MainWindow::onSettingsHideWindowBeforeScreenshotToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("screenshot/hideWindow"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("截图时将隐藏当前窗口") : QStringLiteral("截图时保留当前窗口"));
    }
}

void MainWindow::onSettingsDownloadPathChangeRequested()
{
    const QString current = LocalFileManager::receivedDownloadRootDirectory();
    const QString picked = QFileDialog::getExistingDirectory(this,
                                                             QStringLiteral("选择默认下载目录"),
                                                             current);
    if (picked.trimmed().isEmpty()) {
        return;
    }
    LocalFileManager::setReceivedDownloadRootDirectory(picked);
    const QString saved = LocalFileManager::receivedDownloadRootDirectory();
    if (m_settingsView) {
        m_settingsView->setSyncStatus(QStringLiteral("下载目录已更新"));
    }
    ui->statusbar->showMessage(QStringLiteral("默认下载目录：%1").arg(saved), 2600);
}

void MainWindow::onSettingsScreenshotShortcutChangeRequested()
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    const QString current = settings.value(QStringLiteral("screenshot/shortcut"),
                                            QStringLiteral("Ctrl+Alt+A")).toString();
    bool ok = false;
    const QString entered = QInputDialog::getText(this,
                                                  QStringLiteral("修改截图快捷键"),
                                                  QStringLiteral("请输入快捷键（例如 Ctrl+Alt+A）："),
                                                  QLineEdit::Normal,
                                                  current,
                                                  &ok);
    if (!ok) {
        return;
    }
    const QKeySequence seq(entered.trimmed());
    if (seq.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无效快捷键"),
                             QStringLiteral("无法识别输入的快捷键，请重试。"));
        return;
    }
    const QString normalized = seq.toString(QKeySequence::NativeText);
    settings.setValue(QStringLiteral("screenshot/shortcut"), normalized);
    ui->statusbar->showMessage(QStringLiteral("截图快捷键已更新为 %1（重启后生效）").arg(normalized), 2600);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(QStringLiteral("截图快捷键已更新"));
    }
}

void MainWindow::showMessagesView()
{
    // Bring the messages view forward and keep the side nav highlight in sync.
    // Used when a session is opened from another view (e.g. the contacts card).
    if (m_viewStack) {
        m_viewStack->setCurrentIndex(0);
    }
    if (m_appNav) {
        m_appNav->setCurrentIndex(0);
    }
}

void MainWindow::showAddFriendDialog()
{
    // QQNT add-friend flow: search a QQ/nickname, review the result card, then
    // confirm. Search results arrive asynchronously via onFriendSearchResult,
    // which routes to this dialog while m_activeAddFriendDialog is set.
    AddFriendDialog dialog(this);
    m_activeAddFriendDialog = &dialog;

    connect(&dialog, &AddFriendDialog::searchRequested, this, [this](const QString& text) {
        const QString account = text.trimmed();
        if (account.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("请输入 QQ 号或昵称后再搜索"), 1800);
            return;
        }
        if (!m_client->searchFriendByAccount(account)) {
            ui->statusbar->showMessage(QStringLiteral("当前未连接，无法搜索账号"), 2500);
            if (m_activeAddFriendDialog) {
                m_activeAddFriendDialog->setRequestOutcome(false, QStringLiteral("当前未连接，请恢复连接后重试"));
            }
        }
    });
    connect(&dialog, &AddFriendDialog::addFriendRequested, this, [this, &dialog](const QString& userId) {
        if (userId.isEmpty() || userId == m_currentUserId) {
            dialog.setRequestOutcome(false, QStringLiteral("不能添加自己为好友"));
            return;
        }
        if (m_friendIds.contains(userId)) {
            ui->statusbar->showMessage(QStringLiteral("QQ 账号 %1 已经是你的好友").arg(userId), 2500);
            dialog.setRequestOutcome(false, QStringLiteral("该用户已经是你的好友"));
            return;
        }
        if (m_pendingOutgoingFriendRequests.contains(userId)) {
            dialog.setRequestOutcome(true, QStringLiteral("好友申请已发送，等待对方确认"));
            return;
        }
        if (!m_client->sendFriendRequest(userId)) {
            ui->statusbar->showMessage(QStringLiteral("好友申请发送失败，请检查连接后重试"), 3000);
            dialog.setRequestOutcome(false, QStringLiteral("好友申请发送失败，请检查连接后重试"));
            return;
        }
        if (!m_pendingOutgoingFriendRequests.contains(userId)) {
            m_pendingOutgoingFriendRequests << userId;
        }
        saveFriends();
        appendSystemMessage(QStringLiteral("已发送好友申请 QQ:%1，等待对方同意").arg(userId));
        ui->statusbar->showMessage(
            QStringLiteral("好友申请已发送给 %1").arg(contactDisplayName(userId)), 2500);
        dialog.setRequestOutcome(true);
        refreshFriendList();
    });

    dialog.exec();
    m_activeAddFriendDialog = nullptr;
}

void MainWindow::showGroupInfoPanel()
{
    const bool publicGroup = m_privateChatTarget.isEmpty();
    const QString serverGroupId = publicGroup ? QStringLiteral("public")
        : m_joinedServerSearchGroups.key(m_privateChatTarget);
    const bool serverGroup = !serverGroupId.isEmpty();
    // Joined searchable server groups deliberately use a local session id.  The
    // server mapping must win here, otherwise a server group is treated as a
    // local one and its owner/admin permissions disappear from the panel.
    const bool localGroup = !serverGroup
        && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    if (!localGroup && !publicGroup && !serverGroup) {
        ui->statusbar->showMessage(QStringLiteral("请先进入群聊"), 1800);
        return;
    }

    const QString groupId = serverGroup ? serverGroupId : m_privateChatTarget;
    const QString groupName = localGroup ? m_localGroupNames.value(groupId, QStringLiteral("群聊"))
        : m_serverGroupNames.value(groupId, QStringLiteral("公共聊天室"));
    const QString announcement = localGroup ? m_localGroupAnnouncements.value(groupId)
        : m_serverGroupAnnouncements.value(groupId);
    const QStringList members = localGroup ? m_localGroupMembers.value(groupId)
        : m_serverGroupMembers.value(groupId);
    const bool manager = localGroup ? isCurrentUserGroupOwner(groupId) : canCurrentUserManageServerGroup(groupId);
    const bool owner = localGroup ? isCurrentUserGroupOwner(groupId)
        : (m_serverGroupOwners.value(groupId) == m_currentUserId);
    const QJsonObject serverSettings = m_serverGroupSettings.value(groupId);
    const QJsonObject userSettings = m_serverGroupUserSettings.value(groupId);

    QDialog panel(this);
    panel.setObjectName(QStringLiteral("groupInfoDialog"));
    panel.setWindowTitle(QStringLiteral("群聊资料"));
    panel.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    panel.setFixedSize(384, qMin(736, qMax(592, height() - 28)));
    auto* root = new QVBoxLayout(&panel);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* titleBar = new DialogTitleBar(&panel, QStringLiteral("群聊资料"));
    connect(titleBar, &DialogTitleBar::closeRequested, &panel, &QDialog::reject);
    root->addWidget(titleBar);

    auto* scroll = new QScrollArea(&panel);
    scroll->setObjectName(QStringLiteral("groupInfoScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("groupInfoContent"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(16, 12, 16, 22);
    contentLayout->setSpacing(9);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto stableSettingKey = [groupId, serverGroup]() {
        return QStringLiteral("groupInfo/%1/%2/")
            .arg(serverGroup ? QStringLiteral("server") : QStringLiteral("local"), groupId);
    };
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));

    auto promptText = [&panel](const QString& title, const QString& label,
                               const QString& initialValue, bool multiline = false) -> QPair<bool, QString> {
        QDialog dialog(&panel);
        dialog.setObjectName(QStringLiteral("groupInlineEditor"));
        dialog.setWindowTitle(title);
        dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        dialog.setFixedSize(344, multiline ? 268 : 190);
        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(0, 0, 0, 16);
        layout->setSpacing(0);
        auto* bar = new DialogTitleBar(&dialog, title);
        connect(bar, &DialogTitleBar::closeRequested, &dialog, &QDialog::reject);
        layout->addWidget(bar);
        auto* content = new QWidget(&dialog);
        auto* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(18, 14, 18, 0);
        contentLayout->setSpacing(8);
        auto* prompt = new QLabel(label, content);
        prompt->setObjectName(QStringLiteral("groupInlineEditorHint"));
        contentLayout->addWidget(prompt);
        QLineEdit* lineEdit = nullptr;
        QPlainTextEdit* textEdit = nullptr;
        if (multiline) {
            textEdit = new QPlainTextEdit(initialValue, content);
            textEdit->setObjectName(QStringLiteral("groupInlineEditorText"));
            textEdit->setPlaceholderText(label);
            textEdit->setFixedHeight(108);
            contentLayout->addWidget(textEdit);
        } else {
            lineEdit = new QLineEdit(initialValue, content);
            lineEdit->setObjectName(QStringLiteral("groupInlineEditorInput"));
            lineEdit->setPlaceholderText(label);
            lineEdit->setClearButtonEnabled(true);
            contentLayout->addWidget(lineEdit);
        }
        auto* actions = new QHBoxLayout();
        actions->addStretch();
        auto* cancel = new QPushButton(QStringLiteral("取消"), content);
        cancel->setObjectName(QStringLiteral("dialogSecondaryBtn"));
        auto* save = new QPushButton(QStringLiteral("保存"), content);
        save->setObjectName(QStringLiteral("dialogPrimaryBtn"));
        actions->addWidget(cancel);
        actions->addWidget(save);
        contentLayout->addLayout(actions);
        layout->addWidget(content, 1);
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
        connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.setStyleSheet(DialogStyle::common() + QStringLiteral(
            "QDialog#groupInlineEditor { background:%1; border:1px solid %2; }"
            "QLabel#groupInlineEditorHint { color:%3; font-size:12px; }"
            "QLineEdit#groupInlineEditorInput,QPlainTextEdit#groupInlineEditorText { background:%4; color:%5; border:1px solid %2; border-radius:6px; padding:8px 10px; }"
            "QLineEdit#groupInlineEditorInput:focus,QPlainTextEdit#groupInlineEditorText:focus { border-color:%6; }")
            .arg(ThemeManager::instance()->backgroundColor().name(), ThemeManager::instance()->borderColor().name(),
                 ThemeManager::instance()->textSecondaryColor().name(), ThemeManager::instance()->backgroundSecondaryColor().name(),
                 ThemeManager::instance()->textColor().name(), ThemeManager::instance()->primaryColor().name()));
        if (lineEdit) lineEdit->setFocus(); else textEdit->setFocus();
        if (dialog.exec() != QDialog::Accepted) return qMakePair(false, QString());
        return qMakePair(true, multiline ? textEdit->toPlainText().trimmed() : lineEdit->text().trimmed());
    };

    auto confirmDanger = [&panel](const QString& title, const QString& detail, const QString& actionText) -> bool {
        QDialog dialog(&panel);
        dialog.setObjectName(QStringLiteral("groupDangerConfirm"));
        dialog.setWindowTitle(title);
        dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        dialog.setFixedSize(344, 196);
        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(0, 0, 0, 16);
        layout->setSpacing(0);
        auto* bar = new DialogTitleBar(&dialog, title);
        connect(bar, &DialogTitleBar::closeRequested, &dialog, &QDialog::reject);
        layout->addWidget(bar);
        auto* content = new QWidget(&dialog);
        auto* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(18, 16, 18, 0);
        auto* text = new QLabel(detail, content);
        text->setObjectName(QStringLiteral("groupDangerConfirmDetail"));
        text->setWordWrap(true);
        contentLayout->addWidget(text, 1);
        auto* actions = new QHBoxLayout();
        actions->addStretch();
        auto* cancel = new QPushButton(QStringLiteral("取消"), content);
        cancel->setObjectName(QStringLiteral("dialogSecondaryBtn"));
        auto* accept = new QPushButton(actionText, content);
        accept->setObjectName(QStringLiteral("dialogDangerBtn"));
        actions->addWidget(cancel);
        actions->addWidget(accept);
        contentLayout->addLayout(actions);
        layout->addWidget(content, 1);
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
        connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.setStyleSheet(DialogStyle::common() + QStringLiteral(
            "QDialog#groupDangerConfirm { background:%1; border:1px solid %2; }"
            "QLabel#groupDangerConfirmDetail { color:%3; font-size:13px; line-height:1.45; }")
            .arg(ThemeManager::instance()->backgroundColor().name(), ThemeManager::instance()->borderColor().name(),
                 ThemeManager::instance()->textSecondaryColor().name()));
        return dialog.exec() == QDialog::Accepted;
    };

    auto chooseSetting = [&panel](const QString& title,
                                  const QString& section,
                                  const QList<QPair<QString, QString>>& choices,
                                  const QString& currentKey) -> QString {
        QDialog chooser(&panel);
        chooser.setObjectName(QStringLiteral("groupSettingChooser"));
        chooser.setWindowTitle(title);
        chooser.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        chooser.setFixedSize(336, qBound(230, 122 + choices.size() * 52, 440));
        auto* layout = new QVBoxLayout(&chooser);
        layout->setContentsMargins(0, 0, 0, 14);
        layout->setSpacing(0);
        auto* titleBar = new DialogTitleBar(&chooser, title);
        connect(titleBar, &DialogTitleBar::closeRequested, &chooser, &QDialog::reject);
        layout->addWidget(titleBar);
        auto* sectionLabel = new QLabel(section, &chooser);
        sectionLabel->setObjectName(QStringLiteral("groupSettingChooserCaption"));
        sectionLabel->setContentsMargins(16, 12, 16, 6);
        layout->addWidget(sectionLabel);
        auto* optionCard = new QFrame(&chooser);
        optionCard->setObjectName(QStringLiteral("groupSettingChooserCard"));
        auto* optionLayout = new QVBoxLayout(optionCard);
        optionLayout->setContentsMargins(0, 0, 0, 0);
        optionLayout->setSpacing(0);
        for (int index = 0; index < choices.size(); ++index) {
            const auto& choice = choices.at(index);
            auto* row = new QPushButton(optionCard);
            row->setObjectName(QStringLiteral("groupSettingChoice"));
            row->setCursor(Qt::PointingHandCursor);
            row->setProperty("settingValue", choice.second);
            row->setProperty("selected", choice.second == currentKey);
            row->setFixedHeight(52);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 14, 0);
            auto* selectionBar = new QFrame(row);
            selectionBar->setObjectName(QStringLiteral("groupSettingChoiceBar"));
            selectionBar->setFixedSize(3, 26);
            selectionBar->setVisible(choice.second == currentKey);
            rowLayout->addWidget(selectionBar);
            auto* label = new QLabel(choice.first, row);
            label->setObjectName(QStringLiteral("groupSettingChoiceLabel"));
            label->setContentsMargins(11, 0, 0, 0);
            rowLayout->addWidget(label);
            rowLayout->addStretch();
            auto* checked = new QLabel(QStringLiteral("✓"), row);
            checked->setObjectName(QStringLiteral("groupSettingChoiceCheck"));
            checked->setAlignment(Qt::AlignCenter);
            checked->setFixedSize(20, 20);
            checked->setVisible(choice.second == currentKey);
            rowLayout->addWidget(checked);
            connect(row, &QPushButton::clicked, &chooser, [&chooser, row]() {
                chooser.setProperty("selectedValue", row->property("settingValue"));
                chooser.accept();
            });
            optionLayout->addWidget(row);
        }
        layout->addWidget(optionCard);
        layout->addStretch();
        chooser.setStyleSheet(DialogStyle::common() + QStringLiteral(
            "QDialog#groupSettingChooser { background:%1; }"
            "QLabel#groupSettingChooserCaption { color:%5; font-size:12px; }"
            "QFrame#groupSettingChooserCard { background:%2; border:1px solid %3; border-radius:7px; }"
            "QPushButton#groupSettingChoice { color:%4; background:%2; border:none; border-bottom:1px solid %3; text-align:left; }"
            "QPushButton#groupSettingChoice:last-child { border-bottom:none; }"
            "QPushButton#groupSettingChoice:hover { background:%8; }"
            "QPushButton#groupSettingChoice:pressed { background:%3; }"
            "QPushButton#groupSettingChoice[selected=\"true\"] { background:%2; }"
            "QLabel#groupSettingChoiceLabel { color:%4; font-size:13px; }"
            "QPushButton#groupSettingChoice[selected=\"true\"] QLabel#groupSettingChoiceLabel { color:%7; font-weight:500; }"
            "QFrame#groupSettingChoiceBar { background:%7; border-radius:1px; }"
            "QLabel#groupSettingChoiceCheck { color:%2; background:%7; border-radius:10px; font-size:13px; font-weight:600; }")
            .arg(ThemeManager::instance()->backgroundColor().name(),
                 ThemeManager::instance()->backgroundSecondaryColor().name(),
                 ThemeManager::instance()->borderColor().name(),
                 ThemeManager::instance()->textColor().name(),
                 ThemeManager::instance()->textSecondaryColor().name(),
                 ThemeManager::instance()->borderColor().name(),
                 ThemeManager::instance()->primaryColor().name(),
                 ThemeManager::instance()->backgroundTertiaryColor().name()));
        if (chooser.exec() != QDialog::Accepted) return QString();
        return chooser.property("selectedValue").toString();
    };

    QVBoxLayout* activeSettingsLayout = nullptr;
    auto addCaption = [&panel, contentLayout, &activeSettingsLayout](const QString& text) {
        auto* caption = new QLabel(text, &panel);
        caption->setObjectName(QStringLiteral("groupInfoCaption"));
        contentLayout->addWidget(caption);
        auto* card = new QFrame(&panel);
        card->setObjectName(QStringLiteral("groupInfoSettingCard"));
        activeSettingsLayout = new QVBoxLayout(card);
        activeSettingsLayout->setContentsMargins(0, 0, 0, 0);
        activeSettingsLayout->setSpacing(0);
        contentLayout->addWidget(card);
    };
    auto addRow = [&panel, contentLayout, &activeSettingsLayout](const QString& title, const QString& value = QString(), bool clickable = false) {
        auto* row = new QPushButton(&panel);
        row->setObjectName(clickable ? QStringLiteral("groupInfoActionRow") : QStringLiteral("groupInfoRow"));
        row->setFlat(true);
        row->setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        row->setFixedHeight(48);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(15, 0, 13, 0);
        auto* label = new QLabel(title, row);
        label->setObjectName(QStringLiteral("groupInfoRowTitle"));
        layout->addWidget(label);
        layout->addStretch();
        if (!value.isEmpty()) {
            auto* detail = new QLabel(value, row);
            detail->setObjectName(QStringLiteral("groupInfoRowValue"));
            layout->addWidget(detail);
        }
        if (clickable) {
            auto* arrow = new QLabel(QStringLiteral("›"), row);
            arrow->setObjectName(QStringLiteral("groupInfoArrow"));
            layout->addWidget(arrow);
        }
        if (activeSettingsLayout) activeSettingsLayout->addWidget(row);
        else contentLayout->addWidget(row);
        return row;
    };

    auto* overview = new QFrame(&panel);
    overview->setObjectName(QStringLiteral("groupInfoOverview"));
    auto* overviewLayout = new QHBoxLayout(overview);
    overviewLayout->setContentsMargins(15, 15, 12, 15);
    overviewLayout->setSpacing(12);
    auto* avatarButton = new QToolButton(overview);
    avatarButton->setObjectName(QStringLiteral("groupInfoAvatarButton"));
    avatarButton->setFixedSize(56, 56);
    avatarButton->setCursor(manager ? Qt::PointingHandCursor : Qt::ArrowCursor);
    avatarButton->setToolTip(manager ? QStringLiteral("点击从本地选择群头像")
                                     : QStringLiteral("群头像"));
    avatarButton->setEnabled(manager);
    auto* groupAvatar = new AvatarLabel(avatarButton, 56);
    groupAvatar->setAttribute(Qt::WA_TransparentForMouseEvents);
    const QString avatarPath = localGroup ? m_localGroupAvatarPaths.value(groupId) : QString();
    QPixmap serverAvatar;
    if (serverGroup) serverAvatar.loadFromData(QByteArray::fromBase64(serverSettings.value(QStringLiteral("avatar")).toString().toUtf8()));
    if (!avatarPath.isEmpty() && QFileInfo::exists(avatarPath)) {
        groupAvatar->setPixmap(QPixmap(avatarPath));
    } else if (!serverAvatar.isNull()) {
        groupAvatar->setPixmap(serverAvatar);
    } else {
        groupAvatar->setTextAvatar(groupName, ThemeManager::instance()->primaryColor());
    }
    overviewLayout->addWidget(avatarButton);
    auto* overviewText = new QVBoxLayout();
    overviewText->setSpacing(3);
    auto* name = new QLabel(groupName, overview);
    name->setObjectName(QStringLiteral("groupInfoName"));
    name->setWordWrap(true);
    const QString visibleGroupNumber = serverGroup ? groupId
        : groupId.mid(QStringLiteral("local_group_").size());
    auto* meta = new QLabel(QStringLiteral("群号 %1  ·  %2 位成员")
                                .arg(visibleGroupNumber)
                                .arg(members.size()), overview);
    meta->setObjectName(QStringLiteral("groupInfoMeta"));
    overviewText->addWidget(name);
    overviewText->addWidget(meta);
    overviewLayout->addLayout(overviewText, 1);
    auto* share = new QToolButton(overview);
    share->setObjectName(QStringLiteral("groupInfoShare"));
    share->setText(QStringLiteral("↗"));
    share->setToolTip(QStringLiteral("复制群号"));
    share->setAccessibleName(QStringLiteral("复制群号"));
    share->setFixedSize(30, 30);
    overviewLayout->addWidget(share);
    connect(share, &QToolButton::clicked, this, [groupId, this]() {
        QApplication::clipboard()->setText(groupId);
        ui->statusbar->showMessage(QStringLiteral("群号已复制"), 1600);
    });
    if (manager) {
        connect(avatarButton, &QToolButton::clicked, this,
                [this, serverGroup, groupId, groupName, groupAvatar, avatarButton]() {
            const QString picturesDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
            const QString sourcePath = QFileDialog::getOpenFileName(
                this, QStringLiteral("选择本地群头像"), picturesDirectory,
                QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
            if (sourcePath.isEmpty()) return;

            QImageReader reader(sourcePath);
            reader.setAutoTransform(true);
            const QImage original = reader.read();
            if (original.isNull()) {
                ui->statusbar->showMessage(QStringLiteral("图片读取失败，请选择有效的图片文件"), 2200);
                return;
            }
            QImage normalized = original.scaled(256, 256, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            const int left = qMax(0, (normalized.width() - 256) / 2);
            const int top = qMax(0, (normalized.height() - 256) / 2);
            normalized = normalized.copy(left, top, qMin(256, normalized.width()), qMin(256, normalized.height()));
            QByteArray encoded;
            QBuffer buffer(&encoded);
            if (!buffer.open(QIODevice::WriteOnly) || !normalized.save(&buffer, "PNG") || encoded.size() > 768 * 1024) {
                ui->statusbar->showMessage(QStringLiteral("群头像处理失败，请选择更简单的图片"), 2400);
                return;
            }

            if (serverGroup) {
                if (!m_client || !m_client->sendServerGroupSettingsUpdate(
                        groupId, QJsonObject{{QStringLiteral("avatar"), QString::fromUtf8(encoded.toBase64())}})) {
                    ui->statusbar->showMessage(QStringLiteral("群头像提交失败，请检查连接"), 2600);
                    return;
                }
                m_serverGroupSettings[groupId][QStringLiteral("avatar")] = QString::fromUtf8(encoded.toBase64());
                ui->statusbar->showMessage(QStringLiteral("群头像已提交，正在同步"), 1800);
            } else {
                const QString avatarDirectory = QDir(ClientStorage::appDataRootDirectory())
                    .filePath(QStringLiteral("group_avatars"));
                QDir().mkpath(avatarDirectory);
                const QString avatarPath = QDir(avatarDirectory).filePath(groupId + QStringLiteral(".png"));
                if (!normalized.save(avatarPath, "PNG")) {
                    ui->statusbar->showMessage(QStringLiteral("群头像保存失败"), 2200);
                    return;
                }
                m_localGroupAvatarPaths[groupId] = avatarPath;
                saveLocalGroups();
                refreshFriendList();
                ui->statusbar->showMessage(QStringLiteral("群头像已更新"), 1800);
            }
            groupAvatar->setPixmap(QPixmap::fromImage(normalized));
            avatarButton->setToolTip(QStringLiteral("点击从本地选择群头像"));
        });
    }
    contentLayout->addWidget(overview);

    auto* announcementCard = new QFrame(&panel);
    announcementCard->setObjectName(QStringLiteral("groupInfoAnnouncementCard"));
    auto* announcementLayout = new QVBoxLayout(announcementCard);
    announcementLayout->setContentsMargins(14, 12, 14, 12);
    announcementLayout->setSpacing(6);
    auto* announcementHeader = new QHBoxLayout();
    auto* announcementTitle = new QLabel(QStringLiteral("群公告"), announcementCard);
    announcementTitle->setObjectName(QStringLiteral("groupInfoAnnouncementTitle"));
    announcementHeader->addWidget(announcementTitle);
    announcementHeader->addStretch();
    if (manager) {
        auto* editAnnouncement = new QPushButton(QStringLiteral("编辑"), announcementCard);
        editAnnouncement->setObjectName(QStringLiteral("groupInfoLinkButton"));
        editAnnouncement->setToolTip(QStringLiteral("编辑群公告"));
        announcementHeader->addWidget(editAnnouncement);
        connect(editAnnouncement, &QPushButton::clicked, this, [this, &panel]() {
            onEditGroupAnnouncement();
            panel.accept();
        });
    }
    announcementLayout->addLayout(announcementHeader);
    auto* announcementBody = new QLabel(announcement.isEmpty() ? QStringLiteral("暂无群公告") : announcement, announcementCard);
    announcementBody->setObjectName(QStringLiteral("groupInfoAnnouncementBody"));
    announcementBody->setWordWrap(true);
    announcementBody->setMaximumHeight(62);
    announcementLayout->addWidget(announcementBody);
    contentLayout->addWidget(announcementCard);

    auto* memberCard = new QFrame(&panel);
    memberCard->setObjectName(QStringLiteral("groupInfoMemberCard"));
    auto* memberLayout = new QVBoxLayout(memberCard);
    memberLayout->setContentsMargins(14, 12, 14, 12);
    memberLayout->setSpacing(9);
    auto* memberHeader = new QHBoxLayout();
    auto* memberTitle = new QLabel(QStringLiteral("群成员  %1").arg(members.size()), memberCard);
    memberTitle->setObjectName(QStringLiteral("groupInfoMemberTitle"));
    memberHeader->addWidget(memberTitle);
    memberHeader->addStretch();
    auto* allMembers = new QPushButton(QStringLiteral("查看全部"), memberCard);
    allMembers->setObjectName(QStringLiteral("groupInfoLinkButton"));
    allMembers->setToolTip(QStringLiteral("打开群成员列表"));
    memberHeader->addWidget(allMembers);
    memberLayout->addLayout(memberHeader);
    auto* memberGrid = new QGridLayout();
    memberGrid->setHorizontalSpacing(9);
    memberGrid->setVerticalSpacing(8);
    const int visibleMembers = qMin(10, members.size());
    for (int index = 0; index < visibleMembers; ++index) {
        const QString memberId = members.at(index);
        const QString display = memberId == m_currentUserId ? m_currentUserName
            : (serverGroup ? m_serverGroupMemberNames.value(groupId + QStringLiteral("|") + memberId,
                                                             contactDisplayName(memberId))
                           : contactDisplayName(memberId));
        auto* memberItem = new QWidget(memberCard);
        auto* itemLayout = new QVBoxLayout(memberItem);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(3);
        auto* avatar = new AvatarLabel(memberItem, 34);
        const QString memberAvatarPath = peerAvatarPath(memberId);
        if (!memberAvatarPath.isEmpty() && QFileInfo::exists(memberAvatarPath)) {
            avatar->setPixmap(QPixmap(memberAvatarPath));
        } else {
            avatar->setTextAvatar(display, ThemeManager::instance()->primaryColor());
        }
        avatar->setToolTip(display);
        auto* memberName = new QLabel(display.left(4), memberItem);
        memberName->setObjectName(QStringLiteral("groupInfoMemberName"));
        memberName->setAlignment(Qt::AlignCenter);
        itemLayout->addWidget(avatar, 0, Qt::AlignHCenter);
        itemLayout->addWidget(memberName);
        memberGrid->addWidget(memberItem, index / 5, index % 5);
    }
    if (manager) {
        auto* invite = new QToolButton(memberCard);
        invite->setObjectName(QStringLiteral("groupInfoMemberAction"));
        invite->setText(QStringLiteral("+"));
        invite->setToolTip(QStringLiteral("邀请群成员"));
        invite->setAccessibleName(QStringLiteral("邀请群成员"));
        invite->setFixedSize(34, 34);
        memberGrid->addWidget(invite, visibleMembers / 5, visibleMembers % 5, Qt::AlignHCenter);
        connect(invite, &QToolButton::clicked, this, [this, serverGroup, groupId, promptText]() {
            const auto result = promptText(QStringLiteral("邀请群成员"), QStringLiteral("输入对方 QQ 号"), QString());
            const QString account = result.second;
            if (!result.first || account.isEmpty()) return;
            if (serverGroup) {
                requestServerGroupMemberUpdate(account, QStringLiteral("add"), groupId);
                return;
            }
            if (!isCurrentUserGroupOwner(groupId)) return;
            if (!m_localGroupMembers[groupId].contains(account)) {
                m_localGroupMembers[groupId].append(account);
                saveLocalGroups();
                refreshGroupMemberSidebar();
            }
        });
    }
    memberLayout->addLayout(memberGrid);
    contentLayout->addWidget(memberCard);
    connect(allMembers, &QPushButton::clicked, this, [this, &panel]() {
        if (m_messagesView) {
            refreshGroupMemberSidebar();
            m_messagesView->setGroupMemberSidebarVisible(true);
        }
        panel.accept();
    });

    if (manager) {
        addCaption(QStringLiteral("资料管理"));
        QPushButton* profileRow = addRow(QStringLiteral("群资料设置"), QStringLiteral("群名称、头像"), true);
        connect(profileRow, &QPushButton::clicked, this, [this, serverGroup, groupId, groupName]() {
            QDialog editor(this);
            editor.setObjectName(QStringLiteral("groupProfileEditor"));
            editor.setWindowTitle(QStringLiteral("编辑群资料"));
            editor.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
            editor.setFixedSize(360, 270);
            auto* editorLayout = new QVBoxLayout(&editor);
            editorLayout->setContentsMargins(0, 0, 0, 16);
            editorLayout->setSpacing(0);
            auto* editorBar = new DialogTitleBar(&editor, QStringLiteral("编辑群资料"));
            connect(editorBar, &DialogTitleBar::closeRequested, &editor, &QDialog::reject);
            editorLayout->addWidget(editorBar);
            auto* editorContent = new QWidget(&editor);
            auto* contentLayout = new QVBoxLayout(editorContent);
            contentLayout->setContentsMargins(18, 14, 18, 0);
            contentLayout->setSpacing(10);
            auto* editorHint = new QLabel(QStringLiteral("修改后将同步给当前群成员"), editorContent);
            editorHint->setObjectName(QStringLiteral("groupProfileEditorHint"));
            contentLayout->addWidget(editorHint);

            auto* profileRowLayout = new QHBoxLayout();
            profileRowLayout->setSpacing(12);
            auto* preview = new AvatarLabel(editorContent, 56);
            QByteArray avatarBytes;
            if (serverGroup) {
                avatarBytes = QByteArray::fromBase64(m_serverGroupSettings.value(groupId)
                                                          .value(QStringLiteral("avatar")).toString().toUtf8());
            } else {
                const QString path = m_localGroupAvatarPaths.value(groupId);
                if (!path.isEmpty()) {
                    QFile avatarFile(path);
                    if (avatarFile.open(QIODevice::ReadOnly)) avatarBytes = avatarFile.readAll();
                }
            }
            QPixmap currentAvatar;
            currentAvatar.loadFromData(avatarBytes);
            if (currentAvatar.isNull()) preview->setTextAvatar(groupName, ThemeManager::instance()->primaryColor());
            else preview->setPixmap(currentAvatar);
            profileRowLayout->addWidget(preview);
            auto* profileFields = new QVBoxLayout();
            profileFields->setSpacing(6);
            auto* nameInput = new QLineEdit(groupName, editorContent);
            nameInput->setObjectName(QStringLiteral("groupProfileNameInput"));
            nameInput->setMaxLength(80);
            nameInput->setPlaceholderText(QStringLiteral("输入群名称"));
            auto* chooseAvatar = new QPushButton(QStringLiteral("从本地选择图片"), editorContent);
            chooseAvatar->setObjectName(QStringLiteral("groupProfileAvatarButton"));
            chooseAvatar->setCursor(Qt::PointingHandCursor);
            profileFields->addWidget(nameInput);
            profileFields->addWidget(chooseAvatar, 0, Qt::AlignLeft);
            profileRowLayout->addLayout(profileFields, 1);
            contentLayout->addLayout(profileRowLayout);
            connect(chooseAvatar, &QPushButton::clicked, &editor, [this, &editor, preview, chooseAvatar, &avatarBytes, groupName]() {
                const QString picturesDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
                const QString avatarFilePath = QFileDialog::getOpenFileName(&editor, QStringLiteral("选择本地群头像"), picturesDirectory,
                    QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
                if (avatarFilePath.isEmpty()) return;
                QImageReader reader(avatarFilePath);
                reader.setAutoTransform(true);
                const QImage original = reader.read();
                if (original.isNull()) {
                    ui->statusbar->showMessage(QStringLiteral("图片读取失败，请选择有效的图片文件"), 2200);
                    return;
                }
                QImage normalized = original.scaled(256, 256, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                const int left = qMax(0, (normalized.width() - 256) / 2);
                const int top = qMax(0, (normalized.height() - 256) / 2);
                normalized = normalized.copy(left, top, qMin(256, normalized.width()), qMin(256, normalized.height()));
                QByteArray encoded;
                QBuffer buffer(&encoded);
                if (!buffer.open(QIODevice::WriteOnly) || !normalized.save(&buffer, "PNG")) {
                    ui->statusbar->showMessage(QStringLiteral("群头像处理失败"), 2200);
                    return;
                }
                if (encoded.size() > 768 * 1024) {
                    ui->statusbar->showMessage(QStringLiteral("图片内容过大，请选择更简单的图片"), 2400);
                    return;
                }
                avatarBytes = encoded;
                preview->setPixmap(QPixmap::fromImage(normalized));
                chooseAvatar->setText(QStringLiteral("已选择本地图片"));
                Q_UNUSED(groupName);
            });

            auto* editorActions = new QHBoxLayout();
            editorActions->addStretch();
            auto* cancel = new QPushButton(QStringLiteral("取消"), editorContent);
            cancel->setObjectName(QStringLiteral("dialogSecondaryBtn"));
            auto* save = new QPushButton(QStringLiteral("保存"), editorContent);
            save->setObjectName(QStringLiteral("dialogPrimaryBtn"));
            save->setDefault(true);
            editorActions->addWidget(cancel);
            editorActions->addWidget(save);
            contentLayout->addLayout(editorActions);
            editorLayout->addWidget(editorContent, 1);
            connect(cancel, &QPushButton::clicked, &editor, &QDialog::reject);
            connect(save, &QPushButton::clicked, &editor, [&editor, nameInput]() {
                if (nameInput->text().trimmed().isEmpty()) {
                    nameInput->setFocus();
                    return;
                }
                editor.accept();
            });
            editor.setStyleSheet(DialogStyle::common() + QStringLiteral(
                "QDialog#groupProfileEditor { background:%1; border:1px solid %5; }"
                "QLabel#groupProfileEditorHint { color:%3; font-size:12px; }"
                "QLineEdit#groupProfileNameInput { background:%4; color:%2; border:1px solid %5; border-radius:6px; padding:7px 9px; }"
                "QLineEdit#groupProfileNameInput:focus { border-color:%6; }"
                "QPushButton#groupProfileAvatarButton { color:%6; background:transparent; border:none; padding:2px 0; }"
                "QPushButton#groupProfileAvatarButton:hover { color:%7; }")
                .arg(ThemeManager::instance()->backgroundColor().name(), ThemeManager::instance()->textColor().name(),
                     ThemeManager::instance()->textSecondaryColor().name(), ThemeManager::instance()->backgroundSecondaryColor().name(),
                     ThemeManager::instance()->borderColor().name(), ThemeManager::instance()->primaryColor().name(),
                     ThemeManager::instance()->primaryHoverColor().name()));
            if (editor.exec() != QDialog::Accepted) return;
            const QString newName = nameInput->text().trimmed();
            QJsonObject update{{QStringLiteral("groupName"), newName}};
            if (!avatarBytes.isEmpty()) update[QStringLiteral("avatar")] = QString::fromUtf8(avatarBytes.toBase64());
            if (serverGroup) {
                if (!m_client || !m_client->sendServerGroupSettingsUpdate(groupId, update)) {
                    ui->statusbar->showMessage(QStringLiteral("群资料提交失败，请检查连接"), 2600);
                } else {
                    ui->statusbar->showMessage(QStringLiteral("群资料已提交，正在同步"), 1800);
                }
            } else {
                m_localGroupNames[groupId] = newName;
                if (!avatarBytes.isEmpty()) {
                    const QString avatarDirectory = QDir(ClientStorage::appDataRootDirectory())
                        .filePath(QStringLiteral("group_avatars"));
                    QDir().mkpath(avatarDirectory);
                    const QString localAvatarPath = QDir(avatarDirectory)
                        .filePath(groupId + QStringLiteral(".png"));
                    QPixmap avatar;
                    if (avatar.loadFromData(avatarBytes) && avatar.save(localAvatarPath, "PNG")) {
                        m_localGroupAvatarPaths[groupId] = localAvatarPath;
                    }
                }
                saveLocalGroups();
                refreshFriendList();
                ui->statusbar->showMessage(QStringLiteral("群资料已保存"), 1800);
            }
        });
        addCaption(QStringLiteral("发言权限"));
        auto* muteRow = new QFrame(&panel);
        muteRow->setObjectName(QStringLiteral("groupInfoRow"));
        muteRow->setFixedHeight(48);
        auto* muteLayout = new QHBoxLayout(muteRow);
        muteLayout->setContentsMargins(15, 0, 13, 0);
        auto* muteLabel = new QLabel(QStringLiteral("全员禁言"), muteRow);
        muteLabel->setObjectName(QStringLiteral("groupInfoRowTitle"));
        auto* muteToggle = new QCheckBox(muteRow);
        muteToggle->setToolTip(QStringLiteral("仅允许管理员和群主发言"));
        muteLayout->addWidget(muteLabel);
        muteLayout->addStretch();
        muteLayout->addWidget(muteToggle);
        if (activeSettingsLayout) activeSettingsLayout->addWidget(muteRow);
        const QString allMuteKey = stableSettingKey() + QStringLiteral("allMuted");
        muteToggle->setChecked(serverGroup ? serverSettings.value(QStringLiteral("allMuted")).toBool(false)
                                           : settings.value(allMuteKey, false).toBool());
        connect(muteToggle, &QCheckBox::toggled, this, [this, manager, allMuteKey, serverGroup, groupId](bool enabled) {
            if (!manager) return;
            if (serverGroup) {
                if (!m_client || !m_client->sendServerGroupSettingsUpdate(groupId, QJsonObject{{QStringLiteral("allMuted"), enabled}})) {
                    ui->statusbar->showMessage(QStringLiteral("全员禁言提交失败，请检查连接"), 2600);
                }
                return;
            }
            QSettings localSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
            localSettings.setValue(allMuteKey, enabled);
            ui->statusbar->showMessage(enabled ? QStringLiteral("已保存全员禁言状态") : QStringLiteral("已解除全员禁言状态"), 2200);
        });
        const QString speakingRule = serverGroup ? serverSettings.value(QStringLiteral("speakingRule")).toString(QStringLiteral("unrestricted"))
                                                 : settings.value(stableSettingKey() + QStringLiteral("speakingRule"), QStringLiteral("unrestricted")).toString();
        const QMap<QString, QString> speakingTexts = {
            {QStringLiteral("unrestricted"), QStringLiteral("不限制发言")},
            {QStringLiteral("per_minute_10"), QStringLiteral("每分钟 10 条")},
            {QStringLiteral("per_minute_5"), QStringLiteral("每分钟 5 条")},
            {QStringLiteral("new_members_24h"), QStringLiteral("新成员 24 小时后可发言")}
        };
        QPushButton* speakingRow = addRow(QStringLiteral("发言限制"), speakingTexts.value(speakingRule, QStringLiteral("不限制发言")), true);
        connect(speakingRow, &QPushButton::clicked, this, [this, speakingRow, stableSettingKey, serverGroup, groupId, chooseSetting, speakingTexts]() {
            const QString current = serverGroup
                ? m_serverGroupSettings.value(groupId).value(QStringLiteral("speakingRule")).toString(QStringLiteral("unrestricted"))
                : QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).value(stableSettingKey() + QStringLiteral("speakingRule"), QStringLiteral("unrestricted")).toString();
            const QList<QPair<QString, QString>> choices = {
                {QStringLiteral("不限制发言"), QStringLiteral("unrestricted")},
                {QStringLiteral("每分钟 10 条"), QStringLiteral("per_minute_10")},
                {QStringLiteral("每分钟 5 条"), QStringLiteral("per_minute_5")},
                {QStringLiteral("新成员 24 小时后可发言"), QStringLiteral("new_members_24h")}
            };
            const QString selected = chooseSetting(QStringLiteral("发言限制"), QStringLiteral("选择群成员发言频率"), choices, current);
            if (selected.isEmpty() || selected == current) return;
            if (serverGroup && (!m_client || !m_client->sendServerGroupSettingsUpdate(groupId, QJsonObject{{QStringLiteral("speakingRule"), selected}}))) {
                ui->statusbar->showMessage(QStringLiteral("发言限制提交失败，请检查连接"), 2600);
                return;
            }
            if (!serverGroup) {
                QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).setValue(stableSettingKey() + QStringLiteral("speakingRule"), selected);
            }
            speakingRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->setText(speakingTexts.value(selected));
        });
        addCaption(QStringLiteral("开放设置"));
        const QString joinPolicy = serverGroup ? serverSettings.value(QStringLiteral("joinPolicy")).toString(QStringLiteral("approval"))
                                               : settings.value(stableSettingKey() + QStringLiteral("joinPolicy"), QStringLiteral("approval")).toString();
        const QMap<QString, QString> joinTexts = {
            {QStringLiteral("open"), QStringLiteral("允许任何人加群")},
            {QStringLiteral("approval"), QStringLiteral("需要身份验证")},
            {QStringLiteral("disabled"), QStringLiteral("不允许任何人加群")}
        };
        QPushButton* joinRow = addRow(QStringLiteral("加群方式"), joinTexts.value(joinPolicy, QStringLiteral("需要身份验证")), true);
        connect(joinRow, &QPushButton::clicked, this, [this, joinRow, serverGroup, groupId, stableSettingKey, chooseSetting, joinTexts]() {
            const QString current = serverGroup
                ? m_serverGroupSettings.value(groupId).value(QStringLiteral("joinPolicy")).toString(QStringLiteral("approval"))
                : QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).value(stableSettingKey() + QStringLiteral("joinPolicy"), QStringLiteral("approval")).toString();
            const QString selected = chooseSetting(QStringLiteral("加群方式"), QStringLiteral("选择加入当前群聊的方式"), {
                {QStringLiteral("允许任何人加群"), QStringLiteral("open")},
                {QStringLiteral("需要身份验证"), QStringLiteral("approval")},
                {QStringLiteral("不允许任何人加群"), QStringLiteral("disabled")}
            }, current);
            if (selected.isEmpty() || selected == current) return;
            if (serverGroup) {
                if (!m_client || !m_client->sendServerGroupSettingsUpdate(groupId, QJsonObject{{QStringLiteral("joinPolicy"), selected}})) {
                    ui->statusbar->showMessage(QStringLiteral("加群方式提交失败，请检查连接"), 2600);
                    return;
                }
            } else {
                QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).setValue(stableSettingKey() + QStringLiteral("joinPolicy"), selected);
            }
            joinRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->setText(joinTexts.value(selected));
        });
        const QString searchMode = serverGroup
            ? serverSettings.value(QStringLiteral("searchMode")).toString(QStringLiteral("id_and_keyword"))
            : settings.value(stableSettingKey() + QStringLiteral("searchMode"), QStringLiteral("id_and_keyword")).toString();
        const QMap<QString, QString> searchTexts = {
            {QStringLiteral("id_and_keyword"), QStringLiteral("通过群号及关键词搜索")},
            {QStringLiteral("id_only"), QStringLiteral("通过群号搜索")},
            {QStringLiteral("private"), QStringLiteral("私密")}
        };
        QPushButton* searchRow = addRow(QStringLiteral("群搜索方式"), searchTexts.value(searchMode), true);
        connect(searchRow, &QPushButton::clicked, this, [this, searchRow, serverGroup, groupId, stableSettingKey, chooseSetting, searchTexts]() {
            const QString current = serverGroup
                ? m_serverGroupSettings.value(groupId).value(QStringLiteral("searchMode")).toString(QStringLiteral("id_and_keyword"))
                : QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).value(stableSettingKey() + QStringLiteral("searchMode"), QStringLiteral("id_and_keyword")).toString();
            const QString selected = chooseSetting(QStringLiteral("群搜索方式"), QStringLiteral("选择其他用户查找群聊的方式"), {
                {QStringLiteral("通过群号及关键词搜索"), QStringLiteral("id_and_keyword")},
                {QStringLiteral("通过群号搜索"), QStringLiteral("id_only")},
                {QStringLiteral("私密"), QStringLiteral("private")}
            }, current);
            if (selected.isEmpty() || selected == current) return;
            if (serverGroup) {
                if (!m_client || !m_client->sendServerGroupSettingsUpdate(groupId, QJsonObject{{QStringLiteral("searchMode"), selected}})) {
                    ui->statusbar->showMessage(QStringLiteral("群搜索方式提交失败，请检查连接"), 2600);
                    return;
                }
            } else {
                QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).setValue(stableSettingKey() + QStringLiteral("searchMode"), selected);
            }
            searchRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->setText(searchTexts.value(selected));
        });
    }

    addCaption(QStringLiteral("我的群资料"));
    const QString nicknameKey = stableSettingKey() + QStringLiteral("nickname");
    const QString remarkKey = stableSettingKey() + QStringLiteral("remark");
    const QString currentNickname = serverGroup ? userSettings.value(QStringLiteral("nickname")).toString(m_currentUserName)
                                                : settings.value(nicknameKey, m_currentUserName).toString();
    const QString currentRemark = serverGroup ? userSettings.value(QStringLiteral("remark")).toString()
                                              : settings.value(remarkKey).toString();
    QPushButton* nicknameRow = addRow(QStringLiteral("我的本群昵称"), currentNickname, true);
    connect(nicknameRow, &QPushButton::clicked, this, [nicknameRow, nicknameKey, serverGroup, groupId, this, promptText]() {
        const QString current = nicknameRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->text();
        const auto result = promptText(QStringLiteral("我的本群昵称"), QStringLiteral("输入群昵称"), current);
        const QString value = result.second;
        if (!result.first || value.isEmpty()) return;
        if (serverGroup) {
            if (!m_client || !m_client->sendServerGroupUserSettingsUpdate(groupId, QJsonObject{{QStringLiteral("nickname"), value}})) {
                ui->statusbar->showMessage(QStringLiteral("群昵称保存失败，请检查连接"), 2600);
            }
            return;
        }
        QSettings localSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
        localSettings.setValue(nicknameKey, value);
        nicknameRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->setText(value);
    });
    QPushButton* remarkRow = addRow(QStringLiteral("群聊备注"), currentRemark.isEmpty() ? QStringLiteral("填写备注") : currentRemark, true);
    connect(remarkRow, &QPushButton::clicked, this, [remarkRow, remarkKey, serverGroup, groupId, this, promptText]() {
        const QString current = remarkRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->text();
        const auto result = promptText(QStringLiteral("群聊备注"), QStringLiteral("输入备注名称"),
                                       current == QStringLiteral("填写备注") ? QString() : current);
        const QString value = result.second;
        if (!result.first) return;
        if (serverGroup) {
            if (!m_client || !m_client->sendServerGroupUserSettingsUpdate(groupId, QJsonObject{{QStringLiteral("remark"), value}})) {
                ui->statusbar->showMessage(QStringLiteral("群聊备注保存失败，请检查连接"), 2600);
            }
            return;
        }
        QSettings localSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
        localSettings.setValue(remarkKey, value);
        remarkRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->setText(value.isEmpty() ? QStringLiteral("填写备注") : value);
    });
    addCaption(QStringLiteral("消息设置"));
    auto* notificationRow = new QFrame(&panel);
    notificationRow->setObjectName(QStringLiteral("groupInfoRow"));
    notificationRow->setFixedHeight(42);
    auto* notificationLayout = new QHBoxLayout(notificationRow);
    notificationLayout->setContentsMargins(12, 0, 12, 0);
    auto* notificationLabel = new QLabel(QStringLiteral("消息免打扰"), notificationRow);
    notificationLabel->setObjectName(QStringLiteral("groupInfoRowTitle"));
    auto* notificationToggle = new QCheckBox(notificationRow);
    notificationLayout->addWidget(notificationLabel);
    notificationLayout->addStretch();
    notificationLayout->addWidget(notificationToggle);
    if (activeSettingsLayout) activeSettingsLayout->addWidget(notificationRow);
    const QString muteNotificationKey = stableSettingKey() + QStringLiteral("muteNotifications");
    notificationToggle->setChecked(serverGroup ? userSettings.value(QStringLiteral("muteNotifications")).toBool(false)
                                               : settings.value(muteNotificationKey, false).toBool());
    connect(notificationToggle, &QCheckBox::toggled, this, [this, serverGroup, groupId, muteNotificationKey](bool enabled) {
        if (serverGroup) {
            if (!m_client || !m_client->sendServerGroupUserSettingsUpdate(groupId, QJsonObject{{QStringLiteral("muteNotifications"), enabled}})) {
                ui->statusbar->showMessage(QStringLiteral("消息设置保存失败，请检查连接"), 2600);
            }
            return;
        }
        QSettings localSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
        localSettings.setValue(muteNotificationKey, enabled);
    });
    const QString receiveMode = serverGroup
        ? userSettings.value(QStringLiteral("receiveMode")).toString(QStringLiteral("receive_quiet"))
        : settings.value(stableSettingKey() + QStringLiteral("receiveMode"), QStringLiteral("receive_quiet")).toString();
    const QMap<QString, QString> receiveTexts = {
        {QStringLiteral("receive_quiet"), QStringLiteral("接收消息但不提醒")},
        {QStringLiteral("assistant_quiet"), QStringLiteral("收进群助手且不提醒")},
        {QStringLiteral("block"), QStringLiteral("屏蔽群消息")}
    };
    QPushButton* receiveRow = addRow(QStringLiteral("群消息设置"), receiveTexts.value(receiveMode), true);
    connect(receiveRow, &QPushButton::clicked, this, [this, receiveRow, stableSettingKey, serverGroup, groupId, chooseSetting, receiveTexts]() {
        const QSettings localSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
        const QString current = serverGroup
            ? m_serverGroupUserSettings.value(groupId).value(QStringLiteral("receiveMode")).toString(QStringLiteral("receive_quiet"))
            : localSettings.value(stableSettingKey() + QStringLiteral("receiveMode"), QStringLiteral("receive_quiet")).toString();
        const QString selected = chooseSetting(QStringLiteral("群消息设置"), QStringLiteral("选择该群消息的接收方式"), {
            {QStringLiteral("接收消息但不提醒"), QStringLiteral("receive_quiet")},
            {QStringLiteral("收进群助手且不提醒"), QStringLiteral("assistant_quiet")},
            {QStringLiteral("屏蔽群消息"), QStringLiteral("block")}
        }, current);
        if (selected.isEmpty() || selected == current) return;
        if (serverGroup && (!m_client || !m_client->sendServerGroupUserSettingsUpdate(
                groupId, QJsonObject{{QStringLiteral("receiveMode"), selected}}))) {
            ui->statusbar->showMessage(QStringLiteral("消息设置保存失败，请检查连接"), 2600);
            return;
        }
        if (!serverGroup) QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).setValue(stableSettingKey() + QStringLiteral("receiveMode"), selected);
        receiveRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->setText(receiveTexts.value(selected));
    });
    const QString pinKey = stableSettingKey() + QStringLiteral("pinned");
    auto* pinRow = new QFrame(&panel);
    pinRow->setObjectName(QStringLiteral("groupInfoRow"));
    pinRow->setFixedHeight(42);
    auto* pinLayout = new QHBoxLayout(pinRow);
    pinLayout->setContentsMargins(12, 0, 12, 0);
    auto* pinLabel = new QLabel(QStringLiteral("设为置顶"), pinRow);
    pinLabel->setObjectName(QStringLiteral("groupInfoRowTitle"));
    auto* pinToggle = new QCheckBox(pinRow);
    pinToggle->setToolTip(QStringLiteral("将群聊固定在会话列表顶部"));
    pinToggle->setChecked(settings.value(pinKey, false).toBool());
    pinLayout->addWidget(pinLabel);
    pinLayout->addStretch();
    pinLayout->addWidget(pinToggle);
    if (activeSettingsLayout) activeSettingsLayout->addWidget(pinRow);
    connect(pinToggle, &QCheckBox::toggled, this, [this, pinKey](bool enabled) {
        QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).setValue(pinKey, enabled);
        refreshFriendList();
        ui->statusbar->showMessage(enabled ? QStringLiteral("已置顶群聊") : QStringLiteral("已取消置顶"), 1800);
    });

    if (serverGroup && owner && serverGroupId != QLatin1String("public")) {
        QPushButton* dissolve = new QPushButton(QStringLiteral("解散群聊"), &panel);
        dissolve->setObjectName(QStringLiteral("groupInfoLeaveBtn"));
        dissolve->setCursor(Qt::PointingHandCursor);
        dissolve->setFixedHeight(48);
        contentLayout->addWidget(dissolve);
        connect(dissolve, &QPushButton::clicked, this, [this, serverGroupId, groupName, &panel, confirmDanger]() {
            if (confirmDanger(QStringLiteral("解散群聊"),
                              QStringLiteral("确定解散“%1”吗？解散后成员将无法继续访问该群聊，此操作不可撤销。").arg(groupName),
                              QStringLiteral("确认解散"))
                && m_client && m_client->dissolveServerGroup(serverGroupId)) panel.accept();
        });
    } else if (serverGroup && !owner) {
        QPushButton* leave = new QPushButton(QStringLiteral("退出群聊"), &panel);
        leave->setObjectName(QStringLiteral("groupInfoLeaveBtn"));
        leave->setCursor(Qt::PointingHandCursor);
        leave->setFixedHeight(48);
        contentLayout->addWidget(leave);
        connect(leave, &QPushButton::clicked, this, [this, serverGroupId, groupName, &panel, confirmDanger]() {
            if (confirmDanger(QStringLiteral("退出群聊"), QStringLiteral("确定退出“%1”吗？退出后将不再接收该群消息。").arg(groupName),
                              QStringLiteral("确认退出"))
                && m_client && m_client->leaveServerGroup(serverGroupId)) panel.accept();
        });
    } else if (localGroup && owner) {
        QPushButton* dissolve = new QPushButton(QStringLiteral("解散本地群聊"), &panel);
        dissolve->setObjectName(QStringLiteral("groupInfoLeaveBtn"));
        dissolve->setCursor(Qt::PointingHandCursor);
        dissolve->setFixedHeight(48);
        contentLayout->addWidget(dissolve);
        connect(dissolve, &QPushButton::clicked, this, [this, groupId, groupName, &panel, confirmDanger]() {
            if (!confirmDanger(QStringLiteral("解散本地群聊"),
                               QStringLiteral("确定解散“%1”吗？所有本地成员和聊天记录入口将被移除，此操作不可撤销。").arg(groupName),
                               QStringLiteral("确认解散"))) return;
            m_localGroupIds.removeAll(groupId);
            m_localGroupNames.remove(groupId);
            m_localGroupMembers.remove(groupId);
            m_localGroupAnnouncements.remove(groupId);
            m_localGroupAvatarPaths.remove(groupId);
            saveLocalGroups();
            m_privateChatTarget.clear();
            refreshFriendList();
            panel.accept();
        });
    } else if (localGroup && !owner) {
        QPushButton* leave = new QPushButton(QStringLiteral("退出本地群聊"), &panel);
        leave->setObjectName(QStringLiteral("groupInfoLeaveBtn"));
        leave->setCursor(Qt::PointingHandCursor);
        leave->setFixedHeight(48);
        contentLayout->addWidget(leave);
        connect(leave, &QPushButton::clicked, this, [this, groupId, groupName, &panel, confirmDanger]() {
            if (!confirmDanger(QStringLiteral("退出群聊"), QStringLiteral("确定退出“%1”吗？退出后将不再接收该群消息。").arg(groupName),
                               QStringLiteral("确认退出"))) return;
            m_localGroupMembers[groupId].removeAll(m_currentUserId);
            saveLocalGroups();
            refreshFriendList();
            panel.accept();
        });
    }

    panel.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#groupInfoDialog,QScrollArea#groupInfoScroll,QWidget#groupInfoContent { background:%1; }"
        "QScrollArea#groupInfoScroll { border:none; }"
        "QLabel#groupInfoPanelTitle { color:%4; font-size:15px; font-weight:600; }"
        "QLabel#groupInfoName { color:%4; font-size:17px; font-weight:600; }"
        "QLabel#groupInfoRowTitle,QLabel#groupInfoMemberTitle,QLabel#groupInfoAnnouncementTitle { color:%4; font-size:13px; font-weight:500; }"
        "QFrame#groupInfoOverview,QFrame#groupInfoAnnouncementCard,QFrame#groupInfoMemberCard,QFrame#groupInfoSettingCard { background:%2; border:1px solid %3; border-radius:8px; }"
        "QFrame#groupInfoRow,QPushButton#groupInfoRow,QPushButton#groupInfoActionRow { background:transparent; border:none; border-bottom:1px solid %3; border-radius:0; text-align:left; }"
        "QPushButton#groupInfoActionRow:hover { background:%5; }"
        "QPushButton#groupInfoActionRow:pressed { background:%8; }"
        "QPushButton#groupInfoActionRow:focus { border:1px solid %7; }"
        "QLabel#groupInfoMeta,QLabel#groupInfoRowValue,QLabel#groupInfoCaption,QLabel#groupInfoMemberName,QLabel#groupInfoAnnouncementBody { color:%6; font-size:12px; }"
        "QLabel#groupInfoAnnouncementBody { line-height:1.45; padding-top:2px; }"
        "QLabel#groupInfoRowValue { max-width:156px; color:%6; }"
        "QLabel#groupInfoCaption { color:%6; font-size:11px; font-weight:500; padding:14px 4px 4px 4px; }"
        "QLabel#groupInfoArrow { color:%6; font-size:20px; font-weight:400; padding-left:6px; }"
        "QLabel#groupInfoRoleManage { color:%7; background:%5; border-radius:3px; padding:0 6px; font-size:11px; }"
        "QLabel#groupInfoRoleMember { color:%6; background:%8; border-radius:3px; padding:0 6px; font-size:11px; }"
        "QToolButton#groupInfoAvatarButton { background:transparent; border:1px solid transparent; border-radius:28px; padding:0; }"
        "QToolButton#groupInfoAvatarButton:hover { border-color:%7; background:%5; }"
        "QToolButton#groupInfoAvatarButton:disabled { border-color:transparent; background:transparent; }"
        "QToolButton#groupInfoClose,QToolButton#groupInfoShare { color:%6; border:none; border-radius:4px; font-size:18px; }"
        "QToolButton#groupInfoClose:hover,QToolButton#groupInfoShare:hover { background:%5; color:%4; }"
        "QToolButton#groupInfoMemberAction { color:%7; background:%5; border:1px dashed %7; border-radius:17px; font-size:20px; }"
        "QToolButton#groupInfoMemberAction:hover { background:%7; color:%2; }"
        "QPushButton#groupInfoLinkButton { color:%7; background:transparent; border:none; font-size:12px; padding:3px 0; }"
        "QPushButton#groupInfoLinkButton:hover { color:%9; }"
        "QCheckBox { spacing:7px; }"
        "QCheckBox::indicator { width:30px; height:18px; border-radius:9px; background:%8; border:1px solid %3; image:none; }"
        "QCheckBox::indicator:checked { background:%7; border-color:%7; }"
        "QCheckBox::indicator:checked:disabled { background:%6; border-color:%6; }"
        "QPushButton#groupInfoLeaveBtn { background:%2; color:%10; border:1px solid %3; border-radius:8px; margin-top:18px; padding:0 18px; font-size:14px; font-weight:600; }"
        "QPushButton#groupInfoLeaveBtn:hover { background:%11; border-color:%10; }")
        .arg(ThemeManager::instance()->backgroundColor().name(), ThemeManager::instance()->backgroundSecondaryColor().name(),
             ThemeManager::instance()->borderColor().name(), ThemeManager::instance()->textColor().name(),
             ThemeManager::instance()->primarySoftColor().name(), ThemeManager::instance()->textSecondaryColor().name(),
             ThemeManager::instance()->primaryColor().name(), ThemeManager::instance()->backgroundTertiaryColor().name(),
             ThemeManager::instance()->primaryHoverColor().name(), ThemeManager::instance()->dangerColor().name(),
             ThemeManager::instance()->dangerColor().lighter(185).name()));
    const QPoint panelOrigin = mapToGlobal(QPoint(qMax(0, width() - panel.width() - 10), 36));
    panel.move(panelOrigin);
    panel.exec();
}

void MainWindow::showGlobalSearchDialog(bool contactGroupMode)
{
    // QQNT global search combines locally joined groups with server-side stranger
    // group discovery. A stranger group can only be entered after server approval.
    GlobalSearchDialog dialog(this);
    dialog.setContactGroupMode(contactGroupMode);
    // Every global-search variant receives async QQ account results. Previously
    // only contactGroupMode did so, leaving the standard search blank.
    m_activeContactGroupSearchDialog = &dialog;

    auto populate = [this, &dialog](const QString& keyword) {
        dialog.clearResults();
        const QString q = keyword.trimmed();
        const auto matches = [&q](const QString& id, const QString& name) {
            if (q.isEmpty()) return true;
            return id.contains(q, Qt::CaseInsensitive) || name.contains(q, Qt::CaseInsensitive);
        };
        for (const QString& id : m_friendIds) {
            const QString name = contactDisplayName(id);
            if (matches(id, name)) {
                dialog.addResult(QStringLiteral("contact"), id, name,
                                 QStringLiteral("QQ:%1").arg(id), peerAvatarPath(id));
                dialog.setContactKnown(id, true);
            }
        }
        for (const QString& id : m_localGroupIds) {
            const QString name = m_localGroupNames.value(id, QStringLiteral("群聊"));
            if (matches(id, name)) {
                const int count = m_localGroupMembers.value(id).size();
                dialog.addResult(QStringLiteral("group"), id, name,
                                 QStringLiteral("%1 人").arg(count), m_localGroupAvatarPaths.value(id));
                dialog.setGroupJoined(id, true);
            }
        }
        for (const QString& id : m_serverGroupNames.keys()) {
            if (id == QStringLiteral("public") || m_localGroupIds.contains(id)) continue;
            const QString name = m_serverGroupNames.value(id, QStringLiteral("群聊"));
            if (matches(id, name)) {
                const int count = m_serverGroupMembers.value(id).size();
                dialog.addResult(QStringLiteral("group"), id, name, QStringLiteral("%1 人").arg(count));
                dialog.setGroupJoined(id, m_joinedServerSearchGroups.contains(id));
            }
        }
    };

    // Prime the list with everything, then refilter on each search.
    populate(QString());
    connect(&dialog, &GlobalSearchDialog::searchRequested, this,
            [this, populate](const QString& text) {
        populate(text);
        if (!text.trimmed().isEmpty() && m_client) {
            if (!m_client->searchFriendByAccount(text.trimmed()) && m_activeContactGroupSearchDialog) {
                m_activeContactGroupSearchDialog->setSearching(false);
                m_activeContactGroupSearchDialog->showSearchState(QStringLiteral("当前未连接，搜索不可用"));
            }
            m_client->searchServerGroups(text.trimmed());
        } else if (m_activeContactGroupSearchDialog) {
            m_activeContactGroupSearchDialog->setSearching(false);
            m_activeContactGroupSearchDialog->showSearchState(QStringLiteral("当前未连接，搜索不可用"));
        }
    });
    connect(&dialog, &GlobalSearchDialog::addFriendRequested, this, [this, &dialog](const QString& userId) {
        if (userId.isEmpty() || userId == m_currentUserId) {
            ui->statusbar->showMessage(QStringLiteral("不能添加当前账号"), 2200);
            return;
        }
        if (m_friendIds.contains(userId)) {
            dialog.setContactKnown(userId, true);
            ui->statusbar->showMessage(QStringLiteral("该账号已经是你的好友"), 2200);
            return;
        }
        if (m_client && m_client->sendFriendRequest(userId)) {
            if (!m_pendingOutgoingFriendRequests.contains(userId)) m_pendingOutgoingFriendRequests << userId;
            saveFriends();
            ui->statusbar->showMessage(QStringLiteral("好友申请已发送"), 2200);
        } else {
            ui->statusbar->showMessage(QStringLiteral("好友申请发送失败，请检查连接"), 2600);
        }
    });
    connect(&dialog, &GlobalSearchDialog::joinGroupRequested, this,
            [this, &dialog](const QString& serverGroupId) {
        if (serverGroupId.isEmpty()) {
            return;
        }
        if (m_joinedServerSearchGroups.contains(serverGroupId)) {
            dialog.setGroupJoined(serverGroupId, true);
            return;
        }
        if (!m_client || !m_client->requestServerGroupJoin(serverGroupId)) {
            ui->statusbar->showMessage(QStringLiteral("入群申请发送失败，请检查连接"), 2800);
            return;
        }
        dialog.setGroupJoinPending(serverGroupId, true);
    });
    connect(&dialog, &GlobalSearchDialog::resultActivated, this,
            [this, &dialog](const QString& type, const QString& id) {
        if (type == QStringLiteral("group")) {
            const QString localId = m_joinedServerSearchGroups.value(id, id);
            const QString name = m_localGroupNames.value(localId, m_serverGroupNames.value(id, QStringLiteral("群聊")));
            switchToLocalGroup(localId, name);
        } else {
            openPrivateSession(id);
        }
        showMessagesView();
        dialog.accept();
    });

    dialog.exec();
    m_activeContactGroupSearchDialog = nullptr;
}

void MainWindow::showEssencePanel()
{
    // QQNT essence view: enumerate the current chat's essence-flagged messages
    // (marked via ChatBubbleForwardedRole by the essence/unessence command) and
    // present them in a modal panel. The panel shares Favorites' locate flow
    // and supports removing an essence mark directly from its context menu.
    EssencePanel panel(this);
    connect(&panel, &EssencePanel::messageActivated, this, [this, &panel](const QString& messageId) {
        if (messageId.isEmpty() || !m_chatModel || !m_messagesView) return;
        for (int row = 0; row < m_chatModel->rowCount(); ++row) {
            QStandardItem* item = m_chatModel->item(row);
            const QString itemId = item ? item->data(ChatMessageIdRole).toString() : QString();
            if (!item || (itemId != messageId && QString::number(row) != messageId)) continue;
            const QModelIndex target = m_chatModel->index(row, 0);
            if (QListView* view = m_messagesView->chatListView()) {
                view->scrollTo(target, QAbstractItemView::PositionAtCenter);
                view->setCurrentIndex(target);
            }
            panel.accept();
            ui->statusbar->showMessage(QStringLiteral("已定位精华消息"), 1800);
            return;
        }
        ui->statusbar->showMessage(QStringLiteral("未在当前聊天记录中找到该精华消息"), 2200);
    });
    connect(&panel, &EssencePanel::messageRemovalRequested, this, [this, &panel](const QString& messageId) {
        if (messageId.isEmpty() || !m_chatModel) return;
        for (int row = 0; row < m_chatModel->rowCount(); ++row) {
            QStandardItem* item = m_chatModel->item(row);
            const QString itemId = item ? item->data(ChatMessageIdRole).toString() : QString();
            if (!item || (itemId != messageId && QString::number(row) != messageId)) continue;
            const QModelIndex target = m_chatModel->index(row, 0);
            if (handleBackendContextCommand(QStringLiteral("unessence"), item->text(), target)) {
                panel.accept();
                QTimer::singleShot(0, this, &MainWindow::showEssencePanel);
            }
            return;
        }
        ui->statusbar->showMessage(QStringLiteral("取消精华失败：未找到原消息"), 2200);
    });
    const int rowCount = m_chatModel ? m_chatModel->rowCount() : 0;
    int essenceCount = 0;
    for (int row = 0; row < rowCount; ++row) {
        QStandardItem* item = m_chatModel->item(row);
        if (!item || !item->data(ChatBubbleForwardedRole).toBool()) {
            continue;
        }
        const QString text = item->data(Qt::DisplayRole).toString();
        if (text.isEmpty()) {
            continue;
        }
        const QString sender = item->data(ChatBubbleSenderNameRole).toString();
        const QString timestamp = item->data(ChatBubbleTimestampRole).toString();
        const QString messageId = item->data(ChatMessageIdRole).toString();
        panel.addEssenceMessage(messageId.isEmpty() ? QString::number(row) : messageId,
                                sender.isEmpty() ? QStringLiteral("未知") : sender,
                                text,
                                timestamp.isEmpty() ? QStringLiteral("--:--") : timestamp);
        ++essenceCount;
    }
    if (essenceCount == 0) {
        ui->statusbar->showMessage(QStringLiteral("当前会话暂无精华消息"), 2200);
    }
    panel.exec();
}

void MainWindow::onAppNavRouteActivated(const QString& route)
{
    if (route == QStringLiteral("messages")) {
        m_viewStack->setCurrentIndex(0);
    } else if (route == QStringLiteral("contacts")) {
        m_viewStack->setCurrentIndex(1);
    } else if (route == QStringLiteral("favorites")) {
        refreshFavoritesView();
        m_viewStack->setCurrentIndex(2);
    } else if (route == QStringLiteral("settings")) {
        m_viewStack->setCurrentIndex(3);
    } else if (route == QStringLiteral("profile")) {
        m_viewStack->setCurrentIndex(4);
    } else {
        // mock routes keep current view for now
        return;
    }
    qqntLog("MainWindow", QStringLiteral("route=%1 index=%2 session=%3")
            .arg(route,
                 QString::number(m_viewStack->currentIndex()),
                 m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget));
}




