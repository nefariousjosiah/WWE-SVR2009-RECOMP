// Developer tool: write the game's loaded image (decompressed and decrypted default.xex, as mapped
// at 0x82000000) to a file once at startup, for static analysis of code and data tables.
//
//   SVR_DUMP_IMAGE=logs/default_image.bin   then launch the game
//
// Off unless the environment variable is set.

#pragma once

#include <rex/image_info.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

#include <cstdio>
#include <cstdlib>

inline void DumpGuestImageIfRequested(rex::Runtime* runtime, const rex::PPCImageInfo& image) {
  const char* path = std::getenv("SVR_DUMP_IMAGE");
  if (!path || !*path || !runtime || !runtime->memory())
    return;
  const auto* data = runtime->memory()->TranslateVirtual<const uint8_t*>(uint32_t(image.image_base));
  FILE* file = std::fopen(path, "wb");
  if (!file) {
    REXLOG_WARN("SVR_DUMP_IMAGE: can't open {}", path);
    return;
  }
  size_t written = std::fwrite(data, 1, size_t(image.image_size), file);
  std::fclose(file);
  REXLOG_INFO("SVR_DUMP_IMAGE: wrote {} bytes from 0x{:08X} to {}", written,
              uint32_t(image.image_base), path);
}
