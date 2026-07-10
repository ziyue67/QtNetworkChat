#include "windows/loginwindow.h"

#include "theme/thememanager.h"
#include "widgets/avatarlabel.h"
#include "logincredentialstore.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QMessageBox>
#include <QSettings>
#include <QVBoxLayout>

LoginWindow::LoginWindow(QWidget* parent)
    : QDialog(parent)
    
{
    setObjectName(QStringLiteral("loginWindow"));
    setWindowTitle(QStringLiteral("QQ 登录"));
    setFixedSize(400, 520);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setupUi();
    updateStyle();
    loadSettings();
    setRegisterMode(false);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &LoginWindow::updateStyle);
}

void LoginWindow::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header with gradient background
    QFrame* header = new QFrame(this);
    header->setObjectName(QStringLiteral("loginHeader"));
    header->setFixedHeight(190);
    QVBoxLayout* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(22, 8, 22, 16);
    headerLayout->setSpacing(0);

    // Title bar with close button
    QHBoxLayout* titleBarLayout = new QHBoxLayout();
    QLabel* brandLabel = new QLabel(QStringLiteral("QQ"), header);
    brandLabel->setObjectName(QStringLiteral("brandLabel"));
    titleBarLayout->addWidget(brandLabel);
    titleBarLayout->addStretch();

    QPushButton* closeBtn = new QPushButton(QStringLiteral("×"), header);
    closeBtn->setObjectName(QStringLiteral("windowCloseBtn"));
    closeBtn->setFixedSize(28, 28);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    titleBarLayout->addWidget(closeBtn);
    headerLayout->addLayout(titleBarLayout);

    // Avatar
    m_avatar = new AvatarLabel(header, 72);
    m_avatar->setTextAvatar(QStringLiteral("Q"), QColor(QStringLiteral("#0099ff")));
    headerLayout->addSpacing(16);
    headerLayout->addWidget(m_avatar, 0, Qt::AlignCenter);

    // Title
    m_titleLabel = new QLabel(QStringLiteral("QQ 账号登录"), header);
    m_titleLabel->setObjectName(QStringLiteral("loginTitleLabel"));
    m_titleLabel->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(m_titleLabel);
    mainLayout->addWidget(header);

    // Form card
    QFrame* formCard = new QFrame(this);
    formCard->setObjectName(QStringLiteral("loginFormCard"));
    QVBoxLayout* formLayout = new QVBoxLayout(formCard);
    formLayout->setContentsMargins(33, 16, 33, 24);
    formLayout->setSpacing(10);

    m_accountEdit = new QLineEdit(formCard);
    m_accountEdit->setObjectName(QStringLiteral("loginInput"));
    m_accountEdit->setPlaceholderText(QStringLiteral("QQ 号 / 账号"));
    m_accountEdit->setClearButtonEnabled(true);
    m_accountEdit->setMaxLength(24);
    formLayout->addWidget(m_accountEdit);

    m_nameEdit = new QLineEdit(formCard);
    m_nameEdit->setObjectName(QStringLiteral("loginInput"));
    m_nameEdit->setPlaceholderText(QStringLiteral("昵称"));
    m_nameEdit->setClearButtonEnabled(true);
    m_nameEdit->setMaxLength(20);
    formLayout->addWidget(m_nameEdit);

    m_passwordEdit = new QLineEdit(formCard);
    m_passwordEdit->setObjectName(QStringLiteral("loginInput"));
    m_passwordEdit->setPlaceholderText(QStringLiteral("密码"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setClearButtonEnabled(true);
    m_passwordEdit->setMaxLength(32);
    formLayout->addWidget(m_passwordEdit);

    m_confirmEdit = new QLineEdit(formCard);
    m_confirmEdit->setObjectName(QStringLiteral("loginInput"));
    m_confirmEdit->setPlaceholderText(QStringLiteral("确认密码"));
    m_confirmEdit->setEchoMode(QLineEdit::Password);
    m_confirmEdit->setClearButtonEnabled(true);
    m_confirmEdit->setMaxLength(32);
    formLayout->addWidget(m_confirmEdit);

    // Options
    QHBoxLayout* optionLayout = new QHBoxLayout();
    m_rememberCheck = new QCheckBox(QStringLiteral("记住密码"), formCard);
    m_rememberCheck->setObjectName(QStringLiteral("loginCheck"));
    optionLayout->addWidget(m_rememberCheck);
    optionLayout->addStretch();
    formLayout->addLayout(optionLayout);

    // Agreement
    m_agreementCheck = new QCheckBox(QStringLiteral("我已阅读并同意服务协议和隐私政策"), formCard);
    m_agreementCheck->setObjectName(QStringLiteral("loginCheck"));
    m_agreementCheck->setChecked(true);
    formLayout->addWidget(m_agreementCheck);

    // Feedback
    m_feedbackLabel = new QLabel(QStringLiteral("请输入账号、密码并同意协议"), formCard);
    m_feedbackLabel->setObjectName(QStringLiteral("loginFeedbackLabel"));
    m_feedbackLabel->setWordWrap(true);
    formLayout->addWidget(m_feedbackLabel);

    // OK button
    m_okBtn = new QPushButton(QStringLiteral("登录"), formCard);
    m_okBtn->setObjectName(QStringLiteral("loginPrimaryBtn"));
    connect(m_okBtn, &QPushButton::clicked, this, [this]() {
        if (!m_agreementCheck->isChecked()) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请先勾选同意服务协议和隐私政策"));
            return;
        }
        if (!m_registerMode && m_accountEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请输入 QQ 账号"));
            return;
        }
        if (m_registerMode && m_nameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请输入昵称"));
            return;
        }
        if (m_passwordEdit->text().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请输入密码"));
            return;
        }
        if (m_passwordEdit->text().length() < 6) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("密码至少需要6位"));
            return;
        }
        if (m_registerMode && m_passwordEdit->text() != m_confirmEdit->text()) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("两次输入的密码不一致"));
            return;
        }
        m_account = m_registerMode ? QString() : m_accountEdit->text().trimmed();
        m_password = m_passwordEdit->text();
        m_userName = m_nameEdit->text().trimmed();
        if (m_userName.isEmpty()) {
            m_userName = m_account;
        }
        m_host = QStringLiteral("127.0.0.1");
        m_port = 8888;
        accept();
    });
    formLayout->addWidget(m_okBtn);

    // Register/Login links
    QHBoxLayout* linkLayout = new QHBoxLayout();
    linkLayout->addStretch();
    m_registerLinkBtn = new QPushButton(QStringLiteral("注册账号"), formCard);
    m_registerLinkBtn->setObjectName(QStringLiteral("loginLinkBtn"));
    m_registerLinkBtn->setFlat(true);
    connect(m_registerLinkBtn, &QPushButton::clicked, this, [this]() {
        setRegisterMode(true);
    });
    linkLayout->addWidget(m_registerLinkBtn);

    m_loginLinkBtn = new QPushButton(QStringLiteral("已有账号？去登录"), formCard);
    m_loginLinkBtn->setObjectName(QStringLiteral("loginLinkBtn"));
    m_loginLinkBtn->setFlat(true);
    connect(m_loginLinkBtn, &QPushButton::clicked, this, [this]() {
        setRegisterMode(false);
    });
    linkLayout->addWidget(m_loginLinkBtn);
    linkLayout->addStretch();
    formLayout->addLayout(linkLayout);

    mainLayout->addWidget(formCard, 1);

    // Connect form change signals
    connect(m_accountEdit, &QLineEdit::textChanged, this, &LoginWindow::updateFormState);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &LoginWindow::updateFormState);
    connect(m_passwordEdit, &QLineEdit::textChanged, this, &LoginWindow::updateFormState);
    connect(m_confirmEdit, &QLineEdit::textChanged, this, &LoginWindow::updateFormState);
    connect(m_agreementCheck, &QCheckBox::toggled, this, &LoginWindow::updateFormState);
    connect(m_rememberCheck, &QCheckBox::toggled, this, &LoginWindow::updateFormState);
}

void LoginWindow::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#loginWindow { background-color: %1; }"
        "QFrame#loginHeader { background-color: %4; border-bottom: 1px solid %5; }"
        "QLabel#brandLabel { color: white; font-size: 20px; font-weight: 700; }"
        "QLabel#loginTitleLabel { color: white; font-size: 16px; font-weight: 500; }"
        "QFrame#loginFormCard { background-color: %1; }"
        "QLineEdit#loginInput { background-color: %2; color: %6; border: 1px solid %5; border-radius: 6px; padding: 10px 14px; font-size: 14px; }"
        "QLineEdit#loginInput:focus { border: 1px solid %4; }"
        "QCheckBox#loginCheck { color: %7; font-size: 12px; }"
        "QCheckBox#loginCheck::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid %5; }"
        "QCheckBox#loginCheck::indicator:checked { background-color: %4; border: 1px solid %4; }"
        "QLabel#loginFeedbackLabel { color: %7; font-size: 12px; padding: 4px 8px; border-radius: 6px; background-color: %2; }"
        "QPushButton#loginPrimaryBtn { background-color: %4; color: white; border: none; border-radius: 6px; padding: 10px; font-size: 15px; font-weight: 500; }"
        "QPushButton#loginPrimaryBtn:hover { background-color: %8; }"
        "QPushButton#loginPrimaryBtn:disabled { background-color: %5; color: %7; }"
        "QPushButton#loginLinkBtn { color: %4; border: none; background: transparent; font-size: 13px; }"
        "QPushButton#loginLinkBtn:hover { color: %8; text-decoration: underline; }"
        "QPushButton#windowCloseBtn { color: white; border: none; background: transparent; font-size: 16px; }"
        "QPushButton#windowCloseBtn:hover { background-color: #ff4d4f; border-radius: 4px; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->textColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->primaryHoverColor().name()));
}

void LoginWindow::updateFormState()
{
    const QString account = m_accountEdit->text().trimmed();
    const QString userName = m_nameEdit->text().trimmed();
    const QString password = m_passwordEdit->text();
    const QString confirm = m_confirmEdit->text();
    const bool agreed = m_agreementCheck->isChecked();

    bool ready = true;
    QString feedback;

    if (!agreed) {
        feedback = QStringLiteral("请先勾选服务协议和隐私政策");
        ready = false;
    } else if (!m_registerMode && account.isEmpty()) {
        feedback = QStringLiteral("请输入 QQ 账号");
        ready = false;
    } else if (m_registerMode && userName.isEmpty()) {
        feedback = QStringLiteral("请输入昵称");
        ready = false;
    } else if (password.length() < 6) {
        feedback = QStringLiteral("密码至少需要6位");
        ready = false;
    } else if (m_registerMode && password != confirm) {
        feedback = QStringLiteral("两次输入的密码不一致");
        ready = false;
    } else {
        feedback = m_registerMode
            ? QStringLiteral("资料完整，点击立即注册")
            : QStringLiteral("准备登录 QQ:%1").arg(account);
    }

    m_okBtn->setEnabled(ready);
    m_feedbackLabel->setText(feedback);
    m_titleLabel->setText(m_registerMode
        ? (userName.isEmpty() ? QStringLiteral("欢迎注册 QQ") : QStringLiteral("注册昵称：%1").arg(userName))
        : (account.isEmpty() ? QStringLiteral("QQ 账号登录") : QStringLiteral("QQ %1").arg(account)));
}

void LoginWindow::setRegisterMode(bool registerMode)
{
    m_registerMode = registerMode;
    setWindowTitle(registerMode ? QStringLiteral("注册 QQ") : QStringLiteral("QQ 登录"));
    setFixedSize(400, registerMode ? 560 : 520);
    m_avatar->setTextAvatar(registerMode ? QStringLiteral("注") : QStringLiteral("Q"), QColor(QStringLiteral("#0099ff")));
    m_accountEdit->setVisible(!registerMode);
    m_nameEdit->setVisible(registerMode);
    m_confirmEdit->setVisible(registerMode);
    m_rememberCheck->setVisible(!registerMode);
    m_registerLinkBtn->setVisible(!registerMode);
    m_loginLinkBtn->setVisible(registerMode);
    m_okBtn->setText(registerMode ? QStringLiteral("立即注册") : QStringLiteral("登录"));
    if (registerMode) {
        m_accountEdit->clear();
    }
    updateFormState();
}

void LoginWindow::loadSettings()
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("Login"));
    QString savedAccount = settings.value(QStringLiteral("account")).toString();
    if (!savedAccount.isEmpty()) {
        m_accountEdit->setText(savedAccount);
        m_rememberCheck->setChecked(true);
    }
}

bool LoginWindow::loadLoginFromSqlite()
{
    return false;
}

bool LoginWindow::saveResolvedLoginToSqlite(const QString& account, const QString& userName, bool rememberPassword)
{
    Q_UNUSED(account)
    Q_UNUSED(userName)
    Q_UNUSED(rememberPassword)
    return true;
}

QString LoginWindow::userName() const { return m_userName; }
QString LoginWindow::account() const { return m_account; }
QString LoginWindow::password() const { return m_password; }
QString LoginWindow::serverAddress() const { return m_host; }
quint16 LoginWindow::serverPort() const { return m_port; }
bool LoginWindow::rememberPassword() const { return m_rememberCheck->isChecked(); }
bool LoginWindow::registerMode() const { return m_registerMode; }


