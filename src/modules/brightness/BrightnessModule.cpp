#include "BrightnessModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/brightness/BrightnessService.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::BrightnessService;

static const char* MODULE_MARKER = "__brightness_module";

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

static std::shared_ptr<BrightnessService> getService() {
  auto svc = host::ServiceRegistry::instance().get<BrightnessService>();
  if (!svc) debug("BrightnessModule: BrightnessService not available");
  return svc;
}

static double toDouble(const Value& v, double def = 0.0) {
  if (v.isDouble()) return v.asDouble();
  if (v.isInt()) return static_cast<double>(v.asInt());
  return def;
}

static int toInt(const Value& v, int def = 0) {
  if (v.isInt()) return v.asInt();
  if (v.isDouble()) return static_cast<int>(v.asDouble());
  return def;
}

static int monitorIndex(const std::vector<Value>& args, int pos = 0) {
  return !args.empty() && args[pos].isInt() ? args[pos].asInt() : -1;
}

static Value makeString(const VMApi& api, const std::string& s) {
  return api.makeString(s);
}

void registerBrightnessModule(const VMApi& api) {
  HAVEL_BEGIN_MODULE("Brightness");

  // brightness.get([monitor]) -> number
  HAVEL_REGISTER_FUNCTION(api, "brightness.get", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeDouble(0.0);
    int mi = monitorIndex(args);
    try { return Value::makeDouble(svc->getBrightness(mi)); }
    catch (const std::exception& e) { debug("brightness.get error: {}", e.what()); return Value::makeDouble(0.0); }
  });

  // brightness.set(value [, monitor])
  HAVEL_REGISTER_FUNCTION(api, "brightness.set", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    double level = toDouble(args[0], 0.5);
    int mi = args.size() > 1 ? monitorIndex(args, 1) : -1;
    try { svc->setBrightness(level, mi); }
    catch (const std::exception& e) { debug("brightness.set error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.increase([step [, monitor]]) -> number
  HAVEL_REGISTER_FUNCTION(api, "brightness.increase", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    double step = !args.empty() ? toDouble(args[0], 0.1) : 0.1;
    int mi = args.size() > 1 ? monitorIndex(args, 1) : -1;
    auto svc = getService();
    if (!svc) return Value::makeDouble(0.0);
    try { svc->increaseBrightness(step, mi); return Value::makeDouble(svc->getBrightness(mi)); }
    catch (const std::exception& e) { debug("brightness.increase error: {}", e.what()); return Value::makeDouble(0.0); }
  });

  // brightness.decrease([step [, monitor]]) -> number
  HAVEL_REGISTER_FUNCTION(api, "brightness.decrease", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    double step = !args.empty() ? toDouble(args[0], 0.1) : 0.1;
    int mi = args.size() > 1 ? monitorIndex(args, 1) : -1;
    auto svc = getService();
    if (!svc) return Value::makeDouble(0.0);
    try { svc->decreaseBrightness(step, mi); return Value::makeDouble(svc->getBrightness(mi)); }
    catch (const std::exception& e) { debug("brightness.decrease error: {}", e.what()); return Value::makeDouble(0.0); }
  });

  // brightness.getTemperature([monitor]) -> int
  HAVEL_REGISTER_FUNCTION(api, "brightness.getTemperature", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeInt(0);
    int mi = monitorIndex(args);
    try { return Value::makeInt(svc->getTemperature(mi)); }
    catch (const std::exception& e) { debug("brightness.getTemperature error: {}", e.what()); return Value::makeInt(0); }
  });

  // brightness.setTemperature(temp [, monitor])
  HAVEL_REGISTER_FUNCTION(api, "brightness.setTemperature", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    int temp = toInt(args[0], 6500);
    int mi = args.size() > 1 ? monitorIndex(args, 1) : -1;
    try { svc->setTemperature(temp, mi); }
    catch (const std::exception& e) { debug("brightness.setTemperature error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.increaseTemperature([amount [, monitor]])
  HAVEL_REGISTER_FUNCTION(api, "brightness.increaseTemperature", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    int amount = !args.empty() ? toInt(args[0], 200) : 200;
    int mi = args.size() > 1 ? monitorIndex(args, 1) : -1;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->increaseTemperature(amount, mi); }
    catch (const std::exception& e) { debug("brightness.increaseTemperature error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.decreaseTemperature([amount [, monitor]])
  HAVEL_REGISTER_FUNCTION(api, "brightness.decreaseTemperature", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    int amount = !args.empty() ? toInt(args[0], 200) : 200;
    int mi = args.size() > 1 ? monitorIndex(args, 1) : -1;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->decreaseTemperature(amount, mi); }
    catch (const std::exception& e) { debug("brightness.decreaseTemperature error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.setGammaRGB(red, green, blue [, monitor])
  HAVEL_REGISTER_FUNCTION(api, "brightness.setGammaRGB", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 3) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    double r = toDouble(args[0], 1.0);
    double g = toDouble(args[1], 1.0);
    double b = toDouble(args[2], 1.0);
    int mi = args.size() > 3 ? monitorIndex(args, 3) : -1;
    try { svc->setGammaRGB(r, g, b, mi); }
    catch (const std::exception& e) { debug("brightness.setGammaRGB error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.getGammaRGB([monitor]) -> {r, g, b}
  HAVEL_REGISTER_FUNCTION(api, "brightness.getGammaRGB", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeObject();
    int mi = monitorIndex(args);
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
    try { svc->getGammaRGB(r, g, b, mi); }
    catch (const std::exception& e) { debug("brightness.getGammaRGB error: {}", e.what()); }
    auto obj = api.makeObject();
    api.setField(obj, "r", Value::makeDouble(r));
    api.setField(obj, "g", Value::makeDouble(g));
    api.setField(obj, "b", Value::makeDouble(b));
    return obj;
  });

  // brightness.increaseGamma([amount [, monitor]])
  HAVEL_REGISTER_FUNCTION(api, "brightness.increaseGamma", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    int amount = !args.empty() ? toInt(args[0], 100) : 100;
    int mi = args.size() > 1 ? monitorIndex(args, 1) : -1;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->increaseGamma(amount, mi); }
    catch (const std::exception& e) { debug("brightness.increaseGamma error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.decreaseGamma([amount [, monitor]])
  HAVEL_REGISTER_FUNCTION(api, "brightness.decreaseGamma", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    int amount = !args.empty() ? toInt(args[0], 100) : 100;
    int mi = args.size() > 1 ? monitorIndex(args, 1) : -1;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->decreaseGamma(amount, mi); }
    catch (const std::exception& e) { debug("brightness.decreaseGamma error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.getShadowLift([monitor]) -> number
  HAVEL_REGISTER_FUNCTION(api, "brightness.getShadowLift", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeDouble(0.0);
    int mi = monitorIndex(args);
    try { return Value::makeDouble(svc->getShadowLift(mi)); }
    catch (const std::exception& e) { debug("brightness.getShadowLift error: {}", e.what()); return Value::makeDouble(0.0); }
  });

  // brightness.setShadowLift(lift [, monitor])
  HAVEL_REGISTER_FUNCTION(api, "brightness.setShadowLift", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    double lift = toDouble(args[0], 0.0);
    int mi = args.size() > 1 ? monitorIndex(args, 1) : -1;
    try { svc->setShadowLift(lift, mi); }
    catch (const std::exception& e) { debug("brightness.setShadowLift error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.increaseShadowLift([amount [, monitor]])
  HAVEL_REGISTER_FUNCTION(api, "brightness.increaseShadowLift", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    int amount = !args.empty() ? toInt(args[0], 10) : 10;
    int mi = args.size() > 1 ? monitorIndex(args, 1) : -1;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->increaseShadowLift(amount, mi); }
    catch (const std::exception& e) { debug("brightness.increaseShadowLift error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.decreaseShadowLift([amount [, monitor]])
  HAVEL_REGISTER_FUNCTION(api, "brightness.decreaseShadowLift", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    int amount = !args.empty() ? toInt(args[0], 10) : 10;
    int mi = args.size() > 1 ? monitorIndex(args, 1) : -1;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->decreaseShadowLift(amount, mi); }
    catch (const std::exception& e) { debug("brightness.decreaseShadowLift error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.setBrightnessAndRGB(brightness, red, green, blue [, monitor])
  HAVEL_REGISTER_FUNCTION(api, "brightness.setBrightnessAndRGB", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 4) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    double b = toDouble(args[0], 1.0);
    double r = toDouble(args[1], 1.0);
    double g = toDouble(args[2], 1.0);
    double bl = toDouble(args[3], 1.0);
    int mi = args.size() > 4 ? monitorIndex(args, 4) : -1;
    try { svc->setBrightnessAndRGB(b, r, g, bl, mi); }
    catch (const std::exception& e) { debug("brightness.setBrightnessAndRGB error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.setBrightnessAndTemperature(brightness, kelvin [, monitor])
  HAVEL_REGISTER_FUNCTION(api, "brightness.setBrightnessAndTemperature", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    double b = toDouble(args[0], 1.0);
    int k = toInt(args[1], 6500);
    int mi = args.size() > 2 ? monitorIndex(args, 2) : -1;
    try { svc->setBrightnessAndTemperature(b, k, mi); }
    catch (const std::exception& e) { debug("brightness.setBrightnessAndTemperature error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.setBrightnessAndShadowLift(brightness, shadowLift [, monitor])
  HAVEL_REGISTER_FUNCTION(api, "brightness.setBrightnessAndShadowLift", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    double b = toDouble(args[0], 1.0);
    double sl = toDouble(args[1], 0.0);
    int mi = args.size() > 2 ? monitorIndex(args, 2) : -1;
    try { svc->setBrightnessAndShadowLift(b, sl, mi); }
    catch (const std::exception& e) { debug("brightness.setBrightnessAndShadowLift error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.enableDayNightMode(dayBrightness, nightBrightness, dayTemp, nightTemp, dayStart, nightStart [, intervalMinutes])
  HAVEL_REGISTER_FUNCTION(api, "brightness.enableDayNightMode", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeNull();
    double db = args.size() > 0 ? toDouble(args[0], 1.0) : 1.0;
    double nb = args.size() > 1 ? toDouble(args[1], 0.3) : 0.3;
    int dt = args.size() > 2 ? toInt(args[2], 6500) : 6500;
    int nt = args.size() > 3 ? toInt(args[3], 3000) : 3000;
    int ds = args.size() > 4 ? toInt(args[4], 7) : 7;
    int ns = args.size() > 5 ? toInt(args[5], 20) : 20;
    int ci = args.size() > 6 ? toInt(args[6], 5) : 5;
    try { svc->enableDayNightMode(db, nb, dt, nt, ds, ns, ci); }
    catch (const std::exception& e) { debug("brightness.enableDayNightMode error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.disableDayNightMode()
  HAVEL_REGISTER_FUNCTION(api, "brightness.disableDayNightMode", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->disableDayNightMode(); }
    catch (const std::exception& e) { debug("brightness.disableDayNightMode error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.isDayNightModeEnabled() -> bool
  HAVEL_REGISTER_FUNCTION(api, "brightness.isDayNightModeEnabled", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->isDayNightModeEnabled()); }
    catch (const std::exception& e) { debug("brightness.isDayNightModeEnabled error: {}", e.what()); return Value::makeBool(false); }
  });

  // brightness.setDaySettings(brightness, temperature)
  HAVEL_REGISTER_FUNCTION(api, "brightness.setDaySettings", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    double b = toDouble(args[0], 1.0);
    int t = toInt(args[1], 6500);
    try { svc->setDaySettings(b, t); }
    catch (const std::exception& e) { debug("brightness.setDaySettings error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.setNightSettings(brightness, temperature)
  HAVEL_REGISTER_FUNCTION(api, "brightness.setNightSettings", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    double b = toDouble(args[0], 0.3);
    int t = toInt(args[1], 3000);
    try { svc->setNightSettings(b, t); }
    catch (const std::exception& e) { debug("brightness.setNightSettings error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.setDayNightTiming(dayStart, nightStart)
  HAVEL_REGISTER_FUNCTION(api, "brightness.setDayNightTiming", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    int ds = toInt(args[0], 7);
    int ns = toInt(args[1], 20);
    try { svc->setDayNightTiming(ds, ns); }
    catch (const std::exception& e) { debug("brightness.setDayNightTiming error: {}", e.what()); }
    return Value::makeNull();
  });

  // brightness.switchToDay([monitor])
  HAVEL_REGISTER_FUNCTION(api, "brightness.switchToDay", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    int mi = monitorIndex(args);
    try { return Value::makeBool(svc->switchToDay(mi)); }
    catch (const std::exception& e) { debug("brightness.switchToDay error: {}", e.what()); return Value::makeBool(false); }
  });

  // brightness.switchToNight([monitor])
  HAVEL_REGISTER_FUNCTION(api, "brightness.switchToNight", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    int mi = monitorIndex(args);
    try { return Value::makeBool(svc->switchToNight(mi)); }
    catch (const std::exception& e) { debug("brightness.switchToNight error: {}", e.what()); return Value::makeBool(false); }
  });

  // brightness.isDay() -> bool
  HAVEL_REGISTER_FUNCTION(api, "brightness.isDay", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->isDay()); }
    catch (const std::exception& e) { debug("brightness.isDay error: {}", e.what()); return Value::makeBool(false); }
  });

  // brightness.getMonitors() -> [string]
  HAVEL_REGISTER_FUNCTION(api, "brightness.getMonitors", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeArray();
    try {
      auto names = svc->getConnectedMonitors();
      auto arr = api.makeArray();
      for (auto& n : names) api.push(arr, makeString(api, n));
      return arr;
    }
    catch (const std::exception& e) { debug("brightness.getMonitors error: {}", e.what()); return api.makeArray(); }
  });

  // brightness.getMonitor(index) -> string
  HAVEL_REGISTER_FUNCTION(api, "brightness.getMonitor", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc || args.empty()) return makeString(api, "");
    int idx = toInt(args[0], 0);
    try { return makeString(api, svc->getMonitor(idx)); }
    catch (const std::exception& e) { debug("brightness.getMonitor error: {}", e.what()); return makeString(api, ""); }
  });

  auto obj = api.makeObject();
  api.setGlobal("brightness", obj);
  api.setField(obj, MODULE_MARKER, Value::makeBool(true));
  api.setField(obj, "get", api.makeFunctionRef("brightness.get"));
  api.setField(obj, "set", api.makeFunctionRef("brightness.set"));
  api.setField(obj, "increase", api.makeFunctionRef("brightness.increase"));
  api.setField(obj, "decrease", api.makeFunctionRef("brightness.decrease"));
  api.setField(obj, "getTemperature", api.makeFunctionRef("brightness.getTemperature"));
  api.setField(obj, "setTemperature", api.makeFunctionRef("brightness.setTemperature"));
  api.setField(obj, "increaseTemperature", api.makeFunctionRef("brightness.increaseTemperature"));
  api.setField(obj, "decreaseTemperature", api.makeFunctionRef("brightness.decreaseTemperature"));
  api.setField(obj, "setGammaRGB", api.makeFunctionRef("brightness.setGammaRGB"));
  api.setField(obj, "getGammaRGB", api.makeFunctionRef("brightness.getGammaRGB"));
  api.setField(obj, "increaseGamma", api.makeFunctionRef("brightness.increaseGamma"));
  api.setField(obj, "decreaseGamma", api.makeFunctionRef("brightness.decreaseGamma"));
  api.setField(obj, "getShadowLift", api.makeFunctionRef("brightness.getShadowLift"));
  api.setField(obj, "setShadowLift", api.makeFunctionRef("brightness.setShadowLift"));
  api.setField(obj, "increaseShadowLift", api.makeFunctionRef("brightness.increaseShadowLift"));
  api.setField(obj, "decreaseShadowLift", api.makeFunctionRef("brightness.decreaseShadowLift"));
  api.setField(obj, "setBrightnessAndRGB", api.makeFunctionRef("brightness.setBrightnessAndRGB"));
  api.setField(obj, "setBrightnessAndTemperature", api.makeFunctionRef("brightness.setBrightnessAndTemperature"));
  api.setField(obj, "setBrightnessAndShadowLift", api.makeFunctionRef("brightness.setBrightnessAndShadowLift"));
  api.setField(obj, "enableDayNightMode", api.makeFunctionRef("brightness.enableDayNightMode"));
  api.setField(obj, "disableDayNightMode", api.makeFunctionRef("brightness.disableDayNightMode"));
  api.setField(obj, "isDayNightModeEnabled", api.makeFunctionRef("brightness.isDayNightModeEnabled"));
  api.setField(obj, "setDaySettings", api.makeFunctionRef("brightness.setDaySettings"));
  api.setField(obj, "setNightSettings", api.makeFunctionRef("brightness.setNightSettings"));
  api.setField(obj, "setDayNightTiming", api.makeFunctionRef("brightness.setDayNightTiming"));
  api.setField(obj, "switchToDay", api.makeFunctionRef("brightness.switchToDay"));
  api.setField(obj, "switchToNight", api.makeFunctionRef("brightness.switchToNight"));
  api.setField(obj, "isDay", api.makeFunctionRef("brightness.isDay"));
  api.setField(obj, "getMonitors", api.makeFunctionRef("brightness.getMonitors"));
  api.setField(obj, "getMonitor", api.makeFunctionRef("brightness.getMonitor"));

  HAVEL_END_MODULE();
}

} // namespace havel::modules
