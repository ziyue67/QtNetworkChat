#ifndef CREATEGROUPDIALOG_H
#define CREATEGROUPDIALOG_H

#include <QDialog>

class QLineEdit;
class QLabel;
class QPushButton;
class QListView;
class QStandardItemModel;

class CreateGroupDialog : public QDialog {
    Q_OBJECT

public:
    explicit CreateGroupDialog(QWidget* parent = nullptr);

    QString groupName() const;
    QStringList selectedMembers() const;

signals:
    void createRequested(const QString& name, const QStringList& members);

private:
    void setupUi();
    void updateStyle();

    QLineEdit* m_nameEdit = nullptr;
    QLabel* m_hintLabel = nullptr;
    QListView* m_memberList = nullptr;
    QStandardItemModel* m_memberModel = nullptr;
    QPushButton* m_createBtn = nullptr;
};

#endif // CREATEGROUPDIALOG_H

