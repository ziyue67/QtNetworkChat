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

    return ok ? 0 : 1;
}
