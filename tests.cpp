#include <windows.h>
#include <iostream>
#include "prep_core.h"

class ConsoleProgress : public q2prep::ProgressSink {
public:
    void Update(int percent, const std::wstring& message) {
        std::wcout << L"[" << percent << L"%] " << message << std::endl;
    }
    bool Cancelled() const { return false; }
};

int wmain(int argc, wchar_t** argv) {
    std::wstring report = q2prep::JoinPath(q2prep::ExecutableDirectory(), L"SELFTEST.TXT");
    std::wstring summary;
    bool ok = q2prep::RunSelfTests(report, summary);
    std::wcout << summary << L" Report: " << report << std::endl;
    if (!ok || argc == 1) return ok ? 0 : 1;

    std::wstring source = argv[1];
    q2prep::SourceStatus status = q2prep::InspectSource(source);
    std::wstring output = q2prep::JoinPath(status.resolvedRoot.empty() ? source : status.resolvedRoot,
                                           L"Copy-To-Windows-95");
    ConsoleProgress progress;
    q2prep::PrepareResult result = q2prep::Prepare(source, output, true, true, progress);
    std::wcout << result.message << std::endl;
    if (!result.success) return 2;
    if (result.trackCount != 20) {
        std::wcout << L"Expected 20 tracks, got " << result.trackCount << std::endl;
        return 3;
    }
    if (!q2prep::PathIsFile(q2prep::JoinPath(result.outputPath, L"wincd.dll")) ||
        !q2prep::PathIsFile(q2prep::JoinPath(result.outputPath, L"quake2.exe")) ||
        !q2prep::PathIsFile(q2prep::JoinPath(result.outputPath, L"quake2.exe.bak")) ||
        !q2prep::PathIsFile(q2prep::JoinPath(result.outputPath, L"SETUP95.BAT")) ||
        !q2prep::PathIsFile(q2prep::JoinPath(result.outputPath, L"music\\Track02.wav")) ||
        !q2prep::PathIsFile(q2prep::JoinPath(result.outputPath, L"music\\Track21.wav"))) {
        std::wcout << L"Prepared output is missing required files." << std::endl;
        return 4;
    }
    return 0;
}
