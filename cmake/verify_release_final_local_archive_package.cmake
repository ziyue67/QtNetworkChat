if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/release_final_local_archive_package")
set(OUTPUT_DIR "${TEMP_DIR}/out")
set(EXTRACT_DIR "${TEMP_DIR}/extract")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}" "${OUTPUT_DIR}" "${EXTRACT_DIR}")

set(README_PATH "${TEMP_DIR}/README.md")
set(AUTOMATION_STATUS_PATH "${TEMP_DIR}/automation-status.md")
set(RELEASE_CLOSEOUT_DOC_PATH "${TEMP_DIR}/release-closeout.md")
set(E2E_HARDENING_PATH "${TEMP_DIR}/e2e-hardening-status.md")
set(LOCAL_RELEASE_REVIEW_MANIFEST_PATH "${TEMP_DIR}/local-release-review-manifest.json")
set(LOCAL_RELEASE_REVIEW_MARKDOWN_PATH "${TEMP_DIR}/local-release-review.md")
set(LOCAL_RELEASE_REVIEW_PACKAGE_PATH "${TEMP_DIR}/local-release-review.zip")
set(RELEASE_ARCHIVE_DECISION_MANIFEST_PATH "${TEMP_DIR}/release-archive-decision-manifest.json")
set(RELEASE_ARCHIVE_DECISION_MARKDOWN_PATH "${TEMP_DIR}/release-archive-decision.md")
set(RELEASE_ARCHIVE_DECISION_PACKAGE_PATH "${TEMP_DIR}/release-archive-decision.zip")
set(RELEASE_DELIVERY_HANDOFF_MANIFEST_PATH "${TEMP_DIR}/release-delivery-handoff-manifest.json")
set(RELEASE_DELIVERY_HANDOFF_MARKDOWN_PATH "${TEMP_DIR}/release-delivery-handoff.md")
set(RELEASE_DELIVERY_HANDOFF_PACKAGE_PATH "${TEMP_DIR}/release-delivery-handoff.zip")
set(RELEASE_PUBLICATION_RECORD_PATH "${TEMP_DIR}/release-publication-record.json")
set(RELEASE_DELIVERY_DRILL_MANIFEST_PATH "${TEMP_DIR}/release-delivery-drill-manifest.json")
set(RELEASE_DELIVERY_DRILL_MARKDOWN_PATH "${TEMP_DIR}/release-delivery-drill.md")
set(RELEASE_DIAGNOSTICS_MANIFEST_PATH "${TEMP_DIR}/release-diagnostics-manifest.json")
set(RELEASE_DIAGNOSTICS_PACKAGE_PATH "${TEMP_DIR}/release-diagnostics.zip")
set(RELEASE_CLOSEOUT_SUMMARY_MANIFEST_PATH "${TEMP_DIR}/release-closeout-summary-manifest.json")
set(RELEASE_CLOSEOUT_SUMMARY_MARKDOWN_PATH "${TEMP_DIR}/release-closeout-summary.md")
set(RELEASE_CLOSEOUT_SUMMARY_PACKAGE_PATH "${TEMP_DIR}/release-closeout-summary.zip")

file(WRITE "${README_PATH}" "# Readme\n")
file(WRITE "${AUTOMATION_STATUS_PATH}" "# Automation Status\n")
file(WRITE "${RELEASE_CLOSEOUT_DOC_PATH}" "# Release Closeout\n")
file(WRITE "${E2E_HARDENING_PATH}" "# E2E Hardening\n")
file(WRITE "${LOCAL_RELEASE_REVIEW_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-local-release-review-package-v1\",\n  \"reviewReady\":true,\n  \"reviewGate\":\"review-complete-archive-decision-recorded\",\n  \"targetReleaseHead\":\"abc123\",\n  \"packageSha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"\n}\n")
file(WRITE "${LOCAL_RELEASE_REVIEW_MARKDOWN_PATH}" "# Local Release Review\n")
file(WRITE "${LOCAL_RELEASE_REVIEW_PACKAGE_PATH}" "local release review zip\n")
file(WRITE "${RELEASE_ARCHIVE_DECISION_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-release-archive-decision-v1\",\n  \"decisionRecorded\":true,\n  \"decisionState\":\"approved-local-archive\",\n  \"decisionGate\":\"archive-decision-recorded-publication-pending\",\n  \"publishingRequired\":true,\n  \"publishingStatus\":\"pending-environment-publication\",\n  \"publishingRecordPresent\":true,\n  \"releaseDeliveryDrillPresent\":true,\n  \"targetReleaseHead\":\"abc123\",\n  \"packageSha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\n  \"blockers\":[]\n}\n")
file(WRITE "${RELEASE_ARCHIVE_DECISION_MARKDOWN_PATH}" "# Release Archive Decision\n")
file(WRITE "${RELEASE_ARCHIVE_DECISION_PACKAGE_PATH}" "archive decision zip\n")
file(WRITE "${RELEASE_DELIVERY_HANDOFF_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-release-delivery-handoff-v1\",\n  \"deliveryReady\":true,\n  \"deliveryGate\":\"ready-local-delivery-handoff\",\n  \"deliveryTailCount\":0,\n  \"targetReleaseHead\":\"abc123\",\n  \"packageSha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"\n}\n")
file(WRITE "${RELEASE_DELIVERY_HANDOFF_MARKDOWN_PATH}" "# Release Delivery Handoff\n")
file(WRITE "${RELEASE_DELIVERY_HANDOFF_PACKAGE_PATH}" "delivery handoff zip\n")
file(WRITE "${RELEASE_PUBLICATION_RECORD_PATH}" "{\n  \"format\":\"qtnetworkchat-release-publication-record-v1\",\n  \"channel\":\"team-share\",\n  \"publishingStatus\":\"pending-environment-publication\"\n}\n")
file(WRITE "${RELEASE_DELIVERY_DRILL_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-release-delivery-drill-v1\",\n  \"ok\":true,\n  \"releaseHead\":\"abc123\"\n}\n")
file(WRITE "${RELEASE_DELIVERY_DRILL_MARKDOWN_PATH}" "# Release Delivery Drill\n")
file(WRITE "${RELEASE_DIAGNOSTICS_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-release-diagnostics-package-v1\",\n  \"ok\":true,\n  \"dumpCount\":4,\n  \"inputCount\":11\n}\n")
file(WRITE "${RELEASE_DIAGNOSTICS_PACKAGE_PATH}" "diagnostics zip\n")
file(WRITE "${RELEASE_CLOSEOUT_SUMMARY_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-release-closeout-summary-v1\",\n  \"closeoutReady\":true,\n  \"closeoutGate\":\"release-closeout-ready-for-stop-writing\",\n  \"releaseHead\":\"abc123\",\n  \"releasePublicationRecord\":{\"publishingStatus\":\"pending-environment-publication\"},\n  \"releaseDiagnostics\":{\"ok\":true},\n  \"releaseDeliveryDrill\":{\"ok\":true},\n  \"packageSha256\":\"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\"\n}\n")
file(WRITE "${RELEASE_CLOSEOUT_SUMMARY_MARKDOWN_PATH}" "# Release Closeout Summary\n")
file(WRITE "${RELEASE_CLOSEOUT_SUMMARY_PACKAGE_PATH}" "closeout summary zip\n")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}"
        -ReleaseHead "abc123"
        -ReadmePath "${README_PATH}"
        -AutomationStatusPath "${AUTOMATION_STATUS_PATH}"
        -ReleaseCloseoutDocPath "${RELEASE_CLOSEOUT_DOC_PATH}"
        -E2EHardeningStatusPath "${E2E_HARDENING_PATH}"
        -LocalReleaseReviewManifestPath "${LOCAL_RELEASE_REVIEW_MANIFEST_PATH}"
        -LocalReleaseReviewMarkdownPath "${LOCAL_RELEASE_REVIEW_MARKDOWN_PATH}"
        -LocalReleaseReviewPackagePath "${LOCAL_RELEASE_REVIEW_PACKAGE_PATH}"
        -ReleaseArchiveDecisionManifestPath "${RELEASE_ARCHIVE_DECISION_MANIFEST_PATH}"
        -ReleaseArchiveDecisionMarkdownPath "${RELEASE_ARCHIVE_DECISION_MARKDOWN_PATH}"
        -ReleaseArchiveDecisionPackagePath "${RELEASE_ARCHIVE_DECISION_PACKAGE_PATH}"
        -ReleaseDeliveryHandoffManifestPath "${RELEASE_DELIVERY_HANDOFF_MANIFEST_PATH}"
        -ReleaseDeliveryHandoffMarkdownPath "${RELEASE_DELIVERY_HANDOFF_MARKDOWN_PATH}"
        -ReleaseDeliveryHandoffPackagePath "${RELEASE_DELIVERY_HANDOFF_PACKAGE_PATH}"
        -ReleasePublicationRecordPath "${RELEASE_PUBLICATION_RECORD_PATH}"
        -ReleaseDeliveryDrillManifestPath "${RELEASE_DELIVERY_DRILL_MANIFEST_PATH}"
        -ReleaseDeliveryDrillMarkdownPath "${RELEASE_DELIVERY_DRILL_MARKDOWN_PATH}"
        -ReleaseDiagnosticsManifestPath "${RELEASE_DIAGNOSTICS_MANIFEST_PATH}"
        -ReleaseDiagnosticsPackagePath "${RELEASE_DIAGNOSTICS_PACKAGE_PATH}"
        -ReleaseCloseoutSummaryManifestPath "${RELEASE_CLOSEOUT_SUMMARY_MANIFEST_PATH}"
        -ReleaseCloseoutSummaryMarkdownPath "${RELEASE_CLOSEOUT_SUMMARY_MARKDOWN_PATH}"
        -ReleaseCloseoutSummaryPackagePath "${RELEASE_CLOSEOUT_SUMMARY_PACKAGE_PATH}"
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
    message(FATAL_ERROR "package-release-final-local-archive.ps1 exited with code ${result}")
endif()

set(PACKAGE_PATH "${OUTPUT_DIR}/release-final-local-archive.zip")
set(MANIFEST_PATH "${OUTPUT_DIR}/release-final-local-archive-manifest.json")
set(MARKDOWN_PATH "${OUTPUT_DIR}/release-final-local-archive.md")
if(NOT EXISTS "${PACKAGE_PATH}" OR NOT EXISTS "${MANIFEST_PATH}" OR NOT EXISTS "${MARKDOWN_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected release final local archive artifacts were not created")
endif()

file(READ "${MANIFEST_PATH}" manifest_content)
string(JSON format GET "${manifest_content}" "format")
string(JSON archive_ready GET "${manifest_content}" "archiveReady")
string(JSON archive_gate GET "${manifest_content}" "archiveGate")
string(JSON decision_state GET "${manifest_content}" "decisionState")
string(JSON publishing_status GET "${manifest_content}" "publishingStatus")
string(JSON closeout_ready GET "${manifest_content}" "closeoutReady")
string(JSON input_count GET "${manifest_content}" "inputCount")
string(JSON package_sha256 GET "${manifest_content}" "packageSha256")
string(JSON proof_no_sensitive GET "${manifest_content}" "sensitiveExportProof" "noSensitiveExportProof")
string(LENGTH "${package_sha256}" package_sha256_length)

if(NOT format STREQUAL "qtnetworkchat-release-final-local-archive-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected release final local archive format: ${format}")
endif()
if(NOT archive_ready OR NOT archive_gate STREQUAL "ready-final-local-archive")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release final local archive should report a ready archive gate")
endif()
if(NOT decision_state STREQUAL "approved-local-archive"
        OR NOT publishing_status STREQUAL "pending-environment-publication"
        OR NOT closeout_ready
        OR NOT input_count EQUAL 21
        OR NOT proof_no_sensitive
        OR NOT package_sha256_length EQUAL 64
        OR NOT package_sha256 MATCHES "^[0-9a-f]+$")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release final local archive did not preserve the expected closeout state")
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
    message(FATAL_ERROR "Failed to extract release final local archive package: ${extract_error}")
endif()
foreach(required_path
    IN ITEMS
        "${EXTRACT_DIR}/manifest.json"
        "${EXTRACT_DIR}/release-final-local-archive.md"
        "${EXTRACT_DIR}/docs/README.md"
        "${EXTRACT_DIR}/docs/automation-status.md"
        "${EXTRACT_DIR}/docs/release-closeout.md"
        "${EXTRACT_DIR}/release-review/local-release-review-manifest.json"
        "${EXTRACT_DIR}/archive/release-archive-decision-manifest.json"
        "${EXTRACT_DIR}/delivery/release-delivery-handoff-manifest.json"
        "${EXTRACT_DIR}/publishing/release-publication-record.json"
        "${EXTRACT_DIR}/delivery-drill/release-delivery-drill-manifest.json"
        "${EXTRACT_DIR}/diagnostics/release-diagnostics-manifest.json"
        "${EXTRACT_DIR}/closeout/release-closeout-summary-manifest.json")
    if(NOT EXISTS "${required_path}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Extracted release final local archive package is missing: ${required_path}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Release final local archive package test passed")
