// Frame hitch diagnostics (off unless svr_frame_diag_ms is set, e.g. 22): a frame slower than that
// is logged with the time it spent compiling pipelines, building textures and render targets,
// checking textures for changes, in draws, waiting for the GPU, the swap chain and (the game) for
// the emulated GPU. A slow frame with a long GPU wait is the graphics card's limit; one without
// is a stall on the CPU side. Every 10 s a summary line gives the slowest frame and how many
// were slow.

#include "svr_frame_diag.h"

#include <array>
#include <atomic>

#include <rex/cvar.h>

#include "core/logging.h"

REXCVAR_DEFINE_INT32(svr_frame_diag_ms, 0, "SvR",
                     "Log frames slower than this many ms with what they spent the time on "
                     "(0 = off)");

namespace bd::gpu {

namespace {

struct Slot {
  std::atomic<uint64_t> us{0};
  std::atomic<uint64_t> ticks{0};  // SvrDiagTimer's, converted in SvrDiagEndFrame
  std::atomic<uint32_t> n{0};
};
std::array<Slot, size_t(SvrDiag::kCount)> g_slots;

using Clock = std::chrono::steady_clock;

double Ms(uint64_t us) { return double(us) / 1000.0; }

}  // namespace

void SvrDiagAdd(SvrDiag what, uint64_t microseconds) {
  auto& slot = g_slots[size_t(what)];
  slot.us.fetch_add(microseconds, std::memory_order_relaxed);
  slot.n.fetch_add(1, std::memory_order_relaxed);
}

// The thread that presents (and calls SvrDiagEndFrame) is the one that draws, so its timers add
// to plain thread-local counters; any other thread uses the shared atomics.
struct LocalSlot {
  uint64_t ticks = 0;
  uint32_t n = 0;
};
thread_local bool t_frame_thread = false;
thread_local std::array<LocalSlot, size_t(SvrDiag::kCount)> t_local;

void SvrDiagAddTicks(SvrDiag what, uint64_t ticks) {
  if (t_frame_thread) {
    auto& l = t_local[size_t(what)];
    l.ticks += ticks;
    ++l.n;
    return;
  }
  auto& slot = g_slots[size_t(what)];
  slot.ticks.fetch_add(ticks, std::memory_order_relaxed);
  slot.n.fetch_add(1, std::memory_order_relaxed);
}

void SvrDiagEndFrame() {
  static Clock::time_point last{};
  static uint64_t frames = 0, logged = 0;
  static double window_max = 0.0;
  static uint32_t window_slow = 0, window_frames = 0;
  static Clock::time_point window_start = Clock::now();

  const auto now = Clock::now();
  const double frame_ms =
      last == Clock::time_point{} ? 0.0 : std::chrono::duration<double, std::milli>(now - last).count();
  last = now;
  ++frames;

  // Timer ticks per microsecond, measured against the OS clock since the first frame.
  static const uint64_t ticks0 = SvrDiagTicks();
  static const Clock::time_point clock0 = now;
  const double elapsed_us = std::chrono::duration<double, std::micro>(now - clock0).count();
  const double ticks_per_us =
      elapsed_us > 0.0 ? double(SvrDiagTicks() - ticks0) / elapsed_us : 0.0;

  uint64_t us[size_t(SvrDiag::kCount)];
  uint32_t n[size_t(SvrDiag::kCount)];
  t_frame_thread = true;
  for (size_t i = 0; i < g_slots.size(); ++i) {
    us[i] = g_slots[i].us.exchange(0, std::memory_order_relaxed);
    const uint64_t ticks =
        g_slots[i].ticks.exchange(0, std::memory_order_relaxed) + t_local[i].ticks;
    if (ticks_per_us > 0.0)
      us[i] += uint64_t(double(ticks) / ticks_per_us);
    n[i] = g_slots[i].n.exchange(0, std::memory_order_relaxed) + t_local[i].n;
    t_local[i] = {};
  }

  const int threshold = REXCVAR_GET(svr_frame_diag_ms);
  if (threshold <= 0 || frames < 120)  // skip startup
    return;

  static uint64_t window_draws = 0, window_draw_us = 0, window_check_us = 0, window_hash = 0,
                  window_gpu_wait_us = 0;
  window_gpu_wait_us += us[size_t(SvrDiag::kGuestGpuWait)];
  ++window_frames;
  window_draws += n[size_t(SvrDiag::kDraw)];
  window_draw_us += us[size_t(SvrDiag::kDraw)];
  window_check_us += us[size_t(SvrDiag::kTextureCheck)];
  window_hash += us[size_t(SvrDiag::kHashBytes)];
  if (frame_ms > window_max)
    window_max = frame_ms;
  if (frame_ms > threshold) {
    ++window_slow;
    if (logged < 2000) {
      ++logged;
      auto at = [&](SvrDiag d) { return size_t(d); };
      BD_INFO("[hitch] {:.1f} ms frame: pipelines {}x {:.1f} ms | new textures {}x {:.1f} ms | "
              "texture checks {:.1f} ms | new targets {}x {:.1f} ms | GPU wait {:.1f} ms | "
              "swapchain {:.1f} ms | draws {}x {:.1f} ms | {} texture checks hashing {:.1f} MB | "
              "game waiting for emulated GPU {}x {:.1f} ms",
              frame_ms, n[at(SvrDiag::kPipelineCompile)], Ms(us[at(SvrDiag::kPipelineCompile)]),
              n[at(SvrDiag::kTextureMirror)], Ms(us[at(SvrDiag::kTextureMirror)]),
              Ms(us[at(SvrDiag::kTextureCheck)]), n[at(SvrDiag::kTargetCreate)],
              Ms(us[at(SvrDiag::kTargetCreate)]), Ms(us[at(SvrDiag::kGpuWait)]),
              Ms(us[at(SvrDiag::kAcquire)]), n[at(SvrDiag::kDraw)], Ms(us[at(SvrDiag::kDraw)]),
              n[at(SvrDiag::kTextureCheck)], double(us[at(SvrDiag::kHashBytes)]) / (1024.0 * 1024.0),
              n[at(SvrDiag::kGuestGpuWait)], Ms(us[at(SvrDiag::kGuestGpuWait)]));
    }
  }
  if (now - window_start >= std::chrono::seconds(10)) {
    const double f = window_frames ? double(window_frames) : 1.0;
    BD_INFO("[frames] last 10 s: {} frames, slowest {:.1f} ms, {} over {} ms | per frame: "
            "{:.0f} draws in {:.1f} ms, texture checks {:.1f} ms hashing {:.1f} MB, waiting for "
            "emulated GPU {:.2f} ms",
            window_frames, window_max, window_slow, threshold, window_draws / f,
            Ms(window_draw_us) / f, Ms(window_check_us) / f,
            double(window_hash) / f / (1024.0 * 1024.0), Ms(window_gpu_wait_us) / f);
    window_draws = window_draw_us = window_check_us = window_hash = window_gpu_wait_us = 0;
    window_start = now;
    window_max = 0.0;
    window_slow = 0;
    window_frames = 0;
  }
}

}  // namespace bd::gpu
