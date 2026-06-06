#include "ZoomService.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {
using compiler::Value;
using compiler::VMApi;
using zoom::ZoomService;
using zoom::ZoomFilter;

static Value zoomStart(const VMApi &, const std::vector<Value> &) {
	return Value(ZoomService::instance().start());
}

static Value zoomStop(const VMApi &, const std::vector<Value> &) {
	ZoomService::instance().stop();
	return Value::makeNull();
}

static Value zoomIsRunning(const VMApi &, const std::vector<Value> &) {
	return Value(ZoomService::instance().isRunning());
}

static Value zoomIn(const VMApi &, const std::vector<Value> &args) {
	float step = 0.2f;
	if (!args.empty()) step = static_cast<float>(args[0].asNumber());
	return Value(ZoomService::instance().zoomIn(step));
}

static Value zoomOut(const VMApi &, const std::vector<Value> &args) {
	float step = 0.2f;
	if (!args.empty()) step = static_cast<float>(args[0].asNumber());
	return Value(ZoomService::instance().zoomOut(step));
}

static Value zoomTo(const VMApi &, const std::vector<Value> &args) {
	if (args.empty()) return Value::makeNull();
	return Value(ZoomService::instance().zoomTo(static_cast<float>(args[0].asNumber())));
}

static Value zoomReset(const VMApi &, const std::vector<Value> &) {
	return Value(ZoomService::instance().zoomReset());
}

static Value zoomGetLevel(const VMApi &, const std::vector<Value> &) {
	return Value(static_cast<double>(ZoomService::instance().getZoomLevel()));
}

static Value zoomToggle(const VMApi &, const std::vector<Value> &args) {
	float level = ZoomService::instance().getZoomLevel();
	if (level <= 1.0f) {
		float target = 2.0f;
		if (!args.empty()) target = static_cast<float>(args[0].asNumber());
		return Value(ZoomService::instance().zoomTo(target));
	}
	return Value(ZoomService::instance().zoomReset());
}

static Value zoomSetScale(const VMApi &, const std::vector<Value> &args) {
	if (args.empty()) return Value::makeNull();
	ZoomService::instance().setScale(static_cast<float>(args[0].asNumber()));
	return Value::makeNull();
}

static Value zoomGetScale(const VMApi &, const std::vector<Value> &) {
	return Value(static_cast<double>(ZoomService::instance().getScale()));
}

static Value zoomSetRegion(const VMApi &, const std::vector<Value> &args) {
	if (args.size() < 4) return Value::makeNull();
	ZoomService::instance().setRegion(
		static_cast<int>(args[0].asNumber()),
		static_cast<int>(args[1].asNumber()),
		static_cast<int>(args[2].asNumber()),
		static_cast<int>(args[3].asNumber()));
	return Value::makeNull();
}

static Value zoomGetRegion(const VMApi &api, const std::vector<Value> &) {
	auto reg = ZoomService::instance().getRegion();
	auto obj = api.makeObject();
	api.setField(obj, "x", Value(reg.x));
	api.setField(obj, "y", Value(reg.y));
	api.setField(obj, "width", Value(reg.width));
	api.setField(obj, "height", Value(reg.height));
	return obj;
}

static Value zoomSetFilter(const VMApi &, const std::vector<Value> &args) {
	if (args.empty()) return Value::makeNull();
	ZoomService::instance().setFilter(static_cast<ZoomFilter>(static_cast<int>(args[0].asNumber())));
	return Value::makeNull();
}

static Value zoomGetFilter(const VMApi &, const std::vector<Value> &) {
	return Value(static_cast<int>(ZoomService::instance().getFilter()));
}

static Value zoomSetLocked(const VMApi &, const std::vector<Value> &args) {
	if (args.empty()) return Value::makeNull();
	ZoomService::instance().setLocked(args[0].asBool());
	return Value::makeNull();
}

static Value zoomIsLocked(const VMApi &, const std::vector<Value> &) {
	return Value(ZoomService::instance().isLocked());
}

static Value zoomSetFollowCursor(const VMApi &, const std::vector<Value> &args) {
	if (args.empty()) return Value::makeNull();
	ZoomService::instance().setFollowCursor(args[0].asBool());
	return Value::makeNull();
}

static Value zoomIsFollowCursor(const VMApi &, const std::vector<Value> &) {
	return Value(ZoomService::instance().isFollowCursor());
}

static Value zoomSetColorInvert(const VMApi &, const std::vector<Value> &args) {
	if (args.empty()) return Value::makeNull();
	ZoomService::instance().setColorInvert(args[0].asBool());
	return Value::makeNull();
}

static Value zoomIsColorInvert(const VMApi &, const std::vector<Value> &) {
	return Value(ZoomService::instance().isColorInvert());
}

static Value zoomSetBrightness(const VMApi &, const std::vector<Value> &args) {
	if (args.empty()) return Value::makeNull();
	ZoomService::instance().setBrightness(static_cast<float>(args[0].asNumber()));
	return Value::makeNull();
}

static Value zoomGetBrightness(const VMApi &, const std::vector<Value> &) {
	return Value(static_cast<double>(ZoomService::instance().getBrightness()));
}

static Value zoomSetContrast(const VMApi &, const std::vector<Value> &args) {
	if (args.empty()) return Value::makeNull();
	ZoomService::instance().setContrast(static_cast<float>(args[0].asNumber()));
	return Value::makeNull();
}

static Value zoomGetContrast(const VMApi &, const std::vector<Value> &) {
	return Value(static_cast<double>(ZoomService::instance().getContrast()));
}

static Value zoomPixelColor(const VMApi &api, const std::vector<Value> &args) {
	if (args.size() < 2) return Value::makeNull();
	auto rgb = ZoomService::instance().getPixelColor(
		static_cast<int>(args[0].asNumber()),
		static_cast<int>(args[1].asNumber()));
	auto arr = api.makeArray();
	api.push(arr, Value(rgb[0]));
	api.push(arr, Value(rgb[1]));
	api.push(arr, Value(rgb[2]));
	return arr;
}

static Value zoomPixelColorHex(const VMApi &api, const std::vector<Value> &args) {
	if (args.size() < 2) return Value::makeNull();
	auto hex = ZoomService::instance().getPixelColorHex(
		static_cast<int>(args[0].asNumber()),
		static_cast<int>(args[1].asNumber()));
	auto sid = api.vm().createRuntimeString(std::move(hex));
	return Value::makeStringId(sid.id);
}

static Value zoomScreenWidth(const VMApi &, const std::vector<Value> &) {
	return Value(ZoomService::instance().getScreenWidth());
}

static Value zoomScreenHeight(const VMApi &, const std::vector<Value> &) {
	return Value(ZoomService::instance().getScreenHeight());
}

static Value zoomBackendName(const VMApi &api, const std::vector<Value> &) {
	auto name = ZoomService::instance().getBackendName();
	auto sid = api.vm().createRuntimeString(std::move(name));
	return Value::makeStringId(sid.id);
}

#define ZR(name, fn) api.registerFunction("zoom." name, [api](const std::vector<Value> &a) mutable { return fn(api, a); })
#define ZF(name) api.setField(obj, name, api.makeFunctionRef("zoom." name))

void registerZoomModule(const VMApi &api) {
	auto obj = api.makeObject();
	api.setGlobal("zoom", obj);
	ZR("start", zoomStart);
	ZR("stop", zoomStop);
	ZR("isRunning", zoomIsRunning);
	ZR("in", zoomIn);
	ZR("out", zoomOut);
	ZR("to", zoomTo);
	ZR("reset", zoomReset);
	ZR("getLevel", zoomGetLevel);
	ZR("toggle", zoomToggle);
	ZR("setScale", zoomSetScale);
	ZR("getScale", zoomGetScale);
	ZR("setRegion", zoomSetRegion);
	ZR("getRegion", zoomGetRegion);
	ZR("setFilter", zoomSetFilter);
	ZR("getFilter", zoomGetFilter);
	ZR("setLocked", zoomSetLocked);
	ZR("isLocked", zoomIsLocked);
	ZR("setFollowCursor", zoomSetFollowCursor);
	ZR("isFollowCursor", zoomIsFollowCursor);
	ZR("setColorInvert", zoomSetColorInvert);
	ZR("isColorInvert", zoomIsColorInvert);
	ZR("setBrightness", zoomSetBrightness);
	ZR("getBrightness", zoomGetBrightness);
	ZR("setContrast", zoomSetContrast);
	ZR("getContrast", zoomGetContrast);
	ZR("pixelColor", zoomPixelColor);
	ZR("pixelColorHex", zoomPixelColorHex);
	ZR("screenWidth", zoomScreenWidth);
	ZR("screenHeight", zoomScreenHeight);
	ZR("backendName", zoomBackendName);
	ZF("start"); ZF("stop"); ZF("isRunning");
	ZF("in"); ZF("out"); ZF("to"); ZF("reset"); ZF("getLevel"); ZF("toggle");
	ZF("setScale"); ZF("getScale");
	ZF("setRegion"); ZF("getRegion");
	ZF("setFilter"); ZF("getFilter");
	ZF("setLocked"); ZF("isLocked");
	ZF("setFollowCursor"); ZF("isFollowCursor");
	ZF("setColorInvert"); ZF("isColorInvert");
	ZF("setBrightness"); ZF("getBrightness");
	ZF("setContrast"); ZF("getContrast");
	ZF("pixelColor"); ZF("pixelColorHex");
	ZF("screenWidth"); ZF("screenHeight");
	ZF("backendName");
	api.setField(obj, "Nearest", Value(0));
	api.setField(obj, "Bilinear", Value(1));
	api.setField(obj, "Sharpen", Value(2));
	api.setField(obj, "Lanczos", Value(3));
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"
HAVEL_MODULE_PLUGIN_IMPL(zoom, "2.0.0", "Screen magnifier module (compositor zoom backends)",
	havel::modules::registerZoomModule(*api);
)
#endif
