import pathlib
p = pathlib.Path('D:\\C++VS pro\\QtNetworkChat\\src\\mainwindow.cpp')
t = p.read_text(encoding='utf-8')
if '#include "dialogs/creategroupdialog.h"' not in t:
    t = t.replace('#include "dialogs/mutedurationdialog.h"\n',
                    '#include "dialogs/mutedurationdialog.h"\n#include "dialogs/creategroupdialog.h"\n#include "dialogs/globalsearchdialog.h"\n')
    p.write_text(t, encoding='utf-8')
    print('includes patched')
else:
    print('includes already present')
