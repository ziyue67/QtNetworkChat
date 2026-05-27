#include "server.h"
#include "redisclient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QDataStream>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QEventLoop>
#include <QTimer>
#include <QPointer>
#include <QPair>
#include <QUuid>
#include <algorithm>

namespace {
constexpr qint64 kMaxIncomingPayloadBytes = 80LL * 1024 * 1024;
constexpr qint64 kMaxIncomingChunks = 4096;
constexpr qint64 kForwardChunkBytes = 256LL * 1024;
constexpr int kChunkAckTimeoutMs = 4000;
constexpr int kChunkSendMaxAttempts = 3;
constexpr qint64 kTransferStaleTimeoutMs = 2LL * 60 * 1000;
constexpr int kTransferCleanupIntervalMs = 30 * 1000;
constexpr qint64 kDefaultOfflineAttachmentTtlDays = 14;
constexpr qint64 kMaxOfflineAttachmentTtlDays = 3650;
constexpr int kOfflineAttachmentCleanupIntervalMs = 60 * 60 * 1000;
constexpr qint64 kDefaultOfflineAttachmentResumeProgressTtlHours = 24;
constexpr qint64 kMaxOfflineAttachmentResumeProgressTtlHours = 24LL * 365;
constexpr qint64 kDefaultOfflineAttachmentQuotaBytes = 512LL * 1024 * 1024;
constexpr qint64 kRedisPubSubFileMaxBytes = 1LL * 1024 * 1024;

bool envEnabled(const char* name) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

qint64 positiveIntegerEnvOrDefault(const char* name, qint64 defaultValue, qint64 maxValue = 0) {
    const QByteArray value = qgetenv(name).trimmed();
    if (value.isEmpty()) {
        return defaultValue;
    }

    bool ok = false;
    const qint64 parsed = value.toLongLong(&ok);
    if (!ok || parsed <= 0 || (maxValue > 0 && parsed > maxValue)) {
        return defaultValue;
    }
    return parsed;
}

qint64 offlineAttachmentResumeProgressTtlMs() {
    const qint64 hours = positiveIntegerEnvOrDefault("QTNETWORKCHAT_OFFLINE_RESUME_TTL_HOURS",
                                                     kDefaultOfflineAttachmentResumeProgressTtlHours,
                                                     kMaxOfflineAttachmentResumeProgressTtlHours);
    return hours * 60LL * 60 * 1000;
}

qint64 offlineAttachmentTtlMs() {
    const qint64 days = positiveIntegerEnvOrDefault("QTNETWORKCHAT_OFFLINE_ATTACHMENT_TTL_DAYS",
                                                    kDefaultOfflineAttachmentTtlDays,
                                                    kMaxOfflineAttachmentTtlDays);
    return days * 24LL * 60 * 60 * 1000;
}

QString safePathPart(const QString& value) {
    QString safe;
    safe.reserve(value.size());
    for (const QChar& ch : value) {
        if (ch.isLetterOrNumber()
            || ch == QLatin1Char('_')
            || ch == QLatin1Char('-')
            || ch == QLatin1Char('.')) {
            safe.append(ch);
        } else {
            safe.append(QLatin1Char('_'));
        }
    }
    return safe.isEmpty() ? "unknown" : safe;
}

bool looksLikeSha256Hex(const QString& value) {
    const QString trimmed = value.trimmed();
    if (trimmed.size() != 64) return false;
    for (const QChar& ch : trimmed) {
        const ushort c = ch.toLatin1();
        const bool isHex = (c >= '0' && c <= '9')
            || (c >= 'a' && c <= 'f')
            || (c >= 'A' && c <= 'F');
        if (!isHex) return false;
    }
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
        qWarning() << "TLS requested but Qt/OpenSSL is unavailable; falling back to TCP";
        return new QTcpServer(parent);
    }

    const QString certPath = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_TLS_CERT")).trimmed();
    const QString keyPath = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_TLS_KEY")).trimmed();
    QFile certFile(certPath);
    QFile keyFile(keyPath);
    if (certPath.isEmpty() || keyPath.isEmpty()
        || !certFile.open(QIODevice::ReadOnly)
        || !keyFile.open(QIODevice::ReadOnly)) {
        qWarning() << "TLS requested but QTNETWORKCHAT_TLS_CERT/QTNETWORKCHAT_TLS_KEY are not readable; falling back to TCP";
        return new QTcpServer(parent);
    }

    const QSslCertificate certificate(&certFile, QSsl::Pem);
    const QSslKey privateKey(&keyFile, QSsl::Rsa, QSsl::Pem);
    if (certificate.isNull() || privateKey.isNull()) {
        qWarning() << "TLS certificate or private key is invalid; falling back to TCP";
        return new QTcpServer(parent);
    }

    QTcpServer* server = new TlsTcpServer(certificate, privateKey, parent);
    server->setProperty("tlsEnabled", true);
    return server;
}

void sendSystemNotice(QTcpSocket* socket, const QString& content) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

    QJsonObject response;
    response["type"] = "system";
    response["content"] = content;
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

enum class OfflineAttachmentValidationResult {
    Ready,
    CleanedBadState,
    Blocked
};

OfflineAttachmentValidationResult sendOfflineAttachmentBadStateNotice(QTcpSocket* socket,
                                                                      const QString& filePath,
                                                                      const QString& fileName,
                                                                      const QString& reason) {
    sendSystemNotice(socket, QString("%1：%2，请让对方重新发送。").arg(reason, fileName));
    QFile::remove(filePath);
    return OfflineAttachmentValidationResult::CleanedBadState;
}

OfflineAttachmentValidationResult validateOfflineAttachmentForReplay(const QJsonObject& obj,
                                                                     const QString& filePath,
                                                                     QTcpSocket* socket) {
    const QString fileName = obj["fileName"].toString("未命名文件");
    const QFileInfo attachmentInfo(filePath);
    if (!attachmentInfo.exists() || !attachmentInfo.isFile()) {
        return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件已丢失");
    }
    if (attachmentInfo.size() <= 0) {
        return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件为空");
    }

    const qint64 declaredSize = obj["fileSize"].toVariant().toLongLong();
    if (declaredSize > 0 && declaredSize != attachmentInfo.size()) {
        return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件大小异常");
    }

    const qint64 declaredChunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 declaredChunkCount = obj["chunkCount"].toVariant().toLongLong();
    if (declaredChunkSize <= 0
        || declaredChunkSize > kForwardChunkBytes
        || declaredChunkCount <= 0
        || declaredChunkCount != (attachmentInfo.size() + declaredChunkSize - 1) / declaredChunkSize) {
        return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件分片元数据异常");
    }

    const QString declaredHash = obj["fileHash"].toString().trimmed();
    if (looksLikeSha256Hex(declaredHash)) {
        QFile hashFile(filePath);
        if (!hashFile.open(QIODevice::ReadOnly)) {
            return OfflineAttachmentValidationResult::Blocked;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&hashFile)) {
            return OfflineAttachmentValidationResult::Blocked;
        }
        const QString actualHash = QString::fromLatin1(hash.result().toHex());
        hashFile.close();
        if (actualHash.compare(declaredHash, Qt::CaseInsensitive) != 0) {
            return sendOfflineAttachmentBadStateNotice(socket, filePath, fileName, "离线文件校验失败");
        }
    }

    return OfflineAttachmentValidationResult::Ready;
}

struct OfflineAttachmentReplayPlan {
    qint64 startChunkIndex = 0;
    bool confirmedChunksValid = false;
    QSet<qint64> confirmedChunkIndexes;
};

OfflineAttachmentReplayPlan buildOfflineAttachmentReplayPlan(const QJsonObject& obj,
                                                             qint64 totalBytes,
                                                             qint64 chunkSize,
                                                             qint64 chunkCount,
                                                             qint64 sqliteMessageId) {
    OfflineAttachmentReplayPlan plan;
    const qint64 declaredChunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 declaredChunkCount = obj["chunkCount"].toVariant().toLongLong();
    const qint64 recordedConfirmedBytes = obj["confirmedBytes"].toVariant().toLongLong();
    const QJsonArray confirmedChunks = obj["confirmedChunks"].toArray();
    const bool hasResumeProgress = recordedConfirmedBytes > 0 || !confirmedChunks.isEmpty();
    bool resumeProgressFresh = !hasResumeProgress;
    if (hasResumeProgress) {
        const QDateTime resumeUpdatedAt = QDateTime::fromString(obj["resumeUpdatedAt"].toString(), Qt::ISODate);
        if (resumeUpdatedAt.isValid()) {
            const qint64 ageMs = resumeUpdatedAt.toUTC().msecsTo(QDateTime::currentDateTimeUtc());
            resumeProgressFresh = ageMs >= 0 && ageMs <= offlineAttachmentResumeProgressTtlMs();
        }
    }

    plan.confirmedChunksValid = resumeProgressFresh
        && sqliteMessageId > 0
        && declaredChunkSize == chunkSize
        && declaredChunkCount == chunkCount;
    for (const QJsonValue& value : confirmedChunks) {
        const qint64 chunkIndex = value.toVariant().toLongLong();
        if (chunkIndex < 0 || chunkIndex >= chunkCount) {
            plan.confirmedChunksValid = false;
            plan.confirmedChunkIndexes.clear();
            break;
        }
        plan.confirmedChunkIndexes.insert(chunkIndex);
    }

    const bool canResumeFromConfirmedBytes = resumeProgressFresh
        && sqliteMessageId > 0
        && declaredChunkSize == chunkSize
        && (declaredChunkCount <= 0 || declaredChunkCount == chunkCount)
        && recordedConfirmedBytes > 0
        && recordedConfirmedBytes <= totalBytes
        && recordedConfirmedBytes % chunkSize == 0;
    plan.startChunkIndex = canResumeFromConfirmedBytes
        ? qMin(recordedConfirmedBytes / chunkSize, chunkCount)
        : 0;
    if (plan.confirmedChunksValid && !plan.confirmedChunkIndexes.isEmpty()) {
        plan.startChunkIndex = chunkCount;
        for (qint64 index = 0; index < chunkCount; ++index) {
            if (!plan.confirmedChunkIndexes.contains(index)) {
                plan.startChunkIndex = index;
                break;
            }
        }
    }
    return plan;
}

void sendFileChunkAck(QTcpSocket* socket,
                      const QString& transferId,
                      qint64 chunkIndex,
                      bool accepted,
                      const QString& reason = QString(),
                      qint64 receivedBytes = 0) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState || transferId.isEmpty()) return;

    QJsonObject response;
    response["type"] = "file_chunk_ack";
    response["transferId"] = transferId;
    response["chunkIndex"] = QString::number(chunkIndex);
    response["accepted"] = accepted;
    response["reason"] = reason;
    response["receivedBytes"] = QString::number(receivedBytes);
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}
}

Server::Server(QObject* parent)
    : QObject(parent)
    , m_tcpServer(createServerSocket(this))
    , m_redisClient(new RedisClient(this))
    , m_redisSubscriber(new RedisSubscriber(this))
    , m_transferCleanupTimer(new QTimer(this))
    , m_offlineAttachmentCleanupTimer(new QTimer(this))
    , m_serverPort(0)
    , m_tlsEnabled(m_tcpServer->property("tlsEnabled").toBool())
    , m_instanceId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_redisClient->configureFromEnvironment();
    m_redisSubscriber->configureFromEnvironment();
    connect(m_redisSubscriber, &RedisSubscriber::messageReceived, this, [this](const RedisClient::PubSubMessage& message) {
        handleRedisMessageEvent(message.payload);
    });

    connect(m_tcpServer, &QTcpServer::newConnection, this, &Server::onNewConnection);
    connect(m_transferCleanupTimer, &QTimer::timeout, this, &Server::cleanupExpiredFileTransfers);
    connect(m_offlineAttachmentCleanupTimer, &QTimer::timeout, this, &Server::cleanupExpiredOfflineAttachments);
    m_transferCleanupTimer->start(kTransferCleanupIntervalMs);
    m_offlineAttachmentCleanupTimer->start(kOfflineAttachmentCleanupIntervalMs);
}

Server::~Server() {
    stop();
}

bool Server::start(quint16 port) {
    if (m_redisClient->isEnabled()) {
        const bool redisConnected = m_redisClient->connectToServer();
        if (redisConnected) {
            qDebug() << "Redis presence service enabled";
        } else {
            qWarning() << "Redis presence requested but unavailable:" << m_redisClient->lastError();
        }

        const bool redisSubscribed = m_redisSubscriber->subscribe("messages");
        if (redisSubscribed) {
            qDebug() << "Redis Pub/Sub subscriber enabled";
        } else {
            qWarning() << "Redis Pub/Sub subscriber unavailable:" << m_redisSubscriber->lastError();
        }

    }

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
    if (envEnabled("QTNETWORKCHAT_TLS") && !QSslSocket::supportsSsl()) {
        return "TLS 已请求，但 Qt/OpenSSL 不可用，已回退 TCP";
    }
    if (envEnabled("QTNETWORKCHAT_TLS")) {
        return "TLS 已请求，但证书未配置，已回退 TCP";
    }
    return "普通 TCP 服务";
}

void Server::stop() {
    m_transferCleanupTimer->stop();
    m_offlineAttachmentCleanupTimer->stop();
    m_redisSubscriber->disconnectFromServer();
    for (const ChatUser& user : m_clients.values()) {
        clearRedisPresence(user.id);
    }
    for (QTcpSocket* socket : m_clients.keys()) {
        socket->disconnectFromHost();
    }
    m_clients.clear();
    m_userSockets.clear();
    m_pendingFileTransfers.clear();
    m_tcpServer->close();
    qDebug() << "Server stopped";
}

void Server::onNewConnection() {
    QTcpSocket* clientSocket = m_tcpServer->nextPendingConnection();
    if (!clientSocket) return;

    qDebug() << "New connection from:" << clientSocket->peerAddress().toString()
             << "port:" << clientSocket->peerPort();

    connect(clientSocket, &QTcpSocket::readyRead, this, &Server::onClientReadyRead);
    connect(clientSocket, &QTcpSocket::disconnected, this, &Server::onClientDisconnected);
}

void Server::onClientReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray buffer = socket->property("buffer").toByteArray();
    buffer.append(socket->readAll());
    qDebug() << "Received from" << socket->peerAddress().toString() << ":" << buffer.size() << "bytes";

    while (buffer.contains('\n')) {
        int newlineIndex = buffer.indexOf('\n');
        QByteArray line = buffer.left(newlineIndex);
        buffer = buffer.mid(newlineIndex + 1);
        if (line.isEmpty()) continue;

        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isNull() || !doc.isObject()) {
            qWarning() << "Invalid JSON received";
            continue;
        }

        QJsonObject obj = doc.object();
        QString type = obj["type"].toString();

        if (type == "login") {
            handleLogin(obj, socket);
        } else if (type == "message") {
            handleMessage(obj, socket);
        } else if (type == "file") {
            handleFile(obj, socket);
        } else if (type == "file_chunk") {
            handleFileChunk(obj, socket);
        } else if (type == "file_transfer_resume_query") {
            handleFileTransferResumeQuery(obj, socket);
        } else if (type == "file_transfer_cancel") {
            handleFileTransferCancel(obj, socket);
        } else if (type == "file_chunk_ack") {
            emit fileChunkAckReceived(
                socket,
                obj["transferId"].toString(),
                obj["chunkIndex"].toVariant().toLongLong(),
                obj["accepted"].toBool(false),
                obj["reason"].toString(),
                obj["receivedBytes"].toVariant().toLongLong());
        } else if (type == "private") {
            handleMessage(obj, socket);
        } else if (type == "server_group_announcement_update") {
            handleServerGroupAnnouncementUpdate(obj, socket);
        } else if (type == "server_group_member_update") {
            handleServerGroupMemberUpdate(obj, socket);
        } else if (type == "friend_request" || type == "friend_response" || type == "friend_search") {
            handleFriendEvent(obj, socket);
        } else if (type == "heartbeat") {
            ChatUser* user = findUserBySocket(socket);
            if (user) {
                user->lastActive = QDateTime::currentDateTime();
                refreshRedisPresence(*user);
            }
        }
    }

    socket->setProperty("buffer", buffer);
}

void Server::onClientDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    ChatUser* user = findUserBySocket(socket);
    if (user) {
        recordUserSessionToSqlite(*user, "logout");
        QString userId = user->id;
        QString userName = user->name;
        clearRedisPresence(userId);
        const QString pendingPrefix = QString::number(reinterpret_cast<quintptr>(socket)) + ":";
        for (const QString& key : m_pendingFileTransfers.keys()) {
            if (key.startsWith(pendingPrefix)) {
                m_pendingFileTransfers.remove(key);
            }
        }
        m_userSockets.remove(userId);
        m_clients.remove(socket);
        m_usedNames.remove(userName);

        emit userLeft(userId, userName);
        emit clientDisconnected(userId);

        Message sysMsg;
        sysMsg.type = MessageType::System;
        sysMsg.content = userName + " 离开了聊天室";
        sysMsg.timestamp = QDateTime::currentDateTime();
        broadcastMessage(sysMsg, socket);
        for (QTcpSocket* clientSocket : m_clients.keys()) {
            if (clientSocket->state() == QAbstractSocket::ConnectedState) {
                sendUserList(clientSocket);
            }
        }

        qDebug() << "User disconnected:" << userName;
    }
    socket->deleteLater();
}

void Server::handleLogin(const QJsonObject& obj, QTcpSocket* socket) {
    QString mode = obj["mode"].toString("login");
    QString account = obj["account"].toString().trimmed();
    QString password = obj["password"].toString();
    QString userName = obj["userName"].toString().trimmed();

    if (mode != "register" && account.isEmpty()) account = userName;
    if (userName.isEmpty()) userName = account.isEmpty() ? "User" : account;

    QJsonObject accounts = loadAccountsFromSqlite();
    if (mode == "register") {
        if (password.isEmpty()) {
            QJsonObject response;
            response["type"] = "login_failed";
            response["reason"] = "密码不能为空";
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
            return;
        }
        if (account.isEmpty()) {
            account = generateAccountId(accounts);
        }
        QString passwordHash = QString::fromLatin1(QCryptographicHash::hash((account + ":" + password).toUtf8(), QCryptographicHash::Sha256).toHex());
        if (accounts.contains(account)) {
            QJsonObject response;
            response["type"] = "login_failed";
            response["reason"] = "账号已存在";
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
            return;
        }
        QJsonObject accountObj;
        accountObj["passwordHash"] = passwordHash;
        accountObj["userName"] = userName;
        accountObj["userId"] = account;
        accounts[account] = accountObj;
        insertAccountToSqlite(account, passwordHash, userName);
    } else if (accounts.contains(account)) {
        QString passwordHash = QString::fromLatin1(QCryptographicHash::hash((account + ":" + password).toUtf8(), QCryptographicHash::Sha256).toHex());
        QJsonObject accountObj = accounts[account].toObject();
        QString storedHash = accountObj["passwordHash"].toString();
        if (storedHash.isEmpty()) {
            storedHash = QString::fromLatin1(QCryptographicHash::hash((account + ":" + accountObj["password"].toString()).toUtf8(), QCryptographicHash::Sha256).toHex());
            accountObj.remove("password");
            accountObj["passwordHash"] = storedHash;
            accounts[account] = accountObj;
            insertAccountToSqlite(account, storedHash, accountObj["userName"].toString(userName));
        }
        if (storedHash != passwordHash) {
            QJsonObject response;
            response["type"] = "login_failed";
            response["reason"] = "密码错误";
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
            return;
        }
        userName = accountObj["userName"].toString(userName);
    } else if (!account.isEmpty()) {
        QJsonObject response;
        response["type"] = "login_failed";
        response["reason"] = "账号不存在，请先注册";
        socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
        socket->write("\n");
        socket->flush();
        return;
    }

    if (m_usedNames.contains(userName)) {
        userName += "_" + QString::number(QDateTime::currentMSecsSinceEpoch() % 10000);
    }

    ChatUser user;
    user.id = account.isEmpty() ? QString::number(QDateTime::currentMSecsSinceEpoch()) : account;
    user.name = userName;
    user.address = socket->peerAddress();
    user.port = socket->peerPort();
    user.isOnline = true;
    user.lastActive = QDateTime::currentDateTime();

    m_clients[socket] = user;
    m_userSockets[user.id] = socket;
    m_usedNames.insert(userName);
    recordUserSessionToSqlite(user, "login");
    recordDefaultGroupMembership(user);
    refreshRedisPresence(user);

    QJsonObject response;
    response["type"] = "login_success";
    response["userId"] = user.id;
    response["userName"] = user.name;
    response["account"] = account;
    response["registered"] = mode == "register";
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();

    sendUserList(socket);
    sendServerGroupSnapshot(user.id, socket);
    for (QTcpSocket* clientSocket : m_clients.keys()) {
        if (clientSocket != socket && clientSocket->state() == QAbstractSocket::ConnectedState) {
            sendUserList(clientSocket);
        }
    }
    emit userJoined(user.id, user.name);
    emit clientConnected(user.id);

    Message sysMsg;
    sysMsg.type = MessageType::System;
    sysMsg.content = userName + " 加入了聊天室";
    sysMsg.timestamp = QDateTime::currentDateTime();
    broadcastMessage(sysMsg, socket);

    QPointer<QTcpSocket> socketGuard(socket);
    const QString loggedInUserId = user.id;
    QTimer::singleShot(0, this, [this, socketGuard, loggedInUserId]() {
        if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState) return;
        const ChatUser* currentUser = findUserBySocket(socketGuard);
        if (!currentUser || currentUser->id != loggedInUserId) return;
        sendOfflineMessages(loggedInUserId, socketGuard);
    });

    qDebug() << "User logged in:" << user.name << "id:" << user.id;
}

void Server::handleMessage(const QJsonObject& obj, QTcpSocket* socket) {
    Message msg;
    msg.type = static_cast<MessageType>(obj["messageType"].toInt(static_cast<int>(MessageType::Text)));
    msg.senderId = obj["senderId"].toString();
    msg.senderName = obj["senderName"].toString();
    msg.content = obj["content"].toString();
    msg.receiverId = obj["receiverId"].toString();
    msg.timestamp = QDateTime::currentDateTime();

    if (ChatUser* sender = findUserBySocket(socket)) {
        msg.senderId = sender->id;
        msg.senderName = sender->name;
    }

    QString deliveryState = "broadcast";
    if (!msg.receiverId.isEmpty()) {
        QTcpSocket* targetSocket = m_userSockets.value(msg.receiverId);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            deliveryState = "direct";
            sendToUser(msg);
        } else if (isRedisUserOnline(msg.receiverId)) {
            deliveryState = "remote";
        } else {
            deliveryState = "offline";
            sendToUser(msg);
        }
    } else {
        if (!isServerGroupMember("public", msg.senderId)) {
            sendSystemNotice(socket, "公共群消息发送失败：你已不在该群组，请联系群主或管理员重新邀请。");
            return;
        }
        broadcastMessage(msg);
    }
    saveMessageToSqlite(msg, deliveryState);
    const bool redisPublished = publishRedisMessageEvent(msg, deliveryState);
    if (deliveryState == "remote" && !redisPublished) {
        saveOfflineMessage(msg);
    }

    emit newMessage(msg);
}

void Server::handleServerGroupAnnouncementUpdate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) {
        sendSystemNotice(socket, "群公告更新失败：请先登录");
        return;
    }

    const QString groupId = obj["groupId"].toString("public").trimmed().isEmpty()
        ? QString("public")
        : obj["groupId"].toString("public").trimmed();
    QString announcement = obj["announcement"].toString().trimmed();
    if (announcement.isEmpty()) {
        announcement = "欢迎来到公共聊天室。";
    }
    if (announcement.size() > 1000) {
        announcement = announcement.left(1000);
    }
    if (!ensureAccountDatabase()) {
        sendSystemNotice(socket, "群公告更新失败：服务端群组存储不可用");
        return;
    }

    QStringList memberIds;
    bool allowed = false;
    bool saved = false;
    QString errorText;
    const QString connectionName = "server_group_announcement_update_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(accountDbPath());
        if (!db.open()) {
            errorText = "群公告更新失败：无法打开群组数据库";
        } else {
            QSqlQuery permissionQuery(db);
            permissionQuery.prepare("SELECT COALESCE(g.owner_id, ''), COALESCE(m.role, '') "
                                    "FROM server_groups g "
                                    "JOIN server_group_members m ON m.group_id = g.group_id "
                                    "WHERE g.group_id = ? AND m.user_id = ?");
            permissionQuery.addBindValue(groupId);
            permissionQuery.addBindValue(requester->id);
            if (!permissionQuery.exec()) {
                errorText = "群公告更新失败：权限校验失败";
            } else if (!permissionQuery.next()) {
                errorText = "群公告更新失败：你不在该群组";
            } else {
                const QString ownerId = permissionQuery.value(0).toString();
                const QString role = permissionQuery.value(1).toString().toLower();
                allowed = ownerId == requester->id || role == "owner" || role == "admin";
                if (!allowed) {
                    errorText = "群公告更新失败：只有群主或管理员可以编辑";
                }
            }

            if (allowed) {
                QSqlQuery updateQuery(db);
                updateQuery.prepare("UPDATE server_groups SET announcement = ?, updated_at = datetime('now') "
                                    "WHERE group_id = ?");
                updateQuery.addBindValue(announcement);
                updateQuery.addBindValue(groupId);
                saved = updateQuery.exec();
                if (!saved) {
                    errorText = "群公告更新失败：保存公告失败";
                }
            }

            if (saved) {
                QSqlQuery insertQuery(db);
                insertQuery.prepare("INSERT INTO server_group_announcements(group_id, author_id, author_name, content, created_at) "
                                    "VALUES(?, ?, ?, ?, datetime('now'))");
                insertQuery.addBindValue(groupId);
                insertQuery.addBindValue(requester->id);
                insertQuery.addBindValue(requester->name);
                insertQuery.addBindValue(announcement);
                if (!insertQuery.exec()) {
                    qWarning() << "Failed to record server group announcement history:" << insertQuery.lastError().text();
                }
            }

            if (saved) {
                QSqlQuery memberQuery(db);
                memberQuery.prepare("SELECT user_id FROM server_group_members WHERE group_id = ?");
                memberQuery.addBindValue(groupId);
                if (memberQuery.exec()) {
                    while (memberQuery.next()) {
                        const QString memberId = memberQuery.value(0).toString();
                        if (!memberId.isEmpty() && !memberIds.contains(memberId)) {
                            memberIds << memberId;
                        }
                    }
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);

    if (!saved) {
        sendSystemNotice(socket, errorText.isEmpty() ? "群公告更新失败" : errorText);
        return;
    }

    const QString notice = QString("%1 更新了群公告").arg(requester->name.isEmpty() ? requester->id : requester->name);
    for (const QString& memberId : memberIds) {
        QTcpSocket* memberSocket = m_userSockets.value(memberId);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        sendSystemNotice(memberSocket, notice);
        sendServerGroupSnapshot(memberId, memberSocket);
    }
}

void Server::handleServerGroupMemberUpdate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* requester = findUserBySocket(socket);
    if (!requester) {
        sendSystemNotice(socket, "群成员变更失败：请先登录");
        return;
    }

    const QString groupId = obj["groupId"].toString("public").trimmed().isEmpty()
        ? QString("public")
        : obj["groupId"].toString("public").trimmed();
    const QString action = obj["action"].toString().trimmed().toLower();
    const QString memberId = obj["memberId"].toString().trimmed();
    if ((action != "add" && action != "remove") || groupId.isEmpty() || memberId.isEmpty()) {
        sendSystemNotice(socket, "群成员变更失败：请求参数无效");
        return;
    }
    if (!ensureAccountDatabase()) {
        sendSystemNotice(socket, "群成员变更失败：服务端群组存储不可用");
        return;
    }

    QStringList affectedUserIds;
    bool changed = false;
    QString memberName;
    QString errorText;
    const QString connectionName = "server_group_member_update_" + QString::number(reinterpret_cast<quintptr>(socket));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(accountDbPath());
        if (!db.open()) {
            errorText = "群成员变更失败：无法打开群组数据库";
        } else {
            QString ownerId;
            QString requesterRole;
            QSqlQuery permissionQuery(db);
            permissionQuery.prepare("SELECT COALESCE(g.owner_id, ''), COALESCE(m.role, '') "
                                    "FROM server_groups g "
                                    "JOIN server_group_members m ON m.group_id = g.group_id "
                                    "WHERE g.group_id = ? AND m.user_id = ?");
            permissionQuery.addBindValue(groupId);
            permissionQuery.addBindValue(requester->id);
            if (!permissionQuery.exec()) {
                errorText = "群成员变更失败：权限校验失败";
            } else if (!permissionQuery.next()) {
                errorText = "群成员变更失败：你不在该群组";
            } else {
                ownerId = permissionQuery.value(0).toString();
                requesterRole = permissionQuery.value(1).toString().toLower();
                const bool allowed = ownerId == requester->id || requesterRole == "owner" || requesterRole == "admin";
                if (!allowed) {
                    errorText = "群成员变更失败：只有群主或管理员可以管理成员";
                }
            }

            if (errorText.isEmpty() && action == "add") {
                QSqlQuery accountQuery(db);
                accountQuery.prepare("SELECT COALESCE(user_name, '') FROM accounts WHERE account = ?");
                accountQuery.addBindValue(memberId);
                if (!accountQuery.exec()) {
                    errorText = "群成员变更失败：账号查询失败";
                } else if (!accountQuery.next()) {
                    errorText = "群成员变更失败：目标账号不存在";
                } else {
                    memberName = accountQuery.value(0).toString();
                    if (memberName.isEmpty()) memberName = memberId;
                }
            }

            bool alreadyMember = false;
            QString targetRole;
            if (errorText.isEmpty()) {
                QSqlQuery targetQuery(db);
                targetQuery.prepare("SELECT COALESCE(user_name, ''), COALESCE(role, '') "
                                    "FROM server_group_members WHERE group_id = ? AND user_id = ?");
                targetQuery.addBindValue(groupId);
                targetQuery.addBindValue(memberId);
                if (!targetQuery.exec()) {
                    errorText = "群成员变更失败：成员查询失败";
                } else if (targetQuery.next()) {
                    alreadyMember = true;
                    if (memberName.isEmpty()) memberName = targetQuery.value(0).toString();
                    targetRole = targetQuery.value(1).toString().toLower();
                }
            }

            if (errorText.isEmpty() && action == "add") {
                if (alreadyMember) {
                    errorText = "该用户已经是群成员";
                } else {
                    QSqlQuery insertQuery(db);
                    insertQuery.prepare("INSERT INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) "
                                        "VALUES(?, ?, ?, 'member', datetime('now'), datetime('now'))");
                    insertQuery.addBindValue(groupId);
                    insertQuery.addBindValue(memberId);
                    insertQuery.addBindValue(memberName);
                    if (!insertQuery.exec()) {
                        errorText = "群成员变更失败：添加成员失败";
                    } else {
                        QSqlQuery clearRemovedQuery(db);
                        clearRemovedQuery.prepare("DELETE FROM server_group_removed_members WHERE group_id = ? AND user_id = ?");
                        clearRemovedQuery.addBindValue(groupId);
                        clearRemovedQuery.addBindValue(memberId);
                        if (!clearRemovedQuery.exec()) {
                            qWarning() << "Failed to clear removed group member marker:" << clearRemovedQuery.lastError().text();
                        }
                        changed = true;
                    }
                }
            } else if (errorText.isEmpty() && action == "remove") {
                if (!alreadyMember) {
                    errorText = "群成员变更失败：目标用户不在该群组";
                } else if (memberId == ownerId || targetRole == "owner") {
                    errorText = "群成员变更失败：不能移出群主";
                } else if (memberId == requester->id) {
                    errorText = "群成员变更失败：不能通过管理操作移出自己";
                } else {
                    QSqlQuery deleteQuery(db);
                    deleteQuery.prepare("DELETE FROM server_group_members WHERE group_id = ? AND user_id = ?");
                    deleteQuery.addBindValue(groupId);
                    deleteQuery.addBindValue(memberId);
                    if (!deleteQuery.exec()) {
                        errorText = "群成员变更失败：移出成员失败";
                    } else {
                        QSqlQuery removedQuery(db);
                        removedQuery.prepare("INSERT OR REPLACE INTO server_group_removed_members(group_id, user_id, removed_by, removed_by_name, removed_at) "
                                             "VALUES(?, ?, ?, ?, datetime('now'))");
                        removedQuery.addBindValue(groupId);
                        removedQuery.addBindValue(memberId);
                        removedQuery.addBindValue(requester->id);
                        removedQuery.addBindValue(requester->name);
                        if (!removedQuery.exec()) {
                            qWarning() << "Failed to record removed group member marker:" << removedQuery.lastError().text();
                        }
                        changed = true;
                    }
                }
            }

            if (changed) {
                QSqlQuery memberQuery(db);
                memberQuery.prepare("SELECT user_id FROM server_group_members WHERE group_id = ?");
                memberQuery.addBindValue(groupId);
                if (memberQuery.exec()) {
                    while (memberQuery.next()) {
                        const QString userId = memberQuery.value(0).toString();
                        if (!userId.isEmpty() && !affectedUserIds.contains(userId)) {
                            affectedUserIds << userId;
                        }
                    }
                }
                if (!affectedUserIds.contains(requester->id)) {
                    affectedUserIds << requester->id;
                }
                if (!affectedUserIds.contains(memberId)) {
                    affectedUserIds << memberId;
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);

    if (!changed) {
        sendSystemNotice(socket, errorText.isEmpty() ? "群成员变更未生效" : errorText);
        return;
    }

    const QString displayName = memberName.isEmpty() ? memberId : memberName;
    const QString notice = action == "add"
        ? QString("%1 已被加入群组").arg(displayName)
        : QString("%1 已被移出群组").arg(displayName);
    for (const QString& userId : affectedUserIds) {
        QTcpSocket* memberSocket = m_userSockets.value(userId);
        if (!memberSocket || memberSocket->state() != QAbstractSocket::ConnectedState) continue;
        sendSystemNotice(memberSocket, notice);
        sendServerGroupSnapshot(userId, memberSocket);
    }
}

void Server::handleFriendEvent(const QJsonObject& obj, QTcpSocket* socket) {
    QString type = obj["type"].toString();
    if (type == "friend_search") {
        QString account = obj["account"].toString().trimmed();
        ChatUser* requester = findUserBySocket(socket);
        QJsonObject response;
        response["type"] = "friend_search_result";
        response["account"] = account;
        response["exactMatch"] = false;
        response["matchCount"] = 0;
        response["matchReason"] = "未找到匹配资料";

        QJsonObject accounts = loadAccountsFromSqlite();
        if (!account.isEmpty() && accounts.contains(account)) {
            QJsonObject accountObj = accounts[account].toObject();
            response["found"] = true;
            response["userId"] = account;
            response["userName"] = accountObj["userName"].toString(account);
            response["online"] = m_userSockets.contains(account);
            response["exactMatch"] = true;
            response["matchCount"] = 1;
            response["matchReason"] = "QQ号精确匹配";
        } else {
            QString matchedId;
            QString matchedName;
            int matchCount = 0;
            for (auto it = accounts.begin(); it != accounts.end(); ++it) {
                const QString candidateId = it.key();
                const QJsonObject accountObj = it.value().toObject();
                const QString candidateName = accountObj["userName"].toString(candidateId);
                const bool idMatched = candidateId.contains(account, Qt::CaseInsensitive);
                const bool nameMatched = candidateName.contains(account, Qt::CaseInsensitive);
                if (!account.isEmpty() && (idMatched || nameMatched)) {
                    ++matchCount;
                    if (matchedId.isEmpty()) {
                        matchedId = candidateId;
                        matchedName = candidateName;
                        response["matchReason"] = idMatched ? "QQ号模糊匹配" : "昵称模糊匹配";
                    }
                }
            }

            response["matchCount"] = matchCount;
            if (!matchedId.isEmpty()) {
                response["found"] = true;
                response["userId"] = matchedId;
                response["userName"] = matchedName.isEmpty() ? matchedId : matchedName;
                response["online"] = m_userSockets.contains(matchedId);
            } else {
                response["found"] = false;
                response["online"] = false;
            }
        }
        const QString searchState = response["found"].toBool()
            ? QString("%1_%2").arg(response["exactMatch"].toBool() ? "found_exact" : "found_fuzzy",
                                   response["online"].toBool() ? "online" : "offline")
            : "not_found";
        saveFriendEventToSqlite(type,
                                requester ? requester->id : QString(),
                                requester ? requester->name : QString(),
                                response["userId"].toString(),
                                account,
                                searchState);

        if (socket && socket->state() == QAbstractSocket::ConnectedState) {
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
        }
        return;
    }

    QString receiverId = obj["receiverId"].toString();
    const QString senderId = obj["senderId"].toString();
    const QString senderName = obj["senderName"].toString();
    QTcpSocket* targetSocket = m_userSockets.value(receiverId);
    if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) {
        saveFriendEventToSqlite(type, senderId, senderName, receiverId, QString(), "target_offline", obj["accepted"].toBool(false));
        if (type == "friend_request" && socket && socket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject response;
            response["type"] = "friend_request_sent";
            response["receiverId"] = receiverId;
            response["delivered"] = false;
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
        }
        return;
    }
    saveFriendEventToSqlite(type, senderId, senderName, receiverId, QString(), "delivered", obj["accepted"].toBool(false));

    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    targetSocket->write(data);
    targetSocket->write("\n");
    targetSocket->flush();

    if (type == "friend_request" && socket && socket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject response;
        response["type"] = "friend_request_sent";
        response["receiverId"] = receiverId;
        response["delivered"] = true;
        socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
        socket->write("\n");
        socket->flush();
    }
}

void Server::handleFile(const QJsonObject& obj, QTcpSocket* socket) {
    Message msg;
    msg.senderId = obj["senderId"].toString();
    msg.senderName = obj["senderName"].toString();
    msg.receiverId = obj["receiverId"].toString();
    msg.content = obj["content"].toString();
    msg.fileName = obj["fileName"].toString();
    msg.fileSize = obj["fileSize"].toVariant().toLongLong();
    msg.fileHash = obj["fileHash"].toString();
    const qint64 declaredChunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 declaredChunkCount = obj["chunkCount"].toVariant().toLongLong();
    msg.chunkSize = declaredChunkSize;
    msg.chunkCount = declaredChunkCount;
    msg.type = static_cast<MessageType>(obj["messageType"].toInt(static_cast<int>(MessageType::File)));
    msg.timestamp = QDateTime::currentDateTime();

    if (ChatUser* sender = findUserBySocket(socket)) {
        msg.senderId = sender->id;
        msg.senderName = sender->name;
    }
    if (msg.receiverId.isEmpty() && !isServerGroupMember("public", msg.senderId)) {
        sendSystemNotice(socket, "公共群文件发送失败：你已不在该群组，请联系群主或管理员重新邀请。");
        return;
    }

    QString base64Data = obj["fileData"].toString();
    if (!base64Data.isEmpty()) {
        msg.fileData = QByteArray::fromBase64(base64Data.toLatin1());
    }
    const qint64 declaredSize = msg.fileSize;
    const QString declaredHash = msg.fileHash.trimmed();
    const qint64 actualSize = msg.fileData.size();
    const QString actualHash = msg.fileData.isEmpty()
        ? QString()
        : QString::fromLatin1(QCryptographicHash::hash(msg.fileData, QCryptographicHash::Sha256).toHex());

    QStringList integrityErrors;
    if (actualSize <= 0) {
        integrityErrors << "文件内容为空";
    }
    if (declaredSize < 0) {
        integrityErrors << "声明大小非法";
    }
    if (declaredSize > kMaxIncomingPayloadBytes || actualSize > kMaxIncomingPayloadBytes) {
        integrityErrors << QString("超过服务器限制 %1 MB").arg(kMaxIncomingPayloadBytes / 1024 / 1024);
    }
    if (declaredSize > 0 && declaredSize != actualSize) {
        integrityErrors << QString("大小不一致：声明 %1 字节，实际 %2 字节").arg(declaredSize).arg(actualSize);
    }
    if (!declaredHash.isEmpty() && actualHash.compare(declaredHash, Qt::CaseInsensitive) != 0) {
        integrityErrors << "SHA-256 不一致";
    }
    if (declaredChunkSize < 0 || declaredChunkCount < 0) {
        integrityErrors << "分片元数据非法";
    } else if (declaredChunkSize > 0 || declaredChunkCount > 0) {
        if (declaredChunkSize <= 0 || declaredChunkCount <= 0) {
            integrityErrors << "分片元数据不完整";
        } else {
            const qint64 basisSize = declaredSize > 0 ? declaredSize : actualSize;
            const qint64 expectedChunkCount = (basisSize + declaredChunkSize - 1) / declaredChunkSize;
            if (expectedChunkCount != declaredChunkCount) {
                integrityErrors << QString("分片数量不一致：声明 %1 片，预期 %2 片")
                                       .arg(declaredChunkCount)
                                       .arg(expectedChunkCount);
            }
        }
    }
    if (!integrityErrors.isEmpty()) {
        const QString visibleName = msg.fileName.isEmpty() ? "未命名文件" : msg.fileName;
        sendSystemNotice(socket, QString("文件传输已被服务端拒绝：%1，%2。请重新发送。")
                                .arg(visibleName, integrityErrors.join("；")));
        qWarning() << "Rejected file transfer from" << msg.senderId << msg.fileName << integrityErrors;
        return;
    }

    if (msg.fileSize <= 0) {
        msg.fileSize = msg.fileData.size();
    }
    if (msg.fileHash.isEmpty() && !actualHash.isEmpty()) {
        msg.fileHash = actualHash;
    }

    QString deliveryState = "broadcast";
    if (!msg.receiverId.isEmpty()) {
        QTcpSocket* targetSocket = m_userSockets.value(msg.receiverId);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            deliveryState = "direct";
            sendToUser(msg);
        } else if (msg.fileData.size() <= kRedisPubSubFileMaxBytes && isRedisUserOnline(msg.receiverId)) {
            deliveryState = "remote";
        } else {
            deliveryState = "offline";
            sendToUser(msg);
        }
    } else {
        broadcastMessage(msg);
    }
    saveMessageToSqlite(msg, deliveryState);
    const bool redisPublished = publishRedisMessageEvent(msg, deliveryState);
    if (deliveryState == "remote" && !redisPublished) {
        saveOfflineMessage(msg);
    }
}

void Server::handleFileChunk(const QJsonObject& obj, QTcpSocket* socket) {
    if (!socket) return;

    const QString transferId = obj["transferId"].toString().trimmed();
    const QString fileName = obj["fileName"].toString();
    const qint64 fileSize = obj["fileSize"].toVariant().toLongLong();
    const qint64 chunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 chunkCount = obj["chunkCount"].toVariant().toLongLong();
    const qint64 chunkIndex = obj["chunkIndex"].toVariant().toLongLong();
    const QByteArray chunkData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());
    const QString key = QString::number(reinterpret_cast<quintptr>(socket)) + ":" + transferId;

    auto rejectTransfer = [this, socket, key, fileName, transferId, chunkIndex](const QString& reason) {
        m_pendingFileTransfers.remove(key);
        const QString visibleName = fileName.isEmpty() ? "未命名文件" : fileName;
        sendFileChunkAck(socket, transferId, chunkIndex, false, reason);
        sendSystemNotice(socket, QString("文件分片上传已被服务端拒绝：%1，%2。请重新发送。").arg(visibleName, reason));
        qWarning() << "Rejected file chunk transfer" << visibleName << reason;
    };

    if (transferId.isEmpty()) {
        rejectTransfer("缺少传输编号");
        return;
    }
    if (fileSize <= 0 || fileSize > kMaxIncomingPayloadBytes) {
        rejectTransfer(QString("文件大小非法或超过 %1 MB").arg(kMaxIncomingPayloadBytes / 1024 / 1024));
        return;
    }
    if (chunkSize <= 0 || chunkCount <= 0 || chunkIndex < 0 || chunkIndex >= chunkCount) {
        rejectTransfer("分片序号或数量非法");
        return;
    }
    if (chunkCount > kMaxIncomingChunks) {
        rejectTransfer(QString("分片数量超过服务器限制 %1 片").arg(kMaxIncomingChunks));
        return;
    }
    const qint64 expectedChunkCount = (fileSize + chunkSize - 1) / chunkSize;
    if (expectedChunkCount != chunkCount) {
        rejectTransfer(QString("分片数量不一致：声明 %1 片，预期 %2 片").arg(chunkCount).arg(expectedChunkCount));
        return;
    }
    if (chunkData.isEmpty() || chunkData.size() > chunkSize) {
        rejectTransfer("分片内容为空或超过声明大小");
        return;
    }
    if (chunkIndex < chunkCount - 1 && chunkData.size() != chunkSize) {
        rejectTransfer("非末尾分片大小不一致");
        return;
    }

    PendingFileTransfer& pending = m_pendingFileTransfers[key];
    if (pending.chunks.isEmpty()) {
        pending.envelope = obj;
        pending.envelope["type"] = "file";
        pending.envelope.remove("transferId");
        pending.envelope.remove("chunkIndex");
        pending.envelope.remove("fileData");
        pending.socket = socket;
        pending.fileName = fileName;
        pending.fileSize = fileSize;
        pending.chunkSize = chunkSize;
        pending.chunkCount = chunkCount;
        pending.chunks.resize(static_cast<int>(chunkCount));
    } else if (pending.fileSize != fileSize || pending.chunkSize != chunkSize || pending.chunkCount != chunkCount) {
        rejectTransfer("同一传输编号的元数据不一致");
        return;
    }
    pending.lastActivityMs = QDateTime::currentMSecsSinceEpoch();

    const int index = static_cast<int>(chunkIndex);
    if (!pending.receivedIndexes.contains(index)) {
        pending.chunks[index] = chunkData;
        pending.receivedIndexes.insert(index);
        pending.receivedBytes += chunkData.size();
    }
    if (pending.receivedBytes > fileSize) {
        rejectTransfer("累计分片大小超过声明文件大小");
        return;
    }
    sendFileChunkAck(socket, transferId, chunkIndex, true, QString(), pending.receivedBytes);
    if (pending.receivedIndexes.size() < pending.chunkCount) {
        return;
    }

    QByteArray fileData;
    fileData.reserve(static_cast<int>(fileSize));
    for (const QByteArray& chunk : pending.chunks) {
        if (chunk.isEmpty()) {
            rejectTransfer("存在缺失分片");
            return;
        }
        fileData.append(chunk);
    }

    QJsonObject fullFile = pending.envelope;
    m_pendingFileTransfers.remove(key);
    fullFile["fileData"] = QString::fromLatin1(fileData.toBase64());
    handleFile(fullFile, socket);
}

void Server::handleFileTransferResumeQuery(const QJsonObject& obj, QTcpSocket* socket) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

    const QString transferId = obj["transferId"].toString().trimmed();
    QJsonObject response;
    response["type"] = "file_transfer_resume_state";
    response["transferId"] = transferId;
    response["canResume"] = false;
    response["confirmedBytes"] = QString::number(0);
    response["nextChunkIndex"] = QString::number(0);
    response["receivedChunks"] = QJsonArray();

    if (transferId.isEmpty()) {
        response["reason"] = "缺少传输编号";
    } else {
        const QString key = QString::number(reinterpret_cast<quintptr>(socket)) + ":" + transferId;
        const auto it = m_pendingFileTransfers.constFind(key);
        if (it == m_pendingFileTransfers.constEnd()) {
            response["reason"] = "未找到未完成传输";
        } else {
            const PendingFileTransfer& pending = it.value();
            QJsonArray receivedChunks;
            QVector<int> sortedIndexes = pending.receivedIndexes.values().toVector();
            std::sort(sortedIndexes.begin(), sortedIndexes.end());
            for (int index : sortedIndexes) {
                receivedChunks.append(QString::number(index));
            }

            qint64 nextChunkIndex = 0;
            while (nextChunkIndex < pending.chunkCount
                   && pending.receivedIndexes.contains(static_cast<int>(nextChunkIndex))) {
                ++nextChunkIndex;
            }

            response["canResume"] = true;
            response["reason"] = "";
            response["fileName"] = pending.fileName;
            response["fileSize"] = QString::number(pending.fileSize);
            response["chunkSize"] = QString::number(pending.chunkSize);
            response["chunkCount"] = QString::number(pending.chunkCount);
            response["fileHash"] = pending.envelope["fileHash"].toString();
            response["confirmedBytes"] = QString::number(pending.receivedBytes);
            response["nextChunkIndex"] = QString::number(nextChunkIndex);
            response["receivedChunks"] = receivedChunks;
        }
    }

    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

void Server::handleFileTransferCancel(const QJsonObject& obj, QTcpSocket* socket) {
    if (!socket) return;

    const QString transferId = obj["transferId"].toString().trimmed();
    if (transferId.isEmpty()) return;

    const QString key = QString::number(reinterpret_cast<quintptr>(socket)) + ":" + transferId;
    const bool removed = m_pendingFileTransfers.remove(key) > 0;
    const QString fileName = obj["fileName"].toString();
    const QString visibleName = fileName.isEmpty() ? "未命名文件" : fileName;

    if (removed) {
        sendSystemNotice(socket, QString("文件发送已取消，服务端已清理未完成分片：%1。").arg(visibleName));
        qDebug() << "Canceled pending file transfer" << visibleName << transferId;
    } else {
        qDebug() << "File transfer cancel received after cleanup or completion" << visibleName << transferId;
    }
}

void Server::refreshRedisPresence(const ChatUser& user) {
    if (!m_redisClient || !m_redisClient->isEnabled()) return;
    m_redisClient->setPresence(user.id, user.name);
}

void Server::clearRedisPresence(const QString& userId) {
    if (!m_redisClient || !m_redisClient->isEnabled()) return;
    m_redisClient->clearPresence(userId);
}

bool Server::isRedisUserOnline(const QString& userId) const {
    if (!m_redisClient || !m_redisClient->isEnabled() || userId.isEmpty()) return false;
    return m_redisClient->hasPresence(userId);
}

bool Server::publishRedisMessageEvent(const Message& msg, const QString& deliveryState) {
    if (!m_redisClient || !m_redisClient->isEnabled()) return false;
    const bool isRedisFilePayload =
        (msg.type == MessageType::File || msg.type == MessageType::Image)
        && !msg.fileData.isEmpty()
        && msg.fileData.size() <= kRedisPubSubFileMaxBytes;
    if (msg.type != MessageType::Text && msg.type != MessageType::Private && !isRedisFilePayload) return false;

    const QJsonDocument messageDoc = QJsonDocument::fromJson(msg.toJson());
    if (!messageDoc.isObject()) return false;

    QJsonObject event;
    event["eventType"] = "chat_message";
    event["instanceId"] = m_instanceId;
    event["deliveryState"] = deliveryState;
    event["isPrivate"] = msg.isPrivate();
    event["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    event["message"] = messageDoc.object();

    return m_redisClient->publish("messages", QJsonDocument(event).toJson(QJsonDocument::Compact));
}

void Server::handleRedisMessageEvent(const QByteArray& payload) {
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) return;

    const QJsonObject event = doc.object();
    if (event["eventType"].toString() != "chat_message") return;
    if (event["instanceId"].toString() == m_instanceId) return;

    const QJsonObject messageObj = event["message"].toObject();
    if (messageObj.isEmpty()) return;

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
        QTcpSocket* targetSocket = m_userSockets.value(msg.receiverId);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            sendToUser(msg);
        } else {
            return;
        }
    }

    emit newMessage(msg);
}

ChatUser* Server::findUserBySocket(QTcpSocket* socket) {
    auto it = m_clients.find(socket);
    if (it != m_clients.end()) {
        return &it.value();
    }
    return nullptr;
}

QString Server::generateAccountId(const QJsonObject& accounts) const {
    for (int i = 0; i < 200; ++i) {
        int length = QRandomGenerator::global()->bounded(1, 10);
        int minValue = 1;
        for (int j = 1; j < length; ++j) {
            minValue *= 10;
        }
        int maxValue = minValue * 10;
        QString account = QString::number(QRandomGenerator::global()->bounded(minValue, maxValue));
        if (!accounts.contains(account)) return account;
    }
    return QString::number(QDateTime::currentMSecsSinceEpoch() % 1000000000).rightJustified(1, '1');
}

QJsonObject Server::loadAccountsFromSqlite() const {
    ensureAccountDatabase();

    QJsonObject accounts;
    QString connectionName = "accounts_read_" + QString::number(reinterpret_cast<quintptr>(this));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(accountDbPath());
        if (!db.open()) return accounts;

        QSqlQuery query(db);
        if (query.exec("SELECT account, password_hash, user_name FROM accounts")) {
            while (query.next()) {
                QJsonObject accountObj;
                accountObj["passwordHash"] = query.value(1).toString();
                accountObj["userName"] = query.value(2).toString();
                accountObj["userId"] = query.value(0).toString();
                accounts[query.value(0).toString()] = accountObj;
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    if (accounts.isEmpty()) {
        QJsonObject legacyAccounts = loadAccounts();
        for (auto it = legacyAccounts.begin(); it != legacyAccounts.end(); ++it) {
            QJsonObject accountObj = it.value().toObject();
            QString passwordHash = accountObj["passwordHash"].toString();
            if (passwordHash.isEmpty() && accountObj.contains("password")) {
                passwordHash = QString::fromLatin1(QCryptographicHash::hash((it.key() + ":" + accountObj["password"].toString()).toUtf8(), QCryptographicHash::Sha256).toHex());
            }
            QString userName = accountObj["userName"].toString(it.key());
            if (!passwordHash.isEmpty() && insertAccountToSqlite(it.key(), passwordHash, userName)) {
                QJsonObject migratedObj;
                migratedObj["passwordHash"] = passwordHash;
                migratedObj["userName"] = userName;
                migratedObj["userId"] = it.key();
                accounts[it.key()] = migratedObj;
            }
        }
    }

    return accounts;
}

bool Server::insertAccountToSqlite(const QString& account, const QString& passwordHash, const QString& userName) const {
    if (!ensureAccountDatabase()) return false;

    QString connectionName = "accounts_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(accountDbPath());
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare("INSERT OR REPLACE INTO accounts(account, password_hash, user_name, updated_at) VALUES(?, ?, ?, datetime('now'))");
            query.addBindValue(account);
            query.addBindValue(passwordHash);
            query.addBindValue(userName);
            ok = query.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool Server::recordUserSessionToSqlite(const ChatUser& user, const QString& eventName) const {
    if (!ensureAccountDatabase()) return false;

    QString connectionName = "sessions_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(accountDbPath());
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO user_sessions(user_id, user_name, event_name, peer_address, peer_port, created_at) "
                          "VALUES(?, ?, ?, ?, ?, datetime('now'))");
            query.addBindValue(user.id);
            query.addBindValue(user.name);
            query.addBindValue(eventName);
            query.addBindValue(user.address.toString());
            query.addBindValue(user.port);
            ok = query.exec();
            if (ok && eventName == "login" && !user.id.isEmpty()) {
                QSqlQuery accountQuery(db);
                accountQuery.prepare("UPDATE accounts SET "
                                     "user_name = ?, "
                                     "last_login_at = datetime('now'), "
                                     "last_login_address = ?, "
                                     "login_count = COALESCE(login_count, 0) + 1, "
                                     "updated_at = datetime('now') "
                                     "WHERE account = ?");
                accountQuery.addBindValue(user.name);
                accountQuery.addBindValue(user.address.toString());
                accountQuery.addBindValue(user.id);
                ok = accountQuery.exec();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool Server::recordDefaultGroupMembership(const ChatUser& user) const {
    if (user.id.isEmpty() || !ensureAccountDatabase()) return false;

    QString connectionName = "default_group_member_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(accountDbPath());
        if (db.open()) {
            QSqlQuery groupQuery(db);
            ok = groupQuery.exec("INSERT OR IGNORE INTO server_groups(group_id, group_name, announcement, created_at, updated_at) "
                                 "VALUES('public', '公共聊天室', '欢迎来到公共聊天室。', datetime('now'), datetime('now'))");
            if (ok) {
                QSqlQuery removedQuery(db);
                removedQuery.prepare("SELECT COUNT(*) FROM server_group_removed_members WHERE group_id = 'public' AND user_id = ?");
                removedQuery.addBindValue(user.id);
                ok = removedQuery.exec();
                bool wasRemoved = false;
                if (ok && removedQuery.next()) {
                    wasRemoved = removedQuery.value(0).toInt() > 0;
                }
                if (ok && !wasRemoved) {
                    QSqlQuery insertMemberQuery(db);
                    insertMemberQuery.prepare("INSERT OR IGNORE INTO server_group_members(group_id, user_id, user_name, role, joined_at, updated_at) "
                                              "VALUES('public', ?, ?, 'member', datetime('now'), datetime('now'))");
                    insertMemberQuery.addBindValue(user.id);
                    insertMemberQuery.addBindValue(user.name);
                    ok = insertMemberQuery.exec();
                }
            }
            if (ok) {
                QSqlQuery updateMemberQuery(db);
                updateMemberQuery.prepare("UPDATE server_group_members SET user_name = ?, updated_at = datetime('now') "
                                          "WHERE group_id = 'public' AND user_id = ?");
                updateMemberQuery.addBindValue(user.name);
                updateMemberQuery.addBindValue(user.id);
                ok = updateMemberQuery.exec();
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
                ownerId = user.id;
                QSqlQuery updateOwnerQuery(db);
                updateOwnerQuery.prepare("UPDATE server_groups SET owner_id = ?, updated_at = datetime('now') "
                                         "WHERE group_id = 'public'");
                updateOwnerQuery.addBindValue(ownerId);
                ok = updateOwnerQuery.exec();
            }
            if (ok) {
                QSqlQuery updateRoleQuery(db);
                updateRoleQuery.prepare("UPDATE server_group_members "
                                        "SET role = CASE WHEN user_id = ? THEN 'owner' "
                                        "WHEN role = 'owner' THEN 'member' ELSE role END, "
                                        "updated_at = datetime('now') "
                                        "WHERE group_id = 'public'");
                updateRoleQuery.addBindValue(ownerId);
                ok = updateRoleQuery.exec();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool Server::isServerGroupMember(const QString& groupId, const QString& userId) const {
    if (groupId.isEmpty() || userId.isEmpty() || !ensureAccountDatabase()) return false;

    const QString connectionName = "server_group_membership_check_"
        + QString::number(reinterpret_cast<quintptr>(this)) + "_"
        + QString::number(qHash(groupId + "|" + userId));
    bool exists = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(accountDbPath());
        if (db.open()) {
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
    QSqlDatabase::removeDatabase(connectionName);
    return exists;
}

bool Server::saveMessageToSqlite(const Message& msg, const QString& deliveryState) const {
    if (!ensureAccountDatabase()) return false;

    QString connectionName = "messages_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(accountDbPath());
        if (db.open()) {
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
    QSqlDatabase::removeDatabase(connectionName);
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
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(accountDbPath());
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO friend_events(event_type, sender_id, sender_name, receiver_id, query_account, accepted, event_state, created_at) "
                          "VALUES(?, ?, ?, ?, ?, ?, ?, datetime('now'))");
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
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool Server::ensureAccountDatabase() const {
    QString connectionName = "accounts_init_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(accountDbPath());
        if (db.open()) {
            QSqlQuery query(db);
            ok = query.exec("CREATE TABLE IF NOT EXISTS accounts ("
                            "account TEXT PRIMARY KEY, "
                            "password_hash TEXT NOT NULL, "
                            "user_name TEXT NOT NULL, "
                            "created_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                            "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            if (ok) {
                query.exec("ALTER TABLE accounts ADD COLUMN last_login_at TEXT");
                query.exec("ALTER TABLE accounts ADD COLUMN last_login_address TEXT");
                query.exec("ALTER TABLE accounts ADD COLUMN login_count INTEGER DEFAULT 0");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS user_sessions ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                "user_id TEXT NOT NULL, "
                                "user_name TEXT NOT NULL, "
                                "event_name TEXT NOT NULL, "
                                "peer_address TEXT, "
                                "peer_port INTEGER, "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS messages ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                "message_type INTEGER NOT NULL, "
                                "sender_id TEXT, "
                                "sender_name TEXT, "
                                "receiver_id TEXT, "
                                "content TEXT, "
                                "file_name TEXT, "
                                "file_size INTEGER DEFAULT 0, "
                                "file_hash TEXT, "
                                "file_chunk_size INTEGER DEFAULT 0, "
                                "file_chunk_count INTEGER DEFAULT 0, "
                                "delivery_state TEXT NOT NULL, "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            }
            if (ok) {
                query.exec("ALTER TABLE messages ADD COLUMN file_hash TEXT");
                query.exec("ALTER TABLE messages ADD COLUMN file_chunk_size INTEGER DEFAULT 0");
                query.exec("ALTER TABLE messages ADD COLUMN file_chunk_count INTEGER DEFAULT 0");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS offline_messages ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                "receiver_id TEXT NOT NULL, "
                                "payload TEXT NOT NULL, "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS friend_events ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                "event_type TEXT NOT NULL, "
                                "sender_id TEXT, "
                                "sender_name TEXT, "
                                "receiver_id TEXT, "
                                "query_account TEXT, "
                                "accepted INTEGER DEFAULT 0, "
                                "event_state TEXT NOT NULL, "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_groups ("
                                "group_id TEXT PRIMARY KEY, "
                                "group_name TEXT NOT NULL, "
                                "owner_id TEXT, "
                                "announcement TEXT, "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_group_members ("
                                "group_id TEXT NOT NULL, "
                                "user_id TEXT NOT NULL, "
                                "user_name TEXT, "
                                "role TEXT NOT NULL DEFAULT 'member', "
                                "joined_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "PRIMARY KEY(group_id, user_id))");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_group_removed_members ("
                                "group_id TEXT NOT NULL, "
                                "user_id TEXT NOT NULL, "
                                "removed_by TEXT, "
                                "removed_by_name TEXT, "
                                "removed_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "PRIMARY KEY(group_id, user_id))");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS server_group_announcements ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                "group_id TEXT NOT NULL, "
                                "author_id TEXT, "
                                "author_name TEXT, "
                                "content TEXT NOT NULL, "
                                "created_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            }
            if (ok) {
                query.exec("INSERT OR IGNORE INTO server_groups(group_id, group_name, announcement, created_at, updated_at) "
                           "VALUES('public', '公共聊天室', '欢迎来到公共聊天室。', datetime('now'), datetime('now'))");
            }
            if (ok) {
                query.exec("CREATE INDEX IF NOT EXISTS idx_messages_created_at ON messages(created_at)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_messages_receiver ON messages(receiver_id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_offline_receiver ON offline_messages(receiver_id, id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_friend_events_sender ON friend_events(sender_id, id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_friend_events_receiver ON friend_events(receiver_id, id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_members_user ON server_group_members(user_id, group_id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_removed_members_user ON server_group_removed_members(user_id, group_id)");
                query.exec("CREATE INDEX IF NOT EXISTS idx_server_group_announcements_group ON server_group_announcements(group_id, id)");
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

QString Server::accountDbPath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = ".";
    QDir().mkpath(dir);
    return dir + "/accounts.sqlite3";
}

QJsonObject Server::loadAccounts() const {
    QFile file(accountsFilePath());
    if (!file.open(QIODevice::ReadOnly)) return {};

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return {};
    return doc.object();
}

void Server::saveAccounts(const QJsonObject& accounts) const {
    QFile file(accountsFilePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(accounts).toJson(QJsonDocument::Indented));
}

QString Server::accountsFilePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = ".";
    QDir().mkpath(dir);
    return dir + "/accounts.json";
}

QString Server::offlineFilePath(const QString& userId) const {
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.isEmpty()) baseDir = ".";
    QString dir = baseDir + "/offline";
    QDir().mkpath(dir);
    return dir + "/" + userId + ".jsonl";
}

QString Server::offlineAttachmentRootDir() const {
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.isEmpty()) baseDir = ".";
    const QString dir = baseDir + "/offline_files";
    QDir().mkpath(dir);
    return dir;
}

QString Server::offlineAttachmentDir(const QString& userId) const {
    const QString dir = offlineAttachmentRootDir() + "/" + safePathPart(userId);
    QDir().mkpath(dir);
    return dir;
}

qint64 Server::offlineAttachmentQuotaBytes() const {
    const qint64 quotaMb = positiveIntegerEnvOrDefault("QTNETWORKCHAT_OFFLINE_ATTACHMENT_QUOTA_MB",
                                                       kDefaultOfflineAttachmentQuotaBytes / (1024 * 1024));
    return quotaMb * 1024 * 1024;
}

qint64 Server::offlineAttachmentUsedBytes() const {
    const QString rootDirPath = offlineAttachmentRootDir();
    qint64 usedBytes = 0;
    QDirIterator it(rootDirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        usedBytes += QFileInfo(it.next()).size();
    }
    return usedBytes;
}

bool Server::hasOfflineAttachmentCapacity(qint64 incomingBytes) const {
    if (incomingBytes <= 0) return false;

    const qint64 quotaBytes = offlineAttachmentQuotaBytes();
    return incomingBytes <= quotaBytes && offlineAttachmentUsedBytes() <= quotaBytes - incomingBytes;
}

QString Server::saveOfflineAttachment(const Message& msg) const {
    if (msg.receiverId.isEmpty() || msg.fileData.isEmpty()) return {};
    if (!hasOfflineAttachmentCapacity(msg.fileData.size())) {
        qWarning() << "Offline attachment quota exceeded for" << msg.receiverId
                   << "file:" << msg.fileName
                   << "size:" << msg.fileData.size()
                   << "quota:" << offlineAttachmentQuotaBytes();
        return {};
    }

    const QString dir = offlineAttachmentDir(msg.receiverId);
    for (int attempt = 0; attempt < 5; ++attempt) {
        const QString filePath = QString("%1/%2_%3.bin")
            .arg(dir,
                 QString::number(QDateTime::currentMSecsSinceEpoch()),
                 QString::number(QRandomGenerator::global()->generate()));
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) continue;

        const qint64 written = file.write(msg.fileData);
        file.close();
        if (written == msg.fileData.size()) {
            return filePath;
        }
        file.remove();
    }
    return {};
}

QSet<QString> Server::collectReferencedOfflineAttachments() const {
    QSet<QString> referencedPaths;
    auto collectPayload = [&referencedPaths](const QByteArray& payload) {
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject()) return;

        const QString filePath = doc.object()["offlineFilePath"].toString();
        if (!filePath.isEmpty()) {
            referencedPaths.insert(QDir::cleanPath(filePath));
        }
    };

    if (ensureAccountDatabase()) {
        QString connectionName = "offline_refs_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(accountDbPath());
            if (db.open()) {
                QSqlQuery query(db);
                if (query.exec("SELECT payload FROM offline_messages ORDER BY id ASC")) {
                    while (query.next()) {
                        collectPayload(query.value(0).toString().toUtf8());
                    }
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.isEmpty()) baseDir = ".";
    QDir offlineDir(baseDir + "/offline");
    const QFileInfoList offlineFiles = offlineDir.entryInfoList(QStringList() << "*.jsonl", QDir::Files);
    for (const QFileInfo& info : offlineFiles) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        while (!file.atEnd()) {
            collectPayload(file.readLine().trimmed());
        }
    }

    return referencedPaths;
}

void Server::cleanupExpiredOfflineAttachments() {
    const QString rootDirPath = offlineAttachmentRootDir();
    QDir rootDir(rootDirPath);
    if (!rootDir.exists()) return;

    const QSet<QString> referencedPaths = collectReferencedOfflineAttachments();
    const QDateTime now = QDateTime::currentDateTime();
    QStringList visitedDirs;
    QDirIterator it(rootDirPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (info.isDir()) {
            visitedDirs.append(path);
            continue;
        }

        const QString cleanPath = QDir::cleanPath(path);
        const bool isExpired = info.lastModified().msecsTo(now) > offlineAttachmentTtlMs();
        const bool isOrphaned = !referencedPaths.contains(cleanPath);
        if ((isExpired || isOrphaned) && QFile::remove(path)) {
            qDebug() << "Cleaned offline attachment" << cleanPath
                     << (isExpired ? "expired" : "orphaned");
        }
    }

    std::sort(visitedDirs.begin(), visitedDirs.end(), [](const QString& left, const QString& right) {
        return left.count(QLatin1Char('/')) > right.count(QLatin1Char('/'));
    });
    for (const QString& dirPath : visitedDirs) {
        const QFileInfo info(dirPath);
        QDir parentDir(info.absolutePath());
        parentDir.rmdir(info.fileName());
    }
}

void Server::saveOfflineMessage(const Message& msg) const {
    QJsonObject obj;
    obj["type"] = msg.type == MessageType::File || msg.type == MessageType::Image ? "file" : "private";
    obj["messageType"] = static_cast<int>(msg.type);
    obj["senderId"] = msg.senderId;
    obj["senderName"] = msg.senderName;
    obj["receiverId"] = msg.receiverId;
    obj["content"] = msg.content;
    obj["fileName"] = msg.fileName;
    obj["fileSize"] = QString::number(msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
    obj["fileHash"] = msg.fileHash;
    obj["chunkSize"] = QString::number(msg.chunkSize);
    obj["chunkCount"] = QString::number(msg.chunkCount);
    QString savedAttachmentPath;
    if (!msg.fileData.isEmpty()) {
        const bool shouldStoreAsAttachment = msg.type == MessageType::File || msg.type == MessageType::Image;
        const QString attachmentPath = shouldStoreAsAttachment ? saveOfflineAttachment(msg) : QString();
        if (shouldStoreAsAttachment && attachmentPath.isEmpty()) {
            qWarning() << "Offline file was not queued because attachment storage failed" << msg.receiverId << msg.fileName;
            return;
        } else if (!attachmentPath.isEmpty()) {
            savedAttachmentPath = attachmentPath;
            obj["offlineFilePath"] = attachmentPath;
            obj["offlineFileStoredOnDisk"] = true;
        } else {
            obj["fileData"] = QString::fromLatin1(msg.fileData.toBase64());
        }
    }
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    auto rollbackSavedAttachment = [&savedAttachmentPath]() {
        if (!savedAttachmentPath.isEmpty() && QFile::remove(savedAttachmentPath)) {
            qWarning() << "Rolled back offline attachment after queue persistence failure" << savedAttachmentPath;
        }
    };

    bool savedToSqlite = false;
    if (ensureAccountDatabase()) {
        QString connectionName = "offline_write_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(accountDbPath());
            if (db.open()) {
                QSqlQuery query(db);
                query.prepare("INSERT INTO offline_messages(receiver_id, payload, created_at) VALUES(?, ?, datetime('now'))");
                query.addBindValue(msg.receiverId);
                query.addBindValue(QString::fromUtf8(payload));
                savedToSqlite = query.exec();
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    }
    if (savedToSqlite) return;

    QFile file(offlineFilePath(msg.receiverId));
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        rollbackSavedAttachment();
        return;
    }

    const bool savedToJsonl = file.write(payload) == payload.size()
        && file.write("\n") == 1;
    file.close();
    if (!savedToJsonl) {
        rollbackSavedAttachment();
    }
}

bool Server::updateOfflineMessageProgress(qint64 sqliteMessageId, const QJsonObject& obj, qint64 confirmedBytes, qint64 confirmedChunkIndex) const {
    if (sqliteMessageId <= 0 || confirmedBytes <= 0 || !ensureAccountDatabase()) {
        return false;
    }

    bool saved = false;
    const QString connectionName = "offline_progress_" + QString::number(QCoreApplication::applicationPid())
        + "_" + QString::number(QRandomGenerator::global()->generate());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(accountDbPath());
        if (db.open()) {
            QJsonObject updated = obj;
            QSqlQuery selectQuery(db);
            selectQuery.prepare("SELECT payload FROM offline_messages WHERE id = ?");
            selectQuery.addBindValue(sqliteMessageId);
            if (selectQuery.exec() && selectQuery.next()) {
                const QJsonDocument currentDoc = QJsonDocument::fromJson(selectQuery.value(0).toString().toUtf8());
                if (currentDoc.isObject()) {
                    updated = currentDoc.object();
                }
            }

            const qint64 existingConfirmedBytes = updated["confirmedBytes"].toVariant().toLongLong();
            updated["confirmedBytes"] = QString::number(qMax(existingConfirmedBytes, confirmedBytes));
            QJsonArray confirmedChunks = updated["confirmedChunks"].toArray();
            bool alreadyRecorded = false;
            for (const QJsonValue& value : confirmedChunks) {
                if (value.toVariant().toLongLong() == confirmedChunkIndex) {
                    alreadyRecorded = true;
                    break;
                }
            }
            if (confirmedChunkIndex >= 0 && !alreadyRecorded) {
                confirmedChunks.append(QString::number(confirmedChunkIndex));
            }
            updated["confirmedChunks"] = confirmedChunks;
            updated["resumeUpdatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

            QSqlQuery query(db);
            query.prepare("UPDATE offline_messages SET payload = ? WHERE id = ?");
            query.addBindValue(QString::fromUtf8(QJsonDocument(updated).toJson(QJsonDocument::Compact)));
            query.addBindValue(sqliteMessageId);
            saved = query.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return saved;
}

bool Server::deliverOfflinePayload(const QByteArray& payload, QTcpSocket* socket, qint64 sqliteMessageId) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState || payload.isEmpty()) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        const bool written = socket->write(payload) > 0 && socket->write("\n") > 0;
        socket->flush();
        return written;
    }

    const QJsonObject obj = doc.object();
    const QString offlineFilePath = obj["offlineFilePath"].toString();
    const int messageType = obj["messageType"].toInt(static_cast<int>(MessageType::File));
    const bool isFileMessage = messageType == static_cast<int>(MessageType::File)
        || messageType == static_cast<int>(MessageType::Image);
    if (!isFileMessage || offlineFilePath.isEmpty()) {
        const qint64 written = socket->write(payload);
        socket->write("\n");
        socket->flush();
        return written > 0;
    }

    const OfflineAttachmentValidationResult validation = validateOfflineAttachmentForReplay(obj, offlineFilePath, socket);
    if (validation == OfflineAttachmentValidationResult::CleanedBadState) {
        return true;
    }
    if (validation == OfflineAttachmentValidationResult::Blocked) {
        return false;
    }

    const bool delivered = sendOfflineAttachmentToSocket(obj, offlineFilePath, socket, sqliteMessageId);
    if (delivered) {
        QFile::remove(offlineFilePath);
    }
    return delivered;
}

bool Server::sendOfflineAttachmentToSocket(const QJsonObject& obj, const QString& filePath, QTcpSocket* socket, qint64 sqliteMessageId) {
    QPointer<QTcpSocket> socketGuard(socket);
    if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const qint64 totalBytes = QFileInfo(file).size();
    const qint64 declaredChunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 chunkSize = (declaredChunkSize > 0 && declaredChunkSize <= kForwardChunkBytes)
        ? declaredChunkSize
        : kForwardChunkBytes;
    const qint64 chunkCount = (totalBytes + chunkSize - 1) / chunkSize;
    const QString fileName = obj["fileName"].toString();
    const OfflineAttachmentReplayPlan replayPlan = buildOfflineAttachmentReplayPlan(obj,
                                                                                   totalBytes,
                                                                                   chunkSize,
                                                                                   chunkCount,
                                                                                   sqliteMessageId);
    if (replayPlan.startChunkIndex > 0
        && replayPlan.startChunkIndex < chunkCount
        && !file.seek(replayPlan.startChunkIndex * chunkSize)) {
        qWarning() << "Offline attachment resume seek failed:" << fileName << replayPlan.startChunkIndex << "/" << chunkCount;
        return false;
    }
    const QString transferId = QString("%1_%2_%3")
        .arg(obj["senderId"].toString(),
             QString::number(QDateTime::currentMSecsSinceEpoch()),
             QString::number(QRandomGenerator::global()->generate()));

    for (qint64 index = replayPlan.startChunkIndex; index < chunkCount; ++index) {
        if (replayPlan.confirmedChunksValid && replayPlan.confirmedChunkIndexes.contains(index)) {
            continue;
        }
        if (!file.seek(index * chunkSize)) {
            qWarning() << "Offline attachment chunk seek failed:" << fileName << index + 1 << "/" << chunkCount;
            return false;
        }
        const QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty() || (index < chunkCount - 1 && chunk.size() != chunkSize)) {
            qWarning() << "Offline attachment chunk read failed:" << fileName << index + 1 << "/" << chunkCount;
            return false;
        }

        QJsonObject chunkObj;
        chunkObj["type"] = "file_chunk";
        chunkObj["transferId"] = transferId;
        chunkObj["messageType"] = obj["messageType"].toInt(static_cast<int>(MessageType::File));
        chunkObj["senderId"] = obj["senderId"].toString();
        chunkObj["senderName"] = obj["senderName"].toString();
        chunkObj["receiverId"] = obj["receiverId"].toString();
        chunkObj["content"] = obj["content"].toString();
        chunkObj["fileName"] = fileName;
        chunkObj["fileSize"] = QString::number(totalBytes);
        chunkObj["fileHash"] = obj["fileHash"].toString();
        chunkObj["chunkSize"] = QString::number(chunkSize);
        chunkObj["chunkCount"] = QString::number(chunkCount);
        chunkObj["chunkIndex"] = QString::number(index);
        chunkObj["fileData"] = QString::fromLatin1(chunk.toBase64());

        QString ackRejectReason;
        qint64 ackReceivedBytes = 0;
        bool acknowledged = false;
        const QByteArray data = QJsonDocument(chunkObj).toJson(QJsonDocument::Compact);
        for (int attempt = 1; attempt <= kChunkSendMaxAttempts; ++attempt) {
            if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState || socketGuard->write(data) <= 0) {
                return false;
            }
            socketGuard->write("\n");
            socketGuard->flush();
            if (waitForFileChunkAck(socketGuard, transferId, index, &ackRejectReason, &ackReceivedBytes)) {
                const qint64 expectedAckBytes = qMin(totalBytes, index * chunkSize + chunk.size());
                if (ackReceivedBytes > 0 && (ackReceivedBytes < expectedAckBytes || ackReceivedBytes > totalBytes)) {
                    if (attempt == kChunkSendMaxAttempts) {
                        ackRejectReason = QString::fromUtf8("离线附件确认进度非法");
                    }
                    continue;
                }
                acknowledged = true;
                break;
            }
            if (!socketGuard) {
                return false;
            }
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "Offline attachment chunk rejected by receiver:" << ackRejectReason;
                return false;
            }
        }
        if (!acknowledged) {
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "Offline attachment chunk rejected by receiver:" << ackRejectReason;
                return false;
            }
            qWarning() << "Offline attachment chunk ack timeout:" << fileName << index + 1 << "/" << chunkCount;
            return false;
        }
        const qint64 fallbackConfirmedBytes = qMin(totalBytes, (index + 1) * chunkSize);
        const qint64 confirmedBytes = qBound<qint64>(0, ackReceivedBytes > 0 ? ackReceivedBytes : fallbackConfirmedBytes, totalBytes);
        if (confirmedBytes > 0) {
            updateOfflineMessageProgress(sqliteMessageId, obj, confirmedBytes, index);
        }
    }

    return true;
}

void Server::sendOfflineMessages(const QString& userId, QTcpSocket* socket) {
    if (ensureAccountDatabase()) {
        QString connectionName = "offline_read_" + QString::number(reinterpret_cast<quintptr>(this));
        QVector<qint64> deliveredIds;
        QVector<QPair<qint64, QByteArray>> pendingRows;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(accountDbPath());
            if (db.open()) {
                QSqlQuery query(db);
                query.prepare("SELECT id, payload FROM offline_messages WHERE receiver_id = ? ORDER BY id ASC");
                query.addBindValue(userId);
                if (query.exec()) {
                    while (query.next()) {
                        const qint64 messageId = query.value(0).toLongLong();
                        QByteArray line = query.value(1).toString().toUtf8();
                        if (line.isEmpty()) continue;
                        pendingRows.append(qMakePair(messageId, line));
                    }
                }
                for (const auto& row : pendingRows) {
                    if (!deliverOfflinePayload(row.second, socket, row.first)) {
                        break;
                    }
                    deliveredIds.append(row.first);
                }
                if (!deliveredIds.isEmpty()) {
                    for (qint64 messageId : deliveredIds) {
                        QSqlQuery deleteQuery(db);
                        deleteQuery.prepare("DELETE FROM offline_messages WHERE id = ?");
                        deleteQuery.addBindValue(messageId);
                        deleteQuery.exec();
                    }
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    QFile file(offlineFilePath(userId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QVector<QByteArray> remainingLines;
    bool deliveryBlocked = false;
    while (!file.atEnd()) {
        QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) continue;
        if (deliveryBlocked || !deliverOfflinePayload(line, socket)) {
            deliveryBlocked = true;
            remainingLines.append(line);
        }
    }
    file.close();
    if (remainingLines.isEmpty()) {
        file.remove();
        return;
    }
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        for (const QByteArray& line : remainingLines) {
            file.write(line);
            file.write("\n");
        }
    }
}

bool Server::sendChunkedFileToSocket(const Message& msg, QTcpSocket* socket) {
    QPointer<QTcpSocket> socketGuard(socket);
    if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState || msg.fileData.isEmpty()) {
        return false;
    }

    const qint64 totalBytes = msg.fileData.size();
    const qint64 chunkSize = (msg.chunkSize > 0 && msg.chunkSize <= kForwardChunkBytes)
        ? msg.chunkSize
        : kForwardChunkBytes;
    const qint64 chunkCount = (totalBytes + chunkSize - 1) / chunkSize;
    const QString transferId = QString("%1_%2_%3")
        .arg(msg.senderId,
             QString::number(QDateTime::currentMSecsSinceEpoch()),
             QString::number(QRandomGenerator::global()->generate()));

    for (qint64 index = 0; index < chunkCount; ++index) {
        const qint64 offset = index * chunkSize;
        const QByteArray chunk = msg.fileData.mid(static_cast<int>(offset), static_cast<int>(qMin(chunkSize, totalBytes - offset)));

        QJsonObject obj;
        obj["type"] = "file_chunk";
        obj["transferId"] = transferId;
        obj["messageType"] = static_cast<int>(msg.type);
        obj["senderId"] = msg.senderId;
        obj["senderName"] = msg.senderName;
        obj["receiverId"] = msg.receiverId;
        obj["content"] = msg.content;
        obj["fileName"] = msg.fileName;
        obj["fileSize"] = QString::number(totalBytes);
        obj["fileHash"] = msg.fileHash;
        obj["chunkSize"] = QString::number(chunkSize);
        obj["chunkCount"] = QString::number(chunkCount);
        obj["chunkIndex"] = QString::number(index);
        obj["fileData"] = QString::fromLatin1(chunk.toBase64());

        QString ackRejectReason;
        bool acknowledged = false;
        const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        for (int attempt = 1; attempt <= kChunkSendMaxAttempts; ++attempt) {
            if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState || socketGuard->write(data) <= 0) {
                return false;
            }
            socketGuard->write("\n");
            socketGuard->flush();
            if (waitForFileChunkAck(socketGuard, transferId, index, &ackRejectReason)) {
                acknowledged = true;
                break;
            }
            if (!socketGuard) {
                return false;
            }
            if (!ackRejectReason.isEmpty()) {
                qWarning() << "File chunk rejected by receiver:" << ackRejectReason;
                return false;
            }
        }
        if (!acknowledged) {
            qWarning() << "File chunk ack timeout:" << msg.fileName << index + 1 << "/" << chunkCount;
            return false;
        }
    }
    return true;
}

bool Server::waitForFileChunkAck(QTcpSocket* socket, const QString& transferId, qint64 chunkIndex, QString* rejectReason, qint64* receivedBytes) {
    if (rejectReason) rejectReason->clear();
    if (receivedBytes) *receivedBytes = 0;
    QPointer<QTcpSocket> socketGuard(socket);
    if (!socketGuard || transferId.isEmpty()) return false;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    bool matched = false;
    bool accepted = false;
    QString reason;
    qint64 ackReceivedBytes = 0;

    QMetaObject::Connection ackConnection = connect(
        this,
        &Server::fileChunkAckReceived,
        &loop,
        [&](QTcpSocket* ackSocket, const QString& ackTransferId, qint64 ackChunkIndex, bool ackAccepted, const QString& ackReason, qint64 ackBytes) {
            if (!socketGuard || ackSocket != socketGuard.data() || ackTransferId != transferId || ackChunkIndex != chunkIndex) return;
            matched = true;
            accepted = ackAccepted;
            reason = ackReason;
            ackReceivedBytes = ackBytes;
            loop.quit();
        });
    QMetaObject::Connection disconnectedConnection = connect(socketGuard, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QMetaObject::Connection destroyedConnection = connect(socketGuard, &QObject::destroyed, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(kChunkAckTimeoutMs);
    loop.exec();

    QObject::disconnect(ackConnection);
    QObject::disconnect(disconnectedConnection);
    QObject::disconnect(destroyedConnection);

    if (matched && !accepted && rejectReason) {
        *rejectReason = reason.isEmpty() ? "客户端拒绝分片" : reason;
    }
    if (matched && receivedBytes) {
        *receivedBytes = ackReceivedBytes;
    }
    return matched && accepted;
}

void Server::cleanupExpiredFileTransfers() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QString& key : m_pendingFileTransfers.keys()) {
        const auto it = m_pendingFileTransfers.constFind(key);
        if (it == m_pendingFileTransfers.constEnd()) {
            continue;
        }
        const PendingFileTransfer& pending = it.value();
        if (pending.lastActivityMs <= 0 || now - pending.lastActivityMs <= kTransferStaleTimeoutMs) {
            continue;
        }

        const QString visibleName = pending.fileName.isEmpty() ? "未命名文件" : pending.fileName;
        QTcpSocket* socket = pending.socket;
        const int receivedCount = pending.receivedIndexes.size();
        const qint64 chunkCount = pending.chunkCount;
        m_pendingFileTransfers.remove(key);
        if (socket && socket->state() == QAbstractSocket::ConnectedState) {
            sendSystemNotice(socket, QString("文件分片上传已超时清理：%1。请重新发送。").arg(visibleName));
        }
        qWarning() << "Cleaned expired incoming file transfer" << visibleName << receivedCount << "/" << chunkCount;
    }
}

void Server::broadcastMessage(const Message& msg, QTcpSocket* excludeSocket) {
    QJsonObject obj;
    obj["type"] = msg.type == MessageType::System ? "system" : ((msg.type == MessageType::File || msg.type == MessageType::Image) ? "file" : (msg.isPrivate() ? "private" : "message"));
    obj["messageType"] = static_cast<int>(msg.type);
    obj["senderId"] = msg.senderId;
    obj["senderName"] = msg.senderName;
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
            sendChunkedFileToSocket(msg, targetSocket);
            return;
        }

        QJsonObject obj;
        obj["type"] = msg.type == MessageType::File || msg.type == MessageType::Image ? "file" : "private";
        obj["messageType"] = static_cast<int>(msg.type);
        obj["senderId"] = msg.senderId;
        obj["senderName"] = msg.senderName;
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
        QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        targetSocket->write(data);
        targetSocket->write("\n");
        targetSocket->flush();
    } else {
        saveOfflineMessage(msg);
    }
}

void Server::sendUserList(QTcpSocket* socket) {
    QJsonObject obj;
    obj["type"] = "userlist";

    QJsonArray users;
    QSet<QString> appendedUserIds;
    auto appendOnlineUser = [&](const QString& userId, const QString& userName) {
        if (userId.isEmpty() || appendedUserIds.contains(userId)) return;
        QJsonObject u;
        u["id"] = userId;
        u["name"] = userName.isEmpty() ? userId : userName;
        u["online"] = true;
        users.append(u);
        appendedUserIds.insert(userId);
    };

    for (const ChatUser& user : m_clients.values()) {
        if (user.isOnline) {
            appendOnlineUser(user.id, user.name);
        }
    }

    if (m_redisClient && m_redisClient->isEnabled()) {
        QList<RedisClient::Presence> redisUsers;
        if (m_redisClient->fetchOnlinePresence(&redisUsers)) {
            for (const RedisClient::Presence& user : redisUsers) {
                appendOnlineUser(user.userId, user.userName);
            }
        }
    }
    obj["users"] = users;

    socket->write(QJsonDocument(obj).toJson());
    socket->write("\n");
    socket->flush();
}

void Server::sendServerGroupSnapshot(const QString& userId, QTcpSocket* socket) const {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState || userId.isEmpty() || !ensureAccountDatabase()) {
        return;
    }

    QJsonArray groups;
    QString connectionName = "server_group_snapshot_" + QString::number(reinterpret_cast<quintptr>(this));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(accountDbPath());
        if (db.open()) {
            QSqlQuery groupQuery(db);
            groupQuery.prepare("SELECT g.group_id, g.group_name, COALESCE(g.announcement, ''), COALESCE(g.owner_id, '') "
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

                    QJsonArray members;
                    QSqlQuery memberQuery(db);
                    memberQuery.prepare("SELECT user_id, COALESCE(user_name, ''), role "
                                        "FROM server_group_members "
                                        "WHERE group_id = ? "
                                        "ORDER BY role = 'owner' DESC, joined_at ASC, user_id ASC");
                    memberQuery.addBindValue(groupId);
                    if (memberQuery.exec()) {
                        while (memberQuery.next()) {
                            QJsonObject memberObj;
                            memberObj["userId"] = memberQuery.value(0).toString();
                            memberObj["userName"] = memberQuery.value(1).toString();
                            memberObj["role"] = memberQuery.value(2).toString();
                            members.append(memberObj);
                        }
                    }
                    groupObj["members"] = members;
                    groups.append(groupObj);
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);

    QJsonObject obj;
    obj["type"] = "server_group_snapshot";
    obj["groups"] = groups;
    socket->write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}
