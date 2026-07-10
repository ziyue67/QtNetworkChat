#include "views/contactsview.h"

#include "theme/thememanager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QVBoxLayout>

ContactsView::ContactsView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("contactsView"));
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ContactsView::updateStyle);
}

void ContactsView::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QFrame* header = new QFrame(this);
    header->setObjectName(QStringLiteral("contactsHeader"));
    header->setFixedHeight(60);
    QVBoxLayout* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(16, 8, 16, 8);
    headerLayout->setSpacing(2);

    QLabel* title = new QLabel(QStringLiteral("联系人"), header);
    title->setObjectName(QStringLiteral("contactsTitleLabel"));
    headerLayout->addWidget(title);

    m_searchEdit = new QLineEdit(header);
    m_searchEdit->setObjectName(QStringLiteral("contactsSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索好友或群聊..."));
    headerLayout->addWidget(m_searchEdit);
    root->addWidget(header);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName(QStringLiteral("contactsTabWidget"));

    m_friendModel = new QStandardItemModel(this);
    m_friendListView = new QListView(this);
    m_friendListView->setObjectName(QStringLiteral("friendListView"));
    m_friendListView->setModel(m_friendModel);
    m_friendListView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tabWidget->addTab(m_friendListView, QStringLiteral("好友"));

    m_groupModel = new QStandardItemModel(this);
    m_groupListView = new QListView(this);
    m_groupListView->setObjectName(QStringLiteral("groupListView"));
    m_groupListView->setModel(m_groupModel);
    m_groupListView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tabWidget->addTab(m_groupListView, QStringLiteral("群聊"));

    root->addWidget(m_tabWidget, 1);

    connect(m_friendListView, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) {
            emit friendSelected(index.data(Qt::UserRole).toString());
        }
    });
    connect(m_groupListView, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) {
            emit groupSelected(index.data(Qt::UserRole).toString());
        }
    });
}

void ContactsView::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QFrame#contactsHeader { background-color: %1; border-bottom: 1px solid %2; }"
        "QLabel#contactsTitleLabel { color: %3; font-size: 16px; font-weight: 600; }"
        "QLineEdit#contactsSearchEdit { background-color: %4; color: %3; border: 1px solid %2; border-radius: 6px; padding: 5px 10px; }"
        "QLineEdit#contactsSearchEdit:focus { border: 1px solid %5; }"
        "QTabWidget::pane { background-color: %1; border: none; }"
        "QTabBar::tab { background-color: %1; color: %6; padding: 8px 16px; border: none; }"
        "QTabBar::tab:selected { color: %5; border-bottom: 2px solid %5; }"
        "QListView#friendListView, QListView#groupListView { background: transparent; border: none; outline: none; }"
        "QListView#friendListView::item, QListView#groupListView::item { color: %3; padding: 8px; border-radius: 6px; }"
        "QListView#friendListView::item:selected, QListView#groupListView::item:selected { background-color: %7; }"
        "QListView#friendListView::item:hover, QListView#groupListView::item:hover { background-color: %8; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->textColor().name())
     .arg(tm->backgroundTertiaryColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->textTertiaryColor().name())
     .arg(tm->color(QStringLiteral("session-selected")).name())
     .arg(tm->color(QStringLiteral("session-hover")).name()));
}

QLineEdit* ContactsView::searchEdit() const { return m_searchEdit; }
QListView* ContactsView::friendListView() const { return m_friendListView; }
QListView* ContactsView::groupListView() const { return m_groupListView; }
QStandardItemModel* ContactsView::friendModel() const { return m_friendModel; }
QStandardItemModel* ContactsView::groupModel() const { return m_groupModel; }
