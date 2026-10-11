// SvR 2008 versions of re:Blue renderer hooks into Blue Dragon-specific systems.
//
// re:Blue tunes shadows/reflections for Blue Dragon's engine (gpu/hooks/tweaks.cpp) and predicts
// and precaches pipelines from Blue Dragon's recorded pipeline list (pipeline/pso_predictor.cpp,
// pso_precache.cpp). SvR has neither yet: scales stay 1, pipelines compile when first used.

#include <string>

#include "gpu/hooks/tweaks.h"
#include "gpu/pipeline/pso_precache.h"
#include "gpu/pipeline/pso_predictor.h"

namespace bd::gpu {

f64 ShadowCoverageScale() { return 1.0; }
f32 SceneRenderScale() { return 1.0f; }

void OnModelTechniqueKnown(u32, bool) {}
void OnLoadModelCreated(u32, u32) {}
void OnLoadBegin(u32) {}
void OnLoadEnd() {}
void OnDeclRegistered(u32, u8) {}
void ReemitPredictions() {}
bool IsPairPredicted(u64, u64) { return false; }
std::string DescribePair(u64, u64) { return {}; }

// PrecacheEnabled / EnqueuePipeline / EnqueuePipelinePriority: svr_pipeline_store.cpp.
void BeginLoadCapture() {}
void EndLoadCapture() {}

}  // namespace bd::gpu
