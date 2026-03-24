/* ObjectModule.cpp - VM-native stdlib module */
#include "ObjectModule.hpp"

using havel::compiler::BytecodeValue;
using havel::compiler::VMApi;
using havel::compiler::ObjectRef;

namespace havel::stdlib {

// Register object module with VMApi (stable API layer)
void registerObjectModule(VMApi& api) {
    // Object.keys(obj) - Get array of keys (sorted)
    api.registerFunction("object.keys", [&api](const std::vector<BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.keys() requires object");
        if (!std::holds_alternative<ObjectRef>(args[0])) throw std::runtime_error("Object.keys() arg must be object");
        
        auto obj = std::get<ObjectRef>(args[0]);
        auto keys = api.getObjectKeys(obj);
        
        // Sort keys alphabetically
        std::sort(keys.begin(), keys.end());
        
        auto arr = api.makeArray();
        for (const auto& key : keys) {
            api.push(arr, BytecodeValue(key));
        }
        return BytecodeValue(arr);
    });
    
    // Object.has(obj, key) - Check if object has key
    api.registerFunction("object.has", [&api](const std::vector<BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("Object.has() requires object and key");
        if (!std::holds_alternative<ObjectRef>(args[0])) throw std::runtime_error("Object.has() first arg must be object");
        
        auto obj = std::get<ObjectRef>(args[0]);
        std::string key = std::get<std::string>(args[1]);
        
        return BytecodeValue(api.hasField(obj, key));
    });
    
    // Object.set(obj, key, value) - Set value by key
    api.registerFunction("object.set", [&api](const std::vector<BytecodeValue>& args) {
        if (args.size() < 3) throw std::runtime_error("Object.set() requires object, key, and value");
        if (!std::holds_alternative<ObjectRef>(args[0])) throw std::runtime_error("Object.set() first arg must be object");
        
        auto obj = std::get<ObjectRef>(args[0]);
        std::string key = std::get<std::string>(args[1]);
        BytecodeValue value = args[2];
        
        api.setField(obj, key, value);
        return BytecodeValue(obj);
    });
    
    // Object.isEmpty(obj) - Check if object is empty
    api.registerFunction("object.isEmpty", [&api](const std::vector<BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.isEmpty() requires object");
        if (!std::holds_alternative<ObjectRef>(args[0])) throw std::runtime_error("Object.isEmpty() arg must be object");
        
        auto obj = std::get<ObjectRef>(args[0]);
        auto keys = api.getObjectKeys(obj);
        
        return BytecodeValue(keys.empty());
    });
    
    // Object.size(obj) - Get number of keys in object
    api.registerFunction("object.size", [&api](const std::vector<BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.size() requires object");
        if (!std::holds_alternative<ObjectRef>(args[0])) throw std::runtime_error("Object.size() arg must be object");
        
        auto obj = std::get<ObjectRef>(args[0]);
        auto keys = api.getObjectKeys(obj);
        
        return BytecodeValue(static_cast<int64_t>(keys.size()));
    });
    
    // Register object object
    auto obj = api.makeObject();
    api.setField(obj, "keys", api.makeFunctionRef("object.keys"));
    api.setField(obj, "has", api.makeFunctionRef("object.has"));
    api.setField(obj, "set", api.makeFunctionRef("object.set"));
    api.setField(obj, "isEmpty", api.makeFunctionRef("object.isEmpty"));
    api.setField(obj, "size", api.makeFunctionRef("object.size"));
    api.setGlobal("Object", obj);
}

} // namespace havel::stdlib
