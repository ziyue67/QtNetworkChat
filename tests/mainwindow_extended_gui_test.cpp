#include "client.h"
#include "clientstorage.h"
#include "localfilemanager.h"
#include "mainwindow.h"
#include "server.h"
#include "test_redis_support.h"
#include "gui_test_support.h"
#include "native_dialog_input.h"
#include "widgets/composerwidget.h"
#include "windows/screenshotcapturewindow.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFileDialog>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <memory>

namespace {
QPushButton* buttonWithText(QDialog& dialog, const QString& text) {
    for (auto* button : dialog.findChildren<QPushButton*>())
        if (button->text() == text && button->isVisible()) return button;
    return nullptr;
}

// Interact with the actual menu, including actions below a scrolling menu's
// initial viewport. Never call the command handler from a test.
bool chooseMenu(MainWindow& window, const QString& command) {
    bool chosen = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) return;
        timer.stop();
        for (auto* action : menu->actions()) {
            if (action->data().toString() != command) continue;
            if (!action->isEnabled()) break;
            menu->setActiveAction(action);
            chosen = true;
            QTest::keyClick(menu, Qt::Key_Return);
            return;
        }
        menu->close();
    });
    timer.start(20);
    QTimer::singleShot(3000, &timer, [&] {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) menu->close();
    });
    if (!QMetaObject::invokeMethod(&window, "onShowCreateMenu", Qt::DirectConnection)) return false;
    return chosen;
}

struct Fixture {
    QTemporaryDir root;
    TestRedisServerEnvironment redis{QStringLiteral("extended-gui")};
    Server server;
    QObject clientOwner;
    Client owner{&clientOwner}, member{&clientOwner};
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<MainWindow> memberWindow;
    bool start() {
        if (!root.isValid()) return false;
        qputenv("QTNETWORKCHAT_APPDATA_DIR", root.filePath("server").toUtf8());
        qputenv("QTNETWORKCHAT_DB_DRIVER", "QSQLITE");
        qputenv("QTNETWORKCHAT_TRANSPORT", "tcp");
        qputenv("QTNETWORKCHAT_TLS", "0");
        qputenv("QTNETWORKCHAT_E2E_ALLOW_PLAINTEXT_PRIVATE_FILE", "1");
        qunsetenv("QTNETWORKCHAT_E2E_REQUIRE_PRODUCTION_CRYPTO");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.filePath("settings"));
        ClientStorage::setAppDataRootDirectory(root.filePath("client"));
        LocalFileManager::setReceivedDownloadRootDirectory(root.filePath("downloads"));
        QString error;
        if (!redis.start(&error)) { qWarning() << error; return false; }
        redis.applyEnvironment();
        if (!server.start(0)) return false;
        auto login = [&](Client& client, const char* id, const char* name) {
            client.setUserInfo({}, name);
            client.setAccountInfo(id, "extended-gui-test-only", true);
            return client.connectToServer("127.0.0.1", server.serverPort()) && client.waitForLoginResult();
        };
        if (!login(owner, "920201", "ExtendedOwner")) return false;
        window = std::make_unique<MainWindow>(&owner, owner.currentUserId(), owner.currentUserName());
        if (!login(member, "920202", "ExtendedMember")) return false;
        memberWindow = std::make_unique<MainWindow>(&member, member.currentUserId(), member.currentUserName());
        window->show();
        memberWindow->show();
        QTest::qWait(100);
        return GuiTestSupport::prepareFont();
    }
};
}

class ExtendedGuiTest : public QObject {
    Q_OBJECT
private slots:
    void nativePickers() {
        if (qgetenv("QTNETWORKCHAT_NATIVE_DIALOG_TEST") != "1")
            QSKIP("Native acceptance is run separately on a visible desktop");
        QVERIFY(!QApplication::testAttribute(Qt::AA_DontUseNativeDialogs));
        QVERIFY(QGuiApplication::platformName() != "offscreen");
        Fixture fixture;
        QVERIFY(fixture.start());
        auto& window = *fixture.window;
        fixture.memberWindow->hide();
        window.raise();
        window.activateWindow();
        const QString filePath = fixture.root.filePath("native-file.bin");
        const QString avatarPath = fixture.root.filePath("native-avatar.png");
        const QString downloadPath = fixture.root.filePath("native-downloads");
        const QString exportPath = fixture.root.filePath("native-history.txt");
        const QByteArray bytes(32768, '\x67');
        QFile file(filePath); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size())); file.close();
        QPixmap avatar(80, 80); avatar.fill(Qt::cyan); QVERIFY(avatar.save(avatarPath));
        QVERIFY(QDir().mkpath(downloadPath));
        qInfo().noquote() << "NATIVE_PATHS file=" << filePath << "avatar=" << avatarPath
                         << "directory=" << downloadPath << "export=" << exportPath;
#ifdef Q_OS_WIN
        NativeDialogInput input(this);
        input.choices = {{}, {filePath}, {avatarPath}, {downloadPath, true}, {exportPath}};
#endif
        QSignalSpy received(&fixture.member, &Client::newMessage);
        qInfo() << "NATIVE_STEP 1: cancel file picker";
        QVERIFY(QMetaObject::invokeMethod(&window, "onSendFile"));
        QCOMPARE(received.count(), 0);
        // Let the platform helper finish destroying its rejected native dialog
        // before opening another picker on the same parent window.
        QTest::qWait(500);
        qInfo() << "NATIVE_STEP 2: choose native-file.bin";
        QVERIFY(QMetaObject::invokeMethod(&window, "onSendFile"));
        const QDir downloads(fixture.root.filePath("downloads/Files"));
        QTRY_COMPARE_WITH_TIMEOUT(downloads.entryList({"*_native-file.bin"}, QDir::Files).size(), 1, 5000);
        QFile result(downloads.filePath(downloads.entryList({"*_native-file.bin"}, QDir::Files).first()));
        QVERIFY(result.open(QIODevice::ReadOnly)); QCOMPARE(result.readAll(), bytes);
        qInfo() << "NATIVE_STEP 3: choose native-avatar.png";
        QVERIFY(QMetaObject::invokeMethod(&window, "onUploadAvatar"));
        QVERIFY(window.statusBar()->currentMessage().contains("头像"));
        qInfo() << "NATIVE_STEP 4: select native-downloads directory";
        QTimer storageTimer;
        bool changed = false;
        connect(&storageTimer, &QTimer::timeout, this, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->windowTitle() != "存储管理" || changed) return;
            changed = true;
            auto* change = buttonWithText(*dialog, "更改存储路径");
            if (change) QTest::mouseClick(change, Qt::LeftButton);
            dialog->accept();
        });
        storageTimer.start(100);
        QVERIFY(QMetaObject::invokeMethod(&window, "onShowStorageManager"));
        storageTimer.stop();
        QVERIFY(changed);
        QCOMPARE(QDir::cleanPath(LocalFileManager::receivedDownloadRootDirectory()), QDir::cleanPath(downloadPath));
        auto* composer = window.findChild<ComposerWidget*>(); QVERIFY(composer);
        composer->setText("原生导出验收消息"); QTest::mouseClick(composer->sendButton(), Qt::LeftButton);
        qInfo() << "NATIVE_STEP 5: export native-history.txt";
        QVERIFY(QMetaObject::invokeMethod(&window, "onExportHistory"));
        QFile history(exportPath); QVERIFY(history.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(history.readAll()).contains("原生导出验收消息"));
#ifdef Q_OS_WIN
        QCOMPARE(input.observed, 5);
#endif
        QVERIFY(GuiTestSupport::captureScreenshot(window,
            QDir(QCoreApplication::applicationDirPath()).filePath("mainwindow_native_gui.png")));
        qInfo() << "NATIVE_ACCEPTANCE PASS: cancellation, upload bytes, avatar, directory setting, history export";
    }
    void menusNotificationsScreenshotAndTransfer() {
        Fixture fixture;
        QVERIFY(fixture.start());
        auto& window = *fixture.window;
        auto* composer = window.findChild<ComposerWidget*>();
        QVERIFY(composer);
        bool popupSeen = false;
        QTimer modalGuard;
        connect(&modalGuard, &QTimer::timeout, this, [&] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                popupSeen = true;
                dialog->reject();
            }
        });
        modalGuard.start(40);
        const QStringList commands{
            "create-group", "open-global-search", "focus-contact-search", "refresh-contacts", "clear-search",
            "copy-chat-id", "copy-chat-card", "copy-current-invite", "copy-current-members", "copy-current-online",
            "copy-all-contacts", "copy-search-summary", "copy-quick-guide", "copy-media-guide",
            "copy-current-media-pack", "copy-full-media-plan", "edit-announcement", "copy-announcement",
            "show-friend-notice", "show-group-notice", "send-image", "send-file", "create-group-with-friends"};
        for (const auto& command : commands) {
            QApplication::clipboard()->clear();
            popupSeen = false;
            QVERIFY2(chooseMenu(window, command), qPrintable(command));
            if (command.startsWith("copy-")) QVERIFY2(!QApplication::clipboard()->text().isEmpty(), qPrintable(command));
            if (QStringList{"create-group", "open-global-search", "edit-announcement", "show-friend-notice",
                            "show-group-notice", "send-image", "send-file"}.contains(command)) QVERIFY2(popupSeen, qPrintable(command));
            qInfo().noquote() << "GUI menu PASS" << command;
        }
        // Local group messages stay local and shortcuts expand before storage.
        QSignalSpy received(&fixture.member, &Client::newMessage);
        composer->setText("/card");
        QTest::mouseClick(composer->sendButton(), Qt::LeftButton);
        QCOMPARE(composer->text(), QString());
        QTest::qWait(100);
        QCOMPARE(received.count(), 0);
        QVERIFY(QMetaObject::invokeMethod(&window, "onBackToGroupChat"));
        composer->setText("/qq");
        QTest::mouseClick(composer->sendButton(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(received.count() > 0, 3000);
        QCOMPARE(qvariant_cast<Message>(received.last().first()).content,
                 QStringLiteral("我的 QQ 号：920201，昵称：ExtendedOwner"));
        // Every top-level menu action is exercised; cancelling logout keeps the
        // fixture alive. Hidden resume actions are checked by protocol tests.
        auto* menuBar = window.findChild<QMenuBar*>();
        QVERIFY(menuBar);
        for (auto* action : menuBar->actions()) {
            if (!action->isVisible() || !action->isEnabled()) continue;
            action->trigger();
            QVERIFY(fixture.owner.isConnected());
            qInfo() << "GUI menu bar PASS" << action->text();
        }
        modalGuard.stop();

        // Both rejection and acceptance must reach the requester through the
        // server; changing only the notice's local model would not pass.
        QSignalSpy friendResponse(&fixture.member, &Client::friendResponseReceived);
        for (bool accepted : {false, true}) {
            QSignalSpy incoming(&fixture.owner, &Client::friendRequestReceived);
            QVERIFY(fixture.member.sendFriendRequest(fixture.owner.currentUserId()));
            QTRY_VERIFY_WITH_TIMEOUT(incoming.count() > 0, 3000);
            bool clicked = false;
            QTimer actionTimer;
            connect(&actionTimer, &QTimer::timeout, this, [&] {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!dialog || dialog->objectName() != "noticeDialog") return;
                auto* list = dialog->findChild<QListWidget*>("noticeList");
                auto* decision = buttonWithText(*dialog, accepted ? "同意" : "拒绝");
                if (!list || !decision || !decision->isEnabled()) { dialog->reject(); return; }
                list->setCurrentRow(0);
                QTest::mouseClick(decision, Qt::LeftButton);
                clicked = true;
                dialog->accept();
            });
            actionTimer.start(20);
            QVERIFY(QMetaObject::invokeMethod(&window, "onShowFriendNotifications"));
            QVERIFY(clicked);
            QTRY_VERIFY_WITH_TIMEOUT(friendResponse.count() > 0, 3000);
            QCOMPARE(friendResponse.takeFirst().at(2).toBool(), accepted);
        }
        qInfo() << "GUI friend approve/reject PASS";

        QVERIFY(fixture.owner.createPrivateServerGroup("GUI审批群"));
        QString groupId;
        auto findGroup = [&] {
            for (const auto& value : fixture.owner.serverGroups()) {
                const auto group = value.toObject();
                if (group.value("groupName").toString() == "GUI审批群") groupId = group.value("groupId").toString();
            }
            return !groupId.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(findGroup(), 3000);
        for (bool accepted : {false, true}) {
            QSignalSpy incoming(&fixture.owner, &Client::serverGroupJoinApplicationReceived);
            QSignalSpy response(&fixture.member, &Client::serverGroupJoinRequestStatusReceived);
            QVERIFY(fixture.member.requestServerGroupJoin(groupId, "GUI验收申请"));
            QTRY_VERIFY_WITH_TIMEOUT(incoming.count() > 0, 3000);
            bool clicked = false;
            QTimer timer;
            connect(&timer, &QTimer::timeout, this, [&] {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!dialog || dialog->objectName() != "noticeDialog") return;
                auto* list = dialog->findChild<QListWidget*>("noticeList");
                if (!list) { dialog->reject(); return; }
                for (int row = 0; row < list->count(); ++row) {
                    if (list->item(row)->data(Qt::UserRole).toString().startsWith("join_request:")) {
                        list->setCurrentRow(row); break;
                    }
                }
                auto* decision = buttonWithText(*dialog, accepted ? "同意" : "拒绝");
                if (!decision || !decision->isEnabled()) { dialog->reject(); return; }
                QTest::mouseClick(decision, Qt::LeftButton);
                clicked = true;
                dialog->accept();
            });
            timer.start(20);
            QVERIFY(QMetaObject::invokeMethod(&window, "onShowGroupNotifications"));
            QVERIFY(clicked);
            auto decided = [&] {
                for (const auto& args : response) {
                    const auto status = args.first().toJsonObject().value("state").toString();
                    if (status == (accepted ? "approved" : "rejected")) return true;
                }
                return false;
            };
            QTRY_VERIFY_WITH_TIMEOUT(decided(), 3000);
        }
        qInfo() << "GUI group approve/reject PASS";

        ScreenshotCaptureWindow capture;
        QPixmap image(640, 480); image.fill(Qt::green);
        capture.setScreenshot(image);
        capture.setCaptureGeometry(QRect(0, 0, 640, 480));
        QSignalSpy saved(&capture, &ScreenshotCaptureWindow::saveRequested);
        QSignalSpy canceled(&capture, &ScreenshotCaptureWindow::cancelRequested);
        capture.show();
        QTest::mousePress(&capture, Qt::LeftButton, {}, QPoint(30, 40));
        QTest::mouseMove(&capture, QPoint(180, 140));
        QTest::mouseRelease(&capture, Qt::LeftButton, {}, QPoint(180, 140));
        QTest::keyClick(&capture, Qt::Key_Return);
        QCOMPARE(saved.count(), 1);
        QCOMPARE(qvariant_cast<QPixmap>(saved.first().first()).size(), QSize(151, 101));
        capture.show();
        QTest::keyClick(&capture, Qt::Key_Escape);
        QCOMPARE(canceled.count(), 1);
        qInfo() << "GUI screenshot select/confirm/cancel PASS";

        // A real progress-dialog cancel interrupts a multi-chunk upload. Then
        // pick the same file again and prove complete receiver bytes.
        const QString path = fixture.root.filePath("gui-cancel-retry.bin");
        const QByteArray bytes(2 * 1024 * 1024, '\x39');
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size())); file.close();
        auto selectFile = [&](bool& selected) {
            auto* timer = new QTimer(&window);
            connect(timer, &QTimer::timeout, &window, [&, timer] {
                auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                if (!picker) return;
                timer->stop();
                picker->selectFile(path);
                QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
                selected = true;
                timer->deleteLater();
            });
            timer->start(20);
        };
        bool cancelClicked = false;
        auto cancelConnection = connect(&fixture.owner, &Client::fileTransferProgress, this,
            [&](const QString& name, qint64 prepared, qint64) {
                if (name != "gui-cancel-retry.bin" || prepared == 0 || cancelClicked) return;
                for (auto* progress : window.findChildren<QProgressDialog*>()) {
                    auto* cancel = buttonWithText(*progress, "取消发送");
                    if (cancel) { cancelClicked = true; QTest::mouseClick(cancel, Qt::LeftButton); }
                }
            });
        bool selected = false;
        selectFile(selected);
        QVERIFY(QMetaObject::invokeMethod(&window, "onSendFile"));
        disconnect(cancelConnection);
        QVERIFY(selected && cancelClicked);
        const QDir download(fixture.root.filePath("downloads/Files"));
        QCOMPARE(download.entryList({"*_gui-cancel-retry.bin"}, QDir::Files).size(), 0);
        selected = false; selectFile(selected);
        QVERIFY(QMetaObject::invokeMethod(&window, "onSendFile"));
        QVERIFY(selected);
        QTRY_COMPARE_WITH_TIMEOUT(download.entryList({"*_gui-cancel-retry.bin"}, QDir::Files).size(), 1, 5000);
        QFile result(download.filePath(download.entryList({"*_gui-cancel-retry.bin"}, QDir::Files).first()));
        QVERIFY(result.open(QIODevice::ReadOnly)); QCOMPARE(result.readAll(), bytes);
        qInfo() << "GUI transfer cancel/resend PASS";
        QVERIFY(GuiTestSupport::captureScreenshot(window,
            QDir(QCoreApplication::applicationDirPath()).filePath("mainwindow_extended_gui.png")));
    }
};

int main(int argc, char** argv) {
    if (qgetenv("QTNETWORKCHAT_NATIVE_DIALOG_TEST") != "1")
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("mainwindow_extended_gui_test");
    QStandardPaths::setTestModeEnabled(true);
    ExtendedGuiTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "mainwindow_extended_gui_test.moc"
