// src/havel-lang/errors/ErrorSystem.h
//
// Unified error reporting system for the entire Havel pipeline.
// All compiler stages (lexer, parser, semantic, bytecode, VM, runtime)
// report through this single infrastructure while maintaining backward
// compatibility with existing error types.
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <sstream>
#include <exception>
#include "../utils/Logger.hpp"

namespace havel::errors {

// ============================================================================
// ERROR SEVERITY
// ============================================================================

enum class ErrorSeverity {
  Error,
  Warning,
  Info
};

inline const char* errorSeverityToString(ErrorSeverity s) {
  switch (s) {
    case ErrorSeverity::Error:   return "error";
    case ErrorSeverity::Warning: return "warning";
    case ErrorSeverity::Info:    return "info";
  }
  return "unknown";
}

// ============================================================================
// ERROR STAGE - where in the pipeline the error occurred
// ============================================================================

enum class ErrorStage {
  Lexer,
  Parser,
  AST,
  Compiler,
  Bytecode,
  VM,
  Runtime,
  GC,
  Module
};

inline const char* errorStageToString(ErrorStage stage) {
  switch (stage) {
    case ErrorStage::Lexer:    return "Lexer";
    case ErrorStage::Parser:   return "Parser";
    case ErrorStage::AST:      return "AST";
    case ErrorStage::Compiler: return "Compiler";
    case ErrorStage::Bytecode: return "Bytecode";
    case ErrorStage::VM:       return "VM";
    case ErrorStage::Runtime:  return "Runtime";
    case ErrorStage::GC:       return "GC";
    case ErrorStage::Module:   return "Module";
  }
  return "Unknown";
}

// ============================================================================
// SOURCE LOCATION
// ============================================================================

struct SourceLocation {
  size_t line = 0;
  size_t column = 0;
  size_t length = 1;
  std::string file;

  bool hasLocation() const { return line > 0 || column > 0; }

  std::string toString() const {
    std::ostringstream oss;
    if (!file.empty()) oss << file << ":";
    if (hasLocation()) oss << line << ":" << column;
    return oss.str();
  }
};

// ============================================================================
// STACK FRAME - for runtime error stack traces
// ============================================================================

struct StackFrame {
  std::string functionName;
  std::string file;
  size_t line = 0;
  size_t column = 0;

  std::string toString() const {
    std::ostringstream oss;
    if (!functionName.empty()) oss << "at " << functionName;
    else oss << "at <anonymous>";
    if (!file.empty()) oss << " (" << file << ":" << line << ":" << column << ")";
    else if (line > 0) oss << " (line " << line << ":" << column << ")";
    return oss.str();
  }
};

// ============================================================================
// UNIFIED ERROR - represents any error from any stage
// ============================================================================

struct HavelError {
  ErrorSeverity severity;
  ErrorStage stage;
  std::string message;
  SourceLocation location;
  std::string sourceLine;          // The actual source line content for display
  std::vector<StackFrame> stackTrace;  // For runtime errors
  std::string errorCode;           // Machine-readable code like "E001"

  HavelError(ErrorSeverity sev, ErrorStage stg, const std::string& msg)
    : severity(sev), stage(stg), message(msg) {}

  // Fluent builders
  HavelError& at(size_t line, size_t col, size_t len = 1) {
    location.line = line;
    location.column = col;
    location.length = len;
    return *this;
  }

  HavelError& at(const SourceLocation& loc) {
    location = loc;
    return *this;
  }

  HavelError& file(const std::string& f) {
    location.file = f;
    return *this;
  }

  HavelError& sourceLineContent(const std::string& line) {
    sourceLine = line;
    return *this;
  }

  HavelError& withCode(const std::string& code) {
    errorCode = code;
    return *this;
  }

  HavelError& withStackTrace(std::vector<StackFrame> trace) {
    stackTrace = std::move(trace);
    return *this;
  }

  // Formatted output (rust-style caret error)
  std::string what() const {
    std::ostringstream oss;
    oss << errorStageToString(stage) << " " << errorSeverityToString(severity);
    if (!errorCode.empty()) oss << " [" << errorCode << "]";
    oss << ": " << message;

    if (location.hasLocation()) {
      oss << "\n  --> " << location.toString();
      if (!sourceLine.empty()) {
        oss << "\n   |\n";
        oss << location.line << " | " << sourceLine << "\n";
        oss << "   | ";
        for (size_t i = 1; i < location.column; ++i) oss << " ";
        for (size_t i = 0; i < location.length; ++i) oss << "^";
      }
    }

    if (!stackTrace.empty()) {
      oss << "\n\nStack trace:\n";
      for (size_t i = 0; i < stackTrace.size(); ++i) {
        oss << "  " << (i + 1) << ". " << stackTrace[i].toString() << "\n";
      }
    }
    return oss.str();
  }
};

// ============================================================================
// ERROR CODES - machine-readable error identifiers
// ============================================================================

namespace codes {
  // Lexer errors
  constexpr const char* INVALID_CHAR       = "E001";
  constexpr const char* UNTERMINATED_STRING = "E002";
  constexpr const char* UNTERMINATED_COMMENT = "E003";
  constexpr const char* INVALID_NUMBER     = "E004";
  constexpr const char* INVALID_ESCAPE     = "E005";

  // Parser errors
  constexpr const char* UNEXPECTED_TOKEN     = "E010";
  constexpr const char* EXPECTED_EXPRESSION  = "E011";
  constexpr const char* EXPECTED_IDENTIFIER  = "E012";
  constexpr const char* UNTERMINATED_PARENS  = "E013";
  constexpr const char* UNTERMINATED_BRACKET = "E014";
  constexpr const char* UNTERMINATED_BRACE   = "E015";
  constexpr const char* EXPECTED_STATEMENT   = "E016";

  // AST / semantic errors
  constexpr const char* INVALID_TYPE    = "E020";
  constexpr const char* DUPLICATE_FIELD = "E021";
  constexpr const char* INVALID_PATTERN = "E022";

  // Compiler errors
  constexpr const char* UNDEFINED_VARIABLE   = "E030";
  constexpr const char* UNDEFINED_FUNCTION   = "E031";
  constexpr const char* TYPE_MISMATCH        = "E032";
  constexpr const char* ARITY_MISMATCH       = "E033";
  constexpr const char* NOT_CALLABLE         = "E034";
  constexpr const char* NOT_INDEXABLE        = "E035";
  constexpr const char* INVALID_ASSIGN_TARGET = "E036";

  // Bytecode errors
  constexpr const char* INVALID_OPCODE       = "E040";
  constexpr const char* STACK_OVERFLOW       = "E041";
  constexpr const char* STACK_UNDERFLOW      = "E042";
  constexpr const char* INVALID_CONSTANT_IDX = "E043";

  // VM / Runtime errors
  constexpr const char* DIVISION_BY_ZERO    = "E050";
  constexpr const char* INDEX_OUT_OF_BOUNDS = "E051";
  constexpr const char* NULL_DEREFERENCE    = "E052";
  constexpr const char* STACK_OVERFLOW_RT   = "E053";
  constexpr const char* OUT_OF_MEMORY       = "E054";
  constexpr const char* ASSERTION_FAILED    = "E055";

  // Module / import errors
  constexpr const char* MODULE_NOT_FOUND    = "E060";
  constexpr const char* CIRCULAR_IMPORT     = "E061";
}

// ============================================================================
// ERROR REPORTER - central error accumulation and reporting
// ============================================================================

class ErrorReporter {
public:
  static ErrorReporter& instance() {
    static ErrorReporter inst;
    return inst;
  }

  // Report an error
  void report(const HavelError& error) {
    errors_.push_back(error);
    if (error.severity == ErrorSeverity::Error)   errorCount_++;
    if (error.severity == ErrorSeverity::Warning) warningCount_++;
  }

  // Convenience builders
  void error(ErrorStage stage, const std::string& message) {
    report(HavelError(ErrorSeverity::Error, stage, message));
  }

  void errorAt(ErrorStage stage, const std::string& message,
               size_t line, size_t col, size_t len = 1) {
    report(HavelError(ErrorSeverity::Error, stage, message).at(line, col, len));
  }

  void warning(ErrorStage stage, const std::string& message) {
    report(HavelError(ErrorSeverity::Warning, stage, message));
  }

  void info(ErrorStage stage, const std::string& message) {
    report(HavelError(ErrorSeverity::Info, stage, message));
  }

  // Accessors
  const std::vector<HavelError>& errors()  const { return errors_; }
  size_t errorCount()   const { return errorCount_; }
  size_t warningCount() const { return warningCount_; }
  bool hasErrors()      const { return errorCount_ > 0; }

  // Reset
  void clear() {
    errors_.clear();
    errorCount_ = 0;
    warningCount_ = 0;
  }

  // Print all errors to the log
  void printAll() const {
    for (const auto& err : errors_) {
      havel::debug(err.what());
    }
  }

  // Print summary
  void printSummary() const {
    if (errorCount_ > 0 || warningCount_ > 0) {
      std::ostringstream oss;
      if (errorCount_ > 0) oss << errorCount_ << " error(s)";
      if (warningCount_ > 0) {
        if (errorCount_ > 0) oss << ", ";
        oss << warningCount_ << " warning(s)";
      }
      havel::debug(oss.str());
    }
  }

private:
  ErrorReporter() = default;
  std::vector<HavelError> errors_;
  size_t errorCount_ = 0;
  size_t warningCount_ = 0;
};

// ============================================================================
// EXCEPTION WRAPPER - throw a HavelError across boundaries
// ============================================================================

class HavelException : public std::exception {
public:
  explicit HavelException(HavelError error) : error_(std::move(error)) {}
  const HavelError& error() const { return error_; }
  const char* what() const noexcept override {
    whatBuffer_ = error_.what();
    return whatBuffer_.c_str();
  }
private:
  HavelError error_;
  mutable std::string whatBuffer_;
};

// ============================================================================
// CONVERSION HELPERS - bridge old error types to HavelError
// ============================================================================

// Conversion from existing error types to HavelError is handled via
// the dual-reporting mechanism in each component (Lexer, Parser, VM).
// Each component reports to both its native error collection AND
// the unified ErrorReporter simultaneously.

// ============================================================================
// BACKWARD COMPATIBILITY - re-export for legacy code
// ============================================================================

// Re-export severity enum into the havel namespace so existing code
// that does `using havel::ErrorSeverity` still compiles.
namespace detail {
  // These are picked up via the #include chain:
  //   Lexer.hpp  defines `enum class ErrorSeverity { Error, Warning, Info };`
  //   ErrorSystem.h  defines `havel::errors::ErrorSeverity` with identical layout.
  // As long as both use the same underlying values (default = int starting at 0)
  // they are layout-compatible.
}

} // namespace havel::errors

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

#define HAVEL_ERROR(stage, msg) \
  ::havel::errors::HavelError(::havel::errors::ErrorSeverity::Error, stage, msg)

#define HAVEL_ERROR_AT(stage, msg, line, col) \
  ::havel::errors::HavelError(::havel::errors::ErrorSeverity::Error, stage, msg) \
      .at(line, col)

#define REPORT_ERROR(stage, msg) \
  ::havel::errors::ErrorReporter::instance().error(stage, msg)

#define REPORT_ERROR_AT(stage, msg, line, col) \
  ::havel::errors::ErrorReporter::instance().errorAt(stage, msg, line, col)

#define REPORT_WARNING(stage, msg) \
  ::havel::errors::ErrorReporter::instance().warning(stage, msg)

#define REPORT_INFO(stage, msg) \
  ::havel::errors::ErrorReporter::instance().info(stage, msg)

// ============================================================================
// UNIFIED COMPILER-THROW MACRO
//
// All compiler stages should use this instead of their own
// #define COMPILER_THROW.  It throws a std::runtime_error with
// source-file:line annotation (for the enrichRuntimeError pipeline)
// AND simultaneously records the error in the central ErrorReporter.
// ============================================================================

#define HAVEL_THROW(msg) \
  do { \
    ::havel::errors::ErrorReporter::instance().report( \
        HAVEL_ERROR(::havel::errors::ErrorStage::Compiler, msg)); \
    throw std::runtime_error(std::string(msg) + " [" + __FILE__ + ":" + std::to_string(__LINE__) + "]"); \
  } while (0)
