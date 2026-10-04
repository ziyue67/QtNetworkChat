#pragma once

#include "qtnetworkchat_e2e_provider_api.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

// Internal provider ABI boundary. Selection and status reporting stay in e2eenvelope.cpp.
namespace E2EProviderRuntime {
inline constexpr qsizetype SessionKeyBytes = 32;
inline constexpr char E2EAdvertisedSuite[] = "x25519-hkdf-sha256-aes-256-gcm";

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

bool allProductionProviderOperationsBound();
QString sanitizedBackendId(QString value);
QJsonObject providerTableValidationStatus(const qnc_e2e_provider_table_v1* table);
qnc_e2e_provider_operation_v1 providerOperationPointer(const qnc_e2e_provider_table_v1* table,
                                                      E2ECryptoOperation operation);
qnc_e2e_operation_t providerOperationEnum(E2ECryptoOperation operation);
QString providerStatusClass(qnc_e2e_status_t status);
QString providerMaterialPolicyClass(qnc_e2e_material_policy_t policy);
ProviderDispatchResult dispatchProductionProviderOperation(const qnc_e2e_provider_table_v1* table,
                                                          E2ECryptoOperation operation,
                                                          const QByteArray& primary,
                                                          const QByteArray& secondary,
                                                          const QByteArray& aad);
bool providerResultOk(const ProviderDispatchResult& result);
// Exercises successful operations, tampered authentication, and malformed key material.
bool productionProviderRuntimeReady(const qnc_e2e_provider_table_v1* table, QString* reason = nullptr);
} // namespace E2EProviderRuntime
