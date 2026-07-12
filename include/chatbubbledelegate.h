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
    // Original roles (preserved for backward compatibility)
    ChatBubbleSenderIdRole = Qt::UserRole + 900,
    ChatBubbleSenderNameRole,
    ChatBubbleAvatarPathRole,
    ChatBubbleOutgoingRole,
    ChatBubbleSystemRole,
    ChatBubbleMediaKindRole,
    ChatBubbleMediaPreviewRole,
    ChatBubbleMediaOpenPathRole,
    ChatBubbleMediaPreviewUnavailableRole,

    // New enhanced roles
    ChatBubbleTimestampRole,          // QString - formatted timestamp (e.g. "14:23")
    ChatBubbleReadStatusRole,         // QString - read status ("✓" or "✓✓")
    ChatBubbleQuotedTextRole,         // QString - quoted/replied message text
    ChatBubbleForwardedRole,          // bool - whether message is forwarded
    ChatBubbleIsGroupedRole           // bool - whether this message is grouped (no avatar, tighter)
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
    // Helper methods for paint
    void paintSystemMessage(QPainter* painter, const QRect& rect,
                            const QString& text, QFontMetrics& fm) const;

    void paintUserMessage(QPainter* painter, const QRect& rect,
                          const QModelIndex& index, bool outgoing,
                          bool hasImagePreview) const;

    void paintImagePreview(QPainter* painter, const QRect& bubbleRect,
                           const QModelIndex& index, const QString& text,
                           const QPixmap& preview, const QSize& mediaSize,
                           bool outgoing) const;

    void paintImagePlaceholder(QPainter* painter, const QRect& bubbleRect,
                               bool outgoing) const;

    void paintTextOnly(QPainter* painter, const QRect& bubbleRect,
                       const QString& text, const QPixmap& preview,
                       bool outgoing, const QModelIndex& index) const;

    void paintAvatar(QPainter* painter, const QRect& avatarRect,
                     const QModelIndex& index) const;

    void paintTimestamp(QPainter* painter, const QRect& area,
                        const QString& timestamp, bool outgoing) const;

    void paintReadStatus(QPainter* painter, const QRect& area,
                         const QString& readStatus) const;

    void paintQuotedText(QPainter* painter, const QRect& bubbleRect,
                         const QString& quotedText, bool outgoing,
                         int* consumedHeight) const;

    void paintForwardedLabel(QPainter* painter, const QRect& bubbleRect,
                             int* consumedHeight) const;

    // Layout helpers
    bool isMessageGrouped(const QModelIndex& index) const;
    int maxBubbleWidth(const QRect& rect, bool hasAvatar) const;
    QRect contentRect(const QModelIndex& index) const;

    // Metrics
    // Match tauri-qqnt MessageBubble: px-4 outer padding, 36px avatar,
    // 12px avatar gap and rounded-lg (8px) message surface.
    static constexpr int kAvatarSize = 36;
    static constexpr int kBubbleRadius = 8;
    static constexpr int kSmallBubbleRadius = 6;
    // The list view already provides the 16px outer padding. Keep only the
    // remaining native-view inset here so the avatar aligns with QQNT.
    static constexpr int kSideInset = 6;
    static constexpr int kTopInset = 4;
    static constexpr int kBottomInset = 4;
    static constexpr int kSpacing = 12;
    static constexpr int kGroupedSpacing = 2;
    static constexpr int kTimestampHeight = 16;
    static constexpr int kQuoteBarWidth = 3;
    static constexpr int kForwardLabelHeight = 18;
    static constexpr int kMaxBubbleWidthGlobal = 560;
    static constexpr int kMinBubbleWidth = 250;
    static constexpr int kMaxPreviewWidth = 300;
    static constexpr int kMaxPreviewHeight = 220;
};

#endif // CHATBUBBLEDELEGATE_H
