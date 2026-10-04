#include "client.h"
#include "clientstorage.h"
#include "localfilemanager.h"
#include "mainwindow.h"
#include "server.h"
#include "test_redis_support.h"
#include "gui_test_support.h"
#include "theme/thememanager.h"
#include "widgets/composerwidget.h"
#include "windows/loginwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
#include <QTimer>
#include <functional>

namespace {
QJsonObject publicGroup(const Client& client) {
    for (const auto& value : client.serverGroups()) {
        if (value.toObject().value("groupId").toString() == "public") return value.toObject();
    }
    return {};
}

QPushButton* settingRow(QDialog& dialog, const QString& title) {
    for (auto* button : dialog.findChildren<QPushButton*>()) {
        auto* label = button->findChild<QLabel*>(QStringLiteral("groupInfoRowTitle"));
        if (label && label->text() == title) return button;
    }
    return nullptr;
}

bool hasChatText(MainWindow& window, const QString& text) {
    auto* view = window.findChild<QListView*>(QStringLiteral("chatListView"));
    if (!view || !view->model()) return false;
    for (int row = 0; row < view->model()->rowCount(); ++row) {
        if (view->model()->index(row, 0).data().toString().contains(text)) return true;
    }
    return false;
}

// Drive the real modal dialogs. Polling is bounded, and failed actions close the
// dialog so a regression reports a failure instead of hanging an unattended CI.
void inDialog(QWidget& context, const QString& name, bool& ok,
              const std::function<bool(QDialog&)>& action) {
    auto* timer = new QTimer(&context);
    auto* elapsed = new QElapsedTimer;
    elapsed->start();
    QObject::connect(timer, &QObject::destroyed, [elapsed] { delete elapsed; });
    QObject::connect(timer, &QTimer::timeout, &context, [timer, elapsed, name, action, &ok] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog && (name.isEmpty() || dialog->objectName() == name)) {
            timer->stop();
            if (!action(*dialog)) {
                ok = false;
                dialog->reject();
            } else {
                // Queuing accept() only proves that the slot exists. A picker
                // can reject the selected path and stay open on older Qt.
                QTimer::singleShot(5000, dialog, [dialog, name, &ok] {
                    if (dialog->isVisible()) {
                        ok = false;
                        qWarning() << "Dialog action did not close the dialog:" << name;
                        dialog->reject();
                    }
                });
            }
            timer->deleteLater();
        } else if (elapsed->elapsed() > 5000) {
            ok = false;
            timer->stop();
            timer->deleteLater();
            qWarning() << "Expected dialog did not appear:" << name;
            if (dialog) dialog->reject();
        }
    });
    timer->start(20);
}

bool loginFromForm(Client& client, quint16 port, const QString& account, const QString& password) {
    LoginWindow login;
    login.show();
    for (auto* input : login.findChildren<QLineEdit*>()) {
        if (input->placeholderText() == QStringLiteral("QQ 号 / 账号")) input->setText(account);
        if (input->placeholderText() == QStringLiteral("密码")) input->setText(password);
    }
    auto* submit = login.findChild<QPushButton*>(QStringLiteral("loginPrimaryBtn"));
    if (!submit || !submit->isEnabled()) return false;
    QTest::mouseClick(submit, Qt::LeftButton);
    if (login.result() != QDialog::Accepted) return false;
    client.setAccountInfo(login.account(), login.password(), false);
    client.setLoginMode(login.loginMode());
    return client.connectToServer(QStringLiteral("127.0.0.1"), port)
        && client.waitForLoginResult();
}
}

class MainWindowWorkflowTest : public QObject {
    Q_OBJECT
private slots:
    void loginSettingsMessageFileAndReconnect() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        qputenv("QTNETWORKCHAT_APPDATA_DIR", temporary.filePath("server").toUtf8());
        qputenv("QTNETWORKCHAT_TRANSPORT", "tcp");
        qputenv("QTNETWORKCHAT_TLS", "0");
        qputenv("QTNETWORKCHAT_DB_DRIVER", "QSQLITE");
        qunsetenv("QTNETWORKCHAT_E2E_REQUIRE_PRODUCTION_CRYPTO");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.filePath("settings"));
        QSettings storageSettings(QSettings::IniFormat, QSettings::UserScope, "QtNetworkChat", "QtNetworkChat");
        QVERIFY(QDir::fromNativeSeparators(storageSettings.fileName()).startsWith(temporary.path() + QLatin1Char('/')));
        ClientStorage::setAppDataRootDirectory(temporary.filePath("client"));
        LocalFileManager::setReceivedDownloadRootDirectory(temporary.filePath("downloads"));
        QCOMPARE(ClientStorage::appDataRootDirectory(), temporary.filePath("client"));
        QCOMPARE(LocalFileManager::receivedDownloadRootDirectory(), temporary.filePath("downloads"));
        storageSettings.sync();
        QCOMPARE(storageSettings.value("storage/appDataRoot").toString(), temporary.filePath("client"));
        QCOMPARE(storageSettings.value("storage/receivedDownloadRoot").toString(), temporary.filePath("downloads"));
        QVERIFY(GuiTestSupport::prepareFont());
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
        QTest::failOnWarning(QRegularExpression(QStringLiteral("QString::arg:.*")));
#endif

        TestRedisServerEnvironment redis(QStringLiteral("mainwindow-workflow"));
        QString redisError;
        QVERIFY2(redis.start(&redisError), qPrintable(redisError));
        redis.applyEnvironment();
        QTcpServer portProbe;
        QVERIFY(portProbe.listen(QHostAddress::LocalHost, 0));
        const quint16 port = portProbe.serverPort();
        portProbe.close();
        Server server;
        QVERIFY(server.start(port));
        qInfo() << "GUI workflow: fixture ready";

        // Create disposable accounts through the normal wire protocol; this does
        // not seed the database behind the server or contact the public service.
        Client owner(qApp), member(qApp);
        owner.setUserInfo({}, QStringLiteral("GuiOwner"));
        owner.setAccountInfo(QStringLiteral("920101"), QStringLiteral("test-password"), true);
        QVERIFY(owner.connectToServer(QStringLiteral("127.0.0.1"), port));
        QVERIFY(owner.waitForLoginResult());
        owner.disconnectFromServer();
        QTRY_VERIFY_WITH_TIMEOUT(!owner.isConnected(), 3000);
        QVERIFY(loginFromForm(owner, port, QStringLiteral("920101"), QStringLiteral("test-password")));
        member.setUserInfo({}, QStringLiteral("GuiMember"));
        member.setAccountInfo(QStringLiteral("920102"), QStringLiteral("test-password"), true);
        QVERIFY(member.connectToServer(QStringLiteral("127.0.0.1"), port));
        QVERIFY(member.waitForLoginResult());
        QTRY_COMPARE_WITH_TIMEOUT(publicGroup(owner).value("memberCount").toInt(), 2, 5000);
        qInfo() << "GUI workflow: accounts logged in";

        MainWindow ownerWindow(&owner, owner.currentUserId(), owner.currentUserName());
        MainWindow memberWindow(&member, member.currentUserId(), member.currentUserName());
        ownerWindow.show();
        memberWindow.show();
        bool dialogsOk = true;
        QString unexpectedDialog;
        QTimer messageBoxGuard;
        connect(&messageBoxGuard, &QTimer::timeout, this, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                unexpectedDialog = box->text();
                box->reject();
            }
        });
        messageBoxGuard.start(50);

        auto* more = ownerWindow.findChild<QPushButton*>(QStringLiteral("chatGroupMoreBtn"));
        QVERIFY(more && more->isVisible() && more->isEnabled());
        const QString changedName = QStringLiteral("GUI 已保存的群名");
        inDialog(ownerWindow, QStringLiteral("groupInfoDialog"), dialogsOk, [&](QDialog& panel) {
            auto* profile = settingRow(panel, QStringLiteral("群资料设置"));
            auto* speaking = settingRow(panel, QStringLiteral("发言限制"));
            if (!profile || !speaking) return false;
            inDialog(panel, QStringLiteral("groupProfileEditor"), dialogsOk, [&](QDialog& editor) {
                auto* name = editor.findChild<QLineEdit*>(QStringLiteral("groupProfileNameInput"));
                auto* save = editor.findChild<QPushButton*>(QStringLiteral("dialogPrimaryBtn"));
                if (!name || !save) return false;
                name->setText(changedName);
                QTest::mouseClick(save, Qt::LeftButton);
                return true;
            });
            QTest::mouseClick(profile, Qt::LeftButton);
            inDialog(panel, QStringLiteral("groupSettingChooser"), dialogsOk, [&](QDialog& chooser) {
                for (auto* option : chooser.findChildren<QPushButton*>()) {
                    if (option->property("settingValue").toString() == "per_minute_5") {
                        QTest::mouseClick(option, Qt::LeftButton);
                        return true;
                    }
                }
                return false;
            });
            QTest::mouseClick(speaking, Qt::LeftButton);
            const bool captured = GuiTestSupport::captureScreenshot(panel,
                QDir(QCoreApplication::applicationDirPath()).filePath("mainwindow_workflow_settings.png"));
            panel.accept();
            return captured;
        });
        QTest::mouseClick(more, Qt::LeftButton);
        QVERIFY(dialogsOk);
        QTRY_COMPARE_WITH_TIMEOUT(publicGroup(owner).value("groupName").toString(), changedName, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(publicGroup(member).value("speakingRule").toString(), QStringLiteral("per_minute_5"), 5000);

        // A member sees the same saved group, but cannot enter manager editors.
        auto* memberMore = memberWindow.findChild<QPushButton*>(QStringLiteral("chatGroupMoreBtn"));
        QVERIFY(memberMore && memberMore->isVisible());
        inDialog(memberWindow, QStringLiteral("groupInfoDialog"), dialogsOk, [&](QDialog& panel) {
            const bool allowed = !settingRow(panel, QStringLiteral("群资料设置"))
                && !settingRow(panel, QStringLiteral("发言限制"));
            panel.reject();
            return allowed;
        });
        QTest::mouseClick(memberMore, Qt::LeftButton);
        QVERIFY(dialogsOk);

        // Cancel an edit through its actual button and confirm the saved value.
        inDialog(ownerWindow, QStringLiteral("groupInfoDialog"), dialogsOk, [&](QDialog& panel) {
            auto* profile = settingRow(panel, QStringLiteral("群资料设置"));
            if (!profile) return false;
            inDialog(panel, QStringLiteral("groupProfileEditor"), dialogsOk, [&](QDialog& editor) {
                auto* name = editor.findChild<QLineEdit*>(QStringLiteral("groupProfileNameInput"));
                auto* cancel = editor.findChild<QPushButton*>(QStringLiteral("dialogSecondaryBtn"));
                if (!name || !cancel || name->text() != changedName) return false;
                name->setText(QStringLiteral("取消后不应写入的群名"));
                QTest::mouseClick(cancel, Qt::LeftButton);
                return true;
            });
            QTest::mouseClick(profile, Qt::LeftButton);
            panel.accept();
            return true;
        });
        QTest::mouseClick(more, Qt::LeftButton);
        QVERIFY(dialogsOk);

        // Assert persistence independently of the optimistic labels and snapshots.
        const QString connection = QStringLiteral("gui-workflow-persistence");
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(temporary.filePath("server/accounts.sqlite3"));
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("SELECT group_name, speaking_rule FROM server_groups WHERE group_id = 'public'"));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), changedName);
            QCOMPARE(query.value(1).toString(), QStringLiteral("per_minute_5"));
        }
        QSqlDatabase::removeDatabase(connection);
        qInfo() << "GUI workflow: group settings persisted and permissions checked";

        auto* composer = ownerWindow.findChild<ComposerWidget*>();
        QVERIFY(composer);
        const QString text = QStringLiteral("GUI 消息经过真实 TCP 服务端");
        QVERIFY(!hasChatText(memberWindow, text));
        composer->setText(text);
        QVERIFY(composer->sendButton()->isEnabled());
        QSignalSpy messages(&member, &Client::newMessage);
        QTest::mouseClick(composer->sendButton(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() > 0, 5000);
        QVERIFY(hasChatText(memberWindow, text));
        QCOMPARE(composer->text(), QString());
        QVERIFY(messages.count() > 0);

        // Open the actual file picker and exercise multi-chunk sending, receiving,
        // automatic save, and the receiver's file card, checking exact bytes.
        const QString filePath = temporary.filePath("gui-multi-chunk.bin");
        const QByteArray payload(600 * 1024, '\x5a');
        QFile input(filePath);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(payload), qint64(payload.size()));
        input.close();
        auto* fileButton = static_cast<QPushButton*>(nullptr);
        for (auto* button : composer->findChildren<QPushButton*>()) {
            if (button->toolTip() == QStringLiteral("发送文件")) fileButton = button;
        }
        QVERIFY(fileButton && fileButton->isEnabled());
        qInfo() << "GUI workflow: text received; testing file dialogs";
        const int messageCountBeforeCancel = messages.count();
        inDialog(ownerWindow, {}, dialogsOk, [&](QDialog& dialog) {
            if (!qobject_cast<QFileDialog*>(&dialog)) return false;
            dialog.reject();
            return true;
        });
        QTest::mouseClick(fileButton, Qt::LeftButton);
        QVERIFY(dialogsOk);
        QCOMPARE(messages.count(), messageCountBeforeCancel);
        inDialog(ownerWindow, {}, dialogsOk, [&](QDialog& dialog) {
            auto* picker = qobject_cast<QFileDialog*>(&dialog);
            if (!picker) return false;
            picker->setDirectory(QFileInfo(filePath).absolutePath());
            auto* nameInput = picker->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
            auto* buttons = picker->findChild<QDialogButtonBox*>();
            auto* open = buttons ? buttons->button(QDialogButtonBox::Open) : nullptr;
            if (!nameInput || !open) return false;
            nameInput->setFocus();
            nameInput->selectAll();
            QTest::keyClicks(nameInput, QFileInfo(filePath).fileName());
            if (!open->isEnabled()) return false;
            QTest::mouseClick(open, Qt::LeftButton);
            return picker->result() == QDialog::Accepted;
        });
        QTest::mouseClick(fileButton, Qt::LeftButton);
        QVERIFY(dialogsOk);
        QVERIFY2(unexpectedDialog.isEmpty(), qPrintable(unexpectedDialog));
        qInfo() << "GUI workflow: selected file sent";
        const QDir receivedDirectory(temporary.filePath("downloads/Files"));
        const QStringList receivedFilter{QStringLiteral("*_gui-multi-chunk.bin")};
        QTRY_COMPARE_WITH_TIMEOUT(receivedDirectory.entryList(receivedFilter, QDir::Files).size(), 1, 5000);
        const QString receivedPath = receivedDirectory.filePath(receivedDirectory.entryList(receivedFilter, QDir::Files).first());
        QFile received(receivedPath);
        QVERIFY(received.open(QIODevice::ReadOnly));
        QCOMPARE(received.readAll(), payload);
        qInfo() << "GUI workflow: received file bytes verified";
        QVERIFY(hasChatText(memberWindow, QStringLiteral("gui-multi-chunk.bin")));
        QVERIFY(GuiTestSupport::captureScreenshot(memberWindow,
            QDir(QCoreApplication::applicationDirPath()).filePath("mainwindow_workflow_received.png")));
        ThemeManager::instance()->setTheme(ThemeManager::Theme::Dark);
        QVERIFY(GuiTestSupport::captureScreenshot(ownerWindow,
            QDir(QCoreApplication::applicationDirPath()).filePath("mainwindow_workflow_dark.png")));

        // Disconnect leaves an unsent draft intact; reconnect restores saved
        // settings from the server and allows the same draft to be sent.
        owner.disconnectFromServer();
        QTRY_VERIFY_WITH_TIMEOUT(!owner.isConnected(), 3000);
        composer->setText(QStringLiteral("重连后发送的草稿"));
        QVERIFY(!composer->sendButton()->isEnabled());
        QCOMPARE(composer->text(), QStringLiteral("重连后发送的草稿"));
        QVERIFY(owner.connectToServer(QStringLiteral("127.0.0.1"), port));
        QVERIFY(owner.waitForLoginResult());
        QTRY_COMPARE_WITH_TIMEOUT(publicGroup(owner).value("groupName").toString(), changedName, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(composer->sendButton()->isEnabled(), 3000);
        QTest::mouseClick(composer->sendButton(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(hasChatText(memberWindow, QStringLiteral("重连后发送的草稿")), 5000);
        QVERIFY2(unexpectedDialog.isEmpty(), qPrintable(unexpectedDialog));
        qInfo() << "GUI workflow: reconnect and draft delivery passed";
    }
};

int main(int argc, char** argv) {
    qputenv("QTNETWORKCHAT_TRANSPORT", "tcp");
    qputenv("QTNETWORKCHAT_TLS", "0");
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("QtNetworkChatTests"));
    app.setApplicationName(QStringLiteral("mainwindow_workflow_test"));
    QStandardPaths::setTestModeEnabled(true);
    MainWindowWorkflowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "mainwindow_workflow_test.moc"
