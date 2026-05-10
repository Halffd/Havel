#include "ScreenshotModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/screenshot/ScreenshotService.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::ScreenshotService;

static const char* MODULE_MARKER = "__screenshot_module";

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

static ScreenshotService* getService() {
 auto svc = host::ServiceRegistry::instance().get<ScreenshotService>();
 if (!svc) debug("ScreenshotModule: ScreenshotService not available");
 return svc.get();
}

static int toInt(const Value& v, int def = 0) {
 if (v.isInt()) return static_cast<int>(v.asInt());
 if (v.isDouble()) return static_cast<int>(v.asDouble());
 return def;
}

static Value rgbaToVmArray(const VMApi& api, const std::vector<uint8_t>& rgba, int width, int height) {
 auto result = api.makeObject();
 api.setField(result, "width", Value::makeInt(width));
 api.setField(result, "height", Value::makeInt(height));

 auto pixels = api.makeArray();
 size_t pixelCount = rgba.size() / 4;
 for (size_t i = 0; i < pixelCount; i++) {
 auto pixel = api.makeObject();
 api.setField(pixel, "r", Value::makeInt(rgba[i * 4]));
 api.setField(pixel, "g", Value::makeInt(rgba[i * 4 + 1]));
 api.setField(pixel, "b", Value::makeInt(rgba[i * 4 + 2]));
 api.setField(pixel, "a", Value::makeInt(rgba[i * 4 + 3]));
 api.push(pixels, pixel);
 }
 api.setField(result, "pixels", pixels);
 api.setField(result, "format", api.makeString("rgba"));

 return result;
}

void registerScreenshotModule(const VMApi& api) {
 HAVEL_BEGIN_MODULE("Screenshot");

 HAVEL_REGISTER_FUNCTION(api, "screenshot.capture", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return api.makeNull();
 try {
 auto rgba = svc->captureFullDesktop();
 int pixels = static_cast<int>(rgba.size() / 4);
 int dim = static_cast<int>(std::sqrt(pixels));
 return rgbaToVmArray(api, rgba, dim, dim);
 } catch (const std::exception& e) { debug("screenshot.capture error: {}", e.what()); return api.makeNull(); }
 });

 HAVEL_REGISTER_FUNCTION(api, "screenshot.captureMonitor", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return api.makeNull();
 if (args.empty()) return api.makeNull();
 try {
 int monitorIndex = toInt(args[0]);
 auto rgba = svc->captureMonitor(monitorIndex);
 auto geometry = svc->getMonitorGeometry(monitorIndex);
 int width = geometry.size() >= 3 ? geometry[2] : 1920;
 int height = geometry.size() >= 4 ? geometry[3] : 1080;
 return rgbaToVmArray(api, rgba, width, height);
 } catch (const std::exception& e) { debug("screenshot.captureMonitor error: {}", e.what()); return api.makeNull(); }
 });

 HAVEL_REGISTER_FUNCTION(api, "screenshot.captureActiveWindow", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return api.makeNull();
 try {
 auto rgba = svc->captureActiveWindow();
 int pixels = static_cast<int>(rgba.size() / 4);
 int dim = static_cast<int>(std::sqrt(pixels));
 return rgbaToVmArray(api, rgba, dim, dim);
 } catch (const std::exception& e) { debug("screenshot.captureActiveWindow error: {}", e.what()); return api.makeNull(); }
 });

 HAVEL_REGISTER_FUNCTION(api, "screenshot.captureRegion", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return api.makeNull();
 if (args.size() < 4) return api.makeNull();
 try {
 int x = toInt(args[0]);
 int y = toInt(args[1]);
 int width = toInt(args[2]);
 int height = toInt(args[3]);
 auto rgba = svc->captureRegion(x, y, width, height);
 return rgbaToVmArray(api, rgba, width, height);
 } catch (const std::exception& e) { debug("screenshot.captureRegion error: {}", e.what()); return api.makeNull(); }
 });

 HAVEL_REGISTER_FUNCTION(api, "screenshot.monitorCount", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return Value::makeInt(0);
 try { return Value::makeInt(svc->getMonitorCount()); }
 catch (const std::exception& e) { debug("screenshot.monitorCount error: {}", e.what()); return Value::makeInt(0); }
 });

 HAVEL_REGISTER_FUNCTION(api, "screenshot.monitorGeometry", [api](const auto& rawArgs) {
 auto args = stripReceiver(api, rawArgs);
 auto svc = getService();
 if (!svc) return api.makeNull();
 if (args.empty()) return api.makeNull();
 try {
 int monitorIndex = toInt(args[0]);
 auto geometry = svc->getMonitorGeometry(monitorIndex);
 auto result = api.makeObject();
 if (geometry.size() >= 4) {
 api.setField(result, "x", Value::makeInt(geometry[0]));
 api.setField(result, "y", Value::makeInt(geometry[1]));
 api.setField(result, "width", Value::makeInt(geometry[2]));
 api.setField(result, "height", Value::makeInt(geometry[3]));
 }
 return result;
 } catch (const std::exception& e) { debug("screenshot.monitorGeometry error: {}", e.what()); return api.makeNull(); }
 });

 auto obj = api.makeObject();
 api.setGlobal("screenshot", obj);
 api.setField(obj, MODULE_MARKER, Value::makeBool(true));
 api.setField(obj, "capture", api.makeFunctionRef("screenshot.capture"));
 api.setField(obj, "captureMonitor", api.makeFunctionRef("screenshot.captureMonitor"));
 api.setField(obj, "captureActiveWindow", api.makeFunctionRef("screenshot.captureActiveWindow"));
 api.setField(obj, "captureRegion", api.makeFunctionRef("screenshot.captureRegion"));
 api.setField(obj, "monitorCount", api.makeFunctionRef("screenshot.monitorCount"));
 api.setField(obj, "monitorGeometry", api.makeFunctionRef("screenshot.monitorGeometry"));

 HAVEL_END_MODULE();
}

} // namespace havel::modules
