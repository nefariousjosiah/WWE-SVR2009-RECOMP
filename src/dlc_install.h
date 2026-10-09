// Downloadable content: Xbox 360 add-on packages (STFS files, as copied off a console's hard drive)
// placed anywhere under a "dlc" folder next to the executable are installed once at startup into
// the runtime's content store (userdata/0000000000000000/<title id>/00000002/), where the game
// finds them exactly as it would on the console. Players supply their own packages.
//
// An install that can't complete (a game folder so deep that the DLC's files would pass Windows'
// 260-character path limit, a full disk) is removed again: the game must never see half-installed
// DLC (opening one crashed it). The player gets a message saying what to do.

#pragma once

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xcontent.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace svr {

// Header fields of an STFS package (big-endian): magic at 0, content type at 0x344, title at 0x360.
inline bool IsDlcPackageFor(const std::filesystem::path& file, uint32_t title_id) {
  std::ifstream in(file, std::ios::binary);
  unsigned char h[0x364] = {};
  if (!in.read(reinterpret_cast<char*>(h), sizeof(h)))
    return false;
  const std::string magic(reinterpret_cast<char*>(h), 4);
  if (magic != "LIVE" && magic != "PIRS" && magic != "CON ")
    return false;
  auto be32 = [&](size_t o) {
    return uint32_t(h[o]) << 24 | uint32_t(h[o + 1]) << 16 | uint32_t(h[o + 2]) << 8 | h[o + 3];
  };
  return be32(0x344) == uint32_t(rex::system::XContentType::kMarketplaceContent) &&
         be32(0x360) == title_id;
}

inline void DlcMessage(const std::string& text) {
  REXLOG_ERROR("[dlc] {}", text);
#if defined(_WIN32)
  MessageBoxA(nullptr, text.c_str(), "WWE SmackDown vs. Raw - DLC", MB_OK | MB_ICONWARNING);
#endif
}

inline void InstallDlcPackages(rex::system::KernelState* kernel, const std::filesystem::path& dlc_dir,
                               const std::filesystem::path& user_data_root) {
  std::error_code ec;
  if (!kernel || !std::filesystem::is_directory(dlc_dir, ec))
    return;
  auto* content = kernel->content_manager();
  const uint32_t title_id = kernel->title_id();
  if (!content || !title_id)
    return;
  const auto kind = rex::system::XContentType::kMarketplaceContent;
  const uint32_t hdd = uint32_t(rex::system::xam::DummyDeviceId::HDD);

  std::set<std::string> installed;
  for (const auto& data : content->ListContent(hdd, 0, kind))
    installed.insert(data.file_name());

  for (auto it = std::filesystem::recursive_directory_iterator(dlc_dir, ec);
       !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (!it->is_regular_file(ec) || !IsDlcPackageFor(it->path(), title_id))
      continue;
    const std::string name = it->path().filename().string();
    if (installed.count(name))
      continue;

    // <userdata>\0000000000000000\<title>\00000002\<package>\ plus the deepest file inside the
    // package (about 70 characters for SvR 2009's packs) must stay under MAX_PATH.
    const size_t install_dir_len = user_data_root.wstring().size() + 1 + 16 + 1 + 8 + 1 + 8 + 1 + name.size();
    constexpr size_t kDeepestFileInPackage = 80;
    if (install_dir_len + kDeepestFileInPackage >= 260) {
      DlcMessage("The game's folder is too deep for its DLC: Windows limits file paths to 260 "
                 "characters. Move the game folder somewhere shorter, for example "
                 "C:\\Games\\SVR2009-NATIVE, and start it again. The game runs without the DLC "
                 "for now.");
      return;
    }

    REXLOG_INFO("[dlc] installing {} (first start with this package; this can take a minute)", name);
    const auto result = content->InstallContent(it->path());
    if (result == 0) {
      REXLOG_INFO("[dlc] installed {}", name);
      continue;
    }
    // Remove whatever was unpacked, so the game never opens a half-installed package.
    for (const auto& data : content->ListContent(hdd, 0, kind))
      if (data.file_name() == name)
        content->DeleteContent(0, data);
    DlcMessage("Couldn't install the DLC package " + name + " (error " + std::to_string(result) +
               "). Check there is enough free disk space (about 1.1 GB for both Roster Update "
               "packs) and that the game folder isn't read-only. The game runs without it for now.");
  }
}

}  // namespace svr
