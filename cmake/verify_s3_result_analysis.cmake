if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/s3_result_analysis_sample")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(SAMPLE_LOG "${TEMP_DIR}/sample_route.log")
set(SUMMARY_OUT "${TEMP_DIR}/summary.json")
set(ALERT_OUT "${TEMP_DIR}/alert-summary.json")

file(WRITE "${SAMPLE_LOG}"
"2025-05-30T10:00:00 redis_large_file_route event=object_write result=skipped transferId=t1 objectKey=abc123def456 receiverId=user2 fileHash=a0f0e1d2c3b4a5968778695a4b3c2d1e0f1e2d3c4b5a697887766554433221100 storeType=s3 operation=PUT reason=timeout\n"
"2025-05-30T10:00:01 redis_large_file_route event=offer_validation result=rejected transferId=t2 objectKey=def456abc789 receiverId=user3 fileHash=b0e1d2c3b4a5968778695a4b3c2d1e0f1e2d3c4b5a6978877665544332211000 storeType=s3 operation=validate reason=hash\n"
"2025-05-30T10:00:02 redis_large_file_route event=delivered result=published transferId=t3 objectKey=ghi789jkl012 receiverId=user4 fileHash=c1d2e3f4a5b6c7d8e9f0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2 storeType=s3 operation=PUT reason=success\n"
"2025-05-30T10:00:03 redis_large_file_route event=delivered result=published transferId=t4 objectKey=mno345pqr678 receiverId=user5 fileHash=d2e3f4a5b6c7d8e9f0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3 storeType=s3 operation=GET reason=success\n"
"2025-05-30T10:00:04 redis_large_file_route event=failed result=skipped transferId=t5 objectKey=stu901vwx234 receiverId=user6 fileHash=e3f4a5b6c7d8e9f0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4 storeType=s3 operation=HEAD reason=auth\n"
"2025-05-30T10:00:05 redis_large_file_route event=delivered_cleanup result=cleaned transferId=t3 objectKey=ghi789jkl012 receiverId=user4 fileHash=c1d2e3f4a5b6c7d8e9f0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2 storeType=s3 operation=DELETE reason=success\n"
"2025-05-30T10:00:06 redis_large_file_route event=failed result=skipped transferId=t6 objectKey=yza567bcd890 receiverId=user7 fileHash=f4a5b6c7d8e9f0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5 storeType=filesystem operation=PUT reason=success\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -Path "${SAMPLE_LOG}"
        -WarnTimeout 0
        -WarnAuth 0
        -SummaryPath "${SUMMARY_OUT}"
        -AlertSummaryPath "${ALERT_OUT}"
        -NoFailOnWarning
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
    message(FATAL_ERROR "analyze-s3-request-results.ps1 exited with code ${result}")
endif()

if(NOT EXISTS "${SUMMARY_OUT}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Summary JSON was not created: ${SUMMARY_OUT}")
endif()
if(NOT EXISTS "${ALERT_OUT}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Alert summary JSON was not created: ${ALERT_OUT}")
endif()

file(READ "${SUMMARY_OUT}" summary_content)
string(JSON s3_lines GET "${summary_content}" "s3LineCount")
string(JSON route_lines GET "${summary_content}" "routeLineCount")

if(NOT s3_lines EQUAL 6)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected s3LineCount=6, got ${s3_lines}")
endif()

if(NOT route_lines EQUAL 7)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected routeLineCount=7, got ${route_lines}")
endif()

file(READ "${ALERT_OUT}" alert_content)
string(JSON alert_kind GET "${alert_content}" "kind")
string(JSON alert_ok GET "${alert_content}" "ok")
string(JSON alert_s3_lines GET "${alert_content}" "metrics" "s3LineCount")
if(NOT alert_kind STREQUAL "s3-request-results")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected alert kind: ${alert_kind}")
endif()
if(alert_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected alert ok=false because timeout/auth warnings are present")
endif()
if(NOT alert_s3_lines EQUAL 6)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected alert metrics s3LineCount=6, got ${alert_s3_lines}")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "S3 request result analysis test passed: s3Lines=${s3_lines} routeLines=${route_lines}")
