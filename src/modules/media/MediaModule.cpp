/*
 * MediaModule.cpp
 *
 * Media control module for Havel language.
 * Host binding - connects language to MPVController via HavelApp.
 */
#include "../../havel-lang/runtime/Environment.hpp"
#include "gui/HavelApp.hpp"

namespace havel::modules {

void registerMediaModule(Environment &env, std::shared_ptr<IHostAPI>) {
  auto app = HavelApp::instance;
  if (!app) {
    return; // Skip if HavelApp not available
  }

  // Create media module object
  auto mediaObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Helper to check if MPV is available
  auto checkMpv = [app]() -> bool { return app && app->mpv; };

  // =========================================================================
  // Media control functions
  // =========================================================================

  (*mediaObj)["play"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (checkMpv()) {
          app->mpv->PlayPause();
          return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
      }));

  (*mediaObj)["pause"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (checkMpv()) {
          app->mpv->PlayPause();
          return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
      }));

  (*mediaObj)["stop"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (checkMpv()) {
          app->mpv->Stop();
          return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
      }));

  (*mediaObj)["next"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (checkMpv()) {
          app->mpv->Next();
          return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
      }));

  (*mediaObj)["previous"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (checkMpv()) {
          app->mpv->Previous();
          return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
      }));

  // =========================================================================
  // MPV Controller functions
  // =========================================================================

  auto mpvObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();

  (*mpvObj)["volumeUp"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (checkMpv()) {
          app->mpv->VolumeUp();
          return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
      }));

  (*mpvObj)["volumeDown"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (checkMpv()) {
          app->mpv->VolumeDown();
          return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
      }));

  (*mpvObj)["setVolume"] = HavelValue(BuiltinFunction(
      [checkMpv](const std::vector<HavelValue> &args) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        // Note: MPVController doesn't have SetVolume, use volume up/down
        // instead
        if (args.empty()) {
          return HavelRuntimeError("setVolume() requires volume value");
        }
        double volume = args[0].asNumber();
        (void)volume; // Suppress unused warning
        // Stub - actual volume control would need MPVController extension
        return HavelValue(true);
      }));

  (*mpvObj)["getVolume"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        std::string volumeStr = app->mpv->GetProperty("volume");
        if (!volumeStr.empty()) {
          try {
            double volume = std::stod(volumeStr);
            return HavelValue(volume);
          } catch (...) {
            return HavelValue(100.0); // Default if parsing fails
          }
        }
        return HavelValue(100.0); // Default if no response
      }));

  (*mpvObj)["seek"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &args) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        if (args.empty()) {
          return HavelRuntimeError("seek() requires position");
        }
        double position = args[0].asNumber();
        app->mpv->Seek(static_cast<int>(position));
        return HavelValue(true);
      }));

  (*mpvObj)["getPosition"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        std::string posStr = app->mpv->GetProperty("time-pos");
        if (!posStr.empty()) {
          try {
            double position = std::stod(posStr);
            return HavelValue(position);
          } catch (...) {
            return HavelValue(0.0); // Default if parsing fails
          }
        }
        return HavelValue(0.0); // Default if no response
      }));

  (*mpvObj)["getDuration"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        std::string durStr = app->mpv->GetProperty("duration");
        if (!durStr.empty()) {
          try {
            double duration = std::stod(durStr);
            return HavelValue(duration);
          } catch (...) {
            return HavelValue(0.0); // Default if parsing fails
          }
        }
        return HavelValue(0.0); // Default if no response
      }));

  (*mpvObj)["loadFile"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &args) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        if (args.empty()) {
          return HavelRuntimeError("loadFile() requires file path");
        }
        std::string path = args[0].asString();
        // Use sendRaw to load file via MPV command
        app->mpv->SendRaw("{\"command\":[\"loadfile\",\"" + path + "\"]}");
        return HavelValue(true);
      }));

  // Additional MPV functions
  (*mpvObj)["toggleMute"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (checkMpv()) {
          app->mpv->ToggleMute();
          return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
      }));

  (*mpvObj)["stop"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (checkMpv()) {
          app->mpv->Stop();
          return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
      }));

  (*mpvObj)["next"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (checkMpv()) {
          app->mpv->Next();
          return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
      }));

  (*mpvObj)["previous"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (checkMpv()) {
          app->mpv->Previous();
          return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
      }));

  (*mpvObj)["addSpeed"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &args) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        if (args.empty()) {
          return HavelRuntimeError("addSpeed() requires delta value");
        }
        double delta = args[0].asNumber();
        app->mpv->AddSpeed(delta);
        return HavelValue(true);
      }));

  (*mpvObj)["addSubScale"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &args) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        if (args.empty()) {
          return HavelRuntimeError("addSubScale() requires delta value");
        }
        double delta = args[0].asNumber();
        app->mpv->AddSubScale(delta);
        return HavelValue(true);
      }));

  (*mpvObj)["addSubDelay"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &args) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        if (args.empty()) {
          return HavelRuntimeError("addSubDelay() requires delta value");
        }
        double delta = args[0].asNumber();
        app->mpv->AddSubDelay(delta);
        return HavelValue(true);
      }));

  (*mpvObj)["subSeek"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &args) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        if (args.empty()) {
          return HavelRuntimeError("subSeek() requires index");
        }
        int index = static_cast<int>(args[0].asNumber());
        app->mpv->SubSeek(index);
        return HavelValue(true);
      }));

  (*mpvObj)["cycle"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &args) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        if (args.empty()) {
          return HavelRuntimeError("cycle() requires property name");
        }
        std::string prop = args[0].asString();
        app->mpv->Cycle(prop);
        return HavelValue(true);
      }));

  (*mpvObj)["copyCurrentSubtitle"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        return HavelValue(app->mpv->CopyCurrentSubtitle());
      }));

  (*mpvObj)["ipcSet"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &args) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        if (args.empty()) {
          return HavelRuntimeError("ipcSet() requires socket path");
        }
        std::string path = args[0].asString();
        app->mpv->SetIPC(path);
        return HavelValue(true);
      }));

  (*mpvObj)["ipcRestart"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (checkMpv()) {
          app->mpv->IPCRestart();
          return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
      }));

  // pic property - returns picture-in-picture status
  (*mpvObj)["pic"] = HavelValue(BuiltinFunction(
      [checkMpv, app](const std::vector<HavelValue> &) -> HavelResult {
        if (!checkMpv()) {
          return HavelRuntimeError("MPVController not available");
        }
        // Get picture-in-picture status
        return HavelValue(app->mpv->GetProperty("ontop"));
      }));

  // Register modules
  env.Define("media", HavelValue(mediaObj));
  env.Define("mpvcontroller", HavelValue(mpvObj));
}

} // namespace havel::modules
