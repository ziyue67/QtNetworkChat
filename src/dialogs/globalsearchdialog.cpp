#include "dialogs/globalsearchdialog.h"

#include "theme/thememanager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QVBoxLayout>

GlobalSearchDialog::GlobalSearchDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("globalSearchDialog"));
    setWindowTitle(QStringLiteral("全局搜索"));
    setMinimumSize(500, 400);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &GlobalSearchDialog::updateStyle);
}

void GlobalSearchDialog::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    QLabel* title = new QLabel(QStringLiteral("全局搜索"), this);
    title->setObjectName(QStringLiteral("dialogTitleLabel"));
    root->addWidget(title);

    QHBoxLayout* searchLayout = new QHBoxLayout();
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("dialogInput"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索消息、联系人、群聊..."));
    searchLayout->addWidget(m_searchEdit, 1);

    m_searchBtn = new QPushButton(QStringLiteral("搜索"), this);
    m_searchBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    connect(m_searchBtn, &QPushButton::clicked, this, [this]() {
        clearResults();
        emit searchRequested(m_searchEdit->text());
    });
    searchLayout->addWidget(m_searchBtn);
    root->addLayout(searchLayout);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName(QStringLiteral("dialogTabWidget"));

    m_messagesModel = new QStandardItemModel(this);
    m_messagesList = new QListView(this);
    m_messagesList->setObjectName(QStringLiteral("dialogListView"));
    m_messagesList->setModel(m_messagesModel);
    m_tabWidget->addTab(m_messagesList, QStringLiteral("消息"));

    m_contactsModel = new QStandardItemModel(this);
    m_contactsList = new QListView(this);
    m_contactsList->setObjectName(QStringLiteral("dialogListView"));
    m_contactsList->setModel(m_contactsModel);
    m_tabWidget->addTab(m_contactsList, QStringLiteral("联系人"));

    m_groupsModel = new QStandardItemModel(this);
    m_groupsList = new QListView(this);
    m_groupsList->setObjectName(QStringLiteral("dialogListView"));
    m_groupsList->setModel(m_groupsModel);
    m_tabWidget->addTab(m_groupsList, QStringLiteral("群聊"));

    root->addWidget(m_tabWidget, 1);

    QPushButton* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    root->addWidget(closeBtn);
}

void GlobalSearchDialog::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#globalSearchDialog { background-color: %1; }"
        "QLabel#dialogTitleLabel { color: %2; font-size: 18px; font-weight: 600; }"
        "QLineEdit#dialogInput { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 12px; }"
        "QLineEdit#dialogInput:focus { border: 1px solid %6; }"
        "QPushButton#dialogPrimaryBtn { background-color: %6; color: white; border: none; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogPrimaryBtn:hover { background-color: %7; }"
        "QPushButton#dialogSecondaryBtn { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogSecondaryBtn:hover { background-color: %5; }"
        "QTabWidget#dialogTabWidget::pane { border: 1px solid %5; border-radius: 6px; background-color: %4; }"
        "QTabWidget#dialogTabWidget::tab-bar { left: 8px; }"
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

QString GlobalSearchDialog::searchText() const
{
    return m_searchEdit->text();
}

void GlobalSearchDialog::addResult(const QString& type, const QString& id, const QString& title, const QString& subtitle)
{
    QStandardItem* item = new QStandardItem(title);
    item->setData(id, Qt::UserRole);
    item->setData(type, Qt::UserRole + 1);
    item->setToolTip(subtitle);

    if (type == QStringLiteral("message")) {
        m_messagesModel->appendRow(item);
    } else if (type == QStringLiteral("contact")) {
        m_contactsModel->appendRow(item);
    } else if (type == QStringLiteral("group")) {
        m_groupsModel->appendRow(item);
    }
}

void GlobalSearchDialog::clearResults()
{
    m_messagesModel->clear();
    m_contactsModel->clear();
    m_groupsModel->clear();
}

