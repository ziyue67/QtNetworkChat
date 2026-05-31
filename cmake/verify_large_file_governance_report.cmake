if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/governance_report_sample")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(GOV_DIR "${TEMP_DIR}/governance")
set(REPORT "${TEMP_DIR}/report/governance-report.md")
set(HTML_REPORT "${TEMP_DIR}/report/governance-report.html")
set(BAD_GOV_DIR "${TEMP_DIR}/bad-governance")
file(MAKE_DIRECTORY "${GOV_DIR}" "${BAD_GOV_DIR}")

file(WRITE "${GOV_DIR}/governance-alert-overview.json"
"{\n"
"  \"ok\": false,\n"
"  \"totalWarnings\": 1,\n"
"  \"alertCount\": 3,\n"
"  \"alerts\": [\n"
"    {\"kind\":\"large-file-route-summary\",\"ok\":true,\"warnings\":[],\"source\":\"large-file-route-alert-summary.json\"},\n"
"    {\"kind\":\"s3-request-results\",\"ok\":false,\"warnings\":[\"timeoutCount=2 exceeds threshold 0\"],\"source\":\"s3-request-results-alert-summary.json\"}\n"
"  ]\n"
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
"{\"routeLineCount\":3,\"routeKeys\":2,\"sensitiveHits\":0,\"deliveredCount\":1,\"failedCount\":1}\n"
)
file(WRITE "${GOV_DIR}/s3-request-results-summary.json"
"{\"routeLineCount\":3,\"s3LineCount\":2,\"sensitiveHits\":0,\"timeoutCount\":2,\"authCount\":0}\n"
)
file(WRITE "${GOV_DIR}/s3-real-backend-evidence.json"
"{\"format\":\"qtnetworkchat-s3-real-backend-evidence-v1\",\"ok\":true,\"warnings\":[],\"metrics\":{\"s3LineCount\":2,\"routeLineCount\":3,\"successCount\":1,\"fixedFailureReasonCount\":1,\"sensitiveHits\":0,\"s3SummarySensitiveHits\":0,\"governanceStatus\":\"healthy\",\"governanceOk\":true}}\n"
)
file(WRITE "${GOV_DIR}/s3-stability-runbook.json"
"{\n"
"  \"format\":\"qtnetworkchat-s3-stability-runbook-v1\",\n"
"  \"ok\":true,\n"
"  \"metrics\":{\"coverageAreaCount\":5,\"coverageFixedReasonCount\":30,\"coverageGapCount\":1,\"coverageActionableGapCount\":1},\n"
"  \"coverageGapAreas\":[\"remote-validation-fail-closed\"],\n"
"  \"coverageActionableGapAreas\":[\"remote-validation-fail-closed\"],\n"
"  \"stabilizationCoverage\":[\n"
"    {\"area\":\"source-write-fallback\",\"event\":\"object_write\",\"operation\":\"write\",\"fixedReasons\":[\"timeout\",\"network\"],\"observedEventOperationCount\":2},\n"
"    {\"area\":\"source-delete-retained\",\"event\":\"object_delete\",\"operation\":\"delete\",\"fixedReasons\":[\"server\",\"unknown\"],\"observedEventOperationCount\":1}\n"
"  ]\n"
"}\n"
)
file(WRITE "${GOV_DIR}/receipt-rotation-summary.json"
"{\"totalRecords\":3,\"retainedRecords\":2,\"archivedRecords\":1,\"sensitiveHits\":0}\n"
)
file(MAKE_DIRECTORY "${GOV_DIR}/reconcile")
file(WRITE "${GOV_DIR}/reconcile/reconcile-summary.json"
"{\"receiptCount\":2,\"fallbackCount\":1,\"cleanedCount\":1,\"retainedCount\":1,\"sensitiveHits\":0}\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -GovernanceDir "${GOV_DIR}"
        -ReportPath "${REPORT}"
        -HtmlReportPath "${HTML_REPORT}"
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
    message(FATAL_ERROR "write-large-file-governance-report.ps1 exited with code ${result}")
endif()

if(NOT EXISTS "${REPORT}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Markdown governance report was not created")
endif()
if(NOT EXISTS "${HTML_REPORT}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "HTML governance report was not created")
endif()

file(READ "${REPORT}" report_content)
foreach(expected_text
        "QtNetworkChat Large File Governance Report"
        "## Health"
        "unhealthy"
        "## Alert Sources"
        "## S3 Request Summary"
        "## S3 Real Backend Evidence"
        "fixedFailureReasonCount"
        "## S3 Stabilization Coverage"
        "source-write-fallback"
        "source-delete-retained"
        "Actionable coverage gaps"
        "remote-validation-fail-closed"
        "## Delivered Reconcile Summary"
        "Sensitive hits: `0`")
    string(FIND "${report_content}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Governance report missing expected text: ${expected_text}")
    endif()
endforeach()

file(WRITE "${BAD_GOV_DIR}/governance-alert-overview.json" "{\"ok\":true,\"totalWarnings\":0,\"alertCount\":0,\"alerts\":[]}\n")
file(WRITE "${BAD_GOV_DIR}/last-health.json" "{\"status\":\"healthy\",\"reason\":\"all checks passed\",\"ok\":true,\"totalWarnings\":0,\"alertCount\":0}\n")
file(WRITE "${BAD_GOV_DIR}/large-file-route-summary.json" "{\"objectUrl\":\"https://example.invalid/private\"}\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -GovernanceDir "${BAD_GOV_DIR}"
        -ReportPath "${TEMP_DIR}/bad-report.md"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance report should fail when sensitive fields are present")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Large file governance report test passed")
