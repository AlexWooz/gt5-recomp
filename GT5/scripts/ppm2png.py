"""Convert frame dumps to PNG and report how much of each is non-black: ppm2png.py <dir>"""
import glob
import os
import struct
import sys
import zlib


def read_ppm(path):
    d = open(path, "rb").read()
    fields, pos = [], 0
    while len(fields) < 4:
        while d[pos] in b" \t\r\n":
            pos += 1
        end = pos
        while d[end] not in b" \t\r\n":
            end += 1
        fields.append(d[pos:end])
        pos = end
    return int(fields[1]), int(fields[2]), d[pos + 1:]


def write_png(path, w, h, rgb):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


for p in sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm"))):
    w, h, rgb = read_ppm(p)
    nb = sum(1 for i in range(0, len(rgb), 3 * 97) if rgb[i] or rgb[i + 1] or rgb[i + 2]) * 97
    png = p[:-4] + ".png"
    write_png(png, w, h, rgb)
    os.remove(p)
    print(f"{os.path.basename(png)} {w}x{h} nonblack~{nb * 100 // (w * h)}%")
