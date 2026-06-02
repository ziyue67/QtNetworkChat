if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/pgsql_protocol_smoke_plan")
set(FAKE_BUILD_DIR "${TEMP_DIR}/build")
set(FAKE_QT_ROOT "${TEMP_DIR}/qt")
set(FAKE_PG_BIN "${TEMP_DIR}/postgres/bin")
set(JSON_PATH "${TEMP_DIR}/pgsql-smoke-plan.json")
set(MARKDOWN_PATH "${TEMP_DIR}/pgsql-smoke-plan.md")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY
    "${FAKE_BUILD_DIR}"
    "${FAKE_QT_ROOT}/bin"
    "${FAKE_QT_ROOT}/plugins/sqldrivers"
    "${FAKE_PG_BIN}"
)
file(WRITE "${FAKE_BUILD_DIR}/postgres_qpsql_protocol_smoke_test.exe" "fake qpsql smoke test")
file(WRITE "${FAKE_QT_ROOT}/bin/Qt6Core.dll" "fake Qt runtime")
file(WRITE "${FAKE_QT_ROOT}/plugins/sqldrivers/qsqlpsql.dll" "fake qpsql plugin")
file(WRITE "${FAKE_PG_BIN}/libpq.dll" "fake libpq")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -TestExe "${FAKE_BUILD_DIR}/postgres_qpsql_protocol_smoke_test.exe"
        -QtRoot "${FAKE_QT_ROOT}"
        -PostgresBinDir "${FAKE_PG_BIN}"
        -PostgresPassword "not-used-in-plan"
        -PlanOnly
        -JsonPath "${JSON_PATH}"
        -MarkdownPath "${MARKDOWN_PATH}"
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
    message(FATAL_ERROR "run-pgsql-protocol-smoke.ps1 exited with code ${result}")
endif()
if(NOT EXISTS "${JSON_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan JSON was not created")
endif()
if(NOT EXISTS "${MARKDOWN_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan Markdown was not created")
endif()

file(READ "${JSON_PATH}" json_content)
file(READ "${MARKDOWN_PATH}" markdown_content)
string(JSON format GET "${json_content}" "format")
string(JSON plan_only GET "${json_content}" "planOnly")
string(JSON ok GET "${json_content}" "ok")
string(JSON password GET "${json_content}" "environment" "QTNETWORKCHAT_PGPASSWORD")
string(JSON driver GET "${json_content}" "environment" "QTNETWORKCHAT_DB_DRIVER")
string(JSON coverage0 GET "${json_content}" "coverageSurfaces" 0)
string(JSON boundary0 GET "${json_content}" "boundaryScenarios" 0 "name")
string(JSON boundary0_category GET "${json_content}" "boundaryScenarios" 0 "category")
string(JSON boundary1_persistence GET "${json_content}" "boundaryScenarios" 1 "persistence")
string(FIND "${json_content}" "public-group-member-role-audit" has_group_role)
string(FIND "${json_content}" "public-group-remove-readd-marker" has_group_marker)
string(FIND "${json_content}" "offline-attachment-queue-replay" has_offline_attachment)
string(FIND "${json_content}" "offline-attachment-chunk-metadata" has_offline_attachment_metadata)
string(FIND "${json_content}" "offline-attachment-missing-file-cleanup" has_offline_attachment_missing)
string(FIND "${json_content}" "offline-attachment-size-hash-chunk-cleanup" has_offline_attachment_corruptions)
string(FIND "${json_content}" "offline-attachment-partial-ack-resume" has_offline_attachment_resume)
string(FIND "${json_content}" "offline-attachment-expired-resume-fallback" has_offline_attachment_expired_resume)
string(FIND "${json_content}" "file-chunk-metadata-persistence" has_file_chunk_boundary)
string(FIND "${json_content}" "online-file-chunk-invalid-ack-retry" has_online_file_retry)
string(FIND "${markdown_content}" "PostgreSQL QPSQL Protocol Smoke Evidence" has_markdown_title)
string(FIND "${markdown_content}" "Boundary Scenarios" has_markdown_boundaries)
string(FIND "${markdown_content}" "<redacted>" has_markdown_redacted)
string(FIND "${markdown_content}" "not-used-in-plan" has_markdown_secret)
if(NOT format STREQUAL "qtnetworkchat-pgsql-protocol-smoke-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected PostgreSQL smoke format: ${format}")
endif()
if(NOT plan_only)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PlanOnly should be true")
endif()
if(NOT ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan should pass with fake runtime files")
endif()
if(NOT password STREQUAL "<redacted>")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan leaked a password")
endif()
if(NOT driver STREQUAL "QPSQL")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan should use QPSQL")
endif()
if(NOT coverage0 STREQUAL "register-login")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan should expose register-login coverage")
endif()
if(has_group_role EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan should expose public group member role audit coverage")
endif()
if(has_group_marker EQUAL -1 OR has_offline_attachment EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan should expose group marker and offline attachment coverage")
endif()
if(NOT boundary0 STREQUAL "offline-private-queue-replay" OR NOT boundary0_category STREQUAL "offline-message")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan should expose offline private replay as first boundary")
endif()
if(NOT boundary1_persistence STREQUAL "offline_messages.payload")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan should expose offline attachment payload persistence")
endif()
if(has_offline_attachment_metadata EQUAL -1
    OR has_offline_attachment_missing EQUAL -1
    OR has_offline_attachment_corruptions EQUAL -1
    OR has_offline_attachment_resume EQUAL -1
    OR has_offline_attachment_expired_resume EQUAL -1
    OR has_file_chunk_boundary EQUAL -1
    OR has_online_file_retry EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke plan should expose offline attachment metadata, corruption cleanup, resume fallback and online file chunk retry boundaries")
endif()
if(has_markdown_title EQUAL -1 OR has_markdown_boundaries EQUAL -1 OR has_markdown_redacted EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke Markdown should include title, boundary section and redacted password")
endif()
if(NOT has_markdown_secret EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PostgreSQL smoke Markdown leaked the provided password")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
