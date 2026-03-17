/*
 * WindowModule.cpp
 *
 * Window query module for Havel language.
 * Exposes window.* API for scripts.
 */
#include "WindowModule.hpp"
#include "../../core/DisplayManager.hpp"
#include "../../window/WindowQuery.hpp"
#include <memory>

namespace havel::modules {

// Helper functions for HavelResult handling
bool isError(const HavelResult &result) {
  return std::holds_alternative<HavelRuntimeError>(result);
}

HavelValue unwrap(const HavelResult &result) {
  if (std::holds_alternative<HavelValue>(result)) {
    return std::get<HavelValue>(result);
  }
  return HavelValue(nullptr);
}

void registerWindowQueryModule(Environment &env, std::shared_ptr<IHostAPI>) {
  auto windowObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // window.active - Get active window info
  (*windowObj)["active"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        auto info = WindowQuery::getActive();
        if (!info.valid) {
          return HavelValue(nullptr);
        }

        auto resultObj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*resultObj)["id"] = HavelValue(static_cast<double>(info.windowId));
        (*resultObj)["title"] = HavelValue(info.title);
        (*resultObj)["class"] = HavelValue(info.windowClass);
        (*resultObj)["exe"] = HavelValue(info.exe);
        (*resultObj)["pid"] = HavelValue(static_cast<double>(info.pid));

        return HavelValue(resultObj);
      }));

  // window.list() - Get list of all windows
  (*windowObj)["list"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        auto resultArray = std::make_shared<std::vector<HavelValue>>();
        // TODO: Implement window enumeration
        return HavelValue(resultArray);
      }));

  // window.getMonitors() - Get list of all monitors
  (*windowObj)["getMonitors"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        auto monitors = DisplayManager::GetMonitors();
        auto resultArray = std::make_shared<std::vector<HavelValue>>();

        for (const auto &monitor : monitors) {
          auto monitorObj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*monitorObj)["name"] = HavelValue(monitor.name);
          (*monitorObj)["x"] = HavelValue(static_cast<double>(monitor.x));
          (*monitorObj)["y"] = HavelValue(static_cast<double>(monitor.y));
          (*monitorObj)["width"] =
              HavelValue(static_cast<double>(monitor.width));
          (*monitorObj)["height"] =
              HavelValue(static_cast<double>(monitor.height));
          (*monitorObj)["isPrimary"] = HavelValue(monitor.isPrimary);
          (*monitorObj)["id"] = HavelValue(static_cast<double>(monitor.id));
          (*monitorObj)["crtc_id"] =
              HavelValue(static_cast<double>(monitor.crtc_id));
          resultArray->push_back(HavelValue(monitorObj));
        }

        return HavelValue(resultArray);
      }));

  // window.count() - Count all windows (or matching condition)
  (*windowObj)["count"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        auto info = WindowQuery::getActive();
        if (!info.valid) {
          return HavelValue(0.0);
        }

        if (args.empty()) {
          return HavelValue(1.0);
        }

        // If argument is a function, call it with window info
        if (args[0].is<BuiltinFunction>()) {
          auto winObj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*winObj)["id"] = HavelValue(static_cast<double>(info.windowId));
          (*winObj)["title"] = HavelValue(info.title);
          (*winObj)["class"] = HavelValue(info.windowClass);
          (*winObj)["exe"] = HavelValue(info.exe);
          (*winObj)["pid"] = HavelValue(static_cast<double>(info.pid));

          auto conditionFunc = args[0].get<BuiltinFunction>();
          std::vector<HavelValue> callArgs = {HavelValue(winObj)};
          auto result = conditionFunc(callArgs);

          if (isError(result)) {
            return result;
          }

          bool matches = unwrap(result).isBool() && unwrap(result).get<bool>();
          return HavelValue(matches ? 1.0 : 0.0);
        }

        return HavelValue(1.0);
      }));

  // window.any(condition) - Check if any window matches
  (*windowObj)["any"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        auto info = WindowQuery::getActive();
        if (!info.valid) {
          return HavelValue(false);
        }

        if (args.empty()) {
          return HavelValue(true);
        }

        // If argument is a function, call it with window info
        if (args[0].is<BuiltinFunction>()) {
          auto winObj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*winObj)["id"] = HavelValue(static_cast<double>(info.windowId));
          (*winObj)["title"] = HavelValue(info.title);
          (*winObj)["class"] = HavelValue(info.windowClass);
          (*winObj)["exe"] = HavelValue(info.exe);
          (*winObj)["pid"] = HavelValue(static_cast<double>(info.pid));

          auto conditionFunc = args[0].get<BuiltinFunction>();
          std::vector<HavelValue> callArgs = {HavelValue(winObj)};
          auto result = conditionFunc(callArgs);

          if (isError(result)) {
            return result;
          }

          return HavelValue(unwrap(result).isBool() &&
                            unwrap(result).get<bool>());
        }

        // Simple property checks: window.any("exe", "steam.exe")
        if (args.size() >= 2) {
          std::string prop = args[0].isString() ? args[0].asString() : "";
          HavelValue value = args[1];

          if (prop == "exe") {
            return HavelValue(info.exe == value.asString());
          } else if (prop == "class") {
            return HavelValue(info.windowClass == value.asString());
          } else if (prop == "title") {
            return HavelValue(info.title == value.asString());
          } else if (prop == "pid") {
            return HavelValue(static_cast<double>(info.pid) ==
                              value.asNumber());
          }
        }

        return HavelValue(false);
      }));

  // window.filter(condition) - Filter matching windows
  (*windowObj)["filter"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        auto resultArray = std::make_shared<std::vector<HavelValue>>();

        auto info = WindowQuery::getActive();
        if (!info.valid) {
          return HavelValue(resultArray);
        }

        // If no condition, return active window
        if (args.empty()) {
          auto winObj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*winObj)["id"] = HavelValue(static_cast<double>(info.windowId));
          (*winObj)["title"] = HavelValue(info.title);
          (*winObj)["class"] = HavelValue(info.windowClass);
          (*winObj)["exe"] = HavelValue(info.exe);
          (*winObj)["pid"] = HavelValue(static_cast<double>(info.pid));
          resultArray->push_back(HavelValue(winObj));
          return HavelValue(resultArray);
        }

        // If argument is a function, call it
        if (args[0].is<BuiltinFunction>()) {
          auto winObj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*winObj)["id"] = HavelValue(static_cast<double>(info.windowId));
          (*winObj)["title"] = HavelValue(info.title);
          (*winObj)["class"] = HavelValue(info.windowClass);
          (*winObj)["exe"] = HavelValue(info.exe);
          (*winObj)["pid"] = HavelValue(static_cast<double>(info.pid));

          auto conditionFunc = args[0].get<BuiltinFunction>();
          std::vector<HavelValue> callArgs = {HavelValue(winObj)};
          auto result = conditionFunc(callArgs);

          if (isError(result)) {
            return result;
          }

          if (unwrap(result).isBool() && unwrap(result).get<bool>()) {
            resultArray->push_back(HavelValue(winObj));
          }

          return HavelValue(resultArray);
        }

        return HavelValue(resultArray);
      }));

  // window.map(callback) - Transform windows
  (*windowObj)["map"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        auto resultArray = std::make_shared<std::vector<HavelValue>>();

        auto info = WindowQuery::getActive();
        if (!info.valid || args.empty()) {
          return HavelValue(resultArray);
        }

        if (args[0].is<BuiltinFunction>()) {
          auto winObj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*winObj)["id"] = HavelValue(static_cast<double>(info.windowId));
          (*winObj)["title"] = HavelValue(info.title);
          (*winObj)["class"] = HavelValue(info.windowClass);
          (*winObj)["exe"] = HavelValue(info.exe);
          (*winObj)["pid"] = HavelValue(static_cast<double>(info.pid));

          auto mapFunc = args[0].get<BuiltinFunction>();
          std::vector<HavelValue> callArgs = {HavelValue(winObj)};
          auto result = mapFunc(callArgs);

          if (isError(result)) {
            return result;
          }

          resultArray->push_back(unwrap(result));
          return HavelValue(resultArray);
        }

        return HavelValue(resultArray);
      }));

  // window.forEach(callback) - Iterate over windows
  (*windowObj)["forEach"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        auto info = WindowQuery::getActive();
        if (!info.valid || args.empty()) {
          return HavelValue(nullptr);
        }

        if (args[0].is<BuiltinFunction>()) {
          auto winObj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*winObj)["id"] = HavelValue(static_cast<double>(info.windowId));
          (*winObj)["title"] = HavelValue(info.title);
          (*winObj)["class"] = HavelValue(info.windowClass);
          (*winObj)["exe"] = HavelValue(info.exe);
          (*winObj)["pid"] = HavelValue(static_cast<double>(info.pid));

          auto forEachFunc = args[0].get<BuiltinFunction>();
          std::vector<HavelValue> callArgs = {HavelValue(winObj)};
          auto result = forEachFunc(callArgs);

          if (isError(result)) {
            return result;
          }
        }

        return HavelValue(nullptr);
      }));

  env.Define("window", HavelValue(windowObj));
}

} // namespace havel::modules
