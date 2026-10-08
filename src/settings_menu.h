// In-game settings menu: F1, or Back + Start on a controller. Only the options players need, applied
// at once where the game allows it and saved to the settings file next to the executable. While it
// is open the game gets no input (Svr*App routes the input system's active callback through it).

#pragma once

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#endif

// Set by the app's OnConfigureFonts; nullptr falls back to ImGui's default font.
inline ImFont* g_menu_font = nullptr;

class SettingsMenuDialog : public rex::ui::ImGuiDialog {
 public:
  struct Hooks {
    std::function<bool()> is_fullscreen;
    std::function<void(bool)> set_fullscreen;
    std::function<bool()> fps_counter_on;
    std::function<void(bool)> set_fps_counter;
  };

  SettingsMenuDialog(rex::ui::ImGuiDrawer* drawer, std::string title, std::filesystem::path toml,
                     Hooks hooks)
      : ImGuiDialog(drawer), title_(std::move(title)), toml_(std::move(toml)), hooks_(std::move(hooks)) {
#if defined(_WIN32)
    for (const char* dll : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"}) {
      if (HMODULE m = LoadLibraryA(dll)) {
        xinput_get_state_ = reinterpret_cast<XInputGetStateFn>(GetProcAddress(m, "XInputGetState"));
        if (xinput_get_state_)
          break;
      }
    }
#endif
  }

  void Toggle() { open_ ? Close() : Open(); }
  // Game input stays off while the menu is open, and until the buttons used to close it are released.
  bool BlocksGameInput() const { return open_ || hold_; }

 protected:
  void OnDraw(ImGuiIO& io) override {
    const Pad pad = ReadPad();
    if (!open_) {
      if (hold_ && !pad.any)
        hold_ = false;
      if (pad.combo && !last_.combo)
        Open();
      last_ = pad;
      if (!open_)
        return;
    }
    HandleInput(pad);
    last_ = pad;
    if (open_)
      Draw(io);
  }

 private:
  enum RowId { kResolution, kDisplay, kFrameRate, kShape, kCounter, kSound, kKeyboard, kClose, kRows };
  struct Row {
    const char* name;
    std::vector<const char*> labels;
    const char* hint;
  };
  struct Pad {
    bool up = false, down = false, left = false, right = false, a = false, b = false, combo = false,
         any = false;
  };
  using Clock = std::chrono::steady_clock;

  const std::vector<Row>& Rows() const {
    static const std::vector<Row> rows = {
        {"Resolution", {"Auto (recommended)", "720p (original)", "1440p", "4K"},
         "The game renders at this resolution and scales it to your screen: higher is smoother and "
         "sharper. Auto picks 1440p on 1080p/1440p screens, 4K on 4K screens, 720p on Steam Deck. "
         "Applies the next time you start the game."},
        {"Display", {"Fullscreen", "Window"}, "Switch between fullscreen and a window."},
        {"Frame rate", {"60 fps", "30 fps (original)"},
         "60 fps uses the game's own 60 fps mode, so everything plays at the right speed. Takes "
         "effect from the next screen or match."},
        {"Screen shape", {"16:9 (correct)", "Stretch to fill"},
         "Matters on 16:10 screens like the Steam Deck: 16:9 keeps thin bars, stretch fills the "
         "screen."},
        {"FPS counter", {"Show", "Hide"}, "The frame rate in the corner. F2 also shows or hides it."},
        {"Sound", {"On", "Muted"}, "Mute all of the game's sound."},
        {"Keyboard controls", {"On", "Off"},
         "The keyboard works as a controller (rebind keys with F4). Not tested yet: a controller is "
         "recommended."},
        {"Close", {}, "Back to the game. Your settings are saved."},
    };
    return rows;
  }

  void Open() {
    open_ = true;
    selected_ = 0;
    values_[kResolution] = std::clamp(std::atoi(Get("svr_render_scale").c_str()), 0, 3);
    values_[kDisplay] = hooks_.is_fullscreen && hooks_.is_fullscreen() ? 0 : 1;
    values_[kFrameRate] = Get("svr_60fps") == "true" ? 0 : 1;
    values_[kShape] = Get("bd_aspect_ratio") == "6" ? 1 : 0;
    values_[kCounter] = hooks_.fps_counter_on && hooks_.fps_counter_on() ? 0 : 1;
    values_[kSound] = Get("audio_mute") == "true" ? 1 : 0;
    values_[kKeyboard] = Get("mnk_mode") == "true" ? 0 : 1;
  }
  void Close() {
    open_ = false;
    hold_ = true;
  }

  static std::string Get(const char* name) { return rex::cvar::GetFlagByName(name); }

  void Apply(int row) {
    const int v = values_[row];
    switch (row) {
      case kResolution:
        Set("svr_render_scale", std::to_string(v));
        break;
      case kDisplay:
        if (hooks_.set_fullscreen)
          hooks_.set_fullscreen(v == 0);
        Save("fullscreen", v == 0 ? "true" : "false");
        break;
      case kFrameRate:
        Set("svr_60fps", v == 0 ? "true" : "false");
        break;
      case kShape:
        Set("bd_aspect_ratio", v == 1 ? "6" : "5");
        break;
      case kCounter:
        if (hooks_.set_fps_counter)
          hooks_.set_fps_counter(v == 0);
        Save("svr_fps_counter", v == 0 ? "true" : "false");
        break;
      case kSound:
        Set("audio_mute", v == 1 ? "true" : "false");
        break;
      case kKeyboard:
        Set("mnk_mode", v == 0 ? "true" : "false");
        break;
    }
  }
  void Set(const char* name, const std::string& value) {
    rex::cvar::SetFlagByName(name, value);
    Save(name, value);
  }

  // Replaces (or appends) one "key = value" line, keeping everything else in the file.
  void Save(const char* key, const std::string& value) {
    std::vector<std::string> lines;
    {
      std::ifstream in(toml_);
      std::string line;
      while (std::getline(in, line))
        lines.push_back(line);
    }
    const std::string assignment = std::string(key) + " = " + value;
    bool replaced = false;
    for (std::string& line : lines) {
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
    std::ofstream out(toml_, std::ios::trunc);
    for (const std::string& line : lines)
      out << line << "\n";
  }

  void Change(int delta) {
    if (selected_ >= kClose)
      return;
    const int count = int(Rows()[selected_].labels.size());
    values_[selected_] = (values_[selected_] + delta + count) % count;
    Apply(selected_);
  }

  // Controller state from XInput (also what Proton exposes on the Steam Deck).
  Pad ReadPad() {
    Pad p;
#if defined(_WIN32)
    if (!xinput_get_state_)
      return p;
    for (DWORD i = 0; i < 4; ++i) {
      XINPUT_STATE st{};
      if (xinput_get_state_(i, &st) != ERROR_SUCCESS)
        continue;
      const WORD b = st.Gamepad.wButtons;
      const SHORT ly = st.Gamepad.sThumbLY, lx = st.Gamepad.sThumbLX;
      constexpr SHORT kDead = 16000;
      p.up |= (b & XINPUT_GAMEPAD_DPAD_UP) || ly > kDead;
      p.down |= (b & XINPUT_GAMEPAD_DPAD_DOWN) || ly < -kDead;
      p.left |= (b & XINPUT_GAMEPAD_DPAD_LEFT) || lx < -kDead;
      p.right |= (b & XINPUT_GAMEPAD_DPAD_RIGHT) || lx > kDead;
      p.a |= (b & XINPUT_GAMEPAD_A) != 0;
      p.b |= (b & XINPUT_GAMEPAD_B) != 0;
      p.combo |= (b & XINPUT_GAMEPAD_BACK) && (b & XINPUT_GAMEPAD_START);
      p.any |= b != 0 || ly > kDead || ly < -kDead || lx > kDead || lx < -kDead;
    }
#endif
    return p;
  }

  // A held direction fires once, then repeats after a short delay.
  bool Fire(bool now, bool before, Clock::time_point& next) {
    const auto t = Clock::now();
    if (now && !before) {
      next = t + std::chrono::milliseconds(380);
      return true;
    }
    if (now && t >= next) {
      next = t + std::chrono::milliseconds(120);
      return true;
    }
    return false;
  }

  void HandleInput(const Pad& pad) {
    const bool up = Fire(pad.up, last_.up, rep_[0]) || ImGui::IsKeyPressed(ImGuiKey_UpArrow);
    const bool down = Fire(pad.down, last_.down, rep_[1]) || ImGui::IsKeyPressed(ImGuiKey_DownArrow);
    const bool left = Fire(pad.left, last_.left, rep_[2]) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow);
    const bool right = Fire(pad.right, last_.right, rep_[3]) || ImGui::IsKeyPressed(ImGuiKey_RightArrow);
    const bool accept = (pad.a && !last_.a) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_Space, false);
    const bool back = (pad.b && !last_.b) || (pad.combo && !last_.combo) ||
                      ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (up)
      selected_ = (selected_ + kRows - 1) % kRows;
    if (down)
      selected_ = (selected_ + 1) % kRows;
    if (left)
      Change(-1);
    if (right)
      Change(+1);
    if (accept) {
      if (selected_ == kClose)
        Close();
      else
        Change(+1);
    }
    if (back)
      Close();
  }

  void Draw(ImGuiIO& io) {
    const float s = std::max(0.6f, io.DisplaySize.y / 1080.0f);
    const ImU32 accent = IM_COL32(214, 38, 38, 255);
    const ImU32 text = IM_COL32(236, 238, 244, 255);
    const ImU32 dim = IM_COL32(150, 156, 172, 255);
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));

    const float width = 820 * s;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30 * s, 26 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(18, 21, 30, 245));
    ImGui::PushFont(g_menu_font, 22 * s);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                        ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##svr_settings_menu", nullptr, kFlags)) {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImGui::PushFont(g_menu_font, 34 * s);
      ImGui::TextColored(ImColor(accent), "SETTINGS");
      ImGui::PopFont();
      ImGui::PushFont(g_menu_font, 18 * s);
      ImGui::TextColored(ImColor(dim), "%s", title_.c_str());
      ImGui::PopFont();
      ImGui::Dummy(ImVec2(0, 12 * s));

      const float row_h = 48 * s;
      const float inner = ImGui::GetContentRegionAvail().x;
      const float value_x = 300 * s;
      for (int i = 0; i < kRows; ++i) {
        const Row& row = Rows()[i];
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + inner, p0.y + row_h);
        ImGui::PushID(i);
        ImGui::InvisibleButton("row", ImVec2(inner, row_h));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          selected_ = i;
          if (i == kClose)
            Close();
          else
            Change(io.MousePos.x < p0.x + value_x + (inner - value_x) * 0.5f ? -1 : +1);
        } else if (hovered && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
          selected_ = i;
        }
        ImGui::PopID();
        const bool sel = i == selected_;
        if (sel) {
          dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 16), 8 * s);
          dl->AddRectFilled(p0, ImVec2(p0.x + 5 * s, p1.y), accent, 3 * s);
        }
        const float cy = p0.y + row_h * 0.5f;
        const float fh = ImGui::GetFontSize();
        if (i == kClose) {
          const char* label = "Close";
          const float tw = ImGui::CalcTextSize(label).x;
          dl->AddText(ImVec2(p0.x + (inner - tw) * 0.5f, cy - fh * 0.5f), sel ? text : dim, label);
          continue;
        }
        dl->AddText(ImVec2(p0.x + 20 * s, cy - fh * 0.5f), sel ? text : dim, row.name);
        const char* value = row.labels[values_[i]];
        const float vw = ImGui::CalcTextSize(value).x;
        const float mid = p0.x + value_x + (inner - value_x) * 0.5f;
        dl->AddText(ImVec2(mid - vw * 0.5f, cy - fh * 0.5f), sel ? IM_COL32(255, 255, 255, 255) : text, value);
        const ImU32 arrow = sel ? accent : IM_COL32(90, 96, 112, 255);
        const float ax = 9 * s, ay = 8 * s;
        const float lx = p0.x + value_x + 10 * s, rx = p1.x - 18 * s;
        dl->AddTriangleFilled(ImVec2(lx, cy), ImVec2(lx + ax, cy - ay), ImVec2(lx + ax, cy + ay), arrow);
        dl->AddTriangleFilled(ImVec2(rx + ax, cy), ImVec2(rx, cy - ay), ImVec2(rx, cy + ay), arrow);
      }

      ImGui::Dummy(ImVec2(0, 10 * s));
      ImGui::PushFont(g_menu_font, 18 * s);
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(178, 184, 198, 255));
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
      const float hint_y = ImGui::GetCursorPosY();
      ImGui::TextWrapped("%s", Rows()[selected_].hint);
      ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), hint_y + 3 * ImGui::GetTextLineHeightWithSpacing()));
      ImGui::PopTextWrapPos();
      ImGui::PopStyleColor();
      ImGui::Dummy(ImVec2(0, 6 * s));
      ImGui::TextColored(ImColor(dim), "Up / Down  choose      Left / Right  change      B or Esc  close");
      ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
  }

#if defined(_WIN32)
  using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
  XInputGetStateFn xinput_get_state_ = nullptr;
#endif
  std::string title_;
  std::filesystem::path toml_;
  Hooks hooks_;
  bool open_ = false, hold_ = false;
  int selected_ = 0;
  int values_[kRows] = {};
  Pad last_;
  Clock::time_point rep_[4];
};
