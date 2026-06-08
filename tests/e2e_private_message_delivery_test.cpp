#include "client.h"
#include "e2eenvelope.h"
#include "server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>

#include <cstdio>
#include <functional>

namespace {
QString testAppDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        qWarning() << message;
        return false;
    }
    return true;
}

bool waitFor(const std::function<bool()>& predicate, int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

void drainEvents(int rounds = 5) {
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
}

bool exchangeE2EIdentityAnnouncements(Client& left,
                                      const QString& leftPeerId,
                                      Client& right,
                                      const QString& rightPeerId,
                                      QString* leftRejectReason,
                                      QString* rightRejectReason) {
    bool sent = true;
    for (int i = 0; i < 5; ++i) {
        sent = left.announceE2EIdentity(leftPeerId, leftRejectReason) && sent;
        sent = right.announceE2EIdentity(rightPeerId, rightRejectReason) && sent;
        drainEvents(3);
    }
    return sent;
}

bool waitForMutualE2EIdentityObservation(Client& left,
                                         const QString& leftPeerId,
                                         Client& right,
                                         const QString& rightPeerId,
                                         QString* leftRejectReason,
                                         QString* rightRejectReason,
                                         int timeoutMs = 15000) {
    return waitFor([&] {
        const QString leftLocalFingerprint =
            left.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString();
        const QString rightLocalFingerprint =
            right.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString();
        const QString leftSawRightFingerprint =
            left.e2ePeerIdentityStatus(leftPeerId).value("publicKeyFingerprintSha256").toString();
        const QString rightSawLeftFingerprint =
            right.e2ePeerIdentityStatus(rightPeerId).value("publicKeyFingerprintSha256").toString();
        if (leftSawRightFingerprint != rightLocalFingerprint
            || rightSawLeftFingerprint != leftLocalFingerprint) {
            exchangeE2EIdentityAnnouncements(left,
                                             leftPeerId,
                                             right,
                                             rightPeerId,
                                             leftRejectReason,
                                             rightRejectReason);
        }
        return leftLocalFingerprint.size() == 64
            && rightLocalFingerprint.size() == 64
            && left.e2ePeerIdentityStatus(leftPeerId).value("publicKeyFingerprintSha256").toString()
                == rightLocalFingerprint
            && right.e2ePeerIdentityStatus(rightPeerId).value("publicKeyFingerprintSha256").toString()
                == leftLocalFingerprint
            && left.e2ePeerIdentityStatus(leftPeerId).value("verificationCode").toString().size() >= 12
            && left.e2ePeerIdentityStatus(leftPeerId).value("verificationCode").toString()
                == right.e2ePeerIdentityStatus(rightPeerId).value("verificationCode").toString()
            && (!leftRejectReason || leftRejectReason->isEmpty())
            && (!rightRejectReason || rightRejectReason->isEmpty());
    }, timeoutMs);
}

void disconnectClient(Client& client) {
    client.disconnectFromServer();
    waitFor([&] {
        return !client.isConnected();
    }, 2000);
    drainEvents();
}

quint16 freeLocalPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) return 0;
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

bool registerClient(Client& client,
                    const QString& account,
                    const QString& userName,
                    quint16 port) {
    client.setUserInfo(account, userName);
    client.setAccountInfo(account, "secret", true);
    if (!client.connectToServer("127.0.0.1", port)) return false;
    return client.waitForLoginResult(5000);
}

bool loginClient(Client& client,
                 const QString& account,
                 const QString& userName,
                 quint16 port) {
    client.setUserInfo(account, userName);
    client.setAccountInfo(account, "secret", false);
    if (!client.connectToServer("127.0.0.1", port)) return false;
    return client.waitForLoginResult(5000);
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

QString e2eIdentityFilePath(const QString& appDataDir, const QString& userId) {
    return QDir(appDataDir).filePath("e2e_identity_" + safeLocalFileToken(userId) + ".json");
}

QString e2eTrustPinsFilePath(const QString& appDataDir, const QString& userId) {
    return QDir(appDataDir).filePath("e2e_trust_pins_" + safeLocalFileToken(userId) + ".json");
}

bool writeTextFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(content) == content.size();
}

bool runProductionRotationLocalRebindScenario() {
    bool ok = true;
    QString rejectReason;
    const QString appDataDir = testAppDataDir();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    qunsetenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND");
    Client alice;
    alice.setUserInfo(QStringLiteral("alice-production-rotate"),
                      QStringLiteral("Alice Production Rotate"));
    const QString peerId = QStringLiteral("bob-production-rotate");
    const QString draftIdentityFingerprint =
        alice.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString();
    alice.setE2ESessionKey(peerId,
                           QStringLiteral("draft-session-before-production-rotation"),
                           generateE2ESessionKey());
    ok = expect(draftIdentityFingerprint.size() == 64
                    && alice.e2eLocalIdentityStatus().value("backendId").toString()
                        == QStringLiteral("draft-qt-hmac-stream-v1")
                    && alice.e2eSessionStatus(peerId).value("state").toString()
                        == QStringLiteral("ready"),
                "test should create draft local identity and session before production rotation") && ok;

    qputenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND", "production");
    const QJsonObject backendStatus = e2eCryptoBackendStatus();
    ok = expect(backendStatus.value("productionReady").toBool(false)
                    && backendStatus.value("productionAcceptance").toObject()
                        .value("accepted").toBool(false),
                "linked production provider should be ready before local rotation execution") && ok;
    const QJsonObject dryRun = alice.planE2EProductionRotationDryRun();
    ok = expect(dryRun.value("canRotateInPlace").toBool(false)
                    && dryRun.value("blockedStageCount").toInt(-1) == 0
                    && dryRun.value("migrationRequired").toBool(false)
                    && dryRun.value("sessionMigrationCount").toInt() == 1
                    && dryRun.value("releaseGate").toString()
                        == QStringLiteral("can-rotate-e2e-state-to-production-backend"),
                "production rotation dry-run should be executable once linked provider gates pass") && ok;

    const QJsonObject execution = alice.executeE2EProductionRotation(&rejectReason);
    const QJsonObject afterMigration = alice.planE2EBackendMigration();
    const QJsonObject identityStatus = alice.e2eLocalIdentityStatus();
    const QByteArray executionJson = QJsonDocument(execution).toJson(QJsonDocument::Compact);
    ok = expect(rejectReason.isEmpty()
                    && execution.value("executed").toBool(false)
                    && execution.value("releaseGate").toString()
                        == QStringLiteral("production-rotation-local-state-rebound")
                    && execution.value("rotatedBackendId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1")
                    && execution.value("rotatedIdentityPersisted").toBool(false)
                    && execution.value("identityFingerprintChanged").toBool(false)
                    && execution.value("clearedSessionCount").toInt() == 1
                    && execution.value("newSessionCount").toInt(-1) == 0
                    && execution.value("rebindingRequired").toBool(false)
                    && !execution.value("rawKeyExported").toBool(true)
                    && !execution.value("privateMaterialExported").toBool(true)
                    && !execution.value("sessionSecretExported").toBool(true)
                    && !execution.value("publicKeyExported").toBool(true)
                    && identityStatus.value("backendId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1")
                    && identityStatus.value("publicKeyFingerprintSha256").toString().size() == 64
                    && identityStatus.value("publicKeyFingerprintSha256").toString()
                        != draftIdentityFingerprint
                    && identityStatus.value("agreementSigning").toBool(false)
                    && !afterMigration.value("migrationRequired").toBool(true)
                    && alice.e2eSessionStatus(peerId).value("state").toString()
                        == QStringLiteral("missing-session"),
                "production rotation execute should generate production identity and clear draft sessions for rebind") && ok;
    ok = expect(!executionJson.contains("privateKey")
                    && !executionJson.contains("sessionKey")
                    && !executionJson.contains("publicKey\"")
                    && !executionJson.contains(draftIdentityFingerprint.toUtf8()),
                "production rotation local rebind evidence should stay sanitized") && ok;
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    qunsetenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND");
    const quint16 port = freeLocalPort();
    ok = expect(port != 0, "a local production rotation test port should be available") && ok;
    if (!ok) return false;

    {
        Server server;
        ok = expect(server.start(port), "production rotation server should start") && ok;
        if (!ok) return false;

        Client alicePeer;
        Client bobPeer;
        Message bobProductionMessage;
        Message bobProductionFileMessage;
        QJsonObject bobRotationRequest;
        QJsonObject aliceRotationResponse;
        bool aliceRotationAccepted = false;
        QString aliceRotationResponseReason;
        QString aliceError;
        QString bobError;
        const QString aliceId = QStringLiteral("930101");
        const QString bobId = QStringLiteral("930102");
        QString reject;

        QObject::connect(&bobPeer, &Client::newMessage, &bobPeer, [&](const Message& msg) {
            if (msg.type == MessageType::Private) {
                bobProductionMessage = msg;
            } else if (msg.type == MessageType::File) {
                bobProductionFileMessage = msg;
            }
        });
        QObject::connect(&bobPeer, &Client::e2eSessionRotationRequested, &bobPeer, [&](const QString& peerId, const QJsonObject& agreement) {
            if (peerId == aliceId) {
                bobRotationRequest = agreement;
            }
        });
        QObject::connect(&alicePeer, &Client::e2eSessionRotationResponded, &alicePeer, [&](const QString& peerId, const QJsonObject& agreement, bool accepted, const QString& reason) {
            if (peerId == bobId) {
                aliceRotationResponse = agreement;
                aliceRotationAccepted = accepted;
                aliceRotationResponseReason = reason;
            }
        });
        QObject::connect(&alicePeer, &Client::connectionError, &alicePeer, [&](const QString& error) {
            aliceError = error;
        });
        QObject::connect(&bobPeer, &Client::connectionError, &bobPeer, [&](const QString& error) {
            bobError = error;
        });

        ok = expect(registerClient(alicePeer, aliceId, QStringLiteral("Alice Production Peer"), port),
                    "alice should register before production peer rotation") && ok;
        ok = expect(registerClient(bobPeer, bobId, QStringLiteral("Bob Production Peer"), port),
                    "bob should register before production peer rotation") && ok;
        QString aliceAnnounceReason;
        QString bobAnnounceReason;
        ok = expect(waitForMutualE2EIdentityObservation(alicePeer,
                                                        bobId,
                                                        bobPeer,
                                                        aliceId,
                                                        &aliceAnnounceReason,
                                                        &bobAnnounceReason),
                    "draft identities should be mutually observed before production peer rotation") && ok;
        const QString draftAliceFingerprint =
            alicePeer.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString();
        const QString draftBobFingerprint =
            bobPeer.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString();
        const QString draftVerificationCode =
            alicePeer.e2ePeerIdentityStatus(bobId).value("verificationCode").toString();
        ok = expect(draftVerificationCode.size() == 14
                        && draftVerificationCode
                            == bobPeer.e2ePeerIdentityStatus(aliceId).value("verificationCode").toString(),
                    "draft identities should share a cross-device verification code") && ok;
        ok = expect(alicePeer.pinE2EPeerIdentity(bobId,
                                                 alicePeer.e2ePeerIdentityStatus(bobId)
                                                     .value("publicKeyFingerprintSha256").toString(),
                                                 &reject)
                        && alicePeer.verifyAndPinE2EPeerIdentity(bobId,
                                                                 draftVerificationCode,
                                                                 &reject)
                        && bobPeer.pinE2EPeerIdentity(aliceId,
                                                      bobPeer.e2ePeerIdentityStatus(aliceId)
                                                          .value("publicKeyFingerprintSha256").toString(),
                                                      &reject)
                        && bobPeer.verifyAndPinE2EPeerIdentity(aliceId,
                                                               bobPeer.e2ePeerIdentityStatus(aliceId)
                                                                   .value("verificationCode").toString(),
                                                               &reject),
                    "both peers should trust the draft identities before migration") && ok;
        const QByteArray draftSessionKey = generateE2ESessionKey();
        alicePeer.setE2ESessionKey(bobId,
                                   QStringLiteral("draft-before-production-peer-rotation"),
                                   draftSessionKey);
        bobPeer.setE2ESessionKey(aliceId,
                                 QStringLiteral("draft-before-production-peer-rotation"),
                                 draftSessionKey);
        ok = expect(alicePeer.e2eSessionStatus(bobId).value("state").toString()
                            == QStringLiteral("ready")
                        && bobPeer.e2eSessionStatus(aliceId).value("state").toString()
                            == QStringLiteral("ready")
                        && alicePeer.e2eSessionStatus(bobId).value("backendId").toString()
                            == QStringLiteral("draft-qt-hmac-stream-v1"),
                    "test should install matching draft sessions before production peer rotation") && ok;

        qputenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND", "production");
        const QJsonObject linkedStatus = e2eCryptoBackendStatus();
        ok = expect(linkedStatus.value("selectedBackendId").toString()
                            == QStringLiteral("openssl-reviewed-adapter-v1")
                        && linkedStatus.value("productionReady").toBool(false)
                        && linkedStatus.value("productionAcceptance").toObject()
                            .value("accepted").toBool(false),
                    "linked OpenSSL provider should be accepted for production peer rotation") && ok;
        const QJsonObject aliceDryRun = alicePeer.planE2EProductionRotationDryRun();
        const QJsonObject bobDryRun = bobPeer.planE2EProductionRotationDryRun();
        ok = expect(aliceDryRun.value("canRotateInPlace").toBool(false)
                        && bobDryRun.value("canRotateInPlace").toBool(false)
                        && aliceDryRun.value("blockedStageCount").toInt(-1) == 0
                        && bobDryRun.value("blockedStageCount").toInt(-1) == 0
                        && aliceDryRun.value("sessionMigrationCount").toInt() == 1
                        && bobDryRun.value("sessionMigrationCount").toInt() == 1
                        && aliceDryRun.value("affectedPeerPins").toArray().size() == 1
                        && bobDryRun.value("affectedPeerPins").toArray().size() == 1,
                    "both peers should be eligible for reviewed production rotation from draft state") && ok;

        const QJsonObject aliceExecution = alicePeer.executeE2EProductionRotation(&reject);
        ok = expect(reject.isEmpty()
                        && aliceExecution.value("executed").toBool(false)
                        && aliceExecution.value("releaseGate").toString()
                            == QStringLiteral("production-rotation-local-state-rebound")
                        && aliceExecution.value("clearedSessionCount").toInt() == 1
                        && aliceExecution.value("requiresPeerReverification").toBool(false)
                        && aliceExecution.value("requiresNewSessionAgreement").toBool(false),
                    "alice should rebind local state to production identity and clear draft sessions") && ok;
        const QJsonObject bobExecution = bobPeer.executeE2EProductionRotation(&reject);
        ok = expect(reject.isEmpty()
                        && bobExecution.value("executed").toBool(false)
                        && bobExecution.value("releaseGate").toString()
                            == QStringLiteral("production-rotation-local-state-rebound")
                        && bobExecution.value("clearedSessionCount").toInt() == 1
                        && bobExecution.value("requiresPeerReverification").toBool(false)
                        && bobExecution.value("requiresNewSessionAgreement").toBool(false),
                    "bob should rebind local state to production identity and clear draft sessions") && ok;
        const QString productionAliceFingerprint =
            alicePeer.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString();
        const QString productionBobFingerprint =
            bobPeer.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString();
        const QByteArray aliceExecutionJson =
            QJsonDocument(aliceExecution).toJson(QJsonDocument::Compact);
        const QByteArray bobExecutionJson =
            QJsonDocument(bobExecution).toJson(QJsonDocument::Compact);
        ok = expect(productionAliceFingerprint.size() == 64
                        && productionBobFingerprint.size() == 64
                        && productionAliceFingerprint != draftAliceFingerprint
                        && productionBobFingerprint != draftBobFingerprint
                        && alicePeer.e2eLocalIdentityStatus().value("backendId").toString()
                            == QStringLiteral("openssl-reviewed-adapter-v1")
                        && bobPeer.e2eLocalIdentityStatus().value("backendId").toString()
                            == QStringLiteral("openssl-reviewed-adapter-v1")
                        && alicePeer.e2eSessionStatus(bobId).value("state").toString()
                            == QStringLiteral("missing-session")
                        && bobPeer.e2eSessionStatus(aliceId).value("state").toString()
                            == QStringLiteral("missing-session")
                        && alicePeer.e2ePeerIdentityStatus(bobId).value("trustState").toString()
                            == QStringLiteral("unverified")
                        && bobPeer.e2ePeerIdentityStatus(aliceId).value("trustState").toString()
                            == QStringLiteral("unverified"),
                    "production rebind should leave both peers untrusted until production identities are re-announced") && ok;
        ok = expect(!aliceExecutionJson.contains("privateKey")
                        && !aliceExecutionJson.contains("sessionKey")
                        && !aliceExecutionJson.contains("publicKey\"")
                        && !aliceExecutionJson.contains(draftAliceFingerprint.toUtf8())
                        && !aliceExecutionJson.contains(draftBobFingerprint.toUtf8())
                        && !bobExecutionJson.contains("privateKey")
                        && !bobExecutionJson.contains("sessionKey")
                        && !bobExecutionJson.contains("publicKey\"")
                        && !bobExecutionJson.contains(draftAliceFingerprint.toUtf8())
                        && !bobExecutionJson.contains(draftBobFingerprint.toUtf8()),
                    "production peer rotation execution evidence should not export sensitive material or full draft fingerprints") && ok;

        aliceAnnounceReason.clear();
        bobAnnounceReason.clear();
        ok = expect(waitForMutualE2EIdentityObservation(alicePeer,
                                                        bobId,
                                                        bobPeer,
                                                        aliceId,
                                                        &aliceAnnounceReason,
                                                        &bobAnnounceReason),
                    "production identities should be mutually re-announced after local rebind") && ok;
        ok = expect(alicePeer.e2ePeerIdentityStatus(bobId)
                            .value("publicKeyFingerprintSha256").toString()
                            == productionBobFingerprint
                        && bobPeer.e2ePeerIdentityStatus(aliceId)
                            .value("publicKeyFingerprintSha256").toString()
                            == productionAliceFingerprint,
                    "each peer should observe the other's production identity fingerprint") && ok;
        const QString productionVerificationCode =
            alicePeer.e2ePeerIdentityStatus(bobId).value("verificationCode").toString();
        ok = expect(productionVerificationCode.size() == 14
                        && productionVerificationCode
                            == bobPeer.e2ePeerIdentityStatus(aliceId)
                                .value("verificationCode").toString(),
                    "production identities should derive a fresh shared verification code") && ok;
        ok = expect(alicePeer.pinE2EPeerIdentity(bobId,
                                                 productionBobFingerprint,
                                                 &reject)
                        && alicePeer.verifyAndPinE2EPeerIdentity(bobId,
                                                                 productionVerificationCode,
                                                                 &reject)
                        && bobPeer.pinE2EPeerIdentity(aliceId,
                                                      productionAliceFingerprint,
                                                      &reject)
                        && bobPeer.verifyAndPinE2EPeerIdentity(aliceId,
                                                               productionVerificationCode,
                                                               &reject)
                        && alicePeer.e2ePeerIdentityStatus(bobId)
                            .value("pinBackendId").toString()
                            == QStringLiteral("openssl-reviewed-adapter-v1")
                        && bobPeer.e2ePeerIdentityStatus(aliceId)
                            .value("pinBackendId").toString()
                            == QStringLiteral("openssl-reviewed-adapter-v1"),
                    "both peers should re-pin production identities before session rotation") && ok;

        bobRotationRequest = QJsonObject();
        aliceRotationResponse = QJsonObject();
        aliceRotationAccepted = false;
        aliceRotationResponseReason.clear();
        ok = expect(alicePeer.requestE2ESessionRotation(bobId, &reject),
                    "alice should request a production e2e session rotation through the server") && ok;
        ok = expect(waitFor([&] {
            return bobRotationRequest.value("senderId").toString() == aliceId
                && bobRotationRequest.value("receiverId").toString() == bobId;
        }, 9000), "bob should receive the production key rotation request") && ok;
        const QByteArray requestJson = QJsonDocument(bobRotationRequest).toJson(QJsonDocument::Compact);
        ok = expect(bobRotationRequest.value("senderIdentityFingerprintSha256").toString()
                            == productionAliceFingerprint
                        && bobRotationRequest.value("receiverIdentityFingerprintSha256").toString()
                            == productionBobFingerprint
                        && bobRotationRequest.value("publicKeyFingerprintSha256").toString().size() == 64
                        && bobRotationRequest.value("signature").toString().size() > 20
                        && !bobRotationRequest.contains("sessionKey")
                        && !requestJson.contains("privateKey")
                        && !requestJson.contains("sessionKey"),
                    "production rotation request should be signed and expose only public agreement material") && ok;
        ok = expect(bobPeer.respondE2ESessionRotation(aliceId,
                                                      bobRotationRequest.value("keyId").toString()
                                                          + QStringLiteral("-response"),
                                                      generateE2ESessionKey(),
                                                      true,
                                                      QStringLiteral("accepted"),
                                                      &reject),
                    "bob should accept and derive the production e2e session") && ok;
        ok = expect(waitFor([&] {
            return aliceRotationAccepted
                && aliceRotationResponseReason == QStringLiteral("accepted")
                && aliceRotationResponse.value("senderId").toString() == bobId
                && aliceRotationResponse.value("receiverId").toString() == aliceId
                && alicePeer.hasE2ESession(bobId)
                && bobPeer.hasE2ESession(aliceId);
        }, 9000), "alice should install the production key rotation response") && ok;
        const QByteArray responseJson = QJsonDocument(aliceRotationResponse).toJson(QJsonDocument::Compact);
        ok = expect(aliceRotationResponse.value("senderIdentityFingerprintSha256").toString()
                            == productionBobFingerprint
                        && aliceRotationResponse.value("receiverIdentityFingerprintSha256").toString()
                            == productionAliceFingerprint
                        && alicePeer.e2eSessionStatus(bobId).value("backendId").toString()
                            == QStringLiteral("openssl-reviewed-adapter-v1")
                        && bobPeer.e2eSessionStatus(aliceId).value("backendId").toString()
                            == QStringLiteral("openssl-reviewed-adapter-v1")
                        && alicePeer.e2eSessionStatus(bobId)
                            .value("keyFingerprintSha256").toString().size() == 64
                        && alicePeer.e2eSessionStatus(bobId)
                            .value("keyFingerprintSha256").toString()
                            == bobPeer.e2eSessionStatus(aliceId)
                                .value("keyFingerprintSha256").toString()
                        && !aliceRotationResponse.contains("sessionKey")
                        && !responseJson.contains("privateKey")
                        && !responseJson.contains("sessionKey"),
                    "both peers should install the same production session without raw key export") && ok;

        const QString productionPlaintext =
            QStringLiteral("production e2e private message after peer rotation");
        bobProductionMessage = Message();
        ok = expect(alicePeer.sendEncryptedPrivateMessage(bobId, productionPlaintext, &reject),
                    "production session should send encrypted private text") && ok;
        ok = expect(waitFor([&] {
            return bobProductionMessage.content == productionPlaintext;
        }, 9000), "bob should decrypt private text through the production session") && ok;
        ok = expect(bobProductionMessage.e2eEnvelope.isValid()
                        && bobProductionMessage.e2eEnvelope.ciphertext
                            != productionPlaintext.toUtf8()
                        && aliceError.isEmpty()
                        && bobError.isEmpty(),
                    "production private text should retain envelope metadata and avoid decrypt diagnostics") && ok;

        const QByteArray productionFilePayload("production e2e private file payload");
        const QString productionFilePath =
            QDir(appDataDir).filePath(QStringLiteral("alice-production-private-file.bin"));
        bobProductionFileMessage = Message();
        ok = expect(writeTextFile(productionFilePath, productionFilePayload),
                    "test should write a production private file payload") && ok;
        ok = expect(alicePeer.sendFile(productionFilePath, bobId),
                    "production session should send encrypted private file payload") && ok;
        ok = expect(waitFor([&] {
            return bobProductionFileMessage.type == MessageType::File
                && bobProductionFileMessage.fileData == productionFilePayload;
        }, 9000), "bob should decrypt private file payload through the production session") && ok;
        ok = expect(bobProductionFileMessage.e2eFileEncrypted
                        && bobProductionFileMessage.e2eFilePlainSize
                            == productionFilePayload.size()
                        && bobProductionFileMessage.e2eFilePlainHash
                            == QString::fromLatin1(QCryptographicHash::hash(productionFilePayload,
                                                                             QCryptographicHash::Sha256).toHex())
                        && bobProductionFileMessage.fileHash
                            == bobProductionFileMessage.e2eFilePlainHash
                        && !bobProductionFileMessage.fileData.contains("ciphertext"),
                    "production private file delivery should keep safe e2e metadata after decrypt") && ok;

        disconnectClient(alicePeer);
        disconnectClient(bobPeer);

        Client aliceRestartedPeer;
        Client bobRestartedPeer;
        Message bobRestartedProductionMessage;
        Message bobRestartedProductionFileMessage;
        QJsonObject bobRestartedRotationRequest;
        QJsonObject aliceRestartedRotationResponse;
        bool aliceRestartedRotationAccepted = false;
        QString aliceRestartedRotationReason;
        QString aliceRestartedError;
        QString bobRestartedError;

        QObject::connect(&bobRestartedPeer, &Client::newMessage, &bobRestartedPeer, [&](const Message& msg) {
            if (msg.type == MessageType::Private) {
                bobRestartedProductionMessage = msg;
            } else if (msg.type == MessageType::File) {
                bobRestartedProductionFileMessage = msg;
            }
        });
        QObject::connect(&bobRestartedPeer, &Client::e2eSessionRotationRequested, &bobRestartedPeer, [&](const QString& peerId, const QJsonObject& agreement) {
            if (peerId == aliceId) {
                bobRestartedRotationRequest = agreement;
            }
        });
        QObject::connect(&aliceRestartedPeer, &Client::e2eSessionRotationResponded, &aliceRestartedPeer, [&](const QString& peerId, const QJsonObject& agreement, bool accepted, const QString& reason) {
            if (peerId == bobId) {
                aliceRestartedRotationResponse = agreement;
                aliceRestartedRotationAccepted = accepted;
                aliceRestartedRotationReason = reason;
            }
        });
        QObject::connect(&aliceRestartedPeer, &Client::connectionError, &aliceRestartedPeer, [&](const QString& error) {
            aliceRestartedError = error;
        });
        QObject::connect(&bobRestartedPeer, &Client::connectionError, &bobRestartedPeer, [&](const QString& error) {
            bobRestartedError = error;
        });

        ok = expect(loginClient(aliceRestartedPeer,
                                aliceId,
                                QStringLiteral("Alice Production Peer"),
                                port),
                    "restarted alice should log in with persisted production identity") && ok;
        ok = expect(loginClient(bobRestartedPeer,
                                bobId,
                                QStringLiteral("Bob Production Peer"),
                                port),
                    "restarted bob should log in with persisted production identity") && ok;
        ok = expect(aliceRestartedPeer.e2eLocalIdentityStatus()
                            .value("backendId").toString()
                            == QStringLiteral("openssl-reviewed-adapter-v1")
                        && bobRestartedPeer.e2eLocalIdentityStatus()
                            .value("backendId").toString()
                            == QStringLiteral("openssl-reviewed-adapter-v1")
                        && aliceRestartedPeer.e2eLocalIdentityStatus()
                            .value("publicKeyFingerprintSha256").toString()
                            == productionAliceFingerprint
                        && bobRestartedPeer.e2eLocalIdentityStatus()
                            .value("publicKeyFingerprintSha256").toString()
                            == productionBobFingerprint
                        && aliceRestartedPeer.e2eLocalIdentityStatus()
                            .value("identityPersisted").toBool(false)
                        && bobRestartedPeer.e2eLocalIdentityStatus()
                            .value("identityPersisted").toBool(false)
                        && aliceRestartedPeer.e2eLocalIdentityStatus()
                            .value("agreementSigning").toBool(false)
                        && bobRestartedPeer.e2eLocalIdentityStatus()
                            .value("agreementSigning").toBool(false),
                    "restarted peers should restore usable production identity material from disk") && ok;

        QString aliceRestartAnnounceReason;
        QString bobRestartAnnounceReason;
        ok = expect(waitForMutualE2EIdentityObservation(aliceRestartedPeer,
                                                        bobId,
                                                        bobRestartedPeer,
                                                        aliceId,
                                                        &aliceRestartAnnounceReason,
                                                        &bobRestartAnnounceReason),
                    "restarted peers should observe persisted production identities") && ok;
        ok = expect(aliceRestartedPeer.e2ePeerIdentityStatus(bobId)
                            .value("trustState").toString()
                            == QStringLiteral("trusted")
                        && bobRestartedPeer.e2ePeerIdentityStatus(aliceId)
                            .value("trustState").toString()
                            == QStringLiteral("trusted")
                        && aliceRestartedPeer.e2ePeerIdentityStatus(bobId)
                            .value("pinPersisted").toBool(false)
                        && bobRestartedPeer.e2ePeerIdentityStatus(aliceId)
                            .value("pinPersisted").toBool(false)
                        && aliceRestartedPeer.e2ePeerIdentityStatus(bobId)
                            .value("pinBackendId").toString()
                            == QStringLiteral("openssl-reviewed-adapter-v1")
                        && bobRestartedPeer.e2ePeerIdentityStatus(aliceId)
                            .value("pinBackendId").toString()
                            == QStringLiteral("openssl-reviewed-adapter-v1")
                        && !aliceRestartedPeer.e2ePeerIdentityStatus(bobId)
                            .value("backendMigrationRequired").toBool(true)
                        && !bobRestartedPeer.e2ePeerIdentityStatus(aliceId)
                            .value("backendMigrationRequired").toBool(true)
                        && aliceRestartedPeer.e2ePeerIdentityStatus(bobId)
                            .value("verificationCode").toString()
                            == productionVerificationCode
                        && bobRestartedPeer.e2ePeerIdentityStatus(aliceId)
                            .value("verificationCode").toString()
                            == productionVerificationCode,
                    "restarted peers should restore verified production trust pins without manual re-pin") && ok;
        ok = expect(aliceRestartedPeer.e2eSessionStatus(bobId)
                            .value("state").toString()
                            == QStringLiteral("missing-session")
                        && bobRestartedPeer.e2eSessionStatus(aliceId)
                            .value("state").toString()
                            == QStringLiteral("missing-session"),
                    "production sessions should not be reused from memory after restart") && ok;

        ok = expect(aliceRestartedPeer.requestE2ESessionRotation(bobId, &reject),
                    "restarted alice should request a fresh production session") && ok;
        ok = expect(waitFor([&] {
            return bobRestartedRotationRequest.value("senderId").toString() == aliceId
                && bobRestartedRotationRequest.value("receiverId").toString() == bobId;
        }, 9000), "restarted bob should receive the fresh production rotation request") && ok;
        const QByteArray restartedRequestJson =
            QJsonDocument(bobRestartedRotationRequest).toJson(QJsonDocument::Compact);
        ok = expect(bobRestartedRotationRequest.value("senderIdentityFingerprintSha256").toString()
                            == productionAliceFingerprint
                        && bobRestartedRotationRequest.value("receiverIdentityFingerprintSha256").toString()
                            == productionBobFingerprint
                        && bobRestartedRotationRequest.value("signature").toString().size() > 20
                        && !bobRestartedRotationRequest.contains("sessionKey")
                        && !restartedRequestJson.contains("privateKey")
                        && !restartedRequestJson.contains("sessionKey"),
                    "restarted production rotation request should stay signed and sanitized") && ok;
        ok = expect(bobRestartedPeer.respondE2ESessionRotation(aliceId,
                                                               bobRestartedRotationRequest
                                                                   .value("keyId").toString()
                                                                   + QStringLiteral("-response"),
                                                               generateE2ESessionKey(),
                                                               true,
                                                               QStringLiteral("accepted"),
                                                               &reject),
                    "restarted bob should derive a fresh production session response") && ok;
        ok = expect(waitFor([&] {
            return aliceRestartedRotationAccepted
                && aliceRestartedRotationReason == QStringLiteral("accepted")
                && aliceRestartedRotationResponse.value("senderId").toString() == bobId
                && aliceRestartedRotationResponse.value("receiverId").toString() == aliceId
                && aliceRestartedPeer.hasE2ESession(bobId)
                && bobRestartedPeer.hasE2ESession(aliceId);
        }, 9000), "restarted alice should install the fresh production session") && ok;
        const QByteArray restartedResponseJson =
            QJsonDocument(aliceRestartedRotationResponse).toJson(QJsonDocument::Compact);
        ok = expect(aliceRestartedPeer.e2eSessionStatus(bobId)
                            .value("backendId").toString()
                            == QStringLiteral("openssl-reviewed-adapter-v1")
                        && bobRestartedPeer.e2eSessionStatus(aliceId)
                            .value("backendId").toString()
                            == QStringLiteral("openssl-reviewed-adapter-v1")
                        && aliceRestartedPeer.e2eSessionStatus(bobId)
                            .value("keyFingerprintSha256").toString()
                            == bobRestartedPeer.e2eSessionStatus(aliceId)
                                .value("keyFingerprintSha256").toString()
                        && !aliceRestartedRotationResponse.contains("sessionKey")
                        && !restartedResponseJson.contains("privateKey")
                        && !restartedResponseJson.contains("sessionKey"),
                    "restarted peers should install matching fresh production sessions without raw key export") && ok;

        const QString restartedPlaintext =
            QStringLiteral("production e2e private message after restart");
        bobRestartedProductionMessage = Message();
        ok = expect(aliceRestartedPeer.sendEncryptedPrivateMessage(bobId,
                                                                   restartedPlaintext,
                                                                   &reject),
                    "fresh production session should send encrypted private text after restart") && ok;
        ok = expect(waitFor([&] {
            return bobRestartedProductionMessage.content == restartedPlaintext;
        }, 9000), "restarted bob should decrypt private text through the fresh production session") && ok;
        ok = expect(bobRestartedProductionMessage.e2eEnvelope.isValid()
                        && bobRestartedProductionMessage.e2eEnvelope.ciphertext
                            != restartedPlaintext.toUtf8()
                        && aliceRestartedError.isEmpty()
                        && bobRestartedError.isEmpty(),
                    "fresh production text delivery after restart should keep envelope metadata and avoid diagnostics") && ok;

        const QByteArray restartedFilePayload("production e2e private file payload after restart");
        const QString restartedFilePath =
            QDir(appDataDir).filePath(QStringLiteral("alice-production-private-file-after-restart.bin"));
        bobRestartedProductionFileMessage = Message();
        ok = expect(writeTextFile(restartedFilePath, restartedFilePayload),
                    "test should write a restarted production private file payload") && ok;
        ok = expect(aliceRestartedPeer.sendFile(restartedFilePath, bobId),
                    "fresh production session should send encrypted private file after restart") && ok;
        ok = expect(waitFor([&] {
            return bobRestartedProductionFileMessage.type == MessageType::File
                && bobRestartedProductionFileMessage.fileData == restartedFilePayload;
        }, 9000), "restarted bob should decrypt private file through the fresh production session") && ok;
        ok = expect(bobRestartedProductionFileMessage.e2eFileEncrypted
                        && bobRestartedProductionFileMessage.e2eFilePlainSize
                            == restartedFilePayload.size()
                        && bobRestartedProductionFileMessage.e2eFilePlainHash
                            == QString::fromLatin1(QCryptographicHash::hash(restartedFilePayload,
                                                                             QCryptographicHash::Sha256).toHex())
                        && bobRestartedProductionFileMessage.fileHash
                            == bobRestartedProductionFileMessage.e2eFilePlainHash
                        && !bobRestartedProductionFileMessage.fileData.contains("ciphertext"),
                    "fresh production file delivery after restart should keep safe e2e metadata") && ok;

        disconnectClient(aliceRestartedPeer);
        disconnectClient(bobRestartedPeer);
        qunsetenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND");
    }
    return ok;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("e2e_private_message_delivery_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = testAppDataDir();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    if (qgetenv("QTNETWORKCHAT_E2E_TEST_PRODUCTION_ROTATION_REBIND").trimmed() == "1") {
        return runProductionRotationLocalRebindScenario() ? 0 : 1;
    }

    const quint16 port = freeLocalPort();
    bool ok = true;
    ok = expect(port != 0, "a local test port should be available") && ok;
    if (!ok) return 1;

    {
        Server server;
        ok = expect(server.start(port), "server should start on the test port") && ok;
        if (!ok) return 1;

        Client alice;
        Client bob;
        Client mallory;
        Message bobMessage;
        Message bobFileMessage;
        QString bobError;
        QString malloryError;
        QJsonObject aliceE2EStatus;
        QJsonObject bobE2EStatus;
        QJsonObject bobRotationRequest;
        QJsonObject aliceRotationResponse;
        QJsonObject aliceSawBobIdentity;
        QJsonObject bobSawAliceIdentity;
        bool aliceRotationAccepted = false;
        QString aliceRotationResponseReason;
        const QString aliceId = "920001";
        const QString bobId = "920002";
        const QString malloryId = "920003";

        QObject::connect(&bob, &Client::newMessage, &app, [&](const Message& msg) {
            if (msg.type == MessageType::Private) {
                bobMessage = msg;
            } else if (msg.type == MessageType::File) {
                bobFileMessage = msg;
            }
        });
        QObject::connect(&bob, &Client::connectionError, &app, [&](const QString& error) {
            bobError = error;
        });
        QObject::connect(&mallory, &Client::connectionError, &app, [&](const QString& error) {
            malloryError = error;
        });
        QObject::connect(&alice, &Client::e2eSessionStateChanged, &app, [&](const QString& peerId, const QJsonObject& status) {
            if (peerId == bobId) {
                aliceE2EStatus = status;
            }
        });
        QObject::connect(&bob, &Client::e2eSessionStateChanged, &app, [&](const QString& peerId, const QJsonObject& status) {
            if (peerId == aliceId) {
                bobE2EStatus = status;
            }
        });
        QObject::connect(&bob, &Client::e2eSessionRotationRequested, &app, [&](const QString& peerId, const QJsonObject& agreement) {
            if (peerId == aliceId) {
                bobRotationRequest = agreement;
            }
        });
        QObject::connect(&alice, &Client::e2eSessionRotationResponded, &app, [&](const QString& peerId, const QJsonObject& agreement, bool accepted, const QString& reason) {
            if (peerId == bobId) {
                aliceRotationResponse = agreement;
                aliceRotationAccepted = accepted;
                aliceRotationResponseReason = reason;
            }
        });
        QObject::connect(&alice, &Client::e2eIdentityStateChanged, &app, [&](const QString& peerId, const QJsonObject& status) {
            if (peerId == bobId) {
                aliceSawBobIdentity = status;
            }
        });
        QObject::connect(&bob, &Client::e2eIdentityStateChanged, &app, [&](const QString& peerId, const QJsonObject& status) {
            if (peerId == aliceId) {
                bobSawAliceIdentity = status;
            }
        });

        const QString keyId = "alice-bob-session-1";
        QString rejectReason;

        ok = expect(registerClient(alice, aliceId, "Alice", port), "alice should register and log in") && ok;
        ok = expect(registerClient(bob, bobId, "Bob", port), "bob should register and log in") && ok;
        ok = expect(registerClient(mallory, malloryId, "Mallory", port), "mallory should register and log in") && ok;
        ok = expect(waitFor([&] {
            return aliceSawBobIdentity.value("publicKeyFingerprintSha256").toString().size() == 64;
        }), "alice should receive bob's online e2e identity announcement") && ok;
        ok = expect(alice.announceE2EIdentity(bobId, &rejectReason),
                    "alice should send a targeted e2e identity announcement to bob") && ok;
        ok = expect(waitFor([&] {
            return bobSawAliceIdentity.value("publicKeyFingerprintSha256").toString().size() == 64;
        }), "bob should receive alice's targeted e2e identity announcement") && ok;
        ok = expect(alice.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString().size() == 64
                        && alice.e2eLocalIdentityStatus().value("agreementSigning").toBool(false)
                        && alice.e2eLocalIdentityStatus().value("signatureSuite").toString()
                            == e2eAgreementSignatureSuite()
                        && alice.e2eLocalIdentityStatus().value("cryptoBackend").toObject()
                            .value("backendId").toString() == QStringLiteral("draft-qt-hmac-stream-v1")
                        && !alice.e2eLocalIdentityStatus().value("cryptoBackend").toObject()
                            .value("productionReady").toBool(true)
                        && !alice.e2eLocalIdentityStatus().contains("privateKey")
                        && !aliceSawBobIdentity.contains("privateKey")
                        && !bobSawAliceIdentity.contains("sessionKey"),
                    "e2e identity status should expose signing capability and fingerprints but no private or session keys") && ok;
        const QString originalAliceIdentityFingerprint = alice.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString();
        ok = expect(alice.e2ePeerIdentityStatus(bobId).value("trustState").toString() == QStringLiteral("unverified"),
                    "newly observed peer e2e identity should start unverified") && ok;
        ok = expect(!alice.pinE2EPeerIdentity(bobId, QStringLiteral("bad-fingerprint"), &rejectReason)
                        && rejectReason == QStringLiteral("fingerprint-mismatch")
                        && alice.e2ePeerIdentityStatus(bobId).value("trustState").toString() == QStringLiteral("mismatch"),
                    "pinning with an unexpected fingerprint should fail closed and mark mismatch") && ok;
        ok = expect(!alice.requestE2ESessionRotation(bobId, &rejectReason)
                        && rejectReason == QStringLiteral("fingerprint-mismatch"),
                    "key agreement should fail closed while the observed identity is mismatched") && ok;
        ok = expect(alice.pinE2EPeerIdentity(bobId,
                                             aliceSawBobIdentity.value("publicKeyFingerprintSha256").toString(),
                                             &rejectReason)
                        && alice.e2ePeerIdentityStatus(bobId).value("trustState").toString() == QStringLiteral("pending-verification")
                        && alice.e2ePeerIdentityStatus(bobId).value("pinPersisted").toBool(false),
                    "pinning the observed e2e identity fingerprint should persist pending verification state") && ok;
        ok = expect(!alice.requestE2ESessionRotation(bobId, &rejectReason)
                        && rejectReason == QStringLiteral("unverified-identity"),
                    "default e2e policy should block key agreement until the cross-device code is verified") && ok;
        const QString aliceBobVerificationCode = alice.e2ePeerIdentityStatus(bobId).value("verificationCode").toString();
        ok = expect(aliceBobVerificationCode.size() == 14
                        && aliceBobVerificationCode == bob.e2ePeerIdentityStatus(aliceId).value("verificationCode").toString(),
                    "both devices should compute the same cross-device verification code") && ok;
        ok = expect(!alice.verifyAndPinE2EPeerIdentity(bobId, QStringLiteral("0000-0000-0000"), &rejectReason)
                        && rejectReason == QStringLiteral("verification-code-mismatch"),
                    "wrong cross-device verification code should fail closed") && ok;
        ok = expect(alice.verifyAndPinE2EPeerIdentity(bobId,
                                                      aliceBobVerificationCode.toLower(),
                                                      &rejectReason)
                        && alice.e2ePeerIdentityStatus(bobId).value("trustState").toString() == QStringLiteral("trusted")
                        && alice.e2ePeerIdentityStatus(bobId).value("verified").toBool(false),
                    "matching cross-device verification code should promote the pin to trusted") && ok;
        ok = expect(!bob.requestE2ESessionRotation(aliceId, &rejectReason)
                        && rejectReason == QStringLiteral("untrusted-identity"),
                    "default e2e policy should block key agreement to an unpinned identity") && ok;
        ok = expect(bob.pinE2EPeerIdentity(aliceId,
                                           bobSawAliceIdentity.value("publicKeyFingerprintSha256").toString(),
                                           &rejectReason)
                        && bob.e2ePeerIdentityStatus(aliceId).value("trustState").toString() == QStringLiteral("pending-verification"),
                    "receiver should pin the sender before cross-device verification") && ok;
        ok = expect(bob.verifyAndPinE2EPeerIdentity(aliceId,
                                                    bob.e2ePeerIdentityStatus(aliceId).value("verificationCode").toString(),
                                                    &rejectReason)
                        && bob.e2ePeerIdentityStatus(aliceId).value("trustState").toString() == QStringLiteral("trusted")
                        && bob.e2ePeerIdentityStatus(aliceId).value("verified").toBool(false),
                    "receiver should verify the sender before accepting authenticated key agreement") && ok;
        ok = expect(!alice.announceE2EIdentity(aliceId, &rejectReason)
                        && rejectReason == QStringLiteral("invalid-peer"),
                    "clients should reject self-targeted e2e identity announcements") && ok;

        alice.setE2ESessionMessageLimitForTesting(2);
        bob.setE2ESessionMessageLimitForTesting(2);
        ok = expect(alice.requestE2ESessionRotation(bobId, &rejectReason),
                    "sender should start authenticated e2e key agreement") && ok;
        ok = expect(waitFor([&] {
            return bobRotationRequest.value("senderId").toString() == aliceId
                && bobRotationRequest.value("receiverId").toString() == bobId;
        }), "receiver should observe the authenticated e2e key agreement request") && ok;
        ok = expect(bobRotationRequest.value("keyId").toString().startsWith(QStringLiteral("rotate-"))
                        && bobRotationRequest.value("publicKey").toString().size() > 20
                        && bobRotationRequest.value("publicKeyFingerprintSha256").toString().size() == 64
                        && bobRotationRequest.value("senderIdentityFingerprintSha256").toString()
                            == bob.e2ePeerIdentityStatus(aliceId).value("publicKeyFingerprintSha256").toString()
                        && bobRotationRequest.value("receiverIdentityFingerprintSha256").toString()
                            == bob.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString()
                        && bobRotationRequest.value("signature").toString().size() > 20
                        && !bobRotationRequest.contains("sessionKey"),
                    "key agreement request should expose only public material bound to identity fingerprints") && ok;
        QJsonObject tamperedRequest = bobRotationRequest;
        tamperedRequest["publicKey"] = bobRotationRequest.value("publicKey").toString() + QStringLiteral("AA");
        ok = expect(!verifyE2EKeyAgreementSignature(E2EKeyAgreement::fromJson(tamperedRequest),
                                                    QByteArray("not-the-pinned-public-key"),
                                                    &rejectReason)
                        && (rejectReason == QStringLiteral("identity-key-mismatch")
                            || rejectReason == QStringLiteral("signature-mismatch")
                            || rejectReason == QStringLiteral("invalid-public-key")),
                    "tampered key agreement request should fail signature verification") && ok;
        ok = expect(bob.respondE2ESessionRotation(aliceId,
                                                  bobRotationRequest.value("keyId").toString() + QStringLiteral("-response"),
                                                  generateE2ESessionKey(),
                                                  true,
                                                  QStringLiteral("accepted"),
                                                  &rejectReason),
                    "receiver should accept and derive an authenticated e2e session") && ok;
        ok = expect(waitFor([&] {
            return aliceRotationAccepted
                && aliceRotationResponseReason == QStringLiteral("accepted")
                && aliceRotationResponse.value("senderId").toString() == bobId
                && aliceRotationResponse.value("receiverId").toString() == aliceId
                && alice.hasE2ESession(bobId)
                && bob.hasE2ESession(aliceId);
        }), "sender should install the derived session after the authenticated response") && ok;
        ok = expect(!aliceRotationResponse.contains("sessionKey")
                        && aliceRotationResponse.value("publicKeyFingerprintSha256").toString().size() == 64
                        && alice.e2eSessionStatus(bobId).value("keyId").toString()
                            == aliceRotationResponse.value("keyId").toString()
                        && bob.e2eSessionStatus(aliceId).value("keyId").toString()
                            == aliceRotationResponse.value("keyId").toString()
                        && alice.e2eSessionStatus(bobId).value("keyFingerprintSha256").toString()
                            == bob.e2eSessionStatus(aliceId).value("keyFingerprintSha256").toString(),
                    "both peers should install the same derived session without exposing the raw key") && ok;
        ok = expect(alice.hasE2ESession(bobId)
                        && alice.e2eSessionStatus(bobId).value("state").toString() == "ready"
                        && alice.e2eSessionStatus(bobId).value("keyFingerprintSha256").toString().size() == 64
                        && alice.e2eSessionStatus(bobId).value("cryptoBackend").toObject()
                            .value("backendId").toString() == QStringLiteral("draft-qt-hmac-stream-v1"),
                    "alice should expose ready e2e session and backend status without the raw key") && ok;

        ok = expect(!alice.sendEncryptedPrivateMessage(malloryId, "missing session should fail", &rejectReason)
                        && rejectReason == "missing-session",
                    "encrypted private send should fail closed without a session") && ok;

        const QString plaintext = "e2e private message plaintext";
        ok = expect(alice.sendEncryptedPrivateMessage(bobId, plaintext, &rejectReason),
                    "alice should send encrypted private message") && ok;
        ok = expect(waitFor([&] {
            return bobMessage.content == plaintext;
        }), "bob should receive decrypted private plaintext") && ok;
        ok = expect(bobMessage.e2eEnvelope.isValid(),
                    "bob should retain the validated e2e envelope metadata") && ok;
        ok = expect(bobMessage.e2eEnvelope.ciphertext != plaintext.toUtf8(),
                    "wire ciphertext should not equal plaintext") && ok;
        ok = expect(bobError.isEmpty(),
                    "bob should not report decrypt errors when the session key matches") && ok;
        ok = expect(aliceE2EStatus.value("encryptedMessages").toString() == "1",
                    "sender e2e status should count encrypted messages") && ok;
        ok = expect(bobE2EStatus.value("decryptedMessages").toString() == "1",
                    "receiver e2e status should count decrypted messages") && ok;

        ok = expect(alice.sendEncryptedPrivateMessage(bobId, "second encrypted message reaches rotation threshold", &rejectReason),
                    "second encrypted message should still send before rotation gate closes") && ok;
        ok = expect(waitFor([&] {
            return alice.e2eSessionNeedsRotation(bobId)
                && aliceE2EStatus.value("state").toString() == QStringLiteral("rotation-required");
        }), "sender should require rotation after the configured message limit") && ok;
        ok = expect(!alice.sendEncryptedPrivateMessage(bobId, "third encrypted message should be blocked", &rejectReason)
                        && rejectReason == QStringLiteral("rotation-required"),
                    "sender should fail closed once e2e session rotation is required") && ok;
        bobRotationRequest = QJsonObject();
        aliceRotationResponse = QJsonObject();
        aliceRotationAccepted = false;
        aliceRotationResponseReason.clear();
        ok = expect(alice.requestE2ESessionRotation(bobId, &rejectReason),
                    "sender should send an e2e rotation request after the local rotation gate closes") && ok;
        ok = expect(waitFor([&] {
            return bobRotationRequest.value("senderId").toString() == aliceId
                && bobRotationRequest.value("receiverId").toString() == bobId;
        }), "receiver should observe the e2e rotation request control-plane message") && ok;
        ok = expect(bobRotationRequest.value("keyId").toString().startsWith(QStringLiteral("rotate-"))
                        && bobRotationRequest.value("publicKey").toString().size() > 20
                        && bobRotationRequest.value("publicKeyFingerprintSha256").toString().size() == 64
                        && bobRotationRequest.value("senderIdentityFingerprintSha256").toString()
                            == bob.e2ePeerIdentityStatus(aliceId).value("publicKeyFingerprintSha256").toString()
                        && bobRotationRequest.value("receiverIdentityFingerprintSha256").toString()
                            == bob.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString()
                        && bobRotationRequest.value("signature").toString().size() > 20
                        && !bobRotationRequest.contains("sessionKey"),
                    "rotation request should expose only public agreement material bound to identity fingerprints") && ok;
        ok = expect(bob.respondE2ESessionRotation(aliceId,
                                                  bobRotationRequest.value("keyId").toString() + QStringLiteral("-response"),
                                                  generateE2ESessionKey(),
                                                  true,
                                                  QStringLiteral("accepted"),
                                                  &rejectReason),
                    "receiver should send an authenticated e2e rotation response") && ok;
        ok = expect(waitFor([&] {
            return aliceRotationAccepted
                && aliceRotationResponseReason == QStringLiteral("accepted")
                && aliceRotationResponse.value("senderId").toString() == bobId
                && aliceRotationResponse.value("receiverId").toString() == aliceId
                && alice.e2eSessionStatus(bobId).value("state").toString() == QStringLiteral("ready");
        }), "sender should install the authenticated e2e rotation response") && ok;
        ok = expect(!aliceRotationResponse.contains("sessionKey")
                        && aliceRotationResponse.value("signature").toString().size() > 20
                        && aliceRotationResponse.value("publicKeyFingerprintSha256").toString().size() == 64
                        && alice.e2eSessionStatus(bobId).value("keyFingerprintSha256").toString()
                            == bob.e2eSessionStatus(aliceId).value("keyFingerprintSha256").toString(),
                    "rotation response should not leak a raw session key") && ok;
        ok = expect(aliceRotationResponse.value("senderIdentityFingerprintSha256").toString()
                        == alice.e2ePeerIdentityStatus(bobId).value("publicKeyFingerprintSha256").toString()
                        && aliceRotationResponse.value("receiverIdentityFingerprintSha256").toString()
                            == alice.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString(),
                    "rotation response should be bound to the observed peer identity fingerprints") && ok;
        ok = expect(!alice.e2eSessionNeedsRotation(bobId),
                    "authenticated rotation response should clear the rotation gate") && ok;

        const QByteArray rotatedSessionKey = generateE2ESessionKey();
        alice.setE2ESessionKey(bobId, keyId + "-manual-rotation", rotatedSessionKey);
        bob.setE2ESessionKey(aliceId, keyId + "-manual-rotation", rotatedSessionKey);
        qputenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND", "production");
        const QJsonObject aliceProductionRequestedIdentity = alice.e2eLocalIdentityStatus();
        const QJsonObject aliceProductionRequestedPeer = alice.e2ePeerIdentityStatus(bobId);
        const QJsonObject aliceProductionRequestedSession = alice.e2eSessionStatus(bobId);
        ok = expect(aliceProductionRequestedIdentity.value("backendId").toString()
                        == QStringLiteral("draft-qt-hmac-stream-v1")
                        && aliceProductionRequestedIdentity.value("backendMigrationRequired").toBool(false)
                        && aliceProductionRequestedIdentity.value("blockedReason").toString()
                            == QStringLiteral("production-crypto-backend-unavailable")
                        && !aliceProductionRequestedIdentity.value("agreementSigning").toBool(true),
                    "draft local identity should expose migration-required evidence when production backend is requested") && ok;
        ok = expect(aliceProductionRequestedPeer.value("pinBackendId").toString()
                        == QStringLiteral("draft-qt-hmac-stream-v1")
                        && aliceProductionRequestedPeer.value("backendMigrationRequired").toBool(false)
                        && aliceProductionRequestedPeer.value("blockedReason").toString()
                            == QStringLiteral("production-crypto-backend-unavailable"),
                    "draft trust pin should expose migration-required evidence when production backend is requested") && ok;
        ok = expect(aliceProductionRequestedSession.value("backendId").toString()
                        == QStringLiteral("draft-qt-hmac-stream-v1")
                        && aliceProductionRequestedSession.value("state").toString()
                            == QStringLiteral("backend-migration-required")
                        && aliceProductionRequestedSession.value("backendMigrationRequired").toBool(false)
                        && !aliceProductionRequestedSession.value("ready").toBool(true),
                    "draft session should require backend migration when production backend is requested") && ok;
        ok = expect(!alice.sendEncryptedPrivateMessage(bobId,
                                                       "draft session must not send under production backend request",
                                                       &rejectReason)
                        && rejectReason == QStringLiteral("production-crypto-backend-unavailable"),
                    "production backend request should block sending with a draft session") && ok;
        ok = expect(!alice.requestE2ESessionRotation(bobId, &rejectReason)
                        && rejectReason == QStringLiteral("production-crypto-backend-unavailable"),
                    "production backend request should block draft key agreement rotation") && ok;
        const QString aliceIdentityPathBeforeClear = e2eIdentityFilePath(appDataDir, aliceId);
        const QString aliceTrustPinsPathBeforeClear = e2eTrustPinsFilePath(appDataDir, aliceId);
        ok = expect(QFile::exists(aliceIdentityPathBeforeClear)
                        && QFile::exists(aliceTrustPinsPathBeforeClear),
                    "test should have persisted draft identity and trust pin stores before migration recovery") && ok;
        const QJsonObject migrationPlan = alice.planE2EBackendMigration();
        ok = expect(migrationPlan.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-backend-migration-v1")
                        && migrationPlan.value("mode").toString() == QStringLiteral("plan")
                        && migrationPlan.value("migrationRequired").toBool(false)
                        && migrationPlan.value("releaseGate").toString()
                            == QStringLiteral("manual-e2e-backend-migration-required")
                        && migrationPlan.value("operatorAction").toString()
                            == QStringLiteral("execute-local-e2e-backend-migration-before-production-crypto")
                        && migrationPlan.value("identityStorePresent").toBool(false)
                        && migrationPlan.value("trustPinStorePresent").toBool(false)
                        && migrationPlan.value("pinnedPeerMigrationCount").toInt() >= 1
                        && migrationPlan.value("sessionMigrationCount").toInt() >= 1,
                    "migration plan should expose local draft identity, trust pin, and session migration evidence") && ok;
        const QByteArray migrationPlanJson = QJsonDocument(migrationPlan).toJson(QJsonDocument::Compact);
        ok = expect(!migrationPlanJson.contains("privateKey")
                        && !migrationPlanJson.contains("sessionKey")
                        && !migrationPlanJson.contains("publicKey\"")
                        && !migrationPlanJson.contains(originalAliceIdentityFingerprint.toUtf8()),
                    "migration plan should not leak private keys, raw session keys, public keys, or full identity fingerprints") && ok;
        const QJsonObject rotationDryRun = alice.planE2EProductionRotationDryRun();
        ok = expect(rotationDryRun.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-rotation-dry-run-v1")
                        && rotationDryRun.value("mode").toString() == QStringLiteral("dry-run")
                        && !rotationDryRun.value("destructive").toBool(true)
                        && !rotationDryRun.value("executed").toBool(true)
                        && !rotationDryRun.value("canRotateInPlace").toBool(true)
                        && rotationDryRun.value("releaseGate").toString()
                            == QStringLiteral("production-crypto-provider-not-ready")
                        && rotationDryRun.value("operatorAction").toString()
                            == QStringLiteral("complete-reviewed-production-provider-before-rotation")
                        && rotationDryRun.value("migrationRequired").toBool(false)
                        && rotationDryRun.value("pinnedPeerMigrationCount").toInt() >= 1
                        && rotationDryRun.value("sessionMigrationCount").toInt() >= 1
                        && !rotationDryRun.value("wouldClearLocalIdentityStore").toBool(true)
                        && !rotationDryRun.value("wouldClearTrustPinStore").toBool(true)
                        && !rotationDryRun.value("wouldDropActiveSessions").toBool(true)
                        && rotationDryRun.value("affectedPeerPins").toArray().size() >= 1
                        && rotationDryRun.value("affectedSessions").toArray().size() >= 1
                        && rotationDryRun.value("rotationStages").toArray().size() == 7
                        && rotationDryRun.value("blockedStageCount").toInt() == 7
                        && rotationDryRun.value("productionAcceptanceReleaseGate").toString()
                            == QStringLiteral("production-adapter-not-linked")
                        && !rotationDryRun.value("productionAcceptanceAccepted").toBool(true)
                        && rotationDryRun.value("productionAcceptanceBlockedOperationCount").toInt() == 8,
                    "production rotation dry-run should block until the provider is ready without clearing local state") && ok;
        const QJsonObject rotationExecutionPlan =
            rotationDryRun.value("productionOperationExecutionPlan").toObject();
        ok = expect(rotationExecutionPlan.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-operation-execution-plan-v1")
                        && rotationExecutionPlan.value("releaseGate").toString()
                            == QStringLiteral("production-operation-execution-plan-blocked-not-linked")
                        && rotationDryRun.value("productionExecutionPlanReleaseGate").toString()
                            == QStringLiteral("production-operation-execution-plan-blocked-not-linked")
                        && !rotationDryRun.value("productionExecutionPlanAccepted").toBool(true)
                        && rotationDryRun.value("productionExecutionPlanBlockedStepCount").toInt() == 8
                        && rotationExecutionPlan.value("steps").toArray().size() == 8
                        && rotationExecutionPlan.value("steps").toArray().at(0).toObject()
                            .value("fixtureHashSha256").toString().size() == 64,
                    "production rotation dry-run should embed sanitized execution plan evidence") && ok;
        const QJsonObject rotationInvocation =
            rotationDryRun.value("productionOperationInvocation").toObject();
        ok = expect(rotationInvocation.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-operation-invocation-v1")
                        && rotationInvocation.value("releaseGate").toString()
                            == QStringLiteral("production-operation-invocation-blocked-not-linked")
                        && rotationDryRun.value("productionInvocationReleaseGate").toString()
                            == QStringLiteral("production-operation-invocation-blocked-not-linked")
                        && !rotationDryRun.value("productionInvocationAccepted").toBool(true)
                        && rotationDryRun.value("productionInvocationBlockedOperationCount").toInt() == 8
                        && rotationInvocation.value("invocations").toArray().size() == 8
                        && rotationInvocation.value("invocations").toArray().at(0).toObject()
                            .value("inputContract").toArray().at(0).toString()
                                == QStringLiteral("secure-random-source"),
                    "production rotation dry-run should embed sanitized invocation contract evidence") && ok;
        const QJsonObject rotationSlots =
            rotationDryRun.value("productionOperationSlots").toObject();
        ok = expect(rotationSlots.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-operation-slots-v1")
                        && rotationSlots.value("releaseGate").toString()
                            == QStringLiteral("production-operation-slots-blocked-not-linked")
                        && rotationDryRun.value("productionSlotsReleaseGate").toString()
                            == QStringLiteral("production-operation-slots-blocked-not-linked")
                        && !rotationDryRun.value("productionSlotsAccepted").toBool(true)
                        && rotationDryRun.value("productionSlotsBlockedSlotCount").toInt() == 8
                        && rotationSlots.value("slots").toArray().size() == 8
                        && rotationSlots.value("slots").toArray().at(0).toObject()
                            .value("providerSymbol").toString()
                                == QStringLiteral("qnc_e2e_op_session_key_generation_v1"),
                    "production rotation dry-run should embed sanitized operation slot evidence") && ok;
        const QJsonObject rotationDispatchBindings =
            rotationDryRun.value("productionOperationDispatchBindings").toObject();
        ok = expect(rotationDispatchBindings.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-operation-dispatch-bindings-v1")
                        && rotationDispatchBindings.value("releaseGate").toString()
                            == QStringLiteral("production-operation-dispatch-bindings-blocked-not-linked")
                        && rotationDryRun.value("productionDispatchBindingsReleaseGate").toString()
                            == QStringLiteral("production-operation-dispatch-bindings-blocked-not-linked")
                        && !rotationDryRun.value("productionDispatchBindingsAccepted").toBool(true)
                        && rotationDryRun.value("productionDispatchBindingsBlockedBindingCount").toInt() == 8
                        && rotationDispatchBindings.value("bindings").toArray().size() == 8
                        && rotationDispatchBindings.value("bindings").toArray().at(0).toObject()
                            .value("expectedSignature").toString()
                                .contains(QStringLiteral("qnc_e2e_op_session_key_generation_v1")),
                    "production rotation dry-run should embed sanitized dispatch binding evidence") && ok;
        const QJsonObject rotationExecutionResult =
            rotationDryRun.value("productionOperationExecutionResult").toObject();
        ok = expect(rotationExecutionResult.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-operation-execution-result-v1")
                        && rotationExecutionResult.value("releaseGate").toString()
                            == QStringLiteral("production-operation-execution-results-blocked-not-linked")
                        && rotationDryRun.value("productionExecutionResultReleaseGate").toString()
                            == QStringLiteral("production-operation-execution-results-blocked-not-linked")
                        && !rotationDryRun.value("productionExecutionResultAccepted").toBool(true)
                        && rotationDryRun.value("productionExecutionResultBlockedResultCount").toInt() == 8
                        && rotationDryRun.value("productionExecutionResultPassedResultCount").toInt() == 0
                        && rotationExecutionResult.value("results").toArray().size() == 8
                        && rotationExecutionResult.value("results").toArray().at(0).toObject()
                            .value("resultContract").toArray().size() == 4,
                    "production rotation dry-run should embed sanitized execution result evidence") && ok;
        const QJsonObject rotationProviderTable =
            rotationDryRun.value("productionProviderTable").toObject();
        ok = expect(rotationProviderTable.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-table-v1")
                        && rotationProviderTable.value("releaseGate").toString()
                            == QStringLiteral("production-provider-table-blocked-not-linked")
                        && rotationDryRun.value("productionProviderTableReleaseGate").toString()
                            == QStringLiteral("production-provider-table-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderTableAccepted").toBool(true)
                        && rotationDryRun.value("productionProviderTableBoundSymbolCount").toInt() == 0
                        && rotationDryRun.value("productionProviderTableMissingSymbolCount").toInt() == 8
                        && rotationProviderTable.value("entries").toArray().size() == 8
                        && rotationProviderTable.value("entries").toArray().at(0).toObject()
                            .value("requiredSymbol").toString()
                                == QStringLiteral("qnc_e2e_op_session_key_generation_v1"),
                    "production rotation dry-run should embed provider table binding evidence") && ok;
        const QJsonObject rotationProviderTableBindingProbe =
            rotationDryRun.value("productionProviderTableBindingProbe").toObject();
        ok = expect(rotationProviderTableBindingProbe.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-table-binding-probe-v1")
                        && rotationProviderTableBindingProbe.value("releaseGate").toString()
                            == QStringLiteral("production-provider-table-binding-blocked-not-linked")
                        && rotationDryRun.value("productionProviderTableBindingProbeReleaseGate").toString()
                            == QStringLiteral("production-provider-table-binding-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderTableBindingProbeAccepted").toBool(true)
                        && rotationProviderTableBindingProbe.value("enumMappings").toArray().size() == 8
                        && rotationProviderTableBindingProbe.value("fieldOffsets").toArray().size() == 5,
                    "production rotation dry-run should embed provider table binding probe evidence") && ok;
        const QJsonObject rotationProviderTableRegistration =
            rotationDryRun.value("productionProviderTableRegistration").toObject();
        ok = expect(rotationProviderTableRegistration.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-table-registration-v1")
                        && rotationProviderTableRegistration.value("releaseGate").toString()
                            == QStringLiteral("production-provider-table-registration-blocked-not-linked")
                        && rotationDryRun.value("productionProviderTableRegistrationReleaseGate").toString()
                            == QStringLiteral("production-provider-table-registration-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderTableRegistrationAccepted").toBool(true)
                        && !rotationDryRun.value("productionProviderTableRegistered").toBool(true)
                        && rotationProviderTableRegistration.value("blockedReason").toString()
                            == QStringLiteral("production-provider-table-not-registered")
                        && rotationProviderTableRegistration.value("tableValidation").toObject()
                            .value("blockedReason").toString()
                                == QStringLiteral("production-provider-table-not-bound"),
                    "production rotation dry-run should embed provider table registration evidence") && ok;
        const QJsonObject rotationProviderOperationPreflight =
            rotationDryRun.value("productionProviderOperationPreflight").toObject();
        ok = expect(rotationProviderOperationPreflight.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-operation-preflight-v1")
                        && rotationProviderOperationPreflight.value("releaseGate").toString()
                            == QStringLiteral("production-provider-operation-preflight-blocked-not-linked")
                        && rotationDryRun.value("productionProviderOperationPreflightReleaseGate").toString()
                            == QStringLiteral("production-provider-operation-preflight-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderOperationPreflightAccepted").toBool(true)
                        && rotationDryRun.value("productionProviderOperationPreflightBlockedOperationCount").toInt() == 8
                        && rotationProviderOperationPreflight.value("presentOperationCount").toInt() == 0
                        && rotationProviderOperationPreflight.value("operations").toArray().size() == 8
                        && !rotationProviderOperationPreflight.value("operationInvoked").toBool(true),
                    "production rotation dry-run should embed provider operation preflight evidence") && ok;
        const QJsonObject rotationProviderCallFrame =
            rotationDryRun.value("productionProviderCallFrame").toObject();
        ok = expect(rotationProviderCallFrame.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-call-frame-v1")
                        && rotationProviderCallFrame.value("releaseGate").toString()
                            == QStringLiteral("production-provider-call-frame-blocked-not-linked")
                        && rotationDryRun.value("productionProviderCallFrameReleaseGate").toString()
                            == QStringLiteral("production-provider-call-frame-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderCallFrameAccepted").toBool(true)
                        && rotationDryRun.value("productionProviderCallFrameBlockedFrameCount").toInt() == 8
                        && rotationProviderCallFrame.value("frames").toArray().size() == 8
                        && !rotationProviderCallFrame.value("operationInvoked").toBool(true)
                        && !rotationProviderCallFrame.value("inputBytesAttached").toBool(true)
                        && !rotationProviderCallFrame.value("outputBytesAttached").toBool(true),
                    "production rotation dry-run should embed provider call frame evidence") && ok;
        const QJsonObject rotationProviderInvocationDryRun =
            rotationDryRun.value("productionProviderInvocationDryRun").toObject();
        ok = expect(rotationProviderInvocationDryRun.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-dry-run-v1")
                        && rotationProviderInvocationDryRun.value("releaseGate").toString()
                            == QStringLiteral("production-provider-invocation-dry-run-blocked-not-linked")
                        && rotationDryRun.value("productionProviderInvocationDryRunReleaseGate").toString()
                            == QStringLiteral("production-provider-invocation-dry-run-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderInvocationDryRunAccepted").toBool(true)
                        && rotationDryRun.value("productionProviderInvocationDryRunBlockedInvocationCount").toInt() == 8
                        && rotationProviderInvocationDryRun.value("invocations").toArray().size() == 8
                        && !rotationProviderInvocationDryRun.value("operationInvoked").toBool(true),
                    "production rotation dry-run should embed provider invocation dry-run evidence") && ok;
        const QJsonObject rotationProviderInvocationResult =
            rotationDryRun.value("productionProviderInvocationResult").toObject();
        ok = expect(rotationProviderInvocationResult.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-result-v1")
                        && rotationProviderInvocationResult.value("releaseGate").toString()
                            == QStringLiteral("production-provider-invocation-results-blocked-not-linked")
                        && rotationDryRun.value("productionProviderInvocationResultReleaseGate").toString()
                            == QStringLiteral("production-provider-invocation-results-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderInvocationResultAccepted").toBool(true)
                        && rotationDryRun.value("productionProviderInvocationResultBlockedResultCount").toInt() == 8
                        && rotationProviderInvocationResult.value("results").toArray().size() == 8
                        && rotationProviderInvocationResult.value("providerInvocationDryRunReleaseGate").toString()
                            == QStringLiteral("production-provider-invocation-dry-run-blocked-not-linked")
                        && !rotationProviderInvocationResult.value("operationInvoked").toBool(true)
                        && !rotationProviderInvocationResult.value("resultCaptured").toBool(true),
                    "production rotation dry-run should embed provider invocation result evidence") && ok;
        const QJsonObject rotationProviderExecutionDecision =
            rotationDryRun.value("productionProviderExecutionDecision").toObject();
        ok = expect(rotationProviderExecutionDecision.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-execution-decision-v1")
                        && rotationProviderExecutionDecision.value("releaseGate").toString()
                            == QStringLiteral("production-provider-execution-decision-blocked-not-linked")
                        && rotationDryRun.value("productionProviderExecutionDecisionReleaseGate").toString()
                            == QStringLiteral("production-provider-execution-decision-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderExecutionDecisionAccepted").toBool(true)
                        && rotationDryRun.value("productionProviderExecutionDecisionBlockedDecisionCount").toInt() == 8
                        && rotationProviderExecutionDecision.value("decisions").toArray().size() == 8
                        && !rotationProviderExecutionDecision.value("operationInvoked").toBool(true)
                        && !rotationProviderExecutionDecision.value("resultCaptured").toBool(true),
                    "production rotation dry-run should embed provider execution decision evidence") && ok;
        const QJsonObject rotationProviderCallbackHarness =
            rotationDryRun.value("productionProviderCallbackHarness").toObject();
        ok = expect(rotationProviderCallbackHarness.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-callback-harness-v1")
                        && rotationProviderCallbackHarness.value("releaseGate").toString()
                            == QStringLiteral("production-provider-callback-harness-blocked-not-linked")
                        && rotationDryRun.value("productionProviderCallbackHarnessReleaseGate").toString()
                            == QStringLiteral("production-provider-callback-harness-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderCallbackHarnessAccepted").toBool(true)
                        && rotationDryRun.value("productionProviderCallbackHarnessBlockedCallbackCount").toInt() == 8
                        && rotationProviderCallbackHarness.value("callbacks").toArray().size() == 8
                        && rotationProviderCallbackHarness.value("providerExecutionDecisionReleaseGate").toString()
                            == QStringLiteral("production-provider-execution-decision-blocked-not-linked")
                        && !rotationProviderCallbackHarness.value("operationInvoked").toBool(true)
                        && !rotationProviderCallbackHarness.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderCallbackHarness.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderCallbackHarness.value("resultCaptured").toBool(true),
                    "production rotation dry-run should embed provider callback harness evidence") && ok;
        const QJsonObject rotationProviderVectorSelfTest =
            rotationDryRun.value("productionProviderVectorSelfTest").toObject();
        ok = expect(rotationProviderVectorSelfTest.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-vector-self-test-v1")
                        && rotationProviderVectorSelfTest.value("releaseGate").toString()
                            == QStringLiteral("production-provider-vector-self-test-blocked-not-linked")
                        && rotationDryRun.value("productionProviderVectorSelfTestReleaseGate").toString()
                            == QStringLiteral("production-provider-vector-self-test-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderVectorSelfTestAccepted").toBool(true)
                        && rotationDryRun.value("productionProviderVectorSelfTestBlockedVectorCount").toInt() == 8
                        && rotationProviderVectorSelfTest.value("tests").toArray().size() == 8
                        && rotationProviderVectorSelfTest.value("providerCallbackHarnessReleaseGate").toString()
                            == QStringLiteral("production-provider-callback-harness-blocked-not-linked")
                        && !rotationProviderVectorSelfTest.value("operationInvoked").toBool(true)
                        && !rotationProviderVectorSelfTest.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderVectorSelfTest.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderVectorSelfTest.value("resultCaptured").toBool(true),
                    "production rotation dry-run should embed provider vector self-test evidence") && ok;
        const QJsonObject rotationProviderExecutionSlotBinding =
            rotationDryRun.value("productionProviderExecutionSlotBinding").toObject();
        ok = expect(rotationProviderExecutionSlotBinding.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-execution-slot-binding-v1")
                        && rotationProviderExecutionSlotBinding.value("releaseGate").toString()
                            == QStringLiteral("production-provider-execution-slot-binding-blocked-not-linked")
                        && rotationDryRun.value("productionProviderExecutionSlotBindingReleaseGate").toString()
                            == QStringLiteral("production-provider-execution-slot-binding-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderExecutionSlotBindingAccepted").toBool(true)
                        && rotationDryRun.value("productionProviderExecutionSlotBindingBlockedSlotCount").toInt() == 8
                        && rotationProviderExecutionSlotBinding.value("slots").toArray().size() == 8
                        && rotationProviderExecutionSlotBinding.value("providerVectorSelfTestReleaseGate").toString()
                            == QStringLiteral("production-provider-vector-self-test-blocked-not-linked")
                        && !rotationProviderExecutionSlotBinding.value("operationInvoked").toBool(true)
                        && !rotationProviderExecutionSlotBinding.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderExecutionSlotBinding.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderExecutionSlotBinding.value("resultCaptured").toBool(true),
                    "production rotation dry-run should embed provider execution slot binding evidence") && ok;
        const QJsonObject rotationProviderExecutionPath =
            rotationDryRun.value("productionProviderExecutionPath").toObject();
        ok = expect(rotationProviderExecutionPath.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-execution-path-v1")
                        && rotationProviderExecutionPath.value("releaseGate").toString()
                            == QStringLiteral("production-provider-execution-path-blocked-not-linked")
                        && rotationDryRun.value("productionProviderExecutionPathReleaseGate").toString()
                            == QStringLiteral("production-provider-execution-path-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderExecutionPathAccepted").toBool(true)
                        && rotationDryRun.value("productionProviderExecutionPathBlockedPathCount").toInt() == 8
                        && rotationProviderExecutionPath.value("paths").toArray().size() == 8
                        && rotationProviderExecutionPath.value("providerExecutionSlotBindingReleaseGate").toString()
                            == QStringLiteral("production-provider-execution-slot-binding-blocked-not-linked")
                        && !rotationProviderExecutionPath.value("operationInvoked").toBool(true)
                        && !rotationProviderExecutionPath.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderExecutionPath.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderExecutionPath.value("resultCaptured").toBool(true),
                    "production rotation dry-run should embed provider execution path evidence") && ok;
        const QJsonObject rotationProviderInvocationSandbox =
            rotationDryRun.value("productionProviderInvocationSandbox").toObject();
        ok = expect(rotationProviderInvocationSandbox.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-sandbox-v1")
                        && rotationProviderInvocationSandbox.value("releaseGate").toString()
                            == QStringLiteral("production-provider-invocation-sandbox-blocked-not-linked")
                        && rotationDryRun.value("productionProviderInvocationSandboxReleaseGate").toString()
                            == QStringLiteral("production-provider-invocation-sandbox-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderInvocationSandboxAccepted").toBool(true)
                        && rotationDryRun.value("productionProviderInvocationSandboxBlockedSandboxCount").toInt() == 8
                        && rotationProviderInvocationSandbox.value("sandboxes").toArray().size() == 8
                        && rotationProviderInvocationSandbox.value("providerExecutionPathReleaseGate").toString()
                            == QStringLiteral("production-provider-execution-path-blocked-not-linked")
                        && !rotationProviderInvocationSandbox.value("operationInvoked").toBool(true)
                        && !rotationProviderInvocationSandbox.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderInvocationSandbox.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderInvocationSandbox.value("resultCaptured").toBool(true),
                    "production rotation dry-run should embed provider invocation sandbox evidence") && ok;
        const QJsonObject rotationProviderInvocationVectorResult =
            rotationDryRun.value("productionProviderInvocationVectorResult").toObject();
        ok = expect(rotationProviderInvocationVectorResult.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-vector-result-v1")
                        && rotationProviderInvocationVectorResult.value("releaseGate").toString()
                            == QStringLiteral("production-provider-invocation-vector-result-blocked-not-linked")
                        && rotationDryRun.value("productionProviderInvocationVectorResultReleaseGate").toString()
                            == QStringLiteral("production-provider-invocation-vector-result-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderInvocationVectorResultAccepted").toBool(true)
                        && rotationDryRun.value("productionProviderInvocationVectorResultBlockedVectorResultCount").toInt() == 8
                        && rotationProviderInvocationVectorResult.value("vectorResults").toArray().size() == 8
                        && rotationProviderInvocationVectorResult.value("providerInvocationSandboxReleaseGate").toString()
                            == QStringLiteral("production-provider-invocation-sandbox-blocked-not-linked")
                        && !rotationProviderInvocationVectorResult.value("operationInvoked").toBool(true)
                        && !rotationProviderInvocationVectorResult.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderInvocationVectorResult.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderInvocationVectorResult.value("resultCaptured").toBool(true),
                    "production rotation dry-run should embed provider invocation vector result evidence") && ok;
        const QJsonObject rotationProviderInvocationExecution =
            rotationDryRun.value("productionProviderInvocationExecution").toObject();
        ok = expect(rotationProviderInvocationExecution.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-execution-v1")
                        && rotationProviderInvocationExecution.value("releaseGate").toString()
                            == QStringLiteral("production-provider-invocation-execution-blocked-not-linked")
                        && rotationDryRun.value("productionProviderInvocationExecutionReleaseGate").toString()
                            == QStringLiteral("production-provider-invocation-execution-blocked-not-linked")
                        && !rotationDryRun.value("productionProviderInvocationExecutionAccepted").toBool(true)
                        && rotationDryRun.value("productionProviderInvocationExecutionBlockedExecutionCount").toInt() == 8
                        && rotationProviderInvocationExecution.value("executions").toArray().size() == 8
                        && rotationProviderInvocationExecution.value("providerInvocationVectorResultReleaseGate").toString()
                            == QStringLiteral("production-provider-invocation-vector-result-blocked-not-linked")
                        && !rotationProviderInvocationExecution.value("operationInvoked").toBool(true)
                        && !rotationProviderInvocationExecution.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderInvocationExecution.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderInvocationExecution.value("resultCaptured").toBool(true),
                    "production rotation dry-run should embed provider invocation execution evidence") && ok;
        const QJsonObject rotationProviderReviewedExecutionCandidate =
            rotationDryRun.value("productionProviderReviewedExecutionCandidate").toObject();
        ok = expect(rotationProviderReviewedExecutionCandidate.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-execution-candidate-v1")
                        && rotationProviderReviewedExecutionCandidate.value("releaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-execution-candidate-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedExecutionCandidateReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-execution-candidate-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedExecutionCandidateNonReleaseGate").toBool(false)
                        && rotationDryRun.value("productionProviderReviewedExecutionCandidateReadyCount").toInt() == 0
                        && rotationDryRun.value("productionProviderReviewedExecutionCandidateBlockedCount").toInt() == 8
                        && !rotationProviderReviewedExecutionCandidate.value("probeSourceCaptured").toBool(true)
                        && rotationProviderReviewedExecutionCandidate.value("candidates").toArray().size() == 8
                        && !rotationProviderReviewedExecutionCandidate.value("operationInvokedByCandidate").toBool(true)
                        && !rotationProviderReviewedExecutionCandidate.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedExecutionCandidate.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedExecutionCandidate.value("rawKeyExported").toBool(true)
                        && !rotationProviderReviewedExecutionCandidate.value("privateMaterialExported").toBool(true),
                    "production rotation dry-run should embed reviewed execution candidate evidence without invoking provider callbacks") && ok;
        const QJsonObject rotationProviderReviewedCallHandoff =
            rotationDryRun.value("productionProviderReviewedCallHandoff").toObject();
        ok = expect(rotationProviderReviewedCallHandoff.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-call-handoff-v1")
                        && rotationProviderReviewedCallHandoff.value("releaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-call-handoff-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedCallHandoffReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-call-handoff-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedCallHandoffNonReleaseGate").toBool(false)
                        && rotationDryRun.value("productionProviderReviewedCallHandoffReadyCount").toInt() == 0
                        && rotationDryRun.value("productionProviderReviewedCallHandoffBlockedCount").toInt() == 8
                        && !rotationProviderReviewedCallHandoff.value("candidateSourceCaptured").toBool(true)
                        && rotationProviderReviewedCallHandoff.value("handoffs").toArray().size() == 8
                        && !rotationProviderReviewedCallHandoff.value("operationInvokedByHandoff").toBool(true)
                        && !rotationProviderReviewedCallHandoff.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedCallHandoff.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedCallHandoff.value("resultCaptured").toBool(true)
                        && !rotationProviderReviewedCallHandoff.value("rawKeyExported").toBool(true)
                        && !rotationProviderReviewedCallHandoff.value("privateMaterialExported").toBool(true),
                    "production rotation dry-run should embed reviewed call handoff evidence without invoking provider callbacks") && ok;
        const QJsonObject rotationProviderReviewedOperationStubBoundary =
            rotationDryRun.value("productionProviderReviewedOperationStubBoundary").toObject();
        const QJsonObject rotationProviderReviewedCallableTableBridge =
            rotationDryRun.value("productionProviderReviewedCallableTableBridge").toObject();
        const QJsonObject rotationProviderReviewedOperationCallableInterface =
            rotationDryRun.value("productionProviderReviewedOperationCallableInterface").toObject();
        const QJsonObject rotationProviderReviewedCallableRuntimePreflight =
            rotationDryRun.value("productionProviderReviewedCallableRuntimePreflight").toObject();
        const QJsonObject rotationProviderReviewedInvocationArming =
            rotationDryRun.value("productionProviderReviewedInvocationArming").toObject();
        const QJsonObject rotationProviderReviewedInvocationExecutionAcceptance =
            rotationDryRun.value("productionProviderReviewedInvocationExecutionAcceptance").toObject();
        ok = expect(rotationProviderReviewedOperationStubBoundary.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-operation-stub-boundary-v1")
                        && rotationProviderReviewedOperationStubBoundary.value("releaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-operation-stub-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedOperationStubBoundaryReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-operation-stub-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedOperationStubBoundaryNonReleaseGate").toBool(false)
                        && rotationDryRun.value("productionProviderReviewedOperationStubBoundaryReadyCount").toInt() == 0
                        && rotationDryRun.value("productionProviderReviewedOperationStubBoundaryBlockedCount").toInt() == 8
                        && !rotationProviderReviewedOperationStubBoundary.value("handoffSourceCaptured").toBool(true)
                        && rotationProviderReviewedOperationStubBoundary.value("stubs").toArray().size() == 8
                        && !rotationProviderReviewedOperationStubBoundary.value("operationInvokedByStub").toBool(true)
                        && !rotationProviderReviewedOperationStubBoundary.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedOperationStubBoundary.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedOperationStubBoundary.value("resultCaptured").toBool(true)
                        && !rotationProviderReviewedOperationStubBoundary.value("rawKeyExported").toBool(true)
                        && !rotationProviderReviewedOperationStubBoundary.value("privateMaterialExported").toBool(true),
                    "production rotation dry-run should embed reviewed operation stub evidence without invoking provider callbacks") && ok;
        ok = expect(rotationProviderReviewedCallableTableBridge.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-callable-table-bridge-v1")
                        && rotationProviderReviewedCallableTableBridge.value("releaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-callable-table-bridge-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedCallableTableBridgeReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-callable-table-bridge-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedCallableTableBridgeNonReleaseGate").toBool(false)
                        && rotationDryRun.value("productionProviderReviewedCallableTableBridgeReadyCount").toInt() == 0
                        && rotationDryRun.value("productionProviderReviewedCallableTableBridgeBlockedCount").toInt() == 8
                        && !rotationProviderReviewedCallableTableBridge.value("stubSourceCaptured").toBool(true)
                        && rotationProviderReviewedCallableTableBridge.value("bridges").toArray().size() == 8
                        && !rotationProviderReviewedCallableTableBridge.value("operationInvokedByBridge").toBool(true)
                        && !rotationProviderReviewedCallableTableBridge.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedCallableTableBridge.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedCallableTableBridge.value("resultCaptured").toBool(true)
                        && !rotationProviderReviewedCallableTableBridge.value("rawKeyExported").toBool(true)
                        && !rotationProviderReviewedCallableTableBridge.value("privateMaterialExported").toBool(true),
                    "production rotation dry-run should embed reviewed callable table bridge evidence without invoking provider callbacks") && ok;
        ok = expect(rotationProviderReviewedOperationCallableInterface.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-operation-callable-interface-v1")
                        && rotationProviderReviewedOperationCallableInterface.value("releaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-operation-callable-interface-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedOperationCallableInterfaceReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-operation-callable-interface-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedOperationCallableInterfaceNonReleaseGate").toBool(false)
                        && rotationDryRun.value("productionProviderReviewedOperationCallableInterfaceReadyCount").toInt() == 0
                        && rotationDryRun.value("productionProviderReviewedOperationCallableInterfaceBlockedCount").toInt() == 8
                        && !rotationProviderReviewedOperationCallableInterface.value("bridgeSourceCaptured").toBool(true)
                        && rotationProviderReviewedOperationCallableInterface.value("interfaces").toArray().size() == 8
                        && !rotationProviderReviewedOperationCallableInterface.value("operationInvokedByInterface").toBool(true)
                        && !rotationProviderReviewedOperationCallableInterface.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedOperationCallableInterface.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedOperationCallableInterface.value("resultCaptured").toBool(true)
                        && !rotationProviderReviewedOperationCallableInterface.value("rawKeyExported").toBool(true)
                        && !rotationProviderReviewedOperationCallableInterface.value("privateMaterialExported").toBool(true),
                    "production rotation dry-run should embed reviewed operation callable interface evidence without invoking provider callbacks") && ok;
        ok = expect(rotationProviderReviewedCallableRuntimePreflight.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-callable-runtime-preflight-v1")
                        && rotationProviderReviewedCallableRuntimePreflight.value("releaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-callable-runtime-preflight-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedCallableRuntimePreflightReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-callable-runtime-preflight-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedCallableRuntimePreflightNonReleaseGate").toBool(false)
                        && rotationDryRun.value("productionProviderReviewedCallableRuntimePreflightReadyCount").toInt() == 0
                        && rotationDryRun.value("productionProviderReviewedCallableRuntimePreflightBlockedCount").toInt() == 8
                        && !rotationProviderReviewedCallableRuntimePreflight.value("interfaceSourceCaptured").toBool(true)
                        && rotationProviderReviewedCallableRuntimePreflight.value("preflights").toArray().size() == 8
                        && !rotationProviderReviewedCallableRuntimePreflight.value("operationInvokedByRuntimePreflight").toBool(true)
                        && !rotationProviderReviewedCallableRuntimePreflight.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedCallableRuntimePreflight.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedCallableRuntimePreflight.value("resultCaptured").toBool(true)
                        && !rotationProviderReviewedCallableRuntimePreflight.value("rawKeyExported").toBool(true)
                        && !rotationProviderReviewedCallableRuntimePreflight.value("privateMaterialExported").toBool(true),
                    "production rotation dry-run should embed reviewed callable runtime preflight evidence without invoking provider callbacks") && ok;
        ok = expect(rotationProviderReviewedInvocationArming.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-invocation-arming-v1")
                        && rotationProviderReviewedInvocationArming.value("releaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-invocation-arming-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedInvocationArmingReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-invocation-arming-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedInvocationArmingNonReleaseGate").toBool(false)
                        && rotationDryRun.value("productionProviderReviewedInvocationArmingReadyCount").toInt() == 0
                        && rotationDryRun.value("productionProviderReviewedInvocationArmingBlockedCount").toInt() == 8
                        && !rotationProviderReviewedInvocationArming.value("runtimePreflightSourceCaptured").toBool(true)
                        && rotationProviderReviewedInvocationArming.value("armings").toArray().size() == 8
                        && !rotationProviderReviewedInvocationArming.value("operationInvokedByArming").toBool(true)
                        && !rotationProviderReviewedInvocationArming.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedInvocationArming.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedInvocationArming.value("resultCaptured").toBool(true)
                        && !rotationProviderReviewedInvocationArming.value("rawKeyExported").toBool(true)
                        && !rotationProviderReviewedInvocationArming.value("privateMaterialExported").toBool(true),
                    "production rotation dry-run should embed reviewed invocation arming evidence without invoking provider callbacks") && ok;
        ok = expect(rotationProviderReviewedInvocationExecutionAcceptance.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-invocation-execution-acceptance-v1")
                        && rotationProviderReviewedInvocationExecutionAcceptance.value("releaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedInvocationExecutionAcceptanceReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-not-release-gate")
                        && rotationDryRun.value("productionProviderReviewedInvocationExecutionAcceptanceNonReleaseGate").toBool(false)
                        && rotationDryRun.value("productionProviderReviewedInvocationExecutionAcceptanceReadyCount").toInt() == 0
                        && rotationDryRun.value("productionProviderReviewedInvocationExecutionAcceptanceBlockedCount").toInt() == 8
                        && !rotationProviderReviewedInvocationExecutionAcceptance.value("armingSourceCaptured").toBool(true)
                        && rotationProviderReviewedInvocationExecutionAcceptance.value("acceptances").toArray().size() == 8
                        && !rotationProviderReviewedInvocationExecutionAcceptance.value("operationInvokedByExecutionAcceptance").toBool(true)
                        && !rotationProviderReviewedInvocationExecutionAcceptance.value("inputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedInvocationExecutionAcceptance.value("outputBytesCaptured").toBool(true)
                        && !rotationProviderReviewedInvocationExecutionAcceptance.value("resultCaptured").toBool(true)
                        && !rotationProviderReviewedInvocationExecutionAcceptance.value("rawKeyExported").toBool(true)
                        && !rotationProviderReviewedInvocationExecutionAcceptance.value("privateMaterialExported").toBool(true),
                    "production rotation dry-run should embed reviewed invocation execution acceptance without invoking provider callbacks") && ok;
        const QJsonObject rotationAcceptance =
            rotationDryRun.value("productionAcceptance").toObject();
        ok = expect(rotationAcceptance.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-crypto-acceptance-v1")
                        && rotationAcceptance.value("releaseGate").toString()
                            == QStringLiteral("production-adapter-not-linked")
                        && rotationAcceptance.value("blockedReason").toString()
                            == QStringLiteral("production-crypto-backend-unavailable")
                        && rotationAcceptance.value("operationGates").toArray().size() == 8
                        && rotationAcceptance.value("operationManifest").toArray().size() == 8
                        && rotationAcceptance.value("operationHarness").toObject()
                            .value("operations").toArray().size() == 8
                        && rotationAcceptance.value("operationDispatchBindings").toObject()
                            .value("bindings").toArray().size() == 8
                        && rotationAcceptance.value("providerTable").toObject()
                            .value("missingSymbolCount").toInt() == 8
                        && rotationAcceptance.value("providerTableBindingProbe").toObject()
                            .value("enumMappings").toArray().size() == 8
                        && rotationAcceptance.value("providerTableRegistration").toObject()
                            .value("registered").toBool(true) == false
                        && !rotationAcceptance.value("providerTableRegistrationAccepted").toBool(true)
                        && rotationAcceptance.value("providerOperationPreflight").toObject()
                            .value("blockedOperationCount").toInt() == 8
                        && !rotationAcceptance.value("providerOperationPreflightAccepted").toBool(true)
                        && rotationAcceptance.value("providerCallFrame").toObject()
                            .value("blockedFrameCount").toInt() == 8
                        && !rotationAcceptance.value("providerCallFrameAccepted").toBool(true)
                        && rotationAcceptance.value("providerInvocationDryRun").toObject()
                            .value("blockedInvocationCount").toInt() == 8
                        && !rotationAcceptance.value("providerInvocationDryRunAccepted").toBool(true)
                        && rotationAcceptance.value("providerInvocationResult").toObject()
                            .value("blockedResultCount").toInt() == 8
                        && !rotationAcceptance.value("providerInvocationResultAccepted").toBool(true)
                        && rotationAcceptance.value("implementedOperationCount").toInt() == 0,
                    "production rotation dry-run should embed sanitized acceptance evidence") && ok;
        const QJsonObject rotationHarness =
            rotationAcceptance.value("operationHarness").toObject();
        ok = expect(rotationHarness.value("releaseGate").toString()
                            == QStringLiteral("production-operation-harness-blocked-not-linked")
                        && rotationHarness.value("blockedOperationCount").toInt() == 8
                        && rotationHarness.value("operations").toArray().at(0).toObject()
                            .value("fixtureHashSha256").toString().size() == 64,
                    "production rotation acceptance should carry operation harness evidence") && ok;
        const QJsonObject rotationAcceptanceFirstGate =
            rotationAcceptance.value("operationGates").toArray().at(0).toObject();
        ok = expect(rotationAcceptanceFirstGate.value("implementationState").toString()
                            == QStringLiteral("not-linked")
                        && rotationAcceptanceFirstGate.value("vectorSet").toString()
                            == QStringLiteral("production-session-key-generation-vectors-v1")
                        && rotationAcceptanceFirstGate.value("migrationBlocker").toString()
                            == QStringLiteral("production-session-key-generation-not-implemented"),
                    "production rotation acceptance should expose required operation implementation slots") && ok;
        const QJsonObject firstRotationStage = rotationDryRun.value("rotationStages").toArray().at(0).toObject();
        ok = expect(firstRotationStage.value("name").toString()
                            == QStringLiteral("generate-production-identity")
                        && firstRotationStage.value("requiredOperation").toString()
                            == QStringLiteral("identity-key-generation")
                        && firstRotationStage.value("status").toString() == QStringLiteral("blocked")
                        && firstRotationStage.value("blockedReason").toString()
                            == QStringLiteral("production-crypto-backend-unavailable")
                        && firstRotationStage.value("providerCompatibilityGate").toString()
                            == QStringLiteral("production-adapter-not-linked"),
                    "production rotation dry-run should expose per-stage provider gates") && ok;
        const QByteArray rotationDryRunJson = QJsonDocument(rotationDryRun).toJson(QJsonDocument::Compact);
        ok = expect(!rotationDryRunJson.contains("privateKey")
                        && !rotationDryRunJson.contains("sessionKey")
                        && !rotationDryRunJson.contains("publicKey\"")
                        && !rotationDryRunJson.contains(originalAliceIdentityFingerprint.toUtf8())
                        && QFile::exists(aliceIdentityPathBeforeClear)
                        && QFile::exists(aliceTrustPinsPathBeforeClear)
                        && alice.e2eSessionStatus(bobId).value("state").toString()
                            == QStringLiteral("backend-migration-required"),
                    "production rotation dry-run evidence should stay sanitized and preserve draft state") && ok;
        const QJsonObject rotationExecute = alice.executeE2EProductionRotation(&rejectReason);
        ok = expect(rejectReason == QStringLiteral("production-crypto-backend-unavailable")
                        && rotationExecute.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-rotation-v1")
                        && rotationExecute.value("mode").toString() == QStringLiteral("execute")
                        && !rotationExecute.value("executed").toBool(true)
                        && !rotationExecute.value("destructive").toBool(true)
                        && rotationExecute.value("releaseGate").toString()
                            == QStringLiteral("production-crypto-provider-not-ready")
                        && rotationExecute.value("blockedReason").toString()
                            == QStringLiteral("production-crypto-backend-unavailable")
                        && rotationExecute.value("operatorAction").toString()
                            == QStringLiteral("complete-reviewed-production-provider-before-rotation")
                        && rotationExecute.value("affectedPeerPinCount").toInt() >= 1
                        && rotationExecute.value("affectedSessionCount").toInt() >= 1
                        && rotationExecute.value("rotationStages").toArray().size() == 7
                        && rotationExecute.value("blockedStageCount").toInt() == 7
                        && rotationExecute.value("productionAcceptanceReleaseGate").toString()
                            == QStringLiteral("production-adapter-not-linked")
                        && !rotationExecute.value("productionAcceptanceAccepted").toBool(true)
                        && rotationExecute.value("productionAcceptance").toObject()
                            .value("operationManifest").toArray().size() == 8
                        && rotationExecute.value("productionExecutionPlanReleaseGate").toString()
                            == QStringLiteral("production-operation-execution-plan-blocked-not-linked")
                        && !rotationExecute.value("productionExecutionPlanAccepted").toBool(true)
                        && rotationExecute.value("productionExecutionPlanBlockedStepCount").toInt() == 8
                        && rotationExecute.value("productionOperationExecutionPlan").toObject()
                            .value("steps").toArray().size() == 8
                        && rotationExecute.value("productionInvocationReleaseGate").toString()
                            == QStringLiteral("production-operation-invocation-blocked-not-linked")
                        && !rotationExecute.value("productionInvocationAccepted").toBool(true)
                        && rotationExecute.value("productionInvocationBlockedOperationCount").toInt() == 8
                        && rotationExecute.value("productionOperationInvocation").toObject()
                            .value("invocations").toArray().size() == 8
                        && rotationExecute.value("productionSlotsReleaseGate").toString()
                            == QStringLiteral("production-operation-slots-blocked-not-linked")
                        && !rotationExecute.value("productionSlotsAccepted").toBool(true)
                        && rotationExecute.value("productionSlotsBlockedSlotCount").toInt() == 8
                        && rotationExecute.value("productionOperationSlots").toObject()
                            .value("slots").toArray().size() == 8
                        && rotationExecute.value("productionDispatchBindingsReleaseGate").toString()
                            == QStringLiteral("production-operation-dispatch-bindings-blocked-not-linked")
                        && !rotationExecute.value("productionDispatchBindingsAccepted").toBool(true)
                        && rotationExecute.value("productionDispatchBindingsBlockedBindingCount").toInt() == 8
                        && rotationExecute.value("productionOperationDispatchBindings").toObject()
                            .value("bindings").toArray().size() == 8
                        && rotationExecute.value("productionExecutionResultReleaseGate").toString()
                            == QStringLiteral("production-operation-execution-results-blocked-not-linked")
                        && !rotationExecute.value("productionExecutionResultAccepted").toBool(true)
                        && rotationExecute.value("productionExecutionResultBlockedResultCount").toInt() == 8
                        && rotationExecute.value("productionExecutionResultPassedResultCount").toInt() == 0
                        && rotationExecute.value("productionOperationExecutionResult").toObject()
                            .value("results").toArray().size() == 8
                        && rotationExecute.value("productionProviderTableReleaseGate").toString()
                            == QStringLiteral("production-provider-table-blocked-not-linked")
                        && !rotationExecute.value("productionProviderTableAccepted").toBool(true)
                        && rotationExecute.value("productionProviderTableBoundSymbolCount").toInt() == 0
                        && rotationExecute.value("productionProviderTableMissingSymbolCount").toInt() == 8
                        && rotationExecute.value("productionProviderTable").toObject()
                            .value("entries").toArray().size() == 8
                        && rotationExecute.value("productionProviderTableBindingProbeReleaseGate").toString()
                            == QStringLiteral("production-provider-table-binding-blocked-not-linked")
                        && !rotationExecute.value("productionProviderTableBindingProbeAccepted").toBool(true)
                        && rotationExecute.value("productionProviderTableBindingProbe").toObject()
                            .value("enumMappings").toArray().size() == 8
                        && rotationExecute.value("productionProviderTableRegistrationReleaseGate").toString()
                            == QStringLiteral("production-provider-table-registration-blocked-not-linked")
                        && !rotationExecute.value("productionProviderTableRegistrationAccepted").toBool(true)
                        && !rotationExecute.value("productionProviderTableRegistered").toBool(true)
                        && rotationExecute.value("productionProviderTableRegistration").toObject()
                            .value("blockedReason").toString()
                                == QStringLiteral("production-provider-table-not-registered")
                        && rotationExecute.value("productionProviderOperationPreflightReleaseGate").toString()
                            == QStringLiteral("production-provider-operation-preflight-blocked-not-linked")
                        && !rotationExecute.value("productionProviderOperationPreflightAccepted").toBool(true)
                        && rotationExecute.value("productionProviderOperationPreflightBlockedOperationCount").toInt() == 8
                        && rotationExecute.value("productionProviderOperationPreflight").toObject()
                            .value("operations").toArray().size() == 8
                        && !rotationExecute.value("productionProviderOperationPreflight").toObject()
                            .value("operationInvoked").toBool(true)
                        && rotationExecute.value("productionProviderCallFrameReleaseGate").toString()
                            == QStringLiteral("production-provider-call-frame-blocked-not-linked")
                        && !rotationExecute.value("productionProviderCallFrameAccepted").toBool(true)
                        && rotationExecute.value("productionProviderCallFrameBlockedFrameCount").toInt() == 8
                        && rotationExecute.value("productionProviderCallFrame").toObject()
                            .value("frames").toArray().size() == 8
                        && !rotationExecute.value("productionProviderCallFrame").toObject()
                            .value("operationInvoked").toBool(true)
                        && rotationExecute.value("productionProviderInvocationDryRunReleaseGate").toString()
                            == QStringLiteral("production-provider-invocation-dry-run-blocked-not-linked")
                        && !rotationExecute.value("productionProviderInvocationDryRunAccepted").toBool(true)
                        && rotationExecute.value("productionProviderInvocationDryRunBlockedInvocationCount").toInt() == 8
                        && rotationExecute.value("productionProviderInvocationDryRun").toObject()
                            .value("invocations").toArray().size() == 8
                        && !rotationExecute.value("productionProviderInvocationDryRun").toObject()
                            .value("operationInvoked").toBool(true)
                        && rotationExecute.value("productionProviderInvocationResultReleaseGate").toString()
                            == QStringLiteral("production-provider-invocation-results-blocked-not-linked")
                        && !rotationExecute.value("productionProviderInvocationResultAccepted").toBool(true)
                        && rotationExecute.value("productionProviderInvocationResultBlockedResultCount").toInt() == 8
                        && rotationExecute.value("productionProviderInvocationResult").toObject()
                            .value("results").toArray().size() == 8
                        && !rotationExecute.value("productionProviderInvocationResult").toObject()
                            .value("operationInvoked").toBool(true)
                        && !rotationExecute.value("productionProviderInvocationResult").toObject()
                            .value("resultCaptured").toBool(true)
                        && rotationExecute.value("productionProviderExecutionDecisionReleaseGate").toString()
                            == QStringLiteral("production-provider-execution-decision-blocked-not-linked")
                        && !rotationExecute.value("productionProviderExecutionDecisionAccepted").toBool(true)
                        && rotationExecute.value("productionProviderExecutionDecisionBlockedDecisionCount").toInt() == 8
                        && rotationExecute.value("productionProviderExecutionDecision").toObject()
                            .value("decisions").toArray().size() == 8
                        && !rotationExecute.value("productionProviderExecutionDecision").toObject()
                            .value("operationInvoked").toBool(true)
                        && !rotationExecute.value("productionProviderExecutionDecision").toObject()
                            .value("resultCaptured").toBool(true)
                        && rotationExecute.value("productionProviderCallbackHarnessReleaseGate").toString()
                            == QStringLiteral("production-provider-callback-harness-blocked-not-linked")
                        && !rotationExecute.value("productionProviderCallbackHarnessAccepted").toBool(true)
                        && rotationExecute.value("productionProviderCallbackHarnessBlockedCallbackCount").toInt() == 8
                        && rotationExecute.value("productionProviderCallbackHarness").toObject()
                            .value("callbacks").toArray().size() == 8
                        && !rotationExecute.value("productionProviderCallbackHarness").toObject()
                            .value("operationInvoked").toBool(true)
                        && !rotationExecute.value("productionProviderCallbackHarness").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderCallbackHarness").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderCallbackHarness").toObject()
                            .value("resultCaptured").toBool(true)
                        && rotationExecute.value("productionProviderVectorSelfTestReleaseGate").toString()
                            == QStringLiteral("production-provider-vector-self-test-blocked-not-linked")
                        && !rotationExecute.value("productionProviderVectorSelfTestAccepted").toBool(true)
                        && rotationExecute.value("productionProviderVectorSelfTestBlockedVectorCount").toInt() == 8
                        && rotationExecute.value("productionProviderVectorSelfTest").toObject()
                            .value("tests").toArray().size() == 8
                        && !rotationExecute.value("productionProviderVectorSelfTest").toObject()
                            .value("operationInvoked").toBool(true)
                        && !rotationExecute.value("productionProviderVectorSelfTest").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderVectorSelfTest").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderVectorSelfTest").toObject()
                            .value("resultCaptured").toBool(true)
                        && rotationExecute.value("productionProviderExecutionSlotBindingReleaseGate").toString()
                            == QStringLiteral("production-provider-execution-slot-binding-blocked-not-linked")
                        && !rotationExecute.value("productionProviderExecutionSlotBindingAccepted").toBool(true)
                        && rotationExecute.value("productionProviderExecutionSlotBindingBlockedSlotCount").toInt() == 8
                        && rotationExecute.value("productionProviderExecutionSlotBinding").toObject()
                            .value("slots").toArray().size() == 8
                        && !rotationExecute.value("productionProviderExecutionSlotBinding").toObject()
                            .value("operationInvoked").toBool(true)
                        && !rotationExecute.value("productionProviderExecutionSlotBinding").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderExecutionSlotBinding").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderExecutionSlotBinding").toObject()
                            .value("resultCaptured").toBool(true)
                        && rotationExecute.value("productionProviderExecutionPathReleaseGate").toString()
                            == QStringLiteral("production-provider-execution-path-blocked-not-linked")
                        && !rotationExecute.value("productionProviderExecutionPathAccepted").toBool(true)
                        && rotationExecute.value("productionProviderExecutionPathBlockedPathCount").toInt() == 8
                        && rotationExecute.value("productionProviderExecutionPath").toObject()
                            .value("paths").toArray().size() == 8
                        && !rotationExecute.value("productionProviderExecutionPath").toObject()
                            .value("operationInvoked").toBool(true)
                        && !rotationExecute.value("productionProviderExecutionPath").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderExecutionPath").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderExecutionPath").toObject()
                            .value("resultCaptured").toBool(true)
                        && rotationExecute.value("productionProviderInvocationSandboxReleaseGate").toString()
                            == QStringLiteral("production-provider-invocation-sandbox-blocked-not-linked")
                        && !rotationExecute.value("productionProviderInvocationSandboxAccepted").toBool(true)
                        && rotationExecute.value("productionProviderInvocationSandboxBlockedSandboxCount").toInt() == 8
                        && rotationExecute.value("productionProviderInvocationSandbox").toObject()
                            .value("sandboxes").toArray().size() == 8
                        && !rotationExecute.value("productionProviderInvocationSandbox").toObject()
                            .value("operationInvoked").toBool(true)
                        && !rotationExecute.value("productionProviderInvocationSandbox").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderInvocationSandbox").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderInvocationSandbox").toObject()
                            .value("resultCaptured").toBool(true)
                        && rotationExecute.value("productionProviderInvocationVectorResultReleaseGate").toString()
                            == QStringLiteral("production-provider-invocation-vector-result-blocked-not-linked")
                        && !rotationExecute.value("productionProviderInvocationVectorResultAccepted").toBool(true)
                        && rotationExecute.value("productionProviderInvocationVectorResultBlockedVectorResultCount").toInt() == 8
                        && rotationExecute.value("productionProviderInvocationVectorResult").toObject()
                            .value("vectorResults").toArray().size() == 8
                        && !rotationExecute.value("productionProviderInvocationVectorResult").toObject()
                            .value("operationInvoked").toBool(true)
                        && !rotationExecute.value("productionProviderInvocationVectorResult").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderInvocationVectorResult").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderInvocationVectorResult").toObject()
                            .value("resultCaptured").toBool(true)
                        && rotationExecute.value("productionProviderInvocationExecutionReleaseGate").toString()
                            == QStringLiteral("production-provider-invocation-execution-blocked-not-linked")
                        && !rotationExecute.value("productionProviderInvocationExecutionAccepted").toBool(true)
                        && rotationExecute.value("productionProviderInvocationExecutionBlockedExecutionCount").toInt() == 8
                        && rotationExecute.value("productionProviderInvocationExecution").toObject()
                            .value("executions").toArray().size() == 8
                        && !rotationExecute.value("productionProviderInvocationExecution").toObject()
                            .value("operationInvoked").toBool(true)
                        && !rotationExecute.value("productionProviderInvocationExecution").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderInvocationExecution").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderInvocationExecution").toObject()
                            .value("resultCaptured").toBool(true)
                        && rotationExecute.value("productionProviderReviewedExecutionCandidateReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-execution-candidate-not-release-gate")
                        && rotationExecute.value("productionProviderReviewedExecutionCandidateNonReleaseGate").toBool(false)
                        && rotationExecute.value("productionProviderReviewedExecutionCandidateReadyCount").toInt() == 0
                        && rotationExecute.value("productionProviderReviewedExecutionCandidateBlockedCount").toInt() == 8
                        && rotationExecute.value("productionProviderReviewedExecutionCandidate").toObject()
                            .value("candidates").toArray().size() == 8
                        && !rotationExecute.value("productionProviderReviewedExecutionCandidate").toObject()
                            .value("probeSourceCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedExecutionCandidate").toObject()
                            .value("operationInvokedByCandidate").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedExecutionCandidate").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedExecutionCandidate").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && rotationExecute.value("productionProviderReviewedCallHandoffReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-call-handoff-not-release-gate")
                        && rotationExecute.value("productionProviderReviewedCallHandoffNonReleaseGate").toBool(false)
                        && rotationExecute.value("productionProviderReviewedCallHandoffReadyCount").toInt() == 0
                        && rotationExecute.value("productionProviderReviewedCallHandoffBlockedCount").toInt() == 8
                        && rotationExecute.value("productionProviderReviewedCallHandoff").toObject()
                            .value("handoffs").toArray().size() == 8
                        && !rotationExecute.value("productionProviderReviewedCallHandoff").toObject()
                            .value("candidateSourceCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedCallHandoff").toObject()
                            .value("operationInvokedByHandoff").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedCallHandoff").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedCallHandoff").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && rotationExecute.value("productionProviderReviewedOperationStubBoundaryReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-operation-stub-not-release-gate")
                        && rotationExecute.value("productionProviderReviewedOperationStubBoundaryNonReleaseGate").toBool(false)
                        && rotationExecute.value("productionProviderReviewedOperationStubBoundaryReadyCount").toInt() == 0
                        && rotationExecute.value("productionProviderReviewedOperationStubBoundaryBlockedCount").toInt() == 8
                        && rotationExecute.value("productionProviderReviewedOperationStubBoundary").toObject()
                            .value("stubs").toArray().size() == 8
                        && !rotationExecute.value("productionProviderReviewedOperationStubBoundary").toObject()
                            .value("handoffSourceCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedOperationStubBoundary").toObject()
                            .value("operationInvokedByStub").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedOperationStubBoundary").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedOperationStubBoundary").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && rotationExecute.value("productionProviderReviewedCallableTableBridgeReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-callable-table-bridge-not-release-gate")
                        && rotationExecute.value("productionProviderReviewedCallableTableBridgeNonReleaseGate").toBool(false)
                        && rotationExecute.value("productionProviderReviewedCallableTableBridgeReadyCount").toInt() == 0
                        && rotationExecute.value("productionProviderReviewedCallableTableBridgeBlockedCount").toInt() == 8
                        && rotationExecute.value("productionProviderReviewedCallableTableBridge").toObject()
                            .value("bridges").toArray().size() == 8
                        && !rotationExecute.value("productionProviderReviewedCallableTableBridge").toObject()
                            .value("stubSourceCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedCallableTableBridge").toObject()
                            .value("operationInvokedByBridge").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedCallableTableBridge").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedCallableTableBridge").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && rotationExecute.value("productionProviderReviewedOperationCallableInterfaceReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-operation-callable-interface-not-release-gate")
                        && rotationExecute.value("productionProviderReviewedOperationCallableInterfaceNonReleaseGate").toBool(false)
                        && rotationExecute.value("productionProviderReviewedOperationCallableInterfaceReadyCount").toInt() == 0
                        && rotationExecute.value("productionProviderReviewedOperationCallableInterfaceBlockedCount").toInt() == 8
                        && rotationExecute.value("productionProviderReviewedOperationCallableInterface").toObject()
                            .value("interfaces").toArray().size() == 8
                        && !rotationExecute.value("productionProviderReviewedOperationCallableInterface").toObject()
                            .value("bridgeSourceCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedOperationCallableInterface").toObject()
                            .value("operationInvokedByInterface").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedOperationCallableInterface").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedOperationCallableInterface").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && rotationExecute.value("productionProviderReviewedCallableRuntimePreflightReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-callable-runtime-preflight-not-release-gate")
                        && rotationExecute.value("productionProviderReviewedCallableRuntimePreflightNonReleaseGate").toBool(false)
                        && rotationExecute.value("productionProviderReviewedCallableRuntimePreflightReadyCount").toInt() == 0
                        && rotationExecute.value("productionProviderReviewedCallableRuntimePreflightBlockedCount").toInt() == 8
                        && rotationExecute.value("productionProviderReviewedCallableRuntimePreflight").toObject()
                            .value("preflights").toArray().size() == 8
                        && !rotationExecute.value("productionProviderReviewedCallableRuntimePreflight").toObject()
                            .value("interfaceSourceCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedCallableRuntimePreflight").toObject()
                            .value("operationInvokedByRuntimePreflight").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedCallableRuntimePreflight").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedCallableRuntimePreflight").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && rotationExecute.value("productionProviderReviewedInvocationArmingReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-invocation-arming-not-release-gate")
                        && rotationExecute.value("productionProviderReviewedInvocationArmingNonReleaseGate").toBool(false)
                        && rotationExecute.value("productionProviderReviewedInvocationArmingReadyCount").toInt() == 0
                        && rotationExecute.value("productionProviderReviewedInvocationArmingBlockedCount").toInt() == 8
                        && rotationExecute.value("productionProviderReviewedInvocationArming").toObject()
                            .value("armings").toArray().size() == 8
                        && !rotationExecute.value("productionProviderReviewedInvocationArming").toObject()
                            .value("runtimePreflightSourceCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedInvocationArming").toObject()
                            .value("operationInvokedByArming").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedInvocationArming").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedInvocationArming").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && rotationExecute.value("productionProviderReviewedInvocationExecutionAcceptanceReleaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-not-release-gate")
                        && rotationExecute.value("productionProviderReviewedInvocationExecutionAcceptanceNonReleaseGate").toBool(false)
                        && rotationExecute.value("productionProviderReviewedInvocationExecutionAcceptanceReadyCount").toInt() == 0
                        && rotationExecute.value("productionProviderReviewedInvocationExecutionAcceptanceBlockedCount").toInt() == 8
                        && rotationExecute.value("productionProviderReviewedInvocationExecutionAcceptance").toObject()
                            .value("acceptances").toArray().size() == 8
                        && !rotationExecute.value("productionProviderReviewedInvocationExecutionAcceptance").toObject()
                            .value("armingSourceCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedInvocationExecutionAcceptance").toObject()
                            .value("operationInvokedByExecutionAcceptance").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedInvocationExecutionAcceptance").toObject()
                            .value("inputBytesCaptured").toBool(true)
                        && !rotationExecute.value("productionProviderReviewedInvocationExecutionAcceptance").toObject()
                            .value("outputBytesCaptured").toBool(true)
                        && !rotationExecute.value("wouldClearLocalIdentityStore").toBool(true)
                        && !rotationExecute.value("wouldClearTrustPinStore").toBool(true)
                        && !rotationExecute.value("wouldDropActiveSessions").toBool(true),
                    "production rotation execute should fail closed before provider readiness without clearing local state") && ok;
        const QByteArray rotationExecuteJson = QJsonDocument(rotationExecute).toJson(QJsonDocument::Compact);
        ok = expect(!rotationExecuteJson.contains("privateKey")
                        && !rotationExecuteJson.contains("sessionKey")
                        && !rotationExecuteJson.contains("publicKey\"")
                        && !rotationExecuteJson.contains(originalAliceIdentityFingerprint.toUtf8())
                        && QFile::exists(aliceIdentityPathBeforeClear)
                        && QFile::exists(aliceTrustPinsPathBeforeClear)
                        && alice.e2eSessionStatus(bobId).value("state").toString()
                            == QStringLiteral("backend-migration-required"),
                    "production rotation execute evidence should stay sanitized and preserve draft state") && ok;
        const QJsonObject migrationEvidence = alice.executeE2EBackendMigration(&rejectReason);
        ok = expect(rejectReason.isEmpty()
                        && migrationEvidence.value("executed").toBool(false)
                        && migrationEvidence.value("mode").toString() == QStringLiteral("execute")
                        && migrationEvidence.value("clearedIdentityStore").toBool(false)
                        && migrationEvidence.value("clearedTrustPinStore").toBool(false)
                        && migrationEvidence.value("clearedSessionCount").toInt() >= 1
                        && migrationEvidence.value("clearedPeerTrustCount").toInt() >= 1
                        && migrationEvidence.value("before").toObject().value("migrationRequired").toBool(false)
                        && !migrationEvidence.value("after").toObject().value("migrationRequired").toBool(true),
                    "migration execution should return before/after counters and clear persisted draft identity, pins, sessions, and pending agreements") && ok;
        const QByteArray migrationEvidenceJson = QJsonDocument(migrationEvidence).toJson(QJsonDocument::Compact);
        ok = expect(!migrationEvidenceJson.contains("privateKey")
                        && !migrationEvidenceJson.contains("sessionKey")
                        && !migrationEvidenceJson.contains("publicKey\"")
                        && !migrationEvidenceJson.contains(originalAliceIdentityFingerprint.toUtf8()),
                    "migration execution evidence should stay sanitized") && ok;
        ok = expect(!QFile::exists(aliceIdentityPathBeforeClear)
                        && !QFile::exists(aliceTrustPinsPathBeforeClear)
                        && alice.e2eLocalIdentityStatus().value("blockedReason").toString()
                            == QStringLiteral("identity-not-ready")
                        && alice.e2ePeerIdentityStatus(bobId).value("trustState").toString()
                            == QStringLiteral("unverified")
                        && alice.e2eSessionStatus(bobId).value("state").toString()
                            == QStringLiteral("missing-session"),
                    "migration recovery should leave sanitized untrusted peer identity and no reusable draft session") && ok;
        ok = expect(!alice.sendEncryptedPrivateMessage(bobId,
                                                       "cleared migration state must not send",
                                                       &rejectReason)
                        && rejectReason == QStringLiteral("missing-session"),
                    "cleared migration state should block encrypted sends without reusing draft material") && ok;
        ok = expect(!alice.requestE2ESessionRotation(bobId, &rejectReason)
                        && rejectReason == QStringLiteral("identity-not-ready"),
                    "cleared migration state should require a new backend identity before rotation") && ok;
        ok = expect(!alice.clearE2EBackendMigrationState(&rejectReason)
                        && rejectReason == QStringLiteral("migration-not-required"),
                    "compatibility migration clear wrapper should report when no migration remains") && ok;
        qunsetenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND");
        alice.setUserInfo(aliceId, "Alice");
        bob.setUserInfo(bobId, "Bob");
        ok = expect(waitFor([&] {
            return alice.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString().size() == 64
                && alice.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString()
                    != originalAliceIdentityFingerprint
                && alice.e2eLocalIdentityStatus().value("agreementSigning").toBool(false);
        }, 5000), "migration recovery should create a fresh local draft identity before re-announcing") && ok;
        ok = expect(bob.clearE2EPeerIdentityPin(aliceId, &rejectReason)
                        && bob.e2ePeerIdentityStatus(aliceId).value("trustState").toString()
                            == QStringLiteral("unverified"),
                    "receiver should explicitly clear the stale alice pin before trusting the regenerated identity") && ok;
        QString aliceAnnounceReason;
        QString bobAnnounceReason;
        ok = expect(exchangeE2EIdentityAnnouncements(alice,
                                                     bobId,
                                                     bob,
                                                     aliceId,
                                                     &aliceAnnounceReason,
                                                     &bobAnnounceReason),
                    "test should exchange regenerated draft identity announcements") && ok;
        const bool regeneratedIdentityAnnounced = waitFor([&] {
            if (alice.e2ePeerIdentityStatus(bobId).value("publicKeyFingerprintSha256").toString()
                    != bob.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString()) {
                exchangeE2EIdentityAnnouncements(alice,
                                                 bobId,
                                                 bob,
                                                 aliceId,
                                                 &aliceAnnounceReason,
                                                 &bobAnnounceReason);
            }
            if (bob.e2ePeerIdentityStatus(aliceId).value("publicKeyFingerprintSha256").toString()
                    != alice.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString()) {
                exchangeE2EIdentityAnnouncements(alice,
                                                 bobId,
                                                 bob,
                                                 aliceId,
                                                 &aliceAnnounceReason,
                                                 &bobAnnounceReason);
            }
            return alice.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString().size() == 64
                && alice.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString()
                    != originalAliceIdentityFingerprint
                && alice.e2ePeerIdentityStatus(bobId).value("verificationCode").toString().size() >= 12
                && bob.e2ePeerIdentityStatus(aliceId).value("verificationCode").toString().size() >= 12
                && bob.e2ePeerIdentityStatus(aliceId).value("publicKeyFingerprintSha256").toString()
                    == alice.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString()
                && alice.e2ePeerIdentityStatus(bobId).value("publicKeyFingerprintSha256").toString()
                    == bob.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString()
                && alice.e2ePeerIdentityStatus(bobId).value("verificationCode").toString()
                    == bob.e2ePeerIdentityStatus(aliceId).value("verificationCode").toString();
        }, 15000);
        ok = expect(regeneratedIdentityAnnounced,
                    "regenerated draft identity should be announced with a fresh verification code") && ok;
        ok = expect(aliceAnnounceReason.isEmpty() && bobAnnounceReason.isEmpty(),
                    "regenerated identity announcements should not be rejected") && ok;
        const QString regeneratedAliceBobVerificationCode = alice.e2ePeerIdentityStatus(bobId).value("verificationCode").toString();
        const QString regeneratedBobAliceVerificationCode = bob.e2ePeerIdentityStatus(aliceId).value("verificationCode").toString();
        ok = expect(alice.pinE2EPeerIdentity(bobId,
                                             alice.e2ePeerIdentityStatus(bobId).value("publicKeyFingerprintSha256").toString(),
                                             &rejectReason)
                        && alice.verifyAndPinE2EPeerIdentity(bobId, regeneratedAliceBobVerificationCode, &rejectReason),
                    "test should re-pin bob with the regenerated identity verification code") && ok;
        ok = expect(bob.pinE2EPeerIdentity(aliceId,
                                           bob.e2ePeerIdentityStatus(aliceId).value("publicKeyFingerprintSha256").toString(),
                                           &rejectReason)
                        && bob.verifyAndPinE2EPeerIdentity(aliceId, regeneratedBobAliceVerificationCode, &rejectReason),
                    "receiver should re-pin regenerated alice identity so draft compatibility flow can continue") && ok;
        alice.setE2ESessionKey(bobId, keyId + "-manual-rotation", rotatedSessionKey);
        bobMessage = Message();
        ok = expect(alice.sendEncryptedPrivateMessage(bobId, "encrypted again after manual rotation", &rejectReason),
                    "encrypted send should resume after both clients install a local rotated session") && ok;
        ok = expect(waitFor([&] {
            return bobMessage.content == QStringLiteral("encrypted again after manual rotation");
        }, 9000), "receiver should decrypt after manual rotation installs matching local keys") && ok;

        const QByteArray privateFilePayload("e2e private file payload should not cross the server as plaintext");
        const QString privateFilePath = QDir(appDataDir).filePath(QStringLiteral("alice-private-e2e-file.bin"));
        ok = expect(writeTextFile(privateFilePath, privateFilePayload),
                    "test should write a private file payload") && ok;
        bobFileMessage = Message();
        ok = expect(alice.sendFile(privateFilePath, bobId),
                    "trusted e2e private file send should succeed") && ok;
        ok = expect(waitFor([&] {
            return bobFileMessage.type == MessageType::File
                && bobFileMessage.fileData == privateFilePayload;
        }, 9000), "bob should receive the decrypted private file payload") && ok;
        ok = expect(bobFileMessage.e2eFileEncrypted
                        && bobFileMessage.e2eFilePlainSize == privateFilePayload.size()
                        && bobFileMessage.e2eFilePlainHash
                            == QString::fromLatin1(QCryptographicHash::hash(privateFilePayload, QCryptographicHash::Sha256).toHex())
                        && bobFileMessage.fileHash == bobFileMessage.e2eFilePlainHash
                        && bobFileMessage.content.contains(QStringLiteral("端到端加密")),
                    "decrypted private file should retain safe e2e metadata and plaintext hash") && ok;
        ok = expect(!bobFileMessage.fileData.contains("ciphertext"),
                    "received private file data should be plaintext after local decrypt") && ok;

        alice.clearE2ESessionKey(bobId);
        ok = expect(!alice.hasE2ESession(bobId)
                        && alice.e2eSessionStatus(bobId).value("state").toString() == QStringLiteral("missing-session"),
                    "clearing an e2e session should expose missing-session status") && ok;
        ok = expect(!alice.sendFile(privateFilePath, bobId),
                    "mandatory e2e private file policy should reject private files without a ready session") && ok;
        alice.setE2ESessionKey(bobId, keyId + "-rotated", generateE2ESessionKey());
        ok = expect(alice.hasE2ESession(bobId) && !alice.e2eSessionNeedsRotation(bobId),
                    "setting a new e2e session should clear the rotation gate") && ok;
        ok = expect(!mallory.requestE2ESessionRotation(QStringLiteral("929999"), &rejectReason)
                        && rejectReason == QStringLiteral("missing-identity"),
                    "rotation requests should fail closed until the peer identity has been observed") && ok;

        bob.clearE2ESessionKey(aliceId);
        bobMessage = Message();
        ok = expect(alice.sendEncryptedPrivateMessage(bobId, "cannot decrypt after key removal", &rejectReason),
                    "alice should still send encrypted message after receiver clears key") && ok;
        ok = expect(waitFor([&] {
            return bobMessage.content == QStringLiteral("加密消息无法解密");
        }), "receiver without a key should fail closed with a visible placeholder") && ok;
        ok = expect(bobError.contains("missing-session") || bobError.contains("authentication-failed"),
                    "receiver without a key should emit a decrypt diagnostic") && ok;
        ok = expect(malloryError.isEmpty(),
                    "unrelated clients should not see encrypted private diagnostics") && ok;

        disconnectClient(alice);
        disconnectClient(bob);
        disconnectClient(mallory);

        Client aliceRestarted;
        Client bobRestarted;
        QJsonObject restartedAliceSawBobIdentity;
        QObject::connect(&aliceRestarted, &Client::e2eIdentityStateChanged, &app, [&](const QString& peerId, const QJsonObject& status) {
            if (peerId == bobId) {
                restartedAliceSawBobIdentity = status;
            }
        });
        ok = expect(loginClient(aliceRestarted, aliceId, "Alice", port),
                    "restarted alice should log in with the same app data") && ok;
        ok = expect(loginClient(bobRestarted, bobId, "Bob", port),
                    "restarted bob should log in with the same app data") && ok;
        ok = expect(waitFor([&] {
            return restartedAliceSawBobIdentity.value("trustState").toString() == QStringLiteral("trusted")
                && restartedAliceSawBobIdentity.value("verified").toBool(false)
                && restartedAliceSawBobIdentity.value("verificationCode").toString() == regeneratedAliceBobVerificationCode
                && restartedAliceSawBobIdentity.value("pinPersisted").toBool(false)
                && restartedAliceSawBobIdentity.value("publicKeyFingerprintSha256").toString()
                    == aliceSawBobIdentity.value("publicKeyFingerprintSha256").toString();
        }), "restarted alice should restore the persisted verified e2e trust pin for bob") && ok;
        disconnectClient(aliceRestarted);
        disconnectClient(bobRestarted);

        qputenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND", "production");
        Client aliceProductionRestarted;
        aliceProductionRestarted.setUserInfo(aliceId, "Alice");
        const QJsonObject persistedDraftIdentityUnderProduction = aliceProductionRestarted.e2eLocalIdentityStatus();
        ok = expect(persistedDraftIdentityUnderProduction.value("backendId").toString()
                        == QStringLiteral("draft-qt-hmac-stream-v1")
                        && persistedDraftIdentityUnderProduction.value("publicKeyFingerprintSha256").toString()
                            == alice.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString()
                        && persistedDraftIdentityUnderProduction.value("backendMigrationRequired").toBool(false)
                        && (persistedDraftIdentityUnderProduction.value("blockedReason").toString()
                                == QStringLiteral("e2e-backend-migration-required")
                            || persistedDraftIdentityUnderProduction.value("blockedReason").toString()
                                == QStringLiteral("production-crypto-backend-unavailable"))
                        && !persistedDraftIdentityUnderProduction.value("agreementSigning").toBool(true),
                    "persisted draft identity should remain inspectable but require migration under production backend request") && ok;
        qunsetenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND");

        ok = expect(loginClient(aliceRestarted, aliceId, "Alice", port),
                    "restarted alice should reconnect after production migration inspection") && ok;
        ok = expect(loginClient(bobRestarted, bobId, "Bob", port),
                    "restarted bob should reconnect after production migration inspection") && ok;
        ok = expect(aliceRestarted.clearE2EPeerIdentityPin(bobId, &rejectReason)
                        && aliceRestarted.e2ePeerIdentityStatus(bobId).value("trustState").toString() == QStringLiteral("unverified")
                        && !aliceRestarted.e2ePeerIdentityStatus(bobId).value("verified").toBool(true)
                        && !aliceRestarted.e2ePeerIdentityStatus(bobId).value("pinPersisted").toBool(true),
                    "clearing an e2e trust pin should recover the peer to unverified state") && ok;
        aliceRestarted.setE2ESessionKey(bobId, "stale-after-pin-clear", generateE2ESessionKey());
        ok = expect(!aliceRestarted.sendEncryptedPrivateMessage(bobId, "stale trusted session must not send", &rejectReason)
                        && rejectReason == QStringLiteral("untrusted-identity"),
                    "default e2e policy should block encrypted sends after trust pin recovery clears trust") && ok;
        ok = expect(!aliceRestarted.sendFile(privateFilePath, bobId),
                    "default e2e policy should block private file sends after trust pin recovery clears trust") && ok;
        disconnectClient(aliceRestarted);
        disconnectClient(bobRestarted);

        qputenv("QTNETWORKCHAT_E2E_REQUIRE_PRODUCTION_CRYPTO", "1");
        Client productionRequiredClient;
        productionRequiredClient.setUserInfo("950099", "ProdRequired");
        const QJsonObject productionRequiredIdentity = productionRequiredClient.e2eLocalIdentityStatus();
        ok = expect(productionRequiredIdentity.value("cryptoBackend").toObject()
                        .value("status").toString() == QStringLiteral("production-crypto-backend-unavailable")
                        && productionRequiredIdentity.value("cryptoBackend").toObject()
                            .value("selectedBackendId").toString().isEmpty()
                        && productionRequiredIdentity.value("cryptoBackend").toObject()
                            .value("unavailableReason").toString() == QStringLiteral("production-crypto-backend-unavailable")
                        && !productionRequiredIdentity.value("backendMigrationRequired").toBool(true)
                        && !productionRequiredIdentity.value("agreementSigning").toBool(true),
                    "client identity status should expose blocked production-required crypto backend") && ok;
        productionRequiredClient.setAccountInfo("950099", "secret", false);
        ok = expect(productionRequiredClient.connectToServer("127.0.0.1", port),
                    "production-required client should still connect for fail-closed e2e checks") && ok;
        ok = expect(!productionRequiredClient.announceE2EIdentity(aliceId, &rejectReason)
                        && (rejectReason == QStringLiteral("identity-not-ready")
                            || rejectReason == QStringLiteral("e2e-backend-migration-required")),
                    "production-required client should not announce a draft identity") && ok;
        productionRequiredClient.disconnectFromServer();
        qunsetenv("QTNETWORKCHAT_E2E_REQUIRE_PRODUCTION_CRYPTO");

        qputenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND", "production");
        Client productionAdapterClient;
        productionAdapterClient.setUserInfo("950098", "ProdAdapter");
        const QJsonObject productionAdapterIdentity = productionAdapterClient.e2eLocalIdentityStatus();
        ok = expect(productionAdapterIdentity.value("cryptoBackend").toObject()
                        .value("requestedBackendId").toString() == QStringLiteral("openssl-reviewed-adapter-v1")
                        && productionAdapterIdentity.value("cryptoBackend").toObject()
                            .value("selectionSource").toString() == QStringLiteral("environment")
                        && !productionAdapterIdentity.value("cryptoBackend").toObject()
                            .value("available").toBool(true)
                        && !productionAdapterIdentity.value("backendMigrationRequired").toBool(true)
                        && !productionAdapterIdentity.value("agreementSigning").toBool(true),
                    "client identity status should fail closed when production adapter is requested but not linked") && ok;
        productionAdapterClient.setAccountInfo("950098", "secret", false);
        ok = expect(productionAdapterClient.connectToServer("127.0.0.1", port),
                    "production-adapter client should still connect for fail-closed e2e checks") && ok;
        ok = expect(!productionAdapterClient.announceE2EIdentity(aliceId, &rejectReason)
                        && (rejectReason == QStringLiteral("identity-not-ready")
                            || rejectReason == QStringLiteral("e2e-backend-migration-required")),
                    "production-adapter client should not announce unavailable production identity") && ok;
        productionAdapterClient.disconnectFromServer();
        qunsetenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND");

        Client aliceAfterClear;
        Client bobAfterClear;
        QJsonObject afterClearAliceSawBobIdentity;
        QObject::connect(&aliceAfterClear, &Client::e2eIdentityStateChanged, &app, [&](const QString& peerId, const QJsonObject& status) {
            if (peerId == bobId) {
                afterClearAliceSawBobIdentity = status;
            }
        });
        ok = expect(loginClient(aliceAfterClear, aliceId, "Alice", port),
                    "alice should log in after clearing persisted trust") && ok;
        ok = expect(loginClient(bobAfterClear, bobId, "Bob", port),
                    "bob should log in after clearing persisted trust") && ok;
        ok = expect(waitFor([&] {
            return afterClearAliceSawBobIdentity.value("publicKeyFingerprintSha256").toString().size() == 64
                && afterClearAliceSawBobIdentity.value("trustState").toString() == QStringLiteral("unverified")
                && !afterClearAliceSawBobIdentity.value("pinPersisted").toBool(false);
        }), "cleared e2e trust pin should stay cleared across another restart") && ok;
        disconnectClient(aliceAfterClear);
        disconnectClient(bobAfterClear);

        const QString aliceIdentityPath = e2eIdentityFilePath(appDataDir, aliceId);
        const QString aliceTrustPinsPath = e2eTrustPinsFilePath(appDataDir, aliceId);
        ok = expect(writeTextFile(aliceIdentityPath, QByteArray("{\"schema\":\"qtnetworkchat-e2e-identity-v1\",\"privateKey\":\"broken\",\"publicKeyFingerprintSha256\":\"bad\"}")),
                    "test should corrupt alice local e2e identity file") && ok;
        ok = expect(writeTextFile(aliceTrustPinsPath, QByteArray("{\"schema\":\"qtnetworkchat-e2e-trust-pins-v1\",\"pins\":[{\"peerId\":\"920002\",\"fingerprintSha256\":\"not-a-fingerprint\"}]}")),
                    "test should corrupt alice e2e trust pin file") && ok;
        Client aliceRecovered;
        Client bobAfterCorruption;
        QJsonObject recoveredAliceSawBobIdentity;
        QObject::connect(&aliceRecovered, &Client::e2eIdentityStateChanged, &app, [&](const QString& peerId, const QJsonObject& status) {
            if (peerId == bobId) {
                recoveredAliceSawBobIdentity = status;
            }
        });
        ok = expect(loginClient(aliceRecovered, aliceId, "Alice", port),
                    "alice should recover from a corrupted e2e identity store") && ok;
        ok = expect(aliceRecovered.e2eLocalIdentityStatus().value("identityPersisted").toBool(false)
                        && aliceRecovered.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString().size() == 64
                        && aliceRecovered.e2eLocalIdentityStatus().value("publicKeyFingerprintSha256").toString()
                            != originalAliceIdentityFingerprint,
                    "corrupted local e2e identity should be regenerated and persisted") && ok;
        ok = expect(loginClient(bobAfterCorruption, bobId, "Bob", port),
                    "bob should log in after alice e2e store corruption") && ok;
        ok = expect(waitFor([&] {
            return recoveredAliceSawBobIdentity.value("publicKeyFingerprintSha256").toString().size() == 64
                && recoveredAliceSawBobIdentity.value("trustState").toString() == QStringLiteral("unverified")
                && !recoveredAliceSawBobIdentity.value("pinPersisted").toBool(false);
        }), "corrupted e2e trust pin store should be ignored instead of trusting a malformed pin") && ok;
        disconnectClient(aliceRecovered);
        disconnectClient(bobAfterCorruption);
        server.stop();
        drainEvents();
    }
    drainEvents();

    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
