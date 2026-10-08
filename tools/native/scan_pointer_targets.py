"""List code addresses the game references as data: candidate functions for config/pointer_funcs.toml.

Windows replacement for the Ghidra step of tools/sweep.sh (ghidra_scripts/ExportPointerTargets).

    SVR_DUMP_IMAGE=logs/default_image.bin   (launch the game once: the loaded image is dumped)
    python tools/native/scan_pointer_targets.py logs/default_image.bin > logs/pointer_targets.txt
    python tools/pointer_funcs.py logs/pointer_targets.txt > config/pointer_funcs.toml

Sources of candidates:
  - every aligned big-endian 32-bit value in the image that points into an executable section
    (vtables, callback tables, jump tables);
  - lis/addi (or lis/ori) pairs in code that build such an address (callbacks passed in registers).
A candidate is kept when it looks like a function entry: the instruction before it ends a function
(blr, bctr, trap, an unconditional b, or padding) or the target starts with a typical prologue. pointer_funcs.py
drops the ones analysis already knows and prunes any that split a real function.
"""
import struct
import sys

IMAGE_BASE = 0x82000000


def sections(image):
    """(start, end, executable) for each PE section, as guest addresses."""
    e_lfanew = struct.unpack_from("<I", image, 0x3C)[0]
    count = struct.unpack_from("<H", image, e_lfanew + 6)[0]
    opt_size = struct.unpack_from("<H", image, e_lfanew + 20)[0]
    table = e_lfanew + 24 + opt_size
    out = []
    for i in range(count):
        off = table + i * 40
        vsize, vaddr = struct.unpack_from("<II", image, off + 8)
        characteristics = struct.unpack_from("<I", image, off + 36)[0]
        out.append((IMAGE_BASE + vaddr, IMAGE_BASE + vaddr + vsize,
                     bool(characteristics & 0x20000000)))  # IMAGE_SCN_MEM_EXECUTE
    return out


def word(image, addr):
    off = addr - IMAGE_BASE
    if off < 0 or off + 4 > len(image):
        return None
    return struct.unpack_from(">I", image, off)[0]


def ends_function(insn):
    if insn is None:
        return False
    if insn in (0x4E800020, 0x4E800420, 0x7FE00008, 0x00000000):  # blr, bctr, trap, padding
        return True
    return (insn >> 26) == 18 and (insn & 3) == 0  # b (not bl, not absolute)


def has_prologue(insn):
    if insn is None:
        return False
    return insn in (0x7D8802A6, 0x7C0802A6) or (insn >> 16) == 0x9421  # mflr r12/r0, stwu r1


def main():
    image = open(sys.argv[1], "rb").read()
    secs = sections(image)
    code = [(s, e) for s, e, x in secs if x]
    if not code:
        sys.exit("no executable section found (is this a dumped image?)")

    def in_code(a):
        return a % 4 == 0 and any(s <= a < e for s, e in code)

    def jump_table(a):
        # Switch tables sit inline after the bctr that uses them: two code pointers in a row.
        return in_code(word(image, a) or 1) and in_code(word(image, a + 4) or 1)

    def plausible(a):
        if word(image, a) in (None, 0) or jump_table(a):  # padding is never an entry
            return False
        return ends_function(word(image, a - 4)) or has_prologue(word(image, a))

    targets = set()
    # 1. Data pointers anywhere in the image.
    for off in range(0, len(image) - 3, 4):
        v = struct.unpack_from(">I", image, off)[0]
        if in_code(v) and plausible(v):
            targets.add(v)
    # 2. lis rX,hi ; addi/ori rY,rX,lo in code.
    for s, e in code:
        for a in range(s, e - 4, 4):
            i0 = word(image, a)
            if i0 is None or (i0 >> 26) != 15 or ((i0 >> 16) & 31) != 0:  # lis = addis rD,0,imm
                continue
            rd = (i0 >> 21) & 31
            hi = (i0 & 0xFFFF) << 16
            for b in range(a + 4, min(a + 24, e), 4):
                i1 = word(image, b)
                if i1 is None:
                    break
                op = i1 >> 26
                if op in (14, 24) and ((i1 >> 16) & 31) == rd:  # addi / ori rY,rD,imm
                    lo = i1 & 0xFFFF
                    v = (hi + (lo - 0x10000 if op == 14 and lo & 0x8000 else lo)) & 0xFFFFFFFF
                    if in_code(v) and plausible(v):
                        targets.add(v)
                    break
    for t in sorted(targets):
        print(f"0x{t:08X}")
    print(f"{len(targets)} candidate targets in {len(code)} code section(s)", file=sys.stderr)


if __name__ == "__main__":
    main()
