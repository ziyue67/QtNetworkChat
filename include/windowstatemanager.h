#ifndef WINDOWSTATEMANAGER_H
#define WINDOWSTATEMANAGER_H

#include <QString>

struct WindowChromeState {
    QString windowTitle;
    QString trayToolTip;
};

class WindowStateManager {
public:
    static QString appWindowTitle(const QString& version, const QString& suffix = QString());
    static WindowChromeState unreadState(const QString& version, int unreadCount);
    static WindowChromeState clearedState(const QString& version,
                                          bool privateChatActive,
                                          const QString& currentUserName,
                                          const QString& chatTitle);
};

#endif // WINDOWSTATEMANAGER_H
