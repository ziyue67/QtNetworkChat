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

    const FriendNoticeListRenderUiState emptyNoticeList =
        NotificationPanelManager::friendNoticeListRenderUiState(QStringList(),
                                                                QMap<QString, QString>(),
                                                                2,
                                                                QString());
    ok = expect(emptyNoticeList.statsText == QString::fromUtf8("待处理 0 个申请 · 已有好友 2 人")
                    && emptyNoticeList.entries.size() == 1
                    && !emptyNoticeList.entries.first().enabled
                    && emptyNoticeList.entries.first().muted
                    && emptyNoticeList.entries.first().text == QString::fromUtf8("暂无新的好友申请"),
                "empty friend notice list should render a muted placeholder") && ok;

    const FriendNoticeListRenderUiState emptySearchNoticeList =
        NotificationPanelManager::friendNoticeListRenderUiState(QStringList(),
                                                                QMap<QString, QString>(),
                                                                2,
                                                                QStringLiteral("9988"));
    ok = expect(emptySearchNoticeList.statsText == QString::fromUtf8("暂无待处理申请 · 可搜索 QQ:9988")
                    && emptySearchNoticeList.entries.size() == 1
                    && emptySearchNoticeList.entries.first().entryId == QStringLiteral("search_add:9988")
                    && emptySearchNoticeList.entries.first().accent,
                "empty filtered friend notice list should render a search-add entry") && ok;

    QMap<QString, QString> applicantNames;
    applicantNames.insert(QStringLiteral("10001"), QString::fromUtf8("小明"));
    applicantNames.insert(QStringLiteral("20002"), QString::fromUtf8("小红"));
    const FriendNoticeListRenderUiState filteredNoticeList =
        NotificationPanelManager::friendNoticeListRenderUiState(
            QStringList{QStringLiteral("10001"), QStringLiteral("20002")},
            applicantNames,
            3,
            QString::fromUtf8("小明"));
    ok = expect(filteredNoticeList.statsText == QString::fromUtf8("待处理 2 个申请 · 匹配 1 个 · 已有好友 3 人")
                    && filteredNoticeList.entries.size() == 1
                    && filteredNoticeList.entries.first().entryId == QStringLiteral("10001")
                    && filteredNoticeList.entries.first().rowHeight == 92
                    && filteredNoticeList.entries.first().text.contains(QString::fromUtf8("小明")),
                "filtered friend notice list should keep matching applicants") && ok;

    const FriendNoticeListRenderUiState missingFilteredNoticeList =
        NotificationPanelManager::friendNoticeListRenderUiState(
            QStringList{QStringLiteral("10001"), QStringLiteral("20002")},
            applicantNames,
            3,
            QStringLiteral("9988"));
    ok = expect(missingFilteredNoticeList.statsText == QString::fromUtf8("待处理 2 个申请 · 匹配 0 个 · 已有好友 3 人")
                    && missingFilteredNoticeList.entries.size() == 1
                    && missingFilteredNoticeList.entries.first().entryId == QStringLiteral("search_add:9988")
                    && missingFilteredNoticeList.entries.first().accent,
                "filtered friend notice list without matches should render search-add entry") && ok;

    GroupNoticeListGroupInput productGroup;
    productGroup.groupId = QStringLiteral("local_group_202606");
    productGroup.groupName = QString::fromUtf8("产品群");
    productGroup.groupNumber = QStringLiteral("202606");
    productGroup.announcement = QString::fromUtf8("讨论发布节奏");
    productGroup.memberCount = 4;
    GroupNoticeListGroupInput designGroup;
    designGroup.groupId = QStringLiteral("local_group_7788");
    designGroup.groupName = QString::fromUtf8("设计群");
    designGroup.groupNumber = QStringLiteral("7788");
    designGroup.announcement = QString::fromUtf8("同步界面方案");
    designGroup.memberCount = 3;

    const GroupNoticeListRenderUiState allGroups =
        NotificationPanelManager::groupNoticeListRenderUiState(
            9,
            QList<GroupNoticeListGroupInput>{productGroup, designGroup},
            QString());
    ok = expect(allGroups.countText == QString::fromUtf8("已加入 3 个群聊")
                    && allGroups.entries.size() == 3
                    && allGroups.entries.first().entryId.isEmpty()
                    && allGroups.entries.first().text.contains(QString::fromUtf8("在线成员：9 人"))
                    && allGroups.entries.at(1).entryId == QStringLiteral("local_group_202606")
                    && allGroups.entries.at(1).rowHeight == 108,
                "group notice list should include public and local groups") && ok;

    const GroupNoticeListRenderUiState filteredGroups =
        NotificationPanelManager::groupNoticeListRenderUiState(
            9,
            QList<GroupNoticeListGroupInput>{productGroup, designGroup},
            QStringLiteral("7788"));
    ok = expect(filteredGroups.countText == QString::fromUtf8("匹配 1 / 3 个群聊")
                    && filteredGroups.entries.size() == 1
                    && filteredGroups.entries.first().entryId == QStringLiteral("local_group_7788")
                    && filteredGroups.entries.first().text.contains(QString::fromUtf8("设计群")),
                "group notice list should filter local groups by number") && ok;

    const GroupNoticeListRenderUiState missingGroups =
        NotificationPanelManager::groupNoticeListRenderUiState(
            9,
            QList<GroupNoticeListGroupInput>{productGroup, designGroup},
            QString::fromUtf8("新群"));
    ok = expect(missingGroups.countText == QString::fromUtf8("匹配 0 / 3 个群聊")
                    && missingGroups.entries.size() == 1
                    && missingGroups.entries.first().entryId == QString::fromUtf8("group_create:新群")
                    && missingGroups.entries.first().accent,
                "group notice list should render create placeholder when no groups match") && ok;

    const QStringList publicMembers =
        NotificationPanelManager::publicGroupMemberIds(QStringLiteral("10001"),
                                                       QStringList{
                                                           QStringLiteral("20002"),
                                                           QStringLiteral("10001"),
                                                           QString(),
                                                           QStringLiteral("30003")
                                                       });
    ok = expect(publicMembers == QStringList({QStringLiteral("10001"),
                                              QStringLiteral("20002"),
                                              QStringLiteral("30003")}),
                "public group member helper should keep current user first and deduplicate online ids") && ok;

    GroupNoticeMemberInput selfMember;
    selfMember.userId = QStringLiteral("10001");
    selfMember.displayName = QString::fromUtf8("我");
    selfMember.self = true;
    GroupNoticeMemberInput onlineMember;
    onlineMember.userId = QStringLiteral("20002");
    onlineMember.displayName = QString::fromUtf8("小红");
    onlineMember.online = true;
    GroupNoticeMemberInput offlineMember;
    offlineMember.userId = QStringLiteral("30003");
    offlineMember.displayName = QString::fromUtf8("小蓝");
    const GroupNoticeMemberCopyState allMemberCopy =
        NotificationPanelManager::groupMemberCopyState(
            QList<GroupNoticeMemberInput>{selfMember, onlineMember, offlineMember},
            false);
    ok = expect(allMemberCopy.rows.size() == 3
                    && allMemberCopy.rows.first() == QString::fromUtf8("QQ:10001 昵称:我 状态:在线")
                    && allMemberCopy.rows.last() == QString::fromUtf8("QQ:30003 昵称:小蓝 状态:离线")
                    && allMemberCopy.copiedStatusMessage == QString::fromUtf8("已复制 3 个群成员"),
                "group member copy state should render all members with online state") && ok;

    const GroupNoticeMemberCopyState onlineMemberCopy =
        NotificationPanelManager::groupMemberCopyState(
            QList<GroupNoticeMemberInput>{selfMember, onlineMember, offlineMember},
            true);
    ok = expect(onlineMemberCopy.rows.size() == 2
                    && onlineMemberCopy.rows.first() == QString::fromUtf8("在线群成员 QQ:10001 昵称:我")
                    && onlineMemberCopy.rows.last() == QString::fromUtf8("在线群成员 QQ:20002 昵称:小红")
                    && onlineMemberCopy.emptyStatusMessage == QString::fromUtf8("当前群聊没有在线成员可复制"),
                "group online member copy state should keep self and online members only") && ok;

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

    const GroupNoticeCopyContext publicContext = NotificationPanelManager::groupNoticeCopyContext(
        QString(),
        QStringLiteral(""),
        9,
        QStringLiteral(""),
        0);
    ok = expect(publicContext.groupName == QString::fromUtf8("公共聊天室")
                    && publicContext.groupNumber == QString::fromUtf8("公共聊天室")
                    && publicContext.memberCount == 9,
                "public group copy context should fall back to public group defaults") && ok;

    ok = expect(NotificationPanelManager::friendNoticeTargetId(QStringLiteral("search_add:9988")) == QStringLiteral("9988")
                    && NotificationPanelManager::friendNoticeTargetId(QString(), QStringLiteral(" 5566 ")) == QStringLiteral("5566")
                    && NotificationPanelManager::isRealFriendNoticeRequestId(QStringLiteral("9988"))
                    && !NotificationPanelManager::isRealFriendNoticeRequestId(QStringLiteral("search_add:9988"))
                    && NotificationPanelManager::visibleFriendNoticeTargetIds(
                        QStringList{QStringLiteral("10001"),
                                    QStringLiteral("search_add:9988"),
                                    QStringLiteral("10001"),
                                    QString()})
                           == QStringList({QStringLiteral("10001"), QStringLiteral("9988")}),
                "friend notice target helpers should normalize search entries and deduplicate visible ids") && ok;

    const GroupNoticeCopyContext createContext = NotificationPanelManager::groupNoticeCopyContext(
        QStringLiteral("group_create:项目群"),
        QStringLiteral("ignored"),
        9,
        QStringLiteral(""),
        0);
    ok = expect(createContext.groupName == QString::fromUtf8("项目群")
                    && createContext.groupNumber == QString::fromUtf8("待创建")
                    && createContext.memberCount == 1,
                "create group copy context should keep create target name") && ok;

    ok = expect(NotificationPanelManager::isInspectableGroupNoticeId(QStringLiteral("local_group_123"))
                    && !NotificationPanelManager::isInspectableGroupNoticeId(QStringLiteral("group_create:项目群"))
                    && !NotificationPanelManager::isInspectableGroupNoticeId(QString())
                    && NotificationPanelManager::uniqueGroupNoticeEntryIds(
                        QStringList{QStringLiteral(""),
                                    QStringLiteral("local_group_123"),
                                    QStringLiteral("local_group_123"),
                                    QStringLiteral("group_create:项目群")})
                           == QStringList({QStringLiteral(""),
                                           QStringLiteral("local_group_123"),
                                           QStringLiteral("group_create:项目群")}),
                "group notice entry helpers should distinguish inspectable entries and keep unique order") && ok;

    const GroupNoticeCopyContext localContext = NotificationPanelManager::groupNoticeCopyContext(
        QStringLiteral("local_group_123"),
        QStringLiteral("ignored"),
        9,
        QString::fromUtf8("项目群"),
        0);
    ok = expect(localContext.groupName == QString::fromUtf8("项目群")
                    && localContext.groupNumber == QStringLiteral("123")
                    && localContext.memberCount == 1,
                "local group copy context should derive group number and guard minimum member count") && ok;

    const QString inviteText = NotificationPanelManager::groupNoticeInviteText(
        QString::fromUtf8("项目群"), QStringLiteral("123"), QString::fromUtf8("小明"), QStringLiteral("10001"));
    ok = expect(inviteText.contains(QString::fromUtf8("项目群"))
                    && inviteText.contains(QStringLiteral("123"))
                    && inviteText.contains(QStringLiteral("10001")),
                "group invite text should include target group and current user") && ok;

    const QString publicGroupCard = NotificationPanelManager::groupNoticeCardText(
        true,
        QStringLiteral(""),
        QStringLiteral(""),
        0,
        QStringLiteral(""),
        QStringLiteral("10001"),
        9);
    const QString localGroupCard = NotificationPanelManager::groupNoticeCardText(
        false,
        QString::fromUtf8("项目群"),
        QStringLiteral("123"),
        6,
        QString::fromUtf8("同步发布计划"),
        QStringLiteral("10001"),
        9);
    ok = expect(publicGroupCard.contains(QString::fromUtf8("公共聊天室"))
                    && publicGroupCard.contains(QString::fromUtf8("在线成员:9"))
                    && localGroupCard.contains(QString::fromUtf8("群聊 QQ:123"))
                    && localGroupCard.contains(QString::fromUtf8("公告:同步发布计划")),
                "group notice card text should cover public and local group variants") && ok;

    ok = expect(NotificationPanelManager::groupNoticeAnnouncementText(true, QStringLiteral("")).contains(QString::fromUtf8("默认群聊"))
                    && NotificationPanelManager::groupNoticeAnnouncementText(false, QString::fromUtf8("同步发布计划"))
                        == QString::fromUtf8("同步发布计划"),
                "group notice announcement helper should centralize public and local copy") && ok;

    const QString mediaPackText = NotificationPanelManager::groupNoticeMediaPackText(
        QString::fromUtf8("项目群"), QStringLiteral("123"), 6, QString::fromUtf8("小明"), QStringLiteral("10001"));
    ok = expect(mediaPackText.contains(QString::fromUtf8("群媒体包"))
                    && mediaPackText.contains(QString::fromUtf8("群成员:6"))
                    && mediaPackText.contains(QString::fromUtf8("入群话术")),
                "group media pack text should include member summary and invite copy") && ok;

    const FriendNoticeDialogChrome friendChrome = NotificationPanelManager::friendNoticeDialogChrome();
    ok = expect(friendChrome.dialogSize == QSize(860, 660)
                    && friendChrome.windowTitle == QString::fromUtf8("好友通知")
                    && friendChrome.searchPlaceholder.contains(QString::fromUtf8("申请人"))
                    && friendChrome.acceptButton.objectName == QStringLiteral("noticePrimaryBtn")
                    && friendChrome.copyMediaGuideButton.text == QString::fromUtf8("复制上传指南"),
                "friend notice dialog chrome should centralize dialog shell and button copy") && ok;

    const GroupNoticeDialogChrome groupChrome = NotificationPanelManager::groupNoticeDialogChrome();
    ok = expect(groupChrome.dialogSize == QSize(880, 620)
                    && groupChrome.windowTitle == QString::fromUtf8("群通知")
                    && groupChrome.countTextTemplate == QString::fromUtf8("已加入 %1 个群聊")
                    && groupChrome.openButton.objectName == QStringLiteral("noticePrimaryBtn")
                    && groupChrome.copyBatchPlanButton.text == QString::fromUtf8("复制群批量媒体计划"),
                "group notice dialog chrome should centralize title, count, and action copy") && ok;

    const QString friendDialogStyle = NotificationPanelManager::friendNoticeDialogStyleSheet();
    const QString groupDialogStyle = NotificationPanelManager::groupNoticeDialogStyleSheet();
    ok = expect(friendDialogStyle.contains(QStringLiteral("QDialog#noticeDialog"))
                    && friendDialogStyle.contains(QStringLiteral("QPushButton#noticeDangerBtn"))
                    && groupDialogStyle.contains(QStringLiteral("QLabel#noticeHint"))
                    && groupDialogStyle.contains(QStringLiteral("QListWidget#noticeList::item:selected")),
                "notice dialog styles should be centralized in notification panel manager") && ok;

    const QString batchPlanText = NotificationPanelManager::groupNoticeBatchPlanText(
        QString::fromUtf8("项目"),
        QStringList{QString::fromUtf8("项目群(群号:123,成员:6,在线:4)")},
        6,
        4,
        QString::fromUtf8("小明"),
        QStringLiteral("10001"));
    ok = expect(batchPlanText.contains(QString::fromUtf8("筛选:项目"))
                    && batchPlanText.contains(QString::fromUtf8("可见群:1"))
                    && batchPlanText.contains(QString::fromUtf8("在线:4")),
                "group batch plan text should summarize visible group counts") && ok;

    ok = expect(NotificationPanelManager::groupNoticeBatchTargetText(
                    QStringLiteral("group_create:项目群"),
                    QStringLiteral(""),
                    QStringLiteral(""),
                    1,
                    1) == QString::fromUtf8("待创建群:项目群")
                    && NotificationPanelManager::groupNoticeBatchTargetText(
                        QString(),
                        QStringLiteral(""),
                        QStringLiteral(""),
                        9,
                        9) == QString::fromUtf8("公共聊天室(成员:9,在线:9)")
                    && NotificationPanelManager::groupNoticeBatchTargetText(
                        QStringLiteral("local_group_123"),
                        QString::fromUtf8("项目群"),
                        QStringLiteral("123"),
                        6,
                        4) == QString::fromUtf8("项目群(群号:123,成员:6,在线:4)"),
                "group batch target text should format create, public, and local groups") && ok;

    const QString mediaGuideText = NotificationPanelManager::groupNoticeMediaGuideText(
        QString::fromUtf8("项目群"), QStringLiteral("123"), 6, QString::fromUtf8("小明"), QStringLiteral("10001"));
    ok = expect(mediaGuideText.contains(QString::fromUtf8("群上传指南"))
                    && mediaGuideText.contains(QString::fromUtf8("群成员:6"))
                    && mediaGuideText.contains(QString::fromUtf8("图片/视频")),
                "group media guide text should cover upload guidance") && ok;

    ok = expect(NotificationPanelManager::isSearchAddEntryId(QStringLiteral("search_add:1"))
                    && NotificationPanelManager::searchAddEntryTarget(QStringLiteral("search_add:9988")) == QStringLiteral("9988")
                    && NotificationPanelManager::isGroupCreateEntryId(QStringLiteral("group_create:abc"))
                    && NotificationPanelManager::groupCreateEntryName(QStringLiteral("group_create:项目群")) == QString::fromUtf8("项目群"),
                "entry id helpers should parse prefixed ids") && ok;

    return ok ? 0 : 1;
}
