#include "notificationpanelmanager.h"

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

    const FriendNoticeActionState emptyFriendState =
        NotificationPanelManager::friendNoticeActionState(QString(), false, false);
    ok = expect(!emptyFriendState.acceptEnabled
                    && emptyFriendState.acceptText == QString::fromUtf8("同意")
                    && !emptyFriendState.copyAllEnabled
                    && emptyFriendState.copyInviteToolTip.contains(QString::fromUtf8("输入 QQ")),
                "empty friend notice state should disable actions and keep neutral copy") && ok;

    const FriendNoticeActionState searchFriendState =
        NotificationPanelManager::friendNoticeActionState(QStringLiteral("search_add:12345"), false, true);
    ok = expect(searchFriendState.acceptEnabled
                    && searchFriendState.acceptText == QString::fromUtf8("搜索并添加")
                    && !searchFriendState.rejectEnabled
                    && searchFriendState.copyMediaGuideEnabled,
                "search friend notice state should enable add path and draft helpers") && ok;

    const FriendNoticeActionState pendingFriendState =
        NotificationPanelManager::friendNoticeActionState(QStringLiteral("12345"), true, false);
    ok = expect(pendingFriendState.acceptEnabled
                    && pendingFriendState.rejectEnabled
                    && pendingFriendState.copyCardEnabled
                    && pendingFriendState.acceptAllEnabled
                    && pendingFriendState.clearEnabled,
                "real friend request state should enable request handling actions") && ok;

    ok = expect(NotificationPanelManager::friendNoticePreviewText(QString(), QStringLiteral("Alice"))
                        == QString::fromUtf8("选择申请后可同意、拒绝、复制名片或回复话术")
                    && NotificationPanelManager::friendNoticePreviewText(QStringLiteral("search_add:9988"), QStringLiteral("Alice")).contains(QStringLiteral("9988"))
                    && NotificationPanelManager::friendNoticePreviewText(QStringLiteral("9988"), QStringLiteral("Alice")).contains(QString::fromUtf8("Alice")),
                "friend notice preview text should cover empty, search and request states") && ok;

    const GroupNoticeActionState emptyGroupState =
        NotificationPanelManager::groupNoticeActionState(QString(), false, false, 0);
    ok = expect(!emptyGroupState.openEnabled
                    && emptyGroupState.hintText.contains(QString::fromUtf8("先选择群聊"))
                    && !emptyGroupState.copyBatchPlanEnabled,
                "empty group notice state should disable actions") && ok;

    const GroupNoticeActionState createGroupState =
        NotificationPanelManager::groupNoticeActionState(QStringLiteral("group_create:项目群"), true, true, 1);
    ok = expect(createGroupState.openEnabled
                    && createGroupState.openText == QString::fromUtf8("创建并进入群聊")
                    && !createGroupState.copyIdEnabled
                    && createGroupState.copyInviteEnabled
                    && createGroupState.copyBatchPlanEnabled,
                "group create state should allow creation but block inspect-only actions") && ok;

    const GroupNoticeActionState realGroupState =
        NotificationPanelManager::groupNoticeActionState(QStringLiteral("local_group_123"), true, false, 2);
    ok = expect(realGroupState.openEnabled
                    && realGroupState.openText == QString::fromUtf8("进入选中群聊")
                    && realGroupState.copyIdEnabled
                    && realGroupState.copyMembersEnabled
                    && realGroupState.copyMediaGuideEnabled,
                "real group state should enable inspection and copy actions") && ok;

    ok = expect(NotificationPanelManager::groupNoticePreviewText(QString(), QStringLiteral(""), QStringLiteral(""), 0, 9).contains(QString::fromUtf8("在线成员9人"))
                    && NotificationPanelManager::groupNoticePreviewText(QStringLiteral("group_create:项目群"), QStringLiteral(""), QStringLiteral(""), 0, 0).contains(QString::fromUtf8("待创建群聊"))
                    && NotificationPanelManager::groupNoticePreviewText(QStringLiteral("local_group_123"), QString::fromUtf8("项目群"), QStringLiteral("123"), 6, 0).contains(QString::fromUtf8("成员6人")),
                "group preview text should cover public, create, and local group modes") && ok;

    ok = expect(NotificationPanelManager::isSearchAddEntryId(QStringLiteral("search_add:1"))
                    && NotificationPanelManager::searchAddEntryTarget(QStringLiteral("search_add:9988")) == QStringLiteral("9988")
                    && NotificationPanelManager::isGroupCreateEntryId(QStringLiteral("group_create:abc"))
                    && NotificationPanelManager::groupCreateEntryName(QStringLiteral("group_create:项目群")) == QString::fromUtf8("项目群"),
                "entry id helpers should parse prefixed ids") && ok;

    return ok ? 0 : 1;
}
