#include "dialogs/friendmanagerdialog.h"

#include "theme/dialogstyle.h"
#include "theme/thememanager.h"
#include "widgets/dialogtitlebar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int kColCheck = 0;
constexpr int kColName = 1;
constexpr int kColRemark = 2;
constexpr int kColGroup = 3;
constexpr int kColPermission = 4;
const QString kAllFriendsGroup = QStringLiteral("全部好友");
const QString kDefaultGroup = QStringLiteral("我的好友");
}

FriendManagerDialog::FriendManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("friendManagerDialog"));
    setWindowTitle(QStringLiteral("好友管理器"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(860, 600);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &FriendManagerDialog::updateStyle);
}

void FriendManagerDialog::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    DialogTitleBar* titleBar = new DialogTitleBar(this, QStringLiteral("好友管理器"));
    connect(titleBar, &DialogTitleBar::closeRequested, this, &QDialog::reject);
    root->addWidget(titleBar);

    QWidget* body = new QWidget(this);
    QHBoxLayout* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    // ---- Left group rail (200px) ----------------------------------------
    QWidget* rail = new QWidget(body);
    rail->setObjectName(QStringLiteral("managerRail"));
    rail->setFixedWidth(200);
    QVBoxLayout* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(8, 8, 8, 8);
    railLayout->setSpacing(6);

    m_groupList = new QListWidget(rail);
    m_groupList->setObjectName(QStringLiteral("managerGroupList"));
    m_groupList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_groupList, &QListWidget::currentRowChanged, this, [this]() {
        rebuildTable();
    });
    connect(m_groupList, &QListWidget::customContextMenuRequested,
            this, &FriendManagerDialog::showGroupContextMenu);
    railLayout->addWidget(m_groupList, 1);

    // Adding is an explicit command; editing/removing a group lives in its context menu.
    QHBoxLayout* groupBtnRow = new QHBoxLayout();
    groupBtnRow->setSpacing(6);
    m_addGroupBtn = new QPushButton(QStringLiteral("＋ 新增分组"), rail);
    m_addGroupBtn->setObjectName(QStringLiteral("managerGroupAddBtn"));
    m_addGroupBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addGroupBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("添加分组"),
                                                   QStringLiteral("分组名称:"), QLineEdit::Normal,
                                                   QString(), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        if (name == kAllFriendsGroup || m_customGroups.contains(name) || name == kDefaultGroup) {
            QMessageBox::information(this, QStringLiteral("添加分组"), QStringLiteral("该分组已存在"));
            return;
        }
        m_customGroups.append(name);
        emit createGroupRequested(name);
        rebuildGroupRail();
    });
    groupBtnRow->addWidget(m_addGroupBtn);
    railLayout->addLayout(groupBtnRow);

    bodyLayout->addWidget(rail);

    // ---- Right main panel ------------------------------------------------
    QWidget* main = new QWidget(body);
    QVBoxLayout* mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(20, 18, 20, 16);
    mainLayout->setSpacing(14);

    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setSpacing(12);
    m_titleLabel = new QLabel(QStringLiteral("好友管理器"), main);
    m_titleLabel->setObjectName(QStringLiteral("managerTitle"));
    headerRow->addWidget(m_titleLabel);
    headerRow->addStretch();
    m_searchEdit = new QLineEdit(main);
    m_searchEdit->setObjectName(QStringLiteral("dialogInput"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索"));
    m_searchEdit->setFixedWidth(200);
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_filter = text.trimmed();
        rebuildTable();
    });
    headerRow->addWidget(m_searchEdit);
    mainLayout->addLayout(headerRow);

    m_table = new QTableWidget(main);
    m_table->setObjectName(QStringLiteral("managerTable"));
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(
        {QString(), QStringLiteral("昵称"), QStringLiteral("备注"),
         QStringLiteral("分组"), QStringLiteral("好友权限")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setColumnWidth(kColCheck, 36);
    m_table->horizontalHeader()->setSectionResizeMode(kColCheck, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColRemark, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColGroup, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColPermission, QHeaderView::Stretch);

    QHBoxLayout* selectAllRow = new QHBoxLayout();
    selectAllRow->setContentsMargins(8, 0, 0, 0);
    m_selectAll = new QCheckBox(QStringLiteral("全选"), main);
    m_selectAll->setObjectName(QStringLiteral("managerSelectAll"));
    connect(m_selectAll, &QCheckBox::clicked, this, [this](bool checked) {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (auto* cb = qobject_cast<QCheckBox*>(m_table->cellWidget(row, kColCheck))) {
                cb->setChecked(checked);
            }
        }
    });
    selectAllRow->addWidget(m_selectAll);
    selectAllRow->addStretch();
    mainLayout->addLayout(selectAllRow);
    mainLayout->addWidget(m_table, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_addBtn = new QPushButton(QStringLiteral("添加好友"), main);
    m_addBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    m_addBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addBtn, &QPushButton::clicked, this, &FriendManagerDialog::addFriendRequested);
    btnLayout->addWidget(m_addBtn);

    m_deleteBtn = new QPushButton(QStringLiteral("删除选中"), main);
    m_deleteBtn->setObjectName(QStringLiteral("dialogDangerBtn"));
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
        QStringList selected;
        for (int row = 0; row < m_table->rowCount(); ++row) {
            auto* cb = qobject_cast<QCheckBox*>(m_table->cellWidget(row, kColCheck));
            if (cb && cb->isChecked()) {
                QTableWidgetItem* nameItem = m_table->item(row, kColName);
                if (nameItem) selected << nameItem->data(Qt::UserRole).toString();
            }
        }
        if (selected.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("删除好友"), QStringLiteral("请先勾选要删除的好友"));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("删除好友"),
                                  QStringLiteral("确定删除选中的 %1 位好友吗？").arg(selected.size()),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        for (const QString& id : selected) {
            emit deleteFriendRequested(id);
            m_friendIds.removeAll(id);
            m_friendGroups.remove(id);
        }
        rebuildTable();
    });
    btnLayout->addWidget(m_deleteBtn);
    btnLayout->addStretch();

    QPushButton* closeBtn = new QPushButton(QStringLiteral("关闭"), main);
    closeBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    bodyLayout->addWidget(main, 1);
    root->addWidget(body, 1);

    rebuildGroupRail();
}
QString FriendManagerDialog::currentGroup() const
{
    if (!m_groupList || !m_groupList->currentItem()) return kAllFriendsGroup;
    return m_groupList->currentItem()->data(Qt::UserRole).toString();
}

void FriendManagerDialog::rebuildGroupRail()
{
    if (!m_groupList) return;
    const QString prev = currentGroup();
    QSignalBlocker blocker(m_groupList);
    m_groupList->clear();

    // Count members per group (missing assignment => 我的好友).
    QMap<QString, int> counts;
    for (const QString& id : m_friendIds) {
        const QString g = m_friendGroups.value(id, kDefaultGroup);
        counts[g] += 1;
    }

    auto addRow = [this](const QString& name, int count) {
        auto* item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(name).arg(count), m_groupList);
        item->setData(Qt::UserRole, name);
    };

    // 全部好友 first.
    addRow(kAllFriendsGroup, m_friendIds.size());
    // Default group, then custom groups (union of assigned + declared empty ones).
    QStringList groups;
    groups << kDefaultGroup;
    for (const QString& g : m_customGroups) {
        if (!groups.contains(g)) groups << g;
    }
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (!groups.contains(it.key())) groups << it.key();
    }
    for (const QString& g : groups) {
        if (g == kAllFriendsGroup) continue;
        addRow(g, counts.value(g, 0));
    }

    // Restore selection.
    int restore = 0;
    for (int i = 0; i < m_groupList->count(); ++i) {
        if (m_groupList->item(i)->data(Qt::UserRole).toString() == prev) { restore = i; break; }
    }
    m_groupList->setCurrentRow(restore);
    rebuildTable();
}

bool FriendManagerDialog::isCustomGroup(const QString& groupName) const
{
    return !groupName.isEmpty()
        && groupName != kAllFriendsGroup
        && groupName != kDefaultGroup
        && m_customGroups.contains(groupName);
}

void FriendManagerDialog::showGroupContextMenu(const QPoint& position)
{
    QListWidgetItem* item = m_groupList ? m_groupList->itemAt(position) : nullptr;
    if (!item) return;

    const QString groupName = item->data(Qt::UserRole).toString();
    if (!isCustomGroup(groupName)) return;
    m_groupList->setCurrentItem(item);

    QMenu menu(this);
    QAction* renameAction = menu.addAction(QStringLiteral("重命名"));
    QAction* deleteAction = menu.addAction(QStringLiteral("删除"));
    QAction* chosen = menu.exec(m_groupList->viewport()->mapToGlobal(position));
    if (chosen == renameAction) {
        bool ok = false;
        const QString newName = QInputDialog::getText(this, QStringLiteral("重命名分组"),
                                                       QStringLiteral("分组名称:"), QLineEdit::Normal,
                                                       groupName, &ok).trimmed();
        if (!ok || newName.isEmpty() || newName == groupName) return;
        if (newName == kAllFriendsGroup || newName == kDefaultGroup || m_customGroups.contains(newName)) {
            QMessageBox::information(this, QStringLiteral("重命名分组"), QStringLiteral("该分组已存在"));
            return;
        }
        m_customGroups.replace(m_customGroups.indexOf(groupName), newName);
        for (auto it = m_friendGroups.begin(); it != m_friendGroups.end(); ++it) {
            if (it.value() == groupName) it.value() = newName;
        }
        emit renameGroupRequested(groupName, newName);
        rebuildGroupRail();
    } else if (chosen == deleteAction) {
        if (QMessageBox::question(this, QStringLiteral("删除分组"),
                                  QStringLiteral("确定删除分组“%1”吗？组内好友将回到「我的好友」。").arg(groupName),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        for (auto it = m_friendGroups.begin(); it != m_friendGroups.end(); ++it) {
            if (it.value() == groupName) {
                it.value() = kDefaultGroup;
                emit moveFriendToGroupRequested(it.key(), kDefaultGroup);
            }
        }
        m_customGroups.removeAll(groupName);
        emit deleteGroupRequested(groupName);
        rebuildGroupRail();
    }
}

void FriendManagerDialog::rebuildTable()
{
    if (!m_table) return;
    const QString group = currentGroup();
    m_table->setRowCount(0);

    // Group options for the per-row combo.
    QStringList groupOptions;
    groupOptions << kDefaultGroup;
    for (const QString& g : m_customGroups) {
        if (!groupOptions.contains(g)) groupOptions << g;
    }
    for (const QString& id : m_friendIds) {
        const QString g = m_friendGroups.value(id, kDefaultGroup);
        if (!groupOptions.contains(g)) groupOptions << g;
    }

    for (const QString& id : m_friendIds) {
        const QString name = m_friendNames.value(id, id);
        const QString friendGroup = m_friendGroups.value(id, kDefaultGroup);
        if (group != kAllFriendsGroup && friendGroup != group) {
            continue;
        }
        if (!m_filter.isEmpty()
            && !id.contains(m_filter, Qt::CaseInsensitive)
            && !name.contains(m_filter, Qt::CaseInsensitive)) {
            continue;
        }
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        QWidget* checkHolder = new QWidget(m_table);
        QHBoxLayout* checkLayout = new QHBoxLayout(checkHolder);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->setAlignment(Qt::AlignCenter);
        QCheckBox* cb = new QCheckBox(checkHolder);
        connect(cb, &QCheckBox::clicked, this, [this]() { updateSelectAllState(); });
        checkLayout->addWidget(cb);
        m_table->setCellWidget(row, kColCheck, cb);

        QTableWidgetItem* nameItem = new QTableWidgetItem(name);
        nameItem->setData(Qt::UserRole, id);
        m_table->setItem(row, kColName, nameItem);
        m_table->setItem(row, kColRemark, new QTableWidgetItem(m_friendNames.value(id, QStringLiteral("-"))));

        // Group column: editable combo so the user can move a friend.
        QComboBox* groupCombo = new QComboBox(m_table);
        groupCombo->addItems(groupOptions);
        groupCombo->setCurrentText(friendGroup);
        connect(groupCombo, &QComboBox::currentTextChanged, this, [this, id](const QString& newGroup) {
            if (newGroup.isEmpty()) return;
            m_friendGroups[id] = newGroup;
            emit moveFriendToGroupRequested(id, newGroup);
        });
        m_table->setCellWidget(row, kColGroup, groupCombo);

        m_table->setItem(row, kColPermission, new QTableWidgetItem(QStringLiteral("正常")));
    }
    updateSelectAllState();
}

void FriendManagerDialog::updateSelectAllState()
{
    if (!m_selectAll || !m_table) return;
    int total = 0;
    int checked = 0;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (auto* cb = qobject_cast<QCheckBox*>(m_table->cellWidget(row, kColCheck))) {
            ++total;
            if (cb->isChecked()) ++checked;
        }
    }
    QSignalBlocker blocker(m_selectAll);
    m_selectAll->setChecked(total > 0 && checked == total);
}

void FriendManagerDialog::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#friendManagerDialog { background-color: %1; }"
        "QWidget#managerRail { background-color: %3; border-right: 1px solid %4; }"
        "QListWidget#managerGroupList { background-color: %3; border: none; color: %2; outline: none; }"
        "QListWidget#managerGroupList::item { padding: 8px 10px; border-radius: 6px; }"
        "QListWidget#managerGroupList::item:selected { background-color: %5; color: %2; }"
        "QPushButton#managerGroupAddBtn { background: %1; color: %2; border: 1px solid %4; border-radius: 6px; padding: 6px 4px; font-size: 12px; }"
        "QPushButton#managerGroupAddBtn:hover { border-color: %7; color: %7; }"
        "QLabel#managerTitle { color: %2; font-size: 18px; font-weight: 600; }"
        "QCheckBox#managerSelectAll { color: %6; font-size: 12px; }"
        "QTableWidget#managerTable { background-color: %1; border: none; color: %2; }"
        "QTableWidget#managerTable::item { padding: 6px 8px; border-bottom: 1px solid %4; }"
        "QComboBox { background: %3; color: %2; border: 1px solid %4; border-radius: 4px; padding: 2px 6px; }"
        "QComboBox QAbstractItemView { background: %1; color: %2; selection-background-color: %5; }"
        "QHeaderView::section { background-color: %1; color: %2; border: none; border-bottom: 1px solid %4; padding: 6px 8px; font-weight: 600; }"
    ).arg(tm->backgroundColor().name())            // %1
     .arg(tm->textColor().name())                  // %2
     .arg(tm->backgroundSecondaryColor().name())    // %3
     .arg(tm->borderColor().name())                 // %4
     .arg(tm->primarySoftColor().name())            // %5
     .arg(tm->textSecondaryColor().name())          // %6
     .arg(tm->primaryColor().name()));              // %7
}

void FriendManagerDialog::setFriendList(const QStringList& friendIds,
                                        const QMap<QString, QString>& friendNames,
                                        const QMap<QString, QString>& friendGroups,
                                        const QStringList& customGroups)
{
    m_friendIds = friendIds;
    m_friendNames = friendNames;
    m_friendGroups = friendGroups;
    m_customGroups = customGroups;
    rebuildGroupRail();
}

