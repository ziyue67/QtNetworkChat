#include "dialogs/friendmanagerdialog.h"

#include "theme/dialogstyle.h"
#include "theme/thememanager.h"
#include "widgets/dialogtitlebar.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int kColCheck = 0;
constexpr int kColName = 1;
constexpr int kColRemark = 2;
constexpr int kColGroup = 3;
constexpr int kColPermission = 4;
// The "全部好友" rail entry is always present even without real group data.
const QString kAllFriendsGroup = QStringLiteral("全部好友");
}

FriendManagerDialog::FriendManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("friendManagerDialog"));
    setWindowTitle(QStringLiteral("好友管理器"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(840, 600);
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

    // Left group rail (fixed 200px, matches FriendManagerModal aside).
    m_groupList = new QListWidget(body);
    m_groupList->setObjectName(QStringLiteral("managerGroupList"));
    m_groupList->setFixedWidth(200);
    m_groupList->addItem(kAllFriendsGroup);
    m_groupList->setCurrentRow(0);
    connect(m_groupList, &QListWidget::currentRowChanged, this, [this]() { rebuildTable(); });
    bodyLayout->addWidget(m_groupList);

    // Right main panel.
    QWidget* main = new QWidget(body);
    QVBoxLayout* mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(20, 18, 20, 16);
    mainLayout->setSpacing(14);

    // Header row: title + search box.
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

    // Table: checkbox / name / remark / group / permission.
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

    // "Select all" row above the table (QTableWidget has no natural header
    // checkbox slot, so give it its own labelled control).
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

    // Bottom action row.
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
                if (nameItem) {
                    selected << nameItem->data(Qt::UserRole).toString();
                }
            }
        }
        if (selected.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("删除好友"),
                                     QStringLiteral("请先勾选要删除的好友"));
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
}

void FriendManagerDialog::rebuildTable()
{
    if (!m_table) return;
    m_table->setRowCount(0);
    for (const QString& id : m_friendIds) {
        const QString name = m_friendNames.value(id, id);
        if (!m_filter.isEmpty()
            && !id.contains(m_filter, Qt::CaseInsensitive)
            && !name.contains(m_filter, Qt::CaseInsensitive)) {
            continue;
        }
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        // Checkbox cell, centered.
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
        m_table->setItem(row, kColRemark, new QTableWidgetItem(QStringLiteral("-")));
        m_table->setItem(row, kColGroup, new QTableWidgetItem(kAllFriendsGroup));
        m_table->setItem(row, kColPermission, new QTableWidgetItem(QStringLiteral("正常")));
    }
    updateSelectAllState();

    // Keep the rail count label in sync.
    if (m_groupList->count() > 0) {
        m_groupList->item(0)->setText(
            QStringLiteral("%1  (%2)").arg(kAllFriendsGroup).arg(m_friendIds.size()));
    }
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
    // Shared input/button rules from DialogStyle::common(); only the manager
    // layout (group rail, title, select-all, table, header) is defined here.
    setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#friendManagerDialog { background-color: %1; }"
        "QListWidget#managerGroupList { background-color: %3; border: none; border-right: 1px solid %4; color: %2; outline: none; padding: 8px; }"
        "QListWidget#managerGroupList::item { padding: 8px 10px; border-radius: 6px; }"
        "QListWidget#managerGroupList::item:selected { background-color: %5; color: %2; }"
        "QLabel#managerTitle { color: %2; font-size: 18px; font-weight: 600; }"
        "QCheckBox#managerSelectAll { color: %6; font-size: 12px; }"
        "QTableWidget#managerTable { background-color: %1; border: none; color: %2; }"
        "QTableWidget#managerTable::item { padding: 6px 8px; border-bottom: 1px solid %4; }"
        "QHeaderView::section { background-color: %1; color: %2; border: none; border-bottom: 1px solid %4; padding: 6px 8px; font-weight: 600; }"
    ).arg(tm->backgroundColor().name())            // %1
     .arg(tm->textColor().name())                  // %2
     .arg(tm->backgroundSecondaryColor().name())    // %3
     .arg(tm->borderColor().name())                 // %4
     .arg(tm->primarySoftColor().name())            // %5
     .arg(tm->textSecondaryColor().name()));        // %6
}

void FriendManagerDialog::setFriendList(const QStringList& friendIds, const QMap<QString, QString>& friendNames)
{
    m_friendIds = friendIds;
    m_friendNames = friendNames;
    rebuildTable();
}
