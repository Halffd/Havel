#include "core/IO.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
namespace havel {

// Mouse emulation with better acceleration handling
class MouseController {
private:
  int baseSpeed = 5;
  float acceleration = 1.2f;
  int currentSpeed = baseSpeed;
  std::chrono::steady_clock::time_point lastMoveTime;
  std::chrono::steady_clock::time_point accelerationStartTime;
  std::atomic<bool> accelerationActive{false};
  IO &io;
  std::mutex mutex;

  // Background thread for acceleration reset
  std::thread resetThread;
  std::atomic<bool> running{true};
  std::condition_variable resetCV;

public:
  explicit MouseController(IO &ioInstance) : io(ioInstance) {
    // Start the reset thread
    resetThread = std::thread(&MouseController::resetThreadFunc, this);
  }

  ~MouseController() {
    running = false;
    resetCV.notify_all();
    if (resetThread.joinable()) {
      resetThread.join();
    }
  }

  void move(int dx, int dy) {
    auto now = std::chrono::steady_clock::now();

    // Start acceleration timer on first move
    if (!accelerationActive) {
      accelerationStartTime = now;
      accelerationActive = true;
    }

    // Apply acceleration based on time since acceleration started
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - accelerationStartTime)
                       .count();

    if (elapsed < 1000) {
      float timeFactor = 1.0f + (elapsed / 1000.0f) * 2.0f;
      currentSpeed =
          std::min(baseSpeed * 10,
                   static_cast<int>(baseSpeed * timeFactor * acceleration));
    }

    lastMoveTime = now;
    io.MouseMove(dx, dy, currentSpeed, acceleration);

    // Notify reset thread that we moved
    resetCV.notify_all();
  }

  void resetAcceleration() {
    std::lock_guard<std::mutex> lock(mutex);
    currentSpeed = baseSpeed;
    accelerationActive = false;
  }

  void setBaseSpeed(int speed) {
    std::lock_guard<std::mutex> lock(mutex);
    baseSpeed = std::max(1, speed);
  }

  void setAcceleration(float accel) {
    std::lock_guard<std::mutex> lock(mutex);
    acceleration = std::max(0.1f, accel);
  }

private:
  void resetThreadFunc() {
    std::unique_lock<std::mutex> lock(mutex);

    while (running) {
      // Wait for movement or shutdown
      resetCV.wait_for(lock, std::chrono::milliseconds(300),
                       [this] { return !running; });

      if (!running)
        break;

      // Check if it's time to reset acceleration
      auto now = std::chrono::steady_clock::now();
      auto timeSinceLastMove =
          std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                lastMoveTime)
              .count();

      if (accelerationActive && timeSinceLastMove > 300) {
        currentSpeed = baseSpeed;
        accelerationActive = false;
      }
    }
  }
};
} // namespace havel