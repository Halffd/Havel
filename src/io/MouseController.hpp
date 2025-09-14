#include "core/IO.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>

namespace havel {

class MouseController {
private:
  std::atomic<int> baseSpeed{5};
  std::atomic<float> acceleration{1.2f};
  std::atomic<int> currentSpeed{5};
  std::atomic<bool> accelerationActive{false};

  std::atomic<std::chrono::steady_clock::time_point::rep> lastMoveTicks{0};
  std::atomic<std::chrono::steady_clock::time_point::rep> accelStartTicks{0};

  IO &io;

  std::thread resetThread;
  std::atomic<bool> running{true};
  std::condition_variable_any resetCV; // condition_variable_any works with atomics
  std::mutex cvMutex; // only for waiting

public:
  explicit MouseController(IO &ioInstance) : io(ioInstance) {
    resetThread = std::thread(&MouseController::resetThreadFunc, this);
  }
  MouseController(const MouseController&) = delete;
  MouseController& operator=(const MouseController&) = delete;
  MouseController(MouseController&&) = delete;
  MouseController& operator=(MouseController&&) = delete;
  
  ~MouseController() {
    running = false;
    resetCV.notify_all();
    if (resetThread.joinable()) {
      resetThread.join();
    }
  }

  void move(int dx, int dy) {
    auto now = std::chrono::steady_clock::now();
    auto nowTicks = now.time_since_epoch().count();

    if (!accelerationActive.load(std::memory_order_relaxed)) {
      accelStartTicks.store(nowTicks, std::memory_order_relaxed);
      accelerationActive.store(true, std::memory_order_relaxed);
    }

    long long elapsed = nowTicks - accelStartTicks.load(std::memory_order_relaxed);
    elapsed /= 1'000'000; // ns → ms

    int base = baseSpeed.load(std::memory_order_relaxed);
    float accel = acceleration.load(std::memory_order_relaxed);

    int newSpeed = base;
    if (elapsed < 1000) {
      float timeFactor = 1.0f + (elapsed / 1000.0f) * 2.0f;
      newSpeed = std::min(base * 10,
                          static_cast<int>(base * timeFactor * accel));
    }

    currentSpeed.store(newSpeed, std::memory_order_relaxed);
    lastMoveTicks.store(nowTicks, std::memory_order_relaxed);

    io.MouseMove(dx, dy, newSpeed, accel);
    resetCV.notify_all();
  }

  void resetAcceleration() {
    currentSpeed.store(baseSpeed.load(), std::memory_order_relaxed);
    accelerationActive.store(false, std::memory_order_relaxed);
  }

  void setBaseSpeed(int speed) {
    baseSpeed.store(std::max(1, speed));
  }

  void setAcceleration(float accel) {
    acceleration.store(std::max(0.1f, accel));
  }

private:
  void resetThreadFunc() {
    std::unique_lock<std::mutex> lock(cvMutex);
    while (running.load()) {
      if (resetCV.wait_for(lock, std::chrono::milliseconds(300)) ==
          std::cv_status::timeout) {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        auto last = lastMoveTicks.load(std::memory_order_relaxed);

        long long elapsed = (now - last) / 1'000'000; // ns → ms
        if (accelerationActive.load() && elapsed > 300) {
          currentSpeed.store(baseSpeed.load(), std::memory_order_relaxed);
          accelerationActive.store(false, std::memory_order_relaxed);
        }
      }
    }
  }
};

} // namespace havel
