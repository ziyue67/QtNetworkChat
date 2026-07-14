import re
from pathlib import Path

path = Path(r"D:\C++VS pro\QtNetworkChat\src\mainwindow.cpp")
text = path.read_text(encoding="utf-8")

# 1) Add group nickname dialog include after mutedurationdialog include
text = re.sub(
    r'(#include "dialogs/mutedurationdialog.h")\r?\n',
    r'\1\n#include "dialogs/groupnicknamedialog.h"\n',
    text,
    count=1,
)

# 2) Connect new MessagesView signals after multiSelectFavoriteRequested line
text = re.sub(
    r'(connect\(m_messagesView, &MessagesView::multiSelectFavoriteRequested, this, &MainWindow::onMultiSelectFavoriteRequested\);)\r?\n',
    r'''\1
    connect(m_messagesView, &MessagesView::groupMembersRequested, this, &MainWindow::onShowGroupMemberSidebar);
    connect(m_messagesView, &MessagesView::essenceRequested, this, &MainWindow::onShowEssencePanel);
''',
    text,
    count=1,
)

# 3) Wire GroupMemberSidebar signals and visibility after creation
old_creation = '    m_groupMemberSidebar = new GroupMemberSidebar(m_qqntRoot);\n    m_essencePanel = new EssencePanel(m_qqntRoot);'
new_creation = '''    m_groupMemberSidebar = new GroupMemberSidebar(m_qqntRoot);
    m_essencePanel = new EssencePanel(m_qqntRoot);

    connect(m_groupMemberSidebar, &GroupMemberSidebar::chatWithMember, this, [this](const QString& userId) {
        if (userId.isEmpty()) return;
        openPrivateSession(userId);
    });
    connect(m_groupMemberSidebar, &GroupMemberSidebar::atMember, this, [this](const QString&, const QString& displayName) {
        if (!m_messagesView || !m_messagesView->composer() || !m_messagesView->composer()->inputEdit()) return;
        QTextEdit* edit = m_messagesView->composer()->inputEdit();
        edit->setTextCursor(edit->textCursor());
        edit->insertPlainText(QStringLiteral("@%1 ").arg(displayName));
        edit->setFocus(Qt::OtherFocusReason);
    });
    connect(m_groupMemberSidebar, &GroupMemberSidebar::viewProfile, this, [this](const QString& userId) {
        if (userId.isEmpty()) return;
        if (m_profileView) m_profileView->setUserInfo(userId, contactDisplayName(userId));
        onAppNavRouteActivated(QStringLiteral("profile"));
    });
    connect(m_groupMemberSidebar, &GroupMemberSidebar::addFriend, this, [this](const QString& userId) {
        if (userId.isEmpty()) return;
        if (m_friendIds.contains(userId)) {
            ui->statusbar->showMessage(QStringLiteral("%1 已是好友").arg(contactDisplayName(userId)), 2000);
            return;
        }
        if (m_client && m_client->isConnected() && m_client->sendFriendRequest(userId)) {
            m_friendNames[userId] = contactDisplayName(userId);
            ui->statusbar->showMessage(QStringLiteral("已向 %1 发送好友申请").arg(contactDisplayName(userId)), 2000);
        } else {
            ensureFriendRequestQueued(userId, QStringLiteral("来自群成员侧栏"), true);
            ui->statusbar->showMessage(QStringLiteral("已暂存对 %1 的好友申请").arg(contactDisplayName(userId)), 2000);
        }
    });
    connect(m_groupMemberSidebar, &GroupMemberSidebar::renameMember, this, [this](const QString& userId, const QString& currentName) {
        GroupNicknameDialog dialog(this);
        dialog.setCurrentNickname(currentName);
        dialog.setWindowTitle(QStringLiteral("设置 %1 的群昵称").arg(contactDisplayName(userId)));
        if (dialog.exec() == QDialog::Accepted) {
            ui->statusbar->showMessage(QStringLiteral("%1 的群昵称已设为 %2").arg(contactDisplayName(userId), dialog.nickname()), 2000);
        }
    });
    connect(m_groupMemberSidebar, &GroupMemberSidebar::muteMember, this, [this](const QString& userId, int minutes) {
        if (minutes < 0) {
            MuteDurationDialog dialog(this);
            if (dialog.exec() != QDialog::Accepted) return;
            ui->statusbar->showMessage(QStringLiteral("已将 %1 禁言 %2 分钟").arg(contactDisplayName(userId)).arg(dialog.durationMinutes()), 2000);
        } else {
            ui->statusbar->showMessage(QStringLiteral("已将 %1 禁言 %2 分钟").arg(contactDisplayName(userId)).arg(minutes), 2000);
        }
    });
    connect(m_groupMemberSidebar, &GroupMemberSidebar::unmuteMember, this, [this](const QString& userId) {
        ui->statusbar->showMessage(QStringLiteral("已解除 %1 的禁言").arg(contactDisplayName(userId)), 2000);
    });
    connect(m_groupMemberSidebar, &GroupMemberSidebar::promoteAdmin, this, [this](const QString& userId) {
        ui->statusbar->showMessage(QStringLiteral("%1 已设为管理员").arg(contactDisplayName(userId)), 2000);
    });
    connect(m_groupMemberSidebar, &GroupMemberSidebar::demoteAdmin, this, [this](const QString& userId) {
        ui->statusbar->showMessage(QStringLiteral("%1 已取消管理员").arg(contactDisplayName(userId)), 2000);
    });
    connect(m_groupMemberSidebar, &GroupMemberSidebar::kickMember, this, [this](const QString& userId) {
        if (userId.isEmpty()) return;
        const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
        if (isLocalGroup && isCurrentUserGroupOwner(m_privateChatTarget) && m_localGroupMembers[m_privateChatTarget].contains(userId)) {
            m_localGroupMembers[m_privateChatTarget].removeAll(userId);
            saveLocalGroups();
            refreshGroupMemberPanel();
            refreshGroupMemberSidebar();
            appendSystemMessage(QStringLiteral("已将 %1 移出群聊").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("%1 已移出本群").arg(contactDisplayName(userId)), 2000);
        } else {
            requestServerGroupMemberUpdate(userId, QStringLiteral("remove"));
            ui->statusbar->showMessage(QStringLiteral("已向服务端请求移出 %1").arg(contactDisplayName(userId)), 2000);
        }
    });
    connect(m_groupMemberSidebar, &GroupMemberSidebar::reportMember, this, [this](const QString& userId) {
        ui->statusbar->showMessage(QStringLiteral("举报 %1 已提交").arg(contactDisplayName(userId)), 2000);
    });
    connect(m_groupMemberSidebar, &GroupMemberSidebar::blockMember, this, [this](const QString& userId) {
        ui->statusbar->showMessage(QStringLiteral("%1 已屏蔽").arg(contactDisplayName(userId)), 2000);
    });'''
if old_creation not in text:
    print("sidebar creation block not found")
    raise SystemExit(1)
text = text.replace(old_creation, new_creation + '\n', 1)

# 4) Toggle group action buttons in refreshWorkspaceChrome
text = re.sub(
    r'(void MainWindow::refreshWorkspaceChrome\(\)\s*\{[\s\S]*?const bool isLocalGroup = !m_privateChatTarget\.isEmpty\(\) && m_privateChatTarget\.startsWith\(QStringLiteral\("local_group_"\)\);)',
    r'\1\n    if (m_messagesView) m_messagesView->setGroupActionsVisible(isLocalGroup);',
    text,
    count=1,
)

# 5) Add refreshGroupMemberSidebar helper before onShowGroupMemberSidebar
helper_block = '''void MainWindow::refreshGroupMemberSidebar()
{
    if (!m_groupMemberSidebar) return;
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    const bool isServerPublicGroup = m_privateChatTarget.isEmpty() && !m_serverGroupMembers.value(QStringLiteral("public")).isEmpty();
    if (!isLocalGroup && !isServerPublicGroup) {
        m_groupMemberSidebar->setGroupId(QString());
        m_groupMemberSidebar->setGroupName(QString());
        m_groupMemberSidebar->setMembers(QList<GroupMemberDisplayData>());
        return;
    }

    const QString groupId = m_privateChatTarget;
    const QString activeGroupId = isServerPublicGroup ? QStringLiteral("public") : groupId;
    const QString groupName = isLocalGroup
        ? m_localGroupNames.value(groupId, QStringLiteral("群聊"))
        : m_serverGroupNames.value(QStringLiteral("public"), QStringLiteral("公共聊天室"));
    const QString announcement = isLocalGroup
        ? m_localGroupAnnouncements.value(groupId)
        : m_serverGroupAnnouncements.value(QStringLiteral("public"));

    m_groupMemberSidebar->setGroupId(groupId);
    m_groupMemberSidebar->setGroupName(groupName);
    m_groupMemberSidebar->setAnnouncement(announcement);

    QList<GroupMemberDisplayData> members;
    QStringList memberIds;
    if (isLocalGroup) {
        memberIds = m_localGroupMembers.value(groupId);
    } else {
        memberIds = m_serverGroupMembers.value(QStringLiteral("public"));
    }

    const QString ownerId = isLocalGroup ? groupOwnerId(groupId) : m_serverGroupOwners.value(QStringLiteral("public"));
    for (const QString& id : memberIds) {
        if (id.isEmpty()) continue;
        GroupMemberDisplayData d;
        d.id = id;
        d.nickname = contactDisplayName(id);
        d.role = (id == ownerId) ? QStringLiteral("owner") : QStringLiteral("member");
        if (!isLocalGroup) {
            const QString serverRole = m_serverGroupMemberRoles.value(QStringLiteral("public")).value(id);
            if (!serverRole.isEmpty()) d.role = serverRole;
        }
        members << d;
    }
    m_groupMemberSidebar->setMembers(members);

    QSet<QString> onlineIds;
    for (const QString& id : memberIds) {
        if (id == m_currentUserId || isContactOnline(id)) onlineIds.insert(id);
    }
    m_groupMemberSidebar->setOnlineUsers(onlineIds);
}

'''
text = re.sub(
    r'void MainWindow::onShowGroupMemberSidebar\(\)\s*\{',
    helper_block + r'void MainWindow::onShowGroupMemberSidebar() {',
    text,
    count=1,
)

# 6) Replace onShowGroupMemberSidebar body to show prepopulated sidebar
old_body = '''void MainWindow::onShowGroupMemberSidebar()
{
    if (!m_groupMemberSidebar) return;
    m_groupMemberSidebar->setGroupId(QStringLiteral(""));
    m_groupMemberSidebar->show();
}'''
new_body = '''void MainWindow::onShowGroupMemberSidebar()
{
    if (!m_groupMemberSidebar) return;
    refreshGroupMemberSidebar();
    m_groupMemberSidebar->show();
}'''
text = text.replace(old_body, new_body, 1)

path.write_text(text, encoding="utf-8")
print("mainwindow.cpp patched successfully")
