#include "e2eenvelope.h"
#include "qtnetworkchat_e2e_provider_api.h"

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
    const QJsonObject harness = status.value("productionOperationHarness").toObject();
    const QJsonObject executionPlan = status.value("productionOperationExecutionPlan").toObject();
    const QJsonObject invocation = status.value("productionOperationInvocation").toObject();
    const QJsonObject slotStatus = status.value("productionOperationSlots").toObject();
    const QJsonObject dispatchBindings =
        status.value("productionOperationDispatchBindings").toObject();
    const QJsonObject callableManifest =
        status.value("productionOperationCallableManifest").toObject();
    const QJsonObject executionResult =
        status.value("productionOperationExecutionResult").toObject();
    const QJsonObject providerTable =
        status.value("productionProviderTable").toObject();
    const QJsonObject providerTableBindingProbe =
        status.value("productionProviderTableBindingProbe").toObject();
    ok = expect(harness.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-harness-v1")
                    && harness.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-operation-harness-blocked-placeholder")
                            : QStringLiteral("production-operation-harness-blocked-not-linked"))
                    && harness.value("blockedReason").toString() == (adapterLinked
                        ? QStringLiteral("production-operations-not-implemented")
                        : QStringLiteral("production-crypto-backend-unavailable"))
                    && !harness.value("harnessRunnable").toBool(true)
                    && harness.value("runnableOperationCount").toInt() == 0
                    && harness.value("blockedOperationCount").toInt() == 8
                    && harness.value("operations").toArray().size() == 8
                    && !harness.value("rawKeyExported").toBool(true)
                    && !harness.value("privateMaterialExported").toBool(true),
                "production operation harness should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(executionPlan.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-execution-plan-v1")
                    && executionPlan.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-operation-execution-plan-blocked-placeholder")
                            : QStringLiteral("production-operation-execution-plan-blocked-not-linked"))
                    && executionPlan.value("blockedReason").toString() == (adapterLinked
                        ? QStringLiteral("production-operations-not-implemented")
                        : QStringLiteral("production-crypto-backend-unavailable"))
                    && executionPlan.value("operationHarnessReleaseGate").toString()
                        == harness.value("releaseGate").toString()
                    && !executionPlan.value("accepted").toBool(true)
                    && !executionPlan.value("planReady").toBool(true)
                    && executionPlan.value("runnableStepCount").toInt() == 0
                    && executionPlan.value("blockedStepCount").toInt() == 8
                    && executionPlan.value("steps").toArray().size() == 8
                    && !executionPlan.value("rawKeyExported").toBool(true)
                    && !executionPlan.value("privateMaterialExported").toBool(true),
                "production execution plan should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(invocation.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-invocation-v1")
                    && invocation.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-operation-invocation-blocked-placeholder")
                            : QStringLiteral("production-operation-invocation-blocked-not-linked"))
                    && invocation.value("blockedReason").toString() == (adapterLinked
                        ? QStringLiteral("production-operations-not-implemented")
                        : QStringLiteral("production-crypto-backend-unavailable"))
                    && invocation.value("executionPlanReleaseGate").toString()
                        == executionPlan.value("releaseGate").toString()
                    && !invocation.value("accepted").toBool(true)
                    && invocation.value("callableOperationCount").toInt() == 0
                    && invocation.value("blockedOperationCount").toInt() == 8
                    && invocation.value("invocations").toArray().size() == 8
                    && !invocation.value("rawKeyExported").toBool(true)
                    && !invocation.value("privateMaterialExported").toBool(true),
                "production invocation status should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(slotStatus.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-slots-v1")
                    && slotStatus.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-operation-slots-blocked-placeholder")
                            : QStringLiteral("production-operation-slots-blocked-not-linked"))
                    && slotStatus.value("blockedReason").toString() == (adapterLinked
                        ? QStringLiteral("production-operation-slots-not-reviewed")
                        : QStringLiteral("production-crypto-backend-unavailable"))
                    && !slotStatus.value("accepted").toBool(true)
                    && slotStatus.value("reviewedSlotCount").toInt() == 0
                    && slotStatus.value("callableSlotCount").toInt() == 0
                    && slotStatus.value("blockedSlotCount").toInt() == 8
                    && slotStatus.value("slots").toArray().size() == 8
                    && !slotStatus.value("rawKeyExported").toBool(true)
                    && !slotStatus.value("privateMaterialExported").toBool(true),
                "production slot registry should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(dispatchBindings.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-dispatch-bindings-v1")
                    && dispatchBindings.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-operation-dispatch-bindings-blocked-placeholder")
                            : QStringLiteral("production-operation-dispatch-bindings-blocked-not-linked"))
                    && dispatchBindings.value("blockedReason").toString() == (adapterLinked
                        ? QStringLiteral("production-operation-dispatch-bindings-not-reviewed")
                        : QStringLiteral("production-crypto-backend-unavailable"))
                    && !dispatchBindings.value("accepted").toBool(true)
                    && dispatchBindings.value("reviewedBindingCount").toInt() == 0
                    && dispatchBindings.value("callableBindingCount").toInt() == 0
                    && dispatchBindings.value("blockedBindingCount").toInt() == 8
                    && dispatchBindings.value("bindings").toArray().size() == 8
                    && dispatchBindings.value("operationSlotsReleaseGate").toString()
                        == slotStatus.value("releaseGate").toString()
                    && !dispatchBindings.value("rawKeyExported").toBool(true)
                    && !dispatchBindings.value("privateMaterialExported").toBool(true),
                "production dispatch bindings should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(callableManifest.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-callable-manifest-v1")
                    && callableManifest.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-operation-callable-manifest-blocked-placeholder")
                            : QStringLiteral("production-operation-callable-manifest-blocked-not-linked"))
                    && callableManifest.value("blockedReason").toString() == (adapterLinked
                        ? QStringLiteral("production-callable-manifest-not-reviewed")
                        : QStringLiteral("production-crypto-backend-unavailable"))
                    && !callableManifest.value("accepted").toBool(true)
                    && callableManifest.value("reviewedCallableCount").toInt() == 0
                    && callableManifest.value("blockedCallableCount").toInt() == 8
                    && callableManifest.value("abiMismatchCount").toInt() == 0
                    && callableManifest.value("fixtureMismatchCount").toInt() == 0
                    && callableManifest.value("callables").toArray().size() == 8
                    && callableManifest.value("operationDispatchBindingsReleaseGate").toString()
                        == dispatchBindings.value("releaseGate").toString()
                    && !callableManifest.value("rawKeyExported").toBool(true)
                    && !callableManifest.value("privateMaterialExported").toBool(true),
                "production callable manifest should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(executionResult.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-execution-result-v1")
                    && executionResult.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-operation-execution-results-blocked-placeholder")
                            : QStringLiteral("production-operation-execution-results-blocked-not-linked"))
                    && executionResult.value("blockedReason").toString() == (adapterLinked
                        ? QStringLiteral("production-operation-results-not-executed")
                        : QStringLiteral("production-crypto-backend-unavailable"))
                    && !executionResult.value("accepted").toBool(true)
                    && executionResult.value("passedResultCount").toInt() == 0
                    && executionResult.value("blockedResultCount").toInt() == 8
                    && executionResult.value("sanitizedResultCount").toInt() == 8
                    && executionResult.value("outputContractMismatchCount").toInt() == 0
                    && executionResult.value("results").toArray().size() == 8
                    && executionResult.value("operationCallableManifestReleaseGate").toString()
                        == callableManifest.value("releaseGate").toString()
                    && !executionResult.value("rawKeyExported").toBool(true)
                    && !executionResult.value("privateMaterialExported").toBool(true)
                    && !executionResult.value("sessionSecretExported").toBool(true)
                    && !executionResult.value("privateIdentityMaterialExported").toBool(true),
                "production execution result status should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(providerTable.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-table-v1")
                    && providerTable.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-table-blocked-placeholder")
                            : QStringLiteral("production-provider-table-blocked-not-linked"))
                    && providerTable.value("blockedReason").toString() == (adapterLinked
                        ? QStringLiteral("production-provider-table-placeholder")
                        : QStringLiteral("production-provider-table-not-bound"))
                    && providerTable.value("buildProbeReason").toString() == (adapterLinked
                        ? QStringLiteral("production-provider-table-linked-placeholder")
                        : QStringLiteral("production-provider-table-not-requested"))
                    && providerTable.value("providerApiHeader").toString()
                        == QStringLiteral("include/qtnetworkchat_e2e_provider_api.h")
                    && providerTable.value("headerTableAbi").toString()
                        == QString::fromLatin1(QNC_E2E_PROVIDER_TABLE_ABI)
                    && providerTable.value("abiMatchesHeader").toBool(false)
                    && providerTable.value("headerOperationCount").toInt()
                        == QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT
                    && providerTable.value("operationCountMatchesHeader").toBool(false)
                    && !providerTable.value("accepted").toBool(true)
                    && !providerTable.value("tableBound").toBool(true)
                    && providerTable.value("requiredSymbolCount").toInt() == 8
                    && providerTable.value("boundSymbolCount").toInt() == 0
                    && providerTable.value("missingSymbolCount").toInt() == 8
                    && providerTable.value("callableManifestReleaseGate").toString()
                        == callableManifest.value("releaseGate").toString()
                    && !providerTable.value("rawKeyExported").toBool(true)
                    && !providerTable.value("privateMaterialExported").toBool(true),
                "production provider table should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(providerTableBindingProbe.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-table-binding-probe-v1")
                    && providerTableBindingProbe.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-table-binding-blocked-placeholder")
                            : QStringLiteral("production-provider-table-binding-blocked-not-linked"))
                    && providerTableBindingProbe.value("blockedReason").toString() == (adapterLinked
                        ? QStringLiteral("production-provider-table-placeholder")
                        : QStringLiteral("production-provider-table-not-bound"))
                    && providerTableBindingProbe.value("headerLayoutComplete").toBool(false)
                    && providerTableBindingProbe.value("enumMappingComplete").toBool(false)
                    && providerTableBindingProbe.value("functionPointerSlotsComplete").toBool(false)
                    && !providerTableBindingProbe.value("accepted").toBool(true)
                    && !providerTableBindingProbe.value("tableBound").toBool(true)
                    && providerTableBindingProbe.value("requiredOperationCount").toInt()
                        == QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT
                    && providerTableBindingProbe.value("enumMatchCount").toInt()
                        == QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT
                    && providerTableBindingProbe.value("enumMappings").toArray().size() == 8
                    && providerTableBindingProbe.value("fieldOffsets").toArray().size() == 5
                    && !providerTableBindingProbe.value("rawKeyExported").toBool(true)
                    && !providerTableBindingProbe.value("privateMaterialExported").toBool(true),
                "production provider table binding probe should distinguish not-linked from linked-placeholder gates") && ok;
    const QJsonObject firstHarnessOperation = harness.value("operations").toArray().at(0).toObject();
    ok = expect(firstHarnessOperation.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstHarnessOperation.value("implementationState").toString()
                        == (adapterLinked
                            ? QStringLiteral("linked-placeholder")
                            : QStringLiteral("not-linked"))
                    && firstHarnessOperation.value("fixtureHashSha256").toString().size() == 64
                    && firstHarnessOperation.value("vectorSet").toString()
                        == QStringLiteral("production-session-key-generation-vectors-v1")
                    && firstHarnessOperation.value("blockedReason").toString()
                        == QStringLiteral("production-session-key-generation-not-implemented"),
                "production operation harness should expose sanitized fixture hashes for linked placeholder operations") && ok;
    const QJsonObject firstExecutionStep = executionPlan.value("steps").toArray().at(0).toObject();
    ok = expect(firstExecutionStep.value("sequenceIndex").toInt(-1) == 0
                    && firstExecutionStep.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstExecutionStep.value("dispatchState").toString() == expectedDispatchState
                    && firstExecutionStep.value("implementationState").toString()
                        == (adapterLinked
                            ? QStringLiteral("linked-placeholder")
                            : QStringLiteral("not-linked"))
                    && firstExecutionStep.value("releaseGate").toString()
                        == harness.value("releaseGate").toString()
                    && firstExecutionStep.value("blockedReason").toString()
                        == QStringLiteral("production-session-key-generation-not-implemented")
                    && !firstExecutionStep.value("harnessRunnable").toBool(true),
                "production execution plan should bind each ordered step to harness evidence") && ok;
    const QJsonObject firstInvocation = invocation.value("invocations").toArray().at(0).toObject();
    ok = expect(firstInvocation.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstInvocation.value("invocationState").toString()
                        == (adapterLinked
                            ? QStringLiteral("blocked-linked-placeholder")
                            : QStringLiteral("blocked-not-linked"))
                    && firstInvocation.value("inputContract").toArray().at(0).toString()
                        == QStringLiteral("secure-random-source")
                    && firstInvocation.value("outputContract").toArray().at(0).toString()
                        == QStringLiteral("session-key-handle")
                    && firstInvocation.value("releaseGate").isUndefined()
                    && !firstInvocation.value("callable").toBool(true)
                    && !firstInvocation.value("rawKeyExported").toBool(true),
                "production invocation contracts should expose callable boundaries without enabling placeholder crypto") && ok;
    const QJsonObject firstSlot = slotStatus.value("slots").toArray().at(0).toObject();
    ok = expect(firstSlot.value("slotId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1/session-key-generation-slot")
                    && firstSlot.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstSlot.value("reviewState").toString()
                        == (adapterLinked
                            ? QStringLiteral("placeholder-linked")
                            : QStringLiteral("not-linked"))
                    && firstSlot.value("sideEffectPolicy").toString()
                        == QStringLiteral("may-create-key-handle")
                    && !firstSlot.value("reviewed").toBool(true)
                    && !firstSlot.value("callable").toBool(true),
                "production slot registry should expose reviewed provider symbols without marking placeholders ready") && ok;
    const QJsonObject firstBinding = dispatchBindings.value("bindings").toArray().at(0).toObject();
    ok = expect(firstBinding.value("slotId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1/session-key-generation-slot")
                    && firstBinding.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstBinding.value("bindingState").toString()
                        == (adapterLinked
                            ? QStringLiteral("linked-placeholder")
                            : QStringLiteral("not-linked"))
                    && !firstBinding.value("dispatchCallable").toBool(true)
                    && firstBinding.value("expectedSignature").toString()
                        .contains(QStringLiteral("qnc_e2e_op_session_key_generation_v1"))
                    && firstBinding.value("inputContract").toArray().at(0).toString()
                        == QStringLiteral("secure-random-source")
                    && firstBinding.value("outputContract").toArray().at(0).toString()
                        == QStringLiteral("session-key-handle")
                    && firstBinding.value("blockedReason").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-dispatch-binding-placeholder")
                            : QStringLiteral("production-crypto-backend-unavailable"))
                    && !firstBinding.value("rawKeyExported").toBool(true),
                "production dispatch binding should expose the reviewed provider ABI slot without calling it") && ok;
    const QJsonObject firstCallable = callableManifest.value("callables").toArray().at(0).toObject();
    ok = expect(firstCallable.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstCallable.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstCallable.value("providerAbiSignature").toString()
                        .contains(QStringLiteral("qnc_e2e_op_session_key_generation_v1"))
                    && firstCallable.value("bindingState").toString()
                        == (adapterLinked
                            ? QStringLiteral("linked-placeholder")
                            : QStringLiteral("not-linked"))
                    && firstCallable.value("symbolMatches").toBool(false)
                    && firstCallable.value("abiSignatureMatches").toBool(false)
                    && firstCallable.value("fixtureHashMatches").toBool(false)
                    && !firstCallable.value("dispatchCallable").toBool(true)
                    && !firstCallable.value("callable").toBool(true)
                    && firstCallable.value("blockedReason").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-callable-manifest-placeholder")
                            : QStringLiteral("production-crypto-backend-unavailable"))
                    && !firstCallable.value("rawKeyExported").toBool(true),
                "production callable manifest should expose the reviewed provider callable without calling placeholders") && ok;
    const QJsonObject firstExecutionResult = executionResult.value("results").toArray().at(0).toObject();
    ok = expect(firstExecutionResult.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstExecutionResult.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstExecutionResult.value("providerAbiSignature").toString()
                        .contains(QStringLiteral("qnc_e2e_op_session_key_generation_v1"))
                    && firstExecutionResult.value("resultState").toString()
                        == (adapterLinked
                            ? QStringLiteral("blocked-linked-placeholder")
                            : QStringLiteral("blocked-not-linked"))
                    && firstExecutionResult.value("errorClass").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-result-placeholder-not-executed")
                            : QStringLiteral("production-result-adapter-not-linked"))
                    && firstExecutionResult.value("outputContract").toArray().at(0).toString()
                        == QStringLiteral("session-key-handle")
                    && firstExecutionResult.value("resultContract").toArray().size() == 4
                    && !firstExecutionResult.value("passed").toBool(true)
                    && firstExecutionResult.value("sanitized").toBool(false)
                    && !firstExecutionResult.value("rawKeyExported").toBool(true)
                    && !firstExecutionResult.value("privateMaterialExported").toBool(true)
                    && !firstExecutionResult.value("sessionSecretExported").toBool(true),
                "production execution result contract should expose sanitized execution output boundaries") && ok;
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
                    && acceptance.value("operationDispatchBindings").toObject()
                        .value("bindings").toArray().size() == 8
                    && acceptance.value("operationCallableManifest").toObject()
                        .value("callables").toArray().size() == 8
                    && !acceptance.value("operationCallableManifest").toObject()
                        .value("accepted").toBool(true)
                    && acceptance.value("operationExecutionResult").toObject()
                        .value("results").toArray().size() == 8
                    && !acceptance.value("operationExecutionResult").toObject()
                        .value("accepted").toBool(true)
                    && acceptance.value("passedExecutionResultCount").toInt() == 0
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
