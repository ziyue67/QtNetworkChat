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

ChatContextSavedFileCommand ChatContextManager::savedFileCommand(const QString& commandId) {
    ChatContextSavedFileCommand result;
    if (commandId == QLatin1String("copy-save-path")) {
        result.handled = true;
        result.action = ChatContextSavedFileCommand::Action::CopySavePath;
        result.missingStatusMessage = QStringLiteral("当前消息没有保存路径");
        return result;
    }
    if (commandId == QLatin1String("open-saved-file")) {
        result.handled = true;
        result.action = ChatContextSavedFileCommand::Action::OpenSavedFile;
        result.missingStatusMessage = QStringLiteral("当前消息没有可打开的文件");
        return result;
    }
    if (commandId == QLatin1String("open-save-folder")) {
        result.handled = true;
        result.action = ChatContextSavedFileCommand::Action::OpenSaveFolder;
        result.missingStatusMessage = QStringLiteral("当前消息没有可打开的保存路径");
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
