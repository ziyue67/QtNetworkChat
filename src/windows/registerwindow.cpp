#include "windows/registerwindow.h"

#include "theme/thememanager.h"
#include "widgets/avatarlabel.h"
#include "widgets/dialogtitlebar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QMessageBox>
#include <QVBoxLayout>

RegisterWindow::RegisterWindow(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("registerWindow"));
    setWindowTitle(QStringLiteral("注册 QQ"));
    setFixedSize(400, 560);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &RegisterWindow::updateStyle);
}

void RegisterWindow::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    DialogTitleBar* titleBar = new DialogTitleBar(this, QStringLiteral("注册 QQ"));
    connect(titleBar, &DialogTitleBar::closeRequested, this, &QDialog::reject);
    mainLayout->addWidget(titleBar);

    // Header with gradient
    QFrame* header = new QFrame(this);
    header->setObjectName(QStringLiteral("registerHeader"));
    header->setFixedHeight(180);
    QVBoxLayout* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(22, 16, 22, 16);
    headerLayout->setSpacing(0);

    // Avatar
    m_avatar = new AvatarLabel(header, 72);
    m_avatar->setTextAvatar(QStringLiteral("注"), QColor(QStringLiteral("#0099ff")));
    headerLayout->addWidget(m_avatar, 0, Qt::AlignCenter);

    // Title
    m_titleLabel = new QLabel(QStringLiteral("欢迎注册 QQ"), header);
    m_titleLabel->setObjectName(QStringLiteral("registerTitleLabel"));
    m_titleLabel->setAlignment(Qt::AlignCenter);
    headerLayout->addSpacing(8);
    headerLayout->addWidget(m_titleLabel);
    mainLayout->addWidget(header);

    // Form card
    QFrame* formCard = new QFrame(this);
    formCard->setObjectName(QStringLiteral("registerFormCard"));
    QVBoxLayout* formLayout = new QVBoxLayout(formCard);
    formLayout->setContentsMargins(33, 16, 33, 24);
    formLayout->setSpacing(10);

    m_nameEdit = new QLineEdit(formCard);
    m_nameEdit->setObjectName(QStringLiteral("registerInput"));
    m_nameEdit->setPlaceholderText(QStringLiteral("昵称"));
    m_nameEdit->setClearButtonEnabled(true);
    m_nameEdit->setMaxLength(20);
    formLayout->addWidget(m_nameEdit);

    m_passwordEdit = new QLineEdit(formCard);
    m_passwordEdit->setObjectName(QStringLiteral("registerInput"));
    m_passwordEdit->setPlaceholderText(QStringLiteral("密码"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setClearButtonEnabled(true);
    m_passwordEdit->setMaxLength(32);
    formLayout->addWidget(m_passwordEdit);

    m_confirmEdit = new QLineEdit(formCard);
    m_confirmEdit->setObjectName(QStringLiteral("registerInput"));
    m_confirmEdit->setPlaceholderText(QStringLiteral("确认密码"));
    m_confirmEdit->setEchoMode(QLineEdit::Password);
    m_confirmEdit->setClearButtonEnabled(true);
    m_confirmEdit->setMaxLength(32);
    formLayout->addWidget(m_confirmEdit);

    // Agreement
    m_agreementCheck = new QCheckBox(QStringLiteral("我已阅读并同意服务协议和隐私政策"), formCard);
    m_agreementCheck->setObjectName(QStringLiteral("registerCheck"));
    m_agreementCheck->setChecked(true);
    formLayout->addWidget(m_agreementCheck);

    // Feedback
    m_feedbackLabel = new QLabel(QStringLiteral("请输入昵称、密码并同意协议"), formCard);
    m_feedbackLabel->setObjectName(QStringLiteral("registerFeedbackLabel"));
    m_feedbackLabel->setWordWrap(true);
    formLayout->addWidget(m_feedbackLabel);

    // Register button
    m_registerBtn = new QPushButton(QStringLiteral("立即注册"), formCard);
    m_registerBtn->setObjectName(QStringLiteral("registerPrimaryBtn"));
    connect(m_registerBtn, &QPushButton::clicked, this, [this]() {
        if (!m_agreementCheck->isChecked()) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请先勾选同意服务协议和隐私政策"));
            return;
        }
        if (m_nameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请输入昵称"));
            return;
        }
        if (m_passwordEdit->text().length() < 6) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("密码至少需要6位"));
            return;
        }
        if (m_passwordEdit->text() != m_confirmEdit->text()) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("两次输入的密码不一致"));
            return;
        }
        emit registerRequested(m_nameEdit->text().trimmed(), m_passwordEdit->text());
        accept();
    });
    formLayout->addWidget(m_registerBtn);

    // Login link
    QHBoxLayout* linkLayout = new QHBoxLayout();
    linkLayout->addStretch();
    m_loginLinkBtn = new QPushButton(QStringLiteral("已有账号？去登录"), formCard);
    m_loginLinkBtn->setObjectName(QStringLiteral("registerLinkBtn"));
    m_loginLinkBtn->setFlat(true);
    connect(m_loginLinkBtn, &QPushButton::clicked, this, &RegisterWindow::loginLinkClicked);
    linkLayout->addWidget(m_loginLinkBtn);
    linkLayout->addStretch();
    formLayout->addLayout(linkLayout);

    mainLayout->addWidget(formCard, 1);

    // Connect form change signals
    connect(m_nameEdit, &QLineEdit::textChanged, this, &RegisterWindow::updateFormState);
    connect(m_passwordEdit, &QLineEdit::textChanged, this, &RegisterWindow::updateFormState);
    connect(m_confirmEdit, &QLineEdit::textChanged, this, &RegisterWindow::updateFormState);
    connect(m_agreementCheck, &QCheckBox::toggled, this, &RegisterWindow::updateFormState);
}

void RegisterWindow::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#registerWindow { background-color: %1; }"
        "QFrame#registerHeader { background-color: %4; border-bottom: 1px solid %5; }"
        "QLabel#registerTitleLabel { color: white; font-size: 16px; font-weight: 500; }"
        "QFrame#registerFormCard { background-color: %1; }"
        "QLineEdit#registerInput { background-color: %2; color: %6; border: 1px solid %5; border-radius: 6px; padding: 10px 14px; font-size: 14px; }"
        "QLineEdit#registerInput:focus { border: 1px solid %4; }"
        "QCheckBox#registerCheck { color: %7; font-size: 12px; }"
        "QCheckBox#registerCheck::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid %5; }"
        "QCheckBox#registerCheck::indicator:checked { background-color: %4; border: 1px solid %4; }"
        "QLabel#registerFeedbackLabel { color: %7; font-size: 12px; padding: 4px 8px; border-radius: 6px; background-color: %2; }"
        "QPushButton#registerPrimaryBtn { background-color: %4; color: white; border: none; border-radius: 6px; padding: 10px; font-size: 15px; font-weight: 500; }"
        "QPushButton#registerPrimaryBtn:hover { background-color: %8; }"
        "QPushButton#registerPrimaryBtn:disabled { background-color: %5; color: %7; }"
        "QPushButton#registerLinkBtn { color: %4; border: none; background: transparent; font-size: 13px; }"
        "QPushButton#registerLinkBtn:hover { color: %8; text-decoration: underline; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->textColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->primaryHoverColor().name()));
}

void RegisterWindow::updateFormState()
{
    const QString userName = m_nameEdit->text().trimmed();
    const QString password = m_passwordEdit->text();
    const QString confirm = m_confirmEdit->text();
    const bool agreed = m_agreementCheck->isChecked();

    bool ready = true;
    QString feedback;

    if (!agreed) {
        feedback = QStringLiteral("请先勾选服务协议和隐私政策");
        ready = false;
    } else if (userName.isEmpty()) {
        feedback = QStringLiteral("请输入昵称");
        ready = false;
    } else if (password.length() < 6) {
        feedback = QStringLiteral("密码至少需要6位");
        ready = false;
    } else if (password != confirm) {
        feedback = QStringLiteral("两次输入的密码不一致");
        ready = false;
    } else {
        feedback = QStringLiteral("资料完整，点击立即注册");
    }

    m_registerBtn->setEnabled(ready);
    m_feedbackLabel->setText(feedback);
    m_titleLabel->setText(userName.isEmpty() ? QStringLiteral("欢迎注册 QQ") : QStringLiteral("注册昵称：%1").arg(userName));
}

QString RegisterWindow::userName() const
{
    return m_nameEdit->text().trimmed();
}

QString RegisterWindow::password() const
{
    return m_passwordEdit->text();
}

QString RegisterWindow::account() const
{
    return QString();
}

bool RegisterWindow::agreedToTerms() const
{
    return m_agreementCheck->isChecked();
}

