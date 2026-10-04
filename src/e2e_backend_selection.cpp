#include "e2e_backend_status_p.h"
#include <QCryptographicHash>
#include <QHash>
#include <cstddef>

namespace E2EBackendStatus {
const qnc_e2e_provider_table_v1* g_registeredProductionProviderTable = nullptr;
QJsonObject g_cachedBackendStatus;
QString g_cachedBackendStatusKey;

bool productionProviderTableRegistered() {
    return g_registeredProductionProviderTable != nullptr;
}

void invalidateBackendStatusCache() {
    g_cachedBackendStatus = QJsonObject();
    g_cachedBackendStatusKey.clear();
}

const qnc_e2e_provider_table_v1* builtInProductionProviderTable() {
#if QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_SESSION_KEY_GENERATION != 0
    return qnc_e2e_openssl_provider_table_v1();
#else
    return nullptr;
#endif
}

const qnc_e2e_provider_table_v1* activeProductionProviderTable() {
    return g_registeredProductionProviderTable
        ? g_registeredProductionProviderTable
        : builtInProductionProviderTable();
}

bool usingBuiltInProductionProviderTable() {
    return g_registeredProductionProviderTable == nullptr
        && builtInProductionProviderTable() != nullptr;
}

bool envEnabled(const char* name) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

QString requestedBackendId(QString* source) {
    const QString envValue = sanitizedBackendId(QString::fromUtf8(qgetenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND")));
    if (!envValue.isEmpty()) {
        if (source) *source = QStringLiteral("environment");
        if (envValue == QStringLiteral("production") || envValue == QStringLiteral("openssl")) {
            return QString::fromLatin1(ProductionBackendId);
        }
        return envValue;
    }
    if (source) *source = QStringLiteral("compiled-default");
    return e2eCryptoBackendId();
}

QString backendStatusCacheKey() {
    QString source;
    const QString requested = requestedBackendId(&source);
    return QStringLiteral("%1|%2|%3|%4")
        .arg(requested,
             source,
             e2eProductionCryptoRequired() ? QStringLiteral("required") : QStringLiteral("optional"),
             QString::number(reinterpret_cast<quintptr>(activeProductionProviderTable())));
}

E2ECryptoAdapterDescriptor draftAdapterDescriptor() {
    E2ECryptoAdapterDescriptor descriptor;
    descriptor.id = QString::fromLatin1(DraftBackendId);
    descriptor.type = QStringLiteral("draft");
    descriptor.implementation = QStringLiteral("draft-qt-primitives");
    descriptor.providerId = QStringLiteral("draft-qt-provider-v1");
    descriptor.operationContractVersion = QStringLiteral("qtnetworkchat-e2e-crypto-ops-v1");
    descriptor.dispatchState = QStringLiteral("draft-dispatch-ready");
    descriptor.selfTestStatus = QStringLiteral("development-only-self-test-passed");
    descriptor.readinessGate = QStringLiteral("draft-provider-not-production");
    descriptor.compatibilityStatus = QStringLiteral("development-known-answer-passed");
    descriptor.compatibilityGate = QStringLiteral("draft-provider-not-production");
    descriptor.unavailableReason = e2eProductionCryptoRequired()
        ? QStringLiteral("production-crypto-backend-unavailable")
        : QStringLiteral("draft-backend-available");
    descriptor.operatorAction = e2eProductionCryptoRequired()
        ? QStringLiteral("link-production-crypto-backend-or-disable-requirement")
        : QStringLiteral("allowed-for-development-and-tests");
    descriptor.linked = true;
    descriptor.operations = cryptoOperations();
    return descriptor;
}

E2ECryptoAdapterDescriptor productionAdapterDescriptor() {
    E2ECryptoAdapterDescriptor descriptor;
    const bool adapterLinked = QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED != 0;
    const bool reviewedOperationsBound = adapterLinked && allProductionProviderOperationsBound();
    QString runtimeReadyReason;
    const bool runtimeReady = productionProviderRuntimeReady(&runtimeReadyReason);
    descriptor.id = QString::fromLatin1(ProductionBackendId);
    descriptor.type = QStringLiteral("production-adapter");
    descriptor.implementation = QStringLiteral("production-adapter");
    descriptor.providerId = QStringLiteral("openssl-reviewed-provider-v1");
    descriptor.operationContractVersion = QStringLiteral("qtnetworkchat-e2e-crypto-ops-v1");
    descriptor.dispatchState = runtimeReady
        ? QStringLiteral("production-dispatch-ready")
        : (reviewedOperationsBound
        ? QStringLiteral("reviewed-operations-bound-not-ready")
        : (adapterLinked ? QStringLiteral("linked-placeholder-not-ready")
                         : QStringLiteral("not-linked")));
    descriptor.selfTestStatus = runtimeReady
        ? QStringLiteral("production-self-test-passed")
        : (reviewedOperationsBound
        ? QStringLiteral("self-test-blocked-acceptance-gates")
        : (adapterLinked ? QStringLiteral("self-test-blocked-placeholder")
                         : QStringLiteral("self-test-blocked-not-linked")));
    descriptor.readinessGate = runtimeReady
        ? QString()
        : (reviewedOperationsBound
        ? QStringLiteral("production-acceptance-gates-not-open")
        : (adapterLinked ? QStringLiteral("production-operations-not-implemented")
                         : QStringLiteral("production-adapter-not-linked")));
    descriptor.compatibilityStatus = runtimeReady
        ? QStringLiteral("production-compatibility-passed")
        : (reviewedOperationsBound
        ? QStringLiteral("compatibility-shape-passed-acceptance-blocked")
        : (adapterLinked ? QStringLiteral("compatibility-blocked-placeholder")
                         : QStringLiteral("compatibility-blocked-not-linked")));
    descriptor.compatibilityGate = runtimeReady
        ? QString()
        : (reviewedOperationsBound
        ? QStringLiteral("production-invocation-results-not-accepted")
        : (adapterLinked ? QStringLiteral("production-operation-vectors-not-implemented")
                         : QStringLiteral("production-adapter-not-linked")));
    descriptor.unavailableReason = runtimeReady
        ? QString()
        : (runtimeReadyReason.isEmpty()
            ? QStringLiteral("production-crypto-backend-unavailable")
            : runtimeReadyReason);
    descriptor.operatorAction = runtimeReady
        ? QStringLiteral("none")
        : (reviewedOperationsBound
        ? QStringLiteral("capture-reviewed-provider-results-and-open-production-acceptance-gates")
        : (adapterLinked ? QStringLiteral("run-production-crypto-compatibility-tests")
                         : QStringLiteral("link-reviewed-production-crypto-backend")));
    descriptor.productionReady = runtimeReady;
    descriptor.linked = adapterLinked;
    descriptor.operations = cryptoOperations();
    return descriptor;
}

QList<E2ECryptoAdapterDescriptor> cryptoAdapterRegistry() {
    return {
        draftAdapterDescriptor(),
        productionAdapterDescriptor(),
    };
}

E2ECryptoAdapterDescriptor cryptoAdapterForBackend(const QString& requested, bool* found) {
    for (const E2ECryptoAdapterDescriptor& descriptor : cryptoAdapterRegistry()) {
        if (descriptor.id == requested) {
            if (found) {
                *found = true;
            }
            return descriptor;
        }
    }
    if (found) {
        *found = false;
    }
    E2ECryptoAdapterDescriptor descriptor;
    descriptor.id = requested;
    descriptor.type = QStringLiteral("unsupported");
    descriptor.implementation = QStringLiteral("none");
    descriptor.providerId = QStringLiteral("none");
    descriptor.operationContractVersion = QStringLiteral("qtnetworkchat-e2e-crypto-ops-v1");
    descriptor.dispatchState = QStringLiteral("unsupported-backend");
    descriptor.selfTestStatus = QStringLiteral("self-test-unavailable");
    descriptor.readinessGate = QStringLiteral("unsupported-backend");
    descriptor.compatibilityStatus = QStringLiteral("compatibility-unavailable");
    descriptor.compatibilityGate = QStringLiteral("unsupported-backend");
    descriptor.unavailableReason = QStringLiteral("unsupported-crypto-backend");
    descriptor.operatorAction = QStringLiteral("choose-a-registered-crypto-backend");
    return descriptor;
}

bool adapterSupportsOperation(const E2ECryptoAdapterDescriptor& descriptor,
                              E2ECryptoOperation operation) {
    return descriptor.operations.contains(operation);
}

bool providerCanDispatchOperation(const E2ECryptoAdapterDescriptor& descriptor,
                                  E2ECryptoOperation operation,
                                  bool productionRequired,
                                  QString* reason) {
    if (!adapterSupportsOperation(descriptor, operation)) {
        return fail(reason, QStringLiteral("operation-not-registered"));
    }
    if (descriptor.id == QString::fromLatin1(DraftBackendId)) {
        if (productionRequired) {
            return fail(reason, QStringLiteral("production-crypto-backend-unavailable"));
        }
        if (reason) reason->clear();
        return true;
    }
    if (descriptor.id == QString::fromLatin1(ProductionBackendId)) {
        if (!descriptor.linked) {
            return fail(reason, QStringLiteral("production-crypto-backend-unavailable"));
        }
        if (!descriptor.productionReady) {
            return fail(reason, QStringLiteral("production-adapter-not-ready"));
        }
        if (reason) reason->clear();
        return true;
    }
    return fail(reason, QStringLiteral("unsupported-crypto-backend"));
}

QStringList cryptoOperationNames() {
    QStringList names;
    for (const E2ECryptoOperation operation : cryptoOperations()) {
        names.append(cryptoOperationName(operation));
    }
    return names;
}

E2ECryptoExecutionContext cryptoExecutionContext(E2ECryptoOperation operation,
                                                 const QString& requested,
                                                 bool productionRequired) {
    bool found = false;
    const E2ECryptoAdapterDescriptor descriptor = cryptoAdapterForBackend(requested, &found);
    const bool draftSelected = requested == QString::fromLatin1(DraftBackendId);
    const bool productionSelected = requested == QString::fromLatin1(ProductionBackendId);
    const bool operationRegistered = found && adapterSupportsOperation(descriptor, operation);
    QString dispatchReason;
    const bool operationAvailable = found
        && providerCanDispatchOperation(descriptor, operation, productionRequired, &dispatchReason);

    E2ECryptoExecutionContext context;
    context.descriptor = descriptor;
    context.operation = operation;
    context.backendFound = found;
    context.operationRegistered = operationRegistered;
    context.available = operationAvailable;
    context.entrypoint = descriptor.type + QStringLiteral("/") + cryptoOperationName(operation);

    if (operationAvailable) {
        context.reason = draftSelected
            ? QStringLiteral("draft-backend-available")
            : QStringLiteral("production-backend-available");
        return context;
    }

    if (!dispatchReason.isEmpty()) {
        context.reason = dispatchReason;
        return context;
    }

    context.reason = productionSelected
        ? (descriptor.linked
            ? QStringLiteral("production-adapter-not-ready")
            : QStringLiteral("production-crypto-backend-unavailable"))
        : QStringLiteral("unsupported-crypto-backend");
    return context;
}

QJsonObject cryptoOperationStatus(E2ECryptoOperation operation,
                                  const QString& requested,
                                  bool productionRequired) {
    const E2ECryptoExecutionContext context = cryptoExecutionContext(operation, requested, productionRequired);
    const E2ECryptoAdapterDescriptor descriptor = context.descriptor;
    const bool draftSelected = requested == QString::fromLatin1(DraftBackendId);
    const bool productionSelected = requested == QString::fromLatin1(ProductionBackendId);

    QJsonObject obj;
    obj["operation"] = cryptoOperationName(operation);
    obj["backendId"] = requested;
    obj["adapterType"] = descriptor.type;
    obj["implementation"] = descriptor.implementation;
    obj["providerId"] = descriptor.providerId;
    obj["operationContractVersion"] = descriptor.operationContractVersion;
    obj["dispatchState"] = descriptor.dispatchState;
    obj["providerSelfTestStatus"] = descriptor.selfTestStatus;
    obj["providerReadinessGate"] = descriptor.readinessGate;
    obj["providerCompatibilityStatus"] = descriptor.compatibilityStatus;
    obj["providerCompatibilityGate"] = descriptor.compatibilityGate;
    obj["entrypoint"] = context.entrypoint;
    obj["registered"] = context.operationRegistered;
    obj["adapterLinked"] = descriptor.linked;
    obj["rawKeyExported"] = descriptor.rawKeyExported;
    obj["privateMaterialExported"] = descriptor.privateMaterialExported;
    obj["productionReady"] = descriptor.productionReady;
    obj["requiresProductionReady"] = productionSelected;
    obj["available"] = context.available;
    obj["blockedReason"] = context.available ? QString() : context.reason;
    if (productionSelected) {
        obj["operationImplementation"] =
            productionOperationSpecJson(productionOperationSpec(operation));
    }

    if (draftSelected) {
        obj["reason"] = context.reason;
        obj["operatorAction"] = productionRequired
            ? QStringLiteral("link-production-crypto-backend-or-disable-requirement")
            : QStringLiteral("allowed-for-development-and-tests");
        return obj;
    }

    if (productionSelected) {
        obj["reason"] = context.reason;
        obj["operatorAction"] = context.available
            ? QStringLiteral("none")
            : (descriptor.linked
            ? (allProductionProviderOperationsBound()
                ? QStringLiteral("capture-reviewed-provider-results-and-open-production-acceptance-gates")
                : QStringLiteral("complete-production-crypto-adapter-implementation-and-compatibility-tests"))
            : QStringLiteral("link-reviewed-production-crypto-backend"));
        return obj;
    }

    obj["reason"] = context.reason;
    obj["operatorAction"] = QStringLiteral("choose-a-registered-crypto-backend");
    return obj;
}

bool rejectWhenCryptoOperationUnavailable(E2ECryptoOperation operation, QString* reason) {
    QString source;
    const QString requested = requestedBackendId(&source);
    const E2ECryptoExecutionContext context =
        cryptoExecutionContext(operation, requested, e2eProductionCryptoRequired());
    if (context.available) {
        if (reason) {
            reason->clear();
        }
        return false;
    }
    fail(reason, context.reason.isEmpty() ? QStringLiteral("crypto-backend-unavailable") : context.reason);
    return true;
}

bool rejectWhenCryptoBackendUnavailable(QString* reason) {
    return rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::SessionKeyGeneration, reason);
}

QJsonObject backendDescriptor(const E2ECryptoAdapterDescriptor& descriptor,
                              bool selected,
                              bool available,
                              const QString& reason) {
    QJsonObject obj;
    obj["id"] = descriptor.id;
    obj["type"] = descriptor.type;
    obj["selected"] = selected;
    obj["implementation"] = descriptor.implementation;
    obj["providerId"] = descriptor.providerId;
    obj["operationContractVersion"] = descriptor.operationContractVersion;
    obj["dispatchState"] = descriptor.dispatchState;
    obj["providerSelfTestStatus"] = descriptor.selfTestStatus;
    obj["providerReadinessGate"] = descriptor.readinessGate;
    obj["providerCompatibilityStatus"] = descriptor.compatibilityStatus;
    obj["providerCompatibilityGate"] = descriptor.compatibilityGate;
    obj["providerCompatibilityEvidence"] = providerCompatibilityEvidence(descriptor);
    obj["providerReadiness"] = providerReadinessStatus(descriptor);
    if (descriptor.id == QString::fromLatin1(ProductionBackendId)) {
        obj["operationCallableManifest"] =
            productionOperationCallableManifestForDescriptor(descriptor);
        obj["operationExecutionResult"] =
            productionOperationExecutionResultForDescriptor(descriptor);
        obj["providerTable"] =
            productionProviderTableStatusForDescriptor(descriptor);
        obj["providerTableBindingProbe"] =
            productionProviderTableBindingProbeStatusForDescriptor(descriptor);
        obj["providerTableRegistration"] =
            productionProviderTableRegistrationStatusForDescriptor(descriptor);
        obj["providerOperationPreflight"] =
            productionProviderOperationPreflightStatusForDescriptor(descriptor);
        obj["providerCallFrame"] =
            productionProviderCallFrameStatusForDescriptor(descriptor);
        obj["providerInvocationDryRun"] =
            productionProviderInvocationDryRunStatusForDescriptor(descriptor);
        obj["providerInvocationResult"] =
            productionProviderInvocationResultStatusForDescriptor(descriptor);
        obj["providerExecutionDecision"] =
            productionProviderExecutionDecisionStatusForDescriptor(descriptor);
        obj["providerCallbackHarness"] =
            productionProviderCallbackHarnessStatusForDescriptor(descriptor);
        obj["providerVectorSelfTest"] =
            productionProviderVectorSelfTestStatusForDescriptor(descriptor);
        obj["providerExecutionSlotBinding"] =
            productionProviderExecutionSlotBindingStatusForDescriptor(descriptor);
        obj["providerExecutionPath"] =
            productionProviderExecutionPathStatusForDescriptor(descriptor);
        obj["providerInvocationSandbox"] =
            productionProviderInvocationSandboxStatusForDescriptor(descriptor);
        obj["providerInvocationVectorResult"] =
            productionProviderInvocationVectorResultStatusForDescriptor(descriptor);
        obj["providerInvocationExecution"] =
            productionProviderInvocationExecutionStatusForDescriptor(descriptor);
        obj["providerDataPlaneBridge"] =
            productionProviderDataPlaneBridgeStatusForDescriptor(descriptor);
        obj["providerPublicPrimitiveExecution"] =
            productionProviderPublicPrimitiveExecutionStatusForDescriptor(descriptor);
    }
    obj["linked"] = descriptor.linked;
    obj["productionReady"] = descriptor.productionReady;
    obj["available"] = available;
    obj["reason"] = reason;
    obj["rawKeyExported"] = descriptor.rawKeyExported;
    obj["privateMaterialExported"] = descriptor.privateMaterialExported;
    QJsonArray operations;
    for (const E2ECryptoOperation operation : descriptor.operations) {
        operations.append(cryptoOperationName(operation));
    }
    obj["operations"] = operations;
    return obj;
}

bool productionBackendSelected() {
    QString source;
    return requestedBackendId(&source) == QString::fromLatin1(ProductionBackendId);
}
} // namespace E2EBackendStatus

using namespace E2EBackendStatus;

QString e2eDefaultSuite() {
    QString reason;
    if (productionBackendSelected() && productionProviderRuntimeReady(&reason)) {
        return QString::fromLatin1(E2EProductionSuite);
    }
    return QString::fromLatin1(E2EDraftSuite);
}

QString e2eCryptoBackendId() {
    return QString::fromLatin1(QTNETWORKCHAT_E2E_COMPILED_BACKEND_ID);
}

QString e2eAgreementSignatureSuite() {
    QString reason;
    if (productionBackendSelected() && productionProviderRuntimeReady(&reason)) {
        return QString::fromLatin1(E2EProductionSignatureSuite);
    }
    return QString::fromLatin1(E2EDraftSignatureSuite);
}

bool e2eProductionCryptoRequired() {
    return envEnabled("QTNETWORKCHAT_E2E_REQUIRE_PRODUCTION_CRYPTO");
}

bool e2eCryptoBackendAvailable(QString* reason) {
    if (rejectWhenCryptoBackendUnavailable(reason)) {
        return false;
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

QJsonObject e2eCryptoBackendStatus() {
    const QString cacheKey = backendStatusCacheKey();
    if (g_cachedBackendStatusKey == cacheKey && !g_cachedBackendStatus.isEmpty()) {
        return g_cachedBackendStatus;
    }
    QString selectionSource;
    const QString requested = requestedBackendId(&selectionSource);
    const E2ECryptoAdapterDescriptor requestedDescriptor = cryptoAdapterForBackend(requested);
    const bool selectedProduction = requested == QString::fromLatin1(ProductionBackendId);
    QString availabilityReason;
    const bool available = e2eCryptoBackendAvailable(&availabilityReason);
    if (!available && selectedProduction && requestedDescriptor.linked && !requestedDescriptor.productionReady) {
        availabilityReason = QStringLiteral("production-adapter-not-ready");
    }

    QJsonArray registeredBackends;
    for (const E2ECryptoAdapterDescriptor& descriptor : cryptoAdapterRegistry()) {
        const bool selected = descriptor.id == requested;
        const bool descriptorAvailable = selected && available;
        const QString reason = descriptor.id == QString::fromLatin1(DraftBackendId)
            ? (e2eProductionCryptoRequired()
                ? QStringLiteral("production-required")
                : QStringLiteral("draft-backend-available"))
            : (descriptor.linked
                ? QStringLiteral("production-adapter-not-ready")
                : QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REASON));
        registeredBackends.append(backendDescriptor(descriptor,
                                                    selected,
                                                    descriptorAvailable,
                                                    reason));
    }

    QJsonObject status;
    status["backendId"] = e2eCryptoBackendId();
    status["compiledBackendId"] = e2eCryptoBackendId();
    status["requestedBackendId"] = requested;
    status["selectedBackendId"] = available ? requested : QString();
    status["selectionSource"] = selectionSource;
    status["fallbackBackendId"] = QString::fromLatin1(DraftBackendId);
    status["registeredBackends"] = registeredBackends;
    status["selectedProviderReadiness"] = providerReadinessStatus(requestedDescriptor);
    status["selectedProviderCompatibility"] = providerCompatibilityEvidence(requestedDescriptor);
    QJsonObject operations;
    for (const E2ECryptoOperation operation : cryptoOperations()) {
        operations[cryptoOperationName(operation)] =
            cryptoOperationStatus(operation, requested, e2eProductionCryptoRequired());
    }
    QJsonObject productionOperations;
    for (const E2ECryptoOperation operation : cryptoOperations()) {
        productionOperations[cryptoOperationName(operation)] =
            cryptoOperationStatus(operation,
                                  QString::fromLatin1(ProductionBackendId),
                                  true);
    }
    status["operations"] = operations;
    const QJsonObject productionAcceptance =
        productionAcceptanceStatusForDescriptor(productionAdapterDescriptor(), productionOperations);
    status["productionAcceptance"] = productionAcceptance;
    status["productionRolloutObservability"] =
        productionRolloutObservabilityStatusForAcceptance(productionAcceptance);
    status["productionOperationHarness"] =
        productionOperationHarnessStatusForDescriptor(productionAdapterDescriptor());
    status["productionOperationExecutionPlan"] =
        productionOperationExecutionPlanStatusForDescriptor(productionAdapterDescriptor());
    status["productionOperationInvocation"] =
        productionOperationInvocationStatusForDescriptor(productionAdapterDescriptor());
    status["productionOperationSlots"] =
        productionOperationSlotStatusForDescriptor(productionAdapterDescriptor());
    status["productionOperationDispatchBindings"] =
        productionOperationDispatchBindingStatusForDescriptor(productionAdapterDescriptor());
    status["productionOperationCallableManifest"] =
        productionOperationCallableManifestForDescriptor(productionAdapterDescriptor());
    status["productionOperationExecutionResult"] =
        productionOperationExecutionResultForDescriptor(productionAdapterDescriptor());
    status["productionProviderTable"] =
        productionProviderTableStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderTableBindingProbe"] =
        productionProviderTableBindingProbeStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderTableRegistration"] =
        productionProviderTableRegistrationStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderOperationPreflight"] =
        productionProviderOperationPreflightStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderCallFrame"] =
        productionProviderCallFrameStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderInvocationDryRun"] =
        productionProviderInvocationDryRunStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderInvocationResult"] =
        productionProviderInvocationResultStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderExecutionDecision"] =
        productionProviderExecutionDecisionStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderCallbackHarness"] =
        productionProviderCallbackHarnessStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderVectorSelfTest"] =
        productionProviderVectorSelfTestStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderExecutionSlotBinding"] =
        productionProviderExecutionSlotBindingStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderExecutionPath"] =
        productionProviderExecutionPathStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderInvocationSandbox"] =
        productionProviderInvocationSandboxStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderInvocationVectorResult"] =
        productionProviderInvocationVectorResultStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderInvocationExecution"] =
        productionProviderInvocationExecutionStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderReviewedExecutionCandidate"] =
        productionProviderReviewedExecutionCandidateStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderReviewedCallHandoff"] =
        productionProviderReviewedCallHandoffStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderReviewedOperationStubBoundary"] =
        productionProviderReviewedOperationStubBoundaryStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderReviewedCallableTableBridge"] =
        productionProviderReviewedCallableTableBridgeStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderReviewedOperationCallableInterface"] =
        productionProviderReviewedOperationCallableInterfaceStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderReviewedCallableRuntimePreflight"] =
        productionProviderReviewedCallableRuntimePreflightStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderReviewedInvocationArming"] =
        productionProviderReviewedInvocationArmingStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderReviewedInvocationExecutionAcceptance"] =
        productionProviderReviewedInvocationExecutionAcceptanceStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderDataPlaneBridge"] =
        productionProviderDataPlaneBridgeStatusForDescriptor(productionAdapterDescriptor());
    status["productionProviderPublicPrimitiveExecution"] =
        productionProviderPublicPrimitiveExecutionStatusForDescriptor(productionAdapterDescriptor());
    status["protocol"] = QString::fromLatin1(E2EProtocolV1);
    status["suite"] = selectedProduction && available
        ? QString::fromLatin1(E2EProductionSuite)
        : e2eDefaultSuite();
    status["wireCompatibleSuite"] = QString::fromLatin1(E2EProductionSuite);
    status["signatureSuite"] = e2eAgreementSignatureSuite();
    status["productionReady"] = productionAdapterDescriptor().productionReady;
    status["productionRequired"] = e2eProductionCryptoRequired();
    status["productionBackendRequestedAtBuild"] = QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REQUESTED != 0;
    status["productionBackendAvailableAtBuild"] = QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_AVAILABLE != 0;
    status["productionBackendReason"] = QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REASON);
    status["requestedProductionBackend"] = QString::fromLatin1(QTNETWORKCHAT_E2E_REQUESTED_PRODUCTION_BACKEND);
    status["productionAdapterRequested"] = QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_REQUESTED != 0;
    status["productionAdapterLinked"] = QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED != 0;
    status["productionAdapterReason"] = QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_REASON);
    status["productionRequiredOperations"] =
        QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_REQUIRED_OPERATIONS);
    status["available"] = available;
    status["unavailableReason"] = availabilityReason;
    status["status"] = available
        ? (selectedProduction
            ? QStringLiteral("production-backend-active")
            : QStringLiteral("draft-backend-active"))
        : availabilityReason;
    status["operatorAction"] = !available
        ? QStringLiteral("install-production-crypto-backend-or-disable-requirement")
        : (selectedProduction
            ? QStringLiteral("none")
            : QStringLiteral("draft-backend-allowed-for-current-build"));
    status["rawKeyExported"] = false;
    g_cachedBackendStatusKey = cacheKey;
    g_cachedBackendStatus = status;
    return status;
}

QJsonObject e2eProductionCryptoAcceptanceStatus() {
    return e2eCryptoBackendStatus().value(QStringLiteral("productionAcceptance")).toObject();
}

QJsonObject e2eProductionCryptoRolloutObservabilityStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionRolloutObservability")).toObject();
}

QJsonObject e2eProductionCryptoOperationHarnessStatus() {
    return e2eCryptoBackendStatus().value(QStringLiteral("productionOperationHarness")).toObject();
}

QJsonObject e2eProductionCryptoOperationExecutionPlanStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionOperationExecutionPlan")).toObject();
}

QJsonObject e2eProductionCryptoOperationInvocationStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionOperationInvocation")).toObject();
}

QJsonObject e2eProductionCryptoOperationSlotStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionOperationSlots")).toObject();
}

QJsonObject e2eProductionCryptoOperationCallableManifestStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionOperationCallableManifest")).toObject();
}

QJsonObject e2eProbeProductionCryptoProviderInvocationExecution() {
    return productionProviderInvocationExecutionProbeForDescriptor(productionAdapterDescriptor());
}

QJsonObject e2eProbeProductionCryptoProviderReviewedExecutionCandidate() {
    const QJsonObject invocationExecutionProbe =
        productionProviderInvocationExecutionProbeForDescriptor(productionAdapterDescriptor());
    return productionProviderReviewedExecutionCandidateStatusFromProbe(
        productionAdapterDescriptor(),
        invocationExecutionProbe);
}

QJsonObject e2eProbeProductionCryptoProviderReviewedCallHandoff() {
    const QJsonObject reviewedCandidate =
        e2eProbeProductionCryptoProviderReviewedExecutionCandidate();
    return productionProviderReviewedCallHandoffStatusFromCandidate(
        productionAdapterDescriptor(),
        reviewedCandidate);
}

QJsonObject e2eProbeProductionCryptoProviderReviewedOperationStubBoundary() {
    const QJsonObject reviewedCallHandoff =
        e2eProbeProductionCryptoProviderReviewedCallHandoff();
    return productionProviderReviewedOperationStubBoundaryStatusFromHandoff(
        productionAdapterDescriptor(),
        reviewedCallHandoff);
}

QJsonObject e2eProbeProductionCryptoProviderReviewedCallableTableBridge() {
    const QJsonObject reviewedOperationStubBoundary =
        e2eProbeProductionCryptoProviderReviewedOperationStubBoundary();
    return productionProviderReviewedCallableTableBridgeStatusFromStubBoundary(
        productionAdapterDescriptor(),
        reviewedOperationStubBoundary);
}

QJsonObject e2eProbeProductionCryptoProviderReviewedOperationCallableInterface() {
    const QJsonObject reviewedCallableTableBridge =
        e2eProbeProductionCryptoProviderReviewedCallableTableBridge();
    return productionProviderReviewedOperationCallableInterfaceStatusFromBridge(
        productionAdapterDescriptor(),
        reviewedCallableTableBridge);
}

QJsonObject e2eProbeProductionCryptoProviderReviewedCallableRuntimePreflight() {
    const QJsonObject reviewedOperationCallableInterface =
        e2eProbeProductionCryptoProviderReviewedOperationCallableInterface();
    return productionProviderReviewedCallableRuntimePreflightStatusFromInterface(
        productionAdapterDescriptor(),
        reviewedOperationCallableInterface);
}

QJsonObject e2eProbeProductionCryptoProviderReviewedInvocationArming() {
    const QJsonObject reviewedCallableRuntimePreflight =
        e2eProbeProductionCryptoProviderReviewedCallableRuntimePreflight();
    return productionProviderReviewedInvocationArmingStatusFromRuntimePreflight(
        productionAdapterDescriptor(),
        reviewedCallableRuntimePreflight);
}

QJsonObject e2eProbeProductionCryptoProviderReviewedInvocationExecutionAcceptance() {
    const QJsonObject reviewedInvocationArming =
        e2eProbeProductionCryptoProviderReviewedInvocationArming();
    return productionProviderReviewedInvocationExecutionAcceptanceStatusFromArming(
        productionAdapterDescriptor(),
        reviewedInvocationArming);
}

QJsonObject e2eProbeProductionCryptoProviderDataPlaneBridge() {
    const QJsonObject reviewedInvocationExecutionAcceptance =
        e2eProbeProductionCryptoProviderReviewedInvocationExecutionAcceptance();
    return productionProviderDataPlaneBridgeStatusFromExecutionAcceptance(
        productionAdapterDescriptor(),
        reviewedInvocationExecutionAcceptance);
}

QJsonObject e2eProbeProductionCryptoProviderRoundTripExecution() {
    return productionProviderRoundTripExecutionProbeForDescriptor(productionAdapterDescriptor());
}

QJsonObject e2eProbeProductionCryptoProviderPublicPrimitiveExecution() {
    return productionProviderPublicPrimitiveExecutionProbeForDescriptor(productionAdapterDescriptor());
}

QJsonObject e2eValidateProductionProviderTable(const qnc_e2e_provider_table_v1* table) {
    return providerTableValidationStatus(table);
}

QJsonObject e2eRegisterProductionProviderTable(const qnc_e2e_provider_table_v1* table) {
    g_registeredProductionProviderTable = table;
    invalidateBackendStatusCache();
    return productionProviderTableRegistrationStatusForDescriptor(productionAdapterDescriptor());
}
