// svr2009 - ReXGlue Recompiled Project

#include "generated/default/svr2009_init.h"

#include "svr2009_app.h"

REXCVAR_DEFINE_STRING(gpu_backend, "", "GPU",
                      "GPU backend inside the xenos plugin: d3d12, vulkan or empty for the default");
REXCVAR_DEFINE_BOOL(svr_fps_counter, true, "Game", "Show the FPS counter at startup (F2 toggles)");

REX_DEFINE_APP(svr2009, Svr2009App::Create)
