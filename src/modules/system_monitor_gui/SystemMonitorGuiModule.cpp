#include "SystemMonitorGuiModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "utils/Logger.hpp"
#include "extensions/gui/system_monitor/SystemMonitor.hpp"
#include <QApplication>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static const char* MODULE_MARKER = "__system_monitor_gui_module";

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

static SystemMonitor* g_window = nullptr;

static auto makeCreateFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (!QApplication::instance()) return Value::makeBool(false);
        
        if (g_window) {
            g_window->show();
            g_window->raise();
            g_window->activateWindow();
            return Value::makeBool(true);
        }
        
        g_window = new SystemMonitor();
        g_window->setAttribute(Qt::WA_DeleteOnClose);
        g_window->show();
        
        return Value::makeBool(true);
    };
}

static auto makeShowFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->show();
            g_window->raise();
            g_window->activateWindow();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeHideFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->hide();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeToggleFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            if (g_window->isVisible()) g_window->hide();
            else {
                g_window->show();
                g_window->raise();
                g_window->activateWindow();
            }
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeIsVisibleFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) return Value::makeBool(g_window->isVisible());
        return Value::makeBool(false);
    };
}

void registerSystemMonitorGuiModule(const VMApi& api) {
    HAVEL_BEGIN_MODULE("SystemMonitorGui");

    HAVEL_REGISTER_FUNCTION(api, "system_monitor_gui.create", makeCreateFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "system_monitor_gui.show", makeShowFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "system_monitor_gui.hide", makeHideFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "system_monitor_gui.toggle", makeToggleFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "system_monitor_gui.isVisible", makeIsVisibleFunc(api));

    auto obj = api.makeObject();
    api.setGlobal("system_monitor_gui", obj);
    api.setField(obj, "__system_monitor_gui_module", Value::makeBool(true));
    api.setField(obj, "create", api.makeFunctionRef("system_monitor_gui.create"));
    api.setField(obj, "show", api.makeFunctionRef("system_monitor_gui.show"));
    api.setField(obj, "hide", api.makeFunctionRef("system_monitor_gui.hide"));
    api.setField(obj, "toggle", api.makeFunctionRef("system_monitor_gui.toggle"));
    api.setField(obj, "isVisible", api.makeFunctionRef("system_monitor_gui.isVisible"));

    HAVEL_END_MODULE();;
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(system_monitor_gui, "1.0.0", "System Monitor GUI module",
    havel::modules::registerSystemMonitorGuiModule(*api);
)
#endif
