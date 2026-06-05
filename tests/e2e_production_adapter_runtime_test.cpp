#include "e2eenvelope.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning() << message;
        return false;
    }
    return true;
}
}

int main() {
    bool ok = true;

    qputenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND", "production");
    QString reason;
    const QJsonObject status = e2eCryptoBackendStatus();
    const bool adapterLinked = status.value("productionAdapterLinked").toBool(false);
    const QString expectedReason = adapterLinked
        ? QStringLiteral("production-adapter-not-ready")
        : QStringLiteral("production-crypto-backend-unavailable");
    const QString expectedAction = adapterLinked
        ? QStringLiteral("complete-production-crypto-adapter-implementation-and-compatibility-tests")
        : QStringLiteral("link-reviewed-production-crypto-backend");
    const QString expectedDispatchState = adapterLinked
        ? QStringLiteral("linked-placeholder-not-ready")
        : QStringLiteral("not-linked");
    const QString expectedReadinessGate = adapterLinked
        ? QStringLiteral("production-operations-not-implemented")
        : QStringLiteral("production-adapter-not-linked");
    const QString expectedSelfTestStatus = adapterLinked
        ? QStringLiteral("self-test-blocked-placeholder")
        : QStringLiteral("self-test-blocked-not-linked");
    const QString expectedCompatibilityStatus = adapterLinked
        ? QStringLiteral("compatibility-blocked-placeholder")
        : QStringLiteral("compatibility-blocked-not-linked");
    const QString expectedCompatibilityGate = adapterLinked
        ? QStringLiteral("production-operation-vectors-not-implemented")
        : QStringLiteral("production-adapter-not-linked");

    ok = expect(status.value("requestedBackendId").toString() == QStringLiteral("openssl-reviewed-adapter-v1")
                    && status.value("selectionSource").toString() == QStringLiteral("environment")
                    && !status.value("available").toBool(true)
                    && status.value("selectedBackendId").toString().isEmpty()
                    && status.value("productionReady").toBool(true) == false
                    && status.value("selectedProviderReadiness").toObject()
                        .value("readinessGate").toString()
                            == expectedReadinessGate
                    && status.value("selectedProviderReadiness").toObject()
                        .value("selfTestStatus").toString()
                            == expectedSelfTestStatus
                    && status.value("selectedProviderCompatibility").toObject()
                        .value("status").toString()
                            == expectedCompatibilityStatus
                    && status.value("selectedProviderCompatibility").toObject()
                        .value("gate").toString()
                            == expectedCompatibilityGate
                    && !status.value("selectedProviderCompatibility").toObject()
                        .value("knownAnswerPassed").toBool(true)
                    && status.value("selectedProviderCompatibility").toObject()
                        .value("requiredOperations").toArray().size() == 8
                    && status.value("unavailableReason").toString() == expectedReason,
                "production adapter runtime status should fail closed with a precise reason") && ok;

    const QJsonObject payloadEncrypt =
        status.value("operations").toObject().value("payload-encrypt").toObject();
    ok = expect(payloadEncrypt.value("backendId").toString() == QStringLiteral("openssl-reviewed-adapter-v1")
                    && payloadEncrypt.value("entrypoint").toString() == QStringLiteral("production-adapter/payload-encrypt")
                    && payloadEncrypt.value("providerId").toString() == QStringLiteral("openssl-reviewed-provider-v1")
                    && payloadEncrypt.value("operationContractVersion").toString()
                        == QStringLiteral("qtnetworkchat-e2e-crypto-ops-v1")
                    && payloadEncrypt.value("dispatchState").toString() == expectedDispatchState
                    && payloadEncrypt.value("providerSelfTestStatus").toString()
                        == expectedSelfTestStatus
                    && payloadEncrypt.value("providerReadinessGate").toString()
                        == expectedReadinessGate
                    && payloadEncrypt.value("providerCompatibilityStatus").toString()
                        == expectedCompatibilityStatus
                    && payloadEncrypt.value("providerCompatibilityGate").toString()
                        == expectedCompatibilityGate
                    && payloadEncrypt.value("requiresProductionReady").toBool(false)
                    && payloadEncrypt.value("adapterLinked").toBool(!adapterLinked) == adapterLinked
                    && payloadEncrypt.value("productionReady").toBool(true) == false
                    && payloadEncrypt.value("available").toBool(true) == false
                    && payloadEncrypt.value("reason").toString() == expectedReason
                    && payloadEncrypt.value("blockedReason").toString() == expectedReason
                    && payloadEncrypt.value("operatorAction").toString() == expectedAction
                    && payloadEncrypt.value("rawKeyExported").toBool(true) == false
                    && payloadEncrypt.value("privateMaterialExported").toBool(true) == false,
                "production adapter operation should expose linked-placeholder evidence without enabling data-plane crypto") && ok;

    const QByteArray sessionKey = generateE2ESessionKey();
    ok = expect(sessionKey.isEmpty(),
                "production adapter runtime should not generate a session key until productionReady is true") && ok;

    const E2EEnvelope envelope = encryptE2EPayload(QStringLiteral("10001"),
                                                   QStringLiteral("10002"),
                                                   QStringLiteral("production-runtime-gate"),
                                                   QByteArray(32, '\x11'),
                                                   QByteArray("payload", 7),
                                                   QStringLiteral("runtime-gate"),
                                                   &reason);
    ok = expect(!envelope.isValid()
                    && reason == expectedReason,
                "production adapter runtime should block payload encryption at the shared execution context") && ok;

    qunsetenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND");
    return ok ? 0 : 1;
}
