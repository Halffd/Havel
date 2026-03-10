#pragma once

#include "core/MouseGestureTypes.hpp"
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel {

class MouseGestureEngine {
public:
  MouseGestureEngine();

  bool HasRegisteredGestures() const;
  void RegisterHotkey(int id,
                      const std::vector<MouseGestureDirection> &directions);
  void UnregisterHotkey(int id);
  void Reset();

  std::vector<int> RecordMovement(int dx, int dy);

  MouseGestureDirection GetDirection(int dx, int dy) const;
  bool MatchPattern(const std::vector<MouseGestureDirection> &expected,
                    const std::vector<MouseGestureDirection> &actual) const;
  bool IsGestureValid(const std::vector<MouseGestureDirection> &pattern,
                      int minDistance) const;
  std::vector<MouseGestureDirection>
  ParsePattern(const std::string &patternStr) const;

private:
  struct MouseMovement {
    int dx = 0;
    int dy = 0;
    std::chrono::steady_clock::time_point time;
  };

  std::vector<int> processMovementLocked(int dx, int dy);

  mutable std::mutex mutex;
  MouseGesture currentGesture;
  std::unordered_map<int, std::vector<MouseGestureDirection>> hotkeys;
  std::vector<MouseMovement> recentMovements;
};

} // namespace havel
