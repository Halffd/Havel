#include "BytecodeBuilderModule.hpp"
#include "havel-lang/compiler/core/BytecodeIR.hpp"
#include "havel-lang/compiler/runtime/RuntimeSupport.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include <fstream>

using havel::compiler::BytecodeChunk;
using havel::compiler::BytecodeFunction;
using havel::compiler::Instruction;
using havel::compiler::OpCode;
using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

struct BuilderState {
    std::unique_ptr<BytecodeChunk> chunk;
    int32_t current_func_idx = -1;
    std::vector<int32_t> saved_func_stack;
    std::unordered_map<std::string, OpCode> opcode_map;

	BytecodeFunction *currentFunc() {
		if (current_func_idx < 0) return nullptr;
		return chunk->getFunctionMutable(static_cast<uint32_t>(current_func_idx));
	}

	BuilderState() {
		chunk = std::make_unique<BytecodeChunk>();
		opcode_map = {
		{"LOAD_CONST", OpCode::LOAD_CONST},
		{"LOAD_GLOBAL", OpCode::LOAD_GLOBAL},
		{"STORE_GLOBAL", OpCode::STORE_GLOBAL},
		{"STORE_IMMUT_GLOBAL", OpCode::STORE_IMMUT_GLOBAL},
		{"LOAD_VAR", OpCode::LOAD_VAR},
		{"STORE_VAR", OpCode::STORE_VAR},
		{"STORE_IMMUT_VAR", OpCode::STORE_IMMUT_VAR},
		{"LOAD_UPVALUE", OpCode::LOAD_UPVALUE},
		{"STORE_UPVALUE", OpCode::STORE_UPVALUE},
		{"POP", OpCode::POP},
		{"DUP", OpCode::DUP},
		{"SWAP", OpCode::SWAP},
		{"PUSH_NULL", OpCode::PUSH_NULL},
		{"ADD", OpCode::ADD},
		{"SUB", OpCode::SUB},
		{"MUL", OpCode::MUL},
		{"DIV", OpCode::DIV},
		{"INT_DIV", OpCode::INT_DIV},
		{"MOD", OpCode::MOD},
		{"POW", OpCode::POW},
		{"NEGATE", OpCode::NEGATE},
		{"NOT", OpCode::NOT},
		{"AND", OpCode::AND},
		{"OR", OpCode::OR},
		{"EQ", OpCode::EQ},
		{"NEQ", OpCode::NEQ},
		{"IS", OpCode::IS},
		{"LT", OpCode::LT},
		{"LTE", OpCode::LTE},
		{"GT", OpCode::GT},
		{"GTE", OpCode::GTE},
		{"IS_NULL", OpCode::IS_NULL},
		{"BIT_AND", OpCode::BIT_AND},
		{"BIT_OR", OpCode::BIT_OR},
		{"BIT_XOR", OpCode::BIT_XOR},
		{"BIT_LSH", OpCode::BIT_LSH},
		{"BIT_RSH", OpCode::BIT_RSH},
		{"BIT_NOT", OpCode::BIT_NOT},
		{"LENGTH", OpCode::LENGTH},
		{"JUMP", OpCode::JUMP},
		{"JUMP_IF_FALSE", OpCode::JUMP_IF_FALSE},
		{"JUMP_IF_TRUE", OpCode::JUMP_IF_TRUE},
		{"JUMP_IF_NULL", OpCode::JUMP_IF_NULL},
		{"CALL", OpCode::CALL},
		{"TAIL_CALL", OpCode::TAIL_CALL},
		{"CALL_METHOD", OpCode::CALL_METHOD},
		{"RETURN", OpCode::RETURN},
		{"DEFINE_FUNC", OpCode::DEFINE_FUNC},
		{"CLOSURE", OpCode::CLOSURE},
		{"PRINT", OpCode::PRINT},
		{"OBJECT_NEW", OpCode::OBJECT_NEW},
		{"OBJECT_GET", OpCode::OBJECT_GET},
		{"OBJECT_SET", OpCode::OBJECT_SET},
		{"ARRAY_NEW", OpCode::ARRAY_NEW},
		{"ARRAY_PUSH", OpCode::ARRAY_PUSH},
		{"ARRAY_GET", OpCode::ARRAY_GET},
		{"ARRAY_SET", OpCode::ARRAY_SET},
		{"ARRAY_LEN", OpCode::ARRAY_LEN},
		{"ARRAY_FREEZE", OpCode::ARRAY_FREEZE},
		{"SET_NEW", OpCode::SET_NEW},
		{"RANGE_NEW", OpCode::RANGE_NEW},
		{"ENUM_NEW", OpCode::ENUM_NEW},
		{"ENUM_TAG", OpCode::ENUM_TAG},
		{"ENUM_PAYLOAD", OpCode::ENUM_PAYLOAD},
		{"ITER_NEW", OpCode::ITER_NEW},
		{"ITER_NEXT", OpCode::ITER_NEXT},
		{"STRING_LEN", OpCode::STRING_LEN},
		{"STRING_CONCAT", OpCode::STRING_CONCAT},
		{"SPREAD", OpCode::SPREAD},
		{"TO_INT", OpCode::TO_INT},
		{"TO_FLOAT", OpCode::TO_FLOAT},
		{"TO_STRING", OpCode::TO_STRING},
		{"TO_BOOL", OpCode::TO_BOOL},
		{"TYPE_OF", OpCode::TYPE_OF},
		{"AS_TYPE", OpCode::AS_TYPE},
		{"CLASS_NEW", OpCode::CLASS_NEW},
		{"CLASS_GET_FIELD", OpCode::CLASS_GET_FIELD},
		{"CLASS_SET_FIELD", OpCode::CLASS_SET_FIELD},
		{"CALL_SUPER", OpCode::CALL_SUPER},
		{"STRUCT_NEW", OpCode::STRUCT_NEW},
		{"STRUCT_GET", OpCode::STRUCT_GET},
		{"STRUCT_SET", OpCode::STRUCT_SET},
		{"IMPORT", OpCode::IMPORT},
		{"YIELD", OpCode::YIELD},
		{"THROW", OpCode::THROW},
		{"TRY_ENTER", OpCode::TRY_ENTER},
		{"TRY_EXIT", OpCode::TRY_EXIT},
		{"LOAD_EXCEPTION", OpCode::LOAD_EXCEPTION},
		{"INCLOCAL", OpCode::INCLOCAL},
		{"DECLOCAL", OpCode::DECLOCAL},
		{"INCLOCAL_POST", OpCode::INCLOCAL_POST},
		{"DECLOCAL_POST", OpCode::DECLOCAL_POST},
        {"NOP", OpCode::NOP},
        {"JUMP_IF_NULL", OpCode::JUMP_IF_NULL},
        {"SET_SET", OpCode::SET_SET},
        {"SET_DEL", OpCode::SET_DEL},
        {"RANGE_STEP_NEW", OpCode::RANGE_STEP_NEW},
        {"STRING_PROMOTE", OpCode::STRING_PROMOTE},
        {"STRING_SUB", OpCode::STRING_SUB},
        {"STRING_HAS", OpCode::STRING_HAS},
        {"STRING_FIND", OpCode::STRING_FIND},
        {"STRING_SPLIT", OpCode::STRING_SPLIT},
        {"STRING_REPLACE", OpCode::STRING_REPLACE},
        {"STRING_TRIM", OpCode::STRING_TRIM},
        {"STRING_LOWER", OpCode::STRING_LOWER},
        {"STRING_UPPER", OpCode::STRING_UPPER},
        {"STRING_STARTS", OpCode::STRING_STARTS},
        {"STRING_ENDS", OpCode::STRING_ENDS},
        {"ARRAY_POP", OpCode::ARRAY_POP},
        {"ARRAY_DEL", OpCode::ARRAY_DEL},
        {"ARRAY_FIND", OpCode::ARRAY_FIND},
        {"ARRAY_HAS", OpCode::ARRAY_HAS},
        {"ARRAY_MAP", OpCode::ARRAY_MAP},
        {"ARRAY_FILTER", OpCode::ARRAY_FILTER},
        {"ARRAY_FOREACH", OpCode::ARRAY_FOREACH},
        {"ARRAY_REDUCE", OpCode::ARRAY_REDUCE},
        {"OBJECT_KEYS", OpCode::OBJECT_KEYS},
        {"OBJECT_VALUES", OpCode::OBJECT_VALUES},
        {"OBJECT_HAS", OpCode::OBJECT_HAS},
        {"OBJECT_DELETE", OpCode::OBJECT_DELETE},
        {"OBJECT_ENTRIES", OpCode::OBJECT_ENTRIES},
        {"OBJECT_GET_RAW", OpCode::OBJECT_GET_RAW},
        {"ENUM_MATCH", OpCode::ENUM_MATCH},
        {"CHANNEL_NEW", OpCode::CHANNEL_NEW},
        {"CHANNEL_SEND", OpCode::CHANNEL_SEND},
        {"CHANNEL_RECEIVE", OpCode::CHANNEL_RECEIVE},
        {"CHANNEL_CLOSE", OpCode::CHANNEL_CLOSE},
        {"THREAD_SPAWN", OpCode::THREAD_SPAWN},
        {"THREAD_JOIN", OpCode::THREAD_JOIN},
        {"THREAD_SEND", OpCode::THREAD_SEND},
        {"THREAD_RECEIVE", OpCode::THREAD_RECEIVE},
        {"FIBER_AWAIT", OpCode::FIBER_AWAIT},
        {"FIBER_SLEEP", OpCode::FIBER_SLEEP},
        {"GO_ASYNC", OpCode::GO_ASYNC},
        {"YIELD_RESUME", OpCode::YIELD_RESUME},
        {"LOAD_CLASS_PROTO", OpCode::LOAD_CLASS_PROTO},
        {"PROT_CAST", OpCode::PROT_CAST},
        {"PROT_CHECK", OpCode::PROT_CHECK},
        {"IMPORT_WILDCARD", OpCode::IMPORT_WILDCARD},
        {"ADD_ASSIGN", OpCode::ADD_ASSIGN},
        {"SUB_ASSIGN", OpCode::SUB_ASSIGN},
        {"MUL_ASSIGN", OpCode::MUL_ASSIGN},
        {"DIV_ASSIGN", OpCode::DIV_ASSIGN},
        {"MOD_ASSIGN", OpCode::MOD_ASSIGN},
        {"INT_DIV_ASSIGN", OpCode::INT_DIV_ASSIGN},
        {"POW_ASSIGN", OpCode::POW_ASSIGN},
        {"BITWISE_AND_ASSIGN", OpCode::BITWISE_AND_ASSIGN},
        {"BITWISE_OR_ASSIGN", OpCode::BITWISE_OR_ASSIGN},
        {"BITWISE_XOR_ASSIGN", OpCode::BITWISE_XOR_ASSIGN},
        {"SHIFT_LEFT_ASSIGN", OpCode::SHIFT_LEFT_ASSIGN},
        {"SHIFT_RIGHT_ASSIGN", OpCode::SHIFT_RIGHT_ASSIGN},
        {"REMAINDER", OpCode::REMAINDER},
        {"REMAINDER_ASSIGN", OpCode::REMAINDER_ASSIGN},
        {"DIVMOD", OpCode::DIVMOD},
        {"OBJECT_NEW_UNSORTED", OpCode::OBJECT_NEW_UNSORTED},
        {"SPREAD_CALL", OpCode::SPREAD_CALL},
        {"DEBUG", OpCode::DEBUG},
        {"BEGIN_MODULE", OpCode::BEGIN_MODULE},
        {"END_MODULE", OpCode::END_MODULE},
    };
}
};

static BuilderState g_builder;

static OpCode parseOpcode(const std::string &name) {
	auto it = g_builder.opcode_map.find(name);
	if (it == g_builder.opcode_map.end()) {
		throw std::runtime_error("bc: unknown opcode: " + name);
	}
	return it->second;
}

void registerBytecodeBuilderModule(const VMApi &api) {
	api.registerFunction("bc.reset", [](const std::vector<Value> &) -> Value {
		g_builder = BuilderState();
		return Value::makeNull();
	});

	api.registerFunction("bc.func_new", [api](const std::vector<Value> &args) -> Value {
    if (args.size() < 1 || (!args[0].isStringId() && !args[0].isStringValId())) {
        throw std::runtime_error("bc.func_new: requires name (string)");
    }
    std::string name = api.vm().toString(args[0]);
		uint32_t params = 0;
		uint32_t locals = 0;
		if (args.size() > 1 && args[1].isInt()) params = static_cast<uint32_t>(args[1].asInt());
		if (args.size() > 2 && args[2].isInt()) locals = static_cast<uint32_t>(args[2].asInt());

		BytecodeFunction func(name, params, locals);
		g_builder.chunk->addFunction(std::move(func));
		g_builder.current_func_idx = static_cast<int32_t>(g_builder.chunk->getFunctionCount() - 1);
		return Value::makeInt(static_cast<int64_t>(g_builder.current_func_idx));
    });

    api.registerFunction("bc.func_push", [](const std::vector<Value> &args) -> Value {
        g_builder.saved_func_stack.push_back(g_builder.current_func_idx);
        g_builder.current_func_idx = -1;
        return Value::makeInt(0);
    });

    api.registerFunction("bc.func_pop", [](const std::vector<Value> &args) -> Value {
        if (g_builder.saved_func_stack.empty()) {
            throw std::runtime_error("bc.func_pop: no saved function context");
        }
        g_builder.current_func_idx = g_builder.saved_func_stack.back();
        g_builder.saved_func_stack.pop_back();
        return Value::makeInt(static_cast<int64_t>(g_builder.current_func_idx));
    });

    api.registerFunction("bc.emit", [api](const std::vector<Value> &args) -> Value {
        auto *fn = g_builder.currentFunc();
        if (!fn) throw std::runtime_error("bc.emit: no current function");
    if (args.empty() || (!args[0].isStringId() && !args[0].isStringValId())) {
        throw std::runtime_error("bc.emit: requires opcode name (string)");
    }
    auto opName = api.resolveString(args[0]);
		OpCode op = parseOpcode(opName);

		std::vector<Value> operands;
		for (size_t i = 1; i < args.size(); ++i) {
			operands.push_back(args[i]);
		}
		fn->instructions.emplace_back(op, std::move(operands));
		auto ip = fn->instructions.size() - 1;
		return Value::makeInt(static_cast<int64_t>(ip));
	});

api.registerFunction("bc.add_const", [](const std::vector<Value> &args) -> Value {
    auto *fn = g_builder.currentFunc();
    if (!fn) throw std::runtime_error("bc.add_const: no current function");
    if (args.empty()) throw std::runtime_error("bc.add_const: requires value");
    auto &consts = fn->constants;
    for (uint32_t i = 0; i < consts.size(); ++i) {
        if (consts[i] == args[0]) return Value::makeInt(static_cast<int64_t>(i));
    }
    uint32_t idx = static_cast<uint32_t>(consts.size());
    consts.push_back(args[0]);
    return Value::makeInt(static_cast<int64_t>(idx));
});

api.registerFunction("bc.add_string", [api](const std::vector<Value> &args) -> Value {
        auto *fn = g_builder.currentFunc();
        if (!fn) throw std::runtime_error("bc.add_string: no current function");
        if (args.empty() || (!args[0].isStringId() && !args[0].isStringValId())) {
            throw std::runtime_error("bc.add_string: requires string value");
        }
        auto resolved = api.resolveString(args[0]);
        uint32_t strId = g_builder.chunk->addString(resolved);
        Value constVal = Value::makeStringValId(strId);
        auto &consts = fn->constants;
        for (uint32_t i = 0; i < consts.size(); ++i) {
            if (consts[i] == constVal) return Value::makeInt(static_cast<int64_t>(i));
        }
        uint32_t idx = static_cast<uint32_t>(consts.size());
        consts.push_back(constVal);
        return Value::makeInt(static_cast<int64_t>(idx));
    });

	api.registerFunction("bc.add_chunk_string", [api](const std::vector<Value> &args) -> Value {
    if (args.empty() || (!args[0].isStringId() && !args[0].isStringValId())) {
        throw std::runtime_error("bc.add_chunk_string: requires string");
    }
    auto str = api.resolveString(args[0]);
		auto idx = g_builder.chunk->addString(str);
		return Value::makeInt(static_cast<int64_t>(idx));
	});

	api.registerFunction("bc.patch_jump", [](const std::vector<Value> &args) -> Value {
		auto *fn = g_builder.currentFunc();
		if (!fn) throw std::runtime_error("bc.patch_jump: no current function");
		if (args.size() < 2 || !args[0].isInt() || !args[1].isInt()) {
			throw std::runtime_error("bc.patch_jump: requires (jump_ip, target_ip)");
		}
		uint32_t jumpIp = static_cast<uint32_t>(args[0].asInt());
		uint32_t targetIp = static_cast<uint32_t>(args[1].asInt());
		auto &instrs = fn->instructions;
		if (jumpIp >= instrs.size()) {
			throw std::runtime_error("bc.patch_jump: jump_ip out of range");
		}
		if (instrs[jumpIp].operands.empty()) {
			instrs[jumpIp].operands.push_back(Value::makeInt(static_cast<int64_t>(targetIp)));
		} else {
			instrs[jumpIp].operands[0] = Value::makeInt(static_cast<int64_t>(targetIp));
		}
		return Value::makeNull();
	});

	api.registerFunction("bc.set_local_count", [](const std::vector<Value> &args) -> Value {
		auto *fn = g_builder.currentFunc();
		if (!fn) throw std::runtime_error("bc.set_local_count: no current function");
		if (args.empty() || !args[0].isInt()) {
			throw std::runtime_error("bc.set_local_count: requires count (int)");
		}
		fn->local_count = static_cast<uint32_t>(args[0].asInt());
		return Value::makeNull();
	});

api.registerFunction("bc.set_param_count", [](const std::vector<Value> &args) -> Value {
    auto *fn = g_builder.currentFunc();
    if (!fn) throw std::runtime_error("bc.set_param_count: no current function");
    if (args.empty() || !args[0].isInt()) {
        throw std::runtime_error("bc.set_param_count: requires count (int)");
    }
    fn->param_count = static_cast<uint32_t>(args[0].asInt());
    return Value::makeNull();
});

api.registerFunction("bc.str_id", [](const std::vector<Value> &args) -> Value {
    if (args.empty() || !args[0].isInt()) {
        throw std::runtime_error("bc.str_id: requires chunk string index (int)");
    }
    return Value::makeStringValId(static_cast<uint32_t>(args[0].asInt()));
});

	api.registerFunction("bc.add_upvalue", [](const std::vector<Value> &args) -> Value {
		auto *fn = g_builder.currentFunc();
		if (!fn) throw std::runtime_error("bc.add_upvalue: no current function");
		if (args.size() < 2 || !args[0].isInt() || !args[1].isInt()) {
			throw std::runtime_error("bc.add_upvalue: requires (index, captures_local)");
		}
		havel::compiler::UpvalueDescriptor desc;
		desc.index = static_cast<uint32_t>(args[0].asInt());
		desc.captures_local = args[1].asInt() != 0;
		fn->upvalues.push_back(desc);
		return Value::makeInt(static_cast<int64_t>(fn->upvalues.size() - 1));
	});

api.registerFunction("bc.execute", [api](const std::vector<Value> &args) -> Value {
    if (g_builder.chunk->getFunctionCount() == 0) {
        throw std::runtime_error("bc.execute: no functions in chunk");
    }
    std::string entry = "__main__";
    if (!args.empty() && (args[0].isStringId() || args[0].isStringValId())) {
        entry = api.resolveString(args[0]);
    }

    std::vector<Value> runArgs;
    for (size_t i = 1; i < args.size(); ++i) {
        runArgs.push_back(args[i]);
    }

    auto &vm = api.vm();
    auto saved_chunk = vm.current_chunk;
    auto saved_frame_count = vm.frame_count_;
    auto saved_frame_arena = vm.frame_arena_;
    std::stack<Value> saved_stack = vm.stack;
    auto saved_locals = vm.locals;
    auto saved_main_chunk = vm.getMainChunk();

    // Set main_chunk_ to the inner chunk so CLOSURE doesn't snapshot globals
    // (same-chunk closures should share the globals namespace, not capture a snapshot)
    // Use aliasing shared_ptr: shares ownership with saved_main_chunk but points to inner chunk
    auto inner_chunk_ptr = std::shared_ptr<BytecodeChunk>(saved_main_chunk, g_builder.chunk.get());
    vm.setMainChunkShared(inner_chunk_ptr);
vm.current_chunk = g_builder.chunk.get();

    try {
        auto result = vm.execute(*g_builder.chunk, entry, runArgs);

        vm.current_chunk = saved_chunk;
    vm.frame_count_ = saved_frame_count;
    vm.frame_arena_ = std::move(saved_frame_arena);
    vm.stack = std::move(saved_stack);
    vm.locals = std::move(saved_locals);
    vm.setMainChunkShared(saved_main_chunk);
    return result;
} catch (const std::exception &e) {
    vm.current_chunk = saved_chunk;
    vm.frame_count_ = saved_frame_count;
    vm.frame_arena_ = std::move(saved_frame_arena);
    vm.stack = std::move(saved_stack);
    vm.locals = std::move(saved_locals);
    vm.setMainChunkShared(saved_main_chunk);
    throw std::runtime_error(std::string("Bytecode error: ") + e.what());
}
});

api.registerFunction("bc.execute_persistent", [api](const std::vector<Value> &args) -> Value {
    if (g_builder.chunk->getFunctionCount() == 0) {
        throw std::runtime_error("bc.execute_persistent: no functions in chunk");
    }
    std::string entry = "__main__";
    if (!args.empty() && (args[0].isStringId() || args[0].isStringValId())) {
        entry = api.resolveString(args[0]);
    }

    std::vector<Value> runArgs;
    for (size_t i = 1; i < args.size(); ++i) {
        runArgs.push_back(args[i]);
    }

  auto &vm = api.vm();
  auto saved_chunk = vm.current_chunk;
  auto saved_frame_count = vm.frame_count_;
  auto saved_frame_arena = vm.frame_arena_;
  std::stack<Value> saved_stack = vm.stack;
  auto saved_locals = vm.locals;
  auto saved_immutable_locals = vm.immutable_locals_;
  auto saved_main_chunk = vm.getMainChunk();

  // Set main_chunk_ to the inner chunk so CLOSURE doesn't snapshot globals
  auto inner_chunk_ptr = std::shared_ptr<BytecodeChunk>(saved_main_chunk, g_builder.chunk.get());
  vm.setMainChunkShared(inner_chunk_ptr);
  vm.current_chunk = g_builder.chunk.get();

  try {
    auto result = vm.executePersistent(*g_builder.chunk, entry, runArgs);
    vm.current_chunk = saved_chunk;
    vm.frame_count_ = saved_frame_count;
    vm.frame_arena_ = std::move(saved_frame_arena);
    vm.stack = std::move(saved_stack);
    vm.locals = std::move(saved_locals);
    vm.immutable_locals_ = std::move(saved_immutable_locals);
    vm.setMainChunkShared(saved_main_chunk);
    return result;
  } catch (const std::exception &e) {
    vm.current_chunk = saved_chunk;
    vm.frame_count_ = saved_frame_count;
    vm.frame_arena_ = std::move(saved_frame_arena);
    vm.stack = std::move(saved_stack);
    vm.locals = std::move(saved_locals);
    vm.immutable_locals_ = std::move(saved_immutable_locals);
    vm.setMainChunkShared(saved_main_chunk);
    throw std::runtime_error(std::string("Bytecode error: ") + e.what());
  }
	});

	api.registerFunction("bc.serialize", [api](const std::vector<Value> &args) -> Value {
	    if (args.empty() || (!args[0].isStringId() && !args[0].isStringValId())) {
	        throw std::runtime_error("bc.serialize: requires path (string)");
	    }
	    auto path = api.resolveString(args[0]);
	    auto &chunk = *g_builder.chunk;
	    if (chunk.getFunctionCount() == 0) {
	        throw std::runtime_error("bc.serialize: no functions in chunk");
	    }
	    havel::compiler::ValueSerializer serializer;
	    auto data = serializer.serializeChunk(chunk);
	    std::ofstream out(path, std::ios::binary);
	    if (!out.is_open()) {
	        throw std::runtime_error("bc.serialize: cannot open " + path);
	    }
	    out.write(reinterpret_cast<const char *>(data.data()),
	              static_cast<std::streamsize>(data.size()));
	    out.close();
	    return Value::makeBool(true);
	});

	api.registerFunction("bc.func_count", [](const std::vector<Value> &) -> Value {
		return Value::makeInt(static_cast<int64_t>(g_builder.chunk->getFunctionCount()));
	});

	api.registerFunction("bc.instr_count", [](const std::vector<Value> &) -> Value {
		auto *fn = g_builder.currentFunc();
		if (!fn) return Value::makeInt(0);
		return Value::makeInt(static_cast<int64_t>(fn->instructions.size()));
	});

	api.registerFunction("bc.const_count", [](const std::vector<Value> &) -> Value {
		auto *fn = g_builder.currentFunc();
		if (!fn) return Value::makeInt(0);
		return Value::makeInt(static_cast<int64_t>(fn->constants.size()));
	});

	api.registerFunction("bc.disasm", [api](const std::vector<Value> &) -> Value {
		auto *fn = g_builder.currentFunc();
		if (!fn) return api.makeString("<no current function>");
		std::string out;
		out += "function " + fn->name + " (params=" + std::to_string(fn->param_count)
			+ " locals=" + std::to_string(fn->local_count) + ")\n";
		for (size_t i = 0; i < fn->instructions.size(); ++i) {
			auto &instr = fn->instructions[i];
			out += "  " + std::to_string(i) + ": ";
			uint8_t opVal = static_cast<uint8_t>(instr.opcode);
			out += std::to_string(opVal);
			for (auto &op : instr.operands) {
				if (op.isInt()) out += " " + std::to_string(op.asInt());
				else if (op.isDouble()) out += " " + std::to_string(op.asDouble());
				else if (op.isStringId()) out += " str[" + std::to_string(op.asStringValId()) + "]";
				else out += " ?";
			}
			out += "\n";
		}
		if (!fn->constants.empty()) {
			out += "constants:\n";
			for (size_t i = 0; i < fn->constants.size(); ++i) {
				out += "  [" + std::to_string(i) + "] ";
				auto &c = fn->constants[i];
				if (c.isInt()) out += std::to_string(c.asInt());
				else if (c.isDouble()) out += std::to_string(c.asDouble());
				else if (c.isStringId()) out += "str[" + std::to_string(c.asStringValId()) + "]";
				else out += "?";
				out += "\n";
			}
		}
		return api.makeString(out);
	});

  api.registerFunction("bc.is_string_id", [](const std::vector<Value> &args) -> Value {
    if (args.empty()) return Value::makeBool(false);
    return Value::makeBool(args[0].isStringId());
    });

api.registerFunction("bc.is_string_val_id", [](const std::vector<Value> &args) -> Value {
    if (args.empty()) return Value::makeBool(false);
    return Value::makeBool(args[0].isStringValId());
    });

api.registerFunction("bc.opcode_id", [api](const std::vector<Value> &args) -> Value {
    if (args.empty() || (!args[0].isStringId() && !args[0].isStringValId())) {
        throw std::runtime_error("bc.opcode_id: requires opcode name (string)");
    }
    auto name = api.resolveString(args[0]);
    auto op = parseOpcode(name);
    return Value::makeInt(static_cast<int64_t>(static_cast<uint8_t>(op)));
  });

  auto bcObj = api.makeObject();
  api.setField(bcObj, "reset", api.makeFunctionRef("bc.reset"));
    api.setField(bcObj, "func_new", api.makeFunctionRef("bc.func_new"));
    api.setField(bcObj, "func_push", api.makeFunctionRef("bc.func_push"));
    api.setField(bcObj, "func_pop", api.makeFunctionRef("bc.func_pop"));
  api.setField(bcObj, "emit", api.makeFunctionRef("bc.emit"));
  api.setField(bcObj, "add_const", api.makeFunctionRef("bc.add_const"));
  api.setField(bcObj, "add_string", api.makeFunctionRef("bc.add_string"));
  api.setField(bcObj, "add_chunk_string", api.makeFunctionRef("bc.add_chunk_string"));
  api.setField(bcObj, "patch_jump", api.makeFunctionRef("bc.patch_jump"));
  api.setField(bcObj, "set_local_count", api.makeFunctionRef("bc.set_local_count"));
  api.setField(bcObj, "set_param_count", api.makeFunctionRef("bc.set_param_count"));
  api.setField(bcObj, "add_upvalue", api.makeFunctionRef("bc.add_upvalue"));
  api.setField(bcObj, "execute", api.makeFunctionRef("bc.execute"));
  api.setField(bcObj, "serialize", api.makeFunctionRef("bc.serialize"));
  api.setField(bcObj, "execute_persistent", api.makeFunctionRef("bc.execute_persistent"));
  api.setField(bcObj, "func_count", api.makeFunctionRef("bc.func_count"));
  api.setField(bcObj, "instr_count", api.makeFunctionRef("bc.instr_count"));
  api.setField(bcObj, "const_count", api.makeFunctionRef("bc.const_count"));
  api.setField(bcObj, "disasm", api.makeFunctionRef("bc.disasm"));
api.setField(bcObj, "opcode_id", api.makeFunctionRef("bc.opcode_id"));
api.setField(bcObj, "str_id", api.makeFunctionRef("bc.str_id"));
    api.setField(bcObj, "is_string_id", api.makeFunctionRef("bc.is_string_id"));
    api.setField(bcObj, "is_string_val_id", api.makeFunctionRef("bc.is_string_val_id"));
    api.setGlobal("bc", bcObj);
}

} // namespace havel::stdlib
