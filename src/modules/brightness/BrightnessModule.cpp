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

void registerBrightnessModule(const VMApi& api) {
  HAVEL_BEGIN_MODULE("Brightness");

  HAVEL_REGISTER_FUNCTION(api, "brightness.get", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeDouble(0.0);
    int monitor = !args.empty() && args[0].isInt() ? args[0].asInt() : -1;
    try { return Value::makeDouble(svc->getBrightness(monitor)); } catch (const std::exception& e) { debug("brightness.get error: {}", e.what()); return Value::makeDouble(0.0); }
  });

  HAVEL_REGISTER_FUNCTION(api, "brightness.set", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    double level = toDouble(args[0], 0.5);
    int monitor = args.size() > 1 && args[1].isInt() ? args[1].asInt() : -1;
    try { svc->setBrightness(level, monitor); } catch (const std::exception& e) { debug("brightness.set error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "brightness.increase", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    double step = !args.empty() ? toDouble(args[0], 0.1) : 0.1;
    int monitor = args.size() > 1 && args[1].isInt() ? args[1].asInt() : -1;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->increaseBrightness(step, monitor); } catch (const std::exception& e) { debug("brightness.increase error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "brightness.decrease", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    double step = !args.empty() ? toDouble(args[0], 0.1) : 0.1;
    int monitor = args.size() > 1 && args[1].isInt() ? args[1].asInt() : -1;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->decreaseBrightness(step, monitor); } catch (const std::exception& e) { debug("brightness.decrease error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "brightness.getTemperature", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeInt(0);
    int monitor = !args.empty() && args[0].isInt() ? args[0].asInt() : -1;
    try { return Value::makeInt(svc->getTemperature(monitor)); } catch (const std::exception& e) { debug("brightness.getTemperature error: {}", e.what()); return Value::makeInt(0); }
  });

  HAVEL_REGISTER_FUNCTION(api, "brightness.setTemperature", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    int temp = args[0].isInt() ? args[0].asInt() : 6500;
    int monitor = args.size() > 1 && args[1].isInt() ? args[1].asInt() : -1;
    try { svc->setTemperature(temp, monitor); } catch (const std::exception& e) { debug("brightness.setTemperature error: {}", e.what()); }
    return Value::makeNull();
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

  HAVEL_END_MODULE();
}

} // namespace havel::modules
