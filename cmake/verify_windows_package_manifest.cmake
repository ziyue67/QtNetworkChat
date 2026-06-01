if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()
if(NOT DEFINED REPO_ROOT OR NOT EXISTS "${REPO_ROOT}/README.md")
    message(FATAL_ERROR "REPO_ROOT is invalid: ${REPO_ROOT}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/windows_package_manifest_sample")
set(FAKE_BUILD_DIR "${TEMP_DIR}/fake-build")
set(FAKE_QT_ROOT "${TEMP_DIR}/fake-qt")
set(FAKE_PG_BIN "${TEMP_DIR}/fake-pg/bin")
set(PACKAGE_DIR "${TEMP_DIR}/dist")
set(EXTRACT_DIR "${TEMP_DIR}/extracted")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY
    "${FAKE_BUILD_DIR}"
    "${FAKE_QT_ROOT}/plugins/sqldrivers"
    "${FAKE_PG_BIN}"
    "${PACKAGE_DIR}"
    "${EXTRACT_DIR}"
)
file(WRITE "${FAKE_BUILD_DIR}/QtNetworkChat.exe" "fake executable for package manifest test\n")
file(WRITE "${FAKE_QT_ROOT}/plugins/sqldrivers/qsqlpsql.dll" "fake qpsql plugin\n")
foreach(dep IN ITEMS libpq.dll libssl-3-x64.dll libcrypto-3-x64.dll libintl-9.dll libiconv-2.dll zlib1.dll)
    file(WRITE "${FAKE_PG_BIN}/${dep}" "fake ${dep}\n")
endforeach()

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -BuildDir "${FAKE_BUILD_DIR}"
        -PackageDir "${PACKAGE_DIR}"
        -QtRoot "${FAKE_QT_ROOT}"
        -PostgresBinDir "${FAKE_PG_BIN}"
        -SkipBuild
        -NoDeploy
        -IncludePostgresSql
        -FailOnMissingPostgresSql
    WORKING_DIRECTORY "${REPO_ROOT}"
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
    message(FATAL_ERROR "package-windows.ps1 exited with code ${result}")
endif()

set(STAGE_DIR "${PACKAGE_DIR}/QtNetworkChat-1.0.0-win-x64")
set(ZIP_PATH "${PACKAGE_DIR}/QtNetworkChat-1.0.0-win-x64.zip")
set(MANIFEST "${STAGE_DIR}/manifest.json")
if(NOT EXISTS "${MANIFEST}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Package manifest was not created: ${MANIFEST}")
endif()
if(NOT EXISTS "${ZIP_PATH}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Package zip was not created: ${ZIP_PATH}")
endif()

file(READ "${MANIFEST}" manifest_content)
string(JSON package_format GET "${manifest_content}" "packageFormat")
string(JSON version GET "${manifest_content}" "version")
string(JSON runtime_ok GET "${manifest_content}" "runtimeCheck" "ok")
string(JSON deploy_attempted GET "${manifest_content}" "runtimeCheck" "deployAttempted")
string(JSON postgres_requested GET "${manifest_content}" "postgresSqlRuntime" "requested")
string(JSON postgres_ok GET "${manifest_content}" "postgresSqlRuntime" "ok")
if(NOT package_format STREQUAL "qtnetworkchat-windows-package-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected packageFormat: ${package_format}")
endif()
if(NOT version STREQUAL "1.0.0")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected package version: ${version}")
endif()
if(NOT runtime_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Runtime check should pass when -NoDeploy checks only exe and README")
endif()
if(deploy_attempted)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "deployAttempted should be false with -NoDeploy")
endif()
if(NOT postgres_requested)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "postgresSqlRuntime.requested should be true")
endif()
if(NOT postgres_ok)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "postgresSqlRuntime.ok should be true when fake plugin and libpq deps exist")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xfz "${ZIP_PATH}"
    WORKING_DIRECTORY "${EXTRACT_DIR}"
    RESULT_VARIABLE extract_result
    OUTPUT_VARIABLE extract_output
    ERROR_VARIABLE extract_error
)
if(NOT extract_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Failed to extract Windows package: ${extract_error}")
endif()
if(NOT EXISTS "${EXTRACT_DIR}/manifest.json")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "manifest.json was not included in package zip")
endif()
if(NOT EXISTS "${EXTRACT_DIR}/QtNetworkChat.exe")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "QtNetworkChat.exe was not included in package zip")
endif()
if(NOT EXISTS "${EXTRACT_DIR}/README.md")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "README.md was not included in package zip")
endif()
if(NOT EXISTS "${EXTRACT_DIR}/sqldrivers/qsqlpsql.dll")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "qsqlpsql.dll was not included in package zip")
endif()
foreach(dep IN ITEMS libpq.dll libssl-3-x64.dll libcrypto-3-x64.dll libintl-9.dll libiconv-2.dll zlib1.dll)
    if(NOT EXISTS "${EXTRACT_DIR}/${dep}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "${dep} was not included in package zip")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEMP_DIR}")
