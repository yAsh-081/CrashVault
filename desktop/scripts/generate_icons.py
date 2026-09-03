#!/usr/bin/env python3
"""Generate CrashVault app icons (stdlib only)."""
from __future__ import annotations

import struct
import zlib
from pathlib import Path


def png_chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def write_icon(path: Path, size: int) -> None:
    rows: list[bytes] = []
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            border = x < max(1, size // 32) or y < max(1, size // 32)
            border = border or x >= size - max(1, size // 32) or y >= size - max(1, size // 32)
            inner = size // 4 <= x < 3 * size // 4 and size // 4 <= y < 3 * size // 4
            if border:
                rgba = (40, 44, 52, 255)
            elif inner:
                rgba = (76, 139, 245, 255)
            else:
                rgba = (24, 27, 33, 255)
            row.extend(rgba)
        rows.append(bytes(row))

    compressed = zlib.compress(b"".join(rows), 9)
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", compressed)
        + png_chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def main() -> None:
    root = Path(__file__).resolve().parent.parent / "src-tauri" / "icons"
    write_icon(root / "32x32.png", 32)
    write_icon(root / "128x128.png", 128)
    write_icon(root / "128x128@2x.png", 256)
    write_icon(root / "icon.png", 512)
    print(f"Icons written to {root}")


if __name__ == "__main__":
    main()
