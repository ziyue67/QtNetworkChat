#include "windows/loginwindow.h"

#include "theme/thememanager.h"
#include "widgets/avatarlabel.h"
#include "logincredentialstore.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QCheckBox>
#include <QMessageBox>
#include <QPainter>
#include <QVBoxLayout>

namespace {
QString normalizedPasswordInput(QString password)
{
    // Chinese IMEs and clipboard tools can produce full-width digits or zero-width
    // separators. Normalize only presentation-equivalent input before it reaches
    // the credential protocol so a visually identical local password stays stable.
    password.remove(QChar(0x200B));
    password.remove(QChar(0xFEFF));
    password = password.trimmed();
    for (int i = 0; i < password.size(); ++i) {
        const ushort code = password.at(i).unicode();
        if (code >= 0xFF01 && code <= 0xFF5E) {
            password[i] = QChar(code - 0xFEE0);
        }
    }
    return password;
}

class ReadableCheckBox final : public QCheckBox {
public:
    explicit ReadableCheckBox(const QString& text, QWidget* parent = nullptr)
        : QCheckBox(text, parent) {
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(22);
    }

    QSize sizeHint() const override {
        const QFontMetrics metrics(font());
        return QSize(metrics.horizontalAdvance(text()) + 30, qMax(22, metrics.height() + 4));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRect box(0, (height() - 16) / 2, 16, 16);
        const bool dark = ThemeManager::instance()->isDark();
        const QColor primary = ThemeManager::instance()->primaryColor();

        painter.setPen(QPen(isChecked() ? primary : primary, 1));
        painter.setBrush(isChecked() ? primary : (dark ? QColor("#252525") : Qt::white));
        painter.drawRoundedRect(box.adjusted(0, 0, -1, -1), 4, 4);
        if (isChecked()) {
            painter.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(QPoint(3, box.center().y()), QPoint(6, box.bottom() - 4));
            painter.drawLine(QPoint(6, box.bottom() - 4), QPoint(13, box.top() + 4));
        }

        painter.setPen(ThemeManager::instance()->textColor());
        painter.setFont(font());
        painter.drawText(QRect(23, 0, width() - 23, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
};
}

LoginWindow::LoginWindow(QWidget* parent)
    : QDialog(parent)
    
{
    setObjectName(QStringLiteral("loginWindow"));
    setWindowTitle(QStringLiteral("QQ 登录"));
    setFixedSize(400, 520);
    // Keep the native dialog frame. The launcher and the main window use the
    // application-wide QQNT stylesheet, while this dialog owns its own visual
    // treatment; a native frame also keeps the close affordance reliable.
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
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

    auto* showPasswordCheck = new QCheckBox(QStringLiteral("显示密码"), formCard);
    showPasswordCheck->setObjectName(QStringLiteral("loginCheck"));
    connect(showPasswordCheck, &QCheckBox::toggled, this, [this](bool visible) {
        m_passwordEdit->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        m_confirmEdit->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
    });
    formLayout->addWidget(showPasswordCheck);

    // Options
    QHBoxLayout* optionLayout = new QHBoxLayout();
    m_rememberCheck = new ReadableCheckBox(QStringLiteral("记住密码"), formCard);
    m_rememberCheck->setObjectName(QStringLiteral("loginCheck"));
    optionLayout->addWidget(m_rememberCheck);
    optionLayout->addStretch();
    formLayout->addLayout(optionLayout);

    // Agreement
    m_agreementCheck = new ReadableCheckBox(QStringLiteral("我已阅读并同意服务协议和隐私政策"), formCard);
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
        const QString password = normalizedPasswordInput(m_passwordEdit->text());
        const QString confirmation = normalizedPasswordInput(m_confirmEdit->text());
        if (password.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请输入密码"));
            return;
        }
        if (password.length() < 6) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("密码至少需要6位"));
            return;
        }
        if (m_registerMode && password != confirmation) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("两次输入的密码不一致"));
            return;
        }
        m_account = m_registerMode ? QString() : m_accountEdit->text().trimmed();
        m_password = password;
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
    m_resetPasswordBtn = new QPushButton(QStringLiteral("重置本地密码"), formCard);
    m_resetPasswordBtn->setObjectName(QStringLiteral("loginLinkBtn"));
    m_resetPasswordBtn->setFlat(true);
    connect(m_resetPasswordBtn, &QPushButton::clicked, this, [this]() {
        m_registerMode = false;
        m_resetPasswordMode = true;
        m_titleLabel->setText(QStringLiteral("重置本地账号密码"));
        m_okBtn->setText(QStringLiteral("重置并登录"));
        m_feedbackLabel->setText(QStringLiteral("输入 QQ 号和新密码后，将重置本机 SQLite 账号密码"));
        m_accountEdit->setFocus();
        updateFormState();
    });
    linkLayout->addWidget(m_resetPasswordBtn);
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
    QString style = QStringLiteral(
        "QDialog#loginWindow { background-color: {bg}; }"
        "QFrame#loginHeader { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 {headerStart}, stop:1 {headerEnd}); border-bottom: 1px solid {border}; }"
        "QLabel#brandLabel { color: {primary}; font-size: 24px; font-weight: 800; }"
        "QLabel#loginTitleLabel { color: {text}; font-size: 16px; font-weight: 600; }"
        "QFrame#loginFormCard { background-color: {bg}; }"
        "QLineEdit#loginInput { background-color: {bgSecondary}; color: {text}; border: 1px solid {border}; border-radius: 6px; padding: 10px 14px; font-size: 14px; font-weight: 500; selection-background-color: {primary}; selection-color: white; }"
        "QLineEdit#loginInput:placeholder { color: {placeholder}; }"
        "QLineEdit#loginInput:focus { border: 1px solid {primary}; }"
        "QLabel#loginFeedbackLabel { color: {textSecondary}; font-size: 12px; padding: 4px 8px; border-radius: 6px; background-color: {bgSecondary}; }"
        "QPushButton#loginPrimaryBtn { background-color: {primary}; color: white; border: none; border-radius: 6px; padding: 10px; font-size: 15px; font-weight: 500; }"
        "QPushButton#loginPrimaryBtn:hover { background-color: {primaryHover}; }"
        "QPushButton#loginPrimaryBtn:disabled { background-color: {border}; color: {textSecondary}; }"
        "QPushButton#loginLinkBtn { color: {primary}; border: none; background: transparent; font-size: 13px; }"
        "QPushButton#loginLinkBtn:hover { color: {primaryHover}; text-decoration: underline; }"
        "QPushButton#windowCloseBtn { color: white; border: none; background: transparent; font-size: 16px; }"
        "QPushButton#windowCloseBtn:hover { background-color: #ff4d4f; border-radius: 4px; }"
    );
    const auto replace = [&style](const QString& token, const QColor& color) {
        style.replace(token, color.name());
    };
    replace(QStringLiteral("{bg}"), tm->backgroundColor());
    replace(QStringLiteral("{bgSecondary}"), tm->backgroundSecondaryColor());
    replace(QStringLiteral("{text}"), tm->textColor());
    replace(QStringLiteral("{textSecondary}"), tm->textSecondaryColor());
    replace(QStringLiteral("{primary}"), tm->primaryColor());
    replace(QStringLiteral("{primaryHover}"), tm->primaryHoverColor());
    replace(QStringLiteral("{border}"), tm->borderColor());
    replace(QStringLiteral("{headerStart}"), tm->isDark() ? QColor(QStringLiteral("#252525")) : QColor(QStringLiteral("#f6feff")));
    replace(QStringLiteral("{headerEnd}"), tm->isDark() ? QColor(QStringLiteral("#1e2f3f")) : QColor(QStringLiteral("#e6f4ff")));
    replace(QStringLiteral("{placeholder}"), tm->isDark() ? QColor(QStringLiteral("#b9c2cf")) : QColor(QStringLiteral("#687386")));
    setStyleSheet(style);

    const QColor inputText = tm->textColor();
    const QColor placeholder = tm->isDark() ? QColor(QStringLiteral("#b9c2cf"))
                                             : QColor(QStringLiteral("#687386"));
    for (QLineEdit* edit : {m_accountEdit, m_nameEdit, m_passwordEdit, m_confirmEdit}) {
        QPalette palette = edit->palette();
        palette.setColor(QPalette::Text, inputText);
        palette.setColor(QPalette::PlaceholderText, placeholder);
        edit->setPalette(palette);
        edit->setStyleSheet(QStringLiteral(
            "QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; "
            "padding: 10px 14px; font-size: 14px; font-weight: 500; selection-background-color: %4; selection-color: white; }"
            "QLineEdit:focus { border: 2px solid %4; }")
            .arg(tm->backgroundSecondaryColor().name(), inputText.name(), tm->borderColor().name(), tm->primaryColor().name()));
    }
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
        feedback = m_resetPasswordMode
            ? QStringLiteral("将重置 QQ:%1 的本机密码并直接登录").arg(account)
            : (m_registerMode
            ? QStringLiteral("资料完整，点击立即注册")
            : QStringLiteral("准备登录 QQ:%1").arg(account));
    }

    m_okBtn->setEnabled(ready);
    m_feedbackLabel->setText(feedback);
    m_titleLabel->setText(m_resetPasswordMode
        ? QStringLiteral("重置本地账号密码")
        : (m_registerMode
        ? (userName.isEmpty() ? QStringLiteral("欢迎注册 QQ") : QStringLiteral("注册昵称：%1").arg(userName))
        : (account.isEmpty() ? QStringLiteral("QQ 账号登录") : QStringLiteral("QQ %1").arg(account))));
}

void LoginWindow::setRegisterMode(bool registerMode)
{
    m_registerMode = registerMode;
    m_resetPasswordMode = false;
    setWindowTitle(registerMode ? QStringLiteral("注册 QQ") : QStringLiteral("QQ 登录"));
    setFixedSize(400, registerMode ? 560 : 520);
    m_avatar->setTextAvatar(registerMode ? QStringLiteral("注") : QStringLiteral("Q"), QColor(QStringLiteral("#0099ff")));
    m_accountEdit->setVisible(!registerMode);
    m_nameEdit->setVisible(registerMode);
    m_confirmEdit->setVisible(registerMode);
    m_rememberCheck->setVisible(!registerMode);
    m_registerLinkBtn->setVisible(!registerMode);
    m_loginLinkBtn->setVisible(registerMode);
    m_resetPasswordBtn->setVisible(!registerMode);
    m_okBtn->setText(registerMode ? QStringLiteral("立即注册") : QStringLiteral("登录"));
    if (registerMode) {
        m_accountEdit->clear();
    }
    updateFormState();
}

void LoginWindow::loadSettings()
{
    // Prefer the SQLite login store; fall back to migrating legacy QSettings.
    if (loadLoginFromSqlite()) {
        return;
    }

    SavedLoginCredential credential;
    if (m_loginCredentialStore.migrateLegacySettings(&credential) && !credential.account.isEmpty()) {
        m_accountEdit->setText(credential.account);
        m_nameEdit->setText(credential.userName);
        m_passwordEdit->clear();
        m_rememberCheck->setChecked(credential.rememberPassword);
    }
}

bool LoginWindow::loadLoginFromSqlite()
{
    SavedLoginCredential credential;
    if (!m_loginCredentialStore.load(&credential)) {
        return false;
    }
    m_accountEdit->setText(credential.account);
    m_nameEdit->setText(credential.userName);
    m_passwordEdit->clear();
    m_rememberCheck->setChecked(credential.rememberPassword);
    return true;
}

bool LoginWindow::saveResolvedLoginToSqlite(const QString& account, const QString& userName, bool rememberPassword)
{
    // Only the account and nickname are persisted; the plaintext password is
    // never written to the login store.
    return m_loginCredentialStore.save(account, userName, rememberPassword);
}

QString LoginWindow::userName() const { return m_userName; }
QString LoginWindow::account() const { return m_account; }
QString LoginWindow::password() const { return m_password; }
QString LoginWindow::serverAddress() const { return m_host; }
quint16 LoginWindow::serverPort() const { return m_port; }
bool LoginWindow::rememberPassword() const { return m_rememberCheck->isChecked(); }
bool LoginWindow::registerMode() const { return m_registerMode; }
QString LoginWindow::loginMode() const {
    if (m_resetPasswordMode) return QStringLiteral("reset_password");
    return m_registerMode ? QStringLiteral("register") : QStringLiteral("login");
}
