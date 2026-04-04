/*
 * ScreenshotModule.cpp - Screenshot capture for Havel bytecode VM
 */
#include "ScreenshotModule.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "host/screenshot/ScreenshotService.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

// ============================================================================
// Screenshot Module Implementation
// ============================================================================

static host::ScreenshotService* getService() {
  return &host::ScreenshotService::getInstance();
}

// Helper: Convert RGBA vector to VM array of objects with r,g,b,a fields
static Value rgbaToVmArray(VMApi &api, const std::vector<uint8_t> &rgba, int width, int height) {
  auto result = api.makeObject();
  
  // Store dimensions
  api.setField(result, "width", Value::makeInt(width));
  api.setField(result, "height", Value::makeInt(height));
  
  // Create pixel array
  auto pixels = api.makeArray();
  size_t pixelCount = (rgba.size() / 4);
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

// Helper: Extract int from Value
static int toInt(const Value &v) {
  if (v.isInt()) return static_cast<int>(v.asInt());
  if (v.isDouble()) return static_cast<int>(v.asDouble());
  return 0;
}

// ============================================================================
// Screenshot Functions
// ============================================================================

// screenshot.capture() -> image object
static Value screenshotCapture(VMApi &api, const std::vector<Value> &args) {
  auto service = getService();
  if (!service) {
    return api.makeNull();
  }
  
  auto rgba = service->captureFullDesktop();
  // Estimate dimensions
  int pixels = static_cast<int>(rgba.size() / 4);
  int dim = static_cast<int>(std::sqrt(pixels));
  
  return rgbaToVmArray(api, rgba, dim, dim);
}

// screenshot.captureMonitor(monitorIndex) -> image object
static Value screenshotCaptureMonitor(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 1) {
    return api.makeNull();
  }
  
  auto service = getService();
  if (!service) {
    return api.makeNull();
  }
  
  int monitorIndex = toInt(args[0]);
  auto rgba = service->captureMonitor(monitorIndex);
  
  auto geometry = service->getMonitorGeometry(monitorIndex);
  int width = geometry.size() >= 3 ? geometry[2] : 1920;
  int height = geometry.size() >= 4 ? geometry[3] : 1080;
  
  return rgbaToVmArray(api, rgba, width, height);
}

// screenshot.captureActiveWindow() -> image object
static Value screenshotCaptureActiveWindow(VMApi &api, const std::vector<Value> &args) {
  auto service = getService();
  if (!service) {
    return api.makeNull();
  }
  
  auto rgba = service->captureActiveWindow();
  int pixels = static_cast<int>(rgba.size() / 4);
  int dim = static_cast<int>(std::sqrt(pixels));
  
  return rgbaToVmArray(api, rgba, dim, dim);
}

// screenshot.captureRegion(x, y, width, height) -> image object
static Value screenshotCaptureRegion(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 4) {
    return api.makeNull();
  }
  
  auto service = getService();
  if (!service) {
    return api.makeNull();
  }
  
  int x = toInt(args[0]);
  int y = toInt(args[1]);
  int width = toInt(args[2]);
  int height = toInt(args[3]);
  
  auto rgba = service->captureRegion(x, y, width, height);
  return rgbaToVmArray(api, rgba, width, height);
}

// screenshot.monitorCount() -> int
static Value screenshotMonitorCount(VMApi &api, const std::vector<Value> &args) {
  auto service = getService();
  if (!service) {
    return Value::makeInt(0);
  }
  return Value::makeInt(service->getMonitorCount());
}

// screenshot.monitorGeometry(index) -> {x, y, width, height}
static Value screenshotMonitorGeometry(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 1) {
    return api.makeNull();
  }
  
  auto service = getService();
  if (!service) {
    return api.makeNull();
  }
  
  int monitorIndex = toInt(args[0]);
  auto geometry = service->getMonitorGeometry(monitorIndex);
  
  auto result = api.makeObject();
  if (geometry.size() >= 4) {
    api.setField(result, "x", Value::makeInt(geometry[0]));
    api.setField(result, "y", Value::makeInt(geometry[1]));
    api.setField(result, "width", Value::makeInt(geometry[2]));
    api.setField(result, "height", Value::makeInt(geometry[3]));
  }
  
  return result;
}

// ============================================================================
// Register Screenshot Module
// ============================================================================

void registerScreenshotModule(compiler::VMApi &api) {
  api.registerFunction("screenshot.capture",
                       [&api](const std::vector<Value> &args) {
                         return screenshotCapture(api, args);
                       });
  
  api.registerFunction("screenshot.captureMonitor",
                       [&api](const std::vector<Value> &args) {
                         return screenshotCaptureMonitor(api, args);
                       });
  
  api.registerFunction("screenshot.captureActiveWindow",
                       [&api](const std::vector<Value> &args) {
                         return screenshotCaptureActiveWindow(api, args);
                       });
  
  api.registerFunction("screenshot.captureRegion",
                       [&api](const std::vector<Value> &args) {
                         return screenshotCaptureRegion(api, args);
                       });
  
  api.registerFunction("screenshot.monitorCount",
                       [&api](const std::vector<Value> &args) {
                         return screenshotMonitorCount(api, args);
                       });
  
  api.registerFunction("screenshot.monitorGeometry",
                       [&api](const std::vector<Value> &args) {
                         return screenshotMonitorGeometry(api, args);
                       });
  
  auto screenshotObj = api.makeObject();
  api.setField(screenshotObj, "capture", api.makeFunctionRef("screenshot.capture"));
  api.setField(screenshotObj, "captureMonitor", api.makeFunctionRef("screenshot.captureMonitor"));
  api.setField(screenshotObj, "captureActiveWindow", api.makeFunctionRef("screenshot.captureActiveWindow"));
  api.setField(screenshotObj, "captureRegion", api.makeFunctionRef("screenshot.captureRegion"));
  api.setField(screenshotObj, "monitorCount", api.makeFunctionRef("screenshot.monitorCount"));
  api.setField(screenshotObj, "monitorGeometry", api.makeFunctionRef("screenshot.monitorGeometry"));
  
  api.setGlobal("screenshot", screenshotObj);
}

} // namespace havel::modules
