if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()
if(POLICY CMP0054)
    cmake_policy(SET CMP0054 NEW)
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/automation_task_history_sample")
set(LAST_RUN_A "${TEMP_DIR}/database-last-run.log")
set(LAST_RUN_B "${TEMP_DIR}/governance-last-run.log")
set(ACK_PATH "${TEMP_DIR}/ack.json")
set(JSON_PATH "${TEMP_DIR}/history.json")
set(MD_PATH "${TEMP_DIR}/history.md")
set(BAD_LAST_RUN "${TEMP_DIR}/bad-last-run.log")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

file(WRITE "${LAST_RUN_A}"
"2026-06-03T01:02:03.0000000Z healthExitCode=0 statusExitCode=0 dashboardExitCode=0 exitCode=0 healthPath=redacted statusPath=redacted\n"
"2026-06-03T02:02:03.0000000Z healthExitCode=2 statusExitCode=2 dashboardExitCode=0 exitCode=2 healthPath=redacted statusPath=redacted\n")
file(WRITE "${LAST_RUN_B}" "2026-06-03T03:02:03.0000000Z exitCode=0\n")
file(WRITE "${ACK_PATH}"
"{
  \"acknowledged\": true,
  \"acknowledgedBy\": \"operator-ci\",
  \"acknowledgedAt\": \"2026-06-03T04:00:00Z\",
  \"reason\": \"planned maintenance\"
}
")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -LastRunPath "${LAST_RUN_A},${LAST_RUN_B}"
        -AckPath "${ACK_PATH}"
        -AckExpiryHours 240
        -RetentionCount 2
        -JsonPath "${JSON_PATH}"
        -MarkdownPath "${MD_PATH}"
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
    message(FATAL_ERROR "write-automation-task-history.ps1 exited with code ${result}")
endif()
if(NOT EXISTS "${JSON_PATH}" OR NOT EXISTS "${MD_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Automation task history outputs were not created")
endif()

file(READ "${JSON_PATH}" json_content)
string(JSON format GET "${json_content}" "format")
string(JSON run_count GET "${json_content}" "runCount")
string(JSON failed_run_count GET "${json_content}" "failedRunCount")
string(JSON latest_timestamp GET "${json_content}" "latestRun" "timestamp")
string(JSON latest_exit_code GET "${json_content}" "latestRun" "exitCode")
string(JSON acknowledged GET "${json_content}" "acknowledged")
string(JSON ack_expired GET "${json_content}" "ackExpired")
string(JSON acknowledged_by GET "${json_content}" "acknowledgedBy")
string(JSON ack_reminder GET "${json_content}" "ackReminder")
string(JSON ack_expiry_hours GET "${json_content}" "ackExpiryHours")
string(JSON ack_hours_remaining GET "${json_content}" "ackHoursRemaining")
if(NOT format STREQUAL "qtnetworkchat-automation-task-history-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected history format: ${format}")
endif()
if(NOT run_count EQUAL 2 OR NOT failed_run_count EQUAL 1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected history run counters: ${run_count}/${failed_run_count}")
endif()
if(NOT latest_timestamp STREQUAL "2026-06-03T03:02:03.0000000Z" OR NOT latest_exit_code EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected latest run summary: ${latest_timestamp}/${latest_exit_code}")
endif()
if(NOT acknowledged OR ack_expired OR NOT acknowledged_by STREQUAL "operator-ci")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected acknowledged operator-ci state")
endif()
if(NOT ("${ack_reminder}" STREQUAL "acknowledged" OR "${ack_reminder}" STREQUAL "renew-soon") OR NOT "${ack_expiry_hours}" STREQUAL "240" OR "${ack_hours_remaining}" STREQUAL "unknown")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected acknowledged reminder details, got ${ack_reminder}/${ack_expiry_hours}/${ack_hours_remaining}")
endif()

file(READ "${MD_PATH}" markdown_content)
foreach(expected_text
        "QtNetworkChat Automation Task History"
        "Run count: `2`"
        "Failed runs: `1`"
        "Acknowledged: `true`"
        "Ack expired: `false`"
        "Ack expiry hours: `240`"
        "operator-ci")
    string(FIND "${markdown_content}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "History markdown missing expected text: ${expected_text}")
    endif()
endforeach()
if(NOT (markdown_content MATCHES "Ack reminder: `acknowledged`" OR markdown_content MATCHES "Ack reminder: `renew-soon`"))
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "History markdown missing expected ack reminder state")
endif()

file(WRITE "${BAD_LAST_RUN}" "2026-06-03T05:00:00Z exitCode=0 password=super-secret\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -LastRunPath "${BAD_LAST_RUN}"
        -FailOnSensitive
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(NOT bad_result EQUAL 2)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Sensitive last-run history should fail with exit 2, got ${bad_result}")
endif()

file(WRITE "${ACK_PATH}"
"{
  \"acknowledged\": true,
  \"acknowledgedBy\": \"operator-old\",
  \"acknowledgedAt\": \"2026-05-01T04:00:00Z\",
  \"reason\": \"stale\"
}
")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -LastRunPath "${LAST_RUN_A}"
        -AckPath "${ACK_PATH}"
        -AckExpiryHours 1
        -JsonPath "${JSON_PATH}"
    RESULT_VARIABLE expired_result
    OUTPUT_VARIABLE expired_output
    ERROR_VARIABLE expired_error
)
if(NOT expired_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expired ack run should still succeed")
endif()
file(READ "${JSON_PATH}" expired_json_content)
string(JSON expired_ack GET "${expired_json_content}" "acknowledged")
string(JSON expired_flag GET "${expired_json_content}" "ackExpired")
string(JSON expired_reason GET "${expired_json_content}" "ackReason")
string(JSON expired_reminder GET "${expired_json_content}" "ackReminder")
string(JSON expired_overdue GET "${expired_json_content}" "ackHoursOverdue")
if(expired_ack OR NOT expired_flag OR expired_reason STREQUAL "" OR NOT "${expired_reminder}" STREQUAL "renew-required" OR "${expired_overdue}" STREQUAL "unknown")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expired ack should be downgraded and marked expired")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Automation task history test passed")
