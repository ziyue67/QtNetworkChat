cmake_minimum_required(VERSION 3.16)

project(QtNetworkChat VERSION 1.1.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ui)

if(MSVC)
    add_compile_options(/utf-8 /D_ITERATOR_DEBUG_LEVEL=0 /FI "${CMAKE_CURRENT_SOURCE_DIR}/include/msvc_compat.h")
    add_compile_definitions(_ALLOW_ITERATOR_DEBUG_LEVEL=0)
endif()

set(QTNETWORKCHAT_DEFAULT_SERVER_HOST "" CACHE STRING "Compile-time default chat server host; empty means local mode")
set(QTNETWORKCHAT_DEFAULT_SERVER_PORT "8888" CACHE STRING "Compile-time default chat server port")
set(QTNETWORKCHAT_DEFAULT_TRANSPORT "tcp" CACHE STRING "Compile-time client transport: tcp, tls, ws, or wss")
set_property(CACHE QTNETWORKCHAT_DEFAULT_TRANSPORT PROPERTY STRINGS tcp tls ws wss)
set(QTNETWORKCHAT_DEFAULT_WEBSOCKET_PATH "/ws" CACHE STRING "Compile-time WebSocket request path")
option(QTNETWORKCHAT_DEFAULT_TLS "Use TLS by default for client connections" OFF)
option(QTNETWORKCHAT_DEFAULT_TLS_VERIFY "Verify the server certificate by default" ON)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/qtnetworkchat_version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/qtnetworkchat_version.h
    @ONLY
)

option(QTNETWORKCHAT_E2E_ENABLE_PRODUCTION_CRYPTO "Enable discovery and selection of the production E2E crypto backend" OFF)
option(QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER "Link the OpenSSL production E2E crypto adapter" OFF)
set(QTNETWORKCHAT_E2E_PRODUCTION_CRYPTO_BACKEND "openssl" CACHE STRING "Production E2E crypto backend to select when explicitly enabled")
set_property(CACHE QTNETWORKCHAT_E2E_PRODUCTION_CRYPTO_BACKEND PROPERTY STRINGS openssl)
set(QTNETWORKCHAT_E2E_COMPILED_BACKEND_ID "draft-qt-hmac-stream-v1")
set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_ID "openssl-reviewed-adapter-v1")
set(QTNETWORKCHAT_E2E_REQUESTED_PRODUCTION_BACKEND "none")
set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REQUESTED 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_AVAILABLE 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_REQUESTED 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_SESSION_KEY_GENERATION 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_IDENTITY_KEY_GENERATION 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PUBLIC_KEY_DERIVATION 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_AGREEMENT_SIGN 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_AGREEMENT_VERIFY 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_SESSION_DERIVE 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PAYLOAD_ENCRYPT 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PAYLOAD_DECRYPT 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_OPENSSL_RUNTIME_DIR "")
if(EXISTS "D:/Qt/Tools/mingw1310_64/opt/bin")
    set(QTNETWORKCHAT_E2E_PRODUCTION_OPENSSL_RUNTIME_DIR "D:/Qt/Tools/mingw1310_64/opt/bin")
endif()
set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REASON "production-backend-not-requested")
set(QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_REASON "production-adapter-not-requested")
set(QTNETWORKCHAT_E2E_PRODUCTION_REQUIRED_OPERATIONS "session-key-generation,identity-key-generation,public-key-derivation,agreement-sign,agreement-verify,session-derive,payload-encrypt,payload-decrypt")
set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_ABI "qtnetworkchat-e2e-provider-table-v1")
set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_API_HEADER "include/qtnetworkchat_e2e_provider_api.h")
set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_REQUIRED_SYMBOLS "qnc_e2e_op_session_key_generation_v1,qnc_e2e_op_identity_key_generation_v1,qnc_e2e_op_public_key_derivation_v1,qnc_e2e_op_agreement_sign_v1,qnc_e2e_op_agreement_verify_v1,qnc_e2e_op_session_derive_v1,qnc_e2e_op_payload_encrypt_v1,qnc_e2e_op_payload_decrypt_v1")
set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_BOUND 0)
set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_REASON "production-provider-table-not-requested")
if(QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER)
    set(QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_REQUESTED 1)
    set(QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_LINKED 1)
    if(NOT OPENSSL_ROOT_DIR AND EXISTS "D:/Qt/Tools/mingw1310_64/opt/include/openssl/rand.h")
        set(OPENSSL_ROOT_DIR "D:/Qt/Tools/mingw1310_64/opt")
    endif()
    if(OPENSSL_ROOT_DIR)
        set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")
        if(EXISTS "${OPENSSL_ROOT_DIR}/bin")
            set(QTNETWORKCHAT_E2E_PRODUCTION_OPENSSL_RUNTIME_DIR "${OPENSSL_ROOT_DIR}/bin")
        endif()
        if(EXISTS "${OPENSSL_ROOT_DIR}/lib/libcrypto.dll.a")
            set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libcrypto.dll.a")
        endif()
        if(EXISTS "${OPENSSL_ROOT_DIR}/lib/libssl.dll.a")
            set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libssl.dll.a")
        endif()
    endif()
    set(QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_REASON "production-adapter-linked-placeholder")
    set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_REASON "production-provider-table-linked-placeholder")
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_SESSION_KEY_GENERATION 1)
        set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_IDENTITY_KEY_GENERATION 1)
        set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PUBLIC_KEY_DERIVATION 1)
        set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_AGREEMENT_SIGN 1)
        set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_AGREEMENT_VERIFY 1)
        set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_SESSION_DERIVE 1)
        set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PAYLOAD_ENCRYPT 1)
        set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_PAYLOAD_DECRYPT 1)
        set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_BOUND 1)
        set(QTNETWORKCHAT_E2E_PRODUCTION_ADAPTER_REASON "production-adapter-linked-reviewed-operations")
        set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_REASON "production-provider-table-reviewed-operations-bound")
    endif()
endif()
if(QTNETWORKCHAT_E2E_ENABLE_PRODUCTION_CRYPTO)
    set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REQUESTED 1)
    string(TOLOWER "${QTNETWORKCHAT_E2E_PRODUCTION_CRYPTO_BACKEND}" QTNETWORKCHAT_E2E_REQUESTED_PRODUCTION_BACKEND)
    if(QTNETWORKCHAT_E2E_REQUESTED_PRODUCTION_BACKEND STREQUAL "openssl")
        find_package(OpenSSL QUIET)
        if(OpenSSL_FOUND)
            if(QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER
               AND QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_BOUND)
                set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_AVAILABLE 1)
                set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REASON "openssl-reviewed-adapter-available")
            else()
                set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REASON "openssl-detected-adapter-not-enabled")
                set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_REASON "openssl-detected-provider-table-not-bound")
            endif()
        else()
            set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REASON "openssl-not-found")
            if(NOT QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER)
                set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_REASON "openssl-not-found")
            endif()
        endif()
    else()
        set(QTNETWORKCHAT_E2E_PRODUCTION_BACKEND_REASON "unsupported-production-backend-requested")
        set(QTNETWORKCHAT_E2E_PRODUCTION_PROVIDER_TABLE_REASON "unsupported-production-backend-requested")
    endif()
endif()
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/qtnetworkchat_e2e_crypto_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/qtnetworkchat_e2e_crypto_config.h
    @ONLY
)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Network Sql Multimedia WebSockets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Network Sql Multimedia WebSockets Test)

set(QTNETWORKCHAT_SERVER_SOURCES
    src/server.cpp
    src/server_transport.cpp
    src/server_database.cpp
    src/server_account.cpp
    src/server_offline_delivery.cpp
    src/server_file_transfer.cpp
    src/server_delivery_support.cpp
    src/server_friend.cpp
    src/server_group.cpp
)

set(QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES
    src/tlssecurity.cpp
    src/filetransferstatus.cpp
    src/historyattachmentparser.cpp
    src/chatcontextmanager.cpp
    src/transfermanager.cpp
    src/localfilemanager.cpp
    src/chatsessionmanager.cpp
    src/composermanager.cpp
    src/windowstatemanager.cpp
    src/friendmanager.cpp
    src/notificationpanelmanager.cpp
    src/groupmanager.cpp
    src/e2eenvelope.cpp
    src/e2e_crypto_primitives.cpp
    src/e2e_provider_runtime.cpp
    src/e2e_envelope_codec.cpp
    include/tlssecurity.h
    include/filetransferstatus.h
    include/historyattachmentparser.h
    include/chatcontextmanager.h
    include/transfermanager.h
    include/localfilemanager.h
    include/chatsessionmanager.h
    include/composermanager.h
    include/windowstatemanager.h
    include/friendmanager.h
    include/notificationpanelmanager.h
    include/groupmanager.h
    include/e2eenvelope.h
    include/qtnetworkchat_e2e_provider_api.h
)


set(QTNETWORKCHAT_PRODUCTION_ADAPTER_SOURCES)
if(QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER AND OpenSSL_FOUND)
    list(APPEND QTNETWORKCHAT_PRODUCTION_ADAPTER_SOURCES src/e2e_openssl_provider.cpp)
endif()

function(qtnetworkchat_attach_production_adapter target_name)
    if(QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER AND OpenSSL_FOUND)
        target_sources(${target_name} PRIVATE ${QTNETWORKCHAT_PRODUCTION_ADAPTER_SOURCES})
        target_link_libraries(${target_name} PRIVATE OpenSSL::Crypto)
    endif()
endfunction()

add_executable(QtNetworkChat
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow_menu.cpp
    src/mainwindow_notifications.cpp
    src/mainwindow_group_members.cpp
    src/mainwindow_friends.cpp
    src/mainwindow_views.cpp
    src/mainwindow_group_panel.cpp
    src/group_info_panel_ui.cpp
    src/mainwindow_search.cpp
    src/chatbubbledelegate.cpp
    src/sessionitemdelegate.cpp
    src/iconhelper.cpp
    src/qqnt_redis_service.cpp
    src/qqnt_backend_service.cpp
    src/transferchatitemrenderer.cpp
    src/notificationpanelmanager.cpp
    src/clientstorage.cpp
    src/historyservice.cpp
    src/historymetadata.cpp
    ${QTNETWORKCHAT_SERVER_SOURCES}
    src/client.cpp
    src/redisclient.cpp
    src/message.cpp
    src/objectstore.cpp
    src/heartbeatmonitor.cpp
    src/logincredentialstore.cpp
    src/theme/thememanager.cpp
    src/theme/dialogstyle.cpp
    src/widgets/avatarlabel.cpp
    src/widgets/titlebar.cpp
    src/widgets/dialogtitlebar.cpp
    src/widgets/appnav.cpp
    src/widgets/composerwidget.cpp
    src/widgets/composerTextEdit.cpp
    src/widgets/contactcard.cpp
    src/widgets/contactlistwidget.cpp
    src/widgets/contactnoticepanel.cpp
    src/widgets/groupmembersidebar.cpp
    src/views/messagesview.cpp
    src/views/contactsview.cpp
    src/views/favoritesview.cpp
    src/views/settingsview.cpp
    src/views/profileview.cpp
    src/dialogs/addfrienddialog.cpp
    src/dialogs/creategroupdialog.cpp
    src/dialogs/globalsearchdialog.cpp
    src/dialogs/friendmanagerdialog.cpp
    src/dialogs/mutedurationdialog.cpp
    src/dialogs/groupnicknamedialog.cpp
    src/dialogs/memberprofilecard.cpp
    src/dialogs/essencepanel.cpp
    src/windows/imagepreviewwindow.cpp
    src/windows/forwardwindow.cpp
    src/windows/screenshotcapturewindow.cpp
    src/windows/noticefilterwindow.cpp
    src/windows/registerwindow.cpp
    src/windows/loginwindow.cpp
    ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
    ${QTNETWORKCHAT_PRODUCTION_ADAPTER_SOURCES}
    include/mainwindow.h
    include/chatbubbledelegate.h
    include/qqnt_log.h
    include/sessionitemdelegate.h
    include/iconhelper.h
    include/qqnt_redis_service.h
    include/qqnt_backend_service.h
    include/transferchatitemrenderer.h
    include/notificationpanelmanager.h
    include/clientstorage.h
    include/historyservice.h
    include/historymetadata.h
    include/server.h
    include/client.h
    include/redisclient.h
    include/message.h
    include/objectstore.h
    include/chatuser.h
    include/heartbeatmonitor.h
    include/logincredentialstore.h
    include/theme/thememanager.h
    include/theme/dialogstyle.h
    include/widgets/avatarlabel.h
    include/widgets/titlebar.h
    include/widgets/dialogtitlebar.h
    include/widgets/appnav.h
    include/widgets/composerwidget.h
    include/widgets/composerTextEdit.h
    include/widgets/contactcard.h
    include/widgets/contactlistwidget.h
    include/widgets/contactnoticepanel.h
    include/widgets/groupmembersidebar.h
    include/views/messagesview.h
    include/views/contactsview.h
    include/views/favoritesview.h
    include/views/settingsview.h
    include/views/profileview.h
    include/dialogs/addfrienddialog.h
    include/dialogs/creategroupdialog.h
    include/dialogs/globalsearchdialog.h
    include/dialogs/friendmanagerdialog.h
    include/dialogs/mutedurationdialog.h
    include/dialogs/groupnicknamedialog.h
    include/dialogs/memberprofilecard.h
    include/dialogs/essencepanel.h
    include/windows/imagepreviewwindow.h
    include/windows/forwardwindow.h
    include/windows/screenshotcapturewindow.h
    include/windows/noticefilterwindow.h
    include/windows/registerwindow.h
    include/windows/loginwindow.h
    ui/mainwindow.ui
)

target_include_directories(QtNetworkChat PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated include)
target_link_libraries(QtNetworkChat PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Multimedia Qt${QT_VERSION_MAJOR}::WebSockets)
if(QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER AND OpenSSL_FOUND)
    target_link_libraries(QtNetworkChat PRIVATE OpenSSL::Crypto)
endif()

set(QTNETWORKCHAT_QSQLITE_PLUGIN "$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>/../plugins/sqldrivers/qsqlite.dll")
if(WIN32)
    set(QTNETWORKCHAT_WINDEPLOYQT "$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>/windeployqt.exe")
    add_custom_command(TARGET QtNetworkChat POST_BUILD
        COMMAND "${QTNETWORKCHAT_WINDEPLOYQT}"
            --no-translations
            --compiler-runtime
            "$<TARGET_FILE:QtNetworkChat>"
        COMMENT "Deploying Qt runtime and compiler libraries"
        VERBATIM
    )
endif()
if(WIN32)
add_custom_command(TARGET QtNetworkChat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:QtNetworkChat>/sqldrivers"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${QTNETWORKCHAT_QSQLITE_PLUGIN}"
        "$<TARGET_FILE_DIR:QtNetworkChat>/sqldrivers/qsqlite.dll"
    COMMENT "Deploying Qt SQLite driver"
    VERBATIM
)
endif()
add_custom_command(TARGET QtNetworkChat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:QtNetworkChat>/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/style-qqnt.qss"
        "$<TARGET_FILE_DIR:QtNetworkChat>/ui/style-qqnt.qss"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/style-qqnt-dark.qss"
        "$<TARGET_FILE_DIR:QtNetworkChat>/ui/style-qqnt-dark.qss"
    COMMENT "Deploying QtNetworkChat stylesheets"
    VERBATIM
)




add_executable(sqlite_to_postgres_migrator
    tools/sqlite_to_postgres_migrator.cpp
)
target_link_libraries(sqlite_to_postgres_migrator PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Sql)

add_executable(qqnt_server
    tools/qqnt_server.cpp
    src/redisclient.cpp
    src/qqnt_redis_service.cpp
    ${QTNETWORKCHAT_SERVER_SOURCES}
    src/message.cpp
    src/e2eenvelope.cpp
    src/e2e_crypto_primitives.cpp
    src/e2e_provider_runtime.cpp
    src/e2e_envelope_codec.cpp
    src/objectstore.cpp
    src/heartbeatmonitor.cpp
    ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
    ${QTNETWORKCHAT_PRODUCTION_ADAPTER_SOURCES}
    include/server.h
    include/redisclient.h
    include/qqnt_redis_service.h
    include/message.h
    include/e2eenvelope.h
    include/objectstore.h
    include/chatuser.h
    include/heartbeatmonitor.h
)
target_include_directories(qqnt_server PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated include)
target_link_libraries(qqnt_server PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
qtnetworkchat_attach_production_adapter(qqnt_server)

add_executable(qqnt_engine
    tools/qqnt_engine.cpp
    src/qqnt_client_bridge.cpp
    src/qqnt_engine_command_router.cpp
    src/qqnt_redis_service.cpp
    src/qqnt_backend_service.cpp
    src/client.cpp
    src/redisclient.cpp
    ${QTNETWORKCHAT_SERVER_SOURCES}
    src/message.cpp
    src/e2eenvelope.cpp
    src/e2e_crypto_primitives.cpp
    src/e2e_provider_runtime.cpp
    src/e2e_envelope_codec.cpp
    src/objectstore.cpp
    src/heartbeatmonitor.cpp
    src/localfilemanager.cpp
    src/logincredentialstore.cpp
    src/clientstorage.cpp
    ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
    ${QTNETWORKCHAT_PRODUCTION_ADAPTER_SOURCES}
    include/qqnt_client_bridge.h
    include/qqnt_engine_command_router.h
    include/qqnt_redis_service.h
    include/qqnt_backend_service.h
    include/client.h
    include/redisclient.h
    include/server.h
    include/message.h
    include/e2eenvelope.h
    include/objectstore.h
    include/chatuser.h
    include/heartbeatmonitor.h
    include/localfilemanager.h
    include/logincredentialstore.h
    include/clientstorage.h
)
target_include_directories(qqnt_engine PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated include)
target_link_libraries(qqnt_engine PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::WebSockets)
qtnetworkchat_attach_production_adapter(qqnt_engine)

add_executable(e2e_rollout_observability_exporter
    tools/e2e_rollout_observability_exporter.cpp
    src/e2eenvelope.cpp
    src/e2e_crypto_primitives.cpp
    src/e2e_provider_runtime.cpp
    src/e2e_envelope_codec.cpp
    ${QTNETWORKCHAT_PRODUCTION_ADAPTER_SOURCES}
    include/e2eenvelope.h
    include/qtnetworkchat_e2e_provider_api.h
)
target_include_directories(e2e_rollout_observability_exporter PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated include)
target_link_libraries(e2e_rollout_observability_exporter PRIVATE Qt${QT_VERSION_MAJOR}::Core)
if(QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER AND OpenSSL_FOUND)
    target_link_libraries(e2e_rollout_observability_exporter PRIVATE OpenSSL::Crypto)
endif()

include(CTest)
if(BUILD_TESTING)
    function(qtnetworkchat_target_include_dirs target_name)
        target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated include)
        # Several protocol tests compile Client directly. Keeping WebSockets on
        # test targets avoids leaking the client-only runtime into qqnt_server.
        target_link_libraries(${target_name} PRIVATE Qt${QT_VERSION_MAJOR}::WebSockets)
    endfunction()

    set(QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES
        tests/test_redis_support.cpp
        tests/test_redis_support.h
        src/qqnt_redis_service.cpp
        include/qqnt_redis_service.h
    )

    add_executable(message_serialization_test
        tests/message_serialization_test.cpp
        src/message.cpp
        src/e2eenvelope.cpp
        src/e2e_crypto_primitives.cpp
        src/e2e_provider_runtime.cpp
        src/e2e_envelope_codec.cpp
        ${QTNETWORKCHAT_PRODUCTION_ADAPTER_SOURCES}
        include/message.h
        include/e2eenvelope.h
        include/qtnetworkchat_e2e_provider_api.h
    )
    qtnetworkchat_target_include_dirs(message_serialization_test)
    target_link_libraries(message_serialization_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
    if(QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER AND OpenSSL_FOUND)
        target_link_libraries(message_serialization_test PRIVATE OpenSSL::Crypto)
    endif()

    add_executable(e2e_envelope_test
        tests/e2e_envelope_test.cpp
        src/e2eenvelope.cpp
        src/e2e_crypto_primitives.cpp
        src/e2e_provider_runtime.cpp
        src/e2e_envelope_codec.cpp
        ${QTNETWORKCHAT_PRODUCTION_ADAPTER_SOURCES}
        include/e2eenvelope.h
        include/qtnetworkchat_e2e_provider_api.h
    )
    qtnetworkchat_target_include_dirs(e2e_envelope_test)
    target_link_libraries(e2e_envelope_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
    if(QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER AND OpenSSL_FOUND)
        target_link_libraries(e2e_envelope_test PRIVATE OpenSSL::Crypto)
    endif()


    add_executable(e2e_production_adapter_runtime_test
        tests/e2e_production_adapter_runtime_test.cpp
        src/e2eenvelope.cpp
        src/e2e_crypto_primitives.cpp
        src/e2e_provider_runtime.cpp
        src/e2e_envelope_codec.cpp
        ${QTNETWORKCHAT_PRODUCTION_ADAPTER_SOURCES}
        include/e2eenvelope.h
    )
    qtnetworkchat_target_include_dirs(e2e_production_adapter_runtime_test)
    target_link_libraries(e2e_production_adapter_runtime_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
    if(QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER AND OpenSSL_FOUND)
        target_link_libraries(e2e_production_adapter_runtime_test PRIVATE OpenSSL::Crypto)
    endif()

    add_executable(e2e_private_message_delivery_test
        tests/e2e_private_message_delivery_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
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
    target_link_libraries(e2e_private_message_delivery_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(e2e_private_message_delivery_test)

    add_executable(history_metadata_test
        tests/history_metadata_test.cpp
        src/historymetadata.cpp
        include/historymetadata.h
    )
    target_include_directories(history_metadata_test PRIVATE include)
    target_link_libraries(history_metadata_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(history_attachment_parser_test
        tests/history_attachment_parser_test.cpp
        src/historyattachmentparser.cpp
        src/localfilemanager.cpp
        include/historyattachmentparser.h
        include/localfilemanager.h
    )
    target_include_directories(history_attachment_parser_test PRIVATE include)
    target_link_libraries(history_attachment_parser_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets)

    add_executable(history_service_test
        tests/history_service_test.cpp
        src/historyservice.cpp
        src/historymetadata.cpp
        include/historyservice.h
        include/historymetadata.h
    )
    target_include_directories(history_service_test PRIVATE include)
    target_link_libraries(history_service_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(server_group_member_update_test
        tests/server_group_member_update_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
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
    target_link_libraries(server_group_member_update_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(server_group_member_update_test)

    add_executable(server_friend_persistence_test
        tests/server_friend_persistence_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
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
    qtnetworkchat_target_include_dirs(server_friend_persistence_test)
    target_link_libraries(server_friend_persistence_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(server_friend_persistence_test)

    add_executable(account_password_kdf_test
        tests/account_password_kdf_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
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
    target_link_libraries(account_password_kdf_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(account_password_kdf_test)

    add_executable(login_credential_store_test
        tests/login_credential_store_test.cpp
        src/logincredentialstore.cpp
        include/logincredentialstore.h
    )
    target_include_directories(login_credential_store_test PRIVATE include)
    target_link_libraries(login_credential_store_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(login_window_gui_test
        tests/login_window_gui_test.cpp
        src/windows/loginwindow.cpp
        src/theme/thememanager.cpp
        src/widgets/avatarlabel.cpp
        src/logincredentialstore.cpp
        include/windows/loginwindow.h
        include/theme/thememanager.h
        include/widgets/avatarlabel.h
        include/logincredentialstore.h
    )
    target_include_directories(login_window_gui_test PRIVATE include)
    target_link_libraries(login_window_gui_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Test)

    add_executable(group_info_panel_ui_test
        tests/group_info_panel_ui_test.cpp
        src/group_info_panel_ui.cpp
        src/theme/thememanager.cpp
        src/theme/dialogstyle.cpp
        src/widgets/avatarlabel.cpp
        src/group_info_panel_ui.h
        include/theme/thememanager.h
        include/widgets/avatarlabel.h
    )
    target_include_directories(group_info_panel_ui_test PRIVATE include src)
    target_link_libraries(group_info_panel_ui_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)

    add_executable(tls_security_test
        tests/tls_security_test.cpp
        src/tlssecurity.cpp
        include/tlssecurity.h
    )
    target_include_directories(tls_security_test PRIVATE include)
    target_link_libraries(tls_security_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)

    add_executable(websocket_transport_test
        tests/websocket_transport_test.cpp
        src/client.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        src/message.cpp
        src/objectstore.cpp
        include/client.h
        include/message.h
        include/objectstore.h
        include/chatuser.h
    )
    qtnetworkchat_target_include_dirs(websocket_transport_test)
    target_link_libraries(websocket_transport_test PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::WebSockets)
    qtnetworkchat_attach_production_adapter(websocket_transport_test)

    add_executable(file_transfer_status_test
        tests/file_transfer_status_test.cpp
        src/filetransferstatus.cpp
        include/filetransferstatus.h
    )
    target_include_directories(file_transfer_status_test PRIVATE include)
    target_link_libraries(file_transfer_status_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(transfer_manager_test
        tests/transfer_manager_test.cpp
        src/transfermanager.cpp
        src/localfilemanager.cpp
        src/filetransferstatus.cpp
        include/transfermanager.h
        include/localfilemanager.h
        include/filetransferstatus.h
    )
    target_include_directories(transfer_manager_test PRIVATE include)
    target_link_libraries(transfer_manager_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(transfer_chat_item_renderer_test
        tests/transfer_chat_item_renderer_test.cpp
        src/transferchatitemrenderer.cpp
        include/transferchatitemrenderer.h
        include/transfermanager.h
    )
    target_include_directories(transfer_chat_item_renderer_test PRIVATE include)
    target_link_libraries(transfer_chat_item_renderer_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets)

    add_executable(friend_manager_test
        tests/friend_manager_test.cpp
        src/friendmanager.cpp
        include/friendmanager.h
        include/chatuser.h
    )
    target_include_directories(friend_manager_test PRIVATE include)
    target_link_libraries(friend_manager_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)

    add_executable(local_file_manager_test
        tests/local_file_manager_test.cpp
        src/localfilemanager.cpp
        include/localfilemanager.h
    )
    target_include_directories(local_file_manager_test PRIVATE include)
    target_link_libraries(local_file_manager_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(chat_session_manager_test
        tests/chat_session_manager_test.cpp
        src/chatsessionmanager.cpp
        include/chatsessionmanager.h
    )
    target_include_directories(chat_session_manager_test PRIVATE include)
    target_link_libraries(chat_session_manager_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(chat_context_manager_test
        tests/chat_context_manager_test.cpp
        src/chatcontextmanager.cpp
        include/chatcontextmanager.h
    )
    target_include_directories(chat_context_manager_test PRIVATE include)
    target_link_libraries(chat_context_manager_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(notification_panel_manager_test
        tests/notification_panel_manager_test.cpp
        src/notificationpanelmanager.cpp
        include/notificationpanelmanager.h
    )
    target_include_directories(notification_panel_manager_test PRIVATE include)
    target_link_libraries(notification_panel_manager_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(composer_manager_test
        tests/composer_manager_test.cpp
        src/composermanager.cpp
        include/composermanager.h
    )
    target_include_directories(composer_manager_test PRIVATE include)
    target_link_libraries(composer_manager_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(window_state_manager_test
        tests/window_state_manager_test.cpp
        src/windowstatemanager.cpp
        include/windowstatemanager.h
    )
    target_include_directories(window_state_manager_test PRIVATE include)
    target_link_libraries(window_state_manager_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(group_manager_test
        tests/group_manager_test.cpp
        src/groupmanager.cpp
        include/groupmanager.h
    )
    target_include_directories(group_manager_test PRIVATE include)
    target_link_libraries(group_manager_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(screenshot_geometry_test
        tests/screenshot_geometry_test.cpp
        include/screenshotgeometry.h
    )
    target_include_directories(screenshot_geometry_test PRIVATE include)
    target_link_libraries(screenshot_geometry_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui)

    add_executable(session_list_builder_test
        tests/session_list_builder_test.cpp
        include/sessionlistbuilder.h
    )
    target_include_directories(session_list_builder_test PRIVATE include)
    target_link_libraries(session_list_builder_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(theme_qss_consistency_test
        tests/theme_qss_consistency_test.cpp
    )
    target_include_directories(theme_qss_consistency_test PRIVATE include)
    target_link_libraries(theme_qss_consistency_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(client_storage_test
        tests/client_storage_test.cpp
        src/clientstorage.cpp
        include/clientstorage.h
    )
    target_include_directories(client_storage_test PRIVATE include)
    target_link_libraries(client_storage_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)

    add_executable(file_transfer_cancel_test
        tests/file_transfer_cancel_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
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
    target_link_libraries(file_transfer_cancel_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(file_transfer_cancel_test)

    add_executable(public_image_transfer_test
        tests/public_image_transfer_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
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
    qtnetworkchat_target_include_dirs(public_image_transfer_test)
    target_link_libraries(public_image_transfer_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(public_image_transfer_test)

    add_executable(file_chunk_duplicate_ack_test
        tests/file_chunk_duplicate_ack_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
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
    target_link_libraries(file_chunk_duplicate_ack_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(file_chunk_duplicate_ack_test)

    add_executable(file_chunk_retry_progress_test
        tests/file_chunk_retry_progress_test.cpp
        src/client.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        src/message.cpp
        src/objectstore.cpp
        include/client.h
        include/message.h
        include/objectstore.h
        include/chatuser.h
    )
    qtnetworkchat_target_include_dirs(file_chunk_retry_progress_test)
    target_link_libraries(file_chunk_retry_progress_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)
    qtnetworkchat_attach_production_adapter(file_chunk_retry_progress_test)

    add_executable(offline_attachment_quota_test
        tests/offline_attachment_quota_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
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
    target_link_libraries(offline_attachment_quota_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(offline_attachment_quota_test)

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

    add_executable(redis_startup_requirement_test
        tests/redis_startup_requirement_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
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
    qtnetworkchat_target_include_dirs(redis_startup_requirement_test)
    target_link_libraries(redis_startup_requirement_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(redis_startup_requirement_test)

    add_executable(database_health_test
        tests/database_health_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
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
    target_link_libraries(database_health_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(database_health_test)

    add_executable(postgres_qpsql_protocol_smoke_test
        tests/postgres_qpsql_protocol_smoke_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
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
    target_link_libraries(postgres_qpsql_protocol_smoke_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(postgres_qpsql_protocol_smoke_test)

    add_executable(redis_presence_flow_test
        tests/redis_presence_flow_test.cpp
        src/redisclient.cpp
        include/redisclient.h
    )
    target_include_directories(redis_presence_flow_test PRIVATE include)
    target_link_libraries(redis_presence_flow_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)

    add_executable(redis_server_readiness_test
        tests/redis_server_readiness_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
            src/redisclient.cpp
        src/message.cpp
        src/e2eenvelope.cpp
        src/e2e_crypto_primitives.cpp
        src/e2e_provider_runtime.cpp
        src/e2e_envelope_codec.cpp
        src/objectstore.cpp
        src/heartbeatmonitor.cpp
        include/server.h
        include/redisclient.h
        include/message.h
        include/objectstore.h
        include/chatuser.h
        include/heartbeatmonitor.h
    )
    qtnetworkchat_target_include_dirs(redis_server_readiness_test)
    target_link_libraries(redis_server_readiness_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(redis_server_readiness_test)

    add_executable(redis_cross_instance_chat_test
        tests/redis_cross_instance_chat_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
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
    target_link_libraries(redis_cross_instance_chat_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(redis_cross_instance_chat_test)

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
    # This suite asserts the draft-only, provider-not-linked status contract.
    # The linked production path is covered by E2EProductionAdapterRuntime.
    if(NOT QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER)
        add_test(
            NAME E2EEnvelopeProtocol
            COMMAND ${CMAKE_COMMAND}
                -DTEST_EXE=$<TARGET_FILE:e2e_envelope_test>
                -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
        )
    endif()
    add_test(
        NAME E2ECryptoBackendConfig
        COMMAND ${CMAKE_COMMAND}
            -DCONFIG_HEADER=${CMAKE_CURRENT_BINARY_DIR}/generated/qtnetworkchat_e2e_crypto_config.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_e2e_crypto_backend_config.cmake
    )
    add_test(
        NAME E2EProductionAdapterLinkGate
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DPROBE_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/e2e-production-adapter-link-gate
            -DCMAKE_EXE=${CMAKE_COMMAND}
            -DGENERATOR_NAME=${CMAKE_GENERATOR}
            -DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
            -DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
            -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
            -DQt6WebSockets_DIR=${Qt6WebSockets_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_e2e_production_adapter_link_gate.cmake
    )
    if(WIN32)
    add_test(
        NAME E2EProductionAdapterRuntimeGate
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DPROBE_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/e2e-production-adapter-runtime-gate
            -DCMAKE_EXE=${CMAKE_COMMAND}
            -DGENERATOR_NAME=${CMAKE_GENERATOR}
            -DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
            -DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
            -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
            -DQt6WebSockets_DIR=${Qt6WebSockets_DIR}
            -DCONFIGURATION=$<CONFIG>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -DOPENSSL_RUNTIME_DIR=${QTNETWORKCHAT_E2E_PRODUCTION_OPENSSL_RUNTIME_DIR}
            -DLINKED_RELEASE_CANDIDATE_DIR=${CMAKE_CURRENT_BINARY_DIR}/e2e_release_evidence_linked_probe_fixture
            -DCURRENT_HEAD_LINKED_RELEASE_CANDIDATE_DIR=${CMAKE_CURRENT_BINARY_DIR}/e2e_release_evidence_linked_candidate
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_e2e_production_adapter_runtime_gate.cmake
    )
    endif()
    add_test(
        NAME E2EProductionAdapterRuntime
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:e2e_production_adapter_runtime_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -DOPENSSL_RUNTIME_DIR=${QTNETWORKCHAT_E2E_PRODUCTION_OPENSSL_RUNTIME_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME E2ERolloutObservabilityEvidence
        COMMAND ${CMAKE_COMMAND}
            -DEXPORTER_EXE=$<TARGET_FILE:e2e_rollout_observability_exporter>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -DOPENSSL_RUNTIME_DIR=${QTNETWORKCHAT_E2E_PRODUCTION_OPENSSL_RUNTIME_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_e2e_rollout_observability_evidence.cmake
    )
    if(WIN32)
    add_test(
        NAME E2EReleaseEvidencePackage
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/package-e2e-release-evidence.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_e2e_release_evidence_package.cmake
    )
    add_test(
        NAME LocalReleaseReviewPackage
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/package-local-release-review.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_local_release_review_package.cmake
    )
    add_test(
        NAME ReleaseArchiveDecisionPackage
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/package-release-archive-decision.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_release_archive_decision_package.cmake
    )
    add_test(
        NAME ReleaseDeliveryHandoffPackage
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/package-release-delivery-handoff.ps1
            -DREPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_release_delivery_handoff_package.cmake
    )
    add_test(
        NAME ReleaseCloseoutSummaryPackage
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/package-release-closeout-summary.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_release_closeout_summary_package.cmake
    )
    add_test(
        NAME ReleaseFinalLocalArchivePackage
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/package-release-final-local-archive.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_release_final_local_archive_package.cmake
    )
    add_test(
        NAME ReleaseCloseoutRefresh
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/refresh-release-closeout.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_refresh_release_closeout.cmake
    )
    add_test(
        NAME E2ELinkedCandidatePromotion
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/promote-e2e-linked-candidate.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_e2e_linked_candidate_promotion.cmake
    )
    endif()
    add_test(
        NAME TransferChatItemRendererUi
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:transfer_chat_item_renderer_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
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
        NAME HistoryAttachmentParser
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:history_attachment_parser_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME HistoryServicePersistence
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:history_service_test>
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
        NAME ServerFriendPersistenceProtocol
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:server_friend_persistence_test>
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
        NAME LoginWindowGuiInteraction
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:login_window_gui_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME GroupInfoPanelUiInteraction
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:group_info_panel_ui_test>
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
        NAME WebSocketTransport
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:websocket_transport_test>
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
        NAME TransferManagerRecoveryUi
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:transfer_manager_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME FriendManagerContactState
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:friend_manager_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME LocalFileManagerValidation
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:local_file_manager_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME ChatSessionManagerUiState
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:chat_session_manager_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME ChatContextManagerDispatch
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:chat_context_manager_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME NotificationPanelManagerUiState
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:notification_panel_manager_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME ComposerManagerUiState
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:composer_manager_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME WindowStateManagerChromeState
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:window_state_manager_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME GroupManagerPermissionState
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:group_manager_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME ScreenshotGeometryMapping
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:screenshot_geometry_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME SessionListOrdering
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:session_list_builder_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME ThemeQssConsistency
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:theme_qss_consistency_test>
            "-DTEST_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/ui/style-qqnt.qss;${CMAKE_CURRENT_SOURCE_DIR}/ui/style-qqnt-dark.qss;${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/qss_objectname_scan_manifest.txt"
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME ClientStoragePersistence
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:client_storage_test>
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
        NAME PublicImageTransfer
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:public_image_transfer_test>
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
        NAME RedisStartupRequirement
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:redis_startup_requirement_test>
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
        NAME RedisServerReadiness
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:redis_server_readiness_test>
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
    if(WIN32)
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
        NAME S3RealBackendReadiness
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-s3-real-backend-readiness.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_s3_real_backend_readiness.cmake
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
        NAME LargeFileGovernancePerformanceSummary
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-large-file-governance-performance-summary.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_large_file_governance_performance_summary.cmake
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
        NAME ReadmeInformationArchitecture
        COMMAND powershell -ExecutionPolicy Bypass -File
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-readme-information-architecture.ps1
            -ReadmePath ${CMAKE_CURRENT_SOURCE_DIR}/README.md
            -DocsDir ${CMAKE_CURRENT_SOURCE_DIR}/docs
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
        NAME AutomationAckDrill
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-automation-ack-drill.ps1
            -DACK_SCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-automation-task-ack.ps1
            -DHISTORY_SCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-automation-task-history.ps1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_automation_ack_drill.cmake
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


    add_executable(qqnt_backend_parity_test
        tests/qqnt_backend_parity_test.cpp
        src/qqnt_backend_service.cpp
        include/qqnt_backend_service.h
    )
    qtnetworkchat_target_include_dirs(qqnt_backend_parity_test)
    target_link_libraries(qqnt_backend_parity_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui)

    add_executable(qqnt_backend_service_test
        tests/qqnt_backend_service_test.cpp
        src/qqnt_backend_service.cpp
        include/qqnt_backend_service.h
    )
    qtnetworkchat_target_include_dirs(qqnt_backend_service_test)
    target_link_libraries(qqnt_backend_service_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui)

    add_executable(qqnt_protocol_drift_test
        tests/qqnt_protocol_drift_test.cpp
    )
    qtnetworkchat_target_include_dirs(qqnt_protocol_drift_test)
    target_link_libraries(qqnt_protocol_drift_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(qqnt_engine_end_to_end_test
        tests/qqnt_engine_end_to_end_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
        src/qqnt_redis_service.cpp
        src/client.cpp
        src/redisclient.cpp
        src/message.cpp
        src/e2eenvelope.cpp
        src/e2e_crypto_primitives.cpp
        src/e2e_provider_runtime.cpp
        src/e2e_envelope_codec.cpp
        src/objectstore.cpp
        src/heartbeatmonitor.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_PRODUCTION_ADAPTER_SOURCES}
        include/server.h
        include/qqnt_redis_service.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/e2eenvelope.h
        include/objectstore.h
        include/chatuser.h
        include/heartbeatmonitor.h
    )
    qtnetworkchat_target_include_dirs(qqnt_engine_end_to_end_test)
    target_link_libraries(qqnt_engine_end_to_end_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(qqnt_engine_end_to_end_test)
    add_dependencies(qqnt_engine_end_to_end_test qqnt_engine)

    add_executable(qqnt_server_redis_test
        tests/qqnt_server_redis_test.cpp
        ${QTNETWORKCHAT_TEST_REDIS_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_SERVER_SOURCES}
        src/qqnt_redis_service.cpp
        src/client.cpp
        src/redisclient.cpp
        src/message.cpp
        src/e2eenvelope.cpp
        src/e2e_crypto_primitives.cpp
        src/e2e_provider_runtime.cpp
        src/e2e_envelope_codec.cpp
        src/objectstore.cpp
        src/heartbeatmonitor.cpp
        ${QTNETWORKCHAT_CLIENT_SUPPORT_SOURCES}
        ${QTNETWORKCHAT_PRODUCTION_ADAPTER_SOURCES}
        include/server.h
        include/qqnt_redis_service.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/e2eenvelope.h
        include/objectstore.h
        include/chatuser.h
        include/heartbeatmonitor.h
    )
    qtnetworkchat_target_include_dirs(qqnt_server_redis_test)
    target_link_libraries(qqnt_server_redis_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Gui)
    qtnetworkchat_attach_production_adapter(qqnt_server_redis_test)

    add_executable(qqnt_engine_smoke_test
        tests/qqnt_engine_smoke_test.cpp
    )
    qtnetworkchat_target_include_dirs(qqnt_engine_smoke_test)
    target_link_libraries(qqnt_engine_smoke_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)


    add_test(
        NAME QQNTBackendInvokeParity
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:qqnt_backend_parity_test>
            "-DTEST_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tauri_invoke_commands.json"
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME QQNTBackendLocalActions
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:qqnt_backend_service_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME QQNTProtocolDrift
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:qqnt_protocol_drift_test>
            "-DTEST_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ready.json;${CMAKE_CURRENT_SOURCE_DIR}/docs/qqnt-ipcv1.md;${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/protocol_contract.json;${CMAKE_CURRENT_SOURCE_DIR}/src/qqnt_engine_command_router.cpp;${CMAKE_CURRENT_SOURCE_DIR}/src/qqnt_client_bridge.cpp"
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME QQNTEngineEndToEnd
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:qqnt_engine_end_to_end_test>
            "-DTEST_ARGS=$<TARGET_FILE:qqnt_engine>"
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
    add_test(
        NAME QQNTServerRedis
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:qqnt_server_redis_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )

    add_test(
        NAME QQNTEngineSmoke
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:qqnt_engine_smoke_test>
            -DENGINE_EXE=$<TARGET_FILE:qqnt_engine>
            -DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qqnt_engine_smoke_test.cmake
    )
endif()
