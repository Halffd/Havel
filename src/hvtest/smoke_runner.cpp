#include "smoke_runner.hpp"
#include "havel-lang/compiler/core/ByteCompiler.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/parser/Parser.h"

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

    // any.* dispatch functions for member access and array HOFs
    havel::compiler::VM *vm_ptr = nullptr;
    options.vm_setup = [&](havel::compiler::VM &vm) { vm_ptr = &vm; };
    std::unordered_map<std::string, std::deque<Value>> channels;
    std::unordered_map<std::string, Value> thread_callbacks;
    options.host_functions["any.get"] = [&](const std::vector<Value> &args) {
      if (args.size() < 2 || !vm_ptr) return Value::makeNull();
      const auto &obj = args[0];
      const auto &key = args[1];
      if (obj.isArrayId() && key.isInt()) {
        auto arrRef = havel::compiler::ArrayRef{obj.asArrayId()};
        auto idx = static_cast<size_t>(key.asInt());
        if (idx < vm_ptr->getHostArrayLength(arrRef)) {
          return vm_ptr->getHostArrayValue(arrRef, idx);
        }
        return Value::makeNull();
      }
      if (obj.isObjectId()) {
        auto objRef = havel::compiler::ObjectRef{obj.asObjectId(), true};
        std::string keyStr = vm_ptr->resolveStringKey(key);
        return vm_ptr->getHostObjectField(objRef, keyStr);
      }
      return Value::makeNull();
    };
    options.host_functions["any.len"] = [&](const std::vector<Value> &args) {
      if (args.empty() || !vm_ptr) return Value::makeInt(0);
      if (args[0].isArrayId()) return Value::makeInt(static_cast<int64_t>(vm_ptr->getHostArrayLength(havel::compiler::ArrayRef{args[0].asArrayId()})));
      return Value::makeInt(0);
    };
    options.host_functions["any.map"] = [&](const std::vector<Value> &args) {
      if (args.size() < 2 || !vm_ptr || !args[0].isArrayId()) return Value::makeNull();
      auto arrRef = havel::compiler::ArrayRef{args[0].asArrayId()};
      size_t len = vm_ptr->getHostArrayLength(arrRef);
      auto out = vm_ptr->createHostArray();
      for (size_t i = 0; i < len; i++) {
        auto val = vm_ptr->getHostArrayValue(arrRef, i);
        auto mapped = vm_ptr->callFunction(args[1], {val});
        vm_ptr->pushHostArrayValue(out, mapped);
      }
      return Value::makeArrayId(out.id);
    };
    options.host_functions["any.filter"] = [&](const std::vector<Value> &args) {
      if (args.size() < 2 || !vm_ptr || !args[0].isArrayId()) return Value::makeNull();
      auto arrRef = havel::compiler::ArrayRef{args[0].asArrayId()};
      size_t len = vm_ptr->getHostArrayLength(arrRef);
      auto out = vm_ptr->createHostArray();
      auto truthy = [](const Value &v) {
        if (v.isNull()) return false;
        if (v.isBool()) return v.asBool();
        if (v.isInt()) return v.asInt() != 0;
        if (v.isDouble()) return v.asDouble() != 0.0;
        return true;
      };
      for (size_t i = 0; i < len; i++) {
        auto val = vm_ptr->getHostArrayValue(arrRef, i);
        auto result = vm_ptr->callFunction(args[1], {val});
        if (truthy(result)) vm_ptr->pushHostArrayValue(out, val);
      }
      return Value::makeArrayId(out.id);
    };
    options.host_functions["any.reduce"] = [&](const std::vector<Value> &args) {
      if (args.size() < 3 || !vm_ptr || !args[0].isArrayId()) return Value::makeNull();
      auto arrRef = havel::compiler::ArrayRef{args[0].asArrayId()};
      size_t len = vm_ptr->getHostArrayLength(arrRef);
      Value acc = args[2];
      for (size_t i = 0; i < len; i++) {
        auto val = vm_ptr->getHostArrayValue(arrRef, i);
        acc = vm_ptr->callFunction(args[1], {acc, val});
      }
      return acc;
    };
    options.host_functions["any.foreach"] = [&](const std::vector<Value> &args) {
      if (args.size() < 2 || !vm_ptr || !args[0].isArrayId()) return Value::makeNull();
      auto arrRef = havel::compiler::ArrayRef{args[0].asArrayId()};
      size_t len = vm_ptr->getHostArrayLength(arrRef);
      for (size_t i = 0; i < len; i++) {
        auto val = vm_ptr->getHostArrayValue(arrRef, i);
        vm_ptr->callFunction(args[1], {val});
      }
      return Value::makeNull();
    };
    options.host_functions["any.set"] = [&](const std::vector<Value> &args) {
      if (args.size() < 3 || !vm_ptr || !args[0].isArrayId() || !args[1].isInt()) return Value::makeNull();
      auto arrRef = havel::compiler::ArrayRef{args[0].asArrayId()};
      vm_ptr->setHostArrayValue(arrRef, static_cast<size_t>(args[1].asInt()), args[2]);
      return args[2];
    };

    const auto result =
        havel::compiler::runBytecodePipeline(source, "__main__", options);
    if (!equalsInt(result.return_value, expected)) {
      std::cerr << "[FAIL] " << name << ": expected " << expected
                << " but got non-matching result" << std::endl;
      return 1;
    }

    if (!result.snapshot.artifact_path.empty()) {
      std::cout << "[SNAPSHOT] " << name << ": " << result.snapshot.artifact_path
                << std::endl;
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
    std::unordered_map<std::string, std::deque<Value>> channels;
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
    auto async_obj = vm.createHostObject();
    vm.setHostObjectField(async_obj, "run", Value::makeHostFuncId(vm.getHostFunctionIndex("async.run")));
    vm.setHostObjectField(async_obj, "await", Value::makeHostFuncId(vm.getHostFunctionIndex("async.await")));
    vm.setHostObjectField(async_obj, "sleep", Value::makeHostFuncId(vm.getHostFunctionIndex("async.sleep")));
    vm.setHostObjectField(async_obj, "channel", Value::makeHostFuncId(vm.getHostFunctionIndex("async.channel")));
    vm.setHostObjectField(async_obj, "send", Value::makeHostFuncId(vm.getHostFunctionIndex("async.send")));
    vm.setHostObjectField(async_obj, "receive", Value::makeHostFuncId(vm.getHostFunctionIndex("async.receive")));
    vm.setGlobal("async", Value::makeObjectId(async_obj.id));
    auto thread_obj = vm.createHostObject();
    vm.setHostObjectField(thread_obj, "spawn", Value::makeHostFuncId(vm.getHostFunctionIndex("thread")));
    vm.setGlobal("thread", Value::makeObjectId(thread_obj.id));
    auto interval_obj = vm.createHostObject();
    vm.setHostObjectField(interval_obj, "start", Value::makeHostFuncId(vm.getHostFunctionIndex("interval")));
    vm.setGlobal("interval", Value::makeObjectId(interval_obj.id));
    auto timeout_obj = vm.createHostObject();
    vm.setHostObjectField(timeout_obj, "start", Value::makeHostFuncId(vm.getHostFunctionIndex("timeout")));
    vm.setGlobal("timeout", Value::makeObjectId(timeout_obj.id));
};

options.host_functions["async.run"] =
        [&](const std::vector<Value> &args) {
          if (!vm_ptr) {
            throw std::runtime_error("async.run vm unavailable");
          }
          // async.run(closure) compiles as any.run(async_obj, closure)
          // so the closure is args[1]
          if (args.size() < 2) {
            throw std::runtime_error("async.run requires callback");
          }
          const auto &closure = args[1];

          // Execute closure synchronously (smoke test is single-threaded)
          if (closure.isClosureId() || closure.isFunctionObjId()) {
            return vm_ptr->call(closure, {});
          }
          return Value::makeNull();
        };
    // any.run alias - async.run(x) compiles as any.run(x) via member dispatch
    options.host_functions["any.run"] = options.host_functions["async.run"];
    // any.await - just return the value (async.run already computed it)
    options.host_functions["any.await"] = [&](const std::vector<Value> &args) {
      if (args.empty() || !vm_ptr) return Value::makeNull();
      return args[0];
    };
    options.host_functions["async.await"] =
        [&](const std::vector<Value> &args) {
          if (args.empty()) {
            throw std::runtime_error("async.await requires task id");
          }
          return args[0];
        };
    options.host_functions["await"] = options.host_functions["async.await"];
options.host_functions["async.channel"] =
[&](const std::vector<Value> &args) {
    size_t start = 0;
    if (!args.empty() && args[0].isObjectId()) start = 1;
    if (args.size() <= start || !args[start].isStringValId()) {
        throw std::runtime_error("async.channel requires name");
    }
    const auto &name = vm_ptr->getCurrentChunk()->getString(args[start].asStringValId());
    channels[name] = std::deque<Value>();
    return Value(true);
};
options.host_functions["async.send"] =
[&](const std::vector<Value> &args) {
    size_t start = 0;
    if (!args.empty() && args[0].isObjectId()) start = 1;
    if (args.size() <= start + 1 || !args[start].isStringValId()) {
        throw std::runtime_error("async.send requires name + value");
    }
    const auto &name = vm_ptr->getCurrentChunk()->getString(args[start].asStringValId());
    channels[name].push_back(args[start + 1]);
    return Value(true);
};
options.host_functions["async.receive"] =
[&](const std::vector<Value> &args) {
    size_t start = 0;
    if (!args.empty() && args[0].isObjectId()) start = 1;
    if (args.size() <= start || !args[start].isStringValId()) {
            throw std::runtime_error("async.receive requires name");
          }
          const auto &name = vm_ptr->getCurrentChunk()->getString(args[0].asStringValId());
          auto it = channels.find(name);
          if (it == channels.end() || it->second.empty()) {
            return Value::makeNull();
          }
          auto value = it->second.front();
          it->second.pop_front();
          return value;
        };
    options.host_functions["async.tryReceive"] =
        options.host_functions["async.receive"];

    options.host_functions["thread"] = [&](const std::vector<Value> &args) {
      if (!vm_ptr || args.empty()) {
        throw std::runtime_error("thread requires callback");
      }
    std::string id = "thread-" + std::to_string(next_task_id++);
    thread_callbacks[id] = args[0];
    thread_running[id] = true;
    thread_paused[id] = false;
    auto obj = vm_ptr->createHostObject();
    vm_ptr->setHostObjectField(obj, "__kind", Value::makeInt(0));
    vm_ptr->setHostObjectField(obj, "__id", Value::makeInt(static_cast<int64_t>(thread_callbacks.size())));
    vm_ptr->setHostObjectField(obj, "send", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("thread.send")));
    vm_ptr->setHostObjectField(obj, "pause", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("thread.pause")));
    vm_ptr->setHostObjectField(obj, "resume", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("thread.resume")));
    vm_ptr->setHostObjectField(obj, "running", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("thread.running")));
    return Value::makeObjectId(obj.id);
    };
    options.host_functions["thread.send"] = [&](const std::vector<Value> &args) {
      if (!vm_ptr || args.size() < 2 || !args[0].isObjectId()) {
        return Value(false);
      }
      auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
      auto idv = vm_ptr->getHostObjectField(obj, "__id");
      if (!idv.isInt()) return Value(false);
      // TODO: map id back to thread_callbacks key
      (void)idv;
      return Value(false);
    };
    options.host_functions["thread.pause"] = [&](const std::vector<Value> &args) {
      if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
      auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
      auto idv = vm_ptr->getHostObjectField(obj, "__id");
      if (!idv.isInt()) return Value(false);
      // TODO: map id back to thread_paused key
      (void)idv;
      return Value(false);
    };
    options.host_functions["thread.resume"] = [&](const std::vector<Value> &args) {
      if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
      auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
      auto idv = vm_ptr->getHostObjectField(obj, "__id");
      if (!idv.isInt()) return Value(false);
      // TODO: map id back to thread_paused key
      (void)idv;
      return Value(false);
    };
    options.host_functions["thread.stop"] = [&](const std::vector<Value> &args) {
      if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
      auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
      auto idv = vm_ptr->getHostObjectField(obj, "__id");
      if (!idv.isInt()) return Value(false);
      // TODO: map id back to thread_running key
      (void)idv;
      return Value(false);
    };
    options.host_functions["thread.running"] = [&](const std::vector<Value> &args) {
      if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
      auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
      auto idv = vm_ptr->getHostObjectField(obj, "__id");
      if (!idv.isInt()) return Value(false);
      // TODO: map id back to thread_running key
      (void)idv;
      return Value(false);
    };

    options.host_functions["interval"] = [&](const std::vector<Value> &args) {
    if (!vm_ptr || args.size() < 2) throw std::runtime_error("interval requires delay + callback");
    std::string id = "interval-" + std::to_string(next_task_id++);
    interval_callbacks[id] = args[1];
    interval_running[id] = true;
    interval_paused[id] = false;
    (void)vm_ptr->call(interval_callbacks[id], {});
    auto obj = vm_ptr->createHostObject();
    vm_ptr->setHostObjectField(obj, "__kind", Value::makeInt(1));
    vm_ptr->setHostObjectField(obj, "__id", Value::makeInt(static_cast<int64_t>(interval_callbacks.size())));
    vm_ptr->setHostObjectField(obj, "pause", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("interval.pause")));
    vm_ptr->setHostObjectField(obj, "resume", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("interval.resume")));
    vm_ptr->setHostObjectField(obj, "stop", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("interval.stop")));
    return Value::makeObjectId(obj.id);
    };
    options.host_functions["interval.pause"] = [&](const std::vector<Value> &args) {
      if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
      auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
      auto idv = vm_ptr->getHostObjectField(obj, "__id");
      if (!idv.isInt()) return Value(false);
      // TODO: map id back to interval_paused key
      (void)idv;
      return Value(false);
    };
    options.host_functions["interval.resume"] = [&](const std::vector<Value> &args) {
      if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
      auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
      auto idv = vm_ptr->getHostObjectField(obj, "__id");
      if (!idv.isInt()) return Value(false);
      // TODO: map id back to interval_paused/interval_running key
      (void)idv;
      return Value(false);
    };
    options.host_functions["interval.stop"] = [&](const std::vector<Value> &args) {
      if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
      auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
      auto idv = vm_ptr->getHostObjectField(obj, "__id");
      if (!idv.isInt()) return Value(false);
      // TODO: map id back to interval_running key
      (void)idv;
      return Value(false);
    };

    options.host_functions["timeout"] = [&](const std::vector<Value> &args) {
    if (!vm_ptr || args.size() < 2) throw std::runtime_error("timeout requires delay + callback");
    std::string id = "timeout-" + std::to_string(next_task_id++);
    timeout_callbacks[id] = args[1];
    timeout_running[id] = true;
    (void)vm_ptr->call(timeout_callbacks[id], {});
    timeout_running[id] = false;
    auto obj = vm_ptr->createHostObject();
    vm_ptr->setHostObjectField(obj, "__kind", Value::makeInt(2));
    vm_ptr->setHostObjectField(obj, "__id", Value::makeInt(static_cast<int64_t>(timeout_callbacks.size())));
    vm_ptr->setHostObjectField(obj, "cancel", Value::makeHostFuncId(vm_ptr->getHostFunctionIndex("timeout.cancel")));
    return Value::makeObjectId(obj.id);
    };
    options.host_functions["timeout.cancel"] = [&](const std::vector<Value> &args) {
      if (!vm_ptr || args.empty() || !args[0].isObjectId()) return Value(false);
      auto obj = havel::compiler::ObjectRef{args[0].asObjectId(), true};
      auto idv = vm_ptr->getHostObjectField(obj, "__id");
      if (!idv.isInt()) return Value(false);
      // TODO: map id back to timeout_running key
      (void)idv;
      return Value(false);
    };

    options.host_functions["object.send"] = options.host_functions["thread.send"];
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
      auto idv = vm_ptr->getHostObjectField(obj, "__id");
      if (!kind.isInt() || !idv.isInt()) return Value(false);
      // TODO: map id back to running maps
      (void)kind;
      (void)idv;
      return Value(false);
    };
    options.host_functions["any.send"] = options.host_functions["object.send"];
    options.host_functions["any.pause"] = options.host_functions["object.pause"];
    options.host_functions["any.resume"] = options.host_functions["object.resume"];
    options.host_functions["any.stop"] = options.host_functions["object.stop"];
    options.host_functions["any.cancel"] = options.host_functions["object.cancel"];
    options.host_functions["any.running"] = options.host_functions["object.running"];

    // any.* dispatch functions for member access and array HOFs
    options.host_functions["any.get"] = [&](const std::vector<Value> &args) {
      if (args.size() < 2) return Value::makeNull();
      const auto &obj = args[0];
      const auto &key = args[1];
      if (obj.isArrayId() && key.isInt()) {
        auto arrRef = havel::compiler::ArrayRef{obj.asArrayId()};
        if (!vm_ptr) return Value::makeNull();
        auto idx = static_cast<size_t>(key.asInt());
        if (idx < vm_ptr->getHostArrayLength(arrRef)) {
          return vm_ptr->getHostArrayValue(arrRef, idx);
        }
        return Value::makeNull();
      }
      if (obj.isObjectId() && key.isStringValId()) {
        auto objRef = havel::compiler::ObjectRef{obj.asObjectId(), true};
        if (!vm_ptr) return Value::makeNull();
        return vm_ptr->getHostObjectField(objRef, "<string:" + std::to_string(key.asStringValId()) + ">");
      }
      return Value::makeNull();
    };
    options.host_functions["any.len"] = [&](const std::vector<Value> &args) {
      if (args.empty()) return Value::makeInt(0);
      if (!vm_ptr) return Value::makeInt(0);
      if (args[0].isArrayId()) return Value::makeInt(static_cast<int64_t>(vm_ptr->getHostArrayLength(havel::compiler::ArrayRef{args[0].asArrayId()})));
      if (args[0].isStringValId()) return Value::makeInt(0); // TODO: string length
      return Value::makeInt(0);
    };
    options.host_functions["any.map"] = [&](const std::vector<Value> &args) {
      if (args.size() < 2 || !vm_ptr) return Value::makeNull();
      if (!args[0].isArrayId()) return Value::makeNull();
      auto arrRef = havel::compiler::ArrayRef{args[0].asArrayId()};
      size_t len = vm_ptr->getHostArrayLength(havel::compiler::ArrayRef{args[0].asArrayId()});
      auto out = vm_ptr->createHostArray();
      for (size_t i = 0; i < len; i++) {
        auto val = vm_ptr->getHostArrayValue(arrRef, i);
        auto mapped = vm_ptr->callFunction(args[1], {val});
        vm_ptr->pushHostArrayValue(out, mapped);
      }
      return Value::makeArrayId(out.id);
    };
    options.host_functions["any.filter"] = [&](const std::vector<Value> &args) {
      if (args.size() < 2 || !vm_ptr) return Value::makeNull();
      if (!args[0].isArrayId()) return Value::makeNull();
      auto arrRef = havel::compiler::ArrayRef{args[0].asArrayId()};
      size_t len = vm_ptr->getHostArrayLength(havel::compiler::ArrayRef{args[0].asArrayId()});
      auto out = vm_ptr->createHostArray();
      auto truthy = [](const Value &v) {
        if (v.isNull()) return false;
        if (v.isBool()) return v.asBool();
        if (v.isInt()) return v.asInt() != 0;
        if (v.isDouble()) return v.asDouble() != 0.0;
        return true;
      };
      for (size_t i = 0; i < len; i++) {
        auto val = vm_ptr->getHostArrayValue(arrRef, i);
        auto result = vm_ptr->callFunction(args[1], {val});
        if (truthy(result)) vm_ptr->pushHostArrayValue(out, val);
      }
      return Value::makeArrayId(out.id);
    };
    options.host_functions["any.reduce"] = [&](const std::vector<Value> &args) {
      if (args.size() < 3 || !vm_ptr) return Value::makeNull();
      if (!args[0].isArrayId()) return Value::makeNull();
      auto arrRef = havel::compiler::ArrayRef{args[0].asArrayId()};
      size_t len = vm_ptr->getHostArrayLength(havel::compiler::ArrayRef{args[0].asArrayId()});
      Value acc = args[2];
      for (size_t i = 0; i < len; i++) {
        auto val = vm_ptr->getHostArrayValue(arrRef, i);
        acc = vm_ptr->callFunction(args[1], {acc, val});
      }
      return acc;
    };
    options.host_functions["any.foreach"] = [&](const std::vector<Value> &args) {
      if (args.size() < 2 || !vm_ptr) return Value::makeNull();
      if (!args[0].isArrayId()) return Value::makeNull();
      auto arrRef = havel::compiler::ArrayRef{args[0].asArrayId()};
      size_t len = vm_ptr->getHostArrayLength(havel::compiler::ArrayRef{args[0].asArrayId()});
      for (size_t i = 0; i < len; i++) {
        auto val = vm_ptr->getHostArrayValue(arrRef, i);
        vm_ptr->callFunction(args[1], {val});
      }
      return Value::makeNull();
    };
    options.host_functions["any.set"] = [&](const std::vector<Value> &args) {
      if (args.size() < 3 || !vm_ptr) return Value::makeNull();
      if (args[0].isArrayId() && args[1].isInt()) {
        auto arrRef = havel::compiler::ArrayRef{args[0].asArrayId()};
        auto idx = static_cast<size_t>(args[1].asInt());
        vm_ptr->setHostArrayValue(arrRef, idx, args[2]);
        return args[2];
      }
      return Value::makeNull();
    };

    // Async/concurrency stubs for smoke tests
    options.host_functions["any.run"] = [&](const std::vector<Value> &args) {
      // any.run(obj, callback) - execute callback synchronously and return result
      // Method calls pass the object as args[0], callback as args[1]
      if (args.size() < 2 || !vm_ptr) return Value::makeNull();
      return vm_ptr->call(args[1], {});
    };
    options.host_functions["any.channel"] = [&](const std::vector<Value> &args) {
      // any.channel(name) - create a channel object
      if (!vm_ptr) return Value::makeNull();
      auto obj = vm_ptr->createHostObject();
      if (!args.empty() && args[0].isStringValId()) {
        vm_ptr->setHostObjectField(obj, "name", args[0]);
      }
      return Value::makeObjectId(obj.id);
    };
    options.host_functions["any.gc"] = [&](const std::vector<Value> &args) {
      // any.gc() - trigger garbage collection (stub - GC runs automatically)
      return Value::makeNull();
    };
    options.host_functions["any.gcStats"] = [&](const std::vector<Value> &args) {
      // any.gcStats() - return GC stats object
      if (!vm_ptr) return Value::makeNull();
      auto obj = vm_ptr->createHostObject();
      auto stats = vm_ptr->gcStats();
      vm_ptr->setHostObjectField(obj, "collections", Value::makeInt(static_cast<int64_t>(stats.collections)));
      vm_ptr->setHostObjectField(obj, "heapSize", Value::makeInt(static_cast<int64_t>(stats.heap_size)));
      vm_ptr->setHostObjectField(obj, "objectCount", Value::makeInt(static_cast<int64_t>(stats.object_count)));
      vm_ptr->setHostObjectField(obj, "lastPauseNs", Value::makeInt(static_cast<int64_t>(stats.last_pause_ns)));
      return Value::makeObjectId(obj.id);
    };

    // Async stubs - member access on globals like async.run() gets routed to any.run
    // NOTE: async.run is already defined above with proper task handling
    options.host_functions["async.channel"] = options.host_functions["any.channel"];
    options.host_functions["async.send"] = [&](const std::vector<Value> &args) {
      // async.send(name, value) - send to first channel
      if (args.size() < 2 || !vm_ptr) return Value::makeNull();
      channels.begin()->second.push_back(args[1]);
      return Value(true);
    };
    options.host_functions["async.receive"] = [&](const std::vector<Value> &args) {
      // async.receive(name) - receive from first channel
      if (!vm_ptr) return Value::makeNull();
      auto &queue = channels.begin()->second;
      if (queue.empty()) return Value::makeNull();
      auto value = queue.front();
      queue.pop_front();
      return value;
    };
    options.host_functions["async.tryReceive"] = options.host_functions["async.receive"];
    options.host_functions["any.receive"] = options.host_functions["async.receive"];
    options.host_functions["any.tryReceive"] = options.host_functions["async.tryReceive"];

    // Thread stubs - member access on thread objects
    options.host_functions["thread.send"] = [&](const std::vector<Value> &args) {
      if (args.size() < 2 || !vm_ptr) return Value::makeNull();
      auto cb = thread_callbacks.begin()->second;
      return vm_ptr->callFunction(cb, {args[1]});
    };
    options.host_functions["thread.pause"] = [&](const std::vector<Value> &args) {
      return Value::makeNull();
    };
    options.host_functions["thread.resume"] = [&](const std::vector<Value> &args) {
      return Value::makeNull();
    };
    options.host_functions["thread.running"] = [&](const std::vector<Value> &args) {
      return Value::makeBool(true);
    };
    options.host_functions["thread.stop"] = [&](const std::vector<Value> &args) {
      return Value::makeNull();
    };

    // Interval/timeout stubs
    options.host_functions["interval"] = [&](const std::vector<Value> &args) {
      if (args.size() < 2 || !vm_ptr) return Value::makeNull();
      auto obj = vm_ptr->createHostObject();
      vm_ptr->setHostObjectField(obj, "__kind", Value::makeInt(1));
      vm_ptr->setHostObjectField(obj, "__id", Value::makeInt(0));
      return Value::makeObjectId(obj.id);
    };
    options.host_functions["interval.pause"] = [&](const std::vector<Value> &args) { return Value::makeNull(); };
    options.host_functions["interval.resume"] = [&](const std::vector<Value> &args) { return Value::makeNull(); };
    options.host_functions["interval.stop"] = [&](const std::vector<Value> &args) { return Value::makeNull(); };
    options.host_functions["timeout"] = [&](const std::vector<Value> &args) {
      if (args.size() < 2 || !vm_ptr) return Value::makeNull();
      auto obj = vm_ptr->createHostObject();
      vm_ptr->setHostObjectField(obj, "__kind", Value::makeInt(2));
      vm_ptr->setHostObjectField(obj, "__id", Value::makeInt(0));
      return Value::makeObjectId(obj.id);
    };
    options.host_functions["timeout.cancel"] = [&](const std::vector<Value> &args) { return Value::makeNull(); };
    options.host_functions["object.send"] = [&](const std::vector<Value> &args) {
      if (args.size() < 2 || !vm_ptr) return Value::makeNull();
      return options.host_functions["thread.send"](args);
    };
    options.host_functions["object.pause"] = [&](const std::vector<Value> &args) { return Value::makeNull(); };
    options.host_functions["object.resume"] = [&](const std::vector<Value> &args) { return Value::makeNull(); };
    options.host_functions["object.stop"] = [&](const std::vector<Value> &args) { return Value::makeNull(); };
    options.host_functions["object.running"] = [&](const std::vector<Value> &args) { return Value::makeBool(true); };

    const auto result =
        havel::compiler::runBytecodePipeline(source, "__main__", options);
    if (!equalsInt(result.return_value, expected)) {
      std::cerr << "[FAIL] " << name << ": expected " << expected
                << " but got non-matching result" << std::endl;
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
    if (message.find("Unresolved identifier") == std::string::npos) {
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
    if (message.find("source") == std::string::npos ||
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

  failures += runAsyncCase("async-run-await-closure", R"havel(
fn makeTask(seed) {
    fn run() {
        return seed + 1
    }
    return run
}

task = async.run(makeTask(41))
return await task
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runAsyncCase("async-channel-closure-value", R"havel(
async.channel("jobs")
async.send("jobs", fn(x) { return x + 1 })
cb = async.receive("jobs")
return cb(41)
)havel", 42, dump_bytecode, snapshot_dir);

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
keys = {a: 1, b: 2}.map((k) => k)
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

  if (failures != 0) {
    std::cerr << "Bytecode smoke failed with " << failures << " failing case(s)"
              << std::endl;
    return 1;
  }

	std::cout << "Bytecode smoke passed" << std::endl;
	return 0;
}

} // namespace hvtest
