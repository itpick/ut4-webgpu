#!/usr/bin/env python3
# Converts a P6 (binary) PPM -- what DawnRHITest's readback path writes
# (RHIReadSurfaceData -> raw RGB bytes -> P6 PPM, see DawnRHITestMain.cpp)
# -- to a real PNG, using only the Python standard library (struct + zlib).
# No external deps (imagemagick/pnmtopng/PIL) needed on framepick or
# anywhere else this is run.
#
# Usage: ppm_to_png.py <in.ppm> <out.png>
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:2] == b"P6", "not a binary (P6) PPM"
    idx = 2
    vals = []
    while len(vals) < 3:
        while data[idx] in b" \t\r\n":
            idx += 1
        if data[idx : idx + 1] == b"#":
            while data[idx] not in b"\r\n":
                idx += 1
            continue
        start = idx
        while data[idx] not in b" \t\r\n":
            idx += 1
        vals.append(int(data[start:idx]))
    idx += 1  # single whitespace byte after maxval
    w, h, maxval = vals
    assert maxval == 255
    pixels = data[idx : idx + w * h * 3]
    assert len(pixels) == w * h * 3, "truncated PPM pixel data"
    return w, h, pixels


def write_png(path, w, h, rgb_bytes):
    def chunk(tag, data):
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # 8-bit RGB, no interlace
    stride = w * 3
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter type 0 (None) per scanline
        raw.extend(rgb_bytes[y * stride : (y + 1) * stride])
    idat = zlib.compress(bytes(raw), 9)
    with open(path, "wb") as f:
        f.write(sig)
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: ppm_to_png.py <in.ppm> <out.png>", file=sys.stderr)
        sys.exit(1)
    w, h, px = read_ppm(sys.argv[1])
    write_png(sys.argv[2], w, h, px)
    print(f"wrote {sys.argv[2]} ({w}x{h})")
