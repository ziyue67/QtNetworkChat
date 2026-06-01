if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/database_health_check_plan")
set(QT_ROOT "${TEMP_DIR}/qt")
set(PG_BIN "${TEMP_DIR}/postgres/bin")
set(SQLITE_DIR "${TEMP_DIR}/sqlite")
set(PG_JSON "${TEMP_DIR}/postgres-health-plan.json")
set(SQLITE_JSON "${TEMP_DIR}/sqlite-health-plan.json")
set(PG_BAD_JSON "${TEMP_DIR}/postgres-health-runtime-missing.json")
set(SQLITE_BAD_JSON "${TEMP_DIR}/sqlite-health-path-missing.json")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${QT_ROOT}/plugins/sqldrivers" "${PG_BIN}" "${SQLITE_DIR}")
file(WRITE "${QT_ROOT}/plugins/sqldrivers/qsqlpsql.dll" "fake qpsql plugin")
file(WRITE "${PG_BIN}/psql.exe" "fake psql")
file(WRITE "${PG_BIN}/libpq.dll" "fake libpq")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -Driver postgres
        -QtRoot "${QT_ROOT}"
        -PostgresBinDir "${PG_BIN}"
        -PostgresPassword "not-used-in-plan"
        -PlanOnly
        -JsonPath "${PG_JSON}"
    RESULT_VARIABLE pg_result
    OUTPUT_VARIABLE pg_output
    ERROR_VARIABLE pg_error
)
if(NOT pg_output STREQUAL "")
    message(STATUS "${pg_output}")
endif()
if(NOT pg_error STREQUAL "")
    message(STATUS "${pg_error}")
endif()
if(NOT pg_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL database health plan failed with ${pg_result}")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -Driver sqlite
        -SQLitePath "${SQLITE_DIR}/accounts.sqlite3"
        -PlanOnly
        -JsonPath "${SQLITE_JSON}"
    RESULT_VARIABLE sqlite_result
    OUTPUT_VARIABLE sqlite_output
    ERROR_VARIABLE sqlite_error
)
if(NOT sqlite_output STREQUAL "")
    message(STATUS "${sqlite_output}")
endif()
if(NOT sqlite_error STREQUAL "")
    message(STATUS "${sqlite_error}")
endif()
if(NOT sqlite_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "SQLite database health plan failed with ${sqlite_result}")
endif()

foreach(json_path IN ITEMS "${PG_JSON}" "${SQLITE_JSON}")
    if(NOT EXISTS "${json_path}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Database health JSON was not created: ${json_path}")
    endif()
    file(READ "${json_path}" json_content)
    string(JSON format GET "${json_content}" "format")
    string(JSON plan_only GET "${json_content}" "planOnly")
    string(JSON ok GET "${json_content}" "ok")
    if(NOT format STREQUAL "qtnetworkchat-database-health-check-v1")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Unexpected database health format: ${format}")
    endif()
    if(NOT plan_only)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "PlanOnly should be true for ${json_path}")
    endif()
    if(NOT ok)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "PlanOnly database health should be ok for ${json_path}")
    endif()
    string(FIND "${json_content}" "not-used-in-plan" leaked_password)
    if(NOT leaked_password EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Database health JSON leaked the plan password")
    endif()
endforeach()

file(READ "${PG_JSON}" pg_json)
string(JSON pg_driver GET "${pg_json}" "environment" "QTNETWORKCHAT_DB_DRIVER")
string(JSON pg_password GET "${pg_json}" "environment" "QTNETWORKCHAT_PGPASSWORD")
string(JSON pg_pool_env GET "${pg_json}" "environment" "QTNETWORKCHAT_DB_POOL")
string(JSON pg_pool_enabled GET "${pg_json}" "reconnectPolicy" "poolEnabled")
string(JSON pg_backoff_ms GET "${pg_json}" "reconnectPolicy" "backoffMs")
string(JSON pg_reason_network GET "${pg_json}" "reconnectPolicy" "reasonBuckets" 3)
if(NOT pg_driver STREQUAL "QPSQL")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL database health should expose QPSQL environment")
endif()
if(NOT pg_password STREQUAL "<redacted>")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL database health should redact password")
endif()
if((NOT "${pg_pool_env}" STREQUAL "1") OR (NOT pg_pool_enabled) OR (NOT pg_backoff_ms EQUAL 2000))
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL database health should expose pool/backoff policy")
endif()
if(NOT "${pg_reason_network}" STREQUAL "network")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL database health should expose fixed reconnect reason buckets")
endif()
string(JSON pg_plugin_reason GET "${pg_json}" "checks" 0 "reason")
string(JSON pg_psql_reason GET "${pg_json}" "checks" 1 "reason")
string(JSON pg_libpq_reason GET "${pg_json}" "checks" 2 "reason")
if((NOT "${pg_plugin_reason}" STREQUAL "ok") OR (NOT "${pg_psql_reason}" STREQUAL "ok") OR (NOT "${pg_libpq_reason}" STREQUAL "ok"))
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL healthy plan checks should expose reason=ok")
endif()

file(READ "${SQLITE_JSON}" sqlite_json)
string(JSON sqlite_driver GET "${sqlite_json}" "environment" "QTNETWORKCHAT_DB_DRIVER")
string(JSON sqlite_pool_env GET "${sqlite_json}" "environment" "QTNETWORKCHAT_DB_POOL")
string(JSON sqlite_pool_enabled GET "${sqlite_json}" "reconnectPolicy" "poolEnabled")
if(NOT sqlite_driver STREQUAL "QSQLITE")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "SQLite database health should expose QSQLITE environment")
endif()
if((NOT "${sqlite_pool_env}" STREQUAL "0") OR sqlite_pool_enabled)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "SQLite database health should keep pool disabled")
endif()
string(JSON sqlite_parent_reason GET "${sqlite_json}" "checks" 0 "reason")
if(NOT "${sqlite_parent_reason}" STREQUAL "ok")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "SQLite healthy plan check should expose reason=ok")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -Driver postgres
        -QtRoot "${TEMP_DIR}/missing-qt"
        -PostgresBinDir "${TEMP_DIR}/missing-pg-bin"
        -PlanOnly
        -JsonPath "${PG_BAD_JSON}"
    RESULT_VARIABLE pg_bad_result
    OUTPUT_VARIABLE pg_bad_output
    ERROR_VARIABLE pg_bad_error
)
if(NOT pg_bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL missing runtime plan should not fail without -FailOnUnhealthy")
endif()
file(READ "${PG_BAD_JSON}" pg_bad_json)
string(JSON pg_bad_ok GET "${pg_bad_json}" "ok")
string(JSON pg_bad_status GET "${pg_bad_json}" "status")
string(JSON pg_bad_reason0 GET "${pg_bad_json}" "checks" 0 "reason")
string(JSON pg_bad_reason1 GET "${pg_bad_json}" "checks" 1 "reason")
string(JSON pg_bad_reason2 GET "${pg_bad_json}" "checks" 2 "reason")
if(pg_bad_ok OR (NOT "${pg_bad_status}" STREQUAL "unhealthy"))
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL missing runtime plan should be unhealthy")
endif()
if((NOT "${pg_bad_reason0}" STREQUAL "runtime") OR (NOT "${pg_bad_reason1}" STREQUAL "runtime") OR (NOT "${pg_bad_reason2}" STREQUAL "runtime"))
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL missing runtime checks should expose reason=runtime")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -Driver sqlite
        -SQLitePath "${TEMP_DIR}/missing-sqlite-parent/accounts.sqlite3"
        -PlanOnly
        -JsonPath "${SQLITE_BAD_JSON}"
    RESULT_VARIABLE sqlite_bad_result
    OUTPUT_VARIABLE sqlite_bad_output
    ERROR_VARIABLE sqlite_bad_error
)
if(NOT sqlite_bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "SQLite missing path plan should not fail without -FailOnUnhealthy")
endif()
file(READ "${SQLITE_BAD_JSON}" sqlite_bad_json)
string(JSON sqlite_bad_ok GET "${sqlite_bad_json}" "ok")
string(JSON sqlite_bad_status GET "${sqlite_bad_json}" "status")
string(JSON sqlite_bad_reason GET "${sqlite_bad_json}" "checks" 0 "reason")
if(sqlite_bad_ok OR (NOT "${sqlite_bad_status}" STREQUAL "unhealthy") OR (NOT "${sqlite_bad_reason}" STREQUAL "path"))
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "SQLite missing parent should be unhealthy with reason=path")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
