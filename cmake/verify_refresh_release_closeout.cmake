if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

get_filename_component(REPO_DIR "${SCRIPT_PATH}/../.." ABSOLUTE)
set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/refresh_release_closeout")
set(BUILD_DIR "${TEMP_DIR}/build")
set(AUTOMATION_STATUS_PATH "${TEMP_DIR}/automation-status.md")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}" "${BUILD_DIR}")

execute_process(
    COMMAND git -C "${REPO_DIR}" rev-parse HEAD
    RESULT_VARIABLE head_result
    OUTPUT_VARIABLE release_head
    ERROR_VARIABLE head_error
)
if(NOT head_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Failed to resolve repo HEAD: ${head_error}")
endif()
string(STRIP "${release_head}" release_head)
string(SUBSTRING "${release_head}" 0 12 release_head_short)

file(WRITE "${BUILD_DIR}/QtNetworkChat.exe" "fake current-head exe")
file(MAKE_DIRECTORY "${BUILD_DIR}/Testing/Temporary")
file(WRITE "${BUILD_DIR}/Testing/Temporary/LastTest.log"
"1/86 Testing: QtNetworkChatExecutableExists\n"
"86/86 Testing: PostgresRollbackLiveEvidence\n"
)

file(MAKE_DIRECTORY "${BUILD_DIR}/e2e_rollout_observability_evidence")
file(WRITE "${BUILD_DIR}/e2e_rollout_observability_evidence/e2e-rollout-observability.json" "{\n"
"  \"format\":\"qtnetworkchat-e2e-production-rollout-observability-evidence-v1\",\n"
"  \"status\":\"ready\",\n"
"  \"ok\":true,\n"
"  \"summary\":{\"readiness\":\"verified\",\"operatorAction\":\"none\",\"filesystemObjectRecoveryReady\":true,\"filesystemObjectRecoveryReleaseGate\":\"e2e-filesystem-object-ciphertext-readback-ready\",\"offlineObjectRecoveryReady\":true,\"offlineObjectRecoveryScope\":\"offline-ciphertext-readback\",\"offlineObjectRecoveryReleaseGate\":\"e2e-offline-ciphertext-readback-reviewed-opt-in\"},\n"
"  \"auditSummary\":{\"releaseGate\":\"production-rollout-observability-ready\",\"auditFocus\":[\"production-crypto-acceptance\",\"rollout-observability\"],\"evidenceBundle\":[\"e2e-rollout-observability.json\",\"e2e-rollout-observability.md\"],\"writeIntent\":\"sanitized-status-only\",\"backupRequired\":false},\n"
"  \"productionAcceptanceSummary\":{\"accepted\":true,\"linked\":true,\"productionReady\":true,\"releaseGate\":\"production-crypto-accepted\",\"backendId\":\"openssl-reviewed-adapter-v1\",\"providerId\":\"openssl-reviewed-provider-v1\",\"requiredOperationCount\":8,\"availableOperationCount\":8,\"blockedOperationCount\":0,\"providerDataPlaneBridgeReadyCount\":8,\"providerPublicPrimitiveExecutionReadyCount\":8,\"providerPublicPrimitiveExecutionBlockedCount\":0,\"providerInvocationExecutionAccepted\":true,\"providerOperationPreflightAccepted\":true,\"providerTableAccepted\":true,\"rawKeyExported\":false,\"privateMaterialExported\":false},\n"
"  \"productionRolloutObservability\":{\"accepted\":true,\"linked\":true,\"productionReady\":true,\"releaseGate\":\"production-rollout-observability-ready\",\"backendId\":\"openssl-reviewed-adapter-v1\",\"providerId\":\"openssl-reviewed-provider-v1\",\"releaseRunObservable\":true,\"noSensitiveExportProof\":true,\"requiredOperationCount\":8,\"publicPrimitiveReadyCount\":8,\"materialExportProofCount\":8,\"outputShapeProofCount\":8,\"rawKeyExported\":false,\"privateMaterialExported\":false,\"sessionSecretExported\":false,\"plaintextBytesExported\":false,\"ciphertextBytesExported\":false,\"filesystemObjectRecoveryReady\":true,\"filesystemObjectRecoveryReleaseGate\":\"e2e-filesystem-object-ciphertext-readback-ready\",\"filesystemObjectRecoveryNoSensitiveExportProof\":true,\"filesystemObjectRecoveryAction\":\"resume-verified-filesystem-object-ciphertext-or-fail-closed-to-resend\",\"filesystemObjectRecoveryCapturePolicy\":\"object-key-hash-size-envelope-header-session-metadata-only\",\"filesystemObjectRecoveryPromptReady\":true,\"filesystemObjectRecoveryPrompt\":\"show-resume-when-filesystem-object-evidence-matches-otherwise-resend\",\"offlineObjectRecoveryReady\":true,\"offlineObjectRecoveryReleaseGate\":\"e2e-offline-ciphertext-readback-reviewed-opt-in\",\"offlineObjectRecoveryNoSensitiveExportProof\":true,\"offlineObjectRecoveryAction\":\"enable-reviewed-offline-ciphertext-mirror-or-fail-closed-to-resend\",\"offlineObjectRecoveryCapturePolicy\":\"safe-object-token-hash-size-envelope-header-session-metadata-only\",\"offlineObjectRecoveryScope\":\"offline-ciphertext-readback\",\"operatorAction\":\"none\",\"operatorRecoveryPrompts\":[\"none\"],\"userRecoveryPrompts\":[\"none\"],\"sensitiveFieldsSuppressed\":true},\n"
"  \"releaseRun\":{\"persisted\":true,\"productionRequired\":true,\"requestedBackendId\":\"openssl-reviewed-adapter-v1\",\"selectedBackendId\":\"openssl-reviewed-adapter-v1\",\"selectionSource\":\"environment\",\"tool\":\"e2e_rollout_observability_exporter\",\"capturePolicy\":\"release-gates-counts-actions-and-prompts-only\"},\n"
"  \"sensitiveExportProof\":{\"noSensitiveExportProof\":true,\"sensitiveFieldsSuppressed\":true,\"rawKeyExported\":false,\"privateMaterialExported\":false,\"sessionSecretExported\":false,\"plaintextBytesExported\":false,\"ciphertextBytesExported\":false,\"fullFingerprintExported\":false,\"fullPublicIdentityMaterialExported\":false,\"localPrivatePathExported\":false}\n"
"}\n")
file(WRITE "${BUILD_DIR}/e2e_rollout_observability_evidence/e2e-rollout-observability.md"
"# E2E rollout observability\n\n- Release gate: `production-rollout-observability-ready`\n")

file(WRITE "${BUILD_DIR}/s3-real-backend-readiness.json" "{\n"
"  \"format\":\"qtnetworkchat-s3-real-backend-readiness-v1\",\n"
"  \"status\":\"verified\",\n"
"  \"ok\":true,\n"
"  \"summary\":{\"readiness\":\"verified\"},\n"
"  \"auditSummary\":{\"releaseGate\":\"can-review-s3-real-backend-evidence\"},\n"
"  \"realBackendDefaultCI\":true,\n"
"  \"defaultCIReleaseGate\":\"s3-real-backend-default-ci-ready\"\n"
"}\n")

file(MAKE_DIRECTORY "${BUILD_DIR}/automation-tasks/large-file-governance/diagnostics-package")
file(WRITE "${BUILD_DIR}/automation-tasks/large-file-governance/large-file-governance-dashboard.json" "{\n"
"  \"format\":\"qtnetworkchat-large-file-governance-dashboard-v1\",\n"
"  \"status\":\"healthy\",\n"
"  \"ok\":true,\n"
"  \"totalWarnings\":0,\n"
"  \"summary\":{\"readiness\":\"verified\"},\n"
"  \"auditSummary\":{\"releaseGate\":\"can-review-governance-evidence\"},\n"
"  \"performanceSummary\":{\"auditSummary\":{\"releaseGate\":\"review-governance-performance\"},\"summary\":{\"readiness\":\"review\"}}\n"
"}\n")
file(WRITE "${BUILD_DIR}/automation-tasks/large-file-governance/large-file-governance-report.md" "# Governance report\n")
file(WRITE "${BUILD_DIR}/automation-tasks/large-file-governance/large-file-governance-performance-summary.json" "{\n"
"  \"format\":\"qtnetworkchat-large-file-governance-performance-summary-v1\",\n"
"  \"status\":\"ready\",\n"
"  \"ok\":false,\n"
"  \"summary\":{\"readiness\":\"review\"},\n"
"  \"auditSummary\":{\"releaseGate\":\"review-governance-performance\"},\n"
"  \"bottlenecks\":[\"receipt-archive-pressure\"]\n"
"}\n")
file(WRITE "${BUILD_DIR}/automation-tasks/large-file-governance/diagnostics-package/large-file-governance-diagnostics.zip" "diagnostics")

file(MAKE_DIRECTORY "${BUILD_DIR}/automation-tasks/pgsql-release-acceptance/evidence")
file(WRITE "${BUILD_DIR}/automation-tasks/pgsql-release-acceptance/pgsql-release-acceptance.json" "{\n"
"  \"format\":\"qtnetworkchat-pgsql-release-acceptance-v1\",\n"
"  \"status\":\"ready\",\n"
"  \"ok\":true,\n"
"  \"summary\":{\"readiness\":\"ready\"},\n"
"  \"auditSummary\":{\"releaseGate\":\"can-review-cutover\",\"evidenceBundle\":[\"dashboard-json\"]}\n"
"}\n")
file(WRITE "${BUILD_DIR}/automation-tasks/pgsql-release-acceptance/evidence/pgsql-release-evidence-manifest.json" "{\n"
"  \"format\":\"qtnetworkchat-pgsql-release-evidence-package-v1\",\n"
"  \"ok\":true,\n"
"  \"inputCount\":10\n"
"}\n")

file(MAKE_DIRECTORY "${BUILD_DIR}/pgsql-rollback-live-evidence/evidence")
file(WRITE "${BUILD_DIR}/pgsql-rollback-live-evidence/pgsql-rollback-live-evidence.json" "{\n"
"  \"format\":\"qtnetworkchat-pgsql-rollback-live-evidence-v1\",\n"
"  \"status\":\"verified\",\n"
"  \"ok\":true,\n"
"  \"summary\":{\"readiness\":\"verified\",\"releaseGate\":\"can-close-pgsql-rollback-live-evidence\"}\n"
"}\n")
file(WRITE "${BUILD_DIR}/pgsql-rollback-live-evidence/evidence/pgsql-rollback-live-evidence-manifest.json" "{\n"
"  \"format\":\"qtnetworkchat-pgsql-release-evidence-package-v1\",\n"
"  \"ok\":true,\n"
"  \"inputCount\":8\n"
"}\n")

file(WRITE "${AUTOMATION_STATUS_PATH}" "# Automation Status\n\n- initial placeholder\n")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -BuildDir "${BUILD_DIR}"
        -AutomationStatusPath "${AUTOMATION_STATUS_PATH}"
        -ArchiveDecisionState "approved-local-archive"
        -ArchiveDecidedBy "refresh-test"
        -ArchiveDecisionReason "Current-head release closeout verified locally."
        -ArchivePublishingStatus "pending-environment-publication"
        -ArchivePublishingChannel "team-share"
        -RunDeliveryDrill
        -NoDeploy
    WORKING_DIRECTORY "${REPO_DIR}"
    RESULT_VARIABLE refresh_result
    OUTPUT_VARIABLE refresh_output
    ERROR_VARIABLE refresh_error
)
if(NOT refresh_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(STATUS "refresh stdout:\n${refresh_output}")
    message(STATUS "refresh stderr:\n${refresh_error}")
    message(FATAL_ERROR "refresh-release-closeout.ps1 exited with code ${refresh_result}")
endif()

set(LOCAL_VERIFICATION_PATH "${BUILD_DIR}/local-verification-status.json")
set(LINKED_MANIFEST_PATH "${BUILD_DIR}/e2e_release_evidence_linked_candidate/e2e-release-evidence-manifest.json")
set(LINKED_PROMOTION_PATH "${BUILD_DIR}/e2e_release_evidence_linked_candidate/e2e-release-promotion.json")
set(LOCAL_REVIEW_MANIFEST_PATH "${BUILD_DIR}/local-release-review/local-release-review-manifest.json")
set(ARCHIVE_DECISION_MANIFEST_PATH "${BUILD_DIR}/release-archive-decision/release-archive-decision-manifest.json")
set(DELIVERY_HANDOFF_MANIFEST_PATH "${BUILD_DIR}/release-delivery-handoff/release-delivery-handoff-manifest.json")
set(CLOSEOUT_SUMMARY_MANIFEST_PATH "${BUILD_DIR}/release-closeout-summary/release-closeout-summary-manifest.json")
set(FINAL_LOCAL_ARCHIVE_MANIFEST_PATH "${BUILD_DIR}/release-final-local-archive/release-final-local-archive-manifest.json")
set(WINDOWS_MANIFEST_PATH "${BUILD_DIR}/release-package/QtNetworkChat-1.0.0-win-x64/manifest.json")
set(PUBLICATION_RECORD_PATH "${BUILD_DIR}/release-publication-record.json")
set(DELIVERY_DRILL_MANIFEST_PATH "${BUILD_DIR}/release-delivery-drill/release-delivery-drill-manifest.json")

foreach(required_path
    IN ITEMS
        "${LOCAL_VERIFICATION_PATH}"
        "${LINKED_MANIFEST_PATH}"
        "${LINKED_PROMOTION_PATH}"
        "${LOCAL_REVIEW_MANIFEST_PATH}"
        "${ARCHIVE_DECISION_MANIFEST_PATH}"
        "${DELIVERY_HANDOFF_MANIFEST_PATH}"
        "${CLOSEOUT_SUMMARY_MANIFEST_PATH}"
        "${FINAL_LOCAL_ARCHIVE_MANIFEST_PATH}"
        "${WINDOWS_MANIFEST_PATH}"
        "${PUBLICATION_RECORD_PATH}"
        "${DELIVERY_DRILL_MANIFEST_PATH}"
        "${AUTOMATION_STATUS_PATH}")
    if(NOT EXISTS "${required_path}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Expected refresh-release-closeout output missing: ${required_path}")
    endif()
endforeach()

file(READ "${LOCAL_VERIFICATION_PATH}" local_verification_content)
string(JSON local_ctest_count GET "${local_verification_content}" "ctest" "count")
string(JSON local_build_status GET "${local_verification_content}" "build" "status")
string(JSON local_ctest_status GET "${local_verification_content}" "ctest" "status")
if(NOT local_ctest_count EQUAL 86 OR NOT local_build_status STREQUAL "passed" OR NOT local_ctest_status STREQUAL "passed")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "refresh-release-closeout should rewrite local verification status with 86 passing tests")
endif()

file(READ "${LINKED_MANIFEST_PATH}" linked_manifest_content)
file(READ "${LINKED_PROMOTION_PATH}" linked_promotion_content)
string(JSON linked_target_head GET "${linked_manifest_content}" "targetReleaseHead")
string(JSON linked_release_ready GET "${linked_manifest_content}" "releaseReady")
string(JSON linked_release_gate GET "${linked_manifest_content}" "releaseGate")
string(JSON linked_local_ctest_count GET "${linked_manifest_content}" "localVerification" "ctestCount")
string(JSON linked_local_build_status GET "${linked_manifest_content}" "localVerification" "buildStatus")
string(JSON linked_local_ctest_status GET "${linked_manifest_content}" "localVerification" "ctestStatus")
string(JSON linked_production_ready GET "${linked_manifest_content}" "productionLinkedEvidence" "ready")
string(JSON linked_production_gate GET "${linked_manifest_content}" "productionLinkedEvidence" "releaseGate")
string(JSON linked_promotion_ready GET "${linked_promotion_content}" "promotionReady")
string(JSON linked_promotion_promoted GET "${linked_promotion_content}" "promoted")
string(JSON linked_promotion_gate GET "${linked_promotion_content}" "releaseGate")
string(JSON linked_promotion_blocker_count LENGTH "${linked_promotion_content}" "blockers")
if(NOT linked_target_head STREQUAL "${release_head}"
        OR NOT linked_release_ready
        OR NOT linked_release_gate STREQUAL "ready-local-verification-only"
        OR NOT linked_local_ctest_count EQUAL 86
        OR NOT linked_local_build_status STREQUAL "passed"
        OR NOT linked_local_ctest_status STREQUAL "passed"
        OR NOT linked_production_ready
        OR NOT linked_production_gate STREQUAL "production-linked-rollout-ready"
        OR NOT linked_promotion_ready
        OR NOT linked_promotion_promoted
        OR NOT linked_promotion_gate STREQUAL "ready-local-verification-only"
        OR NOT linked_promotion_blocker_count EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "refresh-release-closeout should refresh linked current-head E2E release evidence from live build artifacts")
endif()

file(READ "${WINDOWS_MANIFEST_PATH}" windows_manifest_content)
string(JSON windows_git_commit GET "${windows_manifest_content}" "gitCommit")
string(JSON windows_runtime_ok GET "${windows_manifest_content}" "runtimeCheck" "ok")
if(NOT windows_git_commit STREQUAL "${release_head_short}" OR NOT windows_runtime_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "refresh-release-closeout should package a current-head Windows manifest")
endif()

file(READ "${LOCAL_REVIEW_MANIFEST_PATH}" local_review_content)
string(JSON local_review_target_head GET "${local_review_content}" "targetReleaseHead")
string(JSON local_review_ready GET "${local_review_content}" "reviewReady")
string(JSON local_review_gate GET "${local_review_content}" "reviewGate")
string(JSON local_review_decision_recorded GET "${local_review_content}" "finalArchiveDecision" "recorded")
string(JSON local_review_decision_state GET "${local_review_content}" "finalArchiveDecision" "decisionState")
string(JSON local_review_blocker_count GET "${local_review_content}" "finalArchiveDecision" "blockerCount")
if(NOT local_review_target_head STREQUAL "${release_head}"
        OR NOT local_review_ready
        OR NOT local_review_gate STREQUAL "review-complete-archive-decision-recorded"
        OR NOT local_review_decision_recorded
        OR NOT local_review_decision_state STREQUAL "approved-local-archive"
        OR NOT local_review_blocker_count EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "refresh-release-closeout should regenerate local release review against the current HEAD")
endif()

file(READ "${ARCHIVE_DECISION_MANIFEST_PATH}" archive_decision_content)
string(JSON archive_target_head GET "${archive_decision_content}" "targetReleaseHead")
string(JSON archive_decision_recorded GET "${archive_decision_content}" "decisionRecorded")
string(JSON archive_decision_state GET "${archive_decision_content}" "decisionState")
string(JSON archive_decision_gate GET "${archive_decision_content}" "decisionGate")
string(JSON archive_publishing_status GET "${archive_decision_content}" "publishingStatus")
if(NOT archive_target_head STREQUAL "${release_head}"
        OR NOT archive_decision_recorded
        OR NOT archive_decision_state STREQUAL "approved-local-archive"
        OR NOT archive_decision_gate STREQUAL "archive-decision-recorded-publication-pending"
        OR NOT archive_publishing_status STREQUAL "pending-environment-publication")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "refresh-release-closeout should record the current-head archive decision state")
endif()

file(READ "${DELIVERY_HANDOFF_MANIFEST_PATH}" delivery_handoff_content)
string(JSON delivery_target_head GET "${delivery_handoff_content}" "targetReleaseHead")
string(JSON delivery_ready GET "${delivery_handoff_content}" "deliveryReady")
string(JSON delivery_gate GET "${delivery_handoff_content}" "deliveryGate")
string(JSON delivery_tail_count GET "${delivery_handoff_content}" "deliveryTailCount")
if(NOT delivery_target_head STREQUAL "${release_head}"
        OR NOT delivery_ready
        OR NOT delivery_gate STREQUAL "ready-local-delivery-handoff"
        OR NOT delivery_tail_count EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "refresh-release-closeout should regenerate a current-head delivery handoff with zero tail items")
endif()

file(READ "${CLOSEOUT_SUMMARY_MANIFEST_PATH}" closeout_summary_content)
string(JSON closeout_release_head GET "${closeout_summary_content}" "releaseHead")
string(JSON closeout_ready GET "${closeout_summary_content}" "closeoutReady")
string(JSON closeout_gate GET "${closeout_summary_content}" "closeoutGate")
string(JSON closeout_publication_status GET "${closeout_summary_content}" "releasePublicationRecord" "publishingStatus")
string(JSON closeout_diagnostics_ok GET "${closeout_summary_content}" "releaseDiagnostics" "ok")
string(JSON closeout_drill_ok GET "${closeout_summary_content}" "releaseDeliveryDrill" "ok")
if(NOT closeout_release_head STREQUAL "${release_head}"
        OR NOT closeout_ready
        OR NOT closeout_gate STREQUAL "release-closeout-ready-for-stop-writing"
        OR NOT closeout_publication_status STREQUAL "pending-environment-publication"
        OR NOT closeout_diagnostics_ok
        OR NOT closeout_drill_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "refresh-release-closeout should regenerate a ready release closeout summary for the current HEAD")
endif()

file(READ "${FINAL_LOCAL_ARCHIVE_MANIFEST_PATH}" final_local_archive_content)
string(JSON final_archive_release_head GET "${final_local_archive_content}" "releaseHead")
string(JSON final_archive_ready GET "${final_local_archive_content}" "archiveReady")
string(JSON final_archive_gate GET "${final_local_archive_content}" "archiveGate")
string(JSON final_archive_decision_state GET "${final_local_archive_content}" "decisionState")
string(JSON final_archive_publishing_status GET "${final_local_archive_content}" "publishingStatus")
string(JSON final_archive_closeout_ready GET "${final_local_archive_content}" "closeoutReady")
if(NOT final_archive_release_head STREQUAL "${release_head}"
        OR NOT final_archive_ready
        OR NOT final_archive_gate STREQUAL "ready-final-local-archive"
        OR NOT final_archive_decision_state STREQUAL "approved-local-archive"
        OR NOT final_archive_publishing_status STREQUAL "pending-environment-publication"
        OR NOT final_archive_closeout_ready)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "refresh-release-closeout should regenerate a ready final local archive for the current HEAD")
endif()

file(READ "${PUBLICATION_RECORD_PATH}" publication_record_content)
string(JSON publication_channel GET "${publication_record_content}" "channel")
string(JSON publication_status GET "${publication_record_content}" "publishingStatus")
if(NOT publication_channel STREQUAL "team-share" OR NOT publication_status STREQUAL "pending-environment-publication")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "refresh-release-closeout should rewrite the sanitized publication record")
endif()

file(READ "${DELIVERY_DRILL_MANIFEST_PATH}" delivery_drill_content)
string(JSON delivery_drill_ok GET "${delivery_drill_content}" "ok")
string(JSON delivery_drill_head GET "${delivery_drill_content}" "releaseHead")
if(NOT delivery_drill_ok OR NOT delivery_drill_head STREQUAL "${release_head}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "refresh-release-closeout should run a delivery drill against the current HEAD package")
endif()

file(READ "${AUTOMATION_STATUS_PATH}" automation_status_content)
foreach(expected_text
        "- GitHub run id: `not-required`"
        "count=`86`"
        "source=`release-review-current-head-closeout`"
        "productionLinked=`true`"
        "currentHead=`${release_head}`"
        "targetReleaseHead=`${release_head}`"
        "Release archive decision artifacts: `manifest=ok; decisionRecorded=true; decisionState=approved-local-archive; decisionGate=archive-decision-recorded-publication-pending; publishing=pending-environment-publication; publishingRecord=true; deliveryDrill=true; blockers=0`"
        "Release delivery handoff artifacts: `manifest=ok; deliveryReady=true; deliveryGate=ready-local-delivery-handoff; deliveryTail=0`"
        "Release closeout summary artifacts: `manifest=ok; closeoutReady=true; closeoutGate=release-closeout-ready-for-stop-writing; publishing=pending-environment-publication; diagnosticsOk=true`"
        "Final local archive artifacts: `manifest=ok; archiveReady=true; archiveGate=ready-final-local-archive; decisionState=approved-local-archive; publishing=pending-environment-publication; closeoutReady=true`")
    string(FIND "${automation_status_content}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "refresh-release-closeout automation status missing expected text: ${expected_text}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "refresh-release-closeout test passed")
