#include "e2e_backend_status_p.h"
#include <QCryptographicHash>
#include <QHash>
#include <cstddef>

namespace E2EBackendStatus {
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
        setNoKeyExportFields(op);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setProviderReportIdentity(frame, descriptor);
        frame[QStringLiteral("providerSymbol")] = productionOperationProviderSymbol(operation);
        frame[QStringLiteral("providerAbiSignature")] =
            productionOperationProviderAbiSignature(operation);
        frame[QStringLiteral("fixtureHashSha256")] = productionHarnessFixtureHash(spec);
        frame[QStringLiteral("suiteId")] = QString::fromLatin1(E2EProductionSuite);
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
        setNoKeyExportFields(frame);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setNoKeyExportFields(invocation);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setProviderReportIdentity(result, descriptor);
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
        setNoKeyExportFields(result);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setProviderReportIdentity(decision, descriptor);
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
        setNoKeyExportFields(decision);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setProviderReportIdentity(callback, descriptor);
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
        setNoKeyExportFields(callback);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setProviderReportIdentity(test, descriptor);
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
        setNoKeyExportFields(test);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setProviderReportIdentity(slot, descriptor);
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
        setNoKeyExportFields(slot);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setProviderReportIdentity(path, descriptor);
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
        setNoKeyExportFields(path);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setProviderReportIdentity(sandbox, descriptor);
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
        setNoKeyExportFields(sandbox);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setProviderReportIdentity(result, descriptor);
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
        setNoKeyExportFields(result);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
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
        setProviderReportIdentity(execution, descriptor);
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
        setNoKeyExportFields(execution);
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
    setProviderReportIdentity(status, descriptor);
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
    setNoKeyExportFields(status);
    status[QStringLiteral("sessionSecretExported")] = false;
    status[QStringLiteral("privateIdentityMaterialExported")] = false;
    status[QStringLiteral("fullPublicIdentityMaterialExported")] = false;
    return status;
}
} // namespace E2EBackendStatus
