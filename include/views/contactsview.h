#ifndef CONTACTSVIEW_H
#define CONTACTSVIEW_H

#include "widgets/contactlistwidget.h"

#include <QMap>
#include <QWidget>

class QLineEdit;
class QTabWidget;
class QListView;
class QStandardItemModel;
class ContactListWidget;
class ContactCard;
class ContactNoticePanel;

class ContactsView : public QWidget {
    Q_OBJECT

public:
    explicit ContactsView(QWidget* parent = nullptr);

    QLineEdit* searchEdit() const;
    QListView* friendListView() const;
    QListView* groupListView() const;
    ContactListWidget* contactList() const;

    QStandardItemModel* friendModel() const;
    QStandardItemModel* groupModel() const;

    void setFriends(const QList<ContactDisplayData>& contacts);
    void setGroups(const QList<ContactDisplayData>& groups);

signals:
    void friendSelected(const QString& userId);
    void groupSelected(const QString& groupId);
    void addFriendRequested();
    void createGroupRequested();
    void friendManagerRequested();
    void globalSearchRequested();

private:
    void setupUi();
    void updateStyle();
    void onContactSelected(const QString& id, bool isGroup);
    void onShowNoticePanel();

    QLineEdit* m_searchEdit = nullptr;
    QTabWidget* m_tabWidget = nullptr;
    QListView* m_friendListView = nullptr;
    QListView* m_groupListView = nullptr;
    QStandardItemModel* m_friendModel = nullptr;
    QStandardItemModel* m_groupModel = nullptr;

    ContactListWidget* m_contactList = nullptr;
    ContactCard* m_contactCard = nullptr;
    ContactNoticePanel* m_noticePanel = nullptr;

    QMap<QString, ContactDisplayData> m_friendMap;
    QMap<QString, ContactDisplayData> m_groupMap;
};

#endif // CONTACTSVIEW_H
