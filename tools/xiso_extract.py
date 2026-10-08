#!/usr/bin/env python3
"""Extract an Xbox 360 disc image (XDVDFS / "XISO") to a directory.

Handles Redump-style full images (XGD1/XGD2/XGD3 game partition offsets) and
rebuilt/trimmed XISOs (partition at 0).

    tools/xiso_extract.py game.iso assets/            # extract everything
    tools/xiso_extract.py game.iso --list             # list files only
    tools/xiso_extract.py game.iso assets/ --only default.xex
"""
import argparse
import os
import struct
import sys

SECTOR = 0x800
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Game partition offsets: rebuilt XISO, XGD2 (most 360 titles), XGD1, XGD3.
PARTITION_OFFSETS = [0x0, 0xFD90000, 0x18300000, 0x2080000]
ATTR_DIRECTORY = 0x10


def find_partition(f):
    for off in PARTITION_OFFSETS:
        f.seek(off + 32 * SECTOR)
        hdr = f.read(SECTOR)
        if hdr[:20] == MAGIC and hdr[0x7EC:0x7EC + 20] == MAGIC:
            root_sector, root_size = struct.unpack_from("<II", hdr, 20)
            return off, root_sector, root_size
    sys.exit("error: no XDVDFS volume descriptor found (not an Xbox 360 game image?)")


def walk(f, base, dir_sector, dir_size, prefix=""):
    """Yield (path, is_dir, sector, size) for the directory's binary tree."""
    if dir_size == 0:
        return
    f.seek(base + dir_sector * SECTOR)
    table = f.read(dir_size)
    seen = set()
    stack = [0]
    while stack:
        off = stack.pop()
        if off in seen or off + 14 > len(table):
            continue
        seen.add(off)
        left, right, sector, size, attr, name_len = struct.unpack_from("<HHIIBB", table, off)
        if left == 0xFFFF and right == 0xFFFF:
            continue  # padding
        name = table[off + 14:off + 14 + name_len].decode("ascii", "replace")
        path = f"{prefix}{name}"
        is_dir = bool(attr & ATTR_DIRECTORY)
        yield path, is_dir, sector, size
        if is_dir:
            yield from walk(f, base, sector, size, path + "/")
        if left:
            stack.append(left * 4)
        if right:
            stack.append(right * 4)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("iso")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--list", action="store_true", help="list contents instead of extracting")
    ap.add_argument("--only", action="append", default=[], help="extract only this path (repeatable)")
    args = ap.parse_args()
    if not args.list and not args.out:
        ap.error("output directory required unless --list")

    with open(args.iso, "rb") as f:
        base, root_sector, root_size = find_partition(f)
        print(f"XDVDFS partition at 0x{base:X}, root sector {root_sector}, root size {root_size}")
        total = files = 0
        only = {p.replace("\\", "/").lower() for p in args.only}
        for path, is_dir, sector, size in sorted(walk(f, base, root_sector, root_size)):
            if only and path.lower() not in only:
                continue
            if args.list:
                print(f"{'<DIR>' if is_dir else size:>12}  {path}")
                continue
            dest = os.path.join(args.out, path)
            if is_dir:
                os.makedirs(dest, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
            f.seek(base + sector * SECTOR)
            with open(dest, "wb") as out:
                remaining = size
                while remaining:
                    chunk = f.read(min(remaining, 8 << 20))
                    if not chunk:
                        sys.exit(f"error: image truncated while reading {path}")
                    out.write(chunk)
                    remaining -= len(chunk)
            files += 1
            total += size
        if not args.list:
            print(f"extracted {files} files, {total / 2**20:.1f} MiB -> {args.out}")


if __name__ == "__main__":
    main()
