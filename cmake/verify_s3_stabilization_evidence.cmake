if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/s3_stabilization_evidence_sample")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(S3_SUMMARY "${TEMP_DIR}/s3-summary.json")
set(ROUTE_SUMMARY "${TEMP_DIR}/route-summary.json")
set(EVIDENCE_OUT "${TEMP_DIR}/evidence.json")
set(EVIDENCE_MD "${TEMP_DIR}/evidence.md")
set(ALERT_OUT "${TEMP_DIR}/alert.json")

file(WRITE "${S3_SUMMARY}"
"{
  \"format\":\"qtnetworkchat-s3-request-results-v1\",
  \"reasonCounts\":{\"timeout\":1,\"network\":1,\"tls\":1,\"auth\":1,\"retryable\":1,\"server\":1,\"hash\":1,\"size\":1},
  \"operationCounts\":{\"PUT\":2,\"HEAD\":1,\"GET\":1,\"DELETE\":1,\"validate\":1,\"read\":1,\"remove\":1}
}
")
file(WRITE "${ROUTE_SUMMARY}"
"{
  \"format\":\"qtnetworkchat-large-file-route-summary-v1\",
  \"failedFallbackRetained\":2
}
")

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -S3SummaryPath "${S3_SUMMARY}"
        -RouteSummaryPath "${ROUTE_SUMMARY}"
        -OutputPath "${EVIDENCE_OUT}"
        -MarkdownPath "${EVIDENCE_MD}"
        -AlertSummaryPath "${ALERT_OUT}"
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
    message(FATAL_ERROR "write-s3-stabilization-evidence.ps1 exited with code ${result}")
endif()

foreach(expected_file "${EVIDENCE_OUT}" "${EVIDENCE_MD}" "${ALERT_OUT}")
    if(NOT EXISTS "${expected_file}")
        message(FATAL_ERROR "Expected evidence output missing: ${expected_file}")
    endif()
endforeach()

file(READ "${EVIDENCE_OUT}" evidence_content)
string(JSON evidence_format GET "${evidence_content}" "format")
string(JSON evidence_ok GET "${evidence_content}" "ok")
string(JSON fallback_signals GET "${evidence_content}" "fallbackSignals")
if(NOT evidence_format STREQUAL "qtnetworkchat-s3-stabilization-evidence-v1")
    message(FATAL_ERROR "Unexpected evidence format: ${evidence_format}")
endif()
if(NOT evidence_ok)
    message(FATAL_ERROR "Expected complete evidence ok=true")
endif()
if(NOT fallback_signals EQUAL 2)
    message(FATAL_ERROR "Expected fallbackSignals=2, got ${fallback_signals}")
endif()

file(READ "${ALERT_OUT}" alert_content)
string(JSON alert_kind GET "${alert_content}" "kind")
string(JSON alert_ok GET "${alert_content}" "ok")
if(NOT alert_kind STREQUAL "s3-stabilization-evidence")
    message(FATAL_ERROR "Unexpected alert kind: ${alert_kind}")
endif()
if(NOT alert_ok)
    message(FATAL_ERROR "Expected alert ok=true")
endif()

file(WRITE "${TEMP_DIR}/bad-summary.json" "{\"endpoint\":\"https://example.invalid/private\"}")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -S3SummaryPath "${TEMP_DIR}/bad-summary.json"
        -OutputPath "${TEMP_DIR}/bad-evidence.json"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    message(FATAL_ERROR "Evidence writer should reject sensitive input")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "S3 stabilization evidence test passed")
