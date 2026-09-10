#define _CRT_SECURE_NO_WARNINGS
#include "prep_internal.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace q2prep {

struct TrackSpec { int number; const wchar_t* hash; };

static const TrackSpec kOggTracks[] = {
    {2, L"FFAED6E480F0B29FCCFE958F8D2553A29C72C7679F7708A5DC980A8ABC82AC61"},
    {3, L"192182AE9421C725E79F522E8530294AE53C62934DEBAB4513DE92DD79AF582B"},
    {4, L"1AF6CCD80F50F208243B9BEA3A6BF56F980EFB3C68F8D56D389CC4254166BAE7"},
    {5, L"99DFF7FDE3E8F04BA3F20BFD841F7C7D6BB7F61415458C8AC8FDD120FF7DA9E3"},
    {6, L"0C94F137B7CBED263040D02B21E5C21A27CE3335C7FE207D162F2B4C5D41E146"},
    {7, L"04DBDF24FCF126021D247E09063B34E77AA7B0DB35EABB64B8085D68A17E78CB"},
    {8, L"509665DDF3CEA3F07F06F8630AAF55D0308E2EAE3B2DD1F2745B01087A7E58FD"},
    {9, L"E84CB5419DA1AD9BD4A08AE61E01229884C700E6CADF16F3495997842C9D5F1B"},
    {10, L"ADCF5846D1E02CBAE889725D8AF9A0DEB5569CA363EDF43E079A0D5582E9C991"},
    {11, L"D7B4469AE8C692CF660DF4FBBC2D7AF5651283CAE09ADBF179A68C59D9A2C5B8"},
    {12, L"95521FBF266D4EBBC5CC72F3BA3AFA327FCADE3DC81F4DA97DC0DF430256D658"},
    {13, L"2F8DE95932FD14DD9EFC6B3CA8D77F6A42C70218C8B1A8914F685C0185D114BE"},
    {14, L"CC36AF6E2BE6335CF8F821FD9DD8DFB2EF578F1C378C2B38256A534004A35160"},
    {15, L"476AEC6E5D0D18D60291FB1572C490AAAEAB69D9B492521AF467577C773ACDA0"},
    {16, L"3FC6E2A70E259D0213C2718AD22C688F300F1A90F98A496949A84EF30E18225B"},
    {17, L"DD1F9E538FA59689A8251B77B53210B8601BF48156D423A258E057329FF1D5D0"},
    {18, L"A6D65036CEF860E89A27427E4E9414C35909262ED9F40BF80628A0E6D177E003"},
    {19, L"B171CE47E023DA3F1389EE7C0F0DF0454BAE0A84392E3B0FBF84ED8F8DB62864"},
    {20, L"79252399855E57F4B01ECFB98F726E34A2FB87B4CBBC2C9284BDD83C286EE50D"},
    {21, L"17B484F4135E4FA7A813DFF74ACECA199F1D274BF3EEDFE0F1603131BF74DB94"}
};

std::wstring ResolveGameRoot(const std::wstring& selected) {
    std::wstring path = selected;
    if (path.empty()) path = ExecutableDirectory();
    if (PathIsFile(JoinPath(path, L"quake2.exe"))) return path;
    std::wstring nested = JoinPath(path, L"QuakeII");
    if (PathIsFile(JoinPath(nested, L"quake2.exe"))) return nested;
    std::wstring parent = ParentPath(path);
    if (!parent.empty() && PathIsFile(JoinPath(parent, L"quake2.exe"))) return parent;
    return path;
}

static void AddIfFile(const std::wstring& path, unsigned long long& total) {
    unsigned long long size = 0;
    if (intern::FileSize(path, size)) total += size;
}

static void AddTree(const std::wstring& path, unsigned long long& total) {
    intern::TreeSize(path, total);
}

SourceStatus InspectSource(const std::wstring& selected) {
    SourceStatus st = {};
    st.usable = false;
    st.exactVersion = true;
    st.resolvedRoot = ResolveGameRoot(selected);
    if (!DirectoryExists(st.resolvedRoot)) {
        st.errors.push_back(L"Select the GOG Quake II game directory.");
        return st;
    }
    std::wstring exe = JoinPath(st.resolvedRoot, L"quake2.exe");
    if (!PathIsFile(exe)) {
        st.errors.push_back(L"quake2.exe was not found in the selected folder, QuakeII\\, or the parent folder.");
        return st;
    }

    static const wchar_t* requiredPaks[] = {
        L"baseq2\\pak0.pak", L"xatrix\\pak0.pak", L"rogue\\pak0.pak"
    };
    for (int i = 0; i < 3; ++i) {
        std::wstring p = JoinPath(st.resolvedRoot, requiredPaks[i]);
        if (!PathIsFile(p)) st.errors.push_back(L"Missing required file: " + std::wstring(requiredPaks[i]));
        else AddIfFile(p, st.estimatedBytes);
    }
    static const wchar_t* optionalCopy[] = {
        L"3dfxgl.dll", L"ref_gl.dll", L"ref_soft.dll", L"pvrgl.dll", L"q2.ico",
        L"readme.txt", L"license.txt", L"3.20_Changes.txt",
        L"baseq2\\pak1.pak", L"baseq2\\pak2.pak", L"baseq2\\gamex86.dll",
        L"baseq2\\maps.lst", L"baseq2\\config.cfg",
        L"xatrix\\gamex86.dll", L"xatrix\\config.cfg",
        L"rogue\\gamex86.dll", L"rogue\\config.cfg"
    };
    for (size_t i = 0; i < sizeof(optionalCopy) / sizeof(optionalCopy[0]); ++i)
        AddIfFile(JoinPath(st.resolvedRoot, optionalCopy[i]), st.estimatedBytes);
    AddTree(JoinPath(st.resolvedRoot, L"docs"), st.estimatedBytes);
    AddTree(JoinPath(JoinPath(st.resolvedRoot, L"baseq2"), L"video"), st.estimatedBytes);
    AddTree(JoinPath(JoinPath(JoinPath(st.resolvedRoot, L"baseq2"), L"players"), L"male"), st.estimatedBytes);
    AddTree(JoinPath(JoinPath(JoinPath(st.resolvedRoot, L"baseq2"), L"players"), L"female"), st.estimatedBytes);
    AddTree(JoinPath(JoinPath(JoinPath(st.resolvedRoot, L"baseq2"), L"players"), L"cyborg"), st.estimatedBytes);
    AddTree(JoinPath(JoinPath(st.resolvedRoot, L"xatrix"), L"docs"), st.estimatedBytes);
    AddTree(JoinPath(JoinPath(st.resolvedRoot, L"xatrix"), L"video"), st.estimatedBytes);
    AddTree(JoinPath(JoinPath(st.resolvedRoot, L"rogue"), L"docs"), st.estimatedBytes);
    AddTree(JoinPath(JoinPath(st.resolvedRoot, L"rogue"), L"video"), st.estimatedBytes);
    AddIfFile(exe, st.estimatedBytes);

    std::wstring err;
    std::vector<unsigned char> exeBytes;
    intern::ReadBytes(exe, exeBytes, 32ull * 1024 * 1024, err);
    bool patched = intern::ExeIsPatched(exeBytes);
    std::wstring hash = Sha256File(exe, &err);
    if (hash.empty()) {
        st.errors.push_back(err.empty() ? L"Could not hash quake2.exe." : err);
    } else if (patched) {
        if (_wcsicmp(hash.c_str(), intern::kPatchedExeHash)) {
            st.exactVersion = false;
            st.warnings.push_back(L"quake2.exe is import-patched but does not match the tested 3.20 patch hash.");
        }
        std::wstring bak = JoinPath(st.resolvedRoot, L"quake2.exe.bak");
        if (!PathIsFile(bak)) {
            st.exactVersion = false;
            st.warnings.push_back(L"quake2.exe is already patched and quake2.exe.bak is missing.");
        } else {
            std::wstring bakHash = Sha256File(bak, &err);
            if (_wcsicmp(bakHash.c_str(), intern::kStockExeHash)) {
                st.exactVersion = false;
                st.warnings.push_back(L"quake2.exe.bak does not match stock GOG 3.20.");
            }
            AddIfFile(bak, st.estimatedBytes);
        }
    } else if (_wcsicmp(hash.c_str(), intern::kStockExeHash)) {
        st.exactVersion = false;
        st.warnings.push_back(L"quake2.exe does not match the tested GOG 3.20 hash.");
    }

    for (size_t i = 0; i < sizeof(kOggTracks) / sizeof(kOggTracks[0]); ++i) {
        int track = kOggTracks[i].number;
        bool hasOgg = false, hasWav = false;
        intern::TrackAvailable(st.resolvedRoot, track, hasOgg, hasWav);
        wchar_t oggName[32], wavName[32];
        swprintf(oggName, 32, L"Track%02d.ogg", track);
        swprintf(wavName, 32, L"Track%02d.wav", track);
        std::wstring music = JoinPath(st.resolvedRoot, L"music");
        if (!hasOgg && !hasWav) {
            st.errors.push_back(std::wstring(L"Missing soundtrack: music\\") + oggName + L" or a canonical 22050 Hz 16-bit stereo " + wavName);
            continue;
        }
        if (hasOgg) {
            std::wstring ogg = JoinPath(music, oggName);
            unsigned long long size = 0;
            intern::FileSize(ogg, size);
            st.estimatedBytes += size * 8 + 16ull * 1024 * 1024;
            std::wstring oggHash = Sha256File(ogg, &err);
            if (oggHash.empty()) st.warnings.push_back(err.empty() ? L"Could not hash soundtrack OGG." : err);
            else if (_wcsicmp(oggHash.c_str(), kOggTracks[i].hash)) {
                st.exactVersion = false;
                st.warnings.push_back(std::wstring(oggName) + L" hash differs from the tested GOG soundtrack.");
            }
        } else {
            unsigned long long size = 0;
            intern::FileSize(JoinPath(music, wavName), size);
            st.estimatedBytes += size;
        }
    }

    st.estimatedBytes += 32ull * 1024 * 1024;
    st.usable = st.errors.empty();
    return st;
}

static bool StageExecutable(const std::wstring& src, const std::wstring& temp, bool allowUnknown,
                            std::wstring& error) {
    std::wstring exePath = JoinPath(src, L"quake2.exe");
    std::vector<unsigned char> data;
    if (!intern::ReadBytes(exePath, data, 32ull * 1024 * 1024, error)) return false;
    const bool patched = intern::ExeIsPatched(data);
    if (patched) {
        if (!intern::WriteAll(JoinPath(temp, L"quake2.exe"), data.empty() ? 0 : &data[0], data.size(), error))
            return false;
        std::wstring bak = JoinPath(src, L"quake2.exe.bak");
        if (PathIsFile(bak)) {
            std::wstring bakHash = Sha256File(bak, &error);
            if (_wcsicmp(bakHash.c_str(), intern::kStockExeHash) && !allowUnknown) {
                error = L"quake2.exe.bak does not match stock GOG 3.20.";
                return false;
            }
            return intern::CopyOne(bak, JoinPath(temp, L"quake2.exe.bak"), error);
        }
        if (!allowUnknown) {
            error = L"quake2.exe is already patched and quake2.exe.bak is missing.";
            return false;
        }
        return true;
    }
    if (!intern::WriteAll(JoinPath(temp, L"quake2.exe.bak"), data.empty() ? 0 : &data[0], data.size(), error))
        return false;
    int hits = intern::PatchWinmmImport(data);
    if (hits < 1) {
        error = L"quake2.exe has no WINMM.dll import to rewrite to wincd.dll.";
        return false;
    }
    return intern::WriteAll(JoinPath(temp, L"quake2.exe"), &data[0], data.size(), error);
}

static bool StageWincd(const std::wstring& temp, std::wstring& error) {
    const unsigned char* data = 0;
    size_t bytes = 0;
    if (!intern::EmbeddedWincd(data, bytes, error)) return false;
    return intern::WriteAll(JoinPath(temp, L"wincd.dll"), data, bytes, error);
}

static bool ConvertTracks(const std::wstring& src, const std::wstring& temp, ProgressSink& progress,
                          std::vector<intern::MusicRow>& rows, std::wstring& error) {
    std::wstring destMusic = JoinPath(temp, L"music");
    if (!intern::EnsureDirectoryDeep(destMusic, error)) return false;
    std::wstring srcMusic = JoinPath(src, L"music");
    for (int track = 2; track <= 21; ++track) {
        if (progress.Cancelled()) { error = L"Cancelled."; return false; }
        wchar_t label[64];
        swprintf(label, 64, L"Converting Track%02d", track);
        progress.Update(22 + (track - 2) * 3, label);
        wchar_t oggName[32], wavName[32];
        swprintf(oggName, 32, L"Track%02d.ogg", track);
        swprintf(wavName, 32, L"Track%02d.wav", track);
        std::wstring ogg = JoinPath(srcMusic, oggName);
        std::wstring srcWav = JoinPath(srcMusic, wavName);
        std::wstring destWav = JoinPath(destMusic, wavName);
        if (PathIsFile(ogg)) {
            std::vector<short> pcm, converted;
            if (!intern::DecodeOgg(ogg, pcm, error)) return false;
            intern::ConvertPcm16(pcm, converted);
            pcm.clear();
            pcm.shrink_to_fit();
            if (!intern::WriteCanonicalWav(destWav, converted, error)) return false;
        } else if (intern::IsCanonicalWavFile(srcWav)) {
            if (!intern::CopyOne(srcWav, destWav, error)) return false;
        } else {
            error = L"Missing soundtrack: " + std::wstring(oggName);
            return false;
        }
        intern::MusicRow row;
        row.track = track;
        intern::FileSize(destWav, row.bytes);
        row.hash = Sha256File(destWav, &error);
        if (row.hash.empty()) return false;
        rows.push_back(row);
    }
    return intern::WriteMusicManifest(destMusic, rows, error);
}

PrepareResult Prepare(const std::wstring& selected, const std::wstring& output, bool allowUnknown,
                      bool replaceExisting, ProgressSink& progress) {
    PrepareResult r = {};
    r.outputPath = output;
    SourceStatus status = InspectSource(selected);
    if (!status.usable) {
        r.message = status.errors.empty() ? L"Invalid source" : status.errors[0];
        return r;
    }
    if (!status.exactVersion && !allowUnknown) {
        r.message = L"Source hashes differ; confirmation is required.";
        return r;
    }
    const std::wstring& src = status.resolvedRoot;
    r.outputPath = JoinPath(src, L"Copy-To-Windows-95");
    std::wstring error;
    if (!intern::IsPathSafe(src, r.outputPath, error)) { r.message = error; return r; }
    if (!output.empty() && _wcsicmp(output.c_str(), r.outputPath.c_str()) &&
        _wcsicmp(JoinPath(selected, L"Copy-To-Windows-95").c_str(), r.outputPath.c_str())) {
        // GUI may pass either selected or resolved output; always publish under the game root.
    }
    if (DirectoryExists(r.outputPath)) {
        if (!replaceExisting) { r.message = L"Output already exists."; return r; }
        if (!intern::OwnedOutput(r.outputPath)) {
            r.message = L"Existing output has no valid ownership marker; it will not be replaced.";
            return r;
        }
    }
    unsigned long long free = 0;
    if (!intern::FreeBytes(src, free) || free < status.estimatedBytes) {
        r.message = L"Insufficient free space for the temporary output.";
        return r;
    }
    wchar_t tempName[64];
    swprintf(tempName, 64, L"Copy-To-Windows-95.tmp-%lu", GetCurrentProcessId());
    std::wstring temp = JoinPath(src, tempName);
    if (DirectoryExists(temp)) {
        r.message = L"A previous temporary directory is in the way: " + temp;
        return r;
    }
    progress.Update(1, L"Creating safe temporary output...");
    if (!intern::EnsureDirectoryDeep(temp, error)) { r.message = error; return r; }
    if (!intern::WriteAscii(JoinPath(temp, intern::kMarkerName), intern::kMarkerText, error)) {
        RemoveDirectoryW(temp.c_str());
        r.message = error;
        return r;
    }

    bool ok = intern::CopyGameFiles(src, temp, progress, error);
    if (ok) {
        progress.Update(20, L"Patching quake2.exe import...");
        ok = StageExecutable(src, temp, allowUnknown, error);
    }
    if (ok) {
        progress.Update(21, L"Extracting wincd.dll...");
        ok = StageWincd(temp, error);
    }
    std::vector<intern::MusicRow> rows;
    if (ok) ok = ConvertTracks(src, temp, progress, rows, error);
    if (ok) {
        progress.Update(88, L"Writing Windows 95 scripts...");
        ok = intern::WriteWin95Scripts(temp, status.exactVersion, static_cast<int>(rows.size()), error);
    }
    if (ok) {
        progress.Update(92, L"Applying 3dfx video defaults...");
        ok = intern::TweakConfig(JoinPath(JoinPath(temp, L"baseq2"), L"config.cfg"), error) &&
             intern::TweakConfig(JoinPath(JoinPath(temp, L"xatrix"), L"config.cfg"), error) &&
             intern::TweakConfig(JoinPath(JoinPath(temp, L"rogue"), L"config.cfg"), error);
    }
    if (!ok) {
        std::wstring cleanup;
        if (intern::OwnedOutput(temp)) intern::RemoveTreeOwned(temp, cleanup);
        r.cancelled = progress.Cancelled();
        r.message = r.cancelled ? L"Preparation cancelled; the previous output was retained." : error;
        return r;
    }

    progress.Update(98, L"Publishing completed output...");
    std::wstring backup;
    if (DirectoryExists(r.outputPath)) {
        if (!intern::OwnedOutput(r.outputPath) || !intern::ValidateNoReparsePoints(r.outputPath, error)) {
            std::wstring cleanup;
            intern::RemoveTreeOwned(temp, cleanup);
            r.message = error.empty() ? L"Existing output cannot be safely replaced." : error;
            return r;
        }
        wchar_t backupName[64];
        swprintf(backupName, 64, L"Copy-To-Windows-95.previous-%lu", GetCurrentProcessId());
        backup = JoinPath(src, backupName);
        if (DirectoryExists(backup)) {
            std::wstring cleanup;
            intern::RemoveTreeOwned(temp, cleanup);
            r.message = L"A previous backup directory is in the way: " + backup;
            return r;
        }
        if (!MoveFileW(r.outputPath.c_str(), backup.c_str())) {
            std::wstring cleanup;
            intern::RemoveTreeOwned(temp, cleanup);
            r.message = L"Could not preserve the previous output: " + intern::ErrorText(GetLastError());
            return r;
        }
    }
    if (!MoveFileW(temp.c_str(), r.outputPath.c_str())) {
        DWORD publishError = GetLastError();
        if (!backup.empty()) MoveFileW(backup.c_str(), r.outputPath.c_str());
        r.message = L"Could not publish output; the previous output was restored: " + intern::ErrorText(publishError);
        return r;
    }
    std::wstring cleanupWarning;
    if (!backup.empty() && !intern::RemoveTreeOwned(backup, cleanupWarning)) {
        cleanupWarning = L" The previous generated output remains at " + backup +
                         L" and may be removed manually.";
    } else {
        cleanupWarning.clear();
    }
    progress.Update(100, L"Preparation complete.");
    r.success = true;
    r.trackCount = static_cast<int>(rows.size());
    r.message = L"Copy-To-Windows-95 is ready. Copy that folder to the Windows 95 PC and start the game. SETUP95.BAT is optional." +
                cleanupWarning;
    return r;
}

bool RunSelfTests(const std::wstring& reportPath, std::wstring& summary) {
    bool ok = true;
    std::ostringstream out;
    out << "Q2Win95Prep self-tests\r\n";

    std::vector<short> samples(2, 0);
    samples[0] = 1000;
    samples[1] = -1000;
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring wavPath = JoinPath(tempDir, L"q2prep-canonical.wav");
    std::wstring error;
    bool wavWrite = intern::WriteCanonicalWav(wavPath, samples, error);
    HANDLE wavFile = CreateFileW(wavPath.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    unsigned char header[44] = {};
    DWORD read = 0;
    LARGE_INTEGER wavSize = {};
    bool wavRead = wavFile != INVALID_HANDLE_VALUE && GetFileSizeEx(wavFile, &wavSize) &&
                   ReadFile(wavFile, header, 44, &read, 0) && read == 44;
    if (wavFile != INVALID_HANDLE_VALUE) CloseHandle(wavFile);
    bool wavHeader = wavRead && intern::IsCanonicalWavBytes(header, 44, static_cast<unsigned long long>(wavSize.QuadPart)) &&
                     memcmp(header + 12, "fmt ", 4) == 0 && memcmp(header + 36, "data", 4) == 0 &&
                     intern::ReadU16(header + 20) == 1 && intern::ReadU16(header + 22) == 2 &&
                     intern::ReadU32(header + 24) == 22050 && intern::ReadU16(header + 34) == 16;
    ok = ok && wavWrite && wavHeader;
    out << "Canonical WAV writer: " << (wavWrite && wavHeader ? "PASS" : "FAIL") << "\r\n";
    DeleteFileW(wavPath.c_str());

    std::vector<short> tiny(4 * 2, 0), converted;
    intern::ConvertPcm16(tiny, converted);
    bool evenLength = converted.size() == 4;
    tiny.assign(5 * 2, 0);
    tiny[8] = 12000;
    tiny[9] = -12000;
    intern::ConvertPcm16(tiny, converted);
    bool oddLength = converted.size() == 6;
    double sumL = 0.0, sumR = 0.0;
    for (int tap = 0; tap < 63; ++tap) {
        long frame = 4 + tap - 31;
        if (frame < 0) frame = 0;
        if (frame >= 5) frame = 4;
        sumL += tiny[frame * 2] * intern::kFir[tap];
        sumR += tiny[frame * 2 + 1] * intern::kFir[tap];
    }
    short expectL = intern::RoundPcm16(sumL);
    short expectR = intern::RoundPcm16(sumR);
    bool oddClamp = oddLength && converted[4] == expectL && converted[5] == expectR;
    ok = ok && evenLength && oddLength && oddClamp;
    out << "Even 4-to-2 frame test: " << (evenLength ? "PASS" : "FAIL") << "\r\n";
    out << "Odd 5-to-3 frame test: " << (oddLength ? "PASS" : "FAIL") << "\r\n";
    out << "Odd final clamped FIR sample: " << (oddClamp ? "PASS" : "FAIL")
        << " (left " << (oddLength ? converted[4] : 0) << ", right " << (oddLength ? converted[5] : 0) << ")\r\n";

    std::vector<short> pcm(44100 * 2);
    for (size_t i = 0; i < pcm.size() / 2; ++i) {
        pcm[i * 2] = 10000;
        pcm[i * 2 + 1] = -10000;
    }
    intern::ConvertPcm16(pcm, converted);
    bool dc = converted.size() == 44100;
    if (dc) {
        for (size_t i = 64; i + 64 < converted.size() / 2; ++i) {
            if (abs(converted[i * 2] - 10000) > 2 || abs(converted[i * 2 + 1] + 10000) > 2) {
                dc = false;
                break;
            }
        }
    }
    bool separated = dc && converted[64 * 2] != converted[64 * 2 + 1];
    ok = ok && dc && separated;
    out << "DC/channel test: " << (dc && separated ? "PASS" : "FAIL") << "\r\n";

    std::vector<unsigned char> buf(32, 0);
    memcpy(&buf[4], "WINMM.dll", 9);
    buf[13] = 0;
    int hits = intern::PatchWinmmImport(buf);
    bool patch = hits == 1 && memcmp(&buf[4], "wincd.dll", 9) == 0 && buf[13] == 0;
    std::vector<unsigned char> noNul(16, 0xFF);
    memcpy(&noNul[0], "WINMM.dll", 9);
    bool noFalseHit = intern::PatchWinmmImport(noNul) == 0;
    ok = ok && patch && noFalseHit;
    out << "WINMM import patcher: " << (patch && noFalseHit ? "PASS" : "FAIL") << "\r\n";

    wchar_t ownedName[64];
    swprintf(ownedName, 64, L"q2prep-owned-%lu", GetCurrentProcessId());
    std::wstring owned = JoinPath(tempDir, ownedName);
    CreateDirectoryW(owned.c_str(), 0);
    bool unmarked = !intern::OwnedOutput(owned);
    std::wstring refuseError;
    bool refused = !intern::RemoveTreeOwned(owned, refuseError);
    intern::WriteAscii(JoinPath(owned, intern::kMarkerName), intern::kMarkerText, error);
    bool marked = intern::OwnedOutput(owned);
    bool removed = intern::RemoveTreeOwned(owned, error);
    ok = ok && unmarked && refused && marked && removed;
    out << "OwnedOutput marker: " << (unmarked && refused && marked && removed ? "PASS" : "FAIL") << "\r\n";

    const unsigned char* wincd = 0;
    size_t wincdBytes = 0;
    bool embedded = intern::EmbeddedWincd(wincd, wincdBytes, error);
    bool mz = embedded && wincdBytes >= 64 && wincd[0] == 'M' && wincd[1] == 'Z';
    bool pe = false;
    if (mz) {
        unsigned long e_lfanew = intern::ReadU32(wincd + 0x3c);
        pe = e_lfanew + 4 < wincdBytes && wincd[e_lfanew] == 'P' && wincd[e_lfanew + 1] == 'E' &&
             wincd[e_lfanew + 2] == 0 && wincd[e_lfanew + 3] == 0;
    }
    bool exportName = false;
    if (embedded) {
        const char* needle = "mciSendCommandA";
        size_t n = strlen(needle);
        for (size_t i = 0; i + n <= wincdBytes; ++i)
            if (memcmp(wincd + i, needle, n) == 0) { exportName = true; break; }
    }
    ok = ok && mz && pe && exportName;
    out << "Embedded wincd.dll MZ/PE: " << (mz && pe ? "PASS" : "FAIL") << " (bytes " << wincdBytes << ")\r\n";
    out << "Embedded mciSendCommandA export name: " << (exportName ? "PASS" : "FAIL") << "\r\n";

    if (!intern::WriteAscii(reportPath, out.str(), error)) {
        summary = error;
        return false;
    }
    summary = ok ? L"All self-tests passed." : L"One or more self-tests failed.";
    return ok;
}

void CleanupOwnedTemporary(const std::wstring& source) {
    std::wstring root = ResolveGameRoot(source);
    if (root.empty()) root = source;
    wchar_t name[64];
    swprintf(name, 64, L"Copy-To-Windows-95.tmp-%lu", GetCurrentProcessId());
    std::wstring path = JoinPath(root, name), error;
    if (DirectoryExists(path) && intern::OwnedOutput(path)) intern::RemoveTreeOwned(path, error);
}

}  // namespace q2prep
