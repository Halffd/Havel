// AudioBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
void AudioBridge::install(PipelineOptions &options) {
  options.host_functions["audio.getVolume"] = [ctx = ctx_](const auto &args) {
    return handleGetVolume(args, ctx);
  };
  options.host_functions["audio.setVolume"] = [ctx = ctx_](const auto &args) {
    return handleSetVolume(args, ctx);
  };
  options.host_functions["audio.isMuted"] = [ctx = ctx_](const auto &args) {
    return handleIsMuted(args, ctx);
  };
  options.host_functions["audio.setMute"] = [ctx = ctx_](const auto &args) {
    return handleSetMute(args, ctx);
  };
  options.host_functions["audio.toggleMute"] = [ctx = ctx_](const auto &args) {
    return handleToggleMute(args, ctx);
  };
  options.host_functions["audio.getDevices"] = [ctx = ctx_](const auto &args) {
    return handleGetDevices(args, ctx);
  };
  options.host_functions["audio.findDeviceByIndex"] =
      [ctx = ctx_](const auto &args) {
        return handleFindDeviceByIndex(args, ctx);
      };
  options.host_functions["audio.findDeviceByName"] =
      [ctx = ctx_](const auto &args) {
        return handleFindDeviceByName(args, ctx);
      };
  options.host_functions["audio.setDefaultOutput"] =
      [ctx = ctx_](const auto &args) {
        return handleSetDefaultOutput(args, ctx);
      };
  options.host_functions["audio.getDefaultOutput"] =
      [ctx = ctx_](const auto &args) {
        return handleGetDefaultOutput(args, ctx);
      };
  options.host_functions["audio.playTestSound"] =
      [ctx = ctx_](const auto &args) { return handlePlayTestSound(args, ctx); };
  options.host_functions["audio.increaseVolume"] =
      [ctx = ctx_](const auto &args) {
        return handleIncreaseVolume(args, ctx);
      };
  options.host_functions["audio.decreaseVolume"] =
      [ctx = ctx_](const auto &args) {
        return handleDecreaseVolume(args, ctx);
      };
}


Value
AudioBridge::handleGetVolume(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value(1.0); // Default volume
  }
  // Check for device-specific overload: getVolume(device)
  if (!args.empty() && args[0].isStringValId()) {
    std::string device = strVal(args[0], ctx ? ctx->vm : nullptr);
    return Value::makeDouble(ctx->audioManager->getVolume(device));
  }
  // Default device
  return Value::makeDouble(ctx->audioManager->getVolume());
}


Value
AudioBridge::handleSetVolume(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }
  if (args.empty()) {
    throw std::runtime_error(
        "audio.setVolume() requires at least a volume number");
  }

  // Check for (device, volume) overload
  if (args.size() >= 2) {
    if (args[0].isStringValId()) {
      std::string device = strVal(args[0], ctx ? ctx->vm : nullptr);
      double volume = 1.0;
      if (args[1].isDouble()) {
        volume = args[1].asDouble();
      } else if (args[1].isInt()) {
        volume = static_cast<double>(args[1].asInt());
      } else {
        throw std::runtime_error(
            "audio.setVolume(device, volume) requires volume as number");
      }
      return Value::makeBool(ctx->audioManager->setVolume(device, volume));
    }
  }

  // Single argument: setVolume(volume) for default device
  double volume = 1.0;
  if (args[0].isDouble()) {
    volume = args[0].asDouble();
  } else if (args[0].isInt()) {
    volume = static_cast<double>(args[0].asInt());
  } else {
    throw std::runtime_error("audio.setVolume() requires a number");
  }
  return Value::makeBool(ctx->audioManager->setVolume(volume));
}


Value AudioBridge::handleIsMuted(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }
  // Check for device-specific overload: isMuted(device)
  if (!args.empty() && args[0].isStringValId()) {
    std::string device = strVal(args[0], ctx ? ctx->vm : nullptr);
    return Value::makeBool(ctx->audioManager->isMuted(device));
  }
  // Default device
  return Value::makeBool(ctx->audioManager->isMuted());
}


Value AudioBridge::handleSetMute(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }
  // Check for (device, muted) overload
  if (args.size() >= 2) {
    if (args[0].isStringValId() &&
        args[1].isBool()) {
      std::string device = strVal(args[0], ctx ? ctx->vm : nullptr);
      bool muted = args[1].asBool();
      return Value::makeBool(ctx->audioManager->setMute(device, muted));
    }
  }
  // Single argument: setMute(muted) for default device
  if (args.empty() || !args[0].isBool()) {
    throw std::runtime_error("audio.setMute() requires a boolean");
  }
  bool muted = args[0].asBool();
  return Value::makeBool(ctx->audioManager->setMute(muted));
}


Value
AudioBridge::handleToggleMute(const std::vector<Value> &args,
                              const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }
  // Check for device-specific overload: toggleMute(device)
  if (!args.empty() && args[0].isStringValId()) {
    std::string device = strVal(args[0], ctx ? ctx->vm : nullptr);
    return Value::makeBool(ctx->audioManager->toggleMute(device));
  }
  // Default device
  return Value::makeBool(ctx->audioManager->toggleMute());
}


Value
AudioBridge::handleGetDevices(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->audioManager) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }

  const auto &devices = ctx->audioManager->getDevices();
  auto arr = vm->createHostArray();
  auto arrGuard = vm->makeRoot(Value::makeArrayId(arr.id));

  for (const auto &dev : devices) {
    auto obj = vm->createHostObject();
    auto nameRef = vm->getHeap().allocateString(dev.name);
    vm->setHostObjectField(obj, "name", Value::makeStringId(nameRef.id));
    auto descRef = vm->getHeap().allocateString(dev.description);
    vm->setHostObjectField(obj, "description", Value::makeStringId(descRef.id));
    vm->setHostObjectField(obj, "index",
                           Value::makeInt(static_cast<int64_t>(dev.index)));
    vm->setHostObjectField(obj, "isDefault", Value::makeBool(dev.isDefault));
    vm->setHostObjectField(obj, "isMuted", Value::makeBool(dev.isMuted));
    vm->setHostObjectField(obj, "volume", Value::makeDouble(dev.volume));
    vm->setHostObjectField(obj, "channels",
                           Value::makeInt(static_cast<int64_t>(dev.channels)));
    vm->pushHostArrayValue(arr, Value::makeObjectId(obj.id));
  }

  return Value::makeArrayId(arr.id);
}


Value
AudioBridge::handleFindDeviceByIndex(const std::vector<Value> &args,
                                     const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeNull();
  }
  if (args.empty() || !args[0].isInt()) {
    throw std::runtime_error("audio.findDeviceByIndex() requires an index");
  }
  uint32_t index = static_cast<uint32_t>(args[0].asInt());

  auto *dev = ctx->audioManager->findDeviceByIndex(index);
  if (!dev) {
    return Value::makeNull();
  }

  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }

  auto obj = vm->createHostObject();
  auto nameRef = vm->getHeap().allocateString(dev->name);
  vm->setHostObjectField(obj, "name", Value::makeStringId(nameRef.id));
  auto descRef = vm->getHeap().allocateString(dev->description);
  vm->setHostObjectField(obj, "description", Value::makeStringId(descRef.id));
  vm->setHostObjectField(obj, "index",
                         Value::makeInt(static_cast<int64_t>(dev->index)));
  vm->setHostObjectField(obj, "isDefault", Value::makeBool(dev->isDefault));
  vm->setHostObjectField(obj, "isMuted", Value::makeBool(dev->isMuted));
  vm->setHostObjectField(obj, "volume", Value::makeDouble(dev->volume));
  vm->setHostObjectField(obj, "channels",
                         Value::makeInt(static_cast<int64_t>(dev->channels)));

  delete dev;
  return Value::makeObjectId(obj.id);
}


Value
AudioBridge::handleFindDeviceByName(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeNull();
  }
  if (args.empty() || !args[0].isStringValId()) {
    throw std::runtime_error("audio.findDeviceByName() requires a name string");
  }
  std::string name = strVal(args[0], ctx ? ctx->vm : nullptr);

  auto *dev = ctx->audioManager->findDeviceByName(name);
  if (!dev) {
    return Value::makeNull();
  }

  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    delete dev;
    return Value::makeNull();
  }

  auto obj = vm->createHostObject();
  auto nameRef = vm->getHeap().allocateString(dev->name);
  vm->setHostObjectField(obj, "name", Value::makeStringId(nameRef.id));
  auto descRef = vm->getHeap().allocateString(dev->description);
  vm->setHostObjectField(obj, "description", Value::makeStringId(descRef.id));
  vm->setHostObjectField(obj, "index",
                         Value::makeInt(static_cast<int64_t>(dev->index)));
  vm->setHostObjectField(obj, "isDefault", Value::makeBool(dev->isDefault));
  vm->setHostObjectField(obj, "isMuted", Value::makeBool(dev->isMuted));
  vm->setHostObjectField(obj, "volume", Value::makeDouble(dev->volume));
  vm->setHostObjectField(obj, "channels",
                         Value::makeInt(static_cast<int64_t>(dev->channels)));

  delete dev;
  return Value::makeObjectId(obj.id);
}


Value
AudioBridge::handleSetDefaultOutput(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }
  if (args.empty() || !args[0].isStringValId()) {
    throw std::runtime_error("audio.setDefaultOutput() requires a device name");
  }
  std::string device = strVal(args[0], ctx ? ctx->vm : nullptr);
  return Value::makeBool(ctx->audioManager->setDefaultOutput(device));
}


Value
AudioBridge::handleGetDefaultOutput(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->audioManager) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }
  auto outRef = vm->getHeap().allocateString(ctx->audioManager->getDefaultOutput());
  return Value::makeStringId(outRef.id);
}


Value
AudioBridge::handlePlayTestSound(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }
  return Value::makeBool(ctx->audioManager->playTestSound());
}


Value
AudioBridge::handleIncreaseVolume(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }

  double amount = 0.05;
  std::string device;

  // Parse arguments: can be (amount) or (device, amount)
  if (args.size() >= 2) {
    // (device, amount)
    if (args[0].isStringValId()) {
      device = strVal(args[0], ctx ? ctx->vm : nullptr);
    }
    if (args[1].isDouble()) {
      amount = args[1].asDouble();
    } else if (args[1].isInt()) {
      amount = static_cast<double>(args[1].asInt());
    }
  } else if (args.size() == 1) {
    // (amount) or (device)
    if (args[0].isDouble()) {
      amount = args[0].asDouble();
    } else if (args[0].isInt()) {
      amount = static_cast<double>(args[0].asInt());
    } else if (args[0].isStringValId()) {
      device = strVal(args[0], ctx ? ctx->vm : nullptr);
    }
  }

  if (device.empty()) {
    return Value::makeDouble(ctx->audioManager->increaseVolume(amount));
  } else {
    return Value::makeDouble(ctx->audioManager->increaseVolume(device, amount));
  }
}


Value
AudioBridge::handleDecreaseVolume(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }

  double amount = 0.05;
  std::string device;

  // Parse arguments: can be (amount) or (device, amount)
  if (args.size() >= 2) {
    // (device, amount)
    if (args[0].isStringValId()) {
      device = strVal(args[0], ctx ? ctx->vm : nullptr);
    }
    if (args[1].isDouble()) {
      amount = args[1].asDouble();
    } else if (args[1].isInt()) {
      amount = static_cast<double>(args[1].asInt());
    }
  } else if (args.size() == 1) {
    // (amount) or (device)
    if (args[0].isDouble()) {
      amount = args[0].asDouble();
    } else if (args[0].isInt()) {
      amount = static_cast<double>(args[0].asInt());
    } else if (args[0].isStringValId()) {
      device = strVal(args[0], ctx ? ctx->vm : nullptr);
    }
  }

  if (device.empty()) {
    return Value::makeDouble(ctx->audioManager->decreaseVolume(amount));
  } else {
    return Value::makeDouble(ctx->audioManager->decreaseVolume(device, amount));
  }
}

// ============================================================================
// DisplayBridge Implementation
// ============================================================================

} // namespace havel::compiler
