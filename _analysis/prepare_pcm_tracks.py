#!/usr/bin/env python3
"""Convert GOG Quake II music/TrackXX.ogg files to canonical 22 kHz PCM WAVs."""
from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
import sys
from pathlib import Path
from shutil import which

ROOT = Path(__file__).resolve().parents[1]
MUSIC = ROOT / "music"
RATE = 22050
CHANNELS = 2
BITS = 16
TRACKS = range(2, 22)


def find_ffmpeg() -> str:
    exe = which("ffmpeg")
    if exe:
        return exe
    raise SystemExit("ffmpeg not found on PATH")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_canonical_pcm(path: Path) -> bool:
    header = path.read_bytes()[:44]
    if len(header) < 44:
        return False
    return (
        header[0:4] == b"RIFF"
        and header[8:12] == b"WAVE"
        and header[12:16] == b"fmt "
        and struct.unpack_from("<I", header, 16)[0] == 16
        and struct.unpack_from("<H", header, 20)[0] == 1
        and struct.unpack_from("<H", header, 22)[0] == CHANNELS
        and struct.unpack_from("<I", header, 24)[0] == RATE
        and struct.unpack_from("<H", header, 34)[0] == BITS
        and header[36:40] == b"data"
    )


def canonicalize_wav(path: Path) -> None:
    data = path.read_bytes()
    if len(data) < 44 or data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise RuntimeError(f"not a WAVE: {path}")
    pos = 12
    fmt = None
    pcm = None
    while pos + 8 <= len(data):
        chunk_id = data[pos : pos + 4]
        chunk_size = struct.unpack_from("<I", data, pos + 4)[0]
        chunk_data = data[pos + 8 : pos + 8 + chunk_size]
        if chunk_id == b"fmt ":
            fmt = chunk_data[:16] if chunk_size >= 16 else chunk_data
        elif chunk_id == b"data":
            pcm = chunk_data
            break
        pos += 8 + chunk_size
        if chunk_size % 2 == 1:
            pos += 1
    if fmt is None or pcm is None:
        raise RuntimeError(f"missing fmt/data: {path}")
    if len(fmt) < 16:
        raise RuntimeError(f"short fmt: {path}")
    out = struct.pack("<4sI4s4sI", b"RIFF", 36 + len(pcm), b"WAVE", b"fmt ", 16)
    out += fmt[:16]
    out += struct.pack("<4sI", b"data", len(pcm))
    out += pcm
    path.write_bytes(out)


def convert_track(ffmpeg: str, src: Path, dest: Path) -> None:
    cmd = [
        ffmpeg,
        "-y",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(src),
        "-acodec",
        "pcm_s16le",
        "-ar",
        str(RATE),
        "-ac",
        str(CHANNELS),
        "-map_metadata",
        "-1",
        "-fflags",
        "+bitexact",
        "-flags:v",
        "+bitexact",
        "-flags:a",
        "+bitexact",
        str(dest),
    ]
    subprocess.run(cmd, check=True)
    canonicalize_wav(dest)
    if not is_canonical_pcm(dest):
        raise RuntimeError(f"non-canonical WAV: {dest}")


def write_manifest(rows: list[dict], ffmpeg: str, dest: Path) -> None:
    lines = [
        "Quake II PCM/CDDA soundtrack manifest",
        f"FFmpeg executable\t{ffmpeg}",
        f"Output sample rate\t{RATE}",
        "channels\t2",
        "bits\t16",
        "",
        "track\tsource\tsource_sha256\twav\twav_sha256\twav_frames\tduration_seconds\tdata_bytes",
    ]
    for row in rows:
        lines.append(
            f"{row['track']}\t{row['source']}\t{row['source_sha256']}\t"
            f"{row['wav']}\t{row['wav_sha256']}\t{row['frames']}\t"
            f"{row['duration']:.6f}\t{row['data_bytes']}"
        )
    dest.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--music", type=Path, default=MUSIC)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    ffmpeg = find_ffmpeg()
    rows = []
    args.music.mkdir(parents=True, exist_ok=True)

    for track in TRACKS:
        src = args.music / f"Track{track:02d}.ogg"
        dest = args.music / f"Track{track:02d}.wav"
        if not src.is_file():
            print(f"missing {src}", file=sys.stderr)
            return 1
        if dest.exists() and not args.force and is_canonical_pcm(dest):
            print(f"skip existing {dest.name}")
        else:
            print(f"convert {src.name} -> {dest.name}")
            convert_track(ffmpeg, src, dest)
        data_bytes = dest.stat().st_size - 44
        frames = data_bytes // (CHANNELS * (BITS // 8))
        rows.append(
            {
                "track": track,
                "source": src.name,
                "source_sha256": sha256_file(src),
                "wav": dest.name,
                "wav_sha256": sha256_file(dest),
                "frames": frames,
                "duration": frames / RATE,
                "data_bytes": data_bytes,
            }
        )

    manifest = args.music / "MANIFEST.TXT"
    write_manifest(rows, ffmpeg, manifest)
    total = sum(row["data_bytes"] + 44 for row in rows)
    print(f"Wrote {len(rows)} tracks, {total} bytes of WAV")
    print(f"Manifest: {manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
