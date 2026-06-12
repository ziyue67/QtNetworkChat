#ifndef COMPOSERMANAGER_H
#define COMPOSERMANAGER_H

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct ComposerContext {
    QString draftText;
    QString targetName;
    bool localGroup = false;
    bool removedFromPublicGroup = false;
    bool clientConnected = false;
    bool encryptedReady = false;
};

struct ComposerUiState {
    bool canReachTarget = false;
    bool canSend = false;
    bool sendFileEnabled = false;
    bool sendImageEnabled = false;
    QString workspaceTitle;
    QString workspaceDetail;
    QString draftSummary;
    QString stateTone;
    QString sendToolTip;
    QString messagePlaceholder;
    QString messageToolTip;
    QString fileToolTip;
    QString imageToolTip;
};

struct ComposerMentionAction {
    QString title;
    QString insertText;
    QString statusMessage;
};

struct ComposerMentionMenuPlan {
    QList<ComposerMentionAction> actions;
    bool separatorAfterAll = false;
};

class ComposerManager {
public:
    static ComposerUiState uiState(const ComposerContext& context);
    static ComposerMentionMenuPlan mentionMenuPlan(const QStringList& memberIds,
                                                   const QString& currentUserId,
                                                   const QMap<QString, QString>& displayNames);
};

#endif // COMPOSERMANAGER_H
