#include "friendmanager.h"

namespace {
const QString kSearchAddPrefix = QStringLiteral("search_add:");

QString globalSearchTargetDisplayName(const GlobalSearchResultCopyInput& result,
                                      const QString& fallbackKeyword) {
    if (result.localGroup || result.entryId.startsWith(QStringLiteral("local_group_"))) {
        return result.displayName.trimmed().isEmpty()
            ? QStringLiteral("群聊")
            : result.displayName.trimmed();
    }
    if (!result.displayName.trimmed().isEmpty()) {
        return result.displayName.trimmed();
    }
    if (!result.entryId.trimmed().isEmpty()) {
        return result.entryId.trimmed();
    }
    return fallbackKeyword.trimmed();
}

QString globalSearchTargetId(const GlobalSearchResultCopyInput& result,
                             const QString& fallbackKeyword) {
    QString entryId = result.entryId.trimmed();
    if (entryId.startsWith(kSearchAddPrefix)) {
        entryId = entryId.mid(kSearchAddPrefix.size());
    }
    if (entryId.startsWith(QStringLiteral("local_group_"))) {
        entryId = entryId.mid(QStringLiteral("local_group_").size());
    }
    if (!entryId.isEmpty()) {
        return entryId;
    }
    return fallbackKeyword.trimmed();
}
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

GlobalSearchResultCopyState FriendManager::globalSearchResultCopyState(
    const QList<GlobalSearchResultCopyInput>& results,
    bool onlineOnly) {
    GlobalSearchResultCopyState state;
    for (const GlobalSearchResultCopyInput& result : results) {
        const QString entryId = result.entryId.trimmed();
        if (entryId.isEmpty()) {
            continue;
        }
        if (entryId.startsWith(kSearchAddPrefix)) {
            if (!onlineOnly) {
                state.rows << QStringLiteral("搜索申请 QQ:%1")
                    .arg(entryId.mid(kSearchAddPrefix.size()));
            }
            continue;
        }
        if (result.localGroup || entryId.startsWith(QStringLiteral("local_group_"))) {
            if (!onlineOnly) {
                const QString groupNumber = entryId.startsWith(QStringLiteral("local_group_"))
                    ? entryId.mid(QStringLiteral("local_group_").size())
                    : entryId;
                const QString groupName = result.displayName.trimmed().isEmpty()
                    ? QStringLiteral("群聊")
                    : result.displayName.trimmed();
                state.rows << QStringLiteral("群聊 QQ:%1 名称:%2")
                    .arg(groupNumber, groupName);
            }
            continue;
        }
        if (onlineOnly && !result.online) {
            continue;
        }
        const QString displayName = result.displayName.trimmed().isEmpty()
            ? entryId
            : result.displayName.trimmed();
        if (onlineOnly) {
            state.rows << QStringLiteral("在线搜索结果 QQ:%1 昵称:%2 关系:%3")
                .arg(entryId,
                     displayName,
                     result.friendContact ? QStringLiteral("好友") : QStringLiteral("可申请"));
        } else {
            state.rows << QStringLiteral("QQ:%1 昵称:%2 状态:%3")
                .arg(entryId,
                     displayName,
                     result.online ? QStringLiteral("在线") : QStringLiteral("离线"));
        }
    }
    state.emptyStatusMessage = onlineOnly
        ? QStringLiteral("当前搜索结果没有在线用户")
        : QStringLiteral("当前搜索结果没有可复制条目");
    state.copiedStatusMessage = onlineOnly
        ? QStringLiteral("已复制 %1 个在线搜索结果").arg(state.rows.size())
        : QStringLiteral("已复制 %1 条搜索结果").arg(state.rows.size());
    return state;
}

QString FriendManager::globalSearchInviteText(const QString& currentUserId,
                                              const QString& currentUserName,
                                              const GlobalSearchResultCopyInput& selectedResult,
                                              const QString& fallbackKeyword) {
    const QString entryId = selectedResult.entryId.trimmed();
    const QString targetId = globalSearchTargetId(selectedResult, fallbackKeyword);
    const QString targetName = globalSearchTargetDisplayName(selectedResult, fallbackKeyword);
    if (entryId.startsWith(QStringLiteral("local_group_")) || selectedResult.localGroup) {
        return QStringLiteral("我想邀请你加入群聊 %1，一起在群里沟通。").arg(targetName);
    }
    return QStringLiteral("你好，我是 %1（QQ:%2），通过 QQ 搜索找到你，方便加个好友吗？")
        .arg(currentUserName, currentUserId.isEmpty() ? targetId : currentUserId);
}

GlobalSearchSelectionCopyState FriendManager::globalSearchInviteCardState(
    const QString& currentUserId,
    const QString& currentUserName,
    const GlobalSearchResultCopyInput& selectedResult,
    const QString& fallbackKeyword) {
    GlobalSearchSelectionCopyState state;
    const QString entryId = selectedResult.entryId.trimmed();
    const QString targetId = globalSearchTargetId(selectedResult, fallbackKeyword);
    if (targetId.isEmpty()) {
        return state;
    }

    QString title;
    QString detail;
    if (entryId.startsWith(QStringLiteral("local_group_")) || selectedResult.localGroup) {
        title = QStringLiteral("邀请加入群聊：%1")
            .arg(globalSearchTargetDisplayName(selectedResult, fallbackKeyword));
        detail = QStringLiteral("群号:%1 · 成员:%2 · 邀请人:%3(QQ:%4)")
            .arg(targetId,
                 QString::number(qMax(0, selectedResult.memberCount)),
                 currentUserName,
                 currentUserId);
    } else {
        title = QStringLiteral("好友邀请：%1")
            .arg(globalSearchTargetDisplayName(selectedResult, fallbackKeyword).isEmpty()
                     ? QStringLiteral("QQ搜索")
                     : globalSearchTargetDisplayName(selectedResult, fallbackKeyword));
        detail = QStringLiteral("目标QQ:%1 · 我的QQ:%2 · 昵称:%3 · 可搜索后直接添加")
            .arg(targetId, currentUserId, currentUserName);
    }
    state.text = title + QChar('\n') + detail;
    state.valid = true;
    return state;
}

GlobalSearchSelectionCopyState FriendManager::globalSearchSummaryCardState(
    const QString& currentUserId,
    const QString& currentUserName,
    const QList<GlobalSearchResultCopyInput>& results,
    const QString& keyword) {
    GlobalSearchSelectionCopyState state;
    QStringList rows;
    rows << QStringLiteral("综合搜索卡片");
    rows << QStringLiteral("关键词:%1").arg(keyword.trimmed().isEmpty() ? QStringLiteral("全部") : keyword.trimmed());
    rows << QStringLiteral("我的QQ:%1 · 昵称:%2").arg(currentUserId, currentUserName);

    int userCount = 0;
    int friendCount = 0;
    int groupCount = 0;
    for (const GlobalSearchResultCopyInput& result : results) {
        const QString entryId = result.entryId.trimmed();
        if (entryId.isEmpty()) {
            continue;
        }
        if (entryId.startsWith(kSearchAddPrefix)) {
            rows << QStringLiteral("继续搜索申请 QQ:%1").arg(entryId.mid(kSearchAddPrefix.size()));
        } else if (result.localGroup || entryId.startsWith(QStringLiteral("local_group_"))) {
            ++groupCount;
            rows << QStringLiteral("群聊 QQ:%1 名称:%2 成员:%3")
                .arg(globalSearchTargetId(result, keyword),
                     globalSearchTargetDisplayName(result, keyword),
                     QString::number(qMax(0, result.memberCount)));
        } else {
            const bool isFriend = result.friendContact;
            if (isFriend) {
                ++friendCount;
            } else {
                ++userCount;
            }
            rows << QStringLiteral("%1 QQ:%2 昵称:%3 状态:%4")
                .arg(isFriend ? QStringLiteral("好友") : QStringLiteral("用户"),
                     entryId,
                     globalSearchTargetDisplayName(result, keyword),
                     result.online ? QStringLiteral("在线") : QStringLiteral("离线"));
        }
    }
    rows << QStringLiteral("匹配好友:%1 · 可申请用户:%2 · 群聊:%3")
        .arg(friendCount)
        .arg(userCount)
        .arg(groupCount);
    state.text = rows.join(QChar('\n'));
    state.valid = !rows.isEmpty();
    return state;
}

GlobalSearchSelectionCopyState FriendManager::globalSearchMediaPackState(
    const QString& currentUserId,
    const QString& currentUserName,
    const GlobalSearchResultCopyInput& selectedResult,
    const QString& keyword) {
    GlobalSearchSelectionCopyState state;
    const QString trimmedKeyword = keyword.trimmed();
    const QString entryId = selectedResult.entryId.trimmed();
    QString targetName = trimmedKeyword.isEmpty() ? QStringLiteral("全部搜索结果") : trimmedKeyword;
    QString targetId = globalSearchTargetId(selectedResult, keyword);
    QString relation = QStringLiteral("搜索结果");

    if (entryId.startsWith(QStringLiteral("local_group_")) || selectedResult.localGroup) {
        targetName = globalSearchTargetDisplayName(selectedResult, keyword);
        relation = QStringLiteral("群聊");
    } else if (!targetId.isEmpty()) {
        targetName = globalSearchTargetDisplayName(selectedResult, keyword);
        relation = selectedResult.friendContact ? QStringLiteral("好友") : QStringLiteral("可申请用户");
    }

    QStringList rows;
    rows << QStringLiteral("综合搜索媒体包 · 目标:%1 · QQ:%2 · 类型:%3")
        .arg(targetName,
             targetId.isEmpty() ? QStringLiteral("批量搜索") : targetId,
             relation);
    rows << QStringLiteral("关键词:%1 · 我的QQ:%2 · 昵称:%3")
        .arg(trimmedKeyword.isEmpty() ? QStringLiteral("全部") : trimmedKeyword,
             currentUserId,
             currentUserName);
    rows << QStringLiteral("可先打开/申请搜索结果，再发送图片/视频或闪传文件");
    rows << QStringLiteral("支持 png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm 和常用文档压缩包");
    rows << QStringLiteral("邀请话术：你好，我是 %1（QQ:%2），通过综合搜索找到你，可以通过好友申请或进群后收发媒体文件。")
        .arg(currentUserName, currentUserId);
    rows << QStringLiteral("查收话术：我已准备发送媒体文件到 %1，请注意查收。").arg(targetName);
    state.text = rows.join(QChar('\n'));
    state.valid = true;
    return state;
}

GlobalSearchSelectionCopyState FriendManager::globalSearchBatchMediaPlanState(
    const QString& currentUserId,
    const QString& currentUserName,
    const QList<GlobalSearchResultCopyInput>& results,
    const QString& keyword) {
    GlobalSearchSelectionCopyState state;
    QStringList users;
    QStringList groups;
    int friendCount = 0;
    int addableCount = 0;

    for (const GlobalSearchResultCopyInput& result : results) {
        const QString entryId = result.entryId.trimmed();
        if (entryId.isEmpty()) {
            continue;
        }
        if (entryId.startsWith(kSearchAddPrefix)) {
            users << QStringLiteral("待搜索QQ:%1").arg(entryId.mid(kSearchAddPrefix.size()));
        } else if (result.localGroup || entryId.startsWith(QStringLiteral("local_group_"))) {
            groups << QStringLiteral("%1(群号:%2,成员:%3)")
                .arg(globalSearchTargetDisplayName(result, keyword),
                     globalSearchTargetId(result, keyword),
                     QString::number(qMax(0, result.memberCount)));
        } else {
            const bool isFriend = result.friendContact;
            if (isFriend) {
                ++friendCount;
            } else {
                ++addableCount;
            }
            users << QStringLiteral("%1(QQ:%2,%3,%4)")
                .arg(globalSearchTargetDisplayName(result, keyword),
                     entryId,
                     isFriend ? QStringLiteral("好友") : QStringLiteral("可申请"),
                     result.online ? QStringLiteral("在线") : QStringLiteral("离线"));
        }
    }

    QStringList rows;
    rows << QStringLiteral("综合搜索批量媒体计划 · 关键词:%1")
        .arg(keyword.trimmed().isEmpty() ? QStringLiteral("全部") : keyword.trimmed());
    rows << QStringLiteral("我的QQ:%1 · 昵称:%2 · 好友结果:%3 · 可申请:%4 · 群聊:%5")
        .arg(currentUserId,
             currentUserName,
             QString::number(friendCount),
             QString::number(addableCount),
             QString::number(groups.size()));
    rows << QStringLiteral("用户目标:%1").arg(users.isEmpty() ? QStringLiteral("无") : users.join(QStringLiteral("、")));
    rows << QStringLiteral("群聊目标:%1").arg(groups.isEmpty() ? QStringLiteral("无") : groups.join(QStringLiteral("、")));
    rows << QStringLiteral("1. 先打开好友或群聊结果，未成为好友先发送申请或复制申请话术");
    rows << QStringLiteral("2. 图片/GIF/视频走图片视频入口，文档和压缩包走闪传文件");
    rows << QStringLiteral("3. 发送后右键聊天记录复制媒体流程、查收话术、回执和保存路径");
    rows << QStringLiteral("4. 可见用户建群后可统一发送群媒体文件");
    state.text = rows.join(QChar('\n'));
    state.valid = true;
    return state;
}

QString FriendManager::globalSearchMediaGuideText(const QString& currentUserId,
                                                  const QString& currentUserName,
                                                  const QString& currentChatDisplayName) {
    QStringList rows;
    rows << QStringLiteral("上传指南 · 我的QQ:%1 · 昵称:%2").arg(currentUserId, currentUserName);
    rows << QStringLiteral("图片/视频：支持 png、jpg、gif、mp4、mov、avi、mkv、wmv、flv、webm");
    rows << QStringLiteral("闪传文件：支持文档、压缩包和媒体文件");
    rows << QStringLiteral("聊天记录右键：可复制媒体卡片和查收话术");
    rows << QStringLiteral("当前会话:%1").arg(currentChatDisplayName.trimmed().isEmpty()
                                                 ? QStringLiteral("公共聊天室")
                                                 : currentChatDisplayName.trimmed());
    return rows.join(QChar('\n'));
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
