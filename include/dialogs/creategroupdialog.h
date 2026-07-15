#ifndef CREATEGROUPDIALOG_H
#define CREATEGROUPDIALOG_H

#include <QDialog>
#include <QMap>
#include <QStringList>

class QButtonGroup;
class QCheckBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QScrollArea;

class CreateGroupDialog : public QDialog {
    Q_OBJECT
public:
    explicit CreateGroupDialog(QWidget* parent = nullptr);
    void setCandidateMembers(const QStringList& memberIds,
                             const QMap<QString, QString>& names,
                             const QStringList& recentMemberIds = {});
    QString groupName() const;
    QStringList selectedMembers() const;
    QString selectedCategory() const;
    QString selectedAvatarId() const;

signals:
    void createRequested(const QString& name, const QStringList& members);

private:
    QWidget* buildSelectPage();
    QWidget* buildCategoryPage();
    QWidget* buildInfoPage();
    void setupUi();
    void updateStyle();
    void showPage(int page);
    void refreshMemberLists();
    void refreshSelectedMembers();
    void toggleMember(const QString& id);
    QWidget* buildMemberRow(const QString& id, QWidget* parent);
    void chooseCategory(const QString& category);
    void finishDirect();
    void finishCategorized();
    QString defaultGroupName() const;

    QStackedWidget* m_stack = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QVBoxLayout* m_recentLayout = nullptr;
    QVBoxLayout* m_friendLayout = nullptr;
    QLabel* m_recentEmpty = nullptr;
    QVBoxLayout* m_selectedLayout = nullptr;
    QLabel* m_selectedEmpty = nullptr;
    QPushButton* m_directCreateBtn = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLabel* m_categoryLabel = nullptr;
    QButtonGroup* m_avatarGroup = nullptr;
    QCheckBox* m_agreeCheck = nullptr;
    QPushButton* m_createBtn = nullptr;
    QStringList m_memberIds;
    QStringList m_recentIds;
    QMap<QString, QString> m_names;
    QStringList m_selectedIds;
    QString m_category;
    QString m_avatarId;
};

#endif
