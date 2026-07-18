#ifndef GLOBALSEARCHDIALOG_H
#define GLOBALSEARCHDIALOG_H

#include <QDialog>
#include <QList>
#include <QMap>

class QLineEdit;
class QLabel;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QFrame;
class AvatarLabel;
class DialogTitleBar;

class GlobalSearchDialog : public QDialog {
    Q_OBJECT

public:
    explicit GlobalSearchDialog(QWidget* parent = nullptr);

    QString searchText() const;
    void setContactGroupMode(bool enabled);
    void setContactKnown(const QString& id, bool known);
    void setGroupJoined(const QString& id, bool joined);
    void setGroupJoinPending(const QString& id, bool pending);
    void setSearching(bool searching);

signals:
    void searchRequested(const QString& text);
    void resultActivated(const QString& type, const QString& id);
    void addFriendRequested(const QString& userId);
    void joinGroupRequested(const QString& groupId);

public slots:
    void addResult(const QString& type, const QString& id, const QString& title,
                   const QString& subtitle, const QString& avatarPath = QString());
    void clearResults();
    void showSearchState(const QString& message);

private:
    void setupUi();
    void updateStyle();
    void setActiveTab(int tab);
    void refreshVisibility();
    void showGroupDetail(QListWidgetItem* item);
    void showUserDetail(QListWidgetItem* item);
    void hideGroupDetail();
    void refreshResultSections();

    enum Tab { TabAll = 0, TabUsers, TabGroups, TabMiniPrograms, TabBots };

    QLineEdit* m_searchEdit = nullptr;
    DialogTitleBar* m_titleBar = nullptr;
    QPushButton* m_searchBtn = nullptr;

    QList<QPushButton*> m_tabButtons;
    int m_activeTab = TabAll;

    QStackedWidget* m_bodyStack = nullptr;   // 0 = results, 1 = placeholder
    QFrame* m_usersSection = nullptr;
    QFrame* m_groupsSection = nullptr;
    QLabel* m_usersHeading = nullptr;
    QListWidget* m_usersList = nullptr;
    QPushButton* m_usersMoreButton = nullptr;
    QLabel* m_groupsHeading = nullptr;
    QListWidget* m_groupsList = nullptr;
    QPushButton* m_groupsMoreButton = nullptr;
    QLabel* m_placeholderLabel = nullptr;

    // Group detail side panel
    QFrame* m_detailPanel = nullptr;
    AvatarLabel* m_detailAvatar = nullptr;
    QLabel* m_detailName = nullptr;
    QLabel* m_detailMeta = nullptr;
    QPushButton* m_detailActionBtn = nullptr;
    QString m_detailGroupId;
    QString m_detailType;
    bool m_contactGroupMode = false;
    QMap<QString, bool> m_knownContacts;
    QMap<QString, bool> m_joinedGroups;
    QMap<QString, bool> m_pendingGroupJoins;
    bool m_searching = false;
    QString m_resultKeyword;
};

#endif // GLOBALSEARCHDIALOG_H
