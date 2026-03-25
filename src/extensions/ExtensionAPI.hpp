#pragma once

/**
 * ExtensionAPI.hpp - C++ wrapper around stable C ABI
 *
 * Extensions can use this C++ wrapper for convenience,
 * but the underlying ABI is C (HavelCAPI.h).
 *
 * This provides:
 * - Type safety
 * - RAII
 * - Easier registration
 *
 * While maintaining ABI stability via C backend.
 */

#include "HavelCAPI.h"
#include <string>
#include <functional>
#include <memory>

namespace havel {

/**
 * ExtensionAPI - C++ wrapper for HavelAPI
 *
 * Provides convenient C++ interface while using stable C ABI underneath.
 */
class ExtensionAPI {
public:
  explicit ExtensionAPI(HavelAPI* api) : api_(api) {}
  
  /**
   * Register a function with automatic type conversion
   *
   * @param module Module name (e.g., "image")
   * @param name Function name (e.g., "load")
   * @param fn C++ function with signature: Value(const std::vector<Value>&)
   */
  using NativeFn = std::function<Value(const std::vector<Value>&)>;
  
  void registerFunction(const std::string& module, const std::string& name, NativeFn fn);
  
  /**
   * Get host service (sandboxed access)
   *
   * Extensions should NOT access OS directly.
   * Use this to request services from HostBridge.
   *
   * @param name Service name (e.g., "io", "filesystem")
   * @return Service interface or nullptr if not available
   */
  template<typename T>
  T* getHostService(const std::string& name) {
    return static_cast<T*>(api_->get_host_service(name.c_str()));
  }
  
  /**
   * Raw API access (for advanced extensions)
   */
  HavelAPI* raw() { return api_; }
  const HavelAPI* raw() const { return api_; }
  
private:
  HavelAPI* api_;
};

/**
 * Value - C++ wrapper for HavelValue
 *
 * RAII handle for VM values.
 * Automatic conversion to/from C++ types.
 */
class Value {
public:
  Value() : handle_(nullptr), api_(nullptr) {}
  explicit Value(HavelValue* h, HavelAPI* api) : handle_(h), api_(api) {}
  ~Value();
  
  /* Copy/move semantics */
  Value(const Value& other) noexcept;
  Value& operator=(const Value& other) noexcept;
  Value(Value&& other) noexcept;
  Value& operator=(Value&& other) noexcept;
  
  /* Type checking */
  bool isNull() const { return type() == HAVEL_NULL; }
  bool isBool() const { return type() == HAVEL_BOOL; }
  bool isInt() const { return type() == HAVEL_INT; }
  bool isFloat() const { return type() == HAVEL_FLOAT; }
  bool isString() const { return type() == HAVEL_STRING; }
  bool isArray() const { return type() == HAVEL_ARRAY; }
  bool isObject() const { return type() == HAVEL_OBJECT; }
  bool isHandle() const { return type() == HAVEL_HANDLE; }
  
  /* Type conversion */
  HavelValueType type() const;
  
  bool asBool() const;
  int64_t asInt() const;
  double asFloat() const;
  std::string asString() const;
  
  /* Handle access (for opaque resources) */
  template<typename T>
  T* asHandle() const {
    return static_cast<T*>(api_->get_handle(handle_));
  }
  
  /* Array operations */
  size_t arrayLength() const;
  Value arrayGet(size_t index) const;
  void arrayPush(const Value& v);
  
  /* Object operations */
  void objectSet(const std::string& key, const Value& v);
  Value objectGet(const std::string& key) const;
  
  /* Factory methods */
  static Value null(HavelAPI* api);
  static Value fromBool(bool b, HavelAPI* api);
  static Value fromInt(int64_t i, HavelAPI* api);
  static Value fromFloat(double f, HavelAPI* api);
  static Value fromString(const std::string& s, HavelAPI* api);
  static Value fromHandle(void* ptr, void (*destructor)(void*), HavelAPI* api);
  
  /* C++ convenience constructors (require api to be set) */
  static void setGlobalAPI(HavelAPI* api);
  
private:
  HavelValue* handle_;
  HavelAPI* api_;
  static HavelAPI* global_api_;  /* For convenience constructors */
};

/**
 * Helper macro for defining extensions with C++ wrapper
 *
 * Usage:
 *   HAVEL_EXTENSION_CPP(my_extension) {
 *     api.registerFunction("image", "load", [](const std::vector<Value>& args) {
 *       // ...
 *       return Value::fromString("result", api.raw());
 *     });
 *   }
 */
#define HAVEL_EXTENSION_CPP(name) \
  static void name##_cpp_init(havel::ExtensionAPI& api); \
  extern "C" void havel_extension_init(HavelAPI* c_api) { \
    havel::ExtensionAPI cpp_api(c_api); \
    name##_cpp_init(cpp_api); \
  } \
  static void name##_cpp_init(havel::ExtensionAPI& api)

} // namespace havel
