#define _CRT_SECURE_NO_WARNINGS
#include "prep_internal.h"

#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <cstring>
#include <cwctype>

#pragma comment(lib, "advapi32.lib")

namespace q2prep {
namespace intern {

const wchar_t* const kMarkerName = L"Q2WIN95.PREP";
const char* const kMarkerText = "Q2Win95Prep output v1\r\n";
const wchar_t* const kStockExeHash = L"9C7AE7872321996FB72FCCC081636821F2FB78132368CC321361E732AE66913E";
const wchar_t* const kPatchedExeHash = L"63E9DEFD0CD6BC20A3B92DD0928B290B517000D85474C223F033683CDBDFF33F";

std::wstring ErrorText(DWORD code) {
    wchar_t* buffer = 0;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, 0, code, 0,
                   reinterpret_cast<wchar_t*>(&buffer), 0, 0);
    std::wstring result = buffer ? buffer : L"Unknown error";
    if (buffer) LocalFree(buffer);
    while (!result.empty() && (result[result.size() - 1] == L'\r' || result[result.size() - 1] == L'\n'))
        result.erase(result.size() - 1);
    return result;
}

bool FileSize(const std::wstring& path, unsigned long long& size) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return true;
}

unsigned short ReadU16(const unsigned char* p) {
    return static_cast<unsigned short>(p[0] | (p[1] << 8));
}

unsigned long ReadU32(const unsigned char* p) {
    return static_cast<unsigned long>(p[0]) |
           (static_cast<unsigned long>(p[1]) << 8) |
           (static_cast<unsigned long>(p[2]) << 16) |
           (static_cast<unsigned long>(p[3]) << 24);
}

void WriteU16(unsigned char* p, unsigned short v) {
    p[0] = (unsigned char)(v & 255);
    p[1] = (unsigned char)(v >> 8);
}

void WriteU32(unsigned char* p, unsigned long v) {
    p[0] = (unsigned char)(v & 255);
    p[1] = (unsigned char)((v >> 8) & 255);
    p[2] = (unsigned char)((v >> 16) & 255);
    p[3] = (unsigned char)((v >> 24) & 255);
}

bool EnsureDirectoryDeep(const std::wstring& path, std::wstring& error) {
    if (path.empty()) return true;
    if (DirectoryExists(path)) return true;
    std::wstring parent = ParentPath(path);
    if (parent.empty() || parent == path) {
        error = L"Could not create " + path;
        return false;
    }
    if (parent.size() > 3 && !EnsureDirectoryDeep(parent, error)) return false;
    if (CreateDirectoryW(path.c_str(), 0) || DirectoryExists(path)) return true;
    error = L"Could not create " + path + L": " + ErrorText(GetLastError());
    return false;
}

bool WriteAll(const std::wstring& path, const void* data, size_t bytes, std::wstring& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Could not create " + path + L": " + ErrorText(GetLastError());
        return false;
    }
    const unsigned char* cursor = static_cast<const unsigned char*>(data);
    size_t left = bytes;
    bool ok = true;
    while (left) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(left, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(file, cursor, chunk, &written, 0) || written != chunk) { ok = false; break; }
        cursor += written;
        left -= written;
    }
    if (!CloseHandle(file)) ok = false;
    if (!ok) error = L"Could not write " + path + L": " + ErrorText(GetLastError());
    return ok;
}

bool WriteAscii(const std::wstring& path, const std::string& text, std::wstring& error) {
    return WriteAll(path, text.data(), text.size(), error);
}

bool ReadBytes(const std::wstring& path, std::vector<unsigned char>& bytes, size_t maximum, std::wstring& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, 0);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Could not open " + path + L": " + ErrorText(GetLastError());
        return false;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        static_cast<unsigned long long>(size.QuadPart) > maximum) {
        CloseHandle(file);
        error = L"File has an invalid size: " + path;
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    bool ok = ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, 0) != FALSE &&
              read == bytes.size();
    CloseHandle(file);
    if (!ok) { error = L"Could not read " + path; bytes.clear(); }
    return ok;
}

bool ReadSmallAscii(const std::wstring& path, std::string& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (file == INVALID_HANDLE_VALUE) return false;
    char buffer[256];
    DWORD read = 0;
    bool ok = ReadFile(file, buffer, sizeof(buffer), &read, 0) != FALSE;
    CloseHandle(file);
    if (ok) text.assign(buffer, buffer + read);
    return ok;
}

bool OwnedOutput(const std::wstring& path) {
    std::string text;
    return ReadSmallAscii(JoinPath(path, kMarkerName), text) &&
           text.find("Q2Win95Prep output v1") == 0;
}

bool ValidateNoReparsePoints(const std::wstring& path, std::wstring& error) {
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(JoinPath(path, L"*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return true;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        std::wstring child = JoinPath(path, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            error = L"Refusing to replace an output containing a reparse point: " + child;
            FindClose(find);
            return false;
        }
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !ValidateNoReparsePoints(child, error)) {
            FindClose(find);
            return false;
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return true;
}

bool DeleteTree(const std::wstring& path, std::wstring& error) {
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(JoinPath(path, L"*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            std::wstring child = JoinPath(path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!DeleteTree(child, error)) { FindClose(find); return false; }
            } else {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileW(child.c_str())) {
                    error = L"Could not remove " + child + L": " + ErrorText(GetLastError());
                    FindClose(find);
                    return false;
                }
            }
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!RemoveDirectoryW(path.c_str())) {
        error = L"Could not remove " + path + L": " + ErrorText(GetLastError());
        return false;
    }
    return true;
}

bool RemoveTreeOwned(const std::wstring& path, std::wstring& error) {
    if (!OwnedOutput(path)) { error = L"Refusing to remove an unowned directory: " + path; return false; }
    if (!ValidateNoReparsePoints(path, error)) return false;
    return DeleteTree(path, error);
}

bool CopyOne(const std::wstring& source, const std::wstring& target, std::wstring& error) {
    std::wstring parent = ParentPath(target);
    if (!parent.empty() && !EnsureDirectoryDeep(parent, error)) return false;
    if (!CopyFileW(source.c_str(), target.c_str(), FALSE)) {
        error = L"Could not copy " + source + L": " + ErrorText(GetLastError());
        return false;
    }
    return true;
}

bool CopyTree(const std::wstring& sourceDir, const std::wstring& destDir, std::wstring& error) {
    if (!DirectoryExists(sourceDir)) return true;
    if (!EnsureDirectoryDeep(destDir, error)) return false;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(JoinPath(sourceDir, L"*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return true;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            error = L"Refusing to copy a reparse point: " + JoinPath(sourceDir, fd.cFileName);
            FindClose(find);
            return false;
        }
        std::wstring src = JoinPath(sourceDir, fd.cFileName);
        std::wstring dst = JoinPath(destDir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!CopyTree(src, dst, error)) { FindClose(find); return false; }
        } else if (!CopyOne(src, dst, error)) {
            FindClose(find);
            return false;
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return true;
}

bool FreeBytes(const std::wstring& path, unsigned long long& available) {
    ULARGE_INTEGER freeAvailable, total, totalFree;
    if (!GetDiskFreeSpaceExW(path.c_str(), &freeAvailable, &total, &totalFree)) return false;
    available = freeAvailable.QuadPart;
    return true;
}

std::wstring Upper(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towupper);
    return value;
}

bool IsPathSafe(const std::wstring& source, const std::wstring& output, std::wstring& error) {
    if (source.size() > 200 || output.size() > 220) {
        error = L"The source or output path is too long for safe Win7/Win95 use.";
        return false;
    }
    wchar_t src[MAX_PATH], out[MAX_PATH];
    if (!GetFullPathNameW(source.c_str(), MAX_PATH, src, 0) ||
        !GetFullPathNameW(output.c_str(), MAX_PATH, out, 0)) {
        error = L"Could not resolve source/output paths.";
        return false;
    }
    std::wstring su = Upper(src), ou = Upper(out);
    if (su == ou || Upper(ParentPath(out)) != su) {
        error = L"Output must be a direct child of the Quake II game directory.";
        return false;
    }
    return true;
}

std::string NarrowAscii(const std::wstring& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
        out += text[i] < 128 ? static_cast<char>(text[i]) : '?';
    return out;
}

}  // namespace intern

std::wstring JoinPath(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) return right;
    if (left[left.size() - 1] == L'\\' || left[left.size() - 1] == L'/') return left + right;
    return left + L"\\" + right;
}

std::wstring ParentPath(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? L"" : path.substr(0, p);
}

std::wstring ExecutableDirectory() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(0, path, MAX_PATH);
    return n ? ParentPath(std::wstring(path, n)) : L"";
}

bool DirectoryExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool PathIsFile(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring Sha256File(const std::wstring& path, std::wstring* error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, 0);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) *error = L"Could not hash " + path;
        return L"";
    }
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    std::wstring result;
    if (!CryptAcquireContextW(&prov, 0, 0, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
        !CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
        if (error) *error = L"Windows SHA-256 initialization failed";
    } else {
        BYTE buffer[65536];
        DWORD read = 0;
        bool ok = true;
        while (ReadFile(file, buffer, sizeof(buffer), &read, 0) && read)
            if (!CryptHashData(hash, buffer, read, 0)) { ok = false; break; }
        BYTE digest[32];
        DWORD len = sizeof(digest);
        if (ok && CryptGetHashParam(hash, HP_HASHVAL, digest, &len, 0)) {
            static const wchar_t hex[] = L"0123456789ABCDEF";
            for (DWORD i = 0; i < len; ++i) {
                result += hex[digest[i] >> 4];
                result += hex[digest[i] & 15];
            }
        } else if (error) {
            *error = L"Could not hash " + path;
        }
    }
    if (hash) CryptDestroyHash(hash);
    if (prov) CryptReleaseContext(prov, 0);
    CloseHandle(file);
    return result;
}

}  // namespace q2prep
