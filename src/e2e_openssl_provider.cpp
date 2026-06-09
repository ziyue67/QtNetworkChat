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
constexpr std::size_t AesGcmNonceBytes = 12;
constexpr std::size_t AesGcmTagBytes = 16;
constexpr std::size_t MaxProbePayloadBytes = 4096;
constexpr std::size_t MaxSessionDeriveMaterialBytes = 4096;
constexpr char SessionDerivePrimaryDomain[] = "qtnetworkchat-e2e-authenticated-production-v1|";
constexpr char SessionDeriveSecondaryDomain[] =
    "qtnetworkchat-e2e-production-session-public-material-v1|";

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

bool validSessionDeriveMaterial(const qnc_e2e_operation_input_v1* input);

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
    if (!sessionKey || !validSessionDeriveMaterial(input)) {
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

bool copyExactBuffer(const qnc_e2e_buffer_view_v1& view,
                     std::uint8_t* output,
                     std::size_t outputSize) {
    if (!view.data || !output || view.size != outputSize) {
        return false;
    }
    std::memcpy(output, view.data, outputSize);
    return true;
}

bool bufferStartsWith(const qnc_e2e_buffer_view_v1& view, const char* prefix) {
    if (!view.data || !prefix) {
        return false;
    }
    const std::size_t prefixSize = std::strlen(prefix);
    return view.size >= prefixSize
        && std::memcmp(view.data, prefix, prefixSize) == 0;
}

bool validSessionDeriveMaterial(const qnc_e2e_operation_input_v1* input) {
    return input
        && input->primary.data
        && input->primary.size > std::strlen(SessionDerivePrimaryDomain)
        && input->primary.size <= MaxSessionDeriveMaterialBytes
        && bufferStartsWith(input->primary, SessionDerivePrimaryDomain)
        && input->secondary.data
        && input->secondary.size > std::strlen(SessionDeriveSecondaryDomain)
        && input->secondary.size <= MaxSessionDeriveMaterialBytes
        && bufferStartsWith(input->secondary, SessionDeriveSecondaryDomain)
        && input->aad.data
        && input->aad.size > 0
        && input->aad.size <= MaxSessionDeriveMaterialBytes;
}

bool copySessionKeyFromPrimary(const qnc_e2e_operation_input_v1* input,
                               std::array<std::uint8_t, SessionKeyBytes>* key) {
    return input && key && copyExactBuffer(input->primary, key->data(), key->size());
}

bool nonceFromAad(const qnc_e2e_operation_input_v1* input,
                  std::array<std::uint8_t, AesGcmNonceBytes>* nonce) {
    if (!input || !nonce || !input->aad.data || input->aad.size == 0) {
        return false;
    }
    std::array<std::uint8_t, HkdfDigestBytes> digest = {};
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    static const char domain[] = "qtnetworkchat-e2e-openssl-payload-nonce-v1";
    SHA256_Update(&ctx, domain, sizeof(domain) - 1);
    if (input->suite_id) {
        SHA256_Update(&ctx, input->suite_id, std::strlen(input->suite_id));
    }
    SHA256_Update(&ctx, input->aad.data, input->aad.size);
    SHA256_Final(digest.data(), &ctx);
    std::memcpy(nonce->data(), digest.data(), nonce->size());
    return true;
}

bool aesGcmEncrypt(const std::array<std::uint8_t, SessionKeyBytes>& key,
                   const std::array<std::uint8_t, AesGcmNonceBytes>& nonce,
                   const qnc_e2e_buffer_view_v1& plaintext,
                   const qnc_e2e_buffer_view_v1& aad,
                   std::array<std::uint8_t, AesGcmNonceBytes + AesGcmTagBytes + MaxProbePayloadBytes>* sealed,
                   std::size_t* sealedSize) {
    if (!plaintext.data || plaintext.size == 0 || plaintext.size > MaxProbePayloadBytes
        || !aad.data || aad.size == 0 || !sealed || !sealedSize) {
        return false;
    }
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return false;
    }
    bool ok = false;
    int outLen = 0;
    int finalLen = 0;
    std::uint8_t* ciphertext = sealed->data() + AesGcmNonceBytes + AesGcmTagBytes;
    std::memcpy(sealed->data(), nonce.data(), nonce.size());
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                              static_cast<int>(nonce.size()),
                              nullptr) == 1
        && EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) == 1
        && EVP_EncryptUpdate(ctx, nullptr, &outLen, aad.data, static_cast<int>(aad.size)) == 1
        && EVP_EncryptUpdate(ctx,
                             ciphertext,
                             &outLen,
                             plaintext.data,
                             static_cast<int>(plaintext.size)) == 1
        && EVP_EncryptFinal_ex(ctx, ciphertext + outLen, &finalLen) == 1
        && EVP_CIPHER_CTX_ctrl(ctx,
                               EVP_CTRL_GCM_GET_TAG,
                               AesGcmTagBytes,
                               sealed->data() + AesGcmNonceBytes) == 1) {
        *sealedSize = AesGcmNonceBytes + AesGcmTagBytes
            + static_cast<std::size_t>(outLen + finalLen);
        ok = true;
    }
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

bool aesGcmDecrypt(const std::array<std::uint8_t, SessionKeyBytes>& key,
                   const qnc_e2e_buffer_view_v1& sealed,
                   const qnc_e2e_buffer_view_v1& aad,
                   std::array<std::uint8_t, MaxProbePayloadBytes>* plaintext,
                   std::size_t* plaintextSize) {
    if (!sealed.data || sealed.size <= AesGcmNonceBytes + AesGcmTagBytes
        || sealed.size > AesGcmNonceBytes + AesGcmTagBytes + MaxProbePayloadBytes
        || !aad.data || aad.size == 0 || !plaintext || !plaintextSize) {
        return false;
    }
    const std::uint8_t* nonce = sealed.data;
    const std::uint8_t* tag = sealed.data + AesGcmNonceBytes;
    const std::uint8_t* ciphertext = sealed.data + AesGcmNonceBytes + AesGcmTagBytes;
    const std::size_t ciphertextSize = sealed.size - AesGcmNonceBytes - AesGcmTagBytes;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return false;
    }
    bool ok = false;
    int outLen = 0;
    int finalLen = 0;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx,
                              EVP_CTRL_GCM_SET_IVLEN,
                              AesGcmNonceBytes,
                              nullptr) == 1
        && EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce) == 1
        && EVP_DecryptUpdate(ctx, nullptr, &outLen, aad.data, static_cast<int>(aad.size)) == 1
        && EVP_DecryptUpdate(ctx,
                             plaintext->data(),
                             &outLen,
                             ciphertext,
                             static_cast<int>(ciphertextSize)) == 1
        && EVP_CIPHER_CTX_ctrl(ctx,
                               EVP_CTRL_GCM_SET_TAG,
                               AesGcmTagBytes,
                               const_cast<std::uint8_t*>(tag)) == 1
        && EVP_DecryptFinal_ex(ctx, plaintext->data() + outLen, &finalLen) == 1) {
        *plaintextSize = static_cast<std::size_t>(outLen + finalLen);
        ok = true;
    }
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

bool copySeedFromPrimary(const qnc_e2e_operation_input_v1* input,
                         std::array<std::uint8_t, Ed25519SeedBytes>* seed) {
    return input && seed && copyExactBuffer(input->primary, seed->data(), seed->size());
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
    if (!input->primary.data || input->primary.size != Ed25519SeedBytes) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "missing-private-identity-handle");
    }

    static thread_local std::array<std::uint8_t, Ed25519PublicKeyBytes> derivedPublic = {};
    std::array<std::uint8_t, Ed25519SeedBytes> seed = {};
    if (!copySeedFromPrimary(input, &seed)) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "invalid-private-identity-handle");
    }
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
    if (!input->primary.data || input->primary.size != Ed25519SeedBytes
        || !input->secondary.data || input->secondary.size == 0) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "missing-signing-input");
    }

    static thread_local std::array<std::uint8_t, Ed25519SignatureBytes> signature = {};
    std::array<std::uint8_t, Ed25519SeedBytes> seed = {};
    if (!copySeedFromPrimary(input, &seed)) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "invalid-signing-key-handle");
    }
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
    if (!input->primary.data || input->primary.size != Ed25519PublicKeyBytes
        || !input->secondary.data || input->secondary.size == 0
        || !input->aad.data || input->aad.size != Ed25519SignatureBytes) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "missing-verification-input");
    }

    std::array<std::uint8_t, Ed25519PublicKeyBytes> publicKey = {};
    if (!copyExactBuffer(input->primary, publicKey.data(), publicKey.size())) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "invalid-verification-public-key");
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
    if (!validSessionDeriveMaterial(input)) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "missing-session-derive-input");
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
    const qnc_e2e_operation_input_v1* input,
    qnc_e2e_operation_output_v1* output) {
    if (!hasBasicInput(input, output, QNC_E2E_OPERATION_PAYLOAD_ENCRYPT)) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }
    if (!input->primary.data || input->primary.size != SessionKeyBytes
        || !input->secondary.data || input->secondary.size == 0
        || !input->aad.data || input->aad.size == 0) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "missing-payload-encrypt-input");
    }

    static thread_local std::array<std::uint8_t, AesGcmNonceBytes + AesGcmTagBytes + MaxProbePayloadBytes> sealed = {};
    static thread_local std::size_t sealedSize = 0;
    std::array<std::uint8_t, SessionKeyBytes> key = {};
    if (!copySessionKeyFromPrimary(input, &key)) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "invalid-payload-key-handle");
    }
    std::array<std::uint8_t, AesGcmNonceBytes> nonce = {};
    if (!nonceFromAad(input, &nonce)
        || !aesGcmEncrypt(key, nonce, input->secondary, input->aad, &sealed, &sealedSize)) {
        return reject(output, QNC_E2E_STATUS_REJECTED, "payload-encrypt-failed");
    }

    output->status = QNC_E2E_STATUS_OK;
    output->material_policy = QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED;
    output->public_output = {};
    output->sealed_output = { sealed.data(), sealedSize };
    output->sanitized_error_class = "ok";
    return QNC_E2E_STATUS_OK;
}

extern "C" qnc_e2e_status_t qnc_e2e_op_payload_decrypt_v1(
    const qnc_e2e_operation_input_v1* input,
    qnc_e2e_operation_output_v1* output) {
    if (!hasBasicInput(input, output, QNC_E2E_OPERATION_PAYLOAD_DECRYPT)) {
        return QNC_E2E_STATUS_INVALID_INPUT;
    }
    if (!input->primary.data || input->primary.size != SessionKeyBytes
        || !input->secondary.data || input->secondary.size == 0
        || !input->aad.data || input->aad.size == 0) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "missing-payload-decrypt-input");
    }

    static thread_local std::array<std::uint8_t, MaxProbePayloadBytes> plaintext = {};
    static thread_local std::size_t plaintextSize = 0;
    std::array<std::uint8_t, SessionKeyBytes> key = {};
    if (!copySessionKeyFromPrimary(input, &key)) {
        return reject(output, QNC_E2E_STATUS_INVALID_INPUT, "invalid-payload-key-handle");
    }
    if (!aesGcmDecrypt(key, input->secondary, input->aad, &plaintext, &plaintextSize)) {
        return reject(output, QNC_E2E_STATUS_REJECTED, "payload-decrypt-failed");
    }

    output->status = QNC_E2E_STATUS_OK;
    output->material_policy = QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED;
    output->public_output = { plaintext.data(), plaintextSize };
    output->sealed_output = {};
    output->sanitized_error_class = "ok";
    return QNC_E2E_STATUS_OK;
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
