if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/governance_dashboard_sample")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(GOV_DIR "${TEMP_DIR}/governance")
set(DASHBOARD "${TEMP_DIR}/dashboard/large-file-governance-dashboard.json")
set(DASHBOARD_MD "${TEMP_DIR}/dashboard/large-file-governance-dashboard.md")
set(BAD_GOV_DIR "${TEMP_DIR}/bad-governance")
file(MAKE_DIRECTORY "${GOV_DIR}" "${GOV_DIR}/reconcile" "${GOV_DIR}/acceptance-package" "${GOV_DIR}/diagnostics-package" "${BAD_GOV_DIR}")

file(WRITE "${GOV_DIR}/governance-alert-overview.json"
"{\n"
"  \"ok\": false,\n"
"  \"totalWarnings\": 2,\n"
"  \"alertCount\": 4,\n"
"  \"alerts\": [\n"
"    {\"kind\":\"large-file-route-summary\",\"ok\":true,\"warnings\":[],\"source\":\"large-file-route-alert-summary.json\"},\n"
"    {\"kind\":\"s3-request-results\",\"ok\":false,\"warnings\":[\"auth=1 exceeds threshold 0\"],\"source\":\"s3-request-results-alert-summary.json\"},\n"
"    {\"kind\":\"s3-real-backend-evidence\",\"ok\":true,\"warnings\":[],\"source\":\"s3-real-backend-evidence-alert-summary.json\"}\n"
"  ]\n"
"}\n"
)
file(WRITE "${GOV_DIR}/last-health.json"
"{\"status\":\"unhealthy\",\"reason\":\"overview.ok=false\",\"ok\":false,\"totalWarnings\":2,\"alertCount\":4}\n"
)
file(WRITE "${GOV_DIR}/large-file-route-summary.json"
"{\"routeLineCount\":5,\"routeKeys\":4,\"sensitiveHits\":0,\"deliveredCount\":1,\"failedCount\":2}\n"
)
file(WRITE "${GOV_DIR}/s3-request-results-summary.json"
"{\"routeLineCount\":5,\"s3LineCount\":3,\"sensitiveHits\":0,\"timeoutCount\":1,\"authCount\":1,\"tlsCount\":0}\n"
)
file(WRITE "${GOV_DIR}/s3-stability-runbook.json"
"{\n"
"  \"format\":\"qtnetworkchat-s3-stability-runbook-v1\",\n"
"  \"ok\":true,\n"
"  \"metrics\":{\"coverageAreaCount\":5,\"coverageFixedReasonCount\":30},\n"
"  \"stabilizationCoverage\":[\n"
"    {\"area\":\"source-write-fallback\",\"event\":\"object_write\",\"operation\":\"write\",\"fixedReasons\":[\"timeout\",\"network\",\"tls\",\"auth\",\"retryable\",\"server\",\"client\"],\"observedEventOperationCount\":2},\n"
"    {\"area\":\"remote-read-fail-closed\",\"event\":\"offer_read\",\"operation\":\"read\",\"fixedReasons\":[\"timeout\",\"network\",\"tls\",\"auth\",\"retryable\",\"server\",\"not_found\",\"unknown\"],\"observedEventOperationCount\":1}\n"
"  ]\n"
"}\n"
)
file(WRITE "${GOV_DIR}/s3-stability-runbook.md" "# safe s3 runbook\n")
file(WRITE "${GOV_DIR}/receipt-rotation-summary.json"
"{\"totalRecords\":4,\"retainedRecords\":2,\"archivedRecords\":2,\"sensitiveHits\":0}\n"
)
file(WRITE "${GOV_DIR}/reconcile/reconcile-summary.json"
"{\"receiptCount\":2,\"fallbackCount\":2,\"cleanedCount\":1,\"retainedCount\":1,\"sensitiveHits\":0}\n"
)
file(WRITE "${GOV_DIR}/large-file-governance-report.md" "# safe report\n")
file(WRITE "${GOV_DIR}/large-file-governance-report.html" "<html>safe report</html>\n")
file(WRITE "${GOV_DIR}/acceptance-package/large-file-acceptance.zip" "placeholder\n")
file(WRITE "${GOV_DIR}/diagnostics-package/large-file-governance-diagnostics.zip" "placeholder\n")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -GovernanceDir "${GOV_DIR}"
        -DashboardPath "${DASHBOARD}"
        -MarkdownPath "${DASHBOARD_MD}"
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
    message(FATAL_ERROR "write-large-file-governance-dashboard.ps1 exited with code ${result}")
endif()

foreach(expected_file "${DASHBOARD}" "${DASHBOARD_MD}")
    if(NOT EXISTS "${expected_file}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Expected dashboard output missing: ${expected_file}")
    endif()
endforeach()

file(READ "${DASHBOARD}" dashboard_content)
string(JSON dashboard_format GET "${dashboard_content}" "format")
string(JSON dashboard_status GET "${dashboard_content}" "status")
string(JSON dashboard_ok GET "${dashboard_content}" "ok")
string(JSON dashboard_warnings GET "${dashboard_content}" "totalWarnings")
string(JSON dashboard_alert_count GET "${dashboard_content}" "alertCount")
string(JSON dashboard_s3_lines GET "${dashboard_content}" "metrics" "s3Lines")
string(JSON dashboard_s3_coverage_areas GET "${dashboard_content}" "metrics" "s3CoverageAreas")
string(JSON dashboard_s3_coverage_reasons GET "${dashboard_content}" "metrics" "s3CoverageFixedReasons")
string(JSON dashboard_retained GET "${dashboard_content}" "metrics" "reconcileRetained")
string(JSON dashboard_sensitive GET "${dashboard_content}" "sensitiveHits")
string(JSON dashboard_coverage_area0 GET "${dashboard_content}" "s3StabilizationCoverage" 0 "area")
if(NOT dashboard_format STREQUAL "qtnetworkchat-large-file-governance-dashboard-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected dashboard format: ${dashboard_format}")
endif()
if(NOT dashboard_status STREQUAL "unhealthy")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected dashboard status=unhealthy, got ${dashboard_status}")
endif()
if(dashboard_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected dashboard ok=false")
endif()
if(NOT dashboard_warnings EQUAL 2)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected dashboard totalWarnings=2, got ${dashboard_warnings}")
endif()
if(NOT dashboard_alert_count EQUAL 4)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected dashboard alertCount=4, got ${dashboard_alert_count}")
endif()
if(NOT dashboard_s3_lines EQUAL 3)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected dashboard s3Lines=3, got ${dashboard_s3_lines}")
endif()
if(NOT dashboard_s3_coverage_areas EQUAL 5)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected dashboard s3CoverageAreas=5, got ${dashboard_s3_coverage_areas}")
endif()
if(NOT dashboard_s3_coverage_reasons EQUAL 30)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected dashboard s3CoverageFixedReasons=30, got ${dashboard_s3_coverage_reasons}")
endif()
if(NOT dashboard_coverage_area0 STREQUAL "source-write-fallback")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected first dashboard coverage area source-write-fallback, got ${dashboard_coverage_area0}")
endif()
if(NOT dashboard_retained EQUAL 1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected dashboard reconcileRetained=1, got ${dashboard_retained}")
endif()
if(NOT dashboard_sensitive EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected dashboard sensitiveHits=0, got ${dashboard_sensitive}")
endif()

file(READ "${DASHBOARD_MD}" markdown_content)
foreach(expected_text
        "QtNetworkChat Large File Governance Dashboard"
        "## Metrics"
        "s3Lines"
        "## S3 Stabilization Coverage"
        "source-write-fallback"
        "remote-read-fail-closed"
        "## Alerts"
        "s3-real-backend-evidence"
        "## Artifacts"
        "s3-stability-runbook.json"
        "large-file-governance-report.md")
    string(FIND "${markdown_content}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Dashboard markdown missing expected text: ${expected_text}")
    endif()
endforeach()

file(WRITE "${BAD_GOV_DIR}/governance-alert-overview.json" "{\"ok\":true,\"totalWarnings\":0,\"alertCount\":0,\"alerts\":[]}\n")
file(WRITE "${BAD_GOV_DIR}/last-health.json" "{\"status\":\"healthy\",\"reason\":\"all checks passed\",\"ok\":true}\n")
file(WRITE "${BAD_GOV_DIR}/s3-request-results-summary.json" "{\"objectUrl\":\"https://example.invalid/private\"}\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -GovernanceDir "${BAD_GOV_DIR}"
        -DashboardPath "${TEMP_DIR}/bad-dashboard.json"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance dashboard should fail when sensitive fields are present")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Large file governance dashboard test passed")
