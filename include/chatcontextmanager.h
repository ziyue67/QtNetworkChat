#ifndef CHATCONTEXTMANAGER_H
#define CHATCONTEXTMANAGER_H

#include <QList>
#include <QString>

struct ChatContextCopyResult {
    bool handled = false;
    QString clipboardText;
    QString statusMessage;
    int timeoutMs = 1800;
};

struct ChatContextDraftResult {
    enum class Action {
        None,
        SetDraft,
        Resend
    };

    bool handled = false;
    Action action = Action::None;
    QString draftText;
    QString resendText;
    QString statusMessage;
    int timeoutMs = 1400;
};

struct ChatContextMenuActionSpec {
    QString title;
    QString toolTip;
    QString commandId;
    bool enabled = true;
    bool separatorBefore = false;
};

class ChatContextManager {
public:
    static QList<ChatContextMenuActionSpec> menuActionSpecs(bool isMediaMessage);
    static ChatContextCopyResult copyCommandResult(const QString& commandId,
                                                   const QString& chatText,
                                                   const QString& privateChatTarget,
                                                   const QString& targetDisplayName,
                                                   const QString& currentUserId,
                                                   const QString& currentUserName);
    static ChatContextDraftResult draftCommandResult(const QString& commandId,
                                                     const QString& chatText,
                                                     const QString& privateChatTarget,
                                                     const QString& targetDisplayName);
    static QString plainContentText(const QString& chatText);
    static QString resendContentText(const QString& chatText);
    static QString senderText(const QString& chatText);
    static QString timeText(const QString& chatText);
    static QString mentionTargetText(const QString& chatText,
                                     const QString& fallbackTargetName);
    static QString mediaTypeFromChatText(const QString& text);
    static QString mediaCardText(const QString& chatText,
                                 const QString& privateChatTarget,
                                 const QString& targetDisplayName,
                                 const QString& currentUserId,
                                 const QString& currentUserName);
    static QString mediaNoticeText(const QString& chatText,
                                   const QString& privateChatTarget,
                                   const QString& targetDisplayName);
    static QString mediaReceiptText(const QString& chatText);
    static QString mediaFlowText(const QString& chatText,
                                 const QString& privateChatTarget,
                                 const QString& targetDisplayName,
                                 const QString& currentUserId,
                                 const QString& currentUserName);
};

#endif // CHATCONTEXTMANAGER_H
