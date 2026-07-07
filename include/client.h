#ifndef CLIENT_H
#define CLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QByteArray>
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
    void setE2ESessionKey(const QString& peerId, const QString& keyId, const QByteArray& sessionKey);
    void clearE2ESessionKey(const QString& peerId);
    bool hasE2ESession(const QString& peerId) const;
    bool e2eSessionNeedsRotation(const QString& peerId) const;
    bool e2ePeerIdentityTrusted(const QString& peerId) const;
    QJsonObject e2eSessionStatus(const QString& peerId) const;
    QJsonObject e2eLocalIdentityStatus() const;
    QJsonObject e2ePeerIdentityStatus(const QString& peerId) const;
    QJsonObject planE2EBackendMigration() const;
    QJsonObject planE2EProductionRotationDryRun() const;
    QJsonObject executeE2EProductionRotation(QString* rejectReason = nullptr);
    QJsonObject executeE2EBackendMigration(QString* rejectReason = nullptr);
    void setE2ESessionMessageLimitForTesting(int limit);
    bool announceE2EIdentity(const QString& peerId = QString(), QString* rejectReason = nullptr);
    bool pinE2EPeerIdentity(const QString& peerId, const QString& expectedFingerprint = QString(), QString* rejectReason = nullptr);
    bool verifyAndPinE2EPeerIdentity(const QString& peerId,
                                     const QString& verificationCode,
                                     QString* rejectReason = nullptr);
    bool clearE2EPeerIdentityPin(const QString& peerId, QString* rejectReason = nullptr);
    bool clearE2EBackendMigrationState(QString* rejectReason = nullptr);
    bool requestE2ESessionRotation(const QString& peerId, QString* rejectReason = nullptr);
    bool respondE2ESessionRotation(const QString& peerId,
                                   const QString& keyId,
                                   const QByteArray& publicKey,
                                   bool accepted,
                                   const QString& reason = QString(),
                                   QString* rejectReason = nullptr);
    bool sendMessage(const QString& content);
    bool sendPrivateMessage(const QString& receiverId, const QString& content, const QString& clientMessageId = QString());
    bool sendEncryptedPrivateMessage(const QString& receiverId, const QString& content, QString* rejectReason = nullptr);
    bool sendFriendRequest(const QString& receiverId);
    bool searchFriendByAccount(const QString& account);
    bool sendFriendResponse(const QString& receiverId, bool accepted);
    bool requestAccountDeactivation(const QString& reason = QString());
    bool cancelAccountDeactivation();
    bool sendServerGroupAnnouncementUpdate(const QString& groupId, const QString& announcement);
    bool sendServerGroupMemberUpdate(const QString& groupId, const QString& memberId, const QString& action);
    bool sendServerGroupEssenceUpdate(const QString& groupId, const QString& messageId, bool enabled);
    bool sendMessageFavoriteUpdate(const QString& sessionId, const QString& messageId, bool favorite, const QJsonObject& message = QJsonObject());
    bool sendServerGroupMessageRecall(const QString& groupId, const QString& messageId);
    bool sendServerGroupMemberMute(const QString& groupId, const QString& memberId, qint64 mutedUntil, const QString& reason = QString());
    bool sendServerGroupMemberUnmute(const QString& groupId, const QString& memberId);
    bool requestServerGroupMemberProfile(const QString& groupId, const QString& memberId);
    bool createPrivateServerGroup(const QString& groupName, const QString& announcement = QString(), const QStringList& initialMemberIds = QStringList());
    bool sendServerGroupMessage(const QString& groupId, const QString& content, const QString& clientMessageId = QString());
    bool sendServerGroupFile(const QString& groupId, const QString& filePath);
    bool sendServerGroupImage(const QString& groupId, const QString& filePath);
    bool sendFile(const QString& filePath, const QString& receiverId = QString());
    bool sendImage(const QString& filePath, const QString& receiverId = QString());
    bool resumeFileTransfer(const QString& filePath,
                            const QString& transferId,
                            qint64 confirmedBytes,
                            qint64 nextChunkIndex,
                            const QString& receiverId = QString(),
                            MessageType messageType = MessageType::File,
                            const QString& serverGroupId = QString());
    bool queryAndResumeFileTransfer(const QString& filePath,
                                    const QString& transferId,
                                    const QString& receiverId = QString(),
                                    MessageType messageType = MessageType::File,
                                    QString* rejectReason = nullptr,
                                    int timeoutMs = 5000,
                                    const QString& serverGroupId = QString());
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
                                   qint64 chunkCount,
                                   const QJsonObject& recoveryPolicy = QJsonObject(),
                                   const QString& serverGroupId = QString());
    bool loadOutgoingTransferState(QJsonObject* state) const;
    QJsonObject savedOutgoingTransferRecoveryStatus() const;
    bool clearOutgoingTransferState();
    bool resumeSavedOutgoingTransfer(QString* rejectReason = nullptr, int timeoutMs = 5000);
    void cancelCurrentOutgoingTransfer();
    void setUserInfo(const QString& userId, const QString& userName);
    void setAvatarData(const QByteArray& pngData);
    bool sendAvatarUpdate(const QByteArray& pngData);
    bool waitForLoginResult(int timeoutMs = 5000);

    bool isConnected() const { return m_socket && m_socket->state() == QAbstractSocket::ConnectedState; }
    QString currentUserId() const { return m_userId; }
    QString currentUserName() const { return m_userName; }
    bool currentLoginWasRegister() const { return m_loginWasRegister; }
    QString lastLoginError() const { return m_loginError; }
    QString currentOutgoingTransferId() const { return m_currentOutgoingTransferId; }
    QString lastOutgoingTransferId() const { return m_lastOutgoingTransferId; }
    QVector<ChatUser> onlineUsers() const { return m_onlineUsers; }
    QVector<ChatUser> friends() const { return m_friends; }
    QString transportSecurityDescription() const;
    bool hasServerGroupSnapshot() const { return m_hasServerGroupSnapshot; }
    QJsonArray serverGroups() const { return m_serverGroups; }
    QJsonArray removedServerGroups() const { return m_removedServerGroups; }

signals:
    void connected();
    void disconnected();
    void newMessage(const Message& msg);
    void userJoined(const QString& userId, const QString& userName);
    void userLeft(const QString& userId, const QString& userName);
    void userListUpdated(const QVector<ChatUser>& users);
    void friendListUpdated(const QVector<ChatUser>& friends);
    void loginSucceeded();
    void loginFailed(const QString& reason);
    void friendRequestReceived(const QString& senderId, const QString& senderName);
    void friendSearchResult(const QString& account, const QString& userId, const QString& userName, bool found, bool online, bool exactMatch, int matchCount, const QString& matchReason);
    void friendRequestSent(const QString& receiverId, bool delivered);
    void friendResponseReceived(const QString& senderId, const QString& senderName, bool accepted);
    void fileTransferProgress(const QString& fileName, qint64 bytesPrepared, qint64 totalBytes, const QString& transferId = QString());
    void fileTransferPrepared(const QString& fileName, qint64 totalBytes, qint64 chunkSize, qint64 chunkCount, const QString& fileHash);
    void fileReceiveProgress(const QString& fileName, qint64 bytesReceived, qint64 totalBytes, const QString& transferId = QString());
    void fileTransferStatusChanged(const QString& fileName,
                                   const QString& transferId,
                                   const QString& reason,
                                   qint64 receivedBytes,
                                   qint64 totalBytes,
                                   const QString& direction = QString(),
                                   const QString& filePath = QString(),
                                   bool terminal = true);
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
    void serverGroupMemberUpdated(const QString& groupId, const QString& memberId, const QString& action);
    void serverGroupEssenceUpdated(const QJsonObject& payload);
    void messageFavoriteUpdated(const QJsonObject& payload);
    void favoriteMessagesSnapshotReceived(const QJsonArray& favorites);
    void serverGroupMessageRecalled(const QJsonObject& payload);
    void serverGroupMemberMuted(const QJsonObject& payload);
    void serverGroupMemberUnmuted(const QJsonObject& payload);
    void serverGroupMemberProfileReceived(const QJsonObject& payload);
    void e2eSessionStateChanged(const QString& peerId, const QJsonObject& status);
    void e2eIdentityStateChanged(const QString& peerId, const QJsonObject& status);
    void e2eSessionRotationRequested(const QString& peerId, const QJsonObject& agreement);
    void e2eSessionRotationResponded(const QString& peerId, const QJsonObject& agreement, bool accepted, const QString& reason);
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
    ChatUser friendCandidateForId(const QString& userId, const QString& fallbackName = QString()) const;
    void addOrUpdateFriend(const QString& userId, const QString& fallbackName = QString());
    void refreshFriendPresenceFromOnlineUsers();
    bool sendFileChunkAck(const QString& transferId, qint64 chunkIndex, bool accepted, const QString& reason = QString(), qint64 receivedBytes = 0);
    bool sendFilePayload(const QString& filePath,
                         const QString& receiverId,
                         MessageType messageType,
                         const QString& contentPrefix,
                         const QString& resumeTransferId = QString(),
                         qint64 resumeConfirmedBytes = 0,
                         qint64 resumeNextChunkIndex = 0,
                         const QVector<qint64>& resumeReceivedChunks = QVector<qint64>(),
                         const QString& serverGroupId = QString());
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
    void loadOrCreateE2ELocalIdentity();
    bool saveE2ELocalIdentity(QString* rejectReason = nullptr) const;
    void loadE2ETrustPins();
    bool saveE2ETrustPins(QString* rejectReason = nullptr) const;
    bool e2eLocalIdentityUsable(QString* rejectReason = nullptr) const;
    bool requireTrustedE2EPeer(const QString& peerId, QString* rejectReason = nullptr) const;
    bool populateE2EAgreementIdentityFingerprints(const QString& peerId,
                                                  E2EKeyAgreement* agreement,
                                                  QString* rejectReason) const;
    bool validateIncomingE2EAgreementIdentity(const E2EKeyAgreement& agreement,
                                              QString* rejectReason) const;
    void installE2EDerivedSession(const QString& peerId,
                                  const QString& keyId,
                                  const QByteArray& sessionKey);
    struct E2ESession {
        QString keyId;
        QString backendId;
        QByteArray sessionKey;
        qint64 createdAtMs = 0;
        qint64 encryptedMessages = 0;
        qint64 decryptedMessages = 0;
        bool rotationRequired = false;
    };

    bool e2eFileSessionForPeer(const QString& peerId,
                               const E2ESession** session,
                               QString* rejectReason = nullptr) const;
    bool markE2EFileChunkSent(const QString& peerId);

    struct E2EPeerIdentity {
        QByteArray publicKey;
        QString fingerprint;
        qint64 firstSeenAtMs = 0;
        qint64 lastSeenAtMs = 0;
        bool pinned = false;
        QString pinnedFingerprint;
        bool verified = false;
        QString verificationCode;
        qint64 verifiedAtMs = 0;
        bool fingerprintMismatch = false;
    };

    void applyE2EStoredTrustPin(const QString& peerId, E2EPeerIdentity* peerIdentity) const;

    struct E2EStoredTrustPin {
        QString fingerprint;
        QString backendId;
        bool verified = false;
        QString verificationCode;
        qint64 verifiedAtMs = 0;
    };

    struct E2EPendingAgreement {
        E2EKeyAgreement agreement;
        QByteArray privateKey;
        qint64 createdAtMs = 0;
    };

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
    QString m_avatarBase64;
    bool m_registerMode;
    bool m_loginFinished;
    bool m_loginOk;
    bool m_loginWasRegister;
    QString m_loginError;
    QString m_serverHost;
    quint16 m_serverPort;
    QVector<ChatUser> m_onlineUsers;
    QVector<ChatUser> m_friends;
    QMap<QString, QString> m_pendingIncomingFriendNames;
    QByteArray m_buffer;
    quint16 m_reconnectAttempts;
    QMap<QString, PendingIncomingFileTransfer> m_incomingFileTransfers;
    bool m_hasServerGroupSnapshot;
    bool m_cancelOutgoingTransfer;
    QString m_currentOutgoingTransferId;
    QString m_lastOutgoingTransferId;
    QString m_currentOutgoingReceiverId;
    QString m_currentOutgoingGroupId;
    QString m_currentOutgoingFileName;
    QJsonArray m_serverGroups;
    QJsonArray m_removedServerGroups;
    QMap<QString, E2ESession> m_e2eSessions;
    QByteArray m_e2eIdentityPrivateKey;
    QByteArray m_e2eIdentityPublicKey;
    QString m_e2eIdentityBackendId;
    QString m_e2eIdentityFingerprint;
    QMap<QString, E2EPeerIdentity> m_e2ePeerIdentities;
    QMap<QString, E2EStoredTrustPin> m_e2eStoredTrustPins;
    QMap<QString, E2EPendingAgreement> m_e2ePendingOutgoingAgreements;
    QMap<QString, E2EPendingAgreement> m_e2ePendingIncomingAgreements;
    int m_e2eSessionMessageLimit;
};

#endif // CLIENT_H
