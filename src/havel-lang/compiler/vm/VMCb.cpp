#include "VM.hpp"
#include "VMInternals.hpp"
#include "../../../utils/Logger.hpp"
#include "../../runtime/concurrency/Thread.hpp"
#include "../../runtime/concurrency/Fiber.hpp"
#include "../../runtime/concurrency/Scheduler.hpp"
#include "../runtime/HostBridge.hpp"

namespace havel::compiler {

// ============================================================================
// Callback System - VM owns closures, systems use opaque IDs
// ============================================================================

CallbackId VM::registerCallback(const Value &closure) {
  // Accept both ClosureRef and FunctionObject
  if (!closure.isClosureId() && !closure.isFunctionObjId()) {
    COMPILER_THROW("registerCallback expects a closure or function");
  }

  // Pin the closure as an external root (GC will not collect it)
  CallbackId id = static_cast<CallbackId>(pinExternalRoot(closure));

  if (id == INVALID_CALLBACK_ID) {
    COMPILER_THROW("Failed to register callback - invalid ID");
  }

  return id;
}

Value VM::invokeCallback(CallbackId id,
                                 const std::vector<Value> &args) {
  if (id == INVALID_CALLBACK_ID) {
    COMPILER_THROW("invokeCallback called with invalid callback ID");
  }

  // Get the closure from external roots
  auto closureValue = externalRootValue(id);
  if (!closureValue.has_value()) {
    COMPILER_THROW(
        "invokeCallback: callback not found (may have been released)");
  }

  // Call the closure and return result
  return call(*closureValue, args);
}

uint32_t VM::spawnGoroutine(const Value &callee, const std::vector<Value> &args) {
  if (!scheduler_) {
    ::havel::warn("[VM] spawnGoroutine: No scheduler available");
    return 0;
  }

  uint32_t function_index = 0;
  uint32_t closure_id = 0;

  if (callee.isFunctionObjId()) {
    function_index = callee.asFunctionObjId();
  } else if (callee.isClosureId()) {
    closure_id = callee.asClosureId();
    auto *closure = heap_.closure(closure_id);
    if (!closure) {
      ::havel::warn("[VM] spawnGoroutine: Closure {} not found", closure_id);
      return 0;
    }
    function_index = closure->function_index;
  } else {
    ::havel::warn("[VM] spawnGoroutine: Callee is not a function or closure");
    return 0;
  }

  // Spawn the goroutine
  return scheduler_->spawn(function_index, args, closure_id, "async-task");
}

uint32_t VM::spawnCallback(CallbackId id, const std::vector<Value> &args) {
    auto closure = externalRootValue(id);
    if (!closure) {
        ::havel::warn("[VM] spawnCallback: Callback {} not found", id);
        return 0;
    }
    return spawnGoroutine(*closure, args);
}

uint32_t VM::spawnCallback(CallbackId id, FiberPriority priority, const std::vector<Value> &args) {
    if (!scheduler_) {
        ::havel::warn("[VM] spawnCallback: No scheduler available");
        return 0;
    }

    auto closure = externalRootValue(id);
    if (!closure) {
        ::havel::warn("[VM] spawnCallback: Callback {} not found", id);
        return 0;
    }

    uint32_t function_index = 0;
    uint32_t closure_id = 0;

    if (closure->isFunctionObjId()) {
        function_index = closure->asFunctionObjId();
    } else if (closure->isClosureId()) {
        closure_id = closure->asClosureId();
        auto *closure_obj = heap_.closure(closure_id);
        if (!closure_obj) {
            ::havel::warn("[VM] spawnCallback: Closure {} not found", closure_id);
            return 0;
        }
        function_index = closure_obj->function_index;
    } else {
        ::havel::warn("[VM] spawnCallback: Callback is not a function or closure");
        return 0;
    }

    ::havel::debug("[VM] spawnCallback: id={} priority={} func={} closure={} args={}", 
                  id, (int)priority, function_index, closure_id, args.size());

    uint32_t gid = scheduler_->spawn(function_index, args, closure_id, "hotkey-callback", priority);
    ::havel::debug("[VM] spawnCallback: created gid={} for callback id={}", gid, id);
    return gid;
}

uint32_t VM::createPersistentHotkeyCallback(CallbackId id, FiberPriority priority,
                                             const std::vector<Value> &args) {
    if (!scheduler_) {
        ::havel::warn("[VM] createPersistentHotkeyCallback: No scheduler available");
        return 0;
    }

    auto closure = externalRootValue(id);
    if (!closure) {
        ::havel::warn("[VM] createPersistentHotkeyCallback: Callback {} not found", id);
        return 0;
    }

    uint32_t function_index = 0;
    uint32_t closure_id = 0;

    if (closure->isFunctionObjId()) {
        function_index = closure->asFunctionObjId();
    } else if (closure->isClosureId()) {
        closure_id = closure->asClosureId();
        auto *closure_obj = heap_.closure(closure_id);
        if (!closure_obj) {
            ::havel::warn("[VM] createPersistentHotkeyCallback: Closure {} not found", closure_id);
            return 0;
        }
        function_index = closure_obj->function_index;
    } else {
        ::havel::warn("[VM] createPersistentHotkeyCallback: Callback is not a function or closure");
        return 0;
    }

    uint32_t gid = scheduler_->spawn(function_index, args, closure_id, "hotkey-persistent", priority);

    auto *g = scheduler_->get(gid);
    if (g) {
        g->persistent = true;
        g->hotkey_function_id = function_index;
        g->hotkey_closure_id = closure_id;
        g->hotkey_args = args;
        // Park immediately: set Suspended so pickNext skips it until first trigger
        g->state = Scheduler::GoroutineState::Suspended;
        g->suspension_reason = Scheduler::SuspensionReason::HotkeyWait;
        if (g->fiber) {
            g->fiber->state = FiberState::SUSPENDED;
            g->fiber->suspended_reason = SuspensionReason::HOTKEY_WAIT;
        }
        ::havel::debug("[VM] createPersistentHotkeyCallback: gid={} persistent (parked)", gid);
    }

    return gid;
}

void VM::releaseCallback(CallbackId id) {
  if (id == INVALID_CALLBACK_ID) {
    return; // Nothing to release
  }

  // Unpin the external root (GC can now collect it)
  unpinExternalRoot(id);
}

bool VM::isValidCallback(CallbackId id) const {
  if (id == INVALID_CALLBACK_ID) {
    return false;
  }

  return externalRootValue(id).has_value();
}

DirectCallThunk VM::buildDirectCallThunk(CallbackId id) {
    auto closureVal = externalRootValue(id);
    if (!closureVal) return {};

    uint32_t function_index = 0;
    const BytecodeChunk *chunk = current_chunk;

    if (closureVal->isFunctionObjId()) {
        function_index = closureVal->asFunctionObjId();
    } else if (closureVal->isClosureId()) {
        auto *closure_obj = heap_.closure(closureVal->asClosureId());
        if (!closure_obj) return {};
        function_index = closure_obj->function_index;
        if (closure_obj->chunk) chunk = closure_obj->chunk;
    } else {
        return {};
    }

    if (!chunk) return {};
    const auto *func = chunk->getFunction(function_index);
    if (!func) {
        if (main_chunk_) func = main_chunk_->getFunction(function_index);
        if (!func) return {};
    }

    DirectCallThunk thunk;
    std::vector<std::optional<Value>> sim;
    bool ok = true;

    auto pushConst = [&](uint32_t idx) {
        if (idx < func->constants.size()) {
            sim.push_back(func->constants[idx]);
        } else {
            ok = false;
        }
    };

    for (auto &instr : func->instructions) {
        if (!ok) break;

        switch (instr.opcode) {
        case OpCode::LOAD_CONST:
            if (instr.operands.empty() || !instr.operands[0].isInt()) { ok = false; break; }
            pushConst(instr.operands[0].asInt());
            break;

        case OpCode::PUSH_NULL:
            sim.push_back(Value::makeNull());
            break;

        case OpCode::DUP:
            if (sim.empty()) { ok = false; break; }
            sim.push_back(sim.back());
            break;

        case OpCode::POP:
            if (sim.empty()) { ok = false; break; }
            sim.pop_back();
            break;

        case OpCode::LOAD_GLOBAL: {
            if (instr.operands.empty() || !instr.operands[0].isStringValId()) { ok = false; break; }
            std::string name = chunk->getString(instr.operands[0].asStringValId());
            Value val = lookupGlobalByKey(name);
            if (val.isNull()) { ok = false; break; }
            sim.push_back(val);
            break;
        }

        case OpCode::CALL:
        case OpCode::TAIL_CALL: {
            if (instr.operands.empty() || !instr.operands[0].isInt()) { ok = false; break; }
            uint32_t n = instr.operands[0].asInt();
            if (sim.size() < n + 1) { ok = false; break; }
            std::vector<Value> args(n);
            for (uint32_t i = 0; i < n; ++i) {
                auto v = sim.back(); sim.pop_back();
                if (!v) { ok = false; break; }
                args[n - 1 - i] = *v;
            }
            if (!ok) break;
            auto callee = sim.back(); sim.pop_back();
            if (!callee || !callee->isHostFuncId()) { ok = false; break; }
            thunk.calls.push_back({callee->asHostFuncId(), std::move(args)});
            sim.push_back(std::nullopt); // return value not known
            break;
        }

        case OpCode::CALL_METHOD: {
            if (instr.operands.size() < 2 ||
                !instr.operands[0].isStringValId() ||
                !instr.operands[1].isInt()) { ok = false; break; }
            std::string method = chunk->getString(instr.operands[0].asStringValId());
            uint32_t n = instr.operands[1].asInt();
            if (sim.size() < n + 1) { ok = false; break; }
            std::vector<Value> args(n);
            for (uint32_t i = 0; i < n; ++i) {
                auto v = sim.back(); sim.pop_back();
                if (!v) { ok = false; break; }
                args[n - 1 - i] = *v;
            }
            if (!ok) break;
            auto recv = sim.back(); sim.pop_back();
            if (!recv) { ok = false; break; }

            // Resolve method on receiver (mirrors VM::executeInstruction CALL_METHOD logic)
            uint32_t host_idx = 0;
            bool found_host = false;
            bool found_via_module = false;
            bool is_instance = false;
            Value vm_func = Value::makeNull();

            if (recv->isObjectId()) {
                auto *obj = heap_.object(recv->asObjectId());
                if (obj) {
                    auto it = obj->find(method);
                    if (it != obj->end()) {
                        if (it->second.isHostFuncId()) {
                            host_idx = it->second.asHostFuncId();
                            found_host = true;
                            // Check if this object is in globals (namespace/module style)
                            for (auto &g : globals) {
                                if (g.second.isObjectId() &&
                                    g.second.asObjectId() == recv->asObjectId()) {
                                    found_via_module = true;
                                    break;
                                }
                            }
                        } else if (it->second.isFunctionObjId() || it->second.isClosureId()) {
                            vm_func = it->second;
                            is_instance = true;
                        }
                    }
                }
                // Check prototype chain
                if (!found_host && vm_func.isNull()) {
                    auto *proto = heap_.object(recv->asObjectId());
                    if (proto) {
                        auto *cv = proto->get("__class");
                        if (!cv) cv = proto->get("__struct");
                        if (cv && cv->isObjectId()) proto = heap_.object(cv->asObjectId());
                        else proto = nullptr;
                    }
                    while (proto) {
                        auto *mv = proto->get(method);
                        if (mv) {
                            if (mv->isHostFuncId()) {
                                host_idx = mv->asHostFuncId();
                                found_host = true;
                                break;
                            } else if (mv->isFunctionObjId() || mv->isClosureId()) {
                                vm_func = *mv;
                                break;
                            }
                        }
                        auto *pv = proto->get("__parent");
                        if (pv && pv->isObjectId()) proto = heap_.object(pv->asObjectId());
                        else break;
                    }
                }
                // Check type prototypes
                if (!found_host && vm_func.isNull()) {
                    std::string tn = "object";
                    auto pt = prototypes_.find(tn);
                    if (pt != prototypes_.end()) {
                        auto mt = pt->second.find(method);
                        if (mt != pt->second.end()) {
                            host_idx = mt->second;
                            found_host = true;
                        }
                    }
                }
            }

            if (!found_host) { ok = false; break; }

            // Assemble args using same convention as VM
            std::vector<Value> all_args;
            if (is_instance || found_via_module) {
                all_args = std::move(args);
            } else {
                all_args.reserve(n + 1);
                all_args.push_back(*recv);
                all_args.insert(all_args.end(), args.begin(), args.end());
            }
            thunk.calls.push_back({host_idx, std::move(all_args)});
            sim.push_back(std::nullopt);
            break;
        }

        case OpCode::RETURN:
            break;

        default:
            ok = false;
            break;
        }
    }

    if (!ok || thunk.calls.empty()) return {};
    return thunk;
}


// ============================================================================
// Image helpers - GC-managed image creation
// ============================================================================

VMImage VM::createImage(int width, int height, int stride, PixelFormat format,
                        const uint8_t *data) {
  VMImage img;
  img.width = width;
  img.height = height;
  img.stride = stride;
  img.format = format;

  // Create GC-managed byte array for image data
  size_t dataSize =
      stride > 0 ? static_cast<size_t>(stride) * height : width * height * 4;
  auto arrayRef = createHostArray();
  for (size_t i = 0; i < dataSize; ++i) {
    pushHostArrayValue(arrayRef, static_cast<int64_t>(data[i]));
  }

  // Store array reference in VMImage
  img.object_ref = ObjectRef{arrayRef.id};

  return img;
}

VMImage VM::createImageFromRGBA(int width, int height,
                                const std::vector<uint8_t> &rgbaData) {
  // RGBA format: 4 bytes per pixel
  int stride = width * 4;
  return createImage(width, height, stride, PixelFormat::RGBA8,
                     rgbaData.data());
}

std::unique_ptr<BytecodeInterpreter> createVM() {
    return std::make_unique<VM>();
}

} // namespace havel::compiler
