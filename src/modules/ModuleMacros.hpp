/*
 * ModuleMacros.hpp - Simplified module registration macros and helpers
 * 
 * PURPOSE:
 * - Reduce boilerplate for host module registration
 * - Provide consistent error handling patterns
 * - Simplify common VM API operations
 * 
 * USAGE EXAMPLE:
 *   void registerMyModule(VMApi& api) {
 *     HAVEL_BEGIN_MODULE("MyModule");
 *     
 *     // Register simple function
 *     HAVEL_REGISTER_FUNCTION(api, "myFunc", [](const auto& args) {
 *       HAVEL_ARG_CHECK(args, 1, "myFunc() requires 1 argument");
 *       return Value::makeInt(args[0].asInt() * 2);
 *     });
 *     
 *     // Register with service
 *     HAVEL_REGISTER_SERVICE_FUNCTION(api, "io.read", IOService, svc, args) {
 *       HAVEL_ARG_CHECK(args, 1, "io.read() requires 1 argument: path");
 *       auto path = havel::toString(args[0]);
 *       auto result = svc->readFile(path);
 *       return api.makeString(result);
 *     }
 *     
 *     HAVEL_END_MODULE();
 *   }
 */

#pragma once

#include "havel-lang/compiler/vm/VMApi.hpp"
#include "host/ServiceRegistry.hpp"
#include "utils/Logger.hpp"
#include <sstream>

namespace havel {
namespace modules {

// ============================================================================
// MODULE REGISTRATION HELPERS
// ============================================================================

// Module registration scope tracking
struct ModuleRegistration {
    std::string name;
    int functionCount = 0;
};

// Global module registration state (thread-local for safety)
inline thread_local ModuleRegistration* g_currentModule = nullptr;

// Begin module registration scope
#define HAVEL_BEGIN_MODULE(moduleName) \
    havel::modules::ModuleRegistration _havel_module_reg{moduleName, 0}; \
    havel::modules::g_currentModule = &_havel_module_reg; \
    havel::modules::detail::ModuleScopeGuard _havel_module_guard(&_havel_module_reg)

// End module registration (actually handled by scope guard)
#define HAVEL_END_MODULE() \
    do { \
        if (havel::modules::g_currentModule) { \
            info("{} module registered with {} functions", \
                 havel::modules::g_currentModule->name, \
                 havel::modules::g_currentModule->functionCount); \
        } \
        havel::modules::g_currentModule = nullptr; \
    } while(0)

namespace detail {
    // RAII guard for module registration
    struct ModuleScopeGuard {
        ModuleRegistration* reg;
        explicit ModuleScopeGuard(ModuleRegistration* r) : reg(r) {}
        ~ModuleScopeGuard() {
            if (havel::modules::g_currentModule == reg) {
                HAVEL_END_MODULE();
            }
        }
    };
}

// ============================================================================
// FUNCTION REGISTRATION MACROS
// ============================================================================

// Register a simple function
#define HAVEL_REGISTER_FUNCTION(api, name, lambda) \
    do { \
        (api).registerFunction(name, lambda); \
        if (havel::modules::g_currentModule) { \
            havel::modules::g_currentModule->functionCount++; \
        } \
    } while(0)

// Register function with specific arity
#define HAVEL_REGISTER_FUNCTION_N(api, name, arity, lambda) \
    do { \
        (api).registerFunction(name, arity, lambda); \
        if (havel::modules::g_currentModule) { \
            havel::modules::g_currentModule->functionCount++; \
        } \
    } while(0)

// Register a function that uses a service
#define HAVEL_REGISTER_SERVICE_FUNCTION(api, name, ServiceType, svcVar, argsVar, ...) \
    HAVEL_REGISTER_FUNCTION(api, name, [&api](const std::vector<compiler::Value>& argsVar) -> compiler::Value { \
        auto& registry = havel::host::ServiceRegistry::instance(); \
        auto svcVar = registry.get<havel::host::ServiceType>(); \
        if (!svcVar) { \
            throw std::runtime_error(std::string(name) + "(): " #ServiceType " not available"); \
        } \
        __VA_ARGS__ \
    })

// Register a method on a module object
#define HAVEL_REGISTER_METHOD(obj, api, name, fn) \
    (api).setField(obj, name, (api).makeFunctionRef(name))

// ============================================================================
// ARGUMENT VALIDATION MACROS
// ============================================================================

// Check minimum argument count
#define HAVEL_ARG_CHECK(args, minCount, msg) \
    do { \
        if ((args).size() < (minCount)) { \
            throw std::runtime_error(msg); \
        } \
    } while(0)

// Check exact argument count
#define HAVEL_ARG_CHECK_EXACT(args, count, msg) \
    do { \
        if ((args).size() != (count)) { \
            throw std::runtime_error(msg); \
        } \
    } while(0)

// Check argument type
#define HAVEL_ARG_TYPE_CHECK(args, index, typeCheck, msg) \
    do { \
        if (!(args)[(index)].typeCheck()) { \
            throw std::runtime_error(msg); \
        } \
    } while(0)

// Get argument or return default
#define HAVEL_GET_ARG_OR(args, index, defaultVal) \
    ((args).size() > (index) ? (args)[(index)] : (defaultVal))

// Get typed argument with default
#define HAVEL_GET_INT_ARG(args, index, defaultVal) \
    ((args).size() > (index) && (args)[(index)].isInt() ? \
     (args)[(index)].asInt() : (defaultVal))

#define HAVEL_GET_BOOL_ARG(args, index, defaultVal) \
    ((args).size() > (index) && (args)[(index)].isBool() ? \
     (args)[(index)].asBool() : (defaultVal))

#define HAVEL_GET_DOUBLE_ARG(args, index, defaultVal) \
    ((args).size() > (index) && (args)[(index)].isDouble() ? \
     (args)[(index)].asDouble() : (defaultVal))

// ============================================================================
// SERVICE HELPERS
// ============================================================================

// Get service or throw
#define HAVEL_GET_SERVICE(svcVar, ServiceType) \
    auto& registry = havel::host::ServiceRegistry::instance(); \
    auto svcVar = registry.get<havel::host::ServiceType>(); \
    if (!svcVar) { \
        throw std::runtime_error(std::string(#ServiceType) " not available"); \
    }

// Get service or return default value
#define HAVEL_GET_SERVICE_OR_RETURN(svcVar, ServiceType, returnVal) \
    auto& registry = havel::host::ServiceRegistry::instance(); \
    auto svcVar = registry.get<havel::host::ServiceType>(); \
    if (!svcVar) { \
        return (returnVal); \
    }

// Get service and check, returning error Value if unavailable
#define HAVEL_REQUIRE_SERVICE(svcVar, ServiceType) \
    auto& registry = havel::host::ServiceRegistry::instance(); \
    auto svcVar = registry.get<havel::host::ServiceType>(); \
    if (!svcVar) { \
        error(#ServiceType " not available for operation"); \
        return compiler::Value::makeNull(); \
    }

// ============================================================================
// VALUE CONVERSION HELPERS
// ============================================================================

// Convert Value to string (inline helper)
inline std::string toString(const compiler::Value &v) {
  if (v.isNull()) return "";
  if (v.isBool()) return v.asBool() ? "true" : "false";
  if (v.isInt()) return std::to_string(v.asInt());
  if (v.isDouble()) {
    double val = v.asDouble();
    if (val == std::floor(val) && std::abs(val) < 1e15) {
      return std::to_string(static_cast<long long>(val));
    }
    std::ostringstream oss;
    oss.precision(15);
    oss << val;
    return oss.str();
  }
  if (v.isStringValId()) {
    // TODO: string pool lookup - for now return placeholder
    return "<string>";
  }
  return "";
}

// Convert string to Value
inline compiler::Value fromString(compiler::VMApi& api, const std::string &s) {
  // Try boolean
  if (s == "true") return compiler::Value::makeBool(true);
  if (s == "false") return compiler::Value::makeBool(false);

  // Try integer
  try {
    size_t pos;
    int64_t i = std::stoll(s, &pos);
    if (pos == s.length()) return compiler::Value::makeInt(i);
  } catch (...) {}

  // Try double
  try {
    size_t pos;
    double d = std::stod(s, &pos);
    if (pos == s.length()) return compiler::Value::makeDouble(d);
  } catch (...) {}

  // Default to string (via API which handles string pool)
  return api.makeString(s);
}

// Create array from vector of values
inline compiler::Value makeArray(compiler::VMApi& api, const std::vector<compiler::Value>& values) {
    auto arr = api.makeArray();
    for (const auto& v : values) {
        api.push(arr, v);
    }
    return arr;
}

// Create object from key-value pairs
using ObjectField = std::pair<std::string, compiler::Value>;
inline compiler::Value makeObject(compiler::VMApi& api, const std::vector<ObjectField>& fields) {
    auto obj = api.makeObject();
    for (const auto& [key, value] : fields) {
        api.setField(obj, key, value);
    }
    return obj;
}

// ============================================================================
// MODULE OBJECT HELPERS
// ============================================================================

// Create and register a module object with methods
#define HAVEL_CREATE_MODULE_OBJECT(api, objVar, moduleName) \
    auto objVar = (api).makeObject(); \
    (api).setGlobal(moduleName, objVar)

// Register multiple methods on a module object
// Usage: HAVEL_REGISTER_METHODS(api, obj, ("method1", func1), ("method2", func2))
#define HAVEL_REGISTER_METHODS(api, obj, ...) \
    do { \
        auto& _api = (api); \
        auto& _obj = (obj); \
        havel::modules::detail::registerMethodsImpl(_api, _obj, __VA_ARGS__); \
    } while(0)

namespace detail {
    // Helper for registering a single method
    inline void registerMethod(compiler::VMApi& api, compiler::Value& obj, 
                               const std::string& name, const std::string& funcName) {
        api.setField(obj, name, api.makeFunctionRef(funcName));
    }
    
    // Variadic template for multiple methods
    template<typename... Pairs>
    void registerMethodsImpl(compiler::VMApi& api, compiler::Value& obj, Pairs... pairs) {
        // This would need more sophisticated implementation
        // For now, users should call registerMethod multiple times
        (void)api; (void)obj; 
        (void)std::initializer_list<int>{((void)pairs, 0)...};
    }
}

// ============================================================================
// RESULT HELPERS
// ============================================================================

// Return success result
#define HAVEL_RETURN_SUCCESS() return compiler::Value::makeBool(true)
#define HAVEL_RETURN_FAILURE() return compiler::Value::makeBool(false)

// Return value or null if condition fails
#define HAVEL_RETURN_IF(cond, val) \
    do { \
        if (!(cond)) { \
            return compiler::Value::makeNull(); \
        } \
        return (val); \
    } while(0)

// ============================================================================
// EXCEPTION HANDLING
// ============================================================================

// Wrap function body with exception handling
#define HAVEL_TRY_CATCH(expr, errorReturn) \
    try { \
        expr; \
    } catch (const std::exception& e) { \
        error("Exception in module function: {}", e.what()); \
        return (errorReturn); \
    } catch (...) { \
        error("Unknown exception in module function"); \
        return (errorReturn); \
    }

// Wrap entire function with exception handling
#define HAVEL_SAFE_FUNCTION(body) \
    try { \
        body; \
    } catch (const std::exception& e) { \
        error("Exception: {}", e.what()); \
        return compiler::Value::makeNull(); \
    } catch (...) { \
        error("Unknown exception"); \
        return compiler::Value::makeNull(); \
    }

} // namespace modules
} // namespace havel
