QT += network widgets sql

CONFIG += c++17

INCLUDEPATH += $$PWD/include

TARGET = QtNetworkChat
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/logincredentialstore.cpp \
    src/historyservice.cpp \
    src/historymetadata.cpp \
    src/transfermanager.cpp \
    src/transferchatitemrenderer.cpp \
    src/chatsessionmanager.cpp \
    src/composermanager.cpp \
    src/windowstatemanager.cpp \
    src/filetransferstatus.cpp \
    src/friendmanager.cpp \
    src/notificationpanelmanager.cpp \
    src/groupmanager.cpp \
    src/chatcontextmanager.cpp \
    src/localfilemanager.cpp \
    src/clientstorage.cpp \
    src/heartbeatmonitor.cpp \
    src/tlssecurity.cpp \
    src/e2eenvelope.cpp \
    src/server.cpp \
    src/client.cpp \
    src/redisclient.cpp \
    src/objectstore.cpp \
    src/message.cpp

HEADERS += \
    include/mainwindow.h \
    include/logincredentialstore.h \
    include/historyservice.h \
    include/historymetadata.h \
    include/transfermanager.h \
    include/transferchatitemrenderer.h \
    include/chatsessionmanager.h \
    include/composermanager.h \
    include/windowstatemanager.h \
    include/filetransferstatus.h \
    include/friendmanager.h \
    include/notificationpanelmanager.h \
    include/groupmanager.h \
    include/chatcontextmanager.h \
    include/localfilemanager.h \
    include/clientstorage.h \
    include/heartbeatmonitor.h \
    include/tlssecurity.h \
    include/e2eenvelope.h \
    include/server.h \
    include/client.h \
    include/redisclient.h \
    include/objectstore.h \
    include/chatuser.h \
    include/message.h \
    include/qtnetworkchat_version.h \
    include/qtnetworkchat_e2e_crypto_config.h \
    include/qtnetworkchat_e2e_provider_api.h

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
