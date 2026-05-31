if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/s3_stability_runbook_sample")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(S3_SUMMARY "${TEMP_DIR}/s3-summary.json")
set(EVIDENCE_JSON "${TEMP_DIR}/evidence.json")
set(STATUS_JSON "${TEMP_DIR}/status.json")
set(RUNBOOK_JSON "${TEMP_DIR}/s3-stability-runbook.json")
set(RUNBOOK_MD "${TEMP_DIR}/s3-stability-runbook.md")
set(ALERT_JSON "${TEMP_DIR}/s3-stability-runbook-alert-summary.json")

file(WRITE "${S3_SUMMARY}"
"{\n"
"  \"routeLineCount\": 9,\n"
"  \"s3LineCount\": 8,\n"
"  \"sensitiveHits\": 0,\n"
"  \"reasonCounts\": {\"success\": 3, \"timeout\": 1, \"retryable\": 1, \"network\": 1, \"auth\": 1, \"hash\": 1, \"not_found\": 1},\n"
"  \"operationCounts\": {\"put\": 2, \"get\": 2, \"head\": 1, \"delete\": 1, \"validate\": 1, \"read\": 1}\n"
"}\n"
)
file(WRITE "${EVIDENCE_JSON}"
"{\"format\":\"qtnetworkchat-s3-real-backend-evidence-v1\",\"ok\":true,\"metrics\":{\"successCount\":3,\"fixedFailureReasonCount\":5}}\n"
)
file(WRITE "${STATUS_JSON}"
"{\"format\":\"qtnetworkchat-large-file-governance-status-v1\",\"status\":\"healthy\",\"ok\":true,\"totalWarnings\":0}\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -S3SummaryPath "${S3_SUMMARY}"
        -EvidencePath "${EVIDENCE_JSON}"
        -GovernanceStatusPath "${STATUS_JSON}"
        -OutputPath "${RUNBOOK_JSON}"
        -MarkdownPath "${RUNBOOK_MD}"
        -AlertSummaryPath "${ALERT_JSON}"
        -MinSuccess 2
        -WarnTimeout 1
        -WarnRetryable 1
        -WarnAuth 1
        -WarnHash 1
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
    message(FATAL_ERROR "write-s3-stability-runbook.ps1 exited with code ${result}")
endif()

foreach(expected_file "${RUNBOOK_JSON}" "${RUNBOOK_MD}" "${ALERT_JSON}")
    if(NOT EXISTS "${expected_file}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Expected runbook output missing: ${expected_file}")
    endif()
endforeach()

file(READ "${RUNBOOK_JSON}" runbook_content)
string(JSON runbook_format GET "${runbook_content}" "format")
string(JSON runbook_ok GET "${runbook_content}" "ok")
string(JSON runbook_success GET "${runbook_content}" "metrics" "successCount")
string(JSON runbook_timeout GET "${runbook_content}" "metrics" "timeoutCount")
string(JSON runbook_network GET "${runbook_content}" "metrics" "networkCount")
string(JSON runbook_auth GET "${runbook_content}" "metrics" "authCount")
string(JSON runbook_status GET "${runbook_content}" "metrics" "governanceStatus")
string(JSON runbook_coverage_areas GET "${runbook_content}" "metrics" "coverageAreaCount")
string(JSON runbook_coverage_reasons GET "${runbook_content}" "metrics" "coverageFixedReasonCount")
if(NOT runbook_format STREQUAL "qtnetworkchat-s3-stability-runbook-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected runbook format: ${runbook_format}")
endif()
if(NOT runbook_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected runbook ok=true")
endif()
if(NOT runbook_success EQUAL 3)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected successCount=3, got ${runbook_success}")
endif()
if(NOT runbook_timeout EQUAL 1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected timeoutCount=1, got ${runbook_timeout}")
endif()
if(NOT runbook_network EQUAL 1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected networkCount=1, got ${runbook_network}")
endif()
if(NOT runbook_auth EQUAL 1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected authCount=1, got ${runbook_auth}")
endif()
if(NOT runbook_status STREQUAL "healthy")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected governanceStatus=healthy, got ${runbook_status}")
endif()
if(NOT runbook_coverage_areas EQUAL 5)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected coverageAreaCount=5, got ${runbook_coverage_areas}")
endif()
if(NOT runbook_coverage_reasons EQUAL 30)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected coverageFixedReasonCount=30, got ${runbook_coverage_reasons}")
endif()
string(JSON coverage_area0 GET "${runbook_content}" "stabilizationCoverage" 0 "area")
string(JSON coverage_event0 GET "${runbook_content}" "stabilizationCoverage" 0 "event")
string(JSON coverage_reason0 GET "${runbook_content}" "stabilizationCoverage" 0 "fixedReasons" 0)
if(NOT coverage_area0 STREQUAL "source-write-fallback" OR NOT coverage_event0 STREQUAL "object_write" OR NOT coverage_reason0 STREQUAL "timeout")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected first stabilization coverage entry")
endif()
foreach(expected_reason "timeout" "retryable" "auth" "integrity" "not_found")
    string(FIND "${runbook_content}" "\"${expected_reason}\"" reason_pos)
    if(reason_pos EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Runbook JSON missing action reason: ${expected_reason}")
    endif()
endforeach()
foreach(sensitive_text "endpoint" "bucket" "objectUrl" "access key" "secret key" "session token" "Authorization" "Credential" "Signature")
    string(FIND "${runbook_content}" "${sensitive_text}" sensitive_pos)
    if(NOT sensitive_pos EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Runbook JSON contains sensitive text: ${sensitive_text}")
    endif()
endforeach()

file(READ "${ALERT_JSON}" alert_content)
string(JSON alert_kind GET "${alert_content}" "kind")
string(JSON alert_ok GET "${alert_content}" "ok")
if(NOT alert_kind STREQUAL "s3-stability-runbook")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected runbook alert kind: ${alert_kind}")
endif()
if(NOT alert_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected runbook alert ok=true")
endif()

file(READ "${RUNBOOK_MD}" markdown_content)
foreach(expected_text
        "QtNetworkChat S3 Stability Runbook"
        "## Actions"
        "## Stabilization Coverage"
        "source-write-fallback"
        "source-delete-retained"
        "timeout"
        "integrity"
        "This runbook is read-only")
    string(FIND "${markdown_content}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Runbook markdown missing expected text: ${expected_text}")
    endif()
endforeach()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -S3SummaryPath "${S3_SUMMARY}"
        -OutputPath "${TEMP_DIR}/warning-runbook.json"
        -MinSuccess 4
    RESULT_VARIABLE warning_result
    OUTPUT_VARIABLE warning_output
    ERROR_VARIABLE warning_error
)
if(warning_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Runbook should fail when MinSuccess is not met")
endif()

set(BAD_SUMMARY "${TEMP_DIR}/bad-s3-summary.json")
file(WRITE "${BAD_SUMMARY}"
"{\"s3LineCount\":1,\"sensitiveHits\":0,\"reasonCounts\":{\"success\":1},\"objectUrl\":\"https://example.invalid/private\"}\n"
)
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -S3SummaryPath "${BAD_SUMMARY}"
        -OutputPath "${TEMP_DIR}/bad-runbook.json"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Runbook should fail when sensitive fields are present")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "S3 stability runbook test passed: success=${runbook_success} timeout=${runbook_timeout}")
