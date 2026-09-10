#!/usr/bin/env python3
"""Stamp a PE so Windows 95 accepts OS/subsystem 4.0."""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def stamp(path: Path) -> None:
    data = bytearray(path.read_bytes())
    if data[0:2] != b"MZ":
        raise SystemExit(f"not MZ: {path}")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe : pe + 4] != b"PE\x00\x00":
        raise SystemExit(f"not PE: {path}")
    opt = pe + 24
    if struct.unpack_from("<H", data, opt)[0] != 0x10B:
        raise SystemExit(f"not PE32: {path}")
    struct.pack_into("<H", data, opt + 40, 4)  # OS major
    struct.pack_into("<H", data, opt + 42, 0)  # OS minor
    struct.pack_into("<H", data, opt + 48, 4)  # subsystem major
    struct.pack_into("<H", data, opt + 50, 0)  # subsystem minor
    dllchar = struct.unpack_from("<H", data, opt + 70)[0]
    struct.pack_into("<H", data, opt + 70, dllchar & ~0x0140)
    path.write_bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()
    for path in args.files:
        stamp(path)
        print(f"stamped {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
