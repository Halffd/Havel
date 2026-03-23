#include "WindowModule.hpp"
#include "../../core/DisplayManager.hpp"
#include "../../window/Window.hpp"
#include "../../window/WindowManager.hpp"
#include "../../window/WindowQuery.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace havel::modules {

// Helper: Convert HavelValue to string (used for window titles, etc)
std::string valueToString(const HavelValue &v) {
  if (v.isString())
    return v.asString();
  if (v.isNumber())
    return std::to_string(static_cast<int>(v.asNumber()));
  if (v.isBool())
    return v.asBool() ? "true" : "false";
  return "";
}

// Helper: Convert WindowInfo to Havel object
std::shared_ptr<std::unordered_map<std::string, HavelValue>>
windowInfoToObject(const WindowInfo &info) {
  auto obj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
  (*obj)["id"] = HavelValue(static_cast<double>(info.id));
  (*obj)["title"] = HavelValue(info.title);
  (*obj)["class"] = HavelValue(info.windowClass);
  (*obj)["app_id"] = HavelValue(info.appId);
  (*obj)["pid"] = HavelValue(static_cast<double>(info.pid));
  (*obj)["x"] = HavelValue(static_cast<double>(info.x));
  (*obj)["y"] = HavelValue(static_cast<double>(info.y));
  (*obj)["width"] = HavelValue(static_cast<double>(info.width));
  (*obj)["height"] = HavelValue(static_cast<double>(info.height));
  (*obj)["floating"] = HavelValue(info.floating);
  (*obj)["minimized"] = HavelValue(info.minimized);
  (*obj)["maximized"] = HavelValue(info.maximized);
  (*obj)["fullscreen"] = HavelValue(info.fullscreen);
  (*obj)["workspace"] = HavelValue(static_cast<double>(info.workspace));
  return obj;
}

void registerModuleStub() {
    // STUBBED FOR BYTECODE VM MIGRATION
    // env removed
    // hostAPI removed

}

} // namespace havel::modules