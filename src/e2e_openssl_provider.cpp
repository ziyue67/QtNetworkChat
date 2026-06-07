#include "qtnetworkchat_e2e_provider_api.h"

#include <openssl/rand.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {
constexpr std::size_t SessionKeyBytes = 32;

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
    if (!input || !output || input->operation != QNC_E2E_OPERATION_SESSION_KEY_GENERATION
        || !input->suite_id) {
        if (output) {
            output->status = QNC_E2E_STATUS_INVALID_INPUT;
            output->material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
            output->public_output = {};
            output->sealed_output = {};
            output->sanitized_error_class = "invalid-input";
        }
        return QNC_E2E_STATUS_INVALID_INPUT;
    }

    static thread_local std::array<std::uint8_t, SessionKeyBytes> sessionKey = {};
    if (RAND_bytes(sessionKey.data(), static_cast<int>(sessionKey.size())) != 1) {
        output->status = QNC_E2E_STATUS_REJECTED;
        output->material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
        output->public_output = {};
        output->sealed_output = {};
        output->sanitized_error_class = "random-source-unavailable";
        return QNC_E2E_STATUS_REJECTED;
    }

    output->status = QNC_E2E_STATUS_OK;
    output->material_policy = QNC_E2E_MATERIAL_HANDLE_ONLY;
    output->public_output = {};
    output->sealed_output = { sessionKey.data(), sessionKey.size() };
    output->sanitized_error_class = "ok";
    return QNC_E2E_STATUS_OK;
}

extern "C" qnc_e2e_status_t qnc_e2e_op_identity_key_generation_v1(
    const qnc_e2e_operation_input_v1*,
    qnc_e2e_operation_output_v1* output) {
    return unsupportedOperation(output);
}

extern "C" qnc_e2e_status_t qnc_e2e_op_public_key_derivation_v1(
    const qnc_e2e_operation_input_v1*,
    qnc_e2e_operation_output_v1* output) {
    return unsupportedOperation(output);
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
