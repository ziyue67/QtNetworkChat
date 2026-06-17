#ifndef QQNT_ENGINE_COMMAND_ROUTER_H
#define QQNT_ENGINE_COMMAND_ROUTER_H

#include <QJsonObject>
#include <QObject>

class QQNTClientBridge;

class QQNTEngineCommandRouter : public QObject {
    Q_OBJECT

public:
    explicit QQNTEngineCommandRouter(QQNTClientBridge* bridge);

    void route(const QJsonObject& command);

private:
    void handleReady(const QString& op, const QString& reqId);
    void handleLogin(const QString& op, const QString& reqId, const QJsonObject& payload, bool registerMode);
    void handleConnect(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleDisconnect(const QString& op, const QString& reqId);
    void handleSetUserInfo(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleGetUserList(const QString& op, const QString& reqId);
    void handleGetFriendList(const QString& op, const QString& reqId);
    void handleGetGroupList(const QString& op, const QString& reqId);
    void handleSearchFriend(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleSendFriendRequest(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleRespondFriendRequest(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleSendPrivateMessage(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleSendGroupMessage(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleCreateGroup(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleUpdateGroupAnnouncement(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleUpdateGroupMember(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleSendFileLike(const QString& op, const QString& reqId, const QJsonObject& payload, bool imageMode);
    void handleCancelTransfer(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleQueryResume(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleE2EStatus(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleE2EAnnounceIdentity(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleE2EPinIdentity(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleE2ERequestRotation(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleProfileUpdate(const QString& op, const QString& reqId, const QJsonObject& payload);
    void handleSettingsSync(const QString& op, const QString& reqId, const QJsonObject& payload);

    void sendBoolAck(const QString& op,
                     const QString& reqId,
                     bool accepted,
                     const QString& failureCode,
                     const QString& failureMessage);

    bool requireString(const QJsonObject& payload,
                       const QString& field,
                       QString* value,
                       const QString& op,
                       const QString& reqId) const;

    QQNTClientBridge* m_bridge;
    QString m_account;
    QString m_password;
    QJsonObject m_settings;
    int m_settingsRevision;
    bool m_hasAccountInfo;
};

#endif // QQNT_ENGINE_COMMAND_ROUTER_H
