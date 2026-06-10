#include "groupmanager.h"

GroupNoticeUiState GroupManager::noticeUiState(int localGroupCount) {
    GroupNoticeUiState state;
    state.joinedGroupCount = qMax(1, localGroupCount + 1);
    state.text = localGroupCount <= 0
        ? QStringLiteral("群通知")
        : QStringLiteral("群通知 %1").arg(state.joinedGroupCount);
    state.toolTip = localGroupCount <= 0
        ? QStringLiteral("查看公共聊天室、群公告和入群邀请")
        : QStringLiteral("已加入 %1 个群聊（含公共聊天室），可查看公告和入群邀请").arg(state.joinedGroupCount);
    return state;
}

QString GroupManager::localGroupOwnerId(const QStringList& members, const QString& currentUserId) {
    for (const QString& memberId : members) {
        const QString trimmed = memberId.trimmed();
        if (!trimmed.isEmpty()) {
            return trimmed;
        }
    }
    return currentUserId;
}

bool GroupManager::isLocalGroupOwner(const QString& groupId,
                                     const QStringList& members,
                                     const QString& currentUserId) {
    return !groupId.isEmpty()
        && groupId.startsWith(QStringLiteral("local_group_"))
        && localGroupOwnerId(members, currentUserId) == currentUserId;
}

bool GroupManager::canManageServerGroup(const QString& groupId,
                                        const QString& currentUserId,
                                        const QMap<QString, QString>& serverGroupOwners,
                                        const QMap<QString, QString>& serverGroupMemberRoles) {
    if (groupId.isEmpty() || currentUserId.isEmpty()) {
        return false;
    }
    const QString role = serverGroupMemberRoles.value(groupId + QStringLiteral("|") + currentUserId).toLower();
    return serverGroupOwners.value(groupId) == currentUserId
        || role == QLatin1String("owner")
        || role == QLatin1String("admin");
}

GroupMemberDisplayState GroupManager::memberDisplayState(const QString& memberId,
                                                         const QString& currentUserId,
                                                         const QString& ownerId,
                                                         const QString& serverRole,
                                                         bool isFriend,
                                                         bool isPending,
                                                         bool online,
                                                         bool serverGroup) {
    GroupMemberDisplayState state;
    state.state = online ? QStringLiteral("在线") : QStringLiteral("离线");
    const bool self = memberId == currentUserId;
    state.isOwner = serverRole.toLower() == QLatin1String("owner")
        || (!ownerId.isEmpty() && memberId == ownerId);
    state.isAdmin = serverGroup && serverRole.toLower() == QLatin1String("admin");

    if (state.isOwner) {
        state.role = self ? QStringLiteral("群主/我") : QStringLiteral("群主");
    } else if (state.isAdmin) {
        state.role = self ? QStringLiteral("管理员/我") : QStringLiteral("管理员");
    } else if (self) {
        state.role = QStringLiteral("我");
    } else if (isFriend) {
        state.role = QStringLiteral("好友");
    } else if (isPending) {
        state.role = QStringLiteral("申请中");
    } else {
        state.role = serverGroup ? QStringLiteral("成员") : QStringLiteral("群成员");
    }

    if (self) {
        state.actionText = QStringLiteral("本人");
    } else if (isFriend) {
        state.actionText = QStringLiteral("已是好友");
    } else if (isPending) {
        state.actionText = QStringLiteral("等待确认");
    } else {
        state.actionText = serverGroup ? QStringLiteral("双击发送申请") : QStringLiteral("可发送申请");
    }
    return state;
}

ServerGroupMemberUpdateDecision GroupManager::serverGroupMemberUpdateDecision(
    const QString& memberId,
    const QString& action,
    bool clientConnected,
    const QString& groupId,
    const QString& currentUserId,
    const QStringList& members,
    const QMap<QString, QString>& serverGroupOwners,
    const QMap<QString, QString>& serverGroupMemberRoles) {
    ServerGroupMemberUpdateDecision decision;
    decision.targetId = memberId.trimmed();
    decision.normalizedAction = action.trimmed().toLower();
    decision.roleAction = decision.normalizedAction == QLatin1String("promote_admin")
        || decision.normalizedAction == QLatin1String("demote_admin");

    if (decision.targetId.isEmpty()
            || (decision.normalizedAction != QLatin1String("add")
                && decision.normalizedAction != QLatin1String("remove")
                && !decision.roleAction)) {
        decision.statusMessage = QStringLiteral("公共群成员变更参数无效");
        decision.statusTimeoutMs = 2200;
        return decision;
    }
    if (!clientConnected) {
        decision.statusMessage = QStringLiteral("公共群成员变更失败：当前未连接服务器");
        decision.statusTimeoutMs = 2600;
        return decision;
    }
    if (!canManageServerGroup(groupId, currentUserId, serverGroupOwners, serverGroupMemberRoles)) {
        decision.statusMessage = QStringLiteral("只有群主或管理员可以管理公共群成员");
        decision.auditMessage = QStringLiteral("公共群成员变更被权限保护拦截：当前账号不是群主或管理员");
        decision.statusTimeoutMs = 2600;
        return decision;
    }

    const QString currentUserRole = serverGroupMemberRoles.value(groupId + QStringLiteral("|") + currentUserId).toLower();
    const QString targetRole = serverGroupMemberRoles.value(groupId + QStringLiteral("|") + decision.targetId).toLower();
    const bool currentUserIsOwner = serverGroupOwners.value(groupId) == currentUserId
        || currentUserRole == QLatin1String("owner");
    if (decision.roleAction && !currentUserIsOwner) {
        decision.statusMessage = QStringLiteral("只有群主可以设置或取消公共群管理员");
        decision.auditMessage = QStringLiteral("公共群管理员变更被权限保护拦截：当前账号不是群主");
        decision.statusTimeoutMs = 2600;
        return decision;
    }
    if (decision.normalizedAction == QLatin1String("add") && members.contains(decision.targetId)) {
        decision.statusMessage = QStringLiteral("该 QQ 已在公共群中");
        decision.statusTimeoutMs = 1800;
        return decision;
    }
    if (decision.normalizedAction == QLatin1String("remove") || decision.roleAction) {
        if (!members.contains(decision.targetId)) {
            decision.statusMessage = QStringLiteral("该 QQ 不在公共群中");
            decision.statusTimeoutMs = 1800;
            return decision;
        }
        if (decision.targetId == currentUserId) {
            decision.statusMessage = QStringLiteral("不能通过管理操作移出自己");
            decision.statusTimeoutMs = 2200;
            return decision;
        }
        if (decision.targetId == serverGroupOwners.value(groupId) || targetRole == QLatin1String("owner")) {
            decision.statusMessage = QStringLiteral("群主不能被移出公共群");
            decision.statusTimeoutMs = 2200;
            return decision;
        }
    }

    decision.allowed = true;
    return decision;
}
