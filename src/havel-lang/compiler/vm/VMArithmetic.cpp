#include "VM.hpp"
#include "VMInternals.hpp"
#include "../../../utils/Logger.hpp"
#include "../../utils/ErrorPrinter.hpp"
#include "../runtime/HostBridge.hpp"
#include "../runtime/RuntimeSupport.hpp"

#include <cmath>
#include <iostream>

namespace havel::compiler {

void VM::execBinaryOp(const Instruction &instruction) {
	Value right = popStack();
	Value left = popStack();

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
		} else if (left.isCoroutineId() && right.isCoroutineId()) {
			identical = left.asCoroutineId() == right.asCoroutineId();
		} else if (left.isThreadId() && right.isThreadId()) {
			identical = left.asThreadId() == right.asThreadId();
		} else if (left.isChannelId() && right.isChannelId()) {
			identical = left.asChannelId() == right.asChannelId();
		} else if (left.isIteratorId() && right.isIteratorId()) {
			identical = left.asIteratorId() == right.asIteratorId();
		} else if (left.isBoundMethodId() && right.isBoundMethodId()) {
			identical = left.asBoundMethodId() == right.asBoundMethodId();
		} else if (left.isErrorId() && right.isErrorId()) {
			identical = left.asErrorId() == right.asErrorId();
		} else if (left.isSetId() && right.isSetId()) {
			identical = left.asSetId() == right.asSetId();
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
			pushStack(l % r);
			break;
		case OpCode::MOD:
			if (r == 0) COMPILER_THROW("Modulo by zero");
			{
				int64_t result = l % r;
				if (result != 0 && ((result < 0) != (r < 0))) result += r;
				pushStack(result);
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
		case OpCode::BIT_LSH: {
			uint64_t shift = static_cast<uint64_t>(r) & 63;
			pushStack(l << shift);
			break;
		}
		case OpCode::BIT_RSH: {
			uint64_t shift = static_cast<uint64_t>(r) & 63;
			pushStack(l >> shift);
			break;
		}
		default: COMPILER_THROW("Unsupported integer operation");
		}
		return;
	}

	if ((left.isInt() || left.isDouble()) &&
		(right.isInt() || right.isDouble())) {
		double l = left.isInt() ? static_cast<double>(left.asInt()) : left.asDouble();
		double r = right.isInt() ? static_cast<double>(right.asInt()) : right.asDouble();
		switch (instruction.opcode) {
		case OpCode::ADD: pushStack(l + r); break;
		case OpCode::SUB: pushStack(l - r); break;
		case OpCode::MUL: pushStack(l * r); break;
		case OpCode::DIV:
			if (r == 0.0) COMPILER_THROW("Division by zero");
			pushStack(l / r);
			break;
		case OpCode::INT_DIV: {
			int64_t divisor = static_cast<int64_t>(r);
			if (divisor == 0) COMPILER_THROW("Division by zero");
			pushStack(static_cast<int64_t>(l) / divisor);
			break;
		}
		case OpCode::DIVMOD:
		{
			int64_t ir = static_cast<int64_t>(r);
			if (ir == 0) COMPILER_THROW("Division by zero");
			int64_t il = static_cast<int64_t>(l);
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
		{
			int64_t divisor = static_cast<int64_t>(r);
			if (divisor == 0) COMPILER_THROW("Division by zero");
			pushStack(static_cast<int64_t>(l) % divisor);
		}
		break;
		case OpCode::MOD:
			if (r == 0.0) COMPILER_THROW("Modulo by zero");
			{
				double m = std::fmod(l, r);
				if (m != 0.0 && ((m < 0.0) != (r < 0.0))) m += r;
				pushStack(m);
			}
			break;
		case OpCode::POW: pushStack(std::pow(l, r)); break;
		case OpCode::EQ: pushStack(l == r); break;
		case OpCode::NEQ: pushStack(l != r); break;
		case OpCode::LT: pushStack(l < r); break;
		case OpCode::LTE: pushStack(l <= r); break;
		case OpCode::GT: pushStack(l > r); break;
		case OpCode::GTE: pushStack(l >= r); break;
		case OpCode::BIT_AND: pushStack(static_cast<int64_t>(l) & static_cast<int64_t>(r)); break;
		case OpCode::BIT_OR: pushStack(static_cast<int64_t>(l) | static_cast<int64_t>(r)); break;
		case OpCode::BIT_XOR: pushStack(static_cast<int64_t>(l) ^ static_cast<int64_t>(r)); break;
		case OpCode::BIT_LSH: pushStack(static_cast<int64_t>(l) << (static_cast<uint64_t>(r) & 63)); break;
		case OpCode::BIT_RSH: pushStack(static_cast<int64_t>(l) >> (static_cast<uint64_t>(r) & 63)); break;
		default: COMPILER_THROW("Unsupported floating point operation");
		}
		return;
	}

	if (left.isStringValId() || left.isStringId() || right.isStringValId() || right.isStringId()) {
		std::string l;
		if (left.isStringValId()) {
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

	if (left.isSetId() || right.isSetId()) {
		switch (instruction.opcode) {
		case OpCode::ADD: {
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

	if (left.isObjectId() || right.isObjectId()) {
		switch (instruction.opcode) {
		case OpCode::ADD: {
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

	auto &frame = currentFrame();
	if (frame.ip < frame.function->type_feedback.size()) {
		auto &fb = frame.function->type_feedback[frame.ip];
		fb.execution_count++;
		fb.left_type_mask |= getFeedbackMask(left);
		fb.right_type_mask |= getFeedbackMask(right);
	}
	switch (opcode) {
	case OpCode::AND: pushStack(isTruthy(left) && isTruthy(right)); break;
	case OpCode::OR: pushStack(isTruthy(left) || isTruthy(right)); break;
	default: COMPILER_THROW("Unknown logical opcode");
	}
}

void VM::execNegate() {
	Value value = popStack();

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

} // namespace havel::compiler
