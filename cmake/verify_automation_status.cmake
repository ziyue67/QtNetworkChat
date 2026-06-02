if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/automation_status_sample")
set(MARKDOWN_PATH "${TEMP_DIR}/automation-status.md")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

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
        -FailOnSensitive
    RESULT_VARIABLE plan_result
    OUTPUT_VARIABLE plan_output
    ERROR_VARIABLE plan_error_output
)
if(NOT plan_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "write-automation-status.ps1 plan mode exited with code ${plan_result}")
endif()
string(FIND "${plan_output}" "HEAD: `def5678`" plan_head)
if(plan_head EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Automation status plan output missing supplied HEAD")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Automation status writer test passed")
