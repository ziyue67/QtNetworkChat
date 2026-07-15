import re

path = r'D:\C++VS pro\QtNetworkChat\src\mainwindow.cpp'
with open(path, 'r', encoding='utf-8', newline='') as f:
    text = f.read()

# Deduplicate consecutive identical dialog includes
lines = text.splitlines(True)
out_lines = []
last = None
for line in lines:
    stripped = line.rstrip('\r\n')
    if (stripped in ('#include "dialogs/friendmanagerdialog.h"', '#include "dialogs/globalsearchdialog.h"')
            and stripped == last):
        continue
    out_lines.append(line)
    last = stripped
text = ''.join(out_lines)

if 'friendManagerRequested' not in text:
    nl = '\r\n' if '\r\n' in text else '\n'
    indent = '    '
    new_block = (
        nl + indent + "connect(m_contactsView, &ContactsView::friendManagerRequested, this, [this]() {" + nl +
        indent + "    FriendManagerDialog dlg(this);" + nl +
        indent + "    dlg.exec();" + nl +
        indent + "});" + nl +
        indent + "connect(m_contactsView, &ContactsView::globalSearchRequested, this, [this]() {" + nl +
        indent + "    GlobalSearchDialog dlg(this);" + nl +
        indent + "    dlg.exec();" + nl +
        indent + "});"
    )
    pattern = re.compile(
        r'connect\(m_contactsView,\s*&ContactsView::createGroupRequested,\s*this,\s*\[this\]\(\)\s*\{\s*handleCreateMenuCommand\(QStringLiteral\("create-group"\)\);\s*\}\);'
    )
    m = pattern.search(text)
    if not m:
        print('createGroupRequested connect block not found')
    else:
        insert_pos = m.end()
        text = text[:insert_pos] + new_block + text[insert_pos:]
        print('connect block inserted')
else:
    print('connect block already present')

with open(path, 'w', encoding='utf-8', newline='') as f:
    f.write(text)

print('mainwindow.cpp patched2')
