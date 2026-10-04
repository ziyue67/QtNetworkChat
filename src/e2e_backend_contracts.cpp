#include "e2e_backend_status_p.h"
#include <QCryptographicHash>
#include <QHash>
#include <cstddef>

namespace E2EBackendStatus {
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

namespace {
bool productionOperationCompiled(E2ECryptoOperation operation) {
    switch (operation) {
    case E2ECryptoOperation::SessionKeyGeneration:
        return QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_SESSION_KEY_GENERATION != 0;
    case E2ECryptoOperation::IdentityKeyGeneration:
        return QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_IDENTITY_KEY_GENERATION != 0;
    case E2ECryptoOperation::PublicKeyDerivation:
        return QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PUBLIC_KEY_DERIVATION != 0;
    case E2ECryptoOperation::AgreementSign:
        return QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_AGREEMENT_SIGN != 0;
    case E2ECryptoOperation::AgreementVerify:
        return QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_AGREEMENT_VERIFY != 0;
    case E2ECryptoOperation::SessionDerive:
        return QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_SESSION_DERIVE != 0;
    case E2ECryptoOperation::PayloadEncrypt:
        return QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PAYLOAD_ENCRYPT != 0;
    case E2ECryptoOperation::PayloadDecrypt:
        return QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PAYLOAD_DECRYPT != 0;
    }
    return false;
}
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
        if (productionOperationCompiled(operation)) {
            spec.implementationState = QStringLiteral("linked-reviewed-") + cryptoOperationName(operation);
            spec.compatibilityStatus = QStringLiteral("known-answer-shape-passed");
            spec.migrationBlocker = QStringLiteral("production-acceptance-gates-not-open");
            spec.operatorAction = QStringLiteral("complete-production-acceptance-gates-before-enabling-dispatch");
            spec.implemented = true;
            spec.knownAnswerPassed = true;
            spec.roundTripPassed = operation == E2ECryptoOperation::PayloadEncrypt
                || operation == E2ECryptoOperation::PayloadDecrypt;
        }
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
    setNoKeyExportFields(obj);
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
    setNoKeyExportFields(contract);
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
    setProviderReportIdentity(frame, descriptor);
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
    setNoKeyExportFields(frame);
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
    setNoKeyExportFields(evidence);
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
    setProviderReportIdentity(slot, descriptor);
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
    setNoKeyExportFields(slot);
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
    setNoKeyExportFields(invocation);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setProviderReportIdentity(binding, descriptor);
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
        setNoKeyExportFields(binding);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setProviderReportIdentity(entry, descriptor);
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
        setNoKeyExportFields(entry);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setProviderReportIdentity(result, descriptor);
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
        setNoKeyExportFields(result);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}
} // namespace E2EBackendStatus
