#ifndef GROUPMEMBERITEMDELEGATE_H
#define GROUPMEMBERITEMDELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QModelIndex>
#include <QStyleOptionViewItem>
#include <QSize>

enum GroupMemberRole {
    MemberIdRole = Qt::UserRole + 700,
    MemberNameRole,
    MemberRoleTextRole,
    MemberOnlineRole,
    MemberIsOwnerRole,
    MemberIsAdminRole,
    MemberIsFriendRole,
    MemberIsPendingRole,
    MemberAvatarRole,
    MemberMutedUntilRole
};

class GroupMemberItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit GroupMemberItemDelegate(QObject* parent = nullptr);

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

private:
    void paintAvatar(QPainter* painter, const QRect& rect,
                     const QString& name, const QPixmap& avatar) const;
    bool isDarkTheme(const QStyleOptionViewItem& option) const;
    QColor textColor(bool primary) const;
    QColor roleBadgeColor(bool owner, bool admin, bool self) const;

    static constexpr int kAvatarSize = 34;
    static constexpr int kRowHeight = 56;
    static constexpr int kBadgeHeight = 18;
};

#endif // GROUPMEMBERITEMDELEGATE_H
