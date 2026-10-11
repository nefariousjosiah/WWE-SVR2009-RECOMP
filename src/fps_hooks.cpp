// Frame-rate hooks for WWE SmackDown vs. Raw 2009.
//
// The game's statically linked Xbox 360 D3D Swap (sub_8225B850) reads the device's present
// interval (D3DPRESENT_INTERVAL_*, device+13596; 2008's XDK: +13572) at 0x8225B900 and turns it
// into the number of vblanks each frame stays on screen: ONE (1) -> every vblank (60 fps),
// TWO (2) -> every second vblank (30 fps). The flip is scheduled at "last flip + interval".
//
// SvrPresentIntervalHook runs right after that load (config/default.toml [[midasm_hook]]).

#include "frame_stats.h"

#include "core/memory_helpers.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

REXCVAR_DEFINE_BOOL(svr_60fps, false, "Game",
                    "Keep the game's frame-rate mode at 60 (50 PAL) instead of dropping to 30 (25) "
                    "in matches; game timing follows the mode (Road to WrestleMania cutscenes "
                    "keep 30, their captions are timed for it)");

// The game keeps a frame-rate mode in a global struct at 0x82C60BC8: +8 target fps (60/50/30/25),
// +12 mode lock, per-frame time-step floats (+28/+32/+36/+60/+64) and +40 ms per frame, all set
// together by SetFrameRate (sub_82476F38), "drop to 30" (sub_824771C0) and "back to 60"
// (sub_82477260); sub_82477920 turns 30/25 into D3D present interval 2. Same design as 2008
// (struct 0x82E6E458, functions 824707A8 / 82470A18 / 82470AC0 / 82471190).
// Overriding only the present interval made the game draw at 60 but still step its logic as if
// at 30 (entrances at double speed); keeping the mode at 60 changes both together.

void SvrPresentIntervalHook(PPCRegister& r11) {
  // Log each change of the interval D3D Swap uses (follows the frame-rate mode).
  static std::atomic<uint32_t> last_logged{0xFFFFFFFFu};
  uint32_t requested = r11.u32;
  if (last_logged.exchange(requested, std::memory_order_relaxed) != requested) {
    REXLOG_INFO("[fps] game present interval: {} ({})", requested,
                requested == 2 ? "30 fps" : requested <= 1 ? "60 fps" : "other");
  }
}

// SetFrameRate(fps), r3 = requested rate (sub_82476F38 entry).
void SvrSetFrameRateHook(PPCRegister& r3) {
  uint32_t requested = r3.u32;
  if (REXCVAR_GET(svr_60fps) && (requested == 30 || requested == 25)) {
    r3.u64 = requested == 30 ? 60 : 50;
    REXLOG_INFO("[fps] game set frame rate {}; using {}", requested, r3.u32);
  } else {
    REXLOG_INFO("[fps] game set frame rate {}", requested);
  }
}

// Scenes ask for the 30 fps mode from sub_824E2658, which reads the scene kind from the game state
// (+4076: 4 menus, which go back to 60; 7 a match; 8 a Road to WrestleMania cutscene). Cutscene
// captions are timed in 30 fps frames, so at 60 they show 3-5 s ahead of the voices. Players choose
// (settings menu > Cutscenes): 60 fps (the default) or the game's own 30 fps mode, as on the
// console, which keeps the captions in sync; the game returns to 60 when a cutscene ends.
REXCVAR_DEFINE_BOOL(svr_cutscene_30fps, false, "Game",
                    "Road to WrestleMania cutscenes keep the game's 30 fps mode, so their captions "
                    "(timed in 30 fps frames) line up with the voices; at 60 fps they show early");

constexpr uint32_t kSceneKindCutscene = 8;
static std::atomic<uint32_t> g_scene_kind{0};

// sub_824E2658 at the "drop to 30" call (0x824E2690), r3 = game state.
void SvrSceneKindHook(PPCRegister& r3) {
  g_scene_kind.store(bd::mem::load<uint32_t>(r3.u32 + 4076), std::memory_order_relaxed);
}

// "Back to 60 (50) fps mode" (sub_82477260 entry).
void SvrBackTo60Hook() { REXLOG_INFO("[fps] game switched back to 60 fps mode"); }

// "Drop to 30 (25) fps mode" (sub_824771C0 entry); returning true skips the function.
bool SvrSkip30ModeHook() {
  const uint32_t kind = g_scene_kind.exchange(0, std::memory_order_relaxed);
  const bool cutscene = kind == kSceneKindCutscene;
  bool skip = REXCVAR_GET(svr_60fps) && !(cutscene && REXCVAR_GET(svr_cutscene_30fps));
  static std::atomic<uint32_t> calls{0};
  uint32_t n = calls.fetch_add(1, std::memory_order_relaxed);
  if (n < 20 || n % 100 == 0) {
    REXLOG_INFO("[fps] game switched to 30 fps mode{} (scene kind {})",
                skip ? "; kept at 60" : (cutscene && REXCVAR_GET(svr_60fps)) ? "; cutscene, kept at 30" : "",
                kind);
  }
  return skip;
}

// Frame counter: SvrFrameSwapHook runs in the game's swap function (sub_8225BD40) right before it
// calls VdSwap at 0x8225BFA8, once per presented frame.
namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t kFrameHistory = 512;  // > 1 s of frames at any realistic rate
std::mutex frame_mutex;
Clock::time_point frame_times[kFrameHistory];
uint64_t frame_count = 0;
}  // namespace

void SvrFrameSwapHook() {
  std::lock_guard lock(frame_mutex);
  frame_times[frame_count % kFrameHistory] = Clock::now();
  ++frame_count;
}

SvrFrameStats GetSvrFrameStats() {
  std::lock_guard lock(frame_mutex);
  SvrFrameStats stats;
  stats.frames = frame_count;
  if (frame_count < 2) {
    return stats;
  }
  auto now = Clock::now();
  auto newest = frame_times[(frame_count - 1) % kFrameHistory];
  // Count frames presented within the last second.
  uint64_t available = std::min<uint64_t>(frame_count, kFrameHistory);
  uint64_t in_window = 0;
  Clock::time_point oldest = newest;
  for (uint64_t i = 1; i <= available; ++i) {
    auto t = frame_times[(frame_count - i) % kFrameHistory];
    if (now - t > std::chrono::seconds(1)) {
      break;
    }
    oldest = t;
    ++in_window;
  }
  stats.fps = double(in_window);
  if (in_window >= 2) {
    stats.frame_time_ms =
        std::chrono::duration<double, std::milli>(newest - oldest).count() / double(in_window - 1);
  }
  return stats;
}
