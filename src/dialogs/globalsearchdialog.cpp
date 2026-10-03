#include "dialogs/globalsearchdialog.h"

#include "theme/dialogstyle.h"
#include "theme/thememanager.h"
#include "widgets/avatarlabel.h"
#include "widgets/dialogtitlebar.h"

#include <QAbstractItemView>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPixmap>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
constexpr int kIdRole = Qt::UserRole;
constexpr int kTypeRole = Qt::UserRole + 1;
constexpr int kSubtitleRole = Qt::UserRole + 2;
constexpr int kTitleRole = Qt::UserRole + 3;
}

GlobalSearchDialog::GlobalSearchDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("globalSearchDialog"));
    setWindowTitle(QStringLiteral("综合搜索"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(660, 560);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &GlobalSearchDialog::updateStyle);
}

void GlobalSearchDialog::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_titleBar = new DialogTitleBar(this, QStringLiteral("综合搜索"));
    m_titleBar->setTitleCentered(true);
    connect(m_titleBar, &DialogTitleBar::closeRequested, this, &QDialog::reject);
    root->addWidget(m_titleBar);

    QWidget* body = new QWidget(this);
    QVBoxLayout* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(16, 16, 16, 16);
    bodyLayout->setSpacing(12);

    // Search row.
    QHBoxLayout* searchLayout = new QHBoxLayout();
    searchLayout->setSpacing(8);
    m_searchEdit = new QLineEdit(body);
    m_searchEdit->setObjectName(QStringLiteral("dialogInput"));
    m_searchEdit->setPlaceholderText(QStringLiteral("输入搜索关键词"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchBtn = new QPushButton(QStringLiteral("搜索"), body);
    m_searchBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    m_searchBtn->setAutoDefault(false);
    m_searchBtn->setDefault(false);
    m_searchBtn->setEnabled(false);
    connect(m_searchEdit, &QLineEdit::textChanged, m_searchBtn, [this](const QString& text) {
        m_searchBtn->setEnabled(!text.trimmed().isEmpty());
    });
    searchLayout->addWidget(m_searchEdit, 1);
    searchLayout->addWidget(m_searchBtn);
    bodyLayout->addLayout(searchLayout);

    auto triggerSearch = [this]() {
        if (m_searching || m_searchEdit->text().trimmed().isEmpty()) return;
        clearResults();
        setSearching(true);
        emit searchRequested(m_searchEdit->text());
    };
    connect(m_searchBtn, &QPushButton::clicked, this, triggerSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, triggerSearch);

    // Tab bar.
    QHBoxLayout* tabRow = new QHBoxLayout();
    tabRow->setSpacing(4);
    const QStringList tabLabels =
        {QStringLiteral("全部"), QStringLiteral("用户"), QStringLiteral("群聊"),
         QStringLiteral("小程序"), QStringLiteral("机器人")};
    for (int i = 0; i < tabLabels.size(); ++i) {
        QPushButton* tab = new QPushButton(tabLabels.at(i), body);
        tab->setObjectName(QStringLiteral("searchTabButton"));
        tab->setCheckable(true);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setChecked(i == TabAll);
        connect(tab, &QPushButton::clicked, this, [this, i]() { setActiveTab(i); });
        m_tabButtons.append(tab);
        tabRow->addWidget(tab);
    }
    tabRow->addStretch();
    bodyLayout->addLayout(tabRow);

    // Content: results/placeholder stack on the left, group detail on the right.
    QHBoxLayout* contentRow = new QHBoxLayout();
    contentRow->setSpacing(0);

    m_bodyStack = new QStackedWidget(body);

    // Page 0: compact QQ-style user/group result sections.
    QWidget* listsPage = new QWidget(m_bodyStack);
    QVBoxLayout* listsLayout = new QVBoxLayout(listsPage);
    listsLayout->setContentsMargins(0, 0, 0, 0);
    listsLayout->setSpacing(10);

    auto buildSection = [this, listsPage](QFrame** frameOut, QLabel** headingOut,
                                           QListWidget** listOut, QPushButton** moreOut) {
        auto* frame = new QFrame(listsPage);
        frame->setObjectName(QStringLiteral("searchResultSection"));
        auto* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto* header = new QWidget(frame);
        header->setObjectName(QStringLiteral("searchSectionHeader"));
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(12, 0, 10, 0);
        auto* heading = new QLabel(header);
        heading->setObjectName(QStringLiteral("searchSectionHeading"));
        headerLayout->addWidget(heading);
        headerLayout->addStretch();
        auto* more = new QPushButton(QStringLiteral("更多 ›"), header);
        more->setObjectName(QStringLiteral("searchMoreButton"));
        more->setCursor(Qt::PointingHandCursor);
        more->setAutoDefault(false);
        more->setDefault(false);
        headerLayout->addWidget(more);
        layout->addWidget(header);
        auto* list = new QListWidget(frame);
        list->setObjectName(QStringLiteral("searchResultList"));
        list->setEditTriggers(QAbstractItemView::NoEditTriggers);
        list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        layout->addWidget(list);
        *frameOut = frame;
        *headingOut = heading;
        *listOut = list;
        *moreOut = more;
    };

    buildSection(&m_usersSection, &m_usersHeading, &m_usersList, &m_usersMoreButton);
    buildSection(&m_groupsSection, &m_groupsHeading, &m_groupsList, &m_groupsMoreButton);
    m_usersHeading->setText(QStringLiteral("用户"));
    m_groupsHeading->setText(QStringLiteral("群聊"));
    listsLayout->addWidget(m_usersSection);
    listsLayout->addWidget(m_groupsSection);
    listsLayout->addStretch(1);
    m_bodyStack->addWidget(listsPage);

    // Page 1: coming-soon placeholder.
    QWidget* placeholderPage = new QWidget(m_bodyStack);
    QVBoxLayout* phLayout = new QVBoxLayout(placeholderPage);
    phLayout->setAlignment(Qt::AlignCenter);
    m_placeholderLabel = new QLabel(QStringLiteral("功能即将上线"), placeholderPage);
    m_placeholderLabel->setObjectName(QStringLiteral("searchPlaceholder"));
    m_placeholderLabel->setAlignment(Qt::AlignCenter);
    phLayout->addWidget(m_placeholderLabel);
    m_bodyStack->addWidget(placeholderPage);

    contentRow->addWidget(m_bodyStack, 1);

    // Group detail side panel (hidden until a group row is clicked).
    m_detailPanel = new QFrame(body);
    m_detailPanel->setObjectName(QStringLiteral("groupDetailPanel"));
    m_detailPanel->setFixedWidth(240);
    m_detailPanel->setVisible(false);
    QVBoxLayout* detailLayout = new QVBoxLayout(m_detailPanel);
    detailLayout->setContentsMargins(16, 16, 16, 16);
    detailLayout->setSpacing(10);

    m_detailAvatar = new AvatarLabel(m_detailPanel, 56);
    detailLayout->addWidget(m_detailAvatar, 0, Qt::AlignHCenter);
    m_detailName = new QLabel(m_detailPanel);
    m_detailName->setObjectName(QStringLiteral("groupDetailName"));
    m_detailName->setAlignment(Qt::AlignHCenter);
    m_detailName->setWordWrap(true);
    m_detailMeta = new QLabel(m_detailPanel);
    m_detailMeta->setObjectName(QStringLiteral("groupDetailInfo"));
    m_detailMeta->setAlignment(Qt::AlignHCenter);
    m_detailMeta->setWordWrap(true);
    detailLayout->addWidget(m_detailName);
    detailLayout->addWidget(m_detailMeta);
    detailLayout->addStretch();

    m_detailActionBtn = new QPushButton(QStringLiteral("申请加群"), m_detailPanel);
    m_detailActionBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    m_detailActionBtn->setCursor(Qt::PointingHandCursor);
    connect(m_detailActionBtn, &QPushButton::clicked, this, [this]() {
        if (!m_detailGroupId.isEmpty()) {
            if (m_detailType == QStringLiteral("contact")) {
                emit addFriendRequested(m_detailGroupId);
            } else if (m_joinedGroups.value(m_detailGroupId, false)) {
                emit resultActivated(QStringLiteral("group"), m_detailGroupId);
            } else {
                emit joinGroupRequested(m_detailGroupId);
            }
        }
    });
    detailLayout->addWidget(m_detailActionBtn);

    QPushButton* detailCloseBtn = new QPushButton(QStringLiteral("关闭"), m_detailPanel);
    detailCloseBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    detailCloseBtn->setCursor(Qt::PointingHandCursor);
    connect(detailCloseBtn, &QPushButton::clicked, this, [this]() { hideGroupDetail(); });
    detailLayout->addWidget(detailCloseBtn);

    contentRow->addWidget(m_detailPanel);
    bodyLayout->addLayout(contentRow, 1);
    root->addWidget(body, 1);

    connect(m_usersList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item) {
            if (item->flags() & Qt::ItemIsEnabled) {
                showUserDetail(item);
            }
        }
    });
    connect(m_groupsList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        showGroupDetail(item);
    });
    connect(m_usersMoreButton, &QPushButton::clicked, this, [this]() { setActiveTab(TabUsers); });
    connect(m_groupsMoreButton, &QPushButton::clicked, this, [this]() { setActiveTab(TabGroups); });

    setActiveTab(TabAll);
}

void GlobalSearchDialog::setContactGroupMode(bool enabled)
{
    m_contactGroupMode = enabled;
    setWindowTitle(enabled ? QStringLiteral("加好友/群") : QStringLiteral("综合搜索"));
    if (m_titleBar) m_titleBar->setTitle(enabled ? QStringLiteral("加好友/群") : QStringLiteral("综合搜索"));
    if (enabled) {
        for (int i = 0; i < m_tabButtons.size(); ++i) {
            m_tabButtons.at(i)->setVisible(i == TabUsers || i == TabGroups);
        }
        m_searchEdit->setPlaceholderText(QStringLiteral("输入 QQ 号、昵称、群名或群号"));
        setActiveTab(TabUsers);
    }
}

void GlobalSearchDialog::setContactKnown(const QString& id, bool known)
{
    m_knownContacts[id] = known;
}

void GlobalSearchDialog::setGroupJoined(const QString& id, bool joined)
{
    m_joinedGroups[id] = joined;
    if (joined) m_pendingGroupJoins.remove(id);
    if (id == m_detailGroupId && m_detailType == QStringLiteral("group") && m_detailActionBtn) {
        m_detailActionBtn->setText(joined ? QStringLiteral("进入群聊")
                                          : (m_pendingGroupJoins.value(id, false) ? QStringLiteral("申请中") : QStringLiteral("申请加群")));
        m_detailActionBtn->setEnabled(!m_pendingGroupJoins.value(id, false) || joined);
    }
    if (!m_groupsList) return;
    for (int row = 0; row < m_groupsList->count(); ++row) {
        QListWidgetItem* item = m_groupsList->item(row);
        if (!item || item->data(kIdRole).toString() != id) continue;
        QWidget* rowWidget = m_groupsList->itemWidget(item);
        if (rowWidget) {
            if (QPushButton* action = rowWidget->findChild<QPushButton*>(QStringLiteral("searchResultActionBtn"))) {
                action->setText(joined ? QStringLiteral("进入")
                                       : (m_pendingGroupJoins.value(id, false) ? QStringLiteral("申请中") : QStringLiteral("加入")));
            }
        }
    }
}

void GlobalSearchDialog::setGroupJoinPending(const QString& id, bool pending)
{
    if (pending) m_pendingGroupJoins[id] = true;
    else m_pendingGroupJoins.remove(id);
    setGroupJoined(id, m_joinedGroups.value(id, false));
}

void GlobalSearchDialog::setSearching(bool searching)
{
    m_searching = searching;
    if (!m_searchBtn || !m_searchEdit) return;
    m_searchBtn->setEnabled(!searching && !m_searchEdit->text().trimmed().isEmpty());
    m_searchBtn->setText(searching ? QStringLiteral("搜索中…") : QStringLiteral("搜索"));
}

void GlobalSearchDialog::setActiveTab(int tab)
{
    m_activeTab = tab;
    for (int i = 0; i < m_tabButtons.size(); ++i) {
        m_tabButtons.at(i)->setChecked(i == tab);
    }
    const bool comingSoon = (tab == TabMiniPrograms || tab == TabBots);
    m_bodyStack->setCurrentIndex(comingSoon ? 1 : 0);
    if (!comingSoon) {
        refreshVisibility();
    }
}

void GlobalSearchDialog::refreshVisibility()
{
    const bool showUsers = (m_activeTab == TabAll || m_activeTab == TabUsers);
    const bool showGroups = (m_activeTab == TabAll || m_activeTab == TabGroups);
    m_usersSection->setVisible(showUsers);
    m_groupsSection->setVisible(showGroups);
    refreshResultSections();
}

void GlobalSearchDialog::showGroupDetail(QListWidgetItem* item)
{
    if (!item) return;
    m_detailGroupId = item->data(kIdRole).toString();
    m_detailType = QStringLiteral("group");
    const QString name = item->data(kTitleRole).toString();
    m_detailName->setText(name);
    m_detailMeta->setText(QStringLiteral("群号: %1 · %2")
                              .arg(m_detailGroupId, item->data(kSubtitleRole).toString()));
    m_detailAvatar->setTextAvatar(name.left(1).toUpper(), ThemeManager::instance()->primaryColor());
    const bool joined = m_joinedGroups.value(m_detailGroupId, false);
    const bool pending = m_pendingGroupJoins.value(m_detailGroupId, false);
    m_detailActionBtn->setText(joined ? QStringLiteral("进入群聊") : (pending ? QStringLiteral("申请中") : QStringLiteral("申请加群")));
    m_detailActionBtn->setEnabled(!pending || joined);
    m_detailPanel->setVisible(true);
}

void GlobalSearchDialog::showUserDetail(QListWidgetItem* item)
{
    if (!item) return;
    m_detailGroupId = item->data(kIdRole).toString();
    m_detailType = QStringLiteral("contact");
    const QString name = item->data(kTitleRole).toString();
    m_detailName->setText(name);
    m_detailMeta->setText(QStringLiteral("QQ: %1\n%2").arg(m_detailGroupId, item->data(kSubtitleRole).toString()));
    m_detailAvatar->setTextAvatar(name.left(1).toUpper(), ThemeManager::instance()->primaryColor());
    const bool known = m_knownContacts.value(m_detailGroupId, false);
    m_detailActionBtn->setText(known ? QStringLiteral("已是好友") : QStringLiteral("加好友"));
    m_detailActionBtn->setEnabled(!known);
    m_detailPanel->setVisible(true);
}

void GlobalSearchDialog::hideGroupDetail()
{
    m_detailPanel->setVisible(false);
    m_detailGroupId.clear();
    m_detailType.clear();
}

void GlobalSearchDialog::addResult(const QString& type, const QString& id, const QString& title,
                                   const QString& subtitle, const QString& avatarPath)
{
    QListWidget* targetList = type == QStringLiteral("group") ? m_groupsList : m_usersList;
    if (!targetList) return;
    auto* item = new QListWidgetItem();
    item->setData(kIdRole, id);
    item->setData(kTypeRole, type);
    item->setData(kSubtitleRole, subtitle);
    item->setData(kTitleRole, title);
    item->setSizeHint(QSize(0, 64));
    targetList->addItem(item);

    auto* row = new QWidget(targetList);
    row->setObjectName(QStringLiteral("searchResultRow"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 7, 10, 7);
    layout->setSpacing(10);
    auto* avatar = new AvatarLabel(row, 42);
    if (!avatarPath.isEmpty() && QFileInfo::exists(avatarPath)) {
        avatar->setPixmap(QPixmap(avatarPath));
    } else {
        avatar->setTextAvatar(title.left(1).toUpper(), ThemeManager::instance()->primaryColor());
    }
    const bool online = subtitle.contains(QStringLiteral("在线")) && !subtitle.contains(QStringLiteral("离线"));
    avatar->setStatus(online ? AvatarLabel::Online : AvatarLabel::None);
    layout->addWidget(avatar);
    auto* details = new QVBoxLayout();
    details->setContentsMargins(0, 0, 0, 0);
    details->setSpacing(2);
    auto* name = new QLabel(title, row);
    name->setObjectName(QStringLiteral("searchResultName"));
    auto* meta = new QLabel(subtitle, row);
    meta->setObjectName(QStringLiteral("searchResultMeta"));
    details->addWidget(name);
    details->addWidget(meta);
    layout->addLayout(details, 1);
    const bool group = type == QStringLiteral("group");
    auto* action = new QPushButton(group
                                       ? (m_joinedGroups.value(id, false) ? QStringLiteral("进入")
                                                                          : (m_pendingGroupJoins.value(id, false) ? QStringLiteral("申请中") : QStringLiteral("加入")))
                                       : QStringLiteral("添加"), row);
    action->setObjectName(QStringLiteral("searchResultActionBtn"));
    action->setCursor(Qt::PointingHandCursor);
    action->setFixedSize(58, 28);
    action->setAutoDefault(false);
    action->setDefault(false);
    layout->addWidget(action);
    targetList->setItemWidget(item, row);
    connect(action, &QPushButton::clicked, this, [this, item, group]() {
        if (group) {
            const QString groupId = item->data(kIdRole).toString();
            if (m_joinedGroups.value(groupId, false)) {
                emit resultActivated(QStringLiteral("group"), groupId);
            } else if (!m_pendingGroupJoins.value(groupId, false)) {
                emit joinGroupRequested(groupId);
            }
        } else {
            showUserDetail(item);
        }
    });
    refreshResultSections();
}

void GlobalSearchDialog::clearResults()
{
    if (m_usersList) m_usersList->clear();
    if (m_groupsList) m_groupsList->clear();
    m_resultKeyword.clear();
    hideGroupDetail();
    refreshResultSections();
}

void GlobalSearchDialog::showSearchState(const QString& message)
{
    if (!m_usersList || message.isEmpty()) return;
    auto* item = new QListWidgetItem(message);
    item->setFlags(Qt::NoItemFlags);
    item->setTextAlignment(Qt::AlignCenter);
    m_usersList->addItem(item);
    refreshResultSections();
}

void GlobalSearchDialog::refreshResultSections()
{
    if (!m_usersList || !m_groupsList || !m_usersSection || !m_groupsSection) return;
    const bool allTab = m_activeTab == TabAll;
    const int userCount = m_usersList->count();
    const int groupCount = m_groupsList->count();
    const int maxRows = allTab ? 3 : qMax(userCount, groupCount);
    auto updateList = [allTab, maxRows](QListWidget* list) {
        for (int row = 0; row < list->count(); ++row) {
            const bool visible = !allTab || row < maxRows;
            list->setRowHidden(row, !visible);
        }
        const int visibleCount = allTab ? qMin(list->count(), maxRows) : list->count();
        list->setFixedHeight(qMax(0, visibleCount * 64));
    };
    updateList(m_usersList);
    updateList(m_groupsList);
    const QString key = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    m_usersHeading->setText(key.isEmpty() ? QStringLiteral("用户") : QStringLiteral("%1 · 用户").arg(key));
    m_groupsHeading->setText(key.isEmpty() ? QStringLiteral("群聊") : QStringLiteral("%1 · 群聊").arg(key));
    m_usersMoreButton->setVisible(allTab && userCount > 0);
    m_groupsMoreButton->setVisible(allTab && groupCount > 0);
    m_usersSection->setVisible((m_activeTab == TabAll || m_activeTab == TabUsers) && userCount > 0);
    m_groupsSection->setVisible((m_activeTab == TabAll || m_activeTab == TabGroups) && groupCount > 0);
}

QString GlobalSearchDialog::searchText() const
{
    return m_searchEdit->text();
}

void GlobalSearchDialog::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    // Shared rules from DialogStyle::common(); search-specific selectors (tab
    // buttons, result list, group detail panel, placeholder, headings) here.
    setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#globalSearchDialog { background-color: %1; }"
        "QPushButton#searchTabButton { background: transparent; color: %3; border: none; padding: 6px 14px; border-radius: 6px; }"
        "QPushButton#searchTabButton:checked { background: %6; color: %2; font-weight: 600; }"
        "QFrame#searchResultSection { background-color: %1; border: 1px solid %5; border-radius: 8px; }"
        "QWidget#searchSectionHeader { min-height: 42px; border-bottom: 1px solid %5; }"
        "QPushButton#searchMoreButton { background: transparent; color: %3; border: none; padding: 0; font-size: 12px; }"
        "QPushButton#searchMoreButton:hover { color: %7; }"
        "QListWidget#searchResultList { background-color: transparent; border: none; color: %2; }"
        "QListWidget#searchResultList::item { padding: 0; border-bottom: 1px solid %5; }"
        "QListWidget#searchResultList::item:selected { background-color: %6; color: %2; }"
        "QWidget#searchResultRow { background: transparent; }"
        "QWidget#searchResultRow:hover { background: %6; }"
        "QLabel#searchResultName { color: %7; font-size: 14px; font-weight: 600; }"
        "QLabel#searchResultMeta { color: %3; font-size: 12px; }"
        "QPushButton#searchResultActionBtn { background: %1; color: %7; border: 1px solid %5; border-radius: 14px; padding: 0; font-size: 12px; }"
        "QPushButton#searchResultActionBtn:hover { background: %6; border-color: %7; }"
        "QFrame#groupDetailPanel { background-color: %4; border-left: 1px solid %5; }"
        "QLabel#groupDetailName { color: %2; font-size: 16px; font-weight: 600; }"
        "QLabel#groupDetailInfo { color: %3; font-size: 12px; }"
        "QLabel#searchPlaceholder { color: %3; font-size: 14px; }"
        "QLabel#searchSectionHeading { color: %7; font-size: 14px; font-weight: 600; }"
    ).arg(tm->backgroundColor().name())            // %1
     .arg(tm->textColor().name())                  // %2
     .arg(tm->textSecondaryColor().name())          // %3
     .arg(tm->backgroundSecondaryColor().name())    // %4
     .arg(tm->borderColor().name())                 // %5
     .arg(tm->primarySoftColor().name())            // %6
     .arg(tm->primaryColor().name()));              // %7
}
