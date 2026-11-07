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
//
// A stored pipeline is ready long before it is drawn; what still stalls a frame is a state
// combination no earlier run drew, usually a stored shader pair with another cull mode, depth
// write, blend or vertex format when a new wrestler or arena appears. So the store's axes are also
// crossed into unseen variants, compiled after the stored pipelines (see SvrSpeculatePipelines).

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
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
REXCVAR_DEFINE_INT32(svr_pipeline_speculate, 4000, "SvR",
                     "Most unseen variants of the stored pipelines to compile in the background "
                     "as well (0 = off)");

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
      std::lock_guard lock(g_queue_mutex);
      if (g_queue.empty())
        BD_INFO("pso: background queue empty after {} pipelines", n);
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

namespace {

// --- speculative variants ---------------------------------------------------------------------
// The state a draw can combine freely, in the groups that change together. spec constants belong
// to the vertex format (hooks/state.cpp derives them from the declaration's normal format).
using Axis = void (*)(const PipelineState &from, PipelineState &to);

void RasterAxis(const PipelineState &from, PipelineState &to) {
  to.cullMode = from.cullMode;
  to.frontFace = from.frontFace;
}

void DepthAxis(const PipelineState &from, PipelineState &to) {
  to.zEnable = from.zEnable;
  to.zWriteEnable = from.zWriteEnable;
  to.zFunc = from.zFunc;
}

void TopologyAxis(const PipelineState &from, PipelineState &to) {
  to.primitiveTopology = from.primitiveTopology;
}

void InputAxis(const PipelineState &from, PipelineState &to) {
  to.vertexDeclaration = from.vertexDeclaration;
  to.instancing = from.instancing;
  std::memcpy(to.vertexStrides, from.vertexStrides, sizeof(to.vertexStrides));
  to.specConstants = from.specConstants;
}

void BlendAxis(const PipelineState &from, PipelineState &to) {
  to.alphaBlendEnable = from.alphaBlendEnable;
  to.srcBlend = from.srcBlend;
  to.destBlend = from.destBlend;
  to.blendOp = from.blendOp;
  to.srcBlendAlpha = from.srcBlendAlpha;
  to.destBlendAlpha = from.destBlendAlpha;
  to.blendOpAlpha = from.blendOpAlpha;
  to.colorWriteEnable = from.colorWriteEnable;
  to.renderTargetFormat = from.renderTargetFormat;
}

void StencilAxis(const PipelineState &from, PipelineState &to) {
  to.stencilEnable = from.stencilEnable;
  to.stencilTwoSided = from.stencilTwoSided;
  to.stencilFunc = from.stencilFunc;
  to.stencilFail = from.stencilFail;
  to.stencilZFail = from.stencilZFail;
  to.stencilPass = from.stencilPass;
  to.stencilFuncCCW = from.stencilFuncCCW;
  to.stencilFailCCW = from.stencilFailCCW;
  to.stencilZFailCCW = from.stencilZFailCCW;
  to.stencilPassCCW = from.stencilPassCCW;
  to.stencilMask = from.stencilMask;
  to.stencilWriteMask = from.stencilWriteMask;
  to.stencilRef = from.stencilRef;
}

// The distinct values one axis took, per scope (a shader, a shader pair, or everything).
class AxisValues {
 public:
  explicit AxisValues(Axis axis) : axis_(axis) {}

  void Add(u64 scope, const PipelineState &record) {
    PipelineState value;
    axis_(record, value);
    if (seen_.insert(HashPipelineState(value) ^ (scope * 0x9E3779B97F4A7C15ull)).second)
      values_[scope].push_back(value);
  }

  const std::vector<PipelineState> &In(u64 scope) const { return values_.at(scope); }
  void Apply(const PipelineState &value, PipelineState &to) const { axis_(value, to); }

 private:
  Axis axis_;
  std::unordered_set<u64> seen_;
  std::unordered_map<u64, std::vector<PipelineState>> values_;
};

u64 HashOf(const void *slot) { return static_cast<u64>(reinterpret_cast<uintptr_t>(slot)); }
u64 PairScope(const PipelineState &s) {
  return HashOf(s.vertexShader) ^ (HashOf(s.pixelShader) * 0x9E3779B97F4A7C15ull);
}

}  // namespace

// Unseen variants of the stored pipelines, in stored form. Replaying the store shows which variants
// a new scene asks for: per shader pair its own blend and stencil, per vertex shader the depth,
// topology and vertex formats it was drawn with under any pixel shader, and the cull modes of every
// pipeline. These are compiled but never written to the store, so they can't feed on themselves.
std::vector<PipelineState> SvrSpeculatePipelines(const std::vector<PipelineState> &stored) {
  std::vector<PipelineState> out;
  const size_t cap = size_t(std::max(REXCVAR_GET(svr_pipeline_speculate), 0));
  if (!cap)
    return out;
  AxisValues raster(RasterAxis), depth(DepthAxis), topology(TopologyAxis), input(InputAxis),
      blend(BlendAxis), stencil(StencilAxis);
  std::vector<PipelineState> pairs;
  std::unordered_set<u64> pairs_seen;
  for (const PipelineState &s : stored) {
    const u64 vs = HashOf(s.vertexShader), pair = PairScope(s);
    raster.Add(0, s);
    depth.Add(vs, s);
    topology.Add(vs, s);
    input.Add(vs, s);
    blend.Add(pair, s);
    stencil.Add(pair, s);
    if (pairs_seen.insert(pair).second)
      pairs.push_back(s);
  }
  std::lock_guard lock(g_store_mutex);
  std::unordered_set<u64> seen;
  bool capped = false;
  for (const PipelineState &pair : pairs) {
    const u64 vs = HashOf(pair.vertexShader), scope = PairScope(pair);
    PipelineState v = pair;
    for (const auto &a : raster.In(0)) {
      raster.Apply(a, v);
      for (const auto &b : depth.In(vs)) {
        depth.Apply(b, v);
        for (const auto &c : topology.In(vs)) {
          topology.Apply(c, v);
          for (const auto &d : input.In(vs)) {
            input.Apply(d, v);
            for (const auto &e : blend.In(scope)) {
              blend.Apply(e, v);
              for (const auto &f : stencil.In(scope)) {
                stencil.Apply(f, v);
                const u64 key = HashPipelineState(v);
                if (g_stored.count(key) || !seen.insert(key).second)
                  continue;
                if (out.size() == cap) {
                  capped = true;
                  continue;
                }
                out.push_back(v);
              }
            }
          }
        }
      }
    }
  }
  BD_INFO("pso: {} speculative pipeline variants from {} shader pairs{}", out.size(), pairs.size(),
          capped ? " (capped by svr_pipeline_speculate)" : "");
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
