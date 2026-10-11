/**
 * @file    gpu/imgui_overlay_drawer.h
 * @brief   Plume/D3D12-backed ImmediateDrawer for detached overlay rendering.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <rex/types.h>

#include <rex/ui/immediate_drawer.h>
#include <rex/ui/presenter.h>

// Plume declares these as 'struct'. Match the tag or clang-cl flags a mismatch.
namespace plume {
struct RenderCommandList;
struct RenderFramebuffer;
struct RenderTexture;
struct RenderTextureView;
struct RenderPipelineLayout;
struct RenderPipeline;
struct RenderShader;
} // namespace plume

namespace bd::gpu {

class ReblueUIDrawContext final : public rex::ui::AppUIDrawContext {
public:
  ReblueUIDrawContext(u32 rt_w, u32 rt_h, plume::RenderCommandList *cmd,
                      plume::RenderFramebuffer *fb)
      : rex::ui::AppUIDrawContext(rt_w, rt_h), cmd_(cmd), fb_(fb) {}

  plume::RenderCommandList *command_list() const { return cmd_; }
  plume::RenderFramebuffer *framebuffer() const { return fb_; }

private:
  plume::RenderCommandList *cmd_;
  plume::RenderFramebuffer *fb_;
};

class PlumeImmediateTexture final : public rex::ui::ImmediateTexture {
public:
  PlumeImmediateTexture(u32 w, u32 h, u32 tex_slot, u32 sampler_slot,
                        std::unique_ptr<plume::RenderTexture> texture,
                        std::unique_ptr<plume::RenderTextureView> view);
  ~PlumeImmediateTexture() override;

  u32 tex_slot() const { return tex_slot_; }
  u32 sampler_slot() const { return sampler_slot_; }

private:
  u32 tex_slot_;
  u32 sampler_slot_;
  std::unique_ptr<plume::RenderTexture> texture_;
  std::unique_ptr<plume::RenderTextureView> view_;
};

// Detached drawer (config.graphics == nullptr): binds the device lazily because
// it exists before reblue's device.
class ImGuiOverlayDrawer final : public rex::ui::ImmediateDrawer {
public:
  ImGuiOverlayDrawer();
  ~ImGuiOverlayDrawer() override;

  std::unique_ptr<rex::ui::ImmediateTexture>
  CreateTexture(u32 width, u32 height, rex::ui::ImmediateTextureFilter filter,
                bool is_repeated, const u8 *data) override;
  void Begin(rex::ui::UIDrawContext &ctx, float coord_w,
             float coord_h) override;
  void BeginDrawBatch(const rex::ui::ImmediateDrawBatch &batch) override;
  void Draw(const rex::ui::ImmediateDraw &draw) override;
  void EndDrawBatch() override;
  void End() override;

  // Overlay without stalling Present. Drawing ImGui directly makes the game thread wait every
  // frame for the UI thread (a few ms, more on slower PCs), so instead the UI thread records
  // ImGui's output (`draw` runs ImGuiDrawer::Draw with a context that has no command list) and
  // Present replays the latest recording, one frame old. A recording that would need a new
  // texture (the font atlas) is refused and NeedsDirectDraw() asks for one direct draw instead.
  template <typename DrawFn> void Capture(u32 rt_w, u32 rt_h, DrawFn &&draw) {
    capture_thread_.store(true, std::memory_order_relaxed);
    ReblueUIDrawContext ctx(rt_w, rt_h, nullptr, nullptr);
    draw(ctx);
    capture_thread_.store(false, std::memory_order_relaxed);
  }
  bool ReplayLatest(plume::RenderCommandList *cmd, plume::RenderFramebuffer *fb, u32 rt_w,
                    u32 rt_h);
  bool NeedsDirectDraw() const { return needs_direct_.load(std::memory_order_relaxed); }
  void ClearCapture();

private:
  struct CapturedDraw {
    u32 count, index_offset;
    i32 base_vertex;
    u32 tex_slot, sampler_slot;
    u32 left, top, width, height;  // scissor, render target pixels
  };
  struct CapturedBatch {
    std::vector<rex::ui::ImmediateVertex> vertices;
    std::vector<u16> indices;
    std::vector<CapturedDraw> draws;
  };
  struct Recording {
    u32 rt_w = 0, rt_h = 0;
    float coord_w = 0, coord_h = 0;
    std::vector<CapturedBatch> batches;
  };
  void BindOverlayState(plume::RenderCommandList *cmd, plume::RenderFramebuffer *fb, u32 rt_w,
                        u32 rt_h, float coord_w, float coord_h);

  std::atomic<bool> capture_thread_{false};  // set while Capture runs (UI thread)
  std::atomic<bool> needs_direct_{true};     // no usable recording yet
  bool capturing_ = false;                   // between Begin and End of a recording
  Recording building_;
  std::mutex recording_mutex_;
  std::shared_ptr<const Recording> latest_;

  bool TryInitDeviceResources(); // lazy GPU init, false if device not ready

  // Requires an open Present command list.
  bool UploadRGBA8Texture(u32 w, u32 h, const u8 *rgba,
                          std::unique_ptr<plume::RenderTexture> &out_tex,
                          std::unique_ptr<plume::RenderTextureView> &out_view,
                          u32 &out_slot);

  std::atomic<bool> resources_ready_{false};
  bool batch_open_ = false;
  plume::RenderCommandList *cmd_ = nullptr; // valid only between Begin/End

  std::unique_ptr<plume::RenderPipelineLayout> layout_;
  std::unique_ptr<plume::RenderPipeline> pipeline_;
  std::unique_ptr<plume::RenderShader> vs_;
  std::unique_ptr<plume::RenderShader> ps_;
  std::unique_ptr<plume::RenderTexture> white_texture_;
  std::unique_ptr<plume::RenderTextureView> white_view_;
  u32 white_slot_ = 0;
};

} // namespace bd::gpu
