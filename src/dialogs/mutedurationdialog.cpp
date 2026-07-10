#include "dialogs/mutedurationdialog.h"

#include "theme/thememanager.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

MuteDurationDialog::MuteDurationDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("muteDurationDialog"));
    setWindowTitle(QStringLiteral("禁言设置"));
    setMinimumSize(300, 200);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &MuteDurationDialog::updateStyle);
}

void MuteDurationDialog::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    QLabel* title = new QLabel(QStringLiteral("设置禁言时长"), this);
    title->setObjectName(QStringLiteral("dialogTitleLabel"));
    root->addWidget(title);

    m_durationCombo = new QComboBox(this);
    m_durationCombo->setObjectName(QStringLiteral("dialogCombo"));
    m_durationCombo->addItem(QStringLiteral("5分钟"), 5);
    m_durationCombo->addItem(QStringLiteral("30分钟"), 30);
    m_durationCombo->addItem(QStringLiteral("1小时"), 60);
    m_durationCombo->addItem(QStringLiteral("12小时"), 720);
    m_durationCombo->addItem(QStringLiteral("1天"), 1440);
    m_durationCombo->addItem(QStringLiteral("7天"), 10080);
    m_durationCombo->addItem(QStringLiteral("永久"), -1);
    root->addWidget(m_durationCombo);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_confirmBtn = new QPushButton(QStringLiteral("确认"), this);
    m_confirmBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    connect(m_confirmBtn, &QPushButton::clicked, this, [this]() {
        emit muteConfirmed(durationMinutes());
        accept();
    });
    btnLayout->addWidget(m_confirmBtn);

    QPushButton* cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    cancelBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    root->addLayout(btnLayout);
}

void MuteDurationDialog::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#muteDurationDialog { background-color: %1; }"
        "QLabel#dialogTitleLabel { color: %2; font-size: 18px; font-weight: 600; }"
        "QComboBox#dialogCombo { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 12px; }"
        "QComboBox#dialogCombo:focus { border: 1px solid %6; }"
        "QComboBox#dialogCombo::drop-down { border: none; width: 24px; }"
        "QComboBox#dialogCombo QAbstractItemView { background-color: %4; color: %2; border: 1px solid %5; selection-background-color: %8; }"
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
     .arg(tm->primaryHoverColor().name())
     .arg(tm->primarySoftColor().name()));
}

int MuteDurationDialog::durationMinutes() const
{
    return m_durationCombo->currentData().toInt();
}

bool MuteDurationDialog::isPermanent() const
{
    return durationMinutes() == -1;
}

