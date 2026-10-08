// Debug: periodically save the guest output image (what the game presented) as PPM.
//
//   SVR_FRAME_DUMP=logs/frames ./run.sh        # every 5 s
//   SVR_FRAME_DUMP_INTERVAL=1 ...              # seconds between captures
//
// Each capture also logs the mean RGB, so a black screen shows up in the log as
// "mean 0,0,0" without opening the image. Convert with `sips -s format png x.ppm --out x.png`.

#pragma once

#include <rex/logging.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/presenter.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

class FrameDumper {
 public:
  ~FrameDumper() { Stop(); }

  void Start(rex::system::IGraphicsSystem* graphics) {
    const char* dir = std::getenv("SVR_FRAME_DUMP");
    if (!dir || !*dir || !graphics || !graphics->presenter())
      return;
    dir_ = dir;
    std::filesystem::create_directories(dir_);
    const char* interval = std::getenv("SVR_FRAME_DUMP_INTERVAL");
    interval_s_ = interval ? (std::max)(1, std::atoi(interval)) : 5;
    presenter_ = graphics->presenter();
    running_ = true;
    thread_ = std::thread([this] { Run(); });
  }

  void Stop() {
    running_ = false;
    if (thread_.joinable())
      thread_.join();
  }

 private:
  void Run() {
    auto start = std::chrono::steady_clock::now();
    while (running_) {
      for (int i = 0; i < interval_s_ * 10 && running_; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (!running_)
        break;
      auto sec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start)
                     .count();
      rex::ui::RawImage image;
      if (!presenter_->CaptureGuestOutput(image)) {
        REXLOG_INFO("[frame] {}s: no guest output yet", sec);
        continue;
      }
      Save(image, sec);
    }
  }

  void Save(const rex::ui::RawImage& image, long long sec) {
    auto path = dir_ / ("frame_" + std::to_string(sec) + "s.ppm");
    FILE* f = std::fopen(path.string().c_str(), "wb");
    if (!f)
      return;
    std::fprintf(f, "P6\n%u %u\n255\n", image.width, image.height);
    uint64_t sum[3] = {};
    for (uint32_t y = 0; y < image.height; ++y) {
      const uint8_t* row = image.data.data() + y * image.stride;
      for (uint32_t x = 0; x < image.width; ++x) {
        std::fwrite(row + x * 4, 1, 3, f);  // R8 G8 B8 X8
        for (int c = 0; c < 3; ++c)
          sum[c] += row[x * 4 + c];
      }
    }
    std::fclose(f);
    uint64_t n = uint64_t(image.width) * image.height;
    REXLOG_INFO("[frame] {}s: {}x{} mean {},{},{} -> {}", sec, image.width, image.height, sum[0] / n,
                sum[1] / n, sum[2] / n, path.string());
  }

  std::filesystem::path dir_;
  int interval_s_ = 5;
  rex::ui::Presenter* presenter_ = nullptr;
  std::atomic<bool> running_{false};
  std::thread thread_;
};
