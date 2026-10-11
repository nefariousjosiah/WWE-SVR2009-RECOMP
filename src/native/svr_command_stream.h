// Threaded command recording. The game's render thread spends a large part of each frame in the
// graphics driver turning draws into Vulkan commands, and SvR runs its game logic on that same
// thread. With svr_threaded_recording on, the frame's command list is a SvrCommandStream: each
// call is copied into a stream (a few bytes, a few ns) and a worker thread replays the stream
// into the real command list while the game carries on. end() waits for the worker to catch up
// and ends the real list, which is then submitted as before.
#pragma once

#include <plume_render_interface.h>

#include <rex/types.h>

namespace bd::gpu {

// The list to record into for frame slot `slot`: its SvrCommandStream when threaded recording is
// on, otherwise `real` itself. The stream for a slot is created once and always targets `real`.
plume::RenderCommandList *SvrRecordingList(u32 slot, plume::RenderCommandList *real);

// Waits until the worker has replayed everything recorded so far (nothing to do when off).
void SvrCommandStreamDrain();

}  // namespace bd::gpu
