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

FriendManagerSelectionPreviewUiState FriendManager::managerSelectionPreviewUiState(const QString& entryId,
                                                                                   const QString& displayName,
                                                                                   bool online,
                                                                                   bool canInviteCurrentGroup) {
    FriendManagerSelectionPreviewUiState state;
    state.text = managerSelectionPreviewText(entryId, displayName, online, canInviteCurrentGroup);
    return state;
}

FriendManagerContactCopyState FriendManager::managerContactCopyState(
    const QList<FriendManagerContactCopyInput>& contacts,
    bool onlineOnly) {
    FriendManagerContactCopyState state;
    for (const FriendManagerContactCopyInput& contact : contacts) {
        const QString userId = contact.userId.trimmed();
        if (userId.isEmpty()) {
            continue;
        }
        if (onlineOnly && !contact.online) {
            continue;
        }
        const QString displayName = contact.displayName.trimmed().isEmpty()
            ? userId
            : contact.displayName.trimmed();
        if (onlineOnly) {
            state.rows << QStringLiteral("在线好友 QQ:%1 昵称:%2")
                .arg(userId, displayName);
        } else {
            state.rows << QStringLiteral("QQ:%1 昵称:%2 状态:%3")
                .arg(userId,
                     displayName,
                     contact.online ? QStringLiteral("在线") : QStringLiteral("离线"));
        }
    }
    state.emptyStatusMessage = onlineOnly
        ? QStringLiteral("当前筛选没有在线好友")
        : QStringLiteral("当前筛选没有可复制好友");
    state.copiedStatusMessage = onlineOnly
        ? QStringLiteral("已复制 %1 个在线好友").arg(state.rows.size())
        : QStringLiteral("已复制 %1 个可见好友").arg(state.rows.size());
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

FriendQuickAddSuggestionUiState FriendManager::quickAddSuggestionUiState(
    const QString& currentUserId,
    const QString& currentUserName,
    const QStringList& friendIds,
    const QStringList& pendingOutgoingFriendRequests,
    const QMap<QString, ChatUser>& knownUsers,
    const QMap<QString, QString>& friendNames,
    const QString& filter,
    int maxVisible) {
    FriendQuickAddSuggestionUiState state;
    const QString trimmedFilter = filter.trimmed();
    const int visibleLimit = qMax(0, maxVisible);
    QString firstPreviewId;
    QString firstPreviewName;

    for (auto it = knownUsers.constBegin(); it != knownUsers.constEnd(); ++it) {
        const ChatUser& user = it.value();
        if (user.id == currentUserId || friendIds.contains(user.id)) {
            continue;
        }
        const bool matches = matchesFilter(user.id, user.name, trimmedFilter);
        if (pendingOutgoingFriendRequests.contains(user.id)) {
            if (matches) {
                ++state.pendingCandidates;
            }
            continue;
        }

        ++state.onlineCandidates;
        if (!matches) {
            continue;
        }

        if (state.visibleCount < visibleLimit) {
            FriendQuickAddSuggestionEntryUiState entry;
            entry.entryId = user.id;
            entry.text = QStringLiteral("QQ:%1 · %2 · 在线 · 双击添加").arg(user.id, user.name);
            state.entries.append(entry);
        }
        if (firstPreviewId.isEmpty()) {
            firstPreviewId = user.id;
            firstPreviewName = user.name;
        }
        ++state.visibleCount;
    }

    state.statsText = trimmedFilter.isEmpty()
        ? QStringLiteral("在线推荐 %1 人 · 已有好友 %2 人").arg(state.onlineCandidates).arg(friendIds.size())
        : QStringLiteral("匹配推荐 %1 人 · 输入回车可搜索 QQ:%2").arg(state.visibleCount).arg(trimmedFilter);
    if (state.pendingCandidates > 0) {
        state.statsText += QStringLiteral(" · 申请中 %1 人").arg(state.pendingCandidates);
    }

    if (state.visibleCount > visibleLimit) {
        FriendQuickAddSuggestionEntryUiState moreEntry;
        moreEntry.text = QStringLiteral("还有 %1 位匹配用户，可缩小关键词继续筛选").arg(state.visibleCount - visibleLimit);
        moreEntry.placeholder = true;
        moreEntry.muted = true;
        moreEntry.enabled = false;
        state.entries.append(moreEntry);
    }

    if (state.entries.isEmpty()) {
        FriendQuickAddSuggestionEntryUiState placeholder;
        placeholder.entryId = trimmedFilter;
        placeholder.text = trimmedFilter.isEmpty()
            ? QStringLiteral("输入 QQ 号后回车搜索申请")
            : QStringLiteral("回车搜索并发送申请 QQ:%1").arg(trimmedFilter);
        placeholder.placeholder = true;
        placeholder.muted = true;
        placeholder.enabled = !placeholder.entryId.isEmpty();
        state.entries.append(placeholder);
    }

    const QString previewId = firstPreviewId.isEmpty() ? trimmedFilter : firstPreviewId;
    const QString previewName = firstPreviewName.isEmpty()
        ? (previewId.isEmpty()
            ? QStringLiteral("待搜索好友")
            : contactDisplayName(previewId, knownUsers, friendNames))
        : firstPreviewName;
    state.previewText = QStringLiteral("邀请预览：%1（QQ:%2）\n你好，我是 %3（QQ:%4），方便加个好友吗？")
        .arg(previewName,
             previewId.isEmpty() ? QStringLiteral("-") : previewId,
             currentUserName,
             currentUserId);
    return state;
}
