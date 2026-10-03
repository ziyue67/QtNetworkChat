#ifndef CHATSESSIONMANAGER_H
#define CHATSESSIONMANAGER_H

#include <QString>

struct PrivateChatUiState {
    QString windowSuffix;
    QString titleText;
    QString hintText;
};

struct ConnectionUiState {
    QString hintText;
    QString statusMessage;
};

class ChatSessionManager {
public:
    static QString e2eSummary(bool hasSession, bool needsRotation);
    static PrivateChatUiState privateChatState(const QString& userId,
                                               const QString& displayName,
                                               bool online,
                                               bool hasE2ESession,
                                               bool e2eNeedsRotation);
    static ConnectionUiState disconnectedState(const QString& targetName, bool localGroup);
    static ConnectionUiState errorState(const QString& targetName, bool localGroup, const QString& error);
    static QString e2eSessionHint(const QString& displayName, const QString& state);
    static QString e2eIdentityPendingHint(const QString& displayName, const QString& fingerprint);
    static QString e2eRotationRequestHint(const QString& displayName);
    static QString e2eRotationResponseHint(const QString& displayName, bool accepted);
    static QString localGroupHint(const QString& groupId, const QString& ownerId, bool owner);
};

#endif // CHATSESSIONMANAGER_H
