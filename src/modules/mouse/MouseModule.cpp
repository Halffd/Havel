/*
 * MouseModule.cpp - Mouse control module for bytecode VM
 * Provides mouse.click, mouse.move, mouse.scroll, etc.
 */
#include "MouseModule.hpp"
#include "host/mouse/MouseService.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include <cmath>
#include <sstream>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

// Helper to convert Value to string
static std::string toString(const Value &v) {
  if (v.isStringValId()) {
    // TODO: string pool lookup
    return "<string:" + std::to_string(v.asStringValId()) + ">";
  }
  if (v.isInt()) return std::to_string(v.asInt());
  if (v.isDouble()) {
    double val = v.asDouble();
    if (val == std::floor(val) && std::abs(val) < 1e15) {
      return std::to_string(static_cast<long long>(val));
    }
    std::ostringstream oss;
    oss.precision(15);
    oss << val;
    return oss.str();
  }
  if (v.isBool()) return v.asBool() ? "true" : "false";
  return "";
}

// Helper to convert Value to int
static int toInt(const Value &v) {
  if (v.isInt()) return static_cast<int>(v.asInt());
  if (v.isDouble()) return static_cast<int>(v.asDouble());
  if (v.isStringValId()) {
    // TODO: string pool lookup
    try { return std::stoi("0"); } catch (...) {}
  }
  return 0;
}

// Helper to convert Value to float
static float toFloat(const Value &v) {
  if (v.isDouble()) return static_cast<float>(v.asDouble());
  if (v.isInt()) return static_cast<float>(v.asInt());
  if (v.isStringValId()) {
    // TODO: string pool lookup
    try { return std::stof("1.0"); } catch (...) {}
  }
  return 1.0f;
}

// mouse.click(button, action)
static Value mouseClick(const std::vector<Value> &args) {
  if (args.empty()) {
    // Default: left click
    havel::host::MouseService::click(havel::host::MouseService::Button::Left);
    return Value::makeBool(true);
  }
  
  if (args.size() == 1) {
    // Just button, default action (click)
    const auto& arg = args[0];
    // TODO: string pool integration for string args
    // For now, assume left click
    havel::host::MouseService::click(havel::host::MouseService::Button::Left);
    return Value::makeBool(true);
  }
  
  // Button and action
  // TODO: string pool integration for string args
  // For now, assume left click
  havel::host::MouseService::click(havel::host::MouseService::Button::Left);
  
  return Value::makeBool(true);
}

// mouse.down(button) - press and hold
static Value mouseDown(const std::vector<Value> &args) {
  if (args.empty()) {
    havel::host::MouseService::press(havel::host::MouseService::Button::Left);
    return Value::makeBool(true);
  }
  
  // TODO: string pool integration for string args
  havel::host::MouseService::press(havel::host::MouseService::Button::Left);
  
  return Value::makeBool(true);
}

// mouse.up(button) - release
static Value mouseUp(const std::vector<Value> &args) {
  if (args.empty()) {
    havel::host::MouseService::release(havel::host::MouseService::Button::Left);
    return Value::makeBool(true);
  }
  
  // TODO: string pool integration for string args
  havel::host::MouseService::release(havel::host::MouseService::Button::Left);
  
  return Value::makeBool(true);
}

// mouse.move(x, y, speed, acceleration)
static Value mouseMove(const std::vector<Value> &args) {
  if (args.size() < 2) {
    throw std::runtime_error("mouse.move() requires at least x, y coordinates");
  }
  
  int x = toInt(args[0]);
  int y = toInt(args[1]);
  int speed = (args.size() > 2) ? toInt(args[2]) : 5;
  float accel = (args.size() > 3) ? toFloat(args[3]) : 1.0f;
  
  havel::host::MouseService::move(x, y, speed, accel);
  return Value::makeBool(true);
}

// mouse.moveRel(dx, dy, speed, acceleration)
static Value mouseMoveRel(const std::vector<Value> &args) {
  if (args.size() < 2) {
    throw std::runtime_error("mouse.moveRel() requires at least dx, dy");
  }
  
  int dx = toInt(args[0]);
  int dy = toInt(args[1]);
  int speed = (args.size() > 2) ? toInt(args[2]) : 5;
  float accel = (args.size() > 3) ? toFloat(args[3]) : 1.0f;
  
  havel::host::MouseService::moveRel(dx, dy, speed, accel);
  return Value::makeBool(true);
}

// mouse.scroll(dy, dx)
static Value mouseScroll(const std::vector<Value> &args) {
  if (args.empty()) {
    throw std::runtime_error("mouse.scroll() requires at least dy");
  }
  
  int dy = toInt(args[0]);
  int dx = (args.size() > 1) ? toInt(args[1]) : 0;
  
  havel::host::MouseService::scroll(dy, dx);
  return Value::makeBool(true);
}

// mouse.pos() - returns {x, y}
static Value mousePos(VMApi &api, const std::vector<Value> &args) {
  (void)args;
  
  auto [x, y] = havel::host::MouseService::pos();
  
  auto obj = api.makeObject();
  api.setField(obj, "x", Value::makeInt(static_cast<int64_t>(x)));
  api.setField(obj, "y", Value::makeInt(static_cast<int64_t>(y)));
  return obj;
}

// mouse.setSpeed(speed)
static Value mouseSetSpeed(const std::vector<Value> &args) {
  if (args.empty()) {
    throw std::runtime_error("mouse.setSpeed() requires a speed value");
  }
  
  int speed = toInt(args[0]);
  havel::host::MouseService::setSpeed(speed);
  return Value::makeBool(true);
}

// mouse.setAccel(accel)
static Value mouseSetAccel(const std::vector<Value> &args) {
  if (args.empty()) {
    throw std::runtime_error("mouse.setAccel() requires an acceleration value");
  }
  
  float accel = toFloat(args[0]);
  havel::host::MouseService::setAccel(accel);
  return Value::makeBool(true);
}

// mouse.setDPI(dpi)
static Value mouseSetDPI(const std::vector<Value> &args) {
  if (args.empty()) {
    throw std::runtime_error("mouse.setDPI() requires a DPI value");
  }
  
  int dpi = toInt(args[0]);
  havel::host::MouseService::setDPI(dpi);
  return Value::makeBool(true);
}

// Register mouse module with VM
void registerMouseModule(VMApi &api) {
  // mouse.click(button, action)
  api.registerFunction("mouse.click", [](const std::vector<Value> &args) {
    return mouseClick(args);
  });
  
  // mouse.down(button)
  api.registerFunction("mouse.down", [](const std::vector<Value> &args) {
    return mouseDown(args);
  });
  
  // mouse.up(button)
  api.registerFunction("mouse.up", [](const std::vector<Value> &args) {
    return mouseUp(args);
  });
  
  // mouse.move(x, y, speed, accel)
  api.registerFunction("mouse.move", [](const std::vector<Value> &args) {
    return mouseMove(args);
  });
  
  // mouse.moveRel(dx, dy, speed, accel)
  api.registerFunction("mouse.moveRel", [](const std::vector<Value> &args) {
    return mouseMoveRel(args);
  });
  
  // mouse.scroll(dy, dx)
  api.registerFunction("mouse.scroll", [](const std::vector<Value> &args) {
    return mouseScroll(args);
  });
  
  // mouse.pos() -> {x, y}
  api.registerFunction("mouse.pos", [&api](const std::vector<Value> &args) {
    return mousePos(api, args);
  });
  
  // mouse.setSpeed(speed)
  api.registerFunction("mouse.setSpeed", [](const std::vector<Value> &args) {
    return mouseSetSpeed(args);
  });
  
  // mouse.setAccel(accel)
  api.registerFunction("mouse.setAccel", [](const std::vector<Value> &args) {
    return mouseSetAccel(args);
  });
  
  // mouse.setDPI(dpi)
  api.registerFunction("mouse.setDPI", [](const std::vector<Value> &args) {
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
  api.registerFunction("click", [](const std::vector<Value> &args) {
    return mouseClick(args);
  });
  api.registerFunction("move", [](const std::vector<Value> &args) {
    return mouseMove(args);
  });
  api.registerFunction("moveRel", [](const std::vector<Value> &args) {
    return mouseMoveRel(args);
  });
  api.registerFunction("scroll", [](const std::vector<Value> &args) {
    return mouseScroll(args);
  });
}

} // namespace havel::modules
