#include "AudioModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/audio/AudioService.hpp"
#include "core/media/AudioManager.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::AudioService;

static const char* MODULE_MARKER = "__audio_module";

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

static std::shared_ptr<AudioService> getService() {
  auto svc = host::ServiceRegistry::instance().get<AudioService>();
  if (!svc) debug("AudioModule: AudioService not available");
  return svc;
}

static double toDouble(const Value& v, double def = 0.0) {
  if (v.isDouble()) return v.asDouble();
  if (v.isInt()) return static_cast<double>(v.asInt());
  return def;
}

static std::string valueToStr(const VMApi& api, const Value& v) {
  if (v.isNull()) return "";
  return api.toString(v);
}

static Value deviceToObject(const VMApi& api, const havel::AudioDevice& dev) {
  auto obj = api.makeObject();
  api.setField(obj, "name", api.makeString(dev.name));
  api.setField(obj, "description", api.makeString(dev.description));
  api.setField(obj, "index", Value::makeInt(static_cast<int64_t>(dev.index)));
  api.setField(obj, "isDefault", Value::makeBool(dev.isDefault));
  api.setField(obj, "isMuted", Value::makeBool(dev.isMuted));
  api.setField(obj, "volume", Value::makeDouble(dev.volume));
  api.setField(obj, "channels", Value::makeInt(static_cast<int64_t>(dev.channels)));
  return obj;
}

static Value appInfoToObject(const VMApi& api, const havel::AudioManager::ApplicationInfo& app) {
  auto obj = api.makeObject();
  api.setField(obj, "name", api.makeString(app.name));
  api.setField(obj, "icon", api.makeString(app.icon));
  api.setField(obj, "index", Value::makeInt(static_cast<int64_t>(app.index)));
  api.setField(obj, "volume", Value::makeDouble(app.volume));
  api.setField(obj, "isMuted", Value::makeBool(app.isMuted));
  return obj;
}

void registerAudioModule(const VMApi& api) {
  HAVEL_BEGIN_MODULE("Audio");

  HAVEL_REGISTER_FUNCTION(api, "audio.getVolume", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeDouble(0.0);
    try { return Value::makeDouble(svc->getVolume()); } catch (const std::exception& e) { debug("audio.getVolume error: {}", e.what()); return Value::makeDouble(0.0); }
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.setVolume", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->setVolume(toDouble(args[0], 0.5)); } catch (const std::exception& e) { debug("audio.setVolume error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.increaseVolume", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    double amount = !args.empty() ? toDouble(args[0], 0.05) : 0.05;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->increaseVolume(amount); } catch (const std::exception& e) { debug("audio.increaseVolume error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.decreaseVolume", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    double amount = !args.empty() ? toDouble(args[0], 0.05) : 0.05;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->decreaseVolume(amount); } catch (const std::exception& e) { debug("audio.decreaseVolume error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.toggleMute", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->toggleMute(); } catch (const std::exception& e) { debug("audio.toggleMute error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.setMute", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    bool muted = args[0].isBool() ? args[0].asBool() : true;
    try { svc->setMute(muted); } catch (const std::exception& e) { debug("audio.setMute error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.isMuted", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->isMuted()); } catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.getActiveAppVolume", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    auto svc = getService();
    if (!svc) return Value::makeDouble(0.0);
    try { return Value::makeDouble(svc->getActiveAppVolume()); } catch (const std::exception& e) { return Value::makeDouble(0.0); }
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.increaseActiveAppVolume", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    double amount = !args.empty() ? toDouble(args[0], 0.05) : 0.05;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->increaseActiveAppVolume(amount); } catch (const std::exception& e) { debug("audio.increaseActiveAppVolume error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.decreaseActiveAppVolume", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    double amount = !args.empty() ? toDouble(args[0], 0.05) : 0.05;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try { svc->decreaseActiveAppVolume(amount); } catch (const std::exception& e) { debug("audio.decreaseActiveAppVolume error: {}", e.what()); }
    return Value::makeNull();
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.getDevices", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    (void)args;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try {
      auto devices = svc->getAllDevices();
      auto arr = api.makeArray();
      for (const auto& dev : devices) {
        api.push(arr, deviceToObject(api, dev));
      }
      return arr;
    } catch (const std::exception& e) { return Value::makeNull(); }
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.findDeviceByName", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try {
      auto dev = svc->findDeviceByName(valueToStr(api, args[0]));
      if (dev.name.empty()) return Value::makeNull();
      return deviceToObject(api, dev);
    } catch (const std::exception& e) { return Value::makeNull(); }
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.findDeviceByIndex", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeNull();
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try {
      auto dev = svc->findDeviceByIndex(static_cast<uint32_t>(toDouble(args[0])));
      if (dev.name.empty()) return Value::makeNull();
      return deviceToObject(api, dev);
    } catch (const std::exception& e) { return Value::makeNull(); }
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.setDefaultOutput", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->setDefaultOutput(valueToStr(api, args[0]))); } catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.getDefaultOutput", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    (void)args;
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getDefaultOutput()); } catch (const std::exception& e) { return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.playTestSound", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    (void)args;
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->playTestSound()); } catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.playSound", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.empty()) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->playSound(valueToStr(api, args[0]))); } catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.getDefaultInput", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    (void)args;
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getDefaultInput()); } catch (const std::exception& e) { return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.getBackend", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    (void)args;
    auto svc = getService();
    if (!svc) return api.makeString("");
    try { return api.makeString(svc->getBackendName()); } catch (const std::exception& e) { return api.makeString(""); }
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.setApplicationVolume", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeBool(false);
    auto svc = getService();
    if (!svc) return Value::makeBool(false);
    try { return Value::makeBool(svc->setApplicationVolume(valueToStr(api, args[0]), toDouble(args[1]))); } catch (const std::exception& e) { return Value::makeBool(false); }
  });

  HAVEL_REGISTER_FUNCTION(api, "audio.getApplications", [api](const auto& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    (void)args;
    auto svc = getService();
    if (!svc) return Value::makeNull();
    try {
      auto apps = svc->getApplications();
      auto arr = api.makeArray();
      for (const auto& app : apps) {
        api.push(arr, appInfoToObject(api, app));
      }
      return arr;
    } catch (const std::exception& e) { return Value::makeNull(); }
  });

  auto obj = api.makeObject();
  api.setGlobal("audio", obj);
  api.setField(obj, MODULE_MARKER, Value::makeBool(true));
  api.setField(obj, "getVolume", api.makeFunctionRef("audio.getVolume"));
  api.setField(obj, "setVolume", api.makeFunctionRef("audio.setVolume"));
  api.setField(obj, "increaseVolume", api.makeFunctionRef("audio.increaseVolume"));
  api.setField(obj, "decreaseVolume", api.makeFunctionRef("audio.decreaseVolume"));
  api.setField(obj, "toggleMute", api.makeFunctionRef("audio.toggleMute"));
  api.setField(obj, "setMute", api.makeFunctionRef("audio.setMute"));
  api.setField(obj, "isMuted", api.makeFunctionRef("audio.isMuted"));
  api.setField(obj, "getActiveAppVolume", api.makeFunctionRef("audio.getActiveAppVolume"));
  api.setField(obj, "increaseActiveAppVolume", api.makeFunctionRef("audio.increaseActiveAppVolume"));
  api.setField(obj, "decreaseActiveAppVolume", api.makeFunctionRef("audio.decreaseActiveAppVolume"));
  api.setField(obj, "getDevices", api.makeFunctionRef("audio.getDevices"));
  api.setField(obj, "findDeviceByName", api.makeFunctionRef("audio.findDeviceByName"));
  api.setField(obj, "findDeviceByIndex", api.makeFunctionRef("audio.findDeviceByIndex"));
  api.setField(obj, "setDefaultOutput", api.makeFunctionRef("audio.setDefaultOutput"));
  api.setField(obj, "getDefaultOutput", api.makeFunctionRef("audio.getDefaultOutput"));
  api.setField(obj, "playTestSound", api.makeFunctionRef("audio.playTestSound"));
  api.setField(obj, "playSound", api.makeFunctionRef("audio.playSound"));
  api.setField(obj, "getDefaultInput", api.makeFunctionRef("audio.getDefaultInput"));
  api.setField(obj, "getBackend", api.makeFunctionRef("audio.getBackend"));
  api.setField(obj, "setApplicationVolume", api.makeFunctionRef("audio.setApplicationVolume"));
  api.setField(obj, "getApplications", api.makeFunctionRef("audio.getApplications"));

  HAVEL_END_MODULE();
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(audio, "1.0.0", "Audio operations module",
    havel::modules::registerAudioModule(*api);
)
#endif
