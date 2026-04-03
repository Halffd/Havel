/*
 * SingleFileMathModule.cpp - Example of single-file auto-registering module
 * 
 * This demonstrates how to create a complete module in one file without
 * needing header files or manual registration in StdLibModules.cpp
 * 
 * The module auto-registers itself using the REGISTER_MODULE macro.
 */

#include "havel-lang/compiler/vm/VMApi.hpp"
#include "modules/ModuleRegistry.hpp"
#include "utils/Logger.hpp"

namespace havel {
namespace modules {

// Module implementation - all in one file
REGISTER_MODULE("singleFileMath", [](compiler::VMApi& api) {
    info("Registering single-file math module...");
    
    // Basic arithmetic
    api.registerFunction("math.add", [](const std::vector<compiler::Value>& args) {
        if (args.size() < 2) return compiler::Value::makeInt(0);
        int64_t a = args[0].isInt() ? args[0].asInt() : 0;
        int64_t b = args[1].isInt() ? args[1].asInt() : 0;
        return compiler::Value::makeInt(a + b);
    });
    
    api.registerFunction("math.sub", [](const std::vector<compiler::Value>& args) {
        if (args.size() < 2) return compiler::Value::makeInt(0);
        int64_t a = args[0].isInt() ? args[0].asInt() : 0;
        int64_t b = args[1].isInt() ? args[1].asInt() : 0;
        return compiler::Value::makeInt(a - b);
    });
    
    api.registerFunction("math.mul", [](const std::vector<compiler::Value>& args) {
        if (args.size() < 2) return compiler::Value::makeInt(0);
        int64_t a = args[0].isInt() ? args[0].asInt() : 0;
        int64_t b = args[1].isInt() ? args[1].asInt() : 0;
        return compiler::Value::makeInt(a * b);
    });
    
    api.registerFunction("math.div", [](const std::vector<compiler::Value>& args) {
        if (args.size() < 2) return compiler::Value::makeInt(0);
        int64_t a = args[0].isInt() ? args[0].asInt() : 0;
        int64_t b = args[1].isInt() ? args[1].asInt() : 1;
        return compiler::Value::makeInt(b != 0 ? a / b : 0);
    });
    
    // Create math module object
    auto mathObj = api.makeObject();
    api.setField(mathObj, "add", api.makeFunctionRef("math.add"));
    api.setField(mathObj, "sub", api.makeFunctionRef("math.sub"));
    api.setField(mathObj, "mul", api.makeFunctionRef("math.mul"));
    api.setField(mathObj, "div", api.makeFunctionRef("math.div"));
    api.setGlobal("singleMath", mathObj);
    
    info("Single-file math module registered with functions: add, sub, mul, div");
});

// Priority-ordered module example
REGISTER_MODULE_PRIORITY("singleFileUtils", 100, [](compiler::VMApi& api) {
    info("Registering single-file utils module (priority 100)...");
    
    api.registerFunction("utils.identity", [](const std::vector<compiler::Value>& args) {
        return args.empty() ? compiler::Value::makeNull() : args[0];
    });
    
    api.registerFunction("utils.isNull", [](const std::vector<compiler::Value>& args) {
        return compiler::Value::makeBool(args.empty() || args[0].isNull());
    });
    
    info("Utils module registered");
});

} // namespace modules
} // namespace havel

// No header file needed, no manual registration needed!
// Just include this .cpp in your build and it auto-registers.
