if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/local_release_review_package")
set(OUTPUT_DIR "${TEMP_DIR}/out")
set(EXTRACT_DIR "${TEMP_DIR}/extract")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}" "${OUTPUT_DIR}" "${EXTRACT_DIR}")

set(README_PATH "${TEMP_DIR}/README.md")
set(AUTOMATION_STATUS_PATH "${TEMP_DIR}/automation-status.md")
set(E2E_HARDENING_PATH "${TEMP_DIR}/e2e-hardening-status.md")
set(E2E_PLAN_PATH "${TEMP_DIR}/end-to-end-encryption-plan.md")
set(LOCAL_VERIFICATION_PATH "${TEMP_DIR}/local-verification-status.json")
set(E2E_MANIFEST_PATH "${TEMP_DIR}/e2e-release-evidence-manifest.json")
set(E2E_PROMOTION_PATH "${TEMP_DIR}/e2e-release-promotion.json")
set(S3_PATH "${TEMP_DIR}/s3-real-backend-readiness.json")
set(GOVERNANCE_DASHBOARD_PATH "${TEMP_DIR}/large-file-governance-dashboard.json")
set(GOVERNANCE_REPORT_PATH "${TEMP_DIR}/large-file-governance-report.md")
set(GOVERNANCE_PERFORMANCE_SUMMARY_PATH "${TEMP_DIR}/large-file-governance-performance-summary.json")
set(GOVERNANCE_DIAGNOSTICS_PATH "${TEMP_DIR}/large-file-governance-diagnostics.zip")
set(PGSQL_ACCEPTANCE_PATH "${TEMP_DIR}/pgsql-release-acceptance.json")
set(PGSQL_EVIDENCE_MANIFEST_PATH "${TEMP_DIR}/pgsql-release-evidence-manifest.json")
set(PGSQL_ROLLBACK_PATH "${TEMP_DIR}/pgsql-rollback-live-evidence.json")
set(PGSQL_ROLLBACK_MANIFEST_PATH "${TEMP_DIR}/pgsql-rollback-live-evidence-manifest.json")
set(WINDOWS_MANIFEST_PATH "${TEMP_DIR}/windows-manifest.json")
set(RELEASE_ARCHIVE_DECISION_MANIFEST_PATH "${TEMP_DIR}/release-archive-decision-manifest.json")
set(RELEASE_ARCHIVE_DECISION_MARKDOWN_PATH "${TEMP_DIR}/release-archive-decision.md")
set(AUTOMATION_POLICY_PATH "${TEMP_DIR}/automation-policy.json")

file(WRITE "${README_PATH}" "# Readme\n")
file(WRITE "${AUTOMATION_STATUS_PATH}" "# Automation Status\n")
file(WRITE "${E2E_HARDENING_PATH}" "# E2E Hardening\n")
file(WRITE "${E2E_PLAN_PATH}" "# E2E Plan\n")
file(WRITE "${LOCAL_VERIFICATION_PATH}" "{\n  \"format\":\"qtnetworkchat-local-verification-status-v1\",\n  \"ok\":true,\n  \"build\":{\"status\":\"passed\"},\n  \"ctest\":{\"status\":\"passed\",\"count\":81},\n  \"sensitiveExportProof\":{\"noSensitiveExportProof\":true}\n}\n")
file(WRITE "${E2E_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-e2e-release-evidence-package-v1\",\n  \"releaseReady\":true,\n  \"releaseGate\":\"ready-local-verification-only\",\n  \"targetReleaseHead\":\"abc123\",\n  \"packageSha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\n  \"productionLinkedEvidence\":{\"ready\":true}\n}\n")
file(WRITE "${E2E_PROMOTION_PATH}" "{\n  \"format\":\"qtnetworkchat-e2e-release-artifact-promotion-v1\",\n  \"promoted\":true,\n  \"promotionReady\":true,\n  \"releaseGate\":\"ready-local-verification-only\"\n}\n")
file(WRITE "${S3_PATH}" "{\n  \"format\":\"qtnetworkchat-s3-real-backend-readiness-v1\",\n  \"ok\":true,\n  \"status\":\"verified\",\n  \"summary\":{\"readiness\":\"verified\"},\n  \"auditSummary\":{\"releaseGate\":\"can-review-s3-real-backend-evidence\"}\n}\n")
file(WRITE "${GOVERNANCE_DASHBOARD_PATH}" "{\n  \"format\":\"qtnetworkchat-large-file-governance-dashboard-v1\",\n  \"ok\":true,\n  \"status\":\"healthy\",\n  \"totalWarnings\":0,\n  \"summary\":{\"readiness\":\"verified\"},\n  \"auditSummary\":{\"releaseGate\":\"can-review-governance-evidence\"}\n}\n")
file(WRITE "${GOVERNANCE_REPORT_PATH}" "# Governance Report\n")
file(WRITE "${GOVERNANCE_PERFORMANCE_SUMMARY_PATH}" "{\n  \"format\":\"qtnetworkchat-large-file-governance-performance-summary-v1\",\n  \"summary\":{\"readiness\":\"review\"},\n  \"auditSummary\":{\"releaseGate\":\"review-governance-performance\"},\n  \"bottlenecks\":[\"receipt-archive-pressure\"]\n}\n")
file(WRITE "${GOVERNANCE_DIAGNOSTICS_PATH}" "placeholder")
file(WRITE "${PGSQL_ACCEPTANCE_PATH}" "{\n  \"format\":\"qtnetworkchat-pgsql-release-acceptance-v1\",\n  \"ok\":true,\n  \"status\":\"ready\",\n  \"summary\":{\"readiness\":\"ready\"},\n  \"auditSummary\":{\"releaseGate\":\"can-review-cutover\"}\n}\n")
file(WRITE "${PGSQL_EVIDENCE_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-pgsql-release-evidence-package-v1\",\n  \"ok\":true,\n  \"inputCount\":18\n}\n")
file(WRITE "${PGSQL_ROLLBACK_PATH}" "{\n  \"format\":\"qtnetworkchat-pgsql-rollback-live-evidence-v1\",\n  \"ok\":true,\n  \"status\":\"verified\",\n  \"summary\":{\"readiness\":\"verified\",\"releaseGate\":\"can-close-pgsql-rollback-live-evidence\"}\n}\n")
file(WRITE "${PGSQL_ROLLBACK_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-pgsql-release-evidence-package-v1\",\n  \"ok\":true,\n  \"inputCount\":20\n}\n")
file(WRITE "${WINDOWS_MANIFEST_PATH}" "{\n  \"packageFormat\":\"qtnetworkchat-windows-package-v1\",\n  \"gitCommit\":\"old-sample-head\",\n  \"runtimeCheck\":{\"ok\":true},\n  \"postgresSqlRuntime\":{\"ok\":true}\n}\n")
file(WRITE "${RELEASE_ARCHIVE_DECISION_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-release-archive-decision-v1\",\n  \"decisionRecorded\":true,\n  \"decisionState\":\"approved-local-archive\",\n  \"decisionGate\":\"archive-decision-recorded-publication-pending\",\n  \"publishingRequired\":true,\n  \"publishingStatus\":\"pending-environment-publication\",\n  \"publishingRecordPresent\":true,\n  \"releaseDeliveryDrillPresent\":true,\n  \"packageSha256\":\"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\",\n  \"blockers\":[]\n}\n")
file(WRITE "${RELEASE_ARCHIVE_DECISION_MARKDOWN_PATH}" "# Release Archive Decision\n")
file(WRITE "${AUTOMATION_POLICY_PATH}" "{\n  \"format\":\"qtnetworkchat-automation-policy-v1\",\n  \"gitHubWindowsBuildPolicy\":\"disabled\"\n}\n")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}"
        -ReadmePath "${README_PATH}"
        -AutomationStatusPath "${AUTOMATION_STATUS_PATH}"
        -E2EHardeningStatusPath "${E2E_HARDENING_PATH}"
        -EndToEndEncryptionPlanPath "${E2E_PLAN_PATH}"
        -LocalVerificationStatusPath "${LOCAL_VERIFICATION_PATH}"
        -E2EReleaseEvidenceManifestPath "${E2E_MANIFEST_PATH}"
        -E2EReleasePromotionPath "${E2E_PROMOTION_PATH}"
        -S3RealBackendReadinessPath "${S3_PATH}"
        -LargeFileGovernanceDashboardPath "${GOVERNANCE_DASHBOARD_PATH}"
        -LargeFileGovernanceReportPath "${GOVERNANCE_REPORT_PATH}"
        -LargeFileGovernancePerformanceSummaryPath "${GOVERNANCE_PERFORMANCE_SUMMARY_PATH}"
        -LargeFileGovernanceDiagnosticsPath "${GOVERNANCE_DIAGNOSTICS_PATH}"
        -PgsqlAcceptancePath "${PGSQL_ACCEPTANCE_PATH}"
        -PgsqlEvidenceManifestPath "${PGSQL_EVIDENCE_MANIFEST_PATH}"
        -PgsqlRollbackLivePath "${PGSQL_ROLLBACK_PATH}"
        -PgsqlRollbackEvidenceManifestPath "${PGSQL_ROLLBACK_MANIFEST_PATH}"
        -WindowsPackageManifestPath "${WINDOWS_MANIFEST_PATH}"
        -ReleaseArchiveDecisionManifestPath "${RELEASE_ARCHIVE_DECISION_MANIFEST_PATH}"
        -ReleaseArchiveDecisionMarkdownPath "${RELEASE_ARCHIVE_DECISION_MARKDOWN_PATH}"
        -AutomationPolicyPath "${AUTOMATION_POLICY_PATH}"
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
    message(FATAL_ERROR "package-local-release-review.ps1 exited with code ${result}")
endif()

set(PACKAGE_PATH "${OUTPUT_DIR}/local-release-review.zip")
set(MANIFEST_PATH "${OUTPUT_DIR}/local-release-review-manifest.json")
set(MARKDOWN_PATH "${OUTPUT_DIR}/local-release-review.md")
if(NOT EXISTS "${PACKAGE_PATH}" OR NOT EXISTS "${MANIFEST_PATH}" OR NOT EXISTS "${MARKDOWN_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected local release review artifacts were not created")
endif()

file(READ "${MANIFEST_PATH}" manifest_content)
string(JSON format GET "${manifest_content}" "format")
string(JSON review_ready GET "${manifest_content}" "reviewReady")
string(JSON review_gate GET "${manifest_content}" "reviewGate")
string(JSON package_path GET "${manifest_content}" "packagePath")
string(JSON package_sha256 GET "${manifest_content}" "packageSha256")
string(JSON staging_dir GET "${manifest_content}" "stagingDir")
string(JSON markdown_packaged_as GET "${manifest_content}" "markdownPackagedAs")
string(JSON manifest_packaged_as GET "${manifest_content}" "manifestPackagedAs")
string(JSON manifest_embedded GET "${manifest_content}" "manifestEmbedded")
string(JSON input_count GET "${manifest_content}" "inputCount")
string(JSON github_policy GET "${manifest_content}" "githubWindowsBuildPolicy")
string(JSON target_release_head GET "${manifest_content}" "targetReleaseHead")
string(JSON decision_ready GET "${manifest_content}" "finalArchiveDecision" "ready")
string(JSON decision_gate GET "${manifest_content}" "finalArchiveDecision" "reviewGate")
string(JSON decision_recorded GET "${manifest_content}" "finalArchiveDecision" "recorded")
string(JSON decision_state GET "${manifest_content}" "finalArchiveDecision" "decisionState")
string(JSON human_decision_required GET "${manifest_content}" "finalArchiveDecision" "humanDecisionRequired")
string(JSON blocker_count GET "${manifest_content}" "finalArchiveDecision" "blockerCount")
string(JSON delivery_tail_count GET "${manifest_content}" "finalArchiveDecision" "deliveryTailCount")
string(JSON nonblocking_count GET "${manifest_content}" "finalArchiveDecision" "nonBlockingObservationCount")
string(JSON observation0 GET "${manifest_content}" "finalArchiveDecision" "nonBlockingObservations" 0)
string(JSON artifact0_kind GET "${manifest_content}" "artifacts" 0 "kind")
string(JSON artifact0_ready GET "${manifest_content}" "artifacts" 0 "ready")
string(JSON proof_no_sensitive GET "${manifest_content}" "sensitiveExportProof" "noSensitiveExportProof")
string(FIND "${manifest_content}" "${TEMP_DIR}" temp_path_index)
if(manifest_content MATCHES "\"sourceName\"[ \t\r\n]*:[ \t\r\n]*\"[A-Za-z]:")
    set(source_absolute_path_leaked TRUE)
else()
    set(source_absolute_path_leaked FALSE)
endif()
string(LENGTH "${package_sha256}" package_sha256_length)

if(NOT format STREQUAL "qtnetworkchat-local-release-review-package-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected local release review manifest format: ${format}")
endif()
if(NOT review_ready
        OR NOT decision_ready
        OR NOT review_gate STREQUAL "review-complete-archive-decision-recorded"
        OR NOT decision_gate STREQUAL "review-complete-archive-decision-recorded"
        OR NOT decision_recorded
        OR NOT decision_state STREQUAL "approved-local-archive"
        OR human_decision_required
        OR NOT blocker_count EQUAL 0
        OR NOT delivery_tail_count EQUAL 0
        OR NOT nonblocking_count EQUAL 1
        OR NOT observation0 STREQUAL "windows-package-manifest-stale-sample")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected ready local release review decision with zero delivery-tail blockers and stale sample windows package observation")
endif()
if(NOT package_path STREQUAL "local-release-review.zip"
        OR NOT package_sha256_length EQUAL 64
        OR NOT package_sha256 MATCHES "^[0-9a-f]+$"
        OR NOT staging_dir STREQUAL "local-release-review"
        OR NOT markdown_packaged_as STREQUAL "local-release-review.md"
        OR NOT manifest_packaged_as STREQUAL "manifest.json"
        OR NOT manifest_embedded
        OR NOT target_release_head STREQUAL "abc123"
        OR NOT input_count EQUAL 19
        OR NOT github_policy STREQUAL "disabled"
        OR NOT artifact0_kind STREQUAL "local-verification"
        OR NOT artifact0_ready
        OR NOT proof_no_sensitive)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Local release review manifest did not preserve the expected package metadata")
endif()
if(NOT temp_path_index EQUAL -1 OR source_absolute_path_leaked)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Local release review manifest leaked local source paths")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xfz "${PACKAGE_PATH}"
    WORKING_DIRECTORY "${EXTRACT_DIR}"
    RESULT_VARIABLE extract_result
    OUTPUT_VARIABLE extract_output
    ERROR_VARIABLE extract_error
)
if(NOT extract_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Failed to extract local release review package: ${extract_error}")
endif()
if(NOT EXISTS "${EXTRACT_DIR}/local-release-review.md"
        OR NOT EXISTS "${EXTRACT_DIR}/manifest.json"
        OR NOT EXISTS "${EXTRACT_DIR}/archive/release-archive-decision-manifest.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Extracted local release review package is missing the embedded summary or manifest")
endif()

set(BLOCKED_ACCEPTANCE_PATH "${TEMP_DIR}/pgsql-release-acceptance-blocked.json")
file(WRITE "${BLOCKED_ACCEPTANCE_PATH}" "{\n  \"format\":\"qtnetworkchat-pgsql-release-acceptance-v1\",\n  \"ok\":false,\n  \"status\":\"blocked\",\n  \"summary\":{\"readiness\":\"blocked\"},\n  \"auditSummary\":{\"releaseGate\":\"blocked\"}\n}\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/blocked"
        -ReadmePath "${README_PATH}"
        -AutomationStatusPath "${AUTOMATION_STATUS_PATH}"
        -E2EHardeningStatusPath "${E2E_HARDENING_PATH}"
        -EndToEndEncryptionPlanPath "${E2E_PLAN_PATH}"
        -LocalVerificationStatusPath "${LOCAL_VERIFICATION_PATH}"
        -E2EReleaseEvidenceManifestPath "${E2E_MANIFEST_PATH}"
        -E2EReleasePromotionPath "${E2E_PROMOTION_PATH}"
        -S3RealBackendReadinessPath "${S3_PATH}"
        -LargeFileGovernanceDashboardPath "${GOVERNANCE_DASHBOARD_PATH}"
        -LargeFileGovernanceReportPath "${GOVERNANCE_REPORT_PATH}"
        -LargeFileGovernancePerformanceSummaryPath "${GOVERNANCE_PERFORMANCE_SUMMARY_PATH}"
        -LargeFileGovernanceDiagnosticsPath "${GOVERNANCE_DIAGNOSTICS_PATH}"
        -PgsqlAcceptancePath "${BLOCKED_ACCEPTANCE_PATH}"
        -PgsqlEvidenceManifestPath "${PGSQL_EVIDENCE_MANIFEST_PATH}"
        -PgsqlRollbackLivePath "${PGSQL_ROLLBACK_PATH}"
        -PgsqlRollbackEvidenceManifestPath "${PGSQL_ROLLBACK_MANIFEST_PATH}"
        -WindowsPackageManifestPath "${WINDOWS_MANIFEST_PATH}"
        -AutomationPolicyPath "${AUTOMATION_POLICY_PATH}"
        -ReleaseHead "abc123"
    RESULT_VARIABLE blocked_result
    OUTPUT_VARIABLE blocked_output
    ERROR_VARIABLE blocked_error
)
if(NOT blocked_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Blocked local release review package run failed unexpectedly: ${blocked_error}")
endif()
file(READ "${OUTPUT_DIR}/blocked/local-release-review-manifest.json" blocked_manifest)
string(JSON blocked_review_ready GET "${blocked_manifest}" "reviewReady")
string(JSON blocked_review_gate GET "${blocked_manifest}" "reviewGate")
string(JSON blocked_blocker0 GET "${blocked_manifest}" "finalArchiveDecision" "blockers" 0)
if(blocked_review_ready OR NOT blocked_review_gate STREQUAL "blocked-local-release-review-core-gates" OR NOT blocked_blocker0 STREQUAL "pgsql-acceptance-not-reviewable")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Blocked local release review package should preserve core gate blockers")
endif()

set(STALE_E2E_MANIFEST_PATH "${TEMP_DIR}/e2e-release-evidence-stale-manifest.json")
file(WRITE "${STALE_E2E_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-e2e-release-evidence-package-v1\",\n  \"releaseReady\":true,\n  \"releaseGate\":\"ready-local-verification-only\",\n  \"targetReleaseHead\":\"stale-head\",\n  \"packageSha256\":\"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\",\n  \"productionLinkedEvidence\":{\"ready\":true}\n}\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/stale"
        -ReadmePath "${README_PATH}"
        -AutomationStatusPath "${AUTOMATION_STATUS_PATH}"
        -E2EHardeningStatusPath "${E2E_HARDENING_PATH}"
        -EndToEndEncryptionPlanPath "${E2E_PLAN_PATH}"
        -LocalVerificationStatusPath "${LOCAL_VERIFICATION_PATH}"
        -E2EReleaseEvidenceManifestPath "${STALE_E2E_MANIFEST_PATH}"
        -E2EReleasePromotionPath "${E2E_PROMOTION_PATH}"
        -S3RealBackendReadinessPath "${S3_PATH}"
        -LargeFileGovernanceDashboardPath "${GOVERNANCE_DASHBOARD_PATH}"
        -LargeFileGovernanceReportPath "${GOVERNANCE_REPORT_PATH}"
        -LargeFileGovernancePerformanceSummaryPath "${GOVERNANCE_PERFORMANCE_SUMMARY_PATH}"
        -LargeFileGovernanceDiagnosticsPath "${GOVERNANCE_DIAGNOSTICS_PATH}"
        -PgsqlAcceptancePath "${PGSQL_ACCEPTANCE_PATH}"
        -PgsqlEvidenceManifestPath "${PGSQL_EVIDENCE_MANIFEST_PATH}"
        -PgsqlRollbackLivePath "${PGSQL_ROLLBACK_PATH}"
        -PgsqlRollbackEvidenceManifestPath "${PGSQL_ROLLBACK_MANIFEST_PATH}"
        -WindowsPackageManifestPath "${WINDOWS_MANIFEST_PATH}"
        -AutomationPolicyPath "${AUTOMATION_POLICY_PATH}"
        -ReleaseHead "abc123"
    RESULT_VARIABLE stale_result
    OUTPUT_VARIABLE stale_output
    ERROR_VARIABLE stale_error
)
if(NOT stale_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Stale-head local release review package run failed unexpectedly: ${stale_error}")
endif()
file(READ "${OUTPUT_DIR}/stale/local-release-review-manifest.json" stale_manifest)
string(JSON stale_review_ready GET "${stale_manifest}" "reviewReady")
string(JSON stale_review_gate GET "${stale_manifest}" "reviewGate")
string(JSON stale_blocker0 GET "${stale_manifest}" "finalArchiveDecision" "blockers" 0)
if(stale_review_ready OR NOT stale_review_gate STREQUAL "blocked-local-release-review-core-gates" OR NOT stale_blocker0 STREQUAL "e2e-release-evidence-head-mismatch")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Local release review package should block stale E2E release evidence heads")
endif()

set(BAD_AUTOMATION_STATUS_PATH "${TEMP_DIR}/bad-automation-status.md")
file(WRITE "${BAD_AUTOMATION_STATUS_PATH}" "Authorization: should-not-ship\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/bad"
        -ReadmePath "${README_PATH}"
        -AutomationStatusPath "${BAD_AUTOMATION_STATUS_PATH}"
        -E2EHardeningStatusPath "${E2E_HARDENING_PATH}"
        -EndToEndEncryptionPlanPath "${E2E_PLAN_PATH}"
        -LocalVerificationStatusPath "${LOCAL_VERIFICATION_PATH}"
        -E2EReleaseEvidenceManifestPath "${E2E_MANIFEST_PATH}"
        -E2EReleasePromotionPath "${E2E_PROMOTION_PATH}"
        -S3RealBackendReadinessPath "${S3_PATH}"
        -LargeFileGovernanceDashboardPath "${GOVERNANCE_DASHBOARD_PATH}"
        -LargeFileGovernanceReportPath "${GOVERNANCE_REPORT_PATH}"
        -LargeFileGovernancePerformanceSummaryPath "${GOVERNANCE_PERFORMANCE_SUMMARY_PATH}"
        -LargeFileGovernanceDiagnosticsPath "${GOVERNANCE_DIAGNOSTICS_PATH}"
        -PgsqlAcceptancePath "${PGSQL_ACCEPTANCE_PATH}"
        -PgsqlEvidenceManifestPath "${PGSQL_EVIDENCE_MANIFEST_PATH}"
        -PgsqlRollbackLivePath "${PGSQL_ROLLBACK_PATH}"
        -PgsqlRollbackEvidenceManifestPath "${PGSQL_ROLLBACK_MANIFEST_PATH}"
        -WindowsPackageManifestPath "${WINDOWS_MANIFEST_PATH}"
        -AutomationPolicyPath "${AUTOMATION_POLICY_PATH}"
        -ReleaseHead "abc123"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Local release review package should fail when sensitive fields are present")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Local release review package test passed")
