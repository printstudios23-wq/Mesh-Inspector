#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Draws the Mesh Inspector logo (the blue isometric cube from the title bar) and writes resources/app.ico.

Plain Python 3, no dependencies:   python3 resources/make_icon.py
The ICO holds PNG images at 16, 24, 32, 48, 64, 128 and 256 pixels.
"""
import math
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SIZES = [16, 24, 32, 48, 64, 128, 256]
SS = 8  # supersampling factor

# colours of the three faces: top, right, left (same as the logo drawn in src/ui.cpp)
TOP, RIGHT, LEFT = (130, 190, 255), (70, 125, 215), (45, 90, 175)


def hexagon(cx, cy, r):
    return [(cx + r * math.cos(math.radians(-90 + 60 * i)), cy + r * math.sin(math.radians(-90 + 60 * i))) for i in range(6)]


def fill_convex(grid, n, poly, value):
    """Marks every sample whose centre lies inside the convex polygon."""
    ys = [p[1] for p in poly]
    for y in range(max(0, int(math.floor(min(ys)))), min(n, int(math.ceil(max(ys))))):
        yc = y + 0.5
        xs = []
        for i in range(len(poly)):
            (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % len(poly)]
            if (y0 <= yc < y1) or (y1 <= yc < y0):
                xs.append(x0 + (yc - y0) * (x1 - x0) / (y1 - y0))
        if len(xs) >= 2:
            lo, hi = min(xs), max(xs)
            row = grid[y]
            for x in range(max(0, int(math.ceil(lo - 0.5))), min(n, int(math.floor(hi - 0.5)) + 1)):
                row[x] = value


def render(size):
    n = size * SS
    grid = [[0] * n for _ in range(n)]
    c = (n / 2, n / 2 + 0.0)
    r = n * 0.485
    p = hexagon(c[0], c[1], r)
    fill_convex(grid, n, p, 3)                       # whole hexagon in the "left" colour ...
    fill_convex(grid, n, [p[5], p[0], p[1], c], 1)   # ... then the top face
    fill_convex(grid, n, [c, p[1], p[2], p[3]], 2)   # ... and the right face
    colours = {1: TOP, 2: RIGHT, 3: LEFT}
    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            acc = [0, 0, 0]
            covered = 0
            for sy in range(SS):
                line = grid[y * SS + sy]
                for sx in range(SS):
                    v = line[x * SS + sx]
                    if v:
                        col = colours[v]
                        acc[0] += col[0]; acc[1] += col[1]; acc[2] += col[2]
                        covered += 1
            if covered:
                row += bytes((acc[0] // covered, acc[1] // covered, acc[2] // covered, covered * 255 // (SS * SS)))
            else:
                row += b"\0\0\0\0"
        rows.append(bytes(row))
    return rows


def png(size, rows):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + r for r in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    images = [(s, png(s, render(s))) for s in SIZES]
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, blobs = b"", b""
    for s, data in images:
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset + len(blobs))
        blobs += data
    with open(os.path.join(HERE, "app.ico"), "wb") as f:
        f.write(header + entries + blobs)
    print("wrote app.ico:", ", ".join("%d px" % s for s in SIZES))


if __name__ == "__main__":
    main()
