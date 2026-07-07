#include "sessionitemdelegate.h"
#include "iconhelper.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QPainterPath>
#include <QFontMetrics>
#include <QPalette>
#include <QtMath>

namespace {
QPixmap generatedAvatar(const QString& displayName, const QString& seedId, int side) {
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

QPixmap roundAvatarPixmap(const QPixmap& source, int side) {
    if (source.isNull() || side <= 0) {
        return QPixmap();
    }
    QPixmap scaled = source.scaled(side, side, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = qMax(0, (scaled.width() - side) / 2);
    const int y = qMax(0, (scaled.height() - side) / 2);
    const QPixmap square = scaled.copy(x, y, side, side);

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
}

SessionItemDelegate::SessionItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {
}

QSize SessionItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(240, kRowHeight);
}

bool SessionItemDelegate::isDarkTheme(const QStyleOptionViewItem& option) const {
    if (const QWidget* widget = option.widget) {
        if (widget->property("theme").toString() == QLatin1String("dark")) {
            return true;
        }
    }
    return option.palette.window().color().lightness() < 128;
}

QColor SessionItemDelegate::textColor(const QStyleOptionViewItem& option, bool primary) const {
    if (isDarkTheme(option)) {
        return primary ? QColor("#e8e8e8") : QColor("#a8a8a8");
    }
    return primary ? QColor("#1f2329") : QColor("#5f6672");
}

QColor SessionItemDelegate::badgeColor() const {
    return QColor("#ff4d4f");
}

void SessionItemDelegate::paintAvatar(QPainter* painter, const QRect& rect, const QString& name, const QPixmap& avatar) const {
    QPixmap rounded;
    if (!avatar.isNull()) {
        rounded = roundAvatarPixmap(avatar, rect.width());
    }
    if (rounded.isNull()) {
        rounded = generatedAvatar(name, name, rect.width());
    }
    painter->drawPixmap(rect, rounded);
}

void SessionItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    const bool dark = isDarkTheme(option);

    if (selected) {
        painter->fillRect(option.rect, dark ? QColor("#3a3a42") : QColor("#e8f8ff"));
    } else if (hover) {
        painter->fillRect(option.rect, dark ? QColor("#2c2c32") : QColor("#f2f6fa"));
    } else {
        painter->fillRect(option.rect, dark ? QColor("#232328") : QColor("#ffffff"));
    }

    const int leftMargin = 12;
    const int rightMargin = 12;
    const int top = option.rect.top() + (option.rect.height() - kAvatarSize) / 2;
    const QRect avatarRect(option.rect.left() + leftMargin, top, kAvatarSize, kAvatarSize);

    const QString name = index.data(SessionNameRole).toString();
    const QString lastMessage = index.data(SessionLastMessageRole).toString();
    const QString timeText = index.data(SessionTimeRole).toString();
    const int unread = index.data(SessionUnreadRole).toInt();
    const bool pinned = index.data(SessionPinnedRole).toBool();
    const QPixmap avatar = index.data(SessionAvatarRole).value<QPixmap>();

    paintAvatar(painter, avatarRect, name.isEmpty() ? index.data(Qt::DisplayRole).toString() : name, avatar);

    const int textX = avatarRect.right() + 12;
    const int textRight = option.rect.right() - rightMargin;
    const int nameY = option.rect.top() + 14;
    const int msgY = option.rect.top() + 38;

    QFont nameFont = QApplication::font();
    nameFont.setPixelSize(14);
    nameFont.setBold(true);
    QFontMetrics nameFm(nameFont);

    const int timeWidth = timeText.isEmpty() ? 0 : QFontMetrics(QApplication::font()).horizontalAdvance(timeText) + 8;
    const int availableNameWidth = qMax(0, textRight - textX - timeWidth);

    painter->setFont(nameFont);
    painter->setPen(textColor(option, true));
    const QString elidedName = nameFm.elidedText(name.isEmpty() ? index.data(Qt::DisplayRole).toString() : name,
                                                  Qt::ElideRight, availableNameWidth);
    painter->drawText(QRect(textX, nameY, availableNameWidth, 20), Qt::AlignLeft | Qt::AlignVCenter, elidedName);

    if (!timeText.isEmpty()) {
        painter->setFont(QApplication::font());
        painter->setPen(textColor(option, false));
        painter->drawText(QRect(textRight - QFontMetrics(QApplication::font()).horizontalAdvance(timeText), nameY,
                                QFontMetrics(QApplication::font()).horizontalAdvance(timeText), 20),
                          Qt::AlignRight | Qt::AlignVCenter, timeText);
    }

    QFont msgFont = QApplication::font();
    msgFont.setPixelSize(12);
    QFontMetrics msgFm(msgFont);
    const int availableMsgWidth = textRight - textX - (unread > 0 ? kBadgeSize + 8 : 0);
    painter->setFont(msgFont);
    painter->setPen(textColor(option, false));
    const QString elidedMessage = msgFm.elidedText(lastMessage, Qt::ElideRight, qMax(0, availableMsgWidth));
    painter->drawText(QRect(textX, msgY, qMax(0, availableMsgWidth), 18), Qt::AlignLeft | Qt::AlignVCenter, elidedMessage);

    if (unread > 0) {
        const QString badgeText = unread > 99 ? QStringLiteral("99+") : QString::number(unread);
        QFont badgeFont = msgFont;
        badgeFont.setBold(true);
        QFontMetrics badgeFm(badgeFont);
        const int badgeWidth = qMax(kBadgeSize, badgeFm.horizontalAdvance(badgeText) + 10);
        const QRect badgeRect(textRight - badgeWidth, msgY + 1, badgeWidth, kBadgeSize);
        painter->setPen(Qt::NoPen);
        painter->setBrush(badgeColor());
        painter->drawRoundedRect(badgeRect, kBadgeSize / 2, kBadgeSize / 2);
        painter->setFont(badgeFont);
        painter->setPen(Qt::white);
        painter->drawText(badgeRect, Qt::AlignCenter, badgeText);
    }

    if (pinned) {
        const int pinSize = 14;
        const int pinX = textRight - pinSize;
        const int pinY = option.rect.top() + 6;
        painter->setPen(Qt::NoPen);
        painter->setBrush(dark ? QColor("#ffc53d") : QColor("#faad14"));
        QPainterPath starPath;
        const QPointF center(pinX + pinSize / 2.0, pinY + pinSize / 2.0);
        const qreal outerRadius = pinSize / 2.0;
        const qreal innerRadius = pinSize / 4.0;
        for (int i = 0; i < 10; ++i) {
            qreal radius = (i % 2 == 0) ? outerRadius : innerRadius;
            qreal angle = M_PI / 2.0 + i * M_PI / 5.0;
            starPath.lineTo(center.x() + radius * cos(angle),
                            center.y() - radius * sin(angle));
        }
        starPath.closeSubpath();
        painter->drawPath(starPath);
    }

    painter->restore();
}
