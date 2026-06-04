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
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>

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
                            == QStringLiteral("draft-identity-hmac-sha256")
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
                        && alice.e2eSessionStatus(bobId).value("keyFingerprintSha256").toString().size() == 64,
                    "alice should expose ready e2e session status without the raw key") && ok;

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
        bobMessage = Message();
        ok = expect(alice.sendEncryptedPrivateMessage(bobId, "encrypted again after manual rotation", &rejectReason),
                    "encrypted send should resume after both clients install a local rotated session") && ok;
        ok = expect(waitFor([&] {
            return bobMessage.content == QStringLiteral("encrypted again after manual rotation");
        }), "receiver should decrypt after manual rotation installs matching local keys") && ok;

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
        ok = expect(!mallory.requestE2ESessionRotation(aliceId, &rejectReason)
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
                && restartedAliceSawBobIdentity.value("verificationCode").toString() == aliceBobVerificationCode
                && restartedAliceSawBobIdentity.value("pinPersisted").toBool(false)
                && restartedAliceSawBobIdentity.value("publicKeyFingerprintSha256").toString()
                    == aliceSawBobIdentity.value("publicKeyFingerprintSha256").toString();
        }), "restarted alice should restore the persisted verified e2e trust pin for bob") && ok;
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
