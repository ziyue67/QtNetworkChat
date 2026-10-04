#include "e2e_backend_status_p.h"
#include <QCryptographicHash>
#include <QHash>
#include <cstddef>

namespace E2EBackendStatus {
QString providerDataPlaneBridgeBlockedReason(const QJsonObject& executionAcceptance,
                                             bool acceptanceSourceCaptured,
                                             bool executionAcceptanceReady,
                                             bool publicApiMapped,
                                             bool contractReady,
                                             bool bridgeSanitized) {
    if (!acceptanceSourceCaptured) {
        return QStringLiteral("production-provider-data-plane-bridge-awaiting-execution-acceptance");
    }
    if (!executionAcceptanceReady) {
        return executionAcceptance.value(QStringLiteral("blockedReason")).toString(
            QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-blocked"));
    }
    if (!publicApiMapped) {
        return QStringLiteral("production-provider-data-plane-bridge-public-api-mapping-blocked");
    }
    if (!contractReady) {
        return QStringLiteral("production-provider-data-plane-bridge-contract-blocked");
    }
    if (!bridgeSanitized) {
        return QStringLiteral("production-provider-data-plane-bridge-sensitive-export-blocked");
    }
    return QStringLiteral("production-provider-data-plane-bridges-awaiting-audit-release-gate");
}

QJsonObject productionProviderDataPlaneBridgeStatusFromExecutionAcceptance(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedInvocationExecutionAcceptance) {
    const QJsonArray acceptances =
        reviewedInvocationExecutionAcceptance.value(QStringLiteral("acceptances")).toArray();
    const bool acceptanceSourceCaptured =
        reviewedInvocationExecutionAcceptance.value(QStringLiteral("schema")).toString()
            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-invocation-execution-acceptance-v1")
        && reviewedInvocationExecutionAcceptance.value(QStringLiteral("armingSourceCaptured")).toBool(false);

    QJsonArray bridges;
    int bridgeCount = 0;
    int readyBridgeCount = 0;
    int blockedBridgeCount = 0;
    int publicApiMappingCount = 0;
    int callbackMappingCount = 0;
    int contractBridgeCount = 0;
    int sanitizedBridgeCount = 0;
    int failClosedBridgeCount = 0;
    int executionAcceptanceReadyBridgeCount = 0;
    QJsonObject blockedReasonSummary;

    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QString operationName = cryptoOperationName(operation);
        const QJsonObject executionAcceptance = sequenceIndex < acceptances.size()
            ? acceptances.at(sequenceIndex).toObject()
            : QJsonObject();
        const QString publicApi = productionOperationPublicApi(operation);
        const QString callbackEntrypoint =
            QStringLiteral("qnc_e2e_provider_table_v1/%1")
                .arg(productionOperationProviderSymbol(operation));
        const bool executionAcceptanceReady =
            executionAcceptance.value(QStringLiteral("executionAcceptanceReady")).toBool(false);
        const bool publicApiMapped =
            !publicApi.isEmpty()
            && publicApi != QStringLiteral("unknown")
            && executionAcceptance.value(QStringLiteral("operation")).toString() == operationName
            && executionAcceptance.value(QStringLiteral("callbackEntrypoint")).toString()
                == callbackEntrypoint;
        const bool contractReady =
            executionAcceptance.value(QStringLiteral("inputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && executionAcceptance.value(QStringLiteral("outputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && executionAcceptance.value(QStringLiteral("expectedStatusClass")).toString()
                == QStringLiteral("ok")
            && executionAcceptance.value(QStringLiteral("expectedFailureClass")).toString()
                == QStringLiteral("none");
        const bool bridgeSanitized =
            executionAcceptance.value(QStringLiteral("sanitized")).toBool(false)
            && !executionAcceptance.value(QStringLiteral("operationInvokedByExecutionAcceptance")).toBool(true)
            && !executionAcceptance.value(QStringLiteral("inputBytesCaptured")).toBool(true)
            && !executionAcceptance.value(QStringLiteral("outputBytesCaptured")).toBool(true)
            && !executionAcceptance.value(QStringLiteral("resultCaptured")).toBool(true)
            && !executionAcceptance.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !executionAcceptance.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !executionAcceptance.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !executionAcceptance.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !executionAcceptance.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool bridgeReady =
            acceptanceSourceCaptured
            && executionAcceptanceReady
            && publicApiMapped
            && contractReady
            && bridgeSanitized;
        const QString blockedReason =
            providerDataPlaneBridgeBlockedReason(executionAcceptance,
                                                 acceptanceSourceCaptured,
                                                 executionAcceptanceReady,
                                                 publicApiMapped,
                                                 contractReady,
                                                 bridgeSanitized);

        QJsonObject bridge;
        bridge[QStringLiteral("sequenceIndex")] = sequenceIndex;
        bridge[QStringLiteral("operation")] = operationName;
        setProviderReportIdentity(bridge, descriptor);
        bridge[QStringLiteral("dataPlaneBridgeId")] =
            QStringLiteral("production-data-plane-bridge/%1/%2")
                .arg(spec.vectorSet, operationName);
        bridge[QStringLiteral("publicApi")] = publicApi;
        bridge[QStringLiteral("publicDataPlaneBoundary")] =
            productionOperationPublicDataPlaneBoundary(operation);
        bridge[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
        bridge[QStringLiteral("callbackEntrypoint")] = callbackEntrypoint;
        bridge[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        bridge[QStringLiteral("reviewedInvocationExecutionAcceptance")] =
            executionAcceptance;
        bridge[QStringLiteral("executionAcceptanceId")] =
            executionAcceptance.value(QStringLiteral("executionAcceptanceId")).toString(
                QStringLiteral("reviewed-invocation-execution-acceptance/%1/%2")
                    .arg(spec.vectorSet, operationName));
        bridge[QStringLiteral("executionAcceptanceReady")] =
            executionAcceptanceReady;
        bridge[QStringLiteral("publicApiMapped")] = publicApiMapped;
        bridge[QStringLiteral("callbackMapped")] =
            executionAcceptance.value(QStringLiteral("callbackEntrypoint")).toString()
                == callbackEntrypoint;
        bridge[QStringLiteral("contractReady")] = contractReady;
        bridge[QStringLiteral("inputContractHashSha256")] =
            executionAcceptance.value(QStringLiteral("inputContractHashSha256")).toString();
        bridge[QStringLiteral("outputContractHashSha256")] =
            executionAcceptance.value(QStringLiteral("outputContractHashSha256")).toString();
        bridge[QStringLiteral("expectedStatusClass")] =
            executionAcceptance.value(QStringLiteral("expectedStatusClass")).toString(
                QStringLiteral("ok"));
        bridge[QStringLiteral("expectedFailureClass")] =
            executionAcceptance.value(QStringLiteral("expectedFailureClass")).toString(
                QStringLiteral("none"));
        bridge[QStringLiteral("expectedVectorResultClass")] =
            executionAcceptance.value(QStringLiteral("expectedVectorResultClass")).toString(
                QStringLiteral("probe-vector-passed"));
        bridge[QStringLiteral("resultCapturePolicy")] =
            QStringLiteral("status-class-and-size-only");
        bridge[QStringLiteral("materialExportPolicy")] =
            QStringLiteral("sizes-and-status-only-no-secret-bytes");
        bridge[QStringLiteral("bridgeState")] = bridgeReady
            ? QStringLiteral("ready-for-reviewed-production-data-plane-bridge")
            : QStringLiteral("production-data-plane-bridge-blocked");
        bridge[QStringLiteral("dataPlaneBridgeReady")] = bridgeReady;
        bridge[QStringLiteral("bridgeSanitized")] = bridgeSanitized;
        bridge[QStringLiteral("failClosed")] = !bridgeReady;
        bridge[QStringLiteral("blockedReason")] = blockedReason;
        bridge[QStringLiteral("operatorAction")] = bridgeReady
            ? QStringLiteral("audit-production-data-plane-bridge-before-release-gate")
            : (acceptanceSourceCaptured
                ? QStringLiteral("fix-production-data-plane-bridge-before-audit")
                : QStringLiteral("produce-reviewed-execution-acceptance-before-data-plane-bridge"));
        bridge[QStringLiteral("dataPlaneBridgeNonReleaseGate")] = true;
        bridge[QStringLiteral("releaseGate")] =
            QStringLiteral("production-provider-data-plane-bridge-not-release-gate");
        bridge[QStringLiteral("publicApiInvoked")] = false;
        bridge[QStringLiteral("providerInvokedByBridge")] = false;
        bridge[QStringLiteral("inputBytesCaptured")] = false;
        bridge[QStringLiteral("outputBytesCaptured")] = false;
        bridge[QStringLiteral("resultCaptured")] = false;
        setNoKeyExportFields(bridge);
        bridge[QStringLiteral("sessionSecretExported")] = false;
        bridge[QStringLiteral("privateIdentityMaterialExported")] = false;
        bridge[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        bridges.append(bridge);

        ++bridgeCount;
        if (bridgeReady) {
            ++readyBridgeCount;
        } else {
            ++blockedBridgeCount;
            ++failClosedBridgeCount;
        }
        if (publicApiMapped) {
            ++publicApiMappingCount;
        }
        if (bridge.value(QStringLiteral("callbackMapped")).toBool(false)) {
            ++callbackMappingCount;
        }
        if (contractReady) {
            ++contractBridgeCount;
        }
        if (bridgeSanitized) {
            ++sanitizedBridgeCount;
        }
        if (executionAcceptanceReady) {
            ++executionAcceptanceReadyBridgeCount;
        }
        incrementSummaryCount(&blockedReasonSummary, blockedReason);
        ++sequenceIndex;
    }

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-data-plane-bridge-v1");
    setProviderReportIdentity(status, descriptor);
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("dataPlaneBridgeNonReleaseGate")] = true;
    status[QStringLiteral("releaseGate")] =
        QStringLiteral("production-provider-data-plane-bridge-not-release-gate");
    status[QStringLiteral("acceptanceSourceCaptured")] = acceptanceSourceCaptured;
    status[QStringLiteral("providerReviewedInvocationExecutionAcceptance")] =
        reviewedInvocationExecutionAcceptance;
    status[QStringLiteral("providerReviewedInvocationExecutionAcceptanceReleaseGate")] =
        reviewedInvocationExecutionAcceptance.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedInvocationExecutionAcceptanceReadyCount")] =
        reviewedInvocationExecutionAcceptance.value(QStringLiteral("readyAcceptanceCount")).toInt();
    status[QStringLiteral("bridgeCount")] = bridgeCount;
    status[QStringLiteral("readyBridgeCount")] = readyBridgeCount;
    status[QStringLiteral("blockedBridgeCount")] = blockedBridgeCount;
    status[QStringLiteral("publicApiMappingCount")] = publicApiMappingCount;
    status[QStringLiteral("callbackMappingCount")] = callbackMappingCount;
    status[QStringLiteral("contractBridgeCount")] = contractBridgeCount;
    status[QStringLiteral("sanitizedBridgeCount")] = sanitizedBridgeCount;
    status[QStringLiteral("failClosedBridgeCount")] = failClosedBridgeCount;
    status[QStringLiteral("executionAcceptanceReadyBridgeCount")] =
        executionAcceptanceReadyBridgeCount;
    status[QStringLiteral("blockedReason")] = readyBridgeCount == bridgeCount
        && bridgeCount == cryptoOperations().size()
        ? QStringLiteral("production-provider-data-plane-bridges-awaiting-audit-release-gate")
        : (acceptanceSourceCaptured
            ? QStringLiteral("production-provider-data-plane-bridge-evidence-blocked")
            : QStringLiteral("production-provider-data-plane-bridge-awaiting-execution-acceptance"));
    status[QStringLiteral("operatorAction")] =
        QStringLiteral("audit-production-data-plane-bridge-before-enabling-public-provider-dispatch");
    status[QStringLiteral("blockedReasonSummary")] = blockedReasonSummary;
    status[QStringLiteral("bridges")] = bridges;
    status[QStringLiteral("publicApiInvoked")] = false;
    status[QStringLiteral("providerInvokedByBridge")] = false;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultCaptured")] = false;
    setNoKeyExportFields(status);
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderDataPlaneBridgeStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    return productionProviderDataPlaneBridgeStatusFromExecutionAcceptance(
        descriptor,
        productionProviderReviewedInvocationExecutionAcceptanceStatusForDescriptor(descriptor));
}

QJsonObject productionProviderPublicPrimitiveExecutionStatusFromBridge(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& providerDataPlaneBridge) {
    const QJsonArray bridges =
        providerDataPlaneBridge.value(QStringLiteral("bridges")).toArray();
    const bool bridgeSourceCaptured =
        providerDataPlaneBridge.value(QStringLiteral("schema")).toString()
            == QStringLiteral("qtnetworkchat-e2e-production-provider-data-plane-bridge-v1")
        && providerDataPlaneBridge.value(QStringLiteral("bridgeCount")).toInt()
            == cryptoOperations().size();

    QJsonArray executions;
    int executionCount = 0;
    int readyExecutionCount = 0;
    int blockedExecutionCount = 0;
    int bridgeReadyExecutionCount = 0;
    int publicApiMappedExecutionCount = 0;
    int callbackMappedExecutionCount = 0;
    int sanitizedExecutionCount = 0;
    int nonReleaseExecutionCount = 0;
    int failClosedExecutionCount = 0;
    QJsonObject blockedReasonSummary;

    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QString operationName = cryptoOperationName(operation);
        const QJsonObject bridge = sequenceIndex < bridges.size()
            ? bridges.at(sequenceIndex).toObject()
            : QJsonObject();
        const QString publicApi = productionOperationPublicApi(operation);
        const bool bridgeReady =
            bridge.value(QStringLiteral("dataPlaneBridgeReady")).toBool(false);
        const bool publicApiMapped =
            bridge.value(QStringLiteral("operation")).toString() == operationName
            && bridge.value(QStringLiteral("publicApi")).toString() == publicApi;
        const bool callbackMapped =
            bridge.value(QStringLiteral("callbackEntrypoint")).toString()
                == QStringLiteral("qnc_e2e_provider_table_v1/%1")
                    .arg(productionOperationProviderSymbol(operation));
        const bool sanitized =
            bridge.value(QStringLiteral("bridgeSanitized")).toBool(false)
            && !bridge.value(QStringLiteral("publicApiInvoked")).toBool(true)
            && !bridge.value(QStringLiteral("providerInvokedByBridge")).toBool(true)
            && !bridge.value(QStringLiteral("inputBytesCaptured")).toBool(true)
            && !bridge.value(QStringLiteral("outputBytesCaptured")).toBool(true)
            && !bridge.value(QStringLiteral("resultCaptured")).toBool(true)
            && !bridge.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !bridge.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !bridge.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !bridge.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !bridge.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool executionReady =
            bridgeSourceCaptured && bridgeReady && publicApiMapped && callbackMapped && sanitized;
        const QString blockedReason = executionReady
            ? QStringLiteral("public-primitive-execution-awaiting-explicit-probe")
            : (!bridgeSourceCaptured
                ? QStringLiteral("production-public-primitive-execution-awaiting-data-plane-bridge")
                : (!bridgeReady
                    ? bridge.value(QStringLiteral("blockedReason")).toString(
                        QStringLiteral("production-data-plane-bridge-not-ready"))
                    : QStringLiteral("production-public-primitive-execution-evidence-blocked")));

        QJsonObject execution;
        execution[QStringLiteral("sequenceIndex")] = sequenceIndex;
        execution[QStringLiteral("operation")] = operationName;
        setProviderReportIdentity(execution, descriptor);
        execution[QStringLiteral("publicPrimitiveExecutionId")] =
            QStringLiteral("production-public-primitive-execution/%1/%2")
                .arg(spec.vectorSet, operationName);
        execution[QStringLiteral("publicApi")] = publicApi;
        execution[QStringLiteral("publicDataPlaneBoundary")] =
            productionOperationPublicDataPlaneBoundary(operation);
        execution[QStringLiteral("providerSymbol")] =
            productionOperationProviderSymbol(operation);
        execution[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        execution[QStringLiteral("dataPlaneBridgeId")] =
            bridge.value(QStringLiteral("dataPlaneBridgeId")).toString();
        execution[QStringLiteral("dataPlaneBridgeReleaseGate")] =
            bridge.value(QStringLiteral("releaseGate")).toString();
        execution[QStringLiteral("dataPlaneBridgeBlockedReason")] =
            bridge.value(QStringLiteral("blockedReason")).toString();
        execution[QStringLiteral("bridgeReady")] = bridgeReady;
        execution[QStringLiteral("publicApiMapped")] = publicApiMapped;
        execution[QStringLiteral("callbackMapped")] = callbackMapped;
        execution[QStringLiteral("executionReady")] = executionReady;
        execution[QStringLiteral("executionState")] = executionReady
            ? QStringLiteral("ready-for-explicit-public-primitive-execution-probe")
            : QStringLiteral("production-public-primitive-execution-blocked");
        execution[QStringLiteral("blockedReason")] = blockedReason;
        execution[QStringLiteral("operatorAction")] = executionReady
            ? QStringLiteral("run-explicit-public-primitive-execution-probe-before-production-release")
            : QStringLiteral("complete-production-data-plane-bridge-before-public-primitive-execution");
        execution[QStringLiteral("publicPrimitiveExecutionNonReleaseGate")] = true;
        execution[QStringLiteral("releaseGate")] =
            QStringLiteral("production-public-primitive-execution-not-release-gate");
        execution[QStringLiteral("publicApiInvoked")] = false;
        execution[QStringLiteral("providerInvokedByPublicPrimitive")] = false;
        execution[QStringLiteral("inputBytesCaptured")] = false;
        execution[QStringLiteral("outputBytesCaptured")] = false;
        execution[QStringLiteral("resultBytesCaptured")] = false;
        setNoKeyExportFields(execution);
        execution[QStringLiteral("sessionSecretExported")] = false;
        execution[QStringLiteral("privateIdentityMaterialExported")] = false;
        execution[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        execution[QStringLiteral("plaintextExported")] = false;
        execution[QStringLiteral("ciphertextExported")] = false;
        executions.append(execution);

        ++executionCount;
        if (executionReady) {
            ++readyExecutionCount;
        } else {
            ++blockedExecutionCount;
            ++failClosedExecutionCount;
        }
        if (bridgeReady) {
            ++bridgeReadyExecutionCount;
        }
        if (publicApiMapped) {
            ++publicApiMappedExecutionCount;
        }
        if (callbackMapped) {
            ++callbackMappedExecutionCount;
        }
        if (sanitized) {
            ++sanitizedExecutionCount;
        }
        if (execution.value(QStringLiteral("publicPrimitiveExecutionNonReleaseGate")).toBool(false)) {
            ++nonReleaseExecutionCount;
        }
        incrementSummaryCount(&blockedReasonSummary, blockedReason);
        ++sequenceIndex;
    }

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-public-primitive-execution-v1");
    setProviderReportIdentity(status, descriptor);
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("publicPrimitiveExecutionNonReleaseGate")] = true;
    status[QStringLiteral("releaseGate")] =
        QStringLiteral("production-public-primitive-execution-not-release-gate");
    status[QStringLiteral("bridgeSourceCaptured")] = bridgeSourceCaptured;
    QJsonObject providerDataPlaneBridgeSummary;
    providerDataPlaneBridgeSummary[QStringLiteral("schema")] =
        providerDataPlaneBridge.value(QStringLiteral("schema")).toString();
    providerDataPlaneBridgeSummary[QStringLiteral("releaseGate")] =
        providerDataPlaneBridge.value(QStringLiteral("releaseGate")).toString();
    providerDataPlaneBridgeSummary[QStringLiteral("bridgeCount")] =
        providerDataPlaneBridge.value(QStringLiteral("bridgeCount")).toInt();
    providerDataPlaneBridgeSummary[QStringLiteral("readyBridgeCount")] =
        providerDataPlaneBridge.value(QStringLiteral("readyBridgeCount")).toInt();
    providerDataPlaneBridgeSummary[QStringLiteral("blockedBridgeCount")] =
        providerDataPlaneBridge.value(QStringLiteral("blockedBridgeCount")).toInt();
    providerDataPlaneBridgeSummary[QStringLiteral("blockedReason")] =
        providerDataPlaneBridge.value(QStringLiteral("blockedReason")).toString();
    providerDataPlaneBridgeSummary[QStringLiteral("operatorAction")] =
        providerDataPlaneBridge.value(QStringLiteral("operatorAction")).toString();
    status[QStringLiteral("providerDataPlaneBridge")] = providerDataPlaneBridgeSummary;
    status[QStringLiteral("providerDataPlaneBridgeReleaseGate")] =
        providerDataPlaneBridge.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerDataPlaneBridgeReadyCount")] =
        providerDataPlaneBridge.value(QStringLiteral("readyBridgeCount")).toInt();
    status[QStringLiteral("executionCount")] = executionCount;
    status[QStringLiteral("readyExecutionCount")] = readyExecutionCount;
    status[QStringLiteral("blockedExecutionCount")] = blockedExecutionCount;
    status[QStringLiteral("bridgeReadyExecutionCount")] = bridgeReadyExecutionCount;
    status[QStringLiteral("publicApiMappedExecutionCount")] =
        publicApiMappedExecutionCount;
    status[QStringLiteral("callbackMappedExecutionCount")] =
        callbackMappedExecutionCount;
    status[QStringLiteral("sanitizedExecutionCount")] = sanitizedExecutionCount;
    status[QStringLiteral("nonReleaseExecutionCount")] = nonReleaseExecutionCount;
    status[QStringLiteral("failClosedExecutionCount")] = failClosedExecutionCount;
    status[QStringLiteral("blockedReason")] = readyExecutionCount == executionCount
        && executionCount == cryptoOperations().size()
        ? QStringLiteral("production-public-primitive-executions-awaiting-explicit-probe")
        : (bridgeSourceCaptured
            ? QStringLiteral("production-public-primitive-execution-evidence-blocked")
            : QStringLiteral("production-public-primitive-execution-awaiting-data-plane-bridge"));
    status[QStringLiteral("operatorAction")] =
        QStringLiteral("run-explicit-public-primitive-execution-probe-before-production-dispatch");
    status[QStringLiteral("blockedReasonSummary")] = blockedReasonSummary;
    status[QStringLiteral("executions")] = executions;
    status[QStringLiteral("publicApiInvoked")] = false;
    status[QStringLiteral("providerInvokedByPublicPrimitive")] = false;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultBytesCaptured")] = false;
    setNoKeyExportFields(status);
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    status[QStringLiteral("plaintextExported")] = false;
    status[QStringLiteral("ciphertextExported")] = false;
    return status;
}

QJsonObject productionProviderPublicPrimitiveExecutionStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    return productionProviderPublicPrimitiveExecutionStatusFromBridge(
        descriptor,
        productionProviderDataPlaneBridgeStatusForDescriptor(descriptor));
}

QJsonObject providerCompatibilityEvidence(const E2ECryptoAdapterDescriptor& descriptor) {
    const bool isDraft = descriptor.id == QString::fromLatin1(DraftBackendId);
    const bool isProduction = descriptor.id == QString::fromLatin1(ProductionBackendId);
    const bool contractComplete = descriptor.operations.size() == cryptoOperations().size();
    const int implementedOperationCount = isProduction ? implementedProductionOperationCount() : 0;
    const int roundTripReadyOperationCount =
        isProduction ? productionRoundTripReadyOperationCount() : 0;
    const bool productionShapePassed = isProduction
        && allProductionProviderOperationsBound()
        && implementedOperationCount == cryptoOperations().size();
    QJsonObject evidence;
    evidence[QStringLiteral("providerId")] = descriptor.providerId;
    evidence[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    evidence[QStringLiteral("status")] = descriptor.compatibilityStatus;
    evidence[QStringLiteral("gate")] = descriptor.compatibilityGate;
    evidence[QStringLiteral("contractComplete")] = contractComplete;
    evidence[QStringLiteral("requiredOperations")] = QJsonArray::fromStringList(cryptoOperationNames());
    evidence[QStringLiteral("coveredOperations")] = QJsonArray::fromStringList([&descriptor]() {
        QStringList names;
        for (const E2ECryptoOperation operation : descriptor.operations) {
            names.append(cryptoOperationName(operation));
        }
        return names;
    }());
    evidence[QStringLiteral("knownAnswerVectorSet")] = isDraft
        ? QStringLiteral("draft-development-vectors-v1")
        : QStringLiteral("production-provider-vectors-v1");
    evidence[QStringLiteral("knownAnswerPassed")] =
        isDraft ? !e2eProductionCryptoRequired() : productionShapePassed;
    evidence[QStringLiteral("roundTripPassed")] =
        isDraft ? !e2eProductionCryptoRequired() : descriptor.productionReady;
    if (isProduction) {
        evidence[QStringLiteral("operationManifest")] = productionOperationManifest();
        evidence[QStringLiteral("operationHarness")] =
            productionOperationHarnessStatusForDescriptor(descriptor);
        evidence[QStringLiteral("operationExecutionPlan")] =
            productionOperationExecutionPlanStatusForDescriptor(descriptor);
        evidence[QStringLiteral("operationInvocation")] =
            productionOperationInvocationStatusForDescriptor(descriptor);
        evidence[QStringLiteral("operationSlots")] =
            productionOperationSlotStatusForDescriptor(descriptor);
        evidence[QStringLiteral("operationDispatchBindings")] =
            productionOperationDispatchBindingStatusForDescriptor(descriptor);
        evidence[QStringLiteral("operationCallableManifest")] =
            productionOperationCallableManifestForDescriptor(descriptor);
        evidence[QStringLiteral("operationExecutionResult")] =
            productionOperationExecutionResultForDescriptor(descriptor);
        evidence[QStringLiteral("providerTable")] =
            productionProviderTableStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerTableBindingProbe")] =
            productionProviderTableBindingProbeStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerTableRegistration")] =
            productionProviderTableRegistrationStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerOperationPreflight")] =
            productionProviderOperationPreflightStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerCallFrame")] =
            productionProviderCallFrameStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerInvocationDryRun")] =
            productionProviderInvocationDryRunStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerInvocationResult")] =
            productionProviderInvocationResultStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerExecutionDecision")] =
            productionProviderExecutionDecisionStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerCallbackHarness")] =
            productionProviderCallbackHarnessStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerVectorSelfTest")] =
            productionProviderVectorSelfTestStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerExecutionSlotBinding")] =
            productionProviderExecutionSlotBindingStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerExecutionPath")] =
            productionProviderExecutionPathStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerInvocationSandbox")] =
            productionProviderInvocationSandboxStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerInvocationVectorResult")] =
            productionProviderInvocationVectorResultStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerInvocationExecution")] =
            productionProviderInvocationExecutionStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerReviewedExecutionCandidate")] =
            productionProviderReviewedExecutionCandidateStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerReviewedCallHandoff")] =
            productionProviderReviewedCallHandoffStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerReviewedOperationStubBoundary")] =
            productionProviderReviewedOperationStubBoundaryStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerReviewedCallableTableBridge")] =
            productionProviderReviewedCallableTableBridgeStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerReviewedOperationCallableInterface")] =
            productionProviderReviewedOperationCallableInterfaceStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerReviewedCallableRuntimePreflight")] =
            productionProviderReviewedCallableRuntimePreflightStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerReviewedInvocationArming")] =
            productionProviderReviewedInvocationArmingStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerReviewedInvocationExecutionAcceptance")] =
            productionProviderReviewedInvocationExecutionAcceptanceStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerDataPlaneBridge")] =
            productionProviderDataPlaneBridgeStatusForDescriptor(descriptor);
        evidence[QStringLiteral("providerPublicPrimitiveExecution")] =
            productionProviderPublicPrimitiveExecutionStatusForDescriptor(descriptor);
        evidence[QStringLiteral("operationManifestComplete")] =
            productionOperationSpecs().size() == cryptoOperations().size();
        evidence[QStringLiteral("reviewedOperationBound")] = productionShapePassed;
        evidence[QStringLiteral("implementedOperationCount")] =
            implementedOperationCount;
        evidence[QStringLiteral("roundTripReadyOperationCount")] =
            roundTripReadyOperationCount;
        evidence[QStringLiteral("blockedOperationCount")] =
            cryptoOperations().size() - implementedOperationCount;
    }
    evidence[QStringLiteral("failClosedPassed")] =
        descriptor.id == QString::fromLatin1(ProductionBackendId)
            ? !descriptor.productionReady
            : !e2eProductionCryptoRequired();
    evidence[QStringLiteral("rawKeyExported")] = descriptor.rawKeyExported;
    evidence[QStringLiteral("privateMaterialExported")] = descriptor.privateMaterialExported;
    evidence[QStringLiteral("operatorAction")] = descriptor.productionReady
        ? QStringLiteral("none")
        : (descriptor.id == QString::fromLatin1(ProductionBackendId)
            ? (descriptor.productionReady
                ? QStringLiteral("none")
                : (productionShapePassed
                    ? QStringLiteral("capture-reviewed-provider-results-and-open-production-acceptance-gates")
                    : QStringLiteral("implement-reviewed-production-provider-and-pass-compatibility-harness")))
            : descriptor.operatorAction);
    return evidence;
}

QJsonArray providerReadinessChecks(const E2ECryptoAdapterDescriptor& descriptor) {
    QJsonArray checks;
    const auto appendCheck = [&checks](const QString& name,
                                      bool passed,
                                      const QString& reason,
                                      const QString& operatorAction) {
        QJsonObject check;
        check[QStringLiteral("name")] = name;
        check[QStringLiteral("passed")] = passed;
        check[QStringLiteral("reason")] = reason;
        check[QStringLiteral("operatorAction")] = operatorAction;
        checks.append(check);
    };

    const bool isDraft = descriptor.id == QString::fromLatin1(DraftBackendId);
    const bool isProduction = descriptor.id == QString::fromLatin1(ProductionBackendId);
    appendCheck(QStringLiteral("provider-linked"),
                descriptor.linked,
                descriptor.linked ? QStringLiteral("linked") : descriptor.readinessGate,
                descriptor.linked ? QStringLiteral("none") : descriptor.operatorAction);
    appendCheck(QStringLiteral("production-reviewed"),
                descriptor.productionReady,
                descriptor.productionReady
                    ? QStringLiteral("reviewed-production-provider")
                    : (isDraft ? QStringLiteral("draft-provider-not-production") : descriptor.readinessGate),
                descriptor.productionReady
                    ? QStringLiteral("none")
                    : QStringLiteral("complete-reviewed-production-crypto-provider"));
    appendCheck(QStringLiteral("operation-contract-complete"),
                descriptor.operations.size() == cryptoOperations().size(),
                descriptor.operations.size() == cryptoOperations().size()
                    ? QStringLiteral("all-required-operations-registered")
                    : QStringLiteral("required-operation-missing"),
                descriptor.operations.size() == cryptoOperations().size()
                    ? QStringLiteral("none")
                    : QStringLiteral("register-all-required-provider-operations"));
    appendCheck(QStringLiteral("compatibility-harness"),
                descriptor.productionReady,
                descriptor.productionReady
                    ? QStringLiteral("production-compatibility-passed")
                    : descriptor.compatibilityGate,
                descriptor.productionReady
                    ? QStringLiteral("none")
                    : QStringLiteral("pass-production-crypto-compatibility-harness"));
    appendCheck(QStringLiteral("production-operation-implementations"),
                !isProduction || implementedProductionOperationCount() == cryptoOperations().size(),
                isProduction
                    ? (implementedProductionOperationCount() == cryptoOperations().size()
                        ? QStringLiteral("all-production-operations-reviewed")
                        : QStringLiteral("production-operation-placeholders-present"))
                    : QStringLiteral("not-production-provider"),
                isProduction && implementedProductionOperationCount() != cryptoOperations().size()
                    ? QStringLiteral("implement-all-production-operation-slots")
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("self-test"),
                isDraft && !e2eProductionCryptoRequired(),
                descriptor.selfTestStatus,
                isProduction
                    ? QStringLiteral("run-production-provider-self-tests")
                    : QStringLiteral("none"));
    const QJsonObject providerTable = productionProviderTableStatusForDescriptor(descriptor);
    const QJsonObject providerTableRegistration =
        productionProviderTableRegistrationStatusForDescriptor(descriptor);
    const QJsonObject providerOperationPreflight =
        productionProviderOperationPreflightStatusForDescriptor(descriptor);
    const QJsonObject providerCallFrame =
        productionProviderCallFrameStatusForDescriptor(descriptor);
    const QJsonObject providerInvocationDryRun =
        productionProviderInvocationDryRunStatusForDescriptor(descriptor);
    const QJsonObject providerInvocationResult =
        productionProviderInvocationResultStatusForDescriptor(descriptor);
    const QJsonObject providerExecutionDecision =
        productionProviderExecutionDecisionStatusForDescriptor(descriptor);
    const QJsonObject providerCallbackHarness =
        productionProviderCallbackHarnessStatusForDescriptor(descriptor);
    const QJsonObject providerVectorSelfTest =
        productionProviderVectorSelfTestStatusForDescriptor(descriptor);
    const QJsonObject providerExecutionSlotBinding =
        productionProviderExecutionSlotBindingStatusForDescriptor(descriptor);
    const QJsonObject providerExecutionPath =
        productionProviderExecutionPathStatusForDescriptor(descriptor);
    const QJsonObject providerInvocationSandbox =
        productionProviderInvocationSandboxStatusForDescriptor(descriptor);
    const QJsonObject providerInvocationVectorResult =
        productionProviderInvocationVectorResultStatusForDescriptor(descriptor);
    const QJsonObject providerInvocationExecution =
        productionProviderInvocationExecutionStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedExecutionCandidate =
        productionProviderReviewedExecutionCandidateStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedCallHandoff =
        productionProviderReviewedCallHandoffStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedOperationStubBoundary =
        productionProviderReviewedOperationStubBoundaryStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedCallableTableBridge =
        productionProviderReviewedCallableTableBridgeStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedOperationCallableInterface =
        productionProviderReviewedOperationCallableInterfaceStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedCallableRuntimePreflight =
        productionProviderReviewedCallableRuntimePreflightStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedInvocationArming =
        productionProviderReviewedInvocationArmingStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedInvocationExecutionAcceptance =
        productionProviderReviewedInvocationExecutionAcceptanceStatusForDescriptor(descriptor);
    const QJsonObject providerDataPlaneBridge =
        productionProviderDataPlaneBridgeStatusForDescriptor(descriptor);
    const QJsonObject providerPublicPrimitiveExecution =
        productionProviderPublicPrimitiveExecutionStatusForDescriptor(descriptor);
    appendCheck(QStringLiteral("provider-table-bound"),
                !isProduction || providerTable.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerTable.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerTable.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-table-registered"),
                !isProduction
                    || providerTableRegistration.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerTableRegistration.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerTableRegistration.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-operation-preflight"),
                !isProduction
                    || providerOperationPreflight.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerOperationPreflight.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerOperationPreflight.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-call-frame"),
                !isProduction
                    || providerCallFrame.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerCallFrame.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerCallFrame.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-invocation-dry-run"),
                !isProduction
                    || providerInvocationDryRun.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerInvocationDryRun.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerInvocationDryRun.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-invocation-result-capture"),
                !isProduction
                    || providerInvocationResult.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerInvocationResult.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerInvocationResult.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-execution-decision"),
                !isProduction
                    || providerExecutionDecision.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerExecutionDecision.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerExecutionDecision.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-callback-harness"),
                !isProduction
                    || providerCallbackHarness.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerCallbackHarness.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerCallbackHarness.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-vector-self-test"),
                !isProduction
                    || providerVectorSelfTest.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerVectorSelfTest.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerVectorSelfTest.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-execution-slot-binding"),
                !isProduction
                    || providerExecutionSlotBinding.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerExecutionSlotBinding.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerExecutionSlotBinding.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-execution-path"),
                !isProduction
                    || providerExecutionPath.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerExecutionPath.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerExecutionPath.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-invocation-sandbox"),
                !isProduction
                    || providerInvocationSandbox.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerInvocationSandbox.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerInvocationSandbox.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-invocation-vector-result"),
                !isProduction
                    || providerInvocationVectorResult.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerInvocationVectorResult.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerInvocationVectorResult.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-invocation-execution"),
                !isProduction
                    || providerInvocationExecution.value(QStringLiteral("accepted")).toBool(false),
                isProduction
                    ? providerInvocationExecution.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerInvocationExecution.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-reviewed-execution-candidate"),
                !isProduction
                    || providerReviewedExecutionCandidate.value(QStringLiteral("candidateReadyCount")).toInt()
                        == cryptoOperations().size(),
                isProduction
                    ? providerReviewedExecutionCandidate.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerReviewedExecutionCandidate.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-reviewed-call-handoff"),
                !isProduction
                    || providerReviewedCallHandoff.value(QStringLiteral("readyHandoffCount")).toInt()
                        == cryptoOperations().size(),
                isProduction
                    ? providerReviewedCallHandoff.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerReviewedCallHandoff.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-reviewed-operation-stub-boundary"),
                !isProduction
                    || providerReviewedOperationStubBoundary.value(QStringLiteral("readyStubCount")).toInt()
                        == cryptoOperations().size(),
                isProduction
                    ? providerReviewedOperationStubBoundary.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerReviewedOperationStubBoundary.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-reviewed-callable-table-bridge"),
                !isProduction
                    || providerReviewedCallableTableBridge.value(QStringLiteral("readyBridgeCount")).toInt()
                        == cryptoOperations().size(),
                isProduction
                    ? providerReviewedCallableTableBridge.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerReviewedCallableTableBridge.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-reviewed-operation-callable-interface"),
                !isProduction
                    || providerReviewedOperationCallableInterface.value(QStringLiteral("readyInterfaceCount")).toInt()
                        == cryptoOperations().size(),
                isProduction
                    ? providerReviewedOperationCallableInterface.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerReviewedOperationCallableInterface.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-reviewed-callable-runtime-preflight"),
                !isProduction
                    || providerReviewedCallableRuntimePreflight.value(QStringLiteral("readyPreflightCount")).toInt()
                        == cryptoOperations().size(),
                isProduction
                    ? providerReviewedCallableRuntimePreflight.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerReviewedCallableRuntimePreflight.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-reviewed-invocation-arming"),
                !isProduction
                    || providerReviewedInvocationArming.value(QStringLiteral("readyArmingCount")).toInt()
                        == cryptoOperations().size(),
                isProduction
                    ? providerReviewedInvocationArming.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerReviewedInvocationArming.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-reviewed-invocation-execution-acceptance"),
                !isProduction
                    || providerReviewedInvocationExecutionAcceptance.value(QStringLiteral("readyAcceptanceCount")).toInt()
                        == cryptoOperations().size(),
                isProduction
                    ? providerReviewedInvocationExecutionAcceptance.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerReviewedInvocationExecutionAcceptance.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-data-plane-bridge"),
                !isProduction
                    || providerDataPlaneBridge.value(QStringLiteral("readyBridgeCount")).toInt()
                        == cryptoOperations().size(),
                isProduction
                    ? providerDataPlaneBridge.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerDataPlaneBridge.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    appendCheck(QStringLiteral("provider-public-primitive-execution"),
                !isProduction
                    || providerPublicPrimitiveExecution.value(QStringLiteral("readyExecutionCount")).toInt()
                        == cryptoOperations().size(),
                isProduction
                    ? providerPublicPrimitiveExecution.value(QStringLiteral("blockedReason")).toString()
                    : QStringLiteral("not-production-provider"),
                isProduction
                    ? providerPublicPrimitiveExecution.value(QStringLiteral("operatorAction")).toString()
                    : QStringLiteral("none"));
    return checks;
}

QJsonObject providerReadinessStatus(const E2ECryptoAdapterDescriptor& descriptor) {
    QJsonObject status;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("dispatchState")] = descriptor.dispatchState;
    status[QStringLiteral("selfTestStatus")] = descriptor.selfTestStatus;
    status[QStringLiteral("readinessGate")] = descriptor.readinessGate;
    status[QStringLiteral("compatibilityStatus")] = descriptor.compatibilityStatus;
    status[QStringLiteral("compatibilityGate")] = descriptor.compatibilityGate;
    status[QStringLiteral("compatibilityEvidence")] = providerCompatibilityEvidence(descriptor);
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("checks")] = providerReadinessChecks(descriptor);
    return status;
}

QJsonObject productionAcceptanceStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor,
                                                    const QJsonObject& operations) {
    QJsonArray operationGates;
    int availableOperationCount = 0;
    int blockedOperationCount = 0;
    for (const E2ECryptoOperation operation : cryptoOperations()) {
        const QString operationName = cryptoOperationName(operation);
        const QJsonObject operationStatus = operations.value(operationName).toObject();
        const bool available = operationStatus.value(QStringLiteral("available")).toBool(false);
        QJsonObject gate;
        gate[QStringLiteral("operation")] = operationName;
        gate[QStringLiteral("entrypoint")] = operationStatus.value(QStringLiteral("entrypoint")).toString();
        gate[QStringLiteral("available")] = available;
        gate[QStringLiteral("registered")] = operationStatus.value(QStringLiteral("registered")).toBool(false);
        gate[QStringLiteral("requiresProductionReady")] =
            operationStatus.value(QStringLiteral("requiresProductionReady")).toBool(false);
        gate[QStringLiteral("providerId")] = operationStatus.value(QStringLiteral("providerId")).toString();
        gate[QStringLiteral("dispatchState")] = operationStatus.value(QStringLiteral("dispatchState")).toString();
        gate[QStringLiteral("blockedReason")] = operationStatus.value(QStringLiteral("blockedReason")).toString();
        gate[QStringLiteral("operatorAction")] = operationStatus.value(QStringLiteral("operatorAction")).toString();
        gate[QStringLiteral("rawKeyExported")] = operationStatus.value(QStringLiteral("rawKeyExported")).toBool(false);
        gate[QStringLiteral("privateMaterialExported")] =
            operationStatus.value(QStringLiteral("privateMaterialExported")).toBool(false);
        if (operationStatus.value(QStringLiteral("operationImplementation")).isObject()) {
            const QJsonObject implementation =
                operationStatus.value(QStringLiteral("operationImplementation")).toObject();
            gate[QStringLiteral("implementationState")] =
                implementation.value(QStringLiteral("implementationState")).toString();
            gate[QStringLiteral("vectorSet")] =
                implementation.value(QStringLiteral("vectorSet")).toString();
            gate[QStringLiteral("compatibilityStatus")] =
                implementation.value(QStringLiteral("compatibilityStatus")).toString();
            gate[QStringLiteral("migrationBlocker")] =
                implementation.value(QStringLiteral("migrationBlocker")).toString();
            gate[QStringLiteral("operationImplementation")] = implementation;
        }
        operationGates.append(gate);
        if (available) {
            ++availableOperationCount;
        } else {
            ++blockedOperationCount;
        }
    }

    const QJsonObject readiness = providerReadinessStatus(descriptor);
    const QJsonObject compatibility = providerCompatibilityEvidence(descriptor);
    const QJsonObject dispatchBindings =
        productionOperationDispatchBindingStatusForDescriptor(descriptor);
    const QJsonObject callableManifest =
        productionOperationCallableManifestForDescriptor(descriptor);
    const QJsonObject executionResult =
        productionOperationExecutionResultForDescriptor(descriptor);
    const QJsonObject providerTable =
        productionProviderTableStatusForDescriptor(descriptor);
    const QJsonObject providerTableBindingProbe =
        productionProviderTableBindingProbeStatusForDescriptor(descriptor);
    const QJsonObject providerTableRegistration =
        productionProviderTableRegistrationStatusForDescriptor(descriptor);
    const QJsonObject providerOperationPreflight =
        productionProviderOperationPreflightStatusForDescriptor(descriptor);
    const QJsonObject providerCallFrame =
        productionProviderCallFrameStatusForDescriptor(descriptor);
    const QJsonObject providerInvocationDryRun =
        productionProviderInvocationDryRunStatusForDescriptor(descriptor);
    const QJsonObject providerInvocationResult =
        productionProviderInvocationResultStatusForDescriptor(descriptor);
    const QJsonObject providerExecutionDecision =
        productionProviderExecutionDecisionStatusForDescriptor(descriptor);
    const QJsonObject providerCallbackHarness =
        productionProviderCallbackHarnessStatusForDescriptor(descriptor);
    const QJsonObject providerVectorSelfTest =
        productionProviderVectorSelfTestStatusForDescriptor(descriptor);
    const QJsonObject providerExecutionSlotBinding =
        productionProviderExecutionSlotBindingStatusForDescriptor(descriptor);
    const QJsonObject providerExecutionPath =
        productionProviderExecutionPathStatusForDescriptor(descriptor);
    const QJsonObject providerInvocationSandbox =
        productionProviderInvocationSandboxStatusForDescriptor(descriptor);
    const QJsonObject providerInvocationVectorResult =
        productionProviderInvocationVectorResultStatusForDescriptor(descriptor);
    const QJsonObject providerInvocationExecution =
        productionProviderInvocationExecutionStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedExecutionCandidate =
        productionProviderReviewedExecutionCandidateStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedCallHandoff =
        productionProviderReviewedCallHandoffStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedOperationStubBoundary =
        productionProviderReviewedOperationStubBoundaryStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedCallableTableBridge =
        productionProviderReviewedCallableTableBridgeStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedOperationCallableInterface =
        productionProviderReviewedOperationCallableInterfaceStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedCallableRuntimePreflight =
        productionProviderReviewedCallableRuntimePreflightStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedInvocationArming =
        productionProviderReviewedInvocationArmingStatusForDescriptor(descriptor);
    const QJsonObject providerReviewedInvocationExecutionAcceptance =
        productionProviderReviewedInvocationExecutionAcceptanceStatusForDescriptor(descriptor);
    const QJsonObject providerDataPlaneBridge =
        productionProviderDataPlaneBridgeStatusForDescriptor(descriptor);
    const QJsonObject providerPublicPrimitiveExecution =
        productionProviderPublicPrimitiveExecutionStatusForDescriptor(descriptor);
    const bool operationContractComplete =
        descriptor.operations.size() == cryptoOperations().size();
    const bool linked = descriptor.linked;
    const bool readinessPassed = descriptor.productionReady
        && readiness.value(QStringLiteral("readinessGate")).toString().isEmpty();
    const bool compatibilityPassed =
        compatibility.value(QStringLiteral("knownAnswerPassed")).toBool(false)
        && compatibility.value(QStringLiteral("roundTripPassed")).toBool(false)
        && compatibility.value(QStringLiteral("contractComplete")).toBool(false);
    const bool noMaterialExport =
        !descriptor.rawKeyExported && !descriptor.privateMaterialExported;
    const bool allOperationsAvailable = blockedOperationCount == 0;
    const bool dispatchBindingsAccepted =
        dispatchBindings.value(QStringLiteral("accepted")).toBool(false);
    const bool callableManifestAccepted =
        callableManifest.value(QStringLiteral("accepted")).toBool(false);
    const bool executionResultAccepted =
        executionResult.value(QStringLiteral("accepted")).toBool(false);
    const bool providerTableAccepted =
        providerTable.value(QStringLiteral("accepted")).toBool(false);
    const bool providerTableBindingProbeAccepted =
        providerTableBindingProbe.value(QStringLiteral("accepted")).toBool(false);
    const bool providerTableRegistrationAccepted =
        providerTableRegistration.value(QStringLiteral("accepted")).toBool(false);
    const bool providerOperationPreflightAccepted =
        providerOperationPreflight.value(QStringLiteral("accepted")).toBool(false);
    const bool providerCallFrameAccepted =
        providerCallFrame.value(QStringLiteral("accepted")).toBool(false);
    const bool providerInvocationDryRunAccepted =
        providerInvocationDryRun.value(QStringLiteral("accepted")).toBool(false);
    const bool providerInvocationResultAccepted =
        providerInvocationResult.value(QStringLiteral("accepted")).toBool(false);
    const bool providerExecutionDecisionAccepted =
        providerExecutionDecision.value(QStringLiteral("accepted")).toBool(false);
    const bool providerCallbackHarnessAccepted =
        providerCallbackHarness.value(QStringLiteral("accepted")).toBool(false);
    const bool providerVectorSelfTestAccepted =
        providerVectorSelfTest.value(QStringLiteral("accepted")).toBool(false);
    const bool providerExecutionSlotBindingAccepted =
        providerExecutionSlotBinding.value(QStringLiteral("accepted")).toBool(false);
    const bool providerExecutionPathAccepted =
        providerExecutionPath.value(QStringLiteral("accepted")).toBool(false);
    const bool providerInvocationSandboxAccepted =
        providerInvocationSandbox.value(QStringLiteral("accepted")).toBool(false);
    const bool providerInvocationVectorResultAccepted =
        providerInvocationVectorResult.value(QStringLiteral("accepted")).toBool(false);
    const bool providerInvocationExecutionAccepted =
        providerInvocationExecution.value(QStringLiteral("accepted")).toBool(false);
    const bool providerReviewedCallHandoffReady =
        providerReviewedCallHandoff.value(QStringLiteral("readyHandoffCount")).toInt()
            == cryptoOperations().size()
        && providerReviewedCallHandoff.value(QStringLiteral("handoffCount")).toInt()
            == cryptoOperations().size();
    const bool providerReviewedOperationStubBoundaryReady =
        providerReviewedOperationStubBoundary.value(QStringLiteral("readyStubCount")).toInt()
            == cryptoOperations().size()
        && providerReviewedOperationStubBoundary.value(QStringLiteral("stubCount")).toInt()
            == cryptoOperations().size();
    const bool providerReviewedCallableTableBridgeReady =
        providerReviewedCallableTableBridge.value(QStringLiteral("readyBridgeCount")).toInt()
            == cryptoOperations().size()
        && providerReviewedCallableTableBridge.value(QStringLiteral("bridgeCount")).toInt()
            == cryptoOperations().size();
    const bool providerReviewedOperationCallableInterfaceReady =
        providerReviewedOperationCallableInterface.value(QStringLiteral("readyInterfaceCount")).toInt()
            == cryptoOperations().size()
        && providerReviewedOperationCallableInterface.value(QStringLiteral("interfaceCount")).toInt()
            == cryptoOperations().size();
    const bool providerReviewedCallableRuntimePreflightReady =
        providerReviewedCallableRuntimePreflight.value(QStringLiteral("readyPreflightCount")).toInt()
            == cryptoOperations().size()
        && providerReviewedCallableRuntimePreflight.value(QStringLiteral("preflightCount")).toInt()
            == cryptoOperations().size();
    const bool providerReviewedInvocationArmingReady =
        providerReviewedInvocationArming.value(QStringLiteral("readyArmingCount")).toInt()
            == cryptoOperations().size()
        && providerReviewedInvocationArming.value(QStringLiteral("armingCount")).toInt()
            == cryptoOperations().size();
    const bool providerReviewedInvocationExecutionAcceptanceReady =
        providerReviewedInvocationExecutionAcceptance.value(QStringLiteral("readyAcceptanceCount")).toInt()
            == cryptoOperations().size()
        && providerReviewedInvocationExecutionAcceptance.value(QStringLiteral("acceptanceCount")).toInt()
            == cryptoOperations().size();
    const bool providerDataPlaneBridgeReady =
        providerDataPlaneBridge.value(QStringLiteral("readyBridgeCount")).toInt()
            == cryptoOperations().size()
        && providerDataPlaneBridge.value(QStringLiteral("bridgeCount")).toInt()
            == cryptoOperations().size();
    const bool providerPublicPrimitiveExecutionReady =
        providerPublicPrimitiveExecution.value(QStringLiteral("readyExecutionCount")).toInt()
            == cryptoOperations().size()
        && providerPublicPrimitiveExecution.value(QStringLiteral("executionCount")).toInt()
            == cryptoOperations().size();
    const bool accepted = linked
        && descriptor.productionReady
        && operationContractComplete
        && compatibilityPassed
        && noMaterialExport
        && allOperationsAvailable
        && dispatchBindingsAccepted
        && callableManifestAccepted
        && executionResultAccepted
        && providerTableAccepted
        && providerTableBindingProbeAccepted
        && providerTableRegistrationAccepted
        && providerOperationPreflightAccepted
        && providerCallFrameAccepted
        && providerInvocationDryRunAccepted
        && providerInvocationResultAccepted
        && providerExecutionDecisionAccepted
        && providerCallbackHarnessAccepted
        && providerVectorSelfTestAccepted
        && providerExecutionSlotBindingAccepted
        && providerExecutionPathAccepted
        && providerInvocationSandboxAccepted
        && providerInvocationVectorResultAccepted
        && providerInvocationExecutionAccepted
        && providerReviewedCallHandoffReady
        && providerReviewedOperationStubBoundaryReady
        && providerReviewedCallableTableBridgeReady
        && providerReviewedOperationCallableInterfaceReady
        && providerReviewedCallableRuntimePreflightReady
        && providerReviewedInvocationArmingReady
        && providerReviewedInvocationExecutionAcceptanceReady
        && providerDataPlaneBridgeReady
        && providerPublicPrimitiveExecutionReady;

    QString releaseGate;
    QString blockedReason;
    QString operatorAction;
    if (accepted) {
        releaseGate = QStringLiteral("production-crypto-accepted");
        operatorAction = QStringLiteral("none");
    } else if (!linked) {
        releaseGate = QStringLiteral("production-adapter-not-linked");
        blockedReason = QStringLiteral("production-crypto-backend-unavailable");
        operatorAction = QStringLiteral("link-reviewed-production-crypto-backend");
    } else if (providerInvocationDryRunAccepted && !providerInvocationResultAccepted) {
        releaseGate = QStringLiteral("production-provider-invocation-results-blocked");
        blockedReason = providerInvocationResult.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerInvocationResult.value(QStringLiteral("operatorAction")).toString();
    } else if (!descriptor.productionReady) {
        releaseGate = QStringLiteral("production-operations-not-ready");
        blockedReason = QStringLiteral("production-adapter-not-ready");
        operatorAction = QStringLiteral("complete-reviewed-production-operations-and-compatibility-tests");
    } else if (!compatibilityPassed) {
        releaseGate = QStringLiteral("production-compatibility-not-passed");
        blockedReason = descriptor.compatibilityGate;
        operatorAction = QStringLiteral("pass-production-crypto-compatibility-harness");
    } else if (!allOperationsAvailable) {
        releaseGate = QStringLiteral("production-operation-dispatch-blocked");
        blockedReason = QStringLiteral("production-operation-unavailable");
        operatorAction = QStringLiteral("fix-production-operation-dispatch");
    } else if (!dispatchBindingsAccepted) {
        releaseGate = QStringLiteral("production-operation-dispatch-bindings-blocked");
        blockedReason = dispatchBindings.value(QStringLiteral("blockedReason")).toString();
        operatorAction = dispatchBindings.value(QStringLiteral("operatorAction")).toString();
    } else if (!callableManifestAccepted) {
        releaseGate = QStringLiteral("production-operation-callable-manifest-blocked");
        blockedReason = callableManifest.value(QStringLiteral("blockedReason")).toString();
        operatorAction = callableManifest.value(QStringLiteral("operatorAction")).toString();
    } else if (!executionResultAccepted) {
        releaseGate = QStringLiteral("production-operation-execution-results-blocked");
        blockedReason = executionResult.value(QStringLiteral("blockedReason")).toString();
        operatorAction = executionResult.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerTableAccepted) {
        releaseGate = QStringLiteral("production-provider-table-blocked");
        blockedReason = providerTable.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerTable.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerTableBindingProbeAccepted) {
        releaseGate = QStringLiteral("production-provider-table-binding-probe-blocked");
        blockedReason = providerTableBindingProbe.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerTableBindingProbe.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerTableRegistrationAccepted) {
        releaseGate = QStringLiteral("production-provider-table-registration-blocked");
        blockedReason = providerTableRegistration.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerTableRegistration.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerOperationPreflightAccepted) {
        releaseGate = QStringLiteral("production-provider-operation-preflight-blocked");
        blockedReason = providerOperationPreflight.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerOperationPreflight.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerCallFrameAccepted) {
        releaseGate = QStringLiteral("production-provider-call-frame-blocked");
        blockedReason = providerCallFrame.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerCallFrame.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerInvocationDryRunAccepted) {
        releaseGate = QStringLiteral("production-provider-invocation-dry-run-blocked");
        blockedReason = providerInvocationDryRun.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerInvocationDryRun.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerInvocationResultAccepted) {
        releaseGate = QStringLiteral("production-provider-invocation-results-blocked");
        blockedReason = providerInvocationResult.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerInvocationResult.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerExecutionDecisionAccepted) {
        releaseGate = QStringLiteral("production-provider-execution-decision-blocked");
        blockedReason = providerExecutionDecision.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerExecutionDecision.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerCallbackHarnessAccepted) {
        releaseGate = QStringLiteral("production-provider-callback-harness-blocked");
        blockedReason = providerCallbackHarness.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerCallbackHarness.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerVectorSelfTestAccepted) {
        releaseGate = QStringLiteral("production-provider-vector-self-test-blocked");
        blockedReason = providerVectorSelfTest.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerVectorSelfTest.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerExecutionSlotBindingAccepted) {
        releaseGate = QStringLiteral("production-provider-execution-slot-binding-blocked");
        blockedReason = providerExecutionSlotBinding.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerExecutionSlotBinding.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerExecutionPathAccepted) {
        releaseGate = QStringLiteral("production-provider-execution-path-blocked");
        blockedReason = providerExecutionPath.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerExecutionPath.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerInvocationSandboxAccepted) {
        releaseGate = QStringLiteral("production-provider-invocation-sandbox-blocked");
        blockedReason = providerInvocationSandbox.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerInvocationSandbox.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerInvocationVectorResultAccepted) {
        releaseGate = QStringLiteral("production-provider-invocation-vector-result-blocked");
        blockedReason = providerInvocationVectorResult.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerInvocationVectorResult.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerInvocationExecutionAccepted) {
        releaseGate = QStringLiteral("production-provider-invocation-execution-blocked");
        blockedReason = providerInvocationExecution.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerInvocationExecution.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerReviewedCallHandoffReady) {
        releaseGate = QStringLiteral("production-provider-reviewed-call-handoff-blocked");
        blockedReason = providerReviewedCallHandoff.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerReviewedCallHandoff.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerReviewedOperationStubBoundaryReady) {
        releaseGate = QStringLiteral("production-provider-reviewed-operation-stub-blocked");
        blockedReason = providerReviewedOperationStubBoundary.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerReviewedOperationStubBoundary.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerReviewedCallableTableBridgeReady) {
        releaseGate = QStringLiteral("production-provider-reviewed-callable-table-bridge-blocked");
        blockedReason = providerReviewedCallableTableBridge.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerReviewedCallableTableBridge.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerReviewedOperationCallableInterfaceReady) {
        releaseGate = QStringLiteral("production-provider-reviewed-operation-callable-interface-blocked");
        blockedReason = providerReviewedOperationCallableInterface.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerReviewedOperationCallableInterface.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerReviewedCallableRuntimePreflightReady) {
        releaseGate = QStringLiteral("production-provider-reviewed-callable-runtime-preflight-blocked");
        blockedReason = providerReviewedCallableRuntimePreflight.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerReviewedCallableRuntimePreflight.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerReviewedInvocationArmingReady) {
        releaseGate = QStringLiteral("production-provider-reviewed-invocation-arming-blocked");
        blockedReason = providerReviewedInvocationArming.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerReviewedInvocationArming.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerReviewedInvocationExecutionAcceptanceReady) {
        releaseGate = QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-blocked");
        blockedReason = providerReviewedInvocationExecutionAcceptance.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerReviewedInvocationExecutionAcceptance.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerDataPlaneBridgeReady) {
        releaseGate = QStringLiteral("production-provider-data-plane-bridge-blocked");
        blockedReason = providerDataPlaneBridge.value(QStringLiteral("blockedReason")).toString();
        operatorAction = providerDataPlaneBridge.value(QStringLiteral("operatorAction")).toString();
    } else if (!providerPublicPrimitiveExecutionReady) {
        releaseGate = QStringLiteral("production-provider-public-primitive-execution-blocked");
        blockedReason =
            providerPublicPrimitiveExecution.value(QStringLiteral("blockedReason")).toString();
        operatorAction =
            providerPublicPrimitiveExecution.value(QStringLiteral("operatorAction")).toString();
    } else if (!noMaterialExport) {
        releaseGate = QStringLiteral("production-material-export-blocked");
        blockedReason = QStringLiteral("provider-exports-sensitive-material");
        operatorAction = QStringLiteral("remove-production-provider-sensitive-material-export");
    } else {
        releaseGate = QStringLiteral("production-acceptance-incomplete");
        blockedReason = QStringLiteral("production-acceptance-incomplete");
        operatorAction = QStringLiteral("complete-production-crypto-acceptance-gates");
    }

    QJsonObject status;
    status[QStringLiteral("schema")] = QStringLiteral("qtnetworkchat-e2e-production-crypto-acceptance-v1");
    setProviderReportIdentity(status, descriptor);
    status[QStringLiteral("dispatchState")] = descriptor.dispatchState;
    status[QStringLiteral("linked")] = linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("releaseGate")] = releaseGate;
    status[QStringLiteral("blockedReason")] = blockedReason;
    status[QStringLiteral("operatorAction")] = operatorAction;
    status[QStringLiteral("requiredOperationCount")] = cryptoOperations().size();
    status[QStringLiteral("registeredOperationCount")] = descriptor.operations.size();
    status[QStringLiteral("availableOperationCount")] = availableOperationCount;
    status[QStringLiteral("blockedOperationCount")] = blockedOperationCount;
    status[QStringLiteral("operationContractComplete")] = operationContractComplete;
    status[QStringLiteral("operationManifest")] = productionOperationManifest();
    status[QStringLiteral("operationHarness")] =
        productionOperationHarnessStatusForDescriptor(descriptor);
    status[QStringLiteral("operationExecutionPlan")] =
        productionOperationExecutionPlanStatusForDescriptor(descriptor);
    status[QStringLiteral("operationInvocation")] =
        productionOperationInvocationStatusForDescriptor(descriptor);
    status[QStringLiteral("operationSlots")] =
        productionOperationSlotStatusForDescriptor(descriptor);
    status[QStringLiteral("operationDispatchBindings")] = dispatchBindings;
    status[QStringLiteral("operationCallableManifest")] = callableManifest;
    status[QStringLiteral("operationExecutionResult")] = executionResult;
    status[QStringLiteral("providerTable")] = providerTable;
    status[QStringLiteral("providerTableBindingProbe")] = providerTableBindingProbe;
    status[QStringLiteral("providerTableRegistration")] = providerTableRegistration;
    status[QStringLiteral("providerOperationPreflight")] = providerOperationPreflight;
    status[QStringLiteral("providerCallFrame")] = providerCallFrame;
    status[QStringLiteral("providerInvocationDryRun")] = providerInvocationDryRun;
    status[QStringLiteral("providerInvocationResult")] = providerInvocationResult;
    status[QStringLiteral("providerExecutionDecision")] = providerExecutionDecision;
    status[QStringLiteral("providerCallbackHarness")] = providerCallbackHarness;
    status[QStringLiteral("providerVectorSelfTest")] = providerVectorSelfTest;
    status[QStringLiteral("providerExecutionSlotBinding")] = providerExecutionSlotBinding;
    status[QStringLiteral("providerExecutionPath")] = providerExecutionPath;
    status[QStringLiteral("providerInvocationSandbox")] = providerInvocationSandbox;
    status[QStringLiteral("providerInvocationVectorResult")] = providerInvocationVectorResult;
    status[QStringLiteral("providerInvocationExecution")] = providerInvocationExecution;
    status[QStringLiteral("providerReviewedExecutionCandidate")] =
        providerReviewedExecutionCandidate;
    status[QStringLiteral("providerReviewedCallHandoff")] = providerReviewedCallHandoff;
    status[QStringLiteral("providerReviewedOperationStubBoundary")] =
        providerReviewedOperationStubBoundary;
    status[QStringLiteral("providerReviewedCallableTableBridge")] =
        providerReviewedCallableTableBridge;
    status[QStringLiteral("providerReviewedOperationCallableInterface")] =
        providerReviewedOperationCallableInterface;
    status[QStringLiteral("providerReviewedCallableRuntimePreflight")] =
        providerReviewedCallableRuntimePreflight;
    status[QStringLiteral("providerReviewedInvocationArming")] =
        providerReviewedInvocationArming;
    status[QStringLiteral("providerReviewedInvocationExecutionAcceptance")] =
        providerReviewedInvocationExecutionAcceptance;
    status[QStringLiteral("providerDataPlaneBridge")] = providerDataPlaneBridge;
    status[QStringLiteral("providerPublicPrimitiveExecution")] =
        providerPublicPrimitiveExecution;
    status[QStringLiteral("operationManifestComplete")] =
        productionOperationSpecs().size() == cryptoOperations().size();
    status[QStringLiteral("implementedOperationCount")] =
        implementedProductionOperationCount();
    status[QStringLiteral("passedExecutionResultCount")] =
        executionResult.value(QStringLiteral("passedResultCount")).toInt();
    status[QStringLiteral("providerTableAccepted")] = providerTableAccepted;
    status[QStringLiteral("providerTableReleaseGate")] =
        providerTable.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerTableBoundSymbolCount")] =
        providerTable.value(QStringLiteral("boundSymbolCount")).toInt();
    status[QStringLiteral("providerTableMissingSymbolCount")] =
        providerTable.value(QStringLiteral("missingSymbolCount")).toInt();
    status[QStringLiteral("providerTableBindingProbeAccepted")] =
        providerTableBindingProbeAccepted;
    status[QStringLiteral("providerTableBindingProbeReleaseGate")] =
        providerTableBindingProbe.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerTableRegistrationAccepted")] =
        providerTableRegistrationAccepted;
    status[QStringLiteral("providerTableRegistrationReleaseGate")] =
        providerTableRegistration.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerTableRegistered")] =
        providerTableRegistration.value(QStringLiteral("registered")).toBool(false);
    status[QStringLiteral("providerOperationPreflightAccepted")] =
        providerOperationPreflightAccepted;
    status[QStringLiteral("providerOperationPreflightReleaseGate")] =
        providerOperationPreflight.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerOperationPreflightBlockedOperationCount")] =
        providerOperationPreflight.value(QStringLiteral("blockedOperationCount")).toInt();
    status[QStringLiteral("providerCallFrameAccepted")] =
        providerCallFrameAccepted;
    status[QStringLiteral("providerCallFrameReleaseGate")] =
        providerCallFrame.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerCallFrameBlockedFrameCount")] =
        providerCallFrame.value(QStringLiteral("blockedFrameCount")).toInt();
    status[QStringLiteral("providerInvocationDryRunAccepted")] =
        providerInvocationDryRunAccepted;
    status[QStringLiteral("providerInvocationDryRunReleaseGate")] =
        providerInvocationDryRun.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerInvocationDryRunBlockedInvocationCount")] =
        providerInvocationDryRun.value(QStringLiteral("blockedInvocationCount")).toInt();
    status[QStringLiteral("providerInvocationResultAccepted")] =
        providerInvocationResultAccepted;
    status[QStringLiteral("providerInvocationResultReleaseGate")] =
        providerInvocationResult.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerInvocationResultBlockedResultCount")] =
        providerInvocationResult.value(QStringLiteral("blockedResultCount")).toInt();
    status[QStringLiteral("providerExecutionDecisionAccepted")] =
        providerExecutionDecisionAccepted;
    status[QStringLiteral("providerExecutionDecisionReleaseGate")] =
        providerExecutionDecision.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerExecutionDecisionBlockedDecisionCount")] =
        providerExecutionDecision.value(QStringLiteral("blockedDecisionCount")).toInt();
    status[QStringLiteral("providerCallbackHarnessAccepted")] =
        providerCallbackHarnessAccepted;
    status[QStringLiteral("providerCallbackHarnessReleaseGate")] =
        providerCallbackHarness.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerCallbackHarnessBlockedCallbackCount")] =
        providerCallbackHarness.value(QStringLiteral("blockedCallbackCount")).toInt();
    status[QStringLiteral("providerVectorSelfTestAccepted")] =
        providerVectorSelfTestAccepted;
    status[QStringLiteral("providerVectorSelfTestReleaseGate")] =
        providerVectorSelfTest.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerVectorSelfTestBlockedVectorCount")] =
        providerVectorSelfTest.value(QStringLiteral("blockedVectorCount")).toInt();
    status[QStringLiteral("providerExecutionSlotBindingAccepted")] =
        providerExecutionSlotBindingAccepted;
    status[QStringLiteral("providerExecutionSlotBindingReleaseGate")] =
        providerExecutionSlotBinding.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerExecutionSlotBindingBlockedSlotCount")] =
        providerExecutionSlotBinding.value(QStringLiteral("blockedSlotCount")).toInt();
    status[QStringLiteral("providerExecutionPathAccepted")] =
        providerExecutionPathAccepted;
    status[QStringLiteral("providerExecutionPathReleaseGate")] =
        providerExecutionPath.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerExecutionPathBlockedPathCount")] =
        providerExecutionPath.value(QStringLiteral("blockedPathCount")).toInt();
    status[QStringLiteral("providerInvocationSandboxAccepted")] =
        providerInvocationSandboxAccepted;
    status[QStringLiteral("providerInvocationSandboxReleaseGate")] =
        providerInvocationSandbox.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerInvocationSandboxBlockedSandboxCount")] =
        providerInvocationSandbox.value(QStringLiteral("blockedSandboxCount")).toInt();
    status[QStringLiteral("providerInvocationVectorResultAccepted")] =
        providerInvocationVectorResultAccepted;
    status[QStringLiteral("providerInvocationVectorResultReleaseGate")] =
        providerInvocationVectorResult.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerInvocationVectorResultBlockedVectorResultCount")] =
        providerInvocationVectorResult.value(QStringLiteral("blockedVectorResultCount")).toInt();
    status[QStringLiteral("providerInvocationExecutionAccepted")] =
        providerInvocationExecutionAccepted;
    status[QStringLiteral("providerInvocationExecutionReleaseGate")] =
        providerInvocationExecution.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerInvocationExecutionBlockedExecutionCount")] =
        providerInvocationExecution.value(QStringLiteral("blockedExecutionCount")).toInt();
    status[QStringLiteral("providerReviewedExecutionCandidateReadyCount")] =
        providerReviewedExecutionCandidate.value(QStringLiteral("candidateReadyCount")).toInt();
    status[QStringLiteral("providerReviewedExecutionCandidateBlockedCount")] =
        providerReviewedExecutionCandidate.value(QStringLiteral("blockedCandidateCount")).toInt();
    status[QStringLiteral("providerReviewedExecutionCandidateReleaseGate")] =
        providerReviewedExecutionCandidate.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedCallHandoffReadyCount")] =
        providerReviewedCallHandoff.value(QStringLiteral("readyHandoffCount")).toInt();
    status[QStringLiteral("providerReviewedCallHandoffBlockedCount")] =
        providerReviewedCallHandoff.value(QStringLiteral("blockedHandoffCount")).toInt();
    status[QStringLiteral("providerReviewedCallHandoffReleaseGate")] =
        providerReviewedCallHandoff.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedOperationStubBoundaryReadyCount")] =
        providerReviewedOperationStubBoundary.value(QStringLiteral("readyStubCount")).toInt();
    status[QStringLiteral("providerReviewedOperationStubBoundaryBlockedCount")] =
        providerReviewedOperationStubBoundary.value(QStringLiteral("blockedStubCount")).toInt();
    status[QStringLiteral("providerReviewedOperationStubBoundaryReleaseGate")] =
        providerReviewedOperationStubBoundary.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedCallableTableBridgeReadyCount")] =
        providerReviewedCallableTableBridge.value(QStringLiteral("readyBridgeCount")).toInt();
    status[QStringLiteral("providerReviewedCallableTableBridgeBlockedCount")] =
        providerReviewedCallableTableBridge.value(QStringLiteral("blockedBridgeCount")).toInt();
    status[QStringLiteral("providerReviewedCallableTableBridgeReleaseGate")] =
        providerReviewedCallableTableBridge.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedOperationCallableInterfaceReadyCount")] =
        providerReviewedOperationCallableInterface.value(QStringLiteral("readyInterfaceCount")).toInt();
    status[QStringLiteral("providerReviewedOperationCallableInterfaceBlockedCount")] =
        providerReviewedOperationCallableInterface.value(QStringLiteral("blockedInterfaceCount")).toInt();
    status[QStringLiteral("providerReviewedOperationCallableInterfaceReleaseGate")] =
        providerReviewedOperationCallableInterface.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedCallableRuntimePreflightReadyCount")] =
        providerReviewedCallableRuntimePreflight.value(QStringLiteral("readyPreflightCount")).toInt();
    status[QStringLiteral("providerReviewedCallableRuntimePreflightBlockedCount")] =
        providerReviewedCallableRuntimePreflight.value(QStringLiteral("blockedPreflightCount")).toInt();
    status[QStringLiteral("providerReviewedCallableRuntimePreflightReleaseGate")] =
        providerReviewedCallableRuntimePreflight.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedInvocationArmingReadyCount")] =
        providerReviewedInvocationArming.value(QStringLiteral("readyArmingCount")).toInt();
    status[QStringLiteral("providerReviewedInvocationArmingBlockedCount")] =
        providerReviewedInvocationArming.value(QStringLiteral("blockedArmingCount")).toInt();
    status[QStringLiteral("providerReviewedInvocationArmingReleaseGate")] =
        providerReviewedInvocationArming.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedInvocationExecutionAcceptanceReadyCount")] =
        providerReviewedInvocationExecutionAcceptance.value(QStringLiteral("readyAcceptanceCount")).toInt();
    status[QStringLiteral("providerReviewedInvocationExecutionAcceptanceBlockedCount")] =
        providerReviewedInvocationExecutionAcceptance.value(QStringLiteral("blockedAcceptanceCount")).toInt();
    status[QStringLiteral("providerReviewedInvocationExecutionAcceptanceReleaseGate")] =
        providerReviewedInvocationExecutionAcceptance.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerDataPlaneBridgeReadyCount")] =
        providerDataPlaneBridge.value(QStringLiteral("readyBridgeCount")).toInt();
    status[QStringLiteral("providerDataPlaneBridgeBlockedCount")] =
        providerDataPlaneBridge.value(QStringLiteral("blockedBridgeCount")).toInt();
    status[QStringLiteral("providerDataPlaneBridgeReleaseGate")] =
        providerDataPlaneBridge.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerPublicPrimitiveExecutionReadyCount")] =
        providerPublicPrimitiveExecution.value(QStringLiteral("readyExecutionCount")).toInt();
    status[QStringLiteral("providerPublicPrimitiveExecutionBlockedCount")] =
        providerPublicPrimitiveExecution.value(QStringLiteral("blockedExecutionCount")).toInt();
    status[QStringLiteral("providerPublicPrimitiveExecutionReleaseGate")] =
        providerPublicPrimitiveExecution.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerPublicPrimitiveExecutionReady")] =
        providerPublicPrimitiveExecutionReady;
    status[QStringLiteral("readinessGate")] = descriptor.readinessGate;
    status[QStringLiteral("readinessPassed")] = readinessPassed;
    status[QStringLiteral("compatibilityGate")] =
        compatibility.value(QStringLiteral("gate")).toString();
    status[QStringLiteral("compatibilityStatus")] =
        compatibility.value(QStringLiteral("status")).toString();
    status[QStringLiteral("compatibilityPassed")] = compatibilityPassed;
    status[QStringLiteral("knownAnswerPassed")] =
        compatibility.value(QStringLiteral("knownAnswerPassed")).toBool(false);
    status[QStringLiteral("roundTripPassed")] =
        compatibility.value(QStringLiteral("roundTripPassed")).toBool(false);
    status[QStringLiteral("failClosedPassed")] =
        compatibility.value(QStringLiteral("failClosedPassed")).toBool(false);
    status[QStringLiteral("rawKeyExported")] = descriptor.rawKeyExported;
    status[QStringLiteral("privateMaterialExported")] = descriptor.privateMaterialExported;
    status[QStringLiteral("operationGates")] = operationGates;
    status[QStringLiteral("readiness")] = readiness;
    status[QStringLiteral("compatibility")] = compatibility;
    return status;
}

QJsonObject productionRolloutObservabilityStatusForAcceptance(const QJsonObject& acceptance) {
    const int requiredOperationCount =
        acceptance.value(QStringLiteral("requiredOperationCount")).toInt(cryptoOperations().size());
    const bool acceptanceAccepted =
        acceptance.value(QStringLiteral("accepted")).toBool(false);
    const bool linked = acceptance.value(QStringLiteral("linked")).toBool(false);
    const bool productionReady =
        acceptance.value(QStringLiteral("productionReady")).toBool(false);
    const bool noMaterialExport =
        !acceptance.value(QStringLiteral("rawKeyExported")).toBool(true)
        && !acceptance.value(QStringLiteral("privateMaterialExported")).toBool(true);
    const QJsonObject invocationResult =
        acceptance.value(QStringLiteral("providerInvocationResult")).toObject();
    const QJsonObject invocationExecution =
        acceptance.value(QStringLiteral("providerInvocationExecution")).toObject();
    const QJsonObject publicPrimitiveExecution =
        acceptance.value(QStringLiteral("providerPublicPrimitiveExecution")).toObject();
    const int materialExportProofCount =
        invocationResult.value(QStringLiteral("materialExportProofCount")).toInt();
    const int outputShapeProofCount =
        invocationExecution.value(QStringLiteral("materialExportProofCount")).toInt();
    const int publicPrimitiveReadyCount =
        publicPrimitiveExecution.value(QStringLiteral("readyExecutionCount")).toInt();
    const int publicPrimitiveBlockedCount =
        publicPrimitiveExecution.value(QStringLiteral("blockedExecutionCount")).toInt();
    const bool noSensitiveExportProof =
        acceptanceAccepted
        && noMaterialExport
        && materialExportProofCount == requiredOperationCount
        && outputShapeProofCount == requiredOperationCount;
    const bool releaseRunObservable =
        acceptanceAccepted
        && noSensitiveExportProof
        && publicPrimitiveReadyCount == requiredOperationCount
        && publicPrimitiveBlockedCount == 0;

    QString releaseGate;
    QString blockedReason;
    QString operatorAction;
    QString userRecoveryPrompt;
    if (releaseRunObservable) {
        releaseGate = QStringLiteral("production-rollout-observability-ready");
        operatorAction = QStringLiteral("none");
        userRecoveryPrompt = QStringLiteral("none");
    } else if (!linked) {
        releaseGate = QStringLiteral("production-rollout-observability-blocked-not-linked");
        blockedReason = QStringLiteral("production-crypto-backend-unavailable");
        operatorAction = QStringLiteral("link-reviewed-production-crypto-backend");
        userRecoveryPrompt = QStringLiteral("retry-after-operator-links-production-crypto-backend");
    } else if (!productionReady) {
        releaseGate = QStringLiteral("production-rollout-observability-blocked-not-ready");
        blockedReason = QStringLiteral("production-adapter-not-ready");
        operatorAction = QStringLiteral("complete-reviewed-production-operations-and-compatibility-tests");
        userRecoveryPrompt = QStringLiteral("keep-existing-e2e-state-and-wait-for-production-crypto-readiness");
    } else if (!acceptanceAccepted) {
        releaseGate = QStringLiteral("production-rollout-observability-blocked-acceptance");
        blockedReason = acceptance.value(QStringLiteral("releaseGate")).toString(
            QStringLiteral("production-acceptance-incomplete"));
        operatorAction = acceptance.value(QStringLiteral("operatorAction")).toString(
            QStringLiteral("complete-production-crypto-acceptance-gates"));
        userRecoveryPrompt = QStringLiteral("do-not-clear-local-e2e-state-until-production-acceptance-passes");
    } else if (!noMaterialExport) {
        releaseGate = QStringLiteral("production-rollout-observability-blocked-sensitive-export");
        blockedReason = QStringLiteral("provider-exports-sensitive-material");
        operatorAction = QStringLiteral("remove-production-provider-sensitive-material-export");
        userRecoveryPrompt = QStringLiteral("stop-production-rollout-and-rotate-any-exposed-test-material");
    } else {
        releaseGate = QStringLiteral("production-rollout-observability-blocked-proof");
        blockedReason = QStringLiteral("production-rollout-observability-proof-incomplete");
        operatorAction =
            QStringLiteral("capture-release-run-observability-and-no-sensitive-export-proof");
        userRecoveryPrompt =
            QStringLiteral("keep-existing-e2e-state-and-use-draft-recovery-paths-until-proof-is-complete");
    }

    QJsonArray operatorPrompts;
    operatorPrompts.append(releaseRunObservable
        ? QStringLiteral("verify-production-required-run-uses-linked-reviewed-provider")
        : operatorAction);
    operatorPrompts.append(QStringLiteral("archive-sanitized-release-run-status-without-key-or-payload-bytes"));
    operatorPrompts.append(QStringLiteral("verify-filesystem-object-ciphertext-readback-evidence-before-full-file-resume-release"));

    QJsonArray userPrompts;
    userPrompts.append(userRecoveryPrompt);
    userPrompts.append(QStringLiteral("show-verification-required-when-trust-pins-are-rebound"));
    userPrompts.append(QStringLiteral("show-resume-or-resend-guidance-from-encrypted-file-recovery-status"));

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-rollout-observability-v1");
    status[QStringLiteral("backendId")] = acceptance.value(QStringLiteral("backendId")).toString();
    status[QStringLiteral("providerId")] = acceptance.value(QStringLiteral("providerId")).toString();
    status[QStringLiteral("operationContractVersion")] =
        acceptance.value(QStringLiteral("operationContractVersion")).toString();
    status[QStringLiteral("linked")] = linked;
    status[QStringLiteral("productionReady")] = productionReady;
    status[QStringLiteral("productionAcceptanceAccepted")] = acceptanceAccepted;
    status[QStringLiteral("productionAcceptanceReleaseGate")] =
        acceptance.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("accepted")] = releaseRunObservable;
    status[QStringLiteral("releaseGate")] = releaseGate;
    status[QStringLiteral("blockedReason")] = blockedReason;
    status[QStringLiteral("operatorAction")] = operatorAction;
    status[QStringLiteral("releaseRunObservable")] = releaseRunObservable;
    status[QStringLiteral("requiredOperationCount")] = requiredOperationCount;
    status[QStringLiteral("materialExportProofCount")] = materialExportProofCount;
    status[QStringLiteral("outputShapeProofCount")] = outputShapeProofCount;
    status[QStringLiteral("publicPrimitiveReadyCount")] = publicPrimitiveReadyCount;
    status[QStringLiteral("publicPrimitiveBlockedCount")] = publicPrimitiveBlockedCount;
    status[QStringLiteral("noSensitiveExportProof")] = noSensitiveExportProof;
    status[QStringLiteral("sensitiveFieldsSuppressed")] = noMaterialExport;
    setNoKeyExportFields(status);
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    status[QStringLiteral("plaintextBytesExported")] = false;
    status[QStringLiteral("ciphertextBytesExported")] = false;
    status[QStringLiteral("statusCapturePolicy")] =
        QStringLiteral("status-counts-release-gates-and-actions-only");
    status[QStringLiteral("evidenceRetention")] =
        QStringLiteral("operator-may-archive-sanitized-json-no-private-paths-or-material");
    status[QStringLiteral("operatorRecoveryPrompts")] = operatorPrompts;
    status[QStringLiteral("userRecoveryPrompts")] = userPrompts;
    status[QStringLiteral("filesystemObjectRecoveryReady")] = true;
    status[QStringLiteral("filesystemObjectRecoveryReleaseGate")] =
        QStringLiteral("e2e-filesystem-object-ciphertext-readback-ready");
    status[QStringLiteral("filesystemObjectRecoveryAction")] =
        QStringLiteral("resume-verified-filesystem-object-ciphertext-or-fail-closed-to-resend");
    status[QStringLiteral("filesystemObjectRecoveryCapturePolicy")] =
        QStringLiteral("object-key-hash-size-envelope-header-session-metadata-only");
    status[QStringLiteral("filesystemObjectRecoveryNoSensitiveExportProof")] = true;
    status[QStringLiteral("filesystemObjectRecoveryPromptReady")] = true;
    status[QStringLiteral("filesystemObjectRecoveryPrompt")] =
        QStringLiteral("show-resume-when-filesystem-object-evidence-matches-otherwise-resend");
    status[QStringLiteral("offlineObjectRecoveryReady")] = true;
    status[QStringLiteral("offlineObjectRecoveryScope")] =
        QStringLiteral("offline-ciphertext-readback");
    status[QStringLiteral("offlineObjectRecoveryReleaseGate")] =
        QStringLiteral("e2e-offline-ciphertext-readback-reviewed-opt-in");
    status[QStringLiteral("offlineObjectRecoveryAction")] =
        QStringLiteral("enable-reviewed-offline-ciphertext-mirror-or-fail-closed-to-resend");
    status[QStringLiteral("offlineObjectRecoveryCapturePolicy")] =
        QStringLiteral("safe-object-token-hash-size-envelope-header-session-metadata-only");
    status[QStringLiteral("offlineObjectRecoveryNoSensitiveExportProof")] = true;
    return status;
}
} // namespace E2EBackendStatus
