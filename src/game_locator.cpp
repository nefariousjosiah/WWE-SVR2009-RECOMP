// Finding the player's own copy of the game (see game_locator.h).

#include "game_locator.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commdlg.h>
#endif

namespace svr {
namespace {

constexpr const char *kRememberedFile = "svr2009-game.txt";

bool IsDiscImage(const std::filesystem::path &path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return ext == ".iso" || ext == ".xiso";
}

// A folder of extracted game files has default.xex at its root.
bool IsGameFolder(const std::filesystem::path &path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path / "default.xex", ec);
}

bool IsUsable(const std::filesystem::path &path) {
  std::error_code ec;
  if (path.empty())
    return false;
  if (std::filesystem::is_directory(path, ec))
    return IsGameFolder(path);
  return std::filesystem::is_regular_file(path, ec) && IsDiscImage(path);
}

std::filesystem::path Remembered(const std::filesystem::path &exe_dir) {
  std::ifstream in(exe_dir / kRememberedFile);
  std::string line;
  if (!in || !std::getline(in, line))
    return {};
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
    line.pop_back();
  return std::filesystem::path(std::u8string(line.begin(), line.end()));
}

void Remember(const std::filesystem::path &exe_dir, const std::filesystem::path &path) {
  std::ofstream out(exe_dir / kRememberedFile, std::ios::trunc);
  const std::u8string utf8 = path.u8string();
  out << std::string(utf8.begin(), utf8.end()) << "\n";
}

#if defined(_WIN32)
// Native "open file" dialog for the disc image or, in an extracted folder, its default.xex.
std::filesystem::path AskForGame() {
  wchar_t file[MAX_PATH * 4] = L"";
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter =
      L"WWE SmackDown vs. Raw 2009 (Xbox 360 disc image or default.xex)\0*.iso;*.xiso;default.xex\0"
      L"All files\0*.*\0";
  ofn.lpstrFile = file;
  ofn.nMaxFile = DWORD(std::size(file));
  ofn.lpstrTitle = L"Select your own copy of WWE SmackDown vs. Raw 2009 (Xbox 360)";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn))
    return {};
  std::filesystem::path chosen(file);
  // default.xex picked: the game is the folder around it.
  if (!IsDiscImage(chosen))
    chosen = chosen.parent_path();
  return chosen;
}
#else
std::filesystem::path AskForGame() { return {}; }
#endif

}  // namespace

std::filesystem::path LocateGameData(const std::filesystem::path &exe_dir) {
  std::error_code ec;
  // 1. Extracted files in "assets" next to the executable.
  if (IsGameFolder(exe_dir / "assets"))
    return exe_dir / "assets";
  // 2. A disc image dropped next to the executable (the simplest setup on Steam Deck).
  std::vector<std::filesystem::path> images;
  for (const auto &entry : std::filesystem::directory_iterator(exe_dir, ec)) {
    if (entry.is_regular_file(ec) && IsDiscImage(entry.path()))
      images.push_back(entry.path());
  }
  std::sort(images.begin(), images.end());
  if (!images.empty())
    return images.front();
  // 3. What the player picked last time.
  if (auto remembered = Remembered(exe_dir); IsUsable(remembered))
    return remembered;
  // 4. Ask.
  if (auto chosen = AskForGame(); IsUsable(chosen)) {
    Remember(exe_dir, chosen);
    return chosen;
  }
  return {};
}

}  // namespace svr
