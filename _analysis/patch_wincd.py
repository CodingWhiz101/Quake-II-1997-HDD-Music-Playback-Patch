#!/usr/bin/env python3
"""Point PE imports at wincd.dll so Windows 95 will load the local shim."""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OLD_NAMES = (b"WINMM.dll", b"WINMM.DLL", b"winmm.dll", b"Winmm.dll")
NEW_NAME = b"wincd.dll"


def patch(path: Path) -> int:
    data = bytearray(path.read_bytes())
    hits = 0
    for old in OLD_NAMES:
        start = 0
        while True:
            at = data.find(old, start)
            if at < 0:
                break
            if at + 9 < len(data) and data[at + 9] == 0:
                data[at : at + 9] = NEW_NAME
                hits += 1
            start = at + 1
    if not hits:
        if b"wincd.dll" in data:
            print(f"already patched {path.name}")
            return 0
        print(f"FAIL no WINMM import in {path}", file=sys.stderr)
        return 1
    path.write_bytes(data)
    print(f"patched {path.name} ({hits} name(s) -> wincd.dll)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="*", type=Path)
    args = parser.parse_args()
    files = args.files
    if not files:
        exe = ROOT / "quake2.exe"
        bak = ROOT / "quake2.exe.bak"
        if exe.is_file() and not bak.is_file():
            shutil.copy2(exe, bak)
            print(f"backed up {bak.name}")
        files = [exe]
    rc = 0
    for path in files:
        if not path.is_file():
            print(f"FAIL missing {path}", file=sys.stderr)
            rc = 1
            continue
        rc |= patch(path)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
