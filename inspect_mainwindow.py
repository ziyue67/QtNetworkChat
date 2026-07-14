from pathlib import Path
import re
p = Path(r"D:\C++VS pro\QtNetworkChat\src\mainwindow.cpp")
text = p.read_text(encoding="utf-8")
for pat in [
    r"m_messagesView->setChatModel\(m_chatModel\);",
    r"void MainWindow::onShowGroupMemberSidebar\(\)",
    r"void MainWindow::refreshWorkspaceChrome\(\)",
    r"void MainWindow::switchToLocalGroup\(const QString& groupId, const QString& groupName\)",
    r"GroupMemberSidebar\* m_groupMemberSidebar = new GroupMemberSidebar",
    r"EssencePanel\* m_essencePanel = new EssencePanel",
    r"refreshWorkspaceChrome\(\)"
]:
    m = re.search(pat, text)
    print(pat, "->", m.start() if m else "not found", "line", text[:m.start()].count('\n')+1 if m else '')
