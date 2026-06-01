if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/database_health_task_sample")
set(OUTPUT_DIR "${TEMP_DIR}/database-health-output")
set(TASK_DIR "${TEMP_DIR}/task")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -TaskName "QtNetworkChatDatabaseHealthPreview"
        -Schedule Hourly
        -EveryHours 4
        -At "03:15"
        -Driver postgres
        -QtRoot "D:/Qt/6.8.3/mingw_64"
        -PostgresBinDir "D:/Program Files/PostgreSQL/17/bin"
        -PostgresHost "127.0.0.1"
        -PostgresPort 5432
        -PostgresDatabase "qtnetworkchat"
        -PostgresUser "postgres"
        -OutputDir "${OUTPUT_DIR}"
        -TaskDir "${TASK_DIR}"
        -PlanOnly
        -FailOnUnhealthy
        -WriteMarkdown
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
    message(FATAL_ERROR "register-database-health-task.ps1 preview exited with code ${result}")
endif()

set(LAUNCHER "${TASK_DIR}/run-database-health-task.ps1")
set(PREVIEW "${TASK_DIR}/database-health-task-preview.json")
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
        "-Driver 'postgres'"
        "-PostgresPassword '$env:QTNETWORKCHAT_PGPASSWORD'"
        "-PlanOnly"
        "-FailOnUnhealthy"
        "-MarkdownPath"
        "database-health.json"
        "database-health-status.json")
    string(FIND "${launcher_content}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Launcher missing expected text: ${expected_text}")
    endif()
endforeach()

foreach(forbidden_text "forbidden-password-sample" "super-secret")
    string(FIND "${launcher_content}" "${forbidden_text}" leaked_at)
    if(NOT leaked_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Launcher leaked forbidden text: ${forbidden_text}")
    endif()
endforeach()

file(READ "${PREVIEW}" preview_content)
string(JSON format GET "${preview_content}" "format")
string(JSON task_name GET "${preview_content}" "taskName")
string(JSON should_register GET "${preview_content}" "register")
string(JSON schedule GET "${preview_content}" "schedule")
string(JSON every_hours GET "${preview_content}" "everyHours")
string(JSON driver GET "${preview_content}" "driver")
string(JSON plan_only GET "${preview_content}" "planOnly")
string(JSON fail_on_unhealthy GET "${preview_content}" "failOnUnhealthy")
string(JSON password_source GET "${preview_content}" "passwordSource")
string(JSON health_path GET "${preview_content}" "healthPath")
string(JSON status_path GET "${preview_content}" "statusPath")
string(JSON markdown_path GET "${preview_content}" "markdownPath")
string(JSON read_only GET "${preview_content}" "readOnly")

if(NOT format STREQUAL "qtnetworkchat-database-health-task-preview-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected preview format: ${format}")
endif()
if(NOT task_name STREQUAL "QtNetworkChatDatabaseHealthPreview")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected taskName in preview: ${task_name}")
endif()
if(should_register)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview mode should not register the scheduled task")
endif()
if(NOT schedule STREQUAL "Hourly" OR NOT "${every_hours}" STREQUAL "4")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected schedule preview: ${schedule}/${every_hours}")
endif()
if(NOT driver STREQUAL "postgres" OR NOT plan_only OR NOT fail_on_unhealthy OR NOT read_only)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview flags did not match expected postgres plan-only read-only health task")
endif()
if(NOT password_source STREQUAL "QTNETWORKCHAT_PGPASSWORD")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview should point passwordSource at QTNETWORKCHAT_PGPASSWORD")
endif()
if(NOT health_path MATCHES "database-health.json" OR NOT status_path MATCHES "database-health-status.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview health/status paths missing expected names")
endif()
if(NOT markdown_path MATCHES "database-health-status.md")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Preview markdown path missing expected name: ${markdown_path}")
endif()
foreach(forbidden_text "forbidden-password-sample" "super-secret")
    string(FIND "${preview_content}" "${forbidden_text}" leaked_at)
    if(NOT leaked_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Preview leaked forbidden text: ${forbidden_text}")
    endif()
endforeach()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -Driver postgres
        -PostgresUser "password=super-secret"
        -OutputDir "${OUTPUT_DIR}/bad"
        -TaskDir "${TEMP_DIR}/bad-task"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Database health scheduled task helper should reject sensitive user input")
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
    message(FATAL_ERROR "Database health scheduled task helper should reject EveryHours=0")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Database health scheduled task helper test passed")
