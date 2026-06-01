if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/local_postgres_start_plan")
set(FAKE_PG_BIN "${TEMP_DIR}/postgres/bin")
set(JSON_PATH "${TEMP_DIR}/local-postgres-plan.json")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${FAKE_PG_BIN}")
foreach(tool initdb pg_ctl pg_isready createdb psql)
    file(WRITE "${FAKE_PG_BIN}/${tool}.exe" "fake ${tool}")
endforeach()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -PostgresBinDir "${FAKE_PG_BIN}"
        -DataDir "${TEMP_DIR}/pgdata"
        -LogPath "${TEMP_DIR}/pg.log"
        -Password "not-used-in-plan"
        -PlanOnly
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
    message(FATAL_ERROR "start-local-postgres.ps1 exited with code ${result}")
endif()
if(NOT EXISTS "${JSON_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Local PostgreSQL plan JSON was not created")
endif()

file(READ "${JSON_PATH}" json_content)
string(JSON format GET "${json_content}" "format")
string(JSON plan_only GET "${json_content}" "planOnly")
string(JSON ok GET "${json_content}" "ok")
string(JSON password GET "${json_content}" "password")
string(JSON database GET "${json_content}" "database")
if(NOT format STREQUAL "qtnetworkchat-local-postgres-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected local PostgreSQL format: ${format}")
endif()
if(NOT plan_only)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PlanOnly should be true")
endif()
if(NOT ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Local PostgreSQL plan should pass with fake runtime files")
endif()
if(NOT password STREQUAL "<redacted>")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Local PostgreSQL plan leaked a password")
endif()
if(NOT database STREQUAL "qtnetworkchat")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Local PostgreSQL plan should default to qtnetworkchat")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
