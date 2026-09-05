#include "FileAutomatorGuiModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "utils/Logger.hpp"
#include "extensions/gui/file_automator/FileAutomator.hpp"
#include <QApplication>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static const char* MODULE_MARKER = "__file_automator_gui_module";

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

static FileAutomator* g_window = nullptr;

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
        
        g_window = new FileAutomator();
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

void registerFileAutomatorGuiModule(const VMApi& api) {
    HAVEL_BEGIN_MODULE("FileAutomatorGui");

    HAVEL_REGISTER_FUNCTION(api, "file_automator_gui.create", makeCreateFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "file_automator_gui.show", makeShowFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "file_automator_gui.hide", makeHideFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "file_automator_gui.toggle", makeToggleFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "file_automator_gui.isVisible", makeIsVisibleFunc(api));

    auto obj = api.makeObject();
    api.setGlobal("file_automator_gui", obj);
    api.setField(obj, "__file_automator_gui_module", Value::makeBool(true));
    api.setField(obj, "create", api.makeFunctionRef("file_automator_gui.create"));
    api.setField(obj, "show", api.makeFunctionRef("file_automator_gui.show"));
    api.setField(obj, "hide", api.makeFunctionRef("file_automator_gui.hide"));
    api.setField(obj, "toggle", api.makeFunctionRef("file_automator_gui.toggle"));
    api.setField(obj, "isVisible", api.makeFunctionRef("file_automator_gui.isVisible"));

    HAVEL_END_MODULE();
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(file_automator_gui, "1.0.0", "File Automator GUI module",
    havel::modules::registerFileAutomatorGuiModule(*api);
)
#endif
