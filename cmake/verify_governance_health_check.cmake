if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/check_governance_health_sample")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(OVERVIEW_OK "${TEMP_DIR}/overview-ok.json")
set(OVERVIEW_WARN "${TEMP_DIR}/overview-warn.json")
set(HEALTH_OUT "${TEMP_DIR}/health-output.json")

# --- Test 1: healthy overview -> exit 0, status=healthy
file(WRITE "${OVERVIEW_OK}"
"{\"ok\":true,\"totalWarnings\":0,\"alertCount\":2,\"alerts\":[]}\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -AlertOverviewPath "${OVERVIEW_OK}" -HealthOutputPath "${HEALTH_OUT}"
    RESULT_VARIABLE result1
    OUTPUT_VARIABLE output1
    ERROR_VARIABLE error1
)
if(NOT output1 STREQUAL "")
    message(STATUS "${output1}")
endif()
if(NOT result1 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test1 (healthy) failed with exit code ${result1}: ${error1}")
endif()
if(NOT EXISTS "${HEALTH_OUT}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test1: health output not created")
endif()
file(READ "${HEALTH_OUT}" health1_content)
string(JSON health1_status GET "${health1_content}" "status")
string(JSON health1_ok GET "${health1_content}" "ok")
if(NOT health1_status STREQUAL "healthy")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test1: expected status=healthy, got ${health1_status}")
endif()
if(NOT health1_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test1: expected ok=true")
endif()
file(REMOVE "${HEALTH_OUT}")

# --- Test 2: unhealthy overview (ok=false) -> exit non-zero, status=unhealthy
file(WRITE "${OVERVIEW_WARN}"
"{\"ok\":false,\"totalWarnings\":3,\"alertCount\":2,\"alerts\":[]}\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -AlertOverviewPath "${OVERVIEW_WARN}" -HealthOutputPath "${HEALTH_OUT}"
    RESULT_VARIABLE result2
    OUTPUT_VARIABLE output2
    ERROR_VARIABLE error2
)
if(NOT output2 STREQUAL "")
    message(STATUS "${output2}")
endif()
if(result2 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test2 (unhealthy) should have exited non-zero")
endif()
if(NOT EXISTS "${HEALTH_OUT}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test2: health output not created")
endif()
file(READ "${HEALTH_OUT}" health2_content)
string(JSON health2_status GET "${health2_content}" "status")
string(JSON health2_ok GET "${health2_content}" "ok")
if(NOT health2_status STREQUAL "unhealthy")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test2: expected status=unhealthy, got ${health2_status}")
endif()
if(health2_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test2: expected ok=false")
endif()
file(REMOVE "${HEALTH_OUT}")

# --- Test 3: MaxWarnings tolerance -> warnings=2 with MaxWarnings=5 is healthy
file(WRITE "${TEMP_DIR}/overview-tolerable.json"
"{\"ok\":true,\"totalWarnings\":2,\"alertCount\":1,\"alerts\":[]}\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -AlertOverviewPath "${TEMP_DIR}/overview-tolerable.json" -HealthOutputPath "${HEALTH_OUT}" -MaxWarnings 5
    RESULT_VARIABLE result3
    OUTPUT_VARIABLE output3
    ERROR_VARIABLE error3
)
if(NOT output3 STREQUAL "")
    message(STATUS "${output3}")
endif()
if(NOT result3 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test3 (MaxWarnings tolerance) failed with exit code ${result3}: ${error3}")
endif()
file(READ "${HEALTH_OUT}" health3_content)
string(JSON health3_status GET "${health3_content}" "status")
if(NOT health3_status STREQUAL "healthy")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test3: expected status=healthy with MaxWarnings tolerance, got ${health3_status}")
endif()
file(REMOVE "${HEALTH_OUT}")

# --- Test 4: missing overview file -> exit non-zero, status=unknown
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -AlertOverviewPath "${TEMP_DIR}/nonexistent.json" -HealthOutputPath "${HEALTH_OUT}" -Quiet
    RESULT_VARIABLE result4
    OUTPUT_VARIABLE output4
    ERROR_VARIABLE error4
)
if(result4 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test4 (missing file) should have exited non-zero, got 0")
endif()
if(NOT EXISTS "${HEALTH_OUT}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test4: health output not created for missing overview")
endif()
file(READ "${HEALTH_OUT}" health4_content)
string(JSON health4_status GET "${health4_content}" "status")
if(NOT health4_status STREQUAL "unknown")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test4: expected status=unknown, got ${health4_status}")
endif()

# --- Test 5: no sensitive fields injected by health checker
string(FIND "${health1_content}" "endpoint" sens1)
string(FIND "${health1_content}" "access_key" sens2)
string(FIND "${health1_content}" "secret_key" sens3)
string(FIND "${health1_content}" "Authorization" sens4)
if(NOT sens1 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test5: output contains 'endpoint'")
endif()
if(NOT sens2 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test5: output contains 'access_key'")
endif()
if(NOT sens3 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test5: output contains 'secret_key'")
endif()
if(NOT sens4 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test5: output contains 'Authorization'")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Governance health check test passed: healthy=${result1} unhealthy=${result2} tolerant=${result3} missing=${result4}")
