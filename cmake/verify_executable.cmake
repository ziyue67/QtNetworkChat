if(NOT DEFINED QTNETWORKCHAT_EXE OR QTNETWORKCHAT_EXE STREQUAL "")
    message(FATAL_ERROR "QTNETWORKCHAT_EXE was not provided.")
endif()

if(NOT EXISTS "${QTNETWORKCHAT_EXE}")
    message(FATAL_ERROR "QtNetworkChat executable was not found: ${QTNETWORKCHAT_EXE}")
endif()

file(SIZE "${QTNETWORKCHAT_EXE}" QTNETWORKCHAT_EXE_SIZE)
if(QTNETWORKCHAT_EXE_SIZE LESS 1024)
    message(FATAL_ERROR "QtNetworkChat executable is unexpectedly small: ${QTNETWORKCHAT_EXE_SIZE} bytes")
endif()

message(STATUS "Verified QtNetworkChat executable: ${QTNETWORKCHAT_EXE} (${QTNETWORKCHAT_EXE_SIZE} bytes)")
