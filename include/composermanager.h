#ifndef COMPOSERMANAGER_H
#define COMPOSERMANAGER_H

#include <QString>

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
    QString sendToolTip;
    QString messagePlaceholder;
    QString messageToolTip;
    QString fileToolTip;
    QString imageToolTip;
};

class ComposerManager {
public:
    static ComposerUiState uiState(const ComposerContext& context);
};

#endif // COMPOSERMANAGER_H
