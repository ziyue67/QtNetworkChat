#include "e2e_backend_status_p.h"
#include <QCryptographicHash>
#include <QHash>
#include <cstddef>

namespace E2EBackendStatus {
QJsonObject productionProviderInvocationExecutionStructuralSnapshotForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    QJsonArray executions;
    int callableEntryPointCount = 0;
    int sanitizedExecutionCount = 0;
    int resultCapturePolicyCount = 0;
    int noMaterialExportCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const bool callableEntryPoint = descriptor.linked;
        const bool resultCapturePolicy = true;
        const bool noSensitiveExport = true;
        const bool sanitized = callableEntryPoint
            && resultCapturePolicy
            && noSensitiveExport;

        QJsonObject execution;
        execution[QStringLiteral("sequenceIndex")] = sequenceIndex;
        execution[QStringLiteral("operation")] = cryptoOperationName(operation);
        execution[QStringLiteral("backendId")] = descriptor.id;
        execution[QStringLiteral("providerId")] = descriptor.providerId;
        execution[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        execution[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
        execution[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        execution[QStringLiteral("vectorSet")] = spec.vectorSet;
        execution[QStringLiteral("fixtureHashSha256")] = productionHarnessFixtureHash(spec);
        execution[QStringLiteral("vectorResultReady")] = false;
        execution[QStringLiteral("callableEntryPointReady")] = callableEntryPoint;
        execution[QStringLiteral("resultCapturePolicyReady")] = resultCapturePolicy;
        execution[QStringLiteral("executionEntryPoint")] =
            QStringLiteral("qnc_e2e_provider_table_v1/%1")
                .arg(productionOperationProviderSymbol(operation));
        execution[QStringLiteral("executionMode")] =
            QStringLiteral("blocked-non-executing-structural-snapshot");
        execution[QStringLiteral("executionState")] = descriptor.linked
            ? QStringLiteral("blocked-structural-snapshot")
            : QStringLiteral("blocked-not-linked");
        execution[QStringLiteral("operationInvoked")] = false;
        execution[QStringLiteral("inputBytesCaptured")] = false;
        execution[QStringLiteral("outputBytesCaptured")] = false;
        execution[QStringLiteral("resultCaptured")] = false;
        execution[QStringLiteral("statusCodeClass")] = QStringLiteral("not-invoked");
        execution[QStringLiteral("sanitizedErrorClass")] = QStringLiteral("not-invoked");
        execution[QStringLiteral("blockedReason")] = descriptor.linked
            ? QStringLiteral("production-provider-invocation-execution-structural-snapshot")
            : QStringLiteral("production-provider-table-not-registered");
        execution[QStringLiteral("operatorAction")] = descriptor.linked
            ? QStringLiteral("run-explicit-provider-invocation-probe-before-execution")
            : QStringLiteral("register-reviewed-provider-table-before-invocation-execution");
        execution[QStringLiteral("sanitized")] = sanitized;
        execution[QStringLiteral("materialExportProof")] = noSensitiveExport
            ? QStringLiteral("no-sensitive-material-export")
            : QStringLiteral("sensitive-material-exported");
        execution[QStringLiteral("rawKeyExported")] = false;
        execution[QStringLiteral("privateMaterialExported")] = false;
        execution[QStringLiteral("sessionSecretExported")] = false;
        execution[QStringLiteral("privateIdentityMaterialExported")] = false;
        execution[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        executions.append(execution);

        if (callableEntryPoint) {
            ++callableEntryPointCount;
        }
        if (sanitized) {
            ++sanitizedExecutionCount;
        }
        if (resultCapturePolicy) {
            ++resultCapturePolicyCount;
        }
        if (noSensitiveExport) {
            ++noMaterialExportCount;
        }
        ++sequenceIndex;
    }

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-execution-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("requiredExecutionCount")] = cryptoOperations().size();
    status[QStringLiteral("readyExecutionCount")] = 0;
    status[QStringLiteral("blockedExecutionCount")] = cryptoOperations().size();
    status[QStringLiteral("vectorResultReadyCount")] = 0;
    status[QStringLiteral("callableEntryPointCount")] = callableEntryPointCount;
    status[QStringLiteral("sanitizedExecutionCount")] = sanitizedExecutionCount;
    status[QStringLiteral("resultCapturePolicyCount")] = resultCapturePolicyCount;
    status[QStringLiteral("materialExportProofCount")] = noMaterialExportCount;
    status[QStringLiteral("releaseGate")] = descriptor.linked
        ? QStringLiteral("production-provider-invocation-execution-structural-snapshot")
        : QStringLiteral("production-provider-invocation-execution-blocked-not-linked");
    status[QStringLiteral("blockedReason")] = descriptor.linked
        ? QStringLiteral("production-provider-invocation-execution-structural-snapshot")
        : QStringLiteral("production-provider-table-not-registered");
    status[QStringLiteral("operatorAction")] = descriptor.linked
        ? QStringLiteral("run-explicit-provider-invocation-probe-before-execution")
        : QStringLiteral("register-reviewed-provider-table-before-invocation-execution");
    status[QStringLiteral("executions")] = executions;
    status[QStringLiteral("operationInvoked")] = false;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultCaptured")] = false;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderInvocationExecutionProbeCoreForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& structuralExecution,
    bool invokeProviderOperations) {
    const qnc_e2e_provider_table_v1* registeredTable = activeProductionProviderTable();
    const QJsonObject registration =
        productionProviderTableRegistrationStatusForDescriptor(descriptor);
    const bool registered = registeredTable != nullptr;
    const bool tableValidationAccepted =
        registration.value(QStringLiteral("tableValidationAccepted")).toBool(false);
    const bool providerInvocationAllowed = invokeProviderOperations;

    static const QByteArray primaryFixture(32, '\x42');
    static const QByteArray secondaryFixture(32, '\x24');
    static const QByteArray sessionDerivePrimaryFixture =
        QByteArray(ProductionSessionDerivePrimaryDomain) + QByteArray(32, '\x42');
    static const QByteArray sessionDeriveSecondaryFixture =
        QByteArray(ProductionSessionDeriveSecondaryDomain) + QByteArray(32, '\x24');
    static const QByteArray aadFixture("qnc-provider-probe-aad", 22);
    QByteArray verifyPublicFixture;
    QByteArray verifySignatureFixture;
    QByteArray decryptCiphertextFixture;
    const qnc_e2e_provider_operation_v1 publicKeyDerivationCallback =
        providerOperationPointer(registeredTable, E2ECryptoOperation::PublicKeyDerivation);
    const qnc_e2e_provider_operation_v1 agreementSignCallback =
        providerOperationPointer(registeredTable, E2ECryptoOperation::AgreementSign);
    const qnc_e2e_provider_operation_v1 payloadEncryptCallback =
        providerOperationPointer(registeredTable, E2ECryptoOperation::PayloadEncrypt);
    const bool prepareLinkedVerifyFixture =
        providerInvocationAllowed
        &&
        usingBuiltInProductionProviderTable()
        && registered
        && tableValidationAccepted
        && publicKeyDerivationCallback
        && agreementSignCallback;
    if (prepareLinkedVerifyFixture) {
        qnc_e2e_operation_input_v1 publicInput = {};
        publicInput.operation = providerOperationEnum(E2ECryptoOperation::PublicKeyDerivation);
        publicInput.suite_id = E2EProductionSuite;
        publicInput.primary.data =
            reinterpret_cast<const uint8_t*>(primaryFixture.constData());
        publicInput.primary.size = static_cast<size_t>(primaryFixture.size());
        qnc_e2e_operation_output_v1 publicOutput = {};
        if (publicKeyDerivationCallback(&publicInput, &publicOutput) == QNC_E2E_STATUS_OK
            && publicOutput.status == QNC_E2E_STATUS_OK
            && publicOutput.public_output.data
            && publicOutput.public_output.size > 0) {
            verifyPublicFixture =
                QByteArray(reinterpret_cast<const char*>(publicOutput.public_output.data),
                           static_cast<qsizetype>(publicOutput.public_output.size));
        }

        qnc_e2e_operation_input_v1 signInput = {};
        signInput.operation = providerOperationEnum(E2ECryptoOperation::AgreementSign);
        signInput.suite_id = E2EProductionSuite;
        signInput.primary.data =
            reinterpret_cast<const uint8_t*>(primaryFixture.constData());
        signInput.primary.size = static_cast<size_t>(primaryFixture.size());
        signInput.secondary.data =
            reinterpret_cast<const uint8_t*>(secondaryFixture.constData());
        signInput.secondary.size = static_cast<size_t>(secondaryFixture.size());
        qnc_e2e_operation_output_v1 signOutput = {};
        if (agreementSignCallback(&signInput, &signOutput) == QNC_E2E_STATUS_OK
            && signOutput.status == QNC_E2E_STATUS_OK
            && signOutput.public_output.data
            && signOutput.public_output.size > 0) {
            verifySignatureFixture =
                QByteArray(reinterpret_cast<const char*>(signOutput.public_output.data),
                           static_cast<qsizetype>(signOutput.public_output.size));
        }
    }
    const bool prepareLinkedDecryptFixture =
        providerInvocationAllowed
        &&
        usingBuiltInProductionProviderTable()
        && registered
        && tableValidationAccepted
        && payloadEncryptCallback;
    if (prepareLinkedDecryptFixture) {
        qnc_e2e_operation_input_v1 encryptInput = {};
        encryptInput.operation = providerOperationEnum(E2ECryptoOperation::PayloadEncrypt);
        encryptInput.suite_id = E2EProductionSuite;
        encryptInput.primary.data =
            reinterpret_cast<const uint8_t*>(primaryFixture.constData());
        encryptInput.primary.size = static_cast<size_t>(primaryFixture.size());
        encryptInput.secondary.data =
            reinterpret_cast<const uint8_t*>(secondaryFixture.constData());
        encryptInput.secondary.size = static_cast<size_t>(secondaryFixture.size());
        encryptInput.aad.data =
            reinterpret_cast<const uint8_t*>(aadFixture.constData());
        encryptInput.aad.size = static_cast<size_t>(aadFixture.size());
        qnc_e2e_operation_output_v1 encryptOutput = {};
        if (payloadEncryptCallback(&encryptInput, &encryptOutput) == QNC_E2E_STATUS_OK
            && encryptOutput.status == QNC_E2E_STATUS_OK
            && encryptOutput.sealed_output.data
            && encryptOutput.sealed_output.size > 0) {
            decryptCiphertextFixture =
                QByteArray(reinterpret_cast<const char*>(encryptOutput.sealed_output.data),
                           static_cast<qsizetype>(encryptOutput.sealed_output.size));
        }
    }

    QJsonArray probes;
    int invokedOperationCount = 0;
    int capturedResultCount = 0;
    int sanitizedProbeCount = 0;
    int okStatusCount = 0;
    int statusMismatchCount = 0;
    int vectorPassCount = 0;
    int vectorFailCount = 0;
    int blockedProbeCount = 0;
    int vectorContractCount = 0;
    int vectorContractHashCount = 0;
    int expectedStatusClassMatchCount = 0;
    int expectedFailureClassMatchCount = 0;
    int expectedMaterialPolicyClassMatchCount = 0;
    int executionFrameCount = 0;
    int executionFrameSanitizedCount = 0;
    int executionFrameResultCapturedCount = 0;
    int knownAnswerOutputEvidenceCount = 0;
    int knownAnswerOutputShapeHashCount = 0;
    int expectedOutputClassMatchCount = 0;
    int expectedOutputClassMismatchCount = 0;
    int outputEvidenceFailClosedCount = 0;
    int providerVectorSetMatchedCount = 0;
    int providerVectorSetMismatchCount = 0;
    int operationPointerMissingProbeCount = 0;
    int tableValidationBlockedProbeCount = 0;
    QJsonObject failureClassSummary;
    QJsonObject vectorResultSummary;
    QJsonObject knownAnswerOutputClassSummary;
    QJsonObject outputEvidenceClassSummary;
    QJsonObject mismatchReasonSummary;
    QJsonObject mismatchSeveritySummary;
    QJsonObject mismatchScopeSummary;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject vectorContract =
            productionProviderProbeVectorContract(spec);
        const qnc_e2e_provider_operation_v1 callback =
            providerOperationPointer(registeredTable, operation);
        const bool pointerPresent = callback != nullptr;
        const bool canInvoke =
            providerInvocationAllowed
            && registered
            && tableValidationAccepted
            && pointerPresent;
        const bool operationPointerMissing = registered && !pointerPresent;
        const QString tableValidationBlockedReason = tableValidationAccepted
            ? QString()
            : registration.value(QStringLiteral("tableValidationBlockedReason")).toString();

        qnc_e2e_operation_input_v1 input = {};
        input.operation = providerOperationEnum(operation);
        input.suite_id = E2EProductionSuite;
        input.primary.data = reinterpret_cast<const uint8_t*>(primaryFixture.constData());
        input.primary.size = static_cast<size_t>(primaryFixture.size());
        input.secondary.data = reinterpret_cast<const uint8_t*>(secondaryFixture.constData());
        input.secondary.size = static_cast<size_t>(secondaryFixture.size());
        input.aad.data = reinterpret_cast<const uint8_t*>(aadFixture.constData());
        input.aad.size = static_cast<size_t>(aadFixture.size());
        if (operation == E2ECryptoOperation::SessionDerive) {
            input.primary.data =
                reinterpret_cast<const uint8_t*>(sessionDerivePrimaryFixture.constData());
            input.primary.size =
                static_cast<size_t>(sessionDerivePrimaryFixture.size());
            input.secondary.data =
                reinterpret_cast<const uint8_t*>(sessionDeriveSecondaryFixture.constData());
            input.secondary.size =
                static_cast<size_t>(sessionDeriveSecondaryFixture.size());
        }
        if (operation == E2ECryptoOperation::AgreementVerify
            && !verifyPublicFixture.isEmpty()
            && !verifySignatureFixture.isEmpty()) {
            input.primary.data =
                reinterpret_cast<const uint8_t*>(verifyPublicFixture.constData());
            input.primary.size = static_cast<size_t>(verifyPublicFixture.size());
            input.aad.data =
                reinterpret_cast<const uint8_t*>(verifySignatureFixture.constData());
            input.aad.size = static_cast<size_t>(verifySignatureFixture.size());
        }
        if (operation == E2ECryptoOperation::PayloadDecrypt
            && !decryptCiphertextFixture.isEmpty()) {
            input.secondary.data =
                reinterpret_cast<const uint8_t*>(decryptCiphertextFixture.constData());
            input.secondary.size = static_cast<size_t>(decryptCiphertextFixture.size());
        }
        qnc_e2e_operation_output_v1 output = {};
        output.status = QNC_E2E_STATUS_UNSUPPORTED;
        output.material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
        output.sanitized_error_class = "not-invoked";

        qnc_e2e_status_t callbackStatus = QNC_E2E_STATUS_UNSUPPORTED;
        if (canInvoke) {
            callbackStatus = callback(&input, &output);
        }
        const QString statusClass = canInvoke
            ? providerStatusClass(callbackStatus)
            : QStringLiteral("not-invoked");
        const QString outputStatusClass = canInvoke
            ? providerStatusClass(output.status)
            : QStringLiteral("not-invoked");
        const QString sanitizedErrorClass = canInvoke && output.sanitized_error_class
            ? sanitizedBackendId(QString::fromLatin1(output.sanitized_error_class))
            : QStringLiteral("not-invoked");
        const bool outputPolicyAllowed =
            output.material_policy == QNC_E2E_MATERIAL_HANDLE_ONLY
            || output.material_policy == QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED
            || output.material_policy == QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED;
        const bool sanitized = !canInvoke
            || (!sanitizedErrorClass.isEmpty() && outputPolicyAllowed);
        const bool captured = canInvoke;
        const bool okStatus =
            callbackStatus == QNC_E2E_STATUS_OK
            && output.status == QNC_E2E_STATUS_OK;
        const bool statusMismatch =
            canInvoke && statusClass != outputStatusClass;
        const QString blockedReason = canInvoke
            ? QString()
            : (!registered
                ? QStringLiteral("production-provider-table-not-registered")
                : (!tableValidationAccepted
                    ? tableValidationBlockedReason
                    : QStringLiteral("production-provider-operation-pointer-missing")));
        const QString failureClass =
            providerProbeFailureClass(canInvoke, statusClass, outputStatusClass, blockedReason);
        const QString vectorResultClass = okStatus
            ? QStringLiteral("probe-vector-passed")
            : (canInvoke
                ? QStringLiteral("probe-vector-failed")
                : QStringLiteral("probe-vector-not-invoked"));
        const QString expectedStatusClass =
            vectorContract.value(QStringLiteral("expectedStatusClass")).toString();
        const QString expectedFailureClass =
            vectorContract.value(QStringLiteral("expectedFailureClass")).toString();
        const QString expectedMaterialPolicyClass =
            vectorContract.value(QStringLiteral("expectedMaterialPolicyClass")).toString();
        const QString materialPolicyClass = canInvoke
            ? providerMaterialPolicyClass(output.material_policy)
            : QStringLiteral("not-invoked");
        const bool vectorContractReady =
            vectorContract.value(QStringLiteral("contractHashReady")).toBool(false)
            && vectorContract.value(QStringLiteral("fixtureHashSha256")).toString().size()
                == FingerprintHexLength
            && vectorContract.value(QStringLiteral("inputContract")).toArray().size()
                == productionOperationInputContract(operation).size()
            && vectorContract.value(QStringLiteral("outputContract")).toArray().size()
                == productionOperationOutputContract(operation).size();
        const bool expectedStatusMatched = canInvoke
            && statusClass == expectedStatusClass
            && outputStatusClass == expectedStatusClass;
        const bool expectedFailureMatched = canInvoke
            && failureClass == expectedFailureClass;
        const bool expectedMaterialPolicyMatched = canInvoke
            && (materialPolicyClass == expectedMaterialPolicyClass
                || (expectedMaterialPolicyClass != QStringLiteral("handle-only")
                    && materialPolicyClass == QStringLiteral("handle-only"))
                || (operation == E2ECryptoOperation::IdentityKeyGeneration
                    && materialPolicyClass == QStringLiteral("handle-only")));
        const QJsonObject executionFrame =
            productionProviderProbeExecutionFrame(spec,
                                                  descriptor,
                                                  vectorContract,
                                                  canInvoke,
                                                  statusClass,
                                                  outputStatusClass,
                                                  failureClass,
                                                  vectorResultClass,
                                                  static_cast<qint64>(input.primary.size),
                                                  static_cast<qint64>(input.secondary.size),
                                                  static_cast<qint64>(input.aad.size),
                                                  canInvoke
                                                      ? static_cast<qint64>(output.public_output.size)
                                                      : 0,
                                                  canInvoke
                                                      ? static_cast<qint64>(output.sealed_output.size)
                                                      : 0);
        const qint64 publicOutputSize = canInvoke
            ? static_cast<qint64>(output.public_output.size)
            : 0;
        const qint64 sealedOutputSize = canInvoke
            ? static_cast<qint64>(output.sealed_output.size)
            : 0;
        const QJsonObject outputEvidence =
            productionProviderProbeKnownAnswerOutputEvidence(spec,
                                                             vectorContract,
                                                             canInvoke,
                                                             statusClass,
                                                             materialPolicyClass,
                                                             publicOutputSize,
                                                             sealedOutputSize);
        const bool expectedOutputClassMatched =
            outputEvidence.value(QStringLiteral("expectedOutputClassMatched")).toBool(false);
        const QString mismatchReason =
            providerProbeMismatchReason(canInvoke,
                                        expectedStatusMatched,
                                        expectedFailureMatched,
                                        expectedMaterialPolicyMatched,
                                        expectedOutputClassMatched,
                                        blockedReason);
        const QString mismatchSeverity =
            providerProbeMismatchSeverity(mismatchReason);
        const QString mismatchScope =
            providerProbeMismatchScope(canInvoke,
                                       tableValidationAccepted,
                                       pointerPresent,
                                       mismatchReason);
        const bool providerVectorSetMatched =
            canInvoke
            && vectorContractReady
            && expectedStatusMatched
            && expectedFailureMatched
            && expectedMaterialPolicyMatched
            && expectedOutputClassMatched;

        QJsonObject probe;
        probe[QStringLiteral("sequenceIndex")] = sequenceIndex;
        probe[QStringLiteral("operation")] = cryptoOperationName(operation);
        probe[QStringLiteral("backendId")] = descriptor.id;
        probe[QStringLiteral("providerId")] = descriptor.providerId;
        probe[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        probe[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
        probe[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        probe[QStringLiteral("vectorSet")] = spec.vectorSet;
        probe[QStringLiteral("knownAnswerVectorId")] =
            QStringLiteral("%1/%2").arg(spec.vectorSet, cryptoOperationName(operation));
        probe[QStringLiteral("knownAnswerFixtureId")] =
            QStringLiteral("probe-fixture/%1").arg(cryptoOperationName(operation));
        probe[QStringLiteral("fixtureHashSha256")] = productionHarnessFixtureHash(spec);
        probe[QStringLiteral("probeVectorContract")] = vectorContract;
        probe[QStringLiteral("probeExecutionFrame")] = executionFrame;
        probe[QStringLiteral("probeKnownAnswerOutputEvidence")] = outputEvidence;
        probe[QStringLiteral("probeKnownAnswerOutputEvidenceSchema")] =
            outputEvidence.value(QStringLiteral("schema")).toString();
        probe[QStringLiteral("expectedKnownAnswerOutputClass")] =
            outputEvidence.value(QStringLiteral("expectedKnownAnswerOutputClass")).toString();
        probe[QStringLiteral("observedKnownAnswerOutputClass")] =
            outputEvidence.value(QStringLiteral("observedKnownAnswerOutputClass")).toString();
        probe[QStringLiteral("expectedOutputShapeClass")] =
            outputEvidence.value(QStringLiteral("expectedKnownAnswerOutputClass")).toString();
        probe[QStringLiteral("observedOutputShapeClass")] =
            outputEvidence.value(QStringLiteral("observedKnownAnswerOutputClass")).toString();
        probe[QStringLiteral("expectedOutputClassMatched")] = expectedOutputClassMatched;
        probe[QStringLiteral("outputEvidenceClass")] =
            outputEvidence.value(QStringLiteral("outputEvidenceClass")).toString();
        probe[QStringLiteral("outputEvidenceFailClosed")] =
            outputEvidence.value(QStringLiteral("outputEvidenceFailClosed")).toBool(false);
        probe[QStringLiteral("outputEvidenceBlockedReason")] =
            outputEvidence.value(QStringLiteral("outputEvidenceBlockedReason")).toString();
        probe[QStringLiteral("outputShapeHashSha256")] =
            outputEvidence.value(QStringLiteral("outputShapeHashSha256")).toString();
        probe[QStringLiteral("probeExecutionFrameSchema")] =
            executionFrame.value(QStringLiteral("schema")).toString();
        probe[QStringLiteral("probeExecutionEntryPoint")] =
            executionFrame.value(QStringLiteral("executionEntryPoint")).toString();
        probe[QStringLiteral("probeExecutionInputCapturePolicy")] =
            executionFrame.value(QStringLiteral("inputCapturePolicy")).toString();
        probe[QStringLiteral("probeExecutionOutputCapturePolicy")] =
            executionFrame.value(QStringLiteral("outputCapturePolicy")).toString();
        probe[QStringLiteral("probeExecutionResultCapturePolicy")] =
            executionFrame.value(QStringLiteral("resultCapturePolicy")).toString();
        probe[QStringLiteral("probeExecutionFrameSanitized")] =
            executionFrame.value(QStringLiteral("sanitized")).toBool(false);
        probe[QStringLiteral("probeVectorSchema")] =
            vectorContract.value(QStringLiteral("probeVectorSchema")).toString();
        probe[QStringLiteral("inputContractHashSha256")] =
            vectorContract.value(QStringLiteral("inputContractHashSha256")).toString();
        probe[QStringLiteral("outputContractHashSha256")] =
            vectorContract.value(QStringLiteral("outputContractHashSha256")).toString();
        probe[QStringLiteral("fixtureInputClass")] =
            vectorContract.value(QStringLiteral("fixtureInputClass")).toString();
        probe[QStringLiteral("expectedStatusClass")] = expectedStatusClass;
        probe[QStringLiteral("expectedFailureClass")] = expectedFailureClass;
        probe[QStringLiteral("expectedMaterialPolicyClass")] = expectedMaterialPolicyClass;
        probe[QStringLiteral("expectedVectorResultClass")] =
            vectorContract.value(QStringLiteral("expectedVectorResultClass")).toString();
        probe[QStringLiteral("vectorContractReady")] = vectorContractReady;
        probe[QStringLiteral("expectedStatusClassMatched")] = expectedStatusMatched;
        probe[QStringLiteral("expectedFailureClassMatched")] = expectedFailureMatched;
        probe[QStringLiteral("expectedMaterialPolicyClassMatched")] =
            expectedMaterialPolicyMatched;
        probe[QStringLiteral("providerVectorSetMatched")] = providerVectorSetMatched;
        probe[QStringLiteral("mismatchReason")] = mismatchReason;
        probe[QStringLiteral("mismatchSeverity")] = mismatchSeverity;
        probe[QStringLiteral("mismatchScope")] = mismatchScope;
        probe[QStringLiteral("registered")] = registered;
        probe[QStringLiteral("tableValidationAccepted")] = tableValidationAccepted;
        probe[QStringLiteral("tableValidationBlockedReason")] = tableValidationBlockedReason;
        probe[QStringLiteral("functionPointerPresent")] = pointerPresent;
        probe[QStringLiteral("operationPointerMissing")] = operationPointerMissing;
        probe[QStringLiteral("operationInvoked")] = canInvoke;
        probe[QStringLiteral("inputBytesCaptured")] = false;
        probe[QStringLiteral("outputBytesCaptured")] = false;
        probe[QStringLiteral("resultCaptured")] = captured;
        probe[QStringLiteral("inputPrimarySize")] = static_cast<qint64>(input.primary.size);
        probe[QStringLiteral("inputSecondarySize")] = static_cast<qint64>(input.secondary.size);
        probe[QStringLiteral("inputAadSize")] = static_cast<qint64>(input.aad.size);
        probe[QStringLiteral("publicOutputSize")] =
            canInvoke ? static_cast<qint64>(output.public_output.size) : 0;
        probe[QStringLiteral("sealedOutputSize")] =
            canInvoke ? static_cast<qint64>(output.sealed_output.size) : 0;
        probe[QStringLiteral("callbackStatusClass")] = statusClass;
        probe[QStringLiteral("outputStatusClass")] = outputStatusClass;
        probe[QStringLiteral("statusConsistencyClass")] = statusMismatch
            ? QStringLiteral("provider-status-mismatch")
            : (canInvoke
                ? QStringLiteral("provider-status-consistent")
                : QStringLiteral("not-invoked"));
        probe[QStringLiteral("sanitizedErrorClass")] = sanitizedErrorClass;
        probe[QStringLiteral("materialPolicyClass")] = materialPolicyClass;
        probe[QStringLiteral("knownAnswerPassed")] = okStatus;
        probe[QStringLiteral("roundTripPassed")] = okStatus
            && (operation == E2ECryptoOperation::PayloadEncrypt
                || operation == E2ECryptoOperation::PayloadDecrypt
                || operation == E2ECryptoOperation::SessionDerive
                || operation == E2ECryptoOperation::AgreementVerify);
        probe[QStringLiteral("vectorResultClass")] = vectorResultClass;
        probe[QStringLiteral("failureClass")] = failureClass;
        probe[QStringLiteral("probeState")] = canInvoke
            ? QStringLiteral("invoked-through-registered-provider-table")
            : QStringLiteral("blocked-before-provider-call");
        probe[QStringLiteral("blockedReason")] = blockedReason;
        probe[QStringLiteral("operatorAction")] = canInvoke
            ? QStringLiteral("none")
            : QStringLiteral("register-complete-reviewed-provider-table-before-probe");
        probe[QStringLiteral("sanitized")] = sanitized;
        probe[QStringLiteral("materialExportProof")] =
            QStringLiteral("sizes-and-status-only-no-secret-bytes");
        probe[QStringLiteral("rawKeyExported")] = false;
        probe[QStringLiteral("privateMaterialExported")] = false;
        probe[QStringLiteral("sessionSecretExported")] = false;
        probe[QStringLiteral("privateIdentityMaterialExported")] = false;
        probe[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        probes.append(probe);

        if (canInvoke) {
            ++invokedOperationCount;
        } else {
            ++blockedProbeCount;
        }
        if (captured) {
            ++capturedResultCount;
        }
        if (sanitized) {
            ++sanitizedProbeCount;
        }
        if (okStatus) {
            ++okStatusCount;
        }
        if (statusMismatch) {
            ++statusMismatchCount;
        }
        if (vectorContractReady) {
            ++vectorContractCount;
        }
        if (vectorContract.value(QStringLiteral("contractHashReady")).toBool(false)) {
            ++vectorContractHashCount;
        }
        if (expectedStatusMatched) {
            ++expectedStatusClassMatchCount;
        }
        if (expectedFailureMatched) {
            ++expectedFailureClassMatchCount;
        }
        if (expectedMaterialPolicyMatched) {
            ++expectedMaterialPolicyClassMatchCount;
        }
        if (executionFrame.value(QStringLiteral("schema")).toString()
            == QStringLiteral("qtnetworkchat-e2e-production-provider-probe-execution-frame-v1")) {
            ++executionFrameCount;
        }
        if (executionFrame.value(QStringLiteral("sanitized")).toBool(false)) {
            ++executionFrameSanitizedCount;
        }
        if (executionFrame.value(QStringLiteral("resultCaptured")).toBool(false)) {
            ++executionFrameResultCapturedCount;
        }
        if (outputEvidence.value(QStringLiteral("schema")).toString()
            == QStringLiteral("qtnetworkchat-e2e-production-provider-probe-output-evidence-v1")) {
            ++knownAnswerOutputEvidenceCount;
        }
        if (outputEvidence.value(QStringLiteral("outputShapeHashSha256")).toString().size()
            == FingerprintHexLength) {
            ++knownAnswerOutputShapeHashCount;
        }
        if (expectedOutputClassMatched) {
            ++expectedOutputClassMatchCount;
        }
        if (canInvoke && !expectedOutputClassMatched) {
            ++expectedOutputClassMismatchCount;
        }
        if (outputEvidence.value(QStringLiteral("outputEvidenceFailClosed")).toBool(false)) {
            ++outputEvidenceFailClosedCount;
        }
        if (providerVectorSetMatched) {
            ++providerVectorSetMatchedCount;
        } else {
            ++providerVectorSetMismatchCount;
        }
        if (operationPointerMissing) {
            ++operationPointerMissingProbeCount;
        }
        if (!tableValidationAccepted) {
            ++tableValidationBlockedProbeCount;
        }
        if (okStatus) {
            ++vectorPassCount;
        } else {
            ++vectorFailCount;
        }
        incrementSummaryCount(&failureClassSummary, failureClass);
        incrementSummaryCount(&vectorResultSummary, vectorResultClass);
        incrementSummaryCount(&knownAnswerOutputClassSummary,
                              outputEvidence.value(QStringLiteral("observedKnownAnswerOutputClass")).toString());
        incrementSummaryCount(&outputEvidenceClassSummary,
                              outputEvidence.value(QStringLiteral("outputEvidenceClass")).toString());
        incrementSummaryCount(&mismatchReasonSummary, mismatchReason);
        incrementSummaryCount(&mismatchSeveritySummary, mismatchSeverity);
        incrementSummaryCount(&mismatchScopeSummary, mismatchScope);
        ++sequenceIndex;
    }

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-execution-probe-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("releaseGate")] =
        QStringLiteral("production-provider-invocation-execution-probe-not-release-gate");
    status[QStringLiteral("providerInvocationExecution")] = structuralExecution;
    status[QStringLiteral("providerInvocationExecutionAccepted")] =
        structuralExecution.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("providerTableRegistration")] = registration;
    status[QStringLiteral("providerTableRegistered")] = registered;
    status[QStringLiteral("tableValidationAccepted")] = tableValidationAccepted;
    status[QStringLiteral("requiredProbeCount")] = cryptoOperations().size();
    status[QStringLiteral("invokedOperationCount")] = invokedOperationCount;
    status[QStringLiteral("blockedProbeCount")] = blockedProbeCount;
    status[QStringLiteral("capturedResultCount")] = capturedResultCount;
    status[QStringLiteral("sanitizedProbeCount")] = sanitizedProbeCount;
    status[QStringLiteral("okStatusCount")] = okStatusCount;
    status[QStringLiteral("statusMismatchCount")] = statusMismatchCount;
    status[QStringLiteral("vectorPassCount")] = vectorPassCount;
    status[QStringLiteral("vectorFailCount")] = vectorFailCount;
    status[QStringLiteral("vectorContractCount")] = vectorContractCount;
    status[QStringLiteral("vectorContractHashCount")] = vectorContractHashCount;
    status[QStringLiteral("expectedStatusClassMatchCount")] =
        expectedStatusClassMatchCount;
    status[QStringLiteral("expectedFailureClassMatchCount")] =
        expectedFailureClassMatchCount;
    status[QStringLiteral("expectedMaterialPolicyClassMatchCount")] =
        expectedMaterialPolicyClassMatchCount;
    status[QStringLiteral("executionFrameCount")] = executionFrameCount;
    status[QStringLiteral("executionFrameSanitizedCount")] = executionFrameSanitizedCount;
    status[QStringLiteral("executionFrameResultCapturedCount")] =
        executionFrameResultCapturedCount;
    status[QStringLiteral("knownAnswerOutputEvidenceCount")] =
        knownAnswerOutputEvidenceCount;
    status[QStringLiteral("knownAnswerOutputShapeHashCount")] =
        knownAnswerOutputShapeHashCount;
    status[QStringLiteral("expectedOutputClassMatchCount")] =
        expectedOutputClassMatchCount;
    status[QStringLiteral("expectedOutputClassMismatchCount")] =
        expectedOutputClassMismatchCount;
    status[QStringLiteral("outputEvidenceFailClosedCount")] =
        outputEvidenceFailClosedCount;
    status[QStringLiteral("providerVectorSetMatchedCount")] =
        providerVectorSetMatchedCount;
    status[QStringLiteral("providerVectorSetMismatchCount")] =
        providerVectorSetMismatchCount;
    status[QStringLiteral("operationPointerMissingProbeCount")] =
        operationPointerMissingProbeCount;
    status[QStringLiteral("tableValidationBlockedProbeCount")] =
        tableValidationBlockedProbeCount;
    status[QStringLiteral("failureClassSummary")] = failureClassSummary;
    status[QStringLiteral("vectorResultSummary")] = vectorResultSummary;
    status[QStringLiteral("knownAnswerOutputClassSummary")] =
        knownAnswerOutputClassSummary;
    status[QStringLiteral("outputEvidenceClassSummary")] =
        outputEvidenceClassSummary;
    status[QStringLiteral("mismatchReasonSummary")] = mismatchReasonSummary;
    status[QStringLiteral("mismatchSeveritySummary")] = mismatchSeveritySummary;
    status[QStringLiteral("mismatchScopeSummary")] = mismatchScopeSummary;
    status[QStringLiteral("operatorAction")] =
        QStringLiteral("use-reviewed-provider-probe-results-as-test-evidence-only");
    status[QStringLiteral("probes")] = probes;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderInvocationExecutionProbeEvidenceForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    return productionProviderInvocationExecutionProbeCoreForDescriptor(
        descriptor,
        QJsonObject(),
        usingBuiltInProductionProviderTable());
}

QJsonObject productionProviderInvocationExecutionProbeForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    return productionProviderInvocationExecutionProbeCoreForDescriptor(
        descriptor,
        productionProviderInvocationExecutionStructuralSnapshotForDescriptor(descriptor),
        true);
}

QJsonObject productionProviderRoundTripExecutionProbeForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    const qnc_e2e_provider_table_v1* registeredTable = activeProductionProviderTable();
    const QJsonObject registration =
        productionProviderTableRegistrationStatusForDescriptor(descriptor);
    const bool registered = registeredTable != nullptr;
    const bool tableValidationAccepted =
        registration.value(QStringLiteral("tableValidationAccepted")).toBool(false);
    const bool canUseTable = registered && tableValidationAccepted;

    struct ProviderRunResult {
        bool pointerPresent = false;
        bool invoked = false;
        qnc_e2e_status_t callbackStatus = QNC_E2E_STATUS_UNSUPPORTED;
        qnc_e2e_status_t outputStatus = QNC_E2E_STATUS_UNSUPPORTED;
        qnc_e2e_material_policy_t materialPolicy = QNC_E2E_MATERIAL_HANDLE_ONLY;
        QString sanitizedErrorClass = QStringLiteral("not-invoked");
        QString blockedReason;
        QByteArray publicOutput;
        QByteArray sealedOutput;
    };

    const auto outputBytes = [](const qnc_e2e_buffer_view_v1& view) {
        if (!view.data || view.size == 0) {
            return QByteArray();
        }
        return QByteArray(reinterpret_cast<const char*>(view.data),
                          static_cast<qsizetype>(view.size));
    };
    const auto invokeOperation =
        [&](E2ECryptoOperation operation,
            const QByteArray& primary,
            const QByteArray& secondary,
            const QByteArray& aad) {
            ProviderRunResult result;
            const qnc_e2e_provider_operation_v1 callback =
                providerOperationPointer(registeredTable, operation);
            result.pointerPresent = callback != nullptr;
            if (!registered) {
                result.blockedReason = QStringLiteral("production-provider-table-not-registered");
                return result;
            }
            if (!tableValidationAccepted) {
                result.blockedReason =
                    registration.value(QStringLiteral("tableValidationBlockedReason")).toString(
                        QStringLiteral("production-provider-table-validation-blocked"));
                return result;
            }
            if (!callback) {
                result.blockedReason =
                    QStringLiteral("production-provider-operation-pointer-missing");
                return result;
            }

            qnc_e2e_operation_input_v1 input = {};
            input.operation = providerOperationEnum(operation);
            input.suite_id = E2EProductionSuite;
            input.primary.data = reinterpret_cast<const uint8_t*>(primary.constData());
            input.primary.size = static_cast<size_t>(primary.size());
            input.secondary.data = reinterpret_cast<const uint8_t*>(secondary.constData());
            input.secondary.size = static_cast<size_t>(secondary.size());
            input.aad.data = reinterpret_cast<const uint8_t*>(aad.constData());
            input.aad.size = static_cast<size_t>(aad.size());

            qnc_e2e_operation_output_v1 output = {};
            output.status = QNC_E2E_STATUS_UNSUPPORTED;
            output.material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
            output.sanitized_error_class = "not-invoked";

            result.callbackStatus = callback(&input, &output);
            result.outputStatus = output.status;
            result.materialPolicy = output.material_policy;
            result.sanitizedErrorClass = output.sanitized_error_class
                ? sanitizedBackendId(QString::fromLatin1(output.sanitized_error_class))
                : QStringLiteral("missing-error-class");
            result.publicOutput = outputBytes(output.public_output);
            result.sealedOutput = outputBytes(output.sealed_output);
            result.invoked = true;
            return result;
        };
    const auto dependencyBlockedResult = [&](E2ECryptoOperation operation,
                                             const QString& blockedReason) {
        ProviderRunResult result;
        result.pointerPresent = providerOperationPointer(registeredTable, operation) != nullptr;
        result.blockedReason = blockedReason;
        return result;
    };

    QJsonArray operations;
    QJsonArray negativeChecks;
    int invokedOperationCount = 0;
    int readyOperationCount = 0;
    int blockedOperationCount = 0;
    int statusConsistentOperationCount = 0;
    int sanitizedOperationCount = 0;
    int materialPolicyMatchedCount = 0;
    int sequenceIndex = 0;
    const auto appendOperation =
        [&](E2ECryptoOperation operation,
            const ProviderRunResult& result,
            bool stepPassed,
            const QString& stepId) {
            const QString statusClass = result.invoked
                ? providerStatusClass(result.callbackStatus)
                : QStringLiteral("not-invoked");
            const QString outputStatusClass = result.invoked
                ? providerStatusClass(result.outputStatus)
                : QStringLiteral("not-invoked");
            const QString materialPolicyClass = result.invoked
                ? providerMaterialPolicyClass(result.materialPolicy)
                : QStringLiteral("not-invoked");
            const bool statusConsistent = result.invoked
                && statusClass == outputStatusClass;
            const bool sanitized = !result.invoked
                || (!result.sanitizedErrorClass.isEmpty()
                    && result.sanitizedErrorClass.size() <= 96);
            const bool materialPolicyMatched = result.invoked
                && (materialPolicyClass == productionProbeExpectedMaterialPolicyClass(operation)
                    || (operation == E2ECryptoOperation::IdentityKeyGeneration
                        && materialPolicyClass == QStringLiteral("handle-only")));

            QJsonObject op;
            op[QStringLiteral("sequenceIndex")] = sequenceIndex++;
            op[QStringLiteral("stepId")] = stepId;
            op[QStringLiteral("operation")] = cryptoOperationName(operation);
            op[QStringLiteral("backendId")] = descriptor.id;
            op[QStringLiteral("providerId")] = descriptor.providerId;
            op[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
            op[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
            op[QStringLiteral("functionPointerPresent")] = result.pointerPresent;
            op[QStringLiteral("operationInvoked")] = result.invoked;
            op[QStringLiteral("callbackStatusClass")] = statusClass;
            op[QStringLiteral("outputStatusClass")] = outputStatusClass;
            op[QStringLiteral("statusConsistent")] = statusConsistent;
            op[QStringLiteral("materialPolicyClass")] = materialPolicyClass;
            op[QStringLiteral("materialPolicyMatched")] = materialPolicyMatched;
            op[QStringLiteral("sanitizedErrorClass")] = result.sanitizedErrorClass;
            op[QStringLiteral("publicOutputSize")] = result.invoked
                ? static_cast<int>(result.publicOutput.size())
                : 0;
            op[QStringLiteral("sealedOutputSize")] = result.invoked
                ? static_cast<int>(result.sealedOutput.size())
                : 0;
            op[QStringLiteral("roundTripStepPassed")] = stepPassed;
            op[QStringLiteral("blockedReason")] = stepPassed
                ? QString()
                : (result.blockedReason.isEmpty()
                    ? QStringLiteral("production-provider-round-trip-step-failed")
                    : result.blockedReason);
            op[QStringLiteral("byteFlowScope")] =
                QStringLiteral("internal-test-vector-only-not-exported");
            op[QStringLiteral("inputBytesCaptured")] = false;
            op[QStringLiteral("outputBytesCaptured")] = false;
            op[QStringLiteral("resultBytesCaptured")] = false;
            op[QStringLiteral("rawKeyExported")] = false;
            op[QStringLiteral("privateMaterialExported")] = false;
            op[QStringLiteral("sessionSecretExported")] = false;
            op[QStringLiteral("privateIdentityMaterialExported")] = false;
            op[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
            op[QStringLiteral("plaintextExported")] = false;
            op[QStringLiteral("ciphertextExported")] = false;
            operations.append(op);

            if (result.invoked) {
                ++invokedOperationCount;
            }
            if (stepPassed) {
                ++readyOperationCount;
            } else {
                ++blockedOperationCount;
            }
            if (statusConsistent) {
                ++statusConsistentOperationCount;
            }
            if (sanitized) {
                ++sanitizedOperationCount;
            }
            if (materialPolicyMatched) {
                ++materialPolicyMatchedCount;
            }
        };
    int negativeCheckCount = 0;
    int negativeCheckPassCount = 0;
    const auto appendNegativeCheck =
        [&](const QString& checkId,
            E2ECryptoOperation operation,
            const ProviderRunResult& result,
            bool rejectedAsExpected) {
            ++negativeCheckCount;
            if (rejectedAsExpected) {
                ++negativeCheckPassCount;
            }
            QJsonObject check;
            check[QStringLiteral("checkId")] = checkId;
            check[QStringLiteral("operation")] = cryptoOperationName(operation);
            check[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
            check[QStringLiteral("operationInvoked")] = result.invoked;
            check[QStringLiteral("callbackStatusClass")] = result.invoked
                ? providerStatusClass(result.callbackStatus)
                : QStringLiteral("not-invoked");
            check[QStringLiteral("outputStatusClass")] = result.invoked
                ? providerStatusClass(result.outputStatus)
                : QStringLiteral("not-invoked");
            check[QStringLiteral("sanitizedErrorClass")] = result.sanitizedErrorClass;
            check[QStringLiteral("rejectedAsExpected")] = rejectedAsExpected;
            check[QStringLiteral("blockedReason")] = rejectedAsExpected
                ? QString()
                : (result.blockedReason.isEmpty()
                    ? QStringLiteral("production-provider-negative-check-failed")
                    : result.blockedReason);
            check[QStringLiteral("inputBytesCaptured")] = false;
            check[QStringLiteral("outputBytesCaptured")] = false;
            check[QStringLiteral("rawKeyExported")] = false;
            check[QStringLiteral("privateMaterialExported")] = false;
            check[QStringLiteral("sessionSecretExported")] = false;
            check[QStringLiteral("plaintextExported")] = false;
            check[QStringLiteral("ciphertextExported")] = false;
            negativeChecks.append(check);
        };

    const QByteArray empty;
    const QByteArray transcript =
        QByteArrayLiteral("qnc-provider-roundtrip-agreement-transcript-v1");
    const QByteArray sessionPrimary =
        QByteArray(ProductionSessionDerivePrimaryDomain)
        + QByteArrayLiteral("qnc-provider-roundtrip-session-primary-v1");
    const QByteArray sessionSecondary =
        QByteArray(ProductionSessionDeriveSecondaryDomain)
        + QByteArrayLiteral("qnc-provider-roundtrip-session-secondary-v1");
    const QByteArray sessionAad =
        QByteArrayLiteral("qnc-provider-roundtrip-session-context-v1");
    const QByteArray payload = QByteArrayLiteral("round-trip-provider-payload");
    const QByteArray payloadAad =
        QByteArrayLiteral("qnc-provider-roundtrip-payload-aad-v1");

    const ProviderRunResult sessionKey =
        invokeOperation(E2ECryptoOperation::SessionKeyGeneration, empty, empty, empty);
    const bool sessionKeyPassed = sessionKey.invoked
        && sessionKey.callbackStatus == QNC_E2E_STATUS_OK
        && sessionKey.outputStatus == QNC_E2E_STATUS_OK
        && sessionKey.materialPolicy == QNC_E2E_MATERIAL_HANDLE_ONLY
        && sessionKey.sealedOutput.size() == SessionKeyBytes;
    appendOperation(E2ECryptoOperation::SessionKeyGeneration,
                    sessionKey,
                    sessionKeyPassed,
                    QStringLiteral("session-key-generation"));

    const ProviderRunResult identityKey =
        invokeOperation(E2ECryptoOperation::IdentityKeyGeneration, empty, empty, empty);
    const bool identityKeyPassed = identityKey.invoked
        && identityKey.callbackStatus == QNC_E2E_STATUS_OK
        && identityKey.outputStatus == QNC_E2E_STATUS_OK
        && identityKey.publicOutput.size() == 32
        && identityKey.sealedOutput.size() == 32;
    appendOperation(E2ECryptoOperation::IdentityKeyGeneration,
                    identityKey,
                    identityKeyPassed,
                    QStringLiteral("identity-key-generation"));

    const ProviderRunResult publicKey = identityKeyPassed
        ? invokeOperation(E2ECryptoOperation::PublicKeyDerivation,
                          identityKey.sealedOutput,
                          empty,
                          empty)
        : dependencyBlockedResult(E2ECryptoOperation::PublicKeyDerivation,
                                  QStringLiteral("identity-generation-not-ready"));
    const bool publicKeyPassed = publicKey.invoked
        && publicKey.callbackStatus == QNC_E2E_STATUS_OK
        && publicKey.outputStatus == QNC_E2E_STATUS_OK
        && publicKey.materialPolicy == QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED
        && publicKey.publicOutput == identityKey.publicOutput
        && publicKey.publicOutput.size() == 32;
    appendOperation(E2ECryptoOperation::PublicKeyDerivation,
                    publicKey,
                    publicKeyPassed,
                    QStringLiteral("public-key-derivation"));

    const ProviderRunResult signature = identityKeyPassed
        ? invokeOperation(E2ECryptoOperation::AgreementSign,
                          identityKey.sealedOutput,
                          transcript,
                          empty)
        : dependencyBlockedResult(E2ECryptoOperation::AgreementSign,
                                  QStringLiteral("identity-generation-not-ready"));
    const bool signaturePassed = signature.invoked
        && signature.callbackStatus == QNC_E2E_STATUS_OK
        && signature.outputStatus == QNC_E2E_STATUS_OK
        && signature.materialPolicy == QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED
        && signature.publicOutput.size() == 64;
    appendOperation(E2ECryptoOperation::AgreementSign,
                    signature,
                    signaturePassed,
                    QStringLiteral("agreement-sign"));

    const ProviderRunResult verification = publicKeyPassed && signaturePassed
        ? invokeOperation(E2ECryptoOperation::AgreementVerify,
                          publicKey.publicOutput,
                          transcript,
                          signature.publicOutput)
        : dependencyBlockedResult(E2ECryptoOperation::AgreementVerify,
                                  QStringLiteral("agreement-signature-input-not-ready"));
    const bool verificationPassed = verification.invoked
        && verification.callbackStatus == QNC_E2E_STATUS_OK
        && verification.outputStatus == QNC_E2E_STATUS_OK
        && verification.materialPolicy == QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED;
    appendOperation(E2ECryptoOperation::AgreementVerify,
                    verification,
                    verificationPassed,
                    QStringLiteral("agreement-verify"));

    if (publicKeyPassed && signaturePassed) {
        QByteArray tamperedSignature = signature.publicOutput;
        if (!tamperedSignature.isEmpty()) {
            tamperedSignature[0] = static_cast<char>(tamperedSignature.at(0) ^ 0x01);
        }
        const ProviderRunResult tamperedVerify =
            invokeOperation(E2ECryptoOperation::AgreementVerify,
                            publicKey.publicOutput,
                            transcript,
                            tamperedSignature);
        appendNegativeCheck(QStringLiteral("agreement-verify-tamper"),
                            E2ECryptoOperation::AgreementVerify,
                            tamperedVerify,
                            tamperedVerify.invoked
                                && tamperedVerify.callbackStatus == QNC_E2E_STATUS_REJECTED
                                && tamperedVerify.outputStatus == QNC_E2E_STATUS_REJECTED);
    } else {
        appendNegativeCheck(QStringLiteral("agreement-verify-tamper"),
                            E2ECryptoOperation::AgreementVerify,
                            dependencyBlockedResult(E2ECryptoOperation::AgreementVerify,
                                                    QStringLiteral("agreement-signature-input-not-ready")),
                            false);
    }

    if (identityKeyPassed) {
        const QByteArray malformedIdentityHandle =
            identityKey.sealedOutput.left(SessionKeyBytes - 1);
        const ProviderRunResult malformedPublicDerivation =
            invokeOperation(E2ECryptoOperation::PublicKeyDerivation,
                            malformedIdentityHandle,
                            empty,
                            empty);
        appendNegativeCheck(QStringLiteral("public-key-derivation-malformed-handle"),
                            E2ECryptoOperation::PublicKeyDerivation,
                            malformedPublicDerivation,
                            malformedPublicDerivation.invoked
                                && malformedPublicDerivation.callbackStatus == QNC_E2E_STATUS_INVALID_INPUT
                                && malformedPublicDerivation.outputStatus == QNC_E2E_STATUS_INVALID_INPUT);

        const ProviderRunResult malformedAgreementSign =
            invokeOperation(E2ECryptoOperation::AgreementSign,
                            malformedIdentityHandle,
                            transcript,
                            empty);
        appendNegativeCheck(QStringLiteral("agreement-sign-malformed-handle"),
                            E2ECryptoOperation::AgreementSign,
                            malformedAgreementSign,
                            malformedAgreementSign.invoked
                                && malformedAgreementSign.callbackStatus == QNC_E2E_STATUS_INVALID_INPUT
                                && malformedAgreementSign.outputStatus == QNC_E2E_STATUS_INVALID_INPUT);
    } else {
        appendNegativeCheck(QStringLiteral("public-key-derivation-malformed-handle"),
                            E2ECryptoOperation::PublicKeyDerivation,
                            dependencyBlockedResult(E2ECryptoOperation::PublicKeyDerivation,
                                                    QStringLiteral("identity-generation-not-ready")),
                            false);
        appendNegativeCheck(QStringLiteral("agreement-sign-malformed-handle"),
                            E2ECryptoOperation::AgreementSign,
                            dependencyBlockedResult(E2ECryptoOperation::AgreementSign,
                                                    QStringLiteral("identity-generation-not-ready")),
                            false);
    }

    if (signaturePassed) {
        const ProviderRunResult malformedVerifyPublicKey =
            invokeOperation(E2ECryptoOperation::AgreementVerify,
                            publicKey.publicOutput.left(31),
                            transcript,
                            signature.publicOutput);
        appendNegativeCheck(QStringLiteral("agreement-verify-malformed-public-key"),
                            E2ECryptoOperation::AgreementVerify,
                            malformedVerifyPublicKey,
                            malformedVerifyPublicKey.invoked
                                && malformedVerifyPublicKey.callbackStatus == QNC_E2E_STATUS_INVALID_INPUT
                                && malformedVerifyPublicKey.outputStatus == QNC_E2E_STATUS_INVALID_INPUT);
    } else {
        appendNegativeCheck(QStringLiteral("agreement-verify-malformed-public-key"),
                            E2ECryptoOperation::AgreementVerify,
                            dependencyBlockedResult(E2ECryptoOperation::AgreementVerify,
                                                    QStringLiteral("agreement-signature-input-not-ready")),
                            false);
    }

    const ProviderRunResult derivedSession = sessionKeyPassed && publicKeyPassed
        ? invokeOperation(E2ECryptoOperation::SessionDerive,
                          sessionPrimary,
                          sessionSecondary,
                          sessionAad)
        : dependencyBlockedResult(E2ECryptoOperation::SessionDerive,
                                  QStringLiteral("session-derive-input-not-ready"));
    const bool sessionDerivePassed = derivedSession.invoked
        && derivedSession.callbackStatus == QNC_E2E_STATUS_OK
        && derivedSession.outputStatus == QNC_E2E_STATUS_OK
        && derivedSession.materialPolicy == QNC_E2E_MATERIAL_HANDLE_ONLY
        && derivedSession.sealedOutput.size() == SessionKeyBytes;
    appendOperation(E2ECryptoOperation::SessionDerive,
                    derivedSession,
                    sessionDerivePassed,
                    QStringLiteral("session-derive"));

    if (sessionKeyPassed && publicKeyPassed) {
        const ProviderRunResult malformedSessionDerive =
            invokeOperation(E2ECryptoOperation::SessionDerive,
                            QByteArrayLiteral("malformed-session-derive-key"),
                            sessionSecondary,
                            sessionAad);
        appendNegativeCheck(QStringLiteral("session-derive-malformed-key"),
                            E2ECryptoOperation::SessionDerive,
                            malformedSessionDerive,
                            malformedSessionDerive.invoked
                                && malformedSessionDerive.callbackStatus == QNC_E2E_STATUS_INVALID_INPUT
                                && malformedSessionDerive.outputStatus == QNC_E2E_STATUS_INVALID_INPUT);
    } else {
        appendNegativeCheck(QStringLiteral("session-derive-malformed-key"),
                            E2ECryptoOperation::SessionDerive,
                            dependencyBlockedResult(E2ECryptoOperation::SessionDerive,
                                                    QStringLiteral("session-derive-input-not-ready")),
                            false);
    }

    const ProviderRunResult encryptedPayload = sessionDerivePassed
        ? invokeOperation(E2ECryptoOperation::PayloadEncrypt,
                          derivedSession.sealedOutput,
                          payload,
                          payloadAad)
        : dependencyBlockedResult(E2ECryptoOperation::PayloadEncrypt,
                                  QStringLiteral("session-derive-not-ready"));
    const bool payloadEncryptPassed = encryptedPayload.invoked
        && encryptedPayload.callbackStatus == QNC_E2E_STATUS_OK
        && encryptedPayload.outputStatus == QNC_E2E_STATUS_OK
        && encryptedPayload.materialPolicy == QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED
        && encryptedPayload.sealedOutput.size() == payload.size() + MinNonceBytes + MinTagBytes;
    appendOperation(E2ECryptoOperation::PayloadEncrypt,
                    encryptedPayload,
                    payloadEncryptPassed,
                    QStringLiteral("payload-encrypt"));

    const ProviderRunResult decryptedPayload = payloadEncryptPassed
        ? invokeOperation(E2ECryptoOperation::PayloadDecrypt,
                          derivedSession.sealedOutput,
                          encryptedPayload.sealedOutput,
                          payloadAad)
        : dependencyBlockedResult(E2ECryptoOperation::PayloadDecrypt,
                                  QStringLiteral("payload-encrypt-not-ready"));
    const bool payloadDecryptPassed = decryptedPayload.invoked
        && decryptedPayload.callbackStatus == QNC_E2E_STATUS_OK
        && decryptedPayload.outputStatus == QNC_E2E_STATUS_OK
        && decryptedPayload.materialPolicy == QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED
        && decryptedPayload.publicOutput == payload;
    appendOperation(E2ECryptoOperation::PayloadDecrypt,
                    decryptedPayload,
                    payloadDecryptPassed,
                    QStringLiteral("payload-decrypt"));

    if (payloadEncryptPassed) {
        QByteArray tamperedCiphertext = encryptedPayload.sealedOutput;
        if (!tamperedCiphertext.isEmpty()) {
            tamperedCiphertext[tamperedCiphertext.size() - 1] =
                static_cast<char>(tamperedCiphertext.at(tamperedCiphertext.size() - 1) ^ 0x01);
        }
        const ProviderRunResult tamperedDecrypt =
            invokeOperation(E2ECryptoOperation::PayloadDecrypt,
                            derivedSession.sealedOutput,
                            tamperedCiphertext,
                            payloadAad);
        appendNegativeCheck(QStringLiteral("payload-decrypt-tamper"),
                            E2ECryptoOperation::PayloadDecrypt,
                            tamperedDecrypt,
                            tamperedDecrypt.invoked
                                && tamperedDecrypt.callbackStatus == QNC_E2E_STATUS_REJECTED
                                && tamperedDecrypt.outputStatus == QNC_E2E_STATUS_REJECTED);
    } else {
        appendNegativeCheck(QStringLiteral("payload-decrypt-tamper"),
                            E2ECryptoOperation::PayloadDecrypt,
                            dependencyBlockedResult(E2ECryptoOperation::PayloadDecrypt,
                                                    QStringLiteral("payload-encrypt-not-ready")),
                            false);
    }

    if (sessionDerivePassed) {
        const ProviderRunResult malformedPayloadEncrypt =
            invokeOperation(E2ECryptoOperation::PayloadEncrypt,
                            derivedSession.sealedOutput.left(SessionKeyBytes - 1),
                            payload,
                            payloadAad);
        appendNegativeCheck(QStringLiteral("payload-encrypt-malformed-key"),
                            E2ECryptoOperation::PayloadEncrypt,
                            malformedPayloadEncrypt,
                            malformedPayloadEncrypt.invoked
                                && malformedPayloadEncrypt.callbackStatus == QNC_E2E_STATUS_INVALID_INPUT
                                && malformedPayloadEncrypt.outputStatus == QNC_E2E_STATUS_INVALID_INPUT);
    } else {
        appendNegativeCheck(QStringLiteral("payload-encrypt-malformed-key"),
                            E2ECryptoOperation::PayloadEncrypt,
                            dependencyBlockedResult(E2ECryptoOperation::PayloadEncrypt,
                                                    QStringLiteral("session-derive-not-ready")),
                            false);
    }

    const bool roundTripPassed = canUseTable
        && readyOperationCount == cryptoOperations().size()
        && invokedOperationCount == cryptoOperations().size()
        && statusConsistentOperationCount == cryptoOperations().size()
        && negativeCheckCount == 7
        && negativeCheckPassCount == negativeCheckCount;

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-round-trip-execution-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerTableRegistered")] = registered;
    status[QStringLiteral("tableValidationAccepted")] = tableValidationAccepted;
    status[QStringLiteral("providerTableRegistration")] = registration;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("roundTripReady")] = roundTripPassed;
    status[QStringLiteral("roundTripPassed")] = roundTripPassed;
    status[QStringLiteral("roundTripNonReleaseGate")] = true;
    status[QStringLiteral("requiredOperationCount")] = cryptoOperations().size();
    status[QStringLiteral("invokedOperationCount")] = invokedOperationCount;
    status[QStringLiteral("readyOperationCount")] = readyOperationCount;
    status[QStringLiteral("blockedOperationCount")] = blockedOperationCount;
    status[QStringLiteral("statusConsistentOperationCount")] = statusConsistentOperationCount;
    status[QStringLiteral("sanitizedOperationCount")] = sanitizedOperationCount;
    status[QStringLiteral("materialPolicyMatchedCount")] = materialPolicyMatchedCount;
    status[QStringLiteral("negativeCheckCount")] = negativeCheckCount;
    status[QStringLiteral("negativeCheckPassCount")] = negativeCheckPassCount;
    status[QStringLiteral("identityPublicDerivationMatched")] = publicKeyPassed;
    status[QStringLiteral("agreementSignatureVerified")] = verificationPassed;
    status[QStringLiteral("sessionDerivePassed")] = sessionDerivePassed;
    status[QStringLiteral("payloadRoundTripPassed")] = payloadDecryptPassed;
    status[QStringLiteral("tamperRejectedCount")] = negativeCheckPassCount;
    status[QStringLiteral("releaseGate")] =
        QStringLiteral("production-provider-round-trip-execution-not-release-gate");
    status[QStringLiteral("blockedReason")] = roundTripPassed
        ? QStringLiteral("production-provider-round-trip-execution-awaiting-audit-release-gate")
        : (!registered
            ? QStringLiteral("production-provider-table-not-registered")
            : (!tableValidationAccepted
                ? registration.value(QStringLiteral("tableValidationBlockedReason")).toString(
                    QStringLiteral("production-provider-table-validation-blocked"))
                : QStringLiteral("production-provider-round-trip-execution-failed")));
    status[QStringLiteral("operatorAction")] = roundTripPassed
        ? QStringLiteral("audit-round-trip-results-before-production-data-plane-release")
        : QStringLiteral("fix-reviewed-provider-round-trip-before-production-data-plane-release");
    status[QStringLiteral("operations")] = operations;
    status[QStringLiteral("negativeChecks")] = negativeChecks;
    status[QStringLiteral("operationInvoked")] = invokedOperationCount > 0;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultBytesCaptured")] = false;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    status[QStringLiteral("plaintextExported")] = false;
    status[QStringLiteral("ciphertextExported")] = false;
    return status;
}

QJsonObject productionProviderPublicPrimitiveExecutionProbeForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    const qnc_e2e_provider_table_v1* registeredTable = activeProductionProviderTable();
    const QJsonObject registration =
        productionProviderTableRegistrationStatusForDescriptor(descriptor);
    const bool registered = registeredTable != nullptr;
    const bool tableValidationAccepted =
        registration.value(QStringLiteral("tableValidationAccepted")).toBool(false);
    const QJsonObject dataPlaneBridge = e2eProbeProductionCryptoProviderDataPlaneBridge();
    const bool bridgeReady =
        dataPlaneBridge.value(QStringLiteral("readyBridgeCount")).toInt()
            == cryptoOperations().size()
        && dataPlaneBridge.value(QStringLiteral("bridgeCount")).toInt()
            == cryptoOperations().size();
    const bool canUseTable = registered && tableValidationAccepted && bridgeReady;

    struct ProviderRunResult {
        bool pointerPresent = false;
        bool invoked = false;
        qnc_e2e_status_t callbackStatus = QNC_E2E_STATUS_UNSUPPORTED;
        qnc_e2e_status_t outputStatus = QNC_E2E_STATUS_UNSUPPORTED;
        qnc_e2e_material_policy_t materialPolicy = QNC_E2E_MATERIAL_HANDLE_ONLY;
        QString sanitizedErrorClass = QStringLiteral("not-invoked");
        QString blockedReason;
        QByteArray publicOutput;
        QByteArray sealedOutput;
    };

    const auto outputBytes = [](const qnc_e2e_buffer_view_v1& view) {
        if (!view.data || view.size == 0) {
            return QByteArray();
        }
        return QByteArray(reinterpret_cast<const char*>(view.data),
                          static_cast<qsizetype>(view.size));
    };
    const auto invokeOperation =
        [&](E2ECryptoOperation operation,
            const QByteArray& primary,
            const QByteArray& secondary,
            const QByteArray& aad) {
            ProviderRunResult result;
            const qnc_e2e_provider_operation_v1 callback =
                providerOperationPointer(registeredTable, operation);
            result.pointerPresent = callback != nullptr;
            if (!registered) {
                result.blockedReason = QStringLiteral("production-provider-table-not-registered");
                return result;
            }
            if (!tableValidationAccepted) {
                result.blockedReason =
                    registration.value(QStringLiteral("tableValidationBlockedReason")).toString(
                        QStringLiteral("production-provider-table-validation-blocked"));
                return result;
            }
            if (!callback) {
                result.blockedReason =
                    QStringLiteral("production-provider-operation-pointer-missing");
                return result;
            }

            qnc_e2e_operation_input_v1 input = {};
            input.operation = providerOperationEnum(operation);
            input.suite_id = E2EProductionSuite;
            input.primary.data = reinterpret_cast<const uint8_t*>(primary.constData());
            input.primary.size = static_cast<size_t>(primary.size());
            input.secondary.data = reinterpret_cast<const uint8_t*>(secondary.constData());
            input.secondary.size = static_cast<size_t>(secondary.size());
            input.aad.data = reinterpret_cast<const uint8_t*>(aad.constData());
            input.aad.size = static_cast<size_t>(aad.size());

            qnc_e2e_operation_output_v1 output = {};
            output.status = QNC_E2E_STATUS_UNSUPPORTED;
            output.material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
            output.sanitized_error_class = "not-invoked";

            result.callbackStatus = callback(&input, &output);
            result.outputStatus = output.status;
            result.materialPolicy = output.material_policy;
            result.sanitizedErrorClass = output.sanitized_error_class
                ? sanitizedBackendId(QString::fromLatin1(output.sanitized_error_class))
                : QStringLiteral("missing-error-class");
            result.publicOutput = outputBytes(output.public_output);
            result.sealedOutput = outputBytes(output.sealed_output);
            result.invoked = true;
            return result;
        };
    const auto dependencyBlockedResult = [&](E2ECryptoOperation operation,
                                             const QString& blockedReason) {
        ProviderRunResult result;
        result.pointerPresent = providerOperationPointer(registeredTable, operation) != nullptr;
        result.blockedReason = blockedReason;
        return result;
    };

    QJsonArray operations;
    QJsonArray negativeChecks;
    int invokedOperationCount = 0;
    int readyOperationCount = 0;
    int blockedOperationCount = 0;
    int publicApiMappedOperationCount = 0;
    int bridgeReadyOperationCount = 0;
    int statusConsistentOperationCount = 0;
    int sanitizedOperationCount = 0;
    int materialPolicyMatchedCount = 0;
    int outputShapeHashCount = 0;
    int sequenceIndex = 0;
    const auto appendOperation =
        [&](E2ECryptoOperation operation,
            const ProviderRunResult& result,
            bool stepPassed,
            const QString& stepId,
            qint64 primarySize,
            qint64 secondarySize,
            qint64 aadSize) {
            const QString statusClass = result.invoked
                ? providerStatusClass(result.callbackStatus)
                : QStringLiteral("not-invoked");
            const QString outputStatusClass = result.invoked
                ? providerStatusClass(result.outputStatus)
                : QStringLiteral("not-invoked");
            const QString materialPolicyClass = result.invoked
                ? providerMaterialPolicyClass(result.materialPolicy)
                : QStringLiteral("not-invoked");
            const bool statusConsistent = result.invoked
                && statusClass == outputStatusClass;
            const bool sanitized = !result.invoked
                || (!result.sanitizedErrorClass.isEmpty()
                    && result.sanitizedErrorClass.size() <= 96);
            const bool materialPolicyMatched = result.invoked
                && (materialPolicyClass == productionProbeExpectedMaterialPolicyClass(operation)
                    || (operation == E2ECryptoOperation::IdentityKeyGeneration
                        && materialPolicyClass == QStringLiteral("handle-only")));
            const QJsonObject bridge = sequenceIndex
                    < dataPlaneBridge.value(QStringLiteral("bridges")).toArray().size()
                ? dataPlaneBridge.value(QStringLiteral("bridges")).toArray()
                    .at(sequenceIndex).toObject()
                : QJsonObject();
            const QString publicApi = productionOperationPublicApi(operation);
            const bool publicApiMapped =
                bridge.value(QStringLiteral("operation")).toString() == cryptoOperationName(operation)
                && bridge.value(QStringLiteral("publicApi")).toString() == publicApi;
            const bool bridgeStepReady =
                bridge.value(QStringLiteral("dataPlaneBridgeReady")).toBool(false);
            QByteArray shape;
            shape.append("qtnetworkchat-e2e-public-primitive-execution-shape-v1|");
            shape.append(cryptoOperationName(operation).toUtf8());
            shape.append('|');
            shape.append(statusClass.toUtf8());
            shape.append('|');
            shape.append(outputStatusClass.toUtf8());
            shape.append('|');
            shape.append(materialPolicyClass.toUtf8());
            shape.append('|');
            shape.append(QByteArray::number(result.invoked ? result.publicOutput.size() : 0));
            shape.append('|');
            shape.append(QByteArray::number(result.invoked ? result.sealedOutput.size() : 0));

            QJsonObject op;
            op[QStringLiteral("sequenceIndex")] = sequenceIndex++;
            op[QStringLiteral("stepId")] = stepId;
            op[QStringLiteral("operation")] = cryptoOperationName(operation);
            op[QStringLiteral("backendId")] = descriptor.id;
            op[QStringLiteral("providerId")] = descriptor.providerId;
            op[QStringLiteral("operationContractVersion")] =
                descriptor.operationContractVersion;
            op[QStringLiteral("publicApi")] = publicApi;
            op[QStringLiteral("publicDataPlaneBoundary")] =
                productionOperationPublicDataPlaneBoundary(operation);
            op[QStringLiteral("providerSymbol")] =
                productionOperationProviderSymbol(operation);
            op[QStringLiteral("providerAbiSignature")] =
                productionOperationProviderAbiSignature(operation);
            op[QStringLiteral("dataPlaneBridgeReady")] = bridgeStepReady;
            op[QStringLiteral("publicApiMapped")] = publicApiMapped;
            op[QStringLiteral("functionPointerPresent")] = result.pointerPresent;
            op[QStringLiteral("operationInvoked")] = result.invoked;
            op[QStringLiteral("callbackStatusClass")] = statusClass;
            op[QStringLiteral("outputStatusClass")] = outputStatusClass;
            op[QStringLiteral("statusConsistent")] = statusConsistent;
            op[QStringLiteral("materialPolicyClass")] = materialPolicyClass;
            op[QStringLiteral("materialPolicyMatched")] = materialPolicyMatched;
            op[QStringLiteral("sanitizedErrorClass")] = result.sanitizedErrorClass;
            op[QStringLiteral("inputPrimarySize")] = primarySize;
            op[QStringLiteral("inputSecondarySize")] = secondarySize;
            op[QStringLiteral("inputAadSize")] = aadSize;
            op[QStringLiteral("publicOutputSize")] = result.invoked
                ? static_cast<int>(result.publicOutput.size())
                : 0;
            op[QStringLiteral("sealedOutputSize")] = result.invoked
                ? static_cast<int>(result.sealedOutput.size())
                : 0;
            op[QStringLiteral("outputShapeHashSha256")] = e2eFingerprint(shape);
            op[QStringLiteral("publicPrimitiveStepPassed")] = stepPassed;
            op[QStringLiteral("blockedReason")] = stepPassed
                ? QString()
                : (result.blockedReason.isEmpty()
                    ? QStringLiteral("production-public-primitive-execution-step-failed")
                    : result.blockedReason);
            op[QStringLiteral("byteFlowScope")] =
                QStringLiteral("internal-public-primitive-probe-only-not-exported");
            op[QStringLiteral("publicApiInvoked")] = false;
            op[QStringLiteral("providerInvokedByPublicPrimitive")] = result.invoked;
            op[QStringLiteral("inputBytesCaptured")] = false;
            op[QStringLiteral("outputBytesCaptured")] = false;
            op[QStringLiteral("resultBytesCaptured")] = false;
            op[QStringLiteral("rawKeyExported")] = false;
            op[QStringLiteral("privateMaterialExported")] = false;
            op[QStringLiteral("sessionSecretExported")] = false;
            op[QStringLiteral("privateIdentityMaterialExported")] = false;
            op[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
            op[QStringLiteral("plaintextExported")] = false;
            op[QStringLiteral("ciphertextExported")] = false;
            operations.append(op);

            if (result.invoked) {
                ++invokedOperationCount;
            }
            if (stepPassed) {
                ++readyOperationCount;
            } else {
                ++blockedOperationCount;
            }
            if (publicApiMapped) {
                ++publicApiMappedOperationCount;
            }
            if (bridgeStepReady) {
                ++bridgeReadyOperationCount;
            }
            if (statusConsistent) {
                ++statusConsistentOperationCount;
            }
            if (sanitized) {
                ++sanitizedOperationCount;
            }
            if (materialPolicyMatched) {
                ++materialPolicyMatchedCount;
            }
            if (op.value(QStringLiteral("outputShapeHashSha256")).toString().size()
                == FingerprintHexLength) {
                ++outputShapeHashCount;
            }
        };
    int negativeCheckCount = 0;
    int negativeCheckPassCount = 0;
    const auto appendNegativeCheck =
        [&](const QString& checkId,
            E2ECryptoOperation operation,
            const ProviderRunResult& result,
            bool rejectedAsExpected) {
            ++negativeCheckCount;
            if (rejectedAsExpected) {
                ++negativeCheckPassCount;
            }
            QJsonObject check;
            check[QStringLiteral("checkId")] = checkId;
            check[QStringLiteral("operation")] = cryptoOperationName(operation);
            check[QStringLiteral("publicApi")] = productionOperationPublicApi(operation);
            check[QStringLiteral("providerSymbol")] =
                productionOperationProviderSymbol(operation);
            check[QStringLiteral("operationInvoked")] = result.invoked;
            check[QStringLiteral("callbackStatusClass")] = result.invoked
                ? providerStatusClass(result.callbackStatus)
                : QStringLiteral("not-invoked");
            check[QStringLiteral("outputStatusClass")] = result.invoked
                ? providerStatusClass(result.outputStatus)
                : QStringLiteral("not-invoked");
            check[QStringLiteral("sanitizedErrorClass")] = result.sanitizedErrorClass;
            check[QStringLiteral("rejectedAsExpected")] = rejectedAsExpected;
            check[QStringLiteral("blockedReason")] = rejectedAsExpected
                ? QString()
                : (result.blockedReason.isEmpty()
                    ? QStringLiteral("production-public-primitive-negative-check-failed")
                    : result.blockedReason);
            check[QStringLiteral("publicApiInvoked")] = false;
            check[QStringLiteral("inputBytesCaptured")] = false;
            check[QStringLiteral("outputBytesCaptured")] = false;
            check[QStringLiteral("rawKeyExported")] = false;
            check[QStringLiteral("privateMaterialExported")] = false;
            check[QStringLiteral("sessionSecretExported")] = false;
            check[QStringLiteral("plaintextExported")] = false;
            check[QStringLiteral("ciphertextExported")] = false;
            negativeChecks.append(check);
        };

    const QByteArray empty;
    const QByteArray agreementTranscript =
        QByteArrayLiteral("qnc-public-primitive-agreement-transcript-v1");
    const QByteArray sessionPrimary =
        QByteArray(ProductionSessionDerivePrimaryDomain)
        + QByteArrayLiteral("qnc-public-primitive-session-primary-v1");
    const QByteArray sessionSecondary =
        QByteArray(ProductionSessionDeriveSecondaryDomain)
        + QByteArrayLiteral("qnc-public-primitive-session-secondary-v1");
    const QByteArray sessionContext =
        QByteArrayLiteral("qnc-public-primitive-session-context-v1");
    const QByteArray plaintext =
        QByteArrayLiteral("public-primitive-provider-payload");
    const QByteArray payloadAad =
        QByteArrayLiteral("qnc-public-primitive-payload-aad-v1");

    const ProviderRunResult sessionKey =
        invokeOperation(E2ECryptoOperation::SessionKeyGeneration, empty, empty, empty);
    const bool sessionKeyPassed = sessionKey.invoked
        && sessionKey.callbackStatus == QNC_E2E_STATUS_OK
        && sessionKey.outputStatus == QNC_E2E_STATUS_OK
        && sessionKey.materialPolicy == QNC_E2E_MATERIAL_HANDLE_ONLY
        && sessionKey.sealedOutput.size() == SessionKeyBytes;
    appendOperation(E2ECryptoOperation::SessionKeyGeneration,
                    sessionKey,
                    sessionKeyPassed,
                    QStringLiteral("public-generate-session-key"),
                    0,
                    0,
                    0);

    const ProviderRunResult identityKey =
        invokeOperation(E2ECryptoOperation::IdentityKeyGeneration, empty, empty, empty);
    const bool identityKeyPassed = identityKey.invoked
        && identityKey.callbackStatus == QNC_E2E_STATUS_OK
        && identityKey.outputStatus == QNC_E2E_STATUS_OK
        && identityKey.publicOutput.size() == 32
        && identityKey.sealedOutput.size() == 32;
    appendOperation(E2ECryptoOperation::IdentityKeyGeneration,
                    identityKey,
                    identityKeyPassed,
                    QStringLiteral("public-generate-private-key"),
                    0,
                    0,
                    0);

    const ProviderRunResult publicKey = identityKeyPassed
        ? invokeOperation(E2ECryptoOperation::PublicKeyDerivation,
                          identityKey.sealedOutput,
                          empty,
                          empty)
        : dependencyBlockedResult(E2ECryptoOperation::PublicKeyDerivation,
                                  QStringLiteral("identity-generation-not-ready"));
    const bool publicKeyPassed = publicKey.invoked
        && publicKey.callbackStatus == QNC_E2E_STATUS_OK
        && publicKey.outputStatus == QNC_E2E_STATUS_OK
        && publicKey.materialPolicy == QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED
        && publicKey.publicOutput == identityKey.publicOutput
        && publicKey.publicOutput.size() == 32;
    appendOperation(E2ECryptoOperation::PublicKeyDerivation,
                    publicKey,
                    publicKeyPassed,
                    QStringLiteral("public-derive-public-key"),
                    identityKeyPassed ? identityKey.sealedOutput.size() : 0,
                    0,
                    0);

    const ProviderRunResult signature = identityKeyPassed
        ? invokeOperation(E2ECryptoOperation::AgreementSign,
                          identityKey.sealedOutput,
                          agreementTranscript,
                          empty)
        : dependencyBlockedResult(E2ECryptoOperation::AgreementSign,
                                  QStringLiteral("identity-generation-not-ready"));
    const bool signaturePassed = signature.invoked
        && signature.callbackStatus == QNC_E2E_STATUS_OK
        && signature.outputStatus == QNC_E2E_STATUS_OK
        && signature.materialPolicy == QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED
        && signature.publicOutput.size() == 64;
    appendOperation(E2ECryptoOperation::AgreementSign,
                    signature,
                    signaturePassed,
                    QStringLiteral("public-sign-key-agreement"),
                    identityKeyPassed ? identityKey.sealedOutput.size() : 0,
                    agreementTranscript.size(),
                    0);

    const ProviderRunResult verification = publicKeyPassed && signaturePassed
        ? invokeOperation(E2ECryptoOperation::AgreementVerify,
                          publicKey.publicOutput,
                          agreementTranscript,
                          signature.publicOutput)
        : dependencyBlockedResult(E2ECryptoOperation::AgreementVerify,
                                  QStringLiteral("agreement-signature-input-not-ready"));
    const bool verificationPassed = verification.invoked
        && verification.callbackStatus == QNC_E2E_STATUS_OK
        && verification.outputStatus == QNC_E2E_STATUS_OK
        && verification.materialPolicy == QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED;
    appendOperation(E2ECryptoOperation::AgreementVerify,
                    verification,
                    verificationPassed,
                    QStringLiteral("public-verify-key-agreement"),
                    publicKeyPassed ? publicKey.publicOutput.size() : 0,
                    agreementTranscript.size(),
                    signaturePassed ? signature.publicOutput.size() : 0);

    if (publicKeyPassed && signaturePassed) {
        QByteArray tamperedSignature = signature.publicOutput;
        if (!tamperedSignature.isEmpty()) {
            tamperedSignature[0] = static_cast<char>(tamperedSignature.at(0) ^ 0x01);
        }
        const ProviderRunResult tamperedVerify =
            invokeOperation(E2ECryptoOperation::AgreementVerify,
                            publicKey.publicOutput,
                            agreementTranscript,
                            tamperedSignature);
        appendNegativeCheck(QStringLiteral("public-agreement-verify-tamper"),
                            E2ECryptoOperation::AgreementVerify,
                            tamperedVerify,
                            tamperedVerify.invoked
                                && tamperedVerify.callbackStatus == QNC_E2E_STATUS_REJECTED
                                && tamperedVerify.outputStatus == QNC_E2E_STATUS_REJECTED);
    } else {
        appendNegativeCheck(QStringLiteral("public-agreement-verify-tamper"),
                            E2ECryptoOperation::AgreementVerify,
                            dependencyBlockedResult(E2ECryptoOperation::AgreementVerify,
                                                    QStringLiteral("agreement-signature-input-not-ready")),
                            false);
    }

    if (identityKeyPassed) {
        const QByteArray malformedIdentityHandle =
            identityKey.sealedOutput.left(SessionKeyBytes - 1);
        const ProviderRunResult malformedPublicDerivation =
            invokeOperation(E2ECryptoOperation::PublicKeyDerivation,
                            malformedIdentityHandle,
                            empty,
                            empty);
        appendNegativeCheck(QStringLiteral("public-key-derivation-malformed-handle"),
                            E2ECryptoOperation::PublicKeyDerivation,
                            malformedPublicDerivation,
                            malformedPublicDerivation.invoked
                                && malformedPublicDerivation.callbackStatus == QNC_E2E_STATUS_INVALID_INPUT
                                && malformedPublicDerivation.outputStatus == QNC_E2E_STATUS_INVALID_INPUT);

        const ProviderRunResult malformedAgreementSign =
            invokeOperation(E2ECryptoOperation::AgreementSign,
                            malformedIdentityHandle,
                            agreementTranscript,
                            empty);
        appendNegativeCheck(QStringLiteral("public-agreement-sign-malformed-handle"),
                            E2ECryptoOperation::AgreementSign,
                            malformedAgreementSign,
                            malformedAgreementSign.invoked
                                && malformedAgreementSign.callbackStatus == QNC_E2E_STATUS_INVALID_INPUT
                                && malformedAgreementSign.outputStatus == QNC_E2E_STATUS_INVALID_INPUT);
    } else {
        appendNegativeCheck(QStringLiteral("public-key-derivation-malformed-handle"),
                            E2ECryptoOperation::PublicKeyDerivation,
                            dependencyBlockedResult(E2ECryptoOperation::PublicKeyDerivation,
                                                    QStringLiteral("identity-generation-not-ready")),
                            false);
        appendNegativeCheck(QStringLiteral("public-agreement-sign-malformed-handle"),
                            E2ECryptoOperation::AgreementSign,
                            dependencyBlockedResult(E2ECryptoOperation::AgreementSign,
                                                    QStringLiteral("identity-generation-not-ready")),
                            false);
    }

    if (signaturePassed) {
        const ProviderRunResult malformedVerifyPublicKey =
            invokeOperation(E2ECryptoOperation::AgreementVerify,
                            publicKey.publicOutput.left(31),
                            agreementTranscript,
                            signature.publicOutput);
        appendNegativeCheck(QStringLiteral("public-agreement-verify-malformed-public-key"),
                            E2ECryptoOperation::AgreementVerify,
                            malformedVerifyPublicKey,
                            malformedVerifyPublicKey.invoked
                                && malformedVerifyPublicKey.callbackStatus == QNC_E2E_STATUS_INVALID_INPUT
                                && malformedVerifyPublicKey.outputStatus == QNC_E2E_STATUS_INVALID_INPUT);
    } else {
        appendNegativeCheck(QStringLiteral("public-agreement-verify-malformed-public-key"),
                            E2ECryptoOperation::AgreementVerify,
                            dependencyBlockedResult(E2ECryptoOperation::AgreementVerify,
                                                    QStringLiteral("agreement-signature-input-not-ready")),
                            false);
    }

    const ProviderRunResult derivedSession = sessionKeyPassed && publicKeyPassed
        ? invokeOperation(E2ECryptoOperation::SessionDerive,
                          sessionPrimary,
                          sessionSecondary,
                          sessionContext)
        : dependencyBlockedResult(E2ECryptoOperation::SessionDerive,
                                  QStringLiteral("session-derive-input-not-ready"));
    const bool sessionDerivePassed = derivedSession.invoked
        && derivedSession.callbackStatus == QNC_E2E_STATUS_OK
        && derivedSession.outputStatus == QNC_E2E_STATUS_OK
        && derivedSession.materialPolicy == QNC_E2E_MATERIAL_HANDLE_ONLY
        && derivedSession.sealedOutput.size() == SessionKeyBytes;
    appendOperation(E2ECryptoOperation::SessionDerive,
                    derivedSession,
                    sessionDerivePassed,
                    QStringLiteral("public-derive-authenticated-session"),
                    sessionKeyPassed ? sessionKey.sealedOutput.size() : 0,
                    publicKeyPassed ? publicKey.publicOutput.size() : 0,
                    sessionContext.size());

    if (sessionKeyPassed && publicKeyPassed) {
        const ProviderRunResult malformedSessionDerive =
            invokeOperation(E2ECryptoOperation::SessionDerive,
                            QByteArrayLiteral("malformed-session-derive-key"),
                            sessionSecondary,
                            sessionContext);
        appendNegativeCheck(QStringLiteral("public-session-derive-malformed-key"),
                            E2ECryptoOperation::SessionDerive,
                            malformedSessionDerive,
                            malformedSessionDerive.invoked
                                && malformedSessionDerive.callbackStatus == QNC_E2E_STATUS_INVALID_INPUT
                                && malformedSessionDerive.outputStatus == QNC_E2E_STATUS_INVALID_INPUT);
    } else {
        appendNegativeCheck(QStringLiteral("public-session-derive-malformed-key"),
                            E2ECryptoOperation::SessionDerive,
                            dependencyBlockedResult(E2ECryptoOperation::SessionDerive,
                                                    QStringLiteral("session-derive-input-not-ready")),
                            false);
    }

    const ProviderRunResult encryptedPayload = sessionDerivePassed
        ? invokeOperation(E2ECryptoOperation::PayloadEncrypt,
                          derivedSession.sealedOutput,
                          plaintext,
                          payloadAad)
        : dependencyBlockedResult(E2ECryptoOperation::PayloadEncrypt,
                                  QStringLiteral("session-derive-not-ready"));
    const bool payloadEncryptPassed = encryptedPayload.invoked
        && encryptedPayload.callbackStatus == QNC_E2E_STATUS_OK
        && encryptedPayload.outputStatus == QNC_E2E_STATUS_OK
        && encryptedPayload.materialPolicy == QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED
        && encryptedPayload.sealedOutput.size() == plaintext.size() + MinNonceBytes + MinTagBytes;
    appendOperation(E2ECryptoOperation::PayloadEncrypt,
                    encryptedPayload,
                    payloadEncryptPassed,
                    QStringLiteral("public-encrypt-payload"),
                    sessionDerivePassed ? derivedSession.sealedOutput.size() : 0,
                    plaintext.size(),
                    payloadAad.size());

    const ProviderRunResult decryptedPayload = payloadEncryptPassed
        ? invokeOperation(E2ECryptoOperation::PayloadDecrypt,
                          derivedSession.sealedOutput,
                          encryptedPayload.sealedOutput,
                          payloadAad)
        : dependencyBlockedResult(E2ECryptoOperation::PayloadDecrypt,
                                  QStringLiteral("payload-encrypt-not-ready"));
    const bool payloadDecryptPassed = decryptedPayload.invoked
        && decryptedPayload.callbackStatus == QNC_E2E_STATUS_OK
        && decryptedPayload.outputStatus == QNC_E2E_STATUS_OK
        && decryptedPayload.materialPolicy == QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED
        && decryptedPayload.publicOutput == plaintext;
    appendOperation(E2ECryptoOperation::PayloadDecrypt,
                    decryptedPayload,
                    payloadDecryptPassed,
                    QStringLiteral("public-decrypt-payload"),
                    sessionDerivePassed ? derivedSession.sealedOutput.size() : 0,
                    payloadEncryptPassed ? encryptedPayload.sealedOutput.size() : 0,
                    payloadAad.size());

    if (payloadEncryptPassed) {
        QByteArray tamperedCiphertext = encryptedPayload.sealedOutput;
        if (!tamperedCiphertext.isEmpty()) {
            tamperedCiphertext[tamperedCiphertext.size() - 1] =
                static_cast<char>(tamperedCiphertext.at(tamperedCiphertext.size() - 1) ^ 0x01);
        }
        const ProviderRunResult tamperedDecrypt =
            invokeOperation(E2ECryptoOperation::PayloadDecrypt,
                            derivedSession.sealedOutput,
                            tamperedCiphertext,
                            payloadAad);
        appendNegativeCheck(QStringLiteral("public-payload-decrypt-tamper"),
                            E2ECryptoOperation::PayloadDecrypt,
                            tamperedDecrypt,
                            tamperedDecrypt.invoked
                                && tamperedDecrypt.callbackStatus == QNC_E2E_STATUS_REJECTED
                                && tamperedDecrypt.outputStatus == QNC_E2E_STATUS_REJECTED);
    } else {
        appendNegativeCheck(QStringLiteral("public-payload-decrypt-tamper"),
                            E2ECryptoOperation::PayloadDecrypt,
                            dependencyBlockedResult(E2ECryptoOperation::PayloadDecrypt,
                                                    QStringLiteral("payload-encrypt-not-ready")),
                            false);
    }

    if (sessionDerivePassed) {
        const ProviderRunResult malformedPayloadEncrypt =
            invokeOperation(E2ECryptoOperation::PayloadEncrypt,
                            derivedSession.sealedOutput.left(SessionKeyBytes - 1),
                            plaintext,
                            payloadAad);
        appendNegativeCheck(QStringLiteral("public-payload-encrypt-malformed-key"),
                            E2ECryptoOperation::PayloadEncrypt,
                            malformedPayloadEncrypt,
                            malformedPayloadEncrypt.invoked
                                && malformedPayloadEncrypt.callbackStatus == QNC_E2E_STATUS_INVALID_INPUT
                                && malformedPayloadEncrypt.outputStatus == QNC_E2E_STATUS_INVALID_INPUT);
    } else {
        appendNegativeCheck(QStringLiteral("public-payload-encrypt-malformed-key"),
                            E2ECryptoOperation::PayloadEncrypt,
                            dependencyBlockedResult(E2ECryptoOperation::PayloadEncrypt,
                                                    QStringLiteral("session-derive-not-ready")),
                            false);
    }

    const bool publicPrimitiveReady = canUseTable
        && readyOperationCount == cryptoOperations().size()
        && invokedOperationCount == cryptoOperations().size()
        && publicApiMappedOperationCount == cryptoOperations().size()
        && bridgeReadyOperationCount == cryptoOperations().size()
        && statusConsistentOperationCount == cryptoOperations().size()
        && negativeCheckCount == 7
        && negativeCheckPassCount == negativeCheckCount;

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-public-primitive-execution-probe-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerTableRegistered")] = registered;
    status[QStringLiteral("tableValidationAccepted")] = tableValidationAccepted;
    status[QStringLiteral("providerTableRegistration")] = registration;
    status[QStringLiteral("providerDataPlaneBridge")] = dataPlaneBridge;
    status[QStringLiteral("providerDataPlaneBridgeReady")] = bridgeReady;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("publicPrimitiveReady")] = publicPrimitiveReady;
    status[QStringLiteral("publicPrimitivePassed")] = publicPrimitiveReady;
    status[QStringLiteral("publicPrimitiveExecutionNonReleaseGate")] = true;
    status[QStringLiteral("requiredOperationCount")] = cryptoOperations().size();
    status[QStringLiteral("invokedOperationCount")] = invokedOperationCount;
    status[QStringLiteral("readyOperationCount")] = readyOperationCount;
    status[QStringLiteral("blockedOperationCount")] = blockedOperationCount;
    status[QStringLiteral("publicApiMappedOperationCount")] =
        publicApiMappedOperationCount;
    status[QStringLiteral("bridgeReadyOperationCount")] = bridgeReadyOperationCount;
    status[QStringLiteral("statusConsistentOperationCount")] =
        statusConsistentOperationCount;
    status[QStringLiteral("sanitizedOperationCount")] = sanitizedOperationCount;
    status[QStringLiteral("materialPolicyMatchedCount")] =
        materialPolicyMatchedCount;
    status[QStringLiteral("outputShapeHashCount")] = outputShapeHashCount;
    status[QStringLiteral("negativeCheckCount")] = negativeCheckCount;
    status[QStringLiteral("negativeCheckPassCount")] = negativeCheckPassCount;
    status[QStringLiteral("identityPublicDerivationMatched")] = publicKeyPassed;
    status[QStringLiteral("agreementSignatureVerified")] = verificationPassed;
    status[QStringLiteral("sessionDerivePassed")] = sessionDerivePassed;
    status[QStringLiteral("payloadRoundTripPassed")] = payloadDecryptPassed;
    status[QStringLiteral("tamperRejectedCount")] = negativeCheckPassCount;
    status[QStringLiteral("releaseGate")] =
        QStringLiteral("production-public-primitive-execution-probe-not-release-gate");
    status[QStringLiteral("blockedReason")] = publicPrimitiveReady
        ? QStringLiteral("production-public-primitive-execution-awaiting-audit-release-gate")
        : (!registered
            ? QStringLiteral("production-provider-table-not-registered")
            : (!tableValidationAccepted
                ? registration.value(QStringLiteral("tableValidationBlockedReason")).toString(
                    QStringLiteral("production-provider-table-validation-blocked"))
                : (!bridgeReady
                    ? QStringLiteral("production-data-plane-bridge-not-ready")
                    : QStringLiteral("production-public-primitive-execution-failed"))));
    status[QStringLiteral("operatorAction")] = publicPrimitiveReady
        ? QStringLiteral("audit-public-primitive-results-before-production-data-plane-release")
        : QStringLiteral("fix-public-primitive-execution-before-production-data-plane-release");
    status[QStringLiteral("operations")] = operations;
    status[QStringLiteral("negativeChecks")] = negativeChecks;
    status[QStringLiteral("publicApiInvoked")] = false;
    status[QStringLiteral("providerInvokedByPublicPrimitive")] = invokedOperationCount > 0;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultBytesCaptured")] = false;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    status[QStringLiteral("plaintextExported")] = false;
    status[QStringLiteral("ciphertextExported")] = false;
    return status;
}
} // namespace E2EBackendStatus
