// Small always-on FPS number in the top-left corner (toggle with F2): white Press Start 2P with a drop shadow.

#pragma once

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>

#include <cstdio>

#include "frame_stats.h"

// Set by Svr2009App::OnConfigureFonts; nullptr falls back to ImGui's default font.
inline ImFont* g_fps_counter_font = nullptr;

class FpsCounterDialog : public rex::ui::ImGuiDialog {
 public:
  explicit FpsCounterDialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    SvrFrameStats stats = GetSvrFrameStats();
    // Just the number: no panel or border, with a dark drop shadow so it reads on bright scenes.
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Always);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (g_fps_counter_font) {
      ImGui::PushFont(g_fps_counter_font);
    }
    if (ImGui::Begin("##svr_fps_counter", nullptr, kFlags)) {
      const ImVec4 color(1.0f, 1.0f, 1.0f, 1.0f);
      char text[16];
      if (stats.frames < 2) {
        std::snprintf(text, sizeof(text), "--");
      } else {
        std::snprintf(text, sizeof(text), "%.0f", stats.fps);
      }
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 2, pos.y + 2), IM_COL32(0, 0, 0, 200), text);
      ImGui::TextColored(color, "%s", text);
    }
    ImGui::End();
    if (g_fps_counter_font) {
      ImGui::PopFont();
    }
    ImGui::PopStyleVar(2);
  }
};
