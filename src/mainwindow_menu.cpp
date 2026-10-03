#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "dialogs/creategroupdialog.h"
#include "notificationpanelmanager.h"
#include "sessionitemdelegate.h"

#include <QAction>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFont>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QStatusBar>
bool MainWindow::handleCreateMenuCommand(const QString& commandId) {
    if (commandId == QLatin1String("create-group")) {
        CreateGroupDialog dialog(this);
        QStringList recentFriendIds;
        if (m_sessionModel) {
            for (int row = 0; row < m_sessionModel->rowCount(); ++row) {
                const QModelIndex index = m_sessionModel->index(row, 0);
                const QString id = index.data(SessionItemDelegate::SessionIdRole).toString();
                const bool isGroup = index.data(SessionItemDelegate::SessionGroupRole).toBool();
                if (!isGroup && m_friendIds.contains(id) && !recentFriendIds.contains(id)) recentFriendIds << id;
            }
        }
        dialog.setCandidateMembers(m_friendIds, m_friendNames, recentFriendIds);
        if (dialog.exec() != QDialog::Accepted) {
            return true;
        }
        QString groupName = dialog.groupName().trimmed();
        if (groupName.isEmpty()) {
            groupName = QStringLiteral("我的群聊");
        }
        const QStringList members = dialog.selectedMembers();
        const QString category = dialog.selectedCategory();

        const QString groupId = members.isEmpty()
            ? createLocalGroupSession(groupName)
            : createLocalGroupSession(
                  groupName,
                  members,
                  category.isEmpty()
                      ? QStringLiteral("%1 已创建，已邀请 %2 位好友。").arg(groupName).arg(members.size())
                      : QStringLiteral("群分类：%1").arg(category));
        const QMap<QString, QColor> avatarColors = {
            {QStringLiteral("blue"), QColor(QStringLiteral("#12a4ff"))},
            {QStringLiteral("green"), QColor(QStringLiteral("#18c98b"))},
            {QStringLiteral("orange"), QColor(QStringLiteral("#ff9f1a"))},
            {QStringLiteral("pink"), QColor(QStringLiteral("#fb6f92"))},
            {QStringLiteral("cyan"), QColor(QStringLiteral("#12c9bd"))},
            {QStringLiteral("purple"), QColor(QStringLiteral("#8b7cf6"))}
        };
        const QString avatarId = dialog.selectedAvatarId();
        QPixmap avatarPixmap(80, 80);
        avatarPixmap.fill(Qt::transparent);
        {
            QPainter painter(&avatarPixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(avatarColors.value(avatarId, avatarColors.value(QStringLiteral("blue"))));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(0, 0, 80, 80);
            painter.setPen(Qt::white);
            QFont font = painter.font();
            font.setBold(true);
            font.setPixelSize(30);
            painter.setFont(font);
            const QString avatarText = avatarId == QStringLiteral("green") ? QStringLiteral("G")
                : avatarId == QStringLiteral("orange") ? QStringLiteral("C")
                : avatarId == QStringLiteral("pink") ? QStringLiteral("N")
                : avatarId == QStringLiteral("cyan") ? QStringLiteral("T")
                : avatarId == QStringLiteral("purple") ? QStringLiteral("群") : QStringLiteral("Q");
            painter.drawText(avatarPixmap.rect(), Qt::AlignCenter, avatarText);
        }
        const QString groupAvatarDir = QDir(ClientStorage::appDataRootDirectory()).filePath(QStringLiteral("group_avatars"));
        QDir().mkpath(groupAvatarDir);
        const QString groupAvatarPath = QDir(groupAvatarDir).filePath(groupId + QStringLiteral(".png"));
        if (avatarPixmap.save(groupAvatarPath, "PNG")) m_localGroupAvatarPaths[groupId] = groupAvatarPath;
        switchToLocalGroup(groupId, groupName);
        showMessagesView();
        const QString created = members.isEmpty()
            ? QStringLiteral("已创建群聊: %1").arg(groupName)
            : QStringLiteral("已创建群聊: %1，并邀请 %2 位好友").arg(groupName).arg(members.size());
        appendSystemMessage(created);
        return true;
    }

    if (commandId == QLatin1String("create-group-with-friends")) {
        QString groupName = ui->contactSearchEdit->text().trimmed();
        if (groupName.isEmpty()) {
            groupName = QStringLiteral("好友群聊");
        }

        const QStringList members = m_friendIds;
        const QString groupId = createLocalGroupSession(
            groupName,
            members,
            QStringLiteral("%1 已创建，已自动邀请全部好友。").arg(groupName));
        switchToLocalGroup(groupId, groupName);
        const int invitedCount = members.size();
        appendSystemMessage(QStringLiteral("已创建群聊并邀请 %1 位好友").arg(invitedCount));
        saveHistory(groupId,
                    QStringLiteral("[%1] [系统] 已创建群聊并邀请 %2 位好友")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")))
                        .arg(invitedCount));
        return true;
    }

    if (commandId == QLatin1String("open-global-search")) {
        onShowGlobalSearch();
        return true;
    }
    if (commandId == QLatin1String("focus-contact-search")) {
        ui->contactSearchEdit->setFocus();
        ui->contactSearchEdit->selectAll();
        ui->statusbar->showMessage(QStringLiteral("已定位到 QQ 搜索框，输入账号后回车自动查找"), 2500);
        return true;
    }
    if (commandId == QLatin1String("refresh-contacts")) {
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->statusbar->showMessage(QStringLiteral("联系人和群成员已刷新"), 2000);
        return true;
    }
    if (commandId == QLatin1String("clear-search")) {
        ui->contactSearchEdit->clear();
        ui->memberSearchEdit->clear();
        m_contactFilter.clear();
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->statusbar->showMessage(QStringLiteral("搜索条件已清空"), 1800);
        return true;
    }
    if (commandId == QLatin1String("copy-chat-id")) {
        QString chatId = m_privateChatTarget;
        if (chatId.startsWith(QStringLiteral("local_group_"))) {
            chatId = chatId.mid(QStringLiteral("local_group_").size());
        }
        if (chatId.isEmpty()) {
            chatId = m_currentUserId;
        }
        copyTextWithStatus(chatId, QStringLiteral("当前会话号已复制: ") + chatId, 2500);
        return true;
    }
    if (commandId == QLatin1String("copy-chat-card")) {
        QString card;
        if (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
            card = QStringLiteral("群聊 QQ:%1\n%2\n公告:%3")
                .arg(m_privateChatTarget.mid(QStringLiteral("local_group_").size()),
                     m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊")),
                     m_localGroupAnnouncements.value(m_privateChatTarget, ui->announcementBodyLabel->text()));
        } else if (!m_privateChatTarget.isEmpty()) {
            card = QStringLiteral("QQ:%1\n昵称:%2\n状态:%3")
                .arg(m_privateChatTarget,
                     contactDisplayName(m_privateChatTarget),
                     isContactOnline(m_privateChatTarget) ? QStringLiteral("在线") : QStringLiteral("离线"));
        } else {
            card = QStringLiteral("公共聊天室\n当前账号:%1\n在线成员:%2")
                .arg(m_currentUserId)
                .arg(m_knownUsers.size());
        }
        copyTextWithStatus(card, QStringLiteral("当前会话名片已复制"), 1800);
        return true;
    }
    if (commandId == QLatin1String("copy-current-invite")) {
        QString text;
        if (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
            text = QStringLiteral("我邀请你加入群聊“%1”（群号:%2）。我是 %3（QQ:%4），进群后我们一起沟通。")
                .arg(m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊")),
                     m_privateChatTarget.mid(QStringLiteral("local_group_").size()),
                     m_currentUserName,
                     m_currentUserId);
        } else if (!m_privateChatTarget.isEmpty()) {
            text = QStringLiteral("你好 %1，我是 %2（QQ:%3）。方便的话加个好友，我们可以继续私聊。")
                .arg(contactDisplayName(m_privateChatTarget), m_currentUserName, m_currentUserId);
        } else {
            text = QStringLiteral("你好，我是 %1（QQ:%2），欢迎加入公共聊天室，也可以通过 QQ 搜索加我好友。")
                .arg(m_currentUserName, m_currentUserId);
        }
        copyTextWithStatus(text, QStringLiteral("当前会话邀请语已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-current-members")) {
        QStringList cards;
        const QStringList ids = currentSessionMemberIds();
        for (const QString& id : ids) {
            cards << QStringLiteral("QQ:%1 昵称:%2 状态:%3")
                .arg(id,
                     contactDisplayName(id),
                     (id == m_currentUserId || isContactOnline(id)) ? QStringLiteral("在线") : QStringLiteral("离线"));
        }
        if (cards.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("当前会话没有成员可复制"), 2200);
            return true;
        }
        copyTextWithStatus(cards.join(QLatin1Char('\n')),
                           QStringLiteral("已复制 %1 个当前成员").arg(cards.size()),
                           2200);
        return true;
    }
    if (commandId == QLatin1String("copy-current-online")) {
        QStringList cards;
        const QStringList ids = currentSessionMemberIds();
        for (const QString& id : ids) {
            if (id != m_currentUserId && !isContactOnline(id)) {
                continue;
            }
            cards << QStringLiteral("在线 QQ:%1 昵称:%2").arg(id, contactDisplayName(id));
        }
        if (cards.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("当前会话没有在线成员可复制"), 2200);
            return true;
        }
        copyTextWithStatus(cards.join(QLatin1Char('\n')),
                           QStringLiteral("已复制 %1 个在线成员").arg(cards.size()),
                           2200);
        return true;
    }
    if (commandId == QLatin1String("copy-all-contacts")) {
        QStringList rows;
        rows << QStringLiteral("我的QQ:%1 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QStringLiteral("好友:%1 群聊:%2 在线:%3")
            .arg(m_friendIds.size())
            .arg(m_localGroupIds.size())
            .arg(m_knownUsers.size());
        for (const QString& id : m_friendIds) {
            rows << QStringLiteral("好友 QQ:%1 昵称:%2 状态:%3")
                .arg(id,
                     contactDisplayName(id),
                     isContactOnline(id) ? QStringLiteral("在线") : QStringLiteral("离线"));
        }
        for (const QString& groupId : m_localGroupIds) {
            rows << QStringLiteral("群聊 QQ:%1 名称:%2 成员:%3")
                .arg(groupId.mid(QStringLiteral("local_group_").size()),
                     m_localGroupNames.value(groupId, QStringLiteral("群聊")),
                     QString::number(m_localGroupMembers.value(groupId).size()));
        }
        copyTextWithStatus(rows.join(QLatin1Char('\n')),
                           QStringLiteral("已复制联系人摘要 %1 行").arg(rows.size()),
                           2200);
        return true;
    }
    if (commandId == QLatin1String("copy-search-summary")) {
        QString filter = ui->contactSearchEdit->text().trimmed();
        if (filter.isEmpty()) {
            filter = QStringLiteral("全部");
        }
        const QString summary = QStringLiteral("QQ搜索:%1\n好友:%2\n本地群:%3\n在线成员:%4\n当前会话:%5")
            .arg(filter)
            .arg(m_friendIds.size())
            .arg(m_localGroupIds.size())
            .arg(m_knownUsers.size())
            .arg(m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget));
        copyTextWithStatus(summary, QStringLiteral("QQ 搜索摘要已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-quick-guide")) {
        QStringList rows;
        rows << QStringLiteral("我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QStringLiteral("1. 点击综合搜索可按 QQ 号/昵称查找用户、好友和群聊");
        rows << QStringLiteral("2. 搜索结果可直接打开、发送好友申请、复制名片或复制邀请卡");
        rows << QStringLiteral("3. 好友申请支持推荐在线用户、复制申请话术和自动发送申请");
        rows << QStringLiteral("4. 好友管理器可搜索、备注、邀入群、复制在线好友和统计");
        rows << QStringLiteral("当前好友:%1 · 群聊:%2 · 在线:%3")
            .arg(m_friendIds.size())
            .arg(m_localGroupIds.size())
            .arg(m_knownUsers.size());
        copyTextWithStatus(rows.join(QLatin1Char('\n')), QStringLiteral("QQ 功能指南已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-media-guide")) {
        QStringList rows;
        rows << QStringLiteral("上传指南 · 我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QStringLiteral("1. 点击 图片/视频 可发送 png、jpg、gif、mp4、mov、avi、mkv、wmv、flv、webm");
        rows << QStringLiteral("2. 图片会显示预览卡片，视频会以文件卡片发送");
        rows << QStringLiteral("3. 点击 闪传文件 可发送文档、压缩包和媒体文件");
        rows << QStringLiteral("4. 聊天记录右键可复制媒体卡片或查收话术");
        rows << QStringLiteral("当前会话:%1")
            .arg(m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget));
        copyTextWithStatus(rows.join(QLatin1Char('\n')), QStringLiteral("上传指南已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-current-media-pack")) {
        const QString sessionName = m_privateChatTarget.isEmpty()
            ? QStringLiteral("公共聊天室")
            : contactDisplayName(m_privateChatTarget);
        QString sessionId = m_privateChatTarget;
        if (sessionId.startsWith(QStringLiteral("local_group_"))) {
            sessionId = sessionId.mid(QStringLiteral("local_group_").size());
        }
        if (sessionId.isEmpty()) {
            sessionId = QStringLiteral("public");
        }
        QStringList rows;
        rows << QStringLiteral("媒体发送包 · 会话:%1 · 会话号:%2").arg(sessionName, sessionId);
        rows << QStringLiteral("发送者:%1 · QQ:%2").arg(m_currentUserName, m_currentUserId);
        rows << QStringLiteral("图片/视频入口：点击工具栏 图片/视频，或菜单栏 发送图片/视频");
        rows << QStringLiteral("文件入口：点击工具栏 闪传文件，或菜单栏 闪传文件");
        rows << QStringLiteral("支持格式：png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm + 文档/压缩包");
        rows << QStringLiteral("查收话术：我已准备发送图片/视频/文件到 %1，请注意查收。").arg(sessionName);
        copyTextWithStatus(rows.join(QLatin1Char('\n')), QStringLiteral("当前媒体发送包已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-full-media-plan")) {
        const QString sessionName = m_privateChatTarget.isEmpty()
            ? QStringLiteral("公共聊天室")
            : contactDisplayName(m_privateChatTarget);
        QString sessionId = m_privateChatTarget;
        if (sessionId.startsWith(QStringLiteral("local_group_"))) {
            sessionId = sessionId.mid(QStringLiteral("local_group_").size());
        }
        if (sessionId.isEmpty()) {
            sessionId = QStringLiteral("public");
        }
        QStringList rows;
        rows << QStringLiteral("完整媒体计划 · 当前会话:%1 · 会话号:%2").arg(sessionName, sessionId);
        rows << QStringLiteral("我的QQ:%1 · 昵称:%2 · 好友:%3 · 群聊:%4 · 在线:%5")
            .arg(m_currentUserId,
                 m_currentUserName,
                 QString::number(m_friendIds.size()),
                 QString::number(m_localGroupIds.size()),
                 QString::number(m_knownUsers.size()));
        rows << QStringLiteral("1. 先用综合搜索/好友申请确认目标 QQ 或群聊");
        rows << QStringLiteral("2. 通过好友管理/群通知复制媒体包、邀请语和成员列表");
        rows << QStringLiteral("3. 点击 图片/视频 发送图片、GIF 或视频；点击 闪传文件 发送文档和压缩包");
        rows << QStringLiteral("4. 发送后聊天记录会生成媒体卡片、查收话术；接收后生成回执话术和保存路径");
        rows << QStringLiteral("当前查收话术：我已准备发送媒体文件到 %1，请注意查收。").arg(sessionName);
        copyTextWithStatus(rows.join(QLatin1Char('\n')), QStringLiteral("完整媒体计划已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("edit-announcement")) {
        onEditGroupAnnouncement();
        return true;
    }
    if (commandId == QLatin1String("copy-announcement")) {
        copyTextWithStatus(ui->announcementBodyLabel->text(), QStringLiteral("群公告已复制"), 1800);
        return true;
    }
    if (commandId == QLatin1String("show-friend-notice")) {
        onShowFriendNotifications();
        return true;
    }
    if (commandId == QLatin1String("show-group-notice")) {
        onShowGroupNotifications();
        return true;
    }
    if (commandId == QLatin1String("send-image")) {
        onSendImage();
        return true;
    }
    if (commandId == QLatin1String("send-file")) {
        onSendFile();
        return true;
    }
    return false;
}

void MainWindow::onShowCreateMenu() {
    QMenu menu(this);
    auto describeAction = [](QAction* action, const QString& tip) {
        action->setToolTip(tip);
        action->setStatusTip(tip);
    };
    const struct MenuSpec {
        const char* commandId;
        const char* title;
        const char* tip;
    } specs[] = {
        {"create-group", "创建群聊", "创建一个新的本地群聊并立即进入"},
        {"create-group-with-friends", "创建群并拉全部好友", "创建群聊并自动邀请当前全部好友"},
        {"open-global-search", "申请好友/群", "打开好友申请窗口，搜索 QQ 号并发送申请"},
        {"focus-contact-search", "定位QQ搜索框", "把焦点定位到左侧 QQ 搜索框"},
        {"refresh-contacts", "刷新联系人", "重新加载好友、群聊和在线联系人列表"},
        {"clear-search", "清空搜索", "清空联系人搜索条件"},
        {"copy-chat-id", "复制当前会话号", "复制当前私聊 QQ 号或群聊号"},
        {"copy-chat-card", "复制当前会话名片", "复制当前会话的名称、账号和成员摘要"},
        {"copy-current-invite", "复制当前邀请语", "复制当前会话可用的邀请话术"},
        {"copy-current-members", "复制当前成员列表", "复制当前群聊的成员列表"},
        {"copy-current-online", "复制当前在线成员", "复制当前群聊在线成员列表"},
        {"copy-all-contacts", "复制全部联系人", "复制全部好友、群聊和在线成员摘要"},
        {"copy-search-summary", "复制搜索摘要", "复制当前搜索条件和联系人统计"},
        {"copy-quick-guide", "复制QQ功能指南", "复制 QQ 搜索、好友、群聊和媒体操作指南"},
        {"copy-media-guide", "复制上传指南", "复制图片、视频和文件上传指南"},
        {"copy-current-media-pack", "复制当前媒体包", "复制当前会话的媒体发送准备包"},
        {"copy-full-media-plan", "复制完整媒体计划", "复制好友、群聊和媒体发送的完整计划"},
        {"edit-announcement", "编辑群公告", "编辑当前本地群聊公告"},
        {"copy-announcement", "复制群公告", "复制当前本地群聊公告"},
        {"show-friend-notice", "好友通知", "打开好友通知并处理好友申请"},
        {"show-group-notice", "群通知", "打开群通知并查看群聊、公告和邀请"},
        {"send-image", "发送图片/视频", "选择图片或视频发送到当前会话"},
        {"send-file", "闪传文件", "选择文件闪传到当前会话"}
    };
    for (const MenuSpec& spec : specs) {
        QAction* action = menu.addAction(QString::fromUtf8(spec.title));
        action->setData(QString::fromLatin1(spec.commandId));
        describeAction(action, QString::fromUtf8(spec.tip));
    }
    QAction* selected = menu.exec(ui->createMenuBtn->mapToGlobal(QPoint(0, ui->createMenuBtn->height())));
    if (!selected) {
        return;
    }
    handleCreateMenuCommand(selected->data().toString());
}

bool MainWindow::handleLocalGroupContextCommand(const QString& groupId,
                                                const QString& groupLabel,
                                                const QString& commandId) {
    auto groupMemberCopyInputs = [this](const QStringList& memberIds) {
        QList<GroupNoticeMemberInput> inputs;
        for (const QString& id : memberIds) {
            GroupNoticeMemberInput input;
            input.userId = id;
            input.displayName = contactDisplayName(id);
            input.self = id == m_currentUserId;
            input.online = isContactOnline(id);
            inputs << input;
        }
        return inputs;
    };

    if (commandId == QLatin1String("open-group")) {
        switchToLocalGroup(groupId, m_localGroupNames.value(groupId, QStringLiteral("群聊")));
        return true;
    }
    if (commandId == QLatin1String("copy-group-id")) {
        const QString groupNumber = groupId.mid(QStringLiteral("local_group_").size());
        copyTextWithStatus(groupNumber, QStringLiteral("群号已复制: ") + groupNumber, 2500);
        return true;
    }
    if (commandId == QLatin1String("copy-group-card")) {
        const QString groupNumber = groupId.mid(QStringLiteral("local_group_").size());
        const QString card = QStringLiteral("群聊 QQ:%1\n%2\n成员:%3\n公告:%4")
            .arg(groupNumber,
                 m_localGroupNames.value(groupId, QStringLiteral("群聊")),
                 QString::number(m_localGroupMembers.value(groupId).size()),
                 m_localGroupAnnouncements.value(groupId,
                                                 QStringLiteral("%1 已创建，可继续邀请好友并发送消息。")
                                                     .arg(m_localGroupNames.value(groupId, QStringLiteral("群聊")))));
        copyTextWithStatus(card, QStringLiteral("群名片已复制"), 1800);
        return true;
    }
    if (commandId == QLatin1String("copy-group-invite")) {
        const QString groupName = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
        const QString groupNumber = groupId.mid(QStringLiteral("local_group_").size());
        const QString inviteText = QStringLiteral("我邀请你加入群聊“%1”（群号:%2）。我是 %3（QQ:%4），进群后我们一起沟通。")
            .arg(groupName, groupNumber, m_currentUserName, m_currentUserId);
        copyTextWithStatus(inviteText, QStringLiteral("群邀请语已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-group-members")) {
        const GroupNoticeMemberCopyState state =
            NotificationPanelManager::groupMemberCopyState(groupMemberCopyInputs(m_localGroupMembers.value(groupId)), false);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return true;
        }
        copyTextWithStatus(state.rows.join(QLatin1Char('\n')), state.copiedStatusMessage, 2200);
        return true;
    }
    if (commandId == QLatin1String("invite-friend")) {
        if (m_friendIds.isEmpty()) {
            appendSystemMessage(QStringLiteral("当前没有好友可邀请"));
            return true;
        }
        QStringList friendLabels;
        QMap<QString, QString> labelToId;
        for (const QString& friendId : m_friendIds) {
            const QString label = QStringLiteral("%1 (QQ:%2)").arg(m_friendNames.value(friendId, friendId), friendId);
            friendLabels << label;
            labelToId[label] = friendId;
        }
        bool ok = false;
        const QString selectedFriend = promptItemValue(QStringLiteral("邀请好友"),
                                                       QStringLiteral("选择好友:"),
                                                       friendLabels,
                                                       &ok);
        if (!ok || selectedFriend.isEmpty()) {
            return true;
        }
        const QString friendId = labelToId.value(selectedFriend);
        const QString friendName = m_friendNames.value(friendId, friendId);
        if (!m_localGroupMembers[groupId].contains(friendId)) {
            m_localGroupMembers[groupId] << friendId;
            saveLocalGroups();
        }
        switchToLocalGroup(groupId, m_localGroupNames.value(groupId, QStringLiteral("群聊")));
        appendSystemMessage(QStringLiteral("已邀请 %1 加入群聊").arg(friendName));
        saveHistory(groupId,
                    QStringLiteral("[%1] [系统] 已邀请 %2 加入群聊")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), friendName));
        return true;
    }
    if (commandId == QLatin1String("invite-by-account")) {
        bool ok = false;
        const QString account = promptTextValue(QStringLiteral("按QQ号邀请"),
                                                QStringLiteral("输入 QQ 账号:"),
                                                QString(),
                                                &ok);
        if (!ok) {
            return true;
        }
        if (account.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("请输入 QQ 号后再邀请入群"), 1800);
            return true;
        }
        if (account == m_currentUserId) {
            ui->statusbar->showMessage(QStringLiteral("你已在当前群聊中，无需重复邀请"), 1800);
            return true;
        }
        if (m_localGroupMembers[groupId].contains(account)) {
            ui->statusbar->showMessage(QStringLiteral("该 QQ 已在当前群聊中"), 1800);
            return true;
        }
        QString requestNote;
        m_localGroupMembers[groupId] << account;
        if (!m_friendIds.contains(account) && !m_pendingOutgoingFriendRequests.contains(account)) {
            const QString displayName = contactDisplayName(account);
            if (m_client && m_client->sendFriendRequest(account)) {
                m_friendNames[account] = displayName;
                m_pendingOutgoingFriendRequests << account;
                requestNote = QStringLiteral("，好友申请等待确认");
            } else {
                requestNote = QStringLiteral("，好友申请发送失败");
                ui->statusbar->showMessage(QStringLiteral("已邀请入群，但好友申请发送失败：%1").arg(displayName), 3000);
            }
        } else if (m_pendingOutgoingFriendRequests.contains(account)) {
            requestNote = QStringLiteral("，好友申请已在等待确认");
        }
        saveLocalGroups();
        refreshFriendList();
        switchToLocalGroup(groupId, m_localGroupNames.value(groupId, QStringLiteral("群聊")));
        refreshGroupMemberPanel();
        appendSystemMessage(QStringLiteral("已按 QQ 号邀请 %1 加入群聊%2").arg(account, requestNote));
        saveHistory(groupId,
                    QStringLiteral("[%1] [系统] 已按 QQ 号邀请 %2 加入群聊")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), account));
        return true;
    }
    if (commandId == QLatin1String("invite-all-friends")) {
        QStringList inviteIds;
        for (const QString& friendId : m_friendIds) {
            if (!m_localGroupMembers[groupId].contains(friendId)) {
                inviteIds << friendId;
            }
        }
        if (inviteIds.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("全部好友已在该群聊中"), 1800);
            return true;
        }
        const QString groupName = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
        if (!confirmAction(QStringLiteral("邀请全部好友"),
                           QStringLiteral("确定邀请 %1 位好友加入群聊“%2”吗？").arg(inviteIds.size()).arg(groupName),
                           QStringLiteral("已取消邀请全部好友"))) {
            return true;
        }
        for (const QString& friendId : inviteIds) {
            m_localGroupMembers[groupId] << friendId;
        }
        saveLocalGroups();
        switchToLocalGroup(groupId, m_localGroupNames.value(groupId, QStringLiteral("群聊")));
        appendSystemMessage(QStringLiteral("已自动邀请 %1 位好友加入群聊").arg(inviteIds.size()));
        saveHistory(groupId,
                    QStringLiteral("[%1] [系统] 已自动邀请 %2 位好友加入群聊")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")))
                        .arg(inviteIds.size()));
        return true;
    }
    if (commandId == QLatin1String("copy-group-online-members")) {
        const GroupNoticeMemberCopyState state =
            NotificationPanelManager::groupMemberCopyState(groupMemberCopyInputs(m_localGroupMembers.value(groupId)), true);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return true;
        }
        copyTextWithStatus(state.rows.join(QLatin1Char('\n')), state.copiedStatusMessage, 2200);
        return true;
    }
    if (commandId == QLatin1String("rename-group")) {
        bool ok = false;
        const QString oldName = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
        const QString newName = promptTextValue(QStringLiteral("重命名群聊"),
                                                QStringLiteral("群聊名称:"),
                                                oldName,
                                                &ok);
        if (!ok) {
            return true;
        }
        if (newName.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("群聊名称不能为空"), 1800);
            return true;
        }
        if (newName == oldName) {
            ui->statusbar->showMessage(QStringLiteral("群聊名称未改变"), 1600);
            return true;
        }
        m_localGroupNames[groupId] = newName;
        saveLocalGroups();
        refreshFriendList();
        if (m_privateChatTarget == groupId) {
            switchToLocalGroup(groupId, newName);
        }
        ui->statusbar->showMessage(QStringLiteral("群聊已重命名为：%1").arg(newName), 2200);
        return true;
    }
    if (commandId == QLatin1String("delete-group")) {
        const QString groupName = m_localGroupNames.value(groupId, groupLabel);
        const int memberCount = qMax(1, m_localGroupMembers.value(groupId).size());
        if (!confirmAction(QStringLiteral("删除群聊"),
                           QStringLiteral("确定删除群聊“%1”吗？本地群成员 %2 人，聊天记录不会在此步骤删除。")
                               .arg(groupName)
                               .arg(memberCount),
                           QStringLiteral("已取消删除群聊"))) {
            return true;
        }
        m_localGroupIds.removeAll(groupId);
        m_localGroupNames.remove(groupId);
        m_localGroupAnnouncements.remove(groupId);
        m_localGroupMembers.remove(groupId);
        saveLocalGroups();
        refreshFriendList();
        if (m_privateChatTarget == groupId) {
            onBackToGroupChat();
        }
        appendSystemMessage(QStringLiteral("已删除群聊: ") + groupName);
        return true;
    }

    return false;
}

bool MainWindow::handleContactContextCommand(const QString& userId,
                                             const QString& commandId) {
    if (commandId == QLatin1String("copy-account")) {
        copyTextWithStatus(userId, QStringLiteral("QQ 号已复制: ") + userId, 2500);
        return true;
    }
    if (commandId == QLatin1String("copy-profile-card")) {
        const QString card = QStringLiteral("QQ:%1\n昵称:%2\n状态:%3")
            .arg(userId,
                 contactDisplayName(userId),
                 isContactOnline(userId) ? QStringLiteral("在线") : QStringLiteral("离线"));
        copyTextWithStatus(card, QStringLiteral("联系人名片已复制"), 1800);
        return true;
    }
    if (commandId == QLatin1String("copy-add-text")) {
        const QString text = QStringLiteral("你好，我是 %1（QQ:%2），通过 QQ 搜索看到你。方便的话加个好友，我们可以私聊或一起进群沟通。")
            .arg(m_currentUserName, m_currentUserId);
        copyTextWithStatus(text, QStringLiteral("好友申请话术已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-online-card")) {
        const QString card = QStringLiteral("QQ:%1\n昵称:%2\n状态:%3\n关系:%4\n当前会话:%5")
            .arg(userId,
                 contactDisplayName(userId),
                 isContactOnline(userId) ? QStringLiteral("在线") : QStringLiteral("离线"),
                 m_friendIds.contains(userId) ? QStringLiteral("好友")
                                              : (m_pendingOutgoingFriendRequests.contains(userId) ? QStringLiteral("申请中")
                                                                                                  : QStringLiteral("陌生人")),
                 m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget));
        copyTextWithStatus(card, QStringLiteral("在线名片已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-chat-starter")) {
        const QString text = m_friendIds.contains(userId)
            ? QStringLiteral("%1，在吗？我是 %2（QQ:%3），想和你私聊确认一下刚才的消息。")
                  .arg(contactDisplayName(userId), m_currentUserName, m_currentUserId)
            : m_pendingOutgoingFriendRequests.contains(userId)
                ? QStringLiteral("%1，你好，我是 %2（QQ:%3），我已经发送好友申请了，通过后我们可以继续私聊。")
                      .arg(contactDisplayName(userId), m_currentUserName, m_currentUserId)
                : QStringLiteral("你好 %1，我是 %2（QQ:%3）。通过 QQ 搜索看到你，方便通过好友申请后再聊吗？")
                      .arg(contactDisplayName(userId), m_currentUserName, m_currentUserId);
        copyTextWithStatus(text, QStringLiteral("开聊话术已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-e2e-status")) {
        copyE2ESessionStatus(userId);
        return true;
    }
    if (commandId == QLatin1String("copy-e2e-identity")) {
        const QJsonObject identity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
        const QString text = identity.value(QStringLiteral("configured")).toBool(false)
            ? QStringLiteral("端到端加密身份\n对端QQ：%1\n信任状态：%2\n验证状态：%3\n公钥指纹：%4\n验证短码：%5\n首次看到：%6\n最近看到：%7")
                  .arg(userId,
                       identity.value(QStringLiteral("trustState")).toString(),
                       identity.value(QStringLiteral("verified")).toBool(false) ? QStringLiteral("verified")
                                                                                 : QStringLiteral("unverified"),
                       identity.value(QStringLiteral("publicKeyFingerprintSha256")).toString(),
                       identity.value(QStringLiteral("verificationCodeDisplay")).toString(),
                       identity.value(QStringLiteral("firstSeenAt")).toString(),
                       identity.value(QStringLiteral("lastSeenAt")).toString())
            : QStringLiteral("端到端加密身份\n对端QQ：%1\n信任状态：unknown\n说明：尚未收到该联系人的身份公告").arg(userId);
        copyTextWithStatus(text, QStringLiteral("端到端加密身份指纹已复制"), 2400);
        return true;
    }
    if (commandId == QLatin1String("copy-e2e-verification")) {
        const QJsonObject identity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
        const QString code = identity.value(QStringLiteral("verificationCodeDisplay")).toString();
        const QString text = identity.value(QStringLiteral("configured")).toBool(false) && !code.isEmpty()
            ? QStringLiteral("端到端加密验证短码\n对端QQ：%1\n短码：%2\n核对方式：请通过另一个可信渠道与对方屏幕上的短码一致后再验证信任。")
                  .arg(userId, code)
            : QStringLiteral("端到端加密验证短码\n对端QQ：%1\n说明：尚未收到该联系人的身份公告").arg(userId);
        copyTextWithStatus(text, QStringLiteral("端到端加密验证短码已复制"), 2400);
        return true;
    }
    if (commandId == QLatin1String("trust-e2e-identity")) {
        QString rejectReason;
        if (m_client && m_client->pinE2EPeerIdentity(userId, QString(), &rejectReason)) {
            appendSystemMessage(QStringLiteral("已固定 %1 的端到端加密身份指纹；完成验证短码核对前不会启用默认加密")
                                    .arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("端到端加密身份已固定，等待验证"), 2600);
        } else {
            appendSystemMessage(QStringLiteral("信任端到端加密身份失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("信任端到端加密身份失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("verify-e2e-identity")) {
        const QJsonObject identity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
        const QString suggestedCode = identity.value(QStringLiteral("verificationCodeDisplay")).toString();
        bool ok = false;
        const QString code = promptTextValue(QStringLiteral("验证加密身份"),
                                             QStringLiteral("请输入与 %1 通过可信渠道核对一致的验证短码:")
                                                 .arg(contactDisplayName(userId)),
                                             suggestedCode,
                                             &ok);
        if (!ok) {
            ui->statusbar->showMessage(QStringLiteral("已取消端到端加密身份验证"), 1800);
            return true;
        }
        QString rejectReason;
        if (m_client && m_client->verifyAndPinE2EPeerIdentity(userId, code, &rejectReason)) {
            appendSystemMessage(QStringLiteral("已验证并信任 %1 的端到端加密身份").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("端到端加密身份已验证"), 2400);
        } else {
            appendSystemMessage(QStringLiteral("验证端到端加密身份失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("验证端到端加密身份失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("clear-e2e-identity-trust")) {
        QString rejectReason;
        if (m_client && m_client->clearE2EPeerIdentityPin(userId, &rejectReason)) {
            appendSystemMessage(QStringLiteral("已清除 %1 的端到端加密身份信任固定").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("端到端加密身份信任已清除"), 2400);
        } else {
            appendSystemMessage(QStringLiteral("清除端到端加密身份信任失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("清除端到端加密身份信任失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("request-e2e-rotation")) {
        QString rejectReason;
        if (m_client && m_client->requestE2ESessionRotation(userId, &rejectReason)) {
            appendSystemMessage(QStringLiteral("已向 %1 发送端到端加密轮换请求").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("端到端加密轮换请求已发送"), 2400);
        } else {
            appendSystemMessage(QStringLiteral("端到端加密轮换请求发送失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("端到端加密轮换请求发送失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("clear-e2e-session")) {
        if (m_client) {
            m_client->clearE2ESessionKey(userId);
            appendSystemMessage(QStringLiteral("已关闭 %1 的本机端到端加密会话").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("本机端到端加密会话已关闭"), 2400);
        }
        return true;
    }
    if (commandId == QLatin1String("clear-e2e-backend-migration")) {
        QString rejectReason;
        if (m_client && m_client->clearE2EBackendMigrationState(&rejectReason)) {
            appendSystemMessage(QStringLiteral("已清理本机端到端加密后端迁移状态；需要重新接收身份公告、核对短码并建立会话后才能继续默认加密"));
            ui->statusbar->showMessage(QStringLiteral("端到端加密迁移状态已清理"), 3200);
        } else {
            appendSystemMessage(QStringLiteral("清理端到端加密迁移状态失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("清理端到端加密迁移状态失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("invite-current-group")) {
        QString requestNote;
        if (!m_friendIds.contains(userId)) {
            if (m_pendingOutgoingFriendRequests.contains(userId)) {
                requestNote = QStringLiteral("，好友申请已在等待确认");
            } else if (m_client && m_client->sendFriendRequest(userId)) {
                m_friendNames[userId] = contactDisplayName(userId);
                m_pendingOutgoingFriendRequests << userId;
                requestNote = QStringLiteral("，好友申请等待确认");
            } else {
                requestNote = QStringLiteral("，好友申请发送失败");
                ui->statusbar->showMessage(QStringLiteral("已邀请入群，但好友申请发送失败：%1").arg(contactDisplayName(userId)), 3000);
            }
        }
        if (!m_localGroupMembers[m_privateChatTarget].contains(userId)) {
            m_localGroupMembers[m_privateChatTarget] << userId;
            saveLocalGroups();
        }
        refreshFriendList();
        refreshGroupMemberPanel();
        appendSystemMessage(QStringLiteral("已邀请 %1 加入当前群聊%2").arg(contactDisplayName(userId), requestNote));
        saveHistory(m_privateChatTarget,
                    QStringLiteral("[%1] [系统] 已邀请 %2 加入群聊")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), contactDisplayName(userId)));
        return true;
    }
    if (commandId == QLatin1String("rename-friend")) {
        bool ok = false;
        const QString oldRemark = contactDisplayName(userId);
        const QString remark = promptTextValue(QStringLiteral("设置备注"),
                                               QStringLiteral("备注名称:"),
                                               oldRemark,
                                               &ok);
        if (!ok) {
            return true;
        }
        if (remark.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("备注名称不能为空"), 1800);
            return true;
        }
        if (remark == oldRemark) {
            ui->statusbar->showMessage(QStringLiteral("备注未改变"), 1600);
            return true;
        }
        m_friendNames[userId] = remark;
        saveFriends();
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->statusbar->showMessage(QStringLiteral("已设置备注：%1").arg(remark), 2200);
        appendSystemMessage(QStringLiteral("已设置 %1 的备注为 %2").arg(userId, remark));
        return true;
    }
    if (commandId == QLatin1String("add-friend")) {
        if (m_pendingOutgoingFriendRequests.contains(userId)) {
            ui->statusbar->showMessage(QStringLiteral("已向 %1 发送过好友申请，等待对方处理").arg(contactDisplayName(userId)), 2500);
            return true;
        }
        if (!m_friendIds.contains(userId)) {
            const QString displayName = contactDisplayName(userId);
            if (!m_client || !m_client->sendFriendRequest(userId)) {
                ui->statusbar->showMessage(QStringLiteral("好友申请发送失败：%1").arg(displayName), 3000);
                appendSystemMessage(QStringLiteral("好友申请发送失败 QQ:%1，请检查连接后重试").arg(userId));
                return true;
            }
            m_friendNames[userId] = displayName;
            m_pendingOutgoingFriendRequests << userId;
            refreshFriendList();
            appendSystemMessage(QStringLiteral("已发送好友申请: %1（QQ:%2），等待对方同意").arg(displayName, userId));
        }
        return true;
    }
    if (commandId == QLatin1String("remove-friend")) {
        const QString displayName = contactDisplayName(userId);
        if (!confirmAction(QStringLiteral("删除好友"),
                           QStringLiteral("确定删除好友“%1”（QQ:%2）吗？删除后可重新搜索并发送申请。").arg(displayName, userId),
                           QStringLiteral("已取消删除好友"))) {
            return true;
        }
        m_friendIds.removeAll(userId);
        m_friendNames.remove(userId);
        saveFriends();
        refreshFriendList();
        appendSystemMessage(QStringLiteral("已删除好友: %1（QQ:%2）").arg(displayName, userId));
        return true;
    }
    return false;
}

void MainWindow::onUserContextMenu(const QPoint& pos) {
    QModelIndex index = ui->userListView->indexAt(pos);
    if (!index.isValid()) return;

    QString userId = index.data(Qt::UserRole + 1).toString();
    if (userId.isEmpty()) return;
    QString userName = index.data().toString();
    userName.remove(QRegularExpression("^[★☆○]\\s*"));
    userName.remove(QRegularExpression("^群聊\\s+QQ:[^\\n]+\\n\\s*"));
    userName.remove(QRegularExpression("\\s*\\[(在线|离线|本地)\\]$"));

    QMenu menu(this);
    if (m_localGroupIds.contains(userId)) {
        const struct GroupActionSpec {
            const char* id;
            const char* title;
            const char* tip;
        } specs[] = {
            {"open-group", "进入群聊", "进入当前本地群聊并加载聊天记录"},
            {"copy-group-id", "复制群号", "复制当前群聊的群号"},
            {"copy-group-card", "复制群名片", "复制群名、群号、成员数和公告摘要"},
            {"copy-group-invite", "复制群邀请语", "复制一段可发送给好友的入群邀请语"},
            {"copy-group-members", "复制成员列表", "复制当前群聊的全部成员列表"},
            {"invite-friend", "邀请好友", "从好友列表中选择一个好友邀请入群"},
            {"invite-by-account", "按QQ号邀请", "输入 QQ 号邀请用户入群，并按需发送好友申请"},
            {"invite-all-friends", "邀请全部好友", "把当前全部好友批量邀请进该群聊"},
            {"copy-group-online-members", "复制在线成员", "复制当前群聊在线成员的 QQ 和昵称"},
            {"rename-group", "重命名群聊", "修改当前本地群聊名称"},
            {"delete-group", "删除群聊", "删除当前本地群聊配置，聊天记录不会在此步骤删除"}
        };
        for (const GroupActionSpec& spec : specs) {
            QAction* action = menu.addAction(QString::fromUtf8(spec.title));
            action->setData(QString::fromLatin1(spec.id));
            action->setToolTip(QString::fromUtf8(spec.tip));
            action->setStatusTip(QString::fromUtf8(spec.tip));
        }
        QAction* selected = menu.exec(ui->userListView->viewport()->mapToGlobal(pos));
        if (!selected) return;
        handleLocalGroupContextCommand(userId, userName, selected->data().toString());
        return;
    }

    const QJsonObject currentE2EIdentity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
    const QJsonObject currentE2ESession = m_client ? m_client->e2eSessionStatus(userId) : QJsonObject();
    const QJsonObject currentE2ELocalIdentity = m_client ? m_client->e2eLocalIdentityStatus() : QJsonObject();
    const bool e2eBackendMigrationRequired =
        currentE2ELocalIdentity.value("backendMigrationRequired").toBool(false)
        || currentE2EIdentity.value("backendMigrationRequired").toBool(false)
        || currentE2ESession.value("backendMigrationRequired").toBool(false);
    const bool hasPendingOutgoing = m_pendingOutgoingFriendRequests.contains(userId);
    const struct ContactActionSpec {
        const char* id;
        const char* title;
        const char* tip;
        bool enabled;
    } baseSpecs[] = {
        {"chat", "发送消息", "打开当前联系人私聊会话", true},
        {"copy-account", "复制QQ号", "复制当前联系人 QQ 号", true},
        {"copy-profile-card", "复制名片", "复制当前联系人 QQ、昵称和关系状态", true},
        {"copy-add-text", "复制申请话术", "复制适合当前联系人的好友申请话术", true},
        {"copy-online-card", "复制在线名片", "复制当前联系人的在线名片和状态", true},
        {"copy-chat-starter", "复制开聊话术", "复制一段可直接发送的开聊话术", true},
        {"copy-e2e-status", "复制加密状态", "复制当前联系人端到端加密会话状态", true},
        {"copy-e2e-identity", "复制加密身份指纹", "复制本机记录的联系人端到端加密身份指纹", true},
        {"copy-e2e-verification", "复制加密验证短码", "复制需要与对方跨设备核对的端到端加密验证短码", true},
        {"trust-e2e-identity", "信任加密身份", "仅固定当前记录的联系人端到端加密身份指纹，仍需验证短码后才能用于默认加密", true},
        {"verify-e2e-identity", "验证并信任加密身份", "输入与对方核对一致的验证短码并将身份标记为已验证信任", true},
        {"request-e2e-rotation", "请求加密轮换", "向当前联系人发送端到端加密会话轮换请求；不包含本机会话密钥", true}
    };
    for (const ContactActionSpec& spec : baseSpecs) {
        QAction* action = menu.addAction(QString::fromUtf8(spec.title));
        action->setData(QString::fromLatin1(spec.id));
        action->setToolTip(QString::fromUtf8(spec.tip));
        action->setStatusTip(QString::fromUtf8(spec.tip));
        action->setEnabled(spec.enabled);
    }
    if (currentE2EIdentity.value("pinned").toBool(false)) {
        QAction* action = menu.addAction(QStringLiteral("清除加密身份信任"));
        action->setData(QStringLiteral("clear-e2e-identity-trust"));
        action->setToolTip(QStringLiteral("清除当前联系人端到端加密身份固定信任并恢复为未验证"));
        action->setStatusTip(action->toolTip());
    }
    if (m_client && m_client->hasE2ESession(userId)) {
        QAction* action = menu.addAction(QStringLiteral("关闭本机会话密钥"));
        action->setData(QStringLiteral("clear-e2e-session"));
        action->setToolTip(QStringLiteral("清除本机为该联系人保存的端到端会话密钥"));
        action->setStatusTip(action->toolTip());
    }
    if (e2eBackendMigrationRequired) {
        QAction* action = menu.addAction(QStringLiteral("清理加密后端迁移状态"));
        action->setData(QStringLiteral("clear-e2e-backend-migration"));
        action->setToolTip(QStringLiteral("清除本机旧加密后端身份、信任和会话状态，等待新后端重新建立信任"));
        action->setStatusTip(action->toolTip());
    }
    if (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
        QAction* action = menu.addAction(QStringLiteral("邀入当前群"));
        action->setData(QStringLiteral("invite-current-group"));
        action->setToolTip(QStringLiteral("邀请当前联系人加入正在查看的本地群聊"));
        action->setStatusTip(action->toolTip());
    }
    if (m_friendIds.contains(userId)) {
        QAction* renameAction = menu.addAction(QStringLiteral("设置备注"));
        renameAction->setData(QStringLiteral("rename-friend"));
        renameAction->setToolTip(QStringLiteral("修改当前好友在本地显示的备注名"));
        renameAction->setStatusTip(renameAction->toolTip());
        QAction* removeAction = menu.addAction(QStringLiteral("删除好友"));
        removeAction->setData(QStringLiteral("remove-friend"));
        removeAction->setToolTip(QStringLiteral("从本地好友列表删除当前好友"));
        removeAction->setStatusTip(removeAction->toolTip());
    } else {
        QAction* addAction = menu.addAction(hasPendingOutgoing ? QStringLiteral("好友申请待确认") : QStringLiteral("加为好友"));
        addAction->setData(QStringLiteral("add-friend"));
        addAction->setEnabled(!hasPendingOutgoing);
        addAction->setToolTip(hasPendingOutgoing ? QStringLiteral("好友申请已发送，等待对方处理")
                                                 : QStringLiteral("向当前联系人发送好友申请"));
        addAction->setStatusTip(addAction->toolTip());
    }

    QAction* selected = menu.exec(ui->userListView->viewport()->mapToGlobal(pos));
    if (!selected) return;
    const QString commandId = selected->data().toString();
    if (commandId == QLatin1String("chat")) {
        onPrivateChat(index);
    } else {
        handleContactContextCommand(userId, commandId);
    }
}
