#ifndef SERVER_H
#define SERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QVector>
#include <QSet>
#include <QJsonObject>
#include <QStringList>
#include "chatuser.h"
#include "message.h"
#include <functional>
#include <memory>

class QTimer;
class ObjectStore;
class QQNTRedisService;
class HeartbeatMonitor;
struct LargeFileDeliveredReceiptDecision;

class Server : public QObject {
    Q_OBJECT

public:
    using ObjectStoreFactory = std::function<std::unique_ptr<ObjectStore>(QString* error)>;

    explicit Server(QObject* parent = nullptr);
    ~Server();

    bool start(quint16 port = 8888);
    void stop();
    quint16 serverPort() const { return m_serverPort; }
    bool isServiceReady() const { return m_serviceReady; }
    QString serviceReadinessReason() const { return m_serviceReadinessReason; }
    QString transportSecurityDescription() const;
    QJsonObject databaseHealthSnapshot() const;
    void setObjectStoreFactoryForTesting(ObjectStoreFactory factory);

signals:
    void newMessage(const Message& msg);
    void userJoined(const QString& userId, const QString& userName);
    void userLeft(const QString& userId, const QString& userName);
    void clientConnected(const QString& userId);
    void clientDisconnected(const QString& userId);
    void fileChunkAckReceived(QTcpSocket* socket, const QString& transferId, qint64 chunkIndex, bool accepted, const QString& reason, qint64 receivedBytes);

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();

private:
    void broadcastMessage(const Message& msg, QTcpSocket* excludeSocket = nullptr);
    void sendUserList(QTcpSocket* socket);
    void sendFriendListSnapshot(const QString& userId, QTcpSocket* socket) const;
    void sendServerGroupSnapshot(const QString& userId, QTcpSocket* socket) const;
    void sendServerGroupMemberUpdated(QTcpSocket* socket, const QString& groupId, const QString& memberId, const QString& action) const;
    void sendToUser(const Message& msg);
    bool sendChunkedFileToSocket(const Message& msg, QTcpSocket* socket);
    bool sendFileChunkAndWaitForAck(QTcpSocket* socket,
                                    const QByteArray& data,
                                    const QString& transferId,
                                    qint64 chunkIndex,
                                    QString* rejectReason = nullptr,
                                    qint64* receivedBytes = nullptr);
    void handleLogin(const QJsonObject& obj, QTcpSocket* socket);
    void handleMessage(const QJsonObject& obj, QTcpSocket* socket = nullptr);
    void handleProfileUpdate(const QJsonObject& obj, QTcpSocket* socket);
    void handleE2EIdentityAnnouncement(const QJsonObject& obj, QTcpSocket* socket);
    void handleE2EKeyRotation(const QJsonObject& obj, QTcpSocket* socket);
    void handleFriendEvent(const QJsonObject& obj, QTcpSocket* socket = nullptr);
    void handleServerGroupCreate(const QJsonObject& obj, QTcpSocket* socket);
    void handleServerGroupMessage(const QJsonObject& obj, QTcpSocket* socket);
    void handleServerGroupAnnouncementUpdate(const QJsonObject& obj, QTcpSocket* socket);
    void handleServerGroupMemberUpdate(const QJsonObject& obj, QTcpSocket* socket);
    void handleFile(const QJsonObject& obj, QTcpSocket* socket);
    void handleFileChunk(const QJsonObject& obj, QTcpSocket* socket);
    void handleFileTransferResumeQuery(const QJsonObject& obj, QTcpSocket* socket);
    void handleFileTransferCancel(const QJsonObject& obj, QTcpSocket* socket);
    void cleanupExpiredFileTransfers();
    bool ensureRedisReadyForStartup();
    void tryRecoverRedisCommandAvailability();
    void tryRecoverRedisSubscriberAvailability();
    void updateRedisCommandAvailability(bool available, const QString& reason = QString());
    void updateRedisSubscriberAvailability(bool available, const QString& reason = QString());
    void refreshServiceReadiness();
    bool ensureServiceReady(QTcpSocket* socket, const QString& action);
    void refreshRedisPresence(const ChatUser& user);
    void clearRedisPresence(const QString& userId);
    bool publishRedisPresenceEvent(const QString& userId, const QString& action) const;
    bool publishRedisServerGroupSnapshotRefresh(const QStringList& userIds,
                                                const QString& groupId,
                                                const QString& notice,
                                                const QString& memberId = QString(),
                                                const QString& memberAction = QString()) const;
    void refreshConnectedClientViews();
    bool isRedisUserOnline(const QString& userId, bool* online = nullptr) const;
    bool canPublishRedisMessageEvent(const Message& msg, const QString& deliveryState) const;
    bool publishRedisMessageEvent(const Message& msg, const QString& deliveryState);
    bool publishRedisE2EControlEvent(const QJsonObject& forwarded) const;
    bool publishRedisLargeFileOffer(const QJsonObject& offlinePayload) const;
    bool publishRedisLargeFileClaim(const QJsonObject& offer) const;
    bool publishRedisLargeFileDelivered(const QJsonObject& offer, qint64 confirmedBytes) const;
    bool publishRedisLargeFileFailed(const QJsonObject& offer, const QString& reason) const;
    void handleRedisMessageEvent(const QByteArray& payload);
    void handleRedisServerGroupSnapshotRefresh(const QJsonObject& event);
    void handleRedisE2EControlEvent(const QJsonObject& event);
    void handleRedisLargeFileOffer(const QJsonObject& event);
    void handleRedisLargeFileDelivered(const QJsonObject& event);
    void handleRedisLargeFileFailed(const QJsonObject& event);
    bool deliverRedisLargeFileOffer(const QJsonObject& event, QTcpSocket* socket, bool publishDeliveredReceipt = true);
    ChatUser* findUserBySocket(QTcpSocket* socket);
    bool ensureAccountDatabase() const;
    QJsonObject loadAccountsFromSqlite() const;
    bool insertAccountToSqlite(const QString& account,
                               const QString& passwordHash,
                               const QString& userName,
                               const QString& avatarBase64 = QString()) const;
    bool updateAccountPasswordHashInSqlite(const QString& account, const QString& passwordHash) const;
    bool recordUserSessionToSqlite(const ChatUser& user, const QString& eventName) const;
    bool recordDefaultGroupMembership(const ChatUser& user) const;
    bool isServerGroupMember(const QString& groupId, const QString& userId) const;
    bool isServerGroupRemovedMember(const QString& groupId, const QString& userId) const;
    QStringList serverGroupMemberIds(const QString& groupId) const;
    bool recordServerGroupAuditEvent(const QString& groupId,
                                     const QString& action,
                                     const QString& actorId,
                                     const QString& actorName,
                                     const QString& targetUserId,
                                     const QString& targetUserName,
                                     const QJsonObject& details = QJsonObject()) const;
    bool saveMessageToSqlite(const Message& msg, const QString& deliveryState) const;
    bool saveFriendEventToSqlite(const QString& eventType,
                                 const QString& senderId,
                                 const QString& senderName,
                                 const QString& receiverId,
                                 const QString& queryAccount,
                                 const QString& eventState,
                                 bool accepted = false) const;
    bool saveAcceptedFriendshipToSqlite(const QString& userId,
                                        const QString& userName,
                                        const QString& friendId,
                                        const QString& friendName) const;
    QVector<ChatUser> loadFriendListFromSqlite(const QString& userId) const;
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
    bool shouldPublishLargeFileOffer(const Message& msg) const;
    bool shouldPublishServerGroupLargeFileOffer(const Message& msg, const QStringList& memberIds) const;
    bool publishRedisServerGroupLargeFileOffer(const Message& msg, const QStringList& memberIds) const;
    QString objectStoreType() const;
    QString objectStoreRootDir() const;
    std::unique_ptr<ObjectStore> createConfiguredObjectStore(QString* error = nullptr) const;
    qint64 objectStoreTtlMs() const;
    LargeFileDeliveredReceiptDecision evaluateRedisLargeFileDeliveredReceipt(const QJsonObject& event) const;
    void persistRedisLargeFileDeliveredReceiptSummary(const QJsonObject& event,
                                                      const LargeFileDeliveredReceiptDecision& decision,
                                                      bool cleanupSucceeded) const;
    struct LargeFileCleanupResult {
        bool queueCleaned = false;
        bool objectDeleteAttempted = false;
        bool objectDeleted = false;
        QString objectDeleteReason;
    };
    LargeFileCleanupResult cleanupDeliveredRedisLargeFile(const QJsonObject& event) const;
    QString saveOfflineAttachment(const Message& msg) const;
    QSet<QString> collectReferencedOfflineAttachments() const;
    void cleanupExpiredOfflineAttachments();
    void saveOfflineMessage(const Message& msg) const;
    bool deliverOfflinePayload(const QByteArray& payload, QTcpSocket* socket, qint64 sqliteMessageId = -1);
    bool sendOfflineAttachmentToSocket(const QJsonObject& obj, const QString& filePath, QTcpSocket* socket, qint64 sqliteMessageId = -1);
    bool updateOfflineMessageProgress(qint64 sqliteMessageId, const QJsonObject& obj, qint64 confirmedBytes, qint64 confirmedChunkIndex) const;
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
    QQNTRedisService* m_redisService;
    QTimer* m_transferCleanupTimer;
    QTimer* m_offlineAttachmentCleanupTimer;
    HeartbeatMonitor* m_heartbeatMonitor;
    quint16 m_serverPort;
    bool m_tlsEnabled;
    QString m_instanceId;
    bool m_serviceReady = false;
    QString m_serviceReadinessReason;
    bool m_stopping = false;
    QMap<QTcpSocket*, ChatUser> m_clients;          // socket -> user
    QMap<QString, QTcpSocket*> m_userSockets;       // userId -> socket
    QSet<QString> m_usedNames;
    QMap<QString, PendingFileTransfer> m_pendingFileTransfers;
    ObjectStoreFactory m_objectStoreFactoryForTesting;
};

#endif // SERVER_H
