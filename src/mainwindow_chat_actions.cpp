#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "sessionitemdelegate.h"
#include "sessionlistbuilder.h"
#include "chatbubbledelegate.h"
#include "qqnt_log.h"
#include "qqnt_backend_service.h"
#include "dialogs/friendmanagerdialog.h"
#include "dialogs/creategroupdialog.h"
#include "dialogs/addfrienddialog.h"
#include "dialogs/globalsearchdialog.h"
#include "dialogs/memberprofilecard.h"
#include "dialogs/groupnicknamedialog.h"
#include "dialogs/essencepanel.h"
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QApplication>
#include <QDateTime>
#include <QDebug>

#include "chatsessionmanager.h"
#include "chatcontextmanager.h"
#include "composermanager.h"
#include "filetransferstatus.h"
#include "localfilemanager.h"
#include "notificationpanelmanager.h"
#include "qtnetworkchat_version.h"
#include "transferchatitemrenderer.h"
#include "windowstatemanager.h"
#include "views/messagesview.h"
#include "views/contactsview.h"
#include "widgets/contactlistwidget.h"
#include "views/favoritesview.h"
#include "views/settingsview.h"
#include "views/profileview.h"
#include "widgets/appnav.h"
#include "widgets/titlebar.h"
#include "widgets/composerwidget.h"
#include "widgets/groupmembersidebar.h"
#include "widgets/avatarlabel.h"
#include "widgets/dialogtitlebar.h"
#include "theme/thememanager.h"
#include "theme/dialogstyle.h"
#include "windows/screenshotcapturewindow.h"
#include "screenshotgeometry.h"
#include "windows/imagepreviewwindow.h"
#include "windows/forwardwindow.h"
#include "dialogs/mutedurationdialog.h"
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QMenu>
#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QListView>
#include <QStatusBar>
#include <QPixmap>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QLinearGradient>
#include <QPolygonF>
#include <QRegularExpression>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QClipboard>
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QTabWidget>
#include <QShortcut>
#include <QUrl>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QProgressDialog>
#include <QCheckBox>
#include <QButtonGroup>
#include <QRadioButton>
#include <QComboBox>
#include <QFrame>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QCryptographicHash>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QBuffer>
#include <QScrollArea>
#include <QStackedWidget>
#include <QScreen>
#include <QStyleHints>
#include <QPalette>
#include <QWindow>
#include <functional>
#include <algorithm>

#include "mainwindow_support.h"

using namespace MainWindowSupport;

void MainWindow::onInsertEmoji() {
    QMenu menu(this);
    // Categorized emoji panel: 10 categories rendered as submenus, each laid out as
    // a compact emoji grid. Mirrors the tauri-qqnt EmojiPicker categories.
    struct EmojiCategory {
        QString title;
        QStringList emojis;
    };
    static const QList<EmojiCategory> categories = {
        {QStringLiteral("最近"), {"😀", "😂", "👍", "❤️", "🎉", "🔥", "🙏", "👏"}},
        {QStringLiteral("超级"), {"🤩", "🥳", "😻", "💯", "✨", "⭐", "🌟", "💫"}},
        {QStringLiteral("小黄脸"), {"😀", "😁", "😂", "🤣", "😊", "😇", "🙂", "😉", "😍", "😘", "😜", "🤔", "😐", "😴", "😭", "😡"}},
        {QStringLiteral("手势"), {"👍", "👎", "👌", "✌️", "🤞", "👏", "🙌", "🙏", "💪", "🤝", "👋", "✋"}},
        {QStringLiteral("爱心"), {"❤️", "🧡", "💛", "💚", "💙", "💜", "🖤", "🤍", "💔", "💕", "💞", "💗"}},
        {QStringLiteral("动物"), {"🐶", "🐱", "🐭", "🐹", "🐰", "🦊", "🐻", "🐼", "🐨", "🐯", "🦁", "🐮"}},
        {QStringLiteral("自然"), {"🌸", "🌼", "🌻", "🌹", "🌈", "☀️", "🌙", "⭐", "❄️", "🍀", "🌿", "🌊"}},
        {QStringLiteral("食物"), {"🍎", "🍌", "🍉", "🍇", "🍓", "🍔", "🍟", "🍕", "🍰", "🍦", "☕", "🍺"}},
        {QStringLiteral("物品"), {"📌", "📎", "📷", "🎁", "💡", "🔔", "📱", "💻", "⏰", "🔑", "📚", "✏️"}},
        {QStringLiteral("符号"), {"✅", "❌", "❓", "❗", "💤", "💢", "💦", "💨", "🎵", "🔞", "♻️", "✔️"}},
    };
    for (const EmojiCategory& category : categories) {
        QMenu* categoryMenu = menu.addMenu(category.title);
        for (const QString& emoji : category.emojis) {
            QAction* action = categoryMenu->addAction(emoji);
            connect(action, &QAction::triggered, this, [this, emoji]() {
                insertChatDraftText(emoji, QString("已插入表情 %1").arg(emoji), 1400);
            });
        }
    }
    menu.addSeparator();
    QMenu* commandMenu = menu.addMenu("QQ快捷指令");
    const QList<ChatContextComposerMenuAction> composerActions = ChatContextManager::composerMenuActions();
    for (const ChatContextComposerMenuAction& spec : composerActions) {
        QAction* action = commandMenu->addAction(spec.title);
        action->setToolTip(spec.toolTip);
        action->setStatusTip(spec.toolTip);
        connect(action, &QAction::triggered, this, [this, commandId = spec.commandId]() {
            applyChatContextComposerCommand(commandId);
        });
    }

    const QList<ChatContextPhraseMenuPlan> phrasePlans = ChatContextManager::composerPhraseMenuPlans();
    for (const ChatContextPhraseMenuPlan& plan : phrasePlans) {
        QMenu* phraseMenu = menu.addMenu(plan.title);
        for (const QString& phrase : plan.phrases) {
            QAction* action = phraseMenu->addAction(phrase);
            connect(action, &QAction::triggered, this, [this, phrase, plan]() {
                setChatDraftText(phrase, plan.insertedStatusMessage, 1400);
            });
        }
    }
    const QPoint position = m_messagesView
        ? m_messagesView->composer()->mapToGlobal(QPoint(0, -menu.sizeHint().height()))
        : ui->emojiBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height()));
    menu.exec(position);
}

void MainWindow::onInsertMention() {
    QMenu menu(this);
    QStringList mentionIds;
    QMap<QString, QString> mentionNames;
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        mentionIds = m_localGroupMembers.value(m_privateChatTarget);
    } else {
        for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
            mentionIds << it.key();
            mentionNames[it.key()] = it.value().name;
        }
    }

    for (const QString& memberId : mentionIds) {
        if (!mentionNames.contains(memberId)) {
            mentionNames[memberId] = memberId == m_currentUserId ? m_currentUserName : contactDisplayName(memberId);
        }
    }

    const ComposerMentionMenuPlan mentionPlan =
        ComposerManager::mentionMenuPlan(mentionIds, m_currentUserId, mentionNames);
    for (int i = 0; i < mentionPlan.actions.size(); ++i) {
        if (i == 1 && mentionPlan.separatorAfterAll) {
            menu.addSeparator();
        }
        const ComposerMentionAction actionPlan = mentionPlan.actions.at(i);
        QAction* action = menu.addAction(actionPlan.title);
        connect(action, &QAction::triggered, this, [this, actionPlan]() {
            insertChatDraftText(actionPlan.insertText, actionPlan.statusMessage, 1400);
        });
    }
    const QPoint position = m_messagesView
        ? m_messagesView->composer()->mapToGlobal(QPoint(36, -menu.sizeHint().height()))
        : ui->mentionBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height()));
    menu.exec(position);
}

void MainWindow::onMessageActionRequested(const QModelIndex& index, const QString& action) {
    if (!index.isValid()) return;

    const QString text = index.data(Qt::DisplayRole).toString();
    const QString senderName = index.data(ChatBubbleSenderNameRole).toString();

    if (action == QStringLiteral("copy")) {
        QClipboard* clipboard = QGuiApplication::clipboard();
        if (clipboard) clipboard->setText(text);
    } else if (action == QStringLiteral("quote")) {
        if (m_messagesView) {
            m_messagesView->composer()->insertText(
                QStringLiteral("[%1]: %2\n").arg(senderName, text));
            m_messagesView->composer()->inputEdit()->setFocus();
        }
    } else if (action == QStringLiteral("multiSelect")) {
        if (m_messagesView) {
            m_messagesView->setMultiSelectMode(true);
        }
        const QString sessionId = m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget;
        const QString senderId = index.data(ChatBubbleSenderIdRole).toString();
        const QString senderName = index.data(ChatBubbleSenderNameRole).toString();
        const QString msgId = QStringLiteral("msg-%1-%2").arg(index.row()).arg(
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz")));
        QJsonObject messageObject;
        messageObject[QStringLiteral("id")] = msgId;
        messageObject[QStringLiteral("messageId")] = msgId;
        messageObject[QStringLiteral("senderId")] = senderId;
        messageObject[QStringLiteral("senderName")] = senderName;
        messageObject[QStringLiteral("content")] = text;
        messageObject[QStringLiteral("sessionId")] = sessionId;
        messageObject[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
        messageObject[QStringLiteral("selected")] = true;
        QJsonObject payload;
        payload[QStringLiteral("message")] = messageObject;
        payload[QStringLiteral("selected")] = true;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        QQNTBackendService::handle(QStringLiteral("toggle_multi_select_local_message"), payload, &response, &errorCode, &errorMessage);
    } else if (action == QStringLiteral("delete") ||
               action == QStringLiteral("favorite") || action == QStringLiteral("essence") || action == QStringLiteral("unessence") ||
               action == QStringLiteral("recall")) {
        handleBackendContextCommand(action, text, index);
    } else if (action == QStringLiteral("forward")) {
        runForwardForMessage(index, text);
    } else {
        qDebug() << "Unhandled message action:" << action;
    }
}

void MainWindow::onAvatarActionRequested(const QModelIndex& index, const QString& action) {
    if (!index.isValid()) return;

    const QString senderName = index.data(ChatBubbleSenderNameRole).toString();
    const QString senderId = index.data(ChatBubbleSenderIdRole).toString();

    if (action == QStringLiteral("at")) {
        if (m_messagesView) {
            m_messagesView->composer()->insertText(QStringLiteral("@%1 ").arg(senderName));
            m_messagesView->composer()->inputEdit()->setFocus();
        }
    } else if (action == QStringLiteral("sendMessage")) {
        if (!senderId.isEmpty() && senderId != m_currentUserId) {
            openPrivateSession(senderId);
            ui->statusbar->showMessage(QStringLiteral("已切换到与 %1 的私聊").arg(senderName), 2000);
        }
    } else if (action == QStringLiteral("viewProfile") || action == QStringLiteral("addFriend") ||
               action == QStringLiteral("setGroupNickname") || action == QStringLiteral("block") ||
               action == QStringLiteral("report")) {
        const QString commandId = camelToKebabCase(action);
        handleBackendContextCommand(commandId, QString(), index);
    } else if (action == QStringLiteral("mute")) {
        // Muting is a group-only, owner/admin action routed through the server group flow.
        if (senderId.isEmpty() || senderId == m_currentUserId) {
            ui->statusbar->showMessage(QStringLiteral("无法对该成员执行禁言"), 2000);
            return;
        }
        const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
        const bool isPublicGroup = m_privateChatTarget.isEmpty();
        if (!isLocalGroup && !isPublicGroup) {
            ui->statusbar->showMessage(QStringLiteral("禁言仅在群聊会话中可用"), 2000);
            return;
        }
        if (isPublicGroup && !canCurrentUserManageServerGroup("public")) {
            ui->statusbar->showMessage(QStringLiteral("只有群主或管理员可以禁言成员"), 2400);
            return;
        }
        if (isLocalGroup && !isCurrentUserGroupOwner(m_privateChatTarget)) {
            ui->statusbar->showMessage(QStringLiteral("只有群主可以禁言成员"), 2400);
            return;
        }
        if (isLocalGroup) {
            // Local groups have no server-side mute enforcement; inform the user.
            ui->statusbar->showMessage(QStringLiteral("本地群聊暂不支持服务端禁言"), 2400);
            return;
        }
        MuteDurationDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted) {
            ui->statusbar->showMessage(QStringLiteral("已取消禁言操作"), 1600);
            return;
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        // Permanent mute maps to the backend's maximum window (30 days).
        const qint64 mutedUntil = dialog.isPermanent()
            ? nowMs + 30LL * 24 * 60 * 60 * 1000
            : nowMs + static_cast<qint64>(dialog.durationMinutes()) * 60 * 1000;
        if (m_client && m_client->sendServerGroupMemberMute(QStringLiteral("public"), senderId, mutedUntil, QString())) {
            ui->statusbar->showMessage(QStringLiteral("已请求禁言 %1").arg(senderName), 2200);
        } else {
            ui->statusbar->showMessage(QStringLiteral("禁言失败：需要有效的服务器连接"), 3000);
        }
    } else if (action == QStringLiteral("setAdmin")) {
        // Admin promotion applies only to the server public group.
        if (senderId.isEmpty() || senderId == m_currentUserId) {
            ui->statusbar->showMessage(QStringLiteral("无法对该成员设置管理员"), 2000);
            return;
        }
        if (!m_privateChatTarget.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("管理员设置仅在公共群会话中可用"), 2000);
            return;
        }
        if (!canCurrentUserManageServerGroup("public")) {
            ui->statusbar->showMessage(QStringLiteral("只有群主可以设置管理员"), 2400);
            return;
        }
        requestServerGroupMemberUpdate(senderId, "promote_admin");
    } else {
        qDebug() << "Unhandled avatar action:" << action;
    }
}

void MainWindow::onMediaActivated(const QModelIndex& index) {
    if (!index.isValid()) return;

    QString mediaKind = index.data(ChatBubbleMediaKindRole).toString();
    QString openPath = index.data(ChatBubbleMediaOpenPathRole).toString().trimmed();

    if (mediaKind == QLatin1String("image")) {
        ImagePreviewWindow preview(this);
        preview.setWindowTitle(QStringLiteral("图片预览"));
        QVariant previewData = index.data(ChatBubbleMediaPreviewRole);
        if (!openPath.isEmpty() && QFile::exists(openPath)) {
            preview.setImagePath(openPath);
        } else if (previewData.canConvert<QPixmap>() && !previewData.value<QPixmap>().isNull()) {
            preview.setImage(previewData.value<QPixmap>());
        } else {
            preview.setErrorMessage(QStringLiteral("图片预览不可用"));
        }
        connect(&preview, &ImagePreviewWindow::saveRequested, this, [this, openPath](const QString& destinationPath) {
            if (destinationPath.isEmpty()) return;
            const QFileInfo destinationInfo(destinationPath);
            QJsonObject payload;
            payload[QStringLiteral("sourcePath")] = openPath;
            payload[QStringLiteral("directoryPath")] = destinationInfo.absolutePath();
            payload[QStringLiteral("fileName")] = destinationInfo.fileName();
            QJsonObject response;
            QString errorCode;
            QString errorMessage;
            if (QQNTBackendService::handle(QStringLiteral("save_file_to_directory"), payload, &response, &errorCode, &errorMessage)) {
                ui->statusbar->showMessage(QStringLiteral("图片已保存到: %1").arg(response.value(QStringLiteral("filePath")).toString()), 2200);
            } else {
                ui->statusbar->showMessage(QStringLiteral("保存图片失败: %1").arg(errorMessage), 3000);
            }
        });
        connect(&preview, &ImagePreviewWindow::openFolderRequested, this, [this, openPath]() {
            if (openPath.isEmpty() || !QFile::exists(openPath)) {
                ui->statusbar->showMessage(QStringLiteral("图片路径无效"), 2000);
                return;
            }
            const QFileInfo info(openPath);
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
            ui->statusbar->showMessage(QStringLiteral("已打开图片所在文件夹"), 1800);
        });
        connect(&preview, &ImagePreviewWindow::copyBase64Requested, this, [this, openPath]() {
            if (openPath.isEmpty() || !QFile::exists(openPath)) {
                ui->statusbar->showMessage(QStringLiteral("图片路径无效"), 2000);
                return;
            }
            QJsonObject payload;
            payload[QStringLiteral("filePath")] = openPath;
            QJsonObject response;
            QString errorCode;
            QString errorMessage;
            if (QQNTBackendService::handle(QStringLiteral("read_image_base64"), payload, &response, &errorCode, &errorMessage)) {
                const QString b64 = response.value(QStringLiteral("base64")).toString();
                if (!b64.isEmpty()) {
                    QClipboard* clipboard = QGuiApplication::clipboard();
                    if (clipboard) clipboard->setText(b64);
                    ui->statusbar->showMessage(QStringLiteral("图片 Base64 已复制到剪贴板"), 1800);
                } else {
                    ui->statusbar->showMessage(QStringLiteral("无法读取图片 Base64"), 2000);
                }
            } else {
                ui->statusbar->showMessage(QStringLiteral("读取 Base64 失败: %1").arg(errorMessage), 3000);
            }
        });
        connect(&preview, &ImagePreviewWindow::forwardRequested, this, [this, index]() {
            runForwardForMessage(index, index.data(Qt::DisplayRole).toString());
        });
        preview.exec();
    } else if (mediaKind == QLatin1String("video")) {
        if (!openPath.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(openPath));
            ui->statusbar->showMessage(QStringLiteral("正在打开视频..."), 1800);
        } else {
            ui->statusbar->showMessage(QStringLiteral("视频路径无效"), 2000);
        }
    } else if (!openPath.isEmpty()) {
        const QFileInfo info(openPath);
        if (info.isFile()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(openPath));
        } else if (info.isDir()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
        } else {
            ui->statusbar->showMessage(QStringLiteral("无法打开文件"), 2000);
        }
    } else {
        ui->statusbar->showMessage(QStringLiteral("无法识别的媒体内容"), 1600);
    }
}

QString MainWindow::camelToKebabCase(const QString& camel) const {
    QString result;
    for (int i = 0; i < camel.size(); ++i) {
        const QChar ch = camel.at(i);
        if (i > 0 && ch.isUpper()) {
            result.append(QLatin1Char('-'));
        }
        result.append(ch.toLower());
    }
    return result;
}

void MainWindow::runForwardForMessage(const QModelIndex& index, const QString& text) {
    if (!m_messagesView) return;

    ForwardWindow dialog(this);
    QMap<QString, QString> contactNames;
    for (const QString& id : m_friendIds) {
        contactNames[id] = contactDisplayName(id);
    }
    QMap<QString, QString> groupNames;
    for (const QString& id : m_localGroupIds) {
        groupNames[id] = m_localGroupNames.value(id, id);
    }
    dialog.setContacts(m_friendIds, contactNames);
    dialog.setGroups(m_localGroupIds, groupNames);

    connect(&dialog, &ForwardWindow::forwardToContactRequested, this, [this, index, text](const QString& userId) {
        forwardMessageToTarget(index, text, userId, QString());
    });
    connect(&dialog, &ForwardWindow::forwardToGroupRequested, this, [this, index, text](const QString& groupId) {
        forwardMessageToTarget(index, text, QString(), groupId);
    });
    dialog.exec();
}

void MainWindow::forwardMessageToTarget(const QModelIndex& index,
                                        const QString& text,
                                        const QString& userId,
                                        const QString& groupId) {
    const QString senderId = index.data(ChatBubbleSenderIdRole).toString();
    const QString senderName = index.data(ChatBubbleSenderNameRole).toString();
    const QString sessionId = m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget;
    const QString messageId = QStringLiteral("msg-%1-%2").arg(index.row()).arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz")));
    const QString targetId = userId.isEmpty() ? groupId : userId;
    if (targetId.isEmpty()) return;

    QJsonObject messageObject;
    messageObject[QStringLiteral("id")] = messageId;
    messageObject[QStringLiteral("messageId")] = messageId;
    messageObject[QStringLiteral("senderId")] = senderId;
    messageObject[QStringLiteral("senderName")] = senderName;
    messageObject[QStringLiteral("content")] = text;
    messageObject[QStringLiteral("sessionId")] = sessionId;
    messageObject[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonObject target;
    target[QStringLiteral("id")] = targetId;
    if (!userId.isEmpty()) {
        target[QStringLiteral("type")] = QStringLiteral("contact");
    } else {
        target[QStringLiteral("type")] = QStringLiteral("group");
    }

    QJsonObject payload;
    payload[QStringLiteral("message")] = messageObject;
    payload[QStringLiteral("target")] = target;

    QJsonObject response;
    QString errorCode;
    QString errorMessage;
    if (!QQNTBackendService::handle(QStringLiteral("forward_local_message"), payload, &response, &errorCode, &errorMessage)) {
        ui->statusbar->showMessage(QStringLiteral("转发失败 (%1)").arg(errorMessage), 3000);
        return;
    }
    ui->statusbar->showMessage(QStringLiteral("已转发到 %1").arg(contactDisplayName(targetId)), 2200);
}

void MainWindow::persistMultiSelectMessages(const QModelIndexList& selected) {
    const QString sessionId = m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget;
    for (const QModelIndex& index : selected) {
        const QString text = index.data(Qt::DisplayRole).toString();
        const QString senderId = index.data(ChatBubbleSenderIdRole).toString();
        const QString senderName = index.data(ChatBubbleSenderNameRole).toString();
        const QString msgId = QStringLiteral("msg-%1-%2").arg(index.row()).arg(
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz")));
        QJsonObject messageObject;
        messageObject[QStringLiteral("id")] = msgId;
        messageObject[QStringLiteral("messageId")] = msgId;
        messageObject[QStringLiteral("senderId")] = senderId;
        messageObject[QStringLiteral("senderName")] = senderName;
        messageObject[QStringLiteral("content")] = text;
        messageObject[QStringLiteral("sessionId")] = sessionId;
        messageObject[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
        messageObject[QStringLiteral("selected")] = true;
        QJsonObject payload;
        payload[QStringLiteral("message")] = messageObject;
        payload[QStringLiteral("selected")] = true;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        QQNTBackendService::handle(QStringLiteral("toggle_multi_select_local_message"), payload, &response, &errorCode, &errorMessage);
    }
}

void MainWindow::onMultiSelectForwardRequested() {
    if (!m_messagesView) return;

    const QModelIndexList selected = m_messagesView->chatListView()->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        ui->statusbar->showMessage(QStringLiteral("请先选择要转发的消息"), 2000);
        return;
    }

    ForwardWindow dialog(this);
    QMap<QString, QString> contactNames;
    for (const QString& id : m_friendIds) {
        contactNames[id] = contactDisplayName(id);
    }
    QMap<QString, QString> groupNames;
    for (const QString& id : m_localGroupIds) {
        groupNames[id] = m_localGroupNames.value(id, id);
    }
    dialog.setContacts(m_friendIds, contactNames);
    dialog.setGroups(m_localGroupIds, groupNames);

    connect(&dialog, &ForwardWindow::forwardToContactRequested, this, [this, selected](const QString& userId) {
        persistMultiSelectMessages(selected);
        for (const QModelIndex& index : selected) {
            const QString text = index.data(Qt::DisplayRole).toString();
            forwardMessageToTarget(index, text, userId, QString());
        }
    });
    connect(&dialog, &ForwardWindow::forwardToGroupRequested, this, [this, selected](const QString& groupId) {
        persistMultiSelectMessages(selected);
        for (const QModelIndex& index : selected) {
            const QString text = index.data(Qt::DisplayRole).toString();
            forwardMessageToTarget(index, text, QString(), groupId);
        }
    });
    dialog.exec();
}

void MainWindow::onMultiSelectDeleteRequested() {
    if (!m_messagesView) return;

    QModelIndexList selected = m_messagesView->chatListView()->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    std::sort(selected.begin(), selected.end(), [](const QModelIndex& a, const QModelIndex& b) {
        return a.row() > b.row();
    });

    const QString sessionId = m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget;
    QStandardItemModel* model = m_messagesView->chatModel();
    int deletedCount = 0;
    for (const QModelIndex& index : selected) {
        const QString msgId = QStringLiteral("msg-%1-%2").arg(index.row()).arg(
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz")));
        QJsonObject payload;
        payload[QStringLiteral("sessionId")] = sessionId;
        payload[QStringLiteral("messageId")] = msgId;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        if (QQNTBackendService::handle(QStringLiteral("delete_local_message"), payload, &response, &errorCode, &errorMessage)) {
            ++deletedCount;
        }
        if (model) {
            model->removeRow(index.row());
        }
    }
    ui->statusbar->showMessage(QStringLiteral("已删除 %1 条消息").arg(deletedCount), 2200);
    m_messagesView->setMultiSelectMode(false);
}

void MainWindow::onMultiSelectFavoriteRequested() {
    if (!m_messagesView) return;

    const QModelIndexList selected = m_messagesView->chatListView()->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    const QString sessionId = m_privateChatTarget.isEmpty() ? QStringLiteral("public") : m_privateChatTarget;
    int favoriteCount = 0;
    for (const QModelIndex& index : selected) {
        const QString text = index.data(Qt::DisplayRole).toString();
        const QString senderId = index.data(ChatBubbleSenderIdRole).toString();
        const QString senderName = index.data(ChatBubbleSenderNameRole).toString();
        const QString msgId = QStringLiteral("msg-%1-%2").arg(index.row()).arg(
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz")));
        QJsonObject messageObject;
        messageObject[QStringLiteral("id")] = msgId;
        messageObject[QStringLiteral("messageId")] = msgId;
        messageObject[QStringLiteral("senderId")] = senderId;
        messageObject[QStringLiteral("senderName")] = senderName;
        messageObject[QStringLiteral("content")] = text;
        messageObject[QStringLiteral("sessionId")] = sessionId;
        messageObject[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);

        QJsonObject payload;
        payload[QStringLiteral("message")] = messageObject;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        if (QQNTBackendService::handle(QStringLiteral("favorite_local_message"), payload, &response, &errorCode, &errorMessage)) {
            ++favoriteCount;
        }
    }
    ui->statusbar->showMessage(QStringLiteral("已收藏 %1 条消息").arg(favoriteCount), 2200);
    m_messagesView->setMultiSelectMode(false);
}

void MainWindow::onCaptureScreenshot(bool hideCurrentWindow) {
    // 1. Mirror the Composer screenshot setting. The default keeps the window
    // hidden, while the menu can intentionally capture the current window.
    if (hideCurrentWindow) {
        QJsonObject payload;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        QQNTBackendService::handle(QStringLiteral("hide_main_window"), payload, &response, &errorCode, &errorMessage);
        hide();
        QApplication::processEvents();
    }

    // 2. Compute the virtual desktop geometry across all monitors.
    const QList<QScreen*> screens = QGuiApplication::screens();
    QRect virtualRect;
    for (QScreen* screen : screens) {
        virtualRect = virtualRect.united(screen->geometry());
    }
    if (virtualRect.isEmpty()) {
        if (hideCurrentWindow) {
            show();
            raise();
            activateWindow();
            QJsonObject payload;
            QJsonObject response;
            QString errorCode;
            QString errorMessage;
            QQNTBackendService::handle(QStringLiteral("restore_main_window"), payload, &response, &errorCode, &errorMessage);
        }
        QMessageBox::warning(this, QStringLiteral("截图失败"), QStringLiteral("未找到可用显示器。"));
        return;
    }

    // 3. Composite every screen into one virtual-desktop QPixmap (no shared buffer).
    //    Each screen is grabbed at its own device pixel ratio and blitted into a
    //    uniformly-scaled canvas whose origin is the virtual desktop's top-left, so
    //    negative-coordinate and high-DPI secondary monitors map correctly.
    QList<qreal> dprs;
    for (QScreen* screen : screens) {
        dprs.append(screen->devicePixelRatio());
    }
    const qreal compositeScale = ScreenshotGeometry::compositeScale(dprs);
    QPixmap screenshot(QSize(qRound(virtualRect.width() * compositeScale),
                             qRound(virtualRect.height() * compositeScale)));
    if (!screenshot.isNull()) {
        screenshot.setDevicePixelRatio(1.0);
        screenshot.fill(Qt::black);
        QPainter compositor(&screenshot);
        for (QScreen* screen : screens) {
            QPixmap grab = screen->grabWindow(0);
            if (grab.isNull()) {
                continue;
            }
            // grabWindow returns a pixmap carrying the screen's DPR; draw it into the
            // canvas using logical target coordinates scaled by the composite scale.
            grab.setDevicePixelRatio(1.0);
            const QRectF target = ScreenshotGeometry::screenTargetRect(
                screen->geometry(), virtualRect, compositeScale);
            compositor.drawPixmap(target, grab, QRectF(grab.rect()));
        }
    }
    if (screenshot.isNull()) {
        if (hideCurrentWindow) {
            show();
            raise();
            activateWindow();
            QJsonObject payload;
            QJsonObject response;
            QString errorCode;
            QString errorMessage;
            QQNTBackendService::handle(QStringLiteral("restore_main_window"), payload, &response, &errorCode, &errorMessage);
        }
        QMessageBox::warning(this, QStringLiteral("截图失败"), QStringLiteral("无法获取当前屏幕内容。"));
        return;
    }

    // 4. Show a full-desktop selection overlay. The overlay is a top-level tool window
    //    and the main window is already hidden, so the screenshot excludes itself.
    ScreenshotCaptureWindow capture;
    capture.setCaptureGeometry(virtualRect);
    capture.setScreenshot(screenshot);
    connect(&capture, &ScreenshotCaptureWindow::saveRequested, this, [this](const QPixmap& cropped) {
        if (cropped.isNull()) return;
        const QString directory = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("QtNetworkChat/screenshots"));
        QDir().mkpath(directory);
        const QString filePath = QDir(directory).filePath(
            QStringLiteral("screenshot-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"))));
        if (!cropped.save(filePath, "PNG")) {
            QMessageBox::warning(this, QStringLiteral("截图失败"), QStringLiteral("无法保存截图文件。"));
            return;
        }
        qtnetworkchat::logDebug("Screenshot", QStringLiteral("saved crop to %1").arg(filePath));
        // Paste the screenshot into the composer as a file drop (sends as image/file).
        onComposerFilesDropped(QStringList{filePath});
    });
    capture.exec();
    if (hideCurrentWindow) {
        show();
        raise();
        activateWindow();
        QJsonObject payload;
        QJsonObject response;
        QString errorCode;
        QString errorMessage;
        QQNTBackendService::handle(QStringLiteral("restore_main_window"), payload, &response, &errorCode, &errorMessage);
    }
}
