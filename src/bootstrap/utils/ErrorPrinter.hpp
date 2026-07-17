#pragma once

#include <string>
#include <vector>

namespace havel {

struct SourceLocation;

class ErrorPrinter {
public:
  // Formats and returns a single error with caret highlighting
  // e.g.
  // \033[1;31mError\033[0m: undefined variable 'foo'
  //   --> file.hv:12:8
  //    |
  // 12 |   let x = foo + 1
  //    |           ^^^
  static std::string formatError(const std::string& errorType,
                                 const std::string& message,
                                 const std::string& filename,
                                 size_t line,
                                 size_t column,
                                 size_t length,
                                 const std::string& sourceLineContent);

  // Overload to fetch source line from file directly
  static std::string formatErrorFromFile(const std::string& errorType,
                                         const std::string& message,
                                         const std::string& filename,
                                         size_t line,
                                         size_t column,
                                         size_t length);
                                 
  // Try to read a specific line from a file
  static std::string readLineFromFile(const std::string& filename, size_t lineNum);
};

} // namespace havel
