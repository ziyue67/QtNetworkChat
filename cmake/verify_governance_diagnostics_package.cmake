if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/governance_diagnostics_package_sample")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(GOV_DIR "${TEMP_DIR}/governance")
set(PACKAGE_DIR "${TEMP_DIR}/package-output")
set(PACKAGE_ZIP "${PACKAGE_DIR}/diagnostics.zip")
set(NOTES "${TEMP_DIR}/notes.txt")
set(BAD_NOTES "${TEMP_DIR}/bad-notes.txt")
file(MAKE_DIRECTORY "${GOV_DIR}" "${PACKAGE_DIR}")

file(WRITE "${GOV_DIR}/governance-alert-overview.json"
"{\n"
"  \"ok\": false,\n"
"  \"totalWarnings\": 1,\n"
"  \"alertCount\": 3,\n"
"  \"alerts\": []\n"
"}\n"
)

file(WRITE "${GOV_DIR}/last-health.json"
"{\n"
"  \"status\": \"unhealthy\",\n"
"  \"reason\": \"overview.ok=false\",\n"
"  \"ok\": false,\n"
"  \"totalWarnings\": 1,\n"
"  \"alertCount\": 3\n"
"}\n"
)

file(WRITE "${GOV_DIR}/large-file-route-summary.json"
"{\"routeLineCount\":3,\"routeKeys\":2,\"sensitiveHits\":0}\n"
)
file(WRITE "${GOV_DIR}/large-file-route-alert-summary.json"
"{\"kind\":\"large-file-route-summary\",\"ok\":true,\"warnings\":[],\"metrics\":{\"routeLineCount\":3}}\n"
)
file(WRITE "${GOV_DIR}/s3-request-results-summary.json"
"{\"routeLineCount\":3,\"s3LineCount\":2,\"sensitiveHits\":0}\n"
)
file(WRITE "${GOV_DIR}/s3-request-results-alert-summary.json"
"{\"kind\":\"s3-request-results\",\"ok\":true,\"warnings\":[],\"metrics\":{\"s3LineCount\":2}}\n"
)
file(WRITE "${GOV_DIR}/s3-real-backend-evidence.json"
"{\"format\":\"qtnetworkchat-s3-real-backend-evidence-v1\",\"ok\":true,\"warnings\":[],\"metrics\":{\"s3LineCount\":2,\"successCount\":1,\"fixedFailureReasonCount\":1,\"sensitiveHits\":0}}\n"
)
file(WRITE "${GOV_DIR}/s3-real-backend-evidence.md"
"# QtNetworkChat S3 Real Backend Evidence\n"
"- S3 lines: `2`\n"
)
file(WRITE "${GOV_DIR}/s3-real-backend-evidence-alert-summary.json"
"{\"kind\":\"s3-real-backend-evidence\",\"ok\":true,\"warnings\":[],\"metrics\":{\"s3LineCount\":2}}\n"
)
file(WRITE "${GOV_DIR}/receipt-rotation-summary.json"
"{\"totalRecords\":3,\"retainedRecords\":2,\"archivedRecords\":1,\"sensitiveHits\":0}\n"
)
file(WRITE "${GOV_DIR}/receipt-rotation-alert-summary.json"
"{\"kind\":\"large-file-receipt-rotation\",\"ok\":false,\"warnings\":[\"archivedRecords=1 exceeds threshold 0\"],\"metrics\":{\"archivedRecords\":1}}\n"
)
file(WRITE "${GOV_DIR}/aggregate-alerts.log"
"governance alert overview\n"
"  sources found: 3\n"
"  total warnings: 1\n"
)
file(WRITE "${GOV_DIR}/large-file-governance-report.md"
"# QtNetworkChat Large File Governance Report\n"
"\n"
"- Sensitive hits: `0`\n"
"## Health\n"
"unhealthy\n"
)
file(WRITE "${GOV_DIR}/health-check.log"
"governance health: unhealthy\n"
"  reason: overview.ok=false\n"
)
file(WRITE "${NOTES}"
"date: 2026-05-31\n"
"commit: sample\n"
"diagnostics: generated from safe local artifacts\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -GovernanceDir "${GOV_DIR}"
        -OutputDir "${PACKAGE_DIR}"
        -PackagePath "${PACKAGE_ZIP}"
        -NotesPath "${NOTES}"
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
    message(FATAL_ERROR "package-governance-diagnostics.ps1 exited with code ${result}")
endif()

if(NOT EXISTS "${PACKAGE_ZIP}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Diagnostics package was not created: ${PACKAGE_ZIP}")
endif()

set(EXTRACT_DIR "${TEMP_DIR}/extracted")
file(MAKE_DIRECTORY "${EXTRACT_DIR}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xfz "${PACKAGE_ZIP}"
    WORKING_DIRECTORY "${EXTRACT_DIR}"
    RESULT_VARIABLE extract_result
    ERROR_VARIABLE extract_error
)
if(NOT extract_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Failed to extract diagnostics package: ${extract_error}")
endif()

set(MANIFEST "${EXTRACT_DIR}/manifest.json")
if(NOT EXISTS "${MANIFEST}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Diagnostics manifest was not created")
endif()
file(READ "${MANIFEST}" manifest_content)
string(JSON package_format GET "${manifest_content}" "packageFormat")
string(JSON sensitive_hits GET "${manifest_content}" "sensitiveHits")
if(NOT package_format STREQUAL "qtnetworkchat-large-file-governance-diagnostics-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected diagnostics package format: ${package_format}")
endif()
if(NOT sensitive_hits EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected sensitiveHits=0, got ${sensitive_hits}")
endif()

foreach(expected_file
        "${EXTRACT_DIR}/governance-alert-overview.json"
        "${EXTRACT_DIR}/last-health.json"
        "${EXTRACT_DIR}/large-file-governance-report.md"
        "${EXTRACT_DIR}/s3-real-backend-evidence.json"
        "${EXTRACT_DIR}/s3-real-backend-evidence.md"
        "${EXTRACT_DIR}/s3-request-results-alert-summary.json")
    if(NOT EXISTS "${expected_file}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Expected diagnostics artifact missing: ${expected_file}")
    endif()
endforeach()

file(WRITE "${BAD_NOTES}" "Authorization: should-not-be-packaged\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -GovernanceDir "${GOV_DIR}"
        -OutputDir "${TEMP_DIR}/bad-package"
        -NotesPath "${BAD_NOTES}"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)

if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Diagnostics packaging should fail when sensitive fields are present")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Governance diagnostics package test passed")
