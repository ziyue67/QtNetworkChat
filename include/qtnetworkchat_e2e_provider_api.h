#ifndef QTNETWORKCHAT_E2E_PROVIDER_API_H
#define QTNETWORKCHAT_E2E_PROVIDER_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QNC_E2E_PROVIDER_TABLE_ABI "qtnetworkchat-e2e-provider-table-v1"
#define QNC_E2E_OPERATION_CONTRACT_VERSION "qtnetworkchat-e2e-crypto-ops-v1"
#define QNC_E2E_PROVIDER_REQUIRED_OPERATION_COUNT 8
#define QNC_E2E_PROVIDER_SYMBOL_SESSION_KEY_GENERATION "qnc_e2e_op_session_key_generation_v1"
#define QNC_E2E_PROVIDER_SYMBOL_IDENTITY_KEY_GENERATION "qnc_e2e_op_identity_key_generation_v1"
#define QNC_E2E_PROVIDER_SYMBOL_PUBLIC_KEY_DERIVATION "qnc_e2e_op_public_key_derivation_v1"
#define QNC_E2E_PROVIDER_SYMBOL_AGREEMENT_SIGN "qnc_e2e_op_agreement_sign_v1"
#define QNC_E2E_PROVIDER_SYMBOL_AGREEMENT_VERIFY "qnc_e2e_op_agreement_verify_v1"
#define QNC_E2E_PROVIDER_SYMBOL_SESSION_DERIVE "qnc_e2e_op_session_derive_v1"
#define QNC_E2E_PROVIDER_SYMBOL_PAYLOAD_ENCRYPT "qnc_e2e_op_payload_encrypt_v1"
#define QNC_E2E_PROVIDER_SYMBOL_PAYLOAD_DECRYPT "qnc_e2e_op_payload_decrypt_v1"

typedef enum qnc_e2e_status_t {
    QNC_E2E_STATUS_OK = 0,
    QNC_E2E_STATUS_REJECTED = 1,
    QNC_E2E_STATUS_INVALID_INPUT = 2,
    QNC_E2E_STATUS_UNSUPPORTED = 3
} qnc_e2e_status_t;

typedef enum qnc_e2e_operation_t {
    QNC_E2E_OPERATION_SESSION_KEY_GENERATION = 0,
    QNC_E2E_OPERATION_IDENTITY_KEY_GENERATION = 1,
    QNC_E2E_OPERATION_PUBLIC_KEY_DERIVATION = 2,
    QNC_E2E_OPERATION_AGREEMENT_SIGN = 3,
    QNC_E2E_OPERATION_AGREEMENT_VERIFY = 4,
    QNC_E2E_OPERATION_SESSION_DERIVE = 5,
    QNC_E2E_OPERATION_PAYLOAD_ENCRYPT = 6,
    QNC_E2E_OPERATION_PAYLOAD_DECRYPT = 7
} qnc_e2e_operation_t;

typedef enum qnc_e2e_material_policy_t {
    QNC_E2E_MATERIAL_HANDLE_ONLY = 0,
    QNC_E2E_MATERIAL_PUBLIC_EXPORT_ALLOWED = 1,
    QNC_E2E_MATERIAL_PAYLOAD_BYTES_ALLOWED = 2
} qnc_e2e_material_policy_t;

typedef struct qnc_e2e_buffer_view_v1 {
    const uint8_t* data;
    size_t size;
} qnc_e2e_buffer_view_v1;

typedef struct qnc_e2e_operation_input_v1 {
    qnc_e2e_operation_t operation;
    const char* suite_id;
    qnc_e2e_buffer_view_v1 primary;
    qnc_e2e_buffer_view_v1 secondary;
    qnc_e2e_buffer_view_v1 aad;
} qnc_e2e_operation_input_v1;

typedef struct qnc_e2e_operation_output_v1 {
    qnc_e2e_status_t status;
    qnc_e2e_material_policy_t material_policy;
    qnc_e2e_buffer_view_v1 public_output;
    qnc_e2e_buffer_view_v1 sealed_output;
    const char* sanitized_error_class;
} qnc_e2e_operation_output_v1;

typedef qnc_e2e_status_t (*qnc_e2e_provider_operation_v1)(
    const qnc_e2e_operation_input_v1* input,
    qnc_e2e_operation_output_v1* output);

typedef struct qnc_e2e_provider_table_v1 {
    const char* abi;
    const char* provider_id;
    uint32_t operation_count;
    qnc_e2e_provider_operation_v1 session_key_generation;
    qnc_e2e_provider_operation_v1 identity_key_generation;
    qnc_e2e_provider_operation_v1 public_key_derivation;
    qnc_e2e_provider_operation_v1 agreement_sign;
    qnc_e2e_provider_operation_v1 agreement_verify;
    qnc_e2e_provider_operation_v1 session_derive;
    qnc_e2e_provider_operation_v1 payload_encrypt;
    qnc_e2e_provider_operation_v1 payload_decrypt;
} qnc_e2e_provider_table_v1;

const qnc_e2e_provider_table_v1* qnc_e2e_openssl_provider_table_v1(void);

#ifdef __cplusplus
}
#endif

#endif // QTNETWORKCHAT_E2E_PROVIDER_API_H
