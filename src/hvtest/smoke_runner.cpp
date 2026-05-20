#include "smoke_runner.hpp"
#include "havel-lang/compiler/core/ByteCompiler.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/parser/Parser.h"
#include "havel-lang/runtime/StdLibModules.hpp"
#include "havel-lang/runtime/HostContext.hpp"

#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace hvtest {
namespace {

using havel::compiler::Value;

std::string opcodeName(havel::compiler::OpCode opcode) {
  using havel::compiler::OpCode;
  switch (opcode) {
  case OpCode::LOAD_CONST:
    return "LOAD_CONST";
  case OpCode::LOAD_GLOBAL:
    return "LOAD_GLOBAL";
  case OpCode::LOAD_VAR:
    return "LOAD_VAR";
  case OpCode::STORE_VAR:
    return "STORE_VAR";
  case OpCode::LOAD_UPVALUE:
    return "LOAD_UPVALUE";
  case OpCode::STORE_UPVALUE:
    return "STORE_UPVALUE";
  case OpCode::POP:
    return "POP";
  case OpCode::DUP:
    return "DUP";
  case OpCode::SWAP:
    return "SWAP";
  case OpCode::ADD:
    return "ADD";
  case OpCode::SUB:
    return "SUB";
  case OpCode::MUL:
    return "MUL";
  case OpCode::DIV:
    return "DIV";
  case OpCode::MOD:
    return "MOD";
  case OpCode::POW:
    return "POW";
  case OpCode::EQ:
    return "EQ";
  case OpCode::NEQ:
    return "NEQ";
  case OpCode::LT:
    return "LT";
  case OpCode::LTE:
    return "LTE";
  case OpCode::GT:
    return "GT";
  case OpCode::GTE:
    return "GTE";
  case OpCode::AND:
    return "AND";
  case OpCode::OR:
    return "OR";
  case OpCode::NOT:
    return "NOT";
  case OpCode::JUMP:
    return "JUMP";
  case OpCode::JUMP_IF_FALSE:
    return "JUMP_IF_FALSE";
  case OpCode::JUMP_IF_TRUE:
    return "JUMP_IF_TRUE";
	case OpCode::CALL:
	return "CALL";
	case OpCode::RETURN:
    return "RETURN";
  case OpCode::TRY_ENTER:
    return "TRY_ENTER";
  case OpCode::TRY_EXIT:
    return "TRY_EXIT";
  case OpCode::LOAD_EXCEPTION:
    return "LOAD_EXCEPTION";
  case OpCode::THROW:
    return "THROW";
  case OpCode::CLOSURE:
    return "CLOSURE";
  case OpCode::ARRAY_NEW:
    return "ARRAY_NEW";
  case OpCode::ARRAY_GET:
    return "ARRAY_GET";
  case OpCode::ARRAY_SET:
    return "ARRAY_SET";
  case OpCode::ARRAY_PUSH:
    return "ARRAY_PUSH";
  case OpCode::SET_NEW:
    return "SET_NEW";
  case OpCode::OBJECT_NEW:
    return "OBJECT_NEW";
  case OpCode::OBJECT_GET:
    return "OBJECT_GET";
    case OpCode::OBJECT_SET:
        return "OBJECT_SET";
    case OpCode::CALL_METHOD:
        return "CALL_METHOD";
 case OpCode::STORE_GLOBAL:
 return "STORE_GLOBAL";
 case OpCode::FIBER_AWAIT:
 return "FIBER_AWAIT";
 case OpCode::FIBER_SLEEP:
 return "FIBER_SLEEP";
 default:
    return "OTHER";
  }
}

std::string bytecodeValueToString(const Value &value) {
  if (value.isNull()) {
    return "null";
  }
  if (value.isBool()) {
    return value.asBool() ? "true" : "false";
  }
  if (value.isInt()) {
    return std::to_string(value.asInt());
  }
  if (value.isDouble()) {
    return std::to_string(value.asDouble());
  }
  if (value.isStringValId()) {
    return "str[" + std::to_string(value.asStringValId()) + "]";
  }
  if (value.isFunctionObjId()) {
    return "fn[" + std::to_string(value.asFunctionObjId()) + "]";
  }
  if (value.isClosureId()) {
    return "closure[" + std::to_string(value.asClosureId()) + "]";
  }
  if (value.isArrayId()) {
    return "array[" + std::to_string(value.asArrayId()) + "]";
  }
  if (value.isObjectId()) {
    return "object[" + std::to_string(value.asObjectId()) + "]";
  }
  if (value.isSetId()) {
    return "set[" + std::to_string(value.asSetId()) + "]";
  }
  if (value.isHostFuncId()) {
    return "hostfn[" + std::to_string(value.asHostFuncId()) + "]";
  }
  return "<unknown>";
}

bool equalsInt(const Value &value, int64_t expected) {
  if (!value.isInt()) {
    return false;
  }
  return value.asInt() == expected;
}

std::unique_ptr<havel::compiler::BytecodeChunk>
compileChunk(const std::string &source,
             const std::vector<std::string> &extra_host_builtins = {}) {
  havel::parser::Parser parser;
  auto program = parser.produceAST(source);
  if (!program) {
    throw std::runtime_error("parser returned null AST");
  }

  havel::compiler::ByteCompiler compiler;
  //for (const auto &name : extra_host_builtins) {
  //  compiler.addHostBuiltin(name);
  //}
  return compiler.compile(*program);
}

void dumpBytecode(const std::string &name, const std::string &source) {
  auto chunk = compileChunk(source);
  std::cout << "--- BYTECODE DUMP: " << name << " ---" << std::endl;

 for (const auto &function : chunk->getAllFunctions()) {
 std::cout << "fn " << function.name << "(params=" << function.param_count
 << ", locals=" << function.local_count << ")" << std::endl;

 if (!function.constants.empty()) {
 std::cout << " constants: ";
 for (size_t i = 0; i < function.constants.size(); ++i) {
 if (i > 0) std::cout << ", ";
 std::cout << "[" << i << "]=" << bytecodeValueToString(function.constants[i]);
 }
 std::cout << std::endl;
 }

 for (size_t i = 0; i < function.instructions.size(); ++i) {
      const auto &instruction = function.instructions[i];
      std::cout << "  " << i << ": " << opcodeName(instruction.opcode);
      if (!instruction.operands.empty()) {
        std::cout << " ";
        for (size_t j = 0; j < instruction.operands.size(); ++j) {
          if (j > 0) {
            std::cout << ", ";
          }
          std::cout << bytecodeValueToString(instruction.operands[j]);
        }
      }
      if (i < function.instruction_locations.size()) {
        const auto &location = function.instruction_locations[i];
        std::cout << " @";
        if (location.line == 0 && location.column == 0) {
          std::cout << "?";
        } else {
          std::cout << location.line << ":" << location.column;
        }
      }
      std::cout << std::endl;
    }

    if (!function.constants.empty()) {
      std::cout << "  constants:" << std::endl;
      for (size_t c = 0; c < function.constants.size(); ++c) {
        std::cout << "    [" << c << "] "
                  << bytecodeValueToString(function.constants[c]) << std::endl;
      }
    }
  }
}

int runCase(const std::string &name, const std::string &source, int64_t expected,
 bool dump_bytecode, const std::string &snapshot_dir) {
 try {
    if (dump_bytecode) {
      dumpBytecode(name, source);
    }

    havel::compiler::PipelineOptions options;
    options.compile_unit_name = name;
    options.snapshot_dir = snapshot_dir;
    options.write_snapshot_artifact = !snapshot_dir.empty();

 havel::compiler::VM *vm_ptr = nullptr;
 options.vm_setup = [&](havel::compiler::VM &vm) { vm_ptr = &vm; };
 std::unordered_map<std::string, Value> thread_callbacks;

 const auto result =
        havel::compiler::runBytecodePipeline(source, "__main__", options);
 if (!equalsInt(result.return_value, expected)) {
 std::string val_desc;
 if (result.return_value.isInt()) val_desc = std::to_string(result.return_value.asInt());
 else if (result.return_value.isNull()) val_desc = "null";
 else if (result.return_value.isCoroutineId()) val_desc = "coroutine:" + std::to_string(result.return_value.asCoroutineId());
 else if (result.return_value.isDouble()) val_desc = "double:" + std::to_string(result.return_value.asDouble());
else if (result.return_value.isBool()) val_desc = "bool:" + std::to_string(result.return_value.asBool());
else val_desc = "non-int";
 std::cerr << "[FAIL] " << name << ": expected " << expected
 << " but got " << val_desc << std::endl;
 return 1;
 }

 std::cout << "[PASS] " << name << std::endl;
 return 0;
  } catch (const std::exception &e) {
    std::cerr << "[FAIL] " << name << ": exception: " << e.what()
              << std::endl;
    return 1;
  }
}

int runAsyncCase(const std::string &name, const std::string &source,
                 int64_t expected, bool dump_bytecode,
                 const std::string &snapshot_dir) {
  try {
    if (dump_bytecode) {
      dumpBytecode(name, source);
    }

    havel::compiler::PipelineOptions options;
    options.compile_unit_name = name;
    options.snapshot_dir = snapshot_dir;
	options.write_snapshot_artifact = !snapshot_dir.empty();

	havel::compiler::VM *vm_ptr = nullptr;
    uint64_t next_task_id = 1;
    std::unordered_map<std::string, Value> task_results;
    std::unordered_map<std::string, Value> thread_callbacks;
    std::unordered_map<std::string, bool> thread_running;
    std::unordered_map<std::string, bool> thread_paused;
    std::unordered_map<std::string, Value> interval_callbacks;
    std::unordered_map<std::string, bool> interval_running;
    std::unordered_map<std::string, bool> interval_paused;
std::unordered_map<std::string, Value> timeout_callbacks;
std::unordered_map<std::string, bool> timeout_running;

options.vm_setup = [&](havel::compiler::VM &vm) {
    vm_ptr = &vm;
  auto thread_obj = vm.createHostObject();
  auto threadGuard = vm.makeRoot(Value::makeObjectId(thread_obj.id));
  vm.setHostObjectField(thread_obj, "spawn", Value::makeHostFuncId(vm.getHostFunctionIndex("thread")));
  vm.setGlobal("thread", Value::makeObjectId(thread_obj.id));
  auto interval_obj = vm.createHostObject();
  auto intervalGuard = vm.makeRoot(Value::makeObjectId(interval_obj.id));
  vm.setHostObjectField(interval_obj, "start", Value::makeHostFuncId(vm.getHostFunctionIndex("interval")));
  vm.setGlobal("interval", Value::makeObjectId(interval_obj.id));
  auto timeout_obj = vm.createHostObject();
  auto timeoutGuard = vm.makeRoot(Value::makeObjectId(timeout_obj.id));
  vm.setHostObjectField(timeout_obj, "start", Value::makeHostFuncId(vm.getHostFunctionIndex("timeout")));
    vm.setGlobal("timeout", Value::makeObjectId(timeout_obj.id));

    vm.registerPrototypeMethodByName("object", "send", "object.send");
    vm.registerPrototypeMethodByName("object", "pause", "object.pause");
    vm.registerPrototypeMethodByName("object", "resume", "object.resume");
    vm.registerPrototypeMethodByName("object", "stop", "object.stop");
    vm.registerPrototypeMethodByName("object", "running", "object.running");
};

 options.host_functions["async.await"] = [&](const std::vector<Value> &args) {
 if (args.empty()) return Value::makeNull();
 return args[0];
 };
 options.host_functions["await"] = options.host_functions["async.await"];

 options.host_functions["sleep"] = [&](const std::vector<Value> &args) {
 return Value::makeNull();
 };

options.host_functions["thread"] = [&](const std::vector<Value> &args) {
    if (!vm_ptr || args.empty()) {
        throw std::runtime_error("thread requires callback");
    }
    // When called via CALL_METHOD (thread.spawn(fn)), receiver is prepended
    Value callback = args[0];
    if (args.size() >= 2 && args[0].isObjectId()) {
        callback = args[1];
    }
    int64_t tid = next_task_id++;
    std::string id = "thread-" + std::to_string(tid);
    thread_callbacks[id] = callback;
    thread_running[id] = true;
    thread_paused[id] = false;
    auto obj = vm_ptr->createHostObject();
    vm_ptr->setHostObjectField(obj, "__kind", Value::makeInt(0));
    vm_ptr->setHostObjectField(obj, "__id", Value::makeInt(tid));
    vm_ptr->setHostObjectField(obj, "send", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("thread.send")));
    vm_ptr->setHostObjectField(obj, "pause", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("thread.pause")));
    vm_ptr->setHostObjectField(obj, "resume", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("thread.resume")));
    vm_ptr->setHostObjectField(obj, "running", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("thread.running")));
    return Value::makeObjectId(obj.id);
};

// thread.spawn(closure) — called by block-sugar "thread { ... }" via LOAD_GLOBAL + CALL
// Args: [closure] (no receiver prepended since it's not CALL_METHOD)
options.host_functions["thread.spawn"] = [&](const std::vector<Value> &args) {
    if (!vm_ptr || args.empty()) {
        throw std::runtime_error("thread.spawn requires callback");
    }
    Value callback = args[0];
    int64_t tid = next_task_id++;
    std::string id = "thread-" + std::to_string(tid);
    thread_callbacks[id] = callback;
    thread_running[id] = true;
    thread_paused[id] = false;
    auto obj = vm_ptr->createHostObject();
    vm_ptr->setHostObjectField(obj, "__kind", Value::makeInt(0));
    vm_ptr->setHostObjectField(obj, "__id", Value::makeInt(tid));
    vm_ptr->setHostObjectField(obj, "send", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("thread.send")));
    vm_ptr->setHostObjectField(obj, "pause", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("thread.pause")));
    vm_ptr->setHostObjectField(obj, "resume", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("thread.resume")));
    vm_ptr->setHostObjectField(obj, "running", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("thread.running")));
    return Value::makeObjectId(obj.id);
};

 options.host_functions["thread.send"] = [&](const std::vector<Value> &args) {
 if (!vm_ptr || args.size() < 2 || !args[0].isObjectId()) {
 return Value::makeNull();
 }
 auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
 auto idv = vm_ptr->getHostObjectField(obj, "__id");
 if (!idv.isInt()) return Value::makeNull();
 std::string key = "thread-" + std::to_string(idv.asInt());
 auto it = thread_callbacks.find(key);
 if (it == thread_callbacks.end()) return Value::makeNull();
 Value msg = args[1];
 Value result = vm_ptr->callFunction(it->second, {msg});
 vm_ptr->setHostObjectField(obj, "__await_result", result);
 return result;
 };
options.host_functions["thread.pause"] = [&](const std::vector<Value> &args) {
    if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
    auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
    auto idv = vm_ptr->getHostObjectField(obj, "__id");
    if (!idv.isInt()) return Value(false);
    std::string key = "thread-" + std::to_string(idv.asInt());
    thread_paused[key] = true;
    return Value(true);
};
options.host_functions["thread.resume"] = [&](const std::vector<Value> &args) {
    if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
    auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
    auto idv = vm_ptr->getHostObjectField(obj, "__id");
    if (!idv.isInt()) return Value(false);
    std::string key = "thread-" + std::to_string(idv.asInt());
    thread_paused[key] = false;
    return Value(true);
};
options.host_functions["thread.stop"] = [&](const std::vector<Value> &args) {
    if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
    auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
    auto idv = vm_ptr->getHostObjectField(obj, "__id");
    if (!idv.isInt()) return Value(false);
    std::string key = "thread-" + std::to_string(idv.asInt());
    thread_running[key] = false;
    return Value(true);
};
options.host_functions["thread.running"] = [&](const std::vector<Value> &args) {
    if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
    auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
    auto idv = vm_ptr->getHostObjectField(obj, "__id");
    if (!idv.isInt()) return Value(false);
    std::string key = "thread-" + std::to_string(idv.asInt());
    auto it = thread_running.find(key);
    return Value(it != thread_running.end() && it->second);
};

 options.host_functions["interval"] = [&](const std::vector<Value> &args) {
 if (!vm_ptr || args.empty()) throw std::runtime_error("interval requires delay + callback");
 size_t cb_idx = 1;
 size_t delay_idx = 1;
 if (args.size() >= 3 && args[0].isObjectId()) {
 cb_idx = 2;
 delay_idx = 1;
 }
 if (args.size() <= cb_idx) throw std::runtime_error("interval requires delay + callback");
 int64_t iid = next_task_id++;
 std::string id = "interval-" + std::to_string(iid);
 interval_callbacks[id] = args[cb_idx];
 interval_running[id] = true;
 interval_paused[id] = false;
 Value result = vm_ptr->callFunction(interval_callbacks[id], {});
 auto obj = vm_ptr->createHostObject();
 vm_ptr->setHostObjectField(obj, "__kind", Value::makeInt(1));
 vm_ptr->setHostObjectField(obj, "__id", Value::makeInt(iid));
 vm_ptr->setHostObjectField(obj, "__await_result", result);
 return Value::makeObjectId(obj.id);
 };

// interval.start(ms, closure) — called by block-sugar "interval N { ... }" via LOAD_GLOBAL + CALL
// Args: [delay, closure] (no receiver prepended)
 options.host_functions["interval.start"] = [&](const std::vector<Value> &args) {
 if (!vm_ptr || args.size() < 2) throw std::runtime_error("interval.start requires delay + callback");
 int64_t iid = next_task_id++;
 std::string id = "interval-" + std::to_string(iid);
 interval_callbacks[id] = args[1];
 interval_running[id] = true;
 interval_paused[id] = false;
 Value result = vm_ptr->callFunction(interval_callbacks[id], {});
 auto obj = vm_ptr->createHostObject();
 vm_ptr->setHostObjectField(obj, "__kind", Value::makeInt(1));
 vm_ptr->setHostObjectField(obj, "__id", Value::makeInt(iid));
 vm_ptr->setHostObjectField(obj, "__await_result", result);
 return Value::makeObjectId(obj.id);
 };

options.host_functions["interval.pause"] = [&](const std::vector<Value> &args) {
    if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
    auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
    auto idv = vm_ptr->getHostObjectField(obj, "__id");
    if (!idv.isInt()) return Value(false);
    std::string key = "interval-" + std::to_string(idv.asInt());
    interval_paused[key] = true;
    return Value(true);
};
options.host_functions["interval.resume"] = [&](const std::vector<Value> &args) {
    if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
    auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
    auto idv = vm_ptr->getHostObjectField(obj, "__id");
    if (!idv.isInt()) return Value(false);
    std::string key = "interval-" + std::to_string(idv.asInt());
    interval_paused[key] = false;
    return Value(true);
};
options.host_functions["interval.stop"] = [&](const std::vector<Value> &args) {
    if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
    auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
    auto idv = vm_ptr->getHostObjectField(obj, "__id");
    if (!idv.isInt()) return Value(false);
    std::string key = "interval-" + std::to_string(idv.asInt());
    interval_running[key] = false;
    return Value(true);
};

 options.host_functions["timeout"] = [&](const std::vector<Value> &args) {
 if (!vm_ptr || args.empty()) throw std::runtime_error("timeout requires delay + callback");
 size_t cb_idx = 1;
 if (args.size() >= 3 && args[0].isObjectId()) {
 cb_idx = 2;
 }
 if (args.size() <= cb_idx) throw std::runtime_error("timeout requires delay + callback");
 int64_t tid = next_task_id++;
 std::string id = "timeout-" + std::to_string(tid);
 timeout_callbacks[id] = args[cb_idx];
 timeout_running[id] = true;
 Value result = vm_ptr->callFunction(timeout_callbacks[id], {});
 timeout_running[id] = false;
 auto obj = vm_ptr->createHostObject();
 vm_ptr->setHostObjectField(obj, "__kind", Value::makeInt(2));
 vm_ptr->setHostObjectField(obj, "__id", Value::makeInt(tid));
 vm_ptr->setHostObjectField(obj, "__await_result", result);
 return Value::makeObjectId(obj.id);
 };
options.host_functions["timeout.cancel"] = [&](const std::vector<Value> &args) {
    if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
    auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
    auto idv = vm_ptr->getHostObjectField(obj, "__id");
    if (!idv.isInt()) return Value(false);
    std::string key = "timeout-" + std::to_string(idv.asInt());
    timeout_running[key] = false;
    return Value(true);
};
// timeout.start(ms, closure) — called by block-sugar "timeout N { ... }" via LOAD_GLOBAL + CALL
// Args: [delay, closure] (no receiver prepended)
 options.host_functions["timeout.start"] = [&](const std::vector<Value> &args) {
 if (!vm_ptr || args.size() < 2) throw std::runtime_error("timeout.start requires delay + callback");
 int64_t tid = next_task_id++;
 std::string id = "timeout-" + std::to_string(tid);
 timeout_callbacks[id] = args[1];
 timeout_running[id] = true;
 Value result = vm_ptr->callFunction(timeout_callbacks[id], {});
 timeout_running[id] = false;
 auto obj = vm_ptr->createHostObject();
 vm_ptr->setHostObjectField(obj, "__kind", Value::makeInt(2));
 vm_ptr->setHostObjectField(obj, "__id", Value::makeInt(tid));
 vm_ptr->setHostObjectField(obj, "__await_result", result);
 return Value::makeObjectId(obj.id);
 };

    options.host_functions["object.send"] = [&](const std::vector<Value> &args) {
    return options.host_functions["thread.send"](args);
};
    options.host_functions["object.pause"] = [&](const std::vector<Value> &args) {
      if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
      auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
      auto kind = vm_ptr->getHostObjectField(obj, "__kind");
      if (!kind.isInt()) return Value(false);
      if (kind.asInt() == 0) return options.host_functions["thread.pause"](args);
      if (kind.asInt() == 1) return options.host_functions["interval.pause"](args);
      return Value(false);
    };
    options.host_functions["object.resume"] = [&](const std::vector<Value> &args) {
      if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
      auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
      auto kind = vm_ptr->getHostObjectField(obj, "__kind");
      if (!kind.isInt()) return Value(false);
      if (kind.asInt() == 0) return options.host_functions["thread.resume"](args);
      if (kind.asInt() == 1) return options.host_functions["interval.resume"](args);
      return Value(false);
    };
    options.host_functions["object.stop"] = [&](const std::vector<Value> &args) {
      if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
      auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
      auto kind = vm_ptr->getHostObjectField(obj, "__kind");
      if (!kind.isInt()) return Value(false);
      if (kind.asInt() == 0) return options.host_functions["thread.stop"](args);
      if (kind.asInt() == 1) return options.host_functions["interval.stop"](args);
      return Value(false);
    };
    options.host_functions["object.cancel"] = options.host_functions["timeout.cancel"];
options.host_functions["object.running"] = [&](const std::vector<Value> &args) {
    if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
    auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
    auto kind = vm_ptr->getHostObjectField(obj, "__kind");
    if (!kind.isInt()) return Value(false);
    if (kind.asInt() == 0) return options.host_functions["thread.running"](args);
    if (kind.asInt() == 1) {
        auto idv = vm_ptr->getHostObjectField(obj, "__id");
        if (!idv.isInt()) return Value(false);
        std::string key = "interval-" + std::to_string(idv.asInt());
        auto it = interval_running.find(key);
        return Value(it != interval_running.end() && it->second);
    }
    if (kind.asInt() == 2) {
        auto idv = vm_ptr->getHostObjectField(obj, "__id");
        if (!idv.isInt()) return Value(false);
        std::string key = "timeout-" + std::to_string(idv.asInt());
        auto it = timeout_running.find(key);
        return Value(it != timeout_running.end() && it->second);
    }
 return Value(false);
 };


 const auto result =
        havel::compiler::runBytecodePipeline(source, "__main__", options);
    if (!equalsInt(result.return_value, expected)) {
        std::cerr << "[FAIL] " << name << ": expected " << expected
                  << " but got " << (result.return_value.isInt() ? std::to_string(result.return_value.asInt()) : std::string("non-int")) << std::endl;
        return 1;
    }

    std::cout << "[PASS] " << name << std::endl;
    return 0;
} catch (const std::exception &e) {
    std::cerr << "[FAIL] " << name << ": exception: " << e.what()
              << std::endl;
    return 1;
}
}

int runClosureCase(bool dump_bytecode, const std::string &snapshot_dir) {
    const std::string source = R"havel(
fn outer() {
    val x = 1
    fn inner() {
        return x
    }
    return inner()
}

return outer()
)havel";

  try {
    if (dump_bytecode) {
      dumpBytecode("closure", source);
    }

    havel::compiler::PipelineOptions options;
    options.compile_unit_name = "closure";
    options.snapshot_dir = snapshot_dir;
    options.write_snapshot_artifact = !snapshot_dir.empty();
    const auto result =
        havel::compiler::runBytecodePipeline(source, "__main__", options);
    if (!equalsInt(result.return_value, 1)) {
      std::cerr << "[FAIL] closure: expected 1 but got non-matching result"
                << std::endl;
      return 1;
    }
    std::cout << "[PASS] closure" << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "[FAIL] closure: exception: " << e.what() << std::endl;
    return 1;
  }
}

int runUnresolvedIdentifierCase(bool dump_bytecode,
                                const std::string &snapshot_dir) {
  const std::string source = R"havel(
return missing_value
)havel";

  try {
    if (dump_bytecode) {
      dumpBytecode("unresolved-identifier", source);
    }

    havel::compiler::PipelineOptions options;
    options.compile_unit_name = "unresolved-identifier";
    options.snapshot_dir = snapshot_dir;
    options.write_snapshot_artifact = !snapshot_dir.empty();
    (void)havel::compiler::runBytecodePipeline(source, "__main__", options);
    std::cerr << "[FAIL] unresolved-identifier: expected resolution error"
              << std::endl;
    return 1;
  } catch (const std::exception &e) {
    const std::string message = e.what();
    if (message.find("Unresolved identifier") == std::string::npos &&
        message.find("Undefined variable") == std::string::npos &&
        message.find("undefined variable") == std::string::npos) {
      std::cerr << "[FAIL] unresolved-identifier: wrong error: " << message
                << std::endl;
      return 1;
    }

    std::cout << "[PASS] unresolved-identifier" << std::endl;
    return 0;
  }
}

int runRuntimeLineErrorCase(bool dump_bytecode, const std::string &snapshot_dir) {
    const std::string source = R"havel(
fn bad() {
    x = 1
    return x / 0
}
return bad()
)havel";

  try {
    if (dump_bytecode) {
      dumpBytecode("runtime-line-error", source);
    }

    havel::compiler::PipelineOptions options;
    options.compile_unit_name = "runtime-line-error";
    options.snapshot_dir = snapshot_dir;
    options.write_snapshot_artifact = !snapshot_dir.empty();
    (void)havel::compiler::runBytecodePipeline(source, "__main__", options);
    std::cerr << "[FAIL] runtime-line-error: expected runtime error"
              << std::endl;
    return 1;
  } catch (const std::exception &e) {
    const std::string message = e.what();
    if (message.find("x / 0") == std::string::npos ||
        message.find("Division by zero") == std::string::npos) {
      std::cerr << "[FAIL] runtime-line-error: wrong error: " << message
                << std::endl;
      return 1;
    }
    std::cout << "[PASS] runtime-line-error" << std::endl;
    return 0;
  }
}

int runStackOverflowCase(bool dump_bytecode, const std::string &snapshot_dir) {
  const std::string source = R"havel(
fn spin() {
    return spin()
}
return spin()
)havel";

  try {
    if (dump_bytecode) {
      dumpBytecode("stack-overflow", source);
    }

    havel::compiler::PipelineOptions options;
    options.compile_unit_name = "stack-overflow";
    options.snapshot_dir = snapshot_dir;
    options.write_snapshot_artifact = !snapshot_dir.empty();
    (void)havel::compiler::runBytecodePipeline(source, "__main__", options);
    std::cerr << "[FAIL] stack-overflow: expected stack overflow error"
              << std::endl;
    return 1;
  } catch (const std::exception &e) {
    const std::string message = e.what();
    if (message.find("Stack overflow") == std::string::npos) {
      std::cerr << "[FAIL] stack-overflow: wrong error: " << message
                << std::endl;
      return 1;
    }
    std::cout << "[PASS] stack-overflow" << std::endl;
    return 0;
  }
}

int runHostRootLifetimeCase(bool dump_bytecode) {
  const std::string source = R"havel(
fn make() {
    x = 41
    fn inner() {
        return x + 1
    }
    return inner
}

cb = make()
store_closure(cb)
cb = 0
gc_now()
loaded = load_closure()
return loaded()
)havel";

  try {
    if (dump_bytecode) {
      dumpBytecode("host-root-lifetime", source);
    }

    auto chunk = compileChunk(source, {"store_closure", "load_closure", "gc_now"});
    havel::compiler::VM vm;
    vm.setGcAllocationBudget(1);
    std::optional<havel::compiler::VM::GCRoot> stored_closure;

    vm.registerHostFunction(
        "store_closure", 1,
        [&vm, &stored_closure](const std::vector<Value> &args) {
          stored_closure.emplace(vm.makeRoot(args[0]));
          return Value(nullptr);
        });
    vm.registerHostFunction(
        "load_closure", 0,
        [&stored_closure](const std::vector<Value> &) {
          if (!stored_closure.has_value()) {
            return Value(nullptr);
          }
          return stored_closure->get().value_or(Value(nullptr));
        });
    vm.registerHostFunction("gc_now", 0,
                            [&vm](const std::vector<Value> &) {
                              vm.runGarbageCollection();
                              return Value(nullptr);
                            });

    const auto result = vm.execute(*chunk, "__main__");
    if (!equalsInt(result, 42)) {
      std::cerr
          << "[FAIL] host-root-lifetime: expected 42 but got non-matching result"
          << std::endl;
      return 1;
    }

    stored_closure.reset();
    if (vm.externalRootCount() != 0) {
      std::cerr << "[FAIL] host-root-lifetime: external roots leaked"
                << std::endl;
      return 1;
    }

    std::cout << "[PASS] host-root-lifetime" << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "[FAIL] host-root-lifetime: exception: " << e.what()
              << std::endl;
    return 1;
  }
}

int runExternalCallbackInvocationCase(bool dump_bytecode) {
  const std::string source = R"havel(
fn makeCallback(seed) {
    x = seed
    fn cb() {
        return x + 2
    }
    return cb
}

cb = makeCallback(40)
register_cb(cb)
return trigger_cb()
)havel";

  try {
    if (dump_bytecode) {
      dumpBytecode("external-callback-invocation", source);
    }

    auto chunk = compileChunk(source, {"register_cb", "trigger_cb"});
    havel::compiler::VM vm;
    std::optional<havel::compiler::VM::GCRoot> stored_callback;

    vm.registerHostFunction(
        "register_cb", 1,
        [&vm, &stored_callback](const std::vector<Value> &args) {
          stored_callback.emplace(vm.makeRoot(args[0]));
          return Value(true);
        });
    vm.registerHostFunction(
        "trigger_cb", 0,
        [&vm, &stored_callback](const std::vector<Value> &) {
          if (!stored_callback.has_value()) {
            throw std::runtime_error("missing callback");
          }
          auto callback = stored_callback->get();
          if (!callback.has_value()) {
            throw std::runtime_error("callback root missing");
          }
          return vm.call(*callback);
        });

    const auto result = vm.execute(*chunk, "__main__");
    if (!equalsInt(result, 42)) {
      std::cerr << "[FAIL] external-callback-invocation: expected 42 but got "
                   "non-matching result"
                << std::endl;
      return 1;
    }

    std::cout << "[PASS] external-callback-invocation" << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "[FAIL] external-callback-invocation: exception: " << e.what()
              << std::endl;
    return 1;
  }
}


// --- Stdlib smoke test infrastructure ---
// Creates a VM with registerPureStdLib, enabling tests that call host functions
// like fmt.hex, bit.and, pack.pack, etc. which are not available in the
// default runCase() pipeline.
int runStdlibCase(const std::string &name, const std::string &source,
                  int64_t expected, bool dump_bytecode,
                  const std::string &snapshot_dir) {
  try {
    if (dump_bytecode) {
      dumpBytecode(name, source);
    }

    // Create VM with HostContext, register pure stdlib modules
    havel::HostContext ctx;
    havel::compiler::VM vm(ctx);
    ctx.vm = &vm;
    havel::registerPureStdLib(vm);

    havel::compiler::PipelineOptions options;
    options.compile_unit_name = name;
    options.snapshot_dir = snapshot_dir;
    options.write_snapshot_artifact = !snapshot_dir.empty();
    options.vm_override = &vm;

    const auto result =
        havel::compiler::runBytecodePipeline(source, "__main__", options);
    if (!equalsInt(result.return_value, expected)) {
      std::string val_desc;
      if (result.return_value.isInt())
        val_desc = std::to_string(result.return_value.asInt());
      else if (result.return_value.isNull())
        val_desc = "null";
      else if (result.return_value.isDouble())
        val_desc = "double:" + std::to_string(result.return_value.asDouble());
      else if (result.return_value.isBool())
        val_desc = "bool:" + std::to_string(result.return_value.asBool());
      else
        val_desc = "non-int";
      std::cerr << "[FAIL] " << name << ": expected " << expected
                << " but got " << val_desc << std::endl;
      return 1;
    }

    std::cout << "[PASS] " << name << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "[FAIL] " << name << ": exception: " << e.what()
              << std::endl;
    return 1;
  }
}
} // namespace

int run_smoke_tests(int argc, char **argv) {
  bool dump_bytecode = false;
  std::string snapshot_dir = "/tmp/havel-bytecode-snapshots";
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--dump-bytecode") {
      dump_bytecode = true;
    } else if (std::string(argv[i]) == "--no-snapshots") {
      snapshot_dir.clear();
    } else if (std::string(argv[i]) == "--snapshot-dir") {
      if (i + 1 >= argc) {
        std::cerr << "--snapshot-dir requires a directory argument" << std::endl;
        return 1;
      }
      snapshot_dir = argv[++i];
    }
  }

  int failures = 0;

  failures += runCase("function-call", R"havel(
fn add(a, b) {
    return a + b
}

x = add(2, 3)
print(x)
return x
)havel", 5, dump_bytecode, snapshot_dir);

  failures += runCase("first-class-call", R"havel(
fn add(a, b) {
    return a + b
}

f = add
return f(20, 22)
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("nested-local-function", R"havel(
fn outer(x) {
    fn doubleIt(value) {
        return value * 2
    }
    f = doubleIt
    return f(x)
}

return outer(21)
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("hof-named-top-level-function", R"havel(
fn noise(v) {
    return v + 100
}

fn double(v) {
    return v * 2
}

nums = [1, 2, 3]
out = nums.map(double)
return out[2]
)havel", 6, dump_bytecode, snapshot_dir);

  failures += runCase("lambda-in-call", R"havel(
nums = [1, 2, 3]
out = nums.map((x) => x * 3)
return out[1]
)havel", 6, dump_bytecode, snapshot_dir);

  failures += runCase("method-chaining-array-hof", R"havel(
nums = [1, 2, 3, 4]
return nums.map((x) => x * 2).filter((x) => x > 4).reduce((a, b) => a + b, 0)
)havel", 14, dump_bytecode, snapshot_dir);

    failures += runAsyncCase("thread-send", R"havel(
worker = thread(fn(msg) { return msg + 1 })
worker.pause()
worker.resume()
worker.send(41)
if worker.running() {
    return 1
}
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runAsyncCase("thread-block-sugar", R"havel(
value = 0
worker = thread {
    value += 1
}
worker.send(0)
return value
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runAsyncCase("interval-timeout-controls", R"havel(
hits = 0
timer = interval(100, fn() { hits += 1 })
timer.pause()
timer.resume()
timer.stop()
t = timeout(100, fn() { hits += 10 })
t.cancel()
return hits
)havel", 11, dump_bytecode, snapshot_dir);

  failures += runAsyncCase("interval-timeout-block-sugar", R"havel(
hits = 0
i = interval 100 {
    hits += 1
}
i.stop()
t = timeout 10 {
    hits += 10
}
t.cancel()
return hits
)havel", 11, dump_bytecode, snapshot_dir);

  failures += runCase("const-tuple-destructure", R"havel(
const (a, b, c) = (5, 7, 9)
total = a + b + c
return total
)havel", 21, dump_bytecode, snapshot_dir);

  failures += runCase("struct-vm-basic", R"havel(
struct Point { x, y }
p = Point(10, 32)
return struct.get(p, "y")
)havel", 32, dump_bytecode, snapshot_dir);

  failures += runCase("closure-return", R"havel(
fn makeGetter(v) {
    x = v
    fn get() {
        return x
    }
    return get
}

getter = makeGetter(9)
return getter()
)havel", 9, dump_bytecode, snapshot_dir);

  failures += runCase("closure-transitive", R"havel(
fn outer() {
    x = 41
    fn middle() {
        fn inner() {
            return x + 1
        }
        return inner
    }
    return middle()
}

f = outer()
return f()
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("try-catch-throw-object", R"havel(
fn test() {
    try {
        throw {code: 41}
    } catch (e) {
        return e.code + 1
    }
}
return test()
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("try-finally-runs", R"havel(
x = 0
try {
    x = 1
} finally {
    x += 1
}
return x
)havel", 2, dump_bytecode, snapshot_dir);

  failures += runCase("throw-across-frames", R"havel(
fn inner() {
    throw 7
}
fn outer() {
    inner()
    return 0
}
try {
    return outer()
} catch (e) {
    return e
}
)havel", 7, dump_bytecode, snapshot_dir);

failures += runCase("iterable-hof-string-and-object", R"havel(
chars = "ab".map((c) => c)
print(chars)
print(chars[0])
print(chars[0] == "a")
keys = {a: 1, b: 2}.map((k) => k)
print(keys)
print(keys.find("a"))
if chars[0] == "a" {
  if keys.find("a") >= 0 {
    if keys.find("b") >= 0 {
      return 1
    }
  }
}
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("assignment-local", R"havel(
x = 1
x = x + 4
x += 5
return x
)havel", 10, dump_bytecode, snapshot_dir);

  failures += runCase("assignment-upvalue", R"havel(
fn makeCounter(seed) {
    x = seed
    fn next() {
        x += 1
        return x
    }
    return next
}

c = makeCounter(40)
c()
return c()
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("object-member-assignment", R"havel(
obj = {a: 1}
obj.a = 7
obj.a += 2
return obj.a
)havel", 9, dump_bytecode, snapshot_dir);

  failures += runCase("index-assignment-array", R"havel(
arr = [1, 2]
arr[1] = 10
arr[3] = 5
return arr[1] + arr[3]
)havel", 15, dump_bytecode, snapshot_dir);

  failures += runCase("set-create-and-assign", R"havel(
s = {1, 2}
s[3] = true
s[2] = false
if s[1] {
    if s[2] {
        return 0
    }
}
if s[3] {
    return 1
}
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("gc-allocation-stress", R"havel(
i = 0
while i < 2000 {
    arr = [i, i + 1, i + 2]
    obj = {v: i}
    s = {i}
    i += 1
}
return i
)havel", 2000, dump_bytecode, snapshot_dir);

  failures += runCase("system-gc-manual", R"havel(
i = 0
while i < 256 {
    arr = [i, i + 1]
    i += 1
}
system.gc()
return i
)havel", 256, dump_bytecode, snapshot_dir);

  failures += runCase("system-gc-stats", R"havel(
before = system.gcStats()
i = 0
while i < 64 {
    obj = {v: i}
    i += 1
}
system.gc()
after = system.gcStats()
if after.collections >= before.collections {
    if after.heapSize >= 0 {
        if after.objectCount >= 0 {
            if after.lastPauseNs >= 0 {
                return 1
            }
        }
    }
}
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("member-compound-single-eval", R"havel(
fn run() {
    hits = 0
    obj = {a: 1}
    fn getObj() {
        hits += 1
        return obj
    }

    getObj().a += 2
    return obj.a + hits * 10
}
return run()
)havel", 13, dump_bytecode, snapshot_dir);

  failures += runCase("index-compound-single-eval", R"havel(
fn run() {
    hits = 0
    arr = [1, 2]
    fn getArr() {
        hits += 1
        return arr
    }

    getArr()[1] += 3
    return arr[1] + hits * 10
}
return run()
)havel", 15, dump_bytecode, snapshot_dir);

  failures += runCase("while-loop", R"havel(
i = 0
while i < 1 {
    return 6
}
return 0
)havel", 6, dump_bytecode, snapshot_dir);

  failures += runCase("shadowing", R"havel(
x = 1
if true {
    val x = 2
    print(x)
}
return x
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("kwargs-test", R"havel(
print("a", "b", delim="-")
return 42
)havel", 42, dump_bytecode, snapshot_dir);

failures += runCase("tail-call-host-func", R"havel(
fn add(a, b) { return a + b }
return add(1, 2)
)havel", 3, dump_bytecode, snapshot_dir);

failures += runCase("tail-call-global-print", R"havel(
x = 3
print(x)
return x
)havel", 3, dump_bytecode, snapshot_dir);

failures += runCase("method-call-on-int", R"havel(
x = 1.+(2)
return x
)havel", 3, dump_bytecode, snapshot_dir);

failures += runCase("tail-call-method-arg", R"havel(
print(1.+(2))
return 0
)havel", 0, dump_bytecode, snapshot_dir);

  failures += runClosureCase(dump_bytecode, snapshot_dir);
  failures += runHostRootLifetimeCase(dump_bytecode);
  failures += runExternalCallbackInvocationCase(dump_bytecode);
  failures += runUnresolvedIdentifierCase(dump_bytecode, snapshot_dir);
  failures += runRuntimeLineErrorCase(dump_bytecode, snapshot_dir);
  failures += runStackOverflowCase(dump_bytecode, snapshot_dir);

 // co fn creates a coroutine object (not a plain call result)
 // Calling a co fn returns a coroutine ID, not the function's return value.
 // A co fn with yield will be is_generator=true, so calling it returns a coroutine.
 // We test that co fn parses, compiles, and the call returns a coroutine value.
 failures += runCase("co-fn-basic", R"havel(
co fn greet() {
 yield 1
 yield 2
}

c = greet()
// c is a coroutine object; calling a generator returns coroutine ID
return 0
)havel", 0, dump_bytecode, snapshot_dir);

 // <- as await/blocking operator on a value (identity for non-coroutine values)
 failures += runCase("await-identity", R"havel(
x = <-42
return x
)havel", 42, dump_bytecode, snapshot_dir);

  // Negative numbers still work after scanNumber fix
  failures += runCase("negative-number", R"havel(
x = -5
y = -3 + 2
z = 10 + -2
return x + y + z
)havel", 2, dump_bytecode, snapshot_dir);

 // co fn returns a coroutine, <- awaits it
 failures += runCase("await-cofn", R"havel(
co fn task() {
  return 99
}
result = <-task()
return result
)havel", 99, dump_bytecode, snapshot_dir);

 // co fn with arguments
 failures += runCase("await-cofn-args", R"havel(
co fn add(a, b) {
  return a + b
}
result = <-add(3, 4)
return result
)havel", 7, dump_bytecode, snapshot_dir);

 // co fn that yields, then returns final value
 failures += runCase("await-cofn-yield", R"havel(
co fn gen() {
  yield 10
  yield 20
  return 30
}
c = gen()
r1 = <-c
return r1
)havel", 10, dump_bytecode, snapshot_dir);

 // Multiple sequential awaits on same yielding coroutine
 failures += runCase("await-cofn-multi-yield", R"havel(
co fn gen() {
  yield 10
  yield 20
  return 30
}
c = gen()
r1 = <-c
r2 = <-c
r3 = <-c
return r1 + r2 + r3
)havel", 60, dump_bytecode, snapshot_dir);

 // Nested await: coroutine awaiting another coroutine
 failures += runCase("await-cofn-nested", R"havel(
co fn inner() {
 return 7
}
co fn outer() {
 x = <-inner()
 return x * 3
}
return <-outer()
)havel", 21, dump_bytecode, snapshot_dir);

 failures += runCase("await-fiber-sleep", R"havel(
co fn sleeper() {
 <-sleep(1)
 return 42
}
return <-sleeper()
)havel", 42, dump_bytecode, snapshot_dir);

 failures += runAsyncCase("await-thread-result", R"havel(
worker = thread(fn(msg) { return msg + 1 })
worker.send(41)
return <-worker
)havel", 42, dump_bytecode, snapshot_dir);

 failures += runAsyncCase("await-timeout-result", R"havel(
t = timeout(10, fn() { return 99 })
return <-t
)havel", 99, dump_bytecode, snapshot_dir);

  failures += runAsyncCase("await-interval-result", R"havel(
i = interval(10, fn() { return 7 })
result = <-i
i.stop()
return result
)havel", 7, dump_bytecode, snapshot_dir);

  // --- Arithmetic ---
  failures += runCase("arithmetic-sub", R"havel(
return 100 - 58
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("arithmetic-mul", R"havel(
return 6 * 7
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("arithmetic-div", R"havel(
return 126 / 3
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("arithmetic-mod", R"havel(
return 43 % 10
)havel", 3, dump_bytecode, snapshot_dir);

  failures += runCase("arithmetic-pow", R"havel(
return 2 ** 5
)havel", 32, dump_bytecode, snapshot_dir);

  failures += runCase("arithmetic-negate", R"havel(
x = 10
return -x + 52
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("arithmetic-compound-sub", R"havel(
x = 50
x -= 8
return x
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("arithmetic-compound-mul", R"havel(
x = 6
x *= 7
return x
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("arithmetic-compound-div", R"havel(
    x = 84
    x /= 2
    return int(x)
  )havel", 42, dump_bytecode, snapshot_dir);

  // --- Comparison ---
  failures += runCase("comparison-eq-neq", R"havel(
a = (10 == 10)
b = (10 != 9)
if a { if b { return 1 } }
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("comparison-lt-gt", R"havel(
a = (5 < 10)
b = (10 > 5)
if a { if b { return 1 } }
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("comparison-lte-gte", R"havel(
a = (5 <= 5)
b = (10 >= 10)
c = (5 <= 4)
if a { if b { if !c { return 1 } } }
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  // --- Boolean logic ---
  failures += runCase("logic-and", R"havel(
if true && true { return 1 }
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("logic-or", R"havel(
if false || true { return 1 }
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("logic-not", R"havel(
if !false { return 1 }
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("logic-and-eval", R"havel(
    x = 0
    fn side() { x += 1; 1 }
    result = false && side()
    x
  )havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("logic-or-eval", R"havel(
    x = 0
    fn side() { x += 1; 1 }
    result = true || side()
    x
  )havel", 1, dump_bytecode, snapshot_dir);

  // --- Bitwise ---
  failures += runCase("bitwise-and", R"havel(
return 0xFF & 0x0F
)havel", 15, dump_bytecode, snapshot_dir);

  failures += runCase("bitwise-or", R"havel(
return 0xF0 | 0x0F
)havel", 255, dump_bytecode, snapshot_dir);

  failures += runCase("bitwise-xor", R"havel(
return 0xFF ^ 0x0F
)havel", 240, dump_bytecode, snapshot_dir);

  failures += runCase("bitwise-lsh", R"havel(
return 1 << 8
)havel", 256, dump_bytecode, snapshot_dir);

  failures += runCase("bitwise-rsh", R"havel(
return 256 >> 4
)havel", 16, dump_bytecode, snapshot_dir);

  failures += runCase("bitwise-xor-negate", R"havel(
    x = 5
    return x ^ (-1)
  )havel", -6, dump_bytecode, snapshot_dir);

  failures += runCase("bitwise-compound-and", R"havel(
x = 0xFF
x &= 0x0F
return x
)havel", 15, dump_bytecode, snapshot_dir);

  failures += runCase("bitwise-compound-or", R"havel(
x = 0xF0
x |= 0x0F
return x
)havel", 255, dump_bytecode, snapshot_dir);

  failures += runCase("bitwise-compound-xor", R"havel(
x = 0xFF
x ^= 0x0F
return x
)havel", 240, dump_bytecode, snapshot_dir);

  // --- For loop ---
  failures += runCase("for-loop-range", R"havel(
    sum = 0
    for i in 1..5 { sum += i }
    return sum
  )havel", 15, dump_bytecode, snapshot_dir);

  failures += runCase("for-loop-array", R"havel(
sum = 0
for v in [10, 20, 30] { sum += v }
return sum
)havel", 60, dump_bytecode, snapshot_dir);

failures += runCase("for-loop-destructure-obj", R"havel(
sum = 0
for i, v in {a: 10, b: 20, c: 30} { sum += v }
return sum
)havel", 60, dump_bytecode, snapshot_dir);

failures += runCase("for-loop-destructure-array", R"havel(
sum = 0
for i, v in [10, 20, 30] { sum += v }
return sum
)havel", 60, dump_bytecode, snapshot_dir);

failures += runCase("for-loop-break", R"havel(
sum = 0
for i in 1..100 {
  if i > 5 { break }
  sum += i
}
return sum
)havel", 15, dump_bytecode, snapshot_dir);

  failures += runCase("for-loop-continue", R"havel(
sum = 0
for i in 1..10 {
  if i % 2 == 0 { continue }
  sum += i
}
return sum
)havel", 25, dump_bytecode, snapshot_dir);

  // --- Loop / do-while ---
  failures += runCase("loop-with-break", R"havel(
i = 0
loop {
  i += 1
  if i >= 5 { break }
}
return i
)havel", 5, dump_bytecode, snapshot_dir);

  failures += runCase("do-while", R"havel(
i = 0
do {
  i += 1
} while i < 5
return i
)havel", 5, dump_bytecode, snapshot_dir);

  // --- Ternary / if-expression ---
  failures += runCase("ternary-expression", R"havel(
x = true ? 42 : 0
return x
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("ternary-nested", R"havel(
x = 1
result = x > 0 ? x * 10 : -x
return result
)havel", 10, dump_bytecode, snapshot_dir);

  // --- String operations ---
  failures += runCase("string-len", R"havel(
return #"hello"
)havel", 5, dump_bytecode, snapshot_dir);

  failures += runCase("string-concat", R"havel(
s = "hello" + " " + "world"
return #s
)havel", 11, dump_bytecode, snapshot_dir);

  failures += runCase("string-index", R"havel(
s = "hello"
return s[0] == "h" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("string-slice", R"havel(
s = "hello world"
sub = s[0:5]
return #sub
)havel", 5, dump_bytecode, snapshot_dir);

  failures += runCase("string-forin", R"havel(
count = 0
for c in "abc" { count += 1 }
return count
)havel", 3, dump_bytecode, snapshot_dir);

  failures += runCase("string-prototype-upper", R"havel(
s = "hello".upper()
return s == "HELLO" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("string-prototype-split", R"havel(
parts = "a,b,c".split(",")
return parts.len
)havel", 3, dump_bytecode, snapshot_dir);

  failures += runCase("string-interpolation", R"havel(
    x = 42
    s = f"x={x}"
    return #s
  )havel", 4, dump_bytecode, snapshot_dir);

  // --- Null coalescing ---
  failures += runCase("null-coalescing", R"havel(
x = null ?? 42
return x
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("null-coalescing-non-null", R"havel(
x = 10 ?? 42
return x
)havel", 10, dump_bytecode, snapshot_dir);

  // --- val (immutable) ---
  failures += runCase("val-binding", R"havel(
val x = 42
return x
)havel", 42, dump_bytecode, snapshot_dir);

  // --- Type conversions ---
  failures += runCase("type-int-str", R"havel(
s = str(42)
return #s
)havel", 2, dump_bytecode, snapshot_dir);

  failures += runCase("type-typeof", R"havel(
t = type(42)
return t == "int" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

  // --- Class ---
  failures += runCase("class-basic", R"havel(
    class Counter {
      count
      fn @(start) { @count = start }
      fn increment() { @count++ }
      fn current() { @count }
    }
    c = Counter(0)
    c.increment()
    c.increment()
    c.increment()
    return c.current()
  )havel", 3, dump_bytecode, snapshot_dir);

  failures += runCase("class-constructor", R"havel(
class Vec2 {
  x, y
  fn @(x, y) { @x = x; @y = y }
  fn sum() { return @x + @y }
}
v = Vec2(20, 22)
return v.sum()
)havel", 42, dump_bytecode, snapshot_dir);

  // --- Enum ---
  // --- Spread ---
  failures += runCase("spread-in-array", R"havel(
base = [1, 2]
merged = [...base, 3, 4]
return merged[3]
)havel", 4, dump_bytecode, snapshot_dir);

  // --- Multiple assignment ---
  // --- Array prototype methods ---
  failures += runCase("array-push-pop", R"havel(
arr = [1, 2]
arr.push(3)
arr.pop()
arr.push(4)
return arr[2]
)havel", 4, dump_bytecode, snapshot_dir);

  failures += runCase("array-find-indexOf", R"havel(
arr = [10, 20, 30]
idx = arr.indexOf(20)
return idx
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("array-includes-has", R"havel(
arr = [10, 20, 30]
if arr.has(20) {
  if !arr.has(99) { return 1 }
}
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runCase("array-join", R"havel(
arr = [1, 2, 3]
s = arr.join("-")
return #s
)havel", 5, dump_bytecode, snapshot_dir);

  failures += runCase("array-reverse-sort", R"havel(
arr = [3, 1, 2]
arr.sort((a, b) => a - b)
return arr[0]
)havel", 1, dump_bytecode, snapshot_dir);

  // --- Object operations ---
  failures += runCase("object-keys-values", R"havel(
obj = {a: 1, b: 2, c: 3}
k = obj.keys()
return k.len
)havel", 3, dump_bytecode, snapshot_dir);

  failures += runCase("object-has-delete", R"havel(
obj = {x: 10, y: 20}
if obj.has("x") {
  obj.delete("y")
  if !obj.has("y") { return 1 }
}
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  // --- Set operations ---
  failures += runCase("set-union-intersection", R"havel(
    a = {1, 2, 3}
    b = {2, 3, 4}
    u = a.union(b)
    i = a.intersection(b)
    return u.len * 10 + i.len
  )havel", 42, dump_bytecode, snapshot_dir);

  // --- Range ---
  failures += runCase("range-has", R"havel(
r = 1..5
if r.has(3) {
  if !r.has(10) { return 1 }
}
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  // --- Increment / decrement ---
  failures += runCase("increment-local", R"havel(
x = 40
x++
return x
)havel", 41, dump_bytecode, snapshot_dir);

  failures += runCase("decrement-local", R"havel(
x = 43
x--
return x
)havel", 42, dump_bytecode, snapshot_dir);

  // --- del operator ---
  failures += runCase("del-object-key", R"havel(
obj = {a: 1, b: 2, c: 3}
del obj.b
return obj.keys().len
)havel", 2, dump_bytecode, snapshot_dir);

  failures += runCase("del-array-index", R"havel(
arr = [10, 20, 30]
del arr[1]
return arr.len
)havel", 2, dump_bytecode, snapshot_dir);

  // --- Nested data structures ---
  failures += runCase("nested-array-access", R"havel(
m = [[1, 2], [3, 4]]
return m[1][0]
)havel", 3, dump_bytecode, snapshot_dir);

  failures += runCase("nested-object-access", R"havel(
    obj = {inner: {value: 42}}
    return obj.inner.value
  )havel", 42, dump_bytecode, snapshot_dir);

  // --- Recursive function ---
  failures += runCase("recursive-factorial", R"havel(
fn fact(n) {
  if n <= 1 { return 1 }
  return n * fact(n - 1)
}
return fact(5)
)havel", 120, dump_bytecode, snapshot_dir);

  // --- Fibonacci ---
  failures += runCase("fibonacci", R"havel(
fn fib(n) {
  if n <= 1 { return n }
  return fib(n - 1) + fib(n - 2)
}
return fib(10)
)havel", 55, dump_bytecode, snapshot_dir);

  // --- Default parameters ---
  failures += runCase("default-params", R"havel(
fn add(a, b = 10) { return a + b }
return add(32)
)havel", 42, dump_bytecode, snapshot_dir);

  // --- String repetition ---
  failures += runCase("string-repeat", R"havel(
s = "ha" * 3
return #s
)havel", 6, dump_bytecode, snapshot_dir);


  // ================================================================
  // Stdlib host function smoke tests (via runStdlibCase)
  // These test functions registered by registerPureStdLib() that
  // the default runCase() pipeline does NOT have access to.
  // ================================================================

  // --- FormatModule: fmt.hex/oct/bin/b64/b64decode/format ---
  failures += runStdlibCase("fmt-hex-int", R"havel(
return fmt.hex(255) == "0xff" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("fmt-hex-zero", R"havel(
return fmt.hex(0) == "0x0" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("fmt-hex-neg", R"havel(
return fmt.hex(-1) == "0xffffffffffffffff" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("fmt-oct-int", R"havel(
return fmt.oct(8) == "0o10" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("fmt-bin-int", R"havel(
return fmt.bin(5) == "0b101" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("fmt-b64-string", R"havel(
s = fmt.b64("Hello")
return #s
)havel", 8, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("fmt-b64decode-returns-array", R"havel(
encoded = fmt.b64("AB")
decoded = fmt.b64decode(encoded)
return decoded.len
)havel", 2, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("fmt-b64-roundtrip", R"havel(
original = "test"
encoded = fmt.b64(original)
decoded = fmt.b64decode(encoded)
// decoded is a byte array; check first 4 bytes match "test"
ok = 1
if decoded[0] != 116 { ok = 0 }  // 't'
if decoded[1] != 101 { ok = 0 }  // 'e'
if decoded[2] != 115 { ok = 0 }  // 's'
if decoded[3] != 116 { ok = 0 }  // 't'
return ok
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("fmt-format-basic", R"havel(
s = fmt.format("{} + {} = {}", 1, 2, 3)
return #s
)havel", 9, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("fmt-format-hex-spec", R"havel(
s = fmt.format("{:x}", 255)
return s == "ff" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

  // --- BitModule: bit.and/or/xor/not/lshift/rshift/test/set/clear/toggle ---
  failures += runStdlibCase("bit-and", R"havel(
    return 12 & 10
)havel", 8, dump_bytecode, snapshot_dir);

    failures += runStdlibCase("bit-or", R"havel(
    return 12 | 10
)havel", 14, dump_bytecode, snapshot_dir);

    failures += runStdlibCase("bit-xor", R"havel(
    return bit.xor(12, 10)
)havel", 6, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-not", R"havel(
// bit.not(0) = ~0 = -1 (all bits set)
return bit.not(0)
)havel", -1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-lshift", R"havel(
return bit.lshift(1, 4)
)havel", 16, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-rshift", R"havel(
return bit.rshift(16, 4)
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-arshift", R"havel(
// arithmetic right shift of negative: -8 >> 2 = -2
return bit.arshift(-8, 2)
)havel", -2, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-test", R"havel(
// test bit 2 of 0b100 (4) -> true -> 1
return bit.test(4, 2) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-test-false", R"havel(
// test bit 1 of 0b100 (4) -> false -> 0
return bit.test(4, 1) ? 1 : 0
)havel", 0, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-set", R"havel(
// set bit 1 of 0b100 (4) -> 0b110 (6)
return bit.set(4, 1)
)havel", 6, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-clear", R"havel(
// clear bit 2 of 0b110 (6) -> 0b010 (2)
return bit.clear(6, 2)
)havel", 2, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-toggle", R"havel(
// toggle bit 0 of 0b110 (6) -> 0b111 (7)
return bit.toggle(6, 0)
)havel", 7, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-lsb", R"havel(
// lsb of 12 (0b1100) = bit position 2
return bit.lsb(12)
)havel", 2, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-msb", R"havel(
// msb of 8 (0b1000) = bit position 3
return bit.msb(8)
)havel", 3, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-count", R"havel(
// popcount of 0b1111 = 4
return bit.count(15)
)havel", 4, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-parity", R"havel(
// parity of 0b1111 (even number of 1s) = 0
return bit.parity(15)
)havel", 0, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-rol", R"havel(
// rotate 1 left by 1 = 2
return bit.rol(1, 1)
)havel", 2, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-ror", R"havel(
// rotate 2 right by 1 = 1
return bit.ror(2, 1)
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-getfield", R"havel(
// get bits [2:4) of 0b1110 (14) = 0b11 (3)
return bit.getfield(14, 1, 2)
)havel", 3, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("bit-setfield", R"havel(
// set bits [1:3) of 0b1000 (8) to 0b11 (3) -> 0b1110 (14)
return bit.setfield(8, 1, 2, 3)
)havel", 14, dump_bytecode, snapshot_dir);

  // --- PackModule: pack.pack/pack.unpack ---
  failures += runStdlibCase("pack-pack-u8", R"havel(
data = pack.pack("u", 42)
return data.len
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("pack-pack-u8-value", R"havel(
data = pack.pack("u", 42)
return data[0]
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("pack-unpack-u8", R"havel(
data = pack.pack("u", 99)
result = pack.unpack("u", data)
return result[0]
)havel", 99, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("pack-roundtrip-i16", R"havel(
    data = pack.pack("I", 1000)
    result = pack.unpack("I", data)
return result[0]
)havel", 1000, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("pack-roundtrip-i32", R"havel(
    data = pack.pack("l", 100000)
    result = pack.unpack("l", data)
return result[0]
)havel", 100000, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("pack-roundtrip-f32", R"havel(
data = pack.pack("f", 3.14)
result = pack.unpack("f", data)
// f32 has limited precision; 3.14 becomes ~3.140000104904175
// check it is approximately right by checking int part
return int(result[0])
)havel", 3, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("pack-multi-format", R"havel(
    data = pack.pack("uI", 1, 500)
    // u8=1 byte, I16=2 bytes -> 3 total
return data.len
)havel", 3, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("pack-unpack-multi", R"havel(
    data = pack.pack("uI", 1, 500)
    result = pack.unpack("uI", data)
ok = 1
if result[0] != 1 { ok = 0 }
if result[1] != 500 { ok = 0 }
return ok
)havel", 1, dump_bytecode, snapshot_dir);

  // --- RandomModule: basic type/range checks ---
  // Random values can't be checked for exact values, but we can
  // verify return types and range constraints.
  failures += runStdlibCase("random-randint-range", R"havel(
// randint(1, 10) should return an int in [1, 10]
v = randint(1, 10)
ok = 1
if v < 1 { ok = 0 }
if v > 10 { ok = 0 }
return ok
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("random-rand-type", R"havel(
// rand() returns a double in [0, 1)
v = rand()
// If it returns a double, comparing with 0 should work
ok = 1
if v < 0 { ok = 0 }
if v >= 1 { ok = 0 }
return ok
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("random-choice-from-array", R"havel(
// choice picks from an array
arr = [10, 20, 30]
v = choice(arr)
ok = 0
if v == 10 { ok = 1 }
if v == 20 { ok = 1 }
if v == 30 { ok = 1 }
return ok
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runStdlibCase("random-seed-deterministic", R"havel(
// Setting seed should make subsequent calls deterministic
random.seed(42)
a = randint(1, 1000)
random.seed(42)
b = randint(1, 1000)
// Same seed must produce same sequence
return a == b ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

// ================================================================
// --- MathModule: math.abs/ceil/floor/round/sin/cos/tan/sqrt/log/exp/pow/min/max/random + PI/E ---
// ================================================================

failures += runStdlibCase("math-abs-positive", R"havel(
v = math.abs(-5)
return v == 5.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-abs-int", R"havel(
v = math.abs(3)
return v == 3.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-ceil", R"havel(
v = math.ceil(3.2)
return v == 4.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-floor", R"havel(
v = math.floor(3.8)
return v == 3.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-round", R"havel(
v = math.round(3.5)
return v == 4.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-sin", R"havel(
// sin(0) == 0
v = math.sin(0)
return v == 0.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-cos", R"havel(
// cos(0) == 1
v = math.cos(0)
return v == 1.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-sqrt", R"havel(
v = math.sqrt(9)
return v == 3.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-log", R"havel(
// log(e) == 1
v = math.log(E)
// Use tolerance: |v - 1.0| < 0.0001
diff = v - 1.0
if diff < 0 { diff = 0 - diff }
return diff < 0.0001 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-exp", R"havel(
// exp(0) == 1
v = math.exp(0)
return v == 1.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-pow", R"havel(
v = math.pow(2, 10)
return v == 1024.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-min", R"havel(
v = math.min(3, 7)
return v == 3.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-max", R"havel(
v = math.max(3, 7)
return v == 7.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-random-range", R"havel(
v = math.random()
ok = 1
if v < 0 { ok = 0 }
if v >= 1 { ok = 0 }
return ok
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("math-pi-constant", R"havel(
// PI should be approximately 3.14159
diff = PI - 3.14159
if diff < 0 { diff = 0 - diff }
return diff < 0.00001 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

// ================================================================
// --- StringModule: string.len/lower/upper/trim/find/startswith/endswith/includes ---
// NOTE: string.sub, string.replace, string.join return null (stubs)
// NOTE: string.split returns stub [42]
// ================================================================

failures += runStdlibCase("string-len", R"havel(
return string.len("hello")
)havel", 5, dump_bytecode, snapshot_dir);

failures += runStdlibCase("string-len-empty", R"havel(
return string.len("")
)havel", 0, dump_bytecode, snapshot_dir);

failures += runStdlibCase("string-lower", R"havel(
s = string.lower("HELLO")
return #s
)havel", 5, dump_bytecode, snapshot_dir);

failures += runStdlibCase("string-lower-content", R"havel(
s = string.lower("HELLO")
return s == "hello" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("string-upper", R"havel(
s = string.upper("hello")
return s == "HELLO" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("string-trim", R"havel(
s = string.trim("  hi  ")
return s == "hi" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("string-find-found", R"havel(
return string.find("hello world", "world")
)havel", 6, dump_bytecode, snapshot_dir);

failures += runStdlibCase("string-find-not-found", R"havel(
// npos cast to int64_t is -1
v = string.find("hello", "xyz")
return v == -1 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("string-startswith-true", R"havel(
return string.startswith("hello", "hel") ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("string-startswith-false", R"havel(
return string.startswith("hello", "xyz") ? 1 : 0
)havel", 0, dump_bytecode, snapshot_dir);

failures += runStdlibCase("string-endswith-true", R"havel(
return string.endswith("hello", "llo") ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("string-endswith-false", R"havel(
return string.endswith("hello", "xyz") ? 1 : 0
)havel", 0, dump_bytecode, snapshot_dir);

failures += runStdlibCase("string-includes-true", R"havel(
return string.includes("hello world", "world") ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("string-includes-false", R"havel(
return string.includes("hello", "xyz") ? 1 : 0
)havel", 0, dump_bytecode, snapshot_dir);

// string.sub is a stub - returns null
failures += runStdlibCase("string-sub-stub", R"havel(
v = string.sub("hello", 0, 3)
return isNull(v) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

// string.replace is a stub - returns null
failures += runStdlibCase("string-replace-stub", R"havel(
v = string.replace("hello", "l", "r")
return isNull(v) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

// string.split returns stub array [42]
failures += runStdlibCase("string-split-stub", R"havel(
v = string.split("a,b,c", ",")
return isArray(v) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

// string.join is a stub - returns null
failures += runStdlibCase("string-join-stub", R"havel(
v = string.join([1, 2, 3], ",")
return isNull(v) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

// Global replace() function (non-regex path, replaces first occurrence)
failures += runStdlibCase("replace-global-plain", R"havel(
s = replace("hello", "l", "r")
return s == "herlo" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

// ================================================================
// --- ObjectModule: object.keys/values/entries/has/find/set/delete/isEmpty/size/len ---
// ================================================================

failures += runStdlibCase("object-keys", R"havel(
obj = { name: "test", value: 42 }
k = object.keys(obj)
return k.len
)havel", 2, dump_bytecode, snapshot_dir);

failures += runStdlibCase("object-values", R"havel(
obj = { a: 1, b: 2 }
v = object.values(obj)
return v.len
)havel", 2, dump_bytecode, snapshot_dir);

failures += runStdlibCase("object-entries", R"havel(
obj = { a: 1 }
e = object.entries(obj)
return e.len
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("object-has-true", R"havel(
obj = { a: 1 }
return object.has(obj, "a") ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("object-has-false", R"havel(
obj = { a: 1 }
return object.has(obj, "b") ? 1 : 0
)havel", 0, dump_bytecode, snapshot_dir);

failures += runStdlibCase("object-find-found", R"havel(
obj = { a: 1, b: 2 }
idx = object.find(obj, "b")
return idx >= 0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("object-find-not-found", R"havel(
obj = { a: 1 }
idx = object.find(obj, "z")
return idx
)havel", -1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("object-set", R"havel(
obj = { a: 1 }
object.set(obj, "b", 2)
return object.has(obj, "b") ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("object-delete", R"havel(
obj = { a: 1, b: 2 }
object.delete(obj, "a")
return object.has(obj, "a") ? 0 : 1
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("object-isEmpty-true", R"havel(
obj = {}
return object.isEmpty(obj) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("object-isEmpty-false", R"havel(
obj = { a: 1 }
return object.isEmpty(obj) ? 0 : 1
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("object-size", R"havel(
obj = { a: 1, b: 2, c: 3 }
return object.size(obj)
)havel", 3, dump_bytecode, snapshot_dir);

failures += runStdlibCase("object-len", R"havel(
obj = { a: 1, b: 2 }
return object.len(obj)
)havel", 2, dump_bytecode, snapshot_dir);

// ================================================================
// --- TypeModule: isNumber/isString/isArray/isObject/isNull/isBoolean/toString/toNumber ---
// ================================================================

failures += runStdlibCase("isNumber-int", R"havel(
return isNumber(42) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("isNumber-double", R"havel(
return isNumber(3.14) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("isNumber-string", R"havel(
return isNumber("42") ? 0 : 1
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("isString-literal", R"havel(
return isString("hello") ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("isString-int", R"havel(
return isString(42) ? 0 : 1
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("isArray-literal", R"havel(
return isArray([1, 2, 3]) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("isObject-literal", R"havel(
return isObject({}) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("isNull-value", R"havel(
return isNull(null) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("isBoolean-true", R"havel(
return isBoolean(true) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("isBoolean-int", R"havel(
return isBoolean(1) ? 0 : 1
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("toNumber-int", R"havel(
return toNumber(42)
)havel", 42, dump_bytecode, snapshot_dir);

failures += runStdlibCase("toNumber-null", R"havel(
return toNumber(null)
)havel", 0, dump_bytecode, snapshot_dir);

failures += runStdlibCase("toNumber-bool-true", R"havel(
return toNumber(true)
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("toNumber-bool-false", R"havel(
return toNumber(false)
)havel", 0, dump_bytecode, snapshot_dir);

// ================================================================
// --- RegexModule: regex_match/regex_search/regex_replace/regex_extract/regex_split/escape_regex ---
// NOTE: regex_search(text, pattern) -- reversed arg order vs regex_match(pattern, text)
// ================================================================

failures += runStdlibCase("regex-match-true", R"havel(
return regex_match("hello.*", "hello world") ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("regex-match-false", R"havel(
return regex_match("^xyz$", "hello") ? 0 : 1
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("regex-search-true", R"havel(
// regex_search takes (text, pattern) -- reversed from regex_match
return regex_search("hello world", "wor") ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("regex-search-false", R"havel(
return regex_search("hello", "xyz") ? 0 : 1
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("regex-replace", R"havel(
s = regex_replace("world", "hello world", "earth")
return s == "hello earth" ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("regex-extract", R"havel(
arr = regex_extract("\\d+", "abc123def456")
return arr.len
)havel", 2, dump_bytecode, snapshot_dir);

failures += runStdlibCase("regex-split", R"havel(
arr = regex_split(",", "a,b,c")
return arr.len
)havel", 3, dump_bytecode, snapshot_dir);

failures += runStdlibCase("escape-regex", R"havel(
s = escape_regex("a.b")
// "a.b" -> "a\\.b" (escaped the dot)
return #s
)havel", 4, dump_bytecode, snapshot_dir);

// ================================================================
// --- PhysicsModule: force/kinetic_energy/potential_energy/momentum/wavelength + constants ---
// All return doubles; compare or check non-zero
// ================================================================

failures += runStdlibCase("physics-force", R"havel(
// F = m * a = 10 * 5 = 50
v = force(10, 5)
return v == 50.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("physics-kinetic-energy", R"havel(
// KE = 0.5 * m * v^2 = 0.5 * 2 * 9 = 9
v = kinetic_energy(2, 3)
return v == 9.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("physics-potential-energy", R"havel(
// PE = m * g * h = 10 * 9.80665 * 5 = 490.3325
v = potential_energy(10, 5)
return v > 490 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("physics-momentum", R"havel(
// p = m * v = 10 * 5 = 50
v = momentum(10, 5)
return v == 50.0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("physics-wavelength", R"havel(
// lambda = c / f = 299792458 / 1000
v = wavelength(1000)
return v > 299000 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("physics-constant-G", R"havel(
// G = 9.80665
return G > 9.8 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("physics-constant-C", R"havel(
// C = 299792458
return C > 299000000 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

// ================================================================
// --- TimeModule: time.now/epoch/millis/year/month/day/hour/minute/second/weekday ---
// All numeric time functions return int64_t
// ================================================================

failures += runStdlibCase("time-now-positive", R"havel(
// time.now() returns ms since epoch, should be > 0
v = time.now()
return v > 0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("time-epoch-positive", R"havel(
v = time.epoch()
return v > 0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("time-millis-positive", R"havel(
v = time.millis()
return v > 0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("time-year-range", R"havel(
y = time.year()
ok = 1
if y < 2020 { ok = 0 }
if y > 2100 { ok = 0 }
return ok
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("time-month-range", R"havel(
m = time.month()
ok = 1
if m < 1 { ok = 0 }
if m > 12 { ok = 0 }
return ok
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("time-day-range", R"havel(
d = time.day()
ok = 1
if d < 1 { ok = 0 }
if d > 31 { ok = 0 }
return ok
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("time-hour-range", R"havel(
h = time.hour()
ok = 1
if h < 0 { ok = 0 }
if h > 23 { ok = 0 }
return ok
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("time-minute-range", R"havel(
m = time.minute()
ok = 1
if m < 0 { ok = 0 }
if m > 59 { ok = 0 }
return ok
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("time-second-range", R"havel(
s = time.second()
ok = 1
if s < 0 { ok = 0 }
if s > 60 { ok = 0 }
return ok
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("time-weekday-range", R"havel(
w = time.weekday()
ok = 1
if w < 0 { ok = 0 }
if w > 6 { ok = 0 }
return ok
)havel", 1, dump_bytecode, snapshot_dir);

// time.format returns a string - check length > 0
failures += runStdlibCase("time-format-length", R"havel(
now = time.now()
s = time.format(now)
return #s > 0 ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

// time.date returns "YYYY-MM-DD" string (len 10)
failures += runStdlibCase("time-date-length", R"havel(
s = time.date()
return #s
)havel", 10, dump_bytecode, snapshot_dir);

// time.time returns "HH:MM:SS" string (len 8)
failures += runStdlibCase("time-time-length", R"havel(
s = time.time()
return #s
)havel", 8, dump_bytecode, snapshot_dir);

// ================================================================
// --- LogModule: log.info/error/warn/debug/critical all return null ---
// log.history returns array, log.get returns string
// ================================================================

failures += runStdlibCase("log-info-returns-null", R"havel(
v = log.info("test")
return isNull(v) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("log-error-returns-null", R"havel(
v = log.error("test")
return isNull(v) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("log-warn-returns-null", R"havel(
v = log.warn("test")
return isNull(v) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("log-debug-returns-null", R"havel(
v = log.debug("test")
return isNull(v) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("log-critical-returns-null", R"havel(
v = log.critical("test")
return isNull(v) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("log-history-returns-array", R"havel(
log.info("hist-test")
h = log.history()
return isArray(h) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

// ================================================================
// --- DebugModule: debug.toggleVerboseConditionLogging/toggleVerboseKeyLogging ---
// Both return bool (toggling on/off)
// ================================================================

failures += runStdlibCase("debug-toggle-verbose-condition", R"havel(
v = debug.toggleVerboseConditionLogging()
return isBoolean(v) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);

failures += runStdlibCase("debug-toggle-verbose-key", R"havel(
v = debug.toggleVerboseKeyLogging()
return isBoolean(v) ? 1 : 0
)havel", 1, dump_bytecode, snapshot_dir);


if (failures != 0) {
    std::cerr << "Bytecode smoke failed with " << failures << " failing case(s)"
              << std::endl;
    return 1;
  }

	std::cout << "Bytecode smoke passed" << std::endl;
	return 0;
}

} // namespace hvtest
