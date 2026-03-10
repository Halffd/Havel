/*
 * MouseController.cpp - Mouse movement and click control
 *
 * Handles all mouse-related operations including movement, clicking, and scrolling.
 * Separated from IO to break monolithic design.
 */
#include "MouseController.hpp"
#include "EventListener.hpp"
#include "utils/Logger.hpp"
#include <cmath>

namespace havel {

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
