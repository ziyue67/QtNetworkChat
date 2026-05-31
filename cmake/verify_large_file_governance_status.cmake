if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/large_file_governance_status_sample")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(GOV_DIR "${TEMP_DIR}/governance")
set(OUT_DIR "${TEMP_DIR}/out")
set(BAD_GOV_DIR "${TEMP_DIR}/bad-governance")
file(MAKE_DIRECTORY "${GOV_DIR}" "${OUT_DIR}" "${BAD_GOV_DIR}")

file(WRITE "${GOV_DIR}/large-file-governance-dashboard.json"
"{\n"
"  \"format\":\"qtnetworkchat-large-file-governance-dashboard-v1\",\n"
"  \"status\":\"healthy\",\n"
"  \"ok\":true,\n"
"  \"reason\":\"all checks passed\",\n"
"  \"totalWarnings\":0,\n"
"  \"alertCount\":3,\n"
"  \"metrics\":{\"s3Lines\":4,\"routeLines\":7,\"reconcileRetained\":0},\n"
"  \"alerts\":[\n"
"    {\"kind\":\"large-file-route-summary\",\"ok\":true,\"warnings\":[]},\n"
"    {\"kind\":\"s3-request-results\",\"ok\":true,\"warnings\":[]},\n"
"    {\"kind\":\"s3-real-backend-evidence\",\"ok\":true,\"warnings\":[]}\n"
"  ],\n"
"  \"sensitiveHits\":0\n"
"}\n"
)
file(WRITE "${GOV_DIR}/last-health.json"
"{\"status\":\"healthy\",\"reason\":\"all checks passed\",\"ok\":true,\"totalWarnings\":0,\"alertCount\":3}\n"
)
file(WRITE "${GOV_DIR}/governance-alert-overview.json"
"{\"ok\":true,\"totalWarnings\":0,\"alertCount\":3,\"alerts\":[{\"kind\":\"s3-real-backend-evidence\",\"ok\":true,\"warnings\":[]}]}\n"
)

set(STATUS_JSON "${OUT_DIR}/status.json")
set(STATUS_MD "${OUT_DIR}/status.md")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -GovernanceDir "${GOV_DIR}"
        -JsonPath "${STATUS_JSON}"
        -MarkdownPath "${STATUS_MD}"
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
    message(FATAL_ERROR "show-large-file-governance-status.ps1 healthy run exited with code ${result}")
endif()
foreach(expected_file "${STATUS_JSON}" "${STATUS_MD}")
    if(NOT EXISTS "${expected_file}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Expected status output missing: ${expected_file}")
    endif()
endforeach()

file(READ "${STATUS_JSON}" status_content)
string(JSON status_format GET "${status_content}" "format")
string(JSON status_value GET "${status_content}" "status")
string(JSON status_ok GET "${status_content}" "ok")
string(JSON status_warning_count GET "${status_content}" "totalWarnings")
string(JSON status_alert_count GET "${status_content}" "alertCount")
string(JSON status_s3_lines GET "${status_content}" "metrics" "s3Lines")
if(NOT status_format STREQUAL "qtnetworkchat-large-file-governance-status-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected status format: ${status_format}")
endif()
if(NOT status_value STREQUAL "healthy")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected status=healthy, got ${status_value}")
endif()
if(NOT status_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected status ok=true")
endif()
if(NOT status_warning_count EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected no warnings, got ${status_warning_count}")
endif()
if(NOT status_alert_count EQUAL 3)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected alertCount=3, got ${status_alert_count}")
endif()
if(NOT status_s3_lines EQUAL 4)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected s3Lines=4, got ${status_s3_lines}")
endif()

file(READ "${STATUS_MD}" markdown_content)
foreach(expected_text
        "QtNetworkChat Large File Governance Status"
        "Status: `healthy`"
        "This status view is read-only")
    string(FIND "${markdown_content}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Status markdown missing expected text: ${expected_text}")
    endif()
endforeach()

file(WRITE "${GOV_DIR}/large-file-governance-dashboard.json"
"{\n"
"  \"format\":\"qtnetworkchat-large-file-governance-dashboard-v1\",\n"
"  \"status\":\"unhealthy\",\n"
"  \"ok\":false,\n"
"  \"reason\":\"overview.ok=false\",\n"
"  \"totalWarnings\":1,\n"
"  \"alertCount\":1,\n"
"  \"alerts\":[{\"kind\":\"s3-request-results\",\"ok\":false,\"warnings\":[\"timeout=1 exceeds threshold 0\"]}],\n"
"  \"sensitiveHits\":0\n"
"}\n"
)
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -GovernanceDir "${GOV_DIR}"
        -FailOnUnhealthy
    RESULT_VARIABLE unhealthy_result
    OUTPUT_VARIABLE unhealthy_output
    ERROR_VARIABLE unhealthy_error
)
if(NOT unhealthy_result EQUAL 2)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected unhealthy status run to exit 2, got ${unhealthy_result}")
endif()
string(FIND "${unhealthy_output}" "status: unhealthy" unhealthy_text)
if(unhealthy_text EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unhealthy status output missing status line")
endif()

file(WRITE "${BAD_GOV_DIR}/large-file-governance-dashboard.json"
"{\"status\":\"healthy\",\"ok\":true,\"objectUrl\":\"https://example.invalid/private\"}\n"
)
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -GovernanceDir "${BAD_GOV_DIR}"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance status should fail when sensitive fields are present")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -GovernanceDir "${TEMP_DIR}/missing-governance"
    RESULT_VARIABLE missing_result
    OUTPUT_VARIABLE missing_output
    ERROR_VARIABLE missing_error
)
if(missing_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance status should fail when no input is present")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Large file governance status test passed")
