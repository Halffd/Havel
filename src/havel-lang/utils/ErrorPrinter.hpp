#pragma once

#include <string>
#include <vector>

namespace havel {

struct SourceLocation;

class ErrorPrinter {
public:
  // Formats and prints a single error with caret highlighting
  // e.g.
  // Error: undefined variable 'foo'
  //   --> file.hv:12:8
  //    |
  // 12 |   let x = foo + 1
  //    |           ^^^
  static void printError(const std::string& errorType,
                         const std::string& message,
                         const std::string& filename,
                         size_t line,
                         size_t column,
                         size_t length,
                         const std::string& sourceLineContent);

  // Overload to fetch source line from file directly
  static void printErrorFromFile(const std::string& errorType,
                                 const std::string& message,
                                 const std::string& filename,
                                 size_t line,
                                 size_t column,
                                 size_t length);
                                 
  // Try to read a specific line from a file
  static std::string readLineFromFile(const std::string& filename, size_t lineNum);
};

} // namespace havel
