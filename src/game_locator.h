// Finding the player's own copy of the game when no --game_data_root is given.
//
// In order: an "assets" folder of extracted files next to the executable, an Xbox 360 disc image
// (.iso) next to it, the path picked last time (svr2009-game.txt beside the executable), and
// finally a file picker for the disc image or an extracted folder's default.xex (remembered).
// The game files themselves are never part of this project.

#pragma once

#include <filesystem>

namespace svr {

// The game data to mount (a folder or a disc image), or empty if none was found or chosen.
std::filesystem::path LocateGameData(const std::filesystem::path &exe_dir);

}  // namespace svr
