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
