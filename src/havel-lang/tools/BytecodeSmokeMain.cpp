#include "havel-lang/compiler/bytecode/ByteCompiler.hpp"
#include "havel-lang/compiler/bytecode/Pipeline.hpp"
#include "havel-lang/compiler/bytecode/VM.hpp"
#include "havel-lang/parser/Parser.h"

#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using havel::compiler::BytecodeValue;

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
  case OpCode::CALL_HOST:
    return "CALL_HOST";
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

std::string bytecodeValueToString(const BytecodeValue &value) {
  if (std::holds_alternative<std::nullptr_t>(value)) {
    return "null";
  }
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? "true" : "false";
  }
  if (std::holds_alternative<int64_t>(value)) {
    return std::to_string(std::get<int64_t>(value));
  }
  if (std::holds_alternative<double>(value)) {
    return std::to_string(std::get<double>(value));
  }
  if (std::holds_alternative<std::string>(value)) {
    return '"' + std::get<std::string>(value) + '"';
  }
  if (std::holds_alternative<uint32_t>(value)) {
    return std::to_string(std::get<uint32_t>(value));
  }
  if (std::holds_alternative<havel::compiler::FunctionObject>(value)) {
    return "fn[" + std::to_string(
                       std::get<havel::compiler::FunctionObject>(value)
                           .function_index) +
           "]";
  }
  if (std::holds_alternative<havel::compiler::ClosureRef>(value)) {
    return "closure[" +
           std::to_string(std::get<havel::compiler::ClosureRef>(value).id) +
           "]";
  }
  if (std::holds_alternative<havel::compiler::ArrayRef>(value)) {
    return "array[" +
           std::to_string(std::get<havel::compiler::ArrayRef>(value).id) + "]";
  }
  if (std::holds_alternative<havel::compiler::ObjectRef>(value)) {
    return "object[" +
           std::to_string(std::get<havel::compiler::ObjectRef>(value).id) + "]";
  }
  if (std::holds_alternative<havel::compiler::SetRef>(value)) {
    return "set[" +
           std::to_string(std::get<havel::compiler::SetRef>(value).id) + "]";
  }
  if (std::holds_alternative<havel::compiler::HostFunctionRef>(value)) {
    return "hostfn[" +
           std::get<havel::compiler::HostFunctionRef>(value).name + "]";
  }
  return "<unknown>";
}

bool equalsInt(const BytecodeValue &value, int64_t expected) {
  if (!std::holds_alternative<int64_t>(value)) {
    return false;
  }
  return std::get<int64_t>(value) == expected;
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
  for (const auto &name : extra_host_builtins) {
    compiler.addHostBuiltin(name);
  }
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
    options.host_global_names.insert("async");
    options.host_global_names.insert("thread");
    options.host_global_names.insert("interval");
    options.host_global_names.insert("timeout");

    havel::compiler::VM *vm_ptr = nullptr;
    uint64_t next_task_id = 1;
    std::unordered_map<std::string, BytecodeValue> task_results;
    std::unordered_map<std::string, std::deque<BytecodeValue>> channels;
    std::unordered_map<std::string, BytecodeValue> thread_callbacks;
    std::unordered_map<std::string, bool> thread_running;
    std::unordered_map<std::string, bool> thread_paused;
    std::unordered_map<std::string, BytecodeValue> interval_callbacks;
    std::unordered_map<std::string, bool> interval_running;
    std::unordered_map<std::string, bool> interval_paused;
    std::unordered_map<std::string, BytecodeValue> timeout_callbacks;
    std::unordered_map<std::string, bool> timeout_running;

    options.vm_setup = [&](havel::compiler::VM &vm) { vm_ptr = &vm; };

    options.host_functions["async.run"] =
        [&](const std::vector<BytecodeValue> &args) {
          if (!vm_ptr) {
            throw std::runtime_error("async.run vm unavailable");
          }
          if (args.empty()) {
            throw std::runtime_error("async.run requires callback");
          }
          std::string task_id = "task-" + std::to_string(next_task_id++);
          task_results[task_id] = vm_ptr->call(args[0], {});
          return BytecodeValue(task_id);
        };
    options.host_functions["async.await"] =
        [&](const std::vector<BytecodeValue> &args) {
          if (args.empty() || !std::holds_alternative<std::string>(args[0])) {
            throw std::runtime_error("async.await requires task id");
          }
          const auto &task_id = std::get<std::string>(args[0]);
          auto it = task_results.find(task_id);
          if (it == task_results.end()) {
            return BytecodeValue(nullptr);
          }
          return it->second;
        };
    options.host_functions["async.channel"] =
        [&](const std::vector<BytecodeValue> &args) {
          if (args.empty() || !std::holds_alternative<std::string>(args[0])) {
            throw std::runtime_error("async.channel requires name");
          }
          channels[std::get<std::string>(args[0])];
          return BytecodeValue(true);
        };
    options.host_functions["async.send"] =
        [&](const std::vector<BytecodeValue> &args) {
          if (args.size() < 2 || !std::holds_alternative<std::string>(args[0])) {
            throw std::runtime_error("async.send requires name + value");
          }
          channels[std::get<std::string>(args[0])].push_back(args[1]);
          return BytecodeValue(true);
        };
    options.host_functions["async.receive"] =
        [&](const std::vector<BytecodeValue> &args) {
          if (args.empty() || !std::holds_alternative<std::string>(args[0])) {
            throw std::runtime_error("async.receive requires name");
          }
          auto &queue = channels[std::get<std::string>(args[0])];
          if (queue.empty()) {
            return BytecodeValue(nullptr);
          }
          auto value = queue.front();
          queue.pop_front();
          return value;
        };
    options.host_functions["async.tryReceive"] =
        options.host_functions["async.receive"];

    options.host_functions["thread"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.empty()) {
        throw std::runtime_error("thread requires callback");
      }
      std::string id = "thread-" + std::to_string(next_task_id++);
      thread_callbacks[id] = args[0];
      thread_running[id] = true;
      thread_paused[id] = false;
      auto obj = vm_ptr->createHostObject();
      vm_ptr->setHostObjectField(obj, "__kind", BytecodeValue(std::string("thread")));
      vm_ptr->setHostObjectField(obj, "__id", BytecodeValue(id));
      return BytecodeValue(obj);
    };
    options.host_functions["thread.send"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.size() < 2 || !std::holds_alternative<havel::compiler::ObjectRef>(args[0])) {
        return BytecodeValue(false);
      }
      auto obj = std::get<havel::compiler::ObjectRef>(args[0]);
      auto idv = vm_ptr->getHostObjectField(obj, "__id");
      if (!std::holds_alternative<std::string>(idv)) return BytecodeValue(false);
      const auto &id = std::get<std::string>(idv);
      if (!thread_running[id] || thread_paused[id]) return BytecodeValue(false);
      (void)vm_ptr->call(thread_callbacks[id], {args[1]});
      return BytecodeValue(true);
    };
    options.host_functions["thread.pause"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.empty() || !std::holds_alternative<havel::compiler::ObjectRef>(args[0])) return BytecodeValue(false);
      auto idv = vm_ptr->getHostObjectField(std::get<havel::compiler::ObjectRef>(args[0]), "__id");
      if (!std::holds_alternative<std::string>(idv)) return BytecodeValue(false);
      thread_paused[std::get<std::string>(idv)] = true;
      return BytecodeValue(true);
    };
    options.host_functions["thread.resume"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.empty() || !std::holds_alternative<havel::compiler::ObjectRef>(args[0])) return BytecodeValue(false);
      auto idv = vm_ptr->getHostObjectField(std::get<havel::compiler::ObjectRef>(args[0]), "__id");
      if (!std::holds_alternative<std::string>(idv)) return BytecodeValue(false);
      thread_paused[std::get<std::string>(idv)] = false;
      return BytecodeValue(true);
    };
    options.host_functions["thread.stop"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.empty() || !std::holds_alternative<havel::compiler::ObjectRef>(args[0])) return BytecodeValue(false);
      auto idv = vm_ptr->getHostObjectField(std::get<havel::compiler::ObjectRef>(args[0]), "__id");
      if (!std::holds_alternative<std::string>(idv)) return BytecodeValue(false);
      thread_running[std::get<std::string>(idv)] = false;
      return BytecodeValue(true);
    };
    options.host_functions["thread.running"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.empty() || !std::holds_alternative<havel::compiler::ObjectRef>(args[0])) return BytecodeValue(false);
      auto idv = vm_ptr->getHostObjectField(std::get<havel::compiler::ObjectRef>(args[0]), "__id");
      if (!std::holds_alternative<std::string>(idv)) return BytecodeValue(false);
      return BytecodeValue(thread_running[std::get<std::string>(idv)]);
    };

    options.host_functions["interval"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.size() < 2) throw std::runtime_error("interval requires delay + callback");
      std::string id = "interval-" + std::to_string(next_task_id++);
      interval_callbacks[id] = args[1];
      interval_running[id] = true;
      interval_paused[id] = false;
      (void)vm_ptr->call(interval_callbacks[id], {});
      auto obj = vm_ptr->createHostObject();
      vm_ptr->setHostObjectField(obj, "__kind", BytecodeValue(std::string("interval")));
      vm_ptr->setHostObjectField(obj, "__id", BytecodeValue(id));
      return BytecodeValue(obj);
    };
    options.host_functions["interval.pause"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.empty() || !std::holds_alternative<havel::compiler::ObjectRef>(args[0])) return BytecodeValue(false);
      auto idv = vm_ptr->getHostObjectField(std::get<havel::compiler::ObjectRef>(args[0]), "__id");
      if (!std::holds_alternative<std::string>(idv)) return BytecodeValue(false);
      interval_paused[std::get<std::string>(idv)] = true;
      return BytecodeValue(true);
    };
    options.host_functions["interval.resume"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.empty() || !std::holds_alternative<havel::compiler::ObjectRef>(args[0])) return BytecodeValue(false);
      auto idv = vm_ptr->getHostObjectField(std::get<havel::compiler::ObjectRef>(args[0]), "__id");
      if (!std::holds_alternative<std::string>(idv)) return BytecodeValue(false);
      interval_paused[std::get<std::string>(idv)] = false;
      if (interval_running[std::get<std::string>(idv)]) {
        (void)vm_ptr->call(interval_callbacks[std::get<std::string>(idv)], {});
      }
      return BytecodeValue(true);
    };
    options.host_functions["interval.stop"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.empty() || !std::holds_alternative<havel::compiler::ObjectRef>(args[0])) return BytecodeValue(false);
      auto idv = vm_ptr->getHostObjectField(std::get<havel::compiler::ObjectRef>(args[0]), "__id");
      if (!std::holds_alternative<std::string>(idv)) return BytecodeValue(false);
      interval_running[std::get<std::string>(idv)] = false;
      return BytecodeValue(true);
    };

    options.host_functions["timeout"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.size() < 2) throw std::runtime_error("timeout requires delay + callback");
      std::string id = "timeout-" + std::to_string(next_task_id++);
      timeout_callbacks[id] = args[1];
      timeout_running[id] = true;
      (void)vm_ptr->call(timeout_callbacks[id], {});
      timeout_running[id] = false;
      auto obj = vm_ptr->createHostObject();
      vm_ptr->setHostObjectField(obj, "__kind", BytecodeValue(std::string("timeout")));
      vm_ptr->setHostObjectField(obj, "__id", BytecodeValue(id));
      return BytecodeValue(obj);
    };
    options.host_functions["timeout.cancel"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.empty() || !std::holds_alternative<havel::compiler::ObjectRef>(args[0])) return BytecodeValue(false);
      auto idv = vm_ptr->getHostObjectField(std::get<havel::compiler::ObjectRef>(args[0]), "__id");
      if (!std::holds_alternative<std::string>(idv)) return BytecodeValue(false);
      timeout_running[std::get<std::string>(idv)] = false;
      return BytecodeValue(true);
    };

    options.host_functions["object.send"] = options.host_functions["thread.send"];
    options.host_functions["object.pause"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.empty() || !std::holds_alternative<havel::compiler::ObjectRef>(args[0])) return BytecodeValue(false);
      auto obj = std::get<havel::compiler::ObjectRef>(args[0]);
      auto kind = vm_ptr->getHostObjectField(obj, "__kind");
      if (!std::holds_alternative<std::string>(kind)) return BytecodeValue(false);
      if (std::get<std::string>(kind) == "thread") return options.host_functions["thread.pause"](args);
      if (std::get<std::string>(kind) == "interval") return options.host_functions["interval.pause"](args);
      return BytecodeValue(false);
    };
    options.host_functions["object.resume"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.empty() || !std::holds_alternative<havel::compiler::ObjectRef>(args[0])) return BytecodeValue(false);
      auto obj = std::get<havel::compiler::ObjectRef>(args[0]);
      auto kind = vm_ptr->getHostObjectField(obj, "__kind");
      if (!std::holds_alternative<std::string>(kind)) return BytecodeValue(false);
      if (std::get<std::string>(kind) == "thread") return options.host_functions["thread.resume"](args);
      if (std::get<std::string>(kind) == "interval") return options.host_functions["interval.resume"](args);
      return BytecodeValue(false);
    };
    options.host_functions["object.stop"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.empty() || !std::holds_alternative<havel::compiler::ObjectRef>(args[0])) return BytecodeValue(false);
      auto obj = std::get<havel::compiler::ObjectRef>(args[0]);
      auto kind = vm_ptr->getHostObjectField(obj, "__kind");
      if (!std::holds_alternative<std::string>(kind)) return BytecodeValue(false);
      if (std::get<std::string>(kind) == "thread") return options.host_functions["thread.stop"](args);
      if (std::get<std::string>(kind) == "interval") return options.host_functions["interval.stop"](args);
      return BytecodeValue(false);
    };
    options.host_functions["object.cancel"] = options.host_functions["timeout.cancel"];
    options.host_functions["object.running"] = [&](const std::vector<BytecodeValue> &args) {
      if (!vm_ptr || args.empty() || !std::holds_alternative<havel::compiler::ObjectRef>(args[0])) return BytecodeValue(false);
      auto obj = std::get<havel::compiler::ObjectRef>(args[0]);
      auto kind = vm_ptr->getHostObjectField(obj, "__kind");
      auto idv = vm_ptr->getHostObjectField(obj, "__id");
      if (!std::holds_alternative<std::string>(kind) || !std::holds_alternative<std::string>(idv)) return BytecodeValue(false);
      const auto &kindv = std::get<std::string>(kind);
      const auto &id = std::get<std::string>(idv);
      if (kindv == "thread") return BytecodeValue(thread_running[id]);
      if (kindv == "interval") return BytecodeValue(interval_running[id]);
      if (kindv == "timeout") return BytecodeValue(timeout_running[id]);
      return BytecodeValue(false);
    };
    options.host_functions["any.send"] = options.host_functions["object.send"];
    options.host_functions["any.pause"] = options.host_functions["object.pause"];
    options.host_functions["any.resume"] = options.host_functions["object.resume"];
    options.host_functions["any.stop"] = options.host_functions["object.stop"];
    options.host_functions["any.cancel"] = options.host_functions["object.cancel"];
    options.host_functions["any.running"] = options.host_functions["object.running"];

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
    let x = 1
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
    let x = 1
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
    let x = 41
    fn inner() {
        return x + 1
    }
    return inner
}

let cb = make()
store_closure(cb)
cb = 0
gc_now()
let loaded = load_closure()
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
        [&vm, &stored_closure](const std::vector<BytecodeValue> &args) {
          stored_closure.emplace(vm.makeRoot(args[0]));
          return BytecodeValue(nullptr);
        });
    vm.registerHostFunction(
        "load_closure", 0,
        [&stored_closure](const std::vector<BytecodeValue> &) {
          if (!stored_closure.has_value()) {
            return BytecodeValue(nullptr);
          }
          return stored_closure->get().value_or(BytecodeValue(nullptr));
        });
    vm.registerHostFunction("gc_now", 0,
                            [&vm](const std::vector<BytecodeValue> &) {
                              vm.runGarbageCollection();
                              return BytecodeValue(nullptr);
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
    let x = seed
    fn cb() {
        return x + 2
    }
    return cb
}

let cb = makeCallback(40)
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
        [&vm, &stored_callback](const std::vector<BytecodeValue> &args) {
          stored_callback.emplace(vm.makeRoot(args[0]));
          return BytecodeValue(true);
        });
    vm.registerHostFunction(
        "trigger_cb", 0,
        [&vm, &stored_callback](const std::vector<BytecodeValue> &) {
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

int main(int argc, char **argv) {
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

let x = add(2, 3)
print(x)
return x
)havel", 5, dump_bytecode, snapshot_dir);

  failures += runCase("first-class-call", R"havel(
fn add(a, b) {
    return a + b
}

let f = add
return f(20, 22)
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("nested-local-function", R"havel(
fn outer(x) {
    fn doubleIt(value) {
        return value * 2
    }
    let f = doubleIt
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

let nums = [1, 2, 3]
let out = nums.map(double)
return out[2]
)havel", 6, dump_bytecode, snapshot_dir);

  failures += runCase("lambda-in-call", R"havel(
let nums = [1, 2, 3]
let out = nums.map((x) => x * 3)
return out[1]
)havel", 6, dump_bytecode, snapshot_dir);

  failures += runCase("method-chaining-array-hof", R"havel(
let nums = [1, 2, 3, 4]
return nums.map((x) => x * 2).filter((x) => x > 4).reduce((a, b) => a + b, 0)
)havel", 14, dump_bytecode, snapshot_dir);

  failures += runAsyncCase("async-run-await-closure", R"havel(
fn makeTask(seed) {
    fn run() {
        return seed + 1
    }
    return run
}

let task = async.run(makeTask(41))
return await task
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runAsyncCase("async-channel-closure-value", R"havel(
async.channel("jobs")
async.send("jobs", fn(x) { return x + 1 })
let cb = async.receive("jobs")
return cb(41)
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runAsyncCase("thread-send", R"havel(
let worker = thread(fn(msg) { return msg + 1 })
worker.pause()
worker.resume()
worker.send(41)
if worker.running() {
    return 1
}
return 0
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runAsyncCase("thread-block-sugar", R"havel(
let value = 0
let worker = thread {
    value += 1
}
worker.send(0)
return value
)havel", 1, dump_bytecode, snapshot_dir);

  failures += runAsyncCase("interval-timeout-controls", R"havel(
let hits = 0
let timer = interval(100, fn() { hits += 1 })
timer.pause()
timer.resume()
timer.stop()
let t = timeout(100, fn() { hits += 10 })
t.cancel()
return hits
)havel", 11, dump_bytecode, snapshot_dir);

  failures += runAsyncCase("interval-timeout-block-sugar", R"havel(
let hits = 0
let i = interval 100 {
    hits += 1
}
i.stop()
let t = timeout 10 {
    hits += 10
}
t.cancel()
return hits
)havel", 11, dump_bytecode, snapshot_dir);

  failures += runCase("const-tuple-destructure", R"havel(
const (a, b, c) = (5, 7, 9)
let total = a + b + c
return total
)havel", 21, dump_bytecode, snapshot_dir);

  failures += runCase("struct-vm-basic", R"havel(
struct Point { x, y }
let p = Point(10, 32)
return struct.get(p, "y")
)havel", 32, dump_bytecode, snapshot_dir);

  failures += runCase("closure-return", R"havel(
fn makeGetter(v) {
    let x = v
    fn get() {
        return x
    }
    return get
}

let getter = makeGetter(9)
return getter()
)havel", 9, dump_bytecode, snapshot_dir);

  failures += runCase("closure-transitive", R"havel(
fn outer() {
    let x = 41
    fn middle() {
        fn inner() {
            return x + 1
        }
        return inner
    }
    return middle()
}

let f = outer()
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
let x = 0
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
let chars = "ab".map((c) => c)
let keys = {a: 1, b: 2}.map((k) => k)
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
let x = 1
x = x + 4
x += 5
return x
)havel", 10, dump_bytecode, snapshot_dir);

  failures += runCase("assignment-upvalue", R"havel(
fn makeCounter(seed) {
    let x = seed
    fn next() {
        x += 1
        return x
    }
    return next
}

let c = makeCounter(40)
c()
return c()
)havel", 42, dump_bytecode, snapshot_dir);

  failures += runCase("object-member-assignment", R"havel(
let obj = {a: 1}
obj.a = 7
obj.a += 2
return obj.a
)havel", 9, dump_bytecode, snapshot_dir);

  failures += runCase("index-assignment-array", R"havel(
let arr = [1, 2]
arr[1] = 10
arr[3] = 5
return arr[1] + arr[3]
)havel", 15, dump_bytecode, snapshot_dir);

  failures += runCase("set-create-and-assign", R"havel(
let s = #{1, 2}
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
let i = 0
while i < 2000 {
    let arr = [i, i + 1, i + 2]
    let obj = {v: i}
    let s = #{i}
    i += 1
}
return i
)havel", 2000, dump_bytecode, snapshot_dir);

  failures += runCase("system-gc-manual", R"havel(
let i = 0
while i < 256 {
    let arr = [i, i + 1]
    i += 1
}
system.gc()
return i
)havel", 256, dump_bytecode, snapshot_dir);

  failures += runCase("system-gc-stats", R"havel(
let before = system.gcStats()
let i = 0
while i < 64 {
    let obj = {v: i}
    i += 1
}
system.gc()
let after = system.gcStats()
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
    let hits = 0
    let obj = {a: 1}
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
    let hits = 0
    let arr = [1, 2]
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
let i = 0
while i < 1 {
    return 6
}
return 0
)havel", 6, dump_bytecode, snapshot_dir);

  failures += runCase("shadowing", R"havel(
let x = 1
if true {
    let x = 2
    print(x)
}
return x
)havel", 1, dump_bytecode, snapshot_dir);

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
