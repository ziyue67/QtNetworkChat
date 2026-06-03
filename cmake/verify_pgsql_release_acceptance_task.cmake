if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/pgsql_release_acceptance_task_sample")
set(OUTPUT_DIR "${TEMP_DIR}/pgsql-output")
set(TASK_DIR "${TEMP_DIR}/task")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -TaskName "QtNetworkChatPgsqlReleaseAcceptancePreview"
        -Schedule Hourly
        -EveryHours 6
        -At "04:45"
        -OutputDir "${OUTPUT_DIR}"
        -TaskDir "${TASK_DIR}"
        -DatabaseHealthDashboardPath "${OUTPUT_DIR}/database-health-dashboard.json"
        -SmokeJsonPath "${OUTPUT_DIR}/pgsql-smoke.json"
        -MigrationJsonPath "${OUTPUT_DIR}/sqlite-pg-migration-plan.json"
        -RollbackPreviewPath "${OUTPUT_DIR}/sqlite-pg-rollback-preview.json"
        -EnsureDatabase
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
    message(FATAL_ERROR "register-pgsql-release-acceptance-task.ps1 preview exited with code ${result}")
endif()

set(LAUNCHER "${TASK_DIR}/run-pgsql-release-acceptance-task.ps1")
set(PREVIEW "${TASK_DIR}/pgsql-release-acceptance-task-preview.json")
if(NOT EXISTS "${LAUNCHER}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected launcher was not created: ${LAUNCHER}")
endif()
if(NOT EXISTS "${PREVIEW}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected preview JSON was not created: ${PREVIEW}")
endif()

file(READ "${LAUNCHER}" launcher_content)
foreach(expected_text
        "check-database-health.ps1"
        "show-database-health-status.ps1"
        "write-database-health-dashboard.ps1"
        "run-pgsql-protocol-smoke.ps1"
        "migrate-sqlite-to-postgres.ps1"
        "write-pgsql-release-acceptance.ps1"
        "package-pgsql-release-evidence.ps1"
        "write-automation-task-history.ps1"
        "-Driver"
        "-HealthPath"
        "-StatusPath"
        "-TestExe"
        "-EnsureDatabase"
        "-MigratorExe"
        "-RollbackPreviewMarkdownPath"
        "-DatabaseHealthDashboardPath"
        "-SmokeJsonPath"
        "-MigrationJsonPath"
        "-RollbackPreviewPath"
        "-JsonPath"
        "-MarkdownPath"
        "-FailOnUnhealthy"
        "healthExitCode"
        "healthStatusExitCode"
        "dashboardExitCode"
        "smokeExitCode"
        "migrationExitCode"
        "acceptanceExitCode"
        "packageExitCode"
        "pipelineExitCode"
        "historyExitCode"
        "evidencePackagePath"
        "evidenceManifestPath"
        "historyPath="
        "historyMarkdownPath="
        "ackPath="
        "pgsql-release-acceptance.json"
        "pgsql-release-acceptance.md"
        "last-run.log")
    string(FIND "${launcher_content}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Launcher missing expected text: ${expected_text}")
    endif()
endforeach()

file(READ "${PREVIEW}" preview_content)
string(JSON format GET "${preview_content}" "format")
string(JSON task_kind GET "${preview_content}" "taskKind")
string(JSON task_name GET "${preview_content}" "taskName")
string(JSON task_display_name GET "${preview_content}" "taskDisplayName")
string(JSON task_summary GET "${preview_content}" "taskSummary")
string(JSON should_register GET "${preview_content}" "register")
string(JSON schedule GET "${preview_content}" "schedule")
string(JSON every_hours GET "${preview_content}" "everyHours")
string(JSON status_artifact_path GET "${preview_content}" "statusArtifactPath")
string(JSON json_path GET "${preview_content}" "jsonPath")
string(JSON markdown_path GET "${preview_content}" "markdownPath")
string(JSON history_script GET "${preview_content}" "historyScript")
string(JSON acceptance_script GET "${preview_content}" "acceptanceScript")
string(JSON package_script GET "${preview_content}" "packageScript")
string(JSON health_script GET "${preview_content}" "healthScript")
string(JSON smoke_script GET "${preview_content}" "smokeScript")
string(JSON migration_script GET "${preview_content}" "migrationScript")
string(JSON health_path GET "${preview_content}" "healthPath")
string(JSON health_status_path GET "${preview_content}" "healthStatusPath")
string(JSON history_path GET "${preview_content}" "historyPath")
string(JSON history_artifact_path GET "${preview_content}" "historyArtifactPath")
string(JSON history_markdown_path GET "${preview_content}" "historyMarkdownPath")
string(JSON ack_path GET "${preview_content}" "ackPath")
string(JSON ack_artifact_path GET "${preview_content}" "ackArtifactPath")
string(JSON last_run_path GET "${preview_content}" "lastRunPath")
string(JSON fail_on_unhealthy GET "${preview_content}" "failOnUnhealthy")
string(JSON read_only GET "${preview_content}" "readOnly")
string(JSON artifact_role_status GET "${preview_content}" "artifactRoles" "status")
string(JSON artifact_role_last_run GET "${preview_content}" "artifactRoles" "lastRun")
string(JSON artifact_role_history GET "${preview_content}" "artifactRoles" "history")
string(JSON artifact_role_ack GET "${preview_content}" "artifactRoles" "ack")
string(JSON artifact_status_path GET "${preview_content}" "artifacts" "status" "path")
string(JSON artifact_last_run_path GET "${preview_content}" "artifacts" "lastRun" "path")
string(JSON artifact_history_path GET "${preview_content}" "artifacts" "history" "path")
string(JSON artifact_ack_path GET "${preview_content}" "artifacts" "ack" "path")
string(JSON artifact_role_evidence GET "${preview_content}" "artifactRoles" "evidence")
string(JSON artifact_evidence_path GET "${preview_content}" "artifacts" "evidence" "path")
string(JSON evidence_package_path GET "${preview_content}" "evidencePackagePath")
string(JSON evidence_manifest_path GET "${preview_content}" "evidenceManifestPath")
string(JSON plan_only GET "${preview_content}" "planOnly")
string(JSON package_evidence GET "${preview_content}" "packageEvidence")
string(JSON password_source GET "${preview_content}" "passwordSource")
string(JSON migration_mode GET "${preview_content}" "migrationMode")

if(NOT format STREQUAL "qtnetworkchat-pgsql-release-acceptance-task-preview-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected preview format: ${format}")
endif()
if(NOT task_kind STREQUAL "pgsql-release-acceptance")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected taskKind in preview: ${task_kind}")
endif()
if(NOT task_name STREQUAL "QtNetworkChatPgsqlReleaseAcceptancePreview")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected taskName in preview: ${task_name}")
endif()
if(NOT task_display_name STREQUAL "PostgreSQL release acceptance")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected taskDisplayName in preview: ${task_display_name}")
endif()
if(NOT task_summary MATCHES "Read-only PostgreSQL release acceptance summary")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected taskSummary in preview: ${task_summary}")
endif()
if(should_register)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview mode should not register the scheduled task")
endif()
if(NOT schedule STREQUAL "Hourly" OR NOT "${every_hours}" STREQUAL "6")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected schedule preview: ${schedule}/${every_hours}")
endif()
if(NOT status_artifact_path STREQUAL json_path OR NOT json_path MATCHES "pgsql-release-acceptance.json" OR NOT markdown_path MATCHES "pgsql-release-acceptance.md")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview JSON/Markdown paths missing expected names")
endif()
if(NOT acceptance_script MATCHES "write-pgsql-release-acceptance.ps1" OR NOT history_script MATCHES "write-automation-task-history.ps1" OR NOT package_script MATCHES "package-pgsql-release-evidence.ps1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview script references missing expected names")
endif()
if(NOT health_script MATCHES "check-database-health.ps1" OR NOT smoke_script MATCHES "run-pgsql-protocol-smoke.ps1" OR NOT migration_script MATCHES "migrate-sqlite-to-postgres.ps1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview orchestration scripts missing expected names")
endif()
if(NOT health_path MATCHES "database-health.json" OR NOT health_status_path MATCHES "database-health-status.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview health/status paths missing expected names")
endif()
if(NOT history_path MATCHES "automation-task-history.json" OR NOT history_markdown_path MATCHES "automation-task-history.md" OR NOT ack_path MATCHES "automation-task-ack.json" OR NOT last_run_path MATCHES "last-run.log")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview history/ack/last-run paths missing expected names")
endif()
if(NOT history_artifact_path STREQUAL history_path OR NOT ack_artifact_path STREQUAL ack_path)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview unified history/ack aliases should match legacy paths")
endif()
if(NOT fail_on_unhealthy OR NOT read_only)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview should be read-only and failOnUnhealthy-enabled")
endif()
if(NOT artifact_role_status STREQUAL "statusArtifactPath" OR NOT artifact_role_last_run STREQUAL "lastRunPath" OR NOT artifact_role_history STREQUAL "historyArtifactPath" OR NOT artifact_role_ack STREQUAL "ackArtifactPath")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview artifactRoles mapping missing expected aliases")
endif()
if(NOT artifact_role_evidence STREQUAL "evidencePackagePath")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview artifactRoles should expose evidence package alias")
endif()
if(NOT artifact_status_path STREQUAL json_path OR NOT artifact_last_run_path STREQUAL last_run_path OR NOT artifact_history_path STREQUAL history_path OR NOT artifact_ack_path STREQUAL ack_path)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview artifacts.*.path entries should match unified aliases")
endif()
if(NOT artifact_evidence_path STREQUAL evidence_package_path OR NOT evidence_package_path MATCHES "pgsql-release-evidence.zip" OR NOT evidence_manifest_path MATCHES "pgsql-release-evidence-manifest.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview evidence package aliases missing expected paths")
endif()
if(plan_only OR NOT package_evidence OR NOT password_source STREQUAL "QTNETWORKCHAT_PGPASSWORD" OR NOT migration_mode STREQUAL "plan")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview should default to live password source, package evidence, and migration plan mode")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${OUTPUT_DIR}/bad"
        -TaskDir "${TEMP_DIR}/bad-task"
        -SmokeJsonPath "password=super-secret"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL release acceptance scheduled task helper should reject sensitive input")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -Schedule Hourly
        -EveryHours 0
        -OutputDir "${OUTPUT_DIR}/bad-hours"
        -TaskDir "${TEMP_DIR}/bad-hours-task"
    RESULT_VARIABLE bad_hours_result
    OUTPUT_VARIABLE bad_hours_output
    ERROR_VARIABLE bad_hours_error
)
if(bad_hours_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL release acceptance scheduled task helper should reject EveryHours=0")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
