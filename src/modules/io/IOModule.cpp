/* IOModule.cpp - Minimal C++ shim for IO module
 * Raw passthrough host functions calling havel::IO* directly.
 * Business logic (defaults, mode strings, error wrapping, double-click)
 * lives in the pure-Havel sidecar modules/app/io.hv.
 */
#include "IOModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "core/io/IO.hpp"
#include "core/io/HotkeyExecutor.hpp"
#include "utils/Logger.hpp"
#include <thread>
#include <chrono>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static IO* getIO() {
    auto io = host::ServiceRegistry::instance().get<IO>();
    if (!io) debug("IOModule: IO not available");
    return io.get();
}

static std::string toStr(const VMApi& api, const Value& v) {
    if (v.isStringId() || v.isStringValId()) return api.toString(v);
    if (v.isInt()) return std::to_string(v.asInt());
    if (v.isDouble()) return std::to_string(v.asDouble());
    if (v.isBool()) return v.asBool() ? "true" : "false";
    return "";
}

void registerIOModule(const VMApi& api) {
    HAVEL_BEGIN_MODULE("IO");

    // --- Keyboard ---
    api.registerFunction("io._send", [api](const std::vector<Value>& args) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        if (args.empty()) return Value::makeBool(false);
        io->Send(toStr(api, args[0]).c_str());
        return Value::makeBool(true);
    });

    api.registerFunction("io._sendX11Key", [api](const std::vector<Value>& args) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        if (args.size() < 2) return Value::makeBool(false);
        bool press = args[1].isBool() ? args[1].asBool() : args[1].asInt() != 0;
        io->SendX11Key(toStr(api, args[0]), press);
        return Value::makeBool(true);
    });

    api.registerFunction("io._map", [api](const std::vector<Value>& args) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        if (args.size() < 2) return Value::makeBool(false);
        io->Map(toStr(api, args[0]), toStr(api, args[1]));
        return Value::makeBool(true);
    });

    api.registerFunction("io._remap", [api](const std::vector<Value>& args) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        if (args.size() < 2) return Value::makeBool(false);
        io->Remap(toStr(api, args[0]), toStr(api, args[1]));
        return Value::makeBool(true);
    });

    // --- Emergency / grab ---
    api.registerFunction("io._emergencyRelease", [](const std::vector<Value>&) {
        auto* io = getIO(); if (!io) return Value::makeNull();
        io->EmergencyReleaseAllKeys();
        return Value::makeNull();
    });

    api.registerFunction("io._ungrabAll", [](const std::vector<Value>&) {
        auto* io = getIO(); if (!io) return Value::makeNull();
        io->UngrabAll();
        return Value::makeNull();
    });

    // --- Suspend / resume ---
    api.registerFunction("io._suspend", [](const std::vector<Value>&) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        return Value::makeBool(io->Suspend());
    });

    api.registerFunction("io._resume", [](const std::vector<Value>&) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        if (io->isSuspended) return Value::makeBool(io->Resume());
        return Value::makeBool(true);
    });

    api.registerFunction("io._isSuspended", [](const std::vector<Value>&) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        return Value::makeBool(io->IsSuspended());
    });

    // --- Key state ---
    api.registerFunction("io._isKeyPressed", [api](const std::vector<Value>& args) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        if (args.empty()) return Value::makeBool(false);
        return Value::makeBool(io->IsKeyPressed(toStr(api, args[0])));
    });

    api.registerFunction("io._isShiftPressed", [](const std::vector<Value>&) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        return Value::makeBool(io->IsShiftPressed());
    });

    api.registerFunction("io._isCtrlPressed", [](const std::vector<Value>&) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        return Value::makeBool(io->IsCtrlPressed());
    });

    api.registerFunction("io._isAltPressed", [](const std::vector<Value>&) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        return Value::makeBool(io->IsAltPressed());
    });

    api.registerFunction("io._isWinPressed", [](const std::vector<Value>&) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        return Value::makeBool(io->IsWinPressed());
    });

    api.registerFunction("io._getCurrentModifiers", [](const std::vector<Value>&) {
        auto* io = getIO(); if (!io) return Value::makeInt(0);
        return Value::makeInt(io->GetCurrentModifiers());
    });

    // --- Executor mode (int-based: 0=Scheduler, 1=Executor, 2=Sync, 3=Thread) ---
    api.registerFunction("io._setExecutorMode", [](const std::vector<Value>& args) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        if (args.empty()) return Value::makeBool(false);
        int m = args[0].isInt() ? args[0].asInt() : 0;
        io->SetExecutorMode(static_cast<ExecutorMode>(m));
        return Value::makeBool(true);
    });

    api.registerFunction("io._getExecutorMode", [](const std::vector<Value>&) {
        auto* io = getIO(); if (!io) return Value::makeInt(0);
        return Value::makeInt(static_cast<int>(io->GetExecutorMode()));
    });

    // --- Mouse ---
    api.registerFunction("io._mouseMove", [](const std::vector<Value>& args) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        if (args.size() < 2) return Value::makeBool(false);
        int dx = args[0].isInt() ? args[0].asInt() : 0;
        int dy = args[1].isInt() ? args[1].asInt() : 0;
        int speed = args.size() > 2 && args[2].isInt() ? args[2].asInt() : 1;
        return Value::makeBool(io->MouseMove(dx, dy, speed));
    });

    api.registerFunction("io._mouseMoveTo", [](const std::vector<Value>& args) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        if (args.size() < 2) return Value::makeBool(false);
        int x = args[0].isInt() ? args[0].asInt() : 0;
        int y = args[1].isInt() ? args[1].asInt() : 0;
        int speed = args.size() > 2 && args[2].isInt() ? args[2].asInt() : 1;
        return Value::makeBool(io->MouseMoveTo(x, y, speed));
    });

    api.registerFunction("io._mouseClick", [](const std::vector<Value>& args) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        int btn = !args.empty() && args[0].isInt() ? args[0].asInt() : 1;
        io->MouseClick(btn);
        return Value::makeBool(true);
    });

    api.registerFunction("io._mouseDown", [](const std::vector<Value>& args) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        int btn = !args.empty() && args[0].isInt() ? args[0].asInt() : 1;
        return Value::makeBool(io->MouseDown(btn));
    });

    api.registerFunction("io._mouseUp", [](const std::vector<Value>& args) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        int btn = !args.empty() && args[0].isInt() ? args[0].asInt() : 1;
        return Value::makeBool(io->MouseUp(btn));
    });

    api.registerFunction("io._scroll", [](const std::vector<Value>& args) {
        auto* io = getIO(); if (!io) return Value::makeBool(false);
        double dy = !args.empty() && (args[0].isDouble() || args[0].isInt())
            ? (args[0].isDouble() ? args[0].asDouble() : static_cast<double>(args[0].asInt())) : 0.0;
        double dx = args.size() > 1 && (args[1].isDouble() || args[1].isInt())
            ? (args[1].isDouble() ? args[1].asDouble() : static_cast<double>(args[1].asInt())) : 0.0;
        return Value::makeBool(io->Scroll(dy, dx));
    });

    api.registerFunction("io._getMousePosition", [api](const std::vector<Value>&) {
        auto* io = getIO(); if (!io) return Value::makeNull();
        auto pos = io->GetMousePosition();
        auto arr = api.makeArray();
        api.push(arr, Value::makeInt(pos.first));
        api.push(arr, Value::makeInt(pos.second));
        return arr;
    });

    api.registerFunction("io._setMouseSensitivity", [](const std::vector<Value>& args) {
        auto* io = getIO(); if (!io) return Value::makeNull();
        if (args.empty()) return Value::makeNull();
        double s = args[0].isDouble() ? args[0].asDouble()
            : args[0].isInt() ? static_cast<double>(args[0].asInt()) : 1.0;
        io->SetMouseSensitivity(s);
        return Value::makeNull();
    });

    api.registerFunction("io._getMouseSensitivity", [](const std::vector<Value>&) {
        auto* io = getIO(); if (!io) return Value::makeDouble(1.0);
        return Value::makeDouble(io->GetMouseSensitivity());
    });

    // --- Utility shim (for double-click timing) ---
    api.registerFunction("io._sleepMs", [](const std::vector<Value>& args) {
        if (args.empty()) return Value::makeNull();
        int ms = args[0].isInt() ? args[0].asInt() : 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return Value::makeNull();
    });

    // Build io namespace object with shim function refs
    auto obj = api.makeObject();
    api.setField(obj, "_send", api.makeFunctionRef("io._send"));
    api.setField(obj, "_sendX11Key", api.makeFunctionRef("io._sendX11Key"));
    api.setField(obj, "_map", api.makeFunctionRef("io._map"));
    api.setField(obj, "_remap", api.makeFunctionRef("io._remap"));
    api.setField(obj, "_emergencyRelease", api.makeFunctionRef("io._emergencyRelease"));
    api.setField(obj, "_ungrabAll", api.makeFunctionRef("io._ungrabAll"));
    api.setField(obj, "_suspend", api.makeFunctionRef("io._suspend"));
    api.setField(obj, "_resume", api.makeFunctionRef("io._resume"));
    api.setField(obj, "_isSuspended", api.makeFunctionRef("io._isSuspended"));
    api.setField(obj, "_isKeyPressed", api.makeFunctionRef("io._isKeyPressed"));
    api.setField(obj, "_isShiftPressed", api.makeFunctionRef("io._isShiftPressed"));
    api.setField(obj, "_isCtrlPressed", api.makeFunctionRef("io._isCtrlPressed"));
    api.setField(obj, "_isAltPressed", api.makeFunctionRef("io._isAltPressed"));
    api.setField(obj, "_isWinPressed", api.makeFunctionRef("io._isWinPressed"));
    api.setField(obj, "_getCurrentModifiers", api.makeFunctionRef("io._getCurrentModifiers"));
    api.setField(obj, "_setExecutorMode", api.makeFunctionRef("io._setExecutorMode"));
    api.setField(obj, "_getExecutorMode", api.makeFunctionRef("io._getExecutorMode"));
    api.setField(obj, "_mouseMove", api.makeFunctionRef("io._mouseMove"));
    api.setField(obj, "_mouseMoveTo", api.makeFunctionRef("io._mouseMoveTo"));
    api.setField(obj, "_mouseClick", api.makeFunctionRef("io._mouseClick"));
    api.setField(obj, "_mouseDown", api.makeFunctionRef("io._mouseDown"));
    api.setField(obj, "_mouseUp", api.makeFunctionRef("io._mouseUp"));
    api.setField(obj, "_scroll", api.makeFunctionRef("io._scroll"));
    api.setField(obj, "_getMousePosition", api.makeFunctionRef("io._getMousePosition"));
    api.setField(obj, "_setMouseSensitivity", api.makeFunctionRef("io._setMouseSensitivity"));
    api.setField(obj, "_getMouseSensitivity", api.makeFunctionRef("io._getMouseSensitivity"));
    api.setField(obj, "_sleepMs", api.makeFunctionRef("io._sleepMs"));
    api.setGlobal("io", obj);

    auto& vm = api.vm();
    Value exports;
    try {
        exports = vm.loadModule("app/io");
    } catch (...) {}

    if (exports.isObjectId()) {
        auto* expObj = vm.getHeap().object(exports.asObjectId());
        if (expObj) {
            for (const auto& [name, value] : *expObj) {
                if (name.empty() || name[0] == '_') continue;
                api.setField(obj, name, value);
                api.setGlobal(name, value);
            }
        }
    }

    HAVEL_END_MODULE();
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(io, "1.0.0", "IO operations module",
    havel::modules::registerIOModule(*api);
)
#endif
