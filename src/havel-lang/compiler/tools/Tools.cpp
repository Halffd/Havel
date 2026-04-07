#include "havel-lang/errors/ErrorSystem.h"
#include "Tools.hpp"
// #include "havel-lang/compiler/tools/// // BytecodeDisassembler.hpp"
#include "havel-lang/compiler/runtime/RuntimeSupport.hpp"
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>

// Macro for throwing errors with source location info
#define COMPILER_THROW(msg) \
  do { \
    ::havel::errors::ErrorReporter::instance().report( \
        HAVEL_ERROR(::havel::errors::ErrorStage::Compiler, msg)); \
    throw std::runtime_error(std::string(msg) + " [" __FILE__ ":" + std::to_string(__LINE__) + "]"); \
  } while (0)

namespace havel::compiler {

// ============================================================================
// REPL Implementation
// ============================================================================

REPL::REPL(const Options& options) : options_(options) {
  CompilationPipeline::Options pipelineOpts;
  pipelineOpts.enableOptimizations = options.enableOptimizations;
  pipelineOpts.strictMode = options.strictMode;
  pipeline_ = std::make_unique<CompilationPipeline>(pipelineOpts);

  contextChunk_ = std::make_unique<BytecodeChunk>();
  // executionContext_ would need a VM reference, simplified here

  registerDefaultCommands();
}

REPL::~REPL() = default;

void REPL::run() {
  run(std::cin, std::cout);
}

void REPL::run(std::istream& input, std::ostream& output) {
  output << "Havel REPL - Type 'help' for commands, 'quit' to exit\n";

  std::string line;
  while (true) {
    if (inMultilineMode_) {
      output << options_.continuationPrompt;
    } else {
      output << options_.prompt;
    }
    output.flush();

    if (!std::getline(input, line)) {
      break;
    }

    if (!processLine(line, output)) {
      break;
    }
  }
}

bool REPL::processLine(const std::string& line, std::ostream& output) {
  // Check for commands
  if (line.empty()) {
    if (inMultilineMode_) {
      // End multiline input
      std::string code = incompleteInput_;
      incompleteInput_.clear();
      inMultilineMode_ = false;

      auto result = evaluate(code);
      if (result.success) {
        printValue(result.value, output);
        output << "\n";
      } else {
        printError(result.error, output);
      }
      addToHistory(code, result);
    }
    return true;
  }

  // Check for commands (start with :)
  if (line[0] == ':') {
    std::string cmdLine = line.substr(1);
    if (executeCommand(cmdLine, output)) {
      return true;
    }
  }

  // Accumulate input
  if (inMultilineMode_) {
    incompleteInput_ += "\n" + line;
  } else {
    incompleteInput_ = line;
  }

  // Check if input is complete
  if (!isCompleteExpression(incompleteInput_)) {
    inMultilineMode_ = true;
    return true;
  }

  // Evaluate complete input
  std::string code = incompleteInput_;
  incompleteInput_.clear();
  inMultilineMode_ = false;

  auto result = evaluate(code);
  if (result.success) {
    printValue(result.value, output);
    output << "\n";
  } else {
    printError(result.error, output);
  }

  addToHistory(code, result);
  return true;
}

REPL::Result REPL::evaluate(const std::string& code) {
  Result result;

  auto startCompile = std::chrono::steady_clock::now();
  auto compileResult = pipeline_->compile(code, "<repl>");
  auto endCompile = std::chrono::steady_clock::now();

  result.compileTimeMs = std::chrono::duration<double, std::milli>(
      endCompile - startCompile).count();

  if (!compileResult.success) {
    result.error = "Compilation failed";
    for (const auto& err : compileResult.errors) {
      result.error += "\n  " + err;
    }
    return result;
  }

  // Execution would happen here with executionContext_
  // Simplified for now
  result.success = true;
  result.value = nullptr;
  result.execTimeMs = 0.0;

  return result;
}

REPL::Result REPL::evaluateExpression(const std::string& expr) {
  // Wrap expression in print
  return evaluate("print(" + expr + ")");
}

REPL::Result REPL::evaluateStatement(const std::string& stmt) {
  return evaluate(stmt);
}

void REPL::addToHistory(const std::string& input, const Result& result) {
  HistoryEntry entry;
  entry.input = input;
  entry.result = result;
  entry.timestamp = std::chrono::system_clock::now();
  history_.push_back(entry);
}

void REPL::saveHistory(const std::string& filename) const {
  std::ofstream file(filename);
  for (const auto& entry : history_) {
    file << entry.input << "\n";
  }
}

void REPL::loadHistory(const std::string& filename) {
  std::ifstream file(filename);
  std::string line;
  while (std::getline(file, line)) {
    // Just add to history without executing
    HistoryEntry entry;
    entry.input = line;
    entry.timestamp = std::chrono::system_clock::now();
    history_.push_back(entry);
  }
}

void REPL::resetContext() {
  contextChunk_ = std::make_unique<BytecodeChunk>();
  // Reset execution context
}

void REPL::registerCommand(const std::string& name, CommandHandler handler) {
  commands_[name] = handler;
}

bool REPL::executeCommand(const std::string& line, std::ostream& output) {
  // Parse command and args
  std::istringstream iss(line);
  std::string cmd;
  iss >> cmd;

  std::vector<std::string> args;
  std::string arg;
  while (iss >> arg) {
    args.push_back(arg);
  }

  auto it = commands_.find(cmd);
  if (it != commands_.end()) {
    it->second(args, output);
    return true;
  }

  return false;
}

std::vector<std::string> REPL::getCompletions(const std::string& partial) const {
  std::vector<std::string> completions;

  for (const auto& [name, _] : commands_) {
    (void)_;
    if (name.find(partial) == 0) {
      completions.push_back(name);
    }
  }

  for (const auto& source : completionSources_) {
    auto sourceCompletions = source();
    for (const auto& c : sourceCompletions) {
      if (c.find(partial) == 0) {
        completions.push_back(c);
      }
    }
  }

  return completions;
}

void REPL::registerCompletionSource(std::function<std::vector<std::string>()> source) {
  completionSources_.push_back(source);
}

void REPL::registerDefaultCommands() {
  registerCommand("help", [this](auto& args, auto& out) { cmdHelp(args, out); });
  registerCommand("quit", [this](auto& args, auto& out) { cmdQuit(args, out); });
  registerCommand("bytecode", [this](auto& args, auto& out) { cmdBytecode(args, out); });
  registerCommand("ast", [this](auto& args, auto& out) { cmdAst(args, out); });
  registerCommand("vars", [this](auto& args, auto& out) { cmdVars(args, out); });
  registerCommand("clear", [this](auto& args, auto& out) { cmdClear(args, out); });
  registerCommand("history", [this](auto& args, auto& out) { cmdHistory(args, out); });
  registerCommand("load", [this](auto& args, auto& out) { cmdLoad(args, out); });
  registerCommand("save", [this](auto& args, auto& out) { cmdSave(args, out); });
  registerCommand("type", [this](auto& args, auto& out) { cmdType(args, out); });
  registerCommand("time", [this](auto& args, auto& out) { cmdTime(args, out); });
  registerCommand("optimize", [this](auto& args, auto& out) { cmdOptimize(args, out); });
  registerCommand("gc", [this](auto& args, auto& out) { cmdGc(args, out); });
}

void REPL::cmdHelp(const std::vector<std::string>& args, std::ostream& output) {
  (void)args;
  output << "Available commands:\n"
         << "  :help          - Show this help\n"
         << "  :quit          - Exit REPL\n"
         << "  :bytecode      - Show bytecode for last expression\n"
         << "  :ast           - Show AST for last expression\n"
         << "  :vars          - Show defined variables\n"
         << "  :clear         - Clear screen and reset context\n"
         << "  :history       - Show command history\n"
         << "  :load <file>   - Load and execute a file\n"
         << "  :save <file>   - Save history to file\n"
         << "  :type <expr>   - Show type of expression\n"
         << "  :time          - Toggle timing display\n"
         << "  :optimize      - Toggle optimizations\n"
         << "  :gc            - Trigger garbage collection\n";
}

void REPL::cmdQuit(const std::vector<std::string>& args, std::ostream& output) {
  (void)args;
  (void)output;
  // Signal to quit
}

void REPL::cmdBytecode(const std::vector<std::string>& args, std::ostream& output) {
  (void)args;
  if (contextChunk_->getFunctionCount() == 0) {
    output << "No bytecode available\n";
    return;
  }
  // // BytecodeDisassembler disassembler(// // BytecodeDisassembler::Options{});
  // TODO: Iterate through functions and disassemble each one
  output << "Bytecode disassembly not fully implemented\n";
}

void REPL::cmdAst(const std::vector<std::string>& args, std::ostream& output) {
  (void)args;
  output << "AST display not yet implemented\n";
}

void REPL::cmdVars(const std::vector<std::string>& args, std::ostream& output) {
  (void)args;
  output << "Defined variables:\n";
  // Would list variables from execution context
}

void REPL::cmdClear(const std::vector<std::string>& args, std::ostream& output) {
  (void)args;
  resetContext();
  // Clear screen (platform dependent)
  output << "\033[2J\033[H";
  output << "Context reset\n";
}

void REPL::cmdHistory(const std::vector<std::string>& args, std::ostream& output) {
  (void)args;
  int i = 1;
  for (const auto& entry : history_) {
    output << i++ << ": " << entry.input.substr(0, 50);
    if (entry.input.length() > 50) output << "...";
    output << "\n";
  }
}

void REPL::cmdLoad(const std::vector<std::string>& args, std::ostream& output) {
  if (args.empty()) {
    output << "Usage: :load <filename>\n";
    return;
  }
  // Load and execute file
  auto result = evaluateStatement(":include \"" + args[0] + "\"");
  if (!result.success) {
    output << "Error loading file: " << result.error << "\n";
  }
}

void REPL::cmdSave(const std::vector<std::string>& args, std::ostream& output) {
  if (args.empty()) {
    output << "Usage: :save <filename>\n";
    return;
  }
  saveHistory(args[0]);
  output << "History saved to " << args[0] << "\n";
}

void REPL::cmdType(const std::vector<std::string>& args, std::ostream& output) {
  if (args.empty()) {
    output << "Usage: :type <expression>\n";
    return;
  }
  std::string expr = args[0];
  for (size_t i = 1; i < args.size(); ++i) {
    expr += " " + args[i];
  }

  auto result = evaluateExpression(expr);
  if (result.success) {
    output << RuntimeTypeSystem::typeName(result.value) << "\n";
  } else {
    output << "Error: " << result.error << "\n";
  }
}

void REPL::cmdTime(const std::vector<std::string>& args, std::ostream& output) {
  (void)args;
  options_.showBytecode = !options_.showBytecode; // Reuse for timing toggle
  output << "Timing display: " << (options_.showBytecode ? "on" : "off") << "\n";
}

void REPL::cmdOptimize(const std::vector<std::string>& args, std::ostream& output) {
  (void)args;
  options_.enableOptimizations = !options_.enableOptimizations;
  pipeline_->setOptions({options_.enableOptimizations, true, options_.strictMode});
  output << "Optimizations: " << (options_.enableOptimizations ? "on" : "off") << "\n";
}

void REPL::cmdGc(const std::vector<std::string>& args, std::ostream& output) {
  (void)args;
  // Trigger GC
  output << "Garbage collection triggered\n";
}

bool REPL::isCompleteExpression(const std::string& input) const {
  // Simple heuristics
  int brackets = 0;
  int braces = 0;
  int parens = 0;

  for (char c : input) {
    switch (c) {
      case '[': brackets++; break;
      case ']': brackets--; break;
      case '{': braces++; break;
      case '}': braces--; break;
      case '(': parens++; break;
      case ')': parens--; break;
    }
  }

  // Check for incomplete structures
  if (brackets > 0 || braces > 0 || parens > 0) {
    return false;
  }

  // Check for statement continuation
  if (input.back() == '\\' || input.back() == ',') {
    return false;
  }

  return true;
}

void REPL::printValue(const Value& value, std::ostream& output) const {
  output << RuntimeTypeSystem::stringify(value);
}

void REPL::printError(const std::string& error, std::ostream& output) const {
  output << "Error: " << error << "\n";
}

// ============================================================================
// TestFramework Implementation
// ============================================================================

TestFramework& TestFramework::instance() {
  static TestFramework instance;
  return instance;
}

void TestFramework::registerSuite(const TestSuite& suite) {
  suites_[suite.name] = suite;
}

void TestFramework::registerTest(const std::string& suiteName, const TestCase& test) {
  auto it = suites_.find(suiteName);
  if (it == suites_.end()) {
    TestSuite suite;
    suite.name = suiteName;
    suite.tests.push_back(test);
    registerSuite(suite);
  } else {
    it->second.tests.push_back(test);
  }
}

TestFramework::SuiteResult TestFramework::runSuite(const std::string& name) {
  auto it = suites_.find(name);
  if (it == suites_.end()) {
    return {};
  }

  const auto& suite = it->second;
  SuiteResult result;
  result.name = name;

  if (suite.setup) suite.setup();

  for (const auto& test : suite.tests) {
    auto testResult = runTestInternal(test);
    result.results.push_back(testResult);
    if (testResult.passed) {
      result.passed++;
    } else {
      result.failed++;
    }
    result.totalTimeMs += testResult.durationMs;
  }

  if (suite.teardown) suite.teardown();

  return result;
}

TestFramework::TestResult TestFramework::runTestInternal(const TestCase& test) {
  TestResult result;
  result.testName = test.name;

  auto start = std::chrono::steady_clock::now();
  try {
    result.passed = test.test();
    if (!result.passed && test.expectedError) {
      result.errorMessage = *test.expectedError;
    }
  } catch (const std::exception& e) {
    if (test.expectedError) {
      result.passed = (e.what() == *test.expectedError);
    } else {
      result.passed = false;
      result.errorMessage = e.what();
    }
  }
  auto end = std::chrono::steady_clock::now();

  result.durationMs = std::chrono::duration<double, std::milli>(end - start).count();
  return result;
}

std::vector<TestFramework::SuiteResult> TestFramework::runAll() {
  std::vector<SuiteResult> results;
  for (const auto& [name, _] : suites_) {
    (void)_;
    results.push_back(runSuite(name));
  }
  return results;
}

std::string TestFramework::generateReport(const std::vector<SuiteResult>& results) const {
  std::ostringstream ss;
  size_t totalPassed = 0;
  size_t totalFailed = 0;
  double totalTime = 0.0;

  ss << "=== Test Report ===\n\n";

  for (const auto& suite : results) {
    ss << "Suite: " << suite.name << "\n";
    ss << "  Passed: " << suite.passed << "/" << (suite.passed + suite.failed) << "\n";
    ss << "  Time: " << suite.totalTimeMs << " ms\n";

    for (const auto& test : suite.results) {
      ss << "  " << (test.passed ? "✓" : "✗") << " " << test.testName;
      if (!test.passed) {
        ss << " (" << test.errorMessage << ")";
      }
      ss << "\n";
    }

    totalPassed += suite.passed;
    totalFailed += suite.failed;
    totalTime += suite.totalTimeMs;
    ss << "\n";
  }

  ss << "=== Summary ===\n";
  ss << "Total: " << totalPassed << " passed, " << totalFailed << " failed\n";
  ss << "Time: " << totalTime << " ms\n";

  return ss.str();
}

void TestFramework::printReport(const std::vector<SuiteResult>& results,
                                 std::ostream& output) const {
  output << generateReport(results);
}

void TestFramework::assertTrue(bool condition, const std::string& message) {
  if (!condition) {
    COMPILER_THROW(message.empty() ? "Expected true" : message);
  }
}

void TestFramework::assertFalse(bool condition, const std::string& message) {
  if (condition) {
    COMPILER_THROW(message.empty() ? "Expected false" : message);
  }
}

void TestFramework::assertEquals(const Value& expected,
                                  const Value& actual,
                                  const std::string& message) {
  if (!RuntimeTypeSystem::equals(expected, actual)) {
    COMPILER_THROW(message.empty() ?
      "Expected " + RuntimeTypeSystem::stringify(expected) +
      " but got " + RuntimeTypeSystem::stringify(actual) : message);
  }
}

void TestFramework::assertNull(const Value& value, const std::string& message) {
  if (!RuntimeTypeSystem::isNull(value)) {
    COMPILER_THROW(message.empty() ? "Expected null" : message);
  }
}

void TestFramework::assertNotNull(const Value& value, const std::string& message) {
  if (RuntimeTypeSystem::isNull(value)) {
    COMPILER_THROW(message.empty() ? "Expected non-null" : message);
  }
}

void TestFramework::assertThrows(std::function<void()> func, const std::string& message) {
  bool threw = false;
  try {
    func();
  } catch (...) {
    threw = true;
  }
  if (!threw) {
    COMPILER_THROW(message.empty() ? "Expected exception" : message);
  }
}

void TestFramework::assertType(const Value& value,
                                RuntimeTypeSystem::Type type,
                                const std::string& message) {
  if (RuntimeTypeSystem::getType(value) != type) {
    COMPILER_THROW(message.empty() ? "Type mismatch" : message);
  }
}

void TestFramework::fail(const std::string& message) {
  COMPILER_THROW(message.empty() ? "Test failed" : message);
}

// ============================================================================
// LSPAdapter Implementation
// ============================================================================

LSPAdapter::LSPAdapter(CompilationPipeline& pipeline) : pipeline_(pipeline) {}

void LSPAdapter::openDocument(const std::string& uri, const std::string& content) {
  documents_[uri] = content;
}

void LSPAdapter::changeDocument(const std::string& uri, const std::string& content) {
  documents_[uri] = content;
}

void LSPAdapter::closeDocument(const std::string& uri) {
  documents_.erase(uri);
}

std::vector<LSPAdapter::Diagnostic> LSPAdapter::getDiagnostics(const std::string& uri) {
  auto it = documents_.find(uri);
  if (it == documents_.end()) {
    return {};
  }

  // Compile and get errors
  auto result = pipeline_.compile(it->second, uri);
  std::vector<Diagnostic> diagnostics;

  for (const auto& error : result.errors) {
    Diagnostic diag;
    diag.message = error;
    diag.severity = 1; // Error
    diagnostics.push_back(diag);
  }

  for (const auto& warning : result.warnings) {
    Diagnostic diag;
    diag.message = warning;
    diag.severity = 2; // Warning
    diagnostics.push_back(diag);
  }

  return diagnostics;
}

std::vector<LSPAdapter::CompletionItem> LSPAdapter::getCompletions(
    const std::string& uri, Position position) {
  (void)uri;
  (void)position;
  // Would analyze AST at position and suggest completions
  return {};
}

std::optional<LSPAdapter::Location> LSPAdapter::getDefinition(
    const std::string& uri, Position position) {
  (void)uri;
  (void)position;
  return std::nullopt;
}

std::optional<LSPAdapter::Location> LSPAdapter::getTypeDefinition(
    const std::string& uri, Position position) {
  (void)uri;
  (void)position;
  return std::nullopt;
}

std::vector<LSPAdapter::Location> LSPAdapter::getReferences(
    const std::string& uri, Position position) {
  (void)uri;
  (void)position;
  return {};
}

std::optional<LSPAdapter::Range> LSPAdapter::getHover(
    const std::string& uri, Position position) {
  (void)uri;
  (void)position;
  return std::nullopt;
}

std::vector<LSPAdapter::SymbolInfo> LSPAdapter::getDocumentSymbols(
    const std::string& uri) {
  (void)uri;
  return {};
}

std::vector<LSPAdapter::SymbolInfo> LSPAdapter::getWorkspaceSymbols(
    const std::string& query) {
  (void)query;
  return {};
}

std::vector<LSPAdapter::TextEdit> LSPAdapter::formatDocument(const std::string& uri) {
  (void)uri;
  return {};
}

std::vector<LSPAdapter::TextEdit> LSPAdapter::formatRange(
    const std::string& uri, Range range) {
  (void)uri;
  (void)range;
  return {};
}

std::vector<LSPAdapter::WorkspaceEdit> LSPAdapter::renameSymbol(
    const std::string& uri, Position position, const std::string& newName) {
  (void)uri;
  (void)position;
  (void)newName;
  return {};
}

std::vector<LSPAdapter::CodeAction> LSPAdapter::getCodeActions(
    const std::string& uri, Range range) {
  (void)uri;
  (void)range;
  return {};
}

// ============================================================================
// DocumentationGenerator Implementation
// ============================================================================

DocumentationGenerator::DocumentationGenerator(const DocOptions& options)
    : options_(options) {}

std::string DocumentationGenerator::generate(const BytecodeChunk& chunk, Format format) {
  std::ostringstream ss;

  for (const auto& func : chunk.getAllFunctions()) {
    switch (format) {
      case Format::Markdown:
        ss << toMarkdown(func) << "\n\n";
        break;
      case Format::HTML:
        ss << toHTML(func) << "\n";
        break;
      case Format::JSON:
        ss << toJSON(func) << ",\n";
        break;
    }
  }

  return ss.str();
}

std::string DocumentationGenerator::generateFunctionDoc(
    const BytecodeFunction& func, Format format) {
  switch (format) {
    case Format::Markdown: return toMarkdown(func);
    case Format::HTML: return toHTML(func);
    case Format::JSON: return toJSON(func);
  }
  return "";
}

std::string DocumentationGenerator::toMarkdown(const BytecodeFunction& func) {
  std::ostringstream ss;
  ss << "## " << func.name << "\n\n";
  ss << "**Arity:** " << func.param_count << "\n\n";
  ss << "**Instructions:** " << func.instructions.size() << "\n\n";
  // Could add more details from debug info
  return ss.str();
}

std::string DocumentationGenerator::toHTML(const BytecodeFunction& func) {
  std::ostringstream ss;
  ss << "<div class=\"function\">\n";
  ss << "  <h2>" << escapeHTML(func.name) << "</h2>\n";
  ss << "  <p>Arity: " << func.param_count << "</p>\n";
  ss << "  <p>Instructions: " << func.instructions.size() << "</p>\n";
  ss << "</div>\n";
  return ss.str();
}

std::string DocumentationGenerator::toJSON(const BytecodeFunction& func) {
  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"name\": \"" << escapeJSON(func.name) << "\",\n";
  ss << "  \"arity\": " << func.param_count << ",\n";
  ss << "  \"instructionCount\": " << func.instructions.size() << "\n";
  ss << "}";
  return ss.str();
}

std::string DocumentationGenerator::escapeHTML(const std::string& text) {
  std::string result;
  for (char c : text) {
    switch (c) {
      case '<': result += "&lt;"; break;
      case '>': result += "&gt;"; break;
      case '&': result += "&amp;"; break;
      case '"': result += "&quot;"; break;
      default: result += c;
    }
  }
  return result;
}

std::string DocumentationGenerator::escapeJSON(const std::string& text) {
  std::string result;
  for (char c : text) {
    switch (c) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += c;
    }
  }
  return result;
}

} // namespace havel::compiler
