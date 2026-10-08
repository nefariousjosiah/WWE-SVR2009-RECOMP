// Loading speed (SvR 2009).
//
// The engine's graphics object (created by sub_82477B28) guards the D3D device with a lock that
// the loader thread holds while loading. The loader calls sub_82477A18 often: once the time since
// the last presented frame exceeds the budget at +196 (milliseconds: the elapsed timebase ticks are
// divided by ticks per ms), it hands the device and the lock to the main thread for one
// loading-screen frame and waits for it. The budget is 15 ms normally, but match loads set 1 ms
// (sub_820EBD58 and others; one path 0 ms), so the loader worked ~1 ms per 16.7 ms frame. On the
// Xbox 360 the DVD was the bottleneck anyway; here the data is there at once and match loads took
// ~50 s, mostly waiting for frames.
//
// The hook raises the budget the loader is compared against (never lowers it): the loading screen
// still gets a frame every 1/60 s, and the loader gets most of each frame.

#include <rex/cvar.h>
#include <rex/ppc.h>

#include <algorithm>

REXCVAR_DEFINE_INT32(svr_load_slice_ms, 12, "Game",
                     "Milliseconds the loader may work per loading-screen frame (the game uses 1 for "
                     "match loads; 0 keeps the game's value)")
    .range(0, 15);

// sub_82477A18 at 0x82477A58 (fcmpu cr6,f12,f13): f12 = ms since the last frame, f13 = budget.
void SvrLoaderBudgetHook(PPCRegister &f13) {
  const int slice = REXCVAR_GET(svr_load_slice_ms);
  if (slice > 0)
    f13.f64 = std::max(f13.f64, double(slice));
}
