if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/pgsql_release_acceptance")
set(DASHBOARD_PATH "${TEMP_DIR}/database-health-dashboard.json")
set(SMOKE_JSON_PATH "${TEMP_DIR}/pgsql-smoke.json")
set(MIGRATION_JSON_PATH "${TEMP_DIR}/migration.json")
set(ROLLBACK_PREVIEW_PATH "${TEMP_DIR}/rollback-preview.json")
set(ROLLBACK_AUDIT_PATH "${TEMP_DIR}/rollback-audit.json")
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

file(WRITE "${ROLLBACK_AUDIT_PATH}" [=[
{
  "executionApplied":false,
  "before":{"totalWouldDeleteRows":5,"riskLevel":"review"},
  "after":{"actualRolledBackRows":0},
  "auditSummary":{"releaseGate":"await-rollback-execute","countMatchesPreview":false,"auditFocus":["rollback-before-preview","rollback-not-executed"],"evidenceBundle":["rollback-audit-json","rollback-preview-json"]}
}
]=])

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -DatabaseHealthDashboardPath "${DASHBOARD_PATH}"
        -SmokeJsonPath "${SMOKE_JSON_PATH}"
        -MigrationJsonPath "${MIGRATION_JSON_PATH}"
        -RollbackPreviewPath "${ROLLBACK_PREVIEW_PATH}"
        -RollbackAuditPath "${ROLLBACK_AUDIT_PATH}"
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
string(JSON plan_only GET "${json_content}" "planOnly")
string(JSON readiness GET "${json_content}" "summary" "readiness")
string(JSON operator_action GET "${json_content}" "summary" "operatorAction")
string(JSON release_gate GET "${json_content}" "auditSummary" "releaseGate")
string(JSON release_detail0 GET "${json_content}" "releaseDetails" 0)
string(JSON release_detail5 GET "${json_content}" "releaseDetails" 5)
string(JSON release_detail6 GET "${json_content}" "releaseDetails" 6)
string(JSON release_detail7 GET "${json_content}" "releaseDetails" 7)
string(JSON metric_retry GET "${json_content}" "metrics" "smokeRetryOrResumeCount")
string(JSON metric_cleanup GET "${json_content}" "metrics" "smokeCleanupProofCount")
string(JSON metric_slow GET "${json_content}" "metrics" "slowQueryCount")
string(JSON metric_query_failure GET "${json_content}" "metrics" "queryFailureCount")
string(JSON metric_rollback_review GET "${json_content}" "metrics" "rollbackPreviewFallbackReviewTables")
string(JSON metric_rollback_audit_before GET "${json_content}" "metrics" "rollbackAuditBeforeRows")
string(JSON metric_rollback_audit_after GET "${json_content}" "metrics" "rollbackAuditAfterRows")
string(JSON metric_rollback_audit_match GET "${json_content}" "metrics" "rollbackAuditCountMatchesPreview")
string(JSON evidence0 GET "${json_content}" "auditSummary" "evidenceBundle" 0)
string(JSON evidence8 GET "${json_content}" "auditSummary" "evidenceBundle" 8)
string(JSON evidence9 GET "${json_content}" "auditSummary" "evidenceBundle" 9)
string(FIND "${markdown_content}" "QtNetworkChat PostgreSQL Release Acceptance" has_title)
string(FIND "${markdown_content}" "Release gate: `review-query-failures`" has_gate)
string(FIND "${markdown_content}" "rollbackAuditGate=await-rollback-execute" has_audit_gate_detail)
string(FIND "${markdown_content}" "| smokeRetryOrResumeCount | 3 |" has_retry_metric)
string(FIND "${markdown_content}" "| rollbackPreviewFallbackReviewTables | 5 |" has_rollback_metric)
string(FIND "${markdown_content}" "| rollbackAuditBeforeRows | 5 |" has_rollback_audit_before_metric)
string(FIND "${markdown_content}" "| rollbackAuditAfterRows | 0 |" has_rollback_audit_after_metric)

if(NOT format STREQUAL "qtnetworkchat-pgsql-release-acceptance-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected PostgreSQL release acceptance format: ${format}")
endif()
if(NOT status STREQUAL "review" OR ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL release acceptance should be review / ok=false for rollback review inputs")
endif()
if(plan_only)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Fixture with real health/smoke/migration inputs should not be plan-only")
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
if(NOT release_detail6 STREQUAL "rollbackAuditGate=await-rollback-execute" OR NOT release_detail7 STREQUAL "rollbackExecuted=false")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release details should expose rollback audit gate and execution flag")
endif()
if(NOT metric_retry EQUAL 3 OR NOT metric_cleanup EQUAL 6 OR NOT metric_rollback_review EQUAL 5 OR NOT metric_slow EQUAL 1 OR NOT metric_query_failure EQUAL 1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release acceptance metrics mismatch")
endif()
if(NOT metric_rollback_audit_before EQUAL 5 OR NOT metric_rollback_audit_after EQUAL 0 OR metric_rollback_audit_match)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release acceptance should expose rollback audit before/after metrics")
endif()
if(NOT evidence0 STREQUAL "dashboard-json" OR NOT evidence8 STREQUAL "rollback-preview-markdown" OR NOT evidence9 STREQUAL "rollback-audit-json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release acceptance evidence bundle should merge and preserve order")
endif()
if(has_title EQUAL -1 OR has_gate EQUAL -1 OR has_audit_gate_detail EQUAL -1 OR has_retry_metric EQUAL -1 OR has_rollback_metric EQUAL -1
    OR has_rollback_audit_before_metric EQUAL -1 OR has_rollback_audit_after_metric EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Release acceptance markdown missing title, gate, details, or metrics")
endif()

set(PLAN_ONLY_DASHBOARD_PATH "${TEMP_DIR}/database-health-dashboard-plan-only.json")
set(PLAN_ONLY_SMOKE_JSON_PATH "${TEMP_DIR}/pgsql-smoke-plan-only.json")
set(PLAN_ONLY_OUTPUT_JSON_PATH "${TEMP_DIR}/pgsql-release-acceptance-plan-only.json")
set(PLAN_ONLY_OUTPUT_MD_PATH "${TEMP_DIR}/pgsql-release-acceptance-plan-only.md")
file(WRITE "${PLAN_ONLY_DASHBOARD_PATH}" [=[
{
  "status":"healthy",
  "ok":true,
  "planOnly":true,
  "queryMetrics":{"slowQueryCount":0,"queryFailureCount":0},
  "summary":{"readiness":"ready","operatorAction":"Run the database health task without PlanOnly and provide QTNETWORKCHAT_PGPASSWORD from the environment to verify live database health."},
  "auditSummary":{"releaseGate":"await-live-health-check","auditFocus":["plan-only-health-evidence"],"evidenceBundle":["dashboard-json","dashboard-markdown","query-metrics"]}
}
]=])
file(WRITE "${PLAN_ONLY_SMOKE_JSON_PATH}" [=[
{
  "ok":true,
  "planOnly":true,
  "summary":{"readiness":"ready","operatorAction":"Runtime prerequisites look ready; next run can enable real smoke against the target PostgreSQL."},
  "auditSummary":{"releaseGate":"await-real-smoke","bootstrapRequired":false,"auditFocus":["offline-attachment-recovery"],"evidenceBundle":["json","markdown","boundary-scenarios"]},
  "recoverySummary":{"retryOrResumeCount":3,"cleanupProofCount":6,"releaseHint":"Use real smoke evidence to confirm retry, cleanup, and restart recovery paths."}
}
]=])
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -DatabaseHealthDashboardPath "${PLAN_ONLY_DASHBOARD_PATH}"
        -SmokeJsonPath "${PLAN_ONLY_SMOKE_JSON_PATH}"
        -MigrationJsonPath "${TEMP_DIR}/missing-migration.json"
        -RollbackPreviewPath "${TEMP_DIR}/missing-rollback-preview.json"
        -RollbackAuditPath "${TEMP_DIR}/missing-rollback-audit.json"
        -JsonPath "${PLAN_ONLY_OUTPUT_JSON_PATH}"
        -MarkdownPath "${PLAN_ONLY_OUTPUT_MD_PATH}"
    RESULT_VARIABLE plan_only_result
    OUTPUT_VARIABLE plan_only_output
    ERROR_VARIABLE plan_only_error_output
)
if(NOT plan_only_output STREQUAL "")
    message(STATUS "${plan_only_output}")
endif()
if(NOT plan_only_error_output STREQUAL "")
    message(STATUS "${plan_only_error_output}")
endif()
if(NOT plan_only_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Plan-only PostgreSQL release acceptance run exited with code ${plan_only_result}")
endif()
file(READ "${PLAN_ONLY_OUTPUT_JSON_PATH}" plan_only_json_content)
file(READ "${PLAN_ONLY_OUTPUT_MD_PATH}" plan_only_markdown_content)
string(JSON plan_only_status GET "${plan_only_json_content}" "status")
string(JSON plan_only_ok GET "${plan_only_json_content}" "ok")
string(JSON plan_only_flag GET "${plan_only_json_content}" "planOnly")
string(JSON plan_only_release_gate GET "${plan_only_json_content}" "auditSummary" "releaseGate")
string(JSON plan_only_operator_action GET "${plan_only_json_content}" "summary" "operatorAction")
string(FIND "${plan_only_markdown_content}" "Plan only: `true`" plan_only_markdown_flag)
if(NOT plan_only_status STREQUAL "review" OR plan_only_ok OR NOT plan_only_flag)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Plan-only acceptance should remain review / ok=false / planOnly=true")
endif()
if(NOT plan_only_release_gate STREQUAL "await-live-pgsql-release-acceptance")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Plan-only acceptance should wait for live PostgreSQL release evidence")
endif()
if(NOT plan_only_operator_action MATCHES "Run with live PostgreSQL credentials")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Plan-only acceptance should tell operator to generate live evidence")
endif()
if(plan_only_markdown_flag EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Plan-only acceptance markdown should expose planOnly=true")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
