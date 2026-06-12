if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/governance_performance_summary_sample")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(GOV_DIR "${TEMP_DIR}/governance")
set(BAD_GOV_DIR "${TEMP_DIR}/bad-governance")
set(JSON_OUT "${TEMP_DIR}/performance/summary.json")
set(MD_OUT "${TEMP_DIR}/performance/summary.md")
file(MAKE_DIRECTORY "${GOV_DIR}/reconcile" "${BAD_GOV_DIR}")

file(WRITE "${GOV_DIR}/last-health.json"
"{\"status\":\"healthy\",\"reason\":\"all checks passed\",\"ok\":true,\"totalWarnings\":0,\"alertCount\":0}\n"
)
file(WRITE "${GOV_DIR}/large-file-route-summary.json"
"{\"routeLineCount\":6,\"routeKeys\":4,\"deliveredCleaned\":2,\"deliveredRetained\":1,\"deliveredWithoutCleanup\":0,\"failedFallbackRetained\":2,\"failedWithoutFallback\":0,\"sensitiveHits\":0}\n"
)
file(WRITE "${GOV_DIR}/s3-request-results-summary.json"
"{\"s3LineCount\":8,\"reasonCounts\":{\"success\":5,\"timeout\":1,\"network\":1,\"retryable\":0,\"server\":0,\"auth\":0,\"tls\":0,\"hash\":0,\"size\":0},\"sensitiveHits\":0}\n"
)
file(WRITE "${GOV_DIR}/s3-stability-runbook.json"
"{\"format\":\"qtnetworkchat-s3-stability-runbook-v1\",\"metrics\":{\"coverageAreaCount\":5,\"coverageGapCount\":0,\"coverageActionableGapCount\":0},\"warnings\":[]}\n"
)
file(WRITE "${GOV_DIR}/receipt-rotation-summary.json"
"{\"totalRecords\":5,\"retainedRecords\":4,\"archivedRecords\":1,\"sensitiveHits\":0}\n"
)
file(WRITE "${GOV_DIR}/reconcile/reconcile-summary.json"
"{\"receiptCount\":3,\"fallbackCount\":2,\"cleanedCount\":2,\"retainedCount\":1,\"sensitiveHits\":0}\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -GovernanceDir "${GOV_DIR}"
        -OutputPath "${JSON_OUT}"
        -MarkdownPath "${MD_OUT}"
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
    message(FATAL_ERROR "write-large-file-governance-performance-summary.ps1 exited with code ${result}")
endif()

foreach(expected_file "${JSON_OUT}" "${MD_OUT}")
    if(NOT EXISTS "${expected_file}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Expected performance summary output missing: ${expected_file}")
    endif()
endforeach()

file(READ "${JSON_OUT}" summary_content)
string(JSON summary_format GET "${summary_content}" "format")
string(JSON summary_status GET "${summary_content}" "status")
string(JSON summary_ok GET "${summary_content}" "ok")
string(JSON summary_reason GET "${summary_content}" "reason")
string(JSON summary_readiness GET "${summary_content}" "summary" "readiness")
string(JSON summary_release_gate GET "${summary_content}" "auditSummary" "releaseGate")
string(JSON summary_bottleneck0 GET "${summary_content}" "bottlenecks" 0)
string(JSON delivery_closure_percent GET "${summary_content}" "ratios" "deliveryClosurePercent")
string(JSON fallback_protection_percent GET "${summary_content}" "ratios" "fallbackProtectionPercent")
string(JSON transient_percent GET "${summary_content}" "ratios" "s3TransientPercent")
string(JSON archived_records GET "${summary_content}" "metrics" "receiptArchivedRecords")
string(JSON s3_transient_count GET "${summary_content}" "metrics" "s3TransientCount")
string(JSON coverage_actionable_gaps GET "${summary_content}" "metrics" "coverageActionableGapCount")
if(NOT summary_format STREQUAL "qtnetworkchat-large-file-governance-performance-summary-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected performance summary format: ${summary_format}")
endif()
if(NOT summary_status STREQUAL "ready")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected summary status=ready, got ${summary_status}")
endif()
if(summary_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected summary ok=false because transient pressure and archived receipts remain")
endif()
if(NOT summary_readiness STREQUAL "review" OR NOT summary_release_gate STREQUAL "review-governance-performance")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected governance performance summary review gate")
endif()
if(NOT summary_bottleneck0 STREQUAL "s3-transient-pressure")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected first bottleneck s3-transient-pressure, got ${summary_bottleneck0}")
endif()
if(NOT archived_records EQUAL 1 OR NOT s3_transient_count EQUAL 2 OR NOT coverage_actionable_gaps EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected performance summary metrics")
endif()
if(NOT delivery_closure_percent MATCHES "^66\\.7")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected deliveryClosurePercent=66.7, got ${delivery_closure_percent}")
endif()
if(NOT fallback_protection_percent MATCHES "^100(\\.0+)?$")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected fallbackProtectionPercent=100, got ${fallback_protection_percent}")
endif()
if(NOT transient_percent MATCHES "^25(\\.0+)?$")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected s3TransientPercent=25, got ${transient_percent}")
endif()

file(READ "${MD_OUT}" markdown_content)
foreach(expected_text
        "QtNetworkChat Large File Governance Performance Summary"
        "Readiness: `review`"
        "Release gate: `review-governance-performance`"
        "## Core Metrics"
        "receiptArchivedRecords"
        "## Core Ratios"
        "deliveryClosurePercent"
        "## Bottlenecks"
        "s3-transient-pressure"
        "receipt-archive-pressure")
    string(FIND "${markdown_content}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Performance summary markdown missing expected text: ${expected_text}")
    endif()
endforeach()

file(WRITE "${BAD_GOV_DIR}/last-health.json" "{\"status\":\"healthy\",\"ok\":true}\n")
file(WRITE "${BAD_GOV_DIR}/large-file-route-summary.json" "{\"routeLineCount\":1}\n")
file(WRITE "${BAD_GOV_DIR}/s3-request-results-summary.json" "{\"objectUrl\":\"https://example.invalid/private\"}\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -GovernanceDir "${BAD_GOV_DIR}"
        -OutputPath "${TEMP_DIR}/bad-summary.json"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Performance summary should fail when sensitive fields are present")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Large file governance performance summary test passed")
