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
constexpr qsizetype kMaxE2EIdentityPublicKeyBytes = 4096;

bool envEnabled(const char* name) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
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

ChatUser* Server::findUserBySocket(QTcpSocket* socket) {
    auto it = m_clients.find(socket);
    if (it != m_clients.end()) {
        return &it.value();
    }
    return nullptr;
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
