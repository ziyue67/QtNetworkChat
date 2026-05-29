if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/receipt_rotation_alerts_sample")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(SUMMARY_IN "${TEMP_DIR}/rotation_summary.json")
set(SUMMARY_IN2 "${TEMP_DIR}/rotation_summary2.json")
set(SUMMARY_WARN "${TEMP_DIR}/rotation_summary_warn.json")

# Clean summary: no built-in warnings
file(WRITE "${SUMMARY_IN}"
"{\n"
"  \"receiptPath\": \"C:/data/receipts.jsonl\",\n"
"  \"archivePath\": \"C:/data/archive/old.jsonl.gz\",\n"
"  \"dryRun\": true,\n"
"  \"totalRecords\": 100,\n"
"  \"retainedRecords\": 50,\n"
"  \"archivedRecords\": 50,\n"
"  \"keepRecords\": 50,\n"
"  \"maxAgeDays\": 14,\n"
"  \"compressArchive\": true,\n"
"  \"sensitiveHits\": 0\n"
"}\n"
)

# Clean summary: no built-in warnings
file(WRITE "${SUMMARY_IN2}"
"{\n"
"  \"receiptPath\": \"C:/data/receipts2.jsonl\",\n"
"  \"archivePath\": \"C:/data/archive/old2.jsonl.gz\",\n"
"  \"dryRun\": false,\n"
"  \"totalRecords\": 200,\n"
"  \"retainedRecords\": 100,\n"
"  \"archivedRecords\": 100,\n"
"  \"keepRecords\": 100,\n"
"  \"maxAgeDays\": 7,\n"
"  \"compressArchive\": false,\n"
"  \"sensitiveHits\": 0\n"
"}\n"
)

# Warning summary: triggers built-in "archivedRecords>0 but archivePath empty" warning
file(WRITE "${SUMMARY_WARN}"
"{\n"
"  \"receiptPath\": \"C:/data/receipts3.jsonl\",\n"
"  \"archivePath\": \"\",\n"
"  \"dryRun\": false,\n"
"  \"totalRecords\": 50,\n"
"  \"retainedRecords\": 0,\n"
"  \"archivedRecords\": 50,\n"
"  \"keepRecords\": 0,\n"
"  \"maxAgeDays\": 14,\n"
"  \"compressArchive\": false,\n"
"  \"sensitiveHits\": 0\n"
"}\n"
)

# Test 1: no warnings (thresholds high enough, no built-in warnings)
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -Command
        "& '${SCRIPT_PATH}' -SummaryPath '${SUMMARY_IN}','${SUMMARY_IN2}' -WarnArchivedRecords 999 -WarnRetainedRecords 999"
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
    message(FATAL_ERROR "Receipt rotation alerts (no-warning case) exited with code ${result}")
endif()

# Test 2: warnings triggered with NoFailOnWarning (built-in warning from warn file)
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -Command
        "& '${SCRIPT_PATH}' -SummaryPath '${SUMMARY_IN}','${SUMMARY_WARN}' -WarnArchivedRecords 999 -WarnRetainedRecords 999 -NoFailOnWarning"
    RESULT_VARIABLE result2
    OUTPUT_VARIABLE output2
    ERROR_VARIABLE error_output2
)

if(NOT output2 STREQUAL "")
    message(STATUS "${output2}")
endif()
if(NOT error_output2 STREQUAL "")
    message(STATUS "${error_output2}")
endif()

if(NOT result2 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Receipt rotation alerts (warning-tolerant case) exited with code ${result2}")
endif()

# Test 3: warnings triggered without NoFailOnWarning should fail
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -Command
        "& '${SCRIPT_PATH}' -SummaryPath '${SUMMARY_IN}','${SUMMARY_WARN}' -WarnArchivedRecords 999 -WarnRetainedRecords 999"
    RESULT_VARIABLE result3
    OUTPUT_VARIABLE output3
    ERROR_VARIABLE error_output3
)

if(result3 EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Receipt rotation alerts should have failed when warnings found without NoFailOnWarning")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Receipt rotation alerts test passed: no-warning=${result} warning-tolerant=${result2} warning-strict=${result3}")
