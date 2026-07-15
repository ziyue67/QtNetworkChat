#ifndef GLOBALSEARCHDIALOG_H
#define GLOBALSEARCHDIALOG_H

#include <QDialog>
#include <QList>

class QLineEdit;
class QLabel;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QFrame;
class AvatarLabel;

class GlobalSearchDialog : public QDialog {
    Q_OBJECT

public:
    explicit GlobalSearchDialog(QWidget* parent = nullptr);

    QString searchText() const;

signals:
    void searchRequested(const QString& text);
    void resultActivated(const QString& type, const QString& id);

public slots:
    void addResult(const QString& type, const QString& id, const QString& title, const QString& subtitle);
    void clearResults();

private:
    void setupUi();
    void updateStyle();
    void setActiveTab(int tab);
    void refreshVisibility();
    void showGroupDetail(QListWidgetItem* item);
    void hideGroupDetail();

    enum Tab { TabAll = 0, TabUsers, TabGroups, TabMiniPrograms, TabBots };

    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_searchBtn = nullptr;

    QList<QPushButton*> m_tabButtons;
    int m_activeTab = TabAll;

    QStackedWidget* m_bodyStack = nullptr;   // 0 = results, 1 = placeholder
    QLabel* m_usersHeading = nullptr;
    QListWidget* m_usersList = nullptr;
    QLabel* m_groupsHeading = nullptr;
    QListWidget* m_groupsList = nullptr;
    QLabel* m_placeholderLabel = nullptr;

    // Group detail side panel
    QFrame* m_detailPanel = nullptr;
    AvatarLabel* m_detailAvatar = nullptr;
    QLabel* m_detailName = nullptr;
    QLabel* m_detailMeta = nullptr;
    QString m_detailGroupId;
};

#endif // GLOBALSEARCHDIALOG_H
