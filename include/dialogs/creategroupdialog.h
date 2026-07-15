#ifndef CREATEGROUPDIALOG_H
#define CREATEGROUPDIALOG_H

#include <QDialog>
#include <QMap>
#include <QStringList>

class QLineEdit;
class QLabel;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;

// Three-step group-creation wizard (matches tauri-qqnt CreateGroupModal):
//   1. 选择成员  2. 选择分类  3. 填写信息
// The public API (setCandidateMembers / groupName / selectedMembers /
// createRequested) is unchanged so MainWindow's wiring keeps working.
class CreateGroupDialog : public QDialog {
    Q_OBJECT

public:
    explicit CreateGroupDialog(QWidget* parent = nullptr);

    // Populate the selectable member list. names maps id -> display name.
    void setCandidateMembers(const QStringList& memberIds,
                             const QMap<QString, QString>& names);

    QString groupName() const;
    QStringList selectedMembers() const;
    QString selectedCategory() const;

signals:
    void createRequested(const QString& name, const QStringList& members);

private:
    void setupUi();
    void updateStyle();
    QWidget* buildMemberStep();
    QWidget* buildCategoryStep();
    QWidget* buildInfoStep();
    void goToStep(int step);
    void updateStepChrome();
    void refreshMemberFilter();
    int selectedMemberCount() const;

    // Wizard chrome
    QStackedWidget* m_stack = nullptr;
    QLabel* m_stepLabel = nullptr;
    QPushButton* m_backBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
    int m_step = 0;

    // Step 1: members
    QLineEdit* m_memberSearch = nullptr;
    QListWidget* m_memberList = nullptr;

    // Step 2: category
    QListWidget* m_categoryList = nullptr;

    // Step 3: info
    QLineEdit* m_nameEdit = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_agreeLabel = nullptr;
    QPushButton* m_agreeCheck = nullptr;
    bool m_agreed = false;
};

#endif // CREATEGROUPDIALOG_H
