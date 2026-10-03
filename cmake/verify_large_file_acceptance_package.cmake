if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/large_file_acceptance_package_sample")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(ROUTE_LOG "${TEMP_DIR}/route.log")
set(ROUTE_SUMMARY "${TEMP_DIR}/route-summary.json")
set(S3_SUMMARY "${TEMP_DIR}/s3-summary.json")
set(ROTATION_SUMMARY "${TEMP_DIR}/rotation-summary.json")
set(NOTES "${TEMP_DIR}/acceptance-notes.txt")
set(RECONCILE_DIR "${TEMP_DIR}/reconcile")
set(PACKAGE_DIR "${TEMP_DIR}/package-output")
set(PACKAGE_ZIP "${PACKAGE_DIR}/acceptance.zip")
set(BAD_NOTES "${TEMP_DIR}/bad-notes.txt")
file(MAKE_DIRECTORY "${RECONCILE_DIR}" "${PACKAGE_DIR}")

file(WRITE "${ROUTE_LOG}"
"2026-05-30T10:00:00 redis_large_file_route event=delivered result=published sourceInstanceId=source-a transferId=clean001 objectKey=clean001.bin receiverId=receiver001 fileHash=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa bytes=1024 storeType=s3 operation=publish reason=success\n"
"2026-05-30T10:00:01 redis_large_file_route event=delivered_cleanup result=cleaned sourceInstanceId=source-a transferId=clean001 objectKey=clean001.bin receiverId=receiver001 fileHash=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa bytes=1024 storeType=s3 operation=delete reason=success\n"
)

file(WRITE "${ROUTE_SUMMARY}"
"{\n"
"  \"routeLineCount\": 2,\n"
"  \"routeKeys\": 1,\n"
"  \"deliveredWithoutCleanup\": 0,\n"
"  \"failedWithoutFallback\": 0,\n"
"  \"sensitiveHits\": 0\n"
"}\n"
)

file(WRITE "${S3_SUMMARY}"
"{\n"
"  \"routeLineCount\": 2,\n"
"  \"s3LineCount\": 1,\n"
"  \"sensitiveHits\": 0,\n"
"  \"warnings\": []\n"
"}\n"
)

file(WRITE "${ROTATION_SUMMARY}"
"{\n"
"  \"totalRecords\": 3,\n"
"  \"retainedRecords\": 2,\n"
"  \"archivedRecords\": 1,\n"
"  \"sensitiveHits\": 0,\n"
"  \"dryRun\": false\n"
"}\n"
)

file(WRITE "${NOTES}"
"date: 2026-05-30\n"
"commit: sample\n"
"objectStore: s3\n"
"prefix: qtchat/manual-acceptance\n"
"success path: passed\n"
"fallback path: passed\n"
"log redaction: passed\n"
)

file(WRITE "${RECONCILE_DIR}/receipts.jsonl"
"{\"sourceInstanceId\":\"source-a\",\"transferId\":\"clean001\",\"receiverId\":\"receiver001\",\"objectKey\":\"clean001.bin\",\"fileHash\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"confirmedBytes\":1024}\n"
)
file(WRITE "${RECONCILE_DIR}/fallbacks.jsonl"
"{\"sourceInstanceId\":\"source-a\",\"transferId\":\"clean001\",\"receiverId\":\"receiver001\",\"objectKey\":\"clean001.bin\",\"fileHash\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"fileSize\":1024}\n"
)
file(WRITE "${RECONCILE_DIR}/reconcile.log"
"large file delivered reconciliation\n"
"decision rows\n"
"  clean001|clean001.bin|receiver001 result=cleaned reason=cleaned confirmedBytes=1024 fileSize=1024\n"
)
file(WRITE "${RECONCILE_DIR}/s3-analysis-summary.json"
"{\"routeLineCount\":2,\"s3LineCount\":1,\"sensitiveHits\":0,\"warnings\":[]}\n"
)

execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -RouteLogPath "${ROUTE_LOG}"
        -RouteSummaryPath "${ROUTE_SUMMARY}"
        -S3SummaryPath "${S3_SUMMARY}"
        -RotationSummaryPath "${ROTATION_SUMMARY}"
        -ReconcileDir "${RECONCILE_DIR}"
        -NotesPath "${NOTES}"
        -OutputDir "${PACKAGE_DIR}"
        -PackagePath "${PACKAGE_ZIP}"
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
    message(FATAL_ERROR "package-large-file-acceptance.ps1 exited with code ${result}")
endif()

if(NOT EXISTS "${PACKAGE_ZIP}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Acceptance package was not created: ${PACKAGE_ZIP}")
endif()

set(EXTRACT_DIR "${TEMP_DIR}/extracted")
file(MAKE_DIRECTORY "${EXTRACT_DIR}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xfz "${PACKAGE_ZIP}"
    WORKING_DIRECTORY "${EXTRACT_DIR}"
    RESULT_VARIABLE extract_result
    OUTPUT_VARIABLE extract_output
    ERROR_VARIABLE extract_error
)
if(NOT extract_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Failed to extract acceptance package: ${extract_error}")
endif()

set(MANIFEST "${EXTRACT_DIR}/manifest.json")
if(NOT EXISTS "${MANIFEST}")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Package manifest was not created")
endif()

file(READ "${MANIFEST}" manifest_content)
string(JSON sensitive_hits GET "${manifest_content}" "sensitiveHits")
if(NOT sensitive_hits EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Expected sensitiveHits=0, got ${sensitive_hits}")
endif()

file(WRITE "${BAD_NOTES}" "Authorization: should-not-be-packaged\n")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${SCRIPT_PATH}"
        -NotesPath "${BAD_NOTES}"
        -OutputDir "${TEMP_DIR}/bad-package"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)

if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Packaging should fail when sensitive fields are present")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Large file acceptance package test passed")
