// The game this launcher starts. The only file that differs between the games' launchers.
#pragma once

#include <cstdint>

namespace launcher::game {

inline constexpr const char *kId = "svr2009";  // svr2009.exe, svr2009.toml, svr2009-game.txt
inline constexpr const char *kTitle = "WWE SmackDown vs. Raw 2009";
inline constexpr const char *kYear = "2009";
// The recompiled program only runs this exact disc release (default.xex execution info).
inline constexpr uint32_t kTitleId = 0x54510826;
inline constexpr uint32_t kMediaId = 0x7AFA4596;
inline constexpr const char *kRelease = "USA / Europe";
inline constexpr const char *kDiscSize = "7.3 GB";
// Card colours: gradient and accent (buttons, glow).
inline constexpr uint8_t kArtA[3] = {150, 24, 32};
inline constexpr uint8_t kArtB[3] = {20, 52, 128};
inline constexpr uint8_t kAccent[3] = {226, 166, 44};
inline constexpr uint8_t kAccentText[3] = {24, 18, 6};  // text on accent buttons

}  // namespace launcher::game
