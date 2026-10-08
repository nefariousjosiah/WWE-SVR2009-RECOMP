// Box art for the launcher. None ships with the game: the player's own image, saved as
// cover.png / cover.jpg next to the launcher (dropped onto the window or chosen in Settings).
#pragma once

#include <filesystem>
#include <string>

struct SDL_Renderer;
struct SDL_Texture;

namespace launcher {

struct Cover {
  SDL_Texture *image = nullptr;    // full size
  SDL_Texture *blurred = nullptr;  // tiny copy; drawn stretched it reads as a strong blur
  int width = 0, height = 0;
};

bool LoadCover(SDL_Renderer *renderer, const std::filesystem::path &dir, Cover &cover);
void FreeCover(Cover &cover);
bool IsImageFile(const std::filesystem::path &path);
// Copies an image to <dir>\cover<ext>, replacing any previous one.
bool SetCover(const std::filesystem::path &dir, const std::filesystem::path &image, std::string &error);
void RemoveCover(const std::filesystem::path &dir);

}  // namespace launcher
