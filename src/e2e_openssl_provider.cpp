#include "qtnetworkchat_e2e_provider_api.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
constexpr std::size_t SessionKeyBytes = 32;
constexpr std::size_t Ed25519SeedBytes = 32;
constexpr std::size_t Ed25519PublicKeyBytes = 32;

void clearOutput(qnc_e2e_operation_output_v1* output) {
    if (!output) {
        return;
    }
    output->status = QNC_E2E_STATUS_REJECTED;
    output->material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
    output->public_output = {};
    output->sealed_output = {};
    output->sanitized_error_class = "rejected";
}

qnc_e2e_status_t reject(qnc_e2e_operation_output_v1* output,
                        qnc_e2e_status_t status,
                        const char* errorClass) {
    if (!output) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }
    output->status = status;
    output->material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
    output->public_output = {};
    output->sealed_output = {};
    output->sanitized_error_class = errorClass;
    return status;
}

bool hasBasicInput(const qnc_e2e_operation_input_v1* input,
                   qnc_e2e_operation_output_v1* output,
                   qnc_e2e_operation_t operation) {
    if (!input || !output || input->operation != operation || !input->suite_id) {
        reject(output, QNC_E2E_STATUS_INVALID_INPUT, "invalid-input");
        return false;
    }
    clearOutput(output);
    return true;
}

bool deriveEd25519PublicKey(const std::array<std::uint8_t, Ed25519SeedBytes>& seed,
                            std::array<std::uint8_t, Ed25519PublicKeyBytes>* publicKey) {
    if (!publicKey) {
        return false;
    }
    EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519,
                                                 nullptr,
                                                 seed.data(),
                                                 seed.size());
    if (!key) {
        return false;
    }
    std::size_t publicSize = publicKey->size();
    const int ok = EVP_PKEY_get_raw_public_key(key, publicKey->data(), &publicSize);
    EVP_PKEY_free(key);
    return ok == 1 && publicSize == publicKey->size();
}

std::array<std::uint8_t, Ed25519SeedBytes> seedFromPrimary(
    const qnc_e2e_operation_input_v1* input) {
    std::array<std::uint8_t, Ed25519SeedBytes> seed = {};
    if (input && input->primary.data && input->primary.size == seed.size()) {
        std::memcpy(seed.data(), input->primary.data, seed.size());
        return seed;
    }

    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    static const char domain[] = "qtnetworkchat-e2e-openssl-ed25519-seed-v1";
    SHA256_Update(&ctx, domain, sizeof(domain) - 1);
    if (input && input->suite_id) {
        SHA256_Update(&ctx, input->suite_id, std::strlen(input->suite_id));
    }
    if (input && input->primary.data && input->primary.size > 0) {
        SHA256_Update(&ctx, input->primary.data, input->primary.size);
    }
    SHA256_Final(seed.data(), &ctx);
    return seed;
}

qnc_e2e_status_t unsupportedOperation(qnc_e2e_operation_output_v1* output) {
    if (!output) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }
    output->status = QNC_E2E_STATUS_UNSUPPORTED;
    output->material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
    output->public_output = {};
    output->sealed_output = {};
    output->sanitized_error_class = "operation-not-implemented";
    return QNC_E2E_STATUS_UNSUPPORTED;
}
}

extern "C" qnc_e2e_status_t qnc_e2e_op_session_key_generation_v1(
    const qnc_e2e_operation_input_v1* input,
    qnc_e2e_operation_output_v1* output) {
    if (!hasBasicInput(input, output, QNC_E2E_OPERATION_SESSION_KEY_GENERATION)) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }

    static thread_local std::array<std::uint8_t, SessionKeyBytes> sessionKey = {};
    if (RAND_bytes(sessionKey.data(), static_cast<int>(sessionKey.size())) != 1) {
        return reject(output, QNC_E2E_STATUS_REJECTED, "random-source-unavailable");
    }

    output->status = QNC_E2E_STATUS_OK;
    output->material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
    output->public_output = {};
    output->sealed_output = { sessionKey.data(), sessionKey.size() };
    output->sanitized_error_class = "ok";
    return QNC_E2E_STATUS_OK;
}

extern "C" qnc_e2e_status_t qnc_e2e_op_identity_key_generation_v1(
    const qnc_e2e_operation_input_v1* input,
    qnc_e2e_operation_output_v1* output) {
    if (!hasBasicInput(input, output, QNC_E2E_OPERATION_IDENTITY_KEY_GENERATION)) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }

    static thread_local std::array<std::uint8_t, Ed25519SeedBytes> identitySeed = {};
    static thread_local std::array<std::uint8_t, Ed25519PublicKeyBytes> identityPublic = {};
    if (RAND_bytes(identitySeed.data(), static_cast<int>(identitySeed.size())) != 1) {
        return reject(output, QNC_E2E_STATUS_REJECTED, "random-source-unavailable");
    }
    if (!deriveEd25519PublicKey(identitySeed, &identityPublic)) {
        return reject(output, QNC_E2E_STATUS_REJECTED, "identity-public-derivation-failed");
    }

    output->status = QNC_E2E_STATUS_OK;
    output->material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
    output->public_output = { identityPublic.data(), identityPublic.size() };
    output->sealed_output = { identitySeed.data(), identitySeed.size() };
    output->sanitized_error_class = "ok";
    return QNC_E2E_STATUS_OK;
}

extern "C" qnc_e2e_status_t qnc_e2e_op_public_key_derivation_v1(
    const qnc_e2e_operation_input_v1* input,
    qnc_e2e_operation_output_v1* output) {
    if (!hasBasicInput(input, output, QNC_E2E_OPERATION_PUBLIC_KEY_DERIVATION)) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }
    if (!input->primary.data || input->primary.size == 0) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "missing-private-identity-handle");
    }

    static thread_local std::array<std::uint8_t, Ed25519PublicKeyBytes> derivedPublic = {};
    const std::array<std::uint8_t, Ed25519SeedBytes> seed = seedFromPrimary(input);
    if (!deriveEd25519PublicKey(seed, &derivedPublic)) {
        return reject(output, QNC_E2E_STATUS_REJECTED, "public-key-derivation-failed");
    }

    output->status = QNC_E2E_STATUS_OK;
    output->material_policy = QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED;
    output->public_output = { derivedPublic.data(), derivedPublic.size() };
    output->sealed_output = {};
    output->sanitized_error_class = "ok";
    return QNC_E2E_STATUS_OK;
}

extern "C" qnc_e2e_status_t qnc_e2e_op_agreement_sign_v1(
    const qnc_e2e_operation_input_v1*,
    qnc_e2e_operation_output_v1* output) {
    return unsupportedOperation(output);
}

extern "C" qnc_e2e_status_t qnc_e2e_op_agreement_verify_v1(
    const qnc_e2e_operation_input_v1*,
    qnc_e2e_operation_output_v1* output) {
    return unsupportedOperation(output);
}

extern "C" qnc_e2e_status_t qnc_e2e_op_session_derive_v1(
    const qnc_e2e_operation_input_v1*,
    qnc_e2e_operation_output_v1* output) {
    return unsupportedOperation(output);
}

extern "C" qnc_e2e_status_t qnc_e2e_op_payload_encrypt_v1(
    const qnc_e2e_operation_input_v1*,
    qnc_e2e_operation_output_v1* output) {
    return unsupportedOperation(output);
}

extern "C" qnc_e2e_status_t qnc_e2e_op_payload_decrypt_v1(
    const qnc_e2e_operation_input_v1*,
    qnc_e2e_operation_output_v1* output) {
    return unsupportedOperation(output);
}

extern "C" const qnc_e2e_provider_table_v1* qnc_e2e_openssl_provider_table_v1() {
    static const qnc_e2e_provider_table_v1 table = {
        QNC_E2E_PROVIDER_TABLE_ABI,
        "openssl-reviewed-provider-v1",
        QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT,
        qnc_e2e_op_session_key_generation_v1,
        qnc_e2e_op_identity_key_generation_v1,
        qnc_e2e_op_public_key_derivation_v1,
        qnc_e2e_op_agreement_sign_v1,
        qnc_e2e_op_agreement_verify_v1,
        qnc_e2e_op_session_derive_v1,
        qnc_e2e_op_payload_encrypt_v1,
        qnc_e2e_op_payload_decrypt_v1,
    };
    return &table;
}
