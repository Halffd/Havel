#include "ModeModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/mode/ModeService.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::ModeService;

static const char* MODULE_MARKER = "__mode_module";

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

static std::shared_ptr<ModeService> getService() {
  auto svc = host::ServiceRegistry::instance().get<ModeService>();
  if (!svc) debug("ModeModule: ModeService not available");
  return svc;
}

static std::string toString(const VMApi& api, const Value& v) {
  if (v.isStringId() || v.isStringValId()) return api.toString(v);
  if (v.isNull()) return "";
  if (v.isInt()) return std::to_string(v.asInt());
  if (v.isDouble()) return std::to_string(v.asDouble());
  if (v.isBool()) return v.asBool() ? "true" : "false";
  return "";
}

void registerModeModule(const VMApi& api) {
  HAVEL_BEGIN_MODULE("Mode");

  HAVEL_REGISTER_FUNCTION(api, "mode.getCurrentMode", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getCurrentMode()); } catch (const std::exception& e) { debug("mode.getCurrentMode error: {}", e.what()); return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mode.getPreviousMode", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getPreviousMode()); } catch (const std::exception& e) { debug("mode.getPreviousMode error: {}", e.what()); return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mode.setMode", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->setMode(toString(api, args[0])); } catch (const std::exception& e) { debug("mode.setMode error: {}", e.what()); }
    return Value::makeNull();
  });

  auto obj = api.makeObject();
  api.setGlobal("mode", obj);
  api.setField(obj, MODULE_MARKER, Value::makeBool(true));
  api.setField(obj, "getCurrentMode", api.makeFunctionRef("mode.getCurrentMode"));
  api.setField(obj, "getPreviousMode", api.makeFunctionRef("mode.getPreviousMode"));
  api.setField(obj, "setMode", api.makeFunctionRef("mode.setMode"));

  HAVEL_END_MODULE();
}

} // namespace havel::modules
