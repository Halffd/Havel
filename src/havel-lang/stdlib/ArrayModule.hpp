/*
 * ArrayModule.hpp - Array stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once

#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"

#include <algorithm>
#include <cmath>

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerArrayModule(Environment& env, Interpreter* interpreter);

// NEW: Register array module with VM's host bridge (VM-native)
inline void registerArrayModuleVM(compiler::HostBridgeRegistry& registry) {
    auto& vm = registry.vm();
    auto& options = registry.options();
    
    // Helper: convert BytecodeValue to number
    auto toNumber = [](const compiler::BytecodeValue& v) -> double {
        if (std::holds_alternative<int64_t>(v)) return static_cast<double>(std::get<int64_t>(v));
        if (std::holds_alternative<double>(v)) return std::get<double>(v);
        return 0.0;
    };
    
    // Helper: check if value is array
    
    // array.len(arr) - Get array length
    options.host_functions["array.len"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("array.len() requires an array");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.len() requires an array argument");
        
        // Note: Would need VM access to get array length
        // Simplified for now
        return compiler::BytecodeValue(static_cast<int64_t>(0));
    };
    
    // array.push(arr, value) - Append to array
    options.host_functions["array.push"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("array.push() requires array and value");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.push() first argument must be array");
        
        const auto& arrRef = std::get<compiler::ArrayRef>(args[0]);
        vm.pushHostArrayValue(arrRef, args[1]);
        return compiler::BytecodeValue(arrRef);
    };
    
    // array.pop(arr) - Remove last element
    options.host_functions["array.pop"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("array.pop() requires an array");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.pop() requires an array argument");
        
        // Note: Would need VM access to pop from array
        // Simplified for now
        return compiler::BytecodeValue(nullptr);
    };
    
    // array.insert(arr, index, value) - Insert at index
    options.host_functions["array.insert"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 3) throw std::runtime_error("array.insert() requires array, index, and value");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.insert() first argument must be array");
        
        // Note: Would need VM access to insert into array
        // Simplified for now
        return compiler::BytecodeValue(args[0]);
    };
    
    // array.remove(arr, index) - Remove at index
    options.host_functions["array.remove"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("array.remove() requires array and index");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.remove() first argument must be array");
        
        // Note: Would need VM access to remove from array
        // Simplified for now
        return compiler::BytecodeValue(args[0]);
    };
    
    // array.concat(arr1, arr2, ...) - Concatenate arrays
    options.host_functions["array.concat"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("array.concat() requires at least 1 array");
        
        auto result = vm.createHostArray();
        for (const auto& arg : args) {
            if (std::holds_alternative<compiler::ArrayRef>(arg)) {
                // Note: Would need VM access to iterate and copy
                // Simplified for now
            }
        }
        return compiler::BytecodeValue(result);
    };
    
    // array.slice(arr, start, end) - Slice array
    options.host_functions["array.slice"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("array.slice() requires an array");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.slice() first argument must be array");
        
        auto result = vm.createHostArray();
        // Note: Would need VM access to slice array
        // Simplified for now
        return compiler::BytecodeValue(result);
    };
    
    // array.map(arr, fn) - Map function over array
    options.host_functions["array.map"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("array.map() requires array and function");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.map() first argument must be array");
        
        auto result = vm.createHostArray();
        // Note: Would need VM access to iterate and call function
        // Simplified for now
        return compiler::BytecodeValue(result);
    };
    
    // array.filter(arr, fn) - Filter array
    options.host_functions["array.filter"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("array.filter() requires array and function");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.filter() first argument must be array");
        
        auto result = vm.createHostArray();
        // Note: Would need VM access to iterate and filter
        // Simplified for now
        return compiler::BytecodeValue(result);
    };
    
    // array.reduce(arr, fn, initial) - Reduce array
    options.host_functions["array.reduce"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 3) throw std::runtime_error("array.reduce() requires array, function, and initial value");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.reduce() first argument must be array");
        
        // Note: Would need VM access to iterate and reduce
        // Simplified for now
        return args[2];
    };
    
    // array.find(arr, fn) - Find first matching element
    options.host_functions["array.find"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("array.find() requires array and function");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.find() first argument must be array");
        
        // Note: Would need VM access to iterate and find
        // Simplified for now
        return compiler::BytecodeValue(nullptr);
    };
    
    // array.indexOf(arr, value) - Find index of value
    options.host_functions["array.indexOf"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("array.indexOf() requires array and value");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.indexOf() first argument must be array");
        
        // Note: Would need VM access to iterate and find index
        // Simplified for now
        return compiler::BytecodeValue(static_cast<int64_t>(-1));
    };
    
    // array.includes(arr, value) - Check if array includes value
    options.host_functions["array.includes"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("array.includes() requires array and value");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.includes() first argument must be array");
        
        // Note: Would need VM access to iterate and check
        // Simplified for now
        return compiler::BytecodeValue(false);
    };
    
    // array.join(arr, delimiter) - Join array elements
    options.host_functions["array.join"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("array.join() requires an array");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.join() first argument must be array");
        
        std::string delimiter = args.size() > 1 && std::holds_alternative<std::string>(args[1]) 
            ? std::get<std::string>(args[1]) : ",";
        
        // Note: Would need VM access to iterate and join
        // Simplified for now
        return compiler::BytecodeValue(std::string(""));
    };
    
    // array.reverse(arr) - Reverse array
    options.host_functions["array.reverse"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("array.reverse() requires an array");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.reverse() first argument must be array");
        
        // Note: Would need VM access to reverse array
        // Simplified for now
        return compiler::BytecodeValue(args[0]);
    };
    
    // array.sort(arr) - Sort array
    options.host_functions["array.sort"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("array.sort() requires an array");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) throw std::runtime_error("array.sort() first argument must be array");
        
        // Note: Would need VM access to sort array
        // Simplified for now
        return compiler::BytecodeValue(args[0]);
    };
    
    // Register array object via vm_setup
    options.vm_setup = [](compiler::VM& vm) {
        auto arrObj = vm.createHostObject();
        vm.setHostObjectField(arrObj, "len", compiler::HostFunctionRef{.name = "array.len"});
        vm.setHostObjectField(arrObj, "push", compiler::HostFunctionRef{.name = "array.push"});
        vm.setHostObjectField(arrObj, "pop", compiler::HostFunctionRef{.name = "array.pop"});
        vm.setHostObjectField(arrObj, "insert", compiler::HostFunctionRef{.name = "array.insert"});
        vm.setHostObjectField(arrObj, "remove", compiler::HostFunctionRef{.name = "array.remove"});
        vm.setHostObjectField(arrObj, "concat", compiler::HostFunctionRef{.name = "array.concat"});
        vm.setHostObjectField(arrObj, "slice", compiler::HostFunctionRef{.name = "array.slice"});
        vm.setHostObjectField(arrObj, "map", compiler::HostFunctionRef{.name = "array.map"});
        vm.setHostObjectField(arrObj, "filter", compiler::HostFunctionRef{.name = "array.filter"});
        vm.setHostObjectField(arrObj, "reduce", compiler::HostFunctionRef{.name = "array.reduce"});
        vm.setHostObjectField(arrObj, "find", compiler::HostFunctionRef{.name = "array.find"});
        vm.setHostObjectField(arrObj, "indexOf", compiler::HostFunctionRef{.name = "array.indexOf"});
        vm.setHostObjectField(arrObj, "includes", compiler::HostFunctionRef{.name = "array.includes"});
        vm.setHostObjectField(arrObj, "join", compiler::HostFunctionRef{.name = "array.join"});
        vm.setHostObjectField(arrObj, "reverse", compiler::HostFunctionRef{.name = "array.reverse"});
        vm.setHostObjectField(arrObj, "sort", compiler::HostFunctionRef{.name = "array.sort"});
        vm.setGlobal("array", arrObj);
    };
}

// Implementation of old registerArrayModule (placeholder)
inline void registerArrayModule(Environment& env, Interpreter* interpreter) {
    (void)env;
    (void)interpreter;
}

} // namespace havel::stdlib
