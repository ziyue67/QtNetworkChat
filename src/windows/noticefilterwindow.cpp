#include "windows/noticefilterwindow.h"

#include "theme/thememanager.h"
#include "widgets/dialogtitlebar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

NoticeFilterWindow::NoticeFilterWindow(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("noticeFilterWindow"));
    setWindowTitle(QStringLiteral("通知筛选"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(300, 250);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &NoticeFilterWindow::updateStyle);
}

void NoticeFilterWindow::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    DialogTitleBar* titleBar = new DialogTitleBar(this, QStringLiteral("通知筛选设置"));
    connect(titleBar, &DialogTitleBar::closeRequested, this, &QDialog::reject);
    root->addWidget(titleBar);

    QWidget* body = new QWidget(this);
    QVBoxLayout* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(20, 20, 20, 20);
    bodyLayout->setSpacing(12);

    m_friendCheck = new QCheckBox(QStringLiteral("好友消息通知"), body);
    m_friendCheck->setObjectName(QStringLiteral("dialogCheck"));
    m_friendCheck->setChecked(true);
    bodyLayout->addWidget(m_friendCheck);

    m_groupCheck = new QCheckBox(QStringLiteral("群消息通知"), body);
    m_groupCheck->setObjectName(QStringLiteral("dialogCheck"));
    m_groupCheck->setChecked(true);
    bodyLayout->addWidget(m_groupCheck);

    m_mentionCheck = new QCheckBox(QStringLiteral("@提及通知"), body);
    m_mentionCheck->setObjectName(QStringLiteral("dialogCheck"));
    m_mentionCheck->setChecked(true);
    bodyLayout->addWidget(m_mentionCheck);

    QLabel* priorityLabel = new QLabel(QStringLiteral("优先级筛选:"), body);
    priorityLabel->setObjectName(QStringLiteral("dialogHintLabel"));
    bodyLayout->addWidget(priorityLabel);

    m_priorityCombo = new QComboBox(body);
    m_priorityCombo->setObjectName(QStringLiteral("dialogCombo"));
    m_priorityCombo->addItem(QStringLiteral("全部"), 0);
    m_priorityCombo->addItem(QStringLiteral("高优先级"), 1);
    m_priorityCombo->addItem(QStringLiteral("普通"), 2);
    bodyLayout->addWidget(m_priorityCombo);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* confirmBtn = new QPushButton(QStringLiteral("确认"), body);
    confirmBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    connect(confirmBtn, &QPushButton::clicked, this, [this]() {
        emit filterChanged();
        accept();
    });
    btnLayout->addWidget(confirmBtn);

    QPushButton* cancelBtn = new QPushButton(QStringLiteral("取消"), body);
    cancelBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    bodyLayout->addLayout(btnLayout);

    root->addWidget(body, 1);

    connect(m_friendCheck, &QCheckBox::toggled, this, &NoticeFilterWindow::filterChanged);
    connect(m_groupCheck, &QCheckBox::toggled, this, &NoticeFilterWindow::filterChanged);
    connect(m_mentionCheck, &QCheckBox::toggled, this, &NoticeFilterWindow::filterChanged);
    connect(m_priorityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NoticeFilterWindow::filterChanged);
}

void NoticeFilterWindow::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#noticeFilterWindow { background-color: %1; }"
        "QLabel#dialogHintLabel { color: %3; font-size: 13px; }"
        "QCheckBox#dialogCheck { color: %2; }"
        "QCheckBox#dialogCheck::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid %5; }"
        "QCheckBox#dialogCheck::indicator:checked { background-color: %4; border: 1px solid %4; }"
        "QComboBox#dialogCombo { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 12px; }"
        "QComboBox#dialogCombo:focus { border: 1px solid %6; }"
        "QPushButton#dialogPrimaryBtn { background-color: %6; color: white; border: none; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogPrimaryBtn:hover { background-color: %7; }"
        "QPushButton#dialogSecondaryBtn { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogSecondaryBtn:hover { background-color: %5; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->primaryHoverColor().name()));
}

bool NoticeFilterWindow::friendNotificationsEnabled() const
{
    return m_friendCheck->isChecked();
}

bool NoticeFilterWindow::groupNotificationsEnabled() const
{
    return m_groupCheck->isChecked();
}

bool NoticeFilterWindow::mentionNotificationsEnabled() const
{
    return m_mentionCheck->isChecked();
}

int NoticeFilterWindow::priorityFilter() const
{
    return m_priorityCombo->currentData().toInt();
}

