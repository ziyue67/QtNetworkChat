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
set(METRICS_HEALTH_JSON "${TEMP_DIR}/database-health-query-metrics.json")
set(METRICS_DASHBOARD_JSON "${TEMP_DIR}/metrics-dashboard.json")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

file(WRITE "${HEALTH_JSON}"
"{
  \"format\":\"qtnetworkchat-database-health-v1\",
  \"status\":\"healthy\",
  \"ok\":true,
  \"config\":{\"driver\":\"QPSQL\",\"password\":\"<redacted>\"},
  \"reconnectPolicy\":{\"poolEnabled\":true,\"maxConnections\":16,\"idleMs\":300000,\"backoffMs\":2000,\"threadPolicy\":{\"connectionOwnership\":\"thread-affine pooled connections\",\"crossThreadReuse\":false}},
  \"checks\":[
    {\"name\":\"open\",\"ok\":true,\"detail\":\"connected\"},
    {\"name\":\"ping\",\"ok\":true,\"detail\":\"SELECT 1\"},
    {\"name\":\"required-tables\",\"ok\":true,\"detail\":\"requiredTables=10/10\"}
  ],
  \"queryMetrics\":{\"slowQueryThresholdMs\":1000,\"slowQueryCount\":0,\"queryFailureCount\":0,\"lastSlowQueryMs\":0,\"lastErrorReason\":\"\",\"lastErrorCheck\":\"\",\"lastErrorSample\":\"\"}
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
string(JSON slow_query_count GET "${dashboard_content}" "queryMetrics" "slowQueryCount")
string(JSON query_failure_count GET "${dashboard_content}" "queryMetrics" "queryFailureCount")
string(JSON last_error_check GET "${dashboard_content}" "queryMetrics" "lastErrorCheck")
string(JSON summary_readiness GET "${dashboard_content}" "summary" "readiness")
string(JSON summary_operator_action GET "${dashboard_content}" "summary" "operatorAction")
string(JSON audit_release_gate GET "${dashboard_content}" "auditSummary" "releaseGate")
string(JSON audit_focus0 GET "${dashboard_content}" "auditSummary" "auditFocus" 0)
string(JSON pool_enabled GET "${dashboard_content}" "reconnectPolicy" "poolEnabled")
string(JSON thread_ownership GET "${dashboard_content}" "reconnectPolicy" "threadPolicy" "connectionOwnership")
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
if((NOT slow_query_count EQUAL 0) OR (NOT query_failure_count EQUAL 0) OR NOT last_error_check STREQUAL "")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Healthy dashboard should expose zero query metrics and empty last error fields")
endif()
if(NOT summary_readiness STREQUAL "verified"
    OR NOT summary_operator_action STREQUAL "Archive the redacted dashboard for release readiness review."
    OR NOT audit_release_gate STREQUAL "can-review-health-evidence"
    OR NOT audit_focus0 STREQUAL "routine-health-review")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Healthy dashboard should expose release gate and audit focus")
endif()
if(NOT task_configured OR NOT password_source STREQUAL "QTNETWORKCHAT_PGPASSWORD")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Dashboard should include task preview password source")
endif()
if(NOT pool_enabled OR NOT thread_ownership STREQUAL "thread-affine pooled connections")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Dashboard should surface pooled thread policy")
endif()
string(FIND "${dashboard_content}" "super-secret" leaked_secret)
if(NOT leaked_secret EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Dashboard JSON leaked a secret")
endif()
file(READ "${DASHBOARD_MD}" markdown_content)
string(FIND "${markdown_content}" "Database Health Dashboard" md_title)
string(FIND "${markdown_content}" "QTNETWORKCHAT_PGPASSWORD" md_password_source)
string(FIND "${markdown_content}" "Thread connection ownership" md_thread_ownership)
string(FIND "${markdown_content}" "Release gate" md_release_gate)
if(md_title EQUAL -1 OR md_password_source EQUAL -1 OR md_thread_ownership EQUAL -1 OR md_release_gate EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Dashboard Markdown is missing expected content")
endif()

file(WRITE "${METRICS_HEALTH_JSON}"
"{
  \"format\":\"qtnetworkchat-database-health-v1\",
  \"status\":\"healthy\",
  \"ok\":true,
  \"config\":{\"driver\":\"QPSQL\",\"password\":\"<redacted>\"},
  \"reconnectPolicy\":{\"poolEnabled\":true,\"maxConnections\":16,\"idleMs\":300000,\"backoffMs\":2000,\"threadPolicy\":{\"connectionOwnership\":\"thread-affine pooled connections\",\"crossThreadReuse\":false}},
  \"checks\":[{\"name\":\"open\",\"ok\":true,\"detail\":\"connected\"}],
  \"queryMetrics\":{\"slowQueryThresholdMs\":750,\"slowQueryCount\":2,\"queryFailureCount\":1,\"lastSlowQueryMs\":2200,\"lastErrorReason\":\"query\",\"lastErrorCheck\":\"postgres-required-tables\",\"lastErrorSample\":\"requiredTables=unknown/10\"}
}
")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -HealthPath "${METRICS_HEALTH_JSON}"
        -DashboardPath "${METRICS_DASHBOARD_JSON}"
    RESULT_VARIABLE metrics_result
    OUTPUT_VARIABLE metrics_output
    ERROR_VARIABLE metrics_error
)
if(NOT metrics_result EQUAL 0 OR NOT EXISTS "${METRICS_DASHBOARD_JSON}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Query metrics dashboard run should write diagnostic JSON")
endif()
file(READ "${METRICS_DASHBOARD_JSON}" metrics_dashboard_content)
string(JSON metrics_status GET "${metrics_dashboard_content}" "status")
string(JSON metrics_ok GET "${metrics_dashboard_content}" "ok")
string(JSON metrics_warning_count GET "${metrics_dashboard_content}" "warningCount")
string(JSON metrics_slow_count GET "${metrics_dashboard_content}" "queryMetrics" "slowQueryCount")
string(JSON metrics_failure_count GET "${metrics_dashboard_content}" "queryMetrics" "queryFailureCount")
string(JSON metrics_last_error_check GET "${metrics_dashboard_content}" "queryMetrics" "lastErrorCheck")
string(JSON metrics_release_gate GET "${metrics_dashboard_content}" "auditSummary" "releaseGate")
string(FIND "${metrics_dashboard_content}" "database-query-failures" metrics_failure_warning)
string(FIND "${metrics_dashboard_content}" "database-slow-queries" metrics_slow_warning)
if(NOT metrics_status STREQUAL "healthy" OR NOT metrics_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Query metrics alone should keep dashboard health status tied to checks")
endif()
if((NOT metrics_warning_count EQUAL 2) OR (NOT metrics_slow_count EQUAL 2) OR (NOT metrics_failure_count EQUAL 1)
    OR metrics_failure_warning EQUAL -1 OR metrics_slow_warning EQUAL -1 OR NOT metrics_last_error_check STREQUAL "postgres-required-tables"
    OR NOT metrics_release_gate STREQUAL "review-query-failures")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Query metrics dashboard should surface slow query/query failure warnings and last error check")
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
