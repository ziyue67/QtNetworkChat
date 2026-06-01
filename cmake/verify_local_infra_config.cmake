if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/local_infra_config_sample")
set(PG_BIN "${TEMP_DIR}/postgres/bin")
set(REDIS_BIN "${TEMP_DIR}/redis")
set(MINIO_DIR "${TEMP_DIR}/minio")
set(JSON_PATH "${TEMP_DIR}/local-infra.json")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${PG_BIN}" "${REDIS_BIN}" "${MINIO_DIR}")
file(WRITE "${PG_BIN}/psql.exe" "fake psql")
file(WRITE "${REDIS_BIN}/redis-cli.exe" "fake redis cli")
file(WRITE "${MINIO_DIR}/minio" "fake minio")
file(WRITE "${MINIO_DIR}/mc" "fake mc")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -PostgresBinDir "${PG_BIN}"
        -RedisBinDir "${REDIS_BIN}"
        -MinioServerPath "${MINIO_DIR}/minio"
        -MinioClientPath "${MINIO_DIR}/mc"
        -MinioEndpoint "http://127.0.0.1:19000"
        -PlanOnly
        -JsonPath "${JSON_PATH}"
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
    message(FATAL_ERROR "verify-local-infra.ps1 exited with code ${result}")
endif()
if(NOT EXISTS "${JSON_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Local infra JSON was not created")
endif()

file(READ "${JSON_PATH}" json_content)
string(JSON format GET "${json_content}" "format")
string(JSON plan_only GET "${json_content}" "planOnly")
string(JSON db_driver GET "${json_content}" "windowsEnvironment" "QTNETWORKCHAT_DB_DRIVER")
string(JSON linux_export GET "${json_content}" "linuxExports" 0)
if(NOT format STREQUAL "qtnetworkchat-local-infra-check-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected local infra format: ${format}")
endif()
if(NOT plan_only)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "PlanOnly should be true")
endif()
if(NOT db_driver STREQUAL "QPSQL")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected QPSQL driver in Windows env")
endif()
if(NOT linux_export MATCHES "export QTNETWORKCHAT_DB_DRIVER=QPSQL")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Linux export sample is missing QPSQL driver")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
