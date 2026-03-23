/* UtilityModule.cpp - VM-native stdlib module */
#include "UtilityModule.hpp"

namespace havel::stdlib {

// Implementation moved from header to avoid lambda mangling issues

inline void registerUtilityModuleVM(compiler::HostBridge& bridge) {
    auto* vm = bridge.context().vm;
    auto& options = bridge.options();
    
    // keys(obj) - Get keys from object/map
    options.host_functions["keys"] = [=, &bridge](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("keys() requires an object");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) {
            throw std::runtime_error("keys() requires an object argument");
        }
        
        const auto& objRef = std::get<compiler::ObjectRef>(args[0]);
        auto arr = ((havel::compiler::VM*)(vm))->createHostArray();
        
        // Note: Would need VM access to iterate object keys
        // Simplified for now
        return compiler::BytecodeValue(arr);
    };
    
    // items(obj) - Get key-value pairs from object/map
    options.host_functions["items"] = [=, &bridge](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("items() requires an object");
        if (!std::holds_alternative<compiler::ObjectRef>(args[0])) {
            throw std::runtime_error("items() requires an object argument");
        }
        
        const auto& objRef = std::get<compiler::ObjectRef>(args[0]);
        auto arr = ((havel::compiler::VM*)(vm))->createHostArray();
        
        // Note: Would need VM access to iterate object
        // Simplified for now
        return compiler::BytecodeValue(arr);
    };
    
    // list(value) - Convert to list
    options.host_functions["list"] = [=, &bridge](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("list() requires an argument");
        
        const auto& arg = args[0];
        
        // If already an array, return it
        if (std::holds_alternative<compiler::ArrayRef>(arg)) {
            return arg;
        }
        
        // If string, convert to array of characters
        if (std::holds_alternative<std::string>(arg)) {
            const auto& str = std::get<std::string>(arg);
            auto arr = ((havel::compiler::VM*)(vm))->createHostArray();
            for (char c : str) {
                ((havel::compiler::VM*)(vm))->pushHostArrayValue(arr, compiler::BytecodeValue(std::string(1, c)));
            }
            return compiler::BytecodeValue(arr);
        }
        
        // For other types, create single-element array
        auto arr = ((havel::compiler::VM*)(vm))->createHostArray();
        ((havel::compiler::VM*)(vm))->pushHostArrayValue(arr, arg);
        return compiler::BytecodeValue(arr);
    };
    
    // range(start, end, step) - Generate range of numbers
    options.host_functions["range"] = [=, &bridge](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("range() requires at least 1 argument");
        
        int64_t start = 0, end = 0, step = 1;
        
        if (args.size() == 1) {
            end = std::holds_alternative<int64_t>(args[0]) ? std::get<int64_t>(args[0]) : 0;
        } else if (args.size() >= 2) {
            start = std::holds_alternative<int64_t>(args[0]) ? std::get<int64_t>(args[0]) : 0;
            end = std::holds_alternative<int64_t>(args[1]) ? std::get<int64_t>(args[1]) : 0;
            if (args.size() >= 3) {
                step = std::holds_alternative<int64_t>(args[2]) ? std::get<int64_t>(args[2]) : 1;
            }
        }
        
        if (step == 0) throw std::runtime_error("range() step cannot be zero");
        
        auto arr = ((havel::compiler::VM*)(vm))->createHostArray();
        if (step > 0) {
            for (int64_t i = start; i < end; i += step) {
                ((havel::compiler::VM*)(vm))->pushHostArrayValue(arr, compiler::BytecodeValue(i));
            }
        } else {
            for (int64_t i = start; i > end; i += step) {
                ((havel::compiler::VM*)(vm))->pushHostArrayValue(arr, compiler::BytecodeValue(i));
            }
        }
        
        return compiler::BytecodeValue(arr);
    };
    
    // enumerate(arr) - Get index-value pairs
    options.host_functions["enumerate"] = [=, &bridge](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("enumerate() requires an array");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) {
            throw std::runtime_error("enumerate() requires an array argument");
        }
        
        const auto& arrRef = std::get<compiler::ArrayRef>(args[0]);
        auto result = ((havel::compiler::VM*)(vm))->createHostArray();
        
        // Note: Would need VM access to iterate array
        // Simplified for now
        return compiler::BytecodeValue(result);
    };
    
    // zip(arr1, arr2, ...) - Combine arrays
    options.host_functions["zip"] = [=, &bridge](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("zip() requires at least 1 array");
        
        auto result = ((havel::compiler::VM*)(vm))->createHostArray();
        
        // Note: Would need VM access to iterate arrays
        // Simplified for now
        return compiler::BytecodeValue(result);
    };
    
    // reverse(arr) - Reverse array
    options.host_functions["reverse"] = [=, &bridge](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("reverse() requires an array");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) {
            throw std::runtime_error("reverse() requires an array argument");
        }
        
        const auto& arrRef = std::get<compiler::ArrayRef>(args[0]);
        auto result = ((havel::compiler::VM*)(vm))->createHostArray();
        
        // Note: Would need VM access to iterate and reverse
        // Simplified for now
        return compiler::BytecodeValue(result);
    };
    
    // sort(arr) - Sort array
    options.host_functions["sort"] = [=, &bridge](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("sort() requires an array");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) {
            throw std::runtime_error("sort() requires an array argument");
        }
        
        const auto& arrRef = std::get<compiler::ArrayRef>(args[0]);
        auto result = ((havel::compiler::VM*)(vm))->createHostArray();
        
        // Note: Would need VM access to iterate and sort
        // Simplified for now
        return compiler::BytecodeValue(result);
    };
    
    // Register utility functions via vm_setup
    bridge.addVmSetup([](compiler::VM& vm) {
        // Utility functions are global
        // No utility object needed
    });
}



} // namespace havel::stdlib
