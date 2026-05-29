#include "VM.hpp"
#include "VMInternals.hpp"
#include "../../../utils/Logger.hpp"
#include "../../utils/ErrorPrinter.hpp"
#include "../runtime/HostBridge.hpp"
#include "../runtime/RuntimeSupport.hpp"
#include "../../runtime/concurrency/DependencyTracker.hpp"
#include "../../runtime/concurrency/WatcherRegistry.hpp"
#include "../../runtime/concurrency/Fiber.hpp"
#include "../prototypes/PrototypeRegistry.hpp"
#include "core/config/ConfigManager.hpp"
#include <cmath>
#include <iostream>
#include <set>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <regex>

namespace havel::compiler {

bool VM::execCollectionOp(const Instruction &instruction) {
	switch (instruction.opcode) {
  case OpCode::ARRAY_NEW: {
    
    Value arr = Value::makeArrayId(heap_.allocateArray().id);
    
    pushStack(arr);
    maybeCollectGarbage();
    break;
  }

  case OpCode::SET_NEW: {
    pushStack(Value::makeSetId(heap_.allocateSet().id));
    maybeCollectGarbage();
    break;
  }

  case OpCode::SET_SET: {
    // Stack: [..., set, value, key] → pops all, does NOT push set back
    // The caller is responsible for managing the set on the stack
    Value key = popStack();
    Value value = popStack();
    Value set_val = popStack();

    if (!set_val.isSetId()) {
      COMPILER_THROW("SET_SET expects set container");
    }
    uint32_t id = set_val.asSetId();
    auto *set = heap_.set(id);
    if (!set) {
      COMPILER_THROW("SET_SET unknown set id");
    }

    std::string keyStr;
    if (key.isStringValId() && current_chunk) {
      keyStr = current_chunk->getString(key.asStringValId());
    } else if (key.isStringId()) {
      if (auto *s = heap_.string(key.asStringId())) {
        keyStr = *s;
      } else {
        COMPILER_THROW("SET_SET expects string key");
      }
    } else if (key.isInt()) {
      keyStr = std::to_string(key.asInt());
    } else {
      COMPILER_THROW("SET_SET expects string/number key");
    }

    (*set)[keyStr] = value;
    // Don't push set back - the caller manages it
    break;
  }

  case OpCode::ARRAY_PUSH: {
    Value value = popStack();
    Value container = popStack();

    if (!container.isArrayId()) {
      COMPILER_THROW("ARRAY_PUSH expects array container");
    }
    uint32_t id = container.asArrayId();
    auto *array = heap_.array(id);
    if (!array) {
      COMPILER_THROW("ARRAY_PUSH unknown array id");
    }
    if (array->frozen) {
      COMPILER_THROW("Cannot modify frozen array (tuple)");
    }
    array->push_back(value);
    pushStack(container);
    break;
  }

  case OpCode::ARRAY_LEN: {
    Value container = popStack();
    if (!container.isArrayId()) {
      COMPILER_THROW("ARRAY_LEN expects array container");
    }
    uint32_t id = container.asArrayId();
    auto *array = heap_.array(id);
    if (!array) {
      COMPILER_THROW("ARRAY_LEN unknown array id");
    }
    pushStack(Value::makeInt(static_cast<int64_t>(array->size())));
    break;
  }

  case OpCode::ARRAY_FREEZE: {
    Value container = popStack();
    if (!container.isArrayId()) {
      COMPILER_THROW("ARRAY_FREEZE expects array container");
    }
    uint32_t id = container.asArrayId();
    auto *array = heap_.array(id);
    if (!array) {
      COMPILER_THROW("ARRAY_FREEZE unknown array id");
    }
    array->frozen = true;
    pushStack(container);
    break;
  }

  // Range creation: start..end or start..step..end
  case OpCode::RANGE_NEW: {
    int64_t end = popStack().asInt();
    int64_t start = popStack().asInt();
    RangeRef rangeRef = heap_.allocateRange(start, end, 1);
    pushStack(Value::makeRangeId(rangeRef.id));
    break;
  }

  case OpCode::RANGE_STEP_NEW: {
    int64_t step = popStack().asInt();
    int64_t end = popStack().asInt();
    int64_t start = popStack().asInt();
    RangeRef rangeRef = heap_.allocateRange(start, end, step);
    pushStack(Value::makeRangeId(rangeRef.id));
    break;
  }

  // Enum operations
  case OpCode::ENUM_NEW: {
    // Operands: typeId (uint32), tag (uint32), payloadCount (uint32)
    uint32_t typeId = instruction.operands[0].asInt();
    uint32_t tag = instruction.operands[1].asInt();
    uint32_t payloadCount = instruction.operands[2].asInt();
    EnumRef enumRef = heap_.allocateEnum(typeId, tag, payloadCount);
    pushStack(Value::makeEnumId(enumRef.id));
    break;
  }

  case OpCode::ENUM_TAG: {
    Value enumVal = popStack();
    if (!enumVal.isEnumId()) {
      COMPILER_THROW("ENUM_TAG expects enum");
    }
    auto enumRef = EnumRef{enumVal.asEnumId(), 0, 0};
    pushStack(Value::makeInt(static_cast<int64_t>(getEnumTag(enumRef))));
    break;
  }

  case OpCode::ENUM_PAYLOAD: {
    Value indexVal = popStack();
    Value enumVal = popStack();
    if (!enumVal.isEnumId() || !indexVal.isInt()) {
      COMPILER_THROW("ENUM_PAYLOAD expects enum and int index");
    }
    auto enumRef = EnumRef{enumVal.asEnumId(), 0, 0};
    size_t index = static_cast<size_t>(indexVal.asInt());
    pushStack(heap_.enums_.at(enumRef.id).second.at(index));
    break;
  }

  case OpCode::ENUM_MATCH: {
    // Pop: enum ref, expected tag
    Value tagVal = popStack();
    Value enumVal = popStack();
    if (!enumVal.isEnumId() || !tagVal.isInt()) {
      COMPILER_THROW("ENUM_MATCH expects enum and int tag");
    }
    auto enumRef = EnumRef{enumVal.asEnumId(), 0, 0};
    int64_t expectedTag = tagVal.asInt();
    pushStack(Value::makeBool(getEnumTag(enumRef) ==
                              static_cast<uint32_t>(expectedTag)));
    break;
  }

  // String promotion: StringValId → StringId (for runtime string iteration)
  case OpCode::STRING_PROMOTE: {
    Value v = popStack();
    if (v.isStringValId()) {
      // Look up string from chunk's string table and allocate on heap
      uint32_t strIdx = v.asStringValId();
      std::string s;
      if (current_chunk) {
        s = current_chunk->getString(strIdx);
      }
      auto strRef = heap_.allocateString(s);
      pushStack(Value::makeStringId(strRef.id));
    } else {
      // Already a StringId or other type, passthrough
      pushStack(v);
    }
    break;
  }

  // Iteration protocol: iter(obj) → iterator
  case OpCode::ITER_NEW: {
    Value iterable = popStack();

    // Create iterator based on type
    IteratorRef iterRef;
    iterRef.id = heap_.createIterator(iterable);
    pushStack(Value::makeIteratorId(iterRef.id));
    break;
  }

  // Iteration protocol: iterator.next() → {value, done}
  case OpCode::ITER_NEXT: {
    Value iterator_val = popStack();
    if (!iterator_val.isIteratorId()) {
      COMPILER_THROW("ITER_NEXT expects iterator");
    }

    uint32_t id = iterator_val.asIteratorId();
    auto result = heap_.iteratorNext(id);

    // result is {value, done} object
    pushStack(result);
    break;
  }

  case OpCode::ARRAY_GET: {
    Value index_or_key = popStack();
    Value container = popStack();

    // Record feedback
    auto &frame = currentFrame();
    if (frame.ip < frame.function->type_feedback.size()) {
      auto &fb = frame.function->type_feedback[frame.ip];
      fb.execution_count++;
      fb.left_type_mask |= getFeedbackMask(container);
      fb.right_type_mask |= getFeedbackMask(index_or_key);
      if (fb.execution_count == 1000 && hot_func_cb_) {
        hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
      }
    }

        if (container.isStringId() || container.isStringValId()) {
            auto index = indexFromValue(index_or_key);
            if (!index) {
                COMPILER_THROW("STRING_GET expects integer index");
            }
            std::string s;
            if (container.isStringId()) {
                auto *sp = heap_.string(container.asStringId());
                if (sp) s = *sp;
            } else if (container.isStringValId() && current_chunk) {
                s = current_chunk->getString(container.asStringValId());
            }
            int64_t numCodepoints = 0;
            size_t bytePos = 0;
            while (bytePos < s.size()) {
                unsigned char c = static_cast<unsigned char>(s[bytePos]);
                size_t cpLen = 1;
                if (c < 0x80) { cpLen = 1; }
                else if ((c & 0xE0) == 0xC0) { cpLen = 2; }
                else if ((c & 0xF0) == 0xE0) { cpLen = 3; }
                else if ((c & 0xF8) == 0xF0) { cpLen = 4; }
                if (bytePos + cpLen > s.size()) { cpLen = 1; }
                numCodepoints++;
                bytePos += cpLen;
            }
            int64_t idx = *index;
            if (idx < 0) idx = numCodepoints + idx;
            if (idx < 0 || idx >= numCodepoints) {
                pushStack(Value::makeNull());
            } else {
                size_t targetByte = 0;
                int64_t cpIdx = 0;
                while (cpIdx < idx && targetByte < s.size()) {
                    unsigned char c = static_cast<unsigned char>(s[targetByte]);
                    size_t cpLen = 1;
                    if (c < 0x80) { cpLen = 1; }
                    else if ((c & 0xE0) == 0xC0) { cpLen = 2; }
                    else if ((c & 0xF0) == 0xE0) { cpLen = 3; }
                    else if ((c & 0xF8) == 0xF0) { cpLen = 4; }
                    if (targetByte + cpLen > s.size()) { cpLen = 1; }
                    targetByte += cpLen;
                    cpIdx++;
                }
                size_t cpLen = 1;
                if (targetByte < s.size()) {
                    unsigned char c = static_cast<unsigned char>(s[targetByte]);
                    if (c < 0x80) { cpLen = 1; }
                    else if ((c & 0xE0) == 0xC0) { cpLen = 2; }
                    else if ((c & 0xF0) == 0xE0) { cpLen = 3; }
                    else if ((c & 0xF8) == 0xF0) { cpLen = 4; }
                    if (targetByte + cpLen > s.size()) { cpLen = 1; }
                }
                auto ref = heap_.allocateString(s.substr(targetByte, cpLen));
                pushStack(Value::makeStringId(ref.id));
            }
            break;
        }

        if (container.isArrayId()) {
            auto index = indexFromValue(index_or_key);
            if (!index) {
                COMPILER_THROW("ARRAY_GET expects integer index");
            }
      auto *array = heap_.array(container.asArrayId());
      if (!array) {
        COMPILER_THROW("ARRAY_GET unknown array id");
      }
      // Handle negative indices: -1 = last element, -2 = second to last, etc.
      int64_t idx = *index;
      if (idx < 0) {
        idx = static_cast<int64_t>(array->size()) + idx;
      }
      Value result;
      if (idx < 0 || static_cast<size_t>(idx) >= array->size()) {
        result = Value::makeNull();
      } else {
        result = (*array)[static_cast<size_t>(idx)];
      }
      
      if (frame.ip < frame.function->type_feedback.size()) {
        frame.function->type_feedback[frame.ip].result_type_mask |= getFeedbackMask(result);
      }
      pushStack(result);
      break;
    }

    if (container.isSetId()) {
      auto key = resolveKey(index_or_key);
      if (!key) {
        COMPILER_THROW(
            "SET membership expects string/number/bool key");
      }
      auto *set = heap_.set(container.asSetId());
      if (!set) {
        COMPILER_THROW("ARRAY_GET unknown set id");
      }
      Value result = Value::makeBool(set->find(*key) != set->end());
      if (frame.ip < frame.function->type_feedback.size()) {
        frame.function->type_feedback[frame.ip].result_type_mask |= getFeedbackMask(result);
      }
      pushStack(result);
      break;
    }

    if (container.isObjectId()) {
        // Operator overloading: check for op_index method
        {
            Value opIndex = getHostObjectField(ObjectRef{container.asObjectId(), true}, "op_index");
            if (!opIndex.isNull() && (opIndex.isFunctionObjId() || opIndex.isClosureId() || opIndex.isHostFuncId())) {
                pushStack(callFunction(opIndex, {container, index_or_key}));
                break;
            }
        }
        // _G globals mirror: resolve from live globals maps
        if (container.asObjectId() == globals_mirror_object_id_) {
        auto key = resolveKey(index_or_key);
        if (!key) {
          COMPILER_THROW("OBJECT index expects string/number/bool key");
        }
        Value result = Value::makeNull();
        auto it = globals.find(*key);
        if (it != globals.end()) {
          result = it->second;
        } else {
          auto hostIt = host_function_globals_.find(*key);
          if (hostIt != host_function_globals_.end()) {
            result = hostIt->second;
          }
        }
        if (frame.ip < frame.function->type_feedback.size()) {
          frame.function->type_feedback[frame.ip].result_type_mask |= getFeedbackMask(result);
        }
        pushStack(result);
        break;
      }
      auto key = resolveKey(index_or_key);
      if (!key) {
        COMPILER_THROW("OBJECT index expects string/number/bool key");
      }
      auto *object = heap_.object(container.asObjectId());
      if (!object) {
        COMPILER_THROW("ARRAY_GET unknown object id");
      }
      auto kv = object->find(*key);
      Value result = (kv == object->end() ? Value::makeNull() : kv->second);
      if (frame.ip < frame.function->type_feedback.size()) {
        frame.function->type_feedback[frame.ip].result_type_mask |= getFeedbackMask(result);
      }
      pushStack(result);
      break;
    }

    COMPILER_THROW("ARRAY_GET expects array/set/object container");
  }

  case OpCode::ARRAY_SET: {
    Value value = popStack();
    Value index_or_key = popStack();
    Value container = popStack();

    if (container.isArrayId()) {
      auto index = indexFromValue(index_or_key);
      if (!index) {
        COMPILER_THROW("ARRAY_SET expects integer index");
      }
      auto *array = heap_.array(container.asArrayId());
      if (!array) {
        COMPILER_THROW("ARRAY_SET unknown array id");
      }
      if (array->frozen) {
        COMPILER_THROW("Cannot modify frozen array (tuple)");
      }
  // Handle negative indices for ARRAY_SET: -1 = last element, -2 = second to last
  int64_t idx = *index;
  if (idx < 0) {
    idx = static_cast<int64_t>(array->size()) + idx;
    if (idx < 0) {
      COMPILER_THROW("ARRAY_SET index out of bounds: " + std::to_string(*index));
    }
  }
  const auto idx_size = static_cast<size_t>(idx);
  if (idx_size >= array->size()) {
    array->resize(idx_size + 1, Value::makeNull());
  }
  (*array)[idx_size] = value;
      break;
    }

    if (container.isSetId()) {
      auto key = resolveKey(index_or_key);
      if (!key) {
        COMPILER_THROW(
            "SET assignment expects string/number/bool key");
      }
      auto *set = heap_.set(container.asSetId());
      if (!set) {
        COMPILER_THROW("ARRAY_SET unknown set id");
      }
      bool present = false;
      if (value.isBool()) {
        present = value.asBool();
      } else if (value.isInt()) {
        present = value.asInt() != 0;
      } else if (value.isDouble()) {
        present = value.asDouble() != 0.0;
      } else {
        COMPILER_THROW(
            "SET assignment value must be bool/number to indicate presence");
      }
      if (present) {
        (*set)[*key] = Value::makeNull();
      } else {
        set->erase(*key);
      }
      break;
    }

    if (container.isObjectId()) {
        // Operator overloading: check for op_index_set method
        {
            Value opIndexSet = getHostObjectField(ObjectRef{container.asObjectId(), true}, "op_index_set");
            if (!opIndexSet.isNull() && (opIndexSet.isFunctionObjId() || opIndexSet.isClosureId() || opIndexSet.isHostFuncId())) {
                callFunction(opIndexSet, {container, index_or_key, value});
                pushStack(container);
                break;
            }
        }
        auto key = resolveKey(index_or_key);
		if (!key) {
			COMPILER_THROW("OBJECT index assignment expects valid key");
		}
		if (container.asObjectId() == globals_mirror_object_id_) {
			auto *object = heap_.object(container.asObjectId());
			if (!object) {
				COMPILER_THROW("ARRAY_SET unknown object id");
			}
			(*object)[*key] = value;
			globals[*key] = value;
			break;
		}
		auto *object = heap_.object(container.asObjectId());
		if (!object) {
			COMPILER_THROW("ARRAY_SET unknown object id");
		}
		(*object)[*key] = value;
		break;
	}

    COMPILER_THROW("ARRAY_SET expects array/set/object container");
  }

  case OpCode::OBJECT_NEW: {
    pushStack(Value::makeObjectId(heap_.allocateObject(true).id)); // sorted = true
    maybeCollectGarbage();
    break;
  }

  case OpCode::OBJECT_NEW_UNSORTED: {
    pushStack(Value::makeObjectId(heap_.allocateObject(false).id)); // sorted = false
    maybeCollectGarbage();
    break;
  }

  case OpCode::OBJECT_GET: {
    Value key_value = popStack();
    Value object = popStack();

    // Handle function objects - support fn.name, fn.arity, fn.params, fn.prop
    if (object.isFunctionObjId()) {
      uint32_t funcIdx = object.asFunctionObjId();
      auto key = resolveKey(key_value);
      
      // First check for user-defined properties
      if (key) {
        auto propIt = function_properties_.find(funcIdx);
        if (propIt != function_properties_.end()) {
          auto* props = heap_.object(propIt->second.id);
          if (props) {
            auto* val = props->get(*key);
            if (val) {
              pushStack(*val);
              break;
            }
          }
        }
      }
      
      // Then check built-in properties
      if (current_chunk) {
        const auto* func = current_chunk->getFunction(funcIdx);
        if (key && func) {
          if (*key == "name") {
            pushStack(Value::makeStringId(heap_.allocateString(func->name).id));
            break;
          } else if (*key == "arity") {
            pushStack(Value::makeInt(static_cast<int64_t>(func->param_count)));
            break;
          } else if (*key == "params") {
            auto arrRef = heap_.allocateArray();
            auto* arr = heap_.array(arrRef.id);
            for (const auto& p : func->param_names) {
              arr->push_back(Value::makeStringId(heap_.allocateString(p).id));
            }
            pushStack(Value::makeArrayId(arrRef.id));
            break;
          }
        }
      }
        pushStack(Value::makeNull());
        break;
    }

    // Handle closure objects - support closure.name, closure.arity, closure.prop
    if (object.isClosureId()) {
        uint32_t closureId = object.asClosureId();
        auto key = resolveKey(key_value);

        // First check for user-defined properties
        if (key) {
            auto propIt = closure_properties_.find(closureId);
            if (propIt != closure_properties_.end()) {
                auto* props = heap_.object(propIt->second.id);
                if (props) {
                    auto* val = props->get(*key);
                    if (val) {
                        pushStack(*val);
                        break;
                    }
                }
            }
        }

        // Then check built-in properties from underlying function
        auto* closure = heap_.closure(closureId);
        if (closure && current_chunk && key) {
            if (closure->function_index < current_chunk->getFunctionCount()) {
                const auto* func = current_chunk->getFunction(closure->function_index);
                if (func) {
                    if (*key == "name") {
                        pushStack(Value::makeStringId(heap_.allocateString(func->name).id));
                        break;
                    } else if (*key == "arity") {
                        pushStack(Value::makeInt(static_cast<int64_t>(func->param_count)));
                        break;
                    }
                }
            }
        }
        pushStack(Value::makeNull());
        break;
    }

    // Handle host function objects - support hostfn.name, hostfn.prop
    if (object.isHostFuncId()) {
        uint32_t hostIdx = object.asHostFuncId();
        auto key = resolveKey(key_value);

        // First check for user-defined properties
        if (key) {
            auto propIt = hostfunc_properties_.find(hostIdx);
            if (propIt != hostfunc_properties_.end()) {
                auto* props = heap_.object(propIt->second.id);
                if (props) {
                    auto* val = props->get(*key);
                    if (val) {
                        pushStack(*val);
                        break;
                    }
                }
            }
        }

        // Then check built-in properties
        if (key) {
            if (*key == "name") {
                if (hostIdx < host_function_names_.size()) {
                    pushStack(Value::makeStringId(heap_.allocateString(host_function_names_[hostIdx]).id));
                    break;
                }
            } else if (*key == "arity") {
                pushStack(Value::makeInt(-1));
                break;
            }
        }
        pushStack(Value::makeNull());
        break;
    }

    // Handle class/struct prototype objects - support Class.name, Class.methods, Class.fields
    if (object.isObjectId()) {
      auto* obj = heap_.object(object.asObjectId());
      if (obj) {
        // Check if this is a class/struct prototype (has __is_class or __is_struct marker)
        bool isClass = false;
        if (obj->get("__is_class") || obj->get("__is_struct")) {
          isClass = true;
        }
        if (isClass) {
          auto key = resolveKey(key_value);
          if (key) {
            if (*key == "name") {
              auto* nameVal = obj->get("__name");
              if (nameVal) { pushStack(*nameVal); break; }
              pushStack(Value::makeNull());
              break;
            } else if (*key == "methods") {
              // Collect method names from the prototype
              auto arrRef = heap_.allocateArray();
              auto* arr = heap_.array(arrRef.id);
              auto keys = obj->getKeys();
              for (const auto& k : keys) {
                auto* val = obj->get(k);
                if (val && (val->isFunctionObjId() || val->isHostFuncId())) {
                  arr->push_back(Value::makeStringId(heap_.allocateString(k).id));
                }
              }
              pushStack(Value::makeArrayId(arrRef.id));
              break;
            } else if (*key == "fields") {
              auto* fieldsVal = obj->get("__fields");
              if (fieldsVal && fieldsVal->isArrayId()) {
                pushStack(*fieldsVal);
              } else {
                // Collect from default null fields
                auto arrRef = heap_.allocateArray();
                auto* arr = heap_.array(arrRef.id);
                auto keys = obj->getKeys();
                for (const auto& k : keys) {
                  auto* val = obj->get(k);
                  if (val && val->isNull()) {
                    arr->push_back(Value::makeStringId(heap_.allocateString(k).id));
                  }
                }
                pushStack(Value::makeArrayId(arrRef.id));
              }
              break;
            }
          }
        }
      }
    }



    // Handle arrays - look up prototype methods OR numeric indices
    if (object.isArrayId()) {
      auto *array = heap_.array(object.asArrayId());
      
      // Check for numeric index first
      if (key_value.isInt() && array) {
        int64_t index = key_value.asInt();
        if (index < 0) {
          index = static_cast<int64_t>(array->size()) + index;
        }
        if (index >= 0 && static_cast<size_t>(index) < array->size()) {
          pushStack((*array)[static_cast<size_t>(index)]);
        } else {
          pushStack(Value::makeNull());
        }
        break;
      }
      
        // Then check for prototype methods
        auto key = resolveKey(key_value);
        if (key) {
            if (*key == "len" && array) {
                pushStack(Value::makeInt(static_cast<int64_t>(array->size())));
                break;
            }
            auto method = getPrototypeMethod(object, *key);
            if (method) {
                auto bmRef = heap_.allocateBoundMethod(Value::makeHostFuncId(getHostFunctionIndex(host_function_names_[*method])), Value::makeArrayId(object.asArrayId()));
                pushStack(Value::makeBoundMethodId(bmRef.id));
                break;
            }
        }
        pushStack(Value::makeNull());
        break;
    }

    // Handle strings - look up prototype methods
    if (object.isStringId() || object.isStringValId()) {
        // Promote StringValId to StringId if needed
        Value stringVal = object;
        std::string actualStr;
        if (object.isStringValId() && current_chunk) {
            actualStr = current_chunk->getString(object.asStringValId());
            auto strRef = heap_.allocateString(actualStr);
            stringVal = Value::makeStringId(strRef.id);
        } else if (object.isStringId()) {
            auto *s = heap_.string(object.asStringId());
            if (s) actualStr = *s;
        }
        auto key = resolveKey(key_value);
        if (key) {
            if (*key == "len") {
                pushStack(Value::makeInt(static_cast<int64_t>(actualStr.size())));
                break;
            }
                auto method = getPrototypeMethod(stringVal, *key);
                if (method) {
                    auto bmRef = heap_.allocateBoundMethod(Value::makeHostFuncId(*method), stringVal);
                    pushStack(Value::makeBoundMethodId(bmRef.id));
                    break;
                }
      }
      pushStack(Value::makeNull());
      break;
    }

    // Handle ranges - expose "step" property for type detection
    if (object.isRangeId()) {
      auto *r = heap_.range(object.asRangeId());
      if (r) {
        auto key = resolveKey(key_value);
        if (key) {
          if (*key == "step") {
            pushStack(Value::makeInt(r->step));
            break;
          } else if (*key == "start") {
            pushStack(Value::makeInt(r->start));
            break;
          } else if (*key == "end") {
            pushStack(Value::makeInt(r->end));
            break;
          }
        }
      }
      pushStack(Value::makeNull());
      break;
    }

    if (object.isHostFuncId()) {
      auto key = resolveKey(key_value);
      if (key) {
        uint32_t idx = object.asHostFuncId();
        if (*key == "name" && idx < host_function_names_.size()) {
          pushStack(Value::makeStringId(heap_.allocateString(host_function_names_[idx]).id));
          break;
        } else if (*key == "arity") {
          pushStack(Value::makeInt(0));
          break;
        }
      }
      pushStack(Value::makeNull());
      break;
    }

    if (object.isSetId()) {
      auto key = resolveKey(key_value);
      if (key && *key == "len") {
        auto *set = heap_.set(object.asSetId());
        pushStack(Value::makeInt(set ? static_cast<int64_t>(set->size()) : 0));
      } else if (key) {
                auto method = getPrototypeMethod(object, *key);
                if (method) {
                    auto bmRef = heap_.allocateBoundMethod(Value::makeHostFuncId(getHostFunctionIndex(host_function_names_[*method])), object);
                    pushStack(Value::makeBoundMethodId(bmRef.id));
        } else {
          pushStack(Value::makeNull());
        }
      } else {
        pushStack(Value::makeNull());
      }
      break;
    }

    if (!object.isObjectId()) {
      pushStack(Value::makeNull());
      break;
    }

	// _G globals mirror: delegate property access to the live globals maps
	if (object.asObjectId() == globals_mirror_object_id_) {
		// Numeric index on _G: index into sorted list of global keys
		if (key_value.isInt()) {
			std::vector<std::string> allKeys;
			std::set<std::string> seen;
			for (const auto& [name, _] : globals) {
				if (seen.insert(name).second) allKeys.push_back(name);
			}
			for (const auto& [name, _] : host_function_globals_) {
				if (seen.insert(name).second) allKeys.push_back(name);
			}
			int64_t index = key_value.asInt();
			if (index < 0) {
				index = static_cast<int64_t>(allKeys.size()) + index;
			}
			if (index >= 0 && static_cast<size_t>(index) < allKeys.size()) {
				const std::string& keyName = allKeys[static_cast<size_t>(index)];
				pushStack(Value::makeStringId(heap_.allocateString(keyName).id));
			} else {
				pushStack(Value::makeNull());
			}
			break;
		}
		auto key = resolveKey(key_value);
		if (!key) {
			COMPILER_THROW("OBJECT_GET expects string/number/bool key");
		}
		auto it = globals.find(*key);
		if (it != globals.end()) {
                if (it->second.isFunctionObjId() || it->second.isClosureId() || it->second.isHostFuncId()) {
                    auto bmRef = heap_.allocateBoundMethod(it->second, object);
                    pushStack(Value::makeBoundMethodId(bmRef.id));
			} else {
				pushStack(it->second);
			}
			break;
		}
		auto hostIt = host_function_globals_.find(*key);
		if (hostIt != host_function_globals_.end()) {
                if (hostIt->second.isFunctionObjId() || hostIt->second.isClosureId() || hostIt->second.isHostFuncId()) {
                    auto bmRef = heap_.allocateBoundMethod(hostIt->second, object);
                    pushStack(Value::makeBoundMethodId(bmRef.id));
                } else {
				pushStack(hostIt->second);
			}
			break;
		}
	pushStack(Value::makeNull());
			break;
		}

        auto objRef = ObjectRef{object.asObjectId(), true};
        auto *obj = heap_.object(objRef.id);
        if (!obj) {
            COMPILER_THROW("OBJECT_GET unknown object id");
	}

        // Check for numeric index (obj[0], obj[-1])
    if (key_value.isInt()) {
      int64_t index = key_value.asInt();
      auto keys = obj->getKeys();
      // Handle negative indices
      if (index < 0) {
        index = static_cast<int64_t>(keys.size()) + index;
      }
      if (index >= 0 && static_cast<size_t>(index) < keys.size()) {
        auto *val = obj->get(keys[static_cast<size_t>(index)]);
        if (val) {
          pushStack(*val);
        } else {
          pushStack(Value::makeNull());
        }
      } else {
        pushStack(Value::makeNull());
      }
      break;
    }

    auto key = resolveKey(key_value);
    if (!key) {
        COMPILER_THROW("OBJECT_GET expects string/number/bool key");
    }

    Value found_val = Value::makeNull();
    GCHeap::ObjectEntry *current_obj = obj;

    while (current_obj) {
      auto *val = current_obj->get(*key);
      if (val) {
        found_val = *val;
        break;
      }
      // Check prototypes: __proto first (Lua-style), then __class, __struct, __parent
      auto* parent_val = current_obj->get("__proto");
      if (!parent_val) parent_val = current_obj->get("__class");
      if (!parent_val) parent_val = current_obj->get("__struct");
      if (!parent_val) parent_val = current_obj->get("__parent");

      if (parent_val && parent_val->isObjectId()) {
        current_obj = heap_.object(parent_val->asObjectId());
      } else {
        current_obj = nullptr;
      }
    }

        if (!found_val.isNull()) {
            if (found_val.isHostFuncId()) {
                pushStack(found_val);
            } else if (found_val.isFunctionObjId() || found_val.isClosureId()) {
                auto boundObj = heap_.allocateObject();
                auto *bObj = heap_.object(boundObj.id);
                (*bObj)["fn"] = found_val;
                (*bObj)["self"] = object;
                pushStack(Value::makeObjectId(boundObj.id));
            } else {
                pushStack(found_val);
            }
    } else {
        // Built-in .len property returns key count for objects
        if (*key == "len") {
            auto keys = obj->getKeys();
            pushStack(Value::makeInt(static_cast<int64_t>(keys.size())));
        } else {
            // Check built-in prototype methods (for strings, arrays, etc.)
            auto method = getPrototypeMethod(object, *key);
            if (method) {
                auto boundObj = heap_.allocateObject();
                auto *bObj = heap_.object(boundObj.id);
                (*bObj)["fn"] = Value::makeHostFuncId(getHostFunctionIndex(host_function_names_[*method]));
                (*bObj)["self"] = object;
                pushStack(Value::makeObjectId(boundObj.id));
            } else {
                // Autovivification: if object has __vivify, auto-create sub-object
                auto* vivify = obj->get("__vivify");
                if (vivify && !vivify->isNull() && !vivify->isBool()) {
                    // Create a new sub-object with same __vivify marker
                    auto subRef = heap_.allocateObject();
                    auto *subObj = heap_.object(subRef.id);
                    (*subObj)["__vivify"] = *vivify;
                    // Also propagate __autosave_root if present
                    auto* autoSaveRoot = obj->get("__autosave_root");
                    if (autoSaveRoot) {
                        (*subObj)["__autosave_root"] = *autoSaveRoot;
                    }
                    // Propagate __cfg_path for nested path tracking
                    auto* parentPath = obj->get("__cfg_path");
                    std::string childPath;
                    if (parentPath && parentPath->isStringValId()) {
                        auto* parentStr = heap_.string(parentPath->asStringValId());
                        childPath = parentStr ? (*parentStr + "." + *key) : *key;
                    } else {
                        childPath = *key;
                    }
                    auto pathRef = heap_.allocateString(childPath);
                    (*subObj)["__cfg_path"] = Value::makeStringValId(pathRef.id);
                    // Store sub-object on parent so it persists
                    obj->set(*key, Value::makeObjectId(subRef.id));
                    pushStack(Value::makeObjectId(subRef.id));
                } else {
                    pushStack(Value::makeNull());
                }
            }
        }
    }
    break;
  }

  case OpCode::OBJECT_SET: {
    // Stack: [..., obj, value, key] → pops all, pushes obj
    // This allows chaining: obj { DUP, val1, "k1", SET, val2, "k2", SET, ... }
    Value key = popStack();
    Value value = popStack();
    Value object = popStack();

    auto keyStr = resolveKey(key);
    if (!keyStr) {
      COMPILER_THROW("OBJECT_SET expects string/number/bool key");
    }

        // Safety: reject __ prefixed keys (reserved for internal use)
        // Exception: __name__, __wrapped__, __arity__ are allowed on callable types
        // for decorator metadata preservation (Python convention)
        bool isCallable = object.isFunctionObjId() || object.isClosureId() || object.isHostFuncId() || object.isBoundMethodId();
        bool isAllowedDunder = *keyStr == "__name__" || *keyStr == "__wrapped__" || *keyStr == "__arity__";
        if (keyStr->size() >= 2 && (*keyStr)[0] == '_' && (*keyStr)[1] == '_' &&
            !(isCallable && isAllowedDunder)) {
            COMPILER_THROW(
                "OBJECT_SET: keys starting with '__' are reserved");
        }

    // Handle function objects - support fn.prop = value
    if (object.isFunctionObjId()) {
      uint32_t funcIdx = object.asFunctionObjId();
      
      // Get or create properties object for this function
      auto& propRef = function_properties_[funcIdx];
      if (propRef.id == 0) {
        propRef = heap_.allocateObject(true); // Create sorted object for properties
      }
      
      auto* props = heap_.object(propRef.id);
      if (!props) {
        COMPILER_THROW("OBJECT_SET: failed to get function properties object");
      }
        props->set(*keyStr, std::move(value));
        pushStack(object); // Return the function object for chaining
        break;
    }

    // Handle closure objects - support closure.prop = value
    if (object.isClosureId()) {
        uint32_t closureId = object.asClosureId();
        auto& propRef = closure_properties_[closureId];
        if (propRef.id == 0) {
            propRef = heap_.allocateObject(true);
        }
        auto* props = heap_.object(propRef.id);
        if (!props) {
            COMPILER_THROW("OBJECT_SET: failed to get closure properties object");
        }
        props->set(*keyStr, std::move(value));
        pushStack(object);
        break;
    }

    // Handle host function objects - support hostfn.prop = value
    if (object.isHostFuncId()) {
        uint32_t hostIdx = object.asHostFuncId();
        auto& propRef = hostfunc_properties_[hostIdx];
        if (propRef.id == 0) {
            propRef = heap_.allocateObject(true);
        }
        auto* props = heap_.object(propRef.id);
        if (!props) {
            COMPILER_THROW("OBJECT_SET: failed to get host func properties object");
        }
        props->set(*keyStr, std::move(value));
        pushStack(object);
        break;
    }

	if (!object.isObjectId()) {
		COMPILER_THROW("OBJECT_SET expects object container");
	}

        // _G globals mirror: writing to _G also updates the globals map
  if (object.asObjectId() == globals_mirror_object_id_) {
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_SET unknown object id");
    }
    obj->set(*keyStr, value);
    globals[*keyStr] = value;
    pushStack(object);
    break;
  }

  auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_SET unknown object id");
    }
    obj->set(*keyStr, value);

    // Auto-save: if object has __autosave_root, persist to config store
    auto* autoSaveRoot = obj->get("__autosave_root");
    if (autoSaveRoot && !autoSaveRoot->isNull()) {
        auto* cfgPathVal = obj->get("__cfg_path");
        if (cfgPathVal && cfgPathVal->isStringValId()) {
            auto* pathStr = heap_.string(cfgPathVal->asStringValId());
            if (pathStr) {
                std::string fullKey = *pathStr + "." + *keyStr;
                // Resolve value to string for config storage
                std::string valStr;
                if (value.isBool()) valStr = value.asBool() ? "true" : "false";
                else if (value.isInt()) valStr = std::to_string(value.asInt());
                else if (value.isDouble()) valStr = std::to_string(value.asDouble());
                else if (value.isStringValId()) {
                    auto* s = heap_.string(value.asStringValId());
                    valStr = s ? *s : "";
                } else if (value.isStringId()) {
                    auto* s = heap_.string(value.asStringId());
                    valStr = s ? *s : "";
                } else {
                    valStr = "";
                }
                
                if (!valStr.empty() || value.isBool() || value.isInt() || value.isDouble()) {
                    auto &config = Configs::Get();
                    config.Set(fullKey, valStr, true);
                    // havel::debug("VM Auto-save: {} = {}", fullKey, valStr);
                }
            }
        }
    }

    pushStack(object); // Return the object for chaining
    break;
  }

	case OpCode::OBJECT_GET_RAW: {
		Value key_value = popStack();
		Value object = popStack();

		if (!object.isObjectId()) {
			pushStack(Value::makeNull());
			break;
		}

		auto key = resolveKey(key_value);
		if (!key) {
			pushStack(Value::makeNull());
			break;
		}

		if (object.asObjectId() == globals_mirror_object_id_) {
			auto it = globals.find(*key);
			if (it != globals.end()) {
				pushStack(it->second);
				break;
			}
			auto hostIt = host_function_globals_.find(*key);
			if (hostIt != host_function_globals_.end()) {
				pushStack(hostIt->second);
				break;
			}
			pushStack(Value::makeNull());
			break;
		}

		GCHeap::ObjectEntry *current_obj = heap_.object(object.asObjectId());
		while (current_obj) {
			auto *val = current_obj->get(*key);
			if (val) {
				pushStack(*val);
				break;
			}
			auto* parent_val = current_obj->get("__class");
			if (!parent_val) parent_val = current_obj->get("__parent");
			if (parent_val && parent_val->isObjectId()) {
				current_obj = heap_.object(parent_val->asObjectId());
			} else {
				current_obj = nullptr;
			}
		}
		if (!current_obj) {
			pushStack(Value::makeNull());
		}
		break;
	}

  // Object intrinsics (VM-level operations)
  case OpCode::OBJECT_KEYS: {
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_KEYS expects object");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_KEYS unknown object id");
    }
    auto arrRef = heap_.allocateArray();
    auto *arr = heap_.array(arrRef.id);
    auto keys = obj->getKeys();
    for (const auto &key : keys) {
      arr->push_back(Value::makeStringId(heap_.allocateString(key).id));
    }
    pushStack(Value::makeArrayId(arrRef.id));
    break;
  }

  case OpCode::OBJECT_VALUES: {
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_VALUES expects object");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_VALUES unknown object id");
    }
    auto arrRef = heap_.allocateArray();
    auto *arr = heap_.array(arrRef.id);
    auto keys = obj->getKeys();
    for (const auto &key : keys) {
      auto *val = obj->get(key);
      if (val) {
        arr->push_back(*val);
      }
    }
    pushStack(Value::makeArrayId(arrRef.id));
    break;
  }

  case OpCode::OBJECT_ENTRIES: {
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_ENTRIES expects object");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_ENTRIES unknown object id");
    }
    auto arrRef = heap_.allocateArray();
    auto keys = obj->getKeys();
    for (const auto &key : keys) {
      auto *val = obj->get(key);
      if (val) {
        // Create [key, value] tuple as array
        auto tupleRef = heap_.allocateArray();
        auto *tuple = heap_.array(tupleRef.id);
        // TODO: string pool registration
        tuple->push_back(Value::makeNull());
        tuple->push_back(*val);
        auto *arr = heap_.array(arrRef.id);
        arr->push_back(Value::makeArrayId(tupleRef.id));
      }
    }
    pushStack(Value::makeArrayId(arrRef.id));
    break;
  }

  case OpCode::OBJECT_HAS: {
    Value keyValue = popStack();
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_HAS expects object");
    }
    auto key = resolveKey(keyValue);
    if (!key) {
      COMPILER_THROW("OBJECT_HAS expects string/number/bool key");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      pushStack(Value::makeBool(false));
    } else {
      pushStack(Value::makeBool(obj->get(*key) != nullptr));
    }
    break;
  }

  case OpCode::OBJECT_DELETE: {
    Value keyValue = popStack();
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_DELETE expects object");
    }
    auto key = resolveKey(keyValue);
    if (!key) {
      COMPILER_THROW("OBJECT_DELETE expects string/number/bool key");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      pushStack(Value::makeBool(false));
    } else {
      pushStack(Value::makeBool(obj->data.erase(*key) > 0));
    }
    break;
  }

  case OpCode::ARRAY_DEL: {
    Value keyValue = popStack();
    Value container = popStack();
    if (container.isArrayId()) {
      auto index = indexFromValue(keyValue);
      if (!index) {
        COMPILER_THROW("ARRAY_DEL expects integer index");
      }
      auto *array = heap_.array(container.asArrayId());
      if (!array) {
        COMPILER_THROW("ARRAY_DEL unknown array id");
      }
      int64_t idx = *index;
      if (idx < 0) idx = static_cast<int64_t>(array->size()) + idx;
      if (idx < 0 || static_cast<size_t>(idx) >= array->size()) {
        pushStack(Value::makeBool(false));
      } else {
        array->erase(array->begin() + static_cast<size_t>(idx));
        pushStack(Value::makeBool(true));
      }
    } else if (container.isSetId()) {
      auto *set = heap_.set(container.asSetId());
      if (!set) {
        COMPILER_THROW("ARRAY_DEL unknown set id");
      }
      auto key = resolveKey(keyValue);
      if (!key) {
        COMPILER_THROW("ARRAY_DEL expects string/number key for set");
      }
      pushStack(Value::makeBool(set->erase(*key) > 0));
    } else {
      COMPILER_THROW("ARRAY_DEL expects array/set container");
    }
    break;
  }

  case OpCode::SET_DEL: {
    Value keyValue = popStack();
    Value setVal = popStack();
    if (!setVal.isSetId()) {
      COMPILER_THROW("SET_DEL expects set container");
    }
    auto *set = heap_.set(setVal.asSetId());
    if (!set) {
      COMPILER_THROW("SET_DEL unknown set id");
    }
    auto key = resolveKey(keyValue);
    if (!key) {
      COMPILER_THROW("SET_DEL expects string/number key");
    }
    pushStack(Value::makeBool(set->erase(*key) > 0));
    break;
  }

  // Array intrinsics (VM-level operations)
  case OpCode::ARRAY_POP: {
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_POP expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeNull());
    } else if (arr->frozen) {
      COMPILER_THROW("Cannot modify frozen array (tuple)");
    } else if (arr->empty()) {
      pushStack(Value::makeNull());
    } else {
      pushStack(arr->back());
      arr->pop_back();
    }
    break;
  }

  case OpCode::ARRAY_HAS: {
    Value value = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_HAS expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeBool(false));
    } else {
      bool found = false;
      for (const auto &elem : *arr) {
        if (valuesEqual(elem, value)) {
          found = true;
          break;
        }
      }
      pushStack(Value::makeBool(found));
    }
    break;
  }

  case OpCode::ARRAY_FIND: {
    Value value = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_FIND expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeInt(-1));
    } else {
      int64_t foundIdx = -1;
      for (size_t i = 0; i < arr->size(); i++) {
        if (valuesEqual((*arr)[i], value)) {
          foundIdx = static_cast<int64_t>(i);
          break;
        }
      }
      pushStack(Value::makeInt(foundIdx));
    }
    break;
  }

  // Array higher-order functions (VM intrinsics)
  case OpCode::ARRAY_MAP: {
    Value fn = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_MAP expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeNull());
      break;
    }
    if (!fn.isFunctionObjId() && !fn.isClosureId()) {
      COMPILER_THROW("ARRAY_MAP expects function/closure");
    }

    auto resultRef = heap_.allocateArray();
    auto *result = heap_.array(resultRef.id);
    uint64_t resultRootId = pinExternalRoot(Value::makeArrayId(resultRef.id));

    for (size_t i = 0; i < arr->size(); i++) {
      Value mapped = callFunctionSync(fn, {(*arr)[i]});
      result->push_back(mapped);
    }

    unpinExternalRoot(resultRootId);
    pushStack(Value::makeArrayId(resultRef.id));
    break;
  }

  case OpCode::ARRAY_FILTER: {
    Value fn = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_FILTER expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeNull());
      break;
    }

    auto resultRef = heap_.allocateArray();
    auto *result = heap_.array(resultRef.id);
    uint64_t resultRootId = pinExternalRoot(Value::makeArrayId(resultRef.id));

    for (size_t i = 0; i < arr->size(); i++) {
      Value predResult = callFunctionSync(fn, {(*arr)[i]});
      if (predResult.isBool() && predResult.asBool()) {
        result->push_back((*arr)[i]);
      }
    }

    unpinExternalRoot(resultRootId);
    pushStack(Value::makeArrayId(resultRef.id));
    break;
  }

  case OpCode::ARRAY_REDUCE: {
    Value initial = popStack();
    Value fn = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_REDUCE expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(initial);
      break;
    }

    Value acc = initial;
    for (size_t i = 0; i < arr->size(); i++) {
      acc = callFunctionSync(fn, {acc, (*arr)[i]});
    }

    pushStack(acc);
    break;
  }

  case OpCode::ARRAY_FOREACH: {
    Value fn = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_FOREACH expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeNull());
      break;
    }

    for (size_t i = 0; i < arr->size(); i++) {
      (void)callFunctionSync(fn, {(*arr)[i]});
    }

    pushStack(Value::makeNull());
    break;
  }

  // String intrinsics (VM-level operations)
  case OpCode::STRING_LEN: {
    Value str = popStack();
    pushStack(Value::makeInt(static_cast<int64_t>(toString(str).size())));
    break;
  }

  case OpCode::STRING_UPPER: {
    Value str = popStack();
    std::string s = toString(str);
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    auto ref = createRuntimeString(std::move(s));
    pushStack(Value::makeStringId(ref.id));
    break;
  }

  case OpCode::STRING_LOWER: {
    Value str = popStack();
    std::string s = toString(str);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    auto ref = createRuntimeString(std::move(s));
    pushStack(Value::makeStringId(ref.id));
    break;
  }

  case OpCode::STRING_TRIM: {
    Value str = popStack();
    std::string s = toString(str);
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
      auto ref = createRuntimeString("");
      pushStack(Value::makeStringId(ref.id));
    } else {
      size_t end = s.find_last_not_of(" \t\n\r");
      auto ref = createRuntimeString(s.substr(start, end - start + 1));
      pushStack(Value::makeStringId(ref.id));
    }
    break;
  }

  case OpCode::STRING_HAS: {
    Value substr = popStack();
    Value str = popStack();
    if (!str.isStringValId() || !substr.isStringValId()) {
      COMPILER_THROW("STRING_HAS expects strings");
    }
    if (!current_chunk) {
      COMPILER_THROW("STRING_HAS: no current chunk");
    }
    const std::string &s = current_chunk->getString(str.asStringValId());
    const std::string &sub = current_chunk->getString(substr.asStringValId());
    pushStack(Value::makeBool(s.find(sub) != std::string::npos));
    break;
  }

  case OpCode::STRING_STARTS: {
    Value prefix = popStack();
    Value str = popStack();
    if (!str.isStringValId() || !prefix.isStringValId()) {
      COMPILER_THROW("STRING_STARTS expects strings");
    }
    if (!current_chunk) {
      COMPILER_THROW("STRING_STARTS: no current chunk");
    }
    const std::string &s = current_chunk->getString(str.asStringValId());
    const std::string &pre = current_chunk->getString(prefix.asStringValId());
    pushStack(Value::makeBool(s.size() >= pre.size() &&
                       s.compare(0, pre.size(), pre) == 0));
    break;
  }

  case OpCode::STRING_ENDS: {
    Value suffix = popStack();
    Value str = popStack();
    if (!str.isStringValId() || !suffix.isStringValId()) {
      COMPILER_THROW("STRING_ENDS expects strings");
    }
    if (!current_chunk) {
      COMPILER_THROW("STRING_ENDS: no current chunk");
    }
    const std::string &s = current_chunk->getString(str.asStringValId());
    const std::string &suf = current_chunk->getString(suffix.asStringValId());
    pushStack(Value::makeBool(s.size() >= suf.size() &&
                       s.compare(s.size() - suf.size(), suf.size(), suf) == 0));
    break;
  }

  // Spread operator - spread array elements
  case OpCode::SPREAD: {
    Value value = popStack();
    if (value.isArrayId()) {
      auto arrRef = ArrayRef{value.asArrayId()};
      auto *arr = heap_.array(arrRef.id);
      if (arr) {
        // Push each element individually
        for (auto &elem : *arr) {
          pushStack(elem);
        }
      }
    } else if (value.isStringValId()) {
      // Spread string into characters
      // TODO: string pool lookup
      std::string str = "<string:" + std::to_string(value.asStringValId()) + ">";
      auto arrRef = heap_.allocateArray();
      auto *arr = heap_.array(arrRef.id);
      if (arr) {
        for (char c : str) {
          // TODO: string pool registration
          arr->push_back(Value::makeNull());
        }
        pushStack(Value::makeArrayId(arrRef.id));
      }
    }
    break;
  }

  // Spread in function call
  case OpCode::SPREAD_CALL: {
    // Similar to SPREAD but marks arguments for spread in CALL
    Value value = popStack();
    if (value.isArrayId()) {
      auto arrRef = ArrayRef{value.asArrayId()};
      auto *arr = heap_.array(arrRef.id);
      if (arr) {
        for (auto &elem : *arr) {
          pushStack(elem);
        }
      }
    }
    break;
  }

	default:
		return false;
	}
	return true;
}

} // namespace havel::compiler
