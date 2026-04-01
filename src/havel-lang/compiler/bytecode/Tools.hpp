#pragma once

#include "CompilationPipeline.hpp"
#include "VMExecutionContext.hpp"
#include "BytecodeIR.hpp"
#include "RuntimeSupport.hpp"
#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <map>

namespace havel::compiler {

// ============================================================================
// REPL - Read-Eval-Print Loop for interactive Havel execution
// ============================================================================
class REPL {
public:
  struct Options {
    bool showBytecode;
    bool showAst;
    bool enableOptimizations;
    bool strictMode;
    std::string prompt;
    std::string continuationPrompt;
    Options() : showBytecode(false), showAst(false), enableOptimizations(true),
                strictMode(false), prompt("havel> "), continuationPrompt("...> ") {}
  };

  struct Result {
    bool success = false;
    std::string output;
    std::string error;
    BytecodeValue value;
    double compileTimeMs = 0.0;
    double execTimeMs = 0.0;
  };

  struct HistoryEntry {
    std::string input;
    Result result;
    std::chrono::system_clock::time_point timestamp;
  };

  explicit REPL(const Options& options = Options{});
  ~REPL();

  // Main loop
  void run();
  void run(std::istream& input, std::ostream& output);

  // Single evaluation
  Result evaluate(const std::string& code);
  Result evaluateExpression(const std::string& expr);
  Result evaluateStatement(const std::string& stmt);

  // Line-by-line processing for integration
  bool processLine(const std::string& line, std::ostream& output);

  // History management
  void addToHistory(const std::string& input, const Result& result);
  const std::vector<HistoryEntry>& getHistory() const { return history_; }
  void clearHistory() { history_.clear(); }
  void saveHistory(const std::string& filename) const;
  void loadHistory(const std::string& filename);

  // Context management
  void resetContext();
  void saveContext(const std::string& filename) const;
  void loadContext(const std::string& filename);

  // Built-in commands
  using CommandHandler = std::function<void(const std::vector<std::string>& args, std::ostream& output)>;
  void registerCommand(const std::string& name, CommandHandler handler);
  bool executeCommand(const std::string& line, std::ostream& output);

  // Auto-completion
  std::vector<std::string> getCompletions(const std::string& partial) const;
  void registerCompletionSource(std::function<std::vector<std::string>()> source);

  // Accessors
  VMExecutionContext& getExecutionContext() { return *executionContext_; }
  CompilationPipeline& getPipeline() { return *pipeline_; }

private:
  Options options_;
  std::unique_ptr<CompilationPipeline> pipeline_;
  std::unique_ptr<VMExecutionContext> executionContext_;
  std::unique_ptr<BytecodeChunk> contextChunk_;

  std::vector<HistoryEntry> history_;
  std::unordered_map<std::string, CommandHandler> commands_;
  std::vector<std::function<std::vector<std::string>()>> completionSources_;

  std::string incompleteInput_;
  bool inMultilineMode_ = false;

  // Command implementations
  void registerDefaultCommands();
  void cmdHelp(const std::vector<std::string>& args, std::ostream& output);
  void cmdQuit(const std::vector<std::string>& args, std::ostream& output);
  void cmdBytecode(const std::vector<std::string>& args, std::ostream& output);
  void cmdAst(const std::vector<std::string>& args, std::ostream& output);
  void cmdVars(const std::vector<std::string>& args, std::ostream& output);
  void cmdClear(const std::vector<std::string>& args, std::ostream& output);
  void cmdHistory(const std::vector<std::string>& args, std::ostream& output);
  void cmdLoad(const std::vector<std::string>& args, std::ostream& output);
  void cmdSave(const std::vector<std::string>& args, std::ostream& output);
  void cmdType(const std::vector<std::string>& args, std::ostream& output);
  void cmdTime(const std::vector<std::string>& args, std::ostream& output);
  void cmdOptimize(const std::vector<std::string>& args, std::ostream& output);
  void cmdGc(const std::vector<std::string>& args, std::ostream& output);

  // Helper methods
  bool isCompleteExpression(const std::string& input) const;
  void printValue(const BytecodeValue& value, std::ostream& output) const;
  void printError(const std::string& error, std::ostream& output) const;
};

// ============================================================================
// TestFramework - Unit testing framework for compiler classes
// ============================================================================
class TestFramework {
public:
  struct TestCase {
    std::string name;
    std::string description;
    std::function<bool()> test;
    std::optional<std::string> expectedError;
  };

  struct TestSuite {
    std::string name;
    std::vector<TestCase> tests;
    std::function<void()> setup;
    std::function<void()> teardown;
  };

  struct TestResult {
    std::string testName;
    std::string suiteName;
    bool passed = false;
    std::string errorMessage;
    double durationMs = 0.0;
  };

  struct SuiteResult {
    std::string name;
    std::vector<TestResult> results;
    size_t passed = 0;
    size_t failed = 0;
    double totalTimeMs = 0.0;
  };

  static TestFramework& instance();

  // Registration
  void registerSuite(const TestSuite& suite);
  void registerTest(const std::string& suiteName, const TestCase& test);

  // Running
  SuiteResult runSuite(const std::string& name);
  TestResult runTest(const std::string& suiteName, const std::string& testName);
  std::vector<SuiteResult> runAll();

  // Reporting
  std::string generateReport(const std::vector<SuiteResult>& results) const;
  void printReport(const std::vector<SuiteResult>& results, std::ostream& output = std::cout) const;

  // Assertions
  static void assertTrue(bool condition, const std::string& message = "");
  static void assertFalse(bool condition, const std::string& message = "");
  static void assertEquals(const BytecodeValue& expected, const BytecodeValue& actual,
                         const std::string& message = "");
  static void assertNull(const BytecodeValue& value, const std::string& message = "");
  static void assertNotNull(const BytecodeValue& value, const std::string& message = "");
  static void assertThrows(std::function<void()> func, const std::string& message = "");
  static void assertType(const BytecodeValue& value, RuntimeTypeSystem::Type type,
                         const std::string& message = "");
  static void fail(const std::string& message = "");

private:
  TestFramework() = default;
  std::unordered_map<std::string, TestSuite> suites_;

  TestResult runTestInternal(const TestCase& test);
};

// ============================================================================
// LSPAdapter - Language Server Protocol support
// ============================================================================
class LSPAdapter {
public:
  struct Position {
    uint32_t line = 0;
    uint32_t character = 0;
  };

  struct Range {
    Position start;
    Position end;
  };

  struct Location {
    std::string uri;
    Range range;
  };

  struct Diagnostic {
    Range range;
    int severity = 1; // 1=Error, 2=Warning, 3=Info, 4=Hint
    std::string message;
    std::string code;
    std::string source = "havel";
  };

  struct CompletionItem {
    std::string label;
    int kind = 1; // 1=Text, 2=Method, 3=Function, etc.
    std::string detail;
    std::string documentation;
    std::string insertText;
  };

  struct SymbolInfo {
    std::string name;
    int kind = 1; // 1=File, 2=Module, 3=Namespace, etc.
    Location location;
    std::string containerName;
  };

  struct TextEdit {
    Range range;
    std::string newText;
  };

  struct WorkspaceEdit {
    std::map<std::string, std::vector<TextEdit>> changes;
  };

  struct CodeAction {
    std::string title;
    std::string kind;
    std::vector<Diagnostic> diagnostics;
    std::optional<WorkspaceEdit> edit;
    std::optional<std::string> command;
  };

  explicit LSPAdapter(CompilationPipeline& pipeline);

  // Document operations
  void openDocument(const std::string& uri, const std::string& content);
  void changeDocument(const std::string& uri, const std::string& content);
  void closeDocument(const std::string& uri);

  // LSP features
  std::vector<Diagnostic> getDiagnostics(const std::string& uri);
  std::vector<CompletionItem> getCompletions(const std::string& uri, Position position);
  std::optional<Location> getDefinition(const std::string& uri, Position position);
  std::optional<Location> getTypeDefinition(const std::string& uri, Position position);
  std::vector<Location> getReferences(const std::string& uri, Position position);
  std::optional<Range> getHover(const std::string& uri, Position position);
  std::vector<SymbolInfo> getDocumentSymbols(const std::string& uri);
  std::vector<SymbolInfo> getWorkspaceSymbols(const std::string& query);

  // Formatting
  std::vector<TextEdit> formatDocument(const std::string& uri);
  std::vector<TextEdit> formatRange(const std::string& uri, Range range);

  // Refactoring
  std::vector<WorkspaceEdit> renameSymbol(const std::string& uri, Position position,
                                           const std::string& newName);

  // Code actions
  std::vector<CodeAction> getCodeActions(const std::string& uri, Range range);

private:
  CompilationPipeline& pipeline_;
  std::unordered_map<std::string, std::string> documents_;

  // Internal helpers
  std::optional<SourceLocation> lspToSource(Position pos) const;
  Position sourceToLsp(const SourceLocation& loc) const;
};

// ============================================================================
// DocumentationGenerator - Generate API documentation
// ============================================================================
class DocumentationGenerator {
public:
  enum class Format {
    Markdown,
    HTML,
    JSON
  };

  struct DocOptions {
    bool includePrivate;
    bool includeExamples;
    bool includeSourceLinks;
    std::string sourceBaseUrl;
    DocOptions() : includePrivate(false), includeExamples(true), includeSourceLinks(false) {}
  };

  explicit DocumentationGenerator(const DocOptions& options = DocOptions{});

  // Generate from compiled chunk
  std::string generate(const BytecodeChunk& chunk, Format format = Format::Markdown);

  // Generate for specific elements
  std::string generateFunctionDoc(const BytecodeFunction& func, Format format);
  std::string generateTypeDoc(const std::string& typeName, Format format);
  std::string generateModuleDoc(const std::string& moduleName,
                                 const std::vector<BytecodeFunction>& functions,
                                 Format format);

  // Cross-reference generation
  std::string generateIndex(const std::vector<std::string>& symbols, Format format);
  std::string generateSearchIndex(const BytecodeChunk& chunk);

  // Output
  void writeToFile(const std::string& filename, const std::string& content);
  void generateSite(const std::string& outputDir, const BytecodeChunk& chunk);

private:
  DocOptions options_;

  std::string toMarkdown(const BytecodeFunction& func);
  std::string toHTML(const BytecodeFunction& func);
  std::string toJSON(const BytecodeFunction& func);

  std::string escapeHTML(const std::string& text);
  std::string escapeJSON(const std::string& text);
};

} // namespace havel::compiler
