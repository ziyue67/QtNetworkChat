#ifndef GLOBALSEARCHDIALOG_H
#define GLOBALSEARCHDIALOG_H

#include <QDialog>

class QLineEdit;
class QLabel;
class QPushButton;
class QTabWidget;
class QListView;
class QStandardItemModel;

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

    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_searchBtn = nullptr;
    QTabWidget* m_tabWidget = nullptr;
    QListView* m_messagesList = nullptr;
    QListView* m_contactsList = nullptr;
    QListView* m_groupsList = nullptr;
    QStandardItemModel* m_messagesModel = nullptr;
    QStandardItemModel* m_contactsModel = nullptr;
    QStandardItemModel* m_groupsModel = nullptr;
};

#endif // GLOBALSEARCHDIALOG_H

