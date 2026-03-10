#include "MouseGestureEngine.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace havel {

MouseGestureEngine::MouseGestureEngine() = default;

bool MouseGestureEngine::HasRegisteredGestures() const {
  std::lock_guard<std::mutex> lock(mutex);
  return !hotkeys.empty();
}

void MouseGestureEngine::RegisterHotkey(
    int id, const std::vector<MouseGestureDirection> &directions) {
  std::lock_guard<std::mutex> lock(mutex);
  hotkeys[id] = directions;
}

void MouseGestureEngine::UnregisterHotkey(int id) {
  std::lock_guard<std::mutex> lock(mutex);
  hotkeys.erase(id);
}

void MouseGestureEngine::Reset() {
  std::lock_guard<std::mutex> lock(mutex);
  currentGesture.isActive = false;
  currentGesture.directions.clear();
  currentGesture.xPositions.clear();
  currentGesture.yPositions.clear();
  currentGesture.totalDistance = 0;
  recentMovements.clear();
}

std::vector<int> MouseGestureEngine::RecordMovement(int dx, int dy) {
  std::lock_guard<std::mutex> lock(mutex);

  if (hotkeys.empty()) {
    return {};
  }

  const auto now = std::chrono::steady_clock::now();
  recentMovements.push_back(MouseMovement{dx, dy, now});

  const auto cutoffTime = now - std::chrono::milliseconds(50);
  recentMovements.erase(
      std::remove_if(recentMovements.begin(), recentMovements.end(),
                     [cutoffTime](const MouseMovement &movement) {
                       return movement.time < cutoffTime;
                     }),
      recentMovements.end());

  int combinedX = 0;
  int combinedY = 0;
  for (const auto &movement : recentMovements) {
    combinedX += movement.dx;
    combinedY += movement.dy;
  }

  if (std::abs(combinedX) <= 5 && std::abs(combinedY) <= 5) {
    return {};
  }

  recentMovements.clear();
  return processMovementLocked(combinedX, combinedY);
}

std::vector<int> MouseGestureEngine::processMovementLocked(int dx, int dy) {
  const auto now = std::chrono::steady_clock::now();

  if (!currentGesture.isActive) {
    const double distance = std::sqrt(dx * dx + dy * dy);
    if (distance < currentGesture.minDistance) {
      return {};
    }

    currentGesture.isActive = true;
    currentGesture.startTime = now;
    currentGesture.lastMoveTime = now;
    currentGesture.totalDistance = static_cast<int>(distance);
    currentGesture.directions = {GetDirection(dx, dy)};
    return {};
  }

  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                            currentGesture.startTime)
          .count();
  if (elapsed > currentGesture.timeout) {
    currentGesture.isActive = false;
    currentGesture.directions.clear();
    currentGesture.totalDistance = 0;
    return {};
  }

  const auto direction = GetDirection(dx, dy);
  if (currentGesture.directions.empty() ||
      currentGesture.directions.back() != direction) {
    currentGesture.directions.push_back(direction);
  }

  const double distance = std::sqrt(dx * dx + dy * dy);
  currentGesture.totalDistance += static_cast<int>(distance);
  currentGesture.lastMoveTime = now;

  std::vector<int> matches;
  for (const auto &[id, expectedDirections] : hotkeys) {
    if (MatchPattern(expectedDirections, currentGesture.directions)) {
      matches.push_back(id);
    }
  }

  if (!matches.empty()) {
    currentGesture.isActive = false;
    currentGesture.directions.clear();
    currentGesture.xPositions.clear();
    currentGesture.yPositions.clear();
    currentGesture.totalDistance = 0;
  }

  return matches;
}

MouseGestureDirection MouseGestureEngine::GetDirection(int dx, int dy) const {
  double angle = std::atan2(dy, dx) * (180.0 / M_PI);
  if (angle < 0) {
    angle += 360.0;
  }

  if (angle >= 337.5 || angle < 22.5) {
    return MouseGestureDirection::Right;
  }
  if (angle >= 22.5 && angle < 67.5) {
    return MouseGestureDirection::DownRight;
  }
  if (angle >= 67.5 && angle < 112.5) {
    return MouseGestureDirection::Down;
  }
  if (angle >= 112.5 && angle < 157.5) {
    return MouseGestureDirection::DownLeft;
  }
  if (angle >= 157.5 && angle < 202.5) {
    return MouseGestureDirection::Left;
  }
  if (angle >= 202.5 && angle < 247.5) {
    return MouseGestureDirection::UpLeft;
  }
  if (angle >= 247.5 && angle < 292.5) {
    return MouseGestureDirection::Up;
  }
  if (angle >= 292.5 && angle < 337.5) {
    return MouseGestureDirection::UpRight;
  }

  return MouseGestureDirection::Right;
}

bool MouseGestureEngine::MatchPattern(
    const std::vector<MouseGestureDirection> &expected,
    const std::vector<MouseGestureDirection> &actual) const {
  if (actual.size() < expected.size()) {
    return false;
  }

  const size_t startIndex = actual.size() - expected.size();
  for (size_t i = 0; i < expected.size(); ++i) {
    if (actual[startIndex + i] != expected[i]) {
      return false;
    }
  }

  return true;
}

bool MouseGestureEngine::IsGestureValid(
    const std::vector<MouseGestureDirection> &pattern, int minDistance) const {
  std::lock_guard<std::mutex> lock(mutex);
  return !pattern.empty() && currentGesture.totalDistance >= minDistance;
}

std::vector<MouseGestureDirection>
MouseGestureEngine::ParsePattern(const std::string &patternStr) const {
  std::vector<MouseGestureDirection> directions;

  if (patternStr == "circle" || patternStr == "square") {
    return {MouseGestureDirection::Right, MouseGestureDirection::Down,
            MouseGestureDirection::Left, MouseGestureDirection::Up};
  }
  if (patternStr == "triangle") {
    return {MouseGestureDirection::UpRight, MouseGestureDirection::DownLeft,
            MouseGestureDirection::Down};
  }
  if (patternStr == "zigzag") {
    return {MouseGestureDirection::Right, MouseGestureDirection::DownLeft,
            MouseGestureDirection::Right, MouseGestureDirection::UpLeft};
  }
  if (patternStr == "check") {
    return {MouseGestureDirection::DownRight, MouseGestureDirection::UpRight};
  }

  std::string direction;
  std::istringstream input(patternStr);
  while (std::getline(input, direction, ',')) {
    direction.erase(0, direction.find_first_not_of(" \t\n\r"));
    direction.erase(direction.find_last_not_of(" \t\n\r") + 1);

    if (direction == "mouseup" || direction == "up") {
      directions.push_back(MouseGestureDirection::Up);
    } else if (direction == "mousedown" || direction == "down") {
      directions.push_back(MouseGestureDirection::Down);
    } else if (direction == "mouseleft" || direction == "left") {
      directions.push_back(MouseGestureDirection::Left);
    } else if (direction == "mouseright" || direction == "right") {
      directions.push_back(MouseGestureDirection::Right);
    } else if (direction == "mouseupleft" || direction == "up-left" ||
               direction == "upleft") {
      directions.push_back(MouseGestureDirection::UpLeft);
    } else if (direction == "mouseupright" || direction == "up-right" ||
               direction == "upright") {
      directions.push_back(MouseGestureDirection::UpRight);
    } else if (direction == "mousedownleft" || direction == "down-left" ||
               direction == "downleft") {
      directions.push_back(MouseGestureDirection::DownLeft);
    } else if (direction == "mousedownright" || direction == "down-right" ||
               direction == "downright") {
      directions.push_back(MouseGestureDirection::DownRight);
    }
  }

  return directions;
}

} // namespace havel
