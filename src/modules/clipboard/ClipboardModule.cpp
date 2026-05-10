#include "ClipboardModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/clipboard/ClipboardService.hpp"
#include "host/clipboard/ClipboardInfo.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::ClipboardService;
using host::Clipboard;

static const char* MODULE_MARKER = "__clipboard_module";

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

static std::shared_ptr<ClipboardService> getService() {
 auto svc = host::ServiceRegistry::instance().get<ClipboardService>();
 if (!svc) debug("ClipboardModule: ClipboardService not available");
 return svc;
}

static std::string toString(const VMApi& api, const Value& v) {
 if (v.isStringId() || v.isStringValId()) return api.toString(v);
 if (v.isNull()) return "";
 if (v.isInt()) return std::to_string(v.asInt());
 if (v.isDouble()) return std::to_string(v.asDouble());
 if (v.isBool()) return v.asBool() ? "true" : "false";
 return "";
}

static int toInt(const Value& v, int def = 0) {
 if (v.isInt()) return static_cast<int>(v.asInt());
 if (v.isDouble()) return static_cast<int>(v.asDouble());
 return def;
}

static Clipboard::Method parseMethod(const std::string& s) {
 if (s == "qt") return Clipboard::Method::QT;
 if (s == "x11") return Clipboard::Method::X11;
 if (s == "wayland") return Clipboard::Method::WAYLAND;
 if (s == "external") return Clipboard::Method::EXTERNAL;
 if (s == "windows") return Clipboard::Method::WINDOWS;
 if (s == "macos") return Clipboard::Method::MACOS;
 return Clipboard::Method::AUTO;
}

static const char* methodToString(Clipboard::Method m) {
 switch (m) {
 case Clipboard::Method::QT: return "qt";
 case Clipboard::Method::X11: return "x11";
 case Clipboard::Method::WAYLAND: return "wayland";
 case Clipboard::Method::EXTERNAL: return "external";
 case Clipboard::Method::WINDOWS: return "windows";
 case Clipboard::Method::MACOS: return "macos";
 default: return "auto";
 }
}

static const char* infoTypeToString(host::ClipboardInfo::Type t) {
 switch (t) {
 case host::ClipboardInfo::Type::TEXT: return "text";
 case host::ClipboardInfo::Type::IMAGE: return "image";
 case host::ClipboardInfo::Type::FILES: return "files";
 default: return "empty";
 }
}

void registerClipboardModule(const VMApi& api) {
 HAVEL_BEGIN_MODULE("Clipboard");

 HAVEL_REGISTER_FUNCTION(api, "clipboard.get", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return api.makeString("");
 try { return api.makeString(svc->getText()); }
 catch (const std::exception& e) { debug("clipboard.get error: {}", e.what()); return api.makeString(""); }
 });

 HAVEL_REGISTER_FUNCTION(api, "clipboard.set", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 if (args.empty()) return Value::makeBool(false);
 auto svc = getService();
 if (!svc) return Value::makeBool(false);
 try { return Value::makeBool(svc->setText(toString(api, args[0]))); }
 catch (const std::exception& e) { debug("clipboard.set error: {}", e.what()); return Value::makeBool(false); }
 });

 HAVEL_REGISTER_FUNCTION(api, "clipboard.clear", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return Value::makeBool(false);
 try { return Value::makeBool(svc->clear()); }
 catch (const std::exception& e) { debug("clipboard.clear error: {}", e.what()); return Value::makeBool(false); }
 });

 HAVEL_REGISTER_FUNCTION(api, "clipboard.hasText", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return Value::makeBool(false);
 try { return Value::makeBool(svc->hasText()); }
 catch (const std::exception& e) { debug("clipboard.hasText error: {}", e.what()); return Value::makeBool(false); }
 });

 HAVEL_REGISTER_FUNCTION(api, "clipboard.setMethod", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return Value::makeBool(false);
 std::string methodStr = !args.empty() ? toString(api, args[0]) : "auto";
 try { svc->setMethod(parseMethod(methodStr)); return Value::makeBool(true); }
 catch (const std::exception& e) { debug("clipboard.setMethod error: {}", e.what()); return Value::makeBool(false); }
 });

 HAVEL_REGISTER_FUNCTION(api, "clipboard.getMethod", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return api.makeString("auto");
 try { return api.makeString(methodToString(svc->getMethod())); }
 catch (const std::exception& e) { debug("clipboard.getMethod error: {}", e.what()); return api.makeString("auto"); }
 });

 HAVEL_REGISTER_FUNCTION(api, "clipboard.detectMethod", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 try { return api.makeString(methodToString(Clipboard::detectBestMethod())); }
 catch (const std::exception& e) { debug("clipboard.detectMethod error: {}", e.what()); return api.makeString("auto"); }
 });

 HAVEL_REGISTER_FUNCTION(api, "clipboard.out", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return api.makeNull();
 try {
 auto info = svc->getInfo();
 auto obj = api.makeObject();
 api.setField(obj, "type", api.makeString(infoTypeToString(info.type)));
 api.setField(obj, "content", api.makeString(info.content));
 api.setField(obj, "size", Value::makeInt(static_cast<int64_t>(info.size)));
 api.setField(obj, "mimeType", api.makeString(info.mimeType));
 auto filesArr = api.makeArray();
 for (const auto& f : info.files) api.push(filesArr, api.makeString(f));
 api.setField(obj, "files", filesArr);
 api.setField(obj, "isText", Value::makeBool(info.isText()));
 api.setField(obj, "isImage", Value::makeBool(info.isImage()));
 api.setField(obj, "isFiles", Value::makeBool(info.isFiles()));
 api.setField(obj, "isEmpty", Value::makeBool(info.isEmpty()));
 return obj;
 } catch (const std::exception& e) { debug("clipboard.out error: {}", e.what()); return api.makeNull(); }
 });

 HAVEL_REGISTER_FUNCTION(api, "clipboard.hasImage", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return Value::makeBool(false);
 try { return Value::makeBool(svc->hasImage()); }
 catch (const std::exception& e) { debug("clipboard.hasImage error: {}", e.what()); return Value::makeBool(false); }
 });

 HAVEL_REGISTER_FUNCTION(api, "clipboard.getImage", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return api.makeString("");
 try { return api.makeString(svc->getImage()); }
 catch (const std::exception& e) { debug("clipboard.getImage error: {}", e.what()); return api.makeString(""); }
 });

 HAVEL_REGISTER_FUNCTION(api, "clipboard.setImage", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 if (args.empty()) return Value::makeBool(false);
 auto svc = getService();
 if (!svc) return Value::makeBool(false);
 try { return Value::makeBool(svc->setImage(toString(api, args[0]))); }
 catch (const std::exception& e) { debug("clipboard.setImage error: {}", e.what()); return Value::makeBool(false); }
 });

 HAVEL_REGISTER_FUNCTION(api, "clipboard.hasFiles", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return Value::makeBool(false);
 try { return Value::makeBool(svc->hasFiles()); }
 catch (const std::exception& e) { debug("clipboard.hasFiles error: {}", e.what()); return Value::makeBool(false); }
 });

 HAVEL_REGISTER_FUNCTION(api, "clipboard.getFiles", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return api.makeArray();
 try {
 auto files = svc->getFiles();
 auto arr = api.makeArray();
 for (const auto& f : files) api.push(arr, api.makeString(f));
 return arr;
 } catch (const std::exception& e) { debug("clipboard.getFiles error: {}", e.what()); return api.makeArray(); }
 });

 HAVEL_REGISTER_FUNCTION(api, "clipboard.setFiles", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 if (args.empty()) return Value::makeBool(false);
 auto svc = getService();
 if (!svc) return Value::makeBool(false);
 try {
 std::vector<std::string> paths;
 if (args[0].isObjectId()) {
 // Array of strings
                auto len = api.length(args[0]);
                for (uint32_t i = 0; i < len; i++) {
                    auto elem = api.getAt(args[0], i);
 paths.push_back(toString(api, elem));
 }
 } else {
 paths.push_back(toString(api, args[0]));
 }
 return Value::makeBool(svc->setFiles(paths));
 } catch (const std::exception& e) { debug("clipboard.setFiles error: {}", e.what()); return Value::makeBool(false); }
 });

 auto obj = api.makeObject();
 api.setGlobal("clipboard", obj);
 api.setField(obj, MODULE_MARKER, Value::makeBool(true));
 api.setField(obj, "get", api.makeFunctionRef("clipboard.get"));
 api.setField(obj, "set", api.makeFunctionRef("clipboard.set"));
 api.setField(obj, "clear", api.makeFunctionRef("clipboard.clear"));
 api.setField(obj, "hasText", api.makeFunctionRef("clipboard.hasText"));
 api.setField(obj, "setMethod", api.makeFunctionRef("clipboard.setMethod"));
 api.setField(obj, "getMethod", api.makeFunctionRef("clipboard.getMethod"));
 api.setField(obj, "detectMethod", api.makeFunctionRef("clipboard.detectMethod"));
 api.setField(obj, "out", api.makeFunctionRef("clipboard.out"));
 api.setField(obj, "hasImage", api.makeFunctionRef("clipboard.hasImage"));
 api.setField(obj, "getImage", api.makeFunctionRef("clipboard.getImage"));
 api.setField(obj, "setImage", api.makeFunctionRef("clipboard.setImage"));
 api.setField(obj, "hasFiles", api.makeFunctionRef("clipboard.hasFiles"));
 api.setField(obj, "getFiles", api.makeFunctionRef("clipboard.getFiles"));
 api.setField(obj, "setFiles", api.makeFunctionRef("clipboard.setFiles"));

 HAVEL_END_MODULE();
}

} // namespace havel::modules
