#include "e2eenvelope.h"
#include "e2e_codec_support.h"
#include "qtnetworkchat_e2e_crypto_config.h"
#include "qtnetworkchat_e2e_provider_api.h"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QList>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QStringList>

#include <cstddef>

namespace {
using namespace E2ECodecSupport;
constexpr qsizetype SessionKeyBytes = 32;
constexpr qsizetype MinSessionKeyBytes = 16;
constexpr quint32 DraftDhPrime = 2147483647u;
constexpr quint32 DraftDhGenerator = 5u;
const char DraftDhPrivatePrefix[] = "qnc-dh1-private:";
const char DraftDhPublicPrefix[] = "qnc-dh1-public:";
const char E2EProtocolV1[] = "qtnetworkchat-e2e-v1";
const char E2EDraftSuite[] = "draft-placeholder";
const char E2EAdvertisedSuite[] = "x25519-hkdf-sha256-aes-256-gcm";
const char E2EDraftSignatureSuite[] = "draft-identity-hmac-sha256";
const char E2EProductionSignatureSuite[] = "ed25519";
const char DraftBackendId[] = "draft-qt-hmac-stream-v1";
const char ProductionBackendId[] = QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_ID;
const char ProductionSessionDerivePrimaryDomain[] =
    "qtnetworkchat-e2e-authenticated-production-v1|";
const char ProductionSessionDeriveSecondaryDomain[] =
    "qtnetworkchat-e2e-production-session-public-material-v1|";

enum class E2ECryptoOperation {
    SessionKeyGeneration,
    IdentityKeyGeneration,
    PublicKeyDerivation,
    AgreementSign,
    AgreementVerify,
    SessionDerive,
    PayloadEncrypt,
    PayloadDecrypt,
};

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

struct ProviderDispatchResult {
    bool invoked = false;
    qnc_e2e_status_t callbackStatus = QNC_E2E_STATUS_UNSUPPORTED;
    qnc_e2e_status_t outputStatus = QNC_E2E_STATUS_UNSUPPORTED;
    qnc_e2e_material_policy_t materialPolicy = QNC_E2E_MATERIAL_HANDLE_ONLY;
    QString errorClass = QStringLiteral("not-invoked");
    QString reason;
    QByteArray publicOutput;
    QByteArray sealedOutput;
};

const qnc_e2e_provider_table_v1* g_registeredProductionProviderTable = nullptr;
QJsonObject g_cachedBackendStatus;
QString g_cachedBackendStatusKey;

void invalidateBackendStatusCache() {
    g_cachedBackendStatus = QJsonObject();
    g_cachedBackendStatusKey.clear();
}

bool allProductionProviderOperationsBound() {
    return QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_SESSION_KEY_GENERATION != 0
        && QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_IDENTITY_KEY_GENERATION != 0
        && QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PUBLIC_KEY_DERIVATION != 0
        && QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_AGREEMENT_SIGN != 0
        && QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_AGREEMENT_VERIFY != 0
        && QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_SESSION_DERIVE != 0
        && QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PAYLOAD_ENCRYPT != 0
        && QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PAYLOAD_DECRYPT != 0;
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


QString sanitizedBackendId(QString value) {
    value = value.trimmed().toLower();
    if (value.isEmpty()) {
        return value;
    }
    QString sanitized;
    sanitized.reserve(qMin(value.size(), 96));
    for (const QChar ch : value.left(96)) {
        const ushort code = ch.unicode();
        const bool digit = code >= '0' && code <= '9';
        const bool lower = code >= 'a' && code <= 'z';
        if (digit || lower || ch == QLatin1Char('-') || ch == QLatin1Char('_') || ch == QLatin1Char('.')) {
            sanitized.append(ch);
        }
    }
    return sanitized;
}

QString requestedBackendId(QString* source = nullptr) {
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

QString cryptoOperationName(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
        return QStringLiteral("session-key-generation");
    case E2ECryptoOperation::IdentityKeyGeneration:
        return QStringLiteral("identity-key-generation");
    case E2ECryptoOperation::PublicKeyDerivation:
        return QStringLiteral("public-key-derivation");
    case E2ECryptoOperation::AgreementSign:
        return QStringLiteral("agreement-sign");
    case E2ECryptoOperation::AgreementVerify:
        return QStringLiteral("agreement-verify");
    case E2ECryptoOperation::SessionDerive:
        return QStringLiteral("session-derive");
    case E2ECryptoOperation::PayloadEncrypt:
        return QStringLiteral("payload-encrypt");
    case E2ECryptoOperation::PayloadDecrypt:
        return QStringLiteral("payload-decrypt");
    }
    return QStringLiteral("unknown");
}

QList<E2ECryptoOperation> cryptoOperations() {
    return {
        E2ECryptoOperation::SessionKeyGeneration,
        E2ECryptoOperation::IdentityKeyGeneration,
        E2ECryptoOperation::PublicKeyDerivation,
        E2ECryptoOperation::AgreementSign,
        E2ECryptoOperation::AgreementVerify,
        E2ECryptoOperation::SessionDerive,
        E2ECryptoOperation::PayloadEncrypt,
        E2ECryptoOperation::PayloadDecrypt,
    };
}

QList<E2ECryptoOperationSpec> productionOperationSpecs() {
    QList<E2ECryptoOperationSpec> specs;
    const auto appendSpec = [&specs](E2ECryptoOperation operation,
                                     const QString& vectorSet,
                                     const QString& migrationBlocker) {
        E2ECryptoOperationSpec spec;
        spec.operation = operation;
        spec.implementationState = QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED != 0
            ? QStringLiteral("linked-placeholder")
            : QStringLiteral("not-linked");
        spec.vectorSet = vectorSet;
        spec.compatibilityStatus = QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED != 0
            ? QStringLiteral("not-run-placeholder")
            : QStringLiteral("not-run-not-linked");
        spec.migrationBlocker = migrationBlocker;
        spec.operatorAction = QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED != 0
            ? QStringLiteral("replace-placeholder-operation-with-reviewed-implementation")
            : QStringLiteral("link-reviewed-production-crypto-backend");
#if QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_SESSION_KEY_GENERATION != 0
        if (operation == E2ECryptoOperation::SessionKeyGeneration) {
            spec.implementationState = QStringLiteral("linked-reviewed-session-key-generation");
            spec.compatibilityStatus = QStringLiteral("known-answer-shape-passed");
            spec.migrationBlocker =
                QStringLiteral("production-acceptance-gates-not-open");
            spec.operatorAction =
                QStringLiteral("complete-production-acceptance-gates-before-enabling-dispatch");
            spec.implemented = true;
            spec.knownAnswerPassed = true;
        }
#endif
#if QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_IDENTITY_KEY_GENERATION != 0
        if (operation == E2ECryptoOperation::IdentityKeyGeneration) {
            spec.implementationState = QStringLiteral("linked-reviewed-identity-key-generation");
            spec.compatibilityStatus = QStringLiteral("known-answer-shape-passed");
            spec.migrationBlocker =
                QStringLiteral("production-acceptance-gates-not-open");
            spec.operatorAction =
                QStringLiteral("complete-production-acceptance-gates-before-enabling-dispatch");
            spec.implemented = true;
            spec.knownAnswerPassed = true;
        }
#endif
#if QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PUBLIC_KEY_DERIVATION != 0
        if (operation == E2ECryptoOperation::PublicKeyDerivation) {
            spec.implementationState = QStringLiteral("linked-reviewed-public-key-derivation");
            spec.compatibilityStatus = QStringLiteral("known-answer-shape-passed");
            spec.migrationBlocker =
                QStringLiteral("production-acceptance-gates-not-open");
            spec.operatorAction =
                QStringLiteral("complete-production-acceptance-gates-before-enabling-dispatch");
            spec.implemented = true;
            spec.knownAnswerPassed = true;
        }
#endif
#if QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_AGREEMENT_SIGN != 0
        if (operation == E2ECryptoOperation::AgreementSign) {
            spec.implementationState = QStringLiteral("linked-reviewed-agreement-sign");
            spec.compatibilityStatus = QStringLiteral("known-answer-shape-passed");
            spec.migrationBlocker =
                QStringLiteral("production-acceptance-gates-not-open");
            spec.operatorAction =
                QStringLiteral("complete-production-acceptance-gates-before-enabling-dispatch");
            spec.implemented = true;
            spec.knownAnswerPassed = true;
        }
#endif
#if QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_AGREEMENT_VERIFY != 0
        if (operation == E2ECryptoOperation::AgreementVerify) {
            spec.implementationState = QStringLiteral("linked-reviewed-agreement-verify");
            spec.compatibilityStatus = QStringLiteral("known-answer-shape-passed");
            spec.migrationBlocker =
                QStringLiteral("production-acceptance-gates-not-open");
            spec.operatorAction =
                QStringLiteral("complete-production-acceptance-gates-before-enabling-dispatch");
            spec.implemented = true;
            spec.knownAnswerPassed = true;
        }
#endif
#if QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_SESSION_DERIVE != 0
        if (operation == E2ECryptoOperation::SessionDerive) {
            spec.implementationState = QStringLiteral("linked-reviewed-session-derive");
            spec.compatibilityStatus = QStringLiteral("known-answer-shape-passed");
            spec.migrationBlocker =
                QStringLiteral("production-acceptance-gates-not-open");
            spec.operatorAction =
                QStringLiteral("complete-production-acceptance-gates-before-enabling-dispatch");
            spec.implemented = true;
            spec.knownAnswerPassed = true;
        }
#endif
#if QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PAYLOAD_ENCRYPT != 0
        if (operation == E2ECryptoOperation::PayloadEncrypt) {
            spec.implementationState = QStringLiteral("linked-reviewed-payload-encrypt");
            spec.compatibilityStatus = QStringLiteral("known-answer-shape-passed");
            spec.migrationBlocker =
                QStringLiteral("production-acceptance-gates-not-open");
            spec.operatorAction =
                QStringLiteral("complete-production-acceptance-gates-before-enabling-dispatch");
            spec.implemented = true;
            spec.knownAnswerPassed = true;
            spec.roundTripPassed = true;
        }
#endif
#if QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PAYLOAD_DECRYPT != 0
        if (operation == E2ECryptoOperation::PayloadDecrypt) {
            spec.implementationState = QStringLiteral("linked-reviewed-payload-decrypt");
            spec.compatibilityStatus = QStringLiteral("known-answer-shape-passed");
            spec.migrationBlocker =
                QStringLiteral("production-acceptance-gates-not-open");
            spec.operatorAction =
                QStringLiteral("complete-production-acceptance-gates-before-enabling-dispatch");
            spec.implemented = true;
            spec.knownAnswerPassed = true;
            spec.roundTripPassed = true;
        }
#endif
        specs.append(spec);
    };

    appendSpec(E2ECryptoOperation::SessionKeyGeneration,
               QStringLiteral("production-session-key-generation-vectors-v1"),
               QStringLiteral("production-session-key-generation-not-implemented"));
    appendSpec(E2ECryptoOperation::IdentityKeyGeneration,
               QStringLiteral("production-identity-key-generation-vectors-v1"),
               QStringLiteral("production-identity-key-generation-not-implemented"));
    appendSpec(E2ECryptoOperation::PublicKeyDerivation,
               QStringLiteral("production-public-key-derivation-vectors-v1"),
               QStringLiteral("production-public-key-derivation-not-implemented"));
    appendSpec(E2ECryptoOperation::AgreementSign,
               QStringLiteral("production-agreement-sign-vectors-v1"),
               QStringLiteral("production-agreement-sign-not-implemented"));
    appendSpec(E2ECryptoOperation::AgreementVerify,
               QStringLiteral("production-agreement-verify-vectors-v1"),
               QStringLiteral("production-agreement-verify-not-implemented"));
    appendSpec(E2ECryptoOperation::SessionDerive,
               QStringLiteral("production-session-derive-vectors-v1"),
               QStringLiteral("production-session-derive-not-implemented"));
    appendSpec(E2ECryptoOperation::PayloadEncrypt,
               QStringLiteral("production-payload-encrypt-vectors-v1"),
               QStringLiteral("production-payload-encrypt-not-implemented"));
    appendSpec(E2ECryptoOperation::PayloadDecrypt,
               QStringLiteral("production-payload-decrypt-vectors-v1"),
               QStringLiteral("production-payload-decrypt-not-implemented"));
    return specs;
}

E2ECryptoOperationSpec productionOperationSpec(E2ECryptoOperation operation) {
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        if (spec.operation == operation) {
            return spec;
        }
    }
    E2ECryptoOperationSpec spec;
    spec.operation = operation;
    spec.implementationState = QStringLiteral("unknown");
    spec.vectorSet = QStringLiteral("unknown");
    spec.compatibilityStatus = QStringLiteral("unknown");
    spec.migrationBlocker = QStringLiteral("production-operation-spec-missing");
    spec.operatorAction = QStringLiteral("register-production-operation-spec");
    return spec;
}

QJsonObject productionOperationSpecJson(const E2ECryptoOperationSpec& spec) {
    QJsonObject obj;
    obj[QStringLiteral("operation")] = cryptoOperationName(spec.operation);
    obj[QStringLiteral("implemented")] = spec.implemented;
    obj[QStringLiteral("implementationState")] = spec.implementationState;
    obj[QStringLiteral("vectorSet")] = spec.vectorSet;
    obj[QStringLiteral("compatibilityStatus")] = spec.compatibilityStatus;
    obj[QStringLiteral("knownAnswerPassed")] = spec.knownAnswerPassed;
    obj[QStringLiteral("roundTripPassed")] = spec.roundTripPassed;
    obj[QStringLiteral("migrationBlocker")] = spec.migrationBlocker;
    obj[QStringLiteral("operatorAction")] = spec.operatorAction;
    obj[QStringLiteral("rawKeyExported")] = false;
    obj[QStringLiteral("privateMaterialExported")] = false;
    return obj;
}

int implementedProductionOperationCount() {
    int count = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        if (spec.implemented) {
            ++count;
        }
    }
    return count;
}

int productionRoundTripReadyOperationCount() {
    int count = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        if (spec.roundTripPassed) {
            ++count;
        }
    }
    return count;
}

QString productionHarnessFixtureHash(const E2ECryptoOperationSpec& spec) {
    QByteArray fixture;
    fixture.append("qtnetworkchat-e2e-production-operation-harness-v1|");
    fixture.append(cryptoOperationName(spec.operation).toUtf8());
    fixture.append('|');
    fixture.append(spec.vectorSet.toUtf8());
    fixture.append('|');
    fixture.append(spec.migrationBlocker.toUtf8());
    return e2eFingerprint(fixture);
}

QJsonArray productionOperationManifest() {
    QJsonArray manifest;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        manifest.append(productionOperationSpecJson(spec));
    }
    return manifest;
}

QJsonObject productionOperationHarnessStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderTableStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderTableBindingProbeStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderTableRegistrationStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderOperationPreflightStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor);
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
QJsonObject productionProviderReviewedExecutionCandidateStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderReviewedExecutionCandidateStatusFromProbe(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& invocationExecutionProbe);
QJsonObject productionProviderReviewedCallHandoffStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderReviewedCallHandoffStatusFromCandidate(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedExecutionCandidate);
QJsonObject productionProviderReviewedOperationStubBoundaryStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderReviewedOperationStubBoundaryStatusFromHandoff(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedCallHandoff);
QJsonObject productionProviderReviewedCallableTableBridgeStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderReviewedCallableTableBridgeStatusFromStubBoundary(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedOperationStubBoundary);
QJsonObject productionProviderReviewedOperationCallableInterfaceStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderReviewedOperationCallableInterfaceStatusFromBridge(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedCallableTableBridge);
QJsonObject productionProviderReviewedCallableRuntimePreflightStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderReviewedCallableRuntimePreflightStatusFromInterface(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedOperationCallableInterface);
QJsonObject productionProviderReviewedInvocationArmingStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderReviewedInvocationArmingStatusFromRuntimePreflight(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedCallableRuntimePreflight);
QJsonObject productionProviderReviewedInvocationExecutionAcceptanceStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderReviewedInvocationExecutionAcceptanceStatusFromArming(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedInvocationArming);
QJsonObject productionProviderDataPlaneBridgeStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderDataPlaneBridgeStatusFromExecutionAcceptance(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& reviewedInvocationExecutionAcceptance);
QJsonObject productionProviderPublicPrimitiveExecutionStatusForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor);
QJsonObject productionProviderPublicPrimitiveExecutionStatusFromBridge(
    const E2ECryptoAdapterDescriptor& descriptor,
    const QJsonObject& providerDataPlaneBridge);
QJsonObject productionProviderPublicPrimitiveExecutionProbeForDescriptor(
    const E2ECryptoAdapterDescriptor& descriptor);
QString productionOperationProviderSymbol(E2ECryptoOperation operation);
QString productionOperationProviderAbiSignature(E2ECryptoOperation operation);
QJsonObject providerTableValidationStatus(const qnc_e2e_provider_table_v1* table);
qnc_e2e_provider_operation_v1 providerOperationPointer(const qnc_e2e_provider_table_v1* table,
                                                       E2ECryptoOperation operation);
ProviderDispatchResult dispatchProductionProviderOperation(E2ECryptoOperation operation,
                                                           const QByteArray& primary,
                                                           const QByteArray& secondary,
                                                           const QByteArray& aad);

QStringList productionOperationInputContract(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
        return { QStringLiteral("secure-random-source"),
                 QStringLiteral("suite-id") };
    case E2ECryptoOperation::IdentityKeyGeneration:
        return { QStringLiteral("secure-random-source"),
                 QStringLiteral("identity-suite-id") };
    case E2ECryptoOperation::PublicKeyDerivation:
        return { QStringLiteral("private-identity-material-handle"),
                 QStringLiteral("identity-suite-id") };
    case E2ECryptoOperation::AgreementSign:
        return { QStringLiteral("private-identity-material-handle"),
                 QStringLiteral("canonical-agreement-transcript") };
    case E2ECryptoOperation::AgreementVerify:
        return { QStringLiteral("peer-public-identity-material"),
                 QStringLiteral("canonical-agreement-transcript"),
                 QStringLiteral("agreement-signature") };
    case E2ECryptoOperation::SessionDerive:
        return { QStringLiteral("local-private-agreement-material-handle"),
                 QStringLiteral("local-agreement-transcript"),
                 QStringLiteral("remote-agreement-transcript") };
    case E2ECryptoOperation::PayloadEncrypt:
        return { QStringLiteral("session-key-handle"),
                 QStringLiteral("plaintext-bytes"),
                 QStringLiteral("aad"),
                 QStringLiteral("nonce-source") };
    case E2ECryptoOperation::PayloadDecrypt:
        return { QStringLiteral("session-key-handle"),
                 QStringLiteral("ciphertext-bytes"),
                 QStringLiteral("aad"),
                 QStringLiteral("nonce"),
                 QStringLiteral("authentication-tag") };
    }
    return {};
}

QStringList productionOperationOutputContract(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
        return { QStringLiteral("session-key-handle"),
                 QStringLiteral("session-key-fingerprint") };
    case E2ECryptoOperation::IdentityKeyGeneration:
        return { QStringLiteral("private-identity-material-handle"),
                 QStringLiteral("public-identity-material"),
                 QStringLiteral("identity-fingerprint") };
    case E2ECryptoOperation::PublicKeyDerivation:
        return { QStringLiteral("public-identity-material"),
                 QStringLiteral("identity-fingerprint") };
    case E2ECryptoOperation::AgreementSign:
        return { QStringLiteral("agreement-signature"),
                 QStringLiteral("signature-suite") };
    case E2ECryptoOperation::AgreementVerify:
        return { QStringLiteral("signature-verified") };
    case E2ECryptoOperation::SessionDerive:
        return { QStringLiteral("session-key-handle"),
                 QStringLiteral("session-key-fingerprint") };
    case E2ECryptoOperation::PayloadEncrypt:
        return { QStringLiteral("nonce"),
                 QStringLiteral("ciphertext-bytes"),
                 QStringLiteral("authentication-tag"),
                 QStringLiteral("envelope-header") };
    case E2ECryptoOperation::PayloadDecrypt:
        return { QStringLiteral("plaintext-bytes"),
                 QStringLiteral("authentication-verified") };
    }
    return {};
}

QString productionProbeFixtureInputClass(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
    case E2ECryptoOperation::IdentityKeyGeneration:
        return QStringLiteral("suite-bound-randomness-fixture");
    case E2ECryptoOperation::PublicKeyDerivation:
    case E2ECryptoOperation::AgreementSign:
        return QStringLiteral("handle-and-transcript-fixture");
    case E2ECryptoOperation::AgreementVerify:
        return QStringLiteral("public-identity-signature-fixture");
    case E2ECryptoOperation::SessionDerive:
        return QStringLiteral("agreement-transcript-fixture");
    case E2ECryptoOperation::PayloadEncrypt:
        return QStringLiteral("payload-encrypt-fixture");
    case E2ECryptoOperation::PayloadDecrypt:
        return QStringLiteral("payload-decrypt-fixture");
    }
    return QStringLiteral("unknown-fixture");
}

QString productionProbeExpectedMaterialPolicyClass(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
    case E2ECryptoOperation::SessionDerive:
        return QStringLiteral("handle-only");
    case E2ECryptoOperation::IdentityKeyGeneration:
    case E2ECryptoOperation::PublicKeyDerivation:
    case E2ECryptoOperation::AgreementSign:
    case E2ECryptoOperation::AgreementVerify:
        return QStringLiteral("public-export-allowed");
    case E2ECryptoOperation::PayloadEncrypt:
    case E2ECryptoOperation::PayloadDecrypt:
        return QStringLiteral("payload-bytes-allowed");
    }
    return QStringLiteral("unknown");
}

QJsonObject productionProviderProbeVectorContract(const E2ECryptoOperationSpec& spec) {
    const QStringList inputContract = productionOperationInputContract(spec.operation);
    const QStringList outputContract = productionOperationOutputContract(spec.operation);
    const QString inputContractHash =
        e2eFingerprint(inputContract.join(QLatin1Char('|')).toUtf8());
    const QString outputContractHash =
        e2eFingerprint(outputContract.join(QLatin1Char('|')).toUtf8());

    QJsonObject contract;
    contract[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-probe-vector-contract-v1");
    contract[QStringLiteral("probeVectorSchema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-probe-vector-v1");
    contract[QStringLiteral("operation")] = cryptoOperationName(spec.operation);
    contract[QStringLiteral("vectorSet")] = spec.vectorSet;
    contract[QStringLiteral("fixtureHashSha256")] = productionHarnessFixtureHash(spec);
    contract[QStringLiteral("inputContract")] = QJsonArray::fromStringList(inputContract);
    contract[QStringLiteral("outputContract")] = QJsonArray::fromStringList(outputContract);
    contract[QStringLiteral("inputContractHashSha256")] = inputContractHash;
    contract[QStringLiteral("outputContractHashSha256")] = outputContractHash;
    contract[QStringLiteral("fixtureInputClass")] =
        productionProbeFixtureInputClass(spec.operation);
    contract[QStringLiteral("expectedStatusClass")] = QStringLiteral("ok");
    contract[QStringLiteral("expectedMaterialPolicyClass")] =
        productionProbeExpectedMaterialPolicyClass(spec.operation);
    contract[QStringLiteral("expectedFailureClass")] = QStringLiteral("none");
    contract[QStringLiteral("expectedVectorResultClass")] =
        QStringLiteral("probe-vector-passed");
    contract[QStringLiteral("materialExportPolicy")] =
        QStringLiteral("sizes-and-status-only-no-secret-bytes");
    contract[QStringLiteral("contractHashReady")] =
        inputContractHash.size() == FingerprintHexLength
        && outputContractHash.size() == FingerprintHexLength;
    contract[QStringLiteral("rawKeyExported")] = false;
    contract[QStringLiteral("privateMaterialExported")] = false;
    contract[QStringLiteral("sessionSecretExported")] = false;
    contract[QStringLiteral("privateIdentityMaterialExported")] = false;
    contract[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return contract;
}

QJsonObject productionProviderProbeExecutionFrame(const E2ECryptoOperationSpec& spec,
                                                  const E2ECryptoAdapterDescriptor& descriptor,
                                                  const QJsonObject& vectorContract,
                                                  bool canInvoke,
                                                  const QString& statusClass,
                                                  const QString& outputStatusClass,
                                                  const QString& failureClass,
                                                  const QString& vectorResultClass,
                                                  qint64 primarySize,
                                                  qint64 secondarySize,
                                                  qint64 aadSize,
                                                  qint64 publicOutputSize,
                                                  qint64 sealedOutputSize) {
    QJsonObject frame;
    frame[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-probe-execution-frame-v1");
    frame[QStringLiteral("operation")] = cryptoOperationName(spec.operation);
    frame[QStringLiteral("backendId")] = descriptor.id;
    frame[QStringLiteral("providerId")] = descriptor.providerId;
    frame[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    frame[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(spec.operation);
    frame[QStringLiteral("providerAbiSignature")] =
        productionOperationProviderAbiSignature(spec.operation);
    frame[QStringLiteral("executionEntryPoint")] =
        QStringLiteral("qnc_e2e_provider_table_v1/%1")
            .arg(productionOperationProviderSymbol(spec.operation));
    frame[QStringLiteral("probeVectorSchema")] =
        vectorContract.value(QStringLiteral("probeVectorSchema")).toString();
    frame[QStringLiteral("fixtureHashSha256")] = productionHarnessFixtureHash(spec);
    frame[QStringLiteral("inputContractHashSha256")] =
        vectorContract.value(QStringLiteral("inputContractHashSha256")).toString();
    frame[QStringLiteral("outputContractHashSha256")] =
        vectorContract.value(QStringLiteral("outputContractHashSha256")).toString();
    frame[QStringLiteral("fixtureInputClass")] =
        vectorContract.value(QStringLiteral("fixtureInputClass")).toString();
    frame[QStringLiteral("suiteIdClass")] = QStringLiteral("advertised-suite-id");
    frame[QStringLiteral("primaryInputClass")] = QStringLiteral("fixed-probe-primary-fixture");
    frame[QStringLiteral("secondaryInputClass")] = QStringLiteral("fixed-probe-secondary-fixture");
    frame[QStringLiteral("aadInputClass")] = QStringLiteral("fixed-probe-aad-fixture");
    frame[QStringLiteral("primaryInputSize")] = primarySize;
    frame[QStringLiteral("secondaryInputSize")] = secondarySize;
    frame[QStringLiteral("aadInputSize")] = aadSize;
    frame[QStringLiteral("publicOutputSize")] = publicOutputSize;
    frame[QStringLiteral("sealedOutputSize")] = sealedOutputSize;
    frame[QStringLiteral("timeoutPolicy")] = QStringLiteral("bounded-explicit-test-probe");
    frame[QStringLiteral("errorPolicy")] = QStringLiteral("status-class-only");
    frame[QStringLiteral("inputCapturePolicy")] = QStringLiteral("size-and-class-only");
    frame[QStringLiteral("outputCapturePolicy")] = QStringLiteral("size-and-class-only");
    frame[QStringLiteral("resultCapturePolicy")] = QStringLiteral("status-class-and-size-only");
    frame[QStringLiteral("materialExportPolicy")] =
        QStringLiteral("sizes-and-status-only-no-secret-bytes");
    frame[QStringLiteral("operationInvoked")] = canInvoke;
    frame[QStringLiteral("inputBytesAttached")] = canInvoke;
    frame[QStringLiteral("inputBytesCaptured")] = false;
    frame[QStringLiteral("outputBytesCaptured")] = false;
    frame[QStringLiteral("resultCaptured")] = canInvoke;
    frame[QStringLiteral("callbackStatusClass")] = statusClass;
    frame[QStringLiteral("outputStatusClass")] = outputStatusClass;
    frame[QStringLiteral("failureClass")] = failureClass;
    frame[QStringLiteral("vectorResultClass")] = vectorResultClass;
    frame[QStringLiteral("sanitized")] =
        !statusClass.isEmpty()
        && !outputStatusClass.isEmpty()
        && !failureClass.isEmpty()
        && !vectorResultClass.isEmpty();
    frame[QStringLiteral("rawKeyExported")] = false;
    frame[QStringLiteral("privateMaterialExported")] = false;
    frame[QStringLiteral("sessionSecretExported")] = false;
    frame[QStringLiteral("privateIdentityMaterialExported")] = false;
    frame[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return frame;
}

QString productionProbeExpectedKnownAnswerOutputClass(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
    case E2ECryptoOperation::SessionDerive:
        return QStringLiteral("handle-status-output");
    case E2ECryptoOperation::IdentityKeyGeneration:
        return QStringLiteral("public-sealed-output-shape");
    case E2ECryptoOperation::PublicKeyDerivation:
    case E2ECryptoOperation::AgreementSign:
    case E2ECryptoOperation::AgreementVerify:
        return QStringLiteral("public-output-shape");
    case E2ECryptoOperation::PayloadEncrypt:
        return QStringLiteral("sealed-output-shape");
    case E2ECryptoOperation::PayloadDecrypt:
        return QStringLiteral("payload-output-shape");
    }
    return QStringLiteral("unknown-output-shape");
}

QString productionProbeObservedKnownAnswerOutputClass(qint64 publicOutputSize,
                                                      qint64 sealedOutputSize,
                                                      const QString& materialPolicyClass,
                                                      const QString& statusClass) {
    if (statusClass != QStringLiteral("ok")) {
        return QStringLiteral("status-error-output");
    }
    if (publicOutputSize == 0
        && materialPolicyClass == QStringLiteral("handle-only")) {
        return QStringLiteral("handle-status-output");
    }
    if (publicOutputSize > 0
        && sealedOutputSize > 0
        && materialPolicyClass == QStringLiteral("handle-only")) {
        return QStringLiteral("public-sealed-output-shape");
    }
    if (sealedOutputSize > 0) {
        return QStringLiteral("sealed-output-shape");
    }
    if (publicOutputSize > 0
        && materialPolicyClass == QStringLiteral("payload-bytes-allowed")) {
        return QStringLiteral("payload-output-shape");
    }
    if (publicOutputSize > 0) {
        return QStringLiteral("public-output-shape");
    }
    return QStringLiteral("empty-output-shape");
}

QJsonObject productionProviderProbeKnownAnswerOutputEvidence(const E2ECryptoOperationSpec& spec,
                                                             const QJsonObject& vectorContract,
                                                             bool canInvoke,
                                                             const QString& statusClass,
                                                             const QString& materialPolicyClass,
                                                             qint64 publicOutputSize,
                                                             qint64 sealedOutputSize) {
    const QString expectedOutputClass =
        productionProbeExpectedKnownAnswerOutputClass(spec.operation);
    const QString observedOutputClass = canInvoke
        ? productionProbeObservedKnownAnswerOutputClass(publicOutputSize,
                                                       sealedOutputSize,
                                                       materialPolicyClass,
                                                       statusClass)
        : QStringLiteral("not-invoked");
    QByteArray shape;
    shape.append("qtnetworkchat-e2e-production-provider-probe-output-evidence-v1|");
    shape.append(cryptoOperationName(spec.operation).toUtf8());
    shape.append('|');
    shape.append(statusClass.toUtf8());
    shape.append('|');
    shape.append(materialPolicyClass.toUtf8());
    shape.append('|');
    shape.append(QByteArray::number(publicOutputSize));
    shape.append('|');
    shape.append(QByteArray::number(sealedOutputSize));
    shape.append('|');
    shape.append(observedOutputClass.toUtf8());

    QJsonObject evidence;
    evidence[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-probe-output-evidence-v1");
    evidence[QStringLiteral("operation")] = cryptoOperationName(spec.operation);
    evidence[QStringLiteral("vectorSet")] = spec.vectorSet;
    evidence[QStringLiteral("probeVectorSchema")] =
        vectorContract.value(QStringLiteral("probeVectorSchema")).toString();
    evidence[QStringLiteral("fixtureHashSha256")] = productionHarnessFixtureHash(spec);
    evidence[QStringLiteral("inputContractHashSha256")] =
        vectorContract.value(QStringLiteral("inputContractHashSha256")).toString();
    evidence[QStringLiteral("outputContractHashSha256")] =
        vectorContract.value(QStringLiteral("outputContractHashSha256")).toString();
    evidence[QStringLiteral("expectedKnownAnswerOutputClass")] = expectedOutputClass;
    evidence[QStringLiteral("observedKnownAnswerOutputClass")] = observedOutputClass;
    const bool matched = canInvoke && observedOutputClass == expectedOutputClass;
    evidence[QStringLiteral("expectedOutputClassMatched")] = matched;
    evidence[QStringLiteral("outputEvidenceClass")] = matched
        ? QStringLiteral("known-answer-output-shape-matched")
        : (canInvoke
            ? QStringLiteral("known-answer-output-shape-mismatch")
            : QStringLiteral("known-answer-output-not-invoked"));
    evidence[QStringLiteral("outputEvidenceFailClosed")] = !matched;
    evidence[QStringLiteral("outputEvidenceBlockedReason")] = matched
        ? QString()
        : (canInvoke
            ? QStringLiteral("known-answer-output-shape-mismatch")
            : QStringLiteral("production-provider-probe-not-invoked"));
    evidence[QStringLiteral("outputShapeHashSha256")] = e2eFingerprint(shape);
    evidence[QStringLiteral("publicOutputSize")] = publicOutputSize;
    evidence[QStringLiteral("sealedOutputSize")] = sealedOutputSize;
    evidence[QStringLiteral("statusClass")] = statusClass;
    evidence[QStringLiteral("materialPolicyClass")] = materialPolicyClass;
    evidence[QStringLiteral("outputBytesCaptured")] = false;
    evidence[QStringLiteral("materialExportProof")] =
        QStringLiteral("output-size-and-class-only-no-secret-bytes");
    evidence[QStringLiteral("rawKeyExported")] = false;
    evidence[QStringLiteral("privateMaterialExported")] = false;
    evidence[QStringLiteral("sessionSecretExported")] = false;
    evidence[QStringLiteral("privateIdentityMaterialExported")] = false;
    evidence[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return evidence;
}

QString productionOperationSlotId(E2ECryptoOperation operation) {
    return QStringLiteral("openssl-reviewed-adapter-v1/%1-slot").arg(cryptoOperationName(operation));
}

QString productionOperationProviderSymbol(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
        return QString::fromLatin1(QNC_E2E_PROVIDER_SYMBOL_SESSION_KEY_GENERATION);
    case E2ECryptoOperation::IdentityKeyGeneration:
        return QString::fromLatin1(QNC_E2E_PROVIDER_SYMBOL_IDENTITY_KEY_GENERATION);
    case E2ECryptoOperation::PublicKeyDerivation:
        return QString::fromLatin1(QNC_E2E_PROVIDER_SYMBOL_PUBLIC_KEY_DERIVATION);
    case E2ECryptoOperation::AgreementSign:
        return QString::fromLatin1(QNC_E2E_PROVIDER_SYMBOL_AGREEMENT_SIGN);
    case E2ECryptoOperation::AgreementVerify:
        return QString::fromLatin1(QNC_E2E_PROVIDER_SYMBOL_AGREEMENT_VERIFY);
    case E2ECryptoOperation::SessionDerive:
        return QString::fromLatin1(QNC_E2E_PROVIDER_SYMBOL_SESSION_DERIVE);
    case E2ECryptoOperation::PayloadEncrypt:
        return QString::fromLatin1(QNC_E2E_PROVIDER_SYMBOL_PAYLOAD_ENCRYPT);
    case E2ECryptoOperation::PayloadDecrypt:
        return QString::fromLatin1(QNC_E2E_PROVIDER_SYMBOL_PAYLOAD_DECRYPT);
    }
    return QStringLiteral("qnc_e2e_op_unknown_v1");
}

QString productionOperationProviderAbiSignature(E2ECryptoOperation operation) {
    return QStringLiteral("qnc_e2e_status_t %1(const qnc_e2e_operation_input_v1*, qnc_e2e_operation_output_v1*)")
        .arg(productionOperationProviderSymbol(operation));
}

QString productionOperationMigrationPhase(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
        return QStringLiteral("session-bootstrap");
    case E2ECryptoOperation::IdentityKeyGeneration:
    case E2ECryptoOperation::PublicKeyDerivation:
        return QStringLiteral("identity-bootstrap");
    case E2ECryptoOperation::AgreementSign:
    case E2ECryptoOperation::AgreementVerify:
        return QStringLiteral("identity-bound-agreement");
    case E2ECryptoOperation::SessionDerive:
        return QStringLiteral("session-rotation");
    case E2ECryptoOperation::PayloadEncrypt:
    case E2ECryptoOperation::PayloadDecrypt:
        return QStringLiteral("payload-data-plane");
    }
    return QStringLiteral("unknown");
}

QString productionOperationPublicApi(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
        return QStringLiteral("generateE2ESessionKey");
    case E2ECryptoOperation::IdentityKeyGeneration:
        return QStringLiteral("generateE2EPrivateKey");
    case E2ECryptoOperation::PublicKeyDerivation:
        return QStringLiteral("e2ePublicKeyFromPrivateKey");
    case E2ECryptoOperation::AgreementSign:
        return QStringLiteral("signE2EKeyAgreement");
    case E2ECryptoOperation::AgreementVerify:
        return QStringLiteral("verifyE2EKeyAgreementSignature");
    case E2ECryptoOperation::SessionDerive:
        return QStringLiteral("deriveE2EAuthenticatedSessionKey");
    case E2ECryptoOperation::PayloadEncrypt:
        return QStringLiteral("encryptE2EPayload");
    case E2ECryptoOperation::PayloadDecrypt:
        return QStringLiteral("decryptE2EPayload");
    }
    return QStringLiteral("unknown");
}

QString productionOperationPublicDataPlaneBoundary(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
        return QStringLiteral("session-key-bootstrap");
    case E2ECryptoOperation::IdentityKeyGeneration:
        return QStringLiteral("local-identity-bootstrap");
    case E2ECryptoOperation::PublicKeyDerivation:
        return QStringLiteral("identity-public-announcement");
    case E2ECryptoOperation::AgreementSign:
        return QStringLiteral("authenticated-key-agreement-sign");
    case E2ECryptoOperation::AgreementVerify:
        return QStringLiteral("authenticated-key-agreement-verify");
    case E2ECryptoOperation::SessionDerive:
        return QStringLiteral("authenticated-session-derive");
    case E2ECryptoOperation::PayloadEncrypt:
        return QStringLiteral("payload-envelope-encrypt");
    case E2ECryptoOperation::PayloadDecrypt:
        return QStringLiteral("payload-envelope-decrypt");
    }
    return QStringLiteral("unknown");
}

QJsonObject productionOperationSlotContract(const E2ECryptoAdapterDescriptor& descriptor,
                                            const E2ECryptoOperationSpec& spec,
                                            const QJsonObject& harnessOperation) {
    const bool harnessRunnable = harnessOperation.value(QStringLiteral("runnable")).toBool(false);
    QJsonObject slot;
    slot[QStringLiteral("operation")] = cryptoOperationName(spec.operation);
    slot[QStringLiteral("slotId")] = productionOperationSlotId(spec.operation);
    slot[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(spec.operation);
    slot[QStringLiteral("backendId")] = descriptor.id;
    slot[QStringLiteral("providerId")] = descriptor.providerId;
    slot[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    slot[QStringLiteral("migrationPhase")] = productionOperationMigrationPhase(spec.operation);
    slot[QStringLiteral("implementationState")] = spec.implementationState;
    slot[QStringLiteral("reviewState")] = spec.implemented
        ? QStringLiteral("reviewed")
        : (descriptor.linked ? QStringLiteral("placeholder-linked") : QStringLiteral("not-linked"));
    slot[QStringLiteral("reviewed")] = spec.implemented;
    slot[QStringLiteral("callable")] = harnessRunnable;
    slot[QStringLiteral("harnessRunnable")] = harnessRunnable;
    slot[QStringLiteral("knownAnswerPassed")] = spec.knownAnswerPassed;
    slot[QStringLiteral("roundTripPassed")] = spec.roundTripPassed;
    slot[QStringLiteral("vectorSet")] = spec.vectorSet;
    slot[QStringLiteral("fixtureHashSha256")] =
        harnessOperation.value(QStringLiteral("fixtureHashSha256")).toString(
            productionHarnessFixtureHash(spec));
    slot[QStringLiteral("materialPolicy")] =
        (spec.operation == E2ECryptoOperation::PayloadEncrypt
         || spec.operation == E2ECryptoOperation::PayloadDecrypt)
            ? QStringLiteral("payload-bytes-allowed-no-key-export")
            : QStringLiteral("handle-based-no-private-material-export");
    slot[QStringLiteral("sideEffectPolicy")] =
        (spec.operation == E2ECryptoOperation::IdentityKeyGeneration
         || spec.operation == E2ECryptoOperation::SessionKeyGeneration)
            ? QStringLiteral("may-create-key-handle")
            : QStringLiteral("pure-or-authenticated-transform");
    slot[QStringLiteral("blockedReason")] = harnessRunnable
        ? QString()
        : harnessOperation.value(QStringLiteral("blockedReason")).toString(spec.migrationBlocker);
    slot[QStringLiteral("operatorAction")] = harnessRunnable
        ? QStringLiteral("none")
        : harnessOperation.value(QStringLiteral("operatorAction")).toString(spec.operatorAction);
    slot[QStringLiteral("rawKeyExported")] = false;
    slot[QStringLiteral("privateMaterialExported")] = false;
    return slot;
}

QJsonObject productionOperationInvocationContract(const E2ECryptoAdapterDescriptor& descriptor,
                                                  const E2ECryptoOperationSpec& spec,
                                                  const QJsonObject& harnessOperation) {
    const bool harnessRunnable = harnessOperation.value(QStringLiteral("runnable")).toBool(false);
    const QString operationName = cryptoOperationName(spec.operation);
    QJsonObject invocation;
    invocation[QStringLiteral("operation")] = operationName;
    invocation[QStringLiteral("entrypoint")] = descriptor.type + QStringLiteral("/") + operationName;
    invocation[QStringLiteral("providerId")] = descriptor.providerId;
    invocation[QStringLiteral("backendId")] = descriptor.id;
    invocation[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    invocation[QStringLiteral("inputContract")] =
        QJsonArray::fromStringList(productionOperationInputContract(spec.operation));
    invocation[QStringLiteral("outputContract")] =
        QJsonArray::fromStringList(productionOperationOutputContract(spec.operation));
    invocation[QStringLiteral("fixtureHashSha256")] =
        harnessOperation.value(QStringLiteral("fixtureHashSha256")).toString(
            productionHarnessFixtureHash(spec));
    invocation[QStringLiteral("operationSlot")] =
        productionOperationSlotContract(descriptor, spec, harnessOperation);
    invocation[QStringLiteral("vectorSet")] = spec.vectorSet;
    invocation[QStringLiteral("implementationState")] = spec.implementationState;
    invocation[QStringLiteral("compatibilityStatus")] = spec.compatibilityStatus;
    invocation[QStringLiteral("knownAnswerPassed")] = spec.knownAnswerPassed;
    invocation[QStringLiteral("roundTripPassed")] = spec.roundTripPassed;
    invocation[QStringLiteral("requiresHarnessRunnable")] = true;
    invocation[QStringLiteral("harnessRunnable")] = harnessRunnable;
    invocation[QStringLiteral("callable")] = harnessRunnable;
    invocation[QStringLiteral("invocationState")] = harnessRunnable
        ? QStringLiteral("callable")
        : (descriptor.linked
            ? QStringLiteral("blocked-linked-placeholder")
            : QStringLiteral("blocked-not-linked"));
    invocation[QStringLiteral("blockedReason")] = harnessRunnable
        ? QString()
        : harnessOperation.value(QStringLiteral("blockedReason")).toString(spec.migrationBlocker);
    invocation[QStringLiteral("operatorAction")] = harnessRunnable
        ? QStringLiteral("none")
        : harnessOperation.value(QStringLiteral("operatorAction")).toString(spec.operatorAction);
    invocation[QStringLiteral("rawKeyExported")] = false;
    invocation[QStringLiteral("privateMaterialExported")] = false;
    invocation[QStringLiteral("plaintextExportedByContract")] =
        spec.operation == E2ECryptoOperation::PayloadDecrypt;
    invocation[QStringLiteral("privateMaterialInputByHandle")] =
        productionOperationInputContract(spec.operation).join(QLatin1Char('|')).contains(
            QStringLiteral("private"), Qt::CaseInsensitive)
        || productionOperationInputContract(spec.operation).join(QLatin1Char('|')).contains(
            QStringLiteral("session-key-handle"), Qt::CaseInsensitive);
    return invocation;
}

QJsonObject productionOperationSlotStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject harness = productionOperationHarnessStatusForDescriptor(descriptor);
    const QJsonArray harnessOperations = harness.value(QStringLiteral("operations")).toArray();
    QHash<QString, QJsonObject> harnessByOperation;
    for (const QJsonValue& value : harnessOperations) {
        const QJsonObject operation = value.toObject();
        harnessByOperation.insert(operation.value(QStringLiteral("operation")).toString(), operation);
    }

    QJsonArray slotArray;
    int reviewedSlotCount = 0;
    int callableSlotCount = 0;
    int blockedSlotCount = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const QString operationName = cryptoOperationName(spec.operation);
        const QJsonObject slot =
            productionOperationSlotContract(descriptor, spec, harnessByOperation.value(operationName));
        slotArray.append(slot);
        if (slot.value(QStringLiteral("reviewed")).toBool(false)) {
            ++reviewedSlotCount;
        }
        if (slot.value(QStringLiteral("callable")).toBool(false)) {
            ++callableSlotCount;
        } else {
            ++blockedSlotCount;
        }
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && reviewedSlotCount == cryptoOperations().size()
        && callableSlotCount == cryptoOperations().size()
        && blockedSlotCount == 0;
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-operation-slots-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredSlotCount")] = cryptoOperations().size();
    status[QStringLiteral("reviewedSlotCount")] = reviewedSlotCount;
    status[QStringLiteral("callableSlotCount")] = callableSlotCount;
    status[QStringLiteral("blockedSlotCount")] = blockedSlotCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-operation-slots-ready")
        : (descriptor.linked
            ? QStringLiteral("production-operation-slots-blocked-placeholder")
            : QStringLiteral("production-operation-slots-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : (descriptor.linked
            ? QStringLiteral("production-operation-slots-not-reviewed")
            : QStringLiteral("production-crypto-backend-unavailable"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("replace-placeholder-slots-with-reviewed-operations")
            : QStringLiteral("link-reviewed-production-crypto-backend"));
    status[QStringLiteral("slots")] = slotArray;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    return status;
}

QJsonObject productionOperationDispatchBindingStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject slotStatus = productionOperationSlotStatusForDescriptor(descriptor);
    const QJsonArray slotItems = slotStatus.value(QStringLiteral("slots")).toArray();

    QJsonArray bindings;
    int reviewedBindingCount = 0;
    int callableBindingCount = 0;
    int blockedBindingCount = 0;
    for (const QJsonValue& value : slotItems) {
        const QJsonObject slot = value.toObject();
        const QString operationName = slot.value(QStringLiteral("operation")).toString();
        const bool reviewed = slot.value(QStringLiteral("reviewed")).toBool(false);
        const bool callable = slot.value(QStringLiteral("callable")).toBool(false)
            && reviewed;
        const QString bindingState = callable
            ? QStringLiteral("reviewed-bound")
            : (descriptor.linked
                ? QStringLiteral("linked-placeholder")
                : QStringLiteral("not-linked"));

        QJsonObject binding;
        binding[QStringLiteral("operation")] = operationName;
        binding[QStringLiteral("slotId")] = slot.value(QStringLiteral("slotId")).toString();
        binding[QStringLiteral("providerSymbol")] =
            slot.value(QStringLiteral("providerSymbol")).toString();
        binding[QStringLiteral("backendId")] = descriptor.id;
        binding[QStringLiteral("providerId")] = descriptor.providerId;
        binding[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        binding[QStringLiteral("bindingState")] = bindingState;
        binding[QStringLiteral("dispatchCallable")] = callable;
        binding[QStringLiteral("reviewed")] = reviewed;
        const E2ECryptoOperation operation = cryptoOperations().at(bindings.size());
        binding[QStringLiteral("expectedSignature")] =
            productionOperationProviderAbiSignature(operation);
        binding[QStringLiteral("inputContract")] =
            QJsonArray::fromStringList(productionOperationInputContract(operation));
        binding[QStringLiteral("outputContract")] =
            QJsonArray::fromStringList(productionOperationOutputContract(operation));
        binding[QStringLiteral("fixtureHashSha256")] =
            slot.value(QStringLiteral("fixtureHashSha256")).toString();
        binding[QStringLiteral("vectorSet")] = slot.value(QStringLiteral("vectorSet")).toString();
        binding[QStringLiteral("migrationPhase")] =
            slot.value(QStringLiteral("migrationPhase")).toString();
        binding[QStringLiteral("materialPolicy")] =
            slot.value(QStringLiteral("materialPolicy")).toString();
        binding[QStringLiteral("sideEffectPolicy")] =
            slot.value(QStringLiteral("sideEffectPolicy")).toString();
        binding[QStringLiteral("blockedReason")] = callable
            ? QString()
            : (descriptor.linked
                ? QStringLiteral("production-dispatch-binding-placeholder")
                : QStringLiteral("production-crypto-backend-unavailable"));
        binding[QStringLiteral("operatorAction")] = callable
            ? QStringLiteral("none")
            : (descriptor.linked
                ? QStringLiteral("bind-reviewed-provider-symbols-to-dispatch-table")
                : QStringLiteral("link-reviewed-production-crypto-backend"));
        binding[QStringLiteral("rawKeyExported")] = false;
        binding[QStringLiteral("privateMaterialExported")] = false;
        bindings.append(binding);

        if (reviewed) {
            ++reviewedBindingCount;
        }
        if (callable) {
            ++callableBindingCount;
        } else {
            ++blockedBindingCount;
        }
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && reviewedBindingCount == cryptoOperations().size()
        && callableBindingCount == cryptoOperations().size()
        && blockedBindingCount == 0;
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-operation-dispatch-bindings-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredBindingCount")] = cryptoOperations().size();
    status[QStringLiteral("reviewedBindingCount")] = reviewedBindingCount;
    status[QStringLiteral("callableBindingCount")] = callableBindingCount;
    status[QStringLiteral("blockedBindingCount")] = blockedBindingCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-operation-dispatch-bindings-ready")
        : (descriptor.linked
            ? QStringLiteral("production-operation-dispatch-bindings-blocked-placeholder")
            : QStringLiteral("production-operation-dispatch-bindings-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : (descriptor.linked
            ? QStringLiteral("production-operation-dispatch-bindings-not-reviewed")
            : QStringLiteral("production-crypto-backend-unavailable"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("bind-reviewed-provider-symbols-to-dispatch-table")
            : QStringLiteral("link-reviewed-production-crypto-backend"));
    status[QStringLiteral("operationSlotsReleaseGate")] =
        slotStatus.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("operationSlotsAccepted")] =
        slotStatus.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("bindings")] = bindings;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    return status;
}

QJsonObject productionOperationCallableManifestForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject dispatchBindings =
        productionOperationDispatchBindingStatusForDescriptor(descriptor);
    const QJsonArray bindingItems = dispatchBindings.value(QStringLiteral("bindings")).toArray();

    QJsonArray callableEntries;
    int reviewedCallableCount = 0;
    int blockedCallableCount = 0;
    int abiMismatchCount = 0;
    int fixtureMismatchCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QString operationName = cryptoOperationName(operation);
        const QJsonObject binding = sequenceIndex < bindingItems.size()
            ? bindingItems.at(sequenceIndex).toObject()
            : QJsonObject();
        const QString expectedSymbol = productionOperationProviderSymbol(operation);
        const QString expectedSignature = productionOperationProviderAbiSignature(operation);
        const QString expectedFixture = productionHarnessFixtureHash(spec);
        const bool symbolMatches = binding.value(QStringLiteral("providerSymbol")).toString() == expectedSymbol;
        const bool signatureMatches =
            binding.value(QStringLiteral("expectedSignature")).toString() == expectedSignature;
        const bool fixtureMatches =
            binding.value(QStringLiteral("fixtureHashSha256")).toString() == expectedFixture;
        const bool reviewed = binding.value(QStringLiteral("reviewed")).toBool(false)
            && spec.implemented;
        const bool callable = binding.value(QStringLiteral("dispatchCallable")).toBool(false)
            && reviewed
            && symbolMatches
            && signatureMatches
            && fixtureMatches
            && spec.knownAnswerPassed
            && (spec.roundTripPassed
                || (operation != E2ECryptoOperation::PayloadEncrypt
                    && operation != E2ECryptoOperation::PayloadDecrypt));

        QJsonObject entry;
        entry[QStringLiteral("sequenceIndex")] = sequenceIndex;
        entry[QStringLiteral("operation")] = operationName;
        entry[QStringLiteral("backendId")] = descriptor.id;
        entry[QStringLiteral("providerId")] = descriptor.providerId;
        entry[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        entry[QStringLiteral("slotId")] = productionOperationSlotId(operation);
        entry[QStringLiteral("providerSymbol")] = expectedSymbol;
        entry[QStringLiteral("providerAbiSignature")] = expectedSignature;
        entry[QStringLiteral("entrypoint")] = descriptor.type + QStringLiteral("/") + operationName;
        entry[QStringLiteral("bindingState")] =
            binding.value(QStringLiteral("bindingState")).toString(
                descriptor.linked ? QStringLiteral("linked-placeholder") : QStringLiteral("not-linked"));
        entry[QStringLiteral("callable")] = callable;
        entry[QStringLiteral("reviewed")] = reviewed;
        entry[QStringLiteral("dispatchCallable")] =
            binding.value(QStringLiteral("dispatchCallable")).toBool(false);
        entry[QStringLiteral("symbolMatches")] = symbolMatches;
        entry[QStringLiteral("abiSignatureMatches")] = signatureMatches;
        entry[QStringLiteral("fixtureHashMatches")] = fixtureMatches;
        entry[QStringLiteral("implementationState")] = spec.implementationState;
        entry[QStringLiteral("compatibilityStatus")] = spec.compatibilityStatus;
        entry[QStringLiteral("knownAnswerPassed")] = spec.knownAnswerPassed;
        entry[QStringLiteral("roundTripPassed")] = spec.roundTripPassed;
        entry[QStringLiteral("fixtureHashSha256")] = expectedFixture;
        entry[QStringLiteral("inputContract")] =
            QJsonArray::fromStringList(productionOperationInputContract(operation));
        entry[QStringLiteral("outputContract")] =
            QJsonArray::fromStringList(productionOperationOutputContract(operation));
        entry[QStringLiteral("materialPolicy")] =
            binding.value(QStringLiteral("materialPolicy")).toString(
                (operation == E2ECryptoOperation::PayloadEncrypt
                 || operation == E2ECryptoOperation::PayloadDecrypt)
                    ? QStringLiteral("payload-bytes-allowed-no-key-export")
                    : QStringLiteral("handle-based-no-private-material-export"));
        entry[QStringLiteral("blockedReason")] = callable
            ? QString()
            : (descriptor.linked
                ? QStringLiteral("production-callable-manifest-placeholder")
                : QStringLiteral("production-crypto-backend-unavailable"));
        entry[QStringLiteral("operatorAction")] = callable
            ? QStringLiteral("none")
            : (descriptor.linked
                ? QStringLiteral("bind-reviewed-callables-and-run-production-vectors")
                : QStringLiteral("link-reviewed-production-crypto-backend"));
        entry[QStringLiteral("rawKeyExported")] = false;
        entry[QStringLiteral("privateMaterialExported")] = false;
        callableEntries.append(entry);

        if (callable) {
            ++reviewedCallableCount;
        } else {
            ++blockedCallableCount;
        }
        if (!signatureMatches) {
            ++abiMismatchCount;
        }
        if (!fixtureMatches) {
            ++fixtureMismatchCount;
        }
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && reviewedCallableCount == cryptoOperations().size()
        && blockedCallableCount == 0
        && abiMismatchCount == 0
        && fixtureMismatchCount == 0
        && dispatchBindings.value(QStringLiteral("accepted")).toBool(false);
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-operation-callable-manifest-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredCallableCount")] = cryptoOperations().size();
    status[QStringLiteral("reviewedCallableCount")] = reviewedCallableCount;
    status[QStringLiteral("blockedCallableCount")] = blockedCallableCount;
    status[QStringLiteral("abiMismatchCount")] = abiMismatchCount;
    status[QStringLiteral("fixtureMismatchCount")] = fixtureMismatchCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-operation-callable-manifest-ready")
        : (descriptor.linked
            ? QStringLiteral("production-operation-callable-manifest-blocked-placeholder")
            : QStringLiteral("production-operation-callable-manifest-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : (descriptor.linked
            ? QStringLiteral("production-callable-manifest-not-reviewed")
            : QStringLiteral("production-crypto-backend-unavailable"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("replace-placeholder-callables-with-reviewed-provider-table")
            : QStringLiteral("link-reviewed-production-crypto-backend"));
    status[QStringLiteral("operationDispatchBindingsReleaseGate")] =
        dispatchBindings.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("operationDispatchBindingsAccepted")] =
        dispatchBindings.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("callables")] = callableEntries;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    return status;
}

QJsonObject productionOperationExecutionResultForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject callableManifest =
        productionOperationCallableManifestForDescriptor(descriptor);
    const QJsonArray callableItems = callableManifest.value(QStringLiteral("callables")).toArray();
    const QJsonObject providerInvocationResult =
        productionProviderInvocationResultStatusForDescriptor(descriptor);
    const QJsonArray providerResults =
        providerInvocationResult.value(QStringLiteral("results")).toArray();

    QJsonArray results;
    int passedResultCount = 0;
    int blockedResultCount = 0;
    int sanitizedResultCount = 0;
    int outputContractMismatchCount = 0;
    int invokedOperationCount = 0;
    int capturedResultCount = 0;
    int providerProbeVectorMatchedCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QString operationName = cryptoOperationName(operation);
        const QJsonObject callable = sequenceIndex < callableItems.size()
            ? callableItems.at(sequenceIndex).toObject()
            : QJsonObject();
        const QJsonObject providerResult = sequenceIndex < providerResults.size()
            ? providerResults.at(sequenceIndex).toObject()
            : QJsonObject();
        const QJsonObject providerProbe =
            providerResult.value(QStringLiteral("providerProbeEvidence")).toObject();
        const bool callableReady = callable.value(QStringLiteral("callable")).toBool(false);
        const bool reviewed = callable.value(QStringLiteral("reviewed")).toBool(false);
        const bool providerCaptureReady =
            providerResult.value(QStringLiteral("captureReady")).toBool(false);
        const bool providerInvoked =
            providerResult.value(QStringLiteral("operationInvoked")).toBool(false);
        const bool providerCaptured =
            providerResult.value(QStringLiteral("resultCaptured")).toBool(false);
        const bool providerProbeVectorMatched =
            providerResult.value(QStringLiteral("providerProbeVectorMatched")).toBool(false);
        const bool knownAnswerPassed =
            providerResult.value(QStringLiteral("knownAnswerPassed")).toBool(
                providerProbe.value(QStringLiteral("knownAnswerPassed")).toBool(
                    callable.value(QStringLiteral("knownAnswerPassed")).toBool(false)));
        const bool roundTripPassed =
            providerResult.value(QStringLiteral("roundTripPassed")).toBool(
                providerProbe.value(QStringLiteral("roundTripPassed")).toBool(
                    callable.value(QStringLiteral("roundTripPassed")).toBool(false)));
        const bool outputContractMatched = callable.value(QStringLiteral("outputContract")).toArray().size()
            == productionOperationOutputContract(operation).size();
        const bool providerOutputContractMatched =
            providerResult.value(QStringLiteral("outputContractMatched")).toBool(false);
        const bool providerFixtureMatched =
            providerResult.value(QStringLiteral("fixtureHashMatched")).toBool(false);
        const bool providerSanitized =
            providerResult.value(QStringLiteral("sanitized")).toBool(false)
            && !providerResult.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !providerResult.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !providerResult.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !providerResult.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !providerResult.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool callableSanitized = !callable.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !callable.value(QStringLiteral("privateMaterialExported")).toBool(true);
        const bool sanitized = callableSanitized
            && (!providerInvoked || providerSanitized);
        const bool passed = reviewed
            && providerCaptureReady
            && providerInvoked
            && providerCaptured
            && providerProbeVectorMatched
            && knownAnswerPassed
            && outputContractMatched
            && providerOutputContractMatched
            && providerFixtureMatched
            && sanitized;

        QJsonObject result;
        result[QStringLiteral("sequenceIndex")] = sequenceIndex;
        result[QStringLiteral("operation")] = operationName;
        result[QStringLiteral("backendId")] = descriptor.id;
        result[QStringLiteral("providerId")] = descriptor.providerId;
        result[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        result[QStringLiteral("entrypoint")] = descriptor.type + QStringLiteral("/") + operationName;
        result[QStringLiteral("providerSymbol")] =
            callable.value(QStringLiteral("providerSymbol")).toString(
                productionOperationProviderSymbol(operation));
        result[QStringLiteral("providerAbiSignature")] =
            callable.value(QStringLiteral("providerAbiSignature")).toString(
                productionOperationProviderAbiSignature(operation));
        result[QStringLiteral("fixtureHashSha256")] =
            callable.value(QStringLiteral("fixtureHashSha256")).toString(
                productionHarnessFixtureHash(spec));
        result[QStringLiteral("providerInvocationResult")] = providerResult;
        result[QStringLiteral("providerInvocationResultReleaseGate")] =
            providerInvocationResult.value(QStringLiteral("releaseGate")).toString();
        result[QStringLiteral("providerInvocationResultAccepted")] =
            providerInvocationResult.value(QStringLiteral("accepted")).toBool(false);
        result[QStringLiteral("providerProbeEvidence")] = providerProbe;
        result[QStringLiteral("providerProbeVectorMatched")] = providerProbeVectorMatched;
        result[QStringLiteral("providerProbeOutputShapeHashSha256")] =
            providerResult.value(QStringLiteral("providerProbeOutputShapeHashSha256")).toString(
                providerProbe.value(QStringLiteral("outputShapeHashSha256")).toString());
        result[QStringLiteral("callable")] = callableReady;
        result[QStringLiteral("reviewed")] = reviewed;
        result[QStringLiteral("knownAnswerPassed")] = knownAnswerPassed;
        result[QStringLiteral("roundTripPassed")] = roundTripPassed;
        result[QStringLiteral("outputContractMatched")] = outputContractMatched;
        result[QStringLiteral("providerOutputContractMatched")] = providerOutputContractMatched;
        result[QStringLiteral("providerFixtureHashMatched")] = providerFixtureMatched;
        result[QStringLiteral("providerCaptureReady")] = providerCaptureReady;
        result[QStringLiteral("operationInvoked")] = providerInvoked;
        result[QStringLiteral("resultCaptured")] = providerCaptured;
        result[QStringLiteral("sanitized")] = sanitized;
        result[QStringLiteral("passed")] = passed;
        result[QStringLiteral("resultState")] = passed
            ? QStringLiteral("passed-reviewed-production-result")
            : (descriptor.linked
                ? (providerInvoked
                    ? QStringLiteral("blocked-linked-provider-result-mismatch")
                    : QStringLiteral("blocked-linked-placeholder"))
                : QStringLiteral("blocked-not-linked"));
        result[QStringLiteral("errorClass")] = passed
            ? QString()
            : (descriptor.linked
                ? providerResult.value(QStringLiteral("expectedErrorClass")).toString(
                    QStringLiteral("production-result-placeholder-not-executed"))
                : QStringLiteral("production-result-adapter-not-linked"));
        result[QStringLiteral("blockedReason")] = passed
            ? QString()
            : (descriptor.linked
                ? providerResult.value(QStringLiteral("blockedReason")).toString(
                    QStringLiteral("production-operation-result-placeholder"))
                : QStringLiteral("production-crypto-backend-unavailable"));
        result[QStringLiteral("operatorAction")] = passed
            ? QStringLiteral("none")
            : (descriptor.linked
                ? providerResult.value(QStringLiteral("operatorAction")).toString(
                    QStringLiteral("execute-reviewed-provider-operation-and-record-sanitized-result"))
                : QStringLiteral("link-reviewed-production-crypto-backend"));
        result[QStringLiteral("inputContract")] =
            QJsonArray::fromStringList(productionOperationInputContract(operation));
        result[QStringLiteral("outputContract")] =
            QJsonArray::fromStringList(productionOperationOutputContract(operation));
        result[QStringLiteral("resultContract")] = QJsonArray::fromStringList({
            QStringLiteral("status-code"),
            QStringLiteral("output-contract-proof"),
            QStringLiteral("fixture-hash-proof"),
            QStringLiteral("material-export-proof"),
        });
        result[QStringLiteral("materialPolicy")] =
            callable.value(QStringLiteral("materialPolicy")).toString(
                (operation == E2ECryptoOperation::PayloadEncrypt
                 || operation == E2ECryptoOperation::PayloadDecrypt)
                    ? QStringLiteral("payload-bytes-allowed-no-key-export")
                    : QStringLiteral("handle-based-no-private-material-export"));
        result[QStringLiteral("plaintextExportedByContract")] =
            operation == E2ECryptoOperation::PayloadDecrypt;
        result[QStringLiteral("rawKeyExported")] = false;
        result[QStringLiteral("privateMaterialExported")] = false;
        result[QStringLiteral("sessionSecretExported")] = false;
        result[QStringLiteral("privateIdentityMaterialExported")] = false;
        result[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        results.append(result);

        if (passed) {
            ++passedResultCount;
        } else {
            ++blockedResultCount;
        }
        if (sanitized) {
            ++sanitizedResultCount;
        }
        if (!outputContractMatched) {
            ++outputContractMismatchCount;
        }
        if (providerInvoked) {
            ++invokedOperationCount;
        }
        if (providerCaptured) {
            ++capturedResultCount;
        }
        if (providerProbeVectorMatched) {
            ++providerProbeVectorMatchedCount;
        }
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && providerInvocationResult.value(QStringLiteral("accepted")).toBool(false)
        && passedResultCount == cryptoOperations().size()
        && blockedResultCount == 0
        && outputContractMismatchCount == 0
        && sanitizedResultCount == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-operation-execution-result-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerInvocationResult")] = providerInvocationResult;
    status[QStringLiteral("providerInvocationResultReleaseGate")] =
        providerInvocationResult.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerInvocationResultAccepted")] =
        providerInvocationResult.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredResultCount")] = cryptoOperations().size();
    status[QStringLiteral("passedResultCount")] = passedResultCount;
    status[QStringLiteral("blockedResultCount")] = blockedResultCount;
    status[QStringLiteral("sanitizedResultCount")] = sanitizedResultCount;
    status[QStringLiteral("outputContractMismatchCount")] = outputContractMismatchCount;
    status[QStringLiteral("invokedOperationCount")] = invokedOperationCount;
    status[QStringLiteral("capturedResultCount")] = capturedResultCount;
    status[QStringLiteral("providerProbeVectorMatchedCount")] =
        providerProbeVectorMatchedCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-operation-execution-results-ready")
        : (descriptor.linked
            ? QStringLiteral("production-operation-execution-results-blocked-placeholder")
            : QStringLiteral("production-operation-execution-results-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : (descriptor.linked
            ? providerInvocationResult.value(QStringLiteral("blockedReason")).toString(
                QStringLiteral("production-operation-results-not-executed"))
            : QStringLiteral("production-crypto-backend-unavailable"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? providerInvocationResult.value(QStringLiteral("operatorAction")).toString(
                QStringLiteral("run-reviewed-production-operations-and-store-sanitized-results"))
            : QStringLiteral("link-reviewed-production-crypto-backend"));
    status[QStringLiteral("operationCallableManifestReleaseGate")] =
        callableManifest.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("operationCallableManifestAccepted")] =
        callableManifest.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("results")] = results;
    status[QStringLiteral("operationInvoked")] = invokedOperationCount > 0;
    status[QStringLiteral("resultCaptured")] = capturedResultCount > 0;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QStringList configuredProductionProviderSymbols() {
    const QStringList configured =
        QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_REQUIRED_SYMBOLS)
            .split(QLatin1Char(','), Qt::SkipEmptyParts);
    QStringList symbols;
    for (const QString& symbol : configured) {
        symbols.append(symbol.trimmed());
    }
    return symbols;
}

QJsonObject productionProviderTableStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject callableManifest =
        productionOperationCallableManifestForDescriptor(descriptor);
    const QJsonArray callables = callableManifest.value(QStringLiteral("callables")).toArray();
    const QStringList configuredSymbols = configuredProductionProviderSymbols();
    const qnc_e2e_provider_table_v1* registeredTable = activeProductionProviderTable();
    const QJsonObject tableValidation = providerTableValidationStatus(registeredTable);
    const bool tableValidationAccepted =
        tableValidation.value(QStringLiteral("accepted")).toBool(false);
    const bool tableBound = QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_BOUND != 0;
    const bool abiMatchesHeader =
        QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_ABI)
            == QString::fromLatin1(QNC_E2E_PROVIDER_TABLE_ABI);
    const bool operationCountMatchesHeader =
        cryptoOperations().size() == QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT;

    QJsonArray entries;
    int requiredSymbolCount = 0;
    int boundSymbolCount = 0;
    int missingSymbolCount = 0;
    int abiMismatchCount = 0;
    int fixtureMismatchCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QString operationName = cryptoOperationName(operation);
        const QString expectedSymbol = productionOperationProviderSymbol(operation);
        const QString expectedAbi = productionOperationProviderAbiSignature(operation);
        const QString expectedFixture = productionHarnessFixtureHash(spec);
        const QJsonObject callable = sequenceIndex < callables.size()
            ? callables.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool symbolRequired = configuredSymbols.contains(expectedSymbol);
        const bool pointerPresent = providerOperationPointer(registeredTable, operation) != nullptr;
        const bool symbolBound = descriptor.linked
            && tableBound
            && tableValidationAccepted
            && pointerPresent
            && symbolRequired;
        const bool abiMatches = callable.value(QStringLiteral("providerAbiSignature")).toString()
            == expectedAbi;
        const bool fixtureMatches = callable.value(QStringLiteral("fixtureHashSha256")).toString()
            == expectedFixture;

        QJsonObject entry;
        entry[QStringLiteral("sequenceIndex")] = sequenceIndex;
        entry[QStringLiteral("operation")] = operationName;
        entry[QStringLiteral("providerId")] = descriptor.providerId;
        entry[QStringLiteral("backendId")] = descriptor.id;
        entry[QStringLiteral("tableAbi")] =
            QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_ABI);
        entry[QStringLiteral("providerApiHeader")] =
            QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_API_HEADER);
        entry[QStringLiteral("headerOperationCount")] =
            QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT;
        entry[QStringLiteral("requiredSymbol")] = expectedSymbol;
        entry[QStringLiteral("required")] = symbolRequired;
        entry[QStringLiteral("bound")] = symbolBound;
        entry[QStringLiteral("pointerPresent")] = pointerPresent;
        entry[QStringLiteral("abiSignature")] = expectedAbi;
        entry[QStringLiteral("abiSignatureMatches")] = abiMatches;
        entry[QStringLiteral("fixtureHashSha256")] = expectedFixture;
        entry[QStringLiteral("fixtureHashMatches")] = fixtureMatches;
        entry[QStringLiteral("callableManifestState")] =
            callable.value(QStringLiteral("bindingState")).toString(
                descriptor.linked ? QStringLiteral("linked-placeholder") : QStringLiteral("not-linked"));
        entry[QStringLiteral("callableManifestAccepted")] =
            callableManifest.value(QStringLiteral("accepted")).toBool(false);
        entry[QStringLiteral("blockedReason")] = symbolBound
            ? QString()
            : (descriptor.linked
                ? QStringLiteral("production-provider-table-placeholder")
                : QStringLiteral("production-provider-table-not-bound"));
        entry[QStringLiteral("operatorAction")] = symbolBound
            ? QStringLiteral("none")
            : (descriptor.linked
                ? QStringLiteral("bind-reviewed-provider-table-symbols")
                : QStringLiteral("link-reviewed-production-crypto-backend"));
        entry[QStringLiteral("rawKeyExported")] = false;
        entry[QStringLiteral("privateMaterialExported")] = false;
        entries.append(entry);

        if (symbolRequired) {
            ++requiredSymbolCount;
        }
        if (symbolBound) {
            ++boundSymbolCount;
        } else {
            ++missingSymbolCount;
        }
        if (!abiMatches) {
            ++abiMismatchCount;
        }
        if (!fixtureMatches) {
            ++fixtureMismatchCount;
        }
        ++sequenceIndex;
    }

    const QJsonObject registration =
        productionProviderTableRegistrationStatusForDescriptor(descriptor);
    const QJsonObject bindingProbe = productionProviderTableBindingProbeStatusForDescriptor(descriptor);
    const bool bindingProbeAccepted =
        bindingProbe.value(QStringLiteral("accepted")).toBool(false);
    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && tableBound
        && registration.value(QStringLiteral("accepted")).toBool(false)
        && abiMatchesHeader
        && operationCountMatchesHeader
        && bindingProbeAccepted
        && requiredSymbolCount == cryptoOperations().size()
        && boundSymbolCount == cryptoOperations().size()
        && missingSymbolCount == 0
        && abiMismatchCount == 0
        && fixtureMismatchCount == 0;

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-table-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("tableAbi")] =
        QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_ABI);
    status[QStringLiteral("providerApiHeader")] =
        QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_API_HEADER);
    status[QStringLiteral("headerTableAbi")] =
        QString::fromLatin1(QNC_E2E_PROVIDER_TABLE_ABI);
    status[QStringLiteral("abiMatchesHeader")] = abiMatchesHeader;
    status[QStringLiteral("headerOperationCount")] =
        QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT;
    status[QStringLiteral("operationCountMatchesHeader")] = operationCountMatchesHeader;
    status[QStringLiteral("buildProbeReason")] =
        QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_REASON);
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("tableBound")] = tableBound;
    status[QStringLiteral("providerTableRegistration")] = registration;
    status[QStringLiteral("providerTableRegistered")] =
        registration.value(QStringLiteral("registered")).toBool(false);
    status[QStringLiteral("registrationReleaseGate")] =
        registration.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("registrationAccepted")] =
        registration.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("tableValidation")] = tableValidation;
    status[QStringLiteral("tableValidationAccepted")] = tableValidationAccepted;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredSymbolCount")] = requiredSymbolCount;
    status[QStringLiteral("boundSymbolCount")] = boundSymbolCount;
    status[QStringLiteral("missingSymbolCount")] = missingSymbolCount;
    status[QStringLiteral("abiMismatchCount")] = abiMismatchCount;
    status[QStringLiteral("fixtureMismatchCount")] = fixtureMismatchCount;
    status[QStringLiteral("callableManifestReleaseGate")] =
        callableManifest.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("callableManifestAccepted")] =
        callableManifest.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("bindingProbe")] = bindingProbe;
    status[QStringLiteral("bindingProbeReleaseGate")] =
        bindingProbe.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("bindingProbeAccepted")] = bindingProbeAccepted;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-table-ready")
        : (descriptor.linked
            ? QStringLiteral("production-provider-table-blocked-placeholder")
            : QStringLiteral("production-provider-table-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : (descriptor.linked
            ? QStringLiteral("production-provider-table-not-bound")
            : QStringLiteral("production-provider-table-not-bound"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("bind-reviewed-provider-table-symbols")
            : QStringLiteral("link-reviewed-production-crypto-backend"));
    status[QStringLiteral("requiredSymbols")] = QJsonArray::fromStringList(configuredSymbols);
    status[QStringLiteral("entries")] = entries;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    return status;
}

QJsonObject providerTableFieldOffsetStatus(const QString& name,
                                           qsizetype offset,
                                           qsizetype previousOffset) {
    QJsonObject field;
    field[QStringLiteral("name")] = name;
    field[QStringLiteral("offset")] = static_cast<qint64>(offset);
    field[QStringLiteral("monotonic")] = offset > previousOffset;
    field[QStringLiteral("sanitized")] = true;
    return field;
}

QJsonObject providerTableValidationStatus(const qnc_e2e_provider_table_v1* table) {
    const bool present = table != nullptr;
    const QString abi = present && table->abi
        ? QString::fromLatin1(table->abi)
        : QString();
    const QString providerId = present && table->provider_id
        ? sanitizedBackendId(QString::fromLatin1(table->provider_id))
        : QString();
    const bool abiMatches = abi == QString::fromLatin1(QNC_E2E_PROVIDER_TABLE_ABI);
    const bool providerIdPresent = !providerId.isEmpty();
    const bool operationCountMatches = present
        && table->operation_count == QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT;
    const bool allOperationPointersPresent = present
        && table->session_key_generation
        && table->identity_key_generation
        && table->public_key_derivation
        && table->agreement_sign
        && table->agreement_verify
        && table->session_derive
        && table->payload_encrypt
        && table->payload_decrypt;
    const bool accepted = present
        && abiMatches
        && providerIdPresent
        && operationCountMatches
        && allOperationPointersPresent;

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-table-validation-v1");
    status[QStringLiteral("present")] = present;
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("tableAbi")] = abi;
    status[QStringLiteral("expectedTableAbi")] =
        QString::fromLatin1(QNC_E2E_PROVIDER_TABLE_ABI);
    status[QStringLiteral("abiMatches")] = abiMatches;
    status[QStringLiteral("providerId")] = providerId;
    status[QStringLiteral("providerIdPresent")] = providerIdPresent;
    status[QStringLiteral("operationCount")] = present
        ? static_cast<int>(table->operation_count)
        : 0;
    status[QStringLiteral("requiredOperationCount")] =
        QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT;
    status[QStringLiteral("operationCountMatches")] = operationCountMatches;
    status[QStringLiteral("allOperationPointersPresent")] = allOperationPointersPresent;
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : (!present
            ? QStringLiteral("production-provider-table-not-bound")
            : (!abiMatches
                ? QStringLiteral("production-provider-table-abi-mismatch")
                : (!providerIdPresent
                    ? QStringLiteral("production-provider-table-provider-id-missing")
                    : (!operationCountMatches
                        ? QStringLiteral("production-provider-table-operation-count-mismatch")
                        : QStringLiteral("production-provider-table-operation-pointer-missing")))));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : QStringLiteral("bind-reviewed-provider-table-with-all-required-operations");
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderTableRegistrationStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const qnc_e2e_provider_table_v1* registeredTable = activeProductionProviderTable();
    const QJsonObject tableValidation = providerTableValidationStatus(registeredTable);
    const bool registered = registeredTable != nullptr;
    const bool builtIn = usingBuiltInProductionProviderTable();
    const bool validationAccepted =
        tableValidation.value(QStringLiteral("accepted")).toBool(false);
    const bool compileTimeBound = QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_BOUND != 0;
    const bool linked = descriptor.linked;
    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && linked
        && compileTimeBound
        && registered
        && validationAccepted;

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-table-registration-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("providerApiHeader")] =
        QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_API_HEADER);
    status[QStringLiteral("expectedTableAbi")] =
        QString::fromLatin1(QNC_E2E_PROVIDER_TABLE_ABI);
    status[QStringLiteral("registered")] = registered;
    status[QStringLiteral("providerTableRegistered")] = registered;
    status[QStringLiteral("registrationSource")] = registered
        ? (builtIn
            ? QStringLiteral("linked-reviewed-operations-provider-table")
            : QStringLiteral("runtime-provider-table-registration"))
        : (linked
            ? QStringLiteral("linked-placeholder-without-runtime-table")
            : QStringLiteral("not-linked"));
    status[QStringLiteral("builtInProviderTable")] = builtIn;
    status[QStringLiteral("explicitProviderTableRegistered")] =
        g_registeredProductionProviderTable != nullptr;
    status[QStringLiteral("linked")] = linked;
    status[QStringLiteral("compileTimeTableBound")] = compileTimeBound;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("tableValidation")] = tableValidation;
    status[QStringLiteral("tableValidationAccepted")] = validationAccepted;
    status[QStringLiteral("tableValidationBlockedReason")] =
        tableValidation.value(QStringLiteral("blockedReason")).toString();
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-table-registration-ready")
        : (registered
            ? QStringLiteral("production-provider-table-registration-blocked-not-bound")
            : (linked
                ? QStringLiteral("production-provider-table-registration-blocked-placeholder")
                : QStringLiteral("production-provider-table-registration-blocked-not-linked")));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : (!registered
            ? QStringLiteral("production-provider-table-not-registered")
            : (!validationAccepted
                ? tableValidation.value(QStringLiteral("blockedReason")).toString()
                : (!compileTimeBound
                    ? QStringLiteral("production-provider-table-compile-binding-disabled")
                    : QStringLiteral("production-provider-table-registration-not-accepted"))));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (!registered
            ? (linked
                ? QStringLiteral("register-reviewed-provider-table-before-production-ready")
                : QStringLiteral("link-reviewed-production-crypto-backend"))
            : (!validationAccepted
                ? QStringLiteral("register-provider-table-with-complete-reviewed-operation-pointers")
                : QStringLiteral("enable-reviewed-provider-table-binding")));
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    return status;
}

qnc_e2e_provider_operation_v1 providerOperationPointer(const qnc_e2e_provider_table_v1* table,
                                                       E2ECryptoOperation operation) {
    if (!table) {
        return nullptr;
    }
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
        return table->session_key_generation;
    case E2ECryptoOperation::IdentityKeyGeneration:
        return table->identity_key_generation;
    case E2ECryptoOperation::PublicKeyDerivation:
        return table->public_key_derivation;
    case E2ECryptoOperation::AgreementSign:
        return table->agreement_sign;
    case E2ECryptoOperation::AgreementVerify:
        return table->agreement_verify;
    case E2ECryptoOperation::SessionDerive:
        return table->session_derive;
    case E2ECryptoOperation::PayloadEncrypt:
        return table->payload_encrypt;
    case E2ECryptoOperation::PayloadDecrypt:
        return table->payload_decrypt;
    }
    return nullptr;
}

qnc_e2e_operation_t providerOperationEnum(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
        return QNC_E2E_OPERATION_SESSION_KEY_GENERATION;
    case E2ECryptoOperation::IdentityKeyGeneration:
        return QNC_E2E_OPERATION_IDENTITY_KEY_GENERATION;
    case E2ECryptoOperation::PublicKeyDerivation:
        return QNC_E2E_OPERATION_PUBLIC_KEY_DERIVATION;
    case E2ECryptoOperation::AgreementSign:
        return QNC_E2E_OPERATION_AGREEMENT_SIGN;
    case E2ECryptoOperation::AgreementVerify:
        return QNC_E2E_OPERATION_AGREEMENT_VERIFY;
    case E2ECryptoOperation::SessionDerive:
        return QNC_E2E_OPERATION_SESSION_DERIVE;
    case E2ECryptoOperation::PayloadEncrypt:
        return QNC_E2E_OPERATION_PAYLOAD_ENCRYPT;
    case E2ECryptoOperation::PayloadDecrypt:
        return QNC_E2E_OPERATION_PAYLOAD_DECRYPT;
    }
    return QNC_E2E_OPERATION_SESSION_KEY_GENERATION;
}

QString providerStatusClass(qnc_e2e_status_t status) {
    switch (status) {
    case QNC_E2E_STATUS_OK:
        return QStringLiteral("ok");
    case QNC_E2E_STATUS_REJECTED:
        return QStringLiteral("rejected");
    case QNC_E2E_STATUS_INVALID_INPUT:
        return QStringLiteral("invalid-input");
    case QNC_E2E_STATUS_UNSUPPORTED:
        return QStringLiteral("unsupported");
    }
    return QStringLiteral("provider-error");
}

QString providerMaterialPolicyClass(qnc_e2e_material_policy_t policy) {
    switch (policy) {
    case QNC_E2E_MATERIAL_HANDLE_ONLY:
        return QStringLiteral("handle-only");
    case QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED:
        return QStringLiteral("public-export-allowed");
    case QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED:
        return QStringLiteral("payload-bytes-allowed");
    }
    return QStringLiteral("unknown");
}

QByteArray providerViewBytes(const qnc_e2e_buffer_view_v1& view) {
    if (!view.data || view.size == 0) {
        return QByteArray();
    }
    return QByteArray(reinterpret_cast<const char*>(view.data),
                      static_cast<qsizetype>(view.size));
}

ProviderDispatchResult dispatchProductionProviderOperation(E2ECryptoOperation operation,
                                                           const QByteArray& primary,
                                                           const QByteArray& secondary,
                                                           const QByteArray& aad) {
    ProviderDispatchResult result;
    const qnc_e2e_provider_table_v1* table = activeProductionProviderTable();
    const QJsonObject validation = providerTableValidationStatus(table);
    if (!validation.value(QStringLiteral("accepted")).toBool(false)) {
        result.reason = validation.value(QStringLiteral("blockedReason")).toString(
            QStringLiteral("production-provider-table-validation-blocked"));
        return result;
    }

    const qnc_e2e_provider_operation_v1 callback =
        providerOperationPointer(table, operation);
    if (!callback) {
        result.reason = QStringLiteral("production-provider-operation-pointer-missing");
        return result;
    }

    qnc_e2e_operation_input_v1 input = {};
    input.operation = providerOperationEnum(operation);
    input.suite_id = E2EAdvertisedSuite;
    input.primary.data = primary.isEmpty()
        ? nullptr
        : reinterpret_cast<const uint8_t*>(primary.constData());
    input.primary.size = static_cast<size_t>(primary.size());
    input.secondary.data = secondary.isEmpty()
        ? nullptr
        : reinterpret_cast<const uint8_t*>(secondary.constData());
    input.secondary.size = static_cast<size_t>(secondary.size());
    input.aad.data = aad.isEmpty()
        ? nullptr
        : reinterpret_cast<const uint8_t*>(aad.constData());
    input.aad.size = static_cast<size_t>(aad.size());

    qnc_e2e_operation_output_v1 output = {};
    output.status = QNC_E2E_STATUS_UNSUPPORTED;
    output.material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
    output.sanitized_error_class = "not-invoked";

    result.callbackStatus = callback(&input, &output);
    result.outputStatus = output.status;
    result.materialPolicy = output.material_policy;
    result.errorClass = output.sanitized_error_class
        ? sanitizedBackendId(QString::fromLatin1(output.sanitized_error_class))
        : QStringLiteral("missing-error-class");
    result.publicOutput = providerViewBytes(output.public_output);
    result.sealedOutput = providerViewBytes(output.sealed_output);
    result.invoked = true;
    if (result.callbackStatus != QNC_E2E_STATUS_OK
        || result.outputStatus != QNC_E2E_STATUS_OK) {
        result.reason = result.errorClass.isEmpty()
            ? providerStatusClass(result.outputStatus)
            : result.errorClass;
    }
    return result;
}

bool providerResultOk(const ProviderDispatchResult& result) {
    return result.invoked
        && result.callbackStatus == QNC_E2E_STATUS_OK
        && result.outputStatus == QNC_E2E_STATUS_OK;
}

bool productionProviderRuntimeReady(QString* reason = nullptr) {
    if (QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED == 0) {
        return fail(reason, QStringLiteral("production-crypto-backend-unavailable"));
    }
    if (!allProductionProviderOperationsBound()) {
        return fail(reason, QStringLiteral("production-provider-operation-pointer-missing"));
    }

    const QByteArray empty;
    const QByteArray transcript =
        QByteArrayLiteral("qnc-provider-runtime-ready-agreement-transcript-v1");
    const QByteArray sessionPrimary =
        QByteArray(ProductionSessionDerivePrimaryDomain)
        + QByteArrayLiteral("qnc-provider-runtime-ready-session-primary-v1");
    const QByteArray sessionSecondary =
        QByteArray(ProductionSessionDeriveSecondaryDomain)
        + QByteArrayLiteral("qnc-provider-runtime-ready-session-secondary-v1");
    const QByteArray sessionContext =
        QByteArrayLiteral("qnc-provider-runtime-ready-session-context-v1");
    const QByteArray plaintext =
        QByteArrayLiteral("provider-runtime-ready-payload");
    const QByteArray payloadAad =
        QByteArrayLiteral("qnc-provider-runtime-ready-payload-aad-v1");

    const ProviderDispatchResult sessionKey =
        dispatchProductionProviderOperation(E2ECryptoOperation::SessionKeyGeneration,
                                            empty,
                                            empty,
                                            empty);
    if (!providerResultOk(sessionKey)
        || sessionKey.materialPolicy != QNC_E2E_MATERIAL_HANDLE_ONLY
        || sessionKey.sealedOutput.size() != SessionKeyBytes) {
        return fail(reason, sessionKey.reason.isEmpty()
            ? QStringLiteral("production-session-key-generation-self-test-failed")
            : sessionKey.reason);
    }

    const ProviderDispatchResult identity =
        dispatchProductionProviderOperation(E2ECryptoOperation::IdentityKeyGeneration,
                                            empty,
                                            empty,
                                            empty);
    if (!providerResultOk(identity)
        || identity.publicOutput.size() != 32
        || identity.sealedOutput.size() != 32) {
        return fail(reason, identity.reason.isEmpty()
            ? QStringLiteral("production-identity-key-generation-self-test-failed")
            : identity.reason);
    }

    const ProviderDispatchResult publicKey =
        dispatchProductionProviderOperation(E2ECryptoOperation::PublicKeyDerivation,
                                            identity.sealedOutput,
                                            empty,
                                            empty);
    if (!providerResultOk(publicKey)
        || publicKey.materialPolicy != QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED
        || publicKey.publicOutput != identity.publicOutput
        || publicKey.publicOutput.size() != 32) {
        return fail(reason, publicKey.reason.isEmpty()
            ? QStringLiteral("production-public-key-derivation-self-test-failed")
            : publicKey.reason);
    }

    const ProviderDispatchResult signature =
        dispatchProductionProviderOperation(E2ECryptoOperation::AgreementSign,
                                            identity.sealedOutput,
                                            transcript,
                                            empty);
    if (!providerResultOk(signature)
        || signature.materialPolicy != QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED
        || signature.publicOutput.size() != 64) {
        return fail(reason, signature.reason.isEmpty()
            ? QStringLiteral("production-agreement-sign-self-test-failed")
            : signature.reason);
    }

    const ProviderDispatchResult verification =
        dispatchProductionProviderOperation(E2ECryptoOperation::AgreementVerify,
                                            publicKey.publicOutput,
                                            transcript,
                                            signature.publicOutput);
    if (!providerResultOk(verification)
        || verification.materialPolicy != QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED) {
        return fail(reason, verification.reason.isEmpty()
            ? QStringLiteral("production-agreement-verify-self-test-failed")
            : verification.reason);
    }

    QByteArray tamperedSignature = signature.publicOutput;
    if (!tamperedSignature.isEmpty()) {
        tamperedSignature[0] = static_cast<char>(tamperedSignature.at(0) ^ 0x01);
    }
    const ProviderDispatchResult rejectedSignature =
        dispatchProductionProviderOperation(E2ECryptoOperation::AgreementVerify,
                                            publicKey.publicOutput,
                                            transcript,
                                            tamperedSignature);
    if (!rejectedSignature.invoked
        || rejectedSignature.callbackStatus != QNC_E2E_STATUS_REJECTED
        || rejectedSignature.outputStatus != QNC_E2E_STATUS_REJECTED) {
        return fail(reason, QStringLiteral("production-agreement-verify-negative-self-test-failed"));
    }

    const ProviderDispatchResult derived =
        dispatchProductionProviderOperation(E2ECryptoOperation::SessionDerive,
                                            sessionPrimary,
                                            sessionSecondary,
                                            sessionContext);
    if (!providerResultOk(derived)
        || derived.materialPolicy != QNC_E2E_MATERIAL_HANDLE_ONLY
        || derived.sealedOutput.size() != SessionKeyBytes) {
        return fail(reason, derived.reason.isEmpty()
            ? QStringLiteral("production-session-derive-self-test-failed")
            : derived.reason);
    }

    const ProviderDispatchResult encrypted =
        dispatchProductionProviderOperation(E2ECryptoOperation::PayloadEncrypt,
                                            derived.sealedOutput,
                                            plaintext,
                                            payloadAad);
    if (!providerResultOk(encrypted)
        || encrypted.materialPolicy != QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED
        || encrypted.sealedOutput.size() != plaintext.size() + MinNonceBytes + MinTagBytes) {
        return fail(reason, encrypted.reason.isEmpty()
            ? QStringLiteral("production-payload-encrypt-self-test-failed")
            : encrypted.reason);
    }

    const ProviderDispatchResult decrypted =
        dispatchProductionProviderOperation(E2ECryptoOperation::PayloadDecrypt,
                                            derived.sealedOutput,
                                            encrypted.sealedOutput,
                                            payloadAad);
    if (!providerResultOk(decrypted)
        || decrypted.materialPolicy != QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED
        || decrypted.publicOutput != plaintext) {
        return fail(reason, decrypted.reason.isEmpty()
            ? QStringLiteral("production-payload-decrypt-self-test-failed")
            : decrypted.reason);
    }

    QByteArray tamperedCiphertext = encrypted.sealedOutput;
    if (!tamperedCiphertext.isEmpty()) {
        tamperedCiphertext[tamperedCiphertext.size() - 1] =
            static_cast<char>(tamperedCiphertext.at(tamperedCiphertext.size() - 1) ^ 0x01);
    }
    const ProviderDispatchResult rejectedCiphertext =
        dispatchProductionProviderOperation(E2ECryptoOperation::PayloadDecrypt,
                                            derived.sealedOutput,
                                            tamperedCiphertext,
                                            payloadAad);
    if (!rejectedCiphertext.invoked
        || rejectedCiphertext.callbackStatus != QNC_E2E_STATUS_REJECTED
        || rejectedCiphertext.outputStatus != QNC_E2E_STATUS_REJECTED) {
        return fail(reason, QStringLiteral("production-payload-decrypt-negative-self-test-failed"));
    }

    const QByteArray malformedIdentityHandle = identity.sealedOutput.left(SessionKeyBytes - 1);
    const ProviderDispatchResult rejectedPublicDerivation =
        dispatchProductionProviderOperation(E2ECryptoOperation::PublicKeyDerivation,
                                            malformedIdentityHandle,
                                            empty,
                                            empty);
    if (!rejectedPublicDerivation.invoked
        || rejectedPublicDerivation.callbackStatus != QNC_E2E_STATUS_INVALID_INPUT
        || rejectedPublicDerivation.outputStatus != QNC_E2E_STATUS_INVALID_INPUT) {
        return fail(reason, QStringLiteral("production-public-key-derivation-malformed-handle-self-test-failed"));
    }

    const ProviderDispatchResult rejectedAgreementVerify =
        dispatchProductionProviderOperation(E2ECryptoOperation::AgreementVerify,
                                            publicKey.publicOutput.left(31),
                                            transcript,
                                            signature.publicOutput);
    if (!rejectedAgreementVerify.invoked
        || rejectedAgreementVerify.callbackStatus != QNC_E2E_STATUS_INVALID_INPUT
        || rejectedAgreementVerify.outputStatus != QNC_E2E_STATUS_INVALID_INPUT) {
        return fail(reason, QStringLiteral("production-agreement-verify-malformed-public-key-self-test-failed"));
    }

    const ProviderDispatchResult rejectedSessionDerive =
        dispatchProductionProviderOperation(E2ECryptoOperation::SessionDerive,
                                            QByteArrayLiteral("malformed-session-derive-key"),
                                            sessionSecondary,
                                            sessionContext);
    if (!rejectedSessionDerive.invoked
        || rejectedSessionDerive.callbackStatus != QNC_E2E_STATUS_INVALID_INPUT
        || rejectedSessionDerive.outputStatus != QNC_E2E_STATUS_INVALID_INPUT) {
        return fail(reason, QStringLiteral("production-session-derive-malformed-key-self-test-failed"));
    }

    const QByteArray malformedSessionKey = derived.sealedOutput.left(SessionKeyBytes - 1);
    const ProviderDispatchResult rejectedPayloadEncrypt =
        dispatchProductionProviderOperation(E2ECryptoOperation::PayloadEncrypt,
                                            malformedSessionKey,
                                            plaintext,
                                            payloadAad);
    if (!rejectedPayloadEncrypt.invoked
        || rejectedPayloadEncrypt.callbackStatus != QNC_E2E_STATUS_INVALID_INPUT
        || rejectedPayloadEncrypt.outputStatus != QNC_E2E_STATUS_INVALID_INPUT) {
        return fail(reason, QStringLiteral("production-payload-encrypt-malformed-key-self-test-failed"));
    }

    const ProviderDispatchResult rejectedPayloadDecrypt =
        dispatchProductionProviderOperation(E2ECryptoOperation::PayloadDecrypt,
                                            malformedSessionKey,
                                            encrypted.sealedOutput,
                                            payloadAad);
    if (!rejectedPayloadDecrypt.invoked
        || rejectedPayloadDecrypt.callbackStatus != QNC_E2E_STATUS_INVALID_INPUT
        || rejectedPayloadDecrypt.outputStatus != QNC_E2E_STATUS_INVALID_INPUT) {
        return fail(reason, QStringLiteral("production-payload-decrypt-malformed-key-self-test-failed"));
    }

    if (reason) {
        reason->clear();
    }
    return true;
}

QString providerProbeFailureClass(bool canInvoke,
                                  const QString& callbackStatusClass,
                                  const QString& outputStatusClass,
                                  const QString& blockedReason) {
    if (!canInvoke) {
        return blockedReason.isEmpty()
            ? QStringLiteral("not-invoked")
            : blockedReason;
    }
    if (callbackStatusClass != outputStatusClass) {
        return QStringLiteral("provider-status-mismatch");
    }
    if (callbackStatusClass == QStringLiteral("ok")) {
        return QStringLiteral("none");
    }
    return callbackStatusClass;
}

QString providerProbeMismatchReason(bool canInvoke,
                                    bool expectedStatusMatched,
                                    bool expectedFailureMatched,
                                    bool expectedMaterialPolicyMatched,
                                    bool expectedOutputClassMatched,
                                    const QString& blockedReason) {
    if (!canInvoke) {
        return blockedReason.isEmpty()
            ? QStringLiteral("production-provider-probe-not-invoked")
            : blockedReason;
    }
    if (!expectedStatusMatched) {
        return QStringLiteral("known-answer-status-mismatch");
    }
    if (!expectedFailureMatched) {
        return QStringLiteral("known-answer-failure-class-mismatch");
    }
    if (!expectedMaterialPolicyMatched) {
        return QStringLiteral("known-answer-material-policy-mismatch");
    }
    if (!expectedOutputClassMatched) {
        return QStringLiteral("known-answer-output-shape-mismatch");
    }
    return QStringLiteral("none");
}

QString providerProbeMismatchSeverity(const QString& mismatchReason) {
    if (mismatchReason == QStringLiteral("none")) {
        return QStringLiteral("none");
    }
    if (mismatchReason == QStringLiteral("production-provider-table-not-registered")
        || mismatchReason == QStringLiteral("production-provider-operation-pointer-missing")
        || mismatchReason == QStringLiteral("production-provider-probe-not-invoked")) {
        return QStringLiteral("blocked");
    }
    return QStringLiteral("fail-closed");
}

QString providerProbeMismatchScope(bool canInvoke,
                                   bool tableValidationAccepted,
                                   bool pointerPresent,
                                   const QString& mismatchReason) {
    if (mismatchReason == QStringLiteral("none")) {
        return QStringLiteral("none");
    }
    if (!tableValidationAccepted) {
        return QStringLiteral("provider-table-validation");
    }
    if (!pointerPresent) {
        return QStringLiteral("provider-operation-pointer");
    }
    if (!canInvoke) {
        return QStringLiteral("provider-invocation");
    }
    return QStringLiteral("known-answer-vector");
}

void incrementSummaryCount(QJsonObject* summary, const QString& key) {
    if (!summary) {
        return;
    }
    const QString normalizedKey = key.trimmed().isEmpty()
        ? QStringLiteral("unknown")
        : key.trimmed();
    (*summary)[normalizedKey] =
        summary->value(normalizedKey).toInt() + 1;
}

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
        publicInput.suite_id = E2EAdvertisedSuite;
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
        signInput.suite_id = E2EAdvertisedSuite;
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
        encryptInput.suite_id = E2EAdvertisedSuite;
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
        input.suite_id = E2EAdvertisedSuite;
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
            input.suite_id = E2EAdvertisedSuite;
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

QJsonObject productionProviderOperationPreflightStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const qnc_e2e_provider_table_v1* registeredTable = activeProductionProviderTable();
    const QJsonObject registration =
        productionProviderTableRegistrationStatusForDescriptor(descriptor);
    const QJsonObject tableValidation =
        registration.value(QStringLiteral("tableValidation")).toObject();
    const bool registered = registeredTable != nullptr;
    const bool validationAccepted =
        tableValidation.value(QStringLiteral("accepted")).toBool(false);
    const bool compileTimeBound = QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_BOUND != 0;

    QJsonArray operations;
    int presentOperationCount = 0;
    int blockedOperationCount = 0;
    int abiMatchedOperationCount = 0;
    int contractMatchedOperationCount = 0;
    int fixtureMatchedOperationCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const bool pointerPresent = providerOperationPointer(registeredTable, operation) != nullptr;
        const QString expectedSymbol = productionOperationProviderSymbol(operation);
        const QString expectedSignature = productionOperationProviderAbiSignature(operation);
        const QString expectedFixture = productionHarnessFixtureHash(spec);
        const bool symbolConfigured =
            configuredProductionProviderSymbols().contains(expectedSymbol);
        const bool abiMatched = QString::fromLatin1(QNC_E2E_PROVIDER_TABLE_ABI)
            == QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_ABI);
        const bool contractMatched =
            !productionOperationInputContract(operation).isEmpty()
            && !productionOperationOutputContract(operation).isEmpty()
            && QString::fromLatin1(QNC_E2E_OPERATION_CONTRACT_VERSION)
                == descriptor.operationContractVersion;
        const bool fixtureMatched = expectedFixture.size() == FingerprintHexLength;
        const bool preflightReady = registered
            && validationAccepted
            && pointerPresent
            && symbolConfigured
            && abiMatched
            && contractMatched
            && fixtureMatched
            && compileTimeBound;

        QJsonObject op;
        op[QStringLiteral("sequenceIndex")] = sequenceIndex;
        op[QStringLiteral("operation")] = cryptoOperationName(operation);
        op[QStringLiteral("providerId")] = descriptor.providerId;
        op[QStringLiteral("backendId")] = descriptor.id;
        op[QStringLiteral("providerSymbol")] = expectedSymbol;
        op[QStringLiteral("providerAbiSignature")] = expectedSignature;
        op[QStringLiteral("pointerPresent")] = pointerPresent;
        op[QStringLiteral("registered")] = registered;
        op[QStringLiteral("symbolConfigured")] = symbolConfigured;
        op[QStringLiteral("abiMatched")] = abiMatched;
        op[QStringLiteral("inputContract")] =
            QJsonArray::fromStringList(productionOperationInputContract(operation));
        op[QStringLiteral("outputContract")] =
            QJsonArray::fromStringList(productionOperationOutputContract(operation));
        op[QStringLiteral("contractMatched")] = contractMatched;
        op[QStringLiteral("fixtureHashSha256")] = expectedFixture;
        op[QStringLiteral("fixtureMatched")] = fixtureMatched;
        op[QStringLiteral("materialPolicy")] =
            (operation == E2ECryptoOperation::PayloadEncrypt
             || operation == E2ECryptoOperation::PayloadDecrypt)
                ? QStringLiteral("payload-bytes-allowed-no-key-export")
                : QStringLiteral("handle-based-no-private-material-export");
        op[QStringLiteral("preflightReady")] = preflightReady;
        op[QStringLiteral("blockedReason")] = preflightReady
            ? QString()
            : (!registered
                ? QStringLiteral("production-provider-table-not-registered")
                : (!validationAccepted
                    ? tableValidation.value(QStringLiteral("blockedReason")).toString()
                    : (!pointerPresent
                        ? QStringLiteral("production-provider-operation-pointer-missing")
                        : (!compileTimeBound
                            ? QStringLiteral("production-provider-table-compile-binding-disabled")
                            : QStringLiteral("production-provider-operation-not-ready")))));
        op[QStringLiteral("operatorAction")] = preflightReady
            ? QStringLiteral("none")
            : (!registered
                ? QStringLiteral("register-reviewed-provider-table-before-operation-preflight")
                : (!validationAccepted
                    ? QStringLiteral("register-provider-table-with-all-required-operation-pointers")
                    : QStringLiteral("enable-reviewed-provider-operation-preflight")));
        op[QStringLiteral("operationInvoked")] = false;
        op[QStringLiteral("rawKeyExported")] = false;
        op[QStringLiteral("privateMaterialExported")] = false;
        operations.append(op);

        if (pointerPresent) {
            ++presentOperationCount;
        }
        if (abiMatched) {
            ++abiMatchedOperationCount;
        }
        if (contractMatched) {
            ++contractMatchedOperationCount;
        }
        if (fixtureMatched) {
            ++fixtureMatchedOperationCount;
        }
        if (!preflightReady) {
            ++blockedOperationCount;
        }
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && compileTimeBound
        && registered
        && validationAccepted
        && presentOperationCount == cryptoOperations().size()
        && blockedOperationCount == 0
        && abiMatchedOperationCount == cryptoOperations().size()
        && contractMatchedOperationCount == cryptoOperations().size()
        && fixtureMatchedOperationCount == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-operation-preflight-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerTableRegistered")] = registered;
    status[QStringLiteral("providerTableRegistration")] = registration;
    status[QStringLiteral("tableValidationAccepted")] = validationAccepted;
    status[QStringLiteral("compileTimeTableBound")] = compileTimeBound;
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredOperationCount")] = cryptoOperations().size();
    status[QStringLiteral("presentOperationCount")] = presentOperationCount;
    status[QStringLiteral("blockedOperationCount")] = blockedOperationCount;
    status[QStringLiteral("abiMatchedOperationCount")] = abiMatchedOperationCount;
    status[QStringLiteral("contractMatchedOperationCount")] = contractMatchedOperationCount;
    status[QStringLiteral("fixtureMatchedOperationCount")] = fixtureMatchedOperationCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-operation-preflight-ready")
        : (registered
            ? QStringLiteral("production-provider-operation-preflight-blocked-not-ready")
            : (descriptor.linked
                ? QStringLiteral("production-provider-operation-preflight-blocked-placeholder")
                : QStringLiteral("production-provider-operation-preflight-blocked-not-linked")));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : (!registered
            ? QStringLiteral("production-provider-table-not-registered")
            : (!validationAccepted
                ? tableValidation.value(QStringLiteral("blockedReason")).toString()
                : (!compileTimeBound
                    ? QStringLiteral("production-provider-table-compile-binding-disabled")
                    : QStringLiteral("production-provider-operations-not-ready"))));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (!registered
            ? QStringLiteral("register-reviewed-provider-table-before-operation-preflight")
            : QStringLiteral("enable-reviewed-provider-operation-preflight"));
    status[QStringLiteral("operations")] = operations;
    status[QStringLiteral("operationInvoked")] = false;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    return status;
}

QString productionCallFramePrimaryClass(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
    case E2ECryptoOperation::IdentityKeyGeneration:
        return QStringLiteral("empty-random-source-context");
    case E2ECryptoOperation::PublicKeyDerivation:
    case E2ECryptoOperation::AgreementSign:
        return QStringLiteral("private-handle-reference");
    case E2ECryptoOperation::AgreementVerify:
        return QStringLiteral("peer-public-identity-material");
    case E2ECryptoOperation::SessionDerive:
        return QStringLiteral("local-private-agreement-handle");
    case E2ECryptoOperation::PayloadEncrypt:
        return QStringLiteral("session-key-handle");
    case E2ECryptoOperation::PayloadDecrypt:
        return QStringLiteral("session-key-handle");
    }
    return QStringLiteral("unknown");
}

QString productionCallFrameSecondaryClass(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
    case E2ECryptoOperation::IdentityKeyGeneration:
        return QStringLiteral("suite-context");
    case E2ECryptoOperation::PublicKeyDerivation:
        return QStringLiteral("identity-suite-context");
    case E2ECryptoOperation::AgreementSign:
    case E2ECryptoOperation::AgreementVerify:
        return QStringLiteral("canonical-agreement-transcript");
    case E2ECryptoOperation::SessionDerive:
        return QStringLiteral("remote-agreement-transcript");
    case E2ECryptoOperation::PayloadEncrypt:
        return QStringLiteral("plaintext-bytes");
    case E2ECryptoOperation::PayloadDecrypt:
        return QStringLiteral("ciphertext-bytes");
    }
    return QStringLiteral("unknown");
}

QString productionCallFrameAadClass(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::PayloadEncrypt:
    case E2ECryptoOperation::PayloadDecrypt:
        return QStringLiteral("envelope-aad");
    case E2ECryptoOperation::AgreementSign:
    case E2ECryptoOperation::AgreementVerify:
    case E2ECryptoOperation::SessionDerive:
        return QStringLiteral("transcript-context");
    case E2ECryptoOperation::SessionKeyGeneration:
    case E2ECryptoOperation::IdentityKeyGeneration:
    case E2ECryptoOperation::PublicKeyDerivation:
        return QStringLiteral("empty");
    }
    return QStringLiteral("unknown");
}

QString productionCallFrameMaterialPolicy(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::PublicKeyDerivation:
    case E2ECryptoOperation::AgreementSign:
    case E2ECryptoOperation::AgreementVerify:
        return QStringLiteral("public-export-allowed");
    case E2ECryptoOperation::PayloadEncrypt:
    case E2ECryptoOperation::PayloadDecrypt:
        return QStringLiteral("payload-bytes-allowed-no-key-export");
    case E2ECryptoOperation::SessionKeyGeneration:
    case E2ECryptoOperation::IdentityKeyGeneration:
    case E2ECryptoOperation::SessionDerive:
        return QStringLiteral("handle-only-no-private-material-export");
    }
    return QStringLiteral("unknown");
}

QJsonObject productionProviderCallFrameStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject preflight =
        productionProviderOperationPreflightStatusForDescriptor(descriptor);
    const QJsonArray preflightOperations =
        preflight.value(QStringLiteral("operations")).toArray();

    QJsonArray frames;
    int readyFrameCount = 0;
    int blockedFrameCount = 0;
    int sanitizedFrameCount = 0;
    int enumMatchedFrameCount = 0;
    int contractHashCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject preflightOperation = sequenceIndex < preflightOperations.size()
            ? preflightOperations.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool preflightReady =
            preflightOperation.value(QStringLiteral("preflightReady")).toBool(false);
        const bool enumMatched = static_cast<int>(static_cast<qnc_e2e_operation_t>(sequenceIndex))
            == sequenceIndex;
        const QStringList inputContract = productionOperationInputContract(operation);
        const QStringList outputContract = productionOperationOutputContract(operation);
        const QString inputHash = e2eFingerprint(inputContract.join(QLatin1Char('|')).toUtf8());
        const QString outputHash = e2eFingerprint(outputContract.join(QLatin1Char('|')).toUtf8());
        const bool contractHashed =
            inputHash.size() == FingerprintHexLength && outputHash.size() == FingerprintHexLength;
        const bool sanitized = true;
        const bool frameReady = preflight.value(QStringLiteral("accepted")).toBool(false)
            && preflightReady
            && enumMatched
            && contractHashed
            && sanitized;

        QJsonObject frame;
        frame[QStringLiteral("sequenceIndex")] = sequenceIndex;
        frame[QStringLiteral("operation")] = cryptoOperationName(operation);
        frame[QStringLiteral("operationEnumValue")] = sequenceIndex;
        frame[QStringLiteral("operationEnumMatched")] = enumMatched;
        frame[QStringLiteral("backendId")] = descriptor.id;
        frame[QStringLiteral("providerId")] = descriptor.providerId;
        frame[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        frame[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
        frame[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        frame[QStringLiteral("fixtureHashSha256")] = productionHarnessFixtureHash(spec);
        frame[QStringLiteral("suiteId")] = QString::fromLatin1(E2EAdvertisedSuite);
        frame[QStringLiteral("primaryInputClass")] = productionCallFramePrimaryClass(operation);
        frame[QStringLiteral("secondaryInputClass")] = productionCallFrameSecondaryClass(operation);
        frame[QStringLiteral("aadInputClass")] = productionCallFrameAadClass(operation);
        frame[QStringLiteral("primaryMaxBytes")] =
            operation == E2ECryptoOperation::PayloadEncrypt
                || operation == E2ECryptoOperation::PayloadDecrypt
                    ? static_cast<int>(MaxAadLength * 8)
                    : static_cast<int>(MaxPublicKeyBytes);
        frame[QStringLiteral("secondaryMaxBytes")] =
            operation == E2ECryptoOperation::PayloadEncrypt
                || operation == E2ECryptoOperation::PayloadDecrypt
                    ? static_cast<int>(MaxAadLength * 8)
                    : static_cast<int>(MaxPublicKeyBytes);
        frame[QStringLiteral("aadMaxBytes")] = static_cast<int>(MaxAadLength);
        frame[QStringLiteral("outputMaterialPolicy")] =
            productionCallFrameMaterialPolicy(operation);
        frame[QStringLiteral("inputContractHashSha256")] = inputHash;
        frame[QStringLiteral("outputContractHashSha256")] = outputHash;
        frame[QStringLiteral("contractHashed")] = contractHashed;
        frame[QStringLiteral("preflightReady")] = preflightReady;
        frame[QStringLiteral("frameReady")] = frameReady;
        frame[QStringLiteral("operationInvoked")] = false;
        frame[QStringLiteral("inputBytesAttached")] = false;
        frame[QStringLiteral("outputBytesAttached")] = false;
        frame[QStringLiteral("blockedReason")] = frameReady
            ? QString()
            : preflightOperation.value(QStringLiteral("blockedReason")).toString(
                preflight.value(QStringLiteral("blockedReason")).toString());
        frame[QStringLiteral("operatorAction")] = frameReady
            ? QStringLiteral("none")
            : preflightOperation.value(QStringLiteral("operatorAction")).toString(
                preflight.value(QStringLiteral("operatorAction")).toString());
        frame[QStringLiteral("sanitized")] = sanitized;
        frame[QStringLiteral("rawKeyExported")] = false;
        frame[QStringLiteral("privateMaterialExported")] = false;
        frame[QStringLiteral("sessionSecretExported")] = false;
        frame[QStringLiteral("privateIdentityMaterialExported")] = false;
        frame[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        frames.append(frame);

        if (frameReady) {
            ++readyFrameCount;
        } else {
            ++blockedFrameCount;
        }
        if (sanitized) {
            ++sanitizedFrameCount;
        }
        if (enumMatched) {
            ++enumMatchedFrameCount;
        }
        if (contractHashed) {
            ++contractHashCount;
        }
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && preflight.value(QStringLiteral("accepted")).toBool(false)
        && readyFrameCount == cryptoOperations().size()
        && blockedFrameCount == 0
        && sanitizedFrameCount == cryptoOperations().size()
        && enumMatchedFrameCount == cryptoOperations().size()
        && contractHashCount == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-call-frame-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerOperationPreflight")] = preflight;
    status[QStringLiteral("providerOperationPreflightReleaseGate")] =
        preflight.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerOperationPreflightAccepted")] =
        preflight.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredFrameCount")] = cryptoOperations().size();
    status[QStringLiteral("readyFrameCount")] = readyFrameCount;
    status[QStringLiteral("blockedFrameCount")] = blockedFrameCount;
    status[QStringLiteral("sanitizedFrameCount")] = sanitizedFrameCount;
    status[QStringLiteral("enumMatchedFrameCount")] = enumMatchedFrameCount;
    status[QStringLiteral("contractHashCount")] = contractHashCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-call-frame-ready")
        : (descriptor.linked
            ? QStringLiteral("production-provider-call-frame-blocked-placeholder")
            : QStringLiteral("production-provider-call-frame-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : preflight.value(QStringLiteral("blockedReason")).toString(
            descriptor.linked
                ? QStringLiteral("production-provider-call-frame-placeholder")
                : QStringLiteral("production-provider-table-not-registered"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("prepare-reviewed-provider-call-frames-after-preflight")
            : QStringLiteral("register-reviewed-provider-table-before-call-frame"));
    status[QStringLiteral("frames")] = frames;
    status[QStringLiteral("operationInvoked")] = false;
    status[QStringLiteral("inputBytesAttached")] = false;
    status[QStringLiteral("outputBytesAttached")] = false;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderInvocationDryRunStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject callFrame =
        productionProviderCallFrameStatusForDescriptor(descriptor);
    const QJsonArray callFrames =
        callFrame.value(QStringLiteral("frames")).toArray();

    QJsonArray invocations;
    int dryRunReadyCount = 0;
    int blockedInvocationCount = 0;
    int sanitizedInvocationCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject frame = sequenceIndex < callFrames.size()
            ? callFrames.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool frameReady =
            frame.value(QStringLiteral("frameReady")).toBool(false);
        const bool dryRunReady = frameReady
            && callFrame.value(QStringLiteral("accepted")).toBool(false);
        const QString operationName = cryptoOperationName(operation);
        const QStringList inputContract = productionOperationInputContract(operation);
        const QStringList outputContract = productionOperationOutputContract(operation);
        const bool sanitized = true;

        QJsonObject invocation;
        invocation[QStringLiteral("sequenceIndex")] = sequenceIndex;
        invocation[QStringLiteral("operation")] = operationName;
        invocation[QStringLiteral("providerId")] = descriptor.providerId;
        invocation[QStringLiteral("backendId")] = descriptor.id;
        invocation[QStringLiteral("entrypoint")] =
            descriptor.type + QStringLiteral("/") + operationName;
        invocation[QStringLiteral("providerSymbol")] =
            productionOperationProviderSymbol(operation);
        invocation[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        invocation[QStringLiteral("fixtureHashSha256")] =
            productionHarnessFixtureHash(spec);
        invocation[QStringLiteral("vectorSet")] = spec.vectorSet;
        invocation[QStringLiteral("inputContract")] =
            QJsonArray::fromStringList(inputContract);
        invocation[QStringLiteral("outputContract")] =
            QJsonArray::fromStringList(outputContract);
        invocation[QStringLiteral("inputContractHashSha256")] =
            e2eFingerprint(inputContract.join(QLatin1Char('|')).toUtf8());
        invocation[QStringLiteral("outputContractHashSha256")] =
            e2eFingerprint(outputContract.join(QLatin1Char('|')).toUtf8());
        invocation[QStringLiteral("preflightReady")] =
            frame.value(QStringLiteral("preflightReady")).toBool(false);
        invocation[QStringLiteral("providerCallFrame")] = frame;
        invocation[QStringLiteral("providerCallFrameReleaseGate")] =
            callFrame.value(QStringLiteral("releaseGate")).toString();
        invocation[QStringLiteral("callFrameReady")] = frameReady;
        invocation[QStringLiteral("dryRunReady")] = dryRunReady;
        invocation[QStringLiteral("operationInvoked")] = false;
        invocation[QStringLiteral("wouldInvokeReviewedProvider")] = dryRunReady;
        invocation[QStringLiteral("resultState")] = dryRunReady
            ? QStringLiteral("ready-for-reviewed-provider-invocation")
            : (descriptor.linked
                ? QStringLiteral("blocked-linked-placeholder")
                : QStringLiteral("blocked-not-linked"));
        invocation[QStringLiteral("expectedStatusOnInvoke")] = dryRunReady
            ? QStringLiteral("qnc-e2e-status-ok-or-sanitized-error")
            : QStringLiteral("not-invoked");
        invocation[QStringLiteral("blockedReason")] = dryRunReady
            ? QString()
            : frame.value(QStringLiteral("blockedReason")).toString(
                callFrame.value(QStringLiteral("blockedReason")).toString());
        invocation[QStringLiteral("operatorAction")] = dryRunReady
            ? QStringLiteral("execute-reviewed-provider-operation-under-harness")
            : frame.value(QStringLiteral("operatorAction")).toString(
                callFrame.value(QStringLiteral("operatorAction")).toString());
        invocation[QStringLiteral("sanitized")] = sanitized;
        invocation[QStringLiteral("rawKeyExported")] = false;
        invocation[QStringLiteral("privateMaterialExported")] = false;
        invocation[QStringLiteral("sessionSecretExported")] = false;
        invocation[QStringLiteral("privateIdentityMaterialExported")] = false;
        invocations.append(invocation);

        if (dryRunReady) {
            ++dryRunReadyCount;
        } else {
            ++blockedInvocationCount;
        }
        if (sanitized) {
            ++sanitizedInvocationCount;
        }
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && callFrame.value(QStringLiteral("accepted")).toBool(false)
        && dryRunReadyCount == cryptoOperations().size()
        && blockedInvocationCount == 0
        && sanitizedInvocationCount == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-dry-run-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerCallFrame")] = callFrame;
    status[QStringLiteral("providerCallFrameReleaseGate")] =
        callFrame.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerCallFrameAccepted")] =
        callFrame.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("providerOperationPreflight")] =
        callFrame.value(QStringLiteral("providerOperationPreflight")).toObject();
    status[QStringLiteral("providerOperationPreflightReleaseGate")] =
        callFrame.value(QStringLiteral("providerOperationPreflightReleaseGate")).toString();
    status[QStringLiteral("providerOperationPreflightAccepted")] =
        callFrame.value(QStringLiteral("providerOperationPreflightAccepted")).toBool(false);
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredInvocationCount")] = cryptoOperations().size();
    status[QStringLiteral("dryRunReadyCount")] = dryRunReadyCount;
    status[QStringLiteral("blockedInvocationCount")] = blockedInvocationCount;
    status[QStringLiteral("sanitizedInvocationCount")] = sanitizedInvocationCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-invocation-dry-run-ready")
        : (descriptor.linked
            ? QStringLiteral("production-provider-invocation-dry-run-blocked-placeholder")
            : QStringLiteral("production-provider-invocation-dry-run-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : callFrame.value(QStringLiteral("blockedReason")).toString(
            descriptor.linked
                ? QStringLiteral("production-provider-invocation-dry-run-placeholder")
                : QStringLiteral("production-provider-table-not-registered"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("complete-provider-preflight-before-reviewed-invocation")
            : QStringLiteral("register-reviewed-provider-table-before-invocation-dry-run"));
    status[QStringLiteral("invocations")] = invocations;
    status[QStringLiteral("operationInvoked")] = false;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    status[QStringLiteral("sessionSecretExported")] = false;
    return status;
}

QJsonObject productionProviderInvocationResultStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject dryRun =
        productionProviderInvocationDryRunStatusForDescriptor(descriptor);
    const QJsonArray dryRunInvocations =
        dryRun.value(QStringLiteral("invocations")).toArray();
    const QJsonObject probeEvidence =
        productionProviderInvocationExecutionProbeEvidenceForDescriptor(descriptor);
    const QJsonArray probes = probeEvidence.value(QStringLiteral("probes")).toArray();

    QJsonArray results;
    int captureReadyCount = 0;
    int blockedResultCount = 0;
    int sanitizedResultCount = 0;
    int outputContractProofCount = 0;
    int fixtureProofCount = 0;
    int materialExportProofCount = 0;
    int invokedOperationCount = 0;
    int capturedResultCount = 0;
    int sequenceIndex = 0;
    const QString dryRunBlockedReason =
        dryRun.value(QStringLiteral("blockedReason")).toString();
    const QString resultCaptureBlockedReason = dryRunBlockedReason.isEmpty()
        ? QStringLiteral("production-provider-result-capture-not-enabled")
        : dryRunBlockedReason;
    const QString dryRunOperatorAction =
        dryRun.value(QStringLiteral("operatorAction")).toString();
    const QString resultCaptureOperatorAction = dryRunOperatorAction.isEmpty()
        ? QStringLiteral("capture-reviewed-provider-invocation-results")
        : dryRunOperatorAction;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QString operationName = cryptoOperationName(operation);
        const QJsonObject invocation = sequenceIndex < dryRunInvocations.size()
            ? dryRunInvocations.at(sequenceIndex).toObject()
            : QJsonObject();
        const QJsonObject probe = sequenceIndex < probes.size()
            ? probes.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool probeInvoked =
            probe.value(QStringLiteral("operationInvoked")).toBool(false);
        const bool probeCaptured =
            probe.value(QStringLiteral("resultCaptured")).toBool(false);
        const bool probeVectorPassed =
            probe.value(QStringLiteral("providerVectorSetMatched")).toBool(false);
        const bool probeSanitized =
            probe.value(QStringLiteral("sanitized")).toBool(false)
            && !probe.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !probe.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !probe.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !probe.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !probe.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool dryRunReady = invocation.value(QStringLiteral("dryRunReady")).toBool(false);
        const bool wouldInvoke = invocation.value(QStringLiteral("wouldInvokeReviewedProvider")).toBool(false);
        const bool outputContractMatches =
            invocation.value(QStringLiteral("outputContract")).toArray().size()
                == productionOperationOutputContract(operation).size();
        const bool fixtureMatches =
            invocation.value(QStringLiteral("fixtureHashSha256")).toString()
                == productionHarnessFixtureHash(spec);
        const bool materialExportProof = !invocation.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !invocation.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !invocation.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !invocation.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true);
        const bool sanitized = outputContractMatches
            && fixtureMatches
            && materialExportProof
            && (!probeInvoked || probeSanitized);
        const bool captureReady = dryRun.value(QStringLiteral("accepted")).toBool(false)
            && dryRunReady
            && wouldInvoke
            && sanitized
            && probeInvoked
            && probeCaptured
            && probeVectorPassed;
        const QString invocationBlockedReason =
            invocation.value(QStringLiteral("blockedReason")).toString();
        const QString invocationOperatorAction =
            invocation.value(QStringLiteral("operatorAction")).toString();

        QJsonObject result;
        result[QStringLiteral("sequenceIndex")] = sequenceIndex;
        result[QStringLiteral("operation")] = operationName;
        result[QStringLiteral("backendId")] = descriptor.id;
        result[QStringLiteral("providerId")] = descriptor.providerId;
        result[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        result[QStringLiteral("entrypoint")] =
            invocation.value(QStringLiteral("entrypoint")).toString(
                descriptor.type + QStringLiteral("/") + operationName);
        result[QStringLiteral("providerSymbol")] =
            invocation.value(QStringLiteral("providerSymbol")).toString(
                productionOperationProviderSymbol(operation));
        result[QStringLiteral("providerAbiSignature")] =
            invocation.value(QStringLiteral("providerAbiSignature")).toString(
                productionOperationProviderAbiSignature(operation));
        result[QStringLiteral("fixtureHashSha256")] =
            invocation.value(QStringLiteral("fixtureHashSha256")).toString(
                productionHarnessFixtureHash(spec));
        result[QStringLiteral("inputContractHashSha256")] =
            invocation.value(QStringLiteral("inputContractHashSha256")).toString();
        result[QStringLiteral("outputContractHashSha256")] =
            invocation.value(QStringLiteral("outputContractHashSha256")).toString();
        result[QStringLiteral("providerProbeEvidence")] = probe;
        result[QStringLiteral("providerProbeVectorMatched")] = probeVectorPassed;
        result[QStringLiteral("providerProbeFailureClass")] =
            probe.value(QStringLiteral("failureClass")).toString();
        result[QStringLiteral("providerProbeMismatchReason")] =
            probe.value(QStringLiteral("mismatchReason")).toString();
        result[QStringLiteral("providerProbeOutputShapeHashSha256")] =
            probe.value(QStringLiteral("outputShapeHashSha256")).toString();
        result[QStringLiteral("statusCodeClass")] = captureReady
            ? probe.value(QStringLiteral("callbackStatusClass")).toString(
                QStringLiteral("qnc-e2e-status-ok-or-sanitized-error"))
            : QStringLiteral("not-invoked");
        result[QStringLiteral("expectedErrorClass")] = captureReady
            ? probe.value(QStringLiteral("sanitizedErrorClass")).toString(
                QStringLiteral("ok-or-sanitized-provider-error"))
            : (descriptor.linked
                ? QStringLiteral("production-provider-result-placeholder")
                : QStringLiteral("production-provider-not-linked"));
        result[QStringLiteral("outputContractProof")] = outputContractMatches
            ? QStringLiteral("output-contract-hash-matched")
            : QStringLiteral("output-contract-hash-mismatch");
        result[QStringLiteral("fixtureProof")] = fixtureMatches
            ? QStringLiteral("fixture-hash-matched")
            : QStringLiteral("fixture-hash-mismatch");
        result[QStringLiteral("materialExportProof")] = materialExportProof
            ? QStringLiteral("no-sensitive-material-export")
            : QStringLiteral("sensitive-material-exported");
        result[QStringLiteral("captureReady")] = captureReady;
        result[QStringLiteral("dryRunReady")] = dryRunReady;
        result[QStringLiteral("operationInvoked")] = probeInvoked;
        result[QStringLiteral("resultCaptured")] = probeCaptured;
        result[QStringLiteral("sanitized")] = sanitized;
        result[QStringLiteral("outputContractMatched")] = outputContractMatches;
        result[QStringLiteral("fixtureHashMatched")] = fixtureMatches;
        result[QStringLiteral("blockedReason")] = captureReady
            ? QString()
            : (invocationBlockedReason.isEmpty()
                ? (probeInvoked
                    ? probe.value(QStringLiteral("mismatchReason")).toString(resultCaptureBlockedReason)
                    : resultCaptureBlockedReason)
                : invocationBlockedReason);
        result[QStringLiteral("operatorAction")] = captureReady
            ? QStringLiteral("none")
            : (invocationOperatorAction.isEmpty()
                ? resultCaptureOperatorAction
                : invocationOperatorAction);
        result[QStringLiteral("inputContract")] =
            QJsonArray::fromStringList(productionOperationInputContract(operation));
        result[QStringLiteral("outputContract")] =
            QJsonArray::fromStringList(productionOperationOutputContract(operation));
        result[QStringLiteral("resultContract")] = QJsonArray::fromStringList({
            QStringLiteral("status-code-class"),
            QStringLiteral("output-contract-proof"),
            QStringLiteral("fixture-proof"),
            QStringLiteral("error-class"),
            QStringLiteral("material-export-proof"),
        });
        result[QStringLiteral("rawKeyExported")] = false;
        result[QStringLiteral("privateMaterialExported")] = false;
        result[QStringLiteral("sessionSecretExported")] = false;
        result[QStringLiteral("privateIdentityMaterialExported")] = false;
        result[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        results.append(result);

        if (captureReady) {
            ++captureReadyCount;
        } else {
            ++blockedResultCount;
        }
        if (sanitized) {
            ++sanitizedResultCount;
        }
        if (outputContractMatches) {
            ++outputContractProofCount;
        }
        if (fixtureMatches) {
            ++fixtureProofCount;
        }
        if (materialExportProof) {
            ++materialExportProofCount;
        }
        if (result.value(QStringLiteral("operationInvoked")).toBool(false)) {
            ++invokedOperationCount;
        }
        if (result.value(QStringLiteral("resultCaptured")).toBool(false)) {
            ++capturedResultCount;
        }
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && dryRun.value(QStringLiteral("accepted")).toBool(false)
        && captureReadyCount == cryptoOperations().size()
        && blockedResultCount == 0
        && sanitizedResultCount == cryptoOperations().size()
        && outputContractProofCount == cryptoOperations().size()
        && fixtureProofCount == cryptoOperations().size()
        && materialExportProofCount == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-result-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerInvocationDryRun")] = dryRun;
    status[QStringLiteral("providerProbeEvidence")] = probeEvidence;
    status[QStringLiteral("providerProbeReleaseGate")] =
        probeEvidence.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerProbeInvokedOperationCount")] =
        probeEvidence.value(QStringLiteral("invokedOperationCount")).toInt();
    status[QStringLiteral("providerProbeVectorPassCount")] =
        probeEvidence.value(QStringLiteral("vectorPassCount")).toInt();
    status[QStringLiteral("providerInvocationDryRunReleaseGate")] =
        dryRun.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerInvocationDryRunAccepted")] =
        dryRun.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredResultCount")] = cryptoOperations().size();
    status[QStringLiteral("captureReadyCount")] = captureReadyCount;
    status[QStringLiteral("blockedResultCount")] = blockedResultCount;
    status[QStringLiteral("sanitizedResultCount")] = sanitizedResultCount;
    status[QStringLiteral("outputContractProofCount")] = outputContractProofCount;
    status[QStringLiteral("fixtureProofCount")] = fixtureProofCount;
    status[QStringLiteral("materialExportProofCount")] = materialExportProofCount;
    status[QStringLiteral("invokedOperationCount")] = invokedOperationCount;
    status[QStringLiteral("capturedResultCount")] = capturedResultCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-invocation-results-ready")
        : (descriptor.linked
            ? QStringLiteral("production-provider-invocation-results-blocked-placeholder")
            : QStringLiteral("production-provider-invocation-results-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : (descriptor.linked
            ? (probeEvidence.value(QStringLiteral("invokedOperationCount")).toInt()
                == cryptoOperations().size()
                    ? QStringLiteral("production-provider-invocation-results-not-clean")
                    : resultCaptureBlockedReason)
            : QStringLiteral("production-provider-table-not-registered"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("capture-reviewed-provider-invocation-results-and-fix-mismatches")
            : QStringLiteral("register-reviewed-provider-table-before-result-capture"));
    status[QStringLiteral("results")] = results;
    status[QStringLiteral("operationInvoked")] = invokedOperationCount > 0;
    status[QStringLiteral("resultCaptured")] = capturedResultCount > 0;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderExecutionDecisionStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject invocationResult =
        productionProviderInvocationResultStatusForDescriptor(descriptor);
    const QJsonArray results =
        invocationResult.value(QStringLiteral("results")).toArray();

    QJsonArray decisions;
    int allowedDecisionCount = 0;
    int blockedDecisionCount = 0;
    int sanitizedDecisionCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject result = sequenceIndex < results.size()
            ? results.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool resultCaptureAccepted =
            invocationResult.value(QStringLiteral("accepted")).toBool(false);
        const bool resultCaptureReady =
            result.value(QStringLiteral("captureReady")).toBool(false);
        const bool outputContractMatched =
            result.value(QStringLiteral("outputContractMatched")).toBool(false);
        const bool fixtureMatched =
            result.value(QStringLiteral("fixtureHashMatched")).toBool(false);
        const bool noSensitiveExport =
            !result.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !result.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !result.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !result.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !result.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool executionAllowed = descriptor.linked
            && resultCaptureAccepted
            && resultCaptureReady
            && outputContractMatched
            && fixtureMatched
            && noSensitiveExport;

        QJsonObject decision;
        decision[QStringLiteral("sequenceIndex")] = sequenceIndex;
        decision[QStringLiteral("operation")] = cryptoOperationName(operation);
        decision[QStringLiteral("backendId")] = descriptor.id;
        decision[QStringLiteral("providerId")] = descriptor.providerId;
        decision[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        decision[QStringLiteral("providerSymbol")] =
            result.value(QStringLiteral("providerSymbol")).toString(
                productionOperationProviderSymbol(operation));
        decision[QStringLiteral("providerAbiSignature")] =
            result.value(QStringLiteral("providerAbiSignature")).toString(
                productionOperationProviderAbiSignature(operation));
        decision[QStringLiteral("fixtureHashSha256")] =
            result.value(QStringLiteral("fixtureHashSha256")).toString(
                productionHarnessFixtureHash(spec));
        decision[QStringLiteral("resultCaptureReady")] = resultCaptureReady;
        decision[QStringLiteral("resultCaptureAccepted")] = resultCaptureAccepted;
        decision[QStringLiteral("outputContractMatched")] = outputContractMatched;
        decision[QStringLiteral("fixtureHashMatched")] = fixtureMatched;
        decision[QStringLiteral("noSensitiveMaterialExport")] = noSensitiveExport;
        decision[QStringLiteral("reviewedProviderCallbackAllowed")] = executionAllowed;
        decision[QStringLiteral("operationInvoked")] = false;
        decision[QStringLiteral("resultCaptured")] = false;
        decision[QStringLiteral("decisionState")] = executionAllowed
            ? QStringLiteral("ready-for-reviewed-provider-callback")
            : (descriptor.linked
                ? QStringLiteral("blocked-linked-placeholder")
                : QStringLiteral("blocked-not-linked"));
        decision[QStringLiteral("blockedReason")] = executionAllowed
            ? QString()
            : result.value(QStringLiteral("blockedReason")).toString(
                invocationResult.value(QStringLiteral("blockedReason")).toString());
        decision[QStringLiteral("operatorAction")] = executionAllowed
            ? QStringLiteral("invoke-reviewed-provider-under-result-capture")
            : result.value(QStringLiteral("operatorAction")).toString(
                invocationResult.value(QStringLiteral("operatorAction")).toString());
        decision[QStringLiteral("rawKeyExported")] = false;
        decision[QStringLiteral("privateMaterialExported")] = false;
        decision[QStringLiteral("sessionSecretExported")] = false;
        decision[QStringLiteral("privateIdentityMaterialExported")] = false;
        decision[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        decisions.append(decision);

        if (executionAllowed) {
            ++allowedDecisionCount;
        } else {
            ++blockedDecisionCount;
        }
        if (noSensitiveExport) {
            ++sanitizedDecisionCount;
        }
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && invocationResult.value(QStringLiteral("accepted")).toBool(false)
        && allowedDecisionCount == cryptoOperations().size()
        && blockedDecisionCount == 0
        && sanitizedDecisionCount == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-execution-decision-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerInvocationResult")] = invocationResult;
    status[QStringLiteral("providerInvocationResultReleaseGate")] =
        invocationResult.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerInvocationResultAccepted")] =
        invocationResult.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredDecisionCount")] = cryptoOperations().size();
    status[QStringLiteral("allowedDecisionCount")] = allowedDecisionCount;
    status[QStringLiteral("blockedDecisionCount")] = blockedDecisionCount;
    status[QStringLiteral("sanitizedDecisionCount")] = sanitizedDecisionCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-execution-decision-ready")
        : (descriptor.linked
            ? QStringLiteral("production-provider-execution-decision-blocked-placeholder")
            : QStringLiteral("production-provider-execution-decision-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : invocationResult.value(QStringLiteral("blockedReason")).toString(
            descriptor.linked
                ? QStringLiteral("production-provider-execution-decision-placeholder")
                : QStringLiteral("production-provider-table-not-registered"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("complete-result-capture-before-reviewed-provider-callback")
            : QStringLiteral("register-reviewed-provider-table-before-execution-decision"));
    status[QStringLiteral("decisions")] = decisions;
    status[QStringLiteral("operationInvoked")] = false;
    status[QStringLiteral("resultCaptured")] = false;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderCallbackHarnessStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject executionDecision =
        productionProviderExecutionDecisionStatusForDescriptor(descriptor);
    const QJsonArray decisions =
        executionDecision.value(QStringLiteral("decisions")).toArray();

    QJsonArray callbacks;
    int armedCallbackCount = 0;
    int blockedCallbackCount = 0;
    int sanitizedCallbackCount = 0;
    int inputCapturePolicyCount = 0;
    int outputCapturePolicyCount = 0;
    int resultCapturePolicyCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject decision = sequenceIndex < decisions.size()
            ? decisions.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool decisionAccepted =
            executionDecision.value(QStringLiteral("accepted")).toBool(false);
        const bool callbackAllowed =
            decision.value(QStringLiteral("reviewedProviderCallbackAllowed")).toBool(false);
        const bool noSensitiveExport =
            decision.value(QStringLiteral("noSensitiveMaterialExport")).toBool(false)
            && !decision.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !decision.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !decision.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !decision.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !decision.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool inputCapturePolicy = true;
        const bool outputCapturePolicy = true;
        const bool resultCapturePolicy = true;
        const bool sanitized = inputCapturePolicy
            && outputCapturePolicy
            && resultCapturePolicy
            && noSensitiveExport;
        const bool harnessArmed = descriptor.linked
            && decisionAccepted
            && callbackAllowed
            && sanitized;

        QJsonObject callback;
        callback[QStringLiteral("sequenceIndex")] = sequenceIndex;
        callback[QStringLiteral("operation")] = cryptoOperationName(operation);
        callback[QStringLiteral("backendId")] = descriptor.id;
        callback[QStringLiteral("providerId")] = descriptor.providerId;
        callback[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        callback[QStringLiteral("providerSymbol")] =
            decision.value(QStringLiteral("providerSymbol")).toString(
                productionOperationProviderSymbol(operation));
        callback[QStringLiteral("providerAbiSignature")] =
            decision.value(QStringLiteral("providerAbiSignature")).toString(
                productionOperationProviderAbiSignature(operation));
        callback[QStringLiteral("fixtureHashSha256")] =
            decision.value(QStringLiteral("fixtureHashSha256")).toString(
                productionHarnessFixtureHash(spec));
        callback[QStringLiteral("executionDecision")] = decision;
        callback[QStringLiteral("executionDecisionReleaseGate")] =
            executionDecision.value(QStringLiteral("releaseGate")).toString();
        callback[QStringLiteral("executionDecisionAccepted")] = decisionAccepted;
        callback[QStringLiteral("reviewedProviderCallbackAllowed")] = callbackAllowed;
        callback[QStringLiteral("callbackHarnessArmed")] = harnessArmed;
        callback[QStringLiteral("callbackState")] = harnessArmed
            ? QStringLiteral("armed-for-reviewed-provider-callback")
            : (descriptor.linked
                ? QStringLiteral("blocked-linked-placeholder")
                : QStringLiteral("blocked-not-linked"));
        callback[QStringLiteral("inputCapturePolicy")] =
            QStringLiteral("sanitized-metadata-only-no-input-bytes");
        callback[QStringLiteral("outputCapturePolicy")] =
            QStringLiteral("sanitized-contract-only-no-output-bytes");
        callback[QStringLiteral("resultCapturePolicy")] =
            QStringLiteral("status-error-fixture-proof-only");
        callback[QStringLiteral("inputCapturePolicyAccepted")] = inputCapturePolicy;
        callback[QStringLiteral("outputCapturePolicyAccepted")] = outputCapturePolicy;
        callback[QStringLiteral("resultCapturePolicyAccepted")] = resultCapturePolicy;
        callback[QStringLiteral("operationInvoked")] = false;
        callback[QStringLiteral("inputBytesCaptured")] = false;
        callback[QStringLiteral("outputBytesCaptured")] = false;
        callback[QStringLiteral("resultCaptured")] = false;
        callback[QStringLiteral("blockedReason")] = harnessArmed
            ? QString()
            : decision.value(QStringLiteral("blockedReason")).toString(
                executionDecision.value(QStringLiteral("blockedReason")).toString());
        callback[QStringLiteral("operatorAction")] = harnessArmed
            ? QStringLiteral("invoke-reviewed-provider-callback-through-harness")
            : decision.value(QStringLiteral("operatorAction")).toString(
                executionDecision.value(QStringLiteral("operatorAction")).toString());
        callback[QStringLiteral("sanitized")] = sanitized;
        callback[QStringLiteral("rawKeyExported")] = false;
        callback[QStringLiteral("privateMaterialExported")] = false;
        callback[QStringLiteral("sessionSecretExported")] = false;
        callback[QStringLiteral("privateIdentityMaterialExported")] = false;
        callback[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        callbacks.append(callback);

        if (harnessArmed) {
            ++armedCallbackCount;
        } else {
            ++blockedCallbackCount;
        }
        if (sanitized) {
            ++sanitizedCallbackCount;
        }
        if (inputCapturePolicy) {
            ++inputCapturePolicyCount;
        }
        if (outputCapturePolicy) {
            ++outputCapturePolicyCount;
        }
        if (resultCapturePolicy) {
            ++resultCapturePolicyCount;
        }
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && executionDecision.value(QStringLiteral("accepted")).toBool(false)
        && armedCallbackCount == cryptoOperations().size()
        && blockedCallbackCount == 0
        && sanitizedCallbackCount == cryptoOperations().size()
        && inputCapturePolicyCount == cryptoOperations().size()
        && outputCapturePolicyCount == cryptoOperations().size()
        && resultCapturePolicyCount == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-callback-harness-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerExecutionDecision")] = executionDecision;
    status[QStringLiteral("providerExecutionDecisionReleaseGate")] =
        executionDecision.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerExecutionDecisionAccepted")] =
        executionDecision.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredCallbackCount")] = cryptoOperations().size();
    status[QStringLiteral("armedCallbackCount")] = armedCallbackCount;
    status[QStringLiteral("blockedCallbackCount")] = blockedCallbackCount;
    status[QStringLiteral("sanitizedCallbackCount")] = sanitizedCallbackCount;
    status[QStringLiteral("inputCapturePolicyCount")] = inputCapturePolicyCount;
    status[QStringLiteral("outputCapturePolicyCount")] = outputCapturePolicyCount;
    status[QStringLiteral("resultCapturePolicyCount")] = resultCapturePolicyCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-callback-harness-ready")
        : (descriptor.linked
            ? QStringLiteral("production-provider-callback-harness-blocked-placeholder")
            : QStringLiteral("production-provider-callback-harness-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : executionDecision.value(QStringLiteral("blockedReason")).toString(
            descriptor.linked
                ? QStringLiteral("production-provider-callback-harness-placeholder")
                : QStringLiteral("production-provider-table-not-registered"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("arm-reviewed-provider-callback-harness-after-execution-decision")
            : QStringLiteral("register-reviewed-provider-table-before-callback-harness"));
    status[QStringLiteral("callbacks")] = callbacks;
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

QJsonObject productionProviderVectorSelfTestStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject callbackHarness =
        productionProviderCallbackHarnessStatusForDescriptor(descriptor);
    const QJsonArray callbacks =
        callbackHarness.value(QStringLiteral("callbacks")).toArray();
    const QJsonObject probeEvidence =
        productionProviderInvocationExecutionProbeEvidenceForDescriptor(descriptor);
    const QJsonArray probes = probeEvidence.value(QStringLiteral("probes")).toArray();

    QJsonArray tests;
    int passedVectorCount = 0;
    int blockedVectorCount = 0;
    int knownAnswerReadyCount = 0;
    int roundTripReadyCount = 0;
    int sanitizedVectorCount = 0;
    int materialExportProofCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject callback = sequenceIndex < callbacks.size()
            ? callbacks.at(sequenceIndex).toObject()
            : QJsonObject();
        const QJsonObject probe = sequenceIndex < probes.size()
            ? probes.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool harnessAccepted =
            callbackHarness.value(QStringLiteral("accepted")).toBool(false);
        const bool callbackArmed =
            callback.value(QStringLiteral("callbackHarnessArmed")).toBool(false);
        const bool probeInvoked =
            probe.value(QStringLiteral("operationInvoked")).toBool(false);
        const bool probeVectorPassed =
            probe.value(QStringLiteral("providerVectorSetMatched")).toBool(false);
        const bool probeSanitized =
            probe.value(QStringLiteral("sanitized")).toBool(false)
            && !probe.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !probe.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !probe.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !probe.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !probe.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool noSensitiveExport =
            !callback.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !callback.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !callback.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !callback.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !callback.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool knownAnswerReady = probeInvoked
            && probeVectorPassed
            && probeSanitized
            && noSensitiveExport;
        const bool roundTripReady = knownAnswerReady
            && probe.value(QStringLiteral("roundTripPassed")).toBool(false);
        const bool vectorPassed = knownAnswerReady
            && noSensitiveExport
            && probe.value(QStringLiteral("providerSymbol")).toString()
                == productionOperationProviderSymbol(operation);
        const bool sanitized = noSensitiveExport;

        QJsonObject test;
        test[QStringLiteral("sequenceIndex")] = sequenceIndex;
        test[QStringLiteral("operation")] = cryptoOperationName(operation);
        test[QStringLiteral("backendId")] = descriptor.id;
        test[QStringLiteral("providerId")] = descriptor.providerId;
        test[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        test[QStringLiteral("vectorSet")] = spec.vectorSet;
        test[QStringLiteral("fixtureHashSha256")] =
            callback.value(QStringLiteral("fixtureHashSha256")).toString(
                productionHarnessFixtureHash(spec));
        test[QStringLiteral("providerSymbol")] =
            callback.value(QStringLiteral("providerSymbol")).toString(
                productionOperationProviderSymbol(operation));
        test[QStringLiteral("providerAbiSignature")] =
            callback.value(QStringLiteral("providerAbiSignature")).toString(
                productionOperationProviderAbiSignature(operation));
        test[QStringLiteral("providerCallbackHarness")] = callback;
        test[QStringLiteral("providerProbeEvidence")] = probe;
        test[QStringLiteral("providerCallbackHarnessReleaseGate")] =
            callbackHarness.value(QStringLiteral("releaseGate")).toString();
        test[QStringLiteral("providerCallbackHarnessAccepted")] = harnessAccepted;
        test[QStringLiteral("callbackHarnessArmed")] = callbackArmed;
        test[QStringLiteral("providerProbeInvoked")] = probeInvoked;
        test[QStringLiteral("providerProbeVectorMatched")] = probeVectorPassed;
        test[QStringLiteral("providerProbeOutputShapeHashSha256")] =
            probe.value(QStringLiteral("outputShapeHashSha256")).toString();
        test[QStringLiteral("knownAnswerVectorReady")] = knownAnswerReady;
        test[QStringLiteral("roundTripVectorReady")] = roundTripReady;
        test[QStringLiteral("knownAnswerPassed")] =
            probe.value(QStringLiteral("knownAnswerPassed")).toBool(false);
        test[QStringLiteral("roundTripPassed")] =
            probe.value(QStringLiteral("roundTripPassed")).toBool(false);
        test[QStringLiteral("vectorPassed")] = vectorPassed;
        test[QStringLiteral("vectorExecutionState")] = vectorPassed
            ? QStringLiteral("passed-reviewed-provider-vector")
            : (descriptor.linked
                ? QStringLiteral("blocked-linked-placeholder")
                : QStringLiteral("blocked-not-linked"));
        test[QStringLiteral("operationInvoked")] = probeInvoked;
        test[QStringLiteral("inputBytesCaptured")] = false;
        test[QStringLiteral("outputBytesCaptured")] = false;
        test[QStringLiteral("resultCaptured")] =
            probe.value(QStringLiteral("resultCaptured")).toBool(false);
        test[QStringLiteral("blockedReason")] = vectorPassed
            ? QString()
            : callback.value(QStringLiteral("blockedReason")).toString(
                callbackHarness.value(QStringLiteral("blockedReason")).toString());
        test[QStringLiteral("operatorAction")] = vectorPassed
            ? QStringLiteral("none")
            : (descriptor.linked
                ? QStringLiteral("run-reviewed-provider-vector-self-tests")
                : QStringLiteral("register-reviewed-provider-table-before-vector-self-test"));
        test[QStringLiteral("sanitized")] = sanitized;
        test[QStringLiteral("materialExportProof")] = noSensitiveExport
            ? QStringLiteral("no-sensitive-material-export")
            : QStringLiteral("sensitive-material-exported");
        test[QStringLiteral("rawKeyExported")] = false;
        test[QStringLiteral("privateMaterialExported")] = false;
        test[QStringLiteral("sessionSecretExported")] = false;
        test[QStringLiteral("privateIdentityMaterialExported")] = false;
        test[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        tests.append(test);

        if (vectorPassed) {
            ++passedVectorCount;
        } else {
            ++blockedVectorCount;
        }
        if (knownAnswerReady) {
            ++knownAnswerReadyCount;
        }
        if (roundTripReady) {
            ++roundTripReadyCount;
        }
        if (sanitized) {
            ++sanitizedVectorCount;
        }
        if (noSensitiveExport) {
            ++materialExportProofCount;
        }
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && passedVectorCount == cryptoOperations().size()
        && blockedVectorCount == 0
        && knownAnswerReadyCount == cryptoOperations().size()
        && sanitizedVectorCount == cryptoOperations().size()
        && materialExportProofCount == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-vector-self-test-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerCallbackHarness")] = callbackHarness;
    status[QStringLiteral("providerProbeEvidence")] = probeEvidence;
    status[QStringLiteral("providerProbeInvokedOperationCount")] =
        probeEvidence.value(QStringLiteral("invokedOperationCount")).toInt();
    status[QStringLiteral("providerProbeVectorPassCount")] =
        probeEvidence.value(QStringLiteral("vectorPassCount")).toInt();
    status[QStringLiteral("providerCallbackHarnessReleaseGate")] =
        callbackHarness.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerCallbackHarnessAccepted")] =
        callbackHarness.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredVectorCount")] = cryptoOperations().size();
    status[QStringLiteral("passedVectorCount")] = passedVectorCount;
    status[QStringLiteral("blockedVectorCount")] = blockedVectorCount;
    status[QStringLiteral("knownAnswerReadyCount")] = knownAnswerReadyCount;
    status[QStringLiteral("roundTripReadyCount")] = roundTripReadyCount;
    status[QStringLiteral("sanitizedVectorCount")] = sanitizedVectorCount;
    status[QStringLiteral("materialExportProofCount")] = materialExportProofCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-vector-self-test-ready")
        : (descriptor.linked
            ? QStringLiteral("production-provider-vector-self-test-blocked-placeholder")
            : QStringLiteral("production-provider-vector-self-test-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : callbackHarness.value(QStringLiteral("blockedReason")).toString(
            descriptor.linked
                ? QStringLiteral("production-provider-vector-self-test-placeholder")
                : QStringLiteral("production-provider-table-not-registered"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("run-reviewed-provider-vector-self-tests")
            : QStringLiteral("register-reviewed-provider-table-before-vector-self-test"));
    status[QStringLiteral("tests")] = tests;
    status[QStringLiteral("operationInvoked")] =
        probeEvidence.value(QStringLiteral("invokedOperationCount")).toInt() > 0;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultCaptured")] =
        probeEvidence.value(QStringLiteral("capturedResultCount")).toInt() > 0;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderExecutionSlotBindingStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject vectorSelfTest =
        productionProviderVectorSelfTestStatusForDescriptor(descriptor);
    const QJsonArray vectorTests =
        vectorSelfTest.value(QStringLiteral("tests")).toArray();

    QJsonArray slotBindings;
    int bindableSlotCount = 0;
    int blockedSlotCount = 0;
    int reviewedSlotCount = 0;
    int contractMatchedSlotCount = 0;
    int fixtureMatchedSlotCount = 0;
    int sanitizedSlotCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject vectorTest = sequenceIndex < vectorTests.size()
            ? vectorTests.at(sequenceIndex).toObject()
            : QJsonObject();
        const QStringList inputContract = productionOperationInputContract(operation);
        const QStringList outputContract = productionOperationOutputContract(operation);
        const QString inputHash = e2eFingerprint(inputContract.join(QLatin1Char('|')).toUtf8());
        const QString outputHash = e2eFingerprint(outputContract.join(QLatin1Char('|')).toUtf8());
        const bool vectorAccepted =
            vectorSelfTest.value(QStringLiteral("accepted")).toBool(false);
        const bool vectorPassed =
            vectorTest.value(QStringLiteral("vectorPassed")).toBool(false);
        const bool contractMatched =
            inputHash.size() == FingerprintHexLength
            && outputHash.size() == FingerprintHexLength
            && vectorTest.value(QStringLiteral("providerSymbol")).toString()
                == productionOperationProviderSymbol(operation);
        const bool fixtureMatched =
            vectorTest.value(QStringLiteral("fixtureHashSha256")).toString()
                == productionHarnessFixtureHash(spec);
        const bool reviewed = descriptor.linked
            && vectorAccepted
            && vectorPassed;
        const bool noSensitiveExport =
            !vectorTest.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !vectorTest.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !vectorTest.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !vectorTest.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !vectorTest.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool bindable = reviewed
            && contractMatched
            && fixtureMatched
            && noSensitiveExport;

        QJsonObject slot;
        slot[QStringLiteral("sequenceIndex")] = sequenceIndex;
        slot[QStringLiteral("operation")] = cryptoOperationName(operation);
        slot[QStringLiteral("slotId")] =
            descriptor.id + QStringLiteral("/") + cryptoOperationName(operation)
            + QStringLiteral("/reviewed-execution-slot");
        slot[QStringLiteral("backendId")] = descriptor.id;
        slot[QStringLiteral("providerId")] = descriptor.providerId;
        slot[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        slot[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
        slot[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        slot[QStringLiteral("vectorSet")] = spec.vectorSet;
        slot[QStringLiteral("fixtureHashSha256")] = productionHarnessFixtureHash(spec);
        slot[QStringLiteral("inputContractHashSha256")] = inputHash;
        slot[QStringLiteral("outputContractHashSha256")] = outputHash;
        slot[QStringLiteral("providerVectorSelfTest")] = vectorTest;
        slot[QStringLiteral("providerVectorSelfTestReleaseGate")] =
            vectorSelfTest.value(QStringLiteral("releaseGate")).toString();
        slot[QStringLiteral("providerVectorSelfTestAccepted")] = vectorAccepted;
        slot[QStringLiteral("vectorPassed")] = vectorPassed;
        slot[QStringLiteral("reviewed")] = reviewed;
        slot[QStringLiteral("contractMatched")] = contractMatched;
        slot[QStringLiteral("fixtureHashMatched")] = fixtureMatched;
        slot[QStringLiteral("executionSlotBindable")] = bindable;
        slot[QStringLiteral("bindingState")] = bindable
            ? QStringLiteral("bindable-reviewed-provider-execution-slot")
            : (descriptor.linked
                ? QStringLiteral("blocked-linked-placeholder")
                : QStringLiteral("blocked-not-linked"));
        slot[QStringLiteral("operationInvoked")] = false;
        slot[QStringLiteral("inputBytesCaptured")] = false;
        slot[QStringLiteral("outputBytesCaptured")] = false;
        slot[QStringLiteral("resultCaptured")] = false;
        slot[QStringLiteral("blockedReason")] = bindable
            ? QString()
            : vectorTest.value(QStringLiteral("blockedReason")).toString(
                vectorSelfTest.value(QStringLiteral("blockedReason")).toString());
        slot[QStringLiteral("operatorAction")] = bindable
            ? QStringLiteral("none")
            : (descriptor.linked
                ? QStringLiteral("bind-reviewed-provider-execution-slot-after-vector-self-test")
                : QStringLiteral("register-reviewed-provider-table-before-execution-slot-binding"));
        slot[QStringLiteral("sanitized")] = noSensitiveExport;
        slot[QStringLiteral("rawKeyExported")] = false;
        slot[QStringLiteral("privateMaterialExported")] = false;
        slot[QStringLiteral("sessionSecretExported")] = false;
        slot[QStringLiteral("privateIdentityMaterialExported")] = false;
        slot[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        slotBindings.append(slot);

        if (bindable) {
            ++bindableSlotCount;
        } else {
            ++blockedSlotCount;
        }
        if (reviewed) {
            ++reviewedSlotCount;
        }
        if (contractMatched) {
            ++contractMatchedSlotCount;
        }
        if (fixtureMatched) {
            ++fixtureMatchedSlotCount;
        }
        if (noSensitiveExport) {
            ++sanitizedSlotCount;
        }
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && vectorSelfTest.value(QStringLiteral("accepted")).toBool(false)
        && bindableSlotCount == cryptoOperations().size()
        && blockedSlotCount == 0
        && reviewedSlotCount == cryptoOperations().size()
        && contractMatchedSlotCount == cryptoOperations().size()
        && fixtureMatchedSlotCount == cryptoOperations().size()
        && sanitizedSlotCount == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-execution-slot-binding-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerVectorSelfTest")] = vectorSelfTest;
    status[QStringLiteral("providerVectorSelfTestReleaseGate")] =
        vectorSelfTest.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerVectorSelfTestAccepted")] =
        vectorSelfTest.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredSlotCount")] = cryptoOperations().size();
    status[QStringLiteral("bindableSlotCount")] = bindableSlotCount;
    status[QStringLiteral("blockedSlotCount")] = blockedSlotCount;
    status[QStringLiteral("reviewedSlotCount")] = reviewedSlotCount;
    status[QStringLiteral("contractMatchedSlotCount")] = contractMatchedSlotCount;
    status[QStringLiteral("fixtureMatchedSlotCount")] = fixtureMatchedSlotCount;
    status[QStringLiteral("sanitizedSlotCount")] = sanitizedSlotCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-execution-slot-binding-ready")
        : (descriptor.linked
            ? QStringLiteral("production-provider-execution-slot-binding-blocked-placeholder")
            : QStringLiteral("production-provider-execution-slot-binding-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : vectorSelfTest.value(QStringLiteral("blockedReason")).toString(
            descriptor.linked
                ? QStringLiteral("production-provider-execution-slot-binding-placeholder")
                : QStringLiteral("production-provider-table-not-registered"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("bind-reviewed-provider-execution-slots")
            : QStringLiteral("register-reviewed-provider-table-before-execution-slot-binding"));
    status[QStringLiteral("slots")] = slotBindings;
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

QJsonObject productionProviderExecutionPathStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const qnc_e2e_provider_table_v1* registeredTable = activeProductionProviderTable();
    const QJsonObject slotBinding =
        productionProviderExecutionSlotBindingStatusForDescriptor(descriptor);
    const QJsonArray boundSlots =
        slotBinding.value(QStringLiteral("slots")).toArray();
    const QJsonObject registration =
        productionProviderTableRegistrationStatusForDescriptor(descriptor);
    const bool tableRegistered =
        registration.value(QStringLiteral("registered")).toBool(false);
    const bool tableValidationAccepted =
        registration.value(QStringLiteral("tableValidationAccepted")).toBool(false);

    QJsonArray paths;
    int mappedPathCount = 0;
    int blockedPathCount = 0;
    int bindableSlotCount = 0;
    int pointerPresentCount = 0;
    int capturePolicyCount = 0;
    int sanitizedPathCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject slot = sequenceIndex < boundSlots.size()
            ? boundSlots.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool pointerPresent =
            providerOperationPointer(registeredTable, operation) != nullptr;
        const bool slotBindable =
            slot.value(QStringLiteral("executionSlotBindable")).toBool(false);
        const bool symbolMatched =
            slot.value(QStringLiteral("providerSymbol")).toString()
            == productionOperationProviderSymbol(operation);
        const bool abiMatched =
            slot.value(QStringLiteral("providerAbiSignature")).toString()
            == productionOperationProviderAbiSignature(operation);
        const bool fixtureMatched =
            slot.value(QStringLiteral("fixtureHashSha256")).toString()
            == productionHarnessFixtureHash(spec);
        const bool capturePolicy = true;
        const bool noSensitiveExport =
            !slot.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !slot.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !slot.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !slot.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !slot.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool mapped = descriptor.linked
            && slotBinding.value(QStringLiteral("accepted")).toBool(false)
            && tableRegistered
            && tableValidationAccepted
            && pointerPresent
            && slotBindable
            && symbolMatched
            && abiMatched
            && fixtureMatched
            && capturePolicy
            && noSensitiveExport;

        QJsonObject path;
        path[QStringLiteral("sequenceIndex")] = sequenceIndex;
        path[QStringLiteral("operation")] = cryptoOperationName(operation);
        path[QStringLiteral("backendId")] = descriptor.id;
        path[QStringLiteral("providerId")] = descriptor.providerId;
        path[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        path[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
        path[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        path[QStringLiteral("fixtureHashSha256")] = productionHarnessFixtureHash(spec);
        path[QStringLiteral("slotId")] =
            slot.value(QStringLiteral("slotId")).toString(
                descriptor.id + QStringLiteral("/") + cryptoOperationName(operation)
                + QStringLiteral("/reviewed-execution-slot"));
        path[QStringLiteral("providerExecutionSlot")] = slot;
        path[QStringLiteral("providerExecutionSlotBindingReleaseGate")] =
            slotBinding.value(QStringLiteral("releaseGate")).toString();
        path[QStringLiteral("providerExecutionSlotBindingAccepted")] =
            slotBinding.value(QStringLiteral("accepted")).toBool(false);
        path[QStringLiteral("providerTableRegistrationReleaseGate")] =
            registration.value(QStringLiteral("releaseGate")).toString();
        path[QStringLiteral("providerTableRegistered")] = tableRegistered;
        path[QStringLiteral("tableValidationAccepted")] = tableValidationAccepted;
        path[QStringLiteral("functionPointerPresent")] = pointerPresent;
        path[QStringLiteral("executionSlotBindable")] = slotBindable;
        path[QStringLiteral("symbolMatched")] = symbolMatched;
        path[QStringLiteral("abiMatched")] = abiMatched;
        path[QStringLiteral("fixtureHashMatched")] = fixtureMatched;
        path[QStringLiteral("capturePolicyReady")] = capturePolicy;
        path[QStringLiteral("executionPathMapped")] = mapped;
        path[QStringLiteral("pathState")] = mapped
            ? QStringLiteral("mapped-reviewed-provider-execution-path")
            : (descriptor.linked
                ? QStringLiteral("blocked-linked-placeholder")
                : QStringLiteral("blocked-not-linked"));
        path[QStringLiteral("operationInvoked")] = false;
        path[QStringLiteral("inputBytesCaptured")] = false;
        path[QStringLiteral("outputBytesCaptured")] = false;
        path[QStringLiteral("resultCaptured")] = false;
        path[QStringLiteral("blockedReason")] = mapped
            ? QString()
            : (!tableRegistered
                ? QStringLiteral("production-provider-table-not-registered")
                : (!pointerPresent
                    ? QStringLiteral("production-provider-operation-pointer-missing")
                    : slot.value(QStringLiteral("blockedReason")).toString(
                        slotBinding.value(QStringLiteral("blockedReason")).toString())));
        path[QStringLiteral("operatorAction")] = mapped
            ? QStringLiteral("none")
            : (!tableRegistered
                ? QStringLiteral("register-reviewed-provider-table-before-execution-path")
                : (!pointerPresent
                    ? QStringLiteral("register-provider-table-with-all-required-operation-pointers")
                    : QStringLiteral("enable-reviewed-provider-execution-path-after-slot-binding")));
        path[QStringLiteral("sanitized")] = noSensitiveExport;
        path[QStringLiteral("rawKeyExported")] = false;
        path[QStringLiteral("privateMaterialExported")] = false;
        path[QStringLiteral("sessionSecretExported")] = false;
        path[QStringLiteral("privateIdentityMaterialExported")] = false;
        path[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        paths.append(path);

        if (mapped) {
            ++mappedPathCount;
        } else {
            ++blockedPathCount;
        }
        if (slotBindable) {
            ++bindableSlotCount;
        }
        if (pointerPresent) {
            ++pointerPresentCount;
        }
        if (capturePolicy) {
            ++capturePolicyCount;
        }
        if (noSensitiveExport) {
            ++sanitizedPathCount;
        }
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && slotBinding.value(QStringLiteral("accepted")).toBool(false)
        && tableRegistered
        && tableValidationAccepted
        && mappedPathCount == cryptoOperations().size()
        && blockedPathCount == 0
        && pointerPresentCount == cryptoOperations().size()
        && bindableSlotCount == cryptoOperations().size()
        && capturePolicyCount == cryptoOperations().size()
        && sanitizedPathCount == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-execution-path-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerExecutionSlotBinding")] = slotBinding;
    status[QStringLiteral("providerExecutionSlotBindingReleaseGate")] =
        slotBinding.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerExecutionSlotBindingAccepted")] =
        slotBinding.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("providerTableRegistration")] = registration;
    status[QStringLiteral("providerTableRegistrationReleaseGate")] =
        registration.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerTableRegistered")] = tableRegistered;
    status[QStringLiteral("tableValidationAccepted")] = tableValidationAccepted;
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredPathCount")] = cryptoOperations().size();
    status[QStringLiteral("mappedPathCount")] = mappedPathCount;
    status[QStringLiteral("blockedPathCount")] = blockedPathCount;
    status[QStringLiteral("pointerPresentCount")] = pointerPresentCount;
    status[QStringLiteral("bindableSlotCount")] = bindableSlotCount;
    status[QStringLiteral("capturePolicyCount")] = capturePolicyCount;
    status[QStringLiteral("sanitizedPathCount")] = sanitizedPathCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-execution-path-ready")
        : (tableRegistered
            ? QStringLiteral("production-provider-execution-path-blocked-not-production-ready")
            : (descriptor.linked
                ? QStringLiteral("production-provider-execution-path-blocked-placeholder")
                : QStringLiteral("production-provider-execution-path-blocked-not-linked")));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : (!tableRegistered
            ? QStringLiteral("production-provider-table-not-registered")
            : slotBinding.value(QStringLiteral("blockedReason")).toString(
                descriptor.linked
                    ? QStringLiteral("production-provider-execution-path-placeholder")
                    : QStringLiteral("production-provider-table-not-registered")));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (!tableRegistered
            ? QStringLiteral("register-reviewed-provider-table-before-execution-path")
            : QStringLiteral("map-reviewed-provider-execution-paths-after-slot-binding"));
    status[QStringLiteral("paths")] = paths;
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

QJsonObject productionProviderInvocationSandboxStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject executionPath =
        productionProviderExecutionPathStatusForDescriptor(descriptor);
    const QJsonArray paths =
        executionPath.value(QStringLiteral("paths")).toArray();

    QJsonArray sandboxes;
    int readySandboxCount = 0;
    int blockedSandboxCount = 0;
    int mappedPathCount = 0;
    int sanitizedSandboxCount = 0;
    int timeoutPolicyCount = 0;
    int errorPolicyCount = 0;
    int materialPolicyCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject path = sequenceIndex < paths.size()
            ? paths.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool pathAccepted =
            executionPath.value(QStringLiteral("accepted")).toBool(false);
        const bool pathMapped =
            path.value(QStringLiteral("executionPathMapped")).toBool(false);
        const bool timeoutPolicy = true;
        const bool errorPolicy = true;
        const bool materialPolicy =
            (operation == E2ECryptoOperation::PayloadEncrypt
             || operation == E2ECryptoOperation::PayloadDecrypt)
                ? path.value(QStringLiteral("providerSymbol")).toString()
                    == productionOperationProviderSymbol(operation)
                : true;
        const bool noSensitiveExport =
            !path.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !path.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !path.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !path.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !path.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool sanitized = timeoutPolicy
            && errorPolicy
            && materialPolicy
            && noSensitiveExport;
        const bool sandboxReady = descriptor.linked
            && pathAccepted
            && pathMapped
            && sanitized;

        QJsonObject sandbox;
        sandbox[QStringLiteral("sequenceIndex")] = sequenceIndex;
        sandbox[QStringLiteral("operation")] = cryptoOperationName(operation);
        sandbox[QStringLiteral("backendId")] = descriptor.id;
        sandbox[QStringLiteral("providerId")] = descriptor.providerId;
        sandbox[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        sandbox[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
        sandbox[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        sandbox[QStringLiteral("fixtureHashSha256")] = productionHarnessFixtureHash(spec);
        sandbox[QStringLiteral("providerExecutionPath")] = path;
        sandbox[QStringLiteral("providerExecutionPathReleaseGate")] =
            executionPath.value(QStringLiteral("releaseGate")).toString();
        sandbox[QStringLiteral("providerExecutionPathAccepted")] = pathAccepted;
        sandbox[QStringLiteral("executionPathMapped")] = pathMapped;
        sandbox[QStringLiteral("timeoutPolicyReady")] = timeoutPolicy;
        sandbox[QStringLiteral("timeoutMs")] = 2500;
        sandbox[QStringLiteral("errorPolicyReady")] = errorPolicy;
        sandbox[QStringLiteral("sanitizedErrorClasses")] = QJsonArray::fromStringList({
            QStringLiteral("ok"),
            QStringLiteral("rejected"),
            QStringLiteral("invalid-input"),
            QStringLiteral("unsupported"),
            QStringLiteral("provider-error"),
        });
        sandbox[QStringLiteral("materialPolicyReady")] = materialPolicy;
        sandbox[QStringLiteral("inputCapturePolicy")] =
            QStringLiteral("fixture-class-and-size-only");
        sandbox[QStringLiteral("outputCapturePolicy")] =
            QStringLiteral("contract-proof-and-size-only");
        sandbox[QStringLiteral("resultCapturePolicy")] =
            QStringLiteral("status-class-without-secret-bytes");
        sandbox[QStringLiteral("sandboxReady")] = sandboxReady;
        sandbox[QStringLiteral("sandboxState")] = sandboxReady
            ? QStringLiteral("ready-for-reviewed-provider-invocation")
            : (descriptor.linked
                ? QStringLiteral("blocked-linked-placeholder")
                : QStringLiteral("blocked-not-linked"));
        sandbox[QStringLiteral("operationInvoked")] = false;
        sandbox[QStringLiteral("inputBytesCaptured")] = false;
        sandbox[QStringLiteral("outputBytesCaptured")] = false;
        sandbox[QStringLiteral("resultCaptured")] = false;
        sandbox[QStringLiteral("blockedReason")] = sandboxReady
            ? QString()
            : path.value(QStringLiteral("blockedReason")).toString(
                executionPath.value(QStringLiteral("blockedReason")).toString());
        sandbox[QStringLiteral("operatorAction")] = sandboxReady
            ? QStringLiteral("none")
            : (descriptor.linked
                ? QStringLiteral("run-reviewed-provider-inside-invocation-sandbox")
                : QStringLiteral("register-reviewed-provider-table-before-invocation-sandbox"));
        sandbox[QStringLiteral("sanitized")] = sanitized;
        sandbox[QStringLiteral("rawKeyExported")] = false;
        sandbox[QStringLiteral("privateMaterialExported")] = false;
        sandbox[QStringLiteral("sessionSecretExported")] = false;
        sandbox[QStringLiteral("privateIdentityMaterialExported")] = false;
        sandbox[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        sandboxes.append(sandbox);

        if (sandboxReady) {
            ++readySandboxCount;
        } else {
            ++blockedSandboxCount;
        }
        if (pathMapped) {
            ++mappedPathCount;
        }
        if (sanitized) {
            ++sanitizedSandboxCount;
        }
        if (timeoutPolicy) {
            ++timeoutPolicyCount;
        }
        if (errorPolicy) {
            ++errorPolicyCount;
        }
        if (materialPolicy) {
            ++materialPolicyCount;
        }
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && executionPath.value(QStringLiteral("accepted")).toBool(false)
        && readySandboxCount == cryptoOperations().size()
        && blockedSandboxCount == 0
        && mappedPathCount == cryptoOperations().size()
        && sanitizedSandboxCount == cryptoOperations().size()
        && timeoutPolicyCount == cryptoOperations().size()
        && errorPolicyCount == cryptoOperations().size()
        && materialPolicyCount == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-sandbox-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerExecutionPath")] = executionPath;
    status[QStringLiteral("providerExecutionPathReleaseGate")] =
        executionPath.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerExecutionPathAccepted")] =
        executionPath.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredSandboxCount")] = cryptoOperations().size();
    status[QStringLiteral("readySandboxCount")] = readySandboxCount;
    status[QStringLiteral("blockedSandboxCount")] = blockedSandboxCount;
    status[QStringLiteral("mappedPathCount")] = mappedPathCount;
    status[QStringLiteral("sanitizedSandboxCount")] = sanitizedSandboxCount;
    status[QStringLiteral("timeoutPolicyCount")] = timeoutPolicyCount;
    status[QStringLiteral("errorPolicyCount")] = errorPolicyCount;
    status[QStringLiteral("materialPolicyCount")] = materialPolicyCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-invocation-sandbox-ready")
        : (descriptor.linked
            ? QStringLiteral("production-provider-invocation-sandbox-blocked-placeholder")
            : QStringLiteral("production-provider-invocation-sandbox-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : executionPath.value(QStringLiteral("blockedReason")).toString(
            descriptor.linked
                ? QStringLiteral("production-provider-invocation-sandbox-placeholder")
                : QStringLiteral("production-provider-table-not-registered"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("invoke-reviewed-provider-through-sandbox")
            : QStringLiteral("register-reviewed-provider-table-before-invocation-sandbox"));
    status[QStringLiteral("sandboxes")] = sandboxes;
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

QJsonObject productionProviderInvocationVectorResultStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject invocationSandbox =
        productionProviderInvocationSandboxStatusForDescriptor(descriptor);
    const QJsonArray sandboxes =
        invocationSandbox.value(QStringLiteral("sandboxes")).toArray();
    const QJsonObject probeEvidence =
        productionProviderInvocationExecutionProbeEvidenceForDescriptor(descriptor);
    const QJsonArray probes = probeEvidence.value(QStringLiteral("probes")).toArray();

    QJsonArray vectorResults;
    int readyVectorResultCount = 0;
    int blockedVectorResultCount = 0;
    int sandboxReadyCount = 0;
    int fixtureMatchedCount = 0;
    int resultContractCount = 0;
    int sanitizedResultCount = 0;
    int materialExportProofCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject sandbox = sequenceIndex < sandboxes.size()
            ? sandboxes.at(sequenceIndex).toObject()
            : QJsonObject();
        const QJsonObject probe = sequenceIndex < probes.size()
            ? probes.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool sandboxAccepted =
            invocationSandbox.value(QStringLiteral("accepted")).toBool(false);
        const bool sandboxReady =
            sandbox.value(QStringLiteral("sandboxReady")).toBool(false);
        const bool probeInvoked =
            probe.value(QStringLiteral("operationInvoked")).toBool(false);
        const bool probeVectorPassed =
            probe.value(QStringLiteral("providerVectorSetMatched")).toBool(false);
        const bool probeSanitized =
            probe.value(QStringLiteral("sanitized")).toBool(false)
            && !probe.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !probe.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !probe.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !probe.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !probe.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool fixtureMatched =
            sandbox.value(QStringLiteral("fixtureHashSha256")).toString()
            == productionHarnessFixtureHash(spec);
        const bool resultContractReady =
            sandbox.value(QStringLiteral("resultCapturePolicy")).toString()
            == QStringLiteral("status-class-without-secret-bytes");
        const bool noSensitiveExport =
            !sandbox.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !sandbox.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !sandbox.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !sandbox.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !sandbox.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool sanitized = resultContractReady
            && noSensitiveExport
            && (!probeInvoked || probeSanitized);
        const bool vectorReady = probeInvoked
            && probeVectorPassed
            && fixtureMatched
            && resultContractReady
            && sanitized;

        QJsonObject result;
        result[QStringLiteral("sequenceIndex")] = sequenceIndex;
        result[QStringLiteral("operation")] = cryptoOperationName(operation);
        result[QStringLiteral("backendId")] = descriptor.id;
        result[QStringLiteral("providerId")] = descriptor.providerId;
        result[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        result[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
        result[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        result[QStringLiteral("vectorSet")] = spec.vectorSet;
        result[QStringLiteral("fixtureHashSha256")] = productionHarnessFixtureHash(spec);
        result[QStringLiteral("providerInvocationSandbox")] = sandbox;
        result[QStringLiteral("providerProbeEvidence")] = probe;
        result[QStringLiteral("providerInvocationSandboxReleaseGate")] =
            invocationSandbox.value(QStringLiteral("releaseGate")).toString();
        result[QStringLiteral("providerInvocationSandboxAccepted")] = sandboxAccepted;
        result[QStringLiteral("sandboxReady")] = sandboxReady;
        result[QStringLiteral("providerProbeInvoked")] = probeInvoked;
        result[QStringLiteral("providerProbeVectorMatched")] = probeVectorPassed;
        result[QStringLiteral("providerProbeOutputShapeHashSha256")] =
            probe.value(QStringLiteral("outputShapeHashSha256")).toString();
        result[QStringLiteral("fixtureHashMatched")] = fixtureMatched;
        result[QStringLiteral("resultContractReady")] = resultContractReady;
        result[QStringLiteral("statusCodeClass")] = vectorReady
            ? probe.value(QStringLiteral("callbackStatusClass")).toString(
                QStringLiteral("qnc-e2e-status-ok-or-sanitized-error"))
            : QStringLiteral("not-invoked");
        result[QStringLiteral("sanitizedErrorClass")] = vectorReady
            ? probe.value(QStringLiteral("sanitizedErrorClass")).toString(
                QStringLiteral("ok-or-sanitized-provider-error"))
            : QStringLiteral("not-invoked");
        result[QStringLiteral("knownAnswerVectorResultReady")] = vectorReady;
        result[QStringLiteral("roundTripVectorResultReady")] = vectorReady
            && probe.value(QStringLiteral("roundTripPassed")).toBool(false);
        result[QStringLiteral("knownAnswerPassed")] =
            probe.value(QStringLiteral("knownAnswerPassed")).toBool(false);
        result[QStringLiteral("roundTripPassed")] =
            probe.value(QStringLiteral("roundTripPassed")).toBool(false);
        result[QStringLiteral("vectorResultState")] = vectorReady
            ? QStringLiteral("ready-for-reviewed-provider-vector-result")
            : (descriptor.linked
                ? QStringLiteral("blocked-linked-placeholder")
                : QStringLiteral("blocked-not-linked"));
        result[QStringLiteral("operationInvoked")] = probeInvoked;
        result[QStringLiteral("inputBytesCaptured")] = false;
        result[QStringLiteral("outputBytesCaptured")] = false;
        result[QStringLiteral("resultCaptured")] =
            probe.value(QStringLiteral("resultCaptured")).toBool(false);
        result[QStringLiteral("blockedReason")] = vectorReady
            ? QString()
            : sandbox.value(QStringLiteral("blockedReason")).toString(
                invocationSandbox.value(QStringLiteral("blockedReason")).toString());
        result[QStringLiteral("operatorAction")] = vectorReady
            ? QStringLiteral("none")
            : (descriptor.linked
                ? QStringLiteral("record-reviewed-provider-vector-results")
                : QStringLiteral("register-reviewed-provider-table-before-vector-result"));
        result[QStringLiteral("resultContract")] = QJsonArray::fromStringList({
            QStringLiteral("status-code-class"),
            QStringLiteral("sanitized-error-class"),
            QStringLiteral("fixture-proof"),
            QStringLiteral("material-export-proof"),
        });
        result[QStringLiteral("materialExportProof")] = noSensitiveExport
            ? QStringLiteral("no-sensitive-material-export")
            : QStringLiteral("sensitive-material-exported");
        result[QStringLiteral("sanitized")] = sanitized;
        result[QStringLiteral("rawKeyExported")] = false;
        result[QStringLiteral("privateMaterialExported")] = false;
        result[QStringLiteral("sessionSecretExported")] = false;
        result[QStringLiteral("privateIdentityMaterialExported")] = false;
        result[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
        vectorResults.append(result);

        if (vectorReady) {
            ++readyVectorResultCount;
        } else {
            ++blockedVectorResultCount;
        }
        if (sandboxReady) {
            ++sandboxReadyCount;
        }
        if (fixtureMatched) {
            ++fixtureMatchedCount;
        }
        if (resultContractReady) {
            ++resultContractCount;
        }
        if (sanitized) {
            ++sanitizedResultCount;
        }
        if (noSensitiveExport) {
            ++materialExportProofCount;
        }
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && readyVectorResultCount == cryptoOperations().size()
        && blockedVectorResultCount == 0
        && fixtureMatchedCount == cryptoOperations().size()
        && resultContractCount == cryptoOperations().size()
        && sanitizedResultCount == cryptoOperations().size()
        && materialExportProofCount == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-vector-result-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerInvocationSandbox")] = invocationSandbox;
    status[QStringLiteral("providerProbeEvidence")] = probeEvidence;
    status[QStringLiteral("providerProbeInvokedOperationCount")] =
        probeEvidence.value(QStringLiteral("invokedOperationCount")).toInt();
    status[QStringLiteral("providerProbeVectorPassCount")] =
        probeEvidence.value(QStringLiteral("vectorPassCount")).toInt();
    status[QStringLiteral("providerInvocationSandboxReleaseGate")] =
        invocationSandbox.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerInvocationSandboxAccepted")] =
        invocationSandbox.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredVectorResultCount")] = cryptoOperations().size();
    status[QStringLiteral("readyVectorResultCount")] = readyVectorResultCount;
    status[QStringLiteral("blockedVectorResultCount")] = blockedVectorResultCount;
    status[QStringLiteral("sandboxReadyCount")] = sandboxReadyCount;
    status[QStringLiteral("fixtureMatchedCount")] = fixtureMatchedCount;
    status[QStringLiteral("resultContractCount")] = resultContractCount;
    status[QStringLiteral("sanitizedResultCount")] = sanitizedResultCount;
    status[QStringLiteral("materialExportProofCount")] = materialExportProofCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-invocation-vector-result-ready")
        : (descriptor.linked
            ? QStringLiteral("production-provider-invocation-vector-result-blocked-placeholder")
            : QStringLiteral("production-provider-invocation-vector-result-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : invocationSandbox.value(QStringLiteral("blockedReason")).toString(
            descriptor.linked
                ? QStringLiteral("production-provider-invocation-vector-result-placeholder")
                : QStringLiteral("production-provider-table-not-registered"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("capture-reviewed-provider-vector-results")
            : QStringLiteral("register-reviewed-provider-table-before-vector-result"));
    status[QStringLiteral("vectorResults")] = vectorResults;
    status[QStringLiteral("operationInvoked")] =
        probeEvidence.value(QStringLiteral("invokedOperationCount")).toInt() > 0;
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultCaptured")] =
        probeEvidence.value(QStringLiteral("capturedResultCount")).toInt() > 0;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

QJsonObject productionProviderInvocationExecutionStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject vectorResult =
        productionProviderInvocationVectorResultStatusForDescriptor(descriptor);
    const QJsonArray vectorResults =
        vectorResult.value(QStringLiteral("vectorResults")).toArray();

    QJsonArray executions;
    int readyExecutionCount = 0;
    int blockedExecutionCount = 0;
    int vectorResultReadyCount = 0;
    int callableEntryPointCount = 0;
    int sanitizedExecutionCount = 0;
    int resultCapturePolicyCount = 0;
    int noMaterialExportCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QJsonObject vector = sequenceIndex < vectorResults.size()
            ? vectorResults.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool vectorAccepted =
            vectorResult.value(QStringLiteral("accepted")).toBool(false);
        const bool vectorReady =
            vector.value(QStringLiteral("knownAnswerVectorResultReady")).toBool(false);
        const bool callableEntryPoint =
            vector.value(QStringLiteral("providerSymbol")).toString()
                == productionOperationProviderSymbol(operation)
            && vector.value(QStringLiteral("providerAbiSignature")).toString()
                == productionOperationProviderAbiSignature(operation);
        const bool resultCapturePolicy =
            vector.value(QStringLiteral("resultContract")).toArray().contains(
                QStringLiteral("status-code-class"))
            && vector.value(QStringLiteral("resultContract")).toArray().contains(
                QStringLiteral("sanitized-error-class"))
            && vector.value(QStringLiteral("resultContract")).toArray().contains(
                QStringLiteral("material-export-proof"));
        const bool noSensitiveExport =
            !vector.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !vector.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !vector.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !vector.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !vector.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool sanitized = callableEntryPoint
            && resultCapturePolicy
            && noSensitiveExport;
        const bool executionReady = vectorAccepted
            && vectorReady
            && sanitized;

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
        execution[QStringLiteral("providerInvocationVectorResult")] = vector;
        execution[QStringLiteral("providerInvocationVectorResultReleaseGate")] =
            vectorResult.value(QStringLiteral("releaseGate")).toString();
        execution[QStringLiteral("providerInvocationVectorResultAccepted")] = vectorAccepted;
        execution[QStringLiteral("vectorResultReady")] = vectorReady;
        execution[QStringLiteral("callableEntryPointReady")] = callableEntryPoint;
        execution[QStringLiteral("resultCapturePolicyReady")] = resultCapturePolicy;
        execution[QStringLiteral("executionEntryPoint")] =
            QStringLiteral("qnc_e2e_provider_table_v1/%1")
                .arg(productionOperationProviderSymbol(operation));
        execution[QStringLiteral("executionMode")] = executionReady
            ? QStringLiteral("reviewed-provider-call")
            : QStringLiteral("blocked-non-executing-placeholder");
        execution[QStringLiteral("executionState")] = executionReady
            ? QStringLiteral("ready-for-reviewed-provider-call")
            : (descriptor.linked
                ? QStringLiteral("blocked-linked-placeholder")
                : QStringLiteral("blocked-not-linked"));
        execution[QStringLiteral("operationInvoked")] =
            vector.value(QStringLiteral("operationInvoked")).toBool(false);
        execution[QStringLiteral("inputBytesCaptured")] = false;
        execution[QStringLiteral("outputBytesCaptured")] = false;
        execution[QStringLiteral("resultCaptured")] =
            vector.value(QStringLiteral("resultCaptured")).toBool(false);
        execution[QStringLiteral("statusCodeClass")] = executionReady
            ? vector.value(QStringLiteral("statusCodeClass")).toString(
                QStringLiteral("qnc-e2e-status-ok-or-sanitized-error"))
            : QStringLiteral("not-invoked");
        execution[QStringLiteral("sanitizedErrorClass")] = executionReady
            ? vector.value(QStringLiteral("sanitizedErrorClass")).toString(
                QStringLiteral("ok-or-sanitized-provider-error"))
            : QStringLiteral("not-invoked");
        execution[QStringLiteral("blockedReason")] = executionReady
            ? QString()
            : vector.value(QStringLiteral("blockedReason")).toString(
                vectorResult.value(QStringLiteral("blockedReason")).toString());
        execution[QStringLiteral("operatorAction")] = executionReady
            ? QStringLiteral("none")
            : (descriptor.linked
                ? QStringLiteral("run-reviewed-provider-call-through-execution-entrypoint")
                : QStringLiteral("register-reviewed-provider-table-before-invocation-execution"));
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

        if (executionReady) {
            ++readyExecutionCount;
        } else {
            ++blockedExecutionCount;
        }
        if (vectorReady) {
            ++vectorResultReadyCount;
        }
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

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && vectorResult.value(QStringLiteral("accepted")).toBool(false)
        && readyExecutionCount == cryptoOperations().size()
        && blockedExecutionCount == 0
        && vectorResultReadyCount == cryptoOperations().size()
        && callableEntryPointCount == cryptoOperations().size()
        && sanitizedExecutionCount == cryptoOperations().size()
        && resultCapturePolicyCount == cryptoOperations().size()
        && noMaterialExportCount == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-invocation-execution-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("providerInvocationVectorResult")] = vectorResult;
    status[QStringLiteral("providerInvocationVectorResultReleaseGate")] =
        vectorResult.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("providerInvocationVectorResultAccepted")] =
        vectorResult.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredExecutionCount")] = cryptoOperations().size();
    status[QStringLiteral("readyExecutionCount")] = readyExecutionCount;
    status[QStringLiteral("blockedExecutionCount")] = blockedExecutionCount;
    status[QStringLiteral("vectorResultReadyCount")] = vectorResultReadyCount;
    status[QStringLiteral("callableEntryPointCount")] = callableEntryPointCount;
    status[QStringLiteral("sanitizedExecutionCount")] = sanitizedExecutionCount;
    status[QStringLiteral("resultCapturePolicyCount")] = resultCapturePolicyCount;
    status[QStringLiteral("materialExportProofCount")] = noMaterialExportCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-invocation-execution-ready")
        : (descriptor.linked
            ? QStringLiteral("production-provider-invocation-execution-blocked-placeholder")
            : QStringLiteral("production-provider-invocation-execution-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : vectorResult.value(QStringLiteral("blockedReason")).toString(
            descriptor.linked
                ? QStringLiteral("production-provider-invocation-execution-placeholder")
                : QStringLiteral("production-provider-table-not-registered"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("execute-reviewed-provider-calls-through-sandbox")
            : QStringLiteral("register-reviewed-provider-table-before-invocation-execution"));
    status[QStringLiteral("executions")] = executions;
    status[QStringLiteral("operationInvoked")] =
        vectorResult.value(QStringLiteral("operationInvoked")).toBool(false);
    status[QStringLiteral("inputBytesCaptured")] = false;
    status[QStringLiteral("outputBytesCaptured")] = false;
    status[QStringLiteral("resultCaptured")] =
        vectorResult.value(QStringLiteral("resultCaptured")).toBool(false);
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}

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
        candidate[QStringLiteral("backendId")] = descriptor.id;
        candidate[QStringLiteral("providerId")] = descriptor.providerId;
        candidate[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
        candidate[QStringLiteral("rawKeyExported")] = false;
        candidate[QStringLiteral("privateMaterialExported")] = false;
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
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
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
        handoff[QStringLiteral("backendId")] = descriptor.id;
        handoff[QStringLiteral("providerId")] = descriptor.providerId;
        handoff[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
        handoff[QStringLiteral("rawKeyExported")] = false;
        handoff[QStringLiteral("privateMaterialExported")] = false;
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
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
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
        stub[QStringLiteral("backendId")] = descriptor.id;
        stub[QStringLiteral("providerId")] = descriptor.providerId;
        stub[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
        stub[QStringLiteral("rawKeyExported")] = false;
        stub[QStringLiteral("privateMaterialExported")] = false;
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
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
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
        bridge[QStringLiteral("backendId")] = descriptor.id;
        bridge[QStringLiteral("providerId")] = descriptor.providerId;
        bridge[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
        bridge[QStringLiteral("rawKeyExported")] = false;
        bridge[QStringLiteral("privateMaterialExported")] = false;
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
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
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
        callableInterface[QStringLiteral("backendId")] = descriptor.id;
        callableInterface[QStringLiteral("providerId")] = descriptor.providerId;
        callableInterface[QStringLiteral("operationContractVersion")] =
            descriptor.operationContractVersion;
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
        callableInterface[QStringLiteral("rawKeyExported")] = false;
        callableInterface[QStringLiteral("privateMaterialExported")] = false;
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
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
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
        preflight[QStringLiteral("backendId")] = descriptor.id;
        preflight[QStringLiteral("providerId")] = descriptor.providerId;
        preflight[QStringLiteral("operationContractVersion")] =
            descriptor.operationContractVersion;
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
        preflight[QStringLiteral("rawKeyExported")] = false;
        preflight[QStringLiteral("privateMaterialExported")] = false;
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
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
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
        arming[QStringLiteral("backendId")] = descriptor.id;
        arming[QStringLiteral("providerId")] = descriptor.providerId;
        arming[QStringLiteral("operationContractVersion")] =
            descriptor.operationContractVersion;
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
        arming[QStringLiteral("rawKeyExported")] = false;
        arming[QStringLiteral("privateMaterialExported")] = false;
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
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
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
        acceptance[QStringLiteral("backendId")] = descriptor.id;
        acceptance[QStringLiteral("providerId")] = descriptor.providerId;
        acceptance[QStringLiteral("operationContractVersion")] =
            descriptor.operationContractVersion;
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
        acceptance[QStringLiteral("rawKeyExported")] = false;
        acceptance[QStringLiteral("privateMaterialExported")] = false;
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
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
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
        bridge[QStringLiteral("backendId")] = descriptor.id;
        bridge[QStringLiteral("providerId")] = descriptor.providerId;
        bridge[QStringLiteral("operationContractVersion")] =
            descriptor.operationContractVersion;
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
        bridge[QStringLiteral("rawKeyExported")] = false;
        bridge[QStringLiteral("privateMaterialExported")] = false;
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
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
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
        execution[QStringLiteral("backendId")] = descriptor.id;
        execution[QStringLiteral("providerId")] = descriptor.providerId;
        execution[QStringLiteral("operationContractVersion")] =
            descriptor.operationContractVersion;
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
        execution[QStringLiteral("rawKeyExported")] = false;
        execution[QStringLiteral("privateMaterialExported")] = false;
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
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
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
            input.suite_id = E2EAdvertisedSuite;
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

QJsonObject productionProviderTableBindingProbeStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject registration =
        productionProviderTableRegistrationStatusForDescriptor(descriptor);
    const QJsonObject tableValidation =
        registration.value(QStringLiteral("tableValidation")).toObject();
    QJsonArray enumMappings;
    int enumMatchCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperation operation : cryptoOperations()) {
        const QString operationName = cryptoOperationName(operation);
        const qnc_e2e_operation_t headerOperation =
            static_cast<qnc_e2e_operation_t>(sequenceIndex);
        const bool enumMatches = static_cast<int>(headerOperation) == sequenceIndex;
        QJsonObject mapping;
        mapping[QStringLiteral("sequenceIndex")] = sequenceIndex;
        mapping[QStringLiteral("operation")] = operationName;
        mapping[QStringLiteral("headerEnumValue")] = static_cast<int>(headerOperation);
        mapping[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
        mapping[QStringLiteral("enumMatchesOperationOrder")] = enumMatches;
        mapping[QStringLiteral("functionPointerSlot")] =
            QStringLiteral("qnc_e2e_provider_table_v1/%1").arg(operationName);
        mapping[QStringLiteral("bound")] =
            registration.value(QStringLiteral("accepted")).toBool(false);
        mapping[QStringLiteral("registrationSource")] =
            registration.value(QStringLiteral("registrationSource")).toString();
        mapping[QStringLiteral("blockedReason")] =
            registration.value(QStringLiteral("blockedReason")).toString();
        enumMappings.append(mapping);
        if (enumMatches) {
            ++enumMatchCount;
        }
        ++sequenceIndex;
    }

    QJsonArray fields;
    fields.append(providerTableFieldOffsetStatus(QStringLiteral("abi"),
                                                 offsetof(qnc_e2e_provider_table_v1, abi),
                                                 -1));
    fields.append(providerTableFieldOffsetStatus(QStringLiteral("provider_id"),
                                                 offsetof(qnc_e2e_provider_table_v1, provider_id),
                                                 offsetof(qnc_e2e_provider_table_v1, abi)));
    fields.append(providerTableFieldOffsetStatus(QStringLiteral("operation_count"),
                                                 offsetof(qnc_e2e_provider_table_v1, operation_count),
                                                 offsetof(qnc_e2e_provider_table_v1, provider_id)));
    fields.append(providerTableFieldOffsetStatus(QStringLiteral("session_key_generation"),
                                                 offsetof(qnc_e2e_provider_table_v1, session_key_generation),
                                                 offsetof(qnc_e2e_provider_table_v1, operation_count)));
    fields.append(providerTableFieldOffsetStatus(QStringLiteral("payload_decrypt"),
                                                 offsetof(qnc_e2e_provider_table_v1, payload_decrypt),
                                                 offsetof(qnc_e2e_provider_table_v1, session_key_generation)));

    const bool headerLayoutComplete =
        sizeof(qnc_e2e_operation_input_v1) > 0
        && sizeof(qnc_e2e_operation_output_v1) > 0
        && sizeof(qnc_e2e_provider_table_v1) >= sizeof(void*) * 10
        && offsetof(qnc_e2e_provider_table_v1, abi) == 0
        && offsetof(qnc_e2e_provider_table_v1, payload_decrypt)
            > offsetof(qnc_e2e_provider_table_v1, session_key_generation);
    const bool enumMappingComplete =
        enumMatchCount == QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT
        && cryptoOperations().size() == QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT;
    const bool functionPointerSlotsComplete =
        sizeof(qnc_e2e_provider_operation_v1) == sizeof(void*);
    const bool tableBound = QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_BOUND != 0;
    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.linked
        && tableBound
        && registration.value(QStringLiteral("accepted")).toBool(false)
        && tableValidation.value(QStringLiteral("accepted")).toBool(false)
        && headerLayoutComplete
        && enumMappingComplete
        && functionPointerSlotsComplete;

    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-provider-table-binding-probe-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("providerApiHeader")] =
        QString::fromLatin1(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_API_HEADER);
    status[QStringLiteral("tableAbi")] =
        QString::fromLatin1(QNC_E2E_PROVIDER_TABLE_ABI);
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("tableBound")] = tableBound;
    status[QStringLiteral("providerTableRegistration")] = registration;
    status[QStringLiteral("providerTableRegistered")] =
        registration.value(QStringLiteral("registered")).toBool(false);
    status[QStringLiteral("registrationReleaseGate")] =
        registration.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("registrationAccepted")] =
        registration.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("headerLayoutComplete")] = headerLayoutComplete;
    status[QStringLiteral("enumMappingComplete")] = enumMappingComplete;
    status[QStringLiteral("functionPointerSlotsComplete")] = functionPointerSlotsComplete;
    status[QStringLiteral("tableValidation")] = tableValidation;
    status[QStringLiteral("tableValidationAccepted")] =
        tableValidation.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("tableValidationBlockedReason")] =
        tableValidation.value(QStringLiteral("blockedReason")).toString();
    status[QStringLiteral("providerTableSizeBytes")] =
        static_cast<qint64>(sizeof(qnc_e2e_provider_table_v1));
    status[QStringLiteral("operationInputSizeBytes")] =
        static_cast<qint64>(sizeof(qnc_e2e_operation_input_v1));
    status[QStringLiteral("operationOutputSizeBytes")] =
        static_cast<qint64>(sizeof(qnc_e2e_operation_output_v1));
    status[QStringLiteral("providerOperationPointerSizeBytes")] =
        static_cast<qint64>(sizeof(qnc_e2e_provider_operation_v1));
    status[QStringLiteral("requiredOperationCount")] =
        QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT;
    status[QStringLiteral("enumMatchCount")] = enumMatchCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-table-binding-ready")
        : (descriptor.linked
            ? QStringLiteral("production-provider-table-binding-blocked-placeholder")
            : QStringLiteral("production-provider-table-binding-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : (descriptor.linked
            ? QStringLiteral("production-provider-table-placeholder")
            : QStringLiteral("production-provider-table-not-bound"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("bind-reviewed-provider-table-and-run-layout-probe")
            : QStringLiteral("link-reviewed-production-crypto-backend"));
    status[QStringLiteral("fieldOffsets")] = fields;
    status[QStringLiteral("enumMappings")] = enumMappings;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    return status;
}

QJsonObject productionOperationHarnessStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const bool isProduction = descriptor.id == QString::fromLatin1(ProductionBackendId);
    QJsonArray operationHarnesses;
    int runnableOperationCount = 0;
    int blockedOperationCount = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const bool operationRegistered = descriptor.operations.contains(spec.operation);
        const bool runnable = isProduction
            && descriptor.linked
            && spec.implemented
            && spec.knownAnswerPassed
            && (spec.roundTripPassed
                || (spec.operation != E2ECryptoOperation::PayloadEncrypt
                    && spec.operation != E2ECryptoOperation::PayloadDecrypt));
        QJsonObject op;
        op[QStringLiteral("operation")] = cryptoOperationName(spec.operation);
        op[QStringLiteral("registered")] = operationRegistered;
        op[QStringLiteral("runnable")] = runnable;
        op[QStringLiteral("implementationState")] = spec.implementationState;
        op[QStringLiteral("vectorSet")] = spec.vectorSet;
        op[QStringLiteral("fixtureHashSha256")] = productionHarnessFixtureHash(spec);
        op[QStringLiteral("knownAnswerPassed")] = spec.knownAnswerPassed;
        op[QStringLiteral("roundTripPassed")] = spec.roundTripPassed;
        op[QStringLiteral("compatibilityStatus")] = spec.compatibilityStatus;
        op[QStringLiteral("blockedReason")] = runnable ? QString() : spec.migrationBlocker;
        op[QStringLiteral("operatorAction")] = spec.operatorAction;
        op[QStringLiteral("rawKeyExported")] = false;
        op[QStringLiteral("privateMaterialExported")] = false;
        operationHarnesses.append(op);
        if (runnable) {
            ++runnableOperationCount;
        } else {
            ++blockedOperationCount;
        }
    }

    const bool accepted = isProduction
        && descriptor.linked
        && blockedOperationCount == 0
        && productionOperationSpecs().size() == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] = QStringLiteral("qtnetworkchat-e2e-production-operation-harness-v1");
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("harnessRunnable")] = accepted;
    status[QStringLiteral("requiredOperationCount")] = cryptoOperations().size();
    status[QStringLiteral("runnableOperationCount")] = runnableOperationCount;
    status[QStringLiteral("blockedOperationCount")] = blockedOperationCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-operation-harness-passed")
        : (descriptor.linked
            ? QStringLiteral("production-operation-harness-blocked-placeholder")
            : QStringLiteral("production-operation-harness-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : (descriptor.linked
            ? QStringLiteral("production-operations-not-implemented")
            : QStringLiteral("production-crypto-backend-unavailable"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("replace-placeholder-operations-and-pass-harness")
            : QStringLiteral("link-reviewed-production-crypto-backend"));
    status[QStringLiteral("operations")] = operationHarnesses;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    return status;
}

QJsonObject productionOperationExecutionPlanStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject harness = productionOperationHarnessStatusForDescriptor(descriptor);
    const QJsonArray harnessOperations = harness.value(QStringLiteral("operations")).toArray();
    QHash<QString, QJsonObject> harnessByOperation;
    for (const QJsonValue& value : harnessOperations) {
        const QJsonObject operation = value.toObject();
        harnessByOperation.insert(operation.value(QStringLiteral("operation")).toString(), operation);
    }

    QJsonArray steps;
    int sequenceIndex = 0;
    int runnableStepCount = 0;
    int blockedStepCount = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const QString operationName = cryptoOperationName(spec.operation);
        const QJsonObject harnessOperation = harnessByOperation.value(operationName);
        const bool harnessRunnable = harnessOperation.value(QStringLiteral("runnable")).toBool(false);
        QJsonObject step;
        step[QStringLiteral("sequenceIndex")] = sequenceIndex++;
        step[QStringLiteral("operation")] = operationName;
        step[QStringLiteral("entrypoint")] = descriptor.type + QStringLiteral("/") + operationName;
        step[QStringLiteral("providerId")] = descriptor.providerId;
        step[QStringLiteral("backendId")] = descriptor.id;
        step[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
        step[QStringLiteral("dispatchState")] = descriptor.dispatchState;
        step[QStringLiteral("requiresHarnessRunnable")] = true;
        step[QStringLiteral("harnessRunnable")] = harnessRunnable;
        step[QStringLiteral("runnable")] = harnessRunnable;
        step[QStringLiteral("registered")] = harnessOperation.value(QStringLiteral("registered")).toBool(false);
        step[QStringLiteral("fixtureHashSha256")] =
            harnessOperation.value(QStringLiteral("fixtureHashSha256")).toString();
        step[QStringLiteral("vectorSet")] = spec.vectorSet;
        step[QStringLiteral("implementationState")] = spec.implementationState;
        step[QStringLiteral("compatibilityStatus")] = spec.compatibilityStatus;
        step[QStringLiteral("knownAnswerPassed")] = spec.knownAnswerPassed;
        step[QStringLiteral("roundTripPassed")] = spec.roundTripPassed;
        step[QStringLiteral("invocationContract")] =
            productionOperationInvocationContract(descriptor, spec, harnessOperation);
        step[QStringLiteral("releaseGate")] = harnessRunnable
            ? QStringLiteral("production-operation-step-ready")
            : harness.value(QStringLiteral("releaseGate")).toString();
        step[QStringLiteral("blockedReason")] = harnessRunnable
            ? QString()
            : harnessOperation.value(QStringLiteral("blockedReason")).toString(spec.migrationBlocker);
        step[QStringLiteral("operatorAction")] = harnessRunnable
            ? QStringLiteral("none")
            : harnessOperation.value(QStringLiteral("operatorAction")).toString(spec.operatorAction);
        step[QStringLiteral("rawKeyExported")] = false;
        step[QStringLiteral("privateMaterialExported")] = false;
        steps.append(step);
        if (harnessRunnable) {
            ++runnableStepCount;
        } else {
            ++blockedStepCount;
        }
    }

    const bool accepted = harness.value(QStringLiteral("accepted")).toBool(false)
        && blockedStepCount == 0
        && productionOperationSpecs().size() == cryptoOperations().size();
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-operation-execution-plan-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("dispatchState")] = descriptor.dispatchState;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("planReady")] = accepted;
    status[QStringLiteral("requiresHarnessRunnable")] = true;
    status[QStringLiteral("requiredStepCount")] = cryptoOperations().size();
    status[QStringLiteral("runnableStepCount")] = runnableStepCount;
    status[QStringLiteral("blockedStepCount")] = blockedStepCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-operation-execution-plan-ready")
        : (descriptor.linked
            ? QStringLiteral("production-operation-execution-plan-blocked-placeholder")
            : QStringLiteral("production-operation-execution-plan-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : harness.value(QStringLiteral("blockedReason")).toString(
            descriptor.linked
                ? QStringLiteral("production-operations-not-implemented")
                : QStringLiteral("production-crypto-backend-unavailable"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("replace-placeholder-operations-and-run-execution-plan")
            : QStringLiteral("link-reviewed-production-crypto-backend"));
    status[QStringLiteral("operationHarnessReleaseGate")] =
        harness.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("operationHarnessAccepted")] =
        harness.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("operationHarness")] = harness;
    status[QStringLiteral("steps")] = steps;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    return status;
}

QJsonObject productionOperationInvocationStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const QJsonObject executionPlan = productionOperationExecutionPlanStatusForDescriptor(descriptor);
    const QJsonArray steps = executionPlan.value(QStringLiteral("steps")).toArray();
    QJsonArray invocations;
    int callableOperationCount = 0;
    int blockedOperationCount = 0;
    for (const QJsonValue& value : steps) {
        const QJsonObject invocation =
            value.toObject().value(QStringLiteral("invocationContract")).toObject();
        invocations.append(invocation);
        if (invocation.value(QStringLiteral("callable")).toBool(false)) {
            ++callableOperationCount;
        } else {
            ++blockedOperationCount;
        }
    }

    const bool accepted = executionPlan.value(QStringLiteral("accepted")).toBool(false)
        && blockedOperationCount == 0;
    QJsonObject status;
    status[QStringLiteral("schema")] =
        QStringLiteral("qtnetworkchat-e2e-production-operation-invocation-v1");
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
    status[QStringLiteral("dispatchState")] = descriptor.dispatchState;
    status[QStringLiteral("linked")] = descriptor.linked;
    status[QStringLiteral("productionReady")] = descriptor.productionReady;
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("callableOperationCount")] = callableOperationCount;
    status[QStringLiteral("blockedOperationCount")] = blockedOperationCount;
    status[QStringLiteral("requiredOperationCount")] = cryptoOperations().size();
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-operation-invocation-ready")
        : (descriptor.linked
            ? QStringLiteral("production-operation-invocation-blocked-placeholder")
            : QStringLiteral("production-operation-invocation-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : executionPlan.value(QStringLiteral("blockedReason")).toString(
            descriptor.linked
                ? QStringLiteral("production-operations-not-implemented")
                : QStringLiteral("production-crypto-backend-unavailable"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("replace-placeholder-operations-and-enable-invocation")
            : QStringLiteral("link-reviewed-production-crypto-backend"));
    status[QStringLiteral("executionPlanReleaseGate")] =
        executionPlan.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("executionPlanAccepted")] =
        executionPlan.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("invocations")] = invocations;
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
    return status;
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

E2ECryptoAdapterDescriptor cryptoAdapterForBackend(const QString& requested, bool* found = nullptr) {
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
    status[QStringLiteral("backendId")] = descriptor.id;
    status[QStringLiteral("providerId")] = descriptor.providerId;
    status[QStringLiteral("operationContractVersion")] = descriptor.operationContractVersion;
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
    status[QStringLiteral("rawKeyExported")] = false;
    status[QStringLiteral("privateMaterialExported")] = false;
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

[[maybe_unused]] QJsonObject currentCryptoOperationStatus(E2ECryptoOperation operation) {
    QString source;
    const QString requested = requestedBackendId(&source);
    return cryptoOperationStatus(operation, requested, e2eProductionCryptoRequired());
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


QByteArray randomBytes(qsizetype size) {
    QByteArray value;
    value.resize(size);
    for (qsizetype i = 0; i < size; ++i) {
        value[static_cast<int>(i)] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return value;
}

quint32 readDhValue(const QByteArray& value, const char* prefix) {
    const QByteArray marker(prefix);
    if (!value.startsWith(marker) || value.size() != marker.size() + 4) {
        return 0;
    }
    quint32 result = 0;
    for (int i = marker.size(); i < value.size(); ++i) {
        result = (result << 8) | static_cast<unsigned char>(value.at(i));
    }
    return result;
}

QByteArray writeDhValue(const char* prefix, quint32 value) {
    QByteArray result(prefix);
    result.append(static_cast<char>((value >> 24) & 0xff));
    result.append(static_cast<char>((value >> 16) & 0xff));
    result.append(static_cast<char>((value >> 8) & 0xff));
    result.append(static_cast<char>(value & 0xff));
    return result;
}

quint32 modMul(quint32 a, quint32 b) {
    return static_cast<quint32>((static_cast<quint64>(a) * static_cast<quint64>(b)) % DraftDhPrime);
}

quint32 modPow(quint32 base, quint32 exponent) {
    quint32 result = 1;
    quint32 factor = base % DraftDhPrime;
    quint32 power = exponent;
    while (power > 0) {
        if ((power & 1u) != 0u) {
            result = modMul(result, factor);
        }
        factor = modMul(factor, factor);
        power >>= 1;
    }
    return result;
}

QByteArray hmacSha256(const QByteArray& key, const QByteArray& data) {
    return QMessageAuthenticationCode::hash(data, key, QCryptographicHash::Sha256);
}

bool constantTimeEqual(const QByteArray& left, const QByteArray& right) {
    if (left.size() != right.size()) {
        return false;
    }

    volatile unsigned char difference = 0;
    for (qsizetype i = 0; i < left.size(); ++i) {
        difference |= static_cast<unsigned char>(left.at(i))
            ^ static_cast<unsigned char>(right.at(i));
    }
    return difference == 0;
}

QByteArray streamXor(const QByteArray& sessionKey,
                     const QByteArray& nonce,
                     const QString& aad,
                     const QByteArray& input) {
    QByteArray output;
    output.resize(input.size());
    qsizetype offset = 0;
    quint32 counter = 0;
    while (offset < input.size()) {
        QByteArray seed;
        seed.reserve(sessionKey.size() + nonce.size() + aad.toUtf8().size() + 8);
        seed.append(sessionKey);
        seed.append(nonce);
        seed.append(aad.toUtf8());
        seed.append(static_cast<char>((counter >> 24) & 0xff));
        seed.append(static_cast<char>((counter >> 16) & 0xff));
        seed.append(static_cast<char>((counter >> 8) & 0xff));
        seed.append(static_cast<char>(counter & 0xff));
        const QByteArray block = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
        for (qsizetype i = 0; i < block.size() && offset < input.size(); ++i, ++offset) {
            output[static_cast<int>(offset)] = static_cast<char>(input[static_cast<int>(offset)] ^ block[static_cast<int>(i)]);
        }
        ++counter;
    }
    return output;
}

QByteArray envelopeTagData(const E2EEnvelope& envelope) {
    QByteArray data;
    data.append(normalizedE2EProtocol(envelope.protocol).toUtf8());
    data.append('|');
    data.append(normalizedE2ESuite(envelope.suite).toUtf8());
    data.append('|');
    data.append(envelope.senderId.trimmed().toUtf8());
    data.append('|');
    data.append(envelope.receiverId.trimmed().toUtf8());
    data.append('|');
    data.append(envelope.keyId.trimmed().toUtf8());
    data.append('|');
    data.append(envelope.aad.toUtf8());
    data.append('|');
    data.append(envelope.nonce);
    data.append('|');
    data.append(envelope.ciphertext);
    return data;
}

QByteArray agreementSignatureData(const E2EKeyAgreement& agreement) {
    QByteArray data;
    data.append("qtnetworkchat-e2e-agreement-signature-v1|");
    data.append(normalizedE2EProtocol(agreement.protocol).toUtf8());
    data.append('|');
    data.append(normalizedE2ESuite(agreement.suite).toUtf8());
    data.append('|');
    data.append(agreement.senderId.trimmed().toUtf8());
    data.append('|');
    data.append(agreement.receiverId.trimmed().toUtf8());
    data.append('|');
    data.append(agreement.keyId.trimmed().toUtf8());
    data.append('|');
    data.append(e2eFingerprint(agreement.publicKey).toUtf8());
    data.append('|');
    data.append(agreement.senderIdentityFingerprint.trimmed().toLower().toUtf8());
    data.append('|');
    data.append(agreement.receiverIdentityFingerprint.trimmed().toLower().toUtf8());
    return data;
}

QByteArray agreementTranscriptData(const E2EKeyAgreement& left,
                                   const E2EKeyAgreement& right,
                                   const QByteArray& sharedSecret) {
    const E2EKeyAgreement* first = &left;
    const E2EKeyAgreement* second = &right;
    if (left.senderId > right.senderId
        || (left.senderId == right.senderId && left.keyId > right.keyId)) {
        first = &right;
        second = &left;
    }

    QByteArray data;
    data.append("qtnetworkchat-e2e-authenticated-draft-v1|");
    data.append(sharedSecret.toHex());
    data.append('|');
    for (const E2EKeyAgreement* agreement : {first, second}) {
        data.append(normalizedE2EProtocol(agreement->protocol).toUtf8());
        data.append('|');
        data.append(normalizedE2ESuite(agreement->suite).toUtf8());
        data.append('|');
        data.append(agreement->senderId.trimmed().toUtf8());
        data.append('|');
        data.append(agreement->receiverId.trimmed().toUtf8());
        data.append('|');
        data.append(agreement->keyId.trimmed().toUtf8());
        data.append('|');
        data.append(agreement->senderIdentityFingerprint.trimmed().toLower().toUtf8());
        data.append('|');
        data.append(agreement->receiverIdentityFingerprint.trimmed().toLower().toUtf8());
        data.append('|');
        data.append(e2eFingerprint(agreement->publicKey).toUtf8());
        data.append('|');
    }
    return data;
}

QByteArray productionSessionDerivePrimary(const E2EKeyAgreement& left,
                                          const E2EKeyAgreement& right) {
    QByteArray transcript =
        agreementTranscriptData(left,
                                right,
                                QByteArrayLiteral("production-provider-session-shared-v1"));
    const QByteArray draftDomain = QByteArrayLiteral("qtnetworkchat-e2e-authenticated-draft-v1|");
    if (transcript.startsWith(draftDomain)) {
        transcript.remove(0, draftDomain.size());
    }
    return QByteArray(ProductionSessionDerivePrimaryDomain) + transcript;
}

QByteArray productionSessionDeriveSecondary(const E2EKeyAgreement& left,
                                            const E2EKeyAgreement& right) {
    const E2EKeyAgreement* first = &left;
    const E2EKeyAgreement* second = &right;
    if (left.senderId > right.senderId
        || (left.senderId == right.senderId && left.keyId > right.keyId)) {
        first = &right;
        second = &left;
    }

    QByteArray data;
    data.append(ProductionSessionDeriveSecondaryDomain);
    for (const E2EKeyAgreement* agreement : {first, second}) {
        data.append(agreement->senderId.trimmed().toUtf8());
        data.append('|');
        data.append(agreement->receiverId.trimmed().toUtf8());
        data.append('|');
        data.append(agreement->keyId.trimmed().toUtf8());
        data.append('|');
        data.append(e2eFingerprint(agreement->publicKey).toUtf8());
        data.append('|');
        data.append(agreement->senderIdentityFingerprint.trimmed().toLower().toUtf8());
        data.append('|');
        data.append(agreement->receiverIdentityFingerprint.trimmed().toLower().toUtf8());
        data.append('|');
    }
    return data;
}

QByteArray productionSessionDeriveAad(const E2EKeyAgreement& left,
                                      const E2EKeyAgreement& right) {
    const E2EKeyAgreement* first = &left;
    const E2EKeyAgreement* second = &right;
    if (left.senderId > right.senderId
        || (left.senderId == right.senderId && left.keyId > right.keyId)) {
        first = &right;
        second = &left;
    }
    QByteArray data;
    data.append("qtnetworkchat-e2e-production-session-context-v1|");
    data.append(first->senderId.trimmed().toUtf8());
    data.append('|');
    data.append(second->senderId.trimmed().toUtf8());
    data.append('|');
    data.append(first->keyId.trimmed().toUtf8());
    data.append('|');
    data.append(second->keyId.trimmed().toUtf8());
    return data;
}

bool productionBackendSelected() {
    QString source;
    return requestedBackendId(&source) == QString::fromLatin1(ProductionBackendId);
}
}

QString normalizedE2EProtocol(QString protocol) {
    return protocol.trimmed().toLower();
}

QString normalizedE2ESuite(QString suite) {
    return suite.trimmed().toLower();
}

bool isSupportedE2EProtocol(const QString& protocol) {
    return normalizedE2EProtocol(protocol) == QString::fromLatin1(E2EProtocolV1);
}

bool isSupportedE2ESuite(const QString& suite) {
    const QString normalized = normalizedE2ESuite(suite);
    return normalized == QString::fromLatin1(E2EAdvertisedSuite)
        || normalized == QString::fromLatin1(E2EDraftSuite);
}

QString e2eDefaultSuite() {
    QString reason;
    if (productionBackendSelected() && productionProviderRuntimeReady(&reason)) {
        return QString::fromLatin1(E2EAdvertisedSuite);
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
        ? QString::fromLatin1(E2EAdvertisedSuite)
        : e2eDefaultSuite();
    status["wireCompatibleSuite"] = QString::fromLatin1(E2EAdvertisedSuite);
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

QString e2eFingerprint(const QByteArray& value) {
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}


QByteArray generateE2ESessionKey() {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::SessionKeyGeneration, nullptr)) {
        return QByteArray();
    }
    if (productionBackendSelected()) {
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::SessionKeyGeneration,
                                                QByteArray(),
                                                QByteArray(),
                                                QByteArray());
        if (providerResultOk(result)
            && result.materialPolicy == QNC_E2E_MATERIAL_HANDLE_ONLY
            && result.sealedOutput.size() == SessionKeyBytes) {
            return result.sealedOutput;
        }
        return QByteArray();
    }
    return randomBytes(SessionKeyBytes);
}

QByteArray generateE2EPrivateKey() {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::IdentityKeyGeneration, nullptr)) {
        return QByteArray();
    }
    if (productionBackendSelected()) {
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::IdentityKeyGeneration,
                                                QByteArray(),
                                                QByteArray(),
                                                QByteArray());
        if (providerResultOk(result)
            && result.publicOutput.size() == 32
            && result.sealedOutput.size() == 32) {
            return result.sealedOutput;
        }
        return QByteArray();
    }
    quint32 scalar = 0;
    while (scalar < 2 || scalar >= DraftDhPrime - 1) {
        const QByteArray bytes = randomBytes(4);
        scalar = 0;
        for (const char byte : bytes) {
            scalar = (scalar << 8) | static_cast<unsigned char>(byte);
        }
        scalar = 2 + (scalar % (DraftDhPrime - 3));
    }
    return writeDhValue(DraftDhPrivatePrefix, scalar);
}

QByteArray e2ePublicKeyFromPrivateKey(const QByteArray& privateKey) {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::PublicKeyDerivation, nullptr)) {
        return QByteArray();
    }
    if (productionBackendSelected()) {
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::PublicKeyDerivation,
                                                privateKey,
                                                QByteArray(),
                                                QByteArray());
        if (providerResultOk(result)
            && result.materialPolicy == QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED
            && result.publicOutput.size() == 32) {
            return result.publicOutput;
        }
        return QByteArray();
    }
    const quint32 scalar = readDhValue(privateKey, DraftDhPrivatePrefix);
    if (scalar < 2 || scalar >= DraftDhPrime - 1) {
        return QByteArray();
    }
    return writeDhValue(DraftDhPublicPrefix, modPow(DraftDhGenerator, scalar));
}

bool signE2EKeyAgreement(E2EKeyAgreement* agreement,
                         const QByteArray& identityPrivateKey,
                         QString* reason) {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::AgreementSign, reason)) {
        if (agreement) agreement->signature.clear();
        return false;
    }
    if (!agreement) {
        return fail(reason, QStringLiteral("invalid-agreement"));
    }
    agreement->signature.clear();
    QString validationReason;
    if (!agreement->isValid(&validationReason)) {
        return fail(reason, validationReason);
    }
    const QByteArray identityPublicKey = e2ePublicKeyFromPrivateKey(identityPrivateKey);
    if (identityPublicKey.isEmpty()
        || agreement->senderIdentityFingerprint.trimmed().toLower() != e2eFingerprint(identityPublicKey)) {
        return fail(reason, QStringLiteral("identity-key-mismatch"));
    }
    if (productionBackendSelected()) {
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::AgreementSign,
                                                identityPrivateKey,
                                                agreementSignatureData(*agreement),
                                                QByteArray());
        if (!providerResultOk(result)
            || result.materialPolicy != QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED
            || result.publicOutput.isEmpty()
            || result.publicOutput.size() > MaxSignatureBytes) {
            return fail(reason, result.reason.isEmpty()
                ? QStringLiteral("signature-generation-failed")
                : result.reason);
        }
        agreement->signature = result.publicOutput;
        if (reason) {
            reason->clear();
        }
        return true;
    }
    agreement->signature = hmacSha256(identityPublicKey, agreementSignatureData(*agreement));
    if (reason) {
        reason->clear();
    }
    return true;
}

bool verifyE2EKeyAgreementSignature(const E2EKeyAgreement& agreement,
                                    const QByteArray& identityPublicKey,
                                    QString* reason) {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::AgreementVerify, reason)) {
        return false;
    }
    QString validationReason;
    if (!agreement.isValid(&validationReason)) {
        return fail(reason, validationReason);
    }
    if (identityPublicKey.isEmpty()
        || agreement.senderIdentityFingerprint.trimmed().toLower() != e2eFingerprint(identityPublicKey)) {
        return fail(reason, QStringLiteral("identity-key-mismatch"));
    }
    if (agreement.signature.isEmpty()) {
        return fail(reason, QStringLiteral("missing-signature"));
    }
    if (productionBackendSelected()) {
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::AgreementVerify,
                                                identityPublicKey,
                                                agreementSignatureData(agreement),
                                                agreement.signature);
        if (!providerResultOk(result)
            || result.materialPolicy != QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED) {
            return fail(reason, result.reason.isEmpty()
                ? QStringLiteral("signature-mismatch")
                : result.reason);
        }
        if (reason) {
            reason->clear();
        }
        return true;
    }
    const QByteArray expected = hmacSha256(identityPublicKey, agreementSignatureData(agreement));
    if (!constantTimeEqual(expected, agreement.signature)) {
        return fail(reason, QStringLiteral("signature-mismatch"));
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

QByteArray deriveE2EAuthenticatedSessionKey(const QByteArray& localPrivateKey,
                                            const E2EKeyAgreement& localAgreement,
                                            const E2EKeyAgreement& remoteAgreement,
                                            QString* reason) {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::SessionDerive, reason)) {
        return QByteArray();
    }
    QString validationReason;
    if (!localAgreement.isValid(&validationReason) || !remoteAgreement.isValid(&validationReason)) {
        fail(reason, validationReason);
        return QByteArray();
    }
    if (localAgreement.senderId.trimmed() != remoteAgreement.receiverId.trimmed()
        || localAgreement.receiverId.trimmed() != remoteAgreement.senderId.trimmed()) {
        fail(reason, QStringLiteral("transcript-peer-mismatch"));
        return QByteArray();
    }
    if (localAgreement.senderIdentityFingerprint.trimmed().toLower()
            != remoteAgreement.receiverIdentityFingerprint.trimmed().toLower()
        || localAgreement.receiverIdentityFingerprint.trimmed().toLower()
            != remoteAgreement.senderIdentityFingerprint.trimmed().toLower()) {
        fail(reason, QStringLiteral("transcript-identity-mismatch"));
        return QByteArray();
    }
    if (productionBackendSelected()) {
        const QByteArray localPublic = e2ePublicKeyFromPrivateKey(localPrivateKey);
        if (localPublic.isEmpty() || localPublic != localAgreement.publicKey) {
            fail(reason, QStringLiteral("invalid-local-key"));
            return QByteArray();
        }
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::SessionDerive,
                                                productionSessionDerivePrimary(localAgreement, remoteAgreement),
                                                productionSessionDeriveSecondary(localAgreement, remoteAgreement),
                                                productionSessionDeriveAad(localAgreement, remoteAgreement));
        if (!providerResultOk(result)
            || result.materialPolicy != QNC_E2E_MATERIAL_HANDLE_ONLY
            || result.sealedOutput.size() != SessionKeyBytes) {
            fail(reason, result.reason.isEmpty()
                ? QStringLiteral("session-derivation-failed")
                : result.reason);
            return QByteArray();
        }
        if (reason) {
            reason->clear();
        }
        return result.sealedOutput;
    }
    const quint32 privateScalar = readDhValue(localPrivateKey, DraftDhPrivatePrefix);
    const quint32 localPublic = readDhValue(localAgreement.publicKey, DraftDhPublicPrefix);
    const quint32 expectedLocalPublic = privateScalar < 2 ? 0 : modPow(DraftDhGenerator, privateScalar);
    if (privateScalar < 2 || privateScalar >= DraftDhPrime - 1 || localPublic != expectedLocalPublic) {
        fail(reason, QStringLiteral("invalid-local-key"));
        return QByteArray();
    }
    const quint32 remotePublic = readDhValue(remoteAgreement.publicKey, DraftDhPublicPrefix);
    if (remotePublic < 2 || remotePublic >= DraftDhPrime) {
        fail(reason, QStringLiteral("invalid-remote-public-key"));
        return QByteArray();
    }

    const QByteArray sharedSecret = writeDhValue("qnc-dh1-shared:", modPow(remotePublic, privateScalar));
    const QByteArray transcript = agreementTranscriptData(localAgreement, remoteAgreement, sharedSecret);
    const QByteArray sessionKey = hmacSha256(QByteArray("qtnetworkchat-e2e-session-v1"), transcript);
    if (sessionKey.size() < SessionKeyBytes) {
        fail(reason, QStringLiteral("session-derivation-failed"));
        return QByteArray();
    }
    if (reason) {
        reason->clear();
    }
    return sessionKey.left(SessionKeyBytes);
}

E2EEnvelope encryptE2EText(const QString& senderId,
                           const QString& receiverId,
                           const QString& keyId,
                           const QByteArray& sessionKey,
                           const QString& plaintext,
                           QString* reason) {
    return encryptE2EPayload(senderId,
                             receiverId,
                             keyId,
                             sessionKey,
                             plaintext.toUtf8(),
                             QStringLiteral("text/private/v1"),
                             reason);
}

bool decryptE2EText(const E2EEnvelope& envelope,
                    const QByteArray& sessionKey,
                    QString* plaintext,
                    QString* reason) {
    if (plaintext) {
        plaintext->clear();
    }
    QByteArray plainBytes;
    if (!decryptE2EPayload(envelope, sessionKey, &plainBytes, reason)) {
        return false;
    }
    const QString decoded = QString::fromUtf8(plainBytes);
    if (decoded.toUtf8() != plainBytes) {
        return fail(reason, QStringLiteral("invalid-plaintext"));
    }
    if (plaintext) {
        *plaintext = decoded;
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

E2EEnvelope encryptE2EPayload(const QString& senderId,
                              const QString& receiverId,
                              const QString& keyId,
                              const QByteArray& sessionKey,
                              const QByteArray& plaintext,
                              const QString& aad,
                              QString* reason) {
    E2EEnvelope envelope;
    envelope.protocol = QString::fromLatin1(E2EProtocolV1);
    envelope.suite = e2eDefaultSuite();
    envelope.senderId = trimmed(senderId);
    envelope.receiverId = trimmed(receiverId);
    envelope.keyId = trimmed(keyId);
    envelope.nonce = randomBytes(MinNonceBytes);
    envelope.aad = aad.trimmed().isEmpty() ? QStringLiteral("payload/private/v1") : aad.trimmed();

    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::PayloadEncrypt, reason)) {
        return envelope;
    }
    if (sessionKey.size() < MinSessionKeyBytes) {
        fail(reason, QStringLiteral("invalid-session-key"));
        return envelope;
    }
    if (plaintext.isEmpty()) {
        fail(reason, QStringLiteral("empty-plaintext"));
        return envelope;
    }

    if (productionBackendSelected()) {
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::PayloadEncrypt,
                                                sessionKey,
                                                plaintext,
                                                envelope.aad.toUtf8());
        if (!providerResultOk(result)
            || result.materialPolicy != QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED
            || result.sealedOutput.size() <= MinNonceBytes + MinTagBytes) {
            fail(reason, result.reason.isEmpty()
                ? QStringLiteral("payload-encrypt-failed")
                : result.reason);
            return E2EEnvelope();
        }
        envelope.nonce = result.sealedOutput.left(MinNonceBytes);
        envelope.tag = result.sealedOutput.mid(MinNonceBytes, MinTagBytes);
        envelope.ciphertext = result.sealedOutput.mid(MinNonceBytes + MinTagBytes);
        if (!envelope.isValid(reason)) {
            return E2EEnvelope();
        }
        if (reason) {
            reason->clear();
        }
        return envelope;
    }
    envelope.ciphertext = streamXor(sessionKey, envelope.nonce, envelope.aad, plaintext);
    envelope.tag = hmacSha256(sessionKey, envelopeTagData(envelope));
    if (!envelope.isValid(reason)) {
        return E2EEnvelope();
    }
    if (reason) {
        reason->clear();
    }
    return envelope;
}

bool decryptE2EPayload(const E2EEnvelope& envelope,
                       const QByteArray& sessionKey,
                       QByteArray* plaintext,
                       QString* reason) {
    if (plaintext) {
        plaintext->clear();
    }
    if (sessionKey.size() < MinSessionKeyBytes) {
        return fail(reason, QStringLiteral("invalid-session-key"));
    }
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::PayloadDecrypt, reason)) {
        return false;
    }
    QString validationReason;
    if (!envelope.isValid(&validationReason)) {
        return fail(reason, validationReason);
    }
    if (productionBackendSelected()) {
        const QByteArray sealed = envelope.nonce + envelope.tag + envelope.ciphertext;
        const ProviderDispatchResult result =
            dispatchProductionProviderOperation(E2ECryptoOperation::PayloadDecrypt,
                                                sessionKey,
                                                sealed,
                                                envelope.aad.toUtf8());
        if (!providerResultOk(result)
            || result.materialPolicy != QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED) {
            return fail(reason, result.reason.isEmpty()
                ? QStringLiteral("authentication-failed")
                : result.reason);
        }
        if (plaintext) {
            *plaintext = result.publicOutput;
        }
        if (reason) {
            reason->clear();
        }
        return true;
    }
    const QByteArray expectedTag = hmacSha256(sessionKey, envelopeTagData(envelope));
    if (!constantTimeEqual(expectedTag, envelope.tag)) {
        return fail(reason, QStringLiteral("authentication-failed"));
    }
    const QByteArray plainBytes = streamXor(sessionKey, envelope.nonce, envelope.aad, envelope.ciphertext);
    if (plaintext) {
        *plaintext = plainBytes;
    }
    if (reason) {
        reason->clear();
    }
    return true;
}
