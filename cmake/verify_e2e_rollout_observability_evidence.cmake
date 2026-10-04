if(NOT DEFINED EXPORTER_EXE OR NOT EXISTS "${EXPORTER_EXE}")
    message(FATAL_ERROR "EXPORTER_EXE does not exist: ${EXPORTER_EXE}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/e2e_rollout_observability_evidence")
set(EVIDENCE_JSON "${TEMP_DIR}/e2e-rollout-observability.json")
set(EVIDENCE_MD "${TEMP_DIR}/e2e-rollout-observability.md")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

if(WIN32 AND DEFINED QT_BIN_DIR AND EXISTS "${QT_BIN_DIR}")
    set(ENV{PATH} "${QT_BIN_DIR};$ENV{PATH}")
endif()
if(WIN32 AND NOT DEFINED OPENSSL_RUNTIME_DIR AND EXISTS "D:/Qt/Tools/mingw1310_64/opt/bin")
    set(OPENSSL_RUNTIME_DIR "D:/Qt/Tools/mingw1310_64/opt/bin")
endif()
if(WIN32 AND DEFINED OPENSSL_RUNTIME_DIR AND EXISTS "${OPENSSL_RUNTIME_DIR}")
    set(ENV{PATH} "${OPENSSL_RUNTIME_DIR};$ENV{PATH}")
endif()

execute_process(
    COMMAND "${EXPORTER_EXE}"
        --json "${EVIDENCE_JSON}"
        --markdown "${EVIDENCE_MD}"
        --print-json
    RESULT_VARIABLE exporter_result
    OUTPUT_VARIABLE exporter_stdout
    ERROR_VARIABLE exporter_stderr
)
if(NOT exporter_result EQUAL 0)
    message(STATUS "Captured E2E rollout observability exporter output: ${exporter_stdout}\n${exporter_stderr}")
    message(FATAL_ERROR "E2E rollout observability exporter should write fail-closed evidence")
endif()
foreach(expected_file IN ITEMS "${EVIDENCE_JSON}" "${EVIDENCE_MD}")
    if(NOT EXISTS "${expected_file}")
        message(FATAL_ERROR "Expected rollout observability evidence file missing: ${expected_file}")
    endif()
endforeach()

file(READ "${EVIDENCE_JSON}" evidence_content)
file(READ "${EVIDENCE_MD}" markdown_content)

string(JSON evidence_format GET "${evidence_content}" "format")
string(JSON evidence_ok GET "${evidence_content}" "ok")
string(JSON evidence_status GET "${evidence_content}" "status")
string(JSON evidence_gate GET "${evidence_content}" "auditSummary" "releaseGate")
string(JSON acceptance_ok GET "${evidence_content}" "productionAcceptanceSummary" "accepted")
string(JSON rollout_ok GET "${evidence_content}" "productionRolloutObservability" "accepted")
string(JSON rollout_no_sensitive GET "${evidence_content}" "sensitiveExportProof" "noSensitiveExportProof")
string(JSON rollout_raw_key GET "${evidence_content}" "sensitiveExportProof" "rawKeyExported")
string(JSON rollout_private_material GET "${evidence_content}" "sensitiveExportProof" "privateMaterialExported")
string(JSON rollout_session_secret GET "${evidence_content}" "sensitiveExportProof" "sessionSecretExported")
string(JSON rollout_plaintext GET "${evidence_content}" "sensitiveExportProof" "plaintextBytesExported")
string(JSON rollout_ciphertext GET "${evidence_content}" "sensitiveExportProof" "ciphertextBytesExported")
string(JSON filesystem_ready GET "${evidence_content}" "summary" "filesystemObjectRecoveryReady")
string(JSON filesystem_gate GET "${evidence_content}" "summary" "filesystemObjectRecoveryReleaseGate")
string(JSON filesystem_no_sensitive GET "${evidence_content}" "summary" "filesystemObjectRecoveryNoSensitiveExportProof")
string(JSON offline_ready GET "${evidence_content}" "summary" "offlineObjectRecoveryReady")
string(JSON offline_scope GET "${evidence_content}" "summary" "offlineObjectRecoveryScope")
string(JSON offline_gate GET "${evidence_content}" "summary" "offlineObjectRecoveryReleaseGate")
string(JSON offline_capture_policy GET "${evidence_content}" "summary" "offlineObjectRecoveryCapturePolicy")
string(JSON offline_no_sensitive GET "${evidence_content}" "summary" "offlineObjectRecoveryNoSensitiveExportProof")
string(JSON audit_focus0 GET "${evidence_content}" "auditSummary" "auditFocus" 0)
string(JSON evidence_bundle0 GET "${evidence_content}" "auditSummary" "evidenceBundle" 0)

if(NOT evidence_format STREQUAL "qtnetworkchat-e2e-production-rollout-observability-evidence-v1")
    message(FATAL_ERROR "Unexpected rollout observability evidence format: ${evidence_format}")
endif()
if(EXPECT_REVIEWED_PROVIDER)
    set(expected_status "ready")
    set(expected_gate "production-rollout-observability-ready")
    set(expected_proof "true")
    if(NOT evidence_ok OR NOT acceptance_ok OR NOT rollout_ok OR NOT rollout_no_sensitive)
        message(FATAL_ERROR "Reviewed linked provider must provide accepted, sanitized rollout evidence")
    endif()
else()
    set(expected_status "blocked")
    set(expected_proof "false")
    if(EXPECT_ADAPTER_LINKED)
        set(expected_gate "production-rollout-observability-blocked-not-ready")
    else()
        set(expected_gate "production-rollout-observability-blocked-not-linked")
    endif()
    if(evidence_ok OR acceptance_ok OR rollout_ok OR rollout_no_sensitive)
        message(FATAL_ERROR "Unavailable provider must not claim acceptance or sensitive-export proof")
    endif()
endif()
if(NOT evidence_status STREQUAL expected_status OR NOT evidence_gate STREQUAL expected_gate)
    message(FATAL_ERROR "Unexpected rollout status/gate: ${evidence_status}/${evidence_gate}; expected ${expected_status}/${expected_gate}")
endif()
if(rollout_raw_key OR rollout_private_material OR rollout_session_secret OR rollout_plaintext OR rollout_ciphertext)
    message(FATAL_ERROR "Rollout evidence must not export sensitive material flags as true")
endif()
if(NOT filesystem_ready
    OR NOT filesystem_gate STREQUAL "e2e-filesystem-object-ciphertext-readback-ready"
    OR NOT filesystem_no_sensitive)
    message(FATAL_ERROR "Filesystem object ciphertext readback should be ready and sanitized")
endif()
if(NOT offline_ready
    OR NOT offline_no_sensitive)
    message(FATAL_ERROR "Reviewed offline object ciphertext readback should be ready and sanitized behind its opt-in gate")
endif()
if(NOT offline_scope STREQUAL "offline-ciphertext-readback"
    OR NOT offline_gate STREQUAL "e2e-offline-ciphertext-readback-reviewed-opt-in"
    OR NOT offline_capture_policy STREQUAL "safe-object-token-hash-size-envelope-header-session-metadata-only")
    message(FATAL_ERROR "Offline/object recovery should expose reviewed offline ciphertext readback as an explicit opt-in gate")
endif()
if(NOT audit_focus0 STREQUAL "production-crypto-acceptance")
    message(FATAL_ERROR "Rollout audit focus should include production crypto acceptance")
endif()
if(NOT evidence_bundle0 STREQUAL "e2e-rollout-observability.json")
    message(FATAL_ERROR "Rollout evidence bundle should name the JSON artifact")
endif()

foreach(expected_text IN ITEMS
    "QtNetworkChat E2E Production Rollout Observability Evidence"
    "Status: `${expected_status}`"
    "Release gate: `${expected_gate}`"
    "Sensitive export proof: noSensitiveExport=`${expected_proof}`, rawKey=`false`, privateMaterial=`false`, sessionSecret=`false`, plaintext=`false`, ciphertext=`false`"
    "Filesystem object recovery ready: `true`"
    "Filesystem object recovery gate: `e2e-filesystem-object-ciphertext-readback-ready`"
    "Offline/object recovery ready: `true`"
    "Offline/object recovery scope: `offline-ciphertext-readback`"
    "Offline/object recovery gate: `e2e-offline-ciphertext-readback-reviewed-opt-in`"
    "Offline/object recovery capture policy: `safe-object-token-hash-size-envelope-header-session-metadata-only`"
)
    string(FIND "${markdown_content}" "${expected_text}" expected_index)
    if(expected_index EQUAL -1)
        message(FATAL_ERROR "Rollout Markdown evidence missing expected text: ${expected_text}")
    endif()
endforeach()

foreach(forbidden_text IN ITEMS
    "privateKey"
    "sessionKey"
    "plaintextBytes="
    "ciphertextBytes="
    "ghp_"
    "github_pat_"
    "Authorization:"
    "Credential="
    "Signature="
)
    string(FIND "${evidence_content}\n${markdown_content}" "${forbidden_text}" forbidden_index)
    if(NOT forbidden_index EQUAL -1)
        message(FATAL_ERROR "Rollout evidence leaked forbidden text: ${forbidden_text}")
    endif()
endforeach()

message(STATUS "E2E rollout observability fail-closed evidence verified")
