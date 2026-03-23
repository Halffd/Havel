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

void registerWindowQueryModule(Environment &env,
                               std::shared_ptr<IHostAPI> hostAPI) {
  auto *wm = hostAPI->GetWindowManager();
  if (!wm) {
    // Can't register window module without window manager
    return;
  }

  auto windowObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // =========================================================================
  // WINDOW QUERY FUNCTIONS - Get info about windows
  // =========================================================================

  // window.active() - Get active window info
  (*windowObj)["active"] = HavelValue(
      BuiltinFunction([wm](const std::vector<HavelValue> &) -> HavelResult {
        auto info = wm->getActiveWindowInfo();
        if (!info.valid) {
          return HavelValue(nullptr);
        }
        return HavelValue(windowInfoToObject(info));
      }));

  // window.list() - Get list of all windows
  (*windowObj)["list"] = HavelValue(
      BuiltinFunction([wm](const std::vector<HavelValue> &) -> HavelResult {
        auto windows = wm->getAllWindows();
        auto resultArray = std::make_shared<std::vector<HavelValue>>();

        for (const auto &info : windows) {
          resultArray->push_back(HavelValue(windowInfoToObject(info)));
        }

        return HavelValue(resultArray);
      }));

  // window.find(condition) - Find first window matching predicate
  (*windowObj)["find"] = HavelValue(BuiltinFunction(
      [wm](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty() || !args[0].is<BuiltinFunction>()) {
          return HavelRuntimeError(
              "window.find() requires a predicate function");
        }

        auto predicate = args[0].get<BuiltinFunction>();
        auto windows = wm->getAllWindows();

        for (const auto &info : windows) {
          auto winObj = windowInfoToObject(info);
          std::vector<HavelValue> callArgs = {HavelValue(winObj)};
          auto result = predicate(callArgs);

          if (std::holds_alternative<HavelRuntimeError>(result)) {
            return result; // Propagate error
          }

          if (std::holds_alternative<HavelValue>(result)) {
            auto val = std::get<HavelValue>(result);
            if (val.isBool() && val.get<bool>()) {
              return HavelValue(winObj);
            }
          }
        }

        return HavelValue(nullptr); // Not found
      }));

  // window.filter(condition) - Filter windows by predicate
  (*windowObj)["filter"] = HavelValue(BuiltinFunction(
      [wm](const std::vector<HavelValue> &args) -> HavelResult {
        auto resultArray = std::make_shared<std::vector<HavelValue>>();

        if (args.empty() || !args[0].is<BuiltinFunction>()) {
          // No predicate? return all windows
          auto windows = wm->getAllWindows();
          for (const auto &info : windows) {
            resultArray->push_back(HavelValue(windowInfoToObject(info)));
          }
          return HavelValue(resultArray);
        }

        auto predicate = args[0].get<BuiltinFunction>();
        auto windows = wm->getAllWindows();

        for (const auto &info : windows) {
          auto winObj = windowInfoToObject(info);
          std::vector<HavelValue> callArgs = {HavelValue(winObj)};
          auto result = predicate(callArgs);

          if (std::holds_alternative<HavelRuntimeError>(result)) {
            return result; // Propagate error
          }

          if (std::holds_alternative<HavelValue>(result)) {
            auto val = std::get<HavelValue>(result);
            if (val.isBool() && val.get<bool>()) {
              resultArray->push_back(HavelValue(winObj));
            }
          }
        }

        return HavelValue(resultArray);
      }));

  // window.any(condition) - Check if any window matches
  (*windowObj)["any"] = HavelValue(BuiltinFunction(
      [wm](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty() || !args[0].is<BuiltinFunction>()) {
          return HavelRuntimeError(
              "window.any() requires a predicate function");
        }

        auto predicate = args[0].get<BuiltinFunction>();
        auto windows = wm->getAllWindows();

        for (const auto &info : windows) {
          auto winObj = windowInfoToObject(info);
          std::vector<HavelValue> callArgs = {HavelValue(winObj)};
          auto result = predicate(callArgs);

          if (std::holds_alternative<HavelRuntimeError>(result)) {
            return result;
          }

          if (std::holds_alternative<HavelValue>(result)) {
            auto val = std::get<HavelValue>(result);
            if (val.isBool() && val.get<bool>()) {
              return HavelValue(true);
            }
          }
        }

        return HavelValue(false);
      }));

  // window.count() - Count windows (optionally matching condition)
  (*windowObj)["count"] = HavelValue(BuiltinFunction(
      [wm](const std::vector<HavelValue> &args) -> HavelResult {
        auto windows = wm->getAllWindows();

        if (args.empty() || !args[0].is<BuiltinFunction>()) {
          return HavelValue(static_cast<double>(windows.size()));
        }

        auto predicate = args[0].get<BuiltinFunction>();
        size_t count = 0;

        for (const auto &info : windows) {
          auto winObj = windowInfoToObject(info);
          std::vector<HavelValue> callArgs = {HavelValue(winObj)};
          auto result = predicate(callArgs);

          if (std::holds_alternative<HavelRuntimeError>(result)) {
            return result;
          }

          if (std::holds_alternative<HavelValue>(result)) {
            auto val = std::get<HavelValue>(result);
            if (val.isBool() && val.get<bool>()) {
              count++;
            }
          }
        }

        return HavelValue(static_cast<double>(count));
      }));

  // window.map(callback) - Transform windows
  (*windowObj)["map"] = HavelValue(BuiltinFunction(
      [wm](const std::vector<HavelValue> &args) -> HavelResult {
        auto resultArray = std::make_shared<std::vector<HavelValue>>();

        if (args.empty() || !args[0].is<BuiltinFunction>()) {
          return HavelRuntimeError("window.map() requires a mapping function");
        }

        auto mapper = args[0].get<BuiltinFunction>();
        auto windows = wm->getAllWindows();

        for (const auto &info : windows) {
          auto winObj = windowInfoToObject(info);
          std::vector<HavelValue> callArgs = {HavelValue(winObj)};
          auto result = mapper(callArgs);

          if (std::holds_alternative<HavelRuntimeError>(result)) {
            return result;
          }

          if (std::holds_alternative<HavelValue>(result)) {
            resultArray->push_back(std::get<HavelValue>(result));
          }
        }

        return HavelValue(resultArray);
      }));

  // window.forEach(callback) - Iterate over windows
  (*windowObj)["forEach"] = HavelValue(BuiltinFunction(
      [wm](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty() || !args[0].is<BuiltinFunction>()) {
          return HavelRuntimeError(
              "window.forEach() requires a callback function");
        }

        auto callback = args[0].get<BuiltinFunction>();
        auto windows = wm->getAllWindows();

        for (const auto &info : windows) {
          auto winObj = windowInfoToObject(info);
          std::vector<HavelValue> callArgs = {HavelValue(winObj)};
          auto result = callback(callArgs);

          if (std::holds_alternative<HavelRuntimeError>(result)) {
            return result;
          }
        }

        return HavelValue(nullptr);
      }));

  // =========================================================================
  // WINDOW CONTROL FUNCTIONS - Actually do things to windows
  // =========================================================================

  // Helper: Get window ID from first argument (either ID number or window
  // object)
  auto getWindowId = [](const std::vector<HavelValue> &args,
                        WindowManager *wm) -> uint64_t {
    if (args.empty()) {
      return wm->getActiveWindow();
    }

    if (args[0].isNumber()) {
      return static_cast<uint64_t>(args[0].asNumber());
    }

    if (args[0]
            .is<std::shared_ptr<
                std::unordered_map<std::string, HavelValue>>>()) {
      auto obj = args[0]
                     .get<std::shared_ptr<
                         std::unordered_map<std::string, HavelValue>>>();
      auto it = obj->find("id");
      if (it != obj->end() && it->second.isNumber()) {
        return static_cast<uint64_t>(it->second.asNumber());
      }
    }

    return wm->getActiveWindow();
  };

  // window.focus(id) - Focus window
  (*windowObj)["focus"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        uint64_t id = getWindowId(args, wm);
        if (id == 0) {
          return HavelRuntimeError("No window to focus");
        }

        bool success = wm->focusWindow(id);
        return HavelValue(success);
      }));

  // window.close(id) - Close window
  (*windowObj)["close"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        uint64_t id = getWindowId(args, wm);
        if (id == 0) {
          return HavelRuntimeError("No window to close");
        }

        bool success = wm->closeWindow(id);
        return HavelValue(success);
      }));

  // window.move(id, x, y) - Move window
  (*windowObj)["move"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 3) {
          return HavelRuntimeError("window.move() requires id, x, y");
        }

        uint64_t id = getWindowId({args[0]}, wm);
        if (id == 0) {
          return HavelRuntimeError("Invalid window");
        }

        int x = static_cast<int>(args[1].asNumber());
        int y = static_cast<int>(args[2].asNumber());

        bool success = wm->moveWindow(id, x, y);
        return HavelValue(success);
      }));

  // window.resize(id, width, height) - Resize window
  (*windowObj)["resize"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 3) {
          return HavelRuntimeError(
              "window.resize() requires id, width, height");
        }

        uint64_t id = getWindowId({args[0]}, wm);
        if (id == 0) {
          return HavelRuntimeError("Invalid window");
        }

        int width = static_cast<int>(args[1].asNumber());
        int height = static_cast<int>(args[2].asNumber());

        bool success = wm->resizeWindow(id, width, height);
        return HavelValue(success);
      }));

  // window.moveResize(id, x, y, width, height) - Move and resize
  (*windowObj)["moveResize"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 5) {
          return HavelRuntimeError(
              "window.moveResize() requires id, x, y, width, height");
        }

        uint64_t id = getWindowId({args[0]}, wm);
        if (id == 0) {
          return HavelRuntimeError("Invalid window");
        }

        int x = static_cast<int>(args[1].asNumber());
        int y = static_cast<int>(args[2].asNumber());
        int width = static_cast<int>(args[3].asNumber());
        int height = static_cast<int>(args[4].asNumber());

        bool success = wm->moveResizeWindow(id, x, y, width, height);
        return HavelValue(success);
      }));

  // window.maximize(id) - Maximize window
  (*windowObj)["maximize"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        uint64_t id = getWindowId(args, wm);
        if (id == 0) {
          return HavelRuntimeError("No window to maximize");
        }

        bool success = wm->maximizeWindow(id);
        return HavelValue(success);
      }));

  // window.minimize(id) - Minimize window
  (*windowObj)["minimize"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        uint64_t id = getWindowId(args, wm);
        if (id == 0) {
          return HavelRuntimeError("No window to minimize");
        }

        bool success = wm->minimizeWindow(id);
        return HavelValue(success);
      }));

  // window.restore(id) - Restore minimized window
  (*windowObj)["restore"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        uint64_t id = getWindowId(args, wm);
        if (id == 0) {
          return HavelRuntimeError("No window to restore");
        }

        bool success = wm->restoreWindow(id);
        return HavelValue(success);
      }));

  // window.toggleFullscreen(id) - Toggle fullscreen
  (*windowObj)["toggleFullscreen"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        uint64_t id = getWindowId(args, wm);
        if (id == 0) {
          return HavelRuntimeError("No window to toggle");
        }

        bool success = wm->toggleFullscreen(id);
        return HavelValue(success);
      }));

  // window.setFloating(id, floating) - Set floating state
  (*windowObj)["setFloating"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "window.setFloating() requires id and floating flag");
        }

        uint64_t id = getWindowId({args[0]}, wm);
        if (id == 0) {
          return HavelRuntimeError("Invalid window");
        }

        bool floating = args[1].asBool();
        bool success = wm->setFloating(id, floating);
        return HavelValue(success);
      }));

  // window.center(id) - Center window on screen
  (*windowObj)["center"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        uint64_t id = getWindowId(args, wm);
        if (id == 0) {
          return HavelRuntimeError("No window to center");
        }

        bool success = wm->centerWindow(id);
        return HavelValue(success);
      }));

  // window.snap(id, position) - Snap to edge (0=left,1=right,2=top,3=bottom)
  (*windowObj)["snap"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError("window.snap() requires id and position");
        }

        uint64_t id = getWindowId({args[0]}, wm);
        if (id == 0) {
          return HavelRuntimeError("Invalid window");
        }

        int position = static_cast<int>(args[1].asNumber());
        if (position < 0 || position > 3) {
          return HavelRuntimeError("Position must be 0-3");
        }

        bool success = wm->snapWindow(id, position);
        return HavelValue(success);
      }));

  // window.moveToWorkspace(id, workspace) - Move window to workspace
  (*windowObj)["moveToWorkspace"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "window.moveToWorkspace() requires id and workspace");
        }

        uint64_t id = getWindowId({args[0]}, wm);
        if (id == 0) {
          return HavelRuntimeError("Invalid window");
        }

        int workspace = static_cast<int>(args[1].asNumber());
        bool success = wm->moveWindowToWorkspace(id, workspace);
        return HavelValue(success);
      }));

  // window.setAlwaysOnTop(id, onTop) - Set always on top
  (*windowObj)["setAlwaysOnTop"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "window.setAlwaysOnTop() requires id and onTop flag");
        }

        uint64_t id = getWindowId({args[0]}, wm);
        if (id == 0) {
          return HavelRuntimeError("Invalid window");
        }

        bool onTop = args[1].asBool();
        bool success = wm->setAlwaysOnTop(id, onTop);
        return HavelValue(success);
      }));

  // =========================================================================
  // WORKSPACE FUNCTIONS
  // =========================================================================

  // window.getWorkspaces() - Get list of workspaces
  (*windowObj)["getWorkspaces"] = HavelValue(
      BuiltinFunction([wm](const std::vector<HavelValue> &) -> HavelResult {
        auto workspaces = wm->getWorkspaces();
        auto resultArray = std::make_shared<std::vector<HavelValue>>();

        for (const auto &ws : workspaces) {
          auto wsObj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*wsObj)["id"] = HavelValue(static_cast<double>(ws.id));
          (*wsObj)["name"] = HavelValue(ws.name);
          (*wsObj)["visible"] = HavelValue(ws.visible);
          (*wsObj)["windows"] = HavelValue(static_cast<double>(ws.windowCount));
          resultArray->push_back(HavelValue(wsObj));
        }

        return HavelValue(resultArray);
      }));

  // window.switchWorkspace(id) - Switch to workspace
  (*windowObj)["switchWorkspace"] = HavelValue(BuiltinFunction(
      [wm](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError(
              "window.switchWorkspace() requires workspace id");
        }

        int workspace = static_cast<int>(args[0].asNumber());
        bool success = wm->switchToWorkspace(workspace);
        return HavelValue(success);
      }));

  // window.getCurrentWorkspace() - Get current workspace
  (*windowObj)["getCurrentWorkspace"] = HavelValue(
      BuiltinFunction([wm](const std::vector<HavelValue> &) -> HavelResult {
        int workspace = wm->getCurrentWorkspace();
        return HavelValue(static_cast<double>(workspace));
      }));

  // =========================================================================
  // MONITOR FUNCTIONS
  // =========================================================================

  // window.getMonitors() - Get list of monitors
  (*windowObj)["getMonitors"] = HavelValue(
      BuiltinFunction([wm](const std::vector<HavelValue> &) -> HavelResult {
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

  // window.moveToMonitor(id, monitorIndex) - Move window to monitor
  (*windowObj)["moveToMonitor"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "window.moveToMonitor() requires id and monitor index");
        }

        uint64_t id = getWindowId({args[0]}, wm);
        if (id == 0) {
          return HavelRuntimeError("Invalid window");
        }

        int monitor = static_cast<int>(args[1].asNumber());
        bool success = wm->moveWindowToMonitor(id, monitor);
        return HavelValue(success);
      }));

  // =========================================================================
  // GROUP FUNCTIONS
  // =========================================================================

  // window.getGroups() - Get list of window groups
  (*windowObj)["getGroups"] = HavelValue(
      BuiltinFunction([wm](const std::vector<HavelValue> &) -> HavelResult {
        auto groups = wm->getGroupNames();
        auto resultArray = std::make_shared<std::vector<HavelValue>>();

        for (const auto &group : groups) {
          resultArray->push_back(HavelValue(group));
        }

        return HavelValue(resultArray);
      }));

  // window.getGroupWindows(groupName) - Get windows in group
  (*windowObj)["getGroupWindows"] = HavelValue(BuiltinFunction(
      [wm](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError(
              "window.getGroupWindows() requires group name");
        }

        std::string groupName = valueToString(args[0]);
        auto windows = wm->getGroupWindows(groupName);
        auto resultArray = std::make_shared<std::vector<HavelValue>>();

        for (const auto &info : windows) {
          resultArray->push_back(HavelValue(windowInfoToObject(info)));
        }

        return HavelValue(resultArray);
      }));

  // window.addToGroup(id, groupName) - Add window to group
  (*windowObj)["addToGroup"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "window.addToGroup() requires id and group name");
        }

        uint64_t id = getWindowId({args[0]}, wm);
        if (id == 0) {
          return HavelRuntimeError("Invalid window");
        }

        std::string groupName = valueToString(args[1]);
        bool success = wm->addWindowToGroup(id, groupName);
        return HavelValue(success);
      }));

  // window.removeFromGroup(id, groupName) - Remove window from group
  (*windowObj)["removeFromGroup"] = HavelValue(BuiltinFunction(
      [wm, getWindowId](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "window.removeFromGroup() requires id and group name");
        }

        uint64_t id = getWindowId({args[0]}, wm);
        if (id == 0) {
          return HavelRuntimeError("Invalid window");
        }

        std::string groupName = valueToString(args[1]);
        bool success = wm->removeWindowFromGroup(id, groupName);
        return HavelValue(success);
      }));

  // =========================================================================
  // UTILITY FUNCTIONS
  // =========================================================================

  // window.wait(condition, timeout) - Wait for condition to be true
  (*windowObj)["wait"] = HavelValue(BuiltinFunction(
      [wm](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty() || !args[0].is<BuiltinFunction>()) {
          return HavelRuntimeError(
              "window.wait() requires a predicate function");
        }

        auto predicate = args[0].get<BuiltinFunction>();
        int timeoutMs =
            args.size() > 1 ? static_cast<int>(args[1].asNumber()) : 5000;
        int intervalMs = 50;
        int elapsed = 0;

        while (elapsed < timeoutMs) {
          // Check condition
          std::vector<HavelValue> callArgs;
          auto result = predicate(callArgs);

          if (std::holds_alternative<HavelRuntimeError>(result)) {
            return result;
          }

          if (std::holds_alternative<HavelValue>(result)) {
            auto val = std::get<HavelValue>(result);
            if (val.isBool() && val.get<bool>()) {
              return HavelValue(true);
            }
          }

          // Sleep a bit
          std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
          elapsed += intervalMs;
        }

        return HavelValue(false); // Timed out
      }));

  // Register the module
  // MIGRATED TO BYTECODE VM: env.Define("window", HavelValue(windowObj));
}

} // namespace havel::modules