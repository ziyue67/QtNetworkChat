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
    const QJsonObject acceptance = status.value("productionAcceptance").toObject();
    ok = expect(acceptance.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-crypto-acceptance-v1")
                    && acceptance.value("backendId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1")
                    && acceptance.value("providerId").toString()
                        == QStringLiteral("openssl-reviewed-provider-v1")
                    && acceptance.value("linked").toBool(false) == adapterLinked
                    && !acceptance.value("productionReady").toBool(true)
                    && !acceptance.value("accepted").toBool(true)
                    && acceptance.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-operations-not-ready")
                            : QStringLiteral("production-adapter-not-linked"))
                    && acceptance.value("blockedReason").toString() == expectedReason
                    && acceptance.value("operationContractComplete").toBool(false)
                    && acceptance.value("registeredOperationCount").toInt() == 8
                    && acceptance.value("blockedOperationCount").toInt() == 8
                    && acceptance.value("operationGates").toArray().size() == 8
                    && acceptance.value("operationManifest").toArray().size() == 8
                    && acceptance.value("implementedOperationCount").toInt() == 0
                    && !acceptance.value("rawKeyExported").toBool(true)
                    && !acceptance.value("privateMaterialExported").toBool(true),
                "production acceptance status should summarize linked-placeholder gates without enabling crypto") && ok;
    const QJsonObject firstGate = acceptance.value("operationGates").toArray().at(0).toObject();
    ok = expect(firstGate.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstGate.value("entrypoint").toString()
                        == QStringLiteral("production-adapter/session-key-generation")
                    && firstGate.value("implementationState").toString()
                        == (adapterLinked
                            ? QStringLiteral("linked-placeholder")
                            : QStringLiteral("not-linked"))
                    && firstGate.value("vectorSet").toString()
                        == QStringLiteral("production-session-key-generation-vectors-v1")
                    && firstGate.value("compatibilityStatus").toString()
                        == (adapterLinked
                            ? QStringLiteral("not-run-placeholder")
                            : QStringLiteral("not-run-not-linked"))
                    && firstGate.value("migrationBlocker").toString()
                        == QStringLiteral("production-session-key-generation-not-implemented")
                    && !firstGate.value("available").toBool(true)
                    && firstGate.value("blockedReason").toString() == expectedReason
                    && firstGate.value("providerId").toString()
                        == QStringLiteral("openssl-reviewed-provider-v1")
                    && !firstGate.value("rawKeyExported").toBool(true)
                    && !firstGate.value("privateMaterialExported").toBool(true),
                "production acceptance operation gates should be sanitized and dispatch-oriented") && ok;
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
                    && payloadEncrypt.value("operationImplementation").toObject()
                        .value("implementationState").toString()
                            == (adapterLinked
                                ? QStringLiteral("linked-placeholder")
                                : QStringLiteral("not-linked"))
                    && payloadEncrypt.value("operationImplementation").toObject()
                        .value("vectorSet").toString()
                            == QStringLiteral("production-payload-encrypt-vectors-v1")
                    && payloadEncrypt.value("operationImplementation").toObject()
                        .value("migrationBlocker").toString()
                            == QStringLiteral("production-payload-encrypt-not-implemented")
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
