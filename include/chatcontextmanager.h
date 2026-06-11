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

struct ChatContextComposerState {
    QString privateChatTarget;
    QString targetDisplayName;
    QString currentUserId;
    QString currentUserName;
    QString currentGroupName;
    int currentGroupMemberCount = 0;
    bool currentTargetOnline = false;
    int friendCount = 0;
    int localGroupCount = 0;
    int knownUserCount = 0;
};

struct ChatContextComposerCommand {
    enum class Action {
        None,
        SetDraft,
        InsertText
    };

    bool handled = false;
    Action action = Action::None;
    QString text;
    QString statusMessage;
    int timeoutMs = 1400;
};

struct ChatContextPhraseMenuPlan {
    QString title;
    QString insertedStatusMessage;
    QStringList phrases;
};

struct ChatContextComposerMenuAction {
    QString title;
    QString toolTip;
    QString commandId;
};

struct ChatContextMenuActionSpec {
    QString title;
    QString toolTip;
    QString commandId;
    bool enabled = true;
    bool separatorBefore = false;
};

struct ChatContextSavedFileState {
    bool hasSavePath = false;
    bool canOpenFile = false;
    bool canOpenFolder = false;
    bool fileExists = false;
};

struct ChatContextSavedFileCommand {
    enum class Action {
        None,
        CopySavePath,
        OpenSavedFile,
        OpenSaveFolder
    };

    bool handled = false;
    Action action = Action::None;
    bool canExecute = false;
    QString clipboardText;
    QString missingStatusMessage;
    QString successStatusMessage;
    QString failureStatusMessage;
    QString failureTransferReason;
    int timeoutMs = 2200;
};

class ChatContextManager {
public:
    static QList<ChatContextMenuActionSpec> menuActionSpecs(bool isMediaMessage,
                                                            const ChatContextSavedFileState& savedFileState = ChatContextSavedFileState());
    static ChatContextCopyResult copyCommandResult(const QString& commandId,
                                                   const QString& chatText,
                                                   const QString& privateChatTarget,
                                                   const QString& targetDisplayName,
                                                   const QString& currentUserId,
                                                   const QString& currentUserName);
    static ChatContextSavedFileCommand savedFileCommand(const QString& commandId,
                                                        const ChatContextSavedFileState& savedFileState,
                                                        const QString& savePath = QString());
    static ChatContextDraftResult draftCommandResult(const QString& commandId,
                                                     const QString& chatText,
                                                     const QString& privateChatTarget,
                                                     const QString& targetDisplayName);
    static ChatContextComposerCommand composerCommand(const QString& commandId,
                                                      const ChatContextComposerState& state);
    static QList<ChatContextPhraseMenuPlan> composerPhraseMenuPlans();
    static QList<ChatContextComposerMenuAction> composerMenuActions();
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
