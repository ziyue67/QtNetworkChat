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
    void handleSendPrivateMessage(const QString& op, const QString& reqId, const QJsonObject& payload);

    bool requireString(const QJsonObject& payload,
                       const QString& field,
                       QString* value,
                       const QString& op,
                       const QString& reqId) const;

    QQNTClientBridge* m_bridge;
    QString m_account;
    QString m_password;
    bool m_hasAccountInfo;
};

#endif // QQNT_ENGINE_COMMAND_ROUTER_H
