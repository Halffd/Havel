#include "PrototypeRegistry.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <map>

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

  auto regProtoVar = [&vm](const std::string& method, BytecodeHostFunction fn) {
    vm.registerHostFunction("object." + method, std::move(fn));
    vm.registerPrototypeMethodByName("object", method, "object." + method);
  };

  regProto("clone", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    auto resultRef = vm.getHeap().allocateObject();
    auto* result = vm.getHeap().object(resultRef.id);
    for (const auto& [key, val] : *obj) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      result->set(key, val);
    }
    return Value::makeObjectId(resultRef.id);
  });

  regProto("isEmpty", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObjectId()) return Value::makeBool(true);
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    return Value::makeBool(!obj || obj->size() == 0);
  });

  regProto("freeze", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
    return args[0];
  });

  regProto("invert", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    auto resultRef = vm.getHeap().allocateObject();
    auto* result = vm.getHeap().object(resultRef.id);
    for (const auto& [key, val] : *obj) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      std::string valStr = vm.toString(val);
      result->set(valStr, [&vm, &key]() -> Value {
        auto kRef = vm.getHeap().allocateString(key);
        return Value::makeStringId(kRef.id);
      }());
    }
    return Value::makeObjectId(resultRef.id);
  });

  regProtoVar("pick", [&vm](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    auto resultRef = vm.getHeap().allocateObject();
    auto* result = vm.getHeap().object(resultRef.id);
    if (args.size() >= 2 && args[1].isArrayId()) {
      auto* keys = vm.getHeap().array(args[1].asArrayId());
      if (keys) {
        for (const auto& kv : *keys) {
          std::string keyStr = extractStringArg(vm, {kv}, 0, "");
          auto it = obj->find(keyStr);
          if (it != obj->end()) result->set(keyStr, it->second);
        }
      }
    }
    return Value::makeObjectId(resultRef.id);
  });

  regProtoVar("omit", [&vm](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    std::set<std::string> omitKeys;
    if (args.size() >= 2 && args[1].isArrayId()) {
      auto* keys = vm.getHeap().array(args[1].asArrayId());
      if (keys) {
        for (const auto& kv : *keys) {
          omitKeys.insert(extractStringArg(vm, {kv}, 0, ""));
        }
      }
    }
    auto resultRef = vm.getHeap().allocateObject();
    auto* result = vm.getHeap().object(resultRef.id);
    for (const auto& [key, val] : *obj) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      if (omitKeys.count(key)) continue;
      result->set(key, val);
    }
    return Value::makeObjectId(resultRef.id);
  });

  regProto("defaults", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].isObjectId() || !args[1].isObjectId()) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    auto* defs = vm.getHeap().object(args[1].asObjectId());
    if (!obj || !defs) return Value::makeNull();
    auto resultRef = vm.getHeap().allocateObject();
    auto* result = vm.getHeap().object(resultRef.id);
    for (const auto& [key, val] : *defs) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      if (obj->find(key) == obj->end()) result->set(key, val);
    }
    for (const auto& [key, val] : *obj) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      result->set(key, val);
    }
    return Value::makeObjectId(resultRef.id);
  });

  regProtoVar("rename", [&vm](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    auto resultRef = vm.getHeap().allocateObject();
    auto* result = vm.getHeap().object(resultRef.id);
    std::map<std::string, std::string> renameMap;
    if (args.size() >= 2 && args[1].isObjectId()) {
      auto* mapping = vm.getHeap().object(args[1].asObjectId());
      if (mapping) {
        for (const auto& [k, v] : *mapping) {
          if (k == "__set_marker__" || k == "__proto__") continue;
          renameMap[k] = extractStringArg(vm, {v}, 0, "");
        }
      }
    }
    for (const auto& [key, val] : *obj) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      std::string outKey = renameMap.count(key) ? renameMap[key] : key;
      result->set(outKey, val);
    }
    return Value::makeObjectId(resultRef.id);
  });

  regProtoVar("pickBy", [&vm](const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].isObjectId()) return Value::makeNull();
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

  regProtoVar("omitBy", [&vm](const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].isObjectId()) return Value::makeNull();
    if (!(args[1].isFunctionObjId() || args[1].isClosureId())) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    auto resultRef = vm.getHeap().allocateObject();
    auto* result = vm.getHeap().object(resultRef.id);
    for (const auto& [key, val] : *obj) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      auto kRef = vm.getHeap().allocateString(key);
      auto predResult = vm.call(args[1], {Value::makeStringId(kRef.id), val});
      if (!vm.toBoolPublic(predResult)) result->set(key, val);
    }
    return Value::makeObjectId(resultRef.id);
  });

  regProto("includes", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].isObjectId()) return Value::makeBool(false);
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeBool(false);
    std::string key = extractStringArg(vm, args, 1, "");
    return Value::makeBool(obj->find(key) != obj->end());
  });

  regProtoVar("count", [&vm](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObjectId()) return Value::makeInt(0);
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeInt(0);
    if (args.size() >= 2 && (args[1].isFunctionObjId() || args[1].isClosureId())) {
      int64_t c = 0;
      for (const auto& [key, val] : *obj) {
        if (key == "__set_marker__" || key == "__proto__") continue;
        auto kRef = vm.getHeap().allocateString(key);
        auto r = vm.call(args[1], {Value::makeStringId(kRef.id), val});
        if (vm.toBoolPublic(r)) ++c;
      }
      return Value::makeInt(c);
    }
    return Value::makeInt(static_cast<int64_t>(obj->size()));
  });

  regProto("sorted", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    std::vector<std::string> keys;
    for (const auto& [k, v] : *obj) {
      if (k != "__set_marker__" && k != "__proto__") keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    auto resultRef = vm.getHeap().allocateObject();
    auto* result = vm.getHeap().object(resultRef.id);
    for (const auto& key : keys) {
      auto it = obj->find(key);
      if (it != obj->end()) result->set(key, it->second);
    }
    return Value::makeObjectId(resultRef.id);
  });

  regProto("unique", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
    return args[0];
  });

  regProto("reversed", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    std::vector<std::pair<std::string, Value>> entries(obj->begin(), obj->end());
    std::reverse(entries.begin(), entries.end());
    auto resultRef = vm.getHeap().allocateObject();
    auto* result = vm.getHeap().object(resultRef.id);
    for (const auto& [key, val] : entries) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      result->set(key, val);
    }
    return Value::makeObjectId(resultRef.id);
  });

  regProto("foreach", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].isObjectId()) return Value::makeNull();
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

  regProto("flatten", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    auto resultRef = vm.getHeap().allocateObject();
    auto* result = vm.getHeap().object(resultRef.id);
    for (const auto& [key, val] : *obj) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      if (val.isObjectId()) {
        auto* inner = vm.getHeap().object(val.asObjectId());
        if (inner) {
          for (const auto& [ik, iv] : *inner) {
            if (ik == "__set_marker__" || ik == "__proto__") continue;
            result->set(key + "." + ik, iv);
          }
          continue;
        }
      }
      result->set(key, val);
    }
    return Value::makeObjectId(resultRef.id);
  });

  regProto("unflatten", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isObjectId()) return Value::makeNull();
    auto* obj = vm.getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    auto resultRef = vm.getHeap().allocateObject();
    auto* result = vm.getHeap().object(resultRef.id);
    for (const auto& [key, val] : *obj) {
      if (key == "__set_marker__" || key == "__proto__") continue;
      auto dotPos = key.find('.');
      if (dotPos == std::string::npos) {
        result->set(key, val);
      } else {
        std::string topKey = key.substr(0, dotPos);
        std::string subKey = key.substr(dotPos + 1);
        Value existing = result->get(topKey);
        if (!existing.isObjectId()) {
          auto innerRef = vm.getHeap().allocateObject();
          result->set(topKey, Value::makeObjectId(innerRef.id));
          auto* inner = vm.getHeap().object(innerRef.id);
          inner->set(subKey, val);
        } else {
          auto* inner = vm.getHeap().object(existing.asObjectId());
          if (inner) inner->set(subKey, val);
        }
      }
    }
    return Value::makeObjectId(resultRef.id);
  });

  regProto("assign", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].isObjectId() || !args[1].isObjectId()) return Value::makeNull();
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

    auto setKeyFromValue = [&vm](const Value& v) -> std::string {
        if (v.isInt()) return std::to_string(v.asInt());
        if (v.isStringValId() && vm.getCurrentChunk()) return vm.getCurrentChunk()->getString(v.asStringValId());
        if (v.isStringId() && vm.getHeap().string(v.asStringId())) return *vm.getHeap().string(v.asStringId());
        return vm.toString(v);
    };

    auto valueFromKey = [&vm](const std::string& key) -> Value {
        try { return Value::makeInt(std::stoll(key)); }
        catch (...) { auto ref = vm.getHeap().allocateString(key); return Value::makeStringId(ref.id); }
    };

  regProto("includes", 2, [&vm, &setKeyFromValue](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    if (args[0].isSetId()) {
      auto* set = vm.getHeap().set(args[0].asSetId());
      if (set) {
        std::string key = setKeyFromValue(args[1]);
                return Value::makeBool(set->find(key) != set->end());
            }
        }
        return Value::makeBool(false);
    });

    regProto("clone", 1, [&vm](const std::vector<Value>& args) {
        if (args.empty()) return Value::makeNull();
        if (args[0].isSetId()) {
            auto* s = vm.getHeap().set(args[0].asSetId());
            if (s) {
                auto resultRef = vm.getHeap().allocateSet();
                auto* result = vm.getHeap().set(resultRef.id);
                for (const auto& [k, v] : *s) (*result)[k] = v;
                return Value::makeSetId(resultRef.id);
            }
        }
        return Value::makeNull();
    });

  regProto("foreach", 2, [&vm, &valueFromKey](const std::vector<Value>& args) {
    if (args.size() < 2 || (!args[1].isFunctionObjId() && !args[1].isClosureId())) return Value::makeNull();
    if (args[0].isSetId()) {
      auto* set = vm.getHeap().set(args[0].asSetId());
      if (set) {
        for (const auto& [k, v] : *set) {
          vm.call(args[1], {valueFromKey(k)});
        }
      }
    }
    return Value::makeNull();
  });

  regProto("each", 2, [&vm, &valueFromKey](const std::vector<Value>& args) {
    if (args.size() < 2 || (!args[1].isFunctionObjId() && !args[1].isClosureId())) return Value::makeNull();
    if (args[0].isSetId()) {
      auto* set = vm.getHeap().set(args[0].asSetId());
      if (set) {
        for (const auto& [k, v] : *set) {
          vm.call(args[1], {valueFromKey(k)});
        }
      }
    }
    return Value::makeNull();
  });

    regProto("sorted", 1, [&vm, &valueFromKey](const std::vector<Value>& args) {
        if (args.empty()) return Value::makeNull();
        if (args[0].isSetId()) {
            auto* s = vm.getHeap().set(args[0].asSetId());
            if (s) {
                std::vector<Value> vals;
                for (const auto& [k, v] : *s) vals.push_back(valueFromKey(k));
                std::sort(vals.begin(), vals.end(), [](const Value& a, const Value& b) {
                    if (a.isInt() && b.isInt()) return a.asInt() < b.asInt();
                    if (a.isDouble() && b.isDouble()) return a.asDouble() < b.asDouble();
                    return false;
                });
                auto arrRef = vm.getHeap().allocateArray();
                auto* arr = vm.getHeap().array(arrRef.id);
                for (const auto& v : vals) arr->push_back(v);
                return Value::makeArrayId(arrRef.id);
            }
        }
        return Value::makeNull();
    });

    regProto("unique", 1, [&vm, &valueFromKey](const std::vector<Value>& args) {
        if (args.empty()) return Value::makeNull();
        if (args[0].isSetId()) {
            auto* s = vm.getHeap().set(args[0].asSetId());
            if (s) {
                auto resultRef = vm.getHeap().allocateArray();
                auto* arr = vm.getHeap().array(resultRef.id);
                for (const auto& [k, v] : *s) arr->push_back(valueFromKey(k));
                return Value::makeArrayId(resultRef.id);
            }
        }
        return Value::makeNull();
    });

    regProto("reversed", 1, [&vm, &valueFromKey](const std::vector<Value>& args) {
        if (args.empty()) return Value::makeNull();
        if (args[0].isSetId()) {
            auto* s = vm.getHeap().set(args[0].asSetId());
            if (s) {
                std::vector<Value> vals;
                for (const auto& [k, v] : *s) vals.push_back(valueFromKey(k));
                auto arrRef = vm.getHeap().allocateArray();
                auto* arr = vm.getHeap().array(arrRef.id);
                for (auto it = vals.rbegin(); it != vals.rend(); ++it) arr->push_back(*it);
                return Value::makeArrayId(arrRef.id);
            }
        }
        return Value::makeNull();
    });

    regProto("isSupersetOf", 2, [&vm](const std::vector<Value>& args) {
        if (args.size() < 2) return Value::makeBool(false);
        if (args[0].isSetId() && args[1].isSetId()) {
            auto* s1 = vm.getHeap().set(args[0].asSetId());
            auto* s2 = vm.getHeap().set(args[1].asSetId());
            if (s1 && s2) {
                for (const auto& [k, v] : *s2) {
                    if (s1->find(k) == s1->end()) return Value::makeBool(false);
                }
                return Value::makeBool(true);
            }
        }
        return Value::makeBool(false);
    });

    regProto("symmetricDifference", 2, [&vm](const std::vector<Value>& args) {
        if (args.size() < 2) return Value::makeNull();
        if (args[0].isSetId() && args[1].isSetId()) {
            auto* s1 = vm.getHeap().set(args[0].asSetId());
            auto* s2 = vm.getHeap().set(args[1].asSetId());
            if (s1 && s2) {
                auto resultRef = vm.getHeap().allocateSet();
                auto* result = vm.getHeap().set(resultRef.id);
                for (const auto& [k, v] : *s1) { if (s2->find(k) == s2->end()) (*result)[k] = v; }
                for (const auto& [k, v] : *s2) { if (s1->find(k) == s1->end()) (*result)[k] = v; }
                return Value::makeSetId(resultRef.id);
            }
        }
        return Value::makeNull();
    });

    regProto("cartesianProduct", 2, [&vm, &valueFromKey](const std::vector<Value>& args) {
        if (args.size() < 2) return Value::makeNull();
        if (args[0].isSetId() && args[1].isSetId()) {
            auto* s1 = vm.getHeap().set(args[0].asSetId());
            auto* s2 = vm.getHeap().set(args[1].asSetId());
            if (s1 && s2) {
                auto resultRef = vm.getHeap().allocateArray();
                auto* result = vm.getHeap().array(resultRef.id);
                for (const auto& [k1, v1] : *s1) {
                    for (const auto& [k2, v2] : *s2) {
                        auto pairRef = vm.getHeap().allocateArray();
                        auto* pair = vm.getHeap().array(pairRef.id);
                        pair->push_back(valueFromKey(k1));
                        pair->push_back(valueFromKey(k2));
                        result->push_back(Value::makeArrayId(pairRef.id));
                    }
                }
                return Value::makeArrayId(resultRef.id);
            }
        }
        return Value::makeNull();
    });

    regProto("powerSet", 1, [&vm](const std::vector<Value>& args) {
        if (args.empty()) return Value::makeNull();
        if (args[0].isSetId()) {
            auto* s = vm.getHeap().set(args[0].asSetId());
            if (s) {
                std::vector<std::string> keys;
                for (const auto& [k, v] : *s) keys.push_back(k);
                auto n = keys.size();
                auto resultRef = vm.getHeap().allocateArray();
                auto* result = vm.getHeap().array(resultRef.id);
                uint64_t total = (n > 63) ? (1ULL << 63) : (1ULL << n);
                for (uint64_t mask = 0; mask < total; ++mask) {
                    auto subsetRef = vm.getHeap().allocateSet();
                    auto* subset = vm.getHeap().set(subsetRef.id);
                    for (size_t i = 0; i < n; ++i) {
                        if (mask & (1ULL << i)) (*subset)[keys[i]] = Value::makeBool(true);
                    }
                    result->push_back(Value::makeSetId(subsetRef.id));
                }
                return Value::makeArrayId(resultRef.id);
            }
        }
        return Value::makeNull();
    });

    regProto("toSet", 1, [&vm](const std::vector<Value>& args) {
        if (args.empty()) return Value::makeNull();
        if (args[0].isSetId()) return args[0];
        return Value::makeNull();
    });

  regProto("discard", 2, [&vm, &setKeyFromValue](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    if (args[0].isSetId()) {
      auto* set = vm.getHeap().set(args[0].asSetId());
      if (set) {
        std::string key = setKeyFromValue(args[1]);
        set->erase(key);
        return args[0];
      }
    }
    return Value::makeNull();
  });

  regProto("list", 1, [&vm, &valueFromKey](const std::vector<Value>& args) {
    if (args.empty() || !args[0].isSetId()) return Value::makeNull();
    auto* s = vm.getHeap().set(args[0].asSetId());
    if (!s) return Value::makeNull();
    auto arrRef = vm.getHeap().allocateArray();
    auto* arr = vm.getHeap().array(arrRef.id);
    for (const auto& [k, v] : *s) arr->push_back(valueFromKey(k));
    return Value::makeArrayId(arrRef.id);
  });

  regProto("union", 2, [&vm, &setKeyFromValue](const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].isSetId() || !args[1].isSetId()) return Value::makeNull();
    auto* s1 = vm.getHeap().set(args[0].asSetId());
    auto* s2 = vm.getHeap().set(args[1].asSetId());
    if (!s1 || !s2) return Value::makeNull();
    auto resultRef = vm.getHeap().allocateSet();
    auto* result = vm.getHeap().set(resultRef.id);
    for (const auto& [k, v] : *s1) (*result)[k] = v;
    for (const auto& [k, v] : *s2) (*result)[k] = v;
    return Value::makeSetId(resultRef.id);
  });

  regProto("intersection", 2, [&vm, &valueFromKey](const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].isSetId() || !args[1].isSetId()) return Value::makeNull();
    auto* s1 = vm.getHeap().set(args[0].asSetId());
    auto* s2 = vm.getHeap().set(args[1].asSetId());
    if (!s1 || !s2) return Value::makeNull();
    auto resultRef = vm.getHeap().allocateSet();
    auto* result = vm.getHeap().set(resultRef.id);
    for (const auto& [k, v] : *s1) {
      if (s2->find(k) != s2->end()) (*result)[k] = v;
    }
    return Value::makeSetId(resultRef.id);
  });

  regProto("difference", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].isSetId() || !args[1].isSetId()) return Value::makeNull();
    auto* s1 = vm.getHeap().set(args[0].asSetId());
    auto* s2 = vm.getHeap().set(args[1].asSetId());
    if (!s1 || !s2) return Value::makeNull();
    auto resultRef = vm.getHeap().allocateSet();
    auto* result = vm.getHeap().set(resultRef.id);
    for (const auto& [k, v] : *s1) {
      if (s2->find(k) == s2->end()) (*result)[k] = v;
    }
    return Value::makeSetId(resultRef.id);
  });
}

} // namespace havel::compiler::prototypes
