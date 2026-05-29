#include "VM.hpp"
#include "VMInternals.hpp"
#include "../../../utils/Logger.hpp"
#include "../../utils/ErrorPrinter.hpp"
#include "../runtime/HostBridge.hpp"
#include "../runtime/RuntimeSupport.hpp"
#include "../../runtime/concurrency/DependencyTracker.hpp"
#include "../../runtime/concurrency/WatcherRegistry.hpp"
#include "../../runtime/concurrency/Fiber.hpp"
#include "../prototypes/PrototypeRegistry.hpp"
#include "core/config/ConfigManager.hpp"
#include <cmath>
#include <iostream>
#include <set>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <regex>

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

	default:
		return false;
	}
	return true;
}

} // namespace havel::compiler
