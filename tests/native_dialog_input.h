#pragma once

#include <QDir>
#include <QTimer>
#include <QStringList>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Only touches visible Win32 file dialogs belonging to this test process.
// Real output assertions live in nativePickers(), outside the input driver.
class NativeDialogInput : public QTimer {
public:
    struct Choice { QString path; bool directory = false; };
    QList<Choice> choices;
    int observed = 0;
    HWND lastDialog = nullptr;
    explicit NativeDialogInput(QObject* parent) : QTimer(parent) {
        connect(this, &QTimer::timeout, this, [this] {
            HWND dialog = nullptr;
            EnumWindows([](HWND candidate, LPARAM result) -> BOOL {
                DWORD pid = 0;
                GetWindowThreadProcessId(candidate, &pid);
                wchar_t name[64]{};
                GetClassNameW(candidate, name, 64);
                if (pid == GetCurrentProcessId() && IsWindowVisible(candidate)
                    && wcscmp(name, L"#32770") == 0) {
                    *reinterpret_cast<HWND*>(result) = candidate;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&dialog));
            if (!dialog || dialog == lastDialog || observed >= choices.size()) return;
            lastDialog = dialog;
            const Choice choice = choices.at(observed++);
            SetForegroundWindow(dialog);
            if (choice.path.isEmpty()) { key(VK_ESCAPE); return; }
            chord(choice.directory ? VK_CONTROL : VK_MENU, choice.directory ? 'L' : 'N');
            QTimer::singleShot(200, this, [choice] {
                chord(VK_CONTROL, 'A');
                const QString path = QDir::toNativeSeparators(choice.path);
                for (const QChar character : path) {
                    INPUT input[2]{};
                    input[0].type = input[1].type = INPUT_KEYBOARD;
                    input[0].ki.wScan = input[1].ki.wScan = character.unicode();
                    input[0].ki.dwFlags = KEYEVENTF_UNICODE;
                    input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                    SendInput(2, input, sizeof(INPUT));
                }
                key(VK_RETURN);
            });
            if (choice.directory) QTimer::singleShot(700, this, [] { key(VK_RETURN); });
        });
        start(200);
    }
private:
    static void key(WORD keyCode) {
        INPUT input[2]{};
        input[0].type = input[1].type = INPUT_KEYBOARD;
        input[0].ki.wVk = input[1].ki.wVk = keyCode;
        input[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, input, sizeof(INPUT));
    }
    static void chord(WORD modifier, WORD keyCode) {
        INPUT input[4]{};
        for (auto& event : input) event.type = INPUT_KEYBOARD;
        input[0].ki.wVk = input[3].ki.wVk = modifier;
        input[1].ki.wVk = input[2].ki.wVk = keyCode;
        input[2].ki.dwFlags = input[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, input, sizeof(INPUT));
    }
};
#endif
