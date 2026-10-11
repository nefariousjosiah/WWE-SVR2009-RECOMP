// Pipelines compiled ahead of use, so they don't stall a frame.
//
// Without this every pipeline the game needs is compiled on the render thread the first time it
// is drawn: up to ~60 in a second when a new scene starts, which shows as frame drops (heavier on
// NVIDIA's driver, whose compiles are slower). Each pipeline compiled that way is appended to
// pipelines.bin next to the executable, keyed by content hashes of its shaders and vertex
// declaration, which stay the same between runs. On the next start the stored pipelines go into
// pso_recorder's replay list, which resolves each one as soon as its shaders and declaration
// exist (usually while the scene loads) and hands it to the background compiler here. Draws are
// never skipped: a pipeline the background compiler hasn't finished yet still compiles on the
// render thread as before, so nothing can go missing (SvR renders some textures only once).

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/filesystem.h>

#include "core/logging.h"
#include "gpu/pipeline/pipeline_cache.h"
#include "gpu/pipeline/pso_precache.h"

REXCVAR_DEFINE_BOOL(svr_pipeline_precache, true, "SvR",
                    "Compile pipelines the game used before in the background as their shaders "
                    "load (pipelines.bin), instead of during a frame");

namespace bd::gpu {

namespace {

constexpr u32 kMagic = 0x50525653;  // "SVRP"
constexpr u32 kVersion = 1;

struct FileHeader {
  u32 magic = kMagic;
  u32 version = kVersion;
  u32 record_size = sizeof(PipelineState);
  u32 reserved = 0;
};

std::filesystem::path StorePath() {
  return rex::filesystem::GetExecutableFolder() / "pipelines.bin";
}

// The stored form: the pipeline with content hashes in its shader and declaration slots, and the
// device-chosen formats in their neutral form (SanitizePipelineState maps them back on load):
// depth-stencil D32S8 (D3D12 picks D24S8 on NVIDIA / Intel) and the scene's RGBA16F, so a store
// recorded on one PC or graphics API serves every other.
PipelineState Stored(const PipelineState &state, u64 vs, u64 ps, u64 decl) {
  PipelineState s = state;
  if (plume::RenderFormatIsStencil(s.depthStencilFormat))
    s.depthStencilFormat = plume::RenderFormat::D32_FLOAT_S8_UINT;
  if (s.renderTargetFormat == plume::RenderFormat::R11G11B10_FLOAT)
    s.renderTargetFormat = plume::RenderFormat::R16G16B16A16_FLOAT;
  s.vertexShader = reinterpret_cast<GuestShader *>(vs);
  s.pixelShader = reinterpret_cast<GuestShader *>(ps);
  s.vertexDeclaration = reinterpret_cast<GuestVertexDeclaration *>(decl);
  return s;
}

std::mutex g_store_mutex;
std::unordered_set<u64> g_stored;  // keys of stored records (hash of the stored form)

// --- background compiler ----------------------------------------------------------------------
std::mutex g_queue_mutex;
std::condition_variable g_queue_cv;
std::deque<PipelineState> g_queue;
std::unordered_set<u64> g_queued;  // live keys ever queued, so each pipeline is compiled once
std::once_flag g_workers_once;
std::atomic<u32> g_compiled{0};

void Worker() {
#if defined(_WIN32)
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  SetThreadDescription(GetCurrentThread(), L"SvR pipeline precompile");
#endif
  for (;;) {
    PipelineState state;
    {
      std::unique_lock lock(g_queue_mutex);
      g_queue_cv.wait(lock, [] { return !g_queue.empty(); });
      state = g_queue.front();
      g_queue.pop_front();
    }
    bool built = false;
    GetOrCreatePipeline(state, &built);
    if (built) {
      const u32 n = g_compiled.fetch_add(1, std::memory_order_relaxed) + 1;
      if (n == 1 || n % 100 == 0)
        BD_INFO("pso: {} pipelines precompiled in the background", n);
    }
  }
}

void StartWorkers() {
  std::call_once(g_workers_once, [] {
    const unsigned hw = std::max(2u, std::thread::hardware_concurrency());
    const unsigned count = std::clamp(hw / 4, 1u, 3u);
    for (unsigned i = 0; i < count; ++i)
      std::thread(Worker).detach();  // process exit is a hard kill, never joined
    BD_INFO("pso: {} background pipeline compile thread(s)", count);
  });
}

void Enqueue(const PipelineState &state, bool front) {
  if (!REXCVAR_GET(svr_pipeline_precache))
    return;
  StartWorkers();
  {
    std::lock_guard lock(g_queue_mutex);
    if (!g_queued.insert(HashPipelineState(state)).second)
      return;
    if (front)
      g_queue.push_front(state);
    else
      g_queue.push_back(state);
  }
  g_queue_cv.notify_one();
}

}  // namespace

bool PrecacheEnabled() { return REXCVAR_GET(svr_pipeline_precache); }
void EnqueuePipeline(const PipelineState &state) { Enqueue(state, false); }
void EnqueuePipelinePriority(const PipelineState &state) { Enqueue(state, true); }

// Stored pipelines for pso_recorder's replay list (ReplayBootCache), hashes in the pointer slots.
std::vector<PipelineState> SvrLoadPipelineStore() {
  std::vector<PipelineState> out;
  std::lock_guard lock(g_store_mutex);
  FILE *f = rex::filesystem::OpenFile(StorePath(), "rb");
  if (!f)
    return out;
  FileHeader header;
  if (std::fread(&header, sizeof(header), 1, f) == 1 && header.magic == kMagic &&
      header.version == kVersion && header.record_size == sizeof(PipelineState)) {
    PipelineState s;
    while (std::fread(&s, sizeof(s), 1, f) == 1) {
      if (g_stored.insert(HashPipelineState(s)).second)
        out.push_back(s);
    }
    std::fclose(f);
  } else {
    std::fclose(f);
    std::error_code ec;
    std::filesystem::remove(StorePath(), ec);
    BD_INFO("pso: pipelines.bin is from another version; starting a new one");
  }
  BD_INFO("pso: {} stored pipelines; each compiles in the background once its shaders load",
          out.size());
  return out;
}

// A pipeline just compiled on the render thread: store it for the next start.
void SvrRecordPipeline(const PipelineState &state, u64 vs, u64 ps, u64 decl) {
  if (!vs || !REXCVAR_GET(svr_pipeline_precache))
    return;
  const PipelineState s = Stored(state, vs, ps, decl);
  std::lock_guard lock(g_store_mutex);
  if (!g_stored.insert(HashPipelineState(s)).second)
    return;
  const auto path = StorePath();
  std::error_code ec;
  const bool fresh = !std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) == 0;
  FILE *f = rex::filesystem::OpenFile(path, fresh ? "wb" : "ab");
  if (!f)
    return;
  if (fresh) {
    const FileHeader header;
    std::fwrite(&header, sizeof(header), 1, f);
  }
  std::fwrite(&s, sizeof(s), 1, f);
  std::fclose(f);
}

}  // namespace bd::gpu
