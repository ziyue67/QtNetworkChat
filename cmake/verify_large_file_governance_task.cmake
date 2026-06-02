if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/large_file_governance_task_sample")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(ROUTE_LOG "${TEMP_DIR}/route.log")
set(QUEUE_LOG "${TEMP_DIR}/queue.jsonl")
set(OUTPUT_DIR "${TEMP_DIR}/governance-output")
set(TASK_DIR "${TEMP_DIR}/task")
set(DASHBOARD_PATH "${OUTPUT_DIR}/dashboard.json")
set(DASHBOARD_MD_PATH "${OUTPUT_DIR}/dashboard.md")
set(S3_RUNBOOK_PATH "${OUTPUT_DIR}/s3-runbook.json")
set(S3_RUNBOOK_MD_PATH "${OUTPUT_DIR}/s3-runbook.md")
set(S3_POLICY_PATH "${TEMP_DIR}/s3-policy.json")
set(SOURCE_INSTANCE "source-task-a")
set(FILE_HASH "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")

file(WRITE "${ROUTE_LOG}"
"2026-05-30T10:00:00 redis_large_file_route event=delivered result=published sourceInstanceId=${SOURCE_INSTANCE} transferId=taskclean001 objectKey=taskclean001.bin receiverId=receiver001 fileHash=${FILE_HASH} bytes=1024 storeType=s3 operation=publish reason=success\n"
)

file(WRITE "${QUEUE_LOG}"
"{\"transferId\":\"taskclean001\",\"receiverId\":\"receiver001\",\"objectStoreKey\":\"taskclean001.bin\",\"fileHash\":\"${FILE_HASH}\",\"fileSize\":1024}\n"
)
file(WRITE "${S3_POLICY_PATH}" "{\"requiredAreas\":[\"source-write-fallback\"],\"allowedGapAreas\":[],\"minObservedAreas\":1}\n")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -TaskName "QtNetworkChatLargeFileGovernancePreview"
        -Schedule Daily
        -At "03:30"
        -RouteLogPath "${ROUTE_LOG}"
        -QueuePath "${QUEUE_LOG}"
        -SourceInstanceId "${SOURCE_INSTANCE}"
        -OutputDir "${OUTPUT_DIR}"
        -TaskDir "${TASK_DIR}"
        -EmitRouteLog
        -NoFailOnWarning
        -PackageAcceptance
        -PackageDiagnostics
        -WriteReport
        -WriteDashboard
        -DashboardPath "${DASHBOARD_PATH}"
        -DashboardMarkdownPath "${DASHBOARD_MD_PATH}"
        -WriteS3StabilityRunbook
        -S3StabilityRunbookPath "${S3_RUNBOOK_PATH}"
        -S3StabilityRunbookMarkdownPath "${S3_RUNBOOK_MD_PATH}"
        -S3CoveragePolicyPath "${S3_POLICY_PATH}"
        -WarnS3CoverageGaps
        -RunS3FailureBatchSample
        -S3FailureBatchCountPerReason 3
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
    message(FATAL_ERROR "register-large-file-governance-task.ps1 preview exited with code ${result}")
endif()

set(LAUNCHER "${TASK_DIR}/run-large-file-governance-task.ps1")
set(PREVIEW "${TASK_DIR}/scheduled-task-preview.json")
if(NOT EXISTS "${LAUNCHER}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected launcher was not created: ${LAUNCHER}")
endif()
if(NOT EXISTS "${PREVIEW}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected preview JSON was not created: ${PREVIEW}")
endif()

file(READ "${LAUNCHER}" launcher_content)
foreach(expected_text
        "run-large-file-governance.ps1"
        "-RouteLogPath"
        "-QueuePath"
        "-SourceInstanceId"
        "-OutputDir"
        "-PackageAcceptance"
        "-PackageDiagnostics"
        "-WriteReport"
        "-WriteDashboard"
        "-DashboardPath"
        "-DashboardMarkdownPath"
        "-WriteS3StabilityRunbook"
        "-S3StabilityRunbookPath"
        "-S3StabilityRunbookMarkdownPath"
        "-S3CoveragePolicyPath"
        "-WarnS3CoverageGaps"
        "-RunS3FailureBatchSample"
        "-S3FailureBatchCountPerReason"
        "exitCode=$exitCode"
        "last-run.log")
    string(FIND "${launcher_content}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Launcher missing expected text: ${expected_text}")
    endif()
endforeach()

file(READ "${PREVIEW}" preview_content)
string(JSON task_name GET "${preview_content}" "taskName")
if(NOT task_name STREQUAL "QtNetworkChatLargeFileGovernancePreview")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected taskName in preview: ${task_name}")
endif()
string(JSON should_register GET "${preview_content}" "register")
string(JSON diagnostics_path GET "${preview_content}" "diagnosticsPackagePath")
string(JSON report_path GET "${preview_content}" "reportPath")
string(JSON dashboard_path GET "${preview_content}" "dashboardPath")
string(JSON dashboard_markdown_path GET "${preview_content}" "dashboardMarkdownPath")
string(JSON s3_runbook_path GET "${preview_content}" "s3StabilityRunbookPath")
string(JSON s3_runbook_markdown_path GET "${preview_content}" "s3StabilityRunbookMarkdownPath")
string(JSON s3_policy_path GET "${preview_content}" "s3CoveragePolicyPath")
string(JSON warn_s3_coverage_gaps GET "${preview_content}" "warnS3CoverageGaps")
string(JSON s3_batch_path GET "${preview_content}" "s3FailureBatchSummaryPath")
string(JSON s3_batch_count GET "${preview_content}" "s3FailureBatchCountPerReason")
if(should_register)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview mode should not register the scheduled task")
endif()
if(diagnostics_path STREQUAL "")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview diagnosticsPackagePath should not be empty")
endif()
if(report_path STREQUAL "")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview reportPath should not be empty")
endif()
if(NOT dashboard_path MATCHES "dashboard.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview dashboardPath missing expected dashboard.json path: ${dashboard_path}")
endif()
if(NOT dashboard_markdown_path MATCHES "dashboard.md")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview dashboardMarkdownPath missing expected dashboard.md path: ${dashboard_markdown_path}")
endif()
if(NOT s3_runbook_path MATCHES "s3-runbook.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview s3StabilityRunbookPath missing expected s3-runbook.json path: ${s3_runbook_path}")
endif()
if(NOT s3_runbook_markdown_path MATCHES "s3-runbook.md")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview s3StabilityRunbookMarkdownPath missing expected s3-runbook.md path: ${s3_runbook_markdown_path}")
endif()
if(NOT warn_s3_coverage_gaps)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview warnS3CoverageGaps should be true")
endif()
if(NOT s3_policy_path MATCHES "s3-policy.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview s3CoveragePolicyPath missing expected policy path: ${s3_policy_path}")
endif()
if(NOT s3_batch_path MATCHES "s3-failure-batch-summary.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview S3 failure batch summary path missing expected file: ${s3_batch_path}")
endif()
if(NOT s3_batch_count EQUAL 3)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview S3 failure batch count should be 3, got ${s3_batch_count}")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -ReceiptPath "https://example.invalid/receipt.jsonl"
        -QueuePath "${QUEUE_LOG}"
        -SourceInstanceId "${SOURCE_INSTANCE}"
        -OutputDir "${OUTPUT_DIR}/bad"
        -TaskDir "${TEMP_DIR}/bad-task"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)

if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Scheduled task helper should reject sensitive URL-like input")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -RouteLogPath "${ROUTE_LOG}"
        -QueuePath "${QUEUE_LOG}"
        -SourceInstanceId "${SOURCE_INSTANCE}"
        -OutputDir "${OUTPUT_DIR}/bad-rotation"
        -TaskDir "${TEMP_DIR}/bad-rotation-task"
        -RotationMaxAgeDays 30
    RESULT_VARIABLE bad_rotation_result
    OUTPUT_VARIABLE bad_rotation_output
    ERROR_VARIABLE bad_rotation_error
)

if(bad_rotation_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Scheduled task helper should fail when rotation options are set without ReceiptRotationPath")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Large file governance scheduled task helper test passed")
