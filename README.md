# Quake II 3.20 — CD Audio from the Hard Drive on Windows 95

Hi everyone, this patch is the result of wanting to play original Quake II
(and both mission packs) on my 1990s PC under Windows 95, without a working
optical drive. I used my legally owned GOG Quad Damage copy, converted the
soundtrack to period-safe PCM, and intercepted the engine’s existing MCI CD
path instead of replacing `quake2.exe` with a source port. Yes, AI was used
for basically most of this project. I am not a software developer, just a
dude who likes playing games on old machines, and doesn’t have time to learn
to code. Hopefully this helps people in the future.

- CodingWhiz101, September 2026

Proven on **Windows 95 OSR2 + Pentium 233 MMX + Voodoo2 + Ensoniq Soundscape**.
SFX already play from the PAKs. This work only replaces Red Book / MCI CD
music.

> **GitHub ships `Q2Win95Prep.exe` and source, not game data.** Do not publish
> `quake2.exe`, PAK files, cinematics, or `music\TrackXX.wav`. Run the
> preparer on **your own** legally obtained GOG Quad Damage install
> (`goggame-1441704824`).

---

## Results

| Capability | Status |
| --- | --- |
| Base-game CD tracks 2–11 from HDD WAVs | Works |
| Track plays to EOF and Quake II loops it (`MM_MCINOTIFY`) | Works |
| SFX + music mix on the Ensoniq | Works |
| Ground Zero (`+set game rogue`) track remap | Works |
| The Reckoning (`+set game xatrix`) mixed-disc remap | Works |
| Attract demos pick the remapped files | Works |
| Timedemo `demo1.dm2` at 640×480 3dfx, sound on | 35.1 fps on the test box |
| Red Book CD required | **No** |

Stock 3.20 is kept. The only patch to `quake2.exe` is renaming the `WINMM.dll`
import to `wincd.dll` (Windows 95 will not load a local file named
`winmm.dll`).

---

## What the preparer creates

`Q2Win95Prep.exe` reads a GOG Quake II folder (never modifies it) and writes a
sibling `Copy-To-Windows-95` directory:

- `music\Track02.wav` … `Track21.wav` — 22050 Hz, 16-bit stereo PCM (~293 MB)
- `wincd.dll` — Watcom Win95 PE 4.0 MCI/DirectSound shim
- `quake2.exe` — 3.20 with that import rename (`quake2.exe.bak` is the stock file)
- `SETUP95.BAT` (optional) / `VERIFY95.BAT` (optional) / `XATRIX.BAT` / `ROGUE.BAT`
- `WIN95_README.TXT` / `PREPINFO.TXT`

`winmmsys.dll` is **not** created on Windows 7/10/11. On the Windows 95 PC,
starting `quake2.exe` copies **that machine’s** `C:\WINDOWS\SYSTEM\winmm.dll`
into the game folder. `SETUP95.BAT` is optional: it does the same copy if you
want the file before the first launch.

Do not copy GOG’s launcher, Vorbis DLLs, or a modern `winmm.dll` / `winmmsys.dll`
onto the 95 machine.

---

## Use the tested game version

Stock Quake II 3.20 (`quake2.exe` **before** the import rename), SHA-256:

```text
9C7AE7872321996FB72FCCC081636821F2FB78132368CC321361E732AE66913E
```

After the 9-byte import patch used on the test machine:

```text
63E9DEFD0CD6BC20A3B92DD0928B290B517000D85474C223F033683CDBDFF33F
```

The preparer treats a matching stock or tested-patched hash as exact. A
different hash shows a warning and asks before continuing. Do not apply this
to a source port or a later re-release executable.

A newly compiled `wincd.dll` will not necessarily match a historical DLL hash
(Open Watcom can embed different build metadata). Rebuild with `_analysis\build.cmd`
and prove it on hardware.

---

## This repository

This GitHub repo **is** the preparer. `Q2Win95Prep.exe`, its C++ sources,
`wincd.dll`, and `_analysis\` (shim source) all live at the **root**. There is
no nested `Q2Win95Prep\` folder.

Download `Q2Win95Prep.exe` (or build it here) and run it against **your** GOG
Quake II folder. `SETUP95.BAT`, `VERIFY95.BAT`, `XATRIX.BAT`, `ROGUE.BAT`, and
`WIN95_README.TXT` are **generated** inside `Copy-To-Windows-95`; they are not
separate downloads.

---

| File | Purpose |
| --- | --- |
| [Q2Win95Prep.exe](Q2Win95Prep.exe) | Double-click this on a modern PC (put it in your GOG `QuakeII` folder, or browse to that folder) |
| [build.cmd](build.cmd) | MSVC x86 rebuild of the preparer; embeds `wincd.dll` |
| [main.cpp](main.cpp) / `prep_*.cpp` | Preparer GUI and conversion/copy logic |
| [wincd.dll](wincd.dll) | Prebuilt Watcom Win95 shim (already inside the .exe; needed only to rebuild) |
| [_analysis/q2cd_winmm.c](_analysis/q2cd_winmm.c) | Shim source (Open Watcom; not needed if you only run the .exe) |
| [_analysis/build.cmd](_analysis/build.cmd) | Rebuild `wincd.dll`, then re-run root `build.cmd` to embed it |
| [_analysis/prepare_pcm_tracks.py](_analysis/prepare_pcm_tracks.py) | Optional FFmpeg conversion (the preparer uses stb_vorbis) |
| [_analysis/patch_wincd.py](_analysis/patch_wincd.py) | Optional standalone import rename |
| [third_party/stb_vorbis.c](third_party/stb_vorbis.c) | OGG decoder used at prepare time |

---

## Quick start (two parts)

### Part 1 — modern PC

1. Install GOG **Quake II Quad Damage**. Put `Q2Win95Prep.exe` in that
   `QuakeII` folder (or browse to it). The GOG install is never modified.
2. Double-click `Q2Win95Prep.exe` and choose **Prepare**.
3. Copy the generated `Copy-To-Windows-95` folder to the Windows 95 PC
   (for example `D:\Games\QuakeII`).

To build the preparer from source on a PC with Visual C++ (x86 `/MT`,
Windows 7 subsystem), from this repo’s root:

```bat
build.cmd
```

That embeds the `wincd.dll` already in this folder. Rebuild the shim with
`_analysis\build.cmd` only if you changed `q2cd_winmm.c`, then run root
`build.cmd` again so the new DLL is baked into `Q2Win95Prep.exe`.

### Part 2 — Windows 95 PC

1. Copy the folder onto that PC. Never bring a `winmmsys.dll` from
   Windows 98/XP/10/11.
2. Start `quake2.exe` (or `XATRIX.BAT` / `ROGUE.BAT`). The shim copies
   `C:\WINDOWS\SYSTEM\winmm.dll` from **that machine** to `winmmsys.dll`.
   `SETUP95.BAT` is optional; it only does that same copy ahead of time so
   `VERIFY95.BAT` can see the file without launching the game.
3. `VERIFY95.BAT` is optional. A missing `winmmsys.dll` before the first
   launch is normal.

Video on a Voodoo2: **3dfx OpenGL**, **640×480**, fullscreen, sync off.
8-bit textures **on** is the safe default; **off** looks a bit richer if the
card has RAM to spare.

---

## Launch

| Campaign | Command |
| --- | --- |
| Quake II | `quake2.exe` |
| The Reckoning | `XATRIX.BAT` (`quake2.exe +set game xatrix`) |
| Ground Zero | `ROGUE.BAT` (`quake2.exe +set game rogue`) |

Retail mission packs were separate Start Menu shortcuts with those same
`+set game` arguments. The in-game Easy/Medium/Hard menu is not a campaign
picker. Changing `game` from the console after startup will **not** remap
music; the shim reads the command line when it loads.

Keep `cd_nocd` at `0`.

---

## Soundtrack map

GOG puts all three discs in one `music\` folder:

- `Track02`–`Track11` — original Quake II CD
- `Track12`–`Track21` — Ground Zero CD
- The Reckoning disc was a mix of those two

Stock 3.20 always asks MCI for tracks **2–11**. A real expansion disc in the
drive would play *that* disc’s track 2. With one GOG folder, the shim remaps:

| Launch | Logical track N | File |
| --- | --- | --- |
| Base | N | `TrackNN.wav` |
| `+set game rogue` | N | `Track(N+10).wav` |
| `+set game xatrix` | 2…11 | 9, 13, 14, 7, 16, 2, 15, 3, 4, 18 |

That Reckoning table matches Yamagi Quake II’s GOG mapper. Hearing a familiar
base-game song during The Reckoning is often correct (several of its tracks
*are* reused originals). Ground Zero’s track 2 is `Track12.wav`, not
`Track02.wav`.

---

## How it works

1. Quake II 3.20 talks to CD audio through ordinary `mciSendCommand` `cdaudio`
   (play/stop/pause/status/notify). Aux volume still goes through real winmm.
2. Windows 95 ignores a game-folder `winmm.dll`, so the shim is `wincd.dll`
   and the EXE import name is patched to match.
3. `wincd.dll` claims a fake CD device (`0x4D01`). Other winmm calls are
   forwarded to `winmmsys.dll`. On load the shim copies this PC’s
   `GetSystemDirectory\winmm.dll` there, which is why `SETUP95.BAT` is
   optional.
4. Playback is a DirectSound secondary ring (8 s, refilled every 4 s). The
   refill timer is real winmm `timeSetEvent` (a `CreateThread` from `DllMain`
   never ran on this OS).
5. End of file posts `MM_MCINOTIFY` so `cd loop` restarts the track the same
   way a real CD would.
6. Expansion identity is snapshotted from `GetCommandLineA()` in `DllMain`.
   Quake II later tokenizes that string in place, so a second lookup at
   `MCI_OPEN` would always see the base game.

This is the same idea as the Jedi Knight / Tomb Raider II HDD-PCM shims:
keep the 1998 binary, feed it PCM, don’t ship a source port.

---

## Optional timedemo

Base game, 3dfx already selected, console:

```text
timedemo 1
demomap demo1.dm2
```

Do not press keys until it finishes, then `timedemo 0`. Sound-off benches
from 1998 on a Pentium 233 + Voodoo2 were around 40 fps; sound on (including
this HDD shim) in the mid-30s is normal.

---

## Do not publish GOG game data

When putting the tooling on GitHub, publish source, scripts, documentation,
and hashes only.

Do not commit:

- `quake2.exe` / `quake2.exe.bak`
- `baseq2`, `xatrix`, `rogue` PAKs, `gamex86.dll`, or `video\*.cin`
- `music\TrackXX.ogg` or the generated WAVs
- GOG launchers, Vorbis DLLs, or a `winmmsys.dll` taken from a modern OS
- `Copy-To-Windows-95` output from a local prepare run

Each user should supply a purchased GOG install and run `Q2Win95Prep.exe` to
generate their own WAVs, patched EXE, and `Copy-To-Windows-95` folder.

*Quake II*, *The Reckoning*, *Ground Zero*, and their audio remain the
property of their respective owners.
