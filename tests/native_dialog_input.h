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
#include <UIAutomation.h>
#include <memory>

// Only touches visible Win32 file dialogs belonging to this test process.
// Real output assertions live in nativePickers(), outside the input driver.
class NativeDialogInput : public NativeDialogInputWorker {
public:
    struct Choice { QString path; bool directory = false; bool save = false; };
    QList<Choice> choices;
    std::atomic<int> observed{0};
    HWND lastDialog = nullptr;
    HWND diagnosedDialog = nullptr;
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
            if (!dialog) { lastDialog = nullptr; diagnosedDialog = nullptr; return; }
            if (dialog == lastDialog || observed >= choices.size()) return;
            const Choice choice = choices.at(observed.load());
            // Hosted runners need not have an input desktop. UI Automation
            // addresses the native picker controls without foreground focus or
            // global keystrokes, and remains scoped to this process's HWND.
            const bool diagnose = dialog != diagnosedDialog;
            diagnosedDialog = dialog;
            if (!choose(dialog, choice, diagnose)) return;
            lastDialog = dialog;
            ++observed;
            qInfo() << "NATIVE_INPUT" << observed.load() << dialog << choice.path;
        };
    }
private:
    struct ReleaseCom {
        template<class T> void operator()(T* value) const { if (value) value->Release(); }
    };
    template<class T> using ComOwner = std::unique_ptr<T, ReleaseCom>;
    static bool choose(HWND dialog, const Choice& choice, bool diagnose) {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialized)) {
            if (diagnose) qWarning() << "NATIVE_UIA COM initialization failed" << initialized;
            return false;
        }
        struct Uninitialize { ~Uninitialize() { CoUninitialize(); } } uninitialize;
        IUIAutomation* rawAutomation = nullptr;
        const HRESULT created = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                                 __uuidof(IUIAutomation), reinterpret_cast<void**>(&rawAutomation));
        if (FAILED(created)) {
            if (diagnose) qWarning() << "NATIVE_UIA creation failed" << created;
            return false;
        }
        ComOwner<IUIAutomation> automation(rawAutomation);
        IUIAutomationElement* rawRoot = nullptr;
        if (FAILED(automation->ElementFromHandle(dialog, &rawRoot))) return false;
        ComOwner<IUIAutomationElement> root(rawRoot);
        IUIAutomationCondition* rawCondition = nullptr;
        if (FAILED(automation->CreateTrueCondition(&rawCondition))) return false;
        ComOwner<IUIAutomationCondition> condition(rawCondition);
        IUIAutomationElementArray* rawElements = nullptr;
        if (FAILED(root->FindAll(TreeScope_Descendants, condition.get(), &rawElements))) return false;
        ComOwner<IUIAutomationElementArray> elements(rawElements);
        int length = 0;
        elements->get_Length(&length);
        ComOwner<IUIAutomationElement> input, button;
        for (int index = 0; index < length; ++index) {
            IUIAutomationElement* rawElement = nullptr;
            if (FAILED(elements->GetElement(index, &rawElement))) continue;
            ComOwner<IUIAutomationElement> element(rawElement);
            CONTROLTYPEID type = 0;
            BSTR rawName = nullptr;
            element->get_CurrentControlType(&type);
            element->get_CurrentName(&rawName);
            const QString name = QString::fromWCharArray(rawName ? rawName : L"");
            SysFreeString(rawName);
            if (diagnose && (type == UIA_EditControlTypeId || type == UIA_ButtonControlTypeId))
                qInfo() << "NATIVE_UIA control" << type << name;
            // The CI image is English. Match the path field, excluding the
            // Explorer address/search fields, and invoke the actual OS action.
            if (type == UIA_EditControlTypeId
                && (name.startsWith(QStringLiteral("File name"), Qt::CaseInsensitive)
                    || name.startsWith(QStringLiteral("Folder"), Qt::CaseInsensitive))) {
                input = std::move(element);
            } else if (type == UIA_ButtonControlTypeId
                       && (choice.path.isEmpty() ? name == QLatin1String("Cancel")
                           : choice.directory ? name == QLatin1String("Select Folder")
                           : choice.save ? name == QLatin1String("Save")
                           : name == QLatin1String("Open"))) {
                button = std::move(element);
            }
        }
        if (!button || (!choice.path.isEmpty() && !input)) return false;
        if (!choice.path.isEmpty()) {
            IUIAutomationValuePattern* rawValue = nullptr;
            if (FAILED(input->GetCurrentPatternAs(UIA_ValuePatternId, __uuidof(IUIAutomationValuePattern),
                                                  reinterpret_cast<void**>(&rawValue)))) return false;
            ComOwner<IUIAutomationValuePattern> value(rawValue);
            const QString path = QDir::toNativeSeparators(choice.path);
            // The Save picker commits its filename when focus leaves the edit.
            // SetValue alone updates the accessibility value but need not
            // commit the shell dialog's pending filename before Invoke.
            if (choice.save) input->SetFocus();
            BSTR nativePath = SysAllocStringLen(reinterpret_cast<LPCWSTR>(path.utf16()),
                                               static_cast<UINT>(path.size()));
            if (!nativePath) return false;
            const HRESULT assigned = value->SetValue(nativePath);
            SysFreeString(nativePath);
            if (FAILED(assigned)) return false;
            if (choice.save) button->SetFocus();
            BSTR actualPath = nullptr;
            if (FAILED(value->get_CurrentValue(&actualPath))) return false;
            const QString assignedPath = QString::fromWCharArray(actualPath ? actualPath : L"");
            SysFreeString(actualPath);
            if (assignedPath != path) {
                if (diagnose) qWarning() << "NATIVE_UIA path did not persist" << assignedPath << path;
                return false;
            }
        }
        IUIAutomationInvokePattern* rawInvoke = nullptr;
        if (FAILED(button->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern),
                                              reinterpret_cast<void**>(&rawInvoke)))) return false;
        ComOwner<IUIAutomationInvokePattern> invoke(rawInvoke);
        return SUCCEEDED(invoke->Invoke());
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
