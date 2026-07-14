import pathlib
p = pathlib.Path('D:\\C++VS pro\\QtNetworkChat\\src\\mainwindow.cpp')
t = p.read_text(encoding='utf-8')

if '#include "widgets/groupmembersidebar.h"' not in t:
    t = t.replace('#include "views/profileview.h"\n',
                    '#include "views/profileview.h"\n#include "widgets/groupmembersidebar.h"\n#include "dialogs/essencepanel.h"\n')

if 'm_groupMemberSidebar = new GroupMemberSidebar' not in t:
    t = t.replace('    m_profileView = new ProfileView(m_qqntRoot);\n',
                  '''    m_profileView = new ProfileView(m_qqntRoot);

    m_groupMemberSidebar = new GroupMemberSidebar(m_qqntRoot);
    m_essencePanel = new EssencePanel(m_qqntRoot);

    QAction* showGroupMembersAction = new QAction(QStringLiteral("成员侧栏"), this);
    showGroupMembersAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+M")));
    connect(showGroupMembersAction, &QAction::triggered, this, &MainWindow::onShowGroupMemberSidebar);
    addAction(showGroupMembersAction);

    QAction* showEssenceAction = new QAction(QStringLiteral("精华消息"), this);
    showEssenceAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    connect(showEssenceAction, &QAction::triggered, this, &MainWindow::onShowEssencePanel);
    addAction(showEssenceAction);
''')

end = '\nvoid MainWindow::onShowGroupMemberSidebar()\n{\n    if (!m_groupMemberSidebar) return;\n    m_groupMemberSidebar->setGroupId(QStringLiteral(""));\n    m_groupMemberSidebar->show();\n}\n\nvoid MainWindow::onShowEssencePanel()\n{\n    if (!m_essencePanel) return;\n    m_essencePanel->show();\n}\n'
if 'void MainWindow::onShowGroupMemberSidebar()' not in t:
    t = t.rstrip() + end

p.write_text(t, encoding='utf-8')
print('mainwindow.cpp patched')
