#include "views/settingsview.h"

#include "theme/thememanager.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SettingsView::SettingsView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("settingsView"));
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &SettingsView::updateStyle);
}

void SettingsView::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(16);

    QLabel* title = new QLabel(QStringLiteral("设置"), this);
    title->setObjectName(QStringLiteral("settingsTitleLabel"));
    root->addWidget(title);

    QPushButton* themeBtn = new QPushButton(QStringLiteral("切换 亮/暗 主题"), this);
    themeBtn->setObjectName(QStringLiteral("settingsBtn"));
    connect(themeBtn, &QPushButton::clicked, this, &SettingsView::themeToggled);
    root->addWidget(themeBtn);

    QCheckBox* notifyCheck = new QCheckBox(QStringLiteral("启用通知"), this);
    notifyCheck->setObjectName(QStringLiteral("settingsCheck"));
    notifyCheck->setChecked(true);
    connect(notifyCheck, &QCheckBox::toggled, this, &SettingsView::notificationsToggled);
    root->addWidget(notifyCheck);

    root->addStretch();
}

void SettingsView::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QLabel#settingsTitleLabel { color: %1; font-size: 20px; font-weight: 600; }"
        "QPushButton#settingsBtn { background-color: %2; color: %1; border: 1px solid %3; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#settingsBtn:hover { background-color: %4; }"
        "QCheckBox#settingsCheck { color: %1; }"
        "QCheckBox#settingsCheck::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid %3; }"
        "QCheckBox#settingsCheck::indicator:checked { background-color: %2; border: 1px solid %2; }"
    ).arg(tm->textColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->primaryHoverColor().name()));
}
