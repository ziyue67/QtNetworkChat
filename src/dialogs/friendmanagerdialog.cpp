#include "dialogs/friendmanagerdialog.h"

#include "theme/dialogstyle.h"
#include "theme/thememanager.h"
#include "widgets/dialogtitlebar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPainter>
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

QIcon closeIcon(const QColor& color)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(4.5, 4.5), QPointF(11.5, 11.5));
    painter.drawLine(QPointF(11.5, 4.5), QPointF(4.5, 11.5));
    return QIcon(pixmap);
}

QString requestGroupName(QWidget* parent, const QString& title, const QString& initialValue,
                         const QStringList& unavailableNames)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("friendGroupEditDialog"));
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground);
    dialog.setFixedSize(380, 250);
    ThemeManager* tm = ThemeManager::instance();
    dialog.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#friendGroupEditDialog { background: transparent; }"
        "QFrame#friendGroupEditSurface { background: %1; border: 1px solid %5; border-radius: 8px; }"
        "QWidget#friendGroupEditHeader { border-bottom: 1px solid %5; }"
        "QLabel#friendGroupEditTitle { color: %2; font-size: 16px; font-weight: 600; }"
        "QLabel#friendGroupEditSubtitle { color: %4; font-size: 12px; }"
        "QLabel#friendGroupEditLabel { color: %2; font-size: 13px; }"
        "QLabel#friendGroupEditHint { color: %4; font-size: 12px; }"
        "QLabel#friendGroupEditHint[invalid=\"true\"] { color: %3; }"
        "QLabel#friendGroupEditCount { color: %4; font-size: 12px; }"
        "QFrame#friendGroupInputShell { background: %6; border: 1px solid %5; border-radius: 6px; }"
        "QFrame#friendGroupInputShell[focused=\"true\"] { border-color: %7; }"
        "QFrame#friendGroupInputShell[invalid=\"true\"] { border-color: %3; }"
        "QLineEdit#friendGroupNameInput { background: transparent; color: %2; border: none; padding: 0; font-size: 13px; }"
        "QPushButton#friendGroupCloseBtn { background: transparent; border: none; border-radius: 4px; padding: 0; }"
        "QPushButton#friendGroupCloseBtn:hover { background: %6; }"
        "QWidget#friendGroupEditFooter { background: %6; border-top: 1px solid %5; }"
    ).arg(tm->backgroundColor().name(), tm->textColor().name(), tm->dangerColor().name(),
          tm->textSecondaryColor().name(), tm->borderColor().name(),
          tm->backgroundSecondaryColor().name(), tm->primaryColor().name()));

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(0);
    auto* surface = new QFrame(&dialog);
    surface->setObjectName(QStringLiteral("friendGroupEditSurface"));
    auto* shadow = new QGraphicsDropShadowEffect(surface);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 8);
    shadow->setColor(QColor(15, 23, 42, tm->isDark() ? 110 : 48));
    surface->setGraphicsEffect(shadow);
    root->addWidget(surface);

    auto* layout = new QVBoxLayout(surface);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(surface);
    header->setObjectName(QStringLiteral("friendGroupEditHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 13, 12, 12);
    headerLayout->setSpacing(10);
    auto* headingColumn = new QVBoxLayout();
    headingColumn->setSpacing(2);
    auto* heading = new QLabel(title == QStringLiteral("添加分组")
                                   ? QStringLiteral("新建好友分组")
                                   : QStringLiteral("重命名好友分组"), header);
    heading->setObjectName(QStringLiteral("friendGroupEditTitle"));
    headingColumn->addWidget(heading);
    auto* subtitle = new QLabel(title == QStringLiteral("添加分组")
                                    ? QStringLiteral("创建后可在好友管理器中调整成员分组")
                                    : QStringLiteral("组内好友将同步使用新的分组名称"), header);
    subtitle->setObjectName(QStringLiteral("friendGroupEditSubtitle"));
    headingColumn->addWidget(subtitle);
    headerLayout->addLayout(headingColumn, 1);
    auto* close = new QPushButton(header);
    close->setObjectName(QStringLiteral("friendGroupCloseBtn"));
    close->setFixedSize(28, 28);
    close->setIcon(closeIcon(tm->textSecondaryColor()));
    close->setIconSize(QSize(16, 16));
    close->setToolTip(QStringLiteral("关闭"));
    close->setAccessibleName(QStringLiteral("关闭分组编辑"));
    headerLayout->addWidget(close, 0, Qt::AlignTop);
    layout->addWidget(header);

    auto* content = new QWidget(surface);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 14, 18, 12);
    contentLayout->setSpacing(7);
    auto* labelRow = new QHBoxLayout();
    auto* label = new QLabel(QStringLiteral("分组名称"), content);
    label->setObjectName(QStringLiteral("friendGroupEditLabel"));
    labelRow->addWidget(label);
    labelRow->addStretch();
    auto* count = new QLabel(content);
    count->setObjectName(QStringLiteral("friendGroupEditCount"));
    labelRow->addWidget(count);
    contentLayout->addLayout(labelRow);

    auto* inputShell = new QFrame(content);
    inputShell->setObjectName(QStringLiteral("friendGroupInputShell"));
    inputShell->setFixedHeight(38);
    auto* inputLayout = new QHBoxLayout(inputShell);
    inputLayout->setContentsMargins(11, 0, 11, 0);
    auto* input = new QLineEdit(initialValue, inputShell);
    input->setObjectName(QStringLiteral("friendGroupNameInput"));
    input->setPlaceholderText(QStringLiteral("例如：同事、游戏好友"));
    input->setMaxLength(24);
    input->setAccessibleName(QStringLiteral("分组名称"));
    inputLayout->addWidget(input);
    contentLayout->addWidget(inputShell);
    auto* hint = new QLabel(content);
    hint->setObjectName(QStringLiteral("friendGroupEditHint"));
    hint->setMinimumHeight(18);
    contentLayout->addWidget(hint);
    layout->addWidget(content, 1);

    auto* footer = new QWidget(surface);
    footer->setObjectName(QStringLiteral("friendGroupEditFooter"));
    auto* actions = new QHBoxLayout(footer);
    actions->setContentsMargins(18, 10, 18, 10);
    actions->setSpacing(8);
    actions->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("取消"), footer);
    cancel->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    cancel->setFixedHeight(32);
    auto* confirm = new QPushButton(title == QStringLiteral("添加分组")
                                        ? QStringLiteral("创建分组")
                                        : QStringLiteral("保存名称"), footer);
    confirm->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    confirm->setFixedHeight(32);
    confirm->setDefault(true);
    actions->addWidget(cancel);
    actions->addWidget(confirm);
    layout->addWidget(footer);

    const auto validationMessage = [&]() {
        const QString name = input->text().trimmed();
        if (name.isEmpty()) {
            return QStringLiteral("请输入分组名称");
        }
        if (unavailableNames.contains(name)) {
            return QStringLiteral("分组名称已存在或不可用");
        }
        return QString();
    };
    bool validationVisible = false;
    const auto refreshValidation = [&]() {
        const QString message = validationMessage();
        const bool showInvalid = validationVisible && !message.isEmpty();
        confirm->setEnabled(message.isEmpty());
        hint->setText(showInvalid ? message : QStringLiteral("分组名称将在好友列表中显示"));
        count->setText(QStringLiteral("%1/24").arg(input->text().size()));
        hint->setProperty("invalid", showInvalid);
        inputShell->setProperty("invalid", showInvalid);
        hint->setStyleSheet(QStringLiteral("color: %1;")
                                .arg(showInvalid ? tm->dangerColor().name()
                                                 : tm->textSecondaryColor().name()));
        inputShell->style()->unpolish(inputShell);
        inputShell->style()->polish(inputShell);
    };
    const auto submit = [&]() {
        if (!validationMessage().isEmpty()) {
            validationVisible = true;
            refreshValidation();
            input->setFocus();
            return;
        }
        dialog.accept();
    };
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(confirm, &QPushButton::clicked, &dialog, submit);
    QObject::connect(input, &QLineEdit::returnPressed, &dialog, submit);
    QObject::connect(input, &QLineEdit::textChanged, &dialog, [&]() {
        validationVisible = true;
        refreshValidation();
    });
    QObject::connect(input, &QLineEdit::selectionChanged, &dialog, [inputShell, input]() {
        inputShell->setProperty("focused", input->hasFocus());
        inputShell->style()->unpolish(inputShell);
        inputShell->style()->polish(inputShell);
    });
    input->selectAll();
    input->setFocus();
    refreshValidation();
    return dialog.exec() == QDialog::Accepted ? input->text().trimmed() : QString();
}

bool confirmGroupDeletion(QWidget* parent, const QString& groupName)
{
    Q_UNUSED(groupName)
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("friendGroupDeleteDialog"));
    dialog.setWindowTitle(QStringLiteral("删除分组"));
    dialog.setModal(true);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground);
    dialog.setFixedSize(380, 150);
    ThemeManager* tm = ThemeManager::instance();
    dialog.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#friendGroupDeleteDialog { background: transparent; }"
        "QFrame#friendGroupDeleteSurface { background: %1; border: 1px solid %4; border-radius: 8px; }"
        "QLabel#friendGroupDeleteMessage { color: %2; font-size: 13px; }"
        "QPushButton#friendGroupCloseBtn { background: transparent; border: none; border-radius: 4px; padding: 0; }"
        "QPushButton#friendGroupCloseBtn:hover { background: %5; }"
    ).arg(tm->backgroundColor().name(), tm->textColor().name(),
          tm->textSecondaryColor().name(), tm->borderColor().name(),
          tm->backgroundSecondaryColor().name()));

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(10, 10, 10, 10);
    auto* surface = new QFrame(&dialog);
    surface->setObjectName(QStringLiteral("friendGroupDeleteSurface"));
    auto* shadow = new QGraphicsDropShadowEffect(surface);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 8);
    shadow->setColor(QColor(15, 23, 42, tm->isDark() ? 110 : 48));
    surface->setGraphicsEffect(shadow);
    root->addWidget(surface);

    auto* layout = new QVBoxLayout(surface);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* closeRow = new QHBoxLayout();
    closeRow->setContentsMargins(18, 4, 4, 0);
    closeRow->addStretch();
    auto* close = new QPushButton(surface);
    close->setObjectName(QStringLiteral("friendGroupCloseBtn"));
    close->setFixedSize(28, 28);
    close->setIcon(closeIcon(tm->textSecondaryColor()));
    close->setIconSize(QSize(16, 16));
    close->setToolTip(QStringLiteral("关闭"));
    closeRow->addWidget(close);
    layout->addLayout(closeRow);

    auto* message = new QLabel(QStringLiteral("确定删除分组吗？"), surface);
    message->setObjectName(QStringLiteral("friendGroupDeleteMessage"));
    message->setContentsMargins(18, 0, 18, 0);
    layout->addWidget(message);
    layout->addStretch();

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(18, 0, 18, 12);
    actions->setSpacing(8);
    actions->addStretch();
    auto* confirm = new QPushButton(QStringLiteral("确定"), surface);
    confirm->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    confirm->setFixedSize(78, 32);
    confirm->setDefault(true);
    auto* cancel = new QPushButton(QStringLiteral("取消"), surface);
    cancel->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    cancel->setFixedSize(78, 32);
    actions->addWidget(confirm);
    actions->addWidget(cancel);
    layout->addLayout(actions);

    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
    return dialog.exec() == QDialog::Accepted;
}
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

    QLabel* groupCaption = new QLabel(QStringLiteral("分组"), rail);
    groupCaption->setObjectName(QStringLiteral("managerGroupCaption"));
    railLayout->addWidget(groupCaption);

    m_groupList = new QListWidget(rail);
    m_groupList->setObjectName(QStringLiteral("managerGroupList"));
    m_groupList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_groupList, &QListWidget::currentRowChanged, this, [this]() {
        rebuildTable();
    });
    connect(m_groupList, &QListWidget::customContextMenuRequested,
            this, &FriendManagerDialog::showGroupContextMenu);
    railLayout->addWidget(m_groupList, 1);

    // Adding is explicit; editing/removing stays on the selected group's context menu.
    m_addGroupBtn = new QPushButton(QStringLiteral("添加分组"), rail);
    m_addGroupBtn->setObjectName(QStringLiteral("managerGroupAddBtn"));
    m_addGroupBtn->setCursor(Qt::PointingHandCursor);
    m_addGroupBtn->setFixedHeight(34);
    m_addGroupBtn->setToolTip(QStringLiteral("创建一个新的好友分组"));
    connect(m_addGroupBtn, &QPushButton::clicked, this, [this]() {
        QStringList unavailable = m_customGroups;
        unavailable << kAllFriendsGroup << kDefaultGroup;
        const QString name = requestGroupName(this, QStringLiteral("添加分组"), QString(), unavailable);
        if (name.isEmpty()) return;
        m_customGroups.append(name);
        emit createGroupRequested(name);
        rebuildGroupRail();
    });
    railLayout->addWidget(m_addGroupBtn);

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
    menu.setObjectName(QStringLiteral("managerGroupContextMenu"));
    QAction* renameAction = menu.addAction(QStringLiteral("重命名"));
    menu.addSeparator();
    QAction* deleteAction = menu.addAction(QStringLiteral("删除分组"));
    deleteAction->setProperty("destructive", true);
    QAction* chosen = menu.exec(m_groupList->viewport()->mapToGlobal(position));
    if (chosen == renameAction) {
        QStringList unavailable = m_customGroups;
        unavailable.removeAll(groupName);
        unavailable << kAllFriendsGroup << kDefaultGroup;
        const QString newName = requestGroupName(this, QStringLiteral("重命名分组"), groupName, unavailable);
        if (newName.isEmpty() || newName == groupName) return;
        m_customGroups.replace(m_customGroups.indexOf(groupName), newName);
        for (auto it = m_friendGroups.begin(); it != m_friendGroups.end(); ++it) {
            if (it.value() == groupName) it.value() = newName;
        }
        emit renameGroupRequested(groupName, newName);
        rebuildGroupRail();
    } else if (chosen == deleteAction) {
        if (!confirmGroupDeletion(this, groupName)) return;
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
        "QLabel#managerGroupCaption { color: %6; font-size: 12px; padding: 4px 8px 2px; }"
        "QListWidget#managerGroupList { background-color: %3; border: none; color: %2; outline: none; }"
        "QListWidget#managerGroupList::item { padding: 9px 10px; margin: 1px 0; border-radius: 6px; }"
        "QListWidget#managerGroupList::item:hover { background-color: %1; }"
        "QListWidget#managerGroupList::item:selected { background-color: %5; color: %2; }"
        "QPushButton#managerGroupAddBtn { background: %1; color: %2; border: 1px solid %4; border-radius: 6px; padding: 0 10px; font-size: 13px; text-align: center; }"
        "QPushButton#managerGroupAddBtn:hover { background: %3; border-color: %7; color: %7; }"
        "QPushButton#managerGroupAddBtn:pressed { padding-top: 1px; }"
        "QMenu#managerGroupContextMenu { background: %1; color: %2; border: 1px solid %4; border-radius: 6px; padding: 4px; }"
        "QMenu#managerGroupContextMenu::item { padding: 7px 30px 7px 10px; border-radius: 4px; }"
        "QMenu#managerGroupContextMenu::item:selected { background: %5; }"
        "QMenu#managerGroupContextMenu::item[destructive=\"true\"] { color: %8; }"
        "QMenu#managerGroupContextMenu::item[destructive=\"true\"]:selected { background: %9; color: %8; }"
        "QDialog#friendGroupEditDialog { background: %1; }"
        "QLabel#friendGroupEditTitle { color: %2; font-size: 16px; font-weight: 600; }"
        "QLabel#friendGroupEditError { color: %8; font-size: 12px; }"
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
     .arg(tm->primaryColor().name())                // %7
     .arg(tm->dangerColor().name())                 // %8
     .arg(tm->dangerColor().lighter(185).name()));  // %9
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

