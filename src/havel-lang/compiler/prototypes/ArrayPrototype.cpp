#include "PrototypeRegistry.hpp"
#include <algorithm>
#include <cmath>

namespace havel::compiler::prototypes {

void registerArrayPrototype(VM& vm) {
  auto regProto = [&vm](const std::string& method, size_t arity, BytecodeHostFunction fn) {
    vm.registerHostFunction("array." + method, arity, std::move(fn));
    vm.registerPrototypeMethodByName("array", method, "array." + method);
  };

  // Helper for variable-arity methods (no strict arity check)
  auto regProtoVar = [&vm](const std::string& method, BytecodeHostFunction fn) {
    vm.registerHostFunction("array." + method, std::move(fn));
    vm.registerPrototypeMethodByName("array", method, "array." + method);
  };

  regProto("len", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeInt(0);
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      return Value::makeInt(arr ? static_cast<int64_t>(arr->size()) : 0);
    }
    return Value::makeInt(0);
  });

  regProto("push", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr && !arr->frozen) {
        arr->push_back(args[1]);
        return Value::makeInt(static_cast<int64_t>(arr->size()));
      }
    }
    return Value::makeNull();
  });

  regProto("pop", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr && !arr->frozen && !arr->empty()) {
        auto val = arr->back(); arr->pop_back(); return val;
      }
    }
    return Value::makeNull();
  });

  regProto("unshift", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr && !arr->frozen) { arr->insert(arr->begin(), args[1]); return Value::makeInt(static_cast<int64_t>(arr->size())); }
    }
    return Value::makeNull();
  });

  regProto("shift", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr && !arr->frozen && !arr->empty()) { auto val = arr->front(); arr->erase(arr->begin()); return val; }
    }
    return Value::makeNull();
  });

  regProto("insert", 3, [&vm](const std::vector<Value>& args) {
    if (args.size() < 3) return Value::makeNull();
    if (args[0].isArrayId() && args[1].isInt()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr && !arr->frozen) {
        int64_t idx = args[1].asInt();
        if (idx < 0) idx = std::max(static_cast<int64_t>(0), static_cast<int64_t>(arr->size()) + idx + 1);
        if (idx >= 0 && static_cast<size_t>(idx) <= arr->size()) {
          arr->insert(arr->begin() + idx, args[2]);
          return Value::makeInt(static_cast<int64_t>(arr->size()));
        }
      }
    }
    return Value::makeNull();
  });

  regProto("has", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        for (const auto& v : *arr) {
          if (vm.valuesEqualDeepPublic(v, args[1])) return Value::makeBool(true);
        }
      }
    }
    return Value::makeBool(false);
  });

  regProto("indexOf", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeInt(-1);
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        for (size_t i = 0; i < arr->size(); ++i) {
          if (vm.valuesEqualDeepPublic((*arr)[i], args[1])) return Value::makeInt(static_cast<int64_t>(i));
        }
      }
    }
    return Value::makeInt(-1);
  });

  regProto("find", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isArrayId() && (args[1].isFunctionObjId() || args[1].isClosureId())) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        for (const auto& v : *arr) {
          auto predResult = vm.call(args[1], {v});
          if (vm.toBoolPublic(predResult)) return v;
        }
      }
    }
    return Value::makeNull();
  });

  regProto("map", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2 || (!args[1].isFunctionObjId() && !args[1].isClosureId())) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        auto resultRef = vm.getHeap().allocateArray();
        auto* result = vm.getHeap().array(resultRef.id);
        for (const auto& v : *arr) {
          result->push_back(vm.call(args[1], {v}));
        }
        return Value::makeArrayId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  regProto("filter", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2 || (!args[1].isFunctionObjId() && !args[1].isClosureId())) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        auto resultRef = vm.getHeap().allocateArray();
        auto* result = vm.getHeap().array(resultRef.id);
        for (const auto& v : *arr) {
          auto predResult = vm.call(args[1], {v});
          if (vm.toBoolPublic(predResult)) result->push_back(v);
        }
        return Value::makeArrayId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  regProto("reduce", 3, [&vm](const std::vector<Value>& args) {
    if (args.size() < 3 || (!args[1].isFunctionObjId() && !args[1].isClosureId())) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr && !arr->empty()) {
        Value acc = args[2];
        for (const auto& v : *arr) {
          acc = vm.call(args[1], {acc, v});
        }
        return acc;
      }
    }
    return args.size() > 2 ? args[2] : Value::makeNull();
  });

  regProto("foreach", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2 || (!args[1].isFunctionObjId() && !args[1].isClosureId())) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        for (const auto& v : *arr) {
          vm.call(args[1], {v});
        }
      }
    }
    return Value::makeNull();
  });

  regProto("every", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2 || (!args[1].isFunctionObjId() && !args[1].isClosureId())) return Value::makeBool(false);
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        for (const auto& v : *arr) {
          auto predResult = vm.call(args[1], {v});
          if (!vm.toBoolPublic(predResult)) return Value::makeBool(false);
        }
        return Value::makeBool(true);
      }
    }
    return Value::makeBool(false);
  });

  regProto("some", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2 || (!args[1].isFunctionObjId() && !args[1].isClosureId())) return Value::makeBool(false);
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        for (const auto& v : *arr) {
          auto predResult = vm.call(args[1], {v});
          if (vm.toBoolPublic(predResult)) return Value::makeBool(true);
        }
      }
    }
    return Value::makeBool(false);
  });

  regProto("join", 2, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    std::string delim = ",";
    if (args.size() > 1 && args[1].isStringValId() && vm.getCurrentChunk()) delim = vm.getCurrentChunk()->getString(args[1].asStringValId());
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        std::string result;
        for (size_t i = 0; i < arr->size(); ++i) {
          if (i > 0) result += delim;
          result += vm.toString((*arr)[i]);
        }
        auto ref = vm.getHeap().allocateString(std::move(result));
        return Value::makeStringId(ref.id);
      }
    }
    return Value::makeNull();
  });

  regProto("concat", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isArrayId() && args[1].isArrayId()) {
      auto* arr1 = vm.getHeap().array(args[0].asArrayId());
      auto* arr2 = vm.getHeap().array(args[1].asArrayId());
      if (arr1 && arr2) {
        auto resultRef = vm.getHeap().allocateArray();
        auto* result = vm.getHeap().array(resultRef.id);
        result->reserve(arr1->size() + arr2->size());
        result->insert(result->end(), arr1->begin(), arr1->end());
        result->insert(result->end(), arr2->begin(), arr2->end());
        return Value::makeArrayId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  regProto("slice", 4, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        int64_t sz = static_cast<int64_t>(arr->size());
        
        // Check which args are specified
        bool start_specified = args.size() > 1 && !args[1].isNull();
        bool end_specified = args.size() > 2 && !args[2].isNull();
        bool step_specified = args.size() > 3 && !args[3].isNull();
        
        // Parse step first (affects defaults for start/end)
        int64_t step = 1;
        if (step_specified) {
          step = args[3].isInt() ? args[3].asInt() : 1;
          if (step == 0) step = 1; // Avoid infinite loop
        }
        
        // Parse start with proper defaults
        int64_t start;
        if (start_specified) {
          start = args[1].isInt() ? args[1].asInt() : 0;
          if (start < 0) start = sz + start;
          if (start < 0) start = 0;
          if (start > sz) start = sz;
        } else {
          start = (step > 0) ? 0 : sz - 1;
        }
        
        // Parse end with proper defaults
        int64_t end;
        if (end_specified) {
          end = args[2].isInt() ? args[2].asInt() : sz;
          if (end < 0) end = sz + end;
          if (end < -1) end = -1;
          if (end > sz) end = sz;
        } else {
          end = (step > 0) ? sz : -1;
        }
        
        // Check bounds
        if (step > 0 && start >= end) { auto resultRef = vm.getHeap().allocateArray(); return Value::makeArrayId(resultRef.id); }
        if (step < 0 && start <= end) { auto resultRef = vm.getHeap().allocateArray(); return Value::makeArrayId(resultRef.id); }

        auto resultRef = vm.getHeap().allocateArray();
        auto* result = vm.getHeap().array(resultRef.id);

        if (step > 0) {
          for (int64_t i = start; i < end; i += step) {
            result->push_back((*arr)[static_cast<size_t>(i)]);
          }
        } else {
          for (int64_t i = start; i > end; i += step) {
            result->push_back((*arr)[static_cast<size_t>(i)]);
          }
        }
        return Value::makeArrayId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  regProto("reverse", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr && !arr->frozen) {
        std::reverse(arr->begin(), arr->end());
        return args[0];
      }
    }
    return Value::makeNull();
  });

  regProto("reversed", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        auto resultRef = vm.getHeap().allocateArray();
        auto* result = vm.getHeap().array(resultRef.id);
        result->reserve(arr->size());
        result->insert(result->end(), arr->rbegin(), arr->rend());
        return Value::makeArrayId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  regProtoVar("sort", [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr && !arr->empty()) {
        if (args.size() > 1 && (args[1].isFunctionObjId() || args[1].isClosureId())) {
          std::sort(arr->begin(), arr->end(), [&vm, &args](const Value& a, const Value& b) {
            auto result = vm.call(args[1], {a, b});
            return result.isInt() && result.asInt() < 0;
          });
        } else {
          std::sort(arr->begin(), arr->end(), [](const Value& a, const Value& b) {
            if (a.isInt() && b.isInt()) return a.asInt() < b.asInt();
            if (a.isDouble() && b.isDouble()) return a.asDouble() < b.asDouble();
            return false;
          });
        }
        return args[0];
      }
    }
    return Value::makeNull();
  });

  regProtoVar("sorted", [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        auto resultRef = vm.getHeap().allocateArray();
        auto* result = vm.getHeap().array(resultRef.id);
        result->assign(arr->begin(), arr->end());
        if (!result->empty()) {
          if (args.size() > 1 && (args[1].isFunctionObjId() || args[1].isClosureId())) {
            std::sort(result->begin(), result->end(), [&vm, &args](const Value& a, const Value& b) {
              auto res = vm.call(args[1], {a, b});
              return res.isInt() && res.asInt() < 0;
            });
          } else {
            std::sort(result->begin(), result->end(), [](const Value& a, const Value& b) {
              if (a.isInt() && b.isInt()) return a.asInt() < b.asInt();
              if (a.isDouble() && b.isDouble()) return a.asDouble() < b.asDouble();
              return false;
            });
          }
        }
        return Value::makeArrayId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  regProto("unique", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        auto resultRef = vm.getHeap().allocateArray();
        auto* result = vm.getHeap().array(resultRef.id);
        for (const auto& v : *arr) {
          bool found = false;
          for (const auto& r : *result) {
            if (vm.valuesEqualDeepPublic(v, r)) { found = true; break; }
          }
          if (!found) result->push_back(v);
        }
        return Value::makeArrayId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  // extend: [1,2,3].extend([4,5]) -> [1,2,3,4,5] (mutates)
  regProto("extend", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isArrayId() && args[1].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      auto* other = vm.getHeap().array(args[1].asArrayId());
      if (arr && !arr->frozen && other) {
        arr->insert(arr->end(), other->begin(), other->end());
        return args[0];
      }
    }
    return Value::makeNull();
  });

  // delete: [1,2,3].delete(2) -> [1,3] (removes first occurrence)
  regProto("delete", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr && !arr->frozen) {
        for (auto it = arr->begin(); it != arr->end(); ++it) {
          if (vm.valuesEqualDeepPublic(*it, args[1])) {
            arr->erase(it);
            return args[0];
          }
        }
      }
    }
    return Value::makeNull();
  });

  // clear: [1,2,3].clear() -> []
  regProto("clear", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr && !arr->frozen) {
        arr->clear();
      }
    }
    return args[0];
  });

  // clone: [1,2,3].clone() -> [1,2,3] (shallow copy)
  regProto("clone", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        auto resultRef = vm.getHeap().allocateArray();
        auto* result = vm.getHeap().array(resultRef.id);
        result->assign(arr->begin(), arr->end());
        return Value::makeArrayId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  // count: [1,2,2,3].count(2) -> 2
  regProto("count", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeInt(0);
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        int64_t count = 0;
        for (const auto& v : *arr) {
          if (vm.valuesEqualDeepPublic(v, args[1])) count++;
        }
        return Value::makeInt(count);
      }
    }
    return Value::makeInt(0);
  });

  // groupBy: [1,2,3].groupBy(x => x % 2) -> {"odd":[1,3], "even":[2]}
  regProto("groupBy", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2 || (!args[1].isFunctionObjId() && !args[1].isClosureId())) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        auto resultRef = vm.getHeap().allocateObject();
        auto* result = vm.getHeap().object(resultRef.id);
        for (const auto& v : *arr) {
          auto keyVal = vm.call(args[1], {v});
          std::string key;
          if (keyVal.isStringValId() && vm.getCurrentChunk()) {
            key = vm.getCurrentChunk()->getString(keyVal.asStringValId());
          } else if (keyVal.isStringId() && vm.getHeap().string(keyVal.asStringId())) {
            key = *vm.getHeap().string(keyVal.asStringId());
          } else if (keyVal.isInt()) {
            key = std::to_string(keyVal.asInt());
          } else {
            key = vm.toString(keyVal);
          }
          auto* bucket = result->get(key);
          if (!bucket || !bucket->isArrayId()) {
            auto arrRef = vm.getHeap().allocateArray();
            vm.getHeap().array(arrRef.id)->push_back(v);
            result->set(key, Value::makeArrayId(arrRef.id));
          } else {
            auto* bucketArr = vm.getHeap().array(bucket->asArrayId());
            if (bucketArr) bucketArr->push_back(v);
          }
        }
        return Value::makeObjectId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  // empty: [1,2,3].empty() -> false
  regProto("empty", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeBool(true);
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      return Value::makeBool(!arr || arr->empty());
    }
    return Value::makeBool(true);
  });

}

} // namespace havel::compiler::prototypes
