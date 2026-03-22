/*
 * MouseController.hpp - Mouse movement and click control
 *
 * Handles all mouse-related operations including movement, clicking, and scrolling.
 * Separated from IO to break monolithic design.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <optional>

namespace havel {

class EventListener;

enum class MouseAction { Hold = 1, Release = 0, Click = 2 };

/**
 * Parse mouse button from string or number
 * @param value String ("left", "L", "1") or number (1-5)
 * @return Button code 1-5, or nullopt if invalid
 */
std::optional<int> ParseMouseButton(const std::string& value);
std::optional<int> ParseMouseButton(int value);

/**
 * Parse duration string (e.g., "30s", "1h30m", "0:0:30.500")
 * @param value Duration string or number (milliseconds)
 * @return Duration in milliseconds, or nullopt if invalid
 */
std::optional<long long> ParseDuration(const std::string& value);
std::optional<long long> ParseDuration(double value);

/**
 * MouseController - Handles mouse movement and clicking
 *
 * Responsibilities:
 * - Mouse movement (relative and absolute)
 * - Mouse clicking
 * - Scroll wheel control
 * - Mouse sensitivity
 */
class MouseController {
public:
  MouseController(EventListener *eventListener);
  ~MouseController() = default;

  // Mouse movement
  bool Move(int dx, int dy, int speed = 1, float accel = 1.0f);
  bool MoveTo(int targetX, int targetY, int speed = 1, float accel = 1.0f);
  bool MoveSensitive(int dx, int dy, int baseSpeed = 5, float accel = 1.5f);

  // Mouse clicking
  bool ClickAt(int x, int y, int button = 1, int speed = 1, float accel = 1.0f);
  bool EmitClick(int btnCode, MouseAction action);

  // Scroll
  bool Scroll(double dy, double dx = 0);

  // Sensitivity control
  void SetSensitivity(double sensitivity);
  double GetSensitivity() const { return sensitivity; }

  void SetScrollSpeed(double speed);
  double GetScrollSpeed() const { return scrollSpeed; }

  // Get mouse position
  std::pair<int, int> GetPosition();

private:
  EventListener *eventListener;

  // Sensitivity settings
  double sensitivity = 1.0;
  double scrollSpeed = 1.0;

  // Scroll accumulation for fractional values
  double scrollAccumY = 0.0;
  double scrollAccumX = 0.0;

  // Current mouse position (for absolute movement)
  int currentX = 0;
  int currentY = 0;
};

} // namespace havel
