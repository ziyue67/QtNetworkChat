#include "friendmanager.h"

#include <QCoreApplication>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning("%s", message);
        return false;
    }
    return true;
}

ChatUser user(const QString& id, const QString& name, bool online) {
    ChatUser chatUser;
    chatUser.id = id;
    chatUser.name = name;
    chatUser.isOnline = online;
    return chatUser;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    bool ok = true;
    QMap<QString, ChatUser> knownUsers;
    knownUsers.insert(QStringLiteral("1001"), user(QStringLiteral("1001"), QStringLiteral("Alice Online"), true));
    knownUsers.insert(QStringLiteral("1002"), user(QStringLiteral("1002"), QString(), false));
    QMap<QString, QString> friendNames;
    friendNames.insert(QStringLiteral("1001"), QStringLiteral("Alice Remark"));
    friendNames.insert(QStringLiteral("1002"), QStringLiteral("Bob Remark"));
    friendNames.insert(QStringLiteral("1003"), QStringLiteral("Carol Remark"));

    ok = expect(FriendManager::contactDisplayName(QStringLiteral("1001"), knownUsers, friendNames) == QStringLiteral("Alice Online"),
                "known online user name should win over local remark") && ok;
    ok = expect(FriendManager::contactDisplayName(QStringLiteral("1002"), knownUsers, friendNames) == QStringLiteral("Bob Remark"),
                "empty known user name should fall back to remark") && ok;
    ok = expect(FriendManager::contactDisplayName(QStringLiteral("9999"), knownUsers, friendNames) == QStringLiteral("9999"),
                "unknown user should fall back to id") && ok;
    ok = expect(FriendManager::isContactOnline(QStringLiteral("1001"), knownUsers)
                    && !FriendManager::isContactOnline(QStringLiteral("1002"), knownUsers)
                    && !FriendManager::isContactOnline(QStringLiteral("9999"), knownUsers),
                "online state should only be true for known online users") && ok;

    FriendNoticeUiState emptyNotice = FriendManager::noticeUiState(0);
    ok = expect(!emptyNotice.hasPending
                    && emptyNotice.text == QString::fromUtf8("好友通知")
                    && emptyNotice.toolTip.contains(QString::fromUtf8("查看并处理")),
                "empty notice badge should use neutral copy") && ok;
    FriendNoticeUiState pendingNotice = FriendManager::noticeUiState(3);
    ok = expect(pendingNotice.hasPending
                    && pendingNotice.pendingCount == 3
                    && pendingNotice.text == QString::fromUtf8("好友通知 3")
                    && pendingNotice.toolTip.contains(QStringLiteral("3")),
                "pending notice badge should include pending count") && ok;
    FriendNoticeUiState negativeNotice = FriendManager::noticeUiState(-4);
    ok = expect(!negativeNotice.hasPending && negativeNotice.pendingCount == 0,
                "negative pending counts should be clamped to zero") && ok;

    ok = expect(FriendManager::matchesFilter(QStringLiteral("920001"), QStringLiteral("Alice"), QStringLiteral("920"))
                    && FriendManager::matchesFilter(QStringLiteral("920001"), QStringLiteral("Alice"), QStringLiteral("ali"))
                    && FriendManager::matchesFilter(QStringLiteral("920001"), QStringLiteral("Alice"), QString())
                    && !FriendManager::matchesFilter(QStringLiteral("920001"), QStringLiteral("Alice"), QStringLiteral("bob")),
                "friend filter should match id, name, empty filter and reject unrelated text") && ok;

    const QStringList friendIds{QStringLiteral("1001"), QStringLiteral("1003")};
    const QStringList pendingOutgoing{QStringLiteral("1004")};
    ok = expect(FriendManager::relationLabel(QStringLiteral("self"), QStringLiteral("self"), friendIds, pendingOutgoing, knownUsers) == QString::fromUtf8("我"),
                "current user relation should be self") && ok;
    ok = expect(FriendManager::relationLabel(QStringLiteral("1001"), QStringLiteral("self"), friendIds, pendingOutgoing, knownUsers) == QString::fromUtf8("好友在线"),
                "online friend relation should be visible") && ok;
    ok = expect(FriendManager::relationLabel(QStringLiteral("1003"), QStringLiteral("self"), friendIds, pendingOutgoing, knownUsers) == QString::fromUtf8("好友离线"),
                "offline friend relation should be visible") && ok;
    ok = expect(FriendManager::relationLabel(QStringLiteral("1004"), QStringLiteral("self"), friendIds, pendingOutgoing, knownUsers) == QString::fromUtf8("申请中"),
                "pending outgoing relation should be visible") && ok;
    ok = expect(FriendManager::relationLabel(QStringLiteral("1002"), QStringLiteral("self"), friendIds, pendingOutgoing, knownUsers) == QString::fromUtf8("可申请"),
                "offline stranger should be applyable") && ok;

    FriendManagerListUiState allVisibleState = FriendManager::managerListUiState(QStringLiteral("self"),
                                                                                 friendIds,
                                                                                 QStringList{QStringLiteral("local_group_1")},
                                                                                 friendNames,
                                                                                 knownUsers,
                                                                                 QString());
    ok = expect(allVisibleState.visibleCount == 2
                    && allVisibleState.onlineCount == 1
                    && allVisibleState.offlineCount == 1
                    && allVisibleState.subTitle.contains(QString::fromUtf8("可见 2 人"))
                    && allVisibleState.statsText.contains(QString::fromUtf8("本地群 1")),
                "friend manager list state should summarize total and visible contacts") && ok;

    FriendManagerListUiState filteredEmptyState = FriendManager::managerListUiState(QStringLiteral("self"),
                                                                                    friendIds,
                                                                                    QStringList(),
                                                                                    friendNames,
                                                                                    knownUsers,
                                                                                    QStringLiteral("missing"));
    ok = expect(filteredEmptyState.visibleCount == 0
                    && filteredEmptyState.emptyEntryId == QStringLiteral("search_add:missing")
                    && filteredEmptyState.emptyText.contains(QStringLiteral("missing")),
                "friend manager list state should expose search-add placeholder when filter misses") && ok;

    FriendManagerListRenderUiState renderState = FriendManager::managerListRenderUiState(QStringLiteral("self"),
                                                                                          friendIds,
                                                                                          QStringList{QStringLiteral("local_group_1")},
                                                                                          friendNames,
                                                                                          knownUsers,
                                                                                          QString());
    ok = expect(renderState.summary.visibleCount == 2
                    && renderState.entries.size() == 2
                    && renderState.entries.at(0).entryId == QStringLiteral("1001")
                    && renderState.entries.at(0).text.contains(QString::fromUtf8("在线"))
                    && !renderState.entries.at(0).muted
                    && renderState.entries.at(0).rowHeight == 58,
                "friend manager render state should provide visible friend entries with row metadata") && ok;

    FriendManagerListRenderUiState emptyRenderState = FriendManager::managerListRenderUiState(QStringLiteral("self"),
                                                                                               friendIds,
                                                                                               QStringList(),
                                                                                               friendNames,
                                                                                               knownUsers,
                                                                                               QStringLiteral("missing"));
    ok = expect(emptyRenderState.entries.size() == 1
                    && emptyRenderState.entries.at(0).placeholder
                    && emptyRenderState.entries.at(0).muted
                    && emptyRenderState.entries.at(0).entryId == QStringLiteral("search_add:missing")
                    && emptyRenderState.entries.at(0).text.contains(QStringLiteral("missing")),
                "friend manager render state should provide muted placeholder entries for empty filters") && ok;

    ok = expect(FriendManager::managerSelectionPreviewText(QString(), QStringLiteral("Alice"), true, false)
                        == QString::fromUtf8("选择好友后可复制名片、邀请语或邀入群")
                    && FriendManager::managerSelectionPreviewText(QStringLiteral("search_add:9988"), QStringLiteral("Alice"), true, false).contains(QStringLiteral("9988"))
                    && FriendManager::managerSelectionPreviewText(QStringLiteral("1001"), QStringLiteral("Alice"), true, true).contains(QString::fromUtf8("可邀入当前群")),
                "friend manager selection preview should cover empty, search, and friend entries") && ok;
    FriendManagerSelectionPreviewUiState emptyPreviewState =
        FriendManager::managerSelectionPreviewUiState(QString(), QStringLiteral("Alice"), true, false);
    FriendManagerSelectionPreviewUiState searchPreviewState =
        FriendManager::managerSelectionPreviewUiState(QStringLiteral("search_add:9988"), QStringLiteral("Alice"), true, false);
    FriendManagerSelectionPreviewUiState friendPreviewState =
        FriendManager::managerSelectionPreviewUiState(QStringLiteral("1001"), QStringLiteral("Alice"), true, true);
    ok = expect(emptyPreviewState.text == QString::fromUtf8("选择好友后可复制名片、邀请语或邀入群")
                    && searchPreviewState.text.contains(QStringLiteral("9988"))
                    && friendPreviewState.text.contains(QString::fromUtf8("可邀入当前群")),
                "friend manager selection preview ui state should mirror preview text") && ok;

    return ok ? 0 : 1;
}
