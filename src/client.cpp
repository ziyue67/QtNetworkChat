#include "client.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDataStream>
#include <QFileInfo>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
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
constexpr qint64 kOutgoingTransferStateMaxAgeMs = 24LL * 60 * 60 * 1000;
const char kOutgoingTransferStateFileName[] = "outgoing_transfer_state.json";

bool envEnabled(const char* name) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

QString appDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool isRetriableFileChunkRejectReason(const QString& reason) {
    const QString trimmed = reason.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    const QStringList fatalTokens = {
        QString::fromUtf8("元数据"),
        QString::fromUtf8("哈希"),
        QString::fromUtf8("校验"),
        QString::fromUtf8("发送者"),
        QString::fromUtf8("分片序号"),
        QString::fromUtf8("分片数量"),
        QString::fromUtf8("分片大小"),
        QString::fromUtf8("非末尾"),
        QString::fromUtf8("文件大小"),
        QString::fromUtf8("超过"),
        QString::fromUtf8("非法"),
        QString::fromUtf8("不一致"),
        QString::fromUtf8("不存在"),
        QString::fromUtf8("取消")
    };
    for (const QString& token : fatalTokens) {
        if (trimmed.contains(token, Qt::CaseInsensitive)) {
            return false;
        }
    }

    const QStringList retriableTokens = {
        QString::fromUtf8("临时"),
        QString::fromUtf8("繁忙"),
        QString::fromUtf8("重试"),
        QString::fromUtf8("稍后"),
        QString::fromLatin1("busy"),
        QString::fromLatin1("temporary"),
        QString::fromLatin1("timeout"),
        QString::fromLatin1("retry")
    };
    for (const QString& token : retriableTokens) {
        if (trimmed.contains(token, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
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

bool collectFileTransferMetadata(const QString& filePath,
                                 qint64* fileSize,
                                 qint64* chunkCount,
                                 QString* fileHash) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile() || fileInfo.size() <= 0 || fileInfo.size() > kMaxOutgoingPayloadBytes) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kTransferChunkBytes);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            return false;
        }
        hasher.addData(chunk);
    }

    if (fileSize) *fileSize = fileInfo.size();
    if (chunkCount) *chunkCount = (fileInfo.size() + kTransferChunkBytes - 1) / kTransferChunkBytes;
    if (fileHash) *fileHash = QString::fromLatin1(hasher.result().toHex());
    return true;
}

bool resolveResumeProgress(qint64 confirmedBytes,
                           qint64 nextChunkIndex,
                           const QVector<qint64>& receivedChunks,
                           qint64 fileSize,
                           qint64 chunkCount,
                           QSet<qint64>* receivedSet,
                           qint64* firstMissingChunkIndex) {
    if (confirmedBytes < 0
        || confirmedBytes > fileSize
        || nextChunkIndex < 0
        || nextChunkIndex > chunkCount) {
        return false;
    }

    QSet<qint64> received;
    for (qint64 index : receivedChunks) {
        if (index < 0 || index >= chunkCount) {
            return false;
        }
        received.insert(index);
    }
    qint64 firstMissing = 0;
    while (firstMissing < chunkCount && received.contains(firstMissing)) {
        ++firstMissing;
    }

    const qint64 minimumConfirmedBytes = qMin(fileSize, firstMissing * kTransferChunkBytes);
    if (confirmedBytes < minimumConfirmedBytes) {
        return false;
    }
    if (receivedSet) *receivedSet = received;
    if (firstMissingChunkIndex) *firstMissingChunkIndex = firstMissing;
    return true;
}

qint64 receivedBytesFromChunks(const QSet<qint64>& receivedChunks,
                               qint64 fileSize,
                               qint64 chunkCount) {
    qint64 receivedBytes = 0;
    for (qint64 index : receivedChunks) {
        if (index < 0 || index >= chunkCount) {
            continue;
        }
        const qint64 chunkStart = index * kTransferChunkBytes;
        const qint64 chunkEnd = qMin(fileSize, chunkStart + kTransferChunkBytes);
        if (chunkEnd > chunkStart) {
            receivedBytes += chunkEnd - chunkStart;
        }
    }
    return qMin(receivedBytes, fileSize);
}

QString outgoingTransferStateFilePath() {
    QString dir = appDataDir();
    if (dir.isEmpty()) {
        dir = QDir::currentPath();
    }
    QDir().mkpath(dir);
    return QDir(dir).filePath(QString::fromLatin1(kOutgoingTransferStateFileName));
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
    , m_hasServerGroupSnapshot(false)
    , m_cancelOutgoingTransfer(false)
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
    m_hasServerGroupSnapshot = false;
    m_cancelOutgoingTransfer = false;
    m_serverGroups = QJsonArray();
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

bool Client::sendServerGroupAnnouncementUpdate(const QString& groupId, const QString& announcement) {
    if (!isConnected() || groupId.trimmed().isEmpty()) return false;

    QJsonObject obj;
    obj["type"] = "server_group_announcement_update";
    obj["groupId"] = groupId.trimmed();
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["announcement"] = announcement.trimmed();
    return sendJson(obj);
}

bool Client::sendServerGroupMemberUpdate(const QString& groupId, const QString& memberId, const QString& action) {
    if (!isConnected() || groupId.trimmed().isEmpty() || memberId.trimmed().isEmpty()) return false;

    const QString normalizedAction = action.trimmed().toLower();
    if (normalizedAction != "add" && normalizedAction != "remove") return false;

    QJsonObject obj;
    obj["type"] = "server_group_member_update";
    obj["groupId"] = groupId.trimmed();
    obj["memberId"] = memberId.trimmed();
    obj["action"] = normalizedAction;
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    return sendJson(obj);
}

bool Client::sendFile(const QString& filePath, const QString& receiverId) {
    return sendFilePayload(filePath, receiverId, MessageType::File, "发送了文件: ");
}

bool Client::sendImage(const QString& filePath, const QString& receiverId) {
    return sendFilePayload(filePath, receiverId, MessageType::Image, "发送了图片: ");
}

bool Client::resumeFileTransfer(const QString& filePath,
                                const QString& transferId,
                                qint64 confirmedBytes,
                                qint64 nextChunkIndex,
                                const QString& receiverId,
                                MessageType messageType) {
    const QString trimmedTransferId = transferId.trimmed();
    if (trimmedTransferId.isEmpty() || confirmedBytes < 0 || nextChunkIndex < 0) {
        return false;
    }
    if (messageType != MessageType::File && messageType != MessageType::Image) {
        return false;
    }

    const QString contentPrefix = messageType == MessageType::Image
        ? "发送了图片: "
        : "发送了文件: ";
    return sendFilePayload(filePath, receiverId, messageType, contentPrefix, trimmedTransferId, confirmedBytes, nextChunkIndex);
}

bool Client::queryAndResumeFileTransfer(const QString& filePath,
                                        const QString& transferId,
                                        const QString& receiverId,
                                        MessageType messageType,
                                        QString* rejectReason,
                                        int timeoutMs) {
    if (rejectReason) rejectReason->clear();
    if (messageType != MessageType::File && messageType != MessageType::Image) {
        if (rejectReason) *rejectReason = "续传类型非法";
        return false;
    }

    qint64 localFileSize = 0;
    qint64 localChunkCount = 0;
    QString localFileHash;
    if (!collectFileTransferMetadata(filePath, &localFileSize, &localChunkCount, &localFileHash)) {
        if (rejectReason) *rejectReason = "本地续传文件不可用";
        return false;
    }

    const QString trimmedTransferId = transferId.trimmed();
    qint64 confirmedBytes = 0;
    qint64 nextChunkIndex = 0;
    QVector<qint64> receivedChunks;
    qint64 remoteFileSize = 0;
    qint64 remoteChunkSize = 0;
    qint64 remoteChunkCount = 0;
    QString remoteFileHash;
    QString queryRejectReason;
    if (!queryFileTransferResumeState(trimmedTransferId,
                                      &confirmedBytes,
                                      &nextChunkIndex,
                                      &receivedChunks,
                                      &queryRejectReason,
                                      timeoutMs,
                                      &remoteFileSize,
                                      &remoteChunkSize,
                                      &remoteChunkCount,
                                      &remoteFileHash)) {
        if (rejectReason) *rejectReason = queryRejectReason;
        return false;
    }

    const QString trimmedRemoteHash = remoteFileHash.trimmed();
    QSet<qint64> receivedChunkSet;
    qint64 firstMissingChunkIndex = 0;
    if (remoteFileSize != localFileSize) {
        if (rejectReason) *rejectReason = "续传文件大小不一致";
        return false;
    }
    if (remoteChunkSize != kTransferChunkBytes) {
        if (rejectReason) *rejectReason = "续传分片大小不一致";
        return false;
    }
    if (remoteChunkCount != localChunkCount) {
        if (rejectReason) *rejectReason = "续传分片数量不一致";
        return false;
    }
    if (trimmedRemoteHash.isEmpty()) {
        if (rejectReason) *rejectReason = "续传文件校验信息缺失";
        return false;
    }
    if (trimmedRemoteHash.compare(localFileHash, Qt::CaseInsensitive) != 0) {
        if (rejectReason) *rejectReason = "续传文件哈希不一致";
        return false;
    }
    if (!resolveResumeProgress(confirmedBytes,
                               nextChunkIndex,
                               receivedChunks,
                               localFileSize,
                               localChunkCount,
                               &receivedChunkSet,
                               &firstMissingChunkIndex)) {
        if (rejectReason) *rejectReason = "续传进度非法";
        return false;
    }

    return sendFilePayload(filePath,
                           receiverId,
                           messageType,
                           messageType == MessageType::Image ? "发送了图片: " : "发送了文件: ",
                           trimmedTransferId,
                           confirmedBytes,
                           firstMissingChunkIndex,
                           receivedChunks);
}

bool Client::queryFileTransferResumeState(const QString& transferId,
                                          qint64* confirmedBytes,
                                          qint64* nextChunkIndex,
                                          QVector<qint64>* receivedChunks,
                                          QString* rejectReason,
                                          int timeoutMs,
                                          qint64* fileSize,
                                          qint64* chunkSize,
                                          qint64* chunkCount,
                                          QString* fileHash) {
    if (confirmedBytes) *confirmedBytes = 0;
    if (nextChunkIndex) *nextChunkIndex = 0;
    if (receivedChunks) receivedChunks->clear();
    if (rejectReason) rejectReason->clear();
    if (fileSize) *fileSize = 0;
    if (chunkSize) *chunkSize = 0;
    if (chunkCount) *chunkCount = 0;
    if (fileHash) fileHash->clear();

    const QString trimmedTransferId = transferId.trimmed();
    if (trimmedTransferId.isEmpty()) {
        if (rejectReason) *rejectReason = "传输编号为空";
        return false;
    }

    QJsonObject obj;
    obj["type"] = "file_transfer_resume_query";
    obj["transferId"] = trimmedTransferId;
    if (!sendJson(obj)) {
        if (rejectReason) *rejectReason = "续传状态查询发送失败";
        return false;
    }
    return waitForFileTransferResumeState(trimmedTransferId,
                                          confirmedBytes,
                                          nextChunkIndex,
                                          receivedChunks,
                                          rejectReason,
                                          timeoutMs,
                                          fileSize,
                                          chunkSize,
                                          chunkCount,
                                          fileHash);
}

bool Client::saveOutgoingTransferState(const QString& transferId,
                                       const QString& filePath,
                                       const QString& receiverId,
                                       MessageType messageType,
                                       const QString& fileHash,
                                       qint64 fileSize,
                                       qint64 chunkCount) {
    const QString trimmedTransferId = transferId.trimmed();
    const QString trimmedFileHash = fileHash.trimmed();
    if (trimmedTransferId.isEmpty()
        || filePath.trimmed().isEmpty()
        || trimmedFileHash.isEmpty()
        || fileSize <= 0
        || chunkCount <= 0
        || (messageType != MessageType::File && messageType != MessageType::Image)) {
        return false;
    }

    QJsonObject state;
    state["transferId"] = trimmedTransferId;
    state["filePath"] = QFileInfo(filePath).absoluteFilePath();
    state["receiverId"] = receiverId;
    state["messageType"] = static_cast<int>(messageType);
    state["fileHash"] = trimmedFileHash;
    state["fileSize"] = QString::number(fileSize);
    state["chunkSize"] = QString::number(kTransferChunkBytes);
    state["chunkCount"] = QString::number(chunkCount);
    state["updatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QSaveFile file(outgoingTransferStateFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
    file.write("\n");
    return file.commit();
}

bool Client::loadOutgoingTransferState(QJsonObject* state) const {
    if (state) *state = QJsonObject();

    const QString path = outgoingTransferStateFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return false;
    }

    const QJsonObject obj = doc.object();
    const MessageType messageType = static_cast<MessageType>(obj["messageType"].toInt(-1));
    if (obj["transferId"].toString().trimmed().isEmpty()
        || obj["filePath"].toString().trimmed().isEmpty()
        || obj["fileHash"].toString().trimmed().isEmpty()
        || obj["fileSize"].toVariant().toLongLong() <= 0
        || obj["chunkSize"].toVariant().toLongLong() != kTransferChunkBytes
        || obj["chunkCount"].toVariant().toLongLong() <= 0
        || (messageType != MessageType::File && messageType != MessageType::Image)) {
        return false;
    }

    const QString updatedAtText = obj["updatedAt"].toString().trimmed();
    if (!updatedAtText.isEmpty()) {
        const QDateTime updatedAt = QDateTime::fromString(updatedAtText, Qt::ISODate);
        if (!updatedAt.isValid()
            || updatedAt.msecsTo(QDateTime::currentDateTimeUtc()) > kOutgoingTransferStateMaxAgeMs) {
            file.close();
            QFile::remove(path);
            return false;
        }
    }

    if (state) *state = obj;
    return true;
}

bool Client::clearOutgoingTransferState() {
    const QString path = outgoingTransferStateFilePath();
    if (!QFile::exists(path)) {
        return true;
    }
    return QFile::remove(path);
}

bool Client::resumeSavedOutgoingTransfer(QString* rejectReason, int timeoutMs) {
    if (rejectReason) rejectReason->clear();

    QJsonObject state;
    if (!loadOutgoingTransferState(&state)) {
        if (rejectReason) *rejectReason = "未找到可恢复的发送任务";
        return false;
    }

    const MessageType messageType = static_cast<MessageType>(state["messageType"].toInt(-1));
    qint64 localFileSize = 0;
    qint64 localChunkCount = 0;
    QString localFileHash;
    if (!collectFileTransferMetadata(state["filePath"].toString(), &localFileSize, &localChunkCount, &localFileHash)
        || state["fileSize"].toVariant().toLongLong() != localFileSize
        || state["chunkSize"].toVariant().toLongLong() != kTransferChunkBytes
        || state["chunkCount"].toVariant().toLongLong() != localChunkCount
        || state["fileHash"].toString().trimmed().compare(localFileHash, Qt::CaseInsensitive) != 0) {
        if (rejectReason) *rejectReason = "保存的发送任务与本地文件不一致";
        return false;
    }

    QString resumeReason;
    const bool resumed = queryAndResumeFileTransfer(
        state["filePath"].toString(),
        state["transferId"].toString(),
        state["receiverId"].toString(),
        messageType,
        &resumeReason,
        timeoutMs);
    if (!resumed) {
        if (rejectReason) *rejectReason = resumeReason.isEmpty() ? "发送任务恢复失败" : resumeReason;
        return false;
    }

    clearOutgoingTransferState();
    return true;
}

void Client::cancelCurrentOutgoingTransfer() {
    if (m_cancelOutgoingTransfer) return;
    m_cancelOutgoingTransfer = true;
    if (isConnected() && !m_currentOutgoingTransferId.isEmpty()) {
        QJsonObject obj;
        obj["type"] = "file_transfer_cancel";
        obj["transferId"] = m_currentOutgoingTransferId;
        obj["senderId"] = m_userId;
        obj["senderName"] = m_userName;
        obj["receiverId"] = m_currentOutgoingReceiverId;
        obj["fileName"] = m_currentOutgoingFileName;
        sendJson(obj);
    }
    clearOutgoingTransferState();
    emit outgoingTransferCancelRequested();
}

bool Client::sendFilePayload(const QString& filePath,
                             const QString& receiverId,
                             MessageType messageType,
                             const QString& contentPrefix,
                             const QString& resumeTransferId,
                             qint64 resumeConfirmedBytes,
                             qint64 resumeNextChunkIndex,
                             const QVector<qint64>& resumeReceivedChunks) {
    if (!isConnected()) return false;
    m_cancelOutgoingTransfer = false;
    m_currentOutgoingTransferId.clear();
    m_currentOutgoingReceiverId.clear();
    m_currentOutgoingFileName.clear();
    struct OutgoingTransferCleanup {
        Client* client;
        ~OutgoingTransferCleanup() {
            client->m_currentOutgoingTransferId.clear();
            client->m_currentOutgoingReceiverId.clear();
            client->m_currentOutgoingFileName.clear();
        }
    } cleanup{this};

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
        if (m_cancelOutgoingTransfer) {
            file.close();
            return false;
        }
        const QByteArray chunk = file.read(kTransferChunkBytes);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            file.close();
            return false;
        }
        hasher.addData(chunk);
        preparedBytes += chunk.size();
        emit fileTransferProgress(fileInfo.fileName(), preparedBytes, fileInfo.size());
        if (m_cancelOutgoingTransfer) {
            file.close();
            return false;
        }
    }
    file.close();

    const qint64 chunkCount = (fileInfo.size() + kTransferChunkBytes - 1) / kTransferChunkBytes;
    const QString fileHash = QString::fromLatin1(hasher.result().toHex());
    const bool resumeMode = !resumeTransferId.trimmed().isEmpty();
    QSet<qint64> receivedChunkIndexes;
    qint64 resolvedResumeNextChunkIndex = resumeNextChunkIndex;
    if (resumeMode) {
        QVector<qint64> resumeProgressChunks = resumeReceivedChunks;
        if (resumeProgressChunks.isEmpty()) {
            for (qint64 index = 0; index < resumeNextChunkIndex; ++index) {
                resumeProgressChunks.append(index);
            }
        }
        if (!resolveResumeProgress(resumeConfirmedBytes,
                                   resumeNextChunkIndex,
                                   resumeProgressChunks,
                                   fileInfo.size(),
                                   chunkCount,
                                   &receivedChunkIndexes,
                                   &resolvedResumeNextChunkIndex)) {
            return false;
        }
        if (resolvedResumeNextChunkIndex == chunkCount && resumeConfirmedBytes != fileInfo.size()) {
            return false;
        }
    }
    emit fileTransferPrepared(fileInfo.fileName(), fileInfo.size(), kTransferChunkBytes, chunkCount, fileHash);
    if (m_cancelOutgoingTransfer) return false;

    if (!file.open(QIODevice::ReadOnly)) return false;
    const QString transferId = resumeMode
        ? resumeTransferId.trimmed()
        : QString("%1_%2_%3")
            .arg(m_userId,
                 QString::number(QDateTime::currentMSecsSinceEpoch()),
                 QString::number(QRandomGenerator::global()->generate()));
    m_currentOutgoingTransferId = transferId;
    m_currentOutgoingReceiverId = receiverId;
    m_currentOutgoingFileName = fileInfo.fileName();
    if (!saveOutgoingTransferState(transferId,
                                   fileInfo.absoluteFilePath(),
                                   receiverId,
                                   messageType,
                                   fileHash,
                                   fileInfo.size(),
                                   chunkCount)) {
        file.close();
        return false;
    }
    qint64 sentBytes = resumeMode ? resumeConfirmedBytes : 0;
    qint64 chunkIndex = resumeMode ? resolvedResumeNextChunkIndex : 0;
    emit fileTransferProgress(fileInfo.fileName(), sentBytes, fileInfo.size());
    if (chunkIndex == chunkCount) {
        file.close();
        const bool completed = sentBytes == fileInfo.size();
        if (completed) {
            clearOutgoingTransferState();
        }
        return completed;
    }
    const qint64 startOffset = chunkIndex * kTransferChunkBytes;
    if (startOffset > fileInfo.size() || !file.seek(startOffset)) {
        file.close();
        return false;
    }

    bool resumeAfterAckTimeoutUsed = false;
    while (!file.atEnd()) {
        if (m_cancelOutgoingTransfer) {
            file.close();
            return false;
        }
        const QByteArray chunk = file.read(kTransferChunkBytes);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            file.close();
            return false;
        }
        if (receivedChunkIndexes.contains(chunkIndex)) {
            sentBytes = qMax(sentBytes, qMin(fileInfo.size(), (chunkIndex + 1) * kTransferChunkBytes));
            ++chunkIndex;
            emit fileTransferProgress(fileInfo.fileName(), sentBytes, fileInfo.size());
            continue;
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
        qint64 ackReceivedBytes = 0;
        bool acknowledged = false;
        bool advancedByResumeState = false;
        for (int attempt = 1; attempt <= kChunkSendMaxAttempts; ++attempt) {
            if (m_cancelOutgoingTransfer) {
                file.close();
                return false;
            }
            if (!sendJson(obj)) {
                file.close();
                return false;
            }
            if (waitForFileChunkAck(transferId, chunkIndex, &ackRejectReason, &ackReceivedBytes)) {
                const qint64 expectedAckBytes = qMin(fileInfo.size(), chunkIndex * kTransferChunkBytes + chunk.size());
                if (ackReceivedBytes > 0 && (ackReceivedBytes < expectedAckBytes || ackReceivedBytes > fileInfo.size())) {
                    if (attempt == kChunkSendMaxAttempts) {
                        ackRejectReason = QString::fromUtf8("文件分片确认进度非法");
                    }
                    continue;
                }
                acknowledged = true;
                break;
            }
            if (m_cancelOutgoingTransfer) {
                file.close();
                return false;
            }
            if (!ackRejectReason.isEmpty()) {
                if (isRetriableFileChunkRejectReason(ackRejectReason) && attempt < kChunkSendMaxAttempts) {
                    emit connectionError(QString("文件分片暂时被拒绝，正在重试：%1").arg(ackRejectReason));
                    ackRejectReason.clear();
                    continue;
                }
                emit connectionError(QString("文件分片发送被拒绝：%1").arg(ackRejectReason));
                file.close();
                return false;
            }
            if (!resumeAfterAckTimeoutUsed) {
                resumeAfterAckTimeoutUsed = true;
                qint64 resumeConfirmedBytes = 0;
                qint64 resumeNextChunkIndex = 0;
                qint64 resumeFileSize = 0;
                qint64 resumeChunkSize = 0;
                qint64 resumeChunkCount = 0;
                QVector<qint64> resumeReceivedChunks;
                QSet<qint64> resumeReceivedChunkSet;
                qint64 firstMissingChunkIndex = 0;
                QString resumeFileHash;
                QString resumeRejectReason;
                if (queryFileTransferResumeState(transferId,
                                                 &resumeConfirmedBytes,
                                                 &resumeNextChunkIndex,
                                                 &resumeReceivedChunks,
                                                 &resumeRejectReason,
                                                 kChunkAckTimeoutMs,
                                                 &resumeFileSize,
                                                 &resumeChunkSize,
                                                 &resumeChunkCount,
                                                 &resumeFileHash)) {
                    if (resumeFileSize == fileInfo.size()
                        && resumeChunkSize == kTransferChunkBytes
                        && resumeChunkCount == chunkCount
                        && resumeFileHash.compare(fileHash, Qt::CaseInsensitive) == 0
                        && resolveResumeProgress(resumeConfirmedBytes,
                                                 resumeNextChunkIndex,
                                                 resumeReceivedChunks,
                                                 fileInfo.size(),
                                                 chunkCount,
                                                 &resumeReceivedChunkSet,
                                                 &firstMissingChunkIndex)) {
                        receivedChunkIndexes = resumeReceivedChunkSet;
                        if (firstMissingChunkIndex > chunkIndex) {
                            if (firstMissingChunkIndex == chunkCount) {
                                sentBytes = fileInfo.size();
                                chunkIndex = chunkCount;
                                emit fileTransferProgress(fileInfo.fileName(), sentBytes, fileInfo.size());
                                advancedByResumeState = true;
                                acknowledged = true;
                                break;
                            }
                            const qint64 resumeOffset = firstMissingChunkIndex * kTransferChunkBytes;
                            if (resumeOffset > fileInfo.size() || !file.seek(resumeOffset)) {
                                continue;
                            }
                            sentBytes = qMax(sentBytes,
                                             receivedBytesFromChunks(resumeReceivedChunkSet,
                                                                     fileInfo.size(),
                                                                     chunkCount));
                            chunkIndex = firstMissingChunkIndex;
                            emit fileTransferProgress(fileInfo.fileName(), sentBytes, fileInfo.size());
                            advancedByResumeState = true;
                            acknowledged = true;
                            break;
                        }
                    }
                }
            }
        }
        if (advancedByResumeState) {
            if (chunkIndex == chunkCount) {
                file.close();
                const bool completed = sentBytes == fileInfo.size();
                if (completed) {
                    clearOutgoingTransferState();
                }
                return completed;
            }
            continue;
        }
        if (!acknowledged) {
            emit connectionError(QString("文件分片发送超时：%1 第 %2/%3 片").arg(fileInfo.fileName()).arg(chunkIndex + 1).arg(chunkCount));
            file.close();
            return false;
        }
        const qint64 nextSentBytes = sentBytes + chunk.size();
        sentBytes = ackReceivedBytes > 0
            ? qBound<qint64>(sentBytes, ackReceivedBytes, fileInfo.size())
            : nextSentBytes;
        receivedChunkIndexes.insert(chunkIndex);
        ++chunkIndex;
        emit fileTransferProgress(fileInfo.fileName(), sentBytes, fileInfo.size());
    }
    file.close();
    const bool completed = sentBytes == fileInfo.size() && chunkIndex == chunkCount;
    if (completed) {
        clearOutgoingTransferState();
    }
    return completed;
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
            obj["reason"].toString(),
            obj["receivedBytes"].toVariant().toLongLong());
        return;
    }

    if (type == "file_transfer_resume_state") {
        QVector<qint64> receivedChunks;
        const QJsonArray chunks = obj["receivedChunks"].toArray();
        receivedChunks.reserve(chunks.size());
        for (const QJsonValue& value : chunks) {
            receivedChunks.append(value.toVariant().toLongLong());
        }
        emit fileTransferResumeStateReceived(
            obj["transferId"].toString(),
            obj["canResume"].toBool(false),
            obj["confirmedBytes"].toVariant().toLongLong(),
            obj["nextChunkIndex"].toVariant().toLongLong(),
            obj["fileSize"].toVariant().toLongLong(),
            obj["chunkSize"].toVariant().toLongLong(),
            obj["chunkCount"].toVariant().toLongLong(),
            obj["fileHash"].toString(),
            receivedChunks,
            obj["reason"].toString());
        return;
    }

    if (type == "server_group_snapshot") {
        m_serverGroups = obj["groups"].toArray();
        m_hasServerGroupSnapshot = true;
        emit serverGroupSnapshotReceived(m_serverGroups);
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

bool Client::waitForFileChunkAck(const QString& transferId, qint64 chunkIndex, QString* rejectReason, qint64* receivedBytes) {
    if (rejectReason) rejectReason->clear();
    if (receivedBytes) *receivedBytes = 0;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    bool matched = false;
    bool accepted = false;
    QString reason;
    qint64 ackReceivedBytes = 0;

    QMetaObject::Connection ackConnection = connect(
        this,
        &Client::fileChunkAckReceived,
        &loop,
        [&](const QString& ackTransferId, qint64 ackChunkIndex, bool ackAccepted, const QString& ackReason, qint64 ackBytes) {
            if (ackTransferId != transferId || ackChunkIndex != chunkIndex) return;
            matched = true;
            accepted = ackAccepted;
            reason = ackReason;
            ackReceivedBytes = ackBytes;
            loop.quit();
        });
    QMetaObject::Connection disconnectedConnection = connect(this, &Client::disconnected, &loop, &QEventLoop::quit);
    QMetaObject::Connection cancelConnection = connect(this, &Client::outgoingTransferCancelRequested, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(kChunkAckTimeoutMs);
    loop.exec();

    QObject::disconnect(ackConnection);
    QObject::disconnect(disconnectedConnection);
    QObject::disconnect(cancelConnection);

    if (matched && !accepted && rejectReason) {
        *rejectReason = reason.isEmpty() ? "服务端拒绝分片" : reason;
    }
    if (matched && receivedBytes) {
        *receivedBytes = ackReceivedBytes;
    }
    return matched && accepted;
}

bool Client::waitForFileTransferResumeState(const QString& transferId,
                                            qint64* confirmedBytes,
                                            qint64* nextChunkIndex,
                                            QVector<qint64>* receivedChunks,
                                            QString* rejectReason,
                                            int timeoutMs,
                                            qint64* fileSize,
                                            qint64* chunkSize,
                                            qint64* chunkCount,
                                            QString* fileHash) {
    if (confirmedBytes) *confirmedBytes = 0;
    if (nextChunkIndex) *nextChunkIndex = 0;
    if (receivedChunks) receivedChunks->clear();
    if (rejectReason) rejectReason->clear();
    if (fileSize) *fileSize = 0;
    if (chunkSize) *chunkSize = 0;
    if (chunkCount) *chunkCount = 0;
    if (fileHash) fileHash->clear();

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    bool matched = false;
    bool canResume = false;
    qint64 matchedConfirmedBytes = 0;
    qint64 matchedNextChunkIndex = 0;
    qint64 matchedFileSize = 0;
    qint64 matchedChunkSize = 0;
    qint64 matchedChunkCount = 0;
    QString matchedFileHash;
    QVector<qint64> matchedReceivedChunks;
    QString reason;

    QMetaObject::Connection resumeConnection = connect(
        this,
        &Client::fileTransferResumeStateReceived,
        &loop,
        [&](const QString& stateTransferId,
            bool stateCanResume,
            qint64 stateConfirmedBytes,
            qint64 stateNextChunkIndex,
            qint64 stateFileSize,
            qint64 stateChunkSize,
            qint64 stateChunkCount,
            const QString& stateFileHash,
            const QVector<qint64>& stateReceivedChunks,
            const QString& stateReason) {
            if (stateTransferId != transferId) return;
            matched = true;
            canResume = stateCanResume;
            matchedConfirmedBytes = stateConfirmedBytes;
            matchedNextChunkIndex = stateNextChunkIndex;
            matchedFileSize = stateFileSize;
            matchedChunkSize = stateChunkSize;
            matchedChunkCount = stateChunkCount;
            matchedFileHash = stateFileHash;
            matchedReceivedChunks = stateReceivedChunks;
            reason = stateReason;
            loop.quit();
        });
    QMetaObject::Connection disconnectedConnection = connect(this, &Client::disconnected, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(qMax(1, timeoutMs));
    loop.exec();

    QObject::disconnect(resumeConnection);
    QObject::disconnect(disconnectedConnection);

    if (matched) {
        if (confirmedBytes) *confirmedBytes = matchedConfirmedBytes;
        if (nextChunkIndex) *nextChunkIndex = matchedNextChunkIndex;
        if (receivedChunks) *receivedChunks = matchedReceivedChunks;
        if (fileSize) *fileSize = matchedFileSize;
        if (chunkSize) *chunkSize = matchedChunkSize;
        if (chunkCount) *chunkCount = matchedChunkCount;
        if (fileHash) *fileHash = matchedFileHash;
        if (!canResume && rejectReason) {
            *rejectReason = reason.isEmpty() ? "服务端未找到可续传状态" : reason;
        }
        return canResume;
    }

    if (rejectReason) *rejectReason = "续传状态查询超时";
    return false;
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
