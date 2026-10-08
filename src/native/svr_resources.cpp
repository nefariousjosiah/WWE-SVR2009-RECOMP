// SvR 2008 host resources for the game's own D3D textures and surfaces (see svr_resources.h).

#include "svr_resources.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include <xxhash.h>

#include <rex/cvar.h>
#include <rex/graphics/xenos.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "gpu/d3d.h"
#include "gpu/host_resource_heap.h"
#include "gpu/native_texture_mirror.h"
#include "gpu/output.h"
#include "gpu/resources.h"
#include "gpu/vertex_declaration.h"

// src/native/crash_trace.cpp: hang watchdog, fed once per presented frame.
void SvrWatchdogHeartbeat();
// src/native/display_size.cpp.
bool SvrDisplaySize(uint32_t &w, uint32_t &h);
bool SvrOnSteamDeck();

REXCVAR_DEFINE_INT32(svr_render_scale, 0, "SvR",
                     "Internal resolution: 1 = 720p (original), 2 = 1440p, 3 = 4K (2160p), "
                     "4 = 2880p. 0 = auto: from the display (1440p on 1080p/1440p screens, 4K on "
                     "4K screens), 720p on Steam Deck");

namespace bd::gpu {

// Host-side creation from re:Blue's CreateTexture/CreateSurface hooks (gpu/hooks/resource.cpp).
GuestTexture *SvrCreateHostTexture(u32 width, u32 height, u32 depth, u32 levels, u32 usage,
                                   u32 format, u32 d3d_type);
GuestTexture *SvrCreateHostSurface(u32 width, u32 height, u32 format);

namespace {

std::mutex g_mutex;
std::unordered_map<u32, GuestTexture *> g_surfaces;  // D3DSurface VA -> host render target
std::unordered_map<u32, GuestTexture *> g_targets;   // texture VA -> GPU-written host texture

// The texture's GPUTEXTURE_FETCH_CONSTANT (D3DTexture+0x1C), as host-order dwords.
bool ReadFetch(u32 texture_va, rex::graphics::xenos::xe_gpu_texture_fetch_t &fetch) {
  const auto *texture = bd::mem::at<const D3DTexture>(texture_va);
  if (!texture)
    return false;
  for (int i = 0; i < 6; ++i)
    (&fetch.dword_0)[i] = u32(texture->Format.dword[i]);
  return fetch.type == rex::graphics::xenos::FetchConstantType::kTexture;
}

}  // namespace

u32 SvrRenderScale() {
  static std::atomic<u32> s_scale{0};
  if (const u32 scale = s_scale.load(std::memory_order_relaxed))
    return scale;
  u32 scale = 1;
  if (const int configured = REXCVAR_GET(svr_render_scale); configured > 0) {
    scale = u32(std::min(configured, 4));
  } else if (!SvrOnSteamDeck()) {
    // The larger of the window and the display, so a window on a big screen still renders
    // sharp (downscaled at present); 720p multiples rounded to nearest: 1080/1440 -> 2, 2160 -> 3.
    // The Deck keeps 1x even when docked: its GPU holds 60 fps there.
    u32 w = 0, h = 0, dw = 0, dh = 0;
    if (!Output::RenderSize(w, h))
      h = 0;
    if (SvrDisplaySize(dw, dh))
      h = std::max(h, dh);
    if (h)
      scale = std::clamp(u32((h + 360u) / 720u), 1u, 4u);
  }
  u32 expected = 0;
  if (s_scale.compare_exchange_strong(expected, scale)) {
    BD_INFO("SvR native: internal resolution {}x{} ({}x)", 1280 * scale, 720 * scale, scale);
    return scale;
  }
  return expected;
}

GuestTexture *SvrResolveSurface(u32 surface_va) {
  if (!surface_va)
    return nullptr;
  if (auto *host = HostResourceHeap::FromGuest<GuestTexture>(surface_va))
    return host;
  std::lock_guard lock(g_mutex);
  if (auto it = g_surfaces.find(surface_va); it != g_surfaces.end())
    return it->second;
  const auto *surface = bd::mem::at<const D3DSurface>(surface_va);
  if (!surface)
    return nullptr;
  // Size packing as decoded by D3DSurface_GetDesc (see gpu/d3d.h).
  const u32 bits = surface->SizeBits;
  const u32 width = (bits >> 18) + 1;
  const u32 height = ((bits >> 3) & 0x7FFF) + 1;
  const u32 format = surface->Format;
  const u32 scale = SvrRenderScale();
  GuestTexture *host = SvrCreateHostSurface(width * scale, height * scale, format);
  BD_INFO("SvR surface 0x{:08X}: {}x{} format 0x{:08X} -> host {}", surface_va, width, height,
          format, host ? "ok" : "FAILED");
  g_surfaces.emplace(surface_va, host);
  return host;
}

GuestTexture *SvrResolveTarget(u32 texture_va) {
  if (!texture_va)
    return nullptr;
  if (auto *host = HostResourceHeap::FromGuest<GuestTexture>(texture_va))
    return host;
  std::lock_guard lock(g_mutex);
  if (auto it = g_targets.find(texture_va); it != g_targets.end())
    return it->second;
  rex::graphics::xenos::xe_gpu_texture_fetch_t fetch{};
  if (!ReadFetch(texture_va, fetch)) {
    g_targets.emplace(texture_va, nullptr);
    return nullptr;
  }
  const u32 width = fetch.size_2d.width + 1;
  const u32 height = fetch.size_2d.height + 1;
  const u32 levels = fetch.mip_max_level + 1;
  // ConvertGuestFormat maps a raw GPUTEXTUREFORMAT byte by its low 6 bits; the tag keeps a small
  // byte from matching one of re:Blue's D3DFormat values (k_8_8_8_8 = 6 = its kIndex32).
  const u32 scale = SvrRenderScale();
  GuestTexture *host = SvrCreateHostTexture(width * scale, height * scale, 1, levels, 0,
                                            0xC0000000u | u32(fetch.format), 0);
  BD_INFO("SvR target 0x{:08X}: {}x{} fmt {} levels {} -> host {}", texture_va, width, height,
          u32(fetch.format), levels, host ? "ok" : "FAILED");
  g_targets.emplace(texture_va, host);
  return host;
}

namespace {

std::shared_mutex g_object_mutex;
std::unordered_map<u32, GuestShader *> g_shaders;  // guest shader object -> host
std::unordered_map<u64, GuestShader *> g_shaders_by_hash;
std::unordered_map<u32, GuestVertexDeclaration *> g_decls;  // guest declaration -> host
std::unordered_map<u64, GuestVertexDeclaration *> g_decls_by_hash;

}  // namespace

void SvrOnShaderCreated(u32 shader_va, u32 function_va, bool pixel) {
  if (!shader_va || !function_va)
    return;
  const auto *container = bd::mem::at<const ShaderContainer>(function_va);
  if (!container)
    return;
  // Keyed like CreateShader's cache lookup (the whole container), plus the stage.
  const u32 length = u32(container->virtualSize) + u32(container->physicalSize);
  const u64 hash = XXH3_64bits(container, length) ^ (pixel ? 0x9E3779B97F4A7C15ull : 0);
  std::unique_lock lock(g_object_mutex);
  GuestShader *&host = g_shaders_by_hash[hash];
  if (!host)
    host = CreateShader(reinterpret_cast<const be_u32 *>(container),
                        pixel ? ResourceType::PixelShader : ResourceType::VertexShader);
  g_shaders[shader_va] = host;
}

GuestShader *SvrLookupShader(u32 shader_va) {
  if (!shader_va)
    return nullptr;
  std::shared_lock lock(g_object_mutex);
  auto it = g_shaders.find(shader_va);
  return it != g_shaders.end() ? it->second : nullptr;
}

void SvrOnVertexDeclarationCreated(u32 decl_va, u32 elements_va) {
  if (!decl_va || !elements_va)
    return;
  auto *elements = bd::mem::at<GuestVertexElement>(elements_va);
  if (!elements)
    return;
  // Elements up to the D3DDECL_END terminator (stream 0xFF), padding zeroed for a stable key.
  std::vector<GuestVertexElement> normalized;
  for (const auto *e = elements; normalized.size() < 64; ++e) {
    normalized.push_back(*e);
    normalized.back().padding = 0;
    if (u16(e->stream) == 0xFF)
      break;
  }
  const u64 hash =
      XXH3_64bits(normalized.data(), normalized.size() * sizeof(GuestVertexElement));
  std::unique_lock lock(g_object_mutex);
  GuestVertexDeclaration *&host = g_decls_by_hash[hash];
  if (!host)
    host = CreateVertexDeclaration(normalized.data());
  g_decls[decl_va] = host;
}

GuestVertexDeclaration *SvrLookupVertexDeclaration(u32 decl_va) {
  if (!decl_va)
    return nullptr;
  std::shared_lock lock(g_object_mutex);
  auto it = g_decls.find(decl_va);
  return it != g_decls.end() ? it->second : nullptr;
}

namespace {

// A mirrored texture as last checked: the fetch constant and content it was built from.
struct MirrorCheck {
  u32 fetch[6] = {};
  u64 hash = 0;
  u64 frame = 0;
  GuestTexture *host = nullptr;
};
std::unordered_map<u32, MirrorCheck> g_mirror_checks;  // D3D texture VA -> last check
std::atomic<u64> g_frame{1};

u64 HashSource(const NativeMirrorSource &src) {
  u64 hash = XXH3_64bits(src.fetch, sizeof(src.fetch));
  if (src.base_size)
    hash = XXH3_64bits_withSeed(src.base, src.base_size, hash);
  if (src.mips_size)
    hash = XXH3_64bits_withSeed(src.mips, src.mips_size, hash);
  return hash;
}

}  // namespace

void SvrOnFrame() {
  SvrWatchdogHeartbeat();
  const u64 frame = g_frame.fetch_add(1, std::memory_order_relaxed);
  // Present rate in the log every 600 frames (pacing check).
  static auto last = std::chrono::steady_clock::now();
  if (frame % 600 == 0) {
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - last).count();
    last = now;
    BD_INFO("SvR native: {:.1f} presents/s", 600.0 / seconds);
  }
}

u64 SvrFrameIndex() { return g_frame.load(std::memory_order_relaxed); }

// The game writes texture memory with the CPU whenever it likes (every Bink movie frame, menus
// reloading into reused memory), so a mirror is checked against its source once per frame it's
// used and rebuilt when the fetch constant or the bytes changed. A texture that can't be mirrored
// is remembered the same way, so it isn't retried (or logged) until it changes.
GuestTexture *SvrResolveTexture(u32 texture_va) {
  if (!texture_va)
    return nullptr;
  const u64 frame = g_frame.load(std::memory_order_relaxed);
  std::lock_guard lock(g_mutex);
  if (auto it = g_targets.find(texture_va); it != g_targets.end() && it->second)
    return it->second;
  MirrorCheck &check = g_mirror_checks[texture_va];
  if (check.frame == frame)
    return check.host;
  NativeMirrorSource src;
  if (!GetNativeMirrorSource(texture_va, src)) {
    check.frame = frame;
    return check.host = nullptr;
  }
  const u64 hash = HashSource(src);
  if (check.frame != 0 && check.hash == hash) {
    check.frame = frame;
    return check.host;
  }
  check.host = GetOrCreateNativeMirror(texture_va, 0);
  check.hash = hash;
  check.frame = frame;
  return check.host;
}

}  // namespace bd::gpu
