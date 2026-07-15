#include "dialogs/creategroupdialog.h"

#include "theme/thememanager.h"
#include "theme/dialogstyle.h"
#include "widgets/dialogtitlebar.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QStackedWidget>

namespace {
// Category options mirror tauri-qqnt's GROUP_CATEGORY_SECTIONS (flattened).
const QStringList kCategories = {
    QStringLiteral("同学"), QStringLiteral("家人"), QStringLiteral("朋友"),
    QStringLiteral("同事"), QStringLiteral("兴趣爱好"), QStringLiteral("学习交流"),
    QStringLiteral("游戏"), QStringLiteral("其他"),
};
constexpr int kStepCount = 3;
}

CreateGroupDialog::CreateGroupDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("createGroupDialog"));
    setWindowTitle(QStringLiteral("创建群聊"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(480, 460);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &CreateGroupDialog::updateStyle);
}

void CreateGroupDialog::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    DialogTitleBar* titleBar = new DialogTitleBar(this, QStringLiteral("创建群聊"));
    connect(titleBar, &DialogTitleBar::closeRequested, this, &QDialog::reject);
    root->addWidget(titleBar);

    QWidget* body = new QWidget(this);
    QVBoxLayout* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(20, 16, 20, 16);
    bodyLayout->setSpacing(12);

    // Step indicator.
    m_stepLabel = new QLabel(body);
    m_stepLabel->setObjectName(QStringLiteral("wizardStepLabel"));
    bodyLayout->addWidget(m_stepLabel);

    // Steps.
    m_stack = new QStackedWidget(body);
    m_stack->addWidget(buildMemberStep());
    m_stack->addWidget(buildCategoryStep());
    m_stack->addWidget(buildInfoStep());
    bodyLayout->addWidget(m_stack, 1);

    // Nav row.
    QHBoxLayout* navRow = new QHBoxLayout();
    m_backBtn = new QPushButton(QStringLiteral("上一步"), body);
    m_backBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    m_backBtn->setCursor(Qt::PointingHandCursor);
    connect(m_backBtn, &QPushButton::clicked, this, [this]() { goToStep(m_step - 1); });
    navRow->addWidget(m_backBtn);
    navRow->addStretch();

    QPushButton* cancelBtn = new QPushButton(QStringLiteral("取消"), body);
    cancelBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    navRow->addWidget(cancelBtn);

    m_nextBtn = new QPushButton(body);
    m_nextBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        if (m_step < kStepCount - 1) {
            goToStep(m_step + 1);
            return;
        }
        // Final step: validate name + agreement, then emit.
        const QString name = m_nameEdit->text().trimmed();
        if (name.isEmpty()) {
            m_nameEdit->setFocus();
            return;
        }
        if (!m_agreed) {
            return;
        }
        emit createRequested(name, selectedMembers());
        accept();
    });
    navRow->addWidget(m_nextBtn);
    bodyLayout->addLayout(navRow);

    root->addWidget(body, 1);

    goToStep(0);
}

QWidget* CreateGroupDialog::buildMemberStep()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_memberSearch = new QLineEdit(page);
    m_memberSearch->setObjectName(QStringLiteral("dialogInput"));
    m_memberSearch->setPlaceholderText(QStringLiteral("搜索好友"));
    m_memberSearch->setClearButtonEnabled(true);
    connect(m_memberSearch, &QLineEdit::textChanged, this, [this]() { refreshMemberFilter(); });
    layout->addWidget(m_memberSearch);

    m_memberList = new QListWidget(page);
    m_memberList->setObjectName(QStringLiteral("wizardMemberList"));
    connect(m_memberList, &QListWidget::itemChanged, this, [this]() { updateStepChrome(); });
    layout->addWidget(m_memberList, 1);

    return page;
}

QWidget* CreateGroupDialog::buildCategoryStep()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    QLabel* hint = new QLabel(QStringLiteral("选择群分类"), page);
    hint->setObjectName(QStringLiteral("dialogHintLabel"));
    layout->addWidget(hint);

    m_categoryList = new QListWidget(page);
    m_categoryList->setObjectName(QStringLiteral("wizardCategoryList"));
    m_categoryList->addItems(kCategories);
    m_categoryList->setCurrentRow(0);
    layout->addWidget(m_categoryList, 1);

    return page;
}

QWidget* CreateGroupDialog::buildInfoStep()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel* nameHint = new QLabel(QStringLiteral("群名称"), page);
    nameHint->setObjectName(QStringLiteral("dialogHintLabel"));
    layout->addWidget(nameHint);

    m_nameEdit = new QLineEdit(page);
    m_nameEdit->setObjectName(QStringLiteral("dialogInput"));
    m_nameEdit->setPlaceholderText(QStringLiteral("输入群名称"));
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() { updateStepChrome(); });
    layout->addWidget(m_nameEdit);

    m_summaryLabel = new QLabel(page);
    m_summaryLabel->setObjectName(QStringLiteral("wizardSummary"));
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);

    layout->addStretch();

    // Agreement toggle (checkable button acting as a checkbox).
    QHBoxLayout* agreeRow = new QHBoxLayout();
    agreeRow->setSpacing(8);
    m_agreeCheck = new QPushButton(page);
    m_agreeCheck->setObjectName(QStringLiteral("wizardAgreeCheck"));
    m_agreeCheck->setCheckable(true);
    m_agreeCheck->setCursor(Qt::PointingHandCursor);
    m_agreeCheck->setFixedSize(18, 18);
    connect(m_agreeCheck, &QPushButton::toggled, this, [this](bool checked) {
        m_agreed = checked;
        m_agreeCheck->setText(checked ? QStringLiteral("✓") : QString());
        updateStepChrome();
    });
    agreeRow->addWidget(m_agreeCheck, 0, Qt::AlignVCenter);
    m_agreeLabel = new QLabel(QStringLiteral("我已阅读并同意群组管理协议"), page);
    m_agreeLabel->setObjectName(QStringLiteral("dialogHintLabel"));
    agreeRow->addWidget(m_agreeLabel, 1);
    layout->addLayout(agreeRow);

    return page;
}

void CreateGroupDialog::goToStep(int step)
{
    m_step = qBound(0, step, kStepCount - 1);
    m_stack->setCurrentIndex(m_step);
    if (m_step == kStepCount - 1) {
        // Refresh the summary shown on the final step.
        const int count = selectedMemberCount();
        m_summaryLabel->setText(
            QStringLiteral("已选成员：%1 人 · 分类：%2")
                .arg(count)
                .arg(selectedCategory()));
    }
    updateStepChrome();
}

void CreateGroupDialog::updateStepChrome()
{
    if (!m_stepLabel) return;
    static const char* titles[] = {"第 1 步 / 3 · 选择成员",
                                    "第 2 步 / 3 · 选择分类",
                                    "第 3 步 / 3 · 填写信息"};
    m_stepLabel->setText(QString::fromUtf8(titles[m_step]));
    m_backBtn->setEnabled(m_step > 0);
    m_backBtn->setVisible(m_step > 0);

    if (m_step < kStepCount - 1) {
        m_nextBtn->setText(QStringLiteral("下一步"));
        m_nextBtn->setEnabled(true);
    } else {
        m_nextBtn->setText(QStringLiteral("创建"));
        // Creation requires a non-empty name and the agreement checked.
        const bool ready = m_nameEdit && !m_nameEdit->text().trimmed().isEmpty() && m_agreed;
        m_nextBtn->setEnabled(ready);
    }
}

void CreateGroupDialog::refreshMemberFilter()
{
    if (!m_memberList) return;
    const QString q = m_memberSearch ? m_memberSearch->text().trimmed() : QString();
    for (int i = 0; i < m_memberList->count(); ++i) {
        QListWidgetItem* item = m_memberList->item(i);
        const QString id = item->data(Qt::UserRole).toString();
        const bool match = q.isEmpty()
            || item->text().contains(q, Qt::CaseInsensitive)
            || id.contains(q, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

int CreateGroupDialog::selectedMemberCount() const
{
    int count = 0;
    if (!m_memberList) return 0;
    for (int i = 0; i < m_memberList->count(); ++i) {
        if (m_memberList->item(i)->checkState() == Qt::Checked) {
            ++count;
        }
    }
    return count;
}

void CreateGroupDialog::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#createGroupDialog { background-color: %1; }"
        "QLabel#dialogHintLabel { color: %3; font-size: 13px; }"
        "QLabel#wizardStepLabel { color: %2; font-size: 14px; font-weight: 600; }"
        "QLabel#wizardSummary { color: %3; font-size: 13px; }"
        "QLineEdit#dialogInput { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 12px; }"
        "QLineEdit#dialogInput:focus { border: 1px solid %6; }"
        "QPushButton#dialogPrimaryBtn { background-color: %6; color: white; border: none; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogPrimaryBtn:hover { background-color: %7; }"
        "QPushButton#dialogPrimaryBtn:disabled { background-color: %5; color: %3; }"
        "QPushButton#dialogSecondaryBtn { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogSecondaryBtn:hover { background-color: %5; }"
        "QPushButton#wizardAgreeCheck { background-color: %4; color: %6; border: 1px solid %5; border-radius: 4px; font-weight: 700; }"
        "QPushButton#wizardAgreeCheck:checked { background-color: %6; color: white; border: 1px solid %6; }"
        "QListWidget#wizardMemberList, QListWidget#wizardCategoryList { background-color: %4; border: 1px solid %5; border-radius: 6px; color: %2; }"
        "QListWidget#wizardMemberList::item, QListWidget#wizardCategoryList::item { padding: 8px 12px; }"
        "QListWidget#wizardCategoryList::item:selected { background-color: %8; color: %2; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->primaryHoverColor().name())
     .arg(tm->primarySoftColor().name()));
}

void CreateGroupDialog::setCandidateMembers(const QStringList& memberIds,
                                            const QMap<QString, QString>& names)
{
    if (!m_memberList) return;
    m_memberList->clear();
    for (const QString& id : memberIds) {
        if (id.isEmpty()) {
            continue;
        }
        QListWidgetItem* item = new QListWidgetItem(names.value(id, id), m_memberList);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    updateStepChrome();
}

QString CreateGroupDialog::groupName() const
{
    return m_nameEdit ? m_nameEdit->text() : QString();
}

QStringList CreateGroupDialog::selectedMembers() const
{
    QStringList members;
    if (!m_memberList) return members;
    for (int i = 0; i < m_memberList->count(); ++i) {
        QListWidgetItem* item = m_memberList->item(i);
        if (item->checkState() == Qt::Checked) {
            members.append(item->data(Qt::UserRole).toString());
        }
    }
    return members;
}

QString CreateGroupDialog::selectedCategory() const
{
    if (m_categoryList && m_categoryList->currentItem()) {
        return m_categoryList->currentItem()->text();
    }
    return kCategories.isEmpty() ? QString() : kCategories.first();
}
