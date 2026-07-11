#ifndef SESSIONITEMDELEGATE_H
#define SESSIONITEMDELEGATE_H

#include <QStyledItemDelegate>

class SessionItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    enum SessionRole {
        SessionIdRole = Qt::UserRole + 100,
        SessionNameRole,
        SessionAvatarPathRole,
        SessionLastMessageRole,
        SessionTimeRole,
        SessionUnreadRole,
        SessionPinnedRole,
        SessionAtMentionRole,
        SessionOnlineRole,
        SessionGroupRole,
        SessionLastMessageTimestampRole
    };

    explicit SessionItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

private:
    QPixmap avatarPixmap(const QModelIndex& index, int size) const;
};

#endif // SESSIONITEMDELEGATE_H
