#define _CRT_SECURE_NO_WARNINGS
#include "prep_internal.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace q2prep {
namespace intern {

static const wchar_t* const kRootOptional[] = {
    L"3dfxgl.dll", L"ref_gl.dll", L"ref_soft.dll", L"pvrgl.dll", L"q2.ico",
    L"readme.txt", L"license.txt", L"3.20_Changes.txt"
};

bool CopyIfFile(const std::wstring& source, const std::wstring& target, std::wstring& error) {
    DWORD attr = GetFileAttributesW(source.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return true;
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
        error = L"Refusing to copy a reparse point: " + source;
        return false;
    }
    if (attr & FILE_ATTRIBUTE_DIRECTORY) return true;
    return CopyOne(source, target, error);
}

bool CopyRequired(const std::wstring& source, const std::wstring& target, std::wstring& error) {
    if (!PathIsFile(source)) {
        error = L"Missing required file: " + source;
        return false;
    }
    DWORD attr = GetFileAttributesW(source.c_str());
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
        error = L"Refusing to copy a reparse point: " + source;
        return false;
    }
    return CopyOne(source, target, error);
}

bool TreeSize(const std::wstring& path, unsigned long long& total) {
    if (!DirectoryExists(path)) return true;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(JoinPath(path, L"*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return true;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        std::wstring child = JoinPath(path, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) TreeSize(child, total);
        else total += (static_cast<unsigned long long>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return true;
}

bool CopyGameFiles(const std::wstring& src, const std::wstring& dst, ProgressSink& progress, std::wstring& error) {
    progress.Update(4, L"Copying Quake II 3.20 files...");
    for (size_t i = 0; i < sizeof(kRootOptional) / sizeof(kRootOptional[0]); ++i) {
        if (progress.Cancelled()) { error = L"Cancelled."; return false; }
        if (!CopyIfFile(JoinPath(src, kRootOptional[i]), JoinPath(dst, kRootOptional[i]), error))
            return false;
    }

    progress.Update(8, L"Copying documentation...");
    if (DirectoryExists(JoinPath(src, L"docs")) &&
        !CopyTree(JoinPath(src, L"docs"), JoinPath(dst, L"docs"), error))
        return false;

    progress.Update(12, L"Copying baseq2...");
    std::wstring sBase = JoinPath(src, L"baseq2");
    std::wstring dBase = JoinPath(dst, L"baseq2");
    if (!CopyRequired(JoinPath(sBase, L"pak0.pak"), JoinPath(dBase, L"pak0.pak"), error)) return false;
    if (!CopyIfFile(JoinPath(sBase, L"pak1.pak"), JoinPath(dBase, L"pak1.pak"), error)) return false;
    if (!CopyIfFile(JoinPath(sBase, L"pak2.pak"), JoinPath(dBase, L"pak2.pak"), error)) return false;
    if (!CopyIfFile(JoinPath(sBase, L"gamex86.dll"), JoinPath(dBase, L"gamex86.dll"), error)) return false;
    if (!CopyIfFile(JoinPath(sBase, L"maps.lst"), JoinPath(dBase, L"maps.lst"), error)) return false;
    if (!CopyIfFile(JoinPath(sBase, L"config.cfg"), JoinPath(dBase, L"config.cfg"), error)) return false;
    if (DirectoryExists(JoinPath(sBase, L"video")) &&
        !CopyTree(JoinPath(sBase, L"video"), JoinPath(dBase, L"video"), error))
        return false;
    static const wchar_t* players[] = { L"male", L"female", L"cyborg" };
    for (int p = 0; p < 3; ++p) {
        std::wstring sPlay = JoinPath(JoinPath(sBase, L"players"), players[p]);
        if (DirectoryExists(sPlay) &&
            !CopyTree(sPlay, JoinPath(JoinPath(dBase, L"players"), players[p]), error))
            return false;
    }

    static const wchar_t* packs[] = { L"xatrix", L"rogue" };
    for (int i = 0; i < 2; ++i) {
        if (progress.Cancelled()) { error = L"Cancelled."; return false; }
        progress.Update(16 + i * 2, std::wstring(L"Copying ") + packs[i] + L"...");
        std::wstring sPack = JoinPath(src, packs[i]);
        std::wstring dPack = JoinPath(dst, packs[i]);
        if (!CopyRequired(JoinPath(sPack, L"pak0.pak"), JoinPath(dPack, L"pak0.pak"), error)) return false;
        if (!CopyIfFile(JoinPath(sPack, L"gamex86.dll"), JoinPath(dPack, L"gamex86.dll"), error)) return false;
        if (!CopyIfFile(JoinPath(sPack, L"config.cfg"), JoinPath(dPack, L"config.cfg"), error)) return false;
        if (DirectoryExists(JoinPath(sPack, L"docs")) &&
            !CopyTree(JoinPath(sPack, L"docs"), JoinPath(dPack, L"docs"), error))
            return false;
        if (DirectoryExists(JoinPath(sPack, L"video")) &&
            !CopyTree(JoinPath(sPack, L"video"), JoinPath(dPack, L"video"), error))
            return false;
    }
    return true;
}

bool LineSetsKey(const std::string& line, const char* key) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i + 4 <= line.size() && (line[i] == 's' || line[i] == 'S') &&
        (line[i + 1] == 'e' || line[i + 1] == 'E') &&
        (line[i + 2] == 't' || line[i + 2] == 'T') &&
        (line[i + 3] == ' ' || line[i + 3] == '\t'))
        i += 4;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    size_t keyLen = strlen(key);
    if (i + keyLen > line.size()) return false;
    for (size_t k = 0; k < keyLen; ++k)
        if (tolower(static_cast<unsigned char>(line[i + k])) !=
            tolower(static_cast<unsigned char>(key[k])))
            return false;
    i += keyLen;
    return i == line.size() || line[i] == ' ' || line[i] == '\t' || line[i] == '"';
}

bool TweakConfig(const std::wstring& path, std::wstring& error) {
    if (!PathIsFile(path)) return true;
    std::vector<unsigned char> bytes;
    if (!ReadBytes(path, bytes, 1024 * 1024, error)) return false;
    std::string text(bytes.begin(), bytes.end());
    std::istringstream in(text);
    std::ostringstream out;
    std::string line;
    bool any = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        const char* replacement = 0;
        if (LineSetsKey(line, "gl_driver")) replacement = "set gl_driver \"3dfxgl\"";
        else if (LineSetsKey(line, "gl_mode")) replacement = "set gl_mode \"3\"";
        else if (LineSetsKey(line, "gl_swapinterval")) replacement = "set gl_swapinterval \"0\"";
        else if (LineSetsKey(line, "vid_fullscreen")) replacement = "set vid_fullscreen \"1\"";
        else if (LineSetsKey(line, "viewsize")) replacement = "set viewsize \"100\"";
        if (replacement) {
            out << replacement << "\r\n";
            any = true;
        } else {
            out << line << "\r\n";
        }
    }
    if (!any) return true;
    return WriteAscii(path, out.str(), error);
}

bool WriteMusicManifest(const std::wstring& music, const std::vector<MusicRow>& rows, std::wstring& error) {
    std::ostringstream s;
    s << "Quake II Win95 PCM soundtrack\r\n";
    s << "rate 22050  channels 2  bits 16\r\n";
    s << "track\twav\tbytes\tsha256\r\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        char name[32];
        sprintf(name, "Track%02d.wav", rows[i].track);
        s << rows[i].track << "\t" << name << "\t" << rows[i].bytes << "\t"
          << NarrowAscii(rows[i].hash) << "\r\n";
    }
    return WriteAscii(JoinPath(music, L"MANIFEST.TXT"), s.str(), error);
}

bool WriteWin95Scripts(const std::wstring& root, bool exact, int tracks, std::wstring& error) {
    if (!WriteAscii(JoinPath(root, L"XATRIX.BAT"),
                    "@echo off\r\nquake2.exe +set game xatrix\r\n", error))
        return false;
    if (!WriteAscii(JoinPath(root, L"ROGUE.BAT"),
                    "@echo off\r\nquake2.exe +set game rogue\r\n", error))
        return false;
    if (!WriteAscii(JoinPath(root, L"SETUP95.BAT"),
                    "@echo off\r\n"
                    "echo SETUP95.BAT is optional.\r\n"
                    "echo Starting quake2.exe also copies this PC's mixer to winmmsys.dll.\r\n"
                    "echo Copying %windir%\\SYSTEM\\winmm.dll to winmmsys.dll...\r\n"
                    "copy /y %windir%\\SYSTEM\\winmm.dll winmmsys.dll\r\n"
                    "if errorlevel 1 goto fail\r\n"
                    "echo OK: winmmsys.dll is ready. You can run VERIFY95.BAT or just start the game.\r\n"
                    "goto end\r\n"
                    ":fail\r\n"
                    "echo FAIL: Could not copy %windir%\\SYSTEM\\winmm.dll to winmmsys.dll.\r\n"
                    "echo Starting quake2.exe will try the same copy.\r\n"
                    ":end\r\n"
                    "pause\r\n", error))
        return false;
    if (!WriteAscii(JoinPath(root, L"VERIFY95.BAT"),
                    "@echo off\r\n"
                    "echo Checking Quake II Windows 95 HDD install...\r\n"
                    "if not exist quake2.exe goto failed\r\n"
                    "if not exist wincd.dll goto failed\r\n"
                    "if not exist music\\Track02.wav goto failed\r\n"
                    "if not exist music\\Track11.wav goto failed\r\n"
                    "if not exist music\\Track12.wav goto failed\r\n"
                    "if not exist music\\Track21.wav goto failed\r\n"
                    "if not exist baseq2\\pak0.pak goto failed\r\n"
                    "if not exist xatrix\\pak0.pak goto failed\r\n"
                    "if not exist rogue\\pak0.pak goto failed\r\n"
                    "if exist winmm.dll goto leftover\r\n"
                    "if exist libvorbisfile-3.dll goto leftover\r\n"
                    "if exist libvorbis-0.dll goto leftover\r\n"
                    "if exist libogg-0.dll goto leftover\r\n"
                    "if exist Q2Launcher.exe goto leftover\r\n"
                    "if not exist winmmsys.dll goto needmixer\r\n"
                    "echo File layout looks complete.\r\n"
                    "echo Start quake2.exe, XATRIX.BAT, or ROGUE.BAT.\r\n"
                    "goto end\r\n"
                    ":leftover\r\n"
                    "echo ERROR: A GOG or local winmm leftover is still in this folder.\r\n"
                    "goto end\r\n"
                    ":needmixer\r\n"
                    "echo Game files look complete.\r\n"
                    "echo NOTE: winmmsys.dll is not here yet. That is optional to create ahead of time.\r\n"
                    "echo Starting the game copies this PC's C:\\WINDOWS\\SYSTEM\\winmm.dll automatically.\r\n"
                    "echo SETUP95.BAT does the same copy if you want the file before launching.\r\n"
                    "goto end\r\n"
                    ":failed\r\n"
                    "echo ERROR: Missing quake2.exe, wincd.dll, music WAVs, or PAK files.\r\n"
                    ":end\r\n"
                    "pause\r\n", error))
        return false;

    if (!WriteAscii(JoinPath(root, L"WIN95_README.TXT"),
                    "QUAKE II 3.20 - WINDOWS 95 HDD INSTALL\r\n"
                    "Pentium / Voodoo2 / no optical drive.\r\n"
                    "\r\n"
                    "This folder is the game. Copy it to D:\\Games\\QuakeII (or similar).\r\n"
                    "Do not copy a modern winmmsys.dll from Windows 7/10/11.\r\n"
                    "\r\n"
                    "REQUIRED ON THE WINDOWS 95 MACHINE\r\n"
                    "----------------------------------\r\n"
                    "wincd.dll is a local stand-in for winmm.dll (Windows 95 will not load a\r\n"
                    "local file named winmm.dll). quake2.exe is patched to import wincd.dll.\r\n"
                    "The untouched 3.20 binary is quake2.exe.bak.\r\n"
                    "\r\n"
                    "Starting quake2.exe copies this PC's mixer:\r\n"
                    "\r\n"
                    "  C:\\WINDOWS\\SYSTEM\\winmm.dll\r\n"
                    "\r\n"
                    "to this folder as winmmsys.dll. Use the mixer from THAT PC.\r\n"
                    "\r\n"
                    "SETUP95.BAT is optional. It performs that same copy if you want\r\n"
                    "winmmsys.dll before the first launch (so VERIFY95.BAT can see it).\r\n"
                    "You can skip SETUP95 and just start the game.\r\n"
                    "\r\n"
                    "There must be no winmm.dll, libvorbis*.dll, libogg*.dll, or Q2Launcher.exe\r\n"
                    "in this folder.\r\n"
                    "\r\n"
                    "LAUNCH\r\n"
                    "------\r\n"
                    "Quake II:          quake2.exe\r\n"
                    "The Reckoning:     XATRIX.BAT    (quake2.exe +set game xatrix)\r\n"
                    "Ground Zero:       ROGUE.BAT     (quake2.exe +set game rogue)\r\n"
                    "\r\n"
                    "Expansions must be started that way so the soundtrack remaps. Changing\r\n"
                    "\"game\" from the console after launch will not.\r\n"
                    "\r\n"
                    "Music files are music\\Track02.wav - Track21.wav (22050 Hz, 16-bit stereo).\r\n"
                    "Keep cd_nocd at 0.\r\n"
                    "\r\n"
                    "VIDEO (Voodoo2)\r\n"
                    "---------------\r\n"
                    "driver: 3dfx OpenGL\r\n"
                    "video mode: 640 480\r\n"
                    "fullscreen: yes\r\n"
                    "8-bit textures: yes (off is finer color if the card has RAM to spare)\r\n"
                    "sync every frame: no\r\n"
                    "\r\n"
                    "VERIFY95.BAT is optional. A missing winmmsys.dll before the first launch\r\n"
                    "is normal. Then start the game.\r\n", error))
        return false;

    std::ostringstream prep;
    prep << "Q2Win95Prep output\r\n"
         << "==================\r\n\r\n"
         << "Source executable: " << (exact ? "verified GOG Quake II 3.20" : "UNVERIFIED - hashes differed from the tested GOG 3.20")
         << "\r\n"
         << "Soundtrack tracks prepared: " << tracks << "\r\n\r\n"
         << "Copy this entire directory to the Windows 95 computer.\r\n"
         << "Start quake2.exe there; the shim copies that PC's winmm.dll to winmmsys.dll.\r\n"
         << "SETUP95.BAT is optional (same copy, before the first launch).\r\n"
         << "Do not bring a winmmsys.dll from Windows 7, 10, or 11.\r\n"
         << "Launch quake2.exe, XATRIX.BAT, or ROGUE.BAT.\r\n"
         << "Generated by Q2Win95Prep. The GOG installation was not modified.\r\n";
    return WriteAscii(JoinPath(root, L"PREPINFO.TXT"), prep.str(), error);
}

}  // namespace intern
}  // namespace q2prep
