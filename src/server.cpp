#include "server.h"
#include "server_group_support.h"
#include "server_database.h"
#include "server_delivery_support.h"
#include "objectstore.h"
#include "qqnt_redis_service.h"
#include "redisclient.h"
#include "heartbeatmonitor.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QJsonArray>
#include <QStringList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QTimer>
#include <QPointer>
#include <QUuid>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <memory>

namespace {
using namespace ServerDatabase;
using namespace ServerDeliverySupport;
constexpr int kTransferCleanupIntervalMs = 30 * 1000;
constexpr int kOfflineAttachmentCleanupIntervalMs = 60 * 60 * 1000;
constexpr qint64 kRedisPubSubEventMaxBytes = 1LL * 1024 * 1024;
constexpr qint64 kDefaultObjectStoreTtlHours = 24;
constexpr qint64 kMaxObjectStoreTtlHours = 24LL * 365;
constexpr qsizetype kMaxE2EIdentityPublicKeyBytes = 4096;

bool serverGroupMessageRateAllowed(const QString& groupId, const QString& userId, int perMinute) {
    static QMutex mutex;
    static QHash<QString, QList<qint64>> timestampsByMember;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString key = groupId + QLatin1Char('|') + userId;
    QMutexLocker lock(&mutex);
    QList<qint64>& timestamps = timestampsByMember[key];
    while (!timestamps.isEmpty() && timestamps.first() <= now - 60 * 1000) timestamps.removeFirst();
    if (timestamps.size() >= perMinute) return false;
    timestamps.append(now);
    return true;
}

bool envEnabled(const char* name) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

QString serverGroupTypeFromId(const QString& groupId) {
    return groupId == QLatin1String("public") ? QStringLiteral("public") : QStringLiteral("private");
}

QString serverGroupHistoryPolicy(const QString& groupType) {
    return groupType == QLatin1String("private")
        ? QStringLiteral("member-and-removed-readonly")
        : QStringLiteral("public-removed-readonly");
}

QString serverGroupFilePolicy(const QString& groupType) {
    return groupType == QLatin1String("private")
        ? QStringLiteral("members-only")
        : QStringLiteral("public-members-only");
}

QString normalizeServerGroupId(QString groupId) {
    groupId = groupId.trimmed();
    if (groupId.startsWith(QStringLiteral("group:"))) {
        groupId = groupId.mid(QStringLiteral("group:").size()).trimmed();
    }
    return groupId.isEmpty() ? QStringLiteral("public") : groupId;
}

QStringList initialServerGroupMemberIds(const QJsonObject& obj, const QString& requesterId) {
    QStringList memberIds;
    const QJsonArray members = obj.value(QStringLiteral("members")).toArray();
    for (const QJsonValue& memberValue : members) {
        QString memberId = memberValue.toString().trimmed();
        if (memberId.isEmpty() && memberValue.isObject()) {
            const QJsonObject memberObject = memberValue.toObject();
            memberId = memberObject.value(QStringLiteral("userId")).toString(
                memberObject.value(QStringLiteral("account")).toString(
                    memberObject.value(QStringLiteral("id")).toString())).trimmed();
        }
        if (memberId.isEmpty() || memberId == requesterId || memberIds.contains(memberId)) {
            continue;
        }
        memberIds << memberId;
    }
    return memberIds;
}

using ServerGroupActorPermission = ServerGroupSupport::ActorPermission;

ServerGroupActorPermission loadServerGroupActorPermission(QSqlDatabase& db,
                                                          const QString& groupId,
                                                          const QString& userId) {
    ServerGroupActorPermission permission;
    QSqlQuery query(db);
    query.prepare("SELECT COALESCE(g.owner_id, ''), COALESCE(m.role, '') "
                  "FROM server_groups g "
                  "JOIN server_group_members m ON m.group_id = g.group_id "
                  "WHERE g.group_id = ? AND m.user_id = ?");
    query.addBindValue(groupId);
    query.addBindValue(userId);
    if (query.exec() && query.next()) {
        permission.member = true;
        permission.ownerId = query.value(0).toString();
        permission.role = query.value(1).toString().toLower();
        permission.owner = permission.ownerId == userId || permission.role == QLatin1String("owner");
        permission.manager = permission.owner || permission.role == QLatin1String("admin");
    }
    return permission;
}

QByteArray fromBase64Url(const QString& value) {
    return QByteArray::fromBase64(value.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

bool validateE2EIdentityJson(const QJsonObject& identity, QString* reason = nullptr) {
    if (!isSupportedE2EProtocol(identity.value("protocol").toString())) {
        if (reason) *reason = QStringLiteral("unsupported-protocol");
        return false;
    }
    if (!isSupportedE2ESuite(identity.value("suite").toString())) {
        if (reason) *reason = QStringLiteral("unsupported-suite");
        return false;
    }
    if (identity.value("userId").toString().trimmed().isEmpty()) {
        if (reason) *reason = QStringLiteral("invalid-peer");
        return false;
    }
    const QByteArray publicKey = fromBase64Url(identity.value("publicKey").toString());
    if (publicKey.isEmpty() || publicKey.size() > kMaxE2EIdentityPublicKeyBytes) {
        if (reason) *reason = QStringLiteral("invalid-public-key");
        return false;
    }
    if (identity.value("publicKeyFingerprintSha256").toString().trimmed().toLower() != e2eFingerprint(publicKey)) {
        if (reason) *reason = QStringLiteral("fingerprint-mismatch");
        return false;
    }
    const QString signatureSuite = identity.value("signatureSuite").toString().trimmed();
    if (!signatureSuite.isEmpty()
        && signatureSuite != e2eAgreementSignatureSuite()) {
        if (reason) *reason = QStringLiteral("unsupported-signature-suite");
        return false;
    }
    if (reason) reason->clear();
    return true;
}

void copyLargeFileRouteContext(QJsonObject* target, const QJsonObject& source) {
    if (!target) return;
    const QStringList keys = {
        QStringLiteral("deliveryState"),
        QStringLiteral("groupId"),
        QStringLiteral("targetUserId")
    };
    for (const QString& key : keys) {
        const QString value = source.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            (*target)[key] = value;
        }
    }
}

bool isServerGroupLargeFileOffer(const QJsonObject& event) {
    return event.value("deliveryState").toString() == QLatin1String("server-group-file")
        || !event.value("groupId").toString().trimmed().isEmpty();
}

class TlsTcpServer : public QTcpServer {
public:
    TlsTcpServer(const QSslCertificate& certificate, const QSslKey& privateKey, QObject* parent = nullptr)
        : QTcpServer(parent)
        , m_certificate(certificate)
        , m_privateKey(privateKey) {
    }

protected:
    void incomingConnection(qintptr socketDescriptor) override {
        QSslSocket* socket = new QSslSocket(this);
        socket->setLocalCertificate(m_certificate);
        socket->setPrivateKey(m_privateKey);
        socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        if (!socket->setSocketDescriptor(socketDescriptor)) {
            socket->deleteLater();
            return;
        }
        addPendingConnection(socket);
        socket->startServerEncryption();
    }

private:
    QSslCertificate m_certificate;
    QSslKey m_privateKey;
};

QTcpServer* createServerSocket(QObject* parent) {
    if (!envEnabled("QTNETWORKCHAT_TLS")) {
        return new QTcpServer(parent);
    }
    if (!QSslSocket::supportsSsl()) {
        QTcpServer* server = new QTcpServer(parent);
        server->setProperty("tlsConfigurationError",
                            QStringLiteral("Qt/OpenSSL is unavailable"));
        return server;
    }

    const QString certPath = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_TLS_CERT")).trimmed();
    const QString keyPath = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_TLS_KEY")).trimmed();
    QFile certFile(certPath);
    QFile keyFile(keyPath);
    if (certPath.isEmpty() || keyPath.isEmpty()
        || !certFile.open(QIODevice::ReadOnly)
        || !keyFile.open(QIODevice::ReadOnly)) {
        QTcpServer* server = new QTcpServer(parent);
        server->setProperty("tlsConfigurationError",
                            QStringLiteral("certificate or private key is not readable"));
        return server;
    }

    const QSslCertificate certificate(&certFile, QSsl::Pem);
    const QSslKey privateKey(&keyFile, QSsl::Rsa, QSsl::Pem);
    if (certificate.isNull() || privateKey.isNull()) {
        QTcpServer* server = new QTcpServer(parent);
        server->setProperty("tlsConfigurationError",
                            QStringLiteral("certificate or private key is invalid"));
        return server;
    }

    QTcpServer* server = new TlsTcpServer(certificate, privateKey, parent);
    server->setProperty("tlsEnabled", true);
    return server;
}

}

namespace ServerGroupSupport {
QString groupType(const QString& groupId) {
    return serverGroupTypeFromId(groupId);
}

QString filePolicy(const QString& groupType) {
    return serverGroupFilePolicy(groupType);
}

bool messageRateAllowed(const QString& groupId, const QString& userId, int perMinute) {
    return serverGroupMessageRateAllowed(groupId, userId, perMinute);
}

QString normalizeGroupId(QString groupId) {
    return normalizeServerGroupId(groupId);
}

QStringList initialMemberIds(const QJsonObject& obj, const QString& requesterId) {
    return initialServerGroupMemberIds(obj, requesterId);
}

QSqlDatabase openDatabase(const QString& connectionName) {
    return openAccountDatabase(connectionName);
}

bool openConnection(QSqlDatabase& db, const QString& scope) {
    return openAccountDatabaseConnection(db, scope);
}

void releaseDatabase(const QString& connectionName) {
    releaseAccountDatabase(connectionName);
}

ActorPermission actorPermission(QSqlDatabase& db, const QString& groupId, const QString& userId) {
    return loadServerGroupActorPermission(db, groupId, userId);
}

QString insertIgnore(const QString& table, const QStringList& columns,
                     const QStringList& values, const QStringList& conflictColumns) {
    return insertIgnoreSql(table, columns, values, conflictColumns);
}

QString insertReplace(const QString& table, const QStringList& columns,
                      const QStringList& values, const QStringList& conflictColumns,
                      const QStringList& updateAssignments) {
    return insertReplaceSql(table, columns, values, conflictColumns, updateAssignments);
}

void sendNotice(QTcpSocket* socket, const QString& content) {
    sendSystemNotice(socket, content);
}
}

Server::Server(QObject* parent)
    : QObject(parent)
    , m_tcpServer(createServerSocket(this))
    , m_redisService(new QQNTRedisService(this))
    , m_transferCleanupTimer(new QTimer(this))
    , m_offlineAttachmentCleanupTimer(new QTimer(this))
    , m_heartbeatMonitor(new HeartbeatMonitor(this))
    , m_serverPort(0)
    , m_tlsEnabled(m_tcpServer->property("tlsEnabled").toBool())
    , m_instanceId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_redisService->configureFromEnvironment();
    connect(m_redisService, &QQNTRedisService::messageReceived, this, [this](const RedisClient::PubSubMessage& message) {
        handleRedisMessageEvent(message.payload);
    });
    connect(m_redisService, &QQNTRedisService::readinessChanged, this, [this](bool ready, const QString& reason) {
        m_serviceReady = ready;
        m_serviceReadinessReason = reason;
    });

    connect(m_tcpServer, &QTcpServer::newConnection, this, &Server::onNewConnection);
    connect(m_transferCleanupTimer, &QTimer::timeout, this, &Server::cleanupExpiredFileTransfers);
    connect(m_offlineAttachmentCleanupTimer, &QTimer::timeout, this, &Server::cleanupExpiredOfflineAttachments);
    m_transferCleanupTimer->start(kTransferCleanupIntervalMs);
    m_offlineAttachmentCleanupTimer->start(kOfflineAttachmentCleanupIntervalMs);

    // Configure heartbeat monitor
    m_heartbeatMonitor->setTimeoutMs(90000); // 90 seconds timeout
    m_heartbeatMonitor->setEventLoopStallGraceMs(60000);
    m_heartbeatMonitor->setTimeoutCallback([this](const QString& clientId) {
        QTcpSocket* socket = m_userSockets.value(clientId);
        if (socket) {
            qWarning() << "Closing connection for timed out client:" << clientId;
            socket->disconnectFromHost();
        }
    });
    connect(m_heartbeatMonitor, &HeartbeatMonitor::clientTimedOut, this, [this](const QString& clientId) {
        qWarning() << "Heartbeat timeout detected for client:" << clientId;
    });
    connect(m_heartbeatMonitor, &HeartbeatMonitor::statsUpdated, this, [this](const HeartbeatStats& stats) {
        if (stats.timedOutClients > 0) {
            qDebug() << "Heartbeat stats - Total:" << stats.totalClients
                     << "Active:" << stats.activeClients
                     << "Timed out:" << stats.timedOutClients
                     << "Avg response:" << stats.avgResponseTimeMs << "ms";
        }
    });
    m_heartbeatMonitor->start(10000); // Check every 10 seconds
}

Server::~Server() {
    stop();
}

void Server::setObjectStoreFactoryForTesting(ObjectStoreFactory factory) {
    m_objectStoreFactoryForTesting = std::move(factory);
}

bool Server::start(quint16 port) {
    const QString tlsConfigurationError =
        m_tcpServer->property("tlsConfigurationError").toString();
    if (!tlsConfigurationError.isEmpty()) {
        qCritical() << "TLS requested but configuration is invalid:"
                    << tlsConfigurationError;
        return false;
    }
    if (!ensureRedisReadyForStartup()) {
        return false;
    }

    qInfo().noquote() << QStringLiteral("Account database: driver=%1 path=%2")
                            .arg(accountDatabaseDriver(), accountDatabasePath());
    ensureAccountDatabase();
    if (m_tcpServer->listen(QHostAddress::Any, port)) {
        if (!m_transferCleanupTimer->isActive()) {
            m_transferCleanupTimer->start(kTransferCleanupIntervalMs);
        }
        if (!m_offlineAttachmentCleanupTimer->isActive()) {
            m_offlineAttachmentCleanupTimer->start(kOfflineAttachmentCleanupIntervalMs);
        }
        cleanupExpiredOfflineAttachments();
        m_serverPort = port;
        refreshServiceReadiness();
        qDebug() << "Server started on port" << port << transportSecurityDescription();

        QList<QHostAddress> interfaces = QNetworkInterface::allAddresses();
        for (const QHostAddress& addr : interfaces) {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol && addr != QHostAddress::LocalHost) {
                qDebug() << "Server IP:" << addr.toString();
            }
        }
        return true;
    }
    return false;
}

QString Server::transportSecurityDescription() const {
    if (m_tlsEnabled) return "TLS 加密服务";
    const QString tlsConfigurationError = m_tcpServer->property("tlsConfigurationError").toString();
    if (!tlsConfigurationError.isEmpty()) {
        return QStringLiteral("TLS 配置无效，服务未启动: %1").arg(tlsConfigurationError);
    }
    return "普通 TCP 服务";
}

bool Server::ensureRedisReadyForStartup() {
    QString error;
    if (!m_redisService->initialize(&error)) {
        m_serviceReady = false;
        m_serviceReadinessReason = m_redisService->readinessReason();
        if (m_serviceReadinessReason == QStringLiteral("redis-required")) {
            qWarning() << "Redis is required for server startup; set QTNETWORKCHAT_REDIS=1";
        } else {
            qWarning() << "Redis startup check failed:" << error;
        }
        return false;
    }

    qDebug() << "Redis presence service enabled";
    qDebug() << "Redis Pub/Sub subscriber enabled";
    refreshServiceReadiness();
    return true;
}

void Server::updateRedisCommandAvailability(bool available, const QString& reason) {
    const bool wasReady = m_serviceReady;
    m_redisService->setCommandAvailability(available, reason);
    if (!available && wasReady) {
        qWarning() << "Redis command channel became unavailable:" << m_redisService->readinessReason();
    }
    refreshServiceReadiness();
}

void Server::updateRedisSubscriberAvailability(bool available, const QString& reason) {
    const bool wasReady = m_serviceReady;
    m_redisService->setSubscriberAvailability(available, reason);
    if (!available && wasReady) {
        qWarning() << "Redis subscriber channel became unavailable:" << m_redisService->readinessReason();
    }
    refreshServiceReadiness();
}

void Server::refreshServiceReadiness() {
    m_serviceReady = m_redisService->isReady();
    m_serviceReadinessReason = m_redisService->readinessReason();
}

void Server::tryRecoverRedisCommandAvailability() {
    m_redisService->recoverCommandAvailability();
    refreshServiceReadiness();
}

void Server::tryRecoverRedisSubscriberAvailability() {
    m_redisService->recoverSubscriberAvailability();
    refreshServiceReadiness();
}

bool Server::ensureServiceReady(QTcpSocket* socket, const QString& action) {
    tryRecoverRedisCommandAvailability();
    tryRecoverRedisSubscriberAvailability();
    if (m_serviceReady) {
        return true;
    }

    if (socket && socket->state() == QAbstractSocket::ConnectedState) {
        const QString actionName = action.trimmed().isEmpty() ? QStringLiteral("request") : action.trimmed();
        sendSystemNotice(socket,
                         QStringLiteral("服务暂不可用：Redis 未就绪，已拒绝 %1。")
                             .arg(actionName));
    }
    return false;
}

void Server::handleMessage(const QJsonObject& obj, QTcpSocket* socket) {
    Message msg;
    msg.type = static_cast<MessageType>(obj["messageType"].toInt(static_cast<int>(MessageType::Text)));
    msg.senderId = obj["senderId"].toString();
    msg.senderName = obj["senderName"].toString();
    msg.content = obj["content"].toString();
    msg.clientMessageId = obj["clientMessageId"].toString();
    msg.receiverId = obj["receiverId"].toString();
    msg.timestamp = QDateTime::currentDateTime();

    if (ChatUser* sender = findUserBySocket(socket)) {
        msg.senderId = sender->id;
        msg.senderName = sender->name;
        msg.senderAvatar = sender->avatar;
    }

    if (obj.value("e2eEnvelope").isObject()) {
        QString reason;
        const E2EEnvelope envelope = E2EEnvelope::fromJson(obj.value("e2eEnvelope").toObject());
        if (!envelope.isValid(&reason)
            || msg.receiverId.isEmpty()
            || envelope.senderId != msg.senderId
            || envelope.receiverId != msg.receiverId) {
            sendSystemNotice(socket, QStringLiteral("加密消息转发失败：端到端加密信封无效"));
            return;
        }
        msg.e2eEnvelope = envelope;
        if (msg.content.trimmed().isEmpty()) {
            msg.content = QStringLiteral("[encrypted]");
        }
    }

    QString deliveryState = "broadcast";
    if (!msg.receiverId.isEmpty()) {
        QTcpSocket* targetSocket = m_userSockets.value(msg.receiverId);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            deliveryState = "direct";
            sendToUser(msg);
        } else {
            bool redisOnline = false;
            if (isRedisUserOnline(msg.receiverId, &redisOnline) && redisOnline) {
                deliveryState = "remote";
            } else {
                deliveryState = "offline";
                sendToUser(msg);
            }
        }
    } else {
        ensurePublicGroupMembership(msg.senderId, socket);
        if (!isServerGroupMember("public", msg.senderId)) {
            sendSystemNotice(socket, "公共群消息发送失败：你已不在该群组，请联系群主或管理员重新邀请。");
            return;
        }
        broadcastMessage(msg);
    }
    saveMessageToSqlite(msg, deliveryState);
    const bool redisPublished = publishRedisMessageEvent(msg, deliveryState);
    if (deliveryState == "remote" && !redisPublished) {
        sendSystemNotice(socket, QStringLiteral("私聊消息发送失败：Redis 路由不可用，请等待服务恢复。"));
        return;
    }

    emit newMessage(msg);
}

void Server::handleE2EIdentityAnnouncement(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* sender = findUserBySocket(socket);
    if (!sender) {
        sendSystemNotice(socket, QStringLiteral("端到端加密身份公告失败：请先登录"));
        return;
    }

    const QString receiverId = obj.value("receiverId").toString().trimmed();
    const QJsonObject identity = obj.value("e2eIdentity").toObject();
    QString reason;
    if (!receiverId.isEmpty() && receiverId == sender->id) {
        sendSystemNotice(socket, QStringLiteral("端到端加密身份公告失败：请求无效"));
        return;
    }
    if (identity.value("userId").toString().trimmed() != sender->id
        || !validateE2EIdentityJson(identity, &reason)) {
        sendSystemNotice(socket, QStringLiteral("端到端加密身份公告失败：身份材料无效"));
        return;
    }

    QJsonObject forwarded;
    forwarded["type"] = QStringLiteral("e2e_identity_announce");
    forwarded["senderId"] = sender->id;
    forwarded["senderName"] = sender->name;
    forwarded["e2eIdentity"] = identity;
    if (!receiverId.isEmpty()) {
        forwarded["receiverId"] = receiverId;
        QTcpSocket* targetSocket = m_userSockets.value(receiverId);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            targetSocket->write(QJsonDocument(forwarded).toJson(QJsonDocument::Compact));
            targetSocket->write("\n");
            targetSocket->flush();
            return;
        }
        if (publishRedisE2EControlEvent(forwarded)) {
            return;
        }
        if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) {
            sendSystemNotice(socket, QStringLiteral("端到端加密身份公告失败：对方不在线，未缓存身份材料"));
            return;
        }
    }

    for (QTcpSocket* targetSocket : m_clients.keys()) {
        if (targetSocket == socket || targetSocket->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        targetSocket->write(QJsonDocument(forwarded).toJson(QJsonDocument::Compact));
        targetSocket->write("\n");
        targetSocket->flush();
    }
}

void Server::handleE2EKeyRotation(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* sender = findUserBySocket(socket);
    if (!sender) {
        sendSystemNotice(socket, QStringLiteral("端到端加密轮换失败：请先登录"));
        return;
    }

    const QString type = obj.value("type").toString();
    const QString receiverId = obj.value("receiverId").toString().trimmed();
    const E2EKeyAgreement agreement = E2EKeyAgreement::fromJson(obj.value("e2eKeyAgreement").toObject());
    QString reason;
    if ((type != QLatin1String("e2e_key_rotation_request") && type != QLatin1String("e2e_key_rotation_response"))
        || receiverId.isEmpty()
        || !agreement.isValid(&reason)
        || agreement.senderId != sender->id
        || agreement.receiverId != receiverId) {
        sendSystemNotice(socket, QStringLiteral("端到端加密轮换失败：请求无效"));
        return;
    }

    QTcpSocket* targetSocket = m_userSockets.value(receiverId);
    if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) {
        QJsonObject forwarded;
        forwarded["type"] = type;
        forwarded["senderId"] = sender->id;
        forwarded["senderName"] = sender->name;
        forwarded["receiverId"] = receiverId;
        forwarded["e2eKeyAgreement"] = agreement.toJson();
        forwarded["reason"] = obj.value("reason").toString(type == QLatin1String("e2e_key_rotation_request")
            ? QStringLiteral("manual-request")
            : QStringLiteral("accepted"));
        if (type == QLatin1String("e2e_key_rotation_response")) {
            forwarded["accepted"] = obj.value("accepted").toBool(false);
        }
        if (publishRedisE2EControlEvent(forwarded)) {
            return;
        }
        sendSystemNotice(socket, QStringLiteral("端到端加密轮换失败：对方不在线，未缓存轮换材料"));
        return;
    }

    QJsonObject forwarded;
    forwarded["type"] = type;
    forwarded["senderId"] = sender->id;
    forwarded["senderName"] = sender->name;
    forwarded["receiverId"] = receiverId;
    forwarded["e2eKeyAgreement"] = agreement.toJson();
    forwarded["reason"] = obj.value("reason").toString(type == QLatin1String("e2e_key_rotation_request")
        ? QStringLiteral("manual-request")
        : QStringLiteral("accepted"));
    if (type == QLatin1String("e2e_key_rotation_response")) {
        forwarded["accepted"] = obj.value("accepted").toBool(false);
    }

    targetSocket->write(QJsonDocument(forwarded).toJson(QJsonDocument::Compact));
    targetSocket->write("\n");
    targetSocket->flush();
}


void Server::handleMessageFavoriteUpdate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) {
        sendSystemNotice(socket, "收藏操作失败：请先登录");
        return;
    }
    const QString sessionId = obj.value("sessionId").toString().trimmed();
    const QString messageId = obj.value("messageId").toString().trimmed();
    const bool favorite = obj.value("favorite").toBool(false);
    if (sessionId.isEmpty() || messageId.isEmpty() || !ensureAccountDatabase()) {
        sendSystemNotice(socket, "收藏操作失败：请求参数无效");
        return;
    }

    QJsonObject message = obj.value("message").toObject();
    message["sessionId"] = sessionId;
    message["messageId"] = messageId;
    if (!message.contains("id") || message.value("id").toString().trimmed().isEmpty()) {
        message["id"] = messageId;
    }
    if (!message.contains("senderId")) message["senderId"] = QString();
    if (!message.contains("senderName")) message["senderName"] = QString();
    if (!message.contains("content")) message["content"] = QString();
    if (!message.contains("type")) message["type"] = QStringLiteral("text");
    if (!message.contains("timestamp")) message["timestamp"] = QString::number(QDateTime::currentMSecsSinceEpoch());

    QString errorText;
    bool changed = false;
    const QString connectionName = "server_message_favorite_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (!openAccountDatabaseConnection(db, connectionName)) {
            errorText = "收藏操作失败：无法打开收藏数据库";
        } else if (favorite) {
            QSqlQuery query(db);
            query.prepare(insertReplaceSql(
                QStringLiteral("server_message_favorites"),
                {QStringLiteral("user_id"), QStringLiteral("session_id"), QStringLiteral("message_id"), QStringLiteral("message_json"), QStringLiteral("created_at"), QStringLiteral("updated_at")},
                {QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("CURRENT_TIMESTAMP"), QStringLiteral("CURRENT_TIMESTAMP")},
                {QStringLiteral("user_id"), QStringLiteral("session_id"), QStringLiteral("message_id")},
                {QStringLiteral("message_json = EXCLUDED.message_json"), QStringLiteral("updated_at = EXCLUDED.updated_at")}));
            query.addBindValue(requester->id);
            query.addBindValue(sessionId);
            query.addBindValue(messageId);
            query.addBindValue(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
            changed = query.exec();
            if (!changed) errorText = query.lastError().text();
        } else {
            QSqlQuery query(db);
            query.prepare("DELETE FROM server_message_favorites WHERE user_id = ? AND session_id = ? AND message_id = ?");
            query.addBindValue(requester->id);
            query.addBindValue(sessionId);
            query.addBindValue(messageId);
            changed = query.exec();
            if (!changed) errorText = query.lastError().text();
        }
        db.close();
    }
    releaseAccountDatabase(connectionName);

    if (!changed) {
        sendSystemNotice(socket, errorText.isEmpty() ? "收藏操作失败" : QStringLiteral("收藏操作失败：%1").arg(errorText));
        return;
    }

    QJsonObject event;
    event["type"] = "message_favorite_updated";
    event["sessionId"] = sessionId;
    event["messageId"] = messageId;
    event["favorite"] = favorite;
    event["operatorId"] = requester->id;
    if (favorite) {
        event["message"] = message;
    }
    socket->write(QJsonDocument(event).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

void Server::refreshRedisPresence(const ChatUser& user) {
    if (!m_redisService->isEnabled()) return;
    if (!m_redisService->setPresence(user.id, user.name)) return;
    publishRedisPresenceEvent(user.id, QStringLiteral("online"));
}

void Server::clearRedisPresence(const QString& userId) {
    if (!m_redisService->isEnabled()) return;
    if (!m_redisService->clearPresence(userId)) return;
    publishRedisPresenceEvent(userId, QStringLiteral("offline"));
}

bool Server::publishRedisPresenceEvent(const QString& userId, const QString& action) const {
    if (!m_redisService->isEnabled() || userId.trimmed().isEmpty()) {
        return false;
    }

    QJsonObject event;
    event["eventType"] = QStringLiteral("presence_update");
    event["instanceId"] = m_instanceId;
    event["userId"] = userId.trimmed();
    event["action"] = action.trimmed().isEmpty() ? QStringLiteral("online") : action.trimmed();
    event["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return m_redisService->publish(QStringLiteral("messages"),
                                   QJsonDocument(event).toJson(QJsonDocument::Compact));
}

bool Server::publishRedisServerGroupSnapshotRefresh(const QStringList& userIds,
                                                    const QString& groupId,
                                                    const QString& notice,
                                                    const QString& memberId,
                                                    const QString& memberAction) const {
    if (!m_redisService->isEnabled() || groupId.trimmed().isEmpty()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    QJsonArray userIdArray;
    QStringList normalizedUserIds;
    for (const QString& userId : userIds) {
        const QString normalizedUserId = userId.trimmed();
        if (normalizedUserId.isEmpty() || normalizedUserIds.contains(normalizedUserId)) {
            continue;
        }
        normalizedUserIds << normalizedUserId;
        userIdArray.append(normalizedUserId);
    }
    if (userIdArray.isEmpty()) return false;

    QJsonObject event;
    event["eventType"] = QStringLiteral("server_group_snapshot_refresh");
    event["instanceId"] = m_instanceId;
    event["groupId"] = groupId.trimmed();
    event["userIds"] = userIdArray;
    event["notice"] = notice.left(200);
    const QString normalizedMemberId = memberId.trimmed();
    const QString normalizedMemberAction = memberAction.trimmed().toLower();
    if (!normalizedMemberId.isEmpty() && !normalizedMemberAction.isEmpty()) {
        event["memberId"] = normalizedMemberId;
        event["memberAction"] = normalizedMemberAction;
    }
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Skip Redis server group snapshot refresh because encoded payload is too large:"
                   << eventPayload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        return false;
    }
    return m_redisService->publish(QStringLiteral("messages"), eventPayload);
}

void Server::refreshConnectedClientViews() {
    struct ClientViewTarget {
        QTcpSocket* socket = nullptr;
        QString userId;
    };

    QVector<ClientViewTarget> targets;
    targets.reserve(m_clients.size());
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        QTcpSocket* clientSocket = it.key();
        if (!clientSocket || clientSocket->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        targets.append({clientSocket, it.value().id});
    }

    for (const ClientViewTarget& target : targets) {
        if (!target.socket || target.socket->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        sendUserList(target.socket);
        if (!target.socket || target.socket->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        sendFriendListSnapshot(target.userId, target.socket);
        if (!target.socket || target.socket->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        sendServerGroupSnapshot(target.userId, target.socket);
    }
}

bool Server::isRedisUserOnline(const QString& userId, bool* online) const {
    if (online) *online = false;
    if (!m_redisService->isEnabled() || userId.isEmpty()) return false;
    return m_redisService->queryPresence(userId, online);
}

bool Server::canPublishRedisMessageEvent(const Message& msg, const QString& deliveryState) const {
    if (!m_redisService->isEnabled()) return false;

    const bool isRedisFilePayload =
        (msg.type == MessageType::File || msg.type == MessageType::Image)
        && !msg.fileData.isEmpty()
        && msg.fileData.size() <= kRedisPubSubFileMaxBytes;
    if (msg.type != MessageType::Text && msg.type != MessageType::Private && !isRedisFilePayload) {
        return false;
    }

    const QJsonDocument messageDoc = QJsonDocument::fromJson(msg.toJson());
    if (!messageDoc.isObject()) return false;

    QJsonObject event;
    event["eventType"] = "chat_message";
    event["instanceId"] = m_instanceId;
    event["deliveryState"] = deliveryState;
    event["isPrivate"] = msg.isPrivate();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    event["message"] = messageDoc.object();

    return QJsonDocument(event).toJson(QJsonDocument::Compact).size() <= kRedisPubSubEventMaxBytes;
}

bool Server::publishRedisMessageEvent(const Message& msg, const QString& deliveryState) {
    if (!m_redisService->isEnabled()) return false;
    tryRecoverRedisCommandAvailability();
    if (!canPublishRedisMessageEvent(msg, deliveryState)) return false;

    const QJsonDocument messageDoc = QJsonDocument::fromJson(msg.toJson());
    if (!messageDoc.isObject()) return false;

    QJsonObject event;
    event["eventType"] = "chat_message";
    event["instanceId"] = m_instanceId;
    event["deliveryState"] = deliveryState;
    event["isPrivate"] = msg.isPrivate();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    event["message"] = messageDoc.object();

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Skip Redis Pub/Sub message event because encoded payload is too large:"
                   << eventPayload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        return false;
    }
    return m_redisService->publish("messages", eventPayload);
}

bool Server::publishRedisE2EControlEvent(const QJsonObject& forwarded) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();
    const QString receiverId = forwarded.value("receiverId").toString().trimmed();
    if (receiverId.isEmpty()) return false;

    QJsonObject event;
    event["eventType"] = "e2e_control";
    event["instanceId"] = m_instanceId;
    event["receiverId"] = receiverId;
    event["senderId"] = forwarded.value("senderId").toString();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    event["message"] = forwarded;

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Skip Redis E2E control event because encoded payload is too large:"
                   << eventPayload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        return false;
    }
    return m_redisService->publish("messages", eventPayload);
}

bool Server::publishRedisLargeFileOffer(const QJsonObject& offlinePayload) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    const QString objectKey = offlinePayload["objectStoreKey"].toString().trimmed();
    if (!FilesystemObjectStore::isValidObjectKey(objectKey)) return false;

    const qint64 fileSize = offlinePayload["fileSize"].toVariant().toLongLong();
    const qint64 chunkSize = offlinePayload["chunkSize"].toVariant().toLongLong();
    const qint64 chunkCount = offlinePayload["chunkCount"].toVariant().toLongLong();
    const QString fileHash = offlinePayload["fileHash"].toString().trimmed();
    if (fileSize <= 0 || chunkSize <= 0 || chunkCount <= 0 || !looksLikeSha256Hex(fileHash)) {
        return false;
    }

    const int messageType = offlinePayload["messageType"].toInt(static_cast<int>(MessageType::File));
    if (messageType != static_cast<int>(MessageType::File)
        && messageType != static_cast<int>(MessageType::Image)) {
        return false;
    }

    const QString storedTransferId = offlinePayload["transferId"].toString().trimmed();
    QJsonObject event;
    event["eventType"] = "large_file_offer";
    event["instanceId"] = m_instanceId;
    event["transferId"] = storedTransferId.isEmpty() ? objectKey : storedTransferId;
    event["objectKey"] = objectKey;
    event["senderId"] = offlinePayload["senderId"].toString();
    event["senderName"] = offlinePayload["senderName"].toString();
    event["receiverId"] = offlinePayload["receiverId"].toString();
    event["messageType"] = messageType == static_cast<int>(MessageType::Image) ? "Image" : "File";
    event["storeType"] = objectStoreType();
    event["fileName"] = offlinePayload["fileName"].toString();
    event["fileSize"] = QString::number(fileSize);
    event["fileHash"] = fileHash;
    event["chunkSize"] = QString::number(chunkSize);
    event["chunkCount"] = QString::number(chunkCount);
    event["expiresAt"] = offlinePayload["objectStoreExpiresAt"].toString();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    appendE2EFileFields(&event, offlinePayload);
    copyLargeFileRouteContext(&event, offlinePayload);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Skip large file offer because encoded payload is too large:"
                   << eventPayload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        logRedisLargeFileRouteEvent(QStringLiteral("offer"),
                                    QStringLiteral("skipped"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                    QStringLiteral("payload-too-large"),
                                    fileSize);
        return false;
    }
    const bool published = m_redisService->publish("messages", eventPayload);
    logRedisLargeFileRouteEvent(QStringLiteral("offer"),
                                published ? QStringLiteral("published") : QStringLiteral("publish-failed"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                published ? QString() : m_redisService->lastError(),
                                fileSize);
    return published;
}

bool Server::publishRedisLargeFileClaim(const QJsonObject& offer) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    QJsonObject event;
    event["eventType"] = "large_file_claim";
    event["instanceId"] = m_instanceId;
    event["sourceInstanceId"] = offer["instanceId"].toString();
    event["transferId"] = offer["transferId"].toString();
    event["objectKey"] = offer["objectKey"].toString();
    event["receiverId"] = offer["receiverId"].toString();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    copyLargeFileRouteContext(&event, offer);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        logRedisLargeFileRouteEvent(QStringLiteral("claim"),
                                    QStringLiteral("skipped"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                    QStringLiteral("payload-too-large"));
        return false;
    }
    const bool published = m_redisService->publish("messages", eventPayload);
    logRedisLargeFileRouteEvent(QStringLiteral("claim"),
                                published ? QStringLiteral("published") : QStringLiteral("publish-failed"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                published ? QString() : m_redisService->lastError());
    return published;
}

bool Server::publishRedisLargeFileDelivered(const QJsonObject& offer, qint64 confirmedBytes) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    QJsonObject event;
    event["eventType"] = "large_file_delivered";
    event["instanceId"] = m_instanceId;
    event["sourceInstanceId"] = offer["instanceId"].toString();
    event["transferId"] = offer["transferId"].toString();
    event["objectKey"] = offer["objectKey"].toString();
    event["receiverId"] = offer["receiverId"].toString();
    event["confirmedBytes"] = QString::number(confirmedBytes);
    event["fileHash"] = offer["fileHash"].toString();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    appendE2EFileFields(&event, offer);
    copyLargeFileRouteContext(&event, offer);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        logRedisLargeFileRouteEvent(QStringLiteral("delivered"),
                                    QStringLiteral("skipped"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                    QStringLiteral("payload-too-large"),
                                    confirmedBytes);
        return false;
    }
    const bool published = m_redisService->publish("messages", eventPayload);
    logRedisLargeFileRouteEvent(QStringLiteral("delivered"),
                                published ? QStringLiteral("published") : QStringLiteral("publish-failed"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                published ? QString() : m_redisService->lastError(),
                                confirmedBytes);
    return published;
}

bool Server::publishRedisLargeFileFailed(const QJsonObject& offer, const QString& reason) const {
    if (!m_redisService->isEnabled()) return false;
    const_cast<Server*>(this)->tryRecoverRedisCommandAvailability();

    QJsonObject event;
    event["eventType"] = "large_file_failed";
    event["instanceId"] = m_instanceId;
    event["sourceInstanceId"] = offer["instanceId"].toString();
    event["transferId"] = offer["transferId"].toString();
    event["objectKey"] = offer["objectKey"].toString();
    event["receiverId"] = offer["receiverId"].toString();
    event["fileHash"] = offer["fileHash"].toString();
    event["reason"] = reason.left(160);
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    appendE2EFileFields(&event, offer);
    copyLargeFileRouteContext(&event, offer);

    const QByteArray eventPayload = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (eventPayload.size() > kRedisPubSubEventMaxBytes) {
        logRedisLargeFileRouteEvent(QStringLiteral("failed"),
                                    QStringLiteral("skipped"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                    QStringLiteral("payload-too-large"));
        return false;
    }
    const bool published = m_redisService->publish("messages", eventPayload);
    logRedisLargeFileRouteEvent(QStringLiteral("failed"),
                                published ? QStringLiteral("published") : QStringLiteral("publish-failed"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("publish")),
                                published ? reason : m_redisService->lastError());
    return published;
}

void Server::handleRedisMessageEvent(const QByteArray& payload) {
    if (payload.size() > kRedisPubSubEventMaxBytes) {
        qWarning() << "Ignore Redis Pub/Sub message event because encoded payload is too large:"
                   << payload.size()
                   << "limit:" << kRedisPubSubEventMaxBytes;
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) return;

    const QJsonObject event = doc.object();
    const QString eventType = event["eventType"].toString();
    if (eventType == "server_group_snapshot_refresh") {
        handleRedisServerGroupSnapshotRefresh(event);
        return;
    }
    if (eventType == "e2e_control") {
        handleRedisE2EControlEvent(event);
        return;
    }
    if (eventType == "large_file_offer") {
        handleRedisLargeFileOffer(event);
        return;
    }
    if (eventType == "large_file_delivered") {
        handleRedisLargeFileDelivered(event);
        return;
    }
    if (eventType == "large_file_failed") {
        handleRedisLargeFileFailed(event);
        return;
    }
    if (eventType == "presence_update") {
        if (event["instanceId"].toString() == m_instanceId) return;
        refreshConnectedClientViews();
        return;
    }
    if (eventType != "chat_message") return;
    if (event["instanceId"].toString() == m_instanceId) return;

    const QJsonObject messageObj = event["message"].toObject();
    if (messageObj.isEmpty()) return;
    const QString deliveryState = event.value("deliveryState").toString();

    const Message msg = Message::fromJson(QJsonDocument(messageObj).toJson(QJsonDocument::Compact));
    if (msg.senderId.isEmpty()) return;
    const bool isRedisFilePayload =
        (msg.type == MessageType::File || msg.type == MessageType::Image)
        && !msg.fileData.isEmpty()
        && msg.fileData.size() <= kRedisPubSubFileMaxBytes;
    if (msg.type != MessageType::Text && msg.type != MessageType::Private && !isRedisFilePayload) return;

    if (msg.receiverId.isEmpty()) {
        broadcastMessage(msg);
    } else {
        if (deliveryState == QLatin1String("server-group") && msg.type == MessageType::Text) {
            const QStringList groupMemberIds = serverGroupMemberIds(msg.receiverId);
            if (!groupMemberIds.isEmpty()) {
                for (const QString& memberId : groupMemberIds) {
                    QTcpSocket* memberSocket = m_userSockets.value(memberId);
                    if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
                    QJsonObject forwarded = QJsonDocument::fromJson(msg.toJson()).object();
                    forwarded["type"] = "server_group_message";
                    forwarded["groupId"] = msg.receiverId;
                    memberSocket->write(QJsonDocument(forwarded).toJson(QJsonDocument::Compact));
                    memberSocket->write("\n");
                    memberSocket->flush();
                }
                emit newMessage(msg);
                return;
            }
        }
        if (deliveryState == QLatin1String("server-group-file") && isRedisFilePayload) {
            const QStringList groupMemberIds = serverGroupMemberIds(msg.receiverId);
            if (groupMemberIds.isEmpty()) return;
            for (const QString& memberId : groupMemberIds) {
                if (memberId == msg.senderId) continue;
                QTcpSocket* memberSocket = m_userSockets.value(memberId);
                if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
                Message groupMsg = msg;
                groupMsg.receiverId = msg.receiverId;
                if (!sendChunkedFileToSocket(groupMsg, memberSocket)) {
                    qWarning() << "Redis private group file delivery failed"
                               << msg.receiverId << memberId << msg.fileName;
                }
            }
            emit newMessage(msg);
            return;
        }
        QTcpSocket* targetSocket = m_userSockets.value(msg.receiverId);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            sendToUser(msg);
        } else {
            return;
        }
    }

    emit newMessage(msg);
}

void Server::handleRedisServerGroupSnapshotRefresh(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;

    const QString groupId = event.value("groupId").toString().trimmed();
    if (groupId.isEmpty()) return;

    const QString notice = event.value("notice").toString().trimmed();
    QStringList userIds;
    const QJsonArray userIdArray = event.value("userIds").toArray();
    for (const QJsonValue& userIdValue : userIdArray) {
        const QString userId = userIdValue.toString().trimmed();
        if (!userId.isEmpty() && !userIds.contains(userId)) {
            userIds << userId;
        }
    }

    for (const QString& userId : userIds) {
        QTcpSocket* targetSocket = m_userSockets.value(userId);
        if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) continue;
        const bool isActiveMember = isServerGroupMember(groupId, userId);
        if (!isActiveMember && !isServerGroupRemovedMember(groupId, userId)) continue;
        if (!notice.isEmpty()) {
            sendSystemNotice(targetSocket, notice);
        }
        const QString memberId = event.value("memberId").toString().trimmed();
        const QString memberAction = event.value("memberAction").toString().trimmed().toLower();
        if (isActiveMember && !memberId.isEmpty() && !memberAction.isEmpty()) {
            sendServerGroupMemberUpdated(targetSocket, groupId, memberId, memberAction);
        }
        sendServerGroupSnapshot(userId, targetSocket);
    }
}

void Server::handleRedisE2EControlEvent(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;
    const QString receiverId = event.value("receiverId").toString().trimmed();
    if (receiverId.isEmpty()) return;

    QTcpSocket* targetSocket = m_userSockets.value(receiverId);
    if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const QJsonObject message = event.value("message").toObject();
    const QString type = message.value("type").toString();
    if ((type != QLatin1String("e2e_identity_announce")
         && type != QLatin1String("e2e_key_rotation_request")
         && type != QLatin1String("e2e_key_rotation_response"))
        || message.value("receiverId").toString().trimmed() != receiverId
        || message.value("senderId").toString().trimmed().isEmpty()) {
        return;
    }

    targetSocket->write(QJsonDocument(message).toJson(QJsonDocument::Compact));
    targetSocket->write("\n");
    targetSocket->flush();
}

void Server::handleRedisLargeFileOffer(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return;

    auto messageFromOffer = [&event](const QString& receiverId) {
        Message msg;
        msg.senderId = event["senderId"].toString();
        msg.senderName = event["senderName"].toString();
        msg.receiverId = receiverId;
        msg.fileName = event["fileName"].toString();
        msg.transferId = event["transferId"].toString();
        msg.fileSize = event["fileSize"].toVariant().toLongLong();
        msg.fileHash = event["fileHash"].toString();
        msg.chunkSize = event["chunkSize"].toVariant().toLongLong();
        msg.chunkCount = event["chunkCount"].toVariant().toLongLong();
        msg.type = event["messageType"].toString() == "Image" ? MessageType::Image : MessageType::File;
        msg.content = QString(msg.type == MessageType::Image ? "发送了图片: %1" : "发送了文件: %1").arg(msg.fileName);
        msg.e2eFileEncrypted = event["e2eFileEncrypted"].toBool(false);
        msg.e2eFileKeyId = event["e2eFileKeyId"].toString();
        msg.e2eFileKeyFingerprint = event["e2eFileKeyFingerprintSha256"].toString();
        msg.e2eFilePlainSize = event["e2eFilePlainSize"].toVariant().toLongLong();
        msg.e2eFilePlainHash = event["e2eFilePlainHash"].toString();
        if (event.value("e2eEnvelope").isObject()) {
            const QJsonObject envelopeObject = event.value("e2eEnvelope").toObject();
            const E2EEnvelope envelope = E2EEnvelope::fromJson(envelopeObject);
            if (envelope.isValid()) {
                msg.e2eEnvelope = envelope;
            } else if (e2eEnvelopeHeaderLooksSafe(envelopeObject)) {
                msg.e2eEnvelopeHeader = envelopeObject;
            }
        }
        msg.timestamp = QDateTime::currentDateTime();
        return msg;
    };

    if (isServerGroupLargeFileOffer(event)) {
        QString groupId = event.value("groupId").toString().trimmed();
        if (groupId.isEmpty()) {
            groupId = event.value("receiverId").toString().trimmed();
        }
        const QString senderId = event.value("senderId").toString().trimmed();
        if (groupId.isEmpty() || groupId == QLatin1String("public") || senderId.isEmpty()) return;

        const QStringList memberIds = serverGroupMemberIds(groupId);
        if (memberIds.isEmpty()) return;
        bool deliveredAny = false;
        for (const QString& memberId : memberIds) {
            if (memberId.isEmpty() || memberId == senderId) continue;
            QTcpSocket* memberSocket = m_userSockets.value(memberId);
            if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
            if (!isServerGroupMember(groupId, memberId)) continue;

            QJsonObject targetedEvent = event;
            targetedEvent["receiverId"] = groupId;
            targetedEvent["groupId"] = groupId;
            targetedEvent["targetUserId"] = memberId;
            if (deliverRedisLargeFileOffer(targetedEvent, memberSocket, false)) {
                deliveredAny = true;
            } else {
                qWarning() << "Redis private group large file delivery failed"
                           << groupId << memberId << event.value("fileName").toString();
            }
        }
        if (deliveredAny) {
            emit newMessage(messageFromOffer(groupId));
        }
        return;
    }

    const QString receiverId = event["receiverId"].toString().trimmed();
    if (receiverId.isEmpty()) return;

    QTcpSocket* targetSocket = m_userSockets.value(receiverId);
    if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    if (deliverRedisLargeFileOffer(event, targetSocket)) {
        emit newMessage(messageFromOffer(receiverId));
    }
}

void Server::handleRedisLargeFileDelivered(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;
    if (event["sourceInstanceId"].toString() != m_instanceId) return;
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return;
    const LargeFileDeliveredReceiptDecision reconcileDecision =
        evaluateRedisLargeFileDeliveredReceipt(event);
    logRedisLargeFileRouteEvent(QStringLiteral("delivered_reconcile"),
                                reconcileDecision.shouldCleanup ? QStringLiteral("cleaned") : QStringLiteral("retained"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("reconcile")),
                                reconcileDecision.reason,
                                event["confirmedBytes"].toVariant().toLongLong());
    const LargeFileCleanupResult cleanupResult = cleanupDeliveredRedisLargeFile(event);
    persistRedisLargeFileDeliveredReceiptSummary(event, reconcileDecision, cleanupResult.queueCleaned);
    logRedisLargeFileRouteEvent(QStringLiteral("delivered_cleanup"),
                                cleanupResult.queueCleaned ? QStringLiteral("cleaned") : QStringLiteral("retained"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("delete")),
                                cleanupResult.queueCleaned ? QString() : QStringLiteral("offline-fallback-not-matched"),
                                event["confirmedBytes"].toVariant().toLongLong());
    if (cleanupResult.objectDeleteAttempted) {
        logRedisLargeFileRouteEvent(QStringLiteral("object_delete"),
                                    cleanupResult.objectDeleted ? QStringLiteral("deleted") : QStringLiteral("retained"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("delete")),
                                    cleanupResult.objectDeleteReason,
                                    event["confirmedBytes"].toVariant().toLongLong());
    }
}

void Server::handleRedisLargeFileFailed(const QJsonObject& event) {
    if (event["instanceId"].toString() == m_instanceId) return;
    if (event["sourceInstanceId"].toString() != m_instanceId) return;
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return;

    const QString objectKey = event["objectKey"].toString().trimmed();
    const QString transferId = event["transferId"].toString().trimmed();
    const QString receiverId = event["receiverId"].toString().trimmed();
    const QString fileHash = event["fileHash"].toString().trimmed();
    if (!FilesystemObjectStore::isValidObjectKey(objectKey)
        || transferId.isEmpty()
        || receiverId.isEmpty()
        || !looksLikeSha256Hex(fileHash)) {
        return;
    }

    qWarning() << "Large file object routing failed on remote instance; origin offline fallback remains"
               << receiverId << transferId << objectKey << event["reason"].toString();
    logRedisLargeFileRouteEvent(QStringLiteral("failed_received"),
                                QStringLiteral("fallback-retained"),
                                largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("fallback")),
                                event["reason"].toString());
}

bool Server::deliverRedisLargeFileOffer(const QJsonObject& event, QTcpSocket* socket, bool publishDeliveredReceipt) {
    QPointer<QTcpSocket> socketGuard(socket);
    auto logDeliveryFailure = [this, &event](const QString& reason, qint64 bytes = 0) {
        logRedisLargeFileRouteEvent(QStringLiteral("offer_delivery"),
                                    QStringLiteral("failed"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("deliver")),
                                    reason,
                                    bytes);
    };
    auto failOffer = [this, &event, publishDeliveredReceipt](const QString& reason) {
        if (publishDeliveredReceipt) {
            publishRedisLargeFileFailed(event, reason);
        }
        return false;
    };

    if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState) {
        logDeliveryFailure(QStringLiteral("receiver-disconnected"));
        return failOffer(QStringLiteral("receiver-disconnected"));
    }

    if (!isSupportedObjectStoreType(objectStoreType())) {
        return false;
    }
    const QString localStoreType = objectStoreType();
    const QString offerStoreType = normalizeObjectStoreType(event["storeType"].toString());
    if (!isSupportedObjectStoreType(offerStoreType)) {
        logRedisLargeFileRouteEvent(QStringLiteral("offer_validation"),
                                    QStringLiteral("rejected"),
                                    largeFileRouteLogMetadata(event, localStoreType, QStringLiteral("validate")),
                                    QStringLiteral("unsupported-offer-store-type"));
        return failOffer(QStringLiteral("unsupported-offer-store-type"));
    }
    if (offerStoreType != localStoreType) {
        logRedisLargeFileRouteEvent(QStringLiteral("offer_validation"),
                                    QStringLiteral("rejected"),
                                    largeFileRouteLogMetadata(event, localStoreType, QStringLiteral("validate")),
                                    QStringLiteral("object-store-type-mismatch"));
        return failOffer(QStringLiteral("object-store-type-mismatch"));
    }

    QString objectStoreError;
    std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore(&objectStoreError);
    if (!objectStore) {
        if (!objectStoreError.isEmpty()) {
            qWarning() << "Skip large file offer because object store is unavailable" << objectStoreError;
        }
        return failOffer(QStringLiteral("object-store-unavailable: ") + objectStoreError);
    }

    const QString objectKey = event["objectKey"].toString().trimmed();
    const qint64 fileSize = event["fileSize"].toVariant().toLongLong();
    const qint64 chunkSize = event["chunkSize"].toVariant().toLongLong();
    const qint64 chunkCount = event["chunkCount"].toVariant().toLongLong();
    const QString fileHash = event["fileHash"].toString().trimmed();
    const QString messageType = event["messageType"].toString();
    if (!FilesystemObjectStore::isValidObjectKey(objectKey)
        || fileSize <= 0
        || chunkSize <= 0
        || chunkSize > kForwardChunkBytes
        || chunkCount <= 0
        || chunkCount != (fileSize + chunkSize - 1) / chunkSize
        || !looksLikeSha256Hex(fileHash)
        || event["senderId"].toString().trimmed().isEmpty()
        || event["receiverId"].toString().trimmed().isEmpty()
        || (messageType != "File" && messageType != "Image")) {
        return failOffer(QStringLiteral("invalid-offer-metadata"));
    }

    const ObjectStore::ValidationResult validation =
        objectStore->validateObject(objectKey, fileSize, fileHash);
    if (!validation.ok) {
        const QString reason = s3ValidationFailureReasonForLog(validation);
        qWarning() << "Rejected large file offer because object validation failed"
                   << objectKey << reason;
        logRedisLargeFileRouteEvent(QStringLiteral("offer_validation"),
                                    QStringLiteral("rejected"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("validate")),
                                    reason,
                                    fileSize);
        return failOffer(reason);
    }

    std::unique_ptr<QIODevice> file = objectStore->openObject(objectKey);
    if (!file || !file->isOpen()) {
        QString reason = QStringLiteral("object-open-failed");
        const QString storeReason = objectStore->lastOpenFailureReason();
        if (!storeReason.isEmpty()) {
            reason = objectStoreOpenFailureReasonForLog(objectStoreType(), storeReason);
        }
        qWarning() << "Rejected large file offer because object open failed"
                   << objectKey << reason;
        logRedisLargeFileRouteEvent(QStringLiteral("offer_read"),
                                    QStringLiteral("rejected"),
                                    largeFileRouteLogMetadata(event, objectStoreType(), QStringLiteral("read")),
                                    reason,
                                    fileSize);
        return failOffer(reason);
    }

    publishRedisLargeFileClaim(event);

    const QString transferId = event["transferId"].toString().trimmed().isEmpty()
        ? objectKey
        : event["transferId"].toString().trimmed();
    const QString fileName = event["fileName"].toString();
    for (qint64 index = 0; index < chunkCount; ++index) {
        if (!file->seek(index * chunkSize)) {
            logDeliveryFailure(QStringLiteral("object-seek-failed"), index * chunkSize);
            return failOffer(QStringLiteral("object-seek-failed"));
        }
        const QByteArray chunk = file->read(chunkSize);
        if (chunk.isEmpty() || (index < chunkCount - 1 && chunk.size() != chunkSize)) {
            logDeliveryFailure(QStringLiteral("object-read-failed"), index * chunkSize);
            return failOffer(QStringLiteral("object-read-failed"));
        }

        QJsonObject chunkObj;
        chunkObj["type"] = "file_chunk";
        chunkObj["transferId"] = transferId;
        chunkObj["messageType"] = messageType == "Image" ? static_cast<int>(MessageType::Image) : static_cast<int>(MessageType::File);
        chunkObj["senderId"] = event["senderId"].toString();
        chunkObj["senderName"] = event["senderName"].toString();
        chunkObj["receiverId"] = event["receiverId"].toString();
        chunkObj["content"] = QString(messageType == "Image" ? "发送了图片: %1" : "发送了文件: %1").arg(fileName);
        chunkObj["fileName"] = fileName;
        chunkObj["fileSize"] = QString::number(fileSize);
        chunkObj["fileHash"] = fileHash;
        chunkObj["chunkSize"] = QString::number(chunkSize);
        chunkObj["chunkCount"] = QString::number(chunkCount);
        chunkObj["chunkIndex"] = QString::number(index);
        chunkObj["fileData"] = QString::fromLatin1(chunk.toBase64());
        appendE2EFileFields(&chunkObj, event);

        QString ackRejectReason;
        qint64 ackReceivedBytes = 0;
        bool acknowledged = false;
        const QByteArray data = QJsonDocument(chunkObj).toJson(QJsonDocument::Compact);
        for (int attempt = 1; attempt <= kChunkSendMaxAttempts; ++attempt) {
            if (sendFileChunkAndWaitForAck(socketGuard, data, transferId, index, &ackRejectReason, &ackReceivedBytes)) {
                const qint64 expectedAckBytes = qMin(fileSize, index * chunkSize + chunk.size());
                if (ackReceivedBytes > 0 && (ackReceivedBytes < expectedAckBytes || ackReceivedBytes > fileSize)) {
                    if (attempt == kChunkSendMaxAttempts) {
                        ackRejectReason = QString::fromUtf8("跨实例大文件确认进度非法");
                    }
                    continue;
                }
                acknowledged = true;
                break;
            }
            if (!socketGuard) {
                logDeliveryFailure(QStringLiteral("receiver-disconnected"), index * chunkSize);
                return failOffer(QStringLiteral("receiver-disconnected"));
            }
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "Large file offer chunk rejected by receiver:" << ackRejectReason;
                logDeliveryFailure(QStringLiteral("chunk-rejected"), qMin(fileSize, index * chunkSize + chunk.size()));
                return failOffer(QStringLiteral("chunk-rejected: ") + ackRejectReason);
            }
        }
        if (!acknowledged) {
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "Large file offer chunk rejected by receiver:" << ackRejectReason;
                logDeliveryFailure(QStringLiteral("chunk-rejected"), qMin(fileSize, index * chunkSize + chunk.size()));
                return failOffer(QStringLiteral("chunk-rejected: ") + ackRejectReason);
            }
            qWarning() << "Large file offer chunk ack timeout:" << fileName << index + 1 << "/" << chunkCount;
            logDeliveryFailure(QStringLiteral("chunk-ack-timeout"), qMin(fileSize, index * chunkSize + chunk.size()));
            return failOffer(QStringLiteral("chunk-ack-timeout"));
        }
    }

    if (publishDeliveredReceipt) {
        publishRedisLargeFileDelivered(event, fileSize);
    }
    return true;
}

ChatUser* Server::findUserBySocket(QTcpSocket* socket) {
    auto it = m_clients.find(socket);
    if (it != m_clients.end()) {
        return &it.value();
    }
    return nullptr;
}

bool Server::recordDefaultGroupMembership(const ChatUser& user) const {
    if (user.id.trimmed().isEmpty() || !ensureAccountDatabase()) return false;

    const QString normalizedUserId = user.id.trimmed();
    const QString normalizedUserName = user.name.trimmed().isEmpty() ? normalizedUserId : user.name.trimmed();
    QString connectionName = "default_group_member_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            if (db.transaction()) {
                QSqlQuery groupQuery(db);
                ok = groupQuery.exec(insertIgnoreSql(
                    QStringLiteral("server_groups"),
                    {QStringLiteral("group_id"), QStringLiteral("group_name"), QStringLiteral("announcement"), QStringLiteral("group_type"), QStringLiteral("history_policy"), QStringLiteral("file_policy"), QStringLiteral("created_at"), QStringLiteral("updated_at")},
                    {QStringLiteral("'public'"), QStringLiteral("'公共聊天室'"), QStringLiteral("'欢迎来到公共聊天室。'"), QStringLiteral("'public'"), QStringLiteral("'public-removed-readonly'"), QStringLiteral("'public-members-only'"), QStringLiteral("CURRENT_TIMESTAMP"), QStringLiteral("CURRENT_TIMESTAMP")},
                    {QStringLiteral("group_id")}));

                if (ok) {
                    QSqlQuery repairGroupQuery(db);
                    repairGroupQuery.prepare("UPDATE server_groups "
                                             "SET group_name = COALESCE(NULLIF(group_name, ''), '公共聊天室'), "
                                             "announcement = COALESCE(NULLIF(announcement, ''), '欢迎来到公共聊天室。'), "
                                             "group_type = 'public', "
                                             "history_policy = COALESCE(NULLIF(history_policy, ''), 'public-removed-readonly'), "
                                             "file_policy = COALESCE(NULLIF(file_policy, ''), 'public-members-only'), "
                                             "updated_at = CURRENT_TIMESTAMP "
                                             "WHERE group_id = 'public'");
                    ok = repairGroupQuery.exec();
                }

                const bool wasRemovedFromPublic = [&]() {
                    QSqlQuery removedQuery(db);
                    removedQuery.prepare("SELECT COUNT(*) FROM server_group_removed_members WHERE group_id = 'public' AND user_id = ?");
                    removedQuery.addBindValue(normalizedUserId);
                    return removedQuery.exec() && removedQuery.next() && removedQuery.value(0).toInt() > 0;
                }();

                if (ok && wasRemovedFromPublic) {
                    ok = db.commit();
                    db.close();
                    releaseAccountDatabase(connectionName);
                    return ok;
                }

                if (ok) {
                    QSqlQuery insertMemberQuery(db);
                    insertMemberQuery.prepare(insertReplaceSql(
                        QStringLiteral("server_group_members"),
                        {QStringLiteral("group_id"), QStringLiteral("user_id"), QStringLiteral("user_name"), QStringLiteral("role"), QStringLiteral("joined_at"), QStringLiteral("updated_at")},
                        {QStringLiteral("'public'"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("COALESCE((SELECT role FROM server_group_members WHERE group_id = 'public' AND user_id = ?), 'member')"), QStringLiteral("CURRENT_TIMESTAMP"), QStringLiteral("CURRENT_TIMESTAMP")},
                        {QStringLiteral("group_id"), QStringLiteral("user_id")},
                        {QStringLiteral("user_name = EXCLUDED.user_name"),
                         QStringLiteral("role = CASE WHEN server_group_members.role = 'owner' THEN 'owner' ELSE EXCLUDED.role END"),
                         QStringLiteral("updated_at = EXCLUDED.updated_at")}));
                    insertMemberQuery.addBindValue(normalizedUserId);
                    insertMemberQuery.addBindValue(normalizedUserName);
                    insertMemberQuery.addBindValue(normalizedUserId);
                    ok = insertMemberQuery.exec();
                    if (!ok) {
                        qWarning() << "Failed to upsert public group member:" << insertMemberQuery.lastError().text();
                    }
                }

                QString ownerId;
                if (ok) {
                    QSqlQuery ownerQuery(db);
                    ownerQuery.prepare("SELECT COALESCE(owner_id, '') FROM server_groups WHERE group_id = 'public'");
                    ok = ownerQuery.exec();
                    if (ok && ownerQuery.next()) {
                        ownerId = ownerQuery.value(0).toString().trimmed();
                    }
                }
                if (ok && !ownerId.isEmpty()) {
                    QSqlQuery ownerMemberQuery(db);
                    ownerMemberQuery.prepare("SELECT COUNT(*) FROM server_group_members WHERE group_id = 'public' AND user_id = ?");
                    ownerMemberQuery.addBindValue(ownerId);
                    ok = ownerMemberQuery.exec();
                    if (ok && ownerMemberQuery.next() && ownerMemberQuery.value(0).toInt() == 0) {
                        ownerId.clear();
                    }
                }
                if (ok && ownerId.isEmpty()) {
                    ownerId = normalizedUserId;
                    QSqlQuery updateOwnerQuery(db);
                    updateOwnerQuery.prepare("UPDATE server_groups SET owner_id = ?, updated_at = CURRENT_TIMESTAMP "
                                             "WHERE group_id = 'public'");
                    updateOwnerQuery.addBindValue(ownerId);
                    ok = updateOwnerQuery.exec();
                }
                if (ok) {
                    QSqlQuery backfillMembersQuery(db);
                    const QString currentOwnerSql = QStringLiteral("COALESCE((SELECT owner_id FROM server_groups WHERE group_id = 'public'), '')");
                    if (accountDatabaseIsPostgres()) {
                        ok = backfillMembersQuery.exec(
                            "INSERT INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) "
                            "SELECT 'public', a.account, COALESCE(NULLIF(a.user_name, ''), a.account), "
                            "CASE WHEN a.account = " + currentOwnerSql + " THEN 'owner' ELSE COALESCE(NULLIF(m.role, ''), 'member') END, "
                            "COALESCE(m.joined_at, CURRENT_TIMESTAMP::text), CURRENT_TIMESTAMP::text "
                            "FROM accounts a "
                            "LEFT JOIN server_group_members m ON m.group_id = 'public' AND m.user_id = a.account "
                            "WHERE COALESCE(a.account_status, 'active') = 'active' "
                            "AND NOT EXISTS (SELECT 1 FROM server_group_removed_members r WHERE r.group_id = 'public' AND r.user_id = a.account) "
                            "ON CONFLICT(group_id, user_id) DO UPDATE SET "
                            "user_name = EXCLUDED.user_name, "
                            "role = CASE WHEN server_group_members.user_id = " + currentOwnerSql + " THEN 'owner' "
                            "WHEN server_group_members.role = 'owner' THEN 'member' ELSE server_group_members.role END, "
                            "updated_at = CURRENT_TIMESTAMP::text");
                    } else {
                        ok = backfillMembersQuery.exec(
                            "INSERT OR REPLACE INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) "
                            "SELECT 'public', a.account, COALESCE(NULLIF(a.user_name, ''), a.account), "
                            "CASE WHEN a.account = " + currentOwnerSql + " THEN 'owner' ELSE COALESCE(NULLIF(m.role, ''), 'member') END, "
                            "COALESCE(m.joined_at, CURRENT_TIMESTAMP), CURRENT_TIMESTAMP "
                            "FROM accounts a "
                            "LEFT JOIN server_group_members m ON m.group_id = 'public' AND m.user_id = a.account "
                            "WHERE COALESCE(a.account_status, 'active') = 'active' "
                            "AND NOT EXISTS (SELECT 1 FROM server_group_removed_members r WHERE r.group_id = 'public' AND r.user_id = a.account)");
                    }
                    if (!ok) {
                        qWarning() << "Failed to backfill public group members:" << backfillMembersQuery.lastError().text();
                    }
                }
                if (ok) {
                    QSqlQuery updateRoleQuery(db);
                    updateRoleQuery.prepare("UPDATE server_group_members "
                                            "SET role = CASE WHEN user_id = ? THEN 'owner' "
                                            "WHEN role = 'owner' THEN 'member' ELSE role END, "
                                            "updated_at = CURRENT_TIMESTAMP "
                                            "WHERE group_id = 'public'");
                    updateRoleQuery.addBindValue(ownerId);
                    ok = updateRoleQuery.exec();
                }

                if (ok) {
                    ok = db.commit();
                } else {
                    db.rollback();
                }
            } else {
                qWarning() << "Failed to start public group membership transaction:" << db.lastError().text();
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::ensurePublicGroupMembership(const QString& userId, QTcpSocket* socket) const {
    const QString normalizedUserId = userId.trimmed();
    if (normalizedUserId.isEmpty()) return false;

    ChatUser user;
    if (socket && m_clients.contains(socket)) {
        const ChatUser socketUser = m_clients.value(socket);
        if (socketUser.id == normalizedUserId) {
            user = socketUser;
        }
    }
    if (user.id.isEmpty()) {
        for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it) {
            if (it.value().id == normalizedUserId) {
                user = it.value();
                break;
            }
        }
    }
    if (user.id.isEmpty()) {
        user.id = normalizedUserId;
        user.name = normalizedUserId;
    }

    if (isServerGroupRemovedMember(QStringLiteral("public"), normalizedUserId)) {
        return false;
    }

    const bool recorded = recordDefaultGroupMembership(user);
    const bool member = isServerGroupMember(QStringLiteral("public"), normalizedUserId);
    if (!member) {
        qWarning() << "Public group membership repair failed" << normalizedUserId << "recorded" << recorded;
    }
    return member;
}

bool Server::isServerGroupMember(const QString& groupId, const QString& userId) const {
    if (groupId.isEmpty() || userId.isEmpty() || !ensureAccountDatabase()) return false;

    const QString connectionName = "server_group_membership_check_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(groupId + "|" + userId));
    bool exists = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("SELECT COUNT(*) FROM server_group_members WHERE group_id = ? AND user_id = ?");
            query.addBindValue(groupId);
            query.addBindValue(userId);
            if (query.exec() && query.next()) {
                exists = query.value(0).toInt() > 0;
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return exists;
}

bool Server::isServerGroupMemberMuted(const QString& groupId, const QString& userId, qint64* mutedUntil) const {
    if (mutedUntil) *mutedUntil = 0;
    if (groupId.isEmpty() || userId.isEmpty() || !ensureAccountDatabase()) return false;

    const QString connectionName = "server_group_mute_check_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(groupId + "|" + userId));
    bool muted = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery cleanup(db);
            cleanup.prepare("DELETE FROM server_group_member_mutes WHERE muted_until <= ?");
            cleanup.addBindValue(now);
            cleanup.exec();

            QSqlQuery query(db);
            query.prepare("SELECT muted_until FROM server_group_member_mutes WHERE group_id = ? AND user_id = ? AND muted_until > ?");
            query.addBindValue(groupId);
            query.addBindValue(userId);
            query.addBindValue(now);
            if (query.exec() && query.next()) {
                const qint64 until = query.value(0).toLongLong();
                muted = true;
                if (mutedUntil) *mutedUntil = until;
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return muted;
}

bool Server::isServerGroupRemovedMember(const QString& groupId, const QString& userId) const {
    if (groupId.isEmpty() || userId.isEmpty() || !ensureAccountDatabase()) return false;

    const QString connectionName = "server_group_removed_membership_check_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(groupId + "|" + userId));
    bool exists = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("SELECT COUNT(*) FROM server_group_removed_members r "
                          "LEFT JOIN server_group_members m ON m.group_id = r.group_id AND m.user_id = r.user_id "
                          "WHERE r.group_id = ? AND r.user_id = ? AND m.user_id IS NULL");
            query.addBindValue(groupId);
            query.addBindValue(userId);
            if (query.exec() && query.next()) {
                exists = query.value(0).toInt() > 0;
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return exists;
}

QStringList Server::serverGroupMemberIds(const QString& groupId) const {
    QStringList memberIds;
    if (groupId.trimmed().isEmpty() || !ensureAccountDatabase()) return memberIds;

    const QString connectionName = "server_group_member_ids_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(groupId));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("SELECT user_id FROM server_group_members WHERE group_id = ?");
            query.addBindValue(groupId.trimmed());
            if (query.exec()) {
                while (query.next()) {
                    const QString memberId = query.value(0).toString().trimmed();
                    if (!memberId.isEmpty() && !memberIds.contains(memberId)) {
                        memberIds << memberId;
                    }
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return memberIds;
}

bool Server::recordServerGroupAuditEvent(const QString& groupId,
                                         const QString& action,
                                         const QString& actorId,
                                         const QString& actorName,
                                         const QString& targetUserId,
                                         const QString& targetUserName,
                                         const QJsonObject& details) const {
    if (groupId.trimmed().isEmpty()
        || action.trimmed().isEmpty()
        || actorId.trimmed().isEmpty()
        || !ensureAccountDatabase()) {
        return false;
    }

    QString connectionName = "server_group_audit_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO server_group_audit_events("
                          "group_id, action, actor_id, actor_name, target_user_id, target_user_name, details, created_at) "
                          "VALUES(?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)");
            query.addBindValue(groupId.trimmed());
            query.addBindValue(action.trimmed().toLower());
            query.addBindValue(actorId.trimmed());
            query.addBindValue(actorName.trimmed());
            query.addBindValue(targetUserId.trimmed());
            query.addBindValue(targetUserName.trimmed());
            query.addBindValue(QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact)));
            ok = query.exec();
            if (!ok) {
                qWarning() << "Failed to record server group audit event:"
                           << query.lastError().text()
                           << groupId.trimmed()
                           << action.trimmed().toLower()
                           << actorId.trimmed();
            }
            db.close();
        } else {
            qWarning() << "Failed to open server group audit database:"
                       << db.lastError().text()
                       << groupId.trimmed()
                       << action.trimmed().toLower()
                       << actorId.trimmed();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::saveMessageToSqlite(const Message& msg, const QString& deliveryState) const {
    if (!ensureAccountDatabase()) return false;

    QString connectionName = "messages_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO messages(message_type, sender_id, sender_name, receiver_id, content, file_name, file_size, file_hash, file_chunk_size, file_chunk_count, delivery_state, created_at) "
                          "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
            query.addBindValue(static_cast<int>(msg.type));
            query.addBindValue(msg.senderId);
            query.addBindValue(msg.senderName);
            query.addBindValue(msg.receiverId);
            query.addBindValue(msg.content);
            query.addBindValue(msg.fileName);
            query.addBindValue(msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
            query.addBindValue(msg.fileHash.trimmed());
            query.addBindValue(msg.chunkSize);
            query.addBindValue(msg.chunkCount);
            query.addBindValue(deliveryState);
            query.addBindValue(msg.timestamp.toUTC().toString(Qt::ISODate));
            ok = query.exec();
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::saveFriendEventToSqlite(const QString& eventType,
                                     const QString& senderId,
                                     const QString& senderName,
                                     const QString& receiverId,
                                     const QString& queryAccount,
                                     const QString& eventState,
                                     bool accepted) const {
    if (!ensureAccountDatabase()) return false;

    QString connectionName = "friend_events_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO friend_events(event_type, sender_id, sender_name, receiver_id, query_account, accepted, event_state, created_at) "
                          "VALUES(?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)");
            query.addBindValue(eventType);
            query.addBindValue(senderId);
            query.addBindValue(senderName);
            query.addBindValue(receiverId);
            query.addBindValue(queryAccount);
            query.addBindValue(accepted ? 1 : 0);
            query.addBindValue(eventState);
            ok = query.exec();
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::saveAcceptedFriendshipToSqlite(const QString& userId,
                                        const QString& userName,
                                        const QString& friendId,
                                        const QString& friendName) const {
    const QString normalizedUserId = userId.trimmed();
    const QString normalizedFriendId = friendId.trimmed();
    if (normalizedUserId.isEmpty() || normalizedFriendId.isEmpty() || normalizedUserId == normalizedFriendId || !ensureAccountDatabase()) {
        return false;
    }

    const QString normalizedUserName = userName.trimmed().isEmpty() ? normalizedUserId : userName.trimmed();
    const QString normalizedFriendName = friendName.trimmed().isEmpty() ? normalizedFriendId : friendName.trimmed();
    const QString connectionName = "server_friends_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName) && db.transaction()) {
            auto upsertFriend = [&db](const QString& ownerId, const QString& peerId, const QString& peerName) {
                QSqlQuery query(db);
                query.prepare(insertReplaceSql(
                    QStringLiteral("server_friends"),
                    {QStringLiteral("user_id"), QStringLiteral("friend_id"), QStringLiteral("friend_name"), QStringLiteral("updated_at")},
                    {QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("CURRENT_TIMESTAMP")},
                    {QStringLiteral("user_id"), QStringLiteral("friend_id")},
                    {QStringLiteral("friend_name = EXCLUDED.friend_name"), QStringLiteral("updated_at = EXCLUDED.updated_at")}));
                query.addBindValue(ownerId);
                query.addBindValue(peerId);
                query.addBindValue(peerName);
                return query.exec();
            };

            ok = upsertFriend(normalizedUserId, normalizedFriendId, normalizedFriendName)
                && upsertFriend(normalizedFriendId, normalizedUserId, normalizedUserName);
            ok = ok ? db.commit() : false;
            if (!ok) {
                db.rollback();
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

QVector<ChatUser> Server::loadFriendListFromSqlite(const QString& userId) const {
    QVector<ChatUser> friends;
    const QString normalizedUserId = userId.trimmed();
    if (normalizedUserId.isEmpty() || !ensureAccountDatabase()) return friends;

    const QString connectionName = "server_friends_read_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(normalizedUserId));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("SELECT f.friend_id, "
                          "COALESCE(NULLIF(a.user_name, ''), NULLIF(f.friend_name, ''), f.friend_id), "
                          "COALESCE(a.avatar, ''), "
                          "COALESCE(f.updated_at, f.created_at, '') "
                          "FROM server_friends f "
                          "LEFT JOIN accounts a ON a.account = f.friend_id "
                          "WHERE f.user_id = ? "
                          "ORDER BY LOWER(COALESCE(NULLIF(a.user_name, ''), NULLIF(f.friend_name, ''), f.friend_id)), f.friend_id");
            query.addBindValue(normalizedUserId);
            if (query.exec()) {
                while (query.next()) {
                    ChatUser user;
                    user.id = query.value(0).toString().trimmed();
                    if (user.id.isEmpty()) {
                        continue;
                    }
                    user.name = query.value(1).toString().trimmed();
                    if (user.name.isEmpty()) {
                        user.name = user.id;
                    }
                    user.avatar = query.value(2).toString();
                    const QString updatedAt = query.value(3).toString().trimmed();
                    user.lastActive = QDateTime::fromString(updatedAt, Qt::ISODate);
                    if (!user.lastActive.isValid()) {
                        user.lastActive = QDateTime::fromString(updatedAt, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                    }
                    friends.append(user);
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return friends;
}

bool Server::shouldPublishLargeFileOffer(const Message& msg) const {
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return false;
    if (!m_redisService->isEnabled()) return false;
    if (!isRedisUserOnline(msg.receiverId)) return false;
    if (msg.receiverId.isEmpty() || msg.fileData.isEmpty()) return false;
    if (msg.type != MessageType::File && msg.type != MessageType::Image) return false;
    QTcpSocket* localSocket = m_userSockets.value(msg.receiverId, nullptr);
    if (localSocket && localSocket->state() == QAbstractSocket::ConnectedState) return false;

    if (!createConfiguredObjectStore()) return false;
    if (msg.fileSize <= 0 || msg.chunkSize <= 0 || msg.chunkCount <= 0) return false;
    if (msg.chunkCount != (msg.fileSize + msg.chunkSize - 1) / msg.chunkSize) return false;
    return looksLikeSha256Hex(msg.fileHash);
}

bool Server::shouldPublishServerGroupLargeFileOffer(const Message& msg, const QStringList& memberIds) const {
    if (!envEnabled("QTNETWORKCHAT_LARGE_FILE_ROUTING")) return false;
    if (!m_redisService->isEnabled()) return false;
    if (msg.receiverId.isEmpty() || msg.receiverId == QLatin1String("public")) return false;
    if (msg.fileData.isEmpty() || msg.fileData.size() <= kRedisPubSubFileMaxBytes) return false;
    if (msg.type != MessageType::File && msg.type != MessageType::Image) return false;
    if (msg.fileSize <= 0 || msg.chunkSize <= 0 || msg.chunkCount <= 0) return false;
    if (msg.chunkCount != (msg.fileSize + msg.chunkSize - 1) / msg.chunkSize) return false;
    if (!looksLikeSha256Hex(msg.fileHash)) return false;
    if (!createConfiguredObjectStore()) return false;

    for (const QString& memberId : memberIds) {
        if (memberId.isEmpty() || memberId == msg.senderId) continue;
        QTcpSocket* localSocket = m_userSockets.value(memberId, nullptr);
        if (localSocket && localSocket->state() == QAbstractSocket::ConnectedState) continue;
        bool redisOnline = false;
        if (isRedisUserOnline(memberId, &redisOnline) && redisOnline) {
            return true;
        }
    }
    return false;
}

bool Server::publishRedisServerGroupLargeFileOffer(const Message& msg, const QStringList& memberIds) const {
    if (!shouldPublishServerGroupLargeFileOffer(msg, memberIds)) return false;

    QString objectStoreError;
    std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore(&objectStoreError);
    QString objectKey;
    QString objectHash;
    QString objectError;
    const QString extension = QFileInfo(msg.fileName).suffix();
    if (!objectStore
        || !objectStore->writeObject(msg.fileData, &objectKey, &objectHash, &objectError, extension)
        || objectHash.compare(msg.fileHash.trimmed(), Qt::CaseInsensitive) != 0) {
        if (objectStore && !objectKey.isEmpty()) {
            objectStore->removeObject(objectKey);
        }
        QJsonObject logMeta;
        logMeta["deliveryState"] = QStringLiteral("server-group-file");
        logMeta["groupId"] = msg.receiverId;
        logMeta["receiverId"] = msg.receiverId;
        logMeta["transferId"] = msg.transferId;
        logMeta["fileName"] = msg.fileName;
        logMeta["messageType"] = msg.type == MessageType::Image ? QStringLiteral("Image") : QStringLiteral("File");
        if (!objectKey.isEmpty()) {
            logMeta["objectKey"] = objectKey;
        }
        const QString writeFailureReason =
            objectStoreWriteFailureReasonForLog(objectStoreType(),
                                                static_cast<bool>(objectStore),
                                                objectError.isEmpty() ? objectStoreError : objectError,
                                                msg.fileHash,
                                                objectHash);
        logRedisLargeFileRouteEvent(QStringLiteral("object_write"),
                                    QStringLiteral("skipped"),
                                    largeFileRouteLogMetadata(logMeta, objectStoreType(), QStringLiteral("write")),
                                    writeFailureReason,
                                    msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
        qWarning() << "Private group large file object routing skipped because object write/validation failed"
                   << msg.receiverId << msg.fileName << writeFailureReason;
        return false;
    }

    QJsonObject obj;
    obj["type"] = QStringLiteral("file");
    obj["messageType"] = static_cast<int>(msg.type);
    obj["senderId"] = msg.senderId;
    obj["senderName"] = msg.senderName;
    obj["senderAvatar"] = msg.senderAvatar;
    obj["receiverId"] = msg.receiverId;
    obj["groupId"] = msg.receiverId;
    obj["deliveryState"] = QStringLiteral("server-group-file");
    obj["content"] = msg.content;
    obj["fileName"] = msg.fileName;
    obj["transferId"] = msg.transferId;
    obj["fileSize"] = QString::number(msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
    obj["fileHash"] = msg.fileHash;
    obj["chunkSize"] = QString::number(msg.chunkSize);
    obj["chunkCount"] = QString::number(msg.chunkCount);
    obj["objectStoreKey"] = objectKey;
    obj["objectStoreHash"] = objectHash;
    obj["objectStoreCreatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    obj["objectStoreExpiresAt"] = QDateTime::currentDateTimeUtc().addMSecs(objectStoreTtlMs()).toString(Qt::ISODate);
    appendE2EFields(&obj, msg);

    const bool published = publishRedisLargeFileOffer(obj);
    if (!published && objectStore) {
        const bool removedObject = objectStore->removeObject(objectKey);
        QJsonObject logMeta = obj;
        logRedisLargeFileRouteEvent(QStringLiteral("object_delete"),
                                    removedObject ? QStringLiteral("deleted") : QStringLiteral("retained"),
                                    largeFileRouteLogMetadata(logMeta, objectStoreType(), QStringLiteral("delete")),
                                    removedObject ? QStringLiteral("publish-failed")
                                                  : objectStoreRemoveFailureReasonForLog(objectStoreType(), objectStore->lastRemoveFailureReason()),
                                    msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
    }
    return published;
}

QString Server::objectStoreType() const {
    return normalizeObjectStoreType(QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_STORE")));
}

QString Server::objectStoreRootDir() const {
    const QString root = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_OBJECT_ROOT")).trimmed();
    return root.isEmpty() ? QString() : QDir::cleanPath(root);
}

std::unique_ptr<ObjectStore> Server::createConfiguredObjectStore(QString* error) const {
    if (m_objectStoreFactoryForTesting) {
        return m_objectStoreFactoryForTesting(error);
    }
    return createObjectStore(objectStoreType(), objectStoreRootDir(), error);
}

qint64 Server::objectStoreTtlMs() const {
    const qint64 hours = positiveIntegerEnvOrDefault("QTNETWORKCHAT_OBJECT_TTL_HOURS",
                                                     kDefaultObjectStoreTtlHours,
                                                     kMaxObjectStoreTtlHours);
    return hours * 60LL * 60 * 1000;
}

LargeFileDeliveredReceiptDecision Server::evaluateRedisLargeFileDeliveredReceipt(const QJsonObject& event) const {
    LargeFileDeliveredReceipt receipt;
    receipt.sourceInstanceId = event["sourceInstanceId"].toString();
    receipt.transferId = event["transferId"].toString().trimmed();
    receipt.receiverId = event["receiverId"].toString().trimmed();
    receipt.objectKey = event["objectKey"].toString().trimmed();
    receipt.fileHash = event["fileHash"].toString().trimmed();
    receipt.confirmedBytes = event["confirmedBytes"].toVariant().toLongLong();

    LargeFileDeliveredReceiptDecision invalidDecision =
        evaluateLargeFileDeliveredReceiptCleanup(receipt, LargeFileDeliveredFallback());
    if (invalidDecision.reason == QStringLiteral("invalid-receipt")) {
        return invalidDecision;
    }

    LargeFileDeliveredReceiptDecision decision;
    decision.shouldCleanup = false;
    decision.reason = QStringLiteral("receipt-not-matched");

    const auto makeFallback = [this](const QJsonObject& obj) {
        LargeFileDeliveredFallback fallback;
        fallback.sourceInstanceId = m_instanceId;
        fallback.transferId = obj["transferId"].toString();
        fallback.receiverId = obj["receiverId"].toString();
        fallback.objectKey = obj["objectStoreKey"].toString();
        fallback.fileHash = obj["fileHash"].toString();
        fallback.fileSize = obj["fileSize"].toVariant().toLongLong();
        return fallback;
    };
    const auto isSameRoute = [&receipt](const LargeFileDeliveredFallback& fallback) {
        return receipt.transferId == fallback.transferId.trimmed()
            && receipt.receiverId == fallback.receiverId.trimmed()
            && receipt.objectKey == fallback.objectKey.trimmed();
    };
    const auto considerPayload = [&](const QJsonObject& obj) {
        const LargeFileDeliveredFallback fallback = makeFallback(obj);
        if (!isSameRoute(fallback)) {
            return false;
        }
        decision = evaluateLargeFileDeliveredReceiptCleanup(receipt, fallback);
        return decision.shouldCleanup;
    };

    if (ensureAccountDatabase()) {
        const QString connectionName = "offline_reconcile_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = openAccountDatabase(connectionName);
            if (openAccountDatabaseConnection(db, connectionName)) {
                QSqlQuery query(db);
                query.prepare("SELECT payload FROM offline_messages WHERE receiver_id = ? ORDER BY id ASC");
                query.addBindValue(receipt.receiverId);
                if (query.exec()) {
                    while (query.next()) {
                        const QJsonDocument doc = QJsonDocument::fromJson(query.value(0).toString().toUtf8());
                        if (doc.isObject() && considerPayload(doc.object())) {
                            break;
                        }
                    }
                }
                db.close();
            }
        }
        releaseAccountDatabase(connectionName);
    }

    if (decision.shouldCleanup) {
        return decision;
    }

    QFile jsonlFile(offlineFilePath(receipt.receiverId));
    if (jsonlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!jsonlFile.atEnd()) {
            const QByteArray line = jsonlFile.readLine().trimmed();
            if (line.isEmpty()) continue;
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (doc.isObject() && considerPayload(doc.object())) {
                break;
            }
        }
        jsonlFile.close();
    }

    return decision;
}

void Server::persistRedisLargeFileDeliveredReceiptSummary(const QJsonObject& event,
                                                          const LargeFileDeliveredReceiptDecision& decision,
                                                          bool cleanupSucceeded) const {
    const QString configuredDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_DELIVERED_RECEIPT_DIR")).trimmed();
    if (configuredDir.isEmpty()) {
        return;
    }

    const QString sourceInstanceId = event["sourceInstanceId"].toString().trimmed();
    const QString transferId = event["transferId"].toString().trimmed();
    const QString receiverId = event["receiverId"].toString().trimmed();
    const QString objectKey = event["objectKey"].toString().trimmed();
    const QString fileHash = event["fileHash"].toString().trimmed().toLower();
    const qint64 confirmedBytes = event["confirmedBytes"].toVariant().toLongLong();
    LargeFileDeliveredReceipt receipt;
    receipt.sourceInstanceId = sourceInstanceId;
    receipt.transferId = transferId;
    receipt.receiverId = receiverId;
    receipt.objectKey = objectKey;
    receipt.fileHash = fileHash;
    receipt.confirmedBytes = confirmedBytes;
    if (evaluateLargeFileDeliveredReceiptCleanup(receipt, LargeFileDeliveredFallback()).reason
            == QStringLiteral("invalid-receipt")) {
        return;
    }

    QDir dir(QDir::cleanPath(configuredDir));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        qWarning() << "Failed to create delivered receipt summary directory";
        return;
    }

    QJsonObject row;
    row["sourceInstanceId"] = sourceInstanceId;
    row["transferId"] = transferId;
    row["receiverId"] = receiverId;
    row["objectKey"] = objectKey;
    row["fileHash"] = fileHash;
    row["confirmedBytes"] = QString::number(confirmedBytes);
    row["result"] = decision.shouldCleanup ? QStringLiteral("cleaned") : QStringLiteral("retained");
    row["reason"] = decision.reason;
    row["cleanupResult"] = cleanupSucceeded ? QStringLiteral("cleaned") : QStringLiteral("retained");
    row["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QFile file(dir.filePath(QStringLiteral("delivered-receipts.jsonl")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Failed to open delivered receipt summary file";
        return;
    }
    file.write(QJsonDocument(row).toJson(QJsonDocument::Compact));
    file.write("\n");
}

Server::LargeFileCleanupResult Server::cleanupDeliveredRedisLargeFile(const QJsonObject& event) const {
    LargeFileCleanupResult result;
    const QString objectKey = event["objectKey"].toString().trimmed();
    const QString transferId = event["transferId"].toString().trimmed();
    const QString receiverId = event["receiverId"].toString().trimmed();
    const QString fileHash = event["fileHash"].toString().trimmed();
    const qint64 confirmedBytes = event["confirmedBytes"].toVariant().toLongLong();
    LargeFileDeliveredReceipt receipt;
    receipt.sourceInstanceId = event["sourceInstanceId"].toString();
    receipt.transferId = transferId;
    receipt.receiverId = receiverId;
    receipt.objectKey = objectKey;
    receipt.fileHash = fileHash;
    receipt.confirmedBytes = confirmedBytes;
    if (evaluateLargeFileDeliveredReceiptCleanup(receipt, LargeFileDeliveredFallback()).reason
            == QStringLiteral("invalid-receipt")) {
        return result;
    }

    auto matchesPayload = [&](const QJsonObject& obj) {
        LargeFileDeliveredFallback fallback;
        fallback.sourceInstanceId = m_instanceId;
        fallback.transferId = obj["transferId"].toString();
        fallback.receiverId = obj["receiverId"].toString();
        fallback.objectKey = obj["objectStoreKey"].toString();
        fallback.fileHash = obj["fileHash"].toString();
        fallback.fileSize = obj["fileSize"].toVariant().toLongLong();
        return evaluateLargeFileDeliveredReceiptCleanup(receipt, fallback).shouldCleanup;
    };

    QStringList offlineAttachmentPaths;
    bool removedQueue = false;
    if (ensureAccountDatabase()) {
        const QString connectionName = "offline_delivered_" + QString::number(reinterpret_cast<quintptr>(this));
        QVector<qint64> deliveredIds;
        {
            QSqlDatabase db = openAccountDatabase(connectionName);
            if (openAccountDatabaseConnection(db, connectionName)) {
                QSqlQuery query(db);
                query.prepare("SELECT id, payload FROM offline_messages WHERE receiver_id = ? ORDER BY id ASC");
                query.addBindValue(receiverId);
                if (query.exec()) {
                    while (query.next()) {
                        const QJsonDocument doc = QJsonDocument::fromJson(query.value(1).toString().toUtf8());
                        if (!doc.isObject()) continue;
                        const QJsonObject obj = doc.object();
                        if (!matchesPayload(obj)) continue;
                        deliveredIds.append(query.value(0).toLongLong());
                        const QString offlinePath = obj["offlineFilePath"].toString();
                        if (!offlinePath.isEmpty()) {
                            offlineAttachmentPaths.append(QDir::cleanPath(offlinePath));
                        }
                    }
                }
                for (qint64 messageId : deliveredIds) {
                    QSqlQuery deleteQuery(db);
                    deleteQuery.prepare("DELETE FROM offline_messages WHERE id = ?");
                    deleteQuery.addBindValue(messageId);
                    if (deleteQuery.exec()) {
                        removedQueue = true;
                    }
                }
                db.close();
            }
        }
        releaseAccountDatabase(connectionName);
    }

    QFile jsonlFile(offlineFilePath(receiverId));
    if (jsonlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QVector<QByteArray> remainingLines;
        while (!jsonlFile.atEnd()) {
            const QByteArray line = jsonlFile.readLine().trimmed();
            if (line.isEmpty()) continue;
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (doc.isObject() && matchesPayload(doc.object())) {
                const QString offlinePath = doc.object()["offlineFilePath"].toString();
                if (!offlinePath.isEmpty()) {
                    offlineAttachmentPaths.append(QDir::cleanPath(offlinePath));
                }
                removedQueue = true;
                continue;
            }
            remainingLines.append(line);
        }
        jsonlFile.close();

        if (removedQueue) {
            if (remainingLines.isEmpty()) {
                jsonlFile.remove();
            } else if (jsonlFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                for (const QByteArray& line : remainingLines) {
                    jsonlFile.write(line);
                    jsonlFile.write("\n");
                }
                jsonlFile.close();
            }
        }
    }

    if (!removedQueue) {
        return result;
    }
    result.queueCleaned = true;

    offlineAttachmentPaths.removeDuplicates();
    for (const QString& path : offlineAttachmentPaths) {
        if (!path.isEmpty()) {
            QFile::remove(path);
        }
    }

    std::unique_ptr<ObjectStore> objectStore = createConfiguredObjectStore();
    if (objectStore) {
        result.objectDeleteAttempted = true;
        result.objectDeleted = objectStore->removeObject(objectKey);
        if (result.objectDeleted) {
            result.objectDeleteReason = QStringLiteral("success");
        } else {
            result.objectDeleteReason =
                objectStoreRemoveFailureReasonForLog(objectStoreType(), objectStore->lastRemoveFailureReason());
        }
    }

    return result;
}

void Server::broadcastMessage(const Message& msg, QTcpSocket* excludeSocket) {
    QJsonObject obj;
    obj["type"] = msg.type == MessageType::System ? "system" : ((msg.type == MessageType::File || msg.type == MessageType::Image) ? "file" : (msg.isPrivate() ? "private" : "message"));
    obj["messageType"] = static_cast<int>(msg.type);
    obj["senderId"] = msg.senderId;
    obj["senderName"] = msg.senderName;
    obj["senderAvatar"] = msg.senderAvatar;
    obj["receiverId"] = msg.receiverId;
    obj["content"] = msg.content;
    obj["fileName"] = msg.fileName;
    obj["fileSize"] = QString::number(msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
    obj["fileHash"] = msg.fileHash;
    obj["chunkSize"] = QString::number(msg.chunkSize);
    obj["chunkCount"] = QString::number(msg.chunkCount);
    if (!msg.fileData.isEmpty() && msg.type != MessageType::File && msg.type != MessageType::Image) {
        obj["fileData"] = QString::fromLatin1(msg.fileData.toBase64());
    }
    appendE2EFields(&obj, msg);
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        QTcpSocket* socket = it.key();
        if (socket != excludeSocket && socket->state() == QAbstractSocket::ConnectedState) {
            if ((msg.type == MessageType::File || msg.type == MessageType::Image) && !msg.fileData.isEmpty()) {
                sendChunkedFileToSocket(msg, socket);
            } else {
                socket->write(data);
                socket->write("\n");
                socket->flush();
            }
        }
    }
}

void Server::sendToUser(const Message& msg) {
    QString receiverId = msg.receiverId;
    if (receiverId.isEmpty()) return;

    QTcpSocket* targetSocket = m_userSockets.value(receiverId);
    if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
        if ((msg.type == MessageType::File || msg.type == MessageType::Image) && !msg.fileData.isEmpty()) {
            if (!sendChunkedFileToSocket(msg, targetSocket)) {
                saveOfflineMessage(msg);
            }
            return;
        }

        QJsonObject obj;
        obj["type"] = msg.type == MessageType::File || msg.type == MessageType::Image ? "file" : "private";
        obj["messageType"] = static_cast<int>(msg.type);
        obj["senderId"] = msg.senderId;
        obj["senderName"] = msg.senderName;
        obj["senderAvatar"] = msg.senderAvatar;
        obj["receiverId"] = msg.receiverId;
        obj["content"] = msg.content;
        obj["fileName"] = msg.fileName;
        obj["fileSize"] = QString::number(msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
        obj["fileHash"] = msg.fileHash;
        obj["chunkSize"] = QString::number(msg.chunkSize);
        obj["chunkCount"] = QString::number(msg.chunkCount);
        if (!msg.fileData.isEmpty()) {
            obj["fileData"] = QString::fromLatin1(msg.fileData.toBase64());
        }
        appendE2EFields(&obj, msg);
        QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        targetSocket->write(data);
        targetSocket->write("\n");
        targetSocket->flush();
    } else {
        saveOfflineMessage(msg);
    }
}

void Server::sendUserList(QTcpSocket* socket) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QJsonObject obj;
    obj["type"] = "userlist";

    QSet<QString> onlineIds;
    QMap<QString, QString> onlineNames;
    QJsonArray users;
    if (m_redisService->isEnabled()) {
        QList<RedisClient::Presence> redisUsers;
        if (m_redisService->fetchOnlinePresence(&redisUsers)) {
            for (const RedisClient::Presence& user : redisUsers) {
                const QString userId = user.userId.trimmed();
                if (userId.isEmpty()) {
                    continue;
                }
                onlineIds.insert(userId);
                if (!user.userName.trimmed().isEmpty()) {
                    onlineNames.insert(userId, user.userName.trimmed());
                }
            }
        }
    }

    const QJsonObject accounts = loadAccountsFromSqlite();
    for (auto it = accounts.constBegin(); it != accounts.constEnd(); ++it) {
        const QString userId = it.key().trimmed();
        if (userId.isEmpty()) {
            continue;
        }
        const QJsonObject account = it.value().toObject();
        const QString persistedName = account.value("userName").toString().trimmed();
        QJsonObject u;
        u["id"] = userId;
        u["name"] = onlineNames.value(userId, persistedName.isEmpty() ? userId : persistedName);
        u["avatar"] = account.value("avatar").toString();
        u["online"] = onlineIds.contains(userId);
        users.append(u);
    }

    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        const ChatUser& user = it.value();
        if (user.id.trimmed().isEmpty()) {
            continue;
        }

        bool merged = false;
        for (int i = 0; i < users.size(); ++i) {
            QJsonObject existing = users.at(i).toObject();
            if (existing.value("id").toString() != user.id) {
                continue;
            }
            existing["name"] = user.name.isEmpty() ? user.id : user.name;
            existing["avatar"] = user.avatar;
            existing["online"] = true;
            users.replace(i, existing);
            merged = true;
            break;
        }
        if (merged) {
            continue;
        }

        QJsonObject u;
        u["id"] = user.id;
        u["name"] = user.name.isEmpty() ? user.id : user.name;
        u["avatar"] = user.avatar;
        u["online"] = true;
        users.append(u);
    }
    obj["users"] = users;

    socket->write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}


void Server::sendServerGroupMemberUpdated(QTcpSocket* socket, const QString& groupId, const QString& memberId, const QString& action) const {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

    QJsonObject obj;
    obj["type"] = "server_group_member_updated";
    obj["groupId"] = groupId;
    obj["memberId"] = memberId;
    obj["action"] = action;
    socket->write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

void Server::sendFavoriteMessagesSnapshot(const QString& userId, QTcpSocket* socket) const {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState || userId.trimmed().isEmpty() || !ensureAccountDatabase()) return;

    QJsonArray favorites;
    const QString connectionName = "server_message_favorites_snapshot_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(userId));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("SELECT session_id, message_id, COALESCE(message_json, ''), updated_at "
                          "FROM server_message_favorites "
                          "WHERE user_id = ? "
                          "ORDER BY updated_at DESC LIMIT 500");
            query.addBindValue(userId.trimmed());
            if (query.exec()) {
                while (query.next()) {
                    const QString sessionId = query.value(0).toString();
                    const QString messageId = query.value(1).toString();
                    const QJsonDocument doc = QJsonDocument::fromJson(query.value(2).toString().toUtf8());
                    QJsonObject item = doc.isObject() ? doc.object() : QJsonObject();
                    item["sessionId"] = sessionId;
                    item["messageId"] = messageId;
                    if (!item.contains("id") || item.value("id").toString().trimmed().isEmpty()) item["id"] = messageId;
                    if (!item.contains("senderId")) item["senderId"] = QString();
                    if (!item.contains("senderName")) item["senderName"] = QString();
                    if (!item.contains("content")) item["content"] = QString();
                    if (!item.contains("type")) item["type"] = QStringLiteral("text");
                    if (!item.contains("timestamp")) item["timestamp"] = QString::number(QDateTime::currentMSecsSinceEpoch());
                    favorites.append(item);
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);

    QJsonObject event;
    event["type"] = "favorite_messages_snapshot";
    event["favorites"] = favorites;
    socket->write(QJsonDocument(event).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

void Server::sendServerGroupSnapshot(const QString& userId, QTcpSocket* socket) const {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState || userId.isEmpty() || !ensureAccountDatabase()) {
        return;
    }
    ensurePublicGroupMembership(userId, socket);

    QJsonArray groups;
    QJsonArray removedGroups;
    QString connectionName = "server_group_snapshot_" + QString::number(reinterpret_cast<quintptr>(this));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery groupQuery(db);
            groupQuery.prepare("SELECT g.group_id, g.group_name, COALESCE(g.announcement, ''), COALESCE(g.owner_id, ''), "
                               "COALESCE(g.group_type, ''), COALESCE(g.history_policy, ''), COALESCE(g.file_policy, ''), "
                               "COALESCE(g.avatar, ''), COALESCE(g.all_muted, 0), COALESCE(g.speaking_rule, 'unrestricted'), "
                                "COALESCE(g.join_policy, 'approval'), COALESCE(g.search_mode, 'id_and_keyword') "
                               "FROM server_groups g "
                               "JOIN server_group_members m ON m.group_id = g.group_id "
                               "WHERE m.user_id = ? "
                               "ORDER BY g.group_id ASC");
            groupQuery.addBindValue(userId);
            if (groupQuery.exec()) {
                while (groupQuery.next()) {
                    const QString groupId = groupQuery.value(0).toString();
                    QJsonObject groupObj;
                    groupObj["groupId"] = groupId;
                    groupObj["groupName"] = groupQuery.value(1).toString();
                    groupObj["announcement"] = groupQuery.value(2).toString();
                    groupObj["ownerId"] = groupQuery.value(3).toString();
                    const QString groupType = groupQuery.value(4).toString().trimmed().isEmpty()
                        ? serverGroupTypeFromId(groupId)
                        : groupQuery.value(4).toString();
                    const QString historyPolicy = groupQuery.value(5).toString().trimmed().isEmpty()
                        ? serverGroupHistoryPolicy(groupType)
                        : groupQuery.value(5).toString();
                    const QString filePolicy = groupQuery.value(6).toString().trimmed().isEmpty()
                        ? serverGroupFilePolicy(groupType)
                        : groupQuery.value(6).toString();
                    groupObj["groupType"] = groupType;
                    groupObj["membershipState"] = "active";
                    groupObj["historyPolicy"] = historyPolicy;
                    groupObj["filePolicy"] = filePolicy;
                    groupObj["avatar"] = groupQuery.value(7).toString();
                    groupObj["allMuted"] = groupQuery.value(8).toInt() != 0;
                    groupObj["speakingRule"] = groupQuery.value(9).toString();
                    groupObj["joinPolicy"] = groupQuery.value(10).toString();
                    groupObj["searchMode"] = groupQuery.value(11).toString();
                    groupObj["searchable"] = groupObj["searchMode"].toString() != QLatin1String("private");
                    groupObj["canSend"] = true;
                    groupObj["canSendFiles"] = true;
                    groupObj["canReadHistory"] = true;
                    groupObj["historyVisibility"] = groupType == QLatin1String("private")
                        ? QStringLiteral("active-members-and-removed-readonly")
                        : QStringLiteral("public-members-and-removed-readonly");
                    groupObj["historyReadOnly"] = false;
                    groupObj["historyRetainedAfterRemoval"] = historyPolicy.contains(QStringLiteral("removed-readonly"));

                    QJsonArray members;
                    QSqlQuery memberQuery(db);
                    memberQuery.prepare("SELECT m.user_id, COALESCE(NULLIF(m.user_name, ''), NULLIF(a.user_name, ''), m.user_id), "
                                        "COALESCE(NULLIF(m.role, ''), 'member'), COALESCE(a.avatar, '') "
                                        "FROM server_group_members m "
                                        "LEFT JOIN accounts a ON a.account = m.user_id "
                                        "WHERE m.group_id = ? "
                                        "ORDER BY CASE WHEN m.role = 'owner' THEN 0 WHEN m.role = 'admin' THEN 1 ELSE 2 END, "
                                        "m.joined_at ASC, m.user_id ASC");
                    memberQuery.addBindValue(groupId);
                    if (memberQuery.exec()) {
                        while (memberQuery.next()) {
                            QJsonObject memberObj;
                            const QString memberUserId = memberQuery.value(0).toString();
                            const QString memberUserName = memberQuery.value(1).toString();
                            memberObj["userId"] = memberUserId;
                            memberObj["userName"] = memberUserName;
                            memberObj["role"] = memberQuery.value(2).toString();
                            memberObj["avatar"] = memberQuery.value(3).toString();
                            memberObj["online"] = m_userSockets.contains(memberUserId);
                            memberObj["groupNickname"] = memberUserName;
                            qint64 mutedUntil = 0;
                            QSqlQuery muteQuery(db);
                            muteQuery.prepare("SELECT muted_until FROM server_group_member_mutes WHERE group_id = ? AND user_id = ? AND muted_until > ?");
                            muteQuery.addBindValue(groupId);
                            muteQuery.addBindValue(memberUserId);
                            muteQuery.addBindValue(QDateTime::currentMSecsSinceEpoch());
                            if (muteQuery.exec() && muteQuery.next()) {
                                mutedUntil = muteQuery.value(0).toLongLong();
                            }
                            if (mutedUntil > 0) {
                                memberObj["mutedUntil"] = QString::number(mutedUntil);
                            }
                            members.append(memberObj);
                        }
                    } else {
                        qWarning() << "Failed to load server group members for snapshot:" << memberQuery.lastError().text() << groupId;
                    }
                    groupObj["members"] = members;
                    groupObj["memberCount"] = members.size();

                    QSqlQuery userSettingsQuery(db);
                    userSettingsQuery.prepare("SELECT COALESCE(nickname, ''), COALESCE(remark, ''), "
                                              "COALESCE(mute_notifications, 0), COALESCE(receive_without_notify, 0), "
                                              "COALESCE(receive_mode, 'receive_quiet') "
                                              "FROM server_group_user_settings WHERE group_id = ? AND user_id = ?");
                    userSettingsQuery.addBindValue(groupId);
                    userSettingsQuery.addBindValue(userId);
                    QJsonObject userSettings;
                    if (userSettingsQuery.exec() && userSettingsQuery.next()) {
                        userSettings["nickname"] = userSettingsQuery.value(0).toString();
                        userSettings["remark"] = userSettingsQuery.value(1).toString();
                        userSettings["muteNotifications"] = userSettingsQuery.value(2).toInt() != 0;
                        userSettings["receiveWithoutNotify"] = userSettingsQuery.value(3).toInt() != 0;
                        userSettings["receiveMode"] = userSettingsQuery.value(4).toString();
                    }
                    groupObj["userSettings"] = userSettings;

                    QJsonArray essenceMessages;
                    QSqlQuery essenceQuery(db);
                    essenceQuery.prepare("SELECT message_id, COALESCE(set_by, ''), COALESCE(set_by_name, ''), created_at "
                                         "FROM server_group_essence_messages "
                                         "WHERE group_id = ? "
                                         "ORDER BY created_at DESC LIMIT 50");
                    essenceQuery.addBindValue(groupId);
                    if (essenceQuery.exec()) {
                        while (essenceQuery.next()) {
                            QJsonObject essenceObj;
                            essenceObj["messageId"] = essenceQuery.value(0).toString();
                            essenceObj["sessionId"] = groupId;
                            essenceObj["setBy"] = essenceQuery.value(1).toString();
                            essenceObj["senderName"] = essenceQuery.value(2).toString();
                            essenceObj["setAt"] = essenceQuery.value(3).toString();
                            essenceObj["enabled"] = true;
                            essenceMessages.append(essenceObj);
                        }
                    }
                    groupObj["essenceMessages"] = essenceMessages;

                    QJsonArray auditEvents;
                    QSqlQuery auditQuery(db);
                    auditQuery.prepare("SELECT action, actor_id, COALESCE(actor_name, ''), "
                                       "COALESCE(target_user_id, ''), COALESCE(target_user_name, ''), "
                                       "COALESCE(details, ''), created_at "
                                       "FROM server_group_audit_events "
                                       "WHERE group_id = ? "
                                       "ORDER BY id DESC LIMIT 20");
                    auditQuery.addBindValue(groupId);
                    if (auditQuery.exec()) {
                        while (auditQuery.next()) {
                            QJsonObject auditObj;
                            auditObj["action"] = auditQuery.value(0).toString();
                            auditObj["actorId"] = auditQuery.value(1).toString();
                            auditObj["actorName"] = auditQuery.value(2).toString();
                            auditObj["targetUserId"] = auditQuery.value(3).toString();
                            auditObj["targetUserName"] = auditQuery.value(4).toString();
                            const QJsonDocument detailsDoc = QJsonDocument::fromJson(auditQuery.value(5).toString().toUtf8());
                            auditObj["details"] = detailsDoc.isObject() ? detailsDoc.object() : QJsonObject();
                            auditObj["createdAt"] = auditQuery.value(6).toString();
                            auditEvents.prepend(auditObj);
                        }
                    }
                    groupObj["auditEvents"] = auditEvents;
                    groups.append(groupObj);
                }
            }

            QSqlQuery removedQuery(db);
            removedQuery.prepare("SELECT g.group_id, g.group_name, COALESCE(g.announcement, ''), "
                                 "COALESCE(g.owner_id, ''), COALESCE(g.group_type, ''), "
                                 "COALESCE(g.history_policy, ''), COALESCE(g.file_policy, ''), "
                                 "COALESCE(r.removed_by, ''), "
                                 "COALESCE(r.removed_by_name, ''), r.removed_at "
                                 "FROM server_group_removed_members r "
                                 "JOIN server_groups g ON g.group_id = r.group_id "
                                 "LEFT JOIN server_group_members m ON m.group_id = r.group_id AND m.user_id = r.user_id "
                                 "WHERE r.user_id = ? AND m.user_id IS NULL "
                                 "ORDER BY r.removed_at DESC, g.group_id ASC");
            removedQuery.addBindValue(userId);
            if (removedQuery.exec()) {
                while (removedQuery.next()) {
                    QJsonObject groupObj;
                    groupObj["groupId"] = removedQuery.value(0).toString();
                    groupObj["groupName"] = removedQuery.value(1).toString();
                    groupObj["announcement"] = removedQuery.value(2).toString();
                    groupObj["ownerId"] = removedQuery.value(3).toString();
                    const QString groupType = removedQuery.value(4).toString().trimmed().isEmpty()
                        ? serverGroupTypeFromId(groupObj["groupId"].toString())
                        : removedQuery.value(4).toString();
                    const QString historyPolicy = removedQuery.value(5).toString().trimmed().isEmpty()
                        ? serverGroupHistoryPolicy(groupType)
                        : removedQuery.value(5).toString();
                    const QString filePolicy = removedQuery.value(6).toString().trimmed().isEmpty()
                        ? serverGroupFilePolicy(groupType)
                        : removedQuery.value(6).toString();
                    groupObj["groupType"] = groupType;
                    groupObj["membershipState"] = "removed";
                    groupObj["canSend"] = false;
                    groupObj["canSendFiles"] = false;
                    groupObj["canReadHistory"] = true;
                    groupObj["historyPolicy"] = historyPolicy;
                    groupObj["filePolicy"] = filePolicy;
                    groupObj["historyVisibility"] = groupType == QLatin1String("private")
                        ? QStringLiteral("removed-member-readonly")
                        : QStringLiteral("public-removed-member-readonly");
                    groupObj["historyReadOnly"] = true;
                    groupObj["historyRetainedAfterRemoval"] = historyPolicy.contains(QStringLiteral("removed-readonly"));
                    groupObj["removedBy"] = removedQuery.value(7).toString();
                    groupObj["removedByName"] = removedQuery.value(8).toString();
                    groupObj["removedAt"] = removedQuery.value(9).toString();
                    removedGroups.append(groupObj);
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);

    QJsonObject obj;
    obj["type"] = "server_group_snapshot";
    obj["groups"] = groups;
    obj["removedGroups"] = removedGroups;
    socket->write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();

    // Pending applications are a management-only feed. Do not expose them to
    // ordinary members, and resend them after owner/admin reconnects.
    const QString applicationConnection = "server_group_join_application_snapshot_"
        + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase applicationDb = openAccountDatabase(applicationConnection);
        if (openAccountDatabaseConnection(applicationDb, applicationConnection)) {
            QSqlQuery applications(applicationDb);
            applications.prepare("SELECT r.request_id, r.group_id, COALESCE(g.group_name, ''), r.applicant_id, "
                                 "COALESCE(r.applicant_name, ''), COALESCE(r.message, '') "
                                 "FROM server_group_join_requests r "
                                 "JOIN server_group_members m ON m.group_id = r.group_id AND m.user_id = ? "
                                 "JOIN server_groups g ON g.group_id = r.group_id "
                                 "WHERE r.state = 'pending' AND m.role IN ('owner', 'admin') "
                                 "ORDER BY r.created_at ASC LIMIT 100");
            applications.addBindValue(userId);
            if (applications.exec()) {
                while (applications.next()) {
                    QJsonObject application;
                    application["type"] = "server_group_join_application";
                    application["requestId"] = applications.value(0).toString();
                    application["groupId"] = applications.value(1).toString();
                    application["groupName"] = applications.value(2).toString();
                    application["applicantId"] = applications.value(3).toString();
                    application["applicantName"] = applications.value(4).toString();
                    application["message"] = applications.value(5).toString();
                    socket->write(QJsonDocument(application).toJson(QJsonDocument::Compact));
                    socket->write("\n");
                }
                socket->flush();
            }
            applicationDb.close();
        }
    }
    releaseAccountDatabase(applicationConnection);

    const QString outgoingApplicationConnection = "server_group_join_outgoing_snapshot_"
        + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase outgoingDb = openAccountDatabase(outgoingApplicationConnection);
        if (openAccountDatabaseConnection(outgoingDb, outgoingApplicationConnection)) {
            QSqlQuery applications(outgoingDb);
            applications.prepare("SELECT r.request_id, r.group_id, COALESCE(g.group_name, ''), COALESCE(r.message, '') "
                                 "FROM server_group_join_requests r JOIN server_groups g ON g.group_id = r.group_id "
                                 "WHERE r.applicant_id = ? AND r.state = 'pending' ORDER BY r.created_at ASC LIMIT 100");
            applications.addBindValue(userId);
            if (applications.exec()) {
                while (applications.next()) {
                    QJsonObject application;
                    application["type"] = "server_group_join_request_status";
                    application["requestId"] = applications.value(0).toString();
                    application["groupId"] = applications.value(1).toString();
                    application["groupName"] = applications.value(2).toString();
                    application["message"] = applications.value(3).toString();
                    application["state"] = "pending";
                    socket->write(QJsonDocument(application).toJson(QJsonDocument::Compact));
                    socket->write("\n");
                }
                socket->flush();
            }
            outgoingDb.close();
        }
    }
    releaseAccountDatabase(outgoingApplicationConnection);
}
