// AppBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
void AppBridge::install(PipelineOptions &options) {
  options.host_functions["app.getName"] = [ctx = ctx_](const auto &args) {
    return handleAppGetName(args, ctx);
  };
  options.host_functions["app.getVersion"] = [ctx = ctx_](const auto &args) {
    return handleAppGetVersion(args, ctx);
  };
  options.host_functions["app.getOS"] = [ctx = ctx_](const auto &args) {
    return handleAppGetOS(args, ctx);
  };
  options.host_functions["app.getHostname"] = [ctx = ctx_](const auto &args) {
    return handleAppGetHostname(args, ctx);
  };
  options.host_functions["app.getUsername"] = [ctx = ctx_](const auto &args) {
    return handleAppGetUsername(args, ctx);
  };
  options.host_functions["app.getHomeDir"] = [ctx = ctx_](const auto &args) {
    return handleAppGetHomeDir(args, ctx);
  };
  options.host_functions["app.getCpuCores"] = [ctx = ctx_](const auto &args) {
    return handleAppGetCpuCores(args, ctx);
  };
  options.host_functions["app.getEnv"] = [ctx = ctx_](const auto &args) {
    return handleAppGetEnv(args, ctx);
  };
  options.host_functions["app.setEnv"] = [ctx = ctx_](const auto &args) {
    return handleAppSetEnv(args, ctx);
  };
  options.host_functions["app.openUrl"] = [ctx = ctx_](const auto &args) {
    return handleAppOpenUrl(args, ctx);
  };
    options.host_functions["app.exit"] = [ctx = ctx_](const auto &args) {
        ctx->vm->exit_requested_.store(true);
        ctx->vm->exit_code_.store(0);
        return Value();
    };
    options.host_functions["app.restart"] = [ctx = ctx_](const auto &args) {
        ctx->vm->exit_requested_.store(true);
        ctx->vm->exit_code_.store(42);
        return Value();
    };
}


Value
AppBridge::handleAppGetName(const std::vector<Value> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->vm) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  ::havel::host::AppService app;
  auto outRef = vm->getHeap().allocateString(app.getAppName());
  return Value::makeStringId(outRef.id);
}


Value
AppBridge::handleAppGetVersion(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->vm) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  ::havel::host::AppService app;
  auto outRef = vm->getHeap().allocateString(app.getAppVersion());
  return Value::makeStringId(outRef.id);
}


Value AppBridge::handleAppGetOS(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->vm) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  ::havel::host::AppService app;
  auto outRef = vm->getHeap().allocateString(app.getOS());
  return Value::makeStringId(outRef.id);
}


Value
AppBridge::handleAppGetHostname(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->vm) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  ::havel::host::AppService app;
  auto outRef = vm->getHeap().allocateString(app.getHostname());
  return Value::makeStringId(outRef.id);
}


Value
AppBridge::handleAppGetUsername(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->vm) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  ::havel::host::AppService app;
  auto outRef = vm->getHeap().allocateString(app.getUsername());
  return Value::makeStringId(outRef.id);
}


Value
AppBridge::handleAppGetHomeDir(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->vm) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  ::havel::host::AppService app;
  auto outRef = vm->getHeap().allocateString(app.getHomeDir());
  return Value::makeStringId(outRef.id);
}


Value
AppBridge::handleAppGetCpuCores(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AppService app;
  return Value::makeInt(static_cast<int64_t>(app.getCpuCores()));
}


Value AppBridge::handleAppGetEnv(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("app.getEnv() requires a variable name");
  }
  if (!ctx || !ctx->vm) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string name;
  if (args[0].isStringValId() || args[0].isStringId()) {
    name = vm->resolveStringKey(args[0]);
  } else {
    throw std::runtime_error("app.getEnv() requires a string");
  }
  ::havel::host::AppService app;
  std::string value = app.getEnv(name);
  auto outRef = vm->getHeap().allocateString(value);
  return Value::makeStringId(outRef.id);
}


Value AppBridge::handleAppSetEnv(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (args.size() < 2) {
    throw std::runtime_error("app.setEnv() requires name and value");
  }
  if (!ctx || !ctx->vm) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string name;
  if (args[0].isStringValId() || args[0].isStringId()) {
    name = vm->resolveStringKey(args[0]);
  } else {
    throw std::runtime_error("app.setEnv() requires a string name");
  }
  std::string value;
  if (args[1].isStringValId() || args[1].isStringId()) {
    value = vm->resolveStringKey(args[1]);
  } else {
    throw std::runtime_error("app.setEnv() requires a string value");
  }
  ::havel::host::AppService app;
  return Value::makeBool(app.setEnv(name, value));
}


Value
AppBridge::handleAppOpenUrl(const std::vector<Value> &args,
                            const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("app.openUrl() requires a URL");
  }
  if (!ctx || !ctx->vm) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string url;
  if (args[0].isStringValId() || args[0].isStringId()) {
    url = vm->resolveStringKey(args[0]);
  } else {
    throw std::runtime_error("app.openUrl() requires a string URL");
  }
  ::havel::host::AppService app;
  return Value::makeBool(app.openUrl(url));
}

} // namespace havel::compiler
