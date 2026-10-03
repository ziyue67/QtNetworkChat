if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/route_log_analysis_sample")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(SAMPLE_LOG "${TEMP_DIR}/sample_route.log")
set(SUMMARY_OUT "${TEMP_DIR}/summary.json")

file(WRITE "${SAMPLE_LOG}"
"2025-05-30T10:00:00 redis_large_file_route event=delivered result=published transferId=t1 objectKey=aaa111 receiverId=user1 fileHash=aabbccdd11223344aabbccdd11223344aabbccdd11223344aabbccdd11223344 storeType=s3 operation=PUT reason=success\n"
"2025-05-30T10:00:01 redis_large_file_route event=delivered_cleanup result=cleaned transferId=t1 objectKey=aaa111 receiverId=user1 fileHash=aabbccdd11223344aabbccdd11223344aabbccdd11223344aabbccdd11223344 storeType=s3 operation=DELETE reason=success\n"
"2025-05-30T10:00:02 redis_large_file_route event=failed result=skipped transferId=t2 objectKey=bbb222 receiverId=user2 fileHash=11223344aabbccdd11223344aabbccdd11223344aabbccdd11223344aabbccdd storeType=s3 operation=PUT reason=timeout\n"
"2025-05-30T10:00:03 redis_large_file_route event=failed_received result=fallback-retained transferId=t2 objectKey=bbb222 receiverId=user2 fileHash=11223344aabbccdd11223344aabbccdd11223344aabbccdd11223344aabbccdd storeType=s3 operation=PUT reason=timeout\n"
"2025-05-30T10:00:04 redis_large_file_route event=delivered result=published transferId=t3 objectKey=ccc333 receiverId=user3 fileHash=ddeeff0011223344aabbccdd11223344aabbccdd11223344aabbccdd1122 storeType=filesystem operation=PUT reason=success\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -Path "${SAMPLE_LOG}"
        -SummaryPath "${SUMMARY_OUT}"
        -NoFailOnSensitive
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
    message(FATAL_ERROR "analyze-large-file-route-logs.ps1 exited with code ${result}")
endif()

if(NOT EXISTS "${SUMMARY_OUT}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Summary JSON was not created: ${SUMMARY_OUT}")
endif()

file(READ "${SUMMARY_OUT}" summary_content)
string(JSON route_lines GET "${summary_content}" "routeLineCount")
string(JSON route_keys GET "${summary_content}" "routeKeys")

if(NOT route_lines EQUAL 5)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected routeLineCount=5, got ${route_lines}")
endif()

if(NOT route_keys EQUAL 3)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected routeKeys=3, got ${route_keys}")
endif()

string(JSON delivered_cleaned GET "${summary_content}" "deliveredCleaned")
if(NOT delivered_cleaned EQUAL 1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected deliveredCleaned=1, got ${delivered_cleaned}")
endif()

string(JSON failed_fallback GET "${summary_content}" "failedFallbackRetained")
if(NOT failed_fallback EQUAL 1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected failedFallbackRetained=1, got ${failed_fallback}")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Route log analysis test passed: routeLines=${route_lines} routeKeys=${route_keys}")
