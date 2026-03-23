/*
 * IOModule.cpp
 *
 * IO control module for Havel language.
 * Host binding - connects language to IO system.
 * 
 * THIN BINDING LAYER - Business logic is in IOService
 */
#include "IOModule.hpp"
#include "core/IO.hpp"
#include "window/WindowManager.hpp"
#include "host/io/IOService.hpp"
#include <algorithm>
#include <cctype>
#include <optional>

namespace havel::modules {

// Global storage for KeyTap instances to keep them alive
static std::vector<std::unique_ptr<KeyTap>> g_keyTapStorage;
static std::mutex g_keyTapMutex;

static std::string toLowerString(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

static std::optional<int> parseMouseButton(const HavelValue &value) {
  if (value.isNumber()) {
    return static_cast<int>(value.asNumber());
  }
  if (!value.isString()) {
    return std::nullopt;
  }

  std::string raw = toLowerString(value.asString());
  if (raw.empty()) {
    return std::nullopt;
  }

  bool isNumeric = std::all_of(raw.begin(), raw.end(), [](unsigned char c) {
    return std::isdigit(c) != 0;
  });
  if (isNumeric) {
    try {
      return std::stoi(raw);
    } catch (...) {
      return std::nullopt;
    }
  }

  if (raw == "left" || raw == "lmb" || raw == "button1")
    return 1;
  if (raw == "right" || raw == "rmb" || raw == "button2")
    return 2;
  if (raw == "middle" || raw == "mmb" || raw == "button3")
    return 3;
  if (raw == "wheelup" || raw == "scrollup" || raw == "button4")
    return 4;
  if (raw == "wheeldown" || raw == "scrolldown" || raw == "button5")
    return 5;

  return std::nullopt;
}

void registerModuleStub() {
    // STUBBED FOR BYTECODE VM MIGRATION
    // env removed
    // hostAPI removed

}

} // namespace havel::modules
