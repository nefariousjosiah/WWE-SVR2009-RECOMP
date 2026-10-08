"""Where do the game's threads spend their time? Reads logs/samples.txt (SVR_SAMPLE_STACKS=<ms>,
src/native/crash_trace.cpp) and reports, per thread, what it was doing in a time window.

    python tools/native/sample_report.py [--from MS] [--to MS] [--top N] [samples] [map]

Frames in svr2009.exe are named from the lld-link map (recompiled guest code shows as
sub_XXXXXXXX); frames in the SDK's DLLs from their exports (nearest export, demangled with
llvm-undname); Windows DLLs keep module+offset except the wait/sleep entry points.
Each thread's samples are grouped by "what it waits in / runs" (innermost named frame outside
Windows) and "on behalf of" (innermost game function), most common first.
"""

import argparse
import bisect
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GAME = os.path.basename(ROOT)
LLVM = r"C:\Program Files\LLVM\bin"
SYSTEM = {"ntdll.dll", "kernelbase.dll", "kernel32.dll", "win32u.dll", "user32.dll"}

sys.path.insert(0, os.path.dirname(__file__))
from symbolize_crash import load_map  # noqa: E402


def load_exports(dll):
    try:
        out = subprocess.run([os.path.join(LLVM, "llvm-readobj.exe"), "--coff-exports", dll],
                             capture_output=True, text=True).stdout
    except OSError:
        return []
    names = re.findall(r"Name: (\S+)\s+RVA: 0x([0-9A-Fa-f]+)", out)
    exports = sorted((int(rva, 16), name) for name, rva in names)
    return exports


def demangle(names):
    names = [n for n in names if n.startswith("?")]
    if not names:
        return {}
    try:
        out = subprocess.run([os.path.join(LLVM, "llvm-undname.exe")], input="\n".join(names),
                             capture_output=True, text=True).stdout.splitlines()
    except OSError:
        return {}
    result = {}
    it = iter(out)
    for line in it:
        if line in names:
            pretty = next(it, line)
            pretty = re.sub(r"\(.*", "", pretty)  # drop the argument list
            pretty = re.sub(r"^.*? (?=\S+$)", "", pretty)  # drop return type / qualifiers
            result[line] = pretty
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("samples", nargs="?", default=os.path.join(ROOT, "logs", "samples.txt"))
    ap.add_argument("map", nargs="?", default=None)
    ap.add_argument("--from", dest="t0", type=int, default=0)
    ap.add_argument("--to", dest="t1", type=int, default=1 << 62)
    ap.add_argument("--top", type=int, default=6)
    args = ap.parse_args()

    exe_name = None
    rows = []
    for line in open(args.samples, errors="replace"):
        parts = line.rstrip("\n").split(" ", 3)
        if len(parts) < 4:
            continue
        t, tid, name, frames = int(parts[0]), parts[1], parts[2], parts[3]
        if args.t0 <= t <= args.t1:
            rows.append((t, tid, name, frames.split(";") if frames else []))
        if exe_name is None:
            m = re.search(r"(svr\d+\.exe)\+", frames)
            exe_name = m.group(1) if m else None
    exe_name = exe_name or "svr2009.exe"
    map_path = args.map or os.path.join(ROOT, "out", "build", "win-amd64-release",
                                        exe_name.replace(".exe", ".map"))
    syms = load_map(map_path)
    addrs = [s[0] for s in syms]
    dll_exports = {}
    for dll in ("rexruntime.dll", "rexgpu-xenos.dll"):
        exports = load_exports(os.path.join(ROOT, "out", "build", "win-amd64-release", dll))
        dll_exports[dll] = ([e[0] for e in exports], [e[1] for e in exports])
    pretty = demangle(sorted({n for _, names in dll_exports.values() for n in names}))

    cache = {}

    def name_of(frame):
        if frame in cache:
            return cache[frame]
        m = re.match(r"(.+)\+0x([0-9A-Fa-f]+)$", frame)
        if not m:
            cache[frame] = (frame, False)
            return cache[frame]
        module, off = m.group(1), int(m.group(2), 16)
        lower = module.lower()
        result = (frame, False)
        if lower == exe_name:
            k = bisect.bisect_right(addrs, off) - 1
            if k >= 0:
                n = syms[k][1]
                result = (n, n.startswith("sub_") or n.startswith("__imp__sub_"))
        elif lower in dll_exports:
            rvas, names = dll_exports[lower]
            k = bisect.bisect_right(rvas, off) - 1
            if k >= 0:
                result = (f"{lower}!{pretty.get(names[k], names[k])}", False)
        elif lower in SYSTEM:
            result = (f"{lower}+0x{off:X}", False)
        cache[frame] = result
        return result

    by_thread = collections.defaultdict(list)
    for t, tid, name, frames in rows:
        by_thread[(tid, name)].append(frames)
    span = (max(r[0] for r in rows) - min(r[0] for r in rows)) / 1000 if rows else 0
    print(f"{len(rows)} samples over {span:.1f} s, window {args.t0}..{args.t1 if args.t1 < 1 << 62 else 'end'} ms\n")
    for (tid, name), stacks in sorted(by_thread.items(), key=lambda kv: -len(kv[1])):
        groups = collections.Counter()
        for frames in stacks:
            named = [name_of(f) for f in frames]
            inner = next((n for n, _ in named if not any(n.startswith(s) for s in SYSTEM)), "?")
            leaf_sys = named[0][0] if named and any(named[0][0].startswith(s) for s in SYSTEM) else ""
            guest = next((n for n, g in named if g), "-")
            groups[(leaf_sys, inner, guest)] += 1
        total = len(stacks)
        print(f"thread {tid} {name}: {total} samples")
        for (leaf_sys, inner, guest), n in groups.most_common(args.top):
            where = f"{inner}" + (f"  [in {leaf_sys}]" if leaf_sys else "")
            print(f"  {100 * n / total:5.1f}%  {where}  <- {guest}")
        print()


if __name__ == "__main__":
    main()
