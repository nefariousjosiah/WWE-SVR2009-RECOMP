// Mods: files that replace the game's own, from the "mods" folder next to the executable.
//
// Each mod is a folder in that folder, extracted by the player: a .zip or .7z there, or one inside a
// mod with no game files of its own yet (the root.7z of SVR09+), is listed with a reminder to
// extract it. Its files replace the disc's files of the same path (pac\ch\ch12.pac), or of the same
// name when the name is unique on the disc (a bare ch12.pac). Mods load in alphabetical order: when
// two replace the same file, the later one wins. The settings menu's Mods page switches them on and
// off (svr_mods_off); that applies when the game starts.
//
// Mods made for the Xbox 360 often bring a save and DLC packages too (in their Content folder). A
// mod with those plays with saves of its own, so the game's own saves and DLC stay as they are: its
// DLC is installed there and its save copied in the first time. Changes a mod makes to the game's
// program (an xdelta patch of default.xex) can't apply here, since the program is recompiled into
// the executable; those of mods this port knows are built in (svr09plus.cpp).

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rex::system {
class KernelState;
}

namespace svr {

struct ModInfo {
  std::string name;     // the folder's (or a still packed archive's) name in the mods folder
  bool archive = false;  // a .zip or .7z not extracted yet: not used
  bool loaded = false;  // in use this session (it was on and usable when the game started)
  int replaced = 0;     // game files it replaces this session
  int overridden = 0;   // of those, ones a mod later in the list replaces instead
  int unused = 0;       // files that match nothing on the disc
  int rejected = 0;     // files skipped because they look made for a different game
  int dlc = 0;          // DLC packages it brings
  int saves = 0;        // save packages it brings
  bool own_saves = false;  // in use with saves of its own (it brings a save or DLC)
  bool packages_ignored = false;  // brings a save or DLC, but another mod's are in use
  enum class Program { kNone, kBuiltIn, kUnsupported } program = Program::kNone;
  std::string kinds;    // what it changes, from those files: "Superstar, Arena"
  std::string error;    // why it couldn't be used
};

// Finds the mods, and puts the mods in front of the game's files when
// those are mounted. Called once, before that (svr2009_app.h OnConfigurePaths); `config` is the
// settings file (read here: it's loaded only after this).
void ModsInit(const std::filesystem::path& mods_dir, const std::filesystem::path& config);

// The saves folder to use instead of `user_data`, when a mod in use brings its own save or DLC;
// empty otherwise.
std::filesystem::path ModsUserData(const std::filesystem::path& user_data);

// Installs the DLC of the mod with its own saves, and copies its save in when there's none yet.
// Before the game's own DLC is installed (a mod's pack can share a DLC pack's name).
void ModsInstallContent(rex::system::KernelState* kernel, const std::filesystem::path& user_data);

// Whether SVR09+ (yumboy11's mod, version 1.1) is in use: its program changes are built in.
bool ModsSvr09Plus();

// Every mod found when the game started, in load order.
std::vector<ModInfo> Mods();

// Names of the mods switched off (the svr_mods_off setting, names separated by '|').
std::vector<std::string> ModsOff();
std::string JoinModsOff(const std::vector<std::string>& names);
// Whether a restart would change the mods in use: one was switched on or off.
bool ModsPending();

// Opens the mods folder in Explorer. Not under Proton / Wine (Steam Deck, Linux): Wine's own file
// window is no use in Game Mode, so the Mods page leaves the button out and says where to go.
bool CanOpenModsFolder();
void OpenModsFolder();

}  // namespace svr
