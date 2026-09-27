// InputBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
void InputBridge::install(PipelineOptions &options) {
  options.host_functions["hotkey.register"] = [ctx = ctx_](const auto &args) {
    return handleHotkeyRegister(args, ctx);
  };
  options.host_functions["hotkey.register_conditional"] = [ctx = ctx_](const auto &args) {
    return handleHotkeyRegisterConditional(args, ctx);
  };
  options.host_functions["hotkey.enable"] = [ctx = ctx_](const auto &args) {
    return handleHotkeyEnable(args, ctx);
  };
  options.host_functions["hotkey.disable"] = [ctx = ctx_](const auto &args) {
    return handleHotkeyDisable(args, ctx);
  };
  options.host_functions["hotkey.remove"] = [ctx = ctx_](const auto &args) {
    return handleHotkeyRemove(args, ctx);
  };
  options.host_functions["hotkey.trigger"] = [ctx = ctx_](const auto &args) {
    return handleHotkeyTrigger(args, ctx);
  };
  options.host_functions["hotkey.list"] = [ctx = ctx_](const auto &args) {
    return handleHotkeyList(args, ctx);
  };
  options.host_functions["mapmanager.map"] = [ctx = ctx_](const auto &args) {
    return handleMapManagerMap(args, ctx);
  };
  options.host_functions["mapmanager.getCurrentProfile"] =
      [ctx = ctx_](const auto &args) {
        return handleMapManagerGetCurrentProfile(args, ctx);
      };
  options.host_functions["alttab.show"] = [ctx = ctx_](const auto &args) {
    return handleAltTabShow(args, ctx);
  };
  options.host_functions["alttab.hide"] = [ctx = ctx_](const auto &args) {
    return handleAltTabHide(args, ctx);
  };
  options.host_functions["alttab.toggle"] = [ctx = ctx_](const auto &args) {
    return handleAltTabToggle(args, ctx);
  };
  options.host_functions["alttab.next"] = [ctx = ctx_](const auto &args) {
    return handleAltTabNext(args, ctx);
  };
  options.host_functions["alttab.previous"] = [ctx = ctx_](const auto &args) {
    return handleAltTabPrevious(args, ctx);
  };
  options.host_functions["alttab.select"] = [ctx = ctx_](const auto &args) {
    return handleAltTabSelect(args, ctx);
  };
    options.host_functions["alttab.getWindows"] = [ctx = ctx_](const auto &args) {
        return handleAltTabGetWindows(args, ctx);
    };
    options.host_functions["hotkey.onAnyKey"] = [ctx = ctx_](const auto &args) {
        return handleHotkeyOnAnyKey(args, ctx);
    };
}

namespace {
std::string resolveHotkeyTargetId(const std::vector<Value> &args,
                                 const HostContext *ctx) {
    if (args.empty() || !ctx || !ctx->vm) return {};
    auto *vm = static_cast<VM *>(ctx->vm);
    if (args[0].isObjectId()) {
        auto objRef = ObjectRef{args[0].asObjectId(), true};
        auto idValue = vm->getHostObjectField(objRef, "id");
        if (!idValue.isNull()) {
            return ::havel::stdlib::HotkeyModule::findByAlias(vm->resolveStringKey(idValue));
        }
        return {};
    }
    if (args[0].isStringValId() || args[0].isStringId()) {
        return ::havel::stdlib::HotkeyModule::findByAlias(vm->resolveStringKey(args[0]));
    }
    return {};
}
} // namespace


Value
InputBridge::handleHotkeyRegister(const std::vector<Value> &args,
    const HostContext *ctx) {
    // Args: [hotkey_string, callback_closure, options?]
    // options is an object with optional "policy" key: "drop"|"replace"|"queue"|"coalesce"
    if (args.size() < 2) {
        return Value::makeNull();
    }

    if (!ctx || !ctx->vm) {
        return Value::makeNull();
    }

    auto *vm = static_cast<VM *>(ctx->vm);
    std::string hotkeyStr;
    if (args[0].isStringValId() || args[0].isStringId()) {
        hotkeyStr = vm->resolveStringKey(args[0]);
    } else {
        return Value::makeNull();
    }

    HotkeyPolicy policy = HotkeyPolicy::Drop;
    if (args.size() >= 3 && args[2].isObjectId()) {
        auto policyVal = vm->objectGetWithClassChain(args[2].asObjectId(), "policy");
        if (policyVal.isStringValId() || policyVal.isStringId()) {
            std::string policyStr = vm->resolveStringKey(policyVal);
            if (policyStr == "replace") policy = HotkeyPolicy::Replace;
            else if (policyStr == "queue") policy = HotkeyPolicy::Queue;
            else if (policyStr == "coalesce") policy = HotkeyPolicy::Coalesce;
        }
    }

    // Generate unique hotkey ID from the key/alias string
    std::string hotkeyId = ::havel::stdlib::HotkeyModule::resolveUniqueId(hotkeyStr);

  // Register closure as a callback - this pins it as a GC root
  CallbackId callbackId = ctx->vm->registerCallback(args[1]);

  // Create hotkey context object using HotkeyModule
    auto hotkeyContext = ::havel::stdlib::HotkeyModule::createHotkeyContext(
        vm, hotkeyId, hotkeyStr, hotkeyStr, "",
        "Hotkey registered via hotkey.register", callbackId, true);

    if (hotkeyContext.isObjectId()) {
        vm->pinExternalRoot(hotkeyContext);
    }

    // Always create a persistent goroutine for the hotkey callback.
    // This avoids per-press goroutine allocation and allows wakeHotkeyByAlias
    // to find and trigger it even without a HotkeyManager (headless mode).
    uint32_t persistentGid = vm->createPersistentHotkeyCallback(
        callbackId, FiberPriority::HOTKEY, {hotkeyContext}, policy, hotkeyStr);

    if (persistentGid != 0) {
        ::havel::stdlib::HotkeyModule::setGoroutineId(hotkeyId, persistentGid);
    }

    // If hotkeyManager is available, wire the OS callback to wake the persistent goroutine
    if (ctx->hotkeyManager) {
        if (persistentGid != 0) {
            auto wakeHotkey = [vm, persistentGid, hotkeyId, policy]() {
                auto *sched = vm->getScheduler();
                if (!sched) return;
                // Coalesce redundant triggers: with Drop policy a wakeHotkey on
                // an already queued/running fiber is dropped by the scheduler
                // anyway, so skip the lock/queue/record work entirely. Prevents
                // bursty input (wheel storms) from hammering the scheduler.
                if (policy == HotkeyPolicy::Drop &&
                    sched->isHotkeyPending(persistentGid))
                    return;
                auto *g = sched->get(persistentGid);
                if (!g) return;
                sched->wakeHotkey(g, {}, "os-callback");
                ::havel::stdlib::HotkeyModule::recordTrigger(hotkeyId);
            };
            ctx->hotkeyManager->AddHotkey(
                hotkeyStr, [vm, wakeHotkey = std::move(wakeHotkey)]() {
                    auto *sched = vm->getScheduler();
                    if (!sched) return;
                    if (sched->isVMThread()) {
                        wakeHotkey();
                    } else {
                        sched->deferToVM(std::move(wakeHotkey));
                    }
                });
        } else {
            // Fallback: spawn a new goroutine per keypress
            ctx->hotkeyManager->AddHotkey(
                hotkeyStr, [vm, callbackId, hotkeyContext]() {
                    auto spawnHotkey = [vm, callbackId, hotkeyContext]() {
                        uint32_t gid = vm->spawnCallback(callbackId, FiberPriority::HOTKEY, {hotkeyContext});
                        if (gid == 0) {
                            ::havel::error("[Hotkey] Failed to spawn goroutine for callback {}", callbackId);
                        }
                    };
                    auto *sched = vm->getScheduler();
                    if (sched && sched->isVMThread()) {
                        spawnHotkey();
                    } else if (sched) {
                        sched->deferToVM(std::move(spawnHotkey));
                    }
                });
        }

    }
    return hotkeyContext;
}


Value
InputBridge::handleHotkeyRegisterConditional(const std::vector<Value> &args,
    const HostContext *ctx) {
    // Args: [hotkey_string, action_closure, condition_closure]
    if (args.size() < 3) return Value::makeNull();
    if (!ctx || !ctx->vm) return Value::makeNull();

    auto *vm = static_cast<VM *>(ctx->vm);
    std::string hotkeyStr;
    if (args[0].isStringValId() || args[0].isStringId()) {
        hotkeyStr = vm->resolveStringKey(args[0]);
    } else {
        return Value::makeNull();
    }

    if (!args[2].isFunctionObjId() && !args[2].isClosureId()) {
        // If no valid condition, fall back to regular register (no condition gating)
        return handleHotkeyRegister(args, ctx);
    }

    CallbackId actionCb = vm->registerCallback(args[1]);
    CallbackId conditionCb = vm->registerCallback(args[2]);

    std::string hotkeyId = ::havel::stdlib::HotkeyModule::resolveUniqueId(hotkeyStr);
    // Build a readable condition description from the callback ID
    std::string condDesc = "conditional_fn:" + std::to_string(conditionCb);
    // Conditional hotkeys don't grab at OS level by default
    auto hotkeyContext = ::havel::stdlib::HotkeyModule::createHotkeyContext(
        vm, hotkeyId, hotkeyStr, hotkeyStr, condDesc,
        "Conditional hotkey", actionCb, false, conditionCb);

    if (hotkeyContext.isObjectId()) {
        vm->pinExternalRoot(hotkeyContext);
    }

    uint32_t persistentGid = vm->createPersistentHotkeyCallback(
        actionCb, FiberPriority::HOTKEY, {hotkeyContext}, HotkeyPolicy::Drop, hotkeyStr);

    if (persistentGid != 0) {
        ::havel::stdlib::HotkeyModule::setGoroutineId(hotkeyId, persistentGid);
        auto *sched = vm->getScheduler();
        if (sched) {
            auto *g = sched->get(persistentGid);
            if (g) {
                g->hotkey_condition_callback_id = conditionCb;
                g->hotkey_condition_alias = hotkeyStr;
                // Evaluate condition to track variable dependencies
                auto condVal = vm->externalRootValue(conditionCb);
                if (condVal) {
                    auto tracker = std::make_shared<havel::compiler::DependencyTracker>();
                    havel::compiler::DependencyTrackerScope scope(tracker);
                    bool initialResult = false;
                    try {
                        Value result = vm->callFunctionSync(*condVal, {});
                        initialResult = vm->toBool(result);
                    } catch (const std::exception &e) {
                        ::havel::error("[InputBridge] conditional '{}' initial eval threw: {}", hotkeyStr, e.what());
                    }
                    g->hotkey_condition_last_result = initialResult;
                    ::havel::info("[InputBridge] conditional '{}' initial result = {}", hotkeyStr, initialResult);
                    auto deps = tracker->getGlobalDependencies();
                    auto fieldDeps = tracker->getFieldDependencies();
                    deps.insert(fieldDeps.begin(), fieldDeps.end());
                    // Union-merge with prior deps so registering/updating while
                    // the condition short-circuits retains previously-tracked
                    // globals.
                    g->hotkey_condition_deps.insert(deps.begin(), deps.end());
                }
                {
                    std::string depStr;
                    for (auto& d : g->hotkey_condition_deps) {
                        if (!depStr.empty()) depStr += ", ";
                        depStr += d;
                    }
                    ::havel::warn("[InputBridge] Conditional hotkey '{}' gid={} condition_cb={} deps={}: [{}]",
                        hotkeyStr, persistentGid, conditionCb, g->hotkey_condition_deps.size(), depStr);
                }
            }
        }
    }

    if (ctx->hotkeyManager) {
        if (persistentGid != 0) {
            auto wakeHotkey = [vm, persistentGid, hotkeyId]() {
                auto *sched = vm->getScheduler();
                if (!sched) return;
                // Conditional hotkeys are always Drop-policy — coalesce
                // redundant triggers on an already queued/running fiber.
                if (sched->isHotkeyPending(persistentGid)) return;
                auto *g = sched->get(persistentGid);
                if (!g) return;
                sched->wakeHotkey(g, {}, "conditional-os-callback");
                ::havel::stdlib::HotkeyModule::recordTrigger(hotkeyId);
            };
            ctx->hotkeyManager->AddHotkey(
                hotkeyStr, [vm, wakeHotkey = std::move(wakeHotkey)]() {
                    auto *sched = vm->getScheduler();
                    if (!sched) return;
                    if (sched->isVMThread()) {
                        wakeHotkey();
                    } else {
                        sched->deferToVM(std::move(wakeHotkey));
                    }
                });
            // Set grab state based on initial condition result
            // onVariableChanged will update this dynamically on VAR_CHANGED events
            if (persistentGid != 0) {
                if (auto* sched = vm->getScheduler()) {
                    if (auto* g = sched->get(persistentGid)) {
                        ctx->hotkeyManager->SetHotkeyGrab(hotkeyStr, g->hotkey_condition_last_result);
                        ::havel::stdlib::HotkeyModule::setGrab(*vm, hotkeyStr, g->hotkey_condition_last_result);
                        ::havel::info("[InputBridge] conditional '{}' initial SetHotkeyGrab = {}", hotkeyStr, g->hotkey_condition_last_result);
                    }
                }
            }
        } else {
            ctx->hotkeyManager->AddHotkey(
                hotkeyStr, [vm, actionCb, hotkeyContext]() {
                    auto spawnHotkey = [vm, actionCb, hotkeyContext]() {
                        vm->spawnCallback(actionCb, FiberPriority::HOTKEY, {hotkeyContext});
                    };
                    auto *sched = vm->getScheduler();
                    if (sched && sched->isVMThread()) {
                        spawnHotkey();
                    } else if (sched) {
                        sched->deferToVM(std::move(spawnHotkey));
                    }
                });
        }
    }

    return hotkeyContext;
}


Value
InputBridge::handleHotkeyOnAnyKey(const std::vector<Value> &args,
                                   const HostContext *ctx) {
    if (args.size() < 1) {
        return Value::makeNull();
    }
    if (!ctx || !ctx->hotkeyManager || !ctx->vm) {
        return Value::makeNull();
    }

    auto *vm = static_cast<VM *>(ctx->vm);
    CallbackId callbackId = ctx->vm->registerCallback(args[0]);

    ctx->hotkeyManager->RegisterAnyKeyPressCallback(
        [vm, callbackId](const std::string &key) {
            auto *sched = vm->getScheduler();
            if (!sched) return;
            sched->deferToVM([vm, callbackId, key]() {
                auto keyRef = vm->createRuntimeString(key);
                vm->spawnCallback(callbackId, FiberPriority::HOTKEY,
                                  {Value::makeStringId(keyRef.id)});
            });
        });

    return Value::makeBool(true);
}


Value
InputBridge::handleHotkeyEnable(const std::vector<Value> &args,
                                 const HostContext *ctx) {
    if (args.empty() || !ctx || !ctx->vm) return Value::makeBool(false);
    auto hotkeyId = resolveHotkeyTargetId(args, ctx);
    if (hotkeyId.empty()) return Value::makeBool(false);

    auto *vm = static_cast<VM *>(ctx->vm);
    auto *hostCtx = vm->hostContext();
    if (!hostCtx || !hostCtx->hotkeyManager) return Value::makeBool(false);

    std::string alias = ::havel::stdlib::HotkeyModule::resolveAlias(hotkeyId);
    if (alias.empty()) alias = hotkeyId;
    hostCtx->hotkeyManager->EnableHotkey(alias);
    ::havel::stdlib::HotkeyModule::setEnabled(hotkeyId, true);
    return Value::makeBool(true);
}


Value
InputBridge::handleHotkeyDisable(const std::vector<Value> &args,
                                  const HostContext *ctx) {
    if (args.empty() || !ctx || !ctx->vm) return Value::makeBool(false);
    auto hotkeyId = resolveHotkeyTargetId(args, ctx);
    if (hotkeyId.empty()) return Value::makeBool(false);

    auto *vm = static_cast<VM *>(ctx->vm);
    auto *hostCtx = vm->hostContext();
    if (!hostCtx || !hostCtx->hotkeyManager) return Value::makeBool(false);

    std::string alias = ::havel::stdlib::HotkeyModule::resolveAlias(hotkeyId);
    if (alias.empty()) alias = hotkeyId;
    hostCtx->hotkeyManager->DisableHotkey(alias);
    ::havel::stdlib::HotkeyModule::setEnabled(hotkeyId, false);
    return Value::makeBool(true);
}


Value
InputBridge::handleHotkeyRemove(const std::vector<Value> &args,
                                 const HostContext *ctx) {
    if (args.empty() || !ctx || !ctx->vm) return Value::makeBool(false);
    auto hotkeyId = resolveHotkeyTargetId(args, ctx);
    if (hotkeyId.empty()) return Value::makeBool(false);

    auto *vm = static_cast<VM *>(ctx->vm);
    auto *hostCtx = vm->hostContext();

    std::string alias = ::havel::stdlib::HotkeyModule::resolveAlias(hotkeyId);
    if (alias.empty()) alias = hotkeyId;
    if (hostCtx && hostCtx->hotkeyManager) {
        hostCtx->hotkeyManager->RemoveHotkey(alias);
    }
    bool removed = ::havel::stdlib::HotkeyModule::removeById(*vm, hotkeyId);
    return Value::makeBool(removed);
}


Value
InputBridge::handleHotkeyList(const std::vector<Value> &args,
                               const HostContext *ctx) {
    (void)args;
    if (!ctx || !ctx->vm) {
        return Value::makeNull();
    }

    auto *vm = static_cast<VM *>(ctx->vm);
    auto result = vm->createHostArray();
    auto resultGuard = vm->makeRoot(Value::makeArrayId(result.id));

    auto ids = ::havel::stdlib::HotkeyModule::getAllIds();
    for (const auto &hotkeyId : ids) {
        auto obj = ::havel::stdlib::HotkeyModule::rebuildHotkeyContext(*vm, hotkeyId);
        if (obj.isObjectId()) {
            vm->pushHostArrayValue(result, obj);
        }
    }

    return Value::makeArrayId(result.id);
}


Value
InputBridge::handleHotkeyTrigger(const std::vector<Value> &args,
                                 const HostContext *ctx) {
    if (!ctx || !ctx->vm) {
        return Value::makeBool(false);
    }

    auto hotkeyId = resolveHotkeyTargetId(args, ctx);
    if (hotkeyId.empty()) {
        return Value::makeBool(false);
    }

    auto *vm = static_cast<VM *>(ctx->vm);
    std::string alias = ::havel::stdlib::HotkeyModule::resolveAlias(hotkeyId);
    if (alias.empty()) alias = hotkeyId;

    ::havel::warn("[ModularHostBridges] hotkey.trigger('{}')", alias);

    bool woke_persistent = false;
    if (auto *sched = vm->getScheduler(); sched) {
        woke_persistent = sched->wakeHotkeyByAlias(alias);
    }

    if (!woke_persistent && ctx->hotkeyManager) {
        ctx->hotkeyManager->triggerForTest(alias);
    }

    return Value::makeBool(true);
}



Value
InputBridge::handleMapManagerMap(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}


Value InputBridge::handleMapManagerGetCurrentProfile(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  // TODO: string pool integration - for now return null
  return Value::makeNull();
}


Value
InputBridge::handleAltTabShow(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
#ifdef HAVE_QT_EXTENSION
  ::havel::AltTabService altTab;
  altTab.show();
#endif
  return Value::makeBool(true);
}


Value
InputBridge::handleAltTabHide(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
#ifdef HAVE_QT_EXTENSION
  ::havel::AltTabService altTab;
  altTab.hide();
#endif
  return Value::makeBool(true);
}


Value
InputBridge::handleAltTabToggle(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
#ifdef HAVE_QT_EXTENSION
  ::havel::AltTabService altTab;
  altTab.toggle();
#endif
  return Value::makeBool(true);
}


Value
InputBridge::handleAltTabNext(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
#ifdef HAVE_QT_EXTENSION
  ::havel::AltTabService altTab;
  altTab.next();
#endif
  return Value::makeBool(true);
}


Value
InputBridge::handleAltTabPrevious(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  (void)args;
  (void)ctx;
#ifdef HAVE_QT_EXTENSION
  ::havel::AltTabService altTab;
  altTab.previous();
#endif
  return Value::makeBool(true);
}


Value
InputBridge::handleAltTabSelect(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
#ifdef HAVE_QT_EXTENSION
  ::havel::AltTabService altTab;
  altTab.select();
#endif
  return Value::makeBool(true);
}


Value
InputBridge::handleAltTabGetWindows(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  (void)args;
#ifdef HAVE_QT_EXTENSION
  ::havel::AltTabService altTab;
  auto windows = altTab.getWindows();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }
  auto arr = vm->createHostArray();
  auto arrGuard = vm->makeRoot(Value::makeArrayId(arr.id));
  for (const auto &win : windows) {
    auto winObj = vm->createHostObject();
    // TODO: string pool integration - for now return null for strings
    (void)win.title; (void)win.className; (void)win.processName;
    vm->setHostObjectField(winObj, "title", Value::makeNull());
    vm->setHostObjectField(winObj, "className", Value::makeNull());
    vm->setHostObjectField(winObj, "processName", Value::makeNull());
    vm->setHostObjectField(winObj, "windowId",
                           Value::makeInt(static_cast<int64_t>(win.windowId)));
    vm->setHostObjectField(winObj, "active", Value::makeBool(win.active));
    vm->pushHostArrayValue(arr, Value::makeObjectId(winObj.id));
  }
  return Value::makeArrayId(arr.id);
#endif
}

// ============================================================================
// AutomationBridge Implementation
// ============================================================================

} // namespace havel::compiler
