if(NOT DEFINED SOURCE_DIR OR NOT EXISTS "${SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED PROBE_BUILD_DIR)
    message(FATAL_ERROR "PROBE_BUILD_DIR is required")
endif()
if(NOT DEFINED CMAKE_EXE OR NOT EXISTS "${CMAKE_EXE}")
    message(FATAL_ERROR "CMAKE_EXE is required")
endif()
if(NOT DEFINED GENERATOR_NAME OR "${GENERATOR_NAME}" STREQUAL "")
    message(FATAL_ERROR "GENERATOR_NAME is required")
endif()

file(REMOVE_RECURSE "${PROBE_BUILD_DIR}")

set(configure_args
    -S "${SOURCE_DIR}"
    -B "${PROBE_BUILD_DIR}"
    -G "${GENERATOR_NAME}"
    -DQTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER=ON
)
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
    list(APPEND configure_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
    list(APPEND configure_args -T "${GENERATOR_TOOLSET}")
endif()
if("${GENERATOR_NAME}" MATCHES "Ninja" AND DEFINED CMAKE_MAKE_PROGRAM AND NOT "${CMAKE_MAKE_PROGRAM}" STREQUAL "")
    list(APPEND configure_args -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM})
endif()
if(DEFINED CMAKE_CXX_COMPILER AND NOT "${CMAKE_CXX_COMPILER}" STREQUAL "")
    list(APPEND configure_args -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
endif()
if(DEFINED CMAKE_PREFIX_PATH AND NOT "${CMAKE_PREFIX_PATH}" STREQUAL "")
    list(APPEND configure_args -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH})
endif()

execute_process(
    COMMAND "${CMAKE_EXE}" ${configure_args}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE configure_stderr
)

string(CONCAT configure_output "${configure_stdout}" "\n" "${configure_stderr}")
string(FIND "${configure_output}" "production-adapter-implementation-missing" adapter_reason_pos)
string(FIND "${configure_output}" "QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER" adapter_message_pos)

file(REMOVE_RECURSE "${PROBE_BUILD_DIR}")

if(configure_result EQUAL 0)
    message(FATAL_ERROR "Production adapter link gate should fail closed until a reviewed implementation is wired")
endif()
if(adapter_reason_pos LESS 0 OR adapter_message_pos LESS 0)
    message(STATUS "Captured production adapter gate output: ${configure_output}")
    message(FATAL_ERROR "Production adapter link gate should report the fixed missing-implementation reason and switch name")
endif()

message(STATUS "E2E production adapter link gate verified")
