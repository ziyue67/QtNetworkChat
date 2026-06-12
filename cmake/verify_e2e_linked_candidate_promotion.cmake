if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH is required")
endif()
get_filename_component(REPO_DIR "${SCRIPT_PATH}/../.." ABSOLUTE)
set(PACKAGER_PATH "${REPO_DIR}/scripts/package-e2e-release-evidence.ps1")
if(NOT EXISTS "${PACKAGER_PATH}")
    message(FATAL_ERROR "E2E release evidence packager is required")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/tmp-e2e-linked-candidate-promotion")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(SOURCE_DIR "${TEMP_DIR}/source-candidate")
set(SOURCE_EVIDENCE_DIR "${SOURCE_DIR}/e2e-release-evidence")
set(OUTPUT_DIR "${TEMP_DIR}/current-head-candidate")
set(SOURCE_BUILD_DIR "${TEMP_DIR}/source-build")
set(SOURCE_BUILD_EVIDENCE_DIR "${SOURCE_BUILD_DIR}/e2e_rollout_observability_evidence")
set(OUTPUT_BUILD_DIR "${TEMP_DIR}/current-head-candidate-from-build")
file(MAKE_DIRECTORY "${SOURCE_EVIDENCE_DIR}")
file(MAKE_DIRECTORY "${SOURCE_BUILD_EVIDENCE_DIR}")

set(ROLLOUT_JSON "${SOURCE_EVIDENCE_DIR}/e2e-rollout-observability.json")
set(ROLLOUT_MD "${SOURCE_EVIDENCE_DIR}/e2e-rollout-observability.md")
set(CI_JSON "${TEMP_DIR}/github-windows-build-status.json")
set(LOCAL_JSON "${TEMP_DIR}/local-verification-status.json")
set(AUTOMATION_MD "${TEMP_DIR}/automation-status.md")

file(WRITE "${ROLLOUT_JSON}" "{\n"
"  \"format\":\"qtnetworkchat-e2e-production-rollout-observability-evidence-v1\",\n"
"  \"status\":\"ready\",\n"
"  \"ok\":true,\n"
"  \"summary\":{\"readiness\":\"verified\"},\n"
"  \"auditSummary\":{\"releaseGate\":\"production-rollout-observability-ready\"},\n"
"  \"productionAcceptanceSummary\":{\n"
"    \"accepted\":true,\n"
"    \"linked\":true,\n"
"    \"productionReady\":true,\n"
"    \"releaseGate\":\"production-crypto-accepted\",\n"
"    \"backendId\":\"openssl-reviewed-adapter-v1\",\n"
"    \"providerId\":\"openssl-reviewed-provider-v1\",\n"
"    \"requiredOperationCount\":8,\n"
"    \"availableOperationCount\":8\n"
"  },\n"
"  \"productionRolloutObservability\":{\n"
"    \"accepted\":true,\n"
"    \"linked\":true,\n"
"    \"productionReady\":true,\n"
"    \"releaseGate\":\"production-rollout-observability-ready\",\n"
"    \"backendId\":\"openssl-reviewed-adapter-v1\",\n"
"    \"providerId\":\"openssl-reviewed-provider-v1\",\n"
"    \"releaseRunObservable\":true,\n"
"    \"noSensitiveExportProof\":true,\n"
"    \"requiredOperationCount\":8,\n"
"    \"publicPrimitiveReadyCount\":8,\n"
"    \"materialExportProofCount\":8,\n"
"    \"outputShapeProofCount\":8,\n"
"    \"rawKeyExported\":false,\n"
"    \"privateMaterialExported\":false,\n"
"    \"sessionSecretExported\":false,\n"
"    \"plaintextBytesExported\":false,\n"
"    \"ciphertextBytesExported\":false\n"
"  },\n"
"  \"releaseRun\":{\n"
"    \"persisted\":true,\n"
"    \"productionRequired\":true,\n"
"    \"requestedBackendId\":\"openssl-reviewed-adapter-v1\",\n"
"    \"selectedBackendId\":\"openssl-reviewed-adapter-v1\"\n"
"  },\n"
"  \"sensitiveExportProof\":{\n"
"    \"noSensitiveExportProof\":true,\n"
"    \"sensitiveFieldsSuppressed\":true,\n"
"    \"rawKeyExported\":false,\n"
"    \"privateMaterialExported\":false,\n"
"    \"sessionSecretExported\":false,\n"
"    \"plaintextBytesExported\":false,\n"
"    \"ciphertextBytesExported\":false\n"
"  }\n"
"}\n")
file(WRITE "${ROLLOUT_MD}" "# Linked rollout evidence\n\n- Release gate: `production-rollout-observability-ready`\n")
file(COPY "${ROLLOUT_JSON}" DESTINATION "${SOURCE_BUILD_EVIDENCE_DIR}")
file(COPY "${ROLLOUT_MD}" DESTINATION "${SOURCE_BUILD_EVIDENCE_DIR}")
file(WRITE "${CI_JSON}" "{\n"
"  \"format\":\"qtnetworkchat-github-windows-build-status-v1\",\n"
"  \"headSha\":\"not-required\",\n"
"  \"status\":\"disabled-by-policy\",\n"
"  \"runId\":\"not-required\",\n"
"  \"source\":\"automation-policy\",\n"
"  \"visibility\":\"not-required\",\n"
"  \"observedRunCount\":0,\n"
"  \"currentHeadObserved\":\"not-required\",\n"
"  \"headMatchesReleaseHead\":\"not-required\",\n"
"  \"externalBlocker\":\"waived-by-policy\",\n"
"  \"releaseGate\":\"not-required\",\n"
"  \"sensitiveExportProof\":{\"noSensitiveExportProof\":true}\n"
"}\n")
file(WRITE "${LOCAL_JSON}" "{\n"
"  \"format\":\"qtnetworkchat-local-verification-status-v1\",\n"
"  \"ok\":true,\n"
"  \"build\":{\"status\":\"passed\"},\n"
"  \"ctest\":{\"status\":\"passed\",\"count\":73},\n"
"  \"sensitiveExportProof\":{\"noSensitiveExportProof\":true}\n"
"}\n")
file(WRITE "${AUTOMATION_MD}" "# Automation Status\n\n- sanitized sample\n")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -SourceCandidateDir "${SOURCE_DIR}"
        -OutputDir "${OUTPUT_DIR}"
        -GitHubWindowsBuildStatusPath "${CI_JSON}"
        -LocalVerificationStatusPath "${LOCAL_JSON}"
        -AutomationStatusPath "${AUTOMATION_MD}"
        -ReleaseHead "current-linked-head"
        -FailOnSensitive
    RESULT_VARIABLE promote_result
    OUTPUT_VARIABLE promote_stdout
    ERROR_VARIABLE promote_stderr
)
if(NOT promote_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(STATUS "Promote linked candidate output: ${promote_stdout}\n${promote_stderr}")
    message(FATAL_ERROR "Linked candidate promotion should generate current-head evidence")
endif()

set(MANIFEST_JSON "${OUTPUT_DIR}/e2e-release-evidence-manifest.json")
set(PROMOTION_JSON "${OUTPUT_DIR}/e2e-release-promotion.json")
if(NOT EXISTS "${MANIFEST_JSON}" OR NOT EXISTS "${PROMOTION_JSON}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Linked candidate promotion did not write manifest and promotion artifacts")
endif()
file(READ "${MANIFEST_JSON}" manifest_content)
file(READ "${PROMOTION_JSON}" promotion_content)
string(JSON target_head GET "${manifest_content}" "targetReleaseHead")
string(JSON release_ready GET "${manifest_content}" "releaseReady")
string(JSON release_gate GET "${manifest_content}" "releaseGate")
string(JSON probe_fixture GET "${manifest_content}" "probeFixture")
string(JSON release_eligible GET "${manifest_content}" "releaseEligible")
string(JSON production_linked_ready GET "${manifest_content}" "productionLinkedEvidence" "ready")
string(JSON production_linked_gate GET "${manifest_content}" "productionLinkedEvidence" "releaseGate")
string(JSON ci_head_match GET "${manifest_content}" "ci" "headMatchesReleaseHead")
string(JSON ci_current_observed GET "${manifest_content}" "ci" "currentHeadObserved")
string(JSON ci_status GET "${manifest_content}" "ci" "status")
string(JSON ci_visibility GET "${manifest_content}" "ci" "visibility")
string(JSON promotion_ready GET "${promotion_content}" "promotionReady")
string(JSON promotion_promoted GET "${promotion_content}" "promoted")
string(JSON promotion_blocker_count LENGTH "${promotion_content}" "blockers")
string(JSON promotion_ci_status GET "${promotion_content}" "ciStatus")
if(NOT target_head STREQUAL "current-linked-head"
        OR NOT release_ready
        OR NOT release_gate STREQUAL "ready-local-verification-only"
        OR probe_fixture
        OR NOT release_eligible
        OR NOT production_linked_ready
        OR NOT production_linked_gate STREQUAL "production-linked-rollout-ready"
        OR NOT ci_head_match STREQUAL "not-required"
        OR NOT ci_current_observed STREQUAL "not-required"
        OR NOT ci_status STREQUAL "disabled-by-policy"
        OR NOT ci_visibility STREQUAL "not-required"
        OR NOT promotion_ready
        OR NOT promotion_promoted
        OR NOT promotion_ci_status STREQUAL "disabled-by-policy"
        OR NOT promotion_blocker_count EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Linked candidate should be current-head, production-linked, non-probe, and ready via local verification when Windows Build is disabled by policy")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -SourceCandidateDir "${SOURCE_BUILD_DIR}"
        -OutputDir "${OUTPUT_BUILD_DIR}"
        -GitHubWindowsBuildStatusPath "${CI_JSON}"
        -LocalVerificationStatusPath "${LOCAL_JSON}"
        -AutomationStatusPath "${AUTOMATION_MD}"
        -ReleaseHead "current-linked-head-from-build"
        -FailOnSensitive
    RESULT_VARIABLE promote_build_result
    OUTPUT_VARIABLE promote_build_stdout
    ERROR_VARIABLE promote_build_stderr
)
if(NOT promote_build_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(STATUS "Promote linked build output: ${promote_build_stdout}\n${promote_build_stderr}")
    message(FATAL_ERROR "Linked candidate promotion should also accept direct linked-build rollout evidence")
endif()

set(MANIFEST_BUILD_JSON "${OUTPUT_BUILD_DIR}/e2e-release-evidence-manifest.json")
set(PROMOTION_BUILD_JSON "${OUTPUT_BUILD_DIR}/e2e-release-promotion.json")
if(NOT EXISTS "${MANIFEST_BUILD_JSON}" OR NOT EXISTS "${PROMOTION_BUILD_JSON}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Linked build promotion did not write manifest and promotion artifacts")
endif()
file(READ "${MANIFEST_BUILD_JSON}" manifest_build_content)
file(READ "${PROMOTION_BUILD_JSON}" promotion_build_content)
string(JSON build_target_head GET "${manifest_build_content}" "targetReleaseHead")
string(JSON build_release_ready GET "${manifest_build_content}" "releaseReady")
string(JSON build_release_gate GET "${manifest_build_content}" "releaseGate")
string(JSON build_release_eligible GET "${manifest_build_content}" "releaseEligible")
string(JSON build_probe_fixture GET "${manifest_build_content}" "probeFixture")
string(JSON build_input_count GET "${manifest_build_content}" "inputCount")
string(JSON build_promotion_ready GET "${promotion_build_content}" "promotionReady")
string(JSON build_promotion_promoted GET "${promotion_build_content}" "promoted")
string(JSON build_promotion_gate GET "${promotion_build_content}" "releaseGate")
if(NOT build_target_head STREQUAL "current-linked-head-from-build"
        OR NOT build_release_ready
        OR NOT build_release_gate STREQUAL "ready-local-verification-only"
        OR NOT build_release_eligible
        OR build_probe_fixture
        OR NOT build_input_count EQUAL 5
        OR NOT build_promotion_ready
        OR NOT build_promotion_promoted
        OR NOT build_promotion_gate STREQUAL "ready-local-verification-only")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Direct linked-build rollout evidence should promote into a ready current-head candidate")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "E2E linked current-head candidate promotion gate verified")
