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
    roles.insert(QStringLiteral("public|coowner"), QStringLiteral("owner"));
    ok = expect(GroupManager::canManageServerGroup(QStringLiteral("public"), QStringLiteral("owner"), owners, roles)
                    && GroupManager::canManageServerGroup(QStringLiteral("public"), QStringLiteral("admin"), owners, roles)
                    && !GroupManager::canManageServerGroup(QStringLiteral("public"), QStringLiteral("member"), owners, roles)
                    && !GroupManager::canManageServerGroup(QString(), QStringLiteral("owner"), owners, roles),
                "server group management should allow owner/admin only") && ok;

    const QStringList publicMembers{QStringLiteral("owner"), QStringLiteral("admin"), QStringLiteral("member"), QStringLiteral("coowner")};
    ServerGroupMemberUpdateDecision invalidUpdate = GroupManager::serverGroupMemberUpdateDecision(
        QString(), QStringLiteral("add"), true, QStringLiteral("public"), QStringLiteral("owner"), publicMembers, owners, roles);
    ok = expect(!invalidUpdate.allowed
                    && invalidUpdate.statusMessage.contains(QString::fromUtf8("参数无效"))
                    && invalidUpdate.statusTimeoutMs == 2200,
                "server group update should reject empty targets") && ok;
    ServerGroupMemberUpdateDecision disconnectedUpdate = GroupManager::serverGroupMemberUpdateDecision(
        QStringLiteral("newbie"), QStringLiteral("add"), false, QStringLiteral("public"), QStringLiteral("owner"), publicMembers, owners, roles);
    ok = expect(!disconnectedUpdate.allowed
                    && disconnectedUpdate.statusMessage.contains(QString::fromUtf8("未连接服务器"))
                    && disconnectedUpdate.statusTimeoutMs == 2600,
                "server group update should reject disconnected clients") && ok;
    ServerGroupMemberUpdateDecision unmanagedUpdate = GroupManager::serverGroupMemberUpdateDecision(
        QStringLiteral("newbie"), QStringLiteral("add"), true, QStringLiteral("public"), QStringLiteral("member"), publicMembers, owners, roles);
    ok = expect(!unmanagedUpdate.allowed
                    && unmanagedUpdate.auditMessage.contains(QString::fromUtf8("不是群主或管理员"))
                    && unmanagedUpdate.statusMessage.contains(QString::fromUtf8("群主或管理员")),
                "server group update should reject non-manager members with audit text") && ok;
    ServerGroupMemberUpdateDecision adminPromoteUpdate = GroupManager::serverGroupMemberUpdateDecision(
        QStringLiteral("member"), QStringLiteral("promote_admin"), true, QStringLiteral("public"), QStringLiteral("admin"), publicMembers, owners, roles);
    ok = expect(!adminPromoteUpdate.allowed
                    && adminPromoteUpdate.roleAction
                    && adminPromoteUpdate.auditMessage.contains(QString::fromUtf8("当前账号不是群主")),
                "server group role updates should require owner privileges") && ok;
    ServerGroupMemberUpdateDecision duplicateAddUpdate = GroupManager::serverGroupMemberUpdateDecision(
        QStringLiteral("member"), QStringLiteral("add"), true, QStringLiteral("public"), QStringLiteral("owner"), publicMembers, owners, roles);
    ok = expect(!duplicateAddUpdate.allowed
                    && duplicateAddUpdate.statusMessage.contains(QString::fromUtf8("已在公共群"))
                    && duplicateAddUpdate.statusTimeoutMs == 1800,
                "server group update should reject duplicate add requests") && ok;
    ServerGroupMemberUpdateDecision missingRemoveUpdate = GroupManager::serverGroupMemberUpdateDecision(
        QStringLiteral("ghost"), QStringLiteral("remove"), true, QStringLiteral("public"), QStringLiteral("owner"), publicMembers, owners, roles);
    ok = expect(!missingRemoveUpdate.allowed
                    && missingRemoveUpdate.statusMessage.contains(QString::fromUtf8("不在公共群")),
                "server group update should reject removing absent users") && ok;
    ServerGroupMemberUpdateDecision selfRemoveUpdate = GroupManager::serverGroupMemberUpdateDecision(
        QStringLiteral("owner"), QStringLiteral("remove"), true, QStringLiteral("public"), QStringLiteral("owner"), publicMembers, owners, roles);
    ok = expect(!selfRemoveUpdate.allowed
                    && selfRemoveUpdate.statusMessage.contains(QString::fromUtf8("移出自己")),
                "server group update should reject self removal") && ok;
    ServerGroupMemberUpdateDecision adminRoleUpdate = GroupManager::serverGroupMemberUpdateDecision(
        QStringLiteral("owner"), QStringLiteral("demote_admin"), true, QStringLiteral("public"), QStringLiteral("admin"), publicMembers, owners, roles);
    ok = expect(!adminRoleUpdate.allowed
                    && adminRoleUpdate.statusMessage.contains(QString::fromUtf8("只有群主")),
                "server group role update should reject admin demoting owner before owner-target checks") && ok;
    ServerGroupMemberUpdateDecision ownerTargetUpdate = GroupManager::serverGroupMemberUpdateDecision(
        QStringLiteral("coowner"), QStringLiteral("remove"), true, QStringLiteral("public"), QStringLiteral("owner"), publicMembers, owners, roles);
    ok = expect(!ownerTargetUpdate.allowed
                    && ownerTargetUpdate.statusMessage.contains(QString::fromUtf8("群主不能被移出")),
                "server group update should reject removing owner-role targets") && ok;
    ServerGroupMemberUpdateDecision allowedAddUpdate = GroupManager::serverGroupMemberUpdateDecision(
        QStringLiteral("newbie"), QStringLiteral("ADD"), true, QStringLiteral("public"), QStringLiteral("admin"), publicMembers, owners, roles);
    ok = expect(allowedAddUpdate.allowed
                    && allowedAddUpdate.targetId == QStringLiteral("newbie")
                    && allowedAddUpdate.normalizedAction == QStringLiteral("add")
                    && !allowedAddUpdate.roleAction,
                "server group update should allow admin add and normalize action") && ok;
    ServerGroupMemberUpdateDecision allowedPromoteUpdate = GroupManager::serverGroupMemberUpdateDecision(
        QStringLiteral("member"), QStringLiteral("promote_admin"), true, QStringLiteral("public"), QStringLiteral("owner"), publicMembers, owners, roles);
    ok = expect(allowedPromoteUpdate.allowed
                    && allowedPromoteUpdate.roleAction
                    && allowedPromoteUpdate.normalizedAction == QStringLiteral("promote_admin"),
                "server group update should allow owner role changes for members") && ok;

    GroupAnnouncementEditDecision localDefaultAnnouncement =
        GroupManager::announcementEditDecision(QStringLiteral("旧公告"),
                                               QString(),
                                               true,
                                               QStringLiteral("产品群"));
    ok = expect(localDefaultAnnouncement.changed
                    && localDefaultAnnouncement.usedDefaultAnnouncement
                    && localDefaultAnnouncement.text == QString::fromUtf8("产品群 已创建，可继续邀请好友并发送消息。")
                    && localDefaultAnnouncement.appliedStatusMessage == QString::fromUtf8("群公告为空，已使用默认公告"),
                "local empty announcement should fall back to local group default copy") && ok;

    GroupAnnouncementEditDecision publicDefaultAnnouncement =
        GroupManager::announcementEditDecision(QStringLiteral("旧公告"),
                                               QString(),
                                               false,
                                               QString());
    ok = expect(publicDefaultAnnouncement.changed
                    && publicDefaultAnnouncement.usedDefaultAnnouncement
                    && publicDefaultAnnouncement.text == QString::fromUtf8("欢迎来到公共聊天室，支持 QQ 号搜索、好友、私聊和文件发送。")
                    && publicDefaultAnnouncement.submittedStatusMessage == QString::fromUtf8("群公告为空，已提交默认公告"),
                "public empty announcement should fall back to public group default copy") && ok;

    GroupAnnouncementEditDecision unchangedAnnouncement =
        GroupManager::announcementEditDecision(QString::fromUtf8("已有公告"),
                                               QString::fromUtf8(" 已有公告 "),
                                               true,
                                               QStringLiteral("产品群"));
    ok = expect(!unchangedAnnouncement.changed
                    && !unchangedAnnouncement.usedDefaultAnnouncement
                    && unchangedAnnouncement.unchangedStatusMessage == QString::fromUtf8("群公告未改变"),
                "trimmed unchanged announcement should not update") && ok;

    GroupAnnouncementEditDecision changedAnnouncement =
        GroupManager::announcementEditDecision(QStringLiteral("旧公告"),
                                               QStringLiteral("新公告"),
                                               true,
                                               QStringLiteral("产品群"));
    ok = expect(changedAnnouncement.changed
                    && !changedAnnouncement.usedDefaultAnnouncement
                    && changedAnnouncement.text == QString::fromUtf8("新公告")
                    && changedAnnouncement.appliedStatusMessage == QString::fromUtf8("群公告已更新"),
                "non-empty changed announcement should keep input copy") && ok;

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

    GroupMemberContextMenuPlan localOwnerMenu = GroupManager::memberContextMenuPlan(
        QStringLiteral("member"),
        QStringLiteral("owner"),
        true,
        false,
        QStringLiteral("owner"),
        true,
        owners,
        roles);
    ok = expect(localOwnerMenu.canManageGroup
                    && localOwnerMenu.removeEnabled
                    && !localOwnerMenu.canSetPublicAdmin
                    && localOwnerMenu.removeToolTip.contains(QString::fromUtf8("本地群聊")),
                "local group owner menu should enable local removal with local copy") && ok;

    GroupMemberContextMenuPlan localMemberMenu = GroupManager::memberContextMenuPlan(
        QStringLiteral("member"),
        QStringLiteral("guest"),
        true,
        false,
        QStringLiteral("owner"),
        false,
        owners,
        roles);
    ok = expect(!localMemberMenu.canManageGroup
                    && !localMemberMenu.removeEnabled
                    && localMemberMenu.removeDeniedMessage.contains(QString::fromUtf8("只有群主")),
                "local group non-owner menu should disable removal with owner guidance") && ok;

    GroupMemberContextMenuPlan publicOwnerTargetMenu = GroupManager::memberContextMenuPlan(
        QStringLiteral("admin"),
        QStringLiteral("owner"),
        false,
        true,
        QString(),
        false,
        owners,
        roles);
    ok = expect(publicOwnerTargetMenu.canManageGroup
                    && publicOwnerTargetMenu.canSetPublicAdmin
                    && !publicOwnerTargetMenu.promoteAdminEnabled
                    && publicOwnerTargetMenu.demoteAdminEnabled
                    && publicOwnerTargetMenu.removeEnabled
                    && publicOwnerTargetMenu.serverTargetRole == QStringLiteral("admin"),
                "public owner menu should allow demoting admin targets and removal") && ok;

    GroupMemberContextMenuPlan publicAdminTargetMenu = GroupManager::memberContextMenuPlan(
        QStringLiteral("member"),
        QStringLiteral("admin"),
        false,
        true,
        QString(),
        false,
        owners,
        roles);
    ok = expect(publicAdminTargetMenu.canManageGroup
                    && !publicAdminTargetMenu.canSetPublicAdmin
                    && !publicAdminTargetMenu.promoteAdminEnabled
                    && !publicAdminTargetMenu.demoteAdminEnabled
                    && publicAdminTargetMenu.removeEnabled
                    && publicAdminTargetMenu.promoteAdminToolTip.contains(QString::fromUtf8("只有公共群群主")),
                "public admin menu should allow removal but not role changes") && ok;

    GroupMemberContextMenuPlan publicMemberMenu = GroupManager::memberContextMenuPlan(
        QStringLiteral("admin"),
        QStringLiteral("member"),
        false,
        true,
        QString(),
        false,
        owners,
        roles);
    ok = expect(!publicMemberMenu.canManageGroup
                    && !publicMemberMenu.canSetPublicAdmin
                    && !publicMemberMenu.removeEnabled
                    && publicMemberMenu.removeToolTip.contains(QString::fromUtf8("公共群群主或管理员")),
                "public member menu should disable management actions") && ok;

    GroupMemberContextMenuPlan publicOwnerSelfTargetMenu = GroupManager::memberContextMenuPlan(
        QStringLiteral("owner"),
        QStringLiteral("admin"),
        false,
        true,
        QString(),
        false,
        owners,
        roles);
    ok = expect(publicOwnerSelfTargetMenu.ownerId == QStringLiteral("owner")
                    && !publicOwnerSelfTargetMenu.removeEnabled
                    && publicOwnerSelfTargetMenu.removeToolTip.contains(QString::fromUtf8("群主不能"))
                    && publicOwnerSelfTargetMenu.ownerRemoveDeniedMessage.contains(QString::fromUtf8("群主不能")),
                "public owner target menu should keep owner removal disabled") && ok;

    return ok ? 0 : 1;
}
