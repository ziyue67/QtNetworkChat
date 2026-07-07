#include "chatbubbledelegate.h"
#include <QFontMetrics>
#include <QIcon>
#include <QPainterPath>
#include <QDateTime>
#include <QCryptographicHash>
#include <QLinearGradient>
#include <QApplication>

// ============================================================================
// Forward declarations for helper functions in anonymous namespace
// ============================================================================
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

QIcon generatedAvatarIcon(const QString& displayName, const QString& seedId, int side) {
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
    return QIcon(pixmap);
}

// Color scheme for dark theme bubbles
struct BubbleColors {
    QColor bg;
    QColor text;
    QColor timestamp;
    QColor readStatus;
    QColor link;

    static BubbleColors sent() {
        // Gradient-like accent purple bubbles (matches: --accent-primary: #5E5CE6)
        return { QColor(94, 92, 230),       // bg - accent purple
                 QColor(255, 255, 255),      // text - white
                 QColor(200, 200, 255, 180),  // timestamp - light purple
                 QColor(180, 230, 255),      // readStatus - cyan tint
                 QColor(220, 220, 255) };    // link
    }

    static BubbleColors received() {
        return { QColor(30, 30, 38),         // bg - dark surface (--bg-tertiary: #1C1C22)
                 QColor(245, 245, 247),       // text - primary
                 QColor(140, 140, 150, 180),  // timestamp - muted
                 QColor(140, 140, 150),       // readStatus - muted (unused for received)
                 QColor(94, 92, 230) };       // link - accent
    }

    static QColor systemBg() {
        return QColor(30, 30, 38);
    }
    static QColor systemText() {
        return QColor(110, 110, 115);
    }
    static QColor quoteBar() {
        return QColor(94, 92, 230, 120);
    }
    static QColor quoteText() {
        return QColor(160, 160, 170);
    }
    static QColor forwardLabel() {
        return QColor(94, 92, 230, 200);
    }
};

} // anonymous namespace

// ============================================================================
// ChatBubbleDelegate implementation
// ============================================================================

ChatBubbleDelegate::ChatBubbleDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {
}

QSize ChatBubbleDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
    const bool system = index.data(ChatBubbleSystemRole).toBool();
    const int width = qMax(360, option.rect.width() > 0 ? option.rect.width() : 640);
    QFontMetrics fm(option.font);

    const int maxTextWidth = system
        ? width - 80
        : qMin(kMaxBubbleWidthGlobal, qMax(kMinBubbleWidth, width - 156));

    const QString mediaKind = index.data(ChatBubbleMediaKindRole).toString();
    const QVariant mediaPreviewData = index.data(ChatBubbleMediaPreviewRole);
    const bool hasImagePreview = mediaKind == QLatin1String("image")
        && mediaPreviewData.canConvert<QPixmap>()
        && !mediaPreviewData.value<QPixmap>().isNull();

    const QString text = index.data(Qt::DisplayRole).toString();
    const QRect textBounds = fm.boundingRect(QRect(0, 0, maxTextWidth, 1000),
                                             Qt::TextWordWrap, text);

    // Extra height for label overlays
    int extraHeight = 0;
    if (index.data(ChatBubbleForwardedRole).toBool()) {
        extraHeight += kForwardLabelHeight + 2;
    }
    if (!index.data(ChatBubbleQuotedTextRole).toString().isEmpty()) {
        QFont smallFont = option.font;
        smallFont.setPointSize(qMax(8, option.font.pointSize() - 1));
        QFontMetrics smallFm(smallFont);
        int quoteWidth = maxTextWidth - 24 - kQuoteBarWidth - 4;
        QString elided = smallFm.elidedText(
            index.data(ChatBubbleQuotedTextRole).toString(),
            Qt::ElideRight, quoteWidth);
        Q_UNUSED(elided);
        extraHeight += qMax(20, smallFm.height() * 2) + 4; // quoted area + top spacing
    }

    const int timestampHeight = index.data(ChatBubbleTimestampRole).toString().isEmpty()
        ? 0 : kTimestampHeight;

    if (hasImagePreview) {
        QSize mediaSize = mediaPreviewData.value<QPixmap>().size();
        mediaSize.scale(qMin(kMaxPreviewWidth, maxTextWidth), kMaxPreviewHeight, Qt::KeepAspectRatio);
        return QSize(width, qMax(120, mediaSize.height() + textBounds.height() + 44
                                        + extraHeight + timestampHeight));
    }

    // For grouped messages, use tighter spacing
    const int groupedAdjust = isMessageGrouped(index) ? -10 : 0;
    return QSize(width, qMax(system ? 42 : 58,
                             textBounds.height() + (system ? 22 : 30)
                             + extraHeight + timestampHeight + groupedAdjust));
}

void ChatBubbleDelegate::paint(QPainter* painter,
                               const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QString text = index.data(Qt::DisplayRole).toString();
    const bool outgoing = index.data(ChatBubbleOutgoingRole).toBool();
    const bool system = index.data(ChatBubbleSystemRole).toBool();

    const QRect rect = option.rect.adjusted(10, kTopInset, -10, -kBottomInset);

    if (system) {
        QFontMetrics fm = painter->fontMetrics();
        paintSystemMessage(painter, rect, text, fm);
        painter->restore();
        return;
    }

    paintUserMessage(painter, rect, index, outgoing, false);
    painter->restore();
}

void ChatBubbleDelegate::paintSystemMessage(QPainter* painter,
                                            const QRect& rect,
                                            const QString& text,
                                            QFontMetrics& fm) const {
    const int maxWidth = qMin(rect.width() - 40, 620);
    const QRect textRect = fm.boundingRect(QRect(0, 0, maxWidth, 1000),
                                           Qt::TextWordWrap, text);
    const QRect bubble(QPoint(rect.center().x() - textRect.width() / 2 - 14, rect.top() + 5),
                       QSize(textRect.width() + 28, textRect.height() + 14));

    // System bubble: semi-transparent dark bg, centered
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(30, 30, 38));
    QPainterPath bubblePath;
    bubblePath.addRoundedRect(QRectF(bubble), kSmallBubbleRadius, kSmallBubbleRadius);
    painter->drawPath(bubblePath);

    painter->setPen(QColor(110, 110, 115));
    QFont f = painter->font();
    f.setPointSize(qMax(8, f.pointSize() - 1));
    painter->setFont(f);
    painter->drawText(bubble.adjusted(14, 7, -14, -7),
                      Qt::TextWordWrap | Qt::AlignCenter, text);
}

void ChatBubbleDelegate::paintUserMessage(QPainter* painter,
                                          const QRect& rect,
                                          const QModelIndex& index,
                                          bool outgoing,
                                          bool hasImagePreview) const {
    const QString mediaKind = index.data(ChatBubbleMediaKindRole).toString();
    hasImagePreview = mediaKind == QLatin1String("image")
        && index.data(ChatBubbleMediaPreviewRole).canConvert<QPixmap>()
        && !index.data(ChatBubbleMediaPreviewRole).value<QPixmap>().isNull();

    const bool grouped = isMessageGrouped(index);

    // Avatar positioning
    const int avatarSize = grouped ? 0 : kAvatarSize;
    const int sideInset = kSideInset;
    const int avatarX = outgoing
        ? rect.right() - avatarSize - sideInset
        : rect.left() + sideInset;

    QRect avatarRect;
    if (!grouped) {
        avatarRect = QRect(avatarX, rect.top() + 8, avatarSize, avatarSize);
        paintAvatar(painter, avatarRect, index);
    }

    // Bubble positioning
    const int maxBubbleWidth = qMin(kMaxBubbleWidthGlobal,
                                    qMax(kMinBubbleWidth,
                                         rect.width() - (grouped ? 48 : avatarSize + 88)));
    QSize bubbleSize;
    QRect textBounds;
    QPixmap mediaPreview;
    QSize mediaSize;

    QFontMetrics fm(painter->font());
    const QString text = index.data(Qt::DisplayRole).toString();

    // Calculate extra overlay heights
    int overlayHeight = 0;
    int quoteHeight = 0;
    int forwardHeight = 0;

    // Forwarded label
    if (index.data(ChatBubbleForwardedRole).toBool()) {
        forwardHeight = kForwardLabelHeight + 4;
        overlayHeight += forwardHeight;
    }

    // Quoted text
    const QString quotedText = index.data(ChatBubbleQuotedTextRole).toString();
    if (!quotedText.isEmpty()) {
        QFont smallFont = painter->font();
        smallFont.setPointSize(qMax(8, painter->font().pointSize() - 1));
        QFontMetrics smallFm(smallFont);
        int quoteWidth = maxBubbleWidth - 24 - kQuoteBarWidth - 4;
        QString elided = smallFm.elidedText(quotedText, Qt::ElideRight, quoteWidth);
        int lines = qMin(2, qMax(1, smallFm.horizontalAdvance(elided) > quoteWidth ? 2 : 1));
        quoteHeight = smallFm.height() * lines + 8 + 4; // border + padding + spacing
        overlayHeight += quoteHeight;
    }

    // Timestamp
    const QString timestamp = index.data(ChatBubbleTimestampRole).toString();
    const int timestampHeight = timestamp.isEmpty() ? 0 : kTimestampHeight;
    overlayHeight += timestampHeight;

    if (hasImagePreview) {
        mediaPreview = index.data(ChatBubbleMediaPreviewRole).value<QPixmap>();
        mediaSize = mediaPreview.size();
        const int maxPreviewWidth = qMin(kMaxPreviewWidth, maxBubbleWidth - 24);
        const int maxPreviewHeight = kMaxPreviewHeight;
        mediaSize.scale(maxPreviewWidth, maxPreviewHeight, Qt::KeepAspectRatio);
        textBounds = fm.boundingRect(QRect(0, 0, maxPreviewWidth, 1000),
                                     Qt::TextWordWrap, text);
        bubbleSize = QSize(qMax(mediaSize.width(), textBounds.width()) + 24,
                           mediaSize.height() + textBounds.height() + 30 + overlayHeight);
    } else {
        textBounds = fm.boundingRect(QRect(0, 0, maxBubbleWidth, 1000),
                                     Qt::TextWordWrap, text);
        bubbleSize = QSize(textBounds.width() + 28,
                           textBounds.height() + 20 + overlayHeight);
    }

    const int bubbleX = outgoing
        ? (grouped ? rect.right() - bubbleSize.width() - sideInset
                   : avatarRect.left() - 10 - bubbleSize.width())
        : (grouped ? rect.left() + sideInset
                   : avatarRect.right() + 10);

    const QRect bubbleRect(QPoint(bubbleX, rect.top() + 6), bubbleSize);

    // Draw the bubble
    BubbleColors colors = outgoing ? BubbleColors::sent() : BubbleColors::received();

    QPainterPath bubblePath;
    bubblePath.addRoundedRect(QRectF(bubbleRect), kBubbleRadius, kBubbleRadius);
    painter->setPen(Qt::NoPen);
    painter->setBrush(colors.bg);
    painter->drawPath(bubblePath);

    // Draw the tail (small triangle) for non-grouped messages
    if (!grouped) {
        QPainterPath tailPath;
        int tailY = rect.top() + 20;
        if (outgoing) {
            tailPath.moveTo(bubbleRect.right(), tailY + 4);
            tailPath.lineTo(bubbleRect.right() + 8, tailY + 2);
            tailPath.lineTo(bubbleRect.right(), tailY + 12);
        } else {
            tailPath.moveTo(bubbleRect.left(), tailY + 4);
            tailPath.lineTo(bubbleRect.left() - 8, tailY + 2);
            tailPath.lineTo(bubbleRect.left(), tailY + 12);
        }
        tailPath.closeSubpath();
        painter->setBrush(colors.bg);
        painter->drawPath(tailPath);
    }

    // Content rendering starts here
    int contentY = bubbleRect.top() + 8;

    // Forwarded label
    if (index.data(ChatBubbleForwardedRole).toBool()) {
        QRect forwardRect(bubbleRect.left() + 14, contentY,
                          bubbleRect.width() - 28, kForwardLabelHeight);
        paintForwardedLabel(painter, forwardRect, &contentY);
    }

    // Quoted text
    if (!quotedText.isEmpty()) {
        QRect quoteRect(bubbleRect.left() + 14, contentY,
                        bubbleRect.width() - 28, quoteHeight - 4);
        paintQuotedText(painter, quoteRect, quotedText, outgoing, &contentY);
    }

    // Message text / image
    painter->setPen(colors.text);
    if (hasImagePreview) {
        paintImagePreview(painter, bubbleRect, index, text,
                          mediaPreview, mediaSize, outgoing);
    } else {
        paintTextOnly(painter, bubbleRect, text, QPixmap(), outgoing, index);
    }

    // Timestamp and read status (bottom row of bubble)
    if (!timestamp.isEmpty()) {
        int tsY = bubbleRect.bottom() - kTimestampHeight - 6;
        QRect tsArea(bubbleRect.left() + 14, tsY,
                     bubbleRect.width() - 28 - (outgoing ? 22 : 0),
                     kTimestampHeight);
        paintTimestamp(painter, tsArea, timestamp, outgoing);

        if (outgoing) {
            QString readStatus = index.data(ChatBubbleReadStatusRole).toString();
            if (readStatus.isEmpty()) {
                readStatus = QStringLiteral("✓"); // single check as default
            }
            QRect rsArea(bubbleRect.right() - 24, tsY, 22, kTimestampHeight);
            paintReadStatus(painter, rsArea, readStatus);
        }
    }
}

void ChatBubbleDelegate::paintImagePreview(QPainter* painter,
                                           const QRect& bubbleRect,
                                           const QModelIndex& index,
                                           const QString& text,
                                           const QPixmap& preview,
                                           const QSize& mediaSize,
                                           bool outgoing) const {
    BubbleColors colors = outgoing ? BubbleColors::sent() : BubbleColors::received();

    const QRect imageRect(bubbleRect.left() + 12,
                          bubbleRect.top() + 12,
                          mediaSize.width(),
                          mediaSize.height());

    // Clip to rounded rect for the image
    QPainterPath clipPath;
    clipPath.addRoundedRect(QRectF(imageRect), 8, 8);
    painter->save();
    painter->setClipPath(clipPath);
    painter->drawPixmap(imageRect, preview);
    painter->restore();

    const int textY = imageRect.bottom() + 8;
    QFontMetrics fm(painter->font());
    QRect textRect(bubbleRect.left() + 12, textY,
                   bubbleRect.width() - 24,
                   fm.boundingRect(QRect(0, 0, bubbleRect.width() - 24, 1000),
                                   Qt::TextWordWrap, text).height() + 4);
    painter->setPen(colors.text);
    painter->drawText(textRect, Qt::TextWordWrap, text);
}

void ChatBubbleDelegate::paintTextOnly(QPainter* painter,
                                       const QRect& bubbleRect,
                                       const QString& text,
                                       const QPixmap& preview,
                                       bool outgoing,
                                       const QModelIndex& index) const {
    BubbleColors colors = outgoing ? BubbleColors::sent() : BubbleColors::received();

    // Account for overlays
    int overlayOffset = 0;
    if (index.data(ChatBubbleForwardedRole).toBool()) {
        overlayOffset += kForwardLabelHeight + 4;
    }
    const QString quotedText = index.data(ChatBubbleQuotedTextRole).toString();
    if (!quotedText.isEmpty()) {
        QFont smallFont = painter->font();
        smallFont.setPointSize(qMax(8, painter->font().pointSize() - 1));
        QFontMetrics smallFm(smallFont);
        overlayOffset += smallFm.height() * 2 + 8 + 4;
    }
    const QString ts = index.data(ChatBubbleTimestampRole).toString();
    const int tsHeight = ts.isEmpty() ? 0 : kTimestampHeight;

    painter->setPen(colors.text);
    QRect textRect(bubbleRect.left() + 14,
                   bubbleRect.top() + 10 + overlayOffset,
                   bubbleRect.width() - 28,
                   bubbleRect.height() - 20 - overlayOffset - tsHeight);
    painter->drawText(textRect, Qt::TextWordWrap, text);
}

void ChatBubbleDelegate::paintAvatar(QPainter* painter,
                                     const QRect& avatarRect,
                                     const QModelIndex& index) const {
    QPixmap avatar;
    const QString avatarPath = index.data(ChatBubbleAvatarPathRole).toString();
    if (!avatarPath.isEmpty()) {
        avatar.load(avatarPath);
    }
    if (avatar.isNull()) {
        QIcon genIcon = generatedAvatarIcon(
            index.data(ChatBubbleSenderNameRole).toString(),
            index.data(ChatBubbleSenderIdRole).toString(),
            kAvatarSize);
        avatar = genIcon.pixmap(kAvatarSize, kAvatarSize);
    } else {
        avatar = roundAvatarPixmap(avatar, kAvatarSize);
    }
    painter->drawPixmap(avatarRect, avatar);
}

void ChatBubbleDelegate::paintTimestamp(QPainter* painter,
                                        const QRect& area,
                                        const QString& timestamp,
                                        bool outgoing) const {
    BubbleColors colors = outgoing ? BubbleColors::sent() : BubbleColors::received();
    painter->save();
    QFont f = painter->font();
    f.setPointSize(qMax(9, f.pointSize() - 2));
    f.setBold(false);
    painter->setFont(f);
    painter->setPen(colors.timestamp);
    painter->drawText(area, Qt::AlignLeft | Qt::AlignVCenter, timestamp);
    painter->restore();
}

void ChatBubbleDelegate::paintReadStatus(QPainter* painter,
                                         const QRect& area,
                                         const QString& readStatus) const {
    painter->save();
    QFont f = painter->font();
    f.setPointSize(qMax(9, f.pointSize() - 2));
    f.setBold(false);
    painter->setFont(f);

    // Blue check mark for read status
    QColor statusColor;
    if (readStatus == QStringLiteral("✓✓")) {
        statusColor = QColor(100, 200, 255);  // cyan - read (double check)
    } else if (readStatus == QStringLiteral("✓")) {
        statusColor = QColor(160, 160, 170);   // gray - delivered (single check)
    } else {
        statusColor = QColor(160, 160, 170);
    }
    painter->setPen(statusColor);
    painter->drawText(area, Qt::AlignRight | Qt::AlignVCenter, readStatus);
    painter->restore();
}

void ChatBubbleDelegate::paintQuotedText(QPainter* painter,
                                         const QRect& area,
                                         const QString& quotedText,
                                         bool outgoing,
                                         int* consumedHeight) const {
    Q_UNUSED(outgoing);
    painter->save();

    QFont f = painter->font();
    f.setPointSize(qMax(9, f.pointSize() - 1));
    painter->setFont(f);
    QFontMetrics fm(f);

    // Quote bar (left border)
    QRect barRect(area.left(), area.top() + 2,
                  kQuoteBarWidth, area.height() - 4);
    painter->setPen(Qt::NoPen);
    painter->setBrush(BubbleColors::quoteBar());
    QPainterPath barPath;
    barPath.addRoundedRect(QRectF(barRect), 1.5, 1.5);
    painter->drawPath(barPath);

    // Quote text
    painter->setPen(BubbleColors::quoteText());
    int textX = barRect.right() + 4;
    int textWidth = area.right() - textX;
    if (textWidth > 0) {
        QRect textRect(textX, area.top(), textWidth, area.height());
        QString elided = fm.elidedText(quotedText, Qt::ElideRight, textWidth);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elided);
    }

    painter->restore();
    if (consumedHeight) {
        *consumedHeight += area.height() + 4;
    }
}

void ChatBubbleDelegate::paintForwardedLabel(QPainter* painter,
                                             const QRect& area,
                                             int* consumedHeight) const {
    painter->save();

    QFont f = painter->font();
    f.setPointSize(qMax(9, f.pointSize() - 1));
    f.setItalic(true);
    painter->setFont(f);
    painter->setPen(BubbleColors::forwardLabel());
    painter->drawText(area, Qt::AlignLeft | Qt::AlignVCenter,
                      QStringLiteral("⇄ Forwarded"));

    painter->restore();
    if (consumedHeight) {
        *consumedHeight += area.height() + 2;
    }
}

bool ChatBubbleDelegate::isMessageGrouped(const QModelIndex& index) const {
    return index.data(ChatBubbleIsGroupedRole).toBool();
}

int ChatBubbleDelegate::maxBubbleWidth(const QRect& rect, bool hasAvatar) const {
    return qMin(kMaxBubbleWidthGlobal,
                qMax(kMinBubbleWidth,
                     rect.width() - (hasAvatar ? kAvatarSize + 88 : 48)));
}

QRect ChatBubbleDelegate::contentRect(const QModelIndex& index) const {
    // Not used directly; we work with the rect passed into paint()
    Q_UNUSED(index);
    return QRect();
}
