#ifndef FRIENDMANAGERDIALOG_H
#define FRIENDMANAGERDIALOG_H

#include <QDialog>
#include <QMap>
#include <QString>
#include <QStringList>

class QListWidget;
class QTableWidget;
class QCheckBox;
class QPushButton;
class QLineEdit;
class QLabel;

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
    void rebuildTable();
    void updateSelectAllState();

    // Left group rail
    QListWidget* m_groupList = nullptr;

    // Right table
    QLabel* m_titleLabel = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QCheckBox* m_selectAll = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_addBtn = nullptr;

    QStringList m_friendIds;
    QMap<QString, QString> m_friendNames;
    QString m_filter;
};

#endif // FRIENDMANAGERDIALOG_H
