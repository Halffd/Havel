#include "MonitoringClipboardModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/clipboard/MonitoringClipboard.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::MonitoringClipboard;

static const char* MONITOR_MODULE_MARKER = "__monitoring_clipboard_module";

static bool isMonitorModuleObject(const VMApi& api, const Value& val) {
    if (!val.isObjectId()) return false;
    auto marker = api.getField(val, MONITOR_MODULE_MARKER);
    return marker.isBool() && marker.asBool();
}

static std::vector<Value> stripMonitorReceiver(const VMApi& api, const std::vector<Value>& args) {
    if (!args.empty() && isMonitorModuleObject(api, args[0])) {
        return std::vector<Value>(args.begin() + 1, args.end());
    }
    return args;
}

static std::shared_ptr<MonitoringClipboard> getMonitorService() {
    auto svc = host::ServiceRegistry::instance().get<MonitoringClipboard>();
    if (!svc) debug("MonitoringClipboardModule: MonitoringClipboard not available");
    return svc;
}

static int monitorToInt(const Value& v, int def = 0) {
    if (v.isInt()) return static_cast<int>(v.asInt());
    if (v.isDouble()) return static_cast<int>(v.asDouble());
    return def;
}

void registerMonitoringClipboardModule(const VMApi& api) {
    HAVEL_BEGIN_MODULE("MonitoringClipboard");

    HAVEL_REGISTER_FUNCTION(api, "clipboard.startMonitoring", [api](const auto& rawArgs) {
        auto args = stripMonitorReceiver(api, rawArgs);
        auto svc = getMonitorService();
        if (!svc) return Value::makeBool(false);
        try { svc->startMonitoring(); return Value::makeBool(true); }
        catch (const std::exception& e) { debug("clipboard.startMonitoring error: {}", e.what()); return Value::makeBool(false); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.stopMonitoring", [api](const auto& rawArgs) {
        auto args = stripMonitorReceiver(api, rawArgs);
        auto svc = getMonitorService();
        if (!svc) return Value::makeBool(false);
        try { svc->stopMonitoring(); return Value::makeBool(true); }
        catch (const std::exception& e) { debug("clipboard.stopMonitoring error: {}", e.what()); return Value::makeBool(false); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.isMonitoring", [api](const auto& rawArgs) {
        auto args = stripMonitorReceiver(api, rawArgs);
        auto svc = getMonitorService();
        if (!svc) return Value::makeBool(false);
        try { return Value::makeBool(svc->isMonitoring()); }
        catch (const std::exception& e) { debug("clipboard.isMonitoring error: {}", e.what()); return Value::makeBool(false); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.setMonitorInterval", [api](const auto& rawArgs) {
        auto args = stripMonitorReceiver(api, rawArgs);
        if (args.empty()) return Value::makeBool(false);
        auto svc = getMonitorService();
        if (!svc) return Value::makeBool(false);
        try { svc->setMonitorInterval(monitorToInt(args[0], 500)); return Value::makeBool(true); }
        catch (const std::exception& e) { debug("clipboard.setMonitorInterval error: {}", e.what()); return Value::makeBool(false); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.getMonitorInterval", [api](const auto& rawArgs) {
        auto args = stripMonitorReceiver(api, rawArgs);
        auto svc = getMonitorService();
        if (!svc) return Value::makeInt(0);
        try { return Value::makeInt(static_cast<int64_t>(svc->getMonitorInterval())); }
        catch (const std::exception& e) { debug("clipboard.getMonitorInterval error: {}", e.what()); return Value::makeInt(0); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.onClipboardChanged", [api](const auto& rawArgs) {
        auto args = stripMonitorReceiver(api, rawArgs);
        auto svc = getMonitorService();
        if (!svc) return Value::makeBool(false);
        try {
            svc->onClipboardChanged([](const std::string& content) {
                debug("clipboard changed: {:.80}{}", content.size() > 80 ? "..." : "", content.size() > 80 ? "" : "");
                (void)content;
            });
            return Value::makeBool(true);
        } catch (const std::exception& e) { debug("clipboard.onClipboardChanged error: {}", e.what()); return Value::makeBool(false); }
    });

    HAVEL_REGISTER_FUNCTION(api, "clipboard.getLastChangeTime", [api](const auto& rawArgs) {
        auto args = stripMonitorReceiver(api, rawArgs);
        auto svc = getMonitorService();
        if (!svc) return Value::makeInt(0);
        try {
            auto tp = svc->getLastChangeTime();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                tp.time_since_epoch()).count();
            return Value::makeInt(static_cast<int64_t>(ms));
        } catch (const std::exception& e) { debug("clipboard.getLastChangeTime error: {}", e.what()); return Value::makeInt(0); }
    });

    auto obj = api.makeObject();
    api.setGlobal("clipboardMonitor", obj);
    api.setField(obj, MONITOR_MODULE_MARKER, Value::makeBool(true));
    api.setField(obj, "start", api.makeFunctionRef("clipboard.startMonitoring"));
    api.setField(obj, "stop", api.makeFunctionRef("clipboard.stopMonitoring"));
    api.setField(obj, "isRunning", api.makeFunctionRef("clipboard.isMonitoring"));
    api.setField(obj, "setInterval", api.makeFunctionRef("clipboard.setMonitorInterval"));
    api.setField(obj, "getInterval", api.makeFunctionRef("clipboard.getMonitorInterval"));
    api.setField(obj, "onChange", api.makeFunctionRef("clipboard.onClipboardChanged"));
    api.setField(obj, "getLastChange", api.makeFunctionRef("clipboard.getLastChangeTime"));

    HAVEL_END_MODULE();
}

} // namespace havel::modules
