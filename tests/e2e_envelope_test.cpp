#include "e2eenvelope.h"
#include "qtnetworkchat_e2e_provider_api.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning() << message;
        return false;
    }
    return true;
}

bool expectOperation(const QJsonObject& backendStatus,
                     const QString& operation,
                     bool available,
                     const QString& reason,
                     const char* message) {
    const QJsonObject operations = backendStatus.value("operations").toObject();
    const QJsonObject status = operations.value(operation).toObject();
    return expect(status.value("operation").toString() == operation
                      && status.value("available").toBool(!available) == available
                      && status.value("reason").toString() == reason
                      && status.value("entrypoint").toString().endsWith(QStringLiteral("/") + operation)
                      && status.value("blockedReason").toString() == (available ? QString() : reason)
                      && status.value("operationContractVersion").toString()
                          == QStringLiteral("qtnetworkchat-e2e-crypto-ops-v1")
                      && !status.value("rawKeyExported").toBool(true)
                      && !status.value("privateMaterialExported").toBool(true),
                  message);
}

bool expectAllOperations(const QJsonObject& backendStatus,
                         bool available,
                         const QString& reason,
                         const char* message) {
    const QStringList names = {
        QStringLiteral("session-key-generation"),
        QStringLiteral("identity-key-generation"),
        QStringLiteral("public-key-derivation"),
        QStringLiteral("agreement-sign"),
        QStringLiteral("agreement-verify"),
        QStringLiteral("session-derive"),
        QStringLiteral("payload-encrypt"),
        QStringLiteral("payload-decrypt"),
    };
    for (const QString& name : names) {
        if (!expectOperation(backendStatus, name, available, reason, message)) {
            return false;
        }
    }
    return true;
}

qnc_e2e_status_t dummyProviderOperation(const qnc_e2e_operation_input_v1*,
                                        qnc_e2e_operation_output_v1*) {
    return QNC_E2E_STATUS_UNSUPPORTED;
}

int g_probeInvocationCount = 0;

qnc_e2e_status_t countingProviderOperation(const qnc_e2e_operation_input_v1* input,
                                           qnc_e2e_operation_output_v1* output) {
    if (!input || !output) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }
    if (input->operation < QNC_E2E_OPERATION_SESSION_KEY_GENERATION
        || input->operation > QNC_E2E_OPERATION_PAYLOAD_DECRYPT
        || !input->suite_id) {
        output->status = QNC_E2E_STATUS_INVALID_INPUT;
        output->sanitized_error_class = "invalid-input";
        return QNC_E2E_STATUS_INVALID_INPUT;
    }
    ++g_probeInvocationCount;
    static const uint8_t publicOutputBytes[] = { 0x51, 0x4e, 0x43, 0x2d };
    static const uint8_t sealedOutputBytes[] = { 0x45, 0x32, 0x45, 0x2d, 0x01 };
    static const uint8_t payloadOutputBytes[] = { 0x50, 0x4c, 0x41, 0x49, 0x4e };
    output->status = QNC_E2E_STATUS_OK;
    output->material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
    output->public_output = {};
    output->sealed_output = {};
    if (input->operation == QNC_E2E_OPERATION_IDENTITY_KEY_GENERATION
        || input->operation == QNC_E2E_OPERATION_PUBLIC_KEY_DERIVATION
        || input->operation == QNC_E2E_OPERATION_AGREEMENT_SIGN
        || input->operation == QNC_E2E_OPERATION_AGREEMENT_VERIFY) {
        output->material_policy = QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED;
        output->public_output = { publicOutputBytes, sizeof(publicOutputBytes) };
    } else if (input->operation == QNC_E2E_OPERATION_PAYLOAD_ENCRYPT) {
        output->material_policy = QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED;
        output->sealed_output = { sealedOutputBytes, sizeof(sealedOutputBytes) };
    } else if (input->operation == QNC_E2E_OPERATION_PAYLOAD_DECRYPT) {
        output->material_policy = QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED;
        output->public_output = { payloadOutputBytes, sizeof(payloadOutputBytes) };
    }
    output->sanitized_error_class = "ok";
    return QNC_E2E_STATUS_OK;
}

qnc_e2e_status_t mismatchedShapeProviderOperation(const qnc_e2e_operation_input_v1* input,
                                                  qnc_e2e_operation_output_v1* output) {
    if (!input || !output) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }
    ++g_probeInvocationCount;
    static const uint8_t wrongShapeBytes[] = { 0x4d, 0x49, 0x53, 0x4d };
    output->status = QNC_E2E_STATUS_OK;
    output->material_policy = QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED;
    output->public_output = { wrongShapeBytes, sizeof(wrongShapeBytes) };
    output->sealed_output = {};
    output->sanitized_error_class = "ok";
    return QNC_E2E_STATUS_OK;
}

qnc_e2e_status_t statusMismatchProviderOperation(const qnc_e2e_operation_input_v1* input,
                                                 qnc_e2e_operation_output_v1* output) {
    if (!input || !output) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }
    ++g_probeInvocationCount;
    output->status = QNC_E2E_STATUS_UNSUPPORTED;
    output->material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
    output->public_output = {};
    output->sealed_output = {};
    output->sanitized_error_class = "unsupported";
    return QNC_E2E_STATUS_OK;
}

qnc_e2e_status_t materialPolicyMismatchProviderOperation(const qnc_e2e_operation_input_v1* input,
                                                         qnc_e2e_operation_output_v1* output) {
    if (!input || !output) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }
    ++g_probeInvocationCount;
    static const uint8_t publicOutputBytes[] = { 0x50, 0x55, 0x42, 0x2d };
    static const uint8_t sealedOutputBytes[] = { 0x53, 0x45, 0x41, 0x4c };
    static const uint8_t payloadOutputBytes[] = { 0x50, 0x41, 0x59, 0x4c };
    output->status = QNC_E2E_STATUS_OK;
    output->material_policy = QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED;
    output->public_output = {};
    output->sealed_output = {};
    if (input->operation == QNC_E2E_OPERATION_IDENTITY_KEY_GENERATION
        || input->operation == QNC_E2E_OPERATION_PUBLIC_KEY_DERIVATION
        || input->operation == QNC_E2E_OPERATION_AGREEMENT_SIGN
        || input->operation == QNC_E2E_OPERATION_AGREEMENT_VERIFY) {
        output->public_output = { publicOutputBytes, sizeof(publicOutputBytes) };
    } else if (input->operation == QNC_E2E_OPERATION_PAYLOAD_ENCRYPT) {
        output->sealed_output = { sealedOutputBytes, sizeof(sealedOutputBytes) };
    } else if (input->operation == QNC_E2E_OPERATION_PAYLOAD_DECRYPT) {
        output->public_output = { payloadOutputBytes, sizeof(payloadOutputBytes) };
    }
    output->sanitized_error_class = "ok";
    return QNC_E2E_STATUS_OK;
}
}

int main() {
    bool ok = true;

    E2EKeyAgreement agreement;
    agreement.protocol = " QtNetworkChat-E2E-V1 ";
    agreement.suite = "X25519-HKDF-SHA256-AES-256-GCM";
    agreement.senderId = "10001";
    agreement.receiverId = "10002";
    agreement.keyId = "alice-bob-1";
    agreement.publicKey = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    agreement.senderIdentityFingerprint = QString(64, QLatin1Char('a'));
    agreement.receiverIdentityFingerprint = QString(64, QLatin1Char('b'));
    agreement.signature = QByteArray::fromHex("aabbccdd");

    QString reason;
    ok = expect(agreement.isValid(&reason) && reason.isEmpty(),
                "valid key agreement should pass") && ok;
    const QJsonObject backendStatus = e2eCryptoBackendStatus();
    const QJsonArray registeredBackends = backendStatus.value("registeredBackends").toArray();
    const QJsonObject draftBackend = registeredBackends.at(0).toObject();
    const QJsonObject productionBackend = registeredBackends.at(1).toObject();
    const QJsonObject productionAcceptance = backendStatus.value("productionAcceptance").toObject();
    const QJsonObject productionHarness = backendStatus.value("productionOperationHarness").toObject();
    const QJsonObject productionExecutionPlan =
        backendStatus.value("productionOperationExecutionPlan").toObject();
    const QJsonObject productionInvocation =
        backendStatus.value("productionOperationInvocation").toObject();
    const QJsonObject productionSlots =
        backendStatus.value("productionOperationSlots").toObject();
    const QJsonObject productionDispatchBindings =
        backendStatus.value("productionOperationDispatchBindings").toObject();
    const QJsonObject productionCallableManifest =
        backendStatus.value("productionOperationCallableManifest").toObject();
    const QJsonObject productionExecutionResult =
        backendStatus.value("productionOperationExecutionResult").toObject();
    const QJsonObject productionProviderTable =
        backendStatus.value("productionProviderTable").toObject();
    const QJsonObject productionProviderTableBindingProbe =
        backendStatus.value("productionProviderTableBindingProbe").toObject();
    const QJsonObject productionProviderTableRegistration =
        backendStatus.value("productionProviderTableRegistration").toObject();
    const QJsonObject productionProviderOperationPreflight =
        backendStatus.value("productionProviderOperationPreflight").toObject();
    const QJsonObject productionProviderCallFrame =
        backendStatus.value("productionProviderCallFrame").toObject();
    const QJsonObject productionProviderInvocationDryRun =
        backendStatus.value("productionProviderInvocationDryRun").toObject();
    const QJsonObject productionProviderInvocationResult =
        backendStatus.value("productionProviderInvocationResult").toObject();
    const QJsonObject productionProviderExecutionDecision =
        backendStatus.value("productionProviderExecutionDecision").toObject();
    const QJsonObject productionProviderCallbackHarness =
        backendStatus.value("productionProviderCallbackHarness").toObject();
    const QJsonObject productionProviderVectorSelfTest =
        backendStatus.value("productionProviderVectorSelfTest").toObject();
    const QJsonObject productionProviderExecutionSlotBinding =
        backendStatus.value("productionProviderExecutionSlotBinding").toObject();
    const QJsonObject productionProviderExecutionPath =
        backendStatus.value("productionProviderExecutionPath").toObject();
    const QJsonObject productionProviderInvocationSandbox =
        backendStatus.value("productionProviderInvocationSandbox").toObject();
    const QJsonObject productionProviderInvocationVectorResult =
        backendStatus.value("productionProviderInvocationVectorResult").toObject();
    const QJsonObject productionProviderInvocationExecution =
        backendStatus.value("productionProviderInvocationExecution").toObject();
    ok = expect(backendStatus.value("backendId").toString() == QStringLiteral("draft-qt-hmac-stream-v1")
                    && backendStatus.value("compiledBackendId").toString() == QStringLiteral("draft-qt-hmac-stream-v1")
                    && backendStatus.value("requestedBackendId").toString() == QStringLiteral("draft-qt-hmac-stream-v1")
                    && backendStatus.value("selectedBackendId").toString() == QStringLiteral("draft-qt-hmac-stream-v1")
                    && backendStatus.value("selectionSource").toString() == QStringLiteral("compiled-default")
                    && backendStatus.value("fallbackBackendId").toString() == QStringLiteral("draft-qt-hmac-stream-v1")
                    && registeredBackends.size() == 2
                    && backendStatus.value("suite").toString() == e2eDefaultSuite()
                    && backendStatus.value("signatureSuite").toString() == e2eAgreementSignatureSuite()
                    && !backendStatus.value("productionReady").toBool(true)
                    && !backendStatus.value("productionAdapterRequested").toBool(true)
                    && !backendStatus.value("productionAdapterLinked").toBool(true)
                    && backendStatus.value("productionAdapterReason").toString()
                        == QStringLiteral("production-adapter-not-requested")
                    && backendStatus.value("productionRequiredOperations").toString().contains(QStringLiteral("payload-decrypt"))
                    && !backendStatus.value("productionBackendRequestedAtBuild").toBool(true)
                    && !backendStatus.value("productionBackendAvailableAtBuild").toBool(true)
                    && backendStatus.value("productionBackendReason").toString()
                        == QStringLiteral("production-backend-not-requested")
                    && backendStatus.value("selectedProviderReadiness").toObject()
                        .value("providerId").toString() == QStringLiteral("draft-qt-provider-v1")
                    && backendStatus.value("selectedProviderReadiness").toObject()
                        .value("selfTestStatus").toString() == QStringLiteral("development-only-self-test-passed")
                    && backendStatus.value("selectedProviderCompatibility").toObject()
                        .value("status").toString() == QStringLiteral("development-known-answer-passed")
                    && backendStatus.value("selectedProviderCompatibility").toObject()
                        .value("knownAnswerPassed").toBool(false)
                    && productionAcceptance.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-crypto-acceptance-v1")
                    && !productionAcceptance.value("accepted").toBool(true)
                    && productionAcceptance.value("releaseGate").toString()
                        == QStringLiteral("production-adapter-not-linked")
                    && productionAcceptance.value("blockedReason").toString()
                        == QStringLiteral("production-crypto-backend-unavailable")
                    && productionAcceptance.value("blockedOperationCount").toInt() == 8
                    && productionAcceptance.value("operationGates").toArray().size() == 8
                    && productionAcceptance.value("operationManifest").toArray().size() == 8
                    && productionAcceptance.value("operationManifestComplete").toBool(false)
                    && productionHarness.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-harness-v1")
                    && productionHarness.value("releaseGate").toString()
                        == QStringLiteral("production-operation-harness-blocked-not-linked")
                    && productionHarness.value("blockedOperationCount").toInt() == 8
                    && productionHarness.value("operations").toArray().size() == 8
                    && productionExecutionPlan.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-execution-plan-v1")
                    && productionExecutionPlan.value("releaseGate").toString()
                        == QStringLiteral("production-operation-execution-plan-blocked-not-linked")
                    && productionExecutionPlan.value("blockedStepCount").toInt() == 8
                    && productionExecutionPlan.value("steps").toArray().size() == 8
                    && productionInvocation.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-invocation-v1")
                    && productionInvocation.value("releaseGate").toString()
                        == QStringLiteral("production-operation-invocation-blocked-not-linked")
                    && productionInvocation.value("blockedOperationCount").toInt() == 8
                    && productionInvocation.value("invocations").toArray().size() == 8
                    && productionSlots.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-slots-v1")
                    && productionSlots.value("releaseGate").toString()
                        == QStringLiteral("production-operation-slots-blocked-not-linked")
                    && productionSlots.value("blockedSlotCount").toInt() == 8
                    && productionSlots.value("slots").toArray().size() == 8
                    && productionDispatchBindings.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-dispatch-bindings-v1")
                    && productionDispatchBindings.value("releaseGate").toString()
                        == QStringLiteral("production-operation-dispatch-bindings-blocked-not-linked")
                    && productionDispatchBindings.value("blockedBindingCount").toInt() == 8
                    && productionDispatchBindings.value("bindings").toArray().size() == 8
                    && productionCallableManifest.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-callable-manifest-v1")
                    && productionCallableManifest.value("releaseGate").toString()
                        == QStringLiteral("production-operation-callable-manifest-blocked-not-linked")
                    && productionCallableManifest.value("blockedCallableCount").toInt() == 8
                    && productionCallableManifest.value("callables").toArray().size() == 8
                    && productionExecutionResult.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-execution-result-v1")
                    && productionExecutionResult.value("releaseGate").toString()
                        == QStringLiteral("production-operation-execution-results-blocked-not-linked")
                    && productionExecutionResult.value("blockedResultCount").toInt() == 8
                    && productionExecutionResult.value("passedResultCount").toInt() == 0
                    && productionExecutionResult.value("sanitizedResultCount").toInt() == 8
                    && productionExecutionResult.value("results").toArray().size() == 8
                    && productionProviderTable.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-table-v1")
                    && productionProviderTable.value("releaseGate").toString()
                        == QStringLiteral("production-provider-table-blocked-not-linked")
                    && productionProviderTable.value("buildProbeReason").toString()
                        == QStringLiteral("production-provider-table-not-requested")
                    && productionProviderTable.value("providerApiHeader").toString()
                        == QStringLiteral("include/qtnetworkchat_e2e_provider_api.h")
                    && productionProviderTable.value("headerTableAbi").toString()
                        == QString::fromLatin1(QNC_E2E_PROVIDER_TABLE_ABI)
                    && productionProviderTable.value("abiMatchesHeader").toBool(false)
                    && productionProviderTable.value("headerOperationCount").toInt()
                        == QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT
                    && productionProviderTable.value("operationCountMatchesHeader").toBool(false)
                    && !productionProviderTable.value("tableBound").toBool(true)
                    && productionProviderTable.value("requiredSymbolCount").toInt() == 8
                    && productionProviderTable.value("boundSymbolCount").toInt() == 0
                    && productionProviderTable.value("missingSymbolCount").toInt() == 8
                    && productionProviderTable.value("entries").toArray().size() == 8
                    && productionProviderTable.value("bindingProbe").toObject()
                        .value("enumMappings").toArray().size() == 8
                    && productionProviderTableBindingProbe.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-table-binding-probe-v1")
                    && productionProviderTableBindingProbe.value("releaseGate").toString()
                        == QStringLiteral("production-provider-table-binding-blocked-not-linked")
                    && productionProviderTableBindingProbe.value("blockedReason").toString()
                        == QStringLiteral("production-provider-table-not-bound")
                    && productionProviderTableBindingProbe.value("headerLayoutComplete").toBool(false)
                    && productionProviderTableBindingProbe.value("enumMappingComplete").toBool(false)
                    && productionProviderTableBindingProbe.value("functionPointerSlotsComplete").toBool(false)
                    && productionProviderTableBindingProbe.value("tableValidation").toObject()
                        .value("blockedReason").toString()
                            == QStringLiteral("production-provider-table-not-bound")
                    && !productionProviderTableBindingProbe.value("tableValidationAccepted").toBool(true)
                    && !productionProviderTableBindingProbe.value("accepted").toBool(true)
                    && productionProviderTableBindingProbe.value("enumMappings").toArray().size() == 8
                    && productionProviderTableBindingProbe.value("fieldOffsets").toArray().size() == 5
                    && productionProviderTableRegistration.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-table-registration-v1")
                    && productionProviderTableRegistration.value("releaseGate").toString()
                        == QStringLiteral("production-provider-table-registration-blocked-not-linked")
                    && !productionProviderTableRegistration.value("registered").toBool(true)
                    && productionProviderTableRegistration.value("blockedReason").toString()
                        == QStringLiteral("production-provider-table-not-registered")
                    && productionProviderTableRegistration.value("tableValidation").toObject()
                        .value("blockedReason").toString()
                            == QStringLiteral("production-provider-table-not-bound")
                    && productionProviderOperationPreflight.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-operation-preflight-v1")
                    && productionProviderOperationPreflight.value("releaseGate").toString()
                        == QStringLiteral("production-provider-operation-preflight-blocked-not-linked")
                    && productionProviderOperationPreflight.value("blockedReason").toString()
                        == QStringLiteral("production-provider-table-not-registered")
                    && productionProviderOperationPreflight.value("blockedOperationCount").toInt() == 8
                    && productionProviderOperationPreflight.value("presentOperationCount").toInt() == 0
                    && productionProviderOperationPreflight.value("operations").toArray().size() == 8
                    && !productionProviderOperationPreflight.value("operationInvoked").toBool(true)
                    && productionProviderCallFrame.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-call-frame-v1")
                    && productionProviderCallFrame.value("releaseGate").toString()
                        == QStringLiteral("production-provider-call-frame-blocked-not-linked")
                    && productionProviderCallFrame.value("blockedFrameCount").toInt() == 8
                    && productionProviderCallFrame.value("readyFrameCount").toInt() == 0
                    && productionProviderCallFrame.value("sanitizedFrameCount").toInt() == 8
                    && productionProviderCallFrame.value("enumMatchedFrameCount").toInt() == 8
                    && productionProviderCallFrame.value("contractHashCount").toInt() == 8
                    && productionProviderCallFrame.value("frames").toArray().size() == 8
                    && !productionProviderCallFrame.value("operationInvoked").toBool(true)
                    && productionProviderInvocationDryRun.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-dry-run-v1")
                    && productionProviderInvocationDryRun.value("releaseGate").toString()
                        == QStringLiteral("production-provider-invocation-dry-run-blocked-not-linked")
                    && productionProviderInvocationDryRun.value("blockedInvocationCount").toInt() == 8
                    && productionProviderInvocationDryRun.value("dryRunReadyCount").toInt() == 0
                    && productionProviderInvocationDryRun.value("sanitizedInvocationCount").toInt() == 8
                    && productionProviderInvocationDryRun.value("invocations").toArray().size() == 8
                    && !productionProviderInvocationDryRun.value("operationInvoked").toBool(true)
                    && productionProviderInvocationResult.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-result-v1")
                    && productionProviderInvocationResult.value("releaseGate").toString()
                        == QStringLiteral("production-provider-invocation-results-blocked-not-linked")
                    && productionProviderInvocationResult.value("blockedResultCount").toInt() == 8
                    && productionProviderInvocationResult.value("captureReadyCount").toInt() == 0
                    && productionProviderInvocationResult.value("sanitizedResultCount").toInt() == 8
                    && productionProviderInvocationResult.value("outputContractProofCount").toInt() == 8
                    && productionProviderInvocationResult.value("fixtureProofCount").toInt() == 8
                    && productionProviderInvocationResult.value("materialExportProofCount").toInt() == 8
                    && productionProviderInvocationResult.value("results").toArray().size() == 8
                    && !productionProviderInvocationResult.value("operationInvoked").toBool(true)
                    && !productionProviderInvocationResult.value("resultCaptured").toBool(true)
                    && productionProviderExecutionDecision.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-execution-decision-v1")
                    && productionProviderExecutionDecision.value("releaseGate").toString()
                        == QStringLiteral("production-provider-execution-decision-blocked-not-linked")
                    && productionProviderExecutionDecision.value("blockedDecisionCount").toInt() == 8
                    && productionProviderExecutionDecision.value("allowedDecisionCount").toInt() == 0
                    && productionProviderExecutionDecision.value("sanitizedDecisionCount").toInt() == 8
                    && productionProviderExecutionDecision.value("decisions").toArray().size() == 8
                    && !productionProviderExecutionDecision.value("operationInvoked").toBool(true)
                    && !productionProviderExecutionDecision.value("resultCaptured").toBool(true)
                    && productionProviderCallbackHarness.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-callback-harness-v1")
                    && productionProviderCallbackHarness.value("releaseGate").toString()
                        == QStringLiteral("production-provider-callback-harness-blocked-not-linked")
                    && productionProviderCallbackHarness.value("blockedCallbackCount").toInt() == 8
                    && productionProviderCallbackHarness.value("armedCallbackCount").toInt() == 0
                    && productionProviderCallbackHarness.value("sanitizedCallbackCount").toInt() == 8
                    && productionProviderCallbackHarness.value("inputCapturePolicyCount").toInt() == 8
                    && productionProviderCallbackHarness.value("outputCapturePolicyCount").toInt() == 8
                    && productionProviderCallbackHarness.value("resultCapturePolicyCount").toInt() == 8
                    && productionProviderCallbackHarness.value("callbacks").toArray().size() == 8
                    && !productionProviderCallbackHarness.value("operationInvoked").toBool(true)
                    && !productionProviderCallbackHarness.value("inputBytesCaptured").toBool(true)
                    && !productionProviderCallbackHarness.value("outputBytesCaptured").toBool(true)
                    && !productionProviderCallbackHarness.value("resultCaptured").toBool(true)
                    && productionProviderVectorSelfTest.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-vector-self-test-v1")
                    && productionProviderVectorSelfTest.value("releaseGate").toString()
                        == QStringLiteral("production-provider-vector-self-test-blocked-not-linked")
                    && productionProviderVectorSelfTest.value("blockedVectorCount").toInt() == 8
                    && productionProviderVectorSelfTest.value("passedVectorCount").toInt() == 0
                    && productionProviderVectorSelfTest.value("knownAnswerReadyCount").toInt() == 0
                    && productionProviderVectorSelfTest.value("sanitizedVectorCount").toInt() == 8
                    && productionProviderVectorSelfTest.value("materialExportProofCount").toInt() == 8
                    && productionProviderVectorSelfTest.value("tests").toArray().size() == 8
                    && !productionProviderVectorSelfTest.value("operationInvoked").toBool(true)
                    && !productionProviderVectorSelfTest.value("inputBytesCaptured").toBool(true)
                    && !productionProviderVectorSelfTest.value("outputBytesCaptured").toBool(true)
                    && !productionProviderVectorSelfTest.value("resultCaptured").toBool(true)
                    && productionProviderExecutionSlotBinding.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-execution-slot-binding-v1")
                    && productionProviderExecutionSlotBinding.value("releaseGate").toString()
                        == QStringLiteral("production-provider-execution-slot-binding-blocked-not-linked")
                    && productionProviderExecutionSlotBinding.value("blockedSlotCount").toInt() == 8
                    && productionProviderExecutionSlotBinding.value("bindableSlotCount").toInt() == 0
                    && productionProviderExecutionSlotBinding.value("reviewedSlotCount").toInt() == 0
                    && productionProviderExecutionSlotBinding.value("contractMatchedSlotCount").toInt() == 8
                    && productionProviderExecutionSlotBinding.value("fixtureMatchedSlotCount").toInt() == 8
                    && productionProviderExecutionSlotBinding.value("sanitizedSlotCount").toInt() == 8
                    && productionProviderExecutionSlotBinding.value("slots").toArray().size() == 8
                    && !productionProviderExecutionSlotBinding.value("operationInvoked").toBool(true)
                    && !productionProviderExecutionSlotBinding.value("inputBytesCaptured").toBool(true)
                    && !productionProviderExecutionSlotBinding.value("outputBytesCaptured").toBool(true)
                    && !productionProviderExecutionSlotBinding.value("resultCaptured").toBool(true)
                    && productionProviderExecutionPath.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-execution-path-v1")
                    && productionProviderExecutionPath.value("releaseGate").toString()
                        == QStringLiteral("production-provider-execution-path-blocked-not-linked")
                    && productionProviderExecutionPath.value("blockedPathCount").toInt() == 8
                    && productionProviderExecutionPath.value("mappedPathCount").toInt() == 0
                    && productionProviderExecutionPath.value("pointerPresentCount").toInt() == 0
                    && productionProviderExecutionPath.value("bindableSlotCount").toInt() == 0
                    && productionProviderExecutionPath.value("capturePolicyCount").toInt() == 8
                    && productionProviderExecutionPath.value("sanitizedPathCount").toInt() == 8
                    && productionProviderExecutionPath.value("paths").toArray().size() == 8
                    && productionProviderExecutionPath.value("providerExecutionSlotBindingReleaseGate").toString()
                        == QStringLiteral("production-provider-execution-slot-binding-blocked-not-linked")
                    && !productionProviderExecutionPath.value("operationInvoked").toBool(true)
                    && !productionProviderExecutionPath.value("inputBytesCaptured").toBool(true)
                    && !productionProviderExecutionPath.value("outputBytesCaptured").toBool(true)
                    && !productionProviderExecutionPath.value("resultCaptured").toBool(true)
                    && productionProviderInvocationSandbox.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-sandbox-v1")
                    && productionProviderInvocationSandbox.value("releaseGate").toString()
                        == QStringLiteral("production-provider-invocation-sandbox-blocked-not-linked")
                    && productionProviderInvocationSandbox.value("readySandboxCount").toInt() == 0
                    && productionProviderInvocationSandbox.value("blockedSandboxCount").toInt() == 8
                    && productionProviderInvocationSandbox.value("mappedPathCount").toInt() == 0
                    && productionProviderInvocationSandbox.value("sanitizedSandboxCount").toInt() == 8
                    && productionProviderInvocationSandbox.value("timeoutPolicyCount").toInt() == 8
                    && productionProviderInvocationSandbox.value("errorPolicyCount").toInt() == 8
                    && productionProviderInvocationSandbox.value("materialPolicyCount").toInt() == 8
                    && productionProviderInvocationSandbox.value("sandboxes").toArray().size() == 8
                    && productionProviderInvocationSandbox.value("providerExecutionPathReleaseGate").toString()
                        == QStringLiteral("production-provider-execution-path-blocked-not-linked")
                    && !productionProviderInvocationSandbox.value("operationInvoked").toBool(true)
                    && !productionProviderInvocationSandbox.value("inputBytesCaptured").toBool(true)
                    && !productionProviderInvocationSandbox.value("outputBytesCaptured").toBool(true)
                    && !productionProviderInvocationSandbox.value("resultCaptured").toBool(true)
                    && productionProviderInvocationVectorResult.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-vector-result-v1")
                    && productionProviderInvocationVectorResult.value("releaseGate").toString()
                        == QStringLiteral("production-provider-invocation-vector-result-blocked-not-linked")
                    && productionProviderInvocationVectorResult.value("readyVectorResultCount").toInt() == 0
                    && productionProviderInvocationVectorResult.value("blockedVectorResultCount").toInt() == 8
                    && productionProviderInvocationVectorResult.value("sandboxReadyCount").toInt() == 0
                    && productionProviderInvocationVectorResult.value("fixtureMatchedCount").toInt() == 8
                    && productionProviderInvocationVectorResult.value("resultContractCount").toInt() == 8
                    && productionProviderInvocationVectorResult.value("sanitizedResultCount").toInt() == 8
                    && productionProviderInvocationVectorResult.value("materialExportProofCount").toInt() == 8
                    && productionProviderInvocationVectorResult.value("vectorResults").toArray().size() == 8
                    && productionProviderInvocationVectorResult.value("providerInvocationSandboxReleaseGate").toString()
                        == QStringLiteral("production-provider-invocation-sandbox-blocked-not-linked")
                    && !productionProviderInvocationVectorResult.value("operationInvoked").toBool(true)
                    && !productionProviderInvocationVectorResult.value("inputBytesCaptured").toBool(true)
                    && !productionProviderInvocationVectorResult.value("outputBytesCaptured").toBool(true)
                    && !productionProviderInvocationVectorResult.value("resultCaptured").toBool(true)
                    && productionProviderInvocationExecution.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-execution-v1")
                    && productionProviderInvocationExecution.value("releaseGate").toString()
                        == QStringLiteral("production-provider-invocation-execution-blocked-not-linked")
                    && productionProviderInvocationExecution.value("readyExecutionCount").toInt() == 0
                    && productionProviderInvocationExecution.value("blockedExecutionCount").toInt() == 8
                    && productionProviderInvocationExecution.value("vectorResultReadyCount").toInt() == 0
                    && productionProviderInvocationExecution.value("callableEntryPointCount").toInt() == 8
                    && productionProviderInvocationExecution.value("sanitizedExecutionCount").toInt() == 8
                    && productionProviderInvocationExecution.value("resultCapturePolicyCount").toInt() == 8
                    && productionProviderInvocationExecution.value("materialExportProofCount").toInt() == 8
                    && productionProviderInvocationExecution.value("executions").toArray().size() == 8
                    && productionProviderInvocationExecution.value("providerInvocationVectorResultReleaseGate").toString()
                        == QStringLiteral("production-provider-invocation-vector-result-blocked-not-linked")
                    && !productionProviderInvocationExecution.value("operationInvoked").toBool(true)
                    && !productionProviderInvocationExecution.value("inputBytesCaptured").toBool(true)
                    && !productionProviderInvocationExecution.value("outputBytesCaptured").toBool(true)
                    && !productionProviderInvocationExecution.value("resultCaptured").toBool(true)
                    && backendStatus.value("available").toBool(false),
                "default e2e backend status should explicitly identify the draft backend") && ok;
    ok = expect(draftBackend.value("id").toString() == QStringLiteral("draft-qt-hmac-stream-v1")
                    && draftBackend.value("linked").toBool(false)
                    && draftBackend.value("providerId").toString() == QStringLiteral("draft-qt-provider-v1")
                    && draftBackend.value("dispatchState").toString() == QStringLiteral("draft-dispatch-ready")
                    && draftBackend.value("providerSelfTestStatus").toString()
                        == QStringLiteral("development-only-self-test-passed")
                    && draftBackend.value("providerReadinessGate").toString()
                        == QStringLiteral("draft-provider-not-production")
                    && draftBackend.value("providerReadiness").toObject()
                        .value("checks").toArray().size() == 21
                    && draftBackend.value("providerCompatibilityStatus").toString()
                        == QStringLiteral("development-known-answer-passed")
                    && draftBackend.value("providerCompatibilityEvidence").toObject()
                        .value("contractComplete").toBool(false)
                    && draftBackend.value("operations").toArray().size() == 8
                    && !draftBackend.value("rawKeyExported").toBool(true)
                    && productionBackend.value("id").toString() == QStringLiteral("openssl-reviewed-adapter-v1")
                    && productionBackend.value("providerId").toString() == QStringLiteral("openssl-reviewed-provider-v1")
                    && productionBackend.value("dispatchState").toString() == QStringLiteral("not-linked")
                    && productionBackend.value("providerSelfTestStatus").toString()
                        == QStringLiteral("self-test-blocked-not-linked")
                    && productionBackend.value("providerReadinessGate").toString()
                        == QStringLiteral("production-adapter-not-linked")
                    && productionBackend.value("providerCompatibilityStatus").toString()
                        == QStringLiteral("compatibility-blocked-not-linked")
                    && productionBackend.value("providerCompatibilityGate").toString()
                        == QStringLiteral("production-adapter-not-linked")
                    && !productionBackend.value("linked").toBool(true)
                    && !productionBackend.value("productionReady").toBool(true)
                    && productionBackend.value("reason").toString() == QStringLiteral("production-backend-not-requested")
                    && productionBackend.value("providerReadiness").toObject()
                        .value("checks").toArray().size() == 21
                    && productionBackend.value("providerCompatibilityEvidence").toObject()
                        .value("operationManifest").toArray().size() == 8
                    && productionBackend.value("providerCompatibilityEvidence").toObject()
                        .value("operationHarness").toObject()
                        .value("operations").toArray().size() == 8
                    && productionBackend.value("providerCompatibilityEvidence").toObject()
                        .value("operationDispatchBindings").toObject()
                        .value("bindings").toArray().size() == 8
                    && productionBackend.value("providerCompatibilityEvidence").toObject()
                        .value("operationCallableManifest").toObject()
                        .value("callables").toArray().size() == 8
                    && productionBackend.value("operationCallableManifest").toObject()
                        .value("blockedCallableCount").toInt() == 8
                    && productionBackend.value("operationExecutionResult").toObject()
                        .value("blockedResultCount").toInt() == 8
                    && productionBackend.value("providerCompatibilityEvidence").toObject()
                        .value("providerTable").toObject()
                        .value("missingSymbolCount").toInt() == 8
                    && productionBackend.value("providerTable").toObject()
                        .value("releaseGate").toString()
                            == QStringLiteral("production-provider-table-blocked-not-linked")
                    && productionBackend.value("providerTableBindingProbe").toObject()
                        .value("releaseGate").toString()
                            == QStringLiteral("production-provider-table-binding-blocked-not-linked")
                    && productionBackend.value("providerTableRegistration").toObject()
                        .value("releaseGate").toString()
                            == QStringLiteral("production-provider-table-registration-blocked-not-linked")
                    && !productionBackend.value("providerTableRegistration").toObject()
                        .value("registered").toBool(true)
                    && productionBackend.value("providerOperationPreflight").toObject()
                        .value("blockedOperationCount").toInt() == 8
                    && !productionBackend.value("providerOperationPreflight").toObject()
                        .value("operationInvoked").toBool(true)
                    && productionBackend.value("providerCallFrame").toObject()
                        .value("blockedFrameCount").toInt() == 8
                    && !productionBackend.value("providerCallFrame").toObject()
                        .value("operationInvoked").toBool(true)
                    && productionBackend.value("providerInvocationDryRun").toObject()
                        .value("blockedInvocationCount").toInt() == 8
                    && !productionBackend.value("providerInvocationDryRun").toObject()
                        .value("operationInvoked").toBool(true)
                    && productionBackend.value("providerInvocationResult").toObject()
                        .value("blockedResultCount").toInt() == 8
                    && !productionBackend.value("providerInvocationResult").toObject()
                        .value("operationInvoked").toBool(true)
                    && productionBackend.value("providerExecutionDecision").toObject()
                        .value("blockedDecisionCount").toInt() == 8
                    && !productionBackend.value("providerExecutionDecision").toObject()
                        .value("operationInvoked").toBool(true)
                    && productionBackend.value("providerCompatibilityEvidence").toObject()
                        .value("providerReviewedExecutionCandidate").toObject()
                        .value("candidateCount").toInt() == 8
                    && productionBackend.value("providerCompatibilityEvidence").toObject()
                        .value("providerReviewedExecutionCandidate").toObject()
                        .value("blockedCandidateCount").toInt() == 8
                    && productionBackend.value("providerCompatibilityEvidence").toObject()
                        .value("providerReviewedExecutionCandidate").toObject()
                        .value("releaseGate").toString()
                            == QStringLiteral("production-provider-reviewed-execution-candidate-not-release-gate")
                    && productionBackend.value("providerCompatibilityEvidence").toObject()
                        .value("blockedOperationCount").toInt() == 8
                    && productionBackend.value("operations").toArray().size() == 8,
                "registered backend descriptors should expose adapter readiness without private material") && ok;
    const QJsonObject firstDispatchBinding =
        productionDispatchBindings.value("bindings").toArray().at(0).toObject();
    ok = expect(firstDispatchBinding.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstDispatchBinding.value("slotId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1/session-key-generation-slot")
                    && firstDispatchBinding.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstDispatchBinding.value("bindingState").toString()
                        == QStringLiteral("not-linked")
                    && !firstDispatchBinding.value("dispatchCallable").toBool(true)
                    && !firstDispatchBinding.value("reviewed").toBool(true)
                    && firstDispatchBinding.value("expectedSignature").toString()
                        .contains(QStringLiteral("qnc_e2e_op_session_key_generation_v1"))
                    && firstDispatchBinding.value("inputContract").toArray().at(0).toString()
                        == QStringLiteral("secure-random-source")
                    && firstDispatchBinding.value("outputContract").toArray().at(0).toString()
                        == QStringLiteral("session-key-handle")
                    && firstDispatchBinding.value("fixtureHashSha256").toString().size() == 64
                    && !firstDispatchBinding.value("rawKeyExported").toBool(true)
                    && !firstDispatchBinding.value("privateMaterialExported").toBool(true),
                "production dispatch bindings should expose stable provider ABI evidence without enabling placeholders") && ok;
    const QJsonObject firstCallable =
        productionCallableManifest.value("callables").toArray().at(0).toObject();
    ok = expect(firstCallable.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstCallable.value("slotId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1/session-key-generation-slot")
                    && firstCallable.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstCallable.value("providerAbiSignature").toString()
                        .contains(QStringLiteral("qnc_e2e_op_session_key_generation_v1"))
                    && firstCallable.value("symbolMatches").toBool(false)
                    && firstCallable.value("abiSignatureMatches").toBool(false)
                    && firstCallable.value("fixtureHashMatches").toBool(false)
                    && !firstCallable.value("callable").toBool(true)
                    && !firstCallable.value("reviewed").toBool(true)
                    && firstCallable.value("blockedReason").toString()
                        == QStringLiteral("production-crypto-backend-unavailable")
                    && !firstCallable.value("rawKeyExported").toBool(true)
                    && !firstCallable.value("privateMaterialExported").toBool(true),
                "production callable manifest should expose reviewed ABI slots without enabling placeholders") && ok;
    const QJsonObject firstExecutionResult =
        productionExecutionResult.value("results").toArray().at(0).toObject();
    ok = expect(firstExecutionResult.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstExecutionResult.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstExecutionResult.value("providerAbiSignature").toString()
                        .contains(QStringLiteral("qnc_e2e_op_session_key_generation_v1"))
                    && firstExecutionResult.value("resultState").toString()
                        == QStringLiteral("blocked-not-linked")
                    && firstExecutionResult.value("errorClass").toString()
                        == QStringLiteral("production-result-adapter-not-linked")
                    && firstExecutionResult.value("outputContract").toArray().at(0).toString()
                        == QStringLiteral("session-key-handle")
                    && firstExecutionResult.value("resultContract").toArray().size() == 4
                    && !firstExecutionResult.value("passed").toBool(true)
                    && firstExecutionResult.value("sanitized").toBool(false)
                    && !firstExecutionResult.value("rawKeyExported").toBool(true)
                    && !firstExecutionResult.value("privateMaterialExported").toBool(true)
                    && !firstExecutionResult.value("sessionSecretExported").toBool(true)
                    && !firstExecutionResult.value("privateIdentityMaterialExported").toBool(true),
                "production execution results should expose sanitized result contracts without executing placeholders") && ok;
    const QJsonObject firstProviderExecutionDecision =
        productionProviderExecutionDecision.value("decisions").toArray().at(0).toObject();
    ok = expect(firstProviderExecutionDecision.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstProviderExecutionDecision.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstProviderExecutionDecision.value("decisionState").toString()
                        == QStringLiteral("blocked-not-linked")
                    && !firstProviderExecutionDecision.value("reviewedProviderCallbackAllowed").toBool(true)
                    && !firstProviderExecutionDecision.value("operationInvoked").toBool(true)
                    && !firstProviderExecutionDecision.value("resultCaptured").toBool(true)
                    && firstProviderExecutionDecision.value("noSensitiveMaterialExport").toBool(false)
                    && !firstProviderExecutionDecision.value("rawKeyExported").toBool(true)
                    && !firstProviderExecutionDecision.value("privateMaterialExported").toBool(true),
                "production provider execution decision should block callbacks until result capture is accepted") && ok;
    const QJsonObject firstProviderCallback =
        productionProviderCallbackHarness.value("callbacks").toArray().at(0).toObject();
    ok = expect(firstProviderCallback.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstProviderCallback.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstProviderCallback.value("callbackState").toString()
                        == QStringLiteral("blocked-not-linked")
                    && firstProviderCallback.value("executionDecisionReleaseGate").toString()
                        == QStringLiteral("production-provider-execution-decision-blocked-not-linked")
                    && !firstProviderCallback.value("reviewedProviderCallbackAllowed").toBool(true)
                    && !firstProviderCallback.value("callbackHarnessArmed").toBool(true)
                    && firstProviderCallback.value("inputCapturePolicy").toString()
                        == QStringLiteral("sanitized-metadata-only-no-input-bytes")
                    && firstProviderCallback.value("outputCapturePolicy").toString()
                        == QStringLiteral("sanitized-contract-only-no-output-bytes")
                    && firstProviderCallback.value("resultCapturePolicy").toString()
                        == QStringLiteral("status-error-fixture-proof-only")
                    && !firstProviderCallback.value("operationInvoked").toBool(true)
                    && !firstProviderCallback.value("inputBytesCaptured").toBool(true)
                    && !firstProviderCallback.value("outputBytesCaptured").toBool(true)
                    && !firstProviderCallback.value("resultCaptured").toBool(true)
                    && !firstProviderCallback.value("rawKeyExported").toBool(true)
                    && !firstProviderCallback.value("privateMaterialExported").toBool(true),
                "production provider callback harness should stay sanitized and fail closed") && ok;
    const QJsonObject firstProviderVectorSelfTest =
        productionProviderVectorSelfTest.value("tests").toArray().at(0).toObject();
    ok = expect(firstProviderVectorSelfTest.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstProviderVectorSelfTest.value("vectorSet").toString()
                        == QStringLiteral("production-session-key-generation-vectors-v1")
                    && firstProviderVectorSelfTest.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstProviderVectorSelfTest.value("vectorExecutionState").toString()
                        == QStringLiteral("blocked-not-linked")
                    && firstProviderVectorSelfTest.value("providerCallbackHarnessReleaseGate").toString()
                        == QStringLiteral("production-provider-callback-harness-blocked-not-linked")
                    && !firstProviderVectorSelfTest.value("knownAnswerVectorReady").toBool(true)
                    && !firstProviderVectorSelfTest.value("roundTripVectorReady").toBool(true)
                    && !firstProviderVectorSelfTest.value("knownAnswerPassed").toBool(true)
                    && !firstProviderVectorSelfTest.value("roundTripPassed").toBool(true)
                    && !firstProviderVectorSelfTest.value("vectorPassed").toBool(true)
                    && !firstProviderVectorSelfTest.value("operationInvoked").toBool(true)
                    && !firstProviderVectorSelfTest.value("inputBytesCaptured").toBool(true)
                    && !firstProviderVectorSelfTest.value("outputBytesCaptured").toBool(true)
                    && !firstProviderVectorSelfTest.value("resultCaptured").toBool(true)
                    && !firstProviderVectorSelfTest.value("rawKeyExported").toBool(true)
                    && !firstProviderVectorSelfTest.value("privateMaterialExported").toBool(true),
                "production provider vector self-test should expose fixtures without invoking provider callbacks") && ok;
    const QJsonObject firstProviderExecutionSlot =
        productionProviderExecutionSlotBinding.value("slots").toArray().at(0).toObject();
    ok = expect(firstProviderExecutionSlot.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstProviderExecutionSlot.value("slotId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1/session-key-generation/reviewed-execution-slot")
                    && firstProviderExecutionSlot.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstProviderExecutionSlot.value("bindingState").toString()
                        == QStringLiteral("blocked-not-linked")
                    && firstProviderExecutionSlot.value("providerVectorSelfTestReleaseGate").toString()
                        == QStringLiteral("production-provider-vector-self-test-blocked-not-linked")
                    && !firstProviderExecutionSlot.value("vectorPassed").toBool(true)
                    && !firstProviderExecutionSlot.value("reviewed").toBool(true)
                    && firstProviderExecutionSlot.value("contractMatched").toBool(false)
                    && firstProviderExecutionSlot.value("fixtureHashMatched").toBool(false)
                    && !firstProviderExecutionSlot.value("executionSlotBindable").toBool(true)
                    && !firstProviderExecutionSlot.value("operationInvoked").toBool(true)
                    && !firstProviderExecutionSlot.value("inputBytesCaptured").toBool(true)
                    && !firstProviderExecutionSlot.value("outputBytesCaptured").toBool(true)
                    && !firstProviderExecutionSlot.value("resultCaptured").toBool(true)
                    && !firstProviderExecutionSlot.value("rawKeyExported").toBool(true)
                    && !firstProviderExecutionSlot.value("privateMaterialExported").toBool(true),
                "production provider execution slot binding should stay blocked until vector self-test passes") && ok;
    const QJsonObject firstProviderCallFrame =
        productionProviderCallFrame.value("frames").toArray().at(0).toObject();
    ok = expect(firstProviderCallFrame.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstProviderCallFrame.value("operationEnumValue").toInt(-1) == 0
                    && firstProviderCallFrame.value("operationEnumMatched").toBool(false)
                    && firstProviderCallFrame.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstProviderCallFrame.value("primaryInputClass").toString()
                        == QStringLiteral("empty-random-source-context")
                    && firstProviderCallFrame.value("outputMaterialPolicy").toString()
                        == QStringLiteral("handle-only-no-private-material-export")
                    && firstProviderCallFrame.value("inputContractHashSha256").toString().size() == 64
                    && firstProviderCallFrame.value("outputContractHashSha256").toString().size() == 64
                    && firstProviderCallFrame.value("contractHashed").toBool(false)
                    && !firstProviderCallFrame.value("frameReady").toBool(true)
                    && !firstProviderCallFrame.value("operationInvoked").toBool(true)
                    && !firstProviderCallFrame.value("inputBytesAttached").toBool(true)
                    && !firstProviderCallFrame.value("outputBytesAttached").toBool(true)
                    && !firstProviderCallFrame.value("rawKeyExported").toBool(true),
                "production provider call frames should expose sanitized ABI inputs before invocation") && ok;
    const QJsonObject firstProviderTableEntry =
        productionProviderTable.value("entries").toArray().at(0).toObject();
    ok = expect(firstProviderTableEntry.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstProviderTableEntry.value("requiredSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstProviderTableEntry.value("tableAbi").toString()
                        == QString::fromLatin1(QNC_E2E_PROVIDER_TABLE_ABI)
                    && firstProviderTableEntry.value("providerApiHeader").toString()
                        == QStringLiteral("include/qtnetworkchat_e2e_provider_api.h")
                    && firstProviderTableEntry.value("headerOperationCount").toInt()
                        == QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT
                    && firstProviderTableEntry.value("required").toBool(false)
                    && !firstProviderTableEntry.value("bound").toBool(true)
                    && firstProviderTableEntry.value("abiSignature").toString()
                        .contains(QStringLiteral("qnc_e2e_op_session_key_generation_v1"))
                    && firstProviderTableEntry.value("fixtureHashSha256").toString().size() == 64
                    && firstProviderTableEntry.value("blockedReason").toString()
                        == QStringLiteral("production-provider-table-not-bound")
                    && !firstProviderTableEntry.value("rawKeyExported").toBool(true)
                    && !firstProviderTableEntry.value("privateMaterialExported").toBool(true),
                "production provider table should expose required symbols without binding placeholders") && ok;
    ok = expect(QString::fromLatin1(QNC_E2E_OPERATION_CONTRACT_VERSION)
                        == QStringLiteral("qtnetworkchat-e2e-crypto-ops-v1")
                    && sizeof(qnc_e2e_operation_input_v1) > 0
                    && sizeof(qnc_e2e_operation_output_v1) > 0
                    && sizeof(qnc_e2e_provider_table_v1) > sizeof(void*)
                    && QNC_E2E_OPERATION_PAYLOAD_DECRYPT == 7
                    && QNC_E2E_STATUS_OK == 0,
                "production provider C ABI header should expose stable table and operation contracts") && ok;
    const QJsonObject firstBindingProbeMapping =
        productionProviderTableBindingProbe.value("enumMappings").toArray().at(0).toObject();
    ok = expect(firstBindingProbeMapping.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstBindingProbeMapping.value("headerEnumValue").toInt(-1)
                        == QNC_E2E_OPERATION_SESSION_KEY_GENERATION
                    && firstBindingProbeMapping.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && firstBindingProbeMapping.value("enumMatchesOperationOrder").toBool(false)
                    && !firstBindingProbeMapping.value("bound").toBool(true),
                "production provider table binding probe should map header enum values to provider symbols") && ok;
    qnc_e2e_provider_table_v1 completeProviderTable = {};
    completeProviderTable.abi = QNC_E2E_PROVIDER_TABLE_ABI;
    completeProviderTable.provider_id = "openssl-reviewed-provider-v1";
    completeProviderTable.operation_count = QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT;
    completeProviderTable.session_key_generation = dummyProviderOperation;
    completeProviderTable.identity_key_generation = dummyProviderOperation;
    completeProviderTable.public_key_derivation = dummyProviderOperation;
    completeProviderTable.agreement_sign = dummyProviderOperation;
    completeProviderTable.agreement_verify = dummyProviderOperation;
    completeProviderTable.session_derive = dummyProviderOperation;
    completeProviderTable.payload_encrypt = dummyProviderOperation;
    completeProviderTable.payload_decrypt = dummyProviderOperation;
    const QJsonObject validProviderTable =
        e2eValidateProductionProviderTable(&completeProviderTable);
    ok = expect(validProviderTable.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-table-validation-v1")
                    && validProviderTable.value("accepted").toBool(false)
                    && validProviderTable.value("present").toBool(false)
                    && validProviderTable.value("abiMatches").toBool(false)
                    && validProviderTable.value("providerId").toString()
                        == QStringLiteral("openssl-reviewed-provider-v1")
                    && validProviderTable.value("operationCountMatches").toBool(false)
                    && validProviderTable.value("allOperationPointersPresent").toBool(false)
                    && validProviderTable.value("blockedReason").toString().isEmpty(),
                "complete production provider table should pass structural validation without executing crypto") && ok;
    const QJsonObject registeredProviderTable =
        e2eRegisterProductionProviderTable(&completeProviderTable);
    ok = expect(registeredProviderTable.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-table-registration-v1")
                    && registeredProviderTable.value("registered").toBool(false)
                    && registeredProviderTable.value("providerTableRegistered").toBool(false)
                    && registeredProviderTable.value("tableValidationAccepted").toBool(false)
                    && registeredProviderTable.value("tableValidation").toObject()
                        .value("providerId").toString()
                            == QStringLiteral("openssl-reviewed-provider-v1")
                    && !registeredProviderTable.value("accepted").toBool(true)
                    && registeredProviderTable.value("releaseGate").toString()
                        == QStringLiteral("production-provider-table-registration-blocked-not-production-ready")
                    && registeredProviderTable.value("blockedReason").toString()
                        == QStringLiteral("production-provider-table-compile-binding-disabled"),
                "runtime provider table registration should expose sanitized evidence without enabling production crypto") && ok;
    const QJsonObject backendStatusAfterRegistration = e2eCryptoBackendStatus();
    const QJsonObject preflightAfterRegistration =
        backendStatusAfterRegistration.value("productionProviderOperationPreflight").toObject();
    const QJsonObject callFrameAfterRegistration =
        backendStatusAfterRegistration.value("productionProviderCallFrame").toObject();
    const QJsonObject invocationDryRunAfterRegistration =
        backendStatusAfterRegistration.value("productionProviderInvocationDryRun").toObject();
    const QJsonObject invocationResultAfterRegistration =
        backendStatusAfterRegistration.value("productionProviderInvocationResult").toObject();
    const QJsonObject executionPathAfterRegistration =
        backendStatusAfterRegistration.value("productionProviderExecutionPath").toObject();
    const QJsonObject invocationSandboxAfterRegistration =
        backendStatusAfterRegistration.value("productionProviderInvocationSandbox").toObject();
    const QJsonObject invocationVectorResultAfterRegistration =
        backendStatusAfterRegistration.value("productionProviderInvocationVectorResult").toObject();
    const QJsonObject invocationExecutionAfterRegistration =
        backendStatusAfterRegistration.value("productionProviderInvocationExecution").toObject();
    ok = expect(!backendStatusAfterRegistration.value("productionReady").toBool(true)
                    && backendStatusAfterRegistration.value("productionProviderTableRegistration").toObject()
                        .value("registered").toBool(false)
                    && backendStatusAfterRegistration.value("productionProviderTableRegistration").toObject()
                        .value("tableValidationAccepted").toBool(false)
                    && backendStatusAfterRegistration.value("productionAcceptance").toObject()
                        .value("providerTableRegistered").toBool(false)
                    && !backendStatusAfterRegistration.value("productionAcceptance").toObject()
                        .value("providerTableRegistrationAccepted").toBool(true)
                    && preflightAfterRegistration.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-operation-preflight-v1")
                    && preflightAfterRegistration.value("providerTableRegistered").toBool(false)
                    && preflightAfterRegistration.value("presentOperationCount").toInt() == 8
                    && preflightAfterRegistration.value("blockedOperationCount").toInt() == 8
                    && preflightAfterRegistration.value("operations").toArray().at(0).toObject()
                        .value("pointerPresent").toBool(false)
                    && !preflightAfterRegistration.value("operationInvoked").toBool(true)
                    && !backendStatusAfterRegistration.value("productionAcceptance").toObject()
                        .value("providerOperationPreflightAccepted").toBool(true)
                    && !backendStatusAfterRegistration.value("productionAcceptance").toObject()
                        .value("providerCallFrameAccepted").toBool(true)
                    && callFrameAfterRegistration.value("blockedFrameCount").toInt() == 8
                    && callFrameAfterRegistration.value("frames").toArray().size() == 8
                    && !callFrameAfterRegistration.value("operationInvoked").toBool(true)
                    && !backendStatusAfterRegistration.value("productionAcceptance").toObject()
                        .value("providerInvocationDryRunAccepted").toBool(true)
                    && !backendStatusAfterRegistration.value("productionAcceptance").toObject()
                        .value("providerInvocationResultAccepted").toBool(true)
                    && invocationDryRunAfterRegistration.value("blockedInvocationCount").toInt() == 8
                    && invocationDryRunAfterRegistration.value("invocations").toArray().size() == 8
                    && !invocationDryRunAfterRegistration.value("operationInvoked").toBool(true)
                    && invocationResultAfterRegistration.value("blockedResultCount").toInt() == 8
                    && invocationResultAfterRegistration.value("results").toArray().size() == 8
                    && !invocationResultAfterRegistration.value("operationInvoked").toBool(true)
                    && !invocationResultAfterRegistration.value("resultCaptured").toBool(true)
                    && executionPathAfterRegistration.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-execution-path-v1")
                    && executionPathAfterRegistration.value("providerTableRegistered").toBool(false)
                    && executionPathAfterRegistration.value("pointerPresentCount").toInt() == 8
                    && executionPathAfterRegistration.value("mappedPathCount").toInt() == 0
                    && executionPathAfterRegistration.value("blockedPathCount").toInt() == 8
                    && executionPathAfterRegistration.value("paths").toArray().size() == 8
                    && executionPathAfterRegistration.value("paths").toArray().at(0).toObject()
                        .value("functionPointerPresent").toBool(false)
                    && !executionPathAfterRegistration.value("operationInvoked").toBool(true)
                    && !executionPathAfterRegistration.value("inputBytesCaptured").toBool(true)
                    && !executionPathAfterRegistration.value("outputBytesCaptured").toBool(true)
                    && !executionPathAfterRegistration.value("resultCaptured").toBool(true)
                    && invocationSandboxAfterRegistration.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-sandbox-v1")
                    && invocationSandboxAfterRegistration.value("readySandboxCount").toInt() == 0
                    && invocationSandboxAfterRegistration.value("blockedSandboxCount").toInt() == 8
                    && invocationSandboxAfterRegistration.value("mappedPathCount").toInt() == 0
                    && invocationSandboxAfterRegistration.value("sandboxes").toArray().size() == 8
                    && !invocationSandboxAfterRegistration.value("operationInvoked").toBool(true)
                    && !invocationSandboxAfterRegistration.value("inputBytesCaptured").toBool(true)
                    && !invocationSandboxAfterRegistration.value("outputBytesCaptured").toBool(true)
                    && !invocationSandboxAfterRegistration.value("resultCaptured").toBool(true)
                    && invocationVectorResultAfterRegistration.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-vector-result-v1")
                    && invocationVectorResultAfterRegistration.value("readyVectorResultCount").toInt() == 0
                    && invocationVectorResultAfterRegistration.value("blockedVectorResultCount").toInt() == 8
                    && invocationVectorResultAfterRegistration.value("vectorResults").toArray().size() == 8
                    && !invocationVectorResultAfterRegistration.value("operationInvoked").toBool(true)
                    && !invocationVectorResultAfterRegistration.value("inputBytesCaptured").toBool(true)
                    && !invocationVectorResultAfterRegistration.value("outputBytesCaptured").toBool(true)
                    && !invocationVectorResultAfterRegistration.value("resultCaptured").toBool(true)
                    && invocationExecutionAfterRegistration.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-execution-v1")
                    && invocationExecutionAfterRegistration.value("readyExecutionCount").toInt() == 0
                    && invocationExecutionAfterRegistration.value("blockedExecutionCount").toInt() == 8
                    && invocationExecutionAfterRegistration.value("executions").toArray().size() == 8
                    && !invocationExecutionAfterRegistration.value("operationInvoked").toBool(true)
                    && !invocationExecutionAfterRegistration.value("inputBytesCaptured").toBool(true)
                    && !invocationExecutionAfterRegistration.value("outputBytesCaptured").toBool(true)
                    && !invocationExecutionAfterRegistration.value("resultCaptured").toBool(true),
                "registered provider table should remain fail-closed until production readiness and binding are enabled") && ok;
    completeProviderTable.payload_decrypt = nullptr;
    const QJsonObject missingPointerTable =
        e2eRegisterProductionProviderTable(&completeProviderTable);
    ok = expect(!missingPointerTable.value("accepted").toBool(true)
                    && missingPointerTable.value("blockedReason").toString()
                        == QStringLiteral("production-provider-table-operation-pointer-missing")
                    && !missingPointerTable.value("tableValidation").toObject()
                        .value("allOperationPointersPresent").toBool(true),
                "provider table registration should fail closed when a required operation pointer is missing") && ok;
    const QJsonObject missingPointerProbe =
        e2eProbeProductionCryptoProviderInvocationExecution();
    const QJsonObject firstMissingPointerProbe =
        missingPointerProbe.value("probes").toArray().at(0).toObject();
    const QJsonObject lastMissingPointerProbe =
        missingPointerProbe.value("probes").toArray().at(7).toObject();
    ok = expect(missingPointerProbe.value("invokedOperationCount").toInt() == 0
                    && missingPointerProbe.value("blockedProbeCount").toInt() == 8
                    && missingPointerProbe.value("tableValidationBlockedProbeCount").toInt() == 8
                    && missingPointerProbe.value("operationPointerMissingProbeCount").toInt() == 1
                    && missingPointerProbe.value("mismatchReasonSummary").toObject()
                        .value("production-provider-table-operation-pointer-missing").toInt() == 8
                    && missingPointerProbe.value("mismatchSeveritySummary").toObject()
                        .value("fail-closed").toInt() == 8
                    && missingPointerProbe.value("mismatchScopeSummary").toObject()
                        .value("provider-table-validation").toInt() == 8
                    && firstMissingPointerProbe.value("tableValidationBlockedReason").toString()
                        == QStringLiteral("production-provider-table-operation-pointer-missing")
                    && firstMissingPointerProbe.value("functionPointerPresent").toBool(false)
                    && !firstMissingPointerProbe.value("operationPointerMissing").toBool(true)
                    && !firstMissingPointerProbe.value("operationInvoked").toBool(true)
                    && firstMissingPointerProbe.value("mismatchScope").toString()
                        == QStringLiteral("provider-table-validation")
                    && !lastMissingPointerProbe.value("functionPointerPresent").toBool(true)
                    && lastMissingPointerProbe.value("operationPointerMissing").toBool(false),
                "provider invocation probe should expose pointer-missing table validation as blocked matrix evidence") && ok;
    completeProviderTable.payload_decrypt = dummyProviderOperation;
    qnc_e2e_provider_table_v1 countingProviderTable = completeProviderTable;
    countingProviderTable.session_key_generation = countingProviderOperation;
    countingProviderTable.identity_key_generation = countingProviderOperation;
    countingProviderTable.public_key_derivation = countingProviderOperation;
    countingProviderTable.agreement_sign = countingProviderOperation;
    countingProviderTable.agreement_verify = countingProviderOperation;
    countingProviderTable.session_derive = countingProviderOperation;
    countingProviderTable.payload_encrypt = countingProviderOperation;
    countingProviderTable.payload_decrypt = countingProviderOperation;
    g_probeInvocationCount = 0;
    const QJsonObject registeredCountingProviderTable =
        e2eRegisterProductionProviderTable(&countingProviderTable);
    const QJsonObject invocationExecutionProbe =
        e2eProbeProductionCryptoProviderInvocationExecution();
    ok = expect(registeredCountingProviderTable.value("registered").toBool(false)
                    && registeredCountingProviderTable.value("tableValidationAccepted").toBool(false)
                    && invocationExecutionProbe.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-execution-probe-v1")
                    && !invocationExecutionProbe.value("accepted").toBool(true)
                    && invocationExecutionProbe.value("releaseGate").toString()
                        == QStringLiteral("production-provider-invocation-execution-probe-not-release-gate")
                    && invocationExecutionProbe.value("invokedOperationCount").toInt() == 8
                    && invocationExecutionProbe.value("capturedResultCount").toInt() == 8
                    && invocationExecutionProbe.value("sanitizedProbeCount").toInt() == 8
                    && invocationExecutionProbe.value("okStatusCount").toInt() == 8
                    && invocationExecutionProbe.value("statusMismatchCount").toInt() == 0
                    && invocationExecutionProbe.value("vectorPassCount").toInt() == 8
                    && invocationExecutionProbe.value("vectorFailCount").toInt() == 0
                    && invocationExecutionProbe.value("vectorContractCount").toInt() == 8
                    && invocationExecutionProbe.value("vectorContractHashCount").toInt() == 8
                    && invocationExecutionProbe.value("expectedStatusClassMatchCount").toInt() == 8
                    && invocationExecutionProbe.value("expectedFailureClassMatchCount").toInt() == 8
                    && invocationExecutionProbe.value("expectedMaterialPolicyClassMatchCount").toInt() == 8
                    && invocationExecutionProbe.value("executionFrameCount").toInt() == 8
                    && invocationExecutionProbe.value("executionFrameSanitizedCount").toInt() == 8
                    && invocationExecutionProbe.value("executionFrameResultCapturedCount").toInt() == 8
                    && invocationExecutionProbe.value("knownAnswerOutputEvidenceCount").toInt() == 8
                    && invocationExecutionProbe.value("knownAnswerOutputShapeHashCount").toInt() == 8
                    && invocationExecutionProbe.value("expectedOutputClassMatchCount").toInt() == 8
                    && invocationExecutionProbe.value("expectedOutputClassMismatchCount").toInt() == 0
                    && invocationExecutionProbe.value("outputEvidenceFailClosedCount").toInt() == 0
                    && invocationExecutionProbe.value("providerVectorSetMatchedCount").toInt() == 8
                    && invocationExecutionProbe.value("providerVectorSetMismatchCount").toInt() == 0
                    && invocationExecutionProbe.value("operationPointerMissingProbeCount").toInt() == 0
                    && invocationExecutionProbe.value("tableValidationBlockedProbeCount").toInt() == 0
                    && invocationExecutionProbe.value("failureClassSummary").toObject()
                        .value("none").toInt() == 8
                    && invocationExecutionProbe.value("vectorResultSummary").toObject()
                        .value("probe-vector-passed").toInt() == 8
                    && invocationExecutionProbe.value("knownAnswerOutputClassSummary").toObject()
                        .value("handle-status-output").toInt() == 2
                    && invocationExecutionProbe.value("knownAnswerOutputClassSummary").toObject()
                        .value("public-output-shape").toInt() == 4
                    && invocationExecutionProbe.value("knownAnswerOutputClassSummary").toObject()
                        .value("sealed-output-shape").toInt() == 1
                    && invocationExecutionProbe.value("knownAnswerOutputClassSummary").toObject()
                        .value("payload-output-shape").toInt() == 1
                    && invocationExecutionProbe.value("outputEvidenceClassSummary").toObject()
                        .value("known-answer-output-shape-matched").toInt() == 8
                    && invocationExecutionProbe.value("mismatchReasonSummary").toObject()
                        .value("none").toInt() == 8
                    && invocationExecutionProbe.value("mismatchSeveritySummary").toObject()
                        .value("none").toInt() == 8
                    && invocationExecutionProbe.value("mismatchScopeSummary").toObject()
                        .value("none").toInt() == 8
                    && invocationExecutionProbe.value("blockedProbeCount").toInt() == 0
                    && invocationExecutionProbe.value("probes").toArray().size() == 8
                    && g_probeInvocationCount == 8
                    && invocationExecutionProbe.value("providerInvocationExecutionAccepted").toBool(true) == false
                    && !invocationExecutionProbe.value("inputBytesCaptured").toBool(true)
                    && !invocationExecutionProbe.value("outputBytesCaptured").toBool(true)
                    && !invocationExecutionProbe.value("rawKeyExported").toBool(true)
                    && !invocationExecutionProbe.value("privateMaterialExported").toBool(true),
                "explicit provider invocation execution probe should call registered stub operations without becoming a release gate") && ok;
    const QJsonObject reviewedCandidateProbe =
        e2eProbeProductionCryptoProviderReviewedExecutionCandidate();
    ok = expect(reviewedCandidateProbe.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-execution-candidate-v1")
                    && !reviewedCandidateProbe.value("accepted").toBool(true)
                    && reviewedCandidateProbe.value("candidateNonReleaseGate").toBool(false)
                    && reviewedCandidateProbe.value("releaseGate").toString()
                        == QStringLiteral("production-provider-reviewed-execution-candidate-not-release-gate")
                    && reviewedCandidateProbe.value("probeSourceCaptured").toBool(false)
                    && reviewedCandidateProbe.value("providerInvocationExecutionProbeReleaseGate").toString()
                        == QStringLiteral("production-provider-invocation-execution-probe-not-release-gate")
                    && reviewedCandidateProbe.value("candidateCount").toInt() == 8
                    && reviewedCandidateProbe.value("candidateReadyCount").toInt() == 8
                    && reviewedCandidateProbe.value("blockedCandidateCount").toInt() == 0
                    && reviewedCandidateProbe.value("probeMatrixMatchedCount").toInt() == 8
                    && reviewedCandidateProbe.value("candidateSanitizedCount").toInt() == 8
                    && reviewedCandidateProbe.value("candidateEntrypointCount").toInt() == 8
                    && reviewedCandidateProbe.value("candidateVectorContractHashCount").toInt() == 8
                    && reviewedCandidateProbe.value("candidateOutputShapeHashCount").toInt() == 8
                    && reviewedCandidateProbe.value("mismatchFreeCandidateCount").toInt() == 8
                    && reviewedCandidateProbe.value("pointerPresentCandidateCount").toInt() == 8
                    && reviewedCandidateProbe.value("tableValidationAcceptedCandidateCount").toInt() == 8
                    && reviewedCandidateProbe.value("materialExportProofCount").toInt() == 8
                    && reviewedCandidateProbe.value("blockedReason").toString()
                        == QStringLiteral("production-provider-reviewed-candidates-awaiting-audit-release-gate")
                    && reviewedCandidateProbe.value("blockedReasonSummary").toObject()
                        .value("none").toInt() == 8
                    && reviewedCandidateProbe.value("candidates").toArray().size() == 8
                    && g_probeInvocationCount == 16
                    && !reviewedCandidateProbe.value("operationInvokedByCandidate").toBool(true)
                    && !reviewedCandidateProbe.value("inputBytesCaptured").toBool(true)
                    && !reviewedCandidateProbe.value("outputBytesCaptured").toBool(true)
                    && !reviewedCandidateProbe.value("rawKeyExported").toBool(true)
                    && !reviewedCandidateProbe.value("privateMaterialExported").toBool(true)
                    && !reviewedCandidateProbe.value("sessionSecretExported").toBool(true),
                "explicit reviewed provider execution candidates should map clean probes without becoming a release gate") && ok;
    const QJsonObject firstExecutionProbe =
        invocationExecutionProbe.value("probes").toArray().at(0).toObject();
    const QJsonObject firstReviewedCandidate =
        reviewedCandidateProbe.value("candidates").toArray().at(0).toObject();
    ok = expect(firstReviewedCandidate.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstReviewedCandidate.value("candidateState").toString()
                        == QStringLiteral("reviewed-provider-execution-candidate-ready")
                    && firstReviewedCandidate.value("candidateReady").toBool(false)
                    && firstReviewedCandidate.value("candidateNonReleaseGate").toBool(false)
                    && firstReviewedCandidate.value("executionEntrypoint").toString()
                        == QStringLiteral("qnc_e2e_provider_table_v1/qnc_e2e_op_session_key_generation_v1")
                    && firstReviewedCandidate.value("probeMatrixMatched").toBool(false)
                    && firstReviewedCandidate.value("mismatchFree").toBool(false)
                    && firstReviewedCandidate.value("mismatchReason").toString()
                        == QStringLiteral("none")
                    && firstReviewedCandidate.value("mismatchSeverity").toString()
                        == QStringLiteral("none")
                    && firstReviewedCandidate.value("mismatchScope").toString()
                        == QStringLiteral("none")
                    && firstReviewedCandidate.value("tableValidationAccepted").toBool(false)
                    && firstReviewedCandidate.value("operationPointerPresent").toBool(false)
                    && firstReviewedCandidate.value("executionEvidenceReady").toBool(false)
                    && firstReviewedCandidate.value("candidateEntrypointReady").toBool(false)
                    && firstReviewedCandidate.value("knownAnswerVectorId").toString()
                        == QStringLiteral("production-session-key-generation-vectors-v1/session-key-generation")
                    && firstReviewedCandidate.value("knownAnswerFixtureId").toString()
                        == QStringLiteral("probe-fixture/session-key-generation")
                    && firstReviewedCandidate.value("probeEvidence").toObject()
                        .value("outputShapeHashSha256").toString().size() == 64
                    && firstReviewedCandidate.value("structuralExecution").toObject()
                        .value("executionEntryPoint").toString()
                            == QStringLiteral("qnc_e2e_provider_table_v1/qnc_e2e_op_session_key_generation_v1")
                    && !firstReviewedCandidate.value("operationInvokedByCandidate").toBool(true)
                    && !firstReviewedCandidate.value("inputBytesCaptured").toBool(true)
                    && !firstReviewedCandidate.value("outputBytesCaptured").toBool(true)
                    && !firstReviewedCandidate.value("rawKeyExported").toBool(true)
                    && !firstReviewedCandidate.value("sessionSecretExported").toBool(true),
                "reviewed provider execution candidate should retain sanitized probe references and entrypoint evidence") && ok;
    ok = expect(firstExecutionProbe.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && firstExecutionProbe.value("probeVectorSchema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-probe-vector-v1")
                    && firstExecutionProbe.value("knownAnswerVectorId").toString()
                        == QStringLiteral("production-session-key-generation-vectors-v1/session-key-generation")
                    && firstExecutionProbe.value("knownAnswerFixtureId").toString()
                        == QStringLiteral("probe-fixture/session-key-generation")
                    && firstExecutionProbe.value("probeExecutionFrameSchema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-probe-execution-frame-v1")
                    && firstExecutionProbe.value("probeExecutionEntryPoint").toString()
                        == QStringLiteral("qnc_e2e_provider_table_v1/qnc_e2e_op_session_key_generation_v1")
                    && firstExecutionProbe.value("probeExecutionInputCapturePolicy").toString()
                        == QStringLiteral("size-and-class-only")
                    && firstExecutionProbe.value("probeExecutionOutputCapturePolicy").toString()
                        == QStringLiteral("size-and-class-only")
                    && firstExecutionProbe.value("probeExecutionResultCapturePolicy").toString()
                        == QStringLiteral("status-class-and-size-only")
                    && firstExecutionProbe.value("probeExecutionFrameSanitized").toBool(false)
                    && firstExecutionProbe.value("probeKnownAnswerOutputEvidenceSchema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-probe-output-evidence-v1")
                    && firstExecutionProbe.value("expectedKnownAnswerOutputClass").toString()
                        == QStringLiteral("handle-status-output")
                    && firstExecutionProbe.value("observedKnownAnswerOutputClass").toString()
                        == QStringLiteral("handle-status-output")
                    && firstExecutionProbe.value("expectedOutputShapeClass").toString()
                        == QStringLiteral("handle-status-output")
                    && firstExecutionProbe.value("observedOutputShapeClass").toString()
                        == QStringLiteral("handle-status-output")
                    && firstExecutionProbe.value("expectedOutputClassMatched").toBool(false)
                    && firstExecutionProbe.value("outputEvidenceClass").toString()
                        == QStringLiteral("known-answer-output-shape-matched")
                    && !firstExecutionProbe.value("outputEvidenceFailClosed").toBool(true)
                    && firstExecutionProbe.value("outputEvidenceBlockedReason").toString().isEmpty()
                    && firstExecutionProbe.value("outputShapeHashSha256").toString().size() == 64
                    && firstExecutionProbe.value("inputContractHashSha256").toString().size() == 64
                    && firstExecutionProbe.value("outputContractHashSha256").toString().size() == 64
                    && firstExecutionProbe.value("fixtureInputClass").toString()
                        == QStringLiteral("suite-bound-randomness-fixture")
                    && firstExecutionProbe.value("expectedStatusClass").toString()
                        == QStringLiteral("ok")
                    && firstExecutionProbe.value("expectedFailureClass").toString()
                        == QStringLiteral("none")
                    && firstExecutionProbe.value("expectedMaterialPolicyClass").toString()
                        == QStringLiteral("handle-only")
                    && firstExecutionProbe.value("expectedVectorResultClass").toString()
                        == QStringLiteral("probe-vector-passed")
                    && firstExecutionProbe.value("vectorContractReady").toBool(false)
                    && firstExecutionProbe.value("expectedStatusClassMatched").toBool(false)
                    && firstExecutionProbe.value("expectedFailureClassMatched").toBool(false)
                    && firstExecutionProbe.value("expectedMaterialPolicyClassMatched").toBool(false)
                    && firstExecutionProbe.value("providerVectorSetMatched").toBool(false)
                    && firstExecutionProbe.value("mismatchReason").toString()
                        == QStringLiteral("none")
                    && firstExecutionProbe.value("mismatchSeverity").toString()
                        == QStringLiteral("none")
                    && firstExecutionProbe.value("mismatchScope").toString()
                        == QStringLiteral("none")
                    && firstExecutionProbe.value("tableValidationBlockedReason").toString().isEmpty()
                    && !firstExecutionProbe.value("operationPointerMissing").toBool(true)
                    && firstExecutionProbe.value("operationInvoked").toBool(false)
                    && firstExecutionProbe.value("resultCaptured").toBool(false)
                    && firstExecutionProbe.value("callbackStatusClass").toString()
                        == QStringLiteral("ok")
                    && firstExecutionProbe.value("outputStatusClass").toString()
                        == QStringLiteral("ok")
                    && firstExecutionProbe.value("statusConsistencyClass").toString()
                        == QStringLiteral("provider-status-consistent")
                    && firstExecutionProbe.value("sanitizedErrorClass").toString()
                        == QStringLiteral("ok")
                    && firstExecutionProbe.value("materialPolicyClass").toString()
                        == QStringLiteral("handle-only")
                    && firstExecutionProbe.value("knownAnswerPassed").toBool(false)
                    && !firstExecutionProbe.value("roundTripPassed").toBool(true)
                    && firstExecutionProbe.value("vectorResultClass").toString()
                        == QStringLiteral("probe-vector-passed")
                    && firstExecutionProbe.value("failureClass").toString()
                        == QStringLiteral("none")
                    && firstExecutionProbe.value("probeState").toString()
                        == QStringLiteral("invoked-through-registered-provider-table")
                    && firstExecutionProbe.value("materialExportProof").toString()
                        == QStringLiteral("sizes-and-status-only-no-secret-bytes")
                    && !firstExecutionProbe.value("inputBytesCaptured").toBool(true)
                    && !firstExecutionProbe.value("outputBytesCaptured").toBool(true)
                    && !firstExecutionProbe.value("rawKeyExported").toBool(true),
                "explicit provider invocation probe should report only sanitized status and sizes") && ok;
    const QJsonObject firstProbeExecutionFrame =
        firstExecutionProbe.value("probeExecutionFrame").toObject();
    ok = expect(firstProbeExecutionFrame.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-probe-execution-frame-v1")
                    && firstProbeExecutionFrame.value("operationInvoked").toBool(false)
                    && firstProbeExecutionFrame.value("inputBytesAttached").toBool(false)
                    && !firstProbeExecutionFrame.value("inputBytesCaptured").toBool(true)
                    && !firstProbeExecutionFrame.value("outputBytesCaptured").toBool(true)
                    && firstProbeExecutionFrame.value("resultCaptured").toBool(false)
                    && firstProbeExecutionFrame.value("primaryInputSize").toInt() == 26
                    && firstProbeExecutionFrame.value("secondaryInputSize").toInt() == 28
                    && firstProbeExecutionFrame.value("aadInputSize").toInt() == 22
                    && firstProbeExecutionFrame.value("timeoutPolicy").toString()
                        == QStringLiteral("bounded-explicit-test-probe")
                    && firstProbeExecutionFrame.value("errorPolicy").toString()
                        == QStringLiteral("status-class-only")
                    && firstProbeExecutionFrame.value("materialExportPolicy").toString()
                        == QStringLiteral("sizes-and-status-only-no-secret-bytes")
                    && firstProbeExecutionFrame.value("callbackStatusClass").toString()
                        == QStringLiteral("ok")
                    && firstProbeExecutionFrame.value("vectorResultClass").toString()
                        == QStringLiteral("probe-vector-passed")
                    && !firstProbeExecutionFrame.value("rawKeyExported").toBool(true)
                    && !firstProbeExecutionFrame.value("sessionSecretExported").toBool(true),
                "provider invocation probe execution frame should expose only sanitized call evidence") && ok;
    const QJsonObject firstProbeVectorContract =
        firstExecutionProbe.value("probeVectorContract").toObject();
    ok = expect(firstProbeVectorContract.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-provider-probe-vector-contract-v1")
                    && firstProbeVectorContract.value("inputContract").toArray().contains(
                        QStringLiteral("secure-random-source"))
                    && firstProbeVectorContract.value("outputContract").toArray().contains(
                        QStringLiteral("session-key-handle"))
                    && firstProbeVectorContract.value("contractHashReady").toBool(false)
                    && firstProbeVectorContract.value("materialExportPolicy").toString()
                        == QStringLiteral("sizes-and-status-only-no-secret-bytes")
                    && !firstProbeVectorContract.value("rawKeyExported").toBool(true)
                    && !firstProbeVectorContract.value("privateMaterialExported").toBool(true),
                "provider invocation probe vector contract should expose stable sanitized fixture schema") && ok;
    const QJsonObject agreementVerifyProbe =
        invocationExecutionProbe.value("probes").toArray().at(4).toObject();
    ok = expect(agreementVerifyProbe.value("operation").toString()
                        == QStringLiteral("agreement-verify")
                    && agreementVerifyProbe.value("fixtureInputClass").toString()
                        == QStringLiteral("public-identity-signature-fixture")
                    && agreementVerifyProbe.value("expectedMaterialPolicyClass").toString()
                        == QStringLiteral("public-export-allowed")
                    && agreementVerifyProbe.value("expectedMaterialPolicyClassMatched").toBool(false)
                    && agreementVerifyProbe.value("observedKnownAnswerOutputClass").toString()
                        == QStringLiteral("public-output-shape")
                    && agreementVerifyProbe.value("expectedOutputClassMatched").toBool(false)
                    && agreementVerifyProbe.value("knownAnswerPassed").toBool(false)
                    && agreementVerifyProbe.value("roundTripPassed").toBool(false)
                    && agreementVerifyProbe.value("vectorResultClass").toString()
                        == QStringLiteral("probe-vector-passed"),
                "round-trip eligible provider probe operations should expose pass classification separately") && ok;
    const QJsonObject payloadEncryptProbe =
        invocationExecutionProbe.value("probes").toArray().at(6).toObject();
    const QJsonObject payloadDecryptProbe =
        invocationExecutionProbe.value("probes").toArray().at(7).toObject();
    ok = expect(payloadEncryptProbe.value("observedKnownAnswerOutputClass").toString()
                        == QStringLiteral("sealed-output-shape")
                    && payloadEncryptProbe.value("expectedKnownAnswerOutputClass").toString()
                        == QStringLiteral("sealed-output-shape")
                    && payloadEncryptProbe.value("expectedOutputClassMatched").toBool(false)
                    && payloadEncryptProbe.value("sealedOutputSize").toInt() == 5
                    && payloadDecryptProbe.value("observedKnownAnswerOutputClass").toString()
                        == QStringLiteral("payload-output-shape")
                    && payloadDecryptProbe.value("expectedKnownAnswerOutputClass").toString()
                        == QStringLiteral("payload-output-shape")
                    && payloadDecryptProbe.value("expectedOutputClassMatched").toBool(false)
                    && payloadDecryptProbe.value("publicOutputSize").toInt() == 5,
                "provider invocation probe should classify encrypted and decrypted output shapes separately") && ok;
    qnc_e2e_provider_table_v1 statusMismatchProviderTable = completeProviderTable;
    statusMismatchProviderTable.session_key_generation = statusMismatchProviderOperation;
    statusMismatchProviderTable.identity_key_generation = statusMismatchProviderOperation;
    statusMismatchProviderTable.public_key_derivation = statusMismatchProviderOperation;
    statusMismatchProviderTable.agreement_sign = statusMismatchProviderOperation;
    statusMismatchProviderTable.agreement_verify = statusMismatchProviderOperation;
    statusMismatchProviderTable.session_derive = statusMismatchProviderOperation;
    statusMismatchProviderTable.payload_encrypt = statusMismatchProviderOperation;
    statusMismatchProviderTable.payload_decrypt = statusMismatchProviderOperation;
    g_probeInvocationCount = 0;
    e2eRegisterProductionProviderTable(&statusMismatchProviderTable);
    const QJsonObject statusMismatchProbe =
        e2eProbeProductionCryptoProviderInvocationExecution();
    const QJsonObject firstStatusMismatchProbe =
        statusMismatchProbe.value("probes").toArray().at(0).toObject();
    ok = expect(statusMismatchProbe.value("invokedOperationCount").toInt() == 8
                    && statusMismatchProbe.value("statusMismatchCount").toInt() == 8
                    && statusMismatchProbe.value("providerVectorSetMatchedCount").toInt() == 0
                    && statusMismatchProbe.value("providerVectorSetMismatchCount").toInt() == 8
                    && statusMismatchProbe.value("mismatchReasonSummary").toObject()
                        .value("known-answer-status-mismatch").toInt() == 8
                    && statusMismatchProbe.value("mismatchSeveritySummary").toObject()
                        .value("fail-closed").toInt() == 8
                    && statusMismatchProbe.value("mismatchScopeSummary").toObject()
                        .value("known-answer-vector").toInt() == 8
                    && firstStatusMismatchProbe.value("callbackStatusClass").toString()
                        == QStringLiteral("ok")
                    && firstStatusMismatchProbe.value("outputStatusClass").toString()
                        == QStringLiteral("unsupported")
                    && firstStatusMismatchProbe.value("statusConsistencyClass").toString()
                        == QStringLiteral("provider-status-mismatch")
                    && firstStatusMismatchProbe.value("mismatchReason").toString()
                        == QStringLiteral("known-answer-status-mismatch")
                    && firstStatusMismatchProbe.value("mismatchSeverity").toString()
                        == QStringLiteral("fail-closed")
                    && firstStatusMismatchProbe.value("mismatchScope").toString()
                        == QStringLiteral("known-answer-vector")
                    && g_probeInvocationCount == 8,
                "provider invocation probe should classify status mismatches separately") && ok;
    qnc_e2e_provider_table_v1 materialMismatchProviderTable = completeProviderTable;
    materialMismatchProviderTable.session_key_generation = materialPolicyMismatchProviderOperation;
    materialMismatchProviderTable.identity_key_generation = materialPolicyMismatchProviderOperation;
    materialMismatchProviderTable.public_key_derivation = materialPolicyMismatchProviderOperation;
    materialMismatchProviderTable.agreement_sign = materialPolicyMismatchProviderOperation;
    materialMismatchProviderTable.agreement_verify = materialPolicyMismatchProviderOperation;
    materialMismatchProviderTable.session_derive = materialPolicyMismatchProviderOperation;
    materialMismatchProviderTable.payload_encrypt = materialPolicyMismatchProviderOperation;
    materialMismatchProviderTable.payload_decrypt = materialPolicyMismatchProviderOperation;
    g_probeInvocationCount = 0;
    e2eRegisterProductionProviderTable(&materialMismatchProviderTable);
    const QJsonObject materialMismatchProbe =
        e2eProbeProductionCryptoProviderInvocationExecution();
    const QJsonObject firstMaterialMismatchProbe =
        materialMismatchProbe.value("probes").toArray().at(0).toObject();
    ok = expect(materialMismatchProbe.value("invokedOperationCount").toInt() == 8
                    && materialMismatchProbe.value("statusMismatchCount").toInt() == 0
                    && materialMismatchProbe.value("providerVectorSetMatchedCount").toInt() == 4
                    && materialMismatchProbe.value("providerVectorSetMismatchCount").toInt() == 4
                    && materialMismatchProbe.value("mismatchReasonSummary").toObject()
                        .value("none").toInt() == 4
                    && materialMismatchProbe.value("mismatchReasonSummary").toObject()
                        .value("known-answer-material-policy-mismatch").toInt() == 4
                    && materialMismatchProbe.value("mismatchSeveritySummary").toObject()
                        .value("fail-closed").toInt() == 4
                    && materialMismatchProbe.value("mismatchScopeSummary").toObject()
                        .value("known-answer-vector").toInt() == 4
                    && firstMaterialMismatchProbe.value("materialPolicyClass").toString()
                        == QStringLiteral("public-export-allowed")
                    && firstMaterialMismatchProbe.value("observedOutputShapeClass").toString()
                        == QStringLiteral("empty-output-shape")
                    && firstMaterialMismatchProbe.value("mismatchReason").toString()
                        == QStringLiteral("known-answer-material-policy-mismatch")
                    && firstMaterialMismatchProbe.value("mismatchSeverity").toString()
                        == QStringLiteral("fail-closed")
                    && firstMaterialMismatchProbe.value("mismatchScope").toString()
                        == QStringLiteral("known-answer-vector")
                    && g_probeInvocationCount == 8,
                "provider invocation probe should classify material policy mismatches separately") && ok;
    qnc_e2e_provider_table_v1 mismatchedProviderTable = completeProviderTable;
    mismatchedProviderTable.session_key_generation = mismatchedShapeProviderOperation;
    mismatchedProviderTable.identity_key_generation = mismatchedShapeProviderOperation;
    mismatchedProviderTable.public_key_derivation = mismatchedShapeProviderOperation;
    mismatchedProviderTable.agreement_sign = mismatchedShapeProviderOperation;
    mismatchedProviderTable.agreement_verify = mismatchedShapeProviderOperation;
    mismatchedProviderTable.session_derive = mismatchedShapeProviderOperation;
    mismatchedProviderTable.payload_encrypt = mismatchedShapeProviderOperation;
    mismatchedProviderTable.payload_decrypt = mismatchedShapeProviderOperation;
    g_probeInvocationCount = 0;
    e2eRegisterProductionProviderTable(&mismatchedProviderTable);
    const QJsonObject mismatchedOutputProbe =
        e2eProbeProductionCryptoProviderInvocationExecution();
    ok = expect(mismatchedOutputProbe.value("invokedOperationCount").toInt() == 8
                    && mismatchedOutputProbe.value("expectedOutputClassMatchCount").toInt() == 4
                    && mismatchedOutputProbe.value("expectedOutputClassMismatchCount").toInt() == 4
                    && mismatchedOutputProbe.value("outputEvidenceFailClosedCount").toInt() == 4
                    && mismatchedOutputProbe.value("providerVectorSetMatchedCount").toInt() == 4
                    && mismatchedOutputProbe.value("providerVectorSetMismatchCount").toInt() == 4
                    && mismatchedOutputProbe.value("outputEvidenceClassSummary").toObject()
                        .value("known-answer-output-shape-matched").toInt() == 4
                    && mismatchedOutputProbe.value("outputEvidenceClassSummary").toObject()
                        .value("known-answer-output-shape-mismatch").toInt() == 4
                    && mismatchedOutputProbe.value("mismatchReasonSummary").toObject()
                        .value("none").toInt() == 4
                    && mismatchedOutputProbe.value("mismatchReasonSummary").toObject()
                        .value("known-answer-material-policy-mismatch").toInt() == 4
                    && mismatchedOutputProbe.value("mismatchSeveritySummary").toObject()
                        .value("none").toInt() == 4
                    && mismatchedOutputProbe.value("mismatchSeveritySummary").toObject()
                        .value("fail-closed").toInt() == 4
                    && !mismatchedOutputProbe.value("accepted").toBool(true)
                    && g_probeInvocationCount == 8,
                "provider invocation probe should fail closed on known-answer output shape mismatch") && ok;
    const QJsonObject mismatchedSessionProbe =
        mismatchedOutputProbe.value("probes").toArray().at(0).toObject();
    const QJsonObject mismatchedPayloadEncryptProbe =
        mismatchedOutputProbe.value("probes").toArray().at(6).toObject();
    ok = expect(mismatchedSessionProbe.value("expectedKnownAnswerOutputClass").toString()
                        == QStringLiteral("handle-status-output")
                    && mismatchedSessionProbe.value("observedKnownAnswerOutputClass").toString()
                        == QStringLiteral("public-output-shape")
                    && mismatchedSessionProbe.value("expectedOutputShapeClass").toString()
                        == QStringLiteral("handle-status-output")
                    && mismatchedSessionProbe.value("observedOutputShapeClass").toString()
                        == QStringLiteral("public-output-shape")
                    && !mismatchedSessionProbe.value("expectedOutputClassMatched").toBool(true)
                    && !mismatchedSessionProbe.value("providerVectorSetMatched").toBool(true)
                    && mismatchedSessionProbe.value("mismatchReason").toString()
                        == QStringLiteral("known-answer-material-policy-mismatch")
                    && mismatchedSessionProbe.value("mismatchSeverity").toString()
                        == QStringLiteral("fail-closed")
                    && mismatchedSessionProbe.value("outputEvidenceClass").toString()
                        == QStringLiteral("known-answer-output-shape-mismatch")
                    && mismatchedSessionProbe.value("outputEvidenceFailClosed").toBool(false)
                    && mismatchedSessionProbe.value("outputEvidenceBlockedReason").toString()
                        == QStringLiteral("known-answer-output-shape-mismatch")
                    && mismatchedSessionProbe.value("outputShapeHashSha256").toString().size() == 64
                    && mismatchedPayloadEncryptProbe.value("expectedKnownAnswerOutputClass").toString()
                        == QStringLiteral("sealed-output-shape")
                    && mismatchedPayloadEncryptProbe.value("observedKnownAnswerOutputClass").toString()
                        == QStringLiteral("public-output-shape")
                    && mismatchedPayloadEncryptProbe.value("outputEvidenceFailClosed").toBool(false),
                "mismatched output evidence should remain sanitized and explain fail-closed reason") && ok;
    completeProviderTable.abi = "bad-abi";
    const QJsonObject badAbiTable =
        e2eValidateProductionProviderTable(&completeProviderTable);
    ok = expect(!badAbiTable.value("accepted").toBool(true)
                    && badAbiTable.value("blockedReason").toString()
                        == QStringLiteral("production-provider-table-abi-mismatch"),
                "provider table validation should fail closed on ABI mismatch") && ok;
    e2eRegisterProductionProviderTable(nullptr);
    ok = expectAllOperations(backendStatus,
                             true,
                             QStringLiteral("draft-backend-available"),
                             "default draft backend should allow every operation in the matrix") && ok;
    ok = expect(backendStatus.value("operations").toObject()
                    .value("payload-encrypt").toObject()
                    .value("implementation").toString() == QStringLiteral("draft-qt-primitives")
                    && backendStatus.value("operations").toObject()
                        .value("payload-encrypt").toObject()
                        .value("entrypoint").toString() == QStringLiteral("draft/payload-encrypt")
                    && backendStatus.value("operations").toObject()
                        .value("payload-encrypt").toObject()
                        .value("providerId").toString() == QStringLiteral("draft-qt-provider-v1")
                    && backendStatus.value("operations").toObject()
                        .value("payload-encrypt").toObject()
                        .value("dispatchState").toString() == QStringLiteral("draft-dispatch-ready")
                    && backendStatus.value("operations").toObject()
                        .value("payload-encrypt").toObject()
                        .value("providerSelfTestStatus").toString()
                            == QStringLiteral("development-only-self-test-passed")
                    && backendStatus.value("operations").toObject()
                        .value("payload-encrypt").toObject()
                        .value("providerCompatibilityStatus").toString()
                            == QStringLiteral("development-known-answer-passed")
                    && backendStatus.value("operations").toObject()
                        .value("payload-encrypt").toObject()
                        .value("adapterLinked").toBool(false),
                "operation matrix should disclose the selected draft implementation") && ok;
    ok = expect(e2eCryptoBackendAvailable(&reason) && reason.isEmpty(),
                "draft backend should be available when production crypto is not required") && ok;

    const QJsonObject agreementJson = agreement.toJson();
    ok = expect(agreementJson.value("protocol").toString() == "qtnetworkchat-e2e-v1",
                "key agreement protocol should normalize") && ok;
    ok = expect(!agreementJson.value("publicKeyFingerprintSha256").toString().isEmpty(),
                "key agreement should expose public key fingerprint") && ok;
    ok = expect(agreementJson.value("senderIdentityFingerprintSha256").toString() == agreement.senderIdentityFingerprint
                    && agreementJson.value("receiverIdentityFingerprintSha256").toString() == agreement.receiverIdentityFingerprint,
                "key agreement should bind both identity fingerprints") && ok;

    const E2EKeyAgreement restoredAgreement = E2EKeyAgreement::fromJson(agreementJson);
    ok = expect(restoredAgreement.publicKey == agreement.publicKey,
                "key agreement public key should round-trip") && ok;
    ok = expect(restoredAgreement.senderIdentityFingerprint == agreement.senderIdentityFingerprint
                    && restoredAgreement.receiverIdentityFingerprint == agreement.receiverIdentityFingerprint,
                "key agreement identity fingerprints should round-trip") && ok;
    ok = expect(restoredAgreement.signature == agreement.signature,
                "key agreement signature should round-trip") && ok;

    E2EEnvelope envelope;
    envelope.protocol = "qtnetworkchat-e2e-v1";
    envelope.suite = "x25519-hkdf-sha256-aes-256-gcm";
    envelope.senderId = "10001";
    envelope.receiverId = "10002";
    envelope.keyId = "alice-bob-1";
    envelope.nonce = QByteArray("123456789012", 12);
    envelope.ciphertext = QByteArray("opaque ciphertext", 17);
    envelope.tag = QByteArray("1234567890abcdef", 16);
    envelope.aad = "sender=10001;receiver=10002";

    ok = expect(envelope.isValid(&reason) && reason.isEmpty(),
                "valid envelope should pass") && ok;
    const QJsonObject envelopeJson = envelope.toJson();
    ok = expect(envelopeJson.value("ciphertext").toString() != QString::fromLatin1(envelope.ciphertext),
                "ciphertext should be base64url encoded in JSON") && ok;
    ok = expect(!envelopeJson.value("ciphertextSha256").toString().isEmpty(),
                "envelope should expose ciphertext fingerprint") && ok;

    const E2EEnvelope restoredEnvelope = E2EEnvelope::fromJson(envelopeJson);
    ok = expect(restoredEnvelope.nonce == envelope.nonce,
                "envelope nonce should round-trip") && ok;
    ok = expect(restoredEnvelope.ciphertext == envelope.ciphertext,
                "envelope ciphertext should round-trip") && ok;
    ok = expect(restoredEnvelope.tag == envelope.tag,
                "envelope tag should round-trip") && ok;

    E2EEnvelope invalid = envelope;
    invalid.protocol = "unknown";
    ok = expect(!invalid.isValid(&reason) && reason == "unsupported-protocol",
                "unsupported protocol should fail closed") && ok;
    invalid = envelope;
    invalid.nonce = QByteArray("short");
    ok = expect(!invalid.isValid(&reason) && reason == "invalid-nonce",
                "short nonce should fail closed") && ok;
    invalid = envelope;
    invalid.ciphertext.clear();
    ok = expect(!invalid.isValid(&reason) && reason == "empty-ciphertext",
                "empty ciphertext should fail closed") && ok;
    invalid = envelope;
    invalid.senderId = invalid.receiverId;
    ok = expect(!invalid.isValid(&reason) && reason == "invalid-peer",
                "same sender and receiver should fail closed") && ok;

    E2EKeyAgreement invalidAgreement = agreement;
    invalidAgreement.publicKey.clear();
    ok = expect(!invalidAgreement.isValid(&reason) && reason == "invalid-public-key",
                "missing public key should fail closed") && ok;
    invalidAgreement = agreement;
    invalidAgreement.senderIdentityFingerprint = "not-a-fingerprint";
    ok = expect(!invalidAgreement.isValid(&reason) && reason == "invalid-identity-fingerprint",
                "malformed identity fingerprints should fail closed") && ok;

    const QByteArray alicePrivateKey = generateE2EPrivateKey();
    const QByteArray bobPrivateKey = generateE2EPrivateKey();
    const QByteArray alicePublicKey = e2ePublicKeyFromPrivateKey(alicePrivateKey);
    const QByteArray bobPublicKey = e2ePublicKeyFromPrivateKey(bobPrivateKey);
    ok = expect(!alicePrivateKey.isEmpty() && !bobPrivateKey.isEmpty()
                    && !alicePublicKey.isEmpty() && !bobPublicKey.isEmpty()
                    && alicePrivateKey != alicePublicKey,
                "draft key agreement should expose only derived public material") && ok;

    E2EKeyAgreement aliceAgreement;
    aliceAgreement.protocol = "qtnetworkchat-e2e-v1";
    aliceAgreement.suite = "draft-placeholder";
    aliceAgreement.senderId = "10001";
    aliceAgreement.receiverId = "10002";
    aliceAgreement.keyId = "alice-bob-auth-1";
    aliceAgreement.publicKey = alicePublicKey;
    aliceAgreement.senderIdentityFingerprint = QString(64, QLatin1Char('1'));
    aliceAgreement.receiverIdentityFingerprint = QString(64, QLatin1Char('2'));
    E2EKeyAgreement bobAgreement;
    bobAgreement.protocol = "qtnetworkchat-e2e-v1";
    bobAgreement.suite = "draft-placeholder";
    bobAgreement.senderId = "10002";
    bobAgreement.receiverId = "10001";
    bobAgreement.keyId = "alice-bob-auth-1-response";
    bobAgreement.publicKey = bobPublicKey;
    bobAgreement.senderIdentityFingerprint = aliceAgreement.receiverIdentityFingerprint;
    bobAgreement.receiverIdentityFingerprint = aliceAgreement.senderIdentityFingerprint;
    const QByteArray aliceIdentityPrivateKey = generateE2EPrivateKey();
    const QByteArray bobIdentityPrivateKey = generateE2EPrivateKey();
    const QByteArray aliceIdentityPublicKey = e2ePublicKeyFromPrivateKey(aliceIdentityPrivateKey);
    const QByteArray bobIdentityPublicKey = e2ePublicKeyFromPrivateKey(bobIdentityPrivateKey);
    aliceAgreement.senderIdentityFingerprint = e2eFingerprint(aliceIdentityPublicKey);
    aliceAgreement.receiverIdentityFingerprint = e2eFingerprint(bobIdentityPublicKey);
    bobAgreement.senderIdentityFingerprint = e2eFingerprint(bobIdentityPublicKey);
    bobAgreement.receiverIdentityFingerprint = e2eFingerprint(aliceIdentityPublicKey);
    ok = expect(signE2EKeyAgreement(&aliceAgreement, aliceIdentityPrivateKey, &reason)
                    && !aliceAgreement.signature.isEmpty()
                    && reason.isEmpty(),
                "sender should sign key agreement with local identity material") && ok;
    ok = expect(signE2EKeyAgreement(&bobAgreement, bobIdentityPrivateKey, &reason)
                    && !bobAgreement.signature.isEmpty(),
                "receiver should sign key agreement with local identity material") && ok;
    ok = expect(verifyE2EKeyAgreementSignature(aliceAgreement, aliceIdentityPublicKey, &reason)
                    && verifyE2EKeyAgreementSignature(bobAgreement, bobIdentityPublicKey, &reason),
                "signed key agreements should verify against pinned identity public material") && ok;
    E2EKeyAgreement unsignedAgreement = aliceAgreement;
    unsignedAgreement.signature.clear();
    ok = expect(!verifyE2EKeyAgreementSignature(unsignedAgreement, aliceIdentityPublicKey, &reason)
                    && reason == QStringLiteral("missing-signature"),
                "unsigned key agreement should fail closed") && ok;
    E2EKeyAgreement tamperedSignatureAgreement = aliceAgreement;
    tamperedSignatureAgreement.publicKey = e2ePublicKeyFromPrivateKey(generateE2EPrivateKey());
    ok = expect(!verifyE2EKeyAgreementSignature(tamperedSignatureAgreement, aliceIdentityPublicKey, &reason)
                    && reason == QStringLiteral("signature-mismatch"),
                "tampered agreement public material should fail signature verification") && ok;
    tamperedSignatureAgreement = aliceAgreement;
    tamperedSignatureAgreement.senderIdentityFingerprint = e2eFingerprint(bobIdentityPublicKey);
    ok = expect(!verifyE2EKeyAgreementSignature(tamperedSignatureAgreement, aliceIdentityPublicKey, &reason)
                    && reason == QStringLiteral("identity-key-mismatch"),
                "agreement signed by a different identity should fail closed") && ok;

    const QByteArray aliceDerived = deriveE2EAuthenticatedSessionKey(alicePrivateKey, aliceAgreement, bobAgreement, &reason);
    const QByteArray bobDerived = deriveE2EAuthenticatedSessionKey(bobPrivateKey, bobAgreement, aliceAgreement, &reason);
    ok = expect(aliceDerived.size() == 32 && aliceDerived == bobDerived && reason.isEmpty(),
                "authenticated key agreement should derive the same session without sending it") && ok;
    E2EKeyAgreement tamperedAgreement = bobAgreement;
    tamperedAgreement.receiverIdentityFingerprint = QString(64, QLatin1Char('3'));
    ok = expect(deriveE2EAuthenticatedSessionKey(alicePrivateKey, aliceAgreement, tamperedAgreement, &reason).isEmpty()
                    && reason == "transcript-identity-mismatch",
                "identity-bound transcript mismatch should fail closed") && ok;
    tamperedAgreement = bobAgreement;
    tamperedAgreement.publicKey = e2ePublicKeyFromPrivateKey(generateE2EPrivateKey());
    ok = expect(deriveE2EAuthenticatedSessionKey(alicePrivateKey, aliceAgreement, tamperedAgreement, &reason) != aliceDerived,
                "changing public agreement material should change the derived session") && ok;

    const QByteArray sessionKey = generateE2ESessionKey();
    ok = expect(sessionKey.size() == 32,
                "generated session key should use 32 bytes") && ok;

    const QString plaintext = "encrypted hello";
    const E2EEnvelope encrypted = encryptE2EText("10001", "10002", "alice-bob-2", sessionKey, plaintext, &reason);
    ok = expect(encrypted.isValid(&reason) && reason.isEmpty(),
                "encrypted text envelope should be valid") && ok;
    ok = expect(encrypted.ciphertext != plaintext.toUtf8(),
                "encrypted text should not expose plaintext bytes") && ok;

    QString decrypted;
    ok = expect(decryptE2EText(encrypted, sessionKey, &decrypted, &reason) && decrypted == plaintext,
                "encrypted text should decrypt with the matching session key") && ok;

    E2EEnvelope tampered = encrypted;
    tampered.ciphertext[0] = static_cast<char>(tampered.ciphertext[0] ^ 0x01);
    ok = expect(!decryptE2EText(tampered, sessionKey, &decrypted, &reason)
                    && reason == "authentication-failed",
                "tampered ciphertext should fail authentication") && ok;

    ok = expect(!decryptE2EText(encrypted, QByteArray("too-short"), &decrypted, &reason)
                    && reason == "invalid-session-key",
                "short session keys should fail closed") && ok;

    const QByteArray binaryPayload("\x00\x01binary file payload\x7f", 22);
    const E2EEnvelope encryptedPayload = encryptE2EPayload("10001",
                                                           "10002",
                                                           "alice-bob-file-1",
                                                           sessionKey,
                                                           binaryPayload,
                                                           QStringLiteral("file/private/v1;transfer-1"),
                                                           &reason);
    ok = expect(encryptedPayload.isValid(&reason)
                    && encryptedPayload.aad.startsWith(QStringLiteral("file/private/v1"))
                    && encryptedPayload.ciphertext != binaryPayload,
                "encrypted binary payload should be valid and opaque") && ok;
    QByteArray decryptedPayload;
    ok = expect(decryptE2EPayload(encryptedPayload, sessionKey, &decryptedPayload, &reason)
                    && decryptedPayload == binaryPayload,
                "encrypted binary payload should decrypt with the matching session key") && ok;
    E2EEnvelope tamperedPayload = encryptedPayload;
    tamperedPayload.aad.append(QStringLiteral(";tampered"));
    ok = expect(!decryptE2EPayload(tamperedPayload, sessionKey, &decryptedPayload, &reason)
                    && reason == QStringLiteral("authentication-failed"),
                "binary payload aad tampering should fail authentication") && ok;

    qputenv("QTNETWORKCHAT_E2E_REQUIRE_PRODUCTION_CRYPTO", "1");
    const QJsonObject requiredBackendStatus = e2eCryptoBackendStatus();
    ok = expect(requiredBackendStatus.value("productionRequired").toBool(false)
                    && !requiredBackendStatus.value("available").toBool(true)
                    && requiredBackendStatus.value("selectedBackendId").toString().isEmpty()
                    && requiredBackendStatus.value("unavailableReason").toString()
                        == QStringLiteral("production-crypto-backend-unavailable")
                    && requiredBackendStatus.value("status").toString()
                        == QStringLiteral("production-crypto-backend-unavailable"),
                "production-required mode should report the draft backend as unavailable") && ok;
    ok = expectAllOperations(requiredBackendStatus,
                             false,
                             QStringLiteral("production-crypto-backend-unavailable"),
                             "production-required mode should block every backend operation") && ok;
    ok = expect(requiredBackendStatus.value("operations").toObject()
                    .value("agreement-sign").toObject()
                    .value("operatorAction").toString()
                        == QStringLiteral("link-production-crypto-backend-or-disable-requirement"),
                "production-required operation status should include an operator action") && ok;
    ok = expect(generateE2ESessionKey().isEmpty(),
                "production-required mode should not generate draft session keys") && ok;
    ok = expect(generateE2EPrivateKey().isEmpty(),
                "production-required mode should not generate draft private keys") && ok;
    E2EKeyAgreement blockedAgreement = aliceAgreement;
    blockedAgreement.signature.clear();
    ok = expect(!signE2EKeyAgreement(&blockedAgreement, aliceIdentityPrivateKey, &reason)
                    && reason == QStringLiteral("production-crypto-backend-unavailable")
                    && blockedAgreement.signature.isEmpty(),
                "production-required mode should block draft agreement signatures") && ok;
    ok = expect(!verifyE2EKeyAgreementSignature(aliceAgreement, aliceIdentityPublicKey, &reason)
                    && reason == QStringLiteral("production-crypto-backend-unavailable"),
                "production-required mode should block draft signature verification") && ok;
    ok = expect(deriveE2EAuthenticatedSessionKey(alicePrivateKey, aliceAgreement, bobAgreement, &reason).isEmpty()
                    && reason == QStringLiteral("production-crypto-backend-unavailable"),
                "production-required mode should block draft session derivation") && ok;
    ok = expect(!encryptE2EPayload("10001",
                                   "10002",
                                   "blocked-production-required",
                                   sessionKey,
                                   binaryPayload,
                                   QStringLiteral("file/private/v1"),
                                   &reason).isValid()
                    && reason == QStringLiteral("production-crypto-backend-unavailable"),
                "production-required mode should block draft payload encryption") && ok;
    ok = expect(!decryptE2EPayload(encryptedPayload, sessionKey, &decryptedPayload, &reason)
                    && reason == QStringLiteral("production-crypto-backend-unavailable"),
                "production-required mode should block draft payload decryption") && ok;
    qunsetenv("QTNETWORKCHAT_E2E_REQUIRE_PRODUCTION_CRYPTO");

    qputenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND", "production");
    const QJsonObject productionRequestedStatus = e2eCryptoBackendStatus();
    const QJsonObject productionRequestedAcceptance =
        e2eProductionCryptoAcceptanceStatus();
    const QJsonObject productionRequestedHarness =
        e2eProductionCryptoOperationHarnessStatus();
    const QJsonObject productionRequestedExecutionPlan =
        e2eProductionCryptoOperationExecutionPlanStatus();
    const QJsonObject productionRequestedInvocation =
        e2eProductionCryptoOperationInvocationStatus();
    const QJsonObject productionRequestedSlots =
        e2eProductionCryptoOperationSlotStatus();
    const QJsonObject productionRequestedCallableManifest =
        e2eProductionCryptoOperationCallableManifestStatus();
    ok = expect(productionRequestedStatus.value("requestedBackendId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1")
                    && productionRequestedStatus.value("selectionSource").toString()
                        == QStringLiteral("environment")
                    && !productionRequestedStatus.value("available").toBool(true)
                    && productionRequestedStatus.value("selectedBackendId").toString().isEmpty()
                    && !productionRequestedStatus.value("productionAdapterRequested").toBool(true)
                    && !productionRequestedStatus.value("productionAdapterLinked").toBool(true)
                    && productionRequestedStatus.value("productionAdapterReason").toString()
                        == QStringLiteral("production-adapter-not-requested")
                    && productionRequestedStatus.value("unavailableReason").toString()
                        == QStringLiteral("production-crypto-backend-unavailable"),
                "explicit production backend request should fail closed until the adapter is linked") && ok;
    ok = expect(productionRequestedAcceptance.value("backendId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1")
                    && productionRequestedAcceptance.value("releaseGate").toString()
                        == QStringLiteral("production-adapter-not-linked")
                    && productionRequestedAcceptance.value("operatorAction").toString()
                        == QStringLiteral("link-reviewed-production-crypto-backend")
                    && !productionRequestedAcceptance.value("accepted").toBool(true)
                    && !productionRequestedAcceptance.value("linked").toBool(true)
                    && !productionRequestedAcceptance.value("productionReady").toBool(true)
                    && productionRequestedAcceptance.value("registeredOperationCount").toInt() == 8
                    && productionRequestedAcceptance.value("blockedOperationCount").toInt() == 8
                    && productionRequestedAcceptance.value("operationManifest").toArray().size() == 8
                    && productionRequestedAcceptance.value("implementedOperationCount").toInt() == 0
                    && !productionRequestedAcceptance.value("rawKeyExported").toBool(true)
                    && !productionRequestedAcceptance.value("privateMaterialExported").toBool(true),
                "production acceptance status should summarize the not-linked release gate") && ok;
    ok = expect(productionRequestedHarness.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-harness-v1")
                    && productionRequestedHarness.value("releaseGate").toString()
                        == QStringLiteral("production-operation-harness-blocked-not-linked")
                    && productionRequestedHarness.value("blockedReason").toString()
                        == QStringLiteral("production-crypto-backend-unavailable")
                    && productionRequestedHarness.value("runnableOperationCount").toInt() == 0
                    && productionRequestedHarness.value("blockedOperationCount").toInt() == 8
                    && productionRequestedHarness.value("operations").toArray().size() == 8
                    && !productionRequestedHarness.value("rawKeyExported").toBool(true)
                    && !productionRequestedHarness.value("privateMaterialExported").toBool(true),
                "production operation harness should expose blocked not-linked execution evidence") && ok;
    ok = expect(productionRequestedExecutionPlan.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-execution-plan-v1")
                    && productionRequestedExecutionPlan.value("backendId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1")
                    && productionRequestedExecutionPlan.value("releaseGate").toString()
                        == QStringLiteral("production-operation-execution-plan-blocked-not-linked")
                    && productionRequestedExecutionPlan.value("blockedReason").toString()
                        == QStringLiteral("production-crypto-backend-unavailable")
                    && !productionRequestedExecutionPlan.value("accepted").toBool(true)
                    && !productionRequestedExecutionPlan.value("planReady").toBool(true)
                    && productionRequestedExecutionPlan.value("runnableStepCount").toInt() == 0
                    && productionRequestedExecutionPlan.value("blockedStepCount").toInt() == 8
                    && productionRequestedExecutionPlan.value("steps").toArray().size() == 8
                    && !productionRequestedExecutionPlan.value("rawKeyExported").toBool(true)
                    && !productionRequestedExecutionPlan.value("privateMaterialExported").toBool(true),
                "production operation execution plan should publish blocked not-linked step evidence") && ok;
    ok = expect(productionRequestedInvocation.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-invocation-v1")
                    && productionRequestedInvocation.value("backendId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1")
                    && productionRequestedInvocation.value("releaseGate").toString()
                        == QStringLiteral("production-operation-invocation-blocked-not-linked")
                    && productionRequestedInvocation.value("blockedReason").toString()
                        == QStringLiteral("production-crypto-backend-unavailable")
                    && !productionRequestedInvocation.value("accepted").toBool(true)
                    && productionRequestedInvocation.value("callableOperationCount").toInt() == 0
                    && productionRequestedInvocation.value("blockedOperationCount").toInt() == 8
                    && productionRequestedInvocation.value("invocations").toArray().size() == 8
                    && !productionRequestedInvocation.value("rawKeyExported").toBool(true)
                    && !productionRequestedInvocation.value("privateMaterialExported").toBool(true),
                "production operation invocation status should publish blocked not-linked call contracts") && ok;
    ok = expect(productionRequestedSlots.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-slots-v1")
                    && productionRequestedSlots.value("backendId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1")
                    && productionRequestedSlots.value("releaseGate").toString()
                        == QStringLiteral("production-operation-slots-blocked-not-linked")
                    && productionRequestedSlots.value("blockedReason").toString()
                        == QStringLiteral("production-crypto-backend-unavailable")
                    && !productionRequestedSlots.value("accepted").toBool(true)
                    && productionRequestedSlots.value("reviewedSlotCount").toInt() == 0
                    && productionRequestedSlots.value("callableSlotCount").toInt() == 0
                    && productionRequestedSlots.value("blockedSlotCount").toInt() == 8
                    && productionRequestedSlots.value("slots").toArray().size() == 8
                    && !productionRequestedSlots.value("rawKeyExported").toBool(true)
                    && !productionRequestedSlots.value("privateMaterialExported").toBool(true),
                "production operation slots should publish blocked not-linked reviewed slot evidence") && ok;
    ok = expect(productionRequestedCallableManifest.value("schema").toString()
                        == QStringLiteral("qtnetworkchat-e2e-production-operation-callable-manifest-v1")
                    && productionRequestedCallableManifest.value("backendId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1")
                    && productionRequestedCallableManifest.value("releaseGate").toString()
                        == QStringLiteral("production-operation-callable-manifest-blocked-not-linked")
                    && productionRequestedCallableManifest.value("blockedReason").toString()
                        == QStringLiteral("production-crypto-backend-unavailable")
                    && !productionRequestedCallableManifest.value("accepted").toBool(true)
                    && productionRequestedCallableManifest.value("reviewedCallableCount").toInt() == 0
                    && productionRequestedCallableManifest.value("blockedCallableCount").toInt() == 8
                    && productionRequestedCallableManifest.value("abiMismatchCount").toInt() == 0
                    && productionRequestedCallableManifest.value("fixtureMismatchCount").toInt() == 0
                    && productionRequestedCallableManifest.value("callables").toArray().size() == 8
                    && !productionRequestedCallableManifest.value("rawKeyExported").toBool(true)
                    && !productionRequestedCallableManifest.value("privateMaterialExported").toBool(true),
                "production callable manifest should publish blocked not-linked provider ABI evidence") && ok;
    const QJsonObject productionExecutionFirstStep =
        productionRequestedExecutionPlan.value("steps").toArray().at(0).toObject();
    ok = expect(productionExecutionFirstStep.value("sequenceIndex").toInt(-1) == 0
                    && productionExecutionFirstStep.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && productionExecutionFirstStep.value("entrypoint").toString()
                        == QStringLiteral("production-adapter/session-key-generation")
                    && productionExecutionFirstStep.value("fixtureHashSha256").toString().size() == 64
                    && productionExecutionFirstStep.value("vectorSet").toString()
                        == QStringLiteral("production-session-key-generation-vectors-v1")
                    && productionExecutionFirstStep.value("releaseGate").toString()
                        == QStringLiteral("production-operation-harness-blocked-not-linked")
                    && productionExecutionFirstStep.value("blockedReason").toString()
                        == QStringLiteral("production-session-key-generation-not-implemented")
                    && productionExecutionFirstStep.value("requiresHarnessRunnable").toBool(false)
                    && !productionExecutionFirstStep.value("harnessRunnable").toBool(true)
                    && !productionExecutionFirstStep.value("rawKeyExported").toBool(true)
                    && !productionExecutionFirstStep.value("privateMaterialExported").toBool(true),
                "production operation execution plan should order sanitized harness-backed steps") && ok;
    const QJsonObject productionFirstInvocation =
        productionRequestedInvocation.value("invocations").toArray().at(0).toObject();
    ok = expect(productionFirstInvocation.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && productionFirstInvocation.value("entrypoint").toString()
                        == QStringLiteral("production-adapter/session-key-generation")
                    && productionFirstInvocation.value("inputContract").toArray().size() == 2
                    && productionFirstInvocation.value("outputContract").toArray().size() == 2
                    && productionFirstInvocation.value("inputContract").toArray().at(0).toString()
                        == QStringLiteral("secure-random-source")
                    && productionFirstInvocation.value("outputContract").toArray().at(0).toString()
                        == QStringLiteral("session-key-handle")
                    && productionFirstInvocation.value("fixtureHashSha256").toString().size() == 64
                    && productionFirstInvocation.value("invocationState").toString()
                        == QStringLiteral("blocked-not-linked")
                    && !productionFirstInvocation.value("callable").toBool(true)
                    && !productionFirstInvocation.value("rawKeyExported").toBool(true)
                    && !productionFirstInvocation.value("privateMaterialExported").toBool(true),
                "production invocation contract should define sanitized inputs and outputs for future reviewed calls") && ok;
    const QJsonObject productionFirstSlot =
        productionRequestedSlots.value("slots").toArray().at(0).toObject();
    ok = expect(productionFirstSlot.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && productionFirstSlot.value("slotId").toString()
                        == QStringLiteral("openssl-reviewed-adapter-v1/session-key-generation-slot")
                    && productionFirstSlot.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && productionFirstSlot.value("migrationPhase").toString()
                        == QStringLiteral("session-bootstrap")
                    && productionFirstSlot.value("reviewState").toString()
                        == QStringLiteral("not-linked")
                    && productionFirstSlot.value("materialPolicy").toString()
                        == QStringLiteral("handle-based-no-private-material-export")
                    && !productionFirstSlot.value("reviewed").toBool(true)
                    && !productionFirstSlot.value("callable").toBool(true)
                    && !productionFirstSlot.value("rawKeyExported").toBool(true),
                "production operation slots should define stable reviewed provider symbols and material policy") && ok;
    const QJsonObject productionFirstCallable =
        productionRequestedCallableManifest.value("callables").toArray().at(0).toObject();
    ok = expect(productionFirstCallable.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && productionFirstCallable.value("providerSymbol").toString()
                        == QStringLiteral("qnc_e2e_op_session_key_generation_v1")
                    && productionFirstCallable.value("providerAbiSignature").toString()
                        .contains(QStringLiteral("qnc_e2e_op_session_key_generation_v1"))
                    && productionFirstCallable.value("symbolMatches").toBool(false)
                    && productionFirstCallable.value("abiSignatureMatches").toBool(false)
                    && productionFirstCallable.value("fixtureHashMatches").toBool(false)
                    && productionFirstCallable.value("inputContract").toArray().at(0).toString()
                        == QStringLiteral("secure-random-source")
                    && productionFirstCallable.value("outputContract").toArray().at(0).toString()
                        == QStringLiteral("session-key-handle")
                    && !productionFirstCallable.value("callable").toBool(true)
                    && !productionFirstCallable.value("reviewed").toBool(true)
                    && !productionFirstCallable.value("rawKeyExported").toBool(true),
                "production callable manifest should tie each operation to reviewed ABI and fixture evidence") && ok;
    const QJsonObject productionHarnessFirstOperation =
        productionRequestedHarness.value("operations").toArray().at(0).toObject();
    ok = expect(productionHarnessFirstOperation.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && !productionHarnessFirstOperation.value("runnable").toBool(true)
                    && productionHarnessFirstOperation.value("fixtureHashSha256").toString().size() == 64
                    && productionHarnessFirstOperation.value("blockedReason").toString()
                        == QStringLiteral("production-session-key-generation-not-implemented")
                    && !productionHarnessFirstOperation.value("rawKeyExported").toBool(true)
                    && !productionHarnessFirstOperation.value("privateMaterialExported").toBool(true),
                "production operation harness should publish sanitized fixture evidence for each operation") && ok;
    const QJsonObject productionSessionKeyGate =
        productionRequestedAcceptance.value("operationGates").toArray().at(0).toObject();
    ok = expect(productionSessionKeyGate.value("operation").toString()
                        == QStringLiteral("session-key-generation")
                    && productionSessionKeyGate.value("implementationState").toString()
                        == QStringLiteral("not-linked")
                    && productionSessionKeyGate.value("vectorSet").toString()
                        == QStringLiteral("production-session-key-generation-vectors-v1")
                    && productionSessionKeyGate.value("migrationBlocker").toString()
                        == QStringLiteral("production-session-key-generation-not-implemented")
                    && productionSessionKeyGate.value("operationImplementation").toObject()
                        .value("implemented").toBool(true) == false,
                "production acceptance gates should expose each required operation implementation slot") && ok;
    ok = expect(productionRequestedAcceptance.value("operationCallableManifest").toObject()
                    .value("callables").toArray().size() == 8
                    && !productionRequestedAcceptance.value("operationCallableManifest").toObject()
                        .value("accepted").toBool(true),
                "production acceptance should include callable manifest gate evidence") && ok;
    ok = expectAllOperations(productionRequestedStatus,
                             false,
                             QStringLiteral("production-crypto-backend-unavailable"),
                             "explicit production backend request should block every backend operation") && ok;
    ok = expect(productionRequestedStatus.value("operations").toObject()
                    .value("payload-decrypt").toObject()
                    .value("implementation").toString() == QStringLiteral("production-adapter")
                    && productionRequestedStatus.value("operations").toObject()
                        .value("payload-decrypt").toObject()
                        .value("entrypoint").toString() == QStringLiteral("production-adapter/payload-decrypt")
                    && productionRequestedStatus.value("operations").toObject()
                        .value("payload-decrypt").toObject()
                        .value("providerId").toString() == QStringLiteral("openssl-reviewed-provider-v1")
                    && productionRequestedStatus.value("operations").toObject()
                        .value("payload-decrypt").toObject()
                        .value("dispatchState").toString() == QStringLiteral("not-linked")
                    && productionRequestedStatus.value("operations").toObject()
                        .value("payload-decrypt").toObject()
                        .value("operationImplementation").toObject()
                        .value("vectorSet").toString()
                            == QStringLiteral("production-payload-decrypt-vectors-v1")
                    && productionRequestedStatus.value("operations").toObject()
                        .value("payload-decrypt").toObject()
                        .value("operationImplementation").toObject()
                        .value("migrationBlocker").toString()
                            == QStringLiteral("production-payload-decrypt-not-implemented")
                    && productionRequestedStatus.value("selectedProviderReadiness").toObject()
                        .value("readinessGate").toString() == QStringLiteral("production-adapter-not-linked")
                    && productionRequestedStatus.value("selectedProviderCompatibility").toObject()
                        .value("gate").toString() == QStringLiteral("production-adapter-not-linked")
                    && !productionRequestedStatus.value("selectedProviderCompatibility").toObject()
                        .value("knownAnswerPassed").toBool(true)
                    && productionRequestedStatus.value("operations").toObject()
                        .value("payload-decrypt").toObject()
                        .value("providerReadinessGate").toString()
                            == QStringLiteral("production-adapter-not-linked")
                    && productionRequestedStatus.value("operations").toObject()
                        .value("payload-decrypt").toObject()
                        .value("providerCompatibilityGate").toString()
                            == QStringLiteral("production-adapter-not-linked")
                    && productionRequestedStatus.value("operations").toObject()
                        .value("payload-decrypt").toObject()
                        .value("requiresProductionReady").toBool(false) == true
                    && !productionRequestedStatus.value("operations").toObject()
                        .value("payload-decrypt").toObject()
                        .value("adapterLinked").toBool(true),
                "explicit production operation status should point at the adapter boundary") && ok;
    ok = expect(!e2eCryptoBackendAvailable(&reason)
                    && reason == QStringLiteral("production-crypto-backend-unavailable"),
                "production backend availability helper should refuse an unlinked adapter") && ok;
    ok = expect(generateE2ESessionKey().isEmpty(),
                "explicit production backend request should not fall back to draft session keys") && ok;
    ok = expect(!signE2EKeyAgreement(&blockedAgreement, aliceIdentityPrivateKey, &reason)
                    && reason == QStringLiteral("production-crypto-backend-unavailable"),
                "explicit production backend request should block draft agreement signatures") && ok;
    qunsetenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND");

    qputenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND", "experimental:bad/backend");
    const QJsonObject unsupportedBackendStatus = e2eCryptoBackendStatus();
    ok = expect(unsupportedBackendStatus.value("requestedBackendId").toString()
                        == QStringLiteral("experimentalbadbackend")
                    && unsupportedBackendStatus.value("selectionSource").toString()
                        == QStringLiteral("environment")
                    && !unsupportedBackendStatus.value("available").toBool(true)
                    && unsupportedBackendStatus.value("unavailableReason").toString()
                        == QStringLiteral("unsupported-crypto-backend"),
                "unsupported backend requests should be sanitized and fail closed") && ok;
    ok = expectAllOperations(unsupportedBackendStatus,
                             false,
                             QStringLiteral("unsupported-crypto-backend"),
                             "unsupported backend request should block every backend operation") && ok;
    ok = expect(unsupportedBackendStatus.value("operations").toObject()
                    .value("session-derive").toObject()
                    .value("operatorAction").toString()
                        == QStringLiteral("choose-a-registered-crypto-backend")
                    && unsupportedBackendStatus.value("operations").toObject()
                        .value("session-derive").toObject()
                        .value("entrypoint").toString() == QStringLiteral("unsupported/session-derive")
                    && unsupportedBackendStatus.value("operations").toObject()
                        .value("session-derive").toObject()
                        .value("providerId").toString() == QStringLiteral("none")
                    && unsupportedBackendStatus.value("operations").toObject()
                        .value("session-derive").toObject()
                        .value("dispatchState").toString() == QStringLiteral("unsupported-backend"),
                "unsupported operation status should tell operators to pick a registered backend") && ok;
    ok = expect(generateE2EPrivateKey().isEmpty(),
                "unsupported backend request should not generate draft private keys") && ok;
    qunsetenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND");

    return ok ? 0 : 1;
}
