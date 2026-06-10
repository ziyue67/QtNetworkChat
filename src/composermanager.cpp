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
