// svr2009 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/input/input_system.h>
#include <rex/ui/keybinds.h>

#include <filesystem>
#include <memory>

#include "fps_counter.h"
#include "game_locator.h"
#include "settings_menu.h"
#if defined(SVR_NATIVE_RENDERER)
#include "gpu/device.h"
#include "gpu/imgui_overlay_drawer.h"
#endif
#include "frame_dump.h"
#include "frame_stats.h"
#include "image_dump.h"

REXCVAR_DECLARE(std::string, gpu_backend);
REXCVAR_DECLARE(bool, svr_fps_counter);

inline constexpr const char* kWindowTitle = "WWE SmackDown vs. Raw 2009";

class Svr2009App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Svr2009App>(new Svr2009App(ctx, "svr2009",
        PPCImageConfig));
  }

  // Render through the Xenos GPU plugin unless --gpu_plugin says otherwise.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty())
      config.gpu_plugin = "xenos";
#if defined(SVR_NATIVE_RENDERER)
    // Native renderer: the Xenos plugin's null backend keeps the game's D3D command stream moving
    // (fences, interrupts, vblank) without drawing; the hooks in src/reblue draw through plume.
    if (!config.graphics) {
      config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, "null");
      REXLOG_INFO("Native renderer: guest GPU {}", config.graphics ? "null (sync only)" : "MISSING");
    }
    return;
#endif
    // ReXApp only asks the plugin for "any" backend (D3D12 first on Windows); --gpu_backend picks one.
    const std::string& backend = REXCVAR_GET(gpu_backend);
    if (!config.graphics && !backend.empty()) {
      config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, backend);
      if (config.graphics)
        REXLOG_INFO("GPU backend: {} (from --gpu_backend)", backend);
    }
  }

#if defined(SVR_NATIVE_RENDERER)
  // The plume device draws the ImGui overlays (FPS counter etc.) at present time.
  std::unique_ptr<rex::ui::ImmediateDrawer> OnCreateImmediateDrawer() override {
    return std::make_unique<bd::gpu::ImGuiOverlayDrawer>();
  }

  void OnPreLaunchModule() override {
    if (!bd::gpu::Video::CreateHostDevice(window())) {
      REXLOG_ERROR("Native renderer: host device creation failed");
      app_context().QuitFromUIThread();
      return;
    }
    // Present runs on the guest thread and ImGui on the UI thread, so the overlay is drawn
    // synchronously on the UI thread into the frame being presented.
    bd::gpu::Video::SetOverlayDrawHook([this](plume::RenderCommandList* cmd,
                                              plume::RenderFramebuffer* fb, uint32_t w,
                                              uint32_t h) {
      if (!imgui_drawer() || !imgui_drawer()->HasDialogs())
        return;
      app_context().CallInUIThreadSynchronous([this, cmd, fb, w, h] {
        bd::gpu::ReblueUIDrawContext ctx(w, h, cmd, fb);
        imgui_drawer()->Draw(ctx);
      });
    });
  }

  void OnWindowPixelSizeChanged(uint32_t, uint32_t) override { bd::gpu::Video::RequestResize(); }
#endif

  void OnPostSetup() override {
    // Replaces the SDK's "svr2009 [rexglue-<build>]" title set during window creation.
    if (window())
      window()->SetTitle(kWindowTitle);
    frame_dumper_.Start(runtime()->graphics_system());
    // The game gets no input while the settings menu is open (or while the mouse is over an
    // overlay). Set here: the runtime (and ReXApp's own callback) exists only from now on.
    if (auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system())) {
      input->SetActiveCallback([this] {
        if (settings_menu_ && settings_menu_->BlocksGameInput())
          return false;
        return !(imgui_drawer_ && imgui_drawer_->GetIO().WantCaptureMouse);
      });
    }
  }
  void OnShutdown() override {
    rex::ui::UnregisterBind("bind_fps_counter");
    rex::ui::UnregisterBind("bind_svr_settings");
    settings_menu_.reset();
    fps_counter_.reset();
    frame_dumper_.Stop();
  }

  // Press Start 2P for the FPS counter (copied to <exe>/fonts by CMake); pixel font, so no
  // oversampling and pixel-snapped glyphs.
  void OnConfigureFonts(ImFontAtlas* atlas) override {
    auto path = rex::filesystem::GetExecutableFolder() / "fonts" / "PressStart2P-Regular.ttf";
    g_fps_counter_font = nullptr;
    if (!std::filesystem::exists(path)) {
      REXLOG_WARN("FPS counter font not found: {}", path.string());
      return;
    }
    ImFontConfig config;
    config.OversampleH = 1;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    g_fps_counter_font = atlas->AddFontFromFileTTF(path.string().c_str(), 20.0f, &config);
    // Roboto for the settings menu (sized per use; ImGui 1.92 scales fonts dynamically).
    auto menu_font = rex::filesystem::GetExecutableFolder() / "fonts" / "Roboto-Medium.ttf";
    g_menu_font = std::filesystem::exists(menu_font)
                      ? atlas->AddFontFromFileTTF(menu_font.string().c_str(), 22.0f)
                      : nullptr;
  }

  // FPS counter in the corner (on by default, F2 toggles), and the same numbers for the F3 overlay.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    imgui_drawer_ = drawer;
    if (REXCVAR_GET(svr_fps_counter))
      fps_counter_ = std::make_unique<FpsCounterDialog>(drawer);
    rex::ui::RegisterBind("bind_fps_counter", "F2", "Toggle FPS counter", [this] {
      if (fps_counter_)
        fps_counter_.reset();
      else if (imgui_drawer_)
        fps_counter_ = std::make_unique<FpsCounterDialog>(imgui_drawer_);
    });
    // In-game settings menu: F1, or Back + Start on a controller (settings_menu.h).
    SettingsMenuDialog::Hooks hooks;
    hooks.is_fullscreen = [this] { return window() && window()->IsFullscreen(); };
    hooks.set_fullscreen = [this](bool on) {
      if (window())
        window()->SetFullscreen(on);
    };
    hooks.fps_counter_on = [this] { return fps_counter_ != nullptr; };
    hooks.set_fps_counter = [this](bool on) {
      if (on && !fps_counter_ && imgui_drawer_)
        fps_counter_ = std::make_unique<FpsCounterDialog>(imgui_drawer_);
      else if (!on)
        fps_counter_.reset();
    };
    settings_menu_ = std::make_unique<SettingsMenuDialog>(
        drawer, "WWE SmackDown vs. Raw 2009",
        rex::filesystem::GetExecutableFolder() / "svr2009.toml", std::move(hooks));
    rex::ui::RegisterBind("bind_svr_settings", "F1", "Open the settings menu", [this] {
      if (settings_menu_)
        settings_menu_->Toggle();
    });
    SetGuestFrameStats([] {
      SvrFrameStats s = GetSvrFrameStats();
      rex::ui::FrameStats stats;
      stats.fps = s.fps;
      stats.frame_time_ms = s.frame_time_ms;
      stats.frame_count = s.frames;
      return stats;
    });
  }

  // Developer image dump for static analysis, only with SVR_DUMP_IMAGE set (image_dump.h).
  void OnPostLoadXexImage() override { DumpGuestImageIfRequested(runtime(), PPCImageConfig); }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // Portable layout: without --game_data_root the game is found next to the executable (an
  // "assets" folder or a disc image), or picked once with a file dialog (game_locator.h);
  // without --user_data_root, saves live in "userdata" beside the executable. A copied folder
  // runs with no arguments, also on Steam Deck through Proton. Settings: svr2009.toml there too.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto exe_dir = rex::filesystem::GetExecutableFolder();
    if (paths.game_data_root.empty())
      paths.game_data_root = svr::LocateGameData(exe_dir);
    if (REXCVAR_GET(user_data_root).empty()) {
      std::error_code ec;
      std::filesystem::create_directories(exe_dir / "userdata", ec);
      if (!ec) {
        paths.user_data_root = exe_dir / "userdata";
        if (REXCVAR_GET(cache_root).empty())
          paths.cache_root = paths.user_data_root / "cache";
      }
    }
  }

 private:
  FrameDumper frame_dumper_;
  rex::ui::ImGuiDrawer* imgui_drawer_ = nullptr;
  std::unique_ptr<FpsCounterDialog> fps_counter_;
  std::unique_ptr<SettingsMenuDialog> settings_menu_;
};
