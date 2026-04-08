#include "PrototypeRegistry.hpp"

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
}

} // namespace havel::compiler::prototypes
