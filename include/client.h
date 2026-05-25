#ifndef CLIENT_H
#define CLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QVector>
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
    bool sendServerGroupAnnouncementUpdate(const QString& groupId, const QString& announcement);
    bool sendServerGroupMemberUpdate(const QString& groupId, const QString& memberId, const QString& action);
    bool sendFile(const QString& filePath, const QString& receiverId = QString());
    bool sendImage(const QString& filePath, const QString& receiverId = QString());
    bool resumeFileTransfer(const QString& filePath,
                            const QString& transferId,
                            qint64 confirmedBytes,
                            qint64 nextChunkIndex,
                            const QString& receiverId = QString(),
                            MessageType messageType = MessageType::File);
    bool queryAndResumeFileTransfer(const QString& filePath,
                                    const QString& transferId,
                                    const QString& receiverId = QString(),
                                    MessageType messageType = MessageType::File,
                                    QString* rejectReason = nullptr,
                                    int timeoutMs = 5000);
    bool queryFileTransferResumeState(const QString& transferId,
                                      qint64* confirmedBytes = nullptr,
                                      qint64* nextChunkIndex = nullptr,
                                      QVector<qint64>* receivedChunks = nullptr,
                                      QString* rejectReason = nullptr,
                                      int timeoutMs = 5000,
                                      qint64* fileSize = nullptr,
                                      qint64* chunkSize = nullptr,
                                      qint64* chunkCount = nullptr,
                                      QString* fileHash = nullptr);
    bool saveOutgoingTransferState(const QString& transferId,
                                   const QString& filePath,
                                   const QString& receiverId,
                                   MessageType messageType,
                                   const QString& fileHash,
                                   qint64 fileSize,
                                   qint64 chunkCount);
    bool loadOutgoingTransferState(QJsonObject* state) const;
    bool clearOutgoingTransferState();
    void cancelCurrentOutgoingTransfer();
    void setUserInfo(const QString& userId, const QString& userName);
    bool waitForLoginResult(int timeoutMs = 5000);

    bool isConnected() const { return m_socket && m_socket->state() == QAbstractSocket::ConnectedState; }
    QString currentUserId() const { return m_userId; }
    QString currentUserName() const { return m_userName; }
    bool currentLoginWasRegister() const { return m_loginWasRegister; }
    QString lastLoginError() const { return m_loginError; }
    QString transportSecurityDescription() const;
    bool hasServerGroupSnapshot() const { return m_hasServerGroupSnapshot; }
    QJsonArray serverGroups() const { return m_serverGroups; }

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
    void friendSearchResult(const QString& account, const QString& userId, const QString& userName, bool found, bool online, bool exactMatch, int matchCount, const QString& matchReason);
    void friendRequestSent(const QString& receiverId, bool delivered);
    void friendResponseReceived(const QString& senderId, const QString& senderName, bool accepted);
    void fileTransferProgress(const QString& fileName, qint64 bytesPrepared, qint64 totalBytes);
    void fileTransferPrepared(const QString& fileName, qint64 totalBytes, qint64 chunkSize, qint64 chunkCount, const QString& fileHash);
    void fileReceiveProgress(const QString& fileName, qint64 bytesReceived, qint64 totalBytes);
    void fileChunkAckReceived(const QString& transferId, qint64 chunkIndex, bool accepted, const QString& reason, qint64 receivedBytes);
    void fileTransferResumeStateReceived(const QString& transferId,
                                         bool canResume,
                                         qint64 confirmedBytes,
                                         qint64 nextChunkIndex,
                                         qint64 fileSize,
                                         qint64 chunkSize,
                                         qint64 chunkCount,
                                         const QString& fileHash,
                                         const QVector<qint64>& receivedChunks,
                                         const QString& reason);
    void serverGroupSnapshotReceived(const QJsonArray& groups);
    void connectionError(const QString& error);
    void outgoingTransferCancelRequested();

private slots:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError socketError);
    void onHeartbeat();

private:
    void sendLogin();
    void handleServerMessage(const QJsonObject& obj);
    void handleIncomingFileChunk(const QJsonObject& obj);
    bool sendJson(const QJsonObject& obj);
    bool sendFileChunkAck(const QString& transferId, qint64 chunkIndex, bool accepted, const QString& reason = QString(), qint64 receivedBytes = 0);
    bool sendFilePayload(const QString& filePath,
                         const QString& receiverId,
                         MessageType messageType,
                         const QString& contentPrefix,
                         const QString& resumeTransferId = QString(),
                         qint64 resumeConfirmedBytes = 0,
                         qint64 resumeNextChunkIndex = 0,
                         const QVector<qint64>& resumeReceivedChunks = QVector<qint64>());
    bool waitForFileChunkAck(const QString& transferId, qint64 chunkIndex, QString* rejectReason = nullptr, qint64* receivedBytes = nullptr);
    bool waitForFileTransferResumeState(const QString& transferId,
                                        qint64* confirmedBytes,
                                        qint64* nextChunkIndex,
                                        QVector<qint64>* receivedChunks,
                                        QString* rejectReason,
                                        int timeoutMs,
                                        qint64* fileSize = nullptr,
                                        qint64* chunkSize = nullptr,
                                        qint64* chunkCount = nullptr,
                                        QString* fileHash = nullptr);
    void cleanupExpiredIncomingFileTransfers();

    struct PendingIncomingFileTransfer {
        QJsonObject envelope;
        QString fileName;
        QVector<QByteArray> chunks;
        QSet<int> receivedIndexes;
        qint64 lastActivityMs = 0;
        qint64 receivedBytes = 0;
        qint64 fileSize = 0;
        qint64 chunkSize = 0;
        qint64 chunkCount = 0;
    };

    QTcpSocket* m_socket;
    QTimer* m_heartbeatTimer;
    QTimer* m_transferCleanupTimer;
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
    QMap<QString, PendingIncomingFileTransfer> m_incomingFileTransfers;
    bool m_hasServerGroupSnapshot;
    bool m_cancelOutgoingTransfer;
    QString m_currentOutgoingTransferId;
    QString m_currentOutgoingReceiverId;
    QString m_currentOutgoingFileName;
    QJsonArray m_serverGroups;
};

#endif // CLIENT_H
