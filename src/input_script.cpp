// Dev aid: scripted controller input for unattended test runs (any renderer).
//
//   SVR_INPUT_SCRIPT="45:START,50:A,52:A,54:DOWN,60:A*10@1.5"
//
// Each entry is <seconds since the game first polled the pad>:<buttons>[*<repeat>@<interval s>],
// buttons joined with '+' (A B X Y START BACK UP DOWN LEFT RIGHT LB RB LT RT). A press is held for
// 150 ms. Presses are ORed into player 1's state (a missing controller reads as connected), so
// the game's own code sees ordinary input and nothing reaches the desktop. Without the variable
// the hook only forwards to the game's XInputGetState.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/types.h>

namespace {

struct Press {
  double at = 0;      // seconds
  uint16_t buttons = 0;
  bool lt = false;
  bool rt = false;
};

constexpr double kHoldSeconds = 0.15;

std::vector<Press> g_presses;
bool g_active = false;
std::once_flag g_parse_once;
std::chrono::steady_clock::time_point g_start;
uint32_t g_packet_bump = 0;
bool g_was_pressed = false;

uint16_t ButtonBit(const std::string &name, bool &lt, bool &rt) {
  if (name == "UP") return 0x0001;
  if (name == "DOWN") return 0x0002;
  if (name == "LEFT") return 0x0004;
  if (name == "RIGHT") return 0x0008;
  if (name == "START") return 0x0010;
  if (name == "BACK") return 0x0020;
  if (name == "LB") return 0x0100;
  if (name == "RB") return 0x0200;
  if (name == "A") return 0x1000;
  if (name == "B") return 0x2000;
  if (name == "X") return 0x4000;
  if (name == "Y") return 0x8000;
  if (name == "LT") lt = true;
  if (name == "RT") rt = true;
  return 0;
}

void Parse() {
  const char *env = std::getenv("SVR_INPUT_SCRIPT");
  if (!env || !*env)
    return;
  std::stringstream entries(env);
  std::string entry;
  while (std::getline(entries, entry, ',')) {
    const size_t colon = entry.find(':');
    if (colon == std::string::npos)
      continue;
    double at = std::atof(entry.substr(0, colon).c_str());
    std::string rest = entry.substr(colon + 1);
    int repeat = 1;
    double interval = 1.0;
    if (const size_t star = rest.find('*'); star != std::string::npos) {
      std::string count = rest.substr(star + 1);
      rest = rest.substr(0, star);
      if (const size_t atsign = count.find('@'); atsign != std::string::npos) {
        interval = std::atof(count.substr(atsign + 1).c_str());
        count = count.substr(0, atsign);
      }
      repeat = std::max(1, std::atoi(count.c_str()));
    }
    Press press;
    std::stringstream names(rest);
    std::string name;
    while (std::getline(names, name, '+'))
      press.buttons |= ButtonBit(name, press.lt, press.rt);
    for (int i = 0; i < repeat; ++i) {
      press.at = at + i * interval;
      g_presses.push_back(press);
    }
  }
  g_active = !g_presses.empty();
  g_start = std::chrono::steady_clock::now();
  REXLOG_INFO("[input script] {} presses", g_presses.size());
}

}  // namespace

// XInputGetState(dwUserIndex, pState): forwards to XamInputGetState (sub_82593558).
REX_EXTERN(__imp__sub_82593558);
REX_HOOK_RAW(sub_82593558) {
  std::call_once(g_parse_once, Parse);
  const uint32_t user = ctx.r3.u32;
  const uint32_t state_va = ctx.r4.u32;
  __imp__sub_82593558(ctx, base);
  if (!g_active || user != 0 || !state_va)
    return;

  const double now =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
  uint16_t buttons = 0;
  bool lt = false, rt = false;
  for (const Press &p : g_presses) {
    if (now >= p.at && now < p.at + kHoldSeconds) {
      buttons |= p.buttons;
      lt |= p.lt;
      rt |= p.rt;
    }
  }
  const bool pressed = buttons || lt || rt;

  // X_INPUT_STATE (big-endian): dwPacketNumber, wButtons, bLeftTrigger, bRightTrigger, thumbs.
  auto *state = base + state_va;
  if (ctx.r3.u32 != 0) {
    // No controller: present an idle one so the script still drives the game.
    std::memset(state, 0, 16);
    ctx.r3.u64 = 0;
  }
  if (pressed != g_was_pressed)
    ++g_packet_bump;
  g_was_pressed = pressed;
  auto *packet = reinterpret_cast<rex::be<uint32_t> *>(state);
  *packet = uint32_t(*packet) + g_packet_bump;
  if (pressed) {
    auto *wbuttons = reinterpret_cast<rex::be<uint16_t> *>(state + 4);
    *wbuttons = uint16_t(uint16_t(*wbuttons) | buttons);
    if (lt)
      state[6] = 0xFF;
    if (rt)
      state[7] = 0xFF;
  }
}
