// Runtime for the developer D3D call census (src/dev/d3d_census.cpp).

#pragma once

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace svr_census {

class Census {
 public:
  Census(const uint32_t* addrs, size_t count, size_t swap_index)
      : addrs_(addrs), count_(count), swap_index_(swap_index),
        calls_(new std::atomic<uint64_t>[count]), samples_(new Sample[count]) {
    for (size_t i = 0; i < count; ++i) calls_[i] = 0;
    reporter_ = std::thread([this] { Report(); });
    reporter_.detach();
  }

  void Hit(size_t index, const PPCContext& ctx) {
    calls_[index].fetch_add(1, std::memory_order_relaxed);
    Sample& s = samples_[index];
    s.r[0] = ctx.r3.u32; s.r[1] = ctx.r4.u32; s.r[2] = ctx.r5.u32;
    s.r[3] = ctx.r6.u32; s.r[4] = ctx.r7.u32; s.r[5] = ctx.r8.u32;
    s.lr = uint32_t(ctx.lr);
  }

 private:
  struct Sample {
    uint32_t r[6];
    uint32_t lr;
  };

  void Report() {
    std::vector<uint64_t> last(count_, 0);
    for (int tick = 1;; ++tick) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      if (tick % 6 == 0) {
        // Every 30 s: every function called so far, with its total and the last call's arguments,
        // so rarely called functions (resource creation, locks) show up too.
        REXLOG_INFO("[census-all] functions called so far:");
        for (size_t i = 0; i < count_; ++i) {
          uint64_t total = calls_[i].load(std::memory_order_relaxed);
          if (!total) continue;
          const Sample& s = samples_[i];
          REXLOG_INFO("[census-all] sub_{:08X} {:10} calls  r3-r8 {:08X} {:08X} {:08X} {:08X} "
                      "{:08X} {:08X} from {:08X}",
                      addrs_[i], total, s.r[0], s.r[1], s.r[2], s.r[3], s.r[4], s.r[5], s.lr);
        }
      }
      std::vector<std::pair<uint64_t, size_t>> rows;
      for (size_t i = 0; i < count_; ++i) {
        uint64_t now = calls_[i].load(std::memory_order_relaxed);
        if (now != last[i]) rows.emplace_back(now - last[i], i);
        last[i] = now;
      }
      uint64_t swaps = 1;
      for (auto& [n, i] : rows)
        if (i == swap_index_) swaps = std::max<uint64_t>(n, 1);
      std::sort(rows.rbegin(), rows.rend());
      REXLOG_INFO("[census] {} swaps in 5 s; most-called D3D functions per swap:", swaps);
      for (size_t k = 0; k < rows.size() && k < 60; ++k) {
        auto [n, i] = rows[k];
        const Sample& s = samples_[i];
        REXLOG_INFO("[census] sub_{:08X} {:9.1f}/swap  r3-r8 {:08X} {:08X} {:08X} {:08X} {:08X} "
                    "{:08X} from {:08X}",
                    addrs_[i], double(n) / swaps, s.r[0], s.r[1], s.r[2], s.r[3], s.r[4],
                    s.r[5], s.lr);
      }
    }
  }

  const uint32_t* addrs_;
  size_t count_;
  size_t swap_index_;
  std::unique_ptr<std::atomic<uint64_t>[]> calls_;
  std::unique_ptr<Sample[]> samples_;
  std::thread reporter_;
};

}  // namespace svr_census
