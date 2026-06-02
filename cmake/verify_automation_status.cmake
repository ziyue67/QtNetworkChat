if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/automation_status_sample")
set(MARKDOWN_PATH "${TEMP_DIR}/automation-status.md")
set(DB_STATUS_PATH "${TEMP_DIR}/database-health-status.json")
set(GOV_STATUS_PATH "${TEMP_DIR}/large-file-governance-status.json")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

file(WRITE "${DB_STATUS_PATH}"
"{
  \"format\":\"qtnetworkchat-database-health-status-v1\",
  \"status\":\"healthy\",
  \"ok\":true,
  \"driver\":\"QPSQL\",
  \"checkCount\":4,
  \"failedChecks\":[],
  \"queryMetrics\":{\"slowQueryCount\":2,\"queryFailureCount\":1}
}
")
file(WRITE "${GOV_STATUS_PATH}"
"{
  \"format\":\"qtnetworkchat-large-file-governance-status-v1\",
  \"status\":\"unhealthy\",
  \"ok\":false,
  \"totalWarnings\":3,
  \"alertCount\":2,
  \"s3CoverageActionableGapAreas\":[\"remote-validation-fail-closed\"]
}
")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -MarkdownPath "${MARKDOWN_PATH}"
        -Head "abc1234"
        -OriginMain "abc1234"
        -OriginCodexQt "abc1234"
        -CiStatus "success"
        -CiRunId "26816554264"
        -BuildStatus "passed"
        -CTestStatus "passed"
        -CTestCount 51
        -DatabaseHealthStatusPath "${DB_STATUS_PATH}"
        -LargeFileGovernanceStatusPath "${GOV_STATUS_PATH}"
        -ProtectedUntracked ".polaris/,AGENTS.md"
        -FailOnSensitive
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
    message(FATAL_ERROR "write-automation-status.ps1 exited with code ${result}")
endif()
if(NOT EXISTS "${MARKDOWN_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Automation status Markdown was not created")
endif()

file(READ "${MARKDOWN_PATH}" markdown_content)
foreach(expected_text
        "QtNetworkChat Automation Status"
        "HEAD: `abc1234`"
        "GitHub Windows Build: `success`"
        "Local CTest count: `51`"
        "Protected untracked entries: `.polaris/, AGENTS.md`"
        "Automation Guardrails"
        "Scheduled Task Readback"
        "Database health: status=`healthy`, ok=`true`, driver=`QPSQL`, checks=`4`, failedChecks=`0`, slowQueries=`2`, queryFailures=`1`"
        "Large-file governance: status=`unhealthy`, ok=`false`, warnings=`3`, alerts=`2`, actionableS3Gaps=`1`"
        "Priority Backlog"
        "QTNETWORKCHAT_PGPASSWORD"
        "generated evidence must remain redacted")
    string(FIND "${markdown_content}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Automation status missing expected text: ${expected_text}")
    endif()
endforeach()

foreach(forbidden_text
        "ghp_"
        "github_pat_"
        "password=super-secret"
        "Authorization:"
        "Credential="
        "Signature="
        "chenjun")
    string(FIND "${markdown_content}" "${forbidden_text}" leaked_at)
    if(NOT leaked_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Automation status leaked forbidden text: ${forbidden_text}")
    endif()
endforeach()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -PlanOnly
        -Head "def5678"
        -CiStatus "success"
        -DatabaseHealthStatusPath "${TEMP_DIR}/missing-database-health-status.json"
        -LargeFileGovernanceStatusPath "${TEMP_DIR}/missing-large-file-governance-status.json"
        -FailOnSensitive
    RESULT_VARIABLE plan_result
    OUTPUT_VARIABLE plan_output
    ERROR_VARIABLE plan_error_output
)
if(NOT plan_output STREQUAL "")
    message(STATUS "${plan_output}")
endif()
if(NOT plan_error_output STREQUAL "")
    message(STATUS "${plan_error_output}")
endif()
if(NOT plan_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "write-automation-status.ps1 plan mode exited with code ${plan_result}")
endif()
string(FIND "${plan_output}" "HEAD: `def5678`" plan_head)
if(plan_head EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Automation status plan output missing supplied HEAD")
endif()
string(FIND "${plan_output}" "origin/main: `unknown`" plan_origin_main)
if(plan_origin_main EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Automation status plan output missing origin/main fallback")
endif()
string(FIND "${plan_output}" "origin/codex/qt: `unknown`" plan_origin_codex_qt)
if(plan_origin_codex_qt EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Automation status plan output missing origin/codex/qt fallback")
endif()
string(FIND "${plan_output}" "Database health: `not configured`" plan_db_missing)
if(plan_db_missing EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Automation status plan output missing database health readback fallback")
endif()
string(FIND "${plan_output}" "Large-file governance: `not configured`" plan_governance_missing)
if(plan_governance_missing EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Automation status plan output missing large-file governance readback fallback")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Automation status writer test passed")
