// Which graphics API the game draws with: Direct3D 12 or Vulkan.
//
// The renderer picks its backend when it's compiled, so a release has two programs side by side:
// svr2009.exe (Direct3D 12) and svr2009_vulkan.exe (Vulkan). Players always start svr2009.exe.
// Before anything else runs, it checks the Graphics API setting (svr_graphics_api in
// svr2009.toml, or --svr_graphics_api=) and, if the other program should run, starts that one
// and waits for it, so Steam and other launchers still see one game running until it closes.
//   auto (default): Direct3D 12 on Windows, Vulkan under Proton / Wine (Steam Deck, Linux),
//                   which runs Vulkan natively and would otherwise translate Direct3D 12.
//   d3d12 / vulkan: that one.
// The Vulkan program does the same check the other way, so the setting works from either.
#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

namespace svr {

inline constexpr const wchar_t* kD3D12Exe = L"svr2009.exe";
inline constexpr const wchar_t* kVulkanExe = L"svr2009_vulkan.exe";
// Set in the program started by the hand-off, so it never hands off again.
inline constexpr const wchar_t* kHandOffEnv = L"SVR_GRAPHICS_API_HANDOFF";

#if defined(REBLUE_D3D12)
inline constexpr bool kThisIsD3D12 = true;
#else
inline constexpr bool kThisIsD3D12 = false;
#endif

inline bool RunningUnderWine() {
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  return ntdll && GetProcAddress(ntdll, "wine_get_version");
}

inline std::filesystem::path ExeFolder() {
  wchar_t path[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
  return std::filesystem::path(std::wstring(path, n)).parent_path();
}

inline std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}

// "auto", "d3d12" or "vulkan": the command line wins over svr2009.toml.
inline std::string GraphicsApiSetting() {
  const std::wstring cmd = GetCommandLineW();
  const std::wstring flag = L"--svr_graphics_api=";
  if (const size_t at = cmd.find(flag); at != std::wstring::npos) {
    std::string v;
    for (size_t i = at + flag.size(); i < cmd.size() && cmd[i] != L' ' && cmd[i] != L'"'; ++i)
      v += char(cmd[i]);
    return Lower(v);
  }
  std::ifstream in(ExeFolder() / "svr2009.toml");
  std::string line;
  while (std::getline(in, line)) {
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos || line.compare(start, 16, "svr_graphics_api") != 0)
      continue;
    const size_t eq = line.find('=', start);
    if (eq == std::string::npos)
      continue;
    std::string v;
    for (char c : line.substr(eq + 1))
      if (std::isalnum(static_cast<unsigned char>(c)))
        v += c;
    return Lower(v);
  }
  return "auto";
}

inline bool WantsVulkan(const std::string& setting) {
  if (setting == "vulkan")
    return true;
  if (setting == "d3d12")
    return false;
  return RunningUnderWine();
}

// Starts the other program with this one's arguments. wait: until it exits (exit_code is its
// exit code); otherwise returns as soon as it started.
inline bool StartOtherGraphicsApi(bool wait, DWORD& exit_code) {
  const std::filesystem::path other = ExeFolder() / (kThisIsD3D12 ? kVulkanExe : kD3D12Exe);
  std::error_code ec;
  if (!std::filesystem::exists(other, ec))
    return false;
  // This program's arguments, without its own path (quoted or not).
  const std::wstring cmd = GetCommandLineW();
  size_t args = 0;
  if (!cmd.empty() && cmd[0] == L'"') {
    args = cmd.find(L'"', 1);
    args = args == std::wstring::npos ? cmd.size() : args + 1;
  } else {
    args = cmd.find(L' ');
    if (args == std::wstring::npos)
      args = cmd.size();
  }
  std::wstring line = L"\"" + other.wstring() + L"\"" + cmd.substr(args);
  SetEnvironmentVariableW(kHandOffEnv, L"1");
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  const std::wstring dir = ExeFolder().wstring();
  if (!CreateProcessW(other.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(),
                      &si, &pi)) {
    SetEnvironmentVariableW(kHandOffEnv, nullptr);
    return false;
  }
  CloseHandle(pi.hThread);
  if (wait) {
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
  }
  CloseHandle(pi.hProcess);
  return true;
}

// True in a program the other one started.
inline bool& StartedByOtherProgram() {
  static bool started = false;
  return started;
}

// Called before main: hands the game to the other program when the setting (or Proton) says so.
inline void HandOffGraphicsApiIfNeeded() {
  if (GetEnvironmentVariableW(kHandOffEnv, nullptr, 0)) {
    // Cleared, so a program this one starts later (the updater's restart) decides again.
    StartedByOtherProgram() = true;
    SetEnvironmentVariableW(kHandOffEnv, nullptr);
    return;
  }
  if (WantsVulkan(GraphicsApiSetting()) != kThisIsD3D12)
    return;
  DWORD code = 0;
  if (StartOtherGraphicsApi(true, code))
    ExitProcess(code);
}

// For the log: what this program draws with, and why it's this one.
inline std::string GraphicsApiDescription() {
  const std::string why = StartedByOtherProgram() ? ", started by the other program" : "";
  return std::string(kThisIsD3D12 ? "Direct3D 12" : "Vulkan") + " (setting " +
         GraphicsApiSetting() + (RunningUnderWine() ? ", Proton/Wine" : ", Windows") + why + ")";
}

}  // namespace svr
#endif  // _WIN32
