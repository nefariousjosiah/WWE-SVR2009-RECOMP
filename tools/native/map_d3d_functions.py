"""Find the Xbox 360 D3D library functions in SvR 2008 by aligning them with re:Blue's.

re:Blue (https://github.com/zolaware/reblue, BSD-3-Clause) names ~330 functions of the statically
linked XDK D3D library in Blue Dragon, with addresses and sizes (config/functions.toml). SvR 2008
links D3D9 2.0.5632; Blue Dragon links a nearby XDK build, so most library functions have the same
size and order, while the address offset drifts as functions in between grow or shrink.

This script sizes every SvR function from the generated C++ (one "// insn" comment per original
instruction), then aligns the two size sequences with a gapped alignment (Needleman-Wunsch style):
SvR functions may be skipped freely (Blue Dragon's list only covers named functions), skipping a
Blue Dragon function costs more, and equal sizes score highest.

    python tools/native/map_d3d_functions.py [path/to/reblue/config/functions.toml]

Without a path it downloads functions.toml from GitHub. Writes:
    config/native/d3d_functions_aligned.txt   every Blue Dragon function and its SvR match
Matches with a different size ('~') are candidates to verify by hand; a few large functions whose
code changed between XDK builds (Swap, draws, Resolve, tiling) don't align and are found by
behaviour instead (see docs/native-renderer.md).
"""

import glob
import os
import re
import sys
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REBLUE_FUNCTIONS_URL = (
    "https://raw.githubusercontent.com/zolaware/reblue/main/config/functions.toml")

BD_RANGE = (0x82460000, 0x824A0000)   # Blue Dragon's D3D library
SVR_RANGE = (0x82220000, 0x82290000)  # where SvR's D3D library sits (offset ~ -0x235000)


def load_reblue(path):
    text = (open(path, encoding="utf-8").read() if path
            else urllib.request.urlopen(REBLUE_FUNCTIONS_URL).read().decode("utf-8"))
    entries = []
    for line in text.splitlines():
        m = re.match(r'\s*0x([0-9A-Fa-f]+)\s*=\s*\{\s*name\s*=\s*"([^"]+)"'
                     r'(?:,\s*size\s*=\s*0x([0-9A-Fa-f]+))?', line)
        if m and m.group(3):
            addr = int(m.group(1), 16)
            if BD_RANGE[0] <= addr < BD_RANGE[1]:
                entries.append((addr, m.group(2), int(m.group(3), 16)))
    return sorted(entries)


def svr_function_sizes():
    sizes, cur, count = {}, None, 0
    define = re.compile(r'^(?:DEFINE_REX_FUNC|PPC_FUNC_IMPL)\((?:__imp__)?sub_([0-9A-F]+)\)')
    insn = re.compile(r'^\t// [a-z]')
    for path in glob.glob(os.path.join(ROOT, "generated", "default", "svr2009_recomp.*.cpp")):
        for line in open(path, encoding="utf-8", errors="replace"):
            m = define.match(line)
            if m:
                if cur is not None:
                    sizes[cur] = count * 4
                cur, count = int(m.group(1), 16), 0
            elif cur is not None and insn.match(line):
                count += 1
        if cur is not None:
            sizes[cur] = count * 4
            cur = None
    return sizes


def score(a, b):
    if a == b:
        return 6
    diff = abs(a - b)
    if diff <= 8:
        return 3
    if diff <= 0.12 * max(a, b):
        return 1
    return -4


def align(bd, svr, skip_svr=0.05, skip_bd=1.5):
    n, m = len(bd), len(svr)
    prev = [0.0] * (m + 1)  # leading SvR functions are free to skip
    back = []
    for i in range(1, n + 1):
        cur = [0.0] * (m + 1)
        bk = bytearray(m + 1)
        cur[0], bk[0] = prev[0] - skip_bd, 1
        size = bd[i - 1][2]
        for j in range(1, m + 1):
            match = prev[j - 1] + score(size, svr[j - 1][1])
            gap_bd = prev[j] - skip_bd
            gap_svr = cur[j - 1] - skip_svr
            if match >= gap_bd and match >= gap_svr:
                cur[j], bk[j] = match, 0
            elif gap_bd >= gap_svr:
                cur[j], bk[j] = gap_bd, 1
            else:
                cur[j], bk[j] = gap_svr, 2
        back.append(bk)
        prev = cur
    i, j = n, max(range(m + 1), key=lambda k: prev[k])
    pairs = {}
    while i > 0 and j > 0:
        step = back[i - 1][j]
        if step == 0:
            pairs[i - 1] = j - 1
            i, j = i - 1, j - 1
        elif step == 1:
            i -= 1
        else:
            j -= 1
    return pairs


def main():
    bd = load_reblue(sys.argv[1] if len(sys.argv) > 1 else None)
    sizes = svr_function_sizes()
    svr = sorted((a, s) for a, s in sizes.items() if SVR_RANGE[0] <= a < SVR_RANGE[1])
    pairs = align(bd, svr)
    lines, exact = [], 0
    for i, (addr, name, size) in enumerate(bd):
        if i in pairs:
            svr_addr, svr_size = svr[pairs[i]]
            exact += size == svr_size
            mark = "=" if size == svr_size else "~"
            lines.append(f"{svr_addr:08X} {mark} {name:52s} size {svr_size:5d} (Blue Dragon "
                         f"{addr:08X} size {size:5d})")
        else:
            lines.append(f"???????? ! {name:52s} not aligned (Blue Dragon {addr:08X} size {size:5d})")
    out = os.path.join(ROOT, "config", "native", "d3d_functions_aligned.txt")
    with open(out, "w") as f:
        f.write("# SvR address, '=' same size / '~' different size / '!' not found, name, sizes\n")
        f.write("# Generated by tools/native/map_d3d_functions.py from re:Blue's functions.toml\n")
        f.write("\n".join(lines) + "\n")
    print(f"{len(pairs)} of {len(bd)} aligned, {exact} with identical size -> {out}")


if __name__ == "__main__":
    main()
