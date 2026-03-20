#include "havel-lang/compiler/bytecode/ByteCompiler.hpp"
#include "havel-lang/compiler/bytecode/Pipeline.hpp"
#include "havel-lang/parser/Parser.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

namespace {

using havel::compiler::BytecodeValue;

std::string opcodeName(havel::compiler::OpCode opcode) {
  using havel::compiler::OpCode;
  switch (opcode) {
  case OpCode::LOAD_CONST:
    return "LOAD_CONST";
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
  return "<unknown>";
}

bool equalsInt(const BytecodeValue &value, int64_t expected) {
  if (!std::holds_alternative<int64_t>(value)) {
    return false;
  }
  return std::get<int64_t>(value) == expected;
}

std::unique_ptr<havel::compiler::BytecodeChunk>
compileChunk(const std::string &source) {
  havel::parser::Parser parser;
  auto program = parser.produceAST(source);
  if (!program) {
    throw std::runtime_error("parser returned null AST");
  }

  havel::compiler::ByteCompiler compiler;
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
  failures += runUnresolvedIdentifierCase(dump_bytecode, snapshot_dir);

  if (failures != 0) {
    std::cerr << "Bytecode smoke failed with " << failures << " failing case(s)"
              << std::endl;
    return 1;
  }

  std::cout << "Bytecode smoke passed" << std::endl;
  return 0;
}
