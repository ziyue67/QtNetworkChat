#ifndef CONTACTSVIEW_H
#define CONTACTSVIEW_H

#include <QWidget>

class QLineEdit;
class QTabWidget;
class QListView;
class QStandardItemModel;

class ContactsView : public QWidget {
    Q_OBJECT

public:
    explicit ContactsView(QWidget* parent = nullptr);

    QLineEdit* searchEdit() const;
    QListView* friendListView() const;
    QListView* groupListView() const;

    QStandardItemModel* friendModel() const;
    QStandardItemModel* groupModel() const;

signals:
    void friendSelected(const QString& userId);
    void groupSelected(const QString& groupId);

private:
    void setupUi();
    void updateStyle();

    QLineEdit* m_searchEdit = nullptr;
    QTabWidget* m_tabWidget = nullptr;
    QListView* m_friendListView = nullptr;
    QListView* m_groupListView = nullptr;
    QStandardItemModel* m_friendModel = nullptr;
    QStandardItemModel* m_groupModel = nullptr;
};

#endif // CONTACTSVIEW_H
