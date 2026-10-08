#include "disc.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <unordered_set>
#include <vector>

#include "game.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace launcher {

fs::path FromUtf8(const std::string &s) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}

std::string ToUtf8(const fs::path &p) {
  const std::u8string u = p.u8string();
  return std::string(u.begin(), u.end());
}

namespace {

uint32_t LoadBE32(const uint8_t *p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
uint32_t LoadLE32(const uint8_t *p) {
  return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0];
}
uint16_t LoadLE16(const uint8_t *p) { return uint16_t(p[1] << 8 | p[0]); }

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}

bool ReadAt(std::ifstream &f, uint64_t offset, void *dst, size_t size) {
  f.clear();
  f.seekg(std::streamoff(offset));
  f.read(static_cast<char *>(dst), std::streamsize(size));
  return f.gcount() == std::streamsize(size);
}

// XEX2 header: optional header 0x00040006 (execution info) = media ID, version, base version,
// title ID (big-endian).
bool ParseXex(const std::vector<uint8_t> &xex, Disc &disc) {
  if (xex.size() < 0x18 || std::memcmp(xex.data(), "XEX2", 4) != 0) {
    disc.error = "its default.xex isn't an Xbox 360 program";
    return false;
  }
  const uint32_t count = LoadBE32(&xex[0x14]);
  for (uint32_t i = 0; i < count && 0x18 + 8 * size_t(i) + 8 <= xex.size(); ++i) {
    const uint8_t *h = &xex[0x18 + 8 * size_t(i)];
    if (LoadBE32(h) != 0x00040006)
      continue;
    const uint32_t at = LoadBE32(h + 4);
    if (size_t(at) + 16 > xex.size())
      break;
    disc.media_id = LoadBE32(&xex[at]);
    disc.title_id = LoadBE32(&xex[at + 12]);
    return true;
  }
  disc.error = "its default.xex has no title information";
  return false;
}

bool ReadXexFile(const fs::path &path, std::vector<uint8_t> &xex) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return false;
  xex.resize(0x20000);  // the header sits well inside the first 128 KB
  f.read(reinterpret_cast<char *>(xex.data()), std::streamsize(xex.size()));
  xex.resize(size_t(f.gcount()));
  return !xex.empty();
}

// Xbox 360 disc images: the game partition starts at one of these offsets depending on how the
// image was made; its volume descriptor sits 32 sectors in.
bool ReadXexFromDisc(const fs::path &path, std::vector<uint8_t> &xex, Disc &disc) {
  constexpr uint64_t kSector = 2048;
  constexpr uint64_t kPartitionOffsets[] = {0x00000000, 0x0000FB20, 0x00020600, 0x02080000,
                                            0x0FD90000};
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    disc.error = "the file can't be opened";
    return false;
  }
  uint64_t game_offset = 0;
  uint8_t volume[28] = {};
  bool found = false;
  for (uint64_t offset : kPartitionOffsets) {
    if (ReadAt(f, offset + 32 * kSector, volume, sizeof(volume)) &&
        std::memcmp(volume, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
      game_offset = offset;
      found = true;
      break;
    }
  }
  if (!found) {
    disc.error = "it isn't an Xbox 360 disc image";
    return false;
  }
  const uint32_t root_sector = LoadLE32(volume + 20);
  const uint32_t root_size = LoadLE32(volume + 24);
  if (root_size < 14 || root_size > 32u << 20) {
    disc.error = "the disc image is damaged";
    return false;
  }
  std::vector<uint8_t> root(root_size);
  if (!ReadAt(f, game_offset + uint64_t(root_sector) * kSector, root.data(), root.size())) {
    disc.error = "the disc image is incomplete";
    return false;
  }
  // The root directory is a binary tree: left/right child (in dwords), sector, size, attributes,
  // name length, name.
  uint32_t xex_sector = 0, xex_size = 0;
  std::unordered_set<uint32_t> seen;
  std::function<bool(uint32_t)> find = [&](uint32_t ordinal) -> bool {
    const size_t at = size_t(ordinal) * 4;
    if (at + 14 > root.size() || !seen.insert(ordinal).second)
      return false;
    const uint8_t *e = &root[at];
    const size_t name_len = e[13];
    if (at + 14 + name_len > root.size())
      return false;
    if (Lower(std::string(reinterpret_cast<const char *>(e + 14), name_len)) == "default.xex") {
      xex_sector = LoadLE32(e + 4);
      xex_size = LoadLE32(e + 8);
      return true;
    }
    const uint16_t left = LoadLE16(e), right = LoadLE16(e + 2);
    return (left && find(left)) || (right && find(right));
  };
  if (!find(0) || !xex_size) {
    disc.error = "there's no default.xex on it";
    return false;
  }
  xex.resize(std::min<uint32_t>(xex_size, 0x20000));
  if (!ReadAt(f, game_offset + uint64_t(xex_sector) * kSector, xex.data(), xex.size())) {
    disc.error = "the disc image is incomplete";
    return false;
  }
  return true;
}

bool IsDiscImageName(const fs::path &p) {
  const std::string ext = Lower(ToUtf8(p.extension()));
  return ext == ".iso" || ext == ".xiso";
}

}  // namespace

Disc CheckDisc(const fs::path &path) {
  Disc disc;
  disc.path = path;
  std::error_code ec;
  std::vector<uint8_t> xex;
  bool read = false;
  if (fs::is_directory(path, ec)) {
    for (const fs::path &candidate : {path / "default.xex", path / "assets" / "default.xex"})
      if (!read && fs::is_regular_file(candidate, ec) && ReadXexFile(candidate, xex))
        read = ParseXex(xex, disc);
    if (!read && disc.error.empty())
      disc.error = "there's no default.xex in that folder";
  } else if (Lower(ToUtf8(path.extension())) == ".xex") {
    read = ReadXexFile(path, xex) && ParseXex(xex, disc);
    if (!read && disc.error.empty())
      disc.error = "the file can't be read";
  } else {
    read = ReadXexFromDisc(path, xex, disc) && ParseXex(xex, disc);
  }
  if (!read)
    disc.check = DiscCheck::kUnreadable;
  else if (disc.title_id != game::kTitleId)
    disc.check = DiscCheck::kOtherGame;
  else if (disc.media_id != game::kMediaId)
    disc.check = DiscCheck::kWrongRelease;
  else
    disc.check = DiscCheck::kReady;
  return disc;
}

Disc FindDisc(const fs::path &game_dir) {
  std::error_code ec;
  // A disc image next to the game wins, the same order the game itself uses.
  std::vector<fs::path> images;
  for (const auto &entry : fs::directory_iterator(game_dir, ec))
    if (entry.is_regular_file(ec) && IsDiscImageName(entry.path()))
      images.push_back(entry.path());
  std::sort(images.begin(), images.end());
  for (const fs::path &image : images) {
    Disc disc = CheckDisc(image);
    if (disc.check == DiscCheck::kReady)
      return disc;
  }
  std::ifstream in(game_dir / (std::string(game::kId) + "-game.txt"));
  std::string line;
  if (in && std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
      line.pop_back();
    if (!line.empty() && fs::exists(FromUtf8(line), ec))
      return CheckDisc(FromUtf8(line));
  }
  return images.empty() ? Disc{} : CheckDisc(images.front());
}

void RememberDisc(const fs::path &game_dir, const fs::path &path) {
  std::ofstream out(game_dir / (std::string(game::kId) + "-game.txt"), std::ios::trunc);
  out << ToUtf8(path) << "\n";
}

Settings LoadSettings(const fs::path &toml) {
  Settings s;
  std::ifstream f(toml);
  std::string line;
  auto trim = [](std::string &t) {
    if (const size_t hash = t.find('#'); hash != std::string::npos)
      t.erase(hash);
    t.erase(0, t.find_first_not_of(" \t"));
    t.erase(t.find_last_not_of(" \t\r") + 1);
  };
  while (std::getline(f, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos || line.find('#') < eq)
      continue;
    std::string key = line.substr(0, eq), value = line.substr(eq + 1);
    trim(key);
    trim(value);
    if (key == "fullscreen")
      s.fullscreen = value == "true";
    else if (key == "svr_60fps")
      s.fps60 = value == "true";
    else if (key == "svr_render_scale")
      s.render_scale = std::clamp(std::atoi(value.c_str()), 0, 3);
    else if (key == "bd_aspect_ratio")
      s.stretch = std::atoi(value.c_str()) == 6;  // renderer AspectMode::Stretch
  }
  return s;
}

bool SaveSettings(const fs::path &toml, const Settings &s) {
  std::vector<std::string> lines;
  {
    std::ifstream f(toml);
    std::string line;
    while (std::getline(f, line))
      lines.push_back(line);
  }
  const std::pair<const char *, std::string> values[] = {
      {"fullscreen", s.fullscreen ? "true" : "false"},
      {"svr_60fps", s.fps60 ? "true" : "false"},
      {"svr_render_scale", std::to_string(s.render_scale)},
      {"bd_aspect_ratio", s.stretch ? "6" : "5"},  // Stretch / Auto (16:9 with bars)
  };
  for (const auto &[key, value] : values) {
    const std::string assignment = std::string(key) + " = " + value;
    bool replaced = false;
    for (std::string &line : lines) {
      const size_t start = line.find_first_not_of(" \t");
      if (start == std::string::npos || line.compare(start, std::strlen(key), key) != 0)
        continue;
      const size_t after = line.find_first_not_of(" \t", start + std::strlen(key));
      if (after != std::string::npos && line[after] == '=') {
        line = assignment;
        replaced = true;
      }
    }
    if (!replaced)
      lines.push_back(assignment);
  }
  std::ofstream f(toml, std::ios::trunc);
  for (const std::string &line : lines)
    f << line << '\n';
  return bool(f);
}

#if defined(_WIN32)
Runner::~Runner() {
  if (process_)
    CloseHandle(process_);
}

bool Runner::Start(const fs::path &exe, const fs::path &disc, std::string &error) {
  fs::path data = disc;
  std::wstring data_arg = data.make_preferred().wstring();
  while (!data_arg.empty() && (data_arg.back() == L'\\' || data_arg.back() == L'/'))
    data_arg.pop_back();  // a trailing backslash would escape the closing quote
  std::wstring cmd = L"\"" + exe.wstring() + L"\" \"--game_data_root=" + data_arg + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  const std::wstring dir = exe.parent_path().wstring();
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(),
                      &si, &pi)) {
    error = "couldn't start " + ToUtf8(exe.filename()) + " (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  CloseHandle(pi.hThread);
  process_ = pi.hProcess;
  return true;
}

bool Runner::Running() {
  if (!process_)
    return false;
  if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT)
    return true;
  CloseHandle(process_);
  process_ = nullptr;
  return false;
}
#else
Runner::~Runner() = default;
bool Runner::Start(const fs::path &, const fs::path &, std::string &error) {
  error = "starting the game is only implemented for Windows (and Proton) so far";
  return false;
}
bool Runner::Running() { return false; }
#endif

}  // namespace launcher
