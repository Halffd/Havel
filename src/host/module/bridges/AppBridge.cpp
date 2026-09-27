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
  (void)ctx;
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app;
  return Value::makeNull();
}


Value
AppBridge::handleAppGetVersion(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app;
  return Value::makeNull();
}


Value AppBridge::handleAppGetOS(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app;
  return Value::makeNull();
}


Value
AppBridge::handleAppGetHostname(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app;
  return Value::makeNull();
}


Value
AppBridge::handleAppGetUsername(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app;
  return Value::makeNull();
}


Value
AppBridge::handleAppGetHomeDir(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app;
  return Value::makeNull();
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
  const std::string *name = nullptr;
  if (!name) {
    throw std::runtime_error("app.getEnv() requires a string");
  }
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app; (void)name;
  return Value::makeNull();
}


Value AppBridge::handleAppSetEnv(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (args.size() < 2) {
    throw std::runtime_error("app.setEnv() requires name and value");
  }
  const std::string *name = nullptr;
  const std::string *value = nullptr;
  if (!name || !value) {
    throw std::runtime_error("app.setEnv() requires string arguments");
  }
  ::havel::host::AppService app;
  // TODO: bool return - for now return null
  (void)app; (void)name; (void)value;
  return Value::makeNull();
}


Value
AppBridge::handleAppOpenUrl(const std::vector<Value> &args,
                            const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("app.openUrl() requires a URL");
  }
  const std::string *url = nullptr;
  if (!url) {
    throw std::runtime_error("app.openUrl() requires a string URL");
  }
  ::havel::host::AppService app;
  // TODO: bool return - for now return null
  (void)app; (void)url;
  return Value::makeNull();
}

} // namespace havel::compiler
