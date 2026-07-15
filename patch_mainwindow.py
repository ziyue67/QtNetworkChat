import os

path = r'D:\C++VS pro\QtNetworkChat\src\mainwindow.cpp'
with open(path, 'r', encoding='utf-8', newline='') as f:
    text = f.read()

old_include = '#include "qqnt_backend_service.h"'
new_include = '''#include "qqnt_backend_service.h"
#include "dialogs/friendmanagerdialog.h"
#include "dialogs/globalsearchdialog.h"'''

old_connect = '''    connect(m_contactsView, &ContactsView::createGroupRequested, this, [this]() {
        handleCreateMenuCommand(QStringLiteral("create-group"));
    });
    if (m_contactsView->searchEdit()) {'''

new_connect = '''    connect(m_contactsView, &ContactsView::createGroupRequested, this, [this]() {
        handleCreateMenuCommand(QStringLiteral("create-group"));
    });
    connect(m_contactsView, &ContactsView::friendManagerRequested, this, [this]() {
        FriendManagerDialog dlg(this);
        dlg.exec();
    });
    connect(m_contactsView, &ContactsView::globalSearchRequested, this, [this]() {
        GlobalSearchDialog dlg(this);
        dlg.exec();
    });
    if (m_contactsView->searchEdit()) {'''

if old_include not in text:
    print('include marker not found')
else:
    text = text.replace(old_include, new_include, 1)

if old_connect not in text:
    print('connect marker not found')
else:
    text = text.replace(old_connect, new_connect, 1)

with open(path, 'w', encoding='utf-8', newline='') as f:
    f.write(text)

print('mainwindow.cpp patched')
