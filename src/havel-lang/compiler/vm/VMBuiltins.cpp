#include "VM.hpp"
#include "VMInternals.hpp"
#include "../../../utils/Logger.hpp"
#include "../../utils/ErrorPrinter.hpp"
#include "../runtime/RuntimeSupport.hpp"
#include "../../runtime/concurrency/DependencyTracker.hpp"
#include "../../runtime/concurrency/WatcherRegistry.hpp"
#include "../../runtime/concurrency/Fiber.hpp"
#include "../prototypes/PrototypeRegistry.hpp"
#include "core/config/ConfigManager.hpp"
#include <cmath>
#include <cctype>
#include <iostream>
#include <set>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <regex>
#include <chrono>

namespace havel::compiler {

bool VM::execBuiltinOp(const Instruction &instruction) {
	switch (instruction.opcode) {
  case OpCode::AS_TYPE: {
    if (instruction.operands.size() < 1) {
      COMPILER_THROW("AS_TYPE requires type operand");
    }
    const std::string &typeName =
        instruction.operands[0].toString();
    Value value = popStack();

    if (typeName == "int" || typeName == "Int") {
      pushStack(toInt(value));
    } else if (typeName == "float" || typeName == "Float" ||
               typeName == "double" || typeName == "num" || typeName == "Num") {
      pushStack(toFloat(value));
    } else if (typeName == "string" || typeName == "String") {
      // TODO: string pool integration - for now return null
      pushStack(Value::makeNull());
    } else if (typeName == "bool" || typeName == "Bool" ||
               typeName == "boolean") {
      pushStack(toBool(value));
    } else if (typeName == "array" || typeName == "Array") {
      // Convert to array if possible
      if (value.isArrayId()) {
        pushStack(value);
      } else {
        auto arrRef = heap_.allocateArray();
        pushStack(Value::makeArrayId(arrRef.id));
      }
    } else {
      pushStack(value); // Unknown type, return as-is
    }
    break;
  }

  // toInt() builtin
  case OpCode::TO_INT: {
    Value value = popStack();
    pushStack(toInt(value));
    break;
  }

  // toFloat() builtin
  case OpCode::TO_FLOAT: {
    Value value = popStack();
    pushStack(toFloat(value));
    break;
  }

  // toString() builtin
  case OpCode::TO_STRING: {
    Value value = popStack();
    auto str_ref = createRuntimeString(toString(value));
    pushStack(Value::makeStringId(str_ref.id));
    break;
  }

  // String concatenation
  case OpCode::STRING_CONCAT: {
    Value right = popStack();
    Value left = popStack();
    auto str_ref = createRuntimeString(toString(left) + toString(right));
    pushStack(Value::makeStringId(str_ref.id));
    break;
  }

  // toBool() builtin
  case OpCode::TO_BOOL: {
    Value value = popStack();
    pushStack(toBool(value));
    break;
  }

  // typeof() builtin
  case OpCode::TYPE_OF: {
    Value value = popStack();
    std::string typeName;
    if (value.isNull()) typeName = "nil";
    else if (value.isInt()) typeName = "int";
    else if (value.isDouble()) typeName = "num";
    else if (value.isBool()) typeName = "bool";
    else if (value.isStringValId() || value.isStringId()) typeName = "str";
    else if (value.isArrayId()) typeName = "array";
    else if (value.isObjectId()) typeName = "object";
    else if (value.isSetId()) typeName = "set";
    else if (value.isRangeId()) typeName = "range";
    else if (value.isClosureId() || value.isFunctionObjId() || value.isHostFuncId()) typeName = "fn";
    else if (value.isCoroutineId()) typeName = "coroutine";
    else if (value.isThreadId()) typeName = "thread";
    else if (value.isChannelId()) typeName = "channel";
    else if (value.isIteratorId()) typeName = "iterator";
    else if (value.isBoundMethodId()) typeName = "bound_method";
    else if (value.isEnumId()) typeName = "enum";
    else if (value.isErrorId()) typeName = "error";
    else typeName = "unknown";
    pushStack(Value::makeStringId(heap_.allocateString(typeName).id));
    break;
  }

  case OpCode::PRINT: {
    Value value = popStack();
    std::cout << toString(value) << std::endl;
    break;
  }

  case OpCode::DEBUG: {
            ::havel::debug("DEBUG: Stack size: {}", stack.size());
            ::havel::debug("DEBUG: Locals size: {}", locals.size());
    break;
  }

case OpCode::IMPORT: {
    Value path_val = popStack();
    std::string path;
    if (path_val.isStringValId() && current_chunk) {
        path = current_chunk->getString(path_val.asStringValId());
    } else if (path_val.isStringId()) {
        if (auto *s = heap_.string(path_val.asStringId())) path = *s;
    }

    if (path.empty()) {
        COMPILER_THROW("IMPORT expects valid string path");
    }

    // Check if module is already in globals (eager modules, previously loaded)
    // Only accept objects (namespace modules) and lazy proxies as pre-loaded.
    // Host functions with the same name as a module should NOT short-circuit
    // the module loading — they are unrelated (e.g., a "debug" host function
    // conflicts with the "debug" .hv module).
    // Lazy proxy objects must be activated before use, so skip the short-circuit.
    auto git = globals.find(path);
        if (git != globals.end() && git->second.isObjectId()) {
            auto *preObj = heap_.object(git->second.asObjectId());
            if (preObj) {
            auto *preLazy = preObj->get("__lazy__");
            if (preLazy && preLazy->isBool() && preLazy->asBool()) {
                // Lazy proxy — fall through to loadModule to activate it
            } else {
                pushStack(git->second);
                break;
            }
        } else {
            pushStack(git->second);
            break;
        }
    } else if (git != globals.end() && git->second.isNull()) {
        pushStack(git->second);
        break;
    }
    // Try capitalized variant
    std::string capPath = path;
    capPath[0] = static_cast<char>(toupper(static_cast<unsigned char>(capPath[0])));
        git = globals.find(capPath);
        if (git != globals.end()) {
            globals[path] = git->second;
            pushStack(git->second);
            break;
        }

    // Load module via C++ module loader (handles .hv/.hvc files)
    // This properly handles .hv/.hvc files and relative imports within modules
    Value exports = loadModule(path);
    pushStack(exports);
    break;
}

    case OpCode::IMPORT_WILDCARD: {
        Value exports = popStack();
        if (!exports.isObjectId()) {
            COMPILER_THROW("IMPORT_WILDCARD expects module object");
        }
        auto *obj = heap_.object(exports.asObjectId());
        if (!obj) {
            COMPILER_THROW("IMPORT_WILDCARD: null module object");
        }
        for (const auto& [name, value] : *obj) {
            if (name.empty() || name[0] == '_') continue;
 globals[name] = value;
            emitVariableChanged(name);
        }
        break;
    }

  // ============================================================================

	if (execConcurrencyOp(instruction)) break;


  case OpCode::BEGIN_MODULE: {
    break;
  }

  case OpCode::END_MODULE: {
    Value exports = Value::makeNull();
    auto it = globals.find("__module_exports__");
    if (it != globals.end()) {
      exports = it->second;
    }
    pushStack(exports);
    module_exports_ = Value::makeNull();
    break;
  }

    case OpCode::NOP:
    case OpCode::DEFINE_FUNC:
        break;

    // Math intrinsics
    case OpCode::MATH_SIN: { Value v = popStack(); pushStack(Value(std::sin(toFloat(v)))); break; }
    case OpCode::MATH_COS: { Value v = popStack(); pushStack(Value(std::cos(toFloat(v)))); break; }
    case OpCode::MATH_TAN: { Value v = popStack(); pushStack(Value(std::tan(toFloat(v)))); break; }
    case OpCode::MATH_ASIN: { Value v = popStack(); pushStack(Value(std::asin(toFloat(v)))); break; }
    case OpCode::MATH_ACOS: { Value v = popStack(); pushStack(Value(std::acos(toFloat(v)))); break; }
    case OpCode::MATH_ATAN: { Value v = popStack(); pushStack(Value(std::atan(toFloat(v)))); break; }
    case OpCode::MATH_ATAN2: { Value y = popStack(); Value x = popStack(); pushStack(Value(std::atan2(toFloat(y), toFloat(x)))); break; }
    case OpCode::MATH_SINH: { Value v = popStack(); pushStack(Value(std::sinh(toFloat(v)))); break; }
    case OpCode::MATH_COSH: { Value v = popStack(); pushStack(Value(std::cosh(toFloat(v)))); break; }
    case OpCode::MATH_TANH: { Value v = popStack(); pushStack(Value(std::tanh(toFloat(v)))); break; }
    case OpCode::MATH_SQRT: { Value v = popStack(); pushStack(Value(std::sqrt(toFloat(v)))); break; }
    case OpCode::MATH_LOG: { Value v = popStack(); pushStack(Value(std::log(toFloat(v)))); break; }
    case OpCode::MATH_LOG2: { Value v = popStack(); pushStack(Value(std::log2(toFloat(v)))); break; }
    case OpCode::MATH_LOG10: { Value v = popStack(); pushStack(Value(std::log10(toFloat(v)))); break; }
    case OpCode::MATH_EXP: { Value v = popStack(); pushStack(Value(std::exp(toFloat(v)))); break; }
    case OpCode::MATH_CEIL: { Value v = popStack(); pushStack(Value(static_cast<int64_t>(std::ceil(toFloat(v))))); break; }
    case OpCode::MATH_FLOOR: { Value v = popStack(); pushStack(Value(static_cast<int64_t>(std::floor(toFloat(v))))); break; }
    case OpCode::MATH_ROUND: { Value v = popStack(); pushStack(Value(static_cast<int64_t>(std::round(toFloat(v))))); break; }
    case OpCode::MATH_ABS: {
        Value v = popStack();
        if (v.isInt()) pushStack(Value(std::abs(v.asInt())));
        else pushStack(Value(std::abs(toFloat(v))));
        break;
    }

    // Bit intrinsics
    case OpCode::BIT_POPCOUNT: { Value v = popStack(); pushStack(Value(static_cast<int64_t>(__builtin_popcountll(static_cast<uint64_t>(toInt(v)))))); break; }
    case OpCode::BIT_CTZ: { Value v = popStack(); pushStack(Value(static_cast<int64_t>(__builtin_ctzll(static_cast<uint64_t>(toInt(v)))))); break; }
    case OpCode::BIT_CLZ: { Value v = popStack(); pushStack(Value(static_cast<int64_t>(__builtin_clzll(static_cast<uint64_t>(toInt(v)))))); break; }
    case OpCode::BIT_BSWAP: { Value v = popStack(); pushStack(Value(static_cast<int64_t>(__builtin_bswap64(static_cast<uint64_t>(toInt(v)))))); break; }
    case OpCode::BIT_ROTL: {
        Value shift = popStack(); Value val = popStack();
        uint64_t x = static_cast<uint64_t>(toInt(val));
        int s = static_cast<int>(toInt(shift)) & 63;
        pushStack(Value(static_cast<int64_t>((x << s) | (x >> (64 - s)))));
        break;
    }
    case OpCode::BIT_ROTR: {
        Value shift = popStack(); Value val = popStack();
        uint64_t x = static_cast<uint64_t>(toInt(val));
        int s = static_cast<int>(toInt(shift)) & 63;
        pushStack(Value(static_cast<int64_t>((x >> s) | (x << (64 - s)))));
        break;
    }

    // Time primitive
    case OpCode::TIME_NOW: {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        pushStack(Value(static_cast<int64_t>(ms)));
        break;
    }

    // Format intrinsics
    case OpCode::FORMAT_HEX: {
        Value v = popStack();
        std::ostringstream oss;
        if (v.isInt()) {
            oss << std::hex << static_cast<uint64_t>(v.asInt());
        } else {
            oss << std::hex << static_cast<uint64_t>(static_cast<int64_t>(toFloat(v)));
        }
        auto ref = createRuntimeString(oss.str());
        pushStack(Value::makeStringId(ref.id));
        break;
    }
    case OpCode::FORMAT_UNHEX: {
        Value v = popStack();
        std::string s = toString(v);
        std::string result;
        for (size_t i = 0; i + 1 < s.size(); i += 2) {
            unsigned byte = 0;
            std::istringstream iss(s.substr(i, 2));
            iss >> std::hex >> byte;
            result += static_cast<char>(byte);
        }
        auto ref = createRuntimeString(std::move(result));
        pushStack(Value::makeStringId(ref.id));
        break;
    }
    case OpCode::FORMAT_BASE64_ENCODE: {
        Value v = popStack();
        std::string s = toString(v);
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((s.size() + 2) / 3) * 4);
        size_t i = 0;
        for (; i + 2 < s.size(); i += 3) {
            uint32_t n = (static_cast<uint8_t>(s[i]) << 16) | (static_cast<uint8_t>(s[i+1]) << 8) | static_cast<uint8_t>(s[i+2]);
            out += b64[(n >> 18) & 0x3F]; out += b64[(n >> 12) & 0x3F];
            out += b64[(n >> 6) & 0x3F]; out += b64[n & 0x3F];
        }
        if (i < s.size()) {
            uint32_t n = static_cast<uint8_t>(s[i]) << 16;
            if (i + 1 < s.size()) n |= static_cast<uint8_t>(s[i+1]) << 8;
            out += b64[(n >> 18) & 0x3F]; out += b64[(n >> 12) & 0x3F];
            out += (i + 1 < s.size()) ? b64[(n >> 6) & 0x3F] : '=';
            out += '=';
        }
        auto ref = createRuntimeString(std::move(out));
        pushStack(Value::makeStringId(ref.id));
        break;
    }
    case OpCode::FORMAT_BASE64_DECODE: {
        Value v = popStack();
        std::string s = toString(v);
        static const int8_t d64[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
            52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
            41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        };
        std::string out;
        out.reserve((s.size() / 4) * 3);
        int val = 0, valb = -8;
        for (unsigned char c : s) {
            if (d64[c] == -1) break;
            val = (val << 6) + d64[c];
            valb += 6;
            if (valb >= 0) {
                out.push_back(static_cast<char>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        auto ref = createRuntimeString(std::move(out));
        pushStack(Value::makeStringId(ref.id));
        break;
    }

	default:
		return false;
	}
	return true;
}

} // namespace havel::compiler
