#include "AppModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/app/AppService.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::AppService;

static const char* MODULE_MARKER = "__app_module";

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

static std::shared_ptr<AppService> getService() {
  auto svc = host::ServiceRegistry::instance().get<AppService>();
  if (!svc) debug("AppModule: AppService not available");
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

void registerAppModule(const VMApi& api) {
  HAVEL_BEGIN_MODULE("App");

  HAVEL_REGISTER_FUNCTION(api, "app.getAppName", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getAppName()); } catch (const std::exception& e) { debug("app.getAppName error: {}", e.what()); return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.getVersion", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getAppVersion()); } catch (const std::exception& e) { debug("app.getVersion error: {}", e.what()); return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.getDir", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getAppDir()); } catch (const std::exception& e) { debug("app.getDir error: {}", e.what()); return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.getOS", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getOS()); } catch (const std::exception& e) { debug("app.getOS error: {}", e.what()); return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.getHostname", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getHostname()); } catch (const std::exception& e) { debug("app.getHostname error: {}", e.what()); return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.getUsername", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getUsername()); } catch (const std::exception& e) { debug("app.getUsername error: {}", e.what()); return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.getHomeDir", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getHomeDir()); } catch (const std::exception& e) { debug("app.getHomeDir error: {}", e.what()); return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.getCpuCores", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeInt(0);
    try { return Value::makeInt(svc->getCpuCores()); } catch (const std::exception& e) { return Value::makeInt(0); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.getTotalMemory", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeInt(0);
    try { return Value::makeInt(static_cast<int64_t>(svc->getTotalMemory())); } catch (const std::exception& e) { return Value::makeInt(0); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.getSystemInfo", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try {
      auto info = svc->getSystemInfo();
      auto obj = api.makeObject();
      api.setField(obj, "os", api.makeString(info.os));
      api.setField(obj, "osVersion", api.makeString(info.osVersion));
      api.setField(obj, "hostname", api.makeString(info.hostname));
      api.setField(obj, "username", api.makeString(info.username));
      api.setField(obj, "homeDir", api.makeString(info.homeDir));
      api.setField(obj, "cpuCores", Value::makeInt(info.cpuCores));
      api.setField(obj, "totalMemory", Value::makeInt(static_cast<int64_t>(info.totalMemory)));
      return obj;
    } catch (const std::exception& e) { debug("app.getSystemInfo error: {}", e.what()); return Value::makeNull(); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.getEnv", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return api.makeString("");
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getEnv(toString(api, args[0]))); } catch (const std::exception& e) { debug("app.getEnv error: {}", e.what()); return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.setEnv", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->setEnv(toString(api, args[0]), toString(api, args[1]))); } catch (const std::exception& e) { debug("app.setEnv error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.getEnvVars", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeArray();
    try {
      auto vars = svc->getEnvVars();
      auto arr = api.makeArray();
      for (const auto& v : vars) api.push(arr, api.makeString(v));
      return arr;
    } catch (const std::exception& e) { debug("app.getEnvVars error: {}", e.what()); return api.makeArray(); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.openUrl", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->openUrl(toString(api, args[0]))); } catch (const std::exception& e) { debug("app.openUrl error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.openFile", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->openFile(toString(api, args[0]))); } catch (const std::exception& e) { debug("app.openFile error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.showInFolder", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->showInFolder(toString(api, args[0]))); } catch (const std::exception& e) { debug("app.showInFolder error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.copyToClipboard", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->copyToClipboard(toString(api, args[0]))); } catch (const std::exception& e) { debug("app.copyToClipboard error: {}", e.what()); return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "app.getClipboardText", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getClipboardText()); } catch (const std::exception& e) { debug("app.getClipboardText error: {}", e.what()); return api.makeString(""); }
  });

  auto obj = api.makeObject();
  api.setGlobal("app", obj);
  api.setField(obj, MODULE_MARKER, Value::makeBool(true));
  api.setField(obj, "getAppName", api.makeFunctionRef("app.getAppName"));
  api.setField(obj, "getVersion", api.makeFunctionRef("app.getVersion"));
  api.setField(obj, "getDir", api.makeFunctionRef("app.getDir"));
  api.setField(obj, "getOS", api.makeFunctionRef("app.getOS"));
  api.setField(obj, "getHostname", api.makeFunctionRef("app.getHostname"));
  api.setField(obj, "getUsername", api.makeFunctionRef("app.getUsername"));
  api.setField(obj, "getHomeDir", api.makeFunctionRef("app.getHomeDir"));
  api.setField(obj, "getCpuCores", api.makeFunctionRef("app.getCpuCores"));
  api.setField(obj, "getTotalMemory", api.makeFunctionRef("app.getTotalMemory"));
  api.setField(obj, "getSystemInfo", api.makeFunctionRef("app.getSystemInfo"));
  api.setField(obj, "getEnv", api.makeFunctionRef("app.getEnv"));
  api.setField(obj, "setEnv", api.makeFunctionRef("app.setEnv"));
  api.setField(obj, "getEnvVars", api.makeFunctionRef("app.getEnvVars"));
  api.setField(obj, "openUrl", api.makeFunctionRef("app.openUrl"));
  api.setField(obj, "openFile", api.makeFunctionRef("app.openFile"));
  api.setField(obj, "showInFolder", api.makeFunctionRef("app.showInFolder"));
  api.setField(obj, "copyToClipboard", api.makeFunctionRef("app.copyToClipboard"));
  api.setField(obj, "getClipboardText", api.makeFunctionRef("app.getClipboardText"));

  HAVEL_END_MODULE();
}

} // namespace havel::modules
