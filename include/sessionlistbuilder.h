#ifndef SESSIONLISTBUILDER_H
#define SESSIONLISTBUILDER_H

#include <QList>
#include <QString>
#include <algorithm>

// Pure ordering rules for the QQNT session list. Kept free of any Qt
// widget/model dependency so the sort order and role composition can be unit
// tested without a running MainWindow.
namespace SessionListBuilder {

struct SessionEntry {
    QString id;          // stable routing id ("__public__" for the public room)
    QString name;
    QString lastMessage;
    bool isGroup = false;
    bool online = false;
    bool pinned = false;
    int unread = 0;
    bool atMention = false;
};

struct LocalGroupInput {
    QString id;
    QString name;
    QString announcement;
    bool pinned = false;
};

struct FriendInput {
    QString id;
    QString name;
    bool online = false;
};

// Build the ordered session list:
//   1) The public chatroom is always first and pinned.
//   2) Local groups follow, sorted by locale-aware name.
//   3) Friends follow, online first then by locale-aware name.
inline QList<SessionEntry> build(const QString& publicRoomName,
                                 const QString& publicRoomLastMessage,
                                 const QList<LocalGroupInput>& localGroups,
                                 const QList<FriendInput>& friends) {
    QList<SessionEntry> entries;

    SessionEntry publicRoom;
    publicRoom.id = QStringLiteral("__public__");
    publicRoom.name = publicRoomName;
    publicRoom.lastMessage = publicRoomLastMessage;
    publicRoom.isGroup = true;
    publicRoom.pinned = true;
    entries.append(publicRoom);

    QList<SessionEntry> groupEntries;
    groupEntries.reserve(localGroups.size());
    for (const LocalGroupInput& group : localGroups) {
        SessionEntry entry;
        entry.id = group.id;
        entry.name = group.name;
        entry.lastMessage = group.announcement;
        entry.isGroup = true;
        entry.pinned = group.pinned;
        groupEntries.append(entry);
    }
    std::sort(groupEntries.begin(), groupEntries.end(), [](const SessionEntry& a, const SessionEntry& b) {
        if (a.pinned != b.pinned) return a.pinned && !b.pinned;
        return a.name.localeAwareCompare(b.name) < 0;
    });

    QList<SessionEntry> friendEntries;
    friendEntries.reserve(friends.size());
    for (const FriendInput& friendInput : friends) {
        SessionEntry entry;
        entry.id = friendInput.id;
        entry.name = friendInput.name;
        entry.online = friendInput.online;
        entry.lastMessage = friendInput.online
            ? QStringLiteral("[在线] 暂无消息")
            : QStringLiteral("[离线] 暂无消息");
        friendEntries.append(entry);
    }
    std::sort(friendEntries.begin(), friendEntries.end(), [](const SessionEntry& a, const SessionEntry& b) {
        if (a.online != b.online) return a.online && !b.online;
        return a.name.localeAwareCompare(b.name) < 0;
    });

    entries.append(groupEntries);
    entries.append(friendEntries);
    return entries;
}

} // namespace SessionListBuilder

#endif // SESSIONLISTBUILDER_H
