QT += network widgets sql

CONFIG += c++17

INCLUDEPATH += $$PWD/include

TARGET = QtNetworkChat
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/server.cpp \
    src/client.cpp \
    src/redisclient.cpp \
    src/message.cpp

HEADERS += \
    include/mainwindow.h \
    include/server.h \
    include/client.h \
    include/redisclient.h \
    include/chatuser.h \
    include/message.h

FORMS += \
    ui/mainwindow.ui

DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc
RCC_DIR = $$PWD/build/rcc
UI_DIR = $$PWD/build/ui

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
