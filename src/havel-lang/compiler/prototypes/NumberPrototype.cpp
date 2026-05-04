#include "PrototypeRegistry.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace havel::compiler::prototypes {

// Helper: extract string from args[i] with fallback
static std::string extractStringArg(VM& vm, const std::vector<Value>& args, size_t i, const std::string& fallback) {
  if (i >= args.size()) return fallback;
  if (args[i].isStringValId() && vm.getCurrentChunk()) return vm.getCurrentChunk()->getString(args[i].asStringValId());
  if (args[i].isStringId() && vm.getHeap().string(args[i].asStringId())) return *vm.getHeap().string(args[i].asStringId());
  return fallback;
}

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

  regIntProto("op_add", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeInt(a.asInt() + b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeDouble(av + bv);
  });

  regIntProto("op_sub", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeInt(a.asInt() - b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeDouble(av - bv);
  });

  regIntProto("op_mul", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeInt(a.asInt() * b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeDouble(av * bv);
  });

  regIntProto("op_div", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) {
      if (b.asInt() == 0) return Value::makeDouble(std::numeric_limits<double>::infinity());
      if (a.asInt() % b.asInt() == 0) return Value::makeInt(a.asInt() / b.asInt());
      return Value::makeDouble(static_cast<double>(a.asInt()) / static_cast<double>(b.asInt()));
    }
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeDouble(av / bv);
  });

  regIntProto("op_mod", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeInt(a.asInt() % b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeDouble(std::fmod(av, bv));
  });

  regIntProto("op_pow", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    auto a = args[0], b = args[1];
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeDouble(std::pow(av, bv));
  });

  regIntProto("op_neg", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    auto a = args[0];
    if (a.isInt()) return Value::makeInt(-a.asInt());
    return Value::makeDouble(-(a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt())));
  });

  regIntProto("op_eq", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeBool(a.asInt() == b.asInt());
    if (a.isDouble() && b.isDouble()) return Value::makeBool(a.asDouble() == b.asDouble());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeBool(av == bv);
  });

  regIntProto("op_ne", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(true);
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeBool(a.asInt() != b.asInt());
    if (a.isDouble() && b.isDouble()) return Value::makeBool(a.asDouble() != b.asDouble());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeBool(av != bv);
  });

  regIntProto("op_lt", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeBool(a.asInt() < b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeBool(av < bv);
  });

  regIntProto("op_gt", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeBool(a.asInt() > b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeBool(av > bv);
  });

  regIntProto("op_le", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeBool(a.asInt() <= b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeBool(av <= bv);
  });

  regIntProto("op_ge", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeBool(a.asInt() >= b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeBool(av >= bv);
  });

  // Float methods
  auto regFloatProto = [&vm](const std::string& method, size_t arity, BytecodeHostFunction fn) {
    vm.registerHostFunction("float." + method, arity, std::move(fn));
    vm.registerPrototypeMethodByName("float", method, "float." + method);
  };

  regFloatProto("op_add", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeInt(a.asInt() + b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeDouble(av + bv);
  });

  regFloatProto("op_sub", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeInt(a.asInt() - b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeDouble(av - bv);
  });

  regFloatProto("op_mul", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeInt(a.asInt() * b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeDouble(av * bv);
  });

  regFloatProto("op_div", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    auto a = args[0], b = args[1];
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeDouble(av / bv);
  });

  regFloatProto("op_mod", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    auto a = args[0], b = args[1];
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeDouble(std::fmod(av, bv));
  });

  regFloatProto("op_pow", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    auto a = args[0], b = args[1];
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeDouble(std::pow(av, bv));
  });

  regFloatProto("op_neg", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    auto a = args[0];
    if (a.isInt()) return Value::makeInt(-a.asInt());
    return Value::makeDouble(-(a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt())));
  });

  regFloatProto("op_eq", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeBool(a.asInt() == b.asInt());
    if (a.isDouble() && b.isDouble()) return Value::makeBool(a.asDouble() == b.asDouble());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeBool(av == bv);
  });

  regFloatProto("op_ne", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(true);
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeBool(a.asInt() != b.asInt());
    if (a.isDouble() && b.isDouble()) return Value::makeBool(a.asDouble() != b.asDouble());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeBool(av != bv);
  });

  regFloatProto("op_lt", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeBool(a.asInt() < b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeBool(av < bv);
  });

  regFloatProto("op_gt", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeBool(a.asInt() > b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeBool(av > bv);
  });

  regFloatProto("op_le", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeBool(a.asInt() <= b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeBool(av <= bv);
  });

  regFloatProto("op_ge", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    auto a = args[0], b = args[1];
    if (a.isInt() && b.isInt()) return Value::makeBool(a.asInt() >= b.asInt());
    double av = a.isDouble() ? a.asDouble() : static_cast<double>(a.asInt());
    double bv = b.isDouble() ? b.asDouble() : static_cast<double>(b.asInt());
    return Value::makeBool(av >= bv);
  });

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

  regProto("has", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        std::string key = extractStringArg(vm, args, 1, "");
        return Value::makeBool(obj->find(key) != obj->end());
      }
    }
    return Value::makeBool(false);
  });

  regProto("get", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        std::string key = extractStringArg(vm, args, 1, "");
        auto it = obj->find(key);
        if (it != obj->end()) return it->second;
      }
    }
    return Value::makeNull();
  });

  regProto("set", 3, [&vm](const std::vector<Value>& args) {
    if (args.size() < 3) return Value::makeNull();
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        std::string key = extractStringArg(vm, args, 1, "");
        obj->set(key, args[2]);
        return args[0];
      }
    }
    return Value::makeNull();
  });

  regProto("delete", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        std::string key = extractStringArg(vm, args, 1, "");
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

  // len: {a:1, b:2}.len() -> 2
  regProto("len", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeInt(0);
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      return Value::makeInt(obj ? static_cast<int64_t>(obj->size()) : 0);
    }
    return Value::makeInt(0);
  });

  // empty: {a:1, b:2}.empty() -> false
  regProto("empty", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeBool(true);
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      return Value::makeBool(!obj || obj->size() == 0);
    }
    return Value::makeBool(true);
  });

  // map: {a:1, b:2}.map((k) => k) -> ["a", "b"]
  // Iterates keys, calls fn(key), returns array of results
  regProto("map", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (!args[0].isObjectId()) return Value::makeNull();
    if (!(args[1].isFunctionObjId() || args[1].isClosureId())) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    auto arrRef = vm.getHeap().allocateArray();
    auto* arr = vm.getHeap().array(arrRef.id);
    for (const auto& [key, val] : *obj) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      auto kRef = vm.getHeap().allocateString(key);
      auto mapped = vm.call(args[1], {Value::makeStringId(kRef.id), val});
      arr->push_back(mapped);
    }
    return Value::makeArrayId(arrRef.id);
  });

  // find: {a:1, b:2}.find("a") -> 0, .find("b") -> 1, .find("c") -> -1
  regProto("find", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeInt(-1);
    if (!args[0].isObjectId()) return Value::makeInt(-1);
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeInt(-1);
    std::string key = extractStringArg(vm, args, 1, "");
    int64_t idx = 0;
    for (const auto& [k, v] : *obj) {
      if (k == "__set_marker__" || k == "__proto__") continue;
      if (k == key) return Value::makeInt(idx);
      ++idx;
    }
    return Value::makeInt(-1);
  });

  // filter: {a:1, b:2}.filter((k, v) => v > 1) -> {b: 2}
  regProto("filter", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (!args[0].isObjectId()) return Value::makeNull();
    if (!(args[1].isFunctionObjId() || args[1].isClosureId())) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    auto resultRef = vm.getHeap().allocateObject();
    auto* result = vm.getHeap().object(resultRef.id);
    for (const auto& [key, val] : *obj) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      auto kRef = vm.getHeap().allocateString(key);
      auto predResult = vm.call(args[1], {Value::makeStringId(kRef.id), val});
      if (vm.toBoolPublic(predResult)) result->set(key, val);
    }
  return Value::makeObjectId(resultRef.id);
 });

  // extend: {a:1}.extend({b: 2}) -> {a:1, b:2} (mutate receiver)
  regProto("extend", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (!args[0].isObjectId() || !args[1].isObjectId()) return Value::makeNull();
    auto* target = vm.getHeap().object(args[0].asObjectId());
    auto* source = vm.getHeap().object(args[1].asObjectId());
    if (target && source) {
      for (const auto& [key, val] : *source) {
        if (key == "__set_marker__" || key == "__proto__") continue;
        target->set(key, val);
      }
    }
    return args[0];
  });

  // each: {a:1, b:2}.each((k, v) => ...) -> null
  regProto("each", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (!args[0].isObjectId()) return Value::makeNull();
    if (!(args[1].isFunctionObjId() || args[1].isClosureId())) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    for (const auto& [key, val] : *obj) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      auto kRef = vm.getHeap().allocateString(key);
      vm.call(args[1], {Value::makeStringId(kRef.id), val});
    }
    return Value::makeNull();
  });

  // reduce: {a:1, b:2}.reduce((acc, k, v) => ..., init) -> result
  regProto("reduce", 3, [&vm](const std::vector<Value>& args) {
    if (args.size() < 3) return Value::makeNull();
    if (!args[0].isObjectId()) return args.size() >= 3 ? args[2] : Value::makeNull();
    if (!(args[1].isFunctionObjId() || args[1].isClosureId())) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return args[2];
    Value acc = args[2];
    for (const auto& [key, val] : *obj) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      auto kRef = vm.getHeap().allocateString(key);
      acc = vm.call(args[1], {acc, Value::makeStringId(kRef.id), val});
    }
    return acc;
  });
}

void registerSetPrototype(VM& vm) {
  auto regProto = [&vm](const std::string& method, size_t arity, BytecodeHostFunction fn) {
    vm.registerHostFunction("set." + method, arity, std::move(fn));
    vm.registerPrototypeMethodByName("set", method, "set." + method);
  };

  regProto("len", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeInt(0);
    if (args[0].isSetId()) {
      auto* set = vm.getHeap().set(args[0].asSetId());
      return Value::makeInt(set ? static_cast<int64_t>(set->size()) : 0);
    }
    return Value::makeInt(0);
  });

  regProto("has", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    if (args[0].isSetId()) {
      auto* set = vm.getHeap().set(args[0].asSetId());
      if (set) {
        std::string key;
        if (args[1].isInt()) {
          key = std::to_string(args[1].asInt());
        } else if (args[1].isStringValId() && vm.getCurrentChunk()) {
          key = vm.getCurrentChunk()->getString(args[1].asStringValId());
        } else if (args[1].isStringId() && vm.getHeap().string(args[1].asStringId())) {
          key = *vm.getHeap().string(args[1].asStringId());
        } else {
          return Value::makeBool(false);
        }
        return Value::makeBool(set->find(key) != set->end());
      }
    }
    return Value::makeBool(false);
  });

  regProto("add", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isSetId()) {
      auto* set = vm.getHeap().set(args[0].asSetId());
      if (set) {
        std::string key;
        if (args[1].isInt()) {
          key = std::to_string(args[1].asInt());
        } else if (args[1].isStringValId() && vm.getCurrentChunk()) {
          key = vm.getCurrentChunk()->getString(args[1].asStringValId());
        } else if (args[1].isStringId() && vm.getHeap().string(args[1].asStringId())) {
          key = *vm.getHeap().string(args[1].asStringId());
        } else {
          return Value::makeNull();
        }
        (*set)[key] = Value::makeBool(true);
        return args[0];
      }
    }
    return Value::makeNull();
  });

  regProto("delete", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    if (args[0].isSetId()) {
      auto* set = vm.getHeap().set(args[0].asSetId());
      if (set) {
        std::string key;
        if (args[1].isInt()) {
          key = std::to_string(args[1].asInt());
        } else if (args[1].isStringValId() && vm.getCurrentChunk()) {
          key = vm.getCurrentChunk()->getString(args[1].asStringValId());
        } else if (args[1].isStringId() && vm.getHeap().string(args[1].asStringId())) {
          key = *vm.getHeap().string(args[1].asStringId());
        } else {
          return Value::makeBool(false);
        }
        return Value::makeBool(set->erase(key) > 0);
      }
    }
    return Value::makeBool(false);
  });

  regProto("list", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    if (args[0].isSetId()) {
      auto* set = vm.getHeap().set(args[0].asSetId());
      if (set) {
        auto arrRef = vm.getHeap().allocateArray();
        auto* arr = vm.getHeap().array(arrRef.id);
        for (const auto& [key, val] : *set) {
          // Try to parse as int, otherwise use string
          try {
            arr->push_back(Value::makeInt(std::stoll(key)));
          } catch (...) {
            auto ref = vm.getHeap().allocateString(key);
            arr->push_back(Value::makeStringId(ref.id));
          }
        }
        return Value::makeArrayId(arrRef.id);
      }
    }
    return Value::makeNull();
  });

  // empty: {1,2,3}.empty() -> false
  regProto("empty", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeBool(true);
    if (args[0].isSetId()) {
      auto* set = vm.getHeap().set(args[0].asSetId());
      return Value::makeBool(!set || set->empty());
    }
    return Value::makeBool(true);
  });

  // union: {1,2,3}.union({3,4,5}) -> {1,2,3,4,5}
  regProto("union", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isSetId() && args[1].isSetId()) {
      auto* s1 = vm.getHeap().set(args[0].asSetId());
      auto* s2 = vm.getHeap().set(args[1].asSetId());
      if (s1 && s2) {
        auto resultRef = vm.getHeap().allocateSet();
        auto* result = vm.getHeap().set(resultRef.id);
        for (const auto& [k, v] : *s1) (*result)[k] = v;
        for (const auto& [k, v] : *s2) (*result)[k] = v;
        return Value::makeSetId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  // intersection: {1,2,3}.intersection({2,3,4}) -> {2,3}
  regProto("intersection", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isSetId() && args[1].isSetId()) {
      auto* s1 = vm.getHeap().set(args[0].asSetId());
      auto* s2 = vm.getHeap().set(args[1].asSetId());
      if (s1 && s2) {
        auto resultRef = vm.getHeap().allocateSet();
        auto* result = vm.getHeap().set(resultRef.id);
        for (const auto& [k, v] : *s1) {
          if (s2->find(k) != s2->end()) (*result)[k] = v;
        }
        return Value::makeSetId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  // difference: {1,2,3}.difference({2,3}) -> {1}
  regProto("difference", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isSetId() && args[1].isSetId()) {
      auto* s1 = vm.getHeap().set(args[0].asSetId());
      auto* s2 = vm.getHeap().set(args[1].asSetId());
      if (s1 && s2) {
        auto resultRef = vm.getHeap().allocateSet();
        auto* result = vm.getHeap().set(resultRef.id);
        for (const auto& [k, v] : *s1) {
          if (s2->find(k) == s2->end()) (*result)[k] = v;
        }
        return Value::makeSetId(resultRef.id);
      }
    }
    return Value::makeNull();
  });

  // isSubsetOf: {1,2,3}.isSubsetOf({1,2,3,4}) -> true
  regProto("isSubsetOf", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    if (args[0].isSetId() && args[1].isSetId()) {
      auto* s1 = vm.getHeap().set(args[0].asSetId());
      auto* s2 = vm.getHeap().set(args[1].asSetId());
      if (s1 && s2) {
        for (const auto& [k, v] : *s1) {
          if (s2->find(k) == s2->end()) return Value::makeBool(false);
        }
        return Value::makeBool(true);
      }
    }
    return Value::makeBool(false);
  });
}

} // namespace havel::compiler::prototypes
