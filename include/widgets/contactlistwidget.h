#ifndef CONTACTLISTWIDGET_H
#define CONTACTLISTWIDGET_H

#include <QAbstractItemDelegate>
#include <QListView>
#include <QSet>
#include <QStandardItemModel>

struct ContactDisplayData {
    QString id;
    QString nickname;
    QString remark;
    QString signature;
    QString group;
    QString category;
    QString avatar;
    QString status = QStringLiteral("offline");
    int memberCount = 0;
    bool isGroup = false;
    bool isTitle = false;
    QString titleText;
    bool isOnline = false;
};
Q_DECLARE_METATYPE(ContactDisplayData)

class ContactListItemDelegate : public QAbstractItemDelegate {
    Q_OBJECT

public:
    explicit ContactListItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

private:
    void drawAvatar(QPainter* painter, const QRect& rect,
                    const ContactDisplayData& data) const;
    void drawTitle(QPainter* painter, const QRect& rect,
                   const QString& title) const;
};

class ContactListWidget : public QListView {
    Q_OBJECT

public:
    explicit ContactListWidget(QWidget* parent = nullptr);

    QStandardItemModel* sourceModel() const { return m_model; }

    void setFriends(const QList<ContactDisplayData>& contacts);
    void setGroups(const QList<ContactDisplayData>& groups);
    void setShowGroups(bool show);
    void setOnlineUsers(const QSet<QString>& onlineIds);

signals:
    void friendSelected(const QString& userId);
    void groupSelected(const QString& groupId);

private:
    void setupModel();
    void updateView();

    QStandardItemModel* m_model = nullptr;
    ContactListItemDelegate* m_delegate = nullptr;
    bool m_showGroups = false;
    QList<ContactDisplayData> m_friends;
    QList<ContactDisplayData> m_groups;
    QSet<QString> m_onlineIds;
};

#endif // CONTACTLISTWIDGET_H
