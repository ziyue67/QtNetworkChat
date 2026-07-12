#include "views/contactsview.h"

#include "theme/thememanager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
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
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(14);

    QFrame* header = new QFrame(this);
    header->setObjectName(QStringLiteral("contactsHeader"));
    header->setFixedHeight(76);
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 14, 20, 14);
    headerLayout->setSpacing(14);

    QLabel* title = new QLabel(QStringLiteral("联系人"), header);
    title->setObjectName(QStringLiteral("contactsTitleLabel"));
    headerLayout->addWidget(title);
    headerLayout->addStretch();

    m_searchEdit = new QLineEdit(header);
    m_searchEdit->setObjectName(QStringLiteral("contactsSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索好友或群聊..."));
    m_searchEdit->setFixedWidth(300);
    headerLayout->addWidget(m_searchEdit);
    auto* addButton = new QPushButton(QStringLiteral("添加联系人"), header);
    addButton->setObjectName(QStringLiteral("contactsAddButton"));
    addButton->setFixedHeight(34);
    headerLayout->addWidget(addButton);
    auto* createGroupButton = new QPushButton(QStringLiteral("创建群聊"), header);
    createGroupButton->setObjectName(QStringLiteral("contactsSecondaryButton"));
    createGroupButton->setFixedHeight(34);
    headerLayout->addWidget(createGroupButton);
    root->addWidget(header);

    QFrame* content = new QFrame(this);
    content->setObjectName(QStringLiteral("contactsContentPanel"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 16, 18, 18);
    contentLayout->setSpacing(10);

    auto* sectionTitle = new QLabel(QStringLiteral("通讯录"), content);
    sectionTitle->setObjectName(QStringLiteral("contactsSectionTitle"));
    contentLayout->addWidget(sectionTitle);

    m_tabWidget = new QTabWidget(content);
    m_tabWidget->setObjectName(QStringLiteral("contactsTabWidget"));
    m_tabWidget->setDocumentMode(true);

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

    contentLayout->addWidget(m_tabWidget, 1);
    root->addWidget(content, 1);

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
    connect(addButton, &QPushButton::clicked, this, &ContactsView::addFriendRequested);
    connect(createGroupButton, &QPushButton::clicked, this, &ContactsView::createGroupRequested);
}

void ContactsView::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QWidget#contactsView { background-color: %4; }"
        "QFrame#contactsHeader, QFrame#contactsContentPanel { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
        "QLabel#contactsTitleLabel { color: %3; font-size: 18px; font-weight: 600; }"
        "QLabel#contactsSectionTitle { color: %3; font-size: 14px; font-weight: 600; }"
        "QLineEdit#contactsSearchEdit { background-color: %4; color: %3; border: 1px solid %2; border-radius: 7px; padding: 7px 10px; }"
        "QLineEdit#contactsSearchEdit:focus { border: 1px solid %5; }"
        "QPushButton#contactsAddButton { background-color: %5; color: white; border: none; border-radius: 6px; padding: 6px 14px; font-weight: 600; }"
        "QPushButton#contactsAddButton:hover { background-color: %9; }"
        "QPushButton#contactsSecondaryButton { background: %1; color: %5; border: 1px solid %2; border-radius: 6px; padding: 6px 14px; }"
        "QPushButton#contactsSecondaryButton:hover { background: %4; }"
        "QTabWidget#contactsTabWidget::pane { background-color: %1; border: none; }"
        "QTabBar::tab { background-color: transparent; color: %6; padding: 8px 18px; border: none; }"
        "QTabBar::tab:selected { color: %5; border-bottom: 2px solid %5; }"
        "QListView#friendListView, QListView#groupListView { background: %1; border: none; outline: none; padding: 6px 0; }"
        "QListView#friendListView::item, QListView#groupListView::item { color: %3; padding: 12px 14px; border-radius: 7px; }"
        "QListView#friendListView::item:selected, QListView#groupListView::item:selected { background-color: %7; }"
        "QListView#friendListView::item:hover, QListView#groupListView::item:hover { background-color: %8; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->textColor().name())
     .arg(tm->backgroundTertiaryColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->textTertiaryColor().name())
     .arg(tm->color(QStringLiteral("session-selected")).name())
     .arg(tm->color(QStringLiteral("session-hover")).name())
     .arg(tm->primaryHoverColor().name()));
}

QLineEdit* ContactsView::searchEdit() const { return m_searchEdit; }
QListView* ContactsView::friendListView() const { return m_friendListView; }
QListView* ContactsView::groupListView() const { return m_groupListView; }
QStandardItemModel* ContactsView::friendModel() const { return m_friendModel; }
QStandardItemModel* ContactsView::groupModel() const { return m_groupModel; }
