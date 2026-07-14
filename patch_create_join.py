import pathlib, re

p = pathlib.Path('D:\\C++VS pro\\QtNetworkChat\\src\\mainwindow.cpp')
t = p.read_text(encoding='utf-8')

# 1. add includes
includes_block = '''#include "views/profileview.h"
#include "widgets/appnav.h"
#include "widgets/groupmembersidebar.h"
#include "dialogs/essencepanel.h"'''
if '#include "dialogs/creategroupdialog.h"' not in t:
    t = t.replace(includes_block,
                    includes_block + '\n#include "dialogs/creategroupdialog.h"\n#include "dialogs/globalsearchdialog.h"\n#include "widgets/contactlistwidget.h"')

# 2. replace create-group branch
old_create = '''    if (commandId == QLatin1String("create-group")) {
        bool ok = false;
        QString groupName = promptTextValue(QStringLiteral("创建群聊"),
                                            QStringLiteral("群聊名称:"),
                                            QStringLiteral("我的群聊"),
                                            &ok);
        if (!ok) {
            return true;
        }
        if (groupName.isEmpty()) {
            groupName = QStringLiteral("我的群聊");
        }

        const QString groupId = createLocalGroupSession(groupName);
        switchToLocalGroup(groupId, groupName);
        appendSystemMessage(QStringLiteral("已创建群聊: ") + groupName);
        return true;
    }'''

new_create = '''    if (commandId == QLatin1String("create-group")) {
        CreateGroupDialog dialog(this);
        QList<ContactDisplayData> friends;
        for (const QString& id : m_friendIds) {
            ContactDisplayData d;
            d.id = id;
            d.nickname = m_friendNames.value(id, id);
            d.isOnline = isContactOnline(id);
            friends.append(d);
        }
        dialog.setFriendList(friends);
        connect(&dialog, &CreateGroupDialog::createRequestedWithCategory,
                this, [this, &dialog](const QString& name, const QString& category, const QStringList& members) {
            const QString announcement = category.isEmpty()
                ? QStringLiteral("%1 已创建").arg(name)
                : QStringLiteral("%1 已创建（%2）").arg(name).arg(category);
            const QString groupId = createLocalGroupSession(name, members, announcement);
            dialog.accept();
            switchToLocalGroup(groupId, name);
            appendSystemMessage(QStringLiteral("已创建群聊: ") + name);
        });
        connect(&dialog, &CreateGroupDialog::createRequested,
                this, [this, &dialog](const QString& name, const QStringList& members) {
            const QString groupId = createLocalGroupSession(name, members);
            dialog.accept();
            switchToLocalGroup(groupId, name);
            appendSystemMessage(QStringLiteral("已创建群聊: ") + name);
        });
        dialog.exec();
        return true;
    }'''
if old_create in t:
    t = t.replace(old_create, new_create)
else:
    print('WARN: create-group branch not found')

# 3. replace onShowGlobalSearch body using brace counting
start_marker = 'void MainWindow::onShowGlobalSearch() {\n'
start = t.find(start_marker)
if start != -1:
    brace_start = start + len(start_marker)
    count = 1
    i = brace_start
    while i < len(t) and count > 0:
        if t[i] == '{':
            count += 1
        elif t[i] == '}':
            count -= 1
        i += 1
    end = i
    new_func = '''void MainWindow::onShowGlobalSearch() {
    GlobalSearchDialog dialog(this);
    for (const QString& id : m_friendIds) {
        ContactDisplayData d;
        d.id = id;
        d.nickname = m_friendNames.value(id, id);
        d.isOnline = isContactOnline(id);
        dialog.addResult(QStringLiteral("contact"), id, d.nickname,
                         isContactOnline(id) ? QStringLiteral("在线") : QStringLiteral("离线"));
    }
    for (const QString& id : m_localGroupIds) {
        dialog.addResult(QStringLiteral("group"), id,
                         m_localGroupNames.value(id, QStringLiteral("群聊")),
                         QStringLiteral("本地群聊"));
    }
    connect(&dialog, &GlobalSearchDialog::resultActivated,
            this, [this](const QString& type, const QString& id) {
        if (type == QStringLiteral("group")) {
            switchToLocalGroup(id, m_localGroupNames.value(id, QStringLiteral("群聊")));
        } else if (!id.isEmpty()) {
            openPrivateSession(id);
        }
    });
    connect(&dialog, &GlobalSearchDialog::joinGroupRequested,
            this, [this](const QString& groupId) {
        if (groupId.isEmpty()) return;
        if (m_localGroupIds.contains(groupId)) {
            if (!m_localGroupMembers.value(groupId).contains(m_currentUserId)) {
                appendMembersToLocalGroup(groupId, QStringList{m_currentUserId});
            }
            switchToLocalGroup(groupId, m_localGroupNames.value(groupId, QStringLiteral("群聊")));
            ui->statusbar->showMessage(QStringLiteral("已加入群聊"), 1800);
        } else {
            ui->statusbar->showMessage(QStringLiteral("该群不存在或暂不支持加入"), 2200);
        }
    });
    dialog.exec();
}
'''
    t = t[:start] + new_func + t[end:]
else:
    print('WARN: onShowGlobalSearch not found')

p.write_text(t, encoding='utf-8')
print('patched create/join')
