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

// QQNT friend manager. Left rail lists 全部好友 + groups (with add/delete group
// buttons); right side is a grouped/filterable friend table. Friend groups are
// a local-only concept persisted by MainWindow, so the dialog only emits intent
// signals and MainWindow does the persistence + re-feeds via setFriendList.
class FriendManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit FriendManagerDialog(QWidget* parent = nullptr);

    // friendGroups: friendId -> group name (missing => 我的好友).
    // customGroups: extra (possibly empty) group names to show in the rail.
    void setFriendList(const QStringList& friendIds,
                       const QMap<QString, QString>& friendNames,
                       const QMap<QString, QString>& friendGroups = {},
                       const QStringList& customGroups = {});

signals:
    void deleteFriendRequested(const QString& userId);
    void addFriendRequested();
    void createGroupRequested(const QString& groupName);
    void deleteGroupRequested(const QString& groupName);
    void moveFriendToGroupRequested(const QString& userId, const QString& groupName);

private:
    void setupUi();
    void updateStyle();
    void rebuildGroupRail();
    void rebuildTable();
    void updateSelectAllState();
    QString currentGroup() const;

    // Left group rail
    QListWidget* m_groupList = nullptr;
    QPushButton* m_addGroupBtn = nullptr;
    QPushButton* m_deleteGroupBtn = nullptr;

    // Right table
    QLabel* m_titleLabel = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QCheckBox* m_selectAll = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_addBtn = nullptr;

    QStringList m_friendIds;
    QMap<QString, QString> m_friendNames;
    QMap<QString, QString> m_friendGroups;
    QStringList m_customGroups;
    QString m_filter;
};

#endif // FRIENDMANAGERDIALOG_H
