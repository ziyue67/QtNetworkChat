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

namespace {
constexpr qint64 kMaxOutgoingPayloadBytes = 80LL * 1024 * 1024;
constexpr qint64 kTransferChunkBytes = 256LL * 1024;

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
}

Client::~Client() {
    m_heartbeatTimer->stop();
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

    QByteArray fileData;
    fileData.reserve(static_cast<int>(fileInfo.size()));
    emit fileTransferProgress(fileInfo.fileName(), 0, fileInfo.size());

    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kTransferChunkBytes);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            file.close();
            return false;
        }
        fileData.append(chunk);
        emit fileTransferProgress(fileInfo.fileName(), fileData.size(), fileInfo.size());
    }
    file.close();

    const qint64 chunkCount = (fileInfo.size() + kTransferChunkBytes - 1) / kTransferChunkBytes;
    const QString fileHash = QString::fromLatin1(QCryptographicHash::hash(fileData, QCryptographicHash::Sha256).toHex());
    emit fileTransferPrepared(fileInfo.fileName(), fileInfo.size(), kTransferChunkBytes, chunkCount, fileHash);

    QJsonObject obj;
    obj["type"] = "file";
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["receiverId"] = receiverId;
    obj["messageType"] = static_cast<int>(messageType);
    obj["fileName"] = fileInfo.fileName();
    obj["fileSize"] = QString::number(fileInfo.size());
    obj["fileHash"] = fileHash;
    obj["chunkSize"] = QString::number(kTransferChunkBytes);
    obj["chunkCount"] = QString::number(chunkCount);
    obj["content"] = contentPrefix + fileInfo.fileName();
    obj["fileData"] = QString::fromLatin1(fileData.toBase64());

    const bool ok = sendJson(obj);
    if (ok) {
        emit fileTransferProgress(fileInfo.fileName(), fileInfo.size(), fileInfo.size());
    }
    return ok;
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
    emit connected();
}

void Client::onDisconnected() {
    m_heartbeatTimer->stop();
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
