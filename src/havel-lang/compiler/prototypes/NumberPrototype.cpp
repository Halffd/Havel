#include "PrototypeRegistry.hpp"
#include <cmath>
#include <algorithm>

namespace havel::compiler::prototypes {

void registerNumberPrototype(VM& vm) {
  // Integer methods
  auto regIntProto = [&vm](const std::string& method, size_t arity, BytecodeHostFunction fn) {
    vm.registerHostFunction("int." + method, arity, std::move(fn));
    vm.registerPrototypeMethodByName("int", method, "int." + method);
  };

  regIntProto("toHex", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    int64_t v = args[0].isInt() ? args[0].asInt() : (args[0].isDouble() ? static_cast<int64_t>(args[0].asDouble()) : 0);
    std::ostringstream oss;
    oss << std::hex << v;
    auto ref = vm.getHeap().allocateString(oss.str());
    return Value::makeStringId(ref.id);
  });

  regIntProto("toBin", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    int64_t v = args[0].isInt() ? args[0].asInt() : (args[0].isDouble() ? static_cast<int64_t>(args[0].asDouble()) : 0);
    std::string result;
    for (int i = 63; i >= 0; --i) {
      if (v & (1LL << i)) { result += '1'; } else if (!result.empty()) { result += '0'; }
    }
    if (result.empty()) result = "0";
    auto ref = vm.getHeap().allocateString(std::move(result));
    return Value::makeStringId(ref.id);
  });

  // Float methods
  auto regFloatProto = [&vm](const std::string& method, size_t arity, BytecodeHostFunction fn) {
    vm.registerHostFunction("float." + method, arity, std::move(fn));
    vm.registerPrototypeMethodByName("float", method, "float." + method);
  };

  regFloatProto("round", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    double v = args[0].isDouble() ? args[0].asDouble() : (args[0].isInt() ? static_cast<double>(args[0].asInt()) : 0.0);
    return Value::makeInt(static_cast<int64_t>(std::round(v)));
  });

  regFloatProto("floor", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    double v = args[0].isDouble() ? args[0].asDouble() : (args[0].isInt() ? static_cast<double>(args[0].asInt()) : 0.0);
    return Value::makeInt(static_cast<int64_t>(std::floor(v)));
  });

  regFloatProto("ceil", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    double v = args[0].isDouble() ? args[0].asDouble() : (args[0].isInt() ? static_cast<double>(args[0].asInt()) : 0.0);
    return Value::makeInt(static_cast<int64_t>(std::ceil(v)));
  });
}

void registerBoolPrototype(VM& vm) {
  auto regProto = [&vm](const std::string& method, size_t arity, BytecodeHostFunction fn) {
    vm.registerHostFunction("bool." + method, arity, std::move(fn));
    vm.registerPrototypeMethodByName("bool", method, "bool." + method);
  };

  regProto("and", 2, [&vm](const std::vector<Value>& args) {
    return Value::makeBool(args.size() >= 2 && vm.toBoolPublic(args[0]) && vm.toBoolPublic(args[1]));
  });

  regProto("or", 2, [&vm](const std::vector<Value>& args) {
    return Value::makeBool(args.size() >= 2 && (vm.toBoolPublic(args[0]) || vm.toBoolPublic(args[1])));
  });

  regProto("not", 1, [&vm](const std::vector<Value>& args) {
    return Value::makeBool(args.empty() || !vm.toBoolPublic(args[0]));
  });
}

void registerObjectPrototype(VM& vm) {
  auto regProto = [&vm](const std::string& method, size_t arity, BytecodeHostFunction fn) {
    vm.registerHostFunction("object." + method, arity, std::move(fn));
    vm.registerPrototypeMethodByName("object", method, "object." + method);
  };

  auto getStringArg = [&vm](const std::vector<Value>& args, size_t i, const std::string& fallback) -> std::string {
    if (i >= args.size()) return fallback;
    if (args[i].isStringValId() && vm.getCurrentChunk()) return vm.getCurrentChunk()->getString(args[i].asStringValId());
    if (args[i].isStringId() && vm.getHeap().string(args[i].asStringId())) return *vm.getHeap().string(args[i].asStringId());
    return fallback;
  };

  regProto("keys", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        auto arrRef = vm.getHeap().allocateArray();
        auto* arr = vm.getHeap().array(arrRef.id);
        auto keys = obj->getKeys();
        for (const auto& key : keys) {
          auto ref = vm.getHeap().allocateString(key);
          arr->push_back(Value::makeStringId(ref.id));
        }
        return Value::makeArrayId(arrRef.id);
      }
    }
    return Value::makeNull();
  });

  regProto("values", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        auto arrRef = vm.getHeap().allocateArray();
        auto* arr = vm.getHeap().array(arrRef.id);
        for (const auto& [key, val] : *obj) {
          arr->push_back(val);
        }
        return Value::makeArrayId(arrRef.id);
      }
    }
    return Value::makeNull();
  });

  regProto("entries", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        auto arrRef = vm.getHeap().allocateArray();
        auto* arr = vm.getHeap().array(arrRef.id);
        for (const auto& [key, val] : *obj) {
          auto entryRef = vm.getHeap().allocateArray();
          auto* entry = vm.getHeap().array(entryRef.id);
          auto kRef = vm.getHeap().allocateString(key);
          entry->push_back(Value::makeStringId(kRef.id));
          entry->push_back(val);
          arr->push_back(Value::makeArrayId(entryRef.id));
        }
        return Value::makeArrayId(arrRef.id);
      }
    }
    return Value::makeNull();
  });

  regProto("has", 2, [&vm, &getStringArg](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        std::string key = getStringArg(args, 1, "");
        return Value::makeBool(obj->find(key) != obj->end());
      }
    }
    return Value::makeBool(false);
  });

  regProto("get", 2, [&vm, &getStringArg](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        std::string key = getStringArg(args, 1, "");
        auto it = obj->find(key);
        if (it != obj->end()) return it->second;
      }
    }
    return Value::makeNull();
  });

  regProto("set", 3, [&vm, &getStringArg](const std::vector<Value>& args) {
    if (args.size() < 3) return Value::makeNull();
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        std::string key = getStringArg(args, 1, "");
        obj->set(key, args[2]);
        return args[0];
      }
    }
    return Value::makeNull();
  });

  regProto("delete", 2, [&vm, &getStringArg](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        std::string key = getStringArg(args, 1, "");
        return Value::makeBool(obj->erase(key) > 0);
      }
    }
    return Value::makeBool(false);
  });

  regProto("merge", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isObjectId() && args[1].isObjectId()) {
      auto* obj1 = vm.getHeap().object(args[0].asObjectId());
      auto* obj2 = vm.getHeap().object(args[1].asObjectId());
      if (obj1 && obj2) {
        auto resultRef = vm.getHeap().allocateObject();
        auto* result = vm.getHeap().object(resultRef.id);
        for (const auto& [key, val] : *obj1) result->set(key, val);
        for (const auto& [key, val] : *obj2) result->set(key, val);
        return Value::makeObjectId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  regProto("sortVal", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isObjectId() && (args[1].isFunctionObjId() || args[1].isClosureId())) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        std::vector<std::pair<std::string, Value>> entries(obj->begin(), obj->end());
        std::sort(entries.begin(), entries.end(), [&vm, &args](const auto& a, const auto& b) {
          auto result = vm.call(args[1], {a.second, b.second});
          return result.isInt() && result.asInt() < 0;
        });
        auto resultRef = vm.getHeap().allocateObject();
        auto* result = vm.getHeap().object(resultRef.id);
        for (const auto& [key, val] : entries) result->set(key, val);
        return Value::makeObjectId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  regProto("sortKey", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isObjectId() && (args[1].isFunctionObjId() || args[1].isClosureId())) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        std::vector<std::pair<std::string, Value>> entries(obj->begin(), obj->end());
        std::sort(entries.begin(), entries.end(), [&vm, &args](const auto& a, const auto& b) {
          auto keyARef = vm.getHeap().allocateString(a.first);
          auto keyA = Value::makeStringId(keyARef.id);
          auto keyBRef = vm.getHeap().allocateString(b.first);
          auto keyB = Value::makeStringId(keyBRef.id);
          auto result = vm.call(args[1], {keyA, keyB});
          return result.isInt() && result.asInt() < 0;
        });
        auto resultRef = vm.getHeap().allocateObject();
        auto* result = vm.getHeap().object(resultRef.id);
        for (const auto& [key, val] : entries) result->set(key, val);
        return Value::makeObjectId(resultRef.id);
      }
    }
    return Value::makeNull();
  });
}

} // namespace havel::compiler::prototypes
