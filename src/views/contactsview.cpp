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

void ContactsView::setupUi()
{
    // QQNT ContactsView layout (mirrors tauri-qqnt ContactsView.tsx):
    //   left sidebar (fixed width): search + [+] menu, 好友管理器, 好友通知 / 群通知,
    //   好友/群聊 toggle, contact list.
    //   right main area: stacked 空占位 / ContactCard / ContactNoticePanel.
    QHBoxLayout* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- Left sidebar ----------------------------------------------------
    QFrame* sidebar = new QFrame(this);
    sidebar->setObjectName(QStringLiteral("contactsSidebar"));
    sidebar->setFixedWidth(260);
    QVBoxLayout* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(8, 8, 8, 8);
    sideLayout->setSpacing(8);

    // Search row: read-only search box + [+] menu button.
    QHBoxLayout* searchRow = new QHBoxLayout();
    searchRow->setSpacing(6);
    m_searchEdit = new QLineEdit(sidebar);
    m_searchEdit->setObjectName(QStringLiteral("contactsSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索"));
    searchRow->addWidget(m_searchEdit, 1);

    m_plusButton = new QPushButton(QStringLiteral("＋"), sidebar);
    m_plusButton->setObjectName(QStringLiteral("contactsPlusButton"));
    m_plusButton->setFixedSize(28, 28);
    m_plusButton->setCursor(Qt::PointingHandCursor);
    m_plusMenu = new QMenu(m_plusButton);
    m_plusMenu->setObjectName(QStringLiteral("contactsPlusMenu"));
    QAction* actionCreateGroup = m_plusMenu->addAction(QStringLiteral("创建群聊"));
    QAction* actionAddFriend = m_plusMenu->addAction(QStringLiteral("加好友/群"));
    QAction* actionFlashFile = m_plusMenu->addAction(QStringLiteral("闪传文件"));
    actionFlashFile->setEnabled(false);
    m_plusButton->setMenu(m_plusMenu);
    searchRow->addWidget(m_plusButton, 0);
    sideLayout->addLayout(searchRow);

    // 好友管理器 full-width button.
    m_friendManagerButton = new QPushButton(QStringLiteral("好友管理器"), sidebar);
    m_friendManagerButton->setObjectName(QStringLiteral("contactsManagerButton"));
    m_friendManagerButton->setCursor(Qt::PointingHandCursor);
    m_friendManagerButton->setFixedHeight(32);
    sideLayout->addWidget(m_friendManagerButton);

    // 好友通知 / 群通知 rows (text + unread dot + chevron).
    auto makeNoticeRow = [sidebar](const QString& text, QLabel** dotOut) -> QPushButton* {
        auto* btn = new QPushButton(sidebar);
        btn->setObjectName(QStringLiteral("contactsNoticeRow"));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(36);
        auto* rowLayout = new QHBoxLayout(btn);
        rowLayout->setContentsMargins(4, 0, 8, 0);
        rowLayout->setSpacing(6);
        auto* label = new QLabel(text, btn);
        label->setObjectName(QStringLiteral("contactsNoticeText"));
        rowLayout->addWidget(label);
        auto* dot = new QLabel(btn);
        dot->setObjectName(QStringLiteral("contactsNoticeDot"));
        dot->setFixedSize(8, 8);
        dot->setVisible(false);
        rowLayout->addWidget(dot);
        rowLayout->addStretch();
        auto* chevron = new QLabel(QStringLiteral("›"), btn);
        chevron->setObjectName(QStringLiteral("contactsNoticeChevron"));
        rowLayout->addWidget(chevron);
        if (dotOut) *dotOut = dot;
        return btn;
    };
    m_friendNoticeButton = makeNoticeRow(QStringLiteral("好友通知"), &m_friendNoticeDot);
    m_groupNoticeButton = makeNoticeRow(QStringLiteral("群通知"), &m_groupNoticeDot);
    sideLayout->addWidget(m_friendNoticeButton);
    sideLayout->addWidget(m_groupNoticeButton);

    // 好友 / 群聊 segmented toggle.
    QHBoxLayout* modeLayout = new QHBoxLayout();
    modeLayout->setSpacing(0);
    m_friendModeButton = new QPushButton(QStringLiteral("好友"), sidebar);
    m_friendModeButton->setObjectName(QStringLiteral("contactsModeButton"));
    m_friendModeButton->setCheckable(true);
    m_friendModeButton->setChecked(true);
    m_friendModeButton->setFixedHeight(30);
    m_groupModeButton = new QPushButton(QStringLiteral("群聊"), sidebar);
    m_groupModeButton->setObjectName(QStringLiteral("contactsModeButton"));
    m_groupModeButton->setCheckable(true);
    m_groupModeButton->setFixedHeight(30);
    modeLayout->addWidget(m_friendModeButton);
    modeLayout->addWidget(m_groupModeButton);
    sideLayout->addLayout(modeLayout);

    // Contact list fills the rest of the sidebar.
    m_contactList = new ContactListWidget(sidebar);
    m_contactList->setObjectName(QStringLiteral("contactListWidget"));
    sideLayout->addWidget(m_contactList, 1);

    root->addWidget(sidebar);

    // ---- Right main area -------------------------------------------------
    m_contactCard = new ContactCard(this);
    m_contactCard->setObjectName(QStringLiteral("contactCard"));

    m_noticePanel = new ContactNoticePanel(this);
    m_noticePanel->setObjectName(QStringLiteral("contactNoticePanel"));

    m_emptyLabel = new QLabel(QStringLiteral("选择一个联系人查看资料"), this);
    m_emptyLabel->setObjectName(QStringLiteral("contactsEmptyLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);

    m_detailStack = new QStackedWidget(this);
    m_detailStack->setObjectName(QStringLiteral("contactsMainArea"));
    m_detailStack->addWidget(m_emptyLabel);
    m_detailStack->addWidget(m_contactCard);
    m_detailStack->addWidget(m_noticePanel);
    m_detailStack->setCurrentIndex(0);
    root->addWidget(m_detailStack, 1);

    // ---- Legacy compatibility (hidden) -----------------------------------
    // MainWindow still reads friendModel()/groupModel()/friendListView() etc.
    // Keep them alive but invisible; the visible list is ContactListWidget.
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName(QStringLiteral("contactsTabWidget"));
    m_friendModel = new QStandardItemModel(this);
    m_friendListView = new QListView(this);
    m_friendListView->setObjectName(QStringLiteral("friendListView"));
    m_friendListView->setModel(m_friendModel);
    m_friendListView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_groupModel = new QStandardItemModel(this);
    m_groupListView = new QListView(this);
    m_groupListView->setObjectName(QStringLiteral("groupListView"));
    m_groupListView->setModel(m_groupModel);
    m_groupListView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tabWidget->addTab(m_friendListView, QStringLiteral("好友"));
    m_tabWidget->addTab(m_groupListView, QStringLiteral("群聊"));
    m_tabWidget->setVisible(false);

    // ---- Connections -----------------------------------------------------
    connect(m_contactList, &ContactListWidget::friendSelected, this, [this](const QString& id){ onContactSelected(id, false); });
    connect(m_contactList, &ContactListWidget::groupSelected, this, [this](const QString& id){ onContactSelected(id, true); });
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
    // The search box opens the global search flow (read-only entry point like the ref).
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &ContactsView::globalSearchRequested);

    auto* modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(m_friendModeButton);
    modeGroup->addButton(m_groupModeButton);
    connect(m_friendModeButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_contactList->setShowGroups(false);
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
    // %1 bg  %2 border  %3 text  %4 bg-secondary  %5 primary  %6 text-tertiary
    // %7 session-selected  %8 session-hover  %9 primary-hover  %10 danger
    setStyleSheet(QStringLiteral(
        "QWidget#contactsView { background-color: %4; }"
        "QFrame#contactsSidebar { background-color: %1; border: 1px solid %2; border-right: none; "
            "border-top-left-radius: 8px; border-bottom-left-radius: 8px; }"
        "QStackedWidget#contactsMainArea { background-color: %1; border: 1px solid %2; "
            "border-top-right-radius: 8px; border-bottom-right-radius: 8px; }"
        "QLabel#contactsEmptyLabel { color: %6; font-size: 14px; }"
        "QLineEdit#contactsSearchEdit { background-color: %4; color: %3; border: 1px solid %2; border-radius: 7px; padding: 6px 10px; }"
        "QLineEdit#contactsSearchEdit:focus { border: 1px solid %5; }"
        "QPushButton#contactsPlusButton { background-color: %5; color: white; border: none; border-radius: 6px; font-weight: 600; font-size: 15px; }"
        "QPushButton#contactsPlusButton:hover { background-color: %9; }"
        "QPushButton#contactsPlusButton::menu-indicator { image: none; }"
        "QPushButton#contactsManagerButton { background: %4; color: %3; border: 1px solid %2; border-radius: 7px; font-weight: 500; }"
        "QPushButton#contactsManagerButton:hover { background: %8; color: %5; }"
        "QPushButton#contactsNoticeRow { background: transparent; border: none; border-radius: 7px; text-align: left; }"
        "QPushButton#contactsNoticeRow:hover { background: %8; }"
        "QLabel#contactsNoticeText { color: %3; font-size: 13px; background: transparent; }"
        "QLabel#contactsNoticeChevron { color: %6; font-size: 15px; background: transparent; }"
        "QLabel#contactsNoticeDot { background-color: %10; border-radius: 4px; }"
        "QPushButton#contactsModeButton { background: %4; color: %3; border: 1px solid %2; padding: 4px 16px; }"
        "QPushButton#contactsModeButton:checked { background: %5; color: white; border-color: %5; }"
        "QPushButton#contactsModeButton:first-child { border-top-left-radius: 6px; border-bottom-left-radius: 6px; }"
        "QPushButton#contactsModeButton:last-child { border-top-right-radius: 6px; border-bottom-right-radius: 6px; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->textColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->textTertiaryColor().name())
     .arg(tm->color(QStringLiteral("session-selected")).name())
     .arg(tm->color(QStringLiteral("session-hover")).name())
     .arg(tm->primaryHoverColor().name())
     .arg(tm->dangerColor().name()));
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
