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

    FriendManagerContactCopyInput aliceCopy;
    aliceCopy.userId = QStringLiteral("1001");
    aliceCopy.displayName = QStringLiteral("Alice");
    aliceCopy.online = true;
    FriendManagerContactCopyInput carolCopy;
    carolCopy.userId = QStringLiteral("1003");
    carolCopy.displayName = QStringLiteral("Carol");
    carolCopy.online = false;
    const FriendManagerContactCopyState allCopy =
        FriendManager::managerContactCopyState(QList<FriendManagerContactCopyInput>{aliceCopy, carolCopy},
                                               false);
    ok = expect(allCopy.rows.size() == 2
                    && allCopy.rows.first() == QString::fromUtf8("QQ:1001 昵称:Alice 状态:在线")
                    && allCopy.rows.last() == QString::fromUtf8("QQ:1003 昵称:Carol 状态:离线")
                    && allCopy.copiedStatusMessage == QString::fromUtf8("已复制 2 个可见好友"),
                "friend manager copy state should render visible friends with online status") && ok;

    const FriendManagerContactCopyState onlineCopy =
        FriendManager::managerContactCopyState(QList<FriendManagerContactCopyInput>{aliceCopy, carolCopy},
                                               true);
    ok = expect(onlineCopy.rows.size() == 1
                    && onlineCopy.rows.first() == QString::fromUtf8("在线好友 QQ:1001 昵称:Alice")
                    && onlineCopy.emptyStatusMessage == QString::fromUtf8("当前筛选没有在线好友"),
                "friend manager online copy state should keep online friends only") && ok;

    const FriendManagerContactCopyState emptyOnlineCopy =
        FriendManager::managerContactCopyState(QList<FriendManagerContactCopyInput>{carolCopy},
                                               true);
    ok = expect(emptyOnlineCopy.rows.isEmpty()
                    && emptyOnlineCopy.copiedStatusMessage == QString::fromUtf8("已复制 0 个在线好友"),
                "friend manager online copy state should expose empty guidance when no rows remain") && ok;

    GlobalSearchResultCopyInput searchAddResult;
    searchAddResult.entryId = QStringLiteral("search_add:9988");
    GlobalSearchResultCopyInput groupResult;
    groupResult.entryId = QStringLiteral("local_group_7788");
    groupResult.displayName = QString::fromUtf8("产品群");
    groupResult.localGroup = true;
    groupResult.memberCount = 5;
    GlobalSearchResultCopyInput onlineSearchResult;
    onlineSearchResult.entryId = QStringLiteral("1001");
    onlineSearchResult.displayName = QStringLiteral("Alice");
    onlineSearchResult.online = true;
    onlineSearchResult.friendContact = true;
    GlobalSearchResultCopyInput offlineSearchResult;
    offlineSearchResult.entryId = QStringLiteral("1003");
    offlineSearchResult.displayName = QStringLiteral("Carol");
    const GlobalSearchResultCopyState searchCopy =
        FriendManager::globalSearchResultCopyState(
            QList<GlobalSearchResultCopyInput>{searchAddResult, groupResult, onlineSearchResult, offlineSearchResult},
            false);
    ok = expect(searchCopy.rows.size() == 4
                    && searchCopy.rows.at(0) == QStringLiteral("搜索申请 QQ:9988")
                    && searchCopy.rows.at(1) == QString::fromUtf8("群聊 QQ:7788 名称:产品群")
                    && searchCopy.rows.at(2) == QString::fromUtf8("QQ:1001 昵称:Alice 状态:在线")
                    && searchCopy.copiedStatusMessage == QString::fromUtf8("已复制 4 条搜索结果"),
                "global search copy state should render search, group, and user rows") && ok;

    const GlobalSearchResultCopyState onlineSearchCopy =
        FriendManager::globalSearchResultCopyState(
            QList<GlobalSearchResultCopyInput>{searchAddResult, groupResult, onlineSearchResult, offlineSearchResult},
            true);
    ok = expect(onlineSearchCopy.rows.size() == 1
                    && onlineSearchCopy.rows.first() == QString::fromUtf8("在线搜索结果 QQ:1001 昵称:Alice 关系:好友")
                    && onlineSearchCopy.emptyStatusMessage == QString::fromUtf8("当前搜索结果没有在线用户"),
                "global search online copy state should skip search/group/offline rows") && ok;

    QMap<QString, ChatUser> quickKnownUsers;
    quickKnownUsers.insert(QStringLiteral("self"), user(QStringLiteral("self"), QStringLiteral("Me"), true));
    quickKnownUsers.insert(QStringLiteral("1001"), user(QStringLiteral("1001"), QStringLiteral("Alice"), true));
    quickKnownUsers.insert(QStringLiteral("1002"), user(QStringLiteral("1002"), QStringLiteral("Bob"), true));
    quickKnownUsers.insert(QStringLiteral("1003"), user(QStringLiteral("1003"), QStringLiteral("Carol"), true));
    quickKnownUsers.insert(QStringLiteral("1004"), user(QStringLiteral("1004"), QStringLiteral("Dora"), true));
    quickKnownUsers.insert(QStringLiteral("1005"), user(QStringLiteral("1005"), QStringLiteral("Evan"), true));
    quickKnownUsers.insert(QStringLiteral("1006"), user(QStringLiteral("1006"), QStringLiteral("Frank"), true));
    quickKnownUsers.insert(QStringLiteral("2001"), user(QStringLiteral("2001"), QStringLiteral("Pending"), true));
    QMap<QString, QString> quickFriendNames;
    quickFriendNames.insert(QStringLiteral("9988"), QStringLiteral("Fallback Friend"));
    const FriendQuickAddSuggestionUiState quickAddAll =
        FriendManager::quickAddSuggestionUiState(QStringLiteral("self"),
                                                 QStringLiteral("Tester"),
                                                 QStringList{QStringLiteral("1006")},
                                                 QStringList{QStringLiteral("2001")},
                                                 quickKnownUsers,
                                                 quickFriendNames,
                                                 QString(),
                                                 3);
    ok = expect(quickAddAll.onlineCandidates == 5
                    && quickAddAll.pendingCandidates == 1
                    && quickAddAll.visibleCount == 5
                    && quickAddAll.statsText.contains(QString::fromUtf8("在线推荐 5 人"))
                    && quickAddAll.statsText.contains(QString::fromUtf8("申请中 1 人"))
                    && quickAddAll.entries.size() == 4
                    && quickAddAll.entries.at(0).entryId == QStringLiteral("1001")
                    && quickAddAll.entries.at(3).placeholder
                    && !quickAddAll.entries.at(3).enabled
                    && quickAddAll.previewText.contains(QString::fromUtf8("Alice"))
                    && quickAddAll.previewText.contains(QStringLiteral("Tester")),
                "quick add suggestions should summarize candidates, cap visible rows, and render preview") && ok;

    const FriendQuickAddSuggestionUiState quickAddFiltered =
        FriendManager::quickAddSuggestionUiState(QStringLiteral("self"),
                                                 QStringLiteral("Tester"),
                                                 QStringList(),
                                                 QStringList(),
                                                 quickKnownUsers,
                                                 quickFriendNames,
                                                 QStringLiteral("1002"),
                                                 5);
    ok = expect(quickAddFiltered.visibleCount == 1
                    && quickAddFiltered.entries.size() == 1
                    && quickAddFiltered.entries.first().entryId == QStringLiteral("1002")
                    && quickAddFiltered.statsText.contains(QStringLiteral("QQ:1002"))
                    && quickAddFiltered.previewText.contains(QString::fromUtf8("Bob")),
                "quick add filtered suggestions should match account id and preview first result") && ok;

    const FriendQuickAddSuggestionUiState quickAddSearchOnly =
        FriendManager::quickAddSuggestionUiState(QStringLiteral("self"),
                                                 QStringLiteral("Tester"),
                                                 QStringList(),
                                                 QStringList(),
                                                 quickKnownUsers,
                                                 quickFriendNames,
                                                 QStringLiteral("9988"),
                                                 5);
    ok = expect(quickAddSearchOnly.entries.size() == 1
                    && quickAddSearchOnly.entries.first().placeholder
                    && quickAddSearchOnly.entries.first().enabled
                    && quickAddSearchOnly.entries.first().entryId == QStringLiteral("9988")
                    && quickAddSearchOnly.previewText.contains(QString::fromUtf8("Fallback Friend")),
                "quick add unmatched filter should create selectable search placeholder and use display fallback") && ok;

    const FriendQuickAddSuggestionUiState quickAddEmpty =
        FriendManager::quickAddSuggestionUiState(QStringLiteral("self"),
                                                 QStringLiteral("Tester"),
                                                 QStringList(),
                                                 QStringList(),
                                                 QMap<QString, ChatUser>(),
                                                 QMap<QString, QString>(),
                                                 QString(),
                                                 5);
    ok = expect(quickAddEmpty.entries.size() == 1
                    && quickAddEmpty.entries.first().placeholder
                    && !quickAddEmpty.entries.first().enabled
                    && quickAddEmpty.previewText.contains(QString::fromUtf8("待搜索好友")),
                "quick add empty state should keep non-selectable guidance and neutral preview") && ok;

    return ok ? 0 : 1;
}
