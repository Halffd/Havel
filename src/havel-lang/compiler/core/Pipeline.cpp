#include "Pipeline.hpp"
#include "../../../utils/Logger.hpp"

#include "ByteCompiler.hpp"
#include "../vm/VM.hpp"
#include "../../lexer/Lexer.hpp"
#include "../../parser/Parser.h"
#include "../../utils/ErrorPrinter.hpp"
#include "../../errors/ErrorSystem.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <tuple>

// Macro for throwing errors with source location info
// Reports to unified ErrorReporter before throwing
#define COMPILER_THROW(msg) \
  do { \
    ::havel::errors::ErrorReporter::instance().report( \
        HAVEL_ERROR(::havel::errors::ErrorStage::Compiler, msg)); \
    throw std::runtime_error(std::string(msg) + " [" + __FILE__ + ":" + std::to_string(__LINE__) + "]"); \
  } while (0)

namespace havel::compiler {

namespace {
std::string bindingKindName(ResolvedBindingKind kind) {
  switch (kind) {
  case ResolvedBindingKind::Local:
    return "Local";
  case ResolvedBindingKind::Upvalue:
    return "Upvalue";
  case ResolvedBindingKind::Global:
    return "Global";
  case ResolvedBindingKind::Function:
    return "Function";
  case ResolvedBindingKind::HostFunction:
    return "HostFunction";
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
  std::string file_path = compile_unit_name.empty() ? "<memory>" : compile_unit_name;
  std::string source_line = sourceLineAt(source, line);
  return ::havel::ErrorPrinter::formatError(kind, message + (annotation.empty() ? "" : " (" + annotation + ")"), file_path, line, column, length, source_line);
}

std::string enrichRuntimeError(const std::string &runtime_error,
                               const std::string &compile_unit_name,
                               const std::string &source) {
  // Try to match "msg at line:col" format from VM_THROW
  static const std::regex at_re(R"((.*) at ([0-9]+):([0-9]+))");
  std::smatch match;
  if (!std::regex_match(runtime_error, match, at_re) || match.size() < 4) {
    return runtime_error;
  }

  const std::string message = match[1].str();
  const size_t line = static_cast<size_t>(std::stoul(match[2].str()));
  const size_t column = static_cast<size_t>(std::stoul(match[3].str()));

  std::string file_path = compile_unit_name.empty() ? "<memory>" : compile_unit_name;

  return ::havel::ErrorPrinter::formatErrorFromFile(
      "Runtime Error", message, file_path, line, column, 1);
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

std::string formatValue(const Value &value) {
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

std::string opcodeName(OpCode opcode) {
  switch (opcode) {
  case OpCode::LOAD_CONST:
    return "LOAD_CONST";
  case OpCode::LOAD_GLOBAL:
    return "LOAD_GLOBAL";
  case OpCode::STORE_GLOBAL:
    return "STORE_GLOBAL";
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
  case OpCode::PUSH_NULL:
    return "PUSH_NULL";
  case OpCode::ADD:
    return "ADD";
  case OpCode::SUB:
    return "SUB";
  case OpCode::MUL:
    return "MUL";
    case OpCode::DIV:
        return "DIV";
    case OpCode::INT_DIV:
        return "INT_DIV";
    case OpCode::MOD:
    return "MOD";
  case OpCode::POW:
    return "POW";
  case OpCode::INCLOCAL:
    return "INCLOCAL";
  case OpCode::DECLOCAL:
    return "DECLOCAL";
  case OpCode::INCLOCAL_POST:
    return "INCLOCAL_POST";
  case OpCode::DECLOCAL_POST:
    return "DECLOCAL_POST";
  case OpCode::EQ:
    return "EQ";
  case OpCode::NEQ:
    return "NEQ";
  case OpCode::IS:
    return "IS";
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
  case OpCode::NEGATE:
    return "NEGATE";
  case OpCode::IS_NULL:
    return "IS_NULL";
  case OpCode::JUMP:
    return "JUMP";
  case OpCode::JUMP_IF_FALSE:
    return "JUMP_IF_FALSE";
  case OpCode::JUMP_IF_TRUE:
    return "JUMP_IF_TRUE";
  case OpCode::JUMP_IF_NULL:
    return "JUMP_IF_NULL";
  case OpCode::CALL:
    return "CALL";
  case OpCode::TAIL_CALL:
	return "TAIL_CALL";
	case OpCode::CALL_METHOD:
    return "CALL_METHOD";
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
  case OpCode::ARRAY_DEL:
    return "ARRAY_DEL";
  case OpCode::ARRAY_PUSH:
    return "ARRAY_PUSH";
  case OpCode::ARRAY_LEN:
    return "ARRAY_LEN";
  case OpCode::ARRAY_FREEZE:
    return "ARRAY_FREEZE";
  case OpCode::SET_SET:
    return "SET_SET";
  case OpCode::SET_DEL:
    return "SET_DEL";
  case OpCode::ITER_NEW:
    return "ITER_NEW";
  case OpCode::ITER_NEXT:
    return "ITER_NEXT";
  case OpCode::RANGE_NEW:
    return "RANGE_NEW";
  case OpCode::RANGE_STEP_NEW:
    return "RANGE_STEP_NEW";

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
  case OpCode::OBJECT_GET_RAW:
    return "OBJECT_GET_RAW";
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
  case OpCode::STRING_SUB:
    return "STRING_SUB";
  case OpCode::STRING_FIND:
    return "STRING_FIND";
  case OpCode::STRING_HAS:
    return "STRING_HAS";
  case OpCode::STRING_STARTS:
    return "STRING_STARTS";
  case OpCode::STRING_ENDS:
    return "STRING_ENDS";
  case OpCode::STRING_SPLIT:
    return "STRING_SPLIT";
  case OpCode::STRING_REPLACE:
    return "STRING_REPLACE";
  case OpCode::STRING_PROMOTE:
    return "STRING_PROMOTE";
  case OpCode::SET_NEW:
    return "SET_NEW";
  case OpCode::OBJECT_NEW:
    return "OBJECT_NEW";
  case OpCode::OBJECT_NEW_UNSORTED:
    return "OBJECT_NEW_UNSORTED";
  case OpCode::OBJECT_GET:
    return "OBJECT_GET";
  case OpCode::OBJECT_SET:
    return "OBJECT_SET";
  case OpCode::STRING_CONCAT:
    return "STRING_CONCAT";
  case OpCode::SPREAD:
    return "SPREAD";
  case OpCode::SPREAD_CALL:
    return "SPREAD_CALL";
  case OpCode::AS_TYPE:
    return "AS_TYPE";
  case OpCode::TO_INT:
    return "TO_INT";
  case OpCode::TO_FLOAT:
    return "TO_FLOAT";
  case OpCode::TO_STRING:
    return "TO_STRING";
  case OpCode::TO_BOOL:
    return "TO_BOOL";
  case OpCode::TYPE_OF:
    return "TYPE_OF";
  case OpCode::PRINT:
    return "PRINT";
  case OpCode::DEBUG:
    return "DEBUG";
  case OpCode::CLASS_NEW:
    return "CLASS_NEW";
  case OpCode::CLASS_GET_FIELD:
    return "CLASS_GET_FIELD";
  case OpCode::CLASS_SET_FIELD:
    return "CLASS_SET_FIELD";
  case OpCode::LOAD_CLASS_PROTO:
    return "LOAD_CLASS_PROTO";
  case OpCode::CALL_SUPER:
    return "CALL_SUPER";
  case OpCode::IMPORT:
    return "IMPORT";
  case OpCode::THREAD_SPAWN:
    return "THREAD_SPAWN";
  case OpCode::THREAD_JOIN:
    return "THREAD_JOIN";
  case OpCode::THREAD_SEND:
    return "THREAD_SEND";
  case OpCode::THREAD_RECEIVE:
    return "THREAD_RECEIVE";
  case OpCode::INTERVAL_START:
    return "INTERVAL_START";
  case OpCode::INTERVAL_STOP:
    return "INTERVAL_STOP";
  case OpCode::TIMEOUT_START:
    return "TIMEOUT_START";
  case OpCode::TIMEOUT_CANCEL:
    return "TIMEOUT_CANCEL";
  case OpCode::YIELD:
    return "YIELD";
  case OpCode::YIELD_RESUME:
    return "YIELD_RESUME";
  case OpCode::GO_ASYNC:
    return "GO_ASYNC";
  case OpCode::CHANNEL_NEW:
    return "CHANNEL_NEW";
  case OpCode::CHANNEL_SEND:
    return "CHANNEL_SEND";
  case OpCode::CHANNEL_RECEIVE:
    return "CHANNEL_RECEIVE";
  case OpCode::CHANNEL_CLOSE:
    return "CHANNEL_CLOSE";
  case OpCode::BEGIN_MODULE:
    return "BEGIN_MODULE";
  case OpCode::END_MODULE:
    return "END_MODULE";
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
        const auto &cv = function.constants[c];
        if (cv.isStringValId()) {
          out << "    [" << c << "] \"" << chunk.getString(cv.asStringValId()) << "\"\n";
        } else {
          out << "    [" << c << "] " << formatValue(cv) << "\n";
        }
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
      COMPILER_THROW("Failed to open snapshot artifact: " +
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

  parser::Parser parser{{.lexer = ::havel::debugging::debug_lexer,
                         .parser = ::havel::debugging::debug_parser,
                         .ast = ::havel::debugging::debug_ast}};
  std::unique_ptr<ast::Program> program;
  try {
    program = parser.produceAST(source);
  } catch (const ::havel::LexError &e) {
    COMPILER_THROW(formatDiagnostic(
        "LexerError", e.what(), options.compile_unit_name, source, e.line,
        e.column, e.length, "unexpected token"));
  } catch (const ::havel::parser::ParseError &e) {
    COMPILER_THROW(formatDiagnostic(
        "ParseError", e.what(), options.compile_unit_name, source, e.line,
        e.column, e.length, "here"));
  }
  if (!program) {
    COMPILER_THROW(
        "Bytecode pipeline failed: parser returned null AST");
  }
  if (parser.hasErrors()) {
    std::string allErrors;
    for (const auto &err : parser.getErrors()) {
      allErrors += formatDiagnostic(
          "ParseError", err.message, options.compile_unit_name, source,
          err.line, err.column, 1, "");
    }
    COMPILER_THROW(allErrors);
  }

	ByteCompiler compiler;
	BytecodeSmokeResult result;
  std::unique_ptr<BytecodeChunk> chunk;
  try {
    chunk = compiler.compile(*program);
    if (!chunk) {
      COMPILER_THROW(
          "Bytecode smoke pipeline failed: compiler returned null chunk");
    }
    result.snapshot.resolver = formatResolverSnapshot(compiler.lexicalResolution());
    result.snapshot.bytecode = formatBytecodeSnapshot(*chunk);
    if (options.debugBytecode) {
            ::havel::debug("=== BYTECODE ===\n{}", result.snapshot.bytecode);
    }
    result.snapshot.artifact_path = writeSnapshotArtifact(result, "");
  } catch (const std::exception &e) {
    std::string formatted = e.what();
    static const std::regex unresolved_re(
        R"(Lexical resolution failed:\s*Unresolved identifier '([^']+)'\s*at\s*([0-9]+):([0-9]+))");
    static const std::regex duplicate_re(
        R"(Lexical resolution failed:\s*Duplicate declaration:\s*'([^']+)'\s*already defined.*?at\s*([0-9]+):([0-9]+))");
    static const std::regex missing_binding_re(
        R"(Missing lexical binding for identifier:\s*(\S+)\s+at line\s*([0-9]+):([0-9]+))");
    std::smatch unresolved_match;
    std::smatch duplicate_match;
    std::smatch missing_binding_match;
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
    } else if (std::regex_search(formatted, missing_binding_match, missing_binding_re) &&
               missing_binding_match.size() >= 4) {
      const std::string symbol = missing_binding_match[1].str();
      const size_t line = static_cast<size_t>(std::stoul(missing_binding_match[2].str()));
      const size_t column = static_cast<size_t>(std::stoul(missing_binding_match[3].str()));
      formatted = formatDiagnostic(
          "SemanticError", "undefined variable '" + symbol + "'",
          options.compile_unit_name, source, line, column,
          std::max<size_t>(1, symbol.size()), "not found in this scope");
    }
    result.snapshot.resolver = formatResolverSnapshot(compiler.lexicalResolution());
    // Try to emit partial bytecode even if compilation failed
    chunk = compiler.takeCurrentChunk();
    if (chunk && !chunk->getAllFunctions().empty()) {
      result.snapshot.bytecode = formatBytecodeSnapshot(*chunk);
      if (options.debugBytecode) {
        ::havel::debug("=== BYTECODE ===\n{}", result.snapshot.bytecode);
    }
    } else {
      result.snapshot.bytecode = "<no bytecode generated>";
    }
    result.snapshot.artifact_path = writeSnapshotArtifact(result, formatted);
    COMPILER_THROW(formatted);
  }

  VM owned_vm;
  VM *vm = options.vm_override ? options.vm_override : &owned_vm;
  for (const auto &[name, fn] : options.host_functions) {
    vm->registerHostFunction(name, fn);
    // registerHostFunction already adds to globals
  }
if (options.vm_setup) {
  options.vm_setup(*vm);
}
// Register protocols and impls from AST with VM
for (const auto &stmt : program->body) {
  if (!stmt) continue;
  if (stmt->kind == ast::NodeType::ProtocolDeclaration) {
    const auto &protDecl =
        static_cast<const ast::ProtocolDeclaration &>(*stmt);
    std::unordered_set<std::string> methodNames;
    for (const auto &method : protDecl.methods) {
      if (method && method->name) {
        methodNames.insert(method->name->symbol);
      }
    }
    if (protDecl.name) {
      vm->registerProtocol(protDecl.name->symbol, methodNames);
    }
  }
  if (stmt->kind == ast::NodeType::ImplDeclaration) {
    const auto &implDecl =
        static_cast<const ast::ImplDeclaration &>(*stmt);
    std::string traitName =
        implDecl.traitName ? implDecl.traitName->symbol : "";
    std::string typeName =
        implDecl.typeName ? implDecl.typeName->symbol : "";
    if (!traitName.empty() && !typeName.empty()) {
      vm->registerProtocolImpl(traitName, typeName);
    }
  }
}
  // Set up system object initializer to run after execute() initializes state
    if (options.system_object_initializer) {
        vm->setSystemObjectInitializer(options.system_object_initializer);
    }
    // Set script directory for relative imports (use compile_unit_name if it's a file path)
    if (!options.compile_unit_name.empty() && options.compile_unit_name != "unit" && options.compile_unit_name != "script") {
        namespace fs = std::filesystem;
        std::string name = options.compile_unit_name;
        auto plusPos = name.find(" + ");
        if (plusPos != std::string::npos) name = name.substr(0, plusPos);
        fs::path p(name);
        if (p.is_absolute() && fs::exists(p)) {
            vm->setCurrentScriptDir(fs::canonical(p).parent_path().string());
        } else if (!p.is_absolute()) {
            fs::path resolved = fs::current_path() / p;
            if (fs::exists(resolved)) {
                vm->setCurrentScriptDir(fs::canonical(resolved).parent_path().string());
            }
        }
    }
 try {
 if (options.vm_override) {
 auto shared_chunk = std::shared_ptr<BytecodeChunk>(std::move(chunk));
 vm->storeMainChunk(shared_chunk);
 result.return_value = vm->execute(*shared_chunk, entry_function);
 } else {
 result.return_value = vm->execute(*chunk, entry_function);
 }
} catch (const ScriptError &e) {
        ::havel::errors::ErrorReporter::instance().errorAt(
            ::havel::errors::ErrorStage::VM, e.message,
            static_cast<size_t>(std::max(1, static_cast<int>(e.line))),
            static_cast<size_t>(std::max(1, static_cast<int>(e.column))));
        if (e.line > 0) {
            throw std::runtime_error(formatDiagnostic(
                "RuntimeError", e.message, options.compile_unit_name, source,
                static_cast<size_t>(e.line), std::max<size_t>(1, e.column),
                1, "uncaught throw"));
        }
        throw std::runtime_error(e.message);
    } catch (const std::exception &e) {
        ::havel::errors::ErrorReporter::instance().error(
            ::havel::errors::ErrorStage::VM, e.what());
        throw std::runtime_error(enrichRuntimeError(e.what(), options.compile_unit_name, source));
    }
  return result;
}

BytecodeSmokeResult runBytecodePipeline(const std::string &source,
                                        const std::string &entry_function) {
  return runBytecodePipeline(source, entry_function, PipelineOptions{});
}

} // namespace havel::compiler
