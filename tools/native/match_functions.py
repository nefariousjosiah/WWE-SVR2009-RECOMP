"""Find another build's version of known functions: map SvR 2008 addresses to SvR 2009.

    python tools/native/match_functions.py OLD_IMAGE NEW_IMAGE ADDR [ADDR ...]

Images are dumps made with SVR_DUMP_IMAGE. Function bounds come from each image's .pdata. An ADDR
may point inside a function (a midasm hook site): the containing function is matched and the
instruction is mapped through the alignment of the two bodies.

Instructions are compared by shape: immediates of D-form instructions (addresses, struct
offsets) and b/bl displacements are masked, everything else must be equal. Candidates are all
new functions of similar length; the best few are printed with a similarity score (1.0 = same
shape). A score well below 0.9, or two candidates close together, needs a manual check
(tools/native/recomp_index.py dis/callers).
"""
import bisect
import difflib
import struct
import sys

IMAGE_BASE = 0x82000000


class Image:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        self.funcs = []  # (start, length in instructions)
        e_lfanew = struct.unpack_from("<I", self.data, 0x3C)[0]
        count = struct.unpack_from("<H", self.data, e_lfanew + 6)[0]
        opt_size = struct.unpack_from("<H", self.data, e_lfanew + 20)[0]
        table = e_lfanew + 24 + opt_size
        for i in range(count):
            off = table + i * 40
            if self.data[off:off + 8].rstrip(b"\0") != b".pdata":
                continue
            vsize, vaddr = struct.unpack_from("<II", self.data, off + 8)
            for p in range(vaddr, vaddr + vsize - 7, 8):
                begin, info = struct.unpack_from(">II", self.data, p)
                length = (info >> 8) & 0x3FFFFF  # RUNTIME_FUNCTION.FunctionLength, instructions
                if begin and length:
                    self.funcs.append((begin, length))
        self.funcs.sort()
        if not self.funcs:
            sys.exit(f"{path}: no .pdata (is this a dumped image?)")
        self.funcs += self.leaf_functions()
        self.funcs.sort()
        self.starts = [f[0] for f in self.funcs]

    def leaf_functions(self):
        # Leaf functions have no unwind data: split the code between .pdata functions at each blr.
        leaves = []
        for (start, length), (nxt, _) in zip(self.funcs, self.funcs[1:]):
            a = start + 4 * length
            begin = None
            while a < nxt:
                w = struct.unpack_from(">I", self.data, a - IMAGE_BASE)[0]
                if begin is None and w != 0:
                    begin = a
                if begin is not None and w == 0x4E800020:  # blr
                    leaves.append((begin, (a + 4 - begin) // 4))
                    begin = None
                a += 4
        return leaves

    def words(self, start, length):
        off = start - IMAGE_BASE
        return list(struct.unpack_from(f">{length}I", self.data, off))

    def containing(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        if i >= 0 and addr < self.funcs[i][0] + 4 * self.funcs[i][1]:
            return self.funcs[i]
        return None


def shape(insn):
    op = insn >> 26
    if op == 18:  # b / bl: target differs between builds
        return insn & 0xFC000003
    if op in (7, 8, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29) or 32 <= op <= 55 or op in (58, 62):
        return insn & 0xFFFF0000  # D/DS-form: keep opcode and registers, drop the immediate
    return insn


def match(old, new, addr, top=3):
    func = old.containing(addr)
    if func is None:
        return None, []
    start, length = func
    a = [shape(w) for w in old.words(start, length)]
    for slack, floor in ((max(4, length // 4), 0.6), (length // 2 + 4, 0.4)):  # widen if nothing
        scored = []
        for nstart, nlength in new.funcs:
            if abs(nlength - length) > slack:
                continue
            b = [shape(w) for w in new.words(nstart, nlength)]
            sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
            if sm.real_quick_ratio() < floor or sm.quick_ratio() < floor:
                continue
            scored.append((sm.ratio(), nstart, nlength, sm))
        if scored:
            break
    scored.sort(key=lambda s: -s[0])
    return func, scored[:top]


def map_offset(sm, index):
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if i1 <= index < i2:
            if tag == "equal" or (tag == "replace" and i2 - i1 == j2 - j1):
                return j1 + (index - i1)
            return None
    return None


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    old, new = Image(sys.argv[1]), Image(sys.argv[2])
    for arg in sys.argv[3:]:
        addr = int(arg, 16)
        func, best = match(old, new, addr)
        if func is None:
            print(f"{addr:08X}: not inside any .pdata function of the old image")
            continue
        start, length = func
        where = f" (+0x{addr - start:X})" if addr != start else ""
        print(f"{addr:08X}{where}: old function {start:08X}, {length} instructions")
        if not best:
            print("    no candidate of similar length and shape")
        for ratio, nstart, nlength, sm in best:
            line = f"    {ratio:.3f}  {nstart:08X} ({nlength} instructions)"
            if addr != start:
                j = map_offset(sm, (addr - start) // 4)
                line += f"  -> {nstart + 4 * j:08X}" if j is not None else "  -> (site not aligned)"
            print(line)


if __name__ == "__main__":
    main()
