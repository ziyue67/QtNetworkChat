#include "sessionitemdelegate.h"

#include "theme/thememanager.h"

#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <QFontMetrics>
#include <QCryptographicHash>

namespace {

QPixmap roundAvatarPixmap(const QPixmap& source, int side) {
    if (source.isNull() || side <= 0) return QPixmap();

    QPixmap scaled = source.scaled(side, side, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = qMax(0, (scaled.width() - side) / 2);
    const int y = qMax(0, (scaled.height() - side) / 2);
    QPixmap square = scaled.copy(x, y, side, side);

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

QPixmap generatedAvatarPixmap(const QString& displayName, const QString& seedId, int side) {
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
    painter.drawText(QRect(0, 0, side, side), Qt::AlignCenter,
                     displayName.trimmed().isEmpty() ? QStringLiteral("?") : displayName.left(1).toUpper());
    return pixmap;
}

QString formatSessionTime(const QVariant& timeValue) {
    if (!timeValue.isValid() || timeValue.isNull()) return QString();
    const QDateTime dt = timeValue.toDateTime();
    if (!dt.isValid()) return QString();

    const QDateTime now = QDateTime::currentDateTime();
    if (dt.date() == now.date()) {
        return dt.toString(QStringLiteral("HH:mm"));
    } else if (dt.date().year() == now.date().year()) {
        return dt.toString(QStringLiteral("M-d"));
    }
    return dt.toString(QStringLiteral("yyyy-M-d"));
}

} // anonymous namespace

SessionItemDelegate::SessionItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QSize SessionItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(236, 64);
}

void SessionItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    ThemeManager* tm = ThemeManager::instance();
    const QRect rect = option.rect;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    // Background
    if (selected) {
        painter->fillRect(rect, tm->primarySoftColor());
    } else if (hovered) {
        painter->fillRect(rect, tm->color(QStringLiteral("session-hover")));
    }

    const int avatarSize = 44;
    const int margin = 8;
    const int contentLeft = rect.left() + margin + avatarSize + margin;
    const int contentRight = rect.right() - margin;
    const int centerY = rect.top() + rect.height() / 2;

    // Avatar
    const QRect avatarRect(rect.left() + margin, rect.top() + (rect.height() - avatarSize) / 2, avatarSize, avatarSize);
    QPixmap avatar = avatarPixmap(index, avatarSize);
    painter->drawPixmap(avatarRect, avatar);

    // Name
    const QString name = index.data(SessionNameRole).toString();
    const QString lastMessage = index.data(SessionLastMessageRole).toString();
    const QString timeText = formatSessionTime(index.data(SessionLastMessageTimestampRole));
    const int unreadCount = index.data(SessionUnreadRole).toInt();
    const bool atMention = index.data(SessionAtMentionRole).toBool();
    const bool pinned = index.data(SessionPinnedRole).toBool();
    const bool online = index.data(SessionOnlineRole).toBool();

    const int textY = rect.top() + margin;
    const int lineHeight = 18;

    QFont nameFont = option.font;
    nameFont.setBold(true);
    nameFont.setPointSize(9);
    painter->setFont(nameFont);
    painter->setPen(tm->textColor());

    const int badgeWidth = unreadCount > 0 ? 18 : 0;
    const int atBadgeWidth = atMention ? 18 : 0;
    const int pinWidth = pinned ? 16 : 0;
    const int rightReserved = badgeWidth + atBadgeWidth + pinWidth + 4 + (timeText.isEmpty() ? 0 : 40);

    const int nameMaxWidth = qMax(20, contentRight - contentLeft - rightReserved);
    const QFontMetrics nameFm(nameFont);
    const QString elidedName = nameFm.elidedText(name, Qt::ElideRight, nameMaxWidth);
    painter->drawText(QPoint(contentLeft, textY + nameFm.ascent()), elidedName);

    // Time
    if (!timeText.isEmpty()) {
        QFont timeFont = option.font;
        timeFont.setPointSize(8);
        painter->setFont(timeFont);
        painter->setPen(tm->textTertiaryColor());
        const QFontMetrics timeFm(timeFont);
        painter->drawText(QPoint(contentRight - timeFm.horizontalAdvance(timeText), textY + timeFm.ascent()), timeText);
    }

    // Last message
    QFont msgFont = option.font;
    msgFont.setPointSize(8);
    painter->setFont(msgFont);
    painter->setPen(tm->textSecondaryColor());
    const QFontMetrics msgFm(msgFont);
    const int msgMaxWidth = contentRight - contentLeft - badgeWidth - atBadgeWidth - 8;
    const QString elidedMsg = msgFm.elidedText(lastMessage, Qt::ElideRight, qMax(20, msgMaxWidth));
    painter->drawText(QPoint(contentLeft, textY + lineHeight + msgFm.ascent()), elidedMsg);

    // Badges
    int badgeX = contentRight;
    if (unreadCount > 0) {
        badgeX -= 18;
        const QRect badgeRect(badgeX, textY + lineHeight + 2, 16, 16);
        painter->setPen(Qt::NoPen);
        painter->setBrush(tm->dangerColor());
        painter->drawRoundedRect(badgeRect, 8, 8);
        painter->setPen(QPen(Qt::white));
        QFont badgeFont = option.font;
        badgeFont.setPointSize(8);
        badgeFont.setBold(true);
        painter->setFont(badgeFont);
        const QString badgeText = unreadCount > 99 ? QStringLiteral("99+") : QString::number(unreadCount);
        painter->drawText(badgeRect, Qt::AlignCenter, badgeText);
    }
    if (atMention) {
        badgeX -= 20;
        const QRect atRect(badgeX, textY + lineHeight + 2, 16, 16);
        painter->setBrush(tm->dangerColor());
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(atRect, 8, 8);
        painter->setPen(QPen(Qt::white));
        painter->drawText(atRect, Qt::AlignCenter, QStringLiteral("@"));
    }
    if (pinned) {
        QFont pinFont = option.font;
        pinFont.setPointSize(9);
        painter->setFont(pinFont);
        painter->setPen(tm->textTertiaryColor());
        painter->drawText(QPoint(contentRight - 12, textY + pinFont.pointSize() + 2), QStringLiteral("★"));
    }
    if (online) {
        const int onlineSize = 10;
        const QRect onlineRect(avatarRect.right() - onlineSize - 2, avatarRect.bottom() - onlineSize - 2, onlineSize, onlineSize);
        painter->setPen(QPen(Qt::white, 2));
        painter->setBrush(tm->successColor());
        painter->drawEllipse(onlineRect);
    }

    painter->restore();
}

QPixmap SessionItemDelegate::avatarPixmap(const QModelIndex& index, int size) const
{
    const QString avatarPath = index.data(SessionAvatarPathRole).toString();
    if (!avatarPath.isEmpty()) {
        QPixmap pixmap(avatarPath);
        if (!pixmap.isNull()) {
            return roundAvatarPixmap(pixmap, size);
        }
    }
    const QString name = index.data(SessionNameRole).toString();
    const QString id = index.data(SessionIdRole).toString();
    return generatedAvatarPixmap(name, id, size);
}
