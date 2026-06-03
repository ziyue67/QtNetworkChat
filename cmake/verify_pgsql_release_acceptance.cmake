if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/pgsql_release_acceptance")
set(DASHBOARD_PATH "${TEMP_DIR}/database-health-dashboard.json")
set(SMOKE_JSON_PATH "${TEMP_DIR}/pgsql-smoke.json")
set(MIGRATION_JSON_PATH "${TEMP_DIR}/migration.json")
set(ROLLBACK_PREVIEW_PATH "${TEMP_DIR}/rollback-preview.json")
set(OUTPUT_JSON_PATH "${TEMP_DIR}/pgsql-release-acceptance.json")
set(OUTPUT_MD_PATH "${TEMP_DIR}/pgsql-release-acceptance.md")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

file(WRITE "${DASHBOARD_PATH}" [=[
{
  "status":"healthy",
  "ok":true,
  "queryMetrics":{"slowQueryCount":1,"queryFailureCount":1},
  "summary":{"readiness":"verified","operatorAction":"Investigate query failures before promoting this database health snapshot."},
  "auditSummary":{"releaseGate":"review-query-failures","auditFocus":["query-failures","slow-queries"],"evidenceBundle":["dashboard-json","dashboard-markdown","query-metrics"]}
}
]=])

file(WRITE "${SMOKE_JSON_PATH}" [=[
{
  "ok":true,
  "summary":{"readiness":"verified","operatorAction":"Archive the evidence bundle and use it for PostgreSQL release readiness review."},
  "auditSummary":{"releaseGate":"can-review-smoke-evidence","bootstrapRequired":true,"auditFocus":["offline-attachment-recovery"],"evidenceBundle":["json","markdown","boundary-scenarios"]},
  "recoverySummary":{"retryOrResumeCount":3,"cleanupProofCount":6,"releaseHint":"Use real smoke evidence to confirm retry, cleanup, and restart recovery paths."}
}
]=])

file(WRITE "${MIGRATION_JSON_PATH}" [=[
{
  "ok":true,
  "diffSummary":{"driftTableCount":0},
  "reportSummary":{"executionReadiness":"ready","operatorAction":"Can proceed after a routine backup and smoke verification."},
  "auditSummary":{"releaseGate":"can-cutover-after-smoke","writeIntent":"dry-run-plan","backupRequired":false,"rollbackPreviewAvailable":true,"auditFocus":["migration-diff"],"evidenceBundle":["json","markdown","html","rollback-preview-json","rollback-preview-markdown"]}
}
]=])

file(WRITE "${ROLLBACK_PREVIEW_PATH}" [=[
{
  "riskLevel":"review",
  "summary":{"tablesWithDeletes":5,"fallbackKeyReviewTableCount":5}
}
]=])

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -DatabaseHealthDashboardPath "${DASHBOARD_PATH}"
        -SmokeJsonPath "${SMOKE_JSON_PATH}"
        -MigrationJsonPath "${MIGRATION_JSON_PATH}"
        -RollbackPreviewPath "${ROLLBACK_PREVIEW_PATH}"
        -JsonPath "${OUTPUT_JSON_PATH}"
        -MarkdownPath "${OUTPUT_MD_PATH}"
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
    message(FATAL_ERROR "write-pgsql-release-acceptance.ps1 exited with code ${result}")
endif()
if(NOT EXISTS "${OUTPUT_JSON_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL release acceptance JSON was not created")
endif()
if(NOT EXISTS "${OUTPUT_MD_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL release acceptance Markdown was not created")
endif()

file(READ "${OUTPUT_JSON_PATH}" json_content)
file(READ "${OUTPUT_MD_PATH}" markdown_content)
string(JSON format GET "${json_content}" "format")
string(JSON status GET "${json_content}" "status")
string(JSON ok GET "${json_content}" "ok")
string(JSON readiness GET "${json_content}" "summary" "readiness")
string(JSON operator_action GET "${json_content}" "summary" "operatorAction")
string(JSON release_gate GET "${json_content}" "auditSummary" "releaseGate")
string(JSON release_detail0 GET "${json_content}" "releaseDetails" 0)
string(JSON release_detail5 GET "${json_content}" "releaseDetails" 5)
string(JSON metric_retry GET "${json_content}" "metrics" "smokeRetryOrResumeCount")
string(JSON metric_cleanup GET "${json_content}" "metrics" "smokeCleanupProofCount")
string(JSON metric_slow GET "${json_content}" "metrics" "slowQueryCount")
string(JSON metric_query_failure GET "${json_content}" "metrics" "queryFailureCount")
string(JSON metric_rollback_review GET "${json_content}" "metrics" "rollbackPreviewFallbackReviewTables")
string(JSON evidence0 GET "${json_content}" "auditSummary" "evidenceBundle" 0)
string(JSON evidence4 GET "${json_content}" "auditSummary" "evidenceBundle" 4)
string(FIND "${markdown_content}" "QtNetworkChat PostgreSQL Release Acceptance" has_title)
string(FIND "${markdown_content}" "Release gate: `review-query-failures`" has_gate)
string(FIND "${markdown_content}" "Release details: `bootstrapRequired=true; smokeReleaseHint=Use real smoke evidence to confirm retry, cleanup, and restart recovery paths.; writeIntent=dry-run-plan; backupRequired=false; rollbackPreview=true; rollbackRisk=review; slowQueries=1; queryFailures=1; evidence=dashboard-json, dashboard-markdown, query-metrics, json, markdown, boundary-scenarios, html, rollback-preview-json, rollback-preview-markdown`" has_details)
string(FIND "${markdown_content}" "| smokeRetryOrResumeCount | 3 |" has_retry_metric)
string(FIND "${markdown_content}" "| rollbackPreviewFallbackReviewTables | 5 |" has_rollback_metric)

if(NOT format STREQUAL "qtnetworkchat-pgsql-release-acceptance-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected PostgreSQL release acceptance format: ${format}")
endif()
if(NOT status STREQUAL "review" OR ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL release acceptance should be review / ok=false for rollback review inputs")
endif()
if(NOT readiness STREQUAL "review")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL release acceptance readiness should be review")
endif()
if(NOT operator_action STREQUAL "Investigate PostgreSQL query failures before promoting PostgreSQL release acceptance.")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected PostgreSQL release acceptance operator action")
endif()
if(NOT release_gate STREQUAL "review-query-failures")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected PostgreSQL release acceptance release gate")
endif()
if(NOT release_detail0 STREQUAL "bootstrapRequired=true")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release details should expose bootstrapRequired")
endif()
if(NOT release_detail5 STREQUAL "rollbackRisk=review")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release details should expose rollback risk")
endif()
if(NOT metric_retry EQUAL 3 OR NOT metric_cleanup EQUAL 6 OR NOT metric_rollback_review EQUAL 5 OR NOT metric_slow EQUAL 1 OR NOT metric_query_failure EQUAL 1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release acceptance metrics mismatch")
endif()
if(NOT evidence0 STREQUAL "dashboard-json" OR NOT evidence4 STREQUAL "markdown")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release acceptance evidence bundle should merge and preserve order")
endif()
if(has_title EQUAL -1 OR has_gate EQUAL -1 OR has_details EQUAL -1 OR has_retry_metric EQUAL -1 OR has_rollback_metric EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release acceptance markdown missing title, gate, details, or metrics")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
