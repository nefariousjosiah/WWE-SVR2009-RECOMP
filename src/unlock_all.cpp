// Unlock everything (settings menu > Unlock everything, cvar svr_unlock_all).
//
// SvR 2009 has no Shop. What is unlocked (superstars, arenas, attires and the rest) is 41 bytes in
// the profile (profile +0x1C328), one per item. sub_8255E740 asks about item N, and a few small
// functions turn something else into its item: sub_8255E9B0 for a superstar, and sub_8255EDD8,
// sub_8255EEE8, sub_8255EF50 and sub_8255EFD8 for the other kinds. Unlocking an item (sub_8255E7E0)
// only sets its byte when the item's entry in the reward data (*0x82C6A490, then two bytes per item,
// +0 non-zero) says it can be unlocked.
//
// With the setting on, all of those answer "unlocked"; the hooks write nothing to the profile. But
// the game's reward code only unlocks an item when the check says it is locked, so rewards earned
// with the setting on aren't recorded (the settings menu warns about it).

#include <rex/cvar.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

#include <cstdint>

REXCVAR_DEFINE_BOOL(svr_unlock_all, false, "SvR",
                    "Every superstar, arena, attire and other reward is available; the save itself "
                    "is not changed");

namespace {
constexpr uint32_t kRewards = 0x82C6A490;
constexpr uint32_t kItemCount = 41;  // sub_8255E740 and the loop in sub_8255E860 stop here

const uint8_t* Guest(uint32_t va) {
  auto* rt = rex::Runtime::instance();
  return rt && rt->memory() && va ? rt->memory()->TranslateVirtual<const uint8_t*>(va) : nullptr;
}

uint32_t GuestU32(uint32_t va) {
  const uint8_t* p = Guest(va);
  return p ? (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3] : 0;
}

bool CanUnlock(uint32_t item) {
  if (item >= kItemCount) {
    return false;
  }
  const uint32_t rewards = GuestU32(kRewards);
  const uint32_t entries = rewards ? GuestU32(rewards) : 0;
  const uint8_t* entry = entries ? Guest(entries + item * 2) : nullptr;
  return entry && entry[0] != 0;
}
}  // namespace

// sub_8255E740 just read item N's byte (r3; r31 = the item).
void SvrUnlockAllHook(PPCRegister& r3, PPCRegister& r31) {
  if (REXCVAR_GET(svr_unlock_all) && r3.u32 == 0 && CanUnlock(r31.u32)) {
    r3.u64 = 1;
  }
}

// The checks for one kind of thing: true jumps to where they answer "available".
bool SvrUnlockAllOn() {
  return REXCVAR_GET(svr_unlock_all);
}
