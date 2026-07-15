#include "views/contactsview.h"

#include "theme/thememanager.h"
#include "widgets/contactcard.h"
#include "widgets/contactlistwidget.h"
#include "widgets/contactnoticepanel.h"

#include <QAction>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
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

namespace {
QPushButton* makeToolButton(QWidget* parent, const QString& text, const QString& objName)
{
    auto* btn = new QPushButton(text, parent);
    btn->setObjectName(objName);
    btn->setFlat(true);
    btn->setCursor(Qt::PointingHandCursor);
    return btn;
}
}

void ContactsView::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(14);

    // Header
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
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索好友或群聊，按回车进入全局搜索"));
    m_searchEdit->setFixedWidth(300);
    headerLayout->addWidget(m_searchEdit);

    m_plusButton = new QPushButton(QStringLiteral("＋"), header);
    m_plusButton->setObjectName(QStringLiteral("contactsPlusButton"));
    m_plusButton->setFixedSize(34, 34);
    m_plusMenu = new QMenu(m_plusButton);
    m_plusMenu->setObjectName(QStringLiteral("contactsPlusMenu"));
    QAction* actionAddFriend = m_plusMenu->addAction(QStringLiteral("加好友/群"));
    QAction* actionCreateGroup = m_plusMenu->addAction(QStringLiteral("创建群聊"));
    QAction* actionFlashFile = m_plusMenu->addAction(QStringLiteral("闪传文件"));
    actionFlashFile->setEnabled(false);
    m_plusButton->setMenu(m_plusMenu);
    headerLayout->addWidget(m_plusButton);

    m_friendManagerButton = makeToolButton(header, QStringLiteral("好友管理器"), QStringLiteral("contactsToolButton"));
    headerLayout->addWidget(m_friendManagerButton);

    root->addWidget(header);

    // Content
    QFrame* content = new QFrame(this);
    content->setObjectName(QStringLiteral("contactsContentPanel"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 16, 18, 18);
    contentLayout->setSpacing(12);

    auto* sectionTitle = new QLabel(QStringLiteral("通讯录"), content);
    sectionTitle->setObjectName(QStringLiteral("contactsSectionTitle"));
    contentLayout->addWidget(sectionTitle);

    // Notice / manager toolbar
    auto* toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(10);
    m_friendNoticeButton = makeToolButton(content, QStringLiteral("好友通知"), QStringLiteral("contactsToolButton"));
    m_groupNoticeButton = makeToolButton(content, QStringLiteral("群通知"), QStringLiteral("contactsToolButton"));
    toolbarLayout->addWidget(m_friendNoticeButton);
    toolbarLayout->addWidget(m_groupNoticeButton);
    toolbarLayout->addStretch();
    contentLayout->addLayout(toolbarLayout);

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

    m_tabWidget->setVisible(false);

    m_contactList = new ContactListWidget(this);
    m_contactList->setObjectName(QStringLiteral("contactListWidget"));

    m_contactCard = new ContactCard(this);
    m_contactCard->setObjectName(QStringLiteral("contactCard"));

    m_noticePanel = new ContactNoticePanel(this);
    m_noticePanel->setObjectName(QStringLiteral("contactNoticePanel"));

    m_emptyLabel = new QLabel(QStringLiteral("选择一位好友或群聊查看详情"), this);
    m_emptyLabel->setObjectName(QStringLiteral("contactsEmptyLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);

    m_detailStack = new QStackedWidget(this);
    m_detailStack->addWidget(m_emptyLabel);
    m_detailStack->addWidget(m_contactCard);
    m_detailStack->addWidget(m_noticePanel);
    m_detailStack->setCurrentIndex(0);

    auto* listDetailLayout = new QHBoxLayout();
    listDetailLayout->setSpacing(14);
    listDetailLayout->addWidget(m_contactList, 1);
    listDetailLayout->addWidget(m_detailStack, 1);
    contentLayout->addLayout(listDetailLayout, 1);

    // Friend / group mode toggle
    auto* modeLayout = new QHBoxLayout();
    modeLayout->setSpacing(0);
    m_friendModeButton = new QPushButton(QStringLiteral("好友"), content);
    m_friendModeButton->setObjectName(QStringLiteral("contactsModeButton"));
    m_friendModeButton->setCheckable(true);
    m_friendModeButton->setChecked(true);
    m_friendModeButton->setFixedHeight(32);

    m_groupModeButton = new QPushButton(QStringLiteral("群聊"), content);
    m_groupModeButton->setObjectName(QStringLiteral("contactsModeButton"));
    m_groupModeButton->setCheckable(true);
    m_groupModeButton->setFixedHeight(32);

    modeLayout->addWidget(m_friendModeButton);
    modeLayout->addWidget(m_groupModeButton);
    modeLayout->addStretch();
    contentLayout->addLayout(modeLayout);

    root->addWidget(content, 1);

    // Connections
    connect(m_contactList, &ContactListWidget::friendSelected, this, [this](const QString& id){ onContactSelected(id, false); });
    connect(m_contactList, &ContactListWidget::groupSelected, this, [this](const QString& id){ onContactSelected(id, true); });
    // The profile card's primary button opens the conversation. Route it through the
    // same friendSelected/groupSelected signals MainWindow already binds to session opening.
    connect(m_contactCard, &ContactCard::sendMessageRequested, this, [this](const QString& id, bool isGroup) {
        if (isGroup) {
            emit groupSelected(id);
        } else {
            emit friendSelected(id);
        }
    });
    connect(actionAddFriend, &QAction::triggered, this, &ContactsView::addFriendRequested);
    connect(actionCreateGroup, &QAction::triggered, this, &ContactsView::createGroupRequested);
    connect(m_friendManagerButton, &QPushButton::clicked, this, &ContactsView::friendManagerRequested);
    connect(m_friendNoticeButton, &QPushButton::clicked, this, [this]() { onShowNoticePanel(false); });
    connect(m_groupNoticeButton, &QPushButton::clicked, this, [this]() { onShowNoticePanel(true); });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &ContactsView::globalSearchRequested);

    auto* modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(m_friendModeButton);
    modeGroup->addButton(m_groupModeButton);
    connect(m_friendModeButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_contactList->setShowGroups(false);
            // No item is selected in the new list yet; show the placeholder rather
            // than a stale card from the previous mode.
            setDetailMode(false);
        }
    });
    connect(m_groupModeButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_contactList->setShowGroups(true);
            setDetailMode(false);
        }
    });
}

void ContactsView::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QWidget#contactsView { background-color: %4; }"
        "QFrame#contactsHeader, QFrame#contactsContentPanel { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
        "QLabel#contactsTitleLabel { color: %3; font-size: 18px; font-weight: 600; }"
        "QLabel#contactsSectionTitle { color: %3; font-size: 14px; font-weight: 600; }"
        "QLabel#contactsEmptyLabel { color: %6; font-size: 14px; }"
        "QLineEdit#contactsSearchEdit { background-color: %4; color: %3; border: 1px solid %2; border-radius: 7px; padding: 7px 10px; }"
        "QLineEdit#contactsSearchEdit:focus { border: 1px solid %5; }"
        "QPushButton#contactsPlusButton { background-color: %5; color: white; border: none; border-radius: 6px; font-weight: 600; font-size: 16px; }"
        "QPushButton#contactsPlusButton:hover { background-color: %9; }"
        "QPushButton#contactsPlusButton::menu-indicator { image: none; }"
        "QPushButton#contactsToolButton { background: transparent; color: %3; border: none; padding: 4px 8px; font-weight: 500; }"
        "QPushButton#contactsToolButton:hover { color: %5; }"
        "QPushButton#contactsModeButton { background: %4; color: %3; border: 1px solid %2; padding: 4px 16px; }"
        "QPushButton#contactsModeButton:checked { background: %5; color: white; border-color: %5; }"
        "QPushButton#contactsModeButton:first-child { border-top-left-radius: 6px; border-bottom-left-radius: 6px; }"
        "QPushButton#contactsModeButton:last-child { border-top-right-radius: 6px; border-bottom-right-radius: 6px; }"
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
ContactListWidget* ContactsView::contactList() const { return m_contactList; }
QStandardItemModel* ContactsView::friendModel() const { return m_friendModel; }
QStandardItemModel* ContactsView::groupModel() const { return m_groupModel; }

void ContactsView::setFriends(const QList<ContactDisplayData>& contacts)
{
    if (m_contactList) {
        m_contactList->setFriends(contacts);
    }
    m_friendMap.clear();
    for (const ContactDisplayData& d : contacts) {
        if (!d.id.isEmpty()) {
            m_friendMap.insert(d.id, d);
        }
    }
}

void ContactsView::setGroups(const QList<ContactDisplayData>& groups)
{
    if (m_contactList) {
        m_contactList->setGroups(groups);
    }
    m_groupMap.clear();
    for (const ContactDisplayData& d : groups) {
        if (!d.id.isEmpty()) {
            m_groupMap.insert(d.id, d);
        }
    }
}

void ContactsView::onContactSelected(const QString& id, bool isGroup)
{
    if (isGroup) {
        const auto it = m_groupMap.constFind(id);
        if (it != m_groupMap.constEnd() && m_contactCard) {
            m_contactCard->setGroupData(*it, {});
            setDetailMode(true);
        }
    } else {
        const auto it = m_friendMap.constFind(id);
        if (it != m_friendMap.constEnd() && m_contactCard) {
            m_contactCard->setFriendData(*it);
            setDetailMode(true);
        }
    }
}

void ContactsView::onShowNoticePanel(bool groupNotice)
{
    if (m_noticePanel) {
        m_noticePanel->setNoticeType(groupNotice ? ContactNoticePanel::GroupNotice
                                                  : ContactNoticePanel::FriendNotice);
        m_detailStack->setCurrentWidget(m_noticePanel);
    }
}

void ContactsView::setDetailMode(bool showCard)
{
    if (!m_detailStack) return;
    if (showCard) {
        m_detailStack->setCurrentWidget(m_contactCard);
    } else {
        m_detailStack->setCurrentWidget(m_emptyLabel);
    }
}
