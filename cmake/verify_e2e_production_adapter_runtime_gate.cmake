if(NOT DEFINED SOURCE_DIR OR NOT EXISTS "${SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED PROBE_BUILD_DIR)
    message(FATAL_ERROR "PROBE_BUILD_DIR is required")
endif()
if(NOT DEFINED CMAKE_EXE OR NOT EXISTS "${CMAKE_EXE}")
    message(FATAL_ERROR "CMAKE_EXE is required")
endif()
if(NOT DEFINED GENERATOR_NAME OR "${GENERATOR_NAME}" STREQUAL "")
    message(FATAL_ERROR "GENERATOR_NAME is required")
endif()
if(NOT DEFINED QT_BIN_DIR OR NOT EXISTS "${QT_BIN_DIR}")
    message(FATAL_ERROR "QT_BIN_DIR is required")
endif()
execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE current_head_result
    OUTPUT_VARIABLE current_head_stdout
    ERROR_VARIABLE current_head_stderr
)
if(NOT current_head_result EQUAL 0)
    message(STATUS "Captured current HEAD output: ${current_head_stdout}\n${current_head_stderr}")
    message(FATAL_ERROR "Production adapter runtime gate should resolve the current repository HEAD")
endif()
string(STRIP "${current_head_stdout}" CURRENT_HEAD_SHA)
if("${CURRENT_HEAD_SHA}" STREQUAL "")
    message(FATAL_ERROR "Production adapter runtime gate resolved an empty current repository HEAD")
endif()
set(RELEASE_EVIDENCE_PACKAGER "${SOURCE_DIR}/scripts/package-e2e-release-evidence.ps1")
if(NOT EXISTS "${RELEASE_EVIDENCE_PACKAGER}")
    message(FATAL_ERROR "E2E release evidence packager does not exist: ${RELEASE_EVIDENCE_PACKAGER}")
endif()
if(DEFINED LINKED_RELEASE_CANDIDATE_DIR AND NOT "${LINKED_RELEASE_CANDIDATE_DIR}" STREQUAL "")
    file(REMOVE_RECURSE "${LINKED_RELEASE_CANDIDATE_DIR}")
endif()
if(DEFINED CURRENT_HEAD_LINKED_RELEASE_CANDIDATE_DIR AND NOT "${CURRENT_HEAD_LINKED_RELEASE_CANDIDATE_DIR}" STREQUAL "")
    file(REMOVE_RECURSE "${CURRENT_HEAD_LINKED_RELEASE_CANDIDATE_DIR}")
endif()

file(REMOVE_RECURSE "${PROBE_BUILD_DIR}")

set(configure_args
    -S "${SOURCE_DIR}"
    -B "${PROBE_BUILD_DIR}"
    -G "${GENERATOR_NAME}"
    -DQTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER=ON
    -DBUILD_TESTING=ON
)
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
    list(APPEND configure_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
    list(APPEND configure_args -T "${GENERATOR_TOOLSET}")
endif()
if("${GENERATOR_NAME}" MATCHES "Ninja" AND DEFINED CMAKE_MAKE_PROGRAM AND NOT "${CMAKE_MAKE_PROGRAM}" STREQUAL "")
    list(APPEND configure_args -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM})
endif()
if(DEFINED CMAKE_CXX_COMPILER AND NOT "${CMAKE_CXX_COMPILER}" STREQUAL "")
    list(APPEND configure_args -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
endif()
if(DEFINED CMAKE_PREFIX_PATH AND NOT "${CMAKE_PREFIX_PATH}" STREQUAL "")
    list(APPEND configure_args -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH})
endif()

execute_process(
    COMMAND "${CMAKE_EXE}" ${configure_args}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE configure_stderr
)
if(NOT configure_result EQUAL 0)
    file(REMOVE_RECURSE "${PROBE_BUILD_DIR}")
    message(STATUS "Captured production adapter runtime configure output: ${configure_stdout}\n${configure_stderr}")
    message(FATAL_ERROR "Production adapter runtime probe should configure")
endif()

execute_process(
    COMMAND "${CMAKE_EXE}" --build "${PROBE_BUILD_DIR}" --target e2e_production_adapter_runtime_test
        --config "${CONFIGURATION}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
)
if(NOT build_result EQUAL 0)
    file(REMOVE_RECURSE "${PROBE_BUILD_DIR}")
    message(STATUS "Captured production adapter runtime build output: ${build_stdout}\n${build_stderr}")
    message(FATAL_ERROR "Production adapter runtime probe should build e2e_production_adapter_runtime_test")
endif()

execute_process(
    COMMAND "${CMAKE_EXE}" --build "${PROBE_BUILD_DIR}" --target e2e_private_message_delivery_test
        --config "${CONFIGURATION}"
    RESULT_VARIABLE client_build_result
    OUTPUT_VARIABLE client_build_stdout
    ERROR_VARIABLE client_build_stderr
)
if(NOT client_build_result EQUAL 0)
    file(REMOVE_RECURSE "${PROBE_BUILD_DIR}")
    message(STATUS "Captured production client rotation build output: ${client_build_stdout}\n${client_build_stderr}")
    message(FATAL_ERROR "Production adapter runtime probe should build e2e_private_message_delivery_test")
endif()

execute_process(
    COMMAND "${CMAKE_EXE}" --build "${PROBE_BUILD_DIR}" --target e2e_rollout_observability_exporter
        --config "${CONFIGURATION}"
    RESULT_VARIABLE exporter_build_result
    OUTPUT_VARIABLE exporter_build_stdout
    ERROR_VARIABLE exporter_build_stderr
)
if(NOT exporter_build_result EQUAL 0)
    file(REMOVE_RECURSE "${PROBE_BUILD_DIR}")
    message(STATUS "Captured production rollout observability exporter build output: ${exporter_build_stdout}\n${exporter_build_stderr}")
    message(FATAL_ERROR "Production adapter runtime probe should build e2e_rollout_observability_exporter")
endif()

set(path_separator ":")
if(WIN32)
    set(path_separator ";")
endif()
set(probe_path "$ENV{PATH}")
if(NOT "${QT_BIN_DIR}" STREQUAL "")
    set(probe_path "${QT_BIN_DIR}${path_separator}$ENV{PATH}")
endif()
if(DEFINED OPENSSL_RUNTIME_DIR AND EXISTS "${OPENSSL_RUNTIME_DIR}")
    set(probe_path "${OPENSSL_RUNTIME_DIR}${path_separator}${probe_path}")
endif()
set(test_args --test-dir "${PROBE_BUILD_DIR}" -R "^E2EProductionAdapterRuntime$" --output-on-failure)
if(DEFINED CONFIGURATION AND NOT "${CONFIGURATION}" STREQUAL "")
    list(APPEND test_args -C "${CONFIGURATION}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=${probe_path}"
        "QTNETWORKCHAT_E2E_CRYPTO_BACKEND=production"
        "${CMAKE_CTEST_COMMAND}" ${test_args}
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_stdout
    ERROR_VARIABLE test_stderr
)

set(client_test_args --test-dir "${PROBE_BUILD_DIR}" -R "^E2EPrivateMessageDelivery$" --output-on-failure)
if(DEFINED CONFIGURATION AND NOT "${CONFIGURATION}" STREQUAL "")
    list(APPEND client_test_args -C "${CONFIGURATION}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=${probe_path}"
        "QTNETWORKCHAT_E2E_CRYPTO_BACKEND=production"
        "QTNETWORKCHAT_E2E_TEST_PRODUCTION_ROTATION_REBIND=1"
        "${CMAKE_CTEST_COMMAND}" ${client_test_args}
    RESULT_VARIABLE client_test_result
    OUTPUT_VARIABLE client_test_stdout
    ERROR_VARIABLE client_test_stderr
)

set(exporter_exe "${PROBE_BUILD_DIR}/e2e_rollout_observability_exporter")
if(WIN32)
    set(exporter_exe "${PROBE_BUILD_DIR}/e2e_rollout_observability_exporter.exe")
endif()
if(DEFINED CONFIGURATION AND NOT "${CONFIGURATION}" STREQUAL "" AND EXISTS "${PROBE_BUILD_DIR}/${CONFIGURATION}/e2e_rollout_observability_exporter.exe")
    set(exporter_exe "${PROBE_BUILD_DIR}/${CONFIGURATION}/e2e_rollout_observability_exporter.exe")
endif()
set(evidence_dir "${PROBE_BUILD_DIR}/production-rollout-observability-evidence")
file(REMOVE_RECURSE "${evidence_dir}")
file(MAKE_DIRECTORY "${evidence_dir}")
set(evidence_json "${evidence_dir}/e2e-rollout-observability.json")
set(evidence_md "${evidence_dir}/e2e-rollout-observability.md")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=${probe_path}"
        "QTNETWORKCHAT_E2E_CRYPTO_BACKEND=production"
        "QTNETWORKCHAT_E2E_REQUIRE_PRODUCTION_CRYPTO=1"
        "${exporter_exe}"
            --json "${evidence_json}"
            --markdown "${evidence_md}"
            --require-accepted
    RESULT_VARIABLE evidence_result
    OUTPUT_VARIABLE evidence_stdout
    ERROR_VARIABLE evidence_stderr
)

if(evidence_result EQUAL 0 AND EXISTS "${evidence_json}" AND EXISTS "${evidence_md}")
    file(READ "${evidence_json}" evidence_content)
    file(READ "${evidence_md}" evidence_markdown)
    string(JSON evidence_format GET "${evidence_content}" "format")
    string(JSON evidence_ok GET "${evidence_content}" "ok")
    string(JSON evidence_gate GET "${evidence_content}" "auditSummary" "releaseGate")
    string(JSON evidence_no_sensitive GET "${evidence_content}" "sensitiveExportProof" "noSensitiveExportProof")
    string(JSON evidence_raw_key GET "${evidence_content}" "sensitiveExportProof" "rawKeyExported")
    string(JSON evidence_plaintext GET "${evidence_content}" "sensitiveExportProof" "plaintextBytesExported")
    string(JSON evidence_ciphertext GET "${evidence_content}" "sensitiveExportProof" "ciphertextBytesExported")
    string(JSON evidence_ready_count GET "${evidence_content}" "productionRolloutObservability" "publicPrimitiveReadyCount")
    string(JSON evidence_blocked_count GET "${evidence_content}" "productionRolloutObservability" "publicPrimitiveBlockedCount")
    string(JSON evidence_filesystem_ready GET "${evidence_content}" "summary" "filesystemObjectRecoveryReady")
    string(JSON evidence_filesystem_gate GET "${evidence_content}" "summary" "filesystemObjectRecoveryReleaseGate")
    string(JSON evidence_offline_ready GET "${evidence_content}" "summary" "offlineObjectRecoveryReady")
    string(JSON evidence_offline_scope GET "${evidence_content}" "summary" "offlineObjectRecoveryScope")
    string(JSON evidence_offline_gate GET "${evidence_content}" "summary" "offlineObjectRecoveryReleaseGate")
    string(JSON evidence_offline_no_sensitive GET "${evidence_content}" "summary" "offlineObjectRecoveryNoSensitiveExportProof")
    set(release_ci_json "${evidence_dir}/github-windows-build-status.json")
    set(release_local_json "${evidence_dir}/local-verification-status.json")
    set(release_status_md "${evidence_dir}/automation-status.md")
    set(release_package_dir "${evidence_dir}/release-package")
    file(WRITE "${release_ci_json}" "{\n  \"format\":\"qtnetworkchat-github-windows-build-status-v1\",\n  \"headSha\":\"production-probe-head\",\n  \"status\":\"success\",\n  \"runId\":\"production-probe-run\",\n  \"source\":\"probe-fixture\",\n  \"visibility\":\"current-head-observed\",\n  \"observedRunCount\":1,\n  \"currentHeadObserved\":true,\n  \"releaseGate\":\"github-windows-build-current-head-success\",\n  \"sensitiveExportProof\":{\"noSensitiveExportProof\":true}\n}\n")
    file(WRITE "${release_local_json}" "{\n  \"format\":\"qtnetworkchat-local-verification-status-v1\",\n  \"ok\":true,\n  \"build\":{\"status\":\"passed\"},\n  \"ctest\":{\"status\":\"passed\",\"count\":3},\n  \"sensitiveExportProof\":{\"noSensitiveExportProof\":true}\n}\n")
    file(WRITE "${release_status_md}" "# Automation Status\n\n- Production probe release packaging fixture.\n")
    execute_process(
        COMMAND powershell -ExecutionPolicy Bypass -File "${RELEASE_EVIDENCE_PACKAGER}"
            -OutputDir "${release_package_dir}"
            -RolloutJsonPath "${evidence_json}"
            -RolloutMarkdownPath "${evidence_md}"
            -GitHubWindowsBuildStatusPath "${release_ci_json}"
            -LocalVerificationStatusPath "${release_local_json}"
            -AutomationStatusPath "${release_status_md}"
            -ReleaseHead "production-probe-head"
            -FailOnSensitive
        RESULT_VARIABLE release_package_result
        OUTPUT_VARIABLE release_package_stdout
        ERROR_VARIABLE release_package_stderr
    )
    if(release_package_result EQUAL 0)
        set(release_manifest_json "${release_package_dir}/e2e-release-evidence-manifest.json")
        set(release_promotion_json "${release_package_dir}/e2e-release-promotion.json")
        if(EXISTS "${release_manifest_json}" AND EXISTS "${release_promotion_json}")
            file(READ "${release_manifest_json}" release_manifest_content)
            file(READ "${release_promotion_json}" release_promotion_content)
            string(JSON release_ready GET "${release_manifest_content}" "releaseReady")
            string(JSON release_gate GET "${release_manifest_content}" "releaseGate")
            string(JSON release_production_linked_ready GET "${release_manifest_content}" "productionLinkedEvidence" "ready")
            string(JSON release_production_linked_gate GET "${release_manifest_content}" "productionLinkedEvidence" "releaseGate")
            string(JSON release_requested_backend_match GET "${release_manifest_content}" "productionLinkedEvidence" "requestedBackendMatchesAcceptance")
            string(JSON release_selected_backend_match GET "${release_manifest_content}" "productionLinkedEvidence" "selectedBackendMatchesAcceptance")
            string(JSON release_operation_counts_ready GET "${release_manifest_content}" "productionLinkedEvidence" "operationCountsReady")
            string(JSON release_sensitive_exported GET "${release_manifest_content}" "productionLinkedEvidence" "sensitiveMaterialExported")
            string(JSON release_promotion_ready GET "${release_promotion_content}" "promotionReady")
            string(JSON release_promotion_promoted GET "${release_promotion_content}" "promoted")
            string(JSON release_promotion_gate GET "${release_promotion_content}" "releaseGate")
            string(JSON release_promotion_blocker_count LENGTH "${release_promotion_content}" "blockers")
            string(JSON release_probe_fixture GET "${release_manifest_content}" "probeFixture")
            string(JSON release_eligible GET "${release_manifest_content}" "releaseEligible")
            string(JSON release_eligibility_gate GET "${release_manifest_content}" "releaseEligibilityGate")
            string(JSON release_promotion_probe_fixture GET "${release_promotion_content}" "probeFixture")
            string(JSON release_promotion_release_eligible GET "${release_promotion_content}" "releaseEligible")
            string(JSON release_promotion_eligibility_gate GET "${release_promotion_content}" "releaseEligibilityGate")
            string(JSON release_promotion_blocker0 GET "${release_promotion_content}" "blockers" 0)
            if(DEFINED LINKED_RELEASE_CANDIDATE_DIR AND NOT "${LINKED_RELEASE_CANDIDATE_DIR}" STREQUAL "")
                file(MAKE_DIRECTORY "${LINKED_RELEASE_CANDIDATE_DIR}")
                file(COPY "${release_package_dir}/" DESTINATION "${LINKED_RELEASE_CANDIDATE_DIR}")
            endif()
        else()
            set(release_package_result 5)
            set(release_package_stderr "release-package-manifest-or-promotion-missing")
        endif()
    endif()

    if(release_package_result EQUAL 0
            AND DEFINED CURRENT_HEAD_LINKED_RELEASE_CANDIDATE_DIR
            AND NOT "${CURRENT_HEAD_LINKED_RELEASE_CANDIDATE_DIR}" STREQUAL "")
        set(current_head_candidate_status_md "${evidence_dir}/current-head-automation-status.md")
        file(WRITE "${current_head_candidate_status_md}" "# Automation Status\n\n- Current-head linked candidate fixture.\n")
        execute_process(
            COMMAND powershell -ExecutionPolicy Bypass -File "${SOURCE_DIR}/scripts/promote-e2e-linked-candidate.ps1"
                -SourceCandidateDir "${evidence_dir}"
                -OutputDir "${CURRENT_HEAD_LINKED_RELEASE_CANDIDATE_DIR}"
                -LocalVerificationStatusPath "${release_local_json}"
                -AutomationStatusPath "${current_head_candidate_status_md}"
                -ReleaseHead "${CURRENT_HEAD_SHA}"
                -GitHubWindowsBuildPolicy "disabled"
                -FailOnSensitive
            RESULT_VARIABLE current_head_candidate_result
            OUTPUT_VARIABLE current_head_candidate_stdout
            ERROR_VARIABLE current_head_candidate_stderr
        )
        if(NOT current_head_candidate_result EQUAL 0)
            set(release_package_result ${current_head_candidate_result})
            set(release_package_stdout "${release_package_stdout}\n${current_head_candidate_stdout}")
            set(release_package_stderr "${release_package_stderr}\n${current_head_candidate_stderr}")
        endif()
    endif()
elseif(evidence_result EQUAL 0)
    set(evidence_result 4)
    set(evidence_stderr "production-rollout-observability-evidence-files-missing")
endif()

file(REMOVE_RECURSE "${PROBE_BUILD_DIR}")

if(NOT test_result EQUAL 0)
    message(STATUS "Captured production adapter runtime test output: ${test_stdout}\n${test_stderr}")
    message(FATAL_ERROR "Production adapter runtime should pass OpenSSL provider dispatch under production backend selection")
endif()

if(NOT client_test_result EQUAL 0)
    message(STATUS "Captured production client rotation test output: ${client_test_stdout}\n${client_test_stderr}")
    message(FATAL_ERROR "Production client rotation should rebind identity, re-pin multiple peers, derive independent production sessions, recover production identities/trust after restart, and deliver encrypted text/file payloads under linked provider gates")
endif()

if(NOT evidence_result EQUAL 0)
    message(STATUS "Captured production rollout observability evidence output: ${evidence_stdout}\n${evidence_stderr}")
    message(FATAL_ERROR "Production rollout observability evidence should be accepted under linked provider gates")
endif()
if(NOT release_package_result EQUAL 0)
    message(STATUS "Captured production release evidence package output: ${release_package_stdout}\n${release_package_stderr}")
    message(FATAL_ERROR "Production-linked rollout evidence should package into a promoted E2E release artifact when CI and local verification are ready")
endif()
if(NOT evidence_format STREQUAL "qtnetworkchat-e2e-production-rollout-observability-evidence-v1"
    OR NOT evidence_ok
    OR NOT evidence_gate STREQUAL "production-rollout-observability-ready"
    OR NOT evidence_no_sensitive
    OR evidence_raw_key
    OR evidence_plaintext
    OR evidence_ciphertext
    OR NOT evidence_ready_count EQUAL 8
    OR NOT evidence_blocked_count EQUAL 0
    OR NOT evidence_filesystem_ready
    OR NOT evidence_filesystem_gate STREQUAL "e2e-filesystem-object-ciphertext-readback-ready"
    OR NOT evidence_offline_ready
    OR NOT evidence_offline_no_sensitive)
    message(FATAL_ERROR "Production rollout observability evidence should be sanitized, accepted, keep filesystem object recovery ready, and keep reviewed offline recovery ready")
endif()
if(NOT evidence_offline_scope STREQUAL "offline-ciphertext-readback"
    OR NOT evidence_offline_gate STREQUAL "e2e-offline-ciphertext-readback-reviewed-opt-in")
    message(FATAL_ERROR "Production rollout observability evidence should expose reviewed offline ciphertext readback as an explicit opt-in gate")
endif()
if(release_ready
    OR NOT release_gate STREQUAL "blocked-release-artifact-probe-fixture"
    OR NOT release_production_linked_ready
    OR NOT release_production_linked_gate STREQUAL "production-linked-rollout-ready"
    OR NOT release_requested_backend_match
    OR NOT release_selected_backend_match
    OR NOT release_operation_counts_ready
    OR release_sensitive_exported
    OR NOT release_probe_fixture
    OR release_eligible
    OR NOT release_eligibility_gate STREQUAL "not-release-eligible-probe-fixture"
    OR release_promotion_ready
    OR release_promotion_promoted
    OR NOT release_promotion_gate STREQUAL "blocked-e2e-release-artifact-promotion"
    OR NOT release_promotion_probe_fixture
    OR release_promotion_release_eligible
    OR NOT release_promotion_eligibility_gate STREQUAL "not-release-eligible-probe-fixture"
    OR NOT release_promotion_blocker_count EQUAL 1
    OR NOT release_promotion_blocker0 STREQUAL "release-artifact-probe-fixture")
    message(FATAL_ERROR "Production-linked probe fixture package should prove accepted rollout evidence but remain ineligible for final promotion")
endif()
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
    string(FIND "${evidence_content}\n${evidence_markdown}" "${forbidden_text}" forbidden_index)
    if(NOT forbidden_index EQUAL -1)
        message(FATAL_ERROR "Production rollout observability evidence leaked forbidden text: ${forbidden_text}")
    endif()
endforeach()

message(STATUS "E2E production adapter runtime provider dispatch, multi-peer connected client rotation, restart recovery, and rollout observability evidence gate verified")
