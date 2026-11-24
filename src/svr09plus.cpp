// SVR09+ (yumboy11's mod, version 1.1) support: on while the mod is in use (mods.h).
//
// The mod's files (superstars, menus, its DLC pack and save) load like any mod's, but it also
// patches default.xex, and this port runs the game's code recompiled ahead of time, so the patch
// is redone here as hooks. Of its four changes, the one that keeps entrances at 60 fps isn't needed
// (the port already does), and the title ID it changes only moves the mod's saves to a folder of
// their own (mods do that here anyway). The other two:
//   - two calls of the superstar's gender (sub_82558A08) answer 0, so anyone can hold any title;
//   - the check whether a superstar may enter a match type (sub_8255F710) answers yes, so Divas can
//     be in every match and the Royal Rumble.

#include <rex/ppc.h>

#include "mods.h"

// At 0x824AFE88 and 0x824B2658, a call of sub_82558A08: true skips it with r3 = 0.
bool SvrSvr09PlusGenderHook(PPCRegister& r3) {
  if (!svr::ModsSvr09Plus())
    return false;
  r3.u64 = 0;
  return true;
}

// sub_8255F710 entry: true returns r3 = 1 at once.
bool SvrSvr09PlusMatchHook(PPCRegister& r3) {
  if (!svr::ModsSvr09Plus())
    return false;
  r3.u64 = 1;
  return true;
}
