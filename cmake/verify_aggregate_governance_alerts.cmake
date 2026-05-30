if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/aggregate_governance_alerts_sample")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(AGG_OUT "${TEMP_DIR}/governance-alert-overview.json")

# Test 1: empty directory -> ok=true, alertCount=0
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -Command
        "& '${SCRIPT_PATH}' -OutputDir '${TEMP_DIR}' -AggregatedPath '${AGG_OUT}'"
    RESULT_VARIABLE result1
    OUTPUT_VARIABLE output1
    ERROR_VARIABLE error1
)
if(NOT output1 STREQUAL "")
    message(STATUS "${output1}")
endif()
if(NOT result1 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test1 (empty dir) failed with exit code ${result1}: ${error1}")
endif()
if(NOT EXISTS "${AGG_OUT}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test1: aggregated output not created")
endif()
file(READ "${AGG_OUT}" agg1_content)
string(JSON agg1_ok GET "${agg1_content}" "ok")
string(JSON agg1_count GET "${agg1_content}" "alertCount")
string(JSON agg1_warnings GET "${agg1_content}" "totalWarnings")
if(NOT agg1_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test1: expected ok=true, got ${agg1_ok}")
endif()
if(NOT agg1_count EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test1: expected alertCount=0, got ${agg1_count}")
endif()
file(REMOVE "${AGG_OUT}")

# Test 2: all-ok alert summaries -> aggregated ok=true, alertCount=2
file(WRITE "${TEMP_DIR}/large-file-route-alert-summary.json"
"{\"kind\":\"large-file-route-summary\",\"ok\":true,\"warnings\":[],\"metrics\":{\"totalEntries\":10}}\n"
)
file(WRITE "${TEMP_DIR}/s3-request-results-alert-summary.json"
"{\"kind\":\"s3-request-results\",\"ok\":true,\"warnings\":[],\"metrics\":{\"totalRequests\":5}}\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -Command
        "& '${SCRIPT_PATH}' -OutputDir '${TEMP_DIR}' -AggregatedPath '${AGG_OUT}'"
    RESULT_VARIABLE result2
    OUTPUT_VARIABLE output2
    ERROR_VARIABLE error2
)
if(NOT output2 STREQUAL "")
    message(STATUS "${output2}")
endif()
if(NOT result2 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test2 (all-ok) failed with exit code ${result2}: ${error2}")
endif()
file(READ "${AGG_OUT}" agg2_content)
string(JSON agg2_ok GET "${agg2_content}" "ok")
string(JSON agg2_count GET "${agg2_content}" "alertCount")
if(NOT agg2_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test2: expected ok=true")
endif()
if(NOT agg2_count EQUAL 2)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test2: expected alertCount=2, got ${agg2_count}")
endif()
file(REMOVE "${AGG_OUT}")

# Test 3: one warning alert -> fails without NoFailOnWarning
file(WRITE "${TEMP_DIR}/receipt-rotation-alert-summary.json"
"{\"kind\":\"large-file-receipt-rotation\",\"ok\":false,\"warnings\":[\"archivedRecords=500 exceeds threshold 100\"],\"metrics\":{\"totalSummaries\":1}}\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -Command
        "& '${SCRIPT_PATH}' -OutputDir '${TEMP_DIR}' -AggregatedPath '${AGG_OUT}'"
    RESULT_VARIABLE result3
    OUTPUT_VARIABLE output3
    ERROR_VARIABLE error3
)
if(result3 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test3: expected failure when warnings present without NoFailOnWarning")
endif()

# Test 4: warning + NoFailOnWarning -> exits 0, ok=false
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -Command
        "& '${SCRIPT_PATH}' -OutputDir '${TEMP_DIR}' -AggregatedPath '${AGG_OUT}' -NoFailOnWarning"
    RESULT_VARIABLE result4
    OUTPUT_VARIABLE output4
    ERROR_VARIABLE error4
)
if(NOT output4 STREQUAL "")
    message(STATUS "${output4}")
endif()
if(NOT result4 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test4 (NoFailOnWarning) failed with exit code ${result4}: ${error4}")
endif()
file(READ "${AGG_OUT}" agg4_content)
string(JSON agg4_ok GET "${agg4_content}" "ok")
string(JSON agg4_count GET "${agg4_content}" "alertCount")
string(JSON agg4_warnings GET "${agg4_content}" "totalWarnings")
if(agg4_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test4: expected ok=false")
endif()
if(NOT agg4_count EQUAL 3)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test4: expected alertCount=3, got ${agg4_count}")
endif()
if(NOT agg4_warnings EQUAL 1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test4: expected totalWarnings=1, got ${agg4_warnings}")
endif()

# Test 5: verify no sensitive fields injected by aggregator
string(FIND "${agg4_content}" "endpoint" sensitive_pos1)
string(FIND "${agg4_content}" "access_key" sensitive_pos2)
string(FIND "${agg4_content}" "secret_key" sensitive_pos3)
string(FIND "${agg4_content}" "Authorization" sensitive_pos4)
if(NOT sensitive_pos1 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test5: aggregator output contains 'endpoint'")
endif()
if(NOT sensitive_pos2 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test5: aggregator output contains 'access_key'")
endif()
if(NOT sensitive_pos3 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test5: aggregator output contains 'secret_key'")
endif()
if(NOT sensitive_pos4 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test5: aggregator output contains 'Authorization'")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Aggregate governance alerts test passed: empty=${result1} all-ok=${result2} strict-fail=${result3} tolerant=${result4}")
