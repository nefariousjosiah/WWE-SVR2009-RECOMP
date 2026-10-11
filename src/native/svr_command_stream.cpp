// Threaded command recording (svr_command_stream.h).
//
// One stream for all frame slots (only one command list is open at a time): a ring of 1 MiB
// blocks the recording thread appends commands to and the worker replays from. Positions only
// grow; a command never straddles a block (a NextBlock marker skips the rest of one). The
// recording side holds the renderer's mutex like every other recorder, so it is one producer at a
// time. It publishes what it wrote at each draw (and whenever it waits), so the worker follows a
// draw or so behind; end() publishes the rest and waits for the worker to finish.

#include "svr_command_stream.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <thread>
#include <type_traits>

#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

#include <rex/cvar.h>

#include "core/logging.h"

REXCVAR_DEFINE_BOOL(svr_threaded_recording, true, "SvR",
                    "Record graphics commands on a separate thread (takes the graphics driver's "
                    "work off the game's thread)");

namespace bd::gpu {
namespace {

using namespace plume;

constexpr size_t kBlockSize = size_t(1) << 20;
constexpr size_t kBlockCount = 64;
constexpr size_t kCapacity = kBlockSize * kBlockCount;

enum class Op : u16 {
  kNextBlock,
  kBegin,
  kEnd,
  kBarriers,
  kDispatch,
  kDrawInstanced,
  kDrawIndexedInstanced,
  kSetPipeline,
  kSetComputePipelineLayout,
  kSetComputePushConstants,
  kSetComputeDescriptorSet,
  kSetGraphicsPipelineLayout,
  kSetGraphicsPushConstants,
  kSetGraphicsDescriptorSet,
  kSetGraphicsRootDescriptor,
  kSetIndexBuffer,
  kSetVertexBuffers,
  kSetViewports,
  kSetScissors,
  kSetFramebuffer,
  kSetDepthBias,
  kClearColor,
  kClearDepthStencil,
  kCopyBufferRegion,
  kCopyTextureRegion,
  kCopyBuffer,
  kCopyTexture,
  kResolveTexture,
  kResolveTextureRegion,
  kDiscardTexture,
  kResetQueryPool,
  kWriteTimestamp,
};

struct Header {
  Op op;
  u16 pad;
  u32 size;  // bytes including this header, a multiple of 8
};
static_assert(sizeof(Header) == 8);

template <typename T> constexpr size_t Align8(T n) { return (size_t(n) + 7) & ~size_t(7); }

class Stream;
Stream &stream();

class Stream {
 public:
  // Space for one command with `payload` bytes after the header; filled by the caller before
  // Commit(). Returns null when the command is too big for a block (the caller runs it directly).
  u8 *Reserve(Op op, size_t payload) {
    const size_t size = Align8(sizeof(Header) + payload);
    if (size > kBlockSize / 2)
      return nullptr;
    size_t in_block = write_ % kBlockSize;
    if (in_block + size > kBlockSize) {
      // The rest of this block is skipped (the marker always fits: sizes are multiples of 8).
      WaitForRoom(kBlockSize - in_block);
      auto *h = reinterpret_cast<Header *>(Block(write_) + in_block);
      h->op = Op::kNextBlock;
      h->size = u32(kBlockSize - in_block);
      write_ += kBlockSize - in_block;
      in_block = 0;
    }
    WaitForRoom(size);
    u8 *at = Block(write_) + in_block;
    auto *h = reinterpret_cast<Header *>(at);
    h->op = op;
    h->size = u32(size);
    pending_ = size;
    return at + sizeof(Header);
  }
  // Ends the reserved command. Work goes to the worker in batches (`draw` counts towards one):
  // waking it, or having it spin, for every draw costs the game thread more than it saves.
  void Commit(bool draw) {
    write_ += pending_;
    pending_ = 0;
    if (draw && (++unpublished_draws_ >= kDrawsPerBatch ||
                 write_ - published_ >= kBytesPerBatch))
      Publish();
  }

  void Publish() {
    unpublished_draws_ = 0;
    if (published_ == write_)
      return;
    published_ = write_;
    committed_.store(write_, std::memory_order_seq_cst);
    if (worker_sleeping_.load(std::memory_order_seq_cst))
      committed_.notify_one();
  }

  void Drain() {
    Publish();
    const size_t target = write_;
    WaitConsumed(target);
  }

  void EnsureWorker() {
    if (started_.exchange(true))
      return;
    std::thread([this] { WorkerLoop(); }).detach();
  }

 private:
  u8 *Block(size_t pos) {
    auto &slot = blocks_[(pos / kBlockSize) % kBlockCount];
    if (!slot)
      slot = std::make_unique<u8[]>(kBlockSize);
    return slot.get();
  }

  // The ring may not overwrite what the worker has not replayed yet.
  void WaitForRoom(size_t size) {
    if (write_ + size - consumed_.load(std::memory_order_acquire) <= kCapacity - kBlockSize)
      return;
    Publish();
    WaitConsumed(write_ + size - (kCapacity - kBlockSize));
  }

  void WaitConsumed(size_t target) {
    for (int spin = 0; consumed_.load(std::memory_order_acquire) < target; ++spin) {
      if (spin < 4000) {
        YieldProcessor();
        continue;
      }
      producer_waiting_.store(true, std::memory_order_seq_cst);
      const size_t seen = consumed_.load(std::memory_order_seq_cst);
      if (seen < target)
        consumed_.wait(seen);
      producer_waiting_.store(false, std::memory_order_relaxed);
    }
  }

  static void YieldProcessor() {
#if defined(_M_X64) || defined(__x86_64__)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
  }

  void WorkerLoop();
  void Replay(const Header *h);

  std::array<std::unique_ptr<u8[]>, kBlockCount> blocks_;
  static constexpr u32 kDrawsPerBatch = 48;
  static constexpr size_t kBytesPerBatch = 64 * 1024;
  size_t write_ = 0;      // recording thread
  size_t pending_ = 0;    // recording thread
  size_t published_ = 0;  // recording thread: last value stored to committed_
  u32 unpublished_draws_ = 0;
  alignas(64) std::atomic<size_t> committed_{0};
  alignas(64) std::atomic<size_t> consumed_{0};
  alignas(64) std::atomic<bool> worker_sleeping_{false};
  std::atomic<bool> producer_waiting_{false};
  std::atomic<bool> started_{false};
  RenderCommandList *target_ = nullptr;  // worker: the list kBegin named
};

Stream &stream() {
  static Stream *s = new Stream();  // never destroyed: the worker is detached
  return *s;
}

// Command payloads. Arrays follow their struct, each padded to 8 bytes.
struct BeginCmd { RenderCommandList *target; };
struct BarriersCmd { RenderBarrierStages stages; u32 buffers, textures; };
struct DispatchCmd { u32 x, y, z; };
struct DrawCmd { u32 count, instances, start; i32 base; u32 start_instance; };
struct PtrCmd { const void *p; };
struct PushCmd { u32 range, offset, size; };
struct SetCmd { RenderDescriptorSet *set; u32 index; };
struct RootCmd { RenderBufferReference ref; u32 index; };
struct IndexCmd { bool has_view; RenderIndexBufferView view; };
struct VertexCmd { u32 start, count; bool has_slots; };
struct CountCmd { u32 count; };
struct DepthBiasCmd { float bias, clamp, slope; };
struct ClearColorCmd { u32 attachment; RenderColor color; u32 rects; };
struct ClearDepthCmd { bool depth, stencil; float value; u32 stencil_value, rects; };
struct CopyBufferRegionCmd { RenderBufferReference dst, src; u64 size; };
struct CopyTextureRegionCmd {
  RenderTextureCopyLocation dst, src;
  u32 x, y, z;
  bool has_box;
  RenderBox box;
};
struct TwoPtrCmd { const void *a, *b; };
struct ResolveRegionCmd {
  const RenderTexture *dst;
  u32 x, y;
  const RenderTexture *src;
  bool has_rect;
  RenderRect rect;
  RenderResolveMode mode;
};
struct QueryCmd { const RenderQueryPool *pool; u32 first, count; };

template <typename T> const T &Payload(const Header *h) {
  return *reinterpret_cast<const T *>(reinterpret_cast<const u8 *>(h) + sizeof(Header));
}
template <typename T, typename A> const A *ArrayAfter(const Header *h, size_t skip = 0) {
  return reinterpret_cast<const A *>(reinterpret_cast<const u8 *>(h) + sizeof(Header) +
                                     Align8(sizeof(T)) + skip);
}

void Stream::Replay(const Header *h) {
  RenderCommandList *t = target_;
  switch (h->op) {
  case Op::kNextBlock:
    return;
  case Op::kBegin:
    target_ = Payload<BeginCmd>(h).target;
    target_->begin();
    return;
  default:
    break;
  }
  if (!t)
    return;
  switch (h->op) {
  case Op::kBarriers: {
    const auto &c = Payload<BarriersCmd>(h);
    const auto *buffers = ArrayAfter<BarriersCmd, RenderBufferBarrier>(h);
    const auto *textures = ArrayAfter<BarriersCmd, RenderTextureBarrier>(
        h, Align8(sizeof(RenderBufferBarrier) * c.buffers));
    t->barriers(c.stages, c.buffers ? buffers : nullptr, c.buffers,
                c.textures ? textures : nullptr, c.textures);
    break;
  }
  case Op::kDispatch: {
    const auto &c = Payload<DispatchCmd>(h);
    t->dispatch(c.x, c.y, c.z);
    break;
  }
  case Op::kDrawInstanced: {
    const auto &c = Payload<DrawCmd>(h);
    t->drawInstanced(c.count, c.instances, c.start, c.start_instance);
    break;
  }
  case Op::kDrawIndexedInstanced: {
    const auto &c = Payload<DrawCmd>(h);
    t->drawIndexedInstanced(c.count, c.instances, c.start, c.base, c.start_instance);
    break;
  }
  case Op::kSetPipeline:
    t->setPipeline(static_cast<const RenderPipeline *>(Payload<PtrCmd>(h).p));
    break;
  case Op::kSetComputePipelineLayout:
    t->setComputePipelineLayout(static_cast<const RenderPipelineLayout *>(Payload<PtrCmd>(h).p));
    break;
  case Op::kSetGraphicsPipelineLayout:
    t->setGraphicsPipelineLayout(static_cast<const RenderPipelineLayout *>(Payload<PtrCmd>(h).p));
    break;
  case Op::kSetComputePushConstants:
  case Op::kSetGraphicsPushConstants: {
    const auto &c = Payload<PushCmd>(h);
    const auto *data = ArrayAfter<PushCmd, u8>(h);
    if (h->op == Op::kSetComputePushConstants)
      t->setComputePushConstants(c.range, data, c.offset, c.size);
    else
      t->setGraphicsPushConstants(c.range, data, c.offset, c.size);
    break;
  }
  case Op::kSetComputeDescriptorSet: {
    const auto &c = Payload<SetCmd>(h);
    t->setComputeDescriptorSet(c.set, c.index);
    break;
  }
  case Op::kSetGraphicsDescriptorSet: {
    const auto &c = Payload<SetCmd>(h);
    t->setGraphicsDescriptorSet(c.set, c.index);
    break;
  }
  case Op::kSetGraphicsRootDescriptor: {
    const auto &c = Payload<RootCmd>(h);
    t->setGraphicsRootDescriptor(c.ref, c.index);
    break;
  }
  case Op::kSetIndexBuffer: {
    const auto &c = Payload<IndexCmd>(h);
    t->setIndexBuffer(c.has_view ? &c.view : nullptr);
    break;
  }
  case Op::kSetVertexBuffers: {
    const auto &c = Payload<VertexCmd>(h);
    const auto *views = ArrayAfter<VertexCmd, RenderVertexBufferView>(h);
    const auto *slots = ArrayAfter<VertexCmd, RenderInputSlot>(
        h, Align8(sizeof(RenderVertexBufferView) * c.count));
    t->setVertexBuffers(c.start, views, c.count, c.has_slots ? slots : nullptr);
    break;
  }
  case Op::kSetViewports: {
    const auto &c = Payload<CountCmd>(h);
    t->setViewports(ArrayAfter<CountCmd, RenderViewport>(h), c.count);
    break;
  }
  case Op::kSetScissors: {
    const auto &c = Payload<CountCmd>(h);
    t->setScissors(ArrayAfter<CountCmd, RenderRect>(h), c.count);
    break;
  }
  case Op::kSetFramebuffer:
    t->setFramebuffer(static_cast<const RenderFramebuffer *>(Payload<PtrCmd>(h).p));
    break;
  case Op::kSetDepthBias: {
    const auto &c = Payload<DepthBiasCmd>(h);
    t->setDepthBias(c.bias, c.clamp, c.slope);
    break;
  }
  case Op::kClearColor: {
    const auto &c = Payload<ClearColorCmd>(h);
    t->clearColor(c.attachment, c.color, c.rects ? ArrayAfter<ClearColorCmd, RenderRect>(h) : nullptr,
                  c.rects);
    break;
  }
  case Op::kClearDepthStencil: {
    const auto &c = Payload<ClearDepthCmd>(h);
    t->clearDepthStencil(c.depth, c.stencil, c.value, c.stencil_value,
                         c.rects ? ArrayAfter<ClearDepthCmd, RenderRect>(h) : nullptr, c.rects);
    break;
  }
  case Op::kCopyBufferRegion: {
    const auto &c = Payload<CopyBufferRegionCmd>(h);
    t->copyBufferRegion(c.dst, c.src, c.size);
    break;
  }
  case Op::kCopyTextureRegion: {
    const auto &c = Payload<CopyTextureRegionCmd>(h);
    t->copyTextureRegion(c.dst, c.src, c.x, c.y, c.z, c.has_box ? &c.box : nullptr);
    break;
  }
  case Op::kCopyBuffer: {
    const auto &c = Payload<TwoPtrCmd>(h);
    t->copyBuffer(static_cast<const RenderBuffer *>(c.a), static_cast<const RenderBuffer *>(c.b));
    break;
  }
  case Op::kCopyTexture: {
    const auto &c = Payload<TwoPtrCmd>(h);
    t->copyTexture(static_cast<const RenderTexture *>(c.a),
                   static_cast<const RenderTexture *>(c.b));
    break;
  }
  case Op::kResolveTexture: {
    const auto &c = Payload<TwoPtrCmd>(h);
    t->resolveTexture(static_cast<const RenderTexture *>(c.a),
                      static_cast<const RenderTexture *>(c.b));
    break;
  }
  case Op::kResolveTextureRegion: {
    const auto &c = Payload<ResolveRegionCmd>(h);
    t->resolveTextureRegion(c.dst, c.x, c.y, c.src, c.has_rect ? &c.rect : nullptr, c.mode);
    break;
  }
  case Op::kDiscardTexture:
    t->discardTexture(static_cast<const RenderTexture *>(Payload<PtrCmd>(h).p));
    break;
  case Op::kResetQueryPool: {
    const auto &c = Payload<QueryCmd>(h);
    t->resetQueryPool(c.pool, c.first, c.count);
    break;
  }
  case Op::kWriteTimestamp: {
    const auto &c = Payload<QueryCmd>(h);
    t->writeTimestamp(c.pool, c.first);
    break;
  }
  default:
    break;
  }
}

void Stream::WorkerLoop() {
#if defined(_WIN32)
  SetThreadDescription(GetCurrentThread(), L"SvR command recording");
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
  size_t pos = 0;
  for (;;) {
    const size_t end = committed_.load(std::memory_order_acquire);
    if (pos < end) {
      while (pos < end) {
        const auto *h =
            reinterpret_cast<const Header *>(blocks_[(pos / kBlockSize) % kBlockCount].get() +
                                             pos % kBlockSize);
        Replay(h);
        pos += h->size;
      }
      consumed_.store(pos, std::memory_order_seq_cst);
      if (producer_waiting_.load(std::memory_order_seq_cst))
        consumed_.notify_all();
      continue;
    }
    // Idle: a short spin, then sleep until the next batch (spinning longer competes with the
    // game thread, possibly on the same core).
    bool more = false;
    for (int spin = 0; spin < 64 && !more; ++spin) {
      YieldProcessor();
      more = committed_.load(std::memory_order_acquire) != end;
    }
    if (more)
      continue;
    worker_sleeping_.store(true, std::memory_order_seq_cst);
    if (committed_.load(std::memory_order_seq_cst) == end)
      committed_.wait(end);
    worker_sleeping_.store(false, std::memory_order_relaxed);
  }
}

// The recording side of one frame slot's command list.
class SvrCommandStream final : public RenderCommandList {
 public:
  explicit SvrCommandStream(RenderCommandList *real) : real_(real) {}

  void begin() override {
    auto *c = Rec<BeginCmd>(Op::kBegin);
    c->target = real_;
    Done(true);
  }
  void end() override {
    stream().Drain();
    real_->end();
  }
  void barriers(RenderBarrierStages stages, const RenderBufferBarrier *buffers, uint32_t nb,
                const RenderTextureBarrier *textures, uint32_t nt) override {
    const size_t bb = Align8(sizeof(RenderBufferBarrier) * nb);
    u8 *p = stream().Reserve(Op::kBarriers,
                             Align8(sizeof(BarriersCmd)) + bb + sizeof(RenderTextureBarrier) * nt);
    if (!p) {
      stream().Drain();
      real_->barriers(stages, buffers, nb, textures, nt);
      return;
    }
    new (p) BarriersCmd{stages, nb, nt};
    if (nb)
      std::memcpy(p + Align8(sizeof(BarriersCmd)), buffers, sizeof(RenderBufferBarrier) * nb);
    if (nt)
      std::memcpy(p + Align8(sizeof(BarriersCmd)) + bb, textures,
                  sizeof(RenderTextureBarrier) * nt);
    Done(false);
  }
  void dispatch(uint32_t x, uint32_t y, uint32_t z) override {
    new (Rec<DispatchCmd>(Op::kDispatch)) DispatchCmd{x, y, z};
    Done(true);
  }
  void traceRays(uint32_t width, uint32_t height, uint32_t depth, RenderBufferReference table,
                 const RenderShaderBindingGroupsInfo &groups) override {
    stream().Drain();
    real_->traceRays(width, height, depth, table, groups);
  }
  void drawInstanced(uint32_t count, uint32_t instances, uint32_t start,
                     uint32_t start_instance) override {
    new (Rec<DrawCmd>(Op::kDrawInstanced)) DrawCmd{count, instances, start, 0, start_instance};
    Done(true);
  }
  void drawIndexedInstanced(uint32_t count, uint32_t instances, uint32_t start, int32_t base,
                            uint32_t start_instance) override {
    new (Rec<DrawCmd>(Op::kDrawIndexedInstanced))
        DrawCmd{count, instances, start, base, start_instance};
    Done(true);
  }
  void setPipeline(const RenderPipeline *pipeline) override { Ptr(Op::kSetPipeline, pipeline); }
  void setComputePipelineLayout(const RenderPipelineLayout *layout) override {
    Ptr(Op::kSetComputePipelineLayout, layout);
  }
  void setComputePushConstants(uint32_t range, const void *data, uint32_t offset,
                               uint32_t size) override {
    Push(Op::kSetComputePushConstants, range, data, offset, size);
  }
  void setComputeDescriptorSet(RenderDescriptorSet *set, uint32_t index) override {
    new (Rec<SetCmd>(Op::kSetComputeDescriptorSet)) SetCmd{set, index};
    Done(false);
  }
  void setGraphicsPipelineLayout(const RenderPipelineLayout *layout) override {
    Ptr(Op::kSetGraphicsPipelineLayout, layout);
  }
  void setGraphicsPushConstants(uint32_t range, const void *data, uint32_t offset,
                                uint32_t size) override {
    Push(Op::kSetGraphicsPushConstants, range, data, offset, size);
  }
  void setGraphicsDescriptorSet(RenderDescriptorSet *set, uint32_t index) override {
    new (Rec<SetCmd>(Op::kSetGraphicsDescriptorSet)) SetCmd{set, index};
    Done(false);
  }
  void setGraphicsRootDescriptor(RenderBufferReference ref, uint32_t index) override {
    new (Rec<RootCmd>(Op::kSetGraphicsRootDescriptor)) RootCmd{ref, index};
    Done(false);
  }
  void setRaytracingPipelineLayout(const RenderPipelineLayout *layout) override {
    stream().Drain();
    real_->setRaytracingPipelineLayout(layout);
  }
  void setRaytracingPushConstants(uint32_t range, const void *data, uint32_t offset,
                                  uint32_t size) override {
    stream().Drain();
    real_->setRaytracingPushConstants(range, data, offset, size);
  }
  void setRaytracingDescriptorSet(RenderDescriptorSet *set, uint32_t index) override {
    stream().Drain();
    real_->setRaytracingDescriptorSet(set, index);
  }
  void setIndexBuffer(const RenderIndexBufferView *view) override {
    auto *c = Rec<IndexCmd>(Op::kSetIndexBuffer);
    new (c) IndexCmd{};
    c->has_view = view != nullptr;
    if (view)
      c->view = *view;
    Done(false);
  }
  void setVertexBuffers(uint32_t start, const RenderVertexBufferView *views, uint32_t count,
                        const RenderInputSlot *slots) override {
    const size_t vb = Align8(sizeof(RenderVertexBufferView) * count);
    u8 *p = stream().Reserve(Op::kSetVertexBuffers, Align8(sizeof(VertexCmd)) + vb +
                                                        (slots ? sizeof(RenderInputSlot) * count : 0));
    if (!p) {
      stream().Drain();
      real_->setVertexBuffers(start, views, count, slots);
      return;
    }
    new (p) VertexCmd{start, count, slots != nullptr};
    if (count)
      std::memcpy(p + Align8(sizeof(VertexCmd)), views, sizeof(RenderVertexBufferView) * count);
    if (slots && count)
      std::memcpy(p + Align8(sizeof(VertexCmd)) + vb, slots, sizeof(RenderInputSlot) * count);
    Done(false);
  }
  void setViewports(const RenderViewport *viewports, uint32_t count) override {
    Array(Op::kSetViewports, viewports, count);
  }
  void setScissors(const RenderRect *rects, uint32_t count) override {
    Array(Op::kSetScissors, rects, count);
  }
  void setFramebuffer(const RenderFramebuffer *framebuffer) override {
    Ptr(Op::kSetFramebuffer, framebuffer);
  }
  void setDepthBias(float bias, float clamp, float slope) override {
    new (Rec<DepthBiasCmd>(Op::kSetDepthBias)) DepthBiasCmd{bias, clamp, slope};
    Done(false);
  }
  void clearColor(uint32_t attachment, RenderColor color, const RenderRect *rects,
                  uint32_t count) override {
    u8 *p = stream().Reserve(Op::kClearColor,
                             Align8(sizeof(ClearColorCmd)) + sizeof(RenderRect) * count);
    if (!p) {
      stream().Drain();
      real_->clearColor(attachment, color, rects, count);
      return;
    }
    new (p) ClearColorCmd{attachment, color, rects ? count : 0u};
    if (rects && count)
      std::memcpy(p + Align8(sizeof(ClearColorCmd)), rects, sizeof(RenderRect) * count);
    Done(true);
  }
  void clearDepthStencil(bool depth, bool stencil, float value, uint32_t stencil_value,
                         const RenderRect *rects, uint32_t count) override {
    u8 *p = stream().Reserve(Op::kClearDepthStencil,
                             Align8(sizeof(ClearDepthCmd)) + sizeof(RenderRect) * count);
    if (!p) {
      stream().Drain();
      real_->clearDepthStencil(depth, stencil, value, stencil_value, rects, count);
      return;
    }
    new (p) ClearDepthCmd{depth, stencil, value, stencil_value, rects ? count : 0u};
    if (rects && count)
      std::memcpy(p + Align8(sizeof(ClearDepthCmd)), rects, sizeof(RenderRect) * count);
    Done(true);
  }
  void copyBufferRegion(RenderBufferReference dst, RenderBufferReference src,
                        uint64_t size) override {
    new (Rec<CopyBufferRegionCmd>(Op::kCopyBufferRegion)) CopyBufferRegionCmd{dst, src, size};
    Done(true);
  }
  void copyTextureRegion(const RenderTextureCopyLocation &dst,
                         const RenderTextureCopyLocation &src, uint32_t x, uint32_t y, uint32_t z,
                         const RenderBox *box) override {
    auto *c = Rec<CopyTextureRegionCmd>(Op::kCopyTextureRegion);
    new (c) CopyTextureRegionCmd{dst, src, x, y, z, box != nullptr, box ? *box : RenderBox()};
    Done(true);
  }
  void copyBuffer(const RenderBuffer *dst, const RenderBuffer *src) override {
    new (Rec<TwoPtrCmd>(Op::kCopyBuffer)) TwoPtrCmd{dst, src};
    Done(true);
  }
  void copyTexture(const RenderTexture *dst, const RenderTexture *src) override {
    new (Rec<TwoPtrCmd>(Op::kCopyTexture)) TwoPtrCmd{dst, src};
    Done(true);
  }
  void resolveTexture(const RenderTexture *dst, const RenderTexture *src) override {
    new (Rec<TwoPtrCmd>(Op::kResolveTexture)) TwoPtrCmd{dst, src};
    Done(true);
  }
  void resolveTextureRegion(const RenderTexture *dst, uint32_t x, uint32_t y,
                            const RenderTexture *src, const RenderRect *rect,
                            RenderResolveMode mode) override {
    auto *c = Rec<ResolveRegionCmd>(Op::kResolveTextureRegion);
    new (c) ResolveRegionCmd{dst, x, y, src, rect != nullptr, rect ? *rect : RenderRect(), mode};
    Done(true);
  }
  void buildBottomLevelAS(const RenderAccelerationStructure *dst, RenderBufferReference scratch,
                          const RenderBottomLevelASBuildInfo &info) override {
    stream().Drain();
    real_->buildBottomLevelAS(dst, scratch, info);
  }
  void buildTopLevelAS(const RenderAccelerationStructure *dst, RenderBufferReference scratch,
                       RenderBufferReference instances,
                       const RenderTopLevelASBuildInfo &info) override {
    stream().Drain();
    real_->buildTopLevelAS(dst, scratch, instances, info);
  }
  void discardTexture(const RenderTexture *texture) override {
    Ptr(Op::kDiscardTexture, texture);
  }
  void resetQueryPool(const RenderQueryPool *pool, uint32_t first, uint32_t count) override {
    new (Rec<QueryCmd>(Op::kResetQueryPool)) QueryCmd{pool, first, count};
    Done(false);
  }
  void writeTimestamp(const RenderQueryPool *pool, uint32_t index) override {
    new (Rec<QueryCmd>(Op::kWriteTimestamp)) QueryCmd{pool, index, 0};
    Done(false);
  }

 private:
  template <typename T> T *Rec(Op op) {
    static_assert(std::is_trivially_copyable_v<T>);
    return reinterpret_cast<T *>(stream().Reserve(op, sizeof(T)));  // fixed-size: always fits
  }
  void Done(bool publish) { stream().Commit(publish); }
  void Ptr(Op op, const void *p) {
    new (Rec<PtrCmd>(op)) PtrCmd{p};
    Done(false);
  }
  void Push(Op op, uint32_t range, const void *data, uint32_t offset, uint32_t size) {
    u8 *p = size ? stream().Reserve(op, Align8(sizeof(PushCmd)) + size) : nullptr;
    if (!p) {
      // Size 0 means "the whole range", which only the real list knows.
      stream().Drain();
      if (op == Op::kSetComputePushConstants)
        real_->setComputePushConstants(range, data, offset, size);
      else
        real_->setGraphicsPushConstants(range, data, offset, size);
      return;
    }
    new (p) PushCmd{range, offset, size};
    std::memcpy(p + Align8(sizeof(PushCmd)), data, size);
    Done(false);
  }
  template <typename A> void Array(Op op, const A *items, uint32_t count) {
    u8 *p = stream().Reserve(op, Align8(sizeof(CountCmd)) + sizeof(A) * count);
    if (!p) {
      stream().Drain();
      if constexpr (std::is_same_v<A, RenderViewport>)
        real_->setViewports(items, count);
      else
        real_->setScissors(items, count);
      return;
    }
    new (p) CountCmd{count};
    if (count)
      std::memcpy(p + Align8(sizeof(CountCmd)), items, sizeof(A) * count);
    Done(false);
  }

  RenderCommandList *real_;
};

static_assert(std::is_trivially_copyable_v<RenderBufferBarrier>);
static_assert(std::is_trivially_copyable_v<RenderTextureBarrier>);
static_assert(std::is_trivially_copyable_v<RenderVertexBufferView>);
static_assert(std::is_trivially_copyable_v<RenderInputSlot>);
static_assert(std::is_trivially_copyable_v<RenderViewport>);
static_assert(std::is_trivially_copyable_v<RenderRect>);
static_assert(std::is_trivially_copyable_v<CopyTextureRegionCmd>);
static_assert(std::is_trivially_copyable_v<IndexCmd>);

}  // namespace

plume::RenderCommandList *SvrRecordingList(u32 slot, plume::RenderCommandList *real) {
  static const bool enabled = [] {
    const bool on = REXCVAR_GET(svr_threaded_recording);
    BD_INFO("SvR: threaded command recording {}", on ? "on" : "off");
    return on;
  }();
  if (!enabled || !real)
    return real;
  static std::unique_ptr<SvrCommandStream> lists[8];
  static plume::RenderCommandList *targets[8] = {};
  if (slot >= 8)
    return real;
  if (!lists[slot] || targets[slot] != real) {
    stream().EnsureWorker();
    // A list re-created (device rebuild) gets a new stream object; the old one is idle by then.
    stream().Drain();
    lists[slot] = std::make_unique<SvrCommandStream>(real);
    targets[slot] = real;
  }
  return lists[slot].get();
}

void SvrCommandStreamDrain() {
  if (REXCVAR_GET(svr_threaded_recording))
    stream().Drain();
}

}  // namespace bd::gpu
