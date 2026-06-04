cmake_minimum_required(VERSION 3.16)

project(QtNetworkChat VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ui)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/qtnetworkchat_version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/qtnetworkchat_version.h
    @ONLY
)

option(QTNETWORKCHAT_E2E_ENABLE_PRODUCTION_CRYPTO "Probe for a production E2E crypto backend without enabling the draft backend as production-ready" OFF)
set(QTNETWORKCHAT_E2E_COMPILED_BACKEND_ID "draft-qt-hmac-stream-v1")
set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REQUESTED 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_AVAILABLE 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REASON "production-backend-not-requested")
if(QTNETWORKCHAT_E2E_ENABLE_PRODUCTION_CRYPTO)
    set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REQUESTED 1)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REASON "openssl-detected-adapter-not-enabled")
    else()
        set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REASON "openssl-not-found")
    endif()
endif()
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/qtnetworkchat_e2e_crypto_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/qtnetworkchat_e2e_crypto_config.h
    @ONLY
)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Network Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Network Sql Test)

set(QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES
    src/tlssecurity.cpp
    src/filetransferstatus.cpp
    src/e2eenvelope.cpp
    include/tlssecurity.h
    include/filetransferstatus.h
    include/e2eenvelope.h
)

add_executable(QtNetworkChat
    src/main.cpp
    src/mainwindow.cpp
    src/historymetadata.cpp
    src/server.cpp
    src/client.cpp
    src/redisclient.cpp
    src/message.cpp
    src/objectstore.cpp
    src/heartbeatmonitor.cpp
    src/logincredentialstore.cpp
    ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
    include/mainwindow.h
    include/historymetadata.h
    include/server.h
    include/client.h
    include/redisclient.h
    include/message.h
    include/objectstore.h
    include/chatuser.h
    include/heartbeatmonitor.h
    include/logincredentialstore.h
    ui/mainwindow.ui
)

target_include_directories(QtNetworkChat PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated include)
target_link_libraries(QtNetworkChat PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

add_executable(sqlite_to_postgres_migrator
    tools/sqlite_to_postgres_migrator.cpp
)
target_link_libraries(sqlite_to_postgres_migrator PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Sql)

include(CTest)
if(BUILD_TESTING)
    function(qtnetworkchat_target_include_dirs target_name)
        target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated include)
    endfunction()

    add_executable(message_serialization_test
        tests/message_serialization_test.cpp
        src/message.cpp
        src/e2eenvelope.cpp
        include/message.h
        include/e2eenvelope.h
    )
    qtnetworkchat_target_include_dirs(message_serialization_test)
    target_link_libraries(message_serialization_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(e2e_envelope_test
        tests/e2e_envelope_test.cpp
        src/e2eenvelope.cpp
        include/e2eenvelope.h
    )
    qtnetworkchat_target_include_dirs(e2e_envelope_test)
    target_link_libraries(e2e_envelope_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(e2e_private_message_delivery_test
        tests/e2e_private_message_delivery_test.cpp
        src/server.cpp
        src/client.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        src/redisclient.cpp
        src/message.cpp
        src/objectstore.cpp
        src/heartbeatmonitor.cpp
        include/server.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/objectstore.h
        include/chatuser.h
        include/heartbeatmonitor.h
    )
    qtnetworkchat_target_include_dirs(e2e_private_message_delivery_test)
    target_link_libraries(e2e_private_message_delivery_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(history_metadata_test
        tests/history_metadata_test.cpp
        src/historymetadata.cpp
        include/historymetadata.h
    )
    target_include_directories(history_metadata_test PRIVATE include)
    target_link_libraries(history_metadata_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(server_group_member_update_test
        tests/server_group_member_update_test.cpp
        src/server.cpp
        src/client.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        src/redisclient.cpp
        src/message.cpp
        src/objectstore.cpp
        src/heartbeatmonitor.cpp
        include/server.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/objectstore.h
        include/chatuser.h
        include/heartbeatmonitor.h
    )
    qtnetworkchat_target_include_dirs(server_group_member_update_test)
    target_link_libraries(server_group_member_update_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(account_password_kdf_test
        tests/account_password_kdf_test.cpp
        src/server.cpp
        src/client.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        src/redisclient.cpp
        src/message.cpp
        src/objectstore.cpp
        src/heartbeatmonitor.cpp
        include/server.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/objectstore.h
        include/chatuser.h
        include/heartbeatmonitor.h
    )
    qtnetworkchat_target_include_dirs(account_password_kdf_test)
    target_link_libraries(account_password_kdf_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(login_credential_store_test
        tests/login_credential_store_test.cpp
        src/logincredentialstore.cpp
        include/logincredentialstore.h
    )
    target_include_directories(login_credential_store_test PRIVATE include)
    target_link_libraries(login_credential_store_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(tls_security_test
        tests/tls_security_test.cpp
        src/tlssecurity.cpp
        include/tlssecurity.h
    )
    target_include_directories(tls_security_test PRIVATE include)
    target_link_libraries(tls_security_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)

    add_executable(file_transfer_status_test
        tests/file_transfer_status_test.cpp
        src/filetransferstatus.cpp
        include/filetransferstatus.h
    )
    target_include_directories(file_transfer_status_test PRIVATE include)
    target_link_libraries(file_transfer_status_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(file_transfer_cancel_test
        tests/file_transfer_cancel_test.cpp
        src/server.cpp
        src/client.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        src/redisclient.cpp
        src/message.cpp
        src/objectstore.cpp
        src/heartbeatmonitor.cpp
        include/server.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/objectstore.h
        include/chatuser.h
        include/heartbeatmonitor.h
    )
    qtnetworkchat_target_include_dirs(file_transfer_cancel_test)
    target_link_libraries(file_transfer_cancel_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(file_chunk_duplicate_ack_test
        tests/file_chunk_duplicate_ack_test.cpp
        src/server.cpp
        src/client.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        src/redisclient.cpp
        src/message.cpp
        src/objectstore.cpp
        src/heartbeatmonitor.cpp
        include/server.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/objectstore.h
        include/chatuser.h
        include/heartbeatmonitor.h
    )
    qtnetworkchat_target_include_dirs(file_chunk_duplicate_ack_test)
    target_link_libraries(file_chunk_duplicate_ack_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(file_chunk_retry_progress_test
        tests/file_chunk_retry_progress_test.cpp
        src/client.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        src/message.cpp
        include/client.h
        include/message.h
        include/chatuser.h
    )
    qtnetworkchat_target_include_dirs(file_chunk_retry_progress_test)
    target_link_libraries(file_chunk_retry_progress_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)

    add_executable(offline_attachment_quota_test
        tests/offline_attachment_quota_test.cpp
        src/server.cpp
        src/client.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        src/redisclient.cpp
        src/message.cpp
        src/objectstore.cpp
        src/heartbeatmonitor.cpp
        include/server.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/objectstore.h
        include/chatuser.h
        include/heartbeatmonitor.h
    )
    qtnetworkchat_target_include_dirs(offline_attachment_quota_test)
    target_link_libraries(offline_attachment_quota_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(redis_client_test
        tests/redis_client_test.cpp
        src/redisclient.cpp
        include/redisclient.h
    )
    target_include_directories(redis_client_test PRIVATE include)
    target_link_libraries(redis_client_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)

    add_executable(objectstore_test
        tests/objectstore_test.cpp
        src/objectstore.cpp
        include/objectstore.h
    )
    target_include_directories(objectstore_test PRIVATE include)
    target_link_libraries(objectstore_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)

    add_executable(redis_unavailable_fallback_test
        tests/redis_unavailable_fallback_test.cpp
        src/server.cpp
        src/client.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        src/redisclient.cpp
        src/message.cpp
        src/objectstore.cpp
        src/heartbeatmonitor.cpp
        include/server.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/objectstore.h
        include/chatuser.h
        include/heartbeatmonitor.h
    )
    qtnetworkchat_target_include_dirs(redis_unavailable_fallback_test)
    target_link_libraries(redis_unavailable_fallback_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(database_health_test
        tests/database_health_test.cpp
        src/server.cpp
        src/client.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        src/redisclient.cpp
        src/message.cpp
        src/objectstore.cpp
        src/heartbeatmonitor.cpp
        include/server.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/objectstore.h
        include/chatuser.h
        include/heartbeatmonitor.h
    )
    qtnetworkchat_target_include_dirs(database_health_test)
    target_link_libraries(database_health_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(postgres_qpsql_protocol_smoke_test
        tests/postgres_qpsql_protocol_smoke_test.cpp
        src/server.cpp
        src/client.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        src/redisclient.cpp
        src/message.cpp
        src/objectstore.cpp
        src/heartbeatmonitor.cpp
        include/server.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/objectstore.h
        include/chatuser.h
        include/heartbeatmonitor.h
    )
    qtnetworkchat_target_include_dirs(postgres_qpsql_protocol_smoke_test)
    target_link_libraries(postgres_qpsql_protocol_smoke_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(redis_presence_flow_test
        tests/redis_presence_flow_test.cpp
        src/redisclient.cpp
        include/redisclient.h
    )
    target_include_directories(redis_presence_flow_test PRIVATE include)
    target_link_libraries(redis_presence_flow_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)

    add_executable(redis_cross_instance_chat_test
        tests/redis_cross_instance_chat_test.cpp
        src/server.cpp
        src/client.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        src/redisclient.cpp
        src/message.cpp
        src/objectstore.cpp
        src/heartbeatmonitor.cpp
        include/server.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/objectstore.h
        include/chatuser.h
        include/heartbeatmonitor.h
    )
    qtnetworkchat_target_include_dirs(redis_cross_instance_chat_test)
    target_link_libraries(redis_cross_instance_chat_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(heartbeat_monitor_test
        tests/heartbeat_monitor_test.cpp
        src/heartbeatmonitor.cpp
        include/heartbeatmonitor.h
    )
    target_include_directories(heartbeat_monitor_test PRIVATE include)
    target_link_libraries(heartbeat_monitor_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Test)

    add_test(
        NAME QtNetworkChatExecutableExists
        COMMAND ${CMAKE_COMMAND}
            -DQTNETWORKCHAT_EXE=$<TARGET_FILE:QtNetworkChat>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_executable.cmake
    )
    add_test(
        NAME MessageSerializationRoundTrip
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:message_serialization_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME E2EEnvelopeProtocol
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:e2e_envelope_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME E2ECryptoBackendConfig
        COMMAND ${CMAKE_COMMAND}
            -DCONFIG_HEADER=${CMAKE_CURRENT_BINARY_DIR}/generated/qtnetworkchat_e2e_crypto_config.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_e2e_crypto_backend_config.cmake
    )
    add_test(
        NAME E2EPrivateMessageDelivery
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:e2e_private_message_delivery_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME HistoryEncryptionMetadata
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:history_metadata_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME ServerGroupMemberUpdateProtocol
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:server_group_member_update_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME AccountPasswordKdfMigration
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:account_password_kdf_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME LoginCredentialStoreSafety
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:login_credential_store_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME TlsSecurityPinning
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:tls_security_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME FileTransferUserStatus
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:file_transfer_status_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME FileTransferCancelProtocol
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:file_transfer_cancel_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME FileChunkDuplicateAckProtocol
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:file_chunk_duplicate_ack_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME FileChunkRetryProgress
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:file_chunk_retry_progress_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME OfflineAttachmentQuota
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:offline_attachment_quota_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME RedisClientRespProtocol
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:redis_client_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME ObjectStoreFilesystem
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:objectstore_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME RedisUnavailableFallback
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:redis_unavailable_fallback_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME DatabaseHealthSnapshot
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:database_health_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME PostgresQpsqlProtocolSmoke
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:postgres_qpsql_protocol_smoke_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME RedisPresenceCommandFlow
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:redis_presence_flow_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME RedisCrossInstanceChatRouting
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:redis_cross_instance_chat_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME HeartbeatMonitorTest
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:heartbeat_monitor_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME S3RequestResultAnalysis
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/analyze-s3-request-results.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_s3_result_analysis.cmake
    )
    add_test(
        NAME S3FailureBatchSample
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-s3-failure-batch-sample.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_s3_failure_batch_sample.cmake
    )
    add_test(
        NAME S3RealBackendEvidence
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-s3-real-backend-evidence.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_s3_real_backend_evidence.cmake
    )
    add_test(
        NAME S3StabilityRunbook
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-s3-stability-runbook.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_s3_stability_runbook.cmake
    )
    add_test(
        NAME S3StabilizationEvidence
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-s3-stabilization-evidence.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_s3_stabilization_evidence.cmake
    )
    add_test(
        NAME RouteLogAnalysis
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/analyze-large-file-route-logs.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_route_log_analysis.cmake
    )
    add_test(
        NAME RouteSummaryAlerts
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/analyze-large-file-route-summary.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_route_summary_alerts.cmake
    )
    add_test(
        NAME ReceiptRotationAlerts
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/analyze-large-file-receipt-rotation.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_receipt_rotation_alerts.cmake
    )
    add_test(
        NAME ReconcileRunnerS3Analysis
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-large-file-delivery-reconcile.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_reconcile_runner_s3_analysis.cmake
    )
    add_test(
        NAME LargeFileAcceptancePackage
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/package-large-file-acceptance.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_large_file_acceptance_package.cmake
    )
    add_test(
        NAME LargeFileGovernanceRunner
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-large-file-governance.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_large_file_governance_runner.cmake
    )
    add_test(
        NAME LargeFileGovernanceTask
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/register-large-file-governance-task.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_large_file_governance_task.cmake
    )
    add_test(
        NAME AggregateGovernanceAlerts
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/aggregate-governance-alerts.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_aggregate_governance_alerts.cmake
    )
    add_test(
        NAME GovernanceHealthCheck
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-governance-health.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_governance_health_check.cmake
    )
    add_test(
        NAME GovernanceSamplePipeline
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-large-file-reconcile-sample.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_governance_sample_pipeline.cmake
    )
    add_test(
        NAME NotifyGovernanceUnhealthy
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/notify-governance-unhealthy.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_notify_governance_unhealthy.cmake
    )
    add_test(
        NAME GovernanceDiagnosticsPackage
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/package-governance-diagnostics.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_governance_diagnostics_package.cmake
    )
    add_test(
        NAME LargeFileGovernanceReport
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-large-file-governance-report.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_large_file_governance_report.cmake
    )
    add_test(
        NAME LargeFileGovernanceDashboard
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-large-file-governance-dashboard.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_large_file_governance_dashboard.cmake
    )
    add_test(
        NAME LargeFileGovernanceStatus
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/show-large-file-governance-status.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_large_file_governance_status.cmake
    )
    add_test(
        NAME WindowsPackageManifest
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/package-windows.ps1
            -DREPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_windows_package_manifest.cmake
    )
    add_test(
        NAME LocalInfraConfig
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-local-infra.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_local_infra_config.cmake
    )
    add_test(
        NAME AutomationStatus
        COMMAND powershell -ExecutionPolicy Bypass -File
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-automation-status.ps1
            -ScriptPath ${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-automation-status.ps1
    )
    add_test(
        NAME AutomationTaskHistory
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-automation-task-history.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_automation_task_history.cmake
    )
    add_test(
        NAME AutomationTaskAck
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-automation-task-ack.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_automation_task_ack.cmake
    )
    add_test(
        NAME DatabaseHealthCheckPlan
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-database-health.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_database_health_check.cmake
    )
    add_test(
        NAME DatabaseHealthStatus
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/show-database-health-status.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_database_health_status.cmake
    )
    add_test(
        NAME DatabaseHealthTask
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/register-database-health-task.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_database_health_task.cmake
    )
    add_test(
        NAME PostgresReleaseAcceptanceTask
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/register-pgsql-release-acceptance-task.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_pgsql_release_acceptance_task.cmake
    )
    add_test(
        NAME PostgresReleaseEvidencePackage
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/package-pgsql-release-evidence.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_pgsql_release_evidence_package.cmake
    )
    add_test(
        NAME DatabaseHealthDashboard
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-database-health-dashboard.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_database_health_dashboard.cmake
    )
    add_test(
        NAME DatabaseHealthAlert
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/notify-database-health-unhealthy.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_database_health_alert.cmake
    )
    add_test(
        NAME PostgresProtocolSmokePlan
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-pgsql-protocol-smoke.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_pgsql_protocol_smoke.cmake
    )
    add_test(
        NAME LocalPostgresStartPlan
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/start-local-postgres.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_local_postgres_start.cmake
    )
    add_test(
        NAME SQLiteToPostgresMigrationPlan
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/migrate-sqlite-to-postgres.ps1
            -DMIGRATOR_EXE=$<TARGET_FILE:sqlite_to_postgres_migrator>
            -DQT_ROOT=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>/..
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_sqlite_pg_migration.cmake
    )
    add_test(
        NAME PostgresReleaseAcceptance
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-pgsql-release-acceptance.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_pgsql_release_acceptance.cmake
    )
    add_test(
        NAME PostgresRollbackLiveEvidence
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-pgsql-rollback-live-evidence.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_pgsql_rollback_live_evidence.cmake
    )
endif()
