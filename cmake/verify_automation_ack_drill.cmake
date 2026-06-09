if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()
if(NOT DEFINED ACK_SCRIPT_PATH OR NOT EXISTS "${ACK_SCRIPT_PATH}")
    message(FATAL_ERROR "ACK_SCRIPT_PATH does not exist: ${ACK_SCRIPT_PATH}")
endif()
if(NOT DEFINED HISTORY_SCRIPT_PATH OR NOT EXISTS "${HISTORY_SCRIPT_PATH}")
    message(FATAL_ERROR "HISTORY_SCRIPT_PATH does not exist: ${HISTORY_SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/automation_ack_drill_sample")
set(DRILL_JSON "${TEMP_DIR}/automation-ack-drill.json")
set(DRILL_MD "${TEMP_DIR}/automation-ack-drill.md")
set(HISTORY_JSON "${TEMP_DIR}/automation-task-history.json")
set(ACK_JSON "${TEMP_DIR}/automation-task-ack.json")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${TEMP_DIR}"
        -AckScriptPath "${ACK_SCRIPT_PATH}"
        -HistoryScriptPath "${HISTORY_SCRIPT_PATH}"
        -AcknowledgedBy "oncall-drill"
        -Reason "ack drill test"
        -FailedAt "2026-06-03T08:00:00Z"
        -AcknowledgedAt "2026-06-03T09:00:00Z"
        -AckExpiryHours 0
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
    message(FATAL_ERROR "Automation ack drill exited with code ${result}")
endif()
if(NOT EXISTS "${DRILL_JSON}" OR NOT EXISTS "${DRILL_MD}" OR NOT EXISTS "${HISTORY_JSON}" OR NOT EXISTS "${ACK_JSON}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Automation ack drill did not create expected evidence artifacts")
endif()

file(READ "${DRILL_JSON}" json_content)
file(READ "${DRILL_MD}" markdown_content)
string(JSON format GET "${json_content}" "format")
string(JSON state GET "${json_content}" "state")
string(JSON ok GET "${json_content}" "ok")
string(JSON release_gate GET "${json_content}" "releaseGate")
string(JSON failed_count GET "${json_content}" "failedRunCount")
string(JSON acknowledged GET "${json_content}" "acknowledged")
string(JSON history_acknowledged GET "${json_content}" "historyAcknowledged")
string(JSON ack_expired GET "${json_content}" "ackExpired")
string(JSON ack_reminder GET "${json_content}" "ackReminder")
string(JSON live_task_mutation GET "${json_content}" "liveTaskMutation")
if(NOT "${format}" STREQUAL "qtnetworkchat-automation-ack-drill-v1"
        OR NOT "${state}" STREQUAL "exercised"
        OR NOT "${ok}" STREQUAL "ON"
        OR NOT "${release_gate}" STREQUAL "automation-ack-drill-exercised"
        OR NOT "${failed_count}" STREQUAL "1"
        OR NOT "${acknowledged}" STREQUAL "ON"
        OR NOT "${history_acknowledged}" STREQUAL "ON"
        OR NOT "${ack_expired}" STREQUAL "OFF"
        OR NOT "${ack_reminder}" STREQUAL "acknowledged"
        OR NOT "${live_task_mutation}" STREQUAL "OFF")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Automation ack drill evidence did not prove exercised acknowledged failure handling")
endif()
foreach(expected_text IN ITEMS
    "QtNetworkChat Automation Ack Drill"
    "Release gate: `automation-ack-drill-exercised`"
    "Live task mutation: `false`")
    string(FIND "${markdown_content}" "${expected_text}" expected_index)
    if(expected_index EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Automation ack drill Markdown missing: ${expected_text}")
    endif()
endforeach()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${TEMP_DIR}/bad"
        -AckScriptPath "${ACK_SCRIPT_PATH}"
        -HistoryScriptPath "${HISTORY_SCRIPT_PATH}"
        -AcknowledgedBy "password=super-secret"
        -FailOnSensitive
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error_output
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Automation ack drill should reject sensitive acknowledgedBy values")
endif()

foreach(forbidden IN ITEMS
    "password=super-secret"
    "ghp_"
    "github_pat_"
    "Authorization:"
    "Credential="
    "Signature=")
    string(FIND "${json_content}\n${markdown_content}" "${forbidden}" forbidden_index)
    if(NOT forbidden_index EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Automation ack drill evidence leaked forbidden text: ${forbidden}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Automation ack drill test passed")
