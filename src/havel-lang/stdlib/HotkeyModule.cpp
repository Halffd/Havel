#ifndef HAVEL_PURE_VM
#include "HotkeyModule.hpp"
#include "core/hotkey/HotkeyManager.hpp"
#include "havel-lang/runtime/HostContext.hpp"
#include "havel-lang/runtime/concurrency/Scheduler.hpp"
#include "../../utils/Logger.hpp"
#include <mutex>
#include <unordered_map>
#include <chrono>

using havel::compiler::Value;
using havel::compiler::CallbackId;
using havel::compiler::ObjectRef;
using havel::compiler::VM;
using havel::compiler::VMApi;
using havel::compiler::Scheduler;
using havel::compiler::HotkeyPolicy;

namespace havel::stdlib {

struct HotkeyContextData {
    std::string id;
    std::string alias;
    std::string key;
    std::string condition;
    std::string info;
    std::string combo;
    std::string modifiers;
    std::string state;
    int64_t addedAt;
    CallbackId callback;
    bool enabled;
    int64_t triggerCount = 0;
    int64_t lastTriggeredAt = 0;

    HotkeyContextData(const std::string &id_, const std::string &alias_,
                      const std::string &key_, const std::string &condition_,
                      const std::string &info_, CallbackId callback_,
                      bool enabled_ = true)
        : id(id_), alias(alias_), key(key_), condition(condition_), info(info_),
          combo(""), modifiers(""), state(enabled_ ? "enabled" : "disabled"),
          addedAt(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count()),
          callback(callback_), enabled(enabled_) {}
};

static std::unordered_map<std::string, std::unique_ptr<HotkeyContextData>>
    g_hotkeyContexts;
static std::mutex g_hotkeyContextsMutex;

static HotkeyContextData *getHotkeyContextData(const std::string &hotkeyId) {
    std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
    auto it = g_hotkeyContexts.find(hotkeyId);
    return (it != g_hotkeyContexts.end()) ? it->second.get() : nullptr;
}

static HotkeyContextData *getHotkeyContextDataMutable(const std::string &hotkeyId) {
    std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
    auto it = g_hotkeyContexts.find(hotkeyId);
    return (it != g_hotkeyContexts.end()) ? it->second.get() : nullptr;
}

static std::string resolveHotkeyId(VM &vm, const Value &idValue) {
    return vm.resolveStringKey(idValue);
}

static std::string extractHotkeyId(VM &vm, const Value &obj) {
    if (!obj.isObjectId()) return "";
    auto objRef = ObjectRef{obj.asObjectId(), true};
    auto idValue = vm.getHostObjectField(objRef, "id");
    if (idValue.isNull()) return "";
    return resolveHotkeyId(vm, idValue);
}

static Value createHotkeyContextObject(VM *vm, const std::string &hotkeyId,
                                        const std::string &alias,
                                        const std::string &key,
                                        const std::string &condition,
                                        const std::string &info,
                                        CallbackId callback,
                                        bool enabled = true) {
    auto contextObj = vm->createHostObject();
    auto guard = vm->makeRoot(Value::makeObjectId(contextObj.id));

    {
        std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
        g_hotkeyContexts[hotkeyId] = std::make_unique<HotkeyContextData>(
            hotkeyId, alias, key, condition, info, callback, enabled);
    }

    auto idStr = vm->createRuntimeString(hotkeyId);
    auto aliasStr = vm->createRuntimeString(alias);
    auto keyStr = vm->createRuntimeString(key);
    auto condStr = vm->createRuntimeString(condition);
    auto infoStr = vm->createRuntimeString(info);

    vm->setHostObjectField(contextObj, "id", Value::makeStringId(idStr.id));
    vm->setHostObjectField(contextObj, "alias", Value::makeStringId(aliasStr.id));
    vm->setHostObjectField(contextObj, "key", Value::makeStringId(keyStr.id));
    vm->setHostObjectField(contextObj, "condition", Value::makeStringId(condStr.id));
    vm->setHostObjectField(contextObj, "info", Value::makeStringId(infoStr.id));
    vm->setHostObjectField(contextObj, "enabled", Value::makeBool(enabled));

    auto typeStr = vm->createRuntimeString("Hotkey");
    vm->setHostObjectField(contextObj, "__class", Value::makeStringId(typeStr.id));

    return Value::makeObjectId(contextObj.id);
}

static HotkeyPolicy parsePolicy(const std::string &s) {
    if (s == "replace") return HotkeyPolicy::Replace;
    if (s == "queue") return HotkeyPolicy::Queue;
    if (s == "coalesce") return HotkeyPolicy::Coalesce;
    return HotkeyPolicy::Queue;
}

static const char *policyToString(HotkeyPolicy p) {
    switch (p) {
        case HotkeyPolicy::Replace: return "replace";
        case HotkeyPolicy::Queue: return "queue";
        case HotkeyPolicy::Coalesce: return "coalesce";
        default: return "drop";
    }
}

void registerHotkeyModule(const VMApi &api) {
    VM &vm = api.vm();

    // Property getters
    api.registerPrototypeMethod("Hotkey", "id", 1, [&vm](const std::vector<Value> &args) -> Value {
        if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
        auto objRef = ObjectRef{args[0].asObjectId(), true};
        return vm.getHostObjectField(objRef, "id");
    });

    api.registerPrototypeMethod("Hotkey", "alias", 1, [&vm](const std::vector<Value> &args) -> Value {
        if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
        auto objRef = ObjectRef{args[0].asObjectId(), true};
        return vm.getHostObjectField(objRef, "alias");
    });

    api.registerPrototypeMethod("Hotkey", "key", 1, [&vm](const std::vector<Value> &args) -> Value {
        if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
        auto objRef = ObjectRef{args[0].asObjectId(), true};
        return vm.getHostObjectField(objRef, "key");
    });

    api.registerPrototypeMethod("Hotkey", "condition", 1, [&vm](const std::vector<Value> &args) -> Value {
        if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
        auto objRef = ObjectRef{args[0].asObjectId(), true};
        return vm.getHostObjectField(objRef, "condition");
    });

    api.registerPrototypeMethod("Hotkey", "info", 1, [&vm](const std::vector<Value> &args) -> Value {
        if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
        auto objRef = ObjectRef{args[0].asObjectId(), true};
        return vm.getHostObjectField(objRef, "info");
    });

    api.registerPrototypeMethod("Hotkey", "callback", 1, [&vm](const std::vector<Value> &args) -> Value {
        return Value::makeNull();
    });

    api.registerPrototypeMethod("Hotkey", "state", 1, [&vm](const std::vector<Value> &args) -> Value {
        auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
        if (hotkeyId.empty()) return Value::makeNull();
        auto *ctx = getHotkeyContextData(hotkeyId);
        if (!ctx) return Value::makeNull();
        auto strRef = vm.createRuntimeString(ctx->state);
        return Value::makeStringId(strRef.id);
    });

    api.registerPrototypeMethod("Hotkey", "modifiers", 1, [&vm](const std::vector<Value> &args) -> Value {
        auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
        if (hotkeyId.empty()) return Value::makeNull();
        auto *ctx = getHotkeyContextData(hotkeyId);
        if (!ctx) return Value::makeNull();
        auto strRef = vm.createRuntimeString(ctx->modifiers);
        return Value::makeStringId(strRef.id);
    });

    api.registerPrototypeMethod("Hotkey", "combo", 1, [&vm](const std::vector<Value> &args) -> Value {
        auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
        if (hotkeyId.empty()) return Value::makeNull();
        auto *ctx = getHotkeyContextData(hotkeyId);
        if (!ctx) return Value::makeNull();
        auto strRef = vm.createRuntimeString(ctx->combo);
        return Value::makeStringId(strRef.id);
    });

    api.registerPrototypeMethod("Hotkey", "addedAt", 1, [&vm](const std::vector<Value> &args) -> Value {
        auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
        if (hotkeyId.empty()) return Value::makeNull();
        auto *ctx = getHotkeyContextData(hotkeyId);
        if (!ctx) return Value::makeNull();
        return Value::makeInt(ctx->addedAt);
    });

    // count() - number of times this hotkey has been triggered
    api.registerPrototypeMethod("Hotkey", "count", 1, [&vm](const std::vector<Value> &args) -> Value {
        auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
        if (hotkeyId.empty()) return Value::makeInt(0);
        auto *ctx = getHotkeyContextData(hotkeyId);
        if (!ctx) return Value::makeInt(0);
        return Value::makeInt(ctx->triggerCount);
    });

    // lastTriggeredAt() - epoch ms of last trigger, 0 if never triggered
    api.registerPrototypeMethod("Hotkey", "lastTriggeredAt", 1, [&vm](const std::vector<Value> &args) -> Value {
        auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
        if (hotkeyId.empty()) return Value::makeInt(0);
        auto *ctx = getHotkeyContextData(hotkeyId);
        if (!ctx) return Value::makeInt(0);
        return Value::makeInt(ctx->lastTriggeredAt);
    });

    // isActive() - true if the persistent goroutine is currently running or pending
    api.registerPrototypeMethod("Hotkey", "isActive", 1, [&vm](const std::vector<Value> &args) -> Value {
        auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
        if (hotkeyId.empty()) return Value::makeBool(false);
        auto *ctx = getHotkeyContextData(hotkeyId);
        if (!ctx) return Value::makeBool(false);
        auto *sched = vm.getScheduler();
        if (!sched) return Value::makeBool(false);
        auto *g = sched->getHotkeyByAlias(ctx->alias);
        if (!g) return Value::makeBool(false);
        bool active = (g->state == Scheduler::GoroutineState::Running ||
                       g->state == Scheduler::GoroutineState::Runnable ||
                       g->state == Scheduler::GoroutineState::Created);
        return Value::makeBool(active);
    });

    // getPolicy() - returns the current hotkey policy as a string
    api.registerPrototypeMethod("Hotkey", "getPolicy", 1, [&vm](const std::vector<Value> &args) -> Value {
        auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
        if (hotkeyId.empty()) return Value::makeNull();
        auto *ctx = getHotkeyContextData(hotkeyId);
        if (!ctx) return Value::makeNull();
        auto *sched = vm.getScheduler();
        HotkeyPolicy policy = HotkeyPolicy::Drop;
        if (sched) {
            auto *g = sched->getHotkeyByAlias(ctx->alias);
            if (g) policy = g->hotkey_policy;
        }
        auto strRef = vm.createRuntimeString(policyToString(policy));
        return Value::makeStringId(strRef.id);
    });

    // enable() - enable this hotkey
    api.registerPrototypeMethod("Hotkey", "enable", 1, [&vm](const std::vector<Value> &args) -> Value {
        if (args.empty() || !args[0].isObjectId()) return Value::makeBool(false);
        auto objRef = ObjectRef{args[0].asObjectId(), true};
        auto idValue = vm.getHostObjectField(objRef, "id");
        if (idValue.isNull()) return Value::makeBool(false);
        auto hotkeyId = resolveHotkeyId(vm, idValue);
        auto *ctx = getHotkeyContextDataMutable(hotkeyId);
        if (!ctx) return Value::makeBool(false);

	auto *hostCtx = vm.hostContext();
	if (hostCtx && hostCtx->hotkeyManager) {
		hostCtx->hotkeyManager->EnableHotkey(ctx->alias);
		ctx->enabled = true;
		ctx->state = "enabled";
		vm.setHostObjectField(objRef, "enabled", Value::makeBool(true));
		auto strRef = vm.createRuntimeString("enabled");
		vm.setHostObjectField(objRef, "state", Value::makeStringId(strRef.id));
		return Value::makeBool(true);
	}
	return Value::makeBool(false);
});

// disable() - disable this hotkey
api.registerPrototypeMethod("Hotkey", "disable", 1, [&vm](const std::vector<Value> &args) -> Value {
	if (args.empty() || !args[0].isObjectId()) return Value::makeBool(false);
	auto objRef = ObjectRef{args[0].asObjectId(), true};
	auto idValue = vm.getHostObjectField(objRef, "id");
	if (idValue.isNull()) return Value::makeBool(false);
	auto hotkeyId = resolveHotkeyId(vm, idValue);
	auto *ctx = getHotkeyContextDataMutable(hotkeyId);
	if (!ctx) return Value::makeBool(false);

	auto *hostCtx = vm.hostContext();
	if (hostCtx && hostCtx->hotkeyManager) {
		hostCtx->hotkeyManager->DisableHotkey(ctx->alias);
		ctx->enabled = false;
		ctx->state = "disabled";
		vm.setHostObjectField(objRef, "enabled", Value::makeBool(false));
		auto strRef = vm.createRuntimeString("disabled");
		vm.setHostObjectField(objRef, "state", Value::makeStringId(strRef.id));
		return Value::makeBool(true);
	}
	return Value::makeBool(false);
});

// toggle() - toggle enabled/disabled
api.registerPrototypeMethod("Hotkey", "toggle", 1, [&vm](const std::vector<Value> &args) -> Value {
	if (args.empty() || !args[0].isObjectId()) return Value::makeBool(false);
	auto objRef = ObjectRef{args[0].asObjectId(), true};
	auto idValue = vm.getHostObjectField(objRef, "id");
	if (idValue.isNull()) return Value::makeBool(false);
	auto hotkeyId = resolveHotkeyId(vm, idValue);
	auto *ctx = getHotkeyContextDataMutable(hotkeyId);
	if (!ctx) return Value::makeBool(false);

	auto *hostCtx = vm.hostContext();
	if (hostCtx && hostCtx->hotkeyManager) {
		bool newState = !ctx->enabled;
		if (newState) {
			hostCtx->hotkeyManager->EnableHotkey(ctx->alias);
		} else {
			hostCtx->hotkeyManager->DisableHotkey(ctx->alias);
		}
            ctx->enabled = newState;
            ctx->state = newState ? "enabled" : "disabled";
            vm.setHostObjectField(objRef, "enabled", Value::makeBool(newState));
            auto strRef = vm.createRuntimeString(ctx->state);
            vm.setHostObjectField(objRef, "state", Value::makeStringId(strRef.id));
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    });

// remove() - remove this hotkey
api.registerPrototypeMethod("Hotkey", "remove", 1, [&vm](const std::vector<Value> &args) -> Value {
	if (args.empty() || !args[0].isObjectId()) return Value::makeBool(false);
	auto objRef = ObjectRef{args[0].asObjectId(), true};
	auto idValue = vm.getHostObjectField(objRef, "id");
	if (idValue.isNull()) return Value::makeBool(false);
	auto hotkeyId = resolveHotkeyId(vm, idValue);
	auto *ctx = getHotkeyContextDataMutable(hotkeyId);

	auto *hostCtx = vm.hostContext();
	if (hostCtx && hostCtx->hotkeyManager) {
		std::string alias = ctx ? ctx->alias : hotkeyId;
		bool success = hostCtx->hotkeyManager->RemoveHotkey(alias);
		if (success) {
                std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
                g_hotkeyContexts.erase(hotkeyId);
            }
            return Value::makeBool(success);
        }
        return Value::makeBool(false);
    });

    // setPolicy(policy_str) - change the hotkey policy at runtime
    api.registerPrototypeMethod("Hotkey", "setPolicy", 2, [&vm](const std::vector<Value> &args) -> Value {
        if (args.size() < 2 || !args[0].isObjectId()) return Value::makeBool(false);
        auto hotkeyId = extractHotkeyId(vm, args[0]);
        if (hotkeyId.empty()) return Value::makeBool(false);
        auto *ctx = getHotkeyContextData(hotkeyId);
        if (!ctx) return Value::makeBool(false);

        std::string policyStr = vm.resolveStringKey(args[1]);
        HotkeyPolicy policy = parsePolicy(policyStr);

        auto *sched = vm.getScheduler();
        if (sched) {
            auto *g = sched->getHotkeyByAlias(ctx->alias);
            if (g) {
                sched->setHotkeyPolicy(g, policy);
                return Value::makeBool(true);
            }
        }
        return Value::makeBool(false);
    });

    // setAlias(alias_str) - change the alias at runtime
    api.registerPrototypeMethod("Hotkey", "setAlias", 2, [&vm](const std::vector<Value> &args) -> Value {
        if (args.size() < 2 || !args[0].isObjectId()) return Value::makeBool(false);
        auto objRef = ObjectRef{args[0].asObjectId(), true};
        auto idValue = vm.getHostObjectField(objRef, "id");
        if (idValue.isNull()) return Value::makeBool(false);
        auto hotkeyId = resolveHotkeyId(vm, idValue);
        auto *ctx = getHotkeyContextDataMutable(hotkeyId);
        if (!ctx) return Value::makeBool(false);

        std::string newAlias = vm.resolveStringKey(args[1]);
        std::string oldAlias = ctx->alias;

        // Update the scheduler goroutine alias if found
        auto *sched = vm.getScheduler();
        if (sched) {
            auto *g = sched->getHotkeyByAlias(oldAlias);
            if (g) {
                g->hotkey_alias = newAlias;
            }
        }

        // Update context data
        ctx->alias = newAlias;

        // Update the object field
        auto aliasStr = vm.createRuntimeString(newAlias);
        vm.setHostObjectField(objRef, "alias", Value::makeStringId(aliasStr.id));

        return Value::makeBool(true);
    });

    // setEnabled(bool) - explicit enable/disable by boolean argument
    api.registerPrototypeMethod("Hotkey", "setEnabled", 2, [&vm](const std::vector<Value> &args) -> Value {
        if (args.size() < 2 || !args[0].isObjectId()) return Value::makeBool(false);
        auto objRef = ObjectRef{args[0].asObjectId(), true};
        auto idValue = vm.getHostObjectField(objRef, "id");
        if (idValue.isNull()) return Value::makeBool(false);
        auto hotkeyId = resolveHotkeyId(vm, idValue);
        auto *ctx = getHotkeyContextDataMutable(hotkeyId);
        if (!ctx) return Value::makeBool(false);

	bool enable = vm.toBoolPublic(args[1]);
	auto *hostCtx = vm.hostContext();
	if (hostCtx && hostCtx->hotkeyManager) {
		if (enable) {
			hostCtx->hotkeyManager->EnableHotkey(ctx->alias);
		} else {
			hostCtx->hotkeyManager->DisableHotkey(ctx->alias);
		}
            ctx->enabled = enable;
            ctx->state = enable ? "enabled" : "disabled";
            vm.setHostObjectField(objRef, "enabled", Value::makeBool(enable));
            auto strRef = vm.createRuntimeString(ctx->state);
            vm.setHostObjectField(objRef, "state", Value::makeStringId(strRef.id));
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    });

// removeAll() - remove all hotkeys
api.registerPrototypeMethod("Hotkey", "removeAll", 1, [&vm](const std::vector<Value> &args) -> Value {
	auto *hostCtx = vm.hostContext();
	if (!hostCtx || !hostCtx->hotkeyManager) return Value::makeInt(0);

	std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
	size_t count = g_hotkeyContexts.size();
	for (auto &[id, data] : g_hotkeyContexts) {
		hostCtx->hotkeyManager->RemoveHotkey(data ? data->alias : id);
	}
	g_hotkeyContexts.clear();
	return Value::makeInt(static_cast<int64_t>(count));
});

// clearAll() - alias for removeAll
api.registerPrototypeMethod("Hotkey", "clearAll", 1, [&vm](const std::vector<Value> &args) -> Value {
	auto *hostCtx = vm.hostContext();
	if (!hostCtx || !hostCtx->hotkeyManager) return Value::makeInt(0);

	std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
	size_t count = g_hotkeyContexts.size();
	for (auto &[id, data] : g_hotkeyContexts) {
		hostCtx->hotkeyManager->RemoveHotkey(data ? data->alias : id);
	}
	g_hotkeyContexts.clear();
	return Value::makeInt(static_cast<int64_t>(count));
});

  // ===== New Prototype Methods =====

  // age() - milliseconds since this hotkey was added
  api.registerPrototypeMethod("Hotkey", "age", 1, [&vm](const std::vector<Value> &args) -> Value {
    auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
    if (hotkeyId.empty()) return Value::makeInt(0);
    auto *ctx = getHotkeyContextData(hotkeyId);
    if (!ctx) return Value::makeInt(0);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
    return Value::makeInt(now - ctx->addedAt);
  });

  // elapsed() - milliseconds since last trigger, -1 if never triggered
  api.registerPrototypeMethod("Hotkey", "elapsed", 1, [&vm](const std::vector<Value> &args) -> Value {
    auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
    if (hotkeyId.empty()) return Value::makeInt(-1);
    auto *ctx = getHotkeyContextData(hotkeyId);
    if (!ctx) return Value::makeInt(-1);
    if (ctx->lastTriggeredAt == 0) return Value::makeInt(-1);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
    return Value::makeInt(now - ctx->lastTriggeredAt);
  });

  // resetCount() - reset the trigger counter to 0
  api.registerPrototypeMethod("Hotkey", "resetCount", 1, [&vm](const std::vector<Value> &args) -> Value {
    auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
    if (hotkeyId.empty()) return Value::makeBool(false);
    auto *ctx = getHotkeyContextDataMutable(hotkeyId);
    if (!ctx) return Value::makeBool(false);
    ctx->triggerCount = 0;
    return Value::makeBool(true);
  });

  // toString() - human-readable summary of this hotkey
  api.registerPrototypeMethod("Hotkey", "toString", 1, [&vm](const std::vector<Value> &args) -> Value {
    auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
    if (hotkeyId.empty()) {
      auto ref = vm.createRuntimeString("Hotkey<unknown>");
      return Value::makeStringId(ref.id);
    }
    auto *ctx = getHotkeyContextData(hotkeyId);
    if (!ctx) {
      auto ref = vm.createRuntimeString("Hotkey<unknown>");
      return Value::makeStringId(ref.id);
    }
    std::string s = "Hotkey<" + ctx->alias + " key=" + ctx->key +
                    " policy=" + std::string(policyToString(
                        [&]() -> HotkeyPolicy {
                          auto *sched = vm.getScheduler();
                          if (!sched) return HotkeyPolicy::Drop;
                          auto *g = sched->getHotkeyByAlias(ctx->alias);
                          if (!g) return HotkeyPolicy::Drop;
                          return g->hotkey_policy;
                        }())) +
                    " enabled=" + (ctx->enabled ? "true" : "false") +
                    " count=" + std::to_string(ctx->triggerCount) + ">";
    auto ref = vm.createRuntimeString(s);
    return Value::makeStringId(ref.id);
  });

  // equals(other) - compare two hotkey context objects by id
  api.registerPrototypeMethod("Hotkey", "equals", 2, [&vm](const std::vector<Value> &args) -> Value {
    if (args.size() < 2 || !args[0].isObjectId() || !args[1].isObjectId())
      return Value::makeBool(false);
    auto objRef0 = ObjectRef{args[0].asObjectId(), true};
    auto objRef1 = ObjectRef{args[1].asObjectId(), true};
    auto id0 = vm.getHostObjectField(objRef0, "id");
    auto id1 = vm.getHostObjectField(objRef1, "id");
    if (id0.isNull() || id1.isNull()) return Value::makeBool(false);
    std::string sid0 = resolveHotkeyId(vm, id0);
    std::string sid1 = resolveHotkeyId(vm, id1);
    return Value::makeBool(sid0 == sid1);
  });

  // isEnabled() - check if this hotkey is enabled (from context data)
  api.registerPrototypeMethod("Hotkey", "isEnabled", 1, [&vm](const std::vector<Value> &args) -> Value {
    if (args.empty() || !args[0].isObjectId()) return Value::makeBool(false);
    auto objRef = ObjectRef{args[0].asObjectId(), true};
    auto enabledVal = vm.getHostObjectField(objRef, "enabled");
    if (enabledVal.isBool()) return enabledVal;
    return Value::makeBool(false);
  });

  // isSuspended() - check if the persistent goroutine is currently suspended
  api.registerPrototypeMethod("Hotkey", "isSuspended", 1, [&vm](const std::vector<Value> &args) -> Value {
    auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
    if (hotkeyId.empty()) return Value::makeBool(false);
    auto *ctx = getHotkeyContextData(hotkeyId);
    if (!ctx) return Value::makeBool(false);
    auto *sched = vm.getScheduler();
    if (!sched) return Value::makeBool(false);
    auto *g = sched->getHotkeyByAlias(ctx->alias);
    if (!g) return Value::makeBool(false);
    return Value::makeBool(g->state == Scheduler::GoroutineState::Suspended);
  });

  // goroutineId() - return the scheduler goroutine ID for this hotkey, or 0
  api.registerPrototypeMethod("Hotkey", "goroutineId", 1, [&vm](const std::vector<Value> &args) -> Value {
    auto hotkeyId = extractHotkeyId(vm, args.empty() ? Value::makeNull() : args[0]);
    if (hotkeyId.empty()) return Value::makeInt(0);
    auto *ctx = getHotkeyContextData(hotkeyId);
    if (!ctx) return Value::makeInt(0);
    auto *sched = vm.getScheduler();
    if (!sched) return Value::makeInt(0);
    auto *g = sched->getHotkeyByAlias(ctx->alias);
    if (!g) return Value::makeInt(0);
    return Value::makeInt(static_cast<int64_t>(g->id));
  });

  // setKey(key_str) - update the key string on this hotkey context
  api.registerPrototypeMethod("Hotkey", "setKey", 2, [&vm](const std::vector<Value> &args) -> Value {
    if (args.size() < 2 || !args[0].isObjectId()) return Value::makeBool(false);
    auto objRef = ObjectRef{args[0].asObjectId(), true};
    auto idValue = vm.getHostObjectField(objRef, "id");
    if (idValue.isNull()) return Value::makeBool(false);
    auto hotkeyId = resolveHotkeyId(vm, idValue);
    auto *ctx = getHotkeyContextDataMutable(hotkeyId);
    if (!ctx) return Value::makeBool(false);
    std::string newKey = vm.resolveStringKey(args[1]);
    ctx->key = newKey;
    auto keyStr = vm.createRuntimeString(newKey);
    vm.setHostObjectField(objRef, "key", Value::makeStringId(keyStr.id));
    return Value::makeBool(true);
  });

  // setInfo(info_str) - update the info string on this hotkey context
  api.registerPrototypeMethod("Hotkey", "setInfo", 2, [&vm](const std::vector<Value> &args) -> Value {
    if (args.size() < 2 || !args[0].isObjectId()) return Value::makeBool(false);
    auto objRef = ObjectRef{args[0].asObjectId(), true};
    auto idValue = vm.getHostObjectField(objRef, "id");
    if (idValue.isNull()) return Value::makeBool(false);
    auto hotkeyId = resolveHotkeyId(vm, idValue);
    auto *ctx = getHotkeyContextDataMutable(hotkeyId);
    if (!ctx) return Value::makeBool(false);
    std::string newInfo = vm.resolveStringKey(args[1]);
    ctx->info = newInfo;
    auto infoStr = vm.createRuntimeString(newInfo);
    vm.setHostObjectField(objRef, "info", Value::makeStringId(infoStr.id));
    return Value::makeBool(true);
  });

  // ===== Static Utility Methods (on Hotkey global) =====

  // Hotkey.count() - total number of registered hotkey contexts
  api.registerPrototypeMethod("Hotkey", "count", 1, [&vm](const std::vector<Value> &args) -> Value {
    (void)args;
    return Value::makeInt(static_cast<int64_t>(HotkeyModule::contextCount()));
  });

  // Hotkey.findByAlias(alias) - find a hotkey context object by alias, or null
  api.registerPrototypeMethod("Hotkey", "findByAlias", 2, [&vm](const std::vector<Value> &args) -> Value {
    if (args.size() < 2) return Value::makeNull();
    std::string alias = vm.resolveStringKey(args[1]);
    std::string hotkeyId = HotkeyModule::findByAlias(alias);
    if (hotkeyId.empty()) return Value::makeNull();
    auto *ctx = getHotkeyContextData(hotkeyId);
    if (!ctx) return Value::makeNull();
    auto contextObj = vm.createHostObject();
    auto idStr = vm.createRuntimeString(ctx->id);
    auto aliasStr = vm.createRuntimeString(ctx->alias);
    auto keyStr = vm.createRuntimeString(ctx->key);
    auto condStr = vm.createRuntimeString(ctx->condition);
    auto infoStr = vm.createRuntimeString(ctx->info);
    vm.setHostObjectField(contextObj, "id", Value::makeStringId(idStr.id));
    vm.setHostObjectField(contextObj, "alias", Value::makeStringId(aliasStr.id));
    vm.setHostObjectField(contextObj, "key", Value::makeStringId(keyStr.id));
    vm.setHostObjectField(contextObj, "condition", Value::makeStringId(condStr.id));
    vm.setHostObjectField(contextObj, "info", Value::makeStringId(infoStr.id));
    vm.setHostObjectField(contextObj, "enabled", Value::makeBool(ctx->enabled));
    auto typeStr = vm.createRuntimeString("Hotkey");
    vm.setHostObjectField(contextObj, "__class", Value::makeStringId(typeStr.id));
    return Value::makeObjectId(contextObj.id);
  });

  // Hotkey.findByKey(key) - find all hotkey contexts matching a key string
  api.registerPrototypeMethod("Hotkey", "findByKey", 2, [&vm](const std::vector<Value> &args) -> Value {
    if (args.size() < 2) return Value::makeNull();
    std::string key = vm.resolveStringKey(args[1]);
    auto ids = HotkeyModule::findByKey(key);
    auto arr = vm.createHostArray();
    for (const auto &hotkeyId : ids) {
      auto *ctx = getHotkeyContextData(hotkeyId);
      if (!ctx) continue;
      auto contextObj = vm.createHostObject();
      auto idStr = vm.createRuntimeString(ctx->id);
      auto aliasStr = vm.createRuntimeString(ctx->alias);
      auto keyStr = vm.createRuntimeString(ctx->key);
      auto condStr = vm.createRuntimeString(ctx->condition);
      auto infoStr = vm.createRuntimeString(ctx->info);
      vm.setHostObjectField(contextObj, "id", Value::makeStringId(idStr.id));
      vm.setHostObjectField(contextObj, "alias", Value::makeStringId(aliasStr.id));
      vm.setHostObjectField(contextObj, "key", Value::makeStringId(keyStr.id));
      vm.setHostObjectField(contextObj, "condition", Value::makeStringId(condStr.id));
      vm.setHostObjectField(contextObj, "info", Value::makeStringId(infoStr.id));
      vm.setHostObjectField(contextObj, "enabled", Value::makeBool(ctx->enabled));
      auto typeStr = vm.createRuntimeString("Hotkey");
      vm.setHostObjectField(contextObj, "__class", Value::makeStringId(typeStr.id));
      vm.pushHostArrayValue(arr, Value::makeObjectId(contextObj.id));
    }
    return Value::makeArrayId(arr.id);
  });

  // Hotkey.all() - return array of all registered hotkey context objects
  api.registerPrototypeMethod("Hotkey", "all", 1, [&vm](const std::vector<Value> &args) -> Value {
    (void)args;
    auto ids = HotkeyModule::getAllIds();
    auto arr = vm.createHostArray();
    for (const auto &hotkeyId : ids) {
      auto *ctx = getHotkeyContextData(hotkeyId);
      if (!ctx) continue;
      auto contextObj = vm.createHostObject();
      auto idStr = vm.createRuntimeString(ctx->id);
      auto aliasStr = vm.createRuntimeString(ctx->alias);
      auto keyStr = vm.createRuntimeString(ctx->key);
      auto condStr = vm.createRuntimeString(ctx->condition);
      auto infoStr = vm.createRuntimeString(ctx->info);
      vm.setHostObjectField(contextObj, "id", Value::makeStringId(idStr.id));
      vm.setHostObjectField(contextObj, "alias", Value::makeStringId(aliasStr.id));
      vm.setHostObjectField(contextObj, "key", Value::makeStringId(keyStr.id));
      vm.setHostObjectField(contextObj, "condition", Value::makeStringId(condStr.id));
      vm.setHostObjectField(contextObj, "info", Value::makeStringId(infoStr.id));
      vm.setHostObjectField(contextObj, "enabled", Value::makeBool(ctx->enabled));
      auto typeStr = vm.createRuntimeString("Hotkey");
      vm.setHostObjectField(contextObj, "__class", Value::makeStringId(typeStr.id));
      vm.pushHostArrayValue(arr, Value::makeObjectId(contextObj.id));
    }
    return Value::makeArrayId(arr.id);
  });

  // Hotkey.activeCount() - number of persistent hotkey goroutines that are running/runnable
  api.registerPrototypeMethod("Hotkey", "activeCount", 1, [&vm](const std::vector<Value> &args) -> Value {
    (void)args;
    auto *sched = vm.getScheduler();
    if (!sched) return Value::makeInt(0);
    return Value::makeInt(static_cast<int64_t>(sched->activeHotkeyCount()));
  });

  // Hotkey.suspendedCount() - number of persistent hotkey goroutines that are suspended
  api.registerPrototypeMethod("Hotkey", "suspendedCount", 1, [&vm](const std::vector<Value> &args) -> Value {
    (void)args;
    auto *sched = vm.getScheduler();
    if (!sched) return Value::makeInt(0);
    return Value::makeInt(static_cast<int64_t>(sched->suspendedHotkeyCount()));
  });

  // Hotkey.policies() - return array of valid policy strings
  api.registerPrototypeMethod("Hotkey", "policies", 1, [&vm](const std::vector<Value> &args) -> Value {
    (void)args;
    auto arr = vm.createHostArray();
    for (const char *p : {"drop", "replace", "queue", "coalesce"}) {
      auto ref = vm.createRuntimeString(p);
      vm.pushHostArrayValue(arr, Value::makeStringId(ref.id));
    }
    return Value::makeArrayId(arr.id);
  });

  // Hotkey.aliases() - return array of all registered hotkey aliases
  api.registerPrototypeMethod("Hotkey", "aliases", 1, [&vm](const std::vector<Value> &args) -> Value {
    (void)args;
    auto *sched = vm.getScheduler();
    if (!sched) {
      auto arr = vm.createHostArray();
      return Value::makeArrayId(arr.id);
    }
    auto aliasList = sched->getHotkeyAliases();
    auto arr = vm.createHostArray();
    for (const auto &a : aliasList) {
      auto ref = vm.createRuntimeString(a);
      vm.pushHostArrayValue(arr, Value::makeStringId(ref.id));
    }
    return Value::makeArrayId(arr.id);
  });

  // Set prototype object as global "Hotkey" constructor
  auto hotkeyPrototype = api.makeObject();
  api.setGlobal("Hotkey", hotkeyPrototype);
}

Value HotkeyModule::createHotkeyContext(VM *vm, const std::string &hotkeyId,
                                         const std::string &alias,
                                         const std::string &key,
                                         const std::string &condition,
                                         const std::string &info,
                                         CallbackId callback) {
    return createHotkeyContextObject(vm, hotkeyId, alias, key, condition, info,
                                     callback);
}

// Called when a hotkey fires — increment trigger count
void HotkeyModule::recordTrigger(const std::string &hotkeyId) {
    auto *ctx = getHotkeyContextDataMutable(hotkeyId);
    if (ctx) {
        ctx->triggerCount++;
        ctx->lastTriggeredAt = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count();
    }
}

size_t HotkeyModule::contextCount() {
  std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
  return g_hotkeyContexts.size();
}

std::string HotkeyModule::findByAlias(const std::string &alias) {
  std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
  for (const auto &[id, data] : g_hotkeyContexts) {
    if (data && data->alias == alias) return id;
  }
  return "";
}

std::vector<std::string> HotkeyModule::findByKey(const std::string &key) {
  std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
  std::vector<std::string> result;
  for (const auto &[id, data] : g_hotkeyContexts) {
    if (data && data->key == key) result.push_back(id);
  }
  return result;
}

std::vector<std::string> HotkeyModule::getAllIds() {
  std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
  std::vector<std::string> result;
  result.reserve(g_hotkeyContexts.size());
  for (const auto &[id, data] : g_hotkeyContexts) {
    (void)data;
    result.push_back(id);
  }
  return result;
}

bool HotkeyModule::removeById(const std::string &hotkeyId) {
  std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
  return g_hotkeyContexts.erase(hotkeyId) > 0;
}

void HotkeyModule::resetTriggerCount(const std::string &hotkeyId) {
  auto *ctx = getHotkeyContextDataMutable(hotkeyId);
  if (ctx) {
    ctx->triggerCount = 0;
  }
}

int64_t HotkeyModule::getAge(const std::string &hotkeyId) {
  auto *ctx = getHotkeyContextData(hotkeyId);
  if (!ctx) return 0;
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now().time_since_epoch())
                 .count();
  return now - ctx->addedAt;
}

int64_t HotkeyModule::getElapsed(const std::string &hotkeyId) {
  auto *ctx = getHotkeyContextData(hotkeyId);
  if (!ctx || ctx->lastTriggeredAt == 0) return -1;
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch())
      .count();
  return now - ctx->lastTriggeredAt;
}

std::string HotkeyModule::findConditionalById(int condId) {
  std::string lookup = "condhk_" + std::to_string(condId);
  std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
  if (g_hotkeyContexts.count(lookup)) return lookup;
  return "";
}

size_t HotkeyModule::conditionalCount() {
  std::lock_guard<std::mutex> lock(g_hotkeyContextsMutex);
  size_t n = 0;
  for (const auto &[id, data] : g_hotkeyContexts) {
    if (id.rfind("condhk_", 0) == 0) n++;
  }
  return n;
}

} // namespace havel::stdlib
#endif // HAVEL_PURE_VM
