#include "views/settingsview.h"

#include "theme/thememanager.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

// Helper: creates a horizontal row with a label on the left and a widget on the right,
// separated by a bottom border. Matches the Row component in tauri-qqnt SettingsView.tsx.
QFrame* makeRow(const QString& label, QWidget* control, QWidget* parent)
{
    auto* row = new QFrame(parent);
    row->setObjectName(QStringLiteral("settingsRow"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto* lbl = new QLabel(label, row);
    lbl->setObjectName(QStringLiteral("settingsRowLabel"));
    layout->addWidget(lbl);
    layout->addStretch();
    layout->addWidget(control);
    return row;
}

// Helper: creates a styled section panel (card) wrapping content.
QFrame* makePanel(QWidget* content, QWidget* parent)
{
    auto* panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("settingsPanel"));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(0);
    layout->addWidget(content);
    return panel;
}

} // namespace

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
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // Left sidebar (tab bar oriented vertically on the left, mimics SettingsSidebar)
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName(QStringLiteral("settingsTabWidget"));
    m_tabWidget->setTabPosition(QTabWidget::West);

    m_tabWidget->addTab(buildGeneralTab(), QStringLiteral("  通用  "));
    m_tabWidget->addTab(buildAccountTab(), QStringLiteral("  账号  "));
    m_tabWidget->addTab(buildNotificationTab(), QStringLiteral("  通知  "));
    m_tabWidget->addTab(buildShortcutTab(), QStringLiteral("  快捷键  "));
    m_tabWidget->addTab(buildFileTab(), QStringLiteral("  文件  "));
    m_tabWidget->addTab(buildE2ETab(), QStringLiteral("  E2E  "));
    m_tabWidget->addTab(buildAboutTab(), QStringLiteral("  关于  "));

    rootLayout->addWidget(m_tabWidget, 1);

    connect(m_tabWidget, &QTabWidget::currentChanged, this, &SettingsView::onTabChanged);
}

QWidget* SettingsView::buildGeneralTab()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("settingsGeneralPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 24);
    layout->setSpacing(12);

    // Header
    auto* header = new QLabel(QStringLiteral("通用"), page);
    header->setObjectName(QStringLiteral("settingsPageHeader"));
    layout->addWidget(header);

    m_syncStatus = new QLabel(QStringLiteral("已保存到本地"), page);
    m_syncStatus->setObjectName(QStringLiteral("settingsSyncStatus"));
    layout->addWidget(m_syncStatus);

    // Theme selector row: three buttons for light/dark/system
    auto* themeGroup = new QButtonGroup(page);
    themeGroup->setExclusive(true);

    m_lightBtn = new QPushButton(QStringLiteral("☀ 浅色"), page);
    m_lightBtn->setObjectName(QStringLiteral("settingsThemeBtn"));
    m_lightBtn->setCheckable(true);
    m_lightBtn->setChecked(true);
    themeGroup->addButton(m_lightBtn, 0);

    m_darkBtn = new QPushButton(QStringLiteral("🌙 深色"), page);
    m_darkBtn->setObjectName(QStringLiteral("settingsThemeBtn"));
    m_darkBtn->setCheckable(true);
    themeGroup->addButton(m_darkBtn, 1);

    m_systemThemeBtn = new QPushButton(QStringLiteral("🖥 跟随系统"), page);
    m_systemThemeBtn->setObjectName(QStringLiteral("settingsThemeBtn"));
    m_systemThemeBtn->setCheckable(true);
    themeGroup->addButton(m_systemThemeBtn, 2);

    auto* themeBox = new QWidget(page);
    auto* themeBoxLayout = new QHBoxLayout(themeBox);
    themeBoxLayout->setContentsMargins(0, 0, 0, 0);
    themeBoxLayout->setSpacing(4);
    themeBoxLayout->addWidget(m_lightBtn);
    themeBoxLayout->addWidget(m_darkBtn);
    themeBoxLayout->addWidget(m_systemThemeBtn);
    layout->addWidget(makeRow(QStringLiteral("主题"), themeBox, page));

    connect(m_lightBtn, &QPushButton::clicked, this, [this]() {
        emit themeModeChanged(0);
        emit themeToggled();
    });
    connect(m_darkBtn, &QPushButton::clicked, this, [this]() {
        emit themeModeChanged(1);
        emit themeToggled();
    });
    connect(m_systemThemeBtn, &QPushButton::clicked, this, [this]() {
        emit themeModeChanged(2);
    });

    // Launch on startup (display-only toggle; backend wiring comes later)
    auto* launchCheck = new QCheckBox(page);
    launchCheck->setObjectName(QStringLiteral("settingsCheck"));
    layout->addWidget(makeRow(QStringLiteral("开机自启"), launchCheck, page));

    // Minimize to tray
    auto* trayCheck = new QCheckBox(page);
    trayCheck->setObjectName(QStringLiteral("settingsCheck"));
    layout->addWidget(makeRow(QStringLiteral("最小化到托盘"), trayCheck, page));

    // Language row (static text)
    auto* langLabel = new QLabel(QStringLiteral("简体中文"), page);
    langLabel->setObjectName(QStringLiteral("settingsStaticText"));
    layout->addWidget(makeRow(QStringLiteral("语言"), langLabel, page));

    layout->addStretch();

    auto* panel = makePanel(page, this);
    auto* wrapper = new QVBoxLayout();
    wrapper->setContentsMargins(0, 0, 0, 0);
    wrapper->addWidget(panel);
    auto* wrapperWidget = new QWidget(this);
    wrapperWidget->setLayout(wrapper);
    return wrapperWidget;
}

QWidget* SettingsView::buildAccountTab()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("settingsAccountPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 24);
    layout->setSpacing(12);

    auto* header = new QLabel(QStringLiteral("账号"), page);
    header->setObjectName(QStringLiteral("settingsPageHeader"));
    layout->addWidget(header);

    m_accountNameLabel = new QLabel(QStringLiteral("未登录"), page);
    m_accountNameLabel->setObjectName(QStringLiteral("settingsStaticText"));
    layout->addWidget(makeRow(QStringLiteral("当前账号"), m_accountNameLabel, page));

    m_accountIdLabel = new QLabel(QStringLiteral("-"), page);
    m_accountIdLabel->setObjectName(QStringLiteral("settingsStaticText"));
    layout->addWidget(makeRow(QStringLiteral("QQ 号"), m_accountIdLabel, page));

    auto* onlineLabel = new QLabel(QStringLiteral("在线"), page);
    onlineLabel->setObjectName(QStringLiteral("settingsOnlineText"));
    layout->addWidget(makeRow(QStringLiteral("在线状态"), onlineLabel, page));

    auto* syncLabel = new QLabel(QStringLiteral("个人资料页保存后同步"), page);
    syncLabel->setObjectName(QStringLiteral("settingsStaticText"));
    layout->addWidget(makeRow(QStringLiteral("资料同步"), syncLabel, page));

    layout->addStretch();

    auto* panel = makePanel(page, this);
    auto* wrapper = new QVBoxLayout();
    wrapper->setContentsMargins(0, 0, 0, 0);
    wrapper->addWidget(panel);
    auto* wrapperWidget = new QWidget(this);
    wrapperWidget->setLayout(wrapper);
    return wrapperWidget;
}

QWidget* SettingsView::buildNotificationTab()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("settingsNotifyPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 24);
    layout->setSpacing(12);

    auto* header = new QLabel(QStringLiteral("通知"), page);
    header->setObjectName(QStringLiteral("settingsPageHeader"));
    layout->addWidget(header);

    m_notifyCheck = new QCheckBox(page);
    m_notifyCheck->setObjectName(QStringLiteral("settingsCheck"));
    m_notifyCheck->setChecked(true);
    connect(m_notifyCheck, &QCheckBox::toggled, this, &SettingsView::notificationsToggled);
    layout->addWidget(makeRow(QStringLiteral("消息通知"), m_notifyCheck, page));

    m_soundCheck = new QCheckBox(page);
    m_soundCheck->setObjectName(QStringLiteral("settingsCheck"));
    m_soundCheck->setChecked(true);
    connect(m_soundCheck, &QCheckBox::toggled, this, &SettingsView::soundToggled);
    layout->addWidget(makeRow(QStringLiteral("声音"), m_soundCheck, page));

    m_desktopNotifyCheck = new QCheckBox(page);
    m_desktopNotifyCheck->setObjectName(QStringLiteral("settingsCheck"));
    m_desktopNotifyCheck->setChecked(true);
    connect(m_desktopNotifyCheck, &QCheckBox::toggled, this, &SettingsView::desktopNotificationsToggled);
    layout->addWidget(makeRow(QStringLiteral("桌面通知"), m_desktopNotifyCheck, page));

    m_muteInSessionCheck = new QCheckBox(page);
    m_muteInSessionCheck->setObjectName(QStringLiteral("settingsCheck"));
    connect(m_muteInSessionCheck, &QCheckBox::toggled, this, &SettingsView::muteInSessionToggled);
    layout->addWidget(makeRow(QStringLiteral("会话内消息免打扰"), m_muteInSessionCheck, page));

    layout->addStretch();

    auto* panel = makePanel(page, this);
    auto* wrapper = new QVBoxLayout();
    wrapper->setContentsMargins(0, 0, 0, 0);
    wrapper->addWidget(panel);
    auto* wrapperWidget = new QWidget(this);
    wrapperWidget->setLayout(wrapper);
    return wrapperWidget;
}

QWidget* SettingsView::buildShortcutTab()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("settingsShortcutPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 24);
    layout->setSpacing(12);

    auto* header = new QLabel(QStringLiteral("快捷键"), page);
    header->setObjectName(QStringLiteral("settingsPageHeader"));
    layout->addWidget(header);

    m_screenshotShortcutBtn = new QPushButton(QStringLiteral("Ctrl+Alt+A"), page);
    m_screenshotShortcutBtn->setObjectName(QStringLiteral("settingsShortcutBtn"));
    connect(m_screenshotShortcutBtn, &QPushButton::clicked,
            this, &SettingsView::screenshotShortcutChangeRequested);
    layout->addWidget(makeRow(QStringLiteral("截图快捷键"), m_screenshotShortcutBtn, page));

    m_hideWindowCheck = new QCheckBox(page);
    m_hideWindowCheck->setObjectName(QStringLiteral("settingsCheck"));
    m_hideWindowCheck->setChecked(true);
    connect(m_hideWindowCheck, &QCheckBox::toggled, this, &SettingsView::hideWindowBeforeScreenshotToggled);
    layout->addWidget(makeRow(QStringLiteral("截图时隐藏当前窗口"), m_hideWindowCheck, page));

    auto* hint1 = new QLabel(QStringLiteral("聊天输入框工具栏 · 剪刀按钮"), page);
    hint1->setObjectName(QStringLiteral("settingsStaticText"));
    layout->addWidget(makeRow(QStringLiteral("截图入口"), hint1, page));

    auto* hint2 = new QLabel(QStringLiteral("保存后自动重新注册全局快捷键"), page);
    hint2->setObjectName(QStringLiteral("settingsStaticText"));
    layout->addWidget(makeRow(QStringLiteral("生效方式"), hint2, page));

    layout->addStretch();

    auto* panel = makePanel(page, this);
    auto* wrapper = new QVBoxLayout();
    wrapper->setContentsMargins(0, 0, 0, 0);
    wrapper->addWidget(panel);
    auto* wrapperWidget = new QWidget(this);
    wrapperWidget->setLayout(wrapper);
    return wrapperWidget;
}

QWidget* SettingsView::buildFileTab()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("settingsFilePage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 24);
    layout->setSpacing(12);

    auto* header = new QLabel(QStringLiteral("文件"), page);
    header->setObjectName(QStringLiteral("settingsPageHeader"));
    layout->addWidget(header);

    m_downloadPathBtn = new QPushButton(QStringLiteral("选择文件夹"), page);
    m_downloadPathBtn->setObjectName(QStringLiteral("settingsPathBtn"));
    connect(m_downloadPathBtn, &QPushButton::clicked, this, &SettingsView::downloadPathChangeRequested);
    layout->addWidget(makeRow(QStringLiteral("默认下载目录"), m_downloadPathBtn, page));

    m_autoAcceptCheck = new QCheckBox(page);
    m_autoAcceptCheck->setObjectName(QStringLiteral("settingsCheck"));
    connect(m_autoAcceptCheck, &QCheckBox::toggled, this, &SettingsView::autoAcceptFilesToggled);
    layout->addWidget(makeRow(QStringLiteral("自动接收文件（不超过 100 MB）"), m_autoAcceptCheck, page));

    m_openFolderCheck = new QCheckBox(page);
    m_openFolderCheck->setObjectName(QStringLiteral("settingsCheck"));
    connect(m_openFolderCheck, &QCheckBox::toggled, this, &SettingsView::openFolderAfterDownloadToggled);
    layout->addWidget(makeRow(QStringLiteral("下载完成后打开文件夹"), m_openFolderCheck, page));

    layout->addStretch();

    auto* panel = makePanel(page, this);
    auto* wrapper = new QVBoxLayout();
    wrapper->setContentsMargins(0, 0, 0, 0);
    wrapper->addWidget(panel);
    auto* wrapperWidget = new QWidget(this);
    wrapperWidget->setLayout(wrapper);
    return wrapperWidget;
}

QWidget* SettingsView::buildE2ETab()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("settingsE2EPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 24);
    layout->setSpacing(12);

    auto* header = new QLabel(QStringLiteral("E2E"), page);
    header->setObjectName(QStringLiteral("settingsPageHeader"));
    layout->addWidget(header);

    m_e2eCheck = new QCheckBox(page);
    m_e2eCheck->setObjectName(QStringLiteral("settingsCheck"));
    m_e2eCheck->setChecked(true);
    connect(m_e2eCheck, &QCheckBox::toggled, this, &SettingsView::e2eEnabledToggled);
    layout->addWidget(makeRow(QStringLiteral("端到端加密"), m_e2eCheck, page));

    auto* securityBtn = new QPushButton(QStringLiteral("查看"), page);
    securityBtn->setObjectName(QStringLiteral("settingsPathBtn"));
    layout->addWidget(makeRow(QStringLiteral("安全号码"), securityBtn, page));

    auto* rotationLabel = new QLabel(QStringLiteral("90 天"), page);
    rotationLabel->setObjectName(QStringLiteral("settingsStaticText"));
    layout->addWidget(makeRow(QStringLiteral("自动轮换周期"), rotationLabel, page));

    layout->addStretch();

    auto* panel = makePanel(page, this);
    auto* wrapper = new QVBoxLayout();
    wrapper->setContentsMargins(0, 0, 0, 0);
    wrapper->addWidget(panel);
    auto* wrapperWidget = new QWidget(this);
    wrapperWidget->setLayout(wrapper);
    return wrapperWidget;
}

QWidget* SettingsView::buildAboutTab()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("settingsAboutPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 24);
    layout->setSpacing(12);

    auto* header = new QLabel(QStringLiteral("关于"), page);
    header->setObjectName(QStringLiteral("settingsPageHeader"));
    layout->addWidget(header);

    auto* versionLabel = new QLabel(QStringLiteral("v0.1.0"), page);
    versionLabel->setObjectName(QStringLiteral("settingsStaticText"));
    layout->addWidget(makeRow(QStringLiteral("QQ NT"), versionLabel, page));

    auto* tauriLabel = new QLabel(QStringLiteral("Qt 6 / Widgets"), page);
    tauriLabel->setObjectName(QStringLiteral("settingsStaticText"));
    layout->addWidget(makeRow(QStringLiteral("框架"), tauriLabel, page));

    auto* protoLabel = new QLabel(QStringLiteral("v1"), page);
    protoLabel->setObjectName(QStringLiteral("settingsStaticText"));
    layout->addWidget(makeRow(QStringLiteral("协议版本"), protoLabel, page));

    auto* syncStatusLabel = new QLabel(QStringLiteral("已保存到本地"), page);
    syncStatusLabel->setObjectName(QStringLiteral("settingsStaticText"));
    layout->addWidget(makeRow(QStringLiteral("设置同步"), syncStatusLabel, page));

    layout->addStretch();

    auto* panel = makePanel(page, this);
    auto* wrapper = new QVBoxLayout();
    wrapper->setContentsMargins(0, 0, 0, 0);
    wrapper->addWidget(panel);
    auto* wrapperWidget = new QWidget(this);
    wrapperWidget->setLayout(wrapper);
    return wrapperWidget;
}

void SettingsView::onTabChanged(int /*index*/)
{
    // Placeholder for future per-tab sync logic.
}

void SettingsView::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    const QString text = tm->textColor().name();
    const QString textSec = tm->textSecondaryColor().name();
    const QString textTer = tm->textTertiaryColor().name();
    const QString bg = tm->backgroundColor().name();
    const QString bgSec = tm->backgroundSecondaryColor().name();
    const QString bgTer = tm->backgroundTertiaryColor().name();
    const QString border = tm->borderColor().name();
    const QString primary = tm->primaryColor().name();
    const QString primaryHover = tm->primaryHoverColor().name();
    const QString primarySoft = tm->primarySoftColor().name();
    const QString success = tm->successColor().name();
    const QString danger = tm->dangerColor().name();
    const int radius = tm->cornerRadius();

    setStyleSheet(QStringLiteral(
        // Tab widget
        "QTabWidget#settingsTabWidget::pane { background: %1; border: none; top: 0; }"
        "QTabWidget#settingsTabWidget::tab-bar { alignment: stretch; }"
        "QTabBar::tab { background: %2; color: %3; padding: 10px 18px; border: none; "
        "  min-width: 100px; text-align: left; }"
        "QTabBar::tab:selected { background: %1; color: %5; border-left: 3px solid %5; }"
        "QTabBar::tab:hover:!selected { background: %4; color: %3; }"

        // Page header
        "QLabel#settingsPageHeader { color: %5; font-size: 16px; font-weight: 600; }"
        "QLabel#settingsSyncStatus { color: %6; font-size: 12px; }"

        // Row
        "QFrame#settingsRow { border-bottom: 1px solid %8; }"
        "QLabel#settingsRowLabel { color: %5; font-size: 13px; }"

        // Panel card
        "QFrame#settingsPanel { background: %1; border: 1px solid %8; border-radius: %7px; }"

        // Checkboxes
        "QCheckBox#settingsCheck { color: %5; spacing: 8px; }"
        "QCheckBox#settingsCheck::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid %8; }"
        "QCheckBox#settingsCheck::indicator:checked { background: %5; border: 1px solid %5; }"

        // Theme buttons
        "QPushButton#settingsThemeBtn { background: %4; color: %3; border: 1px solid %8;"
        "  border-radius: %7px; padding: 5px 12px; }"
        "QPushButton#settingsThemeBtn:checked { background: %10; color: %5; border: 1px solid %5; }"
        "QPushButton#settingsThemeBtn:hover:!checked { background: %9; }"

        // Shortcut / path buttons
        "QPushButton#settingsShortcutBtn, QPushButton#settingsPathBtn {"
        "  background: %4; color: %5; border: 1px solid %8; border-radius: %7px; padding: 5px 14px; }"
        "QPushButton#settingsShortcutBtn:hover, QPushButton#settingsPathBtn:hover {"
        "  background: %9; }"

        // Static text values
        "QLabel#settingsStaticText { color: %6; font-size: 13px; }"
        "QLabel#settingsOnlineText { color: %11; font-size: 13px; }"
    ).arg(bg)            // %1
     .arg(bgSec)         // %2
     .arg(textTer)       // %3
     .arg(bgTer)         // %4
     .arg(text)          // %5
     .arg(textSec)       // %6
     .arg(radius)        // %7
     .arg(border)        // %8
     .arg(primaryHover)   // %9
     .arg(primarySoft)    // %10
     .arg(success)        // %11
    );
}

void SettingsView::setAccountInfo(const QString& userName, const QString& userId)
{
    m_accountNameLabel->setText(userName.isEmpty() ? QStringLiteral("未登录") : userName);
    m_accountIdLabel->setText(userId.isEmpty() ? QStringLiteral("-") : userId);
}

void SettingsView::setThemeMode(int mode)
{
    m_lightBtn->setChecked(mode == 0);
    m_darkBtn->setChecked(mode == 1);
    m_systemThemeBtn->setChecked(mode == 2);
}

void SettingsView::setSyncStatus(const QString& text)
{
    m_syncStatus->setText(text);
}
