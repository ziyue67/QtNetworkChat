#include <QApplication>
#include <QStyleFactory>
#include "logincredentialstore.h"
#include "mainwindow.h"
#include "server.h"
#include "client.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QMessageBox>
#include <QHostAddress>
#include <QGridLayout>
#include <QFrame>
#include <QCheckBox>
#include <QSettings>
#include <QButtonGroup>
#include <QStyle>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
LONG WINAPI qqntUnhandledExceptionFilter(EXCEPTION_POINTERS* ep)
{
    HANDLE hFile = CreateFileW(L"qqnt-debug.log", FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        char buf[1024];
        DWORD written = 0;
        snprintf(buf, sizeof(buf), "[CRASH] Exception code: 0x%08X\n", (unsigned int)ep->ExceptionRecord->ExceptionCode);
        WriteFile(hFile, buf, (DWORD)strlen(buf), &written, NULL);
        snprintf(buf, sizeof(buf), "[CRASH] Exception address: 0x%p\n", (void*)ep->ExceptionRecord->ExceptionAddress);
        WriteFile(hFile, buf, (DWORD)strlen(buf), &written, NULL);

        void* stack[64];
        WORD frames = CaptureStackBackTrace(0, 64, stack, NULL);
        WriteFile(hFile, "[CRASH] Stack trace (return addresses):\n", 40, &written, NULL);
        for (WORD i = 0; i < frames; ++i) {
            snprintf(buf, sizeof(buf), "  %02u: 0x%p\n", i, stack[i]);
            WriteFile(hFile, buf, (DWORD)strlen(buf), &written, NULL);
        }
        CloseHandle(hFile);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

bool envEnabled(const char* name) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

bool canConnectToRedisEndpoint(const QString& host, quint16 port, int timeoutMs = 250) {
    QTcpSocket socket;
    socket.connectToHost(host, port);
    const bool connected = socket.waitForConnected(timeoutMs);
    if (connected) {
        socket.disconnectFromHost();
    }
    return connected;
}

bool canConnectToLocalChatService(quint16 port, int timeoutMs = 250) {
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), port);
    const bool connected = socket.waitForConnected(timeoutMs);
    if (connected) {
        socket.disconnectFromHost();
    }
    return connected;
}

void ensureDesktopRedisEnvironment() {
    if (envEnabled("QTNETWORKCHAT_REDIS")) {
        return;
    }

    const QByteArray configuredHost = qgetenv("QTNETWORKCHAT_REDIS_HOST").trimmed();
    const QString host = configuredHost.isEmpty()
        ? QStringLiteral("127.0.0.1")
        : QString::fromLocal8Bit(configuredHost);
    bool portOk = false;
    const int configuredPort = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_REDIS_PORT")).trimmed().toInt(&portOk);
    const quint16 port = portOk && configuredPort > 0 && configuredPort <= 65535
        ? static_cast<quint16>(configuredPort)
        : static_cast<quint16>(6379);

    if (!canConnectToRedisEndpoint(host, port)) {
        return;
    }

    qputenv("QTNETWORKCHAT_REDIS", "1");
    if (configuredHost.isEmpty()) {
        qputenv("QTNETWORKCHAT_REDIS_HOST", host.toUtf8());
    }
    if (qgetenv("QTNETWORKCHAT_REDIS_PORT").trimmed().isEmpty()) {
        qputenv("QTNETWORKCHAT_REDIS_PORT", QByteArray::number(port));
    }
    if (qgetenv("QTNETWORKCHAT_REDIS_PREFIX").trimmed().isEmpty()) {
        qputenv("QTNETWORKCHAT_REDIS_PREFIX", "qtchat");
    }
}

QString appDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

void maybeWriteDatabaseHealthSnapshot(const Server* server) {
    if (!server || !envEnabled("QTNETWORKCHAT_DB_HEALTH_EXPORT")) return;

    const QString configuredPath = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_DB_HEALTH_JSON")).trimmed();
    const QString defaultPath = QDir(appDataDir()).filePath("database-health.json");
    const QString outputPath = configuredPath.isEmpty() ? defaultPath : configuredPath;
    if (outputPath.trimmed().isEmpty()) return;

    const QFileInfo info(outputPath);
    if (!info.absoluteDir().exists()) {
        QDir().mkpath(info.absolutePath());
    }

    QJsonObject snapshot = server->databaseHealthSnapshot();
    snapshot["source"] = QStringLiteral("server-startup");
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to write database health snapshot:" << outputPath << file.errorString();
        return;
    }
    file.write(QJsonDocument(snapshot).toJson(QJsonDocument::Indented));
    file.close();
    qInfo() << "Database health snapshot written:" << outputPath << snapshot.value("status").toString();
}
}

class LoginDialog : public QDialog {
public:
    LoginDialog(bool isServer, QString& userName, QString& host, quint16& port, QWidget* parent = nullptr)
        : QDialog(parent), m_isServer(isServer), m_userName(userName), m_host(host), m_port(port)
    {
        m_registerMode = isServer;
        setWindowTitle(isServer ? "注册 QQ" : "QQ 登录");
        setFixedSize(342, isServer ? 560 : 522);
        setupUi();
        loadSettings();
        setRegisterMode(m_registerMode);
    }

    QString serverAddress() const { return m_host; }
    quint16 serverPort() const { return m_port; }
    QString userName() const { return m_userName; }
    QString account() const { return m_account; }
    QString password() const { return m_password; }
    bool registerMode() const { return m_registerMode; }
    bool serverMode() const { return m_isServer; }
    bool rememberPassword() const { return m_rememberCheck && m_rememberCheck->isChecked(); }
    bool saveResolvedLoginToSqlite(const QString& account, const QString& userName, const QString& password, bool rememberPassword) const {
        Q_UNUSED(password);
        return m_loginCredentialStore.save(account, userName, rememberPassword);
    }

private:
    void setupUi() {
        setObjectName("qqLoginDialog");
        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        QFrame* header = new QFrame(this);
        header->setObjectName("qqHeader");
        header->setFixedHeight(190);
        QVBoxLayout* headerLayout = new QVBoxLayout(header);
        headerLayout->setContentsMargins(22, 8, 22, 10);
        headerLayout->setSpacing(0);

        QHBoxLayout* titleBarLayout = new QHBoxLayout;
        QLabel* brandLabel = new QLabel("QQ", header);
        brandLabel->setObjectName("brandLabel");
        titleBarLayout->addWidget(brandLabel);
        titleBarLayout->addStretch();
        QPushButton* closeBtn = new QPushButton("×", header);
        closeBtn->setObjectName("windowCloseBtn");
        closeBtn->setFixedSize(28, 28);
        titleBarLayout->addWidget(closeBtn);
        headerLayout->addLayout(titleBarLayout);

        m_avatarLabel = new QLabel("Q", header);
        m_avatarLabel->setObjectName("qqAvatar");
        m_avatarLabel->setAlignment(Qt::AlignCenter);
        m_avatarLabel->setFixedSize(92, 92);
        headerLayout->addSpacing(24);
        headerLayout->addWidget(m_avatarLabel, 0, Qt::AlignCenter);

        m_titleLabel = new QLabel(header);
        m_titleLabel->setObjectName("qqTitle");
        m_titleLabel->setAlignment(Qt::AlignCenter);
        headerLayout->addWidget(m_titleLabel);
        mainLayout->addWidget(header);

        QFrame* formCard = new QFrame(this);
        formCard->setObjectName("qqFormCard");
        QVBoxLayout* formLayout = new QVBoxLayout(formCard);
        formLayout->setContentsMargins(33, 16, 33, 24);
        formLayout->setSpacing(10);

        m_accountEdit = new QLineEdit(formCard);
        m_accountEdit->setObjectName("qqInput");
        m_accountEdit->setPlaceholderText("QQ 号 / 账号");
        m_accountEdit->setClearButtonEnabled(true);
        m_accountEdit->setMaxLength(24);
        m_accountEdit->setToolTip("输入已有 QQ 账号登录本地聊天服务");
        formLayout->addWidget(m_accountEdit);

        m_nameEdit = new QLineEdit(formCard);
        m_nameEdit->setObjectName("qqInput");
        m_nameEdit->setPlaceholderText("昵称");
        m_nameEdit->setClearButtonEnabled(true);
        m_nameEdit->setMaxLength(20);
        m_nameEdit->setToolTip("注册时显示在聊天列表和消息里的昵称");
        formLayout->addWidget(m_nameEdit);

        m_passwordEdit = new QLineEdit(formCard);
        m_passwordEdit->setObjectName("qqInput");
        m_passwordEdit->setPlaceholderText("密码");
        m_passwordEdit->setEchoMode(QLineEdit::Password);
        m_passwordEdit->setClearButtonEnabled(true);
        m_passwordEdit->setMaxLength(32);
        m_passwordEdit->setToolTip("密码至少 6 位；本地不会持久化保存明文密码");
        formLayout->addWidget(m_passwordEdit);

        m_confirmPasswordEdit = new QLineEdit(formCard);
        m_confirmPasswordEdit->setObjectName("qqInput");
        m_confirmPasswordEdit->setPlaceholderText("确认密码");
        m_confirmPasswordEdit->setEchoMode(QLineEdit::Password);
        m_confirmPasswordEdit->setClearButtonEnabled(true);
        m_confirmPasswordEdit->setMaxLength(32);
        m_confirmPasswordEdit->setToolTip("再次输入密码，需与上一行一致");
        formLayout->addWidget(m_confirmPasswordEdit);

        QHBoxLayout* optionLayout = new QHBoxLayout;
        m_autoLoginCheck = new QCheckBox("自动登录", formCard);
        m_autoLoginCheck->setVisible(false);
        m_rememberCheck = new QCheckBox("记住密码", formCard);
        m_rememberCheck->setToolTip("仅记住账号和昵称；不会保存明文密码");
        optionLayout->addWidget(m_autoLoginCheck);
        optionLayout->addWidget(m_rememberCheck);
        optionLayout->addStretch();
        formLayout->addLayout(optionLayout);

        m_agreementCheck = new QCheckBox("已阅读并同意服务协议和隐私政策", formCard);
        m_agreementCheck->setObjectName("agreementCheck");
        m_agreementCheck->setToolTip("勾选后才能继续登录或注册");
        formLayout->addWidget(m_agreementCheck);

        m_feedbackLabel = new QLabel(formCard);
        m_feedbackLabel->setObjectName("formFeedbackLabel");
        m_feedbackLabel->setWordWrap(true);
        formLayout->addWidget(m_feedbackLabel);

        m_okBtn = new QPushButton(formCard);
        m_okBtn->setObjectName("primaryBtn");
        m_okBtn->setMinimumHeight(44);
        m_okBtn->setDefault(true);
        formLayout->addWidget(m_okBtn);

        QHBoxLayout* bottomLayout = new QHBoxLayout;
        bottomLayout->addStretch();
        m_loginLinkBtn = new QPushButton("账号登录", formCard);
        m_loginLinkBtn->setObjectName("linkBtn");
        m_registerLinkBtn = new QPushButton("注册账号", formCard);
        m_registerLinkBtn->setObjectName("linkBtn");
        bottomLayout->addWidget(m_loginLinkBtn);
        bottomLayout->addWidget(m_registerLinkBtn);
        bottomLayout->addStretch();
        formLayout->addLayout(bottomLayout);
        formLayout->addStretch();
        mainLayout->addWidget(formCard);

        if (m_isServer) {
            m_portSpin = new QSpinBox(formCard);
            m_portSpin->setRange(1024, 65535);
            m_portSpin->setValue(8888);
            m_portSpin->hide();
        }

        setStyleSheet(R"(
            QDialog#qqLoginDialog {
                background: #EFF6FA;
                font-family: "Microsoft YaHei", "Segoe UI";
                color: #253342;
            }
            QFrame#qqHeader {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #F6FEFF, stop:0.52 #DDF7F5, stop:1 #DCE8FF);
            }
            QLabel#brandLabel {
                color: #168BE8;
                font-size: 28px;
                font-weight: 900;
            }
            QPushButton#windowCloseBtn {
                background: transparent;
                color: #243447;
                border: none;
                font-size: 20px;
                font-weight: 300;
            }
            QPushButton#windowCloseBtn:hover {
                background: rgba(255, 255, 255, 120);
                border-radius: 14px;
            }
            QLabel#qqAvatar {
                background: white;
                color: #168BE8;
                border: 3px solid rgba(255, 255, 255, 220);
                border-radius: 46px;
                font-size: 42px;
                font-weight: 900;
            }
            QLabel#qqTitle {
                color: #203144;
                font-size: 17px;
                font-weight: 600;
                padding-top: 14px;
            }
            QFrame#qqFormCard {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #FFFFFF, stop:1 #F0F8FB);
            }
            QLineEdit#qqInput {
                min-height: 38px;
                border: none;
                border-bottom: 1px solid rgba(104, 143, 174, 100);
                padding: 4px 6px;
                background: transparent;
                color: #253342;
                font-size: 14px;
            }
            QLineEdit#qqInput:focus {
                border-bottom: 2px solid #17A8F3;
            }
            QCheckBox {
                color: #718395;
                font-size: 12px;
                spacing: 6px;
            }
            QCheckBox#agreementCheck {
                margin-top: 2px;
            }
            QLabel#formFeedbackLabel {
                min-height: 30px;
                border-radius: 10px;
                background: rgba(255, 248, 232, 170);
                color: #A36800;
                font-size: 12px;
                font-weight: 700;
                padding: 5px 9px;
            }
            QPushButton#primaryBtn {
                background: #18A8F2;
                color: white;
                border: none;
                border-radius: 7px;
                font-size: 16px;
                font-weight: 600;
            }
            QPushButton#primaryBtn:hover {
                background: #0E95DF;
            }
            QPushButton#primaryBtn:pressed {
                background: #0B7EC6;
            }
            QPushButton#primaryBtn:disabled {
                background: #BFD0DE;
                color: #F8FBFD;
            }
            QPushButton#linkBtn {
                background: transparent;
                color: #1679CA;
                border: none;
                padding: 6px 10px;
                font-size: 13px;
            }
            QPushButton#linkBtn:hover {
                color: #0B82E6;
                text-decoration: underline;
            }
        )");

        closeBtn->setToolTip("关闭登录窗口");
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_okBtn, &QPushButton::clicked, this, &LoginDialog::onOk);
        connect(m_loginLinkBtn, &QPushButton::clicked, this, [this]() { setRegisterMode(false); });
        connect(m_registerLinkBtn, &QPushButton::clicked, this, [this]() { setRegisterMode(true); });
        connect(m_accountEdit, &QLineEdit::textChanged, this, [this]() { updateFormState(); });
        connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() { updateFormState(); });
        connect(m_passwordEdit, &QLineEdit::textChanged, this, [this]() { updateFormState(); });
        connect(m_confirmPasswordEdit, &QLineEdit::textChanged, this, [this]() { updateFormState(); });
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        connect(m_agreementCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) { updateFormState(); });
        connect(m_rememberCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) { updateFormState(); });
#else
        connect(m_agreementCheck, &QCheckBox::stateChanged, this, [this](int) { updateFormState(); });
        connect(m_rememberCheck, &QCheckBox::stateChanged, this, [this](int) { updateFormState(); });
#endif
    }

    void setRegisterMode(bool registerMode) {
        m_registerMode = registerMode;
        setWindowTitle(registerMode ? "注册 QQ" : "QQ 登录");
        setFixedSize(342, registerMode ? 560 : 522);
        m_avatarLabel->setText(registerMode ? "注" : "Q");
        m_accountEdit->setVisible(!registerMode);
        m_accountEdit->setReadOnly(false);
        m_accountEdit->setPlaceholderText("请输入 QQ 账号");
        m_nameEdit->setVisible(registerMode);
        m_confirmPasswordEdit->setVisible(registerMode);
        m_autoLoginCheck->setVisible(false);
        m_rememberCheck->setVisible(!registerMode);
        m_loginLinkBtn->setVisible(registerMode);
        m_registerLinkBtn->setVisible(!registerMode && !m_isServer);
        m_okBtn->setText(registerMode ? "立即注册" : "登录");
        if (registerMode) {
            m_accountEdit->clear();
        }
        updateFormState();
    }

    void updateFormState() {
        if (!m_okBtn || !m_feedbackLabel) return;

        const QString account = m_accountEdit ? m_accountEdit->text().trimmed() : QString();
        const QString userName = m_nameEdit ? m_nameEdit->text().trimmed() : QString();
        const QString password = m_passwordEdit ? m_passwordEdit->text() : QString();
        const QString confirmPassword = m_confirmPasswordEdit ? m_confirmPasswordEdit->text() : QString();
        const bool agreed = m_agreementCheck && m_agreementCheck->isChecked();
        QString feedback;
        bool ready = true;

        if (!agreed) {
            feedback = "请先勾选服务协议和隐私政策";
            ready = false;
        } else if (!m_registerMode && account.isEmpty()) {
            feedback = "请输入 QQ 账号";
            ready = false;
        } else if (m_registerMode && userName.isEmpty()) {
            feedback = "注册时请输入昵称";
            ready = false;
        } else if (password.isEmpty()) {
            feedback = "请输入密码";
            ready = false;
        } else if (password.length() < 6) {
            feedback = QString("密码至少需要 6 位，当前 %1 位").arg(password.length());
            ready = false;
        } else if (m_registerMode && password != confirmPassword) {
            feedback = "两次输入的密码不一致";
            ready = false;
        } else {
            feedback = m_registerMode
                ? QString("资料完整，点击立即注册创建本地 QQ 账号")
                : QString("准备登录 QQ:%1%2")
                    .arg(account,
                         m_rememberCheck && m_rememberCheck->isChecked() ? "，仅记住账号信息" : "，本次不记住账号");
        }

        m_okBtn->setEnabled(ready);
        m_okBtn->setToolTip(ready ? (m_registerMode ? "创建本地 QQ 账号并进入聊天室" : "登录并进入聊天室") : feedback);
        m_feedbackLabel->setText(feedback);
        m_feedbackLabel->setStyleSheet(ready
            ? "min-height: 30px; border-radius: 10px; font-size: 12px; font-weight: 700; padding: 5px 9px; color: #12875A; background: rgba(232, 248, 239, 190);"
            : "min-height: 30px; border-radius: 10px; font-size: 12px; font-weight: 700; padding: 5px 9px; color: #A36800; background: rgba(255, 248, 232, 170);");
        m_titleLabel->setText(m_registerMode
            ? (userName.isEmpty() ? "欢迎注册 QQ" : QString("注册昵称：%1").arg(userName))
            : (account.isEmpty() ? "QQ 账号登录" : QString("QQ %1").arg(account)));
    }

    void onOk() {
        m_account = m_registerMode ? QString() : m_accountEdit->text().trimmed();
        m_password = m_passwordEdit->text();
        if (!m_agreementCheck->isChecked()) {
            QMessageBox::warning(this, "错误", "请先勾选同意服务协议和隐私政策");
            return;
        }
        if (!m_registerMode && m_account.isEmpty()) {
            QMessageBox::warning(this, "错误", "请输入 QQ 账号");
            return;
        }
        if (m_password.isEmpty()) {
            QMessageBox::warning(this, "错误", "请输入密码");
            return;
        }
        if (m_password.length() < 6) {
            QMessageBox::warning(this, "错误", "密码至少需要 6 位");
            return;
        }
        if (m_registerMode && m_password != m_confirmPasswordEdit->text()) {
            QMessageBox::warning(this, "错误", "两次输入的密码不一致");
            return;
        }
        m_userName = m_nameEdit->text().trimmed();
        if (m_userName.isEmpty()) {
            if (m_registerMode) {
                QMessageBox::warning(this, "错误", "注册时请输入昵称");
                return;
            }
            m_userName = m_account;
        }
        m_host = "127.0.0.1";
        m_port = 8888;
        accept();
    }

    void loadSettings() {
        if (m_isServer) return;
        if (loadLoginFromSqlite()) return;

        SavedLoginCredential credential;
        if (m_loginCredentialStore.migrateLegacySettings(&credential) && !credential.account.isEmpty()) {
            m_accountEdit->setText(credential.account);
            m_nameEdit->setText(credential.userName);
            m_passwordEdit->clear();
            m_rememberCheck->setChecked(credential.rememberPassword);
        }
    }

    void saveSettings() {
        if (m_registerMode || !m_rememberCheck) return;
        if (!saveLoginToSqlite()) {
            QSettings fallback("QtNetworkChat", "QtNetworkChat");
            fallback.setValue("login/account", m_accountEdit->text().trimmed());
            fallback.setValue("login/name", m_nameEdit->text().trimmed());
            fallback.setValue("login/remember", m_rememberCheck->isChecked());
            fallback.remove("login/password");
            return;
        }

        QSettings settings("QtNetworkChat", "QtNetworkChat");
        settings.setValue("login/account", m_accountEdit->text().trimmed());
        settings.setValue("login/name", m_nameEdit->text().trimmed());
        settings.setValue("login/remember", m_rememberCheck->isChecked());
        settings.remove("login/password");
    }

    bool loadLoginFromSqlite() {
        SavedLoginCredential credential;
        if (!m_loginCredentialStore.load(&credential)) return false;
        m_accountEdit->setText(credential.account);
        m_nameEdit->setText(credential.userName);
        m_passwordEdit->clear();
        m_rememberCheck->setChecked(credential.rememberPassword);
        return true;
    }

    bool saveLoginToSqlite() const {
        return m_loginCredentialStore.save(m_accountEdit->text().trimmed(),
                                           m_nameEdit->text().trimmed(),
                                           m_rememberCheck && m_rememberCheck->isChecked());
    }

    bool m_isServer;
    QString& m_userName;
    QString& m_host;
    quint16& m_port;
    LoginCredentialStore m_loginCredentialStore;
    QString m_account;
    QString m_password;
    bool m_registerMode = false;
    QLabel* m_avatarLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_feedbackLabel = nullptr;
    QLineEdit* m_accountEdit = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QLineEdit* m_confirmPasswordEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QCheckBox* m_autoLoginCheck = nullptr;
    QCheckBox* m_rememberCheck = nullptr;
    QCheckBox* m_agreementCheck = nullptr;
    QPushButton* m_okBtn = nullptr;
    QPushButton* m_loginLinkBtn = nullptr;
    QPushButton* m_registerLinkBtn = nullptr;
    QSpinBox* m_portSpin = nullptr;
};

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(qqntUnhandledExceptionFilter);
#endif

    QApplication a(argc, argv);
    ensureDesktopRedisEnvironment();
    a.setApplicationName("QtNetworkChat");
    a.setApplicationVersion("1.0.0");
    a.setStyle(QStyleFactory::create("Fusion"));

    // Debug auto-login: bypass mode/login dialogs to reproduce post-login crashes
    if (qEnvironmentVariableIsSet("QTNETWORKCHAT_DEBUG_AUTO_LOGIN")) {
        Server* server = new Server(&a);
        if (!server->start(8888)) {
            delete server;
            server = nullptr;
        }
        maybeWriteDatabaseHealthSnapshot(server);

        Client* client = new Client;
        client->setUserInfo("", "DebugUser");
        client->setAccountInfo("debug123", "123456", true);
        client->connectToServer("127.0.0.1", 8888);
        if (!client->isConnected() || !client->waitForLoginResult()) {
            qDebug() << "Debug auto-login failed:" << client->lastLoginError();
            delete client;
            return 1;
        }
        MainWindow* w = new MainWindow(client, client->currentUserId(), client->currentUserName());
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
        return a.exec();
    }

    QString userName, host;
    quint16 port = 8888;

    QDialog* modeDialog = new QDialog;
    modeDialog->setObjectName("modeDialog");
    modeDialog->setWindowTitle("QtNetworkChat");
    modeDialog->setFixedSize(322, 460);
    QVBoxLayout* layout = new QVBoxLayout(modeDialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QFrame* modeCard = new QFrame(modeDialog);
    modeCard->setObjectName("modeCard");
    QVBoxLayout* modeCardLayout = new QVBoxLayout(modeCard);
    modeCardLayout->setContentsMargins(32, 10, 32, 22);
    modeCardLayout->setSpacing(0);

    QHBoxLayout* topBarLayout = new QHBoxLayout;
    topBarLayout->addStretch();
    QPushButton* menuBtn = new QPushButton("☰", modeCard);
    menuBtn->setObjectName("topIconBtn");
    menuBtn->setFixedSize(32, 28);
    QPushButton* closeBtn = new QPushButton("×", modeCard);
    closeBtn->setObjectName("topIconBtn");
    closeBtn->setFixedSize(32, 28);
    topBarLayout->addWidget(menuBtn);
    topBarLayout->addWidget(closeBtn);
    modeCardLayout->addLayout(topBarLayout);

    QLabel* logoLabel = new QLabel("QQ", modeCard);
    logoLabel->setObjectName("logoLabel");
    logoLabel->setAlignment(Qt::AlignCenter);
    modeCardLayout->addWidget(logoLabel, 0, Qt::AlignCenter);

    modeCardLayout->addSpacing(28);

    QLabel* avatarLabel = new QLabel("Q", modeCard);
    avatarLabel->setObjectName("avatarLabel");
    avatarLabel->setAlignment(Qt::AlignCenter);
    avatarLabel->setFixedSize(92, 92);
    modeCardLayout->addWidget(avatarLabel, 0, Qt::AlignCenter);

    QLabel* nameLabel = new QLabel("QtNetworkChat", modeCard);
    nameLabel->setObjectName("accountNameLabel");
    nameLabel->setAlignment(Qt::AlignCenter);
    modeCardLayout->addWidget(nameLabel);

    QLabel* serviceBadgeLabel = new QLabel("本地服务", modeCard);
    serviceBadgeLabel->setObjectName("serviceBadgeLabel");
    serviceBadgeLabel->setAlignment(Qt::AlignCenter);
    modeCardLayout->addWidget(serviceBadgeLabel, 0, Qt::AlignCenter);

    QLabel* modeStatusLabel = new QLabel("正在准备本地服务...", modeCard);
    modeStatusLabel->setObjectName("modeStatusLabel");
    modeStatusLabel->setAlignment(Qt::AlignCenter);
    modeStatusLabel->setWordWrap(true);
    modeCardLayout->addWidget(modeStatusLabel);

    modeCardLayout->addSpacing(18);

    QPushButton* clientBtn = new QPushButton("登录", modeCard);
    clientBtn->setObjectName("primaryBtn");
    clientBtn->setMinimumHeight(40);
    clientBtn->setToolTip("使用已有 QQ 账号登录当前本地聊天服务");
    modeCardLayout->addWidget(clientBtn);

    modeCardLayout->addStretch();

    QHBoxLayout* bottomLinkLayout = new QHBoxLayout;
    bottomLinkLayout->addStretch();
    QPushButton* accountLoginBtn = new QPushButton("账号登录", modeCard);
    accountLoginBtn->setObjectName("linkBtn");
    QPushButton* serverBtn = new QPushButton("注册账号", modeCard);
    serverBtn->setObjectName("linkBtn");
    serverBtn->setToolTip("创建一个新的本地 QQ 测试账号");
    bottomLinkLayout->addWidget(accountLoginBtn);
    QLabel* splitLabel = new QLabel("|", modeCard);
    splitLabel->setObjectName("splitLabel");
    bottomLinkLayout->addWidget(splitLabel);
    bottomLinkLayout->addWidget(serverBtn);
    bottomLinkLayout->addStretch();
    modeCardLayout->addLayout(bottomLinkLayout);
    layout->addWidget(modeCard);

    modeDialog->setStyleSheet(R"(
        QDialog#modeDialog {
            background: #EFF6FA;
            font-family: "Microsoft YaHei", "Segoe UI";
            color: #253342;
        }
        QFrame#modeCard {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #F6FEFF, stop:0.52 #DDF7F5, stop:1 #DCE8FF);
        }
        QPushButton#topIconBtn {
            background: transparent;
            color: #243447;
            border: none;
            font-size: 17px;
        }
        QPushButton#topIconBtn:hover {
            background: rgba(255, 255, 255, 120);
            border-radius: 14px;
        }
        QLabel#logoLabel {
            color: #168BE8;
            font-size: 30px;
            font-weight: 900;
        }
        QLabel#avatarLabel {
            background: white;
            color: #168BE8;
            border: 3px solid rgba(255, 255, 255, 220);
            border-radius: 46px;
            font-size: 42px;
            font-weight: 900;
        }
        QLabel#accountNameLabel {
            color: #203144;
            font-size: 17px;
            font-weight: 600;
            padding-top: 16px;
            padding-bottom: 8px;
        }
        QLabel#serviceBadgeLabel {
            background: rgba(255, 255, 255, 175);
            color: #1679CA;
            border: 1px solid rgba(22, 121, 202, 60);
            border-radius: 10px;
            font-size: 12px;
            font-weight: 600;
            padding: 3px 10px;
        }
        QLabel#modeStatusLabel {
            color: #5C7286;
            font-size: 12px;
            padding-top: 10px;
            padding-bottom: 2px;
        }
        QPushButton#primaryBtn {
            background: #18A8F2;
            color: white;
            border: none;
            border-radius: 7px;
            font-size: 16px;
            font-weight: 600;
        }
        QPushButton#primaryBtn:hover {
            background: #0E95DF;
        }
        QPushButton#primaryBtn:pressed {
            background: #0B7EC6;
        }
        QPushButton#linkBtn {
            background: transparent;
            color: #1679CA;
            border: none;
            padding: 5px 6px;
            font-size: 13px;
        }
        QPushButton#linkBtn:hover {
            color: #006FCE;
            text-decoration: underline;
        }
        QLabel#splitLabel {
            color: #8BA6BC;
        }
    )");
    QObject::connect(closeBtn, &QPushButton::clicked, modeDialog, &QDialog::reject);
    QObject::connect(accountLoginBtn, &QPushButton::clicked, clientBtn, &QPushButton::click);

    LoginDialog* loginDlg = nullptr;
    Server* server = new Server(&a);
    if (!server->start(port)) {
        delete server;
        server = nullptr;
        if (canConnectToLocalChatService(port, 250)) {
            serviceBadgeLabel->setText("连接已有服务");
            serviceBadgeLabel->setToolTip("本机 8888 端口已有 QtNetworkChat 服务窗口托管，本窗口将作为客户端加入");
            modeStatusLabel->setText("检测到已有本地聊天服务，本窗口可直接登录或注册第二个账号进行双开测试。");
        } else {
            nameLabel->setText("QtNetworkChat · Redis 未就绪");
            serviceBadgeLabel->setText("服务未启动");
            serviceBadgeLabel->setToolTip("本地服务端依赖 Redis；启动失败时不会降级到无 Redis 模式");
            modeStatusLabel->setText("本地服务启动失败，请先确认 Redis 已在 127.0.0.1:6379 可连接，然后重新打开程序。");
        }
    } else {
        serviceBadgeLabel->setText("托管本地服务");
        serviceBadgeLabel->setToolTip("本窗口已启动端口 8888，且 Redis 已通过启动检查");
        modeStatusLabel->setText("本窗口正在托管本地服务，Redis 已就绪，可再打开一个客户端测试互发消息。");
        maybeWriteDatabaseHealthSnapshot(server);
    }
    Client* client = nullptr;

    QObject::connect(serverBtn, &QPushButton::clicked, [&]() {
        loginDlg = new LoginDialog(true, userName, host, port, modeDialog);
        modeDialog->hide();
        if (loginDlg->exec() == QDialog::Accepted) {
            port = 8888;
            client = new Client;
            client->setUserInfo("", userName);
            client->setAccountInfo(loginDlg->account(), loginDlg->password(), true);
            client->connectToServer("127.0.0.1", port);
            if (!client->isConnected() || !client->waitForLoginResult()) {
                QString reason = client->lastLoginError().isEmpty() ? "无法连接本地测试服务" : client->lastLoginError();
                QMessageBox::critical(nullptr, "注册失败", reason);
                delete client;
                client = nullptr;
                modeDialog->show();
                return;
            }
            if (client->currentLoginWasRegister()) {
                loginDlg->saveResolvedLoginToSqlite(client->currentUserId(),
                                                    client->currentUserName(),
                                                    loginDlg->password(),
                                                    true);
                QMessageBox::information(nullptr,
                                         "注册成功",
                                         QString("你的 QQ 账号是：%1\n账号和密码已保存到本地 SQLite 登录库，之后登录和加好友都使用它。")
                                             .arg(client->currentUserId()));
            }
            MainWindow* w = new MainWindow(client, client->currentUserId(), client->currentUserName());
            w->setAttribute(Qt::WA_DeleteOnClose);
            QObject::connect(w, &MainWindow::logoutRequested, [&]() {
                w->close();
                delete client;
                client = nullptr;
                modeDialog->show();
            });
            QObject::connect(w, &QObject::destroyed, [&]() {
                client = nullptr;
            });
            w->show();
        } else {
            modeDialog->show();
        }
    });

    QObject::connect(clientBtn, &QPushButton::clicked, [&]() {
        loginDlg = new LoginDialog(false, userName, host, port, modeDialog);
        modeDialog->hide();
        if (loginDlg->exec() == QDialog::Accepted) {
            host = "127.0.0.1";
            port = 8888;
            userName = loginDlg->userName();
            client = new Client;
            client->setUserInfo("", userName);
            client->setAccountInfo(loginDlg->account(), loginDlg->password(), loginDlg->registerMode());
            client->connectToServer(host, port);
            if (!client->isConnected() || !client->waitForLoginResult()) {
                QString reason = client->lastLoginError().isEmpty()
                    ? QString("无法连接本地测试服务")
                    : client->lastLoginError();
                QMessageBox::critical(nullptr, "连接失败", reason);
                delete client;
                client = nullptr;
                modeDialog->show();
                return;
            }
            loginDlg->saveResolvedLoginToSqlite(loginDlg->account(),
                                                client->currentUserName(),
                                                loginDlg->password(),
                                                loginDlg->rememberPassword());
            MainWindow* w = new MainWindow(client, client->currentUserId(), client->currentUserName());
            w->setAttribute(Qt::WA_DeleteOnClose);
            QObject::connect(w, &MainWindow::logoutRequested, [&]() {
                w->close();
                delete client;
                client = nullptr;
                modeDialog->show();
            });
            QObject::connect(w, &QObject::destroyed, [&]() {
                client = nullptr;
            });
            w->show();
        } else {
            modeDialog->show();
        }
    });

    modeDialog->show();
    return a.exec();
}
