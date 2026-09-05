#include "MapManagerGuiModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "utils/Logger.hpp"
#include "extensions/gui/map_manager/MapManagerWindow.hpp"
#include "core/io/MapManager.hpp"
#include "host/io/MapManagerService.hpp"
#include "core/io/IO.hpp"
#include <QApplication>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static const char* MODULE_MARKER = "__map_manager_gui_module";

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

static MapManagerWindow* g_window = nullptr;

static host::MapManagerService* getService() {
    auto svc = host::ServiceRegistry::instance().get<host::MapManagerService>();
    if (!svc) debug("MapManagerGuiModule: MapManagerService not available");
    return svc.get();
}

static MapManager* getMapManager() {
    auto* svc = getService();
    if (!svc) return nullptr;
    return svc->getMapManager().get();
}

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
        
        auto* mm = getMapManager();
        if (!mm) return Value::makeBool(false);
        
        // Get IO from host context
        IO* io = nullptr;
        if (const auto* hc = api.vm().hostContext()) {
            io = hc->io;
        }
        
        g_window = new MapManagerWindow(mm, io);
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

static auto makeSaveFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        // MapManager doesn't have saveConfig - placeholder
        return Value::makeBool(false);
    };
}

static auto makeLoadFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        // MapManager doesn't have loadConfig - placeholder
        return Value::makeBool(false);
    };
}

void registerMapManagerGuiModule(const VMApi& api) {
    HAVEL_BEGIN_MODULE("MapManagerGui");

    HAVEL_REGISTER_FUNCTION(api, "map_manager_gui.create", makeCreateFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "map_manager_gui.show", makeShowFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "map_manager_gui.hide", makeHideFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "map_manager_gui.toggle", makeToggleFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "map_manager_gui.isVisible", makeIsVisibleFunc(api));
    auto obj = api.makeObject();
    api.setGlobal("map_manager_gui", obj);
    api.setField(obj, "__map_manager_gui_module", Value::makeBool(true));
    api.setField(obj, "create", api.makeFunctionRef("map_manager_gui.create"));
    api.setField(obj, "show", api.makeFunctionRef("map_manager_gui.show"));
    api.setField(obj, "hide", api.makeFunctionRef("map_manager_gui.hide"));
    api.setField(obj, "toggle", api.makeFunctionRef("map_manager_gui.toggle"));
    api.setField(obj, "isVisible", api.makeFunctionRef("map_manager_gui.isVisible"));
    // save/load not implemented in MapManager

    HAVEL_END_MODULE();
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(map_manager_gui, "1.0.0", "Map Manager GUI module",
    havel::modules::registerMapManagerGuiModule(*api);
)
#endif
