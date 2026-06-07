#include "WaylandModule.hpp"
#include "core/wayland/WaylandProtocolClient.hpp"
#include "core/wayland/VirtualKeyboard.hpp"
#include "core/wayland/VirtualPointer.hpp"
#include "core/wayland/WaylandClipboardBackend.hpp"
#include "core/wayland/ForeignToplevel.hpp"
#include "utils/Logger.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

static std::string strArg(const Value &v) {
    if (v.isStringId() || v.isStringValId()) return v.toString();
    if (v.isInt()) return std::to_string(v.asInt());
    return "";
}

static int64_t intArg(const Value &v, int64_t def = 0) {
    if (v.isInt()) return v.asInt();
    if (v.isDouble()) return static_cast<int64_t>(v.asDouble());
    return def;
}

static double numArg(const Value &v, double def = 0.0) {
    if (v.isDouble()) return v.asDouble();
    if (v.isInt()) return static_cast<double>(v.asInt());
    return def;
}

void registerWaylandModule(const VMApi &api) {
    auto &client = havel::WaylandProtocolClient::instance();

    api.registerFunction("wayland.connect", [&client](const std::vector<Value> &) {
        return Value::makeBool(client.connect());
    });

    api.registerFunction("wayland.connected", [&client](const std::vector<Value> &) {
        return Value::makeBool(client.isConnected());
    });

    api.registerFunction("wayland.compositor", [&client](const std::vector<Value> &) {
        return api.makeString(client.compositorName());
    });

    api.registerFunction("wayland.disconnect", [&client](const std::vector<Value> &) {
        client.disconnect();
        return Value::makeNull();
    });

    // --- info sub-object ---
    auto infoObj = api.makeObject();

    api.registerFunction("wayland.info.protocols", [&client](const std::vector<Value> &) {
        auto arr = api.makeArray();
        for (const auto &proto : client.protocols()) {
            auto obj = api.makeObject();
            api.setField(obj, "name", api.makeString(proto.name));
            api.setField(obj, "version", api.makeNumber(proto.version));
            api.push(arr, obj);
        }
        return arr;
    });

    api.registerFunction("wayland.info.outputs", [&client](const std::vector<Value> &) {
        auto arr = api.makeArray();
        for (const auto &out : client.outputs()) {
            auto obj = api.makeObject();
            api.setField(obj, "name", api.makeString(out.name));
            api.setField(obj, "width", api.makeNumber(out.width));
            api.setField(obj, "height", api.makeNumber(out.height));
            api.setField(obj, "scale", api.makeNumber(out.scale));
            api.setField(obj, "x", api.makeNumber(out.x));
            api.setField(obj, "y", api.makeNumber(out.y));
            api.push(arr, obj);
        }
        return arr;
    });

    api.registerFunction("wayland.info.has_keyboard", [&client](const std::vector<Value> &) {
        return Value::makeBool(client.hasVirtualKeyboard());
    });

    api.registerFunction("wayland.info.has_pointer", [&client](const std::vector<Value> &) {
        return Value::makeBool(client.hasVirtualPointer());
    });

    api.registerFunction("wayland.info.has_toplevel", [&client](const std::vector<Value> &) {
        return Value::makeBool(client.hasForeignToplevel());
    });

    api.registerFunction("wayland.info.has_data_control", [&client](const std::vector<Value> &) {
        return Value::makeBool(client.hasDataControl());
    });

    api.setField(infoObj, "protocols", api.makeFunctionRef("wayland.info.protocols"));
    api.setField(infoObj, "outputs", api.makeFunctionRef("wayland.info.outputs"));
    api.setField(infoObj, "has_keyboard", api.makeFunctionRef("wayland.info.has_keyboard"));
    api.setField(infoObj, "has_pointer", api.makeFunctionRef("wayland.info.has_pointer"));
    api.setField(infoObj, "has_toplevel", api.makeFunctionRef("wayland.info.has_toplevel"));
    api.setField(infoObj, "has_data_control", api.makeFunctionRef("wayland.info.has_data_control"));

    // --- keyboard sub-object ---
    static std::unique_ptr<havel::VirtualKeyboard> vkb;
    static bool kbInit = false;

    api.registerFunction("wayland.keyboard.init", [&client](const std::vector<Value> &) {
        if (kbInit && vkb) return Value::makeBool(true);
        vkb = std::make_unique<havel::VirtualKeyboard>(client);
        kbInit = vkb->initialize();
        return Value::makeBool(kbInit);
    });

    api.registerFunction("wayland.keyboard.press", [](const std::vector<Value> &args) {
        if (!kbInit || !vkb) return Value::makeBool(false);
        uint32_t kc = static_cast<uint32_t>(intArg(args.empty() ? Value::makeNull() : args[0]));
        vkb->pressKey(kc);
        return Value::makeBool(true);
    });

    api.registerFunction("wayland.keyboard.release", [](const std::vector<Value> &args) {
        if (!kbInit || !vkb) return Value::makeBool(false);
        uint32_t kc = static_cast<uint32_t>(intArg(args.empty() ? Value::makeNull() : args[0]));
        vkb->releaseKey(kc);
        return Value::makeBool(true);
    });

    api.registerFunction("wayland.keyboard.tap", [](const std::vector<Value> &args) {
        if (!kbInit || !vkb) return Value::makeBool(false);
        uint32_t kc = static_cast<uint32_t>(intArg(args.empty() ? Value::makeNull() : args[0]));
        uint32_t delay = args.size() > 1 ? static_cast<uint32_t>(intArg(args[1], 50)) : 50;
        vkb->tapKey(kc, delay);
        return Value::makeBool(true);
    });

    api.registerFunction("wayland.keyboard.type", [](const std::vector<Value> &args) {
        if (!kbInit || !vkb || args.empty()) return Value::makeBool(false);
        vkb->typeText(strArg(args[0]));
        return Value::makeBool(true);
    });

    api.registerFunction("wayland.keyboard.press_name", [](const std::vector<Value> &args) {
        if (!kbInit || !vkb || args.empty()) return Value::makeBool(false);
        vkb->pressKeyByName(strArg(args[0]));
        return Value::makeBool(true);
    });

    api.registerFunction("wayland.keyboard.release_name", [](const std::vector<Value> &args) {
        if (!kbInit || !vkb || args.empty()) return Value::makeBool(false);
        vkb->releaseKeyByName(strArg(args[0]));
        return Value::makeBool(true);
    });

    api.registerFunction("wayland.keyboard.tap_name", [](const std::vector<Value> &args) {
        if (!kbInit || !vkb || args.empty()) return Value::makeBool(false);
        vkb->tapKeyByName(strArg(args[0]));
        return Value::makeBool(true);
    });

    api.registerFunction("wayland.keyboard.keycode", [](const std::vector<Value> &args) {
        if (!kbInit || !vkb || args.empty()) return Value::makeInt(0);
        return Value::makeInt(static_cast<int64_t>(vkb->keyNameToKeycode(strArg(args[0]))));
    });

    auto kbObj = api.makeObject();
    api.setField(kbObj, "init", api.makeFunctionRef("wayland.keyboard.init"));
    api.setField(kbObj, "press", api.makeFunctionRef("wayland.keyboard.press"));
    api.setField(kbObj, "release", api.makeFunctionRef("wayland.keyboard.release"));
    api.setField(kbObj, "tap", api.makeFunctionRef("wayland.keyboard.tap"));
    api.setField(kbObj, "type", api.makeFunctionRef("wayland.keyboard.type"));
    api.setField(kbObj, "press_name", api.makeFunctionRef("wayland.keyboard.press_name"));
    api.setField(kbObj, "release_name", api.makeFunctionRef("wayland.keyboard.release_name"));
    api.setField(kbObj, "tap_name", api.makeFunctionRef("wayland.keyboard.tap_name"));
    api.setField(kbObj, "keycode", api.makeFunctionRef("wayland.keyboard.keycode"));

    // --- mouse sub-object ---
    static std::unique_ptr<havel::VirtualPointer> vptr;
    static bool ptrInit = false;

    api.registerFunction("wayland.mouse.init", [&client](const std::vector<Value> &) {
        if (ptrInit && vptr) return Value::makeBool(true);
        vptr = std::make_unique<havel::VirtualPointer>(client);
        ptrInit = vptr->initialize();
        return Value::makeBool(ptrInit);
    });

    api.registerFunction("wayland.mouse.move", [](const std::vector<Value> &args) {
        if (!ptrInit || !vptr || args.size() < 2) return Value::makeBool(false);
        return Value::makeBool(vptr->moveRelative(numArg(args[0]), numArg(args[1])));
    });

    api.registerFunction("wayland.mouse.move_to", [](const std::vector<Value> &args) {
        if (!ptrInit || !vptr || args.size() < 2) return Value::makeBool(false);
        auto &outputs = havel::WaylandProtocolClient::instance().outputs();
        if (outputs.empty()) return Value::makeBool(false);
        auto &out = outputs[0];
        return Value::makeBool(vptr->moveAbsolute(
            static_cast<uint32_t>(intArg(args[0])),
            static_cast<uint32_t>(intArg(args[1])),
            static_cast<uint32_t>(out.width),
            static_cast<uint32_t>(out.height)));
    });

    api.registerFunction("wayland.mouse.click", [](const std::vector<Value> &args) {
        if (!ptrInit || !vptr || args.empty()) return Value::makeBool(false);
        uint32_t btn = static_cast<uint32_t>(intArg(args[0]));
        return Value::makeBool(vptr->tapButton(btn));
    });

    api.registerFunction("wayland.mouse.button_down", [](const std::vector<Value> &args) {
        if (!ptrInit || !vptr || args.empty()) return Value::makeBool(false);
        uint32_t btn = static_cast<uint32_t>(intArg(args[0]));
        return Value::makeBool(vptr->clickButton(btn, true));
    });

    api.registerFunction("wayland.mouse.button_up", [](const std::vector<Value> &args) {
        if (!ptrInit || !vptr || args.empty()) return Value::makeBool(false);
        uint32_t btn = static_cast<uint32_t>(intArg(args[0]));
        return Value::makeBool(vptr->clickButton(btn, false));
    });

    api.registerFunction("wayland.mouse.scroll", [](const std::vector<Value> &args) {
        if (!ptrInit || !vptr) return Value::makeBool(false);
        double dy = args.size() > 0 ? numArg(args[0]) : 0.0;
        double dx = args.size() > 1 ? numArg(args[1]) : 0.0;
        return Value::makeBool(vptr->scrollSmooth(dy, dx));
    });

    api.registerFunction("wayland.mouse.scroll_discrete", [](const std::vector<Value> &args) {
        if (!ptrInit || !vptr) return Value::makeBool(false);
        int32_t dy = static_cast<int32_t>(intArg(args.empty() ? Value::makeNull() : args[0]));
        int32_t dx = args.size() > 1 ? static_cast<int32_t>(intArg(args[1])) : 0;
        return Value::makeBool(vptr->scrollDiscrete(dy, dx));
    });

    api.registerFunction("wayland.mouse.button_name_to_code", [](const std::vector<Value> &args) {
        if (args.empty()) return Value::makeInt(0);
        return Value::makeInt(static_cast<int64_t>(
            havel::VirtualPointer::buttonNameToLinux(strArg(args[0]))));
    });

    auto ptrObj = api.makeObject();
    api.setField(ptrObj, "init", api.makeFunctionRef("wayland.mouse.init"));
    api.setField(ptrObj, "move", api.makeFunctionRef("wayland.mouse.move"));
    api.setField(ptrObj, "move_to", api.makeFunctionRef("wayland.mouse.move_to"));
    api.setField(ptrObj, "click", api.makeFunctionRef("wayland.mouse.click"));
    api.setField(ptrObj, "button_down", api.makeFunctionRef("wayland.mouse.button_down"));
    api.setField(ptrObj, "button_up", api.makeFunctionRef("wayland.mouse.button_up"));
    api.setField(ptrObj, "scroll", api.makeFunctionRef("wayland.mouse.scroll"));
    api.setField(ptrObj, "scroll_discrete", api.makeFunctionRef("wayland.mouse.scroll_discrete"));
    api.setField(ptrObj, "button_name_to_code", api.makeFunctionRef("wayland.mouse.button_name_to_code"));

    // --- clipboard sub-object ---
    static std::unique_ptr<havel::WaylandClipboardBackend> clip;
    static bool clipInit = false;

    api.registerFunction("wayland.clipboard.init", [&client](const std::vector<Value> &) {
        if (clipInit && clip) return Value::makeBool(true);
        clip = std::make_unique<havel::WaylandClipboardBackend>(client);
        clipInit = clip->initialize();
        return Value::makeBool(clipInit);
    });

    api.registerFunction("wayland.clipboard.get_text", [](const std::vector<Value> &) {
        if (!clipInit || !clip) return api.makeString("");
        return api.makeString(clip->getText());
    });

    api.registerFunction("wayland.clipboard.set_text", [](const std::vector<Value> &args) {
        if (!clipInit || !clip || args.empty()) return Value::makeBool(false);
        return Value::makeBool(clip->setText(strArg(args[0])));
    });

    api.registerFunction("wayland.clipboard.clear", [](const std::vector<Value> &) {
        if (!clipInit || !clip) return Value::makeBool(false);
        return Value::makeBool(clip->clear());
    });

    api.registerFunction("wayland.clipboard.has_text", [](const std::vector<Value> &) {
        if (!clipInit || !clip) return Value::makeBool(false);
        return Value::makeBool(clip->hasText());
    });

    api.registerFunction("wayland.clipboard.get_primary", [](const std::vector<Value> &) {
        if (!clipInit || !clip) return api.makeString("");
        return api.makeString(clip->getPrimaryText());
    });

    api.registerFunction("wayland.clipboard.set_primary", [](const std::vector<Value> &args) {
        if (!clipInit || !clip || args.empty()) return Value::makeBool(false);
        return Value::makeBool(clip->setPrimaryText(strArg(args[0])));
    });

    auto clipObj = api.makeObject();
    api.setField(clipObj, "init", api.makeFunctionRef("wayland.clipboard.init"));
    api.setField(clipObj, "get_text", api.makeFunctionRef("wayland.clipboard.get_text"));
    api.setField(clipObj, "set_text", api.makeFunctionRef("wayland.clipboard.set_text"));
    api.setField(clipObj, "clear", api.makeFunctionRef("wayland.clipboard.clear"));
    api.setField(clipObj, "has_text", api.makeFunctionRef("wayland.clipboard.has_text"));
    api.setField(clipObj, "get_primary", api.makeFunctionRef("wayland.clipboard.get_primary"));
    api.setField(clipObj, "set_primary", api.makeFunctionRef("wayland.clipboard.set_primary"));

    // --- windows sub-object ---
    static std::unique_ptr<havel::ForeignToplevel> tl;
    static bool tlInit = false;

    api.registerFunction("wayland.windows.init", [&client](const std::vector<Value> &) {
        if (tlInit && tl) return Value::makeBool(true);
        tl = std::make_unique<havel::ForeignToplevel>(client);
        tlInit = tl->initialize();
        return Value::makeBool(tlInit);
    });

    api.registerFunction("wayland.windows.list", [](const std::vector<Value> &) {
        if (!tlInit || !tl) return api.makeArray();
        auto arr = api.makeArray();
        for (const auto &w : tl->windows()) {
            auto obj = api.makeObject();
            api.setField(obj, "id", api.makeNumber(w.id));
            api.setField(obj, "title", api.makeString(w.title));
            api.setField(obj, "app_id", api.makeString(w.appId));
            api.setField(obj, "active", Value::makeBool(w.active));
            api.setField(obj, "maximized", Value::makeBool(w.maximized));
            api.setField(obj, "minimized", Value::makeBool(w.minimized));
            api.setField(obj, "fullscreen", Value::makeBool(w.fullscreen));
            api.push(arr, obj);
        }
        return arr;
    });

    api.registerFunction("wayland.windows.active", [](const std::vector<Value> &) {
        if (!tlInit || !tl) return Value::makeNull();
        auto w = tl->activeWindow();
        if (!w.handle) return Value::makeNull();
        auto obj = api.makeObject();
        api.setField(obj, "id", api.makeNumber(w.id));
        api.setField(obj, "title", api.makeString(w.title));
        api.setField(obj, "app_id", api.makeString(w.appId));
        api.setField(obj, "active", Value::makeBool(w.active));
        return obj;
    });

    api.registerFunction("wayland.windows.focus", [](const std::vector<Value> &args) {
        if (!tlInit || !tl || args.empty()) return Value::makeBool(false);
        return Value::makeBool(tl->focusWindow(static_cast<uint32_t>(intArg(args[0]))));
    });

    api.registerFunction("wayland.windows.close", [](const std::vector<Value> &args) {
        if (!tlInit || !tl || args.empty()) return Value::makeBool(false);
        return Value::makeBool(tl->closeWindow(static_cast<uint32_t>(intArg(args[0]))));
    });

    api.registerFunction("wayland.windows.minimize", [](const std::vector<Value> &args) {
        if (!tlInit || !tl || args.empty()) return Value::makeBool(false);
        return Value::makeBool(tl->minimizeWindow(static_cast<uint32_t>(intArg(args[0]))));
    });

    api.registerFunction("wayland.windows.maximize", [](const std::vector<Value> &args) {
        if (!tlInit || !tl || args.empty()) return Value::makeBool(false);
        return Value::makeBool(tl->maximizeWindow(static_cast<uint32_t>(intArg(args[0]))));
    });

    api.registerFunction("wayland.windows.fullscreen", [](const std::vector<Value> &args) {
        if (!tlInit || !tl || args.empty()) return Value::makeBool(false);
        bool fs = args.size() > 1 ? (intArg(args[1], 1) != 0) : true;
        return Value::makeBool(tl->setFullscreen(static_cast<uint32_t>(intArg(args[0])), fs));
    });

    auto winObj = api.makeObject();
    api.setField(winObj, "init", api.makeFunctionRef("wayland.windows.init"));
    api.setField(winObj, "list", api.makeFunctionRef("wayland.windows.list"));
    api.setField(winObj, "active", api.makeFunctionRef("wayland.windows.active"));
    api.setField(winObj, "focus", api.makeFunctionRef("wayland.windows.focus"));
    api.setField(winObj, "close", api.makeFunctionRef("wayland.windows.close"));
    api.setField(winObj, "minimize", api.makeFunctionRef("wayland.windows.minimize"));
    api.setField(winObj, "maximize", api.makeFunctionRef("wayland.windows.maximize"));
    api.setField(winObj, "fullscreen", api.makeFunctionRef("wayland.windows.fullscreen"));

    // --- top-level wayland object ---
    auto waylandObj = api.makeObject();
    api.setField(waylandObj, "connect", api.makeFunctionRef("wayland.connect"));
    api.setField(waylandObj, "connected", api.makeFunctionRef("wayland.connected"));
    api.setField(waylandObj, "compositor", api.makeFunctionRef("wayland.compositor"));
    api.setField(waylandObj, "disconnect", api.makeFunctionRef("wayland.disconnect"));
    api.setField(waylandObj, "info", infoObj);
    api.setField(waylandObj, "keyboard", kbObj);
    api.setField(waylandObj, "mouse", ptrObj);
    api.setField(waylandObj, "clipboard", clipObj);
    api.setField(waylandObj, "windows", winObj);
    api.freeze(waylandObj);
    api.setGlobal("wayland", waylandObj);
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(wayland, "1.0.0", "Wayland native input protocols module",
    havel::stdlib::registerWaylandModule(*api);
)
#endif
