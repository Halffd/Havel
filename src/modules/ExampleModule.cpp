/*
 * ExampleModule.cpp - Demonstrates simplified module registration using ModuleMacros
 * 
 * This shows how the new macros reduce boilerplate for host module registration.
 * Compare this to the older style in AutomationModule.cpp or ConfigModule.cpp.
 */

#include "havel-lang/compiler/vm/VMApi.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/io/IOService.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

// ============================================================================
// EXAMPLE 1: Simple function registration with argument checking
// ============================================================================

void registerSimpleFunctions(VMApi& api) {
    // Old style (verbose):
    // api.registerFunction("math.double", [](const std::vector<Value>& args) {
    //   if (args.empty()) {
    //     throw std::runtime_error("math.double() requires 1 argument");
    //   }
    //   if (!args[0].isInt()) {
    //     throw std::runtime_error("math.double() requires integer argument");
    //   }
    //   return Value::makeInt(args[0].asInt() * 2);
    // });
    
    // New style (concise):
    HAVEL_REGISTER_FUNCTION(api, "math.double", [](const auto& args) {
        HAVEL_ARG_CHECK(args, 1, "math.double() requires 1 argument: number");
        HAVEL_ARG_TYPE_CHECK(args, 0, isInt, "math.double() requires integer argument");
        return Value::makeInt(args[0].asInt() * 2);
    });
    
    // Function with optional arguments and defaults
    HAVEL_REGISTER_FUNCTION(api, "math.power", [](const auto& args) {
        HAVEL_ARG_CHECK(args, 1, "math.power() requires at least 1 argument: base");
        
        int base = HAVEL_GET_INT_ARG(args, 0, 0);
        int exp = HAVEL_GET_INT_ARG(args, 1, 2);  // Default exponent is 2
        
        int result = 1;
        for (int i = 0; i < exp; i++) {
            result *= base;
        }
        return Value::makeInt(result);
    });
}

// ============================================================================
// EXAMPLE 2: Service-based function registration
// ============================================================================

void registerServiceFunctions(VMApi& api) {
    // Old style (repetitive service lookup):
    // api.registerFunction("io.exists", [](const auto& args) {
    //   if (args.empty()) {
    //     throw std::runtime_error("io.exists() requires 1 argument: path");
    //   }
    //   auto& registry = host::ServiceRegistry::instance();
    //   auto io = registry.get<host::IOService>();
    //   if (!io) {
    //     throw std::runtime_error("IOService not available");
    //   }
    //   auto path = toString(args[0]);
    //   return Value::makeBool(io->fileExists(path));
    // });
    
    // New style using service macro:
    HAVEL_REGISTER_FUNCTION(api, "io.exists", [&api](const auto& args) {
        HAVEL_ARG_CHECK(args, 1, "io.exists() requires 1 argument: path");
        HAVEL_REQUIRE_SERVICE(io, IOService);
        
        auto path = toString(args[0]);
        return Value::makeBool(io->fileExists(path));
    });
    
    // Another service function with exception handling
    HAVEL_REGISTER_FUNCTION(api, "io.read", [&api](const auto& args) {
        HAVEL_ARG_CHECK(args, 1, "io.read() requires 1 argument: path");
        HAVEL_REQUIRE_SERVICE(io, IOService);
        
        auto path = toString(args[0]);
        try {
            auto content = io->readTextFile(path);
            return fromString(api, content);
        } catch (const std::exception& e) {
            error("io.read() failed: {}", e.what());
            return Value::makeNull();
        }
    });
}

// ============================================================================
// EXAMPLE 3: Module object with methods
// ============================================================================

void registerModuleObject(VMApi& api) {
    // Register functions that will be methods
    HAVEL_REGISTER_FUNCTION(api, "file.read", [&api](const auto& args) {
        HAVEL_ARG_CHECK(args, 1, "file.read() requires 1 argument: path");
        HAVEL_REQUIRE_SERVICE(io, IOService);
        
        auto path = toString(args[0]);
        return fromString(api, io->readTextFile(path));
    });
    
    HAVEL_REGISTER_FUNCTION(api, "file.write", [&api](const auto& args) {
        HAVEL_ARG_CHECK(args, 2, "file.write() requires 2 arguments: path, content");
        HAVEL_REQUIRE_SERVICE(io, IOService);
        
        auto path = toString(args[0]);
        auto content = toString(args[1]);
        io->writeTextFile(path, content);
        HAVEL_RETURN_SUCCESS();
    });
    
    HAVEL_REGISTER_FUNCTION(api, "file.exists", [](const auto& args) {
        HAVEL_ARG_CHECK(args, 1, "file.exists() requires 1 argument: path");
        HAVEL_REQUIRE_SERVICE(io, IOService);
        
        return Value::makeBool(io->fileExists(toString(args[0])));
    });
    
    // Create module object with methods
    HAVEL_CREATE_MODULE_OBJECT(api, fileObj, "file");
    HAVEL_REGISTER_METHOD(fileObj, api, "read", "file.read");
    HAVEL_REGISTER_METHOD(fileObj, api, "write", "file.write");
    HAVEL_REGISTER_METHOD(fileObj, api, "exists", "file.exists");
    
    info("File module object registered with methods: read, write, exists");
}

// ============================================================================
// EXAMPLE 4: Full module with begin/end macros
// ============================================================================

void registerExampleModule(VMApi& api) {
    HAVEL_BEGIN_MODULE("Example");
    
    // Simple math functions
    HAVEL_REGISTER_FUNCTION(api, "example.add", [](const auto& args) {
        HAVEL_ARG_CHECK(args, 2, "example.add() requires 2 arguments: a, b");
        auto a = HAVEL_GET_INT_ARG(args, 0, 0);
        auto b = HAVEL_GET_INT_ARG(args, 1, 0);
        return Value::makeInt(a + b);
    });
    
    HAVEL_REGISTER_FUNCTION(api, "example.concat", [&api](const auto& args) {
        HAVEL_ARG_CHECK(args, 2, "example.concat() requires 2 arguments: str1, str2");
        auto s1 = toString(args[0]);
        auto s2 = toString(args[1]);
        return api.makeString(s1 + s2);
    });
    
    // Service-based function
    HAVEL_REGISTER_FUNCTION(api, "example.pwd", [&api](const auto& args) {
        (void)args;
        HAVEL_REQUIRE_SERVICE(io, IOService);
        return api.makeString(io->getCurrentDirectory());
    });
    
    // Function returning array
    HAVEL_REGISTER_FUNCTION(api, "example.range", [&api](const auto& args) {
        HAVEL_ARG_CHECK(args, 2, "example.range() requires 2 arguments: start, end");
        auto start = HAVEL_GET_INT_ARG(args, 0, 0);
        auto end = HAVEL_GET_INT_ARG(args, 1, 0);
        
        std::vector<Value> values;
        for (int i = start; i < end; i++) {
            values.push_back(Value::makeInt(i));
        }
        return makeArray(api, values);
    });
    
    HAVEL_END_MODULE();
}

// ============================================================================
// MAIN REGISTRATION ENTRY POINT
// ============================================================================

void registerAllExampleModules(VMApi& api) {
    info("Registering example modules with new macro system...");
    
    registerSimpleFunctions(api);
    registerServiceFunctions(api);
    registerModuleObject(api);
    registerExampleModule(api);
    
    info("All example modules registered successfully");
}

} // namespace havel::modules
