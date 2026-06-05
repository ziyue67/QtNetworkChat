#include "groupmanager.h"

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

    GroupNoticeUiState emptyNotice = GroupManager::noticeUiState(0);
    ok = expect(emptyNotice.joinedGroupCount == 1
                    && emptyNotice.text == QString::fromUtf8("群通知")
                    && emptyNotice.toolTip.contains(QString::fromUtf8("公共聊天室")),
                "empty local group notice should show neutral public group entry") && ok;
    GroupNoticeUiState joinedNotice = GroupManager::noticeUiState(2);
    ok = expect(joinedNotice.joinedGroupCount == 3
                    && joinedNotice.text == QString::fromUtf8("群通知 3")
                    && joinedNotice.toolTip.contains(QStringLiteral("3")),
                "joined group notice should include public group in count") && ok;

    ok = expect(GroupManager::localGroupOwnerId(QStringList{QString(), QStringLiteral("owner"), QStringLiteral("member")},
                                                QStringLiteral("self")) == QStringLiteral("owner"),
                "local group owner should be first non-empty member") && ok;
    ok = expect(GroupManager::localGroupOwnerId(QStringList(), QStringLiteral("self")) == QStringLiteral("self"),
                "empty local group should fall back to current user as owner") && ok;
    ok = expect(GroupManager::isLocalGroupOwner(QStringLiteral("local_group_1"),
                                                QStringList{QStringLiteral("self"), QStringLiteral("friend")},
                                                QStringLiteral("self"))
                    && !GroupManager::isLocalGroupOwner(QStringLiteral("local_group_1"),
                                                        QStringList{QStringLiteral("owner"), QStringLiteral("self")},
                                                        QStringLiteral("self"))
                    && !GroupManager::isLocalGroupOwner(QStringLiteral("public"),
                                                        QStringList{QStringLiteral("self")},
                                                        QStringLiteral("self")),
                "local group owner check should require local group id and owner match") && ok;

    QMap<QString, QString> owners;
    owners.insert(QStringLiteral("public"), QStringLiteral("owner"));
    QMap<QString, QString> roles;
    roles.insert(QStringLiteral("public|owner"), QStringLiteral("owner"));
    roles.insert(QStringLiteral("public|admin"), QStringLiteral("admin"));
    roles.insert(QStringLiteral("public|member"), QStringLiteral("member"));
    ok = expect(GroupManager::canManageServerGroup(QStringLiteral("public"), QStringLiteral("owner"), owners, roles)
                    && GroupManager::canManageServerGroup(QStringLiteral("public"), QStringLiteral("admin"), owners, roles)
                    && !GroupManager::canManageServerGroup(QStringLiteral("public"), QStringLiteral("member"), owners, roles)
                    && !GroupManager::canManageServerGroup(QString(), QStringLiteral("owner"), owners, roles),
                "server group management should allow owner/admin only") && ok;

    GroupMemberDisplayState ownerDisplay = GroupManager::memberDisplayState(QStringLiteral("owner"),
                                                                            QStringLiteral("owner"),
                                                                            QStringLiteral("owner"),
                                                                            QStringLiteral("owner"),
                                                                            false,
                                                                            false,
                                                                            true,
                                                                            true);
    ok = expect(ownerDisplay.isOwner
                    && ownerDisplay.role == QString::fromUtf8("群主/我")
                    && ownerDisplay.state == QString::fromUtf8("在线")
                    && ownerDisplay.actionText == QString::fromUtf8("本人"),
                "server owner display should mark self owner") && ok;

    GroupMemberDisplayState adminDisplay = GroupManager::memberDisplayState(QStringLiteral("admin"),
                                                                            QStringLiteral("self"),
                                                                            QStringLiteral("owner"),
                                                                            QStringLiteral("admin"),
                                                                            false,
                                                                            true,
                                                                            false,
                                                                            true);
    ok = expect(adminDisplay.isAdmin
                    && adminDisplay.role == QString::fromUtf8("管理员")
                    && adminDisplay.state == QString::fromUtf8("离线")
                    && adminDisplay.actionText == QString::fromUtf8("等待确认"),
                "server admin pending display should preserve admin role and pending action") && ok;

    GroupMemberDisplayState localStranger = GroupManager::memberDisplayState(QStringLiteral("stranger"),
                                                                             QStringLiteral("self"),
                                                                             QStringLiteral("owner"),
                                                                             QString(),
                                                                             false,
                                                                             false,
                                                                             false,
                                                                             false);
    ok = expect(!localStranger.isOwner
                    && !localStranger.isAdmin
                    && localStranger.role == QString::fromUtf8("群成员")
                    && localStranger.actionText == QString::fromUtf8("可发送申请"),
                "local group stranger should use local group member copy") && ok;

    GroupMemberDisplayState serverStranger = GroupManager::memberDisplayState(QStringLiteral("stranger"),
                                                                              QStringLiteral("self"),
                                                                              QStringLiteral("owner"),
                                                                              QStringLiteral("member"),
                                                                              false,
                                                                              false,
                                                                              true,
                                                                              true);
    ok = expect(serverStranger.role == QString::fromUtf8("成员")
                    && serverStranger.actionText == QString::fromUtf8("双击发送申请"),
                "server group stranger should use server member copy") && ok;

    return ok ? 0 : 1;
}
