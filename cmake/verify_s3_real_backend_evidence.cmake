if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/s3_real_backend_evidence_sample")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(S3_SUMMARY "${TEMP_DIR}/s3-summary.json")
set(ROUTE_SUMMARY "${TEMP_DIR}/route-summary.json")
set(STATUS_JSON "${TEMP_DIR}/governance-status.json")
set(SMOKE_LOG "${TEMP_DIR}/smoke.log")
set(NOTES "${TEMP_DIR}/notes.txt")
set(EVIDENCE_JSON "${TEMP_DIR}/evidence.json")
set(EVIDENCE_MD "${TEMP_DIR}/evidence.md")
set(ALERT_JSON "${TEMP_DIR}/evidence-alert.json")

file(WRITE "${S3_SUMMARY}"
"{\n"
"  \"routeLineCount\": 8,\n"
"  \"s3LineCount\": 7,\n"
"  \"sensitiveHits\": 0,\n"
"  \"reasonCounts\": {\"success\": 3, \"timeout\": 1, \"retryable\": 1, \"hash\": 1, \"server\": 1},\n"
"  \"operationCounts\": {\"put\": 2, \"get\": 1, \"head\": 1, \"delete\": 1, \"write\": 1, \"validate\": 1},\n"
"  \"resultCounts\": {\"published\": 2, \"cleaned\": 1, \"skipped\": 2, \"rejected\": 1, \"retained\": 1}\n"
"}\n"
)
file(WRITE "${ROUTE_SUMMARY}"
"{\"routeLineCount\":8,\"routeKeys\":5,\"sensitiveHits\":0,\"failedWithoutFallback\":0,\"deliveredWithoutCleanup\":0}\n"
)
file(WRITE "${STATUS_JSON}"
"{\"format\":\"qtnetworkchat-large-file-governance-status-v1\",\"status\":\"healthy\",\"ok\":true,\"totalWarnings\":0}\n"
)
file(WRITE "${SMOKE_LOG}"
"minio smoke completed\n"
"put=head=get=delete passed\n"
)
file(WRITE "${NOTES}"
"operator: local acceptance\n"
"result: passed\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -S3SummaryPath "${S3_SUMMARY}"
        -RouteSummaryPath "${ROUTE_SUMMARY}"
        -GovernanceStatusPath "${STATUS_JSON}"
        -SmokeLogPath "${SMOKE_LOG}"
        -NotesPath "${NOTES}"
        -OutputPath "${EVIDENCE_JSON}"
        -MarkdownPath "${EVIDENCE_MD}"
        -AlertSummaryPath "${ALERT_JSON}"
        -MinS3Lines 3
        -RequireSuccess
        -RequireFailureReason
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
    message(FATAL_ERROR "verify-s3-real-backend-evidence.ps1 exited with code ${result}")
endif()

foreach(expected_file "${EVIDENCE_JSON}" "${EVIDENCE_MD}" "${ALERT_JSON}")
    if(NOT EXISTS "${expected_file}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Expected evidence output missing: ${expected_file}")
    endif()
endforeach()

file(READ "${EVIDENCE_JSON}" evidence_content)
string(JSON evidence_format GET "${evidence_content}" "format")
string(JSON evidence_ok GET "${evidence_content}" "ok")
string(JSON evidence_s3_lines GET "${evidence_content}" "metrics" "s3LineCount")
string(JSON evidence_success GET "${evidence_content}" "metrics" "successCount")
string(JSON evidence_failure_count GET "${evidence_content}" "metrics" "fixedFailureReasonCount")
string(JSON evidence_status GET "${evidence_content}" "metrics" "governanceStatus")
if(NOT evidence_format STREQUAL "qtnetworkchat-s3-real-backend-evidence-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected evidence format: ${evidence_format}")
endif()
if(NOT evidence_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected evidence ok=true")
endif()
if(NOT evidence_s3_lines EQUAL 7)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected evidence s3LineCount=7, got ${evidence_s3_lines}")
endif()
if(NOT evidence_success EQUAL 3)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected evidence successCount=3, got ${evidence_success}")
endif()
if(NOT evidence_failure_count EQUAL 4)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected evidence fixedFailureReasonCount=4, got ${evidence_failure_count}")
endif()
if(NOT evidence_status STREQUAL "healthy")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected evidence governanceStatus=healthy, got ${evidence_status}")
endif()

file(READ "${ALERT_JSON}" alert_content)
string(JSON alert_kind GET "${alert_content}" "kind")
string(JSON alert_ok GET "${alert_content}" "ok")
if(NOT alert_kind STREQUAL "s3-real-backend-evidence")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected evidence alert kind: ${alert_kind}")
endif()
if(NOT alert_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected evidence alert ok=true")
endif()

file(READ "${EVIDENCE_MD}" markdown_content)
foreach(expected_text
        "QtNetworkChat S3 Real Backend Evidence"
        "S3 lines: `7`"
        "Fixed failure reasons: `4`")
    string(FIND "${markdown_content}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Evidence markdown missing expected text: ${expected_text}")
    endif()
endforeach()

set(NO_SUCCESS_SUMMARY "${TEMP_DIR}/no-success-s3-summary.json")
file(WRITE "${NO_SUCCESS_SUMMARY}"
"{\"routeLineCount\":1,\"s3LineCount\":1,\"sensitiveHits\":0,\"reasonCounts\":{\"timeout\":1}}\n"
)
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -S3SummaryPath "${NO_SUCCESS_SUMMARY}"
        -OutputPath "${TEMP_DIR}/no-success-evidence.json"
        -RequireSuccess
    RESULT_VARIABLE no_success_result
    OUTPUT_VARIABLE no_success_output
    ERROR_VARIABLE no_success_error
)
if(no_success_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Evidence verifier should fail when success is required but absent")
endif()

set(NO_FAILURE_SUMMARY "${TEMP_DIR}/no-failure-s3-summary.json")
file(WRITE "${NO_FAILURE_SUMMARY}"
"{\"routeLineCount\":1,\"s3LineCount\":1,\"sensitiveHits\":0,\"reasonCounts\":{\"success\":1}}\n"
)
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -S3SummaryPath "${NO_FAILURE_SUMMARY}"
        -OutputPath "${TEMP_DIR}/no-failure-evidence.json"
        -RequireFailureReason
    RESULT_VARIABLE no_failure_result
    OUTPUT_VARIABLE no_failure_output
    ERROR_VARIABLE no_failure_error
)
if(no_failure_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Evidence verifier should fail when failure reason is required but absent")
endif()

set(BAD_SUMMARY "${TEMP_DIR}/bad-s3-summary.json")
file(WRITE "${BAD_SUMMARY}"
"{\"routeLineCount\":1,\"s3LineCount\":1,\"sensitiveHits\":0,\"reasonCounts\":{\"success\":1},\"objectUrl\":\"https://example.invalid/private\"}\n"
)
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -S3SummaryPath "${BAD_SUMMARY}"
        -OutputPath "${TEMP_DIR}/bad-evidence.json"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Evidence verifier should fail when sensitive fields are present")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "S3 real backend evidence test passed: s3Lines=${evidence_s3_lines}")
