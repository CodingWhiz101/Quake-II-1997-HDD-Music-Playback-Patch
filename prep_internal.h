#pragma once
#include "prep_core.h"
#include <vector>

namespace q2prep {
namespace intern {

struct MusicRow {
    int track;
    unsigned long long bytes;
    std::wstring hash;
};

extern const double kFir[63];

std::wstring ErrorText(DWORD code);
bool FileSize(const std::wstring& path, unsigned long long& size);
bool EnsureDirectoryDeep(const std::wstring& path, std::wstring& error);
bool WriteAll(const std::wstring& path, const void* data, size_t bytes, std::wstring& error);
bool WriteAscii(const std::wstring& path, const std::string& text, std::wstring& error);
bool ReadBytes(const std::wstring& path, std::vector<unsigned char>& bytes, size_t maximum, std::wstring& error);
bool OwnedOutput(const std::wstring& path);
bool ValidateNoReparsePoints(const std::wstring& path, std::wstring& error);
bool RemoveTreeOwned(const std::wstring& path, std::wstring& error);
bool CopyOne(const std::wstring& source, const std::wstring& target, std::wstring& error);
bool CopyTree(const std::wstring& sourceDir, const std::wstring& destDir, std::wstring& error);
bool TreeSize(const std::wstring& path, unsigned long long& total);
void ConvertPcm16(const std::vector<short>& pcm, std::vector<short>& out);
short RoundPcm16(double sample);
bool IsCanonicalWavBytes(const unsigned char* header, size_t bytes, unsigned long long fileSize);
bool IsCanonicalWavFile(const std::wstring& path);
bool WriteCanonicalWav(const std::wstring& path, const std::vector<short>& interleaved, std::wstring& error);
bool DecodeOgg(const std::wstring& path, std::vector<short>& pcm, std::wstring& error);
int PatchWinmmImport(std::vector<unsigned char>& data);
bool BufferContains(const std::vector<unsigned char>& data, const char* text);
bool ExeIsPatched(const std::vector<unsigned char>& data);
bool EmbeddedWincd(const unsigned char*& data, size_t& bytes, std::wstring& error);
bool FreeBytes(const std::wstring& path, unsigned long long& available);
bool IsPathSafe(const std::wstring& source, const std::wstring& output, std::wstring& error);
bool TweakConfig(const std::wstring& path, std::wstring& error);
bool CopyGameFiles(const std::wstring& src, const std::wstring& dst, ProgressSink& progress, std::wstring& error);
bool WriteWin95Scripts(const std::wstring& root, bool exact, int tracks, std::wstring& error);
bool WriteMusicManifest(const std::wstring& music, const std::vector<MusicRow>& rows, std::wstring& error);
bool TrackAvailable(const std::wstring& root, int track, bool& hasOgg, bool& hasWav);
std::string NarrowAscii(const std::wstring& text);
void WriteU16(unsigned char* p, unsigned short v);
void WriteU32(unsigned char* p, unsigned long v);
unsigned long ReadU32(const unsigned char* p);
unsigned short ReadU16(const unsigned char* p);

extern const wchar_t* const kMarkerName;
extern const char* const kMarkerText;
extern const wchar_t* const kStockExeHash;
extern const wchar_t* const kPatchedExeHash;

}  // namespace intern
}  // namespace q2prep
