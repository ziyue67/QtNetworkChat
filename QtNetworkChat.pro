QT += network widgets sql multimedia

CONFIG += c++17

INCLUDEPATH += $$PWD/include

TARGET = QtNetworkChat
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/chatbubbledelegate.cpp \
    src/iconhelper.cpp \
    src/logincredentialstore.cpp \
    src/historyservice.cpp \
    src/historymetadata.cpp \
    src/historyattachmentparser.cpp \
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
    src/qqnt_redis_service.cpp \
    src/qqnt_backend_service.cpp \
    src/objectstore.cpp \
    src/message.cpp \
    src/theme/thememanager.cpp \
    src/widgets/avatarlabel.cpp \
    src/widgets/titlebar.cpp \
    src/widgets/appnav.cpp \
    src/widgets/composerwidget.cpp \
    src/widgets/composerTextEdit.cpp \
    src/views/messagesview.cpp \
    src/views/contactsview.cpp \
    src/views/favoritesview.cpp \
    src/views/settingsview.cpp

HEADERS += \
    include/mainwindow.h \
    include/chatbubbledelegate.h \
    include/iconhelper.h \
    include/logincredentialstore.h \
    include/historyservice.h \
    include/historymetadata.h \
    include/historyattachmentparser.h \
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
    include/qqnt_redis_service.h \
    include/qqnt_backend_service.h \
    include/objectstore.h \
    include/chatuser.h \
    include/message.h \
    include/qtnetworkchat_version.h \
    include/qtnetworkchat_e2e_crypto_config.h \
    include/qtnetworkchat_e2e_provider_api.h \
    include/theme/thememanager.h \
    include/widgets/avatarlabel.h \
    include/widgets/titlebar.h \
    include/widgets/appnav.h \
    include/widgets/composerwidget.h \
    include/widgets/composerTextEdit.h \
    include/views/messagesview.h \
    include/views/contactsview.h \
    include/views/favoritesview.h \
    include/views/settingsview.h

FORMS += \
    ui/mainwindow.ui

DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc
RCC_DIR = $$PWD/build/rcc
UI_DIR = $$PWD/build/ui

win32 {
    QSQLITE_SOURCE = $$[QT_INSTALL_PLUGINS]/sqldrivers/qsqlite.dll
    QSQLITE_TARGET = $$DESTDIR/sqldrivers/qsqlite.dll
    QMAKE_POST_LINK += if not exist \"$$shell_path($$DESTDIR/sqldrivers)\" $$QMAKE_MKDIR \"$$shell_path($$DESTDIR/sqldrivers)\" $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$QMAKE_COPY \"$$shell_path($$QSQLITE_SOURCE)\" \"$$shell_path($$QSQLITE_TARGET)\" $$escape_expand(\\n\\t)
}

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
