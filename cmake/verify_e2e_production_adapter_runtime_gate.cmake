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
if(NOT DEFINED QT_BIN_DIR OR NOT EXISTS "${QT_BIN_DIR}")
    message(FATAL_ERROR "QT_BIN_DIR is required")
endif()

file(REMOVE_RECURSE "${PROBE_BUILD_DIR}")

set(configure_args
    -S "${SOURCE_DIR}"
    -B "${PROBE_BUILD_DIR}"
    -G "${GENERATOR_NAME}"
    -DQTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER=ON
    -DBUILD_TESTING=ON
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
if(NOT configure_result EQUAL 0)
    file(REMOVE_RECURSE "${PROBE_BUILD_DIR}")
    message(STATUS "Captured production adapter runtime configure output: ${configure_stdout}\n${configure_stderr}")
    message(FATAL_ERROR "Production adapter runtime probe should configure")
endif()

execute_process(
    COMMAND "${CMAKE_EXE}" --build "${PROBE_BUILD_DIR}" --target e2e_production_adapter_runtime_test
        --config "${CONFIGURATION}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
)
if(NOT build_result EQUAL 0)
    file(REMOVE_RECURSE "${PROBE_BUILD_DIR}")
    message(STATUS "Captured production adapter runtime build output: ${build_stdout}\n${build_stderr}")
    message(FATAL_ERROR "Production adapter runtime probe should build e2e_production_adapter_runtime_test")
endif()

set(path_separator ":")
if(WIN32)
    set(path_separator ";")
endif()
set(probe_path "$ENV{PATH}")
if(NOT "${QT_BIN_DIR}" STREQUAL "")
    set(probe_path "${QT_BIN_DIR}${path_separator}$ENV{PATH}")
endif()
if(DEFINED OPENSSL_RUNTIME_DIR AND EXISTS "${OPENSSL_RUNTIME_DIR}")
    set(probe_path "${OPENSSL_RUNTIME_DIR}${path_separator}${probe_path}")
endif()
set(test_args --test-dir "${PROBE_BUILD_DIR}" -R "^E2EProductionAdapterRuntime$" --output-on-failure)
if(DEFINED CONFIGURATION AND NOT "${CONFIGURATION}" STREQUAL "")
    list(APPEND test_args -C "${CONFIGURATION}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=${probe_path}"
        "QTNETWORKCHAT_E2E_CRYPTO_BACKEND=production"
        "${CMAKE_CTEST_COMMAND}" ${test_args}
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_stdout
    ERROR_VARIABLE test_stderr
)

file(REMOVE_RECURSE "${PROBE_BUILD_DIR}")

if(NOT test_result EQUAL 0)
    message(STATUS "Captured production adapter runtime test output: ${test_stdout}\n${test_stderr}")
    message(FATAL_ERROR "Production adapter runtime should pass OpenSSL provider dispatch under production backend selection")
endif()

message(STATUS "E2E production adapter runtime provider dispatch gate verified")
