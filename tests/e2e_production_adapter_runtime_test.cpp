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
    const QJsonObject providerTableRegistration =
        status.value("productionProviderTableRegistration").toObject();
    const QJsonObject providerOperationPreflight =
        status.value("productionProviderOperationPreflight").toObject();
    const QJsonObject providerCallFrame =
        status.value("productionProviderCallFrame").toObject();
    const QJsonObject providerInvocationDryRun =
        status.value("productionProviderInvocationDryRun").toObject();
    const QJsonObject providerInvocationResult =
        status.value("productionProviderInvocationResult").toObject();
    const QJsonObject providerExecutionDecision =
        status.value("productionProviderExecutionDecision").toObject();
    const QJsonObject providerCallbackHarness =
        status.value("productionProviderCallbackHarness").toObject();
    const QJsonObject providerVectorSelfTest =
        status.value("productionProviderVectorSelfTest").toObject();
    const QJsonObject providerExecutionSlotBinding =
        status.value("productionProviderExecutionSlotBinding").toObject();
    const QJsonObject providerExecutionPath =
        status.value("productionProviderExecutionPath").toObject();
    const QJsonObject providerInvocationSandbox =
        status.value("productionProviderInvocationSandbox").toObject();
    const QJsonObject providerInvocationVectorResult =
        status.value("productionProviderInvocationVectorResult").toObject();
    const QJsonObject providerInvocationExecution =
        status.value("productionProviderInvocationExecution").toObject();
    const QJsonObject providerReviewedExecutionCandidate =
        status.value("productionProviderReviewedExecutionCandidate").toObject();
    const QJsonObject providerReviewedCallHandoff =
        status.value("productionProviderReviewedCallHandoff").toObject();
    const QJsonObject providerReviewedOperationStubBoundary =
        status.value("productionProviderReviewedOperationStubBoundary").toObject();
    const QJsonObject providerReviewedCallableTableBridge =
        status.value("productionProviderReviewedCallableTableBridge").toObject();
    const QJsonObject providerReviewedOperationCallableInterface =
        status.value("productionProviderReviewedOperationCallableInterface").toObject();
    const QJsonObject providerReviewedCallableRuntimePreflight =
        status.value("productionProviderReviewedCallableRuntimePreflight").toObject();
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
                    && !providerTable.value("providerTableRegistered").toBool(true)
                    && !providerTable.value("registrationAccepted").toBool(true)
                    && providerTable.value("registrationReleaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-table-registration-blocked-placeholder")
                            : QStringLiteral("production-provider-table-registration-blocked-not-linked"))
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
                    && providerTableBindingProbe.value("tableValidation").toObject()
                        .value("blockedReason").toString()
                            == QStringLiteral("production-provider-table-not-bound")
                    && !providerTableBindingProbe.value("tableValidationAccepted").toBool(true)
                    && !providerTableBindingProbe.value("accepted").toBool(true)
                    && !providerTableBindingProbe.value("tableBound").toBool(true)
                    && !providerTableBindingProbe.value("providerTableRegistered").toBool(true)
                    && !providerTableBindingProbe.value("registrationAccepted").toBool(true)
                    && providerTableBindingProbe.value("registrationReleaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-table-registration-blocked-placeholder")
                            : QStringLiteral("production-provider-table-registration-blocked-not-linked"))
                    && providerTableBindingProbe.value("requiredOperationCount").toInt()
                        == QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT
                    && providerTableBindingProbe.value("enumMatchCount").toInt()
                        == QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT
                    && providerTableBindingProbe.value("enumMappings").toArray().size() == 8
                    && providerTableBindingProbe.value("fieldOffsets").toArray().size() == 5
                    && !providerTableBindingProbe.value("rawKeyExported").toBool(true)
                    && !providerTableBindingProbe.value("privateMaterialExported").toBool(true),
                "production provider table binding probe should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(providerTableRegistration.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-table-registration-v1")
                    && providerTableRegistration.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-table-registration-blocked-placeholder")
                            : QStringLiteral("production-provider-table-registration-blocked-not-linked"))
                    && providerTableRegistration.value("blockedReason").toString()
                        == QStringLiteral("production-provider-table-not-registered")
                    && !providerTableRegistration.value("registered").toBool(true)
                    && !providerTableRegistration.value("accepted").toBool(true)
                    && providerTableRegistration.value("registrationSource").toString()
                        == (adapterLinked
                            ? QStringLiteral("linked-placeholder-without-runtime-table")
                            : QStringLiteral("not-linked"))
                    && providerTableRegistration.value("tableValidation").toObject()
                        .value("blockedReason").toString()
                            == QStringLiteral("production-provider-table-not-bound")
                    && !providerTableRegistration.value("tableValidationAccepted").toBool(true)
                    && !providerTableRegistration.value("rawKeyExported").toBool(true)
                    && !providerTableRegistration.value("privateMaterialExported").toBool(true),
                "production provider table registration should expose runtime binding evidence without enabling placeholders") && ok;
    ok = expect(providerOperationPreflight.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-operation-preflight-v1")
                    && providerOperationPreflight.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-operation-preflight-blocked-placeholder")
                            : QStringLiteral("production-provider-operation-preflight-blocked-not-linked"))
                    && providerOperationPreflight.value("blockedReason").toString()
                        == QStringLiteral("production-provider-table-not-registered")
                    && !providerOperationPreflight.value("accepted").toBool(true)
                    && !providerOperationPreflight.value("providerTableRegistered").toBool(true)
                    && providerOperationPreflight.value("presentOperationCount").toInt() == 0
                    && providerOperationPreflight.value("blockedOperationCount").toInt() == 8
                    && providerOperationPreflight.value("abiMatchedOperationCount").toInt() == 8
                    && providerOperationPreflight.value("contractMatchedOperationCount").toInt() == 8
                    && providerOperationPreflight.value("fixtureMatchedOperationCount").toInt() == 8
                    && providerOperationPreflight.value("operations").toArray().size() == 8
                    && !providerOperationPreflight.value("operationInvoked").toBool(true)
                    && !providerOperationPreflight.value("rawKeyExported").toBool(true)
                    && !providerOperationPreflight.value("privateMaterialExported").toBool(true),
                "production provider operation preflight should validate dispatch readiness without invoking crypto") && ok;
    ok = expect(providerCallFrame.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-call-frame-v1")
                    && providerCallFrame.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-call-frame-blocked-placeholder")
                            : QStringLiteral("production-provider-call-frame-blocked-not-linked"))
                    && !providerCallFrame.value("accepted").toBool(true)
                    && providerCallFrame.value("readyFrameCount").toInt() == 0
                    && providerCallFrame.value("blockedFrameCount").toInt() == 8
                    && providerCallFrame.value("sanitizedFrameCount").toInt() == 8
                    && providerCallFrame.value("enumMatchedFrameCount").toInt() == 8
                    && providerCallFrame.value("contractHashCount").toInt() == 8
                    && providerCallFrame.value("frames").toArray().size() == 8
                    && providerCallFrame.value("providerOperationPreflightReleaseGate").toString()
                        == providerOperationPreflight.value("releaseGate").toString()
                    && !providerCallFrame.value("operationInvoked").toBool(true)
                    && !providerCallFrame.value("inputBytesAttached").toBool(true)
                    && !providerCallFrame.value("outputBytesAttached").toBool(true)
                    && !providerCallFrame.value("rawKeyExported").toBool(true)
                    && !providerCallFrame.value("privateMaterialExported").toBool(true),
                "production provider call frame should bind sanitized ABI inputs before dry-run") && ok;
    ok = expect(providerInvocationDryRun.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-dry-run-v1")
                    && providerInvocationDryRun.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-invocation-dry-run-blocked-placeholder")
                            : QStringLiteral("production-provider-invocation-dry-run-blocked-not-linked"))
                    && !providerInvocationDryRun.value("accepted").toBool(true)
                    && providerInvocationDryRun.value("dryRunReadyCount").toInt() == 0
                    && providerInvocationDryRun.value("blockedInvocationCount").toInt() == 8
                    && providerInvocationDryRun.value("sanitizedInvocationCount").toInt() == 8
                    && providerInvocationDryRun.value("invocations").toArray().size() == 8
                    && providerInvocationDryRun.value("providerCallFrameReleaseGate").toString()
                        == providerCallFrame.value("releaseGate").toString()
                    && !providerInvocationDryRun.value("operationInvoked").toBool(true)
                    && !providerInvocationDryRun.value("rawKeyExported").toBool(true)
                    && !providerInvocationDryRun.value("privateMaterialExported").toBool(true),
                "production provider invocation dry-run should expose sanitized call evidence without invoking crypto") && ok;
    ok = expect(providerInvocationResult.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-result-v1")
                    && providerInvocationResult.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-invocation-results-blocked-placeholder")
                            : QStringLiteral("production-provider-invocation-results-blocked-not-linked"))
                    && !providerInvocationResult.value("accepted").toBool(true)
                    && providerInvocationResult.value("captureReadyCount").toInt() == 0
                    && providerInvocationResult.value("blockedResultCount").toInt() == 8
                    && providerInvocationResult.value("sanitizedResultCount").toInt() == 8
                    && providerInvocationResult.value("outputContractProofCount").toInt() == 8
                    && providerInvocationResult.value("fixtureProofCount").toInt() == 8
                    && providerInvocationResult.value("materialExportProofCount").toInt() == 8
                    && providerInvocationResult.value("results").toArray().size() == 8
                    && providerInvocationResult.value("providerInvocationDryRunReleaseGate").toString()
                        == providerInvocationDryRun.value("releaseGate").toString()
                    && !providerInvocationResult.value("operationInvoked").toBool(true)
                    && !providerInvocationResult.value("resultCaptured").toBool(true)
                    && !providerInvocationResult.value("rawKeyExported").toBool(true)
                    && !providerInvocationResult.value("privateMaterialExported").toBool(true),
                "production provider invocation result capture should stay sanitized and non-executing") && ok;
    ok = expect(providerExecutionDecision.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-execution-decision-v1")
                    && providerExecutionDecision.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-execution-decision-blocked-placeholder")
                            : QStringLiteral("production-provider-execution-decision-blocked-not-linked"))
                    && !providerExecutionDecision.value("accepted").toBool(true)
                    && providerExecutionDecision.value("allowedDecisionCount").toInt() == 0
                    && providerExecutionDecision.value("blockedDecisionCount").toInt() == 8
                    && providerExecutionDecision.value("sanitizedDecisionCount").toInt() == 8
                    && providerExecutionDecision.value("decisions").toArray().size() == 8
                    && providerExecutionDecision.value("providerInvocationResultReleaseGate").toString()
                        == providerInvocationResult.value("releaseGate").toString()
                    && !providerExecutionDecision.value("operationInvoked").toBool(true)
                    && !providerExecutionDecision.value("resultCaptured").toBool(true)
                    && !providerExecutionDecision.value("rawKeyExported").toBool(true)
                    && !providerExecutionDecision.value("privateMaterialExported").toBool(true),
                "production provider execution decision should block reviewed callbacks until result capture is accepted") && ok;
    ok = expect(providerCallbackHarness.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-callback-harness-v1")
                    && providerCallbackHarness.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-callback-harness-blocked-placeholder")
                            : QStringLiteral("production-provider-callback-harness-blocked-not-linked"))
                    && !providerCallbackHarness.value("accepted").toBool(true)
                    && providerCallbackHarness.value("armedCallbackCount").toInt() == 0
                    && providerCallbackHarness.value("blockedCallbackCount").toInt() == 8
                    && providerCallbackHarness.value("sanitizedCallbackCount").toInt() == 8
                    && providerCallbackHarness.value("inputCapturePolicyCount").toInt() == 8
                    && providerCallbackHarness.value("outputCapturePolicyCount").toInt() == 8
                    && providerCallbackHarness.value("resultCapturePolicyCount").toInt() == 8
                    && providerCallbackHarness.value("callbacks").toArray().size() == 8
                    && providerCallbackHarness.value("providerExecutionDecisionReleaseGate").toString()
                        == providerExecutionDecision.value("releaseGate").toString()
                    && !providerCallbackHarness.value("operationInvoked").toBool(true)
                    && !providerCallbackHarness.value("inputBytesCaptured").toBool(true)
                    && !providerCallbackHarness.value("outputBytesCaptured").toBool(true)
                    && !providerCallbackHarness.value("resultCaptured").toBool(true)
                    && !providerCallbackHarness.value("rawKeyExported").toBool(true)
                    && !providerCallbackHarness.value("privateMaterialExported").toBool(true),
                "production provider callback harness should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(providerVectorSelfTest.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-vector-self-test-v1")
                    && providerVectorSelfTest.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-vector-self-test-blocked-placeholder")
                            : QStringLiteral("production-provider-vector-self-test-blocked-not-linked"))
                    && !providerVectorSelfTest.value("accepted").toBool(true)
                    && providerVectorSelfTest.value("passedVectorCount").toInt() == 0
                    && providerVectorSelfTest.value("blockedVectorCount").toInt() == 8
                    && providerVectorSelfTest.value("knownAnswerReadyCount").toInt() == 0
                    && providerVectorSelfTest.value("sanitizedVectorCount").toInt() == 8
                    && providerVectorSelfTest.value("materialExportProofCount").toInt() == 8
                    && providerVectorSelfTest.value("tests").toArray().size() == 8
                    && providerVectorSelfTest.value("providerCallbackHarnessReleaseGate").toString()
                        == providerCallbackHarness.value("releaseGate").toString()
                    && !providerVectorSelfTest.value("operationInvoked").toBool(true)
                    && !providerVectorSelfTest.value("inputBytesCaptured").toBool(true)
                    && !providerVectorSelfTest.value("outputBytesCaptured").toBool(true)
                    && !providerVectorSelfTest.value("resultCaptured").toBool(true)
                    && !providerVectorSelfTest.value("rawKeyExported").toBool(true)
                    && !providerVectorSelfTest.value("privateMaterialExported").toBool(true),
                "production provider vector self-test should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(providerExecutionSlotBinding.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-execution-slot-binding-v1")
                    && providerExecutionSlotBinding.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-execution-slot-binding-blocked-placeholder")
                            : QStringLiteral("production-provider-execution-slot-binding-blocked-not-linked"))
                    && !providerExecutionSlotBinding.value("accepted").toBool(true)
                    && providerExecutionSlotBinding.value("bindableSlotCount").toInt() == 0
                    && providerExecutionSlotBinding.value("blockedSlotCount").toInt() == 8
                    && providerExecutionSlotBinding.value("reviewedSlotCount").toInt() == 0
                    && providerExecutionSlotBinding.value("contractMatchedSlotCount").toInt() == 8
                    && providerExecutionSlotBinding.value("fixtureMatchedSlotCount").toInt() == 8
                    && providerExecutionSlotBinding.value("sanitizedSlotCount").toInt() == 8
                    && providerExecutionSlotBinding.value("slots").toArray().size() == 8
                    && providerExecutionSlotBinding.value("providerVectorSelfTestReleaseGate").toString()
                        == providerVectorSelfTest.value("releaseGate").toString()
                    && !providerExecutionSlotBinding.value("operationInvoked").toBool(true)
                    && !providerExecutionSlotBinding.value("inputBytesCaptured").toBool(true)
                    && !providerExecutionSlotBinding.value("outputBytesCaptured").toBool(true)
                    && !providerExecutionSlotBinding.value("resultCaptured").toBool(true)
                    && !providerExecutionSlotBinding.value("rawKeyExported").toBool(true)
                    && !providerExecutionSlotBinding.value("privateMaterialExported").toBool(true),
                "production provider execution slot binding should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(providerExecutionPath.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-execution-path-v1")
                    && providerExecutionPath.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-execution-path-blocked-placeholder")
                            : QStringLiteral("production-provider-execution-path-blocked-not-linked"))
                    && !providerExecutionPath.value("accepted").toBool(true)
                    && providerExecutionPath.value("mappedPathCount").toInt() == 0
                    && providerExecutionPath.value("blockedPathCount").toInt() == 8
                    && providerExecutionPath.value("pointerPresentCount").toInt() == 0
                    && providerExecutionPath.value("bindableSlotCount").toInt() == 0
                    && providerExecutionPath.value("capturePolicyCount").toInt() == 8
                    && providerExecutionPath.value("sanitizedPathCount").toInt() == 8
                    && providerExecutionPath.value("paths").toArray().size() == 8
                    && providerExecutionPath.value("providerExecutionSlotBindingReleaseGate").toString()
                        == providerExecutionSlotBinding.value("releaseGate").toString()
                    && !providerExecutionPath.value("operationInvoked").toBool(true)
                    && !providerExecutionPath.value("inputBytesCaptured").toBool(true)
                    && !providerExecutionPath.value("outputBytesCaptured").toBool(true)
                    && !providerExecutionPath.value("resultCaptured").toBool(true)
                    && !providerExecutionPath.value("rawKeyExported").toBool(true)
                    && !providerExecutionPath.value("privateMaterialExported").toBool(true),
                "production provider execution path should map registered function pointers only after slot binding") && ok;
    ok = expect(providerInvocationSandbox.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-sandbox-v1")
                    && providerInvocationSandbox.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-invocation-sandbox-blocked-placeholder")
                            : QStringLiteral("production-provider-invocation-sandbox-blocked-not-linked"))
                    && !providerInvocationSandbox.value("accepted").toBool(true)
                    && providerInvocationSandbox.value("readySandboxCount").toInt() == 0
                    && providerInvocationSandbox.value("blockedSandboxCount").toInt() == 8
                    && providerInvocationSandbox.value("mappedPathCount").toInt() == 0
                    && providerInvocationSandbox.value("sanitizedSandboxCount").toInt() == 8
                    && providerInvocationSandbox.value("timeoutPolicyCount").toInt() == 8
                    && providerInvocationSandbox.value("errorPolicyCount").toInt() == 8
                    && providerInvocationSandbox.value("materialPolicyCount").toInt() == 8
                    && providerInvocationSandbox.value("sandboxes").toArray().size() == 8
                    && providerInvocationSandbox.value("providerExecutionPathReleaseGate").toString()
                        == providerExecutionPath.value("releaseGate").toString()
                    && !providerInvocationSandbox.value("operationInvoked").toBool(true)
                    && !providerInvocationSandbox.value("inputBytesCaptured").toBool(true)
                    && !providerInvocationSandbox.value("outputBytesCaptured").toBool(true)
                    && !providerInvocationSandbox.value("resultCaptured").toBool(true)
                    && !providerInvocationSandbox.value("rawKeyExported").toBool(true)
                    && !providerInvocationSandbox.value("privateMaterialExported").toBool(true),
                "production provider invocation sandbox should expose call policies without invoking placeholders") && ok;
    ok = expect(providerInvocationVectorResult.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-vector-result-v1")
                    && providerInvocationVectorResult.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-invocation-vector-result-blocked-placeholder")
                            : QStringLiteral("production-provider-invocation-vector-result-blocked-not-linked"))
                    && !providerInvocationVectorResult.value("accepted").toBool(true)
                    && providerInvocationVectorResult.value("readyVectorResultCount").toInt() == 0
                    && providerInvocationVectorResult.value("blockedVectorResultCount").toInt() == 8
                    && providerInvocationVectorResult.value("sandboxReadyCount").toInt() == 0
                    && providerInvocationVectorResult.value("fixtureMatchedCount").toInt() == 8
                    && providerInvocationVectorResult.value("resultContractCount").toInt() == 8
                    && providerInvocationVectorResult.value("sanitizedResultCount").toInt() == 8
                    && providerInvocationVectorResult.value("materialExportProofCount").toInt() == 8
                    && providerInvocationVectorResult.value("vectorResults").toArray().size() == 8
                    && providerInvocationVectorResult.value("providerInvocationSandboxReleaseGate").toString()
                        == providerInvocationSandbox.value("releaseGate").toString()
                    && !providerInvocationVectorResult.value("operationInvoked").toBool(true)
                    && !providerInvocationVectorResult.value("inputBytesCaptured").toBool(true)
                    && !providerInvocationVectorResult.value("outputBytesCaptured").toBool(true)
                    && !providerInvocationVectorResult.value("resultCaptured").toBool(true)
                    && !providerInvocationVectorResult.value("rawKeyExported").toBool(true)
                    && !providerInvocationVectorResult.value("privateMaterialExported").toBool(true),
                "production provider invocation vector result should expose sanitized vector evidence without invoking placeholders") && ok;
    ok = expect(providerInvocationExecution.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-execution-v1")
                    && providerInvocationExecution.value("releaseGate").toString()
                        == (adapterLinked
                            ? QStringLiteral("production-provider-invocation-execution-blocked-placeholder")
                            : QStringLiteral("production-provider-invocation-execution-blocked-not-linked"))
                    && !providerInvocationExecution.value("accepted").toBool(true)
                    && providerInvocationExecution.value("readyExecutionCount").toInt() == 0
                    && providerInvocationExecution.value("blockedExecutionCount").toInt() == 8
                    && providerInvocationExecution.value("vectorResultReadyCount").toInt() == 0
                    && providerInvocationExecution.value("callableEntryPointCount").toInt() == 8
                    && providerInvocationExecution.value("sanitizedExecutionCount").toInt() == 8
                    && providerInvocationExecution.value("resultCapturePolicyCount").toInt() == 8
                    && providerInvocationExecution.value("materialExportProofCount").toInt() == 8
                    && providerInvocationExecution.value("executions").toArray().size() == 8
                    && providerInvocationExecution.value("providerInvocationVectorResultReleaseGate").toString()
                        == providerInvocationVectorResult.value("releaseGate").toString()
                    && !providerInvocationExecution.value("operationInvoked").toBool(true)
                    && !providerInvocationExecution.value("inputBytesCaptured").toBool(true)
                    && !providerInvocationExecution.value("outputBytesCaptured").toBool(true)
                    && !providerInvocationExecution.value("resultCaptured").toBool(true)
                    && !providerInvocationExecution.value("rawKeyExported").toBool(true)
                    && !providerInvocationExecution.value("privateMaterialExported").toBool(true),
                "production provider invocation execution should expose controlled call entrypoints without invoking placeholders") && ok;
    ok = expect(providerReviewedExecutionCandidate.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-execution-candidate-v1")
                    && providerReviewedExecutionCandidate.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-execution-candidate-not-release-gate")
                    && !providerReviewedExecutionCandidate.value("accepted").toBool(true)
                    && providerReviewedExecutionCandidate.value("candidateNonReleaseGate").toBool(false)
                    && !providerReviewedExecutionCandidate.value("probeSourceCaptured").toBool(true)
                    && providerReviewedExecutionCandidate.value("candidateCount").toInt() == 8
                    && providerReviewedExecutionCandidate.value("candidateReadyCount").toInt() == 0
                    && providerReviewedExecutionCandidate.value("blockedCandidateCount").toInt() == 8
                    && providerReviewedExecutionCandidate.value("probeMatrixMatchedCount").toInt() == 0
                    && providerReviewedExecutionCandidate.value("candidateSanitizedCount").toInt() == 0
                    && providerReviewedExecutionCandidate.value("candidateEntrypointCount").toInt() == 0
                    && providerReviewedExecutionCandidate.value("blockedReason").toString()
                        == QStringLiteral("production-provider-reviewed-candidate-awaiting-explicit-probe")
                    && providerReviewedExecutionCandidate.value("candidates").toArray().size() == 8
                    && !providerReviewedExecutionCandidate.value("operationInvokedByCandidate").toBool(true)
                    && !providerReviewedExecutionCandidate.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedExecutionCandidate.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedExecutionCandidate.value("rawKeyExported").toBool(true)
                    && !providerReviewedExecutionCandidate.value("privateMaterialExported").toBool(true)
                    && !providerReviewedExecutionCandidate.value("sessionSecretExported").toBool(true),
                "production reviewed execution candidate should stay blocked until an explicit sanitized probe is captured") && ok;
    ok = expect(providerReviewedCallHandoff.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-call-handoff-v1")
                    && providerReviewedCallHandoff.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-call-handoff-not-release-gate")
                    && !providerReviewedCallHandoff.value("accepted").toBool(true)
                    && providerReviewedCallHandoff.value("handoffNonReleaseGate").toBool(false)
                    && !providerReviewedCallHandoff.value("candidateSourceCaptured").toBool(true)
                    && providerReviewedCallHandoff.value("handoffCount").toInt() == 8
                    && providerReviewedCallHandoff.value("readyHandoffCount").toInt() == 0
                    && providerReviewedCallHandoff.value("blockedHandoffCount").toInt() == 8
                    && providerReviewedCallHandoff.value("failClosedHandoffCount").toInt() == 8
                    && providerReviewedCallHandoff.value("blockedReason").toString()
                        == QStringLiteral("production-provider-reviewed-call-handoff-awaiting-candidate")
                    && providerReviewedCallHandoff.value("handoffs").toArray().size() == 8
                    && !providerReviewedCallHandoff.value("operationInvokedByHandoff").toBool(true)
                    && !providerReviewedCallHandoff.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedCallHandoff.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedCallHandoff.value("resultCaptured").toBool(true)
                    && !providerReviewedCallHandoff.value("rawKeyExported").toBool(true)
                    && !providerReviewedCallHandoff.value("privateMaterialExported").toBool(true)
                    && !providerReviewedCallHandoff.value("sessionSecretExported").toBool(true),
                "production reviewed call handoff should stay blocked until reviewed candidates exist") && ok;
    ok = expect(providerReviewedOperationStubBoundary.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-operation-stub-boundary-v1")
                    && providerReviewedOperationStubBoundary.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-operation-stub-not-release-gate")
                    && !providerReviewedOperationStubBoundary.value("accepted").toBool(true)
                    && providerReviewedOperationStubBoundary.value("stubNonReleaseGate").toBool(false)
                    && !providerReviewedOperationStubBoundary.value("handoffSourceCaptured").toBool(true)
                    && providerReviewedOperationStubBoundary.value("stubCount").toInt() == 8
                    && providerReviewedOperationStubBoundary.value("readyStubCount").toInt() == 0
                    && providerReviewedOperationStubBoundary.value("blockedStubCount").toInt() == 8
                    && providerReviewedOperationStubBoundary.value("failClosedStubCount").toInt() == 8
                    && providerReviewedOperationStubBoundary.value("blockedReason").toString()
                        == QStringLiteral("production-provider-reviewed-operation-stub-awaiting-handoff")
                    && providerReviewedOperationStubBoundary.value("stubs").toArray().size() == 8
                    && !providerReviewedOperationStubBoundary.value("operationInvokedByStub").toBool(true)
                    && !providerReviewedOperationStubBoundary.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedOperationStubBoundary.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedOperationStubBoundary.value("resultCaptured").toBool(true)
                    && !providerReviewedOperationStubBoundary.value("rawKeyExported").toBool(true)
                    && !providerReviewedOperationStubBoundary.value("privateMaterialExported").toBool(true)
                    && !providerReviewedOperationStubBoundary.value("sessionSecretExported").toBool(true),
                "production reviewed operation stub boundary should stay blocked until handoff evidence exists") && ok;
    ok = expect(providerReviewedCallableTableBridge.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-callable-table-bridge-v1")
                    && providerReviewedCallableTableBridge.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-callable-table-bridge-not-release-gate")
                    && !providerReviewedCallableTableBridge.value("accepted").toBool(true)
                    && providerReviewedCallableTableBridge.value("bridgeNonReleaseGate").toBool(false)
                    && !providerReviewedCallableTableBridge.value("stubSourceCaptured").toBool(true)
                    && providerReviewedCallableTableBridge.value("bridgeCount").toInt() == 8
                    && providerReviewedCallableTableBridge.value("readyBridgeCount").toInt() == 0
                    && providerReviewedCallableTableBridge.value("blockedBridgeCount").toInt() == 8
                    && providerReviewedCallableTableBridge.value("failClosedBridgeCount").toInt() == 8
                    && providerReviewedCallableTableBridge.value("blockedReason").toString()
                        == QStringLiteral("production-provider-reviewed-callable-table-bridge-awaiting-stub")
                    && providerReviewedCallableTableBridge.value("bridges").toArray().size() == 8
                    && !providerReviewedCallableTableBridge.value("operationInvokedByBridge").toBool(true)
                    && !providerReviewedCallableTableBridge.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedCallableTableBridge.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedCallableTableBridge.value("resultCaptured").toBool(true)
                    && !providerReviewedCallableTableBridge.value("rawKeyExported").toBool(true)
                    && !providerReviewedCallableTableBridge.value("privateMaterialExported").toBool(true)
                    && !providerReviewedCallableTableBridge.value("sessionSecretExported").toBool(true),
                "production reviewed callable table bridge should stay blocked until stub evidence exists") && ok;
    ok = expect(providerReviewedOperationCallableInterface.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-operation-callable-interface-v1")
                    && providerReviewedOperationCallableInterface.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-operation-callable-interface-not-release-gate")
                    && !providerReviewedOperationCallableInterface.value("accepted").toBool(true)
                    && providerReviewedOperationCallableInterface.value("interfaceNonReleaseGate").toBool(false)
                    && !providerReviewedOperationCallableInterface.value("bridgeSourceCaptured").toBool(true)
                    && providerReviewedOperationCallableInterface.value("interfaceCount").toInt() == 8
                    && providerReviewedOperationCallableInterface.value("readyInterfaceCount").toInt() == 0
                    && providerReviewedOperationCallableInterface.value("blockedInterfaceCount").toInt() == 8
                    && providerReviewedOperationCallableInterface.value("failClosedInterfaceCount").toInt() == 8
                    && providerReviewedOperationCallableInterface.value("blockedReason").toString()
                        == QStringLiteral("production-provider-reviewed-operation-callable-interface-awaiting-bridge")
                    && providerReviewedOperationCallableInterface.value("interfaces").toArray().size() == 8
                    && !providerReviewedOperationCallableInterface.value("operationInvokedByInterface").toBool(true)
                    && !providerReviewedOperationCallableInterface.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedOperationCallableInterface.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedOperationCallableInterface.value("resultCaptured").toBool(true)
                    && !providerReviewedOperationCallableInterface.value("rawKeyExported").toBool(true)
                    && !providerReviewedOperationCallableInterface.value("privateMaterialExported").toBool(true)
                    && !providerReviewedOperationCallableInterface.value("sessionSecretExported").toBool(true),
                "production reviewed operation callable interface should stay blocked until bridge evidence exists") && ok;
    ok = expect(providerReviewedCallableRuntimePreflight.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-callable-runtime-preflight-v1")
                    && providerReviewedCallableRuntimePreflight.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-callable-runtime-preflight-not-release-gate")
                    && !providerReviewedCallableRuntimePreflight.value("accepted").toBool(true)
                    && providerReviewedCallableRuntimePreflight.value("runtimePreflightNonReleaseGate").toBool(false)
                    && !providerReviewedCallableRuntimePreflight.value("interfaceSourceCaptured").toBool(true)
                    && providerReviewedCallableRuntimePreflight.value("preflightCount").toInt() == 8
                    && providerReviewedCallableRuntimePreflight.value("readyPreflightCount").toInt() == 0
                    && providerReviewedCallableRuntimePreflight.value("blockedPreflightCount").toInt() == 8
                    && providerReviewedCallableRuntimePreflight.value("failClosedPreflightCount").toInt() == 8
                    && providerReviewedCallableRuntimePreflight.value("blockedReason").toString()
                        == QStringLiteral("production-provider-reviewed-callable-runtime-preflight-awaiting-interface")
                    && providerReviewedCallableRuntimePreflight.value("preflights").toArray().size() == 8
                    && !providerReviewedCallableRuntimePreflight.value("operationInvokedByRuntimePreflight").toBool(true)
                    && !providerReviewedCallableRuntimePreflight.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedCallableRuntimePreflight.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedCallableRuntimePreflight.value("resultCaptured").toBool(true)
                    && !providerReviewedCallableRuntimePreflight.value("rawKeyExported").toBool(true)
                    && !providerReviewedCallableRuntimePreflight.value("privateMaterialExported").toBool(true)
                    && !providerReviewedCallableRuntimePreflight.value("sessionSecretExported").toBool(true),
                "production reviewed callable runtime preflight should stay blocked until interface evidence exists") && ok;
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
    const QJsonObject firstProviderDecision =
        providerExecutionDecision.value("decisions").toArray().at(0).toObject();
    ok = expect(firstProviderDecision.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstProviderDecision.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstProviderDecision.value("decisionState").toString()
                        == (adapterLinked
                            ? QStringLiteral("blocked-linked-placeholder")
                            : QStringLiteral("blocked-not-linked"))
                    && !firstProviderDecision.value("reviewedProviderCallbackAllowed").toBool(true)
                    && !firstProviderDecision.value("operationInvoked").toBool(true)
                    && !firstProviderDecision.value("resultCaptured").toBool(true)
                    && firstProviderDecision.value("noSensitiveMaterialExport").toBool(false)
                    && !firstProviderDecision.value("rawKeyExported").toBool(true),
                "production provider execution decision should keep linked placeholders non-callable") && ok;
    const QJsonObject firstProviderCallFrame =
        providerCallFrame.value("frames").toArray().at(0).toObject();
    ok = expect(firstProviderCallFrame.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstProviderCallFrame.value("operationEnumValue").toInt(-1) == 0
                    && firstProviderCallFrame.value("operationEnumMatched").toBool(false)
                    && firstProviderCallFrame.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstProviderCallFrame.value("suiteId").toString()
                        == QStringLiteral("x25519-hkdf-sha256-aes-256-gcm")
                    && firstProviderCallFrame.value("primaryInputClass").toString()
                        == QStringLiteral("empty-random-source-context")
                    && firstProviderCallFrame.value("secondaryInputClass").toString()
                        == QStringLiteral("suite-context")
                    && firstProviderCallFrame.value("outputMaterialPolicy").toString()
                        == QStringLiteral("handle-only-no-private-material-export")
                    && firstProviderCallFrame.value("contractHashed").toBool(false)
                    && !firstProviderCallFrame.value("frameReady").toBool(true)
                    && !firstProviderCallFrame.value("operationInvoked").toBool(true)
                    && !firstProviderCallFrame.value("inputBytesAttached").toBool(true)
                    && !firstProviderCallFrame.value("outputBytesAttached").toBool(true),
                "production provider call frame should expose sanitized per-operation C ABI evidence") && ok;
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
                    && acceptance.value("providerExecutionDecision").toObject()
                        .value("decisions").toArray().size() == 8
                    && acceptance.value("providerCallFrame").toObject()
                        .value("frames").toArray().size() == 8
                    && !acceptance.value("providerCallFrameAccepted").toBool(true)
                    && acceptance.value("providerCallFrameBlockedFrameCount").toInt() == 8
                    && !acceptance.value("providerExecutionDecisionAccepted").toBool(true)
                    && acceptance.value("providerExecutionDecisionBlockedDecisionCount").toInt() == 8
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
