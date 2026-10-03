if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/database_health_alert_sample")
set(DASHBOARD_HEALTHY "${TEMP_DIR}/dashboard-healthy.json")
set(DASHBOARD_UNHEALTHY "${TEMP_DIR}/dashboard-unhealthy.json")
set(DASHBOARD_SENSITIVE "${TEMP_DIR}/dashboard-sensitive.json")
set(ALERT_JSON "${TEMP_DIR}/database-health-alert.json")
set(ALERT_MD "${TEMP_DIR}/database-health-alert.md")
set(SENSITIVE_ALERT_JSON "${TEMP_DIR}/database-health-sensitive-alert.json")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

file(WRITE "${DASHBOARD_HEALTHY}"
"{
  \"format\":\"qtnetworkchat-database-health-dashboard-v1\",
  \"status\":\"healthy\",
  \"ok\":true,
  \"driver\":\"QPSQL\",
  \"failedChecks\":[],
  \"warnings\":[],
  \"sensitiveHits\":[]
}
")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -DashboardPath "${DASHBOARD_HEALTHY}"
        -AlertPath "${ALERT_JSON}"
        -MarkdownPath "${ALERT_MD}"
        -DryRun
        -FailOnUnhealthy
    RESULT_VARIABLE healthy_result
    OUTPUT_VARIABLE healthy_output
    ERROR_VARIABLE healthy_error
)
if(NOT healthy_output STREQUAL "")
    message(STATUS "${healthy_output}")
endif()
if(NOT healthy_error STREQUAL "")
    message(STATUS "${healthy_error}")
endif()
if(NOT healthy_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Healthy database alert run failed with ${healthy_result}")
endif()
if(NOT EXISTS "${ALERT_JSON}" OR NOT EXISTS "${ALERT_MD}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Healthy database alert outputs were not created")
endif()
file(READ "${ALERT_JSON}" alert_content)
string(JSON alert_format GET "${alert_content}" "format")
string(JSON alert_status GET "${alert_content}" "status")
string(JSON alert_ok GET "${alert_content}" "ok")
string(JSON alert_severity GET "${alert_content}" "severity")
string(JSON alert_notify GET "${alert_content}" "notify")
if(NOT alert_format STREQUAL "qtnetworkchat-database-health-alert-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected alert format: ${alert_format}")
endif()
if(NOT alert_status STREQUAL "healthy" OR NOT alert_ok OR NOT alert_severity STREQUAL "info" OR alert_notify)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Healthy alert should be ok info and not notify")
endif()

file(WRITE "${DASHBOARD_UNHEALTHY}"
"{
  \"format\":\"qtnetworkchat-database-health-dashboard-v1\",
  \"status\":\"unhealthy\",
  \"ok\":false,
  \"driver\":\"QPSQL\",
  \"failedChecks\":[\"required-tables\"],
  \"warnings\":[\"database-checks-failed\"],
  \"sensitiveHits\":[]
}
")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -DashboardPath "${DASHBOARD_UNHEALTHY}"
        -DryRun
    RESULT_VARIABLE unhealthy_result
    OUTPUT_VARIABLE unhealthy_output
    ERROR_VARIABLE unhealthy_error
)
if(NOT unhealthy_output STREQUAL "")
    message(STATUS "${unhealthy_output}")
endif()
if(NOT unhealthy_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unhealthy dry-run alert should exit 0 without -FailOnUnhealthy")
endif()
string(FIND "${unhealthy_output}" "dry-run: would write EventLog" dryrun_pos)
string(FIND "${unhealthy_output}" "\"severity\":  \"warning\"" warning_pos)
if(dryrun_pos EQUAL -1 OR warning_pos EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unhealthy dry-run should notify and report warning severity")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -DashboardPath "${DASHBOARD_UNHEALTHY}"
        -DryRun
        -FailOnUnhealthy
    RESULT_VARIABLE unhealthy_fail_result
    OUTPUT_VARIABLE unhealthy_fail_output
    ERROR_VARIABLE unhealthy_fail_error
)
if(unhealthy_fail_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unhealthy alert should fail with -FailOnUnhealthy")
endif()

file(WRITE "${DASHBOARD_SENSITIVE}"
"{
  \"format\":\"qtnetworkchat-database-health-dashboard-v1\",
  \"status\":\"healthy\",
  \"ok\":true,
  \"driver\":\"QPSQL\",
  \"failedChecks\":[],
  \"warnings\":[],
  \"sensitiveHits\":[],
  \"config\":{\"password\":\"super-secret\"}
}
")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -DashboardPath "${DASHBOARD_SENSITIVE}"
        -AlertPath "${SENSITIVE_ALERT_JSON}"
        -DryRun
        -FailOnUnhealthy
    RESULT_VARIABLE sensitive_result
    OUTPUT_VARIABLE sensitive_output
    ERROR_VARIABLE sensitive_error
)
if(sensitive_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Sensitive dashboard alert should fail with -FailOnUnhealthy")
endif()
if(NOT EXISTS "${SENSITIVE_ALERT_JSON}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Sensitive alert should still write alert JSON")
endif()
file(READ "${SENSITIVE_ALERT_JSON}" sensitive_alert_content)
string(JSON sensitive_severity GET "${sensitive_alert_content}" "severity")
string(JSON sensitive_hit0 GET "${sensitive_alert_content}" "sensitiveHits" 0)
if(NOT sensitive_severity STREQUAL "critical" OR sensitive_hit0 STREQUAL "")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Sensitive alert should be critical with sensitiveHits")
endif()
string(FIND "${sensitive_output}" "super-secret" leaked_secret_output)
string(FIND "${sensitive_alert_content}" "super-secret" leaked_secret_json)
if(NOT leaked_secret_output EQUAL -1 OR NOT leaked_secret_json EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Sensitive alert leaked secret content")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -DashboardPath "${TEMP_DIR}/missing.json"
        -DryRun
    RESULT_VARIABLE missing_result
    OUTPUT_VARIABLE missing_output
    ERROR_VARIABLE missing_error
)
if(missing_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Missing dashboard should fail")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Database health alert test passed")
