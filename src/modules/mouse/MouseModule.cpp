/*
 * MouseModule.cpp - Mouse control module for bytecode VM
 * Provides mouse.click, mouse.move, mouse.scroll, etc.
 */
#include "MouseModule.hpp"
#include "host/mouse/MouseService.hpp"
#include "havel-lang/compiler/bytecode/VMApi.hpp"
#include <cmath>
#include <sstream>

namespace havel::modules {

using compiler::BytecodeValue;
using compiler::VMApi;

// Helper to convert BytecodeValue to string
static std::string toString(const BytecodeValue &v) {
  if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
  if (std::holds_alternative<int64_t>(v)) return std::to_string(std::get<int64_t>(v));
  if (std::holds_alternative<double>(v)) {
    double val = std::get<double>(v);
    if (val == std::floor(val) && std::abs(val) < 1e15) {
      return std::to_string(static_cast<long long>(val));
    }
    std::ostringstream oss;
    oss.precision(15);
    oss << val;
    return oss.str();
  }
  if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? "true" : "false";
  return "";
}

// Helper to convert BytecodeValue to int
static int toInt(const BytecodeValue &v) {
  if (std::holds_alternative<int64_t>(v)) return static_cast<int>(std::get<int64_t>(v));
  if (std::holds_alternative<double>(v)) return static_cast<int>(std::get<double>(v));
  if (std::holds_alternative<std::string>(v)) {
    try { return std::stoi(std::get<std::string>(v)); } catch (...) {}
  }
  return 0;
}

// Helper to convert BytecodeValue to float
static float toFloat(const BytecodeValue &v) {
  if (std::holds_alternative<double>(v)) return static_cast<float>(std::get<double>(v));
  if (std::holds_alternative<int64_t>(v)) return static_cast<float>(std::get<int64_t>(v));
  if (std::holds_alternative<std::string>(v)) {
    try { return std::stof(std::get<std::string>(v)); } catch (...) {}
  }
  return 1.0f;
}

// mouse.click(button, action)
static BytecodeValue mouseClick(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    // Default: left click
    havel::host::MouseService::click(havel::host::MouseService::Button::Left);
    return BytecodeValue(true);
  }
  
  if (args.size() == 1) {
    // Just button, default action (click)
    const auto& arg = args[0];
    if (std::holds_alternative<std::string>(arg)) {
      havel::host::MouseService::click(std::get<std::string>(arg));
    } else if (std::holds_alternative<int64_t>(arg)) {
      havel::host::MouseService::click(static_cast<int>(std::get<int64_t>(arg)));
    }
    return BytecodeValue(true);
  }
  
  // Button and action
  const auto& buttonArg = args[0];
  const auto& actionArg = args[1];
  
  if (std::holds_alternative<std::string>(buttonArg) && std::holds_alternative<std::string>(actionArg)) {
    havel::host::MouseService::click(std::get<std::string>(buttonArg), std::get<std::string>(actionArg));
  } else if (std::holds_alternative<int64_t>(buttonArg) && std::holds_alternative<int64_t>(actionArg)) {
    havel::host::MouseService::click(static_cast<int>(std::get<int64_t>(buttonArg)), 
                                      static_cast<int>(std::get<int64_t>(actionArg)));
  } else if (std::holds_alternative<std::string>(buttonArg)) {
    havel::host::MouseService::click(std::get<std::string>(buttonArg));
  } else if (std::holds_alternative<int64_t>(buttonArg)) {
    havel::host::MouseService::click(static_cast<int>(std::get<int64_t>(buttonArg)));
  }
  
  return BytecodeValue(true);
}

// mouse.down(button) - press and hold
static BytecodeValue mouseDown(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    havel::host::MouseService::press(havel::host::MouseService::Button::Left);
    return BytecodeValue(true);
  }
  
  const auto& arg = args[0];
  if (std::holds_alternative<std::string>(arg)) {
    havel::host::MouseService::press(std::get<std::string>(arg));
  } else if (std::holds_alternative<int64_t>(arg)) {
    havel::host::MouseService::press(static_cast<int>(std::get<int64_t>(arg)));
  }
  
  return BytecodeValue(true);
}

// mouse.up(button) - release
static BytecodeValue mouseUp(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    havel::host::MouseService::release(havel::host::MouseService::Button::Left);
    return BytecodeValue(true);
  }
  
  const auto& arg = args[0];
  if (std::holds_alternative<std::string>(arg)) {
    havel::host::MouseService::release(std::get<std::string>(arg));
  } else if (std::holds_alternative<int64_t>(arg)) {
    havel::host::MouseService::release(static_cast<int>(std::get<int64_t>(arg)));
  }
  
  return BytecodeValue(true);
}

// mouse.move(x, y, speed, acceleration)
static BytecodeValue mouseMove(const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) {
    throw std::runtime_error("mouse.move() requires at least x, y coordinates");
  }
  
  int x = toInt(args[0]);
  int y = toInt(args[1]);
  int speed = (args.size() > 2) ? toInt(args[2]) : 5;
  float accel = (args.size() > 3) ? toFloat(args[3]) : 1.0f;
  
  havel::host::MouseService::move(x, y, speed, accel);
  return BytecodeValue(true);
}

// mouse.moveRel(dx, dy, speed, acceleration)
static BytecodeValue mouseMoveRel(const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) {
    throw std::runtime_error("mouse.moveRel() requires at least dx, dy");
  }
  
  int dx = toInt(args[0]);
  int dy = toInt(args[1]);
  int speed = (args.size() > 2) ? toInt(args[2]) : 5;
  float accel = (args.size() > 3) ? toFloat(args[3]) : 1.0f;
  
  havel::host::MouseService::moveRel(dx, dy, speed, accel);
  return BytecodeValue(true);
}

// mouse.scroll(dy, dx)
static BytecodeValue mouseScroll(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("mouse.scroll() requires at least dy");
  }
  
  int dy = toInt(args[0]);
  int dx = (args.size() > 1) ? toInt(args[1]) : 0;
  
  havel::host::MouseService::scroll(dy, dx);
  return BytecodeValue(true);
}

// mouse.pos() - returns {x, y}
static BytecodeValue mousePos(VMApi &api, const std::vector<BytecodeValue> &args) {
  (void)args;
  
  auto [x, y] = havel::host::MouseService::pos();
  
  auto obj = api.makeObject();
  api.setField(obj, "x", BytecodeValue(static_cast<int64_t>(x)));
  api.setField(obj, "y", BytecodeValue(static_cast<int64_t>(y)));
  return BytecodeValue(obj);
}

// mouse.setSpeed(speed)
static BytecodeValue mouseSetSpeed(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("mouse.setSpeed() requires a speed value");
  }
  
  int speed = toInt(args[0]);
  havel::host::MouseService::setSpeed(speed);
  return BytecodeValue(true);
}

// mouse.setAccel(accel)
static BytecodeValue mouseSetAccel(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("mouse.setAccel() requires an acceleration value");
  }
  
  float accel = toFloat(args[0]);
  havel::host::MouseService::setAccel(accel);
  return BytecodeValue(true);
}

// mouse.setDPI(dpi)
static BytecodeValue mouseSetDPI(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("mouse.setDPI() requires a DPI value");
  }
  
  int dpi = toInt(args[0]);
  havel::host::MouseService::setDPI(dpi);
  return BytecodeValue(true);
}

// Register mouse module with VM
void registerMouseModule(VMApi &api) {
  // mouse.click(button, action)
  api.registerFunction("mouse.click", [](const std::vector<BytecodeValue> &args) {
    return mouseClick(args);
  });
  
  // mouse.down(button)
  api.registerFunction("mouse.down", [](const std::vector<BytecodeValue> &args) {
    return mouseDown(args);
  });
  
  // mouse.up(button)
  api.registerFunction("mouse.up", [](const std::vector<BytecodeValue> &args) {
    return mouseUp(args);
  });
  
  // mouse.move(x, y, speed, accel)
  api.registerFunction("mouse.move", [](const std::vector<BytecodeValue> &args) {
    return mouseMove(args);
  });
  
  // mouse.moveRel(dx, dy, speed, accel)
  api.registerFunction("mouse.moveRel", [](const std::vector<BytecodeValue> &args) {
    return mouseMoveRel(args);
  });
  
  // mouse.scroll(dy, dx)
  api.registerFunction("mouse.scroll", [](const std::vector<BytecodeValue> &args) {
    return mouseScroll(args);
  });
  
  // mouse.pos() -> {x, y}
  api.registerFunction("mouse.pos", [&api](const std::vector<BytecodeValue> &args) {
    return mousePos(api, args);
  });
  
  // mouse.setSpeed(speed)
  api.registerFunction("mouse.setSpeed", [](const std::vector<BytecodeValue> &args) {
    return mouseSetSpeed(args);
  });
  
  // mouse.setAccel(accel)
  api.registerFunction("mouse.setAccel", [](const std::vector<BytecodeValue> &args) {
    return mouseSetAccel(args);
  });
  
  // mouse.setDPI(dpi)
  api.registerFunction("mouse.setDPI", [](const std::vector<BytecodeValue> &args) {
    return mouseSetDPI(args);
  });
  
  // Register mouse global object with methods
  auto mouseObj = api.makeObject();
  api.setField(mouseObj, "click", api.makeFunctionRef("mouse.click"));
  api.setField(mouseObj, "down", api.makeFunctionRef("mouse.down"));
  api.setField(mouseObj, "up", api.makeFunctionRef("mouse.up"));
  api.setField(mouseObj, "move", api.makeFunctionRef("mouse.move"));
  api.setField(mouseObj, "moveRel", api.makeFunctionRef("mouse.moveRel"));
  api.setField(mouseObj, "scroll", api.makeFunctionRef("mouse.scroll"));
  api.setField(mouseObj, "pos", api.makeFunctionRef("mouse.pos"));
  api.setField(mouseObj, "setSpeed", api.makeFunctionRef("mouse.setSpeed"));
  api.setField(mouseObj, "setAccel", api.makeFunctionRef("mouse.setAccel"));
  api.setField(mouseObj, "setDPI", api.makeFunctionRef("mouse.setDPI"));
  api.setGlobal("mouse", mouseObj);
  
  // Register global convenience aliases
  api.registerFunction("click", [](const std::vector<BytecodeValue> &args) {
    return mouseClick(args);
  });
  api.registerFunction("move", [](const std::vector<BytecodeValue> &args) {
    return mouseMove(args);
  });
  api.registerFunction("moveRel", [](const std::vector<BytecodeValue> &args) {
    return mouseMoveRel(args);
  });
  api.registerFunction("scroll", [](const std::vector<BytecodeValue> &args) {
    return mouseScroll(args);
  });
}

} // namespace havel::modules
