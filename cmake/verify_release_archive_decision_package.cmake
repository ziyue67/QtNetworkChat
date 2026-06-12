if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/release_archive_decision_package")
set(OUTPUT_DIR "${TEMP_DIR}/out")
set(EXTRACT_DIR "${TEMP_DIR}/extract")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}" "${OUTPUT_DIR}" "${EXTRACT_DIR}")

set(README_PATH "${TEMP_DIR}/README.md")
set(LOCAL_RELEASE_REVIEW_MANIFEST_PATH "${TEMP_DIR}/local-release-review-manifest.json")
set(LOCAL_RELEASE_REVIEW_MARKDOWN_PATH "${TEMP_DIR}/local-release-review.md")
set(RELEASE_DELIVERY_HANDOFF_MANIFEST_PATH "${TEMP_DIR}/release-delivery-handoff-manifest.json")
set(RELEASE_DELIVERY_HANDOFF_MARKDOWN_PATH "${TEMP_DIR}/release-delivery-handoff.md")

file(WRITE "${README_PATH}" "# Readme\n")
file(WRITE "${LOCAL_RELEASE_REVIEW_MARKDOWN_PATH}" "# Local Release Review\n")
file(WRITE "${RELEASE_DELIVERY_HANDOFF_MARKDOWN_PATH}" "# Release Delivery Handoff\n")
file(WRITE "${LOCAL_RELEASE_REVIEW_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-local-release-review-package-v1\",\n  \"reviewReady\":true,\n  \"reviewGate\":\"ready-for-final-archive-decision\",\n  \"targetReleaseHead\":\"abc123\",\n  \"packageSha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"\n}\n")
file(WRITE "${RELEASE_DELIVERY_HANDOFF_MANIFEST_PATH}" "{\n  \"format\":\"qtnetworkchat-release-delivery-handoff-v1\",\n  \"deliveryReady\":true,\n  \"deliveryGate\":\"ready-local-delivery-handoff\",\n  \"targetReleaseHead\":\"abc123\",\n  \"deliveryTailCount\":0,\n  \"packageSha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"\n}\n")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}"
        -ReadmePath "${README_PATH}"
        -LocalReleaseReviewManifestPath "${LOCAL_RELEASE_REVIEW_MANIFEST_PATH}"
        -ReleaseDeliveryHandoffManifestPath "${RELEASE_DELIVERY_HANDOFF_MANIFEST_PATH}"
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
    message(FATAL_ERROR "package-release-archive-decision.ps1 exited with code ${result}")
endif()

set(PACKAGE_PATH "${OUTPUT_DIR}/release-archive-decision.zip")
set(MANIFEST_PATH "${OUTPUT_DIR}/release-archive-decision-manifest.json")
set(MARKDOWN_PATH "${OUTPUT_DIR}/release-archive-decision.md")
if(NOT EXISTS "${PACKAGE_PATH}" OR NOT EXISTS "${MANIFEST_PATH}" OR NOT EXISTS "${MARKDOWN_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected release archive decision artifacts were not created")
endif()

file(READ "${MANIFEST_PATH}" manifest_content)
string(JSON format GET "${manifest_content}" "format")
string(JSON decision_recorded GET "${manifest_content}" "decisionRecorded")
string(JSON decision_state GET "${manifest_content}" "decisionState")
string(JSON decision_gate GET "${manifest_content}" "decisionGate")
string(JSON publishing_required GET "${manifest_content}" "publishingRequired")
string(JSON publishing_status GET "${manifest_content}" "publishingStatus")
string(JSON blocker0 ERROR_VARIABLE blocker0_error GET "${manifest_content}" "blockers" 0)
string(JSON package_sha256 GET "${manifest_content}" "packageSha256")
string(JSON manifest_embedded GET "${manifest_content}" "manifestEmbedded")
string(JSON input_count GET "${manifest_content}" "inputCount")
string(JSON local_review_ready GET "${manifest_content}" "sourceArtifacts" "localReleaseReview" "ready")
string(JSON delivery_ready GET "${manifest_content}" "sourceArtifacts" "releaseDeliveryHandoff" "ready")
string(LENGTH "${package_sha256}" package_sha256_length)

if(NOT format STREQUAL "qtnetworkchat-release-archive-decision-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected release archive decision manifest format: ${format}")
endif()
if(decision_recorded OR NOT decision_state STREQUAL "pending-human-decision" OR NOT decision_gate STREQUAL "ready-for-archive-decision-record")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected pending archive decision state when no explicit decision is recorded")
endif()
if(publishing_required OR NOT publishing_status STREQUAL "not-started" OR NOT manifest_embedded OR NOT input_count EQUAL 5)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Archive decision manifest metadata mismatch for pending decision")
endif()
if(NOT local_review_ready OR NOT delivery_ready OR NOT package_sha256_length EQUAL 64 OR NOT package_sha256 MATCHES "^[0-9a-f]+$")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Archive decision manifest did not preserve source readiness or package checksum")
endif()
if(NOT blocker0_error)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Pending archive decision should not emit blockers for ready inputs")
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
    message(FATAL_ERROR "Failed to extract release archive decision package: ${extract_error}")
endif()
foreach(required_path
    IN ITEMS
        "${EXTRACT_DIR}/manifest.json"
        "${EXTRACT_DIR}/release-archive-decision.md"
        "${EXTRACT_DIR}/release-review/local-release-review-manifest.json"
        "${EXTRACT_DIR}/release-delivery/release-delivery-handoff-manifest.json")
    if(NOT EXISTS "${required_path}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Extracted release archive decision package is missing: ${required_path}")
    endif()
endforeach()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/approved"
        -ReadmePath "${README_PATH}"
        -LocalReleaseReviewManifestPath "${LOCAL_RELEASE_REVIEW_MANIFEST_PATH}"
        -ReleaseDeliveryHandoffManifestPath "${RELEASE_DELIVERY_HANDOFF_MANIFEST_PATH}"
        -ReleaseHead "abc123"
        -DecisionState "approved-local-archive"
        -DecidedBy "operator"
        -DecisionReason "ready"
        -PublishingStatus "pending-environment-publication"
    RESULT_VARIABLE approved_result
    OUTPUT_VARIABLE approved_output
    ERROR_VARIABLE approved_error
)
if(NOT approved_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Approved release archive decision package run failed unexpectedly: ${approved_error}")
endif()
file(READ "${OUTPUT_DIR}/approved/release-archive-decision-manifest.json" approved_manifest)
string(JSON approved_recorded GET "${approved_manifest}" "decisionRecorded")
string(JSON approved_gate GET "${approved_manifest}" "decisionGate")
string(JSON approved_state GET "${approved_manifest}" "decisionState")
string(JSON approved_publishing GET "${approved_manifest}" "publishingStatus")
if(NOT approved_recorded OR NOT approved_state STREQUAL "approved-local-archive" OR NOT approved_gate STREQUAL "archive-decision-recorded-publication-pending" OR NOT approved_publishing STREQUAL "pending-environment-publication")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Approved archive decision should preserve recorded local archive state")
endif()

set(BAD_README_PATH "${TEMP_DIR}/bad-readme.md")
file(WRITE "${BAD_README_PATH}" "Authorization: should-not-ship\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/bad"
        -ReadmePath "${BAD_README_PATH}"
        -LocalReleaseReviewManifestPath "${LOCAL_RELEASE_REVIEW_MANIFEST_PATH}"
        -ReleaseDeliveryHandoffManifestPath "${RELEASE_DELIVERY_HANDOFF_MANIFEST_PATH}"
        -ReleaseHead "abc123"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release archive decision package should fail when sensitive fields are present")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Release archive decision package test passed")
