#include "AltTabGuiModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "utils/Logger.hpp"
#include "extensions/gui/alt_tab/AltTab.hpp"
#include <QApplication>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static const char* MODULE_MARKER = "__alt_tab_gui_module";

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

static havel::AltTabWindow* g_window = nullptr;

static auto makeCreateFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (!QApplication::instance()) return Value::makeBool(false);
        
        if (g_window) {
            g_window->show();
            g_window->raise();
            g_window->raise();
            return Value::makeBool(true);
        }
        
        g_window = new havel::AltTabWindow();
        g_window->setAttribute(Qt::WA_DeleteOnClose);
        g_window->showAltTab();
        
        return Value::makeBool(true);
    };
}

static auto makeShowFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->show();
            g_window->raise();
            g_window->raise();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeHideFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->hideAltTab();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeToggleFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            if (g_window->isVisible()) g_window->hideAltTab();
            else g_window->showAltTab();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeNextFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->nextWindow();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makePrevFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->prevWindow();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeSelectFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->selectCurrentWindow();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeRefreshFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->refreshWindows();
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

static auto makeSetThumbnailSizeFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (g_window && args.size() >= 2) {
            int w = args[0].isInt() ? static_cast<int>(args[0].asInt()) : 128;
            int h = args[1].isInt() ? static_cast<int>(args[1].asInt()) : 128;
            g_window->setThumbnailSize(w, h);
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeSetMaxVisibleFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (g_window && !args.empty()) {
            int count = args[0].isInt() ? static_cast<int>(args[0].asInt()) : 10;
            g_window->setMaxVisibleWindows(count);
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeSetAnimationsFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (g_window && !args.empty()) {
            bool enabled = args[0].isBool() ? args[0].asBool() : true;
            g_window->setAnimationsEnabled(enabled);
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeGetWindowsFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            auto windows = g_window->getWindows();
            auto result = api.makeArray();
            for (const auto& w : windows) {
                auto item = api.makeObject();
                api.setField(item, "title", api.makeString(w.title));
                api.setField(item, "className", api.makeString(w.className));
                api.setField(item, "width", Value::makeInt(w.width));
                api.setField(item, "height", Value::makeInt(w.height));
                api.setField(item, "isActive", Value::makeBool(w.isActive));
                api.push(result, item);
            }
            return result;
        }
        return api.makeNull();
    };
}

void registerAltTabGuiModule(const VMApi& api) {
    HAVEL_BEGIN_MODULE("AltTabGui");

    HAVEL_REGISTER_FUNCTION(api, "alt_tab_gui.create", makeCreateFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "alt_tab_gui.show", makeShowFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "alt_tab_gui.hide", makeHideFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "alt_tab_gui.toggle", makeToggleFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "alt_tab_gui.next", makeNextFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "alt_tab_gui.prev", makePrevFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "alt_tab_gui.select", makeSelectFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "alt_tab_gui.refresh", makeRefreshFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "alt_tab_gui.isVisible", makeIsVisibleFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "alt_tab_gui.setThumbnailSize", makeSetThumbnailSizeFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "alt_tab_gui.setMaxVisible", makeSetMaxVisibleFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "alt_tab_gui.setAnimations", makeSetAnimationsFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "alt_tab_gui.getWindows", makeGetWindowsFunc(api));

    auto obj = api.makeObject();
    api.setGlobal("alt_tab_gui", obj);
    api.setField(obj, "__alt_tab_gui_module", Value::makeBool(true));
    api.setField(obj, "create", api.makeFunctionRef("alt_tab_gui.create"));
    api.setField(obj, "show", api.makeFunctionRef("alt_tab_gui.show"));
    api.setField(obj, "hide", api.makeFunctionRef("alt_tab_gui.hide"));
    api.setField(obj, "toggle", api.makeFunctionRef("alt_tab_gui.toggle"));
    api.setField(obj, "next", api.makeFunctionRef("alt_tab_gui.next"));
    api.setField(obj, "prev", api.makeFunctionRef("alt_tab_gui.prev"));
    api.setField(obj, "select", api.makeFunctionRef("alt_tab_gui.select"));
    api.setField(obj, "refresh", api.makeFunctionRef("alt_tab_gui.refresh"));
    api.setField(obj, "isVisible", api.makeFunctionRef("alt_tab_gui.isVisible"));
    api.setField(obj, "setThumbnailSize", api.makeFunctionRef("alt_tab_gui.setThumbnailSize"));
    api.setField(obj, "setMaxVisible", api.makeFunctionRef("alt_tab_gui.setMaxVisible"));
    api.setField(obj, "setAnimations", api.makeFunctionRef("alt_tab_gui.setAnimations"));
    api.setField(obj, "getWindows", api.makeFunctionRef("alt_tab_gui.getWindows"));

    HAVEL_END_MODULE();
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(alt_tab_gui, "1.0.0", "Alt-Tab GUI module",
    havel::modules::registerAltTabGuiModule(*api);
)
#endif
