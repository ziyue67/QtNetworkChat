#ifndef SESSIONITEMDELEGATE_H
#define SESSIONITEMDELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QModelIndex>
#include <QStyleOptionViewItem>
#include <QSize>

enum SessionRole {
    SessionNameRole = Qt::UserRole + 500,
    SessionLastMessageRole,
    SessionTimeRole,
    SessionUnreadRole,
    SessionPinnedRole,
    SessionAvatarRole,
    SessionSenderNameRole,
    SessionIdRole
};

class SessionItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit SessionItemDelegate(QObject* parent = nullptr);

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

private:
    void paintAvatar(QPainter* painter, const QRect& rect, const QString& name, const QPixmap& avatar) const;
    bool isDarkTheme(const QStyleOptionViewItem& option) const;
    QColor textColor(const QStyleOptionViewItem& option, bool primary) const;
    QColor badgeColor() const;

    static constexpr int kAvatarSize = 44;
    static constexpr int kRowHeight = 68;
    static constexpr int kBadgeSize = 18;
};

#endif // SESSIONITEMDELEGATE_H
