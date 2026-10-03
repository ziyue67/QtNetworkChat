if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/s3_failure_batch_sample")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(OUTPUT_DIR "${TEMP_DIR}/out")
set(ROUTE_LOG "${OUTPUT_DIR}/s3-failure-batch-route.log")
set(SUMMARY_OUT "${OUTPUT_DIR}/s3-failure-batch-summary.json")
set(ALERT_OUT "${OUTPUT_DIR}/s3-failure-batch-alert-summary.json")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}"
        -CountPerReason 3
        -RunAnalysis
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
    message(FATAL_ERROR "write-s3-failure-batch-sample.ps1 exited with code ${result}")
endif()

foreach(expected_file "${ROUTE_LOG}" "${SUMMARY_OUT}" "${ALERT_OUT}")
    if(NOT EXISTS "${expected_file}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Expected batch sample output missing: ${expected_file}")
    endif()
endforeach()

file(READ "${ROUTE_LOG}" route_content)
foreach(forbidden_text
        "endpoint="
        "bucket="
        "objectUrl="
        "https://"
        "access key"
        "secret key"
        "session token"
        "Authorization"
        "Credential"
        "Signature")
    string(FIND "${route_content}" "${forbidden_text}" found_forbidden)
    if(NOT found_forbidden EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Batch sample route log contains forbidden text: ${forbidden_text}")
    endif()
endforeach()

file(READ "${SUMMARY_OUT}" summary_content)
string(JSON s3_lines GET "${summary_content}" "s3LineCount")
string(JSON route_lines GET "${summary_content}" "routeLineCount")
string(JSON timeout_count GET "${summary_content}" "reasonCounts" "timeout")
string(JSON retryable_count GET "${summary_content}" "reasonCounts" "retryable")
string(JSON auth_count GET "${summary_content}" "reasonCounts" "auth")
string(JSON tls_count GET "${summary_content}" "reasonCounts" "tls")
string(JSON hash_count GET "${summary_content}" "reasonCounts" "hash")
string(JSON size_count GET "${summary_content}" "reasonCounts" "size")
string(JSON deliver_count GET "${summary_content}" "operationCounts" "deliver")
string(JSON delete_count GET "${summary_content}" "operationCounts" "delete")
string(JSON sensitive_hits GET "${summary_content}" "sensitiveHits")

if(NOT s3_lines EQUAL 33 OR NOT route_lines EQUAL 33)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected 33 route/S3 lines, got route=${route_lines} s3=${s3_lines}")
endif()
foreach(count_value ${timeout_count} ${retryable_count} ${auth_count} ${tls_count} ${hash_count} ${size_count})
    if(NOT count_value EQUAL 3)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Expected each watched reason count to be 3, got ${count_value}")
    endif()
endforeach()
if(NOT deliver_count EQUAL 6)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected deliver operation count=6, got ${deliver_count}")
endif()
if(NOT delete_count EQUAL 6)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected delete operation count=6, got ${delete_count}")
endif()
if(NOT sensitive_hits EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected sensitiveHits=0, got ${sensitive_hits}")
endif()

file(READ "${ALERT_OUT}" alert_content)
string(JSON alert_kind GET "${alert_content}" "kind")
string(JSON alert_ok GET "${alert_content}" "ok")
string(JSON alert_timeout GET "${alert_content}" "metrics" "reasonCounts" "timeout")
if(NOT alert_kind STREQUAL "s3-request-results")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected alert kind: ${alert_kind}")
endif()
if(alert_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected alert ok=false because batch sample intentionally exceeds thresholds")
endif()
if(NOT alert_timeout EQUAL 3)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected alert timeout count=3, got ${alert_timeout}")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${TEMP_DIR}/bad"
        -CountPerReason 0
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Batch sample should fail when CountPerReason is 0")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "S3 failure batch sample test passed: s3Lines=${s3_lines}")
