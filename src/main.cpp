#include <QApplication>
#include <QStyleFactory>
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
#include <QSqlDatabase>
#include <QSqlQuery>

class LoginDialog : public QDialog {
public:
    LoginDialog(bool isServer, QString& userName, QString& host, quint16& port, QWidget* parent = nullptr)
        : QDialog(parent), m_isServer(isServer), m_userName(userName), m_host(host), m_port(port)
    {
        m_registerMode = isServer;
        setWindowTitle(isServer ? "注册 QQ" : "QQ 登录");
        setFixedSize(322, isServer ? 520 : 486);
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
        formLayout->addWidget(m_accountEdit);

        m_nameEdit = new QLineEdit(formCard);
        m_nameEdit->setObjectName("qqInput");
        m_nameEdit->setPlaceholderText("昵称");
        m_nameEdit->setMaxLength(20);
        formLayout->addWidget(m_nameEdit);

        m_passwordEdit = new QLineEdit(formCard);
        m_passwordEdit->setObjectName("qqInput");
        m_passwordEdit->setPlaceholderText("密码");
        m_passwordEdit->setEchoMode(QLineEdit::Password);
        formLayout->addWidget(m_passwordEdit);

        m_confirmPasswordEdit = new QLineEdit(formCard);
        m_confirmPasswordEdit->setObjectName("qqInput");
        m_confirmPasswordEdit->setPlaceholderText("确认密码");
        m_confirmPasswordEdit->setEchoMode(QLineEdit::Password);
        formLayout->addWidget(m_confirmPasswordEdit);

        QHBoxLayout* optionLayout = new QHBoxLayout;
        m_autoLoginCheck = new QCheckBox("自动登录", formCard);
        m_autoLoginCheck->setVisible(false);
        m_rememberCheck = new QCheckBox("记住密码", formCard);
        m_rememberCheck->setToolTip("仅在勾选时保存密码；取消勾选后会清除已保存的密码");
        optionLayout->addWidget(m_autoLoginCheck);
        optionLayout->addWidget(m_rememberCheck);
        optionLayout->addStretch();
        formLayout->addLayout(optionLayout);

        m_agreementCheck = new QCheckBox("已阅读并同意服务协议和隐私政策", formCard);
        m_agreementCheck->setObjectName("agreementCheck");
        formLayout->addWidget(m_agreementCheck);

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
                padding: 6px 10px;
                font-size: 13px;
            }
            QPushButton#linkBtn:hover {
                color: #0B82E6;
                text-decoration: underline;
            }
        )");

        connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_okBtn, &QPushButton::clicked, this, &LoginDialog::onOk);
        connect(m_loginLinkBtn, &QPushButton::clicked, this, [this]() { setRegisterMode(false); });
        connect(m_registerLinkBtn, &QPushButton::clicked, this, [this]() { setRegisterMode(true); });
    }

    void setRegisterMode(bool registerMode) {
        m_registerMode = registerMode;
        setWindowTitle(registerMode ? "注册 QQ" : "QQ 登录");
        setFixedSize(322, registerMode ? 520 : 486);
        m_titleLabel->setText(registerMode ? "欢迎注册 QQ" : (m_accountEdit->text().trimmed().isEmpty() ? "QQ 账号登录" : m_accountEdit->text().trimmed()));
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
        saveSettings();
        m_host = "127.0.0.1";
        m_port = 8888;
        accept();
    }

    void loadSettings() {
        if (m_isServer) return;
        if (loadLoginFromSqlite()) return;

        QSettings settings("QtNetworkChat", "QtNetworkChat");
        const bool rememberPassword = settings.value("login/remember", false).toBool();
        m_accountEdit->setText(settings.value("login/account").toString());
        m_passwordEdit->setText(rememberPassword ? settings.value("login/password").toString() : QString());
        m_nameEdit->setText(settings.value("login/name").toString());
        m_rememberCheck->setChecked(rememberPassword);
        saveLoginToSqlite();
    }

    void saveSettings() {
        if (m_registerMode || !m_rememberCheck) return;
        if (!saveLoginToSqlite()) {
            QSettings fallback("QtNetworkChat", "QtNetworkChat");
            fallback.setValue("login/account", m_accountEdit->text().trimmed());
            fallback.setValue("login/name", m_nameEdit->text().trimmed());
            fallback.setValue("login/remember", m_rememberCheck->isChecked());
            if (m_rememberCheck->isChecked()) {
                fallback.setValue("login/password", m_passwordEdit->text());
            } else {
                fallback.remove("login/password");
            }
            return;
        }

        QSettings settings("QtNetworkChat", "QtNetworkChat");
        settings.setValue("login/account", m_accountEdit->text().trimmed());
        settings.setValue("login/name", m_nameEdit->text().trimmed());
        settings.setValue("login/remember", m_rememberCheck->isChecked());
        settings.remove("login/password");
    }

    QString loginDbPath() const {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (dir.isEmpty()) dir = ".";
        QDir().mkpath(dir);
        return dir + "/login_accounts.sqlite3";
    }

    bool ensureLoginDatabase() const {
        const QString connectionName = "login_accounts_init_" + QString::number(reinterpret_cast<quintptr>(this));
        bool ok = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(loginDbPath());
            if (db.open()) {
                QSqlQuery query(db);
                ok = query.exec("CREATE TABLE IF NOT EXISTS login_accounts ("
                                "account TEXT PRIMARY KEY, "
                                "user_name TEXT, "
                                "password TEXT, "
                                "remember_password INTEGER DEFAULT 0, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
        return ok;
    }

    bool loadLoginFromSqlite() {
        if (!ensureLoginDatabase()) return false;

        const QString connectionName = "login_accounts_read_" + QString::number(reinterpret_cast<quintptr>(this));
        bool loaded = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(loginDbPath());
            if (db.open()) {
                QSqlQuery query(db);
                if (query.exec("SELECT account, user_name, password, remember_password FROM login_accounts ORDER BY updated_at DESC LIMIT 1")
                    && query.next()) {
                    const bool rememberPassword = query.value(3).toInt() != 0;
                    m_accountEdit->setText(query.value(0).toString());
                    m_nameEdit->setText(query.value(1).toString());
                    m_passwordEdit->setText(rememberPassword ? query.value(2).toString() : QString());
                    m_rememberCheck->setChecked(rememberPassword);
                    loaded = true;
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
        return loaded;
    }

    bool saveLoginToSqlite() const {
        if (!ensureLoginDatabase()) return false;

        const QString account = m_accountEdit->text().trimmed();
        if (account.isEmpty()) return true;

        const QString connectionName = "login_accounts_write_" + QString::number(reinterpret_cast<quintptr>(this));
        bool ok = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(loginDbPath());
            if (db.open()) {
                QSqlQuery query(db);
                query.prepare("INSERT OR REPLACE INTO login_accounts(account, user_name, password, remember_password, updated_at) "
                              "VALUES(?, ?, ?, ?, datetime('now'))");
                query.addBindValue(account);
                query.addBindValue(m_nameEdit->text().trimmed());
                query.addBindValue(m_rememberCheck && m_rememberCheck->isChecked() ? m_passwordEdit->text() : QString());
                query.addBindValue(m_rememberCheck && m_rememberCheck->isChecked() ? 1 : 0);
                ok = query.exec();
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
        return ok;
    }

    bool m_isServer;
    QString& m_userName;
    QString& m_host;
    quint16& m_port;
    QString m_account;
    QString m_password;
    bool m_registerMode = false;
    QLabel* m_avatarLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
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
    QApplication a(argc, argv);
    a.setApplicationName("QtNetworkChat");
    a.setApplicationVersion("1.0.0");
    a.setStyle(QStyleFactory::create("Fusion"));

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
        nameLabel->setText("QtNetworkChat · 客户端");
        serviceBadgeLabel->setText("连接已有服务");
        serviceBadgeLabel->setToolTip("端口 8888 已有服务，本窗口会作为客户端连接");
        modeStatusLabel->setText("已检测到本地服务运行中，本窗口将直接连接。");
    } else {
        serviceBadgeLabel->setText("托管本地服务");
        serviceBadgeLabel->setToolTip("本窗口已启动端口 8888，本机其他客户端会自动连接");
        modeStatusLabel->setText("本窗口正在托管本地服务，可再打开一个客户端测试互发消息。");
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
                QMessageBox::information(nullptr, "注册成功", QString("你的 QQ 账号是：%1\n请记住该账号，之后登录和加好友都使用它。").arg(client->currentUserId()));
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
