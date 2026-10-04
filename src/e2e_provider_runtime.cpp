#include "e2e_provider_runtime.h"
#include "e2e_codec_support.h"
#include "e2e_crypto_primitives.h"
#include "qtnetworkchat_e2e_crypto_config.h"

#include <cstddef>

namespace E2EProviderRuntime {
using namespace E2ECodecSupport;
using namespace E2ECryptoPrimitives;

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

ProviderDispatchResult dispatchProductionProviderOperation(const qnc_e2e_provider_table_v1* table,
                                                           E2ECryptoOperation operation,
                                                           const QByteArray& primary,
                                                           const QByteArray& secondary,
                                                           const QByteArray& aad) {
    ProviderDispatchResult result;
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
    input.suite_id = E2EProductionSuite;
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

bool productionProviderRuntimeReady(const qnc_e2e_provider_table_v1* table, QString* reason) {
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
        dispatchProductionProviderOperation(table, E2ECryptoOperation::SessionKeyGeneration,
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
        dispatchProductionProviderOperation(table, E2ECryptoOperation::IdentityKeyGeneration,
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
        dispatchProductionProviderOperation(table, E2ECryptoOperation::PublicKeyDerivation,
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
        dispatchProductionProviderOperation(table, E2ECryptoOperation::AgreementSign,
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
        dispatchProductionProviderOperation(table, E2ECryptoOperation::AgreementVerify,
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
        dispatchProductionProviderOperation(table, E2ECryptoOperation::AgreementVerify,
                                            publicKey.publicOutput,
                                            transcript,
                                            tamperedSignature);
    if (!rejectedSignature.invoked
        || rejectedSignature.callbackStatus != QNC_E2E_STATUS_REJECTED
        || rejectedSignature.outputStatus != QNC_E2E_STATUS_REJECTED) {
        return fail(reason, QStringLiteral("production-agreement-verify-negative-self-test-failed"));
    }

    const ProviderDispatchResult derived =
        dispatchProductionProviderOperation(table, E2ECryptoOperation::SessionDerive,
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
        dispatchProductionProviderOperation(table, E2ECryptoOperation::PayloadEncrypt,
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
        dispatchProductionProviderOperation(table, E2ECryptoOperation::PayloadDecrypt,
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
        dispatchProductionProviderOperation(table, E2ECryptoOperation::PayloadDecrypt,
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
        dispatchProductionProviderOperation(table, E2ECryptoOperation::PublicKeyDerivation,
                                            malformedIdentityHandle,
                                            empty,
                                            empty);
    if (!rejectedPublicDerivation.invoked
        || rejectedPublicDerivation.callbackStatus != QNC_E2E_STATUS_INVALID_INPUT
        || rejectedPublicDerivation.outputStatus != QNC_E2E_STATUS_INVALID_INPUT) {
        return fail(reason, QStringLiteral("production-public-key-derivation-malformed-handle-self-test-failed"));
    }

    const ProviderDispatchResult rejectedAgreementVerify =
        dispatchProductionProviderOperation(table, E2ECryptoOperation::AgreementVerify,
                                            publicKey.publicOutput.left(31),
                                            transcript,
                                            signature.publicOutput);
    if (!rejectedAgreementVerify.invoked
        || rejectedAgreementVerify.callbackStatus != QNC_E2E_STATUS_INVALID_INPUT
        || rejectedAgreementVerify.outputStatus != QNC_E2E_STATUS_INVALID_INPUT) {
        return fail(reason, QStringLiteral("production-agreement-verify-malformed-public-key-self-test-failed"));
    }

    const ProviderDispatchResult rejectedSessionDerive =
        dispatchProductionProviderOperation(table, E2ECryptoOperation::SessionDerive,
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
        dispatchProductionProviderOperation(table, E2ECryptoOperation::PayloadEncrypt,
                                            malformedSessionKey,
                                            plaintext,
                                            payloadAad);
    if (!rejectedPayloadEncrypt.invoked
        || rejectedPayloadEncrypt.callbackStatus != QNC_E2E_STATUS_INVALID_INPUT
        || rejectedPayloadEncrypt.outputStatus != QNC_E2E_STATUS_INVALID_INPUT) {
        return fail(reason, QStringLiteral("production-payload-encrypt-malformed-key-self-test-failed"));
    }

    const ProviderDispatchResult rejectedPayloadDecrypt =
        dispatchProductionProviderOperation(table, E2ECryptoOperation::PayloadDecrypt,
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

} // namespace E2EProviderRuntime
