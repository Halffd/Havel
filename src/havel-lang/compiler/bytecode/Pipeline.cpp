#include "Pipeline.hpp"

#include "ByteCompiler.hpp"
#include "VM.hpp"
#include "../../lexer/Lexer.hpp"
#include "../../parser/Parser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace havel::compiler {

namespace {
std::string bindingKindName(ResolvedBindingKind kind) {
  switch (kind) {
  case ResolvedBindingKind::Local:
    return "Local";
  case ResolvedBindingKind::Upvalue:
    return "Upvalue";
  case ResolvedBindingKind::GlobalFunction:
    return "GlobalFunction";
  case ResolvedBindingKind::HostGlobal:
    return "HostGlobal";
  case ResolvedBindingKind::Builtin:
    return "Builtin";
  }
  return "Unknown";
}

std::string sanitizeFileStem(const std::string &value) {
  if (value.empty()) {
    return "unit";
  }

  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_') {
      out.push_back(c);
      continue;
    }
    out.push_back('_');
  }
  return out;
}

std::string displayNameForUnit(const std::string &compile_unit_name) {
  return compile_unit_name.empty() ? "<memory>" : compile_unit_name;
}

std::string sourceLineAt(const std::string &source, size_t one_based_line) {
  if (one_based_line == 0) {
    return {};
  }
  std::istringstream stream(source);
  std::string line;
  size_t current = 1;
  while (std::getline(stream, line)) {
    if (current == one_based_line) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      for (char &ch : line) {
        if (!std::isprint(static_cast<unsigned char>(ch))) {
          ch = ' ';
        }
      }
      return line;
    }
    ++current;
  }
  return {};
}

std::string formatCaretLine(size_t column, size_t length,
                            const std::string &annotation) {
  const size_t safe_col = std::max<size_t>(1, column);
  const size_t safe_len = std::max<size_t>(1, length);
  std::string out = "   | ";
  out.append(safe_col - 1, ' ');
  out.append(safe_len, '^');
  if (!annotation.empty()) {
    out += " ";
    out += annotation;
  }
  return out;
}

std::string formatDiagnostic(const std::string &kind,
                             const std::string &message,
                             const std::string &compile_unit_name,
                             const std::string &source, size_t line,
                             size_t column, size_t length,
                             const std::string &annotation = "") {
  std::ostringstream out;
  out << kind << ": " << message << "\n";
  out << "  --> " << displayNameForUnit(compile_unit_name) << ":" << line
      << ":" << column << "\n";
  out << "   |\n";
  const std::string source_line = sourceLineAt(source, line);
  out << line << " | " << source_line << "\n";
  out << formatCaretLine(column, length, annotation);
  return out.str();
}

std::string enrichRuntimeError(const std::string &runtime_error,
                               const std::string &compile_unit_name,
                               const std::string &source) {
  std::smatch match;
  static const std::regex at_re(R"(\n\s*at\s+([0-9]+):([0-9]+))");
  if (!std::regex_search(runtime_error, match, at_re) || match.size() < 3) {
    return runtime_error;
  }

  const size_t line = static_cast<size_t>(std::stoul(match[1].str()));
  const size_t column = static_cast<size_t>(std::stoul(match[2].str()));

  std::string first_line = runtime_error;
  std::string tail;
  if (const auto nl = runtime_error.find('\n'); nl != std::string::npos) {
    first_line = runtime_error.substr(0, nl);
    tail = runtime_error.substr(nl);
  }

  // Remove the first "at x:y" line from tail; we replace it with full source span.
  tail = std::regex_replace(tail, at_re, "", std::regex_constants::format_first_only);
  static const std::regex caller_line_re(
      R"(\n\s*called from function '[^']+' at [0-9]+:[0-9]+)");
  tail = std::regex_replace(tail, caller_line_re, "");
  while (!tail.empty() && tail.front() == '\n') {
    tail.erase(tail.begin());
  }

  std::ostringstream out;
  out << first_line << "\n";
  out << "  --> " << displayNameForUnit(compile_unit_name) << ":" << line
      << ":" << column << "\n";
  out << "   |\n";
  const std::string source_line = sourceLineAt(source, line);
  out << line << " | " << source_line << "\n";
  out << formatCaretLine(column, 1, "runtime error");

  // Expand each "called from ... at L:C" frame with source snippet.
  static const std::regex caller_re(
      R"(called from function '([^']+)' at ([0-9]+):([0-9]+))");
  std::sregex_iterator it(runtime_error.begin(), runtime_error.end(), caller_re);
  std::sregex_iterator end;
  for (; it != end; ++it) {
    const std::string fn = (*it)[1].str();
    const size_t frame_line = static_cast<size_t>(std::stoul((*it)[2].str()));
    const size_t frame_col = static_cast<size_t>(std::stoul((*it)[3].str()));
    out << "\n";
    out << "  called from function '" << fn << "'\n";
    out << "  --> " << displayNameForUnit(compile_unit_name) << ":"
        << frame_line << ":" << frame_col << "\n";
    out << "   |\n";
    const std::string frame_source = sourceLineAt(source, frame_line);
    out << frame_line << " | " << frame_source << "\n";
    out << formatCaretLine(frame_col, 1, "call site");
  }

  if (!tail.empty()) {
    out << "\n" << tail;
  }
  return out.str();
}

std::string formatResolverSnapshot(const LexicalResolutionResult &resolution) {
  std::ostringstream out;
  out << "[resolver.bindings]\n";
  std::vector<std::tuple<std::string, ResolvedBindingKind, uint32_t, uint32_t>>
      bindings;
  bindings.reserve(resolution.identifier_bindings.size());
  for (const auto &[identifier, binding] : resolution.identifier_bindings) {
    if (!identifier) {
      continue;
    }
    bindings.emplace_back(identifier->symbol, binding.kind, binding.slot,
                          binding.scope_distance);
  }
  std::sort(bindings.begin(), bindings.end(),
            [](const auto &lhs, const auto &rhs) {
              return std::get<0>(lhs) < std::get<0>(rhs);
            });
  for (const auto &[symbol, kind, slot, distance] : bindings) {
    out << symbol << " => kind=" << bindingKindName(kind) << ", slot=" << slot
        << ", distance=" << distance << "\n";
  }

  out << "\n[resolver.declarations]\n";
  std::vector<std::pair<std::string, uint32_t>> declarations;
  declarations.reserve(resolution.declaration_slots.size());
  for (const auto &[identifier, slot] : resolution.declaration_slots) {
    if (!identifier) {
      continue;
    }
    declarations.emplace_back(identifier->symbol, slot);
  }
  std::sort(declarations.begin(), declarations.end(),
            [](const auto &lhs, const auto &rhs) {
              return lhs.first < rhs.first;
            });
  for (const auto &[symbol, slot] : declarations) {
    out << symbol << " => slot=" << slot << "\n";
  }

  out << "\n[resolver.upvalues]\n";
  std::vector<std::pair<std::string, std::vector<UpvalueDescriptor>>> captures;
  captures.reserve(resolution.function_upvalues.size());
  for (const auto &[function, vars] : resolution.function_upvalues) {
    const std::string fn =
        (function && function->name) ? function->name->symbol : "<anonymous>";
    captures.emplace_back(fn, vars);
  }
  std::sort(captures.begin(), captures.end(),
            [](const auto &lhs, const auto &rhs) {
              return lhs.first < rhs.first;
            });
  for (const auto &[fn, vars] : captures) {
    out << fn << " => [";
    for (size_t i = 0; i < vars.size(); ++i) {
      if (i > 0) {
        out << ", ";
      }
      out << "{index=" << vars[i].index
          << ", local=" << (vars[i].captures_local ? "true" : "false") << "}";
    }
    out << "]\n";
  }

  return out.str();
}

std::string formatValue(const BytecodeValue &value) {
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
    return "\"" + std::get<std::string>(value) + "\"";
  }
  if (std::holds_alternative<uint32_t>(value)) {
    return std::to_string(std::get<uint32_t>(value));
  }
  if (std::holds_alternative<FunctionObject>(value)) {
    return "fn[" +
           std::to_string(std::get<FunctionObject>(value).function_index) + "]";
  }
  if (std::holds_alternative<ClosureRef>(value)) {
    return "closure[" + std::to_string(std::get<ClosureRef>(value).id) + "]";
  }
  if (std::holds_alternative<ArrayRef>(value)) {
    return "array[" + std::to_string(std::get<ArrayRef>(value).id) + "]";
  }
  if (std::holds_alternative<ObjectRef>(value)) {
    return "object[" + std::to_string(std::get<ObjectRef>(value).id) + "]";
  }
  if (std::holds_alternative<SetRef>(value)) {
    return "set[" + std::to_string(std::get<SetRef>(value).id) + "]";
  }
  if (std::holds_alternative<HostFunctionRef>(value)) {
    return "hostfn[" + std::get<HostFunctionRef>(value).name + "]";
  }
  return "<unknown>";
}

std::string opcodeName(OpCode opcode) {
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
  case OpCode::TAIL_CALL:
    return "TAIL_CALL";
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
  case OpCode::DEFINE_FUNC:
    return "DEFINE_FUNC";
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
  case OpCode::ARRAY_LEN:
    return "ARRAY_LEN";
  case OpCode::ITER_NEW:
    return "ITER_NEW";
  case OpCode::ITER_NEXT:
    return "ITER_NEXT";
  case OpCode::RANGE_NEW:
    return "RANGE_NEW";
  case OpCode::RANGE_STEP_NEW:
    return "RANGE_STEP_NEW";
  case OpCode::STRUCT_NEW:
    return "STRUCT_NEW";
  case OpCode::STRUCT_GET:
    return "STRUCT_GET";
  case OpCode::STRUCT_SET:
    return "STRUCT_SET";
  case OpCode::ENUM_NEW:
    return "ENUM_NEW";
  case OpCode::ENUM_TAG:
    return "ENUM_TAG";
  case OpCode::ENUM_PAYLOAD:
    return "ENUM_PAYLOAD";
  case OpCode::ENUM_MATCH:
    return "ENUM_MATCH";
  case OpCode::OBJECT_KEYS:
    return "OBJECT_KEYS";
  case OpCode::OBJECT_VALUES:
    return "OBJECT_VALUES";
  case OpCode::OBJECT_ENTRIES:
    return "OBJECT_ENTRIES";
  case OpCode::OBJECT_HAS:
    return "OBJECT_HAS";
  case OpCode::OBJECT_DELETE:
    return "OBJECT_DELETE";
  case OpCode::ARRAY_POP:
    return "ARRAY_POP";
  case OpCode::ARRAY_HAS:
    return "ARRAY_HAS";
  case OpCode::ARRAY_FIND:
    return "ARRAY_FIND";
  case OpCode::ARRAY_MAP:
    return "ARRAY_MAP";
  case OpCode::ARRAY_FILTER:
    return "ARRAY_FILTER";
  case OpCode::ARRAY_REDUCE:
    return "ARRAY_REDUCE";
  case OpCode::ARRAY_FOREACH:
    return "ARRAY_FOREACH";
  case OpCode::STRING_LEN:
    return "STRING_LEN";
  case OpCode::STRING_UPPER:
    return "STRING_UPPER";
  case OpCode::STRING_LOWER:
    return "STRING_LOWER";
  case OpCode::STRING_TRIM:
    return "STRING_TRIM";
  case OpCode::STRING_HAS:
    return "STRING_HAS";
  case OpCode::STRING_STARTS:
    return "STRING_STARTS";
  case OpCode::STRING_ENDS:
    return "STRING_ENDS";
  case OpCode::SET_NEW:
    return "SET_NEW";
  case OpCode::OBJECT_NEW:
    return "OBJECT_NEW";
  case OpCode::OBJECT_GET:
    return "OBJECT_GET";
  case OpCode::OBJECT_SET:
    return "OBJECT_SET";
  case OpCode::STRING_CONCAT:
    return "STRING_CONCAT";
  case OpCode::PRINT:
    return "PRINT";
  case OpCode::DEBUG:
    return "DEBUG";
  case OpCode::NOP:
    return "NOP";
  }
  return "UNKNOWN";
}

std::string formatBytecodeSnapshot(const BytecodeChunk &chunk) {
  std::ostringstream out;
  for (const auto &function : chunk.getAllFunctions()) {
    out << "fn " << function.name << "(params=" << function.param_count
        << ", locals=" << function.local_count << ")\n";

    for (size_t i = 0; i < function.instructions.size(); ++i) {
      out << "  " << i << ": "
          << opcodeName(function.instructions[i].opcode);
      for (size_t j = 0; j < function.instructions[i].operands.size(); ++j) {
        out << (j == 0 ? " " : ", ")
            << formatValue(function.instructions[i].operands[j]);
      }
      if (i < function.instruction_locations.size()) {
        const auto &location = function.instruction_locations[i];
        out << "    @";
        if (location.line == 0 && location.column == 0) {
          out << "?";
        } else {
          out << location.line << ":" << location.column;
        }
      }
      out << "\n";
    }

    if (!function.constants.empty()) {
      out << "  constants:\n";
      for (size_t c = 0; c < function.constants.size(); ++c) {
        out << "    [" << c << "] " << formatValue(function.constants[c])
            << "\n";
      }
    }
  }
  return out.str();
}
} // namespace

BytecodeSmokeResult runBytecodePipeline(
    const std::string &source, const std::string &entry_function,
    const PipelineOptions &options) {
  auto writeSnapshotArtifact = [&](const BytecodeSmokeResult &result,
                                   const std::string &error) {
    if (!options.write_snapshot_artifact || options.snapshot_dir.empty()) {
      return std::string{};
    }
    std::filesystem::create_directories(options.snapshot_dir);
    const auto artifact_name =
        sanitizeFileStem(options.compile_unit_name) + ".snapshot.txt";
    const std::filesystem::path artifact_path =
        std::filesystem::path(options.snapshot_dir) / artifact_name;
    std::ofstream out(artifact_path, std::ios::trunc);
    if (!out.is_open()) {
      throw std::runtime_error("Failed to open snapshot artifact: " +
                               artifact_path.string());
    }
    out << "=== RESOLVER SNAPSHOT ===\n";
    out << result.snapshot.resolver << "\n";
    out << "=== BYTECODE SNAPSHOT ===\n";
    out << result.snapshot.bytecode << "\n";
    if (!error.empty()) {
      out << "=== ERROR ===\n";
      out << error << "\n";
    }
    return artifact_path.string();
  };

  parser::Parser parser;
  std::unique_ptr<ast::Program> program;
  try {
    program = parser.produceAST(source);
  } catch (const havel::LexError &e) {
    throw std::runtime_error(formatDiagnostic(
        "LexerError", e.what(), options.compile_unit_name, source, e.line,
        e.column, e.length, "unexpected token"));
  } catch (const parser::ParseError &e) {
    throw std::runtime_error(formatDiagnostic(
        "ParseError", e.what(), options.compile_unit_name, source, e.line,
        e.column, e.length, "here"));
  }
  if (!program) {
    throw std::runtime_error(
        "Bytecode pipeline failed: parser returned null AST");
  }

  ByteCompiler compiler;
  for (const auto &[name, _] : options.host_functions) {
    compiler.addHostBuiltin(name);
  }
  for (const auto &name : options.host_global_names) {
    compiler.addHostGlobal(name);
  }
  BytecodeSmokeResult result;
  std::unique_ptr<BytecodeChunk> chunk;
  try {
    chunk = compiler.compile(*program);
    if (!chunk) {
      throw std::runtime_error(
          "Bytecode smoke pipeline failed: compiler returned null chunk");
    }
    result.snapshot.resolver = formatResolverSnapshot(compiler.lexicalResolution());
    result.snapshot.bytecode = formatBytecodeSnapshot(*chunk);
    result.snapshot.artifact_path = writeSnapshotArtifact(result, "");
  } catch (const std::exception &e) {
    std::string formatted = e.what();
    static const std::regex unresolved_re(
        R"(Lexical resolution failed:\s*Unresolved identifier '([^']+)'\s*at\s*([0-9]+):([0-9]+))");
    static const std::regex duplicate_re(
        R"(Lexical resolution failed:\s*Duplicate declaration:\s*'([^']+)'\s*already defined.*?at\s*([0-9]+):([0-9]+))");
    std::smatch unresolved_match;
    std::smatch duplicate_match;
    if (std::regex_search(formatted, unresolved_match, unresolved_re) &&
        unresolved_match.size() >= 4) {
      const std::string symbol = unresolved_match[1].str();
      const size_t line = static_cast<size_t>(std::stoul(unresolved_match[2].str()));
      const size_t column = static_cast<size_t>(std::stoul(unresolved_match[3].str()));
      formatted = formatDiagnostic(
          "SemanticError", "undefined variable '" + symbol + "'",
          options.compile_unit_name, source, line, column,
          std::max<size_t>(1, symbol.size()), "not found in this scope");
    } else if (std::regex_search(formatted, duplicate_match, duplicate_re) &&
               duplicate_match.size() >= 4) {
      const std::string symbol = duplicate_match[1].str();
      const size_t line = static_cast<size_t>(std::stoul(duplicate_match[2].str()));
      const size_t column = static_cast<size_t>(std::stoul(duplicate_match[3].str()));
      formatted = formatDiagnostic(
          "SemanticError", "duplicate declaration '" + symbol + "'",
          options.compile_unit_name, source, line, column,
          std::max<size_t>(1, symbol.size()), "already defined in this scope");
    }
    result.snapshot.resolver = formatResolverSnapshot(compiler.lexicalResolution());
    result.snapshot.bytecode = "<not-emitted>";
    result.snapshot.artifact_path = writeSnapshotArtifact(result, formatted);
    throw std::runtime_error(formatted);
  }

  VM owned_vm;
  VM *vm = options.vm_override ? options.vm_override : &owned_vm;
  for (const auto &[name, fn] : options.host_functions) {
    vm->registerHostFunction(name, fn);
    // Also add to globals if it's a host global (so LOAD_GLOBAL can find it)
    if (options.host_global_names.count(name) > 0) {
      vm->setGlobal(name, BytecodeValue(HostFunctionRef{name}));
    }
  }
  if (options.vm_setup) {
    options.vm_setup(*vm);
  }
  try {
    result.return_value = vm->execute(*chunk, entry_function);
  } catch (const std::exception &e) {
    throw std::runtime_error(
        enrichRuntimeError(e.what(), options.compile_unit_name, source));
  }
  return result;
}

BytecodeSmokeResult runBytecodePipeline(const std::string &source,
                                        const std::string &entry_function) {
  return runBytecodePipeline(source, entry_function, PipelineOptions{});
}

} // namespace havel::compiler
