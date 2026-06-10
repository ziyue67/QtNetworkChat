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
