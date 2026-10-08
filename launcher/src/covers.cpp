#include "covers.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include "stb_image.h"

namespace launcher {

namespace fs = std::filesystem;

namespace {

constexpr const char *kExtensions[] = {".png", ".jpg", ".jpeg", ".bmp"};

std::string LowerExt(const fs::path &p) {
  std::string e = p.extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return e;
}

SDL_Texture *MakeTexture(SDL_Renderer *renderer, const unsigned char *rgba, int w, int h) {
  SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
  if (!t)
    return nullptr;
  SDL_UpdateTexture(t, nullptr, rgba, w * 4);
  SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  return t;
}

}  // namespace

bool IsImageFile(const fs::path &path) {
  const std::string e = LowerExt(path);
  return std::find(std::begin(kExtensions), std::end(kExtensions), e) != std::end(kExtensions);
}

bool LoadCover(SDL_Renderer *renderer, const fs::path &dir, Cover &cover) {
  FreeCover(cover);
  std::error_code ec;
  for (const char *ext : kExtensions) {
    const fs::path file = dir / (std::string("cover") + ext);
    if (!fs::is_regular_file(file, ec))
      continue;
    std::ifstream f(file, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), {});
    int w = 0, h = 0, n = 0;
    unsigned char *rgba = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &n, 4);
    if (!rgba)
      continue;
    cover.image = MakeTexture(renderer, rgba, w, h);
    // Box-filter down to a few dozen pixels for the background.
    const int bw = 24, bh = std::max(1, 24 * h / std::max(1, w));
    std::vector<unsigned char> small(size_t(bw) * bh * 4);
    for (int y = 0; y < bh; ++y)
      for (int x = 0; x < bw; ++x) {
        const int x0 = x * w / bw, x1 = std::max(x0 + 1, (x + 1) * w / bw);
        const int y0 = y * h / bh, y1 = std::max(y0 + 1, (y + 1) * h / bh);
        unsigned sum[4] = {};
        for (int yy = y0; yy < y1; ++yy)
          for (int xx = x0; xx < x1; ++xx)
            for (int c = 0; c < 4; ++c)
              sum[c] += rgba[(size_t(yy) * w + xx) * 4 + c];
        const unsigned count = unsigned((x1 - x0) * (y1 - y0));
        for (int c = 0; c < 4; ++c)
          small[(size_t(y) * bw + x) * 4 + c] = static_cast<unsigned char>(sum[c] / count);
      }
    cover.blurred = MakeTexture(renderer, small.data(), bw, bh);
    stbi_image_free(rgba);
    cover.width = w;
    cover.height = h;
    return cover.image != nullptr;
  }
  return false;
}

void FreeCover(Cover &cover) {
  if (cover.image)
    SDL_DestroyTexture(cover.image);
  if (cover.blurred)
    SDL_DestroyTexture(cover.blurred);
  cover = {};
}

void RemoveCover(const fs::path &dir) {
  std::error_code ec;
  for (const char *ext : kExtensions)
    fs::remove(dir / (std::string("cover") + ext), ec);
}

bool SetCover(const fs::path &dir, const fs::path &image, std::string &error) {
  if (!IsImageFile(image)) {
    error = "box art can be a PNG, JPEG or BMP image";
    return false;
  }
  std::error_code ec;
  const fs::path target = dir / (std::string("cover") + LowerExt(image));
  if (fs::equivalent(image, target, ec))
    return true;
  RemoveCover(dir);
  if (!fs::copy_file(image, target, fs::copy_options::overwrite_existing, ec)) {
    error = "couldn't copy the image (" + ec.message() + ")";
    return false;
  }
  return true;
}

}  // namespace launcher
