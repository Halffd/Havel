#include "AudioModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/audio/AudioService.hpp"
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

  HAVEL_END_MODULE();
}

} // namespace havel::modules
