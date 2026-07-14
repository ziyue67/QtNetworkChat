import pathlib
p = pathlib.Path('D:\\C++VS pro\\QtNetworkChat\\src\\windows\\imagepreviewwindow.cpp')
t = p.read_text(encoding='utf-8')
if '#include <QGuiApplication>' not in t:
    t = t.replace('#include <QVBoxLayout>\n', '#include <QVBoxLayout>\n#include <QGuiApplication>\n')
    p.write_text(t, encoding='utf-8')
    print('patched')
else:
    print('already patched')
