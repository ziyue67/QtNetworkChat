#include "groupmemberitemdelegate.h"
#include "iconhelper.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QPainterPath>
#include <QFontMetrics>
#include <QPalette>
#include <QDateTime>

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

GroupMemberItemDelegate::GroupMemberItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {
}

QSize GroupMemberItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(200, kRowHeight);
}

bool GroupMemberItemDelegate::isDarkTheme(const QStyleOptionViewItem& option) const {
    if (const QWidget* widget = option.widget) {
        if (widget->property("theme").toString() == QLatin1String("dark")) {
            return true;
        }
    }
    return QApplication::palette().window().color().lightness() < 128;
}

QColor GroupMemberItemDelegate::textColor(bool primary) const {
    return primary ? QColor("#1f2329") : QColor("#5f6672");
}

QColor GroupMemberItemDelegate::roleBadgeColor(bool owner, bool admin, bool self) const {
    Q_UNUSED(self)
    if (owner) {
        return QColor("#faad14");
    }
    if (admin) {
        return QColor("#0099ff");
    }
    return QColor("#8f959e");
}

void GroupMemberItemDelegate::paintAvatar(QPainter* painter, const QRect& rect,
                                          const QString& name, const QPixmap& avatar) const {
    QPixmap rounded;
    if (!avatar.isNull()) {
        rounded = roundAvatarPixmap(avatar, rect.width());
    }
    if (rounded.isNull()) {
        rounded = generatedAvatar(name, name, rect.width());
    }
    painter->drawPixmap(rect, rounded);
}

void GroupMemberItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    const bool dark = isDarkTheme(option);

    if (selected) {
        painter->fillRect(option.rect, dark ? QColor("#1a3a52") : QColor("#e6f4ff"));
    } else if (hover) {
        painter->fillRect(option.rect, dark ? QColor("#2d2d2d") : QColor("#ebedf0"));
    }

    const int leftMargin = 14;
    const int rightMargin = 14;
    const int top = option.rect.top() + (option.rect.height() - kAvatarSize) / 2;
    const QRect avatarRect(option.rect.left() + leftMargin, top, kAvatarSize, kAvatarSize);

    const QString memberId = index.data(MemberIdRole).toString();
    const QString memberName = index.data(MemberNameRole).toString();
    const QString roleText = index.data(MemberRoleTextRole).toString();
    const bool online = index.data(MemberOnlineRole).toBool();
    const bool owner = index.data(MemberIsOwnerRole).toBool();
    const bool admin = index.data(MemberIsAdminRole).toBool();
    const bool self = memberId == (option.widget ? option.widget->property("currentUserId").toString() : QString());
    const QPixmap avatar = index.data(MemberAvatarRole).value<QPixmap>();
    const qint64 mutedUntil = index.data(MemberMutedUntilRole).toLongLong();
    const bool muted = mutedUntil > QDateTime::currentMSecsSinceEpoch();

    paintAvatar(painter, avatarRect, memberName.isEmpty() ? memberId : memberName, avatar);

    const int textX = avatarRect.right() + 10;
    const int textRight = option.rect.right() - rightMargin;
    const int nameY = option.rect.top() + 10;
    const int statusY = option.rect.top() + 32;

    QFont nameFont = QApplication::font();
    nameFont.setPixelSize(14);
    nameFont.setBold(true);
    QFontMetrics nameFm(nameFont);

    const int statusWidth = QFontMetrics(QApplication::font()).horizontalAdvance(online ? QStringLiteral("在线") : QStringLiteral("离线")) + 8;
    const int roleWidth = roleText.isEmpty() ? 0 : QFontMetrics(QApplication::font()).horizontalAdvance(roleText) + 18;
    const int availableNameWidth = qMax(0, textRight - textX - statusWidth - roleWidth - 8);

    painter->setFont(nameFont);
    painter->setPen(dark ? QColor("#e8e8e8") : textColor(true));
    const QString elidedName = nameFm.elidedText(memberName.isEmpty() ? memberId : memberName,
                                                  Qt::ElideRight, availableNameWidth);
    painter->drawText(QRect(textX, nameY, availableNameWidth, 20), Qt::AlignLeft | Qt::AlignVCenter, elidedName);

    int badgeX = textX + nameFm.horizontalAdvance(elidedName) + 8;
    if (!roleText.isEmpty()) {
        QFont roleFont = QApplication::font();
        roleFont.setPixelSize(11);
        roleFont.setBold(true);
        QFontMetrics roleFm(roleFont);
        const int bw = roleFm.horizontalAdvance(roleText) + 12;
        const QRect badgeRect(badgeX, nameY + 2, bw, kBadgeHeight);
        painter->setPen(Qt::NoPen);
        painter->setBrush(roleBadgeColor(owner, admin, self));
        painter->drawRoundedRect(badgeRect, kBadgeHeight / 2, kBadgeHeight / 2);
        painter->setFont(roleFont);
        painter->setPen(Qt::white);
        painter->drawText(badgeRect, Qt::AlignCenter, roleText);
        badgeX += bw + 6;
    }

    QFont statusFont = QApplication::font();
    statusFont.setPixelSize(12);
    QFontMetrics statusFm(statusFont);
    painter->setFont(statusFont);
    painter->setPen(dark ? QColor("#a8a8a8") : textColor(false));
    painter->drawText(QRect(textRight - statusWidth, nameY, statusWidth, 18), Qt::AlignRight | Qt::AlignVCenter,
                      online ? QStringLiteral("在线") : QStringLiteral("离线"));

    const QString subText = muted
        ? QStringLiteral("QQ:%1 · 禁言至 %2").arg(memberId, QDateTime::fromMSecsSinceEpoch(mutedUntil).toString(QStringLiteral("MM-dd hh:mm")))
        : QStringLiteral("QQ:%1").arg(memberId);
    painter->drawText(QRect(textX, statusY, textRight - textX, 18), Qt::AlignLeft | Qt::AlignVCenter, subText);

    painter->restore();
}
