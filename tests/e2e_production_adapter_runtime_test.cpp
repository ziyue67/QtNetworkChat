#include "e2eenvelope.h"
#include "qtnetworkchat_e2e_provider_api.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

#include <cstdio>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning() << message;
        std::fprintf(stderr, "%s\n", message);
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
    const bool reviewedProviderOperationsBound =
        status.value("productionAdapterReason").toString()
        == QStringLiteral("production-adapter-linked-reviewed-operations");
    const int reviewedCandidateCount = reviewedProviderOperationsBound ? 8 : 0;
    const int remainingUnsupportedCount = reviewedProviderOperationsBound ? 0 : 8;
    const int expectedPreflightReadyCount = reviewedProviderOperationsBound ? 8 : 0;
    const int expectedProviderProbeReadyCount = reviewedProviderOperationsBound ? 8 : 0;
    const int expectedProviderProbeBlockedCount = reviewedProviderOperationsBound ? 0 : 8;
    const bool expectedStructuralGateAccepted = reviewedProviderOperationsBound;
    const bool expectedProviderProbeAccepted = reviewedProviderOperationsBound;
    const bool expectedProviderControlGateAccepted = reviewedProviderOperationsBound;
    const int expectedReviewedEvidenceReadyCount = reviewedProviderOperationsBound ? 8 : 0;
    const int expectedReviewedEvidenceBlockedCount = reviewedProviderOperationsBound ? 0 : 8;
    const bool expectedReviewedEvidenceSourceCaptured = reviewedProviderOperationsBound;
    const bool expectedProductionReady = reviewedProviderOperationsBound;
    const bool expectedAvailable = reviewedProviderOperationsBound;
    const QString expectedReason = reviewedProviderOperationsBound
        ? QString()
        : (adapterLinked
        ? QStringLiteral("production-adapter-not-ready")
        : QStringLiteral("production-crypto-backend-unavailable"));
    const QString expectedAction = reviewedProviderOperationsBound
        ? QStringLiteral("none")
        : (adapterLinked
            ? QStringLiteral("complete-production-crypto-adapter-implementation-and-compatibility-tests")
            : QStringLiteral("link-reviewed-production-crypto-backend"));
    const QString expectedDispatchState = reviewedProviderOperationsBound
        ? QStringLiteral("production-dispatch-ready")
        : (adapterLinked ? QStringLiteral("linked-placeholder-not-ready")
                         : QStringLiteral("not-linked"));
    const QString expectedReadinessGate = reviewedProviderOperationsBound
        ? QString()
        : (adapterLinked ? QStringLiteral("production-operations-not-implemented")
                         : QStringLiteral("production-adapter-not-linked"));
    const QString expectedSelfTestStatus = reviewedProviderOperationsBound
        ? QStringLiteral("production-self-test-passed")
        : (adapterLinked ? QStringLiteral("self-test-blocked-placeholder")
                         : QStringLiteral("self-test-blocked-not-linked"));
    const QString expectedCompatibilityStatus = reviewedProviderOperationsBound
        ? QStringLiteral("production-compatibility-passed")
        : (adapterLinked ? QStringLiteral("compatibility-blocked-placeholder")
                         : QStringLiteral("compatibility-blocked-not-linked"));
    const QString expectedCompatibilityGate = reviewedProviderOperationsBound
        ? QString()
        : (adapterLinked ? QStringLiteral("production-operation-vectors-not-implemented")
                         : QStringLiteral("production-adapter-not-linked"));

    ok = expect(status.value("requestedBackendId").toString() == QStringLiteral("openssl-reviewed-adapter-v1")
                    && status.value("selectionSource").toString() == QStringLiteral("environment")
                    && status.value("available").toBool(!expectedAvailable) == expectedAvailable
                    && status.value("selectedBackendId").toString()
                        == (expectedAvailable
                            ? QStringLiteral("openssl-reviewed-adapter-v1")
                            : QString())
                    && status.value("productionReady").toBool(!expectedProductionReady)
                        == expectedProductionReady
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
                    && status.value("selectedProviderCompatibility").toObject()
                        .value("knownAnswerPassed").toBool(!reviewedProviderOperationsBound)
                            == reviewedProviderOperationsBound
                    && status.value("selectedProviderCompatibility").toObject()
                        .value("roundTripPassed").toBool(!reviewedProviderOperationsBound)
                            == reviewedProviderOperationsBound
                    && status.value("selectedProviderCompatibility").toObject()
                        .value("requiredOperations").toArray().size() == 8
                    && status.value("selectedProviderCompatibility").toObject()
                        .value("implementedOperationCount").toInt()
                            == reviewedCandidateCount
                    && status.value("selectedProviderCompatibility").toObject()
                        .value("blockedOperationCount").toInt()
                            == remainingUnsupportedCount
                    && status.value("selectedProviderCompatibility").toObject()
                        .value("reviewedOperationBound").toBool(!reviewedProviderOperationsBound)
                            == reviewedProviderOperationsBound
                    && status.value("unavailableReason").toString() == expectedReason,
                "production adapter runtime status should expose precise production availability") && ok;

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
    const QJsonObject providerReviewedInvocationArming =
        status.value("productionProviderReviewedInvocationArming").toObject();
    const QJsonObject providerReviewedInvocationExecutionAcceptance =
        status.value("productionProviderReviewedInvocationExecutionAcceptance").toObject();
    const QJsonObject providerDataPlaneBridge =
        status.value("productionProviderDataPlaneBridge").toObject();
    const QJsonObject providerPublicPrimitiveExecution =
        status.value("productionProviderPublicPrimitiveExecution").toObject();
    const QJsonObject rolloutObservability =
        status.value("productionRolloutObservability").toObject();
    ok = expect(harness.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-harness-v1")
                    && harness.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-operation-harness-passed")
                            : (adapterLinked
                                ? QStringLiteral("production-operation-harness-blocked-placeholder")
                                : QStringLiteral("production-operation-harness-blocked-not-linked")))
                    && harness.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : (adapterLinked
                                ? QStringLiteral("production-operations-not-implemented")
                                : QStringLiteral("production-crypto-backend-unavailable")))
                    && harness.value("accepted").toBool(!expectedProviderControlGateAccepted)
                        == expectedProviderControlGateAccepted
                    && harness.value("harnessRunnable").toBool(!expectedProviderControlGateAccepted)
                        == expectedProviderControlGateAccepted
                    && harness.value("runnableOperationCount").toInt()
                        == expectedProviderProbeReadyCount
                    && harness.value("blockedOperationCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && harness.value("operations").toArray().size() == 8
                    && !harness.value("rawKeyExported").toBool(true)
                    && !harness.value("privateMaterialExported").toBool(true),
                "production operation harness should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(executionPlan.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-execution-plan-v1")
                    && executionPlan.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-operation-execution-plan-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-operation-execution-plan-blocked-placeholder")
                                : QStringLiteral("production-operation-execution-plan-blocked-not-linked")))
                    && executionPlan.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : (adapterLinked
                                ? QStringLiteral("production-operations-not-implemented")
                                : QStringLiteral("production-crypto-backend-unavailable")))
                    && executionPlan.value("operationHarnessReleaseGate").toString()
                        == harness.value("releaseGate").toString()
                    && executionPlan.value("accepted").toBool(!expectedProviderControlGateAccepted)
                        == expectedProviderControlGateAccepted
                    && executionPlan.value("planReady").toBool(!expectedProviderControlGateAccepted)
                        == expectedProviderControlGateAccepted
                    && executionPlan.value("runnableStepCount").toInt()
                        == expectedProviderProbeReadyCount
                    && executionPlan.value("blockedStepCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && executionPlan.value("steps").toArray().size() == 8
                    && !executionPlan.value("rawKeyExported").toBool(true)
                    && !executionPlan.value("privateMaterialExported").toBool(true),
                "production execution plan should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(invocation.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-invocation-v1")
                    && invocation.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-operation-invocation-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-operation-invocation-blocked-placeholder")
                                : QStringLiteral("production-operation-invocation-blocked-not-linked")))
                    && invocation.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : (adapterLinked
                                ? QStringLiteral("production-operations-not-implemented")
                                : QStringLiteral("production-crypto-backend-unavailable")))
                    && invocation.value("executionPlanReleaseGate").toString()
                        == executionPlan.value("releaseGate").toString()
                    && invocation.value("accepted").toBool(!expectedProviderControlGateAccepted)
                        == expectedProviderControlGateAccepted
                    && invocation.value("callableOperationCount").toInt()
                        == expectedProviderProbeReadyCount
                    && invocation.value("blockedOperationCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && invocation.value("invocations").toArray().size() == 8
                    && !invocation.value("rawKeyExported").toBool(true)
                    && !invocation.value("privateMaterialExported").toBool(true),
                "production invocation status should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(slotStatus.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-slots-v1")
                    && slotStatus.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-operation-slots-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-operation-slots-blocked-placeholder")
                                : QStringLiteral("production-operation-slots-blocked-not-linked")))
                    && slotStatus.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : (adapterLinked
                                ? QStringLiteral("production-operation-slots-not-reviewed")
                                : QStringLiteral("production-crypto-backend-unavailable")))
                    && slotStatus.value("accepted").toBool(!expectedProviderControlGateAccepted)
                        == expectedProviderControlGateAccepted
                    && slotStatus.value("reviewedSlotCount").toInt()
                        == reviewedCandidateCount
                    && slotStatus.value("callableSlotCount").toInt()
                        == expectedProviderProbeReadyCount
                    && slotStatus.value("blockedSlotCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && slotStatus.value("slots").toArray().size() == 8
                    && !slotStatus.value("rawKeyExported").toBool(true)
                    && !slotStatus.value("privateMaterialExported").toBool(true),
                "production slot registry should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(dispatchBindings.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-dispatch-bindings-v1")
                    && dispatchBindings.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-operation-dispatch-bindings-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-operation-dispatch-bindings-blocked-placeholder")
                                : QStringLiteral("production-operation-dispatch-bindings-blocked-not-linked")))
                    && dispatchBindings.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : (adapterLinked
                                ? QStringLiteral("production-operation-dispatch-bindings-not-reviewed")
                                : QStringLiteral("production-crypto-backend-unavailable")))
                    && dispatchBindings.value("accepted").toBool(!expectedProviderControlGateAccepted)
                        == expectedProviderControlGateAccepted
                    && dispatchBindings.value("reviewedBindingCount").toInt()
                        == reviewedCandidateCount
                    && dispatchBindings.value("callableBindingCount").toInt()
                        == expectedProviderProbeReadyCount
                    && dispatchBindings.value("blockedBindingCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && dispatchBindings.value("bindings").toArray().size() == 8
                    && dispatchBindings.value("operationSlotsReleaseGate").toString()
                        == slotStatus.value("releaseGate").toString()
                    && !dispatchBindings.value("rawKeyExported").toBool(true)
                    && !dispatchBindings.value("privateMaterialExported").toBool(true),
                "production dispatch bindings should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(callableManifest.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-callable-manifest-v1")
                    && callableManifest.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-operation-callable-manifest-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-operation-callable-manifest-blocked-placeholder")
                                : QStringLiteral("production-operation-callable-manifest-blocked-not-linked")))
                    && callableManifest.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : (adapterLinked
                                ? QStringLiteral("production-callable-manifest-not-reviewed")
                                : QStringLiteral("production-crypto-backend-unavailable")))
                    && callableManifest.value("accepted").toBool(!expectedProviderControlGateAccepted)
                        == expectedProviderControlGateAccepted
                    && callableManifest.value("reviewedCallableCount").toInt()
                        == expectedProviderProbeReadyCount
                    && callableManifest.value("blockedCallableCount").toInt()
                        == expectedProviderProbeBlockedCount
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
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-operation-execution-results-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-operation-execution-results-blocked-placeholder")
                                : QStringLiteral("production-operation-execution-results-blocked-not-linked")))
                    && executionResult.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : (adapterLinked
                                ? QStringLiteral("production-operation-results-not-executed")
                                : QStringLiteral("production-crypto-backend-unavailable")))
                    && executionResult.value("accepted").toBool(!expectedProviderProbeAccepted)
                        == expectedProviderProbeAccepted
                    && executionResult.value("passedResultCount").toInt()
                        == expectedProviderProbeReadyCount
                    && executionResult.value("blockedResultCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && executionResult.value("sanitizedResultCount").toInt() == 8
                    && executionResult.value("outputContractMismatchCount").toInt() == 0
                    && executionResult.value("invokedOperationCount").toInt()
                        == expectedProviderProbeReadyCount
                    && executionResult.value("capturedResultCount").toInt()
                        == expectedProviderProbeReadyCount
                    && executionResult.value("providerProbeVectorMatchedCount").toInt()
                        == expectedProviderProbeReadyCount
                    && executionResult.value("results").toArray().size() == 8
                    && executionResult.value("operationCallableManifestReleaseGate").toString()
                        == callableManifest.value("releaseGate").toString()
                    && executionResult.value("providerInvocationResultReleaseGate").toString()
                        == providerInvocationResult.value("releaseGate").toString()
                    && executionResult.value("providerInvocationResultAccepted")
                        .toBool(!expectedProviderProbeAccepted) == expectedProviderProbeAccepted
                    && executionResult.value("operationInvoked")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && executionResult.value("resultCaptured")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && !executionResult.value("rawKeyExported").toBool(true)
                    && !executionResult.value("privateMaterialExported").toBool(true)
                    && !executionResult.value("sessionSecretExported").toBool(true)
                    && !executionResult.value("privateIdentityMaterialExported").toBool(true),
                "production execution result status should accept reviewed probes without opening production mode") && ok;
    ok = expect(providerTable.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-table-v1")
                    && providerTable.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-table-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-table-blocked-placeholder")
                                : QStringLiteral("production-provider-table-blocked-not-linked")))
                    && providerTable.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : QStringLiteral("production-provider-table-not-bound"))
                    && providerTable.value("buildProbeReason").toString() == (adapterLinked
                        ? (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-table-reviewed-operations-bound")
                            : QStringLiteral("production-provider-table-linked-placeholder"))
                        : QStringLiteral("production-provider-table-not-requested"))
                    && providerTable.value("providerApiHeader").toString()
                        == QStringLiteral("include/qtnetworkchat_e2e_provider_api.h")
                    && providerTable.value("headerTableAbi").toString()
                        == QString::fromLatin1(QNC_E2E_PROVIDER_TABLE_ABI)
                    && providerTable.value("abiMatchesHeader").toBool(false)
                    && providerTable.value("headerOperationCount").toInt()
                        == QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT
                    && providerTable.value("operationCountMatchesHeader").toBool(false)
                    && providerTable.value("accepted").toBool(!expectedStructuralGateAccepted)
                        == expectedStructuralGateAccepted
                    && providerTable.value("tableBound").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && providerTable.value("requiredSymbolCount").toInt() == 8
                    && providerTable.value("boundSymbolCount").toInt()
                        == expectedPreflightReadyCount
                    && providerTable.value("missingSymbolCount").toInt()
                        == (8 - expectedPreflightReadyCount)
                    && providerTable.value("providerTableRegistered").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && providerTable.value("registrationAccepted").toBool(!expectedStructuralGateAccepted)
                        == expectedStructuralGateAccepted
                    && providerTable.value("registrationReleaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-table-registration-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-table-registration-blocked-placeholder")
                                : QStringLiteral("production-provider-table-registration-blocked-not-linked")))
                    && providerTable.value("callableManifestReleaseGate").toString()
                        == callableManifest.value("releaseGate").toString()
                    && !providerTable.value("rawKeyExported").toBool(true)
                    && !providerTable.value("privateMaterialExported").toBool(true),
                "production provider table should distinguish not-linked from linked-placeholder gates") && ok;
    ok = expect(providerTableBindingProbe.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-table-binding-probe-v1")
                    && providerTableBindingProbe.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-table-binding-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-table-binding-blocked-placeholder")
                                : QStringLiteral("production-provider-table-binding-blocked-not-linked")))
                    && providerTableBindingProbe.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : (adapterLinked
                                ? QStringLiteral("production-provider-table-placeholder")
                                : QStringLiteral("production-provider-table-not-bound")))
                    && providerTableBindingProbe.value("headerLayoutComplete").toBool(false)
                    && providerTableBindingProbe.value("enumMappingComplete").toBool(false)
                    && providerTableBindingProbe.value("functionPointerSlotsComplete").toBool(false)
                    && providerTableBindingProbe.value("tableValidation").toObject()
                        .value("blockedReason").toString()
                            == (reviewedProviderOperationsBound
                                ? QString()
                                : QStringLiteral("production-provider-table-not-bound"))
                    && providerTableBindingProbe.value("tableValidationAccepted").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && providerTableBindingProbe.value("accepted").toBool(!expectedStructuralGateAccepted)
                        == expectedStructuralGateAccepted
                    && providerTableBindingProbe.value("tableBound").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && providerTableBindingProbe.value("providerTableRegistered").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && providerTableBindingProbe.value("registrationAccepted").toBool(!expectedStructuralGateAccepted)
                        == expectedStructuralGateAccepted
                    && providerTableBindingProbe.value("registrationReleaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-table-registration-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-table-registration-blocked-placeholder")
                                : QStringLiteral("production-provider-table-registration-blocked-not-linked")))
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
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-table-registration-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-table-registration-blocked-placeholder")
                                : QStringLiteral("production-provider-table-registration-blocked-not-linked")))
                    && providerTableRegistration.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : QStringLiteral("production-provider-table-not-registered"))
                    && providerTableRegistration.value("registered").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && providerTableRegistration.value("accepted").toBool(!expectedStructuralGateAccepted)
                        == expectedStructuralGateAccepted
                    && providerTableRegistration.value("registrationSource").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("linked-reviewed-operations-provider-table")
                            : (adapterLinked
                                ? QStringLiteral("linked-placeholder-without-runtime-table")
                                : QStringLiteral("not-linked")))
                    && providerTableRegistration.value("tableValidation").toObject()
                        .value("blockedReason").toString()
                            == (reviewedProviderOperationsBound
                                ? QString()
                                : QStringLiteral("production-provider-table-not-bound"))
                    && providerTableRegistration.value("tableValidationAccepted").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && !providerTableRegistration.value("rawKeyExported").toBool(true)
                    && !providerTableRegistration.value("privateMaterialExported").toBool(true),
                "production provider table registration should expose runtime binding evidence without enabling placeholders") && ok;
    ok = expect(providerOperationPreflight.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-operation-preflight-v1")
                    && providerOperationPreflight.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-operation-preflight-ready")
                            : (adapterLinked
                            ? QStringLiteral("production-provider-operation-preflight-blocked-placeholder")
                            : QStringLiteral("production-provider-operation-preflight-blocked-not-linked")))
                    && providerOperationPreflight.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : QStringLiteral("production-provider-table-not-registered"))
                    && providerOperationPreflight.value("accepted").toBool(!expectedStructuralGateAccepted)
                        == expectedStructuralGateAccepted
                    && providerOperationPreflight.value("providerTableRegistered").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && providerOperationPreflight.value("presentOperationCount").toInt()
                        == (reviewedProviderOperationsBound ? 8 : 0)
                    && providerOperationPreflight.value("blockedOperationCount").toInt()
                        == (8 - expectedPreflightReadyCount)
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
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-call-frame-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-call-frame-blocked-placeholder")
                                : QStringLiteral("production-provider-call-frame-blocked-not-linked")))
                    && providerCallFrame.value("accepted").toBool(!expectedStructuralGateAccepted)
                        == expectedStructuralGateAccepted
                    && providerCallFrame.value("readyFrameCount").toInt()
                        == expectedPreflightReadyCount
                    && providerCallFrame.value("blockedFrameCount").toInt()
                        == (8 - expectedPreflightReadyCount)
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
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-invocation-dry-run-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-invocation-dry-run-blocked-placeholder")
                                : QStringLiteral("production-provider-invocation-dry-run-blocked-not-linked")))
                    && providerInvocationDryRun.value("accepted").toBool(!expectedStructuralGateAccepted)
                        == expectedStructuralGateAccepted
                    && providerInvocationDryRun.value("dryRunReadyCount").toInt()
                        == expectedPreflightReadyCount
                    && providerInvocationDryRun.value("blockedInvocationCount").toInt()
                        == (8 - expectedPreflightReadyCount)
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
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-invocation-results-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-invocation-results-blocked-placeholder")
                                : QStringLiteral("production-provider-invocation-results-blocked-not-linked")))
                    && providerInvocationResult.value("accepted").toBool(!expectedProviderProbeAccepted)
                        == expectedProviderProbeAccepted
                    && providerInvocationResult.value("captureReadyCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerInvocationResult.value("blockedResultCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && providerInvocationResult.value("sanitizedResultCount").toInt() == 8
                    && providerInvocationResult.value("outputContractProofCount").toInt() == 8
                    && providerInvocationResult.value("fixtureProofCount").toInt() == 8
                    && providerInvocationResult.value("materialExportProofCount").toInt() == 8
                    && providerInvocationResult.value("providerProbeInvokedOperationCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerInvocationResult.value("providerProbeVectorPassCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerInvocationResult.value("results").toArray().size() == 8
                    && providerInvocationResult.value("providerInvocationDryRunReleaseGate").toString()
                        == providerInvocationDryRun.value("releaseGate").toString()
                    && providerInvocationResult.value("operationInvoked")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && providerInvocationResult.value("resultCaptured")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && !providerInvocationResult.value("rawKeyExported").toBool(true)
                    && !providerInvocationResult.value("privateMaterialExported").toBool(true),
                "production provider invocation result capture should record reviewed probe output safely") && ok;
    ok = expect(providerExecutionDecision.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-execution-decision-v1")
                    && providerExecutionDecision.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-execution-decision-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-execution-decision-blocked-placeholder")
                                : QStringLiteral("production-provider-execution-decision-blocked-not-linked")))
                    && providerExecutionDecision.value("accepted")
                        .toBool(!expectedProviderControlGateAccepted)
                            == expectedProviderControlGateAccepted
                    && providerExecutionDecision.value("allowedDecisionCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerExecutionDecision.value("blockedDecisionCount").toInt()
                        == expectedProviderProbeBlockedCount
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
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-callback-harness-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-callback-harness-blocked-placeholder")
                                : QStringLiteral("production-provider-callback-harness-blocked-not-linked")))
                    && providerCallbackHarness.value("accepted")
                        .toBool(!expectedProviderControlGateAccepted)
                            == expectedProviderControlGateAccepted
                    && providerCallbackHarness.value("armedCallbackCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerCallbackHarness.value("blockedCallbackCount").toInt()
                        == expectedProviderProbeBlockedCount
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
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-vector-self-test-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-vector-self-test-blocked-placeholder")
                                : QStringLiteral("production-provider-vector-self-test-blocked-not-linked")))
                    && providerVectorSelfTest.value("accepted").toBool(!expectedProviderProbeAccepted)
                        == expectedProviderProbeAccepted
                    && providerVectorSelfTest.value("passedVectorCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerVectorSelfTest.value("blockedVectorCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && providerVectorSelfTest.value("knownAnswerReadyCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerVectorSelfTest.value("sanitizedVectorCount").toInt() == 8
                    && providerVectorSelfTest.value("materialExportProofCount").toInt() == 8
                    && providerVectorSelfTest.value("providerProbeInvokedOperationCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerVectorSelfTest.value("providerProbeVectorPassCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerVectorSelfTest.value("tests").toArray().size() == 8
                    && providerVectorSelfTest.value("providerCallbackHarnessReleaseGate").toString()
                        == providerCallbackHarness.value("releaseGate").toString()
                    && providerVectorSelfTest.value("operationInvoked")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && !providerVectorSelfTest.value("inputBytesCaptured").toBool(true)
                    && !providerVectorSelfTest.value("outputBytesCaptured").toBool(true)
                    && providerVectorSelfTest.value("resultCaptured")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && !providerVectorSelfTest.value("rawKeyExported").toBool(true)
                    && !providerVectorSelfTest.value("privateMaterialExported").toBool(true),
                "production provider vector self-test should accept reviewed vectors without exporting material") && ok;
    ok = expect(providerExecutionSlotBinding.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-execution-slot-binding-v1")
                    && providerExecutionSlotBinding.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-execution-slot-binding-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-execution-slot-binding-blocked-placeholder")
                                : QStringLiteral("production-provider-execution-slot-binding-blocked-not-linked")))
                    && providerExecutionSlotBinding.value("accepted")
                        .toBool(!expectedProviderControlGateAccepted)
                            == expectedProviderControlGateAccepted
                    && providerExecutionSlotBinding.value("bindableSlotCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerExecutionSlotBinding.value("blockedSlotCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && providerExecutionSlotBinding.value("reviewedSlotCount").toInt()
                        == expectedProviderProbeReadyCount
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
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-execution-path-ready")
                            : (adapterLinked
                            ? QStringLiteral("production-provider-execution-path-blocked-placeholder")
                            : QStringLiteral("production-provider-execution-path-blocked-not-linked")))
                    && providerExecutionPath.value("accepted")
                        .toBool(!expectedProviderControlGateAccepted)
                            == expectedProviderControlGateAccepted
                    && providerExecutionPath.value("mappedPathCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerExecutionPath.value("blockedPathCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && providerExecutionPath.value("pointerPresentCount").toInt()
                        == (reviewedProviderOperationsBound ? 8 : 0)
                    && providerExecutionPath.value("bindableSlotCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerExecutionPath.value("capturePolicyCount").toInt() == 8
                    && providerExecutionPath.value("sanitizedPathCount").toInt() == 8
                    && providerExecutionPath.value("paths").toArray().size() == 8
                    && providerExecutionPath.value("providerExecutionSlotBindingReleaseGate").toString()
                        == providerExecutionSlotBinding.value("releaseGate").toString()
                    && providerExecutionPath.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : QStringLiteral("production-provider-table-not-registered"))
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
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-invocation-sandbox-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-invocation-sandbox-blocked-placeholder")
                                : QStringLiteral("production-provider-invocation-sandbox-blocked-not-linked")))
                    && providerInvocationSandbox.value("accepted")
                        .toBool(!expectedProviderControlGateAccepted)
                            == expectedProviderControlGateAccepted
                    && providerInvocationSandbox.value("readySandboxCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerInvocationSandbox.value("blockedSandboxCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && providerInvocationSandbox.value("mappedPathCount").toInt()
                        == expectedProviderProbeReadyCount
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
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-invocation-vector-result-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-invocation-vector-result-blocked-placeholder")
                                : QStringLiteral("production-provider-invocation-vector-result-blocked-not-linked")))
                    && providerInvocationVectorResult.value("accepted")
                        .toBool(!expectedProviderProbeAccepted) == expectedProviderProbeAccepted
                    && providerInvocationVectorResult.value("readyVectorResultCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerInvocationVectorResult.value("blockedVectorResultCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && providerInvocationVectorResult.value("sandboxReadyCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerInvocationVectorResult.value("fixtureMatchedCount").toInt() == 8
                    && providerInvocationVectorResult.value("resultContractCount").toInt() == 8
                    && providerInvocationVectorResult.value("sanitizedResultCount").toInt() == 8
                    && providerInvocationVectorResult.value("materialExportProofCount").toInt() == 8
                    && providerInvocationVectorResult.value("providerProbeInvokedOperationCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerInvocationVectorResult.value("providerProbeVectorPassCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerInvocationVectorResult.value("vectorResults").toArray().size() == 8
                    && providerInvocationVectorResult.value("providerInvocationSandboxReleaseGate").toString()
                        == providerInvocationSandbox.value("releaseGate").toString()
                    && providerInvocationVectorResult.value("operationInvoked")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && !providerInvocationVectorResult.value("inputBytesCaptured").toBool(true)
                    && !providerInvocationVectorResult.value("outputBytesCaptured").toBool(true)
                    && providerInvocationVectorResult.value("resultCaptured")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && !providerInvocationVectorResult.value("rawKeyExported").toBool(true)
                    && !providerInvocationVectorResult.value("privateMaterialExported").toBool(true),
                "production provider invocation vector result should record sanitized reviewed vectors") && ok;
    ok = expect(providerInvocationExecution.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-execution-v1")
                    && providerInvocationExecution.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-invocation-execution-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-provider-invocation-execution-blocked-placeholder")
                                : QStringLiteral("production-provider-invocation-execution-blocked-not-linked")))
                    && providerInvocationExecution.value("accepted")
                        .toBool(!expectedProviderProbeAccepted) == expectedProviderProbeAccepted
                    && providerInvocationExecution.value("readyExecutionCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerInvocationExecution.value("blockedExecutionCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && providerInvocationExecution.value("vectorResultReadyCount").toInt()
                        == expectedProviderProbeReadyCount
                    && providerInvocationExecution.value("callableEntryPointCount").toInt() == 8
                    && providerInvocationExecution.value("sanitizedExecutionCount").toInt() == 8
                    && providerInvocationExecution.value("resultCapturePolicyCount").toInt() == 8
                    && providerInvocationExecution.value("materialExportProofCount").toInt() == 8
                    && providerInvocationExecution.value("executions").toArray().size() == 8
                    && providerInvocationExecution.value("providerInvocationVectorResultReleaseGate").toString()
                        == providerInvocationVectorResult.value("releaseGate").toString()
                    && providerInvocationExecution.value("operationInvoked")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && !providerInvocationExecution.value("inputBytesCaptured").toBool(true)
                    && !providerInvocationExecution.value("outputBytesCaptured").toBool(true)
                    && providerInvocationExecution.value("resultCaptured")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && !providerInvocationExecution.value("rawKeyExported").toBool(true)
                    && !providerInvocationExecution.value("privateMaterialExported").toBool(true),
                "production provider invocation execution should record reviewed probe handoff evidence") && ok;
    ok = expect(providerReviewedExecutionCandidate.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-execution-candidate-v1")
                    && providerReviewedExecutionCandidate.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-execution-candidate-not-release-gate")
                    && !providerReviewedExecutionCandidate.value("accepted").toBool(true)
                    && providerReviewedExecutionCandidate.value("candidateNonReleaseGate").toBool(false)
                    && providerReviewedExecutionCandidate.value("probeSourceCaptured")
                        .toBool(!expectedReviewedEvidenceSourceCaptured)
                            == expectedReviewedEvidenceSourceCaptured
                    && providerReviewedExecutionCandidate.value("candidateCount").toInt() == 8
                    && providerReviewedExecutionCandidate.value("candidateReadyCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && providerReviewedExecutionCandidate.value("blockedCandidateCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedExecutionCandidate.value("probeMatrixMatchedCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && providerReviewedExecutionCandidate.value("candidateSanitizedCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && providerReviewedExecutionCandidate.value("candidateEntrypointCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && providerReviewedExecutionCandidate.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-reviewed-candidates-awaiting-audit-release-gate")
                            : QStringLiteral("production-provider-reviewed-candidate-awaiting-explicit-probe"))
                    && providerReviewedExecutionCandidate.value("candidates").toArray().size() == 8
                    && !providerReviewedExecutionCandidate.value("operationInvokedByCandidate").toBool(true)
                    && !providerReviewedExecutionCandidate.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedExecutionCandidate.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedExecutionCandidate.value("rawKeyExported").toBool(true)
                    && !providerReviewedExecutionCandidate.value("privateMaterialExported").toBool(true)
                    && !providerReviewedExecutionCandidate.value("sessionSecretExported").toBool(true),
                "production reviewed execution candidate status should reflect sanitized provider probe evidence") && ok;
    ok = expect(providerReviewedCallHandoff.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-call-handoff-v1")
                    && providerReviewedCallHandoff.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-call-handoff-not-release-gate")
                    && !providerReviewedCallHandoff.value("accepted").toBool(true)
                    && providerReviewedCallHandoff.value("handoffNonReleaseGate").toBool(false)
                    && providerReviewedCallHandoff.value("candidateSourceCaptured")
                        .toBool(!expectedReviewedEvidenceSourceCaptured)
                            == expectedReviewedEvidenceSourceCaptured
                    && providerReviewedCallHandoff.value("handoffCount").toInt() == 8
                    && providerReviewedCallHandoff.value("readyHandoffCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && providerReviewedCallHandoff.value("blockedHandoffCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedCallHandoff.value("failClosedHandoffCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedCallHandoff.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-reviewed-call-handoffs-awaiting-audit-release-gate")
                            : QStringLiteral("production-provider-reviewed-call-handoff-awaiting-candidate"))
                    && providerReviewedCallHandoff.value("handoffs").toArray().size() == 8
                    && !providerReviewedCallHandoff.value("operationInvokedByHandoff").toBool(true)
                    && !providerReviewedCallHandoff.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedCallHandoff.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedCallHandoff.value("resultCaptured").toBool(true)
                    && !providerReviewedCallHandoff.value("rawKeyExported").toBool(true)
                    && !providerReviewedCallHandoff.value("privateMaterialExported").toBool(true)
                    && !providerReviewedCallHandoff.value("sessionSecretExported").toBool(true),
                "production reviewed call handoff status should follow sanitized candidate evidence") && ok;
    ok = expect(providerReviewedOperationStubBoundary.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-operation-stub-boundary-v1")
                    && providerReviewedOperationStubBoundary.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-operation-stub-not-release-gate")
                    && !providerReviewedOperationStubBoundary.value("accepted").toBool(true)
                    && providerReviewedOperationStubBoundary.value("stubNonReleaseGate").toBool(false)
                    && providerReviewedOperationStubBoundary.value("handoffSourceCaptured")
                        .toBool(!expectedReviewedEvidenceSourceCaptured)
                            == expectedReviewedEvidenceSourceCaptured
                    && providerReviewedOperationStubBoundary.value("stubCount").toInt() == 8
                    && providerReviewedOperationStubBoundary.value("readyStubCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && providerReviewedOperationStubBoundary.value("blockedStubCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedOperationStubBoundary.value("failClosedStubCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedOperationStubBoundary.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-reviewed-operation-stubs-awaiting-audit-release-gate")
                            : QStringLiteral("production-provider-reviewed-operation-stub-awaiting-handoff"))
                    && providerReviewedOperationStubBoundary.value("stubs").toArray().size() == 8
                    && !providerReviewedOperationStubBoundary.value("operationInvokedByStub").toBool(true)
                    && !providerReviewedOperationStubBoundary.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedOperationStubBoundary.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedOperationStubBoundary.value("resultCaptured").toBool(true)
                    && !providerReviewedOperationStubBoundary.value("rawKeyExported").toBool(true)
                    && !providerReviewedOperationStubBoundary.value("privateMaterialExported").toBool(true)
                    && !providerReviewedOperationStubBoundary.value("sessionSecretExported").toBool(true),
                "production reviewed operation stub boundary should follow handoff evidence") && ok;
    ok = expect(providerReviewedCallableTableBridge.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-callable-table-bridge-v1")
                    && providerReviewedCallableTableBridge.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-callable-table-bridge-not-release-gate")
                    && !providerReviewedCallableTableBridge.value("accepted").toBool(true)
                    && providerReviewedCallableTableBridge.value("bridgeNonReleaseGate").toBool(false)
                    && providerReviewedCallableTableBridge.value("stubSourceCaptured")
                        .toBool(!expectedReviewedEvidenceSourceCaptured)
                            == expectedReviewedEvidenceSourceCaptured
                    && providerReviewedCallableTableBridge.value("bridgeCount").toInt() == 8
                    && providerReviewedCallableTableBridge.value("readyBridgeCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && providerReviewedCallableTableBridge.value("blockedBridgeCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedCallableTableBridge.value("failClosedBridgeCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedCallableTableBridge.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-reviewed-callable-table-bridges-awaiting-audit-release-gate")
                            : QStringLiteral("production-provider-reviewed-callable-table-bridge-awaiting-stub"))
                    && providerReviewedCallableTableBridge.value("bridges").toArray().size() == 8
                    && !providerReviewedCallableTableBridge.value("operationInvokedByBridge").toBool(true)
                    && !providerReviewedCallableTableBridge.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedCallableTableBridge.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedCallableTableBridge.value("resultCaptured").toBool(true)
                    && !providerReviewedCallableTableBridge.value("rawKeyExported").toBool(true)
                    && !providerReviewedCallableTableBridge.value("privateMaterialExported").toBool(true)
                    && !providerReviewedCallableTableBridge.value("sessionSecretExported").toBool(true),
                "production reviewed callable table bridge should follow stub evidence") && ok;
    ok = expect(providerReviewedOperationCallableInterface.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-operation-callable-interface-v1")
                    && providerReviewedOperationCallableInterface.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-operation-callable-interface-not-release-gate")
                    && !providerReviewedOperationCallableInterface.value("accepted").toBool(true)
                    && providerReviewedOperationCallableInterface.value("interfaceNonReleaseGate").toBool(false)
                    && providerReviewedOperationCallableInterface.value("bridgeSourceCaptured")
                        .toBool(!expectedReviewedEvidenceSourceCaptured)
                            == expectedReviewedEvidenceSourceCaptured
                    && providerReviewedOperationCallableInterface.value("interfaceCount").toInt() == 8
                    && providerReviewedOperationCallableInterface.value("readyInterfaceCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && providerReviewedOperationCallableInterface.value("blockedInterfaceCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedOperationCallableInterface.value("failClosedInterfaceCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedOperationCallableInterface.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-reviewed-operation-callable-interfaces-awaiting-audit-release-gate")
                            : QStringLiteral("production-provider-reviewed-operation-callable-interface-awaiting-bridge"))
                    && providerReviewedOperationCallableInterface.value("interfaces").toArray().size() == 8
                    && !providerReviewedOperationCallableInterface.value("operationInvokedByInterface").toBool(true)
                    && !providerReviewedOperationCallableInterface.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedOperationCallableInterface.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedOperationCallableInterface.value("resultCaptured").toBool(true)
                    && !providerReviewedOperationCallableInterface.value("rawKeyExported").toBool(true)
                    && !providerReviewedOperationCallableInterface.value("privateMaterialExported").toBool(true)
                    && !providerReviewedOperationCallableInterface.value("sessionSecretExported").toBool(true),
                "production reviewed operation callable interface should follow table bridge evidence") && ok;
    ok = expect(providerReviewedCallableRuntimePreflight.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-callable-runtime-preflight-v1")
                    && providerReviewedCallableRuntimePreflight.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-callable-runtime-preflight-not-release-gate")
                    && !providerReviewedCallableRuntimePreflight.value("accepted").toBool(true)
                    && providerReviewedCallableRuntimePreflight.value("runtimePreflightNonReleaseGate").toBool(false)
                    && providerReviewedCallableRuntimePreflight.value("interfaceSourceCaptured")
                        .toBool(!expectedReviewedEvidenceSourceCaptured)
                            == expectedReviewedEvidenceSourceCaptured
                    && providerReviewedCallableRuntimePreflight.value("preflightCount").toInt() == 8
                    && providerReviewedCallableRuntimePreflight.value("readyPreflightCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && providerReviewedCallableRuntimePreflight.value("blockedPreflightCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedCallableRuntimePreflight.value("failClosedPreflightCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedCallableRuntimePreflight.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-reviewed-callable-runtime-preflights-awaiting-audit-release-gate")
                            : QStringLiteral("production-provider-reviewed-callable-runtime-preflight-awaiting-interface"))
                    && providerReviewedCallableRuntimePreflight.value("preflights").toArray().size() == 8
                    && !providerReviewedCallableRuntimePreflight.value("operationInvokedByRuntimePreflight").toBool(true)
                    && !providerReviewedCallableRuntimePreflight.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedCallableRuntimePreflight.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedCallableRuntimePreflight.value("resultCaptured").toBool(true)
                    && !providerReviewedCallableRuntimePreflight.value("rawKeyExported").toBool(true)
                    && !providerReviewedCallableRuntimePreflight.value("privateMaterialExported").toBool(true)
                    && !providerReviewedCallableRuntimePreflight.value("sessionSecretExported").toBool(true),
                "production reviewed callable runtime preflight should follow callable interface evidence") && ok;
    ok = expect(providerReviewedInvocationArming.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-invocation-arming-v1")
                    && providerReviewedInvocationArming.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-invocation-arming-not-release-gate")
                    && !providerReviewedInvocationArming.value("accepted").toBool(true)
                    && providerReviewedInvocationArming.value("invocationArmingNonReleaseGate").toBool(false)
                    && providerReviewedInvocationArming.value("runtimePreflightSourceCaptured")
                        .toBool(!expectedReviewedEvidenceSourceCaptured)
                            == expectedReviewedEvidenceSourceCaptured
                    && providerReviewedInvocationArming.value("armingCount").toInt() == 8
                    && providerReviewedInvocationArming.value("readyArmingCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && providerReviewedInvocationArming.value("blockedArmingCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedInvocationArming.value("failClosedArmingCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedInvocationArming.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-reviewed-invocation-armings-awaiting-audit-release-gate")
                            : QStringLiteral("production-provider-reviewed-invocation-arming-awaiting-runtime-preflight"))
                    && providerReviewedInvocationArming.value("armings").toArray().size() == 8
                    && !providerReviewedInvocationArming.value("operationInvokedByArming").toBool(true)
                    && !providerReviewedInvocationArming.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedInvocationArming.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedInvocationArming.value("resultCaptured").toBool(true)
                    && !providerReviewedInvocationArming.value("rawKeyExported").toBool(true)
                    && !providerReviewedInvocationArming.value("privateMaterialExported").toBool(true)
                    && !providerReviewedInvocationArming.value("sessionSecretExported").toBool(true),
                "production reviewed invocation arming should follow runtime preflight evidence") && ok;
    ok = expect(providerReviewedInvocationExecutionAcceptance.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-invocation-execution-acceptance-v1")
                    && providerReviewedInvocationExecutionAcceptance.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-not-release-gate")
                    && !providerReviewedInvocationExecutionAcceptance.value("accepted").toBool(true)
                    && providerReviewedInvocationExecutionAcceptance.value("executionAcceptanceNonReleaseGate").toBool(false)
                    && providerReviewedInvocationExecutionAcceptance.value("armingSourceCaptured")
                        .toBool(!expectedReviewedEvidenceSourceCaptured)
                            == expectedReviewedEvidenceSourceCaptured
                    && providerReviewedInvocationExecutionAcceptance.value("acceptanceCount").toInt() == 8
                    && providerReviewedInvocationExecutionAcceptance.value("readyAcceptanceCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && providerReviewedInvocationExecutionAcceptance.value("blockedAcceptanceCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedInvocationExecutionAcceptance.value("failClosedAcceptanceCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerReviewedInvocationExecutionAcceptance.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-reviewed-invocation-execution-acceptances-awaiting-audit-release-gate")
                            : QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-awaiting-arming"))
                    && providerReviewedInvocationExecutionAcceptance.value("acceptances").toArray().size() == 8
                    && !providerReviewedInvocationExecutionAcceptance.value("operationInvokedByExecutionAcceptance").toBool(true)
                    && !providerReviewedInvocationExecutionAcceptance.value("inputBytesCaptured").toBool(true)
                    && !providerReviewedInvocationExecutionAcceptance.value("outputBytesCaptured").toBool(true)
                    && !providerReviewedInvocationExecutionAcceptance.value("resultCaptured").toBool(true)
                    && !providerReviewedInvocationExecutionAcceptance.value("rawKeyExported").toBool(true)
                    && !providerReviewedInvocationExecutionAcceptance.value("privateMaterialExported").toBool(true)
                    && !providerReviewedInvocationExecutionAcceptance.value("sessionSecretExported").toBool(true),
                "production reviewed invocation execution acceptance should follow arming evidence") && ok;
    ok = expect(providerDataPlaneBridge.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-data-plane-bridge-v1")
                    && providerDataPlaneBridge.value("releaseGate").toString()
                        == QStringLiteral("production-provider-data-plane-bridge-not-release-gate")
                    && !providerDataPlaneBridge.value("accepted").toBool(true)
                    && providerDataPlaneBridge.value("dataPlaneBridgeNonReleaseGate").toBool(false)
                    && providerDataPlaneBridge.value("acceptanceSourceCaptured")
                        .toBool(!expectedReviewedEvidenceSourceCaptured)
                            == expectedReviewedEvidenceSourceCaptured
                    && providerDataPlaneBridge.value("bridgeCount").toInt() == 8
                    && providerDataPlaneBridge.value("readyBridgeCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && providerDataPlaneBridge.value("blockedBridgeCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerDataPlaneBridge.value("failClosedBridgeCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerDataPlaneBridge.value("bridges").toArray().size() == 8
                    && providerDataPlaneBridge.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-provider-data-plane-bridges-awaiting-audit-release-gate")
                            : QStringLiteral("production-provider-data-plane-bridge-awaiting-execution-acceptance"))
                    && !providerDataPlaneBridge.value("publicApiInvoked").toBool(true)
                    && !providerDataPlaneBridge.value("providerInvokedByBridge").toBool(true)
                    && !providerDataPlaneBridge.value("inputBytesCaptured").toBool(true)
                    && !providerDataPlaneBridge.value("outputBytesCaptured").toBool(true)
                    && !providerDataPlaneBridge.value("resultCaptured").toBool(true)
                    && !providerDataPlaneBridge.value("rawKeyExported").toBool(true)
                    && !providerDataPlaneBridge.value("privateMaterialExported").toBool(true)
                    && !providerDataPlaneBridge.value("sessionSecretExported").toBool(true),
                "production data-plane bridge should follow reviewed execution acceptance evidence") && ok;
    ok = expect(providerPublicPrimitiveExecution.value("schema").toString()
                    == QStringLiteral("qtnetworkchat-e2e-production-provider-public-primitive-execution-v1")
                    && providerPublicPrimitiveExecution.value("releaseGate").toString()
                        == QStringLiteral("production-public-primitive-execution-not-release-gate")
                    && !providerPublicPrimitiveExecution.value("accepted").toBool(true)
                    && providerPublicPrimitiveExecution.value("publicPrimitiveExecutionNonReleaseGate").toBool(false)
                    && providerPublicPrimitiveExecution.value("bridgeSourceCaptured").toBool(false)
                    && providerPublicPrimitiveExecution.value("executionCount").toInt() == 8
                    && providerPublicPrimitiveExecution.value("readyExecutionCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && providerPublicPrimitiveExecution.value("blockedExecutionCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerPublicPrimitiveExecution.value("failClosedExecutionCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && providerPublicPrimitiveExecution.value("executions").toArray().size() == 8
                    && providerPublicPrimitiveExecution.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-public-primitive-executions-awaiting-explicit-probe")
                            : QStringLiteral("production-public-primitive-execution-evidence-blocked"))
                    && !providerPublicPrimitiveExecution.value("publicApiInvoked").toBool(true)
                    && !providerPublicPrimitiveExecution.value("providerInvokedByPublicPrimitive").toBool(true)
                    && !providerPublicPrimitiveExecution.value("inputBytesCaptured").toBool(true)
                    && !providerPublicPrimitiveExecution.value("outputBytesCaptured").toBool(true)
                    && !providerPublicPrimitiveExecution.value("resultBytesCaptured").toBool(true)
                    && !providerPublicPrimitiveExecution.value("rawKeyExported").toBool(true)
                    && !providerPublicPrimitiveExecution.value("privateMaterialExported").toBool(true)
                    && !providerPublicPrimitiveExecution.value("sessionSecretExported").toBool(true),
                "production public primitive execution should follow data-plane bridge evidence") && ok;
    const QJsonObject firstHarnessOperation = harness.value("operations").toArray().at(0).toObject();
    ok = expect(firstHarnessOperation.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstHarnessOperation.value("implementationState").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("linked-reviewed-session-key-generation")
                            : (adapterLinked
                                ? QStringLiteral("linked-placeholder")
                                : QStringLiteral("not-linked")))
                    && firstHarnessOperation.value("fixtureHashSha256").toString().size() == 64
                    && firstHarnessOperation.value("vectorSet").toString()
                        == QStringLiteral("production-session-key-generation-vectors-v1")
                    && firstHarnessOperation.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : QStringLiteral("production-session-key-generation-not-implemented")),
                "production operation harness should expose sanitized fixture hashes for linked placeholder operations") && ok;
    const QJsonObject firstExecutionStep = executionPlan.value("steps").toArray().at(0).toObject();
    ok = expect(firstExecutionStep.value("sequenceIndex").toInt(-1) == 0
                    && firstExecutionStep.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstExecutionStep.value("dispatchState").toString() == expectedDispatchState
                    && firstExecutionStep.value("implementationState").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("linked-reviewed-session-key-generation")
                            : (adapterLinked
                                ? QStringLiteral("linked-placeholder")
                                : QStringLiteral("not-linked")))
                    && firstExecutionStep.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-operation-step-ready")
                            : harness.value("releaseGate").toString())
                    && firstExecutionStep.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : QStringLiteral("production-session-key-generation-not-implemented"))
                    && firstExecutionStep.value("harnessRunnable")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound,
                "production execution plan should bind each ordered step to harness evidence") && ok;
    const QJsonObject firstInvocation = invocation.value("invocations").toArray().at(0).toObject();
    ok = expect(firstInvocation.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstInvocation.value("invocationState").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("callable")
                            : (adapterLinked
                                ? QStringLiteral("blocked-linked-placeholder")
                                : QStringLiteral("blocked-not-linked")))
                    && firstInvocation.value("inputContract").toArray().at(0).toString()
                        == QStringLiteral("secure-random-source")
                    && firstInvocation.value("outputContract").toArray().at(0).toString()
                        == QStringLiteral("session-key-handle")
                    && firstInvocation.value("releaseGate").isUndefined()
                    && firstInvocation.value("callable")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && !firstInvocation.value("rawKeyExported").toBool(true),
                "production invocation contracts should expose callable boundaries without enabling placeholder crypto") && ok;
    const QJsonObject firstSlot = slotStatus.value("slots").toArray().at(0).toObject();
    ok = expect(firstSlot.value("slotId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1/session-key-generation-slot")
                    && firstSlot.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstSlot.value("reviewState").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("reviewed")
                            : (adapterLinked
                                ? QStringLiteral("placeholder-linked")
                                : QStringLiteral("not-linked")))
                    && firstSlot.value("sideEffectPolicy").toString()
                        == QStringLiteral("may-create-key-handle")
                    && firstSlot.value("reviewed").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && firstSlot.value("callable").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound,
                "production slot registry should expose reviewed provider symbols without marking placeholders ready") && ok;
    const QJsonObject firstBinding = dispatchBindings.value("bindings").toArray().at(0).toObject();
    ok = expect(firstBinding.value("slotId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1/session-key-generation-slot")
                    && firstBinding.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstBinding.value("bindingState").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("reviewed-bound")
                            : (adapterLinked
                                ? QStringLiteral("linked-placeholder")
                                : QStringLiteral("not-linked")))
                    && firstBinding.value("dispatchCallable").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && firstBinding.value("expectedSignature").toString()
                        .contains(QStringLiteral("qnc_e2e_op_session_key_generation_v1"))
                    && firstBinding.value("inputContract").toArray().at(0).toString()
                        == QStringLiteral("secure-random-source")
                    && firstBinding.value("outputContract").toArray().at(0).toString()
                        == QStringLiteral("session-key-handle")
                    && firstBinding.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : (adapterLinked
                                ? QStringLiteral("production-dispatch-binding-placeholder")
                                : QStringLiteral("production-crypto-backend-unavailable")))
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
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("reviewed-bound")
                            : (adapterLinked
                                ? QStringLiteral("linked-placeholder")
                                : QStringLiteral("not-linked")))
                    && firstCallable.value("symbolMatches").toBool(false)
                    && firstCallable.value("abiSignatureMatches").toBool(false)
                    && firstCallable.value("fixtureHashMatches").toBool(false)
                    && firstCallable.value("dispatchCallable").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && firstCallable.value("callable").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && firstCallable.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : (adapterLinked
                                ? QStringLiteral("production-callable-manifest-placeholder")
                                : QStringLiteral("production-crypto-backend-unavailable")))
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
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("passed-reviewed-production-result")
                            : (adapterLinked
                                ? QStringLiteral("blocked-linked-placeholder")
                                : QStringLiteral("blocked-not-linked")))
                    && firstExecutionResult.value("errorClass").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : (adapterLinked
                                ? QStringLiteral("production-result-placeholder-not-executed")
                                : QStringLiteral("production-result-adapter-not-linked")))
                    && firstExecutionResult.value("outputContract").toArray().at(0).toString()
                        == QStringLiteral("session-key-handle")
                    && firstExecutionResult.value("resultContract").toArray().size() == 4
                    && firstExecutionResult.value("passed").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && firstExecutionResult.value("sanitized").toBool(false)
                    && firstExecutionResult.value("operationInvoked")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && firstExecutionResult.value("resultCaptured")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && firstExecutionResult.value("providerProbeVectorMatched")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
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
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("ready-for-reviewed-provider-callback")
                            : (adapterLinked
                                ? QStringLiteral("blocked-linked-placeholder")
                                : QStringLiteral("blocked-not-linked")))
                    && firstProviderDecision.value("reviewedProviderCallbackAllowed")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
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
                    && firstProviderCallFrame.value("frameReady").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
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
                    && acceptance.value("productionReady").toBool(!expectedProductionReady)
                        == expectedProductionReady
                    && acceptance.value("accepted").toBool(!reviewedProviderOperationsBound)
                        == reviewedProviderOperationsBound
                    && acceptance.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-crypto-accepted")
                            : (adapterLinked
                            ? QStringLiteral("production-operations-not-ready")
                            : QStringLiteral("production-adapter-not-linked")))
                    && acceptance.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound
                            ? QString()
                            : expectedReason)
                    && acceptance.value("operationContractComplete").toBool(false)
                    && acceptance.value("registeredOperationCount").toInt() == 8
                    && acceptance.value("blockedOperationCount").toInt()
                        == (reviewedProviderOperationsBound ? 0 : 8)
                    && acceptance.value("operationGates").toArray().size() == 8
                    && acceptance.value("operationManifest").toArray().size() == 8
                    && acceptance.value("operationDispatchBindings").toObject()
                        .value("bindings").toArray().size() == 8
                    && acceptance.value("operationCallableManifest").toObject()
                        .value("callables").toArray().size() == 8
                    && acceptance.value("operationDispatchBindings").toObject()
                        .value("accepted").toBool(!expectedProviderControlGateAccepted)
                            == expectedProviderControlGateAccepted
                    && acceptance.value("operationCallableManifest").toObject()
                        .value("accepted").toBool(!expectedProviderControlGateAccepted)
                            == expectedProviderControlGateAccepted
                    && acceptance.value("operationExecutionResult").toObject()
                        .value("results").toArray().size() == 8
                    && acceptance.value("operationExecutionResult").toObject()
                        .value("accepted").toBool(!expectedProviderProbeAccepted)
                            == expectedProviderProbeAccepted
                    && acceptance.value("providerExecutionDecision").toObject()
                        .value("decisions").toArray().size() == 8
                    && acceptance.value("providerCallFrame").toObject()
                        .value("frames").toArray().size() == 8
                    && acceptance.value("providerCallFrameAccepted").toBool(!expectedStructuralGateAccepted)
                        == expectedStructuralGateAccepted
                    && acceptance.value("providerCallFrameBlockedFrameCount").toInt()
                        == (8 - expectedPreflightReadyCount)
                    && acceptance.value("providerExecutionDecisionAccepted")
                        .toBool(!expectedProviderControlGateAccepted)
                            == expectedProviderControlGateAccepted
                    && acceptance.value("providerExecutionDecisionBlockedDecisionCount").toInt()
                        == expectedProviderProbeBlockedCount
                    && acceptance.value("passedExecutionResultCount").toInt()
                        == expectedProviderProbeReadyCount
                    && acceptance.value("providerInvocationResultAccepted")
                        .toBool(!expectedProviderProbeAccepted) == expectedProviderProbeAccepted
                    && acceptance.value("providerVectorSelfTestAccepted")
                        .toBool(!expectedProviderProbeAccepted) == expectedProviderProbeAccepted
                    && acceptance.value("providerCallbackHarnessAccepted")
                        .toBool(!expectedProviderControlGateAccepted)
                            == expectedProviderControlGateAccepted
                    && acceptance.value("providerExecutionSlotBindingAccepted")
                        .toBool(!expectedProviderControlGateAccepted)
                            == expectedProviderControlGateAccepted
                    && acceptance.value("providerExecutionPathAccepted")
                        .toBool(!expectedProviderControlGateAccepted)
                            == expectedProviderControlGateAccepted
                    && acceptance.value("providerInvocationSandboxAccepted")
                        .toBool(!expectedProviderControlGateAccepted)
                            == expectedProviderControlGateAccepted
                    && acceptance.value("providerInvocationVectorResultAccepted")
                        .toBool(!expectedProviderProbeAccepted) == expectedProviderProbeAccepted
                    && acceptance.value("providerInvocationExecutionAccepted")
                        .toBool(!expectedProviderProbeAccepted) == expectedProviderProbeAccepted
                    && acceptance.value("implementedOperationCount").toInt()
                        == reviewedCandidateCount
                    && acceptance.value("providerReviewedExecutionCandidateReadyCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && acceptance.value("providerReviewedCallHandoffReadyCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && acceptance.value("providerReviewedOperationStubBoundaryReadyCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && acceptance.value("providerReviewedCallableTableBridgeReadyCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && acceptance.value("providerReviewedOperationCallableInterfaceReadyCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && acceptance.value("providerReviewedCallableRuntimePreflightReadyCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && acceptance.value("providerReviewedInvocationArmingReadyCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && acceptance.value("providerReviewedInvocationExecutionAcceptanceReadyCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && acceptance.value("providerDataPlaneBridgeReadyCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && acceptance.value("providerDataPlaneBridgeBlockedCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && acceptance.value("providerDataPlaneBridge").toObject()
                        .value("bridges").toArray().size() == 8
                    && acceptance.value("providerPublicPrimitiveExecutionReadyCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && acceptance.value("providerPublicPrimitiveExecutionBlockedCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && acceptance.value("providerPublicPrimitiveExecutionReady")
                        .toBool(!reviewedProviderOperationsBound)
                            == reviewedProviderOperationsBound
                    && acceptance.value("providerPublicPrimitiveExecution").toObject()
                        .value("executions").toArray().size() == 8
                    && !acceptance.value("rawKeyExported").toBool(true)
                    && !acceptance.value("privateMaterialExported").toBool(true),
                "production acceptance status should summarize reviewed operation gates and execution evidence") && ok;
    ok = expect(rolloutObservability.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-rollout-observability-v1")
                    && rolloutObservability.value("backendId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1")
                    && rolloutObservability.value("providerId").toString()
                        == QStringLiteral("openssl-reviewed-provider-v1")
                    && rolloutObservability.value("linked").toBool(false) == adapterLinked
                    && rolloutObservability.value("productionReady").toBool(!expectedProductionReady)
                        == expectedProductionReady
                    && rolloutObservability.value("productionAcceptanceAccepted")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && rolloutObservability.value("productionAcceptanceReleaseGate").toString()
                        == acceptance.value("releaseGate").toString()
                    && rolloutObservability.value("accepted")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && rolloutObservability.value("releaseRunObservable")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && rolloutObservability.value("releaseGate").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-rollout-observability-ready")
                            : (adapterLinked
                                ? QStringLiteral("production-rollout-observability-blocked-not-ready")
                                : QStringLiteral("production-rollout-observability-blocked-not-linked")))
                    && rolloutObservability.value("blockedReason").toString()
                        == (reviewedProviderOperationsBound ? QString() : expectedReason)
                    && rolloutObservability.value("operatorAction").toString()
                        == (reviewedProviderOperationsBound ? QStringLiteral("none") : expectedAction)
                    && rolloutObservability.value("requiredOperationCount").toInt() == 8
                    && rolloutObservability.value("materialExportProofCount").toInt()
                        == 8
                    && rolloutObservability.value("outputShapeProofCount").toInt()
                        == 8
                    && rolloutObservability.value("publicPrimitiveReadyCount").toInt()
                        == expectedReviewedEvidenceReadyCount
                    && rolloutObservability.value("publicPrimitiveBlockedCount").toInt()
                        == expectedReviewedEvidenceBlockedCount
                    && rolloutObservability.value("noSensitiveExportProof")
                        .toBool(!reviewedProviderOperationsBound) == reviewedProviderOperationsBound
                    && rolloutObservability.value("sensitiveFieldsSuppressed").toBool(false)
                    && rolloutObservability.value("statusCapturePolicy").toString()
                        == QStringLiteral("status-counts-release-gates-and-actions-only")
                    && rolloutObservability.value("operatorRecoveryPrompts").toArray().size() == 3
                    && rolloutObservability.value("userRecoveryPrompts").toArray()
                        .contains(reviewedProviderOperationsBound
                            ? QStringLiteral("none")
                            : (adapterLinked
                                ? QStringLiteral("keep-existing-e2e-state-and-wait-for-production-crypto-readiness")
                                : QStringLiteral("retry-after-operator-links-production-crypto-backend")))
                    && rolloutObservability.value("operatorRecoveryPrompts").toArray()
                        .contains(QStringLiteral("verify-filesystem-object-ciphertext-readback-evidence-before-full-file-resume-release"))
                    && rolloutObservability.value("userRecoveryPrompts").toArray()
                        .contains(QStringLiteral("show-resume-or-resend-guidance-from-encrypted-file-recovery-status"))
                    && rolloutObservability.value("filesystemObjectRecoveryReady").toBool(false)
                    && rolloutObservability.value("filesystemObjectRecoveryReleaseGate").toString()
                        == QStringLiteral("e2e-filesystem-object-ciphertext-readback-ready")
                    && rolloutObservability.value("filesystemObjectRecoveryAction").toString()
                        == QStringLiteral("resume-verified-filesystem-object-ciphertext-or-fail-closed-to-resend")
                    && rolloutObservability.value("filesystemObjectRecoveryNoSensitiveExportProof")
                        .toBool(false)
                    && rolloutObservability.value("filesystemObjectRecoveryPromptReady").toBool(false)
                    && !rolloutObservability.value("rawKeyExported").toBool(true)
                    && !rolloutObservability.value("privateMaterialExported").toBool(true)
                    && !rolloutObservability.value("sessionSecretExported").toBool(true)
                    && !rolloutObservability.value("privateIdentityMaterialExported").toBool(true)
                    && !rolloutObservability.value("fullPublicIdentityMaterialExported").toBool(true)
                    && !rolloutObservability.value("plaintextBytesExported").toBool(true)
                    && !rolloutObservability.value("ciphertextBytesExported").toBool(true)
                    && rolloutObservability.value("offlineObjectRecoveryReady").toBool(false)
                    && rolloutObservability.value("offlineObjectRecoveryScope").toString()
                        == QStringLiteral("offline-ciphertext-readback")
                    && rolloutObservability.value("offlineObjectRecoveryReleaseGate").toString()
                        == QStringLiteral("e2e-offline-ciphertext-readback-reviewed-opt-in")
                    && rolloutObservability.value("offlineObjectRecoveryNoSensitiveExportProof")
                        .toBool(false),
                "production rollout observability should become ready only after linked acceptance and no-sensitive-export proof") && ok;
    const QJsonObject firstGate = acceptance.value("operationGates").toArray().at(0).toObject();
    ok = expect(firstGate.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstGate.value("entrypoint").toString()
                        == QStringLiteral("production-adapter/session-key-generation")
                    && firstGate.value("implementationState").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("linked-reviewed-session-key-generation")
                            : (adapterLinked
                                ? QStringLiteral("linked-placeholder")
                                : QStringLiteral("not-linked")))
                    && firstGate.value("vectorSet").toString()
                        == QStringLiteral("production-session-key-generation-vectors-v1")
                    && firstGate.value("compatibilityStatus").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("known-answer-shape-passed")
                            : (adapterLinked
                                ? QStringLiteral("not-run-placeholder")
                                : QStringLiteral("not-run-not-linked")))
                    && firstGate.value("migrationBlocker").toString()
                        == (reviewedProviderOperationsBound
                            ? QStringLiteral("production-acceptance-gates-not-open")
                            : QStringLiteral("production-session-key-generation-not-implemented"))
                    && firstGate.value("available").toBool(!expectedAvailable)
                        == expectedAvailable
                    && firstGate.value("blockedReason").toString()
                        == (expectedAvailable ? QString() : expectedReason)
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
                            == (reviewedProviderOperationsBound
                                ? QStringLiteral("linked-reviewed-payload-encrypt")
                                : (adapterLinked
                                    ? QStringLiteral("linked-placeholder")
                                    : QStringLiteral("not-linked")))
                    && payloadEncrypt.value("operationImplementation").toObject()
                        .value("vectorSet").toString()
                            == QStringLiteral("production-payload-encrypt-vectors-v1")
                    && payloadEncrypt.value("operationImplementation").toObject()
                        .value("migrationBlocker").toString()
                            == (reviewedProviderOperationsBound
                                ? QStringLiteral("production-acceptance-gates-not-open")
                                : QStringLiteral("production-payload-encrypt-not-implemented"))
                    && payloadEncrypt.value("requiresProductionReady").toBool(false)
                    && payloadEncrypt.value("adapterLinked").toBool(!adapterLinked) == adapterLinked
                    && payloadEncrypt.value("productionReady").toBool(!expectedProductionReady)
                        == expectedProductionReady
                    && payloadEncrypt.value("available").toBool(!expectedAvailable)
                        == expectedAvailable
                    && payloadEncrypt.value("reason").toString()
                        == (expectedAvailable
                            ? QStringLiteral("production-backend-available")
                            : expectedReason)
                    && payloadEncrypt.value("blockedReason").toString()
                        == (expectedAvailable ? QString() : expectedReason)
                    && payloadEncrypt.value("operatorAction").toString() == expectedAction
                    && payloadEncrypt.value("rawKeyExported").toBool(true) == false
                    && payloadEncrypt.value("privateMaterialExported").toBool(true) == false,
                "production adapter operation should expose reviewed operation evidence and dispatch state") && ok;

    const QByteArray sessionKey = generateE2ESessionKey();
    ok = expect(reviewedProviderOperationsBound
                    ? sessionKey.size() == 32
                    : sessionKey.isEmpty(),
                "production adapter runtime should generate a session key only after productionReady") && ok;

    if (reviewedProviderOperationsBound) {
        const QByteArray aliceIdentityPrivateKey = generateE2EPrivateKey();
        const QByteArray bobIdentityPrivateKey = generateE2EPrivateKey();
        const QByteArray aliceAgreementPrivateKey = generateE2EPrivateKey();
        const QByteArray bobAgreementPrivateKey = generateE2EPrivateKey();
        const QByteArray aliceIdentityPublicKey =
            e2ePublicKeyFromPrivateKey(aliceIdentityPrivateKey);
        const QByteArray bobIdentityPublicKey =
            e2ePublicKeyFromPrivateKey(bobIdentityPrivateKey);
        const QByteArray aliceAgreementPublicKey =
            e2ePublicKeyFromPrivateKey(aliceAgreementPrivateKey);
        const QByteArray bobAgreementPublicKey =
            e2ePublicKeyFromPrivateKey(bobAgreementPrivateKey);
        E2EKeyAgreement aliceAgreement;
        aliceAgreement.protocol = QStringLiteral("qtnetworkchat-e2e-v1");
        aliceAgreement.suite = e2eDefaultSuite();
        aliceAgreement.senderId = QStringLiteral("10001");
        aliceAgreement.receiverId = QStringLiteral("10002");
        aliceAgreement.keyId = QStringLiteral("production-public-api-alice");
        aliceAgreement.publicKey = aliceAgreementPublicKey;
        aliceAgreement.senderIdentityFingerprint = e2eFingerprint(aliceIdentityPublicKey);
        aliceAgreement.receiverIdentityFingerprint = e2eFingerprint(bobIdentityPublicKey);
        E2EKeyAgreement bobAgreement;
        bobAgreement.protocol = aliceAgreement.protocol;
        bobAgreement.suite = aliceAgreement.suite;
        bobAgreement.senderId = aliceAgreement.receiverId;
        bobAgreement.receiverId = aliceAgreement.senderId;
        bobAgreement.keyId = QStringLiteral("production-public-api-bob");
        bobAgreement.publicKey = bobAgreementPublicKey;
        bobAgreement.senderIdentityFingerprint = aliceAgreement.receiverIdentityFingerprint;
        bobAgreement.receiverIdentityFingerprint = aliceAgreement.senderIdentityFingerprint;
        ok = expect(aliceIdentityPrivateKey.size() == 32
                        && bobIdentityPrivateKey.size() == 32
                        && aliceAgreementPrivateKey.size() == 32
                        && bobAgreementPrivateKey.size() == 32
                        && aliceIdentityPublicKey.size() == 32
                        && bobIdentityPublicKey.size() == 32
                        && aliceAgreementPublicKey.size() == 32
                        && bobAgreementPublicKey.size() == 32
                        && aliceAgreement.suite
                            == QStringLiteral("x25519-hkdf-sha256-aes-256-gcm"),
                    "linked OpenSSL public API chain should generate identity and agreement key material") && ok;
        ok = expect(signE2EKeyAgreement(&aliceAgreement, aliceIdentityPrivateKey, &reason)
                        && aliceAgreement.signature.size() == 64
                        && reason.isEmpty()
                        && signE2EKeyAgreement(&bobAgreement, bobIdentityPrivateKey, &reason)
                        && bobAgreement.signature.size() == 64
                        && reason.isEmpty(),
                    "linked OpenSSL public API chain should sign both agreements") && ok;
        ok = expect(verifyE2EKeyAgreementSignature(aliceAgreement,
                                                   aliceIdentityPublicKey,
                                                   &reason)
                        && reason.isEmpty()
                        && verifyE2EKeyAgreementSignature(bobAgreement,
                                                          bobIdentityPublicKey,
                                                          &reason)
                        && reason.isEmpty(),
                    "linked OpenSSL public API chain should verify both agreement signatures") && ok;
        E2EKeyAgreement tamperedAgreement = aliceAgreement;
        tamperedAgreement.publicKey = bobAgreementPublicKey;
        ok = expect(!verifyE2EKeyAgreementSignature(tamperedAgreement,
                                                    aliceIdentityPublicKey,
                                                    &reason)
                        && reason == QStringLiteral("agreement-signature-invalid"),
                    "linked OpenSSL public API chain should reject tampered agreement material") && ok;
        E2EKeyAgreement tamperedSignatureAgreement = bobAgreement;
        if (!tamperedSignatureAgreement.signature.isEmpty()) {
            tamperedSignatureAgreement.signature[0] =
                static_cast<char>(tamperedSignatureAgreement.signature.at(0) ^ 0x01);
        }
        ok = expect(!verifyE2EKeyAgreementSignature(tamperedSignatureAgreement,
                                                    bobIdentityPublicKey,
                                                    &reason)
                        && reason == QStringLiteral("agreement-signature-invalid"),
                    "linked OpenSSL public API chain should reject tampered signatures") && ok;
        const QByteArray aliceDerived =
            deriveE2EAuthenticatedSessionKey(aliceAgreementPrivateKey,
                                             aliceAgreement,
                                             bobAgreement,
                                             &reason);
        ok = expect(aliceDerived.size() == 32 && reason.isEmpty(),
                    "linked OpenSSL public API chain should derive Alice session material") && ok;
        const QByteArray bobDerived =
            deriveE2EAuthenticatedSessionKey(bobAgreementPrivateKey,
                                             bobAgreement,
                                             aliceAgreement,
                                             &reason);
        ok = expect(bobDerived.size() == 32
                        && bobDerived == aliceDerived
                        && reason.isEmpty(),
                    "linked OpenSSL public API chain should derive matching Bob session material") && ok;
        const QByteArray productionPlaintext("production-public-api-payload", 29);
        const E2EEnvelope productionEnvelope =
            encryptE2EPayload(aliceAgreement.senderId,
                              bobAgreement.senderId,
                              QStringLiteral("production-public-api-session"),
                              aliceDerived,
                              productionPlaintext,
                              QStringLiteral("production-public-api-runtime-v1"),
                              &reason);
        QByteArray productionDecrypted;
        ok = expect(productionEnvelope.isValid()
                        && productionEnvelope.suite == aliceAgreement.suite
                        && productionEnvelope.ciphertext.size() == productionPlaintext.size()
                        && productionEnvelope.tag.size() >= 16
                        && reason.isEmpty()
                        && decryptE2EPayload(productionEnvelope,
                                             bobDerived,
                                             &productionDecrypted,
                                             &reason)
                        && productionDecrypted == productionPlaintext
                        && reason.isEmpty(),
                    "linked OpenSSL public API chain should encrypt and decrypt payloads with derived sessions") && ok;
        E2EEnvelope tamperedProductionEnvelope = productionEnvelope;
        if (!tamperedProductionEnvelope.tag.isEmpty()) {
            tamperedProductionEnvelope.tag[0] =
                static_cast<char>(tamperedProductionEnvelope.tag.at(0) ^ 0x01);
        }
        ok = expect(!decryptE2EPayload(tamperedProductionEnvelope,
                                       bobDerived,
                                       &productionDecrypted,
                                       &reason)
                        && reason == QStringLiteral("payload-decrypt-failed"),
                    "linked OpenSSL public API chain should reject tampered payload tags") && ok;

        const QJsonObject invocationExecutionProbe =
            e2eProbeProductionCryptoProviderInvocationExecution();
        const QJsonObject firstProbe =
            invocationExecutionProbe.value("probes").toArray().at(0).toObject();
        const QJsonObject secondProbe =
            invocationExecutionProbe.value("probes").toArray().at(1).toObject();
        const QJsonObject thirdProbe =
            invocationExecutionProbe.value("probes").toArray().at(2).toObject();
        const QJsonObject fourthProbe =
            invocationExecutionProbe.value("probes").toArray().at(3).toObject();
        const QJsonObject fifthProbe =
            invocationExecutionProbe.value("probes").toArray().at(4).toObject();
        const QJsonObject sixthProbe =
            invocationExecutionProbe.value("probes").toArray().at(5).toObject();
        const QJsonObject seventhProbe =
            invocationExecutionProbe.value("probes").toArray().at(6).toObject();
        const QJsonObject eighthProbe =
            invocationExecutionProbe.value("probes").toArray().at(7).toObject();
        ok = expect(invocationExecutionProbe.value("invokedOperationCount").toInt() == 8
                        && invocationExecutionProbe.value("okStatusCount").toInt()
                            == reviewedCandidateCount
                        && invocationExecutionProbe.value("vectorPassCount").toInt()
                            == reviewedCandidateCount
                        && invocationExecutionProbe.value("vectorFailCount").toInt()
                            == remainingUnsupportedCount
                        && invocationExecutionProbe.value("providerVectorSetMatchedCount").toInt()
                            == 8
                        && invocationExecutionProbe.value("providerVectorSetMismatchCount").toInt()
                            == 0
                        && invocationExecutionProbe.value("knownAnswerOutputClassSummary").toObject()
                            .value("handle-status-output").toInt() == 2
                        && invocationExecutionProbe.value("knownAnswerOutputClassSummary").toObject()
                            .value("public-sealed-output-shape").toInt() == 1
                        && invocationExecutionProbe.value("knownAnswerOutputClassSummary").toObject()
                            .value("sealed-output-shape").toInt() == 1
                        && invocationExecutionProbe.value("knownAnswerOutputClassSummary").toObject()
                            .value("public-output-shape").toInt() == 3
                        && invocationExecutionProbe.value("knownAnswerOutputClassSummary").toObject()
                            .value("payload-output-shape").toInt() == 1
                        && invocationExecutionProbe.value("knownAnswerOutputClassSummary").toObject()
                            .value("status-error-output").toInt() == 0
                        && invocationExecutionProbe.value("failureClassSummary").toObject()
                            .value("none").toInt() == reviewedCandidateCount
                        && invocationExecutionProbe.value("failureClassSummary").toObject()
                            .value("unsupported").toInt() == remainingUnsupportedCount,
                    "linked OpenSSL provider probe summary should count eight reviewed candidates") && ok;
        ok = expect(firstProbe.value("operation").toString()
                            == QStringLiteral("session-key-generation")
                        && firstProbe.value("functionPointerPresent").toBool(false)
                        && firstProbe.value("operationInvoked").toBool(false)
                        && firstProbe.value("callbackStatusClass").toString()
                            == QStringLiteral("ok")
                        && firstProbe.value("sealedOutputSize").toInt() == 32
                        && firstProbe.value("materialPolicyClass").toString()
                            == QStringLiteral("handle-only")
                        && firstProbe.value("providerVectorSetMatched").toBool(false)
                        && firstProbe.value("materialExportProof").toString()
                            == QStringLiteral("sizes-and-status-only-no-secret-bytes")
                        && !firstProbe.value("outputBytesCaptured").toBool(true)
                        && !firstProbe.value("rawKeyExported").toBool(true),
                    "linked OpenSSL provider should expose a sanitized session-key probe") && ok;
        ok = expect(secondProbe.value("operation").toString()
                            == QStringLiteral("identity-key-generation")
                        && secondProbe.value("callbackStatusClass").toString()
                            == QStringLiteral("ok")
                        && secondProbe.value("publicOutputSize").toInt() == 32
                        && secondProbe.value("sealedOutputSize").toInt() == 32
                        && secondProbe.value("expectedKnownAnswerOutputClass").toString()
                            == QStringLiteral("public-sealed-output-shape")
                        && secondProbe.value("observedKnownAnswerOutputClass").toString()
                            == QStringLiteral("public-sealed-output-shape")
                        && secondProbe.value("providerVectorSetMatched").toBool(false),
                    "linked OpenSSL provider should expose a sanitized identity-key probe") && ok;
        ok = expect(thirdProbe.value("operation").toString()
                            == QStringLiteral("public-key-derivation")
                        && thirdProbe.value("callbackStatusClass").toString()
                            == QStringLiteral("ok")
                        && thirdProbe.value("publicOutputSize").toInt() == 32
                        && thirdProbe.value("sealedOutputSize").toInt() == 0
                        && thirdProbe.value("providerVectorSetMatched").toBool(false),
                    "linked OpenSSL provider should expose a sanitized public-key derivation probe") && ok;
        ok = expect(fourthProbe.value("operation").toString()
                            == QStringLiteral("agreement-sign")
                        && fourthProbe.value("callbackStatusClass").toString()
                            == QStringLiteral("ok")
                        && fourthProbe.value("publicOutputSize").toInt() == 64
                        && fourthProbe.value("sealedOutputSize").toInt() == 0
                        && fourthProbe.value("providerVectorSetMatched").toBool(false),
                    "linked OpenSSL provider should expose a sanitized agreement-sign probe") && ok;
        ok = expect(fifthProbe.value("operation").toString()
                            == QStringLiteral("agreement-verify")
                        && fifthProbe.value("callbackStatusClass").toString()
                            == QStringLiteral("ok")
                        && fifthProbe.value("publicOutputSize").toInt() == 32
                        && fifthProbe.value("sealedOutputSize").toInt() == 0
                        && fifthProbe.value("providerVectorSetMatched").toBool(false),
                    "linked OpenSSL provider should expose a sanitized agreement-verify probe") && ok;
        ok = expect(sixthProbe.value("operation").toString()
                            == QStringLiteral("session-derive")
                        && sixthProbe.value("callbackStatusClass").toString()
                            == QStringLiteral("ok")
                        && sixthProbe.value("publicOutputSize").toInt() == 0
                        && sixthProbe.value("sealedOutputSize").toInt() == 32
                        && sixthProbe.value("materialPolicyClass").toString()
                            == QStringLiteral("handle-only")
                        && sixthProbe.value("providerVectorSetMatched").toBool(false),
                    "linked OpenSSL provider should expose a sanitized session-derive probe") && ok;
        ok = expect(seventhProbe.value("operation").toString()
                            == QStringLiteral("payload-encrypt")
                        && seventhProbe.value("callbackStatusClass").toString()
                            == QStringLiteral("ok")
                        && seventhProbe.value("publicOutputSize").toInt() == 0
                        && seventhProbe.value("sealedOutputSize").toInt() > 28
                        && seventhProbe.value("materialPolicyClass").toString()
                            == QStringLiteral("payload-bytes-allowed")
                        && seventhProbe.value("providerVectorSetMatched").toBool(false),
                    "linked OpenSSL provider should expose a sanitized payload-encrypt probe") && ok;
        ok = expect(eighthProbe.value("operation").toString()
                            == QStringLiteral("payload-decrypt")
                        && eighthProbe.value("callbackStatusClass").toString()
                            == QStringLiteral("ok")
                        && eighthProbe.value("publicOutputSize").toInt() == 32
                        && eighthProbe.value("sealedOutputSize").toInt() == 0
                        && eighthProbe.value("materialPolicyClass").toString()
                            == QStringLiteral("payload-bytes-allowed")
                        && eighthProbe.value("providerVectorSetMatched").toBool(false),
                    "linked OpenSSL provider should expose a sanitized payload-decrypt probe") && ok;

        const QJsonObject runtimePreflightProbe =
            e2eProbeProductionCryptoProviderReviewedCallableRuntimePreflight();
        const QJsonObject invocationArmingProbe =
            e2eProbeProductionCryptoProviderReviewedInvocationArming();
        const QJsonObject executionAcceptanceProbe =
            e2eProbeProductionCryptoProviderReviewedInvocationExecutionAcceptance();
        const QJsonObject dataPlaneBridgeProbe =
            e2eProbeProductionCryptoProviderDataPlaneBridge();
        ok = expect(runtimePreflightProbe.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-callable-runtime-preflight-v1")
                        && runtimePreflightProbe.value("releaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-callable-runtime-preflight-not-release-gate")
                        && !runtimePreflightProbe.value("accepted").toBool(true)
                        && runtimePreflightProbe.value("runtimePreflightNonReleaseGate").toBool(false)
                        && runtimePreflightProbe.value("interfaceSourceCaptured").toBool(false)
                        && runtimePreflightProbe.value("providerReviewedOperationCallableInterfaceReadyCount").toInt() == 8
                        && runtimePreflightProbe.value("preflightCount").toInt() == 8
                        && runtimePreflightProbe.value("readyPreflightCount").toInt() == 8
                        && runtimePreflightProbe.value("blockedPreflightCount").toInt() == 0
                        && runtimePreflightProbe.value("failClosedPreflightCount").toInt() == 0
                        && runtimePreflightProbe.value("interfaceReadyPreflightCount").toInt() == 8
                        && runtimePreflightProbe.value("abiPreflightCount").toInt() == 8
                        && runtimePreflightProbe.value("contractPreflightCount").toInt() == 8
                        && runtimePreflightProbe.value("policyPreflightCount").toInt() == 8
                        && runtimePreflightProbe.value("fixturePreflightCount").toInt() == 8
                        && runtimePreflightProbe.value("sanitizedPreflightCount").toInt() == 8
                        && runtimePreflightProbe.value("blockedReason").toString()
                            == QStringLiteral("production-provider-reviewed-callable-runtime-preflights-awaiting-audit-release-gate")
                        && runtimePreflightProbe.value("preflights").toArray().size() == 8
                        && !runtimePreflightProbe.value("operationInvokedByRuntimePreflight").toBool(true)
                        && !runtimePreflightProbe.value("inputBytesCaptured").toBool(true)
                        && !runtimePreflightProbe.value("outputBytesCaptured").toBool(true)
                        && !runtimePreflightProbe.value("resultCaptured").toBool(true)
                        && !runtimePreflightProbe.value("rawKeyExported").toBool(true)
                        && !runtimePreflightProbe.value("privateMaterialExported").toBool(true)
                        && !runtimePreflightProbe.value("sessionSecretExported").toBool(true),
                    "explicit reviewed runtime preflight probe should produce eight ready non-release preflights") && ok;
        const QJsonObject firstRuntimePreflight =
            runtimePreflightProbe.value("preflights").toArray().at(0).toObject();
        ok = expect(firstRuntimePreflight.value("operation").toString()
                            == QStringLiteral("session-key-generation")
                        && firstRuntimePreflight.value("runtimePreflightState").toString()
                            == QStringLiteral("ready-for-reviewed-provider-runtime-preflight")
                        && firstRuntimePreflight.value("runtimePreflightReady").toBool(false)
                        && firstRuntimePreflight.value("providerSymbol").toString()
                            == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                        && firstRuntimePreflight.value("inputContractHashSha256").toString().size() == 64
                        && firstRuntimePreflight.value("outputContractHashSha256").toString().size() == 64
                        && firstRuntimePreflight.value("fixtureHashSha256").toString().size() == 64
                        && firstRuntimePreflight.value("blockedReason").toString()
                            == QStringLiteral("production-provider-reviewed-callable-runtime-preflights-awaiting-audit-release-gate")
                        && !firstRuntimePreflight.value("operationInvokedByRuntimePreflight").toBool(true)
                        && !firstRuntimePreflight.value("inputBytesCaptured").toBool(true)
                        && !firstRuntimePreflight.value("outputBytesCaptured").toBool(true)
                        && !firstRuntimePreflight.value("resultCaptured").toBool(true),
                    "explicit reviewed runtime preflight probe should keep the first preflight sanitized") && ok;
        ok = expect(invocationArmingProbe.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-invocation-arming-v1")
                        && invocationArmingProbe.value("releaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-invocation-arming-not-release-gate")
                        && !invocationArmingProbe.value("accepted").toBool(true)
                        && invocationArmingProbe.value("invocationArmingNonReleaseGate").toBool(false)
                        && invocationArmingProbe.value("runtimePreflightSourceCaptured").toBool(false)
                        && invocationArmingProbe.value("providerReviewedCallableRuntimePreflightReadyCount").toInt() == 8
                        && invocationArmingProbe.value("armingCount").toInt() == 8
                        && invocationArmingProbe.value("readyArmingCount").toInt() == 8
                        && invocationArmingProbe.value("blockedArmingCount").toInt() == 0
                        && invocationArmingProbe.value("failClosedArmingCount").toInt() == 0
                        && invocationArmingProbe.value("runtimePreflightReadyArmingCount").toInt() == 8
                        && invocationArmingProbe.value("callbackEntryArmingCount").toInt() == 8
                        && invocationArmingProbe.value("sandboxPolicyArmingCount").toInt() == 8
                        && invocationArmingProbe.value("resultPolicyArmingCount").toInt() == 8
                        && invocationArmingProbe.value("sanitizedArmingCount").toInt() == 8
                        && invocationArmingProbe.value("blockedReason").toString()
                            == QStringLiteral("production-provider-reviewed-invocation-armings-awaiting-audit-release-gate")
                        && invocationArmingProbe.value("armings").toArray().size() == 8
                        && !invocationArmingProbe.value("operationInvokedByArming").toBool(true)
                        && !invocationArmingProbe.value("inputBytesCaptured").toBool(true)
                        && !invocationArmingProbe.value("outputBytesCaptured").toBool(true)
                        && !invocationArmingProbe.value("resultCaptured").toBool(true)
                        && !invocationArmingProbe.value("rawKeyExported").toBool(true)
                        && !invocationArmingProbe.value("privateMaterialExported").toBool(true)
                        && !invocationArmingProbe.value("sessionSecretExported").toBool(true),
                    "explicit reviewed invocation arming probe should produce eight ready non-release armings") && ok;
        ok = expect(executionAcceptanceProbe.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-invocation-execution-acceptance-v1")
                        && executionAcceptanceProbe.value("releaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-not-release-gate")
                        && !executionAcceptanceProbe.value("accepted").toBool(true)
                        && executionAcceptanceProbe.value("executionAcceptanceNonReleaseGate").toBool(false)
                        && executionAcceptanceProbe.value("armingSourceCaptured").toBool(false)
                        && executionAcceptanceProbe.value("providerReviewedInvocationArmingReadyCount").toInt() == 8
                        && executionAcceptanceProbe.value("acceptanceCount").toInt() == 8
                        && executionAcceptanceProbe.value("readyAcceptanceCount").toInt() == 8
                        && executionAcceptanceProbe.value("blockedAcceptanceCount").toInt() == 0
                        && executionAcceptanceProbe.value("failClosedAcceptanceCount").toInt() == 0
                        && executionAcceptanceProbe.value("armingReadyAcceptanceCount").toInt() == 8
                        && executionAcceptanceProbe.value("executionContractAcceptanceCount").toInt() == 8
                        && executionAcceptanceProbe.value("vectorEvidenceAcceptanceCount").toInt() == 8
                        && executionAcceptanceProbe.value("resultPolicyAcceptanceCount").toInt() == 8
                        && executionAcceptanceProbe.value("sanitizedAcceptanceCount").toInt() == 8
                        && executionAcceptanceProbe.value("blockedReason").toString()
                            == QStringLiteral("production-provider-reviewed-invocation-execution-acceptances-awaiting-audit-release-gate")
                        && executionAcceptanceProbe.value("acceptances").toArray().size() == 8
                        && !executionAcceptanceProbe.value("operationInvokedByExecutionAcceptance").toBool(true)
                        && !executionAcceptanceProbe.value("inputBytesCaptured").toBool(true)
                        && !executionAcceptanceProbe.value("outputBytesCaptured").toBool(true)
                        && !executionAcceptanceProbe.value("resultCaptured").toBool(true)
                        && !executionAcceptanceProbe.value("rawKeyExported").toBool(true)
                        && !executionAcceptanceProbe.value("privateMaterialExported").toBool(true)
                        && !executionAcceptanceProbe.value("sessionSecretExported").toBool(true),
                    "explicit reviewed execution acceptance probe should produce eight ready non-release acceptances") && ok;
        const QJsonObject firstExecutionAcceptance =
            executionAcceptanceProbe.value("acceptances").toArray().at(0).toObject();
        ok = expect(firstExecutionAcceptance.value("operation").toString()
                            == QStringLiteral("session-key-generation")
                        && firstExecutionAcceptance.value("executionAcceptanceState").toString()
                            == QStringLiteral("ready-for-reviewed-provider-invocation-execution-acceptance")
                        && firstExecutionAcceptance.value("executionAcceptanceReady").toBool(false)
                        && firstExecutionAcceptance.value("expectedVectorResultClass").toString()
                            == QStringLiteral("probe-vector-passed")
                        && firstExecutionAcceptance.value("expectedFailureClass").toString()
                            == QStringLiteral("none")
                        && firstExecutionAcceptance.value("expectedStatusClass").toString()
                            == QStringLiteral("ok")
                        && firstExecutionAcceptance.value("blockedReason").toString()
                            == QStringLiteral("production-provider-reviewed-invocation-execution-acceptances-awaiting-audit-release-gate")
                        && !firstExecutionAcceptance.value("operationInvokedByExecutionAcceptance").toBool(true)
                        && !firstExecutionAcceptance.value("inputBytesCaptured").toBool(true)
                        && !firstExecutionAcceptance.value("outputBytesCaptured").toBool(true)
                        && !firstExecutionAcceptance.value("resultCaptured").toBool(true),
                    "explicit reviewed execution acceptance probe should keep the first acceptance non-invoking") && ok;
        ok = expect(dataPlaneBridgeProbe.value("schema").toString()
                            == QStringLiteral("qtnetworkchat-e2e-production-provider-data-plane-bridge-v1")
                        && dataPlaneBridgeProbe.value("releaseGate").toString()
                            == QStringLiteral("production-provider-data-plane-bridge-not-release-gate")
                        && !dataPlaneBridgeProbe.value("accepted").toBool(true)
                        && dataPlaneBridgeProbe.value("dataPlaneBridgeNonReleaseGate").toBool(false)
                        && dataPlaneBridgeProbe.value("acceptanceSourceCaptured").toBool(false)
                        && dataPlaneBridgeProbe.value("providerReviewedInvocationExecutionAcceptanceReadyCount").toInt() == 8
                        && dataPlaneBridgeProbe.value("bridgeCount").toInt() == 8
                        && dataPlaneBridgeProbe.value("readyBridgeCount").toInt() == 8
                        && dataPlaneBridgeProbe.value("blockedBridgeCount").toInt() == 0
                        && dataPlaneBridgeProbe.value("publicApiMappingCount").toInt() == 8
                        && dataPlaneBridgeProbe.value("callbackMappingCount").toInt() == 8
                        && dataPlaneBridgeProbe.value("contractBridgeCount").toInt() == 8
                        && dataPlaneBridgeProbe.value("sanitizedBridgeCount").toInt() == 8
                        && dataPlaneBridgeProbe.value("failClosedBridgeCount").toInt() == 0
                        && dataPlaneBridgeProbe.value("executionAcceptanceReadyBridgeCount").toInt() == 8
                        && dataPlaneBridgeProbe.value("blockedReason").toString()
                            == QStringLiteral("production-provider-data-plane-bridges-awaiting-audit-release-gate")
                        && dataPlaneBridgeProbe.value("bridges").toArray().size() == 8
                        && !dataPlaneBridgeProbe.value("publicApiInvoked").toBool(true)
                        && !dataPlaneBridgeProbe.value("providerInvokedByBridge").toBool(true)
                        && !dataPlaneBridgeProbe.value("inputBytesCaptured").toBool(true)
                        && !dataPlaneBridgeProbe.value("outputBytesCaptured").toBool(true)
                        && !dataPlaneBridgeProbe.value("resultCaptured").toBool(true)
                        && !dataPlaneBridgeProbe.value("rawKeyExported").toBool(true)
                        && !dataPlaneBridgeProbe.value("privateMaterialExported").toBool(true)
                        && !dataPlaneBridgeProbe.value("sessionSecretExported").toBool(true),
                    "explicit production data-plane bridge probe should map eight public APIs without invoking them") && ok;
        const QJsonObject firstDataPlaneBridge =
            dataPlaneBridgeProbe.value("bridges").toArray().at(0).toObject();
        ok = expect(firstDataPlaneBridge.value("operation").toString()
                            == QStringLiteral("session-key-generation")
                        && firstDataPlaneBridge.value("publicApi").toString()
                            == QStringLiteral("generateE2ESessionKey")
                        && firstDataPlaneBridge.value("publicDataPlaneBoundary").toString()
                            == QStringLiteral("session-key-bootstrap")
                        && firstDataPlaneBridge.value("callbackEntrypoint").toString()
                            == QStringLiteral("qnc_e2e_provider_table_v1/qnc_e2e_op_session_key_generation_v1")
                        && firstDataPlaneBridge.value("bridgeState").toString()
                            == QStringLiteral("ready-for-reviewed-production-data-plane-bridge")
                        && firstDataPlaneBridge.value("dataPlaneBridgeReady").toBool(false)
                        && firstDataPlaneBridge.value("publicApiMapped").toBool(false)
                        && firstDataPlaneBridge.value("callbackMapped").toBool(false)
                        && firstDataPlaneBridge.value("contractReady").toBool(false)
                        && firstDataPlaneBridge.value("bridgeSanitized").toBool(false)
                        && firstDataPlaneBridge.value("blockedReason").toString()
                            == QStringLiteral("production-provider-data-plane-bridges-awaiting-audit-release-gate")
                        && !firstDataPlaneBridge.value("publicApiInvoked").toBool(true)
                        && !firstDataPlaneBridge.value("providerInvokedByBridge").toBool(true)
                        && !firstDataPlaneBridge.value("inputBytesCaptured").toBool(true)
                        && !firstDataPlaneBridge.value("outputBytesCaptured").toBool(true)
                        && !firstDataPlaneBridge.value("resultCaptured").toBool(true),
                    "explicit data-plane bridge should keep the first public primitive mapped but non-invoking") && ok;
    }

    const QJsonObject roundTripExecutionProbe =
        e2eProbeProductionCryptoProviderRoundTripExecution();
    ok = expect(roundTripExecutionProbe.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-round-trip-execution-v1")
                    && roundTripExecutionProbe.value("releaseGate").toString()
                        == QStringLiteral("production-provider-round-trip-execution-not-release-gate")
                    && !roundTripExecutionProbe.value("accepted").toBool(true)
                    && roundTripExecutionProbe.value("roundTripNonReleaseGate").toBool(false)
                    && roundTripExecutionProbe.value("requiredOperationCount").toInt() == 8
                    && roundTripExecutionProbe.value("operations").toArray().size() == 8
                    && roundTripExecutionProbe.value("negativeChecks").toArray().size() == 7
                    && !roundTripExecutionProbe.value("inputBytesCaptured").toBool(true)
                    && !roundTripExecutionProbe.value("outputBytesCaptured").toBool(true)
                    && !roundTripExecutionProbe.value("resultBytesCaptured").toBool(true)
                    && !roundTripExecutionProbe.value("rawKeyExported").toBool(true)
                    && !roundTripExecutionProbe.value("privateMaterialExported").toBool(true)
                    && !roundTripExecutionProbe.value("sessionSecretExported").toBool(true)
                    && !roundTripExecutionProbe.value("privateIdentityMaterialExported").toBool(true)
                    && !roundTripExecutionProbe.value("plaintextExported").toBool(true)
                    && !roundTripExecutionProbe.value("ciphertextExported").toBool(true),
                "production provider round-trip execution probe should stay a sanitized non-release gate") && ok;
    if (reviewedProviderOperationsBound) {
        const QJsonObject firstRoundTripOperation =
            roundTripExecutionProbe.value("operations").toArray().at(0).toObject();
        const QJsonObject lastRoundTripOperation =
            roundTripExecutionProbe.value("operations").toArray().at(7).toObject();
        const QJsonObject firstNegativeCheck =
            roundTripExecutionProbe.value("negativeChecks").toArray().at(0).toObject();
        const QJsonObject malformedPublicDerivationCheck =
            roundTripExecutionProbe.value("negativeChecks").toArray().at(1).toObject();
        const QJsonObject malformedAgreementSignCheck =
            roundTripExecutionProbe.value("negativeChecks").toArray().at(2).toObject();
        const QJsonObject malformedAgreementVerifyCheck =
            roundTripExecutionProbe.value("negativeChecks").toArray().at(3).toObject();
        const QJsonObject malformedSessionDeriveCheck =
            roundTripExecutionProbe.value("negativeChecks").toArray().at(4).toObject();
        const QJsonObject payloadTamperCheck =
            roundTripExecutionProbe.value("negativeChecks").toArray().at(5).toObject();
        const QJsonObject malformedPayloadEncryptCheck =
            roundTripExecutionProbe.value("negativeChecks").toArray().at(6).toObject();
        ok = expect(roundTripExecutionProbe.value("providerTableRegistered").toBool(false)
                        && roundTripExecutionProbe.value("tableValidationAccepted").toBool(false)
                        && roundTripExecutionProbe.value("roundTripReady").toBool(false)
                        && roundTripExecutionProbe.value("roundTripPassed").toBool(false)
                        && roundTripExecutionProbe.value("invokedOperationCount").toInt() == 8
                        && roundTripExecutionProbe.value("readyOperationCount").toInt() == 8
                        && roundTripExecutionProbe.value("blockedOperationCount").toInt() == 0
                        && roundTripExecutionProbe.value("statusConsistentOperationCount").toInt() == 8
                        && roundTripExecutionProbe.value("sanitizedOperationCount").toInt() == 8
                        && roundTripExecutionProbe.value("materialPolicyMatchedCount").toInt() == 8
                        && roundTripExecutionProbe.value("negativeCheckCount").toInt() == 7
                        && roundTripExecutionProbe.value("negativeCheckPassCount").toInt() == 7
                        && roundTripExecutionProbe.value("tamperRejectedCount").toInt() == 7
                        && roundTripExecutionProbe.value("identityPublicDerivationMatched").toBool(false)
                        && roundTripExecutionProbe.value("agreementSignatureVerified").toBool(false)
                        && roundTripExecutionProbe.value("sessionDerivePassed").toBool(false)
                        && roundTripExecutionProbe.value("payloadRoundTripPassed").toBool(false)
                        && roundTripExecutionProbe.value("blockedReason").toString()
                            == QStringLiteral("production-provider-round-trip-execution-awaiting-audit-release-gate")
                        && firstRoundTripOperation.value("operation").toString()
                            == QStringLiteral("session-key-generation")
                        && firstRoundTripOperation.value("operationInvoked").toBool(false)
                        && firstRoundTripOperation.value("sealedOutputSize").toInt() == 32
                        && !firstRoundTripOperation.value("outputBytesCaptured").toBool(true)
                        && firstRoundTripOperation.value("roundTripStepPassed").toBool(false)
                        && lastRoundTripOperation.value("operation").toString()
                            == QStringLiteral("payload-decrypt")
                        && lastRoundTripOperation.value("operationInvoked").toBool(false)
                        && lastRoundTripOperation.value("publicOutputSize").toInt()
                            == QByteArray("round-trip-provider-payload", 27).size()
                        && lastRoundTripOperation.value("roundTripStepPassed").toBool(false)
                        && firstNegativeCheck.value("checkId").toString()
                            == QStringLiteral("agreement-verify-tamper")
                        && firstNegativeCheck.value("rejectedAsExpected").toBool(false)
                        && malformedPublicDerivationCheck.value("checkId").toString()
                            == QStringLiteral("public-key-derivation-malformed-handle")
                        && malformedPublicDerivationCheck.value("callbackStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPublicDerivationCheck.value("outputStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPublicDerivationCheck.value("rejectedAsExpected").toBool(false)
                        && malformedAgreementSignCheck.value("checkId").toString()
                            == QStringLiteral("agreement-sign-malformed-handle")
                        && malformedAgreementSignCheck.value("callbackStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedAgreementSignCheck.value("outputStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedAgreementSignCheck.value("rejectedAsExpected").toBool(false)
                        && malformedAgreementVerifyCheck.value("checkId").toString()
                            == QStringLiteral("agreement-verify-malformed-public-key")
                        && malformedAgreementVerifyCheck.value("callbackStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedAgreementVerifyCheck.value("outputStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedAgreementVerifyCheck.value("rejectedAsExpected").toBool(false)
                        && malformedSessionDeriveCheck.value("checkId").toString()
                            == QStringLiteral("session-derive-malformed-key")
                        && malformedSessionDeriveCheck.value("callbackStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedSessionDeriveCheck.value("outputStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedSessionDeriveCheck.value("rejectedAsExpected").toBool(false)
                        && payloadTamperCheck.value("checkId").toString()
                            == QStringLiteral("payload-decrypt-tamper")
                        && payloadTamperCheck.value("rejectedAsExpected").toBool(false)
                        && malformedPayloadEncryptCheck.value("checkId").toString()
                            == QStringLiteral("payload-encrypt-malformed-key")
                        && malformedPayloadEncryptCheck.value("callbackStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPayloadEncryptCheck.value("outputStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPayloadEncryptCheck.value("rejectedAsExpected").toBool(false),
                    "linked OpenSSL provider round-trip probe should prove the full callback chain without releasing production") && ok;
    } else {
        ok = expect(!roundTripExecutionProbe.value("providerTableRegistered").toBool(true)
                        && !roundTripExecutionProbe.value("tableValidationAccepted").toBool(true)
                        && !roundTripExecutionProbe.value("roundTripReady").toBool(true)
                        && !roundTripExecutionProbe.value("roundTripPassed").toBool(true)
                        && roundTripExecutionProbe.value("invokedOperationCount").toInt() == 0
                        && roundTripExecutionProbe.value("readyOperationCount").toInt() == 0
                        && roundTripExecutionProbe.value("blockedOperationCount").toInt() == 8
                        && roundTripExecutionProbe.value("negativeCheckCount").toInt() == 7
                        && roundTripExecutionProbe.value("negativeCheckPassCount").toInt() == 0
                        && roundTripExecutionProbe.value("blockedReason").toString()
                            == QStringLiteral("production-provider-table-not-registered"),
                    "not-linked production provider round-trip probe should stay fully blocked") && ok;
    }

    const QJsonObject publicPrimitiveProbe =
        e2eProbeProductionCryptoProviderPublicPrimitiveExecution();
    ok = expect(publicPrimitiveProbe.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-public-primitive-execution-probe-v1")
                    && publicPrimitiveProbe.value("releaseGate").toString()
                        == QStringLiteral("production-public-primitive-execution-probe-not-release-gate")
                    && !publicPrimitiveProbe.value("accepted").toBool(true)
                    && publicPrimitiveProbe.value("publicPrimitiveExecutionNonReleaseGate").toBool(false)
                    && publicPrimitiveProbe.value("requiredOperationCount").toInt() == 8
                    && publicPrimitiveProbe.value("operations").toArray().size() == 8
                    && publicPrimitiveProbe.value("negativeChecks").toArray().size() == 7
                    && !publicPrimitiveProbe.value("publicApiInvoked").toBool(true)
                    && !publicPrimitiveProbe.value("inputBytesCaptured").toBool(true)
                    && !publicPrimitiveProbe.value("outputBytesCaptured").toBool(true)
                    && !publicPrimitiveProbe.value("resultBytesCaptured").toBool(true)
                    && !publicPrimitiveProbe.value("rawKeyExported").toBool(true)
                    && !publicPrimitiveProbe.value("privateMaterialExported").toBool(true)
                    && !publicPrimitiveProbe.value("sessionSecretExported").toBool(true)
                    && !publicPrimitiveProbe.value("privateIdentityMaterialExported").toBool(true)
                    && !publicPrimitiveProbe.value("plaintextExported").toBool(true)
                    && !publicPrimitiveProbe.value("ciphertextExported").toBool(true),
                "production public primitive execution probe should stay a sanitized non-release gate") && ok;
    if (reviewedProviderOperationsBound) {
        const QJsonObject firstPublicPrimitiveOperation =
            publicPrimitiveProbe.value("operations").toArray().at(0).toObject();
        const QJsonObject lastPublicPrimitiveOperation =
            publicPrimitiveProbe.value("operations").toArray().at(7).toObject();
        const QJsonObject firstPublicPrimitiveNegative =
            publicPrimitiveProbe.value("negativeChecks").toArray().at(0).toObject();
        const QJsonObject malformedPublicPrimitiveDerivation =
            publicPrimitiveProbe.value("negativeChecks").toArray().at(1).toObject();
        const QJsonObject malformedPublicPrimitiveSign =
            publicPrimitiveProbe.value("negativeChecks").toArray().at(2).toObject();
        const QJsonObject malformedPublicPrimitiveVerify =
            publicPrimitiveProbe.value("negativeChecks").toArray().at(3).toObject();
        const QJsonObject malformedPublicPrimitiveSessionDerive =
            publicPrimitiveProbe.value("negativeChecks").toArray().at(4).toObject();
        const QJsonObject publicPrimitivePayloadTamper =
            publicPrimitiveProbe.value("negativeChecks").toArray().at(5).toObject();
        const QJsonObject malformedPublicPrimitiveEncrypt =
            publicPrimitiveProbe.value("negativeChecks").toArray().at(6).toObject();
        ok = expect(publicPrimitiveProbe.value("providerTableRegistered").toBool(false)
                        && publicPrimitiveProbe.value("tableValidationAccepted").toBool(false)
                        && publicPrimitiveProbe.value("providerDataPlaneBridgeReady").toBool(false)
                        && publicPrimitiveProbe.value("publicPrimitiveReady").toBool(false)
                        && publicPrimitiveProbe.value("publicPrimitivePassed").toBool(false)
                        && publicPrimitiveProbe.value("invokedOperationCount").toInt() == 8
                        && publicPrimitiveProbe.value("readyOperationCount").toInt() == 8
                        && publicPrimitiveProbe.value("blockedOperationCount").toInt() == 0
                        && publicPrimitiveProbe.value("publicApiMappedOperationCount").toInt() == 8
                        && publicPrimitiveProbe.value("bridgeReadyOperationCount").toInt() == 8
                        && publicPrimitiveProbe.value("statusConsistentOperationCount").toInt() == 8
                        && publicPrimitiveProbe.value("sanitizedOperationCount").toInt() == 8
                        && publicPrimitiveProbe.value("materialPolicyMatchedCount").toInt() == 8
                        && publicPrimitiveProbe.value("outputShapeHashCount").toInt() == 8
                        && publicPrimitiveProbe.value("negativeCheckCount").toInt() == 7
                        && publicPrimitiveProbe.value("negativeCheckPassCount").toInt() == 7
                        && publicPrimitiveProbe.value("tamperRejectedCount").toInt() == 7
                        && publicPrimitiveProbe.value("identityPublicDerivationMatched").toBool(false)
                        && publicPrimitiveProbe.value("agreementSignatureVerified").toBool(false)
                        && publicPrimitiveProbe.value("sessionDerivePassed").toBool(false)
                        && publicPrimitiveProbe.value("payloadRoundTripPassed").toBool(false)
                        && publicPrimitiveProbe.value("blockedReason").toString()
                            == QStringLiteral("production-public-primitive-execution-awaiting-audit-release-gate")
                        && firstPublicPrimitiveOperation.value("operation").toString()
                            == QStringLiteral("session-key-generation")
                        && firstPublicPrimitiveOperation.value("publicApi").toString()
                            == QStringLiteral("generateE2ESessionKey")
                        && firstPublicPrimitiveOperation.value("operationInvoked").toBool(false)
                        && firstPublicPrimitiveOperation.value("providerInvokedByPublicPrimitive").toBool(false)
                        && firstPublicPrimitiveOperation.value("sealedOutputSize").toInt() == 32
                        && firstPublicPrimitiveOperation.value("outputShapeHashSha256").toString().size() == 64
                        && !firstPublicPrimitiveOperation.value("publicApiInvoked").toBool(true)
                        && !firstPublicPrimitiveOperation.value("outputBytesCaptured").toBool(true)
                        && firstPublicPrimitiveOperation.value("publicPrimitiveStepPassed").toBool(false)
                        && lastPublicPrimitiveOperation.value("operation").toString()
                            == QStringLiteral("payload-decrypt")
                        && lastPublicPrimitiveOperation.value("publicApi").toString()
                            == QStringLiteral("decryptE2EPayload")
                        && lastPublicPrimitiveOperation.value("operationInvoked").toBool(false)
                        && lastPublicPrimitiveOperation.value("publicOutputSize").toInt()
                            == QByteArray("public-primitive-provider-payload", 33).size()
                        && lastPublicPrimitiveOperation.value("publicPrimitiveStepPassed").toBool(false)
                        && firstPublicPrimitiveNegative.value("checkId").toString()
                            == QStringLiteral("public-agreement-verify-tamper")
                        && firstPublicPrimitiveNegative.value("rejectedAsExpected").toBool(false)
                        && malformedPublicPrimitiveDerivation.value("checkId").toString()
                            == QStringLiteral("public-key-derivation-malformed-handle")
                        && malformedPublicPrimitiveDerivation.value("callbackStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPublicPrimitiveDerivation.value("outputStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPublicPrimitiveDerivation.value("rejectedAsExpected").toBool(false)
                        && malformedPublicPrimitiveSign.value("checkId").toString()
                            == QStringLiteral("public-agreement-sign-malformed-handle")
                        && malformedPublicPrimitiveSign.value("callbackStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPublicPrimitiveSign.value("outputStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPublicPrimitiveSign.value("rejectedAsExpected").toBool(false)
                        && malformedPublicPrimitiveVerify.value("checkId").toString()
                            == QStringLiteral("public-agreement-verify-malformed-public-key")
                        && malformedPublicPrimitiveVerify.value("callbackStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPublicPrimitiveVerify.value("outputStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPublicPrimitiveVerify.value("rejectedAsExpected").toBool(false)
                        && malformedPublicPrimitiveSessionDerive.value("checkId").toString()
                            == QStringLiteral("public-session-derive-malformed-key")
                        && malformedPublicPrimitiveSessionDerive.value("callbackStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPublicPrimitiveSessionDerive.value("outputStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPublicPrimitiveSessionDerive.value("rejectedAsExpected").toBool(false)
                        && publicPrimitivePayloadTamper.value("checkId").toString()
                            == QStringLiteral("public-payload-decrypt-tamper")
                        && publicPrimitivePayloadTamper.value("rejectedAsExpected").toBool(false)
                        && malformedPublicPrimitiveEncrypt.value("checkId").toString()
                            == QStringLiteral("public-payload-encrypt-malformed-key")
                        && malformedPublicPrimitiveEncrypt.value("callbackStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPublicPrimitiveEncrypt.value("outputStatusClass").toString()
                            == QStringLiteral("invalid-input")
                        && malformedPublicPrimitiveEncrypt.value("rejectedAsExpected").toBool(false),
                    "linked OpenSSL provider public primitive probe should exercise all public primitive callbacks without releasing production") && ok;
    } else {
        ok = expect(!publicPrimitiveProbe.value("providerTableRegistered").toBool(true)
                        && !publicPrimitiveProbe.value("tableValidationAccepted").toBool(true)
                        && !publicPrimitiveProbe.value("providerDataPlaneBridgeReady").toBool(true)
                        && !publicPrimitiveProbe.value("publicPrimitiveReady").toBool(true)
                        && !publicPrimitiveProbe.value("publicPrimitivePassed").toBool(true)
                        && publicPrimitiveProbe.value("invokedOperationCount").toInt() == 0
                        && publicPrimitiveProbe.value("readyOperationCount").toInt() == 0
                        && publicPrimitiveProbe.value("blockedOperationCount").toInt() == 8
                        && publicPrimitiveProbe.value("negativeCheckCount").toInt() == 7
                        && publicPrimitiveProbe.value("negativeCheckPassCount").toInt() == 0
                        && publicPrimitiveProbe.value("blockedReason").toString()
                            == QStringLiteral("production-provider-table-not-registered"),
                    "not-linked production public primitive probe should stay fully blocked") && ok;
    }

    const E2EEnvelope envelope = encryptE2EPayload(QStringLiteral("10001"),
                                                   QStringLiteral("10002"),
                                                   QStringLiteral("production-runtime-gate"),
                                                   QByteArray(32, '\x11'),
                                                   QByteArray("payload", 7),
                                                   QStringLiteral("runtime-gate"),
                                                   &reason);
    if (reviewedProviderOperationsBound) {
        QByteArray decryptedPayload;
        ok = expect(envelope.isValid()
                        && reason.isEmpty()
                        && decryptE2EPayload(envelope,
                                             QByteArray(32, '\x11'),
                                             &decryptedPayload,
                                             &reason)
                        && decryptedPayload == QByteArray("payload", 7)
                        && reason.isEmpty(),
                    "production adapter runtime should encrypt and decrypt payloads through OpenSSL provider") && ok;
        E2EEnvelope tamperedEnvelope = envelope;
        if (!tamperedEnvelope.ciphertext.isEmpty()) {
            tamperedEnvelope.ciphertext[0] =
                static_cast<char>(tamperedEnvelope.ciphertext.at(0) ^ 0x01);
        }
        ok = expect(!decryptE2EPayload(tamperedEnvelope,
                                       QByteArray(32, '\x11'),
                                       &decryptedPayload,
                                       &reason)
                        && reason == QStringLiteral("payload-decrypt-failed"),
                    "production adapter runtime should reject tampered payloads") && ok;
    } else {
        ok = expect(!envelope.isValid()
                        && reason == expectedReason,
                    "production adapter runtime should block payload encryption at the shared execution context") && ok;
    }

    qunsetenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND");
    return ok ? 0 : 1;
}
