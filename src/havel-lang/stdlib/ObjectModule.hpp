/*
 * ObjectModule.hpp - Object manipulation stdlib for VM with method chaining
 * Pure VM implementation using BytecodeValue
 */
#pragma once

#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"

#include <algorithm>

namespace havel {
    class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerObjectModule(Environment& env);

// NEW: Register object module with VM's host bridge (VM-native)
inline void registerObjectModuleVM(compiler::HostBridge& registry) {
    auto& vm = bridge.vm();
    
    // Object.keys(obj) - Get array of keys (sorted)
    bridge.options().host_functions["object.keys"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.keys() requires object");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.keys() arg must be object");
        
        auto obj = std::get<compiler::ObjectRef>(args[0]);
        auto keys = vm.getHostObjectKeys(obj);
        
        // Sort keys alphabetically
        std::sort(keys.begin(), keys.end());
        
        auto arr = vm.createHostArray();
        for (const auto& key : keys) {
            vm.pushHostArrayValue(arr, compiler::BytecodeValue(key));
        }
        return compiler::BytecodeValue(arr);
    };
    
    // Object.values(obj) - Get array of values (sorted by key)
    bridge.options().host_functions["object.values"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.values() requires object");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.values() arg must be object");
        
        auto obj = std::get<compiler::ObjectRef>(args[0]);
        auto entries = vm.getHostObjectEntries(obj);
        
        // Sort by key
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        
        auto arr = vm.createHostArray();
        for (const auto& [key, value] : entries) {
            vm.pushHostArrayValue(arr, value);
        }
        return compiler::BytecodeValue(arr);
    };
    
    // Object.entries(obj) - Get array of [key, value] pairs
    bridge.options().host_functions["object.entries"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.entries() requires object");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.entries() arg must be object");
        
        auto obj = std::get<compiler::ObjectRef>(args[0]);
        auto entries = vm.getHostObjectEntries(obj);
        
        // Sort by key
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        
        auto arr = vm.createHostArray();
        for (const auto& [key, value] : entries) {
            auto pairArr = vm.createHostArray();
            vm.pushHostArrayValue(pairArr, compiler::BytecodeValue(key));
            vm.pushHostArrayValue(pairArr, value);
            vm.pushHostArrayValue(arr, compiler::BytecodeValue(pairArr));
        }
        return compiler::BytecodeValue(arr);
    };
    
    // Object.has(obj, key) - Check if object has key
    bridge.options().host_functions["object.has"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("Object.has() requires object and key");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.has() first arg must be object");
        if (!std::holds_alternative<std::string>(args[1])) throw std::runtime_error("Object.has() second arg must be string");
        
        auto obj = std::get<compiler::ObjectRef>(args[0]);
        auto key = std::get<std::string>(args[1]);
        return compiler::BytecodeValue(vm.hasHostObjectField(obj, key));
    };
    
    // Object.delete(obj, key) - Delete key from object
    bridge.options().host_functions["object.delete"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("Object.delete() requires object and key");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.delete() first arg must be object");
        if (!std::holds_alternative<std::string>(args[1])) throw std::runtime_error("Object.delete() second arg must be string");
        
        auto obj = std::get<compiler::ObjectRef>(args[0]);
        auto key = std::get<std::string>(args[1]);
        return compiler::BytecodeValue(vm.deleteHostObjectField(obj, key));
    };
    
    // Object.assign(target, source1, ...) - Copy properties from sources to target
    bridge.options().host_functions["object.assign"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.assign() requires at least target object");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.assign() first arg must be object");
        
        auto target = std::get<compiler::ObjectRef>(args[0]);
        for (size_t i = 1; i < args.size(); ++i) {
            if (std::holds_alternative<compiler::ObjectRef>(args[i])) {
                auto source = std::get<compiler::ObjectRef>(args[i]);
                auto entries = vm.getHostObjectEntries(source);
                for (const auto& [key, value] : entries) {
                    vm.setHostObjectField(target, key, value);
                }
            }
        }
        return compiler::BytecodeValue(target);  // Return target for chaining
    };
    
    // Object.freeze(obj) - Freeze object (prevent modifications)
    bridge.options().host_functions["object.freeze"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.freeze() requires object");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.freeze() arg must be object");
        
        auto obj = std::get<compiler::ObjectRef>(args[0]);
        vm.setHostObjectFrozen(obj, true);
        return compiler::BytecodeValue(obj);  // Return object for chaining
    };
    
    // Object.seal(obj) - Seal object (prevent new properties)
    bridge.options().host_functions["object.seal"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.seal() requires object");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.seal() arg must be object");
        
        auto obj = std::get<compiler::ObjectRef>(args[0]);
        vm.setHostObjectSealed(obj, true);
        return compiler::BytecodeValue(obj);  // Return object for chaining
    };

    // Register prototype methods for {}.method() syntax
    bridge.vm().registerPrototypeMethod("Object", "keys", compiler::HostFunctionRef{.name = "object.keys"});
    bridge.vm().registerPrototypeMethod("Object", "values", compiler::HostFunctionRef{.name = "object.values"});
    bridge.vm().registerPrototypeMethod("Object", "entries", compiler::HostFunctionRef{.name = "object.entries"});
    bridge.vm().registerPrototypeMethod("Object", "has", compiler::HostFunctionRef{.name = "object.has"});
    bridge.vm().registerPrototypeMethod("Object", "delete", compiler::HostFunctionRef{.name = "object.delete"});
    bridge.vm().registerPrototypeMethod("Object", "assign", compiler::HostFunctionRef{.name = "object.assign"});
    bridge.vm().registerPrototypeMethod("Object", "freeze", compiler::HostFunctionRef{.name = "object.freeze"});
    bridge.vm().registerPrototypeMethod("Object", "seal", compiler::HostFunctionRef{.name = "object.seal"});

    // Register module globals (compiler already knows about methods from host_functions)
    bridge.options().host_global_names.insert("Object");

    // Register Object functions via vm_setup (accumulated)
    bridge.addVmSetup([](compiler::VM& vm) {
        auto objConstructor = vm.createHostObject();
        vm.setHostObjectField(objConstructor, "keys", compiler::HostFunctionRef{.name = "object.keys"});
        vm.setHostObjectField(objConstructor, "values", compiler::HostFunctionRef{.name = "object.values"});
        vm.setHostObjectField(objConstructor, "entries", compiler::HostFunctionRef{.name = "object.entries"});
        vm.setHostObjectField(objConstructor, "has", compiler::HostFunctionRef{.name = "object.has"});
        vm.setHostObjectField(objConstructor, "delete", compiler::HostFunctionRef{.name = "object.delete"});
        vm.setHostObjectField(objConstructor, "assign", compiler::HostFunctionRef{.name = "object.assign"});
        vm.setHostObjectField(objConstructor, "freeze", compiler::HostFunctionRef{.name = "object.freeze"});
        vm.setHostObjectField(objConstructor, "seal", compiler::HostFunctionRef{.name = "object.seal"});
        vm.setGlobal("Object", objConstructor);
    });
}

// Implementation of old registerObjectModule (placeholder)
inline void registerObjectModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
