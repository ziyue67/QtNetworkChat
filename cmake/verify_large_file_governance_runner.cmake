if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/large_file_governance_runner_sample")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(ROUTE_LOG "${TEMP_DIR}/route.log")
set(QUEUE_LOG "${TEMP_DIR}/queue.jsonl")
set(RECEIPTS "${TEMP_DIR}/delivered-receipts.jsonl")
set(NOTES "${TEMP_DIR}/notes.txt")
set(OUTPUT_DIR "${TEMP_DIR}/governance")
set(PACKAGE_PATH "${OUTPUT_DIR}/acceptance.zip")
set(DIAGNOSTICS_PATH "${OUTPUT_DIR}/diagnostics.zip")
set(REPORT_PATH "${OUTPUT_DIR}/large-file-governance-report.md")
set(HTML_REPORT_PATH "${OUTPUT_DIR}/large-file-governance-report.html")
set(DASHBOARD_PATH "${OUTPUT_DIR}/large-file-governance-dashboard.json")
set(DASHBOARD_MD_PATH "${OUTPUT_DIR}/large-file-governance-dashboard.md")
set(SOURCE_INSTANCE "source-governance-a")
set(FILE_HASH "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")

file(WRITE "${ROUTE_LOG}"
"2026-05-30T10:00:00 redis_large_file_route event=delivered result=published sourceInstanceId=${SOURCE_INSTANCE} transferId=sampleclean001 objectKey=sampleclean001.bin receiverId=receiver001 fileHash=${FILE_HASH} bytes=1024 storeType=s3 operation=publish reason=success\n"
"2026-05-30T10:00:01 redis_large_file_route event=delivered_cleanup result=cleaned sourceInstanceId=${SOURCE_INSTANCE} transferId=sampleclean001 objectKey=sampleclean001.bin receiverId=receiver001 fileHash=${FILE_HASH} bytes=1024 storeType=s3 operation=delete reason=success\n"
"2026-05-30T10:00:02 redis_large_file_route event=object_write result=skipped sourceInstanceId=${SOURCE_INSTANCE} transferId=sampleskip001 objectKey=sampleskip001.bin receiverId=receiver002 fileHash=${FILE_HASH} bytes=1024 storeType=s3 operation=write reason=network\n"
)

file(WRITE "${QUEUE_LOG}"
"{\"transferId\":\"sampleclean001\",\"receiverId\":\"receiver001\",\"objectStoreKey\":\"sampleclean001.bin\",\"fileHash\":\"${FILE_HASH}\",\"fileSize\":1024}\n"
)

file(WRITE "${RECEIPTS}"
"{\"sourceInstanceId\":\"${SOURCE_INSTANCE}\",\"transferId\":\"old001\",\"receiverId\":\"receiver009\",\"objectKey\":\"old001.bin\",\"fileHash\":\"${FILE_HASH}\",\"confirmedBytes\":128,\"result\":\"retained\",\"reason\":\"receipt-not-matched\",\"cleanupResult\":\"retained\",\"createdAt\":\"2026-01-01T00:00:00Z\"}\n"
"{\"sourceInstanceId\":\"${SOURCE_INSTANCE}\",\"transferId\":\"sampleclean001\",\"receiverId\":\"receiver001\",\"objectKey\":\"sampleclean001.bin\",\"fileHash\":\"${FILE_HASH}\",\"confirmedBytes\":1024,\"result\":\"cleaned\",\"reason\":\"cleaned\",\"cleanupResult\":\"cleaned\",\"createdAt\":\"2026-05-30T00:00:00Z\"}\n"
)

file(WRITE "${NOTES}"
"date: 2026-05-30\n"
"commit: sample\n"
"objectStore: s3\n"
"success path: passed\n"
"fallback path: passed\n"
"log redaction: passed\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -RouteLogPath "${ROUTE_LOG}"
        -QueuePath "${QUEUE_LOG}"
        -SourceInstanceId "${SOURCE_INSTANCE}"
        -OutputDir "${OUTPUT_DIR}"
        -EmitRouteLog
        -NoFailOnWarning
        -ReceiptRotationPath "${RECEIPTS}"
        -RotationKeepRecords 1
        -RotationMaxAgeDays 30
        -CompressRotationArchive
        -NotesPath "${NOTES}"
        -PackageAcceptance
        -PackagePath "${PACKAGE_PATH}"
        -WriteReport
        -ReportPath "${REPORT_PATH}"
        -HtmlReportPath "${HTML_REPORT_PATH}"
        -WriteDashboard
        -DashboardPath "${DASHBOARD_PATH}"
        -DashboardMarkdownPath "${DASHBOARD_MD_PATH}"
        -RunS3FailureBatchSample
        -S3FailureBatchCountPerReason 2
        -PackageDiagnostics
        -DiagnosticsPackagePath "${DIAGNOSTICS_PATH}"
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
    message(FATAL_ERROR "run-large-file-governance.ps1 exited with code ${result}")
endif()

foreach(expected_file
        "${OUTPUT_DIR}/large-file-route-summary.json"
        "${OUTPUT_DIR}/large-file-route-alert-summary.json"
        "${OUTPUT_DIR}/s3-request-results-summary.json"
        "${OUTPUT_DIR}/s3-request-results-alert-summary.json"
        "${OUTPUT_DIR}/s3-failure-batch-summary.json"
        "${OUTPUT_DIR}/s3-failure-batch-alert-summary.json"
        "${OUTPUT_DIR}/reconcile/receipts.jsonl"
        "${OUTPUT_DIR}/reconcile/fallbacks.jsonl"
        "${OUTPUT_DIR}/reconcile/s3-analysis-summary.json"
        "${OUTPUT_DIR}/receipt-rotation-summary.json"
        "${OUTPUT_DIR}/receipt-rotation-alert-summary.json"
        "${OUTPUT_DIR}/governance-alert-overview.json"
        "${REPORT_PATH}"
        "${HTML_REPORT_PATH}"
        "${DASHBOARD_PATH}"
        "${DASHBOARD_MD_PATH}"
        "${DIAGNOSTICS_PATH}"
        "${PACKAGE_PATH}")
    if(NOT EXISTS "${expected_file}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Expected governance output was not created: ${expected_file}")
    endif()
endforeach()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -RouteLogPath "${ROUTE_LOG}"
        -QueuePath "${QUEUE_LOG}"
        -SourceInstanceId "${SOURCE_INSTANCE}"
        -OutputDir "${TEMP_DIR}/bad-rotation"
        -RotationKeepRecords 1
    RESULT_VARIABLE bad_rotation_result
    OUTPUT_VARIABLE bad_rotation_output
    ERROR_VARIABLE bad_rotation_error
)

if(bad_rotation_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance runner should fail when rotation options are set without ReceiptRotationPath")
endif()

file(READ "${OUTPUT_DIR}/s3-request-results-summary.json" s3_summary_content)
string(JSON s3_lines GET "${s3_summary_content}" "s3LineCount")
if(NOT s3_lines EQUAL 2)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected s3LineCount=2, got ${s3_lines}")
endif()

file(READ "${OUTPUT_DIR}/receipt-rotation-summary.json" rotation_summary_content)
string(JSON archived_records GET "${rotation_summary_content}" "archivedRecords")
if(NOT archived_records EQUAL 1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected archivedRecords=1, got ${archived_records}")
endif()

file(READ "${OUTPUT_DIR}/s3-request-results-alert-summary.json" s3_alert_content)
string(JSON s3_alert_kind GET "${s3_alert_content}" "kind")
string(JSON s3_alert_ok GET "${s3_alert_content}" "ok")
string(JSON s3_alert_lines GET "${s3_alert_content}" "metrics" "s3LineCount")
if(NOT s3_alert_kind STREQUAL "s3-request-results")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected governance S3 alert kind: ${s3_alert_kind}")
endif()

file(READ "${DASHBOARD_PATH}" dashboard_content)
string(JSON dashboard_format GET "${dashboard_content}" "format")
string(JSON dashboard_status GET "${dashboard_content}" "status")
string(JSON dashboard_s3_lines GET "${dashboard_content}" "metrics" "s3Lines")
if(NOT dashboard_format STREQUAL "qtnetworkchat-large-file-governance-dashboard-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected governance dashboard format: ${dashboard_format}")
endif()
if(NOT dashboard_status STREQUAL "unknown")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected governance dashboard status=unknown without HealthCheckPath, got ${dashboard_status}")
endif()
if(NOT dashboard_s3_lines EQUAL 2)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected governance dashboard s3Lines=2, got ${dashboard_s3_lines}")
endif()
if(NOT s3_alert_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected governance S3 alert ok=true because default thresholds do not warn on network")
endif()
if(NOT s3_alert_lines EQUAL 2)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected governance S3 alert s3LineCount=2, got ${s3_alert_lines}")
endif()

file(READ "${OUTPUT_DIR}/s3-failure-batch-summary.json" s3_batch_content)
string(JSON s3_batch_lines GET "${s3_batch_content}" "s3LineCount")
string(JSON s3_batch_timeouts GET "${s3_batch_content}" "reasonCounts" "timeout")
if(NOT s3_batch_lines EQUAL 22)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected S3 failure batch s3LineCount=22, got ${s3_batch_lines}")
endif()
if(NOT s3_batch_timeouts EQUAL 2)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected S3 failure batch timeout count=2, got ${s3_batch_timeouts}")
endif()

file(READ "${OUTPUT_DIR}/governance-alert-overview.json" overview_content)
string(JSON overview_alert_count GET "${overview_content}" "alertCount")
if(NOT overview_alert_count EQUAL 4)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected governance alertCount=4 with S3 batch sample, got ${overview_alert_count}")
endif()

file(READ "${OUTPUT_DIR}/receipt-rotation-alert-summary.json" rotation_alert_content)
string(JSON rotation_alert_kind GET "${rotation_alert_content}" "kind")
string(JSON rotation_alert_ok GET "${rotation_alert_content}" "ok")
if(NOT rotation_alert_kind STREQUAL "large-file-receipt-rotation")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected governance rotation alert kind: ${rotation_alert_kind}")
endif()
if(rotation_alert_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected governance rotation alert ok=false because archived/retained default thresholds are exceeded")
endif()

set(EXTRACT_DIR "${TEMP_DIR}/extracted")
file(MAKE_DIRECTORY "${EXTRACT_DIR}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xfz "${PACKAGE_PATH}"
    WORKING_DIRECTORY "${EXTRACT_DIR}"
    RESULT_VARIABLE extract_result
    ERROR_VARIABLE extract_error
)
if(NOT extract_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Failed to extract governance package: ${extract_error}")
endif()

if(NOT EXISTS "${EXTRACT_DIR}/manifest.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance package manifest missing")
endif()

set(DIAG_EXTRACT_DIR "${TEMP_DIR}/diagnostics-extracted")
file(MAKE_DIRECTORY "${DIAG_EXTRACT_DIR}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xfz "${DIAGNOSTICS_PATH}"
    WORKING_DIRECTORY "${DIAG_EXTRACT_DIR}"
    RESULT_VARIABLE diag_extract_result
    ERROR_VARIABLE diag_extract_error
)
if(NOT diag_extract_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Failed to extract governance diagnostics package: ${diag_extract_error}")
endif()

if(NOT EXISTS "${DIAG_EXTRACT_DIR}/manifest.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance diagnostics manifest missing")
endif()
if(NOT EXISTS "${DIAG_EXTRACT_DIR}/governance-alert-overview.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance diagnostics overview missing")
endif()
if(NOT EXISTS "${DIAG_EXTRACT_DIR}/large-file-governance-report.md")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance diagnostics report missing")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Large file governance runner test passed")
