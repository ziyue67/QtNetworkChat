QT += network widgets sql multimedia websockets

CONFIG += c++17

msvc {
    QMAKE_CXXFLAGS += /utf-8
    QMAKE_CXXFLAGS += /D_ALLOW_ITERATOR_DEBUG_LEVEL=0
    QMAKE_CXXFLAGS += /D_ITERATOR_DEBUG_LEVEL=0
    QMAKE_CXXFLAGS += /FI$$shell_quote($$PWD/include/msvc_compat.h)
}

INCLUDEPATH += $$PWD/include

TARGET = QtNetworkChat
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/mainwindow_setup.cpp \
    src/mainwindow_transfers.cpp \
    src/mainwindow_history.cpp \
    src/mainwindow_chat_actions.cpp \
    src/mainwindow_support.cpp \
    src/mainwindow_menu.cpp \
    src/mainwindow_notifications.cpp \
    src/mainwindow_group_members.cpp \
    src/mainwindow_friends.cpp \
    src/mainwindow_views.cpp \
    src/mainwindow_group_panel.cpp \
    src/group_info_panel_ui.cpp \
    src/mainwindow_search.cpp \
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
    src/e2e_backend_selection.cpp \
    src/e2e_backend_contracts.cpp \
    src/e2e_backend_provider_status.cpp \
    src/e2e_backend_probes.cpp \
    src/e2e_backend_invocation.cpp \
    src/e2e_backend_review.cpp \
    src/e2e_backend_acceptance.cpp \
    src/e2e_crypto_primitives.cpp \
    src/e2e_provider_runtime.cpp \
    src/e2e_envelope_codec.cpp \
    src/server.cpp \
    src/server_redis_routing.cpp \
    src/server_large_file_routing.cpp \
    src/server_group_repository.cpp \
    src/server_friend_repository.cpp \
    src/server_transport.cpp \
    src/server_database.cpp \
    src/server_account.cpp \
    src/server_offline_delivery.cpp \
    src/server_file_transfer.cpp \
    src/server_delivery_support.cpp \
    src/server_friend.cpp \
    src/server_group.cpp \
    src/client.cpp \
    src/redisclient.cpp \
    src/qqnt_redis_service.cpp \
    src/qqnt_backend_service.cpp \
    src/objectstore.cpp \
    src/message.cpp \
    src/theme/thememanager.cpp \
    src/theme/dialogstyle.cpp \
    src/widgets/avatarlabel.cpp \
    src/widgets/titlebar.cpp \
    src/widgets/dialogtitlebar.cpp \
    src/widgets/appnav.cpp \
    src/widgets/composerwidget.cpp \
    src/widgets/composerTextEdit.cpp \
    src/widgets/contactcard.cpp \
    src/widgets/contactlistwidget.cpp \
    src/widgets/contactnoticepanel.cpp \
    src/widgets/groupmembersidebar.cpp \
    src/views/messagesview.cpp \
    src/views/contactsview.cpp \
    src/views/favoritesview.cpp \
    src/views/settingsview.cpp \
    src/views/profileview.cpp \
    src/windows/screenshotcapturewindow.cpp \
    src/windows/imagepreviewwindow.cpp \
    src/windows/forwardwindow.cpp \
    src/windows/noticefilterwindow.cpp \
    src/windows/registerwindow.cpp \
    src/windows/loginwindow.cpp \
    src/dialogs/addfrienddialog.cpp \
    src/dialogs/creategroupdialog.cpp \
    src/dialogs/globalsearchdialog.cpp \
    src/dialogs/friendmanagerdialog.cpp \
    src/dialogs/essencepanel.cpp \
    src/dialogs/groupnicknamedialog.cpp \
    src/dialogs/memberprofilecard.cpp \
    src/dialogs/mutedurationdialog.cpp \
    src/sessionitemdelegate.cpp

HEADERS += \
    src/e2e_backend_status_p.h \
    src/mainwindow_support.h \
    include/mainwindow.h \
    src/group_info_panel_ui.h \
    src/e2e_provider_runtime.h \
    src/server_database.h \
    src/server_delivery_support.h \
    include/chatbubbledelegate.h \
    include/qqnt_log.h \
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
    include/theme/dialogstyle.h \
    include/widgets/avatarlabel.h \
    include/widgets/titlebar.h \
    include/widgets/dialogtitlebar.h \
    include/widgets/appnav.h \
    include/widgets/composerwidget.h \
    include/widgets/composerTextEdit.h \
    include/widgets/contactcard.h \
    include/widgets/contactlistwidget.h \
    include/widgets/contactnoticepanel.h \
    include/widgets/groupmembersidebar.h \
    include/views/messagesview.h \
    include/views/contactsview.h \
    include/views/favoritesview.h \
    include/views/settingsview.h \
    include/views/profileview.h \
    include/windows/screenshotcapturewindow.h \
    include/windows/imagepreviewwindow.h \
    include/windows/forwardwindow.h \
    include/windows/noticefilterwindow.h \
    include/windows/registerwindow.h \
    include/windows/loginwindow.h \
    include/dialogs/addfrienddialog.h \
    include/dialogs/creategroupdialog.h \
    include/dialogs/globalsearchdialog.h \
    include/dialogs/friendmanagerdialog.h \
    include/dialogs/essencepanel.h \
    include/dialogs/groupnicknamedialog.h \
    include/dialogs/memberprofilecard.h \
    include/dialogs/mutedurationdialog.h \
    include/sessionitemdelegate.h

FORMS += \
    ui/mainwindow.ui

DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc
RCC_DIR = $$PWD/build/rcc
UI_DIR = $$PWD/build/ui

win32 {
    QSS_SOURCE_DIR = $$PWD/ui
    QSS_TARGET_DIR = $$DESTDIR/ui
    QMAKE_POST_LINK += if not exist \"$$shell_path($$QSS_TARGET_DIR)\" $$QMAKE_MKDIR \"$$shell_path($$QSS_TARGET_DIR)\" $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$QMAKE_COPY \"$$shell_path($$QSS_SOURCE_DIR/style-qqnt.qss)\" \"$$shell_path($$QSS_TARGET_DIR/style-qqnt.qss)\" $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$QMAKE_COPY \"$$shell_path($$QSS_SOURCE_DIR/style-qqnt-dark.qss)\" \"$$shell_path($$QSS_TARGET_DIR/style-qqnt-dark.qss)\" $$escape_expand(\\n\\t)

    QSQLITE_SOURCE = $$[QT_INSTALL_PLUGINS]/sqldrivers/qsqlite.dll
    QSQLITE_TARGET = $$DESTDIR/sqldrivers/qsqlite.dll
    QMAKE_POST_LINK += if not exist \"$$shell_path($$DESTDIR/sqldrivers)\" $$QMAKE_MKDIR \"$$shell_path($$DESTDIR/sqldrivers)\" $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$QMAKE_COPY \"$$shell_path($$QSQLITE_SOURCE)\" \"$$shell_path($$QSQLITE_TARGET)\" $$escape_expand(\\n\\t)
}

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
