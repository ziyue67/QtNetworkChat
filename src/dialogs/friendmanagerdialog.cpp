#include "dialogs/friendmanagerdialog.h"

#include "theme/dialogstyle.h"
#include "theme/thememanager.h"
#include "widgets/avatarlabel.h"
#include "widgets/dialogtitlebar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QApplication>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QStyle>
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

QIcon actionIcon(QStyle::StandardPixmap icon, const QColor& color)
{
    QIcon base = qApp->style()->standardIcon(icon);
    QPixmap pixmap = base.pixmap(22, 22);
    if (pixmap.isNull()) return base;
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
    return QIcon(pixmap);
}

QWidget* groupRailRow(QWidget* parent, const QString& name, int count)
{
    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("managerGroupRow"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(8);
    auto* title = new QLabel(name, row);
    title->setObjectName(QStringLiteral("managerGroupName"));
    title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* value = new QLabel(QString::number(count), row);
    value->setObjectName(QStringLiteral("managerGroupCount"));
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(title, 1);
    layout->addWidget(value);
    return row;
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

bool confirmFriendDeletion(QWidget* parent, int count)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("friendDeleteDialog"));
    dialog.setWindowTitle(QStringLiteral("删除好友"));
    dialog.setModal(true);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground);
    dialog.setFixedSize(390, 176);
    ThemeManager* tm = ThemeManager::instance();
    dialog.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#friendDeleteDialog { background: transparent; }"
        "QFrame#friendDeleteSurface { background: %1; border: 1px solid %4; border-radius: 8px; }"
        "QLabel#friendDeleteTitle { color: %2; font-size: 16px; font-weight: 600; }"
        "QLabel#friendDeleteMessage { color: %3; font-size: 13px; }"
        "QPushButton#friendGroupCloseBtn { background: transparent; border: none; border-radius: 4px; padding: 0; }"
        "QPushButton#friendGroupCloseBtn:hover { background: %5; }"
    ).arg(tm->backgroundColor().name(), tm->textColor().name(), tm->textSecondaryColor().name(),
          tm->borderColor().name(), tm->backgroundSecondaryColor().name()));

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(10, 10, 10, 10);
    auto* surface = new QFrame(&dialog);
    surface->setObjectName(QStringLiteral("friendDeleteSurface"));
    root->addWidget(surface);
    auto* layout = new QVBoxLayout(surface);
    layout->setContentsMargins(18, 12, 18, 12);
    layout->setSpacing(8);
    auto* top = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("删除好友"), surface);
    title->setObjectName(QStringLiteral("friendDeleteTitle"));
    top->addWidget(title);
    top->addStretch();
    auto* close = new QPushButton(surface);
    close->setObjectName(QStringLiteral("friendGroupCloseBtn"));
    close->setFixedSize(28, 28);
    close->setIcon(closeIcon(tm->textSecondaryColor()));
    close->setIconSize(QSize(16, 16));
    close->setToolTip(QStringLiteral("关闭"));
    top->addWidget(close);
    layout->addLayout(top);
    auto* message = new QLabel(QStringLiteral("确定删除选中的 %1 位好友吗？删除后可重新搜索并添加。").arg(count), surface);
    message->setObjectName(QStringLiteral("friendDeleteMessage"));
    message->setWordWrap(true);
    layout->addWidget(message);
    layout->addStretch();
    auto* actions = new QHBoxLayout();
    actions->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("取消"), surface);
    cancel->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    cancel->setFixedSize(78, 32);
    auto* remove = new QPushButton(QStringLiteral("删除"), surface);
    remove->setObjectName(QStringLiteral("dialogDangerBtn"));
    remove->setFixedSize(78, 32);
    actions->addWidget(cancel);
    actions->addWidget(remove);
    layout->addLayout(actions);
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(remove, &QPushButton::clicked, &dialog, &QDialog::accept);
    return dialog.exec() == QDialog::Accepted;
}

void showFriendSelectionHint(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("friendSelectionHintDialog"));
    dialog.setWindowTitle(QStringLiteral("删除好友"));
    dialog.setModal(true);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground);
    dialog.setFixedSize(340, 144);
    ThemeManager* tm = ThemeManager::instance();
    dialog.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#friendSelectionHintDialog { background: transparent; }"
        "QFrame#friendSelectionHintSurface { background: %1; border: 1px solid %4; border-radius: 8px; }"
        "QLabel#friendSelectionHintText { color: %2; font-size: 14px; }"
    ).arg(tm->backgroundColor().name(), tm->textColor().name(), tm->textSecondaryColor().name(), tm->borderColor().name()));
    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(10, 10, 10, 10);
    auto* surface = new QFrame(&dialog);
    surface->setObjectName(QStringLiteral("friendSelectionHintSurface"));
    root->addWidget(surface);
    auto* layout = new QVBoxLayout(surface);
    layout->setContentsMargins(18, 18, 18, 14);
    auto* text = new QLabel(QStringLiteral("请先勾选要删除的好友"), surface);
    text->setObjectName(QStringLiteral("friendSelectionHintText"));
    layout->addWidget(text);
    layout->addStretch();
    auto* actions = new QHBoxLayout();
    actions->addStretch();
    auto* confirm = new QPushButton(QStringLiteral("知道了"), surface);
    confirm->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    confirm->setFixedSize(78, 32);
    confirm->setDefault(true);
    actions->addWidget(confirm);
    layout->addLayout(actions);
    QObject::connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
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

    // ---- Left group rail -------------------------------------------------
    QWidget* rail = new QWidget(body);
    rail->setObjectName(QStringLiteral("managerRail"));
    rail->setFixedWidth(204);
    QVBoxLayout* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(8, 12, 8, 12);
    railLayout->setSpacing(8);

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

    m_addGroupBtn = new QPushButton(QStringLiteral("+  添加分组"), rail);
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
    mainLayout->setContentsMargins(20, 18, 20, 12);
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
    m_searchEdit->setFixedWidth(190);
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
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setAlternatingRowColors(false);
    m_table->verticalHeader()->setDefaultSectionSize(52);
    m_table->horizontalHeader()->setFixedHeight(40);
    m_table->setColumnWidth(kColCheck, 38);
    m_table->horizontalHeader()->setSectionResizeMode(kColCheck, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColRemark, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColGroup, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColPermission, QHeaderView::Stretch);

    m_selectAll = new QCheckBox(m_table);
    m_selectAll->setObjectName(QStringLiteral("managerSelectAll"));
    m_selectAll->setToolTip(QStringLiteral("全选当前列表"));
    connect(m_selectAll, &QCheckBox::clicked, this, [this](bool checked) {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (auto* cb = qobject_cast<QCheckBox*>(m_table->cellWidget(row, kColCheck))) {
                cb->setChecked(checked);
            }
        }
        updateSelectAllState();
    });
    m_selectAll->setParent(m_table->horizontalHeader());
    m_selectAll->move(10, 10);
    m_selectAll->show();
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized, this,
            [this]() { m_selectAll->move(10, 10); });
    QWidget* tableArea = new QWidget(main);
    auto* tableStack = new QStackedLayout(tableArea);
    tableStack->setStackingMode(QStackedLayout::StackAll);
    tableStack->setContentsMargins(0, 0, 0, 0);
    tableStack->addWidget(m_table);
    m_emptyLabel = new QLabel(tableArea);
    m_emptyLabel->setObjectName(QStringLiteral("managerEmptyState"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    tableStack->addWidget(m_emptyLabel);
    mainLayout->addWidget(tableArea, 1);

    m_actionBar = new QWidget(main);
    m_actionBar->setObjectName(QStringLiteral("managerActionBar"));
    m_actionBar->setFixedHeight(58);
    m_actionBar->setVisible(false);
    QHBoxLayout* actionLayout = new QHBoxLayout(m_actionBar);
    actionLayout->setContentsMargins(12, 7, 8, 7);
    actionLayout->setSpacing(6);
    m_selectedLabel = new QLabel(QStringLiteral("已选 0 人"), m_actionBar);
    m_selectedLabel->setObjectName(QStringLiteral("managerSelectedLabel"));
    actionLayout->addWidget(m_selectedLabel);
    actionLayout->addStretch();
    auto addAction = [this, actionLayout](const QString& text, QStyle::StandardPixmap icon,
                                                      const QString& objectName, auto handler) {
        auto* button = new QPushButton(text, m_actionBar);
        button->setObjectName(objectName);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(34);
        button->setIcon(actionIcon(icon, ThemeManager::instance()->textColor()));
        button->setIconSize(QSize(18, 18));
        button->setToolTip(text);
        connect(button, &QPushButton::clicked, this, handler);
        actionLayout->addWidget(button);
    };
    addAction(QStringLiteral("设置分组"), QStyle::SP_DirIcon,
              QStringLiteral("managerActionBtn"), [this]() { moveSelectedFriendsToGroup(); });
    m_deleteBtn = new QPushButton(QStringLiteral("删除好友"), m_actionBar);
    m_deleteBtn->setObjectName(QStringLiteral("managerDangerActionBtn"));
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setFixedHeight(34);
    m_deleteBtn->setIcon(actionIcon(QStyle::SP_TrashIcon, ThemeManager::instance()->dangerColor()));
    m_deleteBtn->setIconSize(QSize(18, 18));
    m_deleteBtn->setToolTip(QStringLiteral("删除选中的好友"));
    connect(m_deleteBtn, &QPushButton::clicked, this, &FriendManagerDialog::deleteSelectedFriends);
    actionLayout->addWidget(m_deleteBtn);
    addAction(QStringLiteral("关闭"), QStyle::SP_DialogCloseButton,
              QStringLiteral("managerActionBtn"), [this]() { reject(); });
    mainLayout->addWidget(m_actionBar);

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
        auto* item = new QListWidgetItem(m_groupList);
        item->setData(Qt::UserRole, name);
        item->setSizeHint(QSize(0, 38));
        m_groupList->setItemWidget(item, groupRailRow(m_groupList, name, count));
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
        m_table->setRowHeight(row, 52);

        QWidget* checkHolder = new QWidget(m_table);
        QHBoxLayout* checkLayout = new QHBoxLayout(checkHolder);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->setAlignment(Qt::AlignCenter);
        QCheckBox* cb = new QCheckBox(checkHolder);
        connect(cb, &QCheckBox::clicked, this, [this]() { updateSelectAllState(); });
        checkLayout->addWidget(cb);
        m_table->setCellWidget(row, kColCheck, cb);

        auto* nameCell = new QWidget(m_table);
        nameCell->setObjectName(QStringLiteral("managerNameCell"));
        auto* nameLayout = new QHBoxLayout(nameCell);
        nameLayout->setContentsMargins(7, 0, 6, 0);
        nameLayout->setSpacing(9);
        auto* avatar = new AvatarLabel(nameCell, 30);
        const QString avatarPath = m_avatarPaths.value(id);
        if (!avatarPath.isEmpty()) {
            const QPixmap avatarPixmap(avatarPath);
            if (!avatarPixmap.isNull()) avatar->setPixmap(avatarPixmap);
            else avatar->setTextAvatar(name, ThemeManager::instance()->primaryColor());
        } else {
            avatar->setTextAvatar(name, ThemeManager::instance()->primaryColor());
        }
        auto* nameLabel = new QLabel(name, nameCell);
        nameLabel->setObjectName(QStringLiteral("managerFriendName"));
        nameLabel->setTextFormat(Qt::PlainText);
        nameLayout->addWidget(avatar);
        nameLayout->addWidget(nameLabel, 1);
        m_table->setCellWidget(row, kColName, nameCell);

        QTableWidgetItem* nameItem = new QTableWidgetItem;
        nameItem->setData(Qt::UserRole, id);
        m_table->setItem(row, kColName, nameItem);
        m_table->setItem(row, kColRemark, new QTableWidgetItem(QStringLiteral("-")));

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
    const bool hasRows = m_table->rowCount() > 0;
    if (m_emptyLabel) {
        const bool hasFilter = !m_filter.isEmpty();
        m_emptyLabel->setText(hasFilter
            ? QStringLiteral("未找到匹配的好友\n换个关键词试试")
            : QStringLiteral("暂无好友\n添加好友后可在这里整理分组和权限"));
        m_emptyLabel->setVisible(!hasRows);
    }
    m_selectAll->setVisible(hasRows);
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
    m_selectAll->setTristate(total > 0 && checked > 0 && checked < total);
    if (m_selectedLabel) {
        m_selectedLabel->setText(QStringLiteral("已选 %1 人").arg(checked));
    }
    if (m_actionBar) {
        m_actionBar->setVisible(checked > 0);
    }
}

QStringList FriendManagerDialog::selectedFriendIds() const
{
    QStringList selected;
    if (!m_table) return selected;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* check = qobject_cast<QCheckBox*>(m_table->cellWidget(row, kColCheck));
        QTableWidgetItem* item = m_table->item(row, kColName);
        if (check && check->isChecked() && item) {
            selected << item->data(Qt::UserRole).toString();
        }
    }
    return selected;
}

void FriendManagerDialog::deleteSelectedFriends()
{
    const QStringList selected = selectedFriendIds();
    if (selected.isEmpty()) {
        showFriendSelectionHint(this);
        return;
    }
    if (!confirmFriendDeletion(this, selected.size())) return;
    for (const QString& id : selected) {
        emit deleteFriendRequested(id);
        m_friendIds.removeAll(id);
        m_friendGroups.remove(id);
        m_avatarPaths.remove(id);
    }
    rebuildGroupRail();
}

void FriendManagerDialog::moveSelectedFriendsToGroup()
{
    const QStringList selected = selectedFriendIds();
    if (selected.isEmpty()) {
        showFriendSelectionHint(this);
        return;
    }
    QStringList groups;
    groups << kDefaultGroup;
    for (const QString& group : m_customGroups) {
        if (!groups.contains(group)) groups << group;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("friendBatchGroupDialog"));
    dialog.setWindowTitle(QStringLiteral("设置分组"));
    dialog.setModal(true);
    dialog.setFixedSize(310, 156);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 16);
    auto* hint = new QLabel(QStringLiteral("将 %1 位好友移动到").arg(selected.size()), &dialog);
    hint->setObjectName(QStringLiteral("friendBatchGroupHint"));
    auto* combo = new QComboBox(&dialog);
    combo->addItems(groups);
    auto* actions = new QHBoxLayout();
    actions->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("取消"), &dialog);
    cancel->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    auto* apply = new QPushButton(QStringLiteral("确定"), &dialog);
    apply->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    actions->addWidget(cancel);
    actions->addWidget(apply);
    layout->addWidget(hint);
    layout->addWidget(combo);
    layout->addLayout(actions);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(apply, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString group = combo->currentText();
    for (const QString& id : selected) {
        m_friendGroups[id] = group;
        emit moveFriendToGroupRequested(id, group);
    }
    rebuildGroupRail();
}

void FriendManagerDialog::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#friendManagerDialog { background-color: %1; }"
        "QWidget#managerRail { background-color: %3; border-right: 1px solid %4; }"
        "QLabel#managerGroupCaption { color: %6; font-size: 12px; padding: 4px 10px 4px; }"
        "QListWidget#managerGroupList { background-color: %3; border: none; color: %2; outline: none; }"
        "QListWidget#managerGroupList::item { margin: 2px 0; border-radius: 6px; }"
        "QListWidget#managerGroupList::item:hover { background-color: %1; }"
        "QListWidget#managerGroupList::item:selected { background-color: %5; color: %2; }"
        "QWidget#managerGroupRow { background: transparent; }"
        "QLabel#managerGroupName { color: %2; font-size: 13px; }"
        "QLabel#managerGroupCount { color: %6; font-size: 12px; }"
        "QPushButton#managerGroupAddBtn { background: %1; color: %2; border: 1px solid %4; border-radius: 6px; padding: 0 10px; font-size: 13px; text-align: center; }"
        "QPushButton#managerGroupAddBtn:hover { background: %5; border-color: %7; color: %7; }"
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
        "QCheckBox#managerSelectAll { spacing: 0; }"
        "QCheckBox#managerSelectAll::indicator, QTableWidget QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid %4; border-radius: 8px; background: %1; }"
        "QCheckBox#managerSelectAll::indicator:checked, QTableWidget QCheckBox::indicator:checked { background: %7; border-color: %7; }"
        "QTableWidget#managerTable { background-color: %1; border: 1px solid %4; border-radius: 6px; color: %2; }"
        "QTableWidget#managerTable::item { padding: 6px 8px; border-bottom: 1px solid %4; }"
        "QTableWidget#managerTable::item:selected { background: %5; color: %2; }"
        "QWidget#managerNameCell { background: transparent; }"
        "QLabel#managerFriendName { color: %2; font-size: 13px; }"
        "QComboBox { background: transparent; color: %2; border: none; border-radius: 4px; padding: 2px 20px 2px 4px; }"
        "QComboBox:hover { background: %3; }"
        "QComboBox QAbstractItemView { background: %1; color: %2; selection-background-color: %5; }"
        "QHeaderView::section { background-color: %1; color: %6; border: none; border-bottom: 1px solid %4; padding: 6px 8px; font-weight: 600; font-size: 13px; }"
        "QLabel#managerEmptyState { color: %6; font-size: 13px; line-height: 1.7; background: %1; border: 1px solid %4; border-radius: 6px; }"
        "QWidget#managerActionBar { background: %3; border: 1px solid %4; border-radius: 6px; }"
        "QLabel#managerSelectedLabel { color: %6; font-size: 13px; padding: 0 6px; }"
        "QPushButton#managerActionBtn, QPushButton#managerDangerActionBtn { background: transparent; border: 1px solid transparent; border-radius: 5px; color: %2; font-size: 12px; padding: 0 9px; text-align: center; }"
        "QPushButton#managerActionBtn:hover { background: %1; border-color: %4; }"
        "QPushButton#managerDangerActionBtn { color: %8; }"
        "QPushButton#managerDangerActionBtn:hover { background: %9; border-color: %8; }"
        "QDialog#friendBatchGroupDialog { background: %1; }"
        "QLabel#friendBatchGroupHint { color: %2; font-size: 13px; }"
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
                                        const QStringList& customGroups,
                                        const QMap<QString, QString>& avatarPaths)
{
    m_friendIds = friendIds;
    m_friendNames = friendNames;
    m_friendGroups = friendGroups;
    m_customGroups = customGroups;
    m_avatarPaths = avatarPaths;
    rebuildGroupRail();
}

