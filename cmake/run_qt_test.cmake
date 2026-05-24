if(NOT DEFINED TEST_EXE OR NOT EXISTS "${TEST_EXE}")
    message(FATAL_ERROR "TEST_EXE does not exist: ${TEST_EXE}")
endif()

if(WIN32 AND DEFINED QT_BIN_DIR AND EXISTS "${QT_BIN_DIR}")
    set(ENV{PATH} "${QT_BIN_DIR};$ENV{PATH}")
endif()

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
