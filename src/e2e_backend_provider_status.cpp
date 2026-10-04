#include "e2e_backend_status_p.h"
#include <QCryptographicHash>
#include <QHash>
#include <cstddef>

namespace E2EBackendStatus {
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
        productionProviderTableRegistered();
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

ProviderDispatchResult dispatchProductionProviderOperation(E2ECryptoOperation operation,
                                                           const QByteArray& primary,
                                                           const QByteArray& secondary,
                                                           const QByteArray& aad) {
    return E2EProviderRuntime::dispatchProductionProviderOperation(
        activeProductionProviderTable(), operation, primary, secondary, aad);
}

bool productionProviderRuntimeReady(QString* reason) {
    return E2EProviderRuntime::productionProviderRuntimeReady(activeProductionProviderTable(), reason);
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
} // namespace E2EBackendStatus
