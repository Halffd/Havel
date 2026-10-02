// BrightnessBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
namespace {
} // namespace

void BrightnessBridge::install(PipelineOptions &options) {
  options.host_functions["brightness.get"] =
      [ctx = ctx_](const auto &args) { return handleGetBrightness(args, ctx); };
  options.host_functions["brightness.set"] =
      [ctx = ctx_](const auto &args) { return handleSetBrightness(args, ctx); };
  options.host_functions["brightness.getTemperature"] =
      [ctx = ctx_](const auto &args) { return handleGetTemperature(args, ctx); };
  options.host_functions["brightness.setTemperature"] =
      [ctx = ctx_](const auto &args) { return handleSetTemperature(args, ctx); };
  options.host_functions["brightness.getGamma"] =
      [ctx = ctx_](const auto &args) { return handleGetGamma(args, ctx); };
  options.host_functions["brightness.setGamma"] =
      [ctx = ctx_](const auto &args) { return handleSetGamma(args, ctx); };
  options.host_functions["brightness.setGammaRGB"] =
      [ctx = ctx_](const auto &args) { return handleSetGammaRGB(args, ctx); };
  options.host_functions["brightness.getGammaR"] =
      [ctx = ctx_](const auto &args) { return handleGetGammaR(args, ctx); };
  options.host_functions["brightness.getGammaG"] =
      [ctx = ctx_](const auto &args) { return handleGetGammaG(args, ctx); };
  options.host_functions["brightness.getGammaB"] =
      [ctx = ctx_](const auto &args) { return handleGetGammaB(args, ctx); };
  options.host_functions["brightness.getShadowLift"] =
      [ctx = ctx_](const auto &args) { return handleGetShadowLift(args, ctx); };
  options.host_functions["brightness.setShadowLift"] =
      [ctx = ctx_](const auto &args) { return handleSetShadowLift(args, ctx); };
  options.host_functions["brightness.increase"] =
      [ctx = ctx_](const auto &args) { return handleIncreaseBrightness(args, ctx); };
  options.host_functions["brightness.decrease"] =
      [ctx = ctx_](const auto &args) { return handleDecreaseBrightness(args, ctx); };
  options.host_functions["brightness.increaseTemperature"] =
      [ctx = ctx_](const auto &args) { return handleIncreaseTemperature(args, ctx); };
  options.host_functions["brightness.decreaseTemperature"] =
      [ctx = ctx_](const auto &args) { return handleDecreaseTemperature(args, ctx); };
  options.host_functions["brightness.increaseGamma"] =
      [ctx = ctx_](const auto &args) { return handleIncreaseGamma(args, ctx); };
  options.host_functions["brightness.decreaseGamma"] =
      [ctx = ctx_](const auto &args) { return handleDecreaseGamma(args, ctx); };
  options.host_functions["brightness.getMonitors"] =
      [ctx = ctx_](const auto &args) { return handleGetMonitors(args, ctx); };
}


Value
BrightnessBridge::handleGetBrightness(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeNull();

  double brightness;
  if (args.empty()) {
    brightness = ctx->brightnessManager->getBrightness();
} else if (args[0].isStringId() || args[0].isStringValId()) {
    std::string monitor = strVal(args[0], vm);
    brightness = ctx->brightnessManager->getBrightness(monitor);
  } else {
    return Value::makeNull();
  }
  return Value::makeDouble(brightness);
}


Value
BrightnessBridge::handleSetBrightness(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeBool(false);
  if (args.size() < 1)
    return Value::makeBool(false);

  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeBool(false);

  double brightness = args[0].asNumber();
  bool success;
if (args.size() >= 2 && (args[1].isStringId() || args[1].isStringValId())) {
    std::string monitor = strVal(args[1], vm);
    success = ctx->brightnessManager->setBrightness(monitor, brightness);
  } else {
    success = ctx->brightnessManager->setBrightness(brightness);
  }
  return Value::makeBool(success);
}


Value
BrightnessBridge::handleGetTemperature(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeNull();

  int temp;
  if (args.empty()) {
    temp = ctx->brightnessManager->getTemperature();
} else if (args[0].isStringId() || args[0].isStringValId()) {
    std::string monitor = strVal(args[0], vm);
    temp = ctx->brightnessManager->getTemperature(monitor);
  } else {
    return Value::makeNull();
  }
  return Value::makeInt(temp);
}


Value
BrightnessBridge::handleSetTemperature(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeBool(false);
  if (args.size() < 1)
    return Value::makeBool(false);

  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeBool(false);

  int kelvin = static_cast<int>(args[0].asNumber());
  bool success;
if (args.size() >= 2 && (args[1].isStringId() || args[1].isStringValId())) {
    std::string monitor = strVal(args[1], vm);
    success = ctx->brightnessManager->setTemperature(monitor, kelvin);
  } else {
    success = ctx->brightnessManager->setTemperature(kelvin);
  }
  return Value::makeBool(success);
}


Value
BrightnessBridge::handleGetGamma(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeNull();

  auto rgb = ctx->brightnessManager->getGammaRGB();
if (args.size() >= 1 && (args[0].isStringId() || args[0].isStringValId())) {
    std::string monitor = strVal(args[0], vm);
    rgb = ctx->brightnessManager->getGammaRGB(monitor);
  }
  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "red", Value::makeDouble(rgb.red));
  vm->setHostObjectField(obj, "green", Value::makeDouble(rgb.green));
  vm->setHostObjectField(obj, "blue", Value::makeDouble(rgb.blue));
  return Value::makeObjectId(obj.id);
}


Value
BrightnessBridge::handleSetGamma(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeBool(false);
  if (args.size() < 3)
    return Value::makeBool(false);

  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeBool(false);

  double red = args[0].asNumber();
  double green = args[1].asNumber();
  double blue = args[2].asNumber();
  bool success;
if (args.size() >= 4 && (args[3].isStringId() || args[3].isStringValId())) {
    std::string monitor = strVal(args[3], vm);
    success = ctx->brightnessManager->setGammaRGB(monitor, red, green, blue);
  } else {
    success = ctx->brightnessManager->setGammaRGB(red, green, blue);
  }
  return Value::makeBool(success);
}


Value
BrightnessBridge::handleSetGammaRGB(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeBool(false);
  if (args.size() < 3)
    return Value::makeBool(false);

  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeBool(false);

  double red = args[0].asNumber();
  double green = args[1].asNumber();
  double blue = args[2].asNumber();
  bool success;
  if (args.size() >= 4 && (args[3].isStringId() || args[3].isStringValId())) {
    std::string monitor = strVal(args[3], vm);
    success = ctx->brightnessManager->setGammaRGB(monitor, red, green, blue);
  } else {
    success = ctx->brightnessManager->setGammaRGB(red, green, blue);
  }
  return Value::makeBool(success);
}


Value
BrightnessBridge::handleGetGammaR(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeNull();

  double val;
  if (args.empty()) {
    val = ctx->brightnessManager->getGammaRGB().red;
  } else if (args[0].isStringId() || args[0].isStringValId()) {
    std::string monitor = strVal(args[0], vm);
    val = ctx->brightnessManager->getGammaRGB(monitor).red;
  } else {
    return Value::makeNull();
  }
  return Value::makeDouble(val);
}


Value
BrightnessBridge::handleGetGammaG(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeNull();

  double val;
  if (args.empty()) {
    val = ctx->brightnessManager->getGammaRGB().green;
  } else if (args[0].isStringId() || args[0].isStringValId()) {
    std::string monitor = strVal(args[0], vm);
    val = ctx->brightnessManager->getGammaRGB(monitor).green;
  } else {
    return Value::makeNull();
  }
  return Value::makeDouble(val);
}


Value
BrightnessBridge::handleGetGammaB(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeNull();

  double val;
  if (args.empty()) {
    val = ctx->brightnessManager->getGammaRGB().blue;
  } else if (args[0].isStringId() || args[0].isStringValId()) {
    std::string monitor = strVal(args[0], vm);
    val = ctx->brightnessManager->getGammaRGB(monitor).blue;
  } else {
    return Value::makeNull();
  }
  return Value::makeDouble(val);
}


Value
BrightnessBridge::handleGetShadowLift(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeNull();

  double lift;
  if (args.empty()) {
    lift = ctx->brightnessManager->getShadowLift();
} else if (args[0].isStringId() || args[0].isStringValId()) {
    std::string monitor = strVal(args[0], vm);
    lift = ctx->brightnessManager->getShadowLift(monitor);
  } else {
    return Value::makeNull();
  }
  return Value::makeDouble(lift);
}


Value
BrightnessBridge::handleSetShadowLift(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeBool(false);
  if (args.size() < 1)
    return Value::makeBool(false);

  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeBool(false);

  double lift = args[0].asNumber();
  bool success;
if (args.size() >= 2 && (args[1].isStringId() || args[1].isStringValId())) {
    std::string monitor = strVal(args[1], vm);
    success = ctx->brightnessManager->setShadowLift(monitor, lift);
  } else {
    success = ctx->brightnessManager->setShadowLift(lift);
  }
  return Value::makeBool(success);
}

// Helper to get monitor name from args (supports both index and name)
static std::string getMonitorFromArgs(const std::vector<Value> &args, size_t arg_index, VM *vm, BrightnessManager *mgr) {
  if (arg_index >= args.size())
    return "";
  
  const Value &arg = args[arg_index];
  if (arg.isInt()) {
    // Monitor index
    size_t index = static_cast<size_t>(arg.asInt());
    return mgr->getMonitorByIndex(index);
  } else if (arg.isStringId() || arg.isStringValId()) {
    // Monitor name
    return strVal(arg, vm);
  }
  return "";
}


Value
BrightnessBridge::handleIncreaseBrightness(const std::vector<Value> &args,
                                           const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeBool(false);
  // 0 args means the default step (0.02), matching the module copy and
  // DEFAULT_BRIGHTNESS_AMOUNT — the old `args.size() < 1` rejection made
  // bare `brightness.increase()` return false with brightness stuck.
  double amount = args.empty() ? 0.02 : args[0].asNumber();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeBool(false);
  bool success;
  if (args.size() >= 2) {
    std::string monitor = getMonitorFromArgs(args, 1, vm, ctx->brightnessManager);
    if (!monitor.empty()) {
      success = ctx->brightnessManager->increaseBrightness(monitor, amount);
    } else {
      success = ctx->brightnessManager->increaseBrightness(amount);
    }
  } else {
    success = ctx->brightnessManager->increaseBrightness(amount);
  }
  return Value::makeBool(success);
}


Value
BrightnessBridge::handleDecreaseBrightness(const std::vector<Value> &args,
                                           const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeBool(false);
  // 0 args means the default step (0.02), matching the module copy.
  double amount = args.empty() ? 0.02 : args[0].asNumber();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeBool(false);
  bool success;
  if (args.size() >= 2) {
    std::string monitor = getMonitorFromArgs(args, 1, vm, ctx->brightnessManager);
    if (!monitor.empty()) {
      success = ctx->brightnessManager->decreaseBrightness(monitor, amount);
    } else {
      success = ctx->brightnessManager->decreaseBrightness(amount);
    }
  } else {
    success = ctx->brightnessManager->decreaseBrightness(amount);
  }
  return Value::makeBool(success);
}


Value
BrightnessBridge::handleIncreaseTemperature(const std::vector<Value> &args,
                                            const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeBool(false);
  // 0 args means the default step (200K), matching the module copy and
  // DEFAULT_TEMP_AMOUNT.
  int amount = args.empty() ? 200 : static_cast<int>(args[0].asNumber());
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeBool(false);
  bool success;
  if (args.size() >= 2) {
    std::string monitor = getMonitorFromArgs(args, 1, vm, ctx->brightnessManager);
    if (!monitor.empty()) {
      success = ctx->brightnessManager->increaseTemperature(monitor, amount);
    } else {
      success = ctx->brightnessManager->increaseTemperature(amount);
    }
  } else {
    success = ctx->brightnessManager->increaseTemperature(amount);
  }
  return Value::makeBool(success);
}


Value
BrightnessBridge::handleDecreaseTemperature(const std::vector<Value> &args,
                                            const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeBool(false);
  // 0 args means the default step (200K), matching the module copy.
  int amount = args.empty() ? 200 : static_cast<int>(args[0].asNumber());
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeBool(false);
  bool success;
  if (args.size() >= 2) {
    std::string monitor = getMonitorFromArgs(args, 1, vm, ctx->brightnessManager);
    if (!monitor.empty()) {
      success = ctx->brightnessManager->decreaseTemperature(monitor, amount);
    } else {
      success = ctx->brightnessManager->decreaseTemperature(amount);
    }
  } else {
    success = ctx->brightnessManager->decreaseTemperature(amount);
  }
  return Value::makeBool(success);
}


Value
BrightnessBridge::handleIncreaseGamma(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeBool(false);
  if (args.size() < 1)
    return Value::makeBool(false);

  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeBool(false);

  int amount = static_cast<int>(args[0].asNumber() * 100); // Convert from 0.01 to 1
  // The C++ method expects amount in 0.01 increments, so we need to pass the raw value
  // Actually the method takes int amount where 1 = 0.01 gamma change
  // The hotkeys script passes 200 for 0.02, so we keep it as is
  int gammaAmount = static_cast<int>(args[0].asNumber());
  
  bool success;
  if (args.size() >= 2) {
    std::string monitor = getMonitorFromArgs(args, 1, vm, ctx->brightnessManager);
    if (!monitor.empty()) {
      success = ctx->brightnessManager->increaseGamma(monitor, gammaAmount);
    } else {
      success = ctx->brightnessManager->increaseGamma(gammaAmount);
    }
  } else {
    success = ctx->brightnessManager->increaseGamma(gammaAmount);
  }
  return Value::makeBool(success);
}


Value
BrightnessBridge::handleDecreaseGamma(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  if (!ctx || !ctx->brightnessManager)
    return Value::makeBool(false);
  if (args.size() < 1)
    return Value::makeBool(false);

  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeBool(false);

  int gammaAmount = static_cast<int>(args[0].asNumber());
  
  bool success;
  if (args.size() >= 2) {
    std::string monitor = getMonitorFromArgs(args, 1, vm, ctx->brightnessManager);
    if (!monitor.empty()) {
      success = ctx->brightnessManager->decreaseGamma(monitor, gammaAmount);
    } else {
      success = ctx->brightnessManager->decreaseGamma(gammaAmount);
    }
  } else {
    success = ctx->brightnessManager->decreaseGamma(gammaAmount);
  }
  return Value::makeBool(success);
}


Value
BrightnessBridge::handleGetMonitors(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->brightnessManager)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeNull();

  auto monitors = ctx->brightnessManager->getConnectedMonitors();
  auto arr = vm->createHostArray();
  auto arrGuard = vm->makeRoot(Value::makeArrayId(arr.id));

  for (const auto &name : monitors) {
    auto obj = vm->createHostObject();
    auto nameRef = vm->createRuntimeString(name);
    vm->setHostObjectField(obj, "name", Value::makeStringId(nameRef.id));
    vm->setHostObjectField(obj, "brightness",
                           Value::makeDouble(ctx->brightnessManager->getBrightness(name)));
    vm->setHostObjectField(obj, "temperature",
                           Value::makeInt(ctx->brightnessManager->getTemperature(name)));
    auto rgb = ctx->brightnessManager->getGammaRGB(name);
    vm->setHostObjectField(obj, "gammaRed", Value::makeDouble(rgb.red));
    vm->setHostObjectField(obj, "gammaGreen", Value::makeDouble(rgb.green));
    vm->setHostObjectField(obj, "gammaBlue", Value::makeDouble(rgb.blue));
    vm->setHostObjectField(obj, "shadowLift",
                           Value::makeDouble(ctx->brightnessManager->getShadowLift(name)));
    vm->pushHostArrayValue(arr, Value::makeObjectId(obj.id));
  }

  return Value::makeArrayId(arr.id);
}

// ============================================================================
// ConfigBridge Implementation
// ============================================================================

} // namespace havel::compiler
