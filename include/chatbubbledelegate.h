#ifndef CHATBUBBLEDELEGATE_H
#define CHATBUBBLEDELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QModelIndex>
#include <QStyleOptionViewItem>
#include <QSize>
#include <QPixmap>

// Custom data roles for chat bubble items
enum ChatBubbleRole {
    ChatBubbleSenderIdRole = Qt::UserRole + 900,
    ChatBubbleSenderNameRole,
    ChatBubbleAvatarPathRole,
    ChatBubbleOutgoingRole,
    ChatBubbleSystemRole,
    ChatBubbleMediaKindRole,
    ChatBubbleMediaPreviewRole,
    ChatBubbleMediaOpenPathRole,

    // New enhanced roles
    ChatBubbleTimestampRole,          // QString - formatted timestamp (e.g. "14:23")
    ChatBubbleReadStatusRole,         // QString - read status ("✓" or "✓✓")
    ChatBubbleQuotedTextRole,         // QString - quoted/replied message text
    ChatBubbleForwardedRole,          // bool - whether message is forwarded
    ChatBubbleIsGroupedRole           // bool - whether this message is grouped (no avatar, tighter)
};

struct BubbleColors {
    QColor bg;
    QColor text;
    QColor timestamp;
    QColor readStatus;
    QColor link;
    QColor quoteBar;
    QColor quoteText;
    QColor forwardLabel;
    static BubbleColors sent(bool dark);
    static BubbleColors received(bool dark);
    static QColor systemBg(bool dark);
    static QColor systemText(bool dark);
};

class ChatBubbleDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ChatBubbleDelegate(QObject* parent = nullptr);

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

private:
    void paintSystemMessage(QPainter* painter, const QRect& rect,
                            const QString& text, QFontMetrics& fm,
                            bool dark) const;

    void paintUserMessage(QPainter* painter, const QRect& rect,
                          const QModelIndex& index, bool outgoing,
                          bool hasImagePreview, const QString& text, bool dark) const;

    void paintImagePreview(QPainter* painter, const QRect& bubbleRect,
                           int contentY, const QModelIndex& index, const QString& text,
                           const QPixmap& preview, const QSize& mediaSize,
                           bool outgoing, bool dark) const;

    void paintTextOnly(QPainter* painter, const QRect& bubbleRect,
                       const QString& text, const QPixmap& preview,
                       bool outgoing, const QModelIndex& index,
                       bool dark) const;

    void paintAvatar(QPainter* painter, const QRect& avatarRect,
                     const QModelIndex& index) const;

    void paintTimestamp(QPainter* painter, const QRect& area,
                        const QString& timestamp, const BubbleColors& colors) const;

    void paintReadStatus(QPainter* painter, const QRect& area,
                         const QString& readStatus, const BubbleColors& colors) const;

    void paintQuotedText(QPainter* painter, const QRect& bubbleRect,
                         const QString& quotedText, const BubbleColors& colors,
                         int* consumedHeight) const;

    void paintForwardedLabel(QPainter* painter, const QRect& area,
                             const QColor& color, int* consumedHeight) const;

    // Layout helpers
    bool isMessageGrouped(const QModelIndex& index) const;
    int maxBubbleWidth(const QRect& rect, bool hasAvatar) const;
    QRect contentRect(const QModelIndex& index) const;

    // Metrics
    static constexpr int kAvatarSize = 38;
    static constexpr int kBubbleRadius = 14;
    static constexpr int kSmallBubbleRadius = 10;
    static constexpr int kSideInset = 14;
    static constexpr int kTopInset = 4;
    static constexpr int kBottomInset = 4;
    static constexpr int kSpacing = 8;
    static constexpr int kGroupedSpacing = 2;
    static constexpr int kTimestampHeight = 16;
    static constexpr int kReadStatusWidth = 24;
    static constexpr int kQuoteBarWidth = 3;
    static constexpr int kForwardLabelHeight = 18;
    static constexpr int kMaxBubbleWidthGlobal = 560;
    static constexpr int kMinBubbleWidth = 250;
    static constexpr int kMaxPreviewWidth = 300;
    static constexpr int kMaxPreviewHeight = 220;
};

#endif // CHATBUBBLEDELEGATE_H
