"""Name the frames of logs/crash_trace.txt (written by src/native/crash_trace.cpp).

    python tools/native/symbolize_crash.py [trace] [map]

Frames in svr2009.exe are given as module offsets; the lld-link map (svr2009.map, preferred base
0x140000000) turns them into symbol+offset, recompiled guest code showing as sub_XXXXXXXX.
"""

import bisect
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PREFERRED_BASE = 0x140000000


def load_map(path):
    syms = []
    for line in open(path, errors="replace"):
        parts = line.split()
        if (len(parts) >= 3 and re.fullmatch(r"[0-9a-f]{4}:[0-9a-f]{8}", parts[0])
                and re.fullmatch(r"[0-9a-f]{16}", parts[2])):
            obj = parts[-1] if len(parts) >= 4 else ""
            syms.append((int(parts[2], 16) - PREFERRED_BASE, parts[1], obj))
    syms.sort()
    return syms


def main():
    trace = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "logs", "crash_trace.txt")
    map_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        ROOT, "out", "build", "win-amd64-release", "svr2009.map")
    syms = load_map(map_path)
    addrs = [s[0] for s in syms]
    for line in open(trace):
        line = line.rstrip()
        m = re.match(r"#(\d+) (\S+)\+0x([0-9A-Fa-f]+)", line)
        if m and m.group(2).lower() == "svr2009.exe":
            rva = int(m.group(3), 16)
            k = bisect.bisect_right(addrs, rva) - 1
            if k >= 0:
                rva0, name, obj = syms[k]
                line = f"#{m.group(1)} {name}+0x{rva - rva0:X}  ({obj})"
        print(line)


if __name__ == "__main__":
    main()
