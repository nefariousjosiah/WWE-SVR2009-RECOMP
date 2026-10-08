// The player's copy of the game: finding it, checking it, remembering it; settings; starting the game.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace launcher {

namespace fs = std::filesystem;

fs::path FromUtf8(const std::string &s);
std::string ToUtf8(const fs::path &p);

enum class DiscCheck {
  kReady,          // this game, the supported release
  kWrongRelease,   // this game, another disc release
  kOtherGame,      // a disc image of something else
  kUnreadable,     // not a disc image / missing / damaged
};

struct Disc {
  fs::path path;   // disc image or folder of extracted files (empty: none chosen yet)
  DiscCheck check = DiscCheck::kUnreadable;
  uint32_t title_id = 0, media_id = 0;
  std::string error;  // why it is unreadable
};

// Checks a disc image (any Xbox 360 GDFX layout), a folder of extracted files or a default.xex.
Disc CheckDisc(const fs::path &path);
// The copy to use: a disc image next to the game, else the one remembered in <id>-game.txt.
Disc FindDisc(const fs::path &game_dir);
// Remembers a copy in <id>-game.txt, the same file the game itself reads.
void RememberDisc(const fs::path &game_dir, const fs::path &path);

// The game's own settings file (<id>.toml next to it).
struct Settings {
  int render_scale = 0;  // 0 auto, 1 = 720p, 2 = 1440p, 3 = 4K
  bool fullscreen = true;
  bool fps60 = true;
  bool stretch = false;  // fill 16:10 screens instead of keeping 16:9 with bars
};
Settings LoadSettings(const fs::path &toml);
bool SaveSettings(const fs::path &toml, const Settings &settings);

// The running game.
class Runner {
 public:
  ~Runner();
  bool Start(const fs::path &exe, const fs::path &disc, std::string &error);
  bool Running();

 private:
  void *process_ = nullptr;  // HANDLE
};

}  // namespace launcher
