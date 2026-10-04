#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "dialogs/groupnicknamedialog.h"
#include "dialogs/memberprofilecard.h"
#include "dialogs/mutedurationdialog.h"
#include "views/messagesview.h"
#include "widgets/composerwidget.h"
#include "widgets/groupmembersidebar.h"

#include <QDateTime>
#include <QMessageBox>
#include <QStandardItem>
#include <QStatusBar>
#include <QTextEdit>

namespace {
QString serverGroupAuditActionText(const QString& action) {
    const QString normalized = action.trimmed().toLower();
    if (normalized == QLatin1String("announcement_update")) return QStringLiteral("更新公告");
    if (normalized == QLatin1String("add")) return QStringLiteral("加入成员");
    if (normalized == QLatin1String("remove")) return QStringLiteral("移出成员");
    if (normalized == QLatin1String("promote_admin")) return QStringLiteral("设为管理员");
    if (normalized == QLatin1String("demote_admin")) return QStringLiteral("取消管理员");
    return normalized.isEmpty() ? QStringLiteral("群操作") : normalized;
}

QString serverGroupAuditSummary(const QJsonObject& event) {
    const QString action = serverGroupAuditActionText(event.value("action").toString());
    const QString actorName = event.value("actorName").toString().trimmed();
    const QString actorId = event.value("actorId").toString().trimmed();
    const QString targetName = event.value("targetUserName").toString().trimmed();
    const QString targetId = event.value("targetUserId").toString().trimmed();
    const QString createdAt = event.value("createdAt").toString().trimmed();
    const QString actor = actorName.isEmpty() ? actorId : QString("%1(%2)").arg(actorName, actorId);
    const QString target = targetId.isEmpty()
        ? QString()
        : (targetName.isEmpty() ? targetId : QString("%1(%2)").arg(targetName, targetId));
    const QString timePart = createdAt.isEmpty() ? QString() : QString(" · %1").arg(createdAt);
    return target.isEmpty()
        ? QString("%1 · %2%3").arg(action, actor, timePart)
        : QString("%1 · %2 -> %3%4").arg(action, actor, target, timePart);
}

} // namespace

void MainWindow::refreshGroupMemberPanel() {
    // Keep the visible QQNT sidebar in sync with every legacy-panel refresh
    // (member snapshots, mute/role changes, kicks). The sidebar computes its own
    // data and does not call back here, so this cannot recurse.
    refreshGroupMemberSidebar();
    if (!ui->groupMemberListView || !m_groupMemberModel) return;
    QString filter = ui->memberSearchEdit ? ui->memberSearchEdit->text().trimmed() : QString();
    m_groupMemberModel->clear();
    m_groupMemberModel->setHorizontalHeaderLabels({"群成员"});

    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        QStringList members = m_localGroupMembers.value(m_privateChatTarget);
        if (members.isEmpty()) members << m_currentUserId;
        const QString ownerId = members.first();
        const QString ownerName = ownerId == m_currentUserId ? m_currentUserName : contactDisplayName(ownerId);
        int visibleMembers = 0;
        int onlineMembers = 0;
        int friendMembers = 0;
        int pendingMembers = 0;
        for (const QString& memberId : members) {
            QString name = memberId == m_currentUserId ? m_currentUserName : m_friendNames.value(memberId, memberId);
            bool online = isContactOnline(memberId) || memberId == m_currentUserId;
            bool isFriend = m_friendIds.contains(memberId);
            bool isPending = !isFriend && m_pendingOutgoingFriendRequests.contains(memberId);
            if (online) ++onlineMembers;
            if (isFriend) ++friendMembers;
            if (isPending) ++pendingMembers;
            if (!filter.isEmpty()
                && !memberId.contains(filter, Qt::CaseInsensitive)
                && !name.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            const GroupMemberDisplayState display = m_groupManager.memberDisplayState(
                memberId, m_currentUserId, ownerId, QString(), isFriend, isPending, online, false);
            QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n%3 · %4 · %5")
                .arg(display.role, memberId, name, display.state, display.actionText));
            item->setData(memberId, Qt::UserRole + 1);
            item->setEditable(false);
            item->setForeground(memberId == m_currentUserId ? QColor(18, 150, 247) : (isFriend ? QColor(20, 92, 160) : (isPending ? QColor(170, 110, 20) : QColor(38, 50, 56))));
            m_groupMemberModel->appendRow(item);
            ++visibleMembers;
        }
        QString pendingPart = pendingMembers > 0 ? QString(" · 申请中%1").arg(pendingMembers) : QString();
        ui->memberTitleLabel->setText(QString("群聊成员 %1 · 群主:%2 · 在线%3 · 好友%4%5")
            .arg(members.size())
            .arg(ownerName)
            .arg(onlineMembers)
            .arg(friendMembers)
            .arg(pendingPart));
        if (visibleMembers == 0 && !filter.isEmpty()) {
            QStandardItem* addItem = new QStandardItem(QString("邀请 QQ:%1\n双击自动加入当前群聊").arg(filter));
            addItem->setData("group_invite:" + filter, Qt::UserRole + 1);
            addItem->setEditable(false);
            addItem->setForeground(QColor(18, 150, 247));
            addItem->setEnabled(isCurrentUserGroupOwner(m_privateChatTarget));
            addItem->setToolTip(isCurrentUserGroupOwner(m_privateChatTarget) ? "双击邀请该 QQ 入群" : "只有群主可以邀请新成员入群");
            m_groupMemberModel->appendRow(addItem);
            ui->memberTitleLabel->setText(QString("群聊成员 %1 · 群主:%2 · 在线%3 · 好友%4%5 · %6QQ:%7")
                .arg(members.size())
                .arg(ownerName)
                .arg(onlineMembers)
                .arg(friendMembers)
                .arg(pendingPart)
                .arg(isCurrentUserGroupOwner(m_privateChatTarget) ? "可邀请" : "无权限邀请")
                .arg(filter));
        } else if (!filter.isEmpty()) {
            ui->memberTitleLabel->setText(QString("群聊成员 %1 · 群主:%2 · 在线%3 · 好友%4%5 · 匹配%6")
                .arg(members.size())
                .arg(ownerName)
                .arg(onlineMembers)
                .arg(friendMembers)
                .arg(pendingPart)
                .arg(visibleMembers));
        }
        refreshSessionSummary();
        return;
    }

    const QStringList serverPublicMembers = m_serverGroupMembers.value("public");
    if (isCurrentUserRemovedFromPublicGroup()) {
        const QJsonObject removedInfo = m_removedServerGroups.value("public");
        const QString removedBy = removedInfo["removedByName"].toString(removedInfo["removedBy"].toString());
        const QString removedAt = removedInfo["removedAt"].toString();
        const QString detail = removedBy.isEmpty()
            ? QStringLiteral("只读历史 · 等待重新邀请")
            : QStringLiteral("只读历史 · %1 移出%2")
                .arg(removedBy, removedAt.isEmpty() ? QString() : QStringLiteral(" · %1").arg(removedAt));
        QStandardItem* removedItem = new QStandardItem(QString("已不在公共群 QQ:%1\n%2").arg(m_currentUserId, detail));
        removedItem->setData(m_currentUserId, Qt::UserRole + 1);
        removedItem->setEditable(false);
        removedItem->setEnabled(false);
        removedItem->setForeground(QColor(170, 110, 20));
        removedItem->setToolTip("服务端已移出当前账号；本机聊天历史仍可查看，重新邀请后会自动恢复群成员列表");
        if (filter.isEmpty()
            || m_currentUserId.contains(filter, Qt::CaseInsensitive)
            || QString("等待邀请 只读 历史").contains(filter, Qt::CaseInsensitive)) {
            m_groupMemberModel->appendRow(removedItem);
        } else {
            delete removedItem;
        }
        ui->memberTitleLabel->setText("公共群成员 · 当前账号已被移出 · 历史只读");
        refreshSessionSummary();
        return;
    }

    if (!serverPublicMembers.isEmpty()) {
        const bool canManagePublicGroup = canCurrentUserManageServerGroup("public");
        const QString ownerId = m_serverGroupOwners.value("public");
        const QString ownerName = ownerId == m_currentUserId
            ? m_currentUserName
            : m_serverGroupMemberNames.value("public|" + ownerId, contactDisplayName(ownerId));
        int memberCount = 0;
        int visibleMembers = 0;
        int friendMembers = 0;
        int pendingMembers = 0;
        int onlineMembers = 0;

        for (const QString& memberId : serverPublicMembers) {
            if (memberId.trimmed().isEmpty()) continue;
            const QString name = memberId == m_currentUserId
                ? m_currentUserName
                : m_serverGroupMemberNames.value("public|" + memberId, contactDisplayName(memberId));
            const bool online = memberId == m_currentUserId || isContactOnline(memberId);
            const bool isFriend = m_friendIds.contains(memberId);
            const bool isPending = !isFriend && memberId != m_currentUserId && m_pendingOutgoingFriendRequests.contains(memberId);
            ++memberCount;
            if (online) ++onlineMembers;
            if (isFriend) ++friendMembers;
            if (isPending) ++pendingMembers;
            if (!filter.isEmpty()
                && !memberId.contains(filter, Qt::CaseInsensitive)
                && !name.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }

            const QString serverRole = m_serverGroupMemberRoles.value("public|" + memberId, "member").toLower();
            const GroupMemberDisplayState display = m_groupManager.memberDisplayState(
                memberId, m_currentUserId, ownerId, serverRole, isFriend, isPending, online, true);
            QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n%3 · %4 · %5")
                .arg(display.role, memberId, name, display.state, display.actionText));
            item->setData(memberId, Qt::UserRole + 1);
            item->setEditable(false);
            item->setForeground(display.isOwner ? QColor(156, 98, 0) : (memberId == m_currentUserId ? QColor(18, 150, 247) : (isFriend ? QColor(18, 150, 247) : (isPending ? QColor(170, 110, 20) : QColor(38, 50, 56)))));
            m_groupMemberModel->appendRow(item);
            ++visibleMembers;
        }

        if (visibleMembers == 0 && !filter.isEmpty()) {
            QStandardItem* addItem = new QStandardItem(canManagePublicGroup
                ? QString("邀请 QQ:%1 加入公共群\n双击提交服务端成员变更").arg(filter)
                : QString("搜索并发送申请 QQ:%1\n双击查找好友").arg(filter));
            addItem->setData("group_search_add:" + filter, Qt::UserRole + 1);
            addItem->setEditable(false);
            addItem->setForeground(QColor(18, 150, 247));
            addItem->setToolTip(canManagePublicGroup ? "双击后由服务端校验群主/管理员权限并添加成员" : "双击查找该 QQ 并发送好友申请");
            m_groupMemberModel->appendRow(addItem);
            ui->memberTitleLabel->setText(QString("群聊成员 %1 · 群主:%2 · 在线%3 · %4QQ:%5")
                .arg(memberCount)
                .arg(ownerName.isEmpty() ? "未指定" : ownerName)
                .arg(onlineMembers)
                .arg(canManagePublicGroup ? "可邀请" : "可搜索")
                .arg(filter));
            return;
        }

        QString pendingPart = pendingMembers > 0 ? QString(" · 申请中%1").arg(pendingMembers) : QString();
        int auditVisibleCount = 0;
        const QJsonArray auditEvents = m_serverGroupAuditEvents.value("public");
        if (!auditEvents.isEmpty() && (filter.isEmpty() || QStringLiteral("审计").contains(filter, Qt::CaseInsensitive))) {
            QStandardItem* auditHeader = new QStandardItem(QString("最近群审计 · %1 条\n服务端同步公告、成员和管理员变更").arg(auditEvents.size()));
            auditHeader->setEditable(false);
            auditHeader->setEnabled(false);
            auditHeader->setForeground(QColor(92, 107, 120));
            m_groupMemberModel->appendRow(auditHeader);
            const int start = qMax(0, auditEvents.size() - 3);
            for (int i = start; i < auditEvents.size(); ++i) {
                const QJsonObject event = auditEvents.at(i).toObject();
                QStandardItem* auditItem = new QStandardItem(QString("审计 · %1").arg(serverGroupAuditSummary(event)));
                auditItem->setEditable(false);
                auditItem->setEnabled(false);
                auditItem->setForeground(QColor(92, 107, 120));
                auditItem->setToolTip(QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));
                m_groupMemberModel->appendRow(auditItem);
                ++auditVisibleCount;
            }
        }
        ui->memberTitleLabel->setText(filter.isEmpty()
            ? QString("群聊成员 %1 · 群主:%2 · 在线%3 · 好友%4%5%6")
                .arg(memberCount)
                .arg(ownerName.isEmpty() ? "未指定" : ownerName)
                .arg(onlineMembers)
                .arg(friendMembers)
                .arg(pendingPart)
                .arg(auditEvents.isEmpty() ? QString() : QString(" · 审计%1").arg(auditEvents.size()))
            : QString("群聊成员 %1 · 群主:%2 · 在线%3 · 好友%4%5 · 匹配%6%7")
                .arg(memberCount)
                .arg(ownerName.isEmpty() ? "未指定" : ownerName)
                .arg(onlineMembers)
                .arg(friendMembers)
                .arg(pendingPart)
                .arg(visibleMembers)
                .arg(auditVisibleCount > 0 ? QString(" · 审计%1").arg(auditVisibleCount) : QString()));
        refreshSessionSummary();
        return;
    }

    QStandardItem* selfItem = new QStandardItem(QString("我  QQ:%1\n%2 · 在线").arg(m_currentUserId, m_currentUserName));
    selfItem->setData(m_currentUserId, Qt::UserRole + 1);
    selfItem->setEditable(false);
    selfItem->setForeground(QColor(18, 150, 247));
    if (filter.isEmpty() || m_currentUserId.contains(filter, Qt::CaseInsensitive) || m_currentUserName.contains(filter, Qt::CaseInsensitive)) {
        m_groupMemberModel->appendRow(selfItem);
    } else {
        delete selfItem;
    }

    int memberCount = 1;
    int visibleMembers = 0;
    int friendMembers = 0;
    int pendingMembers = 0;
    int onlineMembers = 1;
    for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
        const ChatUser& user = it.value();
        if (user.id == m_currentUserId) continue;
        ++memberCount;
        const bool online = user.isOnline;
        if (!filter.isEmpty()
            && !user.id.contains(filter, Qt::CaseInsensitive)
            && !user.name.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        bool isFriend = m_friendIds.contains(user.id);
        bool isPending = !isFriend && m_pendingOutgoingFriendRequests.contains(user.id);
        if (isFriend) ++friendMembers;
        if (isPending) ++pendingMembers;
        if (online) ++onlineMembers;
        QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n%3 · %4 · %5").arg(isFriend ? "好友" : (isPending ? "申请中" : "成员"), user.id, user.name, online ? "在线" : "离线", isFriend ? "已是好友" : (isPending ? "等待确认" : "双击发送申请")));
        item->setData(user.id, Qt::UserRole + 1);
        item->setEditable(false);
        item->setForeground(isFriend ? QColor(18, 150, 247) : (isPending ? QColor(170, 110, 20) : QColor(38, 50, 56)));
        m_groupMemberModel->appendRow(item);
        ++visibleMembers;
    }
    if (visibleMembers == 0 && !filter.isEmpty()) {
        QStandardItem* addItem = new QStandardItem(QString("搜索并发送申请 QQ:%1\n双击查找好友").arg(filter));
        addItem->setData("group_search_add:" + filter, Qt::UserRole + 1);
        addItem->setEditable(false);
        addItem->setForeground(QColor(18, 150, 247));
        m_groupMemberModel->appendRow(addItem);
        ui->memberTitleLabel->setText(QString("群聊成员 %1 · 在线%2 · 可搜索QQ:%3").arg(memberCount).arg(onlineMembers).arg(filter));
        return;
    }
    QString pendingPart = pendingMembers > 0 ? QString(" · 申请中%1").arg(pendingMembers) : QString();
    ui->memberTitleLabel->setText(filter.isEmpty()
        ? QString("群聊成员 %1 · 在线%2 · 好友%3%4").arg(memberCount).arg(onlineMembers).arg(friendMembers).arg(pendingPart)
        : QString("群聊成员 %1 · 在线%2 · 好友%3%4 · 匹配%5").arg(memberCount).arg(onlineMembers).arg(friendMembers).arg(pendingPart).arg(visibleMembers));
    refreshSessionSummary();
    // Keep the visible QQNT sidebar in sync with the same data.
    refreshGroupMemberSidebar();
}

void MainWindow::refreshGroupMemberSidebar() {
    // The QQNT sidebar is the visible group-member surface (the legacy
    // ui->groupMemberListView lives in the hidden compatibility widget). Feed it
    // the same local/server group data the legacy panel computes.
    if (!m_messagesView) return;
    GroupMemberSidebar* sidebar = m_messagesView->groupMemberSidebar();
    if (!sidebar) return;

    const QString serverGroupId = m_privateChatTarget.isEmpty()
        ? QStringLiteral("public")
        : m_joinedServerSearchGroups.key(m_privateChatTarget);
    const bool isServerGroup = !serverGroupId.isEmpty();
    const bool isLocalGroup = !isServerGroup && !m_privateChatTarget.isEmpty()
        && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    if (!isLocalGroup && !isServerGroup) {
        // Not a group session: hide the rail.
        m_messagesView->setGroupMemberSidebarVisible(false);
        return;
    }

    QList<GroupMemberDisplayData> members;
    QString ownerId;
    QString groupName;
    QString announcement;

    if (isLocalGroup) {
        groupName = m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊"));
        announcement = m_localGroupAnnouncements.value(m_privateChatTarget);
        QStringList ids = m_localGroupMembers.value(m_privateChatTarget);
        if (ids.isEmpty()) ids << m_currentUserId;
        ownerId = ids.first();
        for (const QString& id : ids) {
            if (id.trimmed().isEmpty()) continue;
            GroupMemberDisplayData d;
            d.id = id;
            d.nickname = id == m_currentUserId ? m_currentUserName : contactDisplayName(id);
            d.role = (id == ownerId) ? QStringLiteral("owner") : QStringLiteral("member");
            d.isOnline = isContactOnline(id) || id == m_currentUserId;
            members.append(d);
        }
    } else {
        groupName = m_serverGroupNames.value(serverGroupId, QStringLiteral("群聊"));
        announcement = m_serverGroupAnnouncements.value(serverGroupId);
        ownerId = m_serverGroupOwners.value(serverGroupId);
        const QStringList ids = m_serverGroupMembers.value(serverGroupId);
        for (const QString& id : ids) {
            if (id.trimmed().isEmpty()) continue;
            GroupMemberDisplayData d;
            d.id = id;
            d.nickname = id == m_currentUserId
                ? m_currentUserName
                : m_serverGroupMemberNames.value(serverGroupId + QStringLiteral("|") + id, contactDisplayName(id));
            const QString serverRole =
                m_serverGroupMemberRoles.value(serverGroupId + QStringLiteral("|") + id, QStringLiteral("member")).toLower();
            if (id == ownerId) {
                d.role = QStringLiteral("owner");
            } else if (serverRole == QStringLiteral("admin")) {
                d.role = QStringLiteral("admin");
            } else {
                d.role = QStringLiteral("member");
            }
            d.isOnline = id == m_currentUserId || isContactOnline(id);
            members.append(d);
        }
        // Server group without a member snapshot yet: fall back to just showing self.
        if (members.isEmpty() && !m_currentUserId.isEmpty()) {
            GroupMemberDisplayData self;
            self.id = m_currentUserId;
            self.nickname = m_currentUserName;
            self.role = (m_currentUserId == ownerId) ? QStringLiteral("owner") : QStringLiteral("member");
            self.isOnline = true;
            members.append(self);
        }
    }

    sidebar->setGroupId(isServerGroup ? serverGroupId : m_privateChatTarget);
    sidebar->setGroupName(groupName);
    sidebar->setAnnouncement(announcement);
    sidebar->setMembers(members);
    sidebar->setManagementEnabled(isLocalGroup ? isCurrentUserGroupOwner(m_privateChatTarget)
                                                : canCurrentUserManageServerGroup(serverGroupId));
    m_messagesView->setGroupMemberSidebarVisible(true);
}

void MainWindow::connectGroupMemberSidebar() {
    if (!m_messagesView) return;
    GroupMemberSidebar* sidebar = m_messagesView->groupMemberSidebar();
    if (!sidebar) return;

    // A member context action only applies to the current group session. These
    // helpers mirror the legacy right-click handlers' permission checks so the
    // sidebar and the (hidden) legacy list stay behaviourally identical.
    auto currentServerGroupId = [this]() {
        return m_privateChatTarget.isEmpty()
            ? QStringLiteral("public")
            : m_joinedServerSearchGroups.key(m_privateChatTarget);
    };
    auto isLocalGroup = [currentServerGroupId, this]() {
        return currentServerGroupId().isEmpty() && !m_privateChatTarget.isEmpty()
            && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    };

    connect(sidebar, &GroupMemberSidebar::chatWithMember, this, [this](const QString& userId) {
        if (userId.isEmpty() || userId == m_currentUserId) return;
        ensureFriendRequestQueued(userId,
                                  QStringLiteral("已向群成员发送好友申请 QQ:%1，等待对方同意"),
                                  true);
        openPrivateSession(userId);
        showMessagesView();
    });
    connect(sidebar, &GroupMemberSidebar::atMember, this, [this](const QString& userId, const QString& displayName) {
        Q_UNUSED(userId);
        if (!m_messagesView || !m_messagesView->composer()) return;
        QTextEdit* input = m_messagesView->composer()->inputEdit();
        if (!input) return;
        input->insertPlainText(QStringLiteral("@%1 ").arg(displayName));
        input->setFocus();
    });
    connect(sidebar, &GroupMemberSidebar::viewProfile, this, [this, currentServerGroupId](const QString& userId) {
        if (userId.isEmpty()) return;
        const QString serverGroupId = currentServerGroupId();
        const bool local = serverGroupId.isEmpty() && !m_privateChatTarget.isEmpty()
            && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
        const QString groupName = local
            ? m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊"))
            : m_serverGroupNames.value(serverGroupId, QStringLiteral("群聊"));
        QString role = QStringLiteral("成员");
        if (userId == groupOwnerId(m_privateChatTarget)
            || userId == m_serverGroupOwners.value(serverGroupId)) {
            role = QStringLiteral("群主");
        } else if (m_serverGroupMemberRoles.value(serverGroupId + QStringLiteral("|") + userId).toLower()
                   == QStringLiteral("admin")) {
            role = QStringLiteral("管理员");
        }
        MemberProfileCard card(this);
        card.setMemberInfo(userId, contactDisplayName(userId), role, groupName,
                           userId == m_currentUserId || isContactOnline(userId));
        connect(&card, &MemberProfileCard::sendMessageRequested, this, [this](const QString& id) {
            ensureFriendRequestQueued(id,
                                      QStringLiteral("已向群成员发送好友申请 QQ:%1，等待对方同意"),
                                      true);
            openPrivateSession(id);
            showMessagesView();
        });
        connect(&card, &MemberProfileCard::addFriendRequested, this, [this](const QString& id) {
            ensureFriendRequestQueued(id,
                                      QStringLiteral("已向群成员发送好友申请 QQ:%1，等待对方同意"),
                                      true);
        });
        card.exec();
    });
    connect(sidebar, &GroupMemberSidebar::addFriend, this, [this](const QString& userId) {
        ensureFriendRequestQueued(userId,
                                  QStringLiteral("已向群成员发送好友申请 QQ:%1，等待对方同意"),
                                  true);
    });
    connect(sidebar, &GroupMemberSidebar::renameMember, this, [this](const QString& userId, const QString& currentName) {
        GroupNicknameDialog dlg(this);
        dlg.setCurrentNickname(currentName.isEmpty() ? contactDisplayName(userId) : currentName);
        connect(&dlg, &GroupNicknameDialog::nicknameConfirmed, this, [this, userId](const QString& nick) {
            const QString remark = nick.trimmed();
            if (remark.isEmpty()) return;
            m_friendNames[userId] = remark;
            if (m_friendIds.contains(userId)) {
                saveFriends();
            }
            refreshFriendList();
            refreshGroupMemberPanel();
            appendSystemMessage(QStringLiteral("已设置 %1 的群昵称为 %2").arg(userId, remark));
        });
        dlg.exec();
    });
    connect(sidebar, &GroupMemberSidebar::muteMember, this, [this, isLocalGroup, currentServerGroupId](const QString& userId, int minutes) {
        if (userId.isEmpty() || userId == m_currentUserId) {
            ui->statusbar->showMessage(QStringLiteral("无法对该成员执行禁言"), 2000);
            return;
        }
        if (isLocalGroup()) {
            ui->statusbar->showMessage(QStringLiteral("本地群聊暂不支持服务端禁言"), 2400);
            return;
        }
        const QString serverGroupId = currentServerGroupId();
        if (serverGroupId.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("禁言仅在群聊会话中可用"), 2000);
            return;
        }
        if (!canCurrentUserManageServerGroup(serverGroupId)) {
            ui->statusbar->showMessage(QStringLiteral("只有群主或管理员可以禁言成员"), 2400);
            return;
        }
        int durationMinutes = minutes;
        if (durationMinutes < 0) {
            // Custom duration: reuse the mute dialog.
            MuteDurationDialog dialog(this);
            if (dialog.exec() != QDialog::Accepted) {
                ui->statusbar->showMessage(QStringLiteral("已取消禁言操作"), 1600);
                return;
            }
            durationMinutes = dialog.isPermanent() ? 30 * 24 * 60 : dialog.durationMinutes();
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 mutedUntil = nowMs + static_cast<qint64>(durationMinutes) * 60 * 1000;
        if (m_client && m_client->sendServerGroupMemberMute(serverGroupId, userId, mutedUntil, QString())) {
            ui->statusbar->showMessage(QStringLiteral("已请求禁言 %1").arg(contactDisplayName(userId)), 2200);
        } else {
            ui->statusbar->showMessage(QStringLiteral("禁言失败：需要有效的服务器连接"), 3000);
        }
    });
    connect(sidebar, &GroupMemberSidebar::unmuteMember, this, [this, currentServerGroupId](const QString& userId) {
        if (userId.isEmpty()) return;
        const QString serverGroupId = currentServerGroupId();
        if (serverGroupId.isEmpty() || !canCurrentUserManageServerGroup(serverGroupId)) {
            ui->statusbar->showMessage(QStringLiteral("只有群主或管理员可以解除禁言"), 2400);
            return;
        }
        // Unmute maps to a mute window that ends now.
        if (m_client && m_client->sendServerGroupMemberMute(serverGroupId, userId,
                                                            QDateTime::currentMSecsSinceEpoch(), QString())) {
            ui->statusbar->showMessage(QStringLiteral("已请求解除禁言 %1").arg(contactDisplayName(userId)), 2200);
        } else {
            ui->statusbar->showMessage(QStringLiteral("解除禁言失败：需要有效的服务器连接"), 3000);
        }
    });
    connect(sidebar, &GroupMemberSidebar::promoteAdmin, this, [this, currentServerGroupId](const QString& userId) {
        if (userId.isEmpty() || userId == m_currentUserId) return;
        const QString serverGroupId = currentServerGroupId();
        if (serverGroupId.isEmpty() || !canCurrentUserManageServerGroup(serverGroupId)) {
            ui->statusbar->showMessage(QStringLiteral("只有群主可以设置管理员"), 2400);
            return;
        }
        requestServerGroupMemberUpdate(userId, QStringLiteral("promote_admin"), serverGroupId);
    });
    connect(sidebar, &GroupMemberSidebar::demoteAdmin, this, [this, currentServerGroupId](const QString& userId) {
        if (userId.isEmpty() || userId == m_currentUserId) return;
        const QString serverGroupId = currentServerGroupId();
        if (serverGroupId.isEmpty() || !canCurrentUserManageServerGroup(serverGroupId)) {
            ui->statusbar->showMessage(QStringLiteral("只有群主可以取消管理员"), 2400);
            return;
        }
        requestServerGroupMemberUpdate(userId, QStringLiteral("demote_admin"), serverGroupId);
    });
    connect(sidebar, &GroupMemberSidebar::kickMember, this, [this, isLocalGroup, currentServerGroupId](const QString& userId) {
        if (userId.isEmpty() || userId == m_currentUserId) return;
        const QString memberName = contactDisplayName(userId);
        if (isLocalGroup()) {
            if (!isCurrentUserGroupOwner(m_privateChatTarget)) {
                ui->statusbar->showMessage(QStringLiteral("只有群主可以移出成员"), 2400);
                return;
            }
            if (userId == groupOwnerId(m_privateChatTarget)) {
                ui->statusbar->showMessage(QStringLiteral("不能移出群主"), 2200);
                return;
            }
            if (QMessageBox::question(this, QStringLiteral("移出群成员"),
                                      QStringLiteral("确定将“%1”移出当前群聊吗？").arg(memberName),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
                return;
            }
            m_localGroupMembers[m_privateChatTarget].removeAll(userId);
            saveLocalGroups();
            refreshGroupMemberPanel();
            appendSystemMessage(QStringLiteral("已将 %1 移出群聊").arg(memberName));
        } else {
            const QString serverGroupId = currentServerGroupId();
            if (serverGroupId.isEmpty()) return;
            if (!canCurrentUserManageServerGroup(serverGroupId)) {
                ui->statusbar->showMessage(QStringLiteral("只有群主或管理员可以移出成员"), 2400);
                return;
            }
            if (QMessageBox::question(this, QStringLiteral("移出群成员"),
                                      QStringLiteral("确定将“%1”移出当前群聊吗？").arg(memberName),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
                return;
            }
            requestServerGroupMemberUpdate(userId, QStringLiteral("remove"), serverGroupId);
        }
    });
    connect(sidebar, &GroupMemberSidebar::reportMember, this, [this](const QString& userId) {
        ui->statusbar->showMessage(QStringLiteral("已记录对 QQ:%1 的举报（仅本地）").arg(userId), 2400);
    });
    connect(sidebar, &GroupMemberSidebar::blockMember, this, [this](const QString& userId) {
        ui->statusbar->showMessage(QStringLiteral("已屏蔽 QQ:%1 的发言（仅本地）").arg(userId), 2400);
    });
}
