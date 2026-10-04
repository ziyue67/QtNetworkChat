if(NOT DEFINED TEST_EXE OR NOT EXISTS "${TEST_EXE}")
    message(FATAL_ERROR "TEST_EXE does not exist: ${TEST_EXE}")
endif()

if(WIN32 AND DEFINED QT_BIN_DIR AND EXISTS "${QT_BIN_DIR}")
    set(ENV{PATH} "${QT_BIN_DIR};$ENV{PATH}")
endif()
if(WIN32 AND NOT DEFINED OPENSSL_RUNTIME_DIR AND EXISTS "D:/Qt/Tools/mingw1310_64/opt/bin")
    set(OPENSSL_RUNTIME_DIR "D:/Qt/Tools/mingw1310_64/opt/bin")
endif()
if(WIN32 AND DEFINED OPENSSL_RUNTIME_DIR AND EXISTS "${OPENSSL_RUNTIME_DIR}")
    set(ENV{PATH} "${OPENSSL_RUNTIME_DIR};$ENV{PATH}")
endif()

get_filename_component(TEST_WORK_DIR "${TEST_EXE}" DIRECTORY)
get_filename_component(TEST_NAME "${TEST_EXE}" NAME_WE)
set(TEST_RUNTIME_ROOT "${TEST_WORK_DIR}/qt_test_runtime/${TEST_NAME}")
file(REMOVE_RECURSE "${TEST_RUNTIME_ROOT}")
file(MAKE_DIRECTORY
    "${TEST_RUNTIME_ROOT}/AppData/Roaming"
    "${TEST_RUNTIME_ROOT}/AppData/Local"
    "${TEST_RUNTIME_ROOT}/Temp"
)

set(ENV{APPDATA} "${TEST_RUNTIME_ROOT}/AppData/Roaming")
set(ENV{LOCALAPPDATA} "${TEST_RUNTIME_ROOT}/AppData/Local")
set(ENV{TEMP} "${TEST_RUNTIME_ROOT}/Temp")
set(ENV{TMP} "${TEST_RUNTIME_ROOT}/Temp")
set(ENV{TMPDIR} "${TEST_RUNTIME_ROOT}/Temp")
set(ENV{QTNETWORKCHAT_APPDATA_DIR} "${TEST_RUNTIME_ROOT}/AppData/QtNetworkChat")
set(ENV{QTNETWORKCHAT_TLS} "0")
if(NOT DEFINED ENV{QT_QPA_PLATFORM})
    set(ENV{QT_QPA_PLATFORM} "offscreen")
endif()
if(NOT DEFINED ENV{QTNETWORKCHAT_DB_DRIVER})
    set(ENV{QTNETWORKCHAT_DB_DRIVER} "QSQLITE")
endif()

set(test_timeout_args)
if(DEFINED TEST_TIMEOUT)
    list(APPEND test_timeout_args TIMEOUT "${TEST_TIMEOUT}")
endif()

execute_process(
    COMMAND "${TEST_EXE}" ${TEST_ARGS}
    ${test_timeout_args}
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_output
    ERROR_VARIABLE test_error
)

if(NOT test_output STREQUAL "")
    message(STATUS "${test_output}")
endif()
if(NOT test_error STREQUAL "")
    message(STATUS "${test_error}")
endif()
if(NOT test_result STREQUAL "0")
    message(FATAL_ERROR "Qt test failed with exit code ${test_result}")
endif()
