#ifndef SERVER_H
#define SERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QVector>
#include <QSet>
#include <QJsonObject>
#include "chatuser.h"
#include "message.h"

class QTimer;

class Server : public QObject {
    Q_OBJECT

public:
    explicit Server(QObject* parent = nullptr);
    ~Server();

    bool start(quint16 port = 8888);
    void stop();
    quint16 serverPort() const { return m_serverPort; }
    QString transportSecurityDescription() const;

signals:
    void newMessage(const Message& msg);
    void userJoined(const QString& userId, const QString& userName);
    void userLeft(const QString& userId, const QString& userName);
    void clientConnected(const QString& userId);
    void clientDisconnected(const QString& userId);
    void fileChunkAckReceived(QTcpSocket* socket, const QString& transferId, qint64 chunkIndex, bool accepted, const QString& reason);

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();

private:
    void broadcastMessage(const Message& msg, QTcpSocket* excludeSocket = nullptr);
    void sendUserList(QTcpSocket* socket);
    void sendToUser(const Message& msg);
    bool sendChunkedFileToSocket(const Message& msg, QTcpSocket* socket);
    void handleLogin(const QJsonObject& obj, QTcpSocket* socket);
    void handleMessage(const QJsonObject& obj);
    void handleFriendEvent(const QJsonObject& obj, QTcpSocket* socket = nullptr);
    void handleFile(const QJsonObject& obj, QTcpSocket* socket);
    void handleFileChunk(const QJsonObject& obj, QTcpSocket* socket);
    bool waitForFileChunkAck(QTcpSocket* socket, const QString& transferId, qint64 chunkIndex, QString* rejectReason = nullptr);
    void cleanupExpiredFileTransfers();
    ChatUser* findUserBySocket(QTcpSocket* socket);
    bool ensureAccountDatabase() const;
    QJsonObject loadAccountsFromSqlite() const;
    bool insertAccountToSqlite(const QString& account, const QString& passwordHash, const QString& userName) const;
    bool recordUserSessionToSqlite(const ChatUser& user, const QString& eventName) const;
    bool saveMessageToSqlite(const Message& msg, const QString& deliveryState) const;
    bool saveFriendEventToSqlite(const QString& eventType,
                                 const QString& senderId,
                                 const QString& senderName,
                                 const QString& receiverId,
                                 const QString& queryAccount,
                                 const QString& eventState,
                                 bool accepted = false) const;
    QString generateAccountId(const QJsonObject& accounts) const;
    QString accountDbPath() const;
    QJsonObject loadAccounts() const;
    void saveAccounts(const QJsonObject& accounts) const;
    QString accountsFilePath() const;
    QString offlineFilePath(const QString& userId) const;
    QString offlineAttachmentRootDir() const;
    QString offlineAttachmentDir(const QString& userId) const;
    qint64 offlineAttachmentQuotaBytes() const;
    qint64 offlineAttachmentUsedBytes() const;
    bool hasOfflineAttachmentCapacity(qint64 incomingBytes) const;
    QString saveOfflineAttachment(const Message& msg) const;
    QSet<QString> collectReferencedOfflineAttachments() const;
    void cleanupExpiredOfflineAttachments();
    void saveOfflineMessage(const Message& msg) const;
    bool deliverOfflinePayload(const QByteArray& payload, QTcpSocket* socket);
    bool sendOfflineAttachmentToSocket(const QJsonObject& obj, const QString& filePath, QTcpSocket* socket);
    void sendOfflineMessages(const QString& userId, QTcpSocket* socket);

    struct PendingFileTransfer {
        QJsonObject envelope;
        QTcpSocket* socket = nullptr;
        QString fileName;
        QVector<QByteArray> chunks;
        QSet<int> receivedIndexes;
        qint64 lastActivityMs = 0;
        qint64 receivedBytes = 0;
        qint64 fileSize = 0;
        qint64 chunkSize = 0;
        qint64 chunkCount = 0;
    };

    QTcpServer* m_tcpServer;
    QTimer* m_transferCleanupTimer;
    QTimer* m_offlineAttachmentCleanupTimer;
    quint16 m_serverPort;
    bool m_tlsEnabled;
    QMap<QTcpSocket*, ChatUser> m_clients;          // socket -> user
    QMap<QString, QTcpSocket*> m_userSockets;       // userId -> socket
    QSet<QString> m_usedNames;
    QMap<QString, PendingFileTransfer> m_pendingFileTransfers;
};

#endif // SERVER_H
