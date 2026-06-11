#include "composermanager.h"

ComposerUiState ComposerManager::uiState(const ComposerContext& context) {
    ComposerUiState state;
    const QString draftText = context.draftText.trimmed();
    const bool hasText = !draftText.isEmpty();
    const QString targetName = context.targetName.trimmed().isEmpty()
        ? QStringLiteral("公共聊天室")
        : context.targetName.trimmed();
    state.canReachTarget = !context.removedFromPublicGroup
        && (context.localGroup || context.clientConnected);
    state.canSend = hasText && state.canReachTarget;
    state.sendFileEnabled = state.canReachTarget;
    state.sendImageEnabled = state.canReachTarget;

    const QString encryptedSuffix = context.encryptedReady ? QStringLiteral(" · 端到端加密") : QString();
    const QString composerHint = QStringLiteral("发往 %1%2... (Enter 发送，Shift/Ctrl+Enter 换行，Esc 清空草稿)")
        .arg(targetName, encryptedSuffix);

    state.sendToolTip = context.removedFromPublicGroup
        ? QStringLiteral("当前账号已不在公共群，等待群主或管理员重新邀请")
        : (!state.canReachTarget
            ? QStringLiteral("当前已断开，无法发送到 %1").arg(targetName)
            : (hasText
                ? QStringLiteral("发送到 %1 · %2 字%3 (Enter)").arg(targetName).arg(draftText.size()).arg(encryptedSuffix)
                : QStringLiteral("请输入消息后发送到 %1").arg(targetName)));
    state.messagePlaceholder = context.removedFromPublicGroup
        ? QStringLiteral("当前账号已不在公共群，等待群主或管理员重新邀请")
        : (state.canReachTarget
            ? composerHint
            : QStringLiteral("已断开连接，重新登录后可发送到 %1").arg(targetName));
    state.messageToolTip = hasText
        ? QStringLiteral("当前草稿将发送到 %1 · %2 字").arg(targetName).arg(draftText.size())
        : state.messagePlaceholder;
    state.fileToolTip = context.removedFromPublicGroup
        ? QStringLiteral("当前账号已不在公共群，暂不能发送文件")
        : (state.canReachTarget
            ? QStringLiteral("发送文件到 %1，支持文档、压缩包和媒体文件").arg(targetName)
            : QStringLiteral("当前已断开，暂不能发送文件到 %1").arg(targetName));
    state.imageToolTip = context.removedFromPublicGroup
        ? QStringLiteral("当前账号已不在公共群，暂不能发送图片或视频")
        : (state.canReachTarget
            ? QStringLiteral("发送图片或视频到 %1，图片会显示预览").arg(targetName)
            : QStringLiteral("当前已断开，暂不能发送图片/视频到 %1").arg(targetName));
    return state;
}

ComposerMentionMenuPlan ComposerManager::mentionMenuPlan(const QStringList& memberIds,
                                                         const QString& currentUserId,
                                                         const QMap<QString, QString>& displayNames) {
    ComposerMentionMenuPlan plan;
    ComposerMentionAction allAction;
    allAction.title = QStringLiteral("@全体成员");
    allAction.insertText = QStringLiteral("@全体成员 ");
    allAction.statusMessage = QStringLiteral("已插入 @全体成员");
    plan.actions.append(allAction);

    QStringList uniqueMemberIds;
    for (const QString& memberId : memberIds) {
        const QString normalizedId = memberId.trimmed();
        if (normalizedId.isEmpty()
                || normalizedId == currentUserId
                || uniqueMemberIds.contains(normalizedId)) {
            continue;
        }
        uniqueMemberIds.append(normalizedId);
    }
    plan.separatorAfterAll = !uniqueMemberIds.isEmpty();

    for (const QString& memberId : uniqueMemberIds) {
        const QString name = displayNames.value(memberId).trimmed().isEmpty()
            ? memberId
            : displayNames.value(memberId).trimmed();
        ComposerMentionAction action;
        action.title = QStringLiteral("@%1 (QQ:%2)").arg(name, memberId);
        action.insertText = QStringLiteral("@%1 ").arg(name);
        action.statusMessage = QStringLiteral("已插入 @%1").arg(name);
        plan.actions.append(action);
    }

    return plan;
}
