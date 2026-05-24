cmake_minimum_required(VERSION 3.16)

project(QtNetworkChat VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ui)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Sql)

add_executable(QtNetworkChat
    src/main.cpp
    src/mainwindow.cpp
    src/server.cpp
    src/client.cpp
    src/message.cpp
    include/mainwindow.h
    include/server.h
    include/client.h
    include/message.h
    include/chatuser.h
    ui/mainwindow.ui
)

target_include_directories(QtNetworkChat PRIVATE include)
target_link_libraries(QtNetworkChat PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql)

include(CTest)
if(BUILD_TESTING)
    add_test(
        NAME QtNetworkChatExecutableExists
        COMMAND ${CMAKE_COMMAND}
            -DQTNETWORKCHAT_EXE=$<TARGET_FILE:QtNetworkChat>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_executable.cmake
    )
endif()
