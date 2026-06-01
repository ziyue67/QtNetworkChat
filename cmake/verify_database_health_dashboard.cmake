if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/database_health_dashboard_sample")
set(HEALTH_JSON "${TEMP_DIR}/database-health.json")
set(STATUS_JSON "${TEMP_DIR}/database-health-status.json")
set(TASK_PREVIEW "${TEMP_DIR}/database-health-task-preview.json")
set(DASHBOARD_JSON "${TEMP_DIR}/database-health-dashboard.json")
set(DASHBOARD_MD "${TEMP_DIR}/database-health-dashboard.md")
set(BAD_HEALTH_JSON "${TEMP_DIR}/database-health-sensitive.json")
set(BAD_DASHBOARD_JSON "${TEMP_DIR}/bad-dashboard.json")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

file(WRITE "${HEALTH_JSON}"
"{
  \"format\":\"qtnetworkchat-database-health-v1\",
  \"status\":\"healthy\",
  \"ok\":true,
  \"config\":{\"driver\":\"QPSQL\",\"password\":\"<redacted>\"},
  \"checks\":[
    {\"name\":\"open\",\"ok\":true,\"detail\":\"connected\"},
    {\"name\":\"ping\",\"ok\":true,\"detail\":\"SELECT 1\"},
    {\"name\":\"required-tables\",\"ok\":true,\"detail\":\"requiredTables=10/10\"}
  ]
}
")
file(WRITE "${STATUS_JSON}"
"{
  \"format\":\"qtnetworkchat-database-health-status-v1\",
  \"status\":\"healthy\",
  \"ok\":true,
  \"driver\":\"QPSQL\",
  \"checkCount\":3,
  \"failedChecks\":[],
  \"sensitiveHits\":[]
}
")
file(WRITE "${TASK_PREVIEW}"
"{
  \"format\":\"qtnetworkchat-database-health-task-preview-v1\",
  \"taskName\":\"QtNetworkChatDatabaseHealthPreview\",
  \"register\":false,
  \"schedule\":\"Hourly\",
  \"passwordSource\":\"QTNETWORKCHAT_PGPASSWORD\",
  \"readOnly\":true
}
")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -HealthPath "${HEALTH_JSON}"
        -StatusPath "${STATUS_JSON}"
        -TaskPreviewPath "${TASK_PREVIEW}"
        -DashboardPath "${DASHBOARD_JSON}"
        -MarkdownPath "${DASHBOARD_MD}"
        -FailOnUnhealthy
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
    message(FATAL_ERROR "write-database-health-dashboard.ps1 healthy run exited with ${result}")
endif()
if(NOT EXISTS "${DASHBOARD_JSON}" OR NOT EXISTS "${DASHBOARD_MD}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Database health dashboard outputs were not created")
endif()

file(READ "${DASHBOARD_JSON}" dashboard_content)
string(JSON format GET "${dashboard_content}" "format")
string(JSON status GET "${dashboard_content}" "status")
string(JSON ok GET "${dashboard_content}" "ok")
string(JSON driver GET "${dashboard_content}" "driver")
string(JSON check_count GET "${dashboard_content}" "checkCount")
string(JSON warning_count GET "${dashboard_content}" "warningCount")
string(JSON task_configured GET "${dashboard_content}" "taskConfigured")
string(JSON password_source GET "${dashboard_content}" "passwordSource")
if(NOT format STREQUAL "qtnetworkchat-database-health-dashboard-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected dashboard format: ${format}")
endif()
if(NOT status STREQUAL "healthy" OR NOT ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Healthy inputs should produce healthy dashboard")
endif()
if(NOT driver STREQUAL "QPSQL" OR NOT "${check_count}" STREQUAL "3")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected driver/checkCount in dashboard: ${driver}/${check_count}")
endif()
if(NOT "${warning_count}" STREQUAL "0")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected warningCount=0, got ${warning_count}")
endif()
if(NOT task_configured OR NOT password_source STREQUAL "QTNETWORKCHAT_PGPASSWORD")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Dashboard should include task preview password source")
endif()
string(FIND "${dashboard_content}" "super-secret" leaked_secret)
if(NOT leaked_secret EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Dashboard JSON leaked a secret")
endif()
file(READ "${DASHBOARD_MD}" markdown_content)
string(FIND "${markdown_content}" "Database Health Dashboard" md_title)
string(FIND "${markdown_content}" "QTNETWORKCHAT_PGPASSWORD" md_password_source)
if(md_title EQUAL -1 OR md_password_source EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Dashboard Markdown is missing expected content")
endif()

file(WRITE "${BAD_HEALTH_JSON}"
"{
  \"format\":\"qtnetworkchat-database-health-v1\",
  \"status\":\"healthy\",
  \"ok\":true,
  \"config\":{\"driver\":\"QPSQL\",\"password\":\"super-secret\"},
  \"checks\":[{\"name\":\"open\",\"ok\":true}]
}
")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -HealthPath "${BAD_HEALTH_JSON}"
        -DashboardPath "${BAD_DASHBOARD_JSON}"
        -FailOnUnhealthy
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Sensitive database health dashboard input should fail with -FailOnUnhealthy")
endif()
if(NOT EXISTS "${BAD_DASHBOARD_JSON}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Sensitive dashboard run should still write diagnostic JSON")
endif()
file(READ "${BAD_DASHBOARD_JSON}" bad_dashboard_content)
string(JSON bad_status GET "${bad_dashboard_content}" "status")
string(JSON bad_warning0 GET "${bad_dashboard_content}" "warnings" 0)
if(NOT bad_status STREQUAL "unhealthy" OR NOT bad_warning0 STREQUAL "sensitive-fields-detected")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Sensitive dashboard should report unhealthy sensitive-fields-detected")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Database health dashboard test passed")
