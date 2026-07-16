#include "dialogs/globalsearchdialog.h"

#include "theme/dialogstyle.h"
#include "theme/thememanager.h"
#include "widgets/avatarlabel.h"
#include "widgets/dialogtitlebar.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
constexpr int kIdRole = Qt::UserRole;
constexpr int kTypeRole = Qt::UserRole + 1;
constexpr int kSubtitleRole = Qt::UserRole + 2;
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
    m_searchBtn->setEnabled(false);
    connect(m_searchEdit, &QLineEdit::textChanged, m_searchBtn, [this](const QString& text) {
        m_searchBtn->setEnabled(!text.trimmed().isEmpty());
    });
    searchLayout->addWidget(m_searchEdit, 1);
    searchLayout->addWidget(m_searchBtn);
    bodyLayout->addLayout(searchLayout);

    auto triggerSearch = [this]() {
        if (m_searchEdit->text().trimmed().isEmpty()) return;
        clearResults();
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

    // Page 0: user + group result lists with section headings.
    QWidget* listsPage = new QWidget(m_bodyStack);
    QVBoxLayout* listsLayout = new QVBoxLayout(listsPage);
    listsLayout->setContentsMargins(0, 0, 0, 0);
    listsLayout->setSpacing(8);

    m_usersHeading = new QLabel(QStringLiteral("用户"), listsPage);
    m_usersHeading->setObjectName(QStringLiteral("searchSectionHeading"));
    m_usersList = new QListWidget(listsPage);
    m_usersList->setObjectName(QStringLiteral("searchResultList"));
    m_usersList->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_groupsHeading = new QLabel(QStringLiteral("群聊"), listsPage);
    m_groupsHeading->setObjectName(QStringLiteral("searchSectionHeading"));
    m_groupsList = new QListWidget(listsPage);
    m_groupsList->setObjectName(QStringLiteral("searchResultList"));
    m_groupsList->setEditTriggers(QAbstractItemView::NoEditTriggers);

    listsLayout->addWidget(m_usersHeading);
    listsLayout->addWidget(m_usersList, 1);
    listsLayout->addWidget(m_groupsHeading);
    listsLayout->addWidget(m_groupsList, 1);
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

    m_detailActionBtn = new QPushButton(QStringLiteral("进入群聊"), m_detailPanel);
    m_detailActionBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    m_detailActionBtn->setCursor(Qt::PointingHandCursor);
    connect(m_detailActionBtn, &QPushButton::clicked, this, [this]() {
        if (!m_detailGroupId.isEmpty()) {
            if (m_detailType == QStringLiteral("contact")) {
                emit addFriendRequested(m_detailGroupId);
            } else {
                emit resultActivated(QStringLiteral("group"), m_detailGroupId);
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
            if (m_contactGroupMode) showUserDetail(item);
            else emit resultActivated(QStringLiteral("contact"), item->data(kIdRole).toString());
        }
    });
    connect(m_groupsList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        showGroupDetail(item);
    });

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

void GlobalSearchDialog::setGroupEnterable(const QString& id, bool enterable)
{
    m_enterableGroups[id] = enterable;
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
    m_usersHeading->setVisible(showUsers);
    m_usersList->setVisible(showUsers);
    m_groupsHeading->setVisible(showGroups);
    m_groupsList->setVisible(showGroups);
}

void GlobalSearchDialog::showGroupDetail(QListWidgetItem* item)
{
    if (!item) return;
    m_detailGroupId = item->data(kIdRole).toString();
    m_detailType = QStringLiteral("group");
    const QString name = item->data(Qt::DisplayRole).toString();
    m_detailName->setText(name);
    m_detailMeta->setText(QStringLiteral("群号: %1 · %2")
                              .arg(m_detailGroupId, item->data(kSubtitleRole).toString()));
    m_detailAvatar->setTextAvatar(name.left(1).toUpper(), ThemeManager::instance()->primaryColor());
    const bool enterable = m_enterableGroups.value(m_detailGroupId, true);
    m_detailActionBtn->setText(enterable ? QStringLiteral("进入群聊") : QStringLiteral("群资料（只读）"));
    m_detailActionBtn->setEnabled(enterable);
    m_detailPanel->setVisible(true);
}

void GlobalSearchDialog::showUserDetail(QListWidgetItem* item)
{
    if (!item) return;
    m_detailGroupId = item->data(kIdRole).toString();
    m_detailType = QStringLiteral("contact");
    const QString name = item->data(Qt::DisplayRole).toString().section(QStringLiteral("  ·  "), 0, 0);
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

void GlobalSearchDialog::addResult(const QString& type, const QString& id, const QString& title, const QString& subtitle)
{
    QListWidgetItem* item = new QListWidgetItem(
        subtitle.isEmpty() ? title : QStringLiteral("%1  ·  %2").arg(title, subtitle));
    item->setData(kIdRole, id);
    item->setData(kTypeRole, type);
    item->setData(kSubtitleRole, subtitle);
    if (type == QStringLiteral("group")) {
        m_groupsList->addItem(item);
    } else {
        m_usersList->addItem(item);
    }
}

void GlobalSearchDialog::clearResults()
{
    if (m_usersList) m_usersList->clear();
    if (m_groupsList) m_groupsList->clear();
    hideGroupDetail();
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
        "QListWidget#searchResultList { background-color: %4; border: 1px solid %5; border-radius: 6px; color: %2; }"
        "QListWidget#searchResultList::item { padding: 8px 12px; border-bottom: 1px solid %5; }"
        "QListWidget#searchResultList::item:selected { background-color: %6; color: %2; }"
        "QFrame#groupDetailPanel { background-color: %4; border-left: 1px solid %5; }"
        "QLabel#groupDetailName { color: %2; font-size: 16px; font-weight: 600; }"
        "QLabel#groupDetailInfo { color: %3; font-size: 12px; }"
        "QLabel#searchPlaceholder { color: %3; font-size: 14px; }"
        "QLabel#searchSectionHeading { color: %3; font-size: 12px; font-weight: 600; }"
    ).arg(tm->backgroundColor().name())            // %1
     .arg(tm->textColor().name())                  // %2
     .arg(tm->textSecondaryColor().name())          // %3
     .arg(tm->backgroundSecondaryColor().name())    // %4
     .arg(tm->borderColor().name())                 // %5
     .arg(tm->primarySoftColor().name()));          // %6
}
