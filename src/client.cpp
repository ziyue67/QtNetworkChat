#include "client.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDataStream>
#include <QFileInfo>
#include <QEventLoop>
#include <QTimer>
#include <QSslSocket>
#include <QSslError>
#include <QCryptographicHash>
#include <QDateTime>
#include <QRandomGenerator>

namespace {
constexpr qint64 kMaxOutgoingPayloadBytes = 80LL * 1024 * 1024;
constexpr qint64 kTransferChunkBytes = 256LL * 1024;
constexpr qint64 kMaxIncomingChunks = 4096;
constexpr int kChunkAckTimeoutMs = 4000;
constexpr int kChunkSendMaxAttempts = 3;
constexpr qint64 kTransferStaleTimeoutMs = 2LL * 60 * 1000;
constexpr int kTransferCleanupIntervalMs = 30 * 1000;

bool envEnabled(const char* name) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

QTcpSocket* createClientSocket(QObject* parent) {
    if (envEnabled("QTNETWORKCHAT_TLS") && QSslSocket::supportsSsl()) {
        QSslSocket* socket = new QSslSocket(parent);
        socket->setPeerVerifyMode(envEnabled("QTNETWORKCHAT_TLS_VERIFY")
            ? QSslSocket::VerifyPeer
            : QSslSocket::VerifyNone);
        return socket;
    }
    return new QTcpSocket(parent);
}
}

Client::Client(QObject* parent)
    : QObject(parent)
    , m_socket(createClientSocket(this))
    , m_heartbeatTimer(new QTimer(this))
    , m_transferCleanupTimer(new QTimer(this))
    , m_registerMode(false)
    , m_loginFinished(false)
    , m_loginOk(false)
    , m_loginWasRegister(false)
    , m_reconnectAttempts(0)
{
    connect(m_socket, &QTcpSocket::readyRead, this, &Client::onReadyRead);
    if (QSslSocket* sslSocket = qobject_cast<QSslSocket*>(m_socket)) {
        connect(sslSocket, &QSslSocket::encrypted, this, &Client::onConnected);
        connect(sslSocket, QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
                this, [sslSocket](const QList<QSslError>&) {
                    if (!envEnabled("QTNETWORKCHAT_TLS_VERIFY")) {
                        sslSocket->ignoreSslErrors();
                    }
                });
    } else {
        connect(m_socket, &QTcpSocket::connected, this, &Client::onConnected);
    }
    connect(m_socket, &QTcpSocket::disconnected, this, &Client::onDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_socket, &QTcpSocket::errorOccurred, this, &Client::onError);
#else
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &Client::onError);
#endif

    connect(m_heartbeatTimer, &QTimer::timeout, this, &Client::onHeartbeat);
    connect(m_transferCleanupTimer, &QTimer::timeout, this, &Client::cleanupExpiredIncomingFileTransfers);
}

Client::~Client() {
    m_heartbeatTimer->stop();
    m_transferCleanupTimer->stop();
    if (m_socket->isOpen()) {
        m_socket->disconnectFromHost();
    }
}

bool Client::connectToServer(const QString& host, quint16 port) {
    m_serverHost = host;
    m_serverPort = port;
    if (QSslSocket* sslSocket = qobject_cast<QSslSocket*>(m_socket)) {
        sslSocket->connectToHostEncrypted(host, port);
        const bool encrypted = sslSocket->waitForEncrypted(5000);
        if (!encrypted) {
            m_loginError = "TLS 握手失败: " + sslSocket->errorString();
        }
        return encrypted;
    }
    m_socket->connectToHost(host, port);
    return m_socket->waitForConnected(5000);
}

void Client::disconnectFromServer() {
    m_heartbeatTimer->stop();
    if (m_socket->isOpen()) {
        m_socket->disconnectFromHost();
    }
}

void Client::setUserInfo(const QString& userId, const QString& userName) {
    m_userId = userId;
    m_userName = userName;
}

void Client::setAccountInfo(const QString& account, const QString& password, bool registerMode) {
    m_account = account;
    m_password = password;
    m_registerMode = registerMode;
    m_loginFinished = false;
    m_loginOk = false;
    m_loginWasRegister = false;
    m_loginError.clear();
}

bool Client::waitForLoginResult(int timeoutMs) {
    if (m_loginFinished) return m_loginOk;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(this, &Client::loginSucceeded, &loop, &QEventLoop::quit);
    connect(this, &Client::loginFailed, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    if (!m_loginFinished) {
        m_loginError = "登录超时";
        return false;
    }
    return m_loginOk;
}

QString Client::transportSecurityDescription() const {
    if (const QSslSocket* sslSocket = qobject_cast<const QSslSocket*>(m_socket)) {
        return sslSocket->isEncrypted()
            ? "TLS 加密通道"
            : "TLS 已启用，等待握手";
    }
    if (envEnabled("QTNETWORKCHAT_TLS") && !QSslSocket::supportsSsl()) {
        return "TLS 已请求，但当前 Qt/OpenSSL 不可用，已回退 TCP";
    }
    return "普通 TCP 通道";
}

bool Client::sendMessage(const QString& content) {
    if (!isConnected()) return false;

    QJsonObject obj;
    obj["type"] = "message";
    obj["messageType"] = static_cast<int>(MessageType::Text);
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["content"] = content;

    return sendJson(obj);
}

bool Client::sendPrivateMessage(const QString& receiverId, const QString& content) {
    if (!isConnected()) return false;

    QJsonObject obj;
    obj["type"] = "private";
    obj["messageType"] = static_cast<int>(MessageType::Private);
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["receiverId"] = receiverId;
    obj["content"] = content;

    return sendJson(obj);
}

bool Client::sendFriendRequest(const QString& receiverId) {
    if (!isConnected() || receiverId.isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "friend_request";
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["receiverId"] = receiverId;
    return sendJson(obj);
}

bool Client::searchFriendByAccount(const QString& account) {
    if (!isConnected() || account.trimmed().isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "friend_search";
    obj["account"] = account.trimmed();
    return sendJson(obj);
}

bool Client::sendFriendResponse(const QString& receiverId, bool accepted) {
    if (!isConnected() || receiverId.isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "friend_response";
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["receiverId"] = receiverId;
    obj["accepted"] = accepted;
    return sendJson(obj);
}

bool Client::sendFile(const QString& filePath, const QString& receiverId) {
    return sendFilePayload(filePath, receiverId, MessageType::File, "发送了文件: ");
}

bool Client::sendImage(const QString& filePath, const QString& receiverId) {
    return sendFilePayload(filePath, receiverId, MessageType::Image, "发送了图片: ");
}

bool Client::sendFilePayload(const QString& filePath, const QString& receiverId, MessageType messageType, const QString& contentPrefix) {
    if (!isConnected()) return false;

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile() || fileInfo.size() <= 0 || fileInfo.size() > kMaxOutgoingPayloadBytes) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    qint64 preparedBytes = 0;
    emit fileTransferProgress(fileInfo.fileName(), 0, fileInfo.size());

    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kTransferChunkBytes);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            file.close();
            return false;
        }
        hasher.addData(chunk);
        preparedBytes += chunk.size();
        emit fileTransferProgress(fileInfo.fileName(), preparedBytes, fileInfo.size());
    }
    file.close();

    const qint64 chunkCount = (fileInfo.size() + kTransferChunkBytes - 1) / kTransferChunkBytes;
    const QString fileHash = QString::fromLatin1(hasher.result().toHex());
    emit fileTransferPrepared(fileInfo.fileName(), fileInfo.size(), kTransferChunkBytes, chunkCount, fileHash);

    if (!file.open(QIODevice::ReadOnly)) return false;
    const QString transferId = QString("%1_%2_%3")
        .arg(m_userId,
             QString::number(QDateTime::currentMSecsSinceEpoch()),
             QString::number(QRandomGenerator::global()->generate()));
    qint64 sentBytes = 0;
    qint64 chunkIndex = 0;
    emit fileTransferProgress(fileInfo.fileName(), 0, fileInfo.size());

    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kTransferChunkBytes);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            file.close();
            return false;
        }

        QJsonObject obj;
        obj["type"] = "file_chunk";
        obj["transferId"] = transferId;
        obj["senderId"] = m_userId;
        obj["senderName"] = m_userName;
        obj["receiverId"] = receiverId;
        obj["messageType"] = static_cast<int>(messageType);
        obj["fileName"] = fileInfo.fileName();
        obj["fileSize"] = QString::number(fileInfo.size());
        obj["fileHash"] = fileHash;
        obj["chunkSize"] = QString::number(kTransferChunkBytes);
        obj["chunkCount"] = QString::number(chunkCount);
        obj["chunkIndex"] = QString::number(chunkIndex);
        obj["content"] = contentPrefix + fileInfo.fileName();
        obj["fileData"] = QString::fromLatin1(chunk.toBase64());

        QString ackRejectReason;
        bool acknowledged = false;
        for (int attempt = 1; attempt <= kChunkSendMaxAttempts; ++attempt) {
            if (!sendJson(obj)) {
                file.close();
                return false;
            }
            if (waitForFileChunkAck(transferId, chunkIndex, &ackRejectReason)) {
                acknowledged = true;
                break;
            }
            if (!ackRejectReason.isEmpty()) {
                emit connectionError(QString("文件分片发送被拒绝：%1").arg(ackRejectReason));
                file.close();
                return false;
            }
        }
        if (!acknowledged) {
            emit connectionError(QString("文件分片发送超时：%1 第 %2/%3 片").arg(fileInfo.fileName()).arg(chunkIndex + 1).arg(chunkCount));
            file.close();
            return false;
        }
        sentBytes += chunk.size();
        ++chunkIndex;
        emit fileTransferProgress(fileInfo.fileName(), sentBytes, fileInfo.size());
    }
    file.close();
    return sentBytes == fileInfo.size() && chunkIndex == chunkCount;
}

void Client::onReadyRead() {
    m_buffer.append(m_socket->readAll());

    while (m_buffer.contains('\n')) {
        int newlineIndex = m_buffer.indexOf('\n');
        QByteArray line = m_buffer.left(newlineIndex);
        m_buffer = m_buffer.mid(newlineIndex + 1);

        if (line.isEmpty()) continue;

        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isNull() || !doc.isObject()) continue;

        handleServerMessage(doc.object());
    }
}

void Client::onConnected() {
    qDebug() << "Connected to server";
    m_reconnectAttempts = 0;
    sendLogin();
    m_heartbeatTimer->start(30000);
    m_transferCleanupTimer->start(kTransferCleanupIntervalMs);
    emit connected();
}

void Client::onDisconnected() {
    m_heartbeatTimer->stop();
    m_transferCleanupTimer->stop();
    m_incomingFileTransfers.clear();
    qDebug() << "Disconnected from server";
    emit disconnected();
}

void Client::onError(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError)
    QString errorMsg = m_socket->errorString();
    qWarning() << "Socket error:" << errorMsg;
    emit connectionError(errorMsg);
}

void Client::onHeartbeat() {
    QJsonObject obj;
    obj["type"] = "heartbeat";
    sendJson(obj);
}

void Client::sendLogin() {
    QJsonObject obj;
    obj["type"] = "login";
    obj["mode"] = m_registerMode ? "register" : "login";
    obj["account"] = m_account;
    obj["password"] = m_password;
    obj["userName"] = m_userName;
    sendJson(obj);
}

bool Client::sendJson(const QJsonObject& obj) {
    if (!isConnected()) return false;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    qint64 written = m_socket->write(data);
    m_socket->write("\n");
    m_socket->flush();
    return written > 0;
}

bool Client::sendFileChunkAck(const QString& transferId,
                              qint64 chunkIndex,
                              bool accepted,
                              const QString& reason,
                              qint64 receivedBytes) {
    if (transferId.isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "file_chunk_ack";
    obj["transferId"] = transferId;
    obj["chunkIndex"] = QString::number(chunkIndex);
    obj["accepted"] = accepted;
    obj["reason"] = reason;
    obj["receivedBytes"] = QString::number(receivedBytes);
    return sendJson(obj);
}

void Client::handleServerMessage(const QJsonObject& obj) {
    QString type = obj["type"].toString();
    qDebug() << "Server message type:" << type;

    if (type == "login_success") {
        m_userId = obj["userId"].toString();
        m_userName = obj["userName"].toString();
        m_loginFinished = true;
        m_loginOk = true;
        m_loginWasRegister = obj["registered"].toBool(false);
        qDebug() << "Login success, userId:" << m_userId;
        emit loginSucceeded();
        return;
    }

    if (type == "login_failed") {
        m_loginFinished = true;
        m_loginOk = false;
        m_loginError = obj["reason"].toString("登录失败");
        emit loginFailed(m_loginError);
        emit connectionError(m_loginError);
        disconnectFromServer();
        return;
    }

    if (type == "userlist") {
        QJsonArray usersArray = obj["users"].toArray();
        m_onlineUsers.clear();
        for (const QJsonValue& val : usersArray) {
            QJsonObject u = val.toObject();
            ChatUser user;
            user.id = u["id"].toString();
            user.name = u["name"].toString();
            user.isOnline = u["online"].toBool();
            m_onlineUsers.append(user);
        }
        emit userListUpdated(m_onlineUsers);
        return;
    }

    if (type == "message" || type == "private") {
        Message msg;
        msg.type = static_cast<MessageType>(obj["messageType"].toInt());
        msg.senderId = obj["senderId"].toString();
        msg.senderName = obj["senderName"].toString();
        msg.receiverId = obj["receiverId"].toString();
        msg.content = obj["content"].toString();
        msg.timestamp = QDateTime::currentDateTime();
        emit newMessage(msg);
        return;
    }

    if (type == "file") {
        Message msg;
        msg.type = static_cast<MessageType>(obj["messageType"].toInt(static_cast<int>(MessageType::File)));
        msg.senderId = obj["senderId"].toString();
        msg.senderName = obj["senderName"].toString();
        msg.receiverId = obj["receiverId"].toString();
        msg.content = obj["content"].toString();
        msg.fileName = obj["fileName"].toString();
        msg.fileSize = obj["fileSize"].toVariant().toLongLong();
        msg.fileHash = obj["fileHash"].toString();
        msg.chunkSize = obj["chunkSize"].toVariant().toLongLong();
        msg.chunkCount = obj["chunkCount"].toVariant().toLongLong();
        msg.timestamp = QDateTime::currentDateTime();
        QString base64Data = obj["fileData"].toString();
        if (!base64Data.isEmpty()) {
            msg.fileData = QByteArray::fromBase64(base64Data.toLatin1());
        }
        emit newMessage(msg);
        return;
    }

    if (type == "file_chunk") {
        handleIncomingFileChunk(obj);
        return;
    }

    if (type == "file_chunk_ack") {
        emit fileChunkAckReceived(
            obj["transferId"].toString(),
            obj["chunkIndex"].toVariant().toLongLong(),
            obj["accepted"].toBool(false),
            obj["reason"].toString());
        return;
    }

    if (type == "friend_search_result") {
        emit friendSearchResult(
            obj["account"].toString(),
            obj["userId"].toString(),
            obj["userName"].toString(),
            obj["found"].toBool(),
            obj["online"].toBool(),
            obj["exactMatch"].toBool(true),
            obj["matchCount"].toInt(obj["found"].toBool() ? 1 : 0),
            obj["matchReason"].toString());
        return;
    }

    if (type == "friend_request_sent") {
        emit friendRequestSent(obj["receiverId"].toString(), obj["delivered"].toBool());
        return;
    }

    if (type == "friend_request") {
        emit friendRequestReceived(obj["senderId"].toString(), obj["senderName"].toString());
        return;
    }

    if (type == "friend_response") {
        emit friendResponseReceived(obj["senderId"].toString(), obj["senderName"].toString(), obj["accepted"].toBool());
        return;
    }

    if (type == "system") {
        Message msg;
        msg.type = MessageType::System;
        msg.content = obj["content"].toString();
        msg.timestamp = QDateTime::currentDateTime();
        emit newMessage(msg);
        return;
    }
}

void Client::handleIncomingFileChunk(const QJsonObject& obj) {
    const QString transferId = obj["transferId"].toString().trimmed();
    const qint64 fileSize = obj["fileSize"].toVariant().toLongLong();
    const qint64 chunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 chunkCount = obj["chunkCount"].toVariant().toLongLong();
    const qint64 chunkIndex = obj["chunkIndex"].toVariant().toLongLong();
    const QByteArray chunkData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());

    auto failTransfer = [this, transferId, chunkIndex](const QString& reason) {
        if (!transferId.isEmpty()) {
            sendFileChunkAck(transferId, chunkIndex, false, reason);
            m_incomingFileTransfers.remove(transferId);
        }
        emit connectionError("文件分片接收失败：" + reason);
    };

    if (transferId.isEmpty()) {
        failTransfer("缺少传输编号");
        return;
    }
    if (fileSize <= 0 || fileSize > kMaxOutgoingPayloadBytes || chunkSize <= 0 || chunkCount <= 0 || chunkIndex < 0 || chunkIndex >= chunkCount) {
        failTransfer("分片元数据非法");
        return;
    }
    if (chunkCount > kMaxIncomingChunks) {
        failTransfer("分片数量超过接收限制");
        return;
    }
    const qint64 expectedChunkCount = (fileSize + chunkSize - 1) / chunkSize;
    if (expectedChunkCount != chunkCount) {
        failTransfer("分片数量与文件大小不一致");
        return;
    }
    if (chunkData.isEmpty() || chunkData.size() > chunkSize) {
        failTransfer("分片内容为空或超过声明大小");
        return;
    }
    if (chunkIndex < chunkCount - 1 && chunkData.size() != chunkSize) {
        failTransfer("非末尾分片大小不一致");
        return;
    }

    PendingIncomingFileTransfer& pending = m_incomingFileTransfers[transferId];
    if (pending.chunks.isEmpty()) {
        pending.envelope = obj;
        pending.envelope["type"] = "file";
        pending.envelope.remove("transferId");
        pending.envelope.remove("chunkIndex");
        pending.envelope.remove("fileData");
        pending.fileName = obj["fileName"].toString();
        pending.fileSize = fileSize;
        pending.chunkSize = chunkSize;
        pending.chunkCount = chunkCount;
        pending.chunks.resize(static_cast<int>(chunkCount));
    } else if (pending.fileSize != fileSize || pending.chunkSize != chunkSize || pending.chunkCount != chunkCount) {
        failTransfer("同一传输编号的元数据不一致");
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
        failTransfer("累计分片大小超过声明文件大小");
        return;
    }
    emit fileReceiveProgress(obj["fileName"].toString(), pending.receivedBytes, fileSize);
    if (pending.receivedIndexes.size() < pending.chunkCount) {
        sendFileChunkAck(transferId, chunkIndex, true, QString(), pending.receivedBytes);
        return;
    }

    QByteArray fileData;
    fileData.reserve(static_cast<int>(fileSize));
    for (const QByteArray& chunk : pending.chunks) {
        if (chunk.isEmpty()) {
            failTransfer("存在缺失分片");
            return;
        }
        fileData.append(chunk);
    }

    QJsonObject fullFile = pending.envelope;
    m_incomingFileTransfers.remove(transferId);
    sendFileChunkAck(transferId, chunkIndex, true, QString(), fileData.size());
    fullFile["fileData"] = QString::fromLatin1(fileData.toBase64());
    handleServerMessage(fullFile);
}

bool Client::waitForFileChunkAck(const QString& transferId, qint64 chunkIndex, QString* rejectReason) {
    if (rejectReason) rejectReason->clear();

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    bool matched = false;
    bool accepted = false;
    QString reason;

    QMetaObject::Connection ackConnection = connect(
        this,
        &Client::fileChunkAckReceived,
        &loop,
        [&](const QString& ackTransferId, qint64 ackChunkIndex, bool ackAccepted, const QString& ackReason) {
            if (ackTransferId != transferId || ackChunkIndex != chunkIndex) return;
            matched = true;
            accepted = ackAccepted;
            reason = ackReason;
            loop.quit();
        });
    QMetaObject::Connection disconnectedConnection = connect(this, &Client::disconnected, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(kChunkAckTimeoutMs);
    loop.exec();

    QObject::disconnect(ackConnection);
    QObject::disconnect(disconnectedConnection);

    if (matched && !accepted && rejectReason) {
        *rejectReason = reason.isEmpty() ? "服务端拒绝分片" : reason;
    }
    return matched && accepted;
}

void Client::cleanupExpiredIncomingFileTransfers() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QString& transferId : m_incomingFileTransfers.keys()) {
        const auto it = m_incomingFileTransfers.constFind(transferId);
        if (it == m_incomingFileTransfers.constEnd()) {
            continue;
        }
        const PendingIncomingFileTransfer& pending = it.value();
        if (pending.lastActivityMs <= 0 || now - pending.lastActivityMs <= kTransferStaleTimeoutMs) {
            continue;
        }

        const QString visibleName = pending.fileName.isEmpty() ? "未命名文件" : pending.fileName;
        m_incomingFileTransfers.remove(transferId);
        emit connectionError(QString("文件分片接收超时，已清理：%1。请对方重新发送。").arg(visibleName));
    }
}
