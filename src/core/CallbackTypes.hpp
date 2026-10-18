#pragma once
#include "havel_platform.h"
// evdev's input_event/input_event codes. Nothing in this header actually
// refers to them - InputEvent and InputEventKind are declared below in
// terms of plain ints - so on Windows this include is simply absent rather
// than replaced. Kept (gated, not deleted) because Linux input paths
// include this header and may come to rely on it.
#if !HAVEL_PLATFORM_WINDOWS
#include <linux/input.h>
#endif
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
