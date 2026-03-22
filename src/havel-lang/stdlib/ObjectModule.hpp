/*
 * ObjectModule.hpp - Object manipulation stdlib for VM (no host/service)
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
inline void registerObjectModuleVM(compiler::HostBridgeRegistry& registry) {
    auto& vm = registry.vm();
    auto& options = registry.options();
    
    // Object.keys(obj) - Get array of keys (sorted)
    options.host_functions["Object.keys"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.keys() requires object");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.keys() arg must be object");
        
        auto arr = vm.createHostArray();
        // Note: Would need VM access to iterate object keys
        // Simplified for now
        return compiler::BytecodeValue(arr);
    };
    
    // Object.values(obj) - Get array of values (sorted by key)
    options.host_functions["Object.values"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.values() requires object");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.values() arg must be object");
        
        auto arr = vm.createHostArray();
        // Note: Would need VM access to iterate object values
        // Simplified for now
        return compiler::BytecodeValue(arr);
    };
    
    // Object.entries(obj) - Get array of [key, value] pairs
    options.host_functions["Object.entries"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.entries() requires object");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.entries() arg must be object");
        
        auto arr = vm.createHostArray();
        // Note: Would need VM access to iterate object entries
        // Simplified for now
        return compiler::BytecodeValue(arr);
    };
    
    // Object.has(obj, key) - Check if object has key
    options.host_functions["Object.has"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("Object.has() requires object and key");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.has() first arg must be object");
        if (!std::holds_alternative<std::string>(args[1])) throw std::runtime_error("Object.has() second arg must be string");
        
        // Note: Would need VM access to check object keys
        // Simplified for now
        return compiler::BytecodeValue(false);
    };
    
    // Object.delete(obj, key) - Delete key from object
    options.host_functions["Object.delete"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("Object.delete() requires object and key");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.delete() first arg must be object");
        
        // Note: Would need VM access to delete from object
        // Simplified for now
        return compiler::BytecodeValue(false);
    };
    
    // Object.assign(target, source1, ...) - Copy properties from sources to target
    options.host_functions["Object.assign"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.assign() requires at least target object");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) throw std::runtime_error("Object.assign() first arg must be object");
        
        // Note: Would need VM access to copy properties
        // Simplified for now
        return args[0];
    };
    
    // Object.freeze(obj) - Freeze object (prevent modifications)
    options.host_functions["Object.freeze"] = [](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.freeze() requires object");
        // Note: VM doesn't support freezing yet
        return args.empty() ? compiler::BytecodeValue(nullptr) : args[0];
    };
    
    // Object.seal(obj) - Seal object (prevent new properties)
    options.host_functions["Object.seal"] = [](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("Object.seal() requires object");
        // Note: VM doesn't support sealing yet
        return args.empty() ? compiler::BytecodeValue(nullptr) : args[0];
    };
    
    // Register Object functions via vm_setup
    options.vm_setup = [](compiler::VM& vm) {
        auto objConstructor = vm.createHostObject();
        vm.setHostObjectField(objConstructor, "keys", compiler::HostFunctionRef{.name = "Object.keys"});
        vm.setHostObjectField(objConstructor, "values", compiler::HostFunctionRef{.name = "Object.values"});
        vm.setHostObjectField(objConstructor, "entries", compiler::HostFunctionRef{.name = "Object.entries"});
        vm.setHostObjectField(objConstructor, "has", compiler::HostFunctionRef{.name = "Object.has"});
        vm.setHostObjectField(objConstructor, "delete", compiler::HostFunctionRef{.name = "Object.delete"});
        vm.setHostObjectField(objConstructor, "assign", compiler::HostFunctionRef{.name = "Object.assign"});
        vm.setHostObjectField(objConstructor, "freeze", compiler::HostFunctionRef{.name = "Object.freeze"});
        vm.setHostObjectField(objConstructor, "seal", compiler::HostFunctionRef{.name = "Object.seal"});
        vm.setGlobal("Object", objConstructor);
    };
}

// Implementation of old registerObjectModule (placeholder)
inline void registerObjectModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
