/*
 * Quake II 3.20 Win95 MCI CD audio shim.
 * Forwards non-CD WINMM imports to the real system mixer and plays
 * music\TrackXX.wav through a DirectSound secondary buffer.
 */
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#define Q2CD_DEVICE_ID  0x4D01U
#define Q2CD_MAX_TRACK  21
#define RING_BYTES      705600UL
#define HALF_BYTES      352800UL
#define WAV_DATA_OFFSET 44UL
#define SAMPLE_RATE     22050UL

#ifndef MCI_CDA_TRACK_AUDIO
#define MCI_CDA_TRACK_AUDIO 1
#endif
#ifndef MCI_CDA_TRACK_OTHER
#define MCI_CDA_TRACK_OTHER 0
#endif
#ifndef AUXCAPS_CDAUDIO
#define AUXCAPS_CDAUDIO 2
#endif

enum {
    GAME_BASE = 0,
    GAME_ROGUE = 1,
    GAME_XATRIX = 2
};

typedef HRESULT (WINAPI *PFN_DirectSoundCreate)(void *, LPDIRECTSOUND *, void *);

static HMODULE g_real;
static HMODULE g_self;
static HMODULE g_dsound_mod;
static CRITICAL_SECTION g_lock;
static BOOL g_lock_ready;
static UINT g_timer_id;
static DWORD g_last_refill_tick;
static char g_dir[MAX_PATH];

static LPDIRECTSOUND g_ds;
static LPDIRECTSOUNDBUFFER g_buffer;
static HANDLE g_file = INVALID_HANDLE_VALUE;
static DWORD g_data_size;
static DWORD g_data_pos;
static DWORD g_wav_frames[Q2CD_MAX_TRACK + 1];
static DWORD g_last_half;
static DWORD g_eof_countdown;
static BOOL g_playing;
static BOOL g_paused;
static BOOL g_has_volume;
static HWND g_notify_hwnd;
static UINT g_play_track;
static int g_game;
static int g_volume_raw = 255;
static LONG g_volume_ds;
static DWORD g_aux_volume = 0xFFFFFFFFUL;

static DWORD g_seq;
static DWORD g_play_count;
static DWORD g_notify_count;
static DWORD g_refill_count;
static DWORD g_restore_count;
static DWORD g_failure_count;

static const short g_volume_anchor[17] = {
    -10000, -2406, -1803, -1451, -1201, -1007, -849, -715, -598,
    -496, -405, -322, -246, -177, -113, -53, 0
};

typedef DWORD (WINAPI *PFN_timeGetTime)(void);
typedef MMRESULT (WINAPI *PFN_timePeriod)(UINT);
typedef UINT (WINAPI *PFN_timeSetEvent)(UINT, UINT, void *, DWORD, UINT);
typedef MMRESULT (WINAPI *PFN_timeKillEvent)(UINT);
typedef MMRESULT (WINAPI *PFN_waveOutOpen)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD, DWORD, DWORD);
typedef MMRESULT (WINAPI *PFN_waveOutH)(HWAVEOUT);
typedef MMRESULT (WINAPI *PFN_waveOutHdr)(HWAVEOUT, LPWAVEHDR, UINT);
typedef UINT (WINAPI *PFN_joyGetNumDevs)(void);
typedef MMRESULT (WINAPI *PFN_joyGetDevCapsA)(UINT, LPJOYCAPSA, UINT);
typedef MMRESULT (WINAPI *PFN_joyGetPosEx)(UINT, LPJOYINFOEX);
typedef MCIERROR (WINAPI *PFN_mciSendCommandA)(MCIDEVICEID, UINT, DWORD, DWORD);
typedef MCIERROR (WINAPI *PFN_mciSendStringA)(LPCSTR, LPSTR, UINT, HWND);
typedef BOOL (WINAPI *PFN_mciGetErrorStringA)(DWORD, LPSTR, UINT);
typedef UINT (WINAPI *PFN_auxGetNumDevs)(void);
typedef MMRESULT (WINAPI *PFN_auxGetDevCapsA)(UINT, LPAUXCAPSA, UINT);
typedef MMRESULT (WINAPI *PFN_auxVol)(UINT, DWORD *);
typedef MMRESULT (WINAPI *PFN_auxSetVol)(UINT, DWORD);

static PFN_timeGetTime p_timeGetTime;
static PFN_timePeriod p_timeBeginPeriod;
static PFN_timePeriod p_timeEndPeriod;
static PFN_timeSetEvent p_timeSetEvent;
static PFN_timeKillEvent p_timeKillEvent;
static PFN_waveOutOpen p_waveOutOpen;
static PFN_waveOutH p_waveOutClose;
static PFN_waveOutH p_waveOutReset;
static PFN_waveOutHdr p_waveOutPrepareHeader;
static PFN_waveOutHdr p_waveOutUnprepareHeader;
static PFN_waveOutHdr p_waveOutWrite;
static PFN_joyGetNumDevs p_joyGetNumDevs;
static PFN_joyGetDevCapsA p_joyGetDevCapsA;
static PFN_joyGetPosEx p_joyGetPosEx;
static PFN_mciSendCommandA p_mciSendCommandA;
static PFN_mciSendStringA p_mciSendStringA;
static PFN_mciGetErrorStringA p_mciGetErrorStringA;
static PFN_auxGetNumDevs p_auxGetNumDevs;
static PFN_auxGetDevCapsA p_auxGetDevCapsA;
static PFN_auxVol p_auxGetVolume;
static PFN_auxSetVol p_auxSetVolume;
static PFN_DirectSoundCreate p_DirectSoundCreate;

static int ascii_eqi(const char *a, const char *b)
{
    unsigned char ca, cb;
    if (!a || !b)
        return 0;
    while (*a || *b) {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        ++a;
        ++b;
    }
    return 1;
}

static int ascii_matchi(const char *hay, const char *needle)
{
    const char *h, *n;
    unsigned char ch, cn;
    if (!hay || !needle)
        return 0;
    for (; *hay; ++hay) {
        h = hay;
        n = needle;
        while (*n) {
            ch = (unsigned char)*h;
            cn = (unsigned char)*n;
            if (ch >= 'A' && ch <= 'Z')
                ch = (unsigned char)(ch - 'A' + 'a');
            if (cn >= 'A' && cn <= 'Z')
                cn = (unsigned char)(cn - 'A' + 'a');
            if (!ch || ch != cn)
                break;
            ++h;
            ++n;
        }
        if (!*n)
            return 1;
    }
    return 0;
}

static void mem_zero(void *p, unsigned n)
{
    unsigned char *c = (unsigned char *)p;
    while (n--)
        *c++ = 0;
}

static void log_line(const char *text)
{
    HANDLE h;
    DWORD length = 0, written = 0;
    char path[MAX_PATH];
#ifdef Q2CD_DEBUG
#else
    /* Quiet build still writes SUMMARY / failures via log_event. */
#endif
    while (text[length] && length < 700)
        ++length;
    lstrcpynA(path, g_dir, MAX_PATH);
    lstrcatA(path, "q2cd.log");
    h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, text, length, &written, NULL);
    CloseHandle(h);
}

static void log_event(const char *stage, HRESULT hr, DWORD a, DWORD b)
{
    char line[320];
    ++g_seq;
    wsprintfA(line, "EV seq=%u tick=%u stage=%s hr=%08X a=%08X b=%08X\r\n",
              g_seq, GetTickCount(), stage, (DWORD)hr, a, b);
#ifdef Q2CD_DEBUG
    log_line(line);
#else
    if (FAILED(hr) || !lstrcmpA(stage, "SUMMARY") || !lstrcmpA(stage, "NOTIFY") ||
        !lstrcmpA(stage, "INIT") || !lstrcmpA(stage, "OPEN") ||
        !lstrcmpA(stage, "PLAY_REQUEST"))
        log_line(line);
#endif
    if (FAILED(hr))
        ++g_failure_count;
}

static LONG volume_to_ds(int raw)
{
    int index, frac;
    LONG low, high;
    if (raw <= 0)
        return DSBVOLUME_MIN;
    if (raw >= 255)
        return DSBVOLUME_MAX;
    index = raw >> 4;
    frac = raw & 15;
    low = g_volume_anchor[index];
    high = g_volume_anchor[index + 1];
    return low + ((high - low) * frac) / 16;
}

static char g_cmdcopy[1024];

static int is_arg_sep(unsigned char c)
{
    return c == 0 || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int detect_game_from(const char *cmd)
{
    char prev[32];
    char tok[32];
    int n;
    prev[0] = 0;
    if (!cmd)
        return GAME_BASE;
    while (*cmd) {
        while (*cmd && is_arg_sep((unsigned char)*cmd))
            ++cmd;
        if (!*cmd)
            break;
        n = 0;
        while (cmd[n] && !is_arg_sep((unsigned char)cmd[n]) && n < 31) {
            tok[n] = cmd[n];
            if (tok[n] >= 'A' && tok[n] <= 'Z')
                tok[n] = (char)(tok[n] - 'A' + 'a');
            ++n;
        }
        tok[n] = 0;
        cmd += n;
        if (!lstrcmpA(prev, "game")) {
            if (!lstrcmpA(tok, "xatrix"))
                return GAME_XATRIX;
            if (!lstrcmpA(tok, "rogue"))
                return GAME_ROGUE;
        }
        lstrcpynA(prev, tok, 32);
    }
    if (ascii_matchi(cmd, "game xatrix") || ascii_matchi(cmd, "game=xatrix"))
        return GAME_XATRIX;
    if (ascii_matchi(cmd, "game rogue") || ascii_matchi(cmd, "game=rogue"))
        return GAME_ROGUE;
    return GAME_BASE;
}

static int remap_track(int track)
{
    static const int xatrix[12] = {0, 0, 9, 13, 14, 7, 16, 2, 15, 3, 4, 18};
    if (track <= 0)
        return 0;
    if (g_game == GAME_ROGUE)
        return track + 10;
    if (g_game == GAME_XATRIX && track <= 11)
        return xatrix[track];
    return track;
}

static BOOL seek_data(DWORD offset)
{
    DWORD pos;
    pos = SetFilePointer(g_file, WAV_DATA_OFFSET + offset, NULL, FILE_BEGIN);
    return pos != 0xFFFFFFFFUL;
}

static BOOL read_pcm(BYTE *dest, DWORD bytes)
{
    DWORD total = 0;
    while (total < bytes) {
        DWORD remaining = g_data_size - g_data_pos;
        DWORD wanted, got = 0;
        if (remaining == 0) {
            mem_zero(dest + total, bytes - total);
            if (!g_eof_countdown)
                g_eof_countdown = 2;
            return TRUE;
        }
        wanted = bytes - total;
        if (wanted > remaining)
            wanted = remaining;
        if (!ReadFile(g_file, dest + total, wanted, &got, NULL) || got == 0)
            return FALSE;
        total += got;
        g_data_pos += got;
    }
    return TRUE;
}

static HRESULT fill_region(DWORD offset, DWORD bytes)
{
    LPVOID p1 = NULL, p2 = NULL;
    DWORD n1 = 0, n2 = 0;
    HRESULT hr;
    if (!g_buffer)
        return E_FAIL;
    hr = IDirectSoundBuffer_Lock(g_buffer, offset, bytes, &p1, &n1, &p2, &n2, 0);
    if (hr == DSERR_BUFFERLOST) {
        hr = IDirectSoundBuffer_Restore(g_buffer);
        ++g_restore_count;
        if (FAILED(hr))
            return hr;
        hr = IDirectSoundBuffer_Lock(g_buffer, offset, bytes, &p1, &n1, &p2, &n2, 0);
    }
    if (FAILED(hr))
        return hr;
    if (!read_pcm((BYTE *)p1, n1) || (n2 && !read_pcm((BYTE *)p2, n2))) {
        IDirectSoundBuffer_Unlock(g_buffer, p1, n1, p2, n2);
        return E_FAIL;
    }
    return IDirectSoundBuffer_Unlock(g_buffer, p1, n1, p2, n2);
}

static void close_file(void)
{
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
}

static void stream_stop_locked(BOOL send_abort)
{
    if (g_buffer) {
        IDirectSoundBuffer_Stop(g_buffer);
        IDirectSoundBuffer_Release(g_buffer);
        g_buffer = NULL;
    }
    close_file();
    if (send_abort && g_notify_hwnd)
        PostMessageA(g_notify_hwnd, MM_MCINOTIFY, MCI_NOTIFY_ABORTED, Q2CD_DEVICE_ID);
    g_playing = FALSE;
    g_paused = FALSE;
    g_eof_countdown = 0;
    g_has_volume = FALSE;
}

static BOOL load_dsound(void)
{
    if (p_DirectSoundCreate)
        return TRUE;
    g_dsound_mod = LoadLibraryA("dsound.dll");
    if (!g_dsound_mod)
        return FALSE;
    p_DirectSoundCreate = (PFN_DirectSoundCreate)GetProcAddress(g_dsound_mod, "DirectSoundCreate");
    return p_DirectSoundCreate != NULL;
}

static BOOL ensure_ds(HWND hwnd)
{
    HRESULT hr;
    if (g_ds)
        return TRUE;
    if (!load_dsound())
        return FALSE;
    hr = p_DirectSoundCreate(NULL, &g_ds, NULL);
    if (FAILED(hr) || !g_ds)
        return FALSE;
    if (!hwnd)
        hwnd = GetDesktopWindow();
    hr = IDirectSound_SetCooperativeLevel(g_ds, hwnd, DSSCL_NORMAL);
    if (FAILED(hr))
        hr = IDirectSound_SetCooperativeLevel(g_ds, GetDesktopWindow(), DSSCL_NORMAL);
    if (FAILED(hr)) {
        IDirectSound_Release(g_ds);
        g_ds = NULL;
        return FALSE;
    }
    return TRUE;
}

static void apply_volume_locked(void)
{
    g_volume_ds = volume_to_ds(g_volume_raw);
    if (g_buffer && g_has_volume)
        IDirectSoundBuffer_SetVolume(g_buffer, g_volume_ds);
}

static BOOL stream_play_locked(int file_track, HWND notify)
{
    char path[MAX_PATH];
    BYTE hdr[44];
    DWORD got = 0;
    WAVEFORMATEX fmt;
    DSBUFFERDESC desc;
    HRESULT hr;
    DWORD play = 0, write = 0;

    stream_stop_locked(FALSE);
    g_notify_hwnd = notify;
    g_play_track = (UINT)file_track;
    if (file_track < 2 || file_track > Q2CD_MAX_TRACK)
        return FALSE;
    if (!ensure_ds(notify)) {
        log_event("DSOUND_FAIL", E_FAIL, (DWORD)file_track, 0);
        return FALSE;
    }

    lstrcpynA(path, g_dir, MAX_PATH);
    wsprintfA(path + lstrlenA(path), "music\\Track%02d.wav", file_track);
    g_file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_file == INVALID_HANDLE_VALUE) {
        log_event("FILE_OPEN_FAIL", HRESULT_FROM_WIN32(GetLastError()),
                  (DWORD)file_track, 0);
        return FALSE;
    }
    if (!ReadFile(g_file, hdr, sizeof(hdr), &got, NULL) || got != sizeof(hdr)) {
        close_file();
        return FALSE;
    }
    if (hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F' ||
        hdr[8] != 'W' || hdr[9] != 'A' || hdr[10] != 'V' || hdr[11] != 'E' ||
        *(WORD *)(hdr + 20) != WAVE_FORMAT_PCM ||
        *(WORD *)(hdr + 22) != 2 || *(DWORD *)(hdr + 24) != SAMPLE_RATE ||
        *(WORD *)(hdr + 34) != 16) {
        log_event("HEADER_INVALID", E_INVALIDARG, *(DWORD *)(hdr + 24), file_track);
        close_file();
        return FALSE;
    }
    g_data_size = *(DWORD *)(hdr + 40);
    g_data_pos = 0;
    g_eof_countdown = 0;
    mem_zero(&fmt, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = SAMPLE_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = 88200;
    mem_zero(&desc, sizeof(desc));
    desc.dwSize = 20;
    desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2;
    desc.dwBufferBytes = RING_BYTES;
    desc.lpwfxFormat = &fmt;
    hr = IDirectSound_CreateSoundBuffer(g_ds, &desc, &g_buffer, NULL);
    if (FAILED(hr)) {
        desc.dwFlags = DSBCAPS_GETCURRENTPOSITION2;
        hr = IDirectSound_CreateSoundBuffer(g_ds, &desc, &g_buffer, NULL);
    }
    if (FAILED(hr)) {
        log_event("CREATE_BUFFER", hr, (DWORD)file_track, 0);
        close_file();
        return FALSE;
    }
    g_has_volume = (desc.dwFlags & DSBCAPS_CTRLVOLUME) != 0;
    hr = fill_region(0, RING_BYTES);
    if (FAILED(hr)) {
        stream_stop_locked(FALSE);
        return FALSE;
    }
    apply_volume_locked();
    IDirectSoundBuffer_SetCurrentPosition(g_buffer, 0);
    hr = IDirectSoundBuffer_Play(g_buffer, 0, 0, DSBPLAY_LOOPING);
    if (FAILED(hr)) {
        stream_stop_locked(FALSE);
        return FALSE;
    }
    IDirectSoundBuffer_GetCurrentPosition(g_buffer, &play, &write);
    g_last_half = play >= HALF_BYTES;
    g_last_refill_tick = GetTickCount();
    g_playing = TRUE;
    g_paused = FALSE;
    ++g_play_count;
    start_poll_timer();
    log_event("PLAY", DS_OK, (DWORD)file_track, (DWORD)g_game);
    return TRUE;
}

static void stream_pause_locked(void)
{
    if (g_buffer && g_playing && !g_paused) {
        IDirectSoundBuffer_Stop(g_buffer);
        g_paused = TRUE;
        g_playing = FALSE;
    }
}

static BOOL stream_resume_locked(HWND notify)
{
    HRESULT hr;
    if (!g_buffer || !g_paused)
        return FALSE;
    if (notify)
        g_notify_hwnd = notify;
    hr = IDirectSoundBuffer_Play(g_buffer, 0, 0, DSBPLAY_LOOPING);
    if (FAILED(hr))
        return FALSE;
    g_paused = FALSE;
    g_playing = TRUE;
    return TRUE;
}

static void after_refill_locked(void)
{
    g_last_refill_tick = GetTickCount();
    if (g_eof_countdown && --g_eof_countdown == 0) {
        HWND hwnd = g_notify_hwnd;
        stream_stop_locked(FALSE);
        if (hwnd) {
            PostMessageA(hwnd, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, Q2CD_DEVICE_ID);
            ++g_notify_count;
            log_event("NOTIFY", DS_OK, (DWORD)g_play_track, (DWORD)hwnd);
        }
    }
}

static void refill_half_locked(DWORD refill, const char *why)
{
    HRESULT hr;
    hr = fill_region(refill, HALF_BYTES);
    ++g_refill_count;
#ifdef Q2CD_DEBUG
    log_event(why, hr, refill, g_data_pos);
#else
    why = why;
    if (FAILED(hr))
        log_event("REFILL_FAIL", hr, refill, g_data_pos);
#endif
    after_refill_locked();
}

static void poll_once_locked(void)
{
    DWORD play = 0, write = 0, half, now;
    HRESULT hr;
    if (!g_playing || !g_buffer)
        return;
    now = GetTickCount();
    hr = IDirectSoundBuffer_GetCurrentPosition(g_buffer, &play, &write);
    if (hr == DSERR_BUFFERLOST) {
        hr = IDirectSoundBuffer_Restore(g_buffer);
        ++g_restore_count;
        if (SUCCEEDED(hr)) {
            g_data_pos = 0;
            seek_data(0);
            g_eof_countdown = 0;
            hr = fill_region(0, RING_BYTES);
            if (SUCCEEDED(hr)) {
                IDirectSoundBuffer_SetCurrentPosition(g_buffer, 0);
                IDirectSoundBuffer_Play(g_buffer, 0, 0, DSBPLAY_LOOPING);
                g_last_refill_tick = now;
            }
        }
        return;
    }
    if (FAILED(hr))
        return;
    half = play >= HALF_BYTES;
    if (half != g_last_half) {
        refill_half_locked(half ? 0 : HALF_BYTES, "REFILL");
        g_last_half = half;
    } else if (g_last_refill_tick && now - g_last_refill_tick > 4500UL) {
        /* Win95 GetCurrentPosition can sit still; keep the ring moving. */
        g_last_half = !g_last_half;
        refill_half_locked(g_last_half ? 0 : HALF_BYTES, "STALL_REFILL");
    }
}

static void CALLBACK poll_timer(UINT id, UINT msg, DWORD user, DWORD d1, DWORD d2)
{
    id = id;
    msg = msg;
    user = user;
    d1 = d1;
    d2 = d2;
    if (!g_lock_ready)
        return;
    EnterCriticalSection(&g_lock);
    poll_once_locked();
    LeaveCriticalSection(&g_lock);
}

static void stop_poll_timer(void)
{
    if (g_timer_id && p_timeKillEvent) {
        p_timeKillEvent(g_timer_id);
        g_timer_id = 0;
    }
}

static void start_poll_timer(void)
{
    if (g_timer_id)
        return;
    if (p_timeBeginPeriod)
        p_timeBeginPeriod(10);
    if (p_timeSetEvent)
        g_timer_id = p_timeSetEvent(50, 10, (void *)poll_timer, 0, TIME_PERIODIC);
#ifdef Q2CD_DEBUG
    log_event(g_timer_id ? "TIMER_ON" : "TIMER_FAIL", DS_OK, g_timer_id, 0);
#endif
}

static void cache_wav_lengths(void)
{
    int track;
    for (track = 2; track <= Q2CD_MAX_TRACK; ++track) {
        char path[MAX_PATH];
        HANDLE h;
        BYTE hdr[44];
        DWORD got = 0;
        lstrcpynA(path, g_dir, MAX_PATH);
        wsprintfA(path + lstrlenA(path), "music\\Track%02d.wav", track);
        h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        if (ReadFile(h, hdr, sizeof(hdr), &got, NULL) && got == sizeof(hdr))
            g_wav_frames[track] = *(DWORD *)(hdr + 40) / 4UL;
        CloseHandle(h);
    }
}

static DWORD length_msf(int file_track)
{
    DWORD frames, sec, minutes, seconds, cd_frames;
    frames = g_wav_frames[file_track];
    if (!frames)
        frames = SAMPLE_RATE * 3UL;
    sec = frames / SAMPLE_RATE;
    minutes = sec / 60UL;
    seconds = sec % 60UL;
    cd_frames = ((frames % SAMPLE_RATE) * 75UL) / SAMPLE_RATE;
    return minutes | (seconds << 8) | (cd_frames << 16);
}

static MCIERROR handle_mci(UINT msg, DWORD flags, DWORD param)
{
    MCIERROR err = 0;
    EnterCriticalSection(&g_lock);
    switch (msg) {
    case MCI_OPEN: {
        MCI_OPEN_PARMSA *openp = (MCI_OPEN_PARMSA *)param;
        if (!openp || !(flags & MCI_OPEN_TYPE) || !openp->lpstrDeviceType ||
            !ascii_eqi(openp->lpstrDeviceType, "cdaudio")) {
            LeaveCriticalSection(&g_lock);
            return p_mciSendCommandA ? p_mciSendCommandA(0, msg, flags, param) : MCIERR_INVALID_DEVICE_NAME;
        }
        /* Re-apply from the DllMain snapshot. Never call GetCommandLineA
         * here: Q2's WinMain already punched NULs through it. */
        g_game = detect_game_from(g_cmdcopy);
        cache_wav_lengths();
        openp->wDeviceID = Q2CD_DEVICE_ID;
        log_event("OPEN", DS_OK, (DWORD)g_game, Q2CD_DEVICE_ID);
        break;
    }
    case MCI_CLOSE:
        stream_stop_locked(FALSE);
        log_event("CLOSE", DS_OK, 0, 0);
        break;
    case MCI_SET:
        if (flags & MCI_SET_DOOR_OPEN)
            break;
        if (flags & MCI_SET_DOOR_CLOSED)
            break;
        break;
    case MCI_STATUS: {
        MCI_STATUS_PARMS *st = (MCI_STATUS_PARMS *)param;
        DWORD item;
        int logical, file_track;
        if (!st) {
            err = MCIERR_MISSING_PARAMETER;
            break;
        }
        item = st->dwItem;
        poll_once_locked();
        if (item == MCI_STATUS_READY) {
            st->dwReturn = TRUE;
        } else if (item == MCI_STATUS_NUMBER_OF_TRACKS) {
            st->dwReturn = Q2CD_MAX_TRACK;
        } else if (item == MCI_STATUS_MODE) {
            if (g_playing)
                st->dwReturn = MCI_MODE_PLAY;
            else if (g_paused)
                st->dwReturn = MCI_MODE_PAUSE;
            else
                st->dwReturn = MCI_MODE_STOP;
        } else if (item == MCI_CDA_STATUS_TYPE_TRACK) {
            logical = (int)st->dwTrack;
            st->dwReturn = (logical == 1) ? MCI_CDA_TRACK_OTHER : MCI_CDA_TRACK_AUDIO;
        } else if (item == MCI_STATUS_LENGTH) {
            logical = (int)st->dwTrack;
            file_track = remap_track(logical);
            if (file_track < 2 || file_track > Q2CD_MAX_TRACK)
                file_track = logical;
            st->dwReturn = length_msf(file_track);
        } else {
            st->dwReturn = 0;
        }
        break;
    }
    case MCI_PLAY: {
        MCI_PLAY_PARMS *play = (MCI_PLAY_PARMS *)param;
        HWND hwnd = NULL;
        int logical, file_track;
        BOOL ok;
        if (play && (flags & MCI_NOTIFY))
            hwnd = (HWND)play->dwCallback;
        if (!(flags & MCI_FROM) && g_paused) {
            ok = stream_resume_locked(hwnd);
            err = ok ? 0 : MCIERR_HARDWARE;
            break;
        }
        logical = play ? (int)(play->dwFrom & 0xFF) : 0;
        file_track = remap_track(logical);
        log_event("PLAY_REQUEST", DS_OK, (DWORD)logical, (DWORD)file_track);
        ok = stream_play_locked(file_track, hwnd);
        err = ok ? 0 : MCIERR_HARDWARE;
        break;
    }
    case MCI_STOP:
        stream_stop_locked(FALSE);
        log_event("STOP", DS_OK, 0, 0);
        break;
    case MCI_PAUSE:
        stream_pause_locked();
        log_event("PAUSE", DS_OK, 0, 0);
        break;
#ifdef MCI_RESUME
    case MCI_RESUME:
#else
    case 0x0855:
#endif
        if (!stream_resume_locked(g_notify_hwnd))
            err = MCIERR_HARDWARE;
        break;
    default:
        err = 0;
        break;
    }
    LeaveCriticalSection(&g_lock);
    return err;
}

static BOOL is_our_device(MCIDEVICEID id)
{
    return id == Q2CD_DEVICE_ID;
}

MCIERROR WINAPI mciSendCommandA(MCIDEVICEID id, UINT msg, DWORD flags, DWORD param)
{
    if (msg == MCI_OPEN || is_our_device(id))
        return handle_mci(msg, flags, param);
    if (!p_mciSendCommandA)
        return MCIERR_CANNOT_LOAD_DRIVER;
    return p_mciSendCommandA(id, msg, flags, param);
}

MCIERROR WINAPI mciSendStringA(LPCSTR cmd, LPSTR ret, UINT len, HWND hwnd)
{
    if (cmd && ascii_matchi(cmd, "cdaudio")) {
        /* Q2 uses mciSendCommandA only; keep string path out of the real CD. */
        if (ret && len)
            ret[0] = 0;
        hwnd = hwnd;
        return 0;
    }
    if (!p_mciSendStringA)
        return MCIERR_CANNOT_LOAD_DRIVER;
    return p_mciSendStringA(cmd, ret, len, hwnd);
}

BOOL WINAPI mciGetErrorStringA(DWORD err, LPSTR buf, UINT len)
{
    if (p_mciGetErrorStringA)
        return p_mciGetErrorStringA(err, buf, len);
    if (buf && len)
        buf[0] = 0;
    return FALSE;
}

DWORD WINAPI timeGetTime(void)
{
    if (p_timeGetTime)
        return p_timeGetTime();
    return GetTickCount();
}

MMRESULT WINAPI timeBeginPeriod(UINT period)
{
    return p_timeBeginPeriod ? p_timeBeginPeriod(period) : 0;
}

MMRESULT WINAPI timeEndPeriod(UINT period)
{
    return p_timeEndPeriod ? p_timeEndPeriod(period) : 0;
}

MMRESULT WINAPI waveOutOpen(LPHWAVEOUT phwo, UINT id, LPCWAVEFORMATEX fmt, DWORD cb1, DWORD cb2, DWORD flags)
{
    if (!p_waveOutOpen)
        return MMSYSERR_NODRIVER;
    return p_waveOutOpen(phwo, id, fmt, cb1, cb2, flags);
}

MMRESULT WINAPI waveOutClose(HWAVEOUT hwo)
{
    return p_waveOutClose ? p_waveOutClose(hwo) : MMSYSERR_INVALHANDLE;
}

MMRESULT WINAPI waveOutReset(HWAVEOUT hwo)
{
    return p_waveOutReset ? p_waveOutReset(hwo) : MMSYSERR_INVALHANDLE;
}

MMRESULT WINAPI waveOutPrepareHeader(HWAVEOUT hwo, LPWAVEHDR hdr, UINT size)
{
    return p_waveOutPrepareHeader ? p_waveOutPrepareHeader(hwo, hdr, size) : MMSYSERR_INVALHANDLE;
}

MMRESULT WINAPI waveOutUnprepareHeader(HWAVEOUT hwo, LPWAVEHDR hdr, UINT size)
{
    return p_waveOutUnprepareHeader ? p_waveOutUnprepareHeader(hwo, hdr, size) : MMSYSERR_INVALHANDLE;
}

MMRESULT WINAPI waveOutWrite(HWAVEOUT hwo, LPWAVEHDR hdr, UINT size)
{
    return p_waveOutWrite ? p_waveOutWrite(hwo, hdr, size) : MMSYSERR_INVALHANDLE;
}

UINT WINAPI joyGetNumDevs(void)
{
    return p_joyGetNumDevs ? p_joyGetNumDevs() : 0;
}

MMRESULT WINAPI joyGetDevCapsA(UINT id, LPJOYCAPSA caps, UINT size)
{
    return p_joyGetDevCapsA ? p_joyGetDevCapsA(id, caps, size) : MMSYSERR_NODRIVER;
}

MMRESULT WINAPI joyGetPosEx(UINT id, LPJOYINFOEX info)
{
    return p_joyGetPosEx ? p_joyGetPosEx(id, info) : MMSYSERR_NODRIVER;
}

UINT WINAPI auxGetNumDevs(void)
{
    UINT realn = p_auxGetNumDevs ? p_auxGetNumDevs() : 0;
    return realn + 1;
}

MMRESULT WINAPI auxGetDevCapsA(UINT id, LPAUXCAPSA caps, UINT size)
{
    UINT realn = p_auxGetNumDevs ? p_auxGetNumDevs() : 0;
    if (id != realn)
        return p_auxGetDevCapsA ? p_auxGetDevCapsA(id, caps, size) : MMSYSERR_BADDEVICEID;
    if (!caps || size < sizeof(AUXCAPSA))
        return MMSYSERR_INVALPARAM;
    mem_zero(caps, size);
    caps->wMid = 1;
    caps->wPid = 1;
    caps->vDriverVersion = 0x0400;
    lstrcpynA(caps->szPname, "Q2CD PCM", sizeof(caps->szPname));
    caps->wTechnology = AUXCAPS_CDAUDIO;
    caps->dwSupport = AUXCAPS_VOLUME;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI auxGetVolume(UINT id, DWORD *vol)
{
    UINT realn = p_auxGetNumDevs ? p_auxGetNumDevs() : 0;
    if (id != realn)
        return p_auxGetVolume ? p_auxGetVolume(id, vol) : MMSYSERR_BADDEVICEID;
    if (!vol)
        return MMSYSERR_INVALPARAM;
    *vol = g_aux_volume;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI auxSetVolume(UINT id, DWORD vol)
{
    UINT realn = p_auxGetNumDevs ? p_auxGetNumDevs() : 0;
    WORD left;
    if (id != realn)
        return p_auxSetVolume ? p_auxSetVolume(id, vol) : MMSYSERR_BADDEVICEID;
    g_aux_volume = vol;
    left = (WORD)(vol & 0xFFFF);
    g_volume_raw = left >> 8;
    if (g_lock_ready) {
        EnterCriticalSection(&g_lock);
        apply_volume_locked();
        LeaveCriticalSection(&g_lock);
    }
    log_event("VOLUME", DS_OK, (DWORD)g_volume_raw, vol);
    return MMSYSERR_NOERROR;
}

static FARPROC must_proc(const char *name)
{
    FARPROC p = GetProcAddress(g_real, name);
    return p;
}

static BOOL bind_real_winmm(HMODULE mod)
{
    if (!mod || mod == g_self)
        return FALSE;
    g_real = mod;
    p_timeGetTime = (PFN_timeGetTime)must_proc("timeGetTime");
    p_timeBeginPeriod = (PFN_timePeriod)must_proc("timeBeginPeriod");
    p_timeEndPeriod = (PFN_timePeriod)must_proc("timeEndPeriod");
    p_timeSetEvent = (PFN_timeSetEvent)must_proc("timeSetEvent");
    p_timeKillEvent = (PFN_timeKillEvent)must_proc("timeKillEvent");
    p_waveOutOpen = (PFN_waveOutOpen)must_proc("waveOutOpen");
    p_waveOutClose = (PFN_waveOutH)must_proc("waveOutClose");
    p_waveOutReset = (PFN_waveOutH)must_proc("waveOutReset");
    p_waveOutPrepareHeader = (PFN_waveOutHdr)must_proc("waveOutPrepareHeader");
    p_waveOutUnprepareHeader = (PFN_waveOutHdr)must_proc("waveOutUnprepareHeader");
    p_waveOutWrite = (PFN_waveOutHdr)must_proc("waveOutWrite");
    p_joyGetNumDevs = (PFN_joyGetNumDevs)must_proc("joyGetNumDevs");
    p_joyGetDevCapsA = (PFN_joyGetDevCapsA)must_proc("joyGetDevCapsA");
    p_joyGetPosEx = (PFN_joyGetPosEx)must_proc("joyGetPosEx");
    p_mciSendCommandA = (PFN_mciSendCommandA)must_proc("mciSendCommandA");
    p_mciSendStringA = (PFN_mciSendStringA)must_proc("mciSendStringA");
    p_mciGetErrorStringA = (PFN_mciGetErrorStringA)must_proc("mciGetErrorStringA");
    p_auxGetNumDevs = (PFN_auxGetNumDevs)must_proc("auxGetNumDevs");
    p_auxGetDevCapsA = (PFN_auxGetDevCapsA)must_proc("auxGetDevCapsA");
    p_auxGetVolume = (PFN_auxVol)must_proc("auxGetVolume");
    p_auxSetVolume = (PFN_auxSetVol)must_proc("auxSetVolume");
    return p_mciSendCommandA && p_timeGetTime && p_waveOutOpen;
}

static BOOL load_real_winmm(void)
{
    char sys[MAX_PATH];
    char alias[MAX_PATH];
    UINT n;
    HMODULE mod;

    n = GetSystemDirectoryA(sys, MAX_PATH);
    if (!n || n >= MAX_PATH - 12)
        return FALSE;
    if (sys[n - 1] != '\\')
        lstrcatA(sys, "\\");
    lstrcatA(sys, "winmm.dll");

    /* Win9x matches DLLs by base name, so LoadLibrary of the system
       winmm.dll returns this shim. Copy the on-disk system mixer to a
       different name and load that instead. */
    lstrcpynA(alias, g_dir, MAX_PATH);
    lstrcatA(alias, "winmmsys.dll");
    CopyFileA(sys, alias, FALSE);

    mod = LoadLibraryA(alias);
    if (bind_real_winmm(mod))
        return TRUE;

    mod = LoadLibraryA(sys);
    return bind_real_winmm(mod);
}

static void write_summary(void)
{
    char line[320];
    ++g_seq;
    wsprintfA(line,
              "EV seq=%u tick=%u stage=SUMMARY plays=%u notifies=%u refills=%u "
              "restores=%u failures=%u game=%u\r\n",
              g_seq, GetTickCount(), g_play_count, g_notify_count,
              g_refill_count, g_restore_count, g_failure_count, (DWORD)g_game);
    log_line(line);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    char *slash;
    reserved = reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = instance;
        DisableThreadLibraryCalls(instance);
        GetModuleFileNameA(instance, g_dir, MAX_PATH);
        slash = g_dir;
        while (*slash)
            ++slash;
        while (slash > g_dir && slash[-1] != '\\' && slash[-1] != '/')
            --slash;
        *slash = 0;
        log_line("BOOT remap-snap\r\n");
        lstrcpynA(g_cmdcopy, GetCommandLineA() ? GetCommandLineA() : "", 1024);
        g_game = detect_game_from(g_cmdcopy);
        {
            char cmdlog[200];
            char clip[96];
            lstrcpynA(clip, g_cmdcopy, 96);
            wsprintfA(cmdlog, "CMD g=%lu %s\r\n", (unsigned long)g_game, clip);
            log_line(cmdlog);
        }
        if (!load_real_winmm()) {
            log_line("BOOT_FAIL real winmm not bound\r\n");
        }
        InitializeCriticalSection(&g_lock);
        g_lock_ready = TRUE;
        g_volume_ds = volume_to_ds(g_volume_raw);
        log_event("INIT", DS_OK, (DWORD)g_game, Q2CD_DEVICE_ID);
    } else if (reason == DLL_PROCESS_DETACH) {
        stop_poll_timer();
        if (g_lock_ready) {
            EnterCriticalSection(&g_lock);
            stream_stop_locked(FALSE);
            if (g_ds) {
                IDirectSound_Release(g_ds);
                g_ds = NULL;
            }
            LeaveCriticalSection(&g_lock);
            write_summary();
            DeleteCriticalSection(&g_lock);
            g_lock_ready = FALSE;
        }
        if (g_dsound_mod)
            FreeLibrary(g_dsound_mod);
        if (g_real)
            FreeLibrary(g_real);
    }
    return TRUE;
}
