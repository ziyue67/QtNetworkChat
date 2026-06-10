#include "notificationpanelmanager.h"

namespace {
const QString kSearchAddPrefix = QStringLiteral("search_add:");
const QString kGroupCreatePrefix = QStringLiteral("group_create:");
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
