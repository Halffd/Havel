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

namespace havel::compiler {

// ============================================================================
// Extracted opcode handlers to reduce executeInstruction stack frame
// ============================================================================

void VM::execBinaryOp(const Instruction &instruction) {
  Value right = popStack();
  Value left = popStack();

  // Record type feedback for JIT specialization
  auto &frame = currentFrame();
  if (frame.ip < frame.function->type_feedback.size()) {
    auto &fb = frame.function->type_feedback[frame.ip];
    fb.execution_count++;
    fb.left_type_mask |= getFeedbackMask(left);
    fb.right_type_mask |= getFeedbackMask(right);

    
    if (tiering_enabled_ && frame.function && jit_compiler_) {
      const std::string fn_name = frame.function->name;
      if (fb.execution_count >= tier1_threshold_ && !tier1_compiled_.count(fn_name)) {
        tier1_compiled_.insert(fn_name);
        tier1_transition_count_.fetch_add(1);
        ::havel::debug("[tiering] {} -> tier1", fn_name);
        jit_compiler_->compileFunctionTier(*frame.function, 1);
      }
      if (fb.execution_count >= tier2_threshold_ && !tier2_compiled_.count(fn_name)) {
        tier2_compiled_.insert(fn_name);
        {
          std::lock_guard<std::mutex> lk(tier2_queue_mutex_);
          if (tier2_queued_or_compiling_.insert(fn_name).second) {
            tier2_queue_.push(*frame.function);
            tier2_enqueue_count_.fetch_add(1);
            ::havel::debug("[tiering] {} queued for tier2", fn_name);
          } else {
            tier2_skip_duplicate_count_.fetch_add(1);
          }
        }
        if (!tier2_worker_running_.exchange(true)) {
          tier2_worker_ = std::thread([this]() {
            auto hasQueuedWork = [this]() {
              std::lock_guard<std::mutex> lk(tier2_queue_mutex_);
              return !tier2_queue_.empty();
            };
            while (tier2_worker_running_.load() || hasQueuedWork()) {
              std::optional<BytecodeFunction> fn;
              {
                std::lock_guard<std::mutex> lk(tier2_queue_mutex_);
                if (!tier2_queue_.empty()) {
                  fn = tier2_queue_.front();
                  tier2_queue_.pop();
                }
              }
              if (fn.has_value() && jit_compiler_) {
                jit_compiler_->compileFunctionTier(*fn, 2);
                tier2_compile_count_.fetch_add(1);
                ::havel::debug("[tiering] {} -> tier2", fn->name);
                std::lock_guard<std::mutex> lk(tier2_queue_mutex_);
                tier2_queued_or_compiling_.erase(fn->name);
              } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
              }
            }
          });
        }
      }
    }
    if (fb.execution_count == 1000 && hot_func_cb_) {
      hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
    }
  }

  // Handle null comparisons explicitly
  if (isNull(left) || isNull(right)) {
    bool result = false;
    switch (instruction.opcode) {
    case OpCode::EQ:
      result = isNull(left) && isNull(right);
      break;
    case OpCode::NEQ:
      result = !(isNull(left) && isNull(right));
      break;
    case OpCode::IS:
      result = isNull(left) && isNull(right);
      break;
    case OpCode::LT:
    case OpCode::LTE:
    case OpCode::GT:
    case OpCode::GTE:
      result = false;
      break;
case OpCode::ADD:
    case OpCode::SUB:
    case OpCode::MUL:
    case OpCode::DIV:
    case OpCode::MOD:
    case OpCode::INT_DIV:
    case OpCode::DIVMOD:
    case OpCode::REMAINDER:
    case OpCode::POW:
    case OpCode::BIT_AND:
    case OpCode::BIT_OR:
    case OpCode::BIT_XOR:
    case OpCode::BIT_LSH:
    case OpCode::BIT_RSH:
      pushStack(Value::makeNull());
      return;
    default:
      COMPILER_THROW("Invalid operation opcode with null");
    }
    pushStack(result);
    return;
  }

  if (instruction.opcode == OpCode::EQ || instruction.opcode == OpCode::NEQ) {
    const bool equal = valuesEqualDeep(left, right);
    pushStack(instruction.opcode == OpCode::EQ ? equal : !equal);
    return;
  }

  // Identity comparison - check if same object reference
  if (instruction.opcode == OpCode::IS) {
    bool identical = false;
    if (left.isInt() && right.isInt()) {
      identical = left.asInt() == right.asInt();
    } else if (left.isDouble() && right.isDouble()) {
      identical = left.asDouble() == right.asDouble();
    } else if (left.isBool() && right.isBool()) {
      identical = left.asBool() == right.asBool();
    } else if (left.isNull() && right.isNull()) {
      identical = true;
    } else if (left.isStringValId() && right.isStringValId()) {
      identical = left.asStringValId() == right.asStringValId();
    } else if (left.isStringId() && right.isStringId()) {
      identical = left.asStringId() == right.asStringId();
    } else if (left.isArrayId() && right.isArrayId()) {
      identical = left.asArrayId() == right.asArrayId();
    } else if (left.isObjectId() && right.isObjectId()) {
      identical = left.asObjectId() == right.asObjectId();
    } else if (left.isRangeId() && right.isRangeId()) {
      identical = left.asRangeId() == right.asRangeId();
    } else if (left.isClosureId() && right.isClosureId()) {
      identical = left.asClosureId() == right.asClosureId();
    } else if (left.isFunctionObjId() && right.isFunctionObjId()) {
      identical = left.asFunctionObjId() == right.asFunctionObjId();
    } else if (left.isHostFuncId() && right.isHostFuncId()) {
        identical = left.asHostFuncId() == right.asHostFuncId();
    } else if (left.isEnumId() && right.isEnumId()) {
        identical = left.asEnumId() == right.asEnumId();
    }
    pushStack(Value::makeBool(identical));
    return;
  }

    if (left.isInt() && right.isInt()) {
        int64_t l = left.asInt();
        int64_t r = right.asInt();
        switch (instruction.opcode) {
        case OpCode::ADD: pushStack(l + r); break;
        case OpCode::SUB: pushStack(l - r); break;
        case OpCode::MUL: pushStack(l * r); break;
            case OpCode::DIV:
                if (r == 0) COMPILER_THROW("Division by zero");
                pushStack(static_cast<double>(l) / static_cast<double>(r));
                break;
            case OpCode::INT_DIV:
                if (r == 0) COMPILER_THROW("Division by zero");
                pushStack(l / r);
                break;
            case OpCode::DIVMOD:
                if (r == 0) COMPILER_THROW("Division by zero");
      {
        int64_t rem = l % r;
        if (rem != 0 && ((rem < 0) != (r < 0))) rem += r;
        int64_t quot = (l - rem) / r;
        auto arrRef = heap_.allocateArray();
        auto *arr = heap_.array(arrRef.id);
        arr->push_back(Value(quot));
        arr->push_back(Value(rem));
        pushStack(Value::makeArrayId(arrRef.id));
      }
      break;
            case OpCode::REMAINDER:
                if (r == 0) COMPILER_THROW("Division by zero");
                pushStack(l % r); // C-style: sign follows dividend
                break;
            case OpCode::MOD:
                if (r == 0) COMPILER_THROW("Modulo by zero");
      {
        int64_t result = l % r;
        if (result != 0 && ((result < 0) != (r < 0))) result += r;
        pushStack(result); // Python-style: sign follows divisor
      }
      break;
        case OpCode::POW:
          pushStack(static_cast<int64_t>(
              std::pow(static_cast<double>(l), static_cast<double>(r))));
          break;
        case OpCode::EQ: pushStack(l == r); break;
        case OpCode::NEQ: pushStack(l != r); break;
        case OpCode::LT: pushStack(l < r); break;
case OpCode::LTE: pushStack(l <= r); break;
        case OpCode::GT: pushStack(l > r); break;
        case OpCode::GTE: pushStack(l >= r); break;
        case OpCode::BIT_AND: pushStack(l & r); break;
        case OpCode::BIT_OR: pushStack(l | r); break;
        case OpCode::BIT_XOR: pushStack(l ^ r); break;
        case OpCode::BIT_LSH: pushStack(l << r); break;
        case OpCode::BIT_RSH: pushStack(l >> r); break;
        default: COMPILER_THROW("Unsupported integer operation");
    }
    return;
  }

  if ((left.isInt() || left.isDouble()) &&
      (right.isInt() || right.isDouble())) {
    double l = left.isInt() ? static_cast<double>(left.asInt()) : left.asDouble();
    double r = right.isInt() ? static_cast<double>(right.asInt()) : right.asDouble();
    switch (instruction.opcode) {
    case OpCode::ADD:  pushStack(l + r); break;
    case OpCode::SUB:  pushStack(l - r); break;
    case OpCode::MUL:  pushStack(l * r); break;
            case OpCode::DIV:
                if (r == 0.0) COMPILER_THROW("Division by zero");
                pushStack(l / r);
                break;
            case OpCode::INT_DIV:
                if (r == 0.0) COMPILER_THROW("Division by zero");
                pushStack(static_cast<int64_t>(l) / static_cast<int64_t>(r));
                break;
            case OpCode::DIVMOD:
                if (r == 0.0) COMPILER_THROW("Division by zero");
      {
        int64_t il = static_cast<int64_t>(l);
        int64_t ir = static_cast<int64_t>(r);
        int64_t rem = il % ir;
        if (rem != 0 && ((rem < 0) != (ir < 0))) rem += ir;
        int64_t quot = (il - rem) / ir;
        auto arrRef = heap_.allocateArray();
        auto *arr = heap_.array(arrRef.id);
        arr->push_back(Value(quot));
        arr->push_back(Value(rem));
        pushStack(Value::makeArrayId(arrRef.id));
      }
      break;
            case OpCode::REMAINDER:
                if (r == 0.0) COMPILER_THROW("Division by zero");
      pushStack(static_cast<int64_t>(l) % static_cast<int64_t>(r)); // C-style
      break;
    case OpCode::MOD:
      if (r == 0.0) COMPILER_THROW("Modulo by zero");
      {
        double m = std::fmod(l, r);
        if (m != 0.0 && ((m < 0.0) != (r < 0.0))) m += r;
        pushStack(m); // Python-style: sign follows divisor
      }
      break;
    case OpCode::POW:  pushStack(std::pow(l, r)); break;
    case OpCode::EQ:   pushStack(l == r); break;
    case OpCode::NEQ:  pushStack(l != r); break;
    case OpCode::LT:   pushStack(l < r); break;
    case OpCode::LTE:  pushStack(l <= r); break;
    case OpCode::GT:   pushStack(l > r); break;
        case OpCode::GTE: pushStack(l >= r); break;
        case OpCode::BIT_AND: pushStack(static_cast<int64_t>(l) & static_cast<int64_t>(r)); break;
        case OpCode::BIT_OR: pushStack(static_cast<int64_t>(l) | static_cast<int64_t>(r)); break;
        case OpCode::BIT_XOR: pushStack(static_cast<int64_t>(l) ^ static_cast<int64_t>(r)); break;
        case OpCode::BIT_LSH: pushStack(static_cast<int64_t>(l) << static_cast<int64_t>(r)); break;
        case OpCode::BIT_RSH: pushStack(static_cast<int64_t>(l) >> static_cast<int64_t>(r)); break;
        default: COMPILER_THROW("Unsupported floating point operation");
    }
    return;
  }

  // Handle string operations (StringValId = compile-time constant, StringId = runtime string)
  if (left.isStringValId() || left.isStringId() || right.isStringValId() || right.isStringId()) {
    // Resolve left operand to actual string
    std::string l;
    if (left.isStringValId()) {
      // Try to resolve from current chunk's string table
      if (current_chunk) {
        l = current_chunk->getString(left.asStringValId());
      } else {
        l = "<string:" + std::to_string(left.asStringValId()) + ">";
      }
    } else if (left.isStringId()) {
      if (auto *s = heap_.string(left.asStringId())) {
        l = *s;
      } else {
        l = "<string:" + std::to_string(left.asStringId()) + ">";
      }
    } else {
      l = toString(left);
    }

    // Resolve right operand to actual string
    std::string r;
    if (right.isStringValId()) {
      if (current_chunk) {
        r = current_chunk->getString(right.asStringValId());
      } else {
        r = "<string:" + std::to_string(right.asStringValId()) + ">";
      }
    } else if (right.isStringId()) {
      if (auto *s = heap_.string(right.asStringId())) {
        r = *s;
      } else {
        r = "<string:" + std::to_string(right.asStringId()) + ">";
      }
    } else {
      r = toString(right);
    }

    switch (instruction.opcode) {
    case OpCode::ADD: {
      std::string result = l + r;
      auto strRef = heap_.allocateString(std::move(result));
      pushStack(Value::makeStringId(strRef.id));
      break;
    }
    case OpCode::MUL: {
      // string * int → repeat
      if (right.isInt()) {
        int count = static_cast<int>(right.asInt());
        if (count < 0) count = 0;
        std::string result;
        for (int i = 0; i < count; i++) result += l;
        auto strRef = heap_.allocateString(std::move(result));
        pushStack(Value::makeStringId(strRef.id));
      } else {
        COMPILER_THROW("Invalid string operation");
        }
        break;
      }
      case OpCode::EQ: pushStack(l == r); break;
      case OpCode::NEQ: pushStack(l != r); break;
      case OpCode::LT: pushStack(l < r); break;
      case OpCode::LTE: pushStack(l <= r); break;
      case OpCode::GT: pushStack(l > r); break;
      case OpCode::GTE: pushStack(l >= r); break;
      default: COMPILER_THROW("Invalid string operation");
    }
    return;
  }

  // Handle array operations
  if (left.isArrayId() || right.isArrayId()) {
    switch (instruction.opcode) {
    case OpCode::ADD: {
      if (left.isArrayId() && right.isArrayId()) {
        auto *larr = heap_.array(left.asArrayId());
        auto *rarr = heap_.array(right.asArrayId());
        auto arrRef = heap_.allocateArray();
        auto *arr = heap_.array(arrRef.id);
        if (larr) arr->insert(arr->end(), larr->begin(), larr->end());
        if (rarr) arr->insert(arr->end(), rarr->begin(), rarr->end());
        pushStack(Value::makeArrayId(arrRef.id));
      } else if (left.isArrayId() && right.isInt()) {
        // array + int → element-wise add
        auto *larr = heap_.array(left.asArrayId());
        auto arrRef = heap_.allocateArray();
        auto *arr = heap_.array(arrRef.id);
        int64_t delta = right.asInt();
        if (larr) {
          for (auto& v : *larr) {
            if (v.isInt()) arr->push_back(Value::makeInt(v.asInt() + delta));
            else arr->push_back(v);
          }
        }
        pushStack(Value::makeArrayId(arrRef.id));
      } else if (left.isInt() && right.isArrayId()) {
        // int + array → element-wise add
        auto *rarr = heap_.array(right.asArrayId());
        auto arrRef = heap_.allocateArray();
        auto *arr = heap_.array(arrRef.id);
        int64_t delta = left.asInt();
        if (rarr) {
          for (auto& v : *rarr) {
            if (v.isInt()) arr->push_back(Value::makeInt(delta + v.asInt()));
            else arr->push_back(v);
          }
        }
        pushStack(Value::makeArrayId(arrRef.id));
      } else {
        COMPILER_THROW("Type mismatch in ADD for arrays");
      }
      break;
    }
    case OpCode::SUB: {
      if (left.isArrayId() && right.isArrayId()) {
        // array - array → remove elements present in right
        auto *larr = heap_.array(left.asArrayId());
        auto *rarr = heap_.array(right.asArrayId());
        auto arrRef = heap_.allocateArray();
        auto *arr = heap_.array(arrRef.id);
        if (larr) {
          for (auto& v : *larr) {
            bool found = false;
            if (rarr) {
              for (auto& rv : *rarr) {
                if (valuesEqualDeep(v, rv)) { found = true; break; }
              }
            }
            if (!found) arr->push_back(v);
          }
        }
        pushStack(Value::makeArrayId(arrRef.id));
      } else if (left.isArrayId() && right.isInt()) {
        // array - int → remove elements equal to value
        auto *larr = heap_.array(left.asArrayId());
        auto arrRef = heap_.allocateArray();
        auto *arr = heap_.array(arrRef.id);
        if (larr) {
          for (auto& v : *larr) {
            if (!(v.isInt() && v.asInt() == right.asInt())) arr->push_back(v);
          }
        }
        pushStack(Value::makeArrayId(arrRef.id));
      } else {
        COMPILER_THROW("Type mismatch in SUB for arrays");
      }
      break;
    }
    case OpCode::MUL: {
      if (left.isArrayId() && right.isInt()) {
        // array * int → repeat
        auto *larr = heap_.array(left.asArrayId());
        auto arrRef = heap_.allocateArray();
        auto *arr = heap_.array(arrRef.id);
        int count = static_cast<int>(right.asInt());
        if (count < 0) count = 0;
        if (larr) {
          for (int i = 0; i < count; i++)
            arr->insert(arr->end(), larr->begin(), larr->end());
        }
        pushStack(Value::makeArrayId(arrRef.id));
      } else if (left.isInt() && right.isArrayId()) {
        // int * array → repeat
        auto *rarr = heap_.array(right.asArrayId());
        auto arrRef = heap_.allocateArray();
        auto *arr = heap_.array(arrRef.id);
        int count = static_cast<int>(left.asInt());
        if (count < 0) count = 0;
        if (rarr) {
          for (int i = 0; i < count; i++)
            arr->insert(arr->end(), rarr->begin(), rarr->end());
        }
        pushStack(Value::makeArrayId(arrRef.id));
      } else {
        COMPILER_THROW("Type mismatch in MUL for arrays");
      }
      break;
    }
    case OpCode::DIV: {
      if (left.isArrayId() && right.isInt()) {
        // array / int → chunk into groups of N
        auto *larr = heap_.array(left.asArrayId());
        auto arrRef = heap_.allocateArray();
        auto *arr = heap_.array(arrRef.id);
        int chunkSize = static_cast<int>(right.asInt());
        if (chunkSize <= 0) chunkSize = 1;
        if (larr) {
          auto chunkRef = heap_.allocateArray();
          auto *chunk = heap_.array(chunkRef.id);
          for (size_t i = 0; i < larr->size(); i++) {
            chunk->push_back((*larr)[i]);
            if (static_cast<int>(chunk->size()) >= chunkSize) {
              arr->push_back(Value::makeArrayId(chunkRef.id));
              chunkRef = heap_.allocateArray();
              chunk = heap_.array(chunkRef.id);
            }
          }
          if (!chunk->empty()) arr->push_back(Value::makeArrayId(chunkRef.id));
        }
        pushStack(Value::makeArrayId(arrRef.id));
      } else {
        COMPILER_THROW("Type mismatch in DIV for arrays");
      }
      break;
    }
    case OpCode::MOD: {
      if (left.isArrayId() && right.isInt()) {
        // array % int → split into N roughly equal parts
        auto *larr = heap_.array(left.asArrayId());
        auto arrRef = heap_.allocateArray();
        auto *arr = heap_.array(arrRef.id);
        int parts = static_cast<int>(right.asInt());
        if (parts <= 0) parts = 1;
        if (larr) {
          size_t total = larr->size();
          size_t partSize = total / parts;
          size_t remainder = total % parts;
          size_t idx = 0;
          for (int p = 0; p < parts; p++) {
            auto chunkRef = heap_.allocateArray();
            auto *chunk = heap_.array(chunkRef.id);
            size_t sz = partSize + (p < static_cast<int>(remainder) ? 1 : 0);
            for (size_t j = 0; j < sz && idx < total; j++, idx++) {
              chunk->push_back((*larr)[idx]);
            }
            arr->push_back(Value::makeArrayId(chunkRef.id));
          }
        }
        pushStack(Value::makeArrayId(arrRef.id));
      } else {
        COMPILER_THROW("Type mismatch in MOD for arrays");
      }
      break;
    }
    case OpCode::EQ:
    case OpCode::NEQ: {
      bool eq = valuesEqualDeep(left, right);
      pushStack(instruction.opcode == OpCode::EQ ? eq : !eq);
      break;
    }
    default: COMPILER_THROW("Invalid array operation");
    }
    return;
  }

  // Handle set operations
  if (left.isSetId() || right.isSetId()) {
    switch (instruction.opcode) {
    case OpCode::ADD: {
      // set + set → union
      if (left.isSetId() && right.isSetId()) {
        auto *lset = heap_.set(left.asSetId());
        auto *rset = heap_.set(right.asSetId());
        auto setRef = heap_.allocateSet();
        auto *set = heap_.set(setRef.id);
        if (lset) set->insert(lset->begin(), lset->end());
        if (rset) set->insert(rset->begin(), rset->end());
        pushStack(Value::makeSetId(setRef.id));
      } else {
        COMPILER_THROW("Type mismatch in ADD for sets");
      }
      break;
    }
    case OpCode::SUB: {
      // set - set → difference
      if (left.isSetId() && right.isSetId()) {
        auto *lset = heap_.set(left.asSetId());
        auto *rset = heap_.set(right.asSetId());
        auto setRef = heap_.allocateSet();
        auto *set = heap_.set(setRef.id);
        if (lset) set->insert(lset->begin(), lset->end());
        if (rset) {
          for (const auto& [k, v] : *rset) set->erase(k);
        }
        pushStack(Value::makeSetId(setRef.id));
      } else {
        COMPILER_THROW("Type mismatch in SUB for sets");
      }
      break;
    }
    case OpCode::EQ:
    case OpCode::NEQ: {
      bool eq = valuesEqualDeep(left, right);
      pushStack(instruction.opcode == OpCode::EQ ? eq : !eq);
      break;
    }
    default: COMPILER_THROW("Invalid set operation");
    }
    return;
  }

	// Operator overloading: if left is an object, check for op_* methods
	if (left.isObjectId()) {
		const char *opMethodName = nullptr;
		switch (instruction.opcode) {
		case OpCode::ADD: opMethodName = "op_add"; break;
		case OpCode::SUB: opMethodName = "op_sub"; break;
		case OpCode::MUL: opMethodName = "op_mul"; break;
    case OpCode::DIV: opMethodName = "op_div"; break;
    case OpCode::INT_DIV: opMethodName = "op_int_div"; break;
      case OpCode::DIVMOD: opMethodName = "op_divmod"; break;
      case OpCode::REMAINDER: opMethodName = "op_remainder"; break;
		case OpCode::MOD: opMethodName = "op_mod"; break;
		case OpCode::POW: opMethodName = "op_pow"; break;
		case OpCode::EQ: opMethodName = "op_eq"; break;
		case OpCode::NEQ: opMethodName = "op_ne"; break;
		case OpCode::LT: opMethodName = "op_lt"; break;
		case OpCode::GT: opMethodName = "op_gt"; break;
		case OpCode::LTE: opMethodName = "op_le"; break;
		case OpCode::GTE: opMethodName = "op_ge"; break;
		case OpCode::BIT_AND: opMethodName = "op_bit_and"; break;
		case OpCode::BIT_OR: opMethodName = "op_bit_or"; break;
		case OpCode::BIT_XOR: opMethodName = "op_bit_xor"; break;
		case OpCode::BIT_NOT: opMethodName = "op_bit_not"; break;
		case OpCode::BIT_LSH: opMethodName = "op_shift_left"; break;
		case OpCode::BIT_RSH: opMethodName = "op_shift_right"; break;
		default: break;
		}

		if (opMethodName) {
			Value opMethod = getHostObjectField(ObjectRef{left.asObjectId(), true}, opMethodName);
			if (!opMethod.isNull() && (opMethod.isFunctionObjId() || opMethod.isClosureId() || opMethod.isHostFuncId())) {
				pushStack(callFunction(opMethod, {left, right}));
				return;
			}
		}
	}

	// Handle object operations
	if (left.isObjectId() || right.isObjectId()) {
		switch (instruction.opcode) {
		case OpCode::ADD: {
      // object + object → merge (right overwrites left)
      if (left.isObjectId() && right.isObjectId()) {
        auto *lobj = heap_.object(left.asObjectId());
        auto *robj = heap_.object(right.asObjectId());
        auto objRef = heap_.allocateObject();
        auto *obj = heap_.object(objRef.id);
        if (lobj) { for (const auto& [k, v] : *lobj) obj->set(k, v); }
        if (robj) { for (const auto& [k, v] : *robj) obj->set(k, v); }
        pushStack(Value::makeObjectId(objRef.id));
      } else {
        COMPILER_THROW("Type mismatch in ADD for objects");
      }
      break;
    }
    case OpCode::SUB: {
      // object - object → remove keys present in right
      if (left.isObjectId() && right.isObjectId()) {
        auto *lobj = heap_.object(left.asObjectId());
        auto *robj = heap_.object(right.asObjectId());
        auto objRef = heap_.allocateObject();
        auto *obj = heap_.object(objRef.id);
        if (lobj) {
          for (const auto& [k, v] : *lobj) {
            if (!robj || robj->find(k) == robj->end()) {
              obj->set(k, v);
            }
          }
        }
        pushStack(Value::makeObjectId(objRef.id));
      } else {
        COMPILER_THROW("Type mismatch in SUB for objects");
      }
      break;
    }
    case OpCode::MUL: {
      // object * int → multiply all numeric values
      if (left.isObjectId() && right.isInt()) {
        auto *lobj = heap_.object(left.asObjectId());
        auto objRef = heap_.allocateObject();
        auto *obj = heap_.object(objRef.id);
        int64_t mult = right.asInt();
        if (lobj) {
          for (const auto& [k, v] : *lobj) {
            if (v.isInt()) obj->set(k, Value::makeInt(v.asInt() * mult));
            else if (v.isDouble()) obj->set(k, Value::makeDouble(v.asDouble() * mult));
            else obj->set(k, v);
          }
        }
        pushStack(Value::makeObjectId(objRef.id));
      } else {
        COMPILER_THROW("Type mismatch in MUL for objects");
      }
      break;
    }
    case OpCode::DIV: {
      // object / int → divide all numeric values
      if (left.isObjectId() && right.isInt()) {
        auto *lobj = heap_.object(left.asObjectId());
        auto objRef = heap_.allocateObject();
        auto *obj = heap_.object(objRef.id);
        double div = static_cast<double>(right.asInt());
        if (div == 0) COMPILER_THROW("Division by zero");
        if (lobj) {
          for (const auto& [k, v] : *lobj) {
            if (v.isInt()) obj->set(k, Value::makeInt(static_cast<int64_t>(v.asInt() / div)));
            else if (v.isDouble()) obj->set(k, Value::makeDouble(v.asDouble() / div));
            else obj->set(k, v);
          }
        }
        pushStack(Value::makeObjectId(objRef.id));
      } else {
        COMPILER_THROW("Type mismatch in DIV for objects");
      }
      break;
    }
    case OpCode::MOD: {
      // object % int → mod all numeric values
      if (left.isObjectId() && right.isInt()) {
        auto *lobj = heap_.object(left.asObjectId());
        auto objRef = heap_.allocateObject();
        auto *obj = heap_.object(objRef.id);
        int64_t mod = right.asInt();
        if (mod == 0) COMPILER_THROW("Modulo by zero");
        if (lobj) {
          for (const auto& [k, v] : *lobj) {
            if (v.isInt()) obj->set(k, Value::makeInt(v.asInt() % mod));
            else obj->set(k, v);
          }
        }
        pushStack(Value::makeObjectId(objRef.id));
      } else {
        COMPILER_THROW("Type mismatch in MOD for objects");
      }
      break;
    }
    case OpCode::EQ:
    case OpCode::NEQ: {
      bool eq = valuesEqualDeep(left, right);
      pushStack(instruction.opcode == OpCode::EQ ? eq : !eq);
      break;
    }
    default: COMPILER_THROW("Invalid object operation");
    }
    return;
  }

  COMPILER_THROW("Type mismatch in binary operation");
}

void VM::execLogicalOp(OpCode opcode) {
  Value right = popStack();
  Value left = popStack();

  // Record feedback
  auto &frame = currentFrame();
  if (frame.ip < frame.function->type_feedback.size()) {
    auto &fb = frame.function->type_feedback[frame.ip];
    fb.execution_count++;
    fb.left_type_mask |= getFeedbackMask(left);
    fb.right_type_mask |= getFeedbackMask(right);
  }
  switch (opcode) {
  case OpCode::AND: pushStack(isTruthy(left) && isTruthy(right)); break;
  case OpCode::OR:  pushStack(isTruthy(left) || isTruthy(right)); break;
  default: COMPILER_THROW("Unknown logical opcode");
  }
}

void VM::execNegate() {
        Value value = popStack();

	// Record feedback
	auto &frame = currentFrame();
	if (frame.ip < frame.function->type_feedback.size()) {
		auto &fb = frame.function->type_feedback[frame.ip];
		fb.execution_count++;
		fb.left_type_mask |= getFeedbackMask(value);
	}
	if (value.isInt()) {
		pushStack(-value.asInt());
	} else if (value.isDouble()) {
		pushStack(-value.asDouble());
	} else if (value.isObjectId()) {
		Value opMethod = getHostObjectField(ObjectRef{value.asObjectId(), true}, "op_negate");
		if (!opMethod.isNull() && (opMethod.isFunctionObjId() || opMethod.isClosureId() || opMethod.isHostFuncId())) {
			pushStack(callFunction(opMethod, {value}));
		} else {
			COMPILER_THROW("Cannot negate non-numeric value (no op_negate method)");
		}
	} else {
		COMPILER_THROW("Cannot negate non-numeric value");
	}
}

void VM::execJump(const Instruction &instruction) {
 uint32_t target = instruction.operands[0].asInt();
 currentFrame().ip = target;
}

void VM::execJumpIfFalse(const Instruction &instruction) {
  uint32_t target = instruction.operands[0].asInt();
  Value condition = popStack();
  if (!isTruthy(condition)) {
    currentFrame().ip = target;
  }
}

void VM::execJumpIfTrue(const Instruction &instruction) {
  uint32_t target = instruction.operands[0].asInt();
  Value condition = popStack();
  if (isTruthy(condition)) {
    currentFrame().ip = target;
  }
}

// ============================================================================
// Main executeInstruction dispatcher
// ============================================================================

void VM::executeInstruction(const Instruction &instruction) {
    // Sync current_chunk with the current frame's chunk so string resolution
    // is always correct for the executing function (fixes cross-chunk bugs
    // when closures from one chunk call host functions that modify current_chunk).
    if (frame_count_ > 0 && frame_arena_[frame_count_ - 1].chunk) {
        current_chunk = frame_arena_[frame_count_ - 1].chunk;
    }
    switch (instruction.opcode) {
  case OpCode::LOAD_CONST: {
    uint32_t const_index = instruction.operands[0].asInt();
    pushStack(getConstant(const_index));
    break;
  }

case OpCode::LOAD_GLOBAL: {
            if (instruction.operands.empty() ||
                !instruction.operands[0].isStringValId()) {
                COMPILER_THROW("LOAD_GLOBAL expects string operand");
            }
            // Get the string from the function's own chunk's string table
            uint32_t strIndex = instruction.operands[0].asStringValId();
            const auto& cf = currentFrame();
            const auto* func = cf.function;
            const BytecodeChunk* resolveChunk = cf.chunk ? cf.chunk : current_chunk;
            std::string name;
            if (resolveChunk) {
                name = resolveChunk->getString(strIndex);
            } else {
                name = "<unknown:" + std::to_string(strIndex) + ">";
            }

    // First check regular globals (user variables shadow host functions)
            auto it = globals.find(name);
        if (it != globals.end()) {
            trackGlobalAccess(name);
            pushStack(it->second);
            break;
        }

	// Then check host function globals (fallback for built-in functions)
	auto hostIt = host_function_globals_.find(name);
	if (hostIt != host_function_globals_.end()) {
		trackGlobalAccess(name);
		pushStack(hostIt->second);
		break;
	}

    COMPILER_THROW("Undefined variable: '" + name + "'");
    break;
  }

        case OpCode::STORE_GLOBAL: {
            if (instruction.operands.empty() ||
                !instruction.operands[0].isStringValId()) {
                COMPILER_THROW("STORE_GLOBAL expects string operand");
            }
            uint32_t strIndex = instruction.operands[0].asStringValId();
            const auto& cf_store = currentFrame();
            const BytecodeChunk* resolveChunkStore = cf_store.chunk ? cf_store.chunk : current_chunk;
            std::string name;
            if (resolveChunkStore) {
                name = resolveChunkStore->getString(strIndex);
            } else {
                name = "<unknown:" + std::to_string(strIndex) + ">";
            }
            Value value = popStack();

    if (immutable_globals_.count(name)) {
            auto existing = globals.find(name);
            if (existing != globals.end() && existing->second == value) {
                break;
            }
            COMPILER_THROW("Cannot reassign val global: " + name);
        }
        globals[name] = value;
        emitVariableChanged(name);
        break;
    }

        case OpCode::STORE_IMMUT_GLOBAL: {
            if (instruction.operands.empty() ||
                !instruction.operands[0].isStringValId()) {
                COMPILER_THROW("STORE_IMMUT_GLOBAL expects string operand");
            }
            uint32_t strIndex = instruction.operands[0].asStringValId();
            const auto& cf_imut = currentFrame();
            const BytecodeChunk* resolveChunkImut = cf_imut.chunk ? cf_imut.chunk : current_chunk;
            std::string name;
            if (resolveChunkImut) {
                name = resolveChunkImut->getString(strIndex);
            } else {
                name = "<unknown:" + std::to_string(strIndex) + ">";
            }
        Value value = popStack();

        immutable_globals_.insert(name);
        globals[name] = value;
        emitVariableChanged(name);
        break;
    }

case OpCode::LOAD_VAR: {
            uint32_t var_index = instruction.operands[0].asInt();
            uint32_t abs = this->toAbsoluteLocal(var_index);
            this->ensureLocalIndex(abs);
Value value = locals[abs];

    // Record feedback
    auto &frame = currentFrame();
    if (frame.ip < frame.function->type_feedback.size()) {
      auto &fb = frame.function->type_feedback[frame.ip];
      fb.execution_count++;
      fb.result_type_mask |= getFeedbackMask(value);
      if (fb.execution_count == 1000 && hot_func_cb_) {
        hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
      }
    }

    pushStack(value);
    break;
    }

    case OpCode::STORE_VAR: {
            uint32_t var_index = instruction.operands[0].asInt();
            uint32_t abs = this->toAbsoluteLocal(var_index);
            this->ensureLocalIndex(abs);
            Value value = popStack();

        if (immutable_locals_.count(abs)) {
            COMPILER_THROW("Cannot reassign val local at index " + std::to_string(var_index));
        }

        // Record feedback
        auto &frame = currentFrame();
        if (frame.ip < frame.function->type_feedback.size()) {
            auto &fb = frame.function->type_feedback[frame.ip];
            fb.execution_count++;
            fb.left_type_mask |= getFeedbackMask(value);
            if (fb.execution_count == 1000 && hot_func_cb_) {
                hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
            }
        }

        locals[abs] = value;
        break;
    }

    case OpCode::STORE_IMMUT_VAR: {
        uint32_t var_index = instruction.operands[0].asInt();
        uint32_t abs = this->toAbsoluteLocal(var_index);
        this->ensureLocalIndex(abs);
        Value value = popStack();

        immutable_locals_.insert(abs);

        // Record feedback
        auto &frame = currentFrame();
        if (frame.ip < frame.function->type_feedback.size()) {
            auto &fb = frame.function->type_feedback[frame.ip];
            fb.execution_count++;
            fb.left_type_mask |= getFeedbackMask(value);
            if (fb.execution_count == 1000 && hot_func_cb_) {
                hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
            }
        }

        locals[abs] = value;
        break;
    }

    // Increment/Decrement local variable optimization
    case OpCode::INCLOCAL: {
        uint32_t var_index = instruction.operands[0].asInt();
        uint32_t abs = this->toAbsoluteLocal(var_index);
        this->ensureLocalIndex(abs);
        Value& val = locals[abs];
        // Record feedback - INCLOCAL is a loop counter hot spot
        {
            auto &frame = currentFrame();
            if (frame.ip < frame.function->type_feedback.size()) {
                auto &fb = frame.function->type_feedback[frame.ip];
                fb.execution_count++;
                fb.left_type_mask |= getFeedbackMask(val);
                if (fb.execution_count == 1000 && hot_func_cb_) {
                    hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
                }
            }
        }
        if (val.isInt()) {
            val = Value::makeInt(val.asInt() + 1);
            pushStack(val);
        } else if (val.isDouble()) {
            val = Value::makeDouble(val.asDouble() + 1.0);
            pushStack(val);
        } else {
            COMPILER_THROW("Cannot increment non-numeric value");
        }
        break;
    }

    case OpCode::DECLOCAL: {
        uint32_t var_index = instruction.operands[0].asInt();
        uint32_t abs = this->toAbsoluteLocal(var_index);
        this->ensureLocalIndex(abs);
        Value& val = locals[abs];
        {
            auto &frame = currentFrame();
            if (frame.ip < frame.function->type_feedback.size()) {
                auto &fb = frame.function->type_feedback[frame.ip];
                fb.execution_count++;
                fb.left_type_mask |= getFeedbackMask(val);
                if (fb.execution_count == 1000 && hot_func_cb_) {
                    hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
                }
            }
        }
        if (val.isInt()) {
            val = Value::makeInt(val.asInt() - 1);
            pushStack(val);
        } else if (val.isDouble()) {
            val = Value::makeDouble(val.asDouble() - 1.0);
            pushStack(val);
        } else {
            COMPILER_THROW("Cannot decrement non-numeric value");
        }
        break;
    }

    case OpCode::INCLOCAL_POST: {
        uint32_t var_index = instruction.operands[0].asInt();
        uint32_t abs = this->toAbsoluteLocal(var_index);
        this->ensureLocalIndex(abs);
        Value old = locals[abs];
        {
            auto &frame = currentFrame();
            if (frame.ip < frame.function->type_feedback.size()) {
                auto &fb = frame.function->type_feedback[frame.ip];
                fb.execution_count++;
                fb.left_type_mask |= getFeedbackMask(old);
            }
        }
        pushStack(old);  // Push old value first
        if (old.isInt()) {
            locals[abs] = Value::makeInt(old.asInt() + 1);
        } else if (old.isDouble()) {
            locals[abs] = Value::makeDouble(old.asDouble() + 1.0);
        } else {
            COMPILER_THROW("Cannot increment non-numeric value");
        }
        break;
    }

    case OpCode::DECLOCAL_POST: {
        uint32_t var_index = instruction.operands[0].asInt();
        uint32_t abs = this->toAbsoluteLocal(var_index);
        this->ensureLocalIndex(abs);
        Value old = locals[abs];
        {
            auto &frame = currentFrame();
            if (frame.ip < frame.function->type_feedback.size()) {
                auto &fb = frame.function->type_feedback[frame.ip];
                fb.execution_count++;
                fb.left_type_mask |= getFeedbackMask(old);
            }
        }
        pushStack(old);  // Push old value first
        if (old.isInt()) {
            locals[abs] = Value::makeInt(old.asInt() - 1);
        } else if (old.isDouble()) {
            locals[abs] = Value::makeDouble(old.asDouble() - 1.0);
        } else {
            COMPILER_THROW("Cannot decrement non-numeric value");
        }
        break;
    }

case OpCode::LOAD_UPVALUE: {
uint32_t upvalue_index = instruction.operands[0].asInt();
uint32_t closure_id = currentFrame().closure_id;
if (closure_id == 0) {
COMPILER_THROW("LOAD_UPVALUE used without active closure");
}
auto *closure = heap_.closure(closure_id);
if (!closure) {
COMPILER_THROW("Closure not found for LOAD_UPVALUE");
}
if (upvalue_index >= closure->upvalues.size() ||
!closure->upvalues[upvalue_index]) {
COMPILER_THROW("LOAD_UPVALUE index out of range");
}
        const auto &cell = closure->upvalues[upvalue_index];
  Value value;
  if (cell->is_open) {
    uint32_t abs_index = cell->locals_base + cell->open_index;
    this->ensureLocalIndex(abs_index);
    value = locals[abs_index];
  } else {
    value = cell->closed_value;
  }
    pushStack(value);
    break;
}

case OpCode::STORE_UPVALUE: {
uint32_t upvalue_index = instruction.operands[0].asInt();
uint32_t closure_id = currentFrame().closure_id;
if (closure_id == 0) {
COMPILER_THROW("STORE_UPVALUE used without active closure");
}
auto *closure = heap_.closure(closure_id);
if (!closure) {
COMPILER_THROW("Closure not found for STORE_UPVALUE");
}
if (upvalue_index >= closure->upvalues.size() ||
!closure->upvalues[upvalue_index]) {
COMPILER_THROW("STORE_UPVALUE index out of range");
}
auto &cell = closure->upvalues[upvalue_index];
Value value = popStack();

if (cell->is_open) {
uint32_t abs_index = cell->locals_base + cell->open_index;
this->ensureLocalIndex(abs_index);
locals[abs_index] = value;
} else {
cell->closed_value = value;
}
break;
}

  case OpCode::POP: {
    popStack();
    break;
  }

  case OpCode::DUP: {
    Value value = popStack();
    pushStack(value);
    pushStack(value);
    break;
  }

  case OpCode::SWAP: {
    Value top = popStack();
    Value next = popStack();
    pushStack(top);
    pushStack(next);
    break;
  }

  case OpCode::PUSH_NULL: {
    pushStack(Value::makeNull());
    break;
  }

    case OpCode::ADD:
    case OpCode::SUB:
    case OpCode::MUL:
    case OpCode::DIV:
    case OpCode::INT_DIV:
    case OpCode::MOD:
    case OpCode::DIVMOD:
    case OpCode::REMAINDER:
    case OpCode::POW:
    case OpCode::EQ:
    case OpCode::NEQ:
    case OpCode::IS:
    case OpCode::LT:
    case OpCode::LTE:
    case OpCode::GT:
    case OpCode::GTE:
    case OpCode::BIT_AND:
    case OpCode::BIT_OR:
    case OpCode::BIT_XOR:
    case OpCode::BIT_LSH:
    case OpCode::BIT_RSH:
      execBinaryOp(instruction);
      break;

  case OpCode::AND:
  case OpCode::OR:
    execLogicalOp(instruction.opcode);
    break;

        case OpCode::NOT: {
            Value v = popStack();
            if (v.isObjectId()) {
                Value opMethod = getHostObjectField(ObjectRef{v.asObjectId(), true}, "op_not");
                if (!opMethod.isNull() && (opMethod.isFunctionObjId() || opMethod.isClosureId() || opMethod.isHostFuncId())) {
                    pushStack(callFunction(opMethod, {v}));
                } else {
                    pushStack(!isTruthy(v));
                }
            } else {
                pushStack(!isTruthy(v));
            }
            break;
        }

	case OpCode::BIT_NOT: {
		Value v = popStack();
		if (v.isInt()) {
			pushStack(~v.asInt());
		} else if (v.isDouble()) {
			pushStack(~static_cast<int64_t>(v.asDouble()));
		} else if (v.isObjectId()) {
			Value opMethod = getHostObjectField(ObjectRef{v.asObjectId(), true}, "op_bit_not");
			if (!opMethod.isNull() && (opMethod.isFunctionObjId() || opMethod.isClosureId() || opMethod.isHostFuncId())) {
				pushStack(callFunction(opMethod, {v}));
			} else {
				COMPILER_THROW("Bitwise NOT requires integer operand or op_bit_not method");
			}
		} else {
			COMPILER_THROW("Bitwise NOT requires integer operand");
		}
		break;
	}

	case OpCode::NEGATE:
		execNegate();
		break;

case OpCode::LENGTH: {
    Value v = popStack();
    pushStack(execLengthOp(v));
    break;
}

	case OpCode::JUMP:
    execJump(instruction);
    break;

  case OpCode::JUMP_IF_FALSE:
    execJumpIfFalse(instruction);
    break;

  case OpCode::JUMP_IF_TRUE:
    execJumpIfTrue(instruction);
    break;

  case OpCode::IS_NULL: {
    Value value = popStack();
    bool isNullVal = value.isNull();
    pushStack(Value::makeBool(isNullVal));
    break;
  }

  case OpCode::JUMP_IF_NULL: {
    uint32_t target = instruction.operands[0].asInt();
    Value value = popStack();
    // Only jump on null/undefined, not on all falsy values
    if (value.isNull()) {
      currentFrame().ip = target;
    }
    break;
  }

        case OpCode::CALL: {
            uint32_t arg_count = instruction.operands[0].asInt();
            if (stack.size() < static_cast<size_t>(arg_count) + 1) {
                COMPILER_THROW("Stack underflow during CALL");
            }

            std::vector<Value> args(arg_count);
            for (uint32_t i = 0; i < arg_count; ++i) {
                args[arg_count - 1 - i] = popStack();
            }
Value callee_value = popStack();

// Handle callable objects (Lua-style __call metamethod, or op_call operator)
	if (callee_value.isObjectId()) {
		auto *obj = heap_.object(callee_value.asObjectId());
		if (obj) {
			// Look up __call or op_call in the object and its prototype chain
			GCHeap::ObjectEntry* search = obj;
			Value callFn = Value::makeNull();
			while (search) {
				auto* val = search->get("__call");
				if (!val) val = search->get("op_call");
				if (val) {
					callFn = *val;
					break;
				}
				auto* parentVal = search->get("__proto");
				if (!parentVal) parentVal = search->get("__class");
				if (!parentVal) parentVal = search->get("__parent");
				if (parentVal && parentVal->isObjectId()) {
					search = heap_.object(parentVal->asObjectId());
				} else {
					break;
				}
			}
        if (!callFn.isNull() && (callFn.isFunctionObjId() || callFn.isClosureId() || callFn.isHostFuncId())) {
          // Call __call with self as first arg
          std::vector<Value> callArgs;
          callArgs.push_back(callee_value);
          callArgs.insert(callArgs.end(), args.begin(), args.end());
          doCall(callFn, std::move(callArgs));
          break;
        }
      }
    }

    // Handle bound methods (lightweight BoundMethod struct)
    if (callee_value.isBoundMethodId()) {
        auto *bm = heap_.boundMethod(callee_value.asBoundMethodId());
        if (bm && (bm->fn.isHostFuncId() || bm->fn.isFunctionObjId() || bm->fn.isClosureId())) {
            std::vector<Value> boundArgs;
            boundArgs.push_back(bm->self);
            boundArgs.insert(boundArgs.end(), args.begin(), args.end());
            doCall(bm->fn, std::move(boundArgs));
            break;
        }
    }

    // Handle bound method objects (from runtime member lookup)
    if (callee_value.isObjectId()) {
      auto *obj = heap_.object(callee_value.asObjectId());
      if (obj) {
        auto fnIt = obj->find("fn");
        auto selfIt = obj->find("self");
        if (fnIt != obj->end() && selfIt != obj->end() &&
            (fnIt->second.isHostFuncId() || fnIt->second.isFunctionObjId() ||
             fnIt->second.isClosureId())) {
          // Prepend self to args
          std::vector<Value> boundArgs;
          boundArgs.push_back(selfIt->second);
          boundArgs.insert(boundArgs.end(), args.begin(), args.end());
          doCall(fnIt->second, std::move(boundArgs));
          break;
        }
      }
    }

    doCall(callee_value, std::move(args));
    break;
  }

case OpCode::TAIL_CALL: {
        // Tail call optimization: reuse current frame instead of pushing new one
        uint32_t arg_count = instruction.operands[0].asInt();
        if (stack.size() < static_cast<size_t>(arg_count) + 1) {
            COMPILER_THROW("Stack underflow during TAIL_CALL");
        }

    std::vector<Value> args(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      args[arg_count - 1 - i] = popStack();
    }
    Value callee_value = popStack();

    doTailCall(callee_value, std::move(args));
    break;
  }

  case OpCode::CALL_METHOD: {
    // Dispatches based on receiver type without boxing.
    if (instruction.operands.size() != 2 ||
        !instruction.operands[0].isStringValId() ||
        !instruction.operands[1].isInt()) {
      COMPILER_THROW("CALL_METHOD expects operands: <string method_name, uint32 arg_count>");
    }

            uint32_t strIndex = instruction.operands[0].asStringValId();
            const auto& cf_cm = currentFrame();
            const BytecodeChunk* resolveChunkCM = cf_cm.chunk ? cf_cm.chunk : current_chunk;
            std::string method_name;
            if (resolveChunkCM) {
                method_name = resolveChunkCM->getString(strIndex);
            }
            method_name = operatorSymbolToMethodName(method_name);
    uint32_t arg_count = instruction.operands[1].asInt();

    // Receiver is at stack top - arg_count positions down
    if (stack.size() < static_cast<size_t>(arg_count) + 1) {
      COMPILER_THROW("Stack underflow during CALL_METHOD (stack=" + std::to_string(stack.size()) + " need=" + std::to_string(arg_count + 1) + " method=" + method_name + ")");
    }

    // Peek at receiver (don't pop yet)
    std::vector<Value> temp_args;
    temp_args.reserve(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      temp_args.push_back(stack.top());
      stack.pop();
    }
    Value receiver = stack.top();
    // Push args back in reverse order
    for (auto it = temp_args.rbegin(); it != temp_args.rend(); ++it) {
      pushStack(*it);
    }

    // Determine type name for dispatch
    std::string type_name;
    uint32_t host_func_idx = 0;
    bool found_host = false;
    bool found_via_module = false;
    bool isInstanceFunc = false;
    Value vm_func = Value::makeNull();
    if (receiver.isStringValId() || receiver.isStringId()) {
      type_name = "string";
    } else if (receiver.isInt()) {
      type_name = "int";
    } else if (receiver.isDouble()) {
      type_name = "float";
    } else if (receiver.isBool()) {
      type_name = "bool";
    } else if (receiver.isArrayId()) {
      type_name = "array";
    } else if (receiver.isObjectId()) {
      type_name = "object";
    } else if (receiver.isSetId()) {
      type_name = "set";
    } else if (receiver.isThreadId()) {
      type_name = "thread";
    } else if (receiver.isIntervalId()) {
      type_name = "interval";
    } else if (receiver.isTimeoutId()) {
      type_name = "timeout";
} else if (receiver.isRangeId()) {
        type_name = "range";
    } else if (receiver.isHostFuncId()) {
        // Dotted host function call: e.g. interval.start(100, fn)
        // Resolve "interval.start" by concatenating receiver name + "." + method_name
        std::string receiver_name;
        if (receiver.asHostFuncId() < host_function_names_.size()) {
            receiver_name = host_function_names_[receiver.asHostFuncId()];
        }
        std::string dotted_name = receiver_name + "." + method_name;
        for (size_t i = 0; i < host_function_names_.size(); ++i) {
            if (host_function_names_[i] == dotted_name) {
                host_func_idx = static_cast<uint32_t>(i);
                found_host = true;
                found_via_module = true; // Don't pass receiver as self
                break;
            }
        }
        if (!found_host) {
            for (uint32_t i = 0; i < arg_count; ++i) popStack();
            popStack();
            pushStack(Value::makeNull());
            break;
        }
    } else {
        for (uint32_t i = 0; i < arg_count; ++i) popStack();
        popStack(); // receiver
        pushStack(Value::makeNull());
        break;
    }

// Look up method: 0. Object instance field, 1. Host prototype, 2. Module monkey-patch, 3. Class prototype chain

    // 0. If receiver is an object, check for direct callable field FIRST.
    // Namespace objects like `process` have host function fields (e.g. `find`)
    // that must take priority over prototype methods (e.g. `object.find`).
if (receiver.isObjectId()) {
    auto *instanceObj = heap_.object(receiver.asObjectId());
    if (instanceObj) {
        auto it = instanceObj->find(method_name);
        if (it != instanceObj->end()) {
          if (it->second.isHostFuncId()) {
            host_func_idx = it->second.asHostFuncId();
            found_host = true;
            // Set found_via_module if the object is in globals (likely a module/namespace)
            for (const auto &g : globals) {
              if (g.second.isObjectId() && g.second.asObjectId() == receiver.asObjectId()) {
                found_via_module = true;
                break;
              }
            }
          } else if (it->second.isFunctionObjId() || it->second.isClosureId()) {
            vm_func = it->second;
            isInstanceFunc = true;
          }
        }
      }
    }

    // 0.5 Check __class/__struct prototype for class/struct methods first
    if (!found_host && vm_func.isNull() && receiver.isObjectId()) {
      auto *classProto = heap_.object(receiver.asObjectId());
      if (classProto) {
        auto *classVal = classProto->get("__class");
        if (!classVal) classVal = classProto->get("__struct");
        if (classVal && classVal->isObjectId()) {
          classProto = heap_.object(classVal->asObjectId());
        } else {
          classProto = nullptr;
        }
      }

      while (classProto) {
        auto *methodVal = classProto->get(method_name);
        if (methodVal) {
          if (methodVal->isHostFuncId()) {
            host_func_idx = methodVal->asHostFuncId();
            found_host = true;
            break;
          } else if (methodVal->isFunctionObjId() || methodVal->isClosureId()) {
            vm_func = *methodVal;
            break;
          }
        }
        auto *parentVal = classProto->get("__parent");
        if (parentVal && parentVal->isObjectId()) {
          classProto = heap_.object(parentVal->asObjectId());
        } else {
          break;
        }
      }
    }

    // 1. Try host prototype (for primitives and built-in object methods)
    if (!found_host && vm_func.isNull()) {
        auto typeIt = prototypes_.find(type_name);
        if (typeIt != prototypes_.end()) {
            auto methodIt = typeIt->second.find(method_name);
            if (methodIt != typeIt->second.end()) {
                host_func_idx = methodIt->second;
                found_host = true;
            }
        }
    }

    // 1.5 Try module object for monkey-patched methods
    if (!found_host && vm_func.isNull()) {
      // Generate capitalized version (e.g., "string" -> "String")
      std::string capName = type_name;
      if (!capName.empty()) capName[0] = static_cast<char>(std::toupper(capName[0]));

      for (const auto &modName : {type_name, capName}) {
        auto modIt = globals.find(modName);
        if (modIt != globals.end() && modIt->second.isObjectId()) {
          auto *modObj = heap_.object(modIt->second.asObjectId());
          if (modObj) {
            auto *val = modObj->get(method_name);
            if (val) {
              if (val->isHostFuncId()) {
                host_func_idx = val->asHostFuncId();
                found_host = true;
                // Monkey-patched methods in prototypes SHOULD receive 'self', so found_via_module stays false.
                break;
              } else if (val->isFunctionObjId() || val->isClosureId()) {
                vm_func = *val;
                break;
              }
            }
          }
        }
      }
    }

    if (!found_host && vm_func.isNull()) {
      // Pop args and receiver before pushing null result
      for (uint32_t i = 0; i < arg_count; ++i) popStack();
      popStack(); // receiver
      pushStack(Value::makeNull());
      break;
    }

// Pop args and receiver
std::vector<Value> args2(arg_count);
for (uint32_t i = 0; i < arg_count; ++i) {
args2[arg_count - 1 - i] = popStack();
}
Value recv = popStack();

// Prepare args
std::vector<Value> all_args;
if (isInstanceFunc || found_via_module) {
all_args = args2;
} else {
all_args.reserve(arg_count + 1);
all_args.push_back(recv);
all_args.insert(all_args.end(), args2.begin(), args2.end());
}

if (found_host) {
      if (host_func_idx < host_function_names_.size()) {
        std::string resolved_name = host_function_names_[host_func_idx];
        auto fnIt = host_functions.find(resolved_name);
        if (fnIt != host_functions.end()) {
          Value result = fnIt->second(all_args);
          pushStack(result);
          if (currentFrame().ip < currentFrame().function->type_feedback.size()) {
            currentFrame().function->type_feedback[currentFrame().ip].result_type_mask |= getFeedbackMask(result);
          }
        } else {
          pushStack(Value::makeNull());
        }
      } else {
        pushStack(Value::makeNull());
      }
    } else {
      // Call VM function
      doCall(vm_func, all_args, true);
    }
    break;
  }

  case OpCode::CALL_SUPER: {
    // CALL_SUPER: operands are [method_name, arg_count]
    // Pops args from stack, looks up parent class method, calls it
    if (instruction.operands.size() != 2 ||
        !instruction.operands[0].isStringValId() ||
        !instruction.operands[1].isInt()) {
      COMPILER_THROW("CALL_SUPER expects operands: <string "
                               "method_name, uint32 arg_count>");
    }

    const std::string &method_name =
        instruction.operands[0].toString();
    uint32_t arg_count = instruction.operands[1].asInt();

    if (stack.size() < static_cast<size_t>(arg_count)) {
      COMPILER_THROW("Stack underflow during CALL_SUPER");
    }

    // Pop arguments from stack
    std::vector<Value> args(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      args[arg_count - 1 - i] = popStack();
    }

    // Get current 'this' from local scope (slot 0 typically)
    size_t base = currentFrame().locals_base;
    Value this_value = locals[base + 0];

    // Find the parent class method using the prototype chain
    // For now, emit as a host function call with special prefix
    // Full implementation needs parent method lookup via heap_.findClassMethod
    std::string super_method_name = "super." + method_name;

    // Prepend 'this' to args
    args.insert(args.begin(), this_value);

    // Call as host function - runtime will need to resolve via parent class
    pushStack(invokeHostFunction(super_method_name,
                            static_cast<uint32_t>(args.size())));
    break;
  }

  case OpCode::STRUCT_NEW: {
    if (instruction.operands.size() != 2 ||
        !instruction.operands[0].isStringValId() ||
        !instruction.operands[1].isInt()) {
      COMPILER_THROW("STRUCT_NEW expects operands: <string type, uint32 argc>");
    }
    const uint32_t argc = instruction.operands[1].asInt();
    if (stack.size() < argc) {
      COMPILER_THROW("STRUCT_NEW stack underflow");
    }
    std::vector<Value> args(argc + 1);
    args[0] = Value::makeStringValId(instruction.operands[0].asStringValId());
    for (uint32_t i = 0; i < argc; ++i) {
      args[argc - i] = popStack();
    }
    pushStack(invokeHostFunctionDirect("struct.new", args));
    break;
  }

  case OpCode::STRUCT_GET: {
    if (instruction.operands.size() != 1 || !instruction.operands[0].isStringValId()) {
      COMPILER_THROW("STRUCT_GET expects operand: <string field>");
    }
    if (!current_chunk) {
      COMPILER_THROW("STRUCT_GET requires active chunk");
    }
    Value instance = popStack();
    Value field = Value::makeStringValId(instruction.operands[0].asStringValId());
    pushStack(invokeHostFunctionDirect("struct.get", {instance, field}));
    break;
  }

  case OpCode::STRUCT_SET: {
    if (instruction.operands.size() != 1 || !instruction.operands[0].isStringValId()) {
      COMPILER_THROW("STRUCT_SET expects operand: <string field>");
    }
    Value value = popStack();
    Value instance = popStack();
    Value field = Value::makeStringValId(instruction.operands[0].asStringValId());
    pushStack(invokeHostFunctionDirect("struct.set", {instance, field, value}));
    break;
  }

    case OpCode::PROT_CHECK: {
      if (instruction.operands.size() != 1 || !instruction.operands[0].isStringValId()) {
        COMPILER_THROW("PROT_CHECK expects operand: <string protocol>");
      }
      if (!current_chunk) {
        COMPILER_THROW("PROT_CHECK requires active chunk");
      }
    Value value = popStack();
    const std::string proto =
        current_chunk->getString(instruction.operands[0].asStringValId());
    std::string typeName = getTypeName(value);
    if (typeImplementsProtocol(typeName, proto)) {
      pushStack(Value::makeBool(true));
    } else {
      pushStack(Value::makeBool(false));
    }
    break;
  }

  case OpCode::PROT_CAST: {
    if (instruction.operands.size() != 1 || !instruction.operands[0].isStringValId()) {
      COMPILER_THROW("PROT_CAST expects operand: <string protocol>");
    }
    if (!current_chunk) {
      COMPILER_THROW("PROT_CAST requires active chunk");
    }
    Value value = popStack();
    const std::string proto =
        current_chunk->getString(instruction.operands[0].asStringValId());
    std::string typeName = getTypeName(value);
    pushStack(typeImplementsProtocol(typeName, proto) ? value : Value::makeNull());
    break;
  }

        case OpCode::RETURN: {
            this->doReturn();
            break;
        }

  case OpCode::TRY_ENTER: {
    if (instruction.operands.size() < 1 ||
        !instruction.operands[0].isInt()) {
      COMPILER_THROW("TRY_ENTER expects catch ip operand");
    }
    const uint32_t catch_ip = instruction.operands[0].asInt();
    uint32_t finally_ip = 0;
    if (instruction.operands.size() >= 2 &&
        instruction.operands[1].isInt()) {
      finally_ip = instruction.operands[1].asInt();
    }
    currentFrame().try_stack.push_back(TryHandler{.catch_ip = catch_ip,
                                                  .finally_ip = finally_ip,
                                                  .finally_return_ip = 0,
                                                  .stack_depth = stack.size()});
    break;
  }

  case OpCode::TRY_EXIT: {
    if (!currentFrame().try_stack.empty()) {
      currentFrame().try_stack.pop_back();
    }
    break;
  }

  case OpCode::LOAD_EXCEPTION: {
    if (has_current_exception_) {
      pushStack(current_exception_);
    } else {
      pushStack(nullptr);
    }
    break;
  }

  case OpCode::THROW: {
    Value thrown = popStack();
    throw ScriptThrow{std::move(thrown)};
  }

case OpCode::CLOSURE: {
    if (instruction.operands.empty()) COMPILER_THROW("CLOSURE: no operands");
    uint32_t function_index = instruction.operands[0].asInt();
    const auto *target = current_chunk->getFunction(function_index);
    if (!target) {
      COMPILER_THROW("CLOSURE references unknown function index");
    }

    GCHeap::RuntimeClosure closure;
    closure.function_index = function_index;
    closure.upvalues.reserve(target->upvalues.size());
    for (const auto &descriptor : target->upvalues) {
        if (descriptor.captures_local) {
          uint32_t abs = this->toAbsoluteLocal(descriptor.index);
          this->ensureLocalIndex(abs);
          auto open_it = open_upvalues.find(abs);
          if (open_it == open_upvalues.end()) {
      auto cell = std::make_shared<GCHeap::UpvalueCell>();
        cell->is_open = true;
        cell->open_index = descriptor.index;
        cell->locals_base = currentFrame().locals_base;
        open_upvalues.emplace(abs, cell);
            closure.upvalues.push_back(std::move(cell));
} else {
closure.upvalues.push_back(open_it->second);
}
} else {
uint32_t parent_closure_id = currentFrame().closure_id;
if (parent_closure_id == 0) {
COMPILER_THROW(
"CLOSURE tried to capture upvalue without parent closure");
}
auto *parent_closure = heap_.closure(parent_closure_id);
        if (!parent_closure) {
          COMPILER_THROW("Parent closure not found for CLOSURE");
        }
        if (descriptor.index >= parent_closure->upvalues.size()) {
          COMPILER_THROW("CLOSURE upvalue index out of range");
        }
        closure.upvalues.push_back(parent_closure->upvalues[descriptor.index]);
      }
    }

  std::shared_ptr<std::unordered_map<std::string, Value>> closure_globals;
  if (current_chunk != main_chunk_.get()) {
    closure_globals = std::make_shared<std::unordered_map<std::string, Value>>(globals);
  }
            pushStack(Value::makeClosureId(heap_.allocateClosure(
                GCHeap::RuntimeClosure{.function_index = closure.function_index,
                                        .chunk_index = 0,
                                        .chunk = current_chunk,
.module_globals = std::move(closure_globals),
    .upvalues = std::move(closure.upvalues)}).id));
    break;
  }

  case OpCode::ARRAY_NEW: {
    
    Value arr = Value::makeArrayId(heap_.allocateArray().id);
    
    pushStack(arr);
    maybeCollectGarbage();
    break;
  }

  case OpCode::SET_NEW: {
    pushStack(Value::makeSetId(heap_.allocateSet().id));
    maybeCollectGarbage();
    break;
  }

  case OpCode::SET_SET: {
    // Stack: [..., set, value, key] → pops all, does NOT push set back
    // The caller is responsible for managing the set on the stack
    Value key = popStack();
    Value value = popStack();
    Value set_val = popStack();

    if (!set_val.isSetId()) {
      COMPILER_THROW("SET_SET expects set container");
    }
    uint32_t id = set_val.asSetId();
    auto *set = heap_.set(id);
    if (!set) {
      COMPILER_THROW("SET_SET unknown set id");
    }

    std::string keyStr;
    if (key.isStringValId() && current_chunk) {
      keyStr = current_chunk->getString(key.asStringValId());
    } else if (key.isStringId()) {
      if (auto *s = heap_.string(key.asStringId())) {
        keyStr = *s;
      } else {
        COMPILER_THROW("SET_SET expects string key");
      }
    } else if (key.isInt()) {
      keyStr = std::to_string(key.asInt());
    } else {
      COMPILER_THROW("SET_SET expects string/number key");
    }

    (*set)[keyStr] = value;
    // Don't push set back - the caller manages it
    break;
  }

  case OpCode::ARRAY_PUSH: {
    Value value = popStack();
    Value container = popStack();

    if (!container.isArrayId()) {
      COMPILER_THROW("ARRAY_PUSH expects array container");
    }
    uint32_t id = container.asArrayId();
    auto *array = heap_.array(id);
    if (!array) {
      COMPILER_THROW("ARRAY_PUSH unknown array id");
    }
    if (array->frozen) {
      COMPILER_THROW("Cannot modify frozen array (tuple)");
    }
    array->push_back(value);
    pushStack(container);
    break;
  }

  case OpCode::ARRAY_LEN: {
    Value container = popStack();
    if (!container.isArrayId()) {
      COMPILER_THROW("ARRAY_LEN expects array container");
    }
    uint32_t id = container.asArrayId();
    auto *array = heap_.array(id);
    if (!array) {
      COMPILER_THROW("ARRAY_LEN unknown array id");
    }
    pushStack(Value::makeInt(static_cast<int64_t>(array->size())));
    break;
  }

  case OpCode::ARRAY_FREEZE: {
    Value container = popStack();
    if (!container.isArrayId()) {
      COMPILER_THROW("ARRAY_FREEZE expects array container");
    }
    uint32_t id = container.asArrayId();
    auto *array = heap_.array(id);
    if (!array) {
      COMPILER_THROW("ARRAY_FREEZE unknown array id");
    }
    array->frozen = true;
    pushStack(container);
    break;
  }

  // Range creation: start..end or start..step..end
  case OpCode::RANGE_NEW: {
    int64_t end = popStack().asInt();
    int64_t start = popStack().asInt();
    RangeRef rangeRef = heap_.allocateRange(start, end, 1);
    pushStack(Value::makeRangeId(rangeRef.id));
    break;
  }

  case OpCode::RANGE_STEP_NEW: {
    int64_t step = popStack().asInt();
    int64_t end = popStack().asInt();
    int64_t start = popStack().asInt();
    RangeRef rangeRef = heap_.allocateRange(start, end, step);
    pushStack(Value::makeRangeId(rangeRef.id));
    break;
  }

  // Enum operations
  case OpCode::ENUM_NEW: {
    // Operands: typeId (uint32), tag (uint32), payloadCount (uint32)
    uint32_t typeId = instruction.operands[0].asInt();
    uint32_t tag = instruction.operands[1].asInt();
    uint32_t payloadCount = instruction.operands[2].asInt();
    EnumRef enumRef = heap_.allocateEnum(typeId, tag, payloadCount);
    pushStack(Value::makeEnumId(enumRef.id));
    break;
  }

  case OpCode::ENUM_TAG: {
    Value enumVal = popStack();
    if (!enumVal.isEnumId()) {
      COMPILER_THROW("ENUM_TAG expects enum");
    }
    auto enumRef = EnumRef{enumVal.asEnumId(), 0, 0};
    pushStack(Value::makeInt(static_cast<int64_t>(getEnumTag(enumRef))));
    break;
  }

  case OpCode::ENUM_PAYLOAD: {
    Value indexVal = popStack();
    Value enumVal = popStack();
    if (!enumVal.isEnumId() || !indexVal.isInt()) {
      COMPILER_THROW("ENUM_PAYLOAD expects enum and int index");
    }
    auto enumRef = EnumRef{enumVal.asEnumId(), 0, 0};
    size_t index = static_cast<size_t>(indexVal.asInt());
    pushStack(heap_.enums_.at(enumRef.id).second.at(index));
    break;
  }

  case OpCode::ENUM_MATCH: {
    // Pop: enum ref, expected tag
    Value tagVal = popStack();
    Value enumVal = popStack();
    if (!enumVal.isEnumId() || !tagVal.isInt()) {
      COMPILER_THROW("ENUM_MATCH expects enum and int tag");
    }
    auto enumRef = EnumRef{enumVal.asEnumId(), 0, 0};
    int64_t expectedTag = tagVal.asInt();
    pushStack(Value::makeBool(getEnumTag(enumRef) ==
                              static_cast<uint32_t>(expectedTag)));
    break;
  }

  // String promotion: StringValId → StringId (for runtime string iteration)
  case OpCode::STRING_PROMOTE: {
    Value v = popStack();
    if (v.isStringValId()) {
      // Look up string from chunk's string table and allocate on heap
      uint32_t strIdx = v.asStringValId();
      std::string s;
      if (current_chunk) {
        s = current_chunk->getString(strIdx);
      }
      auto strRef = heap_.allocateString(s);
      pushStack(Value::makeStringId(strRef.id));
    } else {
      // Already a StringId or other type, passthrough
      pushStack(v);
    }
    break;
  }

  // Iteration protocol: iter(obj) → iterator
  case OpCode::ITER_NEW: {
    Value iterable = popStack();

    // Create iterator based on type
    IteratorRef iterRef;
    iterRef.id = heap_.createIterator(iterable);
    pushStack(Value::makeIteratorId(iterRef.id));
    break;
  }

  // Iteration protocol: iterator.next() → {value, done}
  case OpCode::ITER_NEXT: {
    Value iterator_val = popStack();
    if (!iterator_val.isIteratorId()) {
      COMPILER_THROW("ITER_NEXT expects iterator");
    }

    uint32_t id = iterator_val.asIteratorId();
    auto result = heap_.iteratorNext(id);

    // result is {value, done} object
    pushStack(result);
    break;
  }

  case OpCode::ARRAY_GET: {
    Value index_or_key = popStack();
    Value container = popStack();

    // Record feedback
    auto &frame = currentFrame();
    if (frame.ip < frame.function->type_feedback.size()) {
      auto &fb = frame.function->type_feedback[frame.ip];
      fb.execution_count++;
      fb.left_type_mask |= getFeedbackMask(container);
      fb.right_type_mask |= getFeedbackMask(index_or_key);
      if (fb.execution_count == 1000 && hot_func_cb_) {
        hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
      }
    }

        if (container.isStringId() || container.isStringValId()) {
            auto index = indexFromValue(index_or_key);
            if (!index) {
                COMPILER_THROW("STRING_GET expects integer index");
            }
            std::string s;
            if (container.isStringId()) {
                auto *sp = heap_.string(container.asStringId());
                if (sp) s = *sp;
            } else if (container.isStringValId() && current_chunk) {
                s = current_chunk->getString(container.asStringValId());
            }
            int64_t numCodepoints = 0;
            size_t bytePos = 0;
            while (bytePos < s.size()) {
                unsigned char c = static_cast<unsigned char>(s[bytePos]);
                size_t cpLen = 1;
                if (c < 0x80) { cpLen = 1; }
                else if ((c & 0xE0) == 0xC0) { cpLen = 2; }
                else if ((c & 0xF0) == 0xE0) { cpLen = 3; }
                else if ((c & 0xF8) == 0xF0) { cpLen = 4; }
                if (bytePos + cpLen > s.size()) { cpLen = 1; }
                numCodepoints++;
                bytePos += cpLen;
            }
            int64_t idx = *index;
            if (idx < 0) idx = numCodepoints + idx;
            if (idx < 0 || idx >= numCodepoints) {
                pushStack(Value::makeNull());
            } else {
                size_t targetByte = 0;
                int64_t cpIdx = 0;
                while (cpIdx < idx && targetByte < s.size()) {
                    unsigned char c = static_cast<unsigned char>(s[targetByte]);
                    size_t cpLen = 1;
                    if (c < 0x80) { cpLen = 1; }
                    else if ((c & 0xE0) == 0xC0) { cpLen = 2; }
                    else if ((c & 0xF0) == 0xE0) { cpLen = 3; }
                    else if ((c & 0xF8) == 0xF0) { cpLen = 4; }
                    if (targetByte + cpLen > s.size()) { cpLen = 1; }
                    targetByte += cpLen;
                    cpIdx++;
                }
                size_t cpLen = 1;
                if (targetByte < s.size()) {
                    unsigned char c = static_cast<unsigned char>(s[targetByte]);
                    if (c < 0x80) { cpLen = 1; }
                    else if ((c & 0xE0) == 0xC0) { cpLen = 2; }
                    else if ((c & 0xF0) == 0xE0) { cpLen = 3; }
                    else if ((c & 0xF8) == 0xF0) { cpLen = 4; }
                    if (targetByte + cpLen > s.size()) { cpLen = 1; }
                }
                auto ref = heap_.allocateString(s.substr(targetByte, cpLen));
                pushStack(Value::makeStringId(ref.id));
            }
            break;
        }

        if (container.isArrayId()) {
            auto index = indexFromValue(index_or_key);
            if (!index) {
                COMPILER_THROW("ARRAY_GET expects integer index");
            }
      auto *array = heap_.array(container.asArrayId());
      if (!array) {
        COMPILER_THROW("ARRAY_GET unknown array id");
      }
      // Handle negative indices: -1 = last element, -2 = second to last, etc.
      int64_t idx = *index;
      if (idx < 0) {
        idx = static_cast<int64_t>(array->size()) + idx;
      }
      Value result;
      if (idx < 0 || static_cast<size_t>(idx) >= array->size()) {
        result = Value::makeNull();
      } else {
        result = (*array)[static_cast<size_t>(idx)];
      }
      
      if (frame.ip < frame.function->type_feedback.size()) {
        frame.function->type_feedback[frame.ip].result_type_mask |= getFeedbackMask(result);
      }
      pushStack(result);
      break;
    }

    if (container.isSetId()) {
      auto key = resolveKey(index_or_key);
      if (!key) {
        COMPILER_THROW(
            "SET membership expects string/number/bool key");
      }
      auto *set = heap_.set(container.asSetId());
      if (!set) {
        COMPILER_THROW("ARRAY_GET unknown set id");
      }
      Value result = Value::makeBool(set->find(*key) != set->end());
      if (frame.ip < frame.function->type_feedback.size()) {
        frame.function->type_feedback[frame.ip].result_type_mask |= getFeedbackMask(result);
      }
      pushStack(result);
      break;
    }

    if (container.isObjectId()) {
        // Operator overloading: check for op_index method
        {
            Value opIndex = getHostObjectField(ObjectRef{container.asObjectId(), true}, "op_index");
            if (!opIndex.isNull() && (opIndex.isFunctionObjId() || opIndex.isClosureId() || opIndex.isHostFuncId())) {
                pushStack(callFunction(opIndex, {container, index_or_key}));
                break;
            }
        }
        // _G globals mirror: resolve from live globals maps
        if (container.asObjectId() == globals_mirror_object_id_) {
        auto key = resolveKey(index_or_key);
        if (!key) {
          COMPILER_THROW("OBJECT index expects string/number/bool key");
        }
        Value result = Value::makeNull();
        auto it = globals.find(*key);
        if (it != globals.end()) {
          result = it->second;
        } else {
          auto hostIt = host_function_globals_.find(*key);
          if (hostIt != host_function_globals_.end()) {
            result = hostIt->second;
          }
        }
        if (frame.ip < frame.function->type_feedback.size()) {
          frame.function->type_feedback[frame.ip].result_type_mask |= getFeedbackMask(result);
        }
        pushStack(result);
        break;
      }
      auto key = resolveKey(index_or_key);
      if (!key) {
        COMPILER_THROW("OBJECT index expects string/number/bool key");
      }
      auto *object = heap_.object(container.asObjectId());
      if (!object) {
        COMPILER_THROW("ARRAY_GET unknown object id");
      }
      auto kv = object->find(*key);
      Value result = (kv == object->end() ? Value::makeNull() : kv->second);
      if (frame.ip < frame.function->type_feedback.size()) {
        frame.function->type_feedback[frame.ip].result_type_mask |= getFeedbackMask(result);
      }
      pushStack(result);
      break;
    }

    COMPILER_THROW("ARRAY_GET expects array/set/object container");
  }

  case OpCode::ARRAY_SET: {
    Value value = popStack();
    Value index_or_key = popStack();
    Value container = popStack();

    if (container.isArrayId()) {
      auto index = indexFromValue(index_or_key);
      if (!index) {
        COMPILER_THROW("ARRAY_SET expects integer index");
      }
      auto *array = heap_.array(container.asArrayId());
      if (!array) {
        COMPILER_THROW("ARRAY_SET unknown array id");
      }
      if (array->frozen) {
        COMPILER_THROW("Cannot modify frozen array (tuple)");
      }
  // Handle negative indices for ARRAY_SET: -1 = last element, -2 = second to last
  int64_t idx = *index;
  if (idx < 0) {
    idx = static_cast<int64_t>(array->size()) + idx;
    if (idx < 0) {
      COMPILER_THROW("ARRAY_SET index out of bounds: " + std::to_string(*index));
    }
  }
  const auto idx_size = static_cast<size_t>(idx);
  if (idx_size >= array->size()) {
    array->resize(idx_size + 1, Value::makeNull());
  }
  (*array)[idx_size] = value;
      break;
    }

    if (container.isSetId()) {
      auto key = resolveKey(index_or_key);
      if (!key) {
        COMPILER_THROW(
            "SET assignment expects string/number/bool key");
      }
      auto *set = heap_.set(container.asSetId());
      if (!set) {
        COMPILER_THROW("ARRAY_SET unknown set id");
      }
      bool present = false;
      if (value.isBool()) {
        present = value.asBool();
      } else if (value.isInt()) {
        present = value.asInt() != 0;
      } else if (value.isDouble()) {
        present = value.asDouble() != 0.0;
      } else {
        COMPILER_THROW(
            "SET assignment value must be bool/number to indicate presence");
      }
      if (present) {
        (*set)[*key] = Value::makeNull();
      } else {
        set->erase(*key);
      }
      break;
    }

    if (container.isObjectId()) {
        // Operator overloading: check for op_index_set method
        {
            Value opIndexSet = getHostObjectField(ObjectRef{container.asObjectId(), true}, "op_index_set");
            if (!opIndexSet.isNull() && (opIndexSet.isFunctionObjId() || opIndexSet.isClosureId() || opIndexSet.isHostFuncId())) {
                callFunction(opIndexSet, {container, index_or_key, value});
                pushStack(container);
                break;
            }
        }
        auto key = resolveKey(index_or_key);
		if (!key) {
			COMPILER_THROW("OBJECT index assignment expects valid key");
		}
		if (container.asObjectId() == globals_mirror_object_id_) {
			auto *object = heap_.object(container.asObjectId());
			if (!object) {
				COMPILER_THROW("ARRAY_SET unknown object id");
			}
			(*object)[*key] = value;
			globals[*key] = value;
			break;
		}
		auto *object = heap_.object(container.asObjectId());
		if (!object) {
			COMPILER_THROW("ARRAY_SET unknown object id");
		}
		(*object)[*key] = value;
		break;
	}

    COMPILER_THROW("ARRAY_SET expects array/set/object container");
  }

  case OpCode::OBJECT_NEW: {
    pushStack(Value::makeObjectId(heap_.allocateObject(true).id)); // sorted = true
    maybeCollectGarbage();
    break;
  }

  case OpCode::OBJECT_NEW_UNSORTED: {
    pushStack(Value::makeObjectId(heap_.allocateObject(false).id)); // sorted = false
    maybeCollectGarbage();
    break;
  }

  case OpCode::OBJECT_GET: {
    Value key_value = popStack();
    Value object = popStack();

    // Handle function objects - support fn.name, fn.arity, fn.params, fn.prop
    if (object.isFunctionObjId()) {
      uint32_t funcIdx = object.asFunctionObjId();
      auto key = resolveKey(key_value);
      
      // First check for user-defined properties
      if (key) {
        auto propIt = function_properties_.find(funcIdx);
        if (propIt != function_properties_.end()) {
          auto* props = heap_.object(propIt->second.id);
          if (props) {
            auto* val = props->get(*key);
            if (val) {
              pushStack(*val);
              break;
            }
          }
        }
      }
      
      // Then check built-in properties
      if (current_chunk) {
        const auto* func = current_chunk->getFunction(funcIdx);
        if (key && func) {
          if (*key == "name") {
            pushStack(Value::makeStringId(heap_.allocateString(func->name).id));
            break;
          } else if (*key == "arity") {
            pushStack(Value::makeInt(static_cast<int64_t>(func->param_count)));
            break;
          } else if (*key == "params") {
            auto arrRef = heap_.allocateArray();
            auto* arr = heap_.array(arrRef.id);
            for (const auto& p : func->param_names) {
              arr->push_back(Value::makeStringId(heap_.allocateString(p).id));
            }
            pushStack(Value::makeArrayId(arrRef.id));
            break;
          }
        }
      }
        pushStack(Value::makeNull());
        break;
    }

    // Handle closure objects - support closure.name, closure.arity, closure.prop
    if (object.isClosureId()) {
        uint32_t closureId = object.asClosureId();
        auto key = resolveKey(key_value);

        // First check for user-defined properties
        if (key) {
            auto propIt = closure_properties_.find(closureId);
            if (propIt != closure_properties_.end()) {
                auto* props = heap_.object(propIt->second.id);
                if (props) {
                    auto* val = props->get(*key);
                    if (val) {
                        pushStack(*val);
                        break;
                    }
                }
            }
        }

        // Then check built-in properties from underlying function
        auto* closure = heap_.closure(closureId);
        if (closure && current_chunk && key) {
            if (closure->function_index < current_chunk->getFunctionCount()) {
                const auto* func = current_chunk->getFunction(closure->function_index);
                if (func) {
                    if (*key == "name") {
                        pushStack(Value::makeStringId(heap_.allocateString(func->name).id));
                        break;
                    } else if (*key == "arity") {
                        pushStack(Value::makeInt(static_cast<int64_t>(func->param_count)));
                        break;
                    }
                }
            }
        }
        pushStack(Value::makeNull());
        break;
    }

    // Handle host function objects - support hostfn.name, hostfn.prop
    if (object.isHostFuncId()) {
        uint32_t hostIdx = object.asHostFuncId();
        auto key = resolveKey(key_value);

        // First check for user-defined properties
        if (key) {
            auto propIt = hostfunc_properties_.find(hostIdx);
            if (propIt != hostfunc_properties_.end()) {
                auto* props = heap_.object(propIt->second.id);
                if (props) {
                    auto* val = props->get(*key);
                    if (val) {
                        pushStack(*val);
                        break;
                    }
                }
            }
        }

        // Then check built-in properties
        if (key) {
            if (*key == "name") {
                if (hostIdx < host_function_names_.size()) {
                    pushStack(Value::makeStringId(heap_.allocateString(host_function_names_[hostIdx]).id));
                    break;
                }
            } else if (*key == "arity") {
                pushStack(Value::makeInt(-1));
                break;
            }
        }
        pushStack(Value::makeNull());
        break;
    }

    // Handle class/struct prototype objects - support Class.name, Class.methods, Class.fields
    if (object.isObjectId()) {
      auto* obj = heap_.object(object.asObjectId());
      if (obj) {
        // Check if this is a class/struct prototype (has __is_class or __is_struct marker)
        bool isClass = false;
        if (obj->get("__is_class") || obj->get("__is_struct")) {
          isClass = true;
        }
        if (isClass) {
          auto key = resolveKey(key_value);
          if (key) {
            if (*key == "name") {
              auto* nameVal = obj->get("__name");
              if (nameVal) { pushStack(*nameVal); break; }
              pushStack(Value::makeNull());
              break;
            } else if (*key == "methods") {
              // Collect method names from the prototype
              auto arrRef = heap_.allocateArray();
              auto* arr = heap_.array(arrRef.id);
              auto keys = obj->getKeys();
              for (const auto& k : keys) {
                auto* val = obj->get(k);
                if (val && (val->isFunctionObjId() || val->isHostFuncId())) {
                  arr->push_back(Value::makeStringId(heap_.allocateString(k).id));
                }
              }
              pushStack(Value::makeArrayId(arrRef.id));
              break;
            } else if (*key == "fields") {
              auto* fieldsVal = obj->get("__fields");
              if (fieldsVal && fieldsVal->isArrayId()) {
                pushStack(*fieldsVal);
              } else {
                // Collect from default null fields
                auto arrRef = heap_.allocateArray();
                auto* arr = heap_.array(arrRef.id);
                auto keys = obj->getKeys();
                for (const auto& k : keys) {
                  auto* val = obj->get(k);
                  if (val && val->isNull()) {
                    arr->push_back(Value::makeStringId(heap_.allocateString(k).id));
                  }
                }
                pushStack(Value::makeArrayId(arrRef.id));
              }
              break;
            }
          }
        }
      }
    }



    // Handle arrays - look up prototype methods OR numeric indices
    if (object.isArrayId()) {
      auto *array = heap_.array(object.asArrayId());
      
      // Check for numeric index first
      if (key_value.isInt() && array) {
        int64_t index = key_value.asInt();
        if (index < 0) {
          index = static_cast<int64_t>(array->size()) + index;
        }
        if (index >= 0 && static_cast<size_t>(index) < array->size()) {
          pushStack((*array)[static_cast<size_t>(index)]);
        } else {
          pushStack(Value::makeNull());
        }
        break;
      }
      
        // Then check for prototype methods
        auto key = resolveKey(key_value);
        if (key) {
            if (*key == "len" && array) {
                pushStack(Value::makeInt(static_cast<int64_t>(array->size())));
                break;
            }
            auto method = getPrototypeMethod(object, *key);
            if (method) {
                auto bmRef = heap_.allocateBoundMethod(Value::makeHostFuncId(getHostFunctionIndex(host_function_names_[*method])), Value::makeArrayId(object.asArrayId()));
                pushStack(Value::makeBoundMethodId(bmRef.id));
                break;
            }
        }
        pushStack(Value::makeNull());
        break;
    }

    // Handle strings - look up prototype methods
    if (object.isStringId() || object.isStringValId()) {
        // Promote StringValId to StringId if needed
        Value stringVal = object;
        std::string actualStr;
        if (object.isStringValId() && current_chunk) {
            actualStr = current_chunk->getString(object.asStringValId());
            auto strRef = heap_.allocateString(actualStr);
            stringVal = Value::makeStringId(strRef.id);
        } else if (object.isStringId()) {
            auto *s = heap_.string(object.asStringId());
            if (s) actualStr = *s;
        }
        auto key = resolveKey(key_value);
        if (key) {
            if (*key == "len") {
                pushStack(Value::makeInt(static_cast<int64_t>(actualStr.size())));
                break;
            }
                auto method = getPrototypeMethod(stringVal, *key);
                if (method) {
                    auto bmRef = heap_.allocateBoundMethod(Value::makeHostFuncId(*method), stringVal);
                    pushStack(Value::makeBoundMethodId(bmRef.id));
                    break;
                }
      }
      pushStack(Value::makeNull());
      break;
    }

    // Handle ranges - expose "step" property for type detection
    if (object.isRangeId()) {
      auto *r = heap_.range(object.asRangeId());
      if (r) {
        auto key = resolveKey(key_value);
        if (key) {
          if (*key == "step") {
            pushStack(Value::makeInt(r->step));
            break;
          } else if (*key == "start") {
            pushStack(Value::makeInt(r->start));
            break;
          } else if (*key == "end") {
            pushStack(Value::makeInt(r->end));
            break;
          }
        }
      }
      pushStack(Value::makeNull());
      break;
    }

    if (object.isHostFuncId()) {
      auto key = resolveKey(key_value);
      if (key) {
        uint32_t idx = object.asHostFuncId();
        if (*key == "name" && idx < host_function_names_.size()) {
          pushStack(Value::makeStringId(heap_.allocateString(host_function_names_[idx]).id));
          break;
        } else if (*key == "arity") {
          pushStack(Value::makeInt(0));
          break;
        }
      }
      pushStack(Value::makeNull());
      break;
    }

    if (object.isSetId()) {
      auto key = resolveKey(key_value);
      if (key && *key == "len") {
        auto *set = heap_.set(object.asSetId());
        pushStack(Value::makeInt(set ? static_cast<int64_t>(set->size()) : 0));
      } else if (key) {
                auto method = getPrototypeMethod(object, *key);
                if (method) {
                    auto bmRef = heap_.allocateBoundMethod(Value::makeHostFuncId(getHostFunctionIndex(host_function_names_[*method])), object);
                    pushStack(Value::makeBoundMethodId(bmRef.id));
        } else {
          pushStack(Value::makeNull());
        }
      } else {
        pushStack(Value::makeNull());
      }
      break;
    }

    if (!object.isObjectId()) {
      pushStack(Value::makeNull());
      break;
    }

	// _G globals mirror: delegate property access to the live globals maps
	if (object.asObjectId() == globals_mirror_object_id_) {
		// Numeric index on _G: index into sorted list of global keys
		if (key_value.isInt()) {
			std::vector<std::string> allKeys;
			std::set<std::string> seen;
			for (const auto& [name, _] : globals) {
				if (seen.insert(name).second) allKeys.push_back(name);
			}
			for (const auto& [name, _] : host_function_globals_) {
				if (seen.insert(name).second) allKeys.push_back(name);
			}
			int64_t index = key_value.asInt();
			if (index < 0) {
				index = static_cast<int64_t>(allKeys.size()) + index;
			}
			if (index >= 0 && static_cast<size_t>(index) < allKeys.size()) {
				const std::string& keyName = allKeys[static_cast<size_t>(index)];
				pushStack(Value::makeStringId(heap_.allocateString(keyName).id));
			} else {
				pushStack(Value::makeNull());
			}
			break;
		}
		auto key = resolveKey(key_value);
		if (!key) {
			COMPILER_THROW("OBJECT_GET expects string/number/bool key");
		}
		auto it = globals.find(*key);
		if (it != globals.end()) {
                if (it->second.isFunctionObjId() || it->second.isClosureId() || it->second.isHostFuncId()) {
                    auto bmRef = heap_.allocateBoundMethod(it->second, object);
                    pushStack(Value::makeBoundMethodId(bmRef.id));
			} else {
				pushStack(it->second);
			}
			break;
		}
		auto hostIt = host_function_globals_.find(*key);
		if (hostIt != host_function_globals_.end()) {
                if (hostIt->second.isFunctionObjId() || hostIt->second.isClosureId() || hostIt->second.isHostFuncId()) {
                    auto bmRef = heap_.allocateBoundMethod(hostIt->second, object);
                    pushStack(Value::makeBoundMethodId(bmRef.id));
                } else {
				pushStack(hostIt->second);
			}
			break;
		}
	pushStack(Value::makeNull());
			break;
		}

        auto objRef = ObjectRef{object.asObjectId(), true};
        auto *obj = heap_.object(objRef.id);
        if (!obj) {
            COMPILER_THROW("OBJECT_GET unknown object id");
	}

        // Check for numeric index (obj[0], obj[-1])
    if (key_value.isInt()) {
      int64_t index = key_value.asInt();
      auto keys = obj->getKeys();
      // Handle negative indices
      if (index < 0) {
        index = static_cast<int64_t>(keys.size()) + index;
      }
      if (index >= 0 && static_cast<size_t>(index) < keys.size()) {
        auto *val = obj->get(keys[static_cast<size_t>(index)]);
        if (val) {
          pushStack(*val);
        } else {
          pushStack(Value::makeNull());
        }
      } else {
        pushStack(Value::makeNull());
      }
      break;
    }

    auto key = resolveKey(key_value);
    if (!key) {
        COMPILER_THROW("OBJECT_GET expects string/number/bool key");
    }

    Value found_val = Value::makeNull();
    GCHeap::ObjectEntry *current_obj = obj;

    while (current_obj) {
      auto *val = current_obj->get(*key);
      if (val) {
        found_val = *val;
        break;
      }
      // Check prototypes: __proto first (Lua-style), then __class, __struct, __parent
      auto* parent_val = current_obj->get("__proto");
      if (!parent_val) parent_val = current_obj->get("__class");
      if (!parent_val) parent_val = current_obj->get("__struct");
      if (!parent_val) parent_val = current_obj->get("__parent");

      if (parent_val && parent_val->isObjectId()) {
        current_obj = heap_.object(parent_val->asObjectId());
      } else {
        current_obj = nullptr;
      }
    }

        if (!found_val.isNull()) {
            if (found_val.isHostFuncId()) {
                pushStack(found_val);
            } else if (found_val.isFunctionObjId() || found_val.isClosureId()) {
                auto boundObj = heap_.allocateObject();
                auto *bObj = heap_.object(boundObj.id);
                (*bObj)["fn"] = found_val;
                (*bObj)["self"] = object;
                pushStack(Value::makeObjectId(boundObj.id));
            } else {
                pushStack(found_val);
            }
    } else {
        // Built-in .len property returns key count for objects
        if (*key == "len") {
            auto keys = obj->getKeys();
            pushStack(Value::makeInt(static_cast<int64_t>(keys.size())));
        } else {
            // Check built-in prototype methods (for strings, arrays, etc.)
            auto method = getPrototypeMethod(object, *key);
            if (method) {
                auto boundObj = heap_.allocateObject();
                auto *bObj = heap_.object(boundObj.id);
                (*bObj)["fn"] = Value::makeHostFuncId(getHostFunctionIndex(host_function_names_[*method]));
                (*bObj)["self"] = object;
                pushStack(Value::makeObjectId(boundObj.id));
            } else {
                // Autovivification: if object has __vivify, auto-create sub-object
                auto* vivify = obj->get("__vivify");
                if (vivify && !vivify->isNull() && !vivify->isBool()) {
                    // Create a new sub-object with same __vivify marker
                    auto subRef = heap_.allocateObject();
                    auto *subObj = heap_.object(subRef.id);
                    (*subObj)["__vivify"] = *vivify;
                    // Also propagate __autosave_root if present
                    auto* autoSaveRoot = obj->get("__autosave_root");
                    if (autoSaveRoot) {
                        (*subObj)["__autosave_root"] = *autoSaveRoot;
                    }
                    // Propagate __cfg_path for nested path tracking
                    auto* parentPath = obj->get("__cfg_path");
                    std::string childPath;
                    if (parentPath && parentPath->isStringValId()) {
                        auto* parentStr = heap_.string(parentPath->asStringValId());
                        childPath = parentStr ? (*parentStr + "." + *key) : *key;
                    } else {
                        childPath = *key;
                    }
                    auto pathRef = heap_.allocateString(childPath);
                    (*subObj)["__cfg_path"] = Value::makeStringValId(pathRef.id);
                    // Store sub-object on parent so it persists
                    obj->set(*key, Value::makeObjectId(subRef.id));
                    pushStack(Value::makeObjectId(subRef.id));
                } else {
                    pushStack(Value::makeNull());
                }
            }
        }
    }
    break;
  }

  case OpCode::OBJECT_SET: {
    // Stack: [..., obj, value, key] → pops all, pushes obj
    // This allows chaining: obj { DUP, val1, "k1", SET, val2, "k2", SET, ... }
    Value key = popStack();
    Value value = popStack();
    Value object = popStack();

    auto keyStr = resolveKey(key);
    if (!keyStr) {
      COMPILER_THROW("OBJECT_SET expects string/number/bool key");
    }

        // Safety: reject __ prefixed keys (reserved for internal use)
        // Exception: __name__, __wrapped__, __arity__ are allowed on callable types
        // for decorator metadata preservation (Python convention)
        bool isCallable = object.isFunctionObjId() || object.isClosureId() || object.isHostFuncId() || object.isBoundMethodId();
        bool isAllowedDunder = *keyStr == "__name__" || *keyStr == "__wrapped__" || *keyStr == "__arity__";
        if (keyStr->size() >= 2 && (*keyStr)[0] == '_' && (*keyStr)[1] == '_' &&
            !(isCallable && isAllowedDunder)) {
            COMPILER_THROW(
                "OBJECT_SET: keys starting with '__' are reserved");
        }

    // Handle function objects - support fn.prop = value
    if (object.isFunctionObjId()) {
      uint32_t funcIdx = object.asFunctionObjId();
      
      // Get or create properties object for this function
      auto& propRef = function_properties_[funcIdx];
      if (propRef.id == 0) {
        propRef = heap_.allocateObject(true); // Create sorted object for properties
      }
      
      auto* props = heap_.object(propRef.id);
      if (!props) {
        COMPILER_THROW("OBJECT_SET: failed to get function properties object");
      }
        props->set(*keyStr, std::move(value));
        pushStack(object); // Return the function object for chaining
        break;
    }

    // Handle closure objects - support closure.prop = value
    if (object.isClosureId()) {
        uint32_t closureId = object.asClosureId();
        auto& propRef = closure_properties_[closureId];
        if (propRef.id == 0) {
            propRef = heap_.allocateObject(true);
        }
        auto* props = heap_.object(propRef.id);
        if (!props) {
            COMPILER_THROW("OBJECT_SET: failed to get closure properties object");
        }
        props->set(*keyStr, std::move(value));
        pushStack(object);
        break;
    }

    // Handle host function objects - support hostfn.prop = value
    if (object.isHostFuncId()) {
        uint32_t hostIdx = object.asHostFuncId();
        auto& propRef = hostfunc_properties_[hostIdx];
        if (propRef.id == 0) {
            propRef = heap_.allocateObject(true);
        }
        auto* props = heap_.object(propRef.id);
        if (!props) {
            COMPILER_THROW("OBJECT_SET: failed to get host func properties object");
        }
        props->set(*keyStr, std::move(value));
        pushStack(object);
        break;
    }

	if (!object.isObjectId()) {
		COMPILER_THROW("OBJECT_SET expects object container");
	}

        // _G globals mirror: writing to _G also updates the globals map
  if (object.asObjectId() == globals_mirror_object_id_) {
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_SET unknown object id");
    }
    obj->set(*keyStr, value);
    globals[*keyStr] = value;
    pushStack(object);
    break;
  }

  auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_SET unknown object id");
    }
    obj->set(*keyStr, value);

    // Auto-save: if object has __autosave_root, persist to config store
    auto* autoSaveRoot = obj->get("__autosave_root");
    if (autoSaveRoot && !autoSaveRoot->isNull()) {
        auto* cfgPathVal = obj->get("__cfg_path");
        if (cfgPathVal && cfgPathVal->isStringValId()) {
            auto* pathStr = heap_.string(cfgPathVal->asStringValId());
            if (pathStr) {
                std::string fullKey = *pathStr + "." + *keyStr;
                // Resolve value to string for config storage
                std::string valStr;
                if (value.isBool()) valStr = value.asBool() ? "true" : "false";
                else if (value.isInt()) valStr = std::to_string(value.asInt());
                else if (value.isDouble()) valStr = std::to_string(value.asDouble());
                else if (value.isStringValId()) {
                    auto* s = heap_.string(value.asStringValId());
                    valStr = s ? *s : "";
                } else if (value.isStringId()) {
                    auto* s = heap_.string(value.asStringId());
                    valStr = s ? *s : "";
                } else {
                    valStr = "";
                }
                
                if (!valStr.empty() || value.isBool() || value.isInt() || value.isDouble()) {
                    auto &config = Configs::Get();
                    config.Set(fullKey, valStr, true);
                    // havel::debug("VM Auto-save: {} = {}", fullKey, valStr);
                }
            }
        }
    }

    pushStack(object); // Return the object for chaining
    break;
  }

	case OpCode::OBJECT_GET_RAW: {
		Value key_value = popStack();
		Value object = popStack();

		if (!object.isObjectId()) {
			pushStack(Value::makeNull());
			break;
		}

		auto key = resolveKey(key_value);
		if (!key) {
			pushStack(Value::makeNull());
			break;
		}

		if (object.asObjectId() == globals_mirror_object_id_) {
			auto it = globals.find(*key);
			if (it != globals.end()) {
				pushStack(it->second);
				break;
			}
			auto hostIt = host_function_globals_.find(*key);
			if (hostIt != host_function_globals_.end()) {
				pushStack(hostIt->second);
				break;
			}
			pushStack(Value::makeNull());
			break;
		}

		GCHeap::ObjectEntry *current_obj = heap_.object(object.asObjectId());
		while (current_obj) {
			auto *val = current_obj->get(*key);
			if (val) {
				pushStack(*val);
				break;
			}
			auto* parent_val = current_obj->get("__class");
			if (!parent_val) parent_val = current_obj->get("__parent");
			if (parent_val && parent_val->isObjectId()) {
				current_obj = heap_.object(parent_val->asObjectId());
			} else {
				current_obj = nullptr;
			}
		}
		if (!current_obj) {
			pushStack(Value::makeNull());
		}
		break;
	}

  // Object intrinsics (VM-level operations)
  case OpCode::OBJECT_KEYS: {
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_KEYS expects object");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_KEYS unknown object id");
    }
    auto arrRef = heap_.allocateArray();
    auto *arr = heap_.array(arrRef.id);
    auto keys = obj->getKeys();
    for (const auto &key : keys) {
      // TODO: string pool registration
      arr->push_back(Value::makeNull());
    }
    pushStack(Value::makeArrayId(arrRef.id));
    break;
  }

  case OpCode::OBJECT_VALUES: {
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_VALUES expects object");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_VALUES unknown object id");
    }
    auto arrRef = heap_.allocateArray();
    auto *arr = heap_.array(arrRef.id);
    auto keys = obj->getKeys();
    for (const auto &key : keys) {
      auto *val = obj->get(key);
      if (val) {
        arr->push_back(*val);
      }
    }
    pushStack(Value::makeArrayId(arrRef.id));
    break;
  }

  case OpCode::OBJECT_ENTRIES: {
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_ENTRIES expects object");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_ENTRIES unknown object id");
    }
    auto arrRef = heap_.allocateArray();
    auto keys = obj->getKeys();
    for (const auto &key : keys) {
      auto *val = obj->get(key);
      if (val) {
        // Create [key, value] tuple as array
        auto tupleRef = heap_.allocateArray();
        auto *tuple = heap_.array(tupleRef.id);
        // TODO: string pool registration
        tuple->push_back(Value::makeNull());
        tuple->push_back(*val);
        auto *arr = heap_.array(arrRef.id);
        arr->push_back(Value::makeArrayId(tupleRef.id));
      }
    }
    pushStack(Value::makeArrayId(arrRef.id));
    break;
  }

  case OpCode::OBJECT_HAS: {
    Value keyValue = popStack();
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_HAS expects object");
    }
    auto key = resolveKey(keyValue);
    if (!key) {
      COMPILER_THROW("OBJECT_HAS expects string/number/bool key");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      pushStack(Value::makeBool(false));
    } else {
      pushStack(Value::makeBool(obj->get(*key) != nullptr));
    }
    break;
  }

  case OpCode::OBJECT_DELETE: {
    Value keyValue = popStack();
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_DELETE expects object");
    }
    auto key = resolveKey(keyValue);
    if (!key) {
      COMPILER_THROW("OBJECT_DELETE expects string/number/bool key");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      pushStack(Value::makeBool(false));
    } else {
      pushStack(Value::makeBool(obj->data.erase(*key) > 0));
    }
    break;
  }

  case OpCode::ARRAY_DEL: {
    Value keyValue = popStack();
    Value container = popStack();
    if (container.isArrayId()) {
      auto index = indexFromValue(keyValue);
      if (!index) {
        COMPILER_THROW("ARRAY_DEL expects integer index");
      }
      auto *array = heap_.array(container.asArrayId());
      if (!array) {
        COMPILER_THROW("ARRAY_DEL unknown array id");
      }
      int64_t idx = *index;
      if (idx < 0) idx = static_cast<int64_t>(array->size()) + idx;
      if (idx < 0 || static_cast<size_t>(idx) >= array->size()) {
        pushStack(Value::makeBool(false));
      } else {
        array->erase(array->begin() + static_cast<size_t>(idx));
        pushStack(Value::makeBool(true));
      }
    } else if (container.isSetId()) {
      auto *set = heap_.set(container.asSetId());
      if (!set) {
        COMPILER_THROW("ARRAY_DEL unknown set id");
      }
      auto key = resolveKey(keyValue);
      if (!key) {
        COMPILER_THROW("ARRAY_DEL expects string/number key for set");
      }
      pushStack(Value::makeBool(set->erase(*key) > 0));
    } else {
      COMPILER_THROW("ARRAY_DEL expects array/set container");
    }
    break;
  }

  case OpCode::SET_DEL: {
    Value keyValue = popStack();
    Value setVal = popStack();
    if (!setVal.isSetId()) {
      COMPILER_THROW("SET_DEL expects set container");
    }
    auto *set = heap_.set(setVal.asSetId());
    if (!set) {
      COMPILER_THROW("SET_DEL unknown set id");
    }
    auto key = resolveKey(keyValue);
    if (!key) {
      COMPILER_THROW("SET_DEL expects string/number key");
    }
    pushStack(Value::makeBool(set->erase(*key) > 0));
    break;
  }

  // Array intrinsics (VM-level operations)
  case OpCode::ARRAY_POP: {
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_POP expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeNull());
    } else if (arr->frozen) {
      COMPILER_THROW("Cannot modify frozen array (tuple)");
    } else if (arr->empty()) {
      pushStack(Value::makeNull());
    } else {
      pushStack(arr->back());
      arr->pop_back();
    }
    break;
  }

  case OpCode::ARRAY_HAS: {
    Value value = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_HAS expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeBool(false));
    } else {
      bool found = false;
      for (const auto &elem : *arr) {
        if (valuesEqual(elem, value)) {
          found = true;
          break;
        }
      }
      pushStack(Value::makeBool(found));
    }
    break;
  }

  case OpCode::ARRAY_FIND: {
    Value value = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_FIND expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeInt(-1));
    } else {
      int64_t foundIdx = -1;
      for (size_t i = 0; i < arr->size(); i++) {
        if (valuesEqual((*arr)[i], value)) {
          foundIdx = static_cast<int64_t>(i);
          break;
        }
      }
      pushStack(Value::makeInt(foundIdx));
    }
    break;
  }

  // Array higher-order functions (VM intrinsics)
  case OpCode::ARRAY_MAP: {
    Value fn = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_MAP expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeNull());
      break;
    }
    if (!fn.isFunctionObjId() && !fn.isClosureId()) {
      COMPILER_THROW("ARRAY_MAP expects function/closure");
    }

    auto resultRef = heap_.allocateArray();
    auto *result = heap_.array(resultRef.id);
    uint64_t resultRootId = pinExternalRoot(Value::makeArrayId(resultRef.id));

    for (size_t i = 0; i < arr->size(); i++) {
      Value mapped = callFunctionSync(fn, {(*arr)[i]});
      result->push_back(mapped);
    }

    unpinExternalRoot(resultRootId);
    pushStack(Value::makeArrayId(resultRef.id));
    break;
  }

  case OpCode::ARRAY_FILTER: {
    Value fn = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_FILTER expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeNull());
      break;
    }

    auto resultRef = heap_.allocateArray();
    auto *result = heap_.array(resultRef.id);
    uint64_t resultRootId = pinExternalRoot(Value::makeArrayId(resultRef.id));

    for (size_t i = 0; i < arr->size(); i++) {
      Value predResult = callFunctionSync(fn, {(*arr)[i]});
      if (predResult.isBool() && predResult.asBool()) {
        result->push_back((*arr)[i]);
      }
    }

    unpinExternalRoot(resultRootId);
    pushStack(Value::makeArrayId(resultRef.id));
    break;
  }

  case OpCode::ARRAY_REDUCE: {
    Value initial = popStack();
    Value fn = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_REDUCE expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(initial);
      break;
    }

    Value acc = initial;
    for (size_t i = 0; i < arr->size(); i++) {
      acc = callFunctionSync(fn, {acc, (*arr)[i]});
    }

    pushStack(acc);
    break;
  }

  case OpCode::ARRAY_FOREACH: {
    Value fn = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_FOREACH expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeNull());
      break;
    }

    for (size_t i = 0; i < arr->size(); i++) {
      (void)callFunctionSync(fn, {(*arr)[i]});
    }

    pushStack(Value::makeNull());
    break;
  }

  // String intrinsics (VM-level operations)
  case OpCode::STRING_LEN: {
    Value str = popStack();
    pushStack(Value::makeInt(static_cast<int64_t>(toString(str).size())));
    break;
  }

  case OpCode::STRING_UPPER: {
    Value str = popStack();
    std::string s = toString(str);
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    auto ref = createRuntimeString(std::move(s));
    pushStack(Value::makeStringId(ref.id));
    break;
  }

  case OpCode::STRING_LOWER: {
    Value str = popStack();
    std::string s = toString(str);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    auto ref = createRuntimeString(std::move(s));
    pushStack(Value::makeStringId(ref.id));
    break;
  }

  case OpCode::STRING_TRIM: {
    Value str = popStack();
    std::string s = toString(str);
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
      auto ref = createRuntimeString("");
      pushStack(Value::makeStringId(ref.id));
    } else {
      size_t end = s.find_last_not_of(" \t\n\r");
      auto ref = createRuntimeString(s.substr(start, end - start + 1));
      pushStack(Value::makeStringId(ref.id));
    }
    break;
  }

  case OpCode::STRING_HAS: {
    Value substr = popStack();
    Value str = popStack();
    if (!str.isStringValId() || !substr.isStringValId()) {
      COMPILER_THROW("STRING_HAS expects strings");
    }
    // TODO: string pool lookup
    const std::string s = "<string:" + std::to_string(str.asStringValId()) + ">";
    const std::string sub = "<string:" + std::to_string(substr.asStringValId()) + ">";
    pushStack(Value::makeBool(s.find(sub) != std::string::npos));
    break;
  }

  case OpCode::STRING_STARTS: {
    Value prefix = popStack();
    Value str = popStack();
    if (!str.isStringValId() || !prefix.isStringValId()) {
      COMPILER_THROW("STRING_STARTS expects strings");
    }
    // TODO: string pool lookup
    const std::string s = "<string:" + std::to_string(str.asStringValId()) + ">";
    const std::string pre = "<string:" + std::to_string(prefix.asStringValId()) + ">";
    pushStack(Value::makeBool(s.size() >= pre.size() &&
                       s.compare(0, pre.size(), pre) == 0));
    break;
  }

  case OpCode::STRING_ENDS: {
    Value suffix = popStack();
    Value str = popStack();
    if (!str.isStringValId() || !suffix.isStringValId()) {
      COMPILER_THROW("STRING_ENDS expects strings");
    }
    // TODO: string pool lookup
    const std::string s = "<string:" + std::to_string(str.asStringValId()) + ">";
    const std::string suf = "<string:" + std::to_string(suffix.asStringValId()) + ">";
    pushStack(Value::makeBool(s.size() >= suf.size() &&
                       s.compare(s.size() - suf.size(), suf.size(), suf) == 0));
    break;
  }

  // Spread operator - spread array elements
  case OpCode::SPREAD: {
    Value value = popStack();
    if (value.isArrayId()) {
      auto arrRef = ArrayRef{value.asArrayId()};
      auto *arr = heap_.array(arrRef.id);
      if (arr) {
        // Push each element individually
        for (auto &elem : *arr) {
          pushStack(elem);
        }
      }
    } else if (value.isStringValId()) {
      // Spread string into characters
      // TODO: string pool lookup
      std::string str = "<string:" + std::to_string(value.asStringValId()) + ">";
      auto arrRef = heap_.allocateArray();
      auto *arr = heap_.array(arrRef.id);
      if (arr) {
        for (char c : str) {
          // TODO: string pool registration
          arr->push_back(Value::makeNull());
        }
        pushStack(Value::makeArrayId(arrRef.id));
      }
    }
    break;
  }

  // Spread in function call
  case OpCode::SPREAD_CALL: {
    // Similar to SPREAD but marks arguments for spread in CALL
    Value value = popStack();
    if (value.isArrayId()) {
      auto arrRef = ArrayRef{value.asArrayId()};
      auto *arr = heap_.array(arrRef.id);
      if (arr) {
        for (auto &elem : *arr) {
          pushStack(elem);
        }
      }
    }
    break;
  }

  // Type conversion - as operator
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
    // TODO: string pool integration - return string ID instead of std::string
    // For now, return a placeholder integer
    pushStack(Value::makeInt(0));
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
  // CONCURRENCY PRIMITIVES
  // ============================================================================

case OpCode::THREAD_SPAWN: {
			Value func_val = popStack();
			if (!func_val.isClosureId() && !func_val.isFunctionObjId()) {
				COMPILER_THROW("THREAD_SPAWN expects a function");
			}
			Value result = invokeHostFunctionDirect("thread_spawn", {func_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("thread.spawn", {func_val});
			}
			if (result.isNull()) {
				uint32_t thread_id = heap_.allocateThread();
				result = Value::makeThreadId(thread_id);
			}
			pushStack(result);
			break;
		}

  case OpCode::THREAD_JOIN: {
    // Join thread and wait for completion (non-blocking via fiber suspension)
    // Phase 3B-7: Instead of blocking thread.join(), suspend the fiber
    // and enqueue a callback to unpark when thread completes
    Value thread_val = popStack();
    if (!thread_val.isThreadId()) {
      COMPILER_THROW("THREAD_JOIN expects a thread");
    }
    
    uint32_t thread_id = thread_val.asThreadId();
    
    // Set suspension request - signals executeOneStep to suspend the fiber
    // Context pointer carries the thread_id for the callback to use
    suspension_requested_ = true;
    suspension_reason_ = static_cast<uint8_t>(SuspensionReason::THREAD_JOIN);
    suspension_context_ = reinterpret_cast<void*>(static_cast<uintptr_t>(thread_id));
    
    // The actual suspension will happen in executeOneStep after this instruction
    // For now, push null as the return value (will be used if thread already done)
    pushStack(Value::makeNull());
    break;
  }

case OpCode::THREAD_SEND: {
			Value message = popStack();
			Value thread_val = popStack();
			if (!thread_val.isThreadId()) {
				COMPILER_THROW("THREAD_SEND expects a thread");
			}
			Value result = invokeHostFunctionDirect("thread_send", {thread_val, message});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("thread.send", {thread_val, message});
			}
			pushStack(result);
			break;
		}

		case OpCode::THREAD_RECEIVE: {
			Value thread_val = popStack();
			if (!thread_val.isThreadId()) {
				COMPILER_THROW("THREAD_RECEIVE expects a thread");
			}
			Value result = invokeHostFunctionDirect("thread_receive", {thread_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("thread.receive", {thread_val});
			}
			pushStack(result);
			break;
		}

case OpCode::INTERVAL_START: {
			Value func_val = popStack();
			Value duration_val = popStack();

			if (!func_val.isClosureId() && !func_val.isFunctionObjId()) {
				COMPILER_THROW("INTERVAL_START expects a function");
			}

			if (!duration_val.isInt()) {
				COMPILER_THROW("INTERVAL_START expects duration in milliseconds");
			}

			Value result = invokeHostFunctionDirect("interval_start", {duration_val, func_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("interval.start", {duration_val, func_val});
			}
			if (result.isNull()) {
				uint32_t interval_id = heap_.allocateInterval();
				result = Value::makeIntervalId(interval_id);
			}
			pushStack(result);
			break;
		}

		case OpCode::INTERVAL_STOP: {
			Value interval_val = popStack();

			if (!interval_val.isIntervalId()) {
				COMPILER_THROW("INTERVAL_STOP expects an interval");
			}

			Value result = invokeHostFunctionDirect("interval_stop", {interval_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("interval.stop", {interval_val});
			}
			pushStack(result);
			break;
		}

case OpCode::TIMEOUT_START: {
			Value func_val = popStack();
			Value delay_val = popStack();

			if (!func_val.isClosureId() && !func_val.isFunctionObjId()) {
				COMPILER_THROW("TIMEOUT_START expects a function");
			}

			if (!delay_val.isInt()) {
				COMPILER_THROW("TIMEOUT_START expects delay in milliseconds");
			}

			Value result = invokeHostFunctionDirect("timeout_start", {delay_val, func_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("timeout.start", {delay_val, func_val});
			}
			if (result.isNull()) {
				uint32_t timeout_id = heap_.allocateTimeout();
				result = Value::makeTimeoutId(timeout_id);
			}
			pushStack(result);
			break;
		}

		case OpCode::TIMEOUT_CANCEL: {
			Value timeout_val = popStack();

			if (!timeout_val.isTimeoutId()) {
				COMPILER_THROW("TIMEOUT_CANCEL expects a timeout");
			}

			Value result = invokeHostFunctionDirect("timeout_cancel", {timeout_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("timeout.cancel", {timeout_val});
			}
			pushStack(result);
			break;
		}

  // ============================================================================
  // COROUTINES
  // ============================================================================

    case OpCode::YIELD: {
        Value yield_value = Value::makeNull();
        if (!stack.empty()) {
            yield_value = popStack();
        }

            if (current_coroutine_id_ != UINT32_MAX) {
                auto *co = heap_.coroutine(current_coroutine_id_);
                if (co) {
                    co->ip = currentFrame().ip + 1;
                    co->locals = locals;

                    // Save coroutine's current stack
                    co->stack.clear();
                    {
                        std::vector<Value> tmp;
                        while (!stack.empty()) {
                            tmp.push_back(stack.top());
                            stack.pop();
                        }
                        for (auto it = tmp.rbegin(); it != tmp.rend(); ++it) {
                            co->stack.push_back(*it);
                        }
                    }

                    co->state = GCHeap::Coroutine::Waiting;

                    // Pop caller frame from the coroutine's caller stack
                    if (!co->caller_stack.empty()) {
                        auto &caller = co->caller_stack.back();
                        frame_count_ = caller.frame_count;
                        locals = caller.locals;
                        current_coroutine_id_ = caller.coroutine_id;

                        currentFrame().ip = caller.ip;

                        stack = std::stack<Value>();
                        for (auto it = caller.stack.begin(); it != caller.stack.end(); ++it) {
                            stack.push(*it);
                        }

                        co->caller_stack.pop_back();
                    }

                    pushStack(yield_value);
                    return;
                }
            }

        // Non-coroutine yield
        pushStack(yield_value);
        break;
}

    case OpCode::YIELD_RESUME: {
        // Resume yielded coroutine
        Value coroutine_val = popStack();

        if (!coroutine_val.isCoroutineId()) {
            COMPILER_THROW("YIELD_RESUME expects a coroutine");
        }

        uint32_t coroutine_id = coroutine_val.asCoroutineId();
        auto *co = heap_.coroutine(coroutine_id);

        if (!co) {
            COMPILER_THROW("YIELD_RESUME: coroutine not found");
        }

        if (co->state == GCHeap::Coroutine::Done) {
            pushStack(Value::makeNull());
            break;
        }

            // Push current caller's state onto the coroutine's caller stack
            {
                GCHeap::CallerFrame cf;
                cf.coroutine_id = current_coroutine_id_;
                cf.frame_count = frame_count_;
                cf.ip = currentFrame().ip + 1;
                cf.locals = locals;
                {
                    std::vector<Value> tmp;
                    while (!stack.empty()) {
                        tmp.push_back(stack.top());
                        stack.pop();
                    }
                    for (auto it = tmp.rbegin(); it != tmp.rend(); ++it) {
                        cf.stack.push_back(*it);
                    }
                }
                co->caller_stack.push_back(std::move(cf));
            }
            co->parent_locals_size = locals.size();

        // Switch to the coroutine
        current_coroutine_id_ = coroutine_id;

        // Restore coroutine's stack (stack[0]=bottom, [N-1]=top)
        stack = std::stack<Value>();
        for (auto it = co->stack.begin(); it != co->stack.end(); ++it) {
            stack.push(*it);
        }

        // Restore coroutine's locals
        locals = co->locals;

        // Restore coroutine's instruction pointer
        currentFrame().ip = co->ip;

        // Set state to Runnable
        co->state = GCHeap::Coroutine::Runnable;

        // Push yield values from last yield (these become the "send" values
        // passed to the coroutine on resume — currently empty for generators)
        for (const auto &val : co->yield_values) {
            pushStack(val);
        }
        co->yield_values.clear();

        break;
    }

case OpCode::GO_ASYNC: {
Value call_val = popStack();

if (!call_val.isClosureId() && !call_val.isFunctionObjId()) {
COMPILER_THROW("GO_ASYNC expects a function");
}

if (scheduler_) {
uint32_t gid = spawnGoroutine(call_val, {});
pushStack(Value::makeThreadId(gid));
} else {
pushStack(Value::makeNull());
}
break;
}

 case OpCode::FIBER_AWAIT: {
 Value awaitable = popStack();

 if (awaitable.isCoroutineId()) {
 uint32_t coId = awaitable.asCoroutineId();
 auto *co = heap_.coroutine(coId);

 if (!co) {
 pushStack(Value::makeNull());
 break;
 }

 if (co->state == GCHeap::Coroutine::Done) {
 Value result = co->stack.empty() ? Value::makeNull() : co->stack.back();
 pushStack(result);
 break;
 }

 struct VMState {
 std::stack<Value> stack;
 std::vector<Value> locals;
 size_t frame_count;
 std::vector<CallFrame> frame_arena;
 uint32_t current_coroutine_id;
 };

 VMState saved;
 saved.stack = stack;
 saved.locals = locals;
 saved.frame_count = frame_count_;
 saved.frame_arena = frame_arena_;
 saved.current_coroutine_id = current_coroutine_id_;

            current_coroutine_id_ = coId;
            {
                GCHeap::CallerFrame cf;
                cf.coroutine_id = saved.current_coroutine_id;
                cf.frame_count = frame_count_;
                cf.ip = 0;
                cf.locals = locals;
                co->caller_stack.push_back(std::move(cf));
            }
            locals = co->locals;

 const auto *chunk = current_chunk;
 const auto *func = chunk ? chunk->getFunction(co->function_index) : nullptr;
 if (!func) {
 stack = saved.stack;
 frame_count_ = saved.frame_count;
 locals = saved.locals;
 frame_arena_ = saved.frame_arena;
 current_coroutine_id_ = saved.current_coroutine_id;
 COMPILER_THROW("FIBER_AWAIT: function not found for coroutine");
 }

        uint32_t coroutine_stack_depth = static_cast<uint32_t>(stack.size());
    if (frame_arena_.size() <= frame_count_) {
        CallFrame cf;
        cf.function = func;
        cf.chunk = current_chunk;
        cf.ip = co->ip;
        cf.locals_base = 0;
        cf.closure_id = co->closure_id;
        cf.stack_depth = coroutine_stack_depth;
        frame_arena_.push_back(std::move(cf));
    } else {
        frame_arena_[frame_count_].function = func;
        frame_arena_[frame_count_].chunk = current_chunk;
        frame_arena_[frame_count_].ip = co->ip;
        frame_arena_[frame_count_].locals_base = 0;
        frame_arena_[frame_count_].closure_id = co->closure_id;
        frame_arena_[frame_count_].stack_depth = coroutine_stack_depth;
        }
        frame_count_++;
 co->state = GCHeap::Coroutine::Runnable;

 size_t caller_frame_count = saved.frame_count;
 size_t max_steps = 1000000;
 size_t steps = 0;

 while (co->state != GCHeap::Coroutine::Done && steps < max_steps) {
 if (frame_count_ <= caller_frame_count && co->state != GCHeap::Coroutine::Done) {
 // Coroutine yielded or slept — check if it's a timed sleep
 if (co->state == GCHeap::Coroutine::Waiting &&
 co->resume_at_time > std::chrono::steady_clock::time_point{}) {
 auto now = std::chrono::steady_clock::now();
 if (co->resume_at_time > now) {
 auto sleep_remaining = co->resume_at_time - now;
 while (std::chrono::steady_clock::now() < co->resume_at_time) {
 if (timer_check_func_) timer_check_func_();
 auto rem = co->resume_at_time - std::chrono::steady_clock::now();
 auto rem_ms = std::chrono::duration_cast<std::chrono::milliseconds>(rem);
 if (rem_ms.count() > 0) {
 std::this_thread::sleep_for(std::chrono::milliseconds(
 std::min(static_cast<int64_t>(1), rem_ms.count())));
 }
 }
 }
 // Resume coroutine after sleep
 co->state = GCHeap::Coroutine::Runnable;
 locals = co->locals;
 const auto *resume_chunk = current_chunk;
 const auto *resume_func = resume_chunk ? resume_chunk->getFunction(co->function_index) : nullptr;
        if (resume_func) {
            uint32_t resume_stack_depth = static_cast<uint32_t>(stack.size());
        if (frame_arena_.size() <= frame_count_) {
            CallFrame cf;
            cf.function = resume_func;
            cf.chunk = resume_chunk;
            cf.ip = co->ip;
            cf.locals_base = 0;
            cf.closure_id = co->closure_id;
            cf.stack_depth = resume_stack_depth;
            frame_arena_.push_back(std::move(cf));
        } else {
            frame_arena_[frame_count_].function = resume_func;
            frame_arena_[frame_count_].chunk = resume_chunk;
            frame_arena_[frame_count_].ip = co->ip;
                frame_arena_[frame_count_].locals_base = 0;
                frame_arena_[frame_count_].closure_id = co->closure_id;
                frame_arena_[frame_count_].stack_depth = resume_stack_depth;
            }
            frame_count_++;
 pushStack(Value::makeNull());
 }
 continue;
 }
 break;
 }

 if (frame_count_ <= caller_frame_count) {
 break;
 }

 size_t afi = frame_count_ - 1;
 const auto *cur_func = frame_arena_[afi].function;
 uint32_t cur_ip = frame_arena_[afi].ip;

 if (cur_ip >= cur_func->instructions.size()) {
 executeInstruction(Instruction{OpCode::RETURN});
 } else {
 const auto &inst = cur_func->instructions[cur_ip];
 executeInstruction(inst);
 if (afi < frame_count_ && frame_arena_[afi].ip == cur_ip) {
 frame_arena_[afi].ip++;
 }
 }

 processPendingCalls();
 steps++;

 if (suspension_requested_) {
 suspension_requested_ = false;
 break;
 }
 }

 Value result;
 if (co->state == GCHeap::Coroutine::Done && !stack.empty()) {
 result = popStack();
 } else if (co->state == GCHeap::Coroutine::Done) {
 result = co->stack.empty() ? Value::makeNull() : co->stack.back();
 } else if (co->state == GCHeap::Coroutine::Waiting && !stack.empty()) {
 result = popStack();
 } else {
 result = awaitable;
 }

 stack = saved.stack;
 frame_count_ = saved.frame_count;
 locals = saved.locals;
 frame_arena_ = saved.frame_arena;
 current_coroutine_id_ = saved.current_coroutine_id;

 pushStack(result);
 break;
 }

 // <- thread_id: join the thread, return its result
 if (awaitable.isThreadId()) {
 uint32_t tid = awaitable.asThreadId();
 auto *threadObj = heap_.thread(tid);
 if (threadObj) {
 threadObj->stop();
 }
 // Check if a result was stored for this thread
 auto it = thread_results_.find(tid);
 Value result = (it != thread_results_.end()) ? it->second : Value::makeNull();
 if (it != thread_results_.end()) thread_results_.erase(it);
 pushStack(result);
 break;
 }

 // <- interval_id: wait for next tick, return callback result
 if (awaitable.isIntervalId()) {
 uint32_t iid = awaitable.asIntervalId();
 // Wait for interval to produce a result by polling
 // In a real async VM, this would suspend the fiber.
 // For now, busy-wait with processPendingCalls
 while (interval_results_.find(iid) == interval_results_.end()) {
 if (timer_check_func_) timer_check_func_();
 std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 auto it = interval_results_.find(iid);
 Value result = it->second;
 interval_results_.erase(it);
 pushStack(result);
 break;
 }

 // <- timeout_id: wait for timeout to fire, return callback result
 if (awaitable.isTimeoutId()) {
 uint32_t tid = awaitable.asTimeoutId();
 auto *timeoutObj = heap_.timeout(tid);
 if (timeoutObj) {
 // The timeout is already scheduled. We need to wait for it.
 // Since Timeout::cancel() joins the thread, we can call it
 // to wait for completion. If it was already cancelled, this
 // is a no-op.
 timeoutObj->cancel();
 }
 auto it = timeout_results_.find(tid);
 Value result = (it != timeout_results_.end()) ? it->second : Value::makeNull();
 if (it != timeout_results_.end()) timeout_results_.erase(it);
 pushStack(result);
 break;
 }

 // <- object with __await_result field (simulated concurrency in tests)
 if (awaitable.isObjectId()) {
 auto objRef = ObjectRef{awaitable.asObjectId(), true};
 Value result = getHostObjectField(objRef, "__await_result");
 if (!result.isNull()) {
 pushStack(result);
 break;
 }
 }

 // Non-awaitable value: push through (identity for resolved values)
 pushStack(awaitable);
 break;
 }

 case OpCode::FIBER_SLEEP: {
 Value ms_val = popStack();
 int ms = toInt(ms_val);

 if (current_coroutine_id_ != UINT32_MAX) {
 // Inside a coroutine: yield with a resume time
 auto *co = heap_.coroutine(current_coroutine_id_);
 if (co && frame_count_ > 0) {
 co->ip = currentFrame().ip + 1;
 co->locals = locals;
 co->state = GCHeap::Coroutine::Waiting;
 co->resume_at_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);

 auto finished = frame_arena_[frame_count_ - 1];
 frame_count_--;

 closeFrameUpvalues(static_cast<uint32_t>(finished.locals_base),
 static_cast<uint32_t>(locals.size()));
 if (locals.size() >= finished.locals_base) {
 locals.resize(finished.locals_base);
 }

                    if (!co->caller_stack.empty()) {
                        auto &caller = co->caller_stack.back();
                        frame_count_ = caller.frame_count;
                        locals = caller.locals;
                        current_coroutine_id_ = caller.coroutine_id;
                        currentFrame().ip = caller.ip;
                        co->caller_stack.pop_back();
                    }

                pushStack(Value::makeNull());

                return;
 }
 }

  // Main fiber: suspend goroutine via scheduler if available,
  // otherwise fall back to blocking sleep
  if (scheduler_) {
    suspension_requested_ = true;
    suspension_reason_ = static_cast<uint8_t>(SuspensionReason::SLEEP);
    suspension_context_ = reinterpret_cast<void*>(
        static_cast<intptr_t>(ms));
    pushStack(Value::makeNull());
    break;
  }

  // No scheduler — blocking sleep with timer processing
  {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
  if (timer_check_func_) timer_check_func_();
  auto remaining = deadline - std::chrono::steady_clock::now();
  auto sleep_ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
  if (sleep_ms.count() > 0) {
  std::this_thread::sleep_for(std::chrono::milliseconds(std::min(static_cast<int64_t>(1), sleep_ms.count())));
  }
  }
  }
  pushStack(Value::makeNull());
  break;
 }

  // ============================================================================
  // CHANNELS
  // ============================================================================

case OpCode::CHANNEL_NEW: {
			Value result = invokeHostFunctionDirect("channel_new", {});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("channel.new", {});
			}
			if (result.isNull()) {
				ChannelRef channel_ref = heap_.allocateChannel();
				result = Value::makeChannelId(channel_ref.id);
			}
			pushStack(result);
			break;
		}

case OpCode::CHANNEL_SEND: {
			Value value = popStack();
			Value channel_val = popStack();

			if (!channel_val.isChannelId()) {
				COMPILER_THROW("CHANNEL_SEND expects a channel");
			}

			Value result = invokeHostFunctionDirect("channel_send", {channel_val, value});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("channel.send", {channel_val, value});
			}
			pushStack(result);
			break;
		}

		case OpCode::CHANNEL_RECEIVE: {
			Value channel_val = popStack();

			if (!channel_val.isChannelId()) {
				COMPILER_THROW("CHANNEL_RECEIVE expects a channel");
			}

			Value result = invokeHostFunctionDirect("channel_receive", {channel_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("channel.receive", {channel_val});
			}
			pushStack(result);
			break;
		}

		case OpCode::CHANNEL_CLOSE: {
			Value channel_val = popStack();

			if (!channel_val.isChannelId()) {
				COMPILER_THROW("CHANNEL_CLOSE expects a channel");
			}

			Value result = invokeHostFunctionDirect("channel_close", {channel_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("channel.close", {channel_val});
			}
			pushStack(result);
			break;
		}

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
    COMPILER_THROW(
        "Unknown opcode: " +
        std::to_string(static_cast<int>(instruction.opcode)));
  }
}


} // namespace havel::compiler
