#include "IOModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "core/io/IO.hpp"
#include "core/io/HotkeyExecutor.hpp"
#include "utils/Logger.hpp"
#include <thread>
#include <chrono>
#include <algorithm>
#include <cctype>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static const char* MODULE_MARKER = "__io_module";

static bool isModuleObject(const VMApi& api, const Value& val) {
  if (!val.isObjectId()) return false;
  auto marker = api.getField(val, MODULE_MARKER);
  return marker.isBool() && marker.asBool();
}

static std::vector<Value> stripReceiver(const VMApi& api, const std::vector<Value>& args) {
  if (!args.empty() && isModuleObject(api, args[0])) {
    return std::vector<Value>(args.begin() + 1, args.end());
  }
  return args;
}

static IO* getIO() {
  auto io = host::ServiceRegistry::instance().get<IO>();
  if (!io) debug("IOModule: IO not available");
  return io.get();
}

static std::string toString(const VMApi& api, const Value& v) {
  if (v.isStringId() || v.isStringValId()) return api.toString(v);
  if (v.isNull()) return "";
  if (v.isInt()) return std::to_string(v.asInt());
  if (v.isDouble()) return std::to_string(v.asDouble());
  if (v.isBool()) return v.asBool() ? "true" : "false";
  return "";
}

static double toDouble(const Value& v, double def = 0.0) {
  if (v.isDouble()) return v.asDouble();
  if (v.isInt()) return static_cast<double>(v.asInt());
  return def;
}

static ExecutorMode stringToExecutorMode(const std::string& mode) {
  std::string modeLower = mode;
  std::transform(modeLower.begin(), modeLower.end(), modeLower.begin(),
    [](unsigned char c){ return std::tolower(c); });
  if (modeLower == "executor") return ExecutorMode::Executor;
  if (modeLower == "sync") return ExecutorMode::Sync;
  if (modeLower == "thread") return ExecutorMode::Thread;
  return ExecutorMode::Scheduler;
}

static const char* executorModeToString(ExecutorMode mode) {
  switch (mode) {
    case ExecutorMode::Executor: return "executor";
    case ExecutorMode::Sync: return "sync";
    case ExecutorMode::Thread: return "thread";
    case ExecutorMode::Scheduler: return "scheduler";
    default: return "scheduler";
  }
}

void registerIOModule(const VMApi& api) {
  HAVEL_BEGIN_MODULE("IO");

  HAVEL_REGISTER_FUNCTION(api, "io.sendKeys", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { io->Send(toString(api, args[0]).c_str()); return Value::makeBool(true); }
    catch (const std::exception& e) { debug("io.sendKeys error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.sendKey", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try {
      auto key = toString(api, args[0]);
      io->SendX11Key(key, true);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      io->SendX11Key(key, false);
      return Value::makeBool(true);
    } catch (const std::exception& e) { debug("io.sendKey error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.keyDown", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { io->SendX11Key(toString(api, args[0]), true); return Value::makeBool(true); }
    catch (const std::exception& e) { debug("io.keyDown error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.keyUp", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { io->SendX11Key(toString(api, args[0]), false); return Value::makeBool(true); }
    catch (const std::exception& e) { debug("io.keyUp error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.map", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeBool(false);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { io->Map(toString(api, args[0]), toString(api, args[1])); return Value::makeBool(true); }
    catch (const std::exception& e) { debug("io.map error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.remap", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeBool(false);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { io->Remap(toString(api, args[0]), toString(api, args[1])); return Value::makeBool(true); }
    catch (const std::exception& e) { debug("io.remap error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.block", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeNull();
    try { io->EmergencyReleaseAllKeys(); } catch (const std::exception& e) { debug("io.block error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "io.unblock", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeNull();
    try { io->UngrabAll(); } catch (const std::exception& e) { debug("io.unblock error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "io.suspend", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { return Value::makeBool(io->Suspend()); }
    catch (const std::exception& e) { debug("io.suspend error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.resume", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try {
      if (io->isSuspended) return Value::makeBool(io->Resume());
      return Value::makeBool(true);
    } catch (const std::exception& e) { debug("io.resume error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.isSuspended", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { return Value::makeBool(io->IsSuspended()); }
    catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.grab", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeNull();
    try { io->EmergencyReleaseAllKeys(); } catch (const std::exception& e) { debug("io.grab error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "io.ungrab", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeNull();
    try { io->UngrabAll(); } catch (const std::exception& e) { debug("io.ungrab error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "io.emergencyRelease", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeNull();
    try { io->EmergencyReleaseAllKeys(); } catch (const std::exception& e) { debug("io.emergencyRelease error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "io.isKeyPressed", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { return Value::makeBool(io->IsKeyPressed(toString(api, args[0]))); }
    catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.isShiftPressed", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { return Value::makeBool(io->IsShiftPressed()); }
    catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.isCtrlPressed", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { return Value::makeBool(io->IsCtrlPressed()); }
    catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.isAltPressed", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { return Value::makeBool(io->IsAltPressed()); }
    catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.isWinPressed", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { return Value::makeBool(io->IsWinPressed()); }
    catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.getCurrentModifiers", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeInt(0);
    try { return Value::makeInt(io->GetCurrentModifiers()); }
    catch (const std::exception& e) { return Value::makeInt(0); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.setExecutorMode", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try {
      auto mode = stringToExecutorMode(toString(api, args[0]));
      io->SetExecutorMode(mode);
      return Value::makeBool(true);
    } catch (const std::exception& e) { debug("io.setExecutorMode error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.getExecutorMode", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return api.makeString("scheduler");
    try { return api.makeString(executorModeToString(io->GetExecutorMode())); }
    catch (const std::exception& e) { return api.makeString("scheduler"); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.mouseMove", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeBool(false);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    int dx = args[0].isInt() ? args[0].asInt() : 0;
    int dy = args[1].isInt() ? args[1].asInt() : 0;
    try { return Value::makeBool(io->MouseMove(dx, dy)); }
    catch (const std::exception& e) { debug("io.mouseMove error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.mouseMoveTo", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeBool(false);
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    int x = args[0].isInt() ? args[0].asInt() : 0;
    int y = args[1].isInt() ? args[1].asInt() : 0;
    int speed = args.size() > 2 && args[2].isInt() ? args[2].asInt() : 1;
    try { return Value::makeBool(io->MouseMoveTo(x, y, speed)); }
    catch (const std::exception& e) { debug("io.mouseMoveTo error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.mouseClick", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    int button = !args.empty() && args[0].isInt() ? args[0].asInt() : 1;
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { io->MouseClick(button); return Value::makeBool(true); }
    catch (const std::exception& e) { debug("io.mouseClick error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.mouseDoubleClick", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    int button = !args.empty() && args[0].isInt() ? args[0].asInt() : 1;
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try {
      io->MouseClick(button);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      io->MouseClick(button);
      return Value::makeBool(true);
    } catch (const std::exception& e) { debug("io.mouseDoubleClick error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.mousePress", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    int button = !args.empty() && args[0].isInt() ? args[0].asInt() : 1;
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { return Value::makeBool(io->MouseDown(button)); }
    catch (const std::exception& e) { debug("io.mousePress error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.mouseRelease", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    int button = !args.empty() && args[0].isInt() ? args[0].asInt() : 1;
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { return Value::makeBool(io->MouseUp(button)); }
    catch (const std::exception& e) { debug("io.mouseRelease error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.scroll", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    double dy = !args.empty() ? toDouble(args[0], 0.0) : 0.0;
    double dx = args.size() > 1 ? toDouble(args[1], 0.0) : 0.0;
    auto* io = getIO();
    if (!io) return Value::makeBool(false);
    try { return Value::makeBool(io->Scroll(dy, dx)); }
    catch (const std::exception& e) { debug("io.scroll error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.getMousePosition", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeNull();
    try {
      auto pos = io->GetMousePosition();
      auto arr = api.makeArray();
      api.push(arr, Value::makeInt(pos.first));
      api.push(arr, Value::makeInt(pos.second));
      return arr;
    } catch (const std::exception& e) { debug("io.getMousePosition error: {}", e.what()); return Value::makeNull(); }
  });

  HAVEL_REGISTER_FUNCTION(api, "io.setMouseSensitivity", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto* io = getIO();
    if (!io) return Value::makeNull();
    try { io->SetMouseSensitivity(toDouble(args[0], 1.0)); }
    catch (const std::exception& e) { debug("io.setMouseSensitivity error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "io.getMouseSensitivity", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto* io = getIO();
    if (!io) return Value::makeDouble(1.0);
    try { return Value::makeDouble(io->GetMouseSensitivity()); }
    catch (const std::exception& e) { return Value::makeDouble(1.0); }
  });

  auto obj = api.makeObject();
  api.setGlobal("io", obj);
  api.setField(obj, MODULE_MARKER, Value::makeBool(true));
  api.setField(obj, "sendKeys", api.makeFunctionRef("io.sendKeys"));
  api.setField(obj, "sendKey", api.makeFunctionRef("io.sendKey"));
  api.setField(obj, "keyDown", api.makeFunctionRef("io.keyDown"));
  api.setField(obj, "keyUp", api.makeFunctionRef("io.keyUp"));
  api.setField(obj, "map", api.makeFunctionRef("io.map"));
  api.setField(obj, "remap", api.makeFunctionRef("io.remap"));
  api.setField(obj, "block", api.makeFunctionRef("io.block"));
  api.setField(obj, "unblock", api.makeFunctionRef("io.unblock"));
  api.setField(obj, "suspend", api.makeFunctionRef("io.suspend"));
  api.setField(obj, "resume", api.makeFunctionRef("io.resume"));
  api.setField(obj, "isSuspended", api.makeFunctionRef("io.isSuspended"));
  api.setField(obj, "grab", api.makeFunctionRef("io.grab"));
  api.setField(obj, "ungrab", api.makeFunctionRef("io.ungrab"));
  api.setField(obj, "emergencyRelease", api.makeFunctionRef("io.emergencyRelease"));
  api.setField(obj, "isKeyPressed", api.makeFunctionRef("io.isKeyPressed"));
  api.setField(obj, "isShiftPressed", api.makeFunctionRef("io.isShiftPressed"));
  api.setField(obj, "isCtrlPressed", api.makeFunctionRef("io.isCtrlPressed"));
  api.setField(obj, "isAltPressed", api.makeFunctionRef("io.isAltPressed"));
  api.setField(obj, "isWinPressed", api.makeFunctionRef("io.isWinPressed"));
  api.setField(obj, "getCurrentModifiers", api.makeFunctionRef("io.getCurrentModifiers"));
  api.setField(obj, "setExecutorMode", api.makeFunctionRef("io.setExecutorMode"));
  api.setField(obj, "getExecutorMode", api.makeFunctionRef("io.getExecutorMode"));
  api.setField(obj, "mouseMove", api.makeFunctionRef("io.mouseMove"));
  api.setField(obj, "mouseMoveTo", api.makeFunctionRef("io.mouseMoveTo"));
  api.setField(obj, "mouseClick", api.makeFunctionRef("io.mouseClick"));
  api.setField(obj, "mouseDoubleClick", api.makeFunctionRef("io.mouseDoubleClick"));
  api.setField(obj, "mousePress", api.makeFunctionRef("io.mousePress"));
  api.setField(obj, "mouseRelease", api.makeFunctionRef("io.mouseRelease"));
  api.setField(obj, "scroll", api.makeFunctionRef("io.scroll"));
  api.setField(obj, "getMousePosition", api.makeFunctionRef("io.getMousePosition"));
  api.setField(obj, "setMouseSensitivity", api.makeFunctionRef("io.setMouseSensitivity"));
  api.setField(obj, "getMouseSensitivity", api.makeFunctionRef("io.getMouseSensitivity"));

  HAVEL_END_MODULE();
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(io, "1.0.0", "IO operations module",
  havel::modules::registerIOModule(*api);
)
#endif
