#pragma once
#include <linux/input.h>
#include <functional>
#include <string>

namespace havel {

// Callback types for IO and EventListener
using AnyKeyPressCallback = std::function<void(const std::string& key)>;

enum class InputEventKind {
  Key,
  MouseButton,
  MouseMove,
  MouseWheel,
  Absolute
};

struct InputEvent {
  InputEventKind kind;
  int code = 0;
  int value = 0;
  bool down = false;
  bool repeat = false;
  int modifiers = 0;
  int originalCode = 0;
  int mappedCode = 0;
  int dx = 0;
  int dy = 0;
  std::string keyName;
};

using InputEventCallback = std::function<void(const InputEvent&)>;

} // namespace havel
