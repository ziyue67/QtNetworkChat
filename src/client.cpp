#include "client.h"
#include "filetransferstatus.h"
#include "tlssecurity.h"
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
constexpr int kDefaultE2ESessionMessageLimit = 100;
constexpr qint64 kTransferStaleTimeoutMs = 2LL * 60 * 1000;
constexpr int kTransferCleanupIntervalMs = 30 * 1000;
constexpr qint64 kOutgoingTransferStateMaxAgeMs = 24LL * 60 * 60 * 1000;
const char kOutgoingTransferStateFileName[] = "outgoing_transfer_state.json";
const char kE2ETrustPinsFilePrefix[] = "e2e_trust_pins_";
const char kE2EIdentityFilePrefix[] = "e2e_identity_";
constexpr qsizetype kMaxE2EIdentityPublicKeyBytes = 4096;
constexpr qsizetype kE2ETrustFingerprintHexLength = 64;

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

QString safeLocalFileToken(const QString& value) {
    QString token;
    for (const QChar ch : value.trimmed()) {
        const ushort code = ch.unicode();
        const bool alpha = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
        const bool digit = code >= '0' && code <= '9';
        token.append(alpha || digit ? ch : QLatin1Char('_'));
    }
    return token.isEmpty() ? QStringLiteral("default") : token.left(96);
}

QString e2eDataFilePath(const QString& prefix, const QString& userId) {
    QString dir = appDataDir();
    if (dir.isEmpty()) {
        dir = QDir::currentPath();
    }
    QDir().mkpath(dir);
    return QDir(dir).filePath(prefix + safeLocalFileToken(userId) + QStringLiteral(".json"));
}

QString e2eTrustPinsFilePath(const QString& userId) {
    return e2eDataFilePath(QString::fromLatin1(kE2ETrustPinsFilePrefix), userId);
}

QString e2eIdentityFilePath(const QString& userId) {
    return e2eDataFilePath(QString::fromLatin1(kE2EIdentityFilePrefix), userId);
}

bool isValidE2EFingerprint(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized.size() != kE2ETrustFingerprintHexLength) {
        return false;
    }
    for (const QChar ch : normalized) {
        const ushort code = ch.unicode();
        const bool digit = code >= '0' && code <= '9';
        const bool hex = code >= 'a' && code <= 'f';
        if (!digit && !hex) {
            return false;
        }
    }
    return true;
}

QString base64Url(const QByteArray& value) {
    return QString::fromLatin1(value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QByteArray fromBase64Url(const QString& value) {
    return QByteArray::fromBase64(value.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QJsonObject e2eIdentityJson(const QString& userId, const QByteArray& publicKey) {
    QJsonObject obj;
    obj["protocol"] = QStringLiteral("qtnetworkchat-e2e-v1");
    obj["suite"] = QStringLiteral("draft-placeholder");
    obj["userId"] = userId.trimmed();
    obj["publicKey"] = base64Url(publicKey);
    obj["publicKeyFingerprintSha256"] = e2eFingerprint(publicKey);
    obj["agreementSigning"] = true;
    obj["signatureSuite"] = QStringLiteral("draft-identity-hmac-sha256");
    obj["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    return obj;
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
    const QString expectedFingerprint = e2eFingerprint(publicKey);
    if (identity.value("publicKeyFingerprintSha256").toString().trimmed().toLower() != expectedFingerprint) {
        if (reason) *reason = QStringLiteral("fingerprint-mismatch");
        return false;
    }
    const QString signatureSuite = identity.value("signatureSuite").toString().trimmed();
    if (!signatureSuite.isEmpty()
        && signatureSuite != QLatin1String("draft-identity-hmac-sha256")) {
        if (reason) *reason = QStringLiteral("unsupported-signature-suite");
        return false;
    }
    if (reason) reason->clear();
    return true;
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
    , m_e2eSessionMessageLimit(kDefaultE2ESessionMessageLimit)
{
    connect(m_socket, &QTcpSocket::readyRead, this, &Client::onReadyRead);
    if (QSslSocket* sslSocket = qobject_cast<QSslSocket*>(m_socket)) {
        connect(sslSocket, &QSslSocket::encrypted, this, &Client::onConnected);
        connect(sslSocket, QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
                this, [sslSocket](const QList<QSslError>&) {
                    if (!envEnabled("QTNETWORKCHAT_TLS_VERIFY")
                        && configuredPinnedTlsFingerprint().isEmpty()) {
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
    loadOrCreateE2ELocalIdentity();
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
            return false;
        }
        const QString pinnedFingerprint = configuredPinnedTlsFingerprint();
        if (!pinnedFingerprint.isEmpty()) {
            QString actualFingerprint;
            if (!pinnedCertificateFingerprintMatches(sslSocket->peerCertificate(),
                                                     pinnedFingerprint,
                                                     &actualFingerprint)) {
                m_loginError = QStringLiteral("TLS 证书指纹不匹配: expected=%1 actual=%2")
                    .arg(normalizedSha256Fingerprint(pinnedFingerprint),
                         actualFingerprint.isEmpty() ? QStringLiteral("unavailable") : actualFingerprint);
                sslSocket->disconnectFromHost();
                emit connectionError(QStringLiteral("TLS 证书指纹不匹配，已断开连接"));
                return false;
            }
        }
        return true;
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
    loadOrCreateE2ELocalIdentity();
    loadE2ETrustPins();
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
    m_removedServerGroups = QJsonArray();
    m_loginError.clear();
}

void Client::loadOrCreateE2ELocalIdentity() {
    const QString normalizedUserId = m_userId.trimmed();
    if (!normalizedUserId.isEmpty()) {
        QFile file(e2eIdentityFilePath(normalizedUserId));
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
            const QByteArray privateKey = fromBase64Url(root.value(QStringLiteral("privateKey")).toString());
            const QByteArray publicKey = e2ePublicKeyFromPrivateKey(privateKey);
            const QString expectedFingerprint = root.value(QStringLiteral("publicKeyFingerprintSha256")).toString().trimmed().toLower();
            if (!privateKey.isEmpty()
                && !publicKey.isEmpty()
                && (expectedFingerprint.isEmpty() || expectedFingerprint == e2eFingerprint(publicKey))) {
                m_e2eIdentityPrivateKey = privateKey;
                m_e2eIdentityPublicKey = publicKey;
                return;
            }
        }
    }

    m_e2eIdentityPrivateKey = generateE2EPrivateKey();
    m_e2eIdentityPublicKey = e2ePublicKeyFromPrivateKey(m_e2eIdentityPrivateKey);
    if (!normalizedUserId.isEmpty()) {
        saveE2ELocalIdentity();
    }
}

bool Client::saveE2ELocalIdentity(QString* rejectReason) const {
    if (rejectReason) rejectReason->clear();
    const QString normalizedUserId = m_userId.trimmed();
    if (normalizedUserId.isEmpty() || m_e2eIdentityPrivateKey.isEmpty() || m_e2eIdentityPublicKey.isEmpty()) {
        if (rejectReason) *rejectReason = QStringLiteral("identity-not-ready");
        return false;
    }
    QJsonObject root;
    root[QStringLiteral("schema")] = QStringLiteral("qtnetworkchat-e2e-identity-v1");
    root[QStringLiteral("userId")] = normalizedUserId;
    root[QStringLiteral("privateKey")] = base64Url(m_e2eIdentityPrivateKey);
    root[QStringLiteral("publicKeyFingerprintSha256")] = e2eFingerprint(m_e2eIdentityPublicKey);
    root[QStringLiteral("updatedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QSaveFile file(e2eIdentityFilePath(normalizedUserId));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (rejectReason) *rejectReason = QStringLiteral("identity-store-open-failed");
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (rejectReason) *rejectReason = QStringLiteral("identity-store-write-failed");
        return false;
    }
    return true;
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

void Client::setE2ESessionKey(const QString& peerId, const QString& keyId, const QByteArray& sessionKey) {
    const QString normalizedPeerId = peerId.trimmed();
    const QString normalizedKeyId = keyId.trimmed();
    if (normalizedPeerId.isEmpty() || normalizedKeyId.isEmpty() || sessionKey.size() < 16) {
        return;
    }

    E2ESession session;
    session.keyId = normalizedKeyId;
    session.sessionKey = sessionKey;
    session.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    m_e2eSessions[normalizedPeerId] = session;
    emit e2eSessionStateChanged(normalizedPeerId, e2eSessionStatus(normalizedPeerId));
}

void Client::clearE2ESessionKey(const QString& peerId) {
    const QString normalizedPeerId = peerId.trimmed();
    m_e2eSessions.remove(normalizedPeerId);
    emit e2eSessionStateChanged(normalizedPeerId, e2eSessionStatus(normalizedPeerId));
}

bool Client::hasE2ESession(const QString& peerId) const {
    return m_e2eSessions.contains(peerId.trimmed());
}

bool Client::e2eSessionNeedsRotation(const QString& peerId) const {
    const auto it = m_e2eSessions.constFind(peerId.trimmed());
    return it != m_e2eSessions.constEnd() && it->rotationRequired;
}

bool Client::e2ePeerIdentityTrusted(const QString& peerId) const {
    QString reason;
    return requireTrustedE2EPeer(peerId, &reason);
}

QJsonObject Client::e2eSessionStatus(const QString& peerId) const {
    const QString normalizedPeerId = peerId.trimmed();
    QJsonObject status;
    status["peerId"] = normalizedPeerId;
    status["configured"] = false;
    status["ready"] = false;
    status["rotationRequired"] = false;
    status["messageLimit"] = m_e2eSessionMessageLimit;
    const auto it = m_e2eSessions.constFind(normalizedPeerId);
    if (it == m_e2eSessions.constEnd()) {
        status["state"] = QStringLiteral("missing-session");
        return status;
    }

    status["configured"] = true;
    status["ready"] = !it->rotationRequired;
    status["state"] = it->rotationRequired ? QStringLiteral("rotation-required") : QStringLiteral("ready");
    status["keyId"] = it->keyId;
    status["keyFingerprintSha256"] = e2eFingerprint(it->sessionKey);
    status["createdAt"] = QDateTime::fromMSecsSinceEpoch(it->createdAtMs).toUTC().toString(Qt::ISODateWithMs);
    status["encryptedMessages"] = QString::number(it->encryptedMessages);
    status["decryptedMessages"] = QString::number(it->decryptedMessages);
    status["rotationRequired"] = it->rotationRequired;
    return status;
}

QJsonObject Client::e2eLocalIdentityStatus() const {
    QJsonObject status = e2eIdentityJson(m_userId, m_e2eIdentityPublicKey);
    status["configured"] = !m_userId.trimmed().isEmpty() && !m_e2eIdentityPublicKey.isEmpty();
    status["trusted"] = true;
    status["trustState"] = QStringLiteral("local");
    status["agreementSigning"] = !m_e2eIdentityPrivateKey.isEmpty()
        && !m_e2eIdentityPublicKey.isEmpty();
    status["signatureSuite"] = QStringLiteral("draft-identity-hmac-sha256");
    status["identityPersisted"] = !m_userId.trimmed().isEmpty()
        && QFile::exists(e2eIdentityFilePath(m_userId));
    return status;
}

QJsonObject Client::e2ePeerIdentityStatus(const QString& peerId) const {
    const QString normalizedPeerId = peerId.trimmed();
    QJsonObject status;
    status["peerId"] = normalizedPeerId;
    status["configured"] = false;
    status["trusted"] = false;
    status["pinned"] = false;
    status["fingerprintMismatch"] = false;
    status["trustState"] = QStringLiteral("unknown");
    const auto it = m_e2ePeerIdentities.constFind(normalizedPeerId);
    if (it == m_e2ePeerIdentities.constEnd()) {
        return status;
    }

    status["configured"] = true;
    status["publicKeyFingerprintSha256"] = it->fingerprint;
    status["pinned"] = it->pinned;
    status["pinnedFingerprintSha256"] = it->pinnedFingerprint;
    status["pinPersisted"] = it->pinned && m_e2eStoredTrustPins.value(normalizedPeerId) == it->pinnedFingerprint;
    status["fingerprintMismatch"] = it->fingerprintMismatch;
    status["trusted"] = it->pinned && !it->fingerprintMismatch;
    status["agreementSignatureVerified"] = it->pinned && !it->fingerprintMismatch && !it->publicKey.isEmpty();
    status["signatureSuite"] = QStringLiteral("draft-identity-hmac-sha256");
    status["trustState"] = it->fingerprintMismatch
        ? QStringLiteral("mismatch")
        : (it->pinned ? QStringLiteral("trusted") : QStringLiteral("unverified"));
    status["firstSeenAt"] = QDateTime::fromMSecsSinceEpoch(it->firstSeenAtMs).toUTC().toString(Qt::ISODateWithMs);
    status["lastSeenAt"] = QDateTime::fromMSecsSinceEpoch(it->lastSeenAtMs).toUTC().toString(Qt::ISODateWithMs);
    return status;
}

void Client::setE2ESessionMessageLimitForTesting(int limit) {
    m_e2eSessionMessageLimit = qBound(1, limit, 1000000);
}

bool Client::announceE2EIdentity(const QString& peerId, QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    if (!isConnected()) {
        if (rejectReason) *rejectReason = QStringLiteral("not-connected");
        return false;
    }
    if (m_userId.trimmed().isEmpty() || m_e2eIdentityPublicKey.isEmpty()) {
        if (rejectReason) *rejectReason = QStringLiteral("identity-not-ready");
        return false;
    }

    const QString normalizedPeerId = peerId.trimmed();
    if (normalizedPeerId == m_userId) {
        if (rejectReason) *rejectReason = QStringLiteral("invalid-peer");
        return false;
    }

    QJsonObject obj;
    obj["type"] = QStringLiteral("e2e_identity_announce");
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    if (!normalizedPeerId.isEmpty()) {
        obj["receiverId"] = normalizedPeerId;
    }
    obj["e2eIdentity"] = e2eIdentityJson(m_userId, m_e2eIdentityPublicKey);
    return sendJson(obj);
}

bool Client::pinE2EPeerIdentity(const QString& peerId, const QString& expectedFingerprint, QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    const QString normalizedPeerId = peerId.trimmed();
    auto it = m_e2ePeerIdentities.find(normalizedPeerId);
    if (normalizedPeerId.isEmpty() || it == m_e2ePeerIdentities.end()) {
        if (rejectReason) *rejectReason = QStringLiteral("missing-identity");
        return false;
    }

    const QString expected = expectedFingerprint.trimmed().toLower();
    if (!expected.isEmpty() && expected != it->fingerprint) {
        if (rejectReason) *rejectReason = QStringLiteral("fingerprint-mismatch");
        it->fingerprintMismatch = true;
        emit e2eIdentityStateChanged(normalizedPeerId, e2ePeerIdentityStatus(normalizedPeerId));
        return false;
    }

    it->pinned = true;
    it->pinnedFingerprint = it->fingerprint;
    it->fingerprintMismatch = false;
    if (!saveE2ETrustPins(rejectReason)) {
        it->pinned = false;
        it->pinnedFingerprint.clear();
        emit e2eIdentityStateChanged(normalizedPeerId, e2ePeerIdentityStatus(normalizedPeerId));
        return false;
    }
    m_e2eStoredTrustPins[normalizedPeerId] = it->pinnedFingerprint;
    emit e2eIdentityStateChanged(normalizedPeerId, e2ePeerIdentityStatus(normalizedPeerId));
    return true;
}

bool Client::clearE2EPeerIdentityPin(const QString& peerId, QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    const QString normalizedPeerId = peerId.trimmed();
    if (normalizedPeerId.isEmpty()) {
        if (rejectReason) *rejectReason = QStringLiteral("invalid-peer");
        return false;
    }
    bool changed = m_e2eStoredTrustPins.remove(normalizedPeerId) > 0;
    auto it = m_e2ePeerIdentities.find(normalizedPeerId);
    if (it != m_e2ePeerIdentities.end()) {
        changed = changed || it->pinned || it->fingerprintMismatch || !it->pinnedFingerprint.isEmpty();
        it->pinned = false;
        it->pinnedFingerprint.clear();
        it->fingerprintMismatch = false;
    }
    if (!changed) {
        if (rejectReason) *rejectReason = QStringLiteral("missing-pin");
        return false;
    }
    if (!saveE2ETrustPins(rejectReason)) {
        return false;
    }
    emit e2eIdentityStateChanged(normalizedPeerId, e2ePeerIdentityStatus(normalizedPeerId));
    return true;
}

void Client::loadE2ETrustPins() {
    m_e2eStoredTrustPins.clear();
    const QString normalizedUserId = m_userId.trimmed();
    if (normalizedUserId.isEmpty()) {
        return;
    }
    QFile file(e2eTrustPinsFilePath(normalizedUserId));
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = doc.object();
    const QJsonArray pins = root.value(QStringLiteral("pins")).toArray();
    for (const QJsonValue& value : pins) {
        const QJsonObject pin = value.toObject();
        const QString peerId = pin.value(QStringLiteral("peerId")).toString().trimmed();
        const QString fingerprint = pin.value(QStringLiteral("fingerprintSha256")).toString().trimmed().toLower();
        if (!peerId.isEmpty() && isValidE2EFingerprint(fingerprint)) {
            m_e2eStoredTrustPins[peerId] = fingerprint;
        }
    }
}

bool Client::saveE2ETrustPins(QString* rejectReason) const {
    if (rejectReason) rejectReason->clear();
    QJsonArray pins;
    for (auto it = m_e2ePeerIdentities.constBegin(); it != m_e2ePeerIdentities.constEnd(); ++it) {
        if (!it->pinned || it->pinnedFingerprint.isEmpty() || !isValidE2EFingerprint(it->pinnedFingerprint)) {
            continue;
        }
        QJsonObject pin;
        pin[QStringLiteral("peerId")] = it.key();
        pin[QStringLiteral("fingerprintSha256")] = it->pinnedFingerprint;
        pin[QStringLiteral("updatedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        pins.append(pin);
    }

    QJsonObject root;
    root[QStringLiteral("schema")] = QStringLiteral("qtnetworkchat-e2e-trust-pins-v1");
    root[QStringLiteral("userId")] = m_userId.trimmed();
    root[QStringLiteral("pins")] = pins;
    QSaveFile file(e2eTrustPinsFilePath(m_userId));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (rejectReason) *rejectReason = QStringLiteral("pin-store-open-failed");
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (rejectReason) *rejectReason = QStringLiteral("pin-store-write-failed");
        return false;
    }
    return true;
}

void Client::applyE2EStoredTrustPin(const QString& peerId, E2EPeerIdentity* peerIdentity) const {
    if (!peerIdentity) {
        return;
    }
    const QString pinnedFingerprint = m_e2eStoredTrustPins.value(peerId.trimmed()).trimmed().toLower();
    if (pinnedFingerprint.isEmpty()) {
        if (!peerIdentity->pinned) {
            peerIdentity->pinnedFingerprint.clear();
            peerIdentity->fingerprintMismatch = false;
        }
        return;
    }
    peerIdentity->pinned = true;
    peerIdentity->pinnedFingerprint = pinnedFingerprint;
    peerIdentity->fingerprintMismatch = peerIdentity->fingerprint != pinnedFingerprint;
}

bool Client::requireTrustedE2EPeer(const QString& peerId, QString* rejectReason) const {
    if (rejectReason) rejectReason->clear();
    const QString normalizedPeerId = peerId.trimmed();
    const auto peerIdentity = m_e2ePeerIdentities.constFind(normalizedPeerId);
    if (normalizedPeerId.isEmpty() || peerIdentity == m_e2ePeerIdentities.constEnd()) {
        if (rejectReason) *rejectReason = QStringLiteral("missing-identity");
        return false;
    }
    if (peerIdentity->fingerprintMismatch) {
        if (rejectReason) *rejectReason = QStringLiteral("fingerprint-mismatch");
        return false;
    }
    if (!peerIdentity->pinned || peerIdentity->pinnedFingerprint != peerIdentity->fingerprint) {
        if (rejectReason) *rejectReason = QStringLiteral("untrusted-identity");
        return false;
    }
    return true;
}

bool Client::populateE2EAgreementIdentityFingerprints(const QString& peerId,
                                                      E2EKeyAgreement* agreement,
                                                      QString* rejectReason) const {
    if (rejectReason) rejectReason->clear();
    if (!agreement) {
        if (rejectReason) *rejectReason = QStringLiteral("invalid-agreement");
        return false;
    }
    if (m_userId.trimmed().isEmpty() || m_e2eIdentityPublicKey.isEmpty()) {
        if (rejectReason) *rejectReason = QStringLiteral("identity-not-ready");
        return false;
    }

    const QString normalizedPeerId = peerId.trimmed();
    if (!requireTrustedE2EPeer(normalizedPeerId, rejectReason)) {
        return false;
    }
    const auto peerIdentity = m_e2ePeerIdentities.constFind(normalizedPeerId);

    agreement->senderIdentityFingerprint = e2eFingerprint(m_e2eIdentityPublicKey);
    agreement->receiverIdentityFingerprint = peerIdentity->fingerprint;
    return true;
}

bool Client::validateIncomingE2EAgreementIdentity(const E2EKeyAgreement& agreement,
                                                  QString* rejectReason) const {
    if (rejectReason) rejectReason->clear();
    const QString senderId = agreement.senderId.trimmed();
    if (senderId.isEmpty() || senderId == m_userId) {
        if (rejectReason) *rejectReason = QStringLiteral("invalid-peer");
        return false;
    }
    const QString localFingerprint = e2eFingerprint(m_e2eIdentityPublicKey);
    if (agreement.receiverIdentityFingerprint.trimmed().toLower() != localFingerprint) {
        if (rejectReason) *rejectReason = QStringLiteral("receiver-fingerprint-mismatch");
        return false;
    }

    if (!requireTrustedE2EPeer(senderId, rejectReason)) {
        return false;
    }
    const auto peerIdentity = m_e2ePeerIdentities.constFind(senderId);
    if (agreement.senderIdentityFingerprint.trimmed().toLower() != peerIdentity->fingerprint) {
        if (rejectReason) *rejectReason = QStringLiteral("sender-fingerprint-mismatch");
        return false;
    }
    if (!verifyE2EKeyAgreementSignature(agreement, peerIdentity->publicKey, rejectReason)) {
        return false;
    }
    return true;
}

void Client::installE2EDerivedSession(const QString& peerId,
                                      const QString& keyId,
                                      const QByteArray& sessionKey) {
    const QString normalizedPeerId = peerId.trimmed();
    const QString normalizedKeyId = keyId.trimmed();
    if (normalizedPeerId.isEmpty() || normalizedKeyId.isEmpty() || sessionKey.size() < 16) {
        return;
    }

    E2ESession session;
    session.keyId = normalizedKeyId;
    session.sessionKey = sessionKey;
    session.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    m_e2eSessions[normalizedPeerId] = session;
    emit e2eSessionStateChanged(normalizedPeerId, e2eSessionStatus(normalizedPeerId));
}

bool Client::requestE2ESessionRotation(const QString& peerId, QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    if (!isConnected()) {
        if (rejectReason) *rejectReason = QStringLiteral("not-connected");
        return false;
    }

    const QString normalizedPeerId = peerId.trimmed();
    if (normalizedPeerId.isEmpty() || normalizedPeerId == m_userId) {
        if (rejectReason) *rejectReason = QStringLiteral("invalid-peer");
        return false;
    }

    E2EKeyAgreement agreement;
    agreement.protocol = QStringLiteral("qtnetworkchat-e2e-v1");
    agreement.suite = QStringLiteral("draft-placeholder");
    agreement.senderId = m_userId;
    agreement.receiverId = normalizedPeerId;
    agreement.keyId = QStringLiteral("rotate-%1-%2")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddhhmmsszzz")),
             e2eFingerprint(generateE2ESessionKey()).left(12));
    E2EPendingAgreement pending;
    pending.privateKey = generateE2EPrivateKey();
    agreement.publicKey = e2ePublicKeyFromPrivateKey(pending.privateKey);
    if (!populateE2EAgreementIdentityFingerprints(normalizedPeerId, &agreement, rejectReason)) {
        return false;
    }
    if (!signE2EKeyAgreement(&agreement, m_e2eIdentityPrivateKey, rejectReason)) {
        return false;
    }

    QString validationReason;
    if (!agreement.isValid(&validationReason)) {
        if (rejectReason) *rejectReason = validationReason;
        return false;
    }

    QJsonObject obj;
    obj["type"] = QStringLiteral("e2e_key_rotation_request");
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["receiverId"] = normalizedPeerId;
    obj["reason"] = e2eSessionNeedsRotation(normalizedPeerId)
        ? QStringLiteral("rotation-required")
        : QStringLiteral("manual-request");
    obj["e2eKeyAgreement"] = agreement.toJson();
    pending.agreement = agreement;
    pending.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    m_e2ePendingOutgoingAgreements[normalizedPeerId] = pending;
    return sendJson(obj);
}

bool Client::respondE2ESessionRotation(const QString& peerId,
                                       const QString& keyId,
                                       const QByteArray& publicKey,
                                       bool accepted,
                                       const QString& reason,
                                       QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    if (!isConnected()) {
        if (rejectReason) *rejectReason = QStringLiteral("not-connected");
        return false;
    }

    const QString normalizedPeerId = peerId.trimmed();
    E2EKeyAgreement agreement;
    agreement.protocol = QStringLiteral("qtnetworkchat-e2e-v1");
    agreement.suite = QStringLiteral("draft-placeholder");
    agreement.senderId = m_userId;
    agreement.receiverId = normalizedPeerId;
    agreement.keyId = keyId.trimmed();
    E2EPendingAgreement pending;
    pending.privateKey = generateE2EPrivateKey();
    agreement.publicKey = e2ePublicKeyFromPrivateKey(pending.privateKey);
    if (!populateE2EAgreementIdentityFingerprints(normalizedPeerId, &agreement, rejectReason)) {
        return false;
    }
    if (!signE2EKeyAgreement(&agreement, m_e2eIdentityPrivateKey, rejectReason)) {
        return false;
    }

    QString validationReason;
    if (!agreement.isValid(&validationReason)) {
        if (rejectReason) *rejectReason = validationReason;
        return false;
    }

    QJsonObject obj;
    obj["type"] = QStringLiteral("e2e_key_rotation_response");
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["receiverId"] = normalizedPeerId;
    obj["accepted"] = accepted;
    obj["reason"] = accepted ? QStringLiteral("accepted") : (reason.trimmed().isEmpty() ? QStringLiteral("rejected") : reason.trimmed());
    obj["e2eKeyAgreement"] = agreement.toJson();
    if (accepted) {
        const auto requestIt = m_e2ePendingIncomingAgreements.constFind(normalizedPeerId);
        if (requestIt == m_e2ePendingIncomingAgreements.constEnd()) {
            if (rejectReason) *rejectReason = QStringLiteral("missing-pending-agreement");
            return false;
        }
        QString deriveReason;
        const QByteArray sessionKey = deriveE2EAuthenticatedSessionKey(pending.privateKey,
                                                                       agreement,
                                                                       requestIt->agreement,
                                                                       &deriveReason);
        if (sessionKey.isEmpty()) {
            if (rejectReason) *rejectReason = deriveReason;
            return false;
        }
        installE2EDerivedSession(normalizedPeerId, agreement.keyId, sessionKey);
        m_e2ePendingIncomingAgreements.remove(normalizedPeerId);
    }
    pending.agreement = agreement;
    pending.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    return sendJson(obj);
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

bool Client::sendEncryptedPrivateMessage(const QString& receiverId, const QString& content, QString* rejectReason) {
    if (rejectReason) rejectReason->clear();
    if (!isConnected()) {
        if (rejectReason) *rejectReason = QStringLiteral("not-connected");
        return false;
    }

    const QString normalizedReceiverId = receiverId.trimmed();
    auto sessionIt = m_e2eSessions.find(normalizedReceiverId);
    if (normalizedReceiverId.isEmpty() || sessionIt == m_e2eSessions.constEnd()) {
        if (rejectReason) *rejectReason = QStringLiteral("missing-session");
        return false;
    }
    if (!requireTrustedE2EPeer(normalizedReceiverId, rejectReason)) {
        return false;
    }
    if (sessionIt->rotationRequired || sessionIt->encryptedMessages >= m_e2eSessionMessageLimit) {
        sessionIt->rotationRequired = true;
        if (rejectReason) *rejectReason = QStringLiteral("rotation-required");
        emit e2eSessionStateChanged(normalizedReceiverId, e2eSessionStatus(normalizedReceiverId));
        return false;
    }

    QString reason;
    const E2EEnvelope envelope = encryptE2EText(m_userId,
                                               normalizedReceiverId,
                                               sessionIt->keyId,
                                               sessionIt->sessionKey,
                                               content,
                                               &reason);
    if (!envelope.isValid(&reason)) {
        if (rejectReason) *rejectReason = reason.isEmpty() ? QStringLiteral("invalid-envelope") : reason;
        return false;
    }

    QJsonObject obj;
    obj["type"] = "private";
    obj["messageType"] = static_cast<int>(MessageType::Private);
    obj["senderId"] = m_userId;
    obj["senderName"] = m_userName;
    obj["receiverId"] = normalizedReceiverId;
    obj["content"] = QStringLiteral("[encrypted]");
    obj["e2eEnvelope"] = envelope.toJson();
    obj["isEncrypted"] = true;

    const bool sent = sendJson(obj);
    if (sent) {
        ++sessionIt->encryptedMessages;
        if (sessionIt->encryptedMessages >= m_e2eSessionMessageLimit) {
            sessionIt->rotationRequired = true;
        }
        emit e2eSessionStateChanged(normalizedReceiverId, e2eSessionStatus(normalizedReceiverId));
    }
    return sent;
}

bool Client::e2eFileSessionForPeer(const QString& peerId,
                                   const E2ESession** session,
                                   QString* rejectReason) const {
    if (session) *session = nullptr;
    if (rejectReason) rejectReason->clear();
    const QString normalizedPeerId = peerId.trimmed();
    const auto sessionIt = m_e2eSessions.constFind(normalizedPeerId);
    if (normalizedPeerId.isEmpty() || sessionIt == m_e2eSessions.constEnd()) {
        if (rejectReason) *rejectReason = QStringLiteral("missing-session");
        return false;
    }
    if (!requireTrustedE2EPeer(normalizedPeerId, rejectReason)) {
        return false;
    }
    if (sessionIt->rotationRequired || sessionIt->encryptedMessages >= m_e2eSessionMessageLimit) {
        if (rejectReason) *rejectReason = QStringLiteral("rotation-required");
        return false;
    }
    if (session) *session = &(*sessionIt);
    return true;
}

bool Client::markE2EFileChunkSent(const QString& peerId) {
    auto sessionIt = m_e2eSessions.find(peerId.trimmed());
    if (sessionIt == m_e2eSessions.end()) {
        return false;
    }
    ++sessionIt->encryptedMessages;
    if (sessionIt->encryptedMessages >= m_e2eSessionMessageLimit) {
        sessionIt->rotationRequired = true;
    }
    emit e2eSessionStateChanged(peerId.trimmed(), e2eSessionStatus(peerId));
    return !sessionIt->rotationRequired;
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
    if (normalizedAction != "add"
        && normalizedAction != "remove"
        && normalizedAction != "promote_admin"
        && normalizedAction != "demote_admin"
        && normalizedAction != "set_admin"
        && normalizedAction != "unset_admin") {
        return false;
    }

    QJsonObject obj;
    obj["type"] = "server_group_member_update";
    obj["groupId"] = groupId.trimmed();
    obj["memberId"] = memberId.trimmed();
    obj["action"] = normalizedAction == QLatin1String("set_admin")
        ? QStringLiteral("promote_admin")
        : (normalizedAction == QLatin1String("unset_admin") ? QStringLiteral("demote_admin") : normalizedAction);
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
    const bool e2eFileRequired = !receiverId.trimmed().isEmpty()
        && (messageType == MessageType::File || messageType == MessageType::Image)
        && hasE2ESession(receiverId);
    const E2ESession* e2eFileSession = nullptr;
    QString e2eFileRejectReason;
    if (e2eFileRequired && !e2eFileSessionForPeer(receiverId, &e2eFileSession, &e2eFileRejectReason)) {
        emit connectionError(QStringLiteral("端到端加密文件发送失败：%1").arg(e2eFileRejectReason));
        return false;
    }
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
    const QString transferId = resumeMode
        ? resumeTransferId.trimmed()
        : QString("%1_%2_%3")
            .arg(m_userId,
                 QString::number(QDateTime::currentMSecsSinceEpoch()),
                 QString::number(QRandomGenerator::global()->generate()));
    const QString e2eFileKeyId = e2eFileRequired && e2eFileSession ? e2eFileSession->keyId : QString();
    const QString e2eFileKeyFingerprint = e2eFileRequired && e2eFileSession ? e2eFingerprint(e2eFileSession->sessionKey) : QString();
    QByteArray e2eWirePayload;
    E2EEnvelope e2eFileEnvelope;
    QString wireFileHash = fileHash;
    qint64 wireFileSize = fileInfo.size();
    qint64 wireChunkCount = chunkCount;
    if (e2eFileRequired) {
        if (resumeMode) {
            emit connectionError(QStringLiteral("端到端加密文件续传失败：需要重新发送以保持认证信封一致"));
            file.close();
            return false;
        }
        QFile plainFile(fileInfo.absoluteFilePath());
        if (!plainFile.open(QIODevice::ReadOnly)) {
            file.close();
            return false;
        }
        const QByteArray plainPayload = plainFile.readAll();
        plainFile.close();
        if (plainPayload.size() != fileInfo.size()) {
            file.close();
            return false;
        }
        const QString aad = QStringLiteral("file/private/v1;%1;%2;%3;%4")
            .arg(transferId,
                 QString::number(fileInfo.size()),
                 fileHash,
                 fileInfo.fileName());
        QString encryptReason;
        e2eFileEnvelope = encryptE2EPayload(m_userId,
                                            receiverId.trimmed(),
                                            e2eFileKeyId,
                                            e2eFileSession->sessionKey,
                                            plainPayload,
                                            aad,
                                            &encryptReason);
        if (!e2eFileEnvelope.isValid(&encryptReason)) {
            emit connectionError(QStringLiteral("端到端加密文件发送失败：%1").arg(encryptReason));
            file.close();
            return false;
        }
        e2eWirePayload = e2eFileEnvelope.ciphertext;
        wireFileSize = e2eWirePayload.size();
        wireChunkCount = (wireFileSize + kTransferChunkBytes - 1) / kTransferChunkBytes;
        wireFileHash = QString::fromLatin1(QCryptographicHash::hash(e2eWirePayload, QCryptographicHash::Sha256).toHex());
    }
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

    if (!e2eFileRequired && !file.open(QIODevice::ReadOnly)) return false;
    m_currentOutgoingTransferId = transferId;
    m_currentOutgoingReceiverId = receiverId;
    m_currentOutgoingFileName = fileInfo.fileName();
    emit fileTransferStatusChanged(fileInfo.fileName(),
                                   transferId,
                                   resumeMode ? QStringLiteral("transfer-resumed") : QStringLiteral("transfer-prepared"),
                                   resumeMode ? resumeConfirmedBytes : 0,
                                   fileInfo.size());
    if (!e2eFileRequired
        && !saveOutgoingTransferState(transferId,
                                      fileInfo.absoluteFilePath(),
                                      receiverId,
                                      messageType,
                                      wireFileHash,
                                      wireFileSize,
                                      wireChunkCount)) {
        file.close();
        return false;
    }
    qint64 sentBytes = resumeMode ? resumeConfirmedBytes : 0;
    qint64 chunkIndex = resumeMode ? resolvedResumeNextChunkIndex : 0;
    emit fileTransferProgress(fileInfo.fileName(), sentBytes, fileInfo.size());
    if (chunkIndex == wireChunkCount) {
        file.close();
        const bool completed = sentBytes == wireFileSize;
        if (completed) {
            clearOutgoingTransferState();
            emit fileTransferStatusChanged(fileInfo.fileName(),
                                           transferId,
                                           QStringLiteral("transfer-completed"),
                                           sentBytes,
                                           fileInfo.size());
        }
        return completed;
    }
    const qint64 startOffset = chunkIndex * kTransferChunkBytes;
    if (startOffset > wireFileSize || (!e2eFileRequired && !file.seek(startOffset))) {
        file.close();
        return false;
    }

    bool resumeAfterAckTimeoutUsed = false;
    while (chunkIndex < wireChunkCount) {
        if (m_cancelOutgoingTransfer) {
            file.close();
            return false;
        }
        const QByteArray chunk = e2eFileRequired
            ? e2eWirePayload.mid(static_cast<int>(chunkIndex * kTransferChunkBytes),
                                 static_cast<int>(qMin(kTransferChunkBytes, wireFileSize - chunkIndex * kTransferChunkBytes)))
            : file.read(kTransferChunkBytes);
        if (chunk.isEmpty()) {
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
        obj["fileSize"] = QString::number(wireFileSize);
        obj["fileHash"] = wireFileHash;
        obj["chunkSize"] = QString::number(kTransferChunkBytes);
        obj["chunkCount"] = QString::number(wireChunkCount);
        obj["chunkIndex"] = QString::number(chunkIndex);
        obj["content"] = contentPrefix + fileInfo.fileName();
        if (e2eFileRequired) {
            obj["e2eEnvelope"] = e2eFileEnvelope.toJson();
            obj["isEncrypted"] = true;
            obj["e2eFileEncrypted"] = true;
            obj["e2eFileKeyId"] = e2eFileKeyId;
            obj["e2eFileKeyFingerprintSha256"] = e2eFileKeyFingerprint;
            obj["e2eFilePlainSize"] = QString::number(fileInfo.size());
            obj["e2eFilePlainHash"] = fileHash;
        }
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
                const qint64 expectedAckBytes = qMin(wireFileSize, chunkIndex * kTransferChunkBytes + chunk.size());
                if (ackReceivedBytes > 0 && (ackReceivedBytes < expectedAckBytes || ackReceivedBytes > wireFileSize)) {
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
                    emit fileTransferStatusChanged(fileInfo.fileName(), transferId, ackRejectReason, sentBytes, fileInfo.size());
                    emit connectionError(fileTransferUserMessage(ackRejectReason,
                        QString("文件分片暂时被拒绝，正在重试：%1").arg(ackRejectReason)) + QStringLiteral("，正在重试"));
                    ackRejectReason.clear();
                    continue;
                }
                emit fileTransferStatusChanged(fileInfo.fileName(), transferId, ackRejectReason, sentBytes, fileInfo.size());
                emit connectionError(fileTransferUserMessage(ackRejectReason,
                    QString("文件分片发送被拒绝：%1").arg(ackRejectReason)));
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
                    if (resumeFileSize == wireFileSize
                        && resumeChunkSize == kTransferChunkBytes
                        && resumeChunkCount == wireChunkCount
                        && resumeFileHash.compare(wireFileHash, Qt::CaseInsensitive) == 0
                        && resolveResumeProgress(resumeConfirmedBytes,
                                                  resumeNextChunkIndex,
                                                  resumeReceivedChunks,
                                                  wireFileSize,
                                                  wireChunkCount,
                                                  &resumeReceivedChunkSet,
                                                  &firstMissingChunkIndex)) {
                        receivedChunkIndexes = resumeReceivedChunkSet;
                        if (firstMissingChunkIndex > chunkIndex) {
                            if (firstMissingChunkIndex == wireChunkCount) {
                                sentBytes = wireFileSize;
                                chunkIndex = wireChunkCount;
                                emit fileTransferProgress(fileInfo.fileName(), sentBytes, fileInfo.size());
                                advancedByResumeState = true;
                                acknowledged = true;
                                break;
                            }
                            const qint64 resumeOffset = firstMissingChunkIndex * kTransferChunkBytes;
                            if (resumeOffset > wireFileSize || (!e2eFileRequired && !file.seek(resumeOffset))) {
                                continue;
                            }
                            sentBytes = qMax(sentBytes,
                                             receivedBytesFromChunks(resumeReceivedChunkSet,
                                                                     wireFileSize,
                                                                     wireChunkCount));
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
            if (chunkIndex == wireChunkCount) {
                file.close();
                const bool completed = sentBytes == wireFileSize;
                if (completed) {
                    clearOutgoingTransferState();
                    emit fileTransferStatusChanged(fileInfo.fileName(),
                                                   transferId,
                                                   QStringLiteral("transfer-completed"),
                                                   sentBytes,
                                                   fileInfo.size());
                }
                return completed;
            }
            continue;
        }
        if (!acknowledged) {
            emit fileTransferStatusChanged(fileInfo.fileName(),
                                           transferId,
                                           QStringLiteral("chunk-ack-timeout"),
                                           sentBytes,
                                           fileInfo.size());
            emit connectionError(fileTransferUserMessage(QStringLiteral("chunk-ack-timeout"),
                QString("文件分片发送超时：%1 第 %2/%3 片").arg(fileInfo.fileName()).arg(chunkIndex + 1).arg(wireChunkCount)));
            file.close();
            return false;
        }
        const qint64 nextSentBytes = sentBytes + chunk.size();
        sentBytes = ackReceivedBytes > 0
            ? qBound<qint64>(sentBytes, ackReceivedBytes, wireFileSize)
            : nextSentBytes;
        receivedChunkIndexes.insert(chunkIndex);
        if (e2eFileRequired) {
            markE2EFileChunkSent(receiverId);
        }
        ++chunkIndex;
        emit fileTransferProgress(fileInfo.fileName(), sentBytes, fileInfo.size());
    }
    file.close();
    const bool completed = sentBytes == wireFileSize && chunkIndex == wireChunkCount;
    if (completed) {
        clearOutgoingTransferState();
        emit fileTransferStatusChanged(fileInfo.fileName(),
                                       transferId,
                                       QStringLiteral("transfer-completed"),
                                       sentBytes,
                                       fileInfo.size());
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
        announceE2EIdentity();
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

    if (type == "e2e_identity_announce") {
        const QString senderId = obj.value("senderId").toString().trimmed();
        const QString receiverId = obj.value("receiverId").toString().trimmed();
        const QJsonObject identity = obj.value("e2eIdentity").toObject();
        QString reason;
        if (senderId.isEmpty()
            || senderId == m_userId
            || (!receiverId.isEmpty() && receiverId != m_userId)
            || identity.value("userId").toString().trimmed() != senderId
            || !validateE2EIdentityJson(identity, &reason)) {
            emit connectionError(QStringLiteral("端到端加密身份公告无效：%1")
                .arg(reason.isEmpty() ? QStringLiteral("identity-mismatch") : reason));
            return;
        }

        const QByteArray publicKey = fromBase64Url(identity.value("publicKey").toString());
        const QString fingerprint = e2eFingerprint(publicKey);
        E2EPeerIdentity& peerIdentity = m_e2ePeerIdentities[senderId];
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (peerIdentity.firstSeenAtMs <= 0) {
            peerIdentity.firstSeenAtMs = nowMs;
        }
        peerIdentity.lastSeenAtMs = nowMs;
        peerIdentity.publicKey = publicKey;
        peerIdentity.fingerprint = fingerprint;
        applyE2EStoredTrustPin(senderId, &peerIdentity);
        peerIdentity.fingerprintMismatch = peerIdentity.pinned
            && !peerIdentity.pinnedFingerprint.isEmpty()
            && peerIdentity.pinnedFingerprint != fingerprint;
        emit e2eIdentityStateChanged(senderId, e2ePeerIdentityStatus(senderId));
        if (peerIdentity.fingerprintMismatch) {
            emit connectionError(QStringLiteral("端到端加密身份指纹变化：%1").arg(senderId));
        }
        return;
    }

    if (type == "e2e_key_rotation_request" || type == "e2e_key_rotation_response") {
        const E2EKeyAgreement agreement = E2EKeyAgreement::fromJson(obj.value("e2eKeyAgreement").toObject());
        QString reason;
        const QString senderId = obj.value("senderId").toString();
        const QString receiverId = obj.value("receiverId").toString();
        if (!agreement.isValid(&reason)
            || senderId != agreement.senderId
            || receiverId != agreement.receiverId
            || receiverId != m_userId
            || !validateIncomingE2EAgreementIdentity(agreement, &reason)) {
            emit connectionError(QStringLiteral("端到端加密轮换消息无效：%1")
                .arg(reason.isEmpty() ? QStringLiteral("identity-mismatch") : reason));
            return;
        }

        if (type == "e2e_key_rotation_request") {
            E2EPendingAgreement pending;
            pending.agreement = agreement;
            pending.createdAtMs = QDateTime::currentMSecsSinceEpoch();
            m_e2ePendingIncomingAgreements[senderId] = pending;
            emit e2eSessionRotationRequested(senderId, agreement.toJson());
        } else {
            const bool accepted = obj.value("accepted").toBool(false);
            if (accepted) {
                const auto pendingIt = m_e2ePendingOutgoingAgreements.constFind(senderId);
                if (pendingIt == m_e2ePendingOutgoingAgreements.constEnd()) {
                    emit connectionError(QStringLiteral("端到端加密轮换消息无效：missing-pending-agreement"));
                    return;
                }
                QString deriveReason;
                const QByteArray sessionKey = deriveE2EAuthenticatedSessionKey(pendingIt->privateKey,
                                                                               pendingIt->agreement,
                                                                               agreement,
                                                                               &deriveReason);
                if (sessionKey.isEmpty()) {
                    emit connectionError(QStringLiteral("端到端加密轮换消息无效：%1").arg(deriveReason));
                    return;
                }
                installE2EDerivedSession(senderId, agreement.keyId, sessionKey);
                m_e2ePendingOutgoingAgreements.remove(senderId);
            }
            emit e2eSessionRotationResponded(senderId,
                                             agreement.toJson(),
                                             accepted,
                                             obj.value("reason").toString());
        }
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
        if (obj.value("e2eEnvelope").isObject()) {
            const E2EEnvelope envelope = E2EEnvelope::fromJson(obj.value("e2eEnvelope").toObject());
            QString reason;
            if (envelope.isValid(&reason)) {
                msg.e2eEnvelope = envelope;
                auto sessionIt = m_e2eSessions.find(msg.senderId);
                QString plaintext;
                if (sessionIt != m_e2eSessions.constEnd()
                    && sessionIt->keyId == envelope.keyId
                    && decryptE2EText(envelope, sessionIt->sessionKey, &plaintext, &reason)) {
                    msg.content = plaintext;
                    ++sessionIt->decryptedMessages;
                    emit e2eSessionStateChanged(msg.senderId, e2eSessionStatus(msg.senderId));
                } else {
                    msg.content = QStringLiteral("加密消息无法解密");
                    emit connectionError(QStringLiteral("端到端加密消息无法解密：%1")
                        .arg(reason.isEmpty() ? QStringLiteral("missing-session") : reason));
                }
            } else {
                msg.content = QStringLiteral("加密消息格式无效");
                emit connectionError(QStringLiteral("端到端加密消息格式无效：%1").arg(reason));
            }
        }
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
        msg.transferId = obj["transferId"].toString();
        msg.fileSize = obj["fileSize"].toVariant().toLongLong();
        msg.fileHash = obj["fileHash"].toString();
        msg.chunkSize = obj["chunkSize"].toVariant().toLongLong();
        msg.chunkCount = obj["chunkCount"].toVariant().toLongLong();
        msg.e2eFileEncrypted = obj["e2eFileEncrypted"].toBool(false);
        msg.e2eFileKeyId = obj["e2eFileKeyId"].toString();
        msg.e2eFileKeyFingerprint = obj["e2eFileKeyFingerprintSha256"].toString();
        msg.e2eFilePlainSize = obj["e2eFilePlainSize"].toVariant().toLongLong();
        msg.e2eFilePlainHash = obj["e2eFilePlainHash"].toString();
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
        m_removedServerGroups = obj["removedGroups"].toArray();
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
    const QString fileName = obj["fileName"].toString();
    const qint64 fileSize = obj["fileSize"].toVariant().toLongLong();
    const qint64 chunkSize = obj["chunkSize"].toVariant().toLongLong();
    const qint64 chunkCount = obj["chunkCount"].toVariant().toLongLong();
    const qint64 chunkIndex = obj["chunkIndex"].toVariant().toLongLong();
    QByteArray chunkData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());
    const bool e2eFileEncrypted = obj["e2eFileEncrypted"].toBool(false);
    const qint64 e2ePlainSize = obj["e2eFilePlainSize"].toVariant().toLongLong();
    const QString e2ePlainHash = obj["e2eFilePlainHash"].toString().trimmed();
    E2EEnvelope e2eEnvelope;
    if (e2eFileEncrypted && obj.value("e2eEnvelope").isObject()) {
        e2eEnvelope = E2EEnvelope::fromJson(obj.value("e2eEnvelope").toObject());
    }

    auto failTransfer = [this, transferId, fileName, fileSize, chunkIndex](const QString& reason) {
        qint64 receivedBytes = 0;
        const auto it = m_incomingFileTransfers.constFind(transferId);
        if (it != m_incomingFileTransfers.constEnd()) {
            receivedBytes = it->receivedBytes;
        }
        if (!transferId.isEmpty()) {
            sendFileChunkAck(transferId, chunkIndex, false, reason);
            m_incomingFileTransfers.remove(transferId);
        }
        emit fileTransferStatusChanged(fileName, transferId, reason, receivedBytes, fileSize);
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
    QString e2eValidationReason;
    if (e2eFileEncrypted
        && (!e2eEnvelope.isValid(&e2eValidationReason)
            || e2eEnvelope.senderId != obj["senderId"].toString()
            || e2eEnvelope.receiverId != m_userId
            || e2eEnvelope.keyId != obj["e2eFileKeyId"].toString()
            || e2ePlainSize <= 0
            || e2ePlainHash.isEmpty())) {
        failTransfer(QStringLiteral("端到端加密文件信封无效"));
        return;
    }

    PendingIncomingFileTransfer& pending = m_incomingFileTransfers[transferId];
    if (pending.chunks.isEmpty()) {
        pending.envelope = obj;
        pending.envelope["type"] = "file";
        pending.envelope.remove("chunkIndex");
        pending.envelope.remove("fileData");
        pending.fileName = obj["fileName"].toString();
        pending.fileSize = fileSize;
        pending.chunkSize = chunkSize;
        pending.chunkCount = chunkCount;
        pending.chunks.resize(static_cast<int>(chunkCount));
        emit fileTransferStatusChanged(pending.fileName,
                                       transferId,
                                       QStringLiteral("receive-started"),
                                       0,
                                       pending.fileSize);
    } else if (pending.fileSize != fileSize
               || pending.chunkSize != chunkSize
               || pending.chunkCount != chunkCount
               || pending.envelope["e2eFileEncrypted"].toBool(false) != e2eFileEncrypted
               || pending.envelope["e2eFilePlainHash"].toString().trimmed() != e2ePlainHash) {
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
    if (pending.receivedBytes > pending.fileSize) {
        failTransfer("累计分片大小超过声明文件大小");
        return;
    }
    emit fileReceiveProgress(obj["fileName"].toString(), pending.receivedBytes, pending.fileSize);
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
    if (e2eFileEncrypted) {
        const E2EEnvelope fullEnvelope = E2EEnvelope::fromJson(fullFile.value("e2eEnvelope").toObject());
        auto sessionIt = m_e2eSessions.find(fullEnvelope.senderId);
        QByteArray plaintextPayload;
        QString decryptReason;
        if (sessionIt == m_e2eSessions.end()
            || sessionIt->keyId != fullEnvelope.keyId
            || !decryptE2EPayload(fullEnvelope, sessionIt->sessionKey, &plaintextPayload, &decryptReason)) {
            failTransfer(QStringLiteral("端到端加密文件无法解密"));
            emit connectionError(QStringLiteral("端到端加密文件无法解密：%1")
                .arg(decryptReason.isEmpty() ? QStringLiteral("missing-session") : decryptReason));
            return;
        }
        fileData = plaintextPayload;
        const QString actualPlainHash = QString::fromLatin1(QCryptographicHash::hash(fileData, QCryptographicHash::Sha256).toHex());
        if (e2ePlainSize > 0 && fileData.size() != e2ePlainSize) {
            failTransfer(QStringLiteral("端到端加密文件明文大小校验失败"));
            return;
        }
        if (!e2ePlainHash.isEmpty() && actualPlainHash.compare(e2ePlainHash, Qt::CaseInsensitive) != 0) {
            failTransfer(QStringLiteral("端到端加密文件明文哈希校验失败"));
            return;
        }
        fullFile.remove("e2eEnvelope");
        fullFile["fileSize"] = QString::number(e2ePlainSize);
        fullFile["fileHash"] = e2ePlainHash;
        fullFile["chunkSize"] = QString::number(kTransferChunkBytes);
        fullFile["chunkCount"] = QString::number((e2ePlainSize + kTransferChunkBytes - 1) / kTransferChunkBytes);
        fullFile["content"] = QStringLiteral("[端到端加密] ") + fullFile["content"].toString();
        ++sessionIt->decryptedMessages;
        emit e2eSessionStateChanged(fullEnvelope.senderId, e2eSessionStatus(fullEnvelope.senderId));
    }
    m_incomingFileTransfers.remove(transferId);
    sendFileChunkAck(transferId, chunkIndex, true, QString(), fileData.size());
    emit fileTransferStatusChanged(fileName,
                                   transferId,
                                   QStringLiteral("receive-completed"),
                                   fileData.size(),
                                   fileSize);
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
