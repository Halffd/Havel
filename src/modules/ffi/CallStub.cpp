/*
 * CallStub.cpp
 *
 * FFI call stub - marshals Havel values to C ABI and calls function.
 * Uses System V AMD64 ABI (Linux x86_64).
 */
#include "../../havel-lang/runtime/Environment.hpp"
#include "DynamicLibrary.hpp"
#include <cstdint>
#include <cstring>

namespace havel::modules::ffi {

// Forward declarations
using BuiltinFunction =
    std::function<HavelResult(const std::vector<HavelValue> &)>;

/**
 * Marshal Havel value to C-compatible uint64_t
 */
static uint64_t marshalToC(const HavelValue &value) {
  if (value.isInt()) {
    return static_cast<uint64_t>(static_cast<int>(value.asNumber()));
  } else if (value.isNumber()) {
    double d = value.asNumber();
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(double));
    return bits;
  } else if (value.isBool()) {
    return value.asBool() ? 1 : 0;
  } else if (value.isString()) {
    const std::string &str = value.asString();
    return reinterpret_cast<uint64_t>(const_cast<char *>(str.c_str()));
  }
  // For pointers stored as int64 in HavelValue
  return static_cast<uint64_t>(value.asNumber());
}

/**
 * Unmarshal C return value to Havel value
 */
static HavelValue unmarshalFromC(uint64_t returnValue) {
  if (returnValue <= INT64_MAX) {
    return HavelValue(static_cast<int>(returnValue));
  }
  return HavelValue(static_cast<double>(returnValue));
}

/**
 * Trampoline function for calling C functions
 * Uses a simple approach: cast function pointer to callable type
 */
typedef uint64_t (*ffi_func0)();
typedef uint64_t (*ffi_func1)(uint64_t);
typedef uint64_t (*ffi_func2)(uint64_t, uint64_t);
typedef uint64_t (*ffi_func3)(uint64_t, uint64_t, uint64_t);
typedef uint64_t (*ffi_func4)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*ffi_func5)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*ffi_func6)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t);

/**
 * Call a C function through FFI
 */
static HavelResult callFFI(void *fn, const std::vector<HavelValue> &args) {
  if (!fn) {
    return HavelRuntimeError("Null function pointer");
  }

  if (args.size() > 6) {
    return HavelRuntimeError(
        "FFI call supports max 6 arguments (System V ABI limit)");
  }

  // Marshal arguments
  uint64_t a[6] = {0};
  for (size_t i = 0; i < args.size() && i < 6; i++) {
    a[i] = marshalToC(args[i]);
  }

  // Call through function pointer using typed trampoline
  uint64_t result;
  switch (args.size()) {
  case 0:
    result = reinterpret_cast<ffi_func0>(fn)();
    break;
  case 1:
    result = reinterpret_cast<ffi_func1>(fn)(a[0]);
    break;
  case 2:
    result = reinterpret_cast<ffi_func2>(fn)(a[0], a[1]);
    break;
  case 3:
    result = reinterpret_cast<ffi_func3>(fn)(a[0], a[1], a[2]);
    break;
  case 4:
    result = reinterpret_cast<ffi_func4>(fn)(a[0], a[1], a[2], a[3]);
    break;
  case 5:
    result = reinterpret_cast<ffi_func5>(fn)(a[0], a[1], a[2], a[3], a[4]);
    break;
  case 6:
    result =
        reinterpret_cast<ffi_func6>(fn)(a[0], a[1], a[2], a[3], a[4], a[5]);
    break;
  default:
    return HavelRuntimeError("Too many arguments");
  }

  return unmarshalFromC(result);
}

/**
 * Register FFI module in environment
 */
void registerFFIModule(Environment &env) {
  auto ffiObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // ffi.dl(path) -> library handle
  (*ffiObj)["dl"] = makeBuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("ffi.dl() requires library path");
        }
        try {
          std::string path =
              args[0].isString()
                  ? args[0].asString()
                  : std::to_string(static_cast<int>(args[0].asNumber()));
          auto lib = DynamicLibrary::open(path);
          auto *libPtr = new DynamicLibrary(std::move(lib));
          // Return handle as double (preserving pointer bits)
          double libNum;
          std::memcpy(&libNum, &libPtr, sizeof(double));
          return HavelValue(libNum);
        } catch (const std::exception &e) {
          return HavelRuntimeError(e.what());
        }
      });

  // ffi.sym(lib, name) -> function pointer
  (*ffiObj)["sym"] = makeBuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError("ffi.sym() requires (library, symbol_name)");
        }
        try {
          // Library handle stored as int64 via double
          double libNum = args[0].asNumber();
          int64_t libAddr;
          std::memcpy(&libAddr, &libNum, sizeof(double));
          auto *libPtr = reinterpret_cast<DynamicLibrary *>(libAddr);
          if (!libPtr || !libPtr->isValid()) {
            return HavelRuntimeError("Invalid library handle: " +
                                     std::to_string(libAddr));
          }
          std::string name =
              args[1].isString()
                  ? args[1].asString()
                  : std::to_string(static_cast<int>(args[1].asNumber()));
          void *sym = libPtr->symbol(name);
          // Return pointer as double (preserving bits)
          double symNum;
          std::memcpy(&symNum, &sym, sizeof(double));
          return HavelValue(symNum);
        } catch (const std::exception &e) {
          return HavelRuntimeError(e.what());
        }
      });

  // ffi.call(fn, ...) -> result
  (*ffiObj)["call"] = makeBuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError(
              "ffi.call() requires function pointer and arguments");
        }
        // Function pointer stored as double (preserving bits)
        double fnNum = args[0].asNumber();
        void *fn;
        std::memcpy(&fn, &fnNum, sizeof(double));
        std::vector<HavelValue> callArgs(args.begin() + 1, args.end());
        return callFFI(fn, callArgs);
      });

  // ffi.close(lib) - free library handle
  (*ffiObj)["close"] = makeBuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("ffi.close() requires library handle");
        }
        try {
          double libNum = args[0].asNumber();
          int64_t libAddr;
          std::memcpy(&libAddr, &libNum, sizeof(double));
          auto *libPtr = reinterpret_cast<DynamicLibrary *>(libAddr);
          if (libPtr) {
            delete libPtr;
          }
          return HavelValue(nullptr);
        } catch (...) {
          return HavelRuntimeError("Invalid library handle");
        }
      });

  env.Define("ffi", HavelValue(ffiObj));
}

} // namespace havel::modules::ffi
