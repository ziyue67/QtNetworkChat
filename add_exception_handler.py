import sys

with open('src/main.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# Add Windows.h include after the existing includes
old_includes = '#include <QTcpSocket>\n'
new_includes = '#include <QTcpSocket>\n\n#ifdef Q_OS_WIN\n#include <Windows.h>\n#endif\n'
if old_includes not in content:
    print('NOT FOUND: includes')
    sys.exit(1)
content = content.replace(old_includes, new_includes, 1)

# Add exception handler before the namespace helper functions
namespace_start = 'namespace {\n'
exception_handler = '''namespace {

#ifdef Q_OS_WIN
LONG WINAPI qqntUnhandledExceptionFilter(EXCEPTION_POINTERS* ep)
{
    HANDLE hFile = CreateFileW(L"qqnt-debug.log", FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        char buf[1024];
        DWORD written = 0;
        snprintf(buf, sizeof(buf), "[CRASH] Exception code: 0x%08X\\n", (unsigned int)ep->ExceptionRecord->ExceptionCode);
        WriteFile(hFile, buf, (DWORD)strlen(buf), &written, NULL);
        snprintf(buf, sizeof(buf), "[CRASH] Exception address: 0x%p\\n", (void*)ep->ExceptionRecord->ExceptionAddress);
        WriteFile(hFile, buf, (DWORD)strlen(buf), &written, NULL);

        void* stack[64];
        WORD frames = CaptureStackBackTrace(0, 64, stack, NULL);
        WriteFile(hFile, "[CRASH] Stack trace (return addresses):\\n", 40, &written, NULL);
        for (WORD i = 0; i < frames; ++i) {
            snprintf(buf, sizeof(buf), "  %02u: 0x%p\\n", i, stack[i]);
            WriteFile(hFile, buf, (DWORD)strlen(buf), &written, NULL);
        }
        CloseHandle(hFile);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

'''

content = content.replace(namespace_start, exception_handler, 1)

# Install the filter at the start of main
old_main = 'int main(int argc, char *argv[])\n{\n    QApplication a(argc, argv);\n'
new_main = 'int main(int argc, char *argv[])\n{\n#ifdef Q_OS_WIN\n    SetUnhandledExceptionFilter(qqntUnhandledExceptionFilter);\n#endif\n\n    QApplication a(argc, argv);\n'
if old_main not in content:
    print('NOT FOUND: main start')
    sys.exit(1)
content = content.replace(old_main, new_main, 1)

with open('src/main.cpp', 'w', encoding='utf-8') as f:
    f.write(content)
print('done')
