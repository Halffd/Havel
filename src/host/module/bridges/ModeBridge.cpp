// ModeBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {

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

void ModeBridge::install(PipelineOptions &options) {
  // mode() - returns current mode name string
  // Does NOT set globals["mode"] — would shadow host function ref,
  // breaking mode.set / mode.current CALL_METHOD dispatch.
  // Reactive when blocks use mode() and LOAD_GLOBAL tracks "mode"
  // via trackGlobalAccess even for host function lookups.
  options.host_functions["mode"] = [ctx = ctx_](const auto &args) {
    (void)args;
    if (!ctx || !ctx->vm) return Value::makeNull();
    auto *vm = static_cast<VM *>(ctx->vm);
    std::string cur = modeState().current;
    if (cur.empty()) cur = "default";
    auto ref = vm->getHeap().allocateString(cur);
    return Value::makeStringId(ref.id);
  };
  options.host_functions["mode.register"] = [ctx = ctx_](const auto &args) {
    return handleRegister(args, ctx);
  };
  options.host_functions["mode.current"] = [ctx = ctx_](const auto &args) {
    return handleGetCurrent(args, ctx);
  };
  options.host_functions["mode.set"] = [ctx = ctx_](const auto &args) {
    return handleSet(args, ctx);
  };
  options.host_functions["mode.previous"] = [ctx = ctx_](const auto &args) {
    return handleGetPrevious(args, ctx);
  };
  options.host_functions["mode.list"] = [ctx = ctx_](const auto &args) {
    return handleList(args, ctx);
  };
  options.host_functions["mode.time"] = [ctx = ctx_](const auto &args) {
    return handleTime(args, ctx);
  };
    options.host_functions["mode.transitions"] = [ctx = ctx_](const auto &args) {
        return handleTransitions(args, ctx);
    };
}


Value ModeBridge::handleRegister(const std::vector<Value> &args,
                                      const HostContext *ctx) {
    auto *vm = static_cast<VM *>(ctx ? ctx->vm : nullptr);
    if (!vm || args.size() < 9) return Value::makeBool(false);
    auto &st = modeState();
    SimpleModeState::Def def;
    std::string name = vm->resolveStringKey(args[0]);
    if (args[1].isInt()) def.priority = static_cast<int>(args[1].asInt());
    def.condition = args[2];
    def.enter = args[3];
    def.exit = args[4];
    if (!args[5].isNull()) def.onEnterFromMode = vm->resolveStringKey(args[5]);
    def.onEnterFrom = args[6];
    if (!args[7].isNull()) def.onExitToMode = vm->resolveStringKey(args[7]);
    def.onExitTo = args[8];
    st.defs[name] = std::move(def);
    return Value::makeBool(true);
}


Value
ModeBridge::handleGetCurrent(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->vm) return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string cur = modeState().current;
  if (cur.empty()) cur = "default";
  auto ref = vm->getHeap().allocateString(cur);
  return Value::makeStringId(ref.id);
}


Value ModeBridge::handleSet(const std::vector<Value> &args,
                            const HostContext *ctx) {
  if (args.empty() || (!args[0].isStringValId() && !args[0].isStringId())) {
    throw std::runtime_error("mode.set() requires a mode name string");
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string modeName = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
  auto &st = modeState();
  if (modeName == st.current) return Value::makeBool(true);
  std::string prev = st.current;
  if (prev.empty()) prev = "default";
  st.previous = st.current;
  st.current = modeName;
  if (vm) {
    fireModeTransition(vm, prev, modeName,
                       static_cast<int64_t>(std::time(nullptr)));
    vm->emitVariableChanged("mode");
  }
  if (ctx && ctx->hotkeyManager) {
    ctx->hotkeyManager->setMode(modeName);
  }
  return Value::makeBool(true);
}


Value
ModeBridge::handleGetPrevious(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->vm) return Value::makeNull();
  auto ref = static_cast<VM *>(ctx->vm)->getHeap().allocateString(modeState().previous);
  return Value::makeStringId(ref.id);
}


Value
ModeBridge::handleList(const std::vector<Value> &,
                       const HostContext *ctx) {
  auto *vm = static_cast<VM *>(ctx ? ctx->vm : nullptr);
  if (!vm) return Value::makeNull();
  auto &st = modeState();
  auto arr = vm->createHostArray();
  auto arrGuard = vm->makeRoot(Value::makeArrayId(arr.id));
  std::vector<std::string> names;
  names.reserve(st.defs.size());
  for (const auto &kv : st.defs) names.push_back(kv.first);
  std::sort(names.begin(), names.end());
  for (const auto &n : names) {
    vm->pushHostArrayValue(
        arr, Value::makeStringId(vm->getHeap().allocateString(n).id));
  }
  return Value::makeArrayId(arr.id);
}


Value
ModeBridge::handleTime(const std::vector<Value> &,
                       const HostContext *ctx) {
  auto *vm = static_cast<VM *>(ctx ? ctx->vm : nullptr);
  if (!vm) return Value::makeInt(0);
  auto &st = modeState();
  if (st.transitions.empty()) return Value::makeInt(0);
  return Value::makeInt(static_cast<int64_t>(
      std::time(nullptr) - st.transitions.back().first));
}


Value
ModeBridge::handleTransitions(const std::vector<Value> &,
                              const HostContext *ctx) {
  auto *vm = static_cast<VM *>(ctx ? ctx->vm : nullptr);
  if (!vm) return Value::makeInt(0);
  return Value::makeInt(static_cast<int64_t>(modeState().transitions.size()));
}

// ============================================================================
// TimerBridge Implementation
// ============================================================================

} // namespace havel::compiler
