if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()
if(NOT DEFINED MIGRATOR_EXE OR NOT EXISTS "${MIGRATOR_EXE}")
    message(FATAL_ERROR "MIGRATOR_EXE does not exist: ${MIGRATOR_EXE}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/sqlite_pg_migration_plan")
if(NOT DEFINED QT_ROOT OR NOT EXISTS "${QT_ROOT}")
    message(FATAL_ERROR "QT_ROOT is required: ${QT_ROOT}")
endif()
set(PG_BIN "${TEMP_DIR}/postgres/bin")
set(SQLITE_PATH "${TEMP_DIR}/accounts.sqlite3")
set(JSON_PATH "${TEMP_DIR}/migration-plan.json")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${PG_BIN}")
file(WRITE "${PG_BIN}/libpq.dll" "fake libpq")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -MigratorExe "${MIGRATOR_EXE}"
        -SQLitePath "${SQLITE_PATH}"
        -QtRoot "${QT_ROOT}"
        -PostgresBinDir "${PG_BIN}"
        -Mode plan
        -CreateSample
        -PostgresPassword "not-used-in-plan"
        -JsonPath "${JSON_PATH}"
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
    message(FATAL_ERROR "migrate-sqlite-to-postgres.ps1 exited with code ${result}")
endif()
if(NOT EXISTS "${JSON_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Migration plan JSON was not created")
endif()

file(READ "${JSON_PATH}" json_content)
string(JSON format GET "${json_content}" "format")
string(JSON mode GET "${json_content}" "mode")
string(JSON ok GET "${json_content}" "ok")
string(JSON password GET "${json_content}" "postgresPassword")
string(JSON first_table GET "${json_content}" "tables" 0 "name")
string(JSON first_rows GET "${json_content}" "tables" 0 "rows")
string(JSON first_diff_status GET "${json_content}" "tables" 0 "diffStatus")
string(JSON first_missing_rows GET "${json_content}" "tables" 0 "missingRows")
string(JSON first_rolled_back_rows GET "${json_content}" "tables" 0 "rolledBackRows")
if(NOT format STREQUAL "qtnetworkchat-sqlite-pg-migration-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected migration format: ${format}")
endif()
if(NOT mode STREQUAL "plan")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Migration mode should be plan")
endif()
if(NOT ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Migration plan should be ok")
endif()
if(NOT password STREQUAL "<redacted>")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Migration plan leaked PostgreSQL password")
endif()
if(NOT first_table STREQUAL "accounts")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "First migration table should be accounts")
endif()
if(NOT first_rows EQUAL 1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Sample accounts row count should be 1")
endif()
if((NOT "${first_diff_status}" STREQUAL "clean") OR (NOT first_missing_rows EQUAL 0) OR (NOT first_rolled_back_rows EQUAL 0))
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Migration plan should include clean diff/rollback counters")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
