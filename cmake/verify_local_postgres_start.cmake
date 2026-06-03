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
string(JSON summary_readiness GET "${json_content}" "summary" "readiness")
string(JSON summary_failed_checks GET "${json_content}" "summary" "failedCheckCount")
string(JSON summary_operator_action GET "${json_content}" "summary" "operatorAction")
string(JSON audit_release_gate GET "${json_content}" "auditSummary" "releaseGate")
string(JSON audit_bootstrap_mode GET "${json_content}" "auditSummary" "bootstrapMode")
string(JSON audit_requires_password GET "${json_content}" "auditSummary" "requiresPassword")
string(JSON audit_evidence0 GET "${json_content}" "auditSummary" "evidenceBundle" 0)
string(JSON audit_evidence1 GET "${json_content}" "auditSummary" "evidenceBundle" 1)
string(JSON audit_focus0 GET "${json_content}" "auditSummary" "auditFocus" 0)
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
if(NOT summary_readiness STREQUAL "ready" OR NOT summary_failed_checks EQUAL 0
    OR NOT summary_operator_action STREQUAL "Local PostgreSQL bootstrap prerequisites look ready; next run can start or reuse the target instance.")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Local PostgreSQL plan should expose ready bootstrap summary")
endif()
if(NOT audit_release_gate STREQUAL "await-local-bootstrap-run" OR NOT audit_bootstrap_mode STREQUAL "plan"
    OR audit_requires_password OR NOT audit_evidence0 STREQUAL "json" OR NOT audit_evidence1 STREQUAL "bootstrap-checks"
    OR NOT audit_focus0 STREQUAL "bootstrap-prerequisites")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Local PostgreSQL plan should expose audit release gate, bootstrap mode, and evidence bundle")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
