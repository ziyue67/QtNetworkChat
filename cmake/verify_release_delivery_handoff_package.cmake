if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/release_delivery_handoff_package")
set(OUTPUT_DIR "${TEMP_DIR}/out")
set(EXTRACT_DIR "${TEMP_DIR}/extract")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}" "${OUTPUT_DIR}" "${EXTRACT_DIR}")

set(README_PATH "${TEMP_DIR}/README.md")
set(AUTOMATION_STATUS_PATH "${TEMP_DIR}/automation-status.md")
set(LOCAL_VERIFICATION_PATH "${TEMP_DIR}/local-verification-status.json")
set(LOCAL_RELEASE_REVIEW_DIR "${TEMP_DIR}/local-release-review")
set(LOCAL_RELEASE_REVIEW_MANIFEST_PATH "${LOCAL_RELEASE_REVIEW_DIR}/local-release-review-manifest.json")
set(LOCAL_RELEASE_REVIEW_PACKAGE_PATH "${LOCAL_RELEASE_REVIEW_DIR}/local-release-review.zip")
set(RELEASE_ARCHIVE_DECISION_MANIFEST_PATH "${TEMP_DIR}/release-archive-decision-manifest.json")
set(RELEASE_ARCHIVE_DECISION_MARKDOWN_PATH "${TEMP_DIR}/release-archive-decision.md")
set(RELEASE_PUBLICATION_RECORD_PATH "${TEMP_DIR}/release-publication-record.json")
set(RELEASE_DELIVERY_DRILL_MANIFEST_PATH "${TEMP_DIR}/release-delivery-drill-manifest.json")
set(WINDOWS_PACKAGE_ROOT "${TEMP_DIR}/release-package")
set(WINDOWS_STAGE_DIR "${WINDOWS_PACKAGE_ROOT}/QtNetworkChat-1.0.0-win-x64")
set(WINDOWS_MANIFEST_PATH "${WINDOWS_STAGE_DIR}/manifest.json")
set(WINDOWS_ZIP_PATH "${WINDOWS_PACKAGE_ROOT}/QtNetworkChat-1.0.0-win-x64.zip")
set(INSTALLER_SCRIPT_PATH "${REPO_ROOT}/scripts/install-qtnetworkchat-package.ps1")
set(DIAGNOSTICS_SCRIPT_PATH "${REPO_ROOT}/scripts/collect-qtnetworkchat-diagnostics.ps1")

file(MAKE_DIRECTORY "${LOCAL_RELEASE_REVIEW_DIR}" "${WINDOWS_STAGE_DIR}")
file(WRITE "${README_PATH}" "# Readme\n")
file(WRITE "${AUTOMATION_STATUS_PATH}" "# Automation Status\n")
file(WRITE "${LOCAL_VERIFICATION_PATH}" "{\n  \"format\":\"qtnetworkchat-local-verification-status-v1\",\n  \"ok\":true,\n  \"build\":{\"status\":\"passed\"},\n  \"ctest\":{\"status\":\"passed\",\"count\":81},\n  \"sensitiveExportProof\":{\"noSensitiveExportProof\":true}\n}\n")
file(WRITE "${LOCAL_RELEASE_REVIEW_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-local-release-review-package-v1\",\n  \"reviewReady\":true,\n  \"reviewGate\":\"ready-for-final-archive-decision\",\n  \"packageSha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"\n}\n")
file(WRITE "${LOCAL_RELEASE_REVIEW_PACKAGE_PATH}" "fake local release review zip\n")
file(WRITE "${RELEASE_ARCHIVE_DECISION_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-release-archive-decision-v1\",\n  \"decisionRecorded\":false,\n  \"decisionState\":\"pending-human-decision\",\n  \"decisionGate\":\"ready-for-archive-decision-record\",\n  \"publishingRequired\":false,\n  \"publishingStatus\":\"not-started\",\n  \"packageSha256\":\"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\",\n  \"blockers\":[]\n}\n")
file(WRITE "${RELEASE_ARCHIVE_DECISION_MARKDOWN_PATH}" "# Release Archive Decision\n")
file(WRITE "${RELEASE_PUBLICATION_RECORD_PATH}" "{\n  \"format\":\"qtnetworkchat-release-publication-record-v1\",\n  \"channel\":\"team-share\",\n  \"publishingStatus\":\"pending-environment-publication\"\n}\n")
file(WRITE "${RELEASE_DELIVERY_DRILL_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-release-delivery-drill-v1\",\n  \"ok\":true\n}\n")
file(WRITE "${WINDOWS_MANIFEST_PATH}" "{\n  \"packageFormat\":\"qtnetworkchat-windows-package-v1\",\n  \"gitCommit\":\"abc123\",\n  \"runtimeCheck\":{\"ok\":true},\n  \"postgresSqlRuntime\":{\"ok\":true}\n}\n")
file(WRITE "${WINDOWS_ZIP_PATH}" "fake windows zip\n")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}"
        -ReleaseHead "abc123"
        -ReadmePath "${README_PATH}"
        -AutomationStatusPath "${AUTOMATION_STATUS_PATH}"
        -LocalVerificationStatusPath "${LOCAL_VERIFICATION_PATH}"
        -LocalReleaseReviewManifestPath "${LOCAL_RELEASE_REVIEW_MANIFEST_PATH}"
        -LocalReleaseReviewPackagePath "${LOCAL_RELEASE_REVIEW_PACKAGE_PATH}"
        -ReleaseArchiveDecisionManifestPath "${RELEASE_ARCHIVE_DECISION_MANIFEST_PATH}"
        -ReleaseArchiveDecisionMarkdownPath "${RELEASE_ARCHIVE_DECISION_MARKDOWN_PATH}"
        -ReleasePublicationRecordPath "${RELEASE_PUBLICATION_RECORD_PATH}"
        -ReleaseDeliveryDrillManifestPath "${RELEASE_DELIVERY_DRILL_MANIFEST_PATH}"
        -WindowsPackageManifestPath "${WINDOWS_MANIFEST_PATH}"
        -WindowsPackageZipPath "${WINDOWS_ZIP_PATH}"
        -InstallerScriptPath "${INSTALLER_SCRIPT_PATH}"
        -DiagnosticsScriptPath "${DIAGNOSTICS_SCRIPT_PATH}"
    WORKING_DIRECTORY "${REPO_ROOT}"
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
    message(FATAL_ERROR "package-release-delivery-handoff.ps1 exited with code ${result}")
endif()

set(PACKAGE_PATH "${OUTPUT_DIR}/release-delivery-handoff.zip")
set(MANIFEST_PATH "${OUTPUT_DIR}/release-delivery-handoff-manifest.json")
set(MARKDOWN_PATH "${OUTPUT_DIR}/release-delivery-handoff.md")
if(NOT EXISTS "${PACKAGE_PATH}" OR NOT EXISTS "${MANIFEST_PATH}" OR NOT EXISTS "${MARKDOWN_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected release delivery handoff artifacts were not created")
endif()

file(READ "${MANIFEST_PATH}" manifest_content)
string(JSON format GET "${manifest_content}" "format")
string(JSON delivery_ready GET "${manifest_content}" "deliveryReady")
string(JSON delivery_gate GET "${manifest_content}" "deliveryGate")
string(JSON package_sha256 GET "${manifest_content}" "packageSha256")
string(JSON windows_current_head_match GET "${manifest_content}" "windowsPackage" "currentHeadMatch")
string(JSON archive_recorded GET "${manifest_content}" "releaseArchiveDecision" "recorded")
string(JSON archive_gate GET "${manifest_content}" "releaseArchiveDecision" "decisionGate")
string(JSON publication_present GET "${manifest_content}" "releasePublicationRecord" "present")
string(JSON drill_present GET "${manifest_content}" "releaseDeliveryDrill" "present")
string(JSON upload_plan_ready GET "${manifest_content}" "components" "uploadPlanReady")
string(JSON installer_ready GET "${manifest_content}" "components" "installerReady")
string(JSON diagnostics_ready GET "${manifest_content}" "components" "diagnosticsReady")
string(JSON ops_ready GET "${manifest_content}" "components" "opsHandoffReady")
string(JSON tail_count GET "${manifest_content}" "deliveryTailCount")
string(JSON proof_no_sensitive GET "${manifest_content}" "sensitiveExportProof" "noSensitiveExportProof")
string(LENGTH "${package_sha256}" package_sha256_length)

if(NOT format STREQUAL "qtnetworkchat-release-delivery-handoff-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected release delivery handoff manifest format: ${format}")
endif()
if(NOT delivery_ready OR NOT delivery_gate STREQUAL "ready-local-delivery-handoff")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected ready release delivery handoff gate")
endif()
if(NOT windows_current_head_match OR NOT upload_plan_ready OR NOT installer_ready OR NOT diagnostics_ready OR NOT ops_ready OR archive_recorded OR NOT archive_gate STREQUAL "ready-for-archive-decision-record" OR NOT publication_present OR NOT drill_present)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected all release delivery handoff components, publication record, and drill evidence to be ready while archive decision remains pending")
endif()
if(NOT tail_count EQUAL 0 OR NOT proof_no_sensitive OR NOT package_sha256_length EQUAL 64 OR NOT package_sha256 MATCHES "^[0-9a-f]+$")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release delivery handoff manifest did not preserve expected package metadata")
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
    message(FATAL_ERROR "Failed to extract release delivery handoff package: ${extract_error}")
endif()
foreach(required_path
    IN ITEMS
        "${EXTRACT_DIR}/manifest.json"
        "${EXTRACT_DIR}/release-delivery-handoff.md"
        "${EXTRACT_DIR}/release-upload-plan.json"
        "${EXTRACT_DIR}/release-upload-plan.md"
        "${EXTRACT_DIR}/ops-handoff.json"
        "${EXTRACT_DIR}/ops-handoff.md"
        "${EXTRACT_DIR}/tools/install-qtnetworkchat-package.ps1"
        "${EXTRACT_DIR}/tools/collect-qtnetworkchat-diagnostics.ps1"
        "${EXTRACT_DIR}/archive/release-archive-decision-manifest.json"
        "${EXTRACT_DIR}/archive/release-publication-record.json"
        "${EXTRACT_DIR}/archive/release-delivery-drill-manifest.json"
        "${EXTRACT_DIR}/windows/manifest.json")
    if(NOT EXISTS "${required_path}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Extracted release delivery handoff package is missing: ${required_path}")
    endif()
endforeach()

set(STALE_WINDOWS_MANIFEST_PATH "${TEMP_DIR}/stale-manifest.json")
file(WRITE "${STALE_WINDOWS_MANIFEST_PATH}" "{\n  \"packageFormat\":\"qtnetworkchat-windows-package-v1\",\n  \"gitCommit\":\"old-sample-head\",\n  \"runtimeCheck\":{\"ok\":true},\n  \"postgresSqlRuntime\":{\"ok\":true}\n}\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/blocked"
        -ReleaseHead "abc123"
        -ReadmePath "${README_PATH}"
        -AutomationStatusPath "${AUTOMATION_STATUS_PATH}"
        -LocalVerificationStatusPath "${LOCAL_VERIFICATION_PATH}"
        -LocalReleaseReviewManifestPath "${LOCAL_RELEASE_REVIEW_MANIFEST_PATH}"
        -LocalReleaseReviewPackagePath "${LOCAL_RELEASE_REVIEW_PACKAGE_PATH}"
        -WindowsPackageManifestPath "${STALE_WINDOWS_MANIFEST_PATH}"
        -WindowsPackageZipPath "${WINDOWS_ZIP_PATH}"
        -InstallerScriptPath "${INSTALLER_SCRIPT_PATH}"
        -DiagnosticsScriptPath "${DIAGNOSTICS_SCRIPT_PATH}"
    WORKING_DIRECTORY "${REPO_ROOT}"
    RESULT_VARIABLE blocked_result
    OUTPUT_VARIABLE blocked_output
    ERROR_VARIABLE blocked_error
)
if(NOT blocked_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Blocked release delivery handoff package run failed unexpectedly: ${blocked_error}")
endif()
file(READ "${OUTPUT_DIR}/blocked/release-delivery-handoff-manifest.json" blocked_manifest)
string(JSON blocked_ready GET "${blocked_manifest}" "deliveryReady")
string(JSON blocked_tail0 GET "${blocked_manifest}" "deliveryTailPending" 0)
if(blocked_ready OR NOT blocked_tail0 STREQUAL "release-auto-upload-not-ready")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Blocked release delivery handoff package should preserve the upload blocker")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Release delivery handoff package test passed")
