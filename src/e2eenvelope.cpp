#include "e2eenvelope.h"
#include "qtnetworkchat_e2e_crypto_config.h"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QList>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QStringList>

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
    QString symbol = cryptoOperationName(operation);
    symbol.replace(QLatin1Char('-'), QLatin1Char('_'));
    return QStringLiteral("qnc_e2e_op_%1_v1").arg(symbol);
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
    const bool accepted = linked
        && descriptor.productionReady
        && operationContractComplete
        && compatibilityPassed
        && noMaterialExport
        && allOperationsAvailable;

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
    status[QStringLiteral("operationManifestComplete")] =
        productionOperationSpecs().size() == cryptoOperations().size();
    status[QStringLiteral("implementedOperationCount")] = 0;
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
