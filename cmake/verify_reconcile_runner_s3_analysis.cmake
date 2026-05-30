if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/reconcile_runner_s3_analysis_sample")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(ROUTE_LOG "${TEMP_DIR}/route.log")
set(QUEUE_LOG "${TEMP_DIR}/queue.jsonl")
set(OUTPUT_DIR "${TEMP_DIR}/out")
set(S3_SUMMARY "${OUTPUT_DIR}/s3-summary.json")
set(SOURCE_INSTANCE "source-test-a")
set(FILE_HASH "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")

file(WRITE "${ROUTE_LOG}"
"2026-05-30T10:00:00 redis_large_file_route event=delivered result=published sourceInstanceId=${SOURCE_INSTANCE} transferId=sampleclean001 objectKey=sampleclean001.bin receiverId=receiver001 fileHash=${FILE_HASH} bytes=1024 storeType=s3 operation=publish reason=success\n"
"2026-05-30T10:00:01 redis_large_file_route event=object_write result=skipped sourceInstanceId=${SOURCE_INSTANCE} transferId=sampleskip001 objectKey=sampleskip001.bin receiverId=receiver002 fileHash=${FILE_HASH} bytes=1024 storeType=s3 operation=write reason=network\n"
"2026-05-30T10:00:02 redis_large_file_route event=offer_validation result=rejected sourceInstanceId=${SOURCE_INSTANCE} transferId=samplereject001 objectKey=samplereject001.bin receiverId=receiver003 fileHash=${FILE_HASH} bytes=1024 storeType=s3 operation=validate reason=not_found\n"
)

file(WRITE "${QUEUE_LOG}"
"{\"transferId\":\"sampleclean001\",\"receiverId\":\"receiver001\",\"objectStoreKey\":\"sampleclean001.bin\",\"fileHash\":\"${FILE_HASH}\",\"fileSize\":1024}\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -RouteLogPath "${ROUTE_LOG}"
        -QueuePath "${QUEUE_LOG}"
        -SourceInstanceId "${SOURCE_INSTANCE}"
        -OutputDir "${OUTPUT_DIR}"
        -EmitRouteLog
        -RunS3Analysis
        -S3AnalysisSummaryPath "${S3_SUMMARY}"
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
    message(FATAL_ERROR "run-large-file-delivery-reconcile.ps1 exited with code ${result}")
endif()

if(NOT EXISTS "${S3_SUMMARY}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "S3 analysis summary was not created: ${S3_SUMMARY}")
endif()

file(READ "${S3_SUMMARY}" summary_content)
string(JSON s3_lines GET "${summary_content}" "s3LineCount")
string(JSON route_lines GET "${summary_content}" "routeLineCount")

if(NOT s3_lines EQUAL 2)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected s3LineCount=2, got ${s3_lines}")
endif()

if(NOT route_lines EQUAL 3)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected routeLineCount=3, got ${route_lines}")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Reconcile runner S3 analysis test passed: s3Lines=${s3_lines} routeLines=${route_lines}")
