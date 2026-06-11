#include "friendmanager.h"

namespace {
const QString kSearchAddPrefix = QStringLiteral("search_add:");
}

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

FriendManagerListUiState FriendManager::managerListUiState(
    const QString& currentUserId,
    const QStringList& friendIds,
    const QStringList& localGroupIds,
    const QMap<QString, QString>& friendNames,
    const QMap<QString, ChatUser>& knownUsers,
    const QString& filter) {
    FriendManagerListUiState state;
    const QString trimmedFilter = filter.trimmed();

    for (const QString& id : friendIds) {
        const QString name = friendNames.value(id, id);
        const bool online = isContactOnline(id, knownUsers);
        if (online) {
            ++state.onlineCount;
        } else {
            ++state.offlineCount;
        }
        if (!matchesFilter(id, name, trimmedFilter)) {
            continue;
        }
        ++state.visibleCount;
    }

    state.subTitle = QStringLiteral("当前 QQ：%1 · 好友 %2 人 · 可见 %3 人")
                         .arg(currentUserId)
                         .arg(friendIds.size())
                         .arg(state.visibleCount);
    state.statsText = QStringLiteral("在线 %1 · 离线 %2 · 本地群 %3")
                          .arg(state.onlineCount)
                          .arg(state.offlineCount)
                          .arg(localGroupIds.size());
    if (state.visibleCount == 0) {
        state.emptyText = trimmedFilter.isEmpty()
            ? QStringLiteral("暂无好友，点击下方发送好友申请")
            : QStringLiteral("未找到好友，双击搜索并发送申请 QQ:%1").arg(trimmedFilter);
        state.emptyEntryId = trimmedFilter.isEmpty() ? QString() : kSearchAddPrefix + trimmedFilter;
    }
    return state;
}

FriendManagerListRenderUiState FriendManager::managerListRenderUiState(
    const QString& currentUserId,
    const QStringList& friendIds,
    const QStringList& localGroupIds,
    const QMap<QString, QString>& friendNames,
    const QMap<QString, ChatUser>& knownUsers,
    const QString& filter) {
    FriendManagerListRenderUiState state;
    state.summary = managerListUiState(currentUserId, friendIds, localGroupIds, friendNames, knownUsers, filter);

    for (const QString& id : friendIds) {
        const QString name = friendNames.value(id, id);
        const bool online = isContactOnline(id, knownUsers);
        if (!matchesFilter(id, name, filter)) {
            continue;
        }

        FriendManagerListEntryUiState entry;
        entry.entryId = id;
        entry.text = QStringLiteral("QQ:%1\n%2 · %3").arg(id, name, online ? QStringLiteral("在线") : QStringLiteral("离线"));
        state.entries.append(entry);
    }

    if (state.entries.isEmpty()) {
        FriendManagerListEntryUiState placeholder;
        placeholder.entryId = state.summary.emptyEntryId;
        placeholder.text = state.summary.emptyText;
        placeholder.placeholder = true;
        placeholder.muted = true;
        state.entries.append(placeholder);
    }

    return state;
}

QString FriendManager::managerSelectionPreviewText(const QString& entryId,
                                                   const QString& displayName,
                                                   bool online,
                                                   bool canInviteCurrentGroup) {
    if (entryId.isEmpty()) {
        return QStringLiteral("选择好友后可复制名片、邀请语或邀入群");
    }
    if (entryId.startsWith(kSearchAddPrefix)) {
        return QStringLiteral("未找到好友，可搜索并发送申请 QQ:%1")
            .arg(entryId.mid(kSearchAddPrefix.size()));
    }
    return QStringLiteral("%1 · QQ:%2 · %3 · %4")
        .arg(displayName,
             entryId,
             online ? QStringLiteral("在线") : QStringLiteral("离线"),
             canInviteCurrentGroup ? QStringLiteral("可邀入当前群") : QStringLiteral("可发起私聊"));
}
