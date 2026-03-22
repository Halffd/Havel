/*
 * MouseController.cpp - Mouse movement and click control
 *
 * Handles all mouse-related operations including movement, clicking, and scrolling.
 * Separated from IO to break monolithic design.
 */
#include "MouseController.hpp"
#include "EventListener.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <sstream>

namespace havel {

std::optional<int> ParseMouseButton(const std::string& value) {
    std::string s = value;
    // Convert to lowercase
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    
    // Full names
    if (s == "left" || s == "l") return 1;
    if (s == "right" || s == "r") return 2;
    if (s == "middle" || s == "m") return 3;
    if (s == "back" || s == "b" || s == "side1") return 4;
    if (s == "forward" || s == "f" || s == "side2") return 5;
    
    // Try parsing as number
    try {
        int btn = std::stoi(s);
        if (btn >= 1 && btn <= 5) return btn;
    } catch (...) {}
    
    return std::nullopt;
}

std::optional<int> ParseMouseButton(int value) {
    if (value >= 1 && value <= 5) return value;
    return std::nullopt;
}

std::optional<long long> ParseDuration(const std::string& value) {
    std::string s = value;
    long long totalMs = 0;
    
    // Check for time format HH:MM:SS.mmm
    std::regex timeFormat(R"((\d+):(\d+):(\d+)(?:\.(\d+))?)");
    std::smatch timeMatch;
    if (std::regex_match(s, timeMatch, timeFormat)) {
        long long hours = std::stoll(timeMatch[1].str());
        long long minutes = std::stoll(timeMatch[2].str());
        long long seconds = std::stoll(timeMatch[3].str());
        long long ms = 0;
        if (timeMatch[4].matched) {
            std::string msStr = timeMatch[4].str();
            // Pad or truncate to 3 digits
            if (msStr.length() > 3) msStr = msStr.substr(0, 3);
            while (msStr.length() < 3) msStr += "0";
            ms = std::stoll(msStr);
        }
        totalMs = hours * 3600000 + minutes * 60000 + seconds * 1000 + ms;
        return totalMs;
    }
    
    // Parse unit-based durations (e.g., "1h30m500ms")
    std::regex unitFormat(R"((\d+)(ms|s|m|h|d|w))", std::regex::icase);
    auto begin = std::sregex_iterator(s.begin(), s.end(), unitFormat);
    auto end = std::sregex_iterator();
    
    if (begin == end) {
        // No units found, try plain number
        try {
            return std::stoll(s);
        } catch (...) {
            return std::nullopt;
        }
    }
    
    for (auto it = begin; it != end; ++it) {
        long long num = std::stoll((*it)[1].str());
        std::string unit = (*it)[2].str();
        
        // Convert to lowercase
        std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
        
        if (unit == "ms") totalMs += num;
        else if (unit == "s") totalMs += num * 1000;
        else if (unit == "m") totalMs += num * 60000;
        else if (unit == "h") totalMs += num * 3600000;
        else if (unit == "d") totalMs += num * 86400000;
        else if (unit == "w") totalMs += num * 604800000;
    }
    
    return totalMs;
}

std::optional<long long> ParseDuration(double value) {
    return static_cast<long long>(value);
}

MouseController::MouseController(EventListener *eventListener)
    : eventListener(eventListener) {
  if (!eventListener) {
    error("MouseController: EventListener is null");
  }
}

bool MouseController::Move(int dx, int dy, int speed, float accel) {
  if (!eventListener) {
    error("Cannot move mouse: EventListener not initialized");
    return false;
  }

  // Apply sensitivity
  double actualDx = dx * sensitivity * accel;
  double actualDy = dy * sensitivity * accel;

  // Send using EventListener
  if (static_cast<int>(actualDx) != 0) {
    eventListener->SendUinputEvent(EV_REL, REL_X, static_cast<int>(actualDx));
  }
  if (static_cast<int>(actualDy) != 0) {
    eventListener->SendUinputEvent(EV_REL, REL_Y, static_cast<int>(actualDy));
  }
  // Send sync event
  eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
  return true;
}

bool MouseController::MoveTo(int targetX, int targetY, int speed, float accel) {
  if (!eventListener) {
    error("Cannot move mouse: EventListener not initialized");
    return false;
  }

  // Get current position
  auto [currentX, currentY] = GetPosition();

  int dx = targetX - currentX;
  int dy = targetY - currentY;
  int distance = std::sqrt(dx * dx + dy * dy);

  // If close enough, just send final delta
  if (distance < 3) {
    if (dx != 0 || dy != 0) {
      eventListener->SendUinputEvent(EV_REL, REL_X, dx);
      eventListener->SendUinputEvent(EV_REL, REL_Y, dy);
      eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
    }
    return true;
  }

  // Calculate steps for smooth movement
  int steps = distance / speed;
  if (steps < 1)
    steps = 1;

  float stepDx = static_cast<float>(dx) / steps;
  float stepDy = static_cast<float>(dy) / steps;

  for (int i = 0; i < steps; i++) {
    // Send RELATIVE movement
    if (static_cast<int>(stepDx) != 0 || static_cast<int>(stepDy) != 0) {
      eventListener->SendUinputEvent(EV_REL, REL_X, static_cast<int>(stepDx));
      eventListener->SendUinputEvent(EV_REL, REL_Y, static_cast<int>(stepDy));
      eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
    }

    // Minimal sleep for smooth movement
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  // Send final correction if needed
  int finalDx = targetX - currentX;
  int finalDy = targetY - currentY;
  if (finalDx != 0 || finalDy != 0) {
    eventListener->SendUinputEvent(EV_REL, REL_X, finalDx);
    eventListener->SendUinputEvent(EV_REL, REL_Y, finalDy);
    eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
  }

  return true;
}

bool MouseController::MoveSensitive(int dx, int dy, int baseSpeed, float accel) {
  // Calculate distance
  int distance = std::sqrt(dx * dx + dy * dy);

  // Adjust speed based on distance (closer = slower, farther = faster)
  float factor = 1.0f;
  if (distance > 0) {
    factor = std::min(2.0f, std::max(0.5f, distance / 100.0f));
  }

  int adjustedDx = static_cast<int>(dx * factor);
  int adjustedDy = static_cast<int>(dy * factor);

  // Call the original Move with adjusted values
  return Move(adjustedDx, adjustedDy, baseSpeed, 1.0f);
}

bool MouseController::ClickAt(int x, int y, int button, int speed, float accel) {
  if (!eventListener) {
    error("Cannot click: EventListener not initialized");
    return false;
  }

  // Move to position
  if (!MoveTo(x, y, speed, accel))
    return false;

  // Click
  return EmitClick(button, MouseAction::Click);
}

bool MouseController::EmitClick(int btnCode, MouseAction action) {
  if (!eventListener) {
    error("Cannot emit click: EventListener not initialized");
    return false;
  }

  // Send events through EventListener
  switch (action) {
  case MouseAction::Release:
    eventListener->SendUinputEvent(EV_KEY, btnCode, 0);
    eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
    return true;

  case MouseAction::Hold:
    eventListener->SendUinputEvent(EV_KEY, btnCode, 1);
    eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
    return true;

  case MouseAction::Click: // Click (FAST)
    eventListener->SendUinputEvent(EV_KEY, btnCode, 1);
    eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
    eventListener->SendUinputEvent(EV_KEY, btnCode, 0);
    eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
    return true;

  default:
    return false;
  }
}

bool MouseController::Scroll(double dy, double dx) {
  if (!eventListener) {
    error("Cannot scroll: EventListener not initialized");
    return false;
  }

  debug("MouseController::Scroll dy={} dx={} scrollSpeed={}", dy, dx,
        scrollSpeed);
  // Apply scroll speed and accumulate
  if (dy != 0.0) {
    scrollAccumY += dy * scrollSpeed;
  }
  if (dx != 0.0) {
    scrollAccumX += dx * scrollSpeed;
  }

  // Emit accumulated scroll values
  int emitY = static_cast<int>(scrollAccumY);
  int emitX = static_cast<int>(scrollAccumX);

  // Only emit if non-zero
  if (emitY != 0) {
    eventListener->SendUinputEvent(EV_REL, REL_WHEEL, emitY);
  }
  if (emitX != 0) {
    eventListener->SendUinputEvent(EV_REL, REL_HWHEEL, emitX);
  }

  // Subtract emitted values from accumulation
  scrollAccumY -= emitY;
  scrollAccumX -= emitX;

  // Sync event
  eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);

  return true;
}

void MouseController::SetSensitivity(double s) { sensitivity = s; }

void MouseController::SetScrollSpeed(double s) { scrollSpeed = s; }

std::pair<int, int> MouseController::GetPosition() {
  if (!eventListener) {
    error("Cannot get mouse position: EventListener not initialized");
    return {0, 0};
  }
  return eventListener->GetMousePosition();
}

} // namespace havel
