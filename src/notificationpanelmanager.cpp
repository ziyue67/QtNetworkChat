#include "notificationpanelmanager.h"

namespace {
const QString kSearchAddPrefix = QStringLiteral("search_add:");
const QString kGroupCreatePrefix = QStringLiteral("group_create:");
}

FriendNoticeListRenderUiState NotificationPanelManager::friendNoticeListRenderUiState(
    const QStringList& pendingFriendRequests,
    const QMap<QString, QString>& friendNames,
    int friendCount,
    const QString& filter) {
    FriendNoticeListRenderUiState state;
    const QString keyword = filter.trimmed();
    int visibleCount = 0;
    state.statsText = QStringLiteral("待处理 %1 个申请 · 已有好友 %2 人")
        .arg(pendingFriendRequests.size())
        .arg(friendCount);

    if (pendingFriendRequests.isEmpty()) {
        FriendNoticeListEntryUiState entry;
        entry.enabled = false;
        entry.muted = true;
        entry.rowHeight = 68;
        if (keyword.isEmpty()) {
            entry.text = QStringLiteral("暂无新的好友申请");
            entry.toolTip = QStringLiteral("当前没有待处理好友申请，可在搜索框输入 QQ 号后回车查找");
        } else {
            state.statsText = QStringLiteral("暂无待处理申请 · 可搜索 QQ:%1").arg(keyword);
            entry.entryId = kSearchAddPrefix + keyword;
            entry.text = QStringLiteral("暂无待处理申请，可直接搜索并添加 QQ:%1").arg(keyword);
            entry.toolTip = QStringLiteral("选择后点击“搜索并添加”，或按回车搜索 QQ:%1").arg(keyword);
            entry.enabled = true;
            entry.muted = false;
            entry.accent = true;
        }
        state.entries << entry;
        return state;
    }

    for (const QString& id : pendingFriendRequests) {
        const QString name = friendNames.value(id, id);
        if (!keyword.isEmpty()
                && !id.contains(keyword, Qt::CaseInsensitive)
                && !name.contains(keyword, Qt::CaseInsensitive)) {
            continue;
        }
        FriendNoticeListEntryUiState entry;
        entry.entryId = id;
        entry.text = QStringLiteral("%1  请求加为好友\n留言：请求添加对方为好友\n来源：QQ号-%2")
            .arg(name, id);
        entry.toolTip = QStringLiteral("申请人 %1（QQ:%2），可同意、拒绝、复制名片或回复话术")
            .arg(name, id);
        entry.rowHeight = 92;
        state.entries << entry;
        ++visibleCount;
    }

    state.statsText = keyword.isEmpty()
        ? QStringLiteral("待处理 %1 个申请 · 已有好友 %2 人")
            .arg(pendingFriendRequests.size())
            .arg(friendCount)
        : QStringLiteral("待处理 %1 个申请 · 匹配 %2 个 · 已有好友 %3 人")
            .arg(pendingFriendRequests.size())
            .arg(visibleCount)
            .arg(friendCount);

    if (visibleCount == 0 && !keyword.isEmpty()) {
        FriendNoticeListEntryUiState entry;
        entry.entryId = kSearchAddPrefix + keyword;
        entry.text = QStringLiteral("未找到申请人，可清空搜索或直接添加 QQ:%1").arg(keyword);
        entry.toolTip = QStringLiteral("没有匹配的好友申请，可直接搜索并添加 QQ:%1").arg(keyword);
        entry.accent = true;
        entry.rowHeight = 68;
        state.entries << entry;
    }
    return state;
}

FriendNoticeActionState NotificationPanelManager::friendNoticeActionState(const QString& currentId,
                                                                          bool hasPending,
                                                                          bool hasSearchKeyword) {
    FriendNoticeActionState state;
    const bool isSearchAdd = isSearchAddEntryId(currentId);
    const bool isRealRequest = !currentId.isEmpty() && !isSearchAdd;
    const bool canAccept = isSearchAdd || isRealRequest;

    state.acceptEnabled = canAccept;
    state.acceptText = isSearchAdd ? QStringLiteral("搜索并添加") : QStringLiteral("同意");
    state.acceptToolTip = isRealRequest
        ? QStringLiteral("同意当前选中的好友申请并加入好友列表")
        : (isSearchAdd
            ? QStringLiteral("对搜索结果里的 QQ 号发送好友申请")
            : QStringLiteral("选择申请后可同意，或先搜索 QQ 号"));

    state.rejectEnabled = isRealRequest;
    state.rejectToolTip = isRealRequest
        ? QStringLiteral("拒绝当前选中的好友申请")
        : QStringLiteral("当前没有可拒绝的好友申请");

    state.copyCardEnabled = isRealRequest;
    state.copyCardToolTip = isRealRequest
        ? QStringLiteral("复制当前申请人的 QQ、昵称和来源")
        : QStringLiteral("当前没有可复制的申请人名片");

    const bool allowDraftOps = hasPending || hasSearchKeyword;
    state.copyInviteEnabled = allowDraftOps;
    state.copyInviteToolTip = allowDraftOps
        ? QStringLiteral("复制一段回复好友申请的礼貌话术")
        : QStringLiteral("有申请或输入 QQ 后可复制回复话术");
    state.copyMediaPackEnabled = allowDraftOps;
    state.copyMediaPackToolTip = allowDraftOps
        ? QStringLiteral("复制同意好友后发送图片、视频或文件的准备摘要")
        : QStringLiteral("有申请或输入 QQ 后可复制媒体准备摘要");
    state.copyBatchPlanEnabled = allowDraftOps;
    state.copyBatchPlanToolTip = allowDraftOps
        ? QStringLiteral("复制当前筛选申请的批量处理和媒体发送清单")
        : QStringLiteral("当前没有可整理的申请");
    state.copyMediaGuideEnabled = allowDraftOps;
    state.copyMediaGuideToolTip = allowDraftOps
        ? QStringLiteral("复制同意好友后发送图片、视频和文件的简短指南")
        : QStringLiteral("有申请或输入 QQ 后可复制上传指南");

    state.copyAllEnabled = hasPending;
    state.copyAllToolTip = hasPending
        ? QStringLiteral("复制所有待处理申请的 QQ、昵称和回复话术")
        : QStringLiteral("当前没有待处理好友申请");
    state.acceptAllEnabled = hasPending;
    state.acceptAllToolTip = hasPending
        ? QStringLiteral("确认后批量同意所有待处理好友申请")
        : QStringLiteral("当前没有待处理好友申请");
    state.rejectAllEnabled = hasPending;
    state.rejectAllToolTip = hasPending
        ? QStringLiteral("确认后批量拒绝所有待处理好友申请")
        : QStringLiteral("当前没有待处理好友申请");
    state.clearEnabled = hasPending;
    state.clearToolTip = hasPending
        ? QStringLiteral("清空全部待处理好友申请，不会自动回复对方")
        : QStringLiteral("当前没有待清空的好友申请");

    return state;
}

QString NotificationPanelManager::friendNoticePreviewText(const QString& currentId,
                                                          const QString& displayName) {
    if (currentId.isEmpty()) {
        return QStringLiteral("选择申请后可同意、拒绝、复制名片或回复话术");
    }
    if (isSearchAddEntryId(currentId)) {
        return QStringLiteral("未找到申请人，可搜索并发送申请 QQ:%1")
            .arg(searchAddEntryTarget(currentId));
    }
    return QStringLiteral("申请人 · %1 · QQ:%2 · 可自动同意并加为好友")
        .arg(displayName, currentId);
}

GroupNoticeActionState NotificationPanelManager::groupNoticeActionState(const QString& currentId,
                                                                        bool hasSelection,
                                                                        bool hasSearchKeyword,
                                                                        int visibleCount) {
    GroupNoticeActionState state;
    const bool isCreateEntry = isGroupCreateEntryId(currentId);
    const bool canInspectGroup = hasSelection && !isCreateEntry;
    const bool allowCopy = hasSelection || hasSearchKeyword;

    state.openEnabled = hasSelection;
    state.openText = isCreateEntry
        ? QStringLiteral("创建并进入群聊")
        : (currentId.isEmpty() ? QStringLiteral("进入公共聊天室") : QStringLiteral("进入选中群聊"));
    state.openToolTip = !hasSelection
        ? QStringLiteral("选择群聊后可进入")
        : (isCreateEntry
            ? QStringLiteral("按当前关键词创建新群并立即进入")
            : QStringLiteral("进入当前选中的公共聊天室或本地群聊"));

    state.copyIdEnabled = canInspectGroup;
    state.copyIdToolTip = canInspectGroup
        ? QStringLiteral("复制当前选中群聊的群号")
        : QStringLiteral("待创建群聊没有群号，请先进入创建");
    state.copyCardEnabled = canInspectGroup;
    state.copyCardToolTip = canInspectGroup
        ? QStringLiteral("复制群名、群号、成员数和公告摘要")
        : QStringLiteral("待创建群聊没有名片，请先进入创建");
    state.copyAnnouncementEnabled = canInspectGroup;
    state.copyAnnouncementToolTip = canInspectGroup
        ? QStringLiteral("复制当前选中群聊的公告内容")
        : QStringLiteral("待创建群聊没有公告，请先进入创建");
    state.copyMembersEnabled = canInspectGroup;
    state.copyMembersToolTip = canInspectGroup
        ? QStringLiteral("复制当前选中群聊的全部成员列表")
        : QStringLiteral("待创建群聊没有成员列表，请先进入创建");
    state.copyOnlineMembersEnabled = canInspectGroup;
    state.copyOnlineMembersToolTip = canInspectGroup
        ? QStringLiteral("复制当前群里在线成员的 QQ 和昵称")
        : QStringLiteral("待创建群聊没有在线成员，请先进入创建");

    state.copyInviteEnabled = allowCopy;
    state.copyInviteToolTip = allowCopy
        ? QStringLiteral("复制一段可直接发给好友的入群邀请")
        : QStringLiteral("当前没有可邀请的群聊");
    state.copyMediaPackEnabled = allowCopy;
    state.copyMediaPackToolTip = allowCopy
        ? QStringLiteral("复制群聊媒体发送前的目标、成员和话术摘要")
        : QStringLiteral("当前没有可复制的群聊媒体包");
    state.copyBatchPlanEnabled = visibleCount > 0;
    state.copyBatchPlanToolTip = visibleCount > 0
        ? QStringLiteral("复制群聊批量发送图片、视频或文件的操作清单")
        : QStringLiteral("当前没有可整理的群聊");
    state.copyMediaGuideEnabled = allowCopy;
    state.copyMediaGuideToolTip = allowCopy
        ? QStringLiteral("复制群聊中发送图片、视频和文件的简短指南")
        : QStringLiteral("当前没有可复制的上传指南");
    state.hintText = !hasSelection
        ? QStringLiteral("先选择群聊后再进入或复制信息")
        : (isCreateEntry
            ? QStringLiteral("双击可按当前关键词创建新群并进入")
            : QStringLiteral("双击群通知可直接进入群聊"));

    return state;
}

QString NotificationPanelManager::groupNoticePreviewText(const QString& currentId,
                                                         const QString& groupName,
                                                         const QString& groupNumber,
                                                         int memberCount,
                                                         int publicOnlineCount) {
    if (currentId.isEmpty()) {
        return QStringLiteral("公共聊天室 · 在线成员%1人 · 可直接进入")
            .arg(publicOnlineCount);
    }
    if (isGroupCreateEntryId(currentId)) {
        const QString createName = groupCreateEntryName(currentId);
        return QStringLiteral("待创建群聊 · %1 · 创建后可邀请好友")
            .arg(createName.isEmpty() ? QStringLiteral("搜索群聊") : createName);
    }
    return QStringLiteral("群聊 · %1 · 群号:%2 · 成员%3人")
        .arg(groupName, groupNumber, QString::number(memberCount));
}

QString NotificationPanelManager::groupNoticeInviteText(const QString& groupName,
                                                        const QString& groupNumber,
                                                        const QString& currentUserName,
                                                        const QString& currentUserId) {
    return QStringLiteral("我邀请你加入群聊“%1”（群号:%2）。我是 %3（QQ:%4），进群后可以一起聊天、发图片和传文件。")
        .arg(groupName, groupNumber, currentUserName, currentUserId);
}

QString NotificationPanelManager::groupNoticeMediaPackText(const QString& groupName,
                                                           const QString& groupNumber,
                                                           int memberCount,
                                                           const QString& currentUserName,
                                                           const QString& currentUserId) {
    QStringList rows;
    rows << QStringLiteral("群媒体包 · %1 · 群号:%2").arg(groupName, groupNumber);
    rows << QStringLiteral("我的QQ:%1 · 昵称:%2 · 群成员:%3")
                .arg(currentUserId, currentUserName, QString::number(memberCount));
    rows << QStringLiteral("群内可直接发送图片/视频，也可用闪传文件发送文档和压缩包");
    rows << QStringLiteral("支持 png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm 和常用文档压缩包");
    rows << QStringLiteral("入群话术：%1")
                .arg(groupNoticeInviteText(groupName, groupNumber, currentUserName, currentUserId));
    rows << QStringLiteral("查收话术：我已发送媒体文件到群聊“%1”，请注意查收。").arg(groupName);
    return rows.join('\n');
}

QString NotificationPanelManager::groupNoticeBatchPlanText(const QString& keyword,
                                                           const QStringList& groups,
                                                           int totalMembers,
                                                           int onlineMembers,
                                                           const QString& currentUserName,
                                                           const QString& currentUserId) {
    QStringList rows;
    rows << QStringLiteral("群批量媒体计划 · 筛选:%1").arg(keyword.isEmpty() ? QStringLiteral("全部群通知") : keyword);
    rows << QStringLiteral("我的QQ:%1 · 昵称:%2 · 可见群:%3 · 成员:%4 · 在线:%5")
                .arg(currentUserId,
                     currentUserName,
                     QString::number(groups.size()),
                     QString::number(totalMembers),
                     QString::number(onlineMembers));
    rows << QStringLiteral("群聊目标:%1").arg(groups.isEmpty() ? QStringLiteral("无可见群聊") : groups.join(QStringLiteral("、")));
    rows << QStringLiteral("1. 先处理待创建群或打开已有群聊");
    rows << QStringLiteral("2. 图片/GIF/视频走图片视频入口，文档和压缩包走闪传文件");
    rows << QStringLiteral("3. 发送后复制群媒体包、上传指南和查收话术给群成员");
    rows << QStringLiteral("4. 可按筛选关键词分批发送，优先覆盖在线成员较多的群聊");
    return rows.join('\n');
}

QString NotificationPanelManager::groupNoticeMediaGuideText(const QString& groupName,
                                                            const QString& groupNumber,
                                                            int memberCount,
                                                            const QString& currentUserName,
                                                            const QString& currentUserId) {
    QStringList rows;
    rows << QStringLiteral("群上传指南 · %1 · 群号:%2").arg(groupName, groupNumber);
    rows << QStringLiteral("我的QQ:%1 · 昵称:%2 · 群成员:%3")
                .arg(currentUserId, currentUserName, QString::number(memberCount));
    rows << QStringLiteral("图片/视频：支持 png、jpg、gif、mp4、mov、avi、mkv、wmv、flv、webm");
    rows << QStringLiteral("闪传文件：支持文档、压缩包和媒体文件");
    rows << QStringLiteral("聊天记录右键可复制媒体卡片和查收话术");
    rows << QStringLiteral("可先复制入群话术邀请好友，进群后直接发送图片/视频/文件");
    return rows.join('\n');
}

bool NotificationPanelManager::isSearchAddEntryId(const QString& entryId) {
    return entryId.startsWith(kSearchAddPrefix);
}

QString NotificationPanelManager::searchAddEntryTarget(const QString& entryId) {
    return isSearchAddEntryId(entryId)
        ? entryId.mid(kSearchAddPrefix.size()).trimmed()
        : QString();
}

bool NotificationPanelManager::isGroupCreateEntryId(const QString& entryId) {
    return entryId.startsWith(kGroupCreatePrefix);
}

QString NotificationPanelManager::groupCreateEntryName(const QString& entryId) {
    return isGroupCreateEntryId(entryId)
        ? entryId.mid(kGroupCreatePrefix.size()).trimmed()
        : QString();
}
