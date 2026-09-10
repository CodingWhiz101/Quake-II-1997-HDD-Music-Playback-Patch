#define _CRT_SECURE_NO_WARNINGS
#include "prep_internal.h"
#include "resource_ids.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "third_party/stb_vorbis.c"

namespace q2prep {
namespace intern {

// 63-tap Blackman-windowed low-pass FIR, fc = 0.225 cycles/input sample.
// Same coefficients as BloodWin95Prep; normalized for unity DC gain.
const double kFir[63] = {
 2.2291315183986354E-20, -9.8434363032654776E-06, -6.4417217135509424E-06,
 9.288793680655306E-05, 8.3699207391128386E-05, -0.00024903485344916965,
 -0.00033673840091378942, 0.00041110468002547292, 0.00088122895742471228,
 -0.00041980340336228439, -0.0018007859277915784, 2.6457832081279495E-18,
 0.0030718380646200822, 0.0012280269326097979, -0.0044703186653053525,
 -0.0036844471567862693, 0.0054878851561089795, 0.00771409303697202,
 -0.0052843026642457273, -0.013444908747554515, 0.002676331407659945,
 0.020666026260984859, 0.003906068186756428, -0.028775385752400774,
 -0.016752451014917347, 0.036831759973675132, 0.040490789663561053,
 -0.043714693186734072, -0.09101419959813746, 0.048358740482636657,
 0.31306568521235212, 0.44999437874006049, 0.31306568521235223,
 0.048358740482636657, -0.09101419959813746, -0.043714693186734072,
 0.040490789663561053, 0.036831759973675139, -0.01675245101491735,
 -0.028775385752400788, 0.0039060681867564293, 0.020666026260984863,
 0.002676331407659945, -0.013444908747554524, -0.0052843026642457325,
 0.0077140930369720227, 0.0054878851561089865, -0.003684447156786268,
 -0.0044703186653053525, 0.001228026932609799, 0.0030718380646200822,
 2.6457832081279533E-18, -0.0018007859277915784, -0.00041980340336228493,
 0.0008812289574247126, 0.00041110468002547243, -0.00033673840091378931,
 -0.00024903485344917003, 8.3699207391128237E-05, 9.288793680655306E-05,
 -6.4417217135510144E-06, -9.843436303265625E-06, 2.2291315183986354E-20
};

short RoundPcm16(double sample) {
    long value = static_cast<long>(sample >= 0.0 ? floor(sample + 0.5) : ceil(sample - 0.5));
    if (value < -32768) value = -32768;
    if (value > 32767) value = 32767;
    return static_cast<short>(value);
}

void ConvertPcm16(const std::vector<short>& pcm, std::vector<short>& out) {
    const size_t inputFrames = pcm.size() / 2;
    const size_t outputFrames = inputFrames / 2 + inputFrames % 2;
    out.resize(outputFrames * 2);
    for (size_t o = 0; o < outputFrames; ++o) {
        const long center = static_cast<long>(o * 2);
        double sumL = 0.0, sumR = 0.0;
        for (int tap = 0; tap < 63; ++tap) {
            long frame = center + tap - 31;
            if (frame < 0) frame = 0;
            if (frame >= static_cast<long>(inputFrames)) frame = static_cast<long>(inputFrames) - 1;
            sumL += pcm[frame * 2] * kFir[tap];
            sumR += pcm[frame * 2 + 1] * kFir[tap];
        }
        out[o * 2] = RoundPcm16(sumL);
        out[o * 2 + 1] = RoundPcm16(sumR);
    }
}

bool IsCanonicalWavBytes(const unsigned char* header, size_t bytes, unsigned long long fileSize) {
    if (!header || bytes < 44) return false;
    if (memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4) ||
        memcmp(header + 12, "fmt ", 4) || memcmp(header + 36, "data", 4))
        return false;
    if (ReadU32(header + 16) != 16) return false;
    if (ReadU16(header + 20) != 1) return false;
    if (ReadU16(header + 22) != 2) return false;
    if (ReadU32(header + 24) != 22050) return false;
    if (ReadU16(header + 34) != 16) return false;
    unsigned long dataBytes = ReadU32(header + 40);
    unsigned long riffSize = ReadU32(header + 4);
    if (fileSize != 44ull + dataBytes) return false;
    if (riffSize != 36ul + dataBytes) return false;
    if (ReadU32(header + 28) != 22050ul * 2ul * 2ul) return false;
    if (ReadU16(header + 32) != 4) return false;
    return true;
}

bool IsCanonicalWavFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (file == INVALID_HANDLE_VALUE) return false;
    unsigned char header[44];
    DWORD read = 0;
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(file, &size) != FALSE &&
              ReadFile(file, header, 44, &read, 0) != FALSE && read == 44;
    CloseHandle(file);
    return ok && IsCanonicalWavBytes(header, 44, static_cast<unsigned long long>(size.QuadPart));
}

bool WriteCanonicalWav(const std::wstring& path, const std::vector<short>& interleaved, std::wstring& error) {
    if (interleaved.size() < 2 || (interleaved.size() % 2) != 0) {
        error = L"Converted track has an invalid sample count.";
        return false;
    }
    const unsigned long dataBytes = static_cast<unsigned long>(interleaved.size() * sizeof(short));
    std::vector<unsigned char> header(44);
    memcpy(&header[0], "RIFF", 4);
    WriteU32(&header[4], 36ul + dataBytes);
    memcpy(&header[8], "WAVE", 4);
    memcpy(&header[12], "fmt ", 4);
    WriteU32(&header[16], 16);
    WriteU16(&header[20], 1);
    WriteU16(&header[22], 2);
    WriteU32(&header[24], 22050);
    WriteU32(&header[28], 22050ul * 2ul * 2ul);
    WriteU16(&header[32], 4);
    WriteU16(&header[34], 16);
    memcpy(&header[36], "data", 4);
    WriteU32(&header[40], dataBytes);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Could not create " + path + L": " + ErrorText(GetLastError());
        return false;
    }
    DWORD written = 0;
    bool ok = WriteFile(file, &header[0], 44, &written, 0) != FALSE && written == 44;
    if (ok) {
        const unsigned char* cursor = reinterpret_cast<const unsigned char*>(&interleaved[0]);
        size_t left = dataBytes;
        while (left) {
            DWORD chunk = static_cast<DWORD>(left > (1u << 20) ? (1u << 20) : left);
            if (!WriteFile(file, cursor, chunk, &written, 0) || written != chunk) { ok = false; break; }
            cursor += written;
            left -= written;
        }
    }
    if (!CloseHandle(file)) ok = false;
    if (!ok) {
        error = L"Could not write " + path;
        return false;
    }
    if (!IsCanonicalWavFile(path)) {
        error = L"Canonical WAV verification failed: " + path;
        return false;
    }
    return true;
}

bool DecodeOgg(const std::wstring& path, std::vector<short>& pcm, std::wstring& error) {
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) { error = L"Could not open " + path; return false; }
    int stbError = 0;
    stb_vorbis* vorbis = stb_vorbis_open_file(file, 0, &stbError, 0);
    if (!vorbis) {
        fclose(file);
        std::wstringstream s;
        s << L"Invalid OGG (decoder " << stbError << L"): " << path;
        error = s.str();
        return false;
    }
    stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    int channels = info.channels;
    int rate = static_cast<int>(info.sample_rate);
    if (channels != 2 || rate != 44100) {
        stb_vorbis_close(vorbis);
        fclose(file);
        std::wstringstream s;
        s << L"Track must be 44.1 kHz stereo; found " << rate << L" Hz, " << channels
          << L" channel(s): " << path;
        error = s.str();
        return false;
    }
    const int chunkFrames = 16384;
    std::vector<short> chunk(chunkFrames * 2);
    for (;;) {
        int frames = stb_vorbis_get_samples_short_interleaved(vorbis, 2, &chunk[0],
                                                             static_cast<int>(chunk.size()));
        if (frames <= 0) break;
        pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + frames * 2);
        if (pcm.size() > static_cast<size_t>(44100) * 2 * 600) {
            stb_vorbis_close(vorbis);
            fclose(file);
            error = L"Track exceeds the 10-minute safety limit: " + path;
            return false;
        }
    }
    stb_vorbis_close(vorbis);
    fclose(file);
    const double seconds = pcm.size() / (2.0 * 44100.0);
    if (seconds < 5.0 || seconds > 600.0) {
        error = L"Track duration is outside the 5 second-10 minute safety range: " + path;
        return false;
    }
    return true;
}

int PatchWinmmImport(std::vector<unsigned char>& data) {
    static const char* olds[] = { "WINMM.dll", "WINMM.DLL", "winmm.dll", "Winmm.dll" };
    static const char neu[] = "wincd.dll";
    int hits = 0;
    for (int n = 0; n < 4; ++n) {
        size_t start = 0;
        while (start + 9 < data.size()) {
            size_t at = static_cast<size_t>(-1);
            for (size_t i = start; i + 9 < data.size(); ++i) {
                if (memcmp(&data[i], olds[n], 9) == 0 && data[i + 9] == 0) {
                    at = i;
                    break;
                }
            }
            if (at == static_cast<size_t>(-1)) break;
            memcpy(&data[at], neu, 9);
            ++hits;
            start = at + 1;
        }
    }
    return hits;
}

bool BufferContains(const std::vector<unsigned char>& data, const char* text) {
    const size_t n = strlen(text);
    if (!n || data.size() < n) return false;
    for (size_t i = 0; i + n <= data.size(); ++i)
        if (memcmp(&data[i], text, n) == 0) return true;
    return false;
}

bool ExeIsPatched(const std::vector<unsigned char>& data) {
    return BufferContains(data, "wincd.dll");
}

bool EmbeddedWincd(const unsigned char*& data, size_t& bytes, std::wstring& error) {
    HMODULE module = GetModuleHandleW(0);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_WINCD_DLL), RT_RCDATA);
    if (!resource) { error = L"Embedded wincd.dll resource is missing."; return false; }
    HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded) { error = L"Could not load embedded wincd.dll."; return false; }
    DWORD size = SizeofResource(module, resource);
    const unsigned char* ptr = static_cast<const unsigned char*>(LockResource(loaded));
    if (!ptr || size < 64) { error = L"Embedded wincd.dll is empty."; return false; }
    data = ptr;
    bytes = size;
    return true;
}

bool TrackAvailable(const std::wstring& root, int track, bool& hasOgg, bool& hasWav) {
    wchar_t name[32];
    std::wstring music = JoinPath(root, L"music");
    swprintf(name, 32, L"Track%02d.ogg", track);
    hasOgg = PathIsFile(JoinPath(music, name));
    swprintf(name, 32, L"Track%02d.wav", track);
    std::wstring wav = JoinPath(music, name);
    hasWav = PathIsFile(wav) && IsCanonicalWavFile(wav);
    return hasOgg || hasWav;
}

}  // namespace intern
}  // namespace q2prep
