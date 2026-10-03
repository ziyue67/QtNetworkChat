#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "chatbubbledelegate.h"
#include "qqnt_backend_service.h"
#include "qqnt_log.h"
#include "dialogs/addfrienddialog.h"
#include "dialogs/essencepanel.h"
#include "dialogs/friendmanagerdialog.h"
#include "dialogs/globalsearchdialog.h"
#include "qtnetworkchat_version.h"
#include "theme/dialogstyle.h"
#include "theme/thememanager.h"
#include "views/contactsview.h"
#include "views/favoritesview.h"
#include "views/messagesview.h"
#include "views/profileview.h"
#include "views/settingsview.h"
#include "widgets/appnav.h"
#include "widgets/avatarlabel.h"
#include "widgets/composerwidget.h"
#include "widgets/dialogtitlebar.h"
#include "widgets/titlebar.h"
#include "windowstatemanager.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleHints>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
void MainWindow::setupQQNT()
{
    qtnetworkchat::logDebug("MainWindow", "setupQQNT start");
    m_qqntRoot = new QWidget(this);
    m_qqntRoot->setObjectName(QStringLiteral("qqntRoot"));
    m_qqntRoot->setAttribute(Qt::WA_StyledBackground, true);
    m_qqntRoot->setAutoFillBackground(true);

    QWidget* legacyCentralWidget = takeCentralWidget();
    if (legacyCentralWidget && legacyCentralWidget != m_qqntRoot) {
        // The generated Ui object still owns pointers into this compatibility
        // surface. Keep it attached to MainWindow instead of moving it under
        // the new central widget: reparenting it made Qt's generated Ui
        // ownership and QMainWindow central-widget destruction disagree.
        legacyCentralWidget->setParent(this);
        legacyCentralWidget->hide();
    }
    setCentralWidget(m_qqntRoot);
    m_qqntRoot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    qtnetworkchat::logDebug("MainWindow", "centralWidget replaced");

    // Hide legacy menu bar and status bar for QQNT style
    if (QMenuBar* mb = menuBar()) { mb->hide(); }
    if (QStatusBar* sb = statusBar()) { sb->hide(); }

    QVBoxLayout* rootLayout = new QVBoxLayout(m_qqntRoot);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->setSizeConstraint(QLayout::SetNoConstraint);

    m_titleBar = new TitleBar(m_qqntRoot);
    rootLayout->addWidget(m_titleBar);
    qtnetworkchat::logDebug("MainWindow", "titleBar created");

    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->setSizeConstraint(QLayout::SetNoConstraint);

    m_appNav = new AppNav(m_qqntRoot);
    contentLayout->addWidget(m_appNav);
    qtnetworkchat::logDebug("MainWindow", "appNav created");

    m_viewStack = new QStackedWidget(m_qqntRoot);
    m_viewStack->setObjectName(QStringLiteral("qqntViewStack"));
    m_viewStack->setAttribute(Qt::WA_StyledBackground, true);
    qtnetworkchat::logDebug("MainWindow", "viewStack created");

    m_messagesView = new MessagesView(m_qqntRoot);
    qtnetworkchat::logDebug("MainWindow", "messagesView created");
    m_messagesView->setSessionModel(m_sessionModel);
    m_messagesView->setChatModel(m_chatModel);
    qtnetworkchat::logDebug("MainWindow", "messagesView models set");
    m_contactsView = new ContactsView(m_qqntRoot);
    m_favoritesView = new FavoritesView(m_qqntRoot);
    m_settingsView = new SettingsView(m_qqntRoot);
    m_profileView = new ProfileView(m_qqntRoot);

    m_viewStack->addWidget(m_messagesView);
    m_viewStack->addWidget(m_contactsView);
    m_viewStack->addWidget(m_favoritesView);
    m_viewStack->addWidget(m_settingsView);
    m_viewStack->addWidget(m_profileView);

    contentLayout->addWidget(m_viewStack, 1);
    rootLayout->addLayout(contentLayout, 1);

    QTimer::singleShot(0, this, [this, legacyCentralWidget]() {
        qtnetworkchat::logDebug("MainWindow", QStringLiteral("geometry window=%1x%2 root=%3x%4 stack=%5x%6 legacy=%7x%8 visible=%9")
                .arg(width()).arg(height())
                .arg(m_qqntRoot ? m_qqntRoot->width() : -1).arg(m_qqntRoot ? m_qqntRoot->height() : -1)
                .arg(m_viewStack ? m_viewStack->width() : -1).arg(m_viewStack ? m_viewStack->height() : -1)
                .arg(legacyCentralWidget ? legacyCentralWidget->width() : -1).arg(legacyCentralWidget ? legacyCentralWidget->height() : -1)
                .arg(legacyCentralWidget && legacyCentralWidget->isVisible() ? QStringLiteral("true") : QStringLiteral("false")));
    });

    connect(m_titleBar, &TitleBar::minimizeRequested, this, &QMainWindow::showMinimized);
    connect(m_titleBar, &TitleBar::maximizeRequested, this, [this]() {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
    });
    connect(m_titleBar, &TitleBar::closeRequested, this, &QMainWindow::close);

    connect(m_appNav, &AppNav::routeActivated, this, &MainWindow::onAppNavRouteActivated);
    connect(m_messagesView, &MessagesView::sendRequested, this, &MainWindow::onSendMessage);
    connect(m_messagesView, &MessagesView::fileRequested, this, &MainWindow::onSendFile);
    connect(m_messagesView, &MessagesView::imageRequested, this, &MainWindow::onSendImage);
    connect(m_messagesView, &MessagesView::emojiRequested, this, &MainWindow::onInsertEmoji);
    connect(m_messagesView, &MessagesView::mentionRequested, this, &MainWindow::onInsertMention);
    connect(m_messagesView->composer(), &ComposerWidget::screenshotRequested, this, &MainWindow::onCaptureScreenshot);
    connect(m_messagesView->composer(), &ComposerWidget::filesDropped, this, &MainWindow::onComposerFilesDropped);
    connect(m_messagesView->composer(), &ComposerWidget::textChanged, this, &MainWindow::refreshComposerState);
    connect(m_messagesView, &MessagesView::viewHistoryRequested, this, &MainWindow::onViewHistory);
    connect(m_messagesView, &MessagesView::filterHistoryByDateRequested, this, &MainWindow::onFilterHistoryByDate);
    connect(m_messagesView, &MessagesView::exportHistoryRequested, this, &MainWindow::onExportHistory);
    connect(m_messagesView, &MessagesView::clearHistoryRequested, this, &MainWindow::onClearHistory);
    connect(m_messagesView, &MessagesView::sessionSelected, this, &MainWindow::onPrivateChat);
    connect(m_messagesView, &MessagesView::filesDropped, this, &MainWindow::onComposerFilesDropped);
    connect(m_messagesView, &MessagesView::messageActionRequested, this, &MainWindow::onMessageActionRequested);
    connect(m_messagesView, &MessagesView::avatarActionRequested, this, &MainWindow::onAvatarActionRequested);
    connect(m_messagesView, &MessagesView::mediaActivated, this, &MainWindow::onMediaActivated);
    connect(m_messagesView, &MessagesView::multiSelectForwardRequested, this, &MainWindow::onMultiSelectForwardRequested);
    connect(m_messagesView, &MessagesView::multiSelectDeleteRequested, this, &MainWindow::onMultiSelectDeleteRequested);
    connect(m_messagesView, &MessagesView::multiSelectFavoriteRequested, this, &MainWindow::onMultiSelectFavoriteRequested);
    connect(m_messagesView, &MessagesView::essenceRequested, this, &MainWindow::showEssencePanel);
    connect(m_messagesView, &MessagesView::groupMoreRequested, this, &MainWindow::showGroupInfoPanel);
    connectGroupMemberSidebar();
    connect(m_contactsView, &ContactsView::friendSelected, this, [this](const QString& userId) {
        openPrivateSession(userId);
        showMessagesView();
    });
    connect(m_contactsView, &ContactsView::groupSelected, this, [this](const QString& groupId) {
        switchToLocalGroup(groupId, m_localGroupNames.value(groupId, QStringLiteral("群聊")));
        showMessagesView();
    });
    connect(m_contactsView, &ContactsView::addFriendRequested, this, [this]() {
        showGlobalSearchDialog();
    });
    connect(m_contactsView, &ContactsView::createGroupRequested, this, [this]() {
        handleCreateMenuCommand(QStringLiteral("create-group"));
    });
    connect(m_contactsView, &ContactsView::friendManagerRequested, this, [this]() {
        FriendManagerDialog dlg(this);
        QMap<QString, QString> avatarPaths;
        for (const QString& id : m_friendIds) {
            const QString path = peerAvatarPath(id);
            if (!path.isEmpty()) avatarPaths.insert(id, path);
        }
        dlg.setFriendList(m_friendIds, m_friendNames, m_friendGroups, m_customGroups, avatarPaths);
        connect(&dlg, &FriendManagerDialog::deleteFriendRequested, this, [this](const QString& userId) {
            // TODO: no delete-friend command exists in Client/protocol yet; the dialog
            // only removes the row locally. Surface this so the state isn't misleading.
            ui->statusbar->showMessage(
                QStringLiteral("暂不支持删除好友（后端未实现该协议），仅本地移除 QQ:%1").arg(userId), 3000);
        });
        // Friend groups are local-only; persist changes so they survive restart.
        connect(&dlg, &FriendManagerDialog::createGroupRequested, this, [this](const QString& name) {
            if (!name.isEmpty() && !m_customGroups.contains(name)) {
                m_customGroups.append(name);
                saveFriendGroups();
            }
        });
        connect(&dlg, &FriendManagerDialog::renameGroupRequested, this,
                [this](const QString& oldName, const QString& newName) {
            const int index = m_customGroups.indexOf(oldName);
            if (index >= 0) m_customGroups[index] = newName;
            for (auto it = m_friendGroups.begin(); it != m_friendGroups.end(); ++it) {
                if (it.value() == oldName) it.value() = newName;
            }
            saveFriendGroups();
        });
        connect(&dlg, &FriendManagerDialog::deleteGroupRequested, this, [this](const QString& name) {
            m_customGroups.removeAll(name);
            for (auto it = m_friendGroups.begin(); it != m_friendGroups.end(); ) {
                if (it.value() == name) it = m_friendGroups.erase(it);
                else ++it;
            }
            saveFriendGroups();
        });
        connect(&dlg, &FriendManagerDialog::moveFriendToGroupRequested, this,
                [this](const QString& userId, const QString& group) {
            if (userId.isEmpty()) return;
            if (group.isEmpty() || group == QStringLiteral("我的好友")) {
                m_friendGroups.remove(userId);
            } else {
                m_friendGroups[userId] = group;
            }
            saveFriendGroups();
        });
        dlg.exec();
    });
    connect(m_contactsView, &ContactsView::globalSearchRequested, this, [this]() {
        showGlobalSearchDialog();
    });
    if (m_contactsView->searchEdit()) {
        connect(m_contactsView->searchEdit(), &QLineEdit::textChanged, this, [this]() {
            refreshContactsAndProfile();
        });
    }
    connect(m_favoritesView, &FavoritesView::favoriteSelected, this,
            [this](const QString& sessionId, const QString& messageId) {
        onFavoriteSelected(sessionId, messageId);
    });
    connect(m_favoritesView, &FavoritesView::favoriteRemovalRequested, this, [this](const QJsonObject& message) {
        if (message.isEmpty()) return;
        QJsonObject payload;
        payload[QStringLiteral("message")] = message;
        payload[QStringLiteral("favorite")] = false;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        if (!QQNTBackendService::handle(QStringLiteral("toggle_local_message_favorite"),
                                        payload, &response, &errorCode, &errorMessage)) {
            ui->statusbar->showMessage(QStringLiteral("取消收藏失败: %1").arg(errorMessage), 3000);
            return;
        }
        if (m_client && m_client->isConnected()) {
            m_client->sendMessageFavoriteUpdate(message.value(QStringLiteral("sessionId")).toString(),
                                                message.value(QStringLiteral("messageId")).toString(message.value(QStringLiteral("id")).toString()),
                                                false,
                                                message);
        }
        refreshFavoritesView();
        ui->statusbar->showMessage(QStringLiteral("已取消收藏"), 1800);
    });
    connect(m_settingsView, &SettingsView::themeModeChanged, this, &MainWindow::onSettingsThemeModeChanged);
    connect(m_settingsView, &SettingsView::notificationsToggled, this, &MainWindow::onSettingsNotificationsToggled);
    connect(m_settingsView, &SettingsView::soundToggled, this, &MainWindow::onSettingsSoundToggled);
    connect(m_settingsView, &SettingsView::desktopNotificationsToggled, this, &MainWindow::onSettingsDesktopNotificationsToggled);
    connect(m_settingsView, &SettingsView::muteInSessionToggled, this, &MainWindow::onSettingsMuteInSessionToggled);
    connect(m_settingsView, &SettingsView::e2eEnabledToggled, this, &MainWindow::onSettingsE2EEnabledToggled);
    connect(m_settingsView, &SettingsView::autoAcceptFilesToggled, this, &MainWindow::onSettingsAutoAcceptFilesToggled);
    connect(m_settingsView, &SettingsView::openFolderAfterDownloadToggled, this, &MainWindow::onSettingsOpenFolderAfterDownloadToggled);
    connect(m_settingsView, &SettingsView::hideWindowBeforeScreenshotToggled, this, &MainWindow::onSettingsHideWindowBeforeScreenshotToggled);
    connect(m_settingsView, &SettingsView::downloadPathChangeRequested, this, &MainWindow::onSettingsDownloadPathChangeRequested);
    connect(m_settingsView, &SettingsView::screenshotShortcutChangeRequested, this, &MainWindow::onSettingsScreenshotShortcutChangeRequested);
    connect(m_settingsView, &SettingsView::logoutRequested, this, &MainWindow::onLogout);
    connect(m_profileView, &ProfileView::logoutRequested, this, &MainWindow::onLogout);
    connect(m_profileView, &ProfileView::editProfileRequested, this, [this]() {
        bool ok = false;
        const QString newName = QInputDialog::getText(this,
                                                      QStringLiteral("修改昵称"),
                                                      QStringLiteral("请输入新的昵称:"),
                                                      QLineEdit::Normal,
                                                      m_currentUserName,
                                                      &ok).trimmed();
        if (!ok || newName.isEmpty() || newName == m_currentUserName) {
            return;
        }
        m_currentUserName = newName;
        if (m_client) {
            m_client->setUserInfo(m_currentUserId, newName);
        }
        saveProfileToSqlite();
        setWindowTitle(WindowStateManager::appWindowTitle(
            QString::fromLatin1(QTNETWORKCHAT_VERSION_STRING), m_currentUserName));
        if (m_titleBar) {
            m_titleBar->setUserName(m_currentUserName);
        }
        m_profileView->setUserInfo(m_currentUserId, m_currentUserName);
        if (m_settingsView) {
            m_settingsView->setAccountInfo(m_currentUserName, m_currentUserId);
        }
        refreshFriendList();
        ui->statusbar->showMessage(QStringLiteral("昵称已更新为 %1").arg(newName), 2200);
    });
    connect(m_profileView, &ProfileView::changeAvatarRequested, this, [this]() {
        onUploadAvatar();
    });

    m_titleBar->setUserName(m_currentUserName);
    m_titleBar->setUserId(m_currentUserId);
    m_profileView->setUserInfo(m_currentUserId, m_currentUserName);

    // Restore persisted settings state into the SettingsView so its controls
    // reflect the values applied elsewhere (theme, notifications, files, screenshot).
    {
        QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
        const int themeMode = settings.value(QStringLiteral("appearance/themeMode"),
                                              ThemeManager::instance()->isDark() ? 1 : 0).toInt();
        m_settingsView->setThemeMode(themeMode);
        m_settingsView->setAccountInfo(m_currentUserName, m_currentUserId);
        m_settingsView->setSyncStatus(QStringLiteral("已保存到本地"));
    }

    m_appNav->setCurrentIndex(0);
    m_viewStack->setCurrentIndex(0);

    m_messagesView->setChatTitle(ui->chatTitleLabel->text(),
                                 ui->chatSubtitleLabel->text(),
                                 ui->chatHintLabel->text());

    updateStyleSheet();

    // Startup assertions: the QQNT shell must be fully constructed. These guard
    // against regressions where a core surface fails to initialize.
    Q_ASSERT(m_qqntRoot);
    Q_ASSERT(m_messagesView);
    Q_ASSERT(m_viewStack);
    Q_ASSERT(m_appNav);
    if (!m_qqntRoot || !m_messagesView || !m_viewStack || !m_appNav) {
        qtnetworkchat::logDebug("MainWindow", "FATAL: QQNT shell missing a core widget after setup");
    }
    // Legacy central widget must be hidden so only the QQNT UI is visible.
    if (QWidget* legacy = centralWidget(); legacy && legacy != m_qqntRoot) {
        qtnetworkchat::logDebug("MainWindow", QStringLiteral("WARN: unexpected central widget %1")
                .arg(legacy->objectName()));
    }
    qtnetworkchat::logDebug("MainWindow", QStringLiteral("setup complete · route=%1 session=%2 sessionRows=%3 chatRows=%4")
            .arg(QString::number(m_viewStack->currentIndex()),
                 m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget,
                 QString::number(m_sessionModel ? m_sessionModel->rowCount() : -1),
                 QString::number(m_chatModel ? m_chatModel->rowCount() : -1)));
}

void MainWindow::updateStyleSheet()
{
    if (!m_qqntRoot) {
        return;
    }

    ThemeManager* tm = ThemeManager::instance();
    QString style = QStringLiteral(
        "QWidget#qqntRoot { background-color: %1; border: none; }"
        "QStackedWidget#qqntViewStack { background-color: %1; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 6px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: %2; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->borderColor().name());

    m_qqntRoot->setStyleSheet(style);

    QPalette windowPalette = palette();
    windowPalette.setColor(QPalette::Window, tm->backgroundColor());
    setPalette(windowPalette);
    setAutoFillBackground(true);
    m_qqntRoot->setPalette(windowPalette);
    m_qqntRoot->update();
}

void MainWindow::loadStyleSheet()
{
    ThemeManager* tm = ThemeManager::instance();
    const QString styleName = tm->isDark()
        ? QStringLiteral("style-qqnt-dark.qss")
        : QStringLiteral("style-qqnt.qss");
    const QString fileName = QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("ui/") + styleName);
    QFile styleFile(fileName);
    if (styleFile.open(QFile::ReadOnly)) {
        QTextStream textStream(&styleFile);
        setStyleSheet(textStream.readAll());
        styleFile.close();
        qtnetworkchat::logDebug("MainWindow", QStringLiteral("stylesheet loaded: %1").arg(fileName));
    } else {
        qtnetworkchat::logDebug("MainWindow", QStringLiteral("stylesheet load failed: %1").arg(fileName));
    }
}

void MainWindow::onThemeToggled()
{
    ThemeManager::instance()->toggleTheme();
    loadStyleSheet();
    updateStyleSheet();
    if (m_settingsView) {
        m_settingsView->setThemeMode(ThemeManager::instance()->isDark() ? 1 : 0);
    }
}

void MainWindow::onSettingsThemeModeChanged(int mode)
{
    // 0=light, 1=dark, 2=system. Persist choice and apply light/dark now.
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("appearance/themeMode"), mode);

    ThemeManager::Theme target = ThemeManager::Theme::Light;
    if (mode == 1) {
        target = ThemeManager::Theme::Dark;
    } else if (mode == 2) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        // Read the OS color scheme. The application palette may already have
        // been overridden by ThemeManager, so it is not a reliable system signal.
        target = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
            ? ThemeManager::Theme::Dark
            : ThemeManager::Theme::Light;
#else
        const QColor windowColor = QGuiApplication::palette().color(QPalette::Window);
        target = windowColor.lightness() < 128
            ? ThemeManager::Theme::Dark
            : ThemeManager::Theme::Light;
#endif
    }
    ThemeManager::instance()->setTheme(target);
    loadStyleSheet();
    updateStyleSheet();
    if (m_settingsView) {
        m_settingsView->setSyncStatus(QStringLiteral("已保存到本地"));
    }
}

void MainWindow::onSettingsNotificationsToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("notifications/enabled"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("消息通知已开启") : QStringLiteral("消息通知已关闭"));
    }
    ui->statusbar->showMessage(enabled ? QStringLiteral("已开启消息通知") : QStringLiteral("已关闭消息通知"), 1800);
}

void MainWindow::onSettingsSoundToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("notifications/sound"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("提示音已开启") : QStringLiteral("提示音已关闭"));
    }
}

void MainWindow::onSettingsDesktopNotificationsToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("notifications/desktop"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("桌面通知已开启") : QStringLiteral("桌面通知已关闭"));
    }
}

void MainWindow::onSettingsMuteInSessionToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("notifications/muteInSession"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("会话内已免打扰") : QStringLiteral("会话内通知已恢复"));
    }
}

void MainWindow::onSettingsE2EEnabledToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("security/e2eEnabled"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("端到端加密已开启") : QStringLiteral("端到端加密已关闭"));
    }
    ui->statusbar->showMessage(enabled ? QStringLiteral("端到端加密已开启（重连后生效）")
                                       : QStringLiteral("端到端加密已关闭（重连后生效）"), 2200);
}

void MainWindow::onSettingsAutoAcceptFilesToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("files/autoAccept"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("已开启自动接收文件") : QStringLiteral("已关闭自动接收文件"));
    }
}

void MainWindow::onSettingsOpenFolderAfterDownloadToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("files/openFolderAfterDownload"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("下载后将自动打开文件夹") : QStringLiteral("下载后不再打开文件夹"));
    }
}

void MainWindow::onSettingsHideWindowBeforeScreenshotToggled(bool enabled)
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    settings.setValue(QStringLiteral("screenshot/hideWindow"), enabled);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(enabled ? QStringLiteral("截图时将隐藏当前窗口") : QStringLiteral("截图时保留当前窗口"));
    }
}

void MainWindow::onSettingsDownloadPathChangeRequested()
{
    const QString current = LocalFileManager::receivedDownloadRootDirectory();
    const QString picked = QFileDialog::getExistingDirectory(this,
                                                             QStringLiteral("选择默认下载目录"),
                                                             current);
    if (picked.trimmed().isEmpty()) {
        return;
    }
    LocalFileManager::setReceivedDownloadRootDirectory(picked);
    const QString saved = LocalFileManager::receivedDownloadRootDirectory();
    if (m_settingsView) {
        m_settingsView->setSyncStatus(QStringLiteral("下载目录已更新"));
    }
    ui->statusbar->showMessage(QStringLiteral("默认下载目录：%1").arg(saved), 2600);
}

void MainWindow::onSettingsScreenshotShortcutChangeRequested()
{
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    const QString current = settings.value(QStringLiteral("screenshot/shortcut"),
                                            QStringLiteral("Ctrl+Alt+A")).toString();
    bool ok = false;
    const QString entered = QInputDialog::getText(this,
                                                  QStringLiteral("修改截图快捷键"),
                                                  QStringLiteral("请输入快捷键（例如 Ctrl+Alt+A）："),
                                                  QLineEdit::Normal,
                                                  current,
                                                  &ok);
    if (!ok) {
        return;
    }
    const QKeySequence seq(entered.trimmed());
    if (seq.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无效快捷键"),
                             QStringLiteral("无法识别输入的快捷键，请重试。"));
        return;
    }
    const QString normalized = seq.toString(QKeySequence::NativeText);
    settings.setValue(QStringLiteral("screenshot/shortcut"), normalized);
    ui->statusbar->showMessage(QStringLiteral("截图快捷键已更新为 %1（重启后生效）").arg(normalized), 2600);
    if (m_settingsView) {
        m_settingsView->setSyncStatus(QStringLiteral("截图快捷键已更新"));
    }
}

void MainWindow::showMessagesView()
{
    // Bring the messages view forward and keep the side nav highlight in sync.
    // Used when a session is opened from another view (e.g. the contacts card).
    if (m_viewStack) {
        m_viewStack->setCurrentIndex(0);
    }
    if (m_appNav) {
        m_appNav->setCurrentIndex(0);
    }
}

void MainWindow::showAddFriendDialog()
{
    // QQNT add-friend flow: search a QQ/nickname, review the result card, then
    // confirm. Search results arrive asynchronously via onFriendSearchResult,
    // which routes to this dialog while m_activeAddFriendDialog is set.
    AddFriendDialog dialog(this);
    m_activeAddFriendDialog = &dialog;

    connect(&dialog, &AddFriendDialog::searchRequested, this, [this](const QString& text) {
        const QString account = text.trimmed();
        if (account.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("请输入 QQ 号或昵称后再搜索"), 1800);
            return;
        }
        if (!m_client->searchFriendByAccount(account)) {
            ui->statusbar->showMessage(QStringLiteral("当前未连接，无法搜索账号"), 2500);
            if (m_activeAddFriendDialog) {
                m_activeAddFriendDialog->setRequestOutcome(false, QStringLiteral("当前未连接，请恢复连接后重试"));
            }
        }
    });
    connect(&dialog, &AddFriendDialog::addFriendRequested, this, [this, &dialog](const QString& userId) {
        if (userId.isEmpty() || userId == m_currentUserId) {
            dialog.setRequestOutcome(false, QStringLiteral("不能添加自己为好友"));
            return;
        }
        if (m_friendIds.contains(userId)) {
            ui->statusbar->showMessage(QStringLiteral("QQ 账号 %1 已经是你的好友").arg(userId), 2500);
            dialog.setRequestOutcome(false, QStringLiteral("该用户已经是你的好友"));
            return;
        }
        if (m_pendingOutgoingFriendRequests.contains(userId)) {
            dialog.setRequestOutcome(true, QStringLiteral("好友申请已发送，等待对方确认"));
            return;
        }
        if (!m_client->sendFriendRequest(userId)) {
            ui->statusbar->showMessage(QStringLiteral("好友申请发送失败，请检查连接后重试"), 3000);
            dialog.setRequestOutcome(false, QStringLiteral("好友申请发送失败，请检查连接后重试"));
            return;
        }
        if (!m_pendingOutgoingFriendRequests.contains(userId)) {
            m_pendingOutgoingFriendRequests << userId;
        }
        saveFriends();
        appendSystemMessage(QStringLiteral("已发送好友申请 QQ:%1，等待对方同意").arg(userId));
        ui->statusbar->showMessage(
            QStringLiteral("好友申请已发送给 %1").arg(contactDisplayName(userId)), 2500);
        dialog.setRequestOutcome(true);
        refreshFriendList();
    });

    dialog.exec();
    m_activeAddFriendDialog = nullptr;
}

void MainWindow::showGlobalSearchDialog(bool contactGroupMode)
{
    // QQNT global search combines locally joined groups with server-side stranger
    // group discovery. A stranger group can only be entered after server approval.
    GlobalSearchDialog dialog(this);
    dialog.setContactGroupMode(contactGroupMode);
    // Every global-search variant receives async QQ account results. Previously
    // only contactGroupMode did so, leaving the standard search blank.
    m_activeContactGroupSearchDialog = &dialog;

    auto populate = [this, &dialog](const QString& keyword) {
        dialog.clearResults();
        const QString q = keyword.trimmed();
        const auto matches = [&q](const QString& id, const QString& name) {
            if (q.isEmpty()) return true;
            return id.contains(q, Qt::CaseInsensitive) || name.contains(q, Qt::CaseInsensitive);
        };
        for (const QString& id : m_friendIds) {
            const QString name = contactDisplayName(id);
            if (matches(id, name)) {
                dialog.addResult(QStringLiteral("contact"), id, name,
                                 QStringLiteral("QQ:%1").arg(id), peerAvatarPath(id));
                dialog.setContactKnown(id, true);
            }
        }
        for (const QString& id : m_localGroupIds) {
            const QString name = m_localGroupNames.value(id, QStringLiteral("群聊"));
            if (matches(id, name)) {
                const int count = m_localGroupMembers.value(id).size();
                dialog.addResult(QStringLiteral("group"), id, name,
                                 QStringLiteral("%1 人").arg(count), m_localGroupAvatarPaths.value(id));
                dialog.setGroupJoined(id, true);
            }
        }
        for (const QString& id : m_serverGroupNames.keys()) {
            if (id == QStringLiteral("public") || m_localGroupIds.contains(id)) continue;
            const QString name = m_serverGroupNames.value(id, QStringLiteral("群聊"));
            if (matches(id, name)) {
                const int count = m_serverGroupMembers.value(id).size();
                dialog.addResult(QStringLiteral("group"), id, name, QStringLiteral("%1 人").arg(count));
                dialog.setGroupJoined(id, m_joinedServerSearchGroups.contains(id));
            }
        }
    };

    // Prime the list with everything, then refilter on each search.
    populate(QString());
    connect(&dialog, &GlobalSearchDialog::searchRequested, this,
            [this, populate](const QString& text) {
        populate(text);
        if (!text.trimmed().isEmpty() && m_client) {
            if (!m_client->searchFriendByAccount(text.trimmed()) && m_activeContactGroupSearchDialog) {
                m_activeContactGroupSearchDialog->setSearching(false);
                m_activeContactGroupSearchDialog->showSearchState(QStringLiteral("当前未连接，搜索不可用"));
            }
            m_client->searchServerGroups(text.trimmed());
        } else if (m_activeContactGroupSearchDialog) {
            m_activeContactGroupSearchDialog->setSearching(false);
            m_activeContactGroupSearchDialog->showSearchState(QStringLiteral("当前未连接，搜索不可用"));
        }
    });
    connect(&dialog, &GlobalSearchDialog::addFriendRequested, this, [this, &dialog](const QString& userId) {
        if (userId.isEmpty() || userId == m_currentUserId) {
            ui->statusbar->showMessage(QStringLiteral("不能添加当前账号"), 2200);
            return;
        }
        if (m_friendIds.contains(userId)) {
            dialog.setContactKnown(userId, true);
            ui->statusbar->showMessage(QStringLiteral("该账号已经是你的好友"), 2200);
            return;
        }
        if (m_client && m_client->sendFriendRequest(userId)) {
            if (!m_pendingOutgoingFriendRequests.contains(userId)) m_pendingOutgoingFriendRequests << userId;
            saveFriends();
            ui->statusbar->showMessage(QStringLiteral("好友申请已发送"), 2200);
        } else {
            ui->statusbar->showMessage(QStringLiteral("好友申请发送失败，请检查连接"), 2600);
        }
    });
    connect(&dialog, &GlobalSearchDialog::joinGroupRequested, this,
            [this, &dialog](const QString& serverGroupId) {
        if (serverGroupId.isEmpty()) {
            return;
        }
        if (m_joinedServerSearchGroups.contains(serverGroupId)) {
            dialog.setGroupJoined(serverGroupId, true);
            return;
        }
        if (!m_client || !m_client->requestServerGroupJoin(serverGroupId)) {
            ui->statusbar->showMessage(QStringLiteral("入群申请发送失败，请检查连接"), 2800);
            return;
        }
        dialog.setGroupJoinPending(serverGroupId, true);
    });
    connect(&dialog, &GlobalSearchDialog::resultActivated, this,
            [this, &dialog](const QString& type, const QString& id) {
        if (type == QStringLiteral("group")) {
            const QString localId = m_joinedServerSearchGroups.value(id, id);
            const QString name = m_localGroupNames.value(localId, m_serverGroupNames.value(id, QStringLiteral("群聊")));
            switchToLocalGroup(localId, name);
        } else {
            openPrivateSession(id);
        }
        showMessagesView();
        dialog.accept();
    });

    dialog.exec();
    m_activeContactGroupSearchDialog = nullptr;
}

void MainWindow::showEssencePanel()
{
    // QQNT essence view: enumerate the current chat's essence-flagged messages
    // (marked via ChatBubbleForwardedRole by the essence/unessence command) and
    // present them in a modal panel. The panel shares Favorites' locate flow
    // and supports removing an essence mark directly from its context menu.
    EssencePanel panel(this);
    connect(&panel, &EssencePanel::messageActivated, this, [this, &panel](const QString& messageId) {
        if (messageId.isEmpty() || !m_chatModel || !m_messagesView) return;
        for (int row = 0; row < m_chatModel->rowCount(); ++row) {
            QStandardItem* item = m_chatModel->item(row);
            const QString itemId = item ? item->data(ChatBubbleMessageIdRole).toString() : QString();
            if (!item || (itemId != messageId && QString::number(row) != messageId)) continue;
            const QModelIndex target = m_chatModel->index(row, 0);
            if (QListView* view = m_messagesView->chatListView()) {
                view->scrollTo(target, QAbstractItemView::PositionAtCenter);
                view->setCurrentIndex(target);
            }
            panel.accept();
            ui->statusbar->showMessage(QStringLiteral("已定位精华消息"), 1800);
            return;
        }
        ui->statusbar->showMessage(QStringLiteral("未在当前聊天记录中找到该精华消息"), 2200);
    });
    connect(&panel, &EssencePanel::messageRemovalRequested, this, [this, &panel](const QString& messageId) {
        if (messageId.isEmpty() || !m_chatModel) return;
        for (int row = 0; row < m_chatModel->rowCount(); ++row) {
            QStandardItem* item = m_chatModel->item(row);
            const QString itemId = item ? item->data(ChatBubbleMessageIdRole).toString() : QString();
            if (!item || (itemId != messageId && QString::number(row) != messageId)) continue;
            const QModelIndex target = m_chatModel->index(row, 0);
            if (handleBackendContextCommand(QStringLiteral("unessence"), item->text(), target)) {
                panel.accept();
                QTimer::singleShot(0, this, &MainWindow::showEssencePanel);
            }
            return;
        }
        ui->statusbar->showMessage(QStringLiteral("取消精华失败：未找到原消息"), 2200);
    });
    const int rowCount = m_chatModel ? m_chatModel->rowCount() : 0;
    int essenceCount = 0;
    for (int row = 0; row < rowCount; ++row) {
        QStandardItem* item = m_chatModel->item(row);
        if (!item || !item->data(ChatBubbleForwardedRole).toBool()) {
            continue;
        }
        const QString text = item->data(Qt::DisplayRole).toString();
        if (text.isEmpty()) {
            continue;
        }
        const QString sender = item->data(ChatBubbleSenderNameRole).toString();
        const QString timestamp = item->data(ChatBubbleTimestampRole).toString();
        const QString messageId = item->data(ChatBubbleMessageIdRole).toString();
        panel.addEssenceMessage(messageId.isEmpty() ? QString::number(row) : messageId,
                                sender.isEmpty() ? QStringLiteral("未知") : sender,
                                text,
                                timestamp.isEmpty() ? QStringLiteral("--:--") : timestamp);
        ++essenceCount;
    }
    if (essenceCount == 0) {
        ui->statusbar->showMessage(QStringLiteral("当前会话暂无精华消息"), 2200);
    }
    panel.exec();
}

void MainWindow::onAppNavRouteActivated(const QString& route)
{
    if (route == QStringLiteral("messages")) {
        m_viewStack->setCurrentIndex(0);
    } else if (route == QStringLiteral("contacts")) {
        m_viewStack->setCurrentIndex(1);
    } else if (route == QStringLiteral("favorites")) {
        refreshFavoritesView();
        m_viewStack->setCurrentIndex(2);
    } else if (route == QStringLiteral("settings")) {
        m_viewStack->setCurrentIndex(3);
    } else if (route == QStringLiteral("profile")) {
        m_viewStack->setCurrentIndex(4);
    } else {
        // mock routes keep current view for now
        return;
    }
    qtnetworkchat::logDebug("MainWindow", QStringLiteral("route=%1 index=%2 session=%3")
            .arg(route,
                 QString::number(m_viewStack->currentIndex()),
                 m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget));
}
