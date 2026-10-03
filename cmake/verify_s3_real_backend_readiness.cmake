if(NOT DEFINED SCRIPT_PATH OR NOT EXISTS "${SCRIPT_PATH}")
    message(FATAL_ERROR "SCRIPT_PATH does not exist: ${SCRIPT_PATH}")
endif()

set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/s3_real_backend_readiness_sample")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

string(REPLACE "'" "''" SCRIPT_PATH_PS "${SCRIPT_PATH}")

set(DEFAULT_JSON "${TEMP_DIR}/default-readiness.json")
set(DEFAULT_MD "${TEMP_DIR}/default-readiness.md")
set(DEFAULT_WRAPPER "${TEMP_DIR}/run-default-readiness.ps1")
string(REPLACE "'" "''" DEFAULT_JSON_PS "${DEFAULT_JSON}")
string(REPLACE "'" "''" DEFAULT_MD_PS "${DEFAULT_MD}")
file(WRITE "${DEFAULT_WRAPPER}"
"$ErrorActionPreference = 'Stop'\n"
"& '${SCRIPT_PATH_PS}' -OutputPath '${DEFAULT_JSON_PS}' -MarkdownPath '${DEFAULT_MD_PS}' -FailOnSensitive\n"
"exit $LASTEXITCODE\n"
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "QTNETWORKCHAT_OBJECT_STORE="
        "QTNETWORKCHAT_OBJECT_S3_ENABLE="
        "QTNETWORKCHAT_OBJECT_S3_ENDPOINT="
        "QTNETWORKCHAT_OBJECT_S3_BUCKET="
        "QTNETWORKCHAT_OBJECT_S3_REGION="
        "QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY="
        "QTNETWORKCHAT_OBJECT_S3_SECRET_KEY="
        powershell -ExecutionPolicy Bypass -File "${DEFAULT_WRAPPER}"
    RESULT_VARIABLE default_result
    OUTPUT_VARIABLE default_output
    ERROR_VARIABLE default_error
)
if(NOT default_output STREQUAL "")
    message(STATUS "${default_output}")
endif()
if(NOT default_error STREQUAL "")
    message(STATUS "${default_error}")
endif()
if(NOT default_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Default S3 readiness run exited with code ${default_result}")
endif()
file(READ "${DEFAULT_JSON}" default_content)
string(JSON default_format GET "${default_content}" "format")
string(JSON default_ok GET "${default_content}" "ok")
string(JSON default_status GET "${default_content}" "status")
string(JSON default_configured GET "${default_content}" "configured")
string(JSON default_gate GET "${default_content}" "auditSummary" "releaseGate")
string(JSON default_ci GET "${default_content}" "auditSummary" "realBackendDefaultCI")
string(JSON default_ci_mode GET "${default_content}" "auditSummary" "defaultCTestMode")
string(JSON default_ci_gate GET "${default_content}" "auditSummary" "defaultCIReleaseGate")
if(NOT default_format STREQUAL "qtnetworkchat-s3-real-backend-readiness-v1")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected default readiness format: ${default_format}")
endif()
if(default_ok OR default_configured)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Default readiness should be not configured and ok=false")
endif()
if(NOT default_status STREQUAL "not-configured" OR NOT default_gate STREQUAL "await-s3-real-backend-config")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected default readiness status/gate")
endif()
if(default_ci)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "S3 real backend readiness must keep default CI disabled")
endif()
if(NOT default_ci_mode STREQUAL "readiness-and-redaction-only"
        OR NOT default_ci_gate STREQUAL "s3-real-backend-default-ci-not-requested")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected default CI mode/gate for not-configured readiness")
endif()

set(SMOKE_JSON "${TEMP_DIR}/smoke-summary.json")
set(EVIDENCE_JSON "${TEMP_DIR}/real-backend-evidence.json")
set(S3_SUMMARY_JSON "${TEMP_DIR}/s3-summary.json")
set(VERIFIED_JSON "${TEMP_DIR}/verified-readiness.json")
set(VERIFIED_MD "${TEMP_DIR}/verified-readiness.md")
set(VERIFIED_WRAPPER "${TEMP_DIR}/run-verified-readiness.ps1")
file(WRITE "${SMOKE_JSON}"
"{\n"
"  \"format\":\"qtnetworkchat-minio-s3-smoke-summary-v1\",\n"
"  \"ok\":true,\n"
"  \"operations\":{\"put\":true,\"head\":true,\"get\":true,\"delete\":true}\n"
"}\n"
)
file(WRITE "${EVIDENCE_JSON}"
"{\n"
"  \"format\":\"qtnetworkchat-s3-real-backend-evidence-v1\",\n"
"  \"ok\":true,\n"
"  \"metrics\":{\"s3LineCount\":5,\"successCount\":5,\"fixedFailureReasonCount\":0,\"sensitiveHits\":0}\n"
"}\n"
)
file(WRITE "${S3_SUMMARY_JSON}"
"{\n"
"  \"s3LineCount\":5,\n"
"  \"sensitiveHits\":0,\n"
"  \"reasonCounts\":{\"success\":5}\n"
"}\n"
)
string(REPLACE "'" "''" VERIFIED_JSON_PS "${VERIFIED_JSON}")
string(REPLACE "'" "''" VERIFIED_MD_PS "${VERIFIED_MD}")
string(REPLACE "'" "''" SMOKE_JSON_PS "${SMOKE_JSON}")
string(REPLACE "'" "''" EVIDENCE_JSON_PS "${EVIDENCE_JSON}")
string(REPLACE "'" "''" S3_SUMMARY_JSON_PS "${S3_SUMMARY_JSON}")
file(WRITE "${VERIFIED_WRAPPER}"
"$ErrorActionPreference = 'Stop'\n"
"& '${SCRIPT_PATH_PS}' -OutputPath '${VERIFIED_JSON_PS}' -MarkdownPath '${VERIFIED_MD_PS}' -SmokeSummaryPath '${SMOKE_JSON_PS}' -EvidencePath '${EVIDENCE_JSON_PS}' -S3SummaryPath '${S3_SUMMARY_JSON_PS}' -FailOnSensitive\n"
"exit $LASTEXITCODE\n"
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "QTNETWORKCHAT_OBJECT_STORE=s3"
        "QTNETWORKCHAT_OBJECT_S3_ENABLE=1"
        "QTNETWORKCHAT_OBJECT_S3_DEFAULT_CI=1"
        "QTNETWORKCHAT_OBJECT_S3_ENDPOINT=configured-endpoint"
        "QTNETWORKCHAT_OBJECT_S3_BUCKET=configured-bucket"
        "QTNETWORKCHAT_OBJECT_S3_REGION=configured-region"
        "QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY=configured-material"
        "QTNETWORKCHAT_OBJECT_S3_SECRET_KEY=configured-material"
        powershell -ExecutionPolicy Bypass -File "${VERIFIED_WRAPPER}"
    RESULT_VARIABLE verified_result
    OUTPUT_VARIABLE verified_output
    ERROR_VARIABLE verified_error
)
if(NOT verified_output STREQUAL "")
    message(STATUS "${verified_output}")
endif()
if(NOT verified_error STREQUAL "")
    message(STATUS "${verified_error}")
endif()
if(NOT verified_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Verified S3 readiness run exited with code ${verified_result}")
endif()
file(READ "${VERIFIED_JSON}" verified_content)
string(JSON verified_ok GET "${verified_content}" "ok")
string(JSON verified_status GET "${verified_content}" "status")
string(JSON verified_configured GET "${verified_content}" "configured")
string(JSON verified_gate GET "${verified_content}" "auditSummary" "releaseGate")
string(JSON verified_ci GET "${verified_content}" "auditSummary" "realBackendDefaultCI")
string(JSON verified_ci_mode GET "${verified_content}" "auditSummary" "defaultCTestMode")
string(JSON verified_ci_gate GET "${verified_content}" "auditSummary" "defaultCIReleaseGate")
string(JSON verified_s3_lines GET "${verified_content}" "evidence" "s3LineCount")
string(JSON verified_success GET "${verified_content}" "evidence" "successCount")
string(JSON verified_sensitive GET "${verified_content}" "evidence" "sensitiveHitCount")
if(NOT verified_ok OR NOT verified_configured)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Verified readiness should be configured and ok=true")
endif()
if(NOT verified_status STREQUAL "verified" OR NOT verified_gate STREQUAL "can-review-s3-real-backend-evidence")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected verified readiness status/gate")
endif()
if(NOT verified_ci
        OR NOT verified_ci_mode STREQUAL "real-backend-gated"
        OR NOT verified_ci_gate STREQUAL "s3-real-backend-default-ci-ready")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Verified readiness with explicit opt-in should enable real backend default CI gate")
endif()
if(NOT verified_s3_lines EQUAL 5 OR NOT verified_success EQUAL 5 OR NOT verified_sensitive EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Unexpected verified readiness metrics")
endif()

file(READ "${VERIFIED_MD}" verified_markdown)
foreach(expected_text
        "QtNetworkChat S3 Real Backend Readiness"
        "Status: `verified`"
        "Release gate: `can-review-s3-real-backend-evidence`"
        "Real backend default CI: `true`"
        "Default CI gate: `s3-real-backend-default-ci-ready`")
    string(FIND "${verified_markdown}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Verified readiness markdown missing expected text: ${expected_text}")
    endif()
endforeach()
foreach(forbidden_text
        "configured-endpoint"
        "configured-bucket"
        "configured-material")
    string(FIND "${verified_markdown}" "${forbidden_text}" forbidden_at)
    if(NOT forbidden_at EQUAL -1)
        file(REMOVE_RECURSE "${TEMP_DIR}")
        message(FATAL_ERROR "Verified readiness markdown leaked configured value: ${forbidden_text}")
    endif()
endforeach()

set(REQUESTED_NOT_READY_JSON "${TEMP_DIR}/requested-not-ready.json")
set(REQUESTED_NOT_READY_WRAPPER "${TEMP_DIR}/run-requested-not-ready.ps1")
string(REPLACE "'" "''" REQUESTED_NOT_READY_JSON_PS "${REQUESTED_NOT_READY_JSON}")
file(WRITE "${REQUESTED_NOT_READY_WRAPPER}"
"$ErrorActionPreference = 'Stop'\n"
"& '${SCRIPT_PATH_PS}' -OutputPath '${REQUESTED_NOT_READY_JSON_PS}' -FailOnSensitive\n"
"exit $LASTEXITCODE\n"
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "QTNETWORKCHAT_OBJECT_STORE=s3"
        "QTNETWORKCHAT_OBJECT_S3_ENABLE=1"
        "QTNETWORKCHAT_OBJECT_S3_DEFAULT_CI=1"
        "QTNETWORKCHAT_OBJECT_S3_ENDPOINT=configured-endpoint"
        "QTNETWORKCHAT_OBJECT_S3_BUCKET=configured-bucket"
        "QTNETWORKCHAT_OBJECT_S3_REGION=configured-region"
        "QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY=configured-material"
        "QTNETWORKCHAT_OBJECT_S3_SECRET_KEY=configured-material"
        powershell -ExecutionPolicy Bypass -File "${REQUESTED_NOT_READY_WRAPPER}"
    RESULT_VARIABLE requested_not_ready_result
    OUTPUT_VARIABLE requested_not_ready_output
    ERROR_VARIABLE requested_not_ready_error
)
if(NOT requested_not_ready_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Requested-not-ready S3 readiness should report blocked state without failing the readback")
endif()
file(READ "${REQUESTED_NOT_READY_JSON}" requested_not_ready_content)
string(JSON requested_not_ready_ok GET "${requested_not_ready_content}" "ok")
string(JSON requested_not_ready_ci GET "${requested_not_ready_content}" "auditSummary" "realBackendDefaultCI")
string(JSON requested_not_ready_ci_mode GET "${requested_not_ready_content}" "auditSummary" "defaultCTestMode")
string(JSON requested_not_ready_ci_gate GET "${requested_not_ready_content}" "auditSummary" "defaultCIReleaseGate")
if(requested_not_ready_ok
        OR requested_not_ready_ci
        OR NOT requested_not_ready_ci_mode STREQUAL "real-backend-requested-but-not-ready"
        OR NOT requested_not_ready_ci_gate STREQUAL "blocked-s3-real-backend-default-ci-not-ready")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Explicit default CI request without smoke/evidence must stay blocked")
endif()

set(BAD_EVIDENCE_JSON "${TEMP_DIR}/bad-evidence.json")
file(WRITE "${BAD_EVIDENCE_JSON}"
"{\n"
"  \"format\":\"qtnetworkchat-s3-real-backend-evidence-v1\",\n"
"  \"ok\":true,\n"
"  \"objectUrl\":\"https://example.invalid/private\",\n"
"  \"metrics\":{\"s3LineCount\":1,\"successCount\":1,\"fixedFailureReasonCount\":0,\"sensitiveHits\":0}\n"
"}\n"
)
set(BAD_WRAPPER "${TEMP_DIR}/run-bad-readiness.ps1")
set(BAD_READINESS_JSON "${TEMP_DIR}/bad-readiness.json")
string(REPLACE "'" "''" BAD_READINESS_JSON_PS "${BAD_READINESS_JSON}")
string(REPLACE "'" "''" BAD_EVIDENCE_JSON_PS "${BAD_EVIDENCE_JSON}")
file(WRITE "${BAD_WRAPPER}"
"$ErrorActionPreference = 'Stop'\n"
"& '${SCRIPT_PATH_PS}' -OutputPath '${BAD_READINESS_JSON_PS}' -EvidencePath '${BAD_EVIDENCE_JSON_PS}' -FailOnSensitive\n"
"exit $LASTEXITCODE\n"
)
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File "${BAD_WRAPPER}"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    file(REMOVE_RECURSE "${TEMP_DIR}")
    message(FATAL_ERROR "Readiness verifier should reject sensitive evidence")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
