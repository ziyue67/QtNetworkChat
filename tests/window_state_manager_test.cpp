#include "windowstatemanager.h"

#include <QCoreApplication>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning("%s", message);
        return false;
    }
    return true;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    bool ok = true;
    const QString version = QStringLiteral("1.2.3");

    ok = expect(WindowStateManager::appWindowTitle(version) == QStringLiteral("QtNetworkChat 1.2.3"),
                "base title should include product name and version") && ok;
    ok = expect(WindowStateManager::appWindowTitle(version, QStringLiteral(" Alice ")) == QStringLiteral("QtNetworkChat 1.2.3 - Alice"),
                "title should trim and append non-empty suffix") && ok;
    ok = expect(WindowStateManager::appWindowTitle(version, QStringLiteral("   ")) == QStringLiteral("QtNetworkChat 1.2.3"),
                "blank suffix should not add separator") && ok;

    WindowChromeState unread = WindowStateManager::unreadState(version, 3);
    ok = expect(unread.windowTitle == QString::fromUtf8("QtNetworkChat 1.2.3 - 3 条新消息")
                    && unread.trayToolTip == unread.windowTitle,
                "unread state should mirror unread count to window and tray") && ok;

    WindowChromeState negativeUnread = WindowStateManager::unreadState(version, -2);
    ok = expect(negativeUnread.windowTitle == QString::fromUtf8("QtNetworkChat 1.2.3 - 0 条新消息"),
                "unread state should clamp negative counts to zero") && ok;

    WindowChromeState publicCleared = WindowStateManager::clearedState(version,
                                                                       false,
                                                                       QString::fromUtf8("小明"),
                                                                       QString::fromUtf8("与好友私聊中"));
    ok = expect(publicCleared.windowTitle == QString::fromUtf8("QtNetworkChat 1.2.3 - 小明")
                    && publicCleared.trayToolTip == QStringLiteral("QtNetworkChat 1.2.3"),
                "public cleared state should restore current user title and neutral tray tooltip") && ok;

    WindowChromeState privateCleared = WindowStateManager::clearedState(version,
                                                                        true,
                                                                        QString::fromUtf8("小明"),
                                                                        QString::fromUtf8("与好友私聊中"));
    ok = expect(privateCleared.windowTitle == QString::fromUtf8("与好友私聊中")
                    && privateCleared.trayToolTip == QStringLiteral("QtNetworkChat 1.2.3"),
                "private cleared state should preserve active chat title and reset tray tooltip") && ok;

    return ok ? 0 : 1;
}
