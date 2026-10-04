#pragma once

#include <QDir>
#include <QTimer>
#include <QStringList>
#include <QCoreApplication>
#include <QProcess>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>
#include <atomic>
#include <functional>
#include <thread>
#include <chrono>

// Native modal loops can suspend Qt timers. Drive OS input from an independent
// worker so acceptance does not depend on the dialog pumping the Qt event loop.
class NativeDialogInputWorker : public QObject {
public:
    explicit NativeDialogInputWorker(QObject* parent) : QObject(parent) {}
    ~NativeDialogInputWorker() override { stop(); }
    void stop() {
        stopped = true;
        if (worker.joinable()) worker.join();
    }
    void start() {
        if (!poll) return;
        worker = std::thread([this] {
            while (!stopped) { poll(); pause(200); }
        });
    }
protected:
    std::function<void()> poll;
    static void pause(int milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
private:
    std::atomic<bool> stopped{false};
    std::thread worker;
};

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Only touches visible Win32 file dialogs belonging to this test process.
// Real output assertions live in nativePickers(), outside the input driver.
class NativeDialogInput : public NativeDialogInputWorker {
public:
    struct Choice { QString path; bool directory = false; };
    QList<Choice> choices;
    std::atomic<int> observed{0};
    HWND lastDialog = nullptr;
    ~NativeDialogInput() override { stop(); }
    explicit NativeDialogInput(QObject* parent) : NativeDialogInputWorker(parent) {
        poll = [this] {
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
            if (!dialog) { lastDialog = nullptr; return; }
            if (dialog == lastDialog || observed >= choices.size()) return;
            SetForegroundWindow(dialog);
            if (GetForegroundWindow() != dialog) return;
            lastDialog = dialog;
            const Choice choice = choices.at(observed++);
            qInfo() << "NATIVE_INPUT" << observed.load() << dialog << choice.path;
            if (choice.path.isEmpty()) { key(VK_ESCAPE); return; }
            chord(choice.directory ? VK_CONTROL : VK_MENU, choice.directory ? 'L' : 'N');
            pause(200);
            {
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
            }
            if (choice.directory) { pause(700); key(VK_RETURN); }
        };
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
#elif defined(Q_OS_LINUX)
class NativeDialogInput : public NativeDialogInputWorker {
public:
    struct Choice { QString path; bool directory = false; };
    QList<Choice> choices;
    std::atomic<int> observed{0};
    QString lastDialog;
    ~NativeDialogInput() override { stop(); }
    explicit NativeDialogInput(QObject* parent) : NativeDialogInputWorker(parent) {
        if (qgetenv("QTNETWORKCHAT_NATIVE_DIALOG_AUTOMATE") != "1") return;
        poll = [this] {
            const auto windows = run({"search", "--all", "--onlyvisible", "--pid",
                QString::number(QCoreApplication::applicationPid()), "--name",
                "^(选择文件|选择头像|选择接收/下载文件保存到|导出聊天记录)$"}).trimmed().split('\n');
            const QString dialog = windows.value(0);
            if (dialog.isEmpty()) { lastDialog.clear(); return; }
            if (dialog == lastDialog || observed >= choices.size()) return;
            // Works on isolated Xvfb desktops as well as desktops with a WM.
            run({"windowfocus", "--sync", dialog});
            if (run({"getwindowfocus"}).trimmed() != dialog) return;
            lastDialog = dialog;
            const Choice choice = choices.at(observed++);
            qInfo() << "NATIVE_INPUT" << observed.load() << dialog << choice.path;
            if (choice.path.isEmpty()) { run({"key", "Escape"}); return; }
            run({"key", "ctrl+l"});
            pause(200);
            run({"key", "ctrl+a"});
            run({"type", "--clearmodifiers", "--delay", "1", choice.path});
            run({"key", "Return"});
            // GTK's location entry first selects/navigates to the typed path;
            // a second Return activates Open/Select while the same picker lives.
            pause(700);
            if (run({"getwindowfocus"}).trimmed() == dialog) run({"key", "Return"});
        };
    }
private:
    static QString run(const QStringList& args) {
        QProcess process;
        process.start("xdotool", args);
        if (!process.waitForFinished(2000)) { process.kill(); return {}; }
        return QString::fromUtf8(process.readAllStandardOutput());
    }
};
#endif
