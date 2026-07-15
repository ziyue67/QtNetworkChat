#ifndef CONTACTSVIEW_H
#define CONTACTSVIEW_H

#include "widgets/contactlistwidget.h"

#include <QMap>
#include <QWidget>

class QAction;
class QLineEdit;
class QMenu;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QListView;
class QStandardItemModel;
class QLabel;
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
    void onShowNoticePanel(bool groupNotice);
    void setDetailMode(bool showCard);

    QLineEdit* m_searchEdit = nullptr;
    QTabWidget* m_tabWidget = nullptr;
    QListView* m_friendListView = nullptr;
    QListView* m_groupListView = nullptr;
    QStandardItemModel* m_friendModel = nullptr;
    QStandardItemModel* m_groupModel = nullptr;

    ContactListWidget* m_contactList = nullptr;
    ContactCard* m_contactCard = nullptr;
    ContactNoticePanel* m_noticePanel = nullptr;
    QStackedWidget* m_detailStack = nullptr;
    QLabel* m_emptyLabel = nullptr;

    QPushButton* m_plusButton = nullptr;
    QMenu* m_plusMenu = nullptr;
    QPushButton* m_friendManagerButton = nullptr;
    QPushButton* m_friendNoticeButton = nullptr;
    QPushButton* m_groupNoticeButton = nullptr;
    QPushButton* m_friendModeButton = nullptr;
    QPushButton* m_groupModeButton = nullptr;

    QMap<QString, ContactDisplayData> m_friendMap;
    QMap<QString, ContactDisplayData> m_groupMap;
};

#endif // CONTACTSVIEW_H