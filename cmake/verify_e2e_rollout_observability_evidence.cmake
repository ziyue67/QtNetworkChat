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
string(JSON audit_focus0 GET "${evidence_content}" "auditSummary" "auditFocus" 0)
string(JSON evidence_bundle0 GET "${evidence_content}" "auditSummary" "evidenceBundle" 0)

if(NOT evidence_format STREQUAL "qtnetworkchat-e2e-production-rollout-observability-evidence-v1")
    message(FATAL_ERROR "Unexpected rollout observability evidence format: ${evidence_format}")
endif()
if(evidence_ok OR NOT evidence_status STREQUAL "blocked")
    message(FATAL_ERROR "Default rollout observability evidence should fail closed")
endif()
if(NOT evidence_gate STREQUAL "production-rollout-observability-blocked-not-linked")
    message(FATAL_ERROR "Default rollout observability gate should be blocked-not-linked, got ${evidence_gate}")
endif()
if(acceptance_ok OR rollout_ok OR rollout_no_sensitive)
    message(FATAL_ERROR "Default rollout evidence should not claim acceptance or no-sensitive proof")
endif()
if(rollout_raw_key OR rollout_private_material OR rollout_session_secret OR rollout_plaintext OR rollout_ciphertext)
    message(FATAL_ERROR "Rollout evidence must not export sensitive material flags as true")
endif()
if(NOT filesystem_ready
    OR NOT filesystem_gate STREQUAL "e2e-filesystem-object-ciphertext-readback-ready"
    OR NOT filesystem_no_sensitive)
    message(FATAL_ERROR "Filesystem object ciphertext readback should be ready and sanitized")
endif()
if(offline_ready)
    message(FATAL_ERROR "S3/offline object recovery should remain explicitly not ready")
endif()
if(NOT offline_scope STREQUAL "s3-offline-auto-readback")
    message(FATAL_ERROR "Offline/object recovery scope should stay limited to S3/offline auto-readback")
endif()
if(NOT audit_focus0 STREQUAL "production-crypto-acceptance")
    message(FATAL_ERROR "Rollout audit focus should include production crypto acceptance")
endif()
if(NOT evidence_bundle0 STREQUAL "e2e-rollout-observability.json")
    message(FATAL_ERROR "Rollout evidence bundle should name the JSON artifact")
endif()

foreach(expected_text IN ITEMS
    "QtNetworkChat E2E Production Rollout Observability Evidence"
    "Status: `blocked`"
    "Release gate: `production-rollout-observability-blocked-not-linked`"
    "Sensitive export proof: noSensitiveExport=`false`, rawKey=`false`, privateMaterial=`false`, sessionSecret=`false`, plaintext=`false`, ciphertext=`false`"
    "Filesystem object recovery ready: `true`"
    "Filesystem object recovery gate: `e2e-filesystem-object-ciphertext-readback-ready`"
    "Offline/object recovery ready: `false`"
    "Offline/object recovery scope: `s3-offline-auto-readback`"
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
