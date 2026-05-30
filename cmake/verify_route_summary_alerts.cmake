if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/route_summary_alerts_sample")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(SUMMARY_CLEAN "${TEMP_DIR}/input_summary_clean.json")
set(SUMMARY_WARN "${TEMP_DIR}/input_summary_warn.json")
set(ALERT_OUT "${TEMP_DIR}/route-alert-summary.json")

# Clean summary: no thresholds exceeded
file(WRITE "${SUMMARY_CLEAN}"
"{\n"
"  \"routeLineCount\": 10,\n"
"  \"routeKeys\": 3,\n"
"  \"deliveredWithoutCleanup\": 0,\n"
"  \"reconcileRetained\": 1,\n"
"  \"failedFallbackRetained\": 1,\n"
"  \"failedWithoutFallback\": 0,\n"
"  \"sensitiveHits\": 0\n"
"}\n"
)

# Warning summary: failedWithoutFallback=1 triggers threshold
file(WRITE "${SUMMARY_WARN}"
"{\n"
"  \"routeLineCount\": 5,\n"
"  \"routeKeys\": 2,\n"
"  \"deliveredWithoutCleanup\": 1,\n"
"  \"reconcileRetained\": 0,\n"
"  \"failedFallbackRetained\": 0,\n"
"  \"failedWithoutFallback\": 1,\n"
"  \"sensitiveHits\": 0\n"
"}\n"
)

# Test 1: no warnings (thresholds high enough)
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -SummaryPath "${SUMMARY_CLEAN}"
        -WarnFailedWithoutFallback 5
        -WarnDeliveredWithoutCleanup 5
        -WarnSensitiveHits 5
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
    message(FATAL_ERROR "Route summary alerts (no-warning case) exited with code ${result}")
endif()

# Test 2: warnings triggered (thresholds at 0) with NoFailOnWarning
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -SummaryPath "${SUMMARY_WARN}"
        -WarnFailedWithoutFallback 0
        -WarnDeliveredWithoutCleanup 0
        -WarnSensitiveHits 0
        -AlertSummaryPath "${ALERT_OUT}"
        -NoFailOnWarning
    RESULT_VARIABLE result2
    OUTPUT_VARIABLE output2
    ERROR_VARIABLE error_output2
)

if(NOT output2 STREQUAL "")
    message(STATUS "${output2}")
endif()
if(NOT error_output2 STREQUAL "")
    message(STATUS "${error_output2}")
endif()

if(NOT result2 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Route summary alerts (warning case) exited with code ${result2}")
endif()
if(NOT EXISTS "${ALERT_OUT}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Route alert summary JSON was not created: ${ALERT_OUT}")
endif()
file(READ "${ALERT_OUT}" alert_content)
string(JSON alert_kind GET "${alert_content}" "kind")
string(JSON alert_ok GET "${alert_content}" "ok")
string(JSON alert_failed_without_fallback GET "${alert_content}" "metrics" "failedWithoutFallback")
if(NOT alert_kind STREQUAL "large-file-route-summary")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected route alert kind: ${alert_kind}")
endif()
if(alert_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected route alert ok=false because warnings are present")
endif()
if(NOT alert_failed_without_fallback EQUAL 1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected failedWithoutFallback=1, got ${alert_failed_without_fallback}")
endif()

# Test 3: warnings triggered without NoFailOnWarning should fail
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -SummaryPath "${SUMMARY_WARN}"
        -WarnFailedWithoutFallback 0
        -WarnDeliveredWithoutCleanup 0
        -WarnSensitiveHits 0
    RESULT_VARIABLE result3
    OUTPUT_VARIABLE output3
    ERROR_VARIABLE error_output3
)

if(result3 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Route summary alerts should have failed when warnings found without NoFailOnWarning")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Route summary alerts test passed: no-warning=${result} warning-tolerant=${result2} warning-strict=${result3}")
