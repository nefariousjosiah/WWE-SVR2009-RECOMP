"""Translate struct offsets between two builds of the XDK D3D library (device layout changes).

    python tools/native/device_offsets.py OLD_IMAGE NEW_IMAGE OLD_FUNC=NEW_FUNC [...]

For each pair of matching functions (from match_functions.py, verified), the bodies are aligned
and every load/store whose shape matches but whose displacement differs is counted as
"old offset -> new offset". Stack accesses (r1) are skipped. Offsets seen with one consistent
translation across functions are reliable; conflicting ones are printed with all their targets.
"""
import collections
import difflib
import sys

from match_functions import Image, shape


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    old, new = Image(sys.argv[1]), Image(sys.argv[2])
    seen = collections.defaultdict(collections.Counter)  # old offset -> Counter(new offset)
    for pair in sys.argv[3:]:
        a_addr, b_addr = (int(x, 16) for x in pair.split("="))
        fa, fb = old.containing(a_addr), new.containing(b_addr)
        if not fa or not fb:
            print(f"{pair}: function not found", file=sys.stderr)
            continue
        wa, wb = old.words(*fa), new.words(*fb)
        sm = difflib.SequenceMatcher(None, [shape(w) for w in wa], [shape(w) for w in wb],
                                     autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag != "equal":
                continue
            for x, y in zip(wa[i1:i2], wb[j1:j2]):
                op = x >> 26
                if not (32 <= op <= 55 or op == 14) or ((x >> 16) & 31) in (0, 1):
                    continue  # loads/stores and addi with a base register other than r0/r1
                ox, oy = x & 0xFFFF, y & 0xFFFF
                ox -= 0x10000 if ox & 0x8000 else 0
                oy -= 0x10000 if oy & 0x8000 else 0
                seen[ox][oy] += 1
    for ox in sorted(seen):
        targets = seen[ox]
        if len(targets) == 1:
            oy, n = next(iter(targets.items()))
            delta = f"{oy - ox:+d}" if oy != ox else "same"
            print(f"  {ox:6d} (0x{ox & 0xFFFF:04X}) -> {oy:6d} (0x{oy & 0xFFFF:04X})  {delta:>6}  x{n}")
        else:
            alts = ", ".join(f"{oy} (0x{oy & 0xFFFF:04X}) x{n}" for oy, n in targets.most_common())
            print(f"  {ox:6d} (0x{ox & 0xFFFF:04X}) -> CONFLICT: {alts}")


if __name__ == "__main__":
    main()
