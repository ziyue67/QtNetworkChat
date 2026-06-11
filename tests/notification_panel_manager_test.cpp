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

    const QString inviteText = NotificationPanelManager::groupNoticeInviteText(
        QString::fromUtf8("项目群"), QStringLiteral("123"), QString::fromUtf8("小明"), QStringLiteral("10001"));
    ok = expect(inviteText.contains(QString::fromUtf8("项目群"))
                    && inviteText.contains(QStringLiteral("123"))
                    && inviteText.contains(QStringLiteral("10001")),
                "group invite text should include target group and current user") && ok;

    const QString mediaPackText = NotificationPanelManager::groupNoticeMediaPackText(
        QString::fromUtf8("项目群"), QStringLiteral("123"), 6, QString::fromUtf8("小明"), QStringLiteral("10001"));
    ok = expect(mediaPackText.contains(QString::fromUtf8("群媒体包"))
                    && mediaPackText.contains(QString::fromUtf8("群成员:6"))
                    && mediaPackText.contains(QString::fromUtf8("入群话术")),
                "group media pack text should include member summary and invite copy") && ok;

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
