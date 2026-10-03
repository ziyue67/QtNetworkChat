#include "windows/forwardwindow.h"

#include "theme/thememanager.h"
#include "widgets/dialogtitlebar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QVBoxLayout>

ForwardWindow::ForwardWindow(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("forwardWindow"));
    setWindowTitle(QStringLiteral("转发"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(400, 450);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ForwardWindow::updateStyle);
}

void ForwardWindow::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    DialogTitleBar* titleBar = new DialogTitleBar(this, QStringLiteral("转发到"));
    connect(titleBar, &DialogTitleBar::closeRequested, this, &QDialog::reject);
    root->addWidget(titleBar);

    QWidget* body = new QWidget(this);
    QVBoxLayout* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(20, 20, 20, 20);
    bodyLayout->setSpacing(12);

    m_searchEdit = new QLineEdit(body);
    m_searchEdit->setObjectName(QStringLiteral("dialogInput"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索..."));
    bodyLayout->addWidget(m_searchEdit);

    m_tabWidget = new QTabWidget(body);
    m_tabWidget->setObjectName(QStringLiteral("dialogTabWidget"));

    m_contactModel = new QStandardItemModel(this);
    m_contactList = new QListView(body);
    m_contactList->setObjectName(QStringLiteral("dialogListView"));
    m_contactList->setModel(m_contactModel);
    m_tabWidget->addTab(m_contactList, QStringLiteral("联系人"));

    m_groupModel = new QStandardItemModel(this);
    m_groupList = new QListView(body);
    m_groupList->setObjectName(QStringLiteral("dialogListView"));
    m_groupList->setModel(m_groupModel);
    m_tabWidget->addTab(m_groupList, QStringLiteral("群聊"));

    bodyLayout->addWidget(m_tabWidget, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_forwardBtn = new QPushButton(QStringLiteral("转发"), body);
    m_forwardBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    connect(m_forwardBtn, &QPushButton::clicked, this, [this]() {
        int currentTab = m_tabWidget->currentIndex();
        if (currentTab == 0) {
            QModelIndex idx = m_contactList->currentIndex();
            if (idx.isValid()) {
                emit forwardToContactRequested(idx.data(Qt::UserRole).toString());
                accept();
            }
        } else {
            QModelIndex idx = m_groupList->currentIndex();
            if (idx.isValid()) {
                emit forwardToGroupRequested(idx.data(Qt::UserRole).toString());
                accept();
            }
        }
    });
    btnLayout->addWidget(m_forwardBtn);

    QPushButton* cancelBtn = new QPushButton(QStringLiteral("取消"), body);
    cancelBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    bodyLayout->addLayout(btnLayout);

    root->addWidget(body, 1);
}

void ForwardWindow::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#forwardWindow { background-color: %1; }"
        "QLineEdit#dialogInput { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 12px; }"
        "QLineEdit#dialogInput:focus { border: 1px solid %6; }"
        "QPushButton#dialogPrimaryBtn { background-color: %6; color: white; border: none; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogPrimaryBtn:hover { background-color: %7; }"
        "QPushButton#dialogSecondaryBtn { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogSecondaryBtn:hover { background-color: %5; }"
        "QTabWidget#dialogTabWidget::pane { border: 1px solid %5; border-radius: 6px; background-color: %4; }"
        "QTabBar::tab { background-color: %4; color: %3; padding: 8px 16px; border: none; }"
        "QTabBar::tab:selected { background-color: %8; color: %2; }"
        "QListView#dialogListView { background-color: %4; border: none; color: %2; }"
        "QListView#dialogListView::item { padding: 8px 12px; }"
        "QListView#dialogListView::item:selected { background-color: %8; color: %2; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->primaryHoverColor().name())
     .arg(tm->primarySoftColor().name()));
}

void ForwardWindow::setContacts(const QStringList& contactIds, const QMap<QString, QString>& contactNames)
{
    m_contactModel->clear();
    for (const QString& id : contactIds) {
        QStandardItem* item = new QStandardItem(contactNames.value(id, id));
        item->setData(id, Qt::UserRole);
        m_contactModel->appendRow(item);
    }
}

void ForwardWindow::setGroups(const QStringList& groupIds, const QMap<QString, QString>& groupNames)
{
    m_groupModel->clear();
    for (const QString& id : groupIds) {
        QStandardItem* item = new QStandardItem(groupNames.value(id, id));
        item->setData(id, Qt::UserRole);
        m_groupModel->appendRow(item);
    }
}


