/*
 * WindowModule.cpp
 * 
 * Window management module for Havel language.
 * Host binding - connects language to WindowManager.
 */
#include "../../host/HostContext.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "window/Window.hpp"
#include "window/WindowManager.hpp"

namespace havel::modules {

void registerWindowModule(Environment& env, HostContext& ctx) {
    if (!ctx.isValid() || !ctx.windowManager) {
        return;  // Skip if no window manager available
    }
    
    auto& wm = *ctx.windowManager;
    
    // Create window module object
    auto win = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // Helper to get active window
    auto getActiveWindow = [&wm]() -> Window {
        return Window(wm.GetActiveWindow());
    };
    
    // Helper to convert value to string
    auto valueToString = [](const HavelValue& v) -> std::string {
        if (v.isString()) return v.asString();
        if (v.isNumber()) {
            double val = v.asNumber();
            if (val == std::floor(val) && std::abs(val) < 1e15) {
                return std::to_string(static_cast<long long>(val));
            } else {
                std::ostringstream oss;
                oss.precision(15);
                oss << val;
                std::string s = oss.str();
                if (s.find('.') != std::string::npos) {
                    size_t last = s.find_last_not_of('0');
                    if (last != std::string::npos && s[last] == '.') {
                        s = s.substr(0, last);
                    } else if (last != std::string::npos) {
                        s = s.substr(0, last + 1);
                    }
                }
                return s;
            }
        }
        if (v.isBool()) return v.asBool() ? "true" : "false";
        return "";
    };
    
    // =========================================================================
    // Window functions
    // =========================================================================

    (*win)["getTitle"] = HavelValue(BuiltinFunction([&wm](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(wm.GetActiveWindowTitle());
    }));
    (*win)["title"] = (*win)["getTitle"];  // Alias

    (*win)["getPID"] = HavelValue(BuiltinFunction([&wm](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(wm.GetActiveWindowPID()));
    }));
    (*win)["pid"] = (*win)["getPID"];  // Alias

    (*win)["getClass"] = HavelValue(BuiltinFunction([&wm](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(wm.GetActiveWindowClass());
    }));
    (*win)["class"] = (*win)["getClass"];  // Alias

    (*win)["maximize"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>&) -> HavelResult {
        getActiveWindow().Max();
        return HavelValue(nullptr);
    }));

    (*win)["minimize"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>&) -> HavelResult {
        getActiveWindow().Min();
        return HavelValue(nullptr);
    }));

    (*win)["active"] = (*win)["isActive"];  // Alias

    (*win)["next"] = HavelValue(BuiltinFunction([&wm](const std::vector<HavelValue>&) -> HavelResult {
        wm.AltTab();
        return HavelValue(nullptr);
    }));

    (*win)["previous"] = HavelValue(BuiltinFunction([&wm](const std::vector<HavelValue>&) -> HavelResult {
        wm.AltTab();
        return HavelValue(nullptr);
    }));

    (*win)["close"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>&) -> HavelResult {
        getActiveWindow().Close();
        return HavelValue(nullptr);
    }));

    (*win)["center"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>&) -> HavelResult {
        // Note: wm.Center(Window) not available, use Window::Center() instead
        getActiveWindow().Center();
        return HavelValue(nullptr);
    }));

    (*win)["focus"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("window.focus() requires window title");
        }
        std::string title = args[0].isString() ? args[0].asString() :
            std::to_string(static_cast<int>(args[0].asNumber()));
        wID winId = WindowManager::FindByTitle(title.c_str());
        if (winId != 0) {
            Window window("", winId);
            window.Activate(winId);
            return HavelValue(true);
        }
        return HavelValue(false);
    }));

    (*win)["move"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("window.move() requires (x, y)");
        }
        int x = static_cast<int>(args[0].asNumber());
        int y = static_cast<int>(args[1].asNumber());
        return HavelValue(getActiveWindow().Move(x, y));
    }));

    (*win)["resize"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("window.resize() requires (width, height)");
        }
        int width = static_cast<int>(args[0].asNumber());
        int height = static_cast<int>(args[1].asNumber());
        return HavelValue(getActiveWindow().Resize(width, height));
    }));

    (*win)["moveResize"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 4) {
            return HavelRuntimeError("window.moveResize() requires (x, y, width, height)");
        }
        int x = static_cast<int>(args[0].asNumber());
        int y = static_cast<int>(args[1].asNumber());
        int width = static_cast<int>(args[2].asNumber());
        int height = static_cast<int>(args[3].asNumber());
        return HavelValue(getActiveWindow().MoveResize(x, y, width, height));
    }));
    
    (*win)["alwaysOnTop"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>& args) -> HavelResult {
        bool top = args.empty() ? true : args[0].asBool();
        getActiveWindow().AlwaysOnTop(top);
        return HavelValue(nullptr);
    }));
    
    (*win)["transparency"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>& args) -> HavelResult {
        int alpha = args.empty() ? 255 : static_cast<int>(args[0].asNumber());
        getActiveWindow().Transparency(alpha);
        return HavelValue(nullptr);
    }));
    
    (*win)["toggleFullscreen"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>&) -> HavelResult {
        getActiveWindow().ToggleFullscreen();
        return HavelValue(nullptr);
    }));
    
    (*win)["snap"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("window.snap() requires position (0-3)");
        }
        int position = static_cast<int>(args[0].asNumber());
        getActiveWindow().Snap(position);
        return HavelValue(nullptr);
    }));
    
    (*win)["moveToMonitor"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("window.moveToMonitor() requires monitor index");
        }
        int monitor = static_cast<int>(args[0].asNumber());
        return HavelValue(getActiveWindow().MoveToMonitor(monitor));
    }));
    
    (*win)["moveToCorner"] = HavelValue(BuiltinFunction([&getActiveWindow, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("window.moveToCorner() requires corner name");
        }
        std::string corner = valueToString(args[0]);
        return HavelValue(getActiveWindow().MoveToCorner(corner));
    }));
    
    (*win)["getClass"] = HavelValue(BuiltinFunction([&wm](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(wm.GetActiveWindowClass());
    }));
    
    (*win)["exists"] = HavelValue(BuiltinFunction([&getActiveWindow, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelValue(getActiveWindow().Exists());
        }
        std::string title = valueToString(args[0]);
        wID winId = WindowManager::FindByTitle(title.c_str());
        return HavelValue(winId != 0);
    }));
    
    (*win)["isActive"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(getActiveWindow().Active());
    }));

    (*win)["getMonitors"] = HavelValue(BuiltinFunction([&wm](const std::vector<HavelValue>&) -> HavelResult {
        // Note: GetMonitorCount not available, return single monitor
        (void)wm;  // Suppress unused warning
        auto monitors = std::make_shared<std::vector<HavelValue>>();
        monitors->push_back(HavelValue(0.0));  // Single monitor (index 0)
        return HavelValue(monitors);
    }));

    (*win)["getMonitorArea"] = HavelValue(BuiltinFunction([&wm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("window.getMonitorArea() requires monitor index");
        }
        int monitor = static_cast<int>(args[0].asNumber());
        // Note: GetMonitorWorkArea not available, return default rect
        (void)monitor;  // Suppress unused warning
        auto area = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*area)["x"] = HavelValue(0.0);
        (*area)["y"] = HavelValue(0.0);
        (*area)["width"] = HavelValue(1920.0);
        (*area)["height"] = HavelValue(1080.0);
        return HavelValue(area);
    }));

    (*win)["getActiveWindow"] = HavelValue(BuiltinFunction([&wm](const std::vector<HavelValue>&) -> HavelResult {
        auto winObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        wID id = wm.GetActiveWindow();
        (*winObj)["id"] = HavelValue(static_cast<double>(id));
        (*winObj)["title"] = HavelValue(wm.GetActiveWindowTitle());
        return HavelValue(winObj);
    }));

    (*win)["pos"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>&) -> HavelResult {
        auto pos = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        // Note: GetRect not available, use Pos() instead
        auto rect = getActiveWindow().Pos();
        (*pos)["x"] = HavelValue(static_cast<double>(rect.x));
        (*pos)["y"] = HavelValue(static_cast<double>(rect.y));
        return HavelValue(pos);
    }));

    (*win)["moveToNextMonitor"] = HavelValue(BuiltinFunction([&getActiveWindow](const std::vector<HavelValue>&) -> HavelResult {
        // Note: MoveToNextMonitor not available, stub out
        (void)getActiveWindow();  // Suppress unused warning
        return HavelValue(nullptr);
    }));

    // =========================================================================
    // Window group functions
    // =========================================================================

    (*win)["getGroups"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        auto groups = WindowManager::GetGroupNames();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto& group : groups) {
            arr->push_back(HavelValue(group));
        }
        return HavelValue(arr);
    }));

    (*win)["getGroupWindows"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("window.getGroupWindows() requires group name");
        }
        std::string groupName = args[0].isString() ? args[0].asString() : "";
        auto windows = WindowManager::GetGroupWindows(groupName.c_str());
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto& win : windows) {
            arr->push_back(HavelValue(win));
        }
        return HavelValue(arr);
    }));

    (*win)["isWindowInGroup"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("window.isWindowInGroup() requires (windowTitle, groupName)");
        }
        std::string windowTitle = args[0].isString() ? args[0].asString() : "";
        std::string groupName = args[1].isString() ? args[1].asString() : "";
        return HavelValue(WindowManager::IsWindowInGroup(windowTitle.c_str(), groupName.c_str()));
    }));

    (*win)["findInGroup"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("window.findInGroup() requires group name");
        }
        std::string groupName = args[0].isString() ? args[0].asString() : "";
        wID winId = WindowManager::FindWindowInGroup(groupName.c_str());
        if (winId) {
            auto winObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*winObj)["id"] = HavelValue(static_cast<double>(winId));
            (*winObj)["found"] = HavelValue(true);
            return HavelValue(winObj);
        }
        return HavelValue(nullptr);
    }));

    // Register window module
    env.Define("window", HavelValue(win));
    
    // Also register individual functions for backward compatibility
    for (const auto& [name, value] : *win) {
        env.Define("window." + name, value);
    }
}

} // namespace havel::modules
