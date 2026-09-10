#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace q2prep {

struct SourceStatus {
    bool usable;
    bool exactVersion;
    unsigned long long estimatedBytes;
    std::wstring resolvedRoot;
    std::vector<std::wstring> errors;
    std::vector<std::wstring> warnings;
};

struct ProgressSink {
    virtual ~ProgressSink() {}
    virtual void Update(int percent, const std::wstring& message) = 0;
    virtual bool Cancelled() const = 0;
};

struct PrepareResult {
    bool success;
    bool cancelled;
    int trackCount;
    std::wstring outputPath;
    std::wstring message;
};

std::wstring JoinPath(const std::wstring& left, const std::wstring& right);
std::wstring ParentPath(const std::wstring& path);
std::wstring ExecutableDirectory();
bool DirectoryExists(const std::wstring& path);
bool PathIsFile(const std::wstring& path);
std::wstring Sha256File(const std::wstring& path, std::wstring* error = 0);
std::wstring ResolveGameRoot(const std::wstring& selected);

SourceStatus InspectSource(const std::wstring& selected);
PrepareResult Prepare(const std::wstring& selected,
                      const std::wstring& output,
                      bool allowUnknown,
                      bool replaceExisting,
                      ProgressSink& progress);

bool RunSelfTests(const std::wstring& reportPath, std::wstring& summary);
void CleanupOwnedTemporary(const std::wstring& source);

}  // namespace q2prep
