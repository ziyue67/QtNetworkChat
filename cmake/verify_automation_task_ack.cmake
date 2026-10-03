if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/automation_task_ack_sample")
set(ACK_PATH "${TEMP_DIR}/ack.json")
set(CLEAR_PATH "${TEMP_DIR}/ack-cleared.json")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -AckPath "${ACK_PATH}"
        -AcknowledgedBy "operator-ci"
        -Reason "planned maintenance"
        -AcknowledgedAt "2026-06-03T04:00:00Z"
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
    message(FATAL_ERROR "write-automation-task-ack.ps1 exited with code ${result}")
endif()
if(NOT EXISTS "${ACK_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Automation task ack JSON was not created")
endif()

file(READ "${ACK_PATH}" json_content)
string(JSON format GET "${json_content}" "format")
string(JSON acknowledged GET "${json_content}" "acknowledged")
string(JSON acknowledged_by GET "${json_content}" "acknowledgedBy")
string(JSON acknowledged_at GET "${json_content}" "acknowledgedAt")
string(JSON reason GET "${json_content}" "reason")
if(NOT format STREQUAL "qtnetworkchat-automation-task-ack-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected ack format: ${format}")
endif()
if(NOT acknowledged OR NOT acknowledged_by STREQUAL "operator-ci" OR NOT reason STREQUAL "planned maintenance")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected acknowledged payload values")
endif()
if(NOT acknowledged_at STREQUAL "2026-06-03T04:00:00.0000000+00:00")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected acknowledgedAt normalization: ${acknowledged_at}")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -AckPath "${CLEAR_PATH}"
        -Clear
        -Reason "handled"
        -FailOnSensitive
    RESULT_VARIABLE clear_result
    OUTPUT_VARIABLE clear_output
    ERROR_VARIABLE clear_error_output
)
if(NOT clear_output STREQUAL "")
    message(STATUS "${clear_output}")
endif()
if(NOT clear_error_output STREQUAL "")
    message(STATUS "${clear_error_output}")
endif()
if(NOT clear_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Clear ack run should succeed")
endif()
file(READ "${CLEAR_PATH}" clear_json_content)
string(JSON clear_ack GET "${clear_json_content}" "acknowledged")
string(JSON clear_by GET "${clear_json_content}" "acknowledgedBy")
string(JSON clear_at GET "${clear_json_content}" "acknowledgedAt")
string(JSON clear_reason GET "${clear_json_content}" "reason")
if(clear_ack OR NOT clear_by STREQUAL "cleared" OR NOT clear_reason STREQUAL "handled" OR NOT clear_at STREQUAL "")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Clear ack payload should reset acknowledged state")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -AckPath "${ACK_PATH}"
        -AcknowledgedBy "operator-preview"
        -PlanOnly
        -FailOnSensitive
    RESULT_VARIABLE plan_result
    OUTPUT_VARIABLE plan_output
    ERROR_VARIABLE plan_error_output
)
if(NOT plan_error_output STREQUAL "")
    message(STATUS "${plan_error_output}")
endif()
if(NOT plan_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Plan-only ack run should succeed")
endif()
string(JSON plan_by GET "${plan_output}" "acknowledgedBy")
string(JSON plan_ack GET "${plan_output}" "acknowledged")
if(NOT plan_by STREQUAL "operator-preview" OR NOT plan_ack)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Plan-only ack output missing expected JSON fields")
endif()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -AckPath "${ACK_PATH}"
        -AcknowledgedBy "password=super-secret"
        -FailOnSensitive
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error_output
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Sensitive acknowledgedBy should fail")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Automation task ack test passed")
