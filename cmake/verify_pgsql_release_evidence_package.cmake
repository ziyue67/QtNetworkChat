if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/pgsql_release_evidence_package")
set(OUTPUT_DIR "${TEMP_DIR}/out")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(HEALTH "${TEMP_DIR}/database-health.json")
set(STATUS "${TEMP_DIR}/database-health-status.json")
set(DASHBOARD "${TEMP_DIR}/database-health-dashboard.json")
set(SMOKE "${TEMP_DIR}/pgsql-smoke.json")
set(MIGRATION "${TEMP_DIR}/migration.json")
set(ROLLBACK "${TEMP_DIR}/rollback-preview.json")
set(ACCEPTANCE "${TEMP_DIR}/pgsql-release-acceptance.json")
set(LAST_RUN "${TEMP_DIR}/last-run.log")
set(HISTORY "${TEMP_DIR}/automation-task-history.json")

file(WRITE "${HEALTH}" "{\"ok\":true,\"postgresPassword\":\"<redacted>\"}\n")
file(WRITE "${STATUS}" "{\"ok\":true,\"status\":\"healthy\"}\n")
file(WRITE "${DASHBOARD}" "{\"ok\":true,\"status\":\"healthy\"}\n")
file(WRITE "${SMOKE}" "{\"ok\":true,\"summary\":{\"readiness\":\"verified\"}}\n")
file(WRITE "${MIGRATION}" "{\"ok\":true,\"reportSummary\":{\"executionReadiness\":\"ready\"}}\n")
file(WRITE "${ROLLBACK}" "{\"ok\":true,\"riskLevel\":\"review\"}\n")
file(WRITE "${ACCEPTANCE}" "{\"ok\":false,\"status\":\"review\"}\n")
file(WRITE "${LAST_RUN}" "2026-06-03T00:00:00Z exitCode=0\n")
file(WRITE "${HISTORY}" "{\"runCount\":1,\"failedRunCount\":0}\n")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}"
        -DatabaseHealthPath "${HEALTH}"
        -DatabaseHealthStatusPath "${STATUS}"
        -DatabaseHealthDashboardPath "${DASHBOARD}"
        -SmokeJsonPath "${SMOKE}"
        -MigrationJsonPath "${MIGRATION}"
        -RollbackPreviewPath "${ROLLBACK}"
        -AcceptanceJsonPath "${ACCEPTANCE}"
        -LastRunPath "${LAST_RUN}"
        -HistoryPath "${HISTORY}"
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
    message(FATAL_ERROR "package-pgsql-release-evidence.ps1 exited with code ${result}")
endif()

set(PACKAGE_PATH "${OUTPUT_DIR}/pgsql-release-evidence.zip")
set(MANIFEST_PATH "${OUTPUT_DIR}/pgsql-release-evidence-manifest.json")
if(NOT EXISTS "${PACKAGE_PATH}" OR NOT EXISTS "${MANIFEST_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected PostgreSQL release evidence package and manifest were not created")
endif()

file(READ "${MANIFEST_PATH}" manifest_content)
string(JSON format GET "${manifest_content}" "format")
string(JSON ok GET "${manifest_content}" "ok")
string(JSON input_count GET "${manifest_content}" "inputCount")
string(JSON input0_kind GET "${manifest_content}" "inputs" 0 "kind")
if(NOT format STREQUAL "qtnetworkchat-pgsql-release-evidence-package-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected PostgreSQL evidence manifest format: ${format}")
endif()
if(NOT ok OR NOT input_count EQUAL 9 OR NOT input0_kind STREQUAL "database-health")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected PostgreSQL evidence manifest summary")
endif()

set(BAD "${TEMP_DIR}/bad.json")
file(WRITE "${BAD}" "{\"password\":\"plain-secret\"}\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/bad"
        -AcceptanceJsonPath "${BAD}"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL release evidence packager should reject sensitive inputs")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
