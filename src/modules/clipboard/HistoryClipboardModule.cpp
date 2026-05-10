#include "HistoryClipboardModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/clipboard/ClipboardService.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::ClipboardService;

static const char* HISTORY_MODULE_MARKER = "__history_clipboard_module";

static bool isHistoryModuleObject(const VMApi& api, const Value& val) {
    if (!val.isObjectId()) return false;
    auto marker = api.getField(val, HISTORY_MODULE_MARKER);
    return marker.isBool() && marker.asBool();
}

static std::vector<Value> stripHistoryReceiver(const VMApi& api, const std::vector<Value>& args) {
    if (!args.empty() && isHistoryModuleObject(api, args[0])) {
        return std::vector<Value>(args.begin() + 1, args.end());
    }
    return args;
}

static std::shared_ptr<ClipboardService> getHistoryService() {
    auto svc = host::ServiceRegistry::instance().get<ClipboardService>();
    if (!svc) debug("HistoryClipboardModule: ClipboardService not available");
    return svc;
}

static std::string historyToString(const VMApi& api, const Value& v) {
    if (v.isStringId() || v.isStringValId()) return api.toString(v);
    if (v.isNull()) return "";
    if (v.isInt()) return std::to_string(v.asInt());
    if (v.isDouble()) return std::to_string(v.asDouble());
    if (v.isBool()) return v.asBool() ? "true" : "false";
    return "";
}

static int historyToInt(const Value& v, int def = 0) {
    if (v.isInt()) return static_cast<int>(v.asInt());
    if (v.isDouble()) return static_cast<int>(v.asDouble());
    return def;
}

void registerHistoryClipboardModule(const VMApi& api) {
    HAVEL_BEGIN_MODULE("HistoryClipboard");

    HAVEL_REGISTER_FUNCTION(api, "clipboard.addToHistory", [api](const auto& rawArgs) {
        auto args = stripHistoryReceiver(api, rawArgs);
        if (args.empty()) return Value::makeBool(false);
        auto svc = getHistoryService();
        if (!svc) return Value::makeBool(false);
        try {
            svc->addToHistory(historyToString(api, args[0]));
            return Value::makeBool(true);
        } catch (const std::exception& e) { debug("clipboard.addToHistory error: {}", e.what()); return Value::makeBool(false); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.getHistoryItem", [api](const auto& rawArgs) {
        auto args = stripHistoryReceiver(api, rawArgs);
        auto svc = getHistoryService();
        if (!svc) return api.makeString("");
        int index = !args.empty() ? historyToInt(args[0]) : 0;
        try { return api.makeString(svc->getHistoryItem(index)); }
        catch (const std::exception& e) { debug("clipboard.getHistoryItem error: {}", e.what()); return api.makeString(""); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.getHistoryCount", [api](const auto& rawArgs) {
        auto args = stripHistoryReceiver(api, rawArgs);
        auto svc = getHistoryService();
        if (!svc) return Value::makeInt(0);
        try { return Value::makeInt(static_cast<int64_t>(svc->getHistoryCount())); }
        catch (const std::exception& e) { debug("clipboard.getHistoryCount error: {}", e.what()); return Value::makeInt(0); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.clearHistory", [api](const auto& rawArgs) {
        auto args = stripHistoryReceiver(api, rawArgs);
        auto svc = getHistoryService();
        if (!svc) return Value::makeBool(false);
        try { svc->clearHistory(); return Value::makeBool(true); }
        catch (const std::exception& e) { debug("clipboard.clearHistory error: {}", e.what()); return Value::makeBool(false); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.getHistory", [api](const auto& rawArgs) {
        auto args = stripHistoryReceiver(api, rawArgs);
        auto svc = getHistoryService();
        if (!svc) return api.makeArray();
        try {
            auto& history = svc->getHistory();
            auto arr = api.makeArray();
            for (const auto& item : history) api.push(arr, api.makeString(item));
            return arr;
        } catch (const std::exception& e) { debug("clipboard.getHistory error: {}", e.what()); return api.makeArray(); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.getLast", [api](const auto& rawArgs) {
        auto args = stripHistoryReceiver(api, rawArgs);
        auto svc = getHistoryService();
        if (!svc) return api.makeString("");
        try { return api.makeString(svc->getLast()); }
        catch (const std::exception& e) { debug("clipboard.getLast error: {}", e.what()); return api.makeString(""); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.getRecent", [api](const auto& rawArgs) {
        auto args = stripHistoryReceiver(api, rawArgs);
        auto svc = getHistoryService();
        if (!svc) return api.makeArray();
        int count = !args.empty() ? historyToInt(args[0], 10) : 10;
        try {
            auto recent = svc->getRecent(count);
            auto arr = api.makeArray();
            for (const auto& item : recent) api.push(arr, api.makeString(item));
            return arr;
        } catch (const std::exception& e) { debug("clipboard.getRecent error: {}", e.what()); return api.makeArray(); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.getHistoryRange", [api](const auto& rawArgs) {
        auto args = stripHistoryReceiver(api, rawArgs);
        auto svc = getHistoryService();
        if (!svc) return api.makeArray();
        int start = args.size() > 0 ? historyToInt(args[0]) : 0;
        int end = args.size() > 1 ? historyToInt(args[1]) : 10;
        try {
            auto range = svc->getHistoryRange(start, end);
            auto arr = api.makeArray();
            for (const auto& item : range) api.push(arr, api.makeString(item));
            return arr;
        } catch (const std::exception& e) { debug("clipboard.getHistoryRange error: {}", e.what()); return api.makeArray(); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.filter", [api](const auto& rawArgs) {
        auto args = stripHistoryReceiver(api, rawArgs);
        if (args.empty()) return api.makeArray();
        auto svc = getHistoryService();
        if (!svc) return api.makeArray();
        try {
            auto filtered = svc->filter(historyToString(api, args[0]));
            auto arr = api.makeArray();
            for (const auto& item : filtered) api.push(arr, api.makeString(item));
            return arr;
        } catch (const std::exception& e) { debug("clipboard.filter error: {}", e.what()); return api.makeArray(); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.find", [api](const auto& rawArgs) {
        auto args = stripHistoryReceiver(api, rawArgs);
        if (args.empty()) return api.makeArray();
        auto svc = getHistoryService();
        if (!svc) return api.makeArray();
        try {
            auto found = svc->find(historyToString(api, args[0]));
            auto arr = api.makeArray();
            for (const auto& item : found) api.push(arr, api.makeString(item));
            return arr;
        } catch (const std::exception& e) { debug("clipboard.find error: {}", e.what()); return api.makeArray(); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.setMaxHistorySize", [api](const auto& rawArgs) {
        auto args = stripHistoryReceiver(api, rawArgs);
        if (args.empty()) return Value::makeBool(false);
        auto svc = getHistoryService();
        if (!svc) return Value::makeBool(false);
        try { svc->setMaxHistorySize(historyToInt(args[0], 100)); return Value::makeBool(true); }
        catch (const std::exception& e) { debug("clipboard.setMaxHistorySize error: {}", e.what()); return Value::makeBool(false); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.getMaxHistorySize", [api](const auto& rawArgs) {
        auto args = stripHistoryReceiver(api, rawArgs);
        auto svc = getHistoryService();
        if (!svc) return Value::makeInt(0);
        try { return Value::makeInt(static_cast<int64_t>(svc->getMaxHistorySize())); }
        catch (const std::exception& e) { debug("clipboard.getMaxHistorySize error: {}", e.what()); return Value::makeInt(0); }
    });

    auto obj = api.makeObject();
    api.setGlobal("clipboardHistory", obj);
    api.setField(obj, HISTORY_MODULE_MARKER, Value::makeBool(true));
    api.setField(obj, "add", api.makeFunctionRef("clipboard.addToHistory"));
    api.setField(obj, "getItem", api.makeFunctionRef("clipboard.getHistoryItem"));
    api.setField(obj, "count", api.makeFunctionRef("clipboard.getHistoryCount"));
    api.setField(obj, "clear", api.makeFunctionRef("clipboard.clearHistory"));
    api.setField(obj, "getAll", api.makeFunctionRef("clipboard.getHistory"));
    api.setField(obj, "getLast", api.makeFunctionRef("clipboard.getLast"));
    api.setField(obj, "getRecent", api.makeFunctionRef("clipboard.getRecent"));
    api.setField(obj, "getRange", api.makeFunctionRef("clipboard.getHistoryRange"));
    api.setField(obj, "filter", api.makeFunctionRef("clipboard.filter"));
    api.setField(obj, "find", api.makeFunctionRef("clipboard.find"));
    api.setField(obj, "setMaxSize", api.makeFunctionRef("clipboard.setMaxHistorySize"));
    api.setField(obj, "getMaxSize", api.makeFunctionRef("clipboard.getMaxHistorySize"));

    HAVEL_END_MODULE();
}

} // namespace havel::modules
