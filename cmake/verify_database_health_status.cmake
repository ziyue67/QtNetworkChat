if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/database_health_status_sample")
set(HEALTH_JSON "${TEMP_DIR}/database-health.json")
set(STATUS_JSON "${TEMP_DIR}/database-health-status.json")
set(STATUS_MD "${TEMP_DIR}/database-health-status.md")
set(BAD_HEALTH_JSON "${TEMP_DIR}/database-health-sensitive.json")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

file(WRITE "${HEALTH_JSON}"
"{
  \"format\":\"qtnetworkchat-database-health-v1\",
  \"status\":\"healthy\",
  \"ok\":true,
  \"config\":{\"driver\":\"QPSQL\",\"host\":\"127.0.0.1\",\"port\":5432,\"database\":\"qtnetworkchat\",\"user\":\"postgres\",\"password\":\"<redacted>\"},
  \"reconnectPolicy\":{\"poolEnabled\":true,\"maxConnections\":16,\"idleMs\":300000,\"backoffMs\":2000,\"threadPolicy\":{\"connectionOwnership\":\"thread-affine pooled connections\",\"crossThreadReuse\":false}},
  \"checks\":[
    {\"name\":\"open\",\"ok\":true,\"detail\":\"connected\"},
    {\"name\":\"ping\",\"ok\":true,\"detail\":\"SELECT 1\"},
    {\"name\":\"required-tables\",\"ok\":true,\"detail\":\"requiredTables=10/10\"}
  ],
  \"queryMetrics\":{\"slowQueryThresholdMs\":750,\"slowQueryCount\":1,\"queryFailureCount\":2,\"lastSlowQueryMs\":1800,\"lastErrorReason\":\"network\",\"lastErrorCheck\":\"postgres-required-tables\",\"lastErrorSample\":\"connection refused while reading required tables\"}
}
")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -HealthPath "${HEALTH_JSON}"
        -JsonPath "${STATUS_JSON}"
        -MarkdownPath "${STATUS_MD}"
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
    message(FATAL_ERROR "show-database-health-status.ps1 healthy run exited with ${result}")
endif()
if(NOT EXISTS "${STATUS_JSON}" OR NOT EXISTS "${STATUS_MD}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Database health status outputs were not created")
endif()

file(READ "${STATUS_JSON}" status_content)
string(JSON format GET "${status_content}" "format")
string(JSON status GET "${status_content}" "status")
string(JSON ok GET "${status_content}" "ok")
string(JSON driver GET "${status_content}" "driver")
string(JSON check_count GET "${status_content}" "checkCount")
string(JSON slow_query_count GET "${status_content}" "queryMetrics" "slowQueryCount")
string(JSON query_failure_count GET "${status_content}" "queryMetrics" "queryFailureCount")
string(JSON last_error_reason GET "${status_content}" "queryMetrics" "lastErrorReason")
string(JSON last_error_check GET "${status_content}" "queryMetrics" "lastErrorCheck")
string(JSON last_error_sample GET "${status_content}" "queryMetrics" "lastErrorSample")
string(JSON summary_readiness GET "${status_content}" "summary" "readiness")
string(JSON summary_operator_action GET "${status_content}" "summary" "operatorAction")
string(JSON audit_release_gate GET "${status_content}" "auditSummary" "releaseGate")
string(JSON audit_focus0 GET "${status_content}" "auditSummary" "auditFocus" 0)
string(JSON pool_enabled GET "${status_content}" "reconnectPolicy" "poolEnabled")
string(JSON thread_ownership GET "${status_content}" "reconnectPolicy" "threadPolicy" "connectionOwnership")
if(NOT format STREQUAL "qtnetworkchat-database-health-status-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected status format: ${format}")
endif()
if(NOT status STREQUAL "healthy" OR NOT ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Healthy input should produce healthy status")
endif()
if(NOT driver STREQUAL "QPSQL")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected driver QPSQL, got ${driver}")
endif()
if(NOT "${check_count}" STREQUAL "3")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected checkCount=3, got ${check_count}")
endif()
if((NOT slow_query_count EQUAL 1) OR (NOT query_failure_count EQUAL 2) OR (NOT last_error_reason STREQUAL "network")
    OR (NOT last_error_check STREQUAL "postgres-required-tables") OR (NOT last_error_sample MATCHES "connection refused")
    OR (NOT pool_enabled) OR (NOT thread_ownership STREQUAL "thread-affine pooled connections")
    OR (NOT summary_readiness STREQUAL "verified")
    OR (NOT summary_operator_action STREQUAL "Investigate query failures before promoting this database health snapshot.")
    OR (NOT audit_release_gate STREQUAL "review-query-failures")
    OR (NOT audit_focus0 STREQUAL "query-failures"))
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Status JSON should carry database query metrics and reconnect thread policy")
endif()
string(FIND "${status_content}" "super-secret" leaked_secret)
if(NOT leaked_secret EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Status JSON leaked a secret")
endif()
file(READ "${STATUS_MD}" markdown_content)
string(FIND "${markdown_content}" "Database Health" md_title)
string(FIND "${markdown_content}" "required-tables" md_check)
string(FIND "${markdown_content}" "queryFailureCount" md_query_failures)
string(FIND "${markdown_content}" "lastErrorCheck" md_last_error_check)
string(FIND "${markdown_content}" "threadConnectionOwnership" md_thread_policy)
string(FIND "${markdown_content}" "releaseGate" md_release_gate)
if(md_title EQUAL -1 OR md_check EQUAL -1 OR md_query_failures EQUAL -1 OR md_last_error_check EQUAL -1 OR md_thread_policy EQUAL -1 OR md_release_gate EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Markdown status is missing expected content, query metrics, or thread policy")
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
        -FailOnUnhealthy
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Sensitive database health input should fail with -FailOnUnhealthy")
endif()
string(FIND "${bad_output}" "sensitiveHits" sensitive_text)
if(sensitive_text EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Sensitive run should report sensitiveHits")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
