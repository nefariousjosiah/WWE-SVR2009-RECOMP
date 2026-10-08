// 1 ms timer resolution for the whole run.
//
// The game's sleeps (KeDelayExecutionThread, waits with timeouts) become Windows Sleep / timed
// waits, which by default round up to the 15.6 ms system tick, against the Xbox 360's 1 ms.
// Loaders that poll with short sleeps then wait up to 15x longer than on the console.

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>

namespace {

struct TimerResolution {
  TimerResolution() {
    timeBeginPeriod(1);
#if defined(PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION)
    // Windows 11 drops the request while the window is hidden or minimised unless told not to.
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    state.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
#endif
  }
  ~TimerResolution() { timeEndPeriod(1); }
} g_timer_resolution;

}  // namespace
#endif
