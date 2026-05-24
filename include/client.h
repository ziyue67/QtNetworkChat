#ifndef CLIENT_H
#define CLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include "chatuser.h"
#include "message.h"

class Client : public QObject {
    Q_OBJECT

public:
    explicit Client(QObject* parent = nullptr);
    ~Client();

    bool connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();

    void setAccountInfo(const QString& account, const QString& password, bool registerMode);
    bool sendMessage(const QString& content);
    bool sendPrivateMessage(const QString& receiverId, const QString& content);
    bool sendFriendRequest(const QString& receiverId);
    bool searchFriendByAccount(const QString& account);
    bool sendFriendResponse(const QString& receiverId, bool accepted);
    bool sendFile(const QString& filePath, const QString& receiverId = QString());
    bool sendImage(const QString& filePath, const QString& receiverId = QString());
    void setUserInfo(const QString& userId, const QString& userName);
    bool waitForLoginResult(int timeoutMs = 5000);

    bool isConnected() const { return m_socket && m_socket->state() == QAbstractSocket::ConnectedState; }
    QString currentUserId() const { return m_userId; }
    QString currentUserName() const { return m_userName; }
    bool currentLoginWasRegister() const { return m_loginWasRegister; }
    QString lastLoginError() const { return m_loginError; }

signals:
    void connected();
    void disconnected();
    void newMessage(const Message& msg);
    void userJoined(const QString& userId, const QString& userName);
    void userLeft(const QString& userId, const QString& userName);
    void userListUpdated(const QVector<ChatUser>& users);
    void loginSucceeded();
    void loginFailed(const QString& reason);
    void friendRequestReceived(const QString& senderId, const QString& senderName);
    void friendSearchResult(const QString& account, const QString& userId, const QString& userName, bool found, bool online);
    void friendRequestSent(const QString& receiverId, bool delivered);
    void friendResponseReceived(const QString& senderId, const QString& senderName, bool accepted);
    void fileTransferProgress(const QString& fileName, qint64 bytesPrepared, qint64 totalBytes);
    void connectionError(const QString& error);

private slots:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError socketError);
    void onHeartbeat();

private:
    void sendLogin();
    void handleServerMessage(const QJsonObject& obj);
    bool sendJson(const QJsonObject& obj);
    bool sendFilePayload(const QString& filePath, const QString& receiverId, MessageType messageType, const QString& contentPrefix);

    QTcpSocket* m_socket;
    QTimer* m_heartbeatTimer;
    QString m_userId;
    QString m_userName;
    QString m_account;
    QString m_password;
    bool m_registerMode;
    bool m_loginFinished;
    bool m_loginOk;
    bool m_loginWasRegister;
    QString m_loginError;
    QString m_serverHost;
    quint16 m_serverPort;
    QVector<ChatUser> m_onlineUsers;
    QByteArray m_buffer;
    quint16 m_reconnectAttempts;
};

#endif // CLIENT_H
