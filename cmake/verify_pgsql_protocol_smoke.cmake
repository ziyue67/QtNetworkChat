if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/pgsql_protocol_smoke_plan")
set(FAKE_BUILD_DIR "${TEMP_DIR}/build")
set(FAKE_QT_ROOT "${TEMP_DIR}/qt")
set(FAKE_PG_BIN "${TEMP_DIR}/postgres/bin")
set(JSON_PATH "${TEMP_DIR}/pgsql-smoke-plan.json")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY
    "${FAKE_BUILD_DIR}"
    "${FAKE_QT_ROOT}/bin"
    "${FAKE_QT_ROOT}/plugins/sqldrivers"
    "${FAKE_PG_BIN}"
)
file(WRITE "${FAKE_BUILD_DIR}/postgres_qpsql_protocol_smoke_test.exe" "fake qpsql smoke test")
file(WRITE "${FAKE_QT_ROOT}/bin/Qt6Core.dll" "fake Qt runtime")
file(WRITE "${FAKE_QT_ROOT}/plugins/sqldrivers/qsqlpsql.dll" "fake qpsql plugin")
file(WRITE "${FAKE_PG_BIN}/libpq.dll" "fake libpq")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -TestExe "${FAKE_BUILD_DIR}/postgres_qpsql_protocol_smoke_test.exe"
        -QtRoot "${FAKE_QT_ROOT}"
        -PostgresBinDir "${FAKE_PG_BIN}"
        -PostgresPassword "not-used-in-plan"
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
    message(FATAL_ERROR "run-pgsql-protocol-smoke.ps1 exited with code ${result}")
endif()
if(NOT EXISTS "${JSON_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan JSON was not created")
endif()

file(READ "${JSON_PATH}" json_content)
string(JSON format GET "${json_content}" "format")
string(JSON plan_only GET "${json_content}" "planOnly")
string(JSON ok GET "${json_content}" "ok")
string(JSON password GET "${json_content}" "environment" "QTNETWORKCHAT_PGPASSWORD")
string(JSON driver GET "${json_content}" "environment" "QTNETWORKCHAT_DB_DRIVER")
string(JSON coverage0 GET "${json_content}" "coverageSurfaces" 0)
string(JSON coverage4 GET "${json_content}" "coverageSurfaces" 4)
if(NOT format STREQUAL "qtnetworkchat-pgsql-protocol-smoke-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected PostgreSQL smoke format: ${format}")
endif()
if(NOT plan_only)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PlanOnly should be true")
endif()
if(NOT ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan should pass with fake runtime files")
endif()
if(NOT password STREQUAL "<redacted>")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan leaked a password")
endif()
if(NOT driver STREQUAL "QPSQL")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan should use QPSQL")
endif()
if(NOT coverage0 STREQUAL "register-login")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan should expose register-login coverage")
endif()
if(NOT coverage4 STREQUAL "public-group-member-role-audit")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan should expose public group member role audit coverage")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
