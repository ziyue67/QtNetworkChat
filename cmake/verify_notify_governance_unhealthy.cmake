if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/notify_governance_unhealthy_sample")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(HEALTH_HEALTHY "${TEMP_DIR}/health-healthy.json")
set(HEALTH_UNHEALTHY "${TEMP_DIR}/health-unhealthy.json")
set(HEALTH_SENSITIVE "${TEMP_DIR}/health-sensitive.json")

# --- Test 1: healthy status -> exit 0, no notification
file(WRITE "${HEALTH_HEALTHY}"
"{\"status\":\"healthy\",\"reason\":\"all checks passed\",\"ok\":true,\"totalWarnings\":0,\"alertCount\":2}\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -HealthCheckPath "${HEALTH_HEALTHY}" -DryRun
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

# --- Test 2: unhealthy status + DryRun -> exit 0, dry-run output
file(WRITE "${HEALTH_UNHEALTHY}"
"{\"status\":\"unhealthy\",\"reason\":\"overview.ok=false\",\"ok\":false,\"totalWarnings\":3,\"alertCount\":2}\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -HealthCheckPath "${HEALTH_UNHEALTHY}" -DryRun
    RESULT_VARIABLE result2
    OUTPUT_VARIABLE output2
    ERROR_VARIABLE error2
)
if(NOT output2 STREQUAL "")
    message(STATUS "${output2}")
endif()
if(NOT result2 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test2 (unhealthy dry-run) failed with exit code ${result2}: ${error2}")
endif()
string(FIND "${output2}" "dry-run: would write EventLog" dryrun_pos)
if(dryrun_pos EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test2: expected dry-run EventLog message in output")
endif()

# --- Test 3: sensitive message rejection
file(WRITE "${HEALTH_SENSITIVE}"
"{\"status\":\"unhealthy\",\"reason\":\"endpoint=http://minio:9000 leaked\",\"ok\":false,\"totalWarnings\":1,\"alertCount\":1}\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -HealthCheckPath "${HEALTH_SENSITIVE}" -DryRun
    RESULT_VARIABLE result3
    OUTPUT_VARIABLE output3
    ERROR_VARIABLE error3
)
if(result3 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test3 (sensitive) should have failed but exited 0")
endif()

# --- Test 4: missing file -> error
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -HealthCheckPath "${TEMP_DIR}/nonexistent.json" -DryRun
    RESULT_VARIABLE result4
    OUTPUT_VARIABLE output4
    ERROR_VARIABLE error4
)
if(result4 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test4 (missing file) should have failed but exited 0")
endif()

# --- Test 5: no sensitive fields in dry-run output
string(FIND "${output2}" "access_key" sens1)
string(FIND "${output2}" "secret_key" sens2)
string(FIND "${output2}" "Authorization" sens3)
string(FIND "${output2}" "Credential" sens4)
if(NOT sens1 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test5: output contains 'access_key'")
endif()
if(NOT sens2 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test5: output contains 'secret_key'")
endif()
if(NOT sens3 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test5: output contains 'Authorization'")
endif()
if(NOT sens4 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Test5: output contains 'Credential'")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Notify governance unhealthy test passed: healthy=${result1} dryrun=${result2} sensitive=${result3} missing=${result4}")
