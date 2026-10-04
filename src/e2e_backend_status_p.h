#pragma once

// Private status/reporting boundary; the public E2E API stays in e2eenvelope.h.
#include "e2eenvelope.h"
#include "e2e_codec_support.h"
#include "e2e_crypto_primitives.h"
#include "e2e_provider_runtime.h"
#include "qtnetworkchat_e2e_crypto_config.h"
#include <QJsonArray>
#include <QStringList>

namespace E2EBackendStatus {
using namespace E2ECodecSupport;
using namespace E2ECryptoPrimitives;
using namespace E2EProviderRuntime;
bool productionProviderTableRegistered();
inline constexpr char E2EProtocolV1[] = "qtnetworkchat-e2e-v1";
inline constexpr char E2EDraftSuite[] = "draft-placeholder";
inline constexpr char E2EDraftSignatureSuite[] = "draft-identity-hmac-sha256";
inline constexpr char E2EProductionSignatureSuite[] = "ed25519";
inline constexpr char DraftBackendId[] = "draft-qt-hmac-stream-v1";
inline constexpr char ProductionBackendId[] = QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_ID;

struct E2ECryptoAdapterDescriptor {
    QString id;
    QString type;
    QString implementation;
    QString providerId;
    QString operationContractVersion;
    QString dispatchState;
    QString selfTestStatus;
    QString readinessGate;
    QString compatibilityStatus;
    QString compatibilityGate;
    QString unavailableReason;
    QString operatorAction;
    bool productionReady = false;
    bool linked = false;
    bool rawKeyExported = false;
    bool privateMaterialExported = false;
    QList<E2ECryptoOperation> operations;
};

struct E2ECryptoOperationSpec {
    E2ECryptoOperation operation;
    QString implementationState;
    QString vectorSet;
    QString compatibilityStatus;
    QString migrationBlocker;
    QString operatorAction;
    bool implemented = false;
    bool knownAnswerPassed = false;
    bool roundTripPassed = false;
};

struct E2ECryptoExecutionContext {
    E2ECryptoAdapterDescriptor descriptor;
    E2ECryptoOperation operation;
    bool backendFound = false;
    bool operationRegistered = false;
    bool available = false;
    QString reason;
    QString entrypoint;
};

void invalidateBackendStatusCache();
const qnc_e2e_provider_table_v1* builtInProductionProviderTable();
const qnc_e2e_provider_table_v1* activeProductionProviderTable();
bool usingBuiltInProductionProviderTable();
bool envEnabled(const char* name);
QString requestedBackendId(QString* source = nullptr);
QString backendStatusCacheKey();
QString cryptoOperationName(E2ECryptoOperation operation);
QList<E2ECryptoOperation> cryptoOperations();
QList<E2ECryptoOperationSpec> productionOperationSpecs();
E2ECryptoOperationSpec productionOperationSpec(E2ECryptoOperation operation);
QJsonObject productionOperationSpecJson(const E2ECryptoOperationSpec& spec);
int implementedProductionOperationCount();
int productionRoundTripReadyOperationCount();
QString productionHarnessFixtureHash(const E2ECryptoOperationSpec& spec);
QJsonArray productionOperationManifest();
QStringList productionOperationInputContract(E2ECryptoOperation operation);
QStringList productionOperationOutputContract(E2ECryptoOperation operation);
QString productionProbeFixtureInputClass(E2ECryptoOperation operation);
QString productionProbeExpectedMaterialPolicyClass(E2ECryptoOperation operation);
QJsonObject productionProviderProbeVectorContract(const E2ECryptoOperationSpec& spec);
QJsonObject productionProviderProbeExecutionFrame(const E2ECryptoOperationSpec& spec, const E2ECryptoAdapterDescriptor& descriptor, const QJsonObject& vectorContract, bool canInvoke, const QString& statusClass, const QString& outputStatusClass, const QString& failureClass, const QString& vectorResultClass, qint64 primarySize, qint64 secondarySize, qint64 aadSize, qint64 publicOutputSize, qint64 sealedOutputSize);
QString productionProbeExpectedKnownAnswerOutputClass(E2ECryptoOperation operation);
QString productionProbeObservedKnownAnswerOutputClass(qint64 publicOutputSize, qint64 sealedOutputSize, const QString& materialPolicyClass, const QString& statusClass);
QJsonObject productionProviderProbeKnownAnswerOutputEvidence(const E2ECryptoOperationSpec& spec, const QJsonObject& vectorContract, bool canInvoke, const QString& statusClass, const QString& materialPolicyClass, qint64 publicOutputSize, qint64 sealedOutputSize);
QString productionOperationSlotId(E2ECryptoOperation operation);
QString productionOperationProviderSymbol(E2ECryptoOperation operation);
QString productionOperationProviderAbiSignature(E2ECryptoOperation operation);
QString productionOperationMigrationPhase(E2ECryptoOperation operation);
QString productionOperationPublicApi(E2ECryptoOperation operation);
QString productionOperationPublicDataPlaneBoundary(E2ECryptoOperation operation);
QJsonObject productionOperationSlotContract(const E2ECryptoAdapterDescriptor& descriptor, const E2ECryptoOperationSpec& spec, const QJsonObject& harnessOperation);
QJsonObject productionOperationInvocationContract(const E2ECryptoAdapterDescriptor& descriptor, const E2ECryptoOperationSpec& spec, const QJsonObject& harnessOperation);
QJsonObject productionOperationSlotStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionOperationDispatchBindingStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionOperationCallableManifestForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionOperationExecutionResultForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QStringList configuredProductionProviderSymbols();
QJsonObject productionProviderTableStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject providerTableFieldOffsetStatus(const QString& name, qsizetype offset, qsizetype previousOffset);
QJsonObject productionProviderTableRegistrationStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
ProviderDispatchResult dispatchProductionProviderOperation(E2ECryptoOperation operation, const QByteArray& primary, const QByteArray& secondary, const QByteArray& aad);
bool productionProviderRuntimeReady(QString* reason = nullptr);
QString providerProbeFailureClass(bool canInvoke, const QString& callbackStatusClass, const QString& outputStatusClass, const QString& blockedReason);
QString providerProbeMismatchReason(bool canInvoke, bool expectedStatusMatched, bool expectedFailureMatched, bool expectedMaterialPolicyMatched, bool expectedOutputClassMatched, const QString& blockedReason);
QString providerProbeMismatchSeverity(const QString& mismatchReason);
QString providerProbeMismatchScope(bool canInvoke, bool tableValidationAccepted, bool pointerPresent, const QString& mismatchReason);
void incrementSummaryCount(QJsonObject* summary, const QString& key);
QJsonObject productionProviderInvocationExecutionStructuralSnapshotForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderInvocationExecutionProbeCoreForDescriptor( const E2ECryptoAdapterDescriptor& descriptor, const QJsonObject& structuralExecution, bool invokeProviderOperations);
QJsonObject productionProviderInvocationExecutionProbeEvidenceForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderInvocationExecutionProbeForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderRoundTripExecutionProbeForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderOperationPreflightStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QString productionCallFramePrimaryClass(E2ECryptoOperation operation);
QString productionCallFrameSecondaryClass(E2ECryptoOperation operation);
QString productionCallFrameAadClass(E2ECryptoOperation operation);
QString productionCallFrameMaterialPolicy(E2ECryptoOperation operation);
QJsonObject productionProviderCallFrameStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderInvocationDryRunStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderInvocationResultStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderExecutionDecisionStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderCallbackHarnessStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderVectorSelfTestStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderExecutionSlotBindingStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderExecutionPathStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderInvocationSandboxStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderInvocationVectorResultStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderInvocationExecutionStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QString providerReviewedCandidateBlockedReason(const QJsonObject& probe, bool probeSourceCaptured, bool executionEvidenceReady, bool candidateSanitized);
QJsonObject productionProviderReviewedExecutionCandidateStatusFromProbe( const E2ECryptoAdapterDescriptor& descriptor, const QJsonObject& invocationExecutionProbe);
QJsonObject productionProviderReviewedExecutionCandidateStatusForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QString providerReviewedCallHandoffBlockedReason(const QJsonObject& candidate, bool candidateSourceCaptured, bool handoffSanitized, bool policyReady);
QJsonObject productionProviderReviewedCallHandoffStatusFromCandidate( const E2ECryptoAdapterDescriptor& descriptor, const QJsonObject& reviewedExecutionCandidate);
QJsonObject productionProviderReviewedCallHandoffStatusForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QString providerReviewedOperationStubBoundaryBlockedReason(const QJsonObject& handoff, bool handoffSourceCaptured, bool stubPolicyReady, bool stubSanitized);
QJsonObject productionProviderReviewedOperationStubBoundaryStatusFromHandoff( const E2ECryptoAdapterDescriptor& descriptor, const QJsonObject& reviewedCallHandoff);
QJsonObject productionProviderReviewedOperationStubBoundaryStatusForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QString providerReviewedCallableTableBridgeBlockedReason(const QJsonObject& stub, bool stubSourceCaptured, bool tableSlotReady, bool contractHashReady, bool bridgeSanitized);
QJsonObject productionProviderReviewedCallableTableBridgeStatusFromStubBoundary( const E2ECryptoAdapterDescriptor& descriptor, const QJsonObject& reviewedOperationStubBoundary);
QJsonObject productionProviderReviewedCallableTableBridgeStatusForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QString providerReviewedOperationCallableInterfaceBlockedReason(const QJsonObject& bridge, bool bridgeSourceCaptured, bool functionPointerReady, bool structContractReady, bool materialPolicyReady, bool interfaceSanitized);
QJsonObject productionProviderReviewedOperationCallableInterfaceStatusFromBridge( const E2ECryptoAdapterDescriptor& descriptor, const QJsonObject& reviewedCallableTableBridge);
QJsonObject productionProviderReviewedOperationCallableInterfaceStatusForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QString providerReviewedCallableRuntimePreflightBlockedReason(const QJsonObject& callableInterface, bool interfaceSourceCaptured, bool abiReady, bool contractReady, bool policyReady, bool fixtureReady, bool preflightSanitized);
QJsonObject productionProviderReviewedCallableRuntimePreflightStatusFromInterface( const E2ECryptoAdapterDescriptor& descriptor, const QJsonObject& reviewedOperationCallableInterface);
QJsonObject productionProviderReviewedCallableRuntimePreflightStatusForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QString providerReviewedInvocationArmingBlockedReason(const QJsonObject& runtimePreflight, bool runtimePreflightSourceCaptured, bool runtimePreflightReady, bool callbackEntryReady, bool sandboxPolicyReady, bool resultPolicyReady, bool armingSanitized);
QJsonObject productionProviderReviewedInvocationArmingStatusFromRuntimePreflight( const E2ECryptoAdapterDescriptor& descriptor, const QJsonObject& reviewedCallableRuntimePreflight);
QJsonObject productionProviderReviewedInvocationArmingStatusForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QString providerReviewedInvocationExecutionAcceptanceBlockedReason(const QJsonObject& arming, bool armingSourceCaptured, bool armingReady, bool executionContractReady, bool vectorEvidenceReady, bool resultPolicyReady, bool acceptanceSanitized);
QJsonObject productionProviderReviewedInvocationExecutionAcceptanceStatusFromArming( const E2ECryptoAdapterDescriptor& descriptor, const QJsonObject& reviewedInvocationArming);
QJsonObject productionProviderReviewedInvocationExecutionAcceptanceStatusForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QString providerDataPlaneBridgeBlockedReason(const QJsonObject& executionAcceptance, bool acceptanceSourceCaptured, bool executionAcceptanceReady, bool publicApiMapped, bool contractReady, bool bridgeSanitized);
QJsonObject productionProviderDataPlaneBridgeStatusFromExecutionAcceptance( const E2ECryptoAdapterDescriptor& descriptor, const QJsonObject& reviewedInvocationExecutionAcceptance);
QJsonObject productionProviderDataPlaneBridgeStatusForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderPublicPrimitiveExecutionStatusFromBridge( const E2ECryptoAdapterDescriptor& descriptor, const QJsonObject& providerDataPlaneBridge);
QJsonObject productionProviderPublicPrimitiveExecutionStatusForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderPublicPrimitiveExecutionProbeForDescriptor( const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderTableBindingProbeStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionOperationHarnessStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionOperationExecutionPlanStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionOperationInvocationStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
E2ECryptoAdapterDescriptor draftAdapterDescriptor();
E2ECryptoAdapterDescriptor productionAdapterDescriptor();
QList<E2ECryptoAdapterDescriptor> cryptoAdapterRegistry();
E2ECryptoAdapterDescriptor cryptoAdapterForBackend(const QString& requested, bool* found = nullptr);
bool adapterSupportsOperation(const E2ECryptoAdapterDescriptor& descriptor, E2ECryptoOperation operation);
bool providerCanDispatchOperation(const E2ECryptoAdapterDescriptor& descriptor, E2ECryptoOperation operation, bool productionRequired, QString* reason);
QStringList cryptoOperationNames();
QJsonObject providerCompatibilityEvidence(const E2ECryptoAdapterDescriptor& descriptor);
QJsonArray providerReadinessChecks(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject providerReadinessStatus(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionAcceptanceStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor, const QJsonObject& operations);
QJsonObject productionRolloutObservabilityStatusForAcceptance(const QJsonObject& acceptance);
E2ECryptoExecutionContext cryptoExecutionContext(E2ECryptoOperation operation, const QString& requested, bool productionRequired);
QJsonObject cryptoOperationStatus(E2ECryptoOperation operation, const QString& requested, bool productionRequired);
bool rejectWhenCryptoOperationUnavailable(E2ECryptoOperation operation, QString* reason);
bool rejectWhenCryptoBackendUnavailable(QString* reason);
QJsonObject backendDescriptor(const E2ECryptoAdapterDescriptor& descriptor, bool selected, bool available, const QString& reason);
bool productionBackendSelected();
} // namespace E2EBackendStatus
