#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <new>

#include <new>
#include <sstream>
#include <string>

#include "prep_core.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

enum {
    IDC_SOURCE = 1001, IDC_BROWSE, IDC_VERSION, IDC_OUTPUT, IDC_SPACE,
    IDC_PROGRESS, IDC_ACTION, IDC_CANCEL, IDC_OPEN
};
const UINT WM_PREP_PROGRESS = WM_APP + 10;
const UINT WM_PREP_DONE = WM_APP + 11;

HWND gWindow = 0, gSource = 0, gVersion = 0, gOutput = 0, gSpace = 0;
HWND gProgress = 0, gAction = 0, gCancel = 0, gOpen = 0;
HANDLE gThread = 0;
volatile LONG gCancelled = 0;
std::wstring gLastOutput;

std::wstring WindowText(HWND control) {
    int n = GetWindowTextLengthW(control);
    std::wstring value(n + 1, L'\0');
    if (n) GetWindowTextW(control, &value[0], n + 1);
    value.resize(n);
    return value;
}

void SetFont(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

std::wstring FormatMiB(unsigned long long bytes) {
    std::wstringstream s;
    s.setf(std::ios::fixed);
    s.precision(1);
    s << (bytes / (1024.0 * 1024.0)) << L" MiB";
    return s.str();
}

std::wstring BrowseForFolder(HWND owner, const std::wstring& initial) {
    struct Callback {
        static int CALLBACK Run(HWND hwnd, UINT msg, LPARAM, LPARAM data) {
            if (msg == BFFM_INITIALIZED && data)
                SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
            return 0;
        }
    };
    BROWSEINFOW bi = {0};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Select the GOG Quake II game directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = Callback::Run;
    bi.lParam = reinterpret_cast<LPARAM>(initial.c_str());
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    wchar_t path[MAX_PATH] = L"";
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return path;
}

void RefreshStatus() {
    std::wstring source = WindowText(gSource);
    q2prep::SourceStatus st = q2prep::InspectSource(source);
    std::wstring root = st.resolvedRoot.empty() ? source : st.resolvedRoot;
    std::wstring output = q2prep::JoinPath(root, L"Copy-To-Windows-95");
    SetWindowTextW(gOutput, (L"Output: " + output).c_str());
    if (!st.usable) {
        std::wstring text = L"Source: not ready";
        if (!st.errors.empty()) text += L" - " + st.errors[0];
        SetWindowTextW(gVersion, text.c_str());
    } else if (st.exactVersion) {
        SetWindowTextW(gVersion, L"Source: verified GOG Quake II 3.20 (Quad Damage)");
    } else {
        SetWindowTextW(gVersion, L"Source: usable, but hashes differ from the tested GOG 3.20 release");
    }
    ULARGE_INTEGER freeAvail, total, totalFree;
    if (GetDiskFreeSpaceExW(root.c_str(), &freeAvail, &total, &totalFree))
        SetWindowTextW(gSpace, (L"Estimated output + workspace: " + FormatMiB(st.estimatedBytes) +
                               L"; available: " + FormatMiB(freeAvail.QuadPart)).c_str());
    else
        SetWindowTextW(gSpace, (L"Estimated output + workspace: " + FormatMiB(st.estimatedBytes)).c_str());
    EnableWindow(gAction, st.usable && gThread == 0);
}

struct ProgressMessage { int percent; std::wstring text; };
struct DoneMessage { q2prep::PrepareResult result; };

class GuiProgress : public q2prep::ProgressSink {
public:
    explicit GuiProgress(HWND hwnd) : hwnd_(hwnd) {}
    void Update(int percent, const std::wstring& message) {
        ProgressMessage* p = new ProgressMessage;
        p->percent = percent;
        p->text = message;
        PostMessageW(hwnd_, WM_PREP_PROGRESS, 0, reinterpret_cast<LPARAM>(p));
    }
    bool Cancelled() const { return InterlockedCompareExchange(const_cast<LONG*>(&gCancelled), 0, 0) != 0; }
private:
    HWND hwnd_;
};

struct Work { std::wstring source, output; bool allowUnknown, replaceExisting; };

DWORD WINAPI Worker(LPVOID parameter) {
    Work* w = static_cast<Work*>(parameter);
    GuiProgress progress(gWindow);
    DoneMessage* done = new DoneMessage;
    try {
        done->result = q2prep::Prepare(w->source, w->output, w->allowUnknown, w->replaceExisting, progress);
    } catch (const std::bad_alloc&) {
        q2prep::CleanupOwnedTemporary(w->source);
        done->result.success = false;
        done->result.cancelled = false;
        done->result.outputPath = w->output;
        done->result.message = L"Not enough memory to decode and convert the selected track.";
    } catch (...) {
        q2prep::CleanupOwnedTemporary(w->source);
        done->result.success = false;
        done->result.cancelled = false;
        done->result.outputPath = w->output;
        done->result.message = L"An unexpected conversion error occurred; the source and previous output were retained.";
    }
    delete w;
    PostMessageW(gWindow, WM_PREP_DONE, 0, reinterpret_cast<LPARAM>(done));
    return 0;
}

void StartPreparation() {
    std::wstring source = WindowText(gSource);
    q2prep::SourceStatus st = q2prep::InspectSource(source);
    if (!st.usable) {
        std::wstring text = L"The source is incomplete:\n\n";
        for (size_t i = 0; i < st.errors.size(); ++i) text += L"- " + st.errors[i] + L"\n";
        MessageBoxW(gWindow, text.c_str(), L"Cannot prepare", MB_OK | MB_ICONERROR);
        return;
    }
    bool allowUnknown = st.exactVersion;
    if (!st.exactVersion) {
        std::wstring text = L"This installation differs from the tested GOG Quake II 3.20:\n\n";
        for (size_t i = 0; i < st.warnings.size(); ++i) text += L"- " + st.warnings[i] + L"\n";
        text += L"\nSoundtrack files must still be 44.1 kHz stereo OGG or canonical 22 kHz WAV. Continue?";
        if (MessageBoxW(gWindow, text.c_str(), L"Unverified source", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
        allowUnknown = true;
    }
    std::wstring output = q2prep::JoinPath(st.resolvedRoot, L"Copy-To-Windows-95");
    bool replace = false;
    if (q2prep::DirectoryExists(output)) {
        if (MessageBoxW(gWindow,
                        L"Copy-To-Windows-95 already exists. It will be replaced only if it was created by this utility. Continue?",
                        L"Replace generated output", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
        replace = true;
    }
    Work* work = new Work;
    work->source = source;
    work->output = output;
    work->allowUnknown = allowUnknown;
    work->replaceExisting = replace;
    InterlockedExchange(const_cast<LONG*>(&gCancelled), 0);
    gLastOutput.clear();
    EnableWindow(gAction, FALSE);
    EnableWindow(gOpen, FALSE);
    EnableWindow(gCancel, TRUE);
    EnableWindow(gSource, FALSE);
    SendMessageW(gProgress, PBM_SETPOS, 0, 0);
    SetWindowTextW(gAction, L"Preparing...");
    gThread = CreateThread(0, 0, Worker, work, 0, 0);
    if (!gThread) {
        delete work;
        EnableWindow(gAction, TRUE);
        EnableWindow(gCancel, FALSE);
        EnableWindow(gSource, TRUE);
        RefreshStatus();
        SetWindowTextW(gAction, L"Prepare");
        MessageBoxW(gWindow, L"Could not start the preparation worker.", L"Error", MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HWND title = CreateWindowW(L"STATIC", L"Quake II Win95 Preparation Utility",
                                   WS_CHILD | WS_VISIBLE, 20, 16, 560, 24, hwnd, 0, 0, 0);
        SetFont(title, font);
        HWND note = CreateWindowW(L"STATIC",
                                  L"Select a GOG Quake II folder. The original installation will not be modified.",
                                  WS_CHILD | WS_VISIBLE, 20, 44, 600, 20, hwnd, 0, 0, 0);
        SetFont(note, font);
        gSource = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  20, 72, 500, 24, hwnd, reinterpret_cast<HMENU>(IDC_SOURCE), 0, 0);
        SetFont(gSource, font);
        HWND browse = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE, 530, 71, 90, 26,
                                    hwnd, reinterpret_cast<HMENU>(IDC_BROWSE), 0, 0);
        SetFont(browse, font);
        gVersion = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 110, 600, 20, hwnd,
                                 reinterpret_cast<HMENU>(IDC_VERSION), 0, 0);
        SetFont(gVersion, font);
        gOutput = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 136, 600, 20, hwnd,
                                reinterpret_cast<HMENU>(IDC_OUTPUT), 0, 0);
        SetFont(gOutput, font);
        gSpace = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 162, 600, 20, hwnd,
                               reinterpret_cast<HMENU>(IDC_SPACE), 0, 0);
        SetFont(gSpace, font);
        gProgress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 20, 194, 600, 22,
                                    hwnd, reinterpret_cast<HMENU>(IDC_PROGRESS), 0, 0);
        SendMessageW(gProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        gAction = CreateWindowW(L"BUTTON", L"Prepare", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                20, 234, 110, 30, hwnd, reinterpret_cast<HMENU>(IDC_ACTION), 0, 0);
        SetFont(gAction, font);
        gCancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_DISABLED, 140, 234, 90, 30,
                                hwnd, reinterpret_cast<HMENU>(IDC_CANCEL), 0, 0);
        SetFont(gCancel, font);
        gOpen = CreateWindowW(L"BUTTON", L"Open Folder", WS_CHILD | WS_VISIBLE | WS_DISABLED, 240, 234, 110, 30,
                              hwnd, reinterpret_cast<HMENU>(IDC_OPEN), 0, 0);
        SetFont(gOpen, font);
        SetWindowTextW(gSource, q2prep::ResolveGameRoot(q2prep::ExecutableDirectory()).c_str());
        RefreshStatus();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BROWSE: {
            std::wstring p = BrowseForFolder(hwnd, WindowText(gSource));
            if (!p.empty()) {
                SetWindowTextW(gSource, p.c_str());
                RefreshStatus();
            }
            break;
        }
        case IDC_ACTION:
            StartPreparation();
            break;
        case IDC_CANCEL:
            InterlockedExchange(const_cast<LONG*>(&gCancelled), 1);
            EnableWindow(gCancel, FALSE);
            SetWindowTextW(gAction, L"Cancelling...");
            break;
        case IDC_OPEN:
            if (!gLastOutput.empty()) ShellExecuteW(hwnd, L"open", gLastOutput.c_str(), 0, 0, SW_SHOWNORMAL);
            break;
        }
        return 0;
    case WM_PREP_PROGRESS: {
        ProgressMessage* p = reinterpret_cast<ProgressMessage*>(lp);
        SendMessageW(gProgress, PBM_SETPOS, p->percent, 0);
        SetWindowTextW(gVersion, p->text.c_str());
        delete p;
        return 0;
    }
    case WM_PREP_DONE: {
        DoneMessage* d = reinterpret_cast<DoneMessage*>(lp);
        if (gThread) {
            CloseHandle(gThread);
            gThread = 0;
        }
        EnableWindow(gCancel, FALSE);
        EnableWindow(gSource, TRUE);
        SetWindowTextW(gAction, L"Prepare");
        if (d->result.success) {
            gLastOutput = d->result.outputPath;
            EnableWindow(gOpen, TRUE);
            SendMessageW(gProgress, PBM_SETPOS, 100, 0);
            MessageBoxW(hwnd, d->result.message.c_str(), L"Preparation complete", MB_OK | MB_ICONINFORMATION);
        } else {
            EnableWindow(gOpen, FALSE);
            MessageBoxW(hwnd, d->result.message.c_str(),
                        d->result.cancelled ? L"Cancelled" : L"Preparation failed",
                        MB_OK | (d->result.cancelled ? MB_ICONINFORMATION : MB_ICONERROR));
        }
        delete d;
        RefreshStatus();
        return 0;
    }
    case WM_CLOSE:
        if (gThread) {
            if (MessageBoxW(hwnd, L"Preparation is running. Cancel and close?", L"Confirm",
                            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                return 0;
            InterlockedExchange(const_cast<LONG*>(&gCancelled), 1);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    INITCOMMONCONTROLSEX ic = {sizeof(ic), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&ic);
    CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"Q2Win95PrepWindow";
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassW(&wc)) return 1;
    gWindow = CreateWindowExW(0, wc.lpszClassName, L"Quake II Win95 Prep",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, 660, 320, 0, 0, instance, 0);
    if (!gWindow) return 1;
    ShowWindow(gWindow, show);
    UpdateWindow(gWindow);
    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
