// Frame hitch diagnostics (svr_frame_diag.cpp): time spent per frame on the work that can stall
// a frame, logged for frames slower than svr_frame_diag_ms.
#pragma once

#include <chrono>
#include <cstdint>
#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

namespace bd::gpu {

enum class SvrDiag : int {
  kPipelineCompile,  // pipelines compiled on the render thread
  kTextureMirror,    // host textures built from the game's textures
  kTextureCheck,     // per-frame change checks of bound textures (includes kTextureMirror)
  kTargetCreate,     // host render targets / surfaces created
  kGpuWait,          // waiting for the GPU to finish an earlier frame
  kAcquire,          // waiting for the swap chain image
  kDraw,             // whole draw calls on the render thread (includes kTextureCheck/kPipelineCompile)
  kHashBytes,        // bytes hashed by the texture checks (the "us" field carries bytes)
  kGuestGpuWait,     // the game's D3D waiting for the emulated GPU's fences (svr_gpu_wait_diag)
  kCount
};

void SvrDiagAdd(SvrDiag what, uint64_t microseconds);
void SvrDiagEndFrame();  // once per presented frame (SvrOnFrame)

// The timers run around every draw and texture check, so they read the CPU's timestamp counter
// (a few ns) rather than the OS clock; SvrDiagEndFrame converts the ticks to time.
inline uint64_t SvrDiagTicks() {
#if defined(_M_X64) || defined(__x86_64__)
  return __rdtsc();
#else
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
#endif
}
void SvrDiagAddTicks(SvrDiag what, uint64_t ticks);

class SvrDiagTimer {
 public:
  explicit SvrDiagTimer(SvrDiag what) : what_(what), start_(SvrDiagTicks()) {}
  ~SvrDiagTimer() { SvrDiagAddTicks(what_, SvrDiagTicks() - start_); }
  SvrDiagTimer(const SvrDiagTimer&) = delete;
  SvrDiagTimer& operator=(const SvrDiagTimer&) = delete;

 private:
  SvrDiag what_;
  uint64_t start_;
};

}  // namespace bd::gpu
