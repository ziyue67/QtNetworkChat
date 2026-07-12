#include <QApplication>
#include <QStyleFactory>
#include "logincredentialstore.h"
#include "mainwindow.h"
#include "server.h"
#include "client.h"
#include "windows/loginwindow.h"
#include "theme/thememanager.h"
#include <QDialog>
#include <QTextStream>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QFrame>
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

bool sendRedisCommand(const QString& host, quint16 port, const QList<QByteArray>& arguments,
                      QByteArray* response = nullptr, int timeoutMs = 500) {
    QTcpSocket socket;
    socket.connectToHost(host, port);
    if (!socket.waitForConnected(timeoutMs)) return false;
    QByteArray request = "*" + QByteArray::number(arguments.size()) + "\r\n";
    for (const QByteArray& argument : arguments) {
        request += "$" + QByteArray::number(argument.size()) + "\r\n" + argument + "\r\n";
    }
    if (socket.write(request) != request.size() || !socket.waitForBytesWritten(timeoutMs)) return false;
    if (!socket.waitForReadyRead(timeoutMs)) return false;
    const QByteArray reply = socket.readAll();
    if (response) *response = reply;
    return !reply.startsWith('-');
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

    // Local desktop Redis installations commonly enter MISCONF after an RDB
    // snapshot failure and then reject every write, which prevents the embedded
    // chat server from starting. Keep the local service writable; account data
    // itself remains persisted in SQLite.
    sendRedisCommand(host, port,
                     {QByteArrayLiteral("CONFIG"), QByteArrayLiteral("SET"),
                      QByteArrayLiteral("stop-writes-on-bgsave-error"), QByteArrayLiteral("no")});

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

namespace {

// Load the QQNT theme stylesheet application-wide so the pre-login dialogs
// (mode/login/register) share the same light/dark tokens as MainWindow. The
// stylesheet ships next to the executable under ui/ (see CMake POST_BUILD).
void applyQqntThemeStyleSheet(QApplication& app) {
    ThemeManager* tm = ThemeManager::instance();
    const QString styleName = tm->isDark()
        ? QStringLiteral("style-qqnt-dark.qss")
        : QStringLiteral("style-qqnt.qss");
    const QString fileName = QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("ui/") + styleName);
    QFile styleFile(fileName);
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream textStream(&styleFile);
        app.setStyleSheet(textStream.readAll());
        styleFile.close();
    }
}

}

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(qqntUnhandledExceptionFilter);
#endif

    QApplication a(argc, argv);
    // Login/register dialogs temporarily leave no visible top-level window while
    // MainWindow is being constructed. Do not let Qt terminate during that gap.
    a.setQuitOnLastWindowClosed(false);
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
        client->setAccountInfo("debug123", "123456", false);
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
    QObject::connect(modeDialog, &QDialog::rejected, &a, &QCoreApplication::quit);
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
            background: transparent;
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
            background: transparent;
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
            background: transparent;
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

    // Shared launcher for both the "登录" and "注册账号" entry points. LoginWindow
    // is a frameless, DialogTitleBar-driven dialog that toggles between login and
    // register modes internally, so the two mode-dialog buttons only differ by the
    // initial mode they open it in.
    auto launchLogin = [&](bool startInRegisterMode) {
        LoginWindow* loginDlg = new LoginWindow(modeDialog);
        loginDlg->setRegisterMode(startInRegisterMode);
        modeDialog->hide();

        if (loginDlg->exec() != QDialog::Accepted) {
            loginDlg->deleteLater();
            modeDialog->show();
            return;
        }

        host = QStringLiteral("127.0.0.1");
        port = 8888;
        userName = loginDlg->userName();
        const bool wasRegisterMode = loginDlg->registerMode();
        const QString account = loginDlg->account();
        const QString password = loginDlg->password();
        const bool remember = loginDlg->rememberPassword();

        // The workspace contains several historical executables. Never submit
        // credentials unless this process owns a healthy local service or a
        // service is verifiably listening on the expected port.
        if (!canConnectToLocalChatService(port, 500)) {
            if (!server) {
                server = new Server(&a);
            }
            if (!server->start(port) && !canConnectToLocalChatService(port, 500)) {
                QMessageBox::critical(
                    nullptr,
                    QStringLiteral("本地服务未启动"),
                    QStringLiteral("无法启动本机聊天服务。\n程序：%1\n数据库：%2")
                        .arg(QCoreApplication::applicationFilePath(),
                             QDir(appDataDir()).filePath(QStringLiteral("accounts.sqlite3"))));
                delete client;
                client = nullptr;
                loginDlg->deleteLater();
                modeDialog->show();
                return;
            }
        }

        client = new Client;
        client->setUserInfo("", userName);
        client->setAccountInfo(account, password, wasRegisterMode);
        client->setLoginMode(loginDlg->loginMode());
        client->connectToServer(host, port);
        if (!client->isConnected() || !client->waitForLoginResult()) {
            const QString reason = client->lastLoginError().isEmpty()
                ? QStringLiteral("无法连接本地测试服务")
                : client->lastLoginError();
            QMessageBox::critical(
                nullptr,
                wasRegisterMode ? QStringLiteral("注册失败") : QStringLiteral("登录失败"),
                QStringLiteral("%1\n\n账号：%2\n程序：%3\n数据库：%4")
                    .arg(reason,
                         account,
                         QCoreApplication::applicationFilePath(),
                         QDir(appDataDir()).filePath(QStringLiteral("accounts.sqlite3"))));
            delete client;
            client = nullptr;
            loginDlg->deleteLater();
            modeDialog->show();
            return;
        }

        if (client->currentLoginWasRegister()) {
            loginDlg->saveResolvedLoginToSqlite(client->currentUserId(),
                                                client->currentUserName(),
                                                true);
            QMessageBox::information(nullptr,
                                     QStringLiteral("注册成功"),
                                     QStringLiteral("你的 QQ 账号是：%1\n账号已保存到本地 SQLite 登录库，之后登录和加好友都使用它。")
                                         .arg(client->currentUserId()));
        } else {
            loginDlg->saveResolvedLoginToSqlite(account, client->currentUserName(), remember);
        }

        MainWindow* w = new MainWindow(client, client->currentUserId(), client->currentUserName());
        w->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(w, &MainWindow::logoutRequested, [&, w]() {
            w->close();
            delete client;
            client = nullptr;
            modeDialog->show();
        });
        QObject::connect(w, &QObject::destroyed, [&]() {
            client = nullptr;
        });
        w->show();
        w->raise();
        w->activateWindow();
        loginDlg->deleteLater();
    };

    QObject::connect(serverBtn, &QPushButton::clicked, [&]() { launchLogin(true); });
    QObject::connect(clientBtn, &QPushButton::clicked, [&]() { launchLogin(false); });

    modeDialog->show();
    return a.exec();
}
