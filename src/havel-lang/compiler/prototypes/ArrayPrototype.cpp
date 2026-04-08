#include "PrototypeRegistry.hpp"
#include <algorithm>
#include <cmath>

namespace havel::compiler::prototypes {

void registerArrayPrototype(VM& vm) {
  auto regProto = [&vm](const std::string& method, size_t arity, BytecodeHostFunction fn) {
    vm.registerHostFunction("array." + method, arity, std::move(fn));
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
      if (arr) { arr->push_back(args[1]); return Value::makeInt(static_cast<int64_t>(arr->size())); }
    }
    return Value::makeNull();
  });

  regProto("pop", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr && !arr->empty()) { auto val = arr->back(); arr->pop_back(); return val; }
    }
    return Value::makeNull();
  });

  regProto("unshift", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) { arr->insert(arr->begin(), args[1]); return Value::makeInt(static_cast<int64_t>(arr->size())); }
    }
    return Value::makeNull();
  });

  regProto("shift", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr && !arr->empty()) { auto val = arr->front(); arr->erase(arr->begin()); return val; }
    }
    return Value::makeNull();
  });

  regProto("insert", 3, [&vm](const std::vector<Value>& args) {
    if (args.size() < 3) return Value::makeNull();
    if (args[0].isArrayId() && args[1].isInt()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
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

  regProto("slice", 3, [&vm](const std::vector<Value>& args) {
    if (args.size() < 3) return Value::makeNull();
    if (args[0].isArrayId() && args[1].isInt() && args[2].isInt()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
        int64_t start = args[1].asInt(), end = args[2].asInt();
        int64_t sz = static_cast<int64_t>(arr->size());
        if (start < 0) start = std::max(static_cast<int64_t>(0), sz + start);
        if (end < 0) end = sz + end;
        if (start < 0) start = 0;
        if (end > sz) end = sz;
        if (start >= end) { auto resultRef = vm.getHeap().allocateArray(); return Value::makeArrayId(resultRef.id); }
        auto resultRef = vm.getHeap().allocateArray();
        auto* result = vm.getHeap().array(resultRef.id);
        result->insert(result->end(), arr->begin() + start, arr->begin() + end);
        return Value::makeArrayId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  regProto("reverse", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isArrayId()) {
      auto* arr = vm.getHeap().array(args[0].asArrayId());
      if (arr) {
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

  regProto("sort", 2, [&vm](const std::vector<Value>& args) {
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

  regProto("sorted", 2, [&vm](const std::vector<Value>& args) {
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
}

} // namespace havel::compiler::prototypes
