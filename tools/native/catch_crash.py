"""Run the native build under lldb and print the stack of the first real guest crash.

    lldb -b -o "command script import tools/native/catch_crash.py" -o "catch_crash <seconds>" \
         -- out/build/win-amd64-release/svr2009.exe <args>

The runtime handles GPU register (MMIO) and memory-watch accesses through access-violation
exceptions, so every exception is checked: those on guest addresses the runtime maps (MMIO at
0x7FC80000, physical views >= 0xA0000000) are passed on; the first other access violation inside
the guest address space stops the run and prints the stack, with recompiled code named
sub_XXXXXXXX through the generated function table (generated/default/svr2009_init.cpp).
"""

import bisect
import os
import re
import time

import lldb

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GUEST_BASE = 0x100000000  # host address of guest address 0


def map_symbols():
    """(rva, name) from the lld-link map next to the exe (svr2009.map), sorted by rva.

    Symbol lines look like: "0001:0074b710  sub_82239E68  000000014074c710  svr2009_recomp.21.cpp.obj"
    (section:offset, name, address at the preferred base 0x140000000, object)."""
    path = os.path.join(ROOT, "out", "build", "win-amd64-release", "svr2009.map")
    preferred_base = 0x140000000
    syms = []
    if os.path.exists(path):
        for line in open(path, errors="replace"):
            parts = line.split()
            if (len(parts) >= 3 and re.fullmatch(r"[0-9a-f]{4}:[0-9a-f]{8}", parts[0])
                    and re.fullmatch(r"[0-9a-f]{16}", parts[2])):
                syms.append((int(parts[2], 16) - preferred_base, parts[1]))
    syms.sort()
    return syms


def catch_crash(debugger, command, result, internal_dict):
    seconds = float(command or 120)
    target = debugger.GetSelectedTarget()
    debugger.SetAsync(False)
    error = lldb.SBError()
    launch = target.GetLaunchInfo()
    launch.SetWorkingDirectory(ROOT)
    process = target.Launch(launch, error)
    if not error.Success():
        print("launch failed:", error)
        return
    start = time.time()
    while time.time() - start < seconds:
        state = process.GetState()
        if state in (lldb.eStateExited, lldb.eStateDetached):
            print("process exited:", process.GetExitStatus())
            return
        if state != lldb.eStateStopped:
            process.Continue()
            continue
        thread = process.GetSelectedThread()
        desc = thread.GetStopDescription(256) or ""
        m = re.search(r"(?:writing|reading) location 0x([0-9a-fA-F]+)", desc)
        if thread.GetStopReason() == lldb.eStopReasonException and m:
            host = int(m.group(1), 16)
            guest = host - GUEST_BASE
            if 0 <= guest < 0x100000000 and not (0x7FC80000 <= guest < 0x7FD00000) and guest < 0xA0000000:
                print("CRASH:", desc)
                syms = map_symbols()
                addrs = [a for a, _ in syms]
                for i, frame in enumerate(thread):
                    if i > 24:
                        break
                    name = frame.GetFunctionName() or ""
                    module = frame.GetModule()
                    mod = module.GetFileSpec().GetFilename() or "?"
                    if not name and mod.lower() == "svr2009.exe" and syms:
                        base = module.GetObjectFileHeaderAddress().GetLoadAddress(target)
                        k = bisect.bisect_right(addrs, frame.GetPC() - base) - 1
                        if k >= 0:
                            name = f"{syms[k][1]}+0x{frame.GetPC() - base - syms[k][0]:X}"
                    print(f"  #{i} {mod}`{name or hex(frame.GetPC())}")
                process.Kill()
                return
        process.Continue()
    print("no crash within", seconds, "s")
    process.Kill()


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f catch_crash.catch_crash catch_crash")
