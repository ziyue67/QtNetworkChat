if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/pgsql_rollback_live_evidence")
set(FAKE_BUILD_DIR "${TEMP_DIR}/build")
set(FAKE_QT_ROOT "${TEMP_DIR}/qt")
set(FAKE_PG_BIN "${TEMP_DIR}/postgres/bin")
set(JSON_PATH "${TEMP_DIR}/rollback-live-plan.json")
set(MARKDOWN_PATH "${TEMP_DIR}/rollback-live-plan.md")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY
    "${FAKE_BUILD_DIR}"
    "${FAKE_QT_ROOT}/bin"
    "${FAKE_QT_ROOT}/plugins/sqldrivers"
    "${FAKE_PG_BIN}"
)
file(WRITE "${FAKE_BUILD_DIR}/sqlite_to_postgres_migrator.exe" "fake migrator")
file(WRITE "${FAKE_BUILD_DIR}/postgres_qpsql_protocol_smoke_test.exe" "fake qpsql smoke test")
file(WRITE "${FAKE_QT_ROOT}/bin/Qt6Core.dll" "fake Qt runtime")
file(WRITE "${FAKE_QT_ROOT}/plugins/sqldrivers/qsqlite.dll" "fake sqlite plugin")
file(WRITE "${FAKE_QT_ROOT}/plugins/sqldrivers/qsqlpsql.dll" "fake qpsql plugin")
file(WRITE "${FAKE_PG_BIN}/libpq.dll" "fake libpq")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${TEMP_DIR}/out"
        -QtRoot "${FAKE_QT_ROOT}"
        -PostgresBinDir "${FAKE_PG_BIN}"
        -MigratorExe "${FAKE_BUILD_DIR}/sqlite_to_postgres_migrator.exe"
        -TestExe "${FAKE_BUILD_DIR}/postgres_qpsql_protocol_smoke_test.exe"
        -PlanOnly
        -JsonPath "${JSON_PATH}"
        -MarkdownPath "${MARKDOWN_PATH}"
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
    message(FATAL_ERROR "run-pgsql-rollback-live-evidence.ps1 exited with code ${result}")
endif()
if(NOT EXISTS "${JSON_PATH}" OR NOT EXISTS "${MARKDOWN_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Rollback live evidence plan artifacts were not created")
endif()

file(READ "${JSON_PATH}" json_content)
file(READ "${MARKDOWN_PATH}" markdown_content)
string(JSON format GET "${json_content}" "format")
string(JSON plan_only GET "${json_content}" "planOnly")
string(JSON ok GET "${json_content}" "ok")
string(JSON status GET "${json_content}" "status")
string(JSON readiness GET "${json_content}" "summary" "readiness")
string(JSON release_gate GET "${json_content}" "summary" "releaseGate")
string(JSON before_preview GET "${json_content}" "rollbackLive" "beforePreviewAvailable")
string(JSON rollback_applied GET "${json_content}" "rollbackLive" "rollbackExecutionApplied")
string(JSON after_audit GET "${json_content}" "rollbackLive" "afterAuditAvailable")
string(JSON acceptance_available GET "${json_content}" "rollbackLive" "releaseAcceptanceAvailable")
string(JSON evidence_available GET "${json_content}" "rollbackLive" "evidencePackageAvailable")
string(JSON password GET "${json_content}" "environment" "QTNETWORKCHAT_PGPASSWORD")
string(JSON driver GET "${json_content}" "environment" "QTNETWORKCHAT_DB_DRIVER")
string(JSON rollback_audit_path GET "${json_content}" "artifacts" "rollbackAuditPath")
string(JSON evidence_package_path GET "${json_content}" "artifacts" "evidencePackagePath")
string(FIND "${markdown_content}" "PostgreSQL Rollback Live Evidence" has_title)
string(FIND "${markdown_content}" "PostgreSQL password: <redacted>" has_redacted)
string(FIND "${markdown_content}" "Release gate: await-live-rollback-evidence" has_gate)
string(FIND "${markdown_content}" "Rollback execution applied: False" has_plan_applied)
if(NOT format STREQUAL "qtnetworkchat-pgsql-rollback-live-evidence-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected rollback live evidence format: ${format}")
endif()
if(NOT plan_only OR NOT ok OR NOT status STREQUAL "plan-ready" OR NOT readiness STREQUAL "ready")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Rollback live evidence plan should be ready and ok")
endif()
if(NOT release_gate STREQUAL "await-live-rollback-evidence")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Rollback live evidence plan should await live evidence")
endif()
if(NOT before_preview OR rollback_applied OR NOT after_audit OR NOT acceptance_available OR NOT evidence_available)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Rollback live evidence plan should declare before/audit/acceptance/evidence surfaces")
endif()
if(NOT password STREQUAL "<redacted>" OR NOT driver STREQUAL "QPSQL")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Rollback live evidence plan should redact PostgreSQL password and use QPSQL")
endif()
if(NOT rollback_audit_path MATCHES "sqlite-pg-rollback-audit-live.json" OR NOT evidence_package_path MATCHES "pgsql-rollback-live-evidence.zip")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Rollback live evidence plan should expose rollback audit and evidence package paths")
endif()
if(has_title EQUAL -1 OR has_redacted EQUAL -1 OR has_gate EQUAL -1 OR has_plan_applied EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Rollback live evidence Markdown should include title, gate, plan execution state, and redaction")
endif()
string(FIND "${json_content}" "not-used-in-plan" leaked_secret)
if(NOT leaked_secret EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Rollback live evidence plan leaked a secret")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
