#include "AltTabModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/window/AltTabService.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::AltTabService;

static const char* MODULE_MARKER = "__alttab_module";

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

static std::shared_ptr<AltTabService> getService() {
  auto svc = host::ServiceRegistry::instance().get<AltTabService>();
  if (!svc) debug("AltTabModule: AltTabService not available");
  return svc;
}

static Value altTabInfoToObject(const VMApi& api, const host::AltTabInfo& info) {
  auto obj = api.makeObject();
  api.setField(obj, "title", api.makeString(info.title));
  api.setField(obj, "className", api.makeString(info.className));
  api.setField(obj, "processName", api.makeString(info.processName));
  api.setField(obj, "windowId", Value::makeInt(info.windowId));
  api.setField(obj, "active", Value::makeBool(info.active));
  return obj;
}

void registerAltTabModule(const VMApi& api) {
  HAVEL_BEGIN_MODULE("AltTab");

  HAVEL_REGISTER_FUNCTION(api, "alttab.show", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->show(); } catch (const std::exception& e) { debug("alttab.show error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.hide", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->hide(); } catch (const std::exception& e) { debug("alttab.hide error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.toggle", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->toggle(); } catch (const std::exception& e) { debug("alttab.toggle error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.next", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->next(); } catch (const std::exception& e) { debug("alttab.next error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.previous", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->previous(); } catch (const std::exception& e) { debug("alttab.previous error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.select", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->select(); } catch (const std::exception& e) { debug("alttab.select error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.refresh", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->refresh(); } catch (const std::exception& e) { debug("alttab.refresh error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.getWindows", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeArray();
    try {
      auto windows = svc->getWindows();
      auto arr = api.makeArray();
      for (const auto& w : windows) {
        api.push(arr, altTabInfoToObject(api, w));
      }
      return arr;
    } catch (const std::exception& e) { debug("alttab.getWindows error: {}", e.what()); return api.makeArray(); }
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.getWindowCount", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeInt(0);
    try { return Value::makeInt(svc->getWindowCount()); } catch (const std::exception& e) { return Value::makeInt(0); }
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.setThumbnailSize", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    int w = args[0].isInt() ? args[0].asInt() : 200;
    int h = args[1].isInt() ? args[1].asInt() : 150;
    try { svc->setThumbnailSize(w, h); } catch (const std::exception& e) { debug("alttab.setThumbnailSize error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.getThumbnailWidth", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeInt(0);
    try { return Value::makeInt(svc->getThumbnailWidth()); } catch (const std::exception& e) { return Value::makeInt(0); }
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.getThumbnailHeight", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeInt(0);
    try { return Value::makeInt(svc->getThumbnailHeight()); } catch (const std::exception& e) { return Value::makeInt(0); }
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.setMaxVisibleWindows", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    int count = args[0].isInt() ? args[0].asInt() : 10;
    try { svc->setMaxVisibleWindows(count); } catch (const std::exception& e) { debug("alttab.setMaxVisibleWindows error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.getMaxVisibleWindows", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeInt(0);
    try { return Value::makeInt(svc->getMaxVisibleWindows()); } catch (const std::exception& e) { return Value::makeInt(0); }
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.setAnimationsEnabled", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    bool enabled = args[0].isBool() ? args[0].asBool() : true;
    try { svc->setAnimationsEnabled(enabled); } catch (const std::exception& e) { debug("alttab.setAnimationsEnabled error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "alttab.isAnimationsEnabled", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->isAnimationsEnabled()); } catch (const std::exception& e) { return Value::makeBool(false); }
  });

  auto obj = api.makeObject();
  api.setGlobal("alttab", obj);
  api.setField(obj, MODULE_MARKER, Value::makeBool(true));
  api.setField(obj, "show", api.makeFunctionRef("alttab.show"));
  api.setField(obj, "hide", api.makeFunctionRef("alttab.hide"));
  api.setField(obj, "toggle", api.makeFunctionRef("alttab.toggle"));
  api.setField(obj, "next", api.makeFunctionRef("alttab.next"));
  api.setField(obj, "previous", api.makeFunctionRef("alttab.previous"));
  api.setField(obj, "select", api.makeFunctionRef("alttab.select"));
  api.setField(obj, "refresh", api.makeFunctionRef("alttab.refresh"));
  api.setField(obj, "getWindows", api.makeFunctionRef("alttab.getWindows"));
  api.setField(obj, "getWindowCount", api.makeFunctionRef("alttab.getWindowCount"));
  api.setField(obj, "setThumbnailSize", api.makeFunctionRef("alttab.setThumbnailSize"));
  api.setField(obj, "getThumbnailWidth", api.makeFunctionRef("alttab.getThumbnailWidth"));
  api.setField(obj, "getThumbnailHeight", api.makeFunctionRef("alttab.getThumbnailHeight"));
  api.setField(obj, "setMaxVisibleWindows", api.makeFunctionRef("alttab.setMaxVisibleWindows"));
  api.setField(obj, "getMaxVisibleWindows", api.makeFunctionRef("alttab.getMaxVisibleWindows"));
  api.setField(obj, "setAnimationsEnabled", api.makeFunctionRef("alttab.setAnimationsEnabled"));
  api.setField(obj, "isAnimationsEnabled", api.makeFunctionRef("alttab.isAnimationsEnabled"));

  HAVEL_END_MODULE();
}

} // namespace havel::modules
