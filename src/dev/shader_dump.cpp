// Developer tool: save every Xenos shader the game creates, for building the native renderer's
// shader cache with XenosRecomp (which expects files containing shader containers).
//
//   SVR_DUMP_SHADERS=logs/shaders   then play; each new shader is written once as <hash>.vs/.ps
//
// Hooks D3DDevice_CreateVertexShader / CreatePixelShader (pFunction in r3 points at the container:
// flags, virtual size, physical size, ...; the shader is virtual + physical bytes long) and then
// runs the original. Built only with -DSVR_D3D_CENSUS=ON (developer builds).

#include "generated/default/svr2009_init.h"

#include <rex/hook.h>
#include <rex/logging.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>

namespace {

std::mutex g_mutex;
std::unordered_set<uint64_t> g_seen;

uint64_t Fnv1a(const uint8_t* data, size_t size) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < size; ++i) h = (h ^ data[i]) * 0x100000001B3ull;
  return h;
}

uint32_t LoadBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void Dump(uint32_t guest_container, uint8_t* base, const char* ext) {
  static const char* dir = std::getenv("SVR_DUMP_SHADERS");
  if (!dir || !*dir || !guest_container) return;
  const uint8_t* container = base + guest_container;
  uint32_t flags = LoadBe32(container);
  uint32_t size = LoadBe32(container + 4) + LoadBe32(container + 8);
  if ((flags & 0xFFFFFF00) != 0x102A1100 || size == 0 || size > 0x200000) {
    REXLOG_WARN("[shaders] unexpected container at {:08X}: flags {:08X} size {}", guest_container,
                flags, size);
    return;
  }
  uint64_t hash = Fnv1a(container, size);
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_seen.insert(hash).second) return;
  std::filesystem::create_directories(dir);
  char name[64];
  std::snprintf(name, sizeof(name), "%016llX.%s", (unsigned long long)hash, ext);
  std::string path = std::string(dir) + "/" + name;
  if (FILE* f = std::fopen(path.c_str(), "wb")) {
    std::fwrite(container, 1, size, f);
    std::fclose(f);
    REXLOG_INFO("[shaders] {} #{} {} ({} bytes)", ext, g_seen.size(), name, size);
  }
}

}  // namespace

REX_HOOK_RAW(sub_82261170) {  // D3DDevice_CreateVertexShader(pFunction)
  Dump(ctx.r3.u32, base, "vs");
  __imp__sub_82261170(ctx, base);
}

REX_HOOK_RAW(sub_82261060) {  // D3DDevice_CreatePixelShader(pFunction)
  Dump(ctx.r3.u32, base, "ps");
  __imp__sub_82261060(ctx, base);
}
