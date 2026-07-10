#ifndef ADDFRIENDDIALOG_H
#define ADDFRIENDDIALOG_H

#include <QDialog>

class QLineEdit;
class QLabel;
class QPushButton;
class QListView;
class QStandardItemModel;

class AddFriendDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddFriendDialog(QWidget* parent = nullptr);

    QString searchText() const;

signals:
    void searchRequested(const QString& text);
    void addFriendRequested(const QString& userId);

public slots:
    void onSearchResult(const QString& account, const QString& userId, const QString& userName, bool found);

private:
    void setupUi();
    void updateStyle();

    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_searchBtn = nullptr;
    QLabel* m_resultLabel = nullptr;
    QListView* m_resultList = nullptr;
    QStandardItemModel* m_resultModel = nullptr;
    QPushButton* m_addBtn = nullptr;
    QString m_currentResultId;
};

#endif // ADDFRIENDDIALOG_H

