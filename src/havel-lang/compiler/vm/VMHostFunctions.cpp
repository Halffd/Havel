#include "VM.hpp"
#include "VMInternals.hpp"
#include "../../../utils/Logger.hpp"
#include "../../runtime/concurrency/Thread.hpp"
#include "../../runtime/concurrency/Fiber.hpp"
#include "../../runtime/concurrency/Scheduler.hpp"
#include "../../runtime/concurrency/DependencyTracker.hpp"
#include "../../runtime/concurrency/WatcherRegistry.hpp"
#include "../runtime/EventQueue.hpp"
#include "../runtime/HostBridge.hpp"
#include "../prototypes/PrototypeRegistry.hpp"

#include <iostream>
#include <sstream>

namespace havel::compiler {

void VM::registerDefaultHostFunctions() {
  // Register print as both host function AND global (for closure access)
  registerHostFunction("print", [this](const std::vector<Value> &args) {
    // Check if last arg is kwargs object (has end= or delim=)
    std::string delim = " ";
    std::string end = "\n";
    size_t argCount = args.size();

    // Check for kwargs object as last argument
    bool hasKwargs = false;
    if (!args.empty() && args.back().isObjectId()) {
      auto *kwargsObj = heap_.object(args.back().asObjectId());
      if (kwargsObj) {
        auto itEnd = kwargsObj->find("end");
        bool foundEnd = itEnd != kwargsObj->end();
        auto itDelim = kwargsObj->find("delim");
        bool foundDelim = itDelim != kwargsObj->end();
        if (foundEnd) {
          end = resolveStringKey(itEnd->second);
        }
        if (foundDelim) {
          delim = resolveStringKey(itDelim->second);
        }
        // Only treat as kwargs if it has at least one of end/delim
        if (foundEnd || foundDelim) {
          hasKwargs = true;
        }
      }
    }
    if (hasKwargs) {
      argCount--; // Don't count kwargs as a value to print
    }

    // Print values with delimiter
    for (size_t i = 0; i < argCount; ++i) {
      if (i > 0) {
        std::cout << delim;
      }
      // For string values, resolve them; for other types use heap-aware toString
      const auto &arg = args[i];
      if (arg.isStringValId() || arg.isStringId() || arg.isRegexValId()) {
        std::cout << resolveStringKey(arg);
      } else {
        std::string s = toString(arg);
        std::cout << s;
      }
    }
    std::cout << end;
    return Value::makeNull();
    });

    registerHostFunction("repr", [this](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("repr() requires an argument");
        const auto &arg = args[0];
        if (arg.isObjectId()) {
            Value opMethod = getHostObjectField(ObjectRef{arg.asObjectId(), true}, "op_repr");
            if (!opMethod.isNull() && (opMethod.isFunctionObjId() || opMethod.isClosureId() || opMethod.isHostFuncId())) {
                Value result = callFunctionSync(opMethod, {arg});
                if (result.isStringValId() && current_chunk) {
                    return result;
                } else if (result.isStringId()) {
                    return result;
                }
            }
        }
        return Value::makeStringId(heap_.allocateString(toString(arg)).id);
    });

    registerHostFunction("copy", [this](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("copy() requires an argument");
        const auto &arg = args[0];
        if (arg.isObjectId()) {
            Value opMethod = getHostObjectField(ObjectRef{arg.asObjectId(), true}, "op_copy");
            if (!opMethod.isNull() && (opMethod.isFunctionObjId() || opMethod.isClosureId() || opMethod.isHostFuncId())) {
                return callFunctionSync(opMethod, {arg});
            }
        }
        return arg;
    });

    registerHostFunction("code", [this](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("code() requires an argument");
        const auto &arg = args[0];
        if (arg.isObjectId()) {
            Value opMethod = getHostObjectField(ObjectRef{arg.asObjectId(), true}, "op_code");
            if (!opMethod.isNull() && (opMethod.isFunctionObjId() || opMethod.isClosureId() || opMethod.isHostFuncId())) {
                Value result = callFunctionSync(opMethod, {arg});
                return result;
            }
        }
        return Value::makeStringId(heap_.allocateString(toString(arg)).id);
    });

    // fmt(format_string, ...) - Python-style string formatting
  registerHostFunction("fmt", [this](const std::vector<Value> &args) {
    if (args.empty()) {
      COMPILER_THROW("fmt() requires at least a format string");
    }

    // Get format string
    if (!args[0].isStringValId()) {
      COMPILER_THROW("fmt() format must be a string");
    }
    // TODO: string pool lookup
    std::string formatStr = "<string:" + std::to_string(args[0].asStringValId()) + ">";

    // Convert args to strings for formatting
    std::vector<std::string> argStrings;
    for (size_t i = 1; i < args.size(); ++i) {
      argStrings.push_back(toString(args[i]));
    }

    // Simple format string processing: {} placeholders
    std::string result;
    size_t argIndex = 0;
    size_t pos = 0;

    while (pos < formatStr.size()) {
      size_t placeholder = formatStr.find("{}", pos);
      if (placeholder == std::string::npos) {
        // No more placeholders, append rest of string
        result += formatStr.substr(pos);
        break;
      }

      // Append text before placeholder
      result += formatStr.substr(pos, placeholder - pos);

      // Replace placeholder with argument
      if (argIndex < argStrings.size()) {
        result += argStrings[argIndex++];
      } else {
        result += "{}"; // No more args, keep placeholder
      }

      pos = placeholder + 2; // Skip past {}
    }

    return Value::makeNull();
  });

  registerHostFunction("clock_ms", 0, [](const std::vector<Value> &) {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now())
                         .time_since_epoch()
                         .count();
    return Value::makeInt(static_cast<int64_t>(now));
  });

  registerHostFunction(
      "sleep_ms", 1, [](const std::vector<Value> &args) {
        if (!args[0].isInt()) {
          COMPILER_THROW(
              "sleep_ms expects exactly 1 integer argument");
        }

        int64_t duration_ms = args[0].asInt();
        if (duration_ms < 0) {
          COMPILER_THROW("sleep_ms duration cannot be negative");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
        return Value::makeNull();
      });

  // Read a line from stdin
  registerHostFunction("input", [this](const std::vector<Value> &args) {
    // Optional prompt
    if (!args.empty()) {
      std::string prompt = resolveStringKey(args[0]);
      std::cout << prompt << std::flush;
    }
    std::string line;
    std::getline(std::cin, line);
    auto strRef = heap_.allocateString(line);
    return Value::makeStringId(strRef.id);
  });

  // globals() - returns count of global variables
  registerHostFunction("globals", [this](const std::vector<Value> &args) {
    (void)args;
    int64_t count = static_cast<int64_t>(globals.size() + host_function_globals_.size());
    return Value::makeInt(count);
  });

  // Enhanced sleep() with duration string support
  registerHostFunction(
      "sleep", 1, [this](const std::vector<Value> &args) {
        if (args.empty()) {
          COMPILER_THROW("sleep() requires one argument");
        }

        auto duration_ms = parseDuration(args[0]);
        if (!duration_ms) {
          COMPILER_THROW(
              "sleep(): invalid duration format. Use numbers (ms) or strings "
              "like '1s', '500ms', '2.5m', '1h'");
        }

        if (*duration_ms < 0) {
          COMPILER_THROW("sleep(): duration cannot be negative");
        }

if (scheduler_ && executing_in_fiber_) {
// Use the VM's goroutine suspension mechanism instead of blocking.
// This lets the scheduler put the goroutine to sleep and resume it
// after the duration, allowing the event loop to process input
// events (including exit requests from other hotkeys) while we wait.
// Only valid when running inside a fiber/goroutine context.
suspension_requested_ = true;
suspension_reason_ = static_cast<uint8_t>(SuspensionReason::SLEEP);
suspension_context_ = reinterpret_cast<void*>(
static_cast<intptr_t>(*duration_ms));
return Value::makeNull();
}

        // No scheduler — fall back to blocking sleep with yields
        {
          const int SLEEP_CHUNK_MS = 10;
          auto end_time = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(*duration_ms);
          while (std::chrono::steady_clock::now() < end_time) {
            if (exit_requested_.load()) return Value::makeNull();
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - std::chrono::steady_clock::now());
            auto chunk = std::min(static_cast<int>(remaining.count()), SLEEP_CHUNK_MS);
            if (chunk > 0) {
              execution_mutex_.lock();
              std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
              execution_mutex_.unlock();
            }
if (timer_check_func_) timer_check_func_();
if (event_queue_) event_queue_->processAll();
          }
        }
        return Value::makeNull();
      });

  // Type conversion builtins
  registerHostFunction("int", 1, [this](const std::vector<Value> &args) {
    return Value(toInt(args[0]));
  });

  registerHostFunction("num", 1, [this](const std::vector<Value> &args) {
    return Value(toFloat(args[0]));
  });

  // Instrumentation: assert(condition, message?)
  registerHostFunction("assert", [this](const std::vector<Value> &args) {
    if (args.empty()) {
      COMPILER_THROW(
          "assert() requires at least a condition argument");
    }
    bool condition = toBool(args[0]);
    if (!condition) {
      std::string msg = "Assertion failed";
      if (args.size() > 1 &&
          (args[1].isStringValId() || args[1].isStringId() || args[1].isRegexValId())) {
        msg = resolveStringKey(args[1]);
      }
      COMPILER_THROW(msg);
    }
    return Value::makeNull();
  });

  registerHostFunction("panic", [this](const std::vector<Value> &args) {
    std::string msg = "panic";
    if (!args.empty()) {
      msg = resolveStringKey(args[0]);
    }
    COMPILER_THROW(msg);
    return Value::makeNull();
  });

  // exit(code) - request clean program shutdown
  // Instead of calling std::exit() (which crashes during static destruction
  // while Qt widgets are alive), we set a flag on the VM for cooperative
  // shutdown. The EventListener detects this and stops cleanly.
    registerHostFunction("exit", [this](const std::vector<Value> &args) {
        int exit_code = 0;
        if (!args.empty() && args[0].isInt()) {
            exit_code = static_cast<int>(args[0].asInt());
        }
        exit_requested_ = true;
        exit_code_ = exit_code;
        return Value::makeNull();
    });

  // Performance: clock_ns() - high-resolution clock in nanoseconds
  registerHostFunction("clock_ns", 0, [](const std::vector<Value> &) {
    const auto now = std::chrono::time_point_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now())
                         .time_since_epoch()
                         .count();
    return Value::makeInt(static_cast<int64_t>(now));
  });

  // Performance: clock_us() - clock in microseconds
  registerHostFunction("clock_us", 0, [](const std::vector<Value> &) {
    const auto now = std::chrono::time_point_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now())
                         .time_since_epoch()
                         .count();
    return Value::makeInt(static_cast<int64_t>(now));
  });

  // str() builtin returns string representation
  registerHostFunction("str", 1, [this](const std::vector<Value> &args) {
    std::string str = this->toString(args[0]);
    auto strRef = heap_.allocateString(str);
    return Value::makeStringId(strRef.id);
  });

  // ord() builtin returns Unicode code point of first character
  registerHostFunction("ord", 1, [this](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeNull();
    std::string s = this->toString(args[0]);
    if (s.empty()) return Value::makeNull();
    unsigned char b0 = static_cast<unsigned char>(s[0]);
    uint32_t cp = 0;
    if (b0 < 0x80) { cp = b0; }
    else if ((b0 & 0xE0) == 0xC0 && s.size() >= 2) {
      cp = ((b0 & 0x1F) << 6) | (static_cast<unsigned char>(s[1]) & 0x3F);
    } else if ((b0 & 0xF0) == 0xE0 && s.size() >= 3) {
      cp = ((b0 & 0x0F) << 12) | ((static_cast<unsigned char>(s[1]) & 0x3F) << 6)
         | (static_cast<unsigned char>(s[2]) & 0x3F);
    } else if ((b0 & 0xF8) == 0xF0 && s.size() >= 4) {
      cp = ((b0 & 0x07) << 18) | ((static_cast<unsigned char>(s[1]) & 0x3F) << 12)
         | ((static_cast<unsigned char>(s[2]) & 0x3F) << 6)
         | (static_cast<unsigned char>(s[3]) & 0x3F);
    } else { return Value::makeNull(); }
    return Value::makeInt(static_cast<int64_t>(cp));
  });

  // char() builtin returns string from Unicode code point
  registerHostFunction("char", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isInt()) return Value::makeNull();
    int64_t cp = args[0].asInt();
    if (cp < 0 || cp > 0x10FFFF) return Value::makeNull();
    std::string result;
    if (cp < 0x80) {
      result += static_cast<char>(cp);
    } else if (cp < 0x800) {
      result += static_cast<char>(0xC0 | (cp >> 6));
      result += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      result += static_cast<char>(0xE0 | (cp >> 12));
      result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      result += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      result += static_cast<char>(0xF0 | (cp >> 18));
      result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      result += static_cast<char>(0x80 | (cp & 0x3F));
    }
    auto strRef = heap_.allocateString(result);
    return Value::makeStringId(strRef.id);
  });

// type() builtin returns type name
  registerHostFunction("type", 1, [this](const std::vector<Value> &args) {
    const auto &value = args[0];
    std::string typeName;
    if (value.isNull()) typeName = "null";
    else if (value.isBool()) typeName = "bool";
    else if (value.isInt()) typeName = "int";
    else if (value.isDouble()) typeName = "float";
    else if (value.isStringValId() || value.isStringId() || value.isRegexValId()) typeName = "string";
    else if (value.isArrayId()) typeName = "array";
    else if (value.isObjectId()) typeName = "object";
    else if (value.isSetId()) typeName = "set";
    else if (value.isRangeId()) typeName = "range";
    else if (value.isHostFuncId()) typeName = "function";
    else if (value.isClosureId()) typeName = "closure";
    else if (value.isFunctionObjId()) typeName = "function";
    else if (value.isEnumId()) typeName = "enum";
    else if (value.isIteratorId()) typeName = "iterator";
    else if (value.isBoundMethodId()) typeName = "fn";
    else if (value.isCoroutineId()) typeName = "coroutine";
    else typeName = "unknown";
    auto strRef = heap_.allocateString(typeName);
    return Value::makeStringId(strRef.id);
  });

    // ========================================================================
    // Duck typing / Protocol functions
    // ========================================================================

    // iter(x) - Get an iterator for any iterable type
    registerHostFunction("iter", 1, [this](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeNull();
    const auto &value = args[0];
    // Check if value is iterable
    if (value.isArrayId() || value.isStringId() || value.isStringValId() || value.isRegexValId() ||
        value.isObjectId() || value.isSetId() || value.isRangeId()) {
      uint32_t iterId = heap_.createIterator(value);
      return Value::makeIteratorId(iterId);
    }
    return Value::makeNull();
  });

  // next(iter) - Get next value from iterator
  registerHostFunction("next", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isIteratorId()) return Value::makeNull();
    return heap_.iteratorNext(args[0].asIteratorId());
  });

  // callable(x) - Check if value can be called
  registerHostFunction("callable", 1, [this](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    const auto &value = args[0];
    bool isCallable = value.isFunctionObjId() || value.isClosureId() ||
                       value.isHostFuncId() || value.isBoundMethodId();
    return Value::makeBool(isCallable);
  });

  // hasattr(obj, name) - Check if object has attribute/method
  registerHostFunction("hasattr", 2, [this](const std::vector<Value> &args) {
    if (args.size() < 2) return Value::makeBool(false);
    std::string name;
    if (args[1].isStringValId() && current_chunk) {
      name = current_chunk->getString(args[1].asStringValId());
    } else if (args[1].isStringId() && heap_.string(args[1].asStringId())) {
      name = *heap_.string(args[1].asStringId());
    } else {
      return Value::makeBool(false);
    }
    if (args[0].isObjectId()) {
      if (args[0].asObjectId() == globals_mirror_object_id_) {
        return Value::makeBool(globals.find(name) != globals.end() ||
                               host_function_globals_.find(name) != host_function_globals_.end());
      }
      auto *obj = heap_.object(args[0].asObjectId());
      return Value::makeBool(obj && obj->find(name) != obj->end());
    }
    return Value::makeBool(false);
  });

  // isIterable(x) - Check if value can be iterated
  registerHostFunction("isIterable", 1, [this](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    const auto &value = args[0];
    bool isIterable = value.isArrayId() || value.isStringId() ||
                      value.isStringValId() || value.isRegexValId() || value.isObjectId() ||
                      value.isSetId() || value.isRangeId() ||
                      value.isIteratorId();
    return Value::makeBool(isIterable);
  });

  // isIndexable(x) - Check if value supports indexing
  registerHostFunction("isIndexable", 1, [this](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    const auto &value = args[0];
    bool isIndexable = value.isArrayId() || value.isStringId() ||
                       value.isStringValId() || value.isRegexValId() || value.isObjectId() ||
                       value.isSetId();
    return Value::makeBool(isIndexable);
  });

  // ========================================================================
  // Function introspection
  // ========================================================================

  // function.name(fn) - Get function name
  registerHostFunction("function.name", 1, [this](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeNull();
    Value fn = args[0];
    
    if (fn.isFunctionObjId()) {
      uint32_t idx = fn.asFunctionObjId();
      if (current_chunk && idx < current_chunk->getFunctionCount()) {
        const auto *bf = current_chunk->getFunction(idx);
        if (bf) {
          auto strRef = heap_.allocateString(bf->name);
          return Value::makeStringId(strRef.id);
        }
      }
    } else if (fn.isClosureId()) {
      auto *closure = heap_.closure(fn.asClosureId());
      if (closure && current_chunk) {
        if (closure->function_index < current_chunk->getFunctionCount()) {
          const auto *bf = current_chunk->getFunction(closure->function_index);
          if (bf) {
            auto strRef = heap_.allocateString(bf->name);
            return Value::makeStringId(strRef.id);
          }
        }
      }
    }
    return Value::makeNull();
  });

  // function.arity(fn) - Get function parameter count
  registerHostFunction("function.arity", 1, [this](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeInt(0);
    Value fn = args[0];
    
    if (fn.isFunctionObjId()) {
      uint32_t idx = fn.asFunctionObjId();
      if (current_chunk && idx < current_chunk->getFunctionCount()) {
        const auto *bf = current_chunk->getFunction(idx);
        if (bf) return Value::makeInt(bf->param_count);
      }
    } else if (fn.isClosureId()) {
      auto *closure = heap_.closure(fn.asClosureId());
      if (closure && current_chunk) {
        if (closure->function_index < current_chunk->getFunctionCount()) {
          const auto *bf = current_chunk->getFunction(closure->function_index);
          if (bf) return Value::makeInt(bf->param_count);
        }
      }
    }
    return Value::makeInt(0);
  });

  // function.params(fn) - Get function parameter names as array
  registerHostFunction("function.params", 1, [this](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeNull();
    Value fn = args[0];
    
    ArrayRef arrRef = heap_.allocateArray();
    auto *arr = heap_.array(arrRef.id);
    
    if (fn.isFunctionObjId()) {
      uint32_t idx = fn.asFunctionObjId();
      if (current_chunk && idx < current_chunk->getFunctionCount()) {
        const auto *bf = current_chunk->getFunction(idx);
        if (bf) {
          for (const auto &pname : bf->param_names) {
            auto strRef = heap_.allocateString(pname);
            arr->push_back(Value::makeStringId(strRef.id));
          }
          return Value::makeArrayId(arrRef.id);
        }
      }
    } else if (fn.isClosureId()) {
      auto *closure = heap_.closure(fn.asClosureId());
      if (closure && current_chunk) {
        if (closure->function_index < current_chunk->getFunctionCount()) {
          const auto *bf = current_chunk->getFunction(closure->function_index);
          if (bf) {
            for (const auto &pname : bf->param_names) {
              auto strRef = heap_.allocateString(pname);
              arr->push_back(Value::makeStringId(strRef.id));
            }
            return Value::makeArrayId(arrRef.id);
          }
        }
      }
    }
    return Value::makeNull();
  });

  // function.source(fn) - Get source location as "file:line" string
  registerHostFunction("function.source", 1, [this](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeNull();
    Value fn = args[0];
    
    if (fn.isFunctionObjId()) {
      uint32_t idx = fn.asFunctionObjId();
      if (current_chunk && idx < current_chunk->getFunctionCount()) {
        const auto *bf = current_chunk->getFunction(idx);
        if (bf) {
          std::string loc = bf->source_file.empty() ? "" : bf->source_file;
          if (bf->source_line > 0) {
            loc += (loc.empty() ? "" : ":") + std::to_string(bf->source_line);
          }
          if (!loc.empty()) {
            auto strRef = heap_.allocateString(loc);
            return Value::makeStringId(strRef.id);
          }
        }
      }
    } else if (fn.isClosureId()) {
      auto *closure = heap_.closure(fn.asClosureId());
      if (closure && current_chunk) {
        if (closure->function_index < current_chunk->getFunctionCount()) {
          const auto *bf = current_chunk->getFunction(closure->function_index);
          if (bf) {
            std::string loc = bf->source_file.empty() ? "" : bf->source_file;
            if (bf->source_line > 0) {
              loc += (loc.empty() ? "" : ":") + std::to_string(bf->source_line);
            }
            if (!loc.empty()) {
              auto strRef = heap_.allocateString(loc);
              return Value::makeStringId(strRef.id);
            }
          }
        }
      }
    }
    return Value::makeNull();
  });

  // Create function prototype object for introspection
  {
    ObjectRef funcProto = heap_.allocateObject();
    auto *funcObj = heap_.object(funcProto.id);
    
    // function.name(fn)
    {
      auto it = host_function_globals_.find("function.name");
      if (it != host_function_globals_.end()) {
        funcObj->set("name", it->second);
      }
    }
    // function.arity(fn)
    {
      auto it = host_function_globals_.find("function.arity");
      if (it != host_function_globals_.end()) {
        funcObj->set("arity", it->second);
      }
    }
    // function.params(fn)
    {
      auto it = host_function_globals_.find("function.params");
      if (it != host_function_globals_.end()) {
        funcObj->set("params", it->second);
      }
    }
    // function.source(fn)
    {
      auto it = host_function_globals_.find("function.source");
      if (it != host_function_globals_.end()) {
        funcObj->set("source", it->second);
      }
    }
    
    globals["function"] = Value::makeObjectId(funcProto.id);
  }

    registerHostFunction("async.await", 1, [this](const std::vector<Value> &args) {
        if (args.empty()) return Value::makeNull();
        return args[0];
    });
    registerHostFunction("await", 1, [this](const std::vector<Value> &args) {
        if (args.empty()) return Value::makeNull();
        return args[0];
    });

    // app.restart() - restart the application
  registerHostFunction("app.restart", 0, [this](const std::vector<Value> &) {
    if (restart_callback_) {
      restart_callback_();
    }
    return Value::makeNull();
  });

  // ========================================================================
  // Thread, Interval, and Timeout - Concurrency primitives
  // ========================================================================

 // thread { closure } - Create and start a message-passing thread
 registerHostFunction("thread", 1, [this](const std::vector<Value> &args) {
 if (args.empty() || (!args[0].isClosureId() && !args[0].isFunctionObjId())) {
 COMPILER_THROW("thread requires a closure argument");
 }

 auto threadObj = std::make_shared<Thread>();
 auto closure = args[0];
 auto threadIdPtr = std::make_shared<uint32_t>(0);

 // Create message handler that invokes the closure
 auto handler = [this, closure, threadIdPtr](const Thread::Message &msg) {
 try {
 Value arg;
 if (std::holds_alternative<std::string>(msg)) {
 auto strRef = heap_.allocateString(std::get<std::string>(msg));
 arg = Value::makeStringId(strRef.id);
 } else if (std::holds_alternative<int>(msg)) {
 arg = Value::makeInt(std::get<int>(msg));
 } else if (std::holds_alternative<double>(msg)) {
 arg = Value::makeDouble(std::get<double>(msg));
 }

 Value result = this->callFunction(closure, {arg});
 thread_results_[*threadIdPtr] = result;
 } catch (const std::exception &e) {
 ::havel::error("[thread] Exception: {}", e.what());
 }
 };

 threadObj->start(std::move(handler));

 // Store thread in GC heap and return wrapper object
 auto threadRef = heap_.allocateThreadObj(threadObj);
 *threadIdPtr = threadRef.id;
 return Value::makeThreadId(threadRef.id);
 });

  // thread.send(thread, message) - Send message to thread
  registerHostFunction("thread.send", 2, [this](const std::vector<Value> &args) {
    if (args.size() < 2 || !args[0].isThreadId()) {
      COMPILER_THROW("thread.send requires a thread object and message");
    }
    
    auto *threadObj = heap_.thread(args[0].asThreadId());
    if (!threadObj) {
      COMPILER_THROW("thread.send: invalid thread reference");
    }
    
    // Convert message Value to Thread::Message
    Thread::Message msg;
    if (args[1].isStringValId()) {
      auto *str = heap_.string(args[1].asStringValId());
      if (str) {
        msg = *str;
      } else {
        COMPILER_THROW("thread.send: invalid string reference");
      }
    } else if (args[1].isInt()) {
      msg = static_cast<int>(args[1].asInt());
    } else if (args[1].isDouble() || args[1].isNumber()) {
      msg = args[1].isDouble() ? args[1].asDouble() : static_cast<double>(args[1].asInt());
    } else {
      COMPILER_THROW("thread.send: message must be string, int, or number");
    }
    
    threadObj->send(msg);
    return Value::makeNull();
  });

  // thread.pause(thread) - Pause thread
  registerHostFunction("thread.pause", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isThreadId()) {
      COMPILER_THROW("thread.pause requires a thread argument");
    }
    
    auto *threadObj = heap_.thread(args[0].asThreadId());
    if (!threadObj) {
      COMPILER_THROW("thread.pause: invalid thread reference");
    }
    
    threadObj->pause();
    return Value::makeNull();
  });

  // thread.resume(thread) - Resume thread
  registerHostFunction("thread.resume", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isThreadId()) {
      COMPILER_THROW("thread.resume requires a thread argument");
    }
    
    auto *threadObj = heap_.thread(args[0].asThreadId());
    if (!threadObj) {
      COMPILER_THROW("thread.resume: invalid thread reference");
    }
    
    threadObj->resume();
    return Value::makeNull();
  });

  // thread.stop(thread) - Stop thread
  registerHostFunction("thread.stop", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isThreadId()) {
      COMPILER_THROW("thread.stop requires a thread argument");
    }
    
    auto *threadObj = heap_.thread(args[0].asThreadId());
    if (!threadObj) {
      COMPILER_THROW("thread.stop: invalid thread reference");
    }
    
    threadObj->stop();
    return Value::makeNull();
  });

  // thread.running(thread) -> bool - Check if thread is running
  registerHostFunction("thread.running", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isThreadId()) {
      COMPILER_THROW("thread.running requires a thread argument");
    }
    
    auto *threadObj = heap_.thread(args[0].asThreadId());
    if (!threadObj) {
      return Value::makeBool(false);
    }
    
        return Value::makeBool(threadObj->isRunning());
    });

    // thread.spawn(closure) - alias for thread(closure), used by ThreadExpression
    registerHostFunction("thread.spawn", 1, [this](const std::vector<Value> &args) {
        if (args.empty() || (!args[0].isClosureId() && !args[0].isFunctionObjId())) {
            COMPILER_THROW("thread.spawn requires a closure argument");
        }
        auto threadObj = std::make_shared<Thread>();
        auto closure = args[0];
        auto handler = [this, closure](const Thread::Message &msg) {
            try {
                Value arg;
                if (std::holds_alternative<std::string>(msg)) {
                    auto strRef = heap_.allocateString(std::get<std::string>(msg));
                    arg = Value::makeStringId(strRef.id);
                } else if (std::holds_alternative<int>(msg)) {
                    arg = Value::makeInt(std::get<int>(msg));
                } else if (std::holds_alternative<double>(msg)) {
                    arg = Value::makeDouble(std::get<double>(msg));
                }
                this->callFunction(closure, {arg});
            } catch (const std::exception &e) {
                ::havel::error("[thread] Exception: {}", e.what());
            }
        };
        threadObj->start(std::move(handler));
        auto threadRef = heap_.allocateThreadObj(threadObj);
        return Value::makeThreadId(threadRef.id);
    });

// interval(ms, closure) - Create repeating timer
registerHostFunction("interval", 2, [this](const std::vector<Value> &args) {
if (args.size() < 2 || !args[0].isNumber()) {
COMPILER_THROW("interval requires milliseconds and closure");
}
if (!args[1].isClosureId() && !args[1].isFunctionObjId()) {
COMPILER_THROW("interval requires a closure argument");
}

int ms = toInt(args[0]);
auto closure = args[1];
auto intervalIdPtr = std::make_shared<uint32_t>(0);

	auto callback = [this, closure, intervalIdPtr]() {
		if (event_queue_) {
auto *payload = new std::pair<Value, uint32_t>(closure, *intervalIdPtr);
event_queue_->push(Event(EventType::TIMER_FIRE, 0, payload));
} else {
try {
Value result = this->callFunction(closure, {});
interval_results_[*intervalIdPtr] = result;
} catch (const std::exception &e) {
::havel::error("[interval] Exception: {}", e.what());
}
}
};

auto intervalObj = std::make_shared<Interval>(ms, std::move(callback));
 auto intervalRef = heap_.allocateIntervalObj(intervalObj);
 *intervalIdPtr = intervalRef.id;
 return Value::makeIntervalId(intervalRef.id);
 });

  // interval.pause(interval) - Pause interval
registerHostFunction("interval.pause", 1, [this](const std::vector<Value> &args) {
if (args.empty() || !args[0].isIntervalId()) {
COMPILER_THROW("interval.pause requires an interval argument");
}

auto *intervalObj = heap_.interval(args[0].asIntervalId());
if (!intervalObj) {
COMPILER_THROW("interval.pause: invalid interval reference");
}
    
    intervalObj->pause();
    return Value::makeNull();
  });

  // interval.resume(interval) - Resume interval
  registerHostFunction("interval.resume", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isIntervalId()) {
      COMPILER_THROW("interval.resume requires an interval argument");
    }
    
    auto *intervalObj = heap_.interval(args[0].asIntervalId());
    if (!intervalObj) {
      COMPILER_THROW("interval.resume: invalid interval reference");
    }
    
    intervalObj->resume();
    return Value::makeNull();
  });

  // interval.stop(interval) - Stop interval
  registerHostFunction("interval.stop", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isIntervalId()) {
      COMPILER_THROW("interval.stop requires an interval argument");
    }
    
    auto *intervalObj = heap_.interval(args[0].asIntervalId());
    if (!intervalObj) {
      COMPILER_THROW("interval.stop: invalid interval reference");
    }
    
        intervalObj->stop();
        return Value::makeNull();
    });

    // interval.start(ms, closure) - alias for interval(ms, closure), used by IntervalExpression
registerHostFunction("interval.start", 2, [this](const std::vector<Value> &args) {
 if (args.size() < 2 || !args[0].isNumber()) {
 COMPILER_THROW("interval.start requires milliseconds and closure");
 }
 if (!args[1].isClosureId() && !args[1].isFunctionObjId()) {
 COMPILER_THROW("interval.start requires a closure argument");
 }
 int ms = toInt(args[0]);
 auto closure = args[1];
 auto intervalIdPtr = std::make_shared<uint32_t>(0);
auto callback = [this, closure, intervalIdPtr]() {
if (event_queue_) {
auto *payload = new std::pair<Value, uint32_t>(closure, *intervalIdPtr);
event_queue_->push(Event(EventType::TIMER_FIRE, 0, payload));
} else {
try {
Value result = this->callFunction(closure, {});
interval_results_[*intervalIdPtr] = result;
} catch (const std::exception &e) {
::havel::error("[interval] Exception: {}", e.what());
}
}
};
auto intervalObj = std::make_shared<Interval>(ms, std::move(callback));
auto intervalRef = heap_.allocateIntervalObj(intervalObj);
*intervalIdPtr = intervalRef.id;
return Value::makeIntervalId(intervalRef.id);
});

// timeout(ms, closure) - Create one-shot delayed execution
registerHostFunction("timeout", 2, [this](const std::vector<Value> &args) {
 if (args.size() < 2 || !args[0].isNumber()) {
 COMPILER_THROW("timeout requires milliseconds and closure");
 }
 if (!args[1].isClosureId() && !args[1].isFunctionObjId()) {
 COMPILER_THROW("timeout requires a closure argument");
 }

 int ms = toInt(args[0]);
 auto closure = args[1];
 auto timeoutIdPtr = std::make_shared<uint32_t>(0);

auto callback = [this, closure, timeoutIdPtr]() {
if (event_queue_) {
auto *payload = new std::pair<Value, uint32_t>(closure, *timeoutIdPtr);
event_queue_->push(Event(EventType::TIMER_FIRE, 1, payload));
} else {
try {
Value result = this->callFunction(closure, {});
timeout_results_[*timeoutIdPtr] = result;
} catch (const std::exception &e) {
::havel::error("[timeout] Exception: {}", e.what());
}
}
};

auto timeoutObj = std::make_shared<Timeout>(ms, std::move(callback));
 auto timeoutRef = heap_.allocateTimeoutObj(timeoutObj);
 *timeoutIdPtr = timeoutRef.id;
 return Value::makeTimeoutId(timeoutRef.id);
 });

  // timeout.cancel(timeout) - Cancel timeout
  registerHostFunction("timeout.cancel", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isTimeoutId()) {
      COMPILER_THROW("timeout.cancel requires a timeout argument");
    }
    
    auto *timeoutObj = heap_.timeout(args[0].asTimeoutId());
    if (!timeoutObj) {
      COMPILER_THROW("timeout.cancel: invalid timeout reference");
    }
    
        timeoutObj->cancel();
        return Value::makeNull();
    });

 // timeout.start(ms, closure) - alias for timeout(ms, closure), used by TimeoutExpression
 registerHostFunction("timeout.start", 2, [this](const std::vector<Value> &args) {
 if (args.size() < 2 || !args[0].isNumber()) {
 COMPILER_THROW("timeout.start requires milliseconds and closure");
 }
 if (!args[1].isClosureId() && !args[1].isFunctionObjId()) {
 COMPILER_THROW("timeout.start requires a closure argument");
 }
 int ms = toInt(args[0]);
 auto closure = args[1];
 auto timeoutIdPtr = std::make_shared<uint32_t>(0);
auto callback = [this, closure, timeoutIdPtr]() {
if (event_queue_) {
auto *payload = new std::pair<Value, uint32_t>(closure, *timeoutIdPtr);
event_queue_->push(Event(EventType::TIMER_FIRE, 1, payload));
} else {
try {
Value result = this->callFunction(closure, {});
timeout_results_[*timeoutIdPtr] = result;
} catch (const std::exception &e) {
::havel::error("[timeout] Exception: {}", e.what());
}
}
};
auto timeoutObj = std::make_shared<Timeout>(ms, std::move(callback));
auto timeoutRef = heap_.allocateTimeoutObj(timeoutObj);
*timeoutIdPtr = timeoutRef.id;
return Value::makeTimeoutId(timeoutRef.id);
});

    // GC control
    // "system.gc" is called via method dispatch (system.gc()) which prepends
    // the receiver, so it needs arity 1. The underscore alias "system_gc"
    // is called directly without a receiver, so arity 0.
registerHostFunction("system.gc", 0, [this](const std::vector<Value> &) {
  runGarbageCollection();
  return Value::makeNull();
});
registerHostFunction("system_gc", 0, [this](const std::vector<Value> &) {
  runGarbageCollection();
  return Value::makeNull();
});

registerHostFunction("system.gcStats", 0, [this](const std::vector<Value> &) {
        const auto stats = gcStats();
        const auto object_ref = createHostObject();
        setHostObjectField(object_ref, "heapSize",
        Value::makeInt(static_cast<int64_t>(stats.heap_size)));
        setHostObjectField(object_ref, "objectCount",
        Value::makeInt(static_cast<int64_t>(stats.object_count)));
        setHostObjectField(object_ref, "collections",
        Value::makeInt(static_cast<int64_t>(stats.collections)));
        setHostObjectField(object_ref, "lastPauseNs",
        Value::makeInt(static_cast<int64_t>(stats.last_pause_ns)));
        return Value::makeObjectId(object_ref.id);
    });
    registerHostFunction("system_gcStats", 0, [this](const std::vector<Value> &) {
        const auto stats = gcStats();
        const auto object_ref = createHostObject();
        setHostObjectField(object_ref, "heapSize",
        Value::makeInt(static_cast<int64_t>(stats.heap_size)));
        setHostObjectField(object_ref, "objectCount",
        Value::makeInt(static_cast<int64_t>(stats.object_count)));
        setHostObjectField(object_ref, "collections",
        Value::makeInt(static_cast<int64_t>(stats.collections)));
        setHostObjectField(object_ref, "lastPauseNs",
        Value::makeInt(static_cast<int64_t>(stats.last_pause_ns)));
        return Value::makeObjectId(object_ref.id);
    });

  // Struct operations (prototype-based)
registerHostFunction(
        "struct.define", [this](const std::vector<Value> &args) {
        size_t offset = (args.size() >= 3 && args[0].isObjectId()) ? 1 : 0;
        if (args.size() - offset < 2) COMPILER_THROW("struct.define requires name and fields");
        if (!current_chunk) COMPILER_THROW("struct.define requires active chunk");

        auto protoRef = heap_.allocateObject();
        auto* proto = heap_.object(protoRef.id);

        proto->set("__name", Value::makeStringValId(args[offset].asStringValId()));
        proto->set("__is_struct", Value::makeBool(true));
        proto->set("__fields", args[offset + 1]);
        if (args.size() - offset >= 3 && args[offset + 2].isArrayId()) {
            proto->set("__defaults", args[offset + 2]);
        }

        return Value::makeObjectId(protoRef.id);
    });

  registerHostFunction(
  "struct.method", [this](const std::vector<Value> &args) {
    if (args.size() != 3 || !args[1].isStringValId() ||
        !args[2].isFunctionObjId()) {
      COMPILER_THROW("struct.method expects (structType, methodNameString, functionObj)");
    }
    if (!current_chunk) COMPILER_THROW("struct.method requires active chunk");

    auto* structObj = heap_.object(args[0].asObjectId());
    if (!structObj) return Value::makeNull();

    const std::string &method_name = current_chunk->getString(args[1].asStringValId());
    structObj->set(method_name, args[2]);

    auto nameVal = structObj->get("__name");
    if (nameVal && nameVal->isStringValId()) {
      const std::string& structName = current_chunk->getString(nameVal->asStringValId());
      setGlobal(structName + "." + method_name, args[2]);
    }

    return Value::makeNull();
  });

  registerHostFunction(
  "struct.new", [this](const std::vector<Value> &args) {
        if (args.empty()) {
          COMPILER_THROW("struct.new(type, ...values) requires a type argument");
        }
        
        if (!current_chunk) COMPILER_THROW("struct.new requires active chunk");
        
        // Determine offset for self argument (when called as method)
        size_t offset = 0;
        if (args.size() >= 3 && args[0].isObjectId() && args[1].isObjectId()) {
          offset = 1; // Skip self
        }
        
        Value protoVal;
        if (args[offset].isObjectId()) {
          // First arg is the prototype object directly
          protoVal = args[offset];
        } else if (args[offset].isStringValId()) {
          // First arg is the name string, look up prototype
          const auto &name = current_chunk->getString(args[offset].asStringValId());
          auto it = globals.find(name);
          if (it == globals.end()) {
              COMPILER_THROW("Unknown struct type: " + name);
          }
          protoVal = it->second;
          if (!protoVal.isObjectId()) {
              COMPILER_THROW("Struct type is not an object prototype: " + name);
          }
        } else {
          COMPILER_THROW("struct.new requires prototype object or type name");
        }

        auto* proto = heap_.object(protoVal.asObjectId());
        auto fieldsVal = proto->get("__fields");
        if (!fieldsVal || !fieldsVal->isArrayId()) {
            COMPILER_THROW("Struct prototype missing __fields array");
        }

        auto* fields = heap_.array(fieldsVal->asArrayId());
        
        auto instanceRef = heap_.allocateObject();
        auto* instance = heap_.object(instanceRef.id);
        
  instance->set("__struct", protoVal); // set prototype

  // Initialize fields from positional arguments (fallback if no init method)
  const size_t provided = args.size() - 1 - offset;

  // Look for init method on prototype (like class.new does)
  Value initMethodVal = Value::makeNull();
  auto* currentProto = proto;
  while (currentProto) {
    auto val = currentProto->get("init");
    if (val) {
      initMethodVal = *val;
      break;
    }
    auto parentVal = currentProto->get("__parent");
    if (parentVal && parentVal->isObjectId()) {
      currentProto = heap_.object(parentVal->asObjectId());
    } else {
      break;
    }
  }

    if (!initMethodVal.isNull()) {
        // Call init method with instance as first arg
        std::vector<Value> ctor_args;
        ctor_args.reserve(args.size());
        ctor_args.push_back(Value::makeObjectId(instanceRef.id));
        for (size_t i = offset + 1; i < args.size(); ++i) {
            ctor_args.push_back(args[i]);
        }
        (void)call(initMethodVal, ctor_args);
    } else {
        // No init method: set fields from positional arguments, falling back to defaults
            GCHeap::ArrayEntry* defaultsArr = nullptr;
            auto defaultsVal = proto->get("__defaults");
            if (defaultsVal && defaultsVal->isArrayId()) {
                defaultsArr = heap_.array(defaultsVal->asArrayId());
            }
            for (size_t i = 0; i < fields->size(); ++i) {
                std::string fieldName = current_chunk->getString((*fields)[i].asStringValId());
                if (i < provided) {
                    instance->set(fieldName, args[i + 1 + offset]);
                } else if (defaultsArr && i < defaultsArr->size() && !(*defaultsArr)[i].isNull()) {
                    instance->set(fieldName, (*defaultsArr)[i]);
                } else {
                    instance->set(fieldName, Value::makeNull());
                }
            }
    }

  return Value::makeObjectId(instanceRef.id);
      });

  // struct.get(instance, field_name)
  registerHostFunction(
      "struct.get", [this](const std::vector<Value> &args) {
        // Handle self offset for method calls
        size_t offset = 0;
        if (args.size() >= 3 && args[0].isObjectId() && args[1].isObjectId()) {
          offset = 1;
        }
        if (args.size() - offset < 2) COMPILER_THROW("struct.get requires instance and field name");
        if (!args[offset].isObjectId()) COMPILER_THROW("struct.get first arg must be object");
        if (!args[offset + 1].isStringValId()) COMPILER_THROW("struct.get second arg must be string");
        
        auto* instance = heap_.object(args[offset].asObjectId());
        std::string fieldName = current_chunk->getString(args[offset + 1].asStringValId());
        auto* val = instance->get(fieldName);
        return val ? *val : Value::makeNull();
      });

  // struct.set(instance, field_name, value)
  registerHostFunction(
      "struct.set", [this](const std::vector<Value> &args) {
        size_t offset = 0;
        if (args.size() >= 4 && args[0].isObjectId()) {
          offset = 1;
        }
        if (args.size() - offset < 3) COMPILER_THROW("struct.set requires instance, field name, and value");
        if (!args[offset].isObjectId()) COMPILER_THROW("struct.set first arg must be object");
        if (!args[offset + 1].isStringValId()) COMPILER_THROW("struct.set second arg must be string");
        
        auto* instance = heap_.object(args[offset].asObjectId());
        std::string fieldName = current_chunk->getString(args[offset + 1].asStringValId());
        instance->set(fieldName, args[offset + 2]);
            return Value::makeNull();
        });

        // Enum operations: enum.define(name, [variantNames], [payloadCounts])
        // Registers the enum type and creates variant constructors as globals
        registerHostFunction(
        "enum.define", [this](const std::vector<Value> &args) {
            if (!current_chunk) COMPILER_THROW("enum.define requires active chunk");
            size_t offset = (args.size() >= 3 && args[0].isObjectId()) ? 1 : 0;
            if (args.size() - offset < 2) COMPILER_THROW("enum.define requires name and variants");

            const std::string &enumName = current_chunk->getString(args[offset].asStringValId());

            auto* variantsArr = heap_.array(args[offset + 1].asArrayId());
            if (!variantsArr) COMPILER_THROW("enum.define variants must be an array");

            std::vector<std::string> variantNames;
            for (size_t i = 0; i < variantsArr->size(); ++i) {
                variantNames.push_back(current_chunk->getString((*variantsArr)[i].asStringValId()));
            }

            uint32_t typeId = heap_.registerEnumType(enumName, variantNames);

            auto enumObj = heap_.allocateObject();
            auto* enumObjPtr = heap_.object(enumObj.id);
            enumObjPtr->set("__name", Value::makeStringValId(args[offset].asStringValId()));
            enumObjPtr->set("__is_enum", Value::makeBool(true));
            enumObjPtr->set("__enum_type_id", Value::makeInt(static_cast<int64_t>(typeId)));

            for (uint32_t tag = 0; tag < variantNames.size(); ++tag) {
                const std::string &variantName = variantNames[tag];
                uint32_t capturedTag = tag;
                uint32_t capturedTypeId = typeId;
                std::string fullName = enumName + "." + variantName;

                bool hasPayload = (args.size() - offset > 2 && args[offset + 2].isArrayId())
                    ? (heap_.array(args[offset + 2].asArrayId())->size() > tag &&
                       (*heap_.array(args[offset + 2].asArrayId()))[tag].asInt() > 0)
                    : false;

                if (hasPayload) {
                    registerHostFunction(fullName, 1, [this, capturedTypeId, capturedTag](const std::vector<Value> &a) {
                        EnumRef ref = heap_.allocateEnum(capturedTypeId, capturedTag, 1);
                        if (!a.empty()) {
                            auto it = heap_.enums_.find(ref.id);
                            if (it != heap_.enums_.end() && !it->second.second.empty())
                                it->second.second[0] = a[0];
                        }
                        return Value::makeEnumId(ref.id, capturedTypeId);
                    });
        } else {
            Value singleton = [&]() -> Value {
                EnumRef ref = heap_.allocateEnum(capturedTypeId, capturedTag, 0);
                return Value::makeEnumId(ref.id, capturedTypeId);
            }();
            registerHostFunction(fullName, 0, [this, singleton](const std::vector<Value> &) -> Value {
                return singleton;
            });
            enumObjPtr->set(variantName, singleton);
            continue;
        }

        uint32_t variantFuncIdx = getHostFunctionIndex(fullName);
        enumObjPtr->set(variantName, Value::makeHostFuncId(variantFuncIdx));
            }

            return Value::makeObjectId(enumObj.id);
        });

        // Class operations (prototype-based)
  registerHostFunction(
      "class.define", [this](const std::vector<Value> &args) {
        if (!current_chunk) COMPILER_THROW("class.define requires active chunk");

        auto protoRef = heap_.allocateObject();
        auto* proto = heap_.object(protoRef.id);

        proto->set("__name", Value::makeStringValId(args[0].asStringValId()));
        proto->set("__is_class", Value::makeBool(true));
        proto->set("__fields", args[1]);

        size_t arg_idx = 2;
        // Check for parent class
        if (args.size() > arg_idx && (args[arg_idx].isObjectId() || args[arg_idx].isStringValId() || args[arg_idx].isNull())) {
            if (args[arg_idx].isObjectId()) {
                proto->set("__parent", args[arg_idx]);
            } else if (args[arg_idx].isStringValId()) {
                const auto &parentName = current_chunk->getString(args[arg_idx].asStringValId());
                auto parentIt = globals.find(parentName);
                if (parentIt == globals.end() || !parentIt->second.isObjectId()) {
                    COMPILER_THROW("Unknown or invalid parent class: " + parentName);
                }
                proto->set("__parent", parentIt->second);
            }
            arg_idx++;
        }

 // Check for class fields (@@fields)
if (args.size() > arg_idx && args[arg_idx].isArrayId()) {
proto->set("__class_fields", args[arg_idx]);
}

proto->set("new", Value::makeHostFuncId(getHostFunctionIndex("class.new")));
proto->set("method", Value::makeHostFuncId(getHostFunctionIndex("class.method")));

return Value::makeObjectId(protoRef.id);
      });

registerHostFunction(
"class.new", [this](const std::vector<Value> &args) {
if (args.empty()) {
COMPILER_THROW("class.new(type, ...values) requires a type argument");
}
if (!current_chunk) COMPILER_THROW("class.new requires active chunk");

Value protoVal;
size_t ctor_offset = 1;
if (args[0].isObjectId()) {
protoVal = args[0];
ctor_offset = 1;
} else if (args[0].isStringValId()) {
const auto &name = current_chunk->getString(args[0].asStringValId());
auto it = globals.find(name);
if (it == globals.end()) {
COMPILER_THROW("Unknown class type: " + name);
}
protoVal = it->second;
ctor_offset = 1;
} else {
COMPILER_THROW("class.new: first argument must be class name or prototype object");
}

if (!protoVal.isObjectId()) {
COMPILER_THROW("Class type is not an object prototype");
}

auto instanceRef = heap_.allocateObject();
auto* instance = heap_.object(instanceRef.id);

instance->set("__class", protoVal);

auto* currentProto = heap_.object(protoVal.asObjectId());
while (currentProto) {
auto fieldsVal = currentProto->get("__fields");
if (fieldsVal && fieldsVal->isArrayId()) {
auto* fields = heap_.array(fieldsVal->asArrayId());
for (const auto& f : *fields) {
std::string fName = current_chunk->getString(f.asStringValId());
instance->set(fName, Value::makeNull());
}
}
auto parentVal = currentProto->get("__parent");
if (parentVal && parentVal->isObjectId()) {
currentProto = heap_.object(parentVal->asObjectId());
} else {
currentProto = nullptr;
}
}

Value initMethodVal = Value::makeNull();
currentProto = heap_.object(protoVal.asObjectId());
while (currentProto) {
auto val = currentProto->get("init");
if (val) {
initMethodVal = *val;
break;
}
auto parentVal = currentProto->get("__parent");
if (parentVal && parentVal->isObjectId()) {
currentProto = heap_.object(parentVal->asObjectId());
} else {
break;
}
}

if (!initMethodVal.isNull()) {
            std::vector<Value> ctor_args;
            ctor_args.reserve(args.size());
            ctor_args.push_back(Value::makeObjectId(instanceRef.id));
            for (size_t i = ctor_offset; i < args.size(); ++i) {
                ctor_args.push_back(args[i]);
            }
            (void)call(initMethodVal, ctor_args);
} else {
auto* proto = heap_.object(protoVal.asObjectId());
auto fieldsVal = proto->get("__fields");
if (fieldsVal && fieldsVal->isArrayId()) {
auto* fields = heap_.array(fieldsVal->asArrayId());
const size_t provided = args.size() - ctor_offset;
for (size_t i = 0; i < provided && i < fields->size(); ++i) {
std::string fieldName = current_chunk->getString((*fields)[i].asStringValId());
instance->set(fieldName, args[i + ctor_offset]);
}
}
}

return Value::makeObjectId(instanceRef.id);
});

  registerHostFunction(
      "class.method", [this](const std::vector<Value> &args) {
        if (args.size() != 3 || !args[1].isStringValId() ||
            (!args[2].isFunctionObjId() && !args[2].isClosureId() && !args[2].isHostFuncId())) {
          COMPILER_THROW("class.method expects (classType, methodNameString, callableObj)");
        }
        if (!current_chunk) COMPILER_THROW("class.method requires active chunk");
        
        auto* classObj = heap_.object(args[0].asObjectId());
        if (!classObj) return Value::makeNull();
        
        const std::string &method_name = current_chunk->getString(args[1].asStringValId());
        
        // Overloading/Arity dispatch logic
        uint32_t classObjId = args[0].asObjectId();
        std::string key = std::to_string(classObjId) + "." + method_name;
        auto &candidates = overloaded_methods_[key];
        
        // If candidates is empty but classObj already has an existing method of that name,
        // add the existing one to the candidates first
        if (candidates.empty()) {
          auto *existing = classObj->get(method_name);
          if (existing && (existing->isFunctionObjId() || existing->isClosureId() || existing->isHostFuncId())) {
            candidates.push_back(*existing);
          }
        }
        
        // Push the new candidate
        candidates.push_back(args[2]);
        
        if (candidates.size() > 1) {
          // Create dispatcher host function name
          std::string dispatcher_name = "_overload_" + std::to_string(classObjId) + "_" + method_name;
          
          // Register dynamic host function (updates if already exists)
          registerHostFunction(dispatcher_name, [this, key](const std::vector<Value> &dispatcher_args) -> Value {
            auto candIt = overloaded_methods_.find(key);
            if (candIt == overloaded_methods_.end()) return Value::makeNull();
            const auto &cands = candIt->second;
            
            Value best_cand = Value::makeNull();
            for (const auto &cand : cands) {
              uint32_t param_count = 0;
              if (cand.isFunctionObjId()) {
                uint32_t idx = cand.asFunctionObjId();
                if (current_chunk && idx < current_chunk->getFunctionCount()) {
                  if (auto *bf = current_chunk->getFunction(idx)) {
                    param_count = bf->param_count;
                  }
                }
              } else if (cand.isClosureId()) {
                if (auto *closure = heap_.closure(cand.asClosureId())) {
                  uint32_t idx = closure->function_index;
                  if (current_chunk && idx < current_chunk->getFunctionCount()) {
                    if (auto *bf = current_chunk->getFunction(idx)) {
                      param_count = bf->param_count;
                    }
                  }
                }
              }
              if (param_count == dispatcher_args.size()) {
                best_cand = cand;
                break;
              }
            }
            if (best_cand.isNull() && !cands.empty()) {
              best_cand = cands.back();
            }
            if (!best_cand.isNull()) {
              return this->callFunction(best_cand, dispatcher_args);
            }
            return Value::makeNull();
          });
          
          uint32_t dispatcher_idx = getHostFunctionIndex(dispatcher_name);
          Value dispatcher_val = Value::makeHostFuncId(dispatcher_idx);
          classObj->set(method_name, dispatcher_val);
          
          // Emulate older fallback for supercalls if invoked globally
          auto nameVal = classObj->get("__name");
          if (nameVal && nameVal->isStringValId()) {
            const std::string& className = current_chunk->getString(nameVal->asStringValId());
            setGlobal(className + "." + method_name, dispatcher_val);
          }
        } else {
          // Only one candidate so far - store it directly (no overhead of host function dispatcher)
          classObj->set(method_name, args[2]);
          
          // Emulate older fallback for supercalls if invoked globally
          auto nameVal = classObj->get("__name");
          if (nameVal && nameVal->isStringValId()) {
            const std::string& className = current_chunk->getString(nameVal->asStringValId());
            setGlobal(className + "." + method_name, args[2]);
          }
        }
        
        return Value::makeNull();
      });

  registerHostFunction(
      "inherits", [this](const std::vector<Value> &args) {
        if (args.size() != 2) {
          COMPILER_THROW("inherits(child, parent) expects two arguments");
        }
        if (args.size() != 2) return Value::makeBool(false);
        if (!args[0].isObjectId() || !args[1].isObjectId()) return Value::makeBool(false);
        auto* obj = heap_.object(args[0].asObjectId());
        auto target_id = args[1].asObjectId();
        
        while (obj) {
            auto parentVal = obj->get("__parent");
            if (!parentVal) parentVal = obj->get("__struct");
            if (!parentVal) parentVal = obj->get("__class");
            if (!parentVal || !parentVal->isObjectId()) break;
            if (parentVal->asObjectId() == target_id) return Value::makeBool(true);
            obj = heap_.object(parentVal->asObjectId());
        }
        return Value::makeBool(false);
      });

  // class.get(instance, field_name)
  registerHostFunction(
      "class.get", [this](const std::vector<Value> &args) {
        size_t offset = 0;
        if (args.size() >= 3 && args[0].isObjectId()) {
          offset = 1;
        }
        if (args.size() - offset < 2) COMPILER_THROW("class.get requires instance and field name");
        if (!args[offset].isObjectId()) COMPILER_THROW("class.get first arg must be object");
        if (!args[offset + 1].isStringValId()) COMPILER_THROW("class.get second arg must be string");
        
        auto* instance = heap_.object(args[offset].asObjectId());
        std::string fieldName = current_chunk->getString(args[offset + 1].asStringValId());
        
        // Walk prototype chain
        GCHeap::ObjectEntry* current = instance;
        while (current) {
          auto* val = current->get(fieldName);
          if (val) return *val;
          auto* parentVal = current->get("__parent");
          if (!parentVal) parentVal = current->get("__class");
          if (parentVal && parentVal->isObjectId()) {
            current = heap_.object(parentVal->asObjectId());
          } else {
            break;
          }
        }
        return Value::makeNull();
      });

  // class.set(instance, field_name, value)
  registerHostFunction(
      "class.set", [this](const std::vector<Value> &args) {
        size_t offset = 0;
        if (args.size() >= 4 && args[0].isObjectId()) {
          offset = 1;
        }
        if (args.size() - offset < 3) COMPILER_THROW("class.set requires instance, field name, and value");
        if (!args[offset].isObjectId()) COMPILER_THROW("class.set first arg must be object");
        if (!args[offset + 1].isStringValId()) COMPILER_THROW("class.set second arg must be string");
        
        auto* instance = heap_.object(args[offset].asObjectId());
        std::string fieldName = current_chunk->getString(args[offset + 1].asStringValId());
	instance->set(fieldName, args[offset + 2]);
	return Value::makeNull();
	});

registerHostFunction(
 "when.register", [this](const std::vector<Value> &args) {
 if (args.size() < 2) COMPILER_THROW("when.register requires condition_func_id and body_func_id");
 if (!args[0].isFunctionObjId() && !args[0].isClosureId())
 COMPILER_THROW("when.register first arg must be a function");
 if (!args[1].isFunctionObjId() && !args[1].isClosureId())
 COMPILER_THROW("when.register second arg must be a function");

 uint32_t cond_func_id = args[0].isFunctionObjId() ? args[0].asFunctionObjId() : 0;
 uint32_t body_func_id = args[1].isFunctionObjId() ? args[1].asFunctionObjId() : 0;

	if (!watcher_registry_) {
	return Value::makeNull();
	}

	auto tracker = std::make_shared<DependencyTracker>();
	DependencyTrackerScope scope(tracker);

	bool initial_result = evaluateConditionBytecode(cond_func_id, 0);

	auto deps = tracker->getGlobalDependencies();

	static std::atomic<uint32_t> next_when_fiber_id{20000};
	uint32_t fiber_id = next_when_fiber_id.fetch_add(1, std::memory_order_relaxed);
	auto fiber = std::make_unique<Fiber>(fiber_id, body_func_id, 0, "when_body");
	fiber->state = FiberState::SUSPENDED;

	Fiber* raw_fiber = fiber.get();

	if (scheduler_) {
	uint32_t goroutine_id = scheduler_->spawn(body_func_id, {}, 0, "when_body");
	scheduler_->attachFiber(goroutine_id, raw_fiber);
	}

	fiber.release();

 watcher_registry_->registerWatcher(
 cond_func_id, 0, initial_result, deps, raw_fiber);

 if (initial_result) {
 try {
 Value body_func = Value::makeFunctionObjId(body_func_id);
 call(body_func, {});
 } catch (...) {
 }
 }

	auto strRef = heap_.allocateString("watcher_registered");
	return Value::makeStringId(strRef.id);
	});
}

void VM::registerDefaultHostGlobals() {
  auto g_obj = heap_.allocateObject();
  globals_mirror_object_id_ = g_obj.id;
  setGlobal("_G", Value::makeObjectId(g_obj.id));

  for (const auto& [name, value] : host_function_globals_) {
    setHostObjectField(g_obj, name, value);
  }

  auto system_obj = heap_.allocateObject();
  setHostObjectField(system_obj, "gc", Value::makeHostFuncId(getHostFunctionIndex("system.gc")));
  setHostObjectField(system_obj, "gcStats", Value::makeHostFuncId(getHostFunctionIndex("system.gcStats")));
  auto sysIt = globals.find("system");
  if (sysIt != globals.end() && sysIt->second.isObjectId()) {
    ObjectRef existingRef{sysIt->second.asObjectId(), true};
    auto* existingObj = heap_.object(existingRef.id);
    if (existingObj) {
      setHostObjectField(existingRef, "gc", Value::makeHostFuncId(getHostFunctionIndex("system.gc")));
      setHostObjectField(existingRef, "gcStats", Value::makeHostFuncId(getHostFunctionIndex("system.gcStats")));
    } else {
      setGlobal("system", Value::makeObjectId(system_obj.id));
    }
  } else {
    setGlobal("system", Value::makeObjectId(system_obj.id));
  }

  auto struct_obj = heap_.allocateObject();
  setHostObjectField(struct_obj, "define", Value::makeHostFuncId(getHostFunctionIndex("struct.define")));
  setHostObjectField(struct_obj, "new", Value::makeHostFuncId(getHostFunctionIndex("struct.new")));
  setHostObjectField(struct_obj, "get", Value::makeHostFuncId(getHostFunctionIndex("struct.get")));
  setHostObjectField(struct_obj, "set", Value::makeHostFuncId(getHostFunctionIndex("struct.set")));
  setGlobal("struct", Value::makeObjectId(struct_obj.id));
  // Also register Struct (capital S) as alias for compatibility
  setGlobal("Struct", Value::makeObjectId(struct_obj.id));

  auto class_obj = heap_.allocateObject();
  setHostObjectField(class_obj, "define", Value::makeHostFuncId(getHostFunctionIndex("class.define")));
  setHostObjectField(class_obj, "new", Value::makeHostFuncId(getHostFunctionIndex("class.new")));
  setHostObjectField(class_obj, "method", Value::makeHostFuncId(getHostFunctionIndex("class.method")));
  setHostObjectField(class_obj, "get", Value::makeHostFuncId(getHostFunctionIndex("class.get")));
  setHostObjectField(class_obj, "set", Value::makeHostFuncId(getHostFunctionIndex("class.set")));
    setGlobal("class", Value::makeObjectId(class_obj.id));

    // app global object with args and restart
  // Merge with existing app object if AppModule already registered one
  {
    auto appIt = globals.find("app");
    if (appIt != globals.end() && appIt->second.isObjectId()) {
      ObjectRef existingRef{appIt->second.asObjectId(), true};
      auto* existingObj = heap_.object(existingRef.id);
      if (existingObj) {
        setHostObjectField(existingRef, "args", Value::makeArrayId(app_args_array_id_));
        setHostObjectField(existingRef, "restart", Value::makeHostFuncId(getHostFunctionIndex("app.restart")));
      } else {
        auto app_obj = heap_.allocateObject();
        setHostObjectField(app_obj, "args", Value::makeArrayId(app_args_array_id_));
        setHostObjectField(app_obj, "restart", Value::makeHostFuncId(getHostFunctionIndex("app.restart")));
        setGlobal("app", Value::makeObjectId(app_obj.id));
      }
    } else {
      auto app_obj = heap_.allocateObject();
      setHostObjectField(app_obj, "args", Value::makeArrayId(app_args_array_id_));
      setHostObjectField(app_obj, "restart", Value::makeHostFuncId(getHostFunctionIndex("app.restart")));
      setGlobal("app", Value::makeObjectId(app_obj.id));
    }
  }

    // FFI module object
    if (hasHostFunction("ffi.open")) {
        auto ffi_obj = heap_.allocateObject();
        setHostObjectField(ffi_obj, "open", Value::makeHostFuncId(getHostFunctionIndex("ffi.open")));
        setHostObjectField(ffi_obj, "close", Value::makeHostFuncId(getHostFunctionIndex("ffi.close")));
        setHostObjectField(ffi_obj, "sym", Value::makeHostFuncId(getHostFunctionIndex("ffi.sym")));
        setHostObjectField(ffi_obj, "call", Value::makeHostFuncId(getHostFunctionIndex("ffi.call")));
        setHostObjectField(ffi_obj, "cdef", Value::makeHostFuncId(getHostFunctionIndex("ffi.cdef")));
        setHostObjectField(ffi_obj, "alloc", Value::makeHostFuncId(getHostFunctionIndex("ffi.alloc")));
        setHostObjectField(ffi_obj, "allocBytes", Value::makeHostFuncId(getHostFunctionIndex("ffi.allocBytes")));
        setHostObjectField(ffi_obj, "free", Value::makeHostFuncId(getHostFunctionIndex("ffi.free")));
        setHostObjectField(ffi_obj, "sizeof", Value::makeHostFuncId(getHostFunctionIndex("ffi.sizeof")));
        setHostObjectField(ffi_obj, "alignof", Value::makeHostFuncId(getHostFunctionIndex("ffi.alignof")));
        setHostObjectField(ffi_obj, "string", Value::makeHostFuncId(getHostFunctionIndex("ffi.string")));
        setHostObjectField(ffi_obj, "cstring", Value::makeHostFuncId(getHostFunctionIndex("ffi.cstring")));
        setHostObjectField(ffi_obj, "array", Value::makeHostFuncId(getHostFunctionIndex("ffi.array")));
        setHostObjectField(ffi_obj, "cast", Value::makeHostFuncId(getHostFunctionIndex("ffi.cast")));
        setHostObjectField(ffi_obj, "newStruct", Value::makeHostFuncId(getHostFunctionIndex("ffi.newStruct")));
        setHostObjectField(ffi_obj, "field", Value::makeHostFuncId(getHostFunctionIndex("ffi.field")));
        setHostObjectField(ffi_obj, "setField", Value::makeHostFuncId(getHostFunctionIndex("ffi.setField")));
        setHostObjectField(ffi_obj, "callback", Value::makeHostFuncId(getHostFunctionIndex("ffi.callback")));
        setHostObjectField(ffi_obj, "closure", Value::makeHostFuncId(getHostFunctionIndex("ffi.closure")));
        setHostObjectField(ffi_obj, "memcpy", Value::makeHostFuncId(getHostFunctionIndex("ffi.memcpy")));
        setHostObjectField(ffi_obj, "memset", Value::makeHostFuncId(getHostFunctionIndex("ffi.memset")));
        setHostObjectField(ffi_obj, "var", Value::makeHostFuncId(getHostFunctionIndex("ffi.var")));
        setHostObjectField(ffi_obj, "get", Value::makeHostFuncId(getHostFunctionIndex("ffi.get")));
        setHostObjectField(ffi_obj, "set", Value::makeHostFuncId(getHostFunctionIndex("ffi.set")));
        setHostObjectField(ffi_obj, "get_i8", Value::makeHostFuncId(getHostFunctionIndex("ffi.get_i8")));
        setHostObjectField(ffi_obj, "set_i8", Value::makeHostFuncId(getHostFunctionIndex("ffi.set_i8")));
        setHostObjectField(ffi_obj, "get_i16", Value::makeHostFuncId(getHostFunctionIndex("ffi.get_i16")));
        setHostObjectField(ffi_obj, "set_i16", Value::makeHostFuncId(getHostFunctionIndex("ffi.set_i16")));
        setHostObjectField(ffi_obj, "get_i32", Value::makeHostFuncId(getHostFunctionIndex("ffi.get_i32")));
        setHostObjectField(ffi_obj, "set_i32", Value::makeHostFuncId(getHostFunctionIndex("ffi.set_i32")));
        setHostObjectField(ffi_obj, "get_i64", Value::makeHostFuncId(getHostFunctionIndex("ffi.get_i64")));
        setHostObjectField(ffi_obj, "set_i64", Value::makeHostFuncId(getHostFunctionIndex("ffi.set_i64")));
        setHostObjectField(ffi_obj, "get_u8", Value::makeHostFuncId(getHostFunctionIndex("ffi.get_u8")));
        setHostObjectField(ffi_obj, "set_u8", Value::makeHostFuncId(getHostFunctionIndex("ffi.set_u8")));
        setHostObjectField(ffi_obj, "get_u16", Value::makeHostFuncId(getHostFunctionIndex("ffi.get_u16")));
        setHostObjectField(ffi_obj, "set_u16", Value::makeHostFuncId(getHostFunctionIndex("ffi.set_u16")));
        setHostObjectField(ffi_obj, "get_u32", Value::makeHostFuncId(getHostFunctionIndex("ffi.get_u32")));
        setHostObjectField(ffi_obj, "set_u32", Value::makeHostFuncId(getHostFunctionIndex("ffi.set_u32")));
        setHostObjectField(ffi_obj, "get_u64", Value::makeHostFuncId(getHostFunctionIndex("ffi.get_u64")));
        setHostObjectField(ffi_obj, "set_u64", Value::makeHostFuncId(getHostFunctionIndex("ffi.set_u64")));
        setHostObjectField(ffi_obj, "get_f32", Value::makeHostFuncId(getHostFunctionIndex("ffi.get_f32")));
        setHostObjectField(ffi_obj, "set_f32", Value::makeHostFuncId(getHostFunctionIndex("ffi.set_f32")));
        setHostObjectField(ffi_obj, "get_f64", Value::makeHostFuncId(getHostFunctionIndex("ffi.get_f64")));
        setHostObjectField(ffi_obj, "set_f64", Value::makeHostFuncId(getHostFunctionIndex("ffi.set_f64")));
        setHostObjectField(ffi_obj, "get_ptr", Value::makeHostFuncId(getHostFunctionIndex("ffi.get_ptr")));
        setHostObjectField(ffi_obj, "set_ptr", Value::makeHostFuncId(getHostFunctionIndex("ffi.set_ptr")));
        setHostObjectField(ffi_obj, "ptr_add", Value::makeHostFuncId(getHostFunctionIndex("ffi.ptr_add")));
        setHostObjectField(ffi_obj, "ptr_sub", Value::makeHostFuncId(getHostFunctionIndex("ffi.ptr_sub")));
        setHostObjectField(ffi_obj, "ptr_to_uint", Value::makeHostFuncId(getHostFunctionIndex("ffi.ptr_to_uint")));
        setHostObjectField(ffi_obj, "lastError", Value::makeHostFuncId(getHostFunctionIndex("ffi.lastError")));
        setHostObjectField(ffi_obj, "clearError", Value::makeHostFuncId(getHostFunctionIndex("ffi.clearError")));
        setGlobal("ffi", Value::makeObjectId(ffi_obj.id));
    }

    // Note: shell, interval, config modules are registered via registerStdLibWithVM()
    // called during VM initialization - do NOT register them here as this function
    // is called on every execute() invocation

  // Register default window globals (these need to be reset per execution)
  setGlobal("title", Value::makeNull());
  setGlobal("exe", Value::makeNull());
  setGlobal("pid", Value::makeInt(0));

  if (system_object_initializer_) {
    system_object_initializer_(this);
    }

    registerDefaultPrototypes();
}

void VM::registerDefaultPrototypes() {
    if (prototypes_registered_) return;
    prototypes_registered_ = true;
    prototypes::registerStringPrototype(*this);
  prototypes::registerArrayPrototype(*this);
  prototypes::registerNumberPrototype(*this);
  prototypes::registerBoolPrototype(*this);
  prototypes::registerObjectPrototype(*this);
  prototypes::registerSetPrototype(*this);
  prototypes::registerRangePrototype(*this);

    registerPrototypeMethodByName("thread", "send", "thread.send");
    registerPrototypeMethodByName("thread", "join", "thread.join");
    registerPrototypeMethodByName("thread", "pause", "thread.pause");
    registerPrototypeMethodByName("thread", "resume", "thread.resume");
    registerPrototypeMethodByName("thread", "running", "thread.running");
registerPrototypeMethodByName("interval", "pause", "interval.pause");
registerPrototypeMethodByName("interval", "resume", "interval.resume");
registerPrototypeMethodByName("interval", "stop", "interval.stop");
registerPrototypeMethodByName("timeout", "cancel", "timeout.cancel");
registerPrototypeMethodByName("timeout", "stop", "timeout.stop");
}

Value VM::invokeHostFunction(const std::string &name,
 uint32_t arg_count) {
 auto it = host_functions.find(name);
 if (it == host_functions.end()) {
 COMPILER_THROW("Host function not found: " + name);
 }


  std::vector<Value> args(arg_count);
  for (uint32_t i = 0; i < arg_count; ++i) {
    if (stack.empty()) {
      COMPILER_THROW("Stack underflow while reading host arguments");
    }
      args[arg_count - 1 - i] = stack.top();
      stack.pop();
    }

  return it->second(args);
}

Value VM::invokeHostFunctionDirect(const std::string &name,
                                    const std::vector<Value> &args) {
  auto it = host_functions.find(name);
  if (it == host_functions.end()) {
    return Value::makeNull();
  }
  return it->second(args);
}

} // namespace havel::compiler
