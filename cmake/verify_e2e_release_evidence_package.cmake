if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/e2e_release_evidence_package")
set(OUTPUT_DIR "${TEMP_DIR}/out")
set(EXTRACT_DIR "${TEMP_DIR}/extract")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(ROLLOUT_JSON "${TEMP_DIR}/e2e-rollout-observability.json")
set(ROLLOUT_MD "${TEMP_DIR}/e2e-rollout-observability.md")
set(CI_JSON "${TEMP_DIR}/github-windows-build-status.json")
set(LOCAL_JSON "${TEMP_DIR}/local-verification-status.json")
set(AUTO_MD "${TEMP_DIR}/automation-status.md")

file(WRITE "${ROLLOUT_JSON}" "{\n  \"format\":\"qtnetworkchat-e2e-production-rollout-observability-evidence-v1\",\n  \"status\":\"blocked\",\n  \"ok\":false,\n  \"summary\":{\"readiness\":\"blocked\"},\n  \"auditSummary\":{\"releaseGate\":\"production-rollout-observability-blocked-not-linked\"},\n  \"sensitiveExportProof\":{\"noSensitiveExportProof\":false,\"sensitiveFieldsSuppressed\":true}\n}\n")
file(WRITE "${ROLLOUT_MD}" "# E2E Rollout\n\n- Status: `blocked`\n")
file(WRITE "${CI_JSON}" "{\n  \"format\":\"qtnetworkchat-github-windows-build-status-v1\",\n  \"headSha\":\"abc123\",\n  \"status\":\"external-visibility-stale\",\n  \"runId\":\"\",\n  \"source\":\"json-artifact\",\n  \"visibility\":\"head-not-observed\",\n  \"observedRunCount\":20,\n  \"sensitiveExportProof\":{\"noSensitiveExportProof\":true}\n}\n")
file(WRITE "${LOCAL_JSON}" "{\n  \"format\":\"qtnetworkchat-local-verification-status-v1\",\n  \"ok\":true,\n  \"build\":{\"status\":\"passed\"},\n  \"ctest\":{\"status\":\"passed\",\"count\":70},\n  \"sensitiveExportProof\":{\"noSensitiveExportProof\":true}\n}\n")
file(WRITE "${AUTO_MD}" "# Automation Status\n\n- GitHub Windows Build: `external-visibility-stale`\n")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}"
        -RolloutJsonPath "${ROLLOUT_JSON}"
        -RolloutMarkdownPath "${ROLLOUT_MD}"
        -GitHubWindowsBuildStatusPath "${CI_JSON}"
        -LocalVerificationStatusPath "${LOCAL_JSON}"
        -AutomationStatusPath "${AUTO_MD}"
        -ReleaseHead "abc123"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output
)

if(NOT output STREQUAL "")
    message(STATUS "${output}")
endif()
if(NOT error_output STREQUAL "")
    message(STATUS "${error_output}")
endif()
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "package-e2e-release-evidence.ps1 exited with code ${result}")
endif()

set(PACKAGE_PATH "${OUTPUT_DIR}/e2e-release-evidence.zip")
set(MANIFEST_PATH "${OUTPUT_DIR}/e2e-release-evidence-manifest.json")
set(PROMOTION_PATH "${OUTPUT_DIR}/e2e-release-promotion.json")
if(NOT EXISTS "${PACKAGE_PATH}" OR NOT EXISTS "${MANIFEST_PATH}" OR NOT EXISTS "${PROMOTION_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected E2E release evidence package, manifest, and promotion decision were not created")
endif()

file(READ "${MANIFEST_PATH}" manifest_content)
file(READ "${PROMOTION_PATH}" promotion_content)
string(JSON format GET "${manifest_content}" "format")
string(JSON ok GET "${manifest_content}" "ok")
string(JSON release_ready GET "${manifest_content}" "releaseReady")
string(JSON release_gate GET "${manifest_content}" "releaseGate")
string(JSON promotion_packaged_as GET "${manifest_content}" "promotionPackagedAs")
string(JSON promotion_embedded_gate GET "${manifest_content}" "promotion" "releaseGate")
string(JSON promotion_embedded_ready GET "${manifest_content}" "promotion" "promotionReady")
string(JSON promotion_embedded_promoted GET "${manifest_content}" "promotion" "promoted")
string(JSON promotion_embedded_blocker0 GET "${manifest_content}" "promotion" "blockers" 0)
string(JSON promotion_embedded_blocker1 GET "${manifest_content}" "promotion" "blockers" 1)
string(JSON production_linked_ready GET "${manifest_content}" "productionLinkedEvidence" "ready")
string(JSON production_linked_gate GET "${manifest_content}" "productionLinkedEvidence" "releaseGate")
string(JSON production_linked_blocker0 GET "${manifest_content}" "productionLinkedEvidence" "blockers" 0)
string(JSON production_linked_backend_match GET "${manifest_content}" "productionLinkedEvidence" "acceptedBackendMatchesRollout")
string(JSON production_linked_request_match GET "${manifest_content}" "productionLinkedEvidence" "requestedBackendMatchesAcceptance")
string(JSON production_linked_select_match GET "${manifest_content}" "productionLinkedEvidence" "selectedBackendMatchesAcceptance")
string(JSON production_linked_counts_ready GET "${manifest_content}" "productionLinkedEvidence" "operationCountsReady")
string(JSON production_linked_sensitive_exported GET "${manifest_content}" "productionLinkedEvidence" "sensitiveMaterialExported")
string(JSON package_path GET "${manifest_content}" "packagePath")
string(JSON package_sha256 GET "${manifest_content}" "packageSha256")
string(JSON staging_dir GET "${manifest_content}" "stagingDir")
string(JSON manifest_packaged_as GET "${manifest_content}" "manifestPackagedAs")
string(JSON manifest_embedded GET "${manifest_content}" "manifestEmbedded")
string(JSON input_count GET "${manifest_content}" "inputCount")
string(JSON input0_source_name GET "${manifest_content}" "inputs" 0 "sourceName")
string(JSON input0_sha256 GET "${manifest_content}" "inputs" 0 "sha256")
string(JSON ci_status GET "${manifest_content}" "ci" "status")
string(JSON ci_head_sha GET "${manifest_content}" "ci" "headSha")
string(JSON ci_visibility GET "${manifest_content}" "ci" "visibility")
string(JSON ci_head_matches_release_head GET "${manifest_content}" "ci" "headMatchesReleaseHead")
string(JSON probe_fixture GET "${manifest_content}" "probeFixture")
string(JSON release_eligible GET "${manifest_content}" "releaseEligible")
string(JSON release_eligibility_gate GET "${manifest_content}" "releaseEligibilityGate")
string(JSON target_release_head GET "${manifest_content}" "targetReleaseHead")
string(JSON local_ctest_count GET "${manifest_content}" "localVerification" "ctestCount")
string(JSON proof_no_sensitive GET "${manifest_content}" "sensitiveExportProof" "noSensitiveExportProof")
string(JSON promotion_format GET "${promotion_content}" "format")
string(JSON promotion_ready GET "${promotion_content}" "promotionReady")
string(JSON promotion_promoted GET "${promotion_content}" "promoted")
string(JSON promotion_gate GET "${promotion_content}" "releaseGate")
string(JSON promotion_evidence_gate GET "${promotion_content}" "evidenceReleaseGate")
string(JSON promotion_target_release_head GET "${promotion_content}" "targetReleaseHead")
string(JSON promotion_ci_head_sha GET "${promotion_content}" "ciHeadSha")
string(JSON promotion_ci_head_matches_release_head GET "${promotion_content}" "ciHeadMatchesReleaseHead")
string(JSON promotion_probe_fixture GET "${promotion_content}" "probeFixture")
string(JSON promotion_release_eligible GET "${promotion_content}" "releaseEligible")
string(JSON promotion_release_eligibility_gate GET "${promotion_content}" "releaseEligibilityGate")
string(JSON promotion_blocker0 GET "${promotion_content}" "blockers" 0)
string(JSON promotion_blocker1 GET "${promotion_content}" "blockers" 1)
string(FIND "${manifest_content}" "${TEMP_DIR}" temp_path_index)
if(manifest_content MATCHES "\"source\"[ \t\r\n]*:[ \t\r\n]*\"[A-Za-z]:")
    set(source_absolute_path_leaked TRUE)
else()
    set(source_absolute_path_leaked FALSE)
endif()
string(LENGTH "${input0_sha256}" input0_sha256_length)
string(LENGTH "${package_sha256}" package_sha256_length)

if(NOT format STREQUAL "qtnetworkchat-e2e-release-evidence-package-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected E2E evidence manifest format: ${format}")
endif()
if(NOT ok OR release_ready OR NOT release_gate STREQUAL "blocked-ci-head-not-observed")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence should package cleanly but stay blocked by stale CI")
endif()
if(NOT promotion_format STREQUAL "qtnetworkchat-e2e-release-artifact-promotion-v1"
        OR promotion_ready
        OR promotion_promoted
        OR NOT promotion_gate STREQUAL "blocked-e2e-release-artifact-promotion"
        OR NOT promotion_evidence_gate STREQUAL "blocked-ci-head-not-observed"
        OR NOT promotion_blocker0 STREQUAL "rollout-not-ready"
        OR NOT promotion_blocker1 STREQUAL "production-linked-rollout-not-ready"
        OR NOT promotion_embedded_gate STREQUAL promotion_gate
        OR promotion_embedded_ready
        OR promotion_embedded_promoted
        OR NOT promotion_embedded_blocker0 STREQUAL "rollout-not-ready"
        OR NOT promotion_embedded_blocker1 STREQUAL "production-linked-rollout-not-ready"
        OR NOT promotion_packaged_as STREQUAL "e2e-release-promotion.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence should emit a fail-closed promotion decision with blockers")
endif()
if(production_linked_ready
        OR NOT production_linked_gate STREQUAL "blocked-production-linked-rollout-not-ready"
        OR NOT production_linked_blocker0 STREQUAL "rollout-not-ready"
        OR production_linked_backend_match
        OR production_linked_request_match
        OR production_linked_select_match
        OR production_linked_counts_ready
        OR production_linked_sensitive_exported)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence should keep production-linked promotion fail-closed for unlinked rollout evidence")
endif()
if(NOT input_count EQUAL 5
        OR NOT ci_status STREQUAL "external-visibility-stale"
        OR NOT ci_head_sha STREQUAL "abc123"
        OR NOT ci_visibility STREQUAL "head-not-observed"
        OR NOT ci_head_matches_release_head
        OR probe_fixture
        OR NOT release_eligible
        OR NOT release_eligibility_gate STREQUAL "release-eligible-current-head"
        OR NOT target_release_head STREQUAL "abc123"
        OR NOT promotion_target_release_head STREQUAL "abc123"
        OR NOT promotion_ci_head_sha STREQUAL "abc123"
        OR NOT promotion_ci_head_matches_release_head
        OR promotion_probe_fixture
        OR NOT promotion_release_eligible
        OR NOT promotion_release_eligibility_gate STREQUAL "release-eligible-current-head")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence manifest did not preserve CI readback")
endif()
if(NOT package_path STREQUAL "e2e-release-evidence.zip"
        OR NOT package_sha256_length EQUAL 64
        OR NOT package_sha256 MATCHES "^[0-9a-f]+$"
        OR NOT staging_dir STREQUAL "e2e-release-evidence"
        OR NOT manifest_packaged_as STREQUAL "manifest.json"
        OR NOT manifest_embedded
        OR NOT input0_source_name STREQUAL "e2e-rollout-observability.json"
        OR NOT input0_sha256_length EQUAL 64
        OR NOT input0_sha256 MATCHES "^[0-9a-f]+$")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence manifest should keep packaged names and SHA-256 evidence without local paths")
endif()
if(NOT temp_path_index EQUAL -1 OR source_absolute_path_leaked)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence manifest leaked local source/staging paths")
endif()
if(NOT local_ctest_count EQUAL 70 OR NOT proof_no_sensitive)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence manifest did not preserve local verification/no-sensitive proof")
endif()

set(READY_ROLLOUT_JSON "${TEMP_DIR}/e2e-rollout-observability-ready.json")
set(READY_CI_JSON "${TEMP_DIR}/github-windows-build-status-ready.json")
set(READY_LOCAL_JSON "${TEMP_DIR}/local-verification-status-ready.json")
file(WRITE "${READY_ROLLOUT_JSON}" "{\n  \"format\":\"qtnetworkchat-e2e-production-rollout-observability-evidence-v1\",\n  \"status\":\"ready\",\n  \"ok\":true,\n  \"summary\":{\"readiness\":\"ready\"},\n  \"auditSummary\":{\"releaseGate\":\"production-rollout-observability-ready\"},\n  \"productionAcceptanceSummary\":{\n    \"accepted\":true,\n    \"linked\":true,\n    \"productionReady\":true,\n    \"releaseGate\":\"production-crypto-accepted\",\n    \"backendId\":\"openssl-reviewed-adapter-v1\",\n    \"providerId\":\"openssl-reviewed-provider-v1\",\n    \"requiredOperationCount\":8,\n    \"availableOperationCount\":8\n  },\n  \"productionRolloutObservability\":{\n    \"accepted\":true,\n    \"linked\":true,\n    \"productionReady\":true,\n    \"releaseGate\":\"production-rollout-observability-ready\",\n    \"backendId\":\"openssl-reviewed-adapter-v1\",\n    \"providerId\":\"openssl-reviewed-provider-v1\",\n    \"releaseRunObservable\":true,\n    \"noSensitiveExportProof\":true,\n    \"requiredOperationCount\":8,\n    \"publicPrimitiveReadyCount\":8,\n    \"materialExportProofCount\":8,\n    \"outputShapeProofCount\":8,\n    \"rawKeyExported\":false,\n    \"privateMaterialExported\":false,\n    \"sessionSecretExported\":false,\n    \"plaintextBytesExported\":false,\n    \"ciphertextBytesExported\":false\n  },\n  \"releaseRun\":{\n    \"persisted\":true,\n    \"productionRequired\":true,\n    \"requestedBackendId\":\"openssl-reviewed-adapter-v1\",\n    \"selectedBackendId\":\"openssl-reviewed-adapter-v1\"\n  },\n  \"sensitiveExportProof\":{\n    \"noSensitiveExportProof\":true,\n    \"sensitiveFieldsSuppressed\":true,\n    \"rawKeyExported\":false,\n    \"privateMaterialExported\":false,\n    \"sessionSecretExported\":false,\n    \"plaintextBytesExported\":false,\n    \"ciphertextBytesExported\":false\n  }\n}\n")
file(WRITE "${READY_CI_JSON}" "{\n  \"format\":\"qtnetworkchat-github-windows-build-status-v1\",\n  \"headSha\":\"abc123\",\n  \"status\":\"success\",\n  \"runId\":\"12345\",\n  \"source\":\"json-artifact\",\n  \"visibility\":\"current-head-observed\",\n  \"observedRunCount\":1,\n  \"currentHeadObserved\":true,\n  \"releaseGate\":\"github-windows-build-current-head-success\",\n  \"sensitiveExportProof\":{\"noSensitiveExportProof\":true}\n}\n")
file(WRITE "${READY_LOCAL_JSON}" "{\n  \"format\":\"qtnetworkchat-local-verification-status-v1\",\n  \"ok\":true,\n  \"build\":{\"status\":\"passed\"},\n  \"ctest\":{\"status\":\"passed\",\"count\":71},\n  \"sensitiveExportProof\":{\"noSensitiveExportProof\":true}\n}\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/ready"
        -RolloutJsonPath "${READY_ROLLOUT_JSON}"
        -RolloutMarkdownPath "${ROLLOUT_MD}"
        -GitHubWindowsBuildStatusPath "${READY_CI_JSON}"
        -LocalVerificationStatusPath "${READY_LOCAL_JSON}"
        -AutomationStatusPath "${AUTO_MD}"
        -ReleaseHead "abc123"
    RESULT_VARIABLE ready_result
    OUTPUT_VARIABLE ready_output
    ERROR_VARIABLE ready_error
)
if(NOT ready_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence ready promotion package failed: ${ready_error}")
endif()
file(READ "${OUTPUT_DIR}/ready/e2e-release-evidence-manifest.json" ready_manifest)
file(READ "${OUTPUT_DIR}/ready/e2e-release-promotion.json" ready_promotion)
string(JSON ready_release_ready GET "${ready_manifest}" "releaseReady")
string(JSON ready_release_gate GET "${ready_manifest}" "releaseGate")
string(JSON ready_promotion_ready GET "${ready_promotion}" "promotionReady")
string(JSON ready_promotion_promoted GET "${ready_promotion}" "promoted")
string(JSON ready_promotion_gate GET "${ready_promotion}" "releaseGate")
string(JSON ready_promotion_blocker_count LENGTH "${ready_promotion}" "blockers")
string(JSON ready_probe_fixture GET "${ready_manifest}" "probeFixture")
string(JSON ready_release_eligible GET "${ready_manifest}" "releaseEligible")
string(JSON ready_eligibility_gate GET "${ready_manifest}" "releaseEligibilityGate")
string(JSON ready_promotion_probe_fixture GET "${ready_promotion}" "probeFixture")
string(JSON ready_promotion_release_eligible GET "${ready_promotion}" "releaseEligible")
string(JSON ready_production_linked_ready GET "${ready_manifest}" "productionLinkedEvidence" "ready")
string(JSON ready_production_linked_gate GET "${ready_manifest}" "productionLinkedEvidence" "releaseGate")
string(JSON ready_production_backend_match GET "${ready_manifest}" "productionLinkedEvidence" "acceptedBackendMatchesRollout")
string(JSON ready_requested_backend_match GET "${ready_manifest}" "productionLinkedEvidence" "requestedBackendMatchesAcceptance")
string(JSON ready_selected_backend_match GET "${ready_manifest}" "productionLinkedEvidence" "selectedBackendMatchesAcceptance")
string(JSON ready_operation_counts GET "${ready_manifest}" "productionLinkedEvidence" "operationCountsReady")
string(JSON ready_sensitive_exported GET "${ready_manifest}" "productionLinkedEvidence" "sensitiveMaterialExported")
if(NOT ready_release_ready
        OR NOT ready_release_gate STREQUAL "e2e-release-evidence-ready"
        OR NOT ready_promotion_ready
        OR NOT ready_promotion_promoted
        OR NOT ready_promotion_gate STREQUAL "e2e-release-artifact-promoted"
        OR ready_probe_fixture
        OR NOT ready_release_eligible
        OR NOT ready_eligibility_gate STREQUAL "release-eligible-current-head"
        OR ready_promotion_probe_fixture
        OR NOT ready_promotion_release_eligible)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence ready path should promote only when rollout, CI, and local verification are ready")
endif()
if(NOT ready_production_linked_ready
        OR NOT ready_production_linked_gate STREQUAL "production-linked-rollout-ready"
        OR NOT ready_production_backend_match
        OR NOT ready_requested_backend_match
        OR NOT ready_selected_backend_match
        OR NOT ready_operation_counts
        OR ready_sensitive_exported)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence ready path should require complete production-linked rollout evidence")
endif()
if(NOT ready_promotion_blocker_count EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence ready promotion should have no blockers")
endif()

set(PROBE_CI_JSON "${TEMP_DIR}/github-windows-build-status-probe.json")
file(WRITE "${PROBE_CI_JSON}" "{\n  \"format\":\"qtnetworkchat-github-windows-build-status-v1\",\n  \"headSha\":\"production-probe-head\",\n  \"status\":\"success\",\n  \"runId\":\"production-probe-run\",\n  \"source\":\"probe-fixture\",\n  \"visibility\":\"current-head-observed\",\n  \"observedRunCount\":1,\n  \"currentHeadObserved\":true,\n  \"releaseGate\":\"github-windows-build-current-head-success\",\n  \"sensitiveExportProof\":{\"noSensitiveExportProof\":true}\n}\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/probe"
        -RolloutJsonPath "${READY_ROLLOUT_JSON}"
        -RolloutMarkdownPath "${ROLLOUT_MD}"
        -GitHubWindowsBuildStatusPath "${PROBE_CI_JSON}"
        -LocalVerificationStatusPath "${READY_LOCAL_JSON}"
        -AutomationStatusPath "${AUTO_MD}"
        -ReleaseHead "production-probe-head"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_output
    ERROR_VARIABLE probe_error
)
if(NOT probe_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence probe package failed: ${probe_error}")
endif()
file(READ "${OUTPUT_DIR}/probe/e2e-release-evidence-manifest.json" probe_manifest)
file(READ "${OUTPUT_DIR}/probe/e2e-release-promotion.json" probe_promotion)
string(JSON probe_release_ready GET "${probe_manifest}" "releaseReady")
string(JSON probe_release_gate GET "${probe_manifest}" "releaseGate")
string(JSON probe_manifest_probe_fixture GET "${probe_manifest}" "probeFixture")
string(JSON probe_manifest_release_eligible GET "${probe_manifest}" "releaseEligible")
string(JSON probe_manifest_eligibility_gate GET "${probe_manifest}" "releaseEligibilityGate")
string(JSON probe_production_linked_ready GET "${probe_manifest}" "productionLinkedEvidence" "ready")
string(JSON probe_promotion_ready GET "${probe_promotion}" "promotionReady")
string(JSON probe_promotion_promoted GET "${probe_promotion}" "promoted")
string(JSON probe_promotion_gate GET "${probe_promotion}" "releaseGate")
string(JSON probe_promotion_probe_fixture GET "${probe_promotion}" "probeFixture")
string(JSON probe_promotion_release_eligible GET "${probe_promotion}" "releaseEligible")
string(JSON probe_promotion_eligibility_gate GET "${probe_promotion}" "releaseEligibilityGate")
string(JSON probe_promotion_blocker0 GET "${probe_promotion}" "blockers" 0)
if(probe_release_ready
        OR NOT probe_release_gate STREQUAL "blocked-release-artifact-probe-fixture"
        OR NOT probe_manifest_probe_fixture
        OR probe_manifest_release_eligible
        OR NOT probe_manifest_eligibility_gate STREQUAL "not-release-eligible-probe-fixture"
        OR NOT probe_production_linked_ready
        OR probe_promotion_ready
        OR probe_promotion_promoted
        OR NOT probe_promotion_gate STREQUAL "blocked-e2e-release-artifact-promotion"
        OR NOT probe_promotion_probe_fixture
        OR probe_promotion_release_eligible
        OR NOT probe_promotion_eligibility_gate STREQUAL "not-release-eligible-probe-fixture"
        OR NOT probe_promotion_blocker0 STREQUAL "release-artifact-probe-fixture")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence must not promote production-probe-head fixtures even when production-linked evidence is ready")
endif()

set(PSEUDO_READY_ROLLOUT_JSON "${TEMP_DIR}/e2e-rollout-observability-pseudo-ready.json")
file(WRITE "${PSEUDO_READY_ROLLOUT_JSON}" "{\n  \"format\":\"qtnetworkchat-e2e-production-rollout-observability-evidence-v1\",\n  \"status\":\"ready\",\n  \"ok\":true,\n  \"summary\":{\"readiness\":\"ready\"},\n  \"auditSummary\":{\"releaseGate\":\"production-rollout-observability-ready\"},\n  \"sensitiveExportProof\":{\"noSensitiveExportProof\":true,\"sensitiveFieldsSuppressed\":true}\n}\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/pseudo-ready"
        -RolloutJsonPath "${PSEUDO_READY_ROLLOUT_JSON}"
        -RolloutMarkdownPath "${ROLLOUT_MD}"
        -GitHubWindowsBuildStatusPath "${READY_CI_JSON}"
        -LocalVerificationStatusPath "${READY_LOCAL_JSON}"
        -AutomationStatusPath "${AUTO_MD}"
        -ReleaseHead "abc123"
    RESULT_VARIABLE pseudo_ready_result
    OUTPUT_VARIABLE pseudo_ready_output
    ERROR_VARIABLE pseudo_ready_error
)
if(NOT pseudo_ready_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence pseudo-ready package failed: ${pseudo_ready_error}")
endif()
file(READ "${OUTPUT_DIR}/pseudo-ready/e2e-release-evidence-manifest.json" pseudo_ready_manifest)
file(READ "${OUTPUT_DIR}/pseudo-ready/e2e-release-promotion.json" pseudo_ready_promotion)
string(JSON pseudo_release_ready GET "${pseudo_ready_manifest}" "releaseReady")
string(JSON pseudo_release_gate GET "${pseudo_ready_manifest}" "releaseGate")
string(JSON pseudo_production_linked_ready GET "${pseudo_ready_manifest}" "productionLinkedEvidence" "ready")
string(JSON pseudo_production_linked_gate GET "${pseudo_ready_manifest}" "productionLinkedEvidence" "releaseGate")
string(JSON pseudo_production_linked_blocker0 GET "${pseudo_ready_manifest}" "productionLinkedEvidence" "blockers" 0)
string(JSON pseudo_promotion_ready GET "${pseudo_ready_promotion}" "promotionReady")
string(JSON pseudo_promotion_promoted GET "${pseudo_ready_promotion}" "promoted")
string(JSON pseudo_promotion_blocker0 GET "${pseudo_ready_promotion}" "blockers" 0)
if(pseudo_release_ready
        OR NOT pseudo_release_gate STREQUAL "blocked-production-linked-rollout-not-ready"
        OR pseudo_production_linked_ready
        OR NOT pseudo_production_linked_gate STREQUAL "blocked-production-linked-rollout-not-ready"
        OR NOT pseudo_production_linked_blocker0 STREQUAL "production-acceptance-not-accepted"
        OR pseudo_promotion_ready
        OR pseudo_promotion_promoted
        OR NOT pseudo_promotion_blocker0 STREQUAL "production-linked-rollout-not-ready")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence must block pseudo-ready rollout artifacts without production-linked proof")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/head-mismatch"
        -RolloutJsonPath "${READY_ROLLOUT_JSON}"
        -RolloutMarkdownPath "${ROLLOUT_MD}"
        -GitHubWindowsBuildStatusPath "${READY_CI_JSON}"
        -LocalVerificationStatusPath "${READY_LOCAL_JSON}"
        -AutomationStatusPath "${AUTO_MD}"
        -ReleaseHead "def456"
    RESULT_VARIABLE head_mismatch_result
    OUTPUT_VARIABLE head_mismatch_output
    ERROR_VARIABLE head_mismatch_error
)
if(NOT head_mismatch_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence head-mismatch package failed: ${head_mismatch_error}")
endif()
file(READ "${OUTPUT_DIR}/head-mismatch/e2e-release-evidence-manifest.json" mismatch_manifest)
file(READ "${OUTPUT_DIR}/head-mismatch/e2e-release-promotion.json" mismatch_promotion)
string(JSON mismatch_release_ready GET "${mismatch_manifest}" "releaseReady")
string(JSON mismatch_release_gate GET "${mismatch_manifest}" "releaseGate")
string(JSON mismatch_ci_head_matches GET "${mismatch_manifest}" "ci" "headMatchesReleaseHead")
string(JSON mismatch_target_head GET "${mismatch_manifest}" "targetReleaseHead")
string(JSON mismatch_ci_head GET "${mismatch_manifest}" "ci" "headSha")
string(JSON mismatch_promotion_ready GET "${mismatch_promotion}" "promotionReady")
string(JSON mismatch_promotion_promoted GET "${mismatch_promotion}" "promoted")
string(JSON mismatch_promotion_ci_head_matches GET "${mismatch_promotion}" "ciHeadMatchesReleaseHead")
string(JSON mismatch_promotion_blocker0 GET "${mismatch_promotion}" "blockers" 0)
if(mismatch_release_ready
        OR NOT mismatch_release_gate STREQUAL "blocked-ci-head-mismatch"
        OR mismatch_ci_head_matches
        OR NOT mismatch_target_head STREQUAL "def456"
        OR NOT mismatch_ci_head STREQUAL "abc123"
        OR mismatch_promotion_ready
        OR mismatch_promotion_promoted
        OR mismatch_promotion_ci_head_matches
        OR NOT mismatch_promotion_blocker0 STREQUAL "ci-head-mismatch")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence should block promotion when the CI artifact head does not match the release head")
endif()

file(MAKE_DIRECTORY "${EXTRACT_DIR}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xfz "${PACKAGE_PATH}"
    WORKING_DIRECTORY "${EXTRACT_DIR}"
    RESULT_VARIABLE extract_result
    OUTPUT_VARIABLE extract_output
    ERROR_VARIABLE extract_error
)
if(NOT extract_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Failed to extract E2E release evidence package: ${extract_error}")
endif()
if(NOT EXISTS "${EXTRACT_DIR}/manifest.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence package should include manifest.json")
endif()
file(READ "${EXTRACT_DIR}/manifest.json" packaged_manifest_content)
string(JSON packaged_format GET "${packaged_manifest_content}" "format")
string(JSON packaged_package_sha256 GET "${packaged_manifest_content}" "packageSha256")
string(JSON packaged_manifest_embedded GET "${packaged_manifest_content}" "manifestEmbedded")
string(FIND "${packaged_manifest_content}" "${TEMP_DIR}" packaged_temp_path_index)
if(NOT packaged_format STREQUAL "qtnetworkchat-e2e-release-evidence-package-v1"
        OR NOT packaged_package_sha256 STREQUAL "pending"
        OR NOT packaged_manifest_embedded)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Packaged E2E release evidence manifest should be self-describing, embedded, and mark pre-zip SHA pending")
endif()
if(NOT packaged_temp_path_index EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Packaged E2E release evidence manifest leaked local paths")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/missing-ci"
        -RolloutJsonPath "${ROLLOUT_JSON}"
        -RolloutMarkdownPath "${ROLLOUT_MD}"
        -LocalVerificationStatusPath "${LOCAL_JSON}"
        -ReleaseHead "abc123"
    RESULT_VARIABLE missing_ci_result
    OUTPUT_VARIABLE missing_ci_output
    ERROR_VARIABLE missing_ci_error
)
if(NOT missing_ci_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence packager should emit a blocked manifest for missing CI, not fail")
endif()
file(READ "${OUTPUT_DIR}/missing-ci/e2e-release-evidence-manifest.json" missing_ci_manifest)
string(JSON missing_ci_ok GET "${missing_ci_manifest}" "ok")
string(JSON missing_ci_gate GET "${missing_ci_manifest}" "releaseGate")
if(missing_ci_ok OR NOT missing_ci_gate STREQUAL "blocked-missing-github-windows-build-status")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Missing CI should produce a blocked E2E release evidence manifest")
endif()

set(BAD "${TEMP_DIR}/bad-rollout.json")
file(WRITE "${BAD}" "{\"password\":\"plain-secret\"}\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/bad"
        -RolloutJsonPath "${BAD}"
        -GitHubWindowsBuildStatusPath "${CI_JSON}"
        -LocalVerificationStatusPath "${LOCAL_JSON}"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "E2E release evidence packager should reject sensitive inputs")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
