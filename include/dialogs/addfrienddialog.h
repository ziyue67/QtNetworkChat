#ifndef ADDFRIENDDIALOG_H
#define ADDFRIENDDIALOG_H

#include <QDialog>

class QLineEdit;
class QLabel;
class QPushButton;
class QFrame;
class AvatarLabel;

class AddFriendDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddFriendDialog(QWidget* parent = nullptr);

    QString searchText() const;

signals:
    void searchRequested(const QString& text);
    void addFriendRequested(const QString& userId);

public slots:
    // Called by MainWindow when an async friend-search result arrives. Populates
    // the result card (found) or shows an inline "未找到" message (not found).
    void onSearchResult(const QString& account, const QString& userId, const QString& userName, bool found);

private:
    void setupUi();
    void updateStyle();
    void setLoading(bool loading);
    void setAdded();

    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_searchBtn = nullptr;
    QLabel* m_errorLabel = nullptr;

    // Result card
    QFrame* m_resultCard = nullptr;
    AvatarLabel* m_resultAvatar = nullptr;
    QLabel* m_resultName = nullptr;
    QLabel* m_resultDesc = nullptr;
    QPushButton* m_addBtn = nullptr;

    QString m_currentResultId;
    bool m_added = false;
};

#endif // ADDFRIENDDIALOG_H
