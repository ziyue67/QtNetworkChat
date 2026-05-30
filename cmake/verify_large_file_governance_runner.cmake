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
        "${OUTPUT_DIR}/s3-request-results-summary.json"
        "${OUTPUT_DIR}/reconcile/receipts.jsonl"
        "${OUTPUT_DIR}/reconcile/fallbacks.jsonl"
        "${OUTPUT_DIR}/reconcile/s3-analysis-summary.json"
        "${OUTPUT_DIR}/receipt-rotation-summary.json"
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

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Large file governance runner test passed")
