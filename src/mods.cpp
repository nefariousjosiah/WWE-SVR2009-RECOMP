// Mods (mods.h): the mods folder, the mods' own saves and DLC, and a layer in front of the game's
// files.

#include "mods.h"

#include <rex/cvar.h>
#include <rex/filesystem/device.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xam/user_profile.h>

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

REXCVAR_DEFINE_STRING(svr_mods_off, "", "Game",
                      "Mods switched off in the settings menu: their names in the mods folder, "
                      "separated by |. Applies the next time the game starts");

namespace svr {
namespace fs = std::filesystem;
namespace rfs = rex::filesystem;
using rex::X_STATUS;

namespace {

// SVR09+ version 1.1 by yumboy11: its Patch.xdelta (FNV-1a 64 of the file).
constexpr uint64_t kSvr09PlusPatch = 0x75ADE9088F6AA0CCull;

std::mutex g_mutex;
fs::path g_dir;
std::vector<ModInfo> g_mods;            // found at startup, in load order
std::vector<std::vector<fs::path>> g_roots;  // per mod: its folder
std::vector<std::set<fs::path>> g_skip;      // per mod: packages and patches, not game files
std::vector<std::string> g_start_off;   // svr_mods_off when the game started
int g_own_saves = -1;                   // the mod whose saves and DLC are in use
std::atomic<bool> g_svr09plus{false};   // SVR09+ in use (read by its hooks, often)
std::vector<fs::path> g_dlc;            // its DLC packages
fs::path g_save;                        // its save package

std::string U8(const fs::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

fs::path FromU8(const std::string& s) {
  return fs::path(std::u8string(s.begin(), s.end()));
}

std::string Lower(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

// A path inside the game's files as the lookup key: lower case, backslashes, no leading one.
std::string Key(std::string_view path) {
  std::string key = Lower(path);
  std::replace(key.begin(), key.end(), '/', '\\');
  const size_t start = key.find_first_not_of('\\');
  if (start == std::string::npos)
    return {};
  key.erase(0, start);
  while (!key.empty() && key.back() == '\\')
    key.pop_back();
  return key;
}

bool IsDirectory(const rfs::Entry* entry) {
  return (entry->attributes() & rfs::kFileAttributeDirectory) != 0;
}

// Every file under `entry`, with its path components from `entry`.
template <typename Fn>
void ForEachFile(rfs::Entry* entry, std::vector<std::string>& parts, const Fn& fn) {
  for (const auto& child : entry->children()) {
    parts.push_back(child->name());
    if (IsDirectory(child.get()))
      ForEachFile(child.get(), parts, fn);
    else
      fn(child.get(), parts);
    parts.pop_back();
  }
}

std::string Extension(const fs::path& p) {
  return Lower(U8(p.extension()));
}

// Files a mod's author adds for people, not for the game: not counted as unused.
bool IsDocument(const std::string& name) {
  static const std::set<std::string> kExt = {".txt", ".md",  ".jpg",  ".jpeg", ".png", ".gif",
                                             ".bmp", ".webp", ".url", ".pdf",  ".nfo", ".rtf",
                                             ".htm", ".html", ".bak", ".exe", ".h"};
  return kExt.count(Extension(FromU8(name))) != 0;
}

bool IsArchive(const fs::path& p) {
  const std::string ext = Extension(p);
  return ext == ".zip" || ext == ".7z";
}

// A patch for the game's program (default.xex).
bool IsProgramPatch(const fs::path& p) {
  static const std::set<std::string> kExt = {".xdelta", ".vcdiff", ".xpatch", ".bps", ".ips", ".ppf"};
  return kExt.count(Extension(p)) != 0;
}

uint64_t Fnv1a64(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  uint64_t h = 0xCBF29CE484222325ull;
  std::vector<char> buf(1 << 16);
  while (in) {
    in.read(buf.data(), std::streamsize(buf.size()));
    for (std::streamsize i = 0; i < in.gcount(); ++i)
      h = (h ^ uint8_t(buf[size_t(i)])) * 0x100000001B3ull;
  }
  return h;
}

uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// Files made for the mods in use (rebuilt .arc tables, patched .pacs): mods\.cache\<sub>, a hidden
// folder.
fs::path CacheDir(const char* sub) {
  const fs::path base = g_dir / ".cache";
  std::error_code ec;
  if (!fs::exists(base, ec)) {
    fs::create_directories(base, ec);
#if defined(_WIN32)
    SetFileAttributesW(base.c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
  }
  return base / sub;
}

// An Xbox 360 package (STFS: CON, LIVE or PIRS): its content type (1 save, 2 DLC); 0 otherwise.
uint32_t PackageType(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  uint8_t h[0x348] = {};
  if (!in.read(reinterpret_cast<char*>(h), sizeof(h)))
    return 0;
  if (std::memcmp(h, "CON ", 4) != 0 && std::memcmp(h, "LIVE", 4) != 0 &&
      std::memcmp(h, "PIRS", 4) != 0)
    return 0;
  return Be32(h + 0x344);
}

// Looks through a mod's folder for its packages and program patches. A mod whose game files are
// still packed in an archive inside it (the root.7z of SVR09+) isn't used until that's extracted.
void FindPackages(size_t m) {
  ModInfo& mod = g_mods[m];
  std::vector<fs::path> dlc, saves;
  std::string packed;  // the first archive in it
  size_t files = 0;    // everything else: game files
  std::error_code ec;
  for (const fs::path& root : g_roots[m])
    for (auto it = fs::recursive_directory_iterator(root, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec) || IsDocument(U8(it->path().filename())))
        continue;
      if (IsProgramPatch(it->path())) {
        g_skip[m].insert(it->path());
        if (Fnv1a64(it->path()) == kSvr09PlusPatch)
          mod.program = ModInfo::Program::kBuiltIn;
        else if (mod.program == ModInfo::Program::kNone)
          mod.program = ModInfo::Program::kUnsupported;
        continue;
      }
      if (IsArchive(it->path())) {
        g_skip[m].insert(it->path());
        if (packed.empty())
          packed = U8(it->path().filename());
        continue;
      }
      const uint32_t type = PackageType(it->path());
      if (!type) {
        ++files;
        continue;
      }
      g_skip[m].insert(it->path());
      if (type == uint32_t(rex::system::XContentType::kMarketplaceContent))
        dlc.push_back(it->path());
      else if (type == uint32_t(rex::system::XContentType::kSavedGame))
        saves.push_back(it->path());
    }
  if (!files && !packed.empty()) {
    mod.error = "its " + packed + " isn't extracted yet. Extract it inside the mod's folder (Extract here), "
                "then restart the game";
    mod.loaded = false;
    return;
  }
  mod.dlc = int(dlc.size());
  mod.saves = int(saves.size());
  if (!mod.dlc && !mod.saves)
    return;
  if (g_own_saves >= 0 && g_own_saves != int(m)) {
    mod.packages_ignored = true;
    return;
  }
  g_own_saves = int(m);
  mod.own_saves = true;
  g_dlc = std::move(dlc);
  // A mod can offer more than one save: the one in its Content folder (where Xbox 360 content goes)
  // is its main one.
  std::sort(saves.begin(), saves.end());
  for (const fs::path& save : saves) {
    const std::string path = Lower(U8(save));
    if (path.find("\\content\\") != std::string::npos || path.find("/content/") != std::string::npos) {
      g_save = save;
      break;
    }
  }
  if (g_save.empty() && !saves.empty())
    g_save = saves.front();
}

// What a replaced game file is, for the Mods page.
const char* KindOf(const std::string& key) {
  const auto in = [&key](const char* prefix) { return key.rfind(prefix, 0) == 0; };
  if (in("pac\\ch\\"))
    return "Superstar";
  if (in("pac\\bg\\"))
    return "Arena";
  if (in("pac\\belt\\"))
    return "Title belt";
  if (in("pac\\menu\\") || in("pac\\game2d"))
    return "Menu";
  if (in("movies\\titantron\\"))
    return "Titantron";
  if (in("movies\\"))
    return "Video";
  if (in("music\\"))
    return "Music";
  if (in("pac\\sound"))
    return "Sound";
  return "Game files";
}

// Game files a mod never replaces: the program itself is recompiled into this executable.
bool NeverReplaced(const std::string& key) {
  return key == "default.xex" || key.rfind("$systemupdate", 0) == 0;
}

// Reads `out.size()` bytes of a file of the game's or of a mod's.
bool ReadEntry(rfs::Entry* entry, size_t offset, std::span<uint8_t> out) {
  rfs::File* file = nullptr;
  if (entry->Open(rfs::FileAccess::kFileReadData, &file) != X_STATUS_SUCCESS || !file)
    return false;
  size_t read = 0;
  const bool ok = file->ReadSync(out, offset, &read) == X_STATUS_SUCCESS && read == out.size();
  file->Destroy();
  return ok;
}

void PutBe16(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(uint8_t(v >> 8));
  out.push_back(uint8_t(v));
}

void PutBe32(std::vector<uint8_t>& out, uint32_t v) {
  PutBe16(out, v >> 16);
  PutBe16(out, v & 0xFFFF);
}

// A .pac's table of contents in plist360.arc's form. A .pac ("EPAC": little-endian size of the
// table at +4) has its table at 0x800: groups of an id (4 chars), a word count of 3 per member, a
// flags word and an offset, each followed by its members (a name, an offset, a size). The arc holds
// the same in big-endian, with the word count times 16.
bool ReadTable(rfs::Entry* pac, std::vector<uint8_t>& table);
bool ArcTable(rfs::Entry* pac, std::vector<uint8_t>& out) {
  std::vector<uint8_t> table;
  if (!ReadTable(pac, table))
    return false;
  const size_t size = table.size();
  for (size_t at = 0; at + 12 <= size && Le32(&table[at]) != 0;) {
    const uint32_t words = Le16(&table[at + 4]);
    if (words % 3 != 0 || at + 12 + size_t(words) * 4 > size)
      return false;
    out.insert(out.end(), &table[at], &table[at] + 4);
    PutBe16(out, words << 4);
    PutBe16(out, Le16(&table[at + 6]));
    PutBe32(out, Le32(&table[at + 8]));
    at += 12;
    for (uint32_t m = 0; m < words / 3; ++m, at += 12) {
      out.insert(out.end(), &table[at], &table[at] + 4);
      PutBe32(out, Le32(&table[at + 4]));
      PutBe32(out, Le32(&table[at + 8]));
    }
  }
  return true;
}

// Calls fn(name, position of the member's row in the table) for each member in a .pac's table
// (ArcTable has the layout).
template <typename Fn>
void ForEachMember(const std::vector<uint8_t>& table, const Fn& fn) {
  for (size_t at = 0; at + 12 <= table.size() && Le32(&table[at]) != 0;) {
    const uint32_t words = Le16(&table[at + 4]);
    if (words % 3 != 0 || at + 12 + size_t(words) * 4 > table.size())
      return;
    at += 12;
    for (uint32_t m = 0; m < words / 3; ++m, at += 12)
      fn(std::string(reinterpret_cast<const char*>(&table[at]), 4), at);
  }
}

bool ReadTable(rfs::Entry* pac, std::vector<uint8_t>& table) {
  uint8_t head[16];
  if (pac->size() < 0x800 || !ReadEntry(pac, 0, head) || std::memcmp(head, "EPAC", 4) != 0)
    return false;
  const uint32_t size = Le32(head + 4);
  if (size < 12 || size > 0x3800 || 0x800 + size > pac->size())
    return false;
  table.resize(size);
  return ReadEntry(pac, 0x800, table);
}

void PutLe32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v), p[1] = uint8_t(v >> 8), p[2] = uint8_t(v >> 16), p[3] = uint8_t(v >> 24);
}

// The layout of a .pac: table at 0x800, members from 0x4000 at offsets in 0x800-byte units, sizes
// in 0x100-byte units; the header's size at +8 is the file's less 0x4800.
constexpr size_t kPacData = 0x4000, kPacOffsetUnit = 0x800, kPacSizeUnit = 0x100, kPacExtra = 0x4800;

// The sub-file ids of a PACH member at `at`; false if there's none there.
bool PachIds(rfs::Entry* entry, size_t at, std::unordered_set<uint32_t>& ids) {
  uint8_t head[8];
  if (at + 8 > entry->size() || !ReadEntry(entry, at, head) || std::memcmp(head, "PACH", 4) != 0)
    return false;
  const uint32_t count = Le32(head + 4);
  if (count == 0 || count > 0x10000 || at + 8 + size_t(count) * 12 > entry->size())
    return false;
  std::vector<uint8_t> rows(size_t(count) * 12);
  if (!ReadEntry(entry, at + 8, rows))
    return false;
  for (uint32_t i = 0; i < count; ++i)
    ids.insert(Le32(&rows[size_t(i) * 12]));
  return true;
}

// Whether a mod's member looks made for a different game: the game looks its sub-files up by id and
// crashes on one that isn't there, and a member from another game numbers them differently. Real
// mods lack at most a handful (4 of 7149 members' worst, in SvR 2007's Ultimate Edition); an arena
// pack built for a later game lacks about 48 of about 190. A share of the ids alone won't do: small
// members of a real mod can lack 30% of theirs.
bool WrongGame(rfs::Entry* mod, size_t mod_at, rfs::Entry* game, size_t game_at) {
  constexpr size_t kMissing = 16;
  std::unordered_set<uint32_t> mod_ids, game_ids;
  if (!PachIds(mod, mod_at, mod_ids) || !PachIds(game, game_at, game_ids))
    return false;  // can't tell
  size_t missing = 0;
  for (const uint32_t id : game_ids)
    missing += mod_ids.count(id) ? 0 : 1;
  return missing >= kMissing && missing * 10 >= game_ids.size();
}

// The same for a whole .pac: any member of it.
bool PacWrongGame(rfs::Entry* mod, rfs::Entry* game) {
  std::vector<uint8_t> mod_table, game_table;
  if (!ReadTable(mod, mod_table) || !ReadTable(game, game_table))
    return false;
  std::unordered_map<std::string, uint32_t> mod_offsets;
  ForEachMember(mod_table, [&](const std::string& name, size_t at) {
    mod_offsets[name] = Le32(&mod_table[at + 4]);
  });
  bool wrong = false;
  ForEachMember(game_table, [&](const std::string& name, size_t at) {
    auto it = mod_offsets.find(name);
    wrong = wrong || (it != mod_offsets.end() &&
                      WrongGame(mod, kPacData + size_t(it->second) * kPacOffsetUnit, game,
                                kPacData + size_t(Le32(&game_table[at + 4])) * kPacOffsetUnit));
  });
  return wrong;
}

// Disc file -> the mod's file used in its place, and the files mods add. Filled before the game
// starts, read-only after.
std::unordered_map<rfs::Entry*, rfs::Entry*> g_redirects;
std::unordered_map<std::string, rfs::Entry*> g_added;
rfs::Device* g_game_device = nullptr;  // the disc's: other devices (saves) aren't touched

// Every file the game opens (rex::filesystem::SetOpenRedirect).
rfs::Entry* RedirectOpen(rfs::Entry* entry, rfs::Entry* parent, std::string_view name) {
  if (!entry) {
    if (!parent || parent->device() != g_game_device)
      return nullptr;
    auto added = g_added.find(Key(parent->path() + "\\" + std::string(name)));
    if (added == g_added.end())
      return nullptr;
    static std::mutex s_added_mutex;
    static std::unordered_set<rfs::Entry*> s_added_logged;
    std::lock_guard lock(s_added_mutex);
    if (s_added_logged.insert(added->second).second)
      REXLOG_INFO("Mods: the game opened {}: added by a mod ({} bytes)", added->first,
                  added->second->size());
    return added->second;
  }
  auto it = g_redirects.find(entry);
  if (it == g_redirects.end())
    return entry;
  static std::mutex s_mutex;
  static std::unordered_set<rfs::Entry*> s_logged;
  std::lock_guard lock(s_mutex);
  if (s_logged.insert(entry).second)
    REXLOG_INFO("Mods: the game opened {}: using the mod's file ({} bytes, the game's is {})",
                entry->path(), it->second->size(), entry->size());
  return it->second;
}

// The layer in front of the game's files: a path a mod replaces resolves to the mod's file, every
// other path to the disc's (RedirectOpen does the same for files opened through their folder).
class ModsDevice final : public rfs::Device {
 public:
  explicit ModsDevice(std::unique_ptr<rfs::Device> base)
      : Device(base->mount_path()), base_(std::move(base)) {}

  bool Initialize() override {
    if (!base_->Initialize())
      return false;
    Load();
    return true;
  }
  rfs::Entry* ResolvePath(const std::string_view path) override {
    if (!replaced_.empty())
      if (auto it = replaced_.find(Key(path)); it != replaced_.end())
        return it->second;
    return base_->ResolvePath(path);
  }
  bool is_read_only() const override { return base_->is_read_only(); }
  void Dump(rex::string::StringBuffer* buffer) override { base_->Dump(buffer); }
  const std::string& name() const override { return base_->name(); }
  uint32_t attributes() const override { return base_->attributes(); }
  uint32_t component_name_max_length() const override { return base_->component_name_max_length(); }
  uint32_t total_allocation_units() const override { return base_->total_allocation_units(); }
  uint32_t available_allocation_units() const override { return base_->available_allocation_units(); }
  uint32_t sectors_per_allocation_unit() const override { return base_->sectors_per_allocation_unit(); }
  uint32_t bytes_per_sector() const override { return base_->bytes_per_sector(); }

 private:
  struct MemberFile {
    size_t mod;
    rfs::Entry* file;
    std::string name;  // lower case
  };

  void Load() {
    // The disc's files: by path, and by name for the names that are unique on it.
    std::unordered_map<std::string, rfs::Entry*> disc;
    std::unordered_map<std::string, std::string> by_name;  // "" when the name isn't unique
    if (rfs::Entry* root = base_->ResolvePath("")) {
      std::vector<std::string> parts;
      ForEachFile(root, parts, [&](rfs::Entry* entry, const std::vector<std::string>& p) {
        std::string key;
        for (const std::string& s : p)
          key += (key.empty() ? "" : "\\") + Lower(s);
        disc[key] = entry;
        auto [it, added] = by_name.try_emplace(Lower(p.back()), key);
        if (!added)
          it->second.clear();
      });
    }

    std::unordered_set<std::string> folders;  // the disc's top-level folders, in lower case
    for (const auto& [key, entry] : disc)
      if (const size_t at = key.find('\\'); at != std::string::npos)
        folders.insert(key.substr(0, at));

    g_game_device = base_.get();
    std::lock_guard lock(g_mutex);
    std::unordered_map<std::string, size_t> owner;  // replaced path -> mod index
    std::vector<MemberFile> members;
    for (size_t m = 0; m < g_mods.size(); ++m) {
      ModInfo& mod = g_mods[m];
      if (!mod.loaded)
        continue;
      for (const fs::path& root : g_roots[m]) {
        auto device = std::make_unique<rfs::HostPathDevice>(mount_path_, root, true);
        rfs::Entry* top = device->Initialize() ? device->ResolvePath("") : nullptr;
        if (!top) {
          mod.error = "couldn't read its files";
          mod.loaded = false;
          break;
        }
        std::vector<std::string> parts;
        ForEachFile(top, parts, [&](rfs::Entry* file, const std::vector<std::string>& p) {
          fs::path host = root;
          for (const std::string& s : p)
            host /= FromU8(s);
          if (g_skip[m].count(host))
            return;  // a save, DLC, archive or program patch: not a game file
          // The disc path the file's path ends with (a mod may sit in folders of its own), else
          // the disc file with its name.
          std::string target;
          for (size_t k = 0; k < p.size() && target.empty(); ++k) {
            std::string key;
            for (size_t i = k; i < p.size(); ++i)
              key += (key.empty() ? "" : "\\") + Lower(p[i]);
            if (disc.count(key))
              target = key;
          }
          if (target.empty())
            if (auto it = by_name.find(Lower(p.back())); it != by_name.end())
              target = it->second;
          if (target.empty()) {
            if (IsDocument(p.back()))
              return;
            // A new file: its path from one of the disc's folders (pac\ch\ch06.pac).
            for (size_t k = 0; k + 1 < p.size() && target.empty(); ++k)
              if (folders.count(Lower(p[k]))) {
                for (size_t i = k; i < p.size(); ++i)
                  target += (target.empty() ? "" : "\\") + Lower(p[i]);
              }
            if (!target.empty()) {
              if (owner.try_emplace(target, m).second) {
                ++mod.replaced;
                AddKind(mod, KindOf(target));
              } else if (owner[target] != m) {
                ++g_mods[owner[target]].overridden;
                owner[target] = m;
              } else {
                return;
              }
              replaced_[target] = file;
              g_added[target] = file;
              return;
            }
            // 0014.pac: one member of a .pac, placed once all the mods are in.
            const std::string low = Lower(p.back());
            if (low.size() == 8 && low.ends_with(".pac"))
              members.push_back({m, file, low.substr(0, 4)});
            else
              ++mod.unused;
            return;
          }
          if (NeverReplaced(target))
            return;
          if (target.rfind("pac\\bg\\", 0) == 0 && PacWrongGame(file, disc[target])) {
            ++mod.rejected;
            return;
          }
          if (auto it = owner.find(target); it != owner.end()) {
            if (it->second == m)
              return;  // the same file twice in one mod: the first is used
            ++g_mods[it->second].overridden;
          } else {
            ++mod.replaced;
          }
          AddKind(mod, KindOf(target));
          owner[target] = m;
          replaced_[target] = file;
          g_redirects[disc[target]] = file;
        });
        devices_.push_back(std::move(device));
      }
    }
    PatchMembers(disc, members);
    for (ModInfo& mod : g_mods) {
      if (!mod.loaded)
        continue;
      if (mod.rejected)
        REXLOG_WARN("Mods: {}: {} file(s) look made for a different game: not used", mod.name,
                    mod.rejected);
      if (mod.rejected && !mod.replaced) {
        mod.error = "it looks made for a different game, so it was left out to stop a crash";
        mod.loaded = false;
        continue;
      }
      REXLOG_INFO("Mods: {} replaces {} game file(s){}{}", mod.name, mod.replaced,
                  mod.unused ? fmt::format(", {} file(s) match nothing on the disc", mod.unused)
                             : std::string(),
                  mod.own_saves ? fmt::format("; plays with its own saves ({} DLC, {} save)",
                                              mod.dlc, mod.saves ? 1 : 0)
                                : std::string());
      if (mod.overridden)
        REXLOG_WARN("Mods: {} file(s) of {} are replaced by a mod later in the list", mod.overridden,
                    mod.name);
    }
    PatchArc(disc, "plist360.arc", "pac\\plist360.h");
    PatchArc(disc, "plist360_4x3.arc", "pac\\plist360_4x3.h");
    if (!g_redirects.empty() || !g_added.empty())
      rfs::SetOpenRedirect(&RedirectOpen);
  }

  static void AddKind(ModInfo& mod, const std::string& kind) {
    if (mod.kinds.find(kind) == std::string::npos)
      mod.kinds += (mod.kinds.empty() ? "" : ", ") + kind;
  }

  rfs::Entry* PacInUse(const std::unordered_map<std::string, rfs::Entry*>& disc,
                       const std::string& key) const {
    if (auto it = replaced_.find(key); it != replaced_.end())
      return it->second;
    auto it = disc.find(key);
    return it != disc.end() ? it->second : nullptr;
  }

  static std::vector<std::string> ReadPacList(rfs::Entry* entry) {
    std::vector<uint8_t> bytes(entry->size());
    std::vector<std::string> pacs;
    if (!ReadEntry(entry, 0, bytes))
      return pacs;
    std::string line;
    bytes.push_back('\n');
    for (const uint8_t c : bytes) {
      if (c == '\n') {
        if (const std::string key = Key(line.substr(0, line.find('\r'))); !key.empty())
          pacs.push_back(key);
        line.clear();
      } else {
        line.push_back(char(c));
      }
    }
    return pacs;
  }

  // Mods' files for one member of a .pac (0014.pac): a member with that name goes in, into a copy
  // of the .pac kept in mods\.cache\pacs. Several .pacs can have a member of a name, so the one
  // closest in size is the one meant. A bigger member than the slot goes at the end of the .pac, as
  // mod tools do; the arc gets the new table from PatchArc.
  void PatchMembers(const std::unordered_map<std::string, rfs::Entry*>& disc,
                    const std::vector<MemberFile>& members) {
    if (members.empty())
      return;
    rfs::Entry* list = PacInUse(disc, "pac\\plist360.h");
    if (!list)
      return;
    struct Slot {
      std::string pac;
      uint32_t size, offset;
    };
    std::unordered_map<std::string, std::vector<Slot>> slots;  // member name -> where it is
    for (const std::string& pac : ReadPacList(list)) {
      rfs::Entry* entry = PacInUse(disc, pac);
      std::vector<uint8_t> table;
      if (!entry || !ReadTable(entry, table))
        continue;
      ForEachMember(table, [&](const std::string& name, size_t at) {
        slots[Lower(name)].push_back(
            {pac, Le32(&table[at + 8]) * uint32_t(kPacSizeUnit), Le32(&table[at + 4])});
      });
    }

    // .pac -> member name -> the mod's file; later mods win.
    std::unordered_map<std::string, std::unordered_map<std::string, const MemberFile*>> plan;
    for (const MemberFile& member : members) {
      ModInfo& mod = g_mods[member.mod];
      auto it = slots.find(member.name);
      if (it == slots.end()) {
        ++mod.unused;
        continue;
      }
      const Slot* best = nullptr;
      const auto gap = [&member](const Slot& slot) {
        const uint64_t a = member.file->size(), b = slot.size;
        return a > b ? a - b : b - a;
      };
      for (const Slot& slot : it->second)
        if (!best || gap(slot) < gap(*best))
          best = &slot;
      if (WrongGame(member.file, 0, PacInUse(disc, best->pac),
                    kPacData + size_t(best->offset) * kPacOffsetUnit)) {
        ++mod.rejected;
        continue;
      }
      const MemberFile*& used = plan[best->pac][member.name];
      if (used) {
        if (used->mod == member.mod)
          continue;  // the same member twice in one mod: the first is used
        ++g_mods[used->mod].overridden;
      } else {
        ++mod.replaced;
      }
      used = &member;
      AddKind(mod, KindOf(best->pac));
    }

    std::error_code ec;
    const fs::path dir = CacheDir("pacs");
    std::vector<std::string> built;
    for (const auto& [pac, files] : plan) {
      rfs::Entry* base = PacInUse(disc, pac);
      if (!base)
        continue;
      // Built once, then again only when what goes into it changes.
      std::string sign = std::to_string(base->size());
      std::vector<std::string> names;
      for (const auto& [name, file] : files)
        names.push_back(name);
      std::sort(names.begin(), names.end());
      for (const std::string& name : names) {
        const rfs::Entry* file = files.at(name)->file;
        sign += " " + name + ":" + std::to_string(file->size()) + ":" +
                std::to_string(file->write_timestamp());
      }
      const fs::path out = dir / FromU8(pac);
      fs::path stamp = out;
      stamp += ".stamp";
      {
        std::ifstream in(stamp);
        std::string have;
        std::getline(in, have);
        if (have == sign && fs::exists(out, ec)) {
          built.push_back(pac);
          continue;
        }
      }
      std::vector<uint8_t> data(base->size());
      std::vector<uint8_t> table;
      if (!ReadEntry(base, 0, data) || !ReadTable(base, table)) {
        REXLOG_WARN("Mods: couldn't read {}", pac);
        continue;
      }
      ForEachMember(table, [&](const std::string& name, size_t at) {
        auto it = files.find(Lower(name));
        if (it == files.end())
          return;
        std::vector<uint8_t> member(it->second->file->size());
        if (member.empty() || !ReadEntry(it->second->file, 0, member)) {
          REXLOG_WARN("Mods: couldn't read the file for {} in {}", name, pac);
          return;
        }
        const uint32_t offset = Le32(&table[at + 4]), units = Le32(&table[at + 8]);
        const uint32_t new_units = uint32_t((member.size() + kPacSizeUnit - 1) / kPacSizeUnit);
        size_t where = kPacData + size_t(offset) * kPacOffsetUnit;
        if (new_units > units) {
          data.resize((data.size() + kPacOffsetUnit - 1) / kPacOffsetUnit * kPacOffsetUnit);
          where = data.size();
          data.resize(where + size_t(new_units) * kPacSizeUnit);
          PutLe32(&table[at + 4], uint32_t((where - kPacData) / kPacOffsetUnit));
        } else if (where + size_t(units) * kPacSizeUnit <= data.size()) {
          std::fill_n(data.begin() + where, size_t(units) * kPacSizeUnit, uint8_t(0));
        } else {
          return;
        }
        std::memcpy(&data[where], member.data(), member.size());
        PutLe32(&table[at + 8], new_units);
      });
      data.resize((data.size() + kPacOffsetUnit - 1) / kPacOffsetUnit * kPacOffsetUnit);
      std::memcpy(&data[0x800], table.data(), table.size());
      PutLe32(&data[8], uint32_t(data.size() - kPacExtra));
      fs::create_directories(out.parent_path(), ec);
      std::ofstream file(out, std::ios::binary | std::ios::trunc);
      file.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
      if (!file) {
        REXLOG_WARN("Mods: couldn't write {}", U8(out));
        continue;
      }
      file.close();
      std::ofstream(stamp) << sign << "\n";
      built.push_back(pac);
      REXLOG_INFO("Mods: {} built with {} member(s) replaced", pac, files.size());
    }
    if (built.empty())
      return;
    auto device = std::make_unique<rfs::HostPathDevice>(mount_path_, dir, true);
    if (!device->Initialize())
      return;
    for (const std::string& pac : built) {
      rfs::Entry* entry = device->ResolvePath(pac);
      auto it = disc.find(pac);
      if (!entry || it == disc.end())
        continue;
      replaced_[pac] = entry;
      g_redirects[it->second] = entry;
    }
    devices_.push_back(std::move(device));
  }

  // plist360.arc holds the tables of contents of all the .pacs, in the order of plist360.h, each
  // after a 4-byte marker (FFFF and its index as 16 bits; the first is FF000000), and ends in
  // FFFFFFFF. Mod tools have to update it after changing a .pac, and a mod's own arc can't be
  // trusted to be right (SVR09+'s numbers its last two .pacs wrong), so when mods touch the game's
  // .pacs it is always built here from the .pacs in use. Built from the disc's own files this comes
  // out byte for byte the same as the disc's arc.
  void PatchArc(const std::unordered_map<std::string, rfs::Entry*>& disc, const char* arc_name,
                const char* list_name) {
    const auto arc_it = disc.find(arc_name);
    rfs::Entry* list_entry = PacInUse(disc, list_name);  // a mod's list when it has one
    if (arc_it == disc.end() || !list_entry)
      return;
    const std::vector<std::string> pacs = ReadPacList(list_entry);
    int patched = 0;
    for (const std::string& pac : pacs)
      patched += replaced_.count(pac) ? 1 : 0;
    if (!patched && !replaced_.count(arc_name))
      return;
    if (pacs.size() > 0xFFFF) {
      REXLOG_WARN("Mods: {} lists {} .pac files, more than an arc can hold", list_name, pacs.size());
      return;
    }
    std::vector<uint8_t> out;
    for (size_t index = 0; index < pacs.size(); ++index) {
      PutBe32(out, index ? 0xFFFF0000u | uint32_t(index) : 0xFF000000u);
      rfs::Entry* pac = PacInUse(disc, pacs[index]);
      if (!pac || !ArcTable(pac, out)) {
        REXLOG_WARN("Mods: {} is in {} but has no table of contents the game can use", pacs[index],
                    list_name);
        return;
      }
    }
    PutBe32(out, 0xFFFFFFFF);

    // Written to the mods' cache and opened like a mod's file.
    std::error_code ec;
    const fs::path dir = CacheDir("arc");
    fs::create_directories(dir, ec);
    {
      std::ofstream file(dir / arc_name, std::ios::binary | std::ios::trunc);
      file.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
      if (!file) {
        REXLOG_WARN("Mods: couldn't write {}", arc_name);
        return;
      }
    }
    auto device = std::make_unique<rfs::HostPathDevice>(mount_path_, dir, true);
    rfs::Entry* entry = device->Initialize() ? device->ResolvePath(arc_name) : nullptr;
    if (!entry)
      return;
    replaced_[arc_name] = entry;
    g_redirects[arc_it->second] = entry;
    devices_.push_back(std::move(device));
    REXLOG_INFO("Mods: {} rebuilt for {} replaced .pac file(s)", arc_name, patched);
  }

  std::unique_ptr<rfs::Device> base_;
  std::vector<std::unique_ptr<rfs::HostPathDevice>> devices_;  // one per folder, not mounted
  std::unordered_map<std::string, rfs::Entry*> replaced_;
};

std::unique_ptr<rfs::Device> WrapGameDevice(std::unique_ptr<rfs::Device> device) {
  std::lock_guard lock(g_mutex);
  // Here rather than in ModsInit: the log is open by now.
  REXLOG_INFO("Mods: {} found in {}", g_mods.size(), U8(g_dir));
  for (const ModInfo& mod : g_mods) {
    if (!mod.error.empty())
      REXLOG_WARN("Mods: {} not used: {}", mod.name, mod.error);
  }
  const bool any = std::any_of(g_mods.begin(), g_mods.end(), [](const ModInfo& m) { return m.loaded; });
  if (!any)
    return device;
  return std::make_unique<ModsDevice>(std::move(device));
}

std::vector<std::string> SplitOff(const std::string& value) {
  std::vector<std::string> names;
  std::string v = value;
  std::erase(v, '"');
  for (size_t a = 0; a <= v.size();) {
    const size_t b = std::min(v.find('|', a), v.size());
    if (b > a)
      names.push_back(v.substr(a, b - a));
    a = b + 1;
  }
  std::sort(names.begin(), names.end());
  return names;
}

// svr_mods_off as the settings file has it: ModsInit runs before the game loads that file.
std::string OffFromConfig(const fs::path& config) {
  std::ifstream in(config);
  std::string line;
  while (std::getline(in, line)) {
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos || line.compare(start, 12, "svr_mods_off") != 0)
      continue;
    const size_t eq = line.find('=', start);
    const size_t open = eq == std::string::npos ? eq : line.find('"', eq);
    const size_t close = open == std::string::npos ? open : line.rfind('"');
    if (close != std::string::npos && close > open)
      return line.substr(open + 1, close - open - 1);
  }
  return {};
}

// Copies the files of a save package into the save folder the game reads.
bool CopySave(const fs::path& package, const fs::path& dest, std::string& error) {
  rfs::StfsContainerDevice device("", package);
  if (!device.Initialize()) {
    error = "couldn't read the save package";
    return false;
  }
  rfs::Entry* root = device.ResolvePath("");
  if (!root) {
    error = "the save package is empty";
    return false;
  }
  std::error_code ec;
  fs::create_directories(dest, ec);
  std::vector<std::string> parts;
  bool ok = true;
  ForEachFile(root, parts, [&](rfs::Entry* entry, const std::vector<std::string>& p) {
    fs::path out = dest;
    for (const std::string& s : p)
      out /= FromU8(s);
    std::vector<uint8_t> data(entry->size());
    if (!data.empty() && !ReadEntry(entry, 0, data)) {
      ok = false;
      return;
    }
    fs::create_directories(out.parent_path(), ec);
    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    ok = ok && bool(file);
  });
  if (!ok)
    error = "couldn't copy the save's files";
  return ok;
}

}  // namespace

void ModsInit(const fs::path& mods_dir, const fs::path& config) {
  std::error_code ec;
  fs::create_directories(mods_dir, ec);
  const std::string off_value =
      REXCVAR_GET(svr_mods_off).empty() ? OffFromConfig(config) : REXCVAR_GET(svr_mods_off);
  const std::vector<std::string> off = SplitOff(off_value);
  std::vector<ModInfo> mods;
  for (const auto& entry : fs::directory_iterator(mods_dir, ec)) {
    const std::string name = U8(entry.path().filename());
    if (name.empty() || name.front() == '.' || name.find_first_of("|\"\\") != std::string::npos)
      continue;
    ModInfo mod;
    mod.name = name;
    mod.loaded = !std::binary_search(off.begin(), off.end(), name);
    if (IsArchive(entry.path()) && !entry.is_directory(ec)) {
      // A mod as downloaded: listed, so the Mods page can say to extract it.
      mod.archive = true;
      mod.loaded = false;
      mod.error = "it's still packed. Extract it here in the mods folder (Extract here), then "
                  "restart the game";
    } else if (!entry.is_directory(ec)) {
      continue;  // the readme, a stray file
    }
    mods.push_back(std::move(mod));
  }
  std::sort(mods.begin(), mods.end(),
            [](const ModInfo& a, const ModInfo& b) { return Lower(a.name) < Lower(b.name); });

  std::lock_guard lock(g_mutex);
  g_dir = mods_dir;
  g_mods = std::move(mods);
  g_start_off = off;
  g_roots.assign(g_mods.size(), {});
  g_skip.assign(g_mods.size(), {});
  for (size_t m = 0; m < g_mods.size(); ++m) {
    ModInfo& mod = g_mods[m];
    if (!mod.loaded)
      continue;
    g_roots[m] = {g_dir / FromU8(mod.name)};
    FindPackages(m);
  }
  g_svr09plus = std::any_of(g_mods.begin(), g_mods.end(), [](const ModInfo& m) {
    return m.loaded && m.program == ModInfo::Program::kBuiltIn;
  });
  rfs::SetGameDeviceWrapper(&WrapGameDevice);
}

fs::path ModsUserData(const fs::path& user_data) {
  std::lock_guard lock(g_mutex);
  if (g_own_saves < 0)
    return {};
  // <userdata>-mods\<the mod's folder name>
  const fs::path name = FromU8(g_mods[size_t(g_own_saves)].name);
  fs::path base = user_data;
  if (!base.has_filename())
    base = base.parent_path();
  return base.parent_path() / FromU8(U8(base.filename()) + "-mods") / name;
}

void ModsInstallContent(rex::system::KernelState* kernel, const fs::path& user_data) {
  std::vector<fs::path> dlc;
  fs::path save;
  std::string mod_name;
  {
    std::lock_guard lock(g_mutex);
    if (g_own_saves < 0)
      return;
    dlc = g_dlc;
    save = g_save;
    mod_name = g_mods[size_t(g_own_saves)].name;
  }
  auto* content = kernel ? kernel->content_manager() : nullptr;
  if (!content)
    return;
  const uint32_t hdd = uint32_t(rex::system::xam::DummyDeviceId::HDD);

  // Its DLC, under the game's title whatever title the package names (mods often use their own).
  std::set<std::string> installed;
  for (const auto& data :
       content->ListContent(hdd, 0, rex::system::XContentType::kMarketplaceContent))
    installed.insert(data.file_name());
  for (const fs::path& package : dlc) {
    const std::string name = U8(package.filename());
    if (installed.count(name))
      continue;
    REXLOG_INFO("Mods: installing {}'s DLC {} (once)", mod_name, name);
    const auto result = content->InstallContent(package);
    if (result != 0)
      REXLOG_WARN("Mods: couldn't install {}'s DLC {} (error {})", mod_name, name, result);
  }

  // Its save, the first time only: after that the save is the player's progress.
  if (save.empty() || !kernel->user_profile())
    return;
  const uint64_t xuid = kernel->user_profile()->xuid();
  rex::system::xam::XCONTENT_AGGREGATE_DATA data;
  data.device_id = hdd;
  data.content_type = rex::system::XContentType::kSavedGame;
  data.title_id = kernel->title_id();
  data.xuid = xuid;
  data.set_file_name(U8(save.filename()));
  if (content->ContentExists(xuid, data))
    return;
  rfs::StfsContainerDevice package("", save);
  if (package.Initialize()) {
    const auto display = package.header().metadata.display_name(rex::system::XLanguage::kEnglish);
    data.set_display_name(display.empty() ? u"Save" : display);
  }
  const fs::path dest = user_data / fmt::format("{:016X}", xuid) /
                        fmt::format("{:08X}", kernel->title_id()) / "00000001" / save.filename();
  std::string error;
  if (!CopySave(save, dest, error)) {
    REXLOG_WARN("Mods: couldn't copy {}'s save in: {}", mod_name, error);
    return;
  }
  content->WriteContentHeaderFile(xuid, data, 0);
  REXLOG_INFO("Mods: {}'s save copied in ({})", mod_name, U8(save.filename()));
}

bool ModsSvr09Plus() { return g_svr09plus.load(std::memory_order_relaxed); }

std::vector<ModInfo> Mods() {
  std::lock_guard lock(g_mutex);
  return g_mods;
}

std::vector<std::string> ModsOff() { return SplitOff(REXCVAR_GET(svr_mods_off)); }

std::string JoinModsOff(const std::vector<std::string>& names) {
  std::string out;
  for (const std::string& n : names)
    out += (out.empty() ? "" : "|") + n;
  return out;
}

bool ModsPending() {
  std::lock_guard lock(g_mutex);
  return ModsOff() != g_start_off;
}

bool CanOpenModsFolder() {
#if defined(_WIN32)
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  return !(ntdll && GetProcAddress(ntdll, "wine_get_version"));
#else
  return false;
#endif
}

void OpenModsFolder() {
  fs::path dir;
  {
    std::lock_guard lock(g_mutex);
    dir = g_dir;
  }
#if defined(_WIN32)
  ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

}  // namespace svr
