// MediaBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
void MediaBridge::install(PipelineOptions &options) {
  options.host_functions["media.playPause"] = [ctx = ctx_](const auto &args) {
    return handleMediaPlayPause(args, ctx);
  };
  options.host_functions["media.play"] = [ctx = ctx_](const auto &args) {
    return handleMediaPlay(args, ctx);
  };
  options.host_functions["media.pause"] = [ctx = ctx_](const auto &args) {
    return handleMediaPause(args, ctx);
  };
  options.host_functions["media.stop"] = [ctx = ctx_](const auto &args) {
    return handleMediaStop(args, ctx);
  };
  options.host_functions["media.next"] = [ctx = ctx_](const auto &args) {
    return handleMediaNext(args, ctx);
  };
  options.host_functions["media.previous"] = [ctx = ctx_](const auto &args) {
    return handleMediaPrevious(args, ctx);
  };
  options.host_functions["media.getVolume"] = [ctx = ctx_](const auto &args) {
    return handleMediaGetVolume(args, ctx);
  };
  options.host_functions["media.setVolume"] = [ctx = ctx_](const auto &args) {
    return handleMediaSetVolume(args, ctx);
  };
  options.host_functions["media.getActivePlayer"] =
      [ctx = ctx_](const auto &args) {
        return handleMediaGetActivePlayer(args, ctx);
      };
  options.host_functions["media.setActivePlayer"] =
      [ctx = ctx_](const auto &args) {
        return handleMediaSetActivePlayer(args, ctx);
      };
  options.host_functions["media.getAvailablePlayers"] =
      [ctx = ctx_](const auto &args) {
        return handleMediaGetAvailablePlayers(args, ctx);
      };
}


Value
MediaBridge::handleMediaPlayPause(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    media.playPause();
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}


Value
MediaBridge::handleMediaPlay(const std::vector<Value> &args,
                             const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    media.play();
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}


Value
MediaBridge::handleMediaPause(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    media.pause();
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}


Value
MediaBridge::handleMediaStop(const std::vector<Value> &args,
                             const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    media.stop();
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}


Value
MediaBridge::handleMediaNext(const std::vector<Value> &args,
                             const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    media.next();
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}


Value
MediaBridge::handleMediaPrevious(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    media.previous();
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}


Value
MediaBridge::handleMediaGetVolume(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    return Value::makeDouble(media.getVolume());
  } catch (...) {
    return Value::makeDouble(0.0);
  }
}


Value
MediaBridge::handleMediaSetVolume(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error(
        "media.setVolume() requires a volume value (0.0-1.0)");
  }
  double volume = 0.0;
  if (args[0].isDouble()) {
    volume = args[0].asDouble();
  } else if (args[0].isInt()) {
    volume = static_cast<double>(args[0].asInt()) / 100.0;
  }
  try {
    ::havel::host::MediaService media;
    media.setVolume(volume);
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}


Value
MediaBridge::handleMediaGetActivePlayer(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }
  try {
    ::havel::host::MediaService media;
    auto outRef = vm->getHeap().allocateString(media.getActivePlayer());
    return Value::makeStringId(outRef.id);
  } catch (...) {
    return Value::makeNull();
  }
}


Value
MediaBridge::handleMediaSetActivePlayer(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("media.setActivePlayer() requires a player name");
  }
  if (!ctx || !ctx->vm) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string name;
  if (args[0].isStringValId() || args[0].isStringId()) {
    name = vm->resolveStringKey(args[0]);
  } else {
    throw std::runtime_error("media.setActivePlayer() requires a string");
  }
  try {
    ::havel::host::MediaService media;
    media.setActivePlayer(name);
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}


Value MediaBridge::handleMediaGetAvailablePlayers(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }
  try {
    ::havel::host::MediaService media;
    auto players = media.getAvailablePlayers();
    auto arr = vm->createHostArray();
    for (const auto &player : players) {
      auto outRef = vm->getHeap().allocateString(player);
      vm->pushHostArrayValue(arr, Value::makeStringId(outRef.id));
    }
    return Value::makeArrayId(arr.id);
  } catch (...) {
    return Value::makeNull();
  }
}

// ============================================================================
// NetworkBridge Implementation
// ============================================================================

} // namespace havel::compiler
