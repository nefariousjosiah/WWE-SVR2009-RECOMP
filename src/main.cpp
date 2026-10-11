// svr2009 - ReXGlue Recompiled Project

#include "generated/default/svr2009_init.h"

#include "svr2009_app.h"

REXCVAR_DEFINE_STRING(gpu_backend, "", "GPU",
                      "GPU backend inside the xenos plugin: d3d12, vulkan or empty for the default");
REXCVAR_DEFINE_BOOL(svr_fps_counter, true, "Game", "Show the FPS counter at startup (F2 toggles)");
REXCVAR_DEFINE_BOOL(svr_controllers_shared, false, "Game",
                    "Every controller controls player 1 (for one player: a controller that "
                    "reconnects, or shows up twice through Steam Input / DS4Windows, can't become "
                    "player 2). Off: each controller is its own player");

REX_DEFINE_APP(svr2009, Svr2009App::Create)
