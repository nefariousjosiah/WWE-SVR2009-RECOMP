"""Index the generated recompiled code for reverse engineering (native renderer work).

Every original PowerPC instruction appears in generated/default/svr2009_recomp.*.cpp as a
"// <asm>" comment inside DEFINE_REX_FUNC(sub_XXXXXXXX), in address order. This builds, per
function: its instructions (address, text), the functions it calls (bl / tail b), the kernel
imports it calls (__imp__Name), and the reverse call graph.

    python tools/native/recomp_index.py                 # build logs/recomp_index.pickle
    python tools/native/recomp_index.py dis 8223A960    # disassembly with call names
    python tools/native/recomp_index.py callers 8223A960
    python tools/native/recomp_index.py imports VdSwap  # functions calling a kernel import
    python tools/native/recomp_index.py grep "lis r11,-16383"  # functions containing text
"""

import glob
import os
import pickle
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GEN = os.path.join(ROOT, "generated", "default")
INDEX = os.path.join(ROOT, "logs", "recomp_index.pickle")
NAMES = os.path.join(ROOT, "config", "native", "d3d_names.toml")

DEFINE = re.compile(r'^(?:DEFINE_REX_FUNC|PPC_FUNC_IMPL)\((?:__imp__)?sub_([0-9A-F]+)\)')
INSN = re.compile(r'^\t// ([a-z].*)$')
IMPORT = re.compile(r'__imp__([A-Za-z0-9_]+)\(ctx, base\)')
BRANCH = re.compile(r'^(bl|b)\s+0x([0-9a-f]+)$')


def build():
    funcs = {}
    for path in sorted(glob.glob(os.path.join(GEN, "svr2009_recomp.*.cpp"))):
        cur = None
        for line in open(path, encoding="utf-8", errors="replace"):
            m = DEFINE.match(line)
            if m:
                addr = int(m.group(1), 16)
                cur = funcs.setdefault(addr, {"insns": [], "calls": set(), "imports": set()})
                pc = addr
                continue
            if cur is None:
                continue
            m = INSN.match(line)
            if m:
                text = m.group(1).strip()
                cur["insns"].append((pc, text))
                b = BRANCH.match(text)
                if b:
                    cur["calls"].add(int(b.group(2), 16))
                pc += 4
                continue
            m = IMPORT.search(line)
            if m:
                cur["imports"].add(m.group(1))
    starts = sorted(funcs)
    callers = {}
    for addr, f in funcs.items():
        # Branches inside the function's own range are local control flow, not calls.
        end = addr + 4 * len(f["insns"])
        f["calls"] = {t for t in f["calls"] if not addr <= t < end}
        for t in f["calls"]:
            callers.setdefault(t, set()).add(addr)
    with open(INDEX, "wb") as out:
        pickle.dump({"funcs": funcs, "callers": callers, "starts": starts}, out)
    print(f"indexed {len(funcs)} functions -> {INDEX}")


def load():
    if not os.path.exists(INDEX):
        build()
    return pickle.load(open(INDEX, "rb"))


def names():
    out = {}
    if os.path.exists(NAMES):
        for line in open(NAMES):
            m = re.match(r'\s*0x([0-9A-Fa-f]+)\s*=\s*\{\s*name\s*=\s*"([^"]+)"', line)
            if m:
                out[int(m.group(1), 16)] = m.group(2)
    return out


def label(addr, nm):
    return f"sub_{addr:08X}" + (f" [{nm[addr]}]" if addr in nm else "")


def main():
    if len(sys.argv) < 2:
        build()
        return
    idx, nm = load(), names()
    funcs, callers = idx["funcs"], idx["callers"]
    cmd, arg = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else ""
    if cmd == "dis":
        a = int(arg, 16)
        print(label(a, nm), f"({4 * len(funcs[a]['insns'])} bytes)")
        for pc, text in funcs[a]["insns"]:
            b = BRANCH.match(text)
            extra = f"   -> {label(int(b.group(2), 16), nm)}" if b and int(b.group(2), 16) in funcs else ""
            print(f"  {pc:08X}: {text}{extra}")
        if funcs[a]["imports"]:
            print("  imports:", ", ".join(sorted(funcs[a]["imports"])))
    elif cmd == "callers":
        a = int(arg, 16)
        for c in sorted(callers.get(a, ())):
            print(label(c, nm), f"{4 * len(funcs[c]['insns'])} bytes")
    elif cmd == "imports":
        for a in sorted(funcs):
            if arg in funcs[a]["imports"]:
                print(label(a, nm), f"{4 * len(funcs[a]['insns'])} bytes")
    elif cmd == "grep":
        pat = re.compile(arg)
        for a in sorted(funcs):
            hits = [pc for pc, t in funcs[a]["insns"] if pat.search(t)]
            if hits:
                print(label(a, nm), f"{4 * len(funcs[a]['insns'])} bytes, {len(hits)} hits",
                      " ".join(f"{h:08X}" for h in hits[:6]))


if __name__ == "__main__":
    main()
