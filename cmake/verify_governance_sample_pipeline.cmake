if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/governance_sample_pipeline")
file(MAKE_DIRECTORY "${TEMP_DIR}")

# Run the full governance sample pipeline
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -OutputDir "${TEMP_DIR}" -RunS3RequestAnalysis -RunGovernance
    RESULT_VARIABLE result1
    OUTPUT_VARIABLE output1
    ERROR_VARIABLE error1
)
if(NOT output1 STREQUAL "")
    message(STATUS "${output1}")
endif()
if(NOT error1 STREQUAL "")
    message(STATUS "${error1}")
endif()
if(NOT result1 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance sample pipeline failed with exit code ${result1}")
endif()

# Verify expected output files exist
set(EXPECTED_FILES
    "sample-route.log"
    "sample-route-summary.json"
    "sample-s3-request-summary.json"
    "sample-last-health.json"
    "governance/large-file-route-alert-summary.json"
    "governance/s3-request-results-alert-summary.json"
    "governance/receipt-rotation-alert-summary.json"
    "governance/governance-alert-overview.json"
    "governance-reconcile/reconcile.log"
    "governance-rotation-summary.json"
)

foreach(expected_file ${EXPECTED_FILES})
    set(full_path "${TEMP_DIR}/${expected_file}")
    if(NOT EXISTS "${full_path}")
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Expected output file not found: ${expected_file}")
    endif()
endforeach()

# Verify governance overview content
file(READ "${TEMP_DIR}/governance/governance-alert-overview.json" overview_content)
string(JSON overview_ok GET "${overview_content}" "ok")
string(JSON overview_count GET "${overview_content}" "alertCount")
if(NOT overview_count GREATER 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance overview alertCount should be > 0, got ${overview_count}")
endif()

# Verify health check output
file(READ "${TEMP_DIR}/sample-last-health.json" health_content)
string(JSON health_status GET "${health_content}" "status")
if(NOT health_status STREQUAL "healthy" AND NOT health_status STREQUAL "unhealthy")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Health status should be healthy or unhealthy, got ${health_status}")
endif()

# Verify no sensitive fields in any output
file(READ "${TEMP_DIR}/governance/governance-alert-overview.json" agg_content)
string(FIND "${agg_content}" "endpoint" s1)
string(FIND "${agg_content}" "access_key" s2)
string(FIND "${agg_content}" "secret_key" s3)
string(FIND "${agg_content}" "Authorization" s4)
string(FIND "${agg_content}" "Credential" s5)
if(NOT s1 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance overview contains 'endpoint'")
endif()
if(NOT s2 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance overview contains 'access_key'")
endif()
if(NOT s3 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance overview contains 'secret_key'")
endif()
if(NOT s4 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance overview contains 'Authorization'")
endif()
if(NOT s5 EQUAL -1)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Governance overview contains 'Credential'")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Governance sample pipeline test passed: exit=${result1} alertCount=${overview_count} healthStatus=${health_status}")
