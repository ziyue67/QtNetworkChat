#ifndef FRIENDMANAGERDIALOG_H
#define FRIENDMANAGERDIALOG_H

#include <QDialog>

class QListView;
class QStandardItemModel;
class QPushButton;
class QLineEdit;

class FriendManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit FriendManagerDialog(QWidget* parent = nullptr);

    void setFriendList(const QStringList& friendIds, const QMap<QString, QString>& friendNames);

signals:
    void deleteFriendRequested(const QString& userId);
    void addFriendRequested();

private:
    void setupUi();
    void updateStyle();

    QListView* m_listView = nullptr;
    QStandardItemModel* m_model = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_addBtn = nullptr;
};

#endif // FRIENDMANAGERDIALOG_H

