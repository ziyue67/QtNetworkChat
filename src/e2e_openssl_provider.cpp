#include "qtnetworkchat_e2e_provider_api.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>
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
constexpr std::size_t Ed25519SignatureBytes = 64;
constexpr std::size_t HkdfDigestBytes = 32;

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

bool signEd25519(const std::array<std::uint8_t, Ed25519SeedBytes>& seed,
                 const qnc_e2e_buffer_view_v1& transcript,
                 std::array<std::uint8_t, Ed25519SignatureBytes>* signature) {
    if (!signature || !transcript.data || transcript.size == 0) {
        return false;
    }
    EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519,
                                                 nullptr,
                                                 seed.data(),
                                                 seed.size());
    if (!key) {
        return false;
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(key);
        return false;
    }
    bool ok = false;
    std::size_t signatureSize = signature->size();
    if (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key) == 1
        && EVP_DigestSign(ctx,
                          signature->data(),
                          &signatureSize,
                          transcript.data,
                          transcript.size) == 1
        && signatureSize == signature->size()) {
        ok = true;
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

bool verifyEd25519(const std::array<std::uint8_t, Ed25519PublicKeyBytes>& publicKey,
                   const qnc_e2e_buffer_view_v1& transcript,
                   const qnc_e2e_buffer_view_v1& signature) {
    if (!transcript.data || transcript.size == 0
        || !signature.data || signature.size != Ed25519SignatureBytes) {
        return false;
    }
    EVP_PKEY* key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519,
                                                nullptr,
                                                publicKey.data(),
                                                publicKey.size());
    if (!key) {
        return false;
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(key);
        return false;
    }
    const bool ok = EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1
        && EVP_DigestVerify(ctx,
                            signature.data,
                            signature.size,
                            transcript.data,
                            transcript.size) == 1;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

bool deriveSessionKey(const qnc_e2e_operation_input_v1* input,
                      std::array<std::uint8_t, SessionKeyBytes>* sessionKey) {
    if (!input || !sessionKey
        || !input->primary.data || input->primary.size == 0
        || !input->secondary.data || input->secondary.size == 0
        || !input->aad.data || input->aad.size == 0) {
        return false;
    }

    std::array<std::uint8_t, HkdfDigestBytes> salt = {};
    SHA256_CTX saltCtx;
    SHA256_Init(&saltCtx);
    static const char saltDomain[] = "qtnetworkchat-e2e-openssl-session-salt-v1";
    SHA256_Update(&saltCtx, saltDomain, sizeof(saltDomain) - 1);
    if (input->suite_id) {
        SHA256_Update(&saltCtx, input->suite_id, std::strlen(input->suite_id));
    }
    SHA256_Update(&saltCtx, input->aad.data, input->aad.size);
    SHA256_Final(salt.data(), &saltCtx);

    std::array<std::uint8_t, HkdfDigestBytes> info = {};
    SHA256_CTX infoCtx;
    SHA256_Init(&infoCtx);
    static const char infoDomain[] = "qtnetworkchat-e2e-openssl-session-info-v1";
    SHA256_Update(&infoCtx, infoDomain, sizeof(infoDomain) - 1);
    SHA256_Update(&infoCtx, input->secondary.data, input->secondary.size);
    SHA256_Update(&infoCtx, input->aad.data, input->aad.size);
    SHA256_Final(info.data(), &infoCtx);

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!ctx) {
        return false;
    }
    const bool ok = EVP_PKEY_derive_init(ctx) == 1
        && EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) == 1
        && EVP_PKEY_CTX_set1_hkdf_salt(ctx,
                                       salt.data(),
                                       static_cast<int>(salt.size())) == 1
        && EVP_PKEY_CTX_set1_hkdf_key(ctx,
                                      input->primary.data,
                                      static_cast<int>(input->primary.size)) == 1
        && EVP_PKEY_CTX_add1_hkdf_info(ctx,
                                       info.data(),
                                       static_cast<int>(info.size())) == 1
        && [&]() {
            std::size_t outputSize = sessionKey->size();
            return EVP_PKEY_derive(ctx, sessionKey->data(), &outputSize) == 1
                && outputSize == sessionKey->size();
        }();
    EVP_PKEY_CTX_free(ctx);
    return ok;
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
    const qnc_e2e_operation_input_v1* input,
    qnc_e2e_operation_output_v1* output) {
    if (!hasBasicInput(input, output, QNC_E2E_OPERATION_AGREEMENT_SIGN)) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }
    if (!input->primary.data || input->primary.size == 0
        || !input->secondary.data || input->secondary.size == 0) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "missing-signing-input");
    }

    static thread_local std::array<std::uint8_t, Ed25519SignatureBytes> signature = {};
    const std::array<std::uint8_t, Ed25519SeedBytes> seed = seedFromPrimary(input);
    if (!signEd25519(seed, input->secondary, &signature)) {
        return reject(output, QNC_E2E_STATUS_REJECTED, "agreement-sign-failed");
    }

    output->status = QNC_E2E_STATUS_OK;
    output->material_policy = QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED;
    output->public_output = { signature.data(), signature.size() };
    output->sealed_output = {};
    output->sanitized_error_class = "ok";
    return QNC_E2E_STATUS_OK;
}

extern "C" qnc_e2e_status_t qnc_e2e_op_agreement_verify_v1(
    const qnc_e2e_operation_input_v1* input,
    qnc_e2e_operation_output_v1* output) {
    if (!hasBasicInput(input, output, QNC_E2E_OPERATION_AGREEMENT_VERIFY)) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }
    if (!input->primary.data || input->primary.size == 0
        || !input->secondary.data || input->secondary.size == 0
        || !input->aad.data || input->aad.size != Ed25519SignatureBytes) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "missing-verification-input");
    }

    std::array<std::uint8_t, Ed25519PublicKeyBytes> publicKey = {};
    if (input->primary.size == publicKey.size()) {
        std::memcpy(publicKey.data(), input->primary.data, publicKey.size());
    } else {
        const std::array<std::uint8_t, Ed25519SeedBytes> seed = seedFromPrimary(input);
        if (!deriveEd25519PublicKey(seed, &publicKey)) {
            return reject(output, QNC_E2E_STATUS_REJECTED, "agreement-public-derivation-failed");
        }
    }
    if (!verifyEd25519(publicKey, input->secondary, input->aad)) {
        return reject(output, QNC_E2E_STATUS_REJECTED, "agreement-signature-invalid");
    }

    output->status = QNC_E2E_STATUS_OK;
    output->material_policy = QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED;
    output->public_output = { publicKey.data(), publicKey.size() };
    output->sealed_output = {};
    output->sanitized_error_class = "ok";
    return QNC_E2E_STATUS_OK;
}

extern "C" qnc_e2e_status_t qnc_e2e_op_session_derive_v1(
    const qnc_e2e_operation_input_v1* input,
    qnc_e2e_operation_output_v1* output) {
    if (!hasBasicInput(input, output, QNC_E2E_OPERATION_SESSION_DERIVE)) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }

    static thread_local std::array<std::uint8_t, SessionKeyBytes> sessionKey = {};
    if (!deriveSessionKey(input, &sessionKey)) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "missing-session-derive-input");
    }

    output->status = QNC_E2E_STATUS_OK;
    output->material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
    output->public_output = {};
    output->sealed_output = { sessionKey.data(), sessionKey.size() };
    output->sanitized_error_class = "ok";
    return QNC_E2E_STATUS_OK;
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
