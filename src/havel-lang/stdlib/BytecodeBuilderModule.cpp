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
    fprintf(stderr, "[DBG add_const] func='%s' idx=%u isInt=%d isDouble=%d isNull=%d isBool=%d bits=0x%016llx",
        fn->name.c_str(), idx, args[0].isInt(), args[0].isDouble(), args[0].isNull(), args[0].isBool(),
        (unsigned long long)args[0].rawBits());
    if (args[0].isInt()) fprintf(stderr, " int_val=%lld", (long long)args[0].asInt());
    if (args[0].isDouble()) fprintf(stderr, " double_val=%f", args[0].asDouble());
    fprintf(stderr, "\n");
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

    fprintf(stderr, "[DBG bc.execute] About to execute chunk with %u functions, globals_size=%zu, 'f' in globals: %d\n",
        g_builder.chunk->getFunctionCount(), vm.globals.size(),
        vm.globals.count("f") > 0 ? 1 : 0);

    // Dump all instructions in the entry function
    const auto* entryFunc = g_builder.chunk->getFunction("__main__");
    if (entryFunc) {
<<<<<<< HEAD
        fprintf(stderr, "[DBG bc.execute] __main__ has %zu instructions, %zu constants, %u params, %u locals\n",
            entryFunc->instructions.size(), entryFunc->constants.size(),
            entryFunc->param_count, entryFunc->local_count);
=======
    fprintf(stderr, "[DBG bc.execute] __main__ has %zu instructions, %zu constants, %u params, %u locals\n",
        entryFunc->instructions.size(), entryFunc->constants.size(),
        entryFunc->param_count, entryFunc->local_count);
    for (size_t ci = 0; ci < entryFunc->constants.size(); ci++) {
        const auto& cv = entryFunc->constants[ci];
        fprintf(stderr, "[DBG bc.execute] __main__ constants[%zu]: isInt=%d isDouble=%d isNull=%d isBool=%d bits=0x%016llx",
            ci, cv.isInt(), cv.isDouble(), cv.isNull(), cv.isBool(), (unsigned long long)cv.rawBits());
        if (cv.isInt()) fprintf(stderr, " int_val=%lld", (long long)cv.asInt());
        if (cv.isDouble()) fprintf(stderr, " double_val=%f", cv.asDouble());
        fprintf(stderr, "\n");
    }
>>>>>>> 57b2a2dd (feat: enhance type identification for functions and closures in runtime)
        for (size_t i = 0; i < entryFunc->instructions.size(); i++) {
            const auto& inst = entryFunc->instructions[i];
            fprintf(stderr, "[DBG bc.execute]   __main__[%zu]: op=%d", i, (int)inst.opcode);
            for (size_t j = 0; j < inst.operands.size(); j++) {
                if (inst.operands[j].isStringValId()) {
                    uint32_t sid = inst.operands[j].asStringValId();
                    std::string s = g_builder.chunk->getString(sid);
                    fprintf(stderr, " strId(%u)='%s'", sid, s.c_str());
                } else if (inst.operands[j].isInt()) {
                    fprintf(stderr, " int(%lld)", (long long)inst.operands[j].asInt());
                } else {
                    fprintf(stderr, " %s", inst.operands[j].toString().c_str());
                }
            }
            fprintf(stderr, "\n");
        }
    }

    // Also dump function 0 (which should be 'f')
    const auto* funcF = g_builder.chunk->getFunction(0);
    if (funcF) {
        fprintf(stderr, "[DBG bc.execute] func[0] name='%s' has %zu instructions, %zu constants, %u params, %u locals\n",
            funcF->name.c_str(), funcF->instructions.size(), funcF->constants.size(),
            funcF->param_count, funcF->local_count);
    }
    // Dump all function names
    for (uint32_t fi = 0; fi < g_builder.chunk->getFunctionCount(); fi++) {
        const auto* fInfo = g_builder.chunk->getFunction(fi);
        if (fInfo) fprintf(stderr, "[DBG bc.execute] chunk func[%u] = '%s'\n", fi, fInfo->name.c_str());
    }

    try {
        auto result = vm.execute(*g_builder.chunk, entry, runArgs);

        fprintf(stderr, "[DBG bc.execute] After execute: globals_size=%zu, 'f' in globals: %d\n",
            vm.globals.size(), vm.globals.count("f") > 0 ? 1 : 0);

vm.current_chunk = saved_chunk;
    vm.frame_count_ = saved_frame_count;
    vm.frame_arena_ = std::move(saved_frame_arena);
    vm.stack = std::move(saved_stack);
    vm.locals = std::move(saved_locals);
    vm.setMainChunkShared(saved_main_chunk);
    return result;
} catch (const std::exception &e) {
    fprintf(stderr, "[DBG bc.execute] CAUGHT: %s\n", e.what());
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

        try {
            auto result = vm.executePersistent(*g_builder.chunk, entry, runArgs);
            vm.current_chunk = saved_chunk;
            vm.frame_count_ = saved_frame_count;
            vm.frame_arena_ = std::move(saved_frame_arena);
            vm.stack = std::move(saved_stack);
            vm.locals = std::move(saved_locals);
            vm.immutable_locals_ = std::move(saved_immutable_locals);
            return result;
        } catch (const std::exception &e) {
            vm.current_chunk = saved_chunk;
            vm.frame_count_ = saved_frame_count;
            vm.frame_arena_ = std::move(saved_frame_arena);
            vm.stack = std::move(saved_stack);
            vm.locals = std::move(saved_locals);
            vm.immutable_locals_ = std::move(saved_immutable_locals);
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
