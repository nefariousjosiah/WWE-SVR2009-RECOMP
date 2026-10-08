// Game frame rate, measured where the game presents each frame (see fps_hooks.cpp).

#pragma once

#include <cstdint>

struct SvrFrameStats {
  double fps = 0;            // frames presented in the last second
  double frame_time_ms = 0;  // average time between those frames
  uint64_t frames = 0;       // frames presented since boot
};

SvrFrameStats GetSvrFrameStats();
