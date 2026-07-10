#ifndef FORWARDWINDOW_H
#define FORWARDWINDOW_H

#include <QDialog>
#include <QTabWidget>

class QListView;
class QStandardItemModel;
class QLineEdit;
class QLabel;
class QPushButton;

class ForwardWindow : public QDialog {
    Q_OBJECT

public:
    explicit ForwardWindow(QWidget* parent = nullptr);

    void setContacts(const QStringList& contactIds, const QMap<QString, QString>& contactNames);
    void setGroups(const QStringList& groupIds, const QMap<QString, QString>& groupNames);

signals:
    void forwardToContactRequested(const QString& userId);
    void forwardToGroupRequested(const QString& groupId);

private:
    void setupUi();
    void updateStyle();

    QLineEdit* m_searchEdit = nullptr;
    QListView* m_contactList = nullptr;
    QListView* m_groupList = nullptr;
    QStandardItemModel* m_contactModel = nullptr;
    QStandardItemModel* m_groupModel = nullptr;
    QTabWidget* m_tabWidget = nullptr;
    QPushButton* m_forwardBtn = nullptr;
};

#endif // FORWARDWINDOW_H



