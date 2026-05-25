cmake_minimum_required(VERSION 3.16)

project(QtNetworkChat VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ui)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Network Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Network Sql)

add_executable(QtNetworkChat
    src/main.cpp
    src/mainwindow.cpp
    src/server.cpp
    src/client.cpp
    src/redisclient.cpp
    src/message.cpp
    include/mainwindow.h
    include/server.h
    include/client.h
    include/redisclient.h
    include/message.h
    include/chatuser.h
    ui/mainwindow.ui
)

target_include_directories(QtNetworkChat PRIVATE include)
target_link_libraries(QtNetworkChat PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

include(CTest)
if(BUILD_TESTING)
    add_executable(message_serialization_test
        tests/message_serialization_test.cpp
        src/message.cpp
        include/message.h
    )
    target_include_directories(message_serialization_test PRIVATE include)
    target_link_libraries(message_serialization_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    add_executable(server_group_member_update_test
        tests/server_group_member_update_test.cpp
        src/server.cpp
        src/client.cpp
        src/redisclient.cpp
        src/message.cpp
        include/server.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/chatuser.h
    )
    target_include_directories(server_group_member_update_test PRIVATE include)
    target_link_libraries(server_group_member_update_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(file_transfer_cancel_test
        tests/file_transfer_cancel_test.cpp
        src/server.cpp
        src/client.cpp
        src/redisclient.cpp
        src/message.cpp
        include/server.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/chatuser.h
    )
    target_include_directories(file_transfer_cancel_test PRIVATE include)
    target_link_libraries(file_transfer_cancel_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(redis_client_test
        tests/redis_client_test.cpp
        src/redisclient.cpp
        include/redisclient.h
    )
    target_include_directories(redis_client_test PRIVATE include)
    target_link_libraries(redis_client_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)

    add_executable(redis_unavailable_fallback_test
        tests/redis_unavailable_fallback_test.cpp
        src/server.cpp
        src/client.cpp
        src/redisclient.cpp
        src/message.cpp
        include/server.h
        include/client.h
        include/redisclient.h
        include/message.h
        include/chatuser.h
    )
    target_include_directories(redis_unavailable_fallback_test PRIVATE include)
    target_link_libraries(redis_unavailable_fallback_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

    add_executable(redis_presence_flow_test
        tests/redis_presence_flow_test.cpp
        src/redisclient.cpp
        include/redisclient.h
    )
    target_include_directories(redis_presence_flow_test PRIVATE include)
    target_link_libraries(redis_presence_flow_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)

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
        NAME ServerGroupMemberUpdateProtocol
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:server_group_member_update_test>
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
        NAME RedisClientRespProtocol
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:redis_client_test>
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
        NAME RedisPresenceCommandFlow
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:redis_presence_flow_test>
            -DQT_BIN_DIR=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_qt_test.cmake
    )
endif()
