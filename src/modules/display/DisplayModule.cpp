/*
 * DisplayModule.cpp - Display management module for bytecode VM
 * Provides monitor enumeration, display server detection, WM info
 * Uses DisplayManager singleton (XRandR/Wayland abstracted)
 */
#include "DisplayModule.hpp"
#include "core/display/DisplayManager.hpp"
#include "core/window/WindowManager.hpp"
#include "core/window/WindowManagerDetector.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static Value makeStr(const VMApi &api, const std::string &s) {
    return api.makeString(s);
}

static Value monitorInfoToValue(const VMApi &api, const DisplayManager::MonitorInfo &mon) {
    auto obj = api.makeObject();
    api.setField(obj, "name", makeStr(api, mon.name));
    api.setField(obj, "x", Value::makeInt(static_cast<int64_t>(mon.x)));
    api.setField(obj, "y", Value::makeInt(static_cast<int64_t>(mon.y)));
    api.setField(obj, "width", Value::makeInt(static_cast<int64_t>(mon.width)));
    api.setField(obj, "height", Value::makeInt(static_cast<int64_t>(mon.height)));
    api.setField(obj, "isPrimary", Value::makeBool(mon.isPrimary));
    return obj;
}

// display.monitors() - Get all monitors
static Value displayMonitors(const VMApi &api, const std::vector<Value> &) {
    auto monitors = DisplayManager::GetMonitors();
    auto arr = api.makeArray();
    for (const auto &mon : monitors) {
        api.push(arr, monitorInfoToValue(api, mon));
    }
    return arr;
}

// display.primary() - Get primary monitor
static Value displayPrimary(const VMApi &api, const std::vector<Value> &) {
    auto mon = DisplayManager::GetPrimaryMonitor();
    return monitorInfoToValue(api, mon);
}

// display.count() - Get monitor count
static Value displayCount(const std::vector<Value> &) {
    auto monitors = DisplayManager::GetMonitors();
    return Value::makeInt(static_cast<int64_t>(monitors.size()));
}

// display.area() - Get total monitors area
static Value displayArea(const VMApi &api, const std::vector<Value> &) {
    auto monitors = DisplayManager::GetMonitors();
    int64_t minX = INT64_MAX, minY = INT64_MAX;
    int64_t maxX = INT64_MIN, maxY = INT64_MIN;
    for (const auto &mon : monitors) {
        if (static_cast<int64_t>(mon.x) < minX) minX = mon.x;
        if (static_cast<int64_t>(mon.y) < minY) minY = mon.y;
        if (static_cast<int64_t>(mon.x) + mon.width > maxX) maxX = mon.x + mon.width;
        if (static_cast<int64_t>(mon.y) + mon.height > maxY) maxY = mon.y + mon.height;
    }
    int64_t totalWidth = (minX != INT64_MAX) ? maxX - minX : 0;
    int64_t totalHeight = (minY != INT64_MAX) ? maxY - minY : 0;
    auto obj = api.makeObject();
    api.setField(obj, "x", Value::makeInt(minX == INT64_MAX ? 0 : minX));
    api.setField(obj, "y", Value::makeInt(minY == INT64_MAX ? 0 : minY));
    api.setField(obj, "width", Value::makeInt(totalWidth));
    api.setField(obj, "height", Value::makeInt(totalHeight));
    return obj;
}

// display.monitorAt(x, y) - Get monitor at position
static Value displayMonitorAt(const VMApi &api, const std::vector<Value> &args) {
    if (args.size() < 2) return Value::makeNull();
    int x = 0, y = 0;
    if (args[0].isInt()) x = static_cast<int>(args[0].asInt());
    if (args[0].isDouble()) x = static_cast<int>(args[0].asDouble());
    if (args[1].isInt()) y = static_cast<int>(args[1].asInt());
    if (args[1].isDouble()) y = static_cast<int>(args[1].asDouble());
    auto mon = DisplayManager::GetMonitorAt(x, y);
    return monitorInfoToValue(api, mon);
}

// display.monitorByName(name) - Get monitor by name
static Value displayMonitorByName(const VMApi &api, const std::vector<Value> &args) {
    if (args.empty()) return Value::makeNull();
    std::string name = api.toString(args[0]);
    auto mon = DisplayManager::GetMonitorByName(name);
    return monitorInfoToValue(api, mon);
}

// display.names() - Get monitor names
static Value displayNames(const VMApi &api, const std::vector<Value> &) {
    auto names = DisplayManager::GetMonitorNames();
    auto arr = api.makeArray();
    for (const auto &n : names) {
        api.push(arr, makeStr(api, n));
    }
    return arr;
}

// display.resolutions() - Get resolution strings
static Value displayResolutions(const VMApi &api, const std::vector<Value> &) {
    auto monitors = DisplayManager::GetMonitors();
    auto arr = api.makeArray();
    for (const auto &mon : monitors) {
        std::string res = std::to_string(mon.width) + "x" + std::to_string(mon.height);
        api.push(arr, makeStr(api, res));
    }
    return arr;
}

// display.isX11()
static Value displayIsX11(const std::vector<Value> &) {
    return Value::makeBool(WindowManager::IsX11());
}

// display.isWayland()
static Value displayIsWayland(const std::vector<Value> &) {
    return Value::makeBool(WindowManager::IsWayland());
}

// display.isWindows()
static Value displayIsWindows(const std::vector<Value> &) {
#ifdef WINDOWS
    return Value::makeBool(true);
#else
    return Value::makeBool(false);
#endif
}

// display.protocol()
static Value displayProtocol(const VMApi &api, const std::vector<Value> &) {
    std::string protocol = "unknown";
    const char *waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    const char *x11Display = std::getenv("DISPLAY");
    const char *xdgSession = std::getenv("XDG_SESSION_TYPE");
    if (waylandDisplay && waylandDisplay[0] != '\0')
        protocol = "wayland";
    if (x11Display && x11Display[0] != '\0') {
        if (protocol == "unknown")
            protocol = "x11";
    }
    if (xdgSession && xdgSession[0] != '\0') {
        std::string session = xdgSession;
        if (session == "wayland") protocol = "wayland";
        else if (session == "x11") protocol = "x11";
        else if (session == "tty") protocol = "tty";
    }
    return makeStr(api, protocol);
}

// display.wm()
static Value displayWm(const VMApi &api, const std::vector<Value> &) {
    std::string wmName = WindowManagerDetector::GetWMName();
    return makeStr(api, wmName);
}

// display.displayNum()
static Value displayDisplayNum(const std::vector<Value> &) {
    const char *display = std::getenv("DISPLAY");
    if (!display || display[0] == '\0')
        return Value::makeInt(0);
    std::string dpy(display);
    auto colonPos = dpy.find(':');
    if (colonPos == std::string::npos)
        return Value::makeInt(0);
    auto dotPos = dpy.find('.', colonPos);
    if (dotPos == std::string::npos)
        return Value::makeInt(0);
    std::string numStr = dpy.substr(colonPos + 1, dotPos - colonPos - 1);
    try {
        return Value::makeInt(std::stoi(numStr));
    } catch (...) {
        return Value::makeInt(0);
    }
}

void registerDisplayModule(const VMApi &api) {
    api.registerFunction("display.monitors", [api](const std::vector<Value> &args) {
        return displayMonitors(api, args);
    });
    api.registerFunction("display.primary", [api](const std::vector<Value> &args) {
        return displayPrimary(api, args);
    });
    api.registerFunction("display.count", [](const std::vector<Value> &args) {
        return displayCount(args);
    });
    api.registerFunction("display.area", [api](const std::vector<Value> &args) {
        return displayArea(api, args);
    });
    api.registerFunction("display.monitorAt", [api](const std::vector<Value> &args) {
        return displayMonitorAt(api, args);
    });
    api.registerFunction("display.monitorByName", [api](const std::vector<Value> &args) {
        return displayMonitorByName(api, args);
    });
    api.registerFunction("display.names", [api](const std::vector<Value> &args) {
        return displayNames(api, args);
    });
    api.registerFunction("display.resolutions", [api](const std::vector<Value> &args) {
        return displayResolutions(api, args);
    });
    api.registerFunction("display.isX11", [](const std::vector<Value> &args) {
        return displayIsX11(args);
    });
    api.registerFunction("display.isWayland", [](const std::vector<Value> &args) {
        return displayIsWayland(args);
    });
    api.registerFunction("display.isWindows", [](const std::vector<Value> &args) {
        return displayIsWindows(args);
    });
    api.registerFunction("display.protocol", [api](const std::vector<Value> &args) {
        return displayProtocol(api, args);
    });
    api.registerFunction("display.wm", [api](const std::vector<Value> &args) {
        return displayWm(api, args);
    });
    api.registerFunction("display.displayNum", [](const std::vector<Value> &args) {
        return displayDisplayNum(args);
    });

    // Register display global object with all methods
    auto displayObj = api.makeObject();
    api.setField(displayObj, "monitors", api.makeFunctionRef("display.monitors"));
    api.setField(displayObj, "primary", api.makeFunctionRef("display.primary"));
    api.setField(displayObj, "count", api.makeFunctionRef("display.count"));
    api.setField(displayObj, "area", api.makeFunctionRef("display.area"));
    api.setField(displayObj, "monitorAt", api.makeFunctionRef("display.monitorAt"));
    api.setField(displayObj, "monitorByName", api.makeFunctionRef("display.monitorByName"));
    api.setField(displayObj, "names", api.makeFunctionRef("display.names"));
    api.setField(displayObj, "resolutions", api.makeFunctionRef("display.resolutions"));
    api.setField(displayObj, "isX11", api.makeFunctionRef("display.isX11"));
    api.setField(displayObj, "isWayland", api.makeFunctionRef("display.isWayland"));
    api.setField(displayObj, "isWindows", api.makeFunctionRef("display.isWindows"));
    api.setField(displayObj, "protocol", api.makeFunctionRef("display.protocol"));
    api.setField(displayObj, "wm", api.makeFunctionRef("display.wm"));
    api.setField(displayObj, "displayNum", api.makeFunctionRef("display.displayNum"));
    api.setGlobal("display", displayObj);

    debug("Display module registered (14 host functions)");
}

} // namespace havel::modules
