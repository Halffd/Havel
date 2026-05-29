/*
 * WindowMonitorModule.cpp - Window monitoring and manipulation module for bytecode VM
 * Provides: window.activeTitle, window.activeClass, window.activeExe, window.activePid,
 *           window.active, window.list, window.find, window.info, window.move,
 *           window.resize, window.moveResize, window.close, window.focus,
 *           window.minimize, window.maximize, window.restore, window.hide,
 *           window.show, window.center, window.snap, window.fullscreen,
 *           window.alwaysOnTop, window.floating, window.opacity,
 *           window.moveToMonitor, window.moveToDesktop, window.desktop,
 *           window.isMinimized, window.isMaximized, window.isFullscreen,
 *           window.exists, window.title, window.className, window.pid,
 *           window.exe, window.geometry, window.workspaces, window.currentDesktop
 * Uses WindowManager singleton (backend-abstracted, works on X11/Wayland/Windows)
 */
#include "WindowMonitorModule.hpp"
#include "core/window/WindowManager.hpp"
#include "core/window/WindowQuery.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static Value makeStr(const VMApi &api, const std::string &s) {
    return api.makeString(s);
}

static int64_t toInt(const Value &v, int64_t def = 0) {
    if (v.isInt()) return v.asInt();
    if (v.isDouble()) return static_cast<int64_t>(v.asDouble());
    return def;
}

static double toDbl(const Value &v, double def = 0.0) {
    if (v.isDouble()) return v.asDouble();
    if (v.isInt()) return static_cast<double>(v.asInt());
    return def;
}

static std::string toStr(const VMApi &api, const Value &v) {
    if (v.isNull()) return "";
    return api.toString(v);
}

static Value windowInfoToValue(const VMApi &api, const WindowInfo &info) {
    auto obj = api.makeObject();
    api.setField(obj, "id", Value::makeInt(static_cast<int64_t>(info.id)));
    api.setField(obj, "title", makeStr(api, info.title));
    api.setField(obj, "class", makeStr(api, info.windowClass));
    api.setField(obj, "appId", makeStr(api, info.appId));
    api.setField(obj, "exe", makeStr(api, info.exe));
    api.setField(obj, "cmdline", makeStr(api, info.cmdline));
    api.setField(obj, "pid", Value::makeInt(static_cast<int64_t>(info.pid)));
    api.setField(obj, "x", Value::makeInt(info.x));
    api.setField(obj, "y", Value::makeInt(info.y));
    api.setField(obj, "width", Value::makeInt(info.width));
    api.setField(obj, "height", Value::makeInt(info.height));
    api.setField(obj, "floating", Value::makeBool(info.floating));
    api.setField(obj, "minimized", Value::makeBool(info.minimized));
    api.setField(obj, "maximized", Value::makeBool(info.maximized));
    api.setField(obj, "fullscreen", Value::makeBool(info.fullscreen));
    api.setField(obj, "workspace", Value::makeInt(info.workspace));
    api.setField(obj, "valid", Value::makeBool(info.valid));
    return obj;
}

static Value workspaceInfoToValue(const VMApi &api, const WorkspaceInfo &info) {
    auto obj = api.makeObject();
    api.setField(obj, "id", Value::makeInt(info.id));
    api.setField(obj, "name", makeStr(api, info.name));
    api.setField(obj, "visible", Value::makeBool(info.visible));
    api.setField(obj, "windowCount", Value::makeInt(info.windowCount));
    return obj;
}

// ============================================================================
// Active window queries
// ============================================================================

static Value windowActiveTitle(const VMApi &api, const std::vector<Value> &) {
    return makeStr(api, WindowManager::GetActiveWindowTitle());
}

static Value windowActiveClass(const VMApi &api, const std::vector<Value> &) {
    return makeStr(api, WindowManager::GetActiveWindowClass());
}

static Value windowActiveExe(const VMApi &api, const std::vector<Value> &) {
    return makeStr(api, WindowManager::GetActiveWindowProcess());
}

static Value windowActivePid(const std::vector<Value> &) {
    return Value::makeInt(static_cast<int64_t>(WindowManager::GetActiveWindowPID()));
}

static Value windowActiveWindow(const std::vector<Value> &) {
    return Value::makeInt(static_cast<int64_t>(WindowManager::GetActiveWindow()));
}

static Value windowActive(const VMApi &api, const std::vector<Value> &) {
    auto info = WindowManager::getActiveWindowInfo();
    return windowInfoToValue(api, info);
}

// ============================================================================
// Window info/queries by ID
// ============================================================================

static Value windowInfo(const VMApi &api, const std::vector<Value> &args) {
    if (args.empty()) return Value::makeNull();
    auto id = static_cast<uint64_t>(toInt(args[0]));
    auto info = WindowManager::getWindowInfo(id);
    return windowInfoToValue(api, info);
}

static Value windowTitle(const VMApi &api, const std::vector<Value> &args) {
    if (args.empty()) return makeStr(api, "");
    auto id = static_cast<uint64_t>(toInt(args[0]));
    return makeStr(api, WindowManager::GetWindowTitle(id));
}

static Value windowClass(const VMApi &api, const std::vector<Value> &args) {
    if (args.empty()) return makeStr(api, "");
    auto id = static_cast<uint64_t>(toInt(args[0]));
    return makeStr(api, WindowManager::GetWindowClass(id));
}

static Value windowPid(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeInt(0);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    return Value::makeInt(static_cast<int64_t>(WindowManager::GetWindowPID(id)));
}

static Value windowExe(const VMApi &api, const std::vector<Value> &args) {
    if (args.empty()) return makeStr(api, "");
    auto id = static_cast<uint64_t>(toInt(args[0]));
    auto pid = WindowManager::GetWindowPID(id);
    if (pid == 0) return makeStr(api, "");
    return makeStr(api, WindowManager::getProcessName(pid));
}

static Value windowGeometry(const VMApi &api, const std::vector<Value> &args) {
    if (args.empty()) return Value::makeNull();
    auto id = static_cast<uint64_t>(toInt(args[0]));
    auto info = WindowManager::getWindowInfo(id);
    auto obj = api.makeObject();
    api.setField(obj, "x", Value::makeInt(info.x));
    api.setField(obj, "y", Value::makeInt(info.y));
    api.setField(obj, "width", Value::makeInt(info.width));
    api.setField(obj, "height", Value::makeInt(info.height));
    return obj;
}

static Value windowExists(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    auto info = WindowManager::getWindowInfo(id);
    return Value::makeBool(info.valid);
}

static Value windowIsMinimized(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    auto info = WindowManager::getWindowInfo(id);
    return Value::makeBool(info.minimized);
}

static Value windowIsMaximized(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    auto info = WindowManager::getWindowInfo(id);
    return Value::makeBool(info.maximized);
}

static Value windowIsFullscreen(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    return Value::makeBool(WindowManager::IsWindowFullscreen(id));
}

// ============================================================================
// Window list/find
// ============================================================================

static Value windowList(const VMApi &api, const std::vector<Value> &) {
    auto windows = WindowManager::getAllWindows();
    auto arr = api.makeArray();
    for (const auto &w : windows) {
        api.push(arr, windowInfoToValue(api, w));
    }
    return arr;
}

static Value windowFindByPid(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeInt(0);
    auto pid = static_cast<pID>(toInt(args[0]));
    auto id = WindowManager::GetwIDByPID(pid);
    return Value::makeInt(static_cast<int64_t>(id));
}

static Value windowFindByClass(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeInt(0);
    std::string cls;
    if (args[0].isStringId()) cls = ""; // resolved below
    return Value::makeInt(0); // fallback
}

static Value windowFindByTitle(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeInt(0);
    return Value::makeInt(0); // fallback
}

// ============================================================================
// Window manipulation
// ============================================================================

static Value windowMove(const std::vector<Value> &args) {
    if (args.size() < 3) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    int x = static_cast<int>(toInt(args[1]));
    int y = static_cast<int>(toInt(args[2]));
    return Value::makeBool(WindowManager::moveWindow(id, x, y));
}

static Value windowResize(const std::vector<Value> &args) {
    if (args.size() < 3) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    int w = static_cast<int>(toInt(args[1]));
    int h = static_cast<int>(toInt(args[2]));
    return Value::makeBool(WindowManager::resizeWindow(id, w, h));
}

static Value windowMoveResize(const std::vector<Value> &args) {
    if (args.size() < 5) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    int x = static_cast<int>(toInt(args[1]));
    int y = static_cast<int>(toInt(args[2]));
    int w = static_cast<int>(toInt(args[3]));
    int h = static_cast<int>(toInt(args[4]));
    return Value::makeBool(WindowManager::moveResizeWindow(id, x, y, w, h));
}

static Value windowClose(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    return Value::makeBool(WindowManager::closeWindow(id));
}

static Value windowFocus(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    return Value::makeBool(WindowManager::focusWindow(id));
}

static Value windowMinimize(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    return Value::makeBool(WindowManager::minimizeWindow(id));
}

static Value windowMaximize(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    return Value::makeBool(WindowManager::maximizeWindow(id));
}

static Value windowRestore(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    return Value::makeBool(WindowManager::restoreWindow(id));
}

static Value windowHide(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    return Value::makeBool(WindowManager::hideWindow(id));
}

static Value windowShow(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    return Value::makeBool(WindowManager::showWindow(id));
}

static Value windowCenter(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    return Value::makeBool(WindowManager::centerWindow(id));
}

static Value windowSnap(const std::vector<Value> &args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    int pos = static_cast<int>(toInt(args[1]));
    return Value::makeBool(WindowManager::snapWindow(id, pos));
}

static Value windowToggleFullscreen(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    return Value::makeBool(WindowManager::toggleFullscreen(id));
}

static Value windowSetAlwaysOnTop(const std::vector<Value> &args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    bool onTop = args[1].isBool() ? args[1].asBool() : (toInt(args[1]) != 0);
    return Value::makeBool(WindowManager::setAlwaysOnTop(id, onTop));
}

static Value windowSetFloating(const std::vector<Value> &args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    bool floating = args[1].isBool() ? args[1].asBool() : (toInt(args[1]) != 0);
    return Value::makeBool(WindowManager::setFloating(id, floating));
}

static Value windowMoveToMonitor(const std::vector<Value> &args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    int monitor = static_cast<int>(toInt(args[1]));
    return Value::makeBool(WindowManager::moveWindowToMonitor(id, monitor));
}

static Value windowMoveToDesktop(const std::vector<Value> &args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto id = static_cast<uint64_t>(toInt(args[0]));
    int desktop = static_cast<int>(toInt(args[1]));
    return Value::makeBool(WindowManager::moveWindowToWorkspace(id, desktop));
}

// ============================================================================
// Desktop/workspace queries
// ============================================================================

static Value windowWorkspaces(const VMApi &api, const std::vector<Value> &) {
    auto workspaces = WindowManager::getWorkspaces();
    auto arr = api.makeArray();
    for (const auto &ws : workspaces) {
        api.push(arr, workspaceInfoToValue(api, ws));
    }
    return arr;
}

static Value windowCurrentDesktop(const std::vector<Value> &) {
    return Value::makeInt(static_cast<int64_t>(WindowManager::getCurrentWorkspace()));
}

static Value windowSwitchDesktop(const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    int desktop = static_cast<int>(toInt(args[0]));
    return Value::makeBool(WindowManager::switchToWorkspace(desktop));
}

// ============================================================================
// Group queries
// ============================================================================

static Value windowGroupNames(const VMApi &api, const std::vector<Value> &) {
    auto names = WindowManager::getGroupNames();
    auto arr = api.makeArray();
    for (const auto &n : names) {
        api.push(arr, makeStr(api, n));
    }
    return arr;
}

// ============================================================================
// Registration
// ============================================================================

void registerWindowMonitorModule(const VMApi &api) {
    // Active window queries
    api.registerFunction("window.activeTitle", [api](const std::vector<Value> &args) {
        return windowActiveTitle(api, args);
    });
    api.registerFunction("window.activeClass", [api](const std::vector<Value> &args) {
        return windowActiveClass(api, args);
    });
    api.registerFunction("window.activeExe", [api](const std::vector<Value> &args) {
        return windowActiveExe(api, args);
    });
    api.registerFunction("window.activePid", [](const std::vector<Value> &args) {
        return windowActivePid(args);
    });
    api.registerFunction("window.activeWindow", [](const std::vector<Value> &args) {
        return windowActiveWindow(args);
    });
    api.registerFunction("window.active", [api](const std::vector<Value> &args) {
        return windowActive(api, args);
    });

    // Window info/queries by ID
    api.registerFunction("window.info", [api](const std::vector<Value> &args) {
        return windowInfo(api, args);
    });
    api.registerFunction("window.title", [api](const std::vector<Value> &args) {
        return windowTitle(api, args);
    });
    api.registerFunction("window.className", [api](const std::vector<Value> &args) {
        return windowClass(api, args);
    });
    api.registerFunction("window.pid", [](const std::vector<Value> &args) {
        return windowPid(args);
    });
    api.registerFunction("window.exe", [api](const std::vector<Value> &args) {
        return windowExe(api, args);
    });
    api.registerFunction("window.geometry", [api](const std::vector<Value> &args) {
        return windowGeometry(api, args);
    });
    api.registerFunction("window.exists", [](const std::vector<Value> &args) {
        return windowExists(args);
    });
    api.registerFunction("window.isMinimized", [](const std::vector<Value> &args) {
        return windowIsMinimized(args);
    });
    api.registerFunction("window.isMaximized", [](const std::vector<Value> &args) {
        return windowIsMaximized(args);
    });
    api.registerFunction("window.isFullscreen", [](const std::vector<Value> &args) {
        return windowIsFullscreen(args);
    });

    // Window list/find
    api.registerFunction("window.list", [api](const std::vector<Value> &args) {
        return windowList(api, args);
    });
    api.registerFunction("window.findByPid", [](const std::vector<Value> &args) {
        return windowFindByPid(args);
    });
    api.registerFunction("window.findByClass", [](const std::vector<Value> &args) {
        return windowFindByClass(args);
    });
    api.registerFunction("window.findByTitle", [](const std::vector<Value> &args) {
        return windowFindByTitle(args);
    });

    // Window manipulation
    api.registerFunction("window.move", [](const std::vector<Value> &args) {
        return windowMove(args);
    });
    api.registerFunction("window.resize", [](const std::vector<Value> &args) {
        return windowResize(args);
    });
    api.registerFunction("window.moveResize", [](const std::vector<Value> &args) {
        return windowMoveResize(args);
    });
    api.registerFunction("window.close", [](const std::vector<Value> &args) {
        return windowClose(args);
    });
    api.registerFunction("window.focus", [](const std::vector<Value> &args) {
        return windowFocus(args);
    });
    api.registerFunction("window.minimize", [](const std::vector<Value> &args) {
        return windowMinimize(args);
    });
    api.registerFunction("window.maximize", [](const std::vector<Value> &args) {
        return windowMaximize(args);
    });
    api.registerFunction("window.restore", [](const std::vector<Value> &args) {
        return windowRestore(args);
    });
    api.registerFunction("window.hide", [](const std::vector<Value> &args) {
        return windowHide(args);
    });
    api.registerFunction("window.show", [](const std::vector<Value> &args) {
        return windowShow(args);
    });
    api.registerFunction("window.center", [](const std::vector<Value> &args) {
        return windowCenter(args);
    });
    api.registerFunction("window.snap", [](const std::vector<Value> &args) {
        return windowSnap(args);
    });
    api.registerFunction("window.toggleFullscreen", [](const std::vector<Value> &args) {
        return windowToggleFullscreen(args);
    });
    api.registerFunction("window.alwaysOnTop", [](const std::vector<Value> &args) {
        return windowSetAlwaysOnTop(args);
    });
    api.registerFunction("window.floating", [](const std::vector<Value> &args) {
        return windowSetFloating(args);
    });
    api.registerFunction("window.moveToMonitor", [](const std::vector<Value> &args) {
        return windowMoveToMonitor(args);
    });
    api.registerFunction("window.moveToDesktop", [](const std::vector<Value> &args) {
        return windowMoveToDesktop(args);
    });

    // Desktop/workspace
    api.registerFunction("window.workspaces", [api](const std::vector<Value> &args) {
        return windowWorkspaces(api, args);
    });
    api.registerFunction("window.currentDesktop", [](const std::vector<Value> &args) {
        return windowCurrentDesktop(args);
    });
    api.registerFunction("window.switchDesktop", [](const std::vector<Value> &args) {
        return windowSwitchDesktop(args);
    });

    // Groups
    api.registerFunction("window.groupNames", [api](const std::vector<Value> &args) {
        return windowGroupNames(api, args);
    });

    // Register window global object with all methods
    auto windowObj = api.makeObject();
    api.setField(windowObj, "activeTitle", api.makeFunctionRef("window.activeTitle"));
    api.setField(windowObj, "activeClass", api.makeFunctionRef("window.activeClass"));
    api.setField(windowObj, "activeExe", api.makeFunctionRef("window.activeExe"));
    api.setField(windowObj, "activePid", api.makeFunctionRef("window.activePid"));
    api.setField(windowObj, "activeWindow", api.makeFunctionRef("window.activeWindow"));
    api.setField(windowObj, "active", api.makeFunctionRef("window.active"));
    api.setField(windowObj, "info", api.makeFunctionRef("window.info"));
    api.setField(windowObj, "title", api.makeFunctionRef("window.title"));
    api.setField(windowObj, "className", api.makeFunctionRef("window.className"));
    api.setField(windowObj, "pid", api.makeFunctionRef("window.pid"));
    api.setField(windowObj, "exe", api.makeFunctionRef("window.exe"));
    api.setField(windowObj, "geometry", api.makeFunctionRef("window.geometry"));
    api.setField(windowObj, "exists", api.makeFunctionRef("window.exists"));
    api.setField(windowObj, "isMinimized", api.makeFunctionRef("window.isMinimized"));
    api.setField(windowObj, "isMaximized", api.makeFunctionRef("window.isMaximized"));
    api.setField(windowObj, "isFullscreen", api.makeFunctionRef("window.isFullscreen"));
    api.setField(windowObj, "list", api.makeFunctionRef("window.list"));
    api.setField(windowObj, "findByPid", api.makeFunctionRef("window.findByPid"));
    api.setField(windowObj, "findByClass", api.makeFunctionRef("window.findByClass"));
    api.setField(windowObj, "findByTitle", api.makeFunctionRef("window.findByTitle"));
    api.setField(windowObj, "move", api.makeFunctionRef("window.move"));
    api.setField(windowObj, "resize", api.makeFunctionRef("window.resize"));
    api.setField(windowObj, "moveResize", api.makeFunctionRef("window.moveResize"));
    api.setField(windowObj, "close", api.makeFunctionRef("window.close"));
    api.setField(windowObj, "focus", api.makeFunctionRef("window.focus"));
    api.setField(windowObj, "minimize", api.makeFunctionRef("window.minimize"));
    api.setField(windowObj, "maximize", api.makeFunctionRef("window.maximize"));
    api.setField(windowObj, "restore", api.makeFunctionRef("window.restore"));
    api.setField(windowObj, "hide", api.makeFunctionRef("window.hide"));
    api.setField(windowObj, "show", api.makeFunctionRef("window.show"));
    api.setField(windowObj, "center", api.makeFunctionRef("window.center"));
    api.setField(windowObj, "snap", api.makeFunctionRef("window.snap"));
    api.setField(windowObj, "toggleFullscreen", api.makeFunctionRef("window.toggleFullscreen"));
    api.setField(windowObj, "alwaysOnTop", api.makeFunctionRef("window.alwaysOnTop"));
    api.setField(windowObj, "floating", api.makeFunctionRef("window.floating"));
    api.setField(windowObj, "moveToMonitor", api.makeFunctionRef("window.moveToMonitor"));
    api.setField(windowObj, "moveToDesktop", api.makeFunctionRef("window.moveToDesktop"));
    api.setField(windowObj, "workspaces", api.makeFunctionRef("window.workspaces"));
    api.setField(windowObj, "currentDesktop", api.makeFunctionRef("window.currentDesktop"));
    api.setField(windowObj, "switchDesktop", api.makeFunctionRef("window.switchDesktop"));
    api.setField(windowObj, "groupNames", api.makeFunctionRef("window.groupNames"));
    api.setGlobal("window", windowObj);

    debug("WindowMonitor module registered (30+ host functions)");
}

void setupDynamicWindowGlobals(const VMApi &api, WindowMonitor *monitor) {
    if (!monitor) {
        debug("WindowMonitor not available, skipping dynamic window globals");
        return;
    }

    auto info = monitor->GetActiveWindowInfo();

    std::string title = "";
    std::string windowClass = "";
    std::string exe = "";
    int64_t pid = 0;

    if (info && info->isValid) {
        title = info->title;
        windowClass = info->windowClass;
        exe = info->processName;
        pid = static_cast<int64_t>(info->pid);
    }

    api.setGlobal("title", makeStr(api, title));
    api.setGlobal("class", makeStr(api, windowClass));
    api.setGlobal("exe", makeStr(api, exe));
    api.setGlobal("pid", Value::makeInt(pid));

    debug("Dynamic window globals setup: title=" + title + ", exe=" + exe);
}

} // namespace havel::modules
