#include "chatsessionmanager.h"

QString ChatSessionManager::e2eSummary(bool hasSession, bool needsRotation) {
    if (!hasSession) {
        return QStringLiteral("端到端加密未就绪");
    }
    return needsRotation ? QStringLiteral("端到端加密需轮换") : QStringLiteral("端到端加密就绪");
}

PrivateChatUiState ChatSessionManager::privateChatState(const QString& userId,
                                                        const QString& displayName,
                                                        bool online,
                                                        bool hasE2ESession,
                                                        bool e2eNeedsRotation) {
    PrivateChatUiState state;
    const QString safeName = displayName.trimmed().isEmpty() ? userId.trimmed() : displayName.trimmed();
    const QString onlineText = online ? QStringLiteral("在线") : QStringLiteral("离线");
    state.windowSuffix = QStringLiteral("私聊: %1").arg(safeName);
    state.titleText = QStringLiteral("与 %1 私聊中").arg(safeName);
    state.hintText = QStringLiteral("QQ: %1 · %2 · %3 · 点击菜单“返回群聊”回到公共聊天室")
        .arg(userId.trimmed(), onlineText, e2eSummary(hasE2ESession, e2eNeedsRotation));
    return state;
}

ConnectionUiState ChatSessionManager::disconnectedState(const QString& targetName, bool localGroup) {
    ConnectionUiState state;
    const QString safeTarget = targetName.trimmed().isEmpty() ? QStringLiteral("公共聊天室") : targetName.trimmed();
    state.hintText = localGroup
        ? QStringLiteral("本地群聊 · 已断开服务器，仍可记录本地消息")
        : QStringLiteral("已断开服务器 · %1 暂停发送，消息草稿会保留").arg(safeTarget);
    state.statusMessage = localGroup
        ? QStringLiteral("已断开服务器，本地群聊仍可继续记录")
        : QStringLiteral("已断开服务器，暂不能发送到 %1").arg(safeTarget);
    return state;
}

ConnectionUiState ChatSessionManager::errorState(const QString& targetName, bool localGroup, const QString& error) {
    ConnectionUiState state;
    const QString safeTarget = targetName.trimmed().isEmpty() ? QStringLiteral("公共聊天室") : targetName.trimmed();
    const QString briefError = error.left(80);
    state.hintText = localGroup
        ? QStringLiteral("连接错误 · 本地群聊仍可记录 · %1").arg(briefError)
        : QStringLiteral("连接错误 · %1 暂停发送 · %2").arg(safeTarget, briefError);
    state.statusMessage = localGroup
        ? QStringLiteral("连接错误，本地群聊仍可继续记录")
        : QStringLiteral("连接错误，暂不能发送到 %1").arg(safeTarget);
    return state;
}

QString ChatSessionManager::e2eSessionHint(const QString& displayName, const QString& state) {
    const QString safeName = displayName.trimmed();
    if (state == QLatin1String("ready")) {
        return QStringLiteral("端到端加密已就绪 · %1").arg(safeName);
    }
    if (state == QLatin1String("rotation-required")) {
        return QStringLiteral("端到端加密需要轮换 · %1").arg(safeName);
    }
    if (state == QLatin1String("missing-session")) {
        return QStringLiteral("端到端加密未就绪 · %1").arg(safeName);
    }
    return QString();
}

QString ChatSessionManager::e2eIdentityPendingHint(const QString& displayName, const QString& fingerprint) {
    return QStringLiteral("端到端加密身份待核对 · %1 · 指纹:%2")
        .arg(displayName.trimmed(), fingerprint.left(16));
}

QString ChatSessionManager::e2eRotationRequestHint(const QString& displayName) {
    return QStringLiteral("收到端到端加密轮换请求 · %1").arg(displayName.trimmed());
}

QString ChatSessionManager::e2eRotationResponseHint(const QString& displayName, bool accepted) {
    return accepted
        ? QStringLiteral("端到端加密轮换已被接受 · %1").arg(displayName.trimmed())
        : QStringLiteral("端到端加密轮换被拒绝 · %1").arg(displayName.trimmed());
}

QString ChatSessionManager::localGroupHint(const QString& groupId, const QString& ownerId, bool owner) {
    const QString localPrefix = QStringLiteral("local_group_");
    const QString groupNumber = groupId.startsWith(localPrefix) ? groupId.mid(localPrefix.size()) : groupId;
    return QStringLiteral("本地群聊 · 群号 %1 · 群主 %2 · 我的权限:%3")
        .arg(groupNumber, ownerId, owner ? QStringLiteral("群主") : QStringLiteral("成员"));
}
