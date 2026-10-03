// EventBridge implementation — the generic event bus host functions.
// Producers publish; the runtime dispatches; handlers execute in the VM
// loop (architecture doc: "on" as syntax sugar over subscriptions).

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"
#include "../../../havel-lang/runtime/events/EventRuntime.hpp"
#include "../../../havel-lang/compiler/vm/VM.hpp"

namespace havel::compiler {
void EventBridge::install(PipelineOptions &options) {
  // event.subscribe(name, handler, [arg]) -> subscription id
  // The handler is a function object (fn index); the registry stores the
  // index (GC-safe — the function is also reachable via the chunk). The
  // optional arg (e.g. a watch path) is remembered for event-source wiring
  // via event.subscriptionArg(id).
  options.host_functions["event.subscribe"] = [ctx = ctx_](const auto &args) {
    if (args.size() < 2 || !ctx || !ctx->eventRuntime) {
      return Value::makeInt(0);
    }
    auto *vm = ctx->vm;
    if (!vm) {
      return Value::makeInt(0);
    }
    if (!args[0].isStringValId() && !args[0].isStringId()) {
      return Value::makeInt(0);
    }
    if (!args[1].isFunctionObjId()) {
      return Value::makeInt(0);
    }
    std::string name = vm->resolveStringKey(args[0]);
    uint32_t fnIndex = args[1].asFunctionObjId();
    auto *runtime = ctx->eventRuntime;
    uint64_t subId = runtime->subscribe(
        name, [vm, fnIndex](const EventPayload &payload) {
          // Runs in the VM's pump loop (never a backend thread). The
          // payload becomes the handler's `event` argument: an object of
          // string fields.
          Value eventObj;
          if (!payload.empty()) {
            auto objRef = vm->createHostObject();
            if (auto *obj = vm->getHeap().object(objRef.id)) {
              for (const auto &[key, str] : payload.fields) {
                auto ref = vm->getHeap().allocateString(str);
                obj->data[key] = Value::makeStringId(ref.id);
                obj->insertionOrder.push_back(key);
              }
            }
            eventObj = Value::makeObjectId(objRef.id);
          } else {
            eventObj = Value::makeNull();
          }
          // Root the event object for the call's duration: it is created
          // here in the pump, outside any VM root set, so the GC could
          // collect it before the handler's locals region references it.
          {
            auto root = vm->makeRoot(eventObj);
            vm->callFunctionSync(Value::makeFunctionObjId(fnIndex), {eventObj});
          }
        });
    // Remember the optional arg for event-source wiring (watch path etc.).
    if (args.size() >= 3 && !args[2].isNull()) {
      if (args[2].isStringValId() || args[2].isStringId()) {
        runtime->setSubscriptionArg(subId, vm->resolveStringKey(args[2]));
      }
    }
    return Value(static_cast<int64_t>(subId));
  };

  // event.subscriptionArg(id) -> string (the optional arg from subscribe)
  options.host_functions["event.subscriptionArg"] = [ctx =
                                                          ctx_](const auto &args) {
    if (args.empty() || !ctx || !ctx->eventRuntime || !args[0].isNumber()) {
      auto *vm = ctx ? ctx->vm : nullptr;
      if (!vm) {
        return Value::makeNull();
      }
      auto ref = vm->getHeap().allocateString("");
      return Value::makeStringId(ref.id);
    }
    auto *vm = ctx->vm;
    auto ref = vm->getHeap().allocateString(
        ctx->eventRuntime->getSubscriptionArg(
            static_cast<uint64_t>(args[0].asNumber())));
    return Value::makeStringId(ref.id);
  };

  // event.unsubscribe(id) / event.cancel(id) -> bool
  auto unsubHandler = [ctx = ctx_](const auto &args) -> Value {
    if (args.empty() || !ctx || !ctx->eventRuntime) {
      return Value::makeBool(false);
    }
    if (!args[0].isNumber()) {
      return Value::makeBool(false);
    }
    return Value::makeBool(ctx->eventRuntime->unsubscribe(
        static_cast<uint64_t>(args[0].asNumber())));
  };
  options.host_functions["event.unsubscribe"] = unsubHandler;
  options.host_functions["event.cancel"] = unsubHandler;

  // event.publish(name, [payload object]) — thread-safe, queued; the VM's
  // pump dispatches. The payload object's fields are flattened to strings
  // (thread-safe plain data).
  options.host_functions["event.publish"] = [ctx = ctx_](const auto &args) {
    if (args.empty() || !ctx || !ctx->eventRuntime) {
      return Value::makeBool(false);
    }
    auto *vm = ctx->vm;
    if (!vm || (!args[0].isStringValId() && !args[0].isStringId())) {
      return Value::makeBool(false);
    }
    std::string name = vm->resolveStringKey(args[0]);
    EventPayload payload;
    if (args.size() >= 2 && !args[1].isNull() && args[1].isObjectId()) {
      if (auto *obj = vm->getHeap().object(args[1].asObjectId())) {
        for (const auto &[key, val] : obj->data) {
          payload.fields[key] = vm->resolveStringKey(val);
        }
      }
    }
    ctx->eventRuntime->publish(name, std::move(payload));
    return Value::makeBool(true);
  };
}

} // namespace havel::compiler
