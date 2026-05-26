#include "ObjectModule.hpp"
#include "../compiler/vm/VMApi.hpp"
#include "../compiler/vm/VM.hpp"
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>

using havel::compiler::Value;
using havel::compiler::VMApi;
using havel::compiler::ObjectRef;

namespace havel::stdlib {

static bool isInternalKey(const std::string &key) {
  return key == "__set_marker__" || key == "__proto__" ||
         key == "__frozen__" || key == "__sealed__";
}

void registerObjectModule(const VMApi &api) {
    // Object.keys(obj) - Get all keys of an object
    api.registerFunction("object.keys", [api](const std::vector<Value>& args) {
        if (args.empty()) throw std::runtime_error("Object.keys() requires object");
        if (!args[0].isObjectId()) throw std::runtime_error("Object.keys() arg must be object");

        auto keys = api.getObjectKeys(args[0]);
        auto result = api.makeArray();
        for (const auto& key : keys) {
            if (!isInternalKey(key)) {
                api.push(result, api.makeString(key));
            }
        }
        return result;
    });

    // Object.values(obj) - Get all values of an object
    api.registerFunction("object.values", [api](const std::vector<Value>& args) {
        if (args.empty()) throw std::runtime_error("Object.values() requires object");
        if (!args[0].isObjectId()) throw std::runtime_error("Object.values() arg must be object");

        auto keys = api.getObjectKeys(args[0]);
        auto result = api.makeArray();
        for (const auto& key : keys) {
            if (!isInternalKey(key)) {
                api.push(result, api.getField(args[0], key));
            }
        }
        return result;
    });

    // Object.entries(obj) - Get all entries of an object as [key, value] pairs
    api.registerFunction("object.entries", [api](const std::vector<Value>& args) {
        if (args.empty()) throw std::runtime_error("Object.entries() requires object");
        if (!args[0].isObjectId()) throw std::runtime_error("Object.entries() arg must be object");

        auto keys = api.getObjectKeys(args[0]);
        auto result = api.makeArray();
        for (const auto& key : keys) {
            if (!isInternalKey(key)) {
                auto entry = api.makeArray();
                api.push(entry, api.makeString(key));
                api.push(entry, api.getField(args[0], key));
                api.push(result, entry);
            }
        }
        return result;
    });

    // Object.has(obj, key) - Check if object has a key
    api.registerFunction("object.has", [api](const std::vector<Value>& args) {
        if (args.size() < 2) throw std::runtime_error("Object.has() requires object and key");
        if (!args[0].isObjectId()) throw std::runtime_error("Object.has() first arg must be object");

        std::string key = api.toString(args[1]);
        return Value::makeBool(api.hasField(args[0], key));
    });

    // Object.find(obj, key) - Find key index in object, returns index >= 0 or -1
    api.registerFunction("object.find", [api](const std::vector<Value>& args) {
        if (args.size() < 2) return Value::makeInt(-1);
        if (!args[0].isObjectId()) return Value::makeInt(-1);

        std::string key = api.toString(args[1]);
        auto keys = api.getObjectKeys(args[0]);
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) return Value::makeInt(static_cast<int64_t>(i));
        }
        return Value::makeInt(-1);
    });

    // Object.set(obj, key, value) - Set a value on an object
    api.registerFunction("object.set", [api](const std::vector<Value>& args) {
        if (args.size() < 3) throw std::runtime_error("Object.set() requires object, key, and value");
        if (!args[0].isObjectId()) throw std::runtime_error("Object.set() first arg must be object");
        if (!args[1].isStringId() && !args[1].isStringValId()) throw std::runtime_error("Object.set() second arg must be key string");

        std::string key = api.toString(args[1]);
        api.setField(args[0], key, args[2]);
        return args[0];
    });

    // Object.delete(obj, key) - Delete a value from an object
    api.registerFunction("object.delete", [api](const std::vector<Value>& args) {
        if (args.size() < 2) throw std::runtime_error("Object.delete() requires object and key");
        if (!args[0].isObjectId()) throw std::runtime_error("Object.delete() first arg must be object");
        if (!args[1].isStringId() && !args[1].isStringValId()) throw std::runtime_error("Object.delete() second arg must be key string");

        std::string key = api.toString(args[1]);
    return Value::makeBool(api.deleteField(args[0], key));
  });

  // Object.freeze(obj) - Freeze object (no additions, deletions, or modifications)
  api.registerFunction("object.freeze", [api](const std::vector<Value>& args) {
    if (args.empty()) throw std::runtime_error("Object.freeze() requires object");
    if (!args[0].isObjectId()) throw std::runtime_error("Object.freeze() arg must be object");
    api.freeze(args[0]);
    return args[0];
  });

  // Object.seal(obj) - Seal object (no additions or deletions, modifications allowed)
  api.registerFunction("object.seal", [api](const std::vector<Value>& args) {
    if (args.empty()) throw std::runtime_error("Object.seal() requires object");
    if (!args[0].isObjectId()) throw std::runtime_error("Object.seal() arg must be object");
    api.seal(args[0]);
    return args[0];
  });

  // Object.isFrozen(obj) - Check if object is frozen
  api.registerFunction("object.isFrozen", [api](const std::vector<Value>& args) {
    if (args.empty()) throw std::runtime_error("Object.isFrozen() requires object");
    if (!args[0].isObjectId()) throw std::runtime_error("Object.isFrozen() arg must be object");
    return Value::makeBool(api.isFrozen(args[0]));
  });

  // Object.isSealed(obj) - Check if object is sealed
  api.registerFunction("object.isSealed", [api](const std::vector<Value>& args) {
    if (args.empty()) throw std::runtime_error("Object.isSealed() requires object");
    if (!args[0].isObjectId()) throw std::runtime_error("Object.isSealed() arg must be object");
    return Value::makeBool(api.isSealed(args[0]));
  });

    // Object.isEmpty(obj) - Check if object has no user keys
    api.registerFunction("object.isEmpty", [api](const std::vector<Value>& args) {
        if (args.empty()) throw std::runtime_error("Object.isEmpty() requires object");
        if (!args[0].isObjectId()) throw std::runtime_error("Object.isEmpty() arg must be object");

        auto keys = api.getObjectKeys(args[0]);
        for (const auto& k : keys) {
            if (!isInternalKey(k)) return Value::makeBool(false);
        }
        return Value::makeBool(true);
    });

    // Object.size(obj) - Get number of keys in object
    api.registerFunction("object.size", [api](const std::vector<Value>& args) {
        if (args.empty()) throw std::runtime_error("Object.size() requires object");
        if (!args[0].isObjectId()) throw std::runtime_error("Object.size() arg must be object");

        auto keys = api.getObjectKeys(args[0]);
        int64_t count = 0;
        for (const auto& k : keys) {
            if (!isInternalKey(k)) count++;
        }
        return Value::makeInt(count);
    });

    // object.len(obj) - Alias for size
    api.registerFunction("object.len", [api](const std::vector<Value>& args) {
        if (args.empty()) throw std::runtime_error("object.len() requires object");
        if (!args[0].isObjectId()) return Value::makeInt(0);
        auto keys = api.getObjectKeys(args[0]);
        int64_t count = 0;
        for (const auto& k : keys) {
            if (!isInternalKey(k)) count++;
        }
        return Value::makeInt(count);
    });

    // object.map(obj, func) - Map object values
    api.registerFunction("object.map", [api](const std::vector<Value>& args) {
        if (args.size() < 2) throw std::runtime_error("Object.map() requires object and function");
        if (!args[0].isObjectId()) throw std::runtime_error("Object.map() first arg must be object");
        
        auto keys = api.getObjectKeys(args[0]);
        auto result = api.makeObject();
        for (const auto& key : keys) {
            if (isInternalKey(key)) continue;
            Value val = api.getField(args[0], key);
            api.setField(result, key, api.invoke(args[1], {val, api.makeString(key)}));
        }
        return result;
    });

    // object.filter(obj, func) - Filter object keys
    api.registerFunction("object.filter", [api](const std::vector<Value>& args) {
        if (args.size() < 2) throw std::runtime_error("Object.filter() requires object and function");
        if (!args[0].isObjectId()) throw std::runtime_error("Object.filter() first arg must be object");
        
        auto keys = api.getObjectKeys(args[0]);
        auto result = api.makeObject();
        for (const auto& key : keys) {
            if (isInternalKey(key)) continue;
            Value val = api.getField(args[0], key);
            if (api.toBool(api.invoke(args[1], {val, api.makeString(key)}))) {
                api.setField(result, key, val);
            }
        }
        return result;
    });

    // Prototype methods for all objects
    api.registerPrototypeMethodByName("object", "keys", "object.keys");
    api.registerPrototypeMethodByName("object", "values", "object.values");
    api.registerPrototypeMethodByName("object", "entries", "object.entries");
    api.registerPrototypeMethodByName("object", "has", "object.has");
    api.registerPrototypeMethodByName("object", "find", "object.find");
    api.registerPrototypeMethodByName("object", "size", "object.size");
    api.registerPrototypeMethodByName("object", "len", "object.len");
    api.registerPrototypeMethodByName("object", "isEmpty", "object.isEmpty");
    api.registerPrototypeMethodByName("object", "map", "object.map");
    api.registerPrototypeMethodByName("object", "filter", "object.filter");
    api.registerPrototypeMethodByName("object", "set", "object.set");
    api.registerPrototypeMethodByName("object", "delete", "object.delete");
  api.registerPrototypeMethodByName("object", "freeze", "object.freeze");
  api.registerPrototypeMethodByName("object", "seal", "object.seal");
  api.registerPrototypeMethodByName("object", "isFrozen", "object.isFrozen");
  api.registerPrototypeMethodByName("object", "isSealed", "object.isSealed");

    // Register global object/Object namespace object
    auto objVal = api.makeObject();
    api.setField(objVal, "keys", api.makeFunctionRef("object.keys"));
    api.setField(objVal, "values", api.makeFunctionRef("object.values"));
    api.setField(objVal, "entries", api.makeFunctionRef("object.entries"));
    api.setField(objVal, "has", api.makeFunctionRef("object.has"));
    api.setField(objVal, "find", api.makeFunctionRef("object.find"));
    api.setField(objVal, "size", api.makeFunctionRef("object.size"));
    api.setField(objVal, "len", api.makeFunctionRef("object.len"));
    api.setField(objVal, "isEmpty", api.makeFunctionRef("object.isEmpty"));
    api.setField(objVal, "map", api.makeFunctionRef("object.map"));
    api.setField(objVal, "filter", api.makeFunctionRef("object.filter"));
    api.setField(objVal, "set", api.makeFunctionRef("object.set"));
    api.setField(objVal, "delete", api.makeFunctionRef("object.delete"));
  api.setField(objVal, "freeze", api.makeFunctionRef("object.freeze"));
  api.setField(objVal, "seal", api.makeFunctionRef("object.seal"));
  api.setField(objVal, "isFrozen", api.makeFunctionRef("object.isFrozen"));
  api.setField(objVal, "isSealed", api.makeFunctionRef("object.isSealed"));

  api.setGlobal("Object", objVal);
  api.setGlobal("object", objVal);

  Value exports;
  try {
    auto &vm = api.vm();
    exports = vm.loadModule("object");
    if (exports.isObjectId()) {
      auto *obj = vm.getHeap().object(exports.asObjectId());
      if (obj) {
        for (const auto& [name, value] : *obj) {
          if (name.empty() || name[0] == '_') continue;
          api.setField(objVal, name, value);
          api.setGlobal(name, value);
        }
      }
    }
  } catch (...) {}
}

} // namespace havel::stdlib
