#include "friendmanager.h"

QString FriendManager::contactDisplayName(const QString& userId,
                                          const QMap<QString, ChatUser>& knownUsers,
                                          const QMap<QString, QString>& friendNames) {
    const auto knownIt = knownUsers.constFind(userId);
    if (knownIt != knownUsers.constEnd() && !knownIt.value().name.trimmed().isEmpty()) {
        return knownIt.value().name;
    }
    return friendNames.value(userId, userId);
}

bool FriendManager::isContactOnline(const QString& userId,
                                    const QMap<QString, ChatUser>& knownUsers) {
    const auto knownIt = knownUsers.constFind(userId);
    return knownIt != knownUsers.constEnd() && knownIt.value().isOnline;
}

FriendNoticeUiState FriendManager::noticeUiState(int pendingIncomingCount) {
    FriendNoticeUiState state;
    state.pendingCount = qMax(0, pendingIncomingCount);
    state.hasPending = state.pendingCount > 0;
    state.text = state.hasPending
        ? QStringLiteral("好友通知 %1").arg(state.pendingCount)
        : QStringLiteral("好友通知");
    state.toolTip = state.hasPending
        ? QStringLiteral("有 %1 个好友申请待处理").arg(state.pendingCount)
        : QStringLiteral("查看并处理好友申请");
    return state;
}

bool FriendManager::matchesFilter(const QString& id, const QString& name, const QString& filter) {
    const QString trimmed = filter.trimmed();
    return trimmed.isEmpty()
        || id.contains(trimmed, Qt::CaseInsensitive)
        || name.contains(trimmed, Qt::CaseInsensitive);
}

QString FriendManager::relationLabel(const QString& userId,
                                     const QString& currentUserId,
                                     const QStringList& friendIds,
                                     const QStringList& pendingOutgoingFriendRequests,
                                     const QMap<QString, ChatUser>& knownUsers) {
    if (userId.isEmpty()) {
        return QStringLiteral("未知");
    }
    if (userId == currentUserId) {
        return QStringLiteral("我");
    }
    if (friendIds.contains(userId)) {
        return isContactOnline(userId, knownUsers) ? QStringLiteral("好友在线") : QStringLiteral("好友离线");
    }
    if (pendingOutgoingFriendRequests.contains(userId)) {
        return QStringLiteral("申请中");
    }
    return isContactOnline(userId, knownUsers) ? QStringLiteral("在线成员") : QStringLiteral("可申请");
}
