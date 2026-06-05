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
