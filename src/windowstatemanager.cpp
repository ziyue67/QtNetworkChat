#include "windowstatemanager.h"

QString WindowStateManager::appWindowTitle(const QString& version, const QString& suffix) {
    const QString base = QStringLiteral("QtNetworkChat %1").arg(version.trimmed());
    return suffix.trimmed().isEmpty() ? base : QStringLiteral("%1 - %2").arg(base, suffix.trimmed());
}

WindowChromeState WindowStateManager::unreadState(const QString& version, int unreadCount) {
    WindowChromeState state;
    const int safeUnreadCount = qMax(0, unreadCount);
    const QString suffix = QStringLiteral("%1 条新消息").arg(safeUnreadCount);
    state.windowTitle = appWindowTitle(version, suffix);
    state.trayToolTip = state.windowTitle;
    return state;
}

WindowChromeState WindowStateManager::clearedState(const QString& version,
                                                   bool privateChatActive,
                                                   const QString& currentUserName,
                                                   const QString& chatTitle) {
    WindowChromeState state;
    state.windowTitle = privateChatActive
        ? chatTitle
        : appWindowTitle(version, currentUserName);
    state.trayToolTip = appWindowTitle(version);
    return state;
}
