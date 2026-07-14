#ifndef GROUPMEMBERSIDEBAR_H
#define GROUPMEMBERSIDEBAR_H

#include <QListView>
#include <QSet>
#include <QStandardItemModel>
#include <QWidget>

struct GroupMemberDisplayData {
    QString id;
    QString nickname;
    QString groupNickname;
    QString avatar;
    QString role; // owner / admin / member
    QString announcement;
    bool isMuted = false;
    bool isOnline = false;
};
Q_DECLARE_METATYPE(GroupMemberDisplayData)

class GroupMemberItemDelegate : public QAbstractItemDelegate {
    Q_OBJECT

public:
    explicit GroupMemberItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};

class QLineEdit;
class QLabel;
class QPushButton;

class GroupMemberSidebar : public QWidget {
    Q_OBJECT

public:
    explicit GroupMemberSidebar(QWidget* parent = nullptr);

    void setGroupId(const QString& groupId);
    void setGroupName(const QString& name);
    void setAnnouncement(const QString& text);
    void setMembers(const QList<GroupMemberDisplayData>& members);
    void setOnlineUsers(const QSet<QString>& onlineIds);

signals:
    void chatWithMember(const QString& userId);
    void atMember(const QString& userId, const QString& displayName);
    void viewProfile(const QString& userId);
    void addFriend(const QString& userId);
    void renameMember(const QString& userId, const QString& currentName);
    void muteMember(const QString& userId, int minutes);
    void unmuteMember(const QString& userId);
    void promoteAdmin(const QString& userId);
    void demoteAdmin(const QString& userId);
    void reportMember(const QString& userId);
    void blockMember(const QString& userId);
    void kickMember(const QString& userId);

private:
    void setupUi();
    void updateStyle();
    void refreshRows();
    void onMemberClicked(const QModelIndex& index);
    void showContextMenu(const QPoint& pos);

    QString m_groupId;
    QString m_groupName;
    QString m_announcement;
    QList<GroupMemberDisplayData> m_members;
    QList<GroupMemberDisplayData> m_filtered;
    QSet<QString> m_onlineIds;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_announcementLabel = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QListView* m_listView = nullptr;
    QStandardItemModel* m_model = nullptr;
    QPushButton* m_closeBtn = nullptr;
};

#endif // GROUPMEMBERSIDEBAR_H
