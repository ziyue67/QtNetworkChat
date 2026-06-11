#include "chatcontextmanager.h"

#include <QDateTime>
#include <QStringList>

namespace {
QString copySavePathToolTip(const ChatContextSavedFileState& savedFileState) {
    return savedFileState.hasSavePath
        ? QStringLiteral("复制收到文件在本机的保存路径")
        : QStringLiteral("这条记录还没有保存路径");
}

QString openSavedFileToolTip(const ChatContextSavedFileState& savedFileState) {
    if (!savedFileState.hasSavePath) {
        return QStringLiteral("收到并保存文件后可直接打开");
    }
    if (savedFileState.canOpenFile) {
        return QStringLiteral("打开这条记录关联的本地文件");
    }
    return savedFileState.fileExists
        ? QStringLiteral("当前保存路径不是文件")
        : QStringLiteral("保存文件不存在或文件已移动");
}

QString openSavedFolderToolTip(const ChatContextSavedFileState& savedFileState) {
    if (!savedFileState.hasSavePath) {
        return QStringLiteral("收到并保存文件后可打开目录");
    }
    return savedFileState.canOpenFolder
        ? QStringLiteral("打开这条记录关联文件所在目录")
        : QStringLiteral("保存目录不存在或无权访问");
}

bool isLocalGroupTarget(const QString& privateChatTarget) {
    return privateChatTarget.startsWith(QStringLiteral("local_group_"));
}

QString composerTargetName(const ChatContextComposerState& state) {
    return state.privateChatTarget.isEmpty()
        ? QStringLiteral("公共聊天室")
        : state.targetDisplayName;
}
}

QList<ChatContextMenuActionSpec> ChatContextManager::menuActionSpecs(bool isMediaMessage,
                                                                     const ChatContextSavedFileState& savedFileState) {
    return {
        { QStringLiteral("复制消息"), QStringLiteral("复制整条聊天记录，包括时间和发送者"), QStringLiteral("copy-message"), true, false },
        { QStringLiteral("只复制内容"), QStringLiteral("只复制消息正文内容"), QStringLiteral("copy-plain"), true, false },
        { QStringLiteral("复制发送者"), QStringLiteral("复制这条消息的发送者名称或账号"), QStringLiteral("copy-sender"), true, false },
        { QStringLiteral("引用回复"), QStringLiteral("把这条消息作为引用插入输入框"), QStringLiteral("quote"), true, false },
        { QStringLiteral("转发到输入框"), QStringLiteral("把消息正文整理成转发内容放入输入框"), QStringLiteral("forward"), true, false },
        { QStringLiteral("再次发送"), QStringLiteral("把消息正文重新填入输入框并立即发送"), QStringLiteral("resend"), true, false },
        { QStringLiteral("复制时间"), QStringLiteral("复制这条消息的发送时间"), QStringLiteral("copy-time"), true, false },
        { QStringLiteral("复制媒体卡片"), QStringLiteral("复制当前媒体或文件消息的卡片摘要"), QStringLiteral("copy-media-card"), isMediaMessage, true },
        { QStringLiteral("复制查收话术"), QStringLiteral("复制提醒对方查收文件的简短话术"), QStringLiteral("copy-file-notice"), isMediaMessage, false },
        { QStringLiteral("复制回执话术"), QStringLiteral("复制已收到文件后的回执话术"), QStringLiteral("copy-receipt"), isMediaMessage, false },
        { QStringLiteral("复制保存路径"), copySavePathToolTip(savedFileState), QStringLiteral("copy-save-path"), savedFileState.hasSavePath, false },
        { QStringLiteral("打开文件"), openSavedFileToolTip(savedFileState), QStringLiteral("open-saved-file"), savedFileState.canOpenFile, false },
        { QStringLiteral("打开保存目录"), openSavedFolderToolTip(savedFileState), QStringLiteral("open-save-folder"), savedFileState.canOpenFolder, false },
        { QStringLiteral("复制媒体流程"), QStringLiteral("复制媒体发送、保存和回执的操作流程"), QStringLiteral("copy-media-flow"), isMediaMessage, false },
        { QStringLiteral("@对方回复"), QStringLiteral("把发送者作为 @ 回复对象插入输入框"), QStringLiteral("mention-reply"), true, true }
    };
}

bool ChatContextManager::isMediaMessage(const QString& chatText,
                                        const ChatContextSavedFileState& savedFileState) {
    return savedFileState.hasSavePath
        || chatText.contains(QStringLiteral("文件"))
        || chatText.contains(QStringLiteral("图片"))
        || chatText.contains(QStringLiteral("视频"))
        || chatText.contains(QStringLiteral("媒体"))
        || chatText.contains(QStringLiteral("查收话术"))
        || chatText.contains(QStringLiteral("回执话术"));
}

ChatContextCommandRoute ChatContextManager::commandRoute(const QString& commandId) {
    ChatContextCommandRoute result;
    if (commandId == QLatin1String("copy-message")
            || commandId == QLatin1String("copy-plain")
            || commandId == QLatin1String("copy-sender")
            || commandId == QLatin1String("copy-time")
            || commandId == QLatin1String("copy-media-card")
            || commandId == QLatin1String("copy-file-notice")
            || commandId == QLatin1String("copy-receipt")
            || commandId == QLatin1String("copy-media-flow")) {
        result.handled = true;
        result.kind = ChatContextCommandRoute::Kind::Copy;
        return result;
    }
    if (commandId == QLatin1String("copy-save-path")
            || commandId == QLatin1String("open-saved-file")
            || commandId == QLatin1String("open-save-folder")) {
        result.handled = true;
        result.kind = ChatContextCommandRoute::Kind::SavedFile;
        return result;
    }
    if (commandId == QLatin1String("quote")
            || commandId == QLatin1String("forward")
            || commandId == QLatin1String("resend")
            || commandId == QLatin1String("mention-reply")) {
        result.handled = true;
        result.kind = ChatContextCommandRoute::Kind::Draft;
        return result;
    }
    return result;
}

ChatContextCopyResult ChatContextManager::copyCommandResult(const QString& commandId,
                                                            const QString& chatText,
                                                            const QString& privateChatTarget,
                                                            const QString& targetDisplayName,
                                                            const QString& currentUserId,
                                                            const QString& currentUserName) {
    ChatContextCopyResult result;
    if (commandId == QLatin1String("copy-message")) {
        result.handled = true;
        result.clipboardText = chatText;
        result.statusMessage = QStringLiteral("消息已复制");
        return result;
    }
    if (commandId == QLatin1String("copy-plain")) {
        result.handled = true;
        result.clipboardText = plainContentText(chatText);
        result.statusMessage = QStringLiteral("消息内容已复制");
        return result;
    }
    if (commandId == QLatin1String("copy-sender")) {
        result.handled = true;
        result.clipboardText = senderText(chatText);
        result.statusMessage = QStringLiteral("发送者已复制");
        return result;
    }
    if (commandId == QLatin1String("copy-time")) {
        const QString parsedTime = timeText(chatText);
        result.handled = true;
        result.clipboardText = parsedTime;
        result.statusMessage = QStringLiteral("消息时间已复制: ") + parsedTime;
        return result;
    }
    if (commandId == QLatin1String("copy-media-card")) {
        result.handled = true;
        result.clipboardText = mediaCardText(chatText, privateChatTarget, targetDisplayName, currentUserId, currentUserName);
        result.statusMessage = QStringLiteral("媒体卡片已复制");
        result.timeoutMs = 2200;
        return result;
    }
    if (commandId == QLatin1String("copy-file-notice")) {
        result.handled = true;
        result.clipboardText = mediaNoticeText(chatText, privateChatTarget, targetDisplayName);
        result.statusMessage = QStringLiteral("查收话术已复制");
        result.timeoutMs = 2200;
        return result;
    }
    if (commandId == QLatin1String("copy-receipt")) {
        result.handled = true;
        result.clipboardText = mediaReceiptText(chatText);
        result.statusMessage = QStringLiteral("回执话术已复制");
        result.timeoutMs = 2200;
        return result;
    }
    if (commandId == QLatin1String("copy-media-flow")) {
        result.handled = true;
        result.clipboardText = mediaFlowText(chatText, privateChatTarget, targetDisplayName, currentUserId, currentUserName);
        result.statusMessage = QStringLiteral("媒体流程已复制");
        result.timeoutMs = 2200;
        return result;
    }
    return result;
}

ChatContextSavedFileCommand ChatContextManager::savedFileCommand(const QString& commandId,
                                                                 const ChatContextSavedFileState& savedFileState,
                                                                 const QString& savePath) {
    ChatContextSavedFileCommand result;
    if (commandId == QLatin1String("copy-save-path")) {
        result.handled = true;
        result.action = ChatContextSavedFileCommand::Action::CopySavePath;
        result.canExecute = savedFileState.hasSavePath && !savePath.trimmed().isEmpty();
        result.clipboardText = savePath.trimmed();
        result.missingStatusMessage = QStringLiteral("当前消息没有保存路径");
        result.successStatusMessage = QStringLiteral("保存路径已复制");
        return result;
    }
    if (commandId == QLatin1String("open-saved-file")) {
        result.handled = true;
        result.action = ChatContextSavedFileCommand::Action::OpenSavedFile;
        result.missingStatusMessage = QStringLiteral("当前消息没有可打开的文件");
        result.canExecute = savedFileState.canOpenFile;
        result.successStatusMessage = QStringLiteral("已打开保存文件");
        result.failureStatusMessage = QStringLiteral("保存文件不存在或无法打开");
        result.failureTransferReason = QStringLiteral("receive-open-failed");
        return result;
    }
    if (commandId == QLatin1String("open-save-folder")) {
        result.handled = true;
        result.action = ChatContextSavedFileCommand::Action::OpenSaveFolder;
        result.missingStatusMessage = QStringLiteral("当前消息没有可打开的保存路径");
        result.canExecute = savedFileState.canOpenFolder;
        result.successStatusMessage = QStringLiteral("已打开保存目录");
        result.failureStatusMessage = QStringLiteral("保存目录无法打开");
        return result;
    }
    return result;
}

ChatContextDraftResult ChatContextManager::draftCommandResult(const QString& commandId,
                                                              const QString& chatText,
                                                              const QString& privateChatTarget,
                                                              const QString& targetDisplayName) {
    ChatContextDraftResult result;
    if (commandId == QLatin1String("quote")) {
        result.handled = true;
        result.action = ChatContextDraftResult::Action::SetDraft;
        result.draftText = QString("> %1\n").arg(chatText);
        result.statusMessage = QStringLiteral("已插入引用回复");
        return result;
    }
    if (commandId == QLatin1String("forward")) {
        result.handled = true;
        result.action = ChatContextDraftResult::Action::SetDraft;
        result.draftText = QStringLiteral("转发：") + plainContentText(chatText);
        result.statusMessage = QStringLiteral("已转发到输入框");
        return result;
    }
    if (commandId == QLatin1String("resend")) {
        result.handled = true;
        result.action = ChatContextDraftResult::Action::Resend;
        result.resendText = resendContentText(chatText);
        return result;
    }
    if (commandId == QLatin1String("mention-reply")) {
        const QString name = mentionTargetText(chatText, targetDisplayName.isEmpty() ? privateChatTarget : targetDisplayName);
        result.handled = true;
        result.action = ChatContextDraftResult::Action::SetDraft;
        result.draftText = QString("@%1 ").arg(name);
        result.statusMessage = QString("已插入 @%1 回复").arg(name);
        return result;
    }
    return result;
}

ChatContextComposerCommand ChatContextManager::composerCommand(const QString& commandId,
                                                               const ChatContextComposerState& state) {
    ChatContextComposerCommand result;
    if (commandId == QLatin1String("quick-reply")) {
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::SetDraft;
        result.text = QStringLiteral("收到，我马上看。");
        result.statusMessage = QStringLiteral("已插入快捷语");
        return result;
    }
    if (commandId == QLatin1String("command-card")) {
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::SetDraft;
        result.text = QStringLiteral("/card");
        result.statusMessage = QStringLiteral("已插入快捷指令：/card");
        result.timeoutMs = 1600;
        return result;
    }
    if (commandId == QLatin1String("command-invite")) {
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::SetDraft;
        result.text = QStringLiteral("/invite");
        result.statusMessage = QStringLiteral("已插入快捷指令：/invite");
        result.timeoutMs = 1600;
        return result;
    }
    if (commandId == QLatin1String("command-qq")) {
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::SetDraft;
        result.text = QStringLiteral("/qq");
        result.statusMessage = QStringLiteral("已插入快捷指令：/qq");
        result.timeoutMs = 1600;
        return result;
    }
    if (commandId == QLatin1String("search-friend-template")) {
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::InsertText;
        result.text = QStringLiteral("请在综合搜索里搜索 QQ:%1，确认资料后可以发送好友申请。").arg(state.currentUserId);
        result.statusMessage = QStringLiteral("已插入 QQ 搜索话术");
        return result;
    }
    if (commandId == QLatin1String("add-friend-template")) {
        const QString target = state.privateChatTarget.isEmpty() ? QStringLiteral("你") : state.targetDisplayName;
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::InsertText;
        result.text = QStringLiteral("%1，你好，我是 %2（QQ:%3），方便加个好友继续聊吗？")
            .arg(target, state.currentUserName, state.currentUserId);
        result.statusMessage = QStringLiteral("已插入好友申请话术");
        return result;
    }
    if (commandId == QLatin1String("invite-group-template")) {
        const QString groupName = isLocalGroupTarget(state.privateChatTarget)
            ? state.currentGroupName
            : QStringLiteral("群聊");
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::InsertText;
        result.text = QStringLiteral("我邀请你加入群聊“%1”，进群后可以一起聊天、发图片和传文件。").arg(groupName);
        result.statusMessage = QStringLiteral("已插入入群邀请话术");
        return result;
    }
    if (commandId == QLatin1String("quote-template")) {
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::InsertText;
        result.text = QStringLiteral("> 引用消息\n我的回复：");
        result.statusMessage = QStringLiteral("已插入引用模板");
        return result;
    }
    if (commandId == QLatin1String("friend-card-template")) {
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::InsertText;
        result.text = QStringLiteral("我的QQ名片：%1（%2）").arg(state.currentUserId, state.currentUserName);
        result.statusMessage = QStringLiteral("已插入我的 QQ 名片");
        return result;
    }
    if (commandId == QLatin1String("group-card-template")) {
        QString card;
        if (isLocalGroupTarget(state.privateChatTarget)) {
            card = QStringLiteral("群聊名片：%1 QQ:%2")
                .arg(state.currentGroupName, state.privateChatTarget.mid(QStringLiteral("local_group_").size()));
        } else if (!state.privateChatTarget.isEmpty()) {
            card = QStringLiteral("好友名片：%1 QQ:%2").arg(state.targetDisplayName, state.privateChatTarget);
        } else {
            card = QStringLiteral("公共聊天室 当前QQ:%1").arg(state.currentUserId);
        }
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::InsertText;
        result.text = card;
        result.statusMessage = QStringLiteral("已插入当前会话名片");
        return result;
    }
    if (commandId == QLatin1String("file-template")) {
        const QString target = composerTargetName(state);
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::InsertText;
        result.text = QStringLiteral("我准备发一个文件到 %1，请注意查收。").arg(target);
        result.statusMessage = QStringLiteral("已插入发文件模板");
        return result;
    }
    if (commandId == QLatin1String("image-template")) {
        const QString target = composerTargetName(state);
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::InsertText;
        result.text = QStringLiteral("我准备发图片到 %1，发送后会显示预览卡片。").arg(target);
        result.statusMessage = QStringLiteral("已插入发图片模板");
        return result;
    }
    if (commandId == QLatin1String("video-template")) {
        const QString target = composerTargetName(state);
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::InsertText;
        result.text = QStringLiteral("我准备发视频到 %1，视频会以文件卡片形式发送。").arg(target);
        result.statusMessage = QStringLiteral("已插入发视频模板");
        return result;
    }
    if (commandId == QLatin1String("group-invite-template")) {
        const QString target = isLocalGroupTarget(state.privateChatTarget)
            ? state.currentGroupName
            : QStringLiteral("群聊");
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::InsertText;
        result.text = QStringLiteral("我想邀请你加入 %1，一起在群里沟通。").arg(target);
        result.statusMessage = QStringLiteral("已插入拉群模板");
        return result;
    }
    if (commandId == QLatin1String("current-summary-template")) {
        QString summary;
        if (isLocalGroupTarget(state.privateChatTarget)) {
            summary = QStringLiteral("当前群聊：%1（群号:%2）· 成员%3人 · 我的QQ:%4")
                .arg(state.currentGroupName,
                     state.privateChatTarget.mid(QStringLiteral("local_group_").size()),
                     QString::number(state.currentGroupMemberCount),
                     state.currentUserId);
        } else if (!state.privateChatTarget.isEmpty()) {
            summary = QStringLiteral("当前私聊：%1 · QQ:%2 · %3 · 我的QQ:%4")
                .arg(state.targetDisplayName,
                     state.privateChatTarget,
                     state.currentTargetOnline ? QStringLiteral("在线") : QStringLiteral("离线"),
                     state.currentUserId);
        } else {
            summary = QStringLiteral("公共聊天室 · 我的QQ:%1 · 好友%2人 · 群聊%3个 · 在线成员%4人")
                .arg(state.currentUserId)
                .arg(state.friendCount)
                .arg(state.localGroupCount)
                .arg(state.knownUserCount);
        }
        result.handled = true;
        result.action = ChatContextComposerCommand::Action::InsertText;
        result.text = summary;
        result.statusMessage = QStringLiteral("已插入当前会话摘要");
        return result;
    }
    return result;
}

QList<ChatContextPhraseMenuPlan> ChatContextManager::composerPhraseMenuPlans() {
    return {
        {
            QStringLiteral("常用话术"),
            QStringLiteral("已插入常用话术"),
            {
                QStringLiteral("在吗？"),
                QStringLiteral("收到，我马上看。"),
                QStringLiteral("稍等一下"),
                QStringLiteral("我发你文件"),
                QStringLiteral("我们群里说"),
                QStringLiteral("方便的话加个好友"),
                QStringLiteral("拉我进群聊一下"),
                QStringLiteral("这个 QQ 号是我")
            }
        },
        {
            QStringLiteral("QQ快捷话术"),
            QStringLiteral("已插入 QQ 快捷话术"),
            {
                QStringLiteral("你好，我是通过 QQ 搜索找到你的，方便加个好友吗？"),
                QStringLiteral("我已经发送好友申请了，通过后我们私聊。"),
                QStringLiteral("我建了一个群聊，等下把大家拉进去一起沟通。"),
                QStringLiteral("这个是我的 QQ 号，请复制保存。"),
                QStringLiteral("收到文件后麻烦回复一下。")
            }
        }
    };
}

QList<ChatContextComposerMenuAction> ChatContextManager::composerMenuActions() {
    return {
        { QStringLiteral("插入快捷语"), QStringLiteral("插入一句常用确认回复"), QStringLiteral("quick-reply") },
        { QStringLiteral("插入/card指令"), QStringLiteral("插入 /card 指令，发送时展开为我的 QQ 名片"), QStringLiteral("command-card") },
        { QStringLiteral("插入/invite指令"), QStringLiteral("插入 /invite 指令，发送时展开为入群邀请"), QStringLiteral("command-invite") },
        { QStringLiteral("插入/qq指令"), QStringLiteral("插入 /qq 指令，发送时展开为当前 QQ 号"), QStringLiteral("command-qq") },
        { QStringLiteral("插入QQ搜索话术"), QStringLiteral("插入一段引导对方通过 QQ 搜索加好友的话术"), QStringLiteral("search-friend-template") },
        { QStringLiteral("插入申请话术"), QStringLiteral("插入面向当前会话对象的好友申请话术"), QStringLiteral("add-friend-template") },
        { QStringLiteral("插入入群邀请话术"), QStringLiteral("插入邀请对方加入当前群聊的话术"), QStringLiteral("invite-group-template") },
        { QStringLiteral("插入引用模板"), QStringLiteral("插入引用回复模板，方便补充上下文"), QStringLiteral("quote-template") },
        { QStringLiteral("插入我的QQ名片"), QStringLiteral("插入我的 QQ 名片到输入框"), QStringLiteral("friend-card-template") },
        { QStringLiteral("插入当前会话名片"), QStringLiteral("插入当前私聊或群聊名片"), QStringLiteral("group-card-template") },
        { QStringLiteral("插入发文件模板"), QStringLiteral("插入发送文件前的提醒话术"), QStringLiteral("file-template") },
        { QStringLiteral("插入发图片模板"), QStringLiteral("插入发送图片前的提醒话术"), QStringLiteral("image-template") },
        { QStringLiteral("插入发视频模板"), QStringLiteral("插入发送视频前的提醒话术"), QStringLiteral("video-template") },
        { QStringLiteral("插入拉群模板"), QStringLiteral("插入拉群邀请模板"), QStringLiteral("group-invite-template") },
        { QStringLiteral("插入当前会话摘要"), QStringLiteral("插入当前会话、账号和在线状态摘要"), QStringLiteral("current-summary-template") }
    };
}

QList<ChatContextComposerMenuAction> ChatContextManager::composerRuntimeActions(const ChatContextComposerRuntimeState& state) {
    const QString pasteToolTip = state.hasClipboardText
        ? QStringLiteral("把剪贴板文字插入输入框")
        : QStringLiteral("剪贴板里没有可粘贴的文字");
    const QString pasteSendToolTip = !state.canReachTarget
        ? QStringLiteral("当前已断开，暂不能粘贴并发送到 %1").arg(state.targetDisplayName)
        : (state.hasClipboardText
            ? QStringLiteral("粘贴剪贴板文字后立即发送")
            : QStringLiteral("剪贴板里没有可发送的文字"));
    const QString sendToolTip = !state.canReachTarget
        ? QStringLiteral("当前已断开，暂不能发送到 %1").arg(state.targetDisplayName)
        : (state.hasDraft
            ? QStringLiteral("发送当前输入 · %1 字").arg(state.draftTextLength)
            : QStringLiteral("请输入消息后再发送"));
    const QString clearToolTip = state.hasDraft
        ? QStringLiteral("清空当前输入框内容")
        : QStringLiteral("输入框已经是空的");
    return {
        { QStringLiteral("粘贴"), pasteToolTip, QStringLiteral("composer-paste") },
        { QStringLiteral("粘贴并发送"), pasteSendToolTip, QStringLiteral("composer-paste-send") },
        { QStringLiteral("立即发送"), sendToolTip, QStringLiteral("composer-send") },
        { QStringLiteral("清空输入"), clearToolTip, QStringLiteral("composer-clear") },
        { QStringLiteral("@成员"), QStringLiteral("打开 @ 成员菜单，插入群成员或在线成员提醒"), QStringLiteral("composer-mention") }
    };
}

QString ChatContextManager::plainContentText(const QString& chatText) {
    QString content = chatText.section(']', 2).trimmed();
    if (content.isEmpty()) content = chatText;
    return content;
}

QString ChatContextManager::resendContentText(const QString& chatText) {
    QString content = chatText.section(']', 2).trimmed();
    if (content.isEmpty()) content = chatText.section('>', 1).trimmed();
    if (content.isEmpty()) content = chatText;
    return content;
}

QString ChatContextManager::senderText(const QString& chatText) {
    QString sender = chatText.section('<', 1, 1).section('>', 0, 0).trimmed();
    if (sender.isEmpty()) sender = chatText.section(']', 1, 1).trimmed();
    return sender;
}

QString ChatContextManager::timeText(const QString& chatText) {
    QString parsedTime = chatText.section(']', 0, 0).section('[', 1).trimmed();
    if (parsedTime.isEmpty()) parsedTime = QDateTime::currentDateTime().toString("hh:mm:ss");
    return parsedTime;
}

QString ChatContextManager::mentionTargetText(const QString& chatText,
                                              const QString& fallbackTargetName) {
    QString name = chatText.section('<', 1, 1).section('>', 0, 0).trimmed();
    if (name.isEmpty()) name = fallbackTargetName;
    return name;
}

QString ChatContextManager::mediaTypeFromChatText(const QString& text) {
    if (text.contains(QStringLiteral("视频"))) return QStringLiteral("视频");
    if (text.contains(QStringLiteral("图片"))) return QStringLiteral("图片");
    return QStringLiteral("文件");
}

QString ChatContextManager::mediaCardText(const QString& chatText,
                                          const QString& privateChatTarget,
                                          const QString& targetDisplayName,
                                          const QString& currentUserId,
                                          const QString& currentUserName) {
    QString fileName = chatText.section(" · ", 0, 0).section(']', -1).trimmed();
    if (fileName.isEmpty()) fileName = chatText;

    const QString sender = chatText.section('<', 1, 1).section('>', 0, 0).trimmed();
    return QString("%1卡片\n文件:%2\n会话:%3\n发送者:%4\n我的QQ:%5")
        .arg(mediaTypeFromChatText(chatText),
             fileName,
             privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : targetDisplayName,
             sender.isEmpty() ? currentUserName : sender,
             currentUserId);
}

QString ChatContextManager::mediaNoticeText(const QString& chatText,
                                            const QString& privateChatTarget,
                                            const QString& targetDisplayName) {
    QString fileName = chatText.section(" · ", 0, 0).section(']', -1).trimmed();
    if (fileName.isEmpty()) fileName = QStringLiteral("刚发送的文件");

    const QString target = privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : targetDisplayName;
    return QString("我已发送 %1 到 %2，请注意查收。").arg(fileName, target);
}

QString ChatContextManager::mediaReceiptText(const QString& chatText) {
    QString fileName = chatText.section(" · ", 1, 1).trimmed();
    if (fileName.isEmpty()) fileName = chatText.section(QStringLiteral("已收到"), 1, 1).section(QStringLiteral("，"), 0, 0).trimmed();
    if (fileName.isEmpty()) fileName = QStringLiteral("刚收到的文件");
    return QString("已收到 %1，文件已保存，我会尽快查看。").arg(fileName);
}

QString ChatContextManager::mediaFlowText(const QString& chatText,
                                          const QString& privateChatTarget,
                                          const QString& targetDisplayName,
                                          const QString& currentUserId,
                                          const QString& currentUserName) {
    QString fileName = chatText.section(" · ", 1, 1).trimmed();
    if (fileName.isEmpty()) fileName = chatText.section(" · ", 0, 0).section(']', -1).trimmed();
    if (fileName.isEmpty()) fileName = QStringLiteral("当前媒体文件");

    const QString target = privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : targetDisplayName;
    QStringList rows;
    rows << QString("媒体流程 · 类型:%1 · 文件:%2").arg(mediaTypeFromChatText(chatText), fileName);
    rows << QString("会话:%1 · 我的QQ:%2 · 昵称:%3").arg(target, currentUserId, currentUserName);
    rows << QStringLiteral("1. 发送方点击图片/视频或闪传文件选择媒体");
    rows << QStringLiteral("2. 聊天记录生成媒体卡片和查收话术");
    rows << QStringLiteral("3. 接收方自动保存后可复制回执话术和保存路径");
    rows << QString("查收话术：我已发送 %1 到 %2，请注意查收。").arg(fileName, target);
    rows << QString("回执话术：已收到 %1，文件已保存，我会尽快查看。").arg(fileName);
    return rows.join('\n');
}
