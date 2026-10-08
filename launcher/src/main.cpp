// The game's launcher: shows where the player's disc image is, checks it, keeps the settings and
// starts the game. Lives next to the game's executable. Mouse, keyboard and controller (Steam Deck
// Game Mode through Proton).

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"  // ShadeVertsLinearColorGradientKeepAlpha
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"

#include "covers.h"
#include "disc.h"
#include "game.h"

namespace {

namespace fs = std::filesystem;
using launcher::Disc;
using launcher::DiscCheck;

constexpr float kDesignHeight = 800.0f;          // layout units: the Steam Deck's screen height
constexpr float kCoverAspect = 282.0f / 346.0f;  // Xbox 360 case insert, front

ImU32 Rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
ImU32 Rgb(const uint8_t (&c)[3]) { return IM_COL32(c[0], c[1], c[2], 255); }
ImU32 Lighten(ImU32 c, float t) {
  ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
  v.x += (1 - v.x) * t;
  v.y += (1 - v.y) * t;
  v.z += (1 - v.z) * t;
  return ImGui::ColorConvertFloat4ToU32(v);
}
ImU32 WithAlpha(ImU32 c, float a) {
  return (c & ~IM_COL32_A_MASK) | (ImU32(std::clamp(a, 0.0f, 1.0f) * 255) << IM_COL32_A_SHIFT);
}
ImTextureRef Tex(SDL_Texture *t) { return ImTextureRef(ImTextureID(intptr_t(t))); }

constexpr ImU32 kSmackDownBlue = IM_COL32(47, 123, 255, 255);
constexpr ImU32 kRawRed = IM_COL32(232, 51, 60, 255);

// uv rectangle that fills a box of the given aspect from an image without stretching it.
void CoverUv(float image_aspect, float box_aspect, ImVec2 &uv0, ImVec2 &uv1) {
  uv0 = ImVec2(0, 0);
  uv1 = ImVec2(1, 1);
  if (image_aspect > box_aspect) {
    const float w = box_aspect / image_aspect;
    uv0.x = (1 - w) * 0.5f;
    uv1.x = uv0.x + w;
  } else if (image_aspect < box_aspect) {
    const float h = image_aspect / box_aspect;
    uv0.y = (1 - h) * 0.5f;
    uv1.y = uv0.y + h;
  }
}

// File dialogs answer on their own thread; the main loop picks the result up.
enum class PickKind { kDisc, kCover };
struct Picked {
  PickKind kind;
  std::string path;
};
std::mutex g_pick_mutex;
std::vector<Picked> g_picked;

void SDLCALL OnPicked(void *userdata, const char *const *files, int) {
  const auto kind = static_cast<PickKind>(reinterpret_cast<intptr_t>(userdata));
  if (files && files[0]) {
    std::lock_guard lock(g_pick_mutex);
    g_picked.push_back({kind, files[0]});
  }
}

bool OnSteamDeck() {
  const char *v = SDL_getenv("SteamDeck");
  return v && std::strcmp(v, "1") == 0;
}

const char *kScaleNames[] = {"Auto (recommended)", "720p (original)", "1440p", "4K"};

struct App {
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  fs::path dir;  // the game's folder (this launcher's folder)
  Disc disc;
  launcher::Cover cover;
  launcher::Runner runner;
  bool running = false;
  bool focus_primary = true;
  bool open_settings = false;
  launcher::Settings settings;
  std::string message;
  double message_until = 0;
  float glow = 0;

  fs::path Exe() const { return dir / (std::string(launcher::game::kId) + ".exe"); }
  fs::path SettingsFile() const { return dir / (std::string(launcher::game::kId) + ".toml"); }

  void Say(const std::string &text) {
    message = text;
    message_until = ImGui::GetTime() + 6.0;
  }

  void UseDisc(const fs::path &path) {
    Disc d = launcher::CheckDisc(path);
    if (d.check == DiscCheck::kReady) {
      launcher::RememberDisc(dir, path);
      Say("Your disc image is set.");
    } else {
      Say("That file can't be used: see below.");
    }
    disc = d;
    focus_primary = true;
  }

  void UseCover(const fs::path &image) {
    std::string error;
    if (!launcher::SetCover(dir, image, error)) {
      Say(error);
      return;
    }
    launcher::LoadCover(renderer, dir, cover);
    Say(cover.image ? "Box art set." : "That image couldn't be read.");
  }
};

void OpenPicker(App &app, PickKind kind) {
  static const SDL_DialogFileFilter disc_filters[] = {
      {"Xbox 360 disc image (*.iso)", "iso;xiso"},
      {"Extracted game (default.xex)", "xex"},
      {"All files", "*"},
  };
  static const SDL_DialogFileFilter image_filters[] = {{"Images (PNG, JPEG, BMP)", "png;jpg;jpeg;bmp"}};
  const bool cover = kind == PickKind::kCover;
  SDL_ShowOpenFileDialog(OnPicked, reinterpret_cast<void *>(intptr_t(kind)), app.window,
                         cover ? image_filters : disc_filters, cover ? 1 : 3,
                         launcher::ToUtf8(app.dir).c_str(), false);
}

void TextCentered(ImDrawList *dl, ImVec2 center, float size, ImU32 color, const char *text) {
  const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
  dl->AddText(ImGui::GetFont(), size, ImVec2(center.x - extent.x * 0.5f, center.y - extent.y * 0.5f),
              color, text);
}

std::string Fit(const std::string &text, float size, float width) {
  ImFont *font = ImGui::GetFont();
  if (font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str()).x <= width)
    return text;
  std::string cut = text;
  while (!cut.empty() && font->CalcTextSizeA(size, FLT_MAX, 0, ("..." + cut).c_str()).x > width)
    cut.erase(0, 1);  // keep the end of a path: the file name
  return "..." + cut;
}

bool StyledButton(const char *label, ImVec2 size, ImU32 color, ImU32 hover, ImU32 text = 0) {
  ImGui::PushStyleColor(ImGuiCol_Button, color);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, hover);
  if (text)
    ImGui::PushStyleColor(ImGuiCol_Text, text);
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(text ? 4 : 3);
  return pressed;
}

void DrawCover(App &app, ImVec2 p0, ImVec2 p1, float s) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const bool ready = app.disc.check == DiscCheck::kReady;
  const ImU32 accent = Rgb(launcher::game::kAccent);
  const float rounding = 14 * s;
  app.glow += ((ready ? 1.0f : 0.0f) - app.glow) * std::min(1.0f, ImGui::GetIO().DeltaTime * 6.0f);
  for (int i = 3; i >= 1; --i) {
    const float spread = i * 7 * s;
    dl->AddRectFilled(ImVec2(p0.x - spread, p0.y - spread + 16 * s), ImVec2(p1.x + spread, p1.y + spread + 16 * s),
                      Rgb(0, 0, 0, 34), rounding + spread);
  }
  for (int i = 3; i >= 1 && app.glow > 0.01f; --i)
    dl->AddRect(ImVec2(p0.x - i * 3 * s, p0.y - i * 3 * s), ImVec2(p1.x + i * 3 * s, p1.y + i * 3 * s),
                WithAlpha(accent, 0.22f * app.glow * (4 - i)), rounding + i * 3 * s, 0, 3 * s);
  if (app.cover.image) {
    ImVec2 uv0, uv1;
    CoverUv(float(app.cover.width) / float(app.cover.height), (p1.x - p0.x) / (p1.y - p0.y), uv0, uv1);
    dl->AddImageRounded(Tex(app.cover.image), p0, p1, uv0, uv1, ready ? IM_COL32_WHITE : Rgb(120, 120, 126),
                        rounding);
  } else {
    // A coloured stand-in until the player adds box art.
    const int vtx_begin = dl->VtxBuffer.Size;
    dl->AddRectFilled(p0, p1, IM_COL32_WHITE, rounding);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, vtx_begin, dl->VtxBuffer.Size, p0, p1,
                                                  Rgb(launcher::game::kArtA), Rgb(launcher::game::kArtB));
    const float cx = (p0.x + p1.x) * 0.5f, cy = p0.y + (p1.y - p0.y) * 0.4f;
    TextCentered(dl, ImVec2(cx, cy - 66 * s), 24 * s, Rgb(255, 255, 255, 225), "SMACKDOWN");
    TextCentered(dl, ImVec2(cx, cy - 38 * s), 17 * s, Rgb(255, 255, 255, 205), "VS.  RAW");
    TextCentered(dl, ImVec2(cx, cy + 22 * s), 104 * s, Rgb(255, 255, 255), launcher::game::kYear);
    TextCentered(dl, ImVec2(cx, p1.y - 34 * s), 14 * s, Rgb(255, 255, 255, 150),
                 "Drop your box art here");
  }
  dl->AddRect(p0, p1, ready ? WithAlpha(Lighten(accent, 0.2f), 0.6f) : Rgb(255, 255, 255, 30), rounding, 0,
              1.5f * s);
}

void DrawSettings(App &app, float s) {
  ImGui::SetNextWindowSize(ImVec2(580 * s, 0));
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("Settings", nullptr,
                              ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar))
    return;
  ImGui::PushFont(nullptr, 24);
  ImGui::TextUnformatted("Settings");
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, 6 * s));

  bool changed = false;
  ImGui::TextDisabled("Resolution");
  if (ImGui::IsWindowAppearing())
    ImGui::SetKeyboardFocusHere();
  for (int k = 0; k < 4; ++k) {
    changed |= ImGui::RadioButton(kScaleNames[k], &app.settings.render_scale, k);
    if (k == 0 || k == 2)
      ImGui::SameLine(270 * s);
  }
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("Auto renders at 1440p on 1080p/1440p screens, 4K on 4K screens and 720p on Steam "
                      "Deck. Higher looks sharper and needs a stronger graphics card.");
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0, 4 * s));
  ImGui::TextDisabled("Display");
  int display = app.settings.fullscreen ? 0 : 1;
  changed |= ImGui::RadioButton("Fullscreen", &display, 0);
  ImGui::SameLine(270 * s);
  changed |= ImGui::RadioButton("Window", &display, 1);
  app.settings.fullscreen = display == 0;
  ImGui::TextDisabled("Frame rate");
  int fps = app.settings.fps60 ? 0 : 1;
  changed |= ImGui::RadioButton("60 fps", &fps, 0);
  ImGui::SameLine(270 * s);
  changed |= ImGui::RadioButton("30 fps (original)", &fps, 1);
  app.settings.fps60 = fps == 0;
  ImGui::TextDisabled("Screen shape");
  int shape = app.settings.stretch ? 1 : 0;
  changed |= ImGui::RadioButton("16:9 (correct)", &shape, 0);
  ImGui::SameLine(270 * s);
  changed |= ImGui::RadioButton("Stretch to fill", &shape, 1);
  app.settings.stretch = shape == 1;
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("On 16:10 screens like the Steam Deck, 16:9 shows thin bars above and below; "
                      "stretching fills the screen but makes everything a little taller.");
  ImGui::PopTextWrapPos();
  if (changed)
    launcher::SaveSettings(app.SettingsFile(), app.settings);

  ImGui::Separator();
  ImGui::TextDisabled("Box art");
  ImGui::TextUnformatted(app.cover.image ? "Your image" : "None: drop an image onto the window");
  if (ImGui::Button("Choose image..."))
    OpenPicker(app, PickKind::kCover);
  if (app.cover.image) {
    ImGui::SameLine();
    if (ImGui::Button("Remove")) {
      launcher::RemoveCover(app.dir);
      launcher::FreeCover(app.cover);
    }
  }

  ImGui::Dummy(ImVec2(0, 10 * s));
  if (ImGui::Button("Done", ImVec2(-FLT_MIN, 48 * s)) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    app.focus_primary = true;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void DrawFrame(App &app) {
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  const float s = vp->Size.y / kDesignHeight;
  const ImVec2 a = vp->Pos, b(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
  const bool ready = app.disc.check == DiscCheck::kReady;
  const ImU32 accent = Rgb(launcher::game::kAccent);

  // Background: the box art, blurred and dimmed.
  ImDrawList *bg = ImGui::GetBackgroundDrawList();
  bg->AddRectFilledMultiColor(a, b, Rgb(9, 11, 17), Rgb(9, 11, 17), Rgb(16, 20, 31), Rgb(16, 20, 31));
  if (app.cover.blurred) {
    ImVec2 uv0, uv1;
    CoverUv(float(app.cover.width) / float(app.cover.height), vp->Size.x / vp->Size.y, uv0, uv1);
    bg->AddImage(Tex(app.cover.blurred), a, b, uv0, uv1, Rgb(255, 255, 255, 92));
  }
  bg->AddRectFilledMultiColor(ImVec2(a.x, a.y + vp->Size.y * 0.45f), b, Rgb(6, 8, 12, 0), Rgb(6, 8, 12, 0),
                              Rgb(6, 8, 12, 235), Rgb(6, 8, 12, 235));

  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::Begin("##launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
  const ImVec2 origin = ImGui::GetWindowPos();
  const float margin = 64 * s;

  // Box art on the left.
  const float cover_top = 72 * s;
  const float cover_h = std::min(vp->Size.y - cover_top - 96 * s, (vp->Size.x * 0.42f) / kCoverAspect);
  const float cover_w = cover_h * kCoverAspect;
  const ImVec2 c0(origin.x + margin, origin.y + cover_top);
  DrawCover(app, c0, ImVec2(c0.x + cover_w, c0.y + cover_h), s);
  if (ImGui::IsMouseHoveringRect(c0, ImVec2(c0.x + cover_w, c0.y + cover_h)) &&
      ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsPopupOpen("Settings"))
    OpenPicker(app, PickKind::kCover);

  // Everything else on the right.
  const float x = margin + cover_w + 64 * s;
  const float w = vp->Size.x - x - margin;
  float y = cover_top + 8 * s;
  ImGui::SetCursorPos(ImVec2(x, y));
  ImGui::PushFont(nullptr, 26);
  ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kSmackDownBlue), "SMACKDOWN");
  ImGui::SameLine(0, 10 * s);
  ImGui::TextColored(ImVec4(0.92f, 0.93f, 0.96f, 1), "vs.");
  ImGui::SameLine(0, 10 * s);
  ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kRawRed), "RAW");
  ImGui::SameLine(0, 12 * s);
  ImGui::TextColored(ImVec4(0.92f, 0.93f, 0.96f, 1), "%s", launcher::game::kYear);
  ImGui::PopFont();
  y += 44 * s;
  ImGui::SetCursorPos(ImVec2(x, y));
  ImGui::PushFont(nullptr, 38);
  ImGui::TextUnformatted(launcher::game::kTitle);
  ImGui::PopFont();
  y += 52 * s;
  ImGui::SetCursorPos(ImVec2(x, y));
  ImGui::TextColored(ImVec4(0.66f, 0.7f, 0.78f, 1), "Native PC and Steam Deck version  \xC2\xB7  60 fps  \xC2\xB7  up to 4K");
  y += 70 * s;

  // "Your game": where the disc image is and whether it's the right one.
  ImGui::SetCursorPos(ImVec2(x, y));
  ImGui::TextDisabled("YOUR GAME");
  y += 30 * s;
  ImU32 dot = Rgb(138, 147, 166);
  std::string status, detail;
  char ids[64];
  switch (app.disc.check) {
  case DiscCheck::kReady:
    dot = Rgb(61, 220, 132);
    status = "Ready to play";
    detail = launcher::ToUtf8(app.disc.path);
    break;
  case DiscCheck::kWrongRelease:
    dot = Rgb(255, 176, 32);
    status = "Different disc release";
    std::snprintf(ids, sizeof(ids), "%08X", app.disc.media_id);
    detail = std::string("This version runs the ") + launcher::game::kRelease + " disc; yours has media ID " + ids + ".";
    break;
  case DiscCheck::kOtherGame:
    dot = Rgb(255, 176, 32);
    status = "That's a different game";
    std::snprintf(ids, sizeof(ids), "%08X", app.disc.title_id);
    detail = std::string("Its title ID is ") + ids + "; choose your " + launcher::game::kTitle + " disc image.";
    break;
  case DiscCheck::kUnreadable:
    if (app.disc.path.empty()) {
      status = "Choose your disc image";
      detail = std::string("Your own ") + launcher::game::kTitle + " disc image (.iso, " + launcher::game::kRelease +
               ", about " + launcher::game::kDiscSize + "). Nothing is downloaded.";
    } else {
      dot = Rgb(255, 120, 90);
      status = "That file can't be used";
      detail = launcher::ToUtf8(app.disc.path.filename()) + ": " + app.disc.error + ".";
    }
    break;
  }
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddCircleFilled(ImVec2(origin.x + x + 8 * s, origin.y + y + 13 * s), 7 * s, dot);
  ImGui::SetCursorPos(ImVec2(x + 26 * s, y));
  ImGui::PushFont(nullptr, 24);
  ImGui::TextUnformatted(status.c_str());
  ImGui::PopFont();
  y += 36 * s;
  ImGui::SetCursorPos(ImVec2(x, y));
  ImGui::PushFont(nullptr, 15);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.66f, 0.74f, 1));
  if (app.disc.check == DiscCheck::kReady) {
    ImGui::TextUnformatted(Fit(detail, 15 * s, w).c_str());
  } else {
    ImGui::PushTextWrapPos(x + w);
    ImGui::TextUnformatted(detail.c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::PopStyleColor();
  ImGui::PopFont();
  y += 74 * s;

  // Buttons.
  const float bh = 60 * s;
  const float bw = std::min(w, 520 * s);
  ImGui::SetCursorPos(ImVec2(x, y));
  if (app.focus_primary) {
    ImGui::SetKeyboardFocusHere();
    app.focus_primary = false;
  }
  ImGui::PushFont(nullptr, 24);
  if (ready) {
    if (StyledButton("Play", ImVec2(bw, bh), accent, Lighten(accent, 0.25f), Rgb(launcher::game::kAccentText))) {
      std::string error;
      if (app.runner.Start(app.Exe(), app.disc.path, error)) {
        app.running = true;
        SDL_HideWindow(app.window);
      } else {
        app.Say(error);
      }
    }
  } else if (StyledButton("Choose disc image...", ImVec2(bw, bh), accent, Lighten(accent, 0.25f), Rgb(launcher::game::kAccentText))) {
    OpenPicker(app, PickKind::kDisc);
  }
  ImGui::PopFont();
  y += bh + 12 * s;
  ImGui::SetCursorPos(ImVec2(x, y));
  const float half = (bw - 12 * s) * 0.5f;
  ImGui::PushFont(nullptr, 19);
  if (StyledButton(ready ? "Change disc image..." : "Look again", ImVec2(half, bh * 0.82f), Rgb(40, 46, 62, 235),
                   Rgb(58, 66, 88))) {
    if (ready) {
      OpenPicker(app, PickKind::kDisc);
    } else {
      app.disc = launcher::FindDisc(app.dir);
      app.Say(app.disc.check == DiscCheck::kReady ? "Found it." : "Still no disc image next to the game.");
    }
  }
  ImGui::SameLine(0, 12 * s);
  if (StyledButton("Settings", ImVec2(half, bh * 0.82f), Rgb(40, 46, 62, 235), Rgb(58, 66, 88))) {
    app.settings = launcher::LoadSettings(app.SettingsFile());
    app.open_settings = true;
  }
  ImGui::PopFont();
  y += bh * 0.82f + 18 * s;
  ImGui::SetCursorPos(ImVec2(x, y));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.54f, 0.62f, 1));
  ImGui::PushTextWrapPos(x + w);
  ImGui::TextUnformatted("Tip: you can also drop your disc image, or an image of the box art, onto this window.");
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();

  if (app.open_settings) {
    ImGui::OpenPopup("Settings");
    app.open_settings = false;
  }
  DrawSettings(app, s);

  // Footer: ownership note, controls, last message.
  const float fy = vp->Size.y - 64 * s;
  ImGui::SetCursorPos(ImVec2(margin, fy));
  ImGui::PushFont(nullptr, 15);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.59f, 0.67f, 1));
  ImGui::TextUnformatted("You need your own copy of the game: none of the disc's files are included or downloaded. This project does not condone piracy.");
  ImGui::PopStyleColor();
  ImGui::SetCursorPos(ImVec2(vp->Size.x - margin - 190 * s, fy + 24 * s));
  ImGui::TextDisabled("A  Select      B  Back");
  if (ImGui::GetTime() < app.message_until) {
    ImGui::SetCursorPos(ImVec2(margin, fy + 24 * s));
    ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.4f, 1), "%s", app.message.c_str());
  }
  ImGui::PopFont();
  ImGui::End();
}

void ApplyStyle(float s) {
  static ImGuiStyle base = [] {
    ImGuiStyle st;
    ImGui::StyleColorsDark(&st);
    st.FrameRounding = 10;
    st.WindowRounding = 14;
    st.PopupRounding = 14;
    st.FramePadding = ImVec2(14, 10);
    st.ItemSpacing = ImVec2(12, 10);
    st.WindowPadding = ImVec2(28, 24);
    st.FontSizeBase = 18;
    st.Colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.11f, 0.15f, 0.98f);
    st.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
    st.Colors[ImGuiCol_CheckMark] = ImVec4(0.89f, 0.65f, 0.17f, 1);
    st.Colors[ImGuiCol_FrameBg] = ImVec4(0.2f, 0.23f, 0.31f, 1);
    st.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.27f, 0.31f, 0.41f, 1);
    st.Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.23f, 0.31f, 1);
    st.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.32f, 0.43f, 1);
    st.Colors[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.37f, 0.5f, 1);
    st.Colors[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.08f);
    st.Colors[ImGuiCol_NavCursor] = ImVec4(1, 1, 1, 0.9f);
    return st;
  }();
  ImGuiStyle &st = ImGui::GetStyle();
  st = base;
  st.ScaleAllSizes(s);
  st.FontScaleMain = s;
}

}  // namespace

int main(int, char **) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, launcher::game::kTitle, SDL_GetError(), nullptr);
    return 1;
  }
  SDL_Window *window = SDL_CreateWindow(launcher::game::kTitle, 1280, 800,
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
  SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
  if (!renderer) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, launcher::game::kTitle, SDL_GetError(), nullptr);
    return 1;
  }
  SDL_SetRenderVSync(renderer, 1);
  SDL_SetWindowMinimumSize(window, 1000, 620);
  if (OnSteamDeck())
    SDL_SetWindowFullscreen(window, true);
  SDL_ShowWindow(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  App app;
  app.window = window;
  app.renderer = renderer;
  const char *base = SDL_GetBasePath();
  app.dir = base ? launcher::FromUtf8(base) : fs::current_path();
  const fs::path font = app.dir / "fonts" / "Roboto-Medium.ttf";
  std::error_code ec;
  if (fs::exists(font, ec))
    io.Fonts->AddFontFromFileTTF(launcher::ToUtf8(font).c_str());
  else
    io.Fonts->AddFontDefault();
  app.disc = launcher::FindDisc(app.dir);
  app.settings = launcher::LoadSettings(app.SettingsFile());
  launcher::LoadCover(renderer, app.dir, app.cover);
  if (!fs::exists(app.Exe(), ec))
    app.Say(std::string(launcher::game::kId) + ".exe is missing from this folder: extract the whole download again.");

  bool quit = false;
  float last_scale = 0;
  while (!quit) {
    // While the game runs the launcher stays hidden and only waits for it to end.
    if (app.running) {
      if (app.runner.Running()) {
        SDL_Event ev;
        if (SDL_WaitEventTimeout(&ev, 250) && ev.type == SDL_EVENT_QUIT)
          quit = true;
        continue;
      }
      app.running = false;
      SDL_ShowWindow(window);
      SDL_RaiseWindow(window);
      app.focus_primary = true;
    }

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL3_ProcessEvent(&ev);
      if (ev.type == SDL_EVENT_QUIT ||
          (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && ev.window.windowID == SDL_GetWindowID(window)))
        quit = true;
      if (ev.type == SDL_EVENT_DROP_FILE && ev.drop.data) {
        const fs::path dropped = launcher::FromUtf8(ev.drop.data);
        if (launcher::IsImageFile(dropped))
          app.UseCover(dropped);
        else
          app.UseDisc(dropped);
      }
    }
    {
      std::vector<Picked> picked;
      {
        std::lock_guard lock(g_pick_mutex);
        picked.swap(g_picked);
      }
      for (const Picked &p : picked) {
        if (p.kind == PickKind::kCover)
          app.UseCover(launcher::FromUtf8(p.path));
        else
          app.UseDisc(launcher::FromUtf8(p.path));
      }
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(50);
      continue;
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    const float s = std::max(0.6f, ImGui::GetMainViewport()->Size.y / kDesignHeight);
    if (s != last_scale) {
      ApplyStyle(s);
      last_scale = s;
    }
    ImGui::NewFrame();
    DrawFrame(app);
    ImGui::Render();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(renderer, 9, 11, 17, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
  }

  launcher::FreeCover(app.cover);
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
