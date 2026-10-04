#include "e2e_backend_status_p.h"
#include <QCryptographicHash>
#include <QHash>
#include <cstddef>

namespace E2EBackendStatus {
QString providerReviewedCandidateBlockedReason(const QJsonObject& probe,
                                               bool probeSourceCaptured,
                                               bool executionEvidenceReady,
                                               bool candidateSanitized) {
    if (!probeSourceCaptured) {
        return QStringLiteral("production-provider-reviewed-candidate-awaiting-explicit-probe");
    }
    if (!probe.value(QStringLiteral("tableValidationAccepted")).toBool(false)) {
        const QString reason =
            probe.value(QStringLiteral("tableValidationBlockedReason")).toString();
        return reason.isEmpty()
            ? QStringLiteral("production-provider-table-validation-blocked")
            : reason;
    }
    if (!probe.value(QStringLiteral("functionPointerPresent")).toBool(false)) {
        return QStringLiteral("production-provider-operation-pointer-missing");
    }
    if (!probe.value(QStringLiteral("operationInvoked")).toBool(false)) {
        return probe.value(QStringLiteral("blockedReason")).toString(
            QStringLiteral("production-provider-probe-not-invoked"));
    }
    if (!probe.value(QStringLiteral("providerVectorSetMatched")).toBool(false)) {
        return probe.value(QStringLiteral("mismatchReason")).toString(
            QStringLiteral("known-answer-vector-mismatch"));
    }
    if (probe.value(QStringLiteral("mismatchReason")).toString() != QStringLiteral("none")
        || probe.value(QStringLiteral("mismatchSeverity")).toString() != QStringLiteral("none")
        || probe.value(QStringLiteral("mismatchScope")).toString() != QStringLiteral("none")) {
        return QStringLiteral("production-provider-probe-mismatch-not-clean");
    }
    if (!executionEvidenceReady) {
        return QStringLiteral("production-provider-execution-evidence-not-ready");
    }
    if (!candidateSanitized) {
        return QStringLiteral("production-provider-reviewed-candidate-sensitive-export-blocked");
    }
    return QString();
}

QJsonObject productionProviderReviewedExecutionCandidateStatusFromProbe(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& invocationExecutionProbe) {
    const QJsonObject structuralExecution =
        productionProviderInvocationExecutionStructuralSnapshotForDescriptor(descriptor);
    const QJsonArray structuralExecutions =
        structuralExecution.value(QStringLiteral("executions")).toArray();
    const QJsonArray probes = invocationExecutionProbe.value(QStringLiteral("probes")).toArray();
    const bool probeSourceCaptured =
        invocationExecutionProbe.value(QStringLiteral("schema")).toString()
            == QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-execution-probe-v1");

    QJsonArray candidates;
    int candidateCount = 0;
    int candidateReadyCount = 0;
    int blockedCandidateCount = 0;
    int probeMatrixMatchedCount = 0;
    int candidateSanitizedCount = 0;
    int candidateEntrypointCount = 0;
    int candidateVectorContractHashCount = 0;
    int candidateOutputShapeHashCount = 0;
    int mismatchFreeCandidateCount = 0;
    int pointerPresentCandidateCount = 0;
    int tableValidationAcceptedCandidateCount = 0;
    int noMaterialExportCandidateCount = 0;
    QJsonObject blockedReasonSummary;

    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject probe = sequenceIndex < probes.size()
            ? probes.at(sequenceIndex).toObject()
            : QJsonObject();
        const QJsonObject execution = sequenceIndex < structuralExecutions.size()
            ? structuralExecutions.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool providerVectorSetMatched =
            probe.value(QStringLiteral("providerVectorSetMatched")).toBool(false);
        const bool mismatchFree =
            probeSourceCaptured
            &&
            probe.value(QStringLiteral("mismatchReason")).toString() == QStringLiteral("none")
            && probe.value(QStringLiteral("mismatchSeverity")).toString() == QStringLiteral("none")
            && probe.value(QStringLiteral("mismatchScope")).toString() == QStringLiteral("none");
        const bool pointerPresent =
            probe.value(QStringLiteral("functionPointerPresent")).toBool(false);
        const bool tableValidationAccepted =
            probe.value(QStringLiteral("tableValidationAccepted")).toBool(false);
        const bool executionEvidenceReady =
            probe.value(QStringLiteral("probeExecutionFrameSchema")).toString()
                == QStringLiteral("qtnetworkchat-e2e-production-provider-probe-execution-frame-v1")
            && probe.value(QStringLiteral("probeKnownAnswerOutputEvidenceSchema")).toString()
                == QStringLiteral("qtnetworkchat-e2e-production-provider-probe-output-evidence-v1")
            && probe.value(QStringLiteral("inputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && probe.value(QStringLiteral("outputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && probe.value(QStringLiteral("outputShapeHashSha256")).toString().size()
                == FingerprintHexLength;
        const bool candidateEntrypoint =
            probe.value(QStringLiteral("providerSymbol")).toString()
                == productionOperationProviderSymbol(operation)
            && probe.value(QStringLiteral("providerAbiSignature")).toString()
                == productionOperationProviderAbiSignature(operation)
            && probe.value(QStringLiteral("probeExecutionEntryPoint")).toString()
                == QStringLiteral("qnc_e2e_provider_table_v1/%1")
                    .arg(productionOperationProviderSymbol(operation));
        const bool noMaterialExport =
            !probe.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !probe.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !probe.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !probe.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !probe.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool capturePolicySafe =
            probe.value(QStringLiteral("probeExecutionInputCapturePolicy")).toString()
                == QStringLiteral("size-and-class-only")
            && probe.value(QStringLiteral("probeExecutionOutputCapturePolicy")).toString()
                == QStringLiteral("size-and-class-only")
            && probe.value(QStringLiteral("probeExecutionResultCapturePolicy")).toString()
                == QStringLiteral("status-class-and-size-only")
            && !probe.value(QStringLiteral("inputBytesCaptured")).toBool(true)
            && !probe.value(QStringLiteral("outputBytesCaptured")).toBool(true);
        const bool candidateSanitized =
            capturePolicySafe
            && noMaterialExport
            && probe.value(QStringLiteral("materialExportProof")).toString()
                == QStringLiteral("sizes-and-status-only-no-secret-bytes");
        const bool readyCandidate =
            probeSourceCaptured
            && providerVectorSetMatched
            && mismatchFree
            && pointerPresent
            && tableValidationAccepted
            && executionEvidenceReady
            && candidateEntrypoint
            && candidateSanitized;
        const QString blockedReason =
            providerReviewedCandidateBlockedReason(probe,
                                                   probeSourceCaptured,
                                                   executionEvidenceReady,
                                                   candidateSanitized);

        QJsonObject candidate;
        candidate[QStringLiteral("sequenceIndex")] = sequenceIndex;
        candidate[QStringLiteral("operation")] = cryptoOperationName(operation);
        setProviderReportIdentity(candidate, descriptor);
        candidate[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
        candidate[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        candidate[QStringLiteral("vectorSet")] = spec.vectorSet;
        candidate[QStringLiteral("knownAnswerVectorId")] =
            probe.value(QStringLiteral("knownAnswerVectorId")).toString(
                QStringLiteral("%1/%2").arg(spec.vectorSet, cryptoOperationName(operation)));
        candidate[QStringLiteral("knownAnswerFixtureId")] =
            probe.value(QStringLiteral("knownAnswerFixtureId")).toString(
                QStringLiteral("probe-fixture/%1").arg(cryptoOperationName(operation)));
        candidate[QStringLiteral("fixtureHashSha256")] =
            probe.value(QStringLiteral("fixtureHashSha256")).toString(
                productionHarnessFixtureHash(spec));
        candidate[QStringLiteral("executionEntrypoint")] =
            QStringLiteral("qnc_e2e_provider_table_v1/%1")
                .arg(productionOperationProviderSymbol(operation));
        candidate[QStringLiteral("structuralExecution")] = execution;
        candidate[QStringLiteral("probeEvidence")] = probe;
        candidate[QStringLiteral("probeEvidenceSchema")] =
            invocationExecutionProbe.value(QStringLiteral("schema")).toString();
        candidate[QStringLiteral("probeMatrixMatched")] = providerVectorSetMatched;
        candidate[QStringLiteral("mismatchFree")] = mismatchFree;
        candidate[QStringLiteral("mismatchReason")] =
            probe.value(QStringLiteral("mismatchReason")).toString(
                probeSourceCaptured
                    ? QStringLiteral("production-provider-probe-missing-reason")
                    : QStringLiteral("production-provider-reviewed-candidate-awaiting-explicit-probe"));
        candidate[QStringLiteral("mismatchSeverity")] =
            probe.value(QStringLiteral("mismatchSeverity")).toString(
                probeSourceCaptured ? QStringLiteral("blocked") : QStringLiteral("blocked"));
        candidate[QStringLiteral("mismatchScope")] =
            probe.value(QStringLiteral("mismatchScope")).toString(
                probeSourceCaptured ? QStringLiteral("provider-invocation") : QStringLiteral("candidate-source"));
        candidate[QStringLiteral("tableValidationAccepted")] = tableValidationAccepted;
        candidate[QStringLiteral("operationPointerPresent")] = pointerPresent;
        candidate[QStringLiteral("operationPointerMissing")] =
            probe.value(QStringLiteral("operationPointerMissing")).toBool(false);
        candidate[QStringLiteral("executionEvidenceReady")] = executionEvidenceReady;
        candidate[QStringLiteral("candidateEntrypointReady")] = candidateEntrypoint;
        candidate[QStringLiteral("candidateState")] = readyCandidate
            ? QStringLiteral("reviewed-provider-execution-candidate-ready")
            : QStringLiteral("reviewed-provider-execution-candidate-blocked");
        candidate[QStringLiteral("candidateReady")] = readyCandidate;
        candidate[QStringLiteral("blockedReason")] = blockedReason;
        candidate[QStringLiteral("operatorAction")] = readyCandidate
            ? QStringLiteral("complete-reviewed-production-provider-audit-before-release-gate")
            : (probeSourceCaptured
                ? QStringLiteral("fix-provider-candidate-probe-evidence-before-review")
                : QStringLiteral("run-explicit-provider-invocation-probe-before-review"));
        candidate[QStringLiteral("candidateNonReleaseGate")] = true;
        candidate[QStringLiteral("releaseGate")] =
            QStringLiteral("production-provider-reviewed-execution-candidate-not-release-gate");
        candidate[QStringLiteral("sanitized")] = candidateSanitized;
        candidate[QStringLiteral("materialExportProof")] = candidateSanitized
            ? QStringLiteral("sizes-and-status-only-no-secret-bytes")
            : QStringLiteral("candidate-sensitive-export-blocked");
        candidate[QStringLiteral("inputBytesCaptured")] = false;
        candidate[QStringLiteral("outputBytesCaptured")] = false;
        candidate[QStringLiteral("operationInvokedByCandidate")] = false;
        setNoKeyExportFields(candidate);
        candidate[QStringLiteral("sessionSecretExported")] = false;
        candidate[QStringLiteral("privateIdentityMaterialExported")] = false;
        candidate[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        candidates.append(candidate);

        ++candidateCount;
        if (readyCandidate) {
            ++candidateReadyCount;
        } else {
            ++blockedCandidateCount;
        }
        if (providerVectorSetMatched) {
            ++probeMatrixMatchedCount;
        }
        if (candidateSanitized) {
            ++candidateSanitizedCount;
        }
        if (candidateEntrypoint) {
            ++candidateEntrypointCount;
        }
        if (probe.value(QStringLiteral("inputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && probe.value(QStringLiteral("outputContractHashSha256")).toString().size()
                == FingerprintHexLength) {
            ++candidateVectorContractHashCount;
        }
        if (probe.value(QStringLiteral("outputShapeHashSha256")).toString().size()
            == FingerprintHexLength) {
            ++candidateOutputShapeHashCount;
        }
        if (mismatchFree) {
            ++mismatchFreeCandidateCount;
        }
        if (pointerPresent) {
            ++pointerPresentCandidateCount;
        }
        if (tableValidationAccepted) {
            ++tableValidationAcceptedCandidateCount;
        }
        if (noMaterialExport) {
            ++noMaterialExportCandidateCount;
        }
        incrementSummaryCount(&blockedReasonSummary,
                              blockedReason.isEmpty()
                                  ? QStringLiteral("none")
                                  : blockedReason);
        ++sequenceIndex;
    }

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-execution-candidate-v1");
    setProviderReportIdentity(status, descriptor);
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("candidateNonReleaseGate")] = true;
    status[QStringLiteral("releaseGate")] =
        QStringLiteral("production-provider-reviewed-execution-candidate-not-release-gate");
    status[QStringLiteral("probeSourceCaptured")] = probeSourceCaptured;
    status[QStringLiteral("providerInvocationExecution")] = structuralExecution;
    status[QStringLiteral("providerInvocationExecutionReleaseGate")] =
        structuralExecution.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerInvocationExecutionAccepted")] =
        structuralExecution.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("providerInvocationExecutionProbe")] = invocationExecutionProbe;
    status[QStringLiteral("providerInvocationExecutionProbeReleaseGate")] =
        invocationExecutionProbe.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("candidateCount")] = candidateCount;
    status[QStringLiteral("candidateReadyCount")] = candidateReadyCount;
    status[QStringLiteral("blockedCandidateCount")] = blockedCandidateCount;
    status[QStringLiteral("probeMatrixMatchedCount")] = probeMatrixMatchedCount;
    status[QStringLiteral("candidateSanitizedCount")] = candidateSanitizedCount;
    status[QStringLiteral("candidateEntrypointCount")] = candidateEntrypointCount;
    status[QStringLiteral("candidateVectorContractHashCount")] =
        candidateVectorContractHashCount;
    status[QStringLiteral("candidateOutputShapeHashCount")] =
        candidateOutputShapeHashCount;
    status[QStringLiteral("mismatchFreeCandidateCount")] = mismatchFreeCandidateCount;
    status[QStringLiteral("pointerPresentCandidateCount")] = pointerPresentCandidateCount;
    status[QStringLiteral("tableValidationAcceptedCandidateCount")] =
        tableValidationAcceptedCandidateCount;
    status[QStringLiteral("materialExportProofCount")] = noMaterialExportCandidateCount;
    status[QStringLiteral("blockedReason")] = candidateReadyCount == candidateCount
        && candidateCount == cryptoOperations().size()
        ? QStringLiteral("production-provider-reviewed-candidates-awaiting-audit-release-gate")
        : (probeSourceCaptured
            ? QStringLiteral("production-provider-reviewed-candidate-evidence-blocked")
            : QStringLiteral("production-provider-reviewed-candidate-awaiting-explicit-probe"));
    status[QStringLiteral("operatorAction")] =
        QStringLiteral("promote-only-after-reviewed-production-provider-audit-and-release-gates");
    status[QStringLiteral("blockedReasonSummary")] = blockedReasonSummary;
    status[QStringLiteral("candidates")] = candidates;
    status[QStringLiteral("operationInvokedByCandidate")] = false;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    setNoKeyExportFields(status);
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderReviewedExecutionCandidateStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject invocationExecutionProbe = descriptor.linked && descriptor.productionReady
        ? productionProviderInvocationExecutionProbeForDescriptor(descriptor)
        : QJsonObject();
    return productionProviderReviewedExecutionCandidateStatusFromProbe(
        descriptor,
        invocationExecutionProbe);
}

QString providerReviewedCallHandoffBlockedReason(const QJsonObject& candidate,
                                                 bool candidateSourceCaptured,
                                                 bool handoffSanitized,
                                                 bool policyReady) {
    if (!candidateSourceCaptured) {
        return QStringLiteral("production-provider-reviewed-call-handoff-awaiting-candidate");
    }
    if (!candidate.value(QStringLiteral("candidateReady")).toBool(false)) {
        return candidate.value(QStringLiteral("blockedReason")).toString(
            QStringLiteral("production-provider-reviewed-candidate-blocked"));
    }
    if (!policyReady) {
        return QStringLiteral("production-provider-reviewed-call-handoff-policy-blocked");
    }
    if (!handoffSanitized) {
        return QStringLiteral("production-provider-reviewed-call-handoff-sensitive-export-blocked");
    }
    return QStringLiteral("production-provider-reviewed-call-handoff-awaiting-audit-release-gate");
}

QJsonObject productionProviderReviewedCallHandoffStatusFromCandidate(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedExecutionCandidate) {
    const QJsonArray candidates =
        reviewedExecutionCandidate.value(QStringLiteral("candidates")).toArray();
    const bool candidateSourceCaptured =
        reviewedExecutionCandidate.value(QStringLiteral("schema")).toString()
            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-execution-candidate-v1")
        && reviewedExecutionCandidate.value(QStringLiteral("probeSourceCaptured")).toBool(false);

    QJsonArray handoffs;
    int handoffCount = 0;
    int readyHandoffCount = 0;
    int blockedHandoffCount = 0;
    int sanitizedHandoffCount = 0;
    int candidateReadyHandoffCount = 0;
    int entrypointReadyHandoffCount = 0;
    int vectorContractHandoffCount = 0;
    int outputShapeHandoffCount = 0;
    int failClosedHandoffCount = 0;
    QJsonObject blockedReasonSummary;

    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject candidate = sequenceIndex < candidates.size()
            ? candidates.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool candidateReady =
            candidate.value(QStringLiteral("candidateReady")).toBool(false);
        const bool candidateEntrypointReady =
            candidate.value(QStringLiteral("candidateEntrypointReady")).toBool(false);
        const bool vectorContractReady =
            candidate.value(QStringLiteral("probeEvidence")).toObject()
                .value(QStringLiteral("vectorContractReady")).toBool(false)
            || (candidate.value(QStringLiteral("probeEvidence")).toObject()
                    .value(QStringLiteral("inputContractHashSha256")).toString().size()
                    == FingerprintHexLength
                && candidate.value(QStringLiteral("probeEvidence")).toObject()
                    .value(QStringLiteral("outputContractHashSha256")).toString().size()
                    == FingerprintHexLength);
        const bool outputShapeReady =
            candidate.value(QStringLiteral("probeEvidence")).toObject()
                .value(QStringLiteral("outputShapeHashSha256")).toString().size()
                == FingerprintHexLength;
        const bool policyReady =
            candidateEntrypointReady
            && vectorContractReady
            && outputShapeReady;
        const bool noSensitiveExport =
            !candidate.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !candidate.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !candidate.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !candidate.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !candidate.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true)
            && !candidate.value(QStringLiteral("inputBytesCaptured")).toBool(true)
            && !candidate.value(QStringLiteral("outputBytesCaptured")).toBool(true);
        const bool handoffSanitized =
            candidate.value(QStringLiteral("sanitized")).toBool(false)
            && noSensitiveExport;
        const bool readyHandoff =
            candidateSourceCaptured
            && candidateReady
            && policyReady
            && handoffSanitized;
        const QString blockedReason =
            providerReviewedCallHandoffBlockedReason(candidate,
                                                     candidateSourceCaptured,
                                                     handoffSanitized,
                                                     policyReady);

        QJsonObject handoff;
        handoff[QStringLiteral("sequenceIndex")] = sequenceIndex;
        handoff[QStringLiteral("operation")] = cryptoOperationName(operation);
        setProviderReportIdentity(handoff, descriptor);
        handoff[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
        handoff[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        handoff[QStringLiteral("executionEntrypoint")] =
            QStringLiteral("qnc_e2e_provider_table_v1/%1")
                .arg(productionOperationProviderSymbol(operation));
        handoff[QStringLiteral("handoffFrameId")] =
            QStringLiteral("reviewed-call-handoff/%1/%2")
                .arg(spec.vectorSet, cryptoOperationName(operation));
        handoff[QStringLiteral("vectorSet")] = spec.vectorSet;
        handoff[QStringLiteral("knownAnswerVectorId")] =
            candidate.value(QStringLiteral("knownAnswerVectorId")).toString(
                QStringLiteral("%1/%2").arg(spec.vectorSet, cryptoOperationName(operation)));
        handoff[QStringLiteral("knownAnswerFixtureId")] =
            candidate.value(QStringLiteral("knownAnswerFixtureId")).toString(
                QStringLiteral("probe-fixture/%1").arg(cryptoOperationName(operation)));
        handoff[QStringLiteral("fixtureHashSha256")] =
            candidate.value(QStringLiteral("fixtureHashSha256")).toString(
                productionHarnessFixtureHash(spec));
        handoff[QStringLiteral("candidate")] = candidate;
        handoff[QStringLiteral("candidateState")] =
            candidate.value(QStringLiteral("candidateState")).toString(
                QStringLiteral("reviewed-provider-execution-candidate-blocked"));
        handoff[QStringLiteral("candidateReady")] = candidateReady;
        handoff[QStringLiteral("candidateNonReleaseGate")] =
            candidate.value(QStringLiteral("candidateNonReleaseGate")).toBool(true);
        handoff[QStringLiteral("candidateReleaseGate")] =
            candidate.value(QStringLiteral("releaseGate")).toString(
                QStringLiteral("production-provider-reviewed-execution-candidate-not-release-gate"));
        handoff[QStringLiteral("candidateEntrypointReady")] = candidateEntrypointReady;
        handoff[QStringLiteral("vectorContractReady")] = vectorContractReady;
        handoff[QStringLiteral("outputShapeEvidenceReady")] = outputShapeReady;
        handoff[QStringLiteral("handoffPolicyReady")] = policyReady;
        handoff[QStringLiteral("handoffReady")] = readyHandoff;
        handoff[QStringLiteral("handoffState")] = readyHandoff
            ? QStringLiteral("ready-for-reviewed-provider-call-handoff")
            : QStringLiteral("reviewed-provider-call-handoff-blocked");
        handoff[QStringLiteral("failClosed")] = !readyHandoff;
        handoff[QStringLiteral("blockedReason")] = blockedReason;
        handoff[QStringLiteral("operatorAction")] = readyHandoff
            ? QStringLiteral("audit-reviewed-call-handoff-before-release-gate")
            : (candidateSourceCaptured
                ? QStringLiteral("fix-reviewed-call-handoff-candidate-before-audit")
                : QStringLiteral("run-reviewed-execution-candidate-probe-before-handoff"));
        handoff[QStringLiteral("inputCapturePolicy")] =
            QStringLiteral("size-and-class-only");
        handoff[QStringLiteral("outputCapturePolicy")] =
            QStringLiteral("size-and-class-only");
        handoff[QStringLiteral("resultCapturePolicy")] =
            QStringLiteral("status-class-and-size-only");
        handoff[QStringLiteral("materialExportPolicy")] =
            QStringLiteral("sizes-and-status-only-no-secret-bytes");
        handoff[QStringLiteral("handoffNonReleaseGate")] = true;
        handoff[QStringLiteral("releaseGate")] =
            QStringLiteral("production-provider-reviewed-call-handoff-not-release-gate");
        handoff[QStringLiteral("operationInvokedByHandoff")] = false;
        handoff[QStringLiteral("inputBytesCaptured")] = false;
        handoff[QStringLiteral("outputBytesCaptured")] = false;
        handoff[QStringLiteral("resultCaptured")] = false;
        setNoKeyExportFields(handoff);
        handoff[QStringLiteral("sessionSecretExported")] = false;
        handoff[QStringLiteral("privateIdentityMaterialExported")] = false;
        handoff[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        handoff[QStringLiteral("sanitized")] = handoffSanitized;
        handoffs.append(handoff);

        ++handoffCount;
        if (readyHandoff) {
            ++readyHandoffCount;
        } else {
            ++blockedHandoffCount;
            ++failClosedHandoffCount;
        }
        if (handoffSanitized) {
            ++sanitizedHandoffCount;
        }
        if (candidateReady) {
            ++candidateReadyHandoffCount;
        }
        if (candidateEntrypointReady) {
            ++entrypointReadyHandoffCount;
        }
        if (vectorContractReady) {
            ++vectorContractHandoffCount;
        }
        if (outputShapeReady) {
            ++outputShapeHandoffCount;
        }
        incrementSummaryCount(&blockedReasonSummary, blockedReason);
        ++sequenceIndex;
    }

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-call-handoff-v1");
    setProviderReportIdentity(status, descriptor);
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("handoffNonReleaseGate")] = true;
    status[QStringLiteral("releaseGate")] =
        QStringLiteral("production-provider-reviewed-call-handoff-not-release-gate");
    status[QStringLiteral("candidateSourceCaptured")] = candidateSourceCaptured;
    status[QStringLiteral("providerReviewedExecutionCandidate")] = reviewedExecutionCandidate;
    status[QStringLiteral("providerReviewedExecutionCandidateReleaseGate")] =
        reviewedExecutionCandidate.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedExecutionCandidateReadyCount")] =
        reviewedExecutionCandidate.value(QStringLiteral("candidateReadyCount")).toInt();
    status[QStringLiteral("handoffCount")] = handoffCount;
    status[QStringLiteral("readyHandoffCount")] = readyHandoffCount;
    status[QStringLiteral("blockedHandoffCount")] = blockedHandoffCount;
    status[QStringLiteral("sanitizedHandoffCount")] = sanitizedHandoffCount;
    status[QStringLiteral("candidateReadyHandoffCount")] = candidateReadyHandoffCount;
    status[QStringLiteral("entrypointReadyHandoffCount")] = entrypointReadyHandoffCount;
    status[QStringLiteral("vectorContractHandoffCount")] = vectorContractHandoffCount;
    status[QStringLiteral("outputShapeHandoffCount")] = outputShapeHandoffCount;
    status[QStringLiteral("failClosedHandoffCount")] = failClosedHandoffCount;
    status[QStringLiteral("blockedReason")] = readyHandoffCount == handoffCount
        && handoffCount == cryptoOperations().size()
        ? QStringLiteral("production-provider-reviewed-call-handoffs-awaiting-audit-release-gate")
        : (candidateSourceCaptured
            ? QStringLiteral("production-provider-reviewed-call-handoff-evidence-blocked")
            : QStringLiteral("production-provider-reviewed-call-handoff-awaiting-candidate"));
    status[QStringLiteral("operatorAction")] =
        QStringLiteral("promote-handoff-only-after-reviewed-provider-audit-and-release-gates");
    status[QStringLiteral("blockedReasonSummary")] = blockedReasonSummary;
    status[QStringLiteral("handoffs")] = handoffs;
    status[QStringLiteral("operationInvokedByHandoff")] = false;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultCaptured")] = false;
    setNoKeyExportFields(status);
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderReviewedCallHandoffStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    return productionProviderReviewedCallHandoffStatusFromCandidate(
        descriptor,
        productionProviderReviewedExecutionCandidateStatusForDescriptor(descriptor));
}

QString providerReviewedOperationStubBoundaryBlockedReason(const QJsonObject& handoff,
                                                           bool handoffSourceCaptured,
                                                           bool stubPolicyReady,
                                                           bool stubSanitized) {
    if (!handoffSourceCaptured) {
        return QStringLiteral("production-provider-reviewed-operation-stub-awaiting-handoff");
    }
    if (!handoff.value(QStringLiteral("handoffReady")).toBool(false)) {
        return handoff.value(QStringLiteral("blockedReason")).toString(
            QStringLiteral("production-provider-reviewed-call-handoff-blocked"));
    }
    if (!stubPolicyReady) {
        return QStringLiteral("production-provider-reviewed-operation-stub-policy-blocked");
    }
    if (!stubSanitized) {
        return QStringLiteral("production-provider-reviewed-operation-stub-sensitive-export-blocked");
    }
    return QStringLiteral("production-provider-reviewed-operation-stubs-awaiting-audit-release-gate");
}

QJsonObject productionProviderReviewedOperationStubBoundaryStatusFromHandoff(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedCallHandoff) {
    const QJsonArray handoffs =
        reviewedCallHandoff.value(QStringLiteral("handoffs")).toArray();
    const bool handoffSourceCaptured =
        reviewedCallHandoff.value(QStringLiteral("schema")).toString()
            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-call-handoff-v1")
        && reviewedCallHandoff.value(QStringLiteral("candidateSourceCaptured")).toBool(false);

    QJsonArray stubs;
    int stubCount = 0;
    int readyStubCount = 0;
    int blockedStubCount = 0;
    int sanitizedStubCount = 0;
    int callableBoundaryCount = 0;
    int handoffReadyStubCount = 0;
    int contractHashStubCount = 0;
    int failClosedStubCount = 0;
    QJsonObject blockedReasonSummary;

    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject handoff = sequenceIndex < handoffs.size()
            ? handoffs.at(sequenceIndex).toObject()
            : QJsonObject();
        const QJsonObject candidate =
            handoff.value(QStringLiteral("candidate")).toObject();
        const QJsonObject probe =
            candidate.value(QStringLiteral("probeEvidence")).toObject();
        const bool handoffReady =
            handoff.value(QStringLiteral("handoffReady")).toBool(false);
        const bool callableBoundary =
            handoff.value(QStringLiteral("executionEntrypoint")).toString()
                == QStringLiteral("qnc_e2e_provider_table_v1/%1")
                    .arg(productionOperationProviderSymbol(operation))
            && handoff.value(QStringLiteral("providerAbiSignature")).toString()
                == productionOperationProviderAbiSignature(operation);
        const bool contractHashReady =
            probe.value(QStringLiteral("inputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && probe.value(QStringLiteral("outputContractHashSha256")).toString().size()
                == FingerprintHexLength;
        const bool policyReady =
            handoff.value(QStringLiteral("inputCapturePolicy")).toString()
                == QStringLiteral("size-and-class-only")
            && handoff.value(QStringLiteral("outputCapturePolicy")).toString()
                == QStringLiteral("size-and-class-only")
            && handoff.value(QStringLiteral("resultCapturePolicy")).toString()
                == QStringLiteral("status-class-and-size-only")
            && handoff.value(QStringLiteral("materialExportPolicy")).toString()
                == QStringLiteral("sizes-and-status-only-no-secret-bytes");
        const bool noSensitiveExport =
            !handoff.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !handoff.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !handoff.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !handoff.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !handoff.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true)
            && !handoff.value(QStringLiteral("inputBytesCaptured")).toBool(true)
            && !handoff.value(QStringLiteral("outputBytesCaptured")).toBool(true);
        const bool stubSanitized =
            handoff.value(QStringLiteral("sanitized")).toBool(false)
            && noSensitiveExport;
        const bool stubReady =
            handoffSourceCaptured
            && handoffReady
            && callableBoundary
            && contractHashReady
            && policyReady
            && stubSanitized;
        const QString blockedReason =
            providerReviewedOperationStubBoundaryBlockedReason(handoff,
                                                               handoffSourceCaptured,
                                                               policyReady,
                                                               stubSanitized);

        QJsonObject stub;
        stub[QStringLiteral("sequenceIndex")] = sequenceIndex;
        stub[QStringLiteral("operation")] = cryptoOperationName(operation);
        setProviderReportIdentity(stub, descriptor);
        stub[QStringLiteral("stubBoundaryId")] =
            QStringLiteral("reviewed-operation-stub/%1/%2")
                .arg(spec.vectorSet, cryptoOperationName(operation));
        stub[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
        stub[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        stub[QStringLiteral("executionEntrypoint")] =
            QStringLiteral("qnc_e2e_provider_table_v1/%1")
                .arg(productionOperationProviderSymbol(operation));
        stub[QStringLiteral("handoff")] = handoff;
        stub[QStringLiteral("handoffReady")] = handoffReady;
        stub[QStringLiteral("handoffReleaseGate")] =
            handoff.value(QStringLiteral("releaseGate")).toString(
                QStringLiteral("production-provider-reviewed-call-handoff-not-release-gate"));
        stub[QStringLiteral("handoffNonReleaseGate")] =
            handoff.value(QStringLiteral("handoffNonReleaseGate")).toBool(true);
        stub[QStringLiteral("callableBoundaryReady")] = callableBoundary;
        stub[QStringLiteral("contractHashReady")] = contractHashReady;
        stub[QStringLiteral("inputContractHashSha256")] =
            probe.value(QStringLiteral("inputContractHashSha256")).toString();
        stub[QStringLiteral("outputContractHashSha256")] =
            probe.value(QStringLiteral("outputContractHashSha256")).toString();
        stub[QStringLiteral("knownAnswerVectorId")] =
            handoff.value(QStringLiteral("knownAnswerVectorId")).toString(
                QStringLiteral("%1/%2").arg(spec.vectorSet, cryptoOperationName(operation)));
        stub[QStringLiteral("knownAnswerFixtureId")] =
            handoff.value(QStringLiteral("knownAnswerFixtureId")).toString(
                QStringLiteral("probe-fixture/%1").arg(cryptoOperationName(operation)));
        stub[QStringLiteral("fixtureHashSha256")] =
            handoff.value(QStringLiteral("fixtureHashSha256")).toString(
                productionHarnessFixtureHash(spec));
        stub[QStringLiteral("inputCapturePolicy")] =
            QStringLiteral("size-and-class-only");
        stub[QStringLiteral("outputCapturePolicy")] =
            QStringLiteral("size-and-class-only");
        stub[QStringLiteral("resultCapturePolicy")] =
            QStringLiteral("status-class-and-size-only");
        stub[QStringLiteral("materialExportPolicy")] =
            QStringLiteral("sizes-and-status-only-no-secret-bytes");
        stub[QStringLiteral("stubPolicyReady")] = policyReady;
        stub[QStringLiteral("stubReady")] = stubReady;
        stub[QStringLiteral("stubState")] = stubReady
            ? QStringLiteral("ready-for-reviewed-provider-operation-stub")
            : QStringLiteral("reviewed-provider-operation-stub-blocked");
        stub[QStringLiteral("failClosed")] = !stubReady;
        stub[QStringLiteral("blockedReason")] = blockedReason;
        stub[QStringLiteral("operatorAction")] = stubReady
            ? QStringLiteral("audit-reviewed-operation-stub-before-release-gate")
            : (handoffSourceCaptured
                ? QStringLiteral("fix-reviewed-operation-stub-boundary-before-audit")
                : QStringLiteral("produce-reviewed-call-handoff-before-stub-boundary"));
        stub[QStringLiteral("stubNonReleaseGate")] = true;
        stub[QStringLiteral("releaseGate")] =
            QStringLiteral("production-provider-reviewed-operation-stub-not-release-gate");
        stub[QStringLiteral("operationInvokedByStub")] = false;
        stub[QStringLiteral("inputBytesCaptured")] = false;
        stub[QStringLiteral("outputBytesCaptured")] = false;
        stub[QStringLiteral("resultCaptured")] = false;
        setNoKeyExportFields(stub);
        stub[QStringLiteral("sessionSecretExported")] = false;
        stub[QStringLiteral("privateIdentityMaterialExported")] = false;
        stub[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        stub[QStringLiteral("sanitized")] = stubSanitized;
        stubs.append(stub);

        ++stubCount;
        if (stubReady) {
            ++readyStubCount;
        } else {
            ++blockedStubCount;
            ++failClosedStubCount;
        }
        if (stubSanitized) {
            ++sanitizedStubCount;
        }
        if (callableBoundary) {
            ++callableBoundaryCount;
        }
        if (handoffReady) {
            ++handoffReadyStubCount;
        }
        if (contractHashReady) {
            ++contractHashStubCount;
        }
        incrementSummaryCount(&blockedReasonSummary, blockedReason);
        ++sequenceIndex;
    }

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-operation-stub-boundary-v1");
    setProviderReportIdentity(status, descriptor);
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("stubNonReleaseGate")] = true;
    status[QStringLiteral("releaseGate")] =
        QStringLiteral("production-provider-reviewed-operation-stub-not-release-gate");
    status[QStringLiteral("handoffSourceCaptured")] = handoffSourceCaptured;
    status[QStringLiteral("providerReviewedCallHandoff")] = reviewedCallHandoff;
    status[QStringLiteral("providerReviewedCallHandoffReleaseGate")] =
        reviewedCallHandoff.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedCallHandoffReadyCount")] =
        reviewedCallHandoff.value(QStringLiteral("readyHandoffCount")).toInt();
    status[QStringLiteral("stubCount")] = stubCount;
    status[QStringLiteral("readyStubCount")] = readyStubCount;
    status[QStringLiteral("blockedStubCount")] = blockedStubCount;
    status[QStringLiteral("sanitizedStubCount")] = sanitizedStubCount;
    status[QStringLiteral("callableBoundaryCount")] = callableBoundaryCount;
    status[QStringLiteral("handoffReadyStubCount")] = handoffReadyStubCount;
    status[QStringLiteral("contractHashStubCount")] = contractHashStubCount;
    status[QStringLiteral("failClosedStubCount")] = failClosedStubCount;
    status[QStringLiteral("blockedReason")] = readyStubCount == stubCount
        && stubCount == cryptoOperations().size()
        ? QStringLiteral("production-provider-reviewed-operation-stubs-awaiting-audit-release-gate")
        : (handoffSourceCaptured
            ? QStringLiteral("production-provider-reviewed-operation-stub-evidence-blocked")
            : QStringLiteral("production-provider-reviewed-operation-stub-awaiting-handoff"));
    status[QStringLiteral("operatorAction")] =
        QStringLiteral("promote-stub-only-after-reviewed-provider-audit-and-release-gates");
    status[QStringLiteral("blockedReasonSummary")] = blockedReasonSummary;
    status[QStringLiteral("stubs")] = stubs;
    status[QStringLiteral("operationInvokedByStub")] = false;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultCaptured")] = false;
    setNoKeyExportFields(status);
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderReviewedOperationStubBoundaryStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    return productionProviderReviewedOperationStubBoundaryStatusFromHandoff(
        descriptor,
        productionProviderReviewedCallHandoffStatusForDescriptor(descriptor));
}

QString providerReviewedCallableTableBridgeBlockedReason(const QJsonObject& stub,
                                                         bool stubSourceCaptured,
                                                         bool tableSlotReady,
                                                         bool contractHashReady,
                                                         bool bridgeSanitized) {
    if (!stubSourceCaptured) {
        return QStringLiteral("production-provider-reviewed-callable-table-bridge-awaiting-stub");
    }
    if (!stub.value(QStringLiteral("stubReady")).toBool(false)) {
        return stub.value(QStringLiteral("blockedReason")).toString(
            QStringLiteral("production-provider-reviewed-operation-stub-blocked"));
    }
    if (!tableSlotReady) {
        return QStringLiteral("production-provider-reviewed-callable-table-slot-blocked");
    }
    if (!contractHashReady) {
        return QStringLiteral("production-provider-reviewed-callable-table-contract-blocked");
    }
    if (!bridgeSanitized) {
        return QStringLiteral("production-provider-reviewed-callable-table-sensitive-export-blocked");
    }
    return QStringLiteral("production-provider-reviewed-callable-table-bridges-awaiting-audit-release-gate");
}

QJsonObject productionProviderReviewedCallableTableBridgeStatusFromStubBoundary(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedOperationStubBoundary) {
    const QJsonArray stubs =
        reviewedOperationStubBoundary.value(QStringLiteral("stubs")).toArray();
    const bool stubSourceCaptured =
        reviewedOperationStubBoundary.value(QStringLiteral("schema")).toString()
            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-operation-stub-boundary-v1")
        && reviewedOperationStubBoundary.value(QStringLiteral("handoffSourceCaptured")).toBool(false);

    QJsonArray bridges;
    int bridgeCount = 0;
    int readyBridgeCount = 0;
    int blockedBridgeCount = 0;
    int sanitizedBridgeCount = 0;
    int stubReadyBridgeCount = 0;
    int functionPointerSlotCount = 0;
    int contractHashBridgeCount = 0;
    int failClosedBridgeCount = 0;
    QJsonObject blockedReasonSummary;

    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QString operationName = cryptoOperationName(operation);
        const QJsonObject stub = sequenceIndex < stubs.size()
            ? stubs.at(sequenceIndex).toObject()
            : QJsonObject();
        const QString providerSymbol = productionOperationProviderSymbol(operation);
        const QString expectedTableSlot =
            QStringLiteral("qnc_e2e_provider_table_v1/%1").arg(providerSymbol);
        const bool stubReady = stub.value(QStringLiteral("stubReady")).toBool(false);
        const bool tableSlotReady =
            stub.value(QStringLiteral("executionEntrypoint")).toString() == expectedTableSlot
            && stub.value(QStringLiteral("providerSymbol")).toString() == providerSymbol
            && stub.value(QStringLiteral("providerAbiSignature")).toString()
                == productionOperationProviderAbiSignature(operation);
        const bool contractHashReady =
            stub.value(QStringLiteral("inputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && stub.value(QStringLiteral("outputContractHashSha256")).toString().size()
                == FingerprintHexLength;
        const bool bridgeSanitized =
            stub.value(QStringLiteral("sanitized")).toBool(false)
            && !stub.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !stub.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !stub.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !stub.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !stub.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true)
            && !stub.value(QStringLiteral("inputBytesCaptured")).toBool(true)
            && !stub.value(QStringLiteral("outputBytesCaptured")).toBool(true)
            && !stub.value(QStringLiteral("resultCaptured")).toBool(true);
        const bool bridgeReady =
            stubSourceCaptured
            && stubReady
            && tableSlotReady
            && contractHashReady
            && bridgeSanitized;
        const QString blockedReason =
            providerReviewedCallableTableBridgeBlockedReason(stub,
                                                             stubSourceCaptured,
                                                             tableSlotReady,
                                                             contractHashReady,
                                                             bridgeSanitized);

        QJsonObject bridge;
        bridge[QStringLiteral("sequenceIndex")] = sequenceIndex;
        bridge[QStringLiteral("operation")] = operationName;
        setProviderReportIdentity(bridge, descriptor);
        bridge[QStringLiteral("bridgeId")] =
            QStringLiteral("reviewed-callable-table-bridge/%1/%2")
                .arg(spec.vectorSet, operationName);
        bridge[QStringLiteral("stubBoundaryId")] =
            stub.value(QStringLiteral("stubBoundaryId")).toString(
                QStringLiteral("reviewed-operation-stub/%1/%2")
                    .arg(spec.vectorSet, operationName));
        bridge[QStringLiteral("providerSymbol")] = providerSymbol;
        bridge[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        bridge[QStringLiteral("executionEntrypoint")] = expectedTableSlot;
        bridge[QStringLiteral("providerTableSlot")] = expectedTableSlot;
        bridge[QStringLiteral("stub")] = stub;
        bridge[QStringLiteral("stubReady")] = stubReady;
        bridge[QStringLiteral("stubReleaseGate")] =
            stub.value(QStringLiteral("releaseGate")).toString(
                QStringLiteral("production-provider-reviewed-operation-stub-not-release-gate"));
        bridge[QStringLiteral("stubNonReleaseGate")] =
            stub.value(QStringLiteral("stubNonReleaseGate")).toBool(true);
        bridge[QStringLiteral("callableTableBridgeReady")] = bridgeReady;
        bridge[QStringLiteral("tableSlotReady")] = tableSlotReady;
        bridge[QStringLiteral("contractHashReady")] = contractHashReady;
        bridge[QStringLiteral("inputContractHashSha256")] =
            stub.value(QStringLiteral("inputContractHashSha256")).toString();
        bridge[QStringLiteral("outputContractHashSha256")] =
            stub.value(QStringLiteral("outputContractHashSha256")).toString();
        bridge[QStringLiteral("knownAnswerVectorId")] =
            stub.value(QStringLiteral("knownAnswerVectorId")).toString(
                QStringLiteral("%1/%2").arg(spec.vectorSet, operationName));
        bridge[QStringLiteral("knownAnswerFixtureId")] =
            stub.value(QStringLiteral("knownAnswerFixtureId")).toString(
                QStringLiteral("probe-fixture/%1").arg(operationName));
        bridge[QStringLiteral("fixtureHashSha256")] =
            stub.value(QStringLiteral("fixtureHashSha256")).toString(
                productionHarnessFixtureHash(spec));
        bridge[QStringLiteral("inputCapturePolicy")] = QStringLiteral("size-and-class-only");
        bridge[QStringLiteral("outputCapturePolicy")] = QStringLiteral("size-and-class-only");
        bridge[QStringLiteral("resultCapturePolicy")] =
            QStringLiteral("status-class-and-size-only");
        bridge[QStringLiteral("materialExportPolicy")] =
            QStringLiteral("sizes-and-status-only-no-secret-bytes");
        bridge[QStringLiteral("bridgeSanitized")] = bridgeSanitized;
        bridge[QStringLiteral("bridgeState")] = bridgeReady
            ? QStringLiteral("ready-for-reviewed-provider-callable-table-bridge")
            : QStringLiteral("reviewed-provider-callable-table-bridge-blocked");
        bridge[QStringLiteral("failClosed")] = !bridgeReady;
        bridge[QStringLiteral("blockedReason")] = blockedReason;
        bridge[QStringLiteral("operatorAction")] = bridgeReady
            ? QStringLiteral("audit-reviewed-callable-table-bridge-before-release-gate")
            : (stubSourceCaptured
                ? QStringLiteral("fix-reviewed-callable-table-bridge-before-audit")
                : QStringLiteral("produce-reviewed-operation-stub-before-callable-table-bridge"));
        bridge[QStringLiteral("bridgeNonReleaseGate")] = true;
        bridge[QStringLiteral("releaseGate")] =
            QStringLiteral("production-provider-reviewed-callable-table-bridge-not-release-gate");
        bridge[QStringLiteral("operationInvokedByBridge")] = false;
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
        if (bridgeSanitized) {
            ++sanitizedBridgeCount;
        }
        if (stubReady) {
            ++stubReadyBridgeCount;
        }
        if (tableSlotReady) {
            ++functionPointerSlotCount;
        }
        if (contractHashReady) {
            ++contractHashBridgeCount;
        }
        incrementSummaryCount(&blockedReasonSummary, blockedReason);
        ++sequenceIndex;
    }

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-callable-table-bridge-v1");
    setProviderReportIdentity(status, descriptor);
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("bridgeNonReleaseGate")] = true;
    status[QStringLiteral("releaseGate")] =
        QStringLiteral("production-provider-reviewed-callable-table-bridge-not-release-gate");
    status[QStringLiteral("stubSourceCaptured")] = stubSourceCaptured;
    status[QStringLiteral("providerReviewedOperationStubBoundary")] =
        reviewedOperationStubBoundary;
    status[QStringLiteral("providerReviewedOperationStubBoundaryReleaseGate")] =
        reviewedOperationStubBoundary.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedOperationStubBoundaryReadyCount")] =
        reviewedOperationStubBoundary.value(QStringLiteral("readyStubCount")).toInt();
    status[QStringLiteral("bridgeCount")] = bridgeCount;
    status[QStringLiteral("readyBridgeCount")] = readyBridgeCount;
    status[QStringLiteral("blockedBridgeCount")] = blockedBridgeCount;
    status[QStringLiteral("sanitizedBridgeCount")] = sanitizedBridgeCount;
    status[QStringLiteral("stubReadyBridgeCount")] = stubReadyBridgeCount;
    status[QStringLiteral("functionPointerSlotCount")] = functionPointerSlotCount;
    status[QStringLiteral("contractHashBridgeCount")] = contractHashBridgeCount;
    status[QStringLiteral("failClosedBridgeCount")] = failClosedBridgeCount;
    status[QStringLiteral("blockedReason")] = readyBridgeCount == bridgeCount
        && bridgeCount == cryptoOperations().size()
        ? QStringLiteral("production-provider-reviewed-callable-table-bridges-awaiting-audit-release-gate")
        : (stubSourceCaptured
            ? QStringLiteral("production-provider-reviewed-callable-table-bridge-evidence-blocked")
            : QStringLiteral("production-provider-reviewed-callable-table-bridge-awaiting-stub"));
    status[QStringLiteral("operatorAction")] =
        QStringLiteral("promote-bridge-only-after-reviewed-provider-audit-and-release-gates");
    status[QStringLiteral("blockedReasonSummary")] = blockedReasonSummary;
    status[QStringLiteral("bridges")] = bridges;
    status[QStringLiteral("operationInvokedByBridge")] = false;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultCaptured")] = false;
    setNoKeyExportFields(status);
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderReviewedCallableTableBridgeStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    return productionProviderReviewedCallableTableBridgeStatusFromStubBoundary(
        descriptor,
        productionProviderReviewedOperationStubBoundaryStatusForDescriptor(descriptor));
}

QString providerReviewedOperationCallableInterfaceBlockedReason(const QJsonObject& bridge,
                                                                bool bridgeSourceCaptured,
                                                                bool functionPointerReady,
                                                                bool structContractReady,
                                                                bool materialPolicyReady,
                                                                bool interfaceSanitized) {
    if (!bridgeSourceCaptured) {
        return QStringLiteral("production-provider-reviewed-operation-callable-interface-awaiting-bridge");
    }
    if (!bridge.value(QStringLiteral("callableTableBridgeReady")).toBool(false)) {
        return bridge.value(QStringLiteral("blockedReason")).toString(
            QStringLiteral("production-provider-reviewed-callable-table-bridge-blocked"));
    }
    if (!functionPointerReady) {
        return QStringLiteral("production-provider-reviewed-operation-callable-interface-pointer-blocked");
    }
    if (!structContractReady) {
        return QStringLiteral("production-provider-reviewed-operation-callable-interface-contract-blocked");
    }
    if (!materialPolicyReady) {
        return QStringLiteral("production-provider-reviewed-operation-callable-interface-material-policy-blocked");
    }
    if (!interfaceSanitized) {
        return QStringLiteral("production-provider-reviewed-operation-callable-interface-sensitive-export-blocked");
    }
    return QStringLiteral("production-provider-reviewed-operation-callable-interfaces-awaiting-audit-release-gate");
}

QJsonObject productionProviderReviewedOperationCallableInterfaceStatusFromBridge(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedCallableTableBridge) {
    const QJsonArray bridges =
        reviewedCallableTableBridge.value(QStringLiteral("bridges")).toArray();
    const bool bridgeSourceCaptured =
        reviewedCallableTableBridge.value(QStringLiteral("schema")).toString()
            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-callable-table-bridge-v1")
        && reviewedCallableTableBridge.value(QStringLiteral("stubSourceCaptured")).toBool(false);

    QJsonArray interfaces;
    int interfaceCount = 0;
    int readyInterfaceCount = 0;
    int blockedInterfaceCount = 0;
    int bridgeReadyInterfaceCount = 0;
    int functionPointerInterfaceCount = 0;
    int structContractInterfaceCount = 0;
    int materialPolicyInterfaceCount = 0;
    int sanitizedInterfaceCount = 0;
    int failClosedInterfaceCount = 0;
    QJsonObject blockedReasonSummary;

    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QString operationName = cryptoOperationName(operation);
        const QJsonObject bridge = sequenceIndex < bridges.size()
            ? bridges.at(sequenceIndex).toObject()
            : QJsonObject();
        const QString providerSymbol = productionOperationProviderSymbol(operation);
        const QString expectedTableSlot =
            QStringLiteral("qnc_e2e_provider_table_v1/%1").arg(providerSymbol);
        const bool bridgeReady =
            bridge.value(QStringLiteral("callableTableBridgeReady")).toBool(false);
        const bool functionPointerReady =
            bridge.value(QStringLiteral("providerTableSlot")).toString() == expectedTableSlot
            && bridge.value(QStringLiteral("providerSymbol")).toString() == providerSymbol
            && bridge.value(QStringLiteral("providerAbiSignature")).toString()
                == productionOperationProviderAbiSignature(operation);
        const bool structContractReady =
            bridge.value(QStringLiteral("inputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && bridge.value(QStringLiteral("outputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && QString::fromLatin1(QNC_E2E_OPERATION_CONTRACT_VERSION)
                == descriptor.operationContractVersion
            && QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT == cryptoOperations().size();
        const bool materialPolicyReady =
            bridge.value(QStringLiteral("inputCapturePolicy")).toString()
                == QStringLiteral("size-and-class-only")
            && bridge.value(QStringLiteral("outputCapturePolicy")).toString()
                == QStringLiteral("size-and-class-only")
            && bridge.value(QStringLiteral("resultCapturePolicy")).toString()
                == QStringLiteral("status-class-and-size-only")
            && bridge.value(QStringLiteral("materialExportPolicy")).toString()
                == QStringLiteral("sizes-and-status-only-no-secret-bytes");
        const bool interfaceSanitized =
            bridge.value(QStringLiteral("bridgeSanitized")).toBool(false)
            && !bridge.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !bridge.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !bridge.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !bridge.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !bridge.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true)
            && !bridge.value(QStringLiteral("inputBytesCaptured")).toBool(true)
            && !bridge.value(QStringLiteral("outputBytesCaptured")).toBool(true)
            && !bridge.value(QStringLiteral("resultCaptured")).toBool(true);
        const bool interfaceReady =
            bridgeSourceCaptured
            && bridgeReady
            && functionPointerReady
            && structContractReady
            && materialPolicyReady
            && interfaceSanitized;
        const QString blockedReason =
            providerReviewedOperationCallableInterfaceBlockedReason(bridge,
                                                                    bridgeSourceCaptured,
                                                                    functionPointerReady,
                                                                    structContractReady,
                                                                    materialPolicyReady,
                                                                    interfaceSanitized);

        QJsonObject callableInterface;
        callableInterface[QStringLiteral("sequenceIndex")] = sequenceIndex;
        callableInterface[QStringLiteral("operation")] = operationName;
        setProviderReportIdentity(callableInterface, descriptor);
        callableInterface[QStringLiteral("callableInterfaceId")] =
            QStringLiteral("reviewed-operation-callable-interface/%1/%2")
                .arg(spec.vectorSet, operationName);
        callableInterface[QStringLiteral("bridgeId")] =
            bridge.value(QStringLiteral("bridgeId")).toString(
                QStringLiteral("reviewed-callable-table-bridge/%1/%2")
                    .arg(spec.vectorSet, operationName));
        callableInterface[QStringLiteral("bridge")] = bridge;
        callableInterface[QStringLiteral("providerSymbol")] = providerSymbol;
        callableInterface[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        callableInterface[QStringLiteral("providerTableSlot")] = expectedTableSlot;
        callableInterface[QStringLiteral("functionPointerTypedef")] =
            QStringLiteral("qnc_e2e_provider_operation_v1");
        callableInterface[QStringLiteral("inputStructAbi")] =
            QStringLiteral("qnc_e2e_operation_input_v1");
        callableInterface[QStringLiteral("outputStructAbi")] =
            QStringLiteral("qnc_e2e_operation_output_v1");
        callableInterface[QStringLiteral("statusEnumAbi")] =
            QStringLiteral("qnc_e2e_status_t");
        callableInterface[QStringLiteral("materialPolicyEnumAbi")] =
            QStringLiteral("qnc_e2e_material_policy_t");
        callableInterface[QStringLiteral("operationEnumValue")] = sequenceIndex;
        callableInterface[QStringLiteral("operationEnumMatchesHeader")] =
            static_cast<int>(static_cast<qnc_e2e_operation_t>(sequenceIndex)) == sequenceIndex;
        callableInterface[QStringLiteral("bridgeReady")] = bridgeReady;
        callableInterface[QStringLiteral("functionPointerReady")] = functionPointerReady;
        callableInterface[QStringLiteral("structContractReady")] = structContractReady;
        callableInterface[QStringLiteral("materialPolicyReady")] = materialPolicyReady;
        callableInterface[QStringLiteral("callableInterfaceReady")] = interfaceReady;
        callableInterface[QStringLiteral("inputContractHashSha256")] =
            bridge.value(QStringLiteral("inputContractHashSha256")).toString();
        callableInterface[QStringLiteral("outputContractHashSha256")] =
            bridge.value(QStringLiteral("outputContractHashSha256")).toString();
        callableInterface[QStringLiteral("knownAnswerVectorId")] =
            bridge.value(QStringLiteral("knownAnswerVectorId")).toString(
                QStringLiteral("%1/%2").arg(spec.vectorSet, operationName));
        callableInterface[QStringLiteral("knownAnswerFixtureId")] =
            bridge.value(QStringLiteral("knownAnswerFixtureId")).toString(
                QStringLiteral("probe-fixture/%1").arg(operationName));
        callableInterface[QStringLiteral("fixtureHashSha256")] =
            bridge.value(QStringLiteral("fixtureHashSha256")).toString(
                productionHarnessFixtureHash(spec));
        callableInterface[QStringLiteral("inputCapturePolicy")] =
            QStringLiteral("size-and-class-only");
        callableInterface[QStringLiteral("outputCapturePolicy")] =
            QStringLiteral("size-and-class-only");
        callableInterface[QStringLiteral("resultCapturePolicy")] =
            QStringLiteral("status-class-and-size-only");
        callableInterface[QStringLiteral("materialExportPolicy")] =
            QStringLiteral("sizes-and-status-only-no-secret-bytes");
        callableInterface[QStringLiteral("interfaceState")] = interfaceReady
            ? QStringLiteral("ready-for-reviewed-provider-operation-callable-interface")
            : QStringLiteral("reviewed-provider-operation-callable-interface-blocked");
        callableInterface[QStringLiteral("failClosed")] = !interfaceReady;
        callableInterface[QStringLiteral("blockedReason")] = blockedReason;
        callableInterface[QStringLiteral("operatorAction")] = interfaceReady
            ? QStringLiteral("audit-reviewed-operation-callable-interface-before-release-gate")
            : (bridgeSourceCaptured
                ? QStringLiteral("fix-reviewed-operation-callable-interface-before-audit")
                : QStringLiteral("produce-reviewed-callable-table-bridge-before-callable-interface"));
        callableInterface[QStringLiteral("interfaceNonReleaseGate")] = true;
        callableInterface[QStringLiteral("releaseGate")] =
            QStringLiteral("production-provider-reviewed-operation-callable-interface-not-release-gate");
        callableInterface[QStringLiteral("operationInvokedByInterface")] = false;
        callableInterface[QStringLiteral("inputBytesCaptured")] = false;
        callableInterface[QStringLiteral("outputBytesCaptured")] = false;
        callableInterface[QStringLiteral("resultCaptured")] = false;
        setNoKeyExportFields(callableInterface);
        callableInterface[QStringLiteral("sessionSecretExported")] = false;
        callableInterface[QStringLiteral("privateIdentityMaterialExported")] = false;
        callableInterface[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        callableInterface[QStringLiteral("sanitized")] = interfaceSanitized;
        interfaces.append(callableInterface);

        ++interfaceCount;
        if (interfaceReady) {
            ++readyInterfaceCount;
        } else {
            ++blockedInterfaceCount;
            ++failClosedInterfaceCount;
        }
        if (bridgeReady) {
            ++bridgeReadyInterfaceCount;
        }
        if (functionPointerReady) {
            ++functionPointerInterfaceCount;
        }
        if (structContractReady) {
            ++structContractInterfaceCount;
        }
        if (materialPolicyReady) {
            ++materialPolicyInterfaceCount;
        }
        if (interfaceSanitized) {
            ++sanitizedInterfaceCount;
        }
        incrementSummaryCount(&blockedReasonSummary, blockedReason);
        ++sequenceIndex;
    }

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-operation-callable-interface-v1");
    setProviderReportIdentity(status, descriptor);
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("interfaceNonReleaseGate")] = true;
    status[QStringLiteral("releaseGate")] =
        QStringLiteral("production-provider-reviewed-operation-callable-interface-not-release-gate");
    status[QStringLiteral("bridgeSourceCaptured")] = bridgeSourceCaptured;
    status[QStringLiteral("providerReviewedCallableTableBridge")] =
        reviewedCallableTableBridge;
    status[QStringLiteral("providerReviewedCallableTableBridgeReleaseGate")] =
        reviewedCallableTableBridge.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedCallableTableBridgeReadyCount")] =
        reviewedCallableTableBridge.value(QStringLiteral("readyBridgeCount")).toInt();
    status[QStringLiteral("interfaceCount")] = interfaceCount;
    status[QStringLiteral("readyInterfaceCount")] = readyInterfaceCount;
    status[QStringLiteral("blockedInterfaceCount")] = blockedInterfaceCount;
    status[QStringLiteral("bridgeReadyInterfaceCount")] = bridgeReadyInterfaceCount;
    status[QStringLiteral("functionPointerInterfaceCount")] =
        functionPointerInterfaceCount;
    status[QStringLiteral("structContractInterfaceCount")] =
        structContractInterfaceCount;
    status[QStringLiteral("materialPolicyInterfaceCount")] =
        materialPolicyInterfaceCount;
    status[QStringLiteral("sanitizedInterfaceCount")] = sanitizedInterfaceCount;
    status[QStringLiteral("failClosedInterfaceCount")] = failClosedInterfaceCount;
    status[QStringLiteral("blockedReason")] = readyInterfaceCount == interfaceCount
        && interfaceCount == cryptoOperations().size()
        ? QStringLiteral("production-provider-reviewed-operation-callable-interfaces-awaiting-audit-release-gate")
        : (bridgeSourceCaptured
            ? QStringLiteral("production-provider-reviewed-operation-callable-interface-evidence-blocked")
            : QStringLiteral("production-provider-reviewed-operation-callable-interface-awaiting-bridge"));
    status[QStringLiteral("operatorAction")] =
        QStringLiteral("promote-interface-only-after-reviewed-provider-audit-and-release-gates");
    status[QStringLiteral("blockedReasonSummary")] = blockedReasonSummary;
    status[QStringLiteral("interfaces")] = interfaces;
    status[QStringLiteral("operationInvokedByInterface")] = false;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultCaptured")] = false;
    setNoKeyExportFields(status);
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderReviewedOperationCallableInterfaceStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    return productionProviderReviewedOperationCallableInterfaceStatusFromBridge(
        descriptor,
        productionProviderReviewedCallableTableBridgeStatusForDescriptor(descriptor));
}

QString providerReviewedCallableRuntimePreflightBlockedReason(const QJsonObject& callableInterface,
                                                              bool interfaceSourceCaptured,
                                                              bool abiReady,
                                                              bool contractReady,
                                                              bool policyReady,
                                                              bool fixtureReady,
                                                              bool preflightSanitized) {
    if (!interfaceSourceCaptured) {
        return QStringLiteral("production-provider-reviewed-callable-runtime-preflight-awaiting-interface");
    }
    if (!callableInterface.value(QStringLiteral("callableInterfaceReady")).toBool(false)) {
        return callableInterface.value(QStringLiteral("blockedReason")).toString(
            QStringLiteral("production-provider-reviewed-operation-callable-interface-blocked"));
    }
    if (!abiReady) {
        return QStringLiteral("production-provider-reviewed-callable-runtime-preflight-abi-blocked");
    }
    if (!contractReady) {
        return QStringLiteral("production-provider-reviewed-callable-runtime-preflight-contract-blocked");
    }
    if (!policyReady) {
        return QStringLiteral("production-provider-reviewed-callable-runtime-preflight-policy-blocked");
    }
    if (!fixtureReady) {
        return QStringLiteral("production-provider-reviewed-callable-runtime-preflight-fixture-blocked");
    }
    if (!preflightSanitized) {
        return QStringLiteral("production-provider-reviewed-callable-runtime-preflight-sensitive-export-blocked");
    }
    return QStringLiteral("production-provider-reviewed-callable-runtime-preflights-awaiting-audit-release-gate");
}

QJsonObject productionProviderReviewedCallableRuntimePreflightStatusFromInterface(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedOperationCallableInterface) {
    const QJsonArray interfaces =
        reviewedOperationCallableInterface.value(QStringLiteral("interfaces")).toArray();
    const bool interfaceSourceCaptured =
        reviewedOperationCallableInterface.value(QStringLiteral("schema")).toString()
            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-operation-callable-interface-v1")
        && reviewedOperationCallableInterface.value(QStringLiteral("bridgeSourceCaptured")).toBool(false);

    QJsonArray preflights;
    int preflightCount = 0;
    int readyPreflightCount = 0;
    int blockedPreflightCount = 0;
    int interfaceReadyPreflightCount = 0;
    int abiPreflightCount = 0;
    int contractPreflightCount = 0;
    int policyPreflightCount = 0;
    int fixturePreflightCount = 0;
    int sanitizedPreflightCount = 0;
    int failClosedPreflightCount = 0;
    QJsonObject blockedReasonSummary;

    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QString operationName = cryptoOperationName(operation);
        const QJsonObject callableInterface = sequenceIndex < interfaces.size()
            ? interfaces.at(sequenceIndex).toObject()
            : QJsonObject();
        const QString providerSymbol = productionOperationProviderSymbol(operation);
        const QString providerTableSlot =
            QStringLiteral("qnc_e2e_provider_table_v1/%1").arg(providerSymbol);
        const bool interfaceReady =
            callableInterface.value(QStringLiteral("callableInterfaceReady")).toBool(false);
        const bool abiReady =
            callableInterface.value(QStringLiteral("providerSymbol")).toString() == providerSymbol
            && callableInterface.value(QStringLiteral("providerTableSlot")).toString()
                == providerTableSlot
            && callableInterface.value(QStringLiteral("providerAbiSignature")).toString()
                == productionOperationProviderAbiSignature(operation)
            && callableInterface.value(QStringLiteral("functionPointerTypedef")).toString()
                == QStringLiteral("qnc_e2e_provider_operation_v1")
            && callableInterface.value(QStringLiteral("inputStructAbi")).toString()
                == QStringLiteral("qnc_e2e_operation_input_v1")
            && callableInterface.value(QStringLiteral("outputStructAbi")).toString()
                == QStringLiteral("qnc_e2e_operation_output_v1");
        const bool contractReady =
            callableInterface.value(QStringLiteral("inputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && callableInterface.value(QStringLiteral("outputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && callableInterface.value(QStringLiteral("operationEnumValue")).toInt(-1)
                == sequenceIndex
            && callableInterface.value(QStringLiteral("operationEnumMatchesHeader")).toBool(false)
            && QString::fromLatin1(QNC_E2E_OPERATION_CONTRACT_VERSION)
                == descriptor.operationContractVersion;
        const bool policyReady =
            callableInterface.value(QStringLiteral("inputCapturePolicy")).toString()
                == QStringLiteral("size-and-class-only")
            && callableInterface.value(QStringLiteral("outputCapturePolicy")).toString()
                == QStringLiteral("size-and-class-only")
            && callableInterface.value(QStringLiteral("resultCapturePolicy")).toString()
                == QStringLiteral("status-class-and-size-only")
            && callableInterface.value(QStringLiteral("materialExportPolicy")).toString()
                == QStringLiteral("sizes-and-status-only-no-secret-bytes");
        const bool fixtureReady =
            callableInterface.value(QStringLiteral("knownAnswerVectorId")).toString()
                == QStringLiteral("%1/%2").arg(spec.vectorSet, operationName)
            && callableInterface.value(QStringLiteral("knownAnswerFixtureId")).toString()
                == QStringLiteral("probe-fixture/%1").arg(operationName)
            && callableInterface.value(QStringLiteral("fixtureHashSha256")).toString().size()
                == FingerprintHexLength;
        const bool preflightSanitized =
            callableInterface.value(QStringLiteral("sanitized")).toBool(false)
            && !callableInterface.value(QStringLiteral("operationInvokedByInterface")).toBool(true)
            && !callableInterface.value(QStringLiteral("inputBytesCaptured")).toBool(true)
            && !callableInterface.value(QStringLiteral("outputBytesCaptured")).toBool(true)
            && !callableInterface.value(QStringLiteral("resultCaptured")).toBool(true)
            && !callableInterface.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !callableInterface.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !callableInterface.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !callableInterface.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !callableInterface.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool preflightReady =
            interfaceSourceCaptured
            && interfaceReady
            && abiReady
            && contractReady
            && policyReady
            && fixtureReady
            && preflightSanitized;
        const QString blockedReason =
            providerReviewedCallableRuntimePreflightBlockedReason(callableInterface,
                                                                  interfaceSourceCaptured,
                                                                  abiReady,
                                                                  contractReady,
                                                                  policyReady,
                                                                  fixtureReady,
                                                                  preflightSanitized);

        QJsonObject preflight;
        preflight[QStringLiteral("sequenceIndex")] = sequenceIndex;
        preflight[QStringLiteral("operation")] = operationName;
        setProviderReportIdentity(preflight, descriptor);
        preflight[QStringLiteral("runtimePreflightId")] =
            QStringLiteral("reviewed-callable-runtime-preflight/%1/%2")
                .arg(spec.vectorSet, operationName);
        preflight[QStringLiteral("callableInterfaceId")] =
            callableInterface.value(QStringLiteral("callableInterfaceId")).toString(
                QStringLiteral("reviewed-operation-callable-interface/%1/%2")
                    .arg(spec.vectorSet, operationName));
        preflight[QStringLiteral("callableInterface")] = callableInterface;
        preflight[QStringLiteral("providerSymbol")] = providerSymbol;
        preflight[QStringLiteral("providerTableSlot")] = providerTableSlot;
        preflight[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        preflight[QStringLiteral("functionPointerTypedef")] =
            QStringLiteral("qnc_e2e_provider_operation_v1");
        preflight[QStringLiteral("inputStructAbi")] =
            QStringLiteral("qnc_e2e_operation_input_v1");
        preflight[QStringLiteral("outputStructAbi")] =
            QStringLiteral("qnc_e2e_operation_output_v1");
        preflight[QStringLiteral("statusEnumAbi")] =
            QStringLiteral("qnc_e2e_status_t");
        preflight[QStringLiteral("materialPolicyEnumAbi")] =
            QStringLiteral("qnc_e2e_material_policy_t");
        preflight[QStringLiteral("operationEnumValue")] = sequenceIndex;
        preflight[QStringLiteral("interfaceReady")] = interfaceReady;
        preflight[QStringLiteral("abiPreflightReady")] = abiReady;
        preflight[QStringLiteral("contractPreflightReady")] = contractReady;
        preflight[QStringLiteral("policyPreflightReady")] = policyReady;
        preflight[QStringLiteral("fixturePreflightReady")] = fixtureReady;
        preflight[QStringLiteral("runtimePreflightReady")] = preflightReady;
        preflight[QStringLiteral("inputContractHashSha256")] =
            callableInterface.value(QStringLiteral("inputContractHashSha256")).toString();
        preflight[QStringLiteral("outputContractHashSha256")] =
            callableInterface.value(QStringLiteral("outputContractHashSha256")).toString();
        preflight[QStringLiteral("knownAnswerVectorId")] =
            callableInterface.value(QStringLiteral("knownAnswerVectorId")).toString(
                QStringLiteral("%1/%2").arg(spec.vectorSet, operationName));
        preflight[QStringLiteral("knownAnswerFixtureId")] =
            callableInterface.value(QStringLiteral("knownAnswerFixtureId")).toString(
                QStringLiteral("probe-fixture/%1").arg(operationName));
        preflight[QStringLiteral("fixtureHashSha256")] =
            callableInterface.value(QStringLiteral("fixtureHashSha256")).toString(
                productionHarnessFixtureHash(spec));
        preflight[QStringLiteral("inputCapturePolicy")] =
            QStringLiteral("size-and-class-only");
        preflight[QStringLiteral("outputCapturePolicy")] =
            QStringLiteral("size-and-class-only");
        preflight[QStringLiteral("resultCapturePolicy")] =
            QStringLiteral("status-class-and-size-only");
        preflight[QStringLiteral("materialExportPolicy")] =
            QStringLiteral("sizes-and-status-only-no-secret-bytes");
        preflight[QStringLiteral("runtimePreflightState")] = preflightReady
            ? QStringLiteral("ready-for-reviewed-provider-runtime-preflight")
            : QStringLiteral("reviewed-provider-runtime-preflight-blocked");
        preflight[QStringLiteral("failClosed")] = !preflightReady;
        preflight[QStringLiteral("blockedReason")] = blockedReason;
        preflight[QStringLiteral("operatorAction")] = preflightReady
            ? QStringLiteral("audit-reviewed-runtime-preflight-before-release-gate")
            : (interfaceSourceCaptured
                ? QStringLiteral("fix-reviewed-runtime-preflight-before-audit")
                : QStringLiteral("produce-reviewed-callable-interface-before-runtime-preflight"));
        preflight[QStringLiteral("runtimePreflightNonReleaseGate")] = true;
        preflight[QStringLiteral("releaseGate")] =
            QStringLiteral("production-provider-reviewed-callable-runtime-preflight-not-release-gate");
        preflight[QStringLiteral("operationInvokedByRuntimePreflight")] = false;
        preflight[QStringLiteral("inputBytesCaptured")] = false;
        preflight[QStringLiteral("outputBytesCaptured")] = false;
        preflight[QStringLiteral("resultCaptured")] = false;
        setNoKeyExportFields(preflight);
        preflight[QStringLiteral("sessionSecretExported")] = false;
        preflight[QStringLiteral("privateIdentityMaterialExported")] = false;
        preflight[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        preflight[QStringLiteral("sanitized")] = preflightSanitized;
        preflights.append(preflight);

        ++preflightCount;
        if (preflightReady) {
            ++readyPreflightCount;
        } else {
            ++blockedPreflightCount;
            ++failClosedPreflightCount;
        }
        if (interfaceReady) {
            ++interfaceReadyPreflightCount;
        }
        if (abiReady) {
            ++abiPreflightCount;
        }
        if (contractReady) {
            ++contractPreflightCount;
        }
        if (policyReady) {
            ++policyPreflightCount;
        }
        if (fixtureReady) {
            ++fixturePreflightCount;
        }
        if (preflightSanitized) {
            ++sanitizedPreflightCount;
        }
        incrementSummaryCount(&blockedReasonSummary, blockedReason);
        ++sequenceIndex;
    }

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-callable-runtime-preflight-v1");
    setProviderReportIdentity(status, descriptor);
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("runtimePreflightNonReleaseGate")] = true;
    status[QStringLiteral("releaseGate")] =
        QStringLiteral("production-provider-reviewed-callable-runtime-preflight-not-release-gate");
    status[QStringLiteral("interfaceSourceCaptured")] = interfaceSourceCaptured;
    status[QStringLiteral("providerReviewedOperationCallableInterface")] =
        reviewedOperationCallableInterface;
    status[QStringLiteral("providerReviewedOperationCallableInterfaceReleaseGate")] =
        reviewedOperationCallableInterface.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedOperationCallableInterfaceReadyCount")] =
        reviewedOperationCallableInterface.value(QStringLiteral("readyInterfaceCount")).toInt();
    status[QStringLiteral("preflightCount")] = preflightCount;
    status[QStringLiteral("readyPreflightCount")] = readyPreflightCount;
    status[QStringLiteral("blockedPreflightCount")] = blockedPreflightCount;
    status[QStringLiteral("interfaceReadyPreflightCount")] =
        interfaceReadyPreflightCount;
    status[QStringLiteral("abiPreflightCount")] = abiPreflightCount;
    status[QStringLiteral("contractPreflightCount")] = contractPreflightCount;
    status[QStringLiteral("policyPreflightCount")] = policyPreflightCount;
    status[QStringLiteral("fixturePreflightCount")] = fixturePreflightCount;
    status[QStringLiteral("sanitizedPreflightCount")] = sanitizedPreflightCount;
    status[QStringLiteral("failClosedPreflightCount")] = failClosedPreflightCount;
    status[QStringLiteral("blockedReason")] = readyPreflightCount == preflightCount
        && preflightCount == cryptoOperations().size()
        ? QStringLiteral("production-provider-reviewed-callable-runtime-preflights-awaiting-audit-release-gate")
        : (interfaceSourceCaptured
            ? QStringLiteral("production-provider-reviewed-callable-runtime-preflight-evidence-blocked")
            : QStringLiteral("production-provider-reviewed-callable-runtime-preflight-awaiting-interface"));
    status[QStringLiteral("operatorAction")] =
        QStringLiteral("promote-runtime-preflight-only-after-reviewed-provider-audit-and-release-gates");
    status[QStringLiteral("blockedReasonSummary")] = blockedReasonSummary;
    status[QStringLiteral("preflights")] = preflights;
    status[QStringLiteral("operationInvokedByRuntimePreflight")] = false;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultCaptured")] = false;
    setNoKeyExportFields(status);
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderReviewedCallableRuntimePreflightStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    return productionProviderReviewedCallableRuntimePreflightStatusFromInterface(
        descriptor,
        productionProviderReviewedOperationCallableInterfaceStatusForDescriptor(descriptor));
}

QString providerReviewedInvocationArmingBlockedReason(const QJsonObject& runtimePreflight,
                                                      bool runtimePreflightSourceCaptured,
                                                      bool runtimePreflightReady,
                                                      bool callbackEntryReady,
                                                      bool sandboxPolicyReady,
                                                      bool resultPolicyReady,
                                                      bool armingSanitized) {
    if (!runtimePreflightSourceCaptured) {
        return QStringLiteral("production-provider-reviewed-invocation-arming-awaiting-runtime-preflight");
    }
    if (!runtimePreflightReady) {
        return runtimePreflight.value(QStringLiteral("blockedReason")).toString(
            QStringLiteral("production-provider-reviewed-callable-runtime-preflight-blocked"));
    }
    if (!callbackEntryReady) {
        return QStringLiteral("production-provider-reviewed-invocation-arming-callback-entry-blocked");
    }
    if (!sandboxPolicyReady) {
        return QStringLiteral("production-provider-reviewed-invocation-arming-sandbox-policy-blocked");
    }
    if (!resultPolicyReady) {
        return QStringLiteral("production-provider-reviewed-invocation-arming-result-policy-blocked");
    }
    if (!armingSanitized) {
        return QStringLiteral("production-provider-reviewed-invocation-arming-sensitive-export-blocked");
    }
    return QStringLiteral("production-provider-reviewed-invocation-armings-awaiting-audit-release-gate");
}

QJsonObject productionProviderReviewedInvocationArmingStatusFromRuntimePreflight(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedCallableRuntimePreflight) {
    const QJsonArray runtimePreflights =
        reviewedCallableRuntimePreflight.value(QStringLiteral("preflights")).toArray();
    const bool runtimePreflightSourceCaptured =
        reviewedCallableRuntimePreflight.value(QStringLiteral("schema")).toString()
            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-callable-runtime-preflight-v1")
        && reviewedCallableRuntimePreflight.value(QStringLiteral("interfaceSourceCaptured")).toBool(false);

    QJsonArray armings;
    int armingCount = 0;
    int readyArmingCount = 0;
    int blockedArmingCount = 0;
    int runtimePreflightReadyArmingCount = 0;
    int callbackEntryArmingCount = 0;
    int sandboxPolicyArmingCount = 0;
    int resultPolicyArmingCount = 0;
    int sanitizedArmingCount = 0;
    int failClosedArmingCount = 0;
    QJsonObject blockedReasonSummary;

    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QString operationName = cryptoOperationName(operation);
        const QJsonObject runtimePreflight = sequenceIndex < runtimePreflights.size()
            ? runtimePreflights.at(sequenceIndex).toObject()
            : QJsonObject();
        const QString providerSymbol = productionOperationProviderSymbol(operation);
        const QString providerTableSlot =
            QStringLiteral("qnc_e2e_provider_table_v1/%1").arg(providerSymbol);
        const bool runtimePreflightReady =
            runtimePreflight.value(QStringLiteral("runtimePreflightReady")).toBool(false);
        const bool callbackEntryReady =
            runtimePreflight.value(QStringLiteral("providerSymbol")).toString() == providerSymbol
            && runtimePreflight.value(QStringLiteral("providerTableSlot")).toString()
                == providerTableSlot
            && runtimePreflight.value(QStringLiteral("functionPointerTypedef")).toString()
                == QStringLiteral("qnc_e2e_provider_operation_v1")
            && runtimePreflight.value(QStringLiteral("operationEnumValue")).toInt(-1)
                == sequenceIndex;
        const bool sandboxPolicyReady =
            runtimePreflight.value(QStringLiteral("inputCapturePolicy")).toString()
                == QStringLiteral("size-and-class-only")
            && runtimePreflight.value(QStringLiteral("outputCapturePolicy")).toString()
                == QStringLiteral("size-and-class-only")
            && runtimePreflight.value(QStringLiteral("materialExportPolicy")).toString()
                == QStringLiteral("sizes-and-status-only-no-secret-bytes");
        const bool resultPolicyReady =
            runtimePreflight.value(QStringLiteral("resultCapturePolicy")).toString()
                == QStringLiteral("status-class-and-size-only")
            && runtimePreflight.value(QStringLiteral("inputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && runtimePreflight.value(QStringLiteral("outputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && runtimePreflight.value(QStringLiteral("fixtureHashSha256")).toString().size()
                == FingerprintHexLength;
        const bool armingSanitized =
            runtimePreflight.value(QStringLiteral("sanitized")).toBool(false)
            && !runtimePreflight.value(QStringLiteral("operationInvokedByRuntimePreflight")).toBool(true)
            && !runtimePreflight.value(QStringLiteral("inputBytesCaptured")).toBool(true)
            && !runtimePreflight.value(QStringLiteral("outputBytesCaptured")).toBool(true)
            && !runtimePreflight.value(QStringLiteral("resultCaptured")).toBool(true)
            && !runtimePreflight.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !runtimePreflight.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !runtimePreflight.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !runtimePreflight.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !runtimePreflight.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool armingReady =
            runtimePreflightSourceCaptured
            && runtimePreflightReady
            && callbackEntryReady
            && sandboxPolicyReady
            && resultPolicyReady
            && armingSanitized;
        const QString blockedReason =
            providerReviewedInvocationArmingBlockedReason(runtimePreflight,
                                                          runtimePreflightSourceCaptured,
                                                          runtimePreflightReady,
                                                          callbackEntryReady,
                                                          sandboxPolicyReady,
                                                          resultPolicyReady,
                                                          armingSanitized);

        QJsonObject arming;
        arming[QStringLiteral("sequenceIndex")] = sequenceIndex;
        arming[QStringLiteral("operation")] = operationName;
        setProviderReportIdentity(arming, descriptor);
        arming[QStringLiteral("invocationArmingId")] =
            QStringLiteral("reviewed-invocation-arming/%1/%2")
                .arg(spec.vectorSet, operationName);
        arming[QStringLiteral("runtimePreflightId")] =
            runtimePreflight.value(QStringLiteral("runtimePreflightId")).toString(
                QStringLiteral("reviewed-callable-runtime-preflight/%1/%2")
                    .arg(spec.vectorSet, operationName));
        arming[QStringLiteral("runtimePreflight")] = runtimePreflight;
        arming[QStringLiteral("providerSymbol")] = providerSymbol;
        arming[QStringLiteral("providerTableSlot")] = providerTableSlot;
        arming[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        arming[QStringLiteral("functionPointerTypedef")] =
            QStringLiteral("qnc_e2e_provider_operation_v1");
        arming[QStringLiteral("operationEnumValue")] = sequenceIndex;
        arming[QStringLiteral("callbackEntrypoint")] = providerTableSlot;
        arming[QStringLiteral("armingTokenId")] =
            QStringLiteral("reviewed-invocation-arming-token/%1/%2")
                .arg(spec.vectorSet, operationName);
        arming[QStringLiteral("runtimePreflightReady")] = runtimePreflightReady;
        arming[QStringLiteral("callbackEntryReady")] = callbackEntryReady;
        arming[QStringLiteral("sandboxPolicyReady")] = sandboxPolicyReady;
        arming[QStringLiteral("resultPolicyReady")] = resultPolicyReady;
        arming[QStringLiteral("invocationArmingReady")] = armingReady;
        arming[QStringLiteral("inputContractHashSha256")] =
            runtimePreflight.value(QStringLiteral("inputContractHashSha256")).toString();
        arming[QStringLiteral("outputContractHashSha256")] =
            runtimePreflight.value(QStringLiteral("outputContractHashSha256")).toString();
        arming[QStringLiteral("knownAnswerVectorId")] =
            runtimePreflight.value(QStringLiteral("knownAnswerVectorId")).toString(
                QStringLiteral("%1/%2").arg(spec.vectorSet, operationName));
        arming[QStringLiteral("knownAnswerFixtureId")] =
            runtimePreflight.value(QStringLiteral("knownAnswerFixtureId")).toString(
                QStringLiteral("probe-fixture/%1").arg(operationName));
        arming[QStringLiteral("fixtureHashSha256")] =
            runtimePreflight.value(QStringLiteral("fixtureHashSha256")).toString(
                productionHarnessFixtureHash(spec));
        arming[QStringLiteral("inputCapturePolicy")] =
            QStringLiteral("size-and-class-only");
        arming[QStringLiteral("outputCapturePolicy")] =
            QStringLiteral("size-and-class-only");
        arming[QStringLiteral("resultCapturePolicy")] =
            QStringLiteral("status-class-and-size-only");
        arming[QStringLiteral("materialExportPolicy")] =
            QStringLiteral("sizes-and-status-only-no-secret-bytes");
        arming[QStringLiteral("sandboxPolicy")] =
            QStringLiteral("reviewed-provider-call-boundary-no-secret-capture");
        arming[QStringLiteral("resultPolicy")] =
            QStringLiteral("status-class-and-size-only-no-secret-bytes");
        arming[QStringLiteral("armingState")] = armingReady
            ? QStringLiteral("ready-for-reviewed-provider-invocation-arming")
            : QStringLiteral("reviewed-provider-invocation-arming-blocked");
        arming[QStringLiteral("failClosed")] = !armingReady;
        arming[QStringLiteral("blockedReason")] = blockedReason;
        arming[QStringLiteral("operatorAction")] = armingReady
            ? QStringLiteral("audit-reviewed-invocation-arming-before-release-gate")
            : (runtimePreflightSourceCaptured
                ? QStringLiteral("fix-reviewed-invocation-arming-before-audit")
                : QStringLiteral("produce-reviewed-runtime-preflight-before-invocation-arming"));
        arming[QStringLiteral("invocationArmingNonReleaseGate")] = true;
        arming[QStringLiteral("releaseGate")] =
            QStringLiteral("production-provider-reviewed-invocation-arming-not-release-gate");
        arming[QStringLiteral("operationInvokedByArming")] = false;
        arming[QStringLiteral("inputBytesCaptured")] = false;
        arming[QStringLiteral("outputBytesCaptured")] = false;
        arming[QStringLiteral("resultCaptured")] = false;
        setNoKeyExportFields(arming);
        arming[QStringLiteral("sessionSecretExported")] = false;
        arming[QStringLiteral("privateIdentityMaterialExported")] = false;
        arming[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        arming[QStringLiteral("sanitized")] = armingSanitized;
        armings.append(arming);

        ++armingCount;
        if (armingReady) {
            ++readyArmingCount;
        } else {
            ++blockedArmingCount;
            ++failClosedArmingCount;
        }
        if (runtimePreflightReady) {
            ++runtimePreflightReadyArmingCount;
        }
        if (callbackEntryReady) {
            ++callbackEntryArmingCount;
        }
        if (sandboxPolicyReady) {
            ++sandboxPolicyArmingCount;
        }
        if (resultPolicyReady) {
            ++resultPolicyArmingCount;
        }
        if (armingSanitized) {
            ++sanitizedArmingCount;
        }
        incrementSummaryCount(&blockedReasonSummary, blockedReason);
        ++sequenceIndex;
    }

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-invocation-arming-v1");
    setProviderReportIdentity(status, descriptor);
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("invocationArmingNonReleaseGate")] = true;
    status[QStringLiteral("releaseGate")] =
        QStringLiteral("production-provider-reviewed-invocation-arming-not-release-gate");
    status[QStringLiteral("runtimePreflightSourceCaptured")] =
        runtimePreflightSourceCaptured;
    status[QStringLiteral("providerReviewedCallableRuntimePreflight")] =
        reviewedCallableRuntimePreflight;
    status[QStringLiteral("providerReviewedCallableRuntimePreflightReleaseGate")] =
        reviewedCallableRuntimePreflight.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedCallableRuntimePreflightReadyCount")] =
        reviewedCallableRuntimePreflight.value(QStringLiteral("readyPreflightCount")).toInt();
    status[QStringLiteral("armingCount")] = armingCount;
    status[QStringLiteral("readyArmingCount")] = readyArmingCount;
    status[QStringLiteral("blockedArmingCount")] = blockedArmingCount;
    status[QStringLiteral("runtimePreflightReadyArmingCount")] =
        runtimePreflightReadyArmingCount;
    status[QStringLiteral("callbackEntryArmingCount")] = callbackEntryArmingCount;
    status[QStringLiteral("sandboxPolicyArmingCount")] = sandboxPolicyArmingCount;
    status[QStringLiteral("resultPolicyArmingCount")] = resultPolicyArmingCount;
    status[QStringLiteral("sanitizedArmingCount")] = sanitizedArmingCount;
    status[QStringLiteral("failClosedArmingCount")] = failClosedArmingCount;
    status[QStringLiteral("blockedReason")] = readyArmingCount == armingCount
        && armingCount == cryptoOperations().size()
        ? QStringLiteral("production-provider-reviewed-invocation-armings-awaiting-audit-release-gate")
        : (runtimePreflightSourceCaptured
            ? QStringLiteral("production-provider-reviewed-invocation-arming-evidence-blocked")
            : QStringLiteral("production-provider-reviewed-invocation-arming-awaiting-runtime-preflight"));
    status[QStringLiteral("operatorAction")] =
        QStringLiteral("promote-invocation-arming-only-after-reviewed-provider-audit-and-release-gates");
    status[QStringLiteral("blockedReasonSummary")] = blockedReasonSummary;
    status[QStringLiteral("armings")] = armings;
    status[QStringLiteral("operationInvokedByArming")] = false;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultCaptured")] = false;
    setNoKeyExportFields(status);
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderReviewedInvocationArmingStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    return productionProviderReviewedInvocationArmingStatusFromRuntimePreflight(
        descriptor,
        productionProviderReviewedCallableRuntimePreflightStatusForDescriptor(descriptor));
}

QString providerReviewedInvocationExecutionAcceptanceBlockedReason(const QJsonObject& arming,
                                                                   bool armingSourceCaptured,
                                                                   bool armingReady,
                                                                   bool executionContractReady,
                                                                   bool vectorEvidenceReady,
                                                                   bool resultPolicyReady,
                                                                   bool acceptanceSanitized) {
    if (!armingSourceCaptured) {
        return QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-awaiting-arming");
    }
    if (!armingReady) {
        return arming.value(QStringLiteral("blockedReason")).toString(
            QStringLiteral("production-provider-reviewed-invocation-arming-blocked"));
    }
    if (!executionContractReady) {
        return QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-contract-blocked");
    }
    if (!vectorEvidenceReady) {
        return QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-vector-blocked");
    }
    if (!resultPolicyReady) {
        return QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-result-policy-blocked");
    }
    if (!acceptanceSanitized) {
        return QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-sensitive-export-blocked");
    }
    return QStringLiteral("production-provider-reviewed-invocation-execution-acceptances-awaiting-audit-release-gate");
}

QJsonObject productionProviderReviewedInvocationExecutionAcceptanceStatusFromArming(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedInvocationArming) {
    const QJsonArray armings =
        reviewedInvocationArming.value(QStringLiteral("armings")).toArray();
    const bool armingSourceCaptured =
        reviewedInvocationArming.value(QStringLiteral("schema")).toString()
            == QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-invocation-arming-v1")
        && reviewedInvocationArming.value(QStringLiteral("runtimePreflightSourceCaptured")).toBool(false);

    QJsonArray acceptances;
    int acceptanceCount = 0;
    int readyAcceptanceCount = 0;
    int blockedAcceptanceCount = 0;
    int armingReadyAcceptanceCount = 0;
    int executionContractAcceptanceCount = 0;
    int vectorEvidenceAcceptanceCount = 0;
    int resultPolicyAcceptanceCount = 0;
    int sanitizedAcceptanceCount = 0;
    int failClosedAcceptanceCount = 0;
    QJsonObject blockedReasonSummary;

    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QString operationName = cryptoOperationName(operation);
        const QJsonObject arming = sequenceIndex < armings.size()
            ? armings.at(sequenceIndex).toObject()
            : QJsonObject();
        const QString providerSymbol = productionOperationProviderSymbol(operation);
        const QString providerTableSlot =
            QStringLiteral("qnc_e2e_provider_table_v1/%1").arg(providerSymbol);
        const bool armingReady =
            arming.value(QStringLiteral("invocationArmingReady")).toBool(false);
        const bool executionContractReady =
            arming.value(QStringLiteral("providerSymbol")).toString() == providerSymbol
            && arming.value(QStringLiteral("providerTableSlot")).toString() == providerTableSlot
            && arming.value(QStringLiteral("providerAbiSignature")).toString()
                == productionOperationProviderAbiSignature(operation)
            && arming.value(QStringLiteral("inputContractHashSha256")).toString().size()
                == FingerprintHexLength
            && arming.value(QStringLiteral("outputContractHashSha256")).toString().size()
                == FingerprintHexLength;
        const bool vectorEvidenceReady =
            arming.value(QStringLiteral("knownAnswerVectorId")).toString()
                == QStringLiteral("%1/%2").arg(spec.vectorSet, operationName)
            && arming.value(QStringLiteral("knownAnswerFixtureId")).toString()
                == QStringLiteral("probe-fixture/%1").arg(operationName)
            && arming.value(QStringLiteral("fixtureHashSha256")).toString().size()
                == FingerprintHexLength;
        const bool resultPolicyReady =
            arming.value(QStringLiteral("resultPolicy")).toString()
                == QStringLiteral("status-class-and-size-only-no-secret-bytes")
            && arming.value(QStringLiteral("resultCapturePolicy")).toString()
                == QStringLiteral("status-class-and-size-only")
            && arming.value(QStringLiteral("materialExportPolicy")).toString()
                == QStringLiteral("sizes-and-status-only-no-secret-bytes");
        const bool acceptanceSanitized =
            arming.value(QStringLiteral("sanitized")).toBool(false)
            && !arming.value(QStringLiteral("operationInvokedByArming")).toBool(true)
            && !arming.value(QStringLiteral("inputBytesCaptured")).toBool(true)
            && !arming.value(QStringLiteral("outputBytesCaptured")).toBool(true)
            && !arming.value(QStringLiteral("resultCaptured")).toBool(true)
            && !arming.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !arming.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !arming.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !arming.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !arming.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool acceptanceReady =
            armingSourceCaptured
            && armingReady
            && executionContractReady
            && vectorEvidenceReady
            && resultPolicyReady
            && acceptanceSanitized;
        const QString blockedReason =
            providerReviewedInvocationExecutionAcceptanceBlockedReason(arming,
                                                                       armingSourceCaptured,
                                                                       armingReady,
                                                                       executionContractReady,
                                                                       vectorEvidenceReady,
                                                                       resultPolicyReady,
                                                                       acceptanceSanitized);

        QJsonObject acceptance;
        acceptance[QStringLiteral("sequenceIndex")] = sequenceIndex;
        acceptance[QStringLiteral("operation")] = operationName;
        setProviderReportIdentity(acceptance, descriptor);
        acceptance[QStringLiteral("executionAcceptanceId")] =
            QStringLiteral("reviewed-invocation-execution-acceptance/%1/%2")
                .arg(spec.vectorSet, operationName);
        acceptance[QStringLiteral("invocationArmingId")] =
            arming.value(QStringLiteral("invocationArmingId")).toString(
                QStringLiteral("reviewed-invocation-arming/%1/%2")
                    .arg(spec.vectorSet, operationName));
        acceptance[QStringLiteral("invocationArming")] = arming;
        acceptance[QStringLiteral("providerSymbol")] = providerSymbol;
        acceptance[QStringLiteral("providerTableSlot")] = providerTableSlot;
        acceptance[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        acceptance[QStringLiteral("callbackEntrypoint")] = providerTableSlot;
        acceptance[QStringLiteral("armingTokenId")] =
            arming.value(QStringLiteral("armingTokenId")).toString(
                QStringLiteral("reviewed-invocation-arming-token/%1/%2")
                    .arg(spec.vectorSet, operationName));
        acceptance[QStringLiteral("executionAcceptanceTokenId")] =
            QStringLiteral("reviewed-invocation-execution-acceptance-token/%1/%2")
                .arg(spec.vectorSet, operationName);
        acceptance[QStringLiteral("armingReady")] = armingReady;
        acceptance[QStringLiteral("executionContractReady")] =
            executionContractReady;
        acceptance[QStringLiteral("vectorEvidenceReady")] = vectorEvidenceReady;
        acceptance[QStringLiteral("resultPolicyReady")] = resultPolicyReady;
        acceptance[QStringLiteral("executionAcceptanceReady")] = acceptanceReady;
        acceptance[QStringLiteral("inputContractHashSha256")] =
            arming.value(QStringLiteral("inputContractHashSha256")).toString();
        acceptance[QStringLiteral("outputContractHashSha256")] =
            arming.value(QStringLiteral("outputContractHashSha256")).toString();
        acceptance[QStringLiteral("knownAnswerVectorId")] =
            arming.value(QStringLiteral("knownAnswerVectorId")).toString(
                QStringLiteral("%1/%2").arg(spec.vectorSet, operationName));
        acceptance[QStringLiteral("knownAnswerFixtureId")] =
            arming.value(QStringLiteral("knownAnswerFixtureId")).toString(
                QStringLiteral("probe-fixture/%1").arg(operationName));
        acceptance[QStringLiteral("fixtureHashSha256")] =
            arming.value(QStringLiteral("fixtureHashSha256")).toString(
                productionHarnessFixtureHash(spec));
        acceptance[QStringLiteral("resultCapturePolicy")] =
            QStringLiteral("status-class-and-size-only");
        acceptance[QStringLiteral("materialExportPolicy")] =
            QStringLiteral("sizes-and-status-only-no-secret-bytes");
        acceptance[QStringLiteral("expectedVectorResultClass")] =
            QStringLiteral("probe-vector-passed");
        acceptance[QStringLiteral("expectedFailureClass")] = QStringLiteral("none");
        acceptance[QStringLiteral("expectedStatusClass")] = QStringLiteral("ok");
        acceptance[QStringLiteral("executionAcceptanceState")] = acceptanceReady
            ? QStringLiteral("ready-for-reviewed-provider-invocation-execution-acceptance")
            : QStringLiteral("reviewed-provider-invocation-execution-acceptance-blocked");
        acceptance[QStringLiteral("failClosed")] = !acceptanceReady;
        acceptance[QStringLiteral("blockedReason")] = blockedReason;
        acceptance[QStringLiteral("operatorAction")] = acceptanceReady
            ? QStringLiteral("audit-reviewed-invocation-execution-acceptance-before-release-gate")
            : (armingSourceCaptured
                ? QStringLiteral("fix-reviewed-invocation-execution-acceptance-before-audit")
                : QStringLiteral("produce-reviewed-invocation-arming-before-execution-acceptance"));
        acceptance[QStringLiteral("executionAcceptanceNonReleaseGate")] = true;
        acceptance[QStringLiteral("releaseGate")] =
            QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-not-release-gate");
        acceptance[QStringLiteral("operationInvokedByExecutionAcceptance")] = false;
        acceptance[QStringLiteral("inputBytesCaptured")] = false;
        acceptance[QStringLiteral("outputBytesCaptured")] = false;
        acceptance[QStringLiteral("resultCaptured")] = false;
        setNoKeyExportFields(acceptance);
        acceptance[QStringLiteral("sessionSecretExported")] = false;
        acceptance[QStringLiteral("privateIdentityMaterialExported")] = false;
        acceptance[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        acceptance[QStringLiteral("sanitized")] = acceptanceSanitized;
        acceptances.append(acceptance);

        ++acceptanceCount;
        if (acceptanceReady) {
            ++readyAcceptanceCount;
        } else {
            ++blockedAcceptanceCount;
            ++failClosedAcceptanceCount;
        }
        if (armingReady) {
            ++armingReadyAcceptanceCount;
        }
        if (executionContractReady) {
            ++executionContractAcceptanceCount;
        }
        if (vectorEvidenceReady) {
            ++vectorEvidenceAcceptanceCount;
        }
        if (resultPolicyReady) {
            ++resultPolicyAcceptanceCount;
        }
        if (acceptanceSanitized) {
            ++sanitizedAcceptanceCount;
        }
        incrementSummaryCount(&blockedReasonSummary, blockedReason);
        ++sequenceIndex;
    }

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-reviewed-invocation-execution-acceptance-v1");
    setProviderReportIdentity(status, descriptor);
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = false;
    status[QStringLiteral("executionAcceptanceNonReleaseGate")] = true;
    status[QStringLiteral("releaseGate")] =
        QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-not-release-gate");
    status[QStringLiteral("armingSourceCaptured")] = armingSourceCaptured;
    status[QStringLiteral("providerReviewedInvocationArming")] =
        reviewedInvocationArming;
    status[QStringLiteral("providerReviewedInvocationArmingReleaseGate")] =
        reviewedInvocationArming.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerReviewedInvocationArmingReadyCount")] =
        reviewedInvocationArming.value(QStringLiteral("readyArmingCount")).toInt();
    status[QStringLiteral("acceptanceCount")] = acceptanceCount;
    status[QStringLiteral("readyAcceptanceCount")] = readyAcceptanceCount;
    status[QStringLiteral("blockedAcceptanceCount")] = blockedAcceptanceCount;
    status[QStringLiteral("armingReadyAcceptanceCount")] =
        armingReadyAcceptanceCount;
    status[QStringLiteral("executionContractAcceptanceCount")] =
        executionContractAcceptanceCount;
    status[QStringLiteral("vectorEvidenceAcceptanceCount")] =
        vectorEvidenceAcceptanceCount;
    status[QStringLiteral("resultPolicyAcceptanceCount")] =
        resultPolicyAcceptanceCount;
    status[QStringLiteral("sanitizedAcceptanceCount")] = sanitizedAcceptanceCount;
    status[QStringLiteral("failClosedAcceptanceCount")] =
        failClosedAcceptanceCount;
    status[QStringLiteral("blockedReason")] = readyAcceptanceCount == acceptanceCount
        && acceptanceCount == cryptoOperations().size()
        ? QStringLiteral("production-provider-reviewed-invocation-execution-acceptances-awaiting-audit-release-gate")
        : (armingSourceCaptured
            ? QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-evidence-blocked")
            : QStringLiteral("production-provider-reviewed-invocation-execution-acceptance-awaiting-arming"));
    status[QStringLiteral("operatorAction")] =
        QStringLiteral("promote-execution-acceptance-only-after-reviewed-provider-audit-and-release-gates");
    status[QStringLiteral("blockedReasonSummary")] = blockedReasonSummary;
    status[QStringLiteral("acceptances")] = acceptances;
    status[QStringLiteral("operationInvokedByExecutionAcceptance")] = false;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultCaptured")] = false;
    setNoKeyExportFields(status);
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderReviewedInvocationExecutionAcceptanceStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor) {
    return productionProviderReviewedInvocationExecutionAcceptanceStatusFromArming(
        descriptor,
        productionProviderReviewedInvocationArmingStatusForDescriptor(descriptor));
}
} // namespace E2EBackendStatus
