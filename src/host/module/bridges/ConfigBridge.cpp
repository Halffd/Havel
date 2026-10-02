// ConfigBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
void ConfigBridge::install(PipelineOptions &options) {
  options.host_functions["config.get"] = [ctx = ctx_](const auto &args) {
    return handleGet(args, ctx);
  };
  options.host_functions["config.set"] = [ctx = ctx_](const auto &args) {
    return handleSet(args, ctx);
  };
  options.host_functions["config.save"] = [ctx = ctx_](const auto &args) {
    return handleSave(args, ctx);
  };
}


Value ConfigBridge::handleGet(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("config.get() requires key and default value");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error("config.get() key must be a string");
  }

  std::string key = strVal(args[0], ctx ? ctx->vm : nullptr);
  auto &config = ::havel::Configs::Get();

  // Return value based on default type
  if (args[1].isStringValId()) {
    std::string def = strVal(args[1], ctx ? ctx->vm : nullptr);
    if (!ctx || !ctx->vm) {
      return Value::makeNull();
    }
    auto *vm = static_cast<VM *>(ctx->vm);
    std::string value = config.Get(key, def);
    auto outRef = vm->getHeap().allocateString(value);
    return Value::makeStringId(outRef.id);
  } else if (args[1].isInt()) {
    int64_t def = args[1].asInt();
    return Value::makeInt(config.Get(key, def));
  } else if (args[1].isDouble()) {
    double def = args[1].asDouble();
    return Value::makeDouble(config.Get(key, def));
  } else if (args[1].isBool()) {
    bool def = args[1].asBool();
    return Value::makeBool(config.Get(key, def));
  }

  return Value::makeNull();
}


Value ConfigBridge::handleSet(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("config.set() requires key and value");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error("config.set() key must be a string");
  }

  std::string key = strVal(args[0], ctx ? ctx->vm : nullptr);
  auto &config = ::havel::Configs::Get();

  bool save = (args.size() > 2 && args[2].isBool() &&
               args[2].asBool());

  if (args[1].isStringValId()) {
    config.Set(key, strVal(args[1], ctx ? ctx->vm : nullptr), save);
  } else if (args[1].isInt()) {
    config.Set(key, args[1].asInt(), save);
  } else if (args[1].isDouble()) {
    config.Set(key, args[1].asDouble(), save);
  } else if (args[1].isBool()) {
    config.Set(key, args[1].asBool(), save);
  }

  return Value::makeBool(true);
}


Value ConfigBridge::handleSave(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto &config = ::havel::Configs::Get();
  config.Save();
  return Value::makeBool(true);
}

// ============================================================================
// ModeBridge Implementation
// ============================================================================

namespace {
struct SimpleModeState {
    std::string current;
    std::string previous;

    // Registered mode definitions, keyed by name. Each carries compiled
    // callback values (function object ids or closures) for condition,
    // enter/exit and transition hooks so mode.set can fire them on
    // transitions. Function ids live in VM-owned tables and closures are
    // rooted by the compiler, so Values here are lifetime-safe.
    struct Def {
        int priority = 0;
        static Value none() { return Value::makeNull(); }
        Value condition = none();
        Value enter = none();
        Value exit = none();
        std::string onEnterFromMode;
        Value onEnterFrom = none();
        std::string onExitToMode;
        Value onExitTo = none();
    };
    std::unordered_map<std::string, Def> defs;

    // Deliberately per-instance state. If an embedder creates two VM
    // instances (havel run + embedded), their modes must not bleed together.
    std::deque<std::pair<int64_t, std::string>> transitions;
};
SimpleModeState &modeState() {
    static SimpleModeState state;
    return state;
}

// Invoke a callback Value (function object id, closure, or null) safely.
Value runModeCallback(VM *vm, const Value &fn, const std::vector<Value> &args) {
    if (!vm || fn.isNull()) return Value::makeNull();
    try {
        return vm->call(fn, args);
    } catch (...) {
        return Value::makeNull();
    }
}

// Fire mode transition callbacks on a mode change.
// Assign mode B while in A:
//   exit(A) first (leaving A), then onExitTo(A,B) if A.exit to B declared,
//   then enter(B), then onEnterFrom(B,A) if B.enter from A declared.
void fireModeTransition(VM *vm, const std::string &prev,
                        const std::string &next, int64_t now) {
    if (prev == next) return;
    auto &st = modeState();
    auto prevIt = st.defs.find(prev);
    auto nextIt = st.defs.find(next);
    if (prevIt != st.defs.end()) {
        runModeCallback(vm, prevIt->second.exit, {});
        if (prevIt->second.onExitToMode == next) {
            runModeCallback(
                vm, prevIt->second.onExitTo,
                {Value::makeStringId(vm->getHeap().allocateString(next).id)});
        }
    }
    if (nextIt != st.defs.end()) {
        if (nextIt->second.onEnterFromMode == prev) {
            runModeCallback(
                vm, nextIt->second.onEnterFrom,
                {Value::makeStringId(vm->getHeap().allocateString(prev).id)});
        }
        runModeCallback(vm, nextIt->second.enter, {});
    }
    st.transitions.push_back({now, prev + "->" + next});
    if (st.transitions.size() > 64) st.transitions.pop_front();
}
} // namespace

} // namespace havel::compiler
