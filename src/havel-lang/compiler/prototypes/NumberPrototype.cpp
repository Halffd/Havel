#include "PrototypeRegistry.hpp"
#include <cmath>
#include <sstream>

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

  regProto("has", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    if (args[0].isObjectId()) {
      auto* obj = vm.getHeap().object(args[0].asObjectId());
      if (obj) {
        std::string key;
        if (args[1].isStringValId() && vm.getCurrentChunk()) key = vm.getCurrentChunk()->getString(args[1].asStringValId());
        else if (args[1].isStringId() && vm.getHeap().string(args[1].asStringId())) key = *vm.getHeap().string(args[1].asStringId());
        return Value::makeBool(obj->find(key) != obj->end());
      }
    }
    return Value::makeBool(false);
  });
}

} // namespace havel::compiler::prototypes
