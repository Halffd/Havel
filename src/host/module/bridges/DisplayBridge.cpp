// DisplayBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
void DisplayBridge::install(PipelineOptions &options) {
  options.host_functions["display.getMonitors"] =
      [ctx = ctx_](const auto &args) { return handleGetMonitors(args, ctx); };
  options.host_functions["display.getPrimary"] =
      [ctx = ctx_](const auto &args) { return handleGetPrimary(args, ctx); };
  options.host_functions["display.getCount"] = [ctx = ctx_](const auto &args) {
    return handleGetCount(args, ctx);
  };
  options.host_functions["display.getMonitorsArea"] =
  [ctx = ctx_](const auto &args) {
    return handleGetMonitorsArea(args, ctx);
  };
  options.host_functions["display.isX11"] = [ctx = ctx_](const auto &args) {
    return handleIsX11(args, ctx);
  };
  options.host_functions["display.isWayland"] = [ctx = ctx_](
                                                   const auto &args) {
    return handleIsWayland(args, ctx);
  };
  options.host_functions["display.isWindows"] = [ctx = ctx_](
                                                    const auto &args) {
    return handleIsWindows(args, ctx);
  };
  options.host_functions["display.protocol"] = [ctx = ctx_](
                                                   const auto &args) {
    return handleProtocol(args, ctx);
  };
  options.host_functions["display.wm"] = [ctx = ctx_](const auto &args) {
    return handleWm(args, ctx);
  };
  options.host_functions["display.displayNum"] = [ctx = ctx_](
                                                     const auto &args) {
    return handleDisplayNum(args, ctx);
  };
  options.host_functions["display.monitorsResolution"] = [ctx = ctx_](
                                                            const auto &args) {
    return handleMonitorsResolution(args, ctx);
  };
}


Value
DisplayBridge::handleGetMonitors(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)args;
  if (!ctx)
    return Value::makeNull();
  // Return array of monitor info objects
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeNull();

  auto monitors = ::havel::DisplayManager::GetMonitors();
  auto arr = vm->createHostArray();
  auto arrGuard = vm->makeRoot(Value::makeArrayId(arr.id));

    for (const auto &mon : monitors) {
        auto obj = vm->createHostObject();
        auto nameRef = vm->createRuntimeString(mon.name);
        vm->setHostObjectField(obj, "name", Value::makeStringId(nameRef.id));
    vm->setHostObjectField(obj, "x",
                           Value::makeInt(static_cast<int64_t>(mon.x)));
    vm->setHostObjectField(obj, "y",
                           Value::makeInt(static_cast<int64_t>(mon.y)));
    vm->setHostObjectField(obj, "width",
                           Value::makeInt(static_cast<int64_t>(mon.width)));
    vm->setHostObjectField(obj, "height",
                           Value::makeInt(static_cast<int64_t>(mon.height)));
    vm->setHostObjectField(obj, "isPrimary", Value::makeBool(mon.isPrimary));
    vm->pushHostArrayValue(arr, Value::makeObjectId(obj.id));
  }

  return Value::makeArrayId(arr.id);
}


Value
DisplayBridge::handleGetPrimary(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  if (!ctx) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }

    auto mon = ::havel::DisplayManager::GetPrimaryMonitor();
    auto obj = vm->createHostObject();
    auto nameRef = vm->createRuntimeString(mon.name);
    vm->setHostObjectField(obj, "name", Value::makeStringId(nameRef.id));
  vm->setHostObjectField(obj, "x", Value::makeInt(static_cast<int64_t>(mon.x)));
  vm->setHostObjectField(obj, "y", Value::makeInt(static_cast<int64_t>(mon.y)));
  vm->setHostObjectField(obj, "width",
                         Value::makeInt(static_cast<int64_t>(mon.width)));
  vm->setHostObjectField(obj, "height",
                         Value::makeInt(static_cast<int64_t>(mon.height)));
  vm->setHostObjectField(obj, "isPrimary", Value::makeBool(mon.isPrimary));

  return Value::makeObjectId(obj.id);
}


Value
DisplayBridge::handleGetCount(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto monitors = ::havel::DisplayManager::GetMonitors();
  return Value::makeInt(static_cast<int64_t>(monitors.size()));
}


Value
DisplayBridge::handleGetMonitorsArea(const std::vector<Value> &args,
                                     const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto monitors = ::havel::DisplayManager::GetMonitors();

  // Calculate total area
  int64_t totalWidth = 0;
  int64_t totalHeight = 0;
  int64_t minX = INT64_MAX;
  int64_t minY = INT64_MAX;
  int64_t maxX = INT64_MIN;
  int64_t maxY = INT64_MIN;

  for (const auto &mon : monitors) {
    if (static_cast<int64_t>(mon.x) < minX)
      minX = mon.x;
    if (static_cast<int64_t>(mon.y) < minY)
      minY = mon.y;
    if (static_cast<int64_t>(mon.x) + static_cast<int64_t>(mon.width) > maxX) {
      maxX = mon.x + mon.width;
    }
    if (static_cast<int64_t>(mon.y) + static_cast<int64_t>(mon.height) > maxY) {
      maxY = mon.y + mon.height;
    }
  }

  if (minX != INT64_MAX)
    totalWidth = maxX - minX;
  if (minY != INT64_MIN)
    totalHeight = maxY - minY;

  auto *vm = static_cast<VM *>(ctx->vm);
  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "width", Value::makeInt(totalWidth));
  vm->setHostObjectField(obj, "height", Value::makeInt(totalHeight));
  vm->setHostObjectField(obj, "totalArea",
                         Value::makeInt(totalWidth * totalHeight));
  vm->setHostObjectField(obj, "x", Value::makeInt(minX == INT64_MAX ? 0 : minX));
  vm->setHostObjectField(obj, "y", Value::makeInt(minY == INT64_MAX ? 0 : minY));

  return Value::makeObjectId(obj.id);
}


Value
DisplayBridge::handleIsX11(const std::vector<Value> &args,
                           const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(WindowManager::IsX11());
}


Value
DisplayBridge::handleIsWayland(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(WindowManager::IsWayland());
}


Value
DisplayBridge::handleIsWindows(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
#ifdef WINDOWS
  return Value::makeBool(true);
#else
  return Value::makeBool(false);
#endif
}


Value
DisplayBridge::handleProtocol(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeNull();
  std::string protocol = "unknown";
  const char *waylandDisplay = std::getenv("WAYLAND_DISPLAY");
  const char *x11Display = std::getenv("DISPLAY");
  const char *xdgSession = std::getenv("XDG_SESSION_TYPE");
  if (waylandDisplay && waylandDisplay[0] != '\0')
    protocol = "wayland";
  if (x11Display && x11Display[0] != '\0') {
    if (protocol == "unknown")
      protocol = "x11";
  }
  if (xdgSession && xdgSession[0] != '\0') {
    std::string session = xdgSession;
    if (session == "wayland")
      protocol = "wayland";
    else if (session == "x11")
      protocol = "x11";
    else if (session == "tty")
      protocol = "tty";
  }
  auto ref = vm->createRuntimeString(protocol);
  return Value::makeStringId(ref.id);
}


Value
DisplayBridge::handleWm(const std::vector<Value> &args,
                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeNull();
  std::string wmName = WindowManagerDetector::GetWMName();
  auto ref = vm->createRuntimeString(wmName);
  return Value::makeStringId(ref.id);
}


Value
DisplayBridge::handleDisplayNum(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  const char *display = std::getenv("DISPLAY");
  if (!display || display[0] == '\0')
    return Value::makeInt(0);
  std::string dpy(display);
  auto colonPos = dpy.find(':');
  if (colonPos == std::string::npos)
    return Value::makeInt(0);
  auto dotPos = dpy.find('.', colonPos);
  if (dotPos == std::string::npos)
    return Value::makeInt(0);
  std::string numStr = dpy.substr(colonPos + 1, dotPos - colonPos - 1);
  try {
    return Value::makeInt(std::stoi(numStr));
  } catch (...) {
    return Value::makeInt(0);
  }
}


Value
DisplayBridge::handleMonitorsResolution(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->vm)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  auto monitors = ::havel::DisplayManager::GetMonitors();
  auto arr = vm->createHostArray();
  auto arrGuard = vm->makeRoot(Value::makeArrayId(arr.id));
  for (const auto &mon : monitors) {
    std::string res = std::to_string(mon.width) + "x" +
                      std::to_string(mon.height);
    auto ref = vm->createRuntimeString(std::move(res));
    vm->pushHostArrayValue(arr, Value::makeStringId(ref.id));
  }
  return Value::makeArrayId(arr.id);
}

// ============================================================================
// BrightnessBridge Implementation
// ============================================================================

} // namespace havel::compiler
