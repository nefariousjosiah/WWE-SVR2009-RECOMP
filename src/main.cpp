// svr2009 - ReXGlue Recompiled Project

#include "generated/default/svr2009_init.h"

#include "svr2009_app.h"

// Direct3D 12 or Vulkan (graphics_api.h): if the other program should run, it's started here,
// before anything else, and this one exits when it does.
#if defined(_WIN32)
static const bool g_graphics_api_checked = [] {
  svr::HandOffGraphicsApiIfNeeded();
  return true;
}();
#endif

REXCVAR_DEFINE_STRING(gpu_backend, "", "GPU",
                      "GPU backend inside the xenos plugin: d3d12, vulkan or empty for the default");
REXCVAR_DEFINE_BOOL(svr_fps_counter, true, "Game", "Show the FPS counter at startup (F2 toggles)");
REXCVAR_DEFINE_BOOL(svr_controllers_shared, true, "Game",
                    "Every controller controls player 1 (for one player: a controller that "
                    "reconnects, or shows up twice through Steam Input / DS4Windows, can't become "
                    "player 2). Off: each controller is its own player");
REXCVAR_DEFINE_STRING(svr_graphics_api, "auto", "Game",
                      "Graphics API: auto (Direct3D 12 on Windows, Vulkan on Steam Deck / Linux), "
                      "d3d12 or vulkan. Applies the next time the game starts");
REXCVAR_DEFINE_BOOL(svr_high_priority, false, "Game",
                    "Run the game at above-normal CPU priority: fewer frame drops when other "
                    "programs compete for the processor (busy or weak PCs)");

// Laptops with two graphics chips: NVIDIA's and AMD's drivers run a program on the dedicated GPU
// when its .exe exports these (they're ignored in a DLL).
#if defined(_WIN32)
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

REX_DEFINE_APP(svr2009, Svr2009App::Create)
