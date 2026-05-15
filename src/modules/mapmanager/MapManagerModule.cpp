#include "MapManagerModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/io/MapManagerService.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::MapManagerService;

static const char* MODULE_MARKER = "__mapmanager_module";

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

static std::shared_ptr<MapManagerService> getService() {
  auto svc = host::ServiceRegistry::instance().get<MapManagerService>();
  if (!svc) debug("MapManagerModule: MapManagerService not available");
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

void registerMapManagerModule(const VMApi& api) {
  HAVEL_BEGIN_MODULE("MapManager");

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.map", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    int id = args.size() > 2 && args[2].isInt() ? args[2].asInt() : 0;
    try { return Value::makeBool(svc->map(toString(api, args[0]), toString(api, args[1]), id)); } catch (const std::exception& e) { debug("mapmanager.map error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.remap", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->remap(toString(api, args[0]), toString(api, args[1]))); } catch (const std::exception& e) { debug("mapmanager.remap error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.unmap", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->unmap(toString(api, args[0]))); } catch (const std::exception& e) { debug("mapmanager.unmap error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.clearAll", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->clearAll(); } catch (const std::exception& e) { debug("mapmanager.clearAll error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.enableAutofire", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    int intervalMs = args.size() > 1 && args[1].isInt() ? args[1].asInt() : 100;
    try { return Value::makeBool(svc->enableAutofire(toString(api, args[0]), intervalMs)); } catch (const std::exception& e) { debug("mapmanager.enableAutofire error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.disableAutofire", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->disableAutofire(toString(api, args[0]))); } catch (const std::exception& e) { debug("mapmanager.disableAutofire error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.setAutofireRate", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    int rateMs = args[1].isInt() ? args[1].asInt() : 100;
    try { return Value::makeBool(svc->setAutofireRate(toString(api, args[0]), rateMs)); } catch (const std::exception& e) { debug("mapmanager.setAutofireRate error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.enableTurbo", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    int intervalMs = args.size() > 1 && args[1].isInt() ? args[1].asInt() : 50;
    try { return Value::makeBool(svc->enableTurbo(toString(api, args[0]), intervalMs)); } catch (const std::exception& e) { debug("mapmanager.enableTurbo error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.disableTurbo", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->disableTurbo(toString(api, args[0]))); } catch (const std::exception& e) { debug("mapmanager.disableTurbo error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.createProfile", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->createProfile(toString(api, args[0]))); } catch (const std::exception& e) { debug("mapmanager.createProfile error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.switchProfile", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->switchProfile(toString(api, args[0]))); } catch (const std::exception& e) { debug("mapmanager.switchProfile error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.getCurrentProfile", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getCurrentProfile()); } catch (const std::exception& e) { debug("mapmanager.getCurrentProfile error: {}", e.what()); return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.getProfileNames", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeArray();
    try {
      auto names = svc->getProfileNames();
      auto arr = api.makeArray();
      for (const auto& n : names) api.push(arr, api.makeString(n));
      return arr;
    } catch (const std::exception& e) { debug("mapmanager.getProfileNames error: {}", e.what()); return api.makeArray(); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.addConditionalMapping", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 3) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->addConditionalMapping(toString(api, args[0]), toString(api, args[1]), toString(api, args[2]))); } catch (const std::exception& e) { debug("mapmanager.addConditionalMapping error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.isMapped", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->isMapped(toString(api, args[0]))); } catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "mapmanager.getMappingCount", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeInt(0);
    try { return Value::makeInt(svc->getMappingCount()); } catch (const std::exception& e) { return Value::makeInt(0); }
  });

  auto obj = api.makeObject();
  api.setGlobal("mapmanager", obj);
  api.setField(obj, MODULE_MARKER, Value::makeBool(true));
  api.setField(obj, "map", api.makeFunctionRef("mapmanager.map"));
  api.setField(obj, "remap", api.makeFunctionRef("mapmanager.remap"));
  api.setField(obj, "unmap", api.makeFunctionRef("mapmanager.unmap"));
  api.setField(obj, "clearAll", api.makeFunctionRef("mapmanager.clearAll"));
  api.setField(obj, "enableAutofire", api.makeFunctionRef("mapmanager.enableAutofire"));
  api.setField(obj, "disableAutofire", api.makeFunctionRef("mapmanager.disableAutofire"));
  api.setField(obj, "setAutofireRate", api.makeFunctionRef("mapmanager.setAutofireRate"));
  api.setField(obj, "enableTurbo", api.makeFunctionRef("mapmanager.enableTurbo"));
  api.setField(obj, "disableTurbo", api.makeFunctionRef("mapmanager.disableTurbo"));
  api.setField(obj, "createProfile", api.makeFunctionRef("mapmanager.createProfile"));
  api.setField(obj, "switchProfile", api.makeFunctionRef("mapmanager.switchProfile"));
  api.setField(obj, "getCurrentProfile", api.makeFunctionRef("mapmanager.getCurrentProfile"));
  api.setField(obj, "getProfileNames", api.makeFunctionRef("mapmanager.getProfileNames"));
  api.setField(obj, "addConditionalMapping", api.makeFunctionRef("mapmanager.addConditionalMapping"));
  api.setField(obj, "isMapped", api.makeFunctionRef("mapmanager.isMapped"));
  api.setField(obj, "getMappingCount", api.makeFunctionRef("mapmanager.getMappingCount"));

  HAVEL_END_MODULE();
}

} // namespace havel::modules
