#include "client.h"
#include "e2eenvelope.h"
#include "server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
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
        QString bobError;
        QString malloryError;
        QJsonObject aliceE2EStatus;
        QJsonObject bobE2EStatus;
        QJsonObject bobRotationRequest;
        QJsonObject aliceRotationResponse;
        bool aliceRotationAccepted = false;
        QString aliceRotationResponseReason;
        const QString aliceId = "920001";
        const QString bobId = "920002";
        const QString malloryId = "920003";

        QObject::connect(&bob, &Client::newMessage, &app, [&](const Message& msg) {
            if (msg.type == MessageType::Private) {
                bobMessage = msg;
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

        const QByteArray sessionKey = generateE2ESessionKey();
        const QString keyId = "alice-bob-session-1";

        ok = expect(registerClient(alice, aliceId, "Alice", port), "alice should register and log in") && ok;
        ok = expect(registerClient(bob, bobId, "Bob", port), "bob should register and log in") && ok;
        ok = expect(registerClient(mallory, malloryId, "Mallory", port), "mallory should register and log in") && ok;

        alice.setE2ESessionMessageLimitForTesting(2);
        alice.setE2ESessionKey(bobId, keyId, sessionKey);
        bob.setE2ESessionKey(aliceId, keyId, sessionKey);
        ok = expect(alice.hasE2ESession(bobId)
                        && alice.e2eSessionStatus(bobId).value("state").toString() == "ready"
                        && alice.e2eSessionStatus(bobId).value("keyFingerprintSha256").toString().size() == 64,
                    "alice should expose ready e2e session status without the raw key") && ok;

        QString rejectReason;
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
        ok = expect(alice.requestE2ESessionRotation(bobId, &rejectReason),
                    "sender should send an e2e rotation request after the local rotation gate closes") && ok;
        ok = expect(waitFor([&] {
            return bobRotationRequest.value("senderId").toString() == aliceId
                && bobRotationRequest.value("receiverId").toString() == bobId;
        }), "receiver should observe the e2e rotation request control-plane message") && ok;
        ok = expect(bobRotationRequest.value("keyId").toString().startsWith(QStringLiteral("rotate-"))
                        && bobRotationRequest.value("publicKey").toString().size() > 20
                        && bobRotationRequest.value("publicKeyFingerprintSha256").toString().size() == 64
                        && !bobRotationRequest.contains("sessionKey"),
                    "rotation request should expose only public agreement material and a fingerprint") && ok;
        const QByteArray responsePublicKey = generateE2ESessionKey();
        ok = expect(bob.respondE2ESessionRotation(aliceId,
                                                  bobRotationRequest.value("keyId").toString() + QStringLiteral("-response"),
                                                  responsePublicKey,
                                                  true,
                                                  QStringLiteral("accepted"),
                                                  &rejectReason),
                    "receiver should send an e2e rotation response") && ok;
        ok = expect(waitFor([&] {
            return aliceRotationAccepted
                && aliceRotationResponseReason == QStringLiteral("accepted")
                && aliceRotationResponse.value("senderId").toString() == bobId
                && aliceRotationResponse.value("receiverId").toString() == aliceId;
        }), "sender should observe the e2e rotation response control-plane message") && ok;
        ok = expect(!aliceRotationResponse.contains("sessionKey")
                        && aliceRotationResponse.value("publicKeyFingerprintSha256").toString() == e2eFingerprint(responsePublicKey),
                    "rotation response should not leak a raw session key") && ok;
        ok = expect(alice.e2eSessionNeedsRotation(bobId),
                    "rotation response should not auto-install a remote session key") && ok;
        ok = expect(!alice.sendEncryptedPrivateMessage(bobId, "control plane alone should not reopen encryption", &rejectReason)
                        && rejectReason == QStringLiteral("rotation-required"),
                    "rotation control plane should keep fail-closed behavior until local keys are installed") && ok;

        const QByteArray rotatedSessionKey = generateE2ESessionKey();
        alice.setE2ESessionKey(bobId, keyId + "-manual-rotation", rotatedSessionKey);
        bob.setE2ESessionKey(aliceId, keyId + "-manual-rotation", rotatedSessionKey);
        bobMessage = Message();
        ok = expect(alice.sendEncryptedPrivateMessage(bobId, "encrypted again after manual rotation", &rejectReason),
                    "encrypted send should resume after both clients install a local rotated session") && ok;
        ok = expect(waitFor([&] {
            return bobMessage.content == QStringLiteral("encrypted again after manual rotation");
        }), "receiver should decrypt after manual rotation installs matching local keys") && ok;

        alice.clearE2ESessionKey(bobId);
        ok = expect(!alice.hasE2ESession(bobId)
                        && alice.e2eSessionStatus(bobId).value("state").toString() == QStringLiteral("missing-session"),
                    "clearing an e2e session should expose missing-session status") && ok;
        alice.setE2ESessionKey(bobId, keyId + "-rotated", generateE2ESessionKey());
        ok = expect(alice.hasE2ESession(bobId) && !alice.e2eSessionNeedsRotation(bobId),
                    "setting a new e2e session should clear the rotation gate") && ok;

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
        server.stop();
        drainEvents();
    }
    drainEvents();

    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}
