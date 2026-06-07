if(NOT DEFINED TEST_EXE OR NOT EXISTS "${TEST_EXE}")
    message(FATAL_ERROR "TEST_EXE does not exist: ${TEST_EXE}")
endif()

if(WIN32 AND DEFINED QT_BIN_DIR AND EXISTS "${QT_BIN_DIR}")
    set(ENV{PATH} "${QT_BIN_DIR};$ENV{PATH}")
endif()

get_filename_component(TEST_WORK_DIR "${TEST_EXE}" DIRECTORY)
set(TEST_RUNTIME_ROOT "${TEST_WORK_DIR}/qt_test_runtime")
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
set(ENV{QTNETWORKCHAT_APPDATA_DIR} "${TEST_RUNTIME_ROOT}/AppData/QtNetworkChat")

execute_process(
    COMMAND "${TEST_EXE}"
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
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR "Qt test failed with exit code ${test_result}")
endif()
