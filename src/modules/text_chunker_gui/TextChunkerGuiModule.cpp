#include "TextChunkerGuiModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "utils/Logger.hpp"
#include "extensions/gui/text_chunker/TextChunkerWindow.hpp"
#include <QApplication>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static const char* MODULE_MARKER = "__text_chunker_gui_module";

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

static havel::gui::TextChunkerWindow* g_window = nullptr;

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
        
        std::string text = args.empty() ? "" : (args[0].isStringValId() ? api.resolveString(args[0]) : "");
        size_t size = args.size() > 1 && args[1].isInt() ? static_cast<size_t>(args[1].asInt()) : 20000;
        bool tail = args.size() > 2 && args[2].isBool() ? args[2].asBool() : false;
        
        g_window = new havel::gui::TextChunkerWindow(text, size, tail);
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

static auto makeNextFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->nextChunk();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makePrevFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->prevChunk();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeInvertFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->invertMode();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeRecopyFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->recopyChunk();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeIncreaseLimitFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->increaseLimit();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeDecreaseLimitFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) {
            g_window->decreaseLimit();
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    };
}

static auto makeLoadNewTextFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (!g_window) return Value::makeBool(false);
        
        g_window->loadNewText();
        return Value::makeBool(true);
    };
}

static auto makeIsVisibleFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (g_window) return Value::makeBool(g_window->isVisible());
        return Value::makeBool(false);
    };
}

void registerTextChunkerGuiModule(const VMApi& api) {
    HAVEL_BEGIN_MODULE("TextChunkerGui");

    HAVEL_REGISTER_FUNCTION(api, "text_chunker_gui.create", makeCreateFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "text_chunker_gui.show", makeShowFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "text_chunker_gui.hide", makeHideFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "text_chunker_gui.toggle", makeToggleFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "text_chunker_gui.next", makeNextFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "text_chunker_gui.prev", makePrevFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "text_chunker_gui.invert", makeInvertFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "text_chunker_gui.recopy", makeRecopyFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "text_chunker_gui.increaseLimit", makeIncreaseLimitFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "text_chunker_gui.decreaseLimit", makeDecreaseLimitFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "text_chunker_gui.loadNewText", makeLoadNewTextFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "text_chunker_gui.isVisible", makeIsVisibleFunc(api));

    auto obj = api.makeObject();
    api.setGlobal("text_chunker_gui", obj);
    api.setField(obj, "__text_chunker_gui_module", Value::makeBool(true));
    api.setField(obj, "create", api.makeFunctionRef("text_chunker_gui.create"));
    api.setField(obj, "show", api.makeFunctionRef("text_chunker_gui.show"));
    api.setField(obj, "hide", api.makeFunctionRef("text_chunker_gui.hide"));
    api.setField(obj, "toggle", api.makeFunctionRef("text_chunker_gui.toggle"));
    api.setField(obj, "next", api.makeFunctionRef("text_chunker_gui.next"));
    api.setField(obj, "prev", api.makeFunctionRef("text_chunker_gui.prev"));
    api.setField(obj, "invert", api.makeFunctionRef("text_chunker_gui.invert"));
    api.setField(obj, "recopy", api.makeFunctionRef("text_chunker_gui.recopy"));
    api.setField(obj, "increaseLimit", api.makeFunctionRef("text_chunker_gui.increaseLimit"));
    api.setField(obj, "decreaseLimit", api.makeFunctionRef("text_chunker_gui.decreaseLimit"));
    api.setField(obj, "loadNewText", api.makeFunctionRef("text_chunker_gui.loadNewText"));
    api.setField(obj, "isVisible", api.makeFunctionRef("text_chunker_gui.isVisible"));

    HAVEL_END_MODULE();
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(text_chunker_gui, "1.0.0", "Text chunker GUI module",
    havel::modules::registerTextChunkerGuiModule(*api);
)
#endif
