#include "chatcontextmanager.h"

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
    const QString privateTarget = QStringLiteral("10001");
    const QString targetName = QString::fromUtf8("好友A");
    const QString currentUserId = QStringLiteral("20002");
    const QString currentUserName = QString::fromUtf8("我自己");
    const QString chatText = QStringLiteral("[12:30:45] <Alice> [图片] report.png · 4 KB");
    ChatContextComposerState composerState;
    composerState.privateChatTarget = privateTarget;
    composerState.targetDisplayName = targetName;
    composerState.currentUserId = currentUserId;
    composerState.currentUserName = currentUserName;
    composerState.currentTargetOnline = true;
    composerState.friendCount = 5;
    composerState.localGroupCount = 2;
    composerState.knownUserCount = 8;

    const QList<ChatContextMenuActionSpec> mediaSpecs = ChatContextManager::menuActionSpecs(true);
    ok = expect(mediaSpecs.size() >= 10
                    && mediaSpecs.first().commandId == QStringLiteral("copy-message")
                    && mediaSpecs.at(7).separatorBefore
                    && mediaSpecs.at(7).commandId == QStringLiteral("copy-media-card")
                    && mediaSpecs.last().separatorBefore
                    && mediaSpecs.last().commandId == QStringLiteral("mention-reply"),
                "menu specs should preserve action ordering and separator boundaries") && ok;

    const QList<ChatContextMenuActionSpec> plainSpecs = ChatContextManager::menuActionSpecs(false);
    ok = expect(!plainSpecs.at(7).enabled
                    && !plainSpecs.at(8).enabled
                    && !plainSpecs.at(13).enabled,
                "media-only menu specs should disable media actions for plain messages") && ok;

    ChatContextSavedFileState missingSavePathState;
    const QList<ChatContextMenuActionSpec> noSavedFileSpecs = ChatContextManager::menuActionSpecs(true, missingSavePathState);
    ok = expect(!noSavedFileSpecs.at(10).enabled
                    && noSavedFileSpecs.at(10).toolTip == QString::fromUtf8("这条记录还没有保存路径")
                    && !noSavedFileSpecs.at(11).enabled
                    && noSavedFileSpecs.at(11).toolTip == QString::fromUtf8("收到并保存文件后可直接打开")
                    && !noSavedFileSpecs.at(12).enabled
                    && noSavedFileSpecs.at(12).toolTip == QString::fromUtf8("收到并保存文件后可打开目录"),
                "saved file actions should expose missing-path guidance when no saved path exists") && ok;

    ChatContextSavedFileState existingSavedFileState;
    existingSavedFileState.hasSavePath = true;
    existingSavedFileState.canOpenFolder = true;
    existingSavedFileState.fileExists = true;
    const QList<ChatContextMenuActionSpec> nonFileSavedSpecs = ChatContextManager::menuActionSpecs(true, existingSavedFileState);
    ok = expect(nonFileSavedSpecs.at(10).enabled
                    && !nonFileSavedSpecs.at(11).enabled
                    && nonFileSavedSpecs.at(11).toolTip == QString::fromUtf8("当前保存路径不是文件")
                    && nonFileSavedSpecs.at(12).enabled,
                "saved file specs should reflect existing non-file paths and accessible folders") && ok;

    ChatContextCommandRoute copyRoute = ChatContextManager::commandRoute(QStringLiteral("copy-message"));
    ok = expect(copyRoute.handled
                    && copyRoute.kind == ChatContextCommandRoute::Kind::Copy,
                "copy-message should route through copy command handling") && ok;

    ChatContextCommandRoute savedFileRoute = ChatContextManager::commandRoute(QStringLiteral("open-saved-file"));
    ok = expect(savedFileRoute.handled
                    && savedFileRoute.kind == ChatContextCommandRoute::Kind::SavedFile,
                "open-saved-file should route through saved-file command handling") && ok;

    ChatContextCommandRoute draftRoute = ChatContextManager::commandRoute(QStringLiteral("quote"));
    ok = expect(draftRoute.handled
                    && draftRoute.kind == ChatContextCommandRoute::Kind::Draft,
                "quote should route through draft command handling") && ok;

    ChatContextCommandRoute unknownRoute = ChatContextManager::commandRoute(QStringLiteral("noop"));
    ok = expect(!unknownRoute.handled
                    && unknownRoute.kind == ChatContextCommandRoute::Kind::None,
                "unknown command should remain unhandled in command routing") && ok;

    ChatContextCopyResult messageCopy = ChatContextManager::copyCommandResult(QStringLiteral("copy-message"),
                                                                              chatText,
                                                                              privateTarget,
                                                                              targetName,
                                                                              currentUserId,
                                                                              currentUserName);
    ok = expect(messageCopy.handled
                    && messageCopy.clipboardText == chatText
                    && messageCopy.statusMessage == QString::fromUtf8("消息已复制"),
                "copy message should return raw text and status") && ok;

    ChatContextCopyResult plainCopy = ChatContextManager::copyCommandResult(QStringLiteral("copy-plain"),
                                                                            chatText,
                                                                            privateTarget,
                                                                            targetName,
                                                                            currentUserId,
                                                                            currentUserName);
    ok = expect(plainCopy.handled
                    && plainCopy.clipboardText.contains(QStringLiteral("report.png"))
                    && !plainCopy.clipboardText.isEmpty(),
                "copy plain should extract chat content") && ok;

    ChatContextCopyResult mediaCard = ChatContextManager::copyCommandResult(QStringLiteral("copy-media-card"),
                                                                            chatText,
                                                                            privateTarget,
                                                                            targetName,
                                                                            currentUserId,
                                                                            currentUserName);
    ok = expect(mediaCard.handled
                    && mediaCard.timeoutMs == 2200
                    && mediaCard.clipboardText.contains(QString::fromUtf8("图片卡片"))
                    && mediaCard.clipboardText.contains(targetName),
                "media card copy should include session display and extended timeout") && ok;

    ChatContextCopyResult flowCopy = ChatContextManager::copyCommandResult(QStringLiteral("copy-media-flow"),
                                                                           chatText,
                                                                           QString(),
                                                                           QStringLiteral("公共聊天室"),
                                                                           currentUserId,
                                                                           currentUserName);
    ok = expect(flowCopy.handled
                    && flowCopy.clipboardText.contains(QString::fromUtf8("媒体流程"))
                    && flowCopy.clipboardText.contains(QString::fromUtf8("公共聊天室")),
                "media flow copy should fall back to public chat") && ok;

    ChatContextDraftResult quoteDraft = ChatContextManager::draftCommandResult(QStringLiteral("quote"),
                                                                               chatText,
                                                                               privateTarget,
                                                                               targetName);
    ok = expect(quoteDraft.handled
                    && quoteDraft.action == ChatContextDraftResult::Action::SetDraft
                    && quoteDraft.draftText.startsWith(QStringLiteral("> "))
                    && quoteDraft.statusMessage == QString::fromUtf8("已插入引用回复"),
                "quote command should create a quoted draft") && ok;

    ChatContextDraftResult resendDraft = ChatContextManager::draftCommandResult(QStringLiteral("resend"),
                                                                                chatText,
                                                                                privateTarget,
                                                                                targetName);
    ok = expect(resendDraft.handled
                    && resendDraft.action == ChatContextDraftResult::Action::Resend
                    && resendDraft.resendText.contains(QStringLiteral("report.png")),
                "resend command should expose resend text") && ok;

    ChatContextDraftResult mentionDraft = ChatContextManager::draftCommandResult(QStringLiteral("mention-reply"),
                                                                                 QStringLiteral("[12:31:00] 系统消息"),
                                                                                 privateTarget,
                                                                                 targetName);
    ok = expect(mentionDraft.handled
                    && mentionDraft.action == ChatContextDraftResult::Action::SetDraft
                    && mentionDraft.draftText == QString::fromUtf8("@好友A ")
                    && mentionDraft.statusMessage.contains(QString::fromUtf8("@好友A")),
                "mention reply should fall back to current target display name") && ok;

    ok = expect(ChatContextManager::senderText(chatText) == QStringLiteral("Alice")
                    && ChatContextManager::mediaTypeFromChatText(chatText) == QString::fromUtf8("图片")
                    && ChatContextManager::mediaReceiptText(QString::fromUtf8("已收到 图片 · report.png，文件已保存")).contains(QStringLiteral("report.png")),
                "chat context parsing helpers should extract sender, media type, and receipt text") && ok;

    ChatContextComposerCommand quickReplyCommand = ChatContextManager::composerCommand(QStringLiteral("quick-reply"),
                                                                                       composerState);
    ok = expect(quickReplyCommand.handled
                    && quickReplyCommand.action == ChatContextComposerCommand::Action::SetDraft
                    && quickReplyCommand.text == QString::fromUtf8("收到，我马上看。")
                    && quickReplyCommand.statusMessage == QString::fromUtf8("已插入快捷语"),
                "quick reply command should centralize set-draft behavior") && ok;

    ChatContextComposerCommand searchFriendCommand = ChatContextManager::composerCommand(QStringLiteral("search-friend-template"),
                                                                                         composerState);
    ok = expect(searchFriendCommand.handled
                    && searchFriendCommand.action == ChatContextComposerCommand::Action::InsertText
                    && searchFriendCommand.text.contains(currentUserId)
                    && searchFriendCommand.statusMessage == QString::fromUtf8("已插入 QQ 搜索话术"),
                "search friend command should centralize QQ search wording") && ok;

    ChatContextComposerCommand groupCardCommand = ChatContextManager::composerCommand(QStringLiteral("group-card-template"),
                                                                                      composerState);
    ok = expect(groupCardCommand.handled
                    && groupCardCommand.action == ChatContextComposerCommand::Action::InsertText
                    && groupCardCommand.text == QString::fromUtf8("好友名片：好友A QQ:10001")
                    && groupCardCommand.statusMessage == QString::fromUtf8("已插入当前会话名片"),
                "group card command should render current private chat card") && ok;

    ChatContextComposerState localGroupComposerState = composerState;
    localGroupComposerState.privateChatTarget = QStringLiteral("local_group_7788");
    localGroupComposerState.targetDisplayName = QString::fromUtf8("项目群");
    localGroupComposerState.currentGroupName = QString::fromUtf8("项目群");
    localGroupComposerState.currentGroupMemberCount = 6;

    ChatContextComposerCommand inviteGroupCommand = ChatContextManager::composerCommand(QStringLiteral("invite-group-template"),
                                                                                        localGroupComposerState);
    ok = expect(inviteGroupCommand.handled
                    && inviteGroupCommand.action == ChatContextComposerCommand::Action::InsertText
                    && inviteGroupCommand.text.contains(QString::fromUtf8("项目群"))
                    && inviteGroupCommand.statusMessage == QString::fromUtf8("已插入入群邀请话术"),
                "invite group command should use current local group name") && ok;

    ChatContextComposerCommand currentSummaryCommand = ChatContextManager::composerCommand(QStringLiteral("current-summary-template"),
                                                                                           localGroupComposerState);
    ok = expect(currentSummaryCommand.handled
                    && currentSummaryCommand.action == ChatContextComposerCommand::Action::InsertText
                    && currentSummaryCommand.text.contains(QString::fromUtf8("当前群聊：项目群"))
                    && currentSummaryCommand.text.contains(QStringLiteral("成员6人")),
                "current summary command should render local group summary") && ok;

    ChatContextComposerState publicComposerState = composerState;
    publicComposerState.privateChatTarget.clear();
    publicComposerState.targetDisplayName = QString::fromUtf8("公共聊天室");

    ChatContextComposerCommand publicSummaryCommand = ChatContextManager::composerCommand(QStringLiteral("current-summary-template"),
                                                                                          publicComposerState);
    ok = expect(publicSummaryCommand.handled
                    && publicSummaryCommand.action == ChatContextComposerCommand::Action::InsertText
                    && publicSummaryCommand.text.contains(QString::fromUtf8("公共聊天室"))
                    && publicSummaryCommand.text.contains(QStringLiteral("好友5人"))
                    && publicSummaryCommand.text.contains(QStringLiteral("群聊2个"))
                    && publicSummaryCommand.text.contains(QStringLiteral("在线成员8人")),
                "current summary command should render public chat aggregate summary") && ok;

    const QList<ChatContextPhraseMenuPlan> phrasePlans = ChatContextManager::composerPhraseMenuPlans();
    ok = expect(phrasePlans.size() == 2
                    && phrasePlans.first().title == QString::fromUtf8("常用话术")
                    && phrasePlans.first().insertedStatusMessage == QString::fromUtf8("已插入常用话术")
                    && phrasePlans.first().phrases.contains(QString::fromUtf8("收到，我马上看。"))
                    && phrasePlans.last().title == QString::fromUtf8("QQ快捷话术")
                    && phrasePlans.last().insertedStatusMessage == QString::fromUtf8("已插入 QQ 快捷话术")
                    && phrasePlans.last().phrases.contains(QString::fromUtf8("收到文件后麻烦回复一下。")),
                "composer phrase menu plans should centralize phrase menus and status copy") && ok;

    const QList<ChatContextComposerMenuAction> composerMenuActions = ChatContextManager::composerMenuActions();
    ok = expect(composerMenuActions.size() == 15
                    && composerMenuActions.first().title == QString::fromUtf8("插入快捷语")
                    && composerMenuActions.first().toolTip == QString::fromUtf8("插入一句常用确认回复")
                    && composerMenuActions.first().commandId == QStringLiteral("quick-reply")
                    && composerMenuActions.at(7).commandId == QStringLiteral("quote-template")
                    && composerMenuActions.last().title == QString::fromUtf8("插入当前会话摘要")
                    && composerMenuActions.last().commandId == QStringLiteral("current-summary-template"),
                "composer menu actions should centralize menu titles, tooltips, and command ids") && ok;

    ChatContextComposerRuntimeState runtimeState;
    runtimeState.hasDraft = true;
    runtimeState.hasClipboardText = false;
    runtimeState.canReachTarget = false;
    runtimeState.draftTextLength = 12;
    runtimeState.targetDisplayName = QString::fromUtf8("好友A");
    const QList<ChatContextComposerMenuAction> runtimeActions = ChatContextManager::composerRuntimeActions(runtimeState);
    ok = expect(runtimeActions.size() == 5
                    && runtimeActions.at(0).commandId == QStringLiteral("composer-paste")
                    && runtimeActions.at(0).toolTip == QString::fromUtf8("剪贴板里没有可粘贴的文字")
                    && runtimeActions.at(1).commandId == QStringLiteral("composer-paste-send")
                    && runtimeActions.at(1).toolTip.contains(QString::fromUtf8("当前已断开"))
                    && runtimeActions.at(2).commandId == QStringLiteral("composer-send")
                    && runtimeActions.at(2).toolTip.contains(QString::fromUtf8("当前已断开"))
                    && runtimeActions.at(3).commandId == QStringLiteral("composer-clear")
                    && runtimeActions.at(3).toolTip == QString::fromUtf8("清空当前输入框内容")
                    && runtimeActions.at(4).commandId == QStringLiteral("composer-mention"),
                "composer runtime actions should centralize enabled-state tooltip copy") && ok;

    ChatContextSavedFileCommand copySavePathCommand = ChatContextManager::savedFileCommand(QStringLiteral("copy-save-path"),
                                                                                           missingSavePathState,
                                                                                           QString());
    ok = expect(copySavePathCommand.handled
                    && copySavePathCommand.action == ChatContextSavedFileCommand::Action::CopySavePath
                    && !copySavePathCommand.canExecute
                    && copySavePathCommand.missingStatusMessage == QString::fromUtf8("当前消息没有保存路径"),
                "copy save path command should map to saved file copy action and missing status") && ok;

    ChatContextSavedFileCommand openSavedFileCommand = ChatContextManager::savedFileCommand(QStringLiteral("open-saved-file"),
                                                                                            missingSavePathState,
                                                                                            QString());
    ok = expect(openSavedFileCommand.handled
                    && openSavedFileCommand.action == ChatContextSavedFileCommand::Action::OpenSavedFile
                    && !openSavedFileCommand.canExecute
                    && openSavedFileCommand.missingStatusMessage == QString::fromUtf8("当前消息没有可打开的文件")
                    && openSavedFileCommand.failureTransferReason == QStringLiteral("receive-open-failed"),
                "open saved file command should expose missing-file status") && ok;

    ChatContextSavedFileState openableSavedFileState;
    openableSavedFileState.hasSavePath = true;
    openableSavedFileState.canOpenFile = true;
    openableSavedFileState.canOpenFolder = true;
    openableSavedFileState.fileExists = true;
    ChatContextSavedFileCommand executableCopyCommand = ChatContextManager::savedFileCommand(QStringLiteral("copy-save-path"),
                                                                                             openableSavedFileState,
                                                                                             QStringLiteral("C:/Downloads/report.zip"));
    ok = expect(executableCopyCommand.handled
                    && executableCopyCommand.canExecute
                    && executableCopyCommand.clipboardText == QStringLiteral("C:/Downloads/report.zip")
                    && executableCopyCommand.successStatusMessage == QString::fromUtf8("保存路径已复制"),
                "copy save path command should carry clipboard text and success status when executable") && ok;

    ChatContextSavedFileCommand executableOpenFileCommand = ChatContextManager::savedFileCommand(QStringLiteral("open-saved-file"),
                                                                                                 openableSavedFileState,
                                                                                                 QStringLiteral("C:/Downloads/report.zip"));
    ok = expect(executableOpenFileCommand.handled
                    && executableOpenFileCommand.canExecute
                    && executableOpenFileCommand.successStatusMessage == QString::fromUtf8("已打开保存文件")
                    && executableOpenFileCommand.failureStatusMessage == QString::fromUtf8("保存文件不存在或无法打开")
                    && executableOpenFileCommand.failureTransferReason == QStringLiteral("receive-open-failed"),
                "open saved file command should centralize success/failure copy and transfer reason") && ok;

    ChatContextSavedFileCommand executableOpenFolderCommand = ChatContextManager::savedFileCommand(QStringLiteral("open-save-folder"),
                                                                                                   openableSavedFileState,
                                                                                                   QStringLiteral("C:/Downloads/report.zip"));
    ok = expect(executableOpenFolderCommand.handled
                    && executableOpenFolderCommand.canExecute
                    && executableOpenFolderCommand.successStatusMessage == QString::fromUtf8("已打开保存目录")
                    && executableOpenFolderCommand.failureStatusMessage == QString::fromUtf8("保存目录无法打开"),
                "open save folder command should centralize folder success/failure copy") && ok;

    ChatContextSavedFileCommand unknownSavedFileCommand = ChatContextManager::savedFileCommand(QStringLiteral("noop"),
                                                                                               missingSavePathState,
                                                                                               QString());
    ok = expect(!unknownSavedFileCommand.handled
                    && unknownSavedFileCommand.action == ChatContextSavedFileCommand::Action::None,
                "unknown saved file command should remain unhandled") && ok;

    return ok ? 0 : 1;
}
