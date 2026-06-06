#include "e2eenvelope.h"
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
constexpr qsizetype MaxKeyIdLength = 128;
constexpr qsizetype MaxAadLength = 512;
constexpr qsizetype MinNonceBytes = 12;
constexpr qsizetype MaxNonceBytes = 24;
constexpr qsizetype MinTagBytes = 16;
constexpr qsizetype MaxTagBytes = 32;
constexpr qsizetype MaxPublicKeyBytes = 4096;
constexpr qsizetype FingerprintHexLength = 64;
constexpr qsizetype MaxSignatureBytes = 4096;
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
const char DraftBackendId[] = "draft-qt-hmac-stream-v1";
const char ProductionBackendId[] = QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_ID;

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

const qnc_e2e_provider_table_v1* g_registeredProductionProviderTable = nullptr;

bool envEnabled(const char* name) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

QString trimmed(QString value) {
    return value.trimmed();
}

QByteArray base64Field(const QJsonObject& obj, const char* name) {
    return QByteArray::fromBase64(obj.value(QString::fromLatin1(name)).toString().toLatin1(),
                                  QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QString toBase64Url(const QByteArray& value) {
    return QString::fromLatin1(value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool fail(QString* reason, const QString& value) {
    if (reason) {
        *reason = value;
    }
    return false;
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
        && descriptor.productionReady
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
            && descriptor.productionReady
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
        && descriptor.productionReady
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
            && descriptor.productionReady
            && reviewed
            && symbolMatches
            && signatureMatches
            && fixtureMatches
            && spec.knownAnswerPassed
            && spec.roundTripPassed;

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
        && descriptor.productionReady
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

    QJsonArray results;
    int passedResultCount = 0;
    int blockedResultCount = 0;
    int sanitizedResultCount = 0;
    int outputContractMismatchCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QString operationName = cryptoOperationName(operation);
        const QJsonObject callable = sequenceIndex < callableItems.size()
            ? callableItems.at(sequenceIndex).toObject()
            : QJsonObject();
        const bool callableReady = callable.value(QStringLiteral("callable")).toBool(false);
        const bool reviewed = callable.value(QStringLiteral("reviewed")).toBool(false);
        const bool knownAnswerPassed = callable.value(QStringLiteral("knownAnswerPassed")).toBool(false);
        const bool roundTripPassed = callable.value(QStringLiteral("roundTripPassed")).toBool(false);
        const bool outputContractMatched = callable.value(QStringLiteral("outputContract")).toArray().size()
            == productionOperationOutputContract(operation).size();
        const bool sanitized = !callable.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !callable.value(QStringLiteral("privateMaterialExported")).toBool(true);
        const bool passed = callableReady
            && reviewed
            && knownAnswerPassed
            && roundTripPassed
            && outputContractMatched
            && sanitized
            && descriptor.productionReady;

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
        result[QStringLiteral("callable")] = callableReady;
        result[QStringLiteral("reviewed")] = reviewed;
        result[QStringLiteral("knownAnswerPassed")] = knownAnswerPassed;
        result[QStringLiteral("roundTripPassed")] = roundTripPassed;
        result[QStringLiteral("outputContractMatched")] = outputContractMatched;
        result[QStringLiteral("sanitized")] = sanitized;
        result[QStringLiteral("passed")] = passed;
        result[QStringLiteral("resultState")] = passed
            ? QStringLiteral("passed-reviewed-production-result")
            : (descriptor.linked
                ? QStringLiteral("blocked-linked-placeholder")
                : QStringLiteral("blocked-not-linked"));
        result[QStringLiteral("errorClass")] = passed
            ? QString()
            : (descriptor.linked
                ? QStringLiteral("production-result-placeholder-not-executed")
                : QStringLiteral("production-result-adapter-not-linked"));
        result[QStringLiteral("blockedReason")] = passed
            ? QString()
            : (descriptor.linked
                ? QStringLiteral("production-operation-result-placeholder")
                : QStringLiteral("production-crypto-backend-unavailable"));
        result[QStringLiteral("operatorAction")] = passed
            ? QStringLiteral("none")
            : (descriptor.linked
                ? QStringLiteral("execute-reviewed-provider-operation-and-record-sanitized-result")
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
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.productionReady
        && callableManifest.value(QStringLiteral("accepted")).toBool(false)
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
    status[QStringLiteral("accepted")] = accepted;
    status[QStringLiteral("requiredResultCount")] = cryptoOperations().size();
    status[QStringLiteral("passedResultCount")] = passedResultCount;
    status[QStringLiteral("blockedResultCount")] = blockedResultCount;
    status[QStringLiteral("sanitizedResultCount")] = sanitizedResultCount;
    status[QStringLiteral("outputContractMismatchCount")] = outputContractMismatchCount;
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-operation-execution-results-ready")
        : (descriptor.linked
            ? QStringLiteral("production-operation-execution-results-blocked-placeholder")
            : QStringLiteral("production-operation-execution-results-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : (descriptor.linked
            ? QStringLiteral("production-operation-results-not-executed")
            : QStringLiteral("production-crypto-backend-unavailable"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("run-reviewed-production-operations-and-store-sanitized-results")
            : QStringLiteral("link-reviewed-production-crypto-backend"));
    status[QStringLiteral("operationCallableManifestReleaseGate")] =
        callableManifest.value(QStringLiteral("releaseGate")).toString();
    status[QStringLiteral("operationCallableManifestAccepted")] =
        callableManifest.value(QStringLiteral("accepted")).toBool(false);
    status[QStringLiteral("results")] = results;
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
        const bool symbolBound = descriptor.productionReady
            && descriptor.linked
            && QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_BOUND != 0
            && callable.value(QStringLiteral("callable")).toBool(false)
            && callable.value(QStringLiteral("reviewed")).toBool(false)
            && callable.value(QStringLiteral("providerSymbol")).toString() == expectedSymbol;
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
    const bool tableBound = QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_BOUND != 0;
    const QJsonObject bindingProbe = productionProviderTableBindingProbeStatusForDescriptor(descriptor);
    const bool bindingProbeAccepted =
        bindingProbe.value(QStringLiteral("accepted")).toBool(false);
    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.productionReady
        && descriptor.linked
        && tableBound
        && registration.value(QStringLiteral("accepted")).toBool(false)
        && abiMatchesHeader
        && operationCountMatchesHeader
        && bindingProbeAccepted
        && callableManifest.value(QStringLiteral("accepted")).toBool(false)
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
            ? QStringLiteral("production-provider-table-placeholder")
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
    const qnc_e2e_provider_table_v1* registeredTable = g_registeredProductionProviderTable;
    const QJsonObject tableValidation = providerTableValidationStatus(registeredTable);
    const bool registered = registeredTable != nullptr;
    const bool validationAccepted =
        tableValidation.value(QStringLiteral("accepted")).toBool(false);
    const bool compileTimeBound = QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_BOUND != 0;
    const bool linked = descriptor.linked;
    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.productionReady
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
        ? QStringLiteral("runtime-provider-table-registration")
        : (linked
            ? QStringLiteral("linked-placeholder-without-runtime-table")
            : QStringLiteral("not-linked"));
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
            ? QStringLiteral("production-provider-table-registration-blocked-not-production-ready")
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
                    : (!descriptor.productionReady
                        ? QStringLiteral("production-provider-table-registered-but-provider-not-ready")
                        : QStringLiteral("production-provider-table-registration-not-accepted")))));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (!registered
            ? (linked
                ? QStringLiteral("register-reviewed-provider-table-before-production-ready")
                : QStringLiteral("link-reviewed-production-crypto-backend"))
            : (!validationAccepted
                ? QStringLiteral("register-provider-table-with-complete-reviewed-operation-pointers")
                : QStringLiteral("enable-reviewed-provider-table-binding-and-production-readiness")));
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

QJsonObject productionProviderOperationPreflightStatusForDescriptor(const E2ECryptoAdapterDescriptor& descriptor) {
    const qnc_e2e_provider_table_v1* registeredTable = g_registeredProductionProviderTable;
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
            && compileTimeBound
            && descriptor.productionReady;

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
        && descriptor.productionReady
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
            ? QStringLiteral("production-provider-operation-preflight-blocked-not-production-ready")
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
        const bool frameReady = descriptor.productionReady
            && preflight.value(QStringLiteral("accepted")).toBool(false)
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
        && descriptor.productionReady
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
            && callFrame.value(QStringLiteral("accepted")).toBool(false)
            && descriptor.productionReady;
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
        && descriptor.productionReady
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

    QJsonArray results;
    int captureReadyCount = 0;
    int blockedResultCount = 0;
    int sanitizedResultCount = 0;
    int outputContractProofCount = 0;
    int fixtureProofCount = 0;
    int materialExportProofCount = 0;
    int invokedOperationCount = 0;
    int sequenceIndex = 0;
    for (const E2ECryptoOperationSpec& spec : productionOperationSpecs()) {
        const E2ECryptoOperation operation = spec.operation;
        const QString operationName = cryptoOperationName(operation);
        const QJsonObject invocation = sequenceIndex < dryRunInvocations.size()
            ? dryRunInvocations.at(sequenceIndex).toObject()
            : QJsonObject();
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
        const bool sanitized = outputContractMatches && fixtureMatches && materialExportProof;
        const bool captureReady = descriptor.productionReady
            && dryRun.value(QStringLiteral("accepted")).toBool(false)
            && dryRunReady
            && wouldInvoke
            && sanitized;

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
        result[QStringLiteral("statusCodeClass")] = captureReady
            ? QStringLiteral("qnc-e2e-status-ok-or-sanitized-error")
            : QStringLiteral("not-invoked");
        result[QStringLiteral("expectedErrorClass")] = captureReady
            ? QStringLiteral("sanitized-provider-error")
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
        result[QStringLiteral("operationInvoked")] = false;
        result[QStringLiteral("resultCaptured")] = false;
        result[QStringLiteral("sanitized")] = sanitized;
        result[QStringLiteral("outputContractMatched")] = outputContractMatches;
        result[QStringLiteral("fixtureHashMatched")] = fixtureMatches;
        result[QStringLiteral("blockedReason")] = captureReady
            ? QString()
            : invocation.value(QStringLiteral("blockedReason")).toString(
                dryRun.value(QStringLiteral("blockedReason")).toString());
        result[QStringLiteral("operatorAction")] = captureReady
            ? QStringLiteral("execute-reviewed-provider-and-record-sanitized-result")
            : invocation.value(QStringLiteral("operatorAction")).toString(
                dryRun.value(QStringLiteral("operatorAction")).toString());
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
        ++sequenceIndex;
    }

    const bool accepted = descriptor.id == QString::fromLatin1(ProductionBackendId)
        && descriptor.productionReady
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
    status[QStringLiteral("releaseGate")] = accepted
        ? QStringLiteral("production-provider-invocation-results-ready")
        : (descriptor.linked
            ? QStringLiteral("production-provider-invocation-results-blocked-placeholder")
            : QStringLiteral("production-provider-invocation-results-blocked-not-linked"));
    status[QStringLiteral("blockedReason")] = accepted
        ? QString()
        : dryRun.value(QStringLiteral("blockedReason")).toString(
            descriptor.linked
                ? QStringLiteral("production-provider-invocation-results-placeholder")
                : QStringLiteral("production-provider-table-not-registered"));
    status[QStringLiteral("operatorAction")] = accepted
        ? QStringLiteral("none")
        : (descriptor.linked
            ? QStringLiteral("capture-reviewed-provider-invocation-results")
            : QStringLiteral("register-reviewed-provider-table-before-result-capture"));
    status[QStringLiteral("results")] = results;
    status[QStringLiteral("operationInvoked")] = false;
    status[QStringLiteral("resultCaptured")] = false;
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
        const bool executionAllowed = descriptor.productionReady
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
        && descriptor.productionReady
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
        const bool harnessArmed = descriptor.productionReady
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
        && descriptor.productionReady
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
        const bool harnessAccepted =
            callbackHarness.value(QStringLiteral("accepted")).toBool(false);
        const bool callbackArmed =
            callback.value(QStringLiteral("callbackHarnessArmed")).toBool(false);
        const bool noSensitiveExport =
            !callback.value(QStringLiteral("rawKeyExported")).toBool(true)
            && !callback.value(QStringLiteral("privateMaterialExported")).toBool(true)
            && !callback.value(QStringLiteral("sessionSecretExported")).toBool(true)
            && !callback.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
            && !callback.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true);
        const bool knownAnswerReady = descriptor.productionReady
            && harnessAccepted
            && callbackArmed
            && noSensitiveExport;
        const bool roundTripReady = knownAnswerReady
            && (operation == E2ECryptoOperation::PayloadEncrypt
                || operation == E2ECryptoOperation::PayloadDecrypt
                || operation == E2ECryptoOperation::SessionDerive
                || operation == E2ECryptoOperation::AgreementVerify);
        const bool vectorPassed = knownAnswerReady
            && noSensitiveExport
            && callback.value(QStringLiteral("providerSymbol")).toString()
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
        test[QStringLiteral("providerCallbackHarnessReleaseGate")] =
            callbackHarness.value(QStringLiteral("releaseGate")).toString();
        test[QStringLiteral("providerCallbackHarnessAccepted")] = harnessAccepted;
        test[QStringLiteral("callbackHarnessArmed")] = callbackArmed;
        test[QStringLiteral("knownAnswerVectorReady")] = knownAnswerReady;
        test[QStringLiteral("roundTripVectorReady")] = roundTripReady;
        test[QStringLiteral("knownAnswerPassed")] = false;
        test[QStringLiteral("roundTripPassed")] = false;
        test[QStringLiteral("vectorPassed")] = vectorPassed;
        test[QStringLiteral("vectorExecutionState")] = vectorPassed
            ? QStringLiteral("passed-reviewed-provider-vector")
            : (descriptor.linked
                ? QStringLiteral("blocked-linked-placeholder")
                : QStringLiteral("blocked-not-linked"));
        test[QStringLiteral("operationInvoked")] = false;
        test[QStringLiteral("inputBytesCaptured")] = false;
        test[QStringLiteral("outputBytesCaptured")] = false;
        test[QStringLiteral("resultCaptured")] = false;
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
        && descriptor.productionReady
        && callbackHarness.value(QStringLiteral("accepted")).toBool(false)
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
            ? QStringLiteral("replace-placeholder-with-reviewed-provider-vector-tests")
            : QStringLiteral("register-reviewed-provider-table-before-vector-self-test"));
    status[QStringLiteral("tests")] = tests;
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
        const bool reviewed = descriptor.productionReady
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
        && descriptor.productionReady
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
    const qnc_e2e_provider_table_v1* registeredTable = g_registeredProductionProviderTable;
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
        const bool mapped = descriptor.productionReady
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
        && descriptor.productionReady
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
        const bool sandboxReady = descriptor.productionReady
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
        && descriptor.productionReady
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
        const bool sandboxAccepted =
            invocationSandbox.value(QStringLiteral("accepted")).toBool(false);
        const bool sandboxReady =
            sandbox.value(QStringLiteral("sandboxReady")).toBool(false);
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
            && noSensitiveExport;
        const bool vectorReady = descriptor.productionReady
            && sandboxAccepted
            && sandboxReady
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
        result[QStringLiteral("providerInvocationSandboxReleaseGate")] =
            invocationSandbox.value(QStringLiteral("releaseGate")).toString();
        result[QStringLiteral("providerInvocationSandboxAccepted")] = sandboxAccepted;
        result[QStringLiteral("sandboxReady")] = sandboxReady;
        result[QStringLiteral("fixtureHashMatched")] = fixtureMatched;
        result[QStringLiteral("resultContractReady")] = resultContractReady;
        result[QStringLiteral("statusCodeClass")] = vectorReady
            ? QStringLiteral("qnc-e2e-status-ok-or-sanitized-error")
            : QStringLiteral("not-invoked");
        result[QStringLiteral("sanitizedErrorClass")] = vectorReady
            ? QStringLiteral("ok-or-sanitized-provider-error")
            : QStringLiteral("not-invoked");
        result[QStringLiteral("knownAnswerVectorResultReady")] = vectorReady;
        result[QStringLiteral("roundTripVectorResultReady")] = vectorReady
            && (operation == E2ECryptoOperation::PayloadEncrypt
                || operation == E2ECryptoOperation::PayloadDecrypt
                || operation == E2ECryptoOperation::SessionDerive
                || operation == E2ECryptoOperation::AgreementVerify);
        result[QStringLiteral("knownAnswerPassed")] = false;
        result[QStringLiteral("roundTripPassed")] = false;
        result[QStringLiteral("vectorResultState")] = vectorReady
            ? QStringLiteral("ready-for-reviewed-provider-vector-result")
            : (descriptor.linked
                ? QStringLiteral("blocked-linked-placeholder")
                : QStringLiteral("blocked-not-linked"));
        result[QStringLiteral("operationInvoked")] = false;
        result[QStringLiteral("inputBytesCaptured")] = false;
        result[QStringLiteral("outputBytesCaptured")] = false;
        result[QStringLiteral("resultCaptured")] = false;
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
        && descriptor.productionReady
        && invocationSandbox.value(QStringLiteral("accepted")).toBool(false)
        && readyVectorResultCount == cryptoOperations().size()
        && blockedVectorResultCount == 0
        && sandboxReadyCount == cryptoOperations().size()
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
        const bool executionReady = descriptor.productionReady
            && vectorAccepted
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
        execution[QStringLiteral("operationInvoked")] = false;
        execution[QStringLiteral("inputBytesCaptured")] = false;
        execution[QStringLiteral("outputBytesCaptured")] = false;
        execution[QStringLiteral("resultCaptured")] = false;
        execution[QStringLiteral("statusCodeClass")] = executionReady
            ? QStringLiteral("qnc-e2e-status-ok-or-sanitized-error")
            : QStringLiteral("not-invoked");
        execution[QStringLiteral("sanitizedErrorClass")] = executionReady
            ? QStringLiteral("ok-or-sanitized-provider-error")
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
        && descriptor.productionReady
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
        && descriptor.productionReady
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
            && descriptor.productionReady
            && spec.implemented
            && spec.knownAnswerPassed
            && spec.roundTripPassed;
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
        && descriptor.productionReady
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
    descriptor.id = QString::fromLatin1(ProductionBackendId);
    descriptor.type = QStringLiteral("production-adapter");
    descriptor.implementation = QStringLiteral("production-adapter");
    descriptor.providerId = QStringLiteral("openssl-reviewed-provider-v1");
    descriptor.operationContractVersion = QStringLiteral("qtnetworkchat-e2e-crypto-ops-v1");
    descriptor.dispatchState = QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED != 0
        ? QStringLiteral("linked-placeholder-not-ready")
        : QStringLiteral("not-linked");
    descriptor.selfTestStatus = QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED != 0
        ? QStringLiteral("self-test-blocked-placeholder")
        : QStringLiteral("self-test-blocked-not-linked");
    descriptor.readinessGate = QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED != 0
        ? QStringLiteral("production-operations-not-implemented")
        : QStringLiteral("production-adapter-not-linked");
    descriptor.compatibilityStatus = QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED != 0
        ? QStringLiteral("compatibility-blocked-placeholder")
        : QStringLiteral("compatibility-blocked-not-linked");
    descriptor.compatibilityGate = QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED != 0
        ? QStringLiteral("production-operation-vectors-not-implemented")
        : QStringLiteral("production-adapter-not-linked");
    descriptor.unavailableReason = QStringLiteral("production-crypto-backend-unavailable");
    descriptor.operatorAction = QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED != 0
        ? QStringLiteral("run-production-crypto-compatibility-tests")
        : QStringLiteral("link-reviewed-production-crypto-backend");
    descriptor.productionReady = false;
    descriptor.linked = QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED != 0;
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
    evidence[QStringLiteral("knownAnswerPassed")] = isDraft && !e2eProductionCryptoRequired();
    evidence[QStringLiteral("roundTripPassed")] = isDraft && !e2eProductionCryptoRequired();
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
        evidence[QStringLiteral("operationManifestComplete")] =
            productionOperationSpecs().size() == cryptoOperations().size();
        evidence[QStringLiteral("implementedOperationCount")] = 0;
        evidence[QStringLiteral("blockedOperationCount")] = cryptoOperations().size();
    }
    evidence[QStringLiteral("failClosedPassed")] = !descriptor.productionReady;
    evidence[QStringLiteral("rawKeyExported")] = descriptor.rawKeyExported;
    evidence[QStringLiteral("privateMaterialExported")] = descriptor.privateMaterialExported;
    evidence[QStringLiteral("operatorAction")] = descriptor.productionReady
        ? QStringLiteral("none")
        : (descriptor.id == QString::fromLatin1(ProductionBackendId)
            ? QStringLiteral("implement-reviewed-production-provider-and-pass-compatibility-harness")
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
                !isProduction || descriptor.productionReady,
                isProduction
                    ? (descriptor.productionReady
                        ? QStringLiteral("all-production-operations-reviewed")
                        : QStringLiteral("production-operation-placeholders-present"))
                    : QStringLiteral("not-production-provider"),
                isProduction && !descriptor.productionReady
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
        && providerInvocationExecutionAccepted;

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
    status[QStringLiteral("operationManifestComplete")] =
        productionOperationSpecs().size() == cryptoOperations().size();
    status[QStringLiteral("implementedOperationCount")] = 0;
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
        obj["operatorAction"] = descriptor.linked
            ? QStringLiteral("complete-production-crypto-adapter-implementation-and-compatibility-tests")
            : QStringLiteral("link-reviewed-production-crypto-backend");
        return obj;
    }

    obj["reason"] = context.reason;
    obj["operatorAction"] = QStringLiteral("choose-a-registered-crypto-backend");
    return obj;
}

QJsonObject currentCryptoOperationStatus(E2ECryptoOperation operation) {
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

bool validIdentity(const QString& value) {
    return !value.trimmed().isEmpty() && value.size() <= 128;
}

bool validOptionalFingerprint(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) return true;
    if (normalized.size() != FingerprintHexLength) return false;
    for (const QChar ch : normalized) {
        const ushort code = ch.unicode();
        const bool digit = code >= '0' && code <= '9';
        const bool lowerHex = code >= 'a' && code <= 'f';
        if (!digit && !lowerHex) return false;
    }
    return true;
}

QByteArray randomBytes(qsizetype size) {
    QByteArray value;
    value.resize(size);
    for (qsizetype i = 0; i < size; ++i) {
        value[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
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
            output[offset] = static_cast<char>(input[offset] ^ block[i]);
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
    return QString::fromLatin1(E2EDraftSuite);
}

QString e2eCryptoBackendId() {
    return QString::fromLatin1(QTNETWORKCHAT_E2E_COMPILED_BACKEND_ID);
}

QString e2eAgreementSignatureSuite() {
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
    QString selectionSource;
    const QString requested = requestedBackendId(&selectionSource);
    const E2ECryptoAdapterDescriptor requestedDescriptor = cryptoAdapterForBackend(requested);
    const bool selectedDraft = requested == QString::fromLatin1(DraftBackendId);
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
    status["productionAcceptance"] =
        productionAcceptanceStatusForDescriptor(productionAdapterDescriptor(), productionOperations);
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
    status["protocol"] = QString::fromLatin1(E2EProtocolV1);
    status["suite"] = e2eDefaultSuite();
    status["wireCompatibleSuite"] = QString::fromLatin1(E2EAdvertisedSuite);
    status["signatureSuite"] = e2eAgreementSignatureSuite();
    status["productionReady"] = false;
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
        ? QStringLiteral("draft-backend-active")
        : availabilityReason;
    status["operatorAction"] = !available
        ? QStringLiteral("install-production-crypto-backend-or-disable-requirement")
        : QStringLiteral("draft-backend-allowed-for-current-build");
    status["rawKeyExported"] = false;
    return status;
}

QJsonObject e2eProductionCryptoAcceptanceStatus() {
    return e2eCryptoBackendStatus().value(QStringLiteral("productionAcceptance")).toObject();
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

QJsonObject e2eProductionCryptoOperationDispatchBindingStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionOperationDispatchBindings")).toObject();
}

QJsonObject e2eProductionCryptoOperationCallableManifestStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionOperationCallableManifest")).toObject();
}

QJsonObject e2eProductionCryptoOperationExecutionResultStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionOperationExecutionResult")).toObject();
}

QJsonObject e2eProductionCryptoProviderTableStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderTable")).toObject();
}

QJsonObject e2eProductionCryptoProviderTableBindingProbeStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderTableBindingProbe")).toObject();
}

QJsonObject e2eProductionCryptoProviderTableRegistrationStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderTableRegistration")).toObject();
}

QJsonObject e2eProductionCryptoProviderOperationPreflightStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderOperationPreflight")).toObject();
}

QJsonObject e2eProductionCryptoProviderCallFrameStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderCallFrame")).toObject();
}

QJsonObject e2eProductionCryptoProviderInvocationDryRunStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderInvocationDryRun")).toObject();
}

QJsonObject e2eProductionCryptoProviderInvocationResultStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderInvocationResult")).toObject();
}

QJsonObject e2eProductionCryptoProviderExecutionDecisionStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderExecutionDecision")).toObject();
}

QJsonObject e2eProductionCryptoProviderCallbackHarnessStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderCallbackHarness")).toObject();
}

QJsonObject e2eProductionCryptoProviderVectorSelfTestStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderVectorSelfTest")).toObject();
}

QJsonObject e2eProductionCryptoProviderExecutionSlotBindingStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderExecutionSlotBinding")).toObject();
}

QJsonObject e2eProductionCryptoProviderExecutionPathStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderExecutionPath")).toObject();
}

QJsonObject e2eProductionCryptoProviderInvocationSandboxStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderInvocationSandbox")).toObject();
}

QJsonObject e2eProductionCryptoProviderInvocationVectorResultStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderInvocationVectorResult")).toObject();
}

QJsonObject e2eProductionCryptoProviderInvocationExecutionStatus() {
    return e2eCryptoBackendStatus()
        .value(QStringLiteral("productionProviderInvocationExecution")).toObject();
}

QJsonObject e2eValidateProductionProviderTable(const qnc_e2e_provider_table_v1* table) {
    return providerTableValidationStatus(table);
}

QJsonObject e2eRegisterProductionProviderTable(const qnc_e2e_provider_table_v1* table) {
    g_registeredProductionProviderTable = table;
    return productionProviderTableRegistrationStatusForDescriptor(productionAdapterDescriptor());
}

QString e2eFingerprint(const QByteArray& value) {
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}

bool E2EKeyAgreement::isValid(QString* reason) const {
    if (!isSupportedE2EProtocol(protocol)) {
        return fail(reason, QStringLiteral("unsupported-protocol"));
    }
    if (!isSupportedE2ESuite(suite)) {
        return fail(reason, QStringLiteral("unsupported-suite"));
    }
    if (!validIdentity(senderId) || !validIdentity(receiverId) || senderId == receiverId) {
        return fail(reason, QStringLiteral("invalid-peer"));
    }
    if (keyId.trimmed().isEmpty() || keyId.size() > MaxKeyIdLength) {
        return fail(reason, QStringLiteral("invalid-key-id"));
    }
    if (publicKey.isEmpty() || publicKey.size() > MaxPublicKeyBytes) {
        return fail(reason, QStringLiteral("invalid-public-key"));
    }
    if (!validOptionalFingerprint(senderIdentityFingerprint)
        || !validOptionalFingerprint(receiverIdentityFingerprint)) {
        return fail(reason, QStringLiteral("invalid-identity-fingerprint"));
    }
    if (signature.size() > MaxSignatureBytes) {
        return fail(reason, QStringLiteral("invalid-signature"));
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

QJsonObject E2EKeyAgreement::toJson() const {
    QJsonObject obj;
    obj["protocol"] = normalizedE2EProtocol(protocol);
    obj["suite"] = normalizedE2ESuite(suite);
    obj["senderId"] = trimmed(senderId);
    obj["receiverId"] = trimmed(receiverId);
    obj["keyId"] = trimmed(keyId);
    obj["publicKey"] = toBase64Url(publicKey);
    if (!senderIdentityFingerprint.trimmed().isEmpty()) {
        obj["senderIdentityFingerprintSha256"] = senderIdentityFingerprint.trimmed().toLower();
    }
    if (!receiverIdentityFingerprint.trimmed().isEmpty()) {
        obj["receiverIdentityFingerprintSha256"] = receiverIdentityFingerprint.trimmed().toLower();
    }
    if (!signature.isEmpty()) {
        obj["signature"] = toBase64Url(signature);
    }
    obj["publicKeyFingerprintSha256"] = e2eFingerprint(publicKey);
    return obj;
}

E2EKeyAgreement E2EKeyAgreement::fromJson(const QJsonObject& obj) {
    E2EKeyAgreement agreement;
    agreement.protocol = normalizedE2EProtocol(obj.value("protocol").toString());
    agreement.suite = normalizedE2ESuite(obj.value("suite").toString());
    agreement.senderId = trimmed(obj.value("senderId").toString());
    agreement.receiverId = trimmed(obj.value("receiverId").toString());
    agreement.keyId = trimmed(obj.value("keyId").toString());
    agreement.publicKey = base64Field(obj, "publicKey");
    agreement.senderIdentityFingerprint = trimmed(obj.value("senderIdentityFingerprintSha256").toString()).toLower();
    agreement.receiverIdentityFingerprint = trimmed(obj.value("receiverIdentityFingerprintSha256").toString()).toLower();
    agreement.signature = base64Field(obj, "signature");
    return agreement;
}

bool E2EEnvelope::isValid(QString* reason) const {
    if (!isSupportedE2EProtocol(protocol)) {
        return fail(reason, QStringLiteral("unsupported-protocol"));
    }
    if (!isSupportedE2ESuite(suite)) {
        return fail(reason, QStringLiteral("unsupported-suite"));
    }
    if (!validIdentity(senderId) || !validIdentity(receiverId) || senderId == receiverId) {
        return fail(reason, QStringLiteral("invalid-peer"));
    }
    if (keyId.trimmed().isEmpty() || keyId.size() > MaxKeyIdLength) {
        return fail(reason, QStringLiteral("invalid-key-id"));
    }
    if (nonce.size() < MinNonceBytes || nonce.size() > MaxNonceBytes) {
        return fail(reason, QStringLiteral("invalid-nonce"));
    }
    if (ciphertext.isEmpty()) {
        return fail(reason, QStringLiteral("empty-ciphertext"));
    }
    if (tag.size() < MinTagBytes || tag.size() > MaxTagBytes) {
        return fail(reason, QStringLiteral("invalid-tag"));
    }
    if (aad.size() > MaxAadLength) {
        return fail(reason, QStringLiteral("invalid-aad"));
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

QJsonObject E2EEnvelope::toJson() const {
    QJsonObject obj;
    obj["protocol"] = normalizedE2EProtocol(protocol);
    obj["suite"] = normalizedE2ESuite(suite);
    obj["senderId"] = trimmed(senderId);
    obj["receiverId"] = trimmed(receiverId);
    obj["keyId"] = trimmed(keyId);
    obj["nonce"] = toBase64Url(nonce);
    obj["ciphertext"] = toBase64Url(ciphertext);
    obj["tag"] = toBase64Url(tag);
    if (!aad.trimmed().isEmpty()) {
        obj["aad"] = aad;
    }
    obj["ciphertextSha256"] = e2eFingerprint(ciphertext);
    return obj;
}

E2EEnvelope E2EEnvelope::fromJson(const QJsonObject& obj) {
    E2EEnvelope envelope;
    envelope.protocol = normalizedE2EProtocol(obj.value("protocol").toString());
    envelope.suite = normalizedE2ESuite(obj.value("suite").toString());
    envelope.senderId = trimmed(obj.value("senderId").toString());
    envelope.receiverId = trimmed(obj.value("receiverId").toString());
    envelope.keyId = trimmed(obj.value("keyId").toString());
    envelope.nonce = base64Field(obj, "nonce");
    envelope.ciphertext = base64Field(obj, "ciphertext");
    envelope.tag = base64Field(obj, "tag");
    envelope.aad = obj.value("aad").toString();
    return envelope;
}

QByteArray generateE2ESessionKey() {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::SessionKeyGeneration, nullptr)) {
        return QByteArray();
    }
    return randomBytes(SessionKeyBytes);
}

QByteArray generateE2EPrivateKey() {
    if (rejectWhenCryptoOperationUnavailable(E2ECryptoOperation::IdentityKeyGeneration, nullptr)) {
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
    const QByteArray expected = hmacSha256(identityPublicKey, agreementSignatureData(agreement));
    if (expected != agreement.signature) {
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
    const QByteArray expectedTag = hmacSha256(sessionKey, envelopeTagData(envelope));
    if (expectedTag != envelope.tag) {
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
