#include "sessionlistbuilder.h"

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

    // Public room is always first, pinned, and keeps its stable routing id.
    {
        const QList<SessionListBuilder::SessionEntry> entries =
            SessionListBuilder::build(QString::fromUtf8("公共聊天室"),
                                      QString::fromUtf8("公共聊天室 · 点击进入"),
                                      {}, {});
        ok = expect(entries.size() == 1
                        && entries.first().id == QStringLiteral("__public__")
                        && entries.first().pinned
                        && entries.first().isGroup
                        && entries.first().name == QString::fromUtf8("公共聊天室"),
                    "public room must be the only pinned first entry when no sessions exist") && ok;
    }

    // Local groups sort by locale-aware name and land after the public room.
    {
        QList<SessionListBuilder::LocalGroupInput> groups;
        groups.append({QStringLiteral("g2"), QStringLiteral("Beta"), QString::fromUtf8("[群]")});
        groups.append({QStringLiteral("g1"), QStringLiteral("Alpha"), QString::fromUtf8("[群]")});
        const QList<SessionListBuilder::SessionEntry> entries =
            SessionListBuilder::build(QString::fromUtf8("公共聊天室"), QString(), groups, {});
        ok = expect(entries.size() == 3
                        && entries.at(0).id == QStringLiteral("__public__")
                        && entries.at(1).id == QStringLiteral("g1")
                        && entries.at(2).id == QStringLiteral("g2")
                        && entries.at(1).isGroup && entries.at(2).isGroup,
                    "local groups should follow the public room sorted by name") && ok;
    }

    // Friends: online first, then by name; offline/online summary text applied.
    {
        QList<SessionListBuilder::FriendInput> friends;
        friends.append({QStringLiteral("f_offline_b"), QStringLiteral("Beta Offline"), false});
        friends.append({QStringLiteral("f_online_b"), QStringLiteral("Beta Online"), true});
        friends.append({QStringLiteral("f_online_a"), QStringLiteral("Alpha Online"), true});
        friends.append({QStringLiteral("f_offline_a"), QStringLiteral("Alpha Offline"), false});
        const QList<SessionListBuilder::SessionEntry> entries =
            SessionListBuilder::build(QString::fromUtf8("公共聊天室"), QString(), {}, friends);

        ok = expect(entries.size() == 5, "public room + four friends expected") && ok;
        // entries[0] is the public room. Online friends come first (a, b), then offline (a, b).
        ok = expect(entries.at(1).id == QStringLiteral("f_online_a")
                        && entries.at(1).online
                        && entries.at(1).lastMessage.contains(QString::fromUtf8("在线")),
                    "first friend should be the alphabetically-first online friend") && ok;
        ok = expect(entries.at(2).id == QStringLiteral("f_online_b") && entries.at(2).online,
                    "second friend should be the next online friend") && ok;
        ok = expect(entries.at(3).id == QStringLiteral("f_offline_a")
                        && !entries.at(3).online
                        && entries.at(3).lastMessage.contains(QString::fromUtf8("离线")),
                    "offline friends should follow all online friends, sorted by name") && ok;
        ok = expect(entries.at(4).id == QStringLiteral("f_offline_b") && !entries.at(4).online,
                    "last friend should be the alphabetically-last offline friend") && ok;
    }

    // Full ordering: public room, then groups, then friends.
    {
        QList<SessionListBuilder::LocalGroupInput> groups;
        groups.append({QStringLiteral("g1"), QString::fromUtf8("甲组"), QString::fromUtf8("[群]")});
        QList<SessionListBuilder::FriendInput> friends;
        friends.append({QStringLiteral("f1"), QString::fromUtf8("好友"), true});
        const QList<SessionListBuilder::SessionEntry> entries =
            SessionListBuilder::build(QString::fromUtf8("公共聊天室"), QString(), groups, friends);
        ok = expect(entries.size() == 3
                        && entries.at(0).id == QStringLiteral("__public__")
                        && entries.at(1).id == QStringLiteral("g1")
                        && entries.at(2).id == QStringLiteral("f1"),
                    "sections should be ordered public, groups, friends") && ok;
    }

    return ok ? 0 : 1;
}
