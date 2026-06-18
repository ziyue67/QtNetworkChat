#ifndef QQNT_CLIENT_BRIDGE_H
#define QQNT_CLIENT_BRIDGE_H

#include "client.h"

#include <QJsonObject>
#include <QObject>

class QQNTEngineCommandRouter;
struct ChatUser;
struct Message;

class QQNTClientBridge : public QObject {
    Q_OBJECT

public:
    explicit QQNTClientBridge(QObject* parent = nullptr);
    ~QQNTClientBridge() override;

    void start();
    Q_INVOKABLE void handleCommandLine(const QByteArray& line);

    Client* client() { return &m_client; }
    const Client* client() const { return &m_client; }

    QJsonObject readyPayload() const;
    QJsonObject userListPayload() const;
    QJsonObject friendListPayload() const;
    QJsonObject groupListPayload() const;
    void setConnectionTarget(const QString& host, quint16 port);
    void sendAck(const QString& op, const QString& reqId, const QJsonObject& payload = QJsonObject());
    void sendErrorAck(const QString& op,
                      const QString& reqId,
                      const QString& code,
                      const QString& message,
                      const QString& source = QStringLiteral("engine"),
                      const QJsonObject& details = QJsonObject());
    void sendEvent(const QString& event, const QJsonObject& payload = QJsonObject());

private:
    void bindClientSignals();
    void sendNotification(const QString& title, const QString& body);
    QJsonObject messageToJson(const Message& message) const;
    QString sessionIdForMessage(const Message& message) const;
    bool isKnownServerGroupId(const QString& groupId) const;
    QJsonObject userToJson(const ChatUser& user) const;

    Client m_client;
    QQNTEngineCommandRouter* m_router;
    QString m_host;
    quint16 m_port;
};

#endif // QQNT_CLIENT_BRIDGE_H
